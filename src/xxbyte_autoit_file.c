#include "xxbyte_autoit_internal.h"
#include "xxbyte/xxbyte_xxfc.h"

#if XXBYTE_USE_AUTOIT

#include <xxfclib/memory/xx_memory.h>

/* ---- ciphers ---- */

#define MT_N 624u
#define MT_M 397u

typedef struct key_stream {
    uint32_t cipher;
    uint32_t index;
    uint32_t c0;
    uint32_t c1;
    uint32_t state[MT_N];
} key_stream;

static uint32_t rol32(uint32_t x, unsigned n)
{
    return (x << n) | (x >> (32u - n));
}

static uint32_t lame_step(key_stream *k)
{
    uint32_t rolled = rol32(k->state[k->c0], 9) + rol32(k->state[k->c1], 13);

    k->state[k->c0] = rolled;
    k->c0 = k->c0 == 0 ? 16u : k->c0 - 1u;
    k->c1 = k->c1 == 0 ? 16u : k->c1 - 1u;
    return rolled;
}

static void mt_twist(uint32_t *s)
{
    uint32_t i;

    for (i = 0; i < MT_N; ++i) {
        uint32_t y = (s[i] & 0x80000000u) | (s[(i + 1u) % MT_N] & 0x7FFFFFFFu);
        uint32_t v = s[(i + MT_M) % MT_N] ^ (y >> 1);
        if ((y & 1u) != 0) {
            v ^= 0x9908B0DFu;
        }
        s[i] = v;
    }
}

static void key_init(key_stream *k, uint32_t cipher, uint32_t seed)
{
    uint32_t i;

    k->cipher = cipher;
    k->index = 0;
    if (cipher == XXBYTE_AUTOIT_CIPHER_MT) {
        k->state[0] = seed;
        for (i = 1; i < MT_N; ++i) {
            uint32_t last = k->state[i - 1];
            k->state[i] = i + 0x6C078965u * (last ^ (last >> 30));
        }
        return;
    }
    for (i = 0; i < 17; ++i) {
        seed = 1u - seed * 0x53A9B4FBu;
        k->state[i] = seed;
    }
    k->c0 = 0;
    k->c1 = 10;
    for (i = 0; i < 9; ++i) {
        (void)lame_step(k);
    }
}

static uint8_t key_next(key_stream *k)
{
    uint32_t y;

    if (k->cipher != XXBYTE_AUTOIT_CIPHER_MT) {
        (void)lame_step(k);
        return (uint8_t)(lame_step(k) >> 24);
    }
    if (k->index == 0) {
        mt_twist(k->state);
    }
    y = k->state[k->index];
    k->index = k->index + 1u == MT_N ? 0u : k->index + 1u;
    y ^= y >> 11;
    y ^= (y << 7) & 0x9D2C5680u;
    y ^= (y << 15) & 0xEFC60000u;
    y ^= y >> 18;
    return (uint8_t)(y >> 1);
}

xxbyte_status xxbyte_autoit_crypt(uint32_t cipher, uint32_t seed, uint8_t *data, size_t size)
{
    key_stream k;
    size_t i;

    if ((data == NULL && size != 0) || cipher > XXBYTE_AUTOIT_CIPHER_LAME) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    key_init(&k, cipher, seed);
    for (i = 0; i < size; ++i) {
        data[i] ^= key_next(&k);
    }
    return XXBYTE_STATUS_OK;
}

uint32_t xxbyte_autoit_adler32(const uint8_t *data, size_t size)
{
    uint32_t a = 1;
    uint32_t b = 0;
    size_t i = 0;

    if (data == NULL) {
        return 1;
    }
    while (i < size) {
        size_t chunk = size - i < 5552u ? size - i : 5552u;
        size_t end = i + chunk;
        for (; i < end; ++i) {
            a += data[i];
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
    }
    return (b << 16) | a;
}

/* ---- decompression ---- */

typedef struct bit_reader {
    const uint8_t *data;
    uint64_t bits;
    uint64_t pos;
} bit_reader;

static int get_bits(bit_reader *r, unsigned n, uint32_t *value)
{
    uint32_t v = 0;
    unsigned i;

    if (r->bits - r->pos < n) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        uint64_t p = r->pos++;
        v = (v << 1) | (((uint32_t)r->data[p >> 3] >> (7u - (unsigned)(p & 7u))) & 1u);
    }
    *value = v;
    return 1;
}

xxbyte_status xxbyte_autoit_unpacked_size(const uint8_t *in, size_t in_size, uint32_t *method, uint32_t *size)
{
    static const char sigs[4][5] = {"EA05", "EA06", "JB00", "JB01"};
    uint32_t m;

    if (in == NULL && in_size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (in_size < 8) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    for (m = 0; m < 4; ++m) {
        if (memcmp(in, sigs[m], 4) == 0) {
            if (method != NULL) {
                *method = m;
            }
            if (size != NULL) {
                *size = xxbyte_u32be(in + 4);
            }
            return XXBYTE_STATUS_OK;
        }
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

static int read_length(bit_reader *r, uint64_t *length)
{
    static const uint32_t base[5] = {3, 6, 13, 44, 299};
    static const unsigned width[5] = {2, 3, 5, 8, 8};
    uint32_t v;
    uint64_t total;
    unsigned i;

    for (i = 0; i < 5; ++i) {
        uint32_t all = (1u << width[i]) - 1u;
        if (!get_bits(r, width[i], &v)) {
            return 0;
        }
        if (v != all) {
            *length = base[i] + v;
            return 1;
        }
    }
    total = 299;
    for (;;) {
        total += 255;
        if (!get_bits(r, 8, &v)) {
            return 0;
        }
        if (v != 255) {
            *length = total + v;
            return 1;
        }
    }
}

xxbyte_status xxbyte_autoit_decompress(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_capacity, size_t *out_size)
{
    bit_reader r;
    uint32_t method;
    uint32_t size;
    uint32_t literal;
    size_t n = 0;
    xxbyte_status status;

    if (out_size == NULL || (out == NULL && out_capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *out_size = 0;
    status = xxbyte_autoit_unpacked_size(in, in_size, &method, &size);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (method == XXBYTE_AUTOIT_PACK_JB01) {
        return XXBYTE_STATUS_UNSUPPORTED_FORMAT;
    }
    if (size > out_capacity) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    r.data = in + 8;
    r.bits = (uint64_t)(in_size - 8) * 8u;
    r.pos = 0;
    literal = method == XXBYTE_AUTOIT_PACK_EA06 ? 1u : 0u;
    while (n < size) {
        uint32_t flag;
        uint32_t v;
        uint64_t distance;
        uint64_t length;
        size_t i;

        if (!get_bits(&r, 1, &flag)) {
            *out_size = n;
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (flag == literal) {
            if (!get_bits(&r, 8, &v)) {
                *out_size = n;
                return XXBYTE_STATUS_TRUNCATED;
            }
            out[n++] = (uint8_t)v;
            continue;
        }
        if (method == XXBYTE_AUTOIT_PACK_JB00) {
            uint32_t len;
            if (!get_bits(&r, 13, &v) || !get_bits(&r, 4, &len)) {
                *out_size = n;
                return XXBYTE_STATUS_TRUNCATED;
            }
            distance = (uint64_t)v + 3u;
            length = (uint64_t)len + 3u;
        } else {
            if (!get_bits(&r, 15, &v) || !read_length(&r, &length)) {
                *out_size = n;
                return XXBYTE_STATUS_TRUNCATED;
            }
            distance = v;
        }
        if (distance == 0 || distance > n) {
            *out_size = n;
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (length > size - n) {
            length = size - n;
        }
        for (i = 0; i < length; ++i, ++n) {
            out[n] = out[n - distance];
        }
    }
    *out_size = n;
    return XXBYTE_STATUS_OK;
}

/* ---- archives ---- */

static const uint8_t archive_signature[20] = {0xA3, 0x48, 0x4B, 0xBE, 0x98, 0x6C, 0x4A, 0xA9, 0x99, 0x4C, 0x53, 0x0A, 0x86, 0xD6, 0x48, 0x7D, 'A', 'U', '3', '!'};

typedef struct version_keys {
    uint32_t cipher;
    uint32_t unicode;
    uint32_t file;
    uint32_t type_xor;
    uint32_t type_seed;
    uint32_t name_xor;
    uint32_t name_seed;
    uint32_t size_xor;
    uint32_t adler_xor;
    uint32_t data_seed;
} version_keys;

static const version_keys ea05_keys = {XXBYTE_AUTOIT_CIPHER_MT, 0, 0x16FA, 0x29BC, 0xA25E, 0x29AC, 0xF25E, 0x45AA, 0xC3D2, 0x22AF};
static const version_keys ea06_keys = {XXBYTE_AUTOIT_CIPHER_LAME, 1, 0x18EE, 0xADBC, 0xB33F, 0xF820, 0xF479, 0x87BC, 0xA685, 0x2477};

static const version_keys *keys_of(uint32_t version)
{
    return version == XXBYTE_AUTOIT_EA05 ? &ea05_keys : version == XXBYTE_AUTOIT_EA06 ? &ea06_keys : NULL;
}

static int file_magic_at(const uint8_t *data, size_t size, uint64_t offset, const version_keys *keys)
{
    uint8_t magic[4];

    if (offset > size || size - offset < 4) {
        return 0;
    }
    memcpy(magic, data + offset, 4);
    (void)xxbyte_autoit_crypt(keys->cipher, keys->file, magic, 4);
    return memcmp(magic, "FILE", 4) == 0;
}

xxbyte_status xxbyte_autoit_find_archive(const uint8_t *data, size_t size, uint64_t start, xxbyte_autoit_archive *archive)
{
    uint64_t pos;

    if (archive == NULL || (data == NULL && size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(archive, 0, sizeof(*archive));
    for (pos = start; size >= 44 && pos <= size - 44; ++pos) {
        const uint8_t *p = data + pos;
        const version_keys *keys;
        uint32_t version;
        uint32_t sum = 0;
        unsigned i;

        if (p[0] != 0xA3 || memcmp(p, archive_signature, sizeof(archive_signature)) != 0) {
            continue;
        }
        if (memcmp(p + 20, "EA05", 4) == 0) {
            version = XXBYTE_AUTOIT_EA05;
        } else if (memcmp(p + 20, "EA06", 4) == 0) {
            version = XXBYTE_AUTOIT_EA06;
        } else {
            continue;
        }
        keys = keys_of(version);
        if (!file_magic_at(data, size, pos + 40, keys)) {
            continue;
        }
        for (i = 0; i < 16; ++i) {
            sum += p[24 + i];
        }
        archive->offset = pos;
        archive->entries_offset = pos + 40;
        archive->version = version;
        archive->checksum = sum;
        return XXBYTE_STATUS_OK;
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

/* Text sink of bounded size: appends while the whole piece fits. */
typedef struct name_sink {
    char *text;
    size_t length;
    int full;
} name_sink;

static void sink_put(name_sink *s, const char *piece, size_t n)
{
    if (s->full || s->length + n >= XXBYTE_AUTOIT_NAME_SIZE) {
        s->full = 1;
        return;
    }
    memcpy(s->text + s->length, piece, n);
    s->length += n;
    s->text[s->length] = '\0';
}

static size_t utf8_encode(uint32_t cp, char *b)
{
    if (cp < 0x80) {
        b[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    b[0] = (char)(0xF0 | (cp >> 18));
    b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    b[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Hex escape of a byte or unit: \xNN or \uNNNN. */
static size_t escape_code(uint32_t value, int wide, char *b)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    int shift;

    b[n++] = '\\';
    b[n++] = wide ? 'u' : 'x';
    for (shift = wide ? 12 : 4; shift >= 0; shift -= 4) {
        b[n++] = hex[(value >> (unsigned)shift) & 0xFu];
    }
    return n;
}

/* Converts text decoded so far: UTF-16 units (wide) or bytes. A pending
 * high surrogate waits in *pending. */
static void sink_unit(name_sink *s, uint32_t unit, int wide, uint32_t *pending)
{
    char b[8];
    size_t n;

    if (!wide) {
        if (unit >= 0x20 && unit < 0x7F) {
            b[0] = (char)unit;
            n = 1;
        } else {
            n = escape_code(unit, 0, b);
        }
        sink_put(s, b, n);
        return;
    }
    if (*pending != 0) {
        uint32_t high = *pending;
        *pending = 0;
        if (unit >= 0xDC00 && unit <= 0xDFFF) {
            n = utf8_encode(0x10000u + ((high - 0xD800u) << 10) + (unit - 0xDC00u), b);
            sink_put(s, b, n);
            return;
        }
        n = escape_code(high, 1, b);
        sink_put(s, b, n);
    }
    if (unit >= 0xD800 && unit <= 0xDBFF) {
        *pending = unit;
        return;
    }
    if (unit < 0x20 || unit == 0x7F || (unit >= 0xDC00 && unit <= 0xDFFF)) {
        n = escape_code(unit, 1, b);
    } else {
        n = utf8_encode(unit, b);
    }
    sink_put(s, b, n);
}

static const char *const entry_types[] = {">>>AUTOIT SCRIPT<<<", ">AUTOIT SCRIPT<", ">AUTOIT UNICODE SCRIPT<", ">>>AUTOIT NO CMDEXECUTE<<<"};
static const uint32_t entry_kinds[] = {XXBYTE_AUTOIT_ENTRY_SCRIPT, XXBYTE_AUTOIT_ENTRY_TEXT, XXBYTE_AUTOIT_ENTRY_UNICODE_TEXT, XXBYTE_AUTOIT_ENTRY_NO_EXECUTE};

/* Decrypts a string field of count characters at *pos into text; returns
 * the index of the entry type it equals, -1 for none, or -2 when it runs
 * past size. */
static int read_text(const uint8_t *data, size_t size, uint64_t *pos, uint32_t count, uint32_t seed, const version_keys *keys, char *text)
{
    uint64_t bytes = keys->unicode ? 2u * (uint64_t)count : count;
    key_stream k;
    name_sink sink;
    uint32_t pending = 0;
    uint32_t matches = 0xFu;
    uint64_t i;
    unsigned t;

    if (*pos > size || bytes > size - *pos) {
        return -2;
    }
    sink.text = text;
    sink.length = 0;
    sink.full = 0;
    text[0] = '\0';
    for (t = 0; t < 4; ++t) {
        if (strlen(entry_types[t]) != count) {
            matches &= ~(1u << t);
        }
    }
    key_init(&k, keys->cipher, seed);
    for (i = 0; i < count; ++i) {
        uint32_t unit;
        if (keys->unicode) {
            uint8_t lo = (uint8_t)(data[*pos + 2u * i] ^ key_next(&k));
            uint8_t hi = (uint8_t)(data[*pos + 2u * i + 1u] ^ key_next(&k));
            unit = (uint32_t)lo | ((uint32_t)hi << 8);
        } else {
            unit = (uint8_t)(data[*pos + i] ^ key_next(&k));
        }
        for (t = 0; t < 4; ++t) {
            if ((matches & (1u << t)) != 0 && (unsigned char)entry_types[t][i] != unit) {
                matches &= ~(1u << t);
            }
        }
        if (!sink.full) {
            sink_unit(&sink, unit, (int)keys->unicode, &pending);
        }
    }
    if (pending != 0) {
        char b[8];
        size_t n = escape_code(pending, 1, b);
        sink_put(&sink, b, n);
    }
    *pos += bytes;
    for (t = 0; t < 4; ++t) {
        if ((matches & (1u << t)) != 0) {
            return (int)t;
        }
    }
    return -1;
}

static int read_u32(const uint8_t *data, size_t size, uint64_t *pos, uint32_t *value)
{
    if (*pos > size || size - *pos < 4) {
        return 0;
    }
    *value = xxbyte_u32le(data + *pos);
    *pos += 4;
    return 1;
}

xxbyte_status xxbyte_autoit_read_entry(const uint8_t *data, size_t size, const xxbyte_autoit_archive *archive, uint64_t offset, xxbyte_autoit_entry *entry)
{
    const version_keys *keys;
    uint64_t pos = offset;
    uint32_t v;
    uint32_t hi;
    uint32_t lo;
    int type;

    if (entry == NULL || archive == NULL || (data == NULL && size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(entry, 0, sizeof(*entry));
    keys = keys_of(archive->version);
    if (keys == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (!file_magic_at(data, size, offset, keys)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    pos += 4;
    entry->offset = offset;
    entry->cipher = keys->cipher;
    if (!read_u32(data, size, &pos, &v)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    entry->type_length = v ^ keys->type_xor;
    type = read_text(data, size, &pos, entry->type_length, entry->type_length + keys->type_seed, keys, entry->type);
    if (type == -2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    entry->kind = type >= 0 ? entry_kinds[type] : XXBYTE_AUTOIT_ENTRY_FILE;
    if (!read_u32(data, size, &pos, &v)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    entry->name_length = v ^ keys->name_xor;
    if (read_text(data, size, &pos, entry->name_length, entry->name_length + keys->name_seed, keys, entry->name) == -2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (pos >= size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    entry->compressed = data[pos++];
    if (!read_u32(data, size, &pos, &v)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    entry->packed_size = v ^ keys->size_xor;
    if (!read_u32(data, size, &pos, &v)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    entry->size = v ^ keys->size_xor;
    if (!read_u32(data, size, &pos, &v)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    entry->adler32 = v ^ keys->adler_xor;
    if (!read_u32(data, size, &pos, &hi) || !read_u32(data, size, &pos, &lo)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    entry->created = ((uint64_t)hi << 32) | lo;
    if (!read_u32(data, size, &pos, &hi) || !read_u32(data, size, &pos, &lo)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    entry->modified = ((uint64_t)hi << 32) | lo;
    entry->data_offset = pos;
    entry->seed = keys->data_seed + (archive->version == XXBYTE_AUTOIT_EA05 ? archive->checksum : 0u);
    if (entry->packed_size > size - pos) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    entry->next = pos + entry->packed_size;
    return XXBYTE_STATUS_OK;
}

void xxbyte_autoit_free(void *pointer)
{
    if (pointer != NULL) {
        xx_mem_free(pointer);
    }
}

/* Unpacks a compressed blob into a new allocation. */
static xxbyte_status unpack(const uint8_t *in, size_t in_size, uint8_t **out, size_t *out_size)
{
    uint32_t size;
    uint32_t method;
    xxbyte_status status = xxbyte_autoit_unpacked_size(in, in_size, &method, &size);
    uint8_t *buffer;

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (method == XXBYTE_AUTOIT_PACK_JB01) {
        return XXBYTE_STATUS_UNSUPPORTED_FORMAT;
    }
    if (size > XXBYTE_AUTOIT_DATA_LIMIT || (uint64_t)size > (uint64_t)in_size * 256u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    buffer = (uint8_t *)xx_mem_alloc(size != 0 ? size : 1u);
    if (buffer == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_autoit_decompress(in, in_size, buffer, size, out_size);
    if (status != XXBYTE_STATUS_OK) {
        xx_mem_free(buffer);
        *out_size = 0;
        return status;
    }
    *out = buffer;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_autoit_entry_data(const uint8_t *data, size_t size, const xxbyte_autoit_entry *entry, uint8_t **out, size_t *out_size, int *adler32_ok)
{
    uint8_t *packed;
    xxbyte_status status;

    if (out == NULL || out_size == NULL || entry == NULL || (data == NULL && size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *out = NULL;
    *out_size = 0;
    if (adler32_ok != NULL) {
        *adler32_ok = 0;
    }
    if (entry->data_offset > size || entry->packed_size > size - entry->data_offset) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (entry->packed_size > XXBYTE_AUTOIT_DATA_LIMIT) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    packed = (uint8_t *)xx_mem_alloc(entry->packed_size != 0 ? entry->packed_size : 1u);
    if (packed == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (entry->packed_size != 0) {
        memcpy(packed, data + entry->data_offset, entry->packed_size);
    }
    (void)xxbyte_autoit_crypt(entry->cipher, entry->seed, packed, entry->packed_size);
    if (adler32_ok != NULL) {
        *adler32_ok = xxbyte_autoit_adler32(packed, entry->packed_size) == entry->adler32;
    }
    if (entry->compressed != 1) {
        *out = packed;
        *out_size = entry->packed_size;
        return XXBYTE_STATUS_OK;
    }
    status = unpack(packed, entry->packed_size, out, out_size);
    xx_mem_free(packed);
    return status;
}

/* ---- source reconstruction ---- */

typedef struct source_state {
    int indent;
    int next_indent;
} source_state;

/* The canonical keyword name of a keyword token, or NULL. */
static const char *keyword_of(const xxbyte_instruction *insn)
{
    if (insn->id == XXBYTE_AUTOIT_ID_KEYWORD_INDEX) {
        return xxbyte_autoit_keyword_name((uint32_t)insn->operands[0].value);
    }
    if (insn->id == XXBYTE_AUTOIT_ID_KEYWORD) {
        return xxbyte_autoit_lookup(xxbyte_autoit_keywords, XXBYTE_AUTOIT_KEYWORD_COUNT, insn->code + 5, (uint32_t)insn->operands[0].extra);
    }
    return NULL;
}

static int name_in(const char *name, const char *const *list)
{
    for (; *list != NULL; ++list) {
        if (strcmp(name, *list) == 0) {
            return 1;
        }
    }
    return 0;
}

/* AutoIt-Ripper's indentation rules. */
static void apply_indent(source_state *st, const char *keyword, int next_is_eol)
{
    static const char *const opens[] = {"While", "Do", "For", "Select", "Switch", "Func", "If", NULL};
    static const char *const middles[] = {"Case", "Else", "ElseIf", NULL};
    static const char *const closes[] = {"WEnd", "Until", "Next", "EndSelect", "EndSwitch", "EndFunc", "EndIf", NULL};

    if (name_in(keyword, opens)) {
        st->next_indent += 1;
    }
    if (name_in(keyword, middles)) {
        st->indent -= 1;
    }
    if (name_in(keyword, closes)) {
        st->next_indent -= 1;
        st->indent -= 1;
    }
    if (strcmp(keyword, "Then") == 0 && !next_is_eol) {
        st->next_indent -= 1;
    }
    if (strcmp(keyword, "EndFunc") == 0) {
        st->next_indent = 0;
    }
}

static int is_operator(const xxbyte_instruction *insn)
{
    return insn->id >= XXBYTE_AUTOIT_ID_COMMA && insn->id <= XXBYTE_AUTOIT_ID_COLON;
}

/* Whether a space goes between prev and cur in the compact layout. */
static int want_space(const xxbyte_instruction *prev, const xxbyte_instruction *cur, int prev_unary)
{
    uint32_t p = prev->id;
    uint32_t c = cur->id;

    if (prev_unary || p == XXBYTE_AUTOIT_ID_LEFT_PAREN || p == XXBYTE_AUTOIT_ID_LEFT_BRACKET) {
        return 0;
    }
    if (c == XXBYTE_AUTOIT_ID_COMMA || c == XXBYTE_AUTOIT_ID_RIGHT_PAREN || c == XXBYTE_AUTOIT_ID_RIGHT_BRACKET) {
        return 0;
    }
    if (c == XXBYTE_AUTOIT_ID_LEFT_PAREN) {
        return !(p == XXBYTE_AUTOIT_ID_FUNCTION_INDEX || p == XXBYTE_AUTOIT_ID_FUNCTION || p == XXBYTE_AUTOIT_ID_IDENTIFIER || p == XXBYTE_AUTOIT_ID_PROPERTY);
    }
    if (c == XXBYTE_AUTOIT_ID_LEFT_BRACKET || c == XXBYTE_AUTOIT_ID_PROPERTY) {
        return !(p == XXBYTE_AUTOIT_ID_VARIABLE || p == XXBYTE_AUTOIT_ID_MACRO || p == XXBYTE_AUTOIT_ID_PROPERTY || p == XXBYTE_AUTOIT_ID_RIGHT_PAREN ||
                 p == XXBYTE_AUTOIT_ID_RIGHT_BRACKET || p == XXBYTE_AUTOIT_ID_IDENTIFIER);
    }
    return 1;
}

/* Whether a "-" after prev is unary. */
static int unary_context(const xxbyte_instruction *prev)
{
    if (prev == NULL) {
        return 1;
    }
    if (prev->id == XXBYTE_AUTOIT_ID_RIGHT_PAREN || prev->id == XXBYTE_AUTOIT_ID_RIGHT_BRACKET) {
        return 0;
    }
    return is_operator(prev) || prev->id == XXBYTE_AUTOIT_ID_KEYWORD_INDEX || prev->id == XXBYTE_AUTOIT_ID_KEYWORD;
}

/*
 * Reconstructs the line at *pos (up to and including its eol) into line,
 * updating the indentation state. Returns OK with *pos after the eol, or
 * the failing decode status with *pos at the failing token.
 */
static xxbyte_status source_line(const uint8_t *tokens, size_t size, size_t *pos, uint32_t flags, source_state *st, xx_buf_t *line)
{
    xxbyte_instruction prev;
    int have_prev = 0;
    int prev_unary = 0;
    xxbyte_fmt fmt;

    fmt.out = line;
    fmt.flags = 0;
    fmt.resolver = NULL;
    fmt.context = NULL;
    for (;;) {
        xxbyte_instruction insn;
        xxbyte_status status;
        const char *keyword;
        int unary = 0;

        if (*pos >= size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        status = xxbyte_decode(XXBYTE_FAMILY_AUTOIT, XXBYTE_AUTOIT_MODE_V3, tokens + *pos, size - *pos, *pos, &insn);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        *pos += insn.size;
        if (insn.id == XXBYTE_AUTOIT_ID_EOL) {
            return xx_buf_ok(line) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        keyword = keyword_of(&insn);
        if (keyword != NULL) {
            apply_indent(st, keyword, *pos < size && tokens[*pos] == XXBYTE_AUTOIT_TOKEN_EOL);
        }
        if (have_prev) {
            int space = (flags & XXBYTE_AUTOIT_SOURCE_SPACED) != 0 ? 1 : want_space(&prev, &insn, prev_unary);
            if (space && !xx_buf_append_char(line, ' ')) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
        if (insn.id == XXBYTE_AUTOIT_ID_SUBTRACT && unary_context(have_prev ? &prev : NULL)) {
            unary = 1;
        }
        if (!xxbyte_autoit_token_source(&insn, &fmt)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        prev = insn;
        have_prev = 1;
        prev_unary = unary;
    }
}

static int append_indent(xx_buf_t *out, int indent)
{
    int i;

    for (i = 0; i < indent; ++i) {
        if (!xx_buf_append_char(out, '\t')) {
            return 0;
        }
    }
    return 1;
}

xxbyte_status xxbyte_autoit_source(const uint8_t *tokens, size_t size, uint32_t flags, struct xx_buf_s *out, uint32_t *line_count)
{
    source_state st = {0, 0};
    xx_buf_t line;
    size_t pos = 4;
    uint32_t lines;
    uint32_t n;
    xxbyte_status status = XXBYTE_STATUS_OK;

    if (line_count != NULL) {
        *line_count = 0;
    }
    if (out == NULL || (tokens == NULL && size != 0) || (flags & ~(uint32_t)(XXBYTE_AUTOIT_SOURCE_SPACED | XXBYTE_AUTOIT_SOURCE_NO_INDENT)) != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    lines = xxbyte_u32le(tokens);
    if (line_count != NULL) {
        *line_count = lines;
    }
    xx_buf_init(&line);
    for (n = 0; n < lines; ++n) {
        int indent;
        xx_buf_clear(&line);
        status = source_line(tokens, size, &pos, flags, &st, &line);
        if (status != XXBYTE_STATUS_OK) {
            break;
        }
        indent = (flags & XXBYTE_AUTOIT_SOURCE_NO_INDENT) != 0 ? 0 : st.indent;
        if ((line.size != 0 || (flags & XXBYTE_AUTOIT_SOURCE_SPACED) != 0) && !append_indent(out, indent)) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        if ((line.size != 0 && !xx_buf_append(out, line.data, line.size)) || !xx_buf_append_str(out, "\r\n")) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        st.indent = st.next_indent;
    }
    xx_buf_free(&line);
    if (status == XXBYTE_STATUS_OK && !xx_buf_ok(out)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return status;
}

/* ---- listing ---- */

static xxbyte_status list_range(const uint8_t *tokens, size_t from, size_t to, uint32_t listing_flags, xx_buf_t *out)
{
    return xxbyte_disasm_buf_ex(XXBYTE_FAMILY_AUTOIT, XXBYTE_AUTOIT_MODE_V3, NULL, tokens + from, to - from, from, listing_flags, NULL, NULL, out);
}

static xxbyte_status list_tokens(const uint8_t *tokens, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    source_state st = {0, 0};
    xx_buf_t line;
    size_t pos = 4;
    uint32_t lines;
    uint32_t n;
    xxbyte_status status = XXBYTE_STATUS_OK;

    if (size < 4) {
        return xx_buf_append_str(out, "; script: truncated line count\n") ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    lines = xxbyte_u32le(tokens);
    if (!xx_buf_appendf(out, "; script: %u lines\n", (unsigned)lines)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    xx_buf_init(&line);
    for (n = 0; n < lines && status == XXBYTE_STATUS_OK; ++n) {
        size_t start = pos;
        xx_buf_clear(&line);
        status = source_line(tokens, size, &pos, XXBYTE_AUTOIT_SOURCE_DEFAULT, &st, &line);
        if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
            break;
        }
        if (status != XXBYTE_STATUS_OK) {
            const char *why = status == XXBYTE_STATUS_TRUNCATED ? "truncated" : "invalid token";
            if (!xx_buf_appendf(out, "; %u: %s%s(%s)\n", (unsigned)(n + 1), line.size != 0 ? line.data : "", line.size != 0 ? " " : "", why)) {
                status = XXBYTE_STATUS_OUT_OF_MEMORY;
                break;
            }
            status = list_range(tokens, start, size, listing_flags, out);
            pos = size;
            break;
        }
        if (!xx_buf_appendf(out, "; %u:%s", (unsigned)(n + 1), line.size != 0 ? " " : "") || (line.size != 0 && !xx_buf_append(out, line.data, line.size)) ||
            !xx_buf_append_char(out, '\n')) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        status = list_range(tokens, start, pos, listing_flags, out);
        st.indent = st.next_indent;
    }
    xx_buf_free(&line);
    if (status == XXBYTE_STATUS_OK && pos < size) {
        if (!xx_buf_append_str(out, "; trailing tokens\n")) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        status = list_range(tokens, pos, size, listing_flags, out);
    }
    return status;
}

/* Source text (ANSI bytes or UTF-16LE) as "; n: text" lines. */
static xxbyte_status list_text(const uint8_t *text, size_t size, int wide, xx_buf_t *out)
{
    size_t count = wide ? size / 2u : size;
    size_t i = 0;
    uint32_t n = 0;
    char piece[16];
    xx_buf_t line;
    xxbyte_status status = XXBYTE_STATUS_OK;

    if (wide && count != 0 && xxbyte_u16le(text) == 0xFEFF) {
        i = 1;
    }
    xx_buf_init(&line);
    while (i < count && status == XXBYTE_STATUS_OK) {
        uint32_t pending = 0;
        xx_buf_clear(&line);
        for (; i < count; ++i) {
            uint32_t u = wide ? xxbyte_u16le(text + 2u * i) : text[i];
            name_sink sink;
            if (u == '\n') {
                ++i;
                break;
            }
            if (u == '\r') {
                continue;
            }
            sink.text = piece;
            sink.length = 0;
            sink.full = 0;
            if (u == '\t') {
                sink_put(&sink, "\t", 1);
            } else {
                sink_unit(&sink, u, wide, &pending);
            }
            if (sink.length != 0 && !xx_buf_append(&line, piece, sink.length)) {
                break;
            }
        }
        if (pending != 0) {
            size_t len = escape_code(pending, 1, piece);
            (void)xx_buf_append(&line, piece, len);
        }
        if (!xx_buf_ok(&line) || !xx_buf_appendf(out, "; %u:%s", (unsigned)++n, line.size != 0 ? " " : "") ||
            (line.size != 0 && !xx_buf_append(out, line.data, line.size)) || !xx_buf_append_char(out, '\n')) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    xx_buf_free(&line);
    return status;
}

/* FILETIME as YYYY-MM-DD HH:MM:SS (UTC). */
static int append_filetime(xx_buf_t *out, uint64_t filetime)
{
    uint64_t seconds = filetime / 10000000u;
    int64_t days = (int64_t)(seconds / 86400u) - 134774; /* since 1970-01-01 */
    uint32_t rest = (uint32_t)(seconds % 86400u);
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = mp < 10 ? mp + 3 : mp - 9;

    if (m <= 2) {
        ++y;
    }
    return xx_buf_appendf(out, "%04lld-%02lld-%02lld %02u:%02u:%02u", (long long)y, (long long)m, (long long)d, (unsigned)(rest / 3600u), (unsigned)(rest / 60u % 60u),
                          (unsigned)(rest % 60u))
               ? 1
               : 0;
}

static xxbyte_status list_archive(const xxbyte_autoit_archive *archive, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    uint64_t offset = archive->entries_offset;
    uint32_t index;
    xxbyte_autoit_entry *entry;
    xxbyte_status result = XXBYTE_STATUS_OK;

    if (!xx_buf_appendf(out, "; AutoIt %s archive at 0x%08llX\n", archive->version == XXBYTE_AUTOIT_EA05 ? "EA05" : "EA06", (unsigned long long)archive->offset)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    entry = (xxbyte_autoit_entry *)xx_mem_alloc(sizeof(*entry));
    if (entry == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (index = 0;; ++index) {
        xxbyte_status status = xxbyte_autoit_read_entry(data, size, archive, offset, entry);
        uint8_t *content = NULL;
        size_t content_size = 0;
        int adler_ok = 0;
        xxbyte_status data_status;
        int ok;

        if (status == XXBYTE_STATUS_INVALID_INSTRUCTION) {
            break;
        }
        if (status != XXBYTE_STATUS_OK) {
            if (index == 0) {
                result = status;
            } else if (!xx_buf_appendf(out, "; entry %u at 0x%08llX: truncated\n", (unsigned)index, (unsigned long long)offset)) {
                result = XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            break;
        }
        data_status = xxbyte_autoit_entry_data(data, size, entry, &content, &content_size, &adler_ok);
        if (data_status == XXBYTE_STATUS_OUT_OF_MEMORY) {
            result = data_status;
            break;
        }
        ok = xx_buf_appendf(out, "; entry %u at 0x%08llX: ", (unsigned)index, (unsigned long long)entry->offset) &&
             (entry->kind == XXBYTE_AUTOIT_ENTRY_FILE ? xx_buf_appendf(out, "FileInstall \"%s\" name \"%s\"", entry->type, entry->name)
                                                      : xx_buf_appendf(out, "%s \"%s\"", entry->type, entry->name)) &&
             (entry->compressed == 1 ? xx_buf_appendf(out, ", compressed %u -> %u bytes", (unsigned)entry->packed_size, (unsigned)entry->size)
                                     : xx_buf_appendf(out, ", stored %u bytes", (unsigned)entry->packed_size)) &&
             xx_buf_appendf(out, ", adler32 %s, created ", adler_ok ? "ok" : "bad") && append_filetime(out, entry->created) && xx_buf_append_str(out, ", modified ") &&
             append_filetime(out, entry->modified) && (data_status == XXBYTE_STATUS_OK || xx_buf_appendf(out, ", data: %s", xxbyte_status_string(data_status))) &&
             xx_buf_append_char(out, '\n');
        if (!ok) {
            xxbyte_autoit_free(content);
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        if (data_status == XXBYTE_STATUS_OK) {
            if (entry->kind == XXBYTE_AUTOIT_ENTRY_SCRIPT) {
                status = list_tokens(content, content_size, listing_flags, out);
            } else if (entry->kind == XXBYTE_AUTOIT_ENTRY_TEXT || entry->kind == XXBYTE_AUTOIT_ENTRY_UNICODE_TEXT) {
                status = list_text(content, content_size, entry->kind == XXBYTE_AUTOIT_ENTRY_UNICODE_TEXT, out);
            }
        }
        xxbyte_autoit_free(content);
        if (status != XXBYTE_STATUS_OK) {
            result = status;
            break;
        }
        offset = entry->next;
    }
    xx_mem_free(entry);
    return result;
}

xxbyte_status xxbyte_autoit_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_autoit_archive archive;
    uint32_t method;
    uint32_t lines;
    xxbyte_instruction insn;

    (void)mode;
    if (xxbyte_autoit_find_archive(data, size, 0, &archive) == XXBYTE_STATUS_OK) {
        return list_archive(&archive, data, size, listing_flags, out);
    }
    if (xxbyte_autoit_unpacked_size(data, size, &method, NULL) == XXBYTE_STATUS_OK) {
        uint8_t *content = NULL;
        size_t content_size = 0;
        xxbyte_status status = unpack(data, size, &content, &content_size);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (method == XXBYTE_AUTOIT_PACK_EA06) {
            status = list_tokens(content, content_size, listing_flags, out);
        } else {
            status = list_text(content, content_size, 0, out);
        }
        xxbyte_autoit_free(content);
        return status;
    }
    if (size < 4) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    lines = xxbyte_u32le(data);
    if (lines > size - 4 || (size > 4 && xxbyte_decode(XXBYTE_FAMILY_AUTOIT, XXBYTE_AUTOIT_MODE_V3, data + 4, size - 4, 4, &insn) != XXBYTE_STATUS_OK)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return list_tokens(data, size, listing_flags, out);
}

#else
typedef int xxbyte_autoit_file_disabled;
#endif
