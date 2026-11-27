/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_nsis_internal.h"

#if XXBYTE_USE_NSIS

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#define ENTRY_SIZE XXBYTE_NSIS_ENTRY_SIZE
#define TEXT_SIZE 512u

#define TRY_OUT(expr)                           \
    do {                                        \
        if (!(expr)) {                          \
            return XXBYTE_STATUS_OUT_OF_MEMORY; \
        }                                       \
    } while (0)

#define TRY(expr)                              \
    do {                                       \
        xxbyte_status try_status_ = (expr);    \
        if (try_status_ != XXBYTE_STATUS_OK) { \
            return try_status_;                \
        }                                      \
    } while (0)

static const char *variant_name(uint32_t variant)
{
    static const char *const names[XXBYTE_NSIS_VARIANT_COUNT] = {
        "V306 (NSIS 3.06+)",
        "V305 (NSIS 3.05)",
        "V300 (NSIS 3.00-3.04)",
        "V226 (NSIS 2.26-2.51)",
        "V204 (NSIS 2.04-2.25)",
        "V202 (NSIS 2.02-2.03)",
        "V200 (NSIS 2.0)",
        "PARK1 (Unicode NSIS before 2.46.2)",
        "PARK2 (Unicode NSIS 2.46.2)",
        "PARK3 (Unicode NSIS 2.46.3+)",
        "V201 (NSIS 2.01)",
    };

    return variant < XXBYTE_NSIS_VARIANT_COUNT ? names[variant] : "?";
}

static const char *page_name(int32_t wndproc)
{
    static const char *const names[] = {"license", "components", "directory", "instfiles", "uninstConfirm", "completed", "custom"};

    return wndproc >= 0 && (size_t)wndproc < sizeof(names) / sizeof(names[0]) ? names[wndproc] : NULL;
}

static const char *root_key_name(uint32_t root)
{
    switch (root & ~0x60000000u) {
        case 0: return "SHCTX";
        case 0x80000000u: return "HKCR";
        case 0x80000001u: return "HKCU";
        case 0x80000002u: return "HKLM";
        case 0x80000003u: return "HKU";
        case 0x80000004u: return "HKPD";
        case 0x80000005u: return "HKCC";
        case 0x80000006u: return "HKDD";
        default: return NULL;
    }
}

/* Appends the quoted string parameter. */
static int append_string(xx_buf_t *out, const xxbyte_nsis_script *script, int32_t parameter)
{
    char text[TEXT_SIZE];
    size_t length = 0;
    xxbyte_status status = xxbyte_nsis_script_string(script, parameter, XXBYTE_NSIS_STRING_QUOTED, text, sizeof(text), &length);

    if (status == XXBYTE_STATUS_INVALID_ARGUMENT) {
        return xx_buf_appendf(out, "string@0x%X", (unsigned)parameter) ? 1 : 0;
    }
    if (!xx_buf_append_str(out, text)) {
        return 0;
    }
    if (length >= sizeof(text) && !xx_buf_append_str(out, "...")) {
        return 0;
    }
    return 1;
}

static int append_label(xx_buf_t *out, const xxbyte_nsis_script *script, int32_t entry)
{
    char text[64];

    if (entry < 0) {
        return 1;
    }
    xxbyte_nsis_script_label(script, (uint32_t)entry, text, sizeof(text));
    return xx_buf_append_str(out, text) ? 1 : 0;
}

/* License data is raw text (not escape coded): a format marker (1 text, 2
 * RTF, 0x10 more for UTF-16) and the file's contents. */
static int append_license(xx_buf_t *out, const xxbyte_nsis_script *script, int32_t parameter)
{
    uint64_t at = 0;
    uint32_t length = 0;
    uint32_t marker;

    if (parameter < 0) {
        return xx_buf_appendf(out, "$(LSTR_%u)", (unsigned)(-(int64_t)parameter - 1)) ? 1 : 0;
    }
    if (xxbyte_nsis_string_at(script->data, script->size, &script->header, (uint32_t)parameter, &at, &length) != XXBYTE_STATUS_OK || length == 0) {
        return xx_buf_appendf(out, "string@0x%X", (unsigned)parameter) ? 1 : 0;
    }
    marker = script->header.unicode ? xxbyte_u16le(script->data + at) : script->data[at];
    return xx_buf_appendf(out, "string@0x%X (%s, %u characters)", (unsigned)parameter, (marker & 0xFu) == 2 ? "RTF" : "text", length - 1) ? 1 : 0;
}

/* Nonzero when language string id is the data of a license page. */
static int is_license_lang(const xxbyte_nsis_script *script, uint32_t id)
{
    uint32_t i;

    for (i = 0; i < script->page_count; ++i) {
        xxbyte_nsis_page page;
        if (xxbyte_nsis_script_page(script, i, &page) == XXBYTE_STATUS_OK && page.wndproc_id == XXBYTE_NSIS_PAGE_LICENSE && page.params[1] < 0 &&
            (uint32_t)(-(int64_t)page.params[1] - 1) == id) {
            return 1;
        }
    }
    return 0;
}

static xxbyte_status list_summary(const xxbyte_nsis_script *script, xx_buf_t *out)
{
    static const char *const block_names[XXBYTE_NSIS_BLOCK_COUNT] = {"pages", "sections", "entries", "strings", "langtables", "ctlcolors", "bgfont", "data"};
    const xxbyte_nsis_header *h = &script->header;
    uint32_t variant = XXBYTE_NSIS_MODE_VARIANT(script->mode);
    uint32_t i;

    TRY_OUT(xx_buf_appendf(out, "; NSIS header: %llu bytes, %s, %u-bit block offsets, flags 0x%X\n", (unsigned long long)script->size, h->unicode ? "Unicode" : "ANSI",
                           h->pointer_size * 8u, h->flags));
    TRY_OUT(xx_buf_appendf(out, "; mode: 0x%03X %s%s%s\n", script->mode, variant_name(variant), (script->mode & XXBYTE_NSIS_MODE_ANSI) != 0 ? " ANSI" : "",
                           (script->mode & XXBYTE_NSIS_MODE_LOG) != 0 ? " LOG" : ""));
    TRY_OUT(xx_buf_append_str(out, "; blocks:"));
    for (i = 0; i < XXBYTE_NSIS_BLOCK_COUNT; ++i) {
        if (i == XXBYTE_NSIS_BLOCK_STRINGS) {
            TRY_OUT(xx_buf_appendf(out, " strings %u bytes @0x%llX,", h->strings_size, (unsigned long long)h->blocks[i].offset));
        } else {
            TRY_OUT(xx_buf_appendf(out, " %s %u @0x%llX%s", block_names[i], h->blocks[i].count, (unsigned long long)h->blocks[i].offset,
                                   i + 1 < XXBYTE_NSIS_BLOCK_COUNT ? "," : "\n"));
        }
    }
    if (script->install_directory > 0) {
        TRY_OUT(xx_buf_append_str(out, "; InstallDir "));
        TRY_OUT(append_string(out, script, script->install_directory));
        TRY_OUT(xx_buf_append_char(out, '\n'));
    }
    if (script->install_reg_key > 0) {
        const char *root = root_key_name(script->install_reg_root);
        TRY_OUT(xx_buf_append_str(out, "; InstallDirRegKey "));
        if (root != NULL) {
            TRY_OUT(xx_buf_append_str(out, root));
        } else {
            TRY_OUT(xx_buf_appendf(out, "0x%X", script->install_reg_root));
        }
        TRY_OUT(xx_buf_append_char(out, ' '));
        TRY_OUT(append_string(out, script, script->install_reg_key));
        TRY_OUT(xx_buf_append_char(out, ' '));
        TRY_OUT(append_string(out, script, script->install_reg_value));
        TRY_OUT(xx_buf_append_char(out, '\n'));
    }
    for (i = 0; i < script->page_count; ++i) {
        xxbyte_nsis_page page;
        const char *name;
        if (xxbyte_nsis_script_page(script, i, &page) != XXBYTE_STATUS_OK) {
            continue;
        }
        name = page_name(page.wndproc_id);
        if (name != NULL) {
            TRY_OUT(xx_buf_appendf(out, "; page %u: %s", i, name));
        } else {
            TRY_OUT(xx_buf_appendf(out, "; page %u: wndproc %d", i, page.wndproc_id));
        }
        if (page.pre_function >= 0) {
            TRY_OUT(xx_buf_append_str(out, ", pre "));
            TRY_OUT(append_label(out, script, page.pre_function));
        }
        if (page.show_function >= 0) {
            TRY_OUT(xx_buf_append_str(out, ", show "));
            TRY_OUT(append_label(out, script, page.show_function));
        }
        if (page.leave_function >= 0) {
            TRY_OUT(xx_buf_append_str(out, ", leave "));
            TRY_OUT(append_label(out, script, page.leave_function));
        }
        if (page.wndproc_id == XXBYTE_NSIS_PAGE_LICENSE && page.params[1] != 0) {
            TRY_OUT(xx_buf_append_str(out, ", license data "));
            TRY_OUT(append_license(out, script, page.params[1]));
        }
        TRY_OUT(xx_buf_append_char(out, '\n'));
    }
    if (script->langtable_count != 0) {
        const uint8_t *table = script->data + h->blocks[XXBYTE_NSIS_BLOCK_LANGTABLES].offset;
        TRY_OUT(xx_buf_appendf(out, "; language tables: %u, %u strings each; languages", script->langtable_count, script->lang_string_count));
        for (i = 0; i < script->langtable_count; ++i) {
            TRY_OUT(xx_buf_appendf(out, " %u", (unsigned)xxbyte_u16le(table + (uint64_t)i * script->langtable_size)));
        }
        TRY_OUT(xx_buf_appendf(out, "; strings of %u:\n", (unsigned)xxbyte_u16le(table + (uint64_t)script->main_langtable * script->langtable_size)));
        for (i = 0; i < script->lang_string_count; ++i) {
            int32_t string = 0;
            if (xxbyte_nsis_script_lang_string(script, script->main_langtable, i, &string) != XXBYTE_STATUS_OK || string == 0) {
                continue;
            }
            if (is_license_lang(script, i)) {
                TRY_OUT(xx_buf_appendf(out, ";   LSTR_%u ", i));
                TRY_OUT(append_license(out, script, string));
                TRY_OUT(xx_buf_append_char(out, '\n'));
                continue;
            }
            TRY_OUT(xx_buf_appendf(out, ";   LSTR_%u ", i));
            TRY_OUT(append_string(out, script, string));
            TRY_OUT(xx_buf_append_char(out, '\n'));
        }
    }
    return XXBYTE_STATUS_OK;
}

/* Lists entries [first, end) of one body, with label lines. */
static xxbyte_status list_body(const xxbyte_nsis_script *script, uint32_t first, uint32_t end, uint32_t listing_flags, xx_buf_t *out)
{
    const uint8_t *entries = script->data + script->header.entries_offset;
    uint32_t start = first;
    uint32_t i;

    for (i = first; i <= end; ++i) {
        if (i < end && (i == first || (script->marks[i] & XXBYTE_NSIS_MARK_LABEL) == 0)) {
            continue;
        }
        if (i > start) {
            TRY(xxbyte_disasm_buf_ex(XXBYTE_FAMILY_NSIS, script->mode, &script->context, entries + (size_t)start * ENTRY_SIZE, (size_t)(i - start) * ENTRY_SIZE,
                                     script->context.entries_address + (uint64_t)start * ENTRY_SIZE, listing_flags, xxbyte_nsis_resolver, (void *)script, out));
        }
        if (i < end) {
            TRY_OUT(append_label(out, script, (int32_t)i));
            TRY_OUT(xx_buf_append_str(out, ":\n"));
        }
        start = i;
    }
    return XXBYTE_STATUS_OK;
}

/* Prints the header line of section record index. */
static int section_line(const xxbyte_nsis_script *script, uint32_t index, const xxbyte_nsis_section *section, uint32_t first, uint32_t end, xx_buf_t *out)
{
    if ((section->flags & XXBYTE_NSIS_SF_SECGRPEND) != 0) {
        if (!xx_buf_appendf(out, "SectionGroupEnd ; section %u", index)) {
            return 0;
        }
    } else {
        int group = (section->flags & XXBYTE_NSIS_SF_SECGRP) != 0;
        const char *option = "";
        if (group && (section->flags & XXBYTE_NSIS_SF_EXPAND) != 0) {
            option = "/e ";
        } else if (!group && (section->flags & XXBYTE_NSIS_SF_SELECTED) == 0) {
            option = "/o ";
        }
        if (!xx_buf_appendf(out, "%s %s", group ? "SectionGroup" : "Section", option) || !append_string(out, script, section->name) ||
            !xx_buf_appendf(out, " ; section %u", index)) {
            return 0;
        }
    }
    if (end > first && !xx_buf_appendf(out, ", entries %u-%u", first, end - 1)) {
        return 0;
    }
    return xx_buf_append_char(out, '\n') ? 1 : 0;
}

/* Prints the section records from *next up to (not including) limit that
 * have no code of their own to list under. */
static xxbyte_status list_records(const xxbyte_nsis_script *script, uint32_t *next, uint32_t limit, xx_buf_t *out)
{
    for (; *next < limit; ++*next) {
        xxbyte_nsis_section section;
        if (xxbyte_nsis_script_section(script, *next, &section) == XXBYTE_STATUS_OK) {
            TRY_OUT(section_line(script, *next, &section, 0, 0, out));
            TRY_OUT(xx_buf_append_char(out, '\n'));
        }
    }
    return XXBYTE_STATUS_OK;
}

/* The first section record (from from on) whose code starts at entry, or
 * section_count. */
static uint32_t section_at(const xxbyte_nsis_script *script, uint32_t from, uint32_t entry, xxbyte_nsis_section *found)
{
    uint32_t i;

    for (i = from; i < script->section_count; ++i) {
        if (xxbyte_nsis_script_section(script, i, found) == XXBYTE_STATUS_OK && found->code_size >= 0 && found->code >= 0 && (uint32_t)found->code == entry) {
            return i;
        }
    }
    return script->section_count;
}

static xxbyte_status list_entries(const xxbyte_nsis_script *script, uint32_t listing_flags, xx_buf_t *out)
{
    uint32_t count = script->header.entry_count;
    uint32_t next_record = 0;
    uint32_t i = 0;

    while (i < count) {
        uint8_t m = script->marks[i];
        uint32_t end = i + 1;
        uint32_t index = script->section_count;
        xxbyte_nsis_section section = {0};

        while (end < count && (script->marks[end] & (XXBYTE_NSIS_MARK_FUNCTION | XXBYTE_NSIS_MARK_SECTION)) == 0) {
            ++end;
        }
        if ((m & XXBYTE_NSIS_MARK_SECTION) != 0) {
            index = section_at(script, next_record, i, &section);
            if (index == script->section_count) {
                index = section_at(script, 0, i, &section);
            }
        }
        if (index < script->section_count) {
            int group = (section.flags & (XXBYTE_NSIS_SF_SECGRP | XXBYTE_NSIS_SF_SECGRPEND)) != 0;
            if (index >= next_record) {
                TRY(list_records(script, &next_record, index, out));
                next_record = index + 1;
            }
            TRY_OUT(section_line(script, index, &section, i, end, out));
            TRY(list_body(script, i, end, listing_flags, out));
            TRY_OUT(xx_buf_append_str(out, group ? "\n" : "SectionEnd\n\n"));
        } else if ((m & XXBYTE_NSIS_MARK_FUNCTION) != 0) {
            TRY_OUT(xx_buf_append_str(out, "Function "));
            TRY_OUT(append_label(out, script, (int32_t)i));
            TRY_OUT(xx_buf_appendf(out, " ; entries %u-%u\n", i, end - 1));
            TRY(list_body(script, i, end, listing_flags, out));
            TRY_OUT(xx_buf_append_str(out, "FunctionEnd\n\n"));
        } else {
            TRY_OUT(xx_buf_appendf(out, "; entries %u-%u\n", i, end - 1));
            TRY(list_body(script, i, end, listing_flags, out));
            TRY_OUT(xx_buf_append_char(out, '\n'));
        }
        i = end;
    }
    return list_records(script, &next_record, script->section_count, out);
}

xxbyte_status xxbyte_nsis_list_header(const uint8_t *header, size_t size, uint32_t mode, uint32_t options, uint32_t listing_flags, struct xx_buf_s *out)
{
    xxbyte_nsis_script script;
    xxbyte_status status;

    if (out == NULL || header == NULL || (listing_flags & ~(uint32_t)XXBYTE_LISTING_KNOWN_FLAGS) != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = xxbyte_nsis_script_init(&script, header, size, mode, options);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    status = list_summary(&script, out);
    if (status == XXBYTE_STATUS_OK) {
        TRY_OUT(xx_buf_append_char(out, '\n'));
        status = list_entries(&script, listing_flags, out);
    }
    xxbyte_nsis_script_free(&script);
    if (status == XXBYTE_STATUS_OK && !xx_buf_ok(out)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return status;
}

static const char *method_text(uint32_t method)
{
    switch (method) {
        case XXBYTE_NSIS_METHOD_COPY: return "stored";
        case XXBYTE_NSIS_METHOD_DEFLATE: return "zlib (raw deflate)";
        case XXBYTE_NSIS_METHOD_BZIP2: return "bzip2";
        case XXBYTE_NSIS_METHOD_LZMA: return "lzma";
        default: return "?";
    }
}

static const char *storage_text(uint32_t storage)
{
    switch (storage) {
        case XXBYTE_NSIS_STORED: return "uncompressed";
        case XXBYTE_NSIS_COMPRESSED: return "non-solid";
        default: return "solid";
    }
}

/* The largest uninstaller (or solid stream prefix) read into memory. */
#define DATA_LIMIT (256u * 1024u * 1024u)

/* Output of a raw-deflate stream, inflated again with more room as more
 * of it is needed. */
typedef struct stream_reader {
    const uint8_t *in;
    size_t in_size;
    uint8_t *out;
    size_t produced;
    size_t capacity;
    int ended;
} stream_reader;

/* Nonzero once the first need bytes of output are available. The output
 * grows by doubling, up to what the input can expand to (1032:1). */
static int stream_need(stream_reader *s, uint64_t need)
{
    uint64_t bound = (uint64_t)s->in_size * 1032u + 64u;

    if (bound > DATA_LIMIT) {
        bound = DATA_LIMIT;
    }
    if (need > bound) {
        return 0;
    }
    while ((uint64_t)s->produced < need && !s->ended) {
        uint64_t capacity = s->capacity < 65536u ? 65536u : (uint64_t)s->capacity * 2u;
        uint8_t *grown;
        if (capacity < need) {
            capacity = need;
        }
        if (capacity > bound) {
            capacity = bound;
        }
        if (capacity <= s->capacity) {
            return 0;
        }
        grown = (uint8_t *)xx_mem_realloc(s->out, (size_t)capacity);
        if (grown == NULL) {
            return 0;
        }
        s->out = grown;
        s->capacity = (size_t)capacity;
        if (xxbyte_inflate(s->in, s->in_size, 0, s->out, s->capacity, &s->produced, NULL) != XXBYTE_STATUS_OUT_OF_MEMORY) {
            s->ended = 1;
        }
    }
    return (uint64_t)s->produced >= need;
}

/* Inflates the whole stream (up to the limits of stream_need). */
static void stream_all(stream_reader *s)
{
    while (!s->ended && stream_need(s, (uint64_t)s->capacity + 1u)) {
    }
}

/* The data parameter of the first EW_WRITEUNINSTALLER entry, or -1. */
static int64_t uninstaller_data(const uint8_t *header, size_t size, uint32_t mode)
{
    xxbyte_nsis_script script;
    int64_t found = -1;
    uint32_t i;

    if (xxbyte_nsis_script_init(&script, header, size, mode, 0) != XXBYTE_STATUS_OK) {
        return -1;
    }
    for (i = 0; i < script.header.entry_count && found < 0; ++i) {
        const uint8_t *entry = header + script.header.entries_offset + (size_t)i * ENTRY_SIZE;
        if (xxbyte_nsis_opcode_id(script.mode, xxbyte_u32le(entry)) == XXBYTE_NSIS_ID_EW_WRITEUNINSTALLER && (int32_t)xxbyte_u32le(entry + 8) >= 0) {
            found = (int32_t)xxbyte_u32le(entry + 8);
        }
    }
    xxbyte_nsis_script_free(&script);
    return found;
}

static xxbyte_status list_archive(uint32_t mode, const uint8_t *archive, size_t archive_size, uint64_t offset, uint32_t listing_flags, xx_buf_t *out, int nested);

/*
 * Lists the uninstaller that EW_WRITEUNINSTALLER writes: its data item is
 * the uninstaller icon, and the next item the uninstaller's firstheader,
 * header and data. Non-solid items are a size (bit 31: deflated) and the
 * bytes; solid ones a size and the bytes inside the stream, after the
 * header.
 */
static xxbyte_status list_uninstaller(uint32_t mode, const uint8_t *archive, size_t archive_size, const xxbyte_nsis_header_location *location, uint32_t header_size,
                                      int64_t item, uint32_t listing_flags, xx_buf_t *out)
{
    stream_reader reader;
    xxbyte_status status = XXBYTE_STATUS_OK;
    const uint8_t *blob = NULL;
    uint64_t blob_size = 0;

    memset(&reader, 0, sizeof(reader));
    if (location->storage == XXBYTE_NSIS_SOLID) {
        uint64_t at = 4u + (uint64_t)header_size + (uint64_t)item;
        reader.in = archive + location->data_offset;
        reader.in_size = archive_size - (size_t)location->data_offset;
        if (stream_need(&reader, at + 4u)) {
            at += 4u + xxbyte_u32le(reader.out + at);
            if (stream_need(&reader, at + 4u)) {
                blob_size = xxbyte_u32le(reader.out + at);
                at += 4u;
                if (stream_need(&reader, at + blob_size)) {
                    blob = reader.out + at;
                }
            }
        }
    } else {
        uint64_t at = location->data_offset + location->data_size + (uint64_t)item;
        if (at + 4u <= archive_size) {
            at += 4u + (xxbyte_u32le(archive + at) & 0x7FFFFFFFu);
            if (at + 4u <= archive_size) {
                uint32_t length = xxbyte_u32le(archive + at);
                at += 4u;
                if (at + (length & 0x7FFFFFFFu) <= archive_size) {
                    if ((length & 0x80000000u) == 0) {
                        blob = archive + at;
                        blob_size = length;
                    } else {
                        reader.in = archive + at;
                        reader.in_size = length & 0x7FFFFFFFu;
                        stream_all(&reader);
                        blob = reader.out;
                        blob_size = reader.produced;
                    }
                }
            }
        }
    }
    if (blob == NULL || blob_size < XXBYTE_NSIS_FIRST_HEADER_SIZE) {
        TRY_OUT(xx_buf_appendf(out, "; uninstaller (EW_WRITEUNINSTALLER data@0x%llX): not found\n", (unsigned long long)item));
    } else {
        TRY_OUT(xx_buf_appendf(out, "; uninstaller (EW_WRITEUNINSTALLER data@0x%llX), %llu bytes\n", (unsigned long long)item, (unsigned long long)blob_size));
        status = list_archive(mode, blob, (size_t)blob_size, 0, listing_flags, out, 1);
    }
    xx_mem_free(reader.out);
    return status;
}

static xxbyte_status list_archive(uint32_t mode, const uint8_t *data, size_t size, uint64_t offset, uint32_t listing_flags, xx_buf_t *out, int nested)
{
    const uint8_t *archive = data + offset;
    size_t archive_size = size - (size_t)offset;
    xxbyte_nsis_first_header first;
    xxbyte_nsis_header_location location;
    xxbyte_status status;
    uint8_t *header;
    size_t need = 0;
    size_t got = 0;

    TRY(xxbyte_nsis_parse_first_header(archive, archive_size, &first));
    TRY_OUT(xx_buf_appendf(out, "; NSIS %s: firstheader at 0x%llX, flags 0x%X, header %u bytes, archive %u bytes\n",
                           (first.flags & XXBYTE_NSIS_FH_UNINSTALL) != 0 ? "uninstaller" : "installer", (unsigned long long)offset, first.flags, first.header_size,
                           first.archive_size));
    status = xxbyte_nsis_decompress_header(archive, archive_size, NULL, 0, &need, &location);
    if (status == XXBYTE_STATUS_OK || (status != XXBYTE_STATUS_OUT_OF_MEMORY && status != XXBYTE_STATUS_UNSUPPORTED_FORMAT && location.data_offset == 0)) {
        return status == XXBYTE_STATUS_OK ? XXBYTE_STATUS_INVALID_INSTRUCTION : status;
    }
    TRY_OUT(xx_buf_appendf(out, "; header data: %s %s%s at 0x%llX", storage_text(location.storage), method_text(location.method),
                           location.filter != 0 ? " with filter flag" : "", (unsigned long long)(offset + location.data_offset)));
    if (location.storage != XXBYTE_NSIS_SOLID) {
        TRY_OUT(xx_buf_appendf(out, ", %u bytes", location.data_size));
    }
    TRY_OUT(xx_buf_append_char(out, '\n'));
    if (status == XXBYTE_STATUS_UNSUPPORTED_FORMAT) {
        TRY_OUT(xx_buf_appendf(out, "; not listed: xxbyte cannot decompress %s; decompress the header and pass it to xxbyte_nsis_list_header\n",
                               method_text(location.method)));
        return status;
    }
    if (status != XXBYTE_STATUS_OUT_OF_MEMORY) {
        return status;
    }
    header = (uint8_t *)xx_mem_alloc(need);
    if (header == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_nsis_decompress_header(archive, archive_size, header, need, &got, NULL);
    if (status == XXBYTE_STATUS_OK) {
        status = xxbyte_nsis_list_header(header, got, mode, (first.flags & XXBYTE_NSIS_FH_UNINSTALL) != 0 ? XXBYTE_NSIS_SCRIPT_UNINSTALLER : 0u, listing_flags, out);
    }
    if (status == XXBYTE_STATUS_OK && !nested && (first.flags & XXBYTE_NSIS_FH_UNINSTALL) == 0) {
        int64_t item = uninstaller_data(header, got, mode);
        if (item >= 0) {
            status = list_uninstaller(mode, archive, archive_size, &location, first.header_size, item, listing_flags, out);
        }
    }
    xx_mem_free(header);
    return status;
}

xxbyte_status xxbyte_nsis_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_nsis_header header;
    uint64_t offset = 0;

    if (data == NULL || out == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (xxbyte_nsis_find_first_header(data, size, &offset) == XXBYTE_STATUS_OK) {
        return list_archive(mode, data, size, offset, listing_flags, out, 0);
    }
    /* A raw (decompressed) header block. */
    if (xxbyte_nsis_parse_header(data, size, &header) == XXBYTE_STATUS_OK ||
        (mode != XXBYTE_MODE_AUTO && XXBYTE_NSIS_MODE_VARIANT(mode) == XXBYTE_NSIS_V200 &&
         xxbyte_nsis_parse_header_ex(data, size, XXBYTE_NSIS_HEADER_7_BLOCKS, &header) == XXBYTE_STATUS_OK)) {
        return xxbyte_nsis_list_header(data, size, mode, 0, listing_flags, out);
    }
    return size < XXBYTE_NSIS_FIRST_HEADER_SIZE ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
}

#else
typedef int xxbyte_nsis_list_disabled;
#endif
