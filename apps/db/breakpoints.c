#include "db.h"

/* ------------------------------------------------------------ breakpoints */

static size_t find_break(const Debugger *db, uint16_t addr)
{
    for (size_t i = 0u; i < db->break_count; i++) {
        if (db->breaks[i].addr == addr) return i;
    }
    return db->break_count;
}

void print_breakpoints(Debugger *db)
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

void add_breakpoint(Debugger *db, const char *arg)
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

void set_break_state(Debugger *db, const char *arg, bool enabled)
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

void delete_breakpoint(Debugger *db, const char *arg)
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

bool hit_breakpoint(Debugger *db)
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
