#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include "asm.h"
#include "version.h"
#define CLI_IMPLEMENTATION
#include "io/cli.h"
#include "io/logging.h"
#include "vm.h"

typedef struct {
    const char* file;
    bool trace;
} CliContext;

static CliContext ctx;

static bool load_program(const CliContext *c, AsmProgram *program)
{
    if (!c->file) {
        ERRO("Provide a source file with -f/--file");
        return false;
    }
    if (asm_parse_file(program, c->file) != 0) {
        fprintf(stderr, "Failed to parse %s:%zu: %s\n",
                c->file, program->error_line, program->error);
        asm_program_free(program);
        return false;
    }
    INFO("Loaded %s (%zu instructions)", c->file, program->count);
    return true;
}

/* With -t, log each instruction and its source line before executing it. */
static VmStatus run_program(const CliContext *c, VmState *vm,
                            const AsmProgram *program)
{
    if (!c->trace) return vm_run(vm, program->insts, program->count, 0);

    for (;;) {
        if (vm->ip >= program->count) return VM_ERR_BAD_IP;
        fprintf(stderr, "[trace ip=%u line=%zu op=%s sp=%u hp=%" PRIu64 "]\n",
                vm->ip, asm_line_of(program, vm->ip),
                vm_opcode_name(program->insts[vm->ip].op), vm->sp, vm->hp);
        VmStatus st = vm_step(vm, program->insts, program->count);
        if (st != VM_OK) return st;
    }
}

static bool run_handler(void* context)
{
    CliContext ctx = *(CliContext*)(context);
    AsmProgram program;
    if (!load_program(&ctx, &program)) return false;

    VmState vm;
    vm_init(&vm);
    VmStatus st = run_program(&ctx, &vm, &program);
    INFO("Finished with status: %s (total ticks=%u)", vm_status_str(st), vm.ticks);
    vm_free(&vm);
    asm_program_free(&program);
    return st == VM_HALTED;
}

int main(int argc, char** argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Prints this message", no_argument),
        cli_arg_new('v', "version", "Prints the program version", no_argument),
        cli_arg_new('f', "file", "Assembly source file to run", required_argument),
        cli_arg_new('t', "trace", "Trace each instruction with its source line", no_argument),
        cli_arg_new('D', "debug", "Prefix program output with vm tick numbers", no_argument),
        NULL
    );

    Dispatcher dispatcher = {0};
    dispatcher_init(&dispatcher);
    dispatcher_add(&dispatcher, "run", "run a program locally", run_handler);

    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, &dispatcher, "", "Written by KDesp73");
                goto cleanup;
            case 'v':
                printf("%s\n", VERSION_STRING);
                goto cleanup;
            case 'f':
                ctx.file = optarg;
                break;
            case 't':
                ctx.trace = true;
                break;
            case 'D':
                vm_set_debug_ticks(1);
                break;
            default:
                break;
        }
    }

    if (argc <= 1) { ERRO("Please provide a command and/or argument"); exit(0); }
    char* cmd = argv[argc-1];

    HandlerFunc handler = dispatcher_find(&dispatcher, cmd);
    if (!handler) { ERRO("No such command: %s", cmd); exit(0); }
    if (!handler(&ctx)) { ERRO("Command `%s` failed", cmd); exit(0); }

cleanup:
    cli_args_free(&args);
    return 0;
}
