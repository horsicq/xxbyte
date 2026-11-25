#include "xxbyte_nsis_internal.h"

#if XXBYTE_USE_NSIS

#define FH_SIGNATURE 0xDEADBEEFu
#define FH_FLAGS_MASK 0xFu
#define FH_ALIGN 512u
#define BLOCKS XXBYTE_NSIS_BLOCK_COUNT
#define ENTRY_SIZE XXBYTE_NSIS_ENTRY_SIZE

/* What follows the firstheader must hold this much for the storage check:
 * size, filter flag, 5 LZMA property bytes and 2 stream bytes (7-Zip). */
#define SIGNATURE_SIZE 12u

xxbyte_status xxbyte_nsis_parse_first_header(const uint8_t *data, size_t size, xxbyte_nsis_first_header *header)
{
    static const uint8_t magic[12] = {'N', 'u', 'l', 'l', 's', 'o', 'f', 't', 'I', 'n', 's', 't'};
    uint32_t flags;

    if (header == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(header, 0, sizeof(*header));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < XXBYTE_NSIS_FIRST_HEADER_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    flags = xxbyte_u32le(data);
    if ((flags & ~FH_FLAGS_MASK) != 0 || xxbyte_u32le(data + 4) != FH_SIGNATURE || memcmp(data + 8, magic, sizeof(magic)) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    header->flags = flags;
    header->header_size = xxbyte_u32le(data + 20);
    header->archive_size = xxbyte_u32le(data + 24);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_nsis_find_first_header(const uint8_t *data, size_t size, uint64_t *offset)
{
    size_t position;

    if (offset == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *offset = 0;
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    for (position = 0; size >= XXBYTE_NSIS_FIRST_HEADER_SIZE && position <= size - XXBYTE_NSIS_FIRST_HEADER_SIZE; position += FH_ALIGN) {
        xxbyte_nsis_first_header header;
        if (xxbyte_nsis_parse_first_header(data + position, size - position, &header) == XXBYTE_STATUS_OK) {
            *offset = position;
            return XXBYTE_STATUS_OK;
        }
        if (size - position < FH_ALIGN) {
            break;
        }
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

/* LZMA properties as NSIS writes them: lc=3 lp=0 pb=2 and a dictionary
 * size below 2^31 (7-Zip's IsLZMA). */
static int is_lzma(const uint8_t *p)
{
    return p[0] == 0x5D && p[1] == 0 && p[2] == 0 && p[5] == 0 && (p[6] & 0x80) == 0;
}

/* Optionally preceded by a filter flag byte (0 or 1). */
static int is_lzma_filtered(const uint8_t *p, uint32_t *filter)
{
    if (is_lzma(p)) {
        *filter = 0;
        return 1;
    }
    if (p[0] <= 1 && is_lzma(p + 1)) {
        *filter = 1;
        return 1;
    }
    return 0;
}

/* NSIS bzip2 streams start with the block size digit '1' and a block. */
static int is_bzip2(const uint8_t *p)
{
    return p[0] == 0x31 && p[1] < 14;
}

xxbyte_status xxbyte_nsis_locate_header(const uint8_t *data, size_t size, xxbyte_nsis_header_location *location)
{
    xxbyte_nsis_first_header first;
    xxbyte_status status;
    const uint8_t *sig;
    uint32_t value;
    uint32_t filter = 0;
    const uint64_t after = XXBYTE_NSIS_FIRST_HEADER_SIZE;

    if (location == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(location, 0, sizeof(*location));
    status = xxbyte_nsis_parse_first_header(data, size, &first);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if ((uint64_t)size < after + SIGNATURE_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    sig = data + after;
    value = xxbyte_u32le(sig);
    if (value == first.header_size) {
        location->storage = XXBYTE_NSIS_STORED;
        location->method = XXBYTE_NSIS_METHOD_COPY;
        location->data_offset = after + 4;
        location->data_size = value;
    } else if (is_lzma_filtered(sig, &filter)) {
        location->storage = XXBYTE_NSIS_SOLID;
        location->method = XXBYTE_NSIS_METHOD_LZMA;
        location->filter = filter;
        location->data_offset = after;
    } else if (sig[3] == 0x80) {
        location->storage = XXBYTE_NSIS_COMPRESSED;
        location->data_offset = after + 4;
        location->data_size = value & 0x7FFFFFFFu;
        if (is_lzma_filtered(sig + 4, &filter)) {
            location->method = XXBYTE_NSIS_METHOD_LZMA;
            location->filter = filter;
        } else if (is_bzip2(sig + 4)) {
            location->method = XXBYTE_NSIS_METHOD_BZIP2;
        } else {
            location->method = XXBYTE_NSIS_METHOD_DEFLATE;
        }
    } else {
        location->storage = XXBYTE_NSIS_SOLID;
        location->method = is_bzip2(sig) ? XXBYTE_NSIS_METHOD_BZIP2 : XXBYTE_NSIS_METHOD_DEFLATE;
        location->data_offset = after;
    }
    if (location->storage != XXBYTE_NSIS_SOLID && location->data_offset + location->data_size > (uint64_t)size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    return XXBYTE_STATUS_OK;
}

/* Reads a block table of records (7 or 8) records with offsets of
 * pointer_size bytes and checks that the layout is consistent; returns 1
 * and fills *header when it is. */
static int try_layout(const uint8_t *data, size_t size, uint32_t records, uint32_t pointer_size, xxbyte_nsis_header *header)
{
    const uint64_t record = pointer_size + 4u;
    const uint64_t table_end = 4u + records * record;
    xxbyte_nsis_header parsed;
    uint64_t previous;
    uint64_t strings_size;
    uint32_t i;

    if ((uint64_t)size < table_end) {
        return 0;
    }
    memset(&parsed, 0, sizeof(parsed));
    parsed.flags = xxbyte_u32le(data);
    parsed.pointer_size = pointer_size;
    for (i = 0; i < records; ++i) {
        const uint8_t *p = data + 4 + i * record;
        /* NSIS 2.0 has no BGFONT record: its 7th record is DATA. */
        uint32_t block = records < BLOCKS && i >= XXBYTE_NSIS_BLOCK_BGFONT ? i + 1 : i;
        if (pointer_size == 8) {
            parsed.blocks[block].offset = xxbyte_u64le(p);
        } else {
            parsed.blocks[block].offset = xxbyte_u32le(p);
        }
        parsed.blocks[block].count = xxbyte_u32le(p + pointer_size);
    }
    /* Pages to ctlcolors follow the header in order. */
    previous = table_end;
    for (i = XXBYTE_NSIS_BLOCK_PAGES; i <= XXBYTE_NSIS_BLOCK_CTLCOLORS; ++i) {
        if (parsed.blocks[i].offset < previous || parsed.blocks[i].offset > size) {
            return 0;
        }
        previous = parsed.blocks[i].offset;
    }
    if (parsed.blocks[XXBYTE_NSIS_BLOCK_BGFONT].offset != 0 &&
        (parsed.blocks[XXBYTE_NSIS_BLOCK_BGFONT].offset < previous || parsed.blocks[XXBYTE_NSIS_BLOCK_BGFONT].offset > size)) {
        return 0;
    }
    if (parsed.blocks[XXBYTE_NSIS_BLOCK_ENTRIES].offset + (uint64_t)parsed.blocks[XXBYTE_NSIS_BLOCK_ENTRIES].count * ENTRY_SIZE >
        parsed.blocks[XXBYTE_NSIS_BLOCK_STRINGS].offset) {
        return 0;
    }
    strings_size = parsed.blocks[XXBYTE_NSIS_BLOCK_LANGTABLES].offset - parsed.blocks[XXBYTE_NSIS_BLOCK_STRINGS].offset;
    if (strings_size < 2 || data[parsed.blocks[XXBYTE_NSIS_BLOCK_STRINGS].offset + strings_size - 1] != 0) {
        return 0;
    }
    parsed.entry_count = parsed.blocks[XXBYTE_NSIS_BLOCK_ENTRIES].count;
    parsed.entries_offset = (uint32_t)parsed.blocks[XXBYTE_NSIS_BLOCK_ENTRIES].offset;
    parsed.strings_offset = (uint32_t)parsed.blocks[XXBYTE_NSIS_BLOCK_STRINGS].offset;
    parsed.strings_size = (uint32_t)strings_size;
    /* A Unicode table starts with an empty UTF-16 string and ends with a
     * UTF-16 NUL. */
    if (xxbyte_u16le(data + parsed.strings_offset) == 0 && (strings_size & 1u) == 0 && data[parsed.strings_offset + strings_size - 2] == 0) {
        parsed.unicode = 1;
    }
    *header = parsed;
    return 1;
}

xxbyte_status xxbyte_nsis_parse_header_ex(const uint8_t *data, size_t size, uint32_t options, xxbyte_nsis_header *header)
{
    uint32_t records = (options & XXBYTE_NSIS_HEADER_7_BLOCKS) != 0 ? BLOCKS - 1u : BLOCKS;

    if (header == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(header, 0, sizeof(*header));
    if (data == NULL || (options & ~XXBYTE_NSIS_HEADER_7_BLOCKS) != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    /* Header sizes stay well below 4 GiB: offsets are 32-bit values. */
    if (size > UINT32_MAX) {
        size = UINT32_MAX;
    }
    if (size < 4u + records * 8u) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (try_layout(data, size, records, 4, header) || try_layout(data, size, records, 8, header)) {
        return XXBYTE_STATUS_OK;
    }
    memset(header, 0, sizeof(*header));
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

xxbyte_status xxbyte_nsis_parse_header(const uint8_t *data, size_t size, xxbyte_nsis_header *header)
{
    return xxbyte_nsis_parse_header_ex(data, size, 0, header);
}

/* Checks that *header describes a string table inside size bytes. */
static int strings_fit(const xxbyte_nsis_header *header, size_t size)
{
    return header->strings_size != 0 && (uint64_t)header->strings_offset + header->strings_size <= (uint64_t)size &&
           (!header->unicode || (header->strings_size & 1u) == 0);
}

xxbyte_status xxbyte_nsis_string_at(const uint8_t *data, size_t size, const xxbyte_nsis_header *header, uint32_t offset, uint64_t *string_offset, uint32_t *length)
{
    const uint8_t *table;
    uint64_t unit;
    uint64_t start;
    uint64_t at;

    if (string_offset != NULL) {
        *string_offset = 0;
    }
    if (length != NULL) {
        *length = 0;
    }
    if (data == NULL || header == NULL || string_offset == NULL || length == NULL || !strings_fit(header, size)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    table = data + header->strings_offset;
    unit = header->unicode ? 2u : 1u;
    start = (uint64_t)offset * unit;
    if (start >= header->strings_size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    for (at = start; at + unit <= header->strings_size; at += unit) {
        if (table[at] == 0 && (unit == 1 || table[at + 1] == 0)) {
            *string_offset = header->strings_offset + start;
            *length = (uint32_t)((at - start) / unit);
            return XXBYTE_STATUS_OK;
        }
    }
    return XXBYTE_STATUS_TRUNCATED;
}

/* Escape-code evidence in the string table. */
enum {
    CODES_NONE = 0,
    CODES_NSIS3 = 1, /* 1..4, numbers with 0x80 bits (CODE_SHORT) */
    CODES_NSIS2 = 2, /* 252..255 in ANSI tables */
    CODES_PARK = 4   /* 0xE000..0xE003 in Unicode tables */
};

static uint32_t scan_codes(const uint8_t *table, uint32_t size, int unicode)
{
    uint32_t found = CODES_NONE;
    uint32_t i;

    if (unicode) {
        for (i = 0; i + 4 <= size; i += 2) {
            uint32_t c = xxbyte_u16le(table + i);
            uint32_t next = xxbyte_u16le(table + i + 2);
            if (c >= 1 && c <= 3 && (next & 0x8080u) == 0x8080u) {
                found |= CODES_NSIS3;
            } else if (c >= 0xE000u && c <= 0xE003u && next != 0) {
                found |= CODES_PARK;
            }
        }
    } else {
        for (i = 0; i + 3 <= size; ++i) {
            uint32_t c = table[i];
            if (c >= 1 && c <= 3 && (table[i + 1] & 0x80) != 0 && (table[i + 2] & 0x80) != 0) {
                found |= CODES_NSIS3;
            } else if (c >= 253 && table[i + 1] != 0 && table[i + 2] != 0) {
                found |= CODES_NSIS2;
            }
        }
    }
    return found;
}

/* Entries that do not fit mode: undefined opcodes and parameters past what
 * makensis writes for the opcode. */
static uint64_t count_misfits(const uint8_t *entries, uint32_t count, uint32_t mode)
{
    uint64_t misfits = 0;
    uint32_t i;

    for (i = 0; i < count; ++i) {
        const uint8_t *entry = entries + (size_t)i * ENTRY_SIZE;
        uint32_t id = xxbyte_nsis_opcode_id(mode, xxbyte_u32le(entry));
        uint32_t used = 6;

        while (used > 0 && xxbyte_u32le(entry + 4 * used) == 0) {
            --used;
        }
        if (id == 0 || used > xxbyte_nsis_param_limit(id) || (id == XXBYTE_NSIS_ID_EW_FINDPROC && used == 0)) {
            ++misfits;
        }
    }
    return misfits;
}

/* The NSIS 3 release, from entries only one of them writes. */
static uint32_t nsis3_variant(const uint8_t *entries, uint32_t count, uint32_t flags)
{
    uint32_t mode = XXBYTE_NSIS_MODE(XXBYTE_NSIS_V306, flags);
    uint32_t image = xxbyte_nsis_id_opcode(mode, XXBYTE_NSIS_ID_EW_LOADANDSETIMAGE);
    uint32_t osinfo = xxbyte_nsis_id_opcode(mode, XXBYTE_NSIS_ID_EW_GETOSINFO);
    int v306_image = 0;
    int v305_image = 0;
    int v300_image = 0;
    uint32_t i;

    for (i = 0; i < count; ++i) {
        const uint8_t *entry = entries + (size_t)i * ENTRY_SIZE;
        uint32_t which = xxbyte_u32le(entry);
        if (which == osinfo) {
            return XXBYTE_NSIS_V306;
        }
        if (which == image) {
            /* Entry 37: 3.06+ LoadAndSetImage keeps its flags in parameter
             * 3, 3.05's in parameter 2; the SetBrandingImage of 3.04 and
             * before has a 0 or 1 resize flag there. */
            if (xxbyte_u32le(entry + 16) != 0) {
                v306_image = 1;
            } else if (xxbyte_u32le(entry + 12) > 1) {
                v305_image = 1;
            } else {
                v300_image = 1;
            }
        }
    }
    if (v306_image) {
        return XXBYTE_NSIS_V306;
    }
    if (v305_image) {
        return XXBYTE_NSIS_V305;
    }
    return v300_image ? XXBYTE_NSIS_V300 : XXBYTE_NSIS_V306;
}

xxbyte_status xxbyte_nsis_guess_mode(const uint8_t *data, size_t size, const xxbyte_nsis_header *header, uint32_t *mode)
{
    static const uint8_t park_variants[] = {
        XXBYTE_NSIS_PARK1,
        XXBYTE_NSIS_PARK2,
        XXBYTE_NSIS_PARK3,
    };
    uint32_t candidates[16];
    size_t candidate_count = 0;
    const uint8_t *entries;
    uint32_t codes;
    uint32_t ansi;
    uint32_t best = 0;
    uint64_t best_misfits = UINT64_MAX;
    size_t i;
    size_t k;

    if (mode != NULL) {
        *mode = 0;
    }
    if (data == NULL || header == NULL || mode == NULL || !strings_fit(header, size) ||
        (uint64_t)header->entries_offset + (uint64_t)header->entry_count * ENTRY_SIZE > (uint64_t)size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    entries = data + header->entries_offset;
    ansi = header->unicode ? 0 : XXBYTE_NSIS_MODE_ANSI;
    codes = scan_codes(data + header->strings_offset, header->strings_size, header->unicode != 0);

    if ((codes & CODES_NSIS3) != 0 || (header->unicode && (codes & CODES_PARK) == 0)) {
        candidates[candidate_count++] = XXBYTE_NSIS_MODE(XXBYTE_NSIS_V306, ansi);
        candidates[candidate_count++] = XXBYTE_NSIS_MODE(XXBYTE_NSIS_V306, ansi | XXBYTE_NSIS_MODE_LOG);
    } else {
        if (!header->unicode) {
            candidates[candidate_count++] = XXBYTE_NSIS_MODE(XXBYTE_NSIS_V226, ansi);
            candidates[candidate_count++] = XXBYTE_NSIS_MODE(XXBYTE_NSIS_V226, ansi | XXBYTE_NSIS_MODE_LOG);
        }
        for (k = 0; k < 2; ++k) {
            for (i = 0; i < sizeof(park_variants); ++i) {
                candidates[candidate_count++] = XXBYTE_NSIS_MODE(park_variants[i], ansi | (k != 0 ? XXBYTE_NSIS_MODE_LOG : 0u));
            }
        }
    }
    for (i = 0; i < candidate_count; ++i) {
        uint64_t misfits = count_misfits(entries, header->entry_count, candidates[i]);
        if (misfits < best_misfits) {
            best_misfits = misfits;
            best = candidates[i];
        }
    }
    if (XXBYTE_NSIS_MODE_VARIANT(best) == XXBYTE_NSIS_V306) {
        uint32_t flags = best & (XXBYTE_NSIS_MODE_ANSI | XXBYTE_NSIS_MODE_LOG);
        best = XXBYTE_NSIS_MODE(nsis3_variant(entries, header->entry_count, flags), flags);
    }
    *mode = best;
    return XXBYTE_STATUS_OK;
}

#else
typedef int xxbyte_nsis_file_disabled;
#endif
