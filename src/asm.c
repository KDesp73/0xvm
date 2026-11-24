#include "asm.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OPR_NONE = 0,
    OPR_INT,     /* integer expression */
    OPR_TARGET,  /* label or integer expression */
    OPR_FLOAT,   /* floating-point literal (raw bits are emitted) */
    OPR_LOCAL,   /* local variable index (encoded in one byte) */
    OPR_PORT     /* device port number (encoded in one byte) */
} OperandKind;

typedef enum { REF_NONE = 0, REF_TARGET, REF_VALUE } RefKind;
typedef enum { D_BYTE, D_WORD, D_QWORD, D_STR, D_ALIGN } DataKind;
typedef enum { ITEM_INST = 0, ITEM_DATA } ItemKind;

typedef struct {
    const char *name;
    size_t len;
    size_t item_index;  /* label points here; item_count means "end" */
} Label;

typedef struct {
    const char *name;
    size_t len;
    int64_t value;
} Constant;

typedef struct {
    ItemKind kind;
    VmOpcode op;
    int64_t arg;
    RefKind ref;
    const char *label;
    size_t label_len;
    DataKind data_kind;
    const char *text;
    size_t text_len;
    size_t line;
} Item;

typedef struct {
    AsmProgram *out;
    char *src;
    Item *items;
    size_t item_count, item_cap;
    Label *labels;
    size_t label_count, label_cap;
    Constant *consts;
    size_t const_count, const_cap;
} Parser;

typedef struct {
    const char *name;
    VmOpcode op;
    OperandKind operand;
} OpDef;

static const OpDef OPS[] = {
    {"HALT",    OP_HALT,     OPR_NONE},
    {"NOP",     OP_NOP,      OPR_NONE},
    {"PUSH",    OP_PUSH,     OPR_INT},
    {"POP",     OP_POP,      OPR_NONE},
    {"DUP",     OP_DUP,      OPR_NONE},
    {"ADD",     OP_ADD,      OPR_NONE},
    {"SUB",     OP_SUB,      OPR_NONE},
    {"MUL",     OP_MUL,      OPR_NONE},
    {"CMP_LT",  OP_CMP_LT,   OPR_NONE},
    {"JMP",     OP_JMP,      OPR_TARGET},
    {"JMP_Z",   OP_JMP_Z,    OPR_TARGET},
    {"ALLOC",   OP_ALLOC,    OPR_NONE},
    {"LOAD32",  OP_LOAD32,   OPR_NONE},
    {"STORE32", OP_STORE32,  OPR_NONE},
    {"DIV",     OP_DIV,      OPR_NONE},
    {"MOD",     OP_MOD,      OPR_NONE},
    {"CMP_EQ",  OP_CMP_EQ,   OPR_NONE},
    {"CMP_NE",  OP_CMP_NE,   OPR_NONE},
    {"CMP_LE",  OP_CMP_LE,   OPR_NONE},
    {"CMP_GT",  OP_CMP_GT,   OPR_NONE},
    {"CMP_GE",  OP_CMP_GE,   OPR_NONE},
    {"AND",     OP_AND,      OPR_NONE},
    {"OR",      OP_OR,       OPR_NONE},
    {"XOR",     OP_XOR,      OPR_NONE},
    {"NOT",     OP_NOT,      OPR_NONE},
    {"SHL",     OP_SHL,      OPR_NONE},
    {"SHR",     OP_SHR,      OPR_NONE},
    {"LNOT",    OP_LNOT,     OPR_NONE},
    {"LAND",    OP_LAND,     OPR_NONE},
    {"LOR",     OP_LOR,      OPR_NONE},

    /* Stack shuffling */
    {"SWAP",    OP_SWAP,     OPR_NONE},
    {"OVER",    OP_OVER,     OPR_NONE},
    {"ROT",     OP_ROT,      OPR_NONE},
    {"PICK",    OP_PICK,     OPR_NONE},

    /* RAM access */
    {"LOAD8",   OP_LOAD8,    OPR_NONE},
    {"STORE8",  OP_STORE8,   OPR_NONE},
    {"LOAD16",  OP_LOAD16,   OPR_NONE},
    {"STORE16", OP_STORE16,  OPR_NONE},
    {"LOAD32U", OP_LOAD32U,  OPR_NONE},
    {"LOAD64",  OP_LOAD64,   OPR_NONE},
    {"STORE64", OP_STORE64,  OPR_NONE},
    {"FREE",    OP_FREE,     OPR_NONE},

    /* Procedures and locals (GET/SET are aliases) */
    {"CALL",    OP_CALL,     OPR_TARGET},
    {"CALL_I",  OP_CALL_I,   OPR_NONE},
    {"RET",     OP_RET,      OPR_NONE},
    {"ENTER",   OP_ENTER,    OPR_INT},
    {"LOCAL_GET", OP_LOCAL_GET, OPR_LOCAL},
    {"LOCAL_SET", OP_LOCAL_SET, OPR_LOCAL},
    {"GET",     OP_LOCAL_GET, OPR_LOCAL},
    {"SET",     OP_LOCAL_SET, OPR_LOCAL},
    {"JMP_I",   OP_JMP_I,    OPR_NONE},

    /* Floating point */
    {"PUSHF",   OP_PUSHF,    OPR_FLOAT},
    {"FADD",    OP_FADD,     OPR_NONE},
    {"FSUB",    OP_FSUB,     OPR_NONE},
    {"FMUL",    OP_FMUL,     OPR_NONE},
    {"FDIV",    OP_FDIV,     OPR_NONE},
    {"FNEG",    OP_FNEG,     OPR_NONE},
    {"FABS",    OP_FABS,     OPR_NONE},
    {"FSQRT",   OP_FSQRT,    OPR_NONE},
    {"FCMP_LT", OP_FCMP_LT,  OPR_NONE},
    {"FCMP_EQ", OP_FCMP_EQ,  OPR_NONE},
    {"FCMP_LE", OP_FCMP_LE,  OPR_NONE},
    {"FCMP_GT", OP_FCMP_GT,  OPR_NONE},
    {"FCMP_GE", OP_FCMP_GE,  OPR_NONE},
    {"FTOI",    OP_FTOI,     OPR_NONE},
    {"ITOF",    OP_ITOF,     OPR_NONE},

    /* Device layer */
    {"DEI",     OP_DEI,      OPR_PORT},
    {"DEO",     OP_DEO,      OPR_PORT}
};

/* ------------------------------------------------------------------ basics */

void asm_program_init(AsmProgram *prog)
{
    if (!prog) return;
    memset(prog, 0, sizeof(*prog));
}

void asm_program_free(AsmProgram *prog)
{
    if (!prog) return;
    free(prog->bytes);
    free(prog->lines);
    memset(prog, 0, sizeof(*prog));
}

/* Release only emitted payload buffers; keep error_line/error for diagnostics. */
static void clear_payload(AsmProgram *prog)
{
    free(prog->bytes);
    free(prog->lines);
    prog->bytes = NULL;
    prog->lines = NULL;
    prog->len = 0u;
    prog->entry = 0u;
}

size_t asm_line_of(const AsmProgram *prog, uint16_t addr)
{
    if (!prog || !prog->lines || addr < VM_IMAGE_START) return 0u;
    size_t offset = (size_t)addr - VM_IMAGE_START;
    if (offset >= prog->len) return 0u;
    return prog->lines[offset];
}

static int fail(Parser *p, size_t line, const char *fmt, ...)
{
    char *dst = p->out->error;
    size_t cap = sizeof(p->out->error);
    int n = snprintf(dst, cap, "line %zu: ", line);
    if (n > 0 && (size_t)n < cap) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(dst + n, cap - (size_t)n, fmt, ap);
        va_end(ap);
    }
    p->out->error_line = line;
    return -1;
}

static int grow(void **array, size_t *cap, size_t need, size_t elem_size)
{
    if (need <= *cap) return 0;
    size_t next = *cap ? *cap : 8;
    while (next < need) {
        if (next > (size_t)-1 / 2u) return -1;
        next *= 2u;
    }
    if (next > (size_t)-1 / elem_size) return -1;
    void *tmp = realloc(*array, next * elem_size);
    if (!tmp) return -1;
    *array = tmp;
    *cap = next;
    return 0;
}

static char *skip_ws(char *s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

static void trim_end(char *s)
{
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
}

static size_t token_len(const char *s)
{
    size_t n = 0;
    while (s[n] && !isspace((unsigned char)s[n])) n++;
    return n;
}

static int token_eq_ci(const char *s, size_t len, const char *lit)
{
    if (strlen(lit) != len) return 0;
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)lit[i])) return 0;
    }
    return 1;
}

static int valid_label(const char *s, size_t len)
{
    if (len == 0 || !(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
    for (size_t i = 1; i < len; i++) {
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return 0;
    }
    return 1;
}

/* Comments respect quoted strings so `STR "a; b"` stays intact. */
static void strip_comment(char *s)
{
    int quoted = 0;
    for (; *s; s++) {
        if (quoted) {
            if (*s == '\\' && s[1]) { s++; continue; }
            if (*s == '"') quoted = 0;
            continue;
        }
        if (*s == '"') { quoted = 1; continue; }
        if (*s == ';' || *s == '#') { *s = '\0'; return; }
        if (*s == '/' && s[1] == '/') { *s = '\0'; return; }
    }
}

/* ------------------------------------------------------------- expressions */

typedef struct {
    const Parser *p;
    const char *cur;
    const char *end;
} Expr;

static void ex_ws(Expr *e)
{
    while (e->cur < e->end && isspace((unsigned char)*e->cur)) e->cur++;
}

static int ex_expr(Expr *e, int64_t *out);

/* Only constants are evaluable at parse time. Labels are resolved after the
 * byte layout is known, which lets them denote absolute RAM addresses. */
static int ex_lookup(const Parser *p, const char *name, size_t len, int64_t *out)
{
    for (size_t i = 0; i < p->const_count; i++) {
        if (p->consts[i].len == len && memcmp(p->consts[i].name, name, len) == 0) {
            *out = p->consts[i].value;
            return 0;
        }
    }
    return -1;
}

static int ex_primary(Expr *e, int64_t *out)
{
    ex_ws(e);
    if (e->cur >= e->end) return -1;

    if (*e->cur == '(') {
        e->cur++;
        if (ex_expr(e, out) != 0) return -1;
        ex_ws(e);
        if (e->cur >= e->end || *e->cur != ')') return -1;
        e->cur++;
        return 0;
    }

    if (isalpha((unsigned char)*e->cur) || *e->cur == '_') {
        const char *start = e->cur;
        while (e->cur < e->end &&
               (isalnum((unsigned char)*e->cur) || *e->cur == '_')) e->cur++;
        return ex_lookup(e->p, start, (size_t)(e->cur - start), out);
    }

    const char *start = e->cur;
    int base = 10;
    if (e->cur + 2u <= e->end && *e->cur == '0' &&
        (e->cur[1] == 'x' || e->cur[1] == 'X')) {
        base = 16;
        e->cur += 2u;
        while (e->cur < e->end && isxdigit((unsigned char)*e->cur)) e->cur++;
    } else {
        while (e->cur < e->end && isdigit((unsigned char)*e->cur)) e->cur++;
    }
    if (e->cur == start || (base == 16 && e->cur == start + 2u)) return -1;

    char buf[64];
    size_t len = (size_t)(e->cur - start);
    if (len >= sizeof buf) return -1;
    memcpy(buf, start, len);
    buf[len] = '\0';
    errno = 0;
    char *endp = NULL;
    long long v = strtoll(buf, &endp, base);
    if (endp != buf + len || errno == ERANGE) return -1;
    *out = (int64_t)v;
    return 0;
}

static int ex_unary(Expr *e, int64_t *out)
{
    ex_ws(e);
    if (e->cur < e->end && (*e->cur == '-' || *e->cur == '+' || *e->cur == '~')) {
        char op = *e->cur++;
        ex_ws(e);
        static const char min_literal[] = "9223372036854775808";
        size_t n = sizeof min_literal - 1u;
        if (op == '-' && (size_t)(e->end - e->cur) >= n &&
            memcmp(e->cur, min_literal, n) == 0 &&
            (e->cur + n == e->end ||
             !(isalnum((unsigned char)e->cur[n]) || e->cur[n] == '_'))) {
            e->cur += n;
            *out = INT64_MIN;
            return 0;
        }
        if (ex_unary(e, out) != 0) return -1;
        if (op == '-') *out = (int64_t)(0u - (uint64_t)*out);
        else if (op == '~') *out = ~(uint64_t)*out;
        return 0;
    }
    return ex_primary(e, out);
}

static int ex_term(Expr *e, int64_t *out)
{
    if (ex_unary(e, out) != 0) return -1;
    for (;;) {
        ex_ws(e);
        if (e->cur >= e->end) return 0;
        char op = *e->cur;
        if (op != '*' && op != '/' && op != '%') return 0;
        e->cur++;
        int64_t rhs;
        if (ex_unary(e, &rhs) != 0) return -1;
        uint64_t a = (uint64_t)*out;
        uint64_t b = (uint64_t)rhs;
        if (op == '*')      *out = (int64_t)(a * b);
        else if (op == '/') { if (rhs == 0 || (a == UINT64_MAX && b == UINT64_MAX)) return -1; *out = (int64_t)(a / b); }
        else                { if (rhs == 0) return -1; *out = (int64_t)(a % b); }
    }
}

static int ex_expr(Expr *e, int64_t *out)
{
    if (ex_term(e, out) != 0) return -1;
    for (;;) {
        ex_ws(e);
        if (e->cur >= e->end) return 0;
        char op = *e->cur;
        if (op != '+' && op != '-') return 0;
        e->cur++;
        int64_t rhs;
        if (ex_term(e, &rhs) != 0) return -1;
        *out = (int64_t)((op == '+') ? (uint64_t)*out + (uint64_t)rhs
                                     : (uint64_t)*out - (uint64_t)rhs);
    }
}

static int eval_expr(const Parser *p, const char *text, size_t len, int64_t *out)
{
    if (len == 0u) return -1;
    Expr e = {p, text, text + len};
    if (ex_expr(&e, out) != 0) return -1;
    ex_ws(&e);
    return e.cur == e.end ? 0 : -1;
}

/* ------------------------------------------------------------------ tables */

static const OpDef *find_op(const char *s, size_t len)
{
    for (size_t i = 0; i < sizeof OPS / sizeof OPS[0]; i++) {
        if (token_eq_ci(s, len, OPS[i].name)) return &OPS[i];
    }
    return NULL;
}

static const Label *find_label(const Parser *p, const char *name, size_t len)
{
    for (size_t i = 0; i < p->label_count; i++) {
        if (p->labels[i].len == len && memcmp(p->labels[i].name, name, len) == 0) {
            return &p->labels[i];
        }
    }
    return NULL;
}

static int add_label(Parser *p, const char *name, size_t len, size_t item_index)
{
    if (find_label(p, name, len)) return -2; /* duplicate */
    if (grow((void **)&p->labels, &p->label_cap, p->label_count + 1u,
             sizeof *p->labels) != 0) return -1;
    Label *label = &p->labels[p->label_count++];
    label->name = name;
    label->len = len;
    label->item_index = item_index;
    return 0;
}

static int add_const(Parser *p, const char *name, size_t len, int64_t value)
{
    for (size_t i = 0; i < p->const_count; i++) {
        if (p->consts[i].len == len && memcmp(p->consts[i].name, name, len) == 0) return -2;
    }
    if (grow((void **)&p->consts, &p->const_cap, p->const_count + 1u,
             sizeof *p->consts) != 0) return -1;
    Constant *c = &p->consts[p->const_count++];
    c->name = name;
    c->len = len;
    c->value = value;
    return 0;
}

static Item *add_item(Parser *p, size_t line)
{
    if (grow((void **)&p->items, &p->item_cap, p->item_count + 1u,
             sizeof *p->items) != 0) return NULL;
    Item *item = &p->items[p->item_count++];
    memset(item, 0, sizeof(*item));
    item->line = line;
    return item;
}

static int add_inst(Parser *p, size_t line, VmOpcode op, int64_t arg)
{
    Item *item = add_item(p, line);
    if (!item) return -1;
    item->op = op;
    item->arg = arg;
    return 0;
}

/* ---------------------------------------------------------- data emission */

typedef struct {
    uint8_t *bytes;
    size_t *lines;
    size_t len, cap;
} DataBuf;

static int db_append(DataBuf *db, const uint8_t *src, size_t n, size_t line)
{
    if (grow((void **)&db->bytes, &db->cap, db->len + n, 1u) != 0) return -1;
    size_t *new_lines = realloc(db->lines, db->cap * sizeof *db->lines);
    if (!new_lines) return -1;
    db->lines = new_lines;
    memcpy(db->bytes + db->len, src, n);
    for (size_t i = 0; i < n; i++) db->lines[db->len + i] = line;
    db->len += n;
    return 0;
}

static int db_byte(DataBuf *db, uint8_t b, size_t line)
{
    return db_append(db, &b, 1u, line);
}

static int db_number(DataBuf *db, uint64_t v, unsigned width, size_t line)
{
    for (unsigned i = 0; i < width; i++) {
        if (db_byte(db, (uint8_t)(v >> (8u * (width - 1u - i))), line) != 0) return -1;
    }
    return 0;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int emit_string(Parser *p, DataBuf *db, const char *text, size_t len, size_t line)
{
    if (len < 2u || text[0] != '"' || text[len - 1u] != '"') {
        return fail(p, line, "STR requires a quoted string");
    }
    for (size_t i = 1u; i + 1u < len; i++) {
        uint8_t c = (uint8_t)text[i];
        if (c == '\\' && i + 2u < len) {
            char e = text[++i];
            switch (e) {
                case 'n':  c = '\n'; break;
                case 'r':  c = '\r'; break;
                case 't':  c = '\t'; break;
                case '0':  c = '\0'; break;
                case '\\': c = '\\'; break;
                case '"':  c = '"'; break;
                case 'x': {
                    if (i + 2u >= len) return fail(p, line, "bad \\x escape");
                    int hi = hex_digit(text[++i]);
                    int lo = hex_digit(text[++i]);
                    if (hi < 0 || lo < 0) return fail(p, line, "bad \\x escape");
                    c = (uint8_t)((hi << 4) | lo);
                    break;
                }
                default: return fail(p, line, "unknown escape sequence");
            }
        }
        if (db_byte(db, c, line) != 0) return fail(p, line, "out of memory");
    }
    return 0;
}

/* Emit all data items into the area after the entry JMP, recording the
 * absolute RAM address where each item starts. */
static int emit_data(Parser *p, DataBuf *db, size_t *item_addr)
{
    for (size_t i = 0; i < p->item_count; i++) {
        Item *item = &p->items[i];
        if (item->kind != ITEM_DATA) continue;
        item_addr[i] = VM_IMAGE_START + 3u + db->len;

        if (item->data_kind == D_STR) {
            if (emit_string(p, db, item->text, item->text_len, item->line) != 0)
                return -1;
            continue;
        }
        if (item->data_kind == D_ALIGN) {
            int64_t n;
            if (eval_expr(p, item->text, item->text_len, &n) != 0 ||
                n <= 0 || n > 4096) {
                return fail(p, item->line, "invalid ALIGN value");
            }
            while (db->len % (size_t)n != 0u) {
                if (db_byte(db, 0u, item->line) != 0)
                    return fail(p, item->line, "out of memory");
            }
            continue;
        }

        unsigned width = item->data_kind == D_BYTE ? 1u :
                         (item->data_kind == D_WORD ? 2u : 8u);
        const char *cur = item->text;
        const char *end = item->text + item->text_len;
        if (cur >= end) return fail(p, item->line, "directive requires a value");
        for (;;) {
            const char *start = cur;
            while (cur < end && *cur != ',' && !isspace((unsigned char)*cur)) cur++;
            int64_t value;
            if (eval_expr(p, start, (size_t)(cur - start), &value) != 0) {
                return fail(p, item->line, "invalid integer '%.*s'",
                            (int)(cur - start), start);
            }
            if (db_number(db, (uint64_t)value, width, item->line) != 0)
                return fail(p, item->line, "out of memory");
            while (cur < end && (isspace((unsigned char)*cur) || *cur == ',')) cur++;
            if (cur >= end) break;
        }
    }
    return 0;
}

static void put_be(uint8_t *p, uint64_t v, unsigned width)
{
    for (unsigned i = 0; i < width; i++) {
        p[width - 1u - i] = (uint8_t)(v & 0xFFu);
        v >>= 8;
    }
}

/* ----------------------------------------------------------------- parser */

int asm_parse_string(AsmProgram *prog, const char *source)
{
    if (!prog || !source) return -1;
    asm_program_init(prog);

    Parser p;
    memset(&p, 0, sizeof p);
    p.out = prog;
    p.src = malloc(strlen(source) + 1u);
    if (!p.src) {
        snprintf(prog->error, sizeof prog->error, "out of memory");
        return -1;
    }
    strcpy(p.src, source);

    char *cursor = p.src;
    size_t line_no = 0;
    for (;;) {
        char *newline = strchr(cursor, '\n');
        if (newline) *newline = '\0';
        line_no++;

        strip_comment(cursor);
        char *s = skip_ws(cursor);
        trim_end(s);

        /* Labels may share a line with an instruction or data directive. */
        while (*s && *s != '"') {
            size_t n = 0;
            while (s[n] && s[n] != ':' && !isspace((unsigned char)s[n])) n++;
            if (s[n] != ':') break;
            if (!valid_label(s, n)) {
                fail(&p, line_no, "invalid label name");
                goto error;
            }
            int rc = add_label(&p, s, n, p.item_count);
            if (rc == -2) {
                fail(&p, line_no, "duplicate label '%.*s'", (int)n, s);
                goto error;
            }
            if (rc != 0) {
                fail(&p, line_no, "out of memory");
                goto error;
            }
            s = skip_ws(s + n + 1u);
        }

        if (*s) {
            size_t mn_len = token_len(s);
            char *rest = skip_ws(s + mn_len);
            size_t rest_len = token_len(rest);

            if (token_eq_ci(s, mn_len, "EQU")) {
                if (rest_len == 0u || !valid_label(rest, rest_len)) {
                    fail(&p, line_no, "EQU requires a name and expression");
                    goto error;
                }
                const char *name = rest;
                char *expr = skip_ws(rest + rest_len);
                int64_t value;
                if (eval_expr(&p, expr, strlen(expr), &value) != 0) {
                    fail(&p, line_no, "invalid constant expression");
                    goto error;
                }
                int rc = add_const(&p, name, rest_len, value);
                if (rc == -2) {
                    fail(&p, line_no, "duplicate constant '%.*s'", (int)rest_len, name);
                    goto error;
                }
                if (rc != 0) {
                    fail(&p, line_no, "out of memory");
                    goto error;
                }
            } else if (token_eq_ci(s, mn_len, "DB") ||
                       token_eq_ci(s, mn_len, "DW") ||
                       token_eq_ci(s, mn_len, "DQ") ||
                       token_eq_ci(s, mn_len, "STR") ||
                       token_eq_ci(s, mn_len, "ALIGN")) {
                DataKind kind = token_eq_ci(s, mn_len, "DB")  ? D_BYTE :
                                token_eq_ci(s, mn_len, "DW")  ? D_WORD :
                                token_eq_ci(s, mn_len, "DQ")  ? D_QWORD :
                                token_eq_ci(s, mn_len, "STR") ? D_STR : D_ALIGN;
                if (rest_len == 0u && kind != D_STR) {
                    fail(&p, line_no, "directive requires a value");
                    goto error;
                }
                size_t text_len = strlen(rest);
                Item *item = add_item(&p, line_no);
                if (!item) goto oom;
                item->kind = ITEM_DATA;
                item->data_kind = kind;
                item->text = rest;
                item->text_len = text_len;
            } else {
                const OpDef *def = find_op(s, mn_len);
                if (!def) {
                    /* Pseudo-ops expand into real instructions and device ports. */
                    bool pseudo = token_eq_ci(s, mn_len, "INC") ||
                                  token_eq_ci(s, mn_len, "DEC") ||
                                  token_eq_ci(s, mn_len, "NEG") ||
                                  token_eq_ci(s, mn_len, "PRINT_CHAR") ||
                                  token_eq_ci(s, mn_len, "PRINT") ||
                                  token_eq_ci(s, mn_len, "FPRINT") ||
                                  token_eq_ci(s, mn_len, "READ_INT") ||
                                  token_eq_ci(s, mn_len, "PRINT_STR") ||
                                  token_eq_ci(s, mn_len, "READ_STR");
                    if (!pseudo) {
                        fail(&p, line_no, "unknown instruction '%.*s'", (int)mn_len, s);
                        goto error;
                    }
                    if (rest_len != 0u) {
                        fail(&p, line_no, "%.*s takes no operand", (int)mn_len, s);
                        goto error;
                    }
                    if (token_eq_ci(s, mn_len, "INC")) {
                        if (add_inst(&p, line_no, OP_PUSH, 1) != 0 ||
                            add_inst(&p, line_no, OP_ADD, 0) != 0) goto oom;
                    } else if (token_eq_ci(s, mn_len, "DEC")) {
                        if (add_inst(&p, line_no, OP_PUSH, 1) != 0 ||
                            add_inst(&p, line_no, OP_SUB, 0) != 0) goto oom;
                    } else if (token_eq_ci(s, mn_len, "NEG")) {
                        if (add_inst(&p, line_no, OP_PUSH, 0) != 0 ||
                            add_inst(&p, line_no, OP_SWAP, 0) != 0 ||
                            add_inst(&p, line_no, OP_SUB, 0) != 0) goto oom;
                    } else if (token_eq_ci(s, mn_len, "PRINT_CHAR")) {
                        if (add_inst(&p, line_no, OP_DEO, VM_CONSOLE_PUTC) != 0) goto oom;
                    } else if (token_eq_ci(s, mn_len, "PRINT")) {
                        if (add_inst(&p, line_no, OP_DEO, VM_CONSOLE_PUTI) != 0) goto oom;
                    } else if (token_eq_ci(s, mn_len, "FPRINT")) {
                        if (add_inst(&p, line_no, OP_DEO, VM_CONSOLE_PUTF) != 0) goto oom;
                    } else if (token_eq_ci(s, mn_len, "READ_INT")) {
                        if (add_inst(&p, line_no, OP_DEI, VM_CONSOLE_GETI) != 0) goto oom;
                    } else if (token_eq_ci(s, mn_len, "PRINT_STR")) {
                        if (add_inst(&p, line_no, OP_DEO, VM_CONSOLE_PUTS_LEN) != 0 ||
                            add_inst(&p, line_no, OP_DEO, VM_CONSOLE_PUTS_ADDR) != 0 ||
                            add_inst(&p, line_no, OP_PUSH, 0) != 0 ||
                            add_inst(&p, line_no, OP_DEO, VM_CONSOLE_PUTS) != 0) goto oom;
                    } else if (token_eq_ci(s, mn_len, "READ_STR")) {
                        if (add_inst(&p, line_no, OP_DEO, VM_CONSOLE_GETS_MAX) != 0 ||
                            add_inst(&p, line_no, OP_DEO, VM_CONSOLE_GETS_ADDR) != 0 ||
                            add_inst(&p, line_no, OP_PUSH, 0) != 0 ||
                            add_inst(&p, line_no, OP_DEO, VM_CONSOLE_GETS) != 0 ||
                            add_inst(&p, line_no, OP_DEI, VM_CONSOLE_GETS_LEN) != 0) goto oom;
                    } else {
                        fail(&p, line_no, "unknown instruction '%.*s'", (int)mn_len, s);
                        goto error;
                    }
                } else if (def->operand == OPR_NONE) {
                    if (rest_len != 0u) {
                        fail(&p, line_no, "%s takes no operand", def->name);
                        goto error;
                    }
                    Item *item = add_item(&p, line_no);
                    if (!item) goto oom;
                    item->op = def->op;
                } else if (rest_len == 0u) {
                    if (def->operand == OPR_TARGET) {
                        fail(&p, line_no, "%s requires a target", def->name);
                    } else if (def->operand == OPR_FLOAT) {
                        fail(&p, line_no, "%s requires a float literal", def->name);
                    } else {
                        fail(&p, line_no, "%s requires an integer operand", def->name);
                    }
                    goto error;
                } else {
                    int64_t value = 0;
                    RefKind ref = REF_NONE;
                    if (def->operand == OPR_FLOAT) {
                        char buf[128];
                        if (rest_len >= sizeof buf) {
                            fail(&p, line_no, "invalid float '%.*s'", (int)rest_len, rest);
                            goto error;
                        }
                        memcpy(buf, rest, rest_len);
                        buf[rest_len] = '\0';
                        char *endp = NULL;
                        errno = 0;
                        double d = strtod(buf, &endp);
                        if (endp == buf || *endp != '\0' || errno == ERANGE) {
                            fail(&p, line_no, "invalid float '%.*s'", (int)rest_len, rest);
                            goto error;
                        }
                        value = vm_f64_to_bits(d);
                    } else if (eval_expr(&p, rest, rest_len, &value) != 0) {
                        if (def->operand == OPR_TARGET && valid_label(rest, rest_len)) {
                            ref = REF_TARGET;
                        } else if (def->operand == OPR_INT && valid_label(rest, rest_len)) {
                            ref = REF_VALUE;
                        } else {
                            fail(&p, line_no, "invalid integer '%.*s'", (int)rest_len, rest);
                            goto error;
                        }
                    }

                    if ((def->operand == OPR_LOCAL || def->op == OP_ENTER) &&
                        (value < 0 || value > 255)) {
                        fail(&p, line_no, "invalid local index");
                        goto error;
                    }
                    if (def->operand == OPR_PORT && (value < 0 || value > 255)) {
                        fail(&p, line_no, "invalid port index");
                        goto error;
                    }
                    Item *item = add_item(&p, line_no);
                    if (!item) goto oom;
                    item->op = def->op;
                    item->arg = value;
                    item->ref = ref;
                    item->label = ref == REF_NONE ? NULL : rest;
                    item->label_len = rest_len;
                }
            }
        }

        if (!newline) break;
        cursor = newline + 1u;
        if (!*cursor) break; /* trailing newline does not start a new line */
    }

    if (p.item_count == 0u) {
        fail(&p, line_no, "no instructions");
        goto error;
    }

    size_t *item_addr = calloc(p.item_count + 1u, sizeof *item_addr);
    DataBuf data;
    memset(&data, 0, sizeof data);
    if (!item_addr) goto oom;
    if (emit_data(&p, &data, item_addr) != 0) {
        free(item_addr);
        free(data.bytes);
        free(data.lines);
        goto error;
    }

    /* Layout: [entry JMP][static data][encoded instructions]. */
    size_t code_off = 3u + data.len;
    size_t total = code_off;
    for (size_t i = 0; i < p.item_count; i++) {
        if (p.items[i].kind != ITEM_INST) continue;
        item_addr[i] = VM_IMAGE_START + total;
        total += vm_inst_size((uint8_t)p.items[i].op);
    }
    item_addr[p.item_count] = VM_IMAGE_START + total;

    if (total > VM_IMAGE_MAX) {
        fail(&p, line_no, "image too large (%zu bytes)", total);
        free(item_addr);
        free(data.bytes);
        free(data.lines);
        goto error;
    }

    prog->bytes = calloc(total, 1u);
    prog->lines = calloc(total, sizeof *prog->lines);
    if (!prog->bytes || !prog->lines) {
        free(item_addr);
        free(data.bytes);
        free(data.lines);
        goto oom;
    }
    prog->len = total;
    prog->entry = 0u;

    size_t first_line = p.items[0].line;
    prog->bytes[0] = (uint8_t)OP_JMP;
    put_be(prog->bytes + 1u, (uint64_t)(VM_IMAGE_START + code_off), 2u);
    for (size_t i = 0; i < 3u; i++) prog->lines[i] = first_line;

    if (data.len > 0u) {
        memcpy(prog->bytes + 3u, data.bytes, data.len);
        memcpy(prog->lines + 3u, data.lines, data.len * sizeof *prog->lines);
    }

    size_t pos = code_off;
    for (size_t i = 0; i < p.item_count; i++) {
        Item *item = &p.items[i];
        if (item->kind != ITEM_INST) continue;

        uint8_t op = (uint8_t)item->op;
        int64_t arg = item->arg;
        if (item->ref != REF_NONE) {
            const Label *label = find_label(&p, item->label, item->label_len);
            if (!label) {
                fail(&p, item->line, "undefined label or invalid integer '%.*s'",
                     (int)item->label_len, item->label);
                free(item_addr);
                free(data.bytes);
                free(data.lines);
                goto error;
            }
            if (label->item_index > p.item_count) {
                fail(&p, item->line, "invalid label target");
                free(item_addr);
                free(data.bytes);
                free(data.lines);
                goto error;
            }
            arg = (int64_t)item_addr[label->item_index];
        }
        if (op == OP_JMP || op == OP_JMP_Z || op == OP_CALL) {
            if (arg < 0 || arg >= (int64_t)VM_RAM_SIZE) {
                fail(&p, item->line, "jump target %" PRId64 " out of range", arg);
                free(item_addr);
                free(data.bytes);
                free(data.lines);
                goto error;
            }
        }

        unsigned width = (unsigned)vm_inst_size(op) - 1u;
        prog->bytes[pos] = op;
        for (size_t k = 0u; k <= width; k++) prog->lines[pos + k] = item->line;
        pos++;
        if (width == 1u) {
            prog->bytes[pos] = (uint8_t)arg;
        } else if (width == 2u || width == 8u) {
            put_be(prog->bytes + pos, (uint64_t)arg, width);
        }
        pos += width;
    }

    free(item_addr);
    free(data.bytes);
    free(data.lines);
    free(p.src);
    free(p.items);
    free(p.labels);
    free(p.consts);
    return 0;

oom:
    fail(&p, line_no, "out of memory");
error:
    free(p.src);
    free(p.items);
    free(p.labels);
    free(p.consts);
    clear_payload(prog);
    return -1;
}

int asm_parse_file(AsmProgram *prog, const char *path)
{
    if (!prog || !path) return -1;
    asm_program_init(prog);

    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(prog->error, sizeof prog->error, "cannot open '%s': %s",
                 path, strerror(errno));
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        snprintf(prog->error, sizeof prog->error, "cannot seek '%s'", path);
        fclose(file);
        return -1;
    }
    long size = ftell(file);
    if (size < 0) {
        snprintf(prog->error, sizeof prog->error, "cannot tell '%s'", path);
        fclose(file);
        return -1;
    }
    rewind(file);

    char *buf = malloc((size_t)size + 1u);
    if (!buf) {
        snprintf(prog->error, sizeof prog->error, "out of memory");
        fclose(file);
        return -1;
    }
    size_t got = fread(buf, 1u, (size_t)size, file);
    fclose(file);
    buf[got] = '\0';

    int rc = asm_parse_string(prog, buf);
    free(buf);
    return rc;
}

/* --------------------------------------------------------------- ROM output */

int asm_make_rom(const AsmProgram *prog, uint8_t *rom, size_t rom_cap,
                 size_t *rom_len)
{
    if (!prog || !rom || !prog->bytes || prog->entry >= prog->len ||
        prog->len > VM_IMAGE_MAX) return -1;
    size_t needed = VM_ROM_HEADER_SIZE + prog->len;
    if (rom_cap < needed) return -1;

    rom[0] = 0x56; rom[1] = 0x4D; rom[2] = 0x49; rom[3] = 0x4D;
    rom[4] = VM_ROM_VERSION;
    rom[5] = 0u;
    put_be(rom + 6u, 0u, 2u);
    put_be(rom + 8u, (uint64_t)prog->len, 2u);
    put_be(rom + 10u, (uint64_t)(VM_IMAGE_START + prog->entry), 2u);
    put_be(rom + 12u, 0u, 4u);
    memcpy(rom + VM_ROM_HEADER_SIZE, prog->bytes, prog->len);

    if (rom_len) *rom_len = needed;
    return 0;
}

int asm_write_rom(const AsmProgram *prog, const char *path)
{
    if (!prog || !path) return -1;
    uint8_t rom[VM_ROM_HEADER_SIZE + VM_IMAGE_MAX];
    size_t rom_len = 0;
    if (asm_make_rom(prog, rom, sizeof rom, &rom_len) != 0) return -1;

    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    size_t written = fwrite(rom, 1u, rom_len, file);
    int rc = fclose(file);
    return (written == rom_len && rc == 0) ? 0 : -1;
}
