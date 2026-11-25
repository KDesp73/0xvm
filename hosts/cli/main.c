#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
    bool profile;
} RunOptions;

typedef struct {
    uint8_t op;
    uint64_t count;
} ProfileEntry;

static int profile_cmp(const void *a, const void *b)
{
    const ProfileEntry *ea = a;
    const ProfileEntry *eb = b;
    if (eb->count < ea->count) return -1;
    if (eb->count > ea->count) return 1;
    return (int)ea->op - (int)eb->op;
}

static void print_profile(const uint64_t counts[256], uint64_t total)
{
    ProfileEntry entries[256];
    size_t n = 0u;
    for (unsigned i = 0u; i < 256u; i++) {
        if (counts[i] == 0u) continue;
        entries[n++] = (ProfileEntry){(uint8_t)i, counts[i]};
    }
    qsort(entries, n, sizeof(entries[0]), profile_cmp);

    fprintf(stderr, "\n[profile] total ticks=%" PRIu64 "\n", total);
    for (size_t i = 0u; i < n; i++) {
        double pct = total ? 100.0 * (double)entries[i].count / (double)total : 0.0;
        fprintf(stderr, "[profile] %-10s  %12" PRIu64 "  %6.2f%%\n",
                vm_opcode_name(entries[i].op), entries[i].count, pct);
    }
}

/* With -t, log each instruction and its source line before executing it.
 * With -p, count opcode executions. Both can be combined. */
static VmStatus run_program(const RunOptions *opts, VmState *vm,
                            const AsmProgram *program, uint64_t counts[256])
{
    if (!opts->trace && !opts->profile) return vm_run(vm, 0);

    for (;;) {
        uint8_t op = vm->mem[vm->ip];
        if (opts->trace) {
            fprintf(stderr,
                    "[trace addr=0x%04" PRIX16 " line=%zu op=%s sp=%u hp=0x%04" PRIX16 "]\n",
                    vm->ip, asm_line_of(program, vm->ip),
                    vm_opcode_name(op), vm->sp, vm->hp);
        }
        VmStatus st = vm_step(vm);
        if (opts->profile) counts[op]++;
        if (st != VM_OK) return st;
    }
}

static double elapsed_sec(const struct timespec *start,
                          const struct timespec *end)
{
    return (double)(end->tv_sec - start->tv_sec) +
           (double)(end->tv_nsec - start->tv_nsec) / 1e9;
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

    uint64_t counts[256] = {0};
    struct timespec start = {0}, end = {0};
    clock_gettime(CLOCK_MONOTONIC, &start);
    st = run_program(opts, &vm, program, counts);
    clock_gettime(CLOCK_MONOTONIC, &end);
    double secs = elapsed_sec(&start, &end);
    double mips = secs > 0.0 ? (double)vm.ticks / secs / 1e6 : 0.0;
    INFO("Finished with status: %s (total ticks=%" PRIu64 ", %.3fs, %.2f MIPS)",
         vm_status_str(st), vm.ticks, secs, mips);
    if (opts->profile) print_profile(counts, vm.ticks);

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
        cli_arg_new('p', "profile", "Report per-opcode execution counts", no_argument),
        NULL
    );

    RunOptions run_opts = {0};
    const char *path = NULL;
    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, NULL, "0xvm -f FILE [-t] [-D] [-p]",
                         "Run VMIM ROMs or assemble and run source files.\n"
                         "  -p reports per-opcode counts and MIPS to stderr.");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'v':
                tools_print_version("0xvm");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'f': path = optarg; break;
            case 't': run_opts.trace = true; break;
            case 'D': run_opts.debug = true; break;
            case 'p': run_opts.profile = true; break;
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
