/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_internal.h"

#if XXBYTE_USE_AVM2

/*
 * abcFile walking (AVM2 Overview chapter 4, avmplus AbcParser). Every table
 * entry takes at least one byte, so a walk ends after at most size steps.
 */

/* Offsets are 32-bit, so only this much of the input is looked at. */
#define MAX_INPUT ((size_t)UINT32_MAX)

/* method_info flags and the instance_info protected-namespace flag. */
#define METHOD_HAS_OPTIONAL 0x08u
#define METHOD_HAS_PARAM_NAMES 0x80u
#define CLASS_PROTECTED_NS 0x08u

typedef struct abc_reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
    xxbyte_status status; /* the first failure */
} abc_reader;

static void reader_init(abc_reader *reader, const uint8_t *data, size_t size, size_t pos)
{
    reader->data = data;
    reader->size = size < MAX_INPUT ? size : MAX_INPUT;
    reader->pos = pos;
    reader->status = XXBYTE_STATUS_OK;
}

static int fail(abc_reader *reader, xxbyte_status status)
{
    if (reader->status == XXBYTE_STATUS_OK) {
        reader->status = status;
    }
    return 0;
}

static int read_u30(abc_reader *reader, uint32_t *value)
{
    xxbyte_status status = xxbyte_avm2_read_u30(reader->data, reader->size, &reader->pos, value);
    return status == XXBYTE_STATUS_OK ? 1 : fail(reader, status);
}

/* An integer or uinteger pool entry: avmplus reads both with
 * AbcParser::readS32. */
static int read_s32(abc_reader *reader, int32_t *value)
{
    xxbyte_status status = xxbyte_avm2_read_s32(reader->data, reader->size, &reader->pos, value);
    return status == XXBYTE_STATUS_OK ? 1 : fail(reader, status);
}

static int read_u8(abc_reader *reader, uint8_t *value)
{
    if (reader->pos >= reader->size) {
        return fail(reader, XXBYTE_STATUS_TRUNCATED);
    }
    *value = reader->data[reader->pos++];
    return 1;
}

static int skip(abc_reader *reader, uint64_t count)
{
    if (count > reader->size - reader->pos) {
        return fail(reader, XXBYTE_STATUS_TRUNCATED);
    }
    reader->pos += (size_t)count;
    return 1;
}

/* count u30 values. */
static int skip_u30s(abc_reader *reader, uint64_t count)
{
    uint32_t value;

    while (count-- != 0) {
        if (!read_u30(reader, &value)) {
            return 0;
        }
    }
    return 1;
}

static int is_namespace_kind(uint8_t kind)
{
    switch (kind) {
        case 0x05: /* PrivateNs */
        case 0x08: /* Namespace */
        case 0x16: /* PackageNamespace */
        case 0x17: /* PackageInternalNs */
        case 0x18: /* ProtectedNamespace */
        case 0x19: /* ExplicitNamespace */
        case 0x1A: /* StaticProtectedNs */ return 1;
        default: return 0;
    }
}

static int skip_multiname(abc_reader *reader)
{
    uint8_t kind;
    uint32_t count;

    if (!read_u8(reader, &kind)) {
        return 0;
    }
    switch (kind) {
        case 0x07: /* QName: ns, name */
        case 0x0D: /* QNameA */
        case 0x09: /* Multiname: name, ns_set */
        case 0x0E: /* MultinameA */ return skip_u30s(reader, 2);
        case 0x0F: /* RTQName: name */
        case 0x10: /* RTQNameA */
        case 0x1B: /* MultinameL: ns_set */
        case 0x1C: /* MultinameLA */ return skip_u30s(reader, 1);
        case 0x11: /* RTQNameL */
        case 0x12: /* RTQNameLA */ return 1;
        case 0x1D: /* TypeName: qname, param_count, params */ return skip_u30s(reader, 1) && read_u30(reader, &count) && skip_u30s(reader, count);
        default: return fail(reader, XXBYTE_STATUS_INVALID_INSTRUCTION);
    }
}

static int skip_method_info(abc_reader *reader)
{
    uint32_t param_count;
    uint32_t option_count;
    uint8_t flags;

    if (!read_u30(reader, &param_count) || !skip_u30s(reader, 1u + (uint64_t)param_count + 1u) || !read_u8(reader, &flags)) {
        return 0;
    }
    if ((flags & METHOD_HAS_OPTIONAL) != 0) {
        uint32_t i;
        if (!read_u30(reader, &option_count)) {
            return 0;
        }
        for (i = 0; i < option_count; ++i) {
            if (!skip_u30s(reader, 1) || !skip(reader, 1)) {
                return 0;
            }
        }
    }
    if ((flags & METHOD_HAS_PARAM_NAMES) != 0) {
        return skip_u30s(reader, param_count);
    }
    return 1;
}

static int read_trait(abc_reader *reader, xxbyte_avm2_trait *trait)
{
    uint8_t tag;

    memset(trait, 0, sizeof(*trait));
    trait->offset = (uint32_t)reader->pos;
    if (!read_u30(reader, &trait->name) || !read_u8(reader, &tag)) {
        return 0;
    }
    trait->kind = (uint8_t)(tag & 0x0Fu);
    trait->attributes = (uint8_t)(tag >> 4);
    switch (trait->kind) {
        case XXBYTE_AVM2_TRAIT_SLOT:
        case XXBYTE_AVM2_TRAIT_CONST:
            if (!read_u30(reader, &trait->id) || !read_u30(reader, &trait->index) || !read_u30(reader, &trait->value_index) ||
                (trait->value_index != 0 && !read_u8(reader, &trait->value_kind))) {
                return 0;
            }
            break;
        case XXBYTE_AVM2_TRAIT_METHOD:
        case XXBYTE_AVM2_TRAIT_GETTER:
        case XXBYTE_AVM2_TRAIT_SETTER:
        case XXBYTE_AVM2_TRAIT_CLASS:
        case XXBYTE_AVM2_TRAIT_FUNCTION:
            if (!read_u30(reader, &trait->id) || !read_u30(reader, &trait->index)) {
                return 0;
            }
            break;
        default: return fail(reader, XXBYTE_STATUS_INVALID_INSTRUCTION);
    }
    if ((trait->attributes & XXBYTE_AVM2_TRAIT_ATTR_METADATA) != 0) {
        if (!read_u30(reader, &trait->metadata_count)) {
            return 0;
        }
        trait->metadata_offset = (uint32_t)reader->pos;
        if (!skip_u30s(reader, trait->metadata_count)) {
            return 0;
        }
    }
    trait->size = (uint32_t)(reader->pos - trait->offset);
    return 1;
}

static int skip_trait_list(abc_reader *reader, uint32_t count)
{
    xxbyte_avm2_trait trait;

    while (count-- != 0) {
        if (!read_trait(reader, &trait)) {
            return 0;
        }
    }
    return 1;
}

/* trait_count, then the traits. */
static int skip_traits(abc_reader *reader)
{
    uint32_t count;

    return read_u30(reader, &count) && skip_trait_list(reader, count);
}

static int skip_instance_info(abc_reader *reader)
{
    uint8_t flags;
    uint32_t interface_count;

    if (!skip_u30s(reader, 2) || !read_u8(reader, &flags) || ((flags & CLASS_PROTECTED_NS) != 0 && !skip_u30s(reader, 1)) || !read_u30(reader, &interface_count) ||
        !skip_u30s(reader, (uint64_t)interface_count + 1u)) {
        return 0;
    }
    return skip_traits(reader);
}

static int read_method_body(abc_reader *reader, xxbyte_avm2_method_body *body)
{
    memset(body, 0, sizeof(*body));
    body->offset = (uint32_t)reader->pos;
    if (!read_u30(reader, &body->method) || !read_u30(reader, &body->max_stack) || !read_u30(reader, &body->local_count) || !read_u30(reader, &body->init_scope_depth) ||
        !read_u30(reader, &body->max_scope_depth) || !read_u30(reader, &body->code_length)) {
        return 0;
    }
    body->code_offset = (uint32_t)reader->pos;
    if (!skip(reader, body->code_length) || !read_u30(reader, &body->exception_count)) {
        return 0;
    }
    body->exception_offset = (uint32_t)reader->pos;
    if (!skip_u30s(reader, (uint64_t)body->exception_count * 5u) || !read_u30(reader, &body->trait_count)) {
        return 0;
    }
    body->trait_offset = (uint32_t)reader->pos;
    if (!skip_trait_list(reader, body->trait_count)) {
        return 0;
    }
    body->size = (uint32_t)(reader->pos - body->offset);
    return 1;
}

/* One entry of table. */
static int skip_entry(abc_reader *reader, xxbyte_avm2_table table)
{
    uint32_t value;
    int32_t number;
    uint8_t kind;
    xxbyte_avm2_method_body body;

    switch (table) {
        case XXBYTE_AVM2_TABLE_INT:
        case XXBYTE_AVM2_TABLE_UINT: return read_s32(reader, &number);
        case XXBYTE_AVM2_TABLE_DOUBLE: return skip(reader, 8);
        case XXBYTE_AVM2_TABLE_FLOAT: return skip(reader, 4);
        case XXBYTE_AVM2_TABLE_FLOAT4: return skip(reader, 16);
        case XXBYTE_AVM2_TABLE_STRING: return read_u30(reader, &value) && skip(reader, value);
        case XXBYTE_AVM2_TABLE_NAMESPACE:
            if (!read_u8(reader, &kind)) {
                return 0;
            }
            if (!is_namespace_kind(kind)) {
                return fail(reader, XXBYTE_STATUS_INVALID_INSTRUCTION);
            }
            return skip_u30s(reader, 1);
        case XXBYTE_AVM2_TABLE_NS_SET: return read_u30(reader, &value) && skip_u30s(reader, value);
        case XXBYTE_AVM2_TABLE_MULTINAME: return skip_multiname(reader);
        case XXBYTE_AVM2_TABLE_METHOD: return skip_method_info(reader);
        case XXBYTE_AVM2_TABLE_METADATA: return skip_u30s(reader, 1) && read_u30(reader, &value) && skip_u30s(reader, (uint64_t)value * 2u);
        case XXBYTE_AVM2_TABLE_INSTANCE: return skip_instance_info(reader);
        case XXBYTE_AVM2_TABLE_CLASS:
        case XXBYTE_AVM2_TABLE_SCRIPT: return skip_u30s(reader, 1) && skip_traits(reader);
        case XXBYTE_AVM2_TABLE_METHOD_BODY: return read_method_body(reader, &body);
        default: return fail(reader, XXBYTE_STATUS_INVALID_ARGUMENT);
    }
}

static int is_pool(xxbyte_avm2_table table)
{
    return table <= XXBYTE_AVM2_TABLE_MULTINAME;
}

/* The number of entries stored in the file for an encoded count. */
static uint32_t stored_entries(xxbyte_avm2_table table, uint32_t count)
{
    return is_pool(table) && count != 0 ? count - 1u : count;
}

static int fixed_entry_size(xxbyte_avm2_table table)
{
    switch (table) {
        case XXBYTE_AVM2_TABLE_DOUBLE: return 8;
        case XXBYTE_AVM2_TABLE_FLOAT: return 4;
        case XXBYTE_AVM2_TABLE_FLOAT4: return 16;
        default: return 0;
    }
}

static int walk_table(abc_reader *reader, xxbyte_avm2_table table, uint32_t count, xxbyte_avm2_table_info *info)
{
    uint32_t entries = stored_entries(table, count);
    int fixed = fixed_entry_size(table);

    info->count = count;
    info->offset = (uint32_t)reader->pos;
    if (fixed != 0) {
        if (!skip(reader, (uint64_t)entries * (uint64_t)fixed)) {
            return 0;
        }
    } else {
        while (entries-- != 0) {
            if (!skip_entry(reader, table)) {
                return 0;
            }
        }
    }
    info->size = (uint32_t)(reader->pos - info->offset);
    return 1;
}

xxbyte_status xxbyte_avm2_parse_abc(const uint8_t *data, size_t size, xxbyte_avm2_abc *abc)
{
    xxbyte_avm2_abc parsed;
    abc_reader reader;
    int table;

    if (abc == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(abc, 0, sizeof(*abc));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    memset(&parsed, 0, sizeof(parsed));
    parsed.minor_version = xxbyte_u16le(data);
    parsed.major_version = xxbyte_u16le(data + 2);
    parsed.mode = XXBYTE_AVM2_MODE(parsed.major_version, parsed.minor_version);
    if (parsed.mode == XXBYTE_AVM2_MODE_DEFAULT || xxbyte_check_mode(XXBYTE_FAMILY_AVM2, parsed.mode) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    reader_init(&reader, data, size, 4);
    for (table = 0; table < (int)XXBYTE_AVM2_TABLE_COUNT; ++table) {
        uint32_t count = 0;
        xxbyte_avm2_table_info *info = &parsed.tables[table];

        if ((table == XXBYTE_AVM2_TABLE_FLOAT || table == XXBYTE_AVM2_TABLE_FLOAT4) && parsed.mode < XXBYTE_AVM2_MODE_FLOAT) {
            continue;
        }
        if (table == XXBYTE_AVM2_TABLE_CLASS) {
            count = parsed.tables[XXBYTE_AVM2_TABLE_INSTANCE].count;
        } else if (!read_u30(&reader, &count)) {
            return reader.status;
        }
        if (!walk_table(&reader, (xxbyte_avm2_table)table, count, info)) {
            return reader.status;
        }
    }
    parsed.size = (uint32_t)reader.pos;
    *abc = parsed;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm2_table_offsets(const uint8_t *data, size_t size, const xxbyte_avm2_abc *abc, xxbyte_avm2_table table, uint32_t *offsets, size_t capacity,
                                        size_t *count)
{
    const xxbyte_avm2_table_info *info;
    abc_reader reader;
    uint32_t i;
    uint32_t first;

    if (count != NULL) {
        *count = 0;
    }
    if (data == NULL || abc == NULL || count == NULL || (offsets == NULL && capacity != 0) || (unsigned)table >= (unsigned)XXBYTE_AVM2_TABLE_COUNT) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    info = &abc->tables[table];
    if ((uint64_t)info->offset + info->size > size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    first = is_pool(table) && info->count != 0 ? 1u : 0u;
    if (first != 0 && capacity != 0) {
        offsets[0] = 0;
    }
    reader_init(&reader, data, (size_t)info->offset + info->size, info->offset);
    for (i = first; i < info->count; ++i) {
        if (i < capacity) {
            offsets[i] = (uint32_t)reader.pos;
        }
        if (fixed_entry_size(table) != 0) {
            if (!skip(&reader, (uint64_t)fixed_entry_size(table))) {
                return reader.status;
            }
        } else if (!skip_entry(&reader, table)) {
            return reader.status;
        }
    }
    *count = info->count;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm2_read_string(const uint8_t *data, size_t size, size_t offset, const uint8_t **text, uint32_t *length)
{
    abc_reader reader;
    uint32_t value;

    if (text != NULL) {
        *text = NULL;
    }
    if (length != NULL) {
        *length = 0;
    }
    if (data == NULL || text == NULL || length == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    reader_init(&reader, data, size, offset);
    if (offset > reader.size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (!read_u30(&reader, &value) || !skip(&reader, value)) {
        return reader.status;
    }
    *text = data + reader.pos - value;
    *length = value;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm2_parse_method_body(const uint8_t *data, size_t size, size_t offset, xxbyte_avm2_method_body *body)
{
    abc_reader reader;
    xxbyte_avm2_method_body parsed;

    if (body == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(body, 0, sizeof(*body));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    reader_init(&reader, data, size, offset);
    if (offset > reader.size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (!read_method_body(&reader, &parsed)) {
        return reader.status;
    }
    *body = parsed;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm2_parse_method_bodies(const uint8_t *data, size_t size, const xxbyte_avm2_abc *abc, xxbyte_avm2_method_body *bodies, size_t capacity,
                                              size_t *count)
{
    const xxbyte_avm2_table_info *info;
    abc_reader reader;
    uint32_t i;

    if (count != NULL) {
        *count = 0;
    }
    if (data == NULL || abc == NULL || count == NULL || (bodies == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    info = &abc->tables[XXBYTE_AVM2_TABLE_METHOD_BODY];
    if ((uint64_t)info->offset + info->size > size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    reader_init(&reader, data, (size_t)info->offset + info->size, info->offset);
    for (i = 0; i < info->count; ++i) {
        xxbyte_avm2_method_body body;
        if (!read_method_body(&reader, &body)) {
            return reader.status;
        }
        if (i < capacity) {
            bodies[i] = body;
        }
    }
    *count = info->count;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm2_parse_exceptions(const uint8_t *data, size_t size, const xxbyte_avm2_method_body *body, xxbyte_avm2_exception *exceptions, size_t capacity,
                                           size_t *count)
{
    abc_reader reader;
    uint32_t i;

    if (count != NULL) {
        *count = 0;
    }
    if (data == NULL || body == NULL || count == NULL || (exceptions == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    reader_init(&reader, data, size, body->exception_offset);
    if (body->exception_offset > reader.size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    for (i = 0; i < body->exception_count; ++i) {
        xxbyte_avm2_exception entry;
        if (!read_u30(&reader, &entry.from) || !read_u30(&reader, &entry.to) || !read_u30(&reader, &entry.target) || !read_u30(&reader, &entry.exc_type) ||
            !read_u30(&reader, &entry.var_name)) {
            return reader.status;
        }
        if (i < capacity) {
            exceptions[i] = entry;
        }
    }
    *count = body->exception_count;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm2_parse_trait(const uint8_t *data, size_t size, size_t offset, xxbyte_avm2_trait *trait)
{
    abc_reader reader;
    xxbyte_avm2_trait parsed;

    if (trait == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(trait, 0, sizeof(*trait));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    reader_init(&reader, data, size, offset);
    if (offset > reader.size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (!read_trait(&reader, &parsed)) {
        return reader.status;
    }
    *trait = parsed;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm2_parse_method_info(const uint8_t *data, size_t size, size_t offset, xxbyte_avm2_method_info *method)
{
    abc_reader reader;
    xxbyte_avm2_method_info parsed;
    uint8_t flags;

    if (method == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(method, 0, sizeof(*method));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    reader_init(&reader, data, size, offset);
    if (offset > reader.size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    memset(&parsed, 0, sizeof(parsed));
    parsed.offset = (uint32_t)offset;
    if (!read_u30(&reader, &parsed.param_count) || !read_u30(&reader, &parsed.return_type)) {
        return reader.status;
    }
    parsed.param_types_offset = (uint32_t)reader.pos;
    if (!skip_u30s(&reader, parsed.param_count) || !read_u30(&reader, &parsed.name) || !read_u8(&reader, &flags)) {
        return reader.status;
    }
    parsed.flags = flags;
    if ((flags & METHOD_HAS_OPTIONAL) != 0) {
        uint32_t i;
        if (!read_u30(&reader, &parsed.option_count)) {
            return reader.status;
        }
        parsed.options_offset = (uint32_t)reader.pos;
        for (i = 0; i < parsed.option_count; ++i) {
            if (!skip_u30s(&reader, 1) || !skip(&reader, 1)) {
                return reader.status;
            }
        }
    }
    if ((flags & METHOD_HAS_PARAM_NAMES) != 0) {
        parsed.param_names_offset = (uint32_t)reader.pos;
        if (!skip_u30s(&reader, parsed.param_count)) {
            return reader.status;
        }
    }
    parsed.size = (uint32_t)(reader.pos - offset);
    *method = parsed;
    return XXBYTE_STATUS_OK;
}

#else
typedef int xxbyte_avm2_abc_disabled;
#endif
