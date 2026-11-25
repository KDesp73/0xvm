#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "asm.h"
#include "tests.h"

static int failures;

#define CHECK(cond) do {                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static uint64_t read_be(const uint8_t *p, unsigned width)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < width; i++) v = (v << 8) | (uint64_t)p[i];
    return v;
}

/* Absolute RAM address of the first byte of the Nth emitted instruction. */
static size_t inst_offset(const AsmProgram *prog, size_t index)
{
    size_t off = (size_t)read_be(prog->bytes + 1u, 2u) - VM_IMAGE_START;
    while (index-- > 0u) {
        if (off >= prog->len) return prog->len;
        off += vm_inst_size(prog->bytes[off]);
    }
    return off;
}

static uint8_t op_at(const AsmProgram *prog, size_t index)
{
    return prog->bytes[inst_offset(prog, index)];
}

static int64_t arg_at(const AsmProgram *prog, size_t index)
{
    size_t off = inst_offset(prog, index);
    unsigned width = (unsigned)vm_inst_size(prog->bytes[off]) - 1u;
    return (int64_t)read_be(prog->bytes + off + 1u, width);
}

static size_t inst_count(const AsmProgram *prog)
{
    size_t off = (size_t)read_be(prog->bytes + 1u, 2u) - VM_IMAGE_START;
    size_t n = 0;
    while (off < prog->len) {
        off += vm_inst_size(prog->bytes[off]);
        n++;
    }
    return n;
}

static void run_ok(const AsmProgram *prog, VmState *vm)
{
    vm_init(vm);
    CHECK(vm_load_image(vm, prog->bytes, prog->len, VM_IMAGE_START) == VM_OK);
    CHECK(vm_run(vm, 0) == VM_HALTED);
}

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
    CHECK(inst_count(&prog) == 7u);
    if (prog.len > 0u) {
        CHECK(prog.bytes[0] == OP_JMP); /* skip the data area */
        CHECK(op_at(&prog, 0) == OP_PUSH && arg_at(&prog, 0) == 16);
        CHECK(op_at(&prog, 1) == OP_PUSH && arg_at(&prog, 1) == -3);
        CHECK(op_at(&prog, 2) == OP_ADD);
        CHECK(op_at(&prog, 3) == OP_DUP);
        CHECK(op_at(&prog, 4) == OP_JMP_Z &&
              arg_at(&prog, 4) == (int64_t)(VM_IMAGE_START + inst_offset(&prog, 6)));
        CHECK(op_at(&prog, 5) == OP_JMP &&
              arg_at(&prog, 5) == (int64_t)(VM_IMAGE_START + inst_offset(&prog, 6)));
        CHECK(op_at(&prog, 6) == OP_HALT);

        VmState vm;
        run_ok(&prog, &vm);
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
    CHECK(inst_count(&prog) == 4u);
    if (prog.len > 0u) {
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == 42);
        vm_free(&vm);
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
    CHECK(inst_count(&prog) == 4u);
    if (prog.len > 0u) {
        CHECK(arg_at(&prog, 0) == INT64_MAX);
        CHECK(arg_at(&prog, 1) == INT64_MIN);
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == -1);
        vm_free(&vm);
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
    CHECK(inst_count(&prog) == 17u);
    if (prog.len > 0u) {
        CHECK(op_at(&prog, 0) == OP_DIV);
        CHECK(op_at(&prog, 1) == OP_MOD);
        CHECK(op_at(&prog, 2) == OP_CMP_EQ);
        CHECK(op_at(&prog, 5) == OP_CMP_GT);
        CHECK(op_at(&prog, 8) == OP_OR);
        CHECK(op_at(&prog, 10) == OP_NOT);
        CHECK(op_at(&prog, 12) == OP_SHR);
        CHECK(op_at(&prog, 15) == OP_LOR);
    }
    asm_program_free(&prog);

    const char *run_src = "PUSH 1071\nPUSH 462\nDIV\nPUSH 2\nMOD\nHALT\n";
    CHECK(asm_parse_string(&prog, run_src) == 0);
    if (prog.len > 0u) {
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == 0);
        vm_free(&vm);
    }
    asm_program_free(&prog);
}

static void test_extended_opcodes(void)
{
    const char *src =
        "INCLUDE \"locals.asm\"\n"
        "SWAP\nOVER\nROT\nPICK\n"
        "LOAD8\nSTORE8\nLOAD16\nSTORE16\nLOAD32U\nLOAD64\nSTORE64\nFREE\n"
        "CALL fn\nCALL_I\nRET\nENTER 2\nGET 0\nSET 1\nJMP_I\n"
        "PUSHF 1.5\nFADD\nFSUB\nFMUL\nFDIV\nFNEG\nFABS\nFSQRT\n"
        "FCMP_LT\nFCMP_EQ\nFCMP_LE\nFCMP_GT\nFCMP_GE\nFTOI\nITOF\n"
        "DEI 0x1B\nDEO 0x10\n"
        "fn: HALT\n";
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    CHECK(inst_count(&prog) == 37u);
    if (prog.len > 0u) {
        CHECK(op_at(&prog, 0) == OP_SWAP);
        CHECK(op_at(&prog, 11) == OP_FREE);
        CHECK(op_at(&prog, 12) == OP_CALL &&
              arg_at(&prog, 12) == (int64_t)(VM_IMAGE_START + inst_offset(&prog, 36)));
        CHECK(op_at(&prog, 15) == OP_ENTER && arg_at(&prog, 15) == 2);
        CHECK(op_at(&prog, 16) == OP_LOCAL_GET && arg_at(&prog, 16) == 0);
        CHECK(op_at(&prog, 17) == OP_LOCAL_SET && arg_at(&prog, 17) == 1);
        CHECK(op_at(&prog, 19) == OP_PUSHF);
        CHECK(vm_bits_to_f64(arg_at(&prog, 19)) == 1.5);
        CHECK(op_at(&prog, 34) == OP_DEI && arg_at(&prog, 34) == VM_CONSOLE_GETI);
        CHECK(op_at(&prog, 35) == OP_DEO && arg_at(&prog, 35) == VM_CONSOLE_PUTC);
        CHECK(op_at(&prog, 36) == OP_HALT);
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
    CHECK(inst_count(&prog) == 6u);
    if (prog.len > 0u) {
        CHECK(arg_at(&prog, 0) == 16);
        CHECK(arg_at(&prog, 1) == -7);
        CHECK(arg_at(&prog, 2) == -1);
        CHECK(op_at(&prog, 3) == OP_CALL &&
              arg_at(&prog, 3) == (int64_t)(VM_IMAGE_START + inst_offset(&prog, 5)));
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 3u && vm.stack[0] == 16 && vm.stack[1] == -7);
        vm_free(&vm);
    }
    asm_program_free(&prog);
}

static void test_pseudo_ops(void)
{
    AsmProgram prog;
    CHECK(asm_parse_string(&prog,
        "INCLUDE \"all.asm\"\nPUSH 5\nINC\nDEC\nNEG\nHALT\n") == 0);
    CHECK(inst_count(&prog) == 9u);
    if (prog.len > 0u) {
        CHECK(op_at(&prog, 1) == OP_PUSH && arg_at(&prog, 1) == 1);
        CHECK(op_at(&prog, 2) == OP_ADD);
        CHECK(op_at(&prog, 3) == OP_PUSH && arg_at(&prog, 3) == 1);
        CHECK(op_at(&prog, 4) == OP_SUB);
        CHECK(op_at(&prog, 5) == OP_PUSH && arg_at(&prog, 5) == 0);
        CHECK(op_at(&prog, 6) == OP_SWAP);
        CHECK(op_at(&prog, 7) == OP_SUB);
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == -5);
        vm_free(&vm);
    }
    asm_program_free(&prog);
}

static void test_console_pseudo_ops(void)
{
    AsmProgram prog;
    CHECK(asm_parse_string(&prog,
        "INCLUDE \"console.asm\"\n"
        "PUSH 65\nPRINT_CHAR\nPUSH 42\nPRINT\nPUSHF 1.5\nFPRINT\n"
        "READ_INT\nHALT\n") == 0);
    CHECK(inst_count(&prog) == 8u);
    if (prog.len > 0u) {
        CHECK(op_at(&prog, 1) == OP_DEO && arg_at(&prog, 1) == VM_CONSOLE_PUTC);
        CHECK(op_at(&prog, 3) == OP_DEO && arg_at(&prog, 3) == VM_CONSOLE_PUTI);
        CHECK(op_at(&prog, 5) == OP_DEO && arg_at(&prog, 5) == VM_CONSOLE_PUTF);
        CHECK(op_at(&prog, 6) == OP_DEI && arg_at(&prog, 6) == VM_CONSOLE_GETI);
    }
    asm_program_free(&prog);

    CHECK(asm_parse_string(&prog,
        "INCLUDE \"console.asm\"\n"
        "PUSH 0x0103\nPUSH 4\nPRINT_STR\n"
        "PUSH 0x0200\nPUSH 16\nREAD_STR\nHALT\n") == 0);
    CHECK(inst_count(&prog) == 14u);
    if (prog.len > 0u) {
        CHECK(op_at(&prog, 2) == OP_DEO && arg_at(&prog, 2) == VM_CONSOLE_PUTS_LEN);
        CHECK(op_at(&prog, 3) == OP_DEO && arg_at(&prog, 3) == VM_CONSOLE_PUTS_ADDR);
        CHECK(op_at(&prog, 4) == OP_PUSH && arg_at(&prog, 4) == 0);
        CHECK(op_at(&prog, 5) == OP_DEO && arg_at(&prog, 5) == VM_CONSOLE_PUTS);
        CHECK(op_at(&prog, 8) == OP_DEO && arg_at(&prog, 8) == VM_CONSOLE_GETS_MAX);
        CHECK(op_at(&prog, 9) == OP_DEO && arg_at(&prog, 9) == VM_CONSOLE_GETS_ADDR);
        CHECK(op_at(&prog, 10) == OP_PUSH && arg_at(&prog, 10) == 0);
        CHECK(op_at(&prog, 11) == OP_DEO && arg_at(&prog, 11) == VM_CONSOLE_GETS);
        CHECK(op_at(&prog, 12) == OP_DEI && arg_at(&prog, 12) == VM_CONSOLE_GETS_LEN);
    }
    asm_program_free(&prog);
}

static void test_stdlib_ports(void)
{
    static const char *const names[] = {
        "SYS_VERSION", "SYS_DEBUG", "SYS_HALT",
        "CONSOLE_PUTC", "CONSOLE_GETC", "CONSOLE_PUTI", "CONSOLE_PUTF",
        "CONSOLE_PUTS_ADDR", "CONSOLE_PUTS_LEN", "CONSOLE_PUTS",
        "CONSOLE_GETS_ADDR", "CONSOLE_GETS_MAX", "CONSOLE_GETS",
        "CONSOLE_GETS_LEN", "CONSOLE_GETI",
        "SCREEN_WIDTH", "SCREEN_HEIGHT", "SCREEN_COLOR", "SCREEN_PIXEL",
        "SCREEN_DRAW", "SCREEN_CLEAR", "CONTROLLER_KEY", "CONTROLLER_QUIT",
        "TIMER_MS"
    };
    static const int64_t expected[] = {
        VM_SYS_VERSION, VM_SYS_DEBUG, VM_SYS_HALT,
        VM_CONSOLE_PUTC, VM_CONSOLE_GETC, VM_CONSOLE_PUTI, VM_CONSOLE_PUTF,
        VM_CONSOLE_PUTS_ADDR, VM_CONSOLE_PUTS_LEN, VM_CONSOLE_PUTS,
        VM_CONSOLE_GETS_ADDR, VM_CONSOLE_GETS_MAX, VM_CONSOLE_GETS,
        VM_CONSOLE_GETS_LEN, VM_CONSOLE_GETI,
        VM_SCREEN_WIDTH, VM_SCREEN_HEIGHT, VM_SCREEN_COLOR, VM_SCREEN_PIXEL,
        VM_SCREEN_DRAW, VM_SCREEN_CLEAR, VM_CONTROLLER_KEY, VM_CONTROLLER_QUIT,
        VM_TIMER_MS
    };
    const size_t count = sizeof expected / sizeof expected[0];

    char src[1024];
    size_t used = (size_t)snprintf(src, sizeof src, "INCLUDE \"ports.asm\"\n");
    for (size_t i = 0u; i < count; i++)
        used += (size_t)snprintf(src + used, sizeof src - used, "DEO %s\n", names[i]);
    snprintf(src + used, sizeof src - used, "HALT\n");

    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    CHECK(inst_count(&prog) == count + 1u);
    if (prog.len > 0u) {
        for (size_t i = 0u; i < count; i++) {
            CHECK(op_at(&prog, i) == OP_DEO);
            CHECK(arg_at(&prog, i) == expected[i]);
        }
    }
    asm_program_free(&prog);
}

static void test_pseudo_opt_in(void)
{
    /* Without an INCLUDE, readable names are just unknown instructions. */
    expect_error("PUSH 5\nINC\nHALT\n", 2, "unknown instruction");
    expect_error("PUSH 5\nPRINT\nHALT\n", 2, "unknown instruction");
    expect_error("ENTER 1\nSET 0\nHALT\n", 2, "unknown instruction");

    /* User macros own the stdlib names; nothing is reserved in the core. */
    AsmProgram prog;
    CHECK(asm_parse_string(&prog,
        "MACRO PRINT\n    PUSH 7\n    DEO 0x12\nENDMACRO\nPRINT\nHALT\n") == 0);
    if (prog.len > 0u) {
        CHECK(inst_count(&prog) == 3u);
        CHECK(op_at(&prog, 0) == OP_PUSH && arg_at(&prog, 0) == 7);
        CHECK(op_at(&prog, 1) == OP_DEO && arg_at(&prog, 1) == VM_CONSOLE_PUTI);
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
    /* 3-byte entry JMP + 16 data bytes + 1-byte HALT. */
    CHECK(prog.len == 20u);
    CHECK(inst_count(&prog) == 1u);
    if (prog.len > 0u) {
        const uint8_t *d = prog.bytes + 3u;
        CHECK(d[0] == 1 && d[1] == 2 && d[2] == 0x41);
        CHECK(d[3] == 'h' && d[4] == 'i' && d[5] == '\n');
        CHECK(d[6] == 0 && d[7] == 0);
        CHECK(d[8] == 0x01 && d[15] == 0xEF);
        CHECK(op_at(&prog, 0) == OP_HALT);

        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 0u && vm.hp == VM_HEAP_START); /* data stays in the image */
        CHECK(vm.mem[VM_IMAGE_START + 3u] == 1);
        CHECK(vm.mem[VM_IMAGE_START + 11u] == 0x01); /* DQ after ALIGN 8 */
        vm_free(&vm);
    }
    asm_program_free(&prog);

    /* A data label resolves to its absolute byte address in the image. */
    CHECK(asm_parse_string(&prog, "msg: STR \"hi\"\nPUSH msg\nLOAD16\nHALT\n") == 0);
    if (prog.len > 0u) {
        CHECK(op_at(&prog, 0) == OP_PUSH);
        CHECK(arg_at(&prog, 0) == (int64_t)(VM_IMAGE_START + 3u));
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == 0x6869);
        vm_free(&vm);
    }
    asm_program_free(&prog);
}

static void test_line_mapping(void)
{
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, "PUSH 1\nPUSH 2\nADD\nHALT\n") == 0);
    CHECK(inst_count(&prog) == 4u);
    CHECK(asm_line_of(&prog, VM_IMAGE_START) == 1u); /* generated entry JMP */
    CHECK(asm_line_of(&prog, VM_IMAGE_START + inst_offset(&prog, 0)) == 1u);
    CHECK(asm_line_of(&prog, VM_IMAGE_START + inst_offset(&prog, 2)) == 3u);
    CHECK(asm_line_of(&prog, VM_IMAGE_START + inst_offset(&prog, 4)) == 0u);
    asm_program_free(&prog);
}

static void test_expression_operators(void)
{
    const char *src =
        "EQU N 4\n"
        "EQU BASE 0x10\n"
        "PUSH N*2 + (BASE-8)\n"
        "PUSH 1 << 4 | 3\n"
        "PUSH 0xF0 & 0x3C\n"
        "PUSH (7 >= 7) + (3 < 2)\n"
        "PUSH 1 & 2 == 0\n"
        "PUSH 1 | 2 & 4\n"
        "ADD\nADD\nADD\nADD\nADD\nHALT\n";
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    CHECK(inst_count(&prog) == 12u);
    if (prog.len > 0u) {
        CHECK(arg_at(&prog, 0) == 16);
        CHECK(arg_at(&prog, 1) == 19);
        CHECK(arg_at(&prog, 2) == 0x30);
        CHECK(arg_at(&prog, 3) == 1);
        CHECK(arg_at(&prog, 4) == 0);
        CHECK(arg_at(&prog, 5) == 1);
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == 85);
        vm_free(&vm);
    }
    asm_program_free(&prog);
}

static void test_conditionals(void)
{
    const char *src =
        "EQU N 2\n"
        "IF N > 1\n"
        "  PUSH 10\n"
        "ELSE\n"
        "  PUSH 99\n"
        "ENDIF\n"
        "IFDEF N\n"
        "  PUSH 20\n"
        "ENDIF\n"
        "IFNDEF MISSING\n"
        "  PUSH 30\n"
        "ENDIF\n"
        "IF 0\n"
        "  FOO 1\n"
        "  skipped: HALT\n"
        "ENDIF\n"
        "ADD\nADD\nHALT\n";
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    CHECK(inst_count(&prog) == 6u);
    if (prog.len > 0u) {
        CHECK(arg_at(&prog, 0) == 10);
        CHECK(arg_at(&prog, 1) == 20);
        CHECK(arg_at(&prog, 2) == 30);
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == 60);
        vm_free(&vm);
    }
    asm_program_free(&prog);

    CHECK(asm_parse_string(&prog,
        "IF 0\nEQU X 1\nskipped: FOO\nENDIF\n"
        "IFDEF X\nFOO\nENDIF\nHALT\n") == 0);
    asm_program_free(&prog);

    expect_error("IF 1\nPUSH 1\n", 1, "unterminated IF");
    expect_error("ELSE\nHALT\n", 1, "ELSE without IF");
    expect_error("IF 0\nELSE\nELSE\nENDIF\nHALT\n", 3, "duplicate ELSE");
}

static void test_macros(void)
{
    const char *src =
        "MACRO PUSH2 a, b\n"
        "  PUSH %a\n"
        "  PUSH %b\n"
        "  ADD\n"
        "ENDMACRO\n"
        "MACRO SKIP_IF_ZERO n\n"
        "  PUSH %1\n"
        "  JMP_Z %%done\n"
        "  %%done:\n"
        "ENDMACRO\n"
        "PUSH2 20, 22\n"
        "PUSH2 1, 2\n"
        "ADD\n"
        "SKIP_IF_ZERO 0\n"
        "SKIP_IF_ZERO 1\n"
        "HALT\n";
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, src) == 0);
    CHECK(inst_count(&prog) == 12u);
    if (prog.len > 0u) {
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == 45);
        vm_free(&vm);
        CHECK(asm_line_of(&prog, VM_IMAGE_START + inst_offset(&prog, 0)) == 11u);
        CHECK(asm_line_of(&prog, VM_IMAGE_START + inst_offset(&prog, 6)) == 13u);
        CHECK(asm_line_of(&prog, VM_IMAGE_START + inst_offset(&prog, 7)) == 14u);
    }
    asm_program_free(&prog);

    const char *nested =
        "MACRO INNER n\n"
        "  PUSH %n\n"
        "  IF %n > 0\n"
        "    PUSH 1\n"
        "  ELSE\n"
        "    PUSH 2\n"
        "  ENDIF\n"
        "  ADD\n"
        "ENDMACRO\n"
        "MACRO OUTER n\n"
        "  INNER %1\n"
        "ENDMACRO\n"
        "OUTER 3\n"
        "OUTER 0\n"
        "ADD\n"
        "HALT\n";
    CHECK(asm_parse_string(&prog, nested) == 0);
    if (prog.len > 0u) {
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == 6);
        vm_free(&vm);
    }
    asm_program_free(&prog);

    expect_error("MACRO M a\nPUSH %a\nENDMACRO\nM 1, 2\nHALT\n",
                 4, "expects 1 argument");
    expect_error("MACRO M a\nPUSH %b\nENDMACRO\nM 1\nHALT\n",
                 2, "unknown macro parameter");
    expect_error("MACRO HALT a\nNOP\nENDMACRO\nHALT\n",
                 1, "reserved macro name");
    expect_error("MACRO M\nNOP\n", 1, "unterminated MACRO");
}

static void write_temp(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    if (!f) return;
    fputs(text, f);
    fclose(f);
}

static void test_includes(void)
{
    const char *lib = "build/asm_inc_lib.asm";
    const char *main_path = "build/asm_inc_main.asm";
    write_temp(lib,
        "EQU LIB_VALUE 7\n"
        "MACRO INC_BY n\n"
        "  PUSH %n\n"
        "  ADD\n"
        "ENDMACRO\n");
    write_temp(main_path,
        "INCLUDE \"asm_inc_lib.asm\"\n"
        "PUSH LIB_VALUE\n"
        "INC_BY 5\n"
        "HALT\n");

    AsmProgram prog;
    CHECK(asm_parse_file(&prog, main_path) == 0);
    CHECK(inst_count(&prog) == 4u);
    if (prog.len > 0u) {
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == 12);
        vm_free(&vm);
        CHECK(asm_line_of(&prog, VM_IMAGE_START + inst_offset(&prog, 1)) == 3u);
    }
    asm_program_free(&prog);

    const char *bad_lib = "build/asm_inc_bad.asm";
    const char *bad_main = "build/asm_inc_bad_main.asm";
    write_temp(bad_lib, "FOO 1\n");
    write_temp(bad_main, "INCLUDE \"asm_inc_bad.asm\"\nHALT\n");
    CHECK(asm_parse_file(&prog, bad_main) == -1);
    CHECK(strstr(prog.error, "asm_inc_bad.asm:1") != NULL);
    CHECK(strstr(prog.error, "unknown instruction") != NULL);
    asm_program_free(&prog);

    const char *cycle = "build/asm_inc_cycle.asm";
    write_temp(cycle, "INCLUDE \"asm_inc_cycle.asm\"\nHALT\n");
    CHECK(asm_parse_file(&prog, cycle) == -1);
    CHECK(strstr(prog.error, "include cycle") != NULL);
    asm_program_free(&prog);

    const char *missing = "build/asm_inc_missing_main.asm";
    write_temp(missing, "INCLUDE \"no_such_file.asm\"\nHALT\n");
    CHECK(asm_parse_file(&prog, missing) == -1);
    CHECK(strstr(prog.error, "cannot open include") != NULL);
    asm_program_free(&prog);

    /* Relative includes resolve against the including file's directory. */
    const char *sub_dir = "build/asm_inc_sub";
    const char *nested_main = "build/asm_inc_sub/main.asm";
    (void)mkdir(sub_dir, 0755);
    write_temp(nested_main,
        "INCLUDE \"../asm_inc_lib.asm\"\n"
        "PUSH LIB_VALUE\n"
        "INC_BY 5\n"
        "HALT\n");
    CHECK(asm_parse_file(&prog, nested_main) == 0);
    if (prog.len > 0u) {
        VmState vm;
        run_ok(&prog, &vm);
        CHECK(vm.sp == 1u && vm.stack[0] == 12);
        vm_free(&vm);
    }
    asm_program_free(&prog);

    remove(nested_main);
    rmdir(sub_dir);
    remove(lib);
    remove(main_path);
    remove(bad_lib);
    remove(bad_main);
    remove(cycle);
    remove(missing);
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
    expect_error("JMP 70000\nHALT\n", 1, "out of range");
    expect_error("\n; only comments\n", 2, "no instructions");
    expect_error("STR hello\nHALT\n", 1, "quoted string");
    expect_error("EQU X 1\nEQU X 2\nHALT\n", 2, "duplicate constant");
    expect_error("LOCAL_SET -1\nHALT\n", 1, "invalid local index");
    expect_error("ENTER 256\nHALT\n", 1, "invalid local index");
    expect_error("PUSHF abc\nHALT\n", 1, "invalid float");
    expect_error("DEI 256\nHALT\n", 1, "invalid port index");
    expect_error("CALL 70000\nHALT\n", 1, "out of range");

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
    test_expression_operators();
    test_conditionals();
    test_macros();
    test_includes();
    test_pseudo_ops();
    test_console_pseudo_ops();
    test_pseudo_opt_in();
    test_stdlib_ports();
    test_data_directives();
    test_line_mapping();
    test_errors();
    return failures;
}
