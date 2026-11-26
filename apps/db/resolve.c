#include "db.h"

/* --------------------------------------------------------- address resolver */

bool resolve_expr(Debugger *db, const char *expr, uint16_t *out)
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

static bool address_for_asm_line(Debugger *db, size_t line, uint16_t *out)
{
    uint32_t cur = code_start(db);
    while (cur < db->image_end && cur <= UINT16_MAX) {
        char dis[VM_DIS_LINE_MAX];
        unsigned size = vm_disassemble_range(db->vm.mem, (uint16_t)cur,
                                             db->image_end, dis, sizeof dis);
        if (size == 0u) break;
        if (asm_line_of(db->program, (uint16_t)cur) == line) {
            *out = (uint16_t)cur;
            return true;
        }
        cur += size;
    }
    return false;
}

/* `40` selects source line 40; `file.lua:40` disambiguates mapped modules. */
bool resolve_line_ref(Debugger *db, const char *text, uint16_t *out)
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

    if (db->source_map.count != 0u) {
        if (!wanted) {
            const char *current = NULL;
            size_t current_line = 0u;
            if (!source_map_address(db, db->vm.ip, &current, &current_line))
                return false;
            wanted = current;
        }
        size_t asm_line = 0u;
        if (!source_map_find(&db->source_map, wanted, (size_t)line, &asm_line))
            return false;
        return address_for_asm_line(db, asm_line, out);
    }

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

bool resolve_break_arg(Debugger *db, const char *arg, uint16_t *out)
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
