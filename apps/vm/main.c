#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asm.h"
#include "io/logging.h"
#include "tools.h"
#include "vm.h"
#define CLI_IMPLEMENTATION
#include "io/cli.h"

typedef struct {
    bool trace;
    bool debug;
} RunOptions;

/* ------------------------------------------------------------ host devices */

typedef struct {
    uint16_t puts_addr, puts_len;
    uint16_t gets_addr, gets_max, gets_len;
    bool debug;
} Console;

static void console_prefix(const Console *c, const VmState *vm)
{
    if (c->debug) printf("[vm tick=%" PRIu64 "] ", vm->ticks);
}

static VmStatus console_flush(void)
{
    return fflush(stdout) == 0 ? VM_OK : VM_ERR_IO;
}

static VmStatus console_read(void *ctx, VmState *vm, uint8_t port, int64_t *out)
{
    Console *c = ctx;
    (void)vm;

    switch (port) {
        case VM_SYS_VERSION:
            *out = (int64_t)VM_ROM_VERSION;
            return VM_OK;
        case VM_SYS_DEBUG:
            *out = c->debug ? 1 : 0;
            return VM_OK;
        case VM_CONSOLE_GETC: {
            int ch = getchar();
            if (ch == EOF && ferror(stdin)) return VM_ERR_IO;
            *out = ch == EOF ? 0 : (int64_t)(unsigned char)ch;
            return VM_OK;
        }
        case VM_CONSOLE_GETI: {
            char line[128];
            if (!fgets(line, sizeof line, stdin)) {
                if (ferror(stdin)) return VM_ERR_IO;
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

static VmStatus console_write(void *ctx, VmState *vm, uint8_t port, int64_t value)
{
    Console *c = ctx;

    switch (port) {
        case VM_SYS_DEBUG:
            c->debug = value != 0;
            return VM_OK;
        case VM_SYS_HALT:
            return VM_HALTED;
        case VM_CONSOLE_PUTC:
            fputc((unsigned char)(uint64_t)value, stdout);
            return console_flush();
        case VM_CONSOLE_PUTI:
            console_prefix(c, vm);
            printf("%" PRId64 "\n", value);
            return console_flush();
        case VM_CONSOLE_PUTF:
            console_prefix(c, vm);
            printf("%.17g\n", vm_bits_to_f64(value));
            return console_flush();
        case VM_CONSOLE_PUTS_ADDR:
            c->puts_addr = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_PUTS_LEN:
            c->puts_len = (uint16_t)(uint64_t)value;
            return VM_OK;
        case VM_CONSOLE_PUTS:
            if ((uint32_t)c->puts_addr + (uint32_t)c->puts_len > VM_RAM_SIZE)
                return VM_ERR_IO;
            console_prefix(c, vm);
            if (c->puts_len > 0u)
                fwrite(vm->mem + c->puts_addr, 1u, c->puts_len, stdout);
            putchar('\n');
            return console_flush();
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
                if (!fgets(buf, (int)c->gets_max + 1, stdin)) {
                    if (ferror(stdin)) { free(buf); return VM_ERR_IO; }
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

static VmDevice console_device(Console *console)
{
    VmDevice dev = {console, console_read, console_write};
    return dev;
}

/* With -t, log each instruction and its source line before executing it. */
static VmStatus run_program(const RunOptions *opts, VmState *vm,
                            const AsmProgram *program)
{
    if (!opts->trace) return vm_run(vm, 0);

    for (;;) {
        if ((uint32_t)vm->ip >= VM_RAM_SIZE) return VM_ERR_BAD_IP;
        fprintf(stderr,
                "[trace addr=0x%04" PRIX16 " line=%zu op=%s sp=%u hp=0x%04" PRIX16 "]\n",
                vm->ip, asm_line_of(program, vm->ip),
                vm_opcode_name(vm->mem[vm->ip]), vm->sp, vm->hp);
        VmStatus st = vm_step(vm);
        if (st != VM_OK) return st;
    }
}

static int run(const char *path, const RunOptions *opts)
{
    uint8_t *filebuf = NULL;
    size_t filelen = 0u;
    if (!tools_read_file(path, &filebuf, &filelen)) return EXIT_FAILURE;

    VmState vm;
    vm_init(&vm);
    Console console = {0};
    console.debug = opts->debug;
    VmDevice device = console_device(&console);
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

    if (st != VM_OK) {
        ERRO("Load failed: %s", vm_status_str(st));
        vm_free(&vm);
        return EXIT_FAILURE;
    }

    st = run_program(opts, &vm, program);
    INFO("Finished with status: %s (total ticks=%" PRIu64 ")",
         vm_status_str(st), vm.ticks);

    if (program) {
        asm_program_free(program);
        free(program);
    }
    vm_free(&vm);
    return st == VM_HALTED ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Print this help message", no_argument),
        cli_arg_new('v', "version", "Print the program version", no_argument),
        cli_arg_new('f', "file", "Assembly source or VMIM ROM file", required_argument),
        cli_arg_new('t', "trace", "Trace address, line, and opcode", no_argument),
        cli_arg_new('D', "debug", "Prefix Console output with vm tick numbers", no_argument),
        NULL
    );

    RunOptions run_opts = {0};
    const char *path = NULL;
    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, NULL, "0xvm -f FILE [-t] [-D]",
                         "Run VMIM ROMs or assemble and run source files.");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'v':
                tools_print_version("0xvm");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'f': path = optarg; break;
            case 't': run_opts.trace = true; break;
            case 'D': run_opts.debug = true; break;
            default: break;
        }
    }
    cli_args_free(&args);

    if (!path) {
        ERRO("Provide a file with -f/--file");
        return EXIT_FAILURE;
    }
    return run(path, &run_opts);
}
