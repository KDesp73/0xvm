#ifndef APPS_COMMON_TOOLS_H
#define APPS_COMMON_TOOLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "asm.h"
#include "vm.h"

bool tools_read_file(const char *path, uint8_t **out, size_t *out_len);
bool tools_is_rom(const uint8_t *buf, size_t len);
size_t tools_rom_payload_len(const uint8_t *buf, size_t len);
bool tools_assemble_file(const char *path, AsmProgram *program);
bool tools_assemble_file_ex(const char *path, AsmProgram *program,
                            const char *const *include_dirs, size_t include_dir_count);
VmStatus tools_load_source(VmState *vm, const char *path,
                           AsmProgram **out_program);
char *tools_default_output(const char *path);
void tools_print_version(const char *tool);

#endif
