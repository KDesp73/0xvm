// SPDX-License-Identifier: MIT
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asm.h"
#include "tests.h"
#include "tools.h"
#include "vm.h"

static int failures;

#define CHECK(cond) do {                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                         \
        }                                                                       \
    } while (0)

/* Global addresses used by examples/typography.asm. */
enum {
    G_TEXT = 16,
    G_LEN  = 20,
    G_CARET = 24,
    G_FRAMES = 32
};

#define CARET_COLOR 0xB9E5ADu   /* house accent: focus/cursor */
#define TEXT_X      16          /* one tile inside the hairline box */
#define TEXT_Y      24          /* below the 8px header and box top */
#define CELL_W_PX   8           /* 8x8 house cell */
#define CELL_H_PX   8
#define GLYPH_W_PX  5           /* 5px glyph plus a 3px caret gutter */
#define MAX_CARET_PIXELS 4096

typedef struct {
    uint16_t x, y;
} Pixel;

typedef struct {
    VmState vm;
    int64_t key;
    AsmProgram *program;
    uint32_t color;
    Pixel carets[MAX_CARET_PIXELS];
    size_t caret_count;
    int sleep_writes;
    int64_t last_sleep_ms;
} Typo;

static VmStatus typo_read(void *ctx, VmState *vm, uint8_t port, int64_t *out)
{
    Typo *t = ctx;
    (void)vm;
    switch (port) {
        case VM_SCREEN_WIDTH:    *out = 320; return VM_OK;
        case VM_SCREEN_HEIGHT:   *out = 240; return VM_OK;
        case VM_CONTROLLER_KEY:
            *out = t->key;
            t->key = 0;
            return VM_OK;
        case VM_CONTROLLER_QUIT: *out = 0; return VM_OK;
        case VM_TIMER_MS:        *out = 250; return VM_OK; /* caret always on */
        default: return VM_OK;
    }
}

static VmStatus typo_write(void *ctx, VmState *vm, uint8_t port, int64_t value)
{
    Typo *t = ctx;
    (void)vm;
    if (port == 0x41u) {        /* host sleep extension */
        t->sleep_writes++;
        t->last_sleep_ms = value;
    } else if (port == VM_SCREEN_COLOR) {
        t->color = (uint32_t)(uint64_t)value;
    } else if (port == VM_SCREEN_PIXEL && t->color == CARET_COLOR &&
               ((uint64_t)value & 0xFFFFu) >= TEXT_Y &&
               ((uint64_t)value & 0xFFFFu) < TEXT_Y + CELL_H_PX &&
               t->caret_count < MAX_CARET_PIXELS) {
        uint64_t packed = (uint64_t)value;
        t->carets[t->caret_count].x = (uint16_t)((packed >> 16u) & 0xFFFFu);
        t->carets[t->caret_count].y = (uint16_t)(packed & 0xFFFFu);
        t->caret_count++;
    }
    return VM_OK;
}

static int32_t rd32(const VmState *vm, uint16_t addr)
{
    uint32_t value = 0u;
    for (unsigned i = 0u; i < 4u; i++)
        value = (value << 8) | vm->mem[addr + (uint16_t)i];
    return (int32_t)value;
}

/* Glyph records are stored bottom row first, each eight bytes per character. */
static void read_glyph(Typo *t, char ch, uint8_t out[8])
{
    const AsmSymbol *glyphs = asm_symbol_by_name(t->program, "glyphs");
    CHECK(glyphs != NULL);
    if (!glyphs) return;
    uint16_t addr = (uint16_t)(glyphs->value + (ch - 32) * 8);
    for (unsigned i = 0u; i < 8u; i++) out[i] = t->vm.mem[addr + i];
}

/* Advance one demo frame: the key is consumed on the next main-loop pass. */
static void press(Typo *t, int64_t key)
{
    int32_t before = rd32(&t->vm, G_FRAMES);
    t->key = key;
    for (uint64_t i = 0u; i < 2000000u; i++) {
        VmStatus st = vm_step(&t->vm);
        if (st != VM_OK) {
            fprintf(stderr, "FAIL typography: VM stopped with %s\n",
                    vm_status_str(st));
            failures++;
            return;
        }
        if (rd32(&t->vm, G_FRAMES) > before) return;
    }
    fprintf(stderr, "FAIL typography: frame timed out\n");
    failures++;
}

static void expect_text(Typo *t, const char *want, int32_t caret)
{
    int32_t len = rd32(&t->vm, G_LEN);
    uint16_t addr = (uint16_t)rd32(&t->vm, G_TEXT);
    CHECK(len == (int32_t)(strlen(want)));
    CHECK(rd32(&t->vm, G_CARET) == caret);
    CHECK(memcmp(t->vm.mem + addr, want, (size_t)len) == 0);
    CHECK(t->vm.sp == 0u);      /* edit keys must not leak stack arguments */
}

/* The caret must occupy the trailing gutter of its cell, not the centre of
 * the glyph drawn in that cell. The first gutter pixel is the ink outline;
 * the two accent pixels after it are tracked here. */
static void expect_caret_column(Typo *t, int32_t column)
{
    uint16_t want_x = (uint16_t)(TEXT_X + column * CELL_W_PX + GLYPH_W_PX + 1);
    bool left = false, right = false;

    CHECK(t->caret_count > 0u);
    for (size_t i = 0u; i < t->caret_count; i++) {
        Pixel p = t->carets[i];
        CHECK(p.y >= TEXT_Y && p.y < TEXT_Y + CELL_H_PX);
        if (p.x == want_x) left = true;
        if (p.x == want_x + 1u) right = true;
        CHECK(p.x != (uint16_t)(TEXT_X + column * CELL_W_PX + 4));
    }
    CHECK(left && right);
    t->caret_count = 0u;
}

/* With no key and nothing dirty the demo must ask the host to sleep, with the
 * wait capped so input stays responsive. */
static void expect_sleep(Typo *t)
{
    int before = t->sleep_writes;
    int32_t frames = rd32(&t->vm, G_FRAMES);
    t->key = 0;
    for (uint64_t i = 0u; i < 2000000u && t->sleep_writes == before; i++) {
        VmStatus st = vm_step(&t->vm);
        if (st != VM_OK) {
            fprintf(stderr, "FAIL typography: VM stopped with %s\n",
                    vm_status_str(st));
            failures++;
            return;
        }
    }
    CHECK(t->sleep_writes > before);
    CHECK(t->last_sleep_ms > 0 && t->last_sleep_ms <= 50);
    CHECK(rd32(&t->vm, G_FRAMES) == frames); /* no redraw while idle */
}

static void test_editing(void)
{
    Typo t = {0};
    VmDevice dev = {&t, typo_read, typo_write};
    vm_init(&t.vm);
    vm_set_device(&t.vm, &dev);

    if (tools_load_source(&t.vm, "examples/typography.asm", &t.program) != VM_OK) {
        fprintf(stderr, "FAIL typography: cannot load examples/typography.asm\n");
        failures++;
        goto done;
    }

    /* '6' must have an open tail and a bottom bowl, not a symmetric oval;
     * lowercase 'n' is x-height and must be distinct from both 'N' and 'm'. */
    {
        /* Glyph records are stored bottom row first: byte 0 is the bottom row,
         * byte 7 is the blank top row. */
        static const uint8_t want6[8] = {0x70,0x88,0x88,0xF0,0x80,0x80,0x70,0x00};
        static const uint8_t wantn[8] = {0x88,0x88,0x88,0xC8,0xB0,0x00,0x00,0x00};
        uint8_t six[8], lower[8], upper[8], em[8];
        read_glyph(&t, '6', six);
        read_glyph(&t, 'n', lower);
        read_glyph(&t, 'N', upper);
        read_glyph(&t, 'm', em);
        CHECK(memcmp(six, want6, sizeof six) == 0);
        CHECK(memcmp(lower, wantn, sizeof lower) == 0);
        CHECK(memcmp(lower, upper, sizeof lower) != 0);
        CHECK(memcmp(lower, em, sizeof lower) != 0);
    }

    press(&t, 0);               /* initial frame, no key */

    press(&t, 'A');
    press(&t, 'B');
    press(&t, 'C');
    expect_text(&t, "ABC", 3);

    t.caret_count = 0u;
    press(&t, 3);               /* caret between 'B' and 'C' (column 2) */
    expect_caret_column(&t, 2);
    press(&t, 3);               /* caret before 'B' (column 1) */
    press(&t, 8);               /* backspace removes 'A' */
    expect_text(&t, "BC", 0);

    press(&t, 8);               /* backspace at start is a no-op */
    expect_text(&t, "BC", 0);

    press(&t, 'X');
    press(&t, 'Y');
    expect_text(&t, "XYBC", 2);

    press(&t, 3);
    press(&t, 3);
    press(&t, 3);               /* caret back at 0 */
    press(&t, 9);               /* delete removes 'X' */
    expect_text(&t, "YBC", 0);

    press(&t, 4);               /* right to end of buffer */
    press(&t, 4);
    press(&t, 4);
    press(&t, 9);               /* delete past the end is a no-op */
    expect_text(&t, "YBC", 3);

    press(&t, 3);               /* caret on last character */
    press(&t, 9);               /* delete last character */
    expect_text(&t, "YB", 2);
    press(&t, 8);               /* backspace last character */
    expect_text(&t, "Y", 1);

    expect_sleep(&t);           /* idle frame hands control to the host */

done:
    if (t.program) {
        asm_program_free(t.program);
        free(t.program);
    }
    vm_free(&t.vm);
}

int test_typography(void)
{
    test_editing();
    return failures;
}
