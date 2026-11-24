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

static int assemble(const char *path, const char *output)
{
    uint8_t *source = NULL;
    size_t len = 0u;
    if (!tools_read_file(path, &source, &len)) return EXIT_FAILURE;
    if (tools_is_rom(source, len)) {
        ERRO("%s is already a ROM image", path);
        free(source);
        return EXIT_FAILURE;
    }
    free(source);

    AsmProgram program;
    if (!tools_assemble_file(path, &program)) return EXIT_FAILURE;

    char *out = output ? (char *)output : tools_default_output(path);
    if (!out) {
        ERRO("Out of memory");
        asm_program_free(&program);
        return EXIT_FAILURE;
    }
    int rc = asm_write_rom(&program, out);
    if (rc != 0) ERRO("Cannot write ROM %s", out);
    else INFO("Wrote ROM %s (%zu bytes)", out,
              (size_t)VM_ROM_HEADER_SIZE + program.len);

    if (!output) free(out);
    asm_program_free(&program);
    return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Print this help message", no_argument),
        cli_arg_new('v', "version", "Print the program version", no_argument),
        cli_arg_new('f', "file", "Assembly source file", required_argument),
        cli_arg_new('o', "output", "ROM output path (default: replace .asm with .rom)", required_argument),
        NULL
    );

    const char *path = NULL;
    const char *output = NULL;
    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, NULL, "0xasm -f FILE [-o ROM]",
                         "Assemble source into a VMIM ROM.");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'v':
                tools_print_version("0xasm");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'f': path = optarg; break;
            case 'o': output = optarg; break;
            default: break;
        }
    }
    cli_args_free(&args);

    if (!path) {
        ERRO("Provide a source file with -f/--file");
        return EXIT_FAILURE;
    }
    return assemble(path, output);
}
