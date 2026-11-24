#include "vm.h"

#include <errno.h>
#include <float.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Wire v3 header:
 * magic(4) ver(1) flags(1) reserved(2) ip(4) sp(4) hp(8) ticks(4) fp(4) */
#define BLOCK_WIRE_BYTES 17u /* addr(8) + size(8) + live(1) */
/* Sanity cap for heap sizes arriving from the wire. Without this, a corrupt
 * hp field could request an enormous allocation before the payload is read. */
#define VM_MAX_WIRE_HEAP (1u << 30) /* 1 GiB */

/* Host-side output policy: tick prefixes are off unless explicitly enabled. */
static int debug_ticks = 0;

void vm_set_debug_ticks(int enabled)
{
    debug_ticks = enabled != 0;
}

static void write_u32_be(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)((v >> 24) & 0xFFu);
    dst[1] = (uint8_t)((v >> 16) & 0xFFu);
    dst[2] = (uint8_t)((v >> 8)  & 0xFFu);
    dst[3] = (uint8_t)(v & 0xFFu);
}

static uint32_t read_u32_be(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8)  | (uint32_t)src[3];
}

static void write_u64_be(uint8_t *dst, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        dst[i] = (uint8_t)((v >> (56 - 8 * i)) & 0xFFu);
    }
}

static uint64_t read_u64_be(const uint8_t *src)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | (uint64_t)src[i];
    return v;
}

static void write_i64_be(uint8_t *dst, int64_t v)
{
    write_u64_be(dst, (uint64_t)v);
}

static int64_t read_i64_be(const uint8_t *src)
{
    return (int64_t)read_u64_be(src);
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

int vm_clone(const VmState *src, VmState *out)
{
    if (!src || !out) return -1;
    VmState tmp;
    vm_init(&tmp);

    if (src->sp > 0u) {
        tmp.stack = malloc((size_t)src->sp * sizeof *tmp.stack);
        if (!tmp.stack) return -1;
        memcpy(tmp.stack, src->stack, (size_t)src->sp * sizeof *tmp.stack);
        tmp.stack_cap = src->sp;
    }
    tmp.sp = src->sp;
    tmp.fp = src->fp;

    if (src->hp > 0u) {
        tmp.heap = malloc((size_t)src->hp);
        if (!tmp.heap) { vm_free(&tmp); return -1; }
        memcpy(tmp.heap, src->heap, (size_t)src->hp);
        tmp.heap_cap = src->hp;
    }
    tmp.hp = src->hp;

    if (src->fsp > 0u) {
        tmp.frames = malloc((size_t)src->fsp * sizeof *tmp.frames);
        if (!tmp.frames) { vm_free(&tmp); return -1; }
        memcpy(tmp.frames, src->frames, (size_t)src->fsp * sizeof *tmp.frames);
        tmp.frame_cap = src->fsp;
    }
    tmp.fsp = src->fsp;

    if (src->block_count > 0u) {
        tmp.blocks = malloc((size_t)src->block_count * sizeof *tmp.blocks);
        if (!tmp.blocks) { vm_free(&tmp); return -1; }
        memcpy(tmp.blocks, src->blocks, (size_t)src->block_count * sizeof *tmp.blocks);
        tmp.block_cap = src->block_count;
    }
    tmp.block_count = src->block_count;
    tmp.ip = src->ip;
    tmp.ticks = src->ticks;

    vm_free(out);
    *out = tmp;
    return 0;
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

/* Heap accesses must stay inside the region covered by serialization ([0, hp)). */
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

        case OP_MIGRATE:
            vm->ip = next_ip; /* resume after the yield */
            vm->ticks++;
            return VM_YIELD_MIGRATE;

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

/* ---------------------------------------------------------- serialization */

static uint64_t state_payload_bytes(const VmState *vm)
{
    return (uint64_t)vm->sp * 8u + 4u /* fsp */ + (uint64_t)vm->fsp * 8u +
           4u /* block count */ + (uint64_t)vm->block_count * BLOCK_WIRE_BYTES + vm->hp;
}

static uint64_t page_count(uint64_t hp)
{
    return (hp + VM_PAGE_SIZE - 1u) / VM_PAGE_SIZE;
}

static uint64_t page_bytes(const VmState *vm, uint64_t index)
{
    uint64_t off = index * VM_PAGE_SIZE;
    uint64_t remain = vm->hp - off;
    return remain < VM_PAGE_SIZE ? remain : VM_PAGE_SIZE;
}

static int page_dirty(const VmState *vm, const VmState *base, uint64_t index)
{
    uint64_t off = index * VM_PAGE_SIZE;
    uint64_t n = page_bytes(vm, index);
    const uint8_t *other = NULL;
    if (base && off <= base->hp && n <= base->hp - off) other = base->heap + off;
    return !other || memcmp(vm->heap + off, other, (size_t)n) != 0;
}

static uint64_t dirty_pages(const VmState *vm, const VmState *base)
{
    uint64_t count = 0;
    uint64_t pages = page_count(vm->hp);
    for (uint64_t i = 0; i < pages; i++) {
        if (page_dirty(vm, base, i)) count++;
    }
    return count;
}

static size_t wire_total(uint64_t payload)
{
    if (!size_ok(payload)) return 0u;
    return (size_t)(VM_WIRE_HEADER_BYTES + payload);
}

static void write_prefix(uint8_t *buf, const VmState *vm, uint8_t flags)
{
    write_u32_be(buf + 0, VM_MAGIC);
    buf[4] = VM_VERSION;
    buf[5] = flags;
    buf[6] = 0u; /* reserved */
    buf[7] = 0u;
    write_u32_be(buf + 8,  vm->ip);
    write_u32_be(buf + 12, vm->sp);
    write_u64_be(buf + 16, vm->hp);
    write_u32_be(buf + 24, vm->ticks);
    write_u32_be(buf + 28, vm->fp);

    size_t pos = VM_WIRE_HEADER_BYTES;
    for (uint32_t i = 0; i < vm->sp; i++) {
        write_i64_be(buf + pos, vm->stack[i]);
        pos += 8u;
    }
    write_u32_be(buf + pos, vm->fsp);
    pos += 4u;
    for (uint32_t i = 0; i < vm->fsp; i++) {
        write_u32_be(buf + pos,     vm->frames[i].ret_ip);
        write_u32_be(buf + pos + 4, vm->frames[i].prev_fp);
        pos += 8u;
    }
    write_u32_be(buf + pos, vm->block_count);
    pos += 4u;
    for (uint32_t i = 0; i < vm->block_count; i++) {
        write_u64_be(buf + pos, vm->blocks[i].addr);
        write_u64_be(buf + pos + 8, vm->blocks[i].size);
        buf[pos + 16] = vm->blocks[i].live;
        pos += BLOCK_WIRE_BYTES;
    }
}

size_t vm_serialize(const VmState *vm, uint8_t *buf, size_t buf_cap)
{
    if (!vm || vm->fp > vm->sp || vm->hp > vm->heap_cap) return 0u;
    size_t total = wire_total(state_payload_bytes(vm));
    if (total == 0u) return 0u;
    if (!buf) return total;
    if (buf_cap < total) return 0u;

    write_prefix(buf, vm, 0u);
    size_t pos = VM_WIRE_HEADER_BYTES + (size_t)state_payload_bytes(vm) - (size_t)vm->hp;
    if (vm->hp > 0u) {
        memcpy(buf + pos, vm->heap, (size_t)vm->hp);
    }
    return total;
}

size_t vm_serialize_diff(const VmState *vm, const VmState *base,
                         uint8_t *buf, size_t buf_cap)
{
    if (!vm || !base || vm->fp > vm->sp || vm->hp > vm->heap_cap) return 0u;

    uint64_t changed = dirty_pages(vm, base);
    if (changed > UINT32_MAX) return 0u;
    uint64_t pages = page_count(vm->hp);
    if (changed > pages) return 0u;

    uint64_t heap_bytes = 4u; /* changed count */
    for (uint64_t i = 0; i < pages; i++) {
        if (page_dirty(vm, base, i)) {
            heap_bytes += 4u + page_bytes(vm, i); /* page index + page data */
        }
    }
    uint64_t payload = state_payload_bytes(vm) - vm->hp + heap_bytes;
    size_t total = wire_total(payload);
    if (total == 0u) return 0u;
    if (!buf) return total;
    if (buf_cap < total) return 0u;

    write_prefix(buf, vm, VM_FLAG_DIRTY);
    size_t pos = VM_WIRE_HEADER_BYTES +
                 (size_t)(state_payload_bytes(vm) - vm->hp);
    write_u32_be(buf + pos, (uint32_t)changed);
    pos += 4u;
    for (uint64_t i = 0; i < pages; i++) {
        if (!page_dirty(vm, base, i)) continue;
        write_u32_be(buf + pos, (uint32_t)i);
        pos += 4u;
        uint64_t n = page_bytes(vm, i);
        memcpy(buf + pos, vm->heap + i * VM_PAGE_SIZE, (size_t)n);
        pos += (size_t)n;
    }
    return total;
}

static int have(size_t len, size_t pos, uint64_t need)
{
    return pos <= len && need <= (uint64_t)(len - pos);
}

/* A single implementation handles both full snapshots and page diffs. On any
 * error `out` is untouched (state is assembled in a temporary first). */
static int deserialize_impl(VmState *out, const VmState *base,
                            const uint8_t *buf, size_t len, int allow_diff)
{
    if (!out || !buf || len < VM_WIRE_HEADER_BYTES) return -1;
    if (read_u32_be(buf) != VM_MAGIC || buf[4] != VM_VERSION) return -1;

    uint8_t flags = buf[5];
    if ((flags & (uint8_t)~VM_FLAG_DIRTY) != 0u) return -1;
    if (buf[6] != 0u || buf[7] != 0u) return -1;
    int dirty = (flags & VM_FLAG_DIRTY) != 0u;
    if (dirty && (!allow_diff || !base)) return -1;

    uint32_t ip    = read_u32_be(buf + 8);
    uint32_t sp    = read_u32_be(buf + 12);
    uint64_t hp    = read_u64_be(buf + 16);
    uint32_t ticks = read_u32_be(buf + 24);
    uint32_t fp    = read_u32_be(buf + 28);
    if (fp > sp || hp > VM_MAX_WIRE_HEAP || !size_ok(hp)) return -1;

    size_t pos = VM_WIRE_HEADER_BYTES;
    uint64_t need = (uint64_t)sp * 8u;
    if (!have(len, pos, need)) return -1;

    VmState tmp;
    vm_init(&tmp);

    if (sp > 0u) {
        tmp.stack = malloc((size_t)sp * sizeof *tmp.stack);
        if (!tmp.stack) goto fail;
        tmp.stack_cap = sp;
    }
    for (uint32_t i = 0; i < sp; i++) {
        tmp.stack[i] = read_i64_be(buf + pos);
        pos += 8u;
    }
    tmp.sp = sp;
    tmp.fp = fp;

    if (!have(len, pos, 4u)) goto fail;
    uint32_t fsp = read_u32_be(buf + pos);
    pos += 4u;
    if (!have(len, pos, (uint64_t)fsp * 8u)) goto fail;
    if (fsp > 0u) {
        tmp.frames = malloc((size_t)fsp * sizeof *tmp.frames);
        if (!tmp.frames) goto fail;
        tmp.frame_cap = fsp;
    }
    for (uint32_t i = 0; i < fsp; i++) {
        tmp.frames[i].ret_ip  = read_u32_be(buf + pos);
        tmp.frames[i].prev_fp = read_u32_be(buf + pos + 4u);
        pos += 8u;
    }
    tmp.fsp = fsp;

    if (!have(len, pos, 4u)) goto fail;
    uint32_t blocks = read_u32_be(buf + pos);
    pos += 4u;
    if (!have(len, pos, (uint64_t)blocks * BLOCK_WIRE_BYTES)) goto fail;
    if (blocks > 0u) {
        tmp.blocks = malloc((size_t)blocks * sizeof *tmp.blocks);
        if (!tmp.blocks) goto fail;
        tmp.block_cap = blocks;
    }
    for (uint32_t i = 0; i < blocks; i++) {
        uint64_t addr = read_u64_be(buf + pos);
        uint64_t size = read_u64_be(buf + pos + 8u);
        uint8_t live = buf[pos + 16u];
        pos += BLOCK_WIRE_BYTES;
        if (live > 1u || size == 0u || addr > hp || size > hp - addr) goto fail;
        tmp.blocks[i] = (VmBlock){addr, size, live};
    }
    tmp.block_count = blocks;

    if (hp > 0u) {
        tmp.heap = malloc((size_t)hp);
        if (!tmp.heap) goto fail;
        tmp.heap_cap = hp;
        if (dirty) {
            /* Start from the base heap; pages beyond it read as zeros. */
            if (base && base->hp > 0u) {
                uint64_t shared = base->hp < hp ? base->hp : hp;
                memcpy(tmp.heap, base->heap, (size_t)shared);
            }
        }
    }
    tmp.hp = hp;

    if (dirty) {
        if (!have(len, pos, 4u)) goto fail;
        uint32_t changed = read_u32_be(buf + pos);
        pos += 4u;
        uint64_t pages = page_count(hp);
        if (changed > pages || !have(len, pos, (uint64_t)changed * 4u)) goto fail;
        for (uint32_t i = 0; i < changed; i++) {
            uint32_t index = read_u32_be(buf + pos);
            pos += 4u;
            if ((uint64_t)index >= pages) goto fail;
            uint64_t off = (uint64_t)index * VM_PAGE_SIZE;
            uint64_t n = hp - off;
            if (n > VM_PAGE_SIZE) n = VM_PAGE_SIZE;
            if (!have(len, pos, n)) goto fail;
            memcpy(tmp.heap + off, buf + pos, (size_t)n);
            pos += (size_t)n;
        }
    } else {
        if (!have(len, pos, hp)) goto fail;
        if (hp > 0u) memcpy(tmp.heap, buf + pos, (size_t)hp);
        pos += (size_t)hp;
    }

    tmp.ip = ip;
    tmp.ticks = ticks;
    vm_free(out);
    *out = tmp;
    return 0;

fail:
    vm_free(&tmp);
    return -1;
}

int vm_deserialize(VmState *out, const uint8_t *buf, size_t buf_len)
{
    return deserialize_impl(out, NULL, buf, buf_len, 0);
}

int vm_deserialize_diff(VmState *out, const VmState *base,
                        const uint8_t *buf, size_t buf_len)
{
    return deserialize_impl(out, base, buf, buf_len, 1);
}

const char *vm_status_str(VmStatus status)
{
    switch (status) {
        case VM_OK:                 return "VM_OK";
        case VM_HALTED:             return "VM_HALTED";
        case VM_YIELD_MIGRATE:      return "VM_YIELD_MIGRATE";
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
        case VM_ERR_PAYLOAD:        return "VM_ERR_PAYLOAD";
        default:                    return "VM_ERR_UNKNOWN";
    }
}

const char *vm_opcode_name(uint8_t op)
{
    static const char *names[OP_COUNT] = {
        "HALT", "NOP", "PUSH", "POP", "DUP", "ADD", "SUB", "MUL", "CMP_LT",
        "JMP", "JMP_Z", "ALLOC", "LOAD32", "STORE32", "PRINT", "MIGRATE",
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
