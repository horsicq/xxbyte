#include "xxbyte_internal.h"

#if XXBYTE_USE_DEX

#include "xxbyte_dex_file.h"
#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

/*
 * DEX file reader, reference formatter and container listing. The layout
 * follows the AOSP "Dalvik executable format" document; the text of names
 * and references follows dexlib2 2.5.2 (smali), which the tests were
 * checked against.
 */

#define DEX_HEADER_SIZE 0x70u
#define DEX_HEADER_SIZE_41 0x78u
#define DEX_ENDIAN_CONSTANT 0x12345678u
/* The longest text of one reference before it is cut. */
#define DEX_REF_TEXT_LIMIT (64u * 1024u)
/* Nesting limit for encoded arrays and annotations. */
#define DEX_VALUE_DEPTH 16u

/* ---- bounds-checked reading ---- */

static int dex_range(const xxbyte_dex_file *dex, uint64_t offset, uint64_t size)
{
    return offset <= (uint64_t)dex->size && size <= (uint64_t)dex->size - offset;
}

typedef struct dex_reader {
    const uint8_t *data;
    size_t pos;
    size_t end;
    xxbyte_status status; /* first failure */
} dex_reader;

static void dex_reader_init(dex_reader *r, const xxbyte_dex_file *dex, uint32_t offset)
{
    r->data = dex->data;
    r->pos = offset;
    r->end = dex->size;
    r->status = XXBYTE_STATUS_OK;
    if ((uint64_t)offset > (uint64_t)dex->size) {
        r->pos = dex->size;
        r->status = XXBYTE_STATUS_TRUNCATED;
    }
}

static void dex_fail(dex_reader *r, xxbyte_status status)
{
    if (r->status == XXBYTE_STATUS_OK) {
        r->status = status;
    }
}

static uint32_t dex_u8(dex_reader *r)
{
    if (r->status != XXBYTE_STATUS_OK || r->pos >= r->end) {
        dex_fail(r, XXBYTE_STATUS_TRUNCATED);
        return 0;
    }
    return r->data[r->pos++];
}

/* ULEB128 of at most 5 bytes; bits past 32 are ignored as ART does. */
static uint32_t dex_uleb(dex_reader *r)
{
    uint32_t value = 0;
    unsigned i;

    for (i = 0; i < 5; ++i) {
        uint32_t b = dex_u8(r);
        if (r->status != XXBYTE_STATUS_OK) {
            return 0;
        }
        value |= (b & 0x7Fu) << (7u * i);
        if ((b & 0x80u) == 0) {
            return value;
        }
    }
    dex_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
    return 0;
}

static int32_t dex_sleb(dex_reader *r)
{
    uint32_t value = 0;
    unsigned i;

    for (i = 0; i < 5; ++i) {
        uint32_t b = dex_u8(r);
        if (r->status != XXBYTE_STATUS_OK) {
            return 0;
        }
        value |= (b & 0x7Fu) << (7u * i);
        if ((b & 0x80u) == 0) {
            unsigned bits = 7u * (i + 1u);
            return bits >= 32 ? (int32_t)value : (int32_t)xxbyte_sext(value, bits);
        }
    }
    dex_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
    return 0;
}

static xxbyte_status dex_item(const xxbyte_dex_file *dex, const xxbyte_dex_section *section, uint32_t index, uint32_t item_size, const uint8_t **item)
{
    if (dex == NULL || dex->data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (index >= section->count) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    /* xxbyte_dex_open checked the whole table. */
    *item = dex->data + section->offset + (size_t)index * item_size;
    return XXBYTE_STATUS_OK;
}

/* ---- header ---- */

static xxbyte_status dex_section_at(xxbyte_dex_file *dex, const uint8_t *p, uint32_t item_size, xxbyte_dex_section *section)
{
    section->count = xxbyte_u32le(p);
    section->offset = xxbyte_u32le(p + 4);
    if (section->count == 0) {
        section->offset = 0;
        return XXBYTE_STATUS_OK;
    }
    return dex_range(dex, section->offset, (uint64_t)section->count * item_size) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_TRUNCATED;
}

static xxbyte_status dex_open_map(xxbyte_dex_file *dex)
{
    uint32_t i;
    uint32_t count;

    if (dex->map_off == 0) {
        return XXBYTE_STATUS_OK;
    }
    if (!dex_range(dex, dex->map_off, 4)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    count = xxbyte_u32le(dex->data + dex->map_off);
    if (!dex_range(dex, (uint64_t)dex->map_off + 4u, (uint64_t)count * 12u)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    dex->map_count = count;
    for (i = 0; i < count; ++i) {
        const uint8_t *p = dex->data + dex->map_off + 4u + 12u * (size_t)i;
        uint16_t type = xxbyte_u16le(p);
        xxbyte_dex_section *section = NULL;
        uint32_t item_size = 0;

        if (type == XXBYTE_DEX_TYPE_CALL_SITE_ID_ITEM) {
            section = &dex->call_site_ids;
            item_size = 4;
        } else if (type == XXBYTE_DEX_TYPE_METHOD_HANDLE_ITEM) {
            section = &dex->method_handles;
            item_size = 8;
        }
        if (section != NULL && section->count == 0) {
            xxbyte_status status = dex_section_at(dex, p + 4, item_size, section);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
        }
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_dex_open(const uint8_t *data, size_t size, xxbyte_dex_file *dex)
{
    xxbyte_status status;
    uint32_t version;
    uint32_t limit;

    if (dex == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(dex, 0, sizeof(*dex));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < 8) {
        return size >= 4 && memcmp(data, "dex\n", 4) != 0 ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_TRUNCATED;
    }
    if (memcmp(data, "dex\n0", 5) != 0 || data[5] < '0' || data[5] > '9' || data[6] < '0' || data[6] > '9' || data[7] != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    version = (uint32_t)(data[5] - '0') * 10u + (uint32_t)(data[6] - '0');
    if (version != 35 && (version < 37 || version > XXBYTE_DEX_VERSION_NEWEST)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (size < DEX_HEADER_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    dex->data = data;
    dex->size = size > UINT32_MAX ? (size_t)UINT32_MAX : size;
    dex->version = version;
    dex->mode = XXBYTE_DEX_MODE(version);
    dex->checksum = xxbyte_u32le(data + 8);
    dex->file_size = xxbyte_u32le(data + 32);
    dex->header_size = xxbyte_u32le(data + 36);
    dex->endian_tag = xxbyte_u32le(data + 40);
    dex->link_size = xxbyte_u32le(data + 44);
    dex->link_off = xxbyte_u32le(data + 48);
    dex->map_off = xxbyte_u32le(data + 52);
    dex->data_size = xxbyte_u32le(data + 104);
    dex->data_off = xxbyte_u32le(data + 108);
    dex->container_size = dex->file_size;
    status = XXBYTE_STATUS_INVALID_INSTRUCTION;
    if (dex->endian_tag != DEX_ENDIAN_CONSTANT || dex->header_size < DEX_HEADER_SIZE || dex->file_size < DEX_HEADER_SIZE) {
        goto fail;
    }
    status = XXBYTE_STATUS_TRUNCATED;
    if (version >= 41 && dex->header_size >= DEX_HEADER_SIZE_41) {
        if (dex->size < DEX_HEADER_SIZE_41) {
            goto fail;
        }
        dex->container_size = xxbyte_u32le(data + 112);
        dex->header_offset = xxbyte_u32le(data + 116);
        if (dex->header_offset != 0 || dex->container_size < dex->file_size) {
            status = XXBYTE_STATUS_INVALID_INSTRUCTION;
            goto fail;
        }
    }
    limit = dex->container_size;
    if ((uint64_t)limit > (uint64_t)size || dex->header_size > limit) {
        goto fail;
    }
    if ((status = dex_section_at(dex, data + 56, 4, &dex->string_ids)) != XXBYTE_STATUS_OK ||
        (status = dex_section_at(dex, data + 64, 4, &dex->type_ids)) != XXBYTE_STATUS_OK ||
        (status = dex_section_at(dex, data + 72, 12, &dex->proto_ids)) != XXBYTE_STATUS_OK ||
        (status = dex_section_at(dex, data + 80, 8, &dex->field_ids)) != XXBYTE_STATUS_OK ||
        (status = dex_section_at(dex, data + 88, 8, &dex->method_ids)) != XXBYTE_STATUS_OK ||
        (status = dex_section_at(dex, data + 96, 32, &dex->class_defs)) != XXBYTE_STATUS_OK || (status = dex_open_map(dex)) != XXBYTE_STATUS_OK) {
        goto fail;
    }
    return XXBYTE_STATUS_OK;
fail:
    memset(dex, 0, sizeof(*dex));
    return status;
}

static int dex_valid(const xxbyte_dex_file *dex)
{
    return dex != NULL && dex->data != NULL;
}

xxbyte_status xxbyte_dex_map_item(const xxbyte_dex_file *dex, uint32_t index, uint16_t *type, uint32_t *count, uint32_t *offset)
{
    const uint8_t *p;

    if (!dex_valid(dex)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (index >= dex->map_count) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    p = dex->data + dex->map_off + 4u + 12u * (size_t)index;
    if (type != NULL) {
        *type = xxbyte_u16le(p);
    }
    if (count != NULL) {
        *count = xxbyte_u32le(p + 4);
    }
    if (offset != NULL) {
        *offset = xxbyte_u32le(p + 8);
    }
    return XXBYTE_STATUS_OK;
}

/* ---- ids ---- */

/* The string_data_item of string_idx, scanning at most max bytes for its
 * NUL: *complete is 0 when the scan stopped there. */
static xxbyte_status dex_string_scan(const xxbyte_dex_file *dex, uint32_t string_idx, size_t max, const uint8_t **mutf8, size_t *length, uint32_t *utf16_size,
                                     int *complete)
{
    const uint8_t *item;
    dex_reader r;
    uint32_t units;
    size_t start;
    size_t end;
    size_t stop;
    xxbyte_status status = dex_item(dex, &dex->string_ids, string_idx, 4, &item);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    dex_reader_init(&r, dex, xxbyte_u32le(item));
    units = dex_uleb(&r);
    if (r.status != XXBYTE_STATUS_OK) {
        return r.status;
    }
    start = r.pos;
    end = start;
    stop = r.end - start > max ? start + max : r.end;
    while (end < stop && r.data[end] != 0) {
        ++end;
    }
    if (end >= r.end) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *complete = end < stop;
    *mutf8 = r.data + start;
    *length = end - start;
    *utf16_size = units;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_dex_string_data(const xxbyte_dex_file *dex, uint32_t string_idx, const uint8_t **mutf8, size_t *length, uint32_t *utf16_size)
{
    const uint8_t *s = NULL;
    size_t n = 0;
    uint32_t units = 0;
    int complete = 0;
    xxbyte_status status = dex_string_scan(dex, string_idx, (size_t)-1, &s, &n, &units, &complete);

    if (status == XXBYTE_STATUS_OK) {
        if (mutf8 != NULL) {
            *mutf8 = s;
        }
        if (length != NULL) {
            *length = n;
        }
        if (utf16_size != NULL) {
            *utf16_size = units;
        }
    }
    return status;
}

xxbyte_status xxbyte_dex_type(const xxbyte_dex_file *dex, uint32_t type_idx, uint32_t *descriptor_idx)
{
    const uint8_t *item;
    xxbyte_status status = dex_item(dex, &dex->type_ids, type_idx, 4, &item);

    if (status == XXBYTE_STATUS_OK && descriptor_idx != NULL) {
        *descriptor_idx = xxbyte_u32le(item);
    }
    return status;
}

xxbyte_status xxbyte_dex_proto(const xxbyte_dex_file *dex, uint32_t proto_idx, xxbyte_dex_proto_id *proto)
{
    const uint8_t *item;
    xxbyte_status status = dex_item(dex, &dex->proto_ids, proto_idx, 12, &item);

    if (status == XXBYTE_STATUS_OK && proto != NULL) {
        proto->shorty_idx = xxbyte_u32le(item);
        proto->return_type_idx = xxbyte_u32le(item + 4);
        proto->parameters_off = xxbyte_u32le(item + 8);
    }
    return status;
}

xxbyte_status xxbyte_dex_type_list(const xxbyte_dex_file *dex, uint32_t offset, uint16_t *types, size_t capacity, size_t *count)
{
    uint32_t n;
    uint32_t i;

    if (!dex_valid(dex) || count == NULL || (types == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    if (offset == 0) {
        return XXBYTE_STATUS_OK;
    }
    if (!dex_range(dex, offset, 4)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    n = xxbyte_u32le(dex->data + offset);
    if (!dex_range(dex, (uint64_t)offset + 4u, 2u * (uint64_t)n)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    for (i = 0; i < n && i < capacity; ++i) {
        types[i] = xxbyte_u16le(dex->data + offset + 4u + 2u * (size_t)i);
    }
    *count = n;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_dex_field(const xxbyte_dex_file *dex, uint32_t field_idx, xxbyte_dex_field_id *field)
{
    const uint8_t *item;
    xxbyte_status status = dex_item(dex, &dex->field_ids, field_idx, 8, &item);

    if (status == XXBYTE_STATUS_OK && field != NULL) {
        field->class_idx = xxbyte_u16le(item);
        field->type_idx = xxbyte_u16le(item + 2);
        field->name_idx = xxbyte_u32le(item + 4);
    }
    return status;
}

xxbyte_status xxbyte_dex_method(const xxbyte_dex_file *dex, uint32_t method_idx, xxbyte_dex_method_id *method)
{
    const uint8_t *item;
    xxbyte_status status = dex_item(dex, &dex->method_ids, method_idx, 8, &item);

    if (status == XXBYTE_STATUS_OK && method != NULL) {
        method->class_idx = xxbyte_u16le(item);
        method->proto_idx = xxbyte_u16le(item + 2);
        method->name_idx = xxbyte_u32le(item + 4);
    }
    return status;
}

xxbyte_status xxbyte_dex_class(const xxbyte_dex_file *dex, uint32_t class_def_idx, xxbyte_dex_class_def *class_def)
{
    const uint8_t *item;
    xxbyte_status status = dex_item(dex, &dex->class_defs, class_def_idx, 32, &item);

    if (status == XXBYTE_STATUS_OK && class_def != NULL) {
        class_def->class_idx = xxbyte_u32le(item);
        class_def->access_flags = xxbyte_u32le(item + 4);
        class_def->superclass_idx = xxbyte_u32le(item + 8);
        class_def->interfaces_off = xxbyte_u32le(item + 12);
        class_def->source_file_idx = xxbyte_u32le(item + 16);
        class_def->annotations_off = xxbyte_u32le(item + 20);
        class_def->class_data_off = xxbyte_u32le(item + 24);
        class_def->static_values_off = xxbyte_u32le(item + 28);
    }
    return status;
}

xxbyte_status xxbyte_dex_method_handle_item(const xxbyte_dex_file *dex, uint32_t handle_idx, xxbyte_dex_method_handle *handle)
{
    const uint8_t *item;
    xxbyte_status status = dex_item(dex, &dex->method_handles, handle_idx, 8, &item);

    if (status == XXBYTE_STATUS_OK && handle != NULL) {
        handle->type = xxbyte_u16le(item);
        handle->member_idx = xxbyte_u16le(item + 4);
    }
    return status;
}

xxbyte_status xxbyte_dex_call_site(const xxbyte_dex_file *dex, uint32_t call_site_idx, uint32_t *offset)
{
    const uint8_t *item;
    xxbyte_status status = dex_item(dex, &dex->call_site_ids, call_site_idx, 4, &item);

    if (status == XXBYTE_STATUS_OK && offset != NULL) {
        *offset = xxbyte_u32le(item);
    }
    return status;
}

/* ---- class data ---- */

typedef struct dex_class_iter {
    dex_reader r;
    uint32_t sizes[4]; /* static fields, instance fields, direct, virtual */
    uint32_t list;     /* current list, 0..3 */
    uint32_t left;     /* items left in it */
    uint32_t index;    /* running index of the current list */
} dex_class_iter;

static xxbyte_status dex_class_iter_init(const xxbyte_dex_file *dex, uint32_t class_def_idx, dex_class_iter *it, uint32_t *class_data_off)
{
    xxbyte_dex_class_def def;
    xxbyte_status status;
    unsigned i;

    memset(it, 0, sizeof(*it));
    status = xxbyte_dex_class(dex, class_def_idx, &def);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    *class_data_off = def.class_data_off;
    if (def.class_data_off == 0) {
        it->list = 4;
        return XXBYTE_STATUS_OK;
    }
    dex_reader_init(&it->r, dex, def.class_data_off);
    for (i = 0; i < 4; ++i) {
        it->sizes[i] = dex_uleb(&it->r);
    }
    it->left = it->sizes[0];
    return it->r.status;
}

/* Next item of a list >= first_list. Returns 1 with the item, 0 at the end
 * or on failure (it->r.status). */
static int dex_class_iter_next(dex_class_iter *it, uint32_t *list, uint32_t *index, uint32_t *flags, uint32_t *code_off)
{
    while (it->list < 4 && it->left == 0) {
        ++it->list;
        it->index = 0;
        it->left = it->list < 4 ? it->sizes[it->list] : 0;
    }
    if (it->list >= 4 || it->r.status != XXBYTE_STATUS_OK) {
        return 0;
    }
    it->index += dex_uleb(&it->r);
    *flags = dex_uleb(&it->r);
    *code_off = it->list >= 2 ? dex_uleb(&it->r) : 0;
    if (it->r.status != XXBYTE_STATUS_OK) {
        return 0;
    }
    *list = it->list;
    *index = it->index;
    --it->left;
    return 1;
}

/* Skips the fields of the iterator. */
static int dex_class_iter_skip_fields(dex_class_iter *it)
{
    uint32_t list;
    uint32_t index;
    uint32_t flags;
    uint32_t code_off;

    while (it->list < 2) {
        if (it->left == 0) {
            ++it->list;
            it->index = 0;
            it->left = it->sizes[it->list];
            continue;
        }
        if (!dex_class_iter_next(it, &list, &index, &flags, &code_off)) {
            return 0;
        }
    }
    return 1;
}

xxbyte_status xxbyte_dex_class_data_header(const xxbyte_dex_file *dex, uint32_t class_def_idx, xxbyte_dex_class_data *class_data)
{
    dex_class_iter it;
    uint32_t offset = 0;
    xxbyte_status status;

    if (class_data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(class_data, 0, sizeof(*class_data));
    status = dex_class_iter_init(dex, class_def_idx, &it, &offset);
    if (status == XXBYTE_STATUS_OK) {
        class_data->static_fields_size = it.sizes[0];
        class_data->instance_fields_size = it.sizes[1];
        class_data->direct_methods_size = it.sizes[2];
        class_data->virtual_methods_size = it.sizes[3];
        class_data->offset = offset;
    }
    return status;
}

xxbyte_status xxbyte_dex_class_fields(const xxbyte_dex_file *dex, uint32_t class_def_idx, xxbyte_dex_field_def *fields, size_t capacity, size_t *count)
{
    dex_class_iter it;
    uint32_t offset = 0;
    uint32_t list;
    uint32_t index;
    uint32_t flags;
    uint32_t code_off;
    size_t n = 0;
    xxbyte_status status;

    if (count == NULL || (fields == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    status = dex_class_iter_init(dex, class_def_idx, &it, &offset);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    while (it.list < 2 && dex_class_iter_next(&it, &list, &index, &flags, &code_off)) {
        if (list >= 2) {
            break;
        }
        if (n < capacity) {
            fields[n].field_idx = index;
            fields[n].access_flags = flags;
            fields[n].is_instance = list;
        }
        ++n;
    }
    if (it.r.status != XXBYTE_STATUS_OK) {
        return it.r.status;
    }
    *count = n;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_dex_class_methods(const xxbyte_dex_file *dex, uint32_t class_def_idx, xxbyte_dex_method_def *methods, size_t capacity, size_t *count)
{
    dex_class_iter it;
    uint32_t offset = 0;
    uint32_t list;
    uint32_t index;
    uint32_t flags;
    uint32_t code_off;
    size_t n = 0;
    xxbyte_status status;

    if (count == NULL || (methods == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    status = dex_class_iter_init(dex, class_def_idx, &it, &offset);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (!dex_class_iter_skip_fields(&it)) {
        return it.r.status;
    }
    while (dex_class_iter_next(&it, &list, &index, &flags, &code_off)) {
        if (n < capacity) {
            methods[n].method_idx = index;
            methods[n].access_flags = flags;
            methods[n].code_off = code_off;
            methods[n].is_virtual = list == 3;
        }
        ++n;
    }
    if (it.r.status != XXBYTE_STATUS_OK) {
        return it.r.status;
    }
    *count = n;
    return XXBYTE_STATUS_OK;
}

/* ---- code items ---- */

/* Walks an encoded_catch_handler; *r is left after it. */
static void dex_skip_handler(dex_reader *r)
{
    int32_t size = dex_sleb(r);
    uint32_t pairs = size < 0 ? 0u - (uint32_t)size : (uint32_t)size;
    uint32_t i;

    for (i = 0; i < pairs && r->status == XXBYTE_STATUS_OK; ++i) {
        (void)dex_uleb(r);
        (void)dex_uleb(r);
    }
    if (size <= 0) {
        (void)dex_uleb(r);
    }
}

xxbyte_status xxbyte_dex_code(const xxbyte_dex_file *dex, uint32_t code_off, xxbyte_dex_code_item *item, uint32_t *size)
{
    const uint8_t *p;
    uint64_t end;
    uint64_t tries = 0;

    if (item == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(item, 0, sizeof(*item));
    if (!dex_valid(dex)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (!dex_range(dex, code_off, 16)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    p = dex->data + code_off;
    end = (uint64_t)code_off + 16u + 2u * (uint64_t)xxbyte_u32le(p + 12);
    if (xxbyte_u16le(p + 6) != 0) {
        tries = (end + 3u) & ~(uint64_t)3u;
        end = tries + 8u * (uint64_t)xxbyte_u16le(p + 6);
    }
    if (end > (uint64_t)dex->size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (tries != 0) {
        dex_reader r;
        uint32_t handlers;
        uint32_t i;

        dex_reader_init(&r, dex, (uint32_t)end);
        handlers = dex_uleb(&r);
        for (i = 0; i < handlers && r.status == XXBYTE_STATUS_OK; ++i) {
            dex_skip_handler(&r);
        }
        if (r.status != XXBYTE_STATUS_OK) {
            return r.status;
        }
        end = r.pos;
    }
    if (end - code_off > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    item->registers_size = xxbyte_u16le(p);
    item->ins_size = xxbyte_u16le(p + 2);
    item->outs_size = xxbyte_u16le(p + 4);
    item->tries_size = xxbyte_u16le(p + 6);
    item->debug_info_off = xxbyte_u32le(p + 8);
    item->insns_size = xxbyte_u32le(p + 12);
    item->insns_offset = code_off + 16u;
    item->tries_offset = (uint32_t)tries;
    if (size != NULL) {
        *size = (uint32_t)(end - code_off);
    }
    return XXBYTE_STATUS_OK;
}

static int dex_item_valid(const xxbyte_dex_file *dex, const xxbyte_dex_code_item *item)
{
    return item->tries_size == 0 || dex_range(dex, item->tries_offset, 8u * (uint64_t)item->tries_size);
}

xxbyte_status xxbyte_dex_tries(const xxbyte_dex_file *dex, const xxbyte_dex_code_item *item, xxbyte_dex_try *tries, size_t capacity, size_t *count)
{
    uint32_t i;

    if (!dex_valid(dex) || item == NULL || count == NULL || (tries == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    if (!dex_item_valid(dex, item)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    for (i = 0; i < item->tries_size && i < capacity; ++i) {
        const uint8_t *p = dex->data + item->tries_offset + 8u * (size_t)i;
        tries[i].start_addr = xxbyte_u32le(p);
        tries[i].insn_count = xxbyte_u16le(p + 4);
        tries[i].handler_off = xxbyte_u16le(p + 6);
    }
    *count = item->tries_size;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_dex_catches(const xxbyte_dex_file *dex, const xxbyte_dex_code_item *item, uint32_t handler_off, xxbyte_dex_catch *catches, size_t capacity,
                                 size_t *count)
{
    dex_reader r;
    int32_t size;
    uint32_t pairs;
    uint32_t i;
    size_t n = 0;
    uint64_t list;

    if (!dex_valid(dex) || item == NULL || count == NULL || (catches == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    if (item->tries_size == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!dex_item_valid(dex, item)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    list = (uint64_t)item->tries_offset + 8u * (uint64_t)item->tries_size;
    if (list + handler_off > (uint64_t)dex->size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    dex_reader_init(&r, dex, (uint32_t)(list + handler_off));
    size = dex_sleb(&r);
    pairs = size < 0 ? 0u - (uint32_t)size : (uint32_t)size;
    for (i = 0; i < pairs && r.status == XXBYTE_STATUS_OK; ++i) {
        uint32_t type = dex_uleb(&r);
        uint32_t addr = dex_uleb(&r);
        if (n < capacity) {
            catches[n].type_idx = type;
            catches[n].addr = addr;
        }
        ++n;
    }
    if (size <= 0) {
        uint32_t addr = dex_uleb(&r);
        if (n < capacity) {
            catches[n].type_idx = XXBYTE_DEX_NO_INDEX;
            catches[n].addr = addr;
        }
        ++n;
    }
    if (r.status != XXBYTE_STATUS_OK) {
        return r.status;
    }
    *count = n;
    return XXBYTE_STATUS_OK;
}

/* ---- text ---- */

typedef struct dex_text {
    xx_buf_t *out;
    size_t limit; /* cut once out->size passes it */
    int cut;
} dex_text;

static int dex_put(dex_text *t, const char *s)
{
    return xx_buf_append_str(t->out, s) ? 1 : 0;
}

static int dex_putc(dex_text *t, char c)
{
    return xx_buf_append_char(t->out, c) ? 1 : 0;
}

static int dex_full(dex_text *t)
{
    if (t->cut || t->out->size > t->limit) {
        t->cut = 1;
        return 1;
    }
    return 0;
}

/* The next UTF-16 code unit of MUTF-8 text s[0..length); *pos advances. */
static uint32_t dex_mutf8_unit(const uint8_t *s, size_t length, size_t *pos)
{
    size_t i = *pos;
    uint32_t b = s[i];

    if (b < 0x80u) {
        *pos = i + 1;
        return b;
    }
    if ((b & 0xE0u) == 0xC0u && i + 1 < length && (s[i + 1] & 0xC0u) == 0x80u) {
        *pos = i + 2;
        return ((b & 0x1Fu) << 6) | (s[i + 1] & 0x3Fu);
    }
    if ((b & 0xF0u) == 0xE0u && i + 2 < length && (s[i + 1] & 0xC0u) == 0x80u && (s[i + 2] & 0xC0u) == 0x80u) {
        *pos = i + 3;
        return ((b & 0x0Fu) << 12) | ((uint32_t)(s[i + 1] & 0x3Fu) << 6) | (s[i + 2] & 0x3Fu);
    }
    *pos = i + 1;
    return b;
}

static int dex_put_unit_escape(dex_text *t, uint32_t unit)
{
    return xx_buf_appendf(t->out, "\\u%04x", (unsigned)unit) ? 1 : 0;
}

static int dex_put_utf8(dex_text *t, uint32_t cp)
{
    char b[4];
    size_t n;

    if (cp < 0x80u) {
        b[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800u) {
        b[0] = (char)(0xC0u | (cp >> 6));
        b[1] = (char)(0x80u | (cp & 0x3Fu));
        n = 2;
    } else if (cp < 0x10000u) {
        b[0] = (char)(0xE0u | (cp >> 12));
        b[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        b[2] = (char)(0x80u | (cp & 0x3Fu));
        n = 3;
    } else {
        b[0] = (char)(0xF0u | (cp >> 18));
        b[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        b[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        b[3] = (char)(0x80u | (cp & 0x3Fu));
        n = 4;
    }
    return xx_buf_append(t->out, b, n) ? 1 : 0;
}

static int dex_put_mutf8(dex_text *t, const uint8_t *s, size_t length, int quoted)
{
    size_t pos = 0;

    if (quoted && !dex_putc(t, '"')) {
        return 0;
    }
    while (pos < length) {
        uint32_t unit = dex_mutf8_unit(s, length, &pos);
        int ok;

        if (dex_full(t)) {
            return 1;
        }
        if (quoted) {
            if (unit >= 0x20u && unit < 0x7Fu) {
                ok = (unit == '\'' || unit == '"' || unit == '\\') ? dex_putc(t, '\\') && dex_putc(t, (char)unit) : dex_putc(t, (char)unit);
            } else if (unit == '\n') {
                ok = dex_put(t, "\\n");
            } else if (unit == '\r') {
                ok = dex_put(t, "\\r");
            } else if (unit == '\t') {
                ok = dex_put(t, "\\t");
            } else {
                ok = dex_put_unit_escape(t, unit);
            }
        } else if (unit >= 0xD800u && unit < 0xDC00u) {
            size_t next = pos;
            uint32_t low = pos < length ? dex_mutf8_unit(s, length, &next) : 0;
            if (low >= 0xDC00u && low < 0xE000u) {
                pos = next;
                ok = dex_put_utf8(t, 0x10000u + ((unit - 0xD800u) << 10) + (low - 0xDC00u));
            } else {
                ok = dex_put_unit_escape(t, unit);
            }
        } else if (unit < 0x20u || (unit >= 0x7Fu && unit < 0xA0u) || (unit >= 0xDC00u && unit < 0xE000u)) {
            ok = dex_put_unit_escape(t, unit);
        } else {
            ok = dex_put_utf8(t, unit);
        }
        if (!ok) {
            return 0;
        }
    }
    return quoted ? dex_putc(t, '"') : 1;
}

/* Each dex_w_* returns XXBYTE_STATUS_OK, or the failure; on allocation
 * failure the buffer's sticky flag is set and OK may be returned. */

static xxbyte_status dex_w_string(dex_text *t, const xxbyte_dex_file *dex, uint32_t string_idx, int quoted)
{
    const uint8_t *s;
    size_t length;
    uint32_t units;
    int complete;
    /* A string longer than the text limit is cut anyway: do not scan the
     * rest of the image for its NUL each time it is referenced. */
    xxbyte_status status = dex_string_scan(dex, string_idx, DEX_REF_TEXT_LIMIT + 16u, &s, &length, &units, &complete);

    if (status == XXBYTE_STATUS_OK) {
        (void)dex_put_mutf8(t, s, length, quoted);
        if (!complete) {
            t->cut = 1;
        }
    }
    return status;
}

static xxbyte_status dex_w_type(dex_text *t, const xxbyte_dex_file *dex, uint32_t type_idx)
{
    uint32_t descriptor;
    xxbyte_status status = xxbyte_dex_type(dex, type_idx, &descriptor);

    return status == XXBYTE_STATUS_OK ? dex_w_string(t, dex, descriptor, 0) : status;
}

static xxbyte_status dex_w_proto(dex_text *t, const xxbyte_dex_file *dex, uint32_t proto_idx)
{
    xxbyte_dex_proto_id proto;
    size_t count = 0;
    size_t i;
    xxbyte_status status = xxbyte_dex_proto(dex, proto_idx, &proto);

    if (status == XXBYTE_STATUS_OK) {
        status = xxbyte_dex_type_list(dex, proto.parameters_off, NULL, 0, &count);
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    (void)dex_putc(t, '(');
    for (i = 0; i < count; ++i) {
        uint32_t type = xxbyte_u16le(dex->data + proto.parameters_off + 4u + 2u * i);
        if (dex_full(t)) {
            return XXBYTE_STATUS_OK;
        }
        status = dex_w_type(t, dex, type);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    (void)dex_putc(t, ')');
    return dex_w_type(t, dex, proto.return_type_idx);
}

static xxbyte_status dex_w_field(dex_text *t, const xxbyte_dex_file *dex, uint32_t field_idx, int full)
{
    xxbyte_dex_field_id field;
    xxbyte_status status = xxbyte_dex_field(dex, field_idx, &field);

    if (status == XXBYTE_STATUS_OK && full) {
        status = dex_w_type(t, dex, field.class_idx);
        (void)dex_put(t, "->");
    }
    if (status == XXBYTE_STATUS_OK) {
        status = dex_w_string(t, dex, field.name_idx, 0);
    }
    if (status == XXBYTE_STATUS_OK) {
        (void)dex_putc(t, ':');
        status = dex_w_type(t, dex, field.type_idx);
    }
    return status;
}

static xxbyte_status dex_w_method(dex_text *t, const xxbyte_dex_file *dex, uint32_t method_idx, int full)
{
    xxbyte_dex_method_id method;
    xxbyte_status status = xxbyte_dex_method(dex, method_idx, &method);

    if (status == XXBYTE_STATUS_OK && full) {
        status = dex_w_type(t, dex, method.class_idx);
        (void)dex_put(t, "->");
    }
    if (status == XXBYTE_STATUS_OK) {
        status = dex_w_string(t, dex, method.name_idx, 0);
    }
    return status == XXBYTE_STATUS_OK ? dex_w_proto(t, dex, method.proto_idx) : status;
}

static const char *const dex_handle_names[] = {
    "static-put", "static-get", "instance-put", "instance-get", "invoke-static", "invoke-instance", "invoke-constructor", "invoke-direct", "invoke-interface",
};

/* The member of a method handle; with prefix its type name and '@' first. */
static xxbyte_status dex_w_handle(dex_text *t, const xxbyte_dex_file *dex, uint32_t handle_idx, int prefix)
{
    xxbyte_dex_method_handle handle;
    xxbyte_status status = xxbyte_dex_method_handle_item(dex, handle_idx, &handle);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (handle.type > XXBYTE_DEX_METHOD_HANDLE_INVOKE_INTERFACE) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (prefix) {
        (void)dex_put(t, dex_handle_names[handle.type]);
        (void)dex_putc(t, '@');
    }
    return handle.type <= XXBYTE_DEX_METHOD_HANDLE_INSTANCE_GET ? dex_w_field(t, dex, handle.member_idx, 1) : dex_w_method(t, dex, handle.member_idx, 1);
}

static int dex_subnormal(double value, int is_float)
{
    double magnitude = value < 0 ? -value : value;

    return is_float ? magnitude < 1.17549435082228750797e-38 : magnitude < 2.22507385850720138309e-308;
}

/*
 * For a positive subnormal value whose shortest decimal has one digit and
 * the decimal exponent exponent (value ~ 0.d * 10^exponent): the integer
 * nearest to value * 10^(2 - exponent), in 10..100. value is scaled to its
 * integer significand exactly, then multiplied by ten and halved in an
 * order that keeps the intermediate result normal; the relative error stays
 * near 1e-13, far below what could move the rounding of a 2-digit result.
 */
static double dex_scaled(double value, int is_float, int exponent)
{
    int halves = is_float ? 149 : 1074;
    int tens = 2 - exponent;
    double t = value;
    int i;

    for (i = 0; i < halves; ++i) {
        t *= 2.0;
    }
    while (tens > 0 || halves > 0) {
        if (tens > 0 && (t < 1e200 || halves == 0)) {
            t *= 10.0;
            --tens;
        } else {
            t *= 0.5;
            --halves;
        }
    }
    while (tens < 0) {
        t /= 10.0;
        ++tens;
    }
    return t >= 0.0 && t < 1000.0 ? t : 0.0;
}

/* Java's Float.toString / Double.toString from the shortest digits. */
static int dex_w_java_float(dex_text *t, double value, int is_float)
{
    xx_buf_t tmp;
    xxbyte_fmt fmt;
    char digits[40];
    size_t ndigits = 0;
    int exponent = 0; /* value = 0.digits * 10^exponent */
    int negative = 0;
    int ok;
    size_t i;
    const char *s;

    if (value != value) {
        return dex_put(t, "NaN");
    }
    if (value == 0.0) {
        return dex_put(t, 1.0 / value < 0 ? "-0.0" : "0.0");
    }
    xx_buf_init(&tmp);
    fmt.out = &tmp;
    fmt.flags = 0;
    fmt.resolver = NULL;
    fmt.context = NULL;
    ok = is_float ? xxbyte_fmt_float(&fmt, (float)value) : xxbyte_fmt_double(&fmt, value);
    if (!ok || !xx_buf_ok(&tmp) || tmp.data == NULL) {
        xx_buf_free(&tmp);
        return 0;
    }
    s = tmp.data;
    if (*s == '-') {
        negative = 1;
        ++s;
    }
    if (strcmp(s, "inf") == 0) {
        xx_buf_free(&tmp);
        return dex_put(t, negative ? "-Infinity" : "Infinity");
    }
    /* value = 0.<all mantissa digits> * 10^(integer digits + e). */
    {
        int integer_digits = -1;
        int total = 0;
        int e = 0;
        int sign = 1;
        size_t lead = 0;

        for (; *s != '\0' && *s != 'e'; ++s) {
            if (*s == '.') {
                integer_digits = total;
            } else if (ndigits < sizeof(digits) - 1) {
                digits[ndigits++] = *s;
                ++total;
            }
        }
        if (integer_digits < 0) {
            integer_digits = total;
        }
        if (*s == 'e') {
            ++s;
            if (*s == '+' || *s == '-') {
                sign = *s == '-' ? -1 : 1;
                ++s;
            }
            for (; *s >= '0' && *s <= '9' && e < 10000; ++s) {
                e = e * 10 + (*s - '0');
            }
        }
        exponent = integer_digits + sign * e;
        while (lead < ndigits && digits[lead] == '0') {
            ++lead;
            --exponent;
        }
        memmove(digits, digits + lead, ndigits - lead);
        ndigits -= lead;
    }
    xx_buf_free(&tmp);
    while (ndigits > 1 && digits[ndigits - 1] == '0') {
        --ndigits;
    }
    if (ndigits == 0) {
        return dex_put(t, negative ? "-0.0" : "0.0");
    }
    if (ndigits == 1 && dex_subnormal(value, is_float)) {
        /* Java prints at least two digits and then takes the 2-digit
         * decimal closest to the value. That is d.0 for normal numbers;
         * subnormals with few significant bits can round elsewhere
         * (Double.MIN_VALUE is 4.9E-324, not 5.0E-324). */
        double scaled = dex_scaled(value < 0 ? -value : value, is_float, exponent);
        uint32_t two = (uint32_t)(scaled + 0.5);
        if (scaled < 10.0 && (uint32_t)(10.0 * scaled + 0.5) <= 99u) {
            /* The closest 2-digit decimal is one decade lower (2 *
             * Double.MIN_VALUE is 9.9E-324, its shortest form 1e-323). */
            --exponent;
            two = (uint32_t)(10.0 * scaled + 0.5);
        }
        if (two >= 100u) {
            two /= 10u;
            ++exponent;
        }
        if (two >= 10u) {
            digits[0] = (char)('0' + two / 10u);
            digits[1] = (char)('0' + two % 10u);
            ndigits = digits[1] == '0' ? 1 : 2;
        }
    }
    digits[ndigits] = '\0';
    if (negative && !dex_putc(t, '-')) {
        return 0;
    }
    if (exponent >= -2 && exponent <= 7) {
        /* 10^-3 <= |v| < 10^7: plain, with at least one fraction digit. */
        if (exponent <= 0) {
            if (!dex_put(t, "0.")) {
                return 0;
            }
            for (i = 0; i < (size_t)(-exponent); ++i) {
                if (!dex_putc(t, '0')) {
                    return 0;
                }
            }
            return dex_put(t, digits);
        }
        for (i = 0; i < (size_t)exponent; ++i) {
            if (!dex_putc(t, i < ndigits ? digits[i] : '0')) {
                return 0;
            }
        }
        if (!dex_putc(t, '.')) {
            return 0;
        }
        return (size_t)exponent < ndigits ? dex_put(t, digits + exponent) : dex_putc(t, '0');
    }
    if (!dex_putc(t, digits[0]) || !dex_putc(t, '.') || !(ndigits > 1 ? dex_put(t, digits + 1) : dex_putc(t, '0'))) {
        return 0;
    }
    return xx_buf_appendf(t->out, "E%d", exponent - 1) ? 1 : 0;
}

/* Encoded value types. */
#define DEX_VALUE_BYTE 0x00u
#define DEX_VALUE_SHORT 0x02u
#define DEX_VALUE_CHAR 0x03u
#define DEX_VALUE_INT 0x04u
#define DEX_VALUE_LONG 0x06u
#define DEX_VALUE_FLOAT 0x10u
#define DEX_VALUE_DOUBLE 0x11u
#define DEX_VALUE_METHOD_TYPE 0x15u
#define DEX_VALUE_METHOD_HANDLE 0x16u
#define DEX_VALUE_STRING 0x17u
#define DEX_VALUE_TYPE 0x18u
#define DEX_VALUE_FIELD 0x19u
#define DEX_VALUE_METHOD 0x1Au
#define DEX_VALUE_ENUM 0x1Bu
#define DEX_VALUE_ARRAY 0x1Cu
#define DEX_VALUE_ANNOTATION 0x1Du
#define DEX_VALUE_NULL 0x1Eu
#define DEX_VALUE_BOOLEAN 0x1Fu

/* Reads size little-endian bytes. */
static uint64_t dex_value_bytes(dex_reader *r, uint32_t size)
{
    uint64_t value = 0;
    uint32_t i;

    for (i = 0; i < size; ++i) {
        value |= (uint64_t)dex_u8(r) << (8u * i);
    }
    return value;
}

static xxbyte_status dex_w_value(dex_text *t, const xxbyte_dex_file *dex, dex_reader *r, unsigned depth);

static xxbyte_status dex_w_array_body(dex_text *t, const xxbyte_dex_file *dex, dex_reader *r, unsigned depth)
{
    uint32_t count = dex_uleb(r);
    uint32_t i;

    for (i = 0; i < count && r->status == XXBYTE_STATUS_OK; ++i) {
        xxbyte_status status;
        if (dex_full(t)) {
            return XXBYTE_STATUS_OK;
        }
        if (i != 0) {
            (void)dex_put(t, ", ");
        }
        status = dex_w_value(t, dex, r, depth + 1);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return r->status;
}

static xxbyte_status dex_w_value(dex_text *t, const xxbyte_dex_file *dex, dex_reader *r, unsigned depth)
{
    uint32_t head = dex_u8(r);
    uint32_t type = head & 0x1Fu;
    uint32_t arg = head >> 5;
    uint32_t size = arg + 1u;
    uint32_t max = 0; /* largest valid arg */
    uint64_t raw;
    xxbyte_status status = XXBYTE_STATUS_OK;

    if (r->status != XXBYTE_STATUS_OK) {
        return r->status;
    }
    if (depth > DEX_VALUE_DEPTH) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    switch (type) {
        case DEX_VALUE_BYTE: max = 0; break;
        case DEX_VALUE_SHORT:
        case DEX_VALUE_CHAR: max = 1; break;
        case DEX_VALUE_INT:
        case DEX_VALUE_FLOAT:
        case DEX_VALUE_METHOD_TYPE:
        case DEX_VALUE_METHOD_HANDLE:
        case DEX_VALUE_STRING:
        case DEX_VALUE_TYPE:
        case DEX_VALUE_FIELD:
        case DEX_VALUE_METHOD:
        case DEX_VALUE_ENUM: max = 3; break;
        case DEX_VALUE_LONG:
        case DEX_VALUE_DOUBLE: max = 7; break;
        case DEX_VALUE_ARRAY:
        case DEX_VALUE_ANNOTATION:
        case DEX_VALUE_NULL: max = 0; break;
        case DEX_VALUE_BOOLEAN: max = 1; break;
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (arg > max) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    switch (type) {
        case DEX_VALUE_BYTE:
        case DEX_VALUE_SHORT:
        case DEX_VALUE_INT:
        case DEX_VALUE_LONG: {
            static const unsigned widths[] = {8, 0, 16, 0, 32, 0, 64};
            unsigned bits = widths[type];
            raw = dex_value_bytes(r, size);
            raw = (uint64_t)xxbyte_sext(raw, 8u * size);
            if (bits < 64) {
                raw &= (UINT64_C(1) << bits) - 1u;
            }
            (void)xx_buf_appendf(t->out, "0x%llx", (unsigned long long)raw);
            break;
        }
        case DEX_VALUE_CHAR:
            raw = dex_value_bytes(r, size);
            (void)xx_buf_appendf(t->out, "0x%llx", (unsigned long long)raw);
            break;
        case DEX_VALUE_FLOAT: {
            uint32_t bits = (uint32_t)(dex_value_bytes(r, size) << (8u * (4u - size)));
            float f;
            memcpy(&f, &bits, sizeof(f));
            if (r->status == XXBYTE_STATUS_OK) {
                (void)dex_w_java_float(t, (double)f, 1);
            }
            break;
        }
        case DEX_VALUE_DOUBLE: {
            uint64_t bits = dex_value_bytes(r, size) << (8u * (8u - size));
            double d;
            memcpy(&d, &bits, sizeof(d));
            if (r->status == XXBYTE_STATUS_OK) {
                (void)dex_w_java_float(t, d, 0);
            }
            break;
        }
        case DEX_VALUE_METHOD_TYPE:
        case DEX_VALUE_METHOD_HANDLE:
        case DEX_VALUE_STRING:
        case DEX_VALUE_TYPE:
        case DEX_VALUE_FIELD:
        case DEX_VALUE_METHOD:
        case DEX_VALUE_ENUM: {
            uint32_t index = (uint32_t)dex_value_bytes(r, size);
            if (r->status != XXBYTE_STATUS_OK) {
                return r->status;
            }
            if (type == DEX_VALUE_METHOD_TYPE) {
                status = dex_w_proto(t, dex, index);
            } else if (type == DEX_VALUE_METHOD_HANDLE) {
                status = dex_w_handle(t, dex, index, 1);
            } else if (type == DEX_VALUE_STRING) {
                status = dex_w_string(t, dex, index, 1);
            } else if (type == DEX_VALUE_TYPE) {
                status = dex_w_type(t, dex, index);
            } else if (type == DEX_VALUE_METHOD) {
                status = dex_w_method(t, dex, index, 1);
            } else {
                status = dex_w_field(t, dex, index, 1);
            }
            break;
        }
        case DEX_VALUE_ARRAY:
            (void)dex_put(t, "Array[");
            status = dex_w_array_body(t, dex, r, depth);
            (void)dex_putc(t, ']');
            break;
        case DEX_VALUE_ANNOTATION: {
            uint32_t type_idx = dex_uleb(r);
            uint32_t count = dex_uleb(r);
            uint32_t i;
            if (r->status != XXBYTE_STATUS_OK) {
                return r->status;
            }
            (void)dex_put(t, "Annotation[");
            status = dex_w_type(t, dex, type_idx);
            for (i = 0; i < count && status == XXBYTE_STATUS_OK && r->status == XXBYTE_STATUS_OK; ++i) {
                uint32_t name = dex_uleb(r);
                if (r->status != XXBYTE_STATUS_OK || dex_full(t)) {
                    break;
                }
                (void)dex_put(t, ", ");
                status = dex_w_string(t, dex, name, 0);
                if (status == XXBYTE_STATUS_OK) {
                    (void)dex_putc(t, '=');
                    status = dex_w_value(t, dex, r, depth + 1);
                }
            }
            (void)dex_putc(t, ']');
            break;
        }
        case DEX_VALUE_NULL: (void)dex_put(t, "null"); break;
        default: (void)dex_put(t, arg != 0 ? "true" : "false"); break;
    }
    if (status == XXBYTE_STATUS_OK) {
        status = r->status;
    }
    return status;
}

/* Reads one encoded value that must be of type expect and returns its
 * index. */
static xxbyte_status dex_read_index_value(dex_reader *r, uint32_t expect, uint32_t *index)
{
    uint32_t head = dex_u8(r);

    if (r->status != XXBYTE_STATUS_OK) {
        return r->status;
    }
    if ((head & 0x1Fu) != expect || (head >> 5) > 3u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *index = (uint32_t)dex_value_bytes(r, (head >> 5) + 1u);
    return r->status;
}

static xxbyte_status dex_w_call_site(dex_text *t, const xxbyte_dex_file *dex, uint32_t call_site_idx)
{
    uint32_t offset;
    uint32_t count;
    uint32_t handle;
    uint32_t name;
    uint32_t proto;
    uint32_t i;
    dex_reader r;
    xxbyte_status status = xxbyte_dex_call_site(dex, call_site_idx, &offset);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    dex_reader_init(&r, dex, offset);
    count = dex_uleb(&r);
    if (r.status != XXBYTE_STATUS_OK) {
        return r.status;
    }
    if (count < 3) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if ((status = dex_read_index_value(&r, DEX_VALUE_METHOD_HANDLE, &handle)) != XXBYTE_STATUS_OK ||
        (status = dex_read_index_value(&r, DEX_VALUE_STRING, &name)) != XXBYTE_STATUS_OK ||
        (status = dex_read_index_value(&r, DEX_VALUE_METHOD_TYPE, &proto)) != XXBYTE_STATUS_OK) {
        return status;
    }
    (void)xx_buf_appendf(t->out, "call_site_%u(", (unsigned)call_site_idx);
    status = dex_w_string(t, dex, name, 1);
    if (status == XXBYTE_STATUS_OK) {
        (void)dex_put(t, ", ");
        status = dex_w_proto(t, dex, proto);
    }
    for (i = 3; i < count && status == XXBYTE_STATUS_OK; ++i) {
        if (dex_full(t)) {
            return XXBYTE_STATUS_OK;
        }
        (void)dex_put(t, ", ");
        status = dex_w_value(t, dex, &r, 0);
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    (void)dex_put(t, ")@");
    return dex_w_handle(t, dex, handle, 0);
}

static xxbyte_status dex_w_ref(dex_text *t, const xxbyte_dex_file *dex, uint32_t kind, uint32_t index)
{
    switch (kind) {
        case XXBYTE_INDEX_STRING: return dex_w_string(t, dex, index, 1);
        case XXBYTE_DEX_REF_STRING_TEXT: return dex_w_string(t, dex, index, 0);
        case XXBYTE_INDEX_TYPE: return dex_w_type(t, dex, index);
        case XXBYTE_INDEX_FIELD: return dex_w_field(t, dex, index, 1);
        case XXBYTE_DEX_REF_FIELD_SHORT: return dex_w_field(t, dex, index, 0);
        case XXBYTE_INDEX_METHOD: return dex_w_method(t, dex, index, 1);
        case XXBYTE_DEX_REF_METHOD_SHORT: return dex_w_method(t, dex, index, 0);
        case XXBYTE_INDEX_PROTO: return dex_w_proto(t, dex, index);
        case XXBYTE_INDEX_METHOD_HANDLE: return dex_w_handle(t, dex, index, 1);
        case XXBYTE_INDEX_CALL_SITE: return dex_w_call_site(t, dex, index);
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
}

/* Appends the reference to out (on failure out is rewound). */
static xxbyte_status dex_append_ref(xx_buf_t *out, const xxbyte_dex_file *dex, uint32_t kind, uint32_t index)
{
    dex_text t;
    size_t mark = out->size;
    xxbyte_status status;

    t.out = out;
    t.limit = mark + DEX_REF_TEXT_LIMIT;
    t.cut = 0;
    status = dex_w_ref(&t, dex, kind, index);
    if (status == XXBYTE_STATUS_OK && t.cut) {
        if (out->size > t.limit) {
            out->size = t.limit;
            if (out->data != NULL) {
                out->data[out->size] = '\0';
            }
        }
        (void)xx_buf_append_str(out, "...");
    }
    if (status != XXBYTE_STATUS_OK && xx_buf_ok(out)) {
        out->size = mark;
        if (out->data != NULL) {
            out->data[mark] = '\0';
        }
    }
    if (status == XXBYTE_STATUS_OK && !xx_buf_ok(out)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return status;
}

static size_t dex_copy_out(const xx_buf_t *text, char *buffer, size_t buffer_size)
{
    if (buffer_size != 0) {
        size_t n = text->size < buffer_size - 1 ? text->size : buffer_size - 1;
        if (n != 0) {
            memcpy(buffer, text->data, n);
        }
        buffer[n] = '\0';
    }
    return text->size;
}

xxbyte_status xxbyte_dex_format_ref(const xxbyte_dex_file *dex, uint32_t kind, uint32_t index, char *buffer, size_t buffer_size, size_t *length)
{
    xx_buf_t text;
    xxbyte_status status;
    size_t n;

    if (length != NULL) {
        *length = 0;
    }
    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if (!dex_valid(dex) || (buffer == NULL && buffer_size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    xx_buf_init(&text);
    status = dex_append_ref(&text, dex, kind, index);
    if (status == XXBYTE_STATUS_OK) {
        n = dex_copy_out(&text, buffer, buffer_size);
        if (length != NULL) {
            *length = n;
        }
    }
    xx_buf_free(&text);
    return status;
}

xxbyte_status xxbyte_dex_string(const xxbyte_dex_file *dex, uint32_t string_idx, uint32_t flags, char *buffer, size_t buffer_size, size_t *length)
{
    if ((flags & ~(uint32_t)XXBYTE_DEX_TEXT_QUOTED) != 0) {
        if (length != NULL) {
            *length = 0;
        }
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return xxbyte_dex_format_ref(dex, (flags & XXBYTE_DEX_TEXT_QUOTED) != 0 ? (uint32_t)XXBYTE_INDEX_STRING : (uint32_t)XXBYTE_DEX_REF_STRING_TEXT, string_idx, buffer,
                                 buffer_size, length);
}

typedef struct dex_flag_name {
    uint32_t bit;
    uint8_t which; /* 1 class, 2 method, 4 field */
    const char *name;
} dex_flag_name;

static const dex_flag_name dex_flag_names[] = {
    {0x00001u, 7, "public"},
    {0x00002u, 7, "private"},
    {0x00004u, 7, "protected"},
    {0x00008u, 7, "static"},
    {0x00010u, 7, "final"},
    {0x00020u, 2, "synchronized"},
    {0x00040u, 4, "volatile"},
    {0x00040u, 2, "bridge"},
    {0x00080u, 4, "transient"},
    {0x00080u, 2, "varargs"},
    {0x00100u, 2, "native"},
    {0x00200u, 1, "interface"},
    {0x00400u, 3, "abstract"},
    {0x00800u, 2, "strictfp"},
    {0x01000u, 7, "synthetic"},
    {0x02000u, 1, "annotation"},
    {0x04000u, 5, "enum"},
    {0x10000u, 2, "constructor"},
    {0x20000u, 2, "declared-synchronized"},
};

static int dex_put_flags(xx_buf_t *out, uint32_t flags, uint32_t which)
{
    uint8_t mask = (uint8_t)(1u << which);
    size_t i;
    int first = 1;

    for (i = 0; i < sizeof(dex_flag_names) / sizeof(dex_flag_names[0]); ++i) {
        if ((flags & dex_flag_names[i].bit) != 0 && (dex_flag_names[i].which & mask) != 0) {
            if ((!first && !xx_buf_append_char(out, ' ')) || !xx_buf_append_str(out, dex_flag_names[i].name)) {
                return 0;
            }
            first = 0;
        }
    }
    return 1;
}

size_t xxbyte_dex_access_flags_text(uint32_t access_flags, uint32_t which, char *buffer, size_t buffer_size)
{
    xx_buf_t text;
    size_t n = 0;

    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if (which > XXBYTE_DEX_FLAGS_FIELD || (buffer == NULL && buffer_size != 0)) {
        return 0;
    }
    xx_buf_init(&text);
    if (dex_put_flags(&text, access_flags, which) && xx_buf_ok(&text)) {
        n = dex_copy_out(&text, buffer, buffer_size);
    }
    xx_buf_free(&text);
    return n;
}

/* ---- resolver ---- */

static int dex_resolvable(const xxbyte_operand *operand)
{
    if (operand->type != XXBYTE_OPERAND_INDEX) {
        return 0;
    }
    switch (operand->kind) {
        case XXBYTE_INDEX_STRING:
        case XXBYTE_INDEX_TYPE:
        case XXBYTE_INDEX_FIELD:
        case XXBYTE_INDEX_METHOD:
        case XXBYTE_INDEX_PROTO:
        case XXBYTE_INDEX_CALL_SITE:
        case XXBYTE_INDEX_METHOD_HANDLE: return operand->value >= 0 && operand->value <= (int64_t)UINT32_MAX;
        default: return 0;
    }
}

size_t xxbyte_dex_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_dex_file *dex = (const xxbyte_dex_file *)context;
    const xxbyte_operand *operand;
    size_t length = 0;

    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if (!dex_valid(dex) || instruction == NULL || instruction->family != XXBYTE_FAMILY_DEX || operand_index >= instruction->operand_count ||
        operand_index >= XXBYTE_MAX_OPERANDS) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (!dex_resolvable(operand) || xxbyte_dex_format_ref(dex, operand->kind, (uint32_t)operand->value, buffer, buffer_size, &length) != XXBYTE_STATUS_OK) {
        return 0;
    }
    return length;
}

/* ---- listing ---- */

#define DEX_LISTING_BYTES 8u
/* list_file output limit: base + factor * image size. */
#define DEX_LISTING_LIMIT_BASE ((size_t)64u * 1024u * 1024u)
#define DEX_LISTING_LIMIT_FACTOR ((size_t)64u)
#define DEX_LISTING_BYTES_WIDTH (DEX_LISTING_BYTES * 3u - 1u)

/* The address and bytes columns, as xxbyte_disasm_buf writes them. */
static int dex_list_prefix(xx_buf_t *out, uint32_t listing_flags, uint64_t address, const uint8_t *bytes, size_t size)
{
    size_t i;
    size_t shown = size < DEX_LISTING_BYTES ? size : DEX_LISTING_BYTES;
    size_t width = 0;

    if ((listing_flags & XXBYTE_LISTING_NO_ADDRESS) == 0 && !xx_buf_appendf(out, "%08llX  ", (unsigned long long)address)) {
        return 0;
    }
    if ((listing_flags & XXBYTE_LISTING_NO_BYTES) != 0) {
        return 1;
    }
    for (i = 0; i < shown; ++i) {
        if (i + 1 == DEX_LISTING_BYTES && size > DEX_LISTING_BYTES) {
            if (!xx_buf_append_str(out, "..")) {
                return 0;
            }
            width += 2;
            break;
        }
        if (!xx_buf_appendf(out, i == 0 ? "%02X" : " %02X", bytes[i])) {
            return 0;
        }
        width += i == 0 ? 2 : 3;
    }
    for (; width < DEX_LISTING_BYTES_WIDTH + 2; ++width) {
        if (!xx_buf_append_char(out, ' ')) {
            return 0;
        }
    }
    return 1;
}

static int dex_list_indent(xx_buf_t *out, uint32_t listing_flags)
{
    size_t width = 0;

    if ((listing_flags & XXBYTE_LISTING_NO_ADDRESS) == 0) {
        width += 10;
    }
    if ((listing_flags & XXBYTE_LISTING_NO_BYTES) == 0) {
        width += DEX_LISTING_BYTES_WIDTH + 2;
    }
    for (; width != 0; --width) {
        if (!xx_buf_append_char(out, ' ')) {
            return 0;
        }
    }
    return 1;
}

static const char *dex_numeric_kind(uint32_t kind)
{
    switch (kind) {
        case XXBYTE_INDEX_STRING: return "string";
        case XXBYTE_INDEX_TYPE: return "type";
        case XXBYTE_INDEX_FIELD: return "field";
        case XXBYTE_INDEX_METHOD: return "method";
        case XXBYTE_INDEX_PROTO: return "proto";
        case XXBYTE_INDEX_CALL_SITE: return "call_site";
        default: return "method_handle";
    }
}

/* "kind@hhhh" as the Dalvik formatter prints an INDEX operand. */
static size_t dex_token(char *token, size_t size, const char *kind, uint64_t value)
{
    static const char hex[] = "0123456789abcdef";
    char digits[16];
    size_t n = 0;
    size_t length = strlen(kind);
    size_t i;

    do {
        digits[n++] = hex[value & 0xFu];
        value >>= 4;
    } while (value != 0 || n < 4);
    if (length + 1 + n >= size) {
        return 0;
    }
    memcpy(token, kind, length);
    token[length++] = '@';
    for (i = n; i-- > 0;) {
        token[length++] = digits[i];
    }
    token[length] = '\0';
    return length;
}

/*
 * Appends the text of instruction with every resolvable reference replaced
 * by its full text: the numeric text is formatted first and each kind@hhhh
 * token of an INDEX operand is replaced in operand order.
 */
static int dex_list_text(xx_buf_t *out, const xxbyte_dex_file *dex, const xxbyte_instruction *instruction, uint32_t format_flags)
{
    xx_buf_t numeric;
    size_t pos = 0;
    uint32_t i;
    int ok;

    xx_buf_init(&numeric);
    ok = xxbyte_format_buf(instruction, format_flags, NULL, NULL, &numeric) && xx_buf_ok(&numeric) && numeric.data != NULL;
    for (i = 0; ok && i < instruction->operand_count; ++i) {
        const xxbyte_operand *operand = &instruction->operands[i];
        char token[48];
        size_t token_length;
        const char *found;

        if (!dex_resolvable(operand)) {
            continue;
        }
        token_length = dex_token(token, sizeof(token), dex_numeric_kind(operand->kind), (uint64_t)operand->value);
        found = token_length != 0 ? strstr(numeric.data + pos, token) : NULL;
        if (found == NULL) {
            continue;
        }
        {
            size_t at = (size_t)(found - numeric.data);
            ok = xx_buf_append(out, numeric.data + pos, at - pos) ? 1 : 0;
            if (ok && dex_append_ref(out, dex, operand->kind, (uint32_t)operand->value) != XXBYTE_STATUS_OK) {
                /* Unresolved: keep the numeric token. */
                ok = xx_buf_ok(out) && xx_buf_append(out, token, token_length);
            }
            pos = at + token_length;
        }
    }
    if (ok) {
        ok = xx_buf_append(out, numeric.data + pos, numeric.size - pos) ? 1 : 0;
    }
    xx_buf_free(&numeric);
    return ok;
}

typedef struct dex_switch_ref {
    uint32_t payload; /* byte address of the payload */
    uint32_t sw;      /* byte address of the switch */
} dex_switch_ref;

static int dex_switch_ref_less(const dex_switch_ref *a, const dex_switch_ref *b)
{
    return a->payload < b->payload || (a->payload == b->payload && a->sw < b->sw);
}

/* Heap sort (no qsort, to stay independent of the C library). */
static void dex_sift(dex_switch_ref *refs, size_t root, size_t count)
{
    for (;;) {
        size_t child = 2 * root + 1;
        dex_switch_ref tmp;

        if (child >= count) {
            return;
        }
        if (child + 1 < count && dex_switch_ref_less(&refs[child], &refs[child + 1])) {
            ++child;
        }
        if (!dex_switch_ref_less(&refs[root], &refs[child])) {
            return;
        }
        tmp = refs[root];
        refs[root] = refs[child];
        refs[child] = tmp;
        root = child;
    }
}

static void dex_sort_refs(dex_switch_ref *refs, size_t count)
{
    size_t i;

    for (i = count / 2; i-- > 0;) {
        dex_sift(refs, i, count);
    }
    for (i = count; i-- > 1;) {
        dex_switch_ref tmp = refs[0];
        refs[0] = refs[i];
        refs[i] = tmp;
        dex_sift(refs, 0, i);
    }
}

/* The first switch referencing payload, or 0 with *found 0. */
static uint32_t dex_find_switch(const dex_switch_ref *refs, size_t count, uint32_t payload, int *found)
{
    size_t lo = 0;
    size_t hi = count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (refs[mid].payload < payload) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    *found = lo < count && refs[lo].payload == payload;
    return *found ? refs[lo].sw : 0;
}

/* Linear sweep helper: decodes at offset, or returns the unit size to skip. */
static int dex_list_decode(uint32_t mode, const uint8_t *code, size_t size, size_t offset, xxbyte_instruction *instruction)
{
    return xxbyte_decode(XXBYTE_FAMILY_DEX, mode, code + offset, size - offset, offset, instruction) == XXBYTE_STATUS_OK;
}

static xxbyte_status dex_list_code(xx_buf_t *out, const xxbyte_dex_file *dex, uint32_t mode, const xxbyte_dex_code_item *item, uint32_t listing_flags, size_t limit)
{
    const uint8_t *code = dex->data + item->insns_offset;
    size_t size = 2u * (size_t)item->insns_size;
    uint32_t format_flags = (listing_flags & XXBYTE_LISTING_UPPERCASE) != 0 ? XXBYTE_FORMAT_UPPERCASE : XXBYTE_FORMAT_DEFAULT;
    dex_switch_ref *refs = NULL;
    size_t ref_count = 0;
    size_t offset;
    xxbyte_instruction instruction;
    int pass;

    /* Pass 0 counts the switches, pass 1 records them. */
    for (pass = 0; pass < 2; ++pass) {
        size_t n = 0;
        for (offset = 0; offset < size;) {
            if (dex_list_decode(mode, code, size, offset, &instruction)) {
                if ((instruction.id == XXBYTE_DEX_ID_PACKED_SWITCH || instruction.id == XXBYTE_DEX_ID_SPARSE_SWITCH) && instruction.operand_count == 2 &&
                    instruction.operands[1].type == XXBYTE_OPERAND_DATA && instruction.operands[1].value >= 0 && instruction.operands[1].value < (int64_t)size) {
                    if (pass == 1 && n < ref_count) {
                        refs[n].payload = (uint32_t)instruction.operands[1].value;
                        refs[n].sw = (uint32_t)offset;
                    }
                    ++n;
                }
                offset += instruction.size;
            } else {
                offset += 2;
            }
        }
        if (pass == 0) {
            if (n == 0) {
                break;
            }
            refs = (dex_switch_ref *)xx_mem_alloc(n * sizeof(*refs));
            if (refs == NULL) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            ref_count = n;
        }
    }
    if (refs != NULL) {
        dex_sort_refs(refs, ref_count);
    }
    for (offset = 0; offset < size;) {
        if (out->size > limit) {
            xx_mem_free(refs);
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        if (dex_list_decode(mode, code, size, offset, &instruction)) {
            if (!dex_list_prefix(out, listing_flags, offset, code + offset, instruction.size) || !dex_list_text(out, dex, &instruction, format_flags) ||
                !xx_buf_append_char(out, '\n')) {
                break;
            }
            if ((instruction.group & XXBYTE_GROUP_PAYLOAD) != 0 && (instruction.group & XXBYTE_GROUP_SWITCH) != 0) {
                int found = 0;
                uint32_t sw = dex_find_switch(refs, ref_count, (uint32_t)offset, &found);
                uint32_t i;
                for (i = 0; found && i < instruction.table_count; ++i) {
                    int64_t key = 0;
                    int64_t target = 0;
                    if (xxbyte_switch_entry(&instruction, i, &key, &target) != XXBYTE_STATUS_OK) {
                        break;
                    }
                    target += (int64_t)sw;
                    if (!dex_list_indent(out, listing_flags) || !xx_buf_append_str(out, "case ") ||
                        !xx_buf_appendf(out, key < 0 ? "-0x%llx" : "0x%llx", key < 0 ? 0ull - (unsigned long long)key : (unsigned long long)key) ||
                        !xx_buf_appendf(out, ": 0x%04llx\n", (unsigned long long)target)) {
                        break;
                    }
                }
            }
            offset += instruction.size;
        } else {
            size_t count = size - offset < 2 ? size - offset : 2;
            size_t i;
            if (!dex_list_prefix(out, listing_flags, offset, code + offset, count) || !xx_buf_append_str(out, "db")) {
                break;
            }
            for (i = 0; i < count; ++i) {
                (void)xx_buf_appendf(out, i == 0 ? " 0x%02X" : ", 0x%02X", code[offset + i]);
            }
            (void)xx_buf_append_char(out, '\n');
            offset += count;
        }
    }
    xx_mem_free(refs);
    return xx_buf_ok(out) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

/* Appends a reference, or its numeric kind@hhhh form. */
static void dex_list_ref(xx_buf_t *out, const xxbyte_dex_file *dex, uint32_t kind, uint32_t index)
{
    if (dex_append_ref(out, dex, kind, index) != XXBYTE_STATUS_OK && xx_buf_ok(out)) {
        uint32_t numeric = kind == XXBYTE_DEX_REF_METHOD_SHORT ? (uint32_t)XXBYTE_INDEX_METHOD : kind;
        (void)xx_buf_appendf(out, "%s@%04x", dex_numeric_kind(numeric), (unsigned)index);
    }
}

static void dex_list_catches(xx_buf_t *out, const xxbyte_dex_file *dex, const xxbyte_dex_code_item *item, xxbyte_status *first_error)
{
    uint32_t i;

    for (i = 0; i < item->tries_size; ++i) {
        const uint8_t *p = dex->data + item->tries_offset + 8u * (size_t)i;
        uint64_t start = 2u * (uint64_t)xxbyte_u32le(p);
        uint64_t end = start + 2u * (uint64_t)xxbyte_u16le(p + 4);
        xxbyte_dex_catch catches[16];
        size_t count = 0;
        size_t j;
        size_t shown;
        xxbyte_status status = xxbyte_dex_catches(dex, item, xxbyte_u16le(p + 6), catches, 16, &count);

        if (status == XXBYTE_STATUS_OK && count > 16) {
            /* Rare: read the whole list once more into a heap array. */
            xxbyte_dex_catch *all = (xxbyte_dex_catch *)xx_mem_alloc(count * sizeof(*all));
            if (all == NULL) {
                status = XXBYTE_STATUS_OUT_OF_MEMORY;
            } else {
                status = xxbyte_dex_catches(dex, item, xxbyte_u16le(p + 6), all, count, &count);
                for (j = 0; status == XXBYTE_STATUS_OK && j < count; ++j) {
                    if (all[j].type_idx == XXBYTE_DEX_NO_INDEX) {
                        (void)xx_buf_append_str(out, ".catchall");
                    } else {
                        (void)xx_buf_append_str(out, ".catch ");
                        dex_list_ref(out, dex, XXBYTE_INDEX_TYPE, all[j].type_idx);
                    }
                    (void)xx_buf_appendf(out, " {0x%04llx .. 0x%04llx} 0x%04llx\n", (unsigned long long)start, (unsigned long long)end,
                                         2ull * (unsigned long long)all[j].addr);
                }
                xx_mem_free(all);
                if (status == XXBYTE_STATUS_OK) {
                    continue;
                }
            }
        }
        if (status != XXBYTE_STATUS_OK) {
            (void)xx_buf_appendf(out, "# try {0x%04llx .. 0x%04llx}: %s\n", (unsigned long long)start, (unsigned long long)end, xxbyte_status_string(status));
            if (*first_error == XXBYTE_STATUS_OK) {
                *first_error = status;
            }
            continue;
        }
        shown = count;
        for (j = 0; j < shown; ++j) {
            if (catches[j].type_idx == XXBYTE_DEX_NO_INDEX) {
                (void)xx_buf_append_str(out, ".catchall");
            } else {
                (void)xx_buf_append_str(out, ".catch ");
                dex_list_ref(out, dex, XXBYTE_INDEX_TYPE, catches[j].type_idx);
            }
            (void)xx_buf_appendf(out, " {0x%04llx .. 0x%04llx} 0x%04llx\n", (unsigned long long)start, (unsigned long long)end,
                                 2ull * (unsigned long long)catches[j].addr);
        }
    }
}

static xxbyte_status dex_list_method(xx_buf_t *out, const xxbyte_dex_file *dex, uint32_t mode, uint32_t listing_flags, const xxbyte_dex_method_def *method,
                                     xxbyte_status *first_error, size_t limit)
{
    xxbyte_dex_code_item item;
    xxbyte_status status;

    (void)xx_buf_append_str(out, ".method ");
    if (!dex_put_flags(out, method->access_flags, XXBYTE_DEX_FLAGS_METHOD)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (out->size != 0 && out->data != NULL && out->data[out->size - 1] != ' ') {
        (void)xx_buf_append_char(out, ' ');
    }
    dex_list_ref(out, dex, XXBYTE_DEX_REF_METHOD_SHORT, method->method_idx);
    (void)xx_buf_append_str(out, "\n# ");
    dex_list_ref(out, dex, XXBYTE_INDEX_METHOD, method->method_idx);
    if (method->code_off == 0) {
        (void)xx_buf_append_str(out, ", no code\n.end method\n");
        return xx_buf_ok(out) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_dex_code(dex, method->code_off, &item, NULL);
    if (status != XXBYTE_STATUS_OK) {
        (void)xx_buf_appendf(out, ", code_item 0x%x: %s\n.end method\n", (unsigned)method->code_off, xxbyte_status_string(status));
        if (*first_error == XXBYTE_STATUS_OK) {
            *first_error = status;
        }
        return xx_buf_ok(out) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    (void)xx_buf_appendf(out, ", code_item 0x%x, registers %u, ins %u, outs %u, insns %u, tries %u\n", (unsigned)method->code_off, (unsigned)item.registers_size,
                         (unsigned)item.ins_size, (unsigned)item.outs_size, (unsigned)item.insns_size, (unsigned)item.tries_size);
    dex_list_catches(out, dex, &item, first_error);
    status = dex_list_code(out, dex, mode, &item, listing_flags, limit);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    (void)xx_buf_append_str(out, ".end method\n");
    return xx_buf_ok(out) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

xxbyte_status xxbyte_dex_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_dex_file dex;
    xxbyte_status first_error = XXBYTE_STATUS_OK;
    xxbyte_status status;
    uint32_t c;
    size_t limit;

    status = xxbyte_dex_open(data, size, &dex);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    /* Shared code_items and call sites referenced many times can make the
     * text of a crafted image grow without bound. */
    limit = dex.size <= ((size_t)-1 - DEX_LISTING_LIMIT_BASE) / DEX_LISTING_LIMIT_FACTOR ? DEX_LISTING_LIMIT_BASE + DEX_LISTING_LIMIT_FACTOR * dex.size : (size_t)-1;
    limit = out->size > (size_t)-1 - limit ? (size_t)-1 : out->size + limit;
    if (mode == XXBYTE_MODE_AUTO) {
        mode = dex.mode;
    }
    for (c = 0; c < dex.class_defs.count; ++c) {
        xxbyte_dex_class_def def;
        dex_class_iter it;
        uint32_t offset = 0;
        uint32_t list;
        xxbyte_dex_method_def method;

        (void)xxbyte_dex_class(&dex, c, &def);
        (void)xx_buf_append_str(out, ".class ");
        if (!dex_put_flags(out, def.access_flags, XXBYTE_DEX_FLAGS_CLASS)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        if (out->data != NULL && out->data[out->size - 1] != ' ') {
            (void)xx_buf_append_char(out, ' ');
        }
        dex_list_ref(out, &dex, XXBYTE_INDEX_TYPE, def.class_idx);
        (void)xx_buf_append_char(out, '\n');
        status = dex_class_iter_init(&dex, c, &it, &offset);
        if (status == XXBYTE_STATUS_OK && !dex_class_iter_skip_fields(&it)) {
            status = it.r.status;
        }
        while (status == XXBYTE_STATUS_OK && dex_class_iter_next(&it, &list, &method.method_idx, &method.access_flags, &method.code_off)) {
            method.is_virtual = list == 3;
            status = dex_list_method(out, &dex, mode, listing_flags, &method, &first_error, limit);
            if (status == XXBYTE_STATUS_OUT_OF_MEMORY || out->size > limit) {
                (void)xx_buf_append_str(out, "# listing stopped: output limit\n");
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
        if (status == XXBYTE_STATUS_OK) {
            status = it.r.status;
        }
        if (status != XXBYTE_STATUS_OK) {
            (void)xx_buf_appendf(out, "# class_data 0x%x: %s\n", (unsigned)offset, xxbyte_status_string(status));
            if (first_error == XXBYTE_STATUS_OK) {
                first_error = status;
            }
        }
        (void)xx_buf_append_char(out, '\n');
        if (!xx_buf_ok(out)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    return first_error;
}

#else
typedef int xxbyte_dex_file_disabled;
#endif
