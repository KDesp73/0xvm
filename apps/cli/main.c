#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asm.h"
#include "version.h"
#define CLI_IMPLEMENTATION
#include "io/cli.h"
#include "io/logging.h"
#include "vm.h"

typedef struct {
    const char *file;
    const char *output;
    bool trace;
} CliContext;

static CliContext ctx;

static bool read_file(const char *path, uint8_t **out, size_t *out_len)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        ERRO("Cannot open %s", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        ERRO("Cannot seek %s", path);
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size < 0) {
        ERRO("Cannot tell %s", path);
        fclose(file);
        return false;
    }
    rewind(file);

    uint8_t *buf = malloc((size_t)size + 1u);
    if (!buf) {
        ERRO("Out of memory reading %s", path);
        fclose(file);
        return false;
    }
    size_t got = fread(buf, 1u, (size_t)size, file);
    fclose(file);
    buf[got] = 0u; /* NUL terminator for the source path; ignored for ROMs */
    *out = buf;
    *out_len = got;
    return true;
}

static bool is_rom(const uint8_t *buf, size_t len)
{
    return len >= 4u && buf[0] == 0x56 && buf[1] == 0x4D &&
           buf[2] == 0x49 && buf[3] == 0x4D;
}

static bool assemble(const uint8_t *source, AsmProgram *program)
{
    if (asm_parse_string(program, (const char *)source) != 0) {
        fprintf(stderr, "Assembly failed at line %zu: %s\n",
                program->error_line, program->error);
        asm_program_free(program);
        return false;
    }
    return true;
}

static VmStatus load_source(VmState *vm, const uint8_t *source,
                            AsmProgram **out_program)
{
    AsmProgram *program = malloc(sizeof *program);
    if (!program) return VM_ERR_HEAP_OVERFLOW;
    if (!assemble(source, program)) {
        free(program);
        return VM_ERR_BAD_OP;
    }

    uint8_t rom[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
    size_t rom_len = 0;
    if (asm_make_rom(program, rom, sizeof rom, &rom_len) != 0) {
        asm_program_free(program);
        free(program);
        return VM_ERR_BAD_IP;
    }
    VmStatus st = vm_load_rom(vm, rom, rom_len);
    if (st != VM_OK) {
        asm_program_free(program);
        free(program);
        return st;
    }
    *out_program = program;
    return VM_OK;
}

/* With -t, log each instruction and its source line before executing it. */
static VmStatus run_program(const CliContext *c, VmState *vm,
                            const AsmProgram *program)
{
    if (!c->trace) return vm_run(vm, 0);

    for (;;) {
        if ((uint32_t)vm->ip >= VM_RAM_SIZE) return VM_ERR_BAD_IP;
        fprintf(stderr, "[trace addr=0x%04" PRIX16 " line=%zu op=%s sp=%u hp=0x%04" PRIX16 "]\n",
                vm->ip, asm_line_of(program, vm->ip),
                vm_opcode_name(vm->mem[vm->ip]), vm->sp, vm->hp);
        VmStatus st = vm_step(vm);
        if (st != VM_OK) return st;
    }
}

static bool run_handler(void *context)
{
    const CliContext *c = context;
    if (!c->file) { ERRO("Provide a file with -f/--file"); return false; }

    uint8_t *filebuf = NULL;
    size_t filelen = 0;
    if (!read_file(c->file, &filebuf, &filelen)) return false;

    VmState vm;
    vm_init(&vm);
    AsmProgram *program = NULL;
    VmStatus st;
    if (is_rom(filebuf, filelen)) {
        st = vm_load_rom(&vm, filebuf, filelen);
        if (st == VM_OK) INFO("Loaded ROM %s (%zu bytes)", c->file, filelen);
    } else {
        st = load_source(&vm, filebuf, &program);
        if (st == VM_OK)
            INFO("Loaded %s (%zu payload bytes)", c->file, program->len);
    }
    free(filebuf);
    if (st != VM_OK) {
        ERRO("Load failed: %s", vm_status_str(st));
        vm_free(&vm);
        return false;
    }

    st = run_program(c, &vm, program);
    INFO("Finished with status: %s (total ticks=%" PRIu64 ")",
         vm_status_str(st), vm.ticks);

    if (program) {
        asm_program_free(program);
        free(program);
    }
    vm_free(&vm);
    return st == VM_HALTED;
}

static char *default_output(const char *path)
{
    size_t n = strlen(path);
    const char *ext = ".rom";
    if (n >= 4u && strcmp(path + n - 4u, ".asm") == 0) path += n - 4u;
    size_t len = strlen(path) + strlen(ext) + 1u;
    char *out = malloc(len);
    if (!out) return NULL;
    snprintf(out, len, "%s%s", path, ext);
    return out;
}

static bool build_handler(void *context)
{
    const CliContext *c = context;
    if (!c->file) { ERRO("Provide a source file with -f/--file"); return false; }

    uint8_t *source = NULL;
    size_t len = 0;
    if (!read_file(c->file, &source, &len)) return false;
    if (is_rom(source, len)) {
        ERRO("%s is already a ROM image", c->file);
        free(source);
        return false;
    }

    AsmProgram program;
    if (!assemble(source, &program)) {
        free(source);
        return false;
    }
    free(source);

    char *out = c->output ? (char *)c->output : default_output(c->file);
    if (!out) { ERRO("Out of memory"); asm_program_free(&program); return false; }
    int rc = asm_write_rom(&program, out);
    if (rc != 0) ERRO("Cannot write ROM %s", out);
    else INFO("Wrote ROM %s (%zu bytes)", out,
              (size_t)VM_ROM_HEADER_SIZE + program.len);
    if (!c->output) free(out);
    asm_program_free(&program);
    return rc == 0;
}

int main(int argc, char **argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Prints this message", no_argument),
        cli_arg_new('v', "version", "Prints the program version", no_argument),
        cli_arg_new('f', "file", "Assembly source or VMIM ROM file", required_argument),
        cli_arg_new('o', "output", "ROM output path for `build`", required_argument),
        cli_arg_new('t', "trace", "Trace each instruction with its source line", no_argument),
        cli_arg_new('D', "debug", "Prefix program output with vm tick numbers", no_argument),
        NULL
    );

    Dispatcher dispatcher = {0};
    dispatcher_init(&dispatcher);
    dispatcher_add(&dispatcher, "run", "assemble/load and run a program", run_handler);
    dispatcher_add(&dispatcher, "build", "assemble a source file into a VMIM ROM", build_handler);

    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, &dispatcher,
                         "vm-cli [-t] [-D] run -f program.asm|program.rom\n"
                         "       vm-cli build -f program.asm [-o program.rom]",
                         "Written by KDesp73");
                goto cleanup;
            case 'v':
                printf("%s\n", VERSION_STRING);
                goto cleanup;
            case 'f':
                ctx.file = optarg;
                break;
            case 'o':
                ctx.output = optarg;
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

    if (argc <= 1) { ERRO("Please provide a command and/or argument"); goto cleanup; }
    char *cmd = argv[argc - 1];

    HandlerFunc handler = dispatcher_find(&dispatcher, cmd);
    if (!handler) { ERRO("No such command: %s", cmd); goto cleanup; }
    if (!handler(&ctx)) ERRO("Command `%s` failed", cmd);

cleanup:
    cli_args_free(&args);
    return 0;
}
