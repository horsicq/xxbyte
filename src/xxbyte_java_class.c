/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_java_internal.h"

#if XXBYTE_USE_JAVA

#include <xxfclib/memory/xx_memory.h>

/* Class-file reader (JVMS SE 25 chapter 4). */

typedef struct cp_slot {
    uint8_t tag;
    uint8_t kind;
    uint16_t index1;
    uint16_t index2;
    uint32_t offset;
    uint32_t length;
    uint64_t value;
    uint32_t text;        /* Utf8: offset of the decoded text in the arena */
    uint32_t text_length; /* Utf8: decoded byte length */
} cp_slot;

struct xxbyte_java_class {
    const uint8_t *data;
    size_t size;
    xxbyte_java_class_info info;
    cp_slot *cp;
    uint32_t cp_count;
    char *arena;
    xxbyte_java_member *fields;
    xxbyte_java_member *methods;
    uint32_t *bootstrap_offsets;
};

static const char empty_text[] = "";

/* ---- bounded reader ---- */

typedef struct reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
} reader;

static int need(const reader *r, size_t count)
{
    return count <= r->size && r->pos <= r->size - count;
}

static uint16_t rd_u2(reader *r)
{
    uint16_t v = xxbyte_u16be(r->data + r->pos);
    r->pos += 2;
    return v;
}

static uint32_t rd_u4(reader *r)
{
    uint32_t v = xxbyte_u32be(r->data + r->pos);
    r->pos += 4;
    return v;
}

/* ---- Modified UTF-8 ---- */

static size_t put_utf8(char *out, uint32_t cp)
{
    if (cp < 0x80u) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (char)(0xC0u | (cp >> 6));
        out[1] = (char)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (char)(0xE0u | (cp >> 12));
        out[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (char)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (char)(0xF0u | (cp >> 18));
    out[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (char)(0x80u | (cp & 0x3Fu));
    return 4;
}

/* Reads one Modified UTF-8 unit at in[*i]: a UTF-16 code unit, or
 * 0xFFFFFFFF for a byte that is not valid (one byte is consumed). */
static uint32_t mutf8_unit(const uint8_t *in, size_t length, size_t *i)
{
    uint8_t b = in[*i];

    if (b != 0 && b < 0x80u) {
        *i += 1;
        return b;
    }
    if ((b & 0xE0u) == 0xC0u && *i + 1 < length && (in[*i + 1] & 0xC0u) == 0x80u) {
        uint32_t v = ((uint32_t)(b & 0x1Fu) << 6) | (in[*i + 1] & 0x3Fu);
        *i += 2;
        return v;
    }
    if ((b & 0xF0u) == 0xE0u && *i + 2 < length && (in[*i + 1] & 0xC0u) == 0x80u && (in[*i + 2] & 0xC0u) == 0x80u) {
        uint32_t v = ((uint32_t)(b & 0x0Fu) << 12) | ((uint32_t)(in[*i + 1] & 0x3Fu) << 6) | (in[*i + 2] & 0x3Fu);
        *i += 3;
        return v;
    }
    *i += 1;
    return 0xFFFFFFFFu;
}

/* Decodes into out, which has room for decoded_bound(in, length) + 1
 * bytes, and returns the decoded length. */
static size_t decode_mutf8(const uint8_t *in, size_t length, char *out)
{
    size_t i = 0;
    size_t n = 0;

    while (i < length) {
        uint32_t unit = mutf8_unit(in, length, &i);
        if (unit == 0xFFFFFFFFu) {
            unit = 0xFFFDu;
        } else if (unit >= 0xD800u && unit < 0xDC00u && i < length) {
            size_t j = i;
            uint32_t low = mutf8_unit(in, length, &j);
            if (low >= 0xDC00u && low < 0xE000u) {
                unit = 0x10000u + ((unit - 0xD800u) << 10) + (low - 0xDC00u);
                i = j;
            }
        }
        n += put_utf8(out + n, unit);
    }
    out[n] = '\0';
    return n;
}

/* The longest decoded size of in: invalid bytes (1 byte in) become U+FFFD
 * (3 bytes out). */
static size_t decoded_bound(const uint8_t *in, size_t length)
{
    size_t i = 0;
    size_t n = 0;

    while (i < length) {
        size_t before = i;
        uint32_t unit = mutf8_unit(in, length, &i);
        n += unit == 0xFFFFFFFFu ? 3u : i - before;
    }
    return n;
}

/* ---- open ---- */

static int utf8_is(const xxbyte_java_class *c, uint32_t index, const char *name)
{
    size_t n = strlen(name);
    const cp_slot *s;

    if (index == 0 || index >= c->cp_count || c->cp[index].tag != XXBYTE_JAVA_CONSTANT_UTF8) {
        return 0;
    }
    s = &c->cp[index];
    return s->length == n && memcmp(c->data + s->offset + 3, name, n) == 0;
}

const char *xxbyte_java_class_utf8(const xxbyte_java_class *java_class, uint32_t index, size_t *length)
{
    const cp_slot *s;

    if (length != NULL) {
        *length = 0;
    }
    if (java_class == NULL || index == 0 || index >= java_class->cp_count || java_class->cp[index].tag != XXBYTE_JAVA_CONSTANT_UTF8) {
        return NULL;
    }
    s = &java_class->cp[index];
    if (length != NULL) {
        *length = s->text_length;
    }
    return java_class->arena + s->text;
}

const char *xxbyte_java_class_name(const xxbyte_java_class *java_class, uint32_t index)
{
    if (java_class == NULL || index == 0 || index >= java_class->cp_count || java_class->cp[index].tag != XXBYTE_JAVA_CONSTANT_CLASS) {
        return NULL;
    }
    return xxbyte_java_class_utf8(java_class, java_class->cp[index].index1, NULL);
}

static const char *utf8_or_empty(const xxbyte_java_class *c, uint32_t index)
{
    const char *s = xxbyte_java_class_utf8(c, index, NULL);
    return s != NULL ? s : empty_text;
}

static xxbyte_status parse_constant_pool(xxbyte_java_class *c, reader *r)
{
    size_t arena_size = 1;
    size_t used = 0;
    uint32_t i;

    for (i = 1; i < c->cp_count; ++i) {
        cp_slot *s = &c->cp[i];
        uint8_t tag;

        if (!need(r, 1)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        s->offset = (uint32_t)r->pos;
        tag = r->data[r->pos++];
        s->tag = tag;
        switch (tag) {
            case XXBYTE_JAVA_CONSTANT_UTF8:
                if (!need(r, 2)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                s->length = rd_u2(r);
                if (!need(r, s->length)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                arena_size += decoded_bound(r->data + r->pos, s->length) + 1u;
                r->pos += s->length;
                break;
            case XXBYTE_JAVA_CONSTANT_INTEGER:
            case XXBYTE_JAVA_CONSTANT_FLOAT:
                if (!need(r, 4)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                s->value = rd_u4(r);
                break;
            case XXBYTE_JAVA_CONSTANT_LONG:
            case XXBYTE_JAVA_CONSTANT_DOUBLE:
                if (!need(r, 8)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                s->value = (uint64_t)rd_u4(r) << 32;
                s->value |= rd_u4(r);
                /* The next slot is unusable (JVMS 4.4.5). A Long or Double
                 * in the last slot is malformed. */
                if (i + 1u >= c->cp_count) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                ++i;
                break;
            case XXBYTE_JAVA_CONSTANT_CLASS:
            case XXBYTE_JAVA_CONSTANT_STRING:
            case XXBYTE_JAVA_CONSTANT_METHOD_TYPE:
            case XXBYTE_JAVA_CONSTANT_MODULE:
            case XXBYTE_JAVA_CONSTANT_PACKAGE:
                if (!need(r, 2)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                s->index1 = rd_u2(r);
                break;
            case XXBYTE_JAVA_CONSTANT_FIELDREF:
            case XXBYTE_JAVA_CONSTANT_METHODREF:
            case XXBYTE_JAVA_CONSTANT_INTERFACE_METHODREF:
            case XXBYTE_JAVA_CONSTANT_NAME_AND_TYPE:
            case XXBYTE_JAVA_CONSTANT_DYNAMIC:
            case XXBYTE_JAVA_CONSTANT_INVOKE_DYNAMIC:
                if (!need(r, 4)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                s->index1 = rd_u2(r);
                s->index2 = rd_u2(r);
                break;
            case XXBYTE_JAVA_CONSTANT_METHOD_HANDLE:
                if (!need(r, 3)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                s->kind = r->data[r->pos++];
                s->index1 = rd_u2(r);
                break;
            default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    if (arena_size > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    c->arena = (char *)xx_mem_alloc(arena_size);
    if (c->arena == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    c->arena[used++] = '\0';
    for (i = 1; i < c->cp_count; ++i) {
        cp_slot *s = &c->cp[i];
        if (s->tag == XXBYTE_JAVA_CONSTANT_UTF8) {
            s->text = (uint32_t)used;
            s->text_length = (uint32_t)decode_mutf8(r->data + s->offset + 3, s->length, c->arena + used);
            used += s->text_length + 1u;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* Reads a u2 attribute that holds a constant-pool index (Signature,
 * SourceFile). */
static xxbyte_status attribute_index(const uint8_t *info, uint32_t length, uint16_t *index)
{
    if (length != 2) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *index = xxbyte_u16be(info);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status parse_code(xxbyte_java_class *c, xxbyte_java_member *m, uint32_t info_offset, uint32_t length)
{
    const uint8_t *info = c->data + info_offset;
    xxbyte_java_code_attribute *code = &m->code;
    xxbyte_status status;
    uint64_t pos;
    uint32_t i;

    status = xxbyte_java_parse_code_attribute(info, length, code);
    if (status != XXBYTE_STATUS_OK) {
        /* Inside the attribute a short body is malformed, not a file that
         * ends early: the attribute itself fits in the file. */
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    m->has_code = 1;
    m->code_attribute_offset = info_offset;
    m->code_attribute_length = length;
    pos = code->attributes_offset;
    for (i = 0; i < code->attributes_count; ++i) {
        uint16_t name = xxbyte_u16be(info + pos);
        uint32_t sub_length = xxbyte_u32be(info + pos + 2);
        const uint8_t *sub = info + pos + 6;

        if (m->line_number_table_offset == 0 && utf8_is(c, name, "LineNumberTable")) {
            uint16_t count;
            if (sub_length < 2) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            count = xxbyte_u16be(sub);
            if (2u + (uint64_t)count * 4u > sub_length) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            m->line_number_table_length = count;
            m->line_number_table_offset = (uint32_t)(info_offset + pos + 8u);
        }
        pos += 6u + (uint64_t)sub_length;
    }
    code->code_offset += info_offset;
    code->exception_table_offset += info_offset;
    code->attributes_offset += info_offset;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status parse_members(xxbyte_java_class *c, reader *r, uint16_t count, xxbyte_java_member *members, int methods)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        xxbyte_java_member *m = &members[i];
        uint32_t a;
        int have_signature = 0;
        int have_exceptions = 0;

        if (!need(r, 8)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        m->offset = (uint32_t)r->pos;
        m->access_flags = rd_u2(r);
        m->name_index = rd_u2(r);
        m->descriptor_index = rd_u2(r);
        m->attributes_count = rd_u2(r);
        m->name = utf8_or_empty(c, m->name_index);
        m->descriptor = utf8_or_empty(c, m->descriptor_index);
        for (a = 0; a < m->attributes_count; ++a) {
            uint16_t name;
            uint32_t length;
            uint32_t info_offset;
            xxbyte_status status = XXBYTE_STATUS_OK;

            if (!need(r, 6)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            name = rd_u2(r);
            length = rd_u4(r);
            if (!need(r, length)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            info_offset = (uint32_t)r->pos;
            if (xxbyte_java_class_utf8(c, name, NULL) == NULL) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (methods && !m->has_code && utf8_is(c, name, "Code")) {
                status = parse_code(c, m, info_offset, length);
            } else if (methods && !have_exceptions && utf8_is(c, name, "Exceptions")) {
                uint16_t n;
                if (length < 2) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                n = xxbyte_u16be(c->data + info_offset);
                if (2u + (uint64_t)n * 2u > length) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                have_exceptions = 1;
                m->exceptions_count = n;
                m->exceptions_offset = info_offset + 2u;
            } else if (!have_signature && utf8_is(c, name, "Signature")) {
                uint16_t index = 0;
                status = attribute_index(c->data + info_offset, length, &index);
                have_signature = 1;
                m->signature = xxbyte_java_class_utf8(c, index, NULL);
            }
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            r->pos += length;
        }
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status parse_bootstrap(xxbyte_java_class *c, uint32_t info_offset, uint32_t length)
{
    reader r;
    uint16_t count;
    uint32_t i;

    r.data = c->data + info_offset;
    r.size = length;
    r.pos = 0;
    if (!need(&r, 2)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    count = rd_u2(&r);
    if (count != 0) {
        c->bootstrap_offsets = (uint32_t *)xx_mem_calloc(count, sizeof(uint32_t));
        if (c->bootstrap_offsets == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    for (i = 0; i < count; ++i) {
        uint16_t argc;
        c->bootstrap_offsets[i] = (uint32_t)(info_offset + r.pos);
        if (!need(&r, 4)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        r.pos += 2;
        argc = rd_u2(&r);
        if (!need(&r, (size_t)argc * 2u)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        r.pos += (size_t)argc * 2u;
    }
    c->info.bootstrap_methods_count = count;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status parse_class(xxbyte_java_class *c)
{
    reader r;
    xxbyte_status status;
    uint32_t a;
    int have_source = 0;
    int have_bootstrap = 0;

    r.data = c->data;
    r.size = c->size;
    r.pos = 0;
    if (!need(&r, 4)) {
        return c->size >= 1 && c->data[0] != 0xCAu ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_TRUNCATED;
    }
    if (rd_u4(&r) != 0xCAFEBABEu) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!need(&r, 6)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    c->info.minor_version = rd_u2(&r);
    c->info.major_version = rd_u2(&r);
    if (c->info.major_version < XXBYTE_JAVA_MIN_MAJOR_VERSION || c->info.major_version > XXBYTE_JAVA_MAX_MAJOR_VERSION) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    c->info.constant_pool_count = rd_u2(&r);
    if (c->info.constant_pool_count == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    c->cp_count = c->info.constant_pool_count;
    c->cp = (cp_slot *)xx_mem_calloc(c->cp_count, sizeof(cp_slot));
    if (c->cp == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = parse_constant_pool(c, &r);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (!need(&r, 8)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    c->info.access_flags = rd_u2(&r);
    c->info.this_class = rd_u2(&r);
    c->info.super_class = rd_u2(&r);
    c->info.interfaces_count = rd_u2(&r);
    c->info.interfaces_offset = (uint32_t)r.pos;
    if (!need(&r, (size_t)c->info.interfaces_count * 2u)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    r.pos += (size_t)c->info.interfaces_count * 2u;
    c->info.this_name = xxbyte_java_class_name(c, c->info.this_class);
    c->info.super_name = xxbyte_java_class_name(c, c->info.super_class);

    if (!need(&r, 2)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    c->info.fields_count = rd_u2(&r);
    if (c->info.fields_count != 0) {
        c->fields = (xxbyte_java_member *)xx_mem_calloc(c->info.fields_count, sizeof(xxbyte_java_member));
        if (c->fields == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    status = parse_members(c, &r, c->info.fields_count, c->fields, 0);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (!need(&r, 2)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    c->info.methods_count = rd_u2(&r);
    if (c->info.methods_count != 0) {
        c->methods = (xxbyte_java_member *)xx_mem_calloc(c->info.methods_count, sizeof(xxbyte_java_member));
        if (c->methods == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    status = parse_members(c, &r, c->info.methods_count, c->methods, 1);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (!need(&r, 2)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    c->info.attributes_count = rd_u2(&r);
    for (a = 0; a < c->info.attributes_count; ++a) {
        uint16_t name;
        uint32_t length;
        uint32_t info_offset;

        if (!need(&r, 6)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        name = rd_u2(&r);
        length = rd_u4(&r);
        if (!need(&r, length)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        info_offset = (uint32_t)r.pos;
        if (xxbyte_java_class_utf8(c, name, NULL) == NULL) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (!have_source && utf8_is(c, name, "SourceFile")) {
            uint16_t index = 0;
            status = attribute_index(c->data + info_offset, length, &index);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            have_source = 1;
            c->info.source_file = xxbyte_java_class_utf8(c, index, NULL);
        } else if (!have_bootstrap && utf8_is(c, name, "BootstrapMethods")) {
            status = parse_bootstrap(c, info_offset, length);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            have_bootstrap = 1;
        }
        r.pos += length;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_java_class_open(const uint8_t *data, size_t size, xxbyte_java_class **java_class)
{
    xxbyte_java_class *c;
    xxbyte_status status;

    if (java_class == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *java_class = NULL;
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (size == 0) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    c = (xxbyte_java_class *)xx_mem_calloc(1, sizeof(*c));
    if (c == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    c->data = data;
    c->size = size;
    status = parse_class(c);
    if (status != XXBYTE_STATUS_OK) {
        xxbyte_java_class_close(c);
        return status;
    }
    *java_class = c;
    return XXBYTE_STATUS_OK;
}

void xxbyte_java_class_close(xxbyte_java_class *java_class)
{
    if (java_class == NULL) {
        return;
    }
    xx_mem_free(java_class->cp);
    xx_mem_free(java_class->arena);
    xx_mem_free(java_class->fields);
    xx_mem_free(java_class->methods);
    xx_mem_free(java_class->bootstrap_offsets);
    xx_mem_free(java_class);
}

xxbyte_status xxbyte_java_class_get_info(const xxbyte_java_class *java_class, xxbyte_java_class_info *info)
{
    if (info == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (java_class == NULL) {
        memset(info, 0, sizeof(*info));
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *info = java_class->info;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_java_class_constant(const xxbyte_java_class *java_class, uint32_t index, xxbyte_java_constant *constant)
{
    const cp_slot *s;

    if (constant == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(constant, 0, sizeof(*constant));
    if (java_class == NULL || index == 0 || index >= java_class->cp_count || java_class->cp[index].tag == 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    s = &java_class->cp[index];
    constant->tag = s->tag;
    constant->reference_kind = s->kind;
    constant->index1 = s->index1;
    constant->index2 = s->index2;
    constant->offset = s->offset;
    constant->length = s->length;
    constant->value = s->value;
    return XXBYTE_STATUS_OK;
}

uint16_t xxbyte_java_class_interface(const xxbyte_java_class *java_class, uint32_t i)
{
    if (java_class == NULL || i >= java_class->info.interfaces_count) {
        return 0;
    }
    return xxbyte_u16be(java_class->data + java_class->info.interfaces_offset + (size_t)i * 2u);
}

xxbyte_status xxbyte_java_class_method(const xxbyte_java_class *java_class, uint32_t i, xxbyte_java_member *member)
{
    if (member == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (java_class == NULL || i >= java_class->info.methods_count) {
        memset(member, 0, sizeof(*member));
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *member = java_class->methods[i];
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_java_class_field(const xxbyte_java_class *java_class, uint32_t i, xxbyte_java_member *member)
{
    if (member == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (java_class == NULL || i >= java_class->info.fields_count) {
        memset(member, 0, sizeof(*member));
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *member = java_class->fields[i];
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_java_class_bootstrap_method(const xxbyte_java_class *java_class, uint32_t i, xxbyte_java_bootstrap_method *method)
{
    const uint8_t *p;

    if (method == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(method, 0, sizeof(*method));
    if (java_class == NULL || i >= java_class->info.bootstrap_methods_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    p = java_class->data + java_class->bootstrap_offsets[i];
    method->method_ref = xxbyte_u16be(p);
    method->argument_count = xxbyte_u16be(p + 2);
    method->arguments_offset = java_class->bootstrap_offsets[i] + 4u;
    return XXBYTE_STATUS_OK;
}

const uint8_t *xxbyte_java_class_data(const xxbyte_java_class *java_class, size_t *size)
{
    if (size != NULL) {
        *size = java_class != NULL ? java_class->size : 0;
    }
    return java_class != NULL ? java_class->data : NULL;
}

/* ---- javap text ---- */

static void buf_rewind(xx_buf_t *out, size_t mark)
{
    if (mark < out->size && out->data != NULL) {
        out->size = mark;
        out->data[mark] = '\0';
    }
}

/* Reads one code point of the class's decoded text (well-formed UTF-8
 * with unpaired surrogates as three-byte sequences). */
static uint32_t next_cp(const char *s, size_t length, size_t *i)
{
    const uint8_t *p = (const uint8_t *)s + *i;
    size_t left = length - *i;

    if (p[0] < 0x80u || left < 2) {
        *i += 1;
        return p[0];
    }
    if (p[0] < 0xE0u) {
        *i += 2;
        return ((uint32_t)(p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
    }
    if (p[0] < 0xF0u || left < 4) {
        if (left < 3) {
            *i += left;
            return 0xFFFDu;
        }
        *i += 3;
        return ((uint32_t)(p[0] & 0x0Fu) << 12) | ((uint32_t)(p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
    }
    *i += 4;
    return ((uint32_t)(p[0] & 0x07u) << 18) | ((uint32_t)(p[1] & 0x3Fu) << 12) | ((uint32_t)(p[2] & 0x3Fu) << 6) | (p[3] & 0x3Fu);
}

static int is_surrogate(uint32_t cp)
{
    return cp >= 0xD800u && cp < 0xE000u;
}

/* One code point as javap's UTF-8 output writes it: '?' for an unpaired
 * surrogate. */
static int put_cp(xx_buf_t *out, uint32_t cp)
{
    char tmp[4];
    if (is_surrogate(cp)) {
        return xx_buf_append_char(out, '?');
    }
    return xx_buf_append(out, tmp, put_utf8(tmp, cp));
}

/* javap's String comment: escapes, ISO controls as \u00xx. */
static int append_string_literal(xx_buf_t *out, const char *s, size_t length)
{
    size_t i = 0;

    while (i < length) {
        uint32_t cp = next_cp(s, length, &i);
        const char *escape = NULL;
        switch (cp) {
            case '\b': escape = "\\b"; break;
            case '\t': escape = "\\t"; break;
            case '\n': escape = "\\n"; break;
            case '\f': escape = "\\f"; break;
            case '\r': escape = "\\r"; break;
            case '"': escape = "\\\""; break;
            case '\'': escape = "\\'"; break;
            case '\\': escape = "\\\\"; break;
            default: break;
        }
        if (escape != NULL) {
            if (!xx_buf_append_str(out, escape)) {
                return 0;
            }
        } else if (cp < 0x20u || (cp >= 0x7Fu && cp <= 0x9Fu)) {
            if (!xx_buf_appendf(out, "\\u%04x", (unsigned)cp)) {
                return 0;
            }
        } else if (!put_cp(out, cp)) {
            return 0;
        }
    }
    return 1;
}

/* javap's checkName: a name that is not identifiers separated by '/' is
 * quoted, with '"' and '\' escaped. */
static int append_name(xx_buf_t *out, const char *s, size_t length)
{
    size_t i = 0;
    uint32_t previous = '/';
    int quote = length == 0;

    while (!quote && i < length) {
        uint32_t cp = next_cp(s, length, &i);
        if ((previous == '/' && !xxbyte_java_identifier_start(cp)) || (cp != '/' && !xxbyte_java_identifier_part(cp))) {
            quote = 1;
        }
        previous = cp;
    }
    if (quote && !xx_buf_append_char(out, '"')) {
        return 0;
    }
    i = 0;
    while (i < length) {
        uint32_t cp = next_cp(s, length, &i);
        if (quote && (cp == '"' || cp == '\\') && !xx_buf_append_char(out, '\\')) {
            return 0;
        }
        if (!put_cp(out, cp)) {
            return 0;
        }
    }
    return !quote || xx_buf_append_char(out, '"');
}

static const cp_slot *slot(const xxbyte_java_class *c, uint32_t index, uint8_t tag)
{
    if (index == 0 || index >= c->cp_count || c->cp[index].tag != tag) {
        return NULL;
    }
    return &c->cp[index];
}

static int append_utf8_name(xx_buf_t *out, const xxbyte_java_class *c, uint32_t index)
{
    size_t length;
    const char *s = xxbyte_java_class_utf8(c, index, &length);
    return s != NULL && append_name(out, s, length);
}

static int append_utf8_raw(xx_buf_t *out, const xxbyte_java_class *c, uint32_t index)
{
    size_t length;
    size_t i = 0;
    const char *s = xxbyte_java_class_utf8(c, index, &length);

    if (s == NULL) {
        return 0;
    }
    while (i < length) {
        if (!put_cp(out, next_cp(s, length, &i))) {
            return 0;
        }
    }
    return 1;
}

/* name:descriptor of a NameAndType. */
static int append_name_and_type(xx_buf_t *out, const xxbyte_java_class *c, uint32_t index)
{
    const cp_slot *nt = slot(c, index, XXBYTE_JAVA_CONSTANT_NAME_AND_TYPE);
    return nt != NULL && append_utf8_name(out, c, nt->index1) && xx_buf_append_char(out, ':') && append_utf8_raw(out, c, nt->index2);
}

/* class.name:descriptor of a member reference; the class is left out when
 * it is this class and skip_own is set. */
static int append_member_ref(xx_buf_t *out, const xxbyte_java_class *c, const cp_slot *ref, int skip_own)
{
    const cp_slot *cls = slot(c, ref->index1, XXBYTE_JAVA_CONSTANT_CLASS);
    size_t length;
    const char *name;

    if (cls == NULL) {
        return 0;
    }
    name = xxbyte_java_class_utf8(c, cls->index1, &length);
    if (name == NULL) {
        return 0;
    }
    if (!skip_own || c->info.this_name == NULL || strcmp(name, c->info.this_name) != 0 || length != strlen(c->info.this_name)) {
        if (!append_name(out, name, length) || !xx_buf_append_char(out, '.')) {
            return 0;
        }
    }
    return append_name_and_type(out, c, ref->index2);
}

static const char *const reference_kinds[] = {
    "REF_getField",     "REF_getStatic",     "REF_putField",         "REF_putStatic",       "REF_invokeVirtual",
    "REF_invokeStatic", "REF_invokeSpecial", "REF_newInvokeSpecial", "REF_invokeInterface",
};

static int append_constant_body(xx_buf_t *out, const xxbyte_java_class *c, uint32_t index)
{
    const cp_slot *s;
    size_t length;
    const char *text;

    if (index == 0 || index >= c->cp_count) {
        return 0;
    }
    s = &c->cp[index];
    switch (s->tag) {
        case XXBYTE_JAVA_CONSTANT_INTEGER: return xx_buf_appendf(out, "int %d", (int)(int32_t)(uint32_t)s->value);
        case XXBYTE_JAVA_CONSTANT_LONG: return xx_buf_appendf(out, "long %lldl", (long long)(int64_t)s->value);
        case XXBYTE_JAVA_CONSTANT_FLOAT: {
            uint32_t bits = (uint32_t)s->value;
            float f;
            memcpy(&f, &bits, sizeof(f));
            return xx_buf_append_str(out, "float ") && xxbyte_java_append_float(out, f) && xx_buf_append_char(out, 'f');
        }
        case XXBYTE_JAVA_CONSTANT_DOUBLE: {
            double d;
            memcpy(&d, &s->value, sizeof(d));
            return xx_buf_append_str(out, "double ") && xxbyte_java_append_double(out, d) && xx_buf_append_char(out, 'd');
        }
        case XXBYTE_JAVA_CONSTANT_CLASS: return xx_buf_append_str(out, "class ") && append_utf8_name(out, c, s->index1);
        case XXBYTE_JAVA_CONSTANT_STRING:
            text = xxbyte_java_class_utf8(c, s->index1, &length);
            return text != NULL && xx_buf_append_str(out, "String ") && append_string_literal(out, text, length);
        case XXBYTE_JAVA_CONSTANT_FIELDREF: return xx_buf_append_str(out, "Field ") && append_member_ref(out, c, s, 1);
        case XXBYTE_JAVA_CONSTANT_METHODREF: return xx_buf_append_str(out, "Method ") && append_member_ref(out, c, s, 1);
        case XXBYTE_JAVA_CONSTANT_INTERFACE_METHODREF: return xx_buf_append_str(out, "InterfaceMethod ") && append_member_ref(out, c, s, 1);
        case XXBYTE_JAVA_CONSTANT_NAME_AND_TYPE: return append_name_and_type(out, c, index);
        case XXBYTE_JAVA_CONSTANT_METHOD_TYPE: return xx_buf_append_str(out, "MethodType ") && append_utf8_raw(out, c, s->index1);
        case XXBYTE_JAVA_CONSTANT_METHOD_HANDLE: {
            const cp_slot *ref;
            if (s->kind < 1 || s->kind > 9 || s->index1 == 0 || s->index1 >= c->cp_count) {
                return 0;
            }
            ref = &c->cp[s->index1];
            if (ref->tag != XXBYTE_JAVA_CONSTANT_FIELDREF && ref->tag != XXBYTE_JAVA_CONSTANT_METHODREF && ref->tag != XXBYTE_JAVA_CONSTANT_INTERFACE_METHODREF) {
                return 0;
            }
            return xx_buf_appendf(out, "MethodHandle %s ", reference_kinds[s->kind - 1]) && append_member_ref(out, c, ref, 0);
        }
        case XXBYTE_JAVA_CONSTANT_DYNAMIC: return xx_buf_appendf(out, "Dynamic #%u:", (unsigned)s->index1) && append_name_and_type(out, c, s->index2);
        case XXBYTE_JAVA_CONSTANT_INVOKE_DYNAMIC: return xx_buf_appendf(out, "InvokeDynamic #%u:", (unsigned)s->index1) && append_name_and_type(out, c, s->index2);
        case XXBYTE_JAVA_CONSTANT_MODULE: return xx_buf_append_str(out, "Module ") && append_utf8_name(out, c, s->index1);
        case XXBYTE_JAVA_CONSTANT_PACKAGE: return xx_buf_append_str(out, "Package ") && append_utf8_name(out, c, s->index1);
        default: return 0;
    }
}

int xxbyte_java_append_constant(xx_buf_t *out, const xxbyte_java_class *java_class, uint32_t index)
{
    size_t mark = out->size;

    if (java_class == NULL) {
        return 0;
    }
    if (!append_constant_body(out, java_class, index)) {
        buf_rewind(out, mark);
        return 0;
    }
    return 1;
}

int xxbyte_java_append_class_name(xx_buf_t *out, const xxbyte_java_class *java_class, uint32_t index)
{
    size_t mark = out->size;
    const cp_slot *s = java_class != NULL ? slot(java_class, index, XXBYTE_JAVA_CONSTANT_CLASS) : NULL;

    if (s == NULL || !append_utf8_name(out, java_class, s->index1)) {
        buf_rewind(out, mark);
        return 0;
    }
    return 1;
}

static size_t copy_out(xx_buf_t *text, int ok, char *buffer, size_t buffer_size)
{
    size_t length = ok && xx_buf_ok(text) ? text->size : 0;

    if (buffer_size != 0) {
        size_t n = length < buffer_size - 1u ? length : buffer_size - 1u;
        if (n != 0) {
            memcpy(buffer, text->data, n);
        }
        buffer[n] = '\0';
    }
    xx_buf_free(text);
    return length;
}

size_t xxbyte_java_constant_text(const xxbyte_java_class *java_class, uint32_t index, char *buffer, size_t buffer_size)
{
    xx_buf_t text;
    int ok;

    if (buffer == NULL && buffer_size != 0) {
        return 0;
    }
    xx_buf_init(&text);
    ok = xxbyte_java_append_constant(&text, java_class, index);
    return copy_out(&text, ok, buffer, buffer_size);
}

/* ---- Float.toString and Double.toString ---- */

/*
 * The shortest decimal of the value comes from xxbyte_fmt_double /
 * xxbyte_fmt_float; it is rearranged into Java's layout: plain for
 * 1e-3 <= |v| < 1e7 ("0.001", "9999999.0"), computerized scientific
 * otherwise ("1.0E7", "9.9E-4"). Java (JDK 19 and later) prefers, when the
 * shortest decimal has one digit, the closest decimal of two digits: that
 * is the one-digit value followed by 0 except for subnormals, whose
 * precision is too low (Double.MIN_VALUE is 4.9E-324, not 5.0E-324).
 */
static int java_layout(xx_buf_t *out, const char *text, int negative, double value, int is_float)
{
    char digits[40];
    int count = 0;
    int point = 0; /* decimal exponent of the digit string read as 0.ddd */
    int seen_point = 0;
    int exponent = 0;
    int leading = 1;
    int sci;
    int i;
    const char *p = text;

    if (*p == '-') {
        ++p;
    }
    for (; *p != '\0' && *p != 'e'; ++p) {
        if (*p == '.') {
            seen_point = 1;
            continue;
        }
        if (*p < '0' || *p > '9') {
            return 0;
        }
        if (leading && *p == '0') {
            if (seen_point) {
                --point;
            }
            continue;
        }
        leading = 0;
        if (count < (int)sizeof(digits) - 1) {
            digits[count++] = *p;
        }
        if (!seen_point) {
            ++point;
        }
    }
    if (*p == 'e') {
        int sign = 1;
        ++p;
        if (*p == '+') {
            ++p;
        } else if (*p == '-') {
            sign = -1;
            ++p;
        }
        for (; *p >= '0' && *p <= '9'; ++p) {
            exponent = exponent * 10 + (*p - '0');
        }
        point += sign * exponent;
    }
    while (count > 1 && digits[count - 1] == '0') {
        --count;
    }
    if (count == 0) {
        return xx_buf_append_str(out, negative ? "-0.0" : "0.0");
    }
    if (count == 1) {
        /* Subnormal: pick the closest two-digit decimal. */
        double magnitude = value < 0 ? -value : value;
        int subnormal = is_float ? magnitude < 1.17549435e-38 : magnitude < 2.2250738585072014e-308;
        if (subnormal) {
            /* scaled = magnitude * 10^(2 - point), computed in steps that
             * neither overflow nor lose more than a few ulps. */
            double scaled = magnitude;
            int k = 2 - point;
            long long two;
            if (!is_float) {
                scaled *= 1e300;
                k -= 300;
            }
            for (; k > 0; --k) {
                scaled *= 10.0;
            }
            if (scaled < 10.0) {
                /* Below 10^(point-1) the two-digit decimals are ten times
                 * finer. */
                scaled *= 10.0;
                --point;
            }
            two = (long long)(scaled + 0.5);
            if (two >= 100) {
                digits[0] = '1';
                count = 1;
                ++point;
            } else if (two >= 10) {
                digits[0] = (char)('0' + two / 10);
                digits[1] = (char)('0' + two % 10);
                count = digits[1] == '0' ? 1 : 2;
            }
        }
    }
    digits[count] = '\0';
    sci = point - 1;
    if (negative && !xx_buf_append_char(out, '-')) {
        return 0;
    }
    if (sci >= -3 && sci < 7) {
        if (point <= 0) {
            if (!xx_buf_append_str(out, "0.")) {
                return 0;
            }
            for (i = point; i < 0; ++i) {
                if (!xx_buf_append_char(out, '0')) {
                    return 0;
                }
            }
            return xx_buf_append_str(out, digits);
        }
        for (i = 0; i < point; ++i) {
            if (!xx_buf_append_char(out, i < count ? digits[i] : '0')) {
                return 0;
            }
        }
        if (!xx_buf_append_char(out, '.')) {
            return 0;
        }
        if (point >= count) {
            return xx_buf_append_char(out, '0');
        }
        return xx_buf_append_str(out, digits + point);
    }
    if (!xx_buf_append_char(out, digits[0]) || !xx_buf_append_char(out, '.') || !xx_buf_append_str(out, count > 1 ? digits + 1 : "0")) {
        return 0;
    }
    return xx_buf_appendf(out, "E%d", sci);
}

static int java_number(xx_buf_t *out, double value, int is_float, float f)
{
    xx_buf_t tmp;
    xxbyte_fmt fmt;
    int ok;

    if (value != value) {
        return xx_buf_append_str(out, "NaN");
    }
    if (value > 1.7976931348623157e308) {
        return xx_buf_append_str(out, "Infinity");
    }
    if (value < -1.7976931348623157e308) {
        return xx_buf_append_str(out, "-Infinity");
    }
    xx_buf_init(&tmp);
    memset(&fmt, 0, sizeof(fmt));
    fmt.out = &tmp;
    ok = is_float ? xxbyte_fmt_float(&fmt, f) : xxbyte_fmt_double(&fmt, value);
    if (ok && xx_buf_ok(&tmp) && tmp.data != NULL) {
        ok = java_layout(out, tmp.data, tmp.data[0] == '-', value, is_float);
    } else {
        ok = 0;
    }
    xx_buf_free(&tmp);
    return ok;
}

int xxbyte_java_append_double(xx_buf_t *out, double value)
{
    return java_number(out, value, 0, 0.0f);
}

int xxbyte_java_append_float(xx_buf_t *out, float value)
{
    return java_number(out, (double)value, 1, value);
}

/* ---- method declarations ---- */

typedef struct sig_parser {
    const char *s;
    size_t length;
    size_t pos;
} sig_parser;

static int sig_peek(const sig_parser *p)
{
    return p->pos < p->length ? (unsigned char)p->s[p->pos] : -1;
}

/* Copies the text, '/' as '.', up to (not including) the first of stops. */
static int sig_identifier(sig_parser *p, xx_buf_t *out, const char *stops)
{
    size_t start = p->pos;

    while (p->pos < p->length && strchr(stops, p->s[p->pos]) == NULL) {
        char ch = p->s[p->pos++];
        if (!xx_buf_append_char(out, ch == '/' ? '.' : ch)) {
            return 0;
        }
    }
    return p->pos > start && p->pos < p->length;
}

static int sig_type(sig_parser *p, xx_buf_t *out, int allow_void);

static const char *primitive_name(int ch)
{
    switch (ch) {
        case 'B': return "byte";
        case 'C': return "char";
        case 'D': return "double";
        case 'F': return "float";
        case 'I': return "int";
        case 'J': return "long";
        case 'S': return "short";
        case 'Z': return "boolean";
        case 'V': return "void";
        default: return NULL;
    }
}

static int sig_type_arguments(sig_parser *p, xx_buf_t *out)
{
    int first = 1;

    ++p->pos; /* '<' */
    if (!xx_buf_append_char(out, '<')) {
        return 0;
    }
    while (sig_peek(p) != '>') {
        int ch = sig_peek(p);
        if (ch < 0) {
            return 0;
        }
        if (!first && !xx_buf_append_str(out, ", ")) {
            return 0;
        }
        first = 0;
        if (ch == '*') {
            ++p->pos;
            if (!xx_buf_append_char(out, '?')) {
                return 0;
            }
            continue;
        }
        if (ch == '+' || ch == '-') {
            ++p->pos;
            if (!xx_buf_append_str(out, ch == '+' ? "? extends " : "? super ")) {
                return 0;
            }
        }
        if (!sig_type(p, out, 0)) {
            return 0;
        }
    }
    if (first) {
        return 0;
    }
    ++p->pos;
    return xx_buf_append_char(out, '>');
}

static int sig_class_type(sig_parser *p, xx_buf_t *out)
{
    ++p->pos; /* 'L' */
    for (;;) {
        int ch;
        if (!sig_identifier(p, out, "<.;")) {
            return 0;
        }
        ch = sig_peek(p);
        if (ch == '<') {
            if (!sig_type_arguments(p, out)) {
                return 0;
            }
            ch = sig_peek(p);
        }
        if (ch == ';') {
            ++p->pos;
            return 1;
        }
        if (ch != '.') {
            return 0;
        }
        ++p->pos;
        if (!xx_buf_append_char(out, '.')) {
            return 0;
        }
    }
}

/* A JavaTypeSignature (a descriptor field type is one too). */
static int sig_type(sig_parser *p, xx_buf_t *out, int allow_void)
{
    int ch = sig_peek(p);
    int dims = 0;
    const char *name;

    while (ch == '[') {
        ++dims;
        ++p->pos;
        ch = sig_peek(p);
        allow_void = 0;
    }
    if (ch == 'L') {
        if (!sig_class_type(p, out)) {
            return 0;
        }
    } else if (ch == 'T') {
        ++p->pos;
        if (!sig_identifier(p, out, ";")) {
            return 0;
        }
        ++p->pos;
    } else {
        name = primitive_name(ch);
        if (name == NULL || (ch == 'V' && !allow_void)) {
            return 0;
        }
        ++p->pos;
        if (!xx_buf_append_str(out, name)) {
            return 0;
        }
    }
    for (; dims > 0; --dims) {
        if (!xx_buf_append_str(out, "[]")) {
            return 0;
        }
    }
    return 1;
}

static int sig_type_parameters(sig_parser *p, xx_buf_t *out)
{
    int first = 1;

    ++p->pos; /* '<' */
    if (!xx_buf_append_char(out, '<')) {
        return 0;
    }
    while (sig_peek(p) != '>') {
        int bound = 0;
        size_t type_start;
        if (sig_peek(p) < 0) {
            return 0;
        }
        if (!first && !xx_buf_append_str(out, ", ")) {
            return 0;
        }
        first = 0;
        if (!sig_identifier(p, out, ":")) {
            return 0;
        }
        while (sig_peek(p) == ':') {
            size_t mark = out->size;
            ++p->pos;
            if (sig_peek(p) == ':') {
                continue; /* an empty class bound */
            }
            if (!xx_buf_append_str(out, bound ? " & " : " extends ")) {
                return 0;
            }
            type_start = out->size;
            if (!sig_type(p, out, 0)) {
                return 0;
            }
            /* javap leaves java.lang.Object bounds out. */
            if (out->size - type_start == 16 && memcmp(out->data + type_start, "java.lang.Object", 16) == 0) {
                buf_rewind(out, mark);
                continue;
            }
            bound = 1;
        }
    }
    if (first) {
        return 0;
    }
    ++p->pos;
    return xx_buf_append_str(out, "> ");
}

typedef struct method_parts {
    xx_buf_t type_parameters;
    xx_buf_t result;
    xx_buf_t parameters;   /* joined with ", " */
    xx_buf_t throws;       /* joined with ", " */
    size_t last_parameter; /* offset of the last parameter in parameters */
    uint32_t parameter_count;
    uint32_t throws_count;
} method_parts;

static int parse_method_type(const char *text, int generic, method_parts *parts)
{
    sig_parser p;

    p.s = text;
    p.length = strlen(text);
    p.pos = 0;
    if (generic && sig_peek(&p) == '<' && !sig_type_parameters(&p, &parts->type_parameters)) {
        return 0;
    }
    if (sig_peek(&p) != '(') {
        return 0;
    }
    ++p.pos;
    while (sig_peek(&p) != ')') {
        if (sig_peek(&p) < 0) {
            return 0;
        }
        if (parts->parameter_count != 0 && !xx_buf_append_str(&parts->parameters, ", ")) {
            return 0;
        }
        parts->last_parameter = parts->parameters.size;
        if (!(generic ? sig_type(&p, &parts->parameters, 0) : sig_type(&p, &parts->parameters, 0))) {
            return 0;
        }
        ++parts->parameter_count;
    }
    ++p.pos;
    if (!sig_type(&p, &parts->result, 1)) {
        return 0;
    }
    while (generic && sig_peek(&p) == '^') {
        int ch;
        ++p.pos;
        ch = sig_peek(&p);
        if (ch != 'L' && ch != 'T') {
            return 0;
        }
        if (parts->throws_count != 0 && !xx_buf_append_str(&parts->throws, ", ")) {
            return 0;
        }
        if (!sig_type(&p, &parts->throws, 0)) {
            return 0;
        }
        ++parts->throws_count;
    }
    return p.pos == p.length;
}

static void parts_init(method_parts *parts)
{
    memset(parts, 0, sizeof(*parts));
    xx_buf_init(&parts->type_parameters);
    xx_buf_init(&parts->result);
    xx_buf_init(&parts->parameters);
    xx_buf_init(&parts->throws);
}

static void parts_free(method_parts *parts)
{
    xx_buf_free(&parts->type_parameters);
    xx_buf_free(&parts->result);
    xx_buf_free(&parts->parameters);
    xx_buf_free(&parts->throws);
}

static int append_buf(xx_buf_t *out, const xx_buf_t *b)
{
    return b->size == 0 || xx_buf_append(out, b->data, b->size);
}

/* Internal class name as javap prints it in declarations. */
static int append_dotted(xx_buf_t *out, const char *name)
{
    for (; *name != '\0'; ++name) {
        if (!xx_buf_append_char(out, *name == '/' ? '.' : *name)) {
            return 0;
        }
    }
    return 1;
}

int xxbyte_java_append_header(xx_buf_t *out, const xxbyte_java_class *java_class, const xxbyte_java_member *method)
{
    static const struct {
        uint16_t flag;
        const char *name;
    } modifiers[] = {
        {XXBYTE_JAVA_ACC_PUBLIC, "public "}, {XXBYTE_JAVA_ACC_PRIVATE, "private "},   {XXBYTE_JAVA_ACC_PROTECTED, "protected "},
        {XXBYTE_JAVA_ACC_STATIC, "static "}, {XXBYTE_JAVA_ACC_FINAL, "final "},       {XXBYTE_JAVA_ACC_SYNCHRONIZED, "synchronized "},
        {XXBYTE_JAVA_ACC_NATIVE, "native "}, {XXBYTE_JAVA_ACC_ABSTRACT, "abstract "}, {XXBYTE_JAVA_ACC_STRICT, "strictfp "},
    };
    method_parts parts;
    size_t mark = out->size;
    uint16_t flags = method->access_flags;
    uint16_t major = java_class->info.major_version;
    int is_init = strcmp(method->name, "<init>") == 0;
    int is_clinit = strcmp(method->name, "<clinit>") == 0;
    int parsed = 0;
    int ok = 1;
    size_t i;

    for (i = 0; ok && i < sizeof(modifiers) / sizeof(modifiers[0]); ++i) {
        if ((flags & modifiers[i].flag) == 0) {
            continue;
        }
        if (modifiers[i].flag == XXBYTE_JAVA_ACC_STRICT && (major < 46 || major > 60)) {
            continue;
        }
        ok = xx_buf_append_str(out, modifiers[i].name);
    }
    if (ok && (java_class->info.access_flags & XXBYTE_JAVA_ACC_INTERFACE) != 0 && !is_clinit &&
        (flags & (XXBYTE_JAVA_ACC_ABSTRACT | XXBYTE_JAVA_ACC_STATIC | XXBYTE_JAVA_ACC_PRIVATE)) == 0) {
        ok = xx_buf_append_str(out, "default ");
    }
    if (!ok) {
        buf_rewind(out, mark);
        return 0;
    }
    if (is_clinit) {
        ok = xx_buf_append_str(out, "{};");
        if (!ok) {
            buf_rewind(out, mark);
        }
        return ok;
    }
    parts_init(&parts);
    if (method->signature != NULL) {
        parsed = parse_method_type(method->signature, 1, &parts);
        if (!parsed) {
            parts_free(&parts);
            parts_init(&parts);
        }
    }
    if (!parsed) {
        parsed = parse_method_type(method->descriptor, 0, &parts);
    }
    if (parsed) {
        ok = append_buf(out, &parts.type_parameters);
        if (ok && !is_init) {
            ok = append_buf(out, &parts.result) && xx_buf_append_char(out, ' ');
        }
        if (ok) {
            ok = is_init ? (java_class->info.this_name != NULL ? append_dotted(out, java_class->info.this_name) : xx_buf_append_str(out, method->name))
                         : xx_buf_append_str(out, method->name);
        }
        if (ok) {
            size_t length = parts.parameters.size;
            ok = xx_buf_append_char(out, '(');
            size_t last = 0;
            /* javap turns the last "[]" of the parameter list into "...",
             * even when the last parameter is not an array. */
            if ((flags & XXBYTE_JAVA_ACC_VARARGS) != 0) {
                for (last = length; last >= 2; --last) {
                    if (parts.parameters.data[last - 2] == '[' && parts.parameters.data[last - 1] == ']') {
                        break;
                    }
                }
            }
            if (ok && last >= 2) {
                ok = xx_buf_append(out, parts.parameters.data, last - 2) && xx_buf_append_str(out, "...") &&
                     xx_buf_append(out, parts.parameters.data + last, length - last);
            } else if (ok) {
                ok = append_buf(out, &parts.parameters);
            }
            ok = ok && xx_buf_append_char(out, ')');
        }
        if (ok && parts.throws_count != 0) {
            ok = xx_buf_append_str(out, " throws ") && append_buf(out, &parts.throws);
        } else if (ok && method->exceptions_count != 0) {
            uint32_t e;
            const uint8_t *p = java_class->data + method->exceptions_offset;
            int first = 1;
            for (e = 0; ok && e < method->exceptions_count; ++e) {
                const char *name = xxbyte_java_class_name(java_class, xxbyte_u16be(p + (size_t)e * 2u));
                if (name == NULL) {
                    continue;
                }
                ok = xx_buf_append_str(out, first ? " throws " : ", ") && append_dotted(out, name);
                first = 0;
            }
        }
    } else {
        /* Unparsable descriptor: print it as it is. */
        ok = xx_buf_append_str(out, method->name) && xx_buf_append_str(out, method->descriptor);
    }
    ok = ok && xx_buf_append_char(out, ';');
    parts_free(&parts);
    if (!ok) {
        buf_rewind(out, mark);
    }
    return ok;
}

size_t xxbyte_java_method_header(const xxbyte_java_class *java_class, const xxbyte_java_member *method, char *buffer, size_t buffer_size)
{
    xx_buf_t text;
    int ok = 0;

    if ((buffer == NULL && buffer_size != 0) || java_class == NULL || method == NULL || method->name == NULL || method->descriptor == NULL) {
        if (buffer != NULL && buffer_size != 0) {
            buffer[0] = '\0';
        }
        return 0;
    }
    xx_buf_init(&text);
    ok = xxbyte_java_append_header(&text, java_class, method);
    return copy_out(&text, ok, buffer, buffer_size);
}

/* ---- resolver ---- */

size_t xxbyte_java_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_java_resolver_context *ctx = (const xxbyte_java_resolver_context *)context;
    const xxbyte_operand *operand;
    xx_buf_t text;
    int ok;

    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if (ctx == NULL || ctx->java_class == NULL || instruction == NULL || buffer == NULL || buffer_size == 0 || operand_index >= instruction->operand_count ||
        instruction->family != XXBYTE_FAMILY_JAVA) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (operand->type != XXBYTE_OPERAND_INDEX || operand->kind != XXBYTE_INDEX_CONSTANT || operand->value < 0 || operand->value > 0xFFFF) {
        return 0;
    }
    xx_buf_init(&text);
    ok = 1;
    if ((ctx->flags & XXBYTE_JAVA_RESOLVE_TEXT_ONLY) == 0) {
        ok = xx_buf_appendf(&text, "#%u // ", (unsigned)operand->value);
    }
    ok = ok && xxbyte_java_append_constant(&text, ctx->java_class, (uint32_t)operand->value);
    return copy_out(&text, ok, buffer, buffer_size);
}

#else
typedef int xxbyte_java_class_disabled;
#endif
