#include "vm.h"

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Host-side output policy: tick prefixes are off unless explicitly enabled. */
static int debug_ticks = 0;

void vm_set_debug_ticks(int enabled)
{
    debug_ticks = enabled != 0;
}

static uint64_t align_up4(uint64_t n)
{
    return (n + 3u) & ~3u;
}

/* Buffer sizing helpers: refuse sizes that cannot be represented by size_t. */
static int size_ok(uint64_t n)
{
    return n <= (uint64_t)SIZE_MAX;
}

/* ------------------------------------------------------------------ state */

static VmStatus grow_array(void **ptr, uint32_t *cap, uint32_t need, size_t elem)
{
    if (need <= *cap) return VM_OK;
    uint64_t next = *cap ? *cap : 8u;
    while (next < need) {
        if (next > UINT32_MAX / 2u) { next = need; break; }
        next *= 2u;
    }
    if (next > UINT32_MAX || !size_ok(next * elem)) return VM_ERR_HEAP_OVERFLOW;
    void *p = realloc(*ptr, (size_t)(next * elem));
    if (!p) return VM_ERR_HEAP_OVERFLOW;
    memset((uint8_t *)p + (size_t)*cap * elem, 0,
           (size_t)(next - *cap) * elem);
    *ptr = p;
    *cap = (uint32_t)next;
    return VM_OK;
}

static VmStatus stack_ensure(VmState *vm, uint32_t need)
{
    return grow_array((void **)&vm->stack, &vm->stack_cap, need, sizeof *vm->stack);
}

static VmStatus frame_ensure(VmState *vm, uint32_t need)
{
    return grow_array((void **)&vm->frames, &vm->frame_cap, need, sizeof *vm->frames);
}

static VmStatus block_ensure(VmState *vm, uint32_t need)
{
    return grow_array((void **)&vm->blocks, &vm->block_cap, need, sizeof *vm->blocks);
}

void vm_init(VmState *vm)
{
    if (!vm) return;
    memset(vm, 0, sizeof(*vm));
}

void vm_free(VmState *vm)
{
    if (!vm) return;
    free(vm->stack);
    free(vm->heap);
    free(vm->frames);
    free(vm->blocks);
    memset(vm, 0, sizeof(*vm));
}

static VmStatus push_val(VmState *vm, int64_t v)
{
    if (vm->sp == UINT32_MAX) return VM_ERR_STACK_OVERFLOW;
    VmStatus st = stack_ensure(vm, vm->sp + 1u);
    if (st != VM_OK) return st;
    vm->stack[vm->sp++] = v;
    return VM_OK;
}

static VmStatus pop_val(VmState *vm, int64_t *out)
{
    if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
    *out = vm->stack[--vm->sp];
    return VM_OK;
}

/* ------------------------------------------------------------------- heap */

/* Grow the heap so that at least `needed` bytes are allocated. */
static VmStatus heap_grow(VmState *vm, uint64_t needed)
{
    if (needed <= vm->heap_cap) return VM_OK;
    if (!size_ok(needed)) return VM_ERR_HEAP_OVERFLOW;

    uint64_t cap = vm->heap_cap ? vm->heap_cap : VM_HEAP_INIT_CAP;
    while (cap < needed) {
        if (cap > SIZE_MAX / 2u) { cap = needed; break; }
        cap *= 2u;
    }
    if (!size_ok(cap)) return VM_ERR_HEAP_OVERFLOW;

    uint8_t *p = realloc(vm->heap, (size_t)cap);
    if (!p) return VM_ERR_HEAP_OVERFLOW;
    if (cap > vm->heap_cap) {
        memset(p + vm->heap_cap, 0, (size_t)(cap - vm->heap_cap));
    }
    vm->heap = p;
    vm->heap_cap = cap;
    return VM_OK;
}

/* Heap accesses must stay inside the used region of the buffer: [0, hp). */
static int heap_range_ok(const VmState *vm, int64_t addr, uint64_t size)
{
    if (addr < 0 || size == 0u) return 0;
    uint64_t a = (uint64_t)addr;
    return vm->hp >= size && a <= vm->hp - size;
}

static uint64_t read_be(const uint8_t *p, unsigned width)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < width; i++) v = (v << 8) | (uint64_t)p[i];
    return v;
}

static void write_be(uint8_t *p, uint64_t v, unsigned width)
{
    for (unsigned i = 0; i < width; i++) {
        p[width - 1u - i] = (uint8_t)(v & 0xFFu);
        v >>= 8;
    }
}

static int find_live_block(const VmState *vm, int64_t addr)
{
    if (addr < 0) return -1;
    uint64_t a = (uint64_t)addr;
    for (uint32_t i = 0; i < vm->block_count; i++) {
        if (vm->blocks[i].live && vm->blocks[i].addr == a) return (int)i;
    }
    return -1;
}

/* Find a dead block that can hold `req` bytes (first fit). */
static int find_free_block(const VmState *vm, uint64_t req)
{
    for (uint32_t i = 0; i < vm->block_count; i++) {
        if (!vm->blocks[i].live && vm->blocks[i].size >= req) return (int)i;
    }
    return -1;
}

static int target_ok(int64_t arg, size_t prog_len)
{
    return arg >= 0 && (uint64_t)arg <= UINT32_MAX && (uint64_t)arg < prog_len;
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

VmStatus vm_step(VmState *vm, const VmInst *prog, size_t prog_len)
{
    if (!vm || !prog || vm->ip >= prog_len) return VM_ERR_BAD_IP;

    VmInst inst = prog[vm->ip];
    uint32_t next_ip = vm->ip + 1u;
    int64_t a = 0, b = 0;
    VmStatus st = VM_OK;

    switch ((VmOpcode)inst.op) {
        case OP_HALT:
            vm->ticks++;
            return VM_HALTED;

        case OP_NOP:
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_PUSH:
            st = push_val(vm, inst.arg);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_PUSHF:
            st = push_val(vm, inst.arg); /* argument carries raw IEEE-754 bits */
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_POP:
            st = pop_val(vm, &a);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_DUP:
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            st = push_val(vm, vm->stack[vm->sp - 1u]);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        /* Binary ops: check depth first so a failure leaves the VM untouched. */
        case OP_ADD:
        case OP_SUB:
        case OP_MUL:
        case OP_CMP_LT:
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            (void)pop_val(vm, &b);
            (void)pop_val(vm, &a);
            if (inst.op == OP_ADD) {
                a = (int64_t)((uint64_t)a + (uint64_t)b);
            } else if (inst.op == OP_SUB) {
                a = (int64_t)((uint64_t)a - (uint64_t)b);
            } else if (inst.op == OP_MUL) {
                a = (int64_t)((uint64_t)a * (uint64_t)b);
            } else {
                a = (a < b) ? 1 : 0;
            }
            st = push_val(vm, a);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        /* Signed division and remainder. Check the divisor before touching the
         * stack so VM_ERR_DIV_ZERO leaves the VM completely unchanged. */
        case OP_DIV:
        case OP_MOD: {
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            b = vm->stack[vm->sp - 1u];
            if (b == 0) return VM_ERR_DIV_ZERO;
            (void)pop_val(vm, &b);
            (void)pop_val(vm, &a);

            int64_t r;
            if (b == -1) {
                r = (inst.op == OP_DIV) ? INT64_MIN : 0; /* avoid signed UB */
            } else {
                r = (inst.op == OP_DIV) ? (a / b) : (a % b);
            }
            st = push_val(vm, r);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        /* Additional comparisons plus bitwise and logical binary ops. */
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
            (void)pop_val(vm, &b);
            (void)pop_val(vm, &a);

            uint64_t ua = (uint64_t)a;
            uint64_t ub = (uint64_t)b;
            int64_t r;
            switch ((VmOpcode)inst.op) {
                case OP_CMP_EQ: r = (a == b); break;
                case OP_CMP_NE: r = (a != b); break;
                case OP_CMP_LE: r = (a <= b); break;
                case OP_CMP_GT: r = (a > b); break;
                case OP_CMP_GE: r = (a >= b); break;
                case OP_AND:    r = (int64_t)(ua & ub); break;
                case OP_OR:     r = (int64_t)(ua | ub); break;
                case OP_XOR:    r = (int64_t)(ua ^ ub); break;
                case OP_SHL:    r = (int64_t)(ua << (ub & 63u)); break;
                case OP_SHR:    r = (int64_t)(ua >> (ub & 63u)); break;
                case OP_LAND:   r = (a != 0 && b != 0); break;
                default:        r = (a != 0 || b != 0); break; /* OP_LOR */
            }
            st = push_val(vm, r);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        case OP_NOT:
        case OP_LNOT: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            (void)pop_val(vm, &a);
            a = (inst.op == OP_NOT) ? (int64_t)~(uint64_t)a : (a == 0);
            st = push_val(vm, a);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        /* ------------------------------------------------ stack shuffling */

        case OP_SWAP:
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            vm->stack[vm->sp - 1u] = vm->stack[vm->sp - 2u];
            vm->stack[vm->sp - 2u] = a;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_OVER:
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            st = push_val(vm, vm->stack[vm->sp - 2u]);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_ROT:
            if (vm->sp < 3u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 3u]; /* [a, b, c] -> [b, c, a] */
            vm->stack[vm->sp - 3u] = vm->stack[vm->sp - 2u];
            vm->stack[vm->sp - 2u] = vm->stack[vm->sp - 1u];
            vm->stack[vm->sp - 1u] = a;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_PICK: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            int64_t n = vm->stack[vm->sp - 1u]; /* peek: validate before pop */
            uint32_t after = vm->sp - 1u;
            if (n < 0 || (uint64_t)n >= after) return VM_ERR_BAD_ADDR;
            (void)pop_val(vm, &n);
            st = push_val(vm, vm->stack[after - 1u - (uint32_t)n]);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        /* ---------------------------------------------------- control flow */

        case OP_JMP:
            if (!target_ok(inst.arg, prog_len)) return VM_ERR_BAD_IP;
            vm->ip = (uint32_t)inst.arg;
            vm->ticks++;
            return VM_OK;

        case OP_JMP_Z:
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u]; /* peek: validate target before consuming */
            if (a == 0 && !target_ok(inst.arg, prog_len)) return VM_ERR_BAD_IP;
            (void)pop_val(vm, &a);
            vm->ip = (a == 0) ? (uint32_t)inst.arg : next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_JMP_I:
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            if (!target_ok(a, prog_len)) return VM_ERR_BAD_IP;
            (void)pop_val(vm, &a);
            vm->ip = (uint32_t)a;
            vm->ticks++;
            return VM_OK;

        case OP_CALL:
            if (!target_ok(inst.arg, prog_len)) return VM_ERR_BAD_IP;
            st = frame_ensure(vm, vm->fsp + 1u);
            if (st != VM_OK) return st;
            vm->frames[vm->fsp++] = (VmFrame){next_ip, vm->fp};
            vm->fp = vm->sp;
            vm->ip = (uint32_t)inst.arg;
            vm->ticks++;
            return VM_OK;

        case OP_CALL_I:
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            if (!target_ok(a, prog_len)) return VM_ERR_BAD_IP;
            st = frame_ensure(vm, vm->fsp + 1u);
            if (st != VM_OK) return st;
            (void)pop_val(vm, &a);
            vm->frames[vm->fsp++] = (VmFrame){next_ip, vm->fp};
            vm->fp = vm->sp;
            vm->ip = (uint32_t)a;
            vm->ticks++;
            return VM_OK;

        case OP_RET: {
            if (vm->fsp == 0u) return VM_ERR_BAD_FRAME;
            if (vm->sp <= vm->fp) return VM_ERR_STACK_UNDERFLOW; /* result required */
            a = vm->stack[vm->sp - 1u];
            VmFrame frame = vm->frames[vm->fsp - 1u];
            vm->sp = vm->fp;
            vm->fp = frame.prev_fp;
            vm->ip = frame.ret_ip;
            vm->fsp--;
            vm->stack[vm->sp++] = a; /* result sits at the caller's top */
            vm->ticks++;
            return VM_OK;
        }

        case OP_ENTER: {
            if (inst.arg < 0) return VM_ERR_BAD_LOCAL;
            uint64_t need = (uint64_t)vm->sp + (uint64_t)inst.arg;
            if (need > UINT32_MAX) return VM_ERR_STACK_OVERFLOW;
            st = stack_ensure(vm, (uint32_t)need);
            if (st != VM_OK) return st;
            for (uint32_t i = vm->sp; i < (uint32_t)need; i++) vm->stack[i] = 0;
            vm->sp = (uint32_t)need;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        case OP_LOCAL_GET: {
            if (inst.arg < 0) return VM_ERR_BAD_LOCAL;
            uint32_t locals = vm->sp > vm->fp ? vm->sp - vm->fp : 0u;
            if ((uint64_t)inst.arg >= locals) return VM_ERR_BAD_LOCAL;
            st = push_val(vm, vm->stack[vm->fp + (uint32_t)inst.arg]);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        case OP_LOCAL_SET: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            uint32_t after = vm->sp - 1u;
            uint32_t locals = after > vm->fp ? after - vm->fp : 0u;
            if (inst.arg < 0 || (uint64_t)inst.arg >= locals) return VM_ERR_BAD_LOCAL;
            b = vm->stack[vm->sp - 1u];
            (void)pop_val(vm, &b);
            vm->stack[vm->fp + (uint32_t)inst.arg] = b;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        /* ----------------------------------------------------------- heap */

        case OP_ALLOC: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            if (a <= 0) return VM_ERR_BAD_ADDR;
            if ((uint64_t)a > UINT64_MAX - 3u) return VM_ERR_HEAP_OVERFLOW;
            uint64_t req = align_up4((uint64_t)a);
            st = stack_ensure(vm, vm->sp + 1u); /* room for the returned address */
            if (st != VM_OK) return st;

            uint64_t addr;
            int reuse = find_free_block(vm, req);
            if (reuse >= 0) {
                uint64_t old_size = vm->blocks[reuse].size;
                uint64_t rest = old_size - req;
                if (rest > 0u) {
                    st = block_ensure(vm, vm->block_count + 1u);
                    if (st != VM_OK) return st;
                }
                addr = vm->blocks[reuse].addr;
                vm->blocks[reuse].size = req;
                vm->blocks[reuse].live = 1u;
                if (rest > 0u) {
                    vm->blocks[vm->block_count++] =
                        (VmBlock){addr + req, rest, 0u};
                }
            } else {
                if (req > UINT64_MAX - vm->hp) return VM_ERR_HEAP_OVERFLOW;
                st = block_ensure(vm, vm->block_count + 1u);
                if (st != VM_OK) return st;
                st = heap_grow(vm, vm->hp + req);
                if (st != VM_OK) return st;
                addr = vm->hp;
                vm->hp += req;
                vm->blocks[vm->block_count++] = (VmBlock){addr, req, 1u};
            }

            (void)pop_val(vm, &a);
            (void)push_val(vm, (int64_t)addr);
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        case OP_FREE: {
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            if (find_live_block(vm, a) < 0) return VM_ERR_BAD_ADDR;
            (void)pop_val(vm, &a);
            vm->blocks[find_live_block(vm, a)].live = 0u;
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        case OP_LOAD8:
        case OP_LOAD16:
        case OP_LOAD32:
        case OP_LOAD32U:
        case OP_LOAD64: {
            unsigned width;
            switch ((VmOpcode)inst.op) {
                case OP_LOAD8:   width = 1u; break;
                case OP_LOAD16:  width = 2u; break;
                case OP_LOAD64:  width = 8u; break;
                default:         width = 4u; break;
            }
            if (vm->sp == 0u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u];
            if (!heap_range_ok(vm, a, width)) return VM_ERR_BAD_ADDR;
            st = stack_ensure(vm, vm->sp + 1u);
            if (st != VM_OK) return st;
            (void)pop_val(vm, &a);
            uint64_t raw = read_be(vm->heap + (uint64_t)a, width);
            if (inst.op == OP_LOAD32) raw = (uint64_t)(int64_t)(int32_t)(uint32_t)raw;
            (void)push_val(vm, (int64_t)raw);
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        case OP_STORE8:
        case OP_STORE16:
        case OP_STORE32:
        case OP_STORE64: {
            unsigned width;
            switch ((VmOpcode)inst.op) {
                case OP_STORE8:  width = 1u; break;
                case OP_STORE16: width = 2u; break;
                case OP_STORE64: width = 8u; break;
                default:         width = 4u; break;
            }
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            a = vm->stack[vm->sp - 1u]; /* addr on top */
            b = vm->stack[vm->sp - 2u]; /* val */
            if (!heap_range_ok(vm, a, width)) return VM_ERR_BAD_ADDR;
            (void)pop_val(vm, &a);
            (void)pop_val(vm, &b);
            write_be(vm->heap + (uint64_t)a, (uint64_t)b, width);
            vm->ip = next_ip;
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
            (void)pop_val(vm, &b);
            (void)pop_val(vm, &a);
            double x = vm_bits_to_f64(a);
            double y = vm_bits_to_f64(b);
            int64_t r;
            switch ((VmOpcode)inst.op) {
                case OP_FADD:    r = vm_f64_to_bits(x + y); break;
                case OP_FSUB:    r = vm_f64_to_bits(x - y); break;
                case OP_FMUL:    r = vm_f64_to_bits(x * y); break;
                case OP_FDIV:    r = vm_f64_to_bits(x / y); break;
                case OP_FCMP_LT: r = (x < y); break;
                case OP_FCMP_EQ: r = (x == y); break;
                case OP_FCMP_LE: r = (x <= y); break;
                case OP_FCMP_GT: r = (x > y); break;
                default:         r = (x >= y); break; /* OP_FCMP_GE */
            }
            st = push_val(vm, r);
            if (st != VM_OK) return st;
            vm->ip = next_ip;
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
            int64_t r;
            if (inst.op == OP_FTOI) {
                r = f64_to_i64(vm_bits_to_f64(a));
            } else if (inst.op == OP_ITOF) {
                r = vm_f64_to_bits((double)a);
            } else {
                double x = vm_bits_to_f64(a);
                if (inst.op == OP_FNEG)       x = -x;
                else if (inst.op == OP_FABS)  x = fabs(x);
                else                          x = sqrt(x);
                r = vm_f64_to_bits(x);
            }
            vm->stack[vm->sp - 1u] = r; /* unary: replace in place */
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        /* ------------------------------------------------------------- I/O */

        case OP_PRINT:
            st = pop_val(vm, &a);
            if (st != VM_OK) return st;
            if (debug_ticks) printf("[vm tick=%u] ", vm->ticks);
            printf("%" PRId64 "\n", a);
            fflush(stdout);
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_FPRINT:
            st = pop_val(vm, &a);
            if (st != VM_OK) return st;
            if (debug_ticks) printf("[vm tick=%u] ", vm->ticks);
            printf("%.17g\n", vm_bits_to_f64(a));
            fflush(stdout);
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_PRINT_STR: {
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            int64_t len = vm->stack[vm->sp - 1u];
            a = vm->stack[vm->sp - 2u];
            if (len < 0 || (uint64_t)len > UINT32_MAX ||
                (len > 0 && !heap_range_ok(vm, a, (uint64_t)len))) {
                return VM_ERR_BAD_ADDR;
            }
            (void)pop_val(vm, &len);
            (void)pop_val(vm, &a);
            if (debug_ticks) printf("[vm tick=%u] ", vm->ticks);
            if (len > 0u) fwrite(vm->heap + (uint64_t)a, 1u, (size_t)len, stdout);
            putchar('\n');
            fflush(stdout);
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        case OP_PRINT_CHAR:
            st = pop_val(vm, &a);
            if (st != VM_OK) return st;
            fputc((unsigned char)(uint64_t)a, stdout);
            fflush(stdout);
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;

        case OP_READ_INT: {
            st = stack_ensure(vm, vm->sp + 1u);
            if (st != VM_OK) return st;
            char line[128];
            if (!fgets(line, sizeof line, stdin)) {
                if (ferror(stdin)) return VM_ERR_IO;
                line[0] = '\0';
            }
            char *end = NULL;
            errno = 0;
            long long value = strtoll(line, &end, 10);
            if (end == line || errno == ERANGE) value = 0;
            (void)push_val(vm, (int64_t)value);
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        case OP_READ_STR: {
            if (vm->sp < 2u) return VM_ERR_STACK_UNDERFLOW;
            int64_t max = vm->stack[vm->sp - 1u];
            a = vm->stack[vm->sp - 2u];
            if (max <= 0 || (uint64_t)max > UINT32_MAX ||
                !heap_range_ok(vm, a, (uint64_t)max)) return VM_ERR_BAD_ADDR;
            st = stack_ensure(vm, vm->sp + 1u);
            if (st != VM_OK) return st;

            char *tmp = malloc((size_t)max + 1u);
            if (!tmp) return VM_ERR_HEAP_OVERFLOW;
            if (!fgets(tmp, (int)max + 1, stdin)) {
                if (ferror(stdin)) { free(tmp); return VM_ERR_IO; }
                tmp[0] = '\0';
            }
            size_t len = strlen(tmp);
            while (len > 0u && (tmp[len - 1u] == '\n' || tmp[len - 1u] == '\r')) {
                tmp[--len] = '\0';
            }
            memcpy(vm->heap + (uint64_t)a, tmp, len); /* fits within max */
            free(tmp);

            (void)pop_val(vm, &max);
            (void)pop_val(vm, &a);
            (void)push_val(vm, (int64_t)len);
            vm->ip = next_ip;
            vm->ticks++;
            return VM_OK;
        }

        default:
            return VM_ERR_BAD_OP;
    }
}

VmStatus vm_run(VmState *vm, const VmInst *prog, size_t prog_len, uint32_t max_steps)
{
    uint32_t steps = 0;
    while (max_steps == 0u || steps < max_steps) {
        VmStatus st = vm_step(vm, prog, prog_len);
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
    static const char *names[OP_COUNT] = {
        "HALT", "NOP", "PUSH", "POP", "DUP", "ADD", "SUB", "MUL", "CMP_LT",
        "JMP", "JMP_Z", "ALLOC", "LOAD32", "STORE32", "PRINT",
        "DIV", "MOD", "CMP_EQ", "CMP_NE", "CMP_LE", "CMP_GT", "CMP_GE",
        "AND", "OR", "XOR", "NOT", "SHL", "SHR", "LNOT", "LAND", "LOR",
        "SWAP", "OVER", "ROT", "PICK",
        "LOAD8", "STORE8", "LOAD16", "STORE16", "LOAD32U", "LOAD64", "STORE64",
        "FREE", "CALL", "CALL_I", "RET", "ENTER", "LOCAL_GET", "LOCAL_SET",
        "JMP_I", "PUSHF", "FADD", "FSUB", "FMUL", "FDIV", "FNEG", "FABS",
        "FSQRT", "FCMP_LT", "FCMP_EQ", "FCMP_LE", "FCMP_GT", "FCMP_GE",
        "FTOI", "ITOF", "FPRINT", "PRINT_STR", "PRINT_CHAR", "READ_INT",
        "READ_STR"
    };
    if (op >= OP_COUNT) return "UNKNOWN";
    return names[op];
}
