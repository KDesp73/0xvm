// SPDX-License-Identifier: MIT
#include "db.h"

/* ------------------------------------------------------------------ output */

void dbg_out(Debugger *db, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(db->out, fmt, ap);
    va_end(ap);
}

void dbg_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("[ERRO] ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

bool parse_i64(const char *s, int64_t *out)
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

uint16_t read_be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
