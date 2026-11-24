#include <stdio.h>

#include "tests.h"

int main(void)
{
    int failures = test_asm() + test_rom() + test_vm();

    if (failures) {
        fprintf(stderr, "%d test check(s) failed\n", failures);
        return 1;
    }

    printf("tests passed!\n");
    return 0;
}
