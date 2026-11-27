/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_beam_internal.h"

#if XXBYTE_USE_BEAM

#include <xxfclib/memory/xx_memory.h>

/*
 * Erlang term text: integers, floats, atoms, strings and external-term-format
 * terms printed the way io_lib:print/4 (the ~p of beam_disasm's users) prints
 * them with an unlimited line length and depth, Latin-1 printable range.
 */

#define BEAM_ETF_MAX_DEPTH 200u
#define BEAM_BIGINT_DECIMAL_LIMIT 4096u
#define BEAM_ETF_INFLATE_LIMIT (64u * 1024u * 1024u)

/* ---------------------------------------------------------------------- */
/* Integers. */

/* Decimal text of a magnitude given as little-endian bytes. */
static int beam_fmt_magnitude(xxbyte_fmt *fmt, const uint8_t *le, size_t size, int negative)
{
    uint32_t *limbs;
    size_t count, i;
    char *digits;
    size_t ndigits = 0, cap;
    int ok;

    while (size > 0 && le[size - 1] == 0) {
        --size;
    }
    if (size == 0) {
        return xxbyte_fmt_char(fmt, '0');
    }
    if (size <= 8) {
        uint64_t v = 0;
        for (i = size; i-- > 0;) {
            v = (v << 8) | le[i];
        }
        return xxbyte_fmt_printf(fmt, "%s%llu", negative ? "-" : "", (unsigned long long)v);
    }
    if (size > BEAM_BIGINT_DECIMAL_LIMIT) {
        /* too long for a quadratic conversion: Erlang base-16 syntax */
        if (!xxbyte_fmt_str(fmt, negative ? "-16#" : "16#")) {
            return 0;
        }
        for (i = size; i-- > 0;) {
            if (!xxbyte_fmt_printf(fmt, i + 1 == size ? "%X" : "%02X", le[i])) {
                return 0;
            }
        }
        return 1;
    }
    count = (size + 3) / 4;
    limbs = (uint32_t *)xx_mem_alloc(count * sizeof(uint32_t));
    cap = size * 3 + 2;
    digits = (char *)xx_mem_alloc(cap);
    if (limbs == NULL || digits == NULL) {
        xx_mem_free(limbs);
        xx_mem_free(digits);
        return 0;
    }
    for (i = 0; i < count; ++i) {
        uint32_t v = 0;
        unsigned k;
        for (k = 0; k < 4; ++k) {
            size_t at = i * 4 + k;
            if (at < size) {
                v |= (uint32_t)le[at] << (8 * k);
            }
        }
        limbs[i] = v;
    }
    while (count > 0) {
        uint64_t rem = 0;
        unsigned k;
        for (i = count; i-- > 0;) {
            uint64_t cur = (rem << 32) | limbs[i];
            limbs[i] = (uint32_t)(cur / 1000000000u);
            rem = cur % 1000000000u;
        }
        while (count > 0 && limbs[count - 1] == 0) {
            --count;
        }
        for (k = 0; k < 9 && (count > 0 || rem != 0); ++k) {
            digits[ndigits++] = (char)('0' + (int)(rem % 10u));
            rem /= 10u;
        }
    }
    ok = !negative || xxbyte_fmt_char(fmt, '-');
    while (ok && ndigits-- > 0) {
        ok = xxbyte_fmt_char(fmt, digits[ndigits]);
    }
    xx_mem_free(limbs);
    xx_mem_free(digits);
    return ok;
}

int xxbyte_beam_fmt_bigint(xxbyte_fmt *fmt, const uint8_t *bytes, size_t size, int is_signed)
{
    uint8_t stack[64] = {0};
    uint8_t *le = stack;
    size_t i;
    int negative = is_signed && size > 0 && (bytes[0] & 0x80) != 0;
    int ok;

    if (size > sizeof(stack)) {
        le = (uint8_t *)xx_mem_alloc(size);
        if (le == NULL) {
            return 0;
        }
    }
    for (i = 0; i < size; ++i) {
        le[i] = bytes[size - 1 - i];
    }
    if (negative) {
        unsigned carry = 1;
        for (i = 0; i < size; ++i) {
            unsigned v = (unsigned)(uint8_t)~le[i] + carry;
            le[i] = (uint8_t)v;
            carry = v >> 8;
        }
    }
    ok = beam_fmt_magnitude(fmt, le, size, negative);
    if (le != stack) {
        xx_mem_free(le);
    }
    return ok;
}

/* ---------------------------------------------------------------------- */
/* Floats: float_to_list(F, [short]) (erts ryu to_chars with the Erlang
 * changes). The shortest digits come from xxbyte_fmt_double. */

int xxbyte_beam_fmt_erlang_float(xxbyte_fmt *fmt, double value)
{
    xx_buf_t tmp;
    xxbyte_fmt t;
    char digits[40];
    size_t n = 0, i;
    int negative = 0, seen_point = 0, int_digits = 0, exp10 = 0, ok = 1;
    int32_t e;
    const char *s;
    uint64_t mantissa = 0;

    if (value != value || value > 1.7976931348623157e308 || value < -1.7976931348623157e308) {
        return xxbyte_fmt_double(fmt, value);
    }
    if (value == 0.0) {
        uint64_t bits;
        memcpy(&bits, &value, sizeof(bits));
        return xxbyte_fmt_str(fmt, (bits >> 63) != 0 ? "-0.0" : "0.0");
    }
    xx_buf_init(&tmp);
    t.out = &tmp;
    t.flags = 0;
    t.resolver = NULL;
    t.context = NULL;
    if (!xxbyte_fmt_double(&t, value) || tmp.data == NULL) {
        xx_buf_free(&tmp);
        return 0;
    }
    s = tmp.data;
    if (*s == '-') {
        negative = 1;
        ++s;
    }
    for (; *s != '\0' && *s != 'e' && *s != 'E'; ++s) {
        if (*s == '.') {
            seen_point = 1;
            continue;
        }
        if (*s < '0' || *s > '9') {
            ok = 0;
            break;
        }
        if (!seen_point) {
            ++int_digits;
        }
        if (n == 0 && *s == '0') {
            /* a leading zero: not a significant digit */
            --int_digits;
            continue;
        }
        if (n < sizeof(digits)) {
            digits[n++] = *s;
        } else {
            ok = 0;
        }
    }
    if (ok && (*s == 'e' || *s == 'E')) {
        int sign = 1;
        ++s;
        if (*s == '+') {
            ++s;
        } else if (*s == '-') {
            sign = -1;
            ++s;
        }
        for (; *s >= '0' && *s <= '9'; ++s) {
            exp10 = exp10 * 10 + (*s - '0');
            if (exp10 > 10000) {
                ok = 0;
                break;
            }
        }
        exp10 *= sign;
    }
    xx_buf_free(&tmp);
    if (!ok || n == 0 || n > 17) {
        return xxbyte_fmt_double(fmt, value);
    }
    /* value = 0.d1d2..dn * 10^(int_digits + exp10) = D * 10^e */
    e = (int32_t)(int_digits + exp10 - (int)n);
    while (n > 1 && digits[n - 1] == '0') {
        --n;
        ++e;
    }
    for (i = 0; i < n; ++i) {
        mantissa = mantissa * 10u + (uint64_t)(digits[i] - '0');
    }
    {
        int32_t olength = (int32_t)n;
        int32_t sci = e + olength - 1;
        int32_t lower, upper;
        int fixed;

        if (olength == 1) {
            lower = -4;
            upper = 2;
        } else if (sci >= 10) {
            lower = -(olength + 2);
            upper = 2;
        } else {
            lower = -(olength + 2);
            upper = 1;
        }
        fixed = lower <= e && e <= upper;
        if (fixed && ((mantissa >= (UINT64_C(1) << 53) && e == 0) || (mantissa > (UINT64_C(1) << 52) / 5 && e == 1) || (mantissa > (UINT64_C(1) << 51) / 25 && e == 2))) {
            fixed = 0;
        }
        if (negative && !xxbyte_fmt_char(fmt, '-')) {
            return 0;
        }
        if (fixed) {
            int32_t whole = olength + e;
            if (e >= 0) {
                if (!xxbyte_fmt_printf(fmt, "%.*s", (int)n, digits)) {
                    return 0;
                }
                for (i = 0; i < (size_t)e; ++i) {
                    if (!xxbyte_fmt_char(fmt, '0')) {
                        return 0;
                    }
                }
                return xxbyte_fmt_str(fmt, ".0");
            }
            if (whole > 0) {
                return xxbyte_fmt_printf(fmt, "%.*s.%.*s", (int)whole, digits, (int)(olength - whole), digits + whole);
            }
            if (!xxbyte_fmt_str(fmt, "0.")) {
                return 0;
            }
            for (i = 0; i < (size_t)(-whole); ++i) {
                if (!xxbyte_fmt_char(fmt, '0')) {
                    return 0;
                }
            }
            return xxbyte_fmt_printf(fmt, "%.*s", (int)n, digits);
        }
        if (n == 1) {
            return xxbyte_fmt_printf(fmt, "%c.0e%d", digits[0], (int)sci);
        }
        return xxbyte_fmt_printf(fmt, "%c.%.*se%d", digits[0], (int)(n - 1), digits + 1, (int)sci);
    }
}

/* ---------------------------------------------------------------------- */
/* Characters, strings and atoms. */

static int beam_fmt_utf8(xxbyte_fmt *fmt, uint32_t c)
{
    char b[4];
    size_t n;

    if (c < 0x80) {
        return xxbyte_fmt_char(fmt, (char)c);
    }
    if (c < 0x800) {
        b[0] = (char)(0xC0 | (c >> 6));
        b[1] = (char)(0x80 | (c & 0x3F));
        n = 2;
    } else if (c < 0x10000) {
        b[0] = (char)(0xE0 | (c >> 12));
        b[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        b[2] = (char)(0x80 | (c & 0x3F));
        n = 3;
    } else {
        b[0] = (char)(0xF0 | ((c >> 18) & 0x07));
        b[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        b[3] = (char)(0x80 | (c & 0x3F));
        n = 4;
    }
    return xx_buf_append(fmt->out, b, n) ? 1 : 0;
}

/* io_lib string_char with unicode_as_latin1 (characters above 255 escaped). */
static int beam_fmt_string_char(xxbyte_fmt *fmt, uint32_t c, char quote)
{
    if (c == (uint32_t)(unsigned char)quote) {
        return xxbyte_fmt_char(fmt, '\\') && xxbyte_fmt_char(fmt, quote);
    }
    if (c == '\\') {
        return xxbyte_fmt_str(fmt, "\\\\");
    }
    if (c >= ' ' && c <= '~') {
        return xxbyte_fmt_char(fmt, (char)c);
    }
    if (c >= 0xA0 && c <= 0xFF) {
        return beam_fmt_utf8(fmt, c);
    }
    if (c > 0xFF) {
        return xxbyte_fmt_printf(fmt, "\\x{%X}", (unsigned)c);
    }
    switch (c) {
        case '\n': return xxbyte_fmt_str(fmt, "\\n");
        case '\r': return xxbyte_fmt_str(fmt, "\\r");
        case '\t': return xxbyte_fmt_str(fmt, "\\t");
        case '\v': return xxbyte_fmt_str(fmt, "\\v");
        case '\b': return xxbyte_fmt_str(fmt, "\\b");
        case '\f': return xxbyte_fmt_str(fmt, "\\f");
        case 27: return xxbyte_fmt_str(fmt, "\\e");
        case 127: return xxbyte_fmt_str(fmt, "\\d");
        default: return xxbyte_fmt_printf(fmt, "\\%c%c%c", '0' + (int)(c >> 6), '0' + (int)((c >> 3) & 7u), '0' + (int)(c & 7u));
    }
}

/* Next code point of an atom name: UTF-8, or Latin-1; a malformed UTF-8
 * byte stands for itself. */
static uint32_t beam_next_char(const uint8_t *text, size_t length, size_t *i, int utf8)
{
    uint8_t b = text[*i];
    uint32_t c;
    size_t need, k;

    ++*i;
    if (!utf8 || b < 0x80) {
        return b;
    }
    if (b >= 0xC2 && b <= 0xDF) {
        need = 1;
        c = b & 0x1Fu;
    } else if (b >= 0xE0 && b <= 0xEF) {
        need = 2;
        c = b & 0x0Fu;
    } else if (b >= 0xF0 && b <= 0xF4) {
        need = 3;
        c = b & 0x07u;
    } else {
        return b;
    }
    if (length - *i < need) {
        return b;
    }
    for (k = 0; k < need; ++k) {
        uint8_t cb = text[*i + k];
        if ((cb & 0xC0) != 0x80) {
            return b;
        }
        c = (c << 6) | (cb & 0x3Fu);
    }
    if ((need == 2 && (c < 0x800 || (c >= 0xD800 && c <= 0xDFFF))) || (need == 3 && (c < 0x10000 || c > 0x10FFFF))) {
        return b;
    }
    *i += need;
    return c;
}

static int beam_name_char(uint32_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7) || (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) || (c >= '0' && c <= '9') ||
           c == '_' || c == '@';
}

static int beam_reserved_word(const uint8_t *text, size_t length)
{
    static const char *const words[] = {
        "after", "begin", "case", "try", "cond", "catch", "andalso", "orelse", "end", "fun", "if", "let", "of",    "receive", "when",
        "bnot",  "not",   "div",  "rem", "band", "and",   "bor",     "bxor",   "bsl", "bsr", "or", "xor", "maybe", "else",
    };
    size_t i;

    for (i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (strlen(words[i]) == length && memcmp(words[i], text, length) == 0) {
            return 1;
        }
    }
    return 0;
}

int xxbyte_beam_fmt_atom(xxbyte_fmt *fmt, const uint8_t *text, size_t length, int utf8)
{
    int quote = 0;
    size_t i = 0;

    if (length == 0 || beam_reserved_word(text, length)) {
        quote = 1;
    } else {
        uint32_t c = beam_next_char(text, length, &i, utf8);
        if (!((c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7))) {
            quote = 1;
        }
        while (!quote && i < length) {
            if (!beam_name_char(beam_next_char(text, length, &i, utf8))) {
                quote = 1;
            }
        }
    }
    if (quote && !xxbyte_fmt_char(fmt, '\'')) {
        return 0;
    }
    i = 0;
    while (i < length) {
        uint32_t c = beam_next_char(text, length, &i, utf8);
        if (!(quote ? beam_fmt_string_char(fmt, c, '\'') : beam_fmt_utf8(fmt, c))) {
            return 0;
        }
    }
    return quote ? xxbyte_fmt_char(fmt, '\'') : 1;
}

/* ---------------------------------------------------------------------- */
/* External term format. */

enum {
    ETF_NEW_FLOAT = 70,
    ETF_BIT_BINARY = 77,
    ETF_COMPRESSED = 80,
    ETF_SMALL_INTEGER = 97,
    ETF_INTEGER = 98,
    ETF_FLOAT = 99,
    ETF_ATOM = 100,
    ETF_SMALL_TUPLE = 104,
    ETF_LARGE_TUPLE = 105,
    ETF_NIL = 106,
    ETF_STRING = 107,
    ETF_LIST = 108,
    ETF_BINARY = 109,
    ETF_SMALL_BIG = 110,
    ETF_LARGE_BIG = 111,
    ETF_EXPORT = 113,
    ETF_SMALL_ATOM = 115,
    ETF_MAP = 116,
    ETF_ATOM_UTF8 = 118,
    ETF_SMALL_ATOM_UTF8 = 119
};

typedef struct beam_etf {
    const uint8_t *data;
    size_t size;
    xxbyte_fmt *fmt; /* NULL: only skip */
    int failed;      /* allocation failure */
    int weak;        /* etf_cmp: maps of one size compare equal */
} beam_etf;

static int etf_need(const beam_etf *e, size_t pos, size_t n)
{
    return pos <= e->size && e->size - pos >= n;
}

static int etf_out(beam_etf *e, int ok)
{
    if (!ok) {
        e->failed = 1;
    }
    return ok;
}

static int etf_str(beam_etf *e, const char *s)
{
    return e->fmt == NULL || etf_out(e, xxbyte_fmt_str(e->fmt, s));
}

/* Integer value of a small or integer term at pos, for the printable test. */
static int etf_char_at(const beam_etf *e, size_t pos, uint32_t *c, size_t *next)
{
    if (!etf_need(e, pos, 1)) {
        return 0;
    }
    if (e->data[pos] == ETF_SMALL_INTEGER && etf_need(e, pos, 2)) {
        *c = e->data[pos + 1];
        *next = pos + 2;
        return 1;
    }
    if (e->data[pos] == ETF_INTEGER && etf_need(e, pos, 5)) {
        int32_t v = (int32_t)xxbyte_u32be(e->data + pos + 1);
        if (v < 0) {
            return 0;
        }
        *c = (uint32_t)v;
        *next = pos + 5;
        return 1;
    }
    return 0;
}

static int beam_printable_latin1(uint32_t c)
{
    return (c >= 32 && c <= 126) || (c >= 160 && c <= 255) || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\b' || c == '\f' || c == 27;
}

static int etf_term(beam_etf *e, size_t *pos, unsigned depth);

/* Walks a list (LIST or STRING, continuing through list tails) without
 * printing: 1 when it is a printable Latin-1 string, 0 when not, -1 when
 * malformed. */
static int etf_list_printable(beam_etf *e, size_t pos, unsigned depth)
{
    beam_etf skip = *e;
    int printable = 1;
    uint64_t count = 0;

    skip.fmt = NULL;
    for (;;) {
        uint8_t tag;
        if (!etf_need(e, pos, 1)) {
            return -1;
        }
        tag = e->data[pos];
        if (tag == ETF_STRING) {
            size_t len, i;
            if (!etf_need(e, pos, 3)) {
                return -1;
            }
            len = xxbyte_u16be(e->data + pos + 1);
            if (!etf_need(e, pos + 3, len)) {
                return -1;
            }
            for (i = 0; i < len; ++i) {
                if (!beam_printable_latin1(e->data[pos + 3 + i])) {
                    printable = 0;
                }
            }
            return printable && count + len > 0;
        }
        if (tag == ETF_NIL) {
            return printable && count > 0;
        }
        if (tag != ETF_LIST) {
            /* improper tail */
            size_t p = pos;
            return etf_term(&skip, &p, depth + 1) ? 0 : -1;
        }
        {
            uint32_t n, i;
            if (!etf_need(e, pos, 5)) {
                return -1;
            }
            n = xxbyte_u32be(e->data + pos + 1);
            pos += 5;
            count += n;
            for (i = 0; i < n; ++i) {
                uint32_t c;
                size_t next;
                if (printable && etf_char_at(e, pos, &c, &next) && beam_printable_latin1(c)) {
                    pos = next;
                    continue;
                }
                printable = 0;
                if (!etf_term(&skip, &pos, depth + 1)) {
                    return -1;
                }
            }
        }
    }
}

static int etf_string_body(beam_etf *e, size_t pos)
{
    /* prints the characters of a printable list chain */
    for (;;) {
        uint8_t tag = e->data[pos];
        if (tag == ETF_STRING) {
            size_t len = xxbyte_u16be(e->data + pos + 1), i;
            for (i = 0; i < len; ++i) {
                if (!etf_out(e, beam_fmt_string_char(e->fmt, e->data[pos + 3 + i], '"'))) {
                    return 0;
                }
            }
            return 1;
        }
        if (tag != ETF_LIST) {
            return 1;
        }
        {
            uint32_t n = xxbyte_u32be(e->data + pos + 1), i;
            pos += 5;
            for (i = 0; i < n; ++i) {
                uint32_t c = 0;
                size_t next = pos;
                (void)etf_char_at(e, pos, &c, &next);
                if (!etf_out(e, beam_fmt_string_char(e->fmt, c, '"'))) {
                    return 0;
                }
                pos = next;
            }
        }
    }
}

static int etf_list(beam_etf *e, size_t *pos, unsigned depth)
{
    size_t p = *pos;
    int first = 1;

    if (e->fmt != NULL) {
        int printable = etf_list_printable(e, p, depth);
        if (printable < 0) {
            return 0;
        }
        if (printable == 1) {
            if (!etf_str(e, "\"") || !etf_string_body(e, p) || !etf_str(e, "\"")) {
                return 0;
            }
            /* skip it */
            {
                beam_etf skip = *e;
                skip.fmt = NULL;
                return etf_list(&skip, pos, depth);
            }
        }
    }
    if (!etf_str(e, "[")) {
        return 0;
    }
    for (;;) {
        uint8_t tag;
        if (!etf_need(e, p, 1)) {
            return 0;
        }
        tag = e->data[p];
        if (tag == ETF_STRING) {
            size_t len, i;
            if (!etf_need(e, p, 3)) {
                return 0;
            }
            len = xxbyte_u16be(e->data + p + 1);
            if (!etf_need(e, p + 3, len)) {
                return 0;
            }
            for (i = 0; i < len && e->fmt != NULL; ++i) {
                if (!etf_out(e, xxbyte_fmt_printf(e->fmt, first ? "%u" : ",%u", (unsigned)e->data[p + 3 + i]))) {
                    return 0;
                }
                first = 0;
            }
            p += 3 + len;
            break;
        }
        if (tag == ETF_NIL) {
            ++p;
            break;
        }
        if (tag != ETF_LIST) {
            if (!etf_str(e, "|") || !etf_term(e, &p, depth + 1)) {
                return 0;
            }
            break;
        }
        {
            uint32_t n, i;
            if (!etf_need(e, p, 5)) {
                return 0;
            }
            n = xxbyte_u32be(e->data + p + 1);
            p += 5;
            for (i = 0; i < n; ++i) {
                if ((!first && !etf_str(e, ",")) || !etf_term(e, &p, depth + 1)) {
                    return 0;
                }
                first = 0;
            }
        }
    }
    *pos = p;
    return etf_str(e, "]");
}

static int etf_binary(beam_etf *e, const uint8_t *bytes, size_t len, unsigned bits)
{
    size_t i;
    int printable = len > 0 && bits == 8;

    if (e->fmt == NULL) {
        return 1;
    }
    for (i = 0; printable && i < len; ++i) {
        printable = beam_printable_latin1(bytes[i]);
    }
    if (!etf_str(e, "<<")) {
        return 0;
    }
    if (printable) {
        if (!etf_str(e, "\"")) {
            return 0;
        }
        for (i = 0; i < len; ++i) {
            if (!etf_out(e, beam_fmt_string_char(e->fmt, bytes[i], '"'))) {
                return 0;
            }
        }
        return etf_str(e, "\">>");
    }
    for (i = 0; i < len; ++i) {
        int ok;
        if (i + 1 == len && bits != 8) {
            ok = xxbyte_fmt_printf(e->fmt, "%s%u:%u", i == 0 ? "" : ",", (unsigned)(bytes[i] >> (8 - bits)), bits);
        } else {
            ok = xxbyte_fmt_printf(e->fmt, "%s%u", i == 0 ? "" : ",", (unsigned)bytes[i]);
        }
        if (!etf_out(e, ok)) {
            return 0;
        }
    }
    return etf_str(e, ">>");
}

static int etf_atom(beam_etf *e, size_t *pos)
{
    size_t p = *pos, len, hdr;
    uint8_t tag;
    int utf8;

    if (!etf_need(e, p, 1)) {
        return 0;
    }
    tag = e->data[p];
    switch (tag) {
        case ETF_ATOM:
            hdr = 3;
            utf8 = 0;
            break;
        case ETF_ATOM_UTF8:
            hdr = 3;
            utf8 = 1;
            break;
        case ETF_SMALL_ATOM:
            hdr = 2;
            utf8 = 0;
            break;
        case ETF_SMALL_ATOM_UTF8:
            hdr = 2;
            utf8 = 1;
            break;
        default: return 0;
    }
    if (!etf_need(e, p, hdr)) {
        return 0;
    }
    len = hdr == 3 ? xxbyte_u16be(e->data + p + 1) : e->data[p + 1];
    if (!etf_need(e, p + hdr, len)) {
        return 0;
    }
    if (e->fmt != NULL && !etf_out(e, xxbyte_beam_fmt_atom(e->fmt, e->data + p + hdr, len, utf8))) {
        return 0;
    }
    *pos = p + hdr + len;
    return 1;
}

/* ---------------------------------------------------------------------- */
/* Term order (the total order of maps:iterator(M, ordered), which ~kp and
 * deterministic term_to_binary use: integers before floats). Terms are
 * known to be well formed (they were skipped first). */

static int etf_cmp(const beam_etf *e, size_t a, size_t b, unsigned depth);

static int etf_rank(uint8_t tag)
{
    switch (tag) {
        case ETF_SMALL_INTEGER:
        case ETF_INTEGER:
        case ETF_SMALL_BIG:
        case ETF_LARGE_BIG: return 0;
        case ETF_NEW_FLOAT: return 1;
        case ETF_ATOM:
        case ETF_ATOM_UTF8:
        case ETF_SMALL_ATOM:
        case ETF_SMALL_ATOM_UTF8: return 2;
        case ETF_EXPORT: return 4;
        case ETF_SMALL_TUPLE:
        case ETF_LARGE_TUPLE: return 7;
        case ETF_MAP: return 8;
        case ETF_NIL: return 9;
        case ETF_STRING:
        case ETF_LIST: return 10;
        default: return 11; /* binaries */
    }
}

static size_t etf_end(const beam_etf *e, size_t pos)
{
    beam_etf skip = *e;
    skip.fmt = NULL;
    (void)etf_term(&skip, &pos, 0);
    return pos;
}

/* An integer as sign and little-endian magnitude (small ones in buf). */
static void etf_integer(const beam_etf *e, size_t p, int *negative, const uint8_t **mag, size_t *len, uint8_t *buf)
{
    uint8_t tag = e->data[p];
    if (tag == ETF_SMALL_INTEGER || tag == ETF_INTEGER) {
        int64_t v = tag == ETF_SMALL_INTEGER ? e->data[p + 1] : (int32_t)xxbyte_u32be(e->data + p + 1);
        uint64_t m = v < 0 ? (uint64_t)(-(v + 1)) + 1u : (uint64_t)v;
        unsigned i;
        for (i = 0; i < 8; ++i) {
            buf[i] = (uint8_t)(m >> (8 * i));
        }
        *negative = v < 0;
        *mag = buf;
        *len = 8;
    } else {
        size_t hdr = tag == ETF_SMALL_BIG ? 3 : 6;
        *len = tag == ETF_SMALL_BIG ? e->data[p + 1] : xxbyte_u32be(e->data + p + 1);
        *negative = e->data[p + hdr - 1] != 0;
        *mag = e->data + p + hdr;
    }
    while (*len > 0 && (*mag)[*len - 1] == 0) {
        --*len;
    }
    if (*len == 0) {
        *negative = 0;
    }
}

static int etf_cmp_integers(const beam_etf *e, size_t a, size_t b)
{
    uint8_t ba[8], bb[8];
    const uint8_t *ma, *mb;
    size_t la, lb, i;
    int na, nb, r = 0;

    etf_integer(e, a, &na, &ma, &la, ba);
    etf_integer(e, b, &nb, &mb, &lb, bb);
    if (na != nb) {
        return na ? -1 : 1;
    }
    if (la != lb) {
        r = la < lb ? -1 : 1;
    } else {
        for (i = la; i-- > 0;) {
            if (ma[i] != mb[i]) {
                r = ma[i] < mb[i] ? -1 : 1;
                break;
            }
        }
    }
    return na ? -r : r;
}

static void etf_atom_text(const beam_etf *e, size_t p, const uint8_t **text, size_t *len, int *utf8)
{
    uint8_t tag = e->data[p];
    size_t hdr = (tag == ETF_ATOM || tag == ETF_ATOM_UTF8) ? 3 : 2;
    *len = hdr == 3 ? xxbyte_u16be(e->data + p + 1) : e->data[p + 1];
    *text = e->data + p + hdr;
    *utf8 = tag == ETF_ATOM_UTF8 || tag == ETF_SMALL_ATOM_UTF8;
}

static int etf_cmp_atoms(const beam_etf *e, size_t a, size_t b)
{
    const uint8_t *ta, *tb;
    size_t la, lb, ia = 0, ib = 0;
    int ua, ub;

    etf_atom_text(e, a, &ta, &la, &ua);
    etf_atom_text(e, b, &tb, &lb, &ub);
    while (ia < la && ib < lb) {
        uint32_t ca = beam_next_char(ta, la, &ia, ua);
        uint32_t cb = beam_next_char(tb, lb, &ib, ub);
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
    }
    return (ia < la) - (ib < lb);
}

/* List iteration over chained LIST/STRING segments. */
typedef struct etf_iter {
    uint32_t left; /* elements left in the current segment */
    size_t next;   /* the next element (LIST) or byte (STRING) */
    int string;
    size_t tail; /* at the end: an improper tail term, or SIZE_MAX */
} etf_iter;

static void etf_iter_enter(const beam_etf *e, etf_iter *it, size_t pos)
{
    it->tail = SIZE_MAX;
    it->string = e->data[pos] == ETF_STRING;
    if (it->string) {
        it->left = xxbyte_u16be(e->data + pos + 1);
        it->next = pos + 3;
    } else if (e->data[pos] == ETF_LIST) {
        it->left = xxbyte_u32be(e->data + pos + 1);
        it->next = pos + 5;
    } else {
        it->left = 0;
        it->next = SIZE_MAX; /* NIL */
    }
}

/* 1 with an element (a STRING byte gives *byte >= 0), 0 at the end. */
static int etf_iter_next(const beam_etf *e, etf_iter *it, size_t *element, int *byte)
{
    for (;;) {
        uint8_t tag;
        if (it->left > 0) {
            --it->left;
            if (it->string) {
                *byte = e->data[it->next++];
                *element = 0;
            } else {
                *byte = -1;
                *element = it->next;
                it->next = etf_end(e, it->next);
            }
            return 1;
        }
        if (it->string || it->next == SIZE_MAX) {
            return 0;
        }
        tag = e->data[it->next];
        if (tag == ETF_LIST || tag == ETF_STRING || tag == ETF_NIL) {
            etf_iter_enter(e, it, it->next);
            continue;
        }
        it->tail = it->next;
        it->next = SIZE_MAX;
        return 0;
    }
}

/* Compares a small non-negative integer with the term at other. */
static int etf_cmp_small_int(const beam_etf *e, int byte, size_t other)
{
    uint8_t buf[8];
    const uint8_t *mag;
    size_t len;
    int negative;

    if (etf_rank(e->data[other]) != 0) {
        return -1;
    }
    etf_integer(e, other, &negative, &mag, &len, buf);
    if (negative) {
        return 1;
    }
    if (len > 1) {
        return -1;
    }
    if (len == 0) {
        return byte > 0 ? 1 : 0;
    }
    return byte < mag[0] ? -1 : (byte > mag[0] ? 1 : 0);
}

static int etf_cmp_lists(const beam_etf *e, size_t a, size_t b, unsigned depth)
{
    etf_iter ia, ib;

    etf_iter_enter(e, &ia, a);
    etf_iter_enter(e, &ib, b);
    for (;;) {
        size_t ea = 0, eb = 0;
        int ba = -1, bb = -1, r;
        int ha = etf_iter_next(e, &ia, &ea, &ba);
        int hb = etf_iter_next(e, &ib, &eb, &bb);
        if (!ha || !hb) {
            int ta = ha ? 10 : (ia.tail == SIZE_MAX ? 9 : etf_rank(e->data[ia.tail]));
            int tb = hb ? 10 : (ib.tail == SIZE_MAX ? 9 : etf_rank(e->data[ib.tail]));
            if (!ha && !hb && ia.tail != SIZE_MAX && ib.tail != SIZE_MAX) {
                return etf_cmp(e, ia.tail, ib.tail, depth + 1);
            }
            return ta < tb ? -1 : (ta > tb ? 1 : 0);
        }
        if (ba >= 0 && bb >= 0) {
            r = ba < bb ? -1 : (ba > bb ? 1 : 0);
        } else if (ba >= 0) {
            r = etf_cmp_small_int(e, ba, eb);
        } else if (bb >= 0) {
            r = -etf_cmp_small_int(e, bb, ea);
        } else {
            r = etf_cmp(e, ea, eb, depth + 1);
        }
        if (r != 0) {
            return r;
        }
    }
}

static void etf_bits(const beam_etf *e, size_t p, const uint8_t **bytes, uint64_t *bits)
{
    uint32_t n = xxbyte_u32be(e->data + p + 1);
    if (e->data[p] == ETF_BINARY) {
        *bytes = e->data + p + 5;
        *bits = (uint64_t)n * 8;
    } else {
        *bytes = e->data + p + 6;
        *bits = (uint64_t)(n - 1) * 8 + e->data[p + 5];
    }
}

static int etf_cmp_bits(const beam_etf *e, size_t a, size_t b)
{
    const uint8_t *pa, *pb;
    uint64_t la, lb, i, full;

    etf_bits(e, a, &pa, &la);
    etf_bits(e, b, &pb, &lb);
    full = (la < lb ? la : lb) / 8;
    for (i = 0; i < full; ++i) {
        if (pa[i] != pb[i]) {
            return pa[i] < pb[i] ? -1 : 1;
        }
    }
    for (i = full * 8; i < la && i < lb; ++i) {
        unsigned xa = ((unsigned)pa[i / 8] >> (7u - (unsigned)(i % 8))) & 1u, xb = ((unsigned)pb[i / 8] >> (7u - (unsigned)(i % 8))) & 1u;
        if (xa != xb) {
            return xa < xb ? -1 : 1;
        }
    }
    return la < lb ? -1 : (la > lb ? 1 : 0);
}

/*
 * Orders the n pairs of a map. The VM orders maps used as keys by their atom
 * keys' atom-table indexes, which no file determines. A map stored in term
 * order apart from such keys (deterministic term_to_binary, which newer
 * compilers use) keeps its stored order; any other map is sorted, comparing
 * map keys by their sorted keys with atoms by name.
 */
static int etf_sort_pairs(const beam_etf *e, size_t *keys, size_t *values, size_t n, unsigned depth)
{
    beam_etf weak = *e;
    size_t i, j;

    weak.weak = 1;
    for (i = 1; i < n; ++i) {
        if (etf_cmp(&weak, keys[i - 1], keys[i], depth + 1) > 0) {
            break;
        }
    }
    if (i >= n) {
        return 1;
    }
    for (i = 1; i < n; ++i) {
        size_t k = keys[i], v = values[i];
        for (j = i; j > 0 && etf_cmp(e, keys[j - 1], k, depth + 1) > 0; --j) {
            keys[j] = keys[j - 1];
            values[j] = values[j - 1];
        }
        keys[j] = k;
        values[j] = v;
    }
    return 1;
}

static int etf_map_pairs(const beam_etf *e, size_t p, size_t **keys, size_t **values, uint32_t *count, unsigned depth)
{
    uint32_t n = xxbyte_u32be(e->data + p + 1), i;
    size_t q = p + 5;

    *count = n;
    *keys = (size_t *)xx_mem_alloc(((size_t)n + 1) * sizeof(size_t));
    *values = (size_t *)xx_mem_alloc(((size_t)n + 1) * sizeof(size_t));
    if (*keys == NULL || *values == NULL) {
        xx_mem_free(*keys);
        xx_mem_free(*values);
        *keys = *values = NULL;
        return 0;
    }
    for (i = 0; i < n; ++i) {
        (*keys)[i] = q;
        q = etf_end(e, q);
        (*values)[i] = q;
        q = etf_end(e, q);
    }
    return etf_sort_pairs(e, *keys, *values, n, depth);
}

static int etf_cmp(const beam_etf *e, size_t a, size_t b, unsigned depth)
{
    uint8_t ta = e->data[a], tb = e->data[b];
    int ra = etf_rank(ta), rb = etf_rank(tb);

    if (ra != rb) {
        return ra < rb ? -1 : 1;
    }
    if (depth > BEAM_ETF_MAX_DEPTH) {
        return 0;
    }
    switch (ra) {
        case 0: return etf_cmp_integers(e, a, b);
        case 1: {
            uint64_t xa = 0, xb = 0;
            double da, db;
            unsigned i;
            for (i = 0; i < 8; ++i) {
                xa = (xa << 8) | e->data[a + 1 + i];
                xb = (xb << 8) | e->data[b + 1 + i];
            }
            memcpy(&da, &xa, sizeof(da));
            memcpy(&db, &xb, sizeof(db));
            if (da < db) {
                return -1;
            }
            if (da > db) {
                return 1;
            }
            return (xa >> 63) == (xb >> 63) ? 0 : ((xa >> 63) != 0 ? -1 : 1);
        }
        case 2: return etf_cmp_atoms(e, a, b);
        case 4: {
            size_t pa = a + 1, pb = b + 1;
            int r = etf_cmp_atoms(e, pa, pb);
            if (r != 0) {
                return r;
            }
            pa = etf_end(e, pa);
            pb = etf_end(e, pb);
            r = etf_cmp_atoms(e, pa, pb);
            if (r != 0) {
                return r;
            }
            return etf_cmp(e, etf_end(e, pa), etf_end(e, pb), depth + 1);
        }
        case 7: {
            uint32_t na = ta == ETF_SMALL_TUPLE ? e->data[a + 1] : xxbyte_u32be(e->data + a + 1);
            uint32_t nb = tb == ETF_SMALL_TUPLE ? e->data[b + 1] : xxbyte_u32be(e->data + b + 1);
            size_t pa = a + (ta == ETF_SMALL_TUPLE ? 2 : 5), pb = b + (tb == ETF_SMALL_TUPLE ? 2 : 5);
            uint32_t i;
            if (na != nb) {
                return na < nb ? -1 : 1;
            }
            for (i = 0; i < na; ++i) {
                int r = etf_cmp(e, pa, pb, depth + 1);
                if (r != 0) {
                    return r;
                }
                pa = etf_end(e, pa);
                pb = etf_end(e, pb);
            }
            return 0;
        }
        case 8: {
            size_t *ka = NULL, *va = NULL, *kb = NULL, *vb = NULL;
            uint32_t na = 0, nb = 0, i;
            int r = 0;
            if (xxbyte_u32be(e->data + a + 1) != xxbyte_u32be(e->data + b + 1)) {
                return xxbyte_u32be(e->data + a + 1) < xxbyte_u32be(e->data + b + 1) ? -1 : 1;
            }
            if (e->weak) {
                return 0;
            }
            if (etf_map_pairs(e, a, &ka, &va, &na, depth) && etf_map_pairs(e, b, &kb, &vb, &nb, depth)) {
                for (i = 0; r == 0 && i < na; ++i) {
                    r = etf_cmp(e, ka[i], kb[i], depth + 1);
                }
                for (i = 0; r == 0 && i < na; ++i) {
                    r = etf_cmp(e, va[i], vb[i], depth + 1);
                }
            }
            xx_mem_free(ka);
            xx_mem_free(va);
            xx_mem_free(kb);
            xx_mem_free(vb);
            return r;
        }
        case 9: return 0;
        case 10: return etf_cmp_lists(e, a, b, depth);
        default: return etf_cmp_bits(e, a, b);
    }
}

static int etf_term(beam_etf *e, size_t *pos, unsigned depth)
{
    size_t p = *pos;
    uint8_t tag;

    if (depth > BEAM_ETF_MAX_DEPTH || !etf_need(e, p, 1)) {
        return 0;
    }
    tag = e->data[p];
    switch (tag) {
        case ETF_SMALL_INTEGER:
            if (!etf_need(e, p, 2)) {
                return 0;
            }
            if (e->fmt != NULL && !etf_out(e, xxbyte_fmt_printf(e->fmt, "%u", (unsigned)e->data[p + 1]))) {
                return 0;
            }
            *pos = p + 2;
            return 1;
        case ETF_INTEGER:
            if (!etf_need(e, p, 5)) {
                return 0;
            }
            if (e->fmt != NULL && !etf_out(e, xxbyte_fmt_printf(e->fmt, "%d", (int)(int32_t)xxbyte_u32be(e->data + p + 1)))) {
                return 0;
            }
            *pos = p + 5;
            return 1;
        case ETF_SMALL_BIG:
        case ETF_LARGE_BIG: {
            size_t hdr = tag == ETF_SMALL_BIG ? 3 : 6, n;
            if (!etf_need(e, p, hdr)) {
                return 0;
            }
            n = tag == ETF_SMALL_BIG ? e->data[p + 1] : xxbyte_u32be(e->data + p + 1);
            if (!etf_need(e, p + hdr, n) || e->data[p + hdr - 1] > 1) {
                return 0;
            }
            if (e->fmt != NULL && !etf_out(e, beam_fmt_magnitude(e->fmt, e->data + p + hdr, n, e->data[p + hdr - 1] != 0))) {
                return 0;
            }
            *pos = p + hdr + n;
            return 1;
        }
        case ETF_NEW_FLOAT: {
            uint64_t bits = 0;
            double d;
            unsigned i;
            if (!etf_need(e, p, 9)) {
                return 0;
            }
            for (i = 0; i < 8; ++i) {
                bits = (bits << 8) | e->data[p + 1 + i];
            }
            memcpy(&d, &bits, sizeof(d));
            if (e->fmt != NULL && !etf_out(e, xxbyte_beam_fmt_erlang_float(e->fmt, d))) {
                return 0;
            }
            *pos = p + 9;
            return 1;
        }
        case ETF_ATOM:
        case ETF_ATOM_UTF8:
        case ETF_SMALL_ATOM:
        case ETF_SMALL_ATOM_UTF8: return etf_atom(e, pos);
        case ETF_SMALL_TUPLE:
        case ETF_LARGE_TUPLE: {
            size_t hdr = tag == ETF_SMALL_TUPLE ? 2 : 5;
            uint32_t n, i;
            if (!etf_need(e, p, hdr)) {
                return 0;
            }
            n = tag == ETF_SMALL_TUPLE ? e->data[p + 1] : xxbyte_u32be(e->data + p + 1);
            p += hdr;
            if (!etf_str(e, "{")) {
                return 0;
            }
            for (i = 0; i < n; ++i) {
                if ((i != 0 && !etf_str(e, ",")) || !etf_term(e, &p, depth + 1)) {
                    return 0;
                }
            }
            *pos = p;
            return etf_str(e, "}");
        }
        case ETF_MAP: {
            uint32_t n, i;
            size_t q;
            if (!etf_need(e, p, 5)) {
                return 0;
            }
            n = xxbyte_u32be(e->data + p + 1);
            q = p + 5;
            {
                beam_etf skip = *e;
                skip.fmt = NULL;
                for (i = 0; i < 2u * (uint64_t)n; ++i) {
                    if (!etf_term(&skip, &q, depth + 1)) {
                        return 0;
                    }
                }
            }
            if (e->fmt != NULL) {
                size_t *keys = NULL, *values = NULL;
                uint32_t count = 0;
                if (!etf_map_pairs(e, p, &keys, &values, &count, depth)) {
                    e->failed = 1;
                    return 0;
                }
                if (!etf_str(e, "#{")) {
                    xx_mem_free(keys);
                    xx_mem_free(values);
                    return 0;
                }
                for (i = 0; i < count; ++i) {
                    size_t kp = keys[i], vp = values[i];
                    if ((i != 0 && !etf_str(e, ",")) || !etf_term(e, &kp, depth + 1) || !etf_str(e, " => ") || !etf_term(e, &vp, depth + 1)) {
                        xx_mem_free(keys);
                        xx_mem_free(values);
                        return 0;
                    }
                }
                xx_mem_free(keys);
                xx_mem_free(values);
                if (!etf_str(e, "}")) {
                    return 0;
                }
            }
            *pos = q;
            return 1;
        }
        case ETF_NIL: *pos = p + 1; return etf_str(e, "[]");
        case ETF_STRING:
        case ETF_LIST: return etf_list(e, pos, depth);
        case ETF_BINARY: {
            uint32_t n;
            if (!etf_need(e, p, 5)) {
                return 0;
            }
            n = xxbyte_u32be(e->data + p + 1);
            if (!etf_need(e, p + 5, n)) {
                return 0;
            }
            *pos = p + 5 + n;
            return etf_binary(e, e->data + p + 5, n, 8);
        }
        case ETF_BIT_BINARY: {
            uint32_t n;
            unsigned bits;
            if (!etf_need(e, p, 6)) {
                return 0;
            }
            n = xxbyte_u32be(e->data + p + 1);
            bits = e->data[p + 5];
            if (!etf_need(e, p + 6, n) || bits < 1 || bits > 8 || n == 0) {
                return 0;
            }
            *pos = p + 6 + n;
            return etf_binary(e, e->data + p + 6, n, bits);
        }
        case ETF_EXPORT: {
            p += 1;
            if (!etf_str(e, "fun ") || !etf_atom(e, &p) || !etf_str(e, ":") || !etf_atom(e, &p) || !etf_str(e, "/")) {
                return 0;
            }
            if (!etf_need(e, p, 2) || e->data[p] != ETF_SMALL_INTEGER) {
                return 0;
            }
            if (!etf_term(e, &p, depth + 1)) {
                return 0;
            }
            *pos = p;
            return 1;
        }
        default: return 0;
    }
}

static int etf_compressed(xxbyte_fmt *fmt, const uint8_t *data, size_t size, size_t *used)
{
    uint32_t original;
    uint8_t *out;
    size_t out_size = 0, in_used = 0, pos = 0;
    beam_etf e;
    int ok;

    if (size < 5) {
        return -1;
    }
    original = xxbyte_u32be(data + 1);
    if (original == 0 || original > BEAM_ETF_INFLATE_LIMIT) {
        return -1;
    }
    out = (uint8_t *)xx_mem_alloc(original);
    if (out == NULL) {
        return 0;
    }
    if (xxbyte_inflate(data + 5, size - 5, 1, out, original, &out_size, &in_used) != XXBYTE_STATUS_OK || out_size != original) {
        xx_mem_free(out);
        return -1;
    }
    e.data = out;
    e.size = out_size;
    e.fmt = fmt;
    e.failed = 0;
    e.weak = 0;
    ok = etf_term(&e, &pos, 0);
    xx_mem_free(out);
    if (e.failed) {
        return 0;
    }
    if (!ok) {
        return -1;
    }
    *used = 5 + in_used;
    return 1;
}

int xxbyte_beam_etf_skip(const uint8_t *data, size_t size, size_t *pos)
{
    beam_etf e;

    e.data = data;
    e.size = size;
    e.fmt = NULL;
    e.failed = 0;
    e.weak = 0;
    return etf_term(&e, pos, 0);
}

int xxbyte_beam_fmt_etf(xxbyte_fmt *fmt, const uint8_t *etf, size_t size, size_t *used)
{
    beam_etf e;
    size_t pos = 1;
    int ok;

    *used = 0;
    if (size < 2 || etf[0] != 131) {
        return -1;
    }
    if (etf[1] == ETF_COMPRESSED) {
        int r = etf_compressed(fmt, etf + 1, size - 1, used);
        if (r > 0) {
            *used += 1;
        }
        return r;
    }
    e.data = etf;
    e.size = size;
    e.fmt = fmt;
    e.failed = 0;
    e.weak = 0;
    ok = etf_term(&e, &pos, 0);
    if (e.failed) {
        return 0;
    }
    if (!ok) {
        return -1;
    }
    *used = pos;
    return 1;
}

/* ---------------------------------------------------------------------- */
/* Types. */

int xxbyte_beam_fmt_type(xxbyte_fmt *fmt, uint32_t bits, int has_min, int64_t min, int has_max, int64_t max, int has_unit, uint32_t unit)
{
    static const char *const names[] = {"atom", "bitstring", "cons", "float", "fun", "integer", "map", "nil", "pid", "port", "reference", "tuple", "record"};
    unsigned i;
    int first = 1;

    if ((bits & 0x1FFFu) == 0x1FFFu && !has_min && !has_max && !has_unit) {
        return xxbyte_fmt_str(fmt, "any");
    }
    if ((bits & 0x1FFFu) == 0) {
        return xxbyte_fmt_str(fmt, "none");
    }
    for (i = 0; i < 13; ++i) {
        if ((bits & (1u << i)) == 0) {
            continue;
        }
        if ((!first && !xxbyte_fmt_char(fmt, '|')) || !xxbyte_fmt_str(fmt, names[i])) {
            return 0;
        }
        first = 0;
        if (i == 5 && (has_min || has_max)) {
            if (!xxbyte_fmt_char(fmt, '(') || !(has_min ? xxbyte_fmt_printf(fmt, "%lld", (long long)min) : xxbyte_fmt_str(fmt, "-inf")) || !xxbyte_fmt_str(fmt, "..") ||
                !(has_max ? xxbyte_fmt_printf(fmt, "%lld", (long long)max) : xxbyte_fmt_str(fmt, "+inf")) || !xxbyte_fmt_char(fmt, ')')) {
                return 0;
            }
        }
        if (i == 1 && has_unit && !xxbyte_fmt_printf(fmt, "(%u)", (unsigned)unit)) {
            return 0;
        }
    }
    return 1;
}

#else
typedef int xxbyte_beam_term_disabled;
#endif
