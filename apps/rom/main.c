// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>

#include "io/logging.h"
#include "tools.h"
#include "vm.h"
#define CLI_IMPLEMENTATION
#include "io/cli.h"

static int inspect(const char *path)
{
    uint8_t *buf = NULL;
    size_t len = 0u;
    if (!tools_read_file(path, &buf, &len)) return EXIT_FAILURE;

    VmState vm;
    vm_init(&vm);
    VmStatus st = vm_load_rom(&vm, buf, len);
    if (st != VM_OK) {
        ERRO("Invalid ROM %s: %s", path, vm_status_str(st));
        free(buf);
        vm_free(&vm);
        return EXIT_FAILURE;
    }

    uint16_t payload_len = (uint16_t)tools_rom_payload_len(buf, len);
    uint16_t entry = (uint16_t)(((uint16_t)buf[10] << 8) | (uint16_t)buf[11]);
    printf("ROM:       %s\n", path);
    printf("Magic:     VMIM\n");
    printf("Version:   %u\n", (unsigned)buf[4]);
    printf("Flags:     0x%02X\n", (unsigned)buf[5]);
    printf("Payload:   %u bytes\n", (unsigned)payload_len);
    printf("Entry:     0x%04X\n", (unsigned)entry);
    printf("File size: %zu bytes\n", len);

    free(buf);
    vm_free(&vm);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Print this help message", no_argument),
        cli_arg_new('v', "version", "Print the program version", no_argument),
        cli_arg_new('f', "file", "VMIM ROM file", required_argument),
        NULL
    );

    const char *path = NULL;
    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, NULL, "0xrom -f FILE",
                         "Inspect and validate a VMIM ROM header.");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'v':
                tools_print_version("0xrom");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'f': path = optarg; break;
            default: break;
        }
    }
    cli_args_free(&args);

    if (!path) {
        ERRO("Provide a ROM file with -f/--file");
        return EXIT_FAILURE;
    }
    return inspect(path);
}
