#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "asm.h"
#include "dis.h"
#include "io/logging.h"
#include "tools.h"
#include "vm.h"
#define CLI_IMPLEMENTATION
#include "io/cli.h"

static int disassemble(const char *path)
{
    uint8_t *filebuf = NULL;
    size_t filelen = 0u;
    if (!tools_read_file(path, &filebuf, &filelen)) return EXIT_FAILURE;

    VmState vm;
    vm_init(&vm);
    AsmProgram *program = NULL;
    VmStatus st;
    size_t payload_len = 0u;
    if (tools_is_rom(filebuf, filelen)) {
        st = vm_load_rom(&vm, filebuf, filelen);
        payload_len = tools_rom_payload_len(filebuf, filelen);
    } else {
        st = tools_load_source(&vm, path, &program);
        payload_len = program ? program->len : 0u;
    }
    free(filebuf);

    if (st != VM_OK) {
        ERRO("Load failed: %s", vm_status_str(st));
        if (program) { asm_program_free(program); free(program); }
        vm_free(&vm);
        return EXIT_FAILURE;
    }

    uint32_t end = (uint32_t)VM_IMAGE_START + (uint32_t)payload_len;
    uint32_t cur = vm.ip;
    bool ok = true;
    bool first = true;
    while (cur < end) {
        char line[VM_DIS_LINE_MAX];
        unsigned size = vm_disassemble_range(&vm.mem[0], (uint16_t)cur, end,
                                             line, sizeof line);
        if (size == 0u) {
            ERRO("Cannot decode instruction at 0x%04X (truncated payload or reserved opcode)",
                 (unsigned)cur);
            ok = false;
            break;
        }
        printf("%s\n", line);

        /* The entry JMP skips the assembler's static-data area. */
        if (first && size == 3u && vm.mem[cur] == OP_JMP) {
            uint32_t target = (uint32_t)(((uint16_t)vm.mem[cur + 1u] << 8) |
                                         (uint16_t)vm.mem[cur + 2u]);
            if (target >= end) {
                ERRO("Entry jump target 0x%04X is outside the payload",
                     (unsigned)target);
                ok = false;
                break;
            }
            cur = target;
        } else {
            cur += size;
        }
        first = false;
    }

    if (program) { asm_program_free(program); free(program); }
    vm_free(&vm);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Print this help message", no_argument),
        cli_arg_new('v', "version", "Print the program version", no_argument),
        cli_arg_new('f', "file", "Assembly source or VMIM ROM file", required_argument),
        NULL
    );

    const char *path = NULL;
    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, NULL, "0xdis -f FILE",
                         "Disassemble a VMIM ROM or assemble and disassemble source.");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'v':
                tools_print_version("0xdis");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'f': path = optarg; break;
            default: break;
        }
    }
    cli_args_free(&args);

    if (!path) {
        ERRO("Provide a file with -f/--file");
        return EXIT_FAILURE;
    }
    return disassemble(path);
}
