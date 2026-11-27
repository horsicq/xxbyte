/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_internal.h"

/*
 * DEFLATE (RFC 1951) and zlib (RFC 1950) decoder, written from the RFCs.
 * Canonical Huffman codes are decoded through a FAST_BITS-wide lookup
 * table with a bit-by-bit canonical fallback for longer codes and for the
 * last few bits of the input. Tree validation follows zlib: over-subscribed
 * sets are rejected, and so are incomplete ones, except a literal/length or
 * distance set whose only code has length 1. An empty code-length set
 * decodes every symbol as length 0 using one bit, as zlib does.
 */

#define FAST_BITS 10
#define MAX_BITS 15

typedef struct {
    const uint8_t *in;
    size_t size;
    size_t pos;      /* next input byte to load */
    uint64_t bitbuf; /* loaded, unconsumed bits, LSB first */
    unsigned bitcnt;
} bit_reader;

typedef struct {
    uint16_t fast[1u << FAST_BITS]; /* (length << 9) | symbol; 0 = slow path */
    uint16_t count[MAX_BITS + 1];
    uint16_t symbol[288]; /* symbols sorted by code */
    unsigned maxlen;
} huffman;

enum {
    SET_CODES,
    SET_LENS,
    SET_DISTS
};

static const uint16_t length_base[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t dist_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                       193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t dist_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const uint8_t codelen_order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

static void br_fill(bit_reader *br)
{
    while (br->bitcnt <= 56 && br->pos < br->size) {
        br->bitbuf |= (uint64_t)br->in[br->pos++] << br->bitcnt;
        br->bitcnt += 8;
    }
}

/* Loads at least n (<= 32) bits; 0 when the input ends first. */
static int br_need(bit_reader *br, unsigned n)
{
    if (br->bitcnt < n) {
        br_fill(br);
    }
    return br->bitcnt >= n;
}

/* Callers made sure n bits are loaded. */
static unsigned br_take(bit_reader *br, unsigned n)
{
    unsigned v = (unsigned)(br->bitbuf & ((1u << n) - 1u));
    br->bitbuf >>= n;
    br->bitcnt -= n;
    return v;
}

/* Drops the bits of a partly used byte and returns the whole loaded bytes
 * to the input, so that byte-aligned data can be read from in[pos..]. */
static void br_align(bit_reader *br)
{
    br->pos -= br->bitcnt >> 3;
    br->bitbuf = 0;
    br->bitcnt = 0;
}

static size_t br_used(const bit_reader *br)
{
    return br->pos - (br->bitcnt >> 3);
}

static uint16_t reverse_bits(unsigned code, unsigned len)
{
    unsigned r = 0;
    while (len--) {
        r = (r << 1) | (code & 1u);
        code >>= 1;
    }
    return (uint16_t)r;
}

/* Builds h from n code lengths; 0 on success, -1 for a set zlib rejects. */
static int huffman_build(huffman *h, const uint8_t *lens, unsigned n, int set)
{
    uint16_t offs[MAX_BITS + 2];
    unsigned next[MAX_BITS + 1];
    unsigned len, s;
    int left = 1;

    memset(h->count, 0, sizeof(h->count));
    memset(h->fast, 0, sizeof(h->fast));
    for (s = 0; s < n; s++) {
        h->count[lens[s]]++;
    }
    h->count[0] = 0;
    h->maxlen = 0;
    for (len = 1; len <= MAX_BITS; len++) {
        if (h->count[len]) {
            h->maxlen = len;
        }
    }
    if (h->maxlen == 0) {
        if (set == SET_CODES) {
            for (s = 0; s < (1u << FAST_BITS); s++) {
                h->fast[s] = 1u << 9; /* length 1, symbol 0 */
            }
        }
        return 0;
    }
    for (len = 1; len <= MAX_BITS; len++) {
        left = 2 * left - h->count[len];
        if (left < 0) {
            return -1; /* over-subscribed */
        }
    }
    if (left > 0 && (set == SET_CODES || h->maxlen != 1)) {
        return -1; /* incomplete */
    }
    offs[1] = 0;
    next[1] = 0;
    for (len = 1; len <= MAX_BITS; len++) {
        offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
        if (len > 1) {
            next[len] = (next[len - 1] + h->count[len - 1]) << 1;
        }
    }
    for (s = 0; s < n; s++) {
        len = lens[s];
        if (len == 0) {
            continue;
        }
        h->symbol[offs[len]++] = (uint16_t)s;
        if (len <= FAST_BITS) {
            unsigned r = reverse_bits(next[len], len);
            for (; r < (1u << FAST_BITS); r += 1u << len) {
                h->fast[r] = (uint16_t)((len << 9) | s);
            }
        }
        next[len]++;
    }
    return 0;
}

/* Decodes one symbol: OK, TRUNCATED or INVALID_INSTRUCTION (no such code). */
static xxbyte_status huffman_decode(bit_reader *br, const huffman *h, unsigned *symbol)
{
    unsigned e, len, lim, code = 0, first = 0, index = 0;

    if (br->bitcnt < MAX_BITS) {
        br_fill(br);
    }
    e = h->fast[br->bitbuf & ((1u << FAST_BITS) - 1u)];
    if (e != 0 && (e >> 9) <= br->bitcnt) {
        br_take(br, e >> 9);
        *symbol = e & 0x1FFu;
        return XXBYTE_STATUS_OK;
    }
    lim = h->maxlen ? h->maxlen : 1;
    for (len = 1; len <= lim; len++) {
        if (len > br->bitcnt) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        code |= (unsigned)(br->bitbuf >> (len - 1)) & 1u;
        if (code - first < h->count[len]) {
            br_take(br, len);
            *symbol = h->symbol[index + code - first];
            return XXBYTE_STATUS_OK;
        }
        index += h->count[len];
        first = (first + h->count[len]) << 1;
        code <<= 1;
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

#define TRY(x)                         \
    do {                               \
        xxbyte_status st_ = (x);       \
        if (st_ != XXBYTE_STATUS_OK) { \
            return st_;                \
        }                              \
    } while (0)
#define NEED(br, n)                         \
    do {                                    \
        if (!br_need((br), (n))) {          \
            return XXBYTE_STATUS_TRUNCATED; \
        }                                   \
    } while (0)

typedef struct {
    uint8_t *out;
    size_t capacity;
    size_t pos;
} out_window;

static xxbyte_status inflate_stored(bit_reader *br, out_window *ow)
{
    size_t len, avail_in, avail_out;

    br_align(br);
    if (br->size - br->pos < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    len = xxbyte_u16le(br->in + br->pos);
    if ((len ^ xxbyte_u16le(br->in + br->pos + 2)) != 0xFFFFu) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    br->pos += 4;
    avail_in = br->size - br->pos;
    avail_out = ow->capacity - ow->pos;
    if (len > avail_in || len > avail_out) {
        size_t n = avail_in < avail_out ? avail_in : avail_out;
        memcpy(ow->out + ow->pos, br->in + br->pos, n);
        ow->pos += n;
        br->pos += n;
        return avail_in <= avail_out ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    memcpy(ow->out + ow->pos, br->in + br->pos, len);
    ow->pos += len;
    br->pos += len;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status inflate_codes(bit_reader *br, out_window *ow, const huffman *lit, const huffman *dist)
{
    for (;;) {
        unsigned sym, len, d;
        size_t n;

        TRY(huffman_decode(br, lit, &sym));
        if (sym < 256) {
            if (ow->pos == ow->capacity) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            ow->out[ow->pos++] = (uint8_t)sym;
            continue;
        }
        if (sym == 256) {
            return XXBYTE_STATUS_OK;
        }
        sym -= 257;
        if (sym >= 29) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        NEED(br, length_extra[sym]);
        len = length_base[sym] + br_take(br, length_extra[sym]);
        TRY(huffman_decode(br, dist, &sym));
        if (sym >= 30) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        NEED(br, dist_extra[sym]);
        d = dist_base[sym] + br_take(br, dist_extra[sym]);
        if (d > ow->pos) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION; /* too far back */
        }
        n = ow->capacity - ow->pos;
        if (n > len) {
            n = len;
        }
        if (d >= n) {
            memcpy(ow->out + ow->pos, ow->out + ow->pos - d, n);
        } else {
            /* Overlapping copy: byte by byte from d bytes back. */
            uint8_t *dst = ow->out + ow->pos;
            const uint8_t *src = dst - d;
            size_t i;
            for (i = 0; i < n; i++) {
                dst[i] = src[i];
            }
        }
        ow->pos += n;
        if (n < len) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
}

static xxbyte_status inflate_fixed(bit_reader *br, out_window *ow, huffman *lit, huffman *dist)
{
    uint8_t lens[288];

    memset(lens, 8, 144);
    memset(lens + 144, 9, 112);
    memset(lens + 256, 7, 24);
    memset(lens + 280, 8, 8);
    (void)huffman_build(lit, lens, 288, SET_LENS);
    memset(lens, 5, 32);
    (void)huffman_build(dist, lens, 32, SET_DISTS);
    return inflate_codes(br, ow, lit, dist);
}

static xxbyte_status inflate_dynamic(bit_reader *br, out_window *ow, huffman *lit, huffman *dist)
{
    uint8_t lens[286 + 30];
    unsigned nlen, ndist, ncode, i, sym;

    NEED(br, 14);
    nlen = br_take(br, 5) + 257;
    ndist = br_take(br, 5) + 1;
    ncode = br_take(br, 4) + 4;
    if (nlen > 286 || ndist > 30) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    memset(lens, 0, 19);
    for (i = 0; i < ncode; i++) {
        NEED(br, 3);
        lens[codelen_order[i]] = (uint8_t)br_take(br, 3);
    }
    if (huffman_build(lit, lens, 19, SET_CODES) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    for (i = 0; i < nlen + ndist;) {
        unsigned rep, value = 0;

        TRY(huffman_decode(br, lit, &sym));
        if (sym < 16) {
            lens[i++] = (uint8_t)sym;
            continue;
        }
        if (sym == 16) {
            NEED(br, 2);
            rep = 3 + br_take(br, 2);
            if (i == 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            value = lens[i - 1];
        } else if (sym == 17) {
            NEED(br, 3);
            rep = 3 + br_take(br, 3);
        } else {
            NEED(br, 7);
            rep = 11 + br_take(br, 7);
        }
        if (i + rep > nlen + ndist) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        memset(lens + i, (int)value, rep);
        i += rep;
    }
    if (lens[256] == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION; /* no end-of-block code */
    }
    if (huffman_build(lit, lens, nlen, SET_LENS) != 0 || huffman_build(dist, lens + nlen, ndist, SET_DISTS) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return inflate_codes(br, ow, lit, dist);
}

static uint32_t adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    while (n > 0) {
        size_t chunk = n < 5552 ? n : 5552; /* keeps b below 2^32 */
        n -= chunk;
        while (chunk--) {
            a += *p++;
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
    }
    return (b << 16) | a;
}

static xxbyte_status inflate_stream(bit_reader *br, int zlib, out_window *ow)
{
    huffman lit, dist;
    unsigned final, type;

    if (zlib) {
        unsigned cmf, flg;
        if (br->size < 2) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        cmf = br->in[0];
        flg = br->in[1];
        br->pos = 2;
        if ((cmf * 256u + flg) % 31u != 0 || (cmf & 15u) != 8 || (cmf >> 4) > 7) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (flg & 0x20u) {
            /* A preset dictionary is not supported; zlib reads DICTID first. */
            if (br->size < 6) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            br->pos = 6;
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    do {
        NEED(br, 3);
        final = br_take(br, 1);
        type = br_take(br, 2);
        if (type == 0) {
            TRY(inflate_stored(br, ow));
        } else if (type == 1) {
            TRY(inflate_fixed(br, ow, &lit, &dist));
        } else if (type == 2) {
            TRY(inflate_dynamic(br, ow, &lit, &dist));
        } else {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    } while (!final);
    if (zlib) {
        const uint8_t *t;
        br_align(br);
        if (br->size - br->pos < 4) {
            br->pos = br->size;
            return XXBYTE_STATUS_TRUNCATED;
        }
        t = br->in + br->pos;
        br->pos += 4;
        if (adler32(ow->out, ow->pos) != ((uint32_t)t[0] << 24 | (uint32_t)t[1] << 16 | (uint32_t)t[2] << 8 | t[3])) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_inflate(const uint8_t *in, size_t in_size, int zlib, uint8_t *out, size_t out_capacity, size_t *out_size, size_t *in_used)
{
    bit_reader br;
    out_window ow;
    xxbyte_status st;

    if (in == NULL || out == NULL || out_size == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    br.in = in;
    br.size = in_size;
    br.pos = 0;
    br.bitbuf = 0;
    br.bitcnt = 0;
    ow.out = out;
    ow.capacity = out_capacity;
    ow.pos = 0;
    st = inflate_stream(&br, zlib, &ow);
    *out_size = ow.pos;
    if (in_used != NULL) {
        *in_used = br_used(&br);
    }
    return st;
}
