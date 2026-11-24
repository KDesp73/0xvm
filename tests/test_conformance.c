#include <inttypes.h>
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

#define PUSH_IMM(v) OP_PUSH,                                                    \
    (uint8_t)((uint64_t)(v) >> 56), (uint8_t)((uint64_t)(v) >> 48),             \
    (uint8_t)((uint64_t)(v) >> 40), (uint8_t)((uint64_t)(v) >> 32),             \
    (uint8_t)((uint64_t)(v) >> 24), (uint8_t)((uint64_t)(v) >> 16),             \
    (uint8_t)((uint64_t)(v) >> 8),  (uint8_t)(uint64_t)(v)

#define OUTPUT_CAP 512u
#define GOLDEN "2\nA\n-42\n3.75\nOK\n66\n-7\n3\nhey\n"

/* Deterministic host for the executable conformance ROM. */
typedef struct {
    char bytes[OUTPUT_CAP];
    size_t len;
    bool debug;
    bool fail_writes;
    uint16_t puts_addr, puts_len;
    uint16_t gets_addr, gets_max, gets_len;
    size_t getc_pos, geti_pos;
} Host;

static const int64_t getc_script[] = {66}; /* 'B', then EOF */
static const int64_t geti_script[] = {-7};
static const char gets_script[] = "hey";

static void host_init(Host *h)
{
    memset(h, 0, sizeof *h);
}

static bool append_bytes(Host *h, const char *s, size_t n)
{
    if (h->len + n >= sizeof h->bytes) return false;
    memcpy(h->bytes + h->len, s, n);
    h->len += n;
    h->bytes[h->len] = '\0';
    return true;
}

static bool append_char(Host *h, char ch)
{
    return append_bytes(h, &ch, 1u);
}

static bool append_prefix(Host *h, const VmState *vm)
{
    char buf[64];
    int n;

    if (!h->debug) return true;
    n = snprintf(buf, sizeof buf, "[vm tick=%" PRIu64 "] ", vm->ticks);
    return n > 0 && (size_t)n < sizeof buf &&
           append_bytes(h, buf, (size_t)n);
}

static bool append_i64(Host *h, int64_t value)
{
    char buf[32];
    int n = snprintf(buf, sizeof buf, "%" PRId64 "\n", value);

    return n > 0 && (size_t)n < sizeof buf && append_bytes(h, buf, (size_t)n);
}

static bool append_f64(Host *h, int64_t bits)
{
    char buf[64];
    int n = snprintf(buf, sizeof buf, "%.17g\n", vm_bits_to_f64(bits));

    return n > 0 && (size_t)n < sizeof buf && append_bytes(h, buf, (size_t)n);
}

static VmStatus host_read(void *ctx, VmState *vm, uint8_t port, int64_t *out)
{
    Host *h = ctx;

    (void)vm;
    switch (port) {
        case VM_SYS_VERSION:
            *out = (int64_t)VM_ROM_VERSION;
            return VM_OK;
        case VM_SYS_DEBUG:
            *out = h->debug ? 1 : 0;
            return VM_OK;
        case VM_CONSOLE_GETC:
            *out = h->getc_pos < sizeof getc_script / sizeof getc_script[0]
                 ? getc_script[h->getc_pos++] : 0;
            return VM_OK;
        case VM_CONSOLE_GETI:
            *out = h->geti_pos < sizeof geti_script / sizeof geti_script[0]
                 ? geti_script[h->geti_pos++] : 0;
            return VM_OK;
        case VM_CONSOLE_GETS_LEN:
            *out = (int64_t)h->gets_len;
            return VM_OK;
        default:
            return VM_ERR_IO;
    }
}

static VmStatus host_write(void *ctx, VmState *vm, uint8_t port, int64_t value)
{
    Host *h = ctx;

    if (h->fail_writes) return VM_ERR_IO;

    switch (port) {
        case VM_SYS_DEBUG:
            h->debug = value != 0;
            return VM_OK;
        case VM_SYS_HALT:
            return VM_HALTED;
        case VM_CONSOLE_PUTC:
            return append_char(h, (char)(unsigned char)(uint64_t)value)
                 ? VM_OK : VM_ERR_IO;
        case VM_CONSOLE_PUTI:
            return append_prefix(h, vm) && append_i64(h, value)
                 ? VM_OK : VM_ERR_IO;
        case VM_CONSOLE_PUTF:
            return append_prefix(h, vm) && append_f64(h, value)
                 ? VM_OK : VM_ERR_IO;
        case VM_CONSOLE_PUTS_ADDR:
            h->puts_addr = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_PUTS_LEN:
            h->puts_len = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_PUTS:
            if ((uint32_t)h->puts_addr + (uint32_t)h->puts_len > VM_RAM_SIZE)
                return VM_ERR_IO;
            if (!append_prefix(h, vm)) return VM_ERR_IO;
            if (h->puts_len > 0u &&
                !append_bytes(h, (const char *)(vm->mem + h->puts_addr),
                              h->puts_len))
                return VM_ERR_IO;
            return append_char(h, '\n') ? VM_OK : VM_ERR_IO;
        case VM_CONSOLE_GETS_ADDR:
            h->gets_addr = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_GETS_MAX:
            h->gets_max = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_GETS: {
            size_t n = strlen(gets_script);

            if (h->gets_max == 0u ||
                (uint32_t)h->gets_addr + (uint32_t)h->gets_max > VM_RAM_SIZE)
                return VM_ERR_IO;
            if (n > h->gets_max) n = h->gets_max;
            memcpy(vm->mem + h->gets_addr, gets_script, n);
            h->gets_len = (uint16_t)n;
            return VM_OK;
        }
        default:
            return VM_ERR_IO;
    }
}

static void install(VmState *vm, Host *h)
{
    VmDevice dev = {h, host_read, host_write};

    vm_init(vm);
    vm_set_device(vm, &dev);
}

static VmStatus run_rom(const uint8_t *rom, size_t len, Host *h,
                        VmState *final)
{
    VmState vm;
    VmStatus st;

    install(&vm, h);
    st = vm_load_rom(&vm, rom, len);
    if (st == VM_OK) st = vm_run(&vm, 0);
    if (final) *final = vm;
    vm_free(&vm);
    return st;
}

static void check_transcript(const Host *h)
{
    const size_t want = sizeof GOLDEN - 1u;

    CHECK(h->len == want && memcmp(h->bytes, GOLDEN, want) == 0);
    if (h->len != want || memcmp(h->bytes, GOLDEN, want) != 0)
        fprintf(stderr, "conformance transcript: got [%.*s]\n",
                (int)h->len, h->bytes);
}

/* Step until the first non-OK status and require it to be atomic. */
static VmStatus step_until_error(VmState *vm, size_t max_steps)
{
    for (size_t i = 0u; i < max_steps; i++) {
        VmState before = *vm;
        VmStatus st = vm_step(vm);

        if (st != VM_OK) {
            CHECK(memcmp(vm, &before, sizeof *vm) == 0);
            return st;
        }
    }
    CHECK(0 /* expected an error inside max_steps */);
    return VM_OK;
}

static void expect_error_program(const uint8_t *payload, size_t len,
                                 VmStatus want, bool fail_writes)
{
    Host host;
    VmState vm;

    host_init(&host);
    host.fail_writes = fail_writes;
    install(&vm, &host);
    CHECK(vm_load_image(&vm, payload, len, VM_IMAGE_START) == VM_OK);
    CHECK(step_until_error(&vm, 2048u) == want);
    vm_free(&vm);
}

static void test_invalid_programs(void)
{
    const uint8_t reserved[] = {0x42u, OP_HALT};
    const uint8_t retired[] = {0x0Eu, OP_HALT};
    const uint8_t underflow[] = {OP_POP, OP_HALT};
    const uint8_t div_zero[] = {PUSH_IMM(10), PUSH_IMM(0), OP_DIV, OP_HALT};
    const uint8_t bad_addr[] = {
        PUSH_IMM(VM_RAM_SIZE - 1u), OP_LOAD64, OP_HALT
    };
    const uint8_t bad_target[] = {PUSH_IMM(-1), OP_JMP_I, OP_HALT};
    const uint8_t bad_frame[] = {OP_RET, OP_HALT};
    const uint8_t bad_local[] = {
        OP_ENTER, 1, OP_LOCAL_GET, 1, OP_HALT
    };
    const uint8_t heap_full[] = {PUSH_IMM(65535), OP_ALLOC, OP_HALT};
    const uint8_t device_error[] = {
        PUSH_IMM(1), OP_DEO, VM_CONSOLE_PUTC, OP_HALT
    };
    uint8_t overflow[257u * 9u + 1u];
    size_t n = 0u;

    expect_error_program(reserved, sizeof reserved, VM_ERR_BAD_OP, false);
    expect_error_program(retired, sizeof retired, VM_ERR_BAD_OP, false);
    expect_error_program(underflow, sizeof underflow,
                         VM_ERR_STACK_UNDERFLOW, false);
    expect_error_program(div_zero, sizeof div_zero, VM_ERR_DIV_ZERO, false);
    expect_error_program(bad_addr, sizeof bad_addr, VM_ERR_BAD_ADDR, false);
    expect_error_program(bad_target, sizeof bad_target, VM_ERR_BAD_IP, false);
    expect_error_program(bad_frame, sizeof bad_frame, VM_ERR_BAD_FRAME, false);
    expect_error_program(bad_local, sizeof bad_local, VM_ERR_BAD_LOCAL, false);
    expect_error_program(heap_full, sizeof heap_full,
                         VM_ERR_HEAP_OVERFLOW, false);
    expect_error_program(device_error, sizeof device_error,
                         VM_ERR_IO, true);

    for (size_t i = 0u; i < 257u; i++) {
        overflow[n++] = OP_PUSH;
        for (unsigned j = 0u; j < 7u; j++) overflow[n++] = 0u;
        overflow[n++] = 1u;
    }
    overflow[n++] = OP_HALT;
    expect_error_program(overflow, n, VM_ERR_STACK_OVERFLOW, false);
}

static void test_truncated_immediate(void)
{
    Host host;
    VmState vm;
    const uint8_t payload[] = {OP_HALT};

    host_init(&host);
    install(&vm, &host);
    CHECK(vm_load_image(&vm, payload, sizeof payload, VM_IMAGE_START) == VM_OK);
    vm.ip = VM_RAM_SIZE - 1u;
    vm.mem[vm.ip] = OP_PUSH;
    {
        VmState before = vm;
        CHECK(vm_step(&vm) == VM_ERR_BAD_IP);
        CHECK(memcmp(&vm, &before, sizeof vm) == 0);
    }
    vm_free(&vm);
}

static void test_device_halt(void)
{
    Host host;
    VmState vm;
    const uint8_t payload[] = {
        PUSH_IMM(9), OP_DEO, VM_SYS_HALT, OP_HALT
    };

    host_init(&host);
    install(&vm, &host);
    CHECK(vm_load_image(&vm, payload, sizeof payload, VM_IMAGE_START) == VM_OK);
    CHECK(vm_step(&vm) == VM_OK);
    {
        uint16_t halt_ip = vm.ip;
        CHECK(vm_step(&vm) == VM_HALTED);
        CHECK(vm.ip == halt_ip && vm.sp == 0u && vm.ticks == 2u);
    }
    vm_free(&vm);
}

static void check_conformance_rom(void)
{
    AsmProgram prog;
    uint8_t rom[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
    size_t rom_len = 0u;
    Host host;
    VmState final;
    const char *path = "build/conformance-v2.rom";
    FILE *file;

    CHECK(asm_parse_file(&prog, "tests/conformance.asm") == 0);
    CHECK(asm_make_rom(&prog, rom, sizeof rom, &rom_len) == 0);

    host_init(&host);
    CHECK(run_rom(rom, rom_len, &host, &final) == VM_HALTED);
    check_transcript(&host);
    CHECK(final.sp == 0u && final.ticks > 0u);

    CHECK(asm_write_rom(&prog, path) == 0);
    file = fopen(path, "rb");
    CHECK(file != NULL);
    if (file) {
        uint8_t reloaded[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
        size_t len = fread(reloaded, 1u, sizeof reloaded, file);
        fclose(file);

        host_init(&host);
        CHECK(run_rom(reloaded, len, &host, &final) == VM_HALTED);
        check_transcript(&host);
        CHECK(final.sp == 0u && final.ticks > 0u);
    }
    asm_program_free(&prog);
}

int test_conformance(void)
{
    check_conformance_rom();
    test_invalid_programs();
    test_truncated_immediate();
    test_device_halt();
    return failures;
}
