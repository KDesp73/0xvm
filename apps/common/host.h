// SPDX-License-Identifier: MIT
#ifndef APPS_COMMON_HOST_H
#define APPS_COMMON_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "vm.h"

/*
 * A host is a set of port ranges handled by one VmDevice callback pair. This
 * keeps System/Console code shared between hosts while each host can add its
 * own devices (Screen, Controller, timer, ...) without touching lib0xvm.
 */
#define HOST_MAX_DEVICES 8u

typedef struct {
    uint8_t first, last;   /* inclusive port range */
    void *ctx;
    VmDeviceRead read;
    VmDeviceWrite write;
} HostPortDevice;

typedef struct {
    HostPortDevice devices[HOST_MAX_DEVICES];
    size_t count;
} VmHost;

void host_init(VmHost *host);
/* Returns -1 if the table is full. */
int host_add(VmHost *host, uint8_t first, uint8_t last, void *ctx,
             VmDeviceRead read, VmDeviceWrite write);
/* The returned VmDevice borrows `host`, which must outlive the VM. */
VmDevice host_device(VmHost *host);

/* Shared System + Console-over-stdio implementation. */
typedef struct {
    uint16_t puts_addr, puts_len;
    uint16_t gets_addr, gets_max, gets_len;
    FILE *input;       /* Console input; defaults to stdin */
    FILE *output;      /* Console output; defaults to stdout */
    bool debug;
    uint8_t caps;      /* capability mask reported by VM_SYS_CAPS */
} HostStdio;

void host_stdio_init(HostStdio *stdio, bool debug);
/* Adds the System range (0x00..0x03) and Console range (0x10..0x1B). */
int host_stdio_add(VmHost *host, HostStdio *stdio);

#endif /* APPS_COMMON_HOST_H */
