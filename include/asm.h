// SPDX-License-Identifier: MIT
#ifndef ASM_H
#define ASM_H

#include <stddef.h>
#include <stdint.h>

#include "vm.h"

/*
 * Assembled payload, loaded verbatim at VM_IMAGE_START.
 *
 * The payload always starts with a JMP over the static-data area:
 *
 *   offset 0:  JMP <code_start>   (entry point = VM_IMAGE_START)
 *   offset 3:  static data bytes from DB/DW/DQ/STR/ALIGN
 *   ...:       encoded instructions
 *
 * `lines` maps every payload byte to the assembly source line that produced it
 * (0 means "generated"). `files` maps those bytes to the source path; both are
 * parallel to `bytes`. Labels and jump targets resolve to absolute byte
 * addresses in VM RAM. This is source-level debug metadata: it is not encoded
 * in the ROM and does not change the VMIM ABI.
 */
typedef enum {
    ASM_SYM_LABEL = 0,
    ASM_SYM_CONST
} AsmSymbolKind;

typedef struct {
    char *name;
    int64_t value;     /* absolute address for labels, literal value for consts */
    size_t line;
    char *path;        /* NULL when assembled from a string */
    AsmSymbolKind kind;
} AsmSymbol;

typedef struct {
    uint8_t *bytes;
    size_t len;        /* <= VM_IMAGE_MAX */
    size_t entry;      /* payload offset of the entry instruction (usually 0) */
    size_t *lines;     /* source line per payload byte, parallel to bytes */
    char **files;      /* borrowed pointers into `sources`, parallel to bytes */
    char **sources;    /* owned unique source paths */
    size_t source_count;
    size_t source_cap;
    AsmSymbol *symbols;
    size_t symbol_count;
    size_t symbol_cap;
    size_t error_line;
    char error[256];
} AsmProgram;

void asm_program_init(AsmProgram *prog);
void asm_program_free(AsmProgram *prog);

/* Both return 0 on success, -1 on failure (prog->error describes the problem). */
int asm_parse_string(AsmProgram *prog, const char *source);
int asm_parse_file(AsmProgram *prog, const char *path);

/* Like asm_parse_file, with extra directories searched by INCLUDE after the
 * including file's own directory. The built-in stdlib directory is always
 * searched as well. The caller owns `include_dirs` for the duration of the call. */
int asm_parse_file_ex(AsmProgram *prog, const char *path,
                      const char *const *include_dirs, size_t include_dir_count);

/* Source line of the byte at absolute RAM address `addr` (0 when unknown). */
size_t asm_line_of(const AsmProgram *prog, uint16_t addr);

/* Source path of the byte at absolute RAM address `addr` (NULL when unknown).
 * The returned pointer is owned by `prog` and must not be freed. */
const char *asm_file_of(const AsmProgram *prog, uint16_t addr);

/* Debug-symbol lookup. Names are case-sensitive, as in the assembler. */
const AsmSymbol *asm_symbol_by_name(const AsmProgram *prog, const char *name);
const AsmSymbol *asm_symbol_at(const AsmProgram *prog, uint16_t addr);

/* Encode prog as a VMIM ROM. `rom_len` receives the exact size on success. */
int asm_make_rom(const AsmProgram *prog, uint8_t *rom, size_t rom_cap,
                 size_t *rom_len);

/* Write a VMIM ROM to `path`. Returns 0 on success, -1 on failure. */
int asm_write_rom(const AsmProgram *prog, const char *path);

#endif /* ASM_H */
