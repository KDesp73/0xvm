#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "net.h"
#include "tests.h"

static int failures;

#define CHECK(cond) do {                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static void write_be32(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)v;
}

static int raw_send(NetConn *conn, const void *buf, size_t len)
{
    const uint8_t *p = buf;
    while (len > 0u) {
        ssize_t n = send(conn->fd, p, len, 0);
        if (n <= 0) return -1;
        p += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

/* Create a connected pair through a real loopback TCP listener. */
static int connect_pair(NetListener *listener, NetConn *sender, NetConn *receiver)
{
    if (net_listen("127.0.0.1", 0, listener) != 0) {
        fprintf(stderr, "  net_listen: %s\n", net_last_error());
        return -1;
    }
    uint16_t port = net_listener_port(listener);
    CHECK(port != 0u);
    if (port == 0u) return -1;

    if (net_connect("127.0.0.1", port, sender) != 0) {
        fprintf(stderr, "  net_connect: %s\n", net_last_error());
        return -1;
    }
    if (net_accept(listener, receiver) != 0) {
        fprintf(stderr, "  net_accept: %s\n", net_last_error());
        return -1;
    }
    return 0;
}

static void close_pair(NetListener *listener, NetConn *sender, NetConn *receiver)
{
    net_close(receiver);
    net_close(sender);
    net_close_listener(listener);
}

/* Build a state with a live heap by executing a real ALLOC. */
static void make_state(VmState *vm, int64_t size, const VmInst *prog, size_t n)
{
    vm_init(vm);
    CHECK(vm_run(vm, prog, n, 0) == VM_HALTED);
    CHECK(vm->hp >= (uint64_t)size);
    for (uint64_t i = 0; i < vm->hp; i++) {
        vm->heap[i] = (uint8_t)(i * 31u + 7u);
    }
    vm->ip = 12u;
    vm->sp = 3u;
    vm->ticks = 1234u;
    vm->stack[0] = INT64_MIN;
    vm->stack[1] = 0;
    vm->stack[2] = INT64_MAX;
}

static int same_state(const VmState *a, const VmState *b)
{
    if (a->ip != b->ip || a->sp != b->sp || a->fp != b->fp ||
        a->hp != b->hp || a->ticks != b->ticks ||
        a->fsp != b->fsp || a->block_count != b->block_count) return 0;
    if (a->sp > 0u &&
        memcmp(a->stack, b->stack, (size_t)a->sp * sizeof(int64_t)) != 0) return 0;
    if (a->fsp > 0u &&
        memcmp(a->frames, b->frames, (size_t)a->fsp * sizeof(VmFrame)) != 0) return 0;
    if (a->block_count > 0u &&
        memcmp(a->blocks, b->blocks, (size_t)a->block_count * sizeof(VmBlock)) != 0) return 0;
    if (a->hp == 0u) return 1;
    if (!a->heap || !b->heap) return 0;
    return memcmp(a->heap, b->heap, (size_t)a->hp) == 0;
}

static void test_roundtrip(int64_t bytes)
{
    NetListener listener;
    NetConn sender, receiver;
    if (connect_pair(&listener, &sender, &receiver) != 0) {
        failures++;
        return;
    }

    VmInst prog[] = {{OP_PUSH, bytes}, {OP_ALLOC, 0}, {OP_POP, 0}, {OP_HALT, 0}};
    VmState a, b;
    make_state(&a, bytes, prog, sizeof prog / sizeof prog[0]);
    vm_init(&b);

    /* A large payload fills the socket buffers, so receive in a child while
     * the parent sends; otherwise both ends would wait for each other. */
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid < 0) {
        vm_free(&a);
        vm_free(&b);
        close_pair(&listener, &sender, &receiver);
        return;
    }

    if (pid == 0) {
        net_close(&sender);
        net_close_listener(&listener);
        int rc = net_recv_state(&receiver, &b);
        if (rc != 0) fprintf(stderr, "  net_recv_state: %s\n", net_last_error());
        int ok = (rc == 0) && same_state(&a, &b);

        /* The restored state must remain executable at its saved ip (12). */
        if (ok) {
            VmInst resume[13];
            for (size_t i = 0; i < sizeof resume / sizeof resume[0]; i++) {
                resume[i] = (VmInst){OP_NOP, 0};
            }
            ok = (vm_step(&b, resume, sizeof resume / sizeof resume[0]) == VM_OK &&
                  b.ticks == 1235u);
        }

        vm_free(&a);
        vm_free(&b);
        net_close(&receiver);
        _exit(ok ? 0 : 1);
    }

    net_close(&receiver);
    net_close_listener(&listener);
    int send_rc = net_send_state(&sender, &a);
    if (send_rc != 0) fprintf(stderr, "  net_send_state(%" PRId64 " bytes): %s\n",
                              bytes, net_last_error());
    CHECK(send_rc == 0);

    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status));
    if (WIFEXITED(status)) CHECK(WEXITSTATUS(status) == 0);

    vm_free(&a);
    net_close(&sender);
}

static void test_diff_roundtrip(void)
{
    NetListener listener;
    NetConn sender, receiver;
    if (connect_pair(&listener, &sender, &receiver) != 0) {
        failures++;
        return;
    }

    VmInst prog[] = {{OP_PUSH, 8192}, {OP_ALLOC, 0}, {OP_POP, 0}, {OP_HALT, 0}};
    VmState a, base, b_base, b;
    make_state(&a, 8192, prog, sizeof prog / sizeof prog[0]);
    vm_init(&base);
    vm_init(&b_base);
    CHECK(vm_clone(&a, &base) == 0);
    CHECK(vm_clone(&base, &b_base) == 0);
    a.heap[5000] = 0x5Au; /* one dirty page */
    vm_init(&b);

    size_t full = vm_serialize(&a, NULL, 0);
    size_t n = vm_serialize_diff(&a, &base, NULL, 0);
    CHECK(n > 0u && n < full);
    uint8_t *wire = malloc(n);
    CHECK(wire != NULL);
    if (wire) {
        CHECK(vm_serialize_diff(&a, &base, wire, n) == n);
        uint8_t prefix[4];
        write_be32(prefix, (uint32_t)n);
        CHECK(raw_send(&sender, prefix, sizeof prefix) == 0);
        CHECK(raw_send(&sender, wire, n) == 0);
        CHECK(net_recv_state_diff(&receiver, &b, &b_base) == 0);
        CHECK(same_state(&a, &b));
        free(wire);
    }

    vm_free(&a);
    vm_free(&base);
    vm_free(&b_base);
    vm_free(&b);
    close_pair(&listener, &sender, &receiver);
}

/* A socket timeout is a wake-up, not a failed transfer. The receiver must
 * keep waiting if the sender is slow (e.g. blocked reading stdin). */
static void test_timeout_retry(void)
{
    NetListener listener;
    NetConn sender, receiver;
    if (connect_pair(&listener, &sender, &receiver) != 0) {
        failures++;
        return;
    }
    CHECK(net_set_timeout(&receiver, 1) == 0);

    VmState a, b;
    vm_init(&a);
    vm_init(&b);

    size_t n = vm_serialize(&a, NULL, 0);
    uint8_t *wire = malloc(n);
    CHECK(wire != NULL);
    if (!wire) {
        vm_free(&a);
        vm_free(&b);
        close_pair(&listener, &sender, &receiver);
        return;
    }
    CHECK(vm_serialize(&a, wire, n) == n);

    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid < 0) {
        free(wire);
        vm_free(&a);
        vm_free(&b);
        close_pair(&listener, &sender, &receiver);
        return;
    }

    if (pid == 0) {
        net_close(&sender);
        net_close_listener(&listener);
        int rc = net_recv_state(&receiver, &b);
        if (rc != 0) fprintf(stderr, "  delayed net_recv_state: %s\n", net_last_error());
        int ok = (rc == 0) && same_state(&a, &b);
        vm_free(&a);
        vm_free(&b);
        net_close(&receiver);
        _exit(ok ? 0 : 1);
    }

    net_close(&receiver);
    net_close_listener(&listener);

    sleep(2); /* longer than the receiver's 1-second timeout */

    uint8_t prefix[4];
    write_be32(prefix, (uint32_t)n);
    CHECK(raw_send(&sender, prefix, sizeof prefix) == 0);
    CHECK(raw_send(&sender, wire, n) == 0);

    int status = 0;
    CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status));
    if (WIFEXITED(status)) CHECK(WEXITSTATUS(status) == 0);

    free(wire);
    vm_free(&a);
    net_close(&sender);
}

static void test_bad_frames(void)
{
    NetListener listener;
    NetConn sender, receiver;
    if (connect_pair(&listener, &sender, &receiver) != 0) {
        failures++;
        return;
    }

    VmState vm;
    vm_init(&vm);

    uint8_t prefix[4];
    write_be32(prefix, 0u);
    CHECK(raw_send(&sender, prefix, sizeof prefix) == 0);
    CHECK(net_recv_state(&receiver, &vm) == NET_RECV_HALTED);

    /* net_send_halt() emits the same zero-length frame. */
    CHECK(net_send_halt(&sender) == 0);
    CHECK(net_recv_state_diff(&receiver, &vm, &vm) == NET_RECV_HALTED);

    write_be32(prefix, NET_MAX_STATE_BYTES + 1u);
    CHECK(raw_send(&sender, prefix, sizeof prefix) == 0);
    CHECK(net_recv_state(&receiver, &vm) == -1);

    /* Announce a payload, send only part of it, then close the connection. */
    write_be32(prefix, 64u);
    uint8_t partial[10] = {0};
    CHECK(raw_send(&sender, prefix, sizeof prefix) == 0);
    CHECK(raw_send(&sender, partial, sizeof partial) == 0);
    net_close(&sender);
    CHECK(net_recv_state(&receiver, &vm) == -1);
    CHECK(vm.ip == 0u && vm.sp == 0u && vm.hp == 0u); /* unchanged on failure */

    vm_free(&vm);
    net_close(&receiver);
    net_close_listener(&listener);
}

static void test_invalid_payload(void)
{
    NetListener listener;
    NetConn sender, receiver;
    if (connect_pair(&listener, &sender, &receiver) != 0) {
        failures++;
        return;
    }

    VmInst prog[] = {{OP_PUSH, 8}, {OP_ALLOC, 0}, {OP_HALT, 0}};
    VmState a, b;
    make_state(&a, 8, prog, sizeof prog / sizeof prog[0]);
    vm_init(&b);

    size_t n = vm_serialize(&a, NULL, 0);
    CHECK(n > 0u);
    uint8_t *wire = malloc(n);
    CHECK(wire != NULL);
    if (wire) {
        CHECK(vm_serialize(&a, wire, n) == n);
        wire[4] = (uint8_t)(VM_VERSION + 1u); /* wrong version */
        uint8_t prefix[4];
        write_be32(prefix, (uint32_t)n);
        CHECK(raw_send(&sender, prefix, sizeof prefix) == 0);
        CHECK(raw_send(&sender, wire, n) == 0);
        CHECK(net_recv_state(&receiver, &b) == -1);
        free(wire);
    }

    vm_free(&a);
    vm_free(&b);
    close_pair(&listener, &sender, &receiver);
}

static void test_errors_without_connection(void)
{
    NetConn conn = {-1};
    NetListener listener = {-1};
    VmState vm;
    vm_init(&vm);

    CHECK(net_listen("bad.invalid.host.example", 1u, &listener) == -1);
    CHECK(net_connect("bad.invalid.host.example", 1u, &conn) == -1);
    CHECK(net_send_state(&conn, &vm) == -1);
    CHECK(net_recv_state(&conn, &vm) == -1);
    CHECK(net_set_timeout(&conn, 1) == -1);
    CHECK(strlen(net_last_error()) > 0u);

    net_close(&conn);
    net_close_listener(&listener);
    vm_free(&vm);
}

int test_net(void)
{
    test_roundtrip(4);
    test_roundtrip(1024 * 1024); /* larger than a socket buffer: exercises send/recv loops */
    test_diff_roundtrip();
    test_timeout_retry();
    test_bad_frames();
    test_invalid_payload();
    test_errors_without_connection();
    return failures;
}
