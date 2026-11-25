#include "xxbyte_hermes_internal.h"

#include "xxbyte/xxbyte_xxfc.h"

#if XXBYTE_USE_HERMES

#define HERMES_DELTA_MAGIC (~XXBYTE_HERMES_MAGIC)

/* Small function header sizes. */
#define HERMES_SMALL_HEADER_LEGACY 16u /* 59 to 96 */
#define HERMES_SMALL_HEADER_V98 12u    /* 98+ */
/* sizeof(FunctionHeader), the large header. */
#define HERMES_LARGE_HEADER_LEGACY 32u
#define HERMES_LARGE_HEADER_V98 40u
#define HERMES_LARGE_HEADER_V99 36u

/* BigInts longer than this many bytes are not converted to decimal. */
#define HERMES_BIGINT_DECIMAL_LIMIT 2048u
/* Displayed string characters before the text is cut with "...". */
#define HERMES_STRING_LIMIT 200u

static int hermes_file_version_ok(uint32_t version)
{
    return version >= XXBYTE_HERMES_MODE_OLDEST && version <= XXBYTE_HERMES_MODE_NEWEST && version != 97u;
}

/* Places a section of count * unit bytes at the aligned *cursor. */
static int hermes_section(uint64_t *cursor, uint64_t count, uint64_t unit, size_t size, uint32_t *offset)
{
    uint64_t start = (*cursor + 3u) & ~(uint64_t)3u;
    uint64_t bytes = count * unit; /* count < 2^32 and unit <= 8: no overflow */

    if (start > size || bytes > size - start) {
        return 0;
    }
    *offset = (uint32_t)start;
    *cursor = start + bytes;
    return 1;
}

xxbyte_status xxbyte_hermes_parse_header(const uint8_t *data, size_t size, xxbyte_hermes_header *header)
{
    xxbyte_hermes_header h;
    uint32_t v;
    uint32_t pos;
    uint64_t cursor;

    if (header == NULL || (data == NULL && size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(header, 0, sizeof(*header));
    if (size < XXBYTE_HERMES_HEADER_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (xxbyte_u64le(data) != XXBYTE_HERMES_MAGIC) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    memset(&h, 0, sizeof(h));
    v = xxbyte_u32le(data + 8);
    if (!hermes_file_version_ok(v)) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    h.version = v;
    memcpy(h.source_hash, data + 12, sizeof(h.source_hash));
    h.file_length = xxbyte_u32le(data + 32);
    h.global_code_index = xxbyte_u32le(data + 36);
    h.function_count = xxbyte_u32le(data + 40);
    h.string_kind_count = xxbyte_u32le(data + 44);
    h.identifier_count = xxbyte_u32le(data + 48);
    h.string_count = xxbyte_u32le(data + 52);
    h.overflow_string_count = xxbyte_u32le(data + 56);
    h.string_storage_size = xxbyte_u32le(data + 60);
    pos = 64;
    if (v >= 87) {
        h.bigint_count = xxbyte_u32le(data + 64);
        h.bigint_storage_size = xxbyte_u32le(data + 68);
        pos = 72;
    }
    h.regexp_count = xxbyte_u32le(data + pos);
    h.regexp_storage_size = xxbyte_u32le(data + pos + 4);
    h.array_buffer_size = xxbyte_u32le(data + pos + 8);
    h.obj_key_buffer_size = xxbyte_u32le(data + pos + 12);
    h.obj_value_buffer_size = xxbyte_u32le(data + pos + 16);
    pos += 20;
    if (v >= 98) {
        h.string_switch_count = xxbyte_u32le(data + pos);
        pos += 4;
    }
    h.segment_id = xxbyte_u32le(data + pos);
    h.cjs_module_count = xxbyte_u32le(data + pos + 4);
    pos += 8;
    if (v >= 84) {
        h.function_source_count = xxbyte_u32le(data + pos);
        pos += 4;
    }
    h.debug_info_offset = xxbyte_u32le(data + pos);
    h.options = data[pos + 4];
    h.function_header_size = (uint8_t)(v >= 98 ? HERMES_SMALL_HEADER_V98 : HERMES_SMALL_HEADER_LEGACY);

    cursor = XXBYTE_HERMES_HEADER_SIZE;
    if (!hermes_section(&cursor, h.function_count, h.function_header_size, size, &h.function_headers_offset) ||
        !hermes_section(&cursor, h.string_kind_count, 4, size, &h.string_kinds_offset) ||
        !hermes_section(&cursor, h.identifier_count, 4, size, &h.identifier_hashes_offset) ||
        !hermes_section(&cursor, h.string_count, 4, size, &h.small_string_table_offset) ||
        !hermes_section(&cursor, h.overflow_string_count, 8, size, &h.overflow_string_table_offset) ||
        !hermes_section(&cursor, h.string_storage_size, 1, size, &h.string_storage_offset) ||
        !hermes_section(&cursor, h.array_buffer_size, 1, size, &h.array_buffer_offset) ||
        !hermes_section(&cursor, h.obj_key_buffer_size, 1, size, &h.obj_key_buffer_offset) ||
        !hermes_section(&cursor, h.obj_value_buffer_size, v >= 98 ? 8u : 1u, size, &h.obj_value_buffer_offset) ||
        !hermes_section(&cursor, h.bigint_count, 8, size, &h.bigint_table_offset) || !hermes_section(&cursor, h.bigint_storage_size, 1, size, &h.bigint_storage_offset) ||
        !hermes_section(&cursor, h.regexp_count, 8, size, &h.regexp_table_offset) || !hermes_section(&cursor, h.regexp_storage_size, 1, size, &h.regexp_storage_offset) ||
        !hermes_section(&cursor, h.cjs_module_count, 8, size, &h.cjs_module_table_offset) ||
        !hermes_section(&cursor, h.function_source_count, 8, size, &h.function_source_table_offset)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    h.sections_end = (uint32_t)cursor;
    *header = h;
    return XXBYTE_STATUS_OK;
}

static int hermes_header_ok(const uint8_t *data, size_t size, const xxbyte_hermes_header *header)
{
    return header != NULL && data != NULL && hermes_file_version_ok(header->version) && header->sections_end != 0 && header->sections_end <= size;
}

xxbyte_status xxbyte_hermes_get_function(const uint8_t *data, size_t size, const xxbyte_hermes_header *header, uint32_t index, xxbyte_hermes_function *function)
{
    xxbyte_hermes_function f;
    const uint8_t *p;
    uint64_t at;

    if (function == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(function, 0, sizeof(*function));
    if (!hermes_header_ok(data, size, header) || index >= header->function_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(&f, 0, sizeof(f));
    f.index = index;
    at = (uint64_t)header->function_headers_offset + (uint64_t)index * header->function_header_size;
    f.header_offset = (uint32_t)at;
    p = data + at;
    if (header->version < 98) {
        uint32_t w0 = xxbyte_u32le(p);
        uint32_t w1 = xxbyte_u32le(p + 4);
        uint32_t w2 = xxbyte_u32le(p + 8);
        f.offset = w0 & 0x1FFFFFFu;
        f.param_count = w0 >> 25;
        f.bytecode_size = w1 & 0x7FFFu;
        f.function_name = w1 >> 15;
        f.info_offset = w2 & 0x1FFFFFFu;
        f.frame_size = w2 >> 25;
        f.environment_size = p[12];
        f.read_cache_size = p[13];
        f.write_cache_size = p[14];
        f.flags = p[15];
        if ((f.flags & XXBYTE_HERMES_FUNCTION_OVERFLOWED) != 0) {
            uint64_t large = ((uint64_t)f.info_offset << 16) | (f.offset & 0xFFFFu);
            if (large > size || size - large < HERMES_LARGE_HEADER_LEGACY) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            p = data + large;
            f.large_header_offset = (uint32_t)large;
            f.offset = xxbyte_u32le(p);
            f.param_count = xxbyte_u32le(p + 4);
            f.bytecode_size = xxbyte_u32le(p + 8);
            f.function_name = xxbyte_u32le(p + 12);
            f.info_offset = xxbyte_u32le(p + 16);
            f.frame_size = xxbyte_u32le(p + 20);
            f.environment_size = xxbyte_u32le(p + 24);
            f.read_cache_size = p[28];
            f.write_cache_size = p[29];
            f.flags = p[30];
        }
    } else {
        uint32_t w1 = xxbyte_u32le(p);
        uint32_t w2 = xxbyte_u32le(p + 4);
        f.offset = w1 & 0x1FFFFFFu;
        f.param_count = (w1 >> 25) & 0x1Fu;
        f.loop_depth = w1 >> 30;
        f.bytecode_size = w2 & 0x3FFFu;
        f.function_name = (w2 >> 14) & 0xFFu;
        f.number_reg_count = (w2 >> 22) & 0x1Fu;
        f.non_ptr_reg_count = w2 >> 27;
        f.frame_size = p[8];
        f.read_cache_size = p[9];
        f.write_cache_size = p[10] & (header->version == 98 ? 0x3Fu : 0x7Fu);
        f.flags = p[11];
        if ((f.flags & XXBYTE_HERMES_FUNCTION_OVERFLOWED) != 0) {
            uint64_t large = ((uint64_t)f.function_name << 24) | (f.offset & 0xFFFFFFu);
            uint32_t large_size = header->version == 98 ? HERMES_LARGE_HEADER_V98 : HERMES_LARGE_HEADER_V99;
            if (large > size || size - large < large_size) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            p = data + large;
            f.large_header_offset = (uint32_t)large;
            f.offset = xxbyte_u32le(p);
            f.param_count = xxbyte_u32le(p + 4);
            f.loop_depth = xxbyte_u32le(p + 8);
            f.bytecode_size = xxbyte_u32le(p + 12);
            f.function_name = xxbyte_u32le(p + 16);
            f.number_reg_count = xxbyte_u32le(p + 20);
            f.non_ptr_reg_count = xxbyte_u32le(p + 24);
            f.frame_size = xxbyte_u32le(p + 28);
            f.read_cache_size = p[32];
            f.write_cache_size = p[33];
            f.flags = p[large_size == HERMES_LARGE_HEADER_V98 ? 36u : 35u];
        }
        f.kind = (uint8_t)(f.flags >> 6);
    }
    f.prohibit_invoke = (uint8_t)(f.flags & 3u);
    *function = f;
    if (f.offset > size || f.bytecode_size > size - f.offset) {
        return XXBYTE_STATUS_TRUNCATED; /* the header is filled in */
    }
    return XXBYTE_STATUS_OK;
}

/* Whether string id is an identifier, from the string kind runs. */
static int hermes_is_identifier(const uint8_t *data, const xxbyte_hermes_header *header, uint32_t id)
{
    uint32_t i;
    uint64_t first = 0;
    unsigned shift = header->version <= 70 ? 30u : 31u;
    uint32_t mask = (UINT32_C(1) << shift) - 1u;

    for (i = 0; i < header->string_kind_count; ++i) {
        uint32_t entry = xxbyte_u32le(data + header->string_kinds_offset + (size_t)i * 4u);
        uint64_t count = entry & mask;
        if (id < first + count) {
            return (entry >> shift) != 0;
        }
        first += count;
    }
    return 0;
}

xxbyte_status xxbyte_hermes_get_string(const uint8_t *data, size_t size, const xxbyte_hermes_header *header, uint32_t id, xxbyte_hermes_string *string)
{
    uint32_t entry;
    uint32_t offset;
    uint32_t length;
    uint32_t utf16;
    uint64_t bytes;

    if (string == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(string, 0, sizeof(*string));
    if (!hermes_header_ok(data, size, header) || id >= header->string_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    entry = xxbyte_u32le(data + header->small_string_table_offset + (size_t)id * 4u);
    utf16 = entry & 1u;
    offset = (entry >> 1) & 0x7FFFFFu;
    length = entry >> 24;
    if (length == 0xFFu) {
        const uint8_t *overflow;
        if (offset >= header->overflow_string_count) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        overflow = data + header->overflow_string_table_offset + (size_t)offset * 8u;
        offset = xxbyte_u32le(overflow);
        length = xxbyte_u32le(overflow + 4);
    }
    bytes = (uint64_t)length << utf16;
    if (offset > header->string_storage_size || bytes > header->string_storage_size - offset) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    string->offset = header->string_storage_offset + offset;
    string->length = length;
    string->utf16 = (uint8_t)utf16;
    string->identifier = (uint8_t)hermes_is_identifier(data, header, id);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_hermes_get_bigint(const uint8_t *data, size_t size, const xxbyte_hermes_header *header, uint32_t index, uint32_t *offset, uint32_t *length)
{
    const uint8_t *entry;
    uint32_t o;
    uint32_t n;

    if (offset == NULL || length == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *offset = 0;
    *length = 0;
    if (!hermes_header_ok(data, size, header) || index >= header->bigint_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    entry = data + header->bigint_table_offset + (size_t)index * 8u;
    o = xxbyte_u32le(entry);
    n = xxbyte_u32le(entry + 4);
    if (o > header->bigint_storage_size || n > header->bigint_storage_size - o) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *offset = header->bigint_storage_offset + o;
    *length = n;
    return XXBYTE_STATUS_OK;
}

/* ------------------------------------------------------------------------
 * Listing. */

typedef struct hermes_names {
    const uint8_t *data;
    size_t size;
    const xxbyte_hermes_header *header;
} hermes_names;

/* A bounded text sink for the resolver buffer. */
typedef struct hermes_sink {
    char *buffer;
    size_t capacity; /* including the NUL */
    size_t length;
    int full;
} hermes_sink;

static void sink_char(hermes_sink *sink, char c)
{
    if (sink->length + 1 < sink->capacity) {
        sink->buffer[sink->length++] = c;
        sink->buffer[sink->length] = '\0';
    } else {
        sink->full = 1;
    }
}

static void sink_str(hermes_sink *sink, const char *text)
{
    while (*text != '\0') {
        sink_char(sink, *text++);
    }
}

static void sink_uint(hermes_sink *sink, size_t value)
{
    char text[24];
    size_t n = 0;
    do {
        text[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0 && n < sizeof(text));
    while (n > 0) {
        sink_char(sink, text[--n]);
    }
}

static void sink_hex(hermes_sink *sink, const char *prefix, uint32_t value, int digits)
{
    static const char hex[] = "0123456789abcdef";
    sink_str(sink, prefix);
    while (digits-- > 0) {
        sink_char(sink, hex[(value >> (digits * 4)) & 0xFu]);
    }
}

/* Appends string id: quoted and escaped ("\"" "\\" "\xHH" "\uHHHH"), or
 * raw for a function name. Returns 0 when the string is unreadable. */
static int hermes_put_string(const hermes_names *names, uint32_t id, int quoted, hermes_sink *sink)
{
    xxbyte_hermes_string s;
    uint32_t i;

    if (xxbyte_hermes_get_string(names->data, names->size, names->header, id, &s) != XXBYTE_STATUS_OK) {
        return 0;
    }
    if (quoted) {
        sink_char(sink, '"');
    }
    for (i = 0; i < s.length; ++i) {
        uint32_t c = s.utf16 ? xxbyte_u16le(names->data + s.offset + (size_t)i * 2u) : names->data[s.offset + i];
        if (i == HERMES_STRING_LIMIT) {
            sink_str(sink, quoted ? "\"..." : "...");
            return 1;
        }
        if (c == '"' && quoted) {
            sink_str(sink, "\\\"");
        } else if (c == '\\' && quoted) {
            sink_str(sink, "\\\\");
        } else if (c < 32 || c == 127 || (c > 127 && c < 256)) {
            sink_hex(sink, "\\x", c, 2);
        } else if (c > 127) {
            sink_hex(sink, "\\u", c, 4);
        } else {
            sink_char(sink, (char)c);
        }
    }
    if (quoted) {
        sink_char(sink, '"');
    }
    return 1;
}

/* Function<name>, Constructor<name> or NCFunction<name>. */
static int hermes_put_function(const hermes_names *names, uint32_t index, hermes_sink *sink)
{
    xxbyte_hermes_function f;
    xxbyte_status status = xxbyte_hermes_get_function(names->data, names->size, names->header, index, &f);

    if (status != XXBYTE_STATUS_OK && (status != XXBYTE_STATUS_TRUNCATED || f.header_offset == 0)) {
        return 0; /* the large header is unreadable */
    }
    switch (f.prohibit_invoke) {
        case XXBYTE_HERMES_PROHIBIT_CALL: sink_str(sink, "Constructor<"); break;
        case XXBYTE_HERMES_PROHIBIT_CONSTRUCT: sink_str(sink, "NCFunction<"); break;
        default: sink_str(sink, "Function<"); break;
    }
    if (!hermes_put_string(names, f.function_name, 0, sink)) {
        sink_str(sink, "?");
    }
    sink_char(sink, '>');
    return 1;
}

/* The decimal value of a little-endian two's complement BigInt, "<N>n". */
static int hermes_put_bigint(const hermes_names *names, uint32_t index, hermes_sink *sink)
{
    uint32_t offset;
    uint32_t length;
    uint8_t mag[HERMES_BIGINT_DECIMAL_LIMIT];
    char digits[HERMES_BIGINT_DECIMAL_LIMIT * 3u + 8u];
    size_t ndigits = 0;
    size_t n;
    size_t i;
    int negative;

    if (xxbyte_hermes_get_bigint(names->data, names->size, names->header, index, &offset, &length) != XXBYTE_STATUS_OK) {
        return 0;
    }
    if (length > HERMES_BIGINT_DECIMAL_LIMIT) {
        return 0;
    }
    n = length;
    negative = n != 0 && (names->data[offset + n - 1] & 0x80u) != 0;
    /* magnitude, big-endian */
    {
        unsigned carry = 1;
        for (i = 0; i < n; ++i) {
            unsigned b = names->data[offset + i];
            if (negative) {
                b = (~b & 0xFFu) + carry;
                carry = b >> 8;
                b &= 0xFFu;
            }
            mag[n - 1 - i] = (uint8_t)b;
        }
    }
    /* repeated division by 10 */
    {
        size_t first = 0;
        while (first < n && mag[first] == 0) {
            ++first;
        }
        while (first < n) {
            unsigned rem = 0;
            for (i = first; i < n; ++i) {
                unsigned cur = (rem << 8) | mag[i];
                mag[i] = (uint8_t)(cur / 10u);
                rem = cur % 10u;
            }
            digits[ndigits++] = (char)('0' + rem);
            while (first < n && mag[first] == 0) {
                ++first;
            }
        }
    }
    if (ndigits == 0) {
        digits[ndigits++] = '0';
        negative = 0;
    }
    if (negative) {
        sink_char(sink, '-');
    }
    if (ndigits + 32u >= sink->capacity) {
        /* too long for the resolver buffer: "123...(4000 decimal digits)n" */
        size_t total = ndigits;
        size_t keep = sink->capacity > 64u ? sink->capacity - 64u : 1u;
        while (keep-- > 0 && ndigits > 0) {
            sink_char(sink, digits[--ndigits]);
        }
        sink_str(sink, "... (");
        sink_uint(sink, total);
        sink_str(sink, " decimal digits)n");
        return 1;
    }
    while (ndigits > 0) {
        sink_char(sink, digits[--ndigits]);
    }
    sink_char(sink, 'n');
    return 1;
}

static size_t hermes_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const hermes_names *names = (const hermes_names *)context;
    const xxbyte_operand *operand = &instruction->operands[operand_index];
    hermes_sink sink;
    int ok = 0;

    if (buffer_size < 2 || operand->type != XXBYTE_OPERAND_INDEX || operand->value < 0 || operand->value > (int64_t)UINT32_MAX) {
        return 0;
    }
    sink.buffer = buffer;
    sink.capacity = buffer_size;
    sink.length = 0;
    sink.full = 0;
    buffer[0] = '\0';
    switch (operand->kind) {
        case XXBYTE_INDEX_STRING: ok = hermes_put_string(names, (uint32_t)operand->value, 1, &sink); break;
        case XXBYTE_INDEX_FUNCTION: ok = hermes_put_function(names, (uint32_t)operand->value, &sink); break;
        case XXBYTE_HERMES_INDEX_BIGINT: ok = hermes_put_bigint(names, (uint32_t)operand->value, &sink); break;
        default: break;
    }
    if (!ok) {
        buffer[0] = '\0';
        return 0;
    }
    return sink.length;
}

static int hermes_list_header(const xxbyte_hermes_header *h, xx_buf_t *out)
{
    size_t i;

    if (!xx_buf_appendf(out, "; hbc version %u, %u functions, %u strings, %u bigints, %u regexps, global function %u\n; source hash ", h->version, h->function_count,
                        h->string_count, h->bigint_count, h->regexp_count, h->global_code_index)) {
        return 0;
    }
    for (i = 0; i < sizeof(h->source_hash); ++i) {
        if (!xx_buf_appendf(out, "%02x", h->source_hash[i])) {
            return 0;
        }
    }
    return xx_buf_append_char(out, '\n') ? 1 : 0;
}

static int hermes_list_function_line(const hermes_names *names, const xxbyte_hermes_function *f, xx_buf_t *out)
{
    char text[512];
    hermes_sink sink;

    sink.buffer = text;
    sink.capacity = sizeof(text);
    sink.length = 0;
    sink.full = 0;
    text[0] = '\0';
    switch (f->prohibit_invoke) {
        case XXBYTE_HERMES_PROHIBIT_CALL: sink_str(&sink, "Constructor<"); break;
        case XXBYTE_HERMES_PROHIBIT_CONSTRUCT: sink_str(&sink, "NCFunction<"); break;
        default: sink_str(&sink, "Function<"); break;
    }
    if (!hermes_put_string(names, f->function_name, 0, &sink)) {
        sink_str(&sink, "?");
    }
    sink_char(&sink, '>');
    if (!xx_buf_appendf(out, "\n; %s(%u params, %u registers", text, f->param_count, f->frame_size)) {
        return 0;
    }
    if (names->header->version >= 98 && !xx_buf_appendf(out, ", %u numbers, %u non-pointers", f->number_reg_count, f->non_ptr_reg_count)) {
        return 0;
    }
    return xx_buf_appendf(out, ") #%u @0x%04x, %u bytes\n", f->index, f->offset, f->bytecode_size) ? 1 : 0;
}

xxbyte_status xxbyte_hermes_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_hermes_header header;
    hermes_names names;
    xxbyte_hermes_context context;
    xxbyte_status status;
    xxbyte_status result = XXBYTE_STATUS_OK;
    uint32_t i;

    if (size < 8 || xxbyte_u64le(data) != XXBYTE_HERMES_MAGIC) {
        return size < 8 ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    status = xxbyte_hermes_parse_header(data, size, &header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (mode == XXBYTE_MODE_AUTO) {
        mode = header.version;
    }
    if (!hermes_list_header(&header, out)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    names.data = data;
    names.size = size;
    names.header = &header;
    context.data = data;
    context.size = size;
    context.address = 0;
    for (i = 0; i < header.function_count; ++i) {
        xxbyte_hermes_function f;

        status = xxbyte_hermes_get_function(data, size, &header, i, &f);
        if (status != XXBYTE_STATUS_OK && f.header_offset == 0) {
            /* the large header is unreadable */
            if (!xx_buf_appendf(out, "\n; function #%u: large header outside the file\n", i)) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            result = XXBYTE_STATUS_TRUNCATED;
            continue;
        }
        if (!hermes_list_function_line(&names, &f, out)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        if (status != XXBYTE_STATUS_OK) {
            if (!xx_buf_append_str(out, "; bytecode outside the file\n")) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            result = XXBYTE_STATUS_TRUNCATED;
            continue;
        }
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_HERMES, mode, &context, data + f.offset, f.bytecode_size, f.offset, listing_flags, hermes_resolver, &names, out);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return result;
}

#else
typedef int xxbyte_hermes_file_disabled;
#endif
