#include <stdio.h>
#include <string.h>

#include "asm.h"
#include "tests.h"
#include "vm.h"

static int failures;

#define CHECK(cond) do {                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static void test_header_and_load(void)
{
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, "PUSH 42\nHALT\n") == 0);

    uint8_t rom[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
    size_t rom_len = 0;
    CHECK(asm_make_rom(&prog, rom, sizeof rom, &rom_len) == 0);
    CHECK(rom_len == VM_ROM_HEADER_SIZE + prog.len);
    CHECK(rom[0] == 0x56 && rom[1] == 0x4D && rom[2] == 0x49 && rom[3] == 0x4D);
    CHECK(VM_ROM_VERSION == 2u);
    CHECK(rom[4] == VM_ROM_VERSION && rom[5] == 0u);
    CHECK(rom[8] == (uint8_t)(prog.len >> 8) && rom[9] == (uint8_t)prog.len);
    CHECK(rom[10] == 0x01 && rom[11] == 0x00);

    VmState vm;
    vm_init(&vm);
    CHECK(vm_load_rom(&vm, rom, rom_len) == VM_OK);
    CHECK(vm.ip == VM_IMAGE_START && vm.hp == VM_HEAP_START);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(vm.sp == 1u && vm.stack[0] == 42);
    vm_free(&vm);
    asm_program_free(&prog);
}

static void test_bad_images(void)
{
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, "PUSH 1\nHALT\n") == 0);

    uint8_t rom[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
    size_t rom_len = 0;
    CHECK(asm_make_rom(&prog, rom, sizeof rom, &rom_len) == 0);

    VmState vm;
    VmState before;
    vm_init(&vm);

    rom[0] = 0;
    before = vm;
    CHECK(vm_load_rom(&vm, rom, rom_len) == VM_ERR_BAD_IP);
    CHECK(memcmp(&vm, &before, sizeof vm) == 0);
    rom[0] = 0x56;

    rom[4] = VM_ROM_VERSION + 1u;
    CHECK(vm_load_rom(&vm, rom, rom_len) == VM_ERR_BAD_IP);
    rom[4] = VM_ROM_VERSION;

    CHECK(vm_load_rom(&vm, rom, rom_len - 1u) == VM_ERR_BAD_IP);
    CHECK(vm_load_rom(&vm, rom, 8u) == VM_ERR_BAD_IP);

    rom[11] = 0xFF; /* entry beyond the payload */
    CHECK(vm_load_rom(&vm, rom, rom_len) == VM_ERR_BAD_IP);
    rom[11] = 0x00;

    uint8_t tiny[VM_ROM_HEADER_SIZE] = {0};
    CHECK(vm_load_rom(&vm, tiny, sizeof tiny) == VM_ERR_BAD_IP);

    vm_free(&vm);
    asm_program_free(&prog);
}

static void test_write_and_reload(void)
{
    const char *path = "build/conformance.rom";
    AsmProgram prog;
    CHECK(asm_parse_string(&prog, "PUSH 7\nPUSH 5\nADD\nHALT\n") == 0);
    CHECK(asm_write_rom(&prog, path) == 0);

    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    if (f) {
        uint8_t rom[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
        size_t len = fread(rom, 1u, sizeof rom, f);
        fclose(f);
        VmState vm;
        vm_init(&vm);
        CHECK(vm_load_rom(&vm, rom, len) == VM_OK);
        CHECK(vm_run(&vm, 0) == VM_HALTED);
        CHECK(vm.sp == 1u && vm.stack[0] == 12);
        vm_free(&vm);
    }
    asm_program_free(&prog);
    remove(path);
}

int test_rom(void)
{
    test_header_and_load();
    test_bad_images();
    test_write_and_reload();
    return failures;
}
