/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_LUA_NUMBER_H
#define XXBYTE_LUA_NUMBER_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_LUA || XXBYTE_USE_LUAJIT

/* Room for any xxbyte_lua_format_g result and its NUL. */
#define XXBYTE_LUA_G_SIZE 32u

/* Exact %.<precision>g: the double is m * 2^e; its decimal digits come from the
 * integer m * 2^e (e >= 0) or m * 5^-e (e < 0, scaled by 10^e). */

#define XXBYTE_LUA_G_MAX_DIGITS 17u
#define XXBYTE_LUA_G_WORDS 82u /* 53 + 1074 * log2(5) bits, rounded up */
#define XXBYTE_LUA_G_DECIMALS 800u

typedef struct xxbyte_g_big {
    uint32_t w[XXBYTE_LUA_G_WORDS]; /* little-endian 32-bit words */
    uint32_t n;                     /* words in use */
} xxbyte_g_big;

static inline void g_big_mul_small(xxbyte_g_big *b, uint32_t factor)
{
    uint64_t carry = 0;
    uint32_t i;

    for (i = 0; i < b->n; ++i) {
        uint64_t v = (uint64_t)b->w[i] * factor + carry;
        b->w[i] = (uint32_t)v;
        carry = v >> 32;
    }
    if (carry != 0 && b->n < XXBYTE_LUA_G_WORDS) {
        b->w[b->n++] = (uint32_t)carry;
    }
}

static inline void g_big_shift_left(xxbyte_g_big *b, uint32_t bits)
{
    uint32_t words = bits / 32u;
    uint32_t rest = bits % 32u;
    uint32_t i;

    if (b->n == 0) {
        return;
    }
    if (words != 0) {
        for (i = b->n; i-- > 0;) {
            b->w[i + words] = b->w[i];
        }
        for (i = 0; i < words; ++i) {
            b->w[i] = 0;
        }
        b->n += words;
    }
    if (rest != 0) {
        uint32_t carry = 0;
        for (i = words; i < b->n; ++i) {
            uint32_t v = b->w[i];
            b->w[i] = (v << rest) | carry;
            carry = v >> (32u - rest);
        }
        if (carry != 0) {
            b->w[b->n++] = carry;
        }
    }
}

/* Divides by 10^9 in place and returns the remainder. */
static inline uint32_t g_big_div_1e9(xxbyte_g_big *b)
{
    uint64_t rem = 0;
    uint32_t i;

    for (i = b->n; i-- > 0;) {
        uint64_t v = (rem << 32) | b->w[i];
        b->w[i] = (uint32_t)(v / 1000000000u);
        rem = v % 1000000000u;
    }
    while (b->n > 0 && b->w[b->n - 1] == 0) {
        --b->n;
    }
    return (uint32_t)rem;
}

/* Writes the decimal digits of b (destroyed) to digits; returns the count. */
static inline uint32_t g_big_to_decimal(xxbyte_g_big *b, char *digits)
{
    char reversed[XXBYTE_LUA_G_DECIMALS];
    uint32_t count = 0;
    uint32_t i;

    while (b->n > 0) {
        uint32_t chunk = g_big_div_1e9(b);
        for (i = 0; i < 9u; ++i) {
            reversed[count++] = (char)('0' + chunk % 10u);
            chunk /= 10u;
        }
    }
    while (count > 1 && reversed[count - 1] == '0') {
        --count;
    }
    for (i = 0; i < count; ++i) {
        digits[i] = reversed[count - 1u - i];
    }
    return count;
}

static inline size_t g_put_text(char *text, size_t at, const char *s)
{
    while (*s != '\0') {
        text[at++] = *s++;
    }
    text[at] = '\0';
    return at;
}

/*
 * Formats the IEEE-754 double with the given bits as C's printf("%.Ng")
 * does for N = precision (1 to 17), as LUA_NUMBER_FMT ("%.14g", 5.5
 * "%.15g", float builds "%.7g") and LuaJIT's tostring ("%.14g") use it:
 * N significant digits,
 * correctly rounded from the exact binary value with ties to even,
 * trailing zeros removed, "e+NN" exponents. The infinities print as "inf"
 * and "-inf" and NaN as "nan", or "-nan" when signed_nan is nonzero and
 * the sign bit is set (glibc's printf). Independent of the locale and the
 * C library. Writes a NUL-terminated string to text, which must hold
 * XXBYTE_LUA_G_SIZE bytes, and returns its length. Header-only, shared
 * by the Lua and LuaJIT listings (either family may be built alone).
 */
static inline size_t xxbyte_lua_format_g(uint64_t bits, uint32_t precision, int signed_nan, char *text)
{
    char digits[XXBYTE_LUA_G_DECIMALS];
    xxbyte_g_big big;
    int negative = (bits >> 63) != 0;
    uint32_t exponent = (uint32_t)(bits >> 52) & 0x7FFu;
    uint64_t mantissa = bits & ((UINT64_C(1) << 52) - 1u);
    int32_t e;
    int32_t scale = 0; /* value = digits * 10^-scale */
    int32_t x;         /* decimal exponent of the first digit */
    uint32_t count;
    uint32_t i;
    size_t at = 0;

    if (precision == 0) {
        precision = 1;
    } else if (precision > XXBYTE_LUA_G_MAX_DIGITS) {
        precision = XXBYTE_LUA_G_MAX_DIGITS;
    }
    if (exponent == 0x7FFu) {
        if (mantissa != 0) {
            return g_put_text(text, 0, negative && signed_nan ? "-nan" : "nan");
        }
        return g_put_text(text, 0, negative ? "-inf" : "inf");
    }
    if (negative) {
        text[at++] = '-';
    }
    if (exponent == 0 && mantissa == 0) {
        return g_put_text(text, at, "0");
    }
    if (exponent == 0) {
        e = -1074;
    } else {
        mantissa |= UINT64_C(1) << 52;
        e = (int32_t)exponent - 1075;
    }
    while ((mantissa & 1u) == 0 && e < 0) {
        mantissa >>= 1;
        ++e;
    }
    memset(&big, 0, sizeof(big));
    big.w[0] = (uint32_t)mantissa;
    big.w[1] = (uint32_t)(mantissa >> 32);
    big.n = big.w[1] != 0 ? 2u : 1u;
    if (e >= 0) {
        g_big_shift_left(&big, (uint32_t)e);
    } else {
        int32_t k = -e;
        scale = k;
        while (k >= 13) {
            g_big_mul_small(&big, 1220703125u); /* 5^13 */
            k -= 13;
        }
        while (k-- > 0) {
            g_big_mul_small(&big, 5u);
        }
    }
    count = g_big_to_decimal(&big, digits);
    x = (int32_t)count - 1 - scale;
    if (count > precision) {
        char next = digits[precision];
        int rest = 0;
        int up;
        for (i = precision + 1u; i < count; ++i) {
            if (digits[i] != '0') {
                rest = 1;
                break;
            }
        }
        up = next > '5' || (next == '5' && (rest || ((digits[precision - 1] - '0') & 1) != 0));
        count = precision;
        if (up) {
            i = count;
            while (i > 0) {
                --i;
                if (digits[i] != '9') {
                    ++digits[i];
                    break;
                }
                digits[i] = '0';
                if (i == 0) {
                    digits[0] = '1';
                    ++x;
                }
            }
        }
    }
    while (count > 1 && digits[count - 1] == '0') {
        --count;
    }
    if (x < -4 || x >= (int32_t)precision) {
        uint32_t magnitude = (uint32_t)(x < 0 ? -x : x);
        text[at++] = digits[0];
        if (count > 1) {
            text[at++] = '.';
            for (i = 1; i < count; ++i) {
                text[at++] = digits[i];
            }
        }
        text[at++] = 'e';
        text[at++] = x < 0 ? '-' : '+';
        if (magnitude >= 100) {
            text[at++] = (char)('0' + magnitude / 100u);
        }
        text[at++] = (char)('0' + magnitude / 10u % 10u);
        text[at++] = (char)('0' + magnitude % 10u);
    } else if (x >= 0) {
        for (i = 0; i <= (uint32_t)x; ++i) {
            text[at++] = i < count ? digits[i] : '0';
        }
        if (count > (uint32_t)x + 1u) {
            text[at++] = '.';
            for (i = (uint32_t)x + 1u; i < count; ++i) {
                text[at++] = digits[i];
            }
        }
    } else {
        text[at++] = '0';
        text[at++] = '.';
        for (i = 1; i < (uint32_t)-x; ++i) {
            text[at++] = '0';
        }
        for (i = 0; i < count; ++i) {
            text[at++] = digits[i];
        }
    }
    text[at] = '\0';
    return at;
}

/* The double bits of a float32 value given by its bits (exact). */
static inline uint64_t xxbyte_lua_float_bits(uint32_t bits)
{
    uint64_t sign = (uint64_t)(bits >> 31) << 63;
    uint32_t exponent = (bits >> 23) & 0xFFu;
    uint32_t mantissa = bits & 0x7FFFFFu;
    int32_t e;

    if (exponent == 0xFFu) {
        return sign | (UINT64_C(0x7FF) << 52) | ((uint64_t)mantissa << 29);
    }
    if (exponent == 0) {
        if (mantissa == 0) {
            return sign;
        }
        e = -126;
        while ((mantissa & 0x800000u) == 0) {
            mantissa <<= 1;
            --e;
        }
        mantissa &= 0x7FFFFFu;
    } else {
        e = (int32_t)exponent - 127;
    }
    return sign | ((uint64_t)(uint32_t)(e + 1023) << 52) | ((uint64_t)mantissa << 29);
}

#endif

#endif
