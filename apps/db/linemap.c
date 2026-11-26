// SPDX-License-Identifier: MIT
#include "db.h"

/* The compiler's `.lumap` sidecar maps generated assembly lines back to the
 * Lua source that produced them. ROMs do not carry this information, so the
 * debugger loads it separately and layers it over the assembler's metadata. */

static const char *path_base(const char *path)
{
    if (!path) return NULL;
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1u : path;
}

static bool path_matches(const char *actual, const char *wanted)
{
    if (!actual || !wanted) return false;
    return strcmp(actual, wanted) == 0 ||
           strcmp(path_base(actual), path_base(wanted)) == 0;
}

static bool parse_number(const char *text, size_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE || value == 0u)
        return false;
    *out = (size_t)value;
    return true;
}

static bool append_entry(SourceMap *map, size_t asm_line, char *path,
                         size_t lua_line)
{
    if (map->count == map->cap) {
        size_t cap = map->cap ? map->cap * 2u : 64u;
        SourceMapEntry *tmp = realloc(map->entries, cap * sizeof *tmp);
        if (!tmp) return false;
        map->entries = tmp;
        map->cap = cap;
    }
    map->entries[map->count].asm_line = asm_line;
    map->entries[map->count].lua_line = lua_line;
    map->entries[map->count].path = path;
    map->count++;
    return true;
}

bool source_map_load(SourceMap *map, const char *path)
{
    if (!map || !path) return false;
    FILE *file = fopen(path, "rb");
    if (!file) {
        dbg_err("cannot open source map '%s'", path);
        return false;
    }

    char line[2048];
    bool ok = true;
    size_t line_no = 0u;
    while (fgets(line, sizeof line, file)) {
        line_no++;
        size_t len = strlen(line);
        while (len > 0u && (line[len - 1u] == '\n' || line[len - 1u] == '\r'))
            line[--len] = '\0';
        if (len == 0u || line[0] == '#') continue;

        char *save = NULL;
        char *asm_text = strtok_r(line, "\t", &save);
        char *source = strtok_r(NULL, "\t", &save);
        char *lua_text = strtok_r(NULL, "\t", &save);
        size_t asm_line = 0u, lua_line = 0u;
        if (!asm_text || !source || !*source || !lua_text ||
            !parse_number(asm_text, &asm_line) ||
            !parse_number(lua_text, &lua_line)) {
            dbg_err("invalid source map '%s' on line %zu", path, line_no);
            ok = false;
            break;
        }

        char *copy = malloc(strlen(source) + 1u);
        if (!copy || !append_entry(map, asm_line, copy, lua_line)) {
            free(copy);
            dbg_err("out of memory loading source map '%s'", path);
            ok = false;
            break;
        }
        strcpy(copy, source);
    }

    fclose(file);
    if (!ok) source_map_free(map);
    return ok;
}

void source_map_free(SourceMap *map)
{
    if (!map) return;
    for (size_t i = 0u; i < map->count; i++) free(map->entries[i].path);
    free(map->entries);
    map->entries = NULL;
    map->count = 0u;
    map->cap = 0u;
}

bool source_map_lookup(const SourceMap *map, size_t asm_line,
                       const char **path, size_t *line)
{
    if (!map || !asm_line) return false;
    for (size_t i = 0u; i < map->count; i++) {
        if (map->entries[i].asm_line == asm_line) {
            if (path) *path = map->entries[i].path;
            if (line) *line = map->entries[i].lua_line;
            return true;
        }
    }
    return false;
}

bool source_map_find(const SourceMap *map, const char *wanted, size_t line,
                     size_t *asm_line)
{
    if (!map || !wanted || !line) return false;
    for (size_t i = 0u; i < map->count; i++) {
        if (map->entries[i].lua_line == line &&
            path_matches(map->entries[i].path, wanted)) {
            if (asm_line) *asm_line = map->entries[i].asm_line;
            return true;
        }
    }
    return false;
}

bool source_map_address(const Debugger *db, uint16_t addr, const char **path,
                        size_t *line)
{
    if (!db || !db->program) {
        if (path) *path = NULL;
        if (line) *line = 0u;
        return false;
    }

    size_t asm_line = asm_line_of(db->program, addr);
    const char *mapped_path = NULL;
    size_t mapped_line = 0u;
    if (asm_line && source_map_lookup(&db->source_map, asm_line,
                                      &mapped_path, &mapped_line)) {
        if (path) *path = mapped_path;
        if (line) *line = mapped_line;
        return true;
    }
    if (path) *path = asm_file_of(db->program, addr);
    if (line) *line = asm_line;
    return asm_line != 0u;
}
