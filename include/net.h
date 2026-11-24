#ifndef NET_H
#define NET_H

#include <stddef.h>
#include <stdint.h>

#include "vm.h"

#define NET_DEFAULT_TIMEOUT_SEC 10
#define NET_MAX_STATE_BYTES     (64u * 1024u * 1024u)
/* A zero-length frame is the graceful "peer halted" signal. */
#define NET_RECV_HALTED         1

typedef struct {
    int fd;
} NetListener;

typedef struct {
    int fd;
} NetConn;

/* Create a TCP listener. A NULL/empty host defaults to 127.0.0.1.
 * Port 0 requests an ephemeral port; use net_listener_port() to read it. */
int net_listen(const char *host, uint16_t port, NetListener *out);
uint16_t net_listener_port(const NetListener *listener);
int net_accept(NetListener *listener, NetConn *out);

/* Connect to a TCP peer. A NULL/empty host defaults to 127.0.0.1. */
int net_connect(const char *host, uint16_t port, NetConn *out);

/* Send/receive one length-framed vm_serialize() payload.
 * net_recv_state() requires state to be initialized (vm_init) or previously
 * deserialized; any heap it owns is released before the new state is installed. */
int net_send_state(NetConn *conn, const VmState *state);
int net_recv_state(NetConn *conn, VmState *state);

/* Tell the peer the VM halted. Receivers return NET_RECV_HALTED on this frame. */
int net_send_halt(NetConn *conn);

/* Page-level incremental transfer: only changed 4 KiB heap pages cross the
 * wire. The receiver starts from a copy of `base`. */
int net_send_state_diff(NetConn *conn, const VmState *state, const VmState *base);
int net_recv_state_diff(NetConn *conn, VmState *state, const VmState *base);

/* Set SO_RCVTIMEO/SO_SNDTIMEO on a connection. Timeouts are retryable
 * wake-ups, not transfer failures, so slow peers are waited for. */
int net_set_timeout(NetConn *conn, int seconds);

void net_close(NetConn *conn);
void net_close_listener(NetListener *listener);

/* Human-readable description of the most recent failure in this module. */
const char *net_last_error(void);

#endif /* NET_H */
