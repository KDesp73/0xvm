// SPDX-License-Identifier: MIT
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tests.h"
#include "vm.h"

static int failures;

#define CHECK(cond) do {                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                         \
        }                                                                       \
    } while (0)

#define MAX_INSTS 512
#define ADR(p, i) ((uint16_t)(VM_IMAGE_START + (p).offsets[i]))

typedef struct {
    uint8_t bytes[VM_IMAGE_MAX];
    size_t len;
    uint16_t offsets[MAX_INSTS];
    size_t count;
} Prog;

static void put_be(uint8_t *p, uint64_t v, unsigned width)
{
    for (unsigned i = 0; i < width; i++) {
        p[width - 1u - i] = (uint8_t)(v & 0xFFu);
        v >>= 8;
    }
}

static void p_reset(Prog *p)
{
    memset(p, 0, sizeof(*p));
}

static uint16_t emit_raw(Prog *p, uint8_t op)
{
    uint16_t addr = (uint16_t)(VM_IMAGE_START + p->len);
    if (p->count < MAX_INSTS) p->offsets[p->count] = (uint16_t)p->len;
    p->count++;
    p->bytes[p->len++] = op;
    return addr;
}

static uint16_t emit0(Prog *p, VmOpcode op)
{
    return emit_raw(p, (uint8_t)op);
}

static uint16_t emit1(Prog *p, VmOpcode op, uint8_t arg)
{
    uint16_t addr = emit_raw(p, (uint8_t)op);
    p->bytes[p->len++] = arg;
    return addr;
}

static uint16_t emit2(Prog *p, VmOpcode op, uint16_t arg)
{
    uint16_t addr = emit_raw(p, (uint8_t)op);
    put_be(p->bytes + p->len, arg, 2u);
    p->len += 2u;
    return addr;
}

static uint16_t emit8(Prog *p, VmOpcode op, int64_t arg)
{
    uint16_t addr = emit_raw(p, (uint8_t)op);
    put_be(p->bytes + p->len, (uint64_t)arg, 8u);
    p->len += 8u;
    return addr;
}

static void patch2(Prog *p, size_t index, uint16_t target)
{
    put_be(p->bytes + p->offsets[index] + 1u, target, 2u);
}

static void patch8(Prog *p, size_t index, uint16_t target)
{
    put_be(p->bytes + p->offsets[index] + 1u, target, 8u);
}

static void load(VmState *vm, const Prog *p)
{
    CHECK(vm_load_image(vm, p->bytes, p->len, VM_IMAGE_START) == VM_OK);
}

static int same_state(const VmState *a, const VmState *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

static void step_to(VmState *vm, const Prog *p, uint16_t addr)
{
    for (size_t i = 0; i <= p->count + 8u; i++) {
        if (vm->ip == addr) return;
        VmStatus st = vm_step(vm);
        if (st != VM_OK) { CHECK(st == VM_OK); return; }
    }
    CHECK(0 /* never reached fail instruction */);
}

/* Run the setup instructions, then assert the target instruction fails atomically. */
static void expect_error(VmState *vm, const Prog *p, uint16_t fail_addr, VmStatus want)
{
    load(vm, p);
    step_to(vm, p, fail_addr);
    VmState before = *vm;
    CHECK(vm_step(vm) == want);
    CHECK(same_state(vm, &before));
}

static void test_stack_ops(void)
{
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, 7);
    emit0(&p, OP_DUP);
    emit0(&p, OP_ADD);
    emit8(&p, OP_PUSH, -3);
    emit0(&p, OP_POP);
    emit0(&p, OP_HALT);
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 14);

    /* Underflow must not consume state. */
    vm_init(&vm);
    p_reset(&p);
    uint16_t bad = emit0(&p, OP_POP);
    expect_error(&vm, &p, bad, VM_ERR_STACK_UNDERFLOW);
    CHECK(vm.sp == 0u && vm.ticks == 0u && vm.ip == VM_IMAGE_START);

    p_reset(&p);
    emit8(&p, OP_PUSH, 1);
    uint16_t add = emit0(&p, OP_ADD);
    expect_error(&vm, &p, add, VM_ERR_STACK_UNDERFLOW);
    vm_free(&vm);
}

static void test_stack_shuffling(void)
{
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, 1);
    emit8(&p, OP_PUSH, 2);
    emit0(&p, OP_SWAP);
    emit8(&p, OP_PUSH, 3);
    emit0(&p, OP_OVER);
    emit0(&p, OP_ROT);
    emit8(&p, OP_PUSH, 1);
    emit0(&p, OP_PICK);
    emit0(&p, OP_HALT);
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 5u);
    CHECK(vm.stack[0] == 2 && vm.stack[1] == 3 && vm.stack[2] == 1);
    CHECK(vm.stack[3] == 1 && vm.stack[4] == 1);
    vm_free(&vm);

    p_reset(&p);
    emit8(&p, OP_PUSH, 1);
    emit8(&p, OP_PUSH, 5);
    uint16_t pick = emit0(&p, OP_PICK);
    expect_error(&vm, &p, pick, VM_ERR_BAD_ADDR);
    vm_free(&vm);
}

static void check_binary(VmOpcode op, int64_t x, int64_t y, int64_t want)
{
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, x);
    emit8(&p, OP_PUSH, y);
    emit0(&p, op);
    emit0(&p, OP_HALT);
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == want);
    vm_free(&vm);
}

static void test_arithmetic(void)
{
    check_binary(OP_ADD, 3, 4, 7);
    check_binary(OP_SUB, 3, 4, -1);
    check_binary(OP_MUL, -3, 4, -12);
    check_binary(OP_CMP_LT, 3, 4, 1);
    check_binary(OP_CMP_LT, 4, 3, 0);
    check_binary(OP_CMP_LT, 3, 3, 0);
    check_binary(OP_ADD, INT64_MAX, 1, INT64_MIN);
    check_binary(OP_MUL, INT64_MAX, 2, -2);
}

static void check_unary(VmOpcode op, int64_t x, int64_t want)
{
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, x);
    emit0(&p, op);
    emit0(&p, OP_HALT);
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == want);
    vm_free(&vm);
}

static void test_div_mod(void)
{
    check_binary(OP_DIV, 7, 2, 3);
    check_binary(OP_DIV, -7, 2, -3);
    check_binary(OP_DIV, 7, -2, -3);
    check_binary(OP_DIV, -7, -2, 3);
    check_binary(OP_DIV, INT64_MIN, -1, INT64_MIN);
    check_binary(OP_MOD, 7, 2, 1);
    check_binary(OP_MOD, -7, 2, -1);
    check_binary(OP_MOD, 7, -2, 1);
    check_binary(OP_MOD, -7, -2, -1);
    check_binary(OP_MOD, INT64_MIN, -1, 0);

    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, 10);
    emit8(&p, OP_PUSH, 0);
    uint16_t div0 = emit0(&p, OP_DIV);
    expect_error(&vm, &p, div0, VM_ERR_DIV_ZERO);
    CHECK(vm.sp == 2u && vm.stack[0] == 10 && vm.stack[1] == 0);
    CHECK(vm.ip == div0 && vm.ticks == 2u);

    p_reset(&p);
    emit8(&p, OP_PUSH, 10);
    emit8(&p, OP_PUSH, 0);
    uint16_t mod0 = emit0(&p, OP_MOD);
    expect_error(&vm, &p, mod0, VM_ERR_DIV_ZERO);
    vm_free(&vm);
}

static void test_bitwise(void)
{
    check_binary(OP_AND, 0xF0F0, 0x3C3C, 0x3030);
    check_binary(OP_OR, 0xF0F0, 0x3C3C, 0xFCFC);
    check_binary(OP_XOR, 0xF0F0, 0x3C3C, 0xCCCC);
    check_binary(OP_AND, -1, 0x1234, 0x1234);
    check_binary(OP_XOR, -1, -1, 0);
    check_binary(OP_SHL, 1, 4, 16);
    check_binary(OP_SHL, 1, 64, 1);
    check_binary(OP_SHL, 1, 65, 2);
    check_binary(OP_SHL, INT64_MIN, 1, 0);
    check_binary(OP_SHR, -1, 1, INT64_MAX);
    check_binary(OP_SHR, INT64_MIN, 63, 1);
    check_unary(OP_NOT, 0, -1);
    check_unary(OP_NOT, -1, 0);
    check_unary(OP_NOT, 0x0F, ~0x0F);
}

static void test_comparisons_and_logic(void)
{
    check_binary(OP_CMP_EQ, 3, 3, 1);
    check_binary(OP_CMP_EQ, 3, 4, 0);
    check_binary(OP_CMP_NE, 3, 4, 1);
    check_binary(OP_CMP_NE, 3, 3, 0);
    check_binary(OP_CMP_LE, 3, 3, 1);
    check_binary(OP_CMP_LE, 4, 3, 0);
    check_binary(OP_CMP_GT, 4, 3, 1);
    check_binary(OP_CMP_GT, 3, 3, 0);
    check_binary(OP_CMP_GE, 3, 3, 1);
    check_binary(OP_CMP_GE, 2, 3, 0);
    check_binary(OP_LAND, 5, 7, 1);
    check_binary(OP_LAND, 5, 0, 0);
    check_binary(OP_LOR, 0, 7, 1);
    check_binary(OP_LOR, 0, 0, 0);
    check_binary(OP_LOR, -1, 0, 1);
    check_unary(OP_LNOT, 0, 1);
    check_unary(OP_LNOT, 42, 0);
    check_unary(OP_LNOT, -1, 0);

    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, 1);
    uint16_t xor = emit0(&p, OP_XOR);
    expect_error(&vm, &p, xor, VM_ERR_STACK_UNDERFLOW);
    CHECK(vm.sp == 1u && vm.stack[0] == 1);
    vm_free(&vm);
}

static void test_jumps(void)
{
    VmState vm;
    vm_init(&vm);
    Prog p;

    p_reset(&p);
    emit8(&p, OP_PUSH, 1);
    uint16_t jz1 = emit2(&p, OP_JMP_Z, 0);
    emit8(&p, OP_PUSH, 10);
    emit0(&p, OP_HALT);
    emit8(&p, OP_PUSH, 20);
    emit0(&p, OP_HALT);
    patch2(&p, 1, ADR(p, 4));
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 10);
    (void)jz1;

    p_reset(&p);
    emit8(&p, OP_PUSH, 0);
    emit2(&p, OP_JMP_Z, 0);
    emit8(&p, OP_PUSH, 10);
    emit0(&p, OP_HALT);
    emit8(&p, OP_PUSH, 20);
    emit0(&p, OP_HALT);
    patch2(&p, 1, ADR(p, 4));
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 20);

    p_reset(&p);
    emit8(&p, OP_PUSH, 0);
    emit2(&p, OP_JMP, 0);
    emit8(&p, OP_PUSH, 99);
    emit8(&p, OP_PUSH, 42);
    emit0(&p, OP_HALT);
    patch2(&p, 1, ADR(p, 3));
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 2u && vm.stack[0] == 0 && vm.stack[1] == 42);

    p_reset(&p);
    emit8(&p, OP_PUSH, 0); /* patched to target */
    emit0(&p, OP_JMP_I);
    emit8(&p, OP_PUSH, 99);
    emit8(&p, OP_PUSH, 42);
    emit0(&p, OP_HALT);
    patch8(&p, 0, ADR(p, 3));
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 42);

    p_reset(&p);
    emit8(&p, OP_PUSH, 70000);
    uint16_t bad = emit0(&p, OP_JMP_I);
    expect_error(&vm, &p, bad, VM_ERR_BAD_IP);
    vm_free(&vm);
}

static void test_store_load(void)
{
    const uint16_t base = VM_HEAP_START;
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, 12);
    emit0(&p, OP_ALLOC);
    emit0(&p, OP_POP);
    emit8(&p, OP_PUSH, 0x01020304);
    emit8(&p, OP_PUSH, base + 8u);
    emit0(&p, OP_STORE32);
    emit8(&p, OP_PUSH, base + 8u);
    emit0(&p, OP_LOAD32);
    emit8(&p, OP_PUSH, 7);
    emit8(&p, OP_PUSH, base);
    emit0(&p, OP_STORE32);
    emit8(&p, OP_PUSH, base);
    emit0(&p, OP_LOAD32);
    emit0(&p, OP_HALT);
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 2u && vm.stack[0] == 0x01020304 && vm.stack[1] == 7);
    CHECK(vm.mem[base + 8u] == 1 && vm.mem[base + 9u] == 2);
    CHECK(vm.mem[base + 10u] == 3 && vm.mem[base + 11u] == 4);
    vm_free(&vm);
}

static void test_memory_widths(void)
{
    const uint16_t base = VM_HEAP_START;
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, 24);
    emit0(&p, OP_ALLOC);
    emit0(&p, OP_POP);
    emit8(&p, OP_PUSH, 0xAB);          emit8(&p, OP_PUSH, base + 1u);  emit0(&p, OP_STORE8);
    emit8(&p, OP_PUSH, 0x1234);        emit8(&p, OP_PUSH, base + 4u);  emit0(&p, OP_STORE16);
    emit8(&p, OP_PUSH, (int64_t)(uint32_t)0xFEDCBA98); emit8(&p, OP_PUSH, base + 8u); emit0(&p, OP_STORE32);
    emit8(&p, OP_PUSH, 0x0123456789ABCDEF); emit8(&p, OP_PUSH, base + 12u); emit0(&p, OP_STORE64);
    emit8(&p, OP_PUSH, base + 1u);  emit0(&p, OP_LOAD8);
    emit8(&p, OP_PUSH, base + 4u);  emit0(&p, OP_LOAD16);
    emit8(&p, OP_PUSH, base + 8u);  emit0(&p, OP_LOAD32U);
    emit8(&p, OP_PUSH, base + 8u);  emit0(&p, OP_LOAD32);
    emit8(&p, OP_PUSH, base + 12u); emit0(&p, OP_LOAD64);
    emit0(&p, OP_HALT);
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 5u);
    CHECK(vm.stack[0] == 0xAB);
    CHECK(vm.stack[1] == 0x1234);
    CHECK(vm.stack[2] == 0xFEDCBA98u);
    CHECK(vm.stack[3] == (int64_t)(int32_t)0xFEDCBA98u);
    CHECK(vm.stack[4] == 0x0123456789ABCDEF);
    vm_free(&vm);

    /* Static data in the image region is directly addressable. */
    vm_init(&vm);
    p_reset(&p);
    emit8(&p, OP_PUSH, 0x1234);
    emit8(&p, OP_PUSH, VM_IMAGE_START + 3u);
    emit0(&p, OP_STORE16);
    emit8(&p, OP_PUSH, VM_IMAGE_START + 3u);
    emit0(&p, OP_LOAD16);
    emit0(&p, OP_HALT);
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 0x1234);
    vm_free(&vm);

    p_reset(&p);
    emit8(&p, OP_PUSH, VM_RAM_SIZE - 1);
    uint16_t oob = emit0(&p, OP_LOAD32);
    expect_error(&vm, &p, oob, VM_ERR_BAD_ADDR);
    vm_free(&vm);
}

static void test_alloc_and_heap_bounds(void)
{
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, 1);
    emit0(&p, OP_ALLOC);
    emit8(&p, OP_PUSH, 5);
    emit0(&p, OP_ALLOC);
    emit0(&p, OP_HALT);
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.hp == VM_HEAP_START + 16u);
    CHECK(vm.stack[0] == VM_HEAP_START && vm.stack[1] == VM_HEAP_START + 8u);
    CHECK(vm.block_count == 2u && vm.blocks[0].live && vm.blocks[1].live);

    /* Loads/stores address all RAM, so only the RAM boundary is out of range. */
    p_reset(&p);
    emit8(&p, OP_PUSH, VM_RAM_SIZE - 2u);
    uint16_t oob = emit0(&p, OP_LOAD32);
    expect_error(&vm, &p, oob, VM_ERR_BAD_ADDR);
    p_reset(&p);
    emit8(&p, OP_PUSH, 99);
    emit8(&p, OP_PUSH, VM_RAM_SIZE - 2u);
    uint16_t store_oob = emit0(&p, OP_STORE32);
    expect_error(&vm, &p, store_oob, VM_ERR_BAD_ADDR);
    p_reset(&p);
    emit8(&p, OP_PUSH, -1);
    uint16_t negative = emit0(&p, OP_LOAD32);
    expect_error(&vm, &p, negative, VM_ERR_BAD_ADDR);
    p_reset(&p);
    emit8(&p, OP_PUSH, 0);
    uint16_t bad_size = emit0(&p, OP_ALLOC);
    expect_error(&vm, &p, bad_size, VM_ERR_BAD_ADDR);
    vm_free(&vm);

    VmState big;
    vm_init(&big);
    p_reset(&p);
    emit8(&p, OP_PUSH, 8192);
    emit0(&p, OP_ALLOC);
    emit0(&p, OP_POP);
    emit8(&p, OP_PUSH, 12345);
    emit8(&p, OP_PUSH, VM_HEAP_START + 8184u);
    emit0(&p, OP_STORE32);
    emit8(&p, OP_PUSH, VM_HEAP_START + 8184u);
    emit0(&p, OP_LOAD32);
    emit0(&p, OP_HALT);
    load(&big, &p);
    CHECK(vm_run(&big, 0) == VM_HALTED);
    CHECK(big.hp == VM_HEAP_START + 8192u);
    CHECK(big.sp == 1u && big.stack[0] == 12345);
    vm_free(&big);
}

static void test_heap_reuse(void)
{
    const uint16_t base = VM_HEAP_START;
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, 8); emit0(&p, OP_ALLOC); emit0(&p, OP_POP);  /* base */
    emit8(&p, OP_PUSH, 16); emit0(&p, OP_ALLOC); emit0(&p, OP_POP); /* base+8 */
    emit8(&p, OP_PUSH, base + 8u); emit0(&p, OP_FREE);
    emit8(&p, OP_PUSH, 8); emit0(&p, OP_ALLOC); /* reuses base+8 */
    emit8(&p, OP_PUSH, 8); emit0(&p, OP_ALLOC); /* reuses split: base+16 */
    emit0(&p, OP_HALT);
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.hp == base + 24u);
    CHECK(vm.sp == 2u && vm.stack[0] == base + 8u && vm.stack[1] == base + 16u);
    CHECK(vm.block_count == 3u);
    CHECK(vm.blocks[0].live && vm.blocks[1].live && vm.blocks[2].live);

    p_reset(&p);
    emit8(&p, OP_PUSH, 99);
    uint16_t bad_free = emit0(&p, OP_FREE);
    expect_error(&vm, &p, bad_free, VM_ERR_BAD_ADDR);
    vm_free(&vm);
}

static void test_calls_and_locals(void)
{
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, 10);
    uint16_t call = emit2(&p, OP_CALL, 0);
    emit0(&p, OP_SWAP);
    emit0(&p, OP_ADD);
    emit0(&p, OP_HALT);
    emit0(&p, OP_NOP);
    emit0(&p, OP_NOP);
    emit1(&p, OP_ENTER, 1);
    emit1(&p, OP_LOCAL_GET, 0);
    emit8(&p, OP_PUSH, 5);
    emit0(&p, OP_ADD);
    emit1(&p, OP_LOCAL_SET, 0);
    emit1(&p, OP_LOCAL_GET, 0);
    emit0(&p, OP_RET);
    patch2(&p, 1, ADR(p, 7));
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 15);
    CHECK(vm.fsp == 0u && vm.fp == 0u);
    (void)call;

    vm_init(&vm);
    p_reset(&p);
    emit8(&p, OP_PUSH, 10);
    emit8(&p, OP_PUSH, 0); /* patched to function */
    emit0(&p, OP_CALL_I);
    emit0(&p, OP_SWAP);
    emit0(&p, OP_ADD);
    emit0(&p, OP_HALT);
    emit0(&p, OP_NOP);
    emit0(&p, OP_NOP);
    emit1(&p, OP_ENTER, 1);
    emit8(&p, OP_PUSH, 42);
    emit1(&p, OP_LOCAL_SET, 0);
    emit1(&p, OP_LOCAL_GET, 0);
    emit0(&p, OP_RET);
    patch8(&p, 1, ADR(p, 8));
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 52);

    p_reset(&p);
    uint16_t bad_ret = emit0(&p, OP_RET);
    expect_error(&vm, &p, bad_ret, VM_ERR_BAD_FRAME);
    vm_free(&vm);
}

static void test_floats(void)
{
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSHF, vm_f64_to_bits(1.5));
    emit8(&p, OP_PUSHF, vm_f64_to_bits(2.0));
    emit0(&p, OP_FADD);
    emit8(&p, OP_PUSHF, vm_f64_to_bits(2.0));
    emit0(&p, OP_FMUL);
    emit0(&p, OP_HALT);
    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm_bits_to_f64(vm.stack[0]) == 7.0);
    vm_free(&vm);

    check_binary(OP_FCMP_LT, vm_f64_to_bits(1.0), vm_f64_to_bits(2.0), 1);
    check_binary(OP_FCMP_GE, vm_f64_to_bits(1.0), vm_f64_to_bits(2.0), 0);
    check_unary(OP_FNEG, vm_f64_to_bits(2.5), vm_f64_to_bits(-2.5));
    check_unary(OP_FABS, vm_f64_to_bits(-2.5), vm_f64_to_bits(2.5));
    check_unary(OP_FSQRT, vm_f64_to_bits(9.0), vm_f64_to_bits(3.0));
    check_unary(OP_FTOI, vm_f64_to_bits(7.9), 7);
    check_unary(OP_ITOF, 7, vm_f64_to_bits(7.0));
}

static void test_error_atomicity(void)
{
    VmState vm;
    vm_init(&vm);
    Prog p;

    p_reset(&p);
    emit8(&p, OP_PUSH, 70000);
    uint16_t bad_target = emit0(&p, OP_JMP_I);
    expect_error(&vm, &p, bad_target, VM_ERR_BAD_IP);

    p_reset(&p);
    emit_raw(&p, 255);
    uint16_t bad_op = ADR(p, 0);
    expect_error(&vm, &p, bad_op, VM_ERR_BAD_OP);

    VmState before = vm;
    CHECK(vm_load_image(&vm, p.bytes, p.len, 0) == VM_ERR_BAD_IP);
    CHECK(same_state(&vm, &before));
    vm_free(&vm);

    /* The operand stack is fixed at 256 cells and refuses further pushes. */
    VmState packed;
    vm_init(&packed);
    p_reset(&p);
    for (size_t i = 0; i < 300u; i++) emit8(&p, OP_PUSH, (int64_t)i);
    load(&packed, &p);
    CHECK(vm_run(&packed, 300u) == VM_ERR_STACK_OVERFLOW);
    CHECK(packed.sp == VM_STACK_SIZE);
    CHECK(packed.stack[VM_STACK_SIZE - 1u] == (int64_t)(VM_STACK_SIZE - 1u));
    vm_free(&packed);
}

static void test_demo_control_flow(void)
{
    const uint16_t b = VM_HEAP_START;
    VmState vm;
    vm_init(&vm);
    Prog p;
    p_reset(&p);
    emit8(&p, OP_PUSH, 0);
    emit8(&p, OP_PUSH, 4);
    emit0(&p, OP_ALLOC);
    emit0(&p, OP_STORE32);
    uint16_t loop = emit8(&p, OP_PUSH, 0); (void)loop;
    emit0(&p, OP_DUP);
    uint16_t cmp = emit8(&p, OP_PUSH, 6); (void)cmp;
    emit0(&p, OP_CMP_LT);
    uint16_t jz = emit2(&p, OP_JMP_Z, 0);
    emit0(&p, OP_DUP);
    emit0(&p, OP_POP);
    emit8(&p, OP_PUSH, b);
    emit0(&p, OP_LOAD32);
    emit0(&p, OP_DUP);
    emit8(&p, OP_PUSH, 10);
    emit0(&p, OP_ADD);
    emit8(&p, OP_PUSH, b);
    emit0(&p, OP_STORE32);
    emit0(&p, OP_POP);
    emit8(&p, OP_PUSH, 1);
    emit0(&p, OP_ADD);
    uint16_t back = emit2(&p, OP_JMP, 0);
    emit8(&p, OP_PUSH, b);
    emit0(&p, OP_LOAD32);
    emit0(&p, OP_HALT);
    patch2(&p, 8, ADR(p, 22));  /* JMP_Z -> done */
    patch2(&p, 21, ADR(p, 5));  /* JMP -> loop body, after counter init */

    load(&vm, &p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.hp == b + 8u && vm.mem[b + 3u] == 60);
    CHECK(vm.sp == 2u && vm.stack[0] == 6 && vm.stack[1] == 60);
    (void)jz;
    (void)back;
    vm_free(&vm);
}

int test_vm(void)
{
    test_stack_ops();
    test_stack_shuffling();
    test_arithmetic();
    test_div_mod();
    test_bitwise();
    test_comparisons_and_logic();
    test_jumps();
    test_store_load();
    test_memory_widths();
    test_alloc_and_heap_bounds();
    test_heap_reuse();
    test_calls_and_locals();
    test_floats();
    test_error_atomicity();
    test_demo_control_flow();
    return failures;
}
