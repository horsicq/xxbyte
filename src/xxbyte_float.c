/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_internal.h"

/*
 * Shortest decimal text for IEEE binary32/binary64 values, independent of the
 * C library and its locale: the free-format algorithm of Burger and Dybvig
 * ("Printing Floating-Point Numbers Quickly and Accurately", 1996) on exact
 * fixed-size big integers. The digits are the shortest that read back as the
 * same value under round-half-even; of two such candidates the closer wins.
 */

/* 40 x 32 bits covers every intermediate value for a binary64. */
#define BN_WORDS 40

typedef struct bignum {
    uint32_t word[BN_WORDS];
    int length; /* words in use; 0 for zero */
} bignum;

static void bn_set(bignum *b, uint64_t value)
{
    memset(b, 0, sizeof(*b));
    b->word[0] = (uint32_t)value;
    b->word[1] = (uint32_t)(value >> 32);
    b->length = b->word[1] != 0 ? 2 : (b->word[0] != 0 ? 1 : 0);
}

static void bn_shl(bignum *b, unsigned bits)
{
    unsigned words = bits / 32u;
    unsigned shift = bits % 32u;
    int i;

    if (b->length == 0) {
        return;
    }
    if (shift != 0) {
        uint32_t carry = 0;
        for (i = 0; i < b->length; ++i) {
            uint32_t next = b->word[i] >> (32u - shift);
            b->word[i] = (b->word[i] << shift) | carry;
            carry = next;
        }
        if (carry != 0) {
            b->word[b->length++] = carry;
        }
    }
    if (words != 0) {
        for (i = b->length - 1; i >= 0; --i) {
            b->word[i + (int)words] = b->word[i];
        }
        for (i = 0; i < (int)words; ++i) {
            b->word[i] = 0;
        }
        b->length += (int)words;
    }
}

static void bn_mul_small(bignum *b, uint32_t factor)
{
    uint64_t carry = 0;
    int i;

    for (i = 0; i < b->length; ++i) {
        uint64_t product = (uint64_t)b->word[i] * factor + carry;
        b->word[i] = (uint32_t)product;
        carry = product >> 32;
    }
    if (carry != 0) {
        b->word[b->length++] = (uint32_t)carry;
    }
}

static void bn_mul_pow10(bignum *b, int exponent)
{
    while (exponent >= 9) {
        bn_mul_small(b, 1000000000u);
        exponent -= 9;
    }
    while (exponent-- > 0) {
        bn_mul_small(b, 10u);
    }
}

static int bn_cmp(const bignum *a, const bignum *b)
{
    int i;

    if (a->length != b->length) {
        return a->length < b->length ? -1 : 1;
    }
    for (i = a->length - 1; i >= 0; --i) {
        if (a->word[i] != b->word[i]) {
            return a->word[i] < b->word[i] ? -1 : 1;
        }
    }
    return 0;
}

/* Compares a + b with c. */
static int bn_cmp_sum(const bignum *a, const bignum *b, const bignum *c)
{
    bignum sum;
    int length = a->length > b->length ? a->length : b->length;
    uint64_t carry = 0;
    int i;

    for (i = 0; i < length; ++i) {
        uint64_t total = carry;
        total += i < a->length ? a->word[i] : 0u;
        total += i < b->length ? b->word[i] : 0u;
        sum.word[i] = (uint32_t)total;
        carry = total >> 32;
    }
    sum.length = length;
    if (carry != 0) {
        sum.word[sum.length++] = (uint32_t)carry;
    }
    return bn_cmp(&sum, c);
}

/* a -= b, where a >= b. */
static void bn_sub(bignum *a, const bignum *b)
{
    uint32_t borrow = 0;
    int i;

    for (i = 0; i < a->length; ++i) {
        uint64_t subtrahend = (uint64_t)(i < b->length ? b->word[i] : 0u) + borrow;
        borrow = (uint64_t)a->word[i] < subtrahend ? 1u : 0u;
        a->word[i] = (uint32_t)((uint64_t)a->word[i] - subtrahend);
    }
    while (a->length > 0 && a->word[a->length - 1] == 0) {
        --a->length;
    }
}

static int bit_length(uint64_t value)
{
    int bits = 0;
    while (value != 0) {
        ++bits;
        value >>= 1;
    }
    return bits;
}

/*
 * Writes the shortest digits of fraction * 2^exponent (fraction != 0) and
 * returns their count; *point receives k such that the value is
 * 0.d1d2... * 10^k. hidden_bit is the implicit leading bit of a normal
 * fraction and min_exponent the exponent of the subnormals.
 */
static int shortest_digits(uint64_t fraction, int exponent, uint64_t hidden_bit, int min_exponent, char *digits, int *point)
{
    /* value = r / s, rounding interval (r - m_minus, r + m_plus) / s */
    bignum r, s, m_minus, m_plus;
    int unequal_gaps = fraction == hidden_bit && exponent > min_exponent;
    int inclusive = (fraction & 1u) == 0;
    double estimate;
    int k;
    int count = 0;

    bn_set(&r, fraction);
    bn_set(&m_minus, 1);
    if (exponent >= 0) {
        bn_shl(&m_minus, (unsigned)exponent);
        m_plus = m_minus;
        if (unequal_gaps) {
            bn_shl(&m_plus, 1);
            bn_shl(&r, (unsigned)exponent + 2u);
            bn_set(&s, 4);
        } else {
            bn_shl(&r, (unsigned)exponent + 1u);
            bn_set(&s, 2);
        }
    } else {
        bn_set(&s, 1);
        if (unequal_gaps) {
            bn_set(&m_plus, 2);
            bn_shl(&r, 2);
            bn_shl(&s, (unsigned)(2 - exponent));
        } else {
            bn_set(&m_plus, 1);
            bn_shl(&r, 1);
            bn_shl(&s, (unsigned)(1 - exponent));
        }
    }

    /* k = ceil(log10(value)) from below; the loop corrects it upwards. */
    estimate = (double)(exponent + bit_length(fraction) - 1) * 0.30102999566398114 - 1e-10;
    k = (int)estimate;
    if (estimate > (double)k) {
        ++k;
    }
    if (k >= 0) {
        bn_mul_pow10(&s, k);
    } else {
        bn_mul_pow10(&r, -k);
        bn_mul_pow10(&m_minus, -k);
        bn_mul_pow10(&m_plus, -k);
    }
    for (;;) {
        int high = bn_cmp_sum(&r, &m_plus, &s);
        if (inclusive ? high < 0 : high <= 0) {
            break;
        }
        bn_mul_small(&s, 10u);
        ++k;
    }

    *point = k;
    for (;;) {
        int digit = 0;
        int low;
        int high;

        bn_mul_small(&r, 10u);
        bn_mul_small(&m_minus, 10u);
        bn_mul_small(&m_plus, 10u);
        while (bn_cmp(&r, &s) >= 0) {
            bn_sub(&r, &s);
            ++digit;
        }
        low = bn_cmp(&r, &m_minus);
        low = inclusive ? low <= 0 : low < 0;
        high = bn_cmp_sum(&r, &m_plus, &s);
        high = inclusive ? high >= 0 : high > 0;
        if (!low && !high) {
            digits[count++] = (char)('0' + digit);
            continue;
        }
        if (low && high) {
            /* Both neighbours read back: take the closer, ties to even. */
            bignum twice = r;
            int half;
            bn_shl(&twice, 1);
            half = bn_cmp(&twice, &s);
            if (half > 0 || (half == 0 && (digit & 1) != 0)) {
                ++digit;
            }
        } else if (high) {
            ++digit;
        }
        digits[count++] = (char)('0' + digit);
        return count;
    }
}

/*
 * Lays the shortest digits out like Python's repr: positional for decimal
 * exponents -4..15 ("0.0001", "1.5", "100"), otherwise scientific with at
 * least two exponent digits ("1e+16", "1.5e-05").
 */
static int format_shortest(xxbyte_fmt *fmt, int negative, uint64_t fraction, int exponent, uint64_t hidden_bit, int min_exponent)
{
    char digits[32];
    char text[64];
    int count;
    int point;
    int x;
    int n = 0;
    int i;

    if (negative) {
        text[n++] = '-';
    }
    if (fraction == 0) {
        text[n++] = '0';
        text[n] = '\0';
        return xxbyte_fmt_str(fmt, text);
    }
    count = shortest_digits(fraction, exponent, hidden_bit, min_exponent, digits, &point);
    x = point - 1;
    if (x >= -4 && x < 16) {
        if (x < 0) {
            text[n++] = '0';
            text[n++] = '.';
            for (i = 0; i < -x - 1; ++i) {
                text[n++] = '0';
            }
            for (i = 0; i < count; ++i) {
                text[n++] = digits[i];
            }
        } else {
            for (i = 0; i <= x; ++i) {
                text[n++] = i < count ? digits[i] : '0';
            }
            if (count > x + 1) {
                text[n++] = '.';
                for (i = x + 1; i < count; ++i) {
                    text[n++] = digits[i];
                }
            }
        }
    } else {
        int magnitude = x < 0 ? -x : x;
        text[n++] = digits[0];
        if (count > 1) {
            text[n++] = '.';
            for (i = 1; i < count; ++i) {
                text[n++] = digits[i];
            }
        }
        text[n++] = 'e';
        text[n++] = x < 0 ? '-' : '+';
        if (magnitude >= 100) {
            text[n++] = (char)('0' + magnitude / 100);
        }
        text[n++] = (char)('0' + magnitude / 10 % 10);
        text[n++] = (char)('0' + magnitude % 10);
    }
    text[n] = '\0';
    return xxbyte_fmt_str(fmt, text);
}

int xxbyte_fmt_double(xxbyte_fmt *fmt, double value)
{
    uint64_t bits;
    uint64_t fraction;
    int biased;
    int negative;

    memcpy(&bits, &value, sizeof(bits));
    negative = (int)(bits >> 63);
    biased = (int)((bits >> 52) & 0x7FFu);
    fraction = bits & ((UINT64_C(1) << 52) - 1u);
    if (biased == 0x7FF) {
        if (fraction != 0) {
            return xxbyte_fmt_str(fmt, "nan");
        }
        return xxbyte_fmt_str(fmt, negative ? "-inf" : "inf");
    }
    if (biased == 0) {
        return format_shortest(fmt, negative, fraction, -1074, UINT64_C(1) << 52, -1074);
    }
    return format_shortest(fmt, negative, fraction | (UINT64_C(1) << 52), biased - 1075, UINT64_C(1) << 52, -1074);
}

int xxbyte_fmt_float(xxbyte_fmt *fmt, float value)
{
    uint32_t bits;
    uint32_t fraction;
    int biased;
    int negative;

    memcpy(&bits, &value, sizeof(bits));
    negative = (int)(bits >> 31);
    biased = (int)((bits >> 23) & 0xFFu);
    fraction = bits & 0x7FFFFFu;
    if (biased == 0xFF) {
        if (fraction != 0) {
            return xxbyte_fmt_str(fmt, "nan");
        }
        return xxbyte_fmt_str(fmt, negative ? "-inf" : "inf");
    }
    if (biased == 0) {
        return format_shortest(fmt, negative, fraction, -149, UINT64_C(1) << 23, -149);
    }
    return format_shortest(fmt, negative, fraction | 0x800000u, biased - 150, UINT64_C(1) << 23, -149);
}
