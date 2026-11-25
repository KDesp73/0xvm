#include <stdio.h>
#include <string.h>

#include "host.h"
#include "tests.h"

static int failures;

#define CHECK(cond) do {                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                         \
        }                                                                       \
    } while (0)

typedef struct {
    int reads, writes;
    int64_t last_value;
    uint8_t last_port;
} Spy;

static VmStatus spy_read(void *ctx, VmState *vm, uint8_t port, int64_t *out)
{
    Spy *spy = ctx;
    (void)vm;
    spy->reads++;
    spy->last_port = port;
    *out = 0x100 + port;
    return VM_OK;
}

static VmStatus spy_write(void *ctx, VmState *vm, uint8_t port, int64_t value)
{
    Spy *spy = ctx;
    (void)vm;
    spy->writes++;
    spy->last_port = port;
    spy->last_value = value;
    return port == 0x41 ? VM_HALTED : VM_OK;
}

static void test_dispatch(void)
{
    VmHost host;
    Spy a = {0}, b = {0};
    int64_t out = 0;
    VmState vm;
    vm_init(&vm);

    host_init(&host);
    CHECK(host_add(&host, 0x10, 0x1F, &a, spy_read, spy_write) == 0);
    CHECK(host_add(&host, 0x40, 0x41, &b, spy_read, spy_write) == 0);
    VmDevice dev = host_device(&host);

    CHECK(dev.read(dev.ctx, &vm, 0x12, &out) == VM_OK);
    CHECK(out == 0x112 && a.reads == 1 && b.reads == 0);
    CHECK(dev.read(dev.ctx, &vm, 0x40, &out) == VM_OK);
    CHECK(out == 0x140 && b.reads == 1);

    CHECK(dev.write(dev.ctx, &vm, 0x1E, 7) == VM_OK);
    CHECK(a.writes == 1 && a.last_port == 0x1E && a.last_value == 7);
    CHECK(dev.write(dev.ctx, &vm, 0x41, 0) == VM_HALTED);
    CHECK(b.writes == 1);

    CHECK(dev.read(dev.ctx, &vm, 0x20, &out) == VM_ERR_IO);
    CHECK(dev.write(dev.ctx, &vm, 0x20, 0) == VM_ERR_IO);

    /* The table is bounded. */
    for (unsigned i = 0u; i < HOST_MAX_DEVICES - 2u; i++)
        CHECK(host_add(&host, 0x50, 0x50, &a, spy_read, spy_write) == 0);
    CHECK(host_add(&host, 0x51, 0x51, &a, spy_read, spy_write) == -1);

    vm_free(&vm);
}

static void test_stdio_ranges(void)
{
    VmHost host;
    HostStdio stdio;
    VmState vm;
    int64_t out = 0;

    vm_init(&vm);
    host_init(&host);
    host_stdio_init(&stdio, false);
    CHECK(host_stdio_add(&host, &stdio) == 0);
    VmDevice dev = host_device(&host);

    CHECK(dev.read(dev.ctx, &vm, VM_SYS_VERSION, &out) == VM_OK);
    CHECK(out == VM_ROM_VERSION);
    CHECK(dev.read(dev.ctx, &vm, VM_SYS_DEBUG, &out) == VM_OK && out == 0);
    CHECK(dev.write(dev.ctx, &vm, VM_SYS_DEBUG, 1) == VM_OK);
    CHECK(dev.read(dev.ctx, &vm, VM_SYS_DEBUG, &out) == VM_OK && out == 1);
    CHECK(dev.write(dev.ctx, &vm, VM_SYS_HALT, 0) == VM_HALTED);
    CHECK(dev.read(dev.ctx, &vm, 0x03, &out) == VM_ERR_IO);

    vm_free(&vm);
}

int test_host(void)
{
    test_dispatch();
    test_stdio_ranges();
    return failures;
}
