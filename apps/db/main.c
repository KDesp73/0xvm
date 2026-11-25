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
#define CLI_IMPLEMENTATION
#include "io/cli.h"

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
    Breakpoint breaks[DBG_MAX_BREAKS];
    size_t break_count;
} Debugger;

/* ------------------------------------------------------------------ output */

static void dbg_out(Debugger *db, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(db->out, fmt, ap);
    va_end(ap);
}

static void dbg_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[ERRO] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

static char *trim(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static bool parse_i64(const char *s, int64_t *out)
{
    if (!s || !*s) return false;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
        if (!*s) return false;
    }
    errno = 0;
    char *end = NULL;
    long long value = strtoll(s, &end, base);
    if (end == s || *end != '\0' || errno == ERANGE) return false;
    *out = (int64_t)value;
    return true;
}

static uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/* --------------------------------------------------------- address resolver */

/* Symbols are case-sensitive, like labels in the assembler. An expression may
 * be a label/constant, a number, `$`, or any of those plus/minus an offset. */
static bool resolve_expr(Debugger *db, const char *expr, uint16_t *out)
{
    if (!expr || !*expr) return false;
    if (strcmp(expr, "$") == 0) {
        *out = db->vm.ip;
        return true;
    }

    const char *op = NULL;
    for (const char *p = expr + 1u; *p; p++) {
        if (*p == '+' || *p == '-') { op = p; break; }
    }
    if (op) {
        size_t n = (size_t)(op - expr);
        char *base = malloc(n + 1u);
        int64_t offset = 0;
        if (!base || !parse_i64(op + 1u, &offset)) {
            free(base);
            return false;
        }
        memcpy(base, expr, n);
        base[n] = '\0';
        bool ok = resolve_expr(db, base, out);
        free(base);
        if (!ok) return false;
        int64_t value = (int64_t)*out + offset;
        if (value < 0 || value > UINT16_MAX) return false;
        *out = (uint16_t)value;
        return true;
    }

    int64_t value = 0;
    if ((expr[0] == '0' && (expr[1] == 'x' || expr[1] == 'X')) ||
        isdigit((unsigned char)expr[0]) || expr[0] == '-') {
        if (!parse_i64(expr, &value) || value < 0 || value > UINT16_MAX)
            return false;
        *out = (uint16_t)value;
        return true;
    }

    const AsmSymbol *sym = db->program ? asm_symbol_by_name(db->program, expr) : NULL;
    if (!sym || sym->value < 0 || sym->value > UINT16_MAX) return false;
    *out = (uint16_t)sym->value;
    return true;
}

static uint32_t code_start(const Debugger *db)
{
    uint32_t start = (uint32_t)VM_IMAGE_START;
    if (db->image_end >= start + 3u && db->vm.mem[start] == (uint8_t)OP_JMP) {
        uint32_t target = (uint32_t)read_be16(db->vm.mem + start + 1u);
        if (target >= start && target < db->image_end) return target;
    }
    return start;
}

static const char *path_base(const char *path)
{
    if (!path) return NULL;
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1u : path;
}

static bool path_matches(const char *actual, const char *wanted)
{
    if (!actual || !wanted) return false;
    if (strcmp(actual, wanted) == 0) return true;
    return strcmp(path_base(actual), path_base(wanted)) == 0;
}

/* `40` selects source line 40; `file.asm:40` disambiguates includes. */
static bool resolve_line_ref(Debugger *db, const char *text, uint16_t *out)
{
    if (!db->program || !text || !*text) return false;
    char buf[256];
    if (strlen(text) >= sizeof buf) return false;
    strcpy(buf, text);

    char *colon = strrchr(buf, ':');
    const char *wanted = NULL;
    char *number = buf;
    if (colon) {
        *colon = '\0';
        wanted = colon == buf ? NULL : buf;
        number = colon + 1u;
    }
    char *end = NULL;
    errno = 0;
    unsigned long line = strtoul(number, &end, 10);
    if (end == number || *end != '\0' || errno == ERANGE || line == 0u)
        return false;
    if (!wanted) wanted = asm_file_of(db->program, db->vm.ip);

    uint32_t cur = code_start(db);
    while (cur < db->image_end && cur <= UINT16_MAX) {
        char dis[VM_DIS_LINE_MAX];
        unsigned size = vm_disassemble_range(db->vm.mem, (uint16_t)cur,
                                             db->image_end, dis, sizeof dis);
        if (size == 0u) break;
        const char *path = asm_file_of(db->program, (uint16_t)cur);
        if (asm_line_of(db->program, (uint16_t)cur) == (size_t)line &&
            (!wanted || path_matches(path, wanted))) {
            *out = (uint16_t)cur;
            return true;
        }
        cur += size;
    }
    return false;
}

static bool resolve_break_arg(Debugger *db, const char *arg, uint16_t *out)
{
    if (!arg || !*arg) return false;
    if (*arg == '*') return resolve_expr(db, arg + 1u, out);
    if (arg[0] == '0' && (arg[1] == 'x' || arg[1] == 'X'))
        return resolve_expr(db, arg, out);
    if (strchr(arg, '+') || strchr(arg, '-')) {
        const AsmSymbol *sym = db->program
            ? asm_symbol_by_name(db->program, arg) : NULL;
        if (sym) return resolve_expr(db, arg, out);
    }
    return resolve_line_ref(db, arg, out);
}

/* ------------------------------------------------------------ breakpoints */

static size_t find_break(const Debugger *db, uint16_t addr)
{
    for (size_t i = 0u; i < db->break_count; i++) {
        if (db->breaks[i].addr == addr) return i;
    }
    return db->break_count;
}

static void print_breakpoints(Debugger *db)
{
    if (db->break_count == 0u) {
        dbg_out(db, "No breakpoints.\n");
        return;
    }
    dbg_out(db, "Num  Address  State   Hits  Source\n");
    for (size_t i = 0u; i < db->break_count; i++) {
        const Breakpoint *bp = &db->breaks[i];
        const AsmSymbol *sym = asm_symbol_at(db->program, bp->addr);
        const char *path = asm_file_of(db->program, bp->addr);
        size_t line = asm_line_of(db->program, bp->addr);
        dbg_out(db, "%-4zu 0x%04X  %-6s %-5" PRIu32 " ", i, (unsigned)bp->addr,
                bp->enabled ? "enabled" : "disabled", bp->hits);
        if (sym) dbg_out(db, "<%s> ", sym->name);
        if (path && line) dbg_out(db, "%s:%zu", path, line);
        dbg_out(db, "\n");
    }
}

static void add_breakpoint(Debugger *db, const char *arg)
{
    if (!arg || !*arg) {
        dbg_err("break requires an address, label, or source line");
        return;
    }
    if (db->break_count >= DBG_MAX_BREAKS) {
        dbg_err("too many breakpoints (max %u)", DBG_MAX_BREAKS);
        return;
    }

    uint16_t addr;
    const AsmSymbol *sym = db->program ? asm_symbol_by_name(db->program, arg) : NULL;
    bool ok = sym ? resolve_expr(db, arg, &addr) : resolve_break_arg(db, arg, &addr);
    if (!ok) {
        dbg_err("cannot resolve breakpoint '%s'", arg);
        return;
    }
    size_t index = find_break(db, addr);
    if (index != db->break_count) {
        dbg_out(db, "Breakpoint %zu already set at 0x%04X.\n", index,
                (unsigned)addr);
        return;
    }
    Breakpoint *bp = &db->breaks[db->break_count++];
    bp->addr = addr;
    bp->enabled = true;
    bp->hits = 0u;
    dbg_out(db, "Breakpoint %zu set at 0x%04X.\n", db->break_count - 1u,
            (unsigned)addr);
}

static void set_break_state(Debugger *db, const char *arg, bool enabled)
{
    if (!arg || !*arg) {
        dbg_err("expected a breakpoint number or 'all'");
        return;
    }
    if (strcmp(arg, "all") == 0) {
        for (size_t i = 0u; i < db->break_count; i++)
            db->breaks[i].enabled = enabled;
        print_breakpoints(db);
        return;
    }
    int64_t index;
    if (!parse_i64(arg, &index) || index < 0 ||
        (size_t)index >= db->break_count) {
        dbg_err("unknown breakpoint '%s'", arg);
        return;
    }
    db->breaks[(size_t)index].enabled = enabled;
    print_breakpoints(db);
}

static void delete_breakpoint(Debugger *db, const char *arg)
{
    if (!arg || !*arg) {
        dbg_err("delete requires a number, address, label, or 'all'");
        return;
    }
    if (strcmp(arg, "all") == 0) {
        db->break_count = 0u;
        dbg_out(db, "All breakpoints deleted.\n");
        return;
    }
    size_t index = db->break_count;
    int64_t number;
    if (parse_i64(arg, &number) && number >= 0 &&
        (size_t)number < db->break_count) {
        index = (size_t)number;
    } else {
        uint16_t addr;
        if (resolve_break_arg(db, arg, &addr)) index = find_break(db, addr);
    }
    if (index == db->break_count) {
        dbg_err("unknown breakpoint '%s'", arg);
        return;
    }
    memmove(&db->breaks[index], &db->breaks[index + 1u],
            (db->break_count - index - 1u) * sizeof *db->breaks);
    db->break_count--;
    dbg_out(db, "Breakpoint %zu deleted.\n", index);
}

static bool hit_breakpoint(Debugger *db)
{
    for (size_t i = 0u; i < db->break_count; i++) {
        Breakpoint *bp = &db->breaks[i];
        if (bp->enabled && bp->addr == db->vm.ip) {
            bp->hits++;
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------ inspection */

static void print_current(Debugger *db)
{
    char dis[VM_DIS_LINE_MAX];
    uint32_t end = db->image_end < VM_RAM_SIZE ? db->image_end : VM_RAM_SIZE;
    if (db->vm.ip >= end ||
        vm_disassemble_range(db->vm.mem, db->vm.ip, end, dis, sizeof dis) == 0u) {
        dbg_out(db, "-> 0x%04X: <outside image>\n", (unsigned)db->vm.ip);
        return;
    }
    dbg_out(db, "-> %s", dis);
    const AsmSymbol *sym = asm_symbol_at(db->program, db->vm.ip);
    const char *path = asm_file_of(db->program, db->vm.ip);
    size_t line = asm_line_of(db->program, db->vm.ip);
    if (sym) dbg_out(db, "  <%s>", sym->name);
    if (path && line) dbg_out(db, "  %s:%zu", path, line);
    else if (line) dbg_out(db, "  line %zu", line);
    dbg_out(db, "\n");
}

static void stopped(Debugger *db, const char *reason)
{
    dbg_out(db, "Stopped: %s (ticks=%" PRIu64 ")\n", reason, db->vm.ticks);
    print_current(db);
}

static void print_registers(Debugger *db)
{
    dbg_out(db,
            "ip=0x%04X sp=%u fp=%u fsp=%u hp=0x%04X ticks=%" PRIu64
            " status=%s\n",
            (unsigned)db->vm.ip, (unsigned)db->vm.sp, (unsigned)db->vm.fp,
            (unsigned)db->vm.fsp, (unsigned)db->vm.hp, db->vm.ticks,
            vm_status_str(db->status));
    print_current(db);
}

static void print_stack(Debugger *db, const char *arg)
{
    unsigned long count = 8u;
    if (arg && *arg) {
        char *end = NULL;
        count = strtoul(arg, &end, 10);
        if (end == arg || *end != '\0') {
            dbg_err("invalid stack count '%s'", arg);
            return;
        }
    }
    if (count > db->vm.sp) count = db->vm.sp;
    if (count == 0u) {
        dbg_out(db, "Stack is empty.\n");
        return;
    }
    for (unsigned long i = 0u; i < count; i++) {
        uint16_t index = (uint16_t)(db->vm.sp - 1u - (unsigned)i);
        int64_t value = db->vm.stack[index];
        dbg_out(db, "%s stack[%u]=%20" PRId64 "  0x%016" PRIX64 "\n",
                i == 0u ? "top" : "   ", (unsigned)index, value,
                (uint64_t)value);
    }
}

static void print_locals(Debugger *db, const char *arg)
{
    if (db->vm.fsp == 0u) {
        dbg_out(db, "No active frame.\n");
        return;
    }
    unsigned long count = 8u;
    if (arg && *arg) {
        char *end = NULL;
        count = strtoul(arg, &end, 10);
        if (end == arg || *end != '\0') {
            dbg_err("invalid local count '%s'", arg);
            return;
        }
    }
    unsigned long available = db->vm.sp > db->vm.fp
        ? (unsigned long)(db->vm.sp - db->vm.fp) : 0u;
    if (count > available) count = available;
    if (count == 0u) {
        dbg_out(db, "No locals in the current frame.\n");
        return;
    }
    for (unsigned long i = 0u; i < count; i++) {
        uint16_t index = (uint16_t)(db->vm.fp + (unsigned)i);
        int64_t value = db->vm.stack[index];
        dbg_out(db, "L+%-2lu stack[%u]=%20" PRId64 "\n", i,
                (unsigned)index, value);
    }
}

static void print_frames(Debugger *db)
{
    if (db->vm.fsp == 0u) {
        dbg_out(db, "No call frames.\n");
        return;
    }
    for (uint16_t i = 0u; i < db->vm.fsp && i < VM_FRAME_COUNT; i++) {
        const VmFrame *frame = &db->vm.frames[i];
        dbg_out(db, "#%u ret=0x%04X caller_fp=%u\n", (unsigned)i,
                (unsigned)frame->ret_ip, (unsigned)frame->prev_fp);
    }
}

static void print_heap(Debugger *db)
{
    if (db->vm.block_count == 0u) {
        dbg_out(db, "Heap is empty.\n");
        return;
    }
    dbg_out(db, "Num  Address  Size  State\n");
    for (uint16_t i = 0u; i < db->vm.block_count && i < VM_BLOCK_COUNT; i++) {
        const VmBlock *block = &db->vm.blocks[i];
        dbg_out(db, "%-4u 0x%04X  %-5u %s\n", (unsigned)i,
                (unsigned)block->addr, (unsigned)block->size,
                block->live ? "live" : "free");
    }
}

static void print_symbols(Debugger *db, const char *filter)
{
    if (!db->program || db->program->symbol_count == 0u) {
        dbg_out(db, "No source symbols loaded.\n");
        return;
    }
    size_t shown = 0u;
    for (size_t i = 0u; i < db->program->symbol_count; i++) {
        const AsmSymbol *sym = &db->program->symbols[i];
        if (filter && *filter && strstr(sym->name, filter) == NULL) continue;
        if (sym->kind == ASM_SYM_LABEL) {
            dbg_out(db, "label %-24s 0x%04" PRIX64, sym->name,
                    (uint64_t)sym->value);
        } else {
            dbg_out(db, "const %-24s %20" PRId64, sym->name, sym->value);
        }
        if (sym->path && sym->line) dbg_out(db, "  %s:%zu", sym->path, sym->line);
        dbg_out(db, "\n");
        shown++;
    }
    if (shown == 0u) dbg_out(db, "No symbols match '%s'.\n", filter);
}

static void print_source_range(Debugger *db, const char *path, size_t line,
                               size_t context)
{
    if (!path || !*path || path[0] == '<') {
        dbg_out(db, "Source is not available for this address.\n");
        return;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        dbg_err("cannot open source '%s'", path);
        return;
    }
    size_t current = 0u;
    char buf[512];
    size_t from = line > context ? line - context : 1u;
    size_t to = line + context;
    while (fgets(buf, sizeof buf, file)) {
        current++;
        if (current < from || current > to) continue;
        size_t len = strlen(buf);
        while (len > 0u && (buf[len - 1u] == '\n' || buf[len - 1u] == '\r'))
            buf[--len] = '\0';
        dbg_out(db, "%s %4zu | %s\n", current == line ? "->" : "  ",
                current, buf);
    }
    fclose(file);
}

static void list_cmd(Debugger *db, const char *arg)
{
    if (!db->program) {
        dbg_out(db, "Source mapping requires an assembly file (use -s with a ROM).\n");
        return;
    }
    const char *path = asm_file_of(db->program, db->vm.ip);
    size_t line = asm_line_of(db->program, db->vm.ip);
    if (arg && *arg) {
        int64_t value;
        if (parse_i64(arg, &value)) {
            if (value <= 0) {
                dbg_err("line numbers start at 1");
                return;
            }
            line = (size_t)value;
        } else {
            uint16_t addr;
            if (!resolve_expr(db, arg, &addr) && !resolve_line_ref(db, arg, &addr)) {
                dbg_err("cannot resolve '%s'", arg);
                return;
            }
            path = asm_file_of(db->program, addr);
            line = asm_line_of(db->program, addr);
        }
    }
    if (line == 0u) {
        dbg_out(db, "No source line is mapped to this address.\n");
        return;
    }
    print_source_range(db, path, line, 5u);
}

static void disassemble_cmd(Debugger *db, const char *arg)
{
    uint16_t addr = db->vm.ip;
    unsigned long count = 12u;
    char buf[256];
    if (arg && *arg) {
        if (strlen(arg) >= sizeof buf) {
            dbg_err("disassemble arguments are too long");
            return;
        }
        strcpy(buf, arg);
        char *save = NULL;
        char *first = strtok_r(buf, " \t", &save);
        char *second = strtok_r(NULL, " \t", &save);
        if (!resolve_expr(db, first, &addr)) {
            dbg_err("cannot resolve '%s'", first);
            return;
        }
        if (second) {
            char *end = NULL;
            count = strtoul(second, &end, 10);
            if (end == second || *end != '\0') {
                dbg_err("invalid instruction count '%s'", second);
                return;
            }
        }
    }

    uint32_t end = db->image_end < VM_RAM_SIZE ? db->image_end : VM_RAM_SIZE;
    for (unsigned long i = 0u; i < count && addr < end; i++) {
        char dis[VM_DIS_LINE_MAX];
        unsigned size = vm_disassemble_range(db->vm.mem, addr, end,
                                             dis, sizeof dis);
        if (size == 0u) {
            dbg_err("cannot decode instruction at 0x%04X", (unsigned)addr);
            return;
        }
        dbg_out(db, "%s", dis);
        const AsmSymbol *sym = asm_symbol_at(db->program, addr);
        const char *path = asm_file_of(db->program, addr);
        size_t line = asm_line_of(db->program, addr);
        if (sym) dbg_out(db, "  <%s>", sym->name);
        if (path && line) dbg_out(db, "  %s:%zu", path, line);
        dbg_out(db, "\n");
        addr = (uint16_t)(addr + size);
    }
}

static uint64_t mem_read(const uint8_t *mem, uint16_t addr, unsigned width)
{
    uint64_t value = 0u;
    for (unsigned i = 0u; i < width; i++)
        value = (value << 8) | (uint64_t)mem[addr + i];
    return value;
}

static void examine_cmd(Debugger *db, const char *arg)
{
    if (!arg || !*arg) {
        dbg_err("examine requires an address expression");
        return;
    }
    char buf[256];
    if (strlen(arg) >= sizeof buf) {
        dbg_err("examine arguments are too long");
        return;
    }
    strcpy(buf, arg);
    char *save = NULL;
    char *first = strtok_r(buf, " \t", &save);

    if (strcmp(first, "x") == 0 || strcmp(first, "examine") == 0)
        first = strtok_r(NULL, " \t", &save);
    else if (strncmp(first, "x/", 2u) == 0)
        memmove(first, first + 1u, strlen(first)); /* "x/32" -> "/32" */

    unsigned width = 1u;
    if (first && first[0] == '/') {
        if (strcmp(first, "/8") == 0) width = 1u;
        else if (strcmp(first, "/16") == 0) width = 2u;
        else if (strcmp(first, "/32") == 0) width = 4u;
        else if (strcmp(first, "/64") == 0) width = 8u;
        else {
            dbg_err("use /8, /16, /32, or /64");
            return;
        }
        first = strtok_r(NULL, " \t", &save);
        if (!first) {
            dbg_err("examine requires an address expression");
            return;
        }
    }

    uint16_t addr;
    if (!resolve_expr(db, first, &addr)) {
        dbg_err("cannot resolve '%s'", first);
        return;
    }
    char *count_text = strtok_r(NULL, " \t", &save);
    unsigned long count = width == 1u ? 16u : 8u;
    if (count_text) {
        char *end = NULL;
        count = strtoul(count_text, &end, 10);
        if (end == count_text || *end != '\0' || count == 0u) {
            dbg_err("invalid examine count '%s'", count_text);
            return;
        }
    }

    if (width == 1u) {
        char ascii[17];
        size_t ascii_len = 0u;
        for (unsigned long i = 0u; i < count; i++) {
            uint32_t at = (uint32_t)addr + i;
            if (at >= VM_RAM_SIZE) break;
            if (i % 16u == 0u) dbg_out(db, "0x%04X: ", (unsigned)at);
            uint8_t byte = db->vm.mem[at];
            dbg_out(db, "%02X ", (unsigned)byte);
            ascii[ascii_len++] = isprint(byte) ? (char)byte : '.';
            if (i % 16u == 15u || at + 1u >= VM_RAM_SIZE ||
                i + 1u == count) {
                ascii[ascii_len] = '\0';
                dbg_out(db, " |%s|\n", ascii);
                ascii_len = 0u;
            }
        }
        return;
    }

    for (unsigned long i = 0u; i < count; i++) {
        uint32_t at = (uint32_t)addr + (unsigned long)i * width;
        if (at + width > VM_RAM_SIZE) break;
        uint64_t value = mem_read(db->vm.mem, (uint16_t)at, width);
        if (width == 2u)
            dbg_out(db, "0x%04X: 0x%04" PRIX64 "\n", (unsigned)at, value);
        else if (width == 4u)
            dbg_out(db, "0x%04X: 0x%08" PRIX64 "\n", (unsigned)at, value);
        else
            dbg_out(db, "0x%04X: 0x%016" PRIX64 "  %" PRId64 "\n",
                    (unsigned)at, value, (int64_t)value);
    }
}

/* -------------------------------------------------------------- execution */

static void run_until(Debugger *db, RunMode mode, uint16_t until,
                      uint16_t base_fsp)
{
    uint64_t steps = 0u;
    while (steps < DBG_MAX_RUN_STEPS) {
        VmStatus st = vm_step(&db->vm);
        db->status = st;
        if (st != VM_OK) {
            stopped(db, vm_status_str(st));
            return;
        }
        steps++;
        if (mode == RUN_UNTIL && db->vm.ip == until) {
            stopped(db, "reached target address");
            return;
        }
        if (mode == RUN_FINISH && db->vm.fsp <= base_fsp) {
            stopped(db, "returned from frame");
            return;
        }
        if (hit_breakpoint(db)) {
            stopped(db, "breakpoint");
            return;
        }
    }
    stopped(db, "step limit reached");
}

static void step_cmd(Debugger *db, const char *arg)
{
    unsigned long count = 1u;
    if (arg && *arg) {
        char *end = NULL;
        count = strtoul(arg, &end, 10);
        if (end == arg || *end != '\0' || count == 0u) {
            dbg_err("invalid step count '%s'", arg);
            return;
        }
        if (count > 100000u) count = 100000u;
    }
    if (db->status == VM_HALTED) {
        dbg_out(db, "Program is halted; use `reset` to run it again.\n");
        return;
    }
    for (unsigned long i = 0u; i < count; i++) {
        VmStatus st = vm_step(&db->vm);
        db->status = st;
        if (st != VM_OK) {
            stopped(db, vm_status_str(st));
            return;
        }
        if (hit_breakpoint(db)) {
            stopped(db, "breakpoint");
            return;
        }
    }
    print_current(db);
}

static void next_cmd(Debugger *db)
{
    if (db->status == VM_HALTED) {
        dbg_out(db, "Program is halted; use `reset` to run it again.\n");
        return;
    }
    uint8_t op = db->vm.mem[db->vm.ip];
    if ((op == OP_CALL || op == OP_CALL_I) && db->vm.fsp < VM_FRAME_COUNT) {
        run_until(db, RUN_FINISH, 0u, db->vm.fsp);
    } else {
        step_cmd(db, NULL);
    }
}

static void finish_cmd(Debugger *db)
{
    if (db->vm.fsp == 0u) {
        dbg_out(db, "No call frame to finish.\n");
        return;
    }
    run_until(db, RUN_FINISH, 0u, db->vm.fsp);
}

static void continue_cmd(Debugger *db)
{
    if (db->status == VM_HALTED) {
        dbg_out(db, "Program is halted; use `reset` to run it again.\n");
        return;
    }
    run_until(db, RUN_CONTINUE, 0u, 0u);
}

static void until_cmd(Debugger *db, const char *arg)
{
    uint16_t addr;
    if (!arg || !resolve_expr(db, arg, &addr)) {
        dbg_err("until requires an address expression");
        return;
    }
    run_until(db, RUN_UNTIL, addr, 0u);
}

static void reset_cmd(Debugger *db)
{
    VmDevice device = db->vm.dev;
    vm_init(&db->vm);
    vm_set_device(&db->vm, &device);
    db->status = vm_load_image(&db->vm, db->image, db->image_len, db->entry);
    if (db->status != VM_OK) {
        dbg_err("reset failed: %s", vm_status_str(db->status));
        return;
    }
    dbg_out(db, "Reset to entry point.\n");
    print_current(db);
}

/* ------------------------------------------------------------- commands */

static void print_help(Debugger *db)
{
    dbg_out(db,
        "0xdb commands:\n"
        "  help, q[uit]                       show help / exit\n"
        "  c[ontinue]                         run to a breakpoint or halt\n"
        "  s[tep] [N]                         execute N instructions (default 1)\n"
        "  n[ext]                             step over CALL\n"
        "  finish | until ADDR                run to return / address\n"
        "  b[reak] ADDR|LABEL|LINE            set a breakpoint (*ADDR forces address)\n"
        "  d[elete] N|all                     remove breakpoints\n"
        "  enable N|all, disable N|all        toggle breakpoints\n"
        "  info [registers|breakpoints|symbols|stack|locals|frames|heap|source]\n"
        "  stack [N], locals [N], frames      inspect the VM stack and call frames\n"
        "  heap                               inspect allocator blocks\n"
        "  x[/8|/16|/32|/64] ADDR [N]         dump RAM\n"
        "  dis[assemble] [ADDR] [N]           disassemble around an address\n"
        "  l[ist] [LINE|ADDR]                 show source lines\n"
        "  symbols [FILTER]                   list labels and constants\n"
        "  reset                              reload the image and reset state\n"
        "\n"
        "Addresses are hex with 0x, labels, `$`, or label+$offset. A plain number\n"
        "in `break`/`list` is a source line; prefix it with `*` for an address.\n"
        "Use -i FILE for Console input so it does not mix with debugger commands.\n");
}

static void info_cmd(Debugger *db, const char *rest)
{
    char buf[256];
    char *sub = NULL;
    char *arg = NULL;
    if (rest && *rest) {
        if (strlen(rest) >= sizeof buf) {
            dbg_err("info arguments are too long");
            return;
        }
        strcpy(buf, rest);
        char *save = NULL;
        sub = strtok_r(buf, " \t", &save);
        arg = strtok_r(NULL, " \t", &save);
    }
    if (!sub || strcmp(sub, "registers") == 0 || strcmp(sub, "regs") == 0) {
        print_registers(db);
    } else if (strcmp(sub, "breakpoints") == 0 || strcmp(sub, "break") == 0) {
        print_breakpoints(db);
    } else if (strcmp(sub, "symbols") == 0) {
        print_symbols(db, arg);
    } else if (strcmp(sub, "stack") == 0) {
        print_stack(db, arg);
    } else if (strcmp(sub, "locals") == 0) {
        print_locals(db, arg);
    } else if (strcmp(sub, "frames") == 0) {
        print_frames(db);
    } else if (strcmp(sub, "heap") == 0) {
        print_heap(db);
    } else if (strcmp(sub, "source") == 0) {
        list_cmd(db, arg);
    } else {
        dbg_err("unknown info topic '%s'", sub);
    }
}

static bool cmd_is(const char *cmd, const char *full, const char *alias)
{
    return strcmp(cmd, full) == 0 || (alias && strcmp(cmd, alias) == 0);
}

static void execute_line(Debugger *db, char *line)
{
    char *save = NULL;
    char *cmd = strtok_r(line, " \t\r\n", &save);
    if (!cmd || *cmd == ';') return;
    char *arg = strtok_r(NULL, "", &save);
    if (arg) arg = trim(arg);

    if (cmd_is(cmd, "help", "h") || strcmp(cmd, "?") == 0) print_help(db);
    else if (cmd_is(cmd, "quit", "q") || strcmp(cmd, "exit") == 0) db->quit = true;
    else if (cmd_is(cmd, "continue", "c") || strcmp(cmd, "cont") == 0) continue_cmd(db);
    else if (cmd_is(cmd, "step", "s")) step_cmd(db, arg);
    else if (cmd_is(cmd, "next", "n")) next_cmd(db);
    else if (strcmp(cmd, "finish") == 0 || strcmp(cmd, "fin") == 0) finish_cmd(db);
    else if (strcmp(cmd, "until") == 0) until_cmd(db, arg);
    else if (cmd_is(cmd, "break", "b") || strcmp(cmd, "br") == 0) add_breakpoint(db, arg);
    else if (cmd_is(cmd, "delete", "d") || strcmp(cmd, "del") == 0) delete_breakpoint(db, arg);
    else if (strcmp(cmd, "enable") == 0) set_break_state(db, arg, true);
    else if (strcmp(cmd, "disable") == 0) set_break_state(db, arg, false);
    else if (cmd_is(cmd, "info", "i")) info_cmd(db, arg);
    else if (cmd_is(cmd, "stack", "st") || strcmp(cmd, "dump") == 0) print_stack(db, arg);
    else if (strcmp(cmd, "locals") == 0 || strcmp(cmd, "loc") == 0) print_locals(db, arg);
    else if (strcmp(cmd, "frames") == 0 || strcmp(cmd, "fr") == 0) print_frames(db);
    else if (strcmp(cmd, "heap") == 0) print_heap(db);
    else if (cmd_is(cmd, "examine", "x") || strncmp(cmd, "x/", 2u) == 0) {
        char examine[512];
        snprintf(examine, sizeof examine, "%s%s%s", cmd,
                 arg ? " " : "", arg ? arg : "");
        examine_cmd(db, examine);
    }
    else if (cmd_is(cmd, "disassemble", "dis")) disassemble_cmd(db, arg);
    else if (cmd_is(cmd, "list", "l")) list_cmd(db, arg);
    else if (cmd_is(cmd, "symbols", "sym")) print_symbols(db, arg);
    else if (strcmp(cmd, "reset") == 0) reset_cmd(db);
    else dbg_err("unknown command '%s' (try 'help')", cmd);
}

/* ---------------------------------------------------------------- loading */

static bool load_program(Debugger *db, const char *path, const char *symbols_path,
                         const char *const *include_dirs, size_t include_dir_count)
{
    uint8_t *buf = NULL;
    size_t len = 0u;
    if (!tools_read_file(path, &buf, &len)) return false;

    if (tools_is_rom(buf, len)) {
        VmStatus st = vm_load_rom(&db->vm, buf, len);
        if (st != VM_OK) {
            dbg_err("cannot load ROM: %s", vm_status_str(st));
            free(buf);
            return false;
        }
        db->image_len = tools_rom_payload_len(buf, len);
        db->entry = read_be16(buf + 10u);
        db->image_end = (uint32_t)VM_IMAGE_START + (uint32_t)db->image_len;
        db->image = malloc(db->image_len);
        if (!db->image) {
            dbg_err("out of memory loading ROM");
            free(buf);
            return false;
        }
        memcpy(db->image, buf + VM_ROM_HEADER_SIZE, db->image_len);
        free(buf);

        if (symbols_path) {
            AsmProgram *program = malloc(sizeof *program);
            if (!program || !tools_assemble_file_ex(symbols_path, program,
                                                    include_dirs,
                                                    include_dir_count)) {
                free(program);
                dbg_err("cannot load symbols from '%s'", symbols_path);
                return false;
            }
            if (db->program) { asm_program_free(db->program); free(db->program); }
            db->program = program;
            if (program->len != db->image_len)
                dbg_err("symbol source layout differs from the ROM; line info may be wrong");
        }
        return true;
    }

    AsmProgram *program = malloc(sizeof *program);
    if (!program || !tools_assemble_file_ex(path, program, include_dirs,
                                            include_dir_count)) {
        free(program);
        free(buf);
        return false;
    }
    free(buf);
    db->image_len = program->len;
    db->image = malloc(db->image_len);
    if (!db->image) {
        dbg_err("out of memory loading program");
        asm_program_free(program);
        free(program);
        return false;
    }
    memcpy(db->image, program->bytes, db->image_len);
    db->entry = (uint16_t)(VM_IMAGE_START + program->entry);
    db->image_end = (uint32_t)VM_IMAGE_START + (uint32_t)db->image_len;
    db->program = program;

    VmStatus st = vm_load_image(&db->vm, db->image, db->image_len, db->entry);
    if (st != VM_OK) {
        dbg_err("cannot load image: %s", vm_status_str(st));
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    cli_args_t args = cli_args_make(
        cli_arg_new('h', "help", "Print this help message", no_argument),
        cli_arg_new('v', "version", "Print the program version", no_argument),
        cli_arg_new('f', "file", "Assembly source or VMIM ROM file", required_argument),
        cli_arg_new('s', "symbols", "Assembly source to use as symbols for a ROM", required_argument),
        cli_arg_new('i', "input", "File to use for Console input", required_argument),
        cli_arg_new('x', "execute", "Read debugger commands from a script", required_argument),
        cli_arg_new('I', "include-dir", "Add a directory to the INCLUDE search path", required_argument),
        NULL
    );

    const char *path = NULL;
    const char *symbols_path = NULL;
    const char *input_path = NULL;
    const char *script_path = NULL;
    const char **include_dirs = NULL;
    size_t include_dir_count = 0u;
    int opt;
    LOOP_ARGS(opt, args) {
        switch (opt) {
            case 'h':
                cli_help(args, NULL, "0xdb -f FILE [-s SOURCE] [-i INPUT] [-x SCRIPT] [-I DIR]...",
                         "Interactive debugger for 0xvm programs. Console input is read\n"
                         "from -i (or /dev/null) so it never competes with commands.");
                cli_args_free(&args);
                free(include_dirs);
                return EXIT_SUCCESS;
            case 'v':
                tools_print_version("0xdb");
                cli_args_free(&args);
                free(include_dirs);
                return EXIT_SUCCESS;
            case 'f': path = optarg; break;
            case 's': symbols_path = optarg; break;
            case 'i': input_path = optarg; break;
            case 'x': script_path = optarg; break;
            case 'I': {
                const char **tmp = realloc(include_dirs,
                    (include_dir_count + 1u) * sizeof *tmp);
                if (!tmp) {
                    dbg_err("out of memory");
                    cli_args_free(&args);
                    free(include_dirs);
                    return EXIT_FAILURE;
                }
                include_dirs = tmp;
                include_dirs[include_dir_count++] = optarg;
                break;
            }
            default: break;
        }
    }
    cli_args_free(&args);

    if (!path) {
        dbg_err("provide a file with -f/--file");
        free(include_dirs);
        return EXIT_FAILURE;
    }

    Debugger db;
    memset(&db, 0, sizeof db);
    db.out = stdout;
    db.status = VM_OK;
    db.program_path = path;

    vm_init(&db.vm);
    VmHost host;
    host_init(&host);
    HostStdio stdio;
    host_stdio_init(&stdio, false);
    db.program_input = input_path ? fopen(input_path, "rb") : fopen("/dev/null", "rb");
    if (!db.program_input) {
        dbg_err("cannot open Console input '%s'", input_path ? input_path : "/dev/null");
        free(include_dirs);
        return EXIT_FAILURE;
    }
    stdio.input = db.program_input;
    stdio.output = stdout;
    host_stdio_add(&host, &stdio);
    VmDevice device = host_device(&host);
    vm_set_device(&db.vm, &device);

    if (script_path) {
        db.cmd = fopen(script_path, "rb");
        if (!db.cmd) {
            dbg_err("cannot open command script '%s'", script_path);
            fclose(db.program_input);
            free(include_dirs);
            return EXIT_FAILURE;
        }
        db.interactive = false;
    } else {
        db.cmd = stdin;
        db.interactive = isatty(fileno(stdin)) != 0;
    }

    if (!load_program(&db, path, symbols_path, include_dirs, include_dir_count)) {
        if (script_path) fclose(db.cmd);
        fclose(db.program_input);
        free(db.image);
        vm_free(&db.vm);
        free(include_dirs);
        return EXIT_FAILURE;
    }
    free(include_dirs);

    dbg_out(&db, "0xdb: loaded %s (%zu bytes)\n", path, db.image_len);
    print_current(&db);
    if (db.interactive) print_help(&db);

    char line[DBG_LINE_MAX];
    while (!db.quit) {
        if (db.interactive) {
            dbg_out(&db, "0xdb> ");
            fflush(db.out);
        }
        if (!fgets(line, sizeof line, db.cmd)) {
            if (db.interactive) dbg_out(&db, "\n");
            break;
        }
        execute_line(&db, line);
    }

    if (script_path) fclose(db.cmd);
    if (db.program_input != stdin) fclose(db.program_input);
    if (db.program) { asm_program_free(db.program); free(db.program); }
    free(db.image);
    vm_free(&db.vm);
    return EXIT_SUCCESS;
}
