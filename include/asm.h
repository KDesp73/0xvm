#ifndef ASM_H
#define ASM_H

#include <stddef.h>

#include "vm.h"

/*
 * Parsed assembly program: a flat instruction array ready for vm_run().
 * Labels, constants, and jump targets are resolved during parsing. When the
 * source uses data directives (DB/DW/DQ/STR/ALIGN), the parser prepends an
 * initialization prologue that allocates heap space and writes those bytes;
 * programs without data directives produce exactly the instructions written.
 */
typedef struct {
    VmInst *insts;
    size_t count;
    size_t *lines;      /* source line for each instruction, parallel to insts */
    size_t error_line;
    char error[256];
} AsmProgram;

void asm_program_init(AsmProgram *prog);
void asm_program_free(AsmProgram *prog);

/* Both return 0 on success, -1 on failure (prog->error describes the problem). */
int asm_parse_string(AsmProgram *prog, const char *source);
int asm_parse_file(AsmProgram *prog, const char *path);

/* Source line of instruction `ip` (0 when unknown or out of range). */
size_t asm_line_of(const AsmProgram *prog, size_t ip);

#endif /* ASM_H */
