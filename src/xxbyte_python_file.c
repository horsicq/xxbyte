/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_python_marshal.h"

#if XXBYTE_USE_PYTHON

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#define PY_MODE(major, minor) XXBYTE_PYTHON_MODE(major, minor)

/* Nesting shown by repr before "..." (Python itself would raise
 * RecursionError much deeper). */
#define PYR_MAX_DEPTH 64u
/* Longs larger than this many 15-bit digits (1800 bits, 542 decimal
 * digits, more than a resolver buffer holds) are not converted to decimal,
 * which takes time quadratic in the digits. */
#define PYR_MAX_LONG_DIGITS 120u

struct xxbyte_python_file {
    pym_tree tree;
    size_t selected;
    uint64_t *labels; /* sorted label offsets of the selected code (3.13+) */
    size_t label_count;
    size_t label_capacity;
};

/* ------------------------------------------------------------------ */
/* Bounded text output. */

typedef struct pyr_out {
    char *buf;
    size_t cap; /* > 0 */
    size_t len;
    int full;
} pyr_out;

static void o_mem(pyr_out *o, const char *text, size_t n)
{
    size_t avail;

    if (o->full) {
        return;
    }
    avail = o->cap - 1 - o->len;
    if (n > avail) {
        /* Cut before a UTF-8 sequence that does not fit. */
        n = avail;
        while (n > 0 && ((unsigned char)text[n] & 0xC0u) == 0x80u) {
            --n;
        }
        o->full = 1;
    }
    memcpy(o->buf + o->len, text, n);
    o->len += n;
    o->buf[o->len] = '\0';
}

static void o_str(pyr_out *o, const char *text)
{
    o_mem(o, text, strlen(text));
}

static void o_char(pyr_out *o, char c)
{
    if (o->full) {
        return;
    }
    if (o->len + 1 >= o->cap) {
        o->full = 1;
        return;
    }
    o->buf[o->len++] = c;
    o->buf[o->len] = '\0';
}

static void o_u64(pyr_out *o, uint64_t value)
{
    char tmp[24];
    size_t n = sizeof(tmp);

    do {
        tmp[--n] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    o_mem(o, tmp + n, sizeof(tmp) - n);
}

static void o_i64(pyr_out *o, int64_t value)
{
    if (value < 0) {
        o_char(o, '-');
        o_u64(o, (uint64_t)0 - (uint64_t)value);
    } else {
        o_u64(o, (uint64_t)value);
    }
}

static void o_hex(pyr_out *o, uint32_t value, unsigned digits)
{
    static const char hex[] = "0123456789abcdef";
    char tmp[8];
    unsigned i;

    for (i = 0; i < digits; ++i) {
        tmp[digits - 1 - i] = hex[(value >> (4 * i)) & 15u];
    }
    o_mem(o, tmp, digits);
}

/* Appends a code point as UTF-8 (surrogates too, as surrogatepass). */
static void o_utf8(pyr_out *o, uint32_t cp)
{
    char tmp[4];

    if (cp < 0x80u) {
        tmp[0] = (char)cp;
        o_mem(o, tmp, 1);
    } else if (cp < 0x800u) {
        tmp[0] = (char)(0xC0u | (cp >> 6));
        tmp[1] = (char)(0x80u | (cp & 0x3Fu));
        o_mem(o, tmp, 2);
    } else if (cp < 0x10000u) {
        tmp[0] = (char)(0xE0u | (cp >> 12));
        tmp[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        tmp[2] = (char)(0x80u | (cp & 0x3Fu));
        o_mem(o, tmp, 3);
    } else {
        tmp[0] = (char)(0xF0u | (cp >> 18));
        tmp[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        tmp[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        tmp[3] = (char)(0x80u | (cp & 0x3Fu));
        o_mem(o, tmp, 4);
    }
}

/* Decodes one code point of a UTF-8 string with surrogatepass; an invalid
 * byte b decodes as U+DC00 + b (surrogateescape). */
static uint32_t utf8_next(const uint8_t *p, size_t size, size_t *pos)
{
    size_t i = *pos;
    uint32_t c = p[i];
    uint32_t cp;
    size_t need;
    size_t k;

    if (c < 0x80u) {
        *pos = i + 1;
        return c;
    }
    if (c >= 0xC2u && c <= 0xDFu) {
        need = 1;
        cp = c & 0x1Fu;
    } else if (c >= 0xE0u && c <= 0xEFu) {
        need = 2;
        cp = c & 0x0Fu;
    } else if (c >= 0xF0u && c <= 0xF4u) {
        need = 3;
        cp = c & 0x07u;
    } else {
        *pos = i + 1;
        return 0xDC00u + c;
    }
    if (size - i - 1 < need) {
        *pos = i + 1;
        return 0xDC00u + c;
    }
    for (k = 1; k <= need; ++k) {
        uint32_t b = p[i + k];
        if ((b & 0xC0u) != 0x80u) {
            *pos = i + 1;
            return 0xDC00u + c;
        }
        cp = (cp << 6) | (b & 0x3Fu);
    }
    if ((need == 2 && cp < 0x800u) || (need == 3 && (cp < 0x10000u || cp > 0x10FFFFu))) {
        *pos = i + 1;
        return 0xDC00u + c;
    }
    *pos = i + 1 + need;
    return cp;
}

/* ------------------------------------------------------------------ */
/* repr */

typedef struct pyr {
    const pym_tree *t;
    pyr_out *o;
    uint32_t mode;
    uint32_t path[PYR_MAX_DEPTH];
    unsigned depth;
} pyr;

static int pyr_py2(const pyr *r)
{
    return r->mode < PY_MODE(3, 0);
}

/* The quote Python's repr picks: ' unless the text has ' and no ". */
static char pyr_quote(const pym_node *node)
{
    return (node->flags & PYM_FLAG_DQUOTE) != 0 ? '"' : '\'';
}

/* The escapes every repr shares; returns 1 when c was written. */
static int pyr_common_escape(pyr_out *o, uint32_t c, char quote)
{
    if (c == (uint32_t)(unsigned char)quote || c == '\\') {
        o_char(o, '\\');
        o_char(o, (char)c);
    } else if (c == '\t') {
        o_str(o, "\\t");
    } else if (c == '\n') {
        o_str(o, "\\n");
    } else if (c == '\r') {
        o_str(o, "\\r");
    } else if (c < 0x20u || c == 0x7Fu) {
        o_str(o, "\\x");
        o_hex(o, c, 2);
    } else if (c < 0x7Fu) {
        o_char(o, (char)c);
    } else {
        return 0;
    }
    return 1;
}

/* 2.0: octal escapes and no \t, \n or \r. */
static void pyr_octal(pyr_out *o, uint32_t c)
{
    char tmp[4];

    tmp[0] = '\\';
    tmp[1] = (char)('0' + ((c >> 6) & 7u));
    tmp[2] = (char)('0' + ((c >> 3) & 7u));
    tmp[3] = (char)('0' + (c & 7u));
    o_mem(o, tmp, 4);
}

/* Python 2 str, Python 3 bytes. */
static void pyr_bytes(pyr *r, const pym_node *node, int prefix)
{
    const uint8_t *p = r->t->data + node->data;
    size_t n = node->size;
    char quote = pyr_quote(node);
    size_t i;

    if (r->mode == PY_MODE(2, 0)) {
        o_char(r->o, quote);
        for (i = 0; i < n && !r->o->full; ++i) {
            if (p[i] == (uint8_t)quote || p[i] == '\\') {
                o_char(r->o, '\\');
                o_char(r->o, (char)p[i]);
            } else if (p[i] < 0x20u || p[i] >= 0x7Fu) {
                pyr_octal(r->o, p[i]);
            } else {
                o_char(r->o, (char)p[i]);
            }
        }
        o_char(r->o, quote);
        return;
    }

    if (prefix) {
        o_char(r->o, 'b');
    }
    o_char(r->o, quote);
    for (i = 0; i < n && !r->o->full; ++i) {
        if (!pyr_common_escape(r->o, p[i], quote)) {
            o_str(r->o, "\\x");
            o_hex(r->o, p[i], 2);
        }
    }
    o_char(r->o, quote);
}

static void pyr_str(pyr *r, const pym_node *node)
{
    const uint8_t *p = r->t->data + node->data;
    size_t n = node->size;
    char quote = pyr_quote(node);
    int py2 = (node->flags & PYM_FLAG_PY2_UNICODE) != 0;
    size_t pos = 0;

    if (py2) {
        o_char(r->o, 'u');
    }
    o_char(r->o, quote);
    while (pos < n && !r->o->full) {
        uint32_t c;
        if ((node->flags & PYM_FLAG_LATIN1) != 0) {
            c = p[pos++];
        } else {
            c = utf8_next(p, n, &pos);
        }
        if (r->mode == PY_MODE(2, 0)) {
            /* 2.0: \uXXXX in upper case, octal below 0x20 and from 0x80 */
            static const char upper[] = "0123456789ABCDEF";
            if (c == (uint32_t)(unsigned char)quote || c == '\\') {
                o_char(r->o, '\\');
                o_char(r->o, (char)c);
            } else if (c >= 0x10000u) {
                uint32_t hs = 0xD800u + ((c - 0x10000u) >> 10);
                uint32_t ls = 0xDC00u + ((c - 0x10000u) & 0x3FFu);
                char tmp[12] = {'\\', 'u', upper[(hs >> 12) & 15u], upper[(hs >> 8) & 15u], upper[(hs >> 4) & 15u], upper[hs & 15u],
                                '\\', 'u', upper[(ls >> 12) & 15u], upper[(ls >> 8) & 15u], upper[(ls >> 4) & 15u], upper[ls & 15u]};
                o_mem(r->o, tmp, 12);
            } else if (c >= 0x100u) {
                char tmp[6] = {'\\', 'u', upper[(c >> 12) & 15u], upper[(c >> 8) & 15u], upper[(c >> 4) & 15u], upper[c & 15u]};
                o_mem(r->o, tmp, 6);
            } else if (c < 0x20u || c >= 0x80u) {
                pyr_octal(r->o, c);
            } else {
                o_char(r->o, (char)c);
            }
            continue;
        }
        if (pyr_common_escape(r->o, c, quote)) {
            continue;
        }
        if (r->mode == PY_MODE(2, 1) && c >= 0x10000u) {
            /* 2.1 (narrow) shows the two surrogates */
            o_str(r->o, "\\u");
            o_hex(r->o, 0xD800u + ((c - 0x10000u) >> 10), 4);
            o_str(r->o, "\\u");
            o_hex(r->o, 0xDC00u + ((c - 0x10000u) & 0x3FFu), 4);
        } else if (!py2 && xxbyte_python_printable(c, XXBYTE_PYTHON_MODE_MINOR(r->mode))) {
            o_utf8(r->o, c);
        } else if (c < 0x100u) {
            o_str(r->o, "\\x");
            o_hex(r->o, c, 2);
        } else if (c < 0x10000u) {
            o_str(r->o, "\\u");
            o_hex(r->o, c, 4);
        } else {
            o_str(r->o, "\\U");
            o_hex(r->o, c, 8);
        }
    }
    o_char(r->o, quote);
}

/* The decimal value of a marshal long (15-bit digits, least significant
 * first). */
static void pyr_long(pyr *r, const pym_node *node)
{
    const uint8_t *p = r->t->data + node->data;
    uint32_t n = node->size;
    uint32_t *digits;
    uint16_t *chunks;
    size_t chunk_count = 0;
    uint32_t top;
    uint32_t i;

    while (n > 0 && xxbyte_u16le(p + 2u * (n - 1u)) == 0) {
        --n;
    }
    if (n == 0) {
        o_str(r->o, pyr_py2(r) ? "0L" : "0");
        return;
    }
    if (n > PYR_MAX_LONG_DIGITS) {
        o_str(r->o, "<int of ");
        o_u64(r->o, (uint64_t)n * 15u);
        o_str(r->o, " bits>");
        return;
    }
    digits = (uint32_t *)xx_mem_alloc(n * sizeof(uint32_t));
    /* Each 15-bit digit adds at most 4.52 decimal digits: one 4-digit
     * chunk and a bit per digit, plus one. */
    chunks = (uint16_t *)xx_mem_alloc((2u * n + 2u) * sizeof(uint16_t));
    if (digits == NULL || chunks == NULL) {
        xx_mem_free(digits);
        xx_mem_free(chunks);
        o_str(r->o, "<int>");
        return;
    }
    for (i = 0; i < n; ++i) {
        digits[i] = xxbyte_u16le(p + 2u * i);
    }
    top = n;
    while (top > 0) {
        uint32_t rem = 0;
        uint32_t k = top;
        while (k-- > 0) {
            /* rem < 10000, so cur < 10000 * 32768 + 65536 even for the
             * out-of-range digits of malformed data. */
            uint32_t cur = rem * 32768u + digits[k];
            digits[k] = cur / 10000u;
            rem = cur % 10000u;
        }
        chunks[chunk_count++] = (uint16_t)rem;
        while (top > 0 && digits[top - 1] == 0) {
            --top;
        }
    }
    if ((node->flags & PYM_FLAG_NEG) != 0) {
        o_char(r->o, '-');
    }
    o_u64(r->o, chunks[chunk_count - 1]);
    for (i = (uint32_t)chunk_count - 1; i-- > 0;) {
        char tmp[4];
        uint32_t v = chunks[i];
        tmp[0] = (char)('0' + v / 1000u);
        tmp[1] = (char)('0' + v / 100u % 10u);
        tmp[2] = (char)('0' + v / 10u % 10u);
        tmp[3] = (char)('0' + v % 10u);
        o_mem(r->o, tmp, 4);
    }
    xx_mem_free(digits);
    xx_mem_free(chunks);
    if (pyr_py2(r)) {
        o_char(r->o, 'L');
    }
}

/* The shortest round-trip text of value (xxbyte_fmt_double) into text. */
static size_t pyr_shortest(double value, char *text, size_t capacity)
{
    xx_buf_t buf;
    xxbyte_fmt fmt;
    size_t n = 0;

    xx_buf_init(&buf);
    fmt.out = &buf;
    fmt.flags = 0;
    fmt.resolver = NULL;
    fmt.context = NULL;
    if (xxbyte_fmt_double(&fmt, value) && xx_buf_ok(&buf) && buf.size < capacity) {
        memcpy(text, buf.data, buf.size);
        n = buf.size;
    }
    xx_buf_free(&buf);
    text[n] = '\0';
    return n;
}

/*
 * The exact decimal expansion of m * 2^e (m != 0): digits, most significant
 * first, without leading or trailing zeros, value = 0.digits * 10^exp10.
 * Base 10^9 limbs; m * 5^-e for e < 0.
 */
#define PYR_LIMBS 130u
static size_t pyr_exact(uint64_t m, int e, char *digits, int *exp10)
{
    uint32_t limb[PYR_LIMBS];
    size_t limbs = 0;
    size_t nd = 0;
    size_t i;
    int k;

    limb[limbs++] = (uint32_t)(m % 1000000000u);
    if (m >= 1000000000u) {
        limb[limbs++] = (uint32_t)(m / 1000000000u % 1000000000u);
        if (m >= UINT64_C(1000000000000000000)) {
            limb[limbs++] = (uint32_t)(m / UINT64_C(1000000000000000000));
        }
    }
    for (k = 0; k < (e < 0 ? -e : e); ++k) {
        uint32_t carry = 0;
        uint32_t factor = e < 0 ? 5u : 2u;
        for (i = 0; i < limbs; ++i) {
            uint64_t cur = (uint64_t)limb[i] * factor + carry;
            limb[i] = (uint32_t)(cur % 1000000000u);
            carry = (uint32_t)(cur / 1000000000u);
        }
        if (carry != 0 && limbs < PYR_LIMBS) {
            limb[limbs++] = carry;
        }
    }
    for (i = limbs; i-- > 0;) {
        uint32_t v = limb[i];
        char tmp[9];
        int j;
        for (j = 8; j >= 0; --j) {
            tmp[j] = (char)('0' + v % 10u);
            v /= 10u;
        }
        for (j = 0; j < 9; ++j) {
            if (nd == 0 && tmp[j] == '0') {
                continue;
            }
            digits[nd++] = tmp[j];
        }
    }
    *exp10 = (int)nd + (e < 0 ? e : 0);
    while (nd > 1 && digits[nd - 1] == '0') {
        --nd;
    }
    return nd;
}

/* Compares 0.a * 10^ea with 0.b * 10^eb (normalized digit strings). */
static int pyr_cmp_dec(const char *a, size_t na, int ea, const char *b, size_t nb, int eb)
{
    size_t i;

    if (ea != eb) {
        return ea < eb ? -1 : 1;
    }
    for (i = 0; i < na || i < nb; ++i) {
        char ca = i < na ? a[i] : '0';
        char cb = i < nb ? b[i] : '0';
        if (ca != cb) {
            return ca < cb ? -1 : 1;
        }
    }
    return 0;
}

static double pyr_from_bits(uint64_t bits)
{
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

/* The decimal expansion of the midpoint between finite positive doubles
 * with bits lo and lo + 1. */
static size_t pyr_midpoint(uint64_t lo, char *digits, int *exp10)
{
    uint64_t hi = lo + 1;
    int elo = (int)(lo >> 52);
    int ehi = (int)(hi >> 52);
    uint64_t mlo = lo & ((UINT64_C(1) << 52) - 1u);
    uint64_t mhi = hi & ((UINT64_C(1) << 52) - 1u);
    int e;

    if (elo != 0) {
        mlo |= UINT64_C(1) << 52;
    } else {
        elo = 1;
    }
    if (ehi != 0) {
        mhi |= UINT64_C(1) << 52;
    } else {
        ehi = 1;
    }
    /* align to the smaller exponent; they differ by at most one */
    e = elo < ehi ? elo : ehi;
    mlo <<= elo - e;
    mhi <<= ehi - e;
    return pyr_exact(mlo + mhi, e - 1075 - 1, digits, exp10);
}

/*
 * Correctly rounded decimal-to-double for the text floats of marshal
 * versions 0 and 1 ("%.12g" in 2.0 and 2.1, "%.17g" later). Returns 0 for
 * text that is not a plain finite number.
 */
static int pyr_parse_float(const uint8_t *p, size_t n, double *out)
{
    char want[48];
    size_t nw = 0;
    int dexp = 0;
    int neg = 0;
    int seen = 0;
    int dot = 0;
    long xexp = 0;
    size_t i = 0;
    uint64_t bits;
    double approx;
    int step;

    if (i < n && (p[i] == '-' || p[i] == '+')) {
        neg = p[i] == '-';
        ++i;
    }
    for (; i < n; ++i) {
        if (p[i] == '.' && !dot) {
            dot = 1;
        } else if (p[i] >= '0' && p[i] <= '9') {
            seen = 1;
            if (nw == 0 && p[i] == '0') {
                if (dot) {
                    --dexp;
                }
                continue;
            }
            if (nw < sizeof(want)) {
                want[nw++] = (char)p[i];
            }
            if (!dot) {
                ++dexp;
            }
        } else {
            break;
        }
    }
    if (!seen) {
        return 0;
    }
    if (i < n && (p[i] == 'e' || p[i] == 'E')) {
        int eneg = 0;
        int edigits = 0;
        ++i;
        if (i < n && (p[i] == '-' || p[i] == '+')) {
            eneg = p[i] == '-';
            ++i;
        }
        for (; i < n && p[i] >= '0' && p[i] <= '9'; ++i) {
            if (xexp < 100000) {
                xexp = xexp * 10 + (p[i] - '0');
            }
            edigits = 1;
        }
        if (!edigits) {
            return 0;
        }
        if (eneg) {
            xexp = -xexp;
        }
    }
    if (i != n) {
        return 0;
    }
    while (nw > 1 && want[nw - 1] == '0') {
        --nw;
    }
    if (nw == 0) {
        *out = neg ? -0.0 : 0.0;
        return 1;
    }
    xexp += dexp;
    if (xexp > 310) {
        *out = neg ? -1e308 * 10.0 : 1e308 * 10.0;
        return 1;
    }
    if (xexp < -330) {
        *out = neg ? -0.0 : 0.0;
        return 1;
    }
    /* A first guess within a few units in the last place ... */
    {
        double mant = 0.0;
        long scale;
        size_t k;
        for (k = 0; k < nw && k < 19; ++k) {
            mant = mant * 10.0 + (want[k] - '0');
        }
        scale = xexp - (long)(k);
        approx = mant;
        while (scale > 0) {
            approx *= 10.0;
            --scale;
        }
        while (scale < 0) {
            approx /= 10.0;
            ++scale;
        }
    }
    memcpy(&bits, &approx, sizeof(bits));
    if (bits >= UINT64_C(0x7FF0000000000000)) {
        bits = UINT64_C(0x7FEFFFFFFFFFFFFF);
    }
    /* ... then the exact comparisons with the neighbouring midpoints. */
    for (step = 0; step < 64; ++step) {
        char mid[PYR_LIMBS * 9 + 2];
        int mexp;
        size_t nm;
        int c;
        if (bits < UINT64_C(0x7FEFFFFFFFFFFFFF)) {
            nm = pyr_midpoint(bits, mid, &mexp);
            c = pyr_cmp_dec(want, nw, (int)xexp, mid, nm, mexp);
            if (c > 0 || (c == 0 && (bits & 1u) != 0)) {
                ++bits;
                continue;
            }
        }
        if (bits > 0) {
            nm = pyr_midpoint(bits - 1, mid, &mexp);
            c = pyr_cmp_dec(want, nw, (int)xexp, mid, nm, mexp);
            if (c < 0 || (c == 0 && (bits & 1u) != 0)) {
                --bits;
                continue;
            }
        }
        break;
    }
    *out = pyr_from_bits(bits | (neg ? UINT64_C(1) << 63 : 0));
    return 1;
}

/*
 * C's "%.<prec>g" (prec 1 to 17) of a finite double, correctly rounded (half to even on the
 * exact binary value, as glibc does), from its exact decimal expansion:
 * value = m * 2^e is m * 5^-e / 10^-e for e < 0. Base 10^9 limbs.
 */
static size_t pyr_gfmt(double value, unsigned prec, char *text)
{
    char digits[PYR_LIMBS * 9 + 2];
    size_t nd = 0;
    uint64_t bits;
    uint64_t m;
    int e;
    int exp10;
    int neg;
    size_t len = 0;
    int k;

    digits[0] = '0';
    memcpy(&bits, &value, sizeof(bits));
    neg = (int)(bits >> 63);
    e = (int)((bits >> 52) & 0x7FFu);
    m = bits & ((UINT64_C(1) << 52) - 1u);
    if (neg) {
        text[len++] = '-';
    }
    if (e == 0 && m == 0) {
        text[len++] = '0';
        text[len] = '\0';
        return len;
    }
    if (e == 0) {
        e = 1;
    } else {
        m |= UINT64_C(1) << 52;
    }
    e -= 1075;
    nd = pyr_exact(m, e, digits, &exp10);
    if (nd > prec) {
        int up;
        size_t rest;
        int beyond = 0;
        char next = digits[prec];
        for (rest = prec + 1; rest < nd; ++rest) {
            if (digits[rest] != '0') {
                beyond = 1;
                break;
            }
        }
        up = next > '5' || (next == '5' && (beyond || ((digits[prec - 1] - '0') & 1)));
        nd = prec;
        if (up) {
            size_t j = prec;
            while (j > 0) {
                --j;
                if (digits[j] == '9') {
                    digits[j] = '0';
                    if (j == 0) {
                        memmove(digits + 1, digits, prec - 1);
                        digits[0] = '1';
                        ++exp10;
                        break;
                    }
                } else {
                    ++digits[j];
                    break;
                }
            }
        }
    }
    while (nd > 1 && digits[nd - 1] == '0') {
        --nd;
    }
    /* %g: scientific when the exponent X = exp10 - 1 is < -4 or >= prec. */
    if (exp10 - 1 < -4 || exp10 - 1 >= (int)prec) {
        int x = exp10 - 1;
        text[len++] = digits[0];
        if (nd > 1) {
            text[len++] = '.';
            memcpy(text + len, digits + 1, nd - 1);
            len += nd - 1;
        }
        text[len++] = 'e';
        text[len++] = x < 0 ? '-' : '+';
        if (x < 0) {
            x = -x;
        }
        if (x >= 100) {
            text[len++] = (char)('0' + x / 100);
        }
        text[len++] = (char)('0' + x / 10 % 10);
        text[len++] = (char)('0' + x % 10);
    } else if (exp10 <= 0) {
        text[len++] = '0';
        text[len++] = '.';
        for (k = 0; k < -exp10; ++k) {
            text[len++] = '0';
        }
        memcpy(text + len, digits, nd);
        len += nd;
    } else {
        for (k = 0; k < exp10; ++k) {
            text[len++] = (size_t)k < nd ? digits[k] : '0';
        }
        if ((size_t)exp10 < nd) {
            text[len++] = '.';
            memcpy(text + len, digits + exp10, nd - (size_t)exp10);
            len += nd - (size_t)exp10;
        }
    }
    text[len] = '\0';
    return len;
}

/* Python's float formatting without the ".0" of repr: "%.17g" before 2.7
 * and in 3.0, the shortest repr otherwise. */
static size_t pyr_float_text(const pyr *r, double value, unsigned prec, char *text, size_t capacity)
{
    int old = r->mode < PY_MODE(2, 7) || r->mode == PY_MODE(3, 0);

    if (value != value) {
        memcpy(text, "nan", 4);
        return 3;
    }
    if (value - value != value - value) {
        if (value < 0) {
            memcpy(text, "-inf", 5);
            return 4;
        }
        memcpy(text, "inf", 4);
        return 3;
    }
    if (old) {
        return pyr_gfmt(value, prec, text);
    }
    return pyr_shortest(value, text, capacity);
}

static void pyr_float(pyr *r, double value)
{
    char text[128];
    size_t n = pyr_float_text(r, value, 17, text, sizeof(text));
    size_t i;
    int integral = 1;

    for (i = 0; i < n; ++i) {
        if (text[i] != '-' && (text[i] < '0' || text[i] > '9')) {
            integral = 0;
        }
    }
    o_mem(r->o, text, n);
    if (integral) {
        o_str(r->o, ".0");
    }
}

static void pyr_complex(pyr *r, double real, double imag)
{
    char text[128];
    size_t n;
    uint64_t bits;
    /* complex repr: "%.12g" in 2.0, "%.17g" until 2.6 and in 3.0 */
    unsigned prec = r->mode == PY_MODE(2, 0) ? 12u : 17u;

    memcpy(&bits, &real, sizeof(bits));
    if (real == 0.0 && (bits >> 63) == 0) {
        n = pyr_float_text(r, imag, prec, text, sizeof(text));
        o_mem(r->o, text, n);
        o_char(r->o, 'j');
        return;
    }
    o_char(r->o, '(');
    n = pyr_float_text(r, real, prec, text, sizeof(text));
    o_mem(r->o, text, n);
    n = pyr_float_text(r, imag, prec, text, sizeof(text));
    if (text[0] != '-') {
        o_char(r->o, '+');
    }
    o_mem(r->o, text, n);
    o_str(r->o, "j)");
}

/* Text floats (marshal version 0 and 1) hold the repr of their writer. */
static void pyr_float_bytes(pyr *r, uint32_t at, uint32_t size, int strip)
{
    const char *p = (const char *)r->t->data + at;

    /* complex components are printed without the ".0" of float repr */
    if (strip && size > 2 && p[size - 2] == '.' && p[size - 1] == '0') {
        size -= 2;
    }
    o_mem(r->o, p, size);
}

static int pyr_text_is_zero(const uint8_t *p, uint32_t size)
{
    uint32_t i;

    if (size == 0) {
        return 0;
    }
    for (i = 0; i < size; ++i) {
        if (p[i] == 'e' || p[i] == 'E') {
            break;
        }
        if (p[i] != '0' && p[i] != '.' && p[i] != '+') {
            return 0;
        }
    }
    return 1;
}

static void pyr_complex_text(pyr *r, const pym_node *node)
{
    const uint8_t *real = r->t->data + node->data;
    double re;
    double im;

    if (pyr_parse_float(real, node->size, &re) && pyr_parse_float(r->t->data + node->first, node->extra, &im)) {
        pyr_complex(r, re, im);
        return;
    }

    if (pyr_text_is_zero(real, node->size)) {
        pyr_float_bytes(r, node->first, node->extra, 1);
        o_char(r->o, 'j');
        return;
    }
    o_char(r->o, '(');
    pyr_float_bytes(r, node->data, node->size, 1);
    if (node->extra == 0 || r->t->data[node->first] != '-') {
        o_char(r->o, '+');
    }
    pyr_float_bytes(r, node->first, node->extra, 1);
    o_str(r->o, "j)");
}

/* Raw text of a string node (UTF-8 out), for names. */
static void pyr_raw(pyr_out *o, const pym_tree *t, const pym_node *node)
{
    const uint8_t *p = t->data + node->data;
    uint32_t i;

    if (node->kind == PYM_STR && (node->flags & PYM_FLAG_LATIN1) != 0) {
        for (i = 0; i < node->size && !o->full; ++i) {
            o_utf8(o, p[i]);
        }
        return;
    }
    o_mem(o, (const char *)p, node->size);
}

static int pyr_is_text(const pym_tree *t, uint32_t node)
{
    return node != PYM_NONE_INDEX && (t->nodes[node].kind == PYM_STR || t->nodes[node].kind == PYM_BYTES);
}

/* Appends at most limit bytes of a text node (code repr precision). */
static void pyr_raw_limit(pyr_out *o, const pym_tree *t, const pym_node *node, size_t limit)
{
    pym_node copy = *node;

    if (copy.size > limit) {
        copy.size = (uint32_t)limit;
    }
    pyr_raw(o, t, &copy);
}

static void pyr_code(pyr_out *o, const pym_tree *t, uint32_t mode, const pym_code *c)
{
    int precision = mode < PY_MODE(3, 2);
    int32_t line = c->firstlineno != 0 ? c->firstlineno : -1;
    /* 2.x needs str names; 3.x prints "???" for a non-str file name. */
    uint8_t text_kind = mode < PY_MODE(3, 0) ? PYM_BYTES : PYM_STR;

    o_str(o, "<code object ");
    if (c->name != PYM_NONE_INDEX && t->nodes[c->name].kind == text_kind) {
        pyr_raw_limit(o, t, &t->nodes[c->name], precision ? 100u : (size_t)-1);
    } else {
        o_str(o, "???");
    }
    o_str(o, " at 0x");
    {
        uint32_t at = t->nodes[c->node].offset;
        unsigned digits = 1;
        while (digits < 8 && (at >> (4 * digits)) != 0) {
            ++digits;
        }
        o_hex(o, at, digits);
    }
    if (c->filename != PYM_NONE_INDEX && t->nodes[c->filename].kind == text_kind) {
        o_str(o, ", file \"");
        pyr_raw_limit(o, t, &t->nodes[c->filename], precision ? 300u : (size_t)-1);
        o_str(o, "\", line ");
    } else if (mode < PY_MODE(3, 2)) {
        o_str(o, ", file \"???\", line ");
    } else {
        o_str(o, ", file ???, line ");
    }
    o_i64(o, line);
    o_char(o, '>');
}

static void pyr_object(pyr *r, uint32_t index);

static void pyr_items(pyr *r, const pym_node *node, uint32_t count, int pairs)
{
    uint32_t i;
    uint32_t first = node->first;

    for (i = 0; i < count && !r->o->full; ++i) {
        if (i != 0) {
            o_str(r->o, ", ");
        }
        if (pairs) {
            pyr_object(r, r->t->kids[first + 2u * i]);
            o_str(r->o, ": ");
            pyr_object(r, r->t->kids[first + 2u * i + 1u]);
        } else {
            pyr_object(r, r->t->kids[first + i]);
        }
    }
}

static void pyr_container(pyr *r, uint32_t index)
{
    const pym_node *node = &r->t->nodes[index];
    int py2 = pyr_py2(r);
    unsigned i;

    for (i = 0; i < r->depth; ++i) {
        if (r->path[i] == index) {
            switch (node->kind) {
                case PYM_TUPLE: o_str(r->o, "(...)"); return;
                case PYM_LIST: o_str(r->o, "[...]"); return;
                case PYM_DICT: o_str(r->o, "{...}"); return;
                case PYM_SET: o_str(r->o, py2 ? "set(...)" : "set(...)"); return;
                default: o_str(r->o, "frozenset(...)"); return;
            }
        }
    }
    if (r->depth >= PYR_MAX_DEPTH) {
        o_str(r->o, "...");
        return;
    }
    r->path[r->depth++] = index;
    switch (node->kind) {
        case PYM_TUPLE:
            o_char(r->o, '(');
            pyr_items(r, node, node->size, 0);
            if (node->size == 1) {
                o_char(r->o, ',');
            }
            o_char(r->o, ')');
            break;
        case PYM_LIST:
            o_char(r->o, '[');
            pyr_items(r, node, node->size, 0);
            o_char(r->o, ']');
            break;
        case PYM_DICT:
            o_char(r->o, '{');
            pyr_items(r, node, node->size, 1);
            o_char(r->o, '}');
            break;
        case PYM_SLICE:
            o_str(r->o, "slice(");
            pyr_items(r, node, 3, 0);
            o_char(r->o, ')');
            break;
        default: {
            const char *type = node->kind == PYM_SET ? "set" : "frozenset";
            if (py2) {
                o_str(r->o, type);
                o_str(r->o, "([");
                pyr_items(r, node, node->size, 0);
                o_str(r->o, "])");
            } else if (node->size == 0) {
                o_str(r->o, type);
                o_str(r->o, "()");
            } else if (node->kind == PYM_SET) {
                o_char(r->o, '{');
                pyr_items(r, node, node->size, 0);
                o_char(r->o, '}');
            } else {
                o_str(r->o, "frozenset({");
                pyr_items(r, node, node->size, 0);
                o_str(r->o, "})");
            }
            break;
        }
    }
    --r->depth;
}

static void pyr_object(pyr *r, uint32_t index)
{
    const pym_node *node;

    if (r->o->full) {
        return;
    }
    node = &r->t->nodes[index];
    switch (node->kind) {
        case PYM_NULL: o_str(r->o, "<NULL>"); break;
        case PYM_NONE: o_str(r->o, "None"); break;
        case PYM_TRUE: o_str(r->o, "True"); break;
        case PYM_FALSE: o_str(r->o, "False"); break;
        case PYM_ELLIPSIS: o_str(r->o, "Ellipsis"); break;
        case PYM_STOPITER: o_str(r->o, pyr_py2(r) ? "<type 'exceptions.StopIteration'>" : "<class 'StopIteration'>"); break;
        case PYM_INT: o_i64(r->o, node->v.i); break;
        case PYM_LONG: pyr_long(r, node); break;
        case PYM_FLOAT: pyr_float(r, node->v.d[0]); break;
        case PYM_FLOAT_TEXT: {
            double d;
            if (pyr_parse_float(r->t->data + node->data, node->size, &d)) {
                pyr_float(r, d);
            } else {
                pyr_float_bytes(r, node->data, node->size, 0);
            }
            break;
        }
        case PYM_COMPLEX: pyr_complex(r, node->v.d[0], node->v.d[1]); break;
        case PYM_COMPLEX_TEXT: pyr_complex_text(r, node); break;
        case PYM_BYTES: pyr_bytes(r, node, !pyr_py2(r)); break;
        case PYM_STR: pyr_str(r, node); break;
        case PYM_CODE: pyr_code(r->o, r->t, r->mode, &r->t->codes[node->first]); break;
        default: pyr_container(r, index); break;
    }
}

/* ------------------------------------------------------------------ */
/* Files */

static const pym_code *file_code(const xxbyte_python_file *file, size_t index)
{
    if (file == NULL || index >= file->tree.walk_count) {
        return NULL;
    }
    return &file->tree.codes[file->tree.walk[index].code];
}

static xxbyte_status file_open(uint32_t mode, uint32_t magic, const uint8_t *data, size_t size, size_t offset, int need_code, xxbyte_python_file **file)
{
    xxbyte_python_file *f;
    xxbyte_status status;

    f = (xxbyte_python_file *)xx_mem_calloc(1, sizeof(*f));
    if (f == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_pym_parse(&f->tree, mode, magic, data, size, offset);
    if (status == XXBYTE_STATUS_OK && need_code && f->tree.nodes[f->tree.root].kind != PYM_CODE) {
        status = XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (status != XXBYTE_STATUS_OK) {
        xxbyte_python_close(f);
        return status;
    }
    f->selected = (size_t)-1;
    *file = f;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_python_open_pyc(uint32_t mode, const uint8_t *data, size_t size, xxbyte_python_file **file)
{
    xxbyte_python_pyc_header header;
    xxbyte_status status;
    uint32_t magic;
    size_t header_size;

    if (file == NULL || (data == NULL && size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *file = NULL;
    status = xxbyte_python_parse_pyc_header(data, size, &header);
    if (mode == XXBYTE_MODE_AUTO) {
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        mode = header.mode;
        magic = header.magic;
        header_size = header.header_size;
    } else {
        if (xxbyte_check_mode(XXBYTE_FAMILY_PYTHON, mode) != XXBYTE_STATUS_OK) {
            return XXBYTE_STATUS_UNSUPPORTED_MODE;
        }
        magic = xxbyte_python_release_magic(mode);
        if (status == XXBYTE_STATUS_OK) {
            header_size = header.header_size;
        } else {
            header_size = mode < PY_MODE(3, 3) ? 8u : mode < PY_MODE(3, 7) ? 12u : 16u;
            if (size < header_size) {
                return XXBYTE_STATUS_TRUNCATED;
            }
        }
    }
    return file_open(mode, magic, data, size, header_size, 1, file);
}

xxbyte_status xxbyte_python_open_marshal(uint16_t magic, const uint8_t *data, size_t size, size_t offset, xxbyte_python_file **file)
{
    uint32_t mode = xxbyte_python_mode_from_magic(magic);

    if (file == NULL || (data == NULL && size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *file = NULL;
    if (mode == 0) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if (offset > size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    return file_open(mode, magic, data, size, offset, 0, file);
}

void xxbyte_python_close(xxbyte_python_file *file)
{
    if (file == NULL) {
        return;
    }
    xxbyte_pym_free(&file->tree);
    xx_mem_free(file->labels);
    xx_mem_free(file);
}

uint32_t xxbyte_python_file_mode(const xxbyte_python_file *file)
{
    return file != NULL ? file->tree.mode : 0;
}

size_t xxbyte_python_file_end(const xxbyte_python_file *file)
{
    return file != NULL ? file->tree.end : 0;
}

int xxbyte_python_walk_truncated(const xxbyte_python_file *file)
{
    return file != NULL && file->tree.walk_truncated;
}

size_t xxbyte_python_code_count(const xxbyte_python_file *file)
{
    return file != NULL ? file->tree.walk_count : 0;
}

static void text_span(const pym_tree *t, uint32_t node, uint32_t *offset, uint32_t *size)
{
    if (pyr_is_text(t, node)) {
        *offset = t->nodes[node].data;
        *size = t->nodes[node].size;
    } else {
        *offset = 0;
        *size = 0;
    }
}

/* Counts co_localspluskinds entries with any bit of mask. */
static uint32_t kinds_count(const pym_tree *t, const pym_code *c, uint32_t mask)
{
    uint32_t count = 0;
    uint32_t i;

    if (c->kinds == PYM_NONE_INDEX || t->nodes[c->kinds].kind != PYM_BYTES) {
        return 0;
    }
    for (i = 0; i < t->nodes[c->kinds].size; ++i) {
        if ((t->data[t->nodes[c->kinds].data + i] & mask) != 0) {
            ++count;
        }
    }
    return count;
}

xxbyte_status xxbyte_python_get_code(const xxbyte_python_file *file, size_t index, xxbyte_python_code_object *object)
{
    const pym_code *c = file_code(file, index);
    const pym_tree *t;

    if (object == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(object, 0, sizeof(*object));
    if (c == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    t = &file->tree;
    object->offset = t->nodes[c->node].offset;
    object->parent = t->walk[index].parent;
    object->depth = t->walk[index].depth;
    object->code_offset = t->nodes[c->code].data;
    object->code_size = t->nodes[c->code].size;
    object->argcount = c->argcount;
    object->posonlyargcount = c->posonlyargcount;
    object->kwonlyargcount = c->kwonlyargcount;
    object->nlocals = c->nlocals;
    object->stacksize = c->stacksize;
    object->flags = c->flags;
    object->first_line = c->firstlineno;
    text_span(t, c->name, &object->name_offset, &object->name_size);
    text_span(t, c->qualname != PYM_NONE_INDEX ? c->qualname : c->name, &object->qualname_offset, &object->qualname_size);
    text_span(t, c->filename, &object->filename_offset, &object->filename_size);
    if (c->linetable != PYM_NONE_INDEX && t->nodes[c->linetable].kind == PYM_BYTES) {
        object->line_table_offset = t->nodes[c->linetable].data;
        object->line_table_size = t->nodes[c->linetable].size;
    }
    if (c->exceptiontable != PYM_NONE_INDEX && t->nodes[c->exceptiontable].kind == PYM_BYTES) {
        object->exception_table_offset = t->nodes[c->exceptiontable].data;
        object->exception_table_size = t->nodes[c->exceptiontable].size;
    }
    object->const_count = xxbyte_pym_length(t, c->consts);
    object->name_count = xxbyte_pym_length(t, c->names);
    if (c->kinds != PYM_NONE_INDEX) {
        object->fast_count = xxbyte_pym_length(t, c->varnames);
        /* CO_FAST_LOCAL 0x20, CO_FAST_CELL 0x40, CO_FAST_FREE 0x80 */
        object->local_count = kinds_count(t, c, 0x20u);
        object->cell_count = kinds_count(t, c, 0x40u);
        object->free_count = kinds_count(t, c, 0x80u);
    } else {
        object->local_count = xxbyte_pym_length(t, c->varnames);
        object->cell_count = xxbyte_pym_length(t, c->cellvars);
        object->free_count = xxbyte_pym_length(t, c->freevars);
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_python_find_code_objects(uint32_t mode, const uint8_t *data, size_t size, xxbyte_python_code_object *objects, size_t capacity, size_t *count)
{
    xxbyte_python_file *file = NULL;
    xxbyte_status status;
    size_t i;

    if (count == NULL || (objects == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    status = xxbyte_python_open_pyc(mode, data, size, &file);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    *count = file->tree.walk_count;
    for (i = 0; i < capacity && i < file->tree.walk_count; ++i) {
        xxbyte_python_get_code(file, i, &objects[i]);
    }
    xxbyte_python_close(file);
    return XXBYTE_STATUS_OK;
}

/* dis._parse_varint: 6-bit groups, 0x40 continues (0x80 marks a start). */
static int exc_varint(const uint8_t *p, size_t size, size_t *pos, uint64_t *value)
{
    uint64_t v;
    uint32_t b;

    if (*pos >= size) {
        return 0;
    }
    b = p[(*pos)++];
    v = b & 63u;
    while ((b & 64u) != 0) {
        if (*pos >= size) {
            return 0;
        }
        b = p[(*pos)++];
        v = ((v << 6) | (b & 63u)) & 0xFFFFFFFFFFFFull;
    }
    *value = v;
    return 1;
}

static uint32_t clamp32(uint64_t v)
{
    return v > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)v;
}

xxbyte_status xxbyte_python_exception_entries(const xxbyte_python_file *file, size_t index, xxbyte_python_exception_entry *entries, size_t capacity, size_t *count)
{
    const pym_code *c = file_code(file, index);
    const pym_node *table;
    const uint8_t *p;
    size_t pos = 0;
    size_t n = 0;

    if (count == NULL || (entries == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    if (c == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (c->exceptiontable == PYM_NONE_INDEX || file->tree.nodes[c->exceptiontable].kind != PYM_BYTES) {
        return XXBYTE_STATUS_OK;
    }
    table = &file->tree.nodes[c->exceptiontable];
    p = file->tree.data + table->data;
    for (;;) {
        uint64_t start;
        uint64_t length;
        uint64_t target;
        uint64_t dl;
        if (!exc_varint(p, table->size, &pos, &start) || !exc_varint(p, table->size, &pos, &length) || !exc_varint(p, table->size, &pos, &target) ||
            !exc_varint(p, table->size, &pos, &dl)) {
            break;
        }
        if (n < capacity) {
            entries[n].start = clamp32(start * 2u);
            entries[n].end = clamp32(start * 2u + length * 2u);
            entries[n].target = clamp32(target * 2u);
            entries[n].depth = clamp32(dl >> 1);
            entries[n].lasti = (uint32_t)(dl & 1u);
        }
        ++n;
    }
    *count = n;
    return XXBYTE_STATUS_OK;
}

static size_t out_finish(const pyr_out *o)
{
    return o->len;
}

size_t xxbyte_python_code_repr(const xxbyte_python_file *file, size_t index, char *buffer, size_t buffer_size)
{
    const pym_code *c = file_code(file, index);
    pyr_out o;

    if (c == NULL || buffer == NULL || buffer_size == 0) {
        return 0;
    }
    o.buf = buffer;
    o.cap = buffer_size;
    o.len = 0;
    o.full = 0;
    buffer[0] = '\0';
    pyr_code(&o, &file->tree, file->tree.mode, c);
    return out_finish(&o);
}

static size_t repr_node(const xxbyte_python_file *file, uint32_t node, char *buffer, size_t buffer_size)
{
    pyr_out o;
    pyr r;

    if (node == PYM_NONE_INDEX || buffer == NULL || buffer_size == 0) {
        return 0;
    }
    o.buf = buffer;
    o.cap = buffer_size;
    o.len = 0;
    o.full = 0;
    buffer[0] = '\0';
    r.t = &file->tree;
    r.o = &o;
    r.mode = file->tree.mode;
    r.depth = 0;
    pyr_object(&r, node);
    return out_finish(&o);
}

size_t xxbyte_python_const_repr(const xxbyte_python_file *file, size_t index, int64_t const_index, char *buffer, size_t buffer_size)
{
    const pym_code *c = file_code(file, index);

    if (c == NULL) {
        return 0;
    }
    return repr_node(file, xxbyte_pym_item(&file->tree, c->consts, const_index), buffer, buffer_size);
}

/* A name: its raw text when it is a string, its repr otherwise. */
static size_t name_node(const xxbyte_python_file *file, uint32_t node, char *buffer, size_t buffer_size)
{
    pyr_out o;

    if (node == PYM_NONE_INDEX || buffer == NULL || buffer_size == 0) {
        return 0;
    }
    if (!pyr_is_text(&file->tree, node)) {
        return repr_node(file, node, buffer, buffer_size);
    }
    o.buf = buffer;
    o.cap = buffer_size;
    o.len = 0;
    o.full = 0;
    buffer[0] = '\0';
    pyr_raw(&o, &file->tree, &file->tree.nodes[node]);
    return out_finish(&o);
}

size_t xxbyte_python_name_text(const xxbyte_python_file *file, size_t index, uint32_t index_kind, int64_t item, char *buffer, size_t buffer_size)
{
    const pym_code *c = file_code(file, index);
    const pym_tree *t;
    uint32_t node = PYM_NONE_INDEX;

    if (c == NULL) {
        return 0;
    }
    t = &file->tree;
    switch (index_kind) {
        case XXBYTE_INDEX_NAME: node = xxbyte_pym_item(t, c->names, item); break;
        case XXBYTE_INDEX_LOCAL: node = xxbyte_pym_item(t, c->varnames, item); break;
        case XXBYTE_INDEX_FREE:
            if (c->kinds != PYM_NONE_INDEX || t->mode >= PY_MODE(3, 11)) {
                node = xxbyte_pym_item(t, c->varnames, item);
            } else {
                /* co_cellvars + co_freevars, indexed as one tuple */
                int64_t cells = (int64_t)xxbyte_pym_length(t, c->cellvars);
                int64_t total = cells + (int64_t)xxbyte_pym_length(t, c->freevars);
                if (item < 0) {
                    item += total;
                }
                if (item >= 0 && item < cells) {
                    node = xxbyte_pym_item(t, c->cellvars, item);
                } else if (item >= cells && item < total) {
                    node = xxbyte_pym_item(t, c->freevars, item - cells);
                }
            }
            break;
        default: break;
    }
    return name_node(file, node, buffer, buffer_size);
}

size_t xxbyte_python_code_text(const xxbyte_python_file *file, size_t index, uint32_t what, char *buffer, size_t buffer_size)
{
    const pym_code *c = file_code(file, index);
    uint32_t node;

    if (c == NULL) {
        return 0;
    }
    switch (what) {
        case 0: node = c->name; break;
        case 1: node = c->qualname != PYM_NONE_INDEX ? c->qualname : c->name; break;
        case 2: node = c->filename; break;
        default: return 0;
    }
    return name_node(file, node, buffer, buffer_size);
}

/* ------------------------------------------------------------------ */
/* Labels and the resolver */

static int label_add(xxbyte_python_file *file, uint64_t offset)
{
    if (file->label_count == file->label_capacity) {
        size_t cap = file->label_capacity == 0 ? 64 : file->label_capacity * 2;
        uint64_t *p = (uint64_t *)xx_mem_realloc(file->labels, cap * sizeof(uint64_t));
        if (p == NULL) {
            return 0;
        }
        file->labels = p;
        file->label_capacity = cap;
    }
    file->labels[file->label_count++] = offset;
    return 1;
}

static void label_sort(uint64_t *a, size_t n)
{
    /* Shell sort; label sets are small. */
    size_t gap;
    size_t i;

    for (gap = n / 2; gap > 0; gap /= 2) {
        for (i = gap; i < n; ++i) {
            uint64_t v = a[i];
            size_t j = i;
            while (j >= gap && a[j - gap] > v) {
                a[j] = a[j - gap];
                j -= gap;
            }
            a[j] = v;
        }
    }
}

/* The 1-based label number of offset, 0 when it has none. */
static size_t label_find(const xxbyte_python_file *file, uint64_t offset)
{
    size_t lo = 0;
    size_t hi = file->label_count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (file->labels[mid] < offset) {
            lo = mid + 1;
        } else if (file->labels[mid] > offset) {
            hi = mid;
        } else {
            return mid + 1;
        }
    }
    return 0;
}

xxbyte_status xxbyte_python_select(xxbyte_python_file *file, size_t index)
{
    const pym_code *c = file_code(file, index);
    const pym_node *code;
    const uint8_t *bytes;
    size_t pos = 0;
    size_t n;
    size_t i;
    size_t out;

    if (c == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    file->selected = index;
    file->label_count = 0;
    if (file->tree.mode < PY_MODE(3, 13)) {
        return XXBYTE_STATUS_OK;
    }
    /* dis._make_labels_map: every jump target and the start, end and
     * target of every exception-table entry. */
    code = &file->tree.nodes[c->code];
    bytes = file->tree.data + code->data;
    while (pos < code->size) {
        xxbyte_instruction insn;
        uint32_t k;
        if (xxbyte_decode(XXBYTE_FAMILY_PYTHON, file->tree.mode, bytes + pos, code->size - pos, pos, &insn) != XXBYTE_STATUS_OK) {
            pos += 2;
            continue;
        }
        for (k = 0; k < insn.operand_count; ++k) {
            if (insn.operands[k].type == XXBYTE_OPERAND_BRANCH && !label_add(file, (uint64_t)insn.operands[k].value)) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
        pos += insn.size;
    }
    xxbyte_python_exception_entries(file, index, NULL, 0, &n);
    if (n > 0) {
        xxbyte_python_exception_entry *entries = (xxbyte_python_exception_entry *)xx_mem_alloc(n * sizeof(*entries));
        if (entries == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        xxbyte_python_exception_entries(file, index, entries, n, &n);
        for (i = 0; i < n; ++i) {
            if (!label_add(file, entries[i].start) || !label_add(file, entries[i].end) || !label_add(file, entries[i].target)) {
                xx_mem_free(entries);
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
        xx_mem_free(entries);
    }
    label_sort(file->labels, file->label_count);
    out = 0;
    for (i = 0; i < file->label_count; ++i) {
        if (out == 0 || file->labels[out - 1] != file->labels[i]) {
            file->labels[out++] = file->labels[i];
        }
    }
    file->label_count = out;
    return XXBYTE_STATUS_OK;
}

size_t xxbyte_python_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_python_file *file = (const xxbyte_python_file *)context;
    const xxbyte_operand *operand;

    if (file == NULL || instruction == NULL || buffer == NULL || buffer_size == 0 || operand_index >= instruction->operand_count ||
        instruction->family != XXBYTE_FAMILY_PYTHON || file_code(file, file->selected) == NULL) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (operand->type == XXBYTE_OPERAND_BRANCH) {
        size_t label;
        pyr_out o;
        if (file->tree.mode < PY_MODE(3, 13) || operand->value < 0) {
            return 0;
        }
        label = label_find(file, (uint64_t)operand->value);
        if (label == 0) {
            return 0;
        }
        o.buf = buffer;
        o.cap = buffer_size;
        o.len = 0;
        o.full = 0;
        buffer[0] = '\0';
        o_char(&o, 'L');
        o_u64(&o, label);
        return out_finish(&o);
    }
    if (operand->type != XXBYTE_OPERAND_INDEX) {
        return 0;
    }
    if (operand->kind == XXBYTE_INDEX_CONSTANT) {
        return xxbyte_python_const_repr(file, file->selected, operand->value, buffer, buffer_size);
    }
    return xxbyte_python_name_text(file, file->selected, operand->kind, operand->value, buffer, buffer_size);
}

int xxbyte_python_empty_name(const void *context, const xxbyte_instruction *instruction)
{
    const xxbyte_python_file *file = (const xxbyte_python_file *)context;
    const pym_code *c = file_code(file, file != NULL ? file->selected : 0);
    const xxbyte_operand *operand;
    uint32_t node = PYM_NONE_INDEX;

    if (c == NULL || instruction == NULL || instruction->operand_count == 0 || instruction->operands[0].type != XXBYTE_OPERAND_INDEX) {
        return 0;
    }
    operand = &instruction->operands[0];
    switch (operand->kind) {
        case XXBYTE_INDEX_NAME: node = xxbyte_pym_item(&file->tree, c->names, operand->value); break;
        case XXBYTE_INDEX_LOCAL: node = xxbyte_pym_item(&file->tree, c->varnames, operand->value); break;
        default: return 0;
    }
    return pyr_is_text(&file->tree, node) && file->tree.nodes[node].size == 0;
}

/* ------------------------------------------------------------------ */
/* list_file */

static int list_text(xx_buf_t *out, const char *text)
{
    return xx_buf_append_str(out, text) ? 1 : 0;
}

static xxbyte_status list_exceptions(xxbyte_python_file *file, size_t index, xx_buf_t *out)
{
    xxbyte_python_exception_entry *entries;
    size_t n = 0;
    size_t i;
    int labels = file->tree.mode >= PY_MODE(3, 13);

    if (file->tree.mode < PY_MODE(3, 11)) {
        return XXBYTE_STATUS_OK;
    }
    xxbyte_python_exception_entries(file, index, NULL, 0, &n);
    if (n == 0) {
        return XXBYTE_STATUS_OK;
    }
    entries = (xxbyte_python_exception_entry *)xx_mem_alloc(n * sizeof(*entries));
    if (entries == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    xxbyte_python_exception_entries(file, index, entries, n, &n);
    list_text(out, "ExceptionTable:\n");
    for (i = 0; i < n; ++i) {
        const xxbyte_python_exception_entry *e = &entries[i];
        if (labels) {
            xx_buf_appendf(out, "  L%llu to L%llu -> L%llu [%u]%s\n", (unsigned long long)label_find(file, e->start), (unsigned long long)label_find(file, e->end),
                           (unsigned long long)label_find(file, e->target), (unsigned)e->depth, e->lasti ? " lasti" : "");
        } else {
            xx_buf_appendf(out, "  %lld to %lld -> %u [%u]%s\n", (long long)e->start, (long long)e->end - 2, (unsigned)e->target, (unsigned)e->depth,
                           e->lasti ? " lasti" : "");
        }
    }
    xx_mem_free(entries);
    return xx_buf_ok(out) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

static xxbyte_status list_code(xxbyte_python_file *file, size_t index, uint32_t listing_flags, xx_buf_t *out)
{
    const pym_code *c = file_code(file, index);
    const pym_node *code = &file->tree.nodes[c->code];
    const uint8_t *bytes = file->tree.data + code->data;
    uint32_t mode = file->tree.mode;
    char header[1024];
    size_t pos = 0;
    size_t segment = 0;
    xxbyte_status status;

    xxbyte_python_code_repr(file, index, header, sizeof(header));
    if (!xx_buf_appendf(out, "Disassembly of %s:\n", header)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_python_select(file, index);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    /* Split the listing before every labelled instruction start, found by
     * the same sweep xxbyte_disasm_buf makes (one byte on failure). */
    while (pos < code->size) {
        xxbyte_instruction insn;
        size_t label = file->label_count != 0 ? label_find(file, pos) : 0;
        if (label != 0) {
            if (pos > segment) {
                status =
                    xxbyte_disasm_buf_ex(XXBYTE_FAMILY_PYTHON, mode, NULL, bytes + segment, pos - segment, segment, listing_flags, xxbyte_python_resolver, file, out);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                segment = pos;
            }
            if (!xx_buf_appendf(out, "L%llu:\n", (unsigned long long)label)) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
        if (file->label_count == 0) {
            break;
        }
        if (xxbyte_decode(XXBYTE_FAMILY_PYTHON, mode, bytes + pos, code->size - pos, pos, &insn) == XXBYTE_STATUS_OK) {
            pos += insn.size;
        } else {
            ++pos;
        }
    }
    if (segment < code->size) {
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_PYTHON, mode, NULL, bytes + segment, code->size - segment, segment, listing_flags, xxbyte_python_resolver, file, out);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    status = list_exceptions(file, index, out);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    return list_text(out, "\n") ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

xxbyte_status xxbyte_python_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_python_file *file = NULL;
    xxbyte_status status = xxbyte_python_open_pyc(mode, data, size, &file);
    size_t i;
    uint64_t budget = (uint64_t)size * 4u + (1u << 20);
    uint64_t listed = 0;
    int cut = 0;

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    /* Shared code objects are listed each time they occur, as dis does;
     * the bytecode listed is bounded so that a small file can not ask for
     * an enormous listing. */
    for (i = 0; i < file->tree.walk_count && status == XXBYTE_STATUS_OK; ++i) {
        listed += file->tree.nodes[file->tree.codes[file->tree.walk[i].code].code].size;
        if (listed > budget) {
            cut = 1;
            break;
        }
        status = list_code(file, i, listing_flags, out);
    }
    if (status == XXBYTE_STATUS_OK && (cut || file->tree.walk_truncated) && !list_text(out, "# walk truncated\n")) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    xxbyte_python_close(file);
    return status;
}

#else
typedef int xxbyte_python_file_disabled;
#endif
