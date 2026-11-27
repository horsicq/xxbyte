/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_gdscript_internal.h"

#include <xxfclib/memory/xx_memory.h>

#if XXBYTE_USE_GDSCRIPT

/*
 * A bounded Zstandard decoder written from RFC 8878 (Zstandard Compression
 * and the 'application/zstd' Media Type): one frame, no dictionary, the
 * whole output in one buffer. Compressed 4.x .gdc files hold such a frame
 * (Compression::compress with MODE_ZSTD).
 */

#define ZSTD_MAGIC 0xFD2FB528u
#define BLOCK_MAX (128u * 1024u)
#define LL_MAX_LOG 9u
#define ML_MAX_LOG 9u
#define OF_MAX_LOG 8u
#define HUF_MAX_BITS 11u
#define HUF_WEIGHT_LOG 6u
#define FSE_MAX_SYMBOLS 256u

typedef struct fse_cell {
    uint16_t base;
    uint8_t symbol;
    uint8_t bits;
} fse_cell;

typedef struct fse_table {
    uint32_t log;
    fse_cell cells[1u << LL_MAX_LOG];
} fse_table;

typedef struct huf_cell {
    uint8_t symbol;
    uint8_t bits;
} huf_cell;

typedef struct zstd_state {
    const uint8_t *in;
    size_t in_size;
    uint8_t *out;
    size_t out_size;
    size_t produced;
    uint32_t repeat[3];
    fse_table ll;
    fse_table of;
    fse_table ml;
    int have_ll;
    int have_of;
    int have_ml;
    huf_cell huf[1u << HUF_MAX_BITS];
    uint32_t huf_bits; /* max bits; 0 until a table is read */
    uint8_t literals[BLOCK_MAX];
} zstd_state;

/* ------------------------------------------------------------------------ */
/* Backward bit stream. */

typedef struct bits_in {
    const uint8_t *data;
    size_t size;
    int64_t position; /* bits not yet consumed (counted from bit 0) */
    int overflow;
} bits_in;

static xxbyte_status bits_init(bits_in *b, const uint8_t *data, size_t size)
{
    uint8_t last;
    unsigned top = 7;

    if (size == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    last = data[size - 1];
    if (last == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    while ((((unsigned)last >> top) & 1u) == 0) {
        --top;
    }
    b->data = data;
    b->size = size;
    b->position = (int64_t)(size - 1) * 8 + (int64_t)top;
    b->overflow = 0;
    return XXBYTE_STATUS_OK;
}

/* Bits [low, low + count) of the stream, count <= 32; bits outside the
 * stream read as 0. */
static uint32_t bits_at(const bits_in *b, int64_t low, uint32_t count)
{
    uint64_t value = 0;
    int64_t first;
    int64_t i;
    uint32_t shift;

    if (count == 0) {
        return 0;
    }
    if (low < 0) {
        uint32_t missing = (uint32_t)(-low);

        if (missing >= count) {
            return 0;
        }
        return bits_at(b, 0, count - missing) << missing;
    }
    first = low >> 3;
    shift = (uint32_t)(low & 7);
    for (i = 0; i < 5; ++i) {
        if ((uint64_t)(first + i) < b->size) {
            value |= (uint64_t)b->data[first + i] << (8 * i);
        }
    }
    return (uint32_t)((value >> shift) & ((UINT64_C(1) << count) - 1u));
}

static uint32_t bits_read(bits_in *b, uint32_t count)
{
    uint32_t value;

    b->position -= count;
    if (b->position < 0) {
        b->overflow = 1;
    }
    value = bits_at(b, b->position, count);
    return value;
}

static uint32_t bits_peek(const bits_in *b, uint32_t count)
{
    return bits_at(b, b->position - (int64_t)count, count);
}

/* ------------------------------------------------------------------------ */
/* FSE. */

static uint32_t highbit(uint32_t v)
{
    uint32_t n = 0;

    while (v >>= 1) {
        ++n;
    }
    return n;
}

/* Builds a decoding table from normalized counts (-1 = "less than 1"). */
static xxbyte_status fse_build(fse_table *t, const int16_t *norm, uint32_t symbols, uint32_t log)
{
    uint32_t size = 1u << log;
    uint32_t high = size - 1u;
    uint32_t step = (size >> 1) + (size >> 3) + 3u;
    uint32_t mask = size - 1u;
    uint32_t position = 0;
    uint16_t next[FSE_MAX_SYMBOLS];
    uint32_t s;
    uint32_t i;

    t->log = log;
    for (s = 0; s < symbols; ++s) {
        if (norm[s] == -1) {
            t->cells[high].symbol = (uint8_t)s;
            --high;
            next[s] = 1;
        } else {
            next[s] = (uint16_t)norm[s];
        }
    }
    for (s = 0; s < symbols; ++s) {
        int16_t k;

        for (k = 0; k < norm[s]; ++k) {
            t->cells[position].symbol = (uint8_t)s;
            do {
                position = (position + step) & mask;
            } while (position > high);
        }
    }
    if (position != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    for (i = 0; i < size; ++i) {
        uint32_t symbol = t->cells[i].symbol;
        uint32_t state = next[symbol]++;
        uint32_t bits = log - highbit(state);

        t->cells[i].bits = (uint8_t)bits;
        t->cells[i].base = (uint16_t)((state << bits) - size);
    }
    return XXBYTE_STATUS_OK;
}

/* Bits [position, position + count) of a forward little-endian stream,
 * count <= 16, or 0xFFFFFFFF past its end. */
static uint32_t forward_bits(const uint8_t *data, size_t size, uint64_t position, uint32_t count)
{
    uint32_t value = 0;
    uint32_t i;

    if (position + count > (uint64_t)size * 8u) {
        return 0xFFFFFFFFu;
    }
    for (i = 0; i < count; ++i) {
        uint64_t bit = position + i;

        value |= ((uint32_t)(data[bit >> 3] >> (bit & 7u)) & 1u) << i;
    }
    return value;
}

/* Reads an FSE table description (RFC 8878 4.1.1) from data[0..size);
 * *used receives its byte size. */
static xxbyte_status fse_read(fse_table *t, const uint8_t *data, size_t size, uint32_t max_log, uint32_t max_symbol, size_t *used)
{
    int16_t norm[FSE_MAX_SYMBOLS];
    uint64_t position = 4;
    uint32_t log;
    int32_t remaining;
    uint32_t symbol = 0;

    if (size == 0) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    memset(norm, 0, sizeof(norm));
    log = (data[0] & 15u) + 5u;
    if (log > max_log) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    remaining = (int32_t)(1u << log) + 1;
    while (remaining > 1) {
        uint32_t bits = highbit((uint32_t)remaining) + 1u;
        uint32_t max_small = (1u << bits) - 1u - (uint32_t)remaining;
        uint32_t value;
        int32_t probability;

        if (symbol > max_symbol) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        value = forward_bits(data, size, position, bits - 1u);
        if (value == 0xFFFFFFFFu) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (value < max_small) {
            position += bits - 1u;
        } else {
            value = forward_bits(data, size, position, bits);
            if (value == 0xFFFFFFFFu) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            if (value >= (1u << (bits - 1u))) {
                value -= max_small;
            }
            position += bits;
        }
        probability = (int32_t)value - 1;
        remaining -= probability < 0 ? -probability : probability;
        norm[symbol++] = (int16_t)probability;
        if (probability == 0) {
            /* 2-bit repeat flags: that many more zeros, and 3 means another
             * flag follows. */
            for (;;) {
                uint32_t repeat = forward_bits(data, size, position, 2);
                uint32_t k;

                if (repeat == 0xFFFFFFFFu) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                position += 2;
                if (symbol + repeat > max_symbol + 1u) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                for (k = 0; k < repeat; ++k) {
                    norm[symbol++] = 0;
                }
                if (repeat != 3u) {
                    break;
                }
            }
        }
    }
    if (remaining != 1 || symbol > max_symbol + 1u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *used = (size_t)((position + 7u) >> 3);
    return fse_build(t, norm, symbol, log);
}

static void fse_rle(fse_table *t, uint8_t symbol)
{
    t->log = 0;
    t->cells[0].symbol = symbol;
    t->cells[0].bits = 0;
    t->cells[0].base = 0;
}

/* ------------------------------------------------------------------------ */
/* Huffman literals. */

static xxbyte_status huf_read(zstd_state *z, const uint8_t *data, size_t size, size_t *used)
{
    uint8_t weights[256];
    uint32_t count;
    uint32_t i;
    uint32_t total = 0;
    uint32_t max_bits;
    uint32_t last;
    uint32_t rank_start[HUF_MAX_BITS + 2];
    uint32_t position;

    if (size == 0) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    memset(weights, 0, sizeof(weights));
    if (data[0] >= 128u) {
        count = data[0] - 127u;
        if (1u + (count + 1u) / 2u > size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        for (i = 0; i < count; ++i) {
            uint8_t b = data[1u + i / 2u];

            weights[i] = (uint8_t)((i & 1u) == 0 ? b >> 4 : b & 15u);
        }
        *used = 1u + (count + 1u) / 2u;
    } else {
        /* FSE-compressed weights, two interleaved states. */
        size_t header;
        bits_in b;
        uint32_t s1;
        uint32_t s2;
        size_t compressed = data[0];

        if (compressed == 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (1u + compressed > size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        {
            fse_table local;

            if (fse_read(&local, data + 1, compressed, HUF_WEIGHT_LOG, 255u, &header) != XXBYTE_STATUS_OK || header >= compressed) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (bits_init(&b, data + 1 + header, compressed - header) != XXBYTE_STATUS_OK) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            s1 = bits_read(&b, local.log);
            s2 = bits_read(&b, local.log);
            count = 0;
            for (;;) {
                if (count >= 255u) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                weights[count++] = local.cells[s1].symbol;
                s1 = local.cells[s1].base + bits_read(&b, local.cells[s1].bits);
                if (b.overflow) {
                    if (count >= 255u) {
                        return XXBYTE_STATUS_INVALID_INSTRUCTION;
                    }
                    weights[count++] = local.cells[s2].symbol;
                    break;
                }
                if (count >= 255u) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                weights[count++] = local.cells[s2].symbol;
                s2 = local.cells[s2].base + bits_read(&b, local.cells[s2].bits);
                if (b.overflow) {
                    if (count >= 255u) {
                        return XXBYTE_STATUS_INVALID_INSTRUCTION;
                    }
                    weights[count++] = local.cells[s1].symbol;
                    break;
                }
            }
        }
        *used = 1u + compressed;
    }
    for (i = 0; i < count; ++i) {
        if (weights[i] > HUF_MAX_BITS) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (weights[i] != 0) {
            total += 1u << (weights[i] - 1u);
        }
    }
    if (total == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    max_bits = highbit(total) + 1u;
    if (max_bits > HUF_MAX_BITS) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    last = (1u << max_bits) - total;
    if ((last & (last - 1u)) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    weights[count++] = (uint8_t)(highbit(last) + 1u);
    /* Codes: symbols of weight w get max_bits + 1 - w bits; the table is
     * filled by increasing weight, in symbol order. */
    memset(rank_start, 0, sizeof(rank_start));
    position = 0;
    {
        uint32_t w;

        for (w = 1; w <= max_bits; ++w) {
            for (i = 0; i < count; ++i) {
                if (weights[i] == w) {
                    uint32_t bits = max_bits + 1u - w;
                    uint32_t span = 1u << (max_bits - bits);
                    uint32_t k;

                    if (position + span > (1u << max_bits)) {
                        return XXBYTE_STATUS_INVALID_INSTRUCTION;
                    }
                    for (k = 0; k < span; ++k) {
                        z->huf[position + k].symbol = (uint8_t)i;
                        z->huf[position + k].bits = (uint8_t)bits;
                    }
                    position += span;
                }
            }
        }
    }
    if (position != (1u << max_bits)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    z->huf_bits = max_bits;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status huf_stream(const zstd_state *z, const uint8_t *data, size_t size, uint8_t *out, size_t count)
{
    bits_in b;
    size_t i;

    if (bits_init(&b, data, size) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    for (i = 0; i < count; ++i) {
        const huf_cell *cell = &z->huf[bits_peek(&b, z->huf_bits)];

        out[i] = cell->symbol;
        b.position -= cell->bits;
        if (b.position < 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    return b.position == 0 ? XXBYTE_STATUS_OK : XXBYTE_STATUS_INVALID_INSTRUCTION;
}

/* Reads the literals section; *used receives its size, *count the
 * literal count. */
static xxbyte_status read_literals(zstd_state *z, const uint8_t *data, size_t size, size_t *used, size_t *count)
{
    uint32_t type;
    uint32_t format;
    size_t header;
    size_t regenerated;
    size_t compressed = 0;
    int streams4 = 0;

    if (size == 0) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    type = data[0] & 3u;
    format = (data[0] >> 2) & 3u;
    if (type < 2) {
        switch (format) {
            case 0:
            case 2:
                header = 1;
                regenerated = data[0] >> 3;
                break;
            case 1:
                header = 2;
                if (size < 2) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                regenerated = (size_t)(data[0] >> 4) | ((size_t)data[1] << 4);
                break;
            default:
                header = 3;
                if (size < 3) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                regenerated = (size_t)(data[0] >> 4) | ((size_t)data[1] << 4) | ((size_t)data[2] << 12);
                break;
        }
        if (regenerated > BLOCK_MAX) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (type == 0) {
            if (size - header < regenerated) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            memcpy(z->literals, data + header, regenerated);
            *used = header + regenerated;
        } else {
            if (size - header < 1) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            memset(z->literals, data[header], regenerated);
            *used = header + 1;
        }
        *count = regenerated;
        return XXBYTE_STATUS_OK;
    }
    switch (format) {
        case 0:
        case 1:
            header = 3;
            if (size < 3) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            {
                uint32_t v = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);

                regenerated = (v >> 4) & 0x3FFu;
                compressed = (v >> 14) & 0x3FFu;
            }
            streams4 = format == 1;
            break;
        case 2:
            header = 4;
            if (size < 4) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            {
                uint32_t v = xxbyte_u32le(data);

                regenerated = (v >> 4) & 0x3FFFu;
                compressed = (v >> 18) & 0x3FFFu;
            }
            streams4 = 1;
            break;
        default:
            header = 5;
            if (size < 5) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            {
                uint64_t v = (uint64_t)xxbyte_u32le(data) | ((uint64_t)data[4] << 32);

                regenerated = (size_t)((v >> 4) & 0x3FFFFu);
                compressed = (size_t)((v >> 22) & 0x3FFFFu);
            }
            streams4 = 1;
            break;
    }
    if (regenerated > BLOCK_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (size - header < compressed) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    {
        const uint8_t *p = data + header;
        size_t left = compressed;
        xxbyte_status status;

        if (type == 2) {
            size_t tree;

            status = huf_read(z, p, left, &tree);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (tree > left) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            p += tree;
            left -= tree;
        } else if (z->huf_bits == 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (!streams4) {
            status = huf_stream(z, p, left, z->literals, regenerated);
        } else {
            size_t s1;
            size_t s2;
            size_t s3;
            size_t s4;
            size_t part = (regenerated + 3u) / 4u;

            if (left < 6) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            s1 = xxbyte_u16le(p);
            s2 = xxbyte_u16le(p + 2);
            s3 = xxbyte_u16le(p + 4);
            if (s1 + s2 + s3 > left - 6u || part * 3u > regenerated) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            s4 = left - 6u - s1 - s2 - s3;
            p += 6;
            status = huf_stream(z, p, s1, z->literals, part);
            if (status == XXBYTE_STATUS_OK) {
                status = huf_stream(z, p + s1, s2, z->literals + part, part);
            }
            if (status == XXBYTE_STATUS_OK) {
                status = huf_stream(z, p + s1 + s2, s3, z->literals + 2u * part, part);
            }
            if (status == XXBYTE_STATUS_OK) {
                status = huf_stream(z, p + s1 + s2 + s3, s4, z->literals + 3u * part, regenerated - 3u * part);
            }
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    *used = header + compressed;
    *count = regenerated;
    return XXBYTE_STATUS_OK;
}

/* ------------------------------------------------------------------------ */
/* Sequences. */

static const uint32_t ll_base[36] = {0,  1,  2,  3,  4,  5,  6,  7,  8,   9,   10,  11,   12,   13,   14,   15,    16,    18,
                                     20, 22, 24, 28, 32, 40, 48, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
static const uint8_t ll_bits[36] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const uint32_t ml_base[53] = {3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19,  20,  21,  22,   23,   24,   25,   26,    27,    28,   29,
                                     30, 31, 32, 33, 34, 35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 131, 259, 515, 1027, 2051, 4099, 8195, 16387, 32771, 65539};
static const uint8_t ml_bits[53] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0, 0,
                                    0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const int16_t ll_default[36] = {4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
static const int16_t ml_default[53] = {1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1, 1,
                                       1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
static const int16_t of_default[29] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};

static xxbyte_status read_table(fse_table *t, int *have_table, uint32_t mode, const uint8_t *data, size_t size, size_t *used, const int16_t *defaults,
                                uint32_t default_count, uint32_t default_log, uint32_t max_log, uint32_t max_symbol)
{
    *used = 0;
    switch (mode) {
        case 0: *have_table = 1; return fse_build(t, defaults, default_count, default_log);
        case 1:
            if (size < 1) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            if (data[0] > max_symbol) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            fse_rle(t, data[0]);
            *used = 1;
            *have_table = 1;
            return XXBYTE_STATUS_OK;
        case 2: {
            xxbyte_status status = fse_read(t, data, size, max_log, max_symbol, used);

            if (status == XXBYTE_STATUS_OK) {
                *have_table = 1;
            }
            return status;
        }
        default: return *have_table ? XXBYTE_STATUS_OK : XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
}

static xxbyte_status execute(zstd_state *z, const uint8_t *literals, size_t literal_count, size_t *literal_pos, uint32_t ll, uint32_t ml, uint32_t offset)
{
    size_t i;

    if (ll > literal_count - *literal_pos || ll > z->out_size - z->produced) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    memcpy(z->out + z->produced, literals + *literal_pos, ll);
    *literal_pos += ll;
    z->produced += ll;
    if (offset == 0 || offset > z->produced || ml > z->out_size - z->produced) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    for (i = 0; i < ml; ++i) {
        z->out[z->produced] = z->out[z->produced - offset];
        ++z->produced;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status read_block(zstd_state *z, const uint8_t *data, size_t size)
{
    size_t used;
    size_t literal_count;
    size_t literal_pos = 0;
    size_t p;
    uint32_t sequences;
    xxbyte_status status;

    status = read_literals(z, data, size, &used, &literal_count);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    p = used;
    if (p >= size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (data[p] < 128u) {
        sequences = data[p];
        p += 1;
    } else if (data[p] < 255u) {
        if (size - p < 2) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        sequences = ((uint32_t)(data[p] - 128u) << 8) + data[p + 1];
        p += 2;
    } else {
        if (size - p < 3) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        sequences = (uint32_t)data[p + 1] + ((uint32_t)data[p + 2] << 8) + 0x7F00u;
        p += 3;
    }
    if (sequences == 0) {
        if (p != size || literal_count > z->out_size - z->produced) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        memcpy(z->out + z->produced, z->literals, literal_count);
        z->produced += literal_count;
        return XXBYTE_STATUS_OK;
    }
    if (p >= size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    {
        uint8_t modes = data[p++];
        bits_in b;
        uint32_t ls;
        uint32_t os;
        uint32_t ms;
        uint32_t n;

        if ((modes & 3u) != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        status = read_table(&z->ll, &z->have_ll, modes >> 6, data + p, size - p, &used, ll_default, 36, 6, LL_MAX_LOG, 35);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        p += used;
        status = read_table(&z->of, &z->have_of, (modes >> 4) & 3u, data + p, size - p, &used, of_default, 29, 5, OF_MAX_LOG, 31);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        p += used;
        status = read_table(&z->ml, &z->have_ml, (modes >> 2) & 3u, data + p, size - p, &used, ml_default, 53, 6, ML_MAX_LOG, 52);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        p += used;
        if (bits_init(&b, data + p, size - p) != XXBYTE_STATUS_OK) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        ls = bits_read(&b, z->ll.log);
        os = bits_read(&b, z->of.log);
        ms = bits_read(&b, z->ml.log);
        for (n = 0; n < sequences; ++n) {
            uint32_t of_code = z->of.cells[os].symbol;
            uint32_t ml_code = z->ml.cells[ms].symbol;
            uint32_t ll_code = z->ll.cells[ls].symbol;
            uint32_t offset_value;
            uint32_t offset;
            uint32_t ml;
            uint32_t ll;

            if (of_code > 31u || ml_code > 52u || ll_code > 35u) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            offset_value = (uint32_t)((UINT64_C(1) << of_code) + bits_read(&b, of_code));
            ml = ml_base[ml_code] + bits_read(&b, ml_bits[ml_code]);
            ll = ll_base[ll_code] + bits_read(&b, ll_bits[ll_code]);
            if (offset_value > 3u) {
                offset = offset_value - 3u;
                z->repeat[2] = z->repeat[1];
                z->repeat[1] = z->repeat[0];
                z->repeat[0] = offset;
            } else {
                uint32_t index = offset_value - 1u + (ll == 0 ? 1u : 0u);

                if (index == 0) {
                    offset = z->repeat[0];
                } else {
                    offset = index == 3 ? z->repeat[0] - 1u : z->repeat[index];
                    if (index != 1) {
                        z->repeat[2] = z->repeat[1];
                    }
                    z->repeat[1] = z->repeat[0];
                    z->repeat[0] = offset;
                }
            }
            status = execute(z, z->literals, literal_count, &literal_pos, ll, ml, offset);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (n + 1u < sequences) {
                ls = z->ll.cells[ls].base + bits_read(&b, z->ll.cells[ls].bits);
                ms = z->ml.cells[ms].base + bits_read(&b, z->ml.cells[ms].bits);
                os = z->of.cells[os].base + bits_read(&b, z->of.cells[os].bits);
            }
            if (b.overflow) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
        }
        if (b.position != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    if (literal_count - literal_pos > z->out_size - z->produced) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    memcpy(z->out + z->produced, z->literals + literal_pos, literal_count - literal_pos);
    z->produced += literal_count - literal_pos;
    return XXBYTE_STATUS_OK;
}

/* ------------------------------------------------------------------------ */
/* XXH64 (the content checksum is its low 32 bits). */

#define P1 UINT64_C(11400714785074694791)
#define P2 UINT64_C(14029467366897019727)
#define P3 UINT64_C(1609587929392839161)
#define P4 UINT64_C(9650029242287828579)
#define P5 UINT64_C(2870177450012600261)

static uint64_t rotl(uint64_t v, unsigned r)
{
    return (v << r) | (v >> (64u - r));
}

static uint64_t xxh_round(uint64_t acc, uint64_t lane)
{
    return rotl(acc + lane * P2, 31) * P1;
}

static uint64_t xxh_merge(uint64_t acc, uint64_t v)
{
    return (acc ^ xxh_round(0, v)) * P1 + P4;
}

static uint64_t xxh64(const uint8_t *p, size_t size)
{
    const uint8_t *end = p + size;
    uint64_t h;

    if (size >= 32) {
        uint64_t v1 = P1 + P2;
        uint64_t v2 = P2;
        uint64_t v3 = 0;
        uint64_t v4 = (uint64_t)0 - P1;

        while ((size_t)(end - p) >= 32) {
            v1 = xxh_round(v1, xxbyte_u64le(p));
            v2 = xxh_round(v2, xxbyte_u64le(p + 8));
            v3 = xxh_round(v3, xxbyte_u64le(p + 16));
            v4 = xxh_round(v4, xxbyte_u64le(p + 24));
            p += 32;
        }
        h = rotl(v1, 1) + rotl(v2, 7) + rotl(v3, 12) + rotl(v4, 18);
        h = xxh_merge(h, v1);
        h = xxh_merge(h, v2);
        h = xxh_merge(h, v3);
        h = xxh_merge(h, v4);
    } else {
        h = P5;
    }
    h += (uint64_t)size;
    while ((size_t)(end - p) >= 8) {
        h ^= xxh_round(0, xxbyte_u64le(p));
        h = rotl(h, 27) * P1 + P4;
        p += 8;
    }
    if ((size_t)(end - p) >= 4) {
        h ^= (uint64_t)xxbyte_u32le(p) * P1;
        h = rotl(h, 23) * P2 + P3;
        p += 4;
    }
    while (p < end) {
        h ^= (uint64_t)*p * P5;
        h = rotl(h, 11) * P1;
        ++p;
    }
    h ^= h >> 33;
    h *= P2;
    h ^= h >> 29;
    h *= P3;
    h ^= h >> 32;
    return h;
}

/* ------------------------------------------------------------------------ */
/* Frame. */

xxbyte_status xxbyte_gdscript_zstd(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size)
{
    zstd_state *z;
    size_t p;
    uint8_t descriptor;
    uint32_t fcs_flag;
    int single;
    int checksum;
    uint32_t dict_flag;
    uint64_t content_size = 0;
    int have_size;
    xxbyte_status status = XXBYTE_STATUS_OK;
    static const uint8_t dict_bytes[4] = {0, 1, 2, 4};
    static const uint8_t fcs_bytes[4] = {0, 2, 4, 8};

    if (in == NULL || (out == NULL && out_size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (in_size < 5) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (xxbyte_u32le(in) != ZSTD_MAGIC) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    descriptor = in[4];
    fcs_flag = descriptor >> 6;
    single = (descriptor >> 5) & 1;
    checksum = (descriptor >> 2) & 1;
    dict_flag = descriptor & 3u;
    if ((descriptor & 8u) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    p = 5;
    if (!single) {
        p += 1; /* window descriptor: the whole output is in memory */
    }
    if (in_size - 5 < (p - 5) + dict_bytes[dict_flag]) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    {
        uint32_t dict = 0;
        uint32_t k;

        for (k = 0; k < dict_bytes[dict_flag]; ++k) {
            dict |= (uint32_t)in[p + k] << (8 * k);
        }
        if (dict != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        p += dict_bytes[dict_flag];
    }
    {
        uint32_t bytes = fcs_flag == 0 ? (single ? 1u : 0u) : fcs_bytes[fcs_flag];
        uint32_t k;

        if (in_size - p < bytes) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        for (k = 0; k < bytes; ++k) {
            content_size |= (uint64_t)in[p + k] << (8 * k);
        }
        if (bytes == 2) {
            content_size += 256u;
        }
        have_size = bytes != 0;
        p += bytes;
    }
    if (have_size && content_size != out_size) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    z = (zstd_state *)xx_mem_calloc(1, sizeof(*z));
    if (z == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    z->in = in;
    z->in_size = in_size;
    z->out = out;
    z->out_size = out_size;
    z->repeat[0] = 1;
    z->repeat[1] = 4;
    z->repeat[2] = 8;
    for (;;) {
        uint32_t header;
        uint32_t last;
        uint32_t type;
        uint32_t size;

        if (in_size - p < 3) {
            status = XXBYTE_STATUS_TRUNCATED;
            break;
        }
        header = (uint32_t)in[p] | ((uint32_t)in[p + 1] << 8) | ((uint32_t)in[p + 2] << 16);
        p += 3;
        last = header & 1u;
        type = (header >> 1) & 3u;
        size = header >> 3;
        if (type == 3) {
            status = XXBYTE_STATUS_INVALID_INSTRUCTION;
            break;
        }
        if (type == 1) {
            if (in_size - p < 1) {
                status = XXBYTE_STATUS_TRUNCATED;
                break;
            }
            if (size > BLOCK_MAX || size > out_size - z->produced) {
                status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                break;
            }
            memset(out + z->produced, in[p], size);
            z->produced += size;
            p += 1;
        } else {
            if (size > BLOCK_MAX) {
                status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                break;
            }
            if (in_size - p < size) {
                status = XXBYTE_STATUS_TRUNCATED;
                break;
            }
            if (type == 0) {
                if (size > out_size - z->produced) {
                    status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                    break;
                }
                memcpy(out + z->produced, in + p, size);
                z->produced += size;
            } else {
                status = read_block(z, in + p, size);
                if (status != XXBYTE_STATUS_OK) {
                    break;
                }
            }
            p += size;
        }
        if (last) {
            break;
        }
    }
    if (status == XXBYTE_STATUS_OK && z->produced != out_size) {
        status = XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (status == XXBYTE_STATUS_OK && checksum) {
        if (in_size - p < 4) {
            status = XXBYTE_STATUS_TRUNCATED;
        } else if ((uint32_t)xxh64(out, out_size) != xxbyte_u32le(in + p)) {
            status = XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    xx_mem_free(z);
    return status;
}

#else
typedef int xxbyte_gdscript_zstd_disabled;
#endif
