#include "vm.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static unsigned immediate_size(uint8_t op)
{
    switch ((VmOpcode)op) {
        case OP_PUSH:
        case OP_PUSHF:
            return 8u;
        case OP_JMP:
        case OP_JMP_Z:
        case OP_CALL:
            return 2u;
        case OP_ENTER:
        case OP_LOCAL_GET:
        case OP_LOCAL_SET:
        case OP_DEI:
        case OP_DEO:
            return 1u;
        default:
            return 0u;
    }
}

unsigned vm_inst_size(uint8_t op)
{
    return 1u + immediate_size(op);
}

static uint64_t read_be(const uint8_t *p, unsigned width)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < width; i++) v = (v << 8) | (uint64_t)p[i];
    return v;
}

static uint16_t align_up8(uint16_t n)
{
    return (uint16_t)((n + 7u) & ~7u);
}

/* ------------------------------------------------------------------- state */

void vm_init(VmState *vm)
{
    if (!vm) return;
    memset(vm, 0, sizeof(*vm));
    vm->hp = VM_HEAP_START;
}

void vm_free(VmState *vm)
{
    if (!vm) return;
    VmDevice dev = vm->dev;
    vm_init(vm);
    vm->dev = dev;
}

void vm_set_device(VmState *vm, const VmDevice *dev)
{
    if (!vm) return;
    vm->dev = dev ? *dev : (VmDevice){0};
}

/* ------------------------------------------------------------------- image */

VmStatus vm_load_image(VmState *vm, const uint8_t *payload,
                       size_t payload_len, uint16_t entry)
{
    if (!vm || !payload || payload_len == 0u || payload_len > VM_IMAGE_MAX)
        return VM_ERR_BAD_IP;
    if (entry < VM_IMAGE_START ||
        (uint32_t)entry >= (uint32_t)VM_IMAGE_START + (uint32_t)payload_len)
        return VM_ERR_BAD_IP;

    VmDevice dev = vm->dev; /* host wiring is not reset state */
    memset(vm, 0, sizeof(*vm));
    vm->dev = dev;
    memcpy(vm->mem + VM_IMAGE_START, payload, payload_len);
    vm->ip = entry;
    vm->hp = VM_HEAP_START;
    return VM_OK;
}

VmStatus vm_load_rom(VmState *vm, const uint8_t *rom, size_t rom_len)
{
    if (!vm || !rom || rom_len < VM_ROM_HEADER_SIZE) return VM_ERR_BAD_IP;
    if (rom[0] != 0x56 || rom[1] != 0x4D || rom[2] != 0x49 || rom[3] != 0x4D)
        return VM_ERR_BAD_IP;
    if (rom[4] != VM_ROM_VERSION || rom[5] != 0u ||
        rom[6] != 0u || rom[7] != 0u)
        return VM_ERR_BAD_IP;
    for (size_t i = 12u; i < VM_ROM_HEADER_SIZE; i++) {
        if (rom[i] != 0u) return VM_ERR_BAD_IP;
    }

    uint16_t payload_len = (uint16_t)read_be(rom + 8u, 2u);
    uint16_t entry = (uint16_t)read_be(rom + 10u, 2u);
    if (payload_len > VM_IMAGE_MAX ||
        rom_len != (size_t)VM_ROM_HEADER_SIZE + (size_t)payload_len)
        return VM_ERR_BAD_IP;

    return vm_load_image(vm, rom + VM_ROM_HEADER_SIZE, payload_len, entry);
}

/* ---------------------------------------------------------------- stack/ram */

static VmStatus push_val(VmState *vm, int64_t v)
{
    if (vm->sp >= VM_STACK_SIZE) return VM_ERR_STACK_OVERFLOW;
    vm->stack[vm->sp++] = v;
    return VM_OK;
}

static VmStatus pop_val(VmState *vm, int64_t *out)
{
    if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
    *out = vm->stack[--vm->sp];
    return VM_OK;
}

static int target_ok(int64_t addr)
{
    return addr >= 0 && addr < (int64_t)VM_RAM_SIZE;
}

static int mem_range_ok(int64_t addr, unsigned width)
{
    return addr >= 0 && (uint64_t)addr <= (uint64_t)(VM_RAM_SIZE - width);
}

static int find_live_block(const VmState *vm, int64_t addr)
{
    if (addr < 0 || addr > UINT16_MAX) return -1;
    for (uint16_t i = 0; i < vm->block_count; i++) {
        if (vm->blocks[i].live && vm->blocks[i].addr == (uint16_t)addr)
            return (int)i;
    }
    return -1;
}

/* First-fit reuse of a dead block. */
static int find_free_block(const VmState *vm, uint16_t req)
{
    for (uint16_t i = 0; i < vm->block_count; i++) {
        if (!vm->blocks[i].live && vm->blocks[i].size >= req) return (int)i;
    }
    return -1;
}

/* ------------------------------------------------------------- conversion */

double vm_bits_to_f64(int64_t bits)
{
    double d;
    uint64_t u = (uint64_t)bits;
    memcpy(&d, &u, sizeof d);
    return d;
}

int64_t vm_f64_to_bits(double value)
{
    uint64_t u;
    memcpy(&u, &value, sizeof u);
    return (int64_t)u;
}

static int64_t f64_to_i64(double d)
{
    if (isnan(d)) return 0;
    double lo = (double)INT64_MIN;
    double hi = 0x1p63; /* 2^63, one past INT64_MAX */
    if (d >= hi) return INT64_MAX;
    if (d <= lo) return INT64_MIN;
    return (int64_t)d;
}

/* -------------------------------------------------------------- execution */

typedef struct {
    uint8_t op;
    uint16_t arg;   /* zero-extended immediate; full 64-bit PUSH uses raw */
    int64_t raw;    /* 64-bit immediate for PUSH/PUSHF */
    uint16_t next;  /* ip after this instruction */
} Decoded;

static VmStatus decode(VmState *vm, Decoded *out)
{
    uint32_t ip = vm->ip;
    if (ip >= VM_RAM_SIZE) return VM_ERR_BAD_IP;

    uint8_t op = vm->mem[ip];
    unsigned width = immediate_size(op);
    if ((uint64_t)ip + 1u + (uint64_t)width > (uint64_t)VM_RAM_SIZE)
        return VM_ERR_BAD_IP;

    out->op = op;
    out->arg = 0u;
    out->raw = 0;
    out->next = (uint16_t)(ip + 1u + width);

    if (width == 8u) {
        out->raw = (int64_t)read_be(vm->mem + ip + 1u, 8u);
    } else if (width == 2u) {
        out->arg = (uint16_t)read_be(vm->mem + ip + 1u, 2u);
    } else if (width == 1u) {
        out->arg = vm->mem[ip + 1u];
    }
    return VM_OK;
}

VmStatus vm_step(VmState *vm)
{
    if (!vm) return VM_ERR_BAD_IP;
    Decoded in;
    VmStatus st = decode(vm, &in);
    if (st != VM_OK) return st;

    int64_t a = 0, b = 0;
    uint16_t next = in.next;

    switch ((VmOpcode)in.op) {
        case OP_HALT:
            vm->ticks++;
            return VM_HALTED;

        case OP_NOP:
            vm->ip = next;
            vm->ticks++;
            return VM_OK;

        case OP_PUSH:
        case OP_PUSHF:
            st = push_val(vm, in.raw);
            if (st != VM_OK) return st;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;

        case OP_POP:
            st = pop_val(vm, &a);
            if (st != VM_OK) return st;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;

        case OP_DUP:
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            st = push_val(vm, vm->stack[vm->sp - 1u]);
            if (st != VM_OK) return st;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;

        /* Binary ops: validate depth first, then rewrite top two slots in place. */
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_CMP_LT:
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 2u];
            b = vm->stack[vm->sp - 1u];
            if (in.op == OP_ADD)       a = (int64_t)((uint64_t)a + (uint64_t)b);
            else if (in.op == OP_SUB)  a = (int64_t)((uint64_t)a - (uint64_t)b);
            else if (in.op == OP_MUL)  a = (int64_t)((uint64_t)a * (uint64_t)b);
            else                       a = (a < b) ? 1 : 0;
            vm->stack[vm->sp - 2u] = a;
            vm->sp--;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;

        /* Check the divisor before touching the stack. */
        case OP_DIV:
        case OP_MOD: {
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 2u];
            b = vm->stack[vm->sp - 1u];
            if (b == 0) return VM_ERR_DIV_ZERO;
            if (b == -1) a = (in.op == OP_DIV) ? INT64_MIN : 0;
            else         a = (in.op == OP_DIV) ? (a / b) : (a % b);
            vm->stack[vm->sp - 2u] = a;
            vm->sp--;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        case OP_CMP_EQ:
        case OP_CMP_NE:
        case OP_CMP_LE:
        case OP_CMP_GT:
        case OP_CMP_GE:
        case OP_AND:
        case OP_OR:
        case OP_XOR:
        case OP_SHL:
        case OP_SHR:
        case OP_LAND:
        case OP_LOR: {
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 2u];
            b = vm->stack[vm->sp - 1u];
            uint64_t ua = (uint64_t)a;
            uint64_t ub = (uint64_t)b;
            switch ((VmOpcode)in.op) {
                case OP_CMP_EQ: a = (a == b); break;
                case OP_CMP_NE: a = (a != b); break;
                case OP_CMP_LE: a = (a <= b); break;
                case OP_CMP_GT: a = (a > b); break;
                case OP_CMP_GE: a = (a >= b); break;
                case OP_AND:    a = (int64_t)(ua & ub); break;
                case OP_OR:     a = (int64_t)(ua | ub); break;
                case OP_XOR:    a = (int64_t)(ua ^ ub); break;
                case OP_SHL:    a = (int64_t)(ua << (ub & 63u)); break;
                case OP_SHR:    a = (int64_t)(ua >> (ub & 63u)); break;
                case OP_LAND:   a = (a != 0 && b != 0); break;
                default:        a = (a != 0 || b != 0); break; /* OP_LOR */
            }
            vm->stack[vm->sp - 2u] = a;
            vm->sp--;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        case OP_NOT:
        case OP_LNOT: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            a = (in.op == OP_NOT) ? (int64_t)~(uint64_t)a : (a == 0);
            vm->stack[vm->sp - 1u] = a;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        /* ------------------------------------------------ stack shuffling */

        case OP_SWAP:
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            vm->stack[vm->sp - 1u] = vm->stack[vm->sp - 2u];
            vm->stack[vm->sp - 2u] = a;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;

        case OP_OVER:
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            st = push_val(vm, vm->stack[vm->sp - 2u]);
            if (st != VM_OK) return st;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;

        case OP_ROT:
            if (vm->sp < 3u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 3u]; /* [a, b, c] -> [b, c, a] */
            vm->stack[vm->sp - 3u] = vm->stack[vm->sp - 2u];
            vm->stack[vm->sp - 2u] = vm->stack[vm->sp - 1u];
            vm->stack[vm->sp - 1u] = a;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;

        case OP_PICK: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            int64_t n = vm->stack[vm->sp - 1u];
            uint16_t after = (uint16_t)(vm->sp - 1u);
            if (n < 0 || (uint64_t)n >= after) return VM_ERR_BAD_ADDR;
            vm->stack[after] = vm->stack[after - 1u - (uint16_t)n];
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        /* -------------------------------------------------- control flow */

        case OP_JMP:
            if (!target_ok(in.arg)) return VM_ERR_BAD_IP;
            vm->ip = in.arg;
            vm->ticks++;
            return VM_OK;

        case OP_JMP_Z:
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u]; /* peek: validate target before consuming */
            if (a == 0 && !target_ok(in.arg)) return VM_ERR_BAD_IP;
            vm->sp--;
            vm->ip = (a == 0) ? in.arg : next;
            vm->ticks++;
            return VM_OK;

        case OP_JMP_I:
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            if (!target_ok(a)) return VM_ERR_BAD_IP;
            vm->sp--;
            vm->ip = (uint16_t)a;
            vm->ticks++;
            return VM_OK;

        case OP_CALL:
            if (!target_ok(in.arg)) return VM_ERR_BAD_IP;
            if (vm->fsp >= VM_FRAME_COUNT) return VM_ERR_STACK_OVERFLOW;
            vm->frames[vm->fsp++] = (VmFrame){next, vm->fp};
            vm->fp = vm->sp;
            vm->ip = in.arg;
            vm->ticks++;
            return VM_OK;

        case OP_CALL_I:
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            if (!target_ok(a)) return VM_ERR_BAD_IP;
            if (vm->fsp >= VM_FRAME_COUNT) return VM_ERR_STACK_OVERFLOW;
            vm->sp--;
            vm->frames[vm->fsp++] = (VmFrame){next, vm->fp};
            vm->fp = vm->sp;
            vm->ip = (uint16_t)a;
            vm->ticks++;
            return VM_OK;

        case OP_RET: {
            if (vm->fsp == 0u) return VM_ERR_BAD_FRAME;
            if (vm->sp <= vm->fp) return VM_ERR_STACK_UNDERFLOW; /* result required */
            a = vm->stack[vm->sp - 1u];
            VmFrame frame = vm->frames[--vm->fsp];
            vm->sp = vm->fp;
            vm->fp = frame.prev_fp;
            vm->ip = frame.ret_ip;
            vm->stack[vm->sp++] = a; /* result sits at the caller's top */
            vm->ticks++;
            return VM_OK;
        }

        case OP_ENTER: {
            uint32_t need = (uint32_t)vm->sp + (uint32_t)in.arg;
            if (need > VM_STACK_SIZE) return VM_ERR_STACK_OVERFLOW;
            for (uint16_t i = vm->sp; i < (uint16_t)need; i++) vm->stack[i] = 0;
            vm->sp = (uint16_t)need;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        case OP_LOCAL_GET: {
            uint16_t locals = vm->sp > vm->fp ? (uint16_t)(vm->sp - vm->fp) : 0u;
            if (in.arg >= locals) return VM_ERR_BAD_LOCAL;
            st = push_val(vm, vm->stack[vm->fp + in.arg]);
            if (st != VM_OK) return st;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        case OP_LOCAL_SET: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            uint16_t after = (uint16_t)(vm->sp - 1u);
            uint16_t locals = after > vm->fp ? (uint16_t)(after - vm->fp) : 0u;
            if (in.arg >= locals) return VM_ERR_BAD_LOCAL;
            vm->stack[vm->fp + in.arg] = vm->stack[after];
            vm->sp = after;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        /* ----------------------------------------------------------- heap */

        case OP_ALLOC: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            int64_t size = vm->stack[vm->sp - 1u];
            if (size <= 0) return VM_ERR_BAD_ADDR;
            if ((uint64_t)size > (uint64_t)UINT16_MAX - 7u)
                return VM_ERR_HEAP_OVERFLOW;
            uint16_t req = align_up8((uint16_t)size);
            if (req > (uint16_t)(VM_RAM_SIZE - vm->hp))
                return VM_ERR_HEAP_OVERFLOW;

            int reuse = find_free_block(vm, req);
            uint16_t addr;
            if (reuse >= 0) {
                uint16_t old_size = vm->blocks[reuse].size;
                uint16_t rest = (uint16_t)(old_size - req);
                if (rest > 0u && vm->block_count >= VM_BLOCK_COUNT)
                    return VM_ERR_HEAP_OVERFLOW;
                addr = vm->blocks[reuse].addr;
                vm->blocks[reuse].size = req;
                vm->blocks[reuse].live = 1u;
                if (rest > 0u) {
                    vm->blocks[vm->block_count++] =
                        (VmBlock){(uint16_t)(addr + req), rest, 0u};
                }
            } else {
                if (vm->block_count >= VM_BLOCK_COUNT) return VM_ERR_HEAP_OVERFLOW;
                addr = vm->hp;
                vm->hp = (uint16_t)(vm->hp + req);
                vm->blocks[vm->block_count++] = (VmBlock){addr, req, 1u};
            }
            vm->stack[vm->sp - 1u] = (int64_t)addr;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        case OP_FREE: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            int idx = find_live_block(vm, vm->stack[vm->sp - 1u]);
            if (idx < 0) return VM_ERR_BAD_ADDR;
            vm->blocks[idx].live = 0u;
            vm->sp--;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        /* ----------------------------------------------------------- RAM */

        case OP_LOAD8:
        case OP_LOAD16:
        case OP_LOAD32:
        case OP_LOAD32U:
        case OP_LOAD64: {
            unsigned width = 4u;
            if (in.op == OP_LOAD8)         width = 1u;
            else if (in.op == OP_LOAD16)   width = 2u;
            else if (in.op == OP_LOAD64)   width = 8u;
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            if (!mem_range_ok(a, width)) return VM_ERR_BAD_ADDR;
            uint64_t raw = read_be(vm->mem + (size_t)a, width);
            if (in.op == OP_LOAD32) raw = (uint64_t)(int64_t)(int32_t)(uint32_t)raw;
            vm->stack[vm->sp - 1u] = (int64_t)raw;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        case OP_STORE8:
        case OP_STORE16:
        case OP_STORE32:
        case OP_STORE64: {
            unsigned width = 4u;
            if (in.op == OP_STORE8)        width = 1u;
            else if (in.op == OP_STORE16)  width = 2u;
            else if (in.op == OP_STORE64)  width = 8u;
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u]; /* addr on top */
            b = vm->stack[vm->sp - 2u]; /* val */
            if (!mem_range_ok(a, width)) return VM_ERR_BAD_ADDR;
            uint64_t v = (uint64_t)b;
            for (unsigned i = 0; i < width; i++) {
                vm->mem[(size_t)a + width - 1u - i] = (uint8_t)(v & 0xFFu);
                v >>= 8;
            }
            vm->sp -= 2u;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        /* --------------------------------------------------------- floats */

        case OP_FADD:
        case OP_FSUB:
        case OP_FMUL:
        case OP_FDIV:
        case OP_FCMP_LT:
        case OP_FCMP_EQ:
        case OP_FCMP_LE:
        case OP_FCMP_GT:
        case OP_FCMP_GE: {
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 2u];
            b = vm->stack[vm->sp - 1u];
            double x = vm_bits_to_f64(a);
            double y = vm_bits_to_f64(b);
            switch ((VmOpcode)in.op) {
                case OP_FADD:    a = vm_f64_to_bits(x + y); break;
                case OP_FSUB:    a = vm_f64_to_bits(x - y); break;
                case OP_FMUL:    a = vm_f64_to_bits(x * y); break;
                case OP_FDIV:    a = vm_f64_to_bits(x / y); break;
                case OP_FCMP_LT: a = (x < y); break;
                case OP_FCMP_EQ: a = (x == y); break;
                case OP_FCMP_LE: a = (x <= y); break;
                case OP_FCMP_GT: a = (x > y); break;
                default:         a = (x >= y); break; /* OP_FCMP_GE */
            }
            vm->stack[vm->sp - 2u] = a;
            vm->sp--;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        case OP_FNEG:
        case OP_FABS:
        case OP_FSQRT:
        case OP_FTOI:
        case OP_ITOF: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            if (in.op == OP_FTOI) {
                a = f64_to_i64(vm_bits_to_f64(a));
            } else if (in.op == OP_ITOF) {
                a = vm_f64_to_bits((double)a);
            } else {
                double x = vm_bits_to_f64(a);
                if (in.op == OP_FNEG)       x = -x;
                else if (in.op == OP_FABS)  x = fabs(x);
                else                        x = sqrt(x);
                a = vm_f64_to_bits(x);
            }
            vm->stack[vm->sp - 1u] = a;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        /* ------------------------------------------------------ device layer */

        case OP_DEI: {
            if (vm->sp >= VM_STACK_SIZE) return VM_ERR_STACK_OVERFLOW;
            if (!vm->dev.read) return VM_ERR_IO;
            int64_t value = 0;
            VmStatus dr = vm->dev.read(vm->dev.ctx, vm, in.arg, &value);
            if (dr == VM_ERR_IO) return dr;
            if (dr != VM_OK && dr != VM_HALTED) return VM_ERR_IO;
            if (dr == VM_HALTED) { vm->ticks++; return VM_HALTED; }
            vm->stack[vm->sp++] = value;
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        case OP_DEO: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            if (!vm->dev.write) return VM_ERR_IO;
            int64_t value = vm->stack[vm->sp - 1u];
            VmStatus dw = vm->dev.write(vm->dev.ctx, vm, in.arg, value);
            if (dw == VM_ERR_IO) return dw;
            if (dw != VM_OK && dw != VM_HALTED) return VM_ERR_IO;
            vm->sp--;
            if (dw == VM_HALTED) { vm->ticks++; return VM_HALTED; }
            vm->ip = next;
            vm->ticks++;
            return VM_OK;
        }

        default:
            return VM_ERR_BAD_OP;
    }
}

VmStatus vm_run(VmState *vm, uint64_t max_steps)
{
    uint64_t steps = 0;
    while (max_steps == 0u || steps < max_steps) {
        VmStatus st = vm_step(vm);
        steps++;
        if (st != VM_OK) return st;
    }
    return VM_OK;
}

const char *vm_status_str(VmStatus status)
{
    switch (status) {
        case VM_OK:                 return "VM_OK";
        case VM_HALTED:             return "VM_HALTED";
        case VM_ERR_BAD_IP:         return "VM_ERR_BAD_IP";
        case VM_ERR_BAD_OP:         return "VM_ERR_BAD_OP";
        case VM_ERR_STACK_OVERFLOW: return "VM_ERR_STACK_OVERFLOW";
        case VM_ERR_STACK_UNDERFLOW:return "VM_ERR_STACK_UNDERFLOW";
        case VM_ERR_HEAP_OVERFLOW:  return "VM_ERR_HEAP_OVERFLOW";
        case VM_ERR_BAD_ADDR:       return "VM_ERR_BAD_ADDR";
        case VM_ERR_DIV_ZERO:       return "VM_ERR_DIV_ZERO";
        case VM_ERR_IO:             return "VM_ERR_IO";
        case VM_ERR_BAD_FRAME:      return "VM_ERR_BAD_FRAME";
        case VM_ERR_BAD_LOCAL:      return "VM_ERR_BAD_LOCAL";
        default:                    return "VM_ERR_UNKNOWN";
    }
}

const char *vm_opcode_name(uint8_t op)
{
    static const char *names[VM_OPCODE_MAX + 1u] = {
        [OP_HALT] = "HALT", [OP_NOP] = "NOP", [OP_PUSH] = "PUSH",
        [OP_POP] = "POP", [OP_DUP] = "DUP", [OP_ADD] = "ADD",
        [OP_SUB] = "SUB", [OP_MUL] = "MUL", [OP_CMP_LT] = "CMP_LT",
        [OP_JMP] = "JMP", [OP_JMP_Z] = "JMP_Z", [OP_ALLOC] = "ALLOC",
        [OP_LOAD32] = "LOAD32", [OP_STORE32] = "STORE32",
        [OP_DIV] = "DIV", [OP_MOD] = "MOD", [OP_CMP_EQ] = "CMP_EQ",
        [OP_CMP_NE] = "CMP_NE", [OP_CMP_LE] = "CMP_LE", [OP_CMP_GT] = "CMP_GT",
        [OP_CMP_GE] = "CMP_GE", [OP_AND] = "AND", [OP_OR] = "OR",
        [OP_XOR] = "XOR", [OP_NOT] = "NOT", [OP_SHL] = "SHL",
        [OP_SHR] = "SHR", [OP_LNOT] = "LNOT", [OP_LAND] = "LAND",
        [OP_LOR] = "LOR", [OP_SWAP] = "SWAP", [OP_OVER] = "OVER",
        [OP_ROT] = "ROT", [OP_PICK] = "PICK", [OP_LOAD8] = "LOAD8",
        [OP_STORE8] = "STORE8", [OP_LOAD16] = "LOAD16", [OP_STORE16] = "STORE16",
        [OP_LOAD32U] = "LOAD32U", [OP_LOAD64] = "LOAD64", [OP_STORE64] = "STORE64",
        [OP_FREE] = "FREE", [OP_CALL] = "CALL", [OP_CALL_I] = "CALL_I",
        [OP_RET] = "RET", [OP_ENTER] = "ENTER", [OP_LOCAL_GET] = "LOCAL_GET",
        [OP_LOCAL_SET] = "LOCAL_SET", [OP_JMP_I] = "JMP_I", [OP_PUSHF] = "PUSHF",
        [OP_FADD] = "FADD", [OP_FSUB] = "FSUB", [OP_FMUL] = "FMUL",
        [OP_FDIV] = "FDIV", [OP_FNEG] = "FNEG", [OP_FABS] = "FABS",
        [OP_FSQRT] = "FSQRT", [OP_FCMP_LT] = "FCMP_LT", [OP_FCMP_EQ] = "FCMP_EQ",
        [OP_FCMP_LE] = "FCMP_LE", [OP_FCMP_GT] = "FCMP_GT", [OP_FCMP_GE] = "FCMP_GE",
        [OP_FTOI] = "FTOI", [OP_ITOF] = "ITOF", [OP_DEI] = "DEI", [OP_DEO] = "DEO"
    };
    if (op > VM_OPCODE_MAX || names[op] == NULL) return "RESERVED";
    return names[op];
}
