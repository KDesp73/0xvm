// SPDX-License-Identifier: MIT
#include <emscripten.h>

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

/* Implemented by hosts/wasm/library.js. */
extern void vmw_js_screen_init(int width, int height);
extern void vmw_js_screen_color(int r, int g, int b);
extern void vmw_js_screen_pixel(int x, int y);
extern void vmw_js_screen_draw(void);
extern void vmw_js_screen_clear(void);
extern void vmw_js_timer_reset(void);
extern double vmw_js_now_ms(void);
extern void vmw_js_console_write(const char *text, size_t len);

#define WEB_KEY_CAP      16u
#define WEB_INPUT_CAP    1024u
#define WEB_LINE_CAP     256u

typedef struct {
    int width, height;
    uint32_t color;
    bool debug;
    bool quit;

    int64_t keys[WEB_KEY_CAP];
    size_t key_head, key_len;

    char input[WEB_INPUT_CAP];
    size_t input_head, input_len;

    uint16_t puts_addr, puts_len;
    uint16_t gets_addr, gets_max, gets_len;
    bool sleep_requested;
} WebDevice;

static VmState g_vm;
static WebDevice g_web;
static VmDevice g_device;
static VmStatus g_status = VM_OK;
static bool g_ready;

/* --------------------------------------------------------------- JS helpers */

static void console_write(const char *text, size_t len)
{
    vmw_js_console_write(text, len);
}

static void console_text(const char *text)
{
    console_write(text, strlen(text));
}

static void console_line_prefix(VmState *vm)
{
    if (!g_web.debug) return;
    char prefix[48];
    int n = snprintf(prefix, sizeof prefix, "[vm tick=%" PRIu64 "] ", vm->ticks);
    if (n > 0) console_write(prefix, (size_t)n < sizeof prefix ? (size_t)n : sizeof prefix - 1u);
}

static void console_line(VmState *vm, const char *line)
{
    console_line_prefix(vm);
    console_text(line);
    console_text("\n");
}

/* ----------------------------------------------------------- input queues */

static void input_append(WebDevice *d, const char *text)
{
    if (!text) return;
    size_t n = strlen(text);
    if (n == 0u) return;
    if (n > WEB_INPUT_CAP) {
        text += n - WEB_INPUT_CAP;
        n = WEB_INPUT_CAP;
    }

    if (d->input_head + d->input_len + n > WEB_INPUT_CAP) {
        memmove(d->input, d->input + d->input_head, d->input_len);
        d->input_head = 0u;
    }
    if (n > WEB_INPUT_CAP - d->input_len) {
        size_t drop = n - (WEB_INPUT_CAP - d->input_len);
        d->input_head += drop;
        d->input_len -= drop;
    }
    memcpy(d->input + d->input_head + d->input_len, text, n);
    d->input_len += n;
}

static void input_consume(WebDevice *d, size_t n)
{
    if (n > d->input_len) n = d->input_len;
    d->input_head += n;
    d->input_len -= n;
    if (d->input_len == 0u) d->input_head = 0u;
}

static int input_pop_byte(WebDevice *d)
{
    if (d->input_len == 0u) return -1;
    unsigned char ch = (unsigned char)d->input[d->input_head];
    input_consume(d, 1u);
    return (int)ch;
}

/* Returns 0 until a complete line is available. The queue is left untouched. */
static int input_pop_line(WebDevice *d, char *out, size_t cap)
{
    if (cap == 0u || d->input_len == 0u) return 0;

    size_t i = 0u;
    while (i < d->input_len) {
        char ch = d->input[d->input_head + i];
        if (ch == '\n' || ch == '\r') break;
        i++;
    }
    if (i == d->input_len) return 0;

    size_t len = i < cap - 1u ? i : cap - 1u;
    memcpy(out, d->input + d->input_head, len);
    out[len] = '\0';

    size_t consumed = i + 1u;
    if (consumed < d->input_len &&
        d->input[d->input_head + i] == '\r' &&
        d->input[d->input_head + consumed] == '\n') {
        consumed++;
    }
    input_consume(d, consumed);
    return 1;
}

/* ------------------------------------------------------------ VM callbacks */

static VmStatus web_read(void *ctx, VmState *vm, uint8_t port, int64_t *out)
{
    WebDevice *d = ctx;
    (void)vm;

    switch (port) {
        case VM_SYS_VERSION:
            *out = (int64_t)VM_ROM_VERSION;
            return VM_OK;
        case VM_SYS_DEBUG:
            *out = d->debug ? 1 : 0;
            return VM_OK;
        case VM_SYS_CAPS:
            *out = VM_CAPS_CONSOLE | VM_CAPS_SCREEN |
                   VM_CAPS_CONTROLLER | VM_CAPS_TIMER;
            return VM_OK;

        case VM_CONSOLE_GETC: {
            int ch = input_pop_byte(d);
            if (ch < 0) return VM_ERR_IO; /* pause until the page supplies input */
            *out = (int64_t)ch;
            return VM_OK;
        }
        case VM_CONSOLE_GETI: {
            char line[WEB_LINE_CAP];
            if (!input_pop_line(d, line, sizeof line)) return VM_ERR_IO;
            char *end = NULL;
            errno = 0;
            long long value = strtoll(line, &end, 10);
            if (end == line || errno == ERANGE) value = 0;
            *out = (int64_t)value;
            return VM_OK;
        }
        case VM_CONSOLE_GETS_LEN:
            *out = (int64_t)d->gets_len;
            return VM_OK;

        case VM_SCREEN_WIDTH:
            *out = d->width;
            return VM_OK;
        case VM_SCREEN_HEIGHT:
            *out = d->height;
            return VM_OK;

        case VM_CONTROLLER_KEY:
            *out = d->key_len > 0u ? d->keys[d->key_head] : 0;
            if (d->key_len > 0u) {
                d->key_head = (d->key_head + 1u) % WEB_KEY_CAP;
                d->key_len--;
            }
            return VM_OK;
        case VM_CONTROLLER_QUIT:
            *out = d->quit ? 1 : 0;
            return VM_OK;

        case VM_TIMER_MS:
            *out = (int64_t)vmw_js_now_ms();
            return VM_OK;
        case 0x41u: /* host extension: sleep ms, as in the SDL host */
            *out = 0;
            return VM_OK;

        default:
            return VM_ERR_IO;
    }
}

static VmStatus web_write(void *ctx, VmState *vm, uint8_t port, int64_t value)
{
    WebDevice *d = ctx;

    switch (port) {
        case VM_SYS_DEBUG:
            d->debug = value != 0;
            return VM_OK;
        case VM_SYS_HALT:
            return VM_HALTED;

        case VM_CONSOLE_PUTC: {
            char ch = (char)(unsigned char)(uint64_t)value;
            console_write(&ch, 1u);
            return VM_OK;
        }
        case VM_CONSOLE_PUTI: {
            char line[64];
            snprintf(line, sizeof line, "%" PRId64, value);
            console_line(vm, line);
            return VM_OK;
        }
        case VM_CONSOLE_PUTF: {
            char line[64];
            snprintf(line, sizeof line, "%.17g", vm_bits_to_f64(value));
            console_line(vm, line);
            return VM_OK;
        }
        case VM_CONSOLE_PUTS_ADDR:
            d->puts_addr = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_PUTS_LEN:
            d->puts_len = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_PUTS:
            if ((uint32_t)d->puts_addr + (uint32_t)d->puts_len > VM_RAM_SIZE)
                return VM_ERR_IO;
            console_line_prefix(vm);
            if (d->puts_len > 0u)
                console_write((const char *)vm->mem + d->puts_addr, d->puts_len);
            console_text("\n");
            return VM_OK;

        case VM_CONSOLE_GETS_ADDR:
            d->gets_addr = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_GETS_MAX:
            d->gets_max = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_GETS: {
            if (d->gets_max == 0u ||
                (uint32_t)d->gets_addr + (uint32_t)d->gets_max > VM_RAM_SIZE)
                return VM_ERR_IO;
            char line[WEB_LINE_CAP];
            if (!input_pop_line(d, line, sizeof line)) return VM_ERR_IO;
            size_t cap = d->gets_max < sizeof line ? (size_t)d->gets_max : sizeof line;
            size_t len = strlen(line);
            if (len >= cap) len = cap - 1u;
            memcpy(vm->mem + d->gets_addr, line, len);
            d->gets_len = (uint16_t)len;
            return VM_OK;
        }

        case VM_SCREEN_COLOR: {
            d->color = (uint32_t)(uint64_t)value & 0xFFFFFFu;
            vmw_js_screen_color((int)((d->color >> 16) & 0xFFu),
                                (int)((d->color >> 8) & 0xFFu),
                                (int)(d->color & 0xFFu));
            return VM_OK;
        }
        case VM_SCREEN_PIXEL: {
            uint64_t packed = (uint64_t)value;
            int x = (int)((packed >> 16) & 0xFFFFu);
            int y = (int)(packed & 0xFFFFu);
            if (x >= d->width || y >= d->height) return VM_ERR_IO;
            vmw_js_screen_pixel(x, y);
            return VM_OK;
        }
        case VM_SCREEN_DRAW:
            vmw_js_screen_draw();
            return VM_OK;
        case VM_SCREEN_CLEAR:
            vmw_js_screen_clear();
            return VM_OK;
        case 0x41u: /* host extension: yield until the next animation frame */
            d->sleep_requested = true;
            return VM_OK;

        default:
            return VM_ERR_IO;
    }
}

/* ------------------------------------------------------------- public API */

static void reset_runtime(void)
{
    memset(g_web.keys, 0, sizeof g_web.keys);
    g_web.key_head = 0u;
    g_web.key_len = 0u;
    g_web.input_head = 0u;
    g_web.input_len = 0u;
    g_web.puts_addr = 0u;
    g_web.puts_len = 0u;
    g_web.gets_addr = 0u;
    g_web.gets_max = 0u;
    g_web.gets_len = 0u;
    g_web.quit = false;
    g_web.sleep_requested = false;
    vmw_js_timer_reset();
}

EMSCRIPTEN_KEEPALIVE int vmw_init(int width, int height)
{
    if (width < 16 || width > 4096 || height < 16 || height > 4096)
        return (int)VM_ERR_BAD_IP;

    memset(&g_web, 0, sizeof g_web);
    g_web.width = width;
    g_web.height = height;

    vm_init(&g_vm);
    g_device.ctx = &g_web;
    g_device.read = web_read;
    g_device.write = web_write;
    vm_set_device(&g_vm, &g_device);

    g_status = VM_OK;
    g_ready = true;
    vmw_js_screen_init(width, height);
    vmw_js_timer_reset();
    return (int)VM_OK;
}

EMSCRIPTEN_KEEPALIVE int vmw_load(const uint8_t *bytes, size_t len)
{
    if (!g_ready || !bytes) return (int)VM_ERR_BAD_IP;
    reset_runtime();
    g_status = vm_load_rom(&g_vm, bytes, len);
    return (int)g_status;
}

EMSCRIPTEN_KEEPALIVE int vmw_step(void)
{
    if (!g_ready) return (int)VM_ERR_BAD_IP;
    g_status = vm_step(&g_vm);
    return (int)g_status;
}

EMSCRIPTEN_KEEPALIVE int vmw_run(int budget)
{
    if (!g_ready || budget <= 0) return (int)g_status;
    g_status = VM_OK;
    g_web.sleep_requested = false;
    /* Stop as soon as the ROM asks to sleep. The browser calls again on the
     * next animation frame, which provides the pacing without blocking JS. */
    for (int i = 0; i < budget; i++) {
        VmStatus st = vm_step(&g_vm);
        if (st != VM_OK) {
            g_status = st;
            break;
        }
        if (g_web.sleep_requested) break;
    }
    return (int)g_status;
}

EMSCRIPTEN_KEEPALIVE int vmw_status(void)
{
    return (int)g_status;
}

EMSCRIPTEN_KEEPALIVE double vmw_ticks(void)
{
    return (double)g_vm.ticks;
}

EMSCRIPTEN_KEEPALIVE int vmw_ip(void)
{
    return (int)g_vm.ip;
}

EMSCRIPTEN_KEEPALIVE int vmw_sp(void)
{
    return (int)g_vm.sp;
}

EMSCRIPTEN_KEEPALIVE int vmw_peek8(int addr)
{
    return (int)g_vm.mem[(uint16_t)addr];
}

EMSCRIPTEN_KEEPALIVE void vmw_push_key(int code)
{
    if (!g_ready || code == 0 || g_web.key_len >= WEB_KEY_CAP) return;
    g_web.keys[(g_web.key_head + g_web.key_len) % WEB_KEY_CAP] = (int64_t)code;
    g_web.key_len++;
}

EMSCRIPTEN_KEEPALIVE void vmw_request_quit(void)
{
    g_web.quit = true;
}

EMSCRIPTEN_KEEPALIVE void vmw_console_input(const char *text)
{
    input_append(&g_web, text);
}

EMSCRIPTEN_KEEPALIVE void vmw_set_debug(int enabled)
{
    g_web.debug = enabled != 0;
}
