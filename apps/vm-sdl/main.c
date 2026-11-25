#include <SDL3/SDL.h>

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asm.h"
#include "host.h"
#include "io/logging.h"
#include "tools.h"
#include "vm.h"
#define CLI_IMPLEMENTATION
#include "io/cli.h"

/* ------------------------------------------------------------ SDL devices */

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    const SDL_PixelFormatDetails *format;
    Uint32 *pixels;
    int width, height;
    Uint32 color;
    int64_t key;        /* queued key-down, consumed by VM_CONTROLLER_KEY */
    bool quit;          /* set by a window-close event */
    Uint64 start_ms;
} SdlDevice;

/* Portable VM key codes: 1..4 = arrows, 5 = space, 6 = enter, 7 = escape,
 * 32..126 = printable ASCII. Anything else reports 0 (no key). */
static int64_t map_key(SDL_Keycode key)
{
    switch (key) {
        case SDLK_UP:    return 1;
        case SDLK_DOWN:  return 2;
        case SDLK_LEFT:  return 3;
        case SDLK_RIGHT: return 4;
        case SDLK_SPACE: return 5;
        case SDLK_RETURN:
        case SDLK_RETURN2: return 6;
        case SDLK_ESCAPE: return 7;
        default: break;
    }
    if (key >= 32u && key <= 126u) return (int64_t)key;
    return 0;
}

static void sdl_poll(SdlDevice *d)
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            d->quit = true;
        } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
            int64_t code = map_key(event.key.key);
            if (code != 0) d->key = code;
        }
    }
}

static bool sdl_present(SdlDevice *d)
{
    int pitch = d->width * (int)sizeof *d->pixels;
    if (!SDL_UpdateTexture(d->texture, NULL, d->pixels, pitch)) return false;
    if (!SDL_RenderClear(d->renderer)) return false;
    if (!SDL_RenderTexture(d->renderer, d->texture, NULL, NULL)) return false;
    SDL_RenderPresent(d->renderer);
    return true;
}

static void sdl_device_free(SdlDevice *d);

static bool sdl_device_init(SdlDevice *d, int width, int height)
{
    memset(d, 0, sizeof *d);
    d->width = width;
    d->height = height;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        ERRO("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    d->window = SDL_CreateWindow("0xvm", width, height, 0);
    if (!d->window) {
        ERRO("SDL_CreateWindow failed: %s", SDL_GetError());
        goto fail;
    }
    d->renderer = SDL_CreateRenderer(d->window, NULL);
    if (!d->renderer) {
        ERRO("SDL_CreateRenderer failed: %s", SDL_GetError());
        goto fail;
    }
    d->texture = SDL_CreateTexture(d->renderer, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!d->texture) {
        ERRO("SDL_CreateTexture failed: %s", SDL_GetError());
        goto fail;
    }
    d->format = SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32);
    d->pixels = calloc((size_t)width * (size_t)height, sizeof *d->pixels);
    if (!d->format || !d->pixels) {
        ERRO("Cannot allocate framebuffer");
        goto fail;
    }
    d->color = SDL_MapRGBA(d->format, NULL, 255, 255, 255, 255);
    d->start_ms = SDL_GetTicks();
    if (!sdl_present(d)) {
        ERRO("Initial frame failed: %s", SDL_GetError());
        goto fail;
    }
    return true;

fail:
    sdl_device_free(d);
    return false;
}

static void sdl_device_free(SdlDevice *d)
{
    if (d->texture) SDL_DestroyTexture(d->texture);
    if (d->renderer) SDL_DestroyRenderer(d->renderer);
    if (d->window) SDL_DestroyWindow(d->window);
    free(d->pixels);
    SDL_Quit();
    memset(d, 0, sizeof *d);
}

static VmStatus sdl_read(void *ctx, VmState *vm, uint8_t port, int64_t *out)
{
    SdlDevice *d = ctx;
    (void)vm;

    switch (port) {
        case VM_SCREEN_WIDTH:
            *out = d->width;
            return VM_OK;
        case VM_SCREEN_HEIGHT:
            *out = d->height;
            return VM_OK;
        case VM_CONTROLLER_KEY:
            *out = d->key;
            d->key = 0;              /* key-down is consumed on read */
            return VM_OK;
        case VM_CONTROLLER_QUIT:
            *out = d->quit ? 1 : 0;
            return VM_OK;
        case VM_TIMER_MS:
            *out = (int64_t)(SDL_GetTicks() - d->start_ms);
            return VM_OK;
        default:
            return VM_ERR_IO;
    }
}

static VmStatus sdl_write(void *ctx, VmState *vm, uint8_t port, int64_t value)
{
    SdlDevice *d = ctx;
    (void)vm;

    switch (port) {
        case VM_SCREEN_COLOR: {
            uint32_t rgb = (uint32_t)(uint64_t)value;
            d->color = SDL_MapRGBA(d->format, NULL,
                (Uint8)((rgb >> 16) & 0xFFu),
                (Uint8)((rgb >> 8) & 0xFFu),
                (Uint8)(rgb & 0xFFu), 255);
            return VM_OK;
        }
        case VM_SCREEN_PIXEL: {
            uint64_t packed = (uint64_t)value;
            uint32_t x = (uint32_t)((packed >> 16) & 0xFFFFu);
            uint32_t y = (uint32_t)(packed & 0xFFFFu);
            if (x >= (uint32_t)d->width || y >= (uint32_t)d->height)
                return VM_ERR_IO;
            d->pixels[(size_t)y * (size_t)d->width + x] = d->color;
            return VM_OK;
        }
        case VM_SCREEN_DRAW:
            return sdl_present(d) ? VM_OK : VM_ERR_IO;
        case VM_SCREEN_CLEAR: {
            size_t count = (size_t)d->width * (size_t)d->height;
            for (size_t i = 0u; i < count; i++) d->pixels[i] = d->color;
            return sdl_present(d) ? VM_OK : VM_ERR_IO;
        }
        default:
            return VM_ERR_IO;
    }
}

static int sdl_device_add(VmHost *host, SdlDevice *d)
{
    if (host_add(host, VM_SCREEN_WIDTH, VM_SCREEN_CLEAR, d,
                 sdl_read, sdl_write) != 0) return -1;
    if (host_add(host, VM_CONTROLLER_KEY, VM_CONTROLLER_QUIT, d,
                 sdl_read, sdl_write) != 0) return -1;
    if (host_add(host, VM_TIMER_MS, VM_TIMER_MS, d,
                 sdl_read, sdl_write) != 0) return -1;
    return 0;
}

/* ------------------------------------------------------------------- run */

static int run(const char *path, int width, int height, bool debug)
{
    uint8_t *filebuf = NULL;
    size_t filelen = 0u;
    if (!tools_read_file(path, &filebuf, &filelen)) return EXIT_FAILURE;

    SdlDevice sdl;
    if (!sdl_device_init(&sdl, width, height)) {
        free(filebuf);
        return EXIT_FAILURE;
    }

    VmState vm;
    vm_init(&vm);
    VmHost host;
    host_init(&host);
    HostStdio stdio;
    host_stdio_init(&stdio, debug);
    host_stdio_add(&host, &stdio);
    sdl_device_add(&host, &sdl);
    VmDevice device = host_device(&host);
    vm_set_device(&vm, &device);

    AsmProgram *program = NULL;
    VmStatus st;
    if (tools_is_rom(filebuf, filelen)) {
        st = vm_load_rom(&vm, filebuf, filelen);
        if (st == VM_OK) INFO("Loaded ROM %s (%zu bytes)", path, filelen);
    } else {
        st = tools_load_source(&vm, path, &program);
        if (st == VM_OK)
            INFO("Loaded %s (%zu payload bytes)", path, program->len);
    }
    free(filebuf);

    if (st == VM_OK) {
        for (;;) {
            sdl_poll(&sdl);
            st = vm_step(&vm);
            if (st != VM_OK) break;
        }
    }
    if (st != VM_OK && st != VM_HALTED)
        ERRO("Run failed: %s", vm_status_str(st));
    INFO("Finished with status: %s (total ticks=%" PRIu64 ")",
         vm_status_str(st), vm.ticks);

    if (program) {
        asm_program_free(program);
        free(program);
    }
    vm_free(&vm);
    sdl_device_free(&sdl);
    return st == VM_HALTED ? EXIT_SUCCESS : EXIT_FAILURE;
}

static bool parse_size(const char *text, int *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 16 || value > 4096) return false;
    *out = (int)value;
    return true;
}

int main(int argc, char **argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Print this help message", no_argument),
        cli_arg_new('v', "version", "Print the program version", no_argument),
        cli_arg_new('f', "file", "Assembly source or VMIM ROM file", required_argument),
        cli_arg_new('W', "width", "Framebuffer width (16..4096, default 320)", required_argument),
        cli_arg_new('H', "height", "Framebuffer height (16..4096, default 240)", required_argument),
        cli_arg_new('D', "debug", "Prefix Console output with vm tick numbers", no_argument),
        NULL
    );

    const char *path = NULL;
    int width = 320, height = 240;
    bool debug = false;
    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, NULL, "0xvm-sdl -f FILE [-W WIDTH] [-H HEIGHT] [-D]",
                         "Run a ROM with Screen, Controller, and timer devices.");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'v':
                tools_print_version("0xvm-sdl");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'f': path = optarg; break;
            case 'W':
                if (!parse_size(optarg, &width)) {
                    ERRO("Invalid width '%s'", optarg);
                    cli_args_free(&args);
                    return EXIT_FAILURE;
                }
                break;
            case 'H':
                if (!parse_size(optarg, &height)) {
                    ERRO("Invalid height '%s'", optarg);
                    cli_args_free(&args);
                    return EXIT_FAILURE;
                }
                break;
            case 'D': debug = true; break;
            default: break;
        }
    }
    cli_args_free(&args);

    if (!path) {
        ERRO("Provide a file with -f/--file");
        return EXIT_FAILURE;
    }
    return run(path, width, height, debug);
}
