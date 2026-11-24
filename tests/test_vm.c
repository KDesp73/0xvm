#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "tests.h"
#include "vm.h"

static int failures;

#define NELEMS(a) (sizeof(a) / sizeof((a)[0]))
#define CHECK(cond) do {                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static int same_state(const VmState *a, const VmState *b)
{
    if (a->ip != b->ip || a->sp != b->sp || a->fp != b->fp ||
        a->hp != b->hp || a->ticks != b->ticks ||
        a->fsp != b->fsp || a->block_count != b->block_count) return 0;
    if (a->sp > 0u && memcmp(a->stack, b->stack, (size_t)a->sp * sizeof(int64_t)) != 0) return 0;
    if (a->fsp > 0u && memcmp(a->frames, b->frames, (size_t)a->fsp * sizeof(VmFrame)) != 0) return 0;
    if (a->block_count > 0u &&
        memcmp(a->blocks, b->blocks, (size_t)a->block_count * sizeof(VmBlock)) != 0) return 0;
    if (a->hp == 0u) return 1;
    if (!a->heap || !b->heap) return 0;
    return memcmp(a->heap, b->heap, (size_t)a->hp) == 0;
}

static int32_t heap_i32(const VmState *vm, uint64_t addr)
{
    const uint8_t *p = vm->heap + addr;
    return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                     ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

/* Run a snippet's setup instructions, then assert the last one fails atomically. */
static void expect_error(VmState *vm, const VmInst *p, size_t n, VmStatus want)
{
    vm->ip = 0u;
    for (size_t i = 0; i + 1u < n; i++) {
        CHECK(vm_step(vm, p, n) == VM_OK);
    }
    VmState before = *vm;
    CHECK(vm_step(vm, p, n) == want);
    CHECK(same_state(vm, &before));
}

static void test_stack_ops(void)
{
    VmState vm;
    vm_init(&vm);
    const VmInst p[] = {
        {OP_PUSH, 7}, {OP_DUP, 0}, {OP_ADD, 0},
        {OP_PUSH, -3}, {OP_POP, 0}, {OP_HALT, 0}
    };
    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 14);

    /* Underflow must not consume state. */
    vm_init(&vm);
    const VmInst bad[] = {{OP_POP, 0}};
    expect_error(&vm, bad, NELEMS(bad), VM_ERR_STACK_UNDERFLOW);
    CHECK(vm.sp == 0u && vm.ticks == 0u && vm.ip == 0u);

    const VmInst one[] = {{OP_PUSH, 1}, {OP_ADD, 0}};
    expect_error(&vm, one, NELEMS(one), VM_ERR_STACK_UNDERFLOW);
    vm_free(&vm);
}

static void test_stack_shuffling(void)
{
    VmState vm;
    vm_init(&vm);
    const VmInst p[] = {
        {OP_PUSH, 1}, {OP_PUSH, 2}, {OP_SWAP, 0},
        {OP_PUSH, 3}, {OP_OVER, 0}, {OP_ROT, 0},
        {OP_PUSH, 1}, {OP_PICK, 0}, {OP_HALT, 0}
    };
    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
    /* [1,2] -> swap [2,1] -> push3 [2,1,3] -> over [2,1,3,1]
     * -> rot [2,3,1,1] -> push1 [2,3,1,1,1] -> pick(1) [2,3,1,1,1]
     * (PICK replaces n with the picked value, so sp stays at 5). */
    CHECK(vm.sp == 5u);
    CHECK(vm.stack[0] == 2 && vm.stack[1] == 3 && vm.stack[2] == 1);
    CHECK(vm.stack[3] == 1 && vm.stack[4] == 1);
    vm_free(&vm);

    const VmInst bad_pick[] = {{OP_PUSH, 1}, {OP_PUSH, 5}, {OP_PICK, 0}};
    expect_error(&vm, bad_pick, NELEMS(bad_pick), VM_ERR_BAD_ADDR);
    vm_free(&vm);
}

static void check_binary(VmOpcode op, int64_t x, int64_t y, int64_t want)
{
    VmState vm;
    vm_init(&vm);
    const VmInst p[] = {{OP_PUSH, x}, {OP_PUSH, y}, {(uint8_t)op, 0}, {OP_HALT, 0}};
    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
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
    check_binary(OP_ADD, INT64_MAX, 1, INT64_MIN); /* wrap without UB */
    check_binary(OP_MUL, INT64_MAX, 2, -2);
}

static void check_unary(VmOpcode op, int64_t x, int64_t want)
{
    VmState vm;
    vm_init(&vm);
    const VmInst p[] = {{OP_PUSH, x}, {(uint8_t)op, 0}, {OP_HALT, 0}};
    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
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
    const VmInst div0[] = {{OP_PUSH, 10}, {OP_PUSH, 0}, {OP_DIV, 0}};
    expect_error(&vm, div0, NELEMS(div0), VM_ERR_DIV_ZERO);
    CHECK(vm.sp == 2u && vm.stack[0] == 10 && vm.stack[1] == 0);
    CHECK(vm.ip == 2u && vm.ticks == 2u);

    const VmInst mod0[] = {{OP_PUSH, 10}, {OP_PUSH, 0}, {OP_MOD, 0}};
    expect_error(&vm, mod0, NELEMS(mod0), VM_ERR_DIV_ZERO);
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
    const VmInst under[] = {{OP_PUSH, 1}, {OP_XOR, 0}};
    expect_error(&vm, under, NELEMS(under), VM_ERR_STACK_UNDERFLOW);
    CHECK(vm.sp == 1u && vm.stack[0] == 1);
    vm_free(&vm);
}

static void test_jumps(void)
{
    const VmInst taken[] = {
        {OP_PUSH, 1}, {OP_JMP_Z, 4}, {OP_PUSH, 10}, {OP_HALT, 0},
        {OP_PUSH, 20}, {OP_HALT, 0}
    };
    const VmInst not_taken[] = {
        {OP_PUSH, 0}, {OP_JMP_Z, 4}, {OP_PUSH, 10}, {OP_HALT, 0},
        {OP_PUSH, 20}, {OP_HALT, 0}
    };
    const VmInst jmp[] = {
        {OP_PUSH, 0}, {OP_JMP, 3}, {OP_PUSH, 99}, {OP_PUSH, 42}, {OP_HALT, 0}
    };
    const VmInst indirect[] = {
        {OP_PUSH, 3}, {OP_JMP_I, 0}, {OP_PUSH, 99}, {OP_PUSH, 42}, {OP_HALT, 0}
    };
    VmState vm;

    vm_init(&vm);
    CHECK(vm_run(&vm, taken, NELEMS(taken), 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 10);

    vm_init(&vm);
    CHECK(vm_run(&vm, not_taken, NELEMS(not_taken), 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 20);

    vm_init(&vm);
    CHECK(vm_run(&vm, jmp, NELEMS(jmp), 0) == VM_HALTED);
    CHECK(vm.sp == 2u && vm.stack[0] == 0 && vm.stack[1] == 42);

    vm_init(&vm);
    CHECK(vm_run(&vm, indirect, NELEMS(indirect), 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 42);

    const VmInst bad_indirect[] = {{OP_PUSH, 99}, {OP_JMP_I, 0}};
    expect_error(&vm, bad_indirect, NELEMS(bad_indirect), VM_ERR_BAD_IP);
    vm_free(&vm);
}

static void test_store_load(void)
{
    VmState vm;
    vm_init(&vm);
    const VmInst p[] = {
        {OP_PUSH, 12}, {OP_ALLOC, 0}, {OP_POP, 0},
        {OP_PUSH, 0x01020304}, {OP_PUSH, 8}, {OP_STORE32, 0},
        {OP_PUSH, 8}, {OP_LOAD32, 0},
        {OP_PUSH, 7}, {OP_PUSH, 0}, {OP_STORE32, 0},
        {OP_PUSH, 0}, {OP_LOAD32, 0}, {OP_HALT, 0}
    };
    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
    CHECK(vm.sp == 2u && vm.stack[0] == 0x01020304 && vm.stack[1] == 7);
    CHECK(vm.heap[8] == 1 && vm.heap[9] == 2 && vm.heap[10] == 3 && vm.heap[11] == 4);
    vm_free(&vm);
}

static void test_memory_widths(void)
{
    VmState vm;
    vm_init(&vm);
    const VmInst p[] = {
        {OP_PUSH, 24}, {OP_ALLOC, 0}, {OP_POP, 0},
        {OP_PUSH, 0xAB},          {OP_PUSH, 1},  {OP_STORE8, 0},
        {OP_PUSH, 0x1234},        {OP_PUSH, 4},  {OP_STORE16, 0},
        {OP_PUSH, (int64_t)(uint32_t)0xFEDCBA98}, {OP_PUSH, 8},  {OP_STORE32, 0},
        {OP_PUSH, 0x0123456789ABCDEF}, {OP_PUSH, 12}, {OP_STORE64, 0},
        {OP_PUSH, 1},  {OP_LOAD8, 0},
        {OP_PUSH, 4},  {OP_LOAD16, 0},
        {OP_PUSH, 8},  {OP_LOAD32U, 0},
        {OP_PUSH, 8},  {OP_LOAD32, 0},
        {OP_PUSH, 12}, {OP_LOAD64, 0}, {OP_HALT, 0}
    };
    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
    CHECK(vm.sp == 5u);
    CHECK(vm.stack[0] == 0xAB);
    CHECK(vm.stack[1] == 0x1234);
    CHECK(vm.stack[2] == 0xFEDCBA98u); /* zero extended uint32 */
    CHECK(vm.stack[3] == (int64_t)(int32_t)0xFEDCBA98u); /* sign extended */
    CHECK(vm.stack[4] == 0x0123456789ABCDEF);
    vm_free(&vm);

    const VmInst oob[] = {{OP_PUSH, 0}, {OP_PUSH, 4}, {OP_STORE8, 0}}; /* no heap yet */
    expect_error(&vm, oob, NELEMS(oob), VM_ERR_BAD_ADDR);
    vm_free(&vm);
}

static void test_alloc_and_heap_bounds(void)
{
    VmState vm;
    vm_init(&vm);

    const VmInst alloc[] = {
        {OP_PUSH, 1}, {OP_ALLOC, 0}, {OP_PUSH, 5}, {OP_ALLOC, 0}, {OP_HALT, 0}
    };
    CHECK(vm_run(&vm, alloc, NELEMS(alloc), 0) == VM_HALTED);
    CHECK(vm.hp == 12u && vm.stack[0] == 0 && vm.stack[1] == 4);
    CHECK(vm.block_count == 2u && vm.blocks[0].live && vm.blocks[1].live);

    const VmInst oob[] = {{OP_PUSH, 12}, {OP_LOAD32, 0}};
    expect_error(&vm, oob, NELEMS(oob), VM_ERR_BAD_ADDR);
    const VmInst store_oob[] = {{OP_PUSH, 99}, {OP_PUSH, 12}, {OP_STORE32, 0}};
    expect_error(&vm, store_oob, NELEMS(store_oob), VM_ERR_BAD_ADDR);
    const VmInst negative[] = {{OP_PUSH, -1}, {OP_LOAD32, 0}};
    expect_error(&vm, negative, NELEMS(negative), VM_ERR_BAD_ADDR);
    const VmInst bad_size[] = {{OP_PUSH, 0}, {OP_ALLOC, 0}};
    expect_error(&vm, bad_size, NELEMS(bad_size), VM_ERR_BAD_ADDR);
    vm_free(&vm);

    VmState big;
    vm_init(&big);
    const VmInst grow[] = {
        {OP_PUSH, 8192}, {OP_ALLOC, 0}, {OP_POP, 0},
        {OP_PUSH, 12345}, {OP_PUSH, 8188}, {OP_STORE32, 0},
        {OP_PUSH, 8188}, {OP_LOAD32, 0}, {OP_HALT, 0}
    };
    CHECK(vm_run(&big, grow, NELEMS(grow), 0) == VM_HALTED);
    CHECK(big.hp == 8192u && big.heap_cap >= big.hp);
    CHECK(big.sp == 1u && big.stack[0] == 12345);
    vm_free(&big);
}

static void test_heap_reuse(void)
{
    VmState vm;
    vm_init(&vm);
    const VmInst p[] = {
        {OP_PUSH, 8}, {OP_ALLOC, 0}, {OP_POP, 0}, /* addr 0 */
        {OP_PUSH, 8}, {OP_ALLOC, 0}, {OP_POP, 0}, /* addr 8 */
        {OP_PUSH, 8}, {OP_FREE, 0},
        {OP_PUSH, 4}, {OP_ALLOC, 0}, /* reuses addr 8 */
        {OP_PUSH, 4}, {OP_ALLOC, 0}, /* reuses addr 12 from the split */
        {OP_HALT, 0}
    };
    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
    CHECK(vm.hp == 16u);
    CHECK(vm.sp == 2u && vm.stack[0] == 8 && vm.stack[1] == 12);
    CHECK(vm.block_count == 3u);
    CHECK(vm.blocks[0].live && vm.blocks[1].live && vm.blocks[2].live);

    const VmInst bad_free[] = {{OP_PUSH, 99}, {OP_FREE, 0}};
    expect_error(&vm, bad_free, NELEMS(bad_free), VM_ERR_BAD_ADDR);
    vm_free(&vm);
}

static void test_calls_and_locals(void)
{
    VmState vm;
    vm_init(&vm);
    const VmInst p[] = {
        {OP_PUSH, 10}, {OP_CALL, 7}, {OP_SWAP, 0}, {OP_ADD, 0}, {OP_HALT, 0},
        {OP_NOP, 0}, {OP_NOP, 0},
        /* 7: function */
        {OP_ENTER, 1},       /* 7 */
        {OP_LOCAL_GET, 0},   /* 8 */
        {OP_PUSH, 5},        /* 9 */
        {OP_ADD, 0},         /* 10 */
        {OP_LOCAL_SET, 0},   /* 11 */
        {OP_LOCAL_GET, 0},   /* 12 */
        {OP_RET, 0}          /* 13 */
    };
    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 15);
    CHECK(vm.fsp == 0u && vm.fp == 0u);

    /* Indirect call through a value on the stack. */
    vm_init(&vm);
    const VmInst ind[] = {
        {OP_PUSH, 10}, {OP_PUSH, 8}, {OP_CALL_I, 0}, {OP_SWAP, 0}, {OP_ADD, 0}, {OP_HALT, 0},
        {OP_NOP, 0}, {OP_NOP, 0},
        {OP_ENTER, 1}, {OP_PUSH, 42}, {OP_LOCAL_SET, 0},
        {OP_LOCAL_GET, 0}, {OP_RET, 0}
    };
    CHECK(vm_run(&vm, ind, NELEMS(ind), 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 52);

    const VmInst bad_ret[] = {{OP_RET, 0}};
    expect_error(&vm, bad_ret, NELEMS(bad_ret), VM_ERR_BAD_FRAME);
    vm_free(&vm);
}

static void test_floats(void)
{
    VmState vm;
    vm_init(&vm);
    const VmInst p[] = {
        {OP_PUSHF, vm_f64_to_bits(1.5)},
        {OP_PUSHF, vm_f64_to_bits(2.0)},
        {OP_FADD, 0},                 /* 1.5 + 2.0 = 3.5 */
        {OP_PUSHF, vm_f64_to_bits(2.0)},
        {OP_FMUL, 0},                 /* 3.5 * 2.0 = 7 */
        {OP_HALT, 0}
    };
    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
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

    const VmInst bad_target[] = {{OP_PUSH, 0}, {OP_JMP_Z, 999}};
    expect_error(&vm, bad_target, NELEMS(bad_target), VM_ERR_BAD_IP);
    const VmInst bad_op[] = {{255, 0}};
    expect_error(&vm, bad_op, NELEMS(bad_op), VM_ERR_BAD_OP);
    VmState before = vm;
    CHECK(vm_step(&vm, bad_target, 0) == VM_ERR_BAD_IP);
    CHECK(same_state(&vm, &before));
    vm_free(&vm);

    /* The operand stack grows past the old fixed 256-slot ceiling. */
    VmState packed;
    vm_init(&packed);
    VmInst fills[600];
    for (size_t i = 0; i < NELEMS(fills); i++) fills[i] = (VmInst){OP_PUSH, (int64_t)i};
    CHECK(vm_run(&packed, fills, NELEMS(fills), (uint32_t)NELEMS(fills)) == VM_OK);
    CHECK(packed.sp == 600u && packed.stack_cap >= 600u);
    CHECK(packed.stack[599] == 599);
    vm_free(&packed);
}

static void test_io(void)
{
    /* Keep stdout quiet: only exercise the opcodes' state transitions. */
    VmState vm;
    vm_init(&vm);
    const VmInst p[] = {
        {OP_PUSH, 4}, {OP_ALLOC, 0}, {OP_POP, 0},
        {OP_PUSH, 65}, {OP_PUSH, 0}, {OP_STORE8, 0},
        {OP_PUSH, 1}, {OP_PUSH, 0}, {OP_PRINT_STR, 0},
        {OP_PUSH, 66}, {OP_PRINT_CHAR, 0}, {OP_HALT, 0}
    };
    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
    CHECK(vm.sp == 0u && vm.hp == 4u && vm.heap[0] == 65);
    vm_free(&vm);
}

static void test_demo_control_flow(void)
{
    const VmInst p[] = {
        {OP_PUSH, 0}, {OP_PUSH, 4}, {OP_ALLOC, 0}, {OP_STORE32, 0},
        {OP_PUSH, 0}, {OP_DUP, 0}, {OP_PUSH, 6}, {OP_CMP_LT, 0}, {OP_JMP_Z, 22},
        {OP_DUP, 0}, {OP_POP, 0},
        {OP_PUSH, 0}, {OP_LOAD32, 0}, {OP_DUP, 0}, {OP_PUSH, 10}, {OP_ADD, 0},
        {OP_PUSH, 0}, {OP_STORE32, 0}, {OP_POP, 0},
        {OP_PUSH, 1}, {OP_ADD, 0}, {OP_JMP, 5},
        {OP_PUSH, 0}, {OP_LOAD32, 0}, {OP_HALT, 0}
    };
    VmState vm;
    vm_init(&vm);

    CHECK(vm_run(&vm, p, NELEMS(p), 0) == VM_HALTED);
    CHECK(vm.hp == 4u && heap_i32(&vm, 0) == 60);
    CHECK(vm.sp == 2u && vm.stack[0] == 6 && vm.stack[1] == 60);
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
    test_io();
    test_demo_control_flow();
    return failures;
}
