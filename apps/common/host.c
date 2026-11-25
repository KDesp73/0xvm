#include "host.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------ port routing */

void host_init(VmHost *host)
{
    if (!host) return;
    memset(host, 0, sizeof *host);
}

int host_add(VmHost *host, uint8_t first, uint8_t last, void *ctx,
             VmDeviceRead read, VmDeviceWrite write)
{
    if (!host || host->count >= HOST_MAX_DEVICES || first > last) return -1;
    HostPortDevice *dev = &host->devices[host->count++];
    dev->first = first;
    dev->last = last;
    dev->ctx = ctx;
    dev->read = read;
    dev->write = write;
    return 0;
}

static const HostPortDevice *find_device(const VmHost *host, uint8_t port)
{
    for (size_t i = 0u; i < host->count; i++) {
        const HostPortDevice *dev = &host->devices[i];
        if (port >= dev->first && port <= dev->last) return dev;
    }
    return NULL;
}

static VmStatus host_read(void *ctx, VmState *vm, uint8_t port, int64_t *out)
{
    const HostPortDevice *dev = find_device(ctx, port);
    if (!dev || !dev->read || !out) return VM_ERR_IO;
    return dev->read(dev->ctx, vm, port, out);
}

static VmStatus host_write(void *ctx, VmState *vm, uint8_t port, int64_t value)
{
    const HostPortDevice *dev = find_device(ctx, port);
    if (!dev || !dev->write) return VM_ERR_IO;
    return dev->write(dev->ctx, vm, port, value);
}

VmDevice host_device(VmHost *host)
{
    VmDevice dev = {host, host_read, host_write};
    return dev;
}

/* ------------------------------------------------------- System + Console */

void host_stdio_init(HostStdio *stdio, bool debug)
{
    if (!stdio) return;
    memset(stdio, 0, sizeof *stdio);
    stdio->input = stdin;
    stdio->output = stdout;
    stdio->debug = debug;
}

static void stdio_prefix(const HostStdio *c, const VmState *vm)
{
    if (c->debug) fprintf(c->output, "[vm tick=%" PRIu64 "] ", vm->ticks);
}

static VmStatus stdio_flush(HostStdio *c)
{
    return fflush(c->output) == 0 ? VM_OK : VM_ERR_IO;
}

static VmStatus stdio_read(void *ctx, VmState *vm, uint8_t port, int64_t *out)
{
    HostStdio *c = ctx;
    (void)vm;

    switch (port) {
        case VM_SYS_VERSION:
            *out = (int64_t)VM_ROM_VERSION;
            return VM_OK;
        case VM_SYS_DEBUG:
            *out = c->debug ? 1 : 0;
            return VM_OK;
        case VM_CONSOLE_GETC: {
            int ch = fgetc(c->input);
            if (ch == EOF && ferror(c->input)) return VM_ERR_IO;
            *out = ch == EOF ? 0 : (int64_t)(unsigned char)ch;
            return VM_OK;
        }
        case VM_CONSOLE_GETI: {
            char line[128];
            if (!fgets(line, sizeof line, c->input)) {
                if (ferror(c->input)) return VM_ERR_IO;
                line[0] = '\0';
            }
            char *end = NULL;
            errno = 0;
            long long value = strtoll(line, &end, 10);
            if (end == line || errno == ERANGE) value = 0;
            *out = (int64_t)value;
            return VM_OK;
        }
        case VM_CONSOLE_GETS_LEN:
            *out = (int64_t)c->gets_len;
            return VM_OK;
        default:
            return VM_ERR_IO;
    }
}

static VmStatus stdio_write(void *ctx, VmState *vm, uint8_t port, int64_t value)
{
    HostStdio *c = ctx;

    switch (port) {
        case VM_SYS_DEBUG:
            c->debug = value != 0;
            return VM_OK;
        case VM_SYS_HALT:
            return VM_HALTED;
        case VM_CONSOLE_PUTC:
            fputc((unsigned char)(uint64_t)value, c->output);
            return stdio_flush(c);
        case VM_CONSOLE_PUTI:
            stdio_prefix(c, vm);
            fprintf(c->output, "%" PRId64 "\n", value);
            return stdio_flush(c);
        case VM_CONSOLE_PUTF:
            stdio_prefix(c, vm);
            fprintf(c->output, "%.17g\n", vm_bits_to_f64(value));
            return stdio_flush(c);
        case VM_CONSOLE_PUTS_ADDR:
            c->puts_addr = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_PUTS_LEN:
            c->puts_len = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_PUTS:
            if ((uint32_t)c->puts_addr + (uint32_t)c->puts_len > VM_RAM_SIZE)
                return VM_ERR_IO;
            stdio_prefix(c, vm);
            if (c->puts_len > 0u)
                fwrite(vm->mem + c->puts_addr, 1u, c->puts_len, c->output);
            fputc('\n', c->output);
            return stdio_flush(c);
        case VM_CONSOLE_GETS_ADDR:
            c->gets_addr = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_GETS_MAX:
            c->gets_max = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_GETS:
            if (c->gets_max == 0u ||
                (uint32_t)c->gets_addr + (uint32_t)c->gets_max > VM_RAM_SIZE)
                return VM_ERR_IO;
            {
                char *buf = malloc((size_t)c->gets_max + 1u);
                if (!buf) return VM_ERR_IO;
                if (!fgets(buf, (int)c->gets_max + 1, c->input)) {
                    if (ferror(c->input)) { free(buf); return VM_ERR_IO; }
                    buf[0] = '\0';
                }
                size_t len = strlen(buf);
                while (len > 0u && (buf[len - 1u] == '\n' || buf[len - 1u] == '\r'))
                    buf[--len] = '\0';
                memcpy(vm->mem + c->gets_addr, buf, len);
                free(buf);
                c->gets_len = (uint16_t)len;
            }
            return VM_OK;
        default:
            return VM_ERR_IO;
    }
}

int host_stdio_add(VmHost *host, HostStdio *stdio)
{
    if (host_add(host, VM_SYS_VERSION, VM_SYS_HALT, stdio,
                 stdio_read, stdio_write) != 0)
        return -1;
    if (host_add(host, VM_CONSOLE_PUTC, VM_CONSOLE_GETI, stdio,
                 stdio_read, stdio_write) != 0)
        return -1;
    return 0;
}
