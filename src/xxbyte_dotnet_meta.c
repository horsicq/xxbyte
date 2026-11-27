/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* ECMA-335 PE files, metadata root, streams and tables (Partition II). */
#include "xxbyte_dotnet_internal.h"

#if XXBYTE_USE_DOTNET

/* Column kinds. */
enum {
    K_END = 0,
    K_U8,
    K_U16,
    K_U32,
    K_STRING,
    K_GUID,
    K_BLOB,
    K_TABLE = 0x40, /* + table number: simple index */
    K_CODED = 0x80  /* + coded index kind */
};

/* Coded index kinds (II.24.2.6). */
enum {
    CI_TYPEDEFORREF,
    CI_HASCONSTANT,
    CI_HASCUSTOMATTRIBUTE,
    CI_HASFIELDMARSHAL,
    CI_HASDECLSECURITY,
    CI_MEMBERREFPARENT,
    CI_HASSEMANTICS,
    CI_METHODDEFORREF,
    CI_MEMBERFORWARDED,
    CI_IMPLEMENTATION,
    CI_CUSTOMATTRIBUTETYPE,
    CI_RESOLUTIONSCOPE,
    CI_TYPEORMETHODDEF,
    CI_COUNT
};

#define NO_TABLE 0xFFu

typedef struct coded_index {
    uint8_t bits;
    uint8_t count;
    uint8_t tables[22];
} coded_index;

static const coded_index coded_indexes[CI_COUNT] = {
    {2, 3, {0x02, 0x01, 0x1B}},
    {2, 3, {0x04, 0x08, 0x17}},
    {5, 22, {0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17, 0x14, 0x11, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C, 0x2B}},
    {1, 2, {0x04, 0x08}},
    {2, 3, {0x02, 0x06, 0x20}},
    {3, 5, {0x02, 0x01, 0x1A, 0x06, 0x1B}},
    {1, 2, {0x14, 0x17}},
    {1, 2, {0x06, 0x0A}},
    {1, 2, {0x04, 0x06}},
    {2, 3, {0x26, 0x23, 0x27}},
    {3, 5, {NO_TABLE, NO_TABLE, 0x06, 0x0A, NO_TABLE}},
    {2, 4, {0x00, 0x1A, 0x23, 0x01}},
    {1, 2, {0x02, 0x06}},
};

#define T(table) (K_TABLE + (table))
#define C(kind) (K_CODED + (kind))
#define MAX_COLUMNS 9

/* II.22, by table number. */
static const uint8_t schema[XXBYTE_DOTNET_TABLE_KNOWN][MAX_COLUMNS + 1] = {
    /* 0x00 Module */ {K_U16, K_STRING, K_GUID, K_GUID, K_GUID},
    /* 0x01 TypeRef */ {C(CI_RESOLUTIONSCOPE), K_STRING, K_STRING},
    /* 0x02 TypeDef */ {K_U32, K_STRING, K_STRING, C(CI_TYPEDEFORREF), T(0x04), T(0x06)},
    /* 0x03 FieldPtr */ {T(0x04)},
    /* 0x04 Field */ {K_U16, K_STRING, K_BLOB},
    /* 0x05 MethodPtr */ {T(0x06)},
    /* 0x06 MethodDef */ {K_U32, K_U16, K_U16, K_STRING, K_BLOB, T(0x08)},
    /* 0x07 ParamPtr */ {T(0x08)},
    /* 0x08 Param */ {K_U16, K_U16, K_STRING},
    /* 0x09 InterfaceImpl */ {T(0x02), C(CI_TYPEDEFORREF)},
    /* 0x0A MemberRef */ {C(CI_MEMBERREFPARENT), K_STRING, K_BLOB},
    /* 0x0B Constant */ {K_U8, K_U8, C(CI_HASCONSTANT), K_BLOB},
    /* 0x0C CustomAttribute */ {C(CI_HASCUSTOMATTRIBUTE), C(CI_CUSTOMATTRIBUTETYPE), K_BLOB},
    /* 0x0D FieldMarshal */ {C(CI_HASFIELDMARSHAL), K_BLOB},
    /* 0x0E DeclSecurity */ {K_U16, C(CI_HASDECLSECURITY), K_BLOB},
    /* 0x0F ClassLayout */ {K_U16, K_U32, T(0x02)},
    /* 0x10 FieldLayout */ {K_U32, T(0x04)},
    /* 0x11 StandAloneSig */ {K_BLOB},
    /* 0x12 EventMap */ {T(0x02), T(0x14)},
    /* 0x13 EventPtr */ {T(0x14)},
    /* 0x14 Event */ {K_U16, K_STRING, C(CI_TYPEDEFORREF)},
    /* 0x15 PropertyMap */ {T(0x02), T(0x17)},
    /* 0x16 PropertyPtr */ {T(0x17)},
    /* 0x17 Property */ {K_U16, K_STRING, K_BLOB},
    /* 0x18 MethodSemantics */ {K_U16, T(0x06), C(CI_HASSEMANTICS)},
    /* 0x19 MethodImpl */ {T(0x02), C(CI_METHODDEFORREF), C(CI_METHODDEFORREF)},
    /* 0x1A ModuleRef */ {K_STRING},
    /* 0x1B TypeSpec */ {K_BLOB},
    /* 0x1C ImplMap */ {K_U16, C(CI_MEMBERFORWARDED), K_STRING, T(0x1A)},
    /* 0x1D FieldRVA */ {K_U32, T(0x04)},
    /* 0x1E EncLog */ {K_U32, K_U32},
    /* 0x1F EncMap */ {K_U32},
    /* 0x20 Assembly */ {K_U32, K_U16, K_U16, K_U16, K_U16, K_U32, K_BLOB, K_STRING, K_STRING},
    /* 0x21 AssemblyProcessor */ {K_U32},
    /* 0x22 AssemblyOS */ {K_U32, K_U32, K_U32},
    /* 0x23 AssemblyRef */ {K_U16, K_U16, K_U16, K_U16, K_U32, K_BLOB, K_STRING, K_STRING, K_BLOB},
    /* 0x24 AssemblyRefProcessor */ {K_U32, T(0x23)},
    /* 0x25 AssemblyRefOS */ {K_U32, K_U32, K_U32, T(0x23)},
    /* 0x26 File */ {K_U32, K_STRING, K_BLOB},
    /* 0x27 ExportedType */ {K_U32, K_U32, K_STRING, K_STRING, C(CI_IMPLEMENTATION)},
    /* 0x28 ManifestResource */ {K_U32, K_U32, K_STRING, C(CI_IMPLEMENTATION)},
    /* 0x29 NestedClass */ {T(0x02), T(0x02)},
    /* 0x2A GenericParam */ {K_U16, K_U16, C(CI_TYPEORMETHODDEF), K_STRING},
    /* 0x2B MethodSpec */ {C(CI_METHODDEFORREF), K_BLOB},
    /* 0x2C GenericParamConstraint */ {T(0x2A), C(CI_TYPEDEFORREF)},
};

static uint32_t u32_at(const uint8_t *p)
{
    return xxbyte_u32le(p);
}

static int in_range(size_t size, uint64_t offset, uint64_t length)
{
    return offset <= size && length <= size - offset;
}

static uint32_t coded_size(const uint32_t *rows, uint32_t kind)
{
    const coded_index *ci = &coded_indexes[kind];
    uint32_t limit = 1u << (16u - ci->bits);
    uint32_t i;

    for (i = 0; i < ci->count; ++i) {
        if (ci->tables[i] != NO_TABLE && rows[ci->tables[i]] >= limit) {
            return 4;
        }
    }
    return 2;
}

static uint32_t column_size(const xxbyte_dotnet_image *image, uint8_t kind)
{
    switch (kind) {
        case K_U8: return 1;
        case K_U16: return 2;
        case K_U32: return 4;
        case K_STRING: return (image->heap_sizes & 0x01u) ? 4u : 2u;
        case K_GUID: return (image->heap_sizes & 0x02u) ? 4u : 2u;
        case K_BLOB: return (image->heap_sizes & 0x04u) ? 4u : 2u;
        default: break;
    }
    if (kind >= K_CODED) {
        return coded_size(image->rows, (uint32_t)(kind - K_CODED));
    }
    return image->rows[kind - K_TABLE] > 0xFFFFu ? 4u : 2u;
}

static uint32_t decode_column(const xxbyte_dotnet_image *image, uint8_t kind, const uint8_t *p, uint32_t size)
{
    uint32_t value = size == 1 ? p[0] : size == 2 ? xxbyte_u16le(p) : u32_at(p);

    if (kind >= K_CODED) {
        const coded_index *ci = &coded_indexes[kind - K_CODED];
        uint32_t tag = value & ((1u << ci->bits) - 1u);
        uint32_t row = value >> ci->bits;
        uint32_t table = tag < ci->count ? ci->tables[tag] : NO_TABLE;
        (void)image;
        return XXBYTE_DOTNET_TOKEN(table, row);
    }
    return value;
}

uint32_t xxbyte_dotnet_column(const xxbyte_dotnet_image *image, xxbyte_dotnet_table table, uint32_t row, uint32_t column)
{
    const uint8_t *p = image->data + image->table_offset[table] + (size_t)(row - 1u) * image->row_size[table];
    const uint8_t *kinds = schema[table];
    uint32_t i;

    for (i = 0; i < column; ++i) {
        p += column_size(image, kinds[i]);
    }
    return decode_column(image, kinds[column], p, column_size(image, kinds[column]));
}

uint32_t xxbyte_dotnet_column_count(xxbyte_dotnet_table table)
{
    uint32_t n = 0;

    if ((uint32_t)table >= XXBYTE_DOTNET_TABLE_KNOWN) {
        return 0;
    }
    while (n < MAX_COLUMNS && schema[table][n] != K_END) {
        ++n;
    }
    return n;
}

xxbyte_status xxbyte_dotnet_read_row(const xxbyte_dotnet_image *image, xxbyte_dotnet_table table, uint32_t row, uint32_t *values, size_t capacity, size_t *count)
{
    uint32_t n, i;
    const uint8_t *p;

    if (count != NULL) {
        *count = 0;
    }
    if (image == NULL || (values == NULL && capacity != 0) || (uint32_t)table >= XXBYTE_DOTNET_TABLE_KNOWN) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    n = xxbyte_dotnet_column_count(table);
    if (row == 0 || row > image->rows[table] || image->row_size[table] == 0) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    p = image->data + image->table_offset[table] + (size_t)(row - 1u) * image->row_size[table];
    for (i = 0; i < n; ++i) {
        uint32_t size = column_size(image, schema[table][i]);
        if (i < capacity) {
            values[i] = decode_column(image, schema[table][i], p, size);
        }
        p += size;
    }
    if (count != NULL) {
        *count = n;
    }
    return XXBYTE_STATUS_OK;
}

/* ---- tables stream ---- */

static xxbyte_status open_tables(xxbyte_dotnet_image *image)
{
    const uint8_t *s;
    uint64_t offset, end;
    uint32_t t;

    if (image->tables.size < 24) {
        return image->tables.size == 0 ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_TRUNCATED;
    }
    s = image->data + image->tables.offset;
    image->tables_major = s[4];
    image->tables_minor = s[5];
    image->heap_sizes = s[6];
    image->valid = xxbyte_u64le(s + 8);
    image->sorted = xxbyte_u64le(s + 16);
    offset = 24;
    end = image->tables.size;
    for (t = 0; t < XXBYTE_DOTNET_TABLE_MAX; ++t) {
        if ((image->valid >> t) & 1u) {
            if (offset + 4 > end) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            image->rows[t] = u32_at(s + offset);
            offset += 4;
        }
    }
    /* Heap flag 0x40 (the CLR's EXTRA_DATA) adds 4 bytes after the row
     * counts. */
    if ((image->heap_sizes & 0x40u) != 0) {
        offset += 4;
    }
    for (t = 0; t < XXBYTE_DOTNET_TABLE_KNOWN; ++t) {
        uint32_t c, row_size = 0;
        uint64_t bytes;

        if (image->rows[t] == 0) {
            continue;
        }
        for (c = 0; c < MAX_COLUMNS && schema[t][c] != K_END; ++c) {
            row_size += column_size(image, schema[t][c]);
        }
        bytes = (uint64_t)row_size * image->rows[t];
        if (offset > end || bytes > end - offset) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        image->row_size[t] = row_size;
        image->table_offset[t] = (uint32_t)(image->tables.offset + offset);
        offset += bytes;
    }
    return XXBYTE_STATUS_OK;
}

/* ---- metadata root ---- */

static int stream_is(const uint8_t *name, size_t length, const char *expected)
{
    size_t n = strlen(expected);
    return length == n && memcmp(name, expected, n) == 0;
}

static xxbyte_status open_root(xxbyte_dotnet_image *image, size_t root, size_t limit)
{
    const uint8_t *d = image->data;
    uint32_t length, i, count;
    size_t p;
    int have_tables = 0;

    /* limit: bytes available from root (the metadata directory clipped to
     * its section, or the whole input). */
    if (limit < 16) {
        return limit >= 4 && u32_at(d + root) != 0x424A5342u ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_TRUNCATED;
    }
    if (u32_at(d + root) != 0x424A5342u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    image->metadata_offset = (uint32_t)root;
    image->metadata_size = (uint32_t)limit;
    image->metadata_major = xxbyte_u16le(d + root + 4);
    image->metadata_minor = xxbyte_u16le(d + root + 6);
    length = u32_at(d + root + 12);
    if (length > limit - 16 || length > 255u || limit - 16 - length < 4) {
        return length > 255u ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_TRUNCATED;
    }
    image->version_offset = (uint32_t)(root + 16);
    for (i = 0; i < length && d[root + 16 + i] != 0; ++i) {
    }
    image->version_length = i;
    p = 16 + (size_t)length; /* flags (2) and stream count (2) follow */
    count = xxbyte_u16le(d + root + p + 2);
    p += 4;
    for (i = 0; i < count; ++i) {
        uint32_t offset, size, n;
        const uint8_t *name;
        xxbyte_dotnet_stream stream;

        if (limit - p < 8) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        offset = u32_at(d + root + p);
        size = u32_at(d + root + p + 4);
        name = d + root + p + 8;
        p += 8;
        for (n = 0; n < 32 && p + n < limit && name[n] != 0; ++n) {
        }
        if (n == 32) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (p + n >= limit) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        /* The name, its NUL and padding to a 4-byte boundary. */
        p += ((size_t)n + 4u) & ~(size_t)3u;
        if (p > limit) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (!in_range(limit, offset, size)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        stream.offset = (uint32_t)(root + offset);
        stream.size = size;
        if (stream_is(name, n, "#~") || stream_is(name, n, "#-")) {
            image->tables = stream;
            image->uncompressed = name[1] == '-';
            have_tables = 1;
        } else if (stream_is(name, n, "#Strings")) {
            image->strings = stream;
        } else if (stream_is(name, n, "#US")) {
            image->user_strings = stream;
        } else if (stream_is(name, n, "#Blob")) {
            image->blob = stream;
        } else if (stream_is(name, n, "#GUID")) {
            image->guid = stream;
        }
    }
    if (!have_tables) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return open_tables(image);
}

static void clear_image(xxbyte_dotnet_image *image)
{
    memset(image, 0, sizeof(*image));
}

xxbyte_status xxbyte_dotnet_open_metadata(const uint8_t *data, size_t size, xxbyte_dotnet_image *image)
{
    xxbyte_status status;

    if (image == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    clear_image(image);
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size > UINT32_MAX) {
        size = UINT32_MAX;
    }
    image->data = data;
    image->size = size;
    status = open_root(image, 0, size);
    if (status != XXBYTE_STATUS_OK) {
        clear_image(image);
    }
    return status;
}

/* ---- PE ---- */

int xxbyte_dotnet_rva_to_offset(const xxbyte_dotnet_image *image, uint32_t rva, size_t *offset, size_t *available)
{
    uint32_t i;

    if (image == NULL || offset == NULL || !image->is_pe) {
        return 0;
    }
    for (i = 0; i < image->section_count; ++i) {
        const uint8_t *s = image->data + image->section_table + (size_t)i * 40u;
        uint32_t va = u32_at(s + 12);
        uint32_t raw_size = u32_at(s + 16);
        uint32_t raw = u32_at(s + 20);
        uint64_t at, end;

        if (rva < va || rva - va >= raw_size) {
            continue;
        }
        at = (uint64_t)raw + (rva - va);
        if (at >= image->size) {
            return 0;
        }
        end = (uint64_t)raw + raw_size;
        if (end > image->size) {
            end = image->size;
        }
        *offset = (size_t)at;
        if (available != NULL) {
            *available = (size_t)(end - at);
        }
        return 1;
    }
    return 0;
}

xxbyte_status xxbyte_dotnet_open_image(const uint8_t *data, size_t size, xxbyte_dotnet_image *image)
{
    uint32_t pe, opt, opt_size, dirs, ndirs, cli_rva, cli_size, md_size;
    size_t cli, md, avail;
    uint16_t magic;
    xxbyte_status status;
    const uint8_t *c;

    if (image == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    clear_image(image);
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size > UINT32_MAX) {
        size = UINT32_MAX;
    }
    if (size < 2 || data[0] != 'M' || data[1] != 'Z') {
        return size < 2 ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (size < 0x40) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    pe = u32_at(data + 0x3C);
    if (!in_range(size, pe, 24)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (u32_at(data + pe) != 0x00004550u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    image->data = data;
    image->size = size;
    image->is_pe = 1;
    image->machine = xxbyte_u16le(data + pe + 4);
    image->section_count = xxbyte_u16le(data + pe + 6);
    opt_size = xxbyte_u16le(data + pe + 20);
    image->characteristics = xxbyte_u16le(data + pe + 22);
    opt = pe + 24;
    if (!in_range(size, opt, 2) || opt_size < 2) {
        clear_image(image);
        return opt_size < 2 ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_TRUNCATED;
    }
    magic = xxbyte_u16le(data + opt);
    if (magic == 0x10Bu) {
        dirs = 96;
    } else if (magic == 0x20Bu) {
        dirs = 112;
        image->pe32_plus = 1;
    } else {
        clear_image(image);
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    /* Data directory 14 (CLI header) must lie in the optional header. */
    if (opt_size < dirs + 15u * 8u) {
        clear_image(image);
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!in_range(size, opt, opt_size)) {
        clear_image(image);
        return XXBYTE_STATUS_TRUNCATED;
    }
    ndirs = u32_at(data + opt + dirs - 4);
    if (ndirs < 15) {
        clear_image(image);
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    image->section_table = opt + opt_size;
    if (!in_range(size, image->section_table, (uint64_t)image->section_count * 40u)) {
        clear_image(image);
        return XXBYTE_STATUS_TRUNCATED;
    }
    cli_rva = u32_at(data + opt + dirs + 14u * 8u);
    cli_size = u32_at(data + opt + dirs + 14u * 8u + 4u);
    if (cli_rva == 0 || cli_size == 0) {
        clear_image(image);
        return XXBYTE_STATUS_INVALID_INSTRUCTION; /* a native PE */
    }
    if (!xxbyte_dotnet_rva_to_offset(image, cli_rva, &cli, &avail)) {
        clear_image(image);
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (avail < 72) {
        clear_image(image);
        return XXBYTE_STATUS_TRUNCATED;
    }
    c = data + cli;
    image->cli_header = (uint32_t)cli;
    image->runtime_major = xxbyte_u16le(c + 4);
    image->runtime_minor = xxbyte_u16le(c + 6);
    image->metadata_rva = u32_at(c + 8);
    md_size = u32_at(c + 12);
    image->cli_flags = u32_at(c + 16);
    image->entry_point_token = u32_at(c + 20);
    if (!xxbyte_dotnet_rva_to_offset(image, image->metadata_rva, &md, &avail)) {
        clear_image(image);
        return XXBYTE_STATUS_TRUNCATED;
    }
    /* Streams may extend past the directory size (some packers shrink it)
     * but not past the section. */
    (void)md_size;
    status = open_root(image, md, avail);
    if (status != XXBYTE_STATUS_OK) {
        clear_image(image);
    }
    return status;
}

/* ---- heaps ---- */

xxbyte_status xxbyte_dotnet_read_compressed(const uint8_t *data, size_t size, size_t *offset, uint32_t *value)
{
    size_t at;
    uint8_t b;

    if (data == NULL || offset == NULL || value == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    at = *offset;
    if (at >= size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    b = data[at];
    if ((b & 0x80u) == 0) {
        *value = b;
        *offset = at + 1;
    } else if ((b & 0xC0u) == 0x80u) {
        if (size - at < 2) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        *value = ((uint32_t)(b & 0x3Fu) << 8) | data[at + 1];
        *offset = at + 2;
    } else if ((b & 0xE0u) == 0xC0u) {
        if (size - at < 4) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        *value = ((uint32_t)(b & 0x1Fu) << 24) | ((uint32_t)data[at + 1] << 16) | ((uint32_t)data[at + 2] << 8) | data[at + 3];
        *offset = at + 4;
    } else {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_dotnet_read_compressed_signed(const uint8_t *data, size_t size, size_t *offset, int32_t *value)
{
    size_t at;
    uint32_t raw, bits;
    xxbyte_status status;

    if (offset == NULL || value == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    at = *offset;
    status = xxbyte_dotnet_read_compressed(data, size, offset, &raw);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    bits = (*offset - at == 1) ? 7u : (*offset - at == 2) ? 14u : 29u;
    /* Undo the rotation: bit 0 is the sign. */
    if (raw & 1u) {
        *value = (int32_t)xxbyte_sext((raw >> 1) | (1u << (bits - 1u)), bits);
    } else {
        *value = (int32_t)(raw >> 1);
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_dotnet_get_string(const xxbyte_dotnet_image *image, uint32_t index, const char **text, size_t *length)
{
    const uint8_t *heap;
    size_t n;

    if (image == NULL || text == NULL || length == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *text = NULL;
    *length = 0;
    if (index >= image->strings.size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    heap = image->data + image->strings.offset;
    for (n = index; n < image->strings.size && heap[n] != 0; ++n) {
    }
    *text = (const char *)(heap + index);
    *length = n - index;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status get_sized(const xxbyte_dotnet_image *image, const xxbyte_dotnet_stream *heap, uint32_t index, const uint8_t **item, size_t *length)
{
    const uint8_t *base;
    size_t at = index;
    uint32_t n;
    xxbyte_status status;

    if (image == NULL || item == NULL || length == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *item = NULL;
    *length = 0;
    if (index >= heap->size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    base = image->data + heap->offset;
    status = xxbyte_dotnet_read_compressed(base, heap->size, &at, &n);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (n > heap->size - at) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *item = base + at;
    *length = n;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_dotnet_get_blob(const xxbyte_dotnet_image *image, uint32_t index, const uint8_t **blob, size_t *length)
{
    return get_sized(image, image != NULL ? &image->blob : NULL, index, blob, length);
}

xxbyte_status xxbyte_dotnet_get_user_string(const xxbyte_dotnet_image *image, uint32_t index, const uint8_t **utf16, size_t *length)
{
    xxbyte_status status = get_sized(image, image != NULL ? &image->user_strings : NULL, index, utf16, length);

    if (status == XXBYTE_STATUS_OK) {
        *length &= ~(size_t)1u; /* drop the flag byte */
    }
    return status;
}

xxbyte_status xxbyte_dotnet_get_guid(const xxbyte_dotnet_image *image, uint32_t index, const uint8_t **guid)
{
    if (image == NULL || guid == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *guid = NULL;
    if (index == 0 || (uint64_t)index * 16u > image->guid.size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *guid = image->data + image->guid.offset + (size_t)(index - 1u) * 16u;
    return XXBYTE_STATUS_OK;
}

/* ---- methods ---- */

/* The TypeDef row whose MethodList range holds list position pos. */
static uint32_t owner_of_position(const xxbyte_dotnet_image *image, uint32_t list_column, uint32_t pos)
{
    uint32_t lo = 1, hi = image->rows[XXBYTE_DOTNET_TABLE_TYPEDEF], found = 0;

    /* The last TypeDef whose list starts at or before pos; lists are
     * ascending in a well-formed file. */
    while (lo <= hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t start = xxbyte_dotnet_column(image, XXBYTE_DOTNET_TABLE_TYPEDEF, mid, list_column);
        if (start <= pos) {
            found = mid;
            lo = mid + 1u;
        } else {
            hi = mid - 1u;
        }
    }
    /* Empty ranges share a start: take the last type with that start. */
    return found;
}

xxbyte_status xxbyte_dotnet_read_method(const xxbyte_dotnet_image *image, uint32_t row, xxbyte_dotnet_method *method, int find_owner)
{
    uint32_t pos = row;
    size_t offset, avail;
    xxbyte_status status;

    if (method == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(method, 0, sizeof(*method));
    if (image == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (row == 0 || row > image->rows[XXBYTE_DOTNET_TABLE_METHODDEF]) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    method->token = XXBYTE_DOTNET_TOKEN(XXBYTE_DOTNET_TABLE_METHODDEF, row);
    method->rva = xxbyte_dotnet_column(image, XXBYTE_DOTNET_TABLE_METHODDEF, row, 0);
    method->impl_flags = (uint16_t)xxbyte_dotnet_column(image, XXBYTE_DOTNET_TABLE_METHODDEF, row, 1);
    method->flags = (uint16_t)xxbyte_dotnet_column(image, XXBYTE_DOTNET_TABLE_METHODDEF, row, 2);
    method->name = xxbyte_dotnet_column(image, XXBYTE_DOTNET_TABLE_METHODDEF, row, 3);
    method->signature = xxbyte_dotnet_column(image, XXBYTE_DOTNET_TABLE_METHODDEF, row, 4);
    method->param_list = xxbyte_dotnet_column(image, XXBYTE_DOTNET_TABLE_METHODDEF, row, 5);
    if (!find_owner) {
        pos = 0;
    } else if (image->rows[XXBYTE_DOTNET_TABLE_METHODPTR] != 0) {
        uint32_t i;
        pos = 0;
        for (i = 1; i <= image->rows[XXBYTE_DOTNET_TABLE_METHODPTR]; ++i) {
            if (xxbyte_dotnet_column(image, XXBYTE_DOTNET_TABLE_METHODPTR, i, 0) == row) {
                pos = i;
                break;
            }
        }
    }
    if (pos != 0) {
        uint32_t owner = owner_of_position(image, 5, pos);
        if (owner != 0) {
            method->type_token = XXBYTE_DOTNET_TOKEN(XXBYTE_DOTNET_TABLE_TYPEDEF, owner);
        }
    }
    /* CodeTypeMask: 0 IL, 1 native, 3 runtime; abstract (0x0400) and
     * pinvokeimpl (0x2000) methods have no body. */
    if (method->rva == 0 || (method->impl_flags & 0x0003u) != 0 || (method->flags & 0x2400u) != 0) {
        method->body_status = XXBYTE_STATUS_END_OF_INPUT;
        return XXBYTE_STATUS_OK;
    }
    if (!xxbyte_dotnet_rva_to_offset(image, method->rva, &offset, &avail)) {
        method->body_status = XXBYTE_STATUS_TRUNCATED;
        return XXBYTE_STATUS_OK;
    }
    method->body_offset = offset;
    method->available = avail;
    status = xxbyte_dotnet_parse_method_header(image->data + offset, avail, &method->header);
    method->body_status = status;
    if (status == XXBYTE_STATUS_OK) {
        uint64_t end = (uint64_t)method->header.header_size + method->header.code_size;
        if (method->header.has_more_sections) {
            /* Walk the section chain for its total size. */
            uint64_t at = end;
            for (;;) {
                uint32_t data_size;
                const uint8_t *sect;
                at = (at + 3u) & ~(uint64_t)3u;
                if (at + 4u > avail) {
                    method->body_status = XXBYTE_STATUS_TRUNCATED;
                    break;
                }
                sect = image->data + offset + at;
                data_size = (sect[0] & XXBYTE_DOTNET_SECT_FAT_FORMAT) ? ((uint32_t)sect[1] | ((uint32_t)sect[2] << 8) | ((uint32_t)sect[3] << 16)) : sect[1];
                if (data_size < 4) {
                    method->body_status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                    break;
                }
                if (at + data_size > avail) {
                    method->body_status = XXBYTE_STATUS_TRUNCATED;
                    break;
                }
                at += data_size;
                if ((sect[0] & XXBYTE_DOTNET_SECT_MORE_SECTS) == 0) {
                    break;
                }
            }
            end = at;
        }
        method->body_size = (size_t)end;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_dotnet_get_method(const xxbyte_dotnet_image *image, uint32_t row, xxbyte_dotnet_method *method)
{
    return xxbyte_dotnet_read_method(image, row, method, 1);
}

#else
typedef int xxbyte_dotnet_meta_disabled;
#endif
