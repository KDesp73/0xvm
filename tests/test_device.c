// SPDX-License-Identifier: MIT
#include <stdint.h>
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

typedef struct {
    uint8_t last_port;
    int64_t last_value;
    int64_t read_value;
    VmStatus read_status;
    VmStatus write_status;
    unsigned reads, writes;
} FakeDevice;

static VmStatus fake_read(void *ctx, VmState *vm, uint8_t port, int64_t *out)
{
    FakeDevice *fake = ctx;
    (void)vm;
    fake->last_port = port;
    fake->reads++;
    if (fake->read_status != VM_OK) return fake->read_status;
    *out = fake->read_value;
    return VM_OK;
}

static VmStatus fake_write(void *ctx, VmState *vm, uint8_t port, int64_t value)
{
    FakeDevice *fake = ctx;
    (void)vm;
    fake->last_port = port;
    fake->last_value = value;
    fake->writes++;
    return fake->write_status;
}

static void install(VmState *vm, FakeDevice *fake)
{
    VmDevice dev = {fake, fake_read, fake_write};
    vm_init(vm);
    vm_set_device(vm, &dev);
}

static void load(VmState *vm, const uint8_t *bytes, size_t len)
{
    CHECK(vm_load_image(vm, bytes, len, VM_IMAGE_START) == VM_OK);
}

static void test_deo(void)
{
    VmState vm;
    FakeDevice fake = {0};
    install(&vm, &fake);

    const uint8_t p[] = {
        OP_PUSH, 0, 0, 0, 0, 0, 0, 0, 42,
        OP_DEO, VM_CONSOLE_PUTC,
        OP_HALT
    };
    load(&vm, p, sizeof p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(fake.writes == 1u);
    CHECK(fake.last_port == VM_CONSOLE_PUTC && fake.last_value == 42);
    CHECK(vm.sp == 0u && vm.hp == VM_HEAP_START);
    vm_free(&vm);
}

static void test_dei(void)
{
    VmState vm;
    FakeDevice fake = {0};
    fake.read_value = 7;
    install(&vm, &fake);

    const uint8_t p[] = {OP_DEI, VM_CONSOLE_GETI, OP_HALT};
    load(&vm, p, sizeof p);
    CHECK(vm_run(&vm, 0) == VM_HALTED);
    CHECK(fake.reads == 1u && fake.last_port == VM_CONSOLE_GETI);
    CHECK(vm.sp == 1u && vm.stack[0] == 7);
    vm_free(&vm);
}

static void test_device_error_is_atomic(void)
{
    VmState vm;
    FakeDevice fake = {0};
    install(&vm, &fake);

    const uint8_t p[] = {
        OP_PUSH, 0, 0, 0, 0, 0, 0, 0, 1,
        OP_DEO, VM_CONSOLE_PUTC,
        OP_HALT
    };
    load(&vm, p, sizeof p);
    CHECK(vm_step(&vm) == VM_OK); /* PUSH */

    VmState before = vm;
    fake.write_status = VM_ERR_IO;
    CHECK(vm_step(&vm) == VM_ERR_IO);
    CHECK(memcmp(&vm, &before, sizeof vm) == 0);
    CHECK(fake.writes == 1u);
    vm_free(&vm);
}

static void test_device_halt(void)
{
    VmState vm;
    FakeDevice fake = {0};
    fake.write_status = VM_HALTED;
    install(&vm, &fake);

    const uint8_t p[] = {
        OP_PUSH, 0, 0, 0, 0, 0, 0, 0, 9,
        OP_DEO, VM_SYS_HALT,
        OP_HALT
    };
    load(&vm, p, sizeof p);
    CHECK(vm_step(&vm) == VM_OK); /* PUSH */
    uint16_t deo_addr = vm.ip;
    CHECK(vm_step(&vm) == VM_HALTED);
    CHECK(vm.ip == deo_addr);
    CHECK(vm.sp == 0u && vm.ticks == 2u);
    CHECK(fake.last_port == VM_SYS_HALT && fake.last_value == 9);
    vm_free(&vm);
}

static void test_retired_io_opcodes(void)
{
    VmState vm;
    FakeDevice fake = {0};
    install(&vm, &fake);

    const uint8_t retired[] = {0x0Eu, 0x41u, 0x42u, 0x43u, 0x44u, 0x45u};
    for (size_t i = 0u; i < sizeof retired; i++) {
        uint8_t p[2] = {retired[i], OP_HALT};
        load(&vm, p, sizeof p);
        VmState before = vm;
        CHECK(vm_step(&vm) == VM_ERR_BAD_OP);
        CHECK(memcmp(&vm, &before, sizeof vm) == 0);
    }
    CHECK(strcmp(vm_opcode_name(OP_DEI), "DEI") == 0);
    CHECK(strcmp(vm_opcode_name(OP_DEO), "DEO") == 0);
    CHECK(strcmp(vm_opcode_name(0x42u), "RESERVED") == 0);
    vm_free(&vm);
}

int test_device(void)
{
    test_deo();
    test_dei();
    test_device_error_is_atomic();
    test_device_halt();
    test_retired_io_opcodes();
    return failures;
}
