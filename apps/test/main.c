#include <stdio.h>
#include <stdlib.h>

#include "tests.h"
#include "tools.h"
#define CLI_IMPLEMENTATION
#include "io/cli.h"

int main(int argc, char **argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Print this help message", no_argument),
        cli_arg_new('v', "version", "Print the program version", no_argument),
        NULL
    );

    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, NULL, "0xtest", "Run the VM test suite.");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            case 'v':
                tools_print_version("0xtest");
                cli_args_free(&args);
                return EXIT_SUCCESS;
            default: break;
        }
    }
    cli_args_free(&args);

    int failures = test_asm() + test_rom() + test_device() +
                   test_conformance() + test_dis() + test_vm();

    if (failures) {
        fprintf(stderr, "%d test check(s) failed\n", failures);
        return EXIT_FAILURE;
    }

    printf("tests passed!\n");
    return EXIT_SUCCESS;
}
