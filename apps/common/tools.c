// SPDX-License-Identifier: MIT
#include "tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "io/logging.h"
#include "version.h"

bool tools_read_file(const char *path, uint8_t **out, size_t *out_len)
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
    buf[got] = 0u; /* NUL terminator for source text; ignored for ROMs */
    *out = buf;
    *out_len = got;
    return true;
}

bool tools_is_rom(const uint8_t *buf, size_t len)
{
    return len >= 4u && buf[0] == 0x56 && buf[1] == 0x4D &&
           buf[2] == 0x49 && buf[3] == 0x4D;
}

size_t tools_rom_payload_len(const uint8_t *buf, size_t len)
{
    if (!tools_is_rom(buf, len) || len < VM_ROM_HEADER_SIZE) return 0u;
    return ((size_t)buf[8] << 8) | (size_t)buf[9];
}

bool tools_assemble_file_ex(const char *path, AsmProgram *program,
                            const char *const *include_dirs,
                            size_t include_dir_count)
{
    if (asm_parse_file_ex(program, path, include_dirs, include_dir_count) != 0) {
        ERRO("Assembly failed: %s", program->error);
        asm_program_free(program);
        return false;
    }
    return true;
}

bool tools_assemble_file(const char *path, AsmProgram *program)
{
    return tools_assemble_file_ex(path, program, NULL, 0u);
}

VmStatus tools_load_source(VmState *vm, const char *path,
                           AsmProgram **out_program)
{
    AsmProgram *program = malloc(sizeof *program);
    if (!program) return VM_ERR_HEAP_OVERFLOW;
    if (!tools_assemble_file(path, program)) {
        free(program);
        return VM_ERR_BAD_OP;
    }

    uint8_t rom[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
    size_t rom_len = 0u;
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

char *tools_default_output(const char *path)
{
    size_t n = strlen(path);
    size_t base = n >= 4u && strcmp(path + n - 4u, ".asm") == 0 ? n - 4u : n;
    char *out = malloc(base + 5u);
    if (!out) return NULL;
    snprintf(out, base + 5u, "%.*s.rom", (int)base, path);
    return out;
}

void tools_print_version(const char *tool)
{
    printf("%s %s (VM ABI v%u)\n", tool, VERSION_STRING, VM_ROM_VERSION);
}
