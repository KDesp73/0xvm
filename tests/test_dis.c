// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <string.h>

#include "asm.h"
#include "dis.h"
#include "tests.h"
#include "vm.h"

static int failures;
static uint8_t mem[VM_RAM_SIZE];

#define CHECK(cond) do {                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static void put_be(uint8_t *p, uint64_t v, unsigned width)
{
    for (unsigned i = 0u; i < width; i++) {
        p[width - 1u - i] = (uint8_t)(v & 0xFFu);
        v >>= 8;
    }
}

static unsigned expect_line(uint16_t addr, const char *needle)
{
    char line[VM_DIS_LINE_MAX];
    unsigned size = vm_disassemble(mem, addr, line, sizeof line);
    CHECK(size > 0u);
    if (size == 0u) return 0u;
    CHECK(strstr(line, needle) != NULL);
    return size;
}

static void test_encoding(void)
{
    memset(mem, 0, sizeof mem);
    uint16_t addr = VM_IMAGE_START;
    char line[VM_DIS_LINE_MAX];

    /* Every opcode byte decodes to its public name and encoded size. */
    for (unsigned op = 0u; op <= 0xFFu; op++) {
        mem[addr] = (uint8_t)op;
        unsigned size = vm_disassemble_range(mem, addr, (uint32_t)addr + 9u,
                                             line, sizeof line);
        CHECK(size == vm_inst_size((uint8_t)op));
        CHECK(strstr(line, vm_opcode_name((uint8_t)op)) != NULL);
    }
    memset(mem + addr, 0u, 9u);

    mem[addr] = OP_PUSH;
    put_be(mem + addr + 1u, 42u, 8u);
    CHECK(expect_line(addr, "PUSH 42") == 9u);

    mem[addr] = OP_PUSHF;
    put_be(mem + addr + 1u, (uint64_t)vm_f64_to_bits(1.5), 8u);
    CHECK(expect_line(addr, "PUSHF 1.5") == 9u);

    mem[addr] = OP_JMP;
    put_be(mem + addr + 1u, 0x0123u, 2u);
    CHECK(expect_line(addr, "JMP 0x0123") == 3u);

    mem[addr] = OP_ENTER;
    mem[addr + 1u] = 2u;
    CHECK(expect_line(addr, "ENTER 2") == 2u);

    mem[addr] = OP_DEI;
    mem[addr + 1u] = VM_CONSOLE_GETI;
    CHECK(expect_line(addr, "DEI 0x1B") == 2u);

    mem[addr] = OP_HALT;
    CHECK(expect_line(addr, "HALT") == 1u);

    mem[addr] = 0x42u;
    CHECK(expect_line(addr, "RESERVED (0x42)") == 1u);

    /* An immediate that crosses the payload end must be rejected. */
    mem[addr] = OP_PUSH;
    put_be(mem + addr + 1u, 42u, 8u);
    CHECK(vm_disassemble_range(mem, addr, (uint32_t)addr + 8u,
                               line, sizeof line) == 0u);
    CHECK(vm_disassemble_range(mem, addr, (uint32_t)addr + 9u,
                               line, sizeof line) == 9u);

    mem[VM_RAM_SIZE - 1u] = OP_PUSH;
    CHECK(vm_disassemble(mem, (uint16_t)(VM_RAM_SIZE - 1u),
                         line, sizeof line) == 0u);
}

static void test_program_disassembly(void)
{
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, "PUSH 1\nHALT\n") == 0);

    uint8_t rom[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
    size_t rom_len = 0u;
    CHECK(asm_make_rom(&prog, rom, sizeof rom, &rom_len) == 0);

    VmState vm;
    vm_init(&vm);
    CHECK(vm_load_rom(&vm, rom, rom_len) == VM_OK);

    char lines[3][VM_DIS_LINE_MAX];
    size_t count = 0u;
    uint16_t addr = vm.ip;
    uint32_t end = (uint32_t)VM_IMAGE_START + (uint32_t)prog.len;
    while (count < 3u && addr < end) {
        unsigned size = vm_disassemble_range(vm.mem, addr, end, lines[count],
                                             sizeof lines[count]);
        CHECK(size > 0u);
        if (size == 0u) break;
        count++;
        if (vm.mem[addr] == OP_HALT) break;
        addr = (uint16_t)((uint32_t)addr + size);
    }

    CHECK(count == 3u);
    if (count == 3u) {
        CHECK(strstr(lines[0], "JMP") != NULL);
        CHECK(strstr(lines[1], "PUSH 1") != NULL);
        CHECK(strstr(lines[2], "HALT") != NULL);
    }

    vm_free(&vm);
    asm_program_free(&prog);
}

int test_dis(void)
{
    test_encoding();
    test_program_disassembly();
    return failures;
}
