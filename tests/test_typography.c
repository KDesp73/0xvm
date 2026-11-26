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

#define CARET_COLOR 0xFFC040u
#define TEXT_X      3           /* BORDER + 1 */
#define TEXT_Y      21          /* BAR_H + 1 */
#define CELL_W_PX   12          /* (5 + 1) * SCALE */
#define CELL_H_PX   16          /* 8 * SCALE */
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
    if (port == VM_SCREEN_COLOR) {
        t->color = (uint32_t)(uint64_t)value;
    } else if (port == VM_SCREEN_PIXEL && t->color == CARET_COLOR &&
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

/* The caret must occupy the two-pixel gutter before its cell, not the
 * centre of the glyph drawn in that cell. */
static void expect_caret_column(Typo *t, int32_t column)
{
    uint16_t want_x = (uint16_t)(TEXT_X + column * CELL_W_PX - 2);
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
