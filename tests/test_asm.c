#include <stdio.h>
#include <string.h>

#include "asm.h"
#include "tests.h"

static int failures;

#define CHECK(cond) do {                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static void expect_error(const char *src, size_t line, const char *needle)
{
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == -1);
    CHECK(prog.error_line == line);
    CHECK(strstr(prog.error, needle) != NULL);
    asm_program_free(&prog);
}

static void test_parse_and_run(void)
{
    const char *src =
        "; a comment\n"
        "# another style\n"
        "START: push 0x10   // inline comment\n"
        "        PUSH -3\n"
        "        ADD\n"
        "loop:   DUP\n"
        "        JMP_Z done\n"
        "        JMP done\n"
        "done:   HALT\n";

    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    if (prog.count != 7u) {
        CHECK(prog.count == 7u);
        fprintf(stderr, "  parse error: %s\n", prog.error);
    } else {
        CHECK(prog.insts[0].op == OP_PUSH && prog.insts[0].arg == 16);
        CHECK(prog.insts[1].op == OP_PUSH && prog.insts[1].arg == -3);
        CHECK(prog.insts[2].op == OP_ADD);
        CHECK(prog.insts[3].op == OP_DUP);
        CHECK(prog.insts[4].op == OP_JMP_Z && prog.insts[4].arg == 6);
        CHECK(prog.insts[5].op == OP_JMP && prog.insts[5].arg == 6);
        CHECK(prog.insts[6].op == OP_HALT);

        VmState vm;
        vm_init(&vm);
        CHECK(vm_run(&vm, prog.insts, prog.count, 0) == VM_HALTED);
        CHECK(vm.sp == 1u && vm.stack[0] == 13);
        vm_free(&vm);
    }
    asm_program_free(&prog);
}

static void test_file(void)
{
    const char *path = "build/test_program.asm";
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (!f) return;
    fputs("PUSH 40\nPUSH 2\nADD\nHALT\n", f);
    fclose(f);

    AsmProgram prog;
    CHECK(asm_parse_file(&prog, path) == 0);
    if (prog.count == 4u) {
        VmState vm;
        vm_init(&vm);
        CHECK(vm_run(&vm, prog.insts, prog.count, 0) == VM_HALTED);
        CHECK(vm.sp == 1u && vm.stack[0] == 42);
        vm_free(&vm);
    } else {
        CHECK(prog.count == 4u);
    }
    asm_program_free(&prog);
    remove(path);
}

static void test_i64_literals(void)
{
    const char *src =
        "PUSH 9223372036854775807\n"
        "PUSH -9223372036854775808\n"
        "ADD\n"
        "HALT\n";
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    if (prog.count == 4u) {
        CHECK(prog.insts[0].arg == INT64_MAX);
        CHECK(prog.insts[1].arg == INT64_MIN);
        VmState vm;
        vm_init(&vm);
        CHECK(vm_run(&vm, prog.insts, prog.count, 0) == VM_HALTED);
        CHECK(vm.sp == 1u && vm.stack[0] == -1);
        vm_free(&vm);
    } else {
        CHECK(prog.count == 4u);
    }
    asm_program_free(&prog);

    expect_error("PUSH 9223372036854775808\n", 1, "invalid integer");
}

static void test_new_opcodes(void)
{
    const char *src =
        "div\nmod\ncmp_eq\ncmp_ne\ncmp_le\ncmp_gt\ncmp_ge\n"
        "and\nor\nxor\nnot\nshl\nshr\nlnot\nland\nlor\nHALT\n";
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    if (prog.count == 17u) {
        CHECK(prog.insts[0].op == OP_DIV);
        CHECK(prog.insts[1].op == OP_MOD);
        CHECK(prog.insts[2].op == OP_CMP_EQ);
        CHECK(prog.insts[5].op == OP_CMP_GT);
        CHECK(prog.insts[8].op == OP_OR);
        CHECK(prog.insts[10].op == OP_NOT);
        CHECK(prog.insts[12].op == OP_SHR);
        CHECK(prog.insts[15].op == OP_LOR);
    } else {
        CHECK(prog.count == 17u);
        fprintf(stderr, "  parse error: %s\n", prog.error);
    }
    asm_program_free(&prog);

    const char *run_src = "PUSH 1071\nPUSH 462\nDIV\nPUSH 2\nMOD\nHALT\n";
    CHECK(asm_parse_string(&prog, run_src) == 0);
    if (prog.count == 6u) {
        VmState vm;
        vm_init(&vm);
        CHECK(vm_run(&vm, prog.insts, prog.count, 0) == VM_HALTED);
        CHECK(vm.sp == 1u && vm.stack[0] == 0);
        vm_free(&vm);
    }
    asm_program_free(&prog);
}

static void test_extended_opcodes(void)
{
    const char *src =
        "SWAP\nOVER\nROT\nPICK\n"
        "LOAD8\nSTORE8\nLOAD16\nSTORE16\nLOAD32U\nLOAD64\nSTORE64\nFREE\n"
        "CALL fn\nCALL_I\nRET\nENTER 2\nGET 0\nSET 1\nJMP_I\n"
        "PUSHF 1.5\nFADD\nFSUB\nFMUL\nFDIV\nFNEG\nFABS\nFSQRT\n"
        "FCMP_LT\nFCMP_EQ\nFCMP_LE\nFCMP_GT\nFCMP_GE\nFTOI\nITOF\nFPRINT\n"
        "PRINT_STR\nPRINT_CHAR\nREAD_INT\nREAD_STR\n"
        "fn: HALT\n";
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    if (prog.count == 40u) {
        CHECK(prog.insts[0].op == OP_SWAP);
        CHECK(prog.insts[11].op == OP_FREE);
        CHECK(prog.insts[12].op == OP_CALL && prog.insts[12].arg == 39);
        CHECK(prog.insts[15].op == OP_ENTER && prog.insts[15].arg == 2);
        CHECK(prog.insts[16].op == OP_LOCAL_GET && prog.insts[16].arg == 0);
        CHECK(prog.insts[17].op == OP_LOCAL_SET && prog.insts[17].arg == 1);
        CHECK(prog.insts[19].op == OP_PUSHF);
        CHECK(vm_bits_to_f64(prog.insts[19].arg) == 1.5);
        CHECK(prog.insts[39].op == OP_HALT);
    } else {
        CHECK(prog.count == 40u);
        fprintf(stderr, "  parse error: %s\n", prog.error);
    }
    asm_program_free(&prog);
}

static void test_expressions_and_constants(void)
{
    const char *src =
        "EQU N 4\n"
        "EQU BASE 0x10\n"
        "PUSH N*2+(BASE-8)\n"
        "PUSH -(3+4)\n"
        "PUSH ~0\n"
        "CALL fn\n"
        "HALT\n"
        "fn: HALT\n";
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    if (prog.count == 6u) {
        CHECK(prog.insts[0].arg == 16);
        CHECK(prog.insts[1].arg == -7);
        CHECK(prog.insts[2].arg == -1);
        CHECK(prog.insts[3].op == OP_CALL && prog.insts[3].arg == 5);
        VmState vm;
        vm_init(&vm);
        CHECK(vm_run(&vm, prog.insts, prog.count, 0) == VM_HALTED);
        CHECK(vm.sp == 3u && vm.stack[0] == 16 && vm.stack[1] == -7);
        vm_free(&vm);
    } else {
        CHECK(prog.count == 6u);
        fprintf(stderr, "  parse error: %s\n", prog.error);
    }
    asm_program_free(&prog);
}

static void test_pseudo_ops(void)
{
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, "PUSH 5\nINC\nDEC\nNEG\nHALT\n") == 0);
    if (prog.count == 9u) {
        CHECK(prog.insts[1].op == OP_PUSH && prog.insts[1].arg == 1);
        CHECK(prog.insts[2].op == OP_ADD);
        CHECK(prog.insts[3].op == OP_PUSH && prog.insts[3].arg == 1);
        CHECK(prog.insts[4].op == OP_SUB);
        CHECK(prog.insts[5].op == OP_PUSH && prog.insts[5].arg == 0);
        CHECK(prog.insts[6].op == OP_SWAP);
        CHECK(prog.insts[7].op == OP_SUB);
        VmState vm;
        vm_init(&vm);
        CHECK(vm_run(&vm, prog.insts, prog.count, 0) == VM_HALTED);
        CHECK(vm.sp == 1u && vm.stack[0] == -5);
        vm_free(&vm);
    } else {
        CHECK(prog.count == 9u);
    }
    asm_program_free(&prog);
}

static void test_data_directives(void)
{
    const char *src =
        "DB 1, 2, 0x41\n"
        "STR \"hi\\n\"\n"
        "ALIGN 8\n"
        "DQ 0x0123456789ABCDEF\n"
        "HALT\n";
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    /* 16 data bytes * 3 prologue instructions + 3 header + 1 HALT = 52. */
    if (prog.count == 52u) {
        CHECK(prog.insts[0].op == OP_PUSH && prog.insts[0].arg == 16);
        CHECK(prog.insts[1].op == OP_ALLOC);
        CHECK(prog.insts[2].op == OP_POP);
        CHECK(prog.insts[3].op == OP_PUSH && prog.insts[3].arg == 1);
        CHECK(prog.insts[4].op == OP_PUSH && prog.insts[4].arg == 0);
        CHECK(prog.insts[5].op == OP_STORE8);
        CHECK(prog.insts[51].op == OP_HALT);

        VmState vm;
        vm_init(&vm);
        CHECK(vm_run(&vm, prog.insts, prog.count, 0) == VM_HALTED);
        CHECK(vm.hp == 16u && vm.sp == 0u);
        CHECK(vm.heap[0] == 1 && vm.heap[1] == 2 && vm.heap[2] == 0x41);
        CHECK(vm.heap[3] == 'h' && vm.heap[4] == 'i' && vm.heap[5] == '\n');
        CHECK(vm.heap[6] == 0 && vm.heap[7] == 0);
        CHECK(vm.heap[8] == 0x01 && vm.heap[15] == 0xEF);
        vm_free(&vm);
    } else {
        CHECK(prog.count == 52u);
        fprintf(stderr, "  parse error: %s\n", prog.error);
    }
    asm_program_free(&prog);

    /* A program with no data directives has no prologue: indices are unchanged. */
    CHECK(asm_parse_string(&prog, "PUSH 1\nHALT\n") == 0);
    CHECK(prog.count == 2u && prog.insts[0].op == OP_PUSH);
    asm_program_free(&prog);
}

static void test_line_mapping(void)
{
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, "PUSH 1\nPUSH 2\nADD\nHALT\n") == 0);
    CHECK(prog.count == 4u);
    CHECK(asm_line_of(&prog, 0) == 1u);
    CHECK(asm_line_of(&prog, 2) == 3u);
    CHECK(asm_line_of(&prog, 4) == 0u); /* out of range */
    asm_program_free(&prog);
}

static void test_errors(void)
{
    expect_error("FOO 1\n", 1, "unknown instruction");
    expect_error("push\n", 1, "requires an integer operand");
    expect_error("PUSH abc\n", 1, "invalid integer");
    expect_error("HALT 1\n", 1, "takes no operand");
    expect_error("JMP nowhere\nHALT\n", 1, "undefined label");
    expect_error("L: HALT\nL: HALT\n", 2, "duplicate label");
    expect_error("bad-label: HALT\n", 1, "invalid label name");
    expect_error("JMP 99\nHALT\n", 1, "out of range");
    expect_error("\n; only comments\n", 2, "no instructions");
    expect_error("STR hello\nHALT\n", 1, "quoted string");
    expect_error("EQU X 1\nEQU X 2\nHALT\n", 2, "duplicate constant");
    expect_error("SET -1\nHALT\n", 1, "invalid local index");
    expect_error("PUSHF abc\nHALT\n", 1, "invalid float");
    expect_error("CALL 99\nHALT\n", 1, "out of range");

    AsmProgram prog;
    CHECK(asm_parse_file(&prog, "build/does_not_exist.asm") == -1);
    CHECK(strstr(prog.error, "cannot open") != NULL);
    asm_program_free(&prog);
}

int test_asm(void)
{
    test_parse_and_run();
    test_file();
    test_i64_literals();
    test_new_opcodes();
    test_extended_opcodes();
    test_expressions_and_constants();
    test_pseudo_ops();
    test_data_directives();
    test_line_mapping();
    test_errors();
    return failures;
}
