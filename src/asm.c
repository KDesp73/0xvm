#include "asm.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OPR_NONE = 0,
    OPR_INT,     /* integer expression */
    OPR_TARGET,  /* label or integer expression */
    OPR_FLOAT,   /* floating-point literal (raw bits are emitted) */
    OPR_LOCAL    /* local variable index */
} OperandKind;

typedef enum { REF_NONE = 0, REF_TARGET, REF_VALUE } RefKind;

typedef struct {
    const char *name;
    size_t len;
    size_t index;
} Label;

typedef struct {
    const char *name;
    size_t len;
    int64_t value;
} Constant;

typedef struct {
    VmOpcode op;
    int64_t arg;
    RefKind ref;
    const char *label;
    size_t label_len;
    size_t line;
} Pending;

typedef enum { D_BYTE, D_WORD, D_QWORD, D_STR, D_ALIGN } DataKind;

typedef struct {
    DataKind kind;
    const char *text;
    size_t len;
    size_t line;
} DataItem;

typedef struct {
    AsmProgram *out;
    char *src;
    Label *labels;
    size_t label_count, label_cap;
    Constant *consts;
    size_t const_count, const_cap;
    Pending *pending;
    size_t pending_count, pending_cap;
    DataItem *data;
    size_t data_count, data_cap;
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
    {"PRINT",   OP_PRINT,    OPR_NONE},
    {"MIGRATE", OP_MIGRATE,  OPR_NONE},
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

    /* Heap access */
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
    {"FPRINT",  OP_FPRINT,   OPR_NONE},

    /* Host I/O */
    {"PRINT_STR",  OP_PRINT_STR,  OPR_NONE},
    {"PRINT_CHAR", OP_PRINT_CHAR, OPR_NONE},
    {"READ_INT",   OP_READ_INT,   OPR_NONE},
    {"READ_STR",   OP_READ_STR,   OPR_NONE}
};

void asm_program_init(AsmProgram *prog)
{
    if (!prog) return;
    memset(prog, 0, sizeof(*prog));
}

void asm_program_free(AsmProgram *prog)
{
    if (!prog) return;
    free(prog->insts);
    free(prog->lines);
    prog->insts = NULL;
    prog->lines = NULL;
    prog->count = 0;
}

size_t asm_line_of(const AsmProgram *prog, size_t ip)
{
    if (!prog || !prog->lines || ip >= prog->count) return 0u;
    return prog->lines[ip];
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

static int ex_lookup(const Parser *p, const char *name, size_t len, int64_t *out)
{
    for (size_t i = 0; i < p->const_count; i++) {
        if (p->consts[i].len == len && memcmp(p->consts[i].name, name, len) == 0) {
            *out = p->consts[i].value;
            return 0;
        }
    }
    for (size_t i = 0; i < p->label_count; i++) {
        if (p->labels[i].len == len && memcmp(p->labels[i].name, name, len) == 0) {
            *out = (int64_t)p->labels[i].index;
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
        /* INT64_MIN cannot be written as a positive int64 literal. */
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

static int add_label(Parser *p, const char *name, size_t len, size_t index)
{
    if (find_label(p, name, len)) return -2; /* duplicate */
    if (grow((void **)&p->labels, &p->label_cap, p->label_count + 1u,
             sizeof *p->labels) != 0) return -1;
    Label *label = &p->labels[p->label_count++];
    label->name = name;
    label->len = len;
    label->index = index;
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

static int add_pending(Parser *p, VmOpcode op, int64_t arg, size_t line)
{
    if (grow((void **)&p->pending, &p->pending_cap, p->pending_count + 1u,
             sizeof *p->pending) != 0) return -1;
    Pending *inst = &p->pending[p->pending_count++];
    memset(inst, 0, sizeof *inst);
    inst->op = op;
    inst->arg = arg;
    inst->line = line;
    return 0;
}

static int add_data(Parser *p, DataKind kind, const char *text, size_t len, size_t line)
{
    if (grow((void **)&p->data, &p->data_cap, p->data_count + 1u,
             sizeof *p->data) != 0) return -1;
    DataItem *item = &p->data[p->data_count++];
    item->kind = kind;
    item->text = text;
    item->len = len;
    item->line = line;
    return 0;
}

/* ---------------------------------------------------------- data emission */

typedef struct {
    uint8_t *bytes;
    size_t len, cap;
} DataBuf;

static int db_append(DataBuf *db, const uint8_t *src, size_t n)
{
    if (grow((void **)&db->bytes, &db->cap, db->len + n, 1u) != 0) return -1;
    memcpy(db->bytes + db->len, src, n);
    db->len += n;
    return 0;
}

static int db_byte(DataBuf *db, uint8_t b)
{
    return db_append(db, &b, 1u);
}

static int db_number(DataBuf *db, uint64_t v, unsigned width)
{
    for (unsigned i = 0; i < width; i++) {
        if (db_byte(db, (uint8_t)(v >> (8u * (width - 1u - i)))) != 0) return -1;
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
        if (db_byte(db, c) != 0) return fail(p, line, "out of memory");
    }
    return 0;
}

static int emit_data(Parser *p, DataBuf *db)
{
    for (size_t i = 0; i < p->data_count; i++) {
        const DataItem *item = &p->data[i];
        if (item->kind == D_STR) {
            if (emit_string(p, db, item->text, item->len, item->line) != 0) return -1;
            continue;
        }
        if (item->kind == D_ALIGN) {
            int64_t n;
            if (eval_expr(p, item->text, item->len, &n) != 0 || n <= 0 || n > 4096) {
                return fail(p, item->line, "invalid ALIGN value");
            }
            while (db->len % (size_t)n != 0u) {
                if (db_byte(db, 0u) != 0) return fail(p, item->line, "out of memory");
            }
            continue;
        }
        unsigned width = item->kind == D_BYTE ? 1u : (item->kind == D_WORD ? 2u : 8u);
        const char *cur = item->text;
        const char *end = item->text + item->len;
        if (cur >= end) return fail(p, item->line, "directive requires a value");
        for (;;) {
            const char *start = cur;
            while (cur < end && *cur != ',' && !isspace((unsigned char)*cur)) cur++;
            int64_t value;
            if (eval_expr(p, start, (size_t)(cur - start), &value) != 0) {
                return fail(p, item->line, "invalid integer '%.*s'",
                            (int)(cur - start), start);
            }
            if (db_number(db, (uint64_t)value, width) != 0) {
                return fail(p, item->line, "out of memory");
            }
            while (cur < end && (isspace((unsigned char)*cur) || *cur == ',')) cur++;
            if (cur >= end) break;
        }
    }
    return 0;
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

        /* Labels may share a line with an instruction: "loop: DUP". */
        while (*s && *s != '"') {
            size_t n = 0;
            while (s[n] && s[n] != ':' && !isspace((unsigned char)s[n])) n++;
            if (s[n] != ':') break;
            if (!valid_label(s, n)) {
                fail(&p, line_no, "invalid label name");
                goto error;
            }
            int rc = add_label(&p, s, n, p.pending_count);
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
                /* Data directives consume the whole rest of the line so that
                 * comma-separated expressions and strings stay intact. */
                size_t text_len = strlen(rest);
                if (add_data(&p, kind, rest, text_len, line_no) != 0) {
                    fail(&p, line_no, "out of memory");
                    goto error;
                }
            } else {
                const OpDef *def = find_op(s, mn_len);
                if (!def) {
                    /* Pseudo-ops expand into several real instructions. */
                    if (token_eq_ci(s, mn_len, "INC") ||
                        token_eq_ci(s, mn_len, "DEC") ||
                        token_eq_ci(s, mn_len, "NEG")) {
                        if (rest_len != 0u) {
                            fail(&p, line_no, "%.*s takes no operand", (int)mn_len, s);
                            goto error;
                        }
                        if (token_eq_ci(s, mn_len, "INC")) {
                            if (add_pending(&p, OP_PUSH, 1, line_no) != 0 ||
                                add_pending(&p, OP_ADD, 0, line_no) != 0) goto oom;
                        } else if (token_eq_ci(s, mn_len, "DEC")) {
                            if (add_pending(&p, OP_PUSH, 1, line_no) != 0 ||
                                add_pending(&p, OP_SUB, 0, line_no) != 0) goto oom;
                        } else {
                            if (add_pending(&p, OP_PUSH, 0, line_no) != 0 ||
                                add_pending(&p, OP_SWAP, 0, line_no) != 0 ||
                                add_pending(&p, OP_SUB, 0, line_no) != 0) goto oom;
                        }
                    } else {
                        fail(&p, line_no, "unknown instruction '%.*s'", (int)mn_len, s);
                        goto error;
                    }
                } else if (def->operand == OPR_NONE) {
                    if (rest_len != 0u) {
                        fail(&p, line_no, "%s takes no operand", def->name);
                        goto error;
                    }
                    if (add_pending(&p, def->op, 0, line_no) != 0) goto oom;
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

                    if (grow((void **)&p.pending, &p.pending_cap,
                             p.pending_count + 1u, sizeof *p.pending) != 0) goto oom;
                    Pending *inst = &p.pending[p.pending_count++];
                    memset(inst, 0, sizeof *inst);
                    inst->op = def->op;
                    inst->arg = value;
                    inst->ref = ref;
                    inst->label = ref == REF_NONE ? NULL : rest;
                    inst->label_len = rest_len;
                    inst->line = line_no;

                    if (def->operand == OPR_LOCAL && value < 0) {
                        fail(&p, line_no, "invalid local index");
                        goto error;
                    }
                }
            }
        }

        if (!newline) break;
        cursor = newline + 1u;
        if (!*cursor) break; /* trailing newline does not start a new line */
    }

    if (p.pending_count == 0u) {
        fail(&p, line_no, "no instructions");
        goto error;
    }

    DataBuf data;
    memset(&data, 0, sizeof data);
    if (emit_data(&p, &data) != 0) {
        free(data.bytes);
        goto error;
    }

    /* Data directives become an initialization prologue executed before the
     * program body. Every source instruction index is shifted by its length. */
    size_t prologue = 0;
    if (data.len > 0u) prologue = 3u + 3u * data.len;

    size_t total = prologue + p.pending_count;
    prog->insts = calloc(total, sizeof *prog->insts);
    prog->lines = calloc(total, sizeof *prog->lines);
    if (!prog->insts || !prog->lines) {
        free(data.bytes);
        goto oom;
    }
    prog->count = total;

    size_t pos = 0;
    size_t data_line = p.data_count > 0u ? p.data[0].line : 1u;
    if (data.len > 0u) {
        prog->insts[pos++] = (VmInst){OP_PUSH, (int64_t)data.len};
        prog->lines[pos - 1u] = data_line;
        prog->insts[pos++] = (VmInst){OP_ALLOC, 0};
        prog->lines[pos - 1u] = data_line;
        prog->insts[pos++] = (VmInst){OP_POP, 0};
        prog->lines[pos - 1u] = data_line;
        for (size_t i = 0; i < data.len; i++) {
            prog->insts[pos++] = (VmInst){OP_PUSH, (int64_t)data.bytes[i]};
            prog->lines[pos - 1u] = data_line;
            prog->insts[pos++] = (VmInst){OP_PUSH, (int64_t)i};
            prog->lines[pos - 1u] = data_line;
            prog->insts[pos++] = (VmInst){OP_STORE8, 0};
            prog->lines[pos - 1u] = data_line;
        }
    }
    free(data.bytes);

    for (size_t i = 0; i < p.pending_count; i++) {
        Pending *src = &p.pending[i];
        VmInst inst = {src->op, src->arg};
        if (src->ref != REF_NONE) {
            const Label *label = find_label(&p, src->label, src->label_len);
            if (!label) {
                fail(&p, src->line, "undefined label or invalid integer '%.*s'",
                     (int)src->label_len, src->label);
                goto error;
            }
            inst.arg = (int64_t)label->index + (int64_t)prologue;
        } else if (inst.op == OP_JMP || inst.op == OP_JMP_Z || inst.op == OP_CALL) {
            inst.arg += (int64_t)prologue;
        }
        if ((inst.op == OP_JMP || inst.op == OP_JMP_Z || inst.op == OP_CALL) &&
            (inst.arg < 0 || (uint64_t)inst.arg >= total)) {
            fail(&p, src->line, "jump target %" PRId64 " out of range", inst.arg);
            goto error;
        }
        prog->insts[pos] = inst;
        prog->lines[pos] = src->line;
        pos++;
    }

    free(p.src);
    free(p.labels);
    free(p.consts);
    free(p.pending);
    free(p.data);
    return 0;

oom:
    fail(&p, line_no, "out of memory");
error:
    free(p.src);
    free(p.labels);
    free(p.consts);
    free(p.pending);
    free(p.data);
    asm_program_free(prog);
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
