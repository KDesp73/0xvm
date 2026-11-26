#ifndef DB_H
#define DB_H

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "asm.h"
#include "dis.h"
#include "host.h"
#include "tools.h"
#include "vm.h"

#define DBG_MAX_BREAKS    64u
#define DBG_LINE_MAX      1024u
#define DBG_MAX_RUN_STEPS 100000000ULL

typedef enum {
    RUN_CONTINUE = 0,
    RUN_UNTIL,
    RUN_FINISH
} RunMode;

typedef struct {
    uint16_t addr;
    bool enabled;
    uint32_t hits;
} Breakpoint;

typedef struct {
    size_t asm_line;
    size_t lua_line;
    char *path;
} SourceMapEntry;

typedef struct {
    SourceMapEntry *entries;
    size_t count;
    size_t cap;
} SourceMap;

typedef struct {
    VmState vm;
    AsmProgram *program;       /* source symbols/lines, or a separate -s file */
    const char *program_path;
    uint8_t *image;            /* loaded payload, kept for `reset` */
    size_t image_len;
    uint16_t entry;
    uint32_t image_end;        /* exclusive absolute image limit */
    FILE *cmd;
    FILE *program_input;
    FILE *out;
    bool interactive;
    bool quit;
    VmStatus status;
    SourceMap source_map;      /* optional Lua source-line sidecar */
    Breakpoint breaks[DBG_MAX_BREAKS];
    size_t break_count;
} Debugger;

/* util.c - output and parsing helpers */
void dbg_out(Debugger *db, const char *fmt, ...);
void dbg_err(const char *fmt, ...);
bool parse_i64(const char *s, int64_t *out);
uint16_t read_be16(const uint8_t *p);

/* linemap.c - optional Lua source-line sidecar */
bool source_map_load(SourceMap *map, const char *path);
void source_map_free(SourceMap *map);
bool source_map_lookup(const SourceMap *map, size_t asm_line,
                       const char **path, size_t *line);
bool source_map_find(const SourceMap *map, const char *path, size_t line,
                     size_t *asm_line);
bool source_map_address(const Debugger *db, uint16_t addr, const char **path,
                        size_t *line);

/* resolve.c - address and source-line expressions */
bool resolve_expr(Debugger *db, const char *expr, uint16_t *out);
bool resolve_line_ref(Debugger *db, const char *text, uint16_t *out);
bool resolve_break_arg(Debugger *db, const char *arg, uint16_t *out);

/* breakpoints.c */
void print_breakpoints(Debugger *db);
void add_breakpoint(Debugger *db, const char *arg);
void set_break_state(Debugger *db, const char *arg, bool enabled);
void delete_breakpoint(Debugger *db, const char *arg);
bool hit_breakpoint(Debugger *db);

/* inspect.c - state display commands */
void print_current(Debugger *db);
void print_registers(Debugger *db);
void print_stack(Debugger *db, const char *arg);
void print_locals(Debugger *db, const char *arg);
void print_frames(Debugger *db);
void print_heap(Debugger *db);
void print_symbols(Debugger *db, const char *filter);
void list_cmd(Debugger *db, const char *arg);
void disassemble_cmd(Debugger *db, const char *arg);
void examine_cmd(Debugger *db, const char *arg);

/* execute.c - run control */
void step_cmd(Debugger *db, const char *arg);
void next_cmd(Debugger *db);
void finish_cmd(Debugger *db);
void continue_cmd(Debugger *db);
void until_cmd(Debugger *db, const char *arg);
void reset_cmd(Debugger *db);

/* commands.c - command dispatch */
void print_help(Debugger *db);
void execute_line(Debugger *db, char *line);

#endif /* DB_H */
