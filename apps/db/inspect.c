#include "db.h"

/* ------------------------------------------------------------ inspection */

void print_current(Debugger *db)
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
    const char *path = NULL;
    size_t line = 0u;
    source_map_address(db, db->vm.ip, &path, &line);
    if (sym) dbg_out(db, "  <%s>", sym->name);
    if (path && line) dbg_out(db, "  %s:%zu", path, line);
    else if (line) dbg_out(db, "  line %zu", line);
    dbg_out(db, "\n");
}

void print_registers(Debugger *db)
{
    dbg_out(db,
            "ip=0x%04X sp=%u fp=%u fsp=%u hp=0x%04X ticks=%" PRIu64
            " status=%s\n",
            (unsigned)db->vm.ip, (unsigned)db->vm.sp, (unsigned)db->vm.fp,
            (unsigned)db->vm.fsp, (unsigned)db->vm.hp, db->vm.ticks,
            vm_status_str(db->status));
    print_current(db);
}

void print_stack(Debugger *db, const char *arg)
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

void print_locals(Debugger *db, const char *arg)
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

void print_frames(Debugger *db)
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

void print_heap(Debugger *db)
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

void print_symbols(Debugger *db, const char *filter)
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

void list_cmd(Debugger *db, const char *arg)
{
    if (!db->program) {
        dbg_out(db, "Source mapping requires an assembly file (use -s with a ROM).\n");
        return;
    }
    const char *path = NULL;
    size_t line = 0u;
    source_map_address(db, db->vm.ip, &path, &line);
    if (arg && *arg) {
        int64_t value;
        if (parse_i64(arg, &value)) {
            if (value <= 0) {
                dbg_err("line numbers start at 1");
                return;
            }
            if (db->source_map.count == 0u) {
                line = (size_t)value;
            } else {
                uint16_t addr;
                if (!resolve_line_ref(db, arg, &addr)) {
                    dbg_err("cannot resolve line '%s'", arg);
                    return;
                }
                source_map_address(db, addr, &path, &line);
            }
        } else {
            uint16_t addr;
            if (!resolve_expr(db, arg, &addr) && !resolve_line_ref(db, arg, &addr)) {
                dbg_err("cannot resolve '%s'", arg);
                return;
            }
            source_map_address(db, addr, &path, &line);
        }
    }
    if (line == 0u) {
        dbg_out(db, "No source line is mapped to this address.\n");
        return;
    }
    print_source_range(db, path, line, 5u);
}

void disassemble_cmd(Debugger *db, const char *arg)
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

void examine_cmd(Debugger *db, const char *arg)
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
