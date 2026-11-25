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
    const char *path;   /* display path for diagnostics (borrowed) */
} Item;

/* A unit of input: the main file, an included file, or a macro expansion. */
typedef struct {
    char *text;          /* owned, mutable line buffer */
    char *cursor;        /* current line in `text` */
    char *next;          /* first byte of the following line */
    size_t line_no;      /* 1-based line number within this unit */
    size_t call_line;    /* line in the parent unit that pushed this frame */
    size_t stamp_line;   /* non-zero: stamp generated items with this line */
    const char *path;    /* owned display path, or NULL */
    unsigned depth;      /* include/macro nesting depth, for cycle limits */
} SourceFrame;

typedef struct {
    bool parent;         /* enclosing branch was active */
    bool active;         /* this branch is being assembled */
    bool taken;          /* a branch of this IF has already been taken */
    bool seen_else;
    size_t line;
} CondFrame;

typedef struct {
    const char *name;
    size_t name_len;
    char **params;       /* owned strings; names without the leading '%' */
    size_t param_count;
    char **lines;        /* borrowed pointers into owned source buffers */
    size_t line_count, line_cap;
    size_t def_line;
} Macro;

typedef struct {
    AsmProgram *out;
    char *src;
    const char *const *include_dirs; /* caller-owned extra INCLUDE search paths */
    size_t include_dir_count;
    char **owned_texts;  /* every owned source buffer, freed at the end */
    size_t text_count, text_cap;
    char **owned_paths;
    size_t path_count, path_cap;
    Item *items;
    size_t item_count, item_cap;
    Label *labels;
    size_t label_count, label_cap;
    Constant *consts;
    size_t const_count, const_cap;
    Macro *macros;
    size_t macro_count, macro_cap;
    Macro *defining;
    unsigned macro_serial;
    SourceFrame *frames;
    size_t frame_count, frame_cap;
    CondFrame *conds;
    size_t cond_count, cond_cap;
    bool emit;
    const char *err_path;
    size_t err_line;
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

    /* Procedures and locals */
    {"CALL",    OP_CALL,     OPR_TARGET},
    {"CALL_I",  OP_CALL_I,   OPR_NONE},
    {"RET",     OP_RET,      OPR_NONE},
    {"ENTER",   OP_ENTER,    OPR_INT},
    {"LOCAL_GET", OP_LOCAL_GET, OPR_LOCAL},
    {"LOCAL_SET", OP_LOCAL_SET, OPR_LOCAL},
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
    char error[sizeof prog->error];
    size_t error_line = prog->error_line;
    memcpy(error, prog->error, sizeof error);
    free(prog->bytes);
    free(prog->lines);
    memset(prog, 0, sizeof(*prog));
    memcpy(prog->error, error, sizeof error);
    prog->error_line = error_line;
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
    int n = p->err_path && *p->err_path
        ? snprintf(dst, cap, "%s:%zu: ", p->err_path, line)
        : snprintf(dst, cap, "line %zu: ", line);
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

/* Keep every source buffer alive until emission is complete. Items and macro
 * bodies borrow pointers into these buffers. */
static char *own_text(Parser *p, const char *text, size_t len)
{
    char *copy = malloc(len + 1u);
    if (!copy) return NULL;
    memcpy(copy, text, len);
    copy[len] = '\0';
    if (grow((void **)&p->owned_texts, &p->text_cap,
             p->text_count + 1u, sizeof *p->owned_texts) != 0) {
        free(copy);
        return NULL;
    }
    p->owned_texts[p->text_count++] = copy;
    return copy;
}

static char *own_path(Parser *p, const char *path)
{
    size_t len = strlen(path);
    char *copy = malloc(len + 1u);
    if (!copy) return NULL;
    memcpy(copy, path, len + 1u);
    if (grow((void **)&p->owned_paths, &p->path_cap,
             p->path_count + 1u, sizeof *p->owned_paths) != 0) {
        free(copy);
        return NULL;
    }
    p->owned_paths[p->path_count++] = copy;
    return copy;
}

static int push_frame(Parser *p, char *text, const char *path,
                      size_t call_line, size_t stamp_line, unsigned depth)
{
    if (grow((void **)&p->frames, &p->frame_cap, p->frame_count + 1u,
             sizeof *p->frames) != 0) return -1;
    SourceFrame *f = &p->frames[p->frame_count++];
    f->text = text;
    f->cursor = text;
    f->next = text;
    f->line_no = 0u;
    f->call_line = call_line;
    f->stamp_line = stamp_line;
    f->path = path;
    f->depth = depth;
    return 0;
}

/* Fetch the next line from the innermost frame. Returns false at EOF. */
static bool next_line(Parser *p, char **out, SourceFrame **out_frame)
{
    while (p->frame_count > 0u) {
        SourceFrame *f = &p->frames[p->frame_count - 1u];
        char *line = f->cursor;
        if (!*line) { p->frame_count--; continue; }

        char *newline = strchr(line, '\n');
        if (newline) {
            *newline = '\0';
            f->next = newline + 1u;
        } else {
            f->next = line + strlen(line);
        }
        f->cursor = line;
        f->line_no++;
        p->err_path = f->path;
        p->err_line = f->line_no;
        *out = line;
        *out_frame = f;
        return true;
    }
    return false;
}

static void advance_line(SourceFrame *f)
{
    f->cursor = f->next;
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
static int ex_cmp(Expr *e, int64_t *out);

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

static int ex_add(Expr *e, int64_t *out)
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

static bool ex_take2(Expr *e, char a, char b)
{
    ex_ws(e);
    if (e->cur + 1u >= e->end || e->cur[0] != a || e->cur[1] != b) return false;
    e->cur += 2u;
    return true;
}

static int ex_shift(Expr *e, int64_t *out)
{
    if (ex_add(e, out) != 0) return -1;
    for (;;) {
        bool left = ex_take2(e, '<', '<');
        bool right = !left && ex_take2(e, '>', '>');
        if (!left && !right) return 0;
        int64_t rhs;
        if (ex_add(e, &rhs) != 0) return -1;
        uint64_t value = (uint64_t)*out;
        uint64_t count = (uint64_t)rhs;
        if (count >= 64u) *out = 0;
        else *out = (int64_t)(left ? value << count : value >> count);
    }
}

static int ex_bitand(Expr *e, int64_t *out)
{
    if (ex_cmp(e, out) != 0) return -1;
    for (;;) {
        ex_ws(e);
        if (e->cur >= e->end || *e->cur != '&') return 0;
        e->cur++;
        int64_t rhs;
        if (ex_cmp(e, &rhs) != 0) return -1;
        *out = (int64_t)((uint64_t)*out & (uint64_t)rhs);
    }
}

static int ex_bitxor(Expr *e, int64_t *out)
{
    if (ex_bitand(e, out) != 0) return -1;
    for (;;) {
        ex_ws(e);
        if (e->cur >= e->end || *e->cur != '^') return 0;
        e->cur++;
        int64_t rhs;
        if (ex_bitand(e, &rhs) != 0) return -1;
        *out = (int64_t)((uint64_t)*out ^ (uint64_t)rhs);
    }
}

static int ex_bitor(Expr *e, int64_t *out)
{
    if (ex_bitxor(e, out) != 0) return -1;
    for (;;) {
        ex_ws(e);
        if (e->cur >= e->end || *e->cur != '|') return 0;
        e->cur++;
        int64_t rhs;
        if (ex_bitxor(e, &rhs) != 0) return -1;
        *out = (int64_t)((uint64_t)*out | (uint64_t)rhs);
    }
}

static int ex_cmp(Expr *e, int64_t *out)
{
    if (ex_shift(e, out) != 0) return -1;
    for (;;) {
        ex_ws(e);
        if (e->cur >= e->end) return 0;
        unsigned op;
        if (ex_take2(e, '=', '=')) op = 1u;
        else if (ex_take2(e, '!', '=')) op = 2u;
        else if (ex_take2(e, '<', '=')) op = 4u;
        else if (ex_take2(e, '>', '=')) op = 6u;
        else if (*e->cur == '<' &&
                 !(e->cur + 1u < e->end && e->cur[1] == '<')) { e->cur++; op = 3u; }
        else if (*e->cur == '>' &&
                 !(e->cur + 1u < e->end && e->cur[1] == '>')) { e->cur++; op = 5u; }
        else return 0;

        int64_t rhs;
        if (ex_shift(e, &rhs) != 0) return -1;
        int64_t lhs = *out;
        switch (op) {
            case 1u: *out = lhs == rhs; break;
            case 2u: *out = lhs != rhs; break;
            case 3u: *out = lhs < rhs;  break;
            case 4u: *out = lhs <= rhs; break;
            case 5u: *out = lhs > rhs;  break;
            default: *out = lhs >= rhs; break;
        }
    }
}

static int ex_expr(Expr *e, int64_t *out)
{
    return ex_bitor(e, out);
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

static const Macro *find_macro(const Parser *p, const char *name, size_t len)
{
    for (size_t i = 0; i < p->macro_count; i++) {
        const Macro *m = &p->macros[i];
        if (m->name_len != len) continue;
        size_t j = 0u;
        while (j < len &&
               tolower((unsigned char)name[j]) == tolower((unsigned char)m->name[j])) j++;
        if (j == len) return m;
    }
    return NULL;
}

/* Macro names may not hide instructions or directives. Readable aliases and
 * pseudo-ops live in the opt-in stdlib and can be replaced by user macros. */
static bool is_builtin_name(const char *s, size_t len)
{
    static const char *const names[] = {
        "EQU", "DB", "DW", "DQ", "STR", "ALIGN", "INCLUDE",
        "IF", "IFDEF", "IFNDEF", "ELIF", "ELSE", "ENDIF",
        "MACRO", "ENDMACRO", "ENDM"
    };
    if (find_op(s, len)) return true;
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        if (token_eq_ci(s, len, names[i])) return true;
    }
    return false;
}

static bool is_defined(const Parser *p, const char *name, size_t len)
{
    for (size_t i = 0; i < p->const_count; i++) {
        if (p->consts[i].len == len &&
            memcmp(p->consts[i].name, name, len) == 0) return true;
    }
    return find_macro(p, name, len) != NULL;
}

static int push_cond(Parser *p, bool parent, bool active, size_t line)
{
    if (grow((void **)&p->conds, &p->cond_cap, p->cond_count + 1u,
             sizeof *p->conds) != 0) return -1;
    CondFrame *c = &p->conds[p->cond_count++];
    c->parent = parent;
    c->active = active;
    c->taken = active;
    c->seen_else = false;
    c->line = line;
    p->emit = active;
    return 0;
}

static void pop_cond(Parser *p)
{
    if (p->cond_count == 0u) return;
    p->cond_count--;
    p->emit = p->cond_count == 0u
        ? true
        : p->conds[p->cond_count - 1u].active;
}

static int split_params(const char *s, char ***out, size_t *count)
{
    *out = NULL;
    *count = 0u;
    while (*s) {
        while (*s && (isspace((unsigned char)*s) || *s == ',')) s++;
        if (!*s) break;
        const char *start = s;
        while (*s && !isspace((unsigned char)*s) && *s != ',') s++;
        size_t len = (size_t)(s - start);
        char *copy = malloc(len + 1u);
        if (!copy) return -1;
        memcpy(copy, start, len);
        copy[len] = '\0';
        char **tmp = realloc(*out, (*count + 2u) * sizeof *tmp);
        if (!tmp) { free(copy); return -1; }
        *out = tmp;
        (*out)[(*count)++] = copy;
        (*out)[*count] = NULL;
    }
    return 0;
}

/* Split macro arguments on top-level commas, respecting quotes and parens. */
static int split_args(const char *s, char ***out, size_t *count)
{
    *out = NULL;
    *count = 0u;
    while (*s) {
        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s) break;
        const char *start = s;
        bool quoted = false;
        unsigned depth = 0u;
        for (; *s; s++) {
            if (quoted) {
                if (*s == '\\' && s[1]) { s++; continue; }
                if (*s == '"') quoted = false;
                continue;
            }
            if (*s == '"') { quoted = true; continue; }
            if (*s == '(' || *s == '[') depth++;
            else if ((*s == ')' || *s == ']') && depth > 0u) depth--;
            else if (*s == ',' && depth == 0u) break;
        }
        size_t len = (size_t)(s - start);
        while (len > 0u && isspace((unsigned char)start[len - 1u])) len--;
        char *copy = malloc(len + 1u);
        if (!copy) return -1;
        memcpy(copy, start, len);
        copy[len] = '\0';
        char **tmp = realloc(*out, (*count + 2u) * sizeof *tmp);
        if (!tmp) { free(copy); return -1; }
        *out = tmp;
        (*out)[(*count)++] = copy;
        (*out)[*count] = NULL;
        if (*s == ',') s++;
    }
    return 0;
}

static void free_strings(char **items)
{
    if (!items) return;
    for (size_t i = 0u; items[i]; i++) free(items[i]);
    free(items);
}

static bool macro_param_index(const Macro *m, const char *name, size_t len,
                              size_t *index)
{
    for (size_t i = 0u; i < m->param_count; i++) {
        if (strlen(m->params[i]) == len &&
            memcmp(m->params[i], name, len) == 0) {
            *index = i;
            return true;
        }
    }
    return false;
}

static int add_macro(Parser *p, const char *name, size_t name_len,
                     char **params, size_t param_count, size_t line)
{
    if (find_macro(p, name, name_len) || is_builtin_name(name, name_len))
        return -2;
    if (grow((void **)&p->macros, &p->macro_cap, p->macro_count + 1u,
             sizeof *p->macros) != 0) return -1;
    Macro *m = &p->macros[p->macro_count++];
    memset(m, 0, sizeof *m);
    m->name = name;
    m->name_len = name_len;
    m->params = params;
    m->param_count = param_count;
    m->def_line = line;
    return 0;
}

static int quoted_path(const char *text, size_t len, char **out)
{
    if (len < 2u || text[0] != '"' || text[len - 1u] != '"') return -1;
    char *buf = malloc(len);
    if (!buf) return -1;
    size_t n = 0u;
    for (size_t i = 1u; i + 1u < len; i++) {
        if (text[i] == '\\' && i + 2u < len) {
            char c = text[++i];
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            buf[n++] = c;
        } else {
            buf[n++] = text[i];
        }
    }
    buf[n] = '\0';
    *out = buf;
    return 0;
}

static bool path_exists(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

/* Returns an owned candidate path; caller frees it after use. */
static char *join_path(const char *dir, const char *spec)
{
    size_t dir_len = strlen(dir);
    size_t spec_len = strlen(spec);
    bool slash = dir_len != 0u && dir[dir_len - 1u] == '/';
    char *buf = malloc(dir_len + (slash ? 0u : 1u) + spec_len + 1u);
    if (!buf) return NULL;
    memcpy(buf, dir, dir_len);
    if (!slash) buf[dir_len++] = '/';
    memcpy(buf + dir_len, spec, spec_len + 1u);
    return buf;
}

static const char *resolve_include(Parser *p, const SourceFrame *frame,
                                   const char *spec)
{
    if (*spec == '/') return own_path(p, spec);

    /* 1. relative to the including file (existing behaviour), 2. explicit -I
     * directories, 3. the built-in stdlib. Keep the first candidate as the
     * fallback so a missing file reports a useful path. */
    char *fallback = NULL;
    if (frame && frame->path) {
        const char *slash = strrchr(frame->path, '/');
        size_t dir_len = slash ? (size_t)(slash - frame->path) + 1u : 0u;
        size_t spec_len = strlen(spec);
        char *buf = malloc(dir_len + spec_len + 1u);
        if (!buf) return NULL;
        memcpy(buf, frame->path, dir_len);
        memcpy(buf + dir_len, spec, spec_len + 1u);
        if (path_exists(buf)) {
            const char *path = own_path(p, buf);
            free(buf);
            return path;
        }
        fallback = buf;
    }

    for (size_t i = 0u; i < p->include_dir_count; i++) {
        char *buf = join_path(p->include_dirs[i], spec);
        if (!buf) { free(fallback); return NULL; }
        if (path_exists(buf)) {
            const char *path = own_path(p, buf);
            free(buf);
            free(fallback);
            return path;
        }
        free(buf);
    }

#ifdef ASM_STDLIB_DIR
    {
        char *buf = join_path(ASM_STDLIB_DIR, spec);
        if (!buf) { free(fallback); return NULL; }
        if (path_exists(buf)) {
            const char *path = own_path(p, buf);
            free(buf);
            free(fallback);
            return path;
        }
        free(buf);
    }
#endif

    if (fallback) {
        const char *path = own_path(p, fallback);
        free(fallback);
        return path;
    }
    return own_path(p, spec);
}

static bool path_in_stack(const Parser *p, const char *path)
{
    for (size_t i = 0u; i < p->frame_count; i++) {
        if (p->frames[i].path && strcmp(p->frames[i].path, path) == 0) return true;
    }
    return false;
}

static char *read_file_text(Parser *p, const char *path, size_t *len,
                            size_t line)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        fail(p, line, "cannot open include '%s': %s", path, strerror(errno));
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fail(p, line, "cannot seek include '%s'", path);
        fclose(file);
        return NULL;
    }
    long size = ftell(file);
    if (size < 0) {
        fail(p, line, "cannot tell include '%s'", path);
        fclose(file);
        return NULL;
    }
    rewind(file);
    char *buf = malloc((size_t)size + 1u);
    if (!buf) {
        fail(p, line, "out of memory");
        fclose(file);
        return NULL;
    }
    size_t got = fread(buf, 1u, (size_t)size, file);
    fclose(file);
    buf[got] = '\0';
    *len = got;
    return buf;
}

static Item *add_item(Parser *p, size_t line)
{
    if (grow((void **)&p->items, &p->item_cap, p->item_count + 1u,
             sizeof *p->items) != 0) return NULL;
    Item *item = &p->items[p->item_count++];
    memset(item, 0, sizeof(*item));
    item->line = line;
    item->path = p->err_path;
    return item;
}

/* ------------------------------------------------------- macro expansion */

typedef struct {
    char *data;
    size_t len, cap;
} StrBuf;

static int sb_append(StrBuf *sb, const char *text, size_t len)
{
    if (grow((void **)&sb->data, &sb->cap, sb->len + len + 1u, 1u) != 0)
        return -1;
    memcpy(sb->data + sb->len, text, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return 0;
}

static int sb_char(StrBuf *sb, char c)
{
    return sb_append(sb, &c, 1u);
}

/* Expand %N / %name arguments and %% into a per-invocation label prefix. */
static int substitute_line(Parser *p, const Macro *m, char **args, size_t argc,
                           const char *prefix, const char *line, size_t line_no,
                           StrBuf *out)
{
    for (const char *cur = line; *cur;) {
        if (*cur != '%') {
            if (sb_char(out, *cur++) != 0) return -1;
            continue;
        }
        if (cur[1] == '%') {
            if (sb_append(out, prefix, strlen(prefix)) != 0) return -1;
            cur += 2u;
            continue;
        }
        if (isdigit((unsigned char)cur[1])) {
            size_t index = 0u;
            const char *q = cur + 1u;
            while (isdigit((unsigned char)*q)) {
                index = index * 10u + (size_t)(*q - '0');
                q++;
            }
            if (index == 0u || index > argc) {
                return fail(p, line_no, "unknown macro parameter '%%%zu'", index);
            }
            if (sb_append(out, args[index - 1u], strlen(args[index - 1u])) != 0)
                return -1;
            cur = q;
            continue;
        }
        if (!(isalpha((unsigned char)cur[1]) || cur[1] == '_')) {
            if (sb_char(out, '%') != 0) return -1;
            cur++;
            continue;
        }
        const char *q = cur + 1u;
        while (isalnum((unsigned char)*q) || *q == '_') q++;
        size_t name_len = (size_t)(q - (cur + 1u));
        size_t index;
        if (!macro_param_index(m, cur + 1u, name_len, &index)) {
            return fail(p, line_no, "unknown macro parameter '%.*s'",
                        (int)name_len, cur + 1u);
        }
        if (sb_append(out, args[index], strlen(args[index])) != 0) return -1;
        cur = q;
    }
    return 0;
}

static int expand_macro(Parser *p, const Macro *m, char **args, size_t argc,
                        size_t call_line, size_t depth)
{
    char prefix[32];
    snprintf(prefix, sizeof prefix, "__m%u_", ++p->macro_serial);

    StrBuf body = {0};
    for (size_t i = 0u; i < m->line_count; i++) {
        if (substitute_line(p, m, args, argc, prefix, m->lines[i],
                            m->def_line + i + 1u, &body) != 0) {
            free(body.data);
            return -1;
        }
        if (sb_char(&body, '\n') != 0) {
            free(body.data);
            return fail(p, call_line, "out of memory");
        }
    }
    if (!body.data) {
        body.data = malloc(1u);
        if (!body.data) return fail(p, call_line, "out of memory");
        body.data[0] = '\0';
    }

    char *text = own_text(p, body.data, body.len);
    free(body.data);
    if (!text) return fail(p, call_line, "out of memory");

    char label[64];
    snprintf(label, sizeof label, "<macro %.*s>", (int)m->name_len, m->name);
    const char *path = own_path(p, label);
    if (!path) return fail(p, call_line, "out of memory");
    return push_frame(p, text, path, call_line, call_line, depth + 1u);
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
        p->err_path = item->path;
        p->err_line = item->line;
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

/* Handle one source line. May push an include or macro frame; the main loop
 * processes the new frame before continuing with the caller. */
static int parse_line(Parser *p, char *line, SourceFrame *frame)
{
    size_t err_line = frame->line_no;
    size_t item_line = frame->stamp_line ? frame->stamp_line : err_line;

    strip_comment(line);
    char *s = skip_ws(line);
    trim_end(s);

    /* While defining a macro, store raw lines verbatim (minus comments). */
    if (p->defining) {
        if (*s) {
            size_t n = token_len(s);
            if (token_eq_ci(s, n, "ENDMACRO") || token_eq_ci(s, n, "ENDM")) {
                if (*skip_ws(s + n)) return fail(p, err_line, "ENDMACRO takes no operand");
                p->defining = NULL;
                return 0;
            }
            if (token_eq_ci(s, n, "MACRO"))
                return fail(p, err_line, "nested MACRO definition");
            char **tmp = realloc(p->defining->lines,
                                 (p->defining->line_count + 1u) * sizeof *tmp);
            if (!tmp) return fail(p, err_line, "out of memory");
            p->defining->lines = tmp;
            p->defining->lines[p->defining->line_count++] = s;
        }
        return 0;
    }

    /* Conditional directives are handled even when the enclosing branch is
     * skipped, so nested IF/ELSE/ENDIF pairs stay balanced. */
    if (*s) {
        size_t mn_len = token_len(s);
        char *rest = skip_ws(s + mn_len);
        size_t rest_len = strlen(rest);

        if (token_eq_ci(s, mn_len, "IF") ||
            token_eq_ci(s, mn_len, "IFDEF") || token_eq_ci(s, mn_len, "IFNDEF")) {
            bool defined = false;
            bool parent = p->emit;
            bool cond = false;
            if (token_eq_ci(s, mn_len, "IF")) {
                int64_t value;
                if (rest_len == 0u || eval_expr(p, rest, rest_len, &value) != 0)
                    return fail(p, err_line, "IF requires an expression");
                cond = value != 0;
            } else {
                if (rest_len == 0u || !valid_label(rest, rest_len))
                    return fail(p, err_line, "%.*s requires a name",
                                (int)mn_len, s);
                defined = is_defined(p, rest, rest_len);
                cond = token_eq_ci(s, mn_len, "IFDEF") ? defined : !defined;
            }
            if (push_cond(p, parent, parent && cond, err_line) != 0)
                return fail(p, err_line, "out of memory");
            return 0;
        }

        if (token_eq_ci(s, mn_len, "ELIF")) {
            if (p->cond_count == 0u) return fail(p, err_line, "ELIF without IF");
            CondFrame *c = &p->conds[p->cond_count - 1u];
            if (c->seen_else) return fail(p, err_line, "ELIF after ELSE");
            bool cond = false;
            if (c->parent && !c->taken) {
                int64_t value;
                if (rest_len == 0u || eval_expr(p, rest, rest_len, &value) != 0)
                    return fail(p, err_line, "ELIF requires an expression");
                cond = value != 0;
            }
            c->active = c->parent && !c->taken && cond;
            c->taken = c->taken || c->active;
            p->emit = c->active;
            return 0;
        }

        if (token_eq_ci(s, mn_len, "ELSE")) {
            if (p->cond_count == 0u) return fail(p, err_line, "ELSE without IF");
            CondFrame *c = &p->conds[p->cond_count - 1u];
            if (c->seen_else) return fail(p, err_line, "duplicate ELSE");
            if (rest_len != 0u) return fail(p, err_line, "ELSE takes no operand");
            c->seen_else = true;
            c->active = c->parent && !c->taken;
            c->taken = c->taken || c->active;
            p->emit = c->active;
            return 0;
        }

        if (token_eq_ci(s, mn_len, "ENDIF")) {
            if (p->cond_count == 0u) return fail(p, err_line, "ENDIF without IF");
            if (rest_len != 0u) return fail(p, err_line, "ENDIF takes no operand");
            pop_cond(p);
            return 0;
        }
    }

    if (!p->emit || !*s) return 0;

    /* Labels may share a line with an instruction or data directive. */
    while (*s && *s != '"') {
        size_t n = 0;
        while (s[n] && s[n] != ':' && !isspace((unsigned char)s[n])) n++;
        if (s[n] != ':') break;
        if (!valid_label(s, n)) return fail(p, err_line, "invalid label name");
        int rc = add_label(p, s, n, p->item_count);
        if (rc == -2) return fail(p, err_line, "duplicate label '%.*s'", (int)n, s);
        if (rc != 0) return fail(p, err_line, "out of memory");
        s = skip_ws(s + n + 1u);
    }
    if (!*s) return 0;

    size_t mn_len = token_len(s);
    char *rest = skip_ws(s + mn_len);
    size_t rest_len = strlen(rest);

    if (token_eq_ci(s, mn_len, "MACRO")) {
        const char *name = rest;
        size_t name_len = token_len(name);
        if (name_len == 0u || !valid_label(name, name_len))
            return fail(p, err_line, "MACRO requires a name");
        char **params = NULL;
        size_t param_count = 0u;
        if (split_params(skip_ws(rest + name_len), &params, &param_count) != 0) {
            free_strings(params);
            return fail(p, err_line, "out of memory");
        }
        for (size_t i = 0u; i < param_count; i++) {
            if (!valid_label(params[i], strlen(params[i]))) {
                free_strings(params);
                return fail(p, err_line, "invalid macro parameter '%s'", params[i]);
            }
        }
        int rc = add_macro(p, name, name_len, params, param_count, err_line);
        if (rc == -2) {
            free_strings(params);
            return fail(p, err_line, "duplicate or reserved macro name '%.*s'",
                        (int)name_len, name);
        }
        if (rc != 0) {
            free_strings(params);
            return fail(p, err_line, "out of memory");
        }
        p->defining = &p->macros[p->macro_count - 1u];
        return 0;
    }

    if (token_eq_ci(s, mn_len, "INCLUDE")) {
        char *spec = NULL;
        if (quoted_path(rest, rest_len, &spec) != 0)
            return fail(p, err_line, "INCLUDE requires a quoted path");
        const char *path = resolve_include(p, frame, spec);
        free(spec);
        if (!path) return fail(p, err_line, "out of memory");
        if (frame->depth >= 32u) return fail(p, err_line, "include nesting too deep");
        if (path_in_stack(p, path))
            return fail(p, err_line, "include cycle for '%s'", path);
        size_t file_len = 0u;
        char *buf = read_file_text(p, path, &file_len, err_line);
        if (!buf) return -1;
        char *text = own_text(p, buf, file_len);
        free(buf);
        if (!text) return fail(p, err_line, "out of memory");
        if (push_frame(p, text, path, err_line, 0u, frame->depth + 1u) != 0)
            return fail(p, err_line, "out of memory");
        return 0;
    }

    if (token_eq_ci(s, mn_len, "EQU")) {
        char *name = rest;
        size_t name_len = token_len(name);
        if (!valid_label(name, name_len))
            return fail(p, err_line, "EQU requires a name and expression");
        char *expr = skip_ws(name + name_len);
        int64_t value;
        if (eval_expr(p, expr, strlen(expr), &value) != 0)
            return fail(p, err_line, "invalid constant expression");
        int rc = add_const(p, name, name_len, value);
        if (rc == -2)
            return fail(p, err_line, "duplicate constant '%.*s'", (int)name_len, name);
        if (rc != 0) return fail(p, err_line, "out of memory");
        return 0;
    }

    if (token_eq_ci(s, mn_len, "DB") || token_eq_ci(s, mn_len, "DW") ||
        token_eq_ci(s, mn_len, "DQ") || token_eq_ci(s, mn_len, "STR") ||
        token_eq_ci(s, mn_len, "ALIGN")) {
        DataKind kind = token_eq_ci(s, mn_len, "DB")  ? D_BYTE :
                        token_eq_ci(s, mn_len, "DW")  ? D_WORD :
                        token_eq_ci(s, mn_len, "DQ")  ? D_QWORD :
                        token_eq_ci(s, mn_len, "STR") ? D_STR : D_ALIGN;
        if (rest_len == 0u && kind != D_STR)
            return fail(p, err_line, "directive requires a value");
        Item *item = add_item(p, item_line);
        if (!item) return fail(p, err_line, "out of memory");
        item->kind = ITEM_DATA;
        item->data_kind = kind;
        item->text = rest;
        item->text_len = rest_len;
        return 0;
    }

    const Macro *macro = find_macro(p, s, mn_len);
    if (macro) {
        if (frame->depth >= 32u) return fail(p, err_line, "macro nesting too deep");
        char **args = NULL;
        size_t argc = 0u;
        if (split_args(rest, &args, &argc) != 0) {
            free_strings(args);
            return fail(p, err_line, "out of memory");
        }
        if (argc != macro->param_count) {
            fail(p, err_line, "macro '%.*s' expects %zu argument(s), got %zu",
                 (int)mn_len, s, macro->param_count, argc);
            free_strings(args);
            return -1;
        }
        int rc = expand_macro(p, macro, args, argc, item_line, frame->depth);
        free_strings(args);
        if (rc != 0 && p->out->error[0] == '\0')
            return fail(p, err_line, "cannot expand macro '%.*s'", (int)mn_len, s);
        return rc;
    }

    const OpDef *def = find_op(s, mn_len);
    if (!def)
        return fail(p, err_line, "unknown instruction '%.*s'", (int)mn_len, s);

    if (def->operand == OPR_NONE) {
        if (rest_len != 0u)
            return fail(p, err_line, "%s takes no operand", def->name);
        Item *item = add_item(p, item_line);
        if (!item) return fail(p, err_line, "out of memory");
        item->op = def->op;
        return 0;
    }
    if (rest_len == 0u) {
        if (def->operand == OPR_TARGET)
            return fail(p, err_line, "%s requires a target", def->name);
        if (def->operand == OPR_FLOAT)
            return fail(p, err_line, "%s requires a float literal", def->name);
        return fail(p, err_line, "%s requires an integer operand", def->name);
    }

    int64_t value = 0;
    RefKind ref = REF_NONE;
    if (def->operand == OPR_FLOAT) {
        char buf[128];
        if (rest_len >= sizeof buf)
            return fail(p, err_line, "invalid float '%.*s'", (int)rest_len, rest);
        memcpy(buf, rest, rest_len);
        buf[rest_len] = '\0';
        char *endp = NULL;
        errno = 0;
        double d = strtod(buf, &endp);
        if (endp == buf || *endp != '\0' || errno == ERANGE)
            return fail(p, err_line, "invalid float '%.*s'", (int)rest_len, rest);
        value = vm_f64_to_bits(d);
    } else if (eval_expr(p, rest, rest_len, &value) != 0) {
        if (def->operand == OPR_TARGET && valid_label(rest, rest_len)) {
            ref = REF_TARGET;
        } else if (def->operand == OPR_INT && valid_label(rest, rest_len)) {
            ref = REF_VALUE;
        } else {
            return fail(p, err_line, "invalid integer '%.*s'", (int)rest_len, rest);
        }
    }

    if ((def->operand == OPR_LOCAL || def->op == OP_ENTER) &&
        (value < 0 || value > 255))
        return fail(p, err_line, "invalid local index");
    if (def->operand == OPR_PORT && (value < 0 || value > 255))
        return fail(p, err_line, "invalid port index");

    Item *item = add_item(p, item_line);
    if (!item) return fail(p, err_line, "out of memory");
    item->op = def->op;
    item->arg = value;
    item->ref = ref;
    item->label = ref == REF_NONE ? NULL : rest;
    item->label_len = rest_len;
    return 0;
}

static void free_parser(Parser *p)
{
    for (size_t i = 0u; i < p->macro_count; i++) {
        free_strings(p->macros[i].params);
        free(p->macros[i].lines);
    }
    free(p->macros);
    free(p->items);
    free(p->labels);
    free(p->consts);
    free(p->frames);
    free(p->conds);
    for (size_t i = 0u; i < p->text_count; i++) free(p->owned_texts[i]);
    free(p->owned_texts);
    for (size_t i = 0u; i < p->path_count; i++) free(p->owned_paths[i]);
    free(p->owned_paths);
}

static int parse_text(AsmProgram *prog, const char *source, const char *path,
                      const char *const *include_dirs, size_t include_dir_count)
{
    if (!prog || !source) return -1;
    asm_program_init(prog);

    Parser p;
    memset(&p, 0, sizeof p);
    p.out = prog;
    p.include_dirs = include_dirs;
    p.include_dir_count = include_dir_count;
    p.emit = true;

    p.src = own_text(&p, source, strlen(source));
    const char *src_path = path ? own_path(&p, path) : NULL;
    if (!p.src || (path && !src_path) ||
        push_frame(&p, p.src, src_path, 0u, 0u, 0u) != 0) {
        snprintf(prog->error, sizeof prog->error, "out of memory");
        free_parser(&p);
        return -1;
    }

    char *line;
    SourceFrame *frame;
    while (next_line(&p, &line, &frame)) {
        if (parse_line(&p, line, frame) != 0) goto error;
        advance_line(frame);
    }

    if (p.defining) {
        fail(&p, p.defining->def_line, "unterminated MACRO");
        goto error;
    }
    if (p.cond_count != 0u) {
        fail(&p, p.conds[p.cond_count - 1u].line, "unterminated IF");
        goto error;
    }
    if (p.item_count == 0u) {
        fail(&p, p.err_line, "no instructions");
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
    for (size_t i = 0u; i < p.item_count; i++) {
        if (p.items[i].kind != ITEM_INST) continue;
        item_addr[i] = VM_IMAGE_START + total;
        total += vm_inst_size((uint8_t)p.items[i].op);
    }
    item_addr[p.item_count] = VM_IMAGE_START + total;

    if (total > VM_IMAGE_MAX) {
        fail(&p, p.err_line, "image too large (%zu bytes)", total);
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
    for (size_t i = 0u; i < 3u; i++) prog->lines[i] = first_line;

    if (data.len > 0u) {
        memcpy(prog->bytes + 3u, data.bytes, data.len);
        memcpy(prog->lines + 3u, data.lines, data.len * sizeof *prog->lines);
    }

    size_t pos = code_off;
    for (size_t i = 0u; i < p.item_count; i++) {
        Item *item = &p.items[i];
        if (item->kind != ITEM_INST) continue;

        uint8_t op = (uint8_t)item->op;
        int64_t arg = item->arg;
        p.err_path = item->path;
        p.err_line = item->line;
        if (item->ref != REF_NONE) {
            const Label *label = find_label(&p, item->label, item->label_len);
            if (!label) {
                fail(&p, item->line, "undefined label or invalid integer '%.*s'",
                     (int)item->label_len, item->label);
                goto emit_error;
            }
            if (label->item_index > p.item_count) {
                fail(&p, item->line, "invalid label target");
                goto emit_error;
            }
            arg = (int64_t)item_addr[label->item_index];
        }
        if (op == OP_JMP || op == OP_JMP_Z || op == OP_CALL) {
            if (arg < 0 || arg >= (int64_t)VM_RAM_SIZE) {
                fail(&p, item->line, "jump target %" PRId64 " out of range", arg);
                goto emit_error;
            }
        }

        unsigned width = (unsigned)vm_inst_size(op) - 1u;
        prog->bytes[pos] = op;
        for (size_t k = 0u; k <= width; k++) prog->lines[pos + k] = item->line;
        pos++;
        if (width == 1u) prog->bytes[pos] = (uint8_t)arg;
        else if (width == 2u || width == 8u) put_be(prog->bytes + pos, (uint64_t)arg, width);
        pos += width;
    }

    free(item_addr);
    free(data.bytes);
    free(data.lines);
    free_parser(&p);
    return 0;

emit_error:
    free(item_addr);
    free(data.bytes);
    free(data.lines);
    goto error;
oom:
    fail(&p, p.err_line, "out of memory");
error:
    free_parser(&p);
    clear_payload(prog);
    return -1;
}

int asm_parse_string(AsmProgram *prog, const char *source)
{
    return parse_text(prog, source, NULL, NULL, 0u);
}

int asm_parse_file_ex(AsmProgram *prog, const char *path,
                      const char *const *include_dirs, size_t include_dir_count)
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

    int rc = parse_text(prog, buf, path, include_dirs, include_dir_count);
    free(buf);
    return rc;
}

int asm_parse_file(AsmProgram *prog, const char *path)
{
    return asm_parse_file_ex(prog, path, NULL, 0u);
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
