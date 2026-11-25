#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "asm.h"
#include "host.h"
#include "io/logging.h"
#include "tools.h"
#include "vm.h"
#define CLI_IMPLEMENTATION
#include "io/cli.h"

typedef struct {
    bool trace;
    bool debug;
} RunOptions;

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
    VmHost host;
    host_init(&host);
    HostStdio stdio;
    host_stdio_init(&stdio, opts->debug);
    host_stdio_add(&host, &stdio);
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
