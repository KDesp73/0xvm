#include <stdbool.h>
#include <stdint.h>
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

typedef struct {
    char bytes[16];
    size_t len;
} Capture;

static VmStatus capture_read(void *ctx, VmState *vm, uint8_t port, int64_t *out)
{
    (void)ctx;
    (void)vm;
    switch (port) {
        case VM_SYS_VERSION: *out = (int64_t)VM_ROM_VERSION; return VM_OK;
        case VM_SYS_DEBUG:   *out = 0; return VM_OK;
        default:             return VM_ERR_IO;
    }
}

static VmStatus capture_write(void *ctx, VmState *vm, uint8_t port, int64_t value)
{
    Capture *cap = ctx;
    (void)vm;
    if (port != VM_CONSOLE_PUTC) return VM_ERR_IO;
    if (cap->len + 1u >= sizeof cap->bytes) return VM_ERR_IO;
    cap->bytes[cap->len++] = (char)(unsigned char)(uint64_t)value;
    return VM_OK;
}

static bool run_rom(const uint8_t *rom, size_t len, Capture *cap)
{
    VmState vm;
    vm_init(&vm);
    memset(cap, 0, sizeof *cap);
    VmDevice dev = {cap, capture_read, capture_write};
    vm_set_device(&vm, &dev);

    VmStatus st = vm_load_rom(&vm, rom, len);
    if (st == VM_OK) st = vm_run(&vm, 0);
    vm_free(&vm);
    return st == VM_HALTED;
}

static void check_ok(Capture *cap)
{
    CHECK(cap->len == 3u);
    CHECK(memcmp(cap->bytes, "OK\n", 3u) == 0);
}

int test_conformance(void)
{
    AsmProgram prog;
    CHECK(asm_parse_file(&prog, "tests/conformance.asm") == 0);

    uint8_t rom[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
    size_t rom_len = 0u;
    CHECK(asm_make_rom(&prog, rom, sizeof rom, &rom_len) == 0);

    Capture cap;
    CHECK(run_rom(rom, rom_len, &cap));
    check_ok(&cap);

    const char *path = "build/conformance-v2.rom";
    CHECK(asm_write_rom(&prog, path) == 0);
    FILE *file = fopen(path, "rb");
    CHECK(file != NULL);
    if (file) {
        uint8_t reloaded[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
        size_t len = fread(reloaded, 1u, sizeof reloaded, file);
        fclose(file);
        CHECK(run_rom(reloaded, len, &cap));
        check_ok(&cap);
    }
    remove(path);

    asm_program_free(&prog);
    return failures;
}
