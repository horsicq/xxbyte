#include "xxbyte_nsis_internal.h"

#if XXBYTE_USE_NSIS

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#define ENTRY_SIZE XXBYTE_NSIS_ENTRY_SIZE

/* A raw-deflate stream expands at most 1032:1 (258-byte matches coded in 2
 * bits); a header claiming more than that from the input is corrupt. */
#define DEFLATE_RATIO 1032u

/* Header fields after the block table (fileform.h header): rootkey,
 * key_ptr, value_ptr, bg_color1..3, lb_bg, lb_fg, langtable_size,
 * license_bg, the callbacks, install_types[33], install_directory_ptr. */
#define FIELD_REG_ROOT 0u
#define FIELD_REG_KEY 1u
#define FIELD_REG_VALUE 2u
#define FIELD_LANGTABLE_SIZE 8u
#define FIELD_CALLBACKS 10u
#define FIELD_INSTALL_DIRECTORY (FIELD_CALLBACKS + XXBYTE_NSIS_CB_COUNT + 33u)

#define LANGTABLE_HEADER 10u /* LANGID, dlg_offset, rtl */
#define SECTION_FIELDS 6u

xxbyte_status xxbyte_nsis_decompress_header(const uint8_t *data, size_t size, uint8_t *out, size_t out_capacity, size_t *out_size, xxbyte_nsis_header_location *location)
{
    xxbyte_nsis_header_location where;
    xxbyte_nsis_first_header first;
    xxbyte_status status;
    size_t produced = 0;

    if (location != NULL) {
        memset(location, 0, sizeof(*location));
    }
    if (out_size == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *out_size = 0;
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = xxbyte_nsis_parse_first_header(data, size, &first);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    status = xxbyte_nsis_locate_header(data, size, &where);
    if (location != NULL) {
        *location = where;
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (where.method == XXBYTE_NSIS_METHOD_BZIP2 || where.method == XXBYTE_NSIS_METHOD_LZMA) {
        return XXBYTE_STATUS_UNSUPPORTED_FORMAT;
    }
    if (first.header_size == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    /* No larger header than the data can hold, before anything is
     * allocated for it. */
    if (where.storage == XXBYTE_NSIS_COMPRESSED && (uint64_t)first.header_size > (uint64_t)where.data_size * DEFLATE_RATIO) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (where.storage == XXBYTE_NSIS_SOLID && 4u + (uint64_t)first.header_size > ((uint64_t)size - where.data_offset) * DEFLATE_RATIO) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (out_capacity < first.header_size || out == NULL) {
        *out_size = first.header_size;
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (where.storage == XXBYTE_NSIS_STORED) {
        memcpy(out, data + where.data_offset, first.header_size);
        *out_size = first.header_size;
        return XXBYTE_STATUS_OK;
    }
    if (where.storage == XXBYTE_NSIS_COMPRESSED) {
        /* One block: it must produce exactly header_size bytes. */
        status = xxbyte_inflate(data + where.data_offset, where.data_size, 0, out, first.header_size, &produced, NULL);
        if (status == XXBYTE_STATUS_OUT_OF_MEMORY || (status == XXBYTE_STATUS_OK && produced != first.header_size)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (status == XXBYTE_STATUS_OK) {
            *out_size = produced;
        }
        return status;
    }
    /* Solid: the stream continues with the files; take the size prefix
     * and the header. */
    {
        uint64_t available = (uint64_t)size - where.data_offset;
        size_t need = (size_t)first.header_size + 4u;
        uint8_t *work;

#if SIZE_MAX <= 0xFFFFFFFFu
        if (first.header_size > SIZE_MAX - 4u) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
#endif
        work = (uint8_t *)xx_mem_alloc(need);
        if (work == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        status = xxbyte_inflate(data + where.data_offset, (size_t)available, 0, work, need, &produced, NULL);
        if (status == XXBYTE_STATUS_OUT_OF_MEMORY && produced == need) {
            status = XXBYTE_STATUS_OK;
        } else if (status == XXBYTE_STATUS_OK && produced != need) {
            /* The stream ends before the header does. */
            status = XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if ((status == XXBYTE_STATUS_OK || status == XXBYTE_STATUS_TRUNCATED) && produced >= 4 && xxbyte_u32le(work) != first.header_size) {
            status = XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (status == XXBYTE_STATUS_OK) {
            memcpy(out, work + 4, first.header_size);
            *out_size = first.header_size;
        }
        xx_mem_free(work);
        return status;
    }
}

/* ---- the script model ---- */

static int32_t field_at(const xxbyte_nsis_script *script, uint32_t field, int32_t fallback)
{
    uint32_t records = (XXBYTE_NSIS_MODE_VARIANT(script->mode) == XXBYTE_NSIS_V200) ? XXBYTE_NSIS_BLOCK_COUNT - 1u : XXBYTE_NSIS_BLOCK_COUNT;
    uint64_t at = 4u + (uint64_t)records * (script->header.pointer_size + 4u) + (uint64_t)field * 4u;

    /* The fields end before the first block (the pages). */
    if (at + 4u > script->header.blocks[XXBYTE_NSIS_BLOCK_PAGES].offset || at + 4u > script->size) {
        return fallback;
    }
    return (int32_t)xxbyte_u32le(script->data + at);
}

/* Bytes per record of block, from the offset of the next block. */
static uint32_t record_size(const xxbyte_nsis_header *header, uint32_t block, uint32_t next)
{
    uint64_t span;

    if (header->blocks[block].count == 0 || header->blocks[next].offset < header->blocks[block].offset) {
        return 0;
    }
    span = (header->blocks[next].offset - header->blocks[block].offset) / header->blocks[block].count;
    return span > UINT32_MAX ? UINT32_MAX : (uint32_t)span;
}

static void mark(xxbyte_nsis_script *script, int64_t entry, uint8_t bit)
{
    if (entry >= 0 && entry < (int64_t)script->header.entry_count) {
        script->marks[entry] |= bit;
    }
}

static void mark_entries(xxbyte_nsis_script *script)
{
    const uint8_t *entries = script->data + script->header.entries_offset;
    uint32_t i;
    uint32_t k;

    for (i = 0; i < script->header.entry_count; ++i) {
        xxbyte_instruction insn;
        if (xxbyte_decode_ex(XXBYTE_FAMILY_NSIS, script->mode, NULL, entries + (size_t)i * ENTRY_SIZE, ENTRY_SIZE, (uint64_t)i * ENTRY_SIZE, &insn) != XXBYTE_STATUS_OK) {
            continue;
        }
        for (k = 0; k < insn.operand_count; ++k) {
            const xxbyte_operand *op = &insn.operands[k];
            if (op->type != XXBYTE_OPERAND_BRANCH) {
                continue;
            }
            /* "Call :label" (parameter 1 set) calls a label. */
            if (insn.id == XXBYTE_NSIS_ID_EW_CALL && op->extra == 0 && xxbyte_u32le(entries + (size_t)i * ENTRY_SIZE + 8) == 0) {
                mark(script, op->value / ENTRY_SIZE, XXBYTE_NSIS_MARK_FUNCTION);
            } else {
                mark(script, op->value / ENTRY_SIZE, XXBYTE_NSIS_MARK_LABEL);
            }
        }
    }
    for (i = 0; i < XXBYTE_NSIS_CB_COUNT; ++i) {
        mark(script, script->callbacks[i], XXBYTE_NSIS_MARK_FUNCTION);
    }
    for (i = 0; i < script->page_count; ++i) {
        xxbyte_nsis_page page;
        if (xxbyte_nsis_script_page(script, i, &page) == XXBYTE_STATUS_OK) {
            mark(script, page.pre_function, XXBYTE_NSIS_MARK_FUNCTION);
            mark(script, page.show_function, XXBYTE_NSIS_MARK_FUNCTION);
            mark(script, page.leave_function, XXBYTE_NSIS_MARK_FUNCTION);
        }
    }
    for (i = 0; i < script->section_count; ++i) {
        xxbyte_nsis_section section;
        /* Group records have code too: an EW_RET of their own. */
        if (xxbyte_nsis_script_section(script, i, &section) == XXBYTE_STATUS_OK && section.code_size >= 0) {
            mark(script, section.code, XXBYTE_NSIS_MARK_SECTION);
        }
    }
}

static uint32_t string_codes(uint32_t mode, uint32_t unicode)
{
    uint32_t variant = XXBYTE_NSIS_MODE_VARIANT(mode);

    if (xxbyte_nsis_variant_is_nsis3(variant)) {
        return XXBYTE_NSIS_CODES_NSIS3;
    }
    if (xxbyte_nsis_variant_is_park(variant) && unicode) {
        return XXBYTE_NSIS_CODES_PARK;
    }
    return XXBYTE_NSIS_CODES_NSIS2;
}

xxbyte_status xxbyte_nsis_script_init(xxbyte_nsis_script *script, const uint8_t *header, size_t size, uint32_t mode, uint32_t options)
{
    xxbyte_nsis_script s;
    xxbyte_status status;
    uint32_t i;

    if (script == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(script, 0, sizeof(*script));
    if (header == NULL || (options & ~XXBYTE_NSIS_SCRIPT_UNINSTALLER) != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (mode != XXBYTE_MODE_AUTO && !xxbyte_nsis_mode_valid(mode)) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    memset(&s, 0, sizeof(s));
    s.data = header;
    s.size = size;
    s.options = options;
    status = xxbyte_nsis_parse_header_ex(header, size, mode != XXBYTE_MODE_AUTO && XXBYTE_NSIS_MODE_VARIANT(mode) == XXBYTE_NSIS_V200 ? XXBYTE_NSIS_HEADER_7_BLOCKS : 0u,
                                         &s.header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if ((uint64_t)s.header.entries_offset + (uint64_t)s.header.entry_count * ENTRY_SIZE > (uint64_t)size) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (mode == XXBYTE_MODE_AUTO) {
        status = xxbyte_nsis_guess_mode(header, size, &s.header, &mode);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    s.mode = mode;
    s.string_codes = string_codes(mode, s.header.unicode);

    s.page_size = record_size(&s.header, XXBYTE_NSIS_BLOCK_PAGES, XXBYTE_NSIS_BLOCK_SECTIONS);
    s.page_count = s.page_size >= 4u * 5u ? s.header.blocks[XXBYTE_NSIS_BLOCK_PAGES].count : 0u;
    s.section_size = record_size(&s.header, XXBYTE_NSIS_BLOCK_SECTIONS, XXBYTE_NSIS_BLOCK_ENTRIES);
    s.section_count = s.section_size >= 4u * SECTION_FIELDS ? s.header.blocks[XXBYTE_NSIS_BLOCK_SECTIONS].count : 0u;

    for (i = 0; i < XXBYTE_NSIS_CB_COUNT; ++i) {
        int32_t entry = field_at(&s, FIELD_CALLBACKS + i, -1);
        s.callbacks[i] = entry >= 0 && (uint32_t)entry < s.header.entry_count ? entry : -1;
    }
    s.install_reg_root = (uint32_t)field_at(&s, FIELD_REG_ROOT, 0);
    s.install_reg_key = field_at(&s, FIELD_REG_KEY, 0);
    s.install_reg_value = field_at(&s, FIELD_REG_VALUE, 0);
    s.install_directory = field_at(&s, FIELD_INSTALL_DIRECTORY, 0);

    s.langtable_count = s.header.blocks[XXBYTE_NSIS_BLOCK_LANGTABLES].count;
    {
        uint32_t table_size = (uint32_t)field_at(&s, FIELD_LANGTABLE_SIZE, 0);
        uint64_t start = s.header.blocks[XXBYTE_NSIS_BLOCK_LANGTABLES].offset;
        uint64_t end = s.header.blocks[XXBYTE_NSIS_BLOCK_CTLCOLORS].offset;
        if (s.langtable_count != 0 && table_size >= LANGTABLE_HEADER && table_size <= 0x7FFFFFFFu && end >= start && end <= size &&
            (uint64_t)table_size * s.langtable_count <= end - start) {
            s.langtable_size = table_size;
            s.lang_string_count = (table_size - LANGTABLE_HEADER) / 4u;
            for (i = 0; i < s.langtable_count; ++i) {
                if (xxbyte_u16le(header + start + (uint64_t)i * table_size) == 1033u) {
                    s.main_langtable = i;
                    break;
                }
            }
        } else {
            s.langtable_count = 0;
        }
    }

    if (s.header.entry_count != 0) {
        s.marks = (uint8_t *)xx_mem_alloc(s.header.entry_count);
        if (s.marks == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        memset(s.marks, 0, s.header.entry_count);
        mark_entries(&s);
    }
    *script = s;
    return XXBYTE_STATUS_OK;
}

void xxbyte_nsis_script_free(xxbyte_nsis_script *script)
{
    if (script == NULL) {
        return;
    }
    xx_mem_free(script->marks);
    memset(script, 0, sizeof(*script));
}

static const uint8_t *record(const xxbyte_nsis_script *script, uint32_t block, uint32_t record_bytes, uint32_t index)
{
    uint64_t at = script->header.blocks[block].offset + (uint64_t)index * record_bytes;

    return script->data + at;
}

static int32_t record_field(const uint8_t *p, uint32_t record_bytes, uint32_t field, int32_t fallback)
{
    return (field + 1u) * 4u <= record_bytes ? (int32_t)xxbyte_u32le(p + field * 4u) : fallback;
}

xxbyte_status xxbyte_nsis_script_section(const xxbyte_nsis_script *script, uint32_t index, xxbyte_nsis_section *section)
{
    const uint8_t *p;

    if (section != NULL) {
        memset(section, 0, sizeof(*section));
    }
    if (script == NULL || section == NULL || script->data == NULL || index >= script->section_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    p = record(script, XXBYTE_NSIS_BLOCK_SECTIONS, script->section_size, index);
    section->name = (int32_t)xxbyte_u32le(p);
    section->install_types = xxbyte_u32le(p + 4);
    section->flags = xxbyte_u32le(p + 8);
    section->code = (int32_t)xxbyte_u32le(p + 12);
    section->code_size = (int32_t)xxbyte_u32le(p + 16);
    section->size_kb = (int32_t)xxbyte_u32le(p + 20);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_nsis_script_page(const xxbyte_nsis_script *script, uint32_t index, xxbyte_nsis_page *page)
{
    const uint8_t *p;
    uint32_t size;
    uint32_t i;

    if (page != NULL) {
        memset(page, 0, sizeof(*page));
    }
    if (script == NULL || page == NULL || script->data == NULL || index >= script->page_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    size = script->page_size;
    p = record(script, XXBYTE_NSIS_BLOCK_PAGES, size, index);
    page->dialog_id = record_field(p, size, 0, 0);
    page->wndproc_id = record_field(p, size, 1, -1);
    page->pre_function = record_field(p, size, 2, -1);
    page->show_function = record_field(p, size, 3, -1);
    page->leave_function = record_field(p, size, 4, -1);
    page->flags = (uint32_t)record_field(p, size, 5, 0);
    page->caption = record_field(p, size, 6, 0);
    page->back = record_field(p, size, 7, 0);
    page->next = record_field(p, size, 8, 0);
    page->click_next = record_field(p, size, 9, 0);
    page->cancel = record_field(p, size, 10, 0);
    for (i = 0; i < 5; ++i) {
        page->params[i] = record_field(p, size, 11 + i, 0);
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_nsis_script_lang_string(const xxbyte_nsis_script *script, uint32_t table, uint32_t id, int32_t *string)
{
    const uint8_t *p;

    if (string != NULL) {
        *string = 0;
    }
    if (script == NULL || string == NULL || script->data == NULL || table >= script->langtable_count || id >= script->lang_string_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    p = script->data + script->header.blocks[XXBYTE_NSIS_BLOCK_LANGTABLES].offset + (uint64_t)table * script->langtable_size;
    *string = (int32_t)xxbyte_u32le(p + LANGTABLE_HEADER + id * 4u);
    return XXBYTE_STATUS_OK;
}

const char *xxbyte_nsis_callback_name(uint32_t index, int uninstaller)
{
    static const char *const names[2][XXBYTE_NSIS_CB_COUNT] = {
        {".onInit", ".onInstSuccess", ".onInstFailed", ".onUserAbort", ".onGUIInit", ".onGUIEnd", ".onMouseOverSection", ".onVerifyInstDir", ".onSelChange",
         ".onRebootFailed"},
        {"un.onInit", "un.onUninstSuccess", "un.onUninstFailed", "un.onUserAbort", "un.onGUIInit", "un.onGUIEnd", "un.onMouseOverSection", "un.onVerifyInstDir",
         "un.onSelChange", "un.onRebootFailed"},
    };

    if (index >= XXBYTE_NSIS_CB_COUNT) {
        return NULL;
    }
    return names[uninstaller != 0][index];
}

/* ---- strings ---- */

/* Output that keeps whole pieces: once one piece does not fit in limit
 * bytes, nothing more is written, but the full length is still counted. */
typedef struct text_out {
    char *buffer;
    size_t limit; /* bytes that may be written, without the NUL */
    size_t written;
    size_t length;
    int full;
} text_out;

static void put(text_out *out, const char *text, size_t n)
{
    out->length += n;
    if (out->full) {
        return;
    }
    if (n > out->limit - out->written) {
        out->full = 1;
        return;
    }
    if (out->buffer != NULL) {
        memcpy(out->buffer + out->written, text, n);
    }
    out->written += n;
}

static void put_hex(text_out *out, const char *prefix, uint32_t value, unsigned digits, const char *suffix)
{
    static const char hex[] = "0123456789ABCDEF";
    char text[32];
    size_t n = 0;
    unsigned i;

    while (*prefix != '\0') {
        text[n++] = *prefix++;
    }
    for (i = digits; i-- > 0;) {
        text[n++] = hex[(value >> (4u * i)) & 0xFu];
    }
    while (*suffix != '\0') {
        text[n++] = *suffix++;
    }
    put(out, text, n);
}

static void put_decimal(text_out *out, const char *prefix, uint32_t value, const char *suffix)
{
    char digits[12];
    size_t n = 0;
    char text[48];
    size_t m = 0;

    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    while (*prefix != '\0') {
        text[m++] = *prefix++;
    }
    while (n > 0) {
        text[m++] = digits[--n];
    }
    while (*suffix != '\0') {
        text[m++] = *suffix++;
    }
    put(out, text, m);
}

/* A character of the string's own text. */
static void put_char(text_out *out, uint32_t c, int quoted)
{
    char text[4];

    if (quoted) {
        switch (c) {
            case '"': put(out, "$\\\"", 3); return;
            case '\r': put(out, "$\\r", 3); return;
            case '\n': put(out, "$\\n", 3); return;
            case '\t': put(out, "$\\t", 3); return;
            case '$': put(out, "$$", 2); return;
            default: break;
        }
        if (c < 0x20u || c == 0x7Fu) {
            put_hex(out, "${U+", c, 2, "}");
            return;
        }
    }
    if (c >= 0xD800u && c <= 0xDFFFu) {
        put_hex(out, "${U+", c, 4, "}");
    } else if (c < 0x80u) {
        text[0] = (char)c;
        put(out, text, 1);
    } else if (c < 0x800u) {
        text[0] = (char)(0xC0u | (c >> 6));
        text[1] = (char)(0x80u | (c & 0x3Fu));
        put(out, text, 2);
    } else if (c < 0x10000u) {
        text[0] = (char)(0xE0u | (c >> 12));
        text[1] = (char)(0x80u | ((c >> 6) & 0x3Fu));
        text[2] = (char)(0x80u | (c & 0x3Fu));
        put(out, text, 3);
    } else {
        text[0] = (char)(0xF0u | (c >> 18));
        text[1] = (char)(0x80u | ((c >> 12) & 0x3Fu));
        text[2] = (char)(0x80u | ((c >> 6) & 0x3Fu));
        text[3] = (char)(0x80u | (c & 0x3Fu));
        put(out, text, 4);
    }
}

static void put_variable(text_out *out, uint32_t mode, uint32_t index)
{
    char name[32];
    size_t n = xxbyte_nsis_variable_name(mode, index, name, sizeof(name));

    put(out, name, n < sizeof(name) ? n : sizeof(name) - 1);
}

typedef struct shell_folder {
    const char *name;
    uint8_t current;
    uint8_t all;
} shell_folder;

/* makensis 3 (build.cpp): name, CSIDL for the current user, for all
 * users. The first of two equal pairs wins. */
static const shell_folder shell_nsis3[] = {
    {"WINDIR", 0x24, 0x24},
    {"SYSDIR", 0x25, 0x25},
    {"SMPROGRAMS", 0x02, 0x17},
    {"SMSTARTUP", 0x07, 0x18},
    {"DESKTOP", 0x10, 0x19},
    {"STARTMENU", 0x0B, 0x16},
    {"QUICKLAUNCH", 0x1A, 0x1A},
    {"DOCUMENTS", 0x05, 0x2E},
    {"SENDTO", 0x09, 0x09},
    {"RECENT", 0x08, 0x08},
    {"FAVORITES", 0x06, 0x1F},
    {"MUSIC", 0x0D, 0x35},
    {"PICTURES", 0x27, 0x36},
    {"VIDEOS", 0x0E, 0x37},
    {"NETHOOD", 0x13, 0x13},
    {"FONTS", 0x14, 0x14},
    {"TEMPLATES", 0x15, 0x2D},
    {"APPDATA", 0x1A, 0x23},
    {"LOCALAPPDATA", 0x1C, 0x23},
    {"PRINTHOOD", 0x1B, 0x1B},
    {"INTERNET_CACHE", 0x20, 0x20},
    {"COOKIES", 0x21, 0x21},
    {"HISTORY", 0x22, 0x22},
    {"PROFILE", 0x28, 0x28},
    {"ADMINTOOLS", 0x30, 0x2F},
    {"RESOURCES", 0x38, 0x38},
    {"RESOURCES_LOCALIZED", 0x39, 0x39},
    {"CDBURN_AREA", 0x3B, 0x3B},
    {"USERAPPDATA", 0x1A, 0x5A},
    {"USERLOCALAPPDATA", 0x1C, 0x1C},
    {"USERTEMPLATES", 0x15, 0x15},
    {"USERSTARTMENU", 0x0B, 0x0B},
    {"USERSMPROGRAMS", 0x02, 0x02},
    {"USERDESKTOP", 0x10, 0x10},
    {"COMMONLOCALAPPDATA", 0x23, 0x23}, /* also $COMMONPROGRAMDATA */
    {"COMMONTEMPLATES", 0x2D, 0x2D},
    {"COMMONSTARTMENU", 0x16, 0x16},
    {"COMMONSMPROGRAMS", 0x17, 0x17},
    {"COMMONDESKTOP", 0x19, 0x19},
};

/* makensis 2.51 (and Park's 2.46 builds): $LOCALAPPDATA is the user's
 * folder for both contexts, and there are no USER and COMMON forms. */
static const shell_folder shell_nsis2[] = {
    {"WINDIR", 0x24, 0x24},       {"SYSDIR", 0x25, 0x25},    {"SMPROGRAMS", 0x02, 0x17},
    {"SMSTARTUP", 0x07, 0x18},    {"DESKTOP", 0x10, 0x19},   {"STARTMENU", 0x0B, 0x16},
    {"QUICKLAUNCH", 0x1A, 0x1A},  {"DOCUMENTS", 0x05, 0x2E}, {"SENDTO", 0x09, 0x09},
    {"RECENT", 0x08, 0x08},       {"FAVORITES", 0x06, 0x1F}, {"MUSIC", 0x0D, 0x35},
    {"PICTURES", 0x27, 0x36},     {"VIDEOS", 0x0E, 0x37},    {"NETHOOD", 0x13, 0x13},
    {"FONTS", 0x14, 0x14},        {"TEMPLATES", 0x15, 0x2D}, {"APPDATA", 0x1A, 0x23},
    {"LOCALAPPDATA", 0x1C, 0x1C}, {"PRINTHOOD", 0x1B, 0x1B}, {"INTERNET_CACHE", 0x20, 0x20},
    {"COOKIES", 0x21, 0x21},      {"HISTORY", 0x22, 0x22},   {"PROFILE", 0x28, 0x28},
    {"ADMINTOOLS", 0x30, 0x2F},   {"RESOURCES", 0x38, 0x38}, {"RESOURCES_LOCALIZED", 0x39, 0x39},
    {"CDBURN_AREA", 0x3B, 0x3B},
};

/* Compares the string at string-table offset with an ASCII text. */
static int string_equals(const xxbyte_nsis_script *script, uint32_t offset, const char *text)
{
    uint64_t at;
    uint32_t length;
    uint32_t i;

    if (xxbyte_nsis_string_at(script->data, script->size, &script->header, offset, &at, &length) != XXBYTE_STATUS_OK || length != strlen(text)) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        uint32_t c = script->header.unicode ? xxbyte_u16le(script->data + at + 2u * i) : script->data[at + i];
        if (c != (uint8_t)text[i]) {
            return 0;
        }
    }
    return 1;
}

/* prefix and name as one piece. */
static void put_name(text_out *out, const char *prefix, const char *name)
{
    char text[40];
    size_t a = strlen(prefix);
    size_t b = strlen(name);

    memcpy(text, prefix, a);
    memcpy(text + a, name, b);
    put(out, text, a + b);
}

static void put_shell(text_out *out, const xxbyte_nsis_script *script, uint32_t current, uint32_t all)
{
    const shell_folder *table = script->string_codes == XXBYTE_NSIS_CODES_NSIS3 ? shell_nsis3 : shell_nsis2;
    size_t count = script->string_codes == XXBYTE_NSIS_CODES_NSIS3 ? sizeof(shell_nsis3) / sizeof(shell_nsis3[0]) : sizeof(shell_nsis2) / sizeof(shell_nsis2[0]);
    const shell_folder *other = table == shell_nsis3 ? shell_nsis2 : shell_nsis3;
    size_t other_count = table == shell_nsis3 ? sizeof(shell_nsis2) / sizeof(shell_nsis2[0]) : sizeof(shell_nsis3) / sizeof(shell_nsis3[0]);
    size_t i;

    if ((current & 0x80u) != 0) {
        /* A registry value under HKLM\Software\Microsoft\Windows\
         * CurrentVersion (its name at offset current & 0x3F), with 0x40
         * for the other registry view, and the fallback string in all. */
        const char *name = NULL;
        if (string_equals(script, current & 0x3Fu, "ProgramFilesDir")) {
            name = "$PROGRAMFILES";
        } else if (string_equals(script, current & 0x3Fu, "CommonFilesDir")) {
            name = "$COMMONFILES";
        }
        if (name == NULL) {
            put_hex(out, "$_SHELL_REG_0x", current, 2, "_");
            return;
        }
        if ((current & 0x40u) != 0) {
            /* KEY_WOW64_64KEY in 32-bit installers, 32KEY in 64-bit ones */
            put_name(out, name, script->header.pointer_size == 8 ? "32" : "64");
        } else {
            put_name(out, name, "");
        }
        return;
    }
    for (i = 0; i < count; ++i) {
        if (table[i].current == current && table[i].all == all) {
            put_name(out, "$", table[i].name);
            return;
        }
    }
    /* A pair only the other generation writes. */
    for (i = 0; i < other_count; ++i) {
        if (other[i].current == current && other[i].all == all) {
            put_name(out, "$", other[i].name);
            return;
        }
    }
    {
        static const char hex[] = "0123456789ABCDEF";
        char text[] = "$_SHELL_0x00_0x00_";
        text[10] = hex[(current >> 4) & 0xFu];
        text[11] = hex[current & 0xFu];
        text[15] = hex[(all >> 4) & 0xFu];
        text[16] = hex[all & 0xFu];
        put(out, text, sizeof(text) - 1);
    }
}

/* Decodes the string at string-table offset into out. */
static xxbyte_status decode_string(const xxbyte_nsis_script *script, uint32_t offset, int quoted, text_out *out)
{
    const xxbyte_nsis_header *h = &script->header;
    const uint8_t *table = script->data + h->strings_offset;
    const uint32_t unit = h->unicode ? 2u : 1u;
    const uint64_t end = h->strings_size;
    uint64_t at = (uint64_t)offset * unit;
    uint32_t codes = script->string_codes;

    if (h->strings_size == 0 || at >= end || (uint64_t)h->strings_offset + end > script->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
#define READ(pos) (unit == 2u ? (uint32_t)xxbyte_u16le(table + (pos)) : (uint32_t)table[(pos)])
    while (at + unit <= end) {
        uint32_t c = READ(at);
        uint32_t code = 0; /* 1 lang, 2 shell, 3 var, 4 skip */
        at += unit;
        if (c == 0) {
            return XXBYTE_STATUS_OK;
        }
        if (codes == XXBYTE_NSIS_CODES_NSIS3) {
            code = c <= 4u ? c : 0u;
        } else if (codes == XXBYTE_NSIS_CODES_PARK) {
            /* 0xE000 skip, 0xE001 var, 0xE002 shell, 0xE003 lang */
            code = c >= 0xE000u && c <= 0xE003u ? 4u - (c - 0xE000u) : 0u;
        } else if (!h->unicode) {
            /* 252 skip, 253 var, 254 shell, 255 lang */
            code = c >= 252u ? 4u - (c - 252u) : 0u;
        }
        if (code == 0) {
            if (unit == 2u && c >= 0xD800u && c <= 0xDBFFu && at + 2u <= end) {
                uint32_t low = READ(at);
                if (low >= 0xDC00u && low <= 0xDFFFu) {
                    at += 2u;
                    c = 0x10000u + ((c - 0xD800u) << 10) + (low - 0xDC00u);
                }
            }
            put_char(out, c, quoted);
            continue;
        }
        if (code == 4u || unit == 2u) {
            /* One character follows: the skipped one, or (Unicode) the
             * coded number. */
            uint32_t n;
            if (at + unit > end) {
                break;
            }
            n = READ(at);
            at += unit;
            if (n == 0) {
                return XXBYTE_STATUS_OK;
            }
            if (code == 4u) {
                put_char(out, n, quoted);
            } else if (code == 2u) {
                put_shell(out, script, n & 0xFFu, n >> 8);
            } else {
                uint32_t number = codes == XXBYTE_NSIS_CODES_PARK ? (n & 0x7FFFu) : ((n & 0x7Fu) | (((n >> 8) & 0x7Fu) << 7));
                if (code == 3u) {
                    put_variable(out, script->mode, number);
                } else {
                    put_decimal(out, "$(LSTR_", number, ")");
                }
            }
            continue;
        }
        /* ANSI: two bytes follow. */
        {
            uint32_t c0;
            uint32_t c1;
            if (at + 1u > end) {
                break;
            }
            c0 = table[at++];
            if (c0 == 0) {
                return XXBYTE_STATUS_OK;
            }
            if (at + 1u > end) {
                break;
            }
            c1 = table[at++];
            if (c1 == 0) {
                return XXBYTE_STATUS_OK;
            }
            if (code == 2u) {
                put_shell(out, script, c0, c1);
            } else {
                uint32_t number = (c0 & 0x7Fu) | ((c1 & 0x7Fu) << 7);
                if (code == 3u) {
                    put_variable(out, script->mode, number);
                } else {
                    put_decimal(out, "$(LSTR_", number, ")");
                }
            }
        }
    }
#undef READ
    return XXBYTE_STATUS_TRUNCATED;
}

/* The text of parameter into out, quoted or not. */
static xxbyte_status string_text(const xxbyte_nsis_script *script, int32_t parameter, uint32_t flags, text_out *out)
{
    int quoted = (flags & XXBYTE_NSIS_STRING_QUOTED) != 0;
    xxbyte_status status;

    if (parameter < 0) {
        put_decimal(out, "$(LSTR_", (uint32_t)(-(int64_t)parameter - 1), ")");
        return XXBYTE_STATUS_OK;
    }
    if (quoted) {
        put(out, "\"", 1);
    }
    status = decode_string(script, (uint32_t)parameter, quoted, out);
    if (status == XXBYTE_STATUS_INVALID_ARGUMENT) {
        return status;
    }
    if (quoted) {
        put(out, "\"", 1);
    }
    return status;
}

xxbyte_status xxbyte_nsis_script_string(const xxbyte_nsis_script *script, int32_t parameter, uint32_t flags, char *buffer, size_t buffer_size, size_t *length)
{
    text_out out;
    xxbyte_status status;

    if (length != NULL) {
        *length = 0;
    }
    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if (script == NULL || script->data == NULL || length == NULL || (buffer == NULL && buffer_size != 0) || (flags & ~XXBYTE_NSIS_STRING_QUOTED) != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(&out, 0, sizeof(out));
    out.buffer = buffer;
    out.limit = buffer_size != 0 ? buffer_size - 1 : 0;
    status = string_text(script, parameter, flags, &out);
    if (status == XXBYTE_STATUS_INVALID_ARGUMENT) {
        if (buffer != NULL && buffer_size != 0) {
            buffer[0] = '\0';
        }
        return status;
    }
    if (buffer != NULL && buffer_size != 0) {
        buffer[out.written] = '\0';
    }
    *length = out.length;
    return status;
}

static size_t copy_out(const char *text, size_t length, char *buffer, size_t buffer_size)
{
    if (buffer != NULL && buffer_size != 0) {
        size_t n = length < buffer_size ? length : buffer_size - 1;
        memcpy(buffer, text, n);
        buffer[n] = '\0';
    }
    return length;
}

size_t xxbyte_nsis_script_label(const xxbyte_nsis_script *script, uint32_t entry, char *buffer, size_t buffer_size)
{
    char text[40];
    text_out out;
    uint32_t i;

    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if (script == NULL) {
        return 0;
    }
    for (i = 0; i < XXBYTE_NSIS_CB_COUNT; ++i) {
        if (script->callbacks[i] >= 0 && (uint32_t)script->callbacks[i] == entry) {
            const char *name = xxbyte_nsis_callback_name(i, (script->options & XXBYTE_NSIS_SCRIPT_UNINSTALLER) != 0);
            return copy_out(name, strlen(name), buffer, buffer_size);
        }
    }
    memset(&out, 0, sizeof(out));
    out.buffer = text;
    out.limit = sizeof(text) - 1;
    if (script->marks != NULL && entry < script->header.entry_count && (script->marks[entry] & XXBYTE_NSIS_MARK_FUNCTION) != 0) {
        put_decimal(&out, "func_", entry, "");
    } else {
        put_decimal(&out, "label_", entry, "");
    }
    return copy_out(text, out.written, buffer, buffer_size);
}

size_t xxbyte_nsis_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_nsis_script *script = (const xxbyte_nsis_script *)context;
    const xxbyte_operand *operand;

    if (script == NULL || script->data == NULL || instruction == NULL || buffer == NULL || buffer_size < 8 || operand_index >= instruction->operand_count ||
        instruction->family != XXBYTE_FAMILY_NSIS) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (operand->type == XXBYTE_OPERAND_BRANCH) {
        uint64_t base = script->context.entries_address;
        uint64_t target = (uint64_t)operand->value;
        if (target < base || (target - base) % ENTRY_SIZE != 0 || (target - base) / ENTRY_SIZE >= script->header.entry_count) {
            return 0;
        }
        return xxbyte_nsis_script_label(script, (uint32_t)((target - base) / ENTRY_SIZE), buffer, buffer_size);
    }
    if (operand->type != XXBYTE_OPERAND_INDEX) {
        return 0;
    }
    if (operand->kind == XXBYTE_INDEX_STRING || operand->kind == XXBYTE_NSIS_INDEX_LANGSTRING) {
        text_out out;
        int32_t parameter;
        if (operand->value < 0 || operand->value > INT32_MAX) {
            return 0;
        }
        parameter = operand->kind == XXBYTE_INDEX_STRING ? (int32_t)operand->value : (int32_t)(-operand->value - 1);
        memset(&out, 0, sizeof(out));
        out.buffer = buffer;
        out.limit = buffer_size - 1 - 4; /* room for ..." */
        if (string_text(script, parameter, XXBYTE_NSIS_STRING_QUOTED, &out) == XXBYTE_STATUS_INVALID_ARGUMENT) {
            return 0;
        }
        if (out.full) {
            memcpy(buffer + out.written, "...\"", 4);
            out.written += 4;
        }
        buffer[out.written] = '\0';
        return out.written;
    }
    return 0;
}

#else
typedef int xxbyte_nsis_script_disabled;
#endif
