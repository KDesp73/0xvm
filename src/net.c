#include "net.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef MSG_NOSIGNAL
#define NET_SEND_FLAGS MSG_NOSIGNAL
#else
#define NET_SEND_FLAGS 0
#endif

static char net_error[256] = {0};

static void net_set_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(net_error, sizeof net_error, fmt, ap);
    va_end(ap);
}

/* SO_RCVTIMEO/SO_SNDTIMEO surface as EAGAIN/EWOULDBLOCK on macOS and Linux.
 * They are wake-ups, not fatal errors: a peer may be reading stdin or simply
 * not scheduled yet, so keep waiting rather than tearing down the transfer. */
static int retryable_net_error(int err)
{
    return err == EINTR || err == EAGAIN
#ifdef EWOULDBLOCK
           || err == EWOULDBLOCK
#endif
           ;
}

const char *net_last_error(void)
{
    return net_error[0] ? net_error : "no network error";
}

static void configure_socket(int fd)
{
#ifdef SO_NOSIGPIPE
    int set = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof set);
#endif

    struct timeval tv;
    tv.tv_sec = NET_DEFAULT_TIMEOUT_SEC;
    tv.tv_usec = 0;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
}

static int resolve(const char *host, uint16_t port, int passive,
                   struct addrinfo **out)
{
    char portstr[16];
    snprintf(portstr, sizeof portstr, "%u", (unsigned)port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (passive) hints.ai_flags = AI_PASSIVE;

    const char *node = (host && *host) ? host : "127.0.0.1";
    int rc = getaddrinfo(node, portstr, &hints, out);
    if (rc != 0) {
        net_set_error("getaddrinfo('%s', '%s'): %s", node, portstr,
                      gai_strerror(rc));
        return -1;
    }
    return 0;
}

static int send_all(int fd, const uint8_t *buf, size_t len)
{
    while (len > 0u) {
        ssize_t n = send(fd, buf, len, NET_SEND_FLAGS);
        if (n < 0) {
            if (retryable_net_error(errno)) continue;
            net_set_error("send: %s", strerror(errno));
            return -1;
        }
        if (n == 0) {
            net_set_error("send: connection closed");
            return -1;
        }
        buf += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static int recv_all(int fd, uint8_t *buf, size_t len)
{
    while (len > 0u) {
        ssize_t n = recv(fd, buf, len, 0);
        if (n < 0) {
            if (retryable_net_error(errno)) continue;
            net_set_error("recv: %s", strerror(errno));
            return -1;
        }
        if (n == 0) {
            net_set_error("recv: peer closed connection early");
            return -1;
        }
        buf += (size_t)n;
        len -= (size_t)n;
    }
    return 0;
}

static void write_u32_be(uint8_t *dst, uint32_t v)
{
    dst[0] = (uint8_t)(v >> 24);
    dst[1] = (uint8_t)(v >> 16);
    dst[2] = (uint8_t)(v >> 8);
    dst[3] = (uint8_t)v;
}

static uint32_t read_u32_be(const uint8_t *src)
{
    return ((uint32_t)src[0] << 24) | ((uint32_t)src[1] << 16) |
           ((uint32_t)src[2] << 8) | (uint32_t)src[3];
}

int net_listen(const char *host, uint16_t port, NetListener *out)
{
    if (!out) return -1;
    out->fd = -1;

    struct addrinfo *res = NULL;
    if (resolve(host, port, 1, &res) != 0) return -1;

    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        int reuse = 1;
        (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse);
        configure_socket(fd);

        if (bind(fd, rp->ai_addr, rp->ai_addrlen) == 0 &&
            listen(fd, 8) == 0) {
            out->fd = fd;
            freeaddrinfo(res);
            return 0;
        }

        close(fd);
        fd = -1;
    }

    net_set_error("cannot listen on %s:%u: %s",
                  (host && *host) ? host : "127.0.0.1", (unsigned)port,
                  strerror(errno));
    freeaddrinfo(res);
    return -1;
}

uint16_t net_listener_port(const NetListener *listener)
{
    if (!listener || listener->fd < 0) return 0;

    struct sockaddr_storage addr;
    socklen_t len = sizeof addr;
    if (getsockname(listener->fd, (struct sockaddr *)&addr, &len) != 0) {
        net_set_error("getsockname: %s", strerror(errno));
        return 0;
    }

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)&addr;
        return ntohs(in->sin_port);
    }
    if (addr.ss_family == AF_INET6) {
        struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&addr;
        return ntohs(in6->sin6_port);
    }
    net_set_error("getsockname: unexpected address family");
    return 0;
}

int net_accept(NetListener *listener, NetConn *out)
{
    if (!listener || listener->fd < 0 || !out) return -1;
    out->fd = -1;

    for (;;) {
        int fd = accept(listener->fd, NULL, NULL);
        if (fd < 0) {
            if (retryable_net_error(errno)) continue;
            net_set_error("accept: %s", strerror(errno));
            return -1;
        }
        configure_socket(fd);
        out->fd = fd;
        return 0;
    }
}

int net_connect(const char *host, uint16_t port, NetConn *out)
{
    if (!out) return -1;
    out->fd = -1;

    struct addrinfo *res = NULL;
    if (resolve(host, port, 0, &res) != 0) return -1;

    int fd = -1;
    for (struct addrinfo *rp = res; rp; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;

        configure_socket(fd);
        if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            out->fd = fd;
            freeaddrinfo(res);
            return 0;
        }

        close(fd);
        fd = -1;
    }

    net_set_error("cannot connect to %s:%u: %s",
                  (host && *host) ? host : "127.0.0.1", (unsigned)port,
                  strerror(errno));
    freeaddrinfo(res);
    return -1;
}

static int send_state_impl(NetConn *conn, const VmState *state,
                           const VmState *base, int diff)
{
    if (!conn || conn->fd < 0 || !state || (diff && !base)) {
        net_set_error("invalid connection or state");
        return -1;
    }

    size_t n = diff ? vm_serialize_diff(state, base, NULL, 0)
                    : vm_serialize(state, NULL, 0);
    if (n == 0u) {
        net_set_error("state serialization failed");
        return -1;
    }
    if (n > NET_MAX_STATE_BYTES) {
        net_set_error("state too large (%zu bytes, limit %u)", n,
                      (unsigned)NET_MAX_STATE_BYTES);
        return -1;
    }

    uint8_t *wire = malloc(n);
    if (!wire) {
        net_set_error("out of memory");
        return -1;
    }
    size_t written = diff ? vm_serialize_diff(state, base, wire, n)
                          : vm_serialize(state, wire, n);
    if (written != n) {
        net_set_error("state serialization failed");
        free(wire);
        return -1;
    }

    uint8_t prefix[4];
    write_u32_be(prefix, (uint32_t)n);
    int rc = send_all(conn->fd, prefix, sizeof prefix);
    if (rc == 0) rc = send_all(conn->fd, wire, n);

    free(wire);
    return rc;
}

static int recv_state_impl(NetConn *conn, VmState *state,
                           const VmState *base, int diff)
{
    if (!conn || conn->fd < 0 || !state || (diff && !base)) {
        net_set_error("invalid connection or state");
        return -1;
    }

    uint8_t prefix[4];
    if (recv_all(conn->fd, prefix, sizeof prefix) != 0) return -1;

    uint32_t n32 = read_u32_be(prefix);
    if (n32 == 0u) return NET_RECV_HALTED; /* peer finished cleanly */
    if (n32 > NET_MAX_STATE_BYTES) {
        net_set_error("invalid state length %u", (unsigned)n32);
        return -1;
    }

    uint8_t *wire = malloc(n32);
    if (!wire) {
        net_set_error("out of memory");
        return -1;
    }
    if (recv_all(conn->fd, wire, n32) != 0) {
        free(wire);
        return -1;
    }

    int rc = diff ? vm_deserialize_diff(state, base, wire, n32)
                  : vm_deserialize(state, wire, n32);
    if (rc != 0) net_set_error("invalid VM state payload");
    free(wire);
    return rc == 0 ? 0 : -1;
}

int net_send_halt(NetConn *conn)
{
    if (!conn || conn->fd < 0) {
        net_set_error("invalid connection");
        return -1;
    }
    uint8_t prefix[4] = {0u, 0u, 0u, 0u};
    return send_all(conn->fd, prefix, sizeof prefix);
}

int net_send_state(NetConn *conn, const VmState *state)
{
    return send_state_impl(conn, state, NULL, 0);
}

int net_recv_state(NetConn *conn, VmState *state)
{
    return recv_state_impl(conn, state, NULL, 0);
}

int net_send_state_diff(NetConn *conn, const VmState *state, const VmState *base)
{
    return send_state_impl(conn, state, base, 1);
}

int net_recv_state_diff(NetConn *conn, VmState *state, const VmState *base)
{
    return recv_state_impl(conn, state, base, 1);
}

int net_set_timeout(NetConn *conn, int seconds)
{
    if (!conn || conn->fd < 0 || seconds < 0) {
        net_set_error("invalid connection or timeout");
        return -1;
    }

    struct timeval tv;
    tv.tv_sec = seconds;
    tv.tv_usec = 0;
    if (setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv) != 0 ||
        setsockopt(conn->fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv) != 0) {
        net_set_error("setsockopt(timeout): %s", strerror(errno));
        return -1;
    }
    return 0;
}

void net_close(NetConn *conn)
{
    if (!conn || conn->fd < 0) return;
    close(conn->fd);
    conn->fd = -1;
}

void net_close_listener(NetListener *listener)
{
    if (!listener || listener->fd < 0) return;
    close(listener->fd);
    listener->fd = -1;
}
