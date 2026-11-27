/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_yarv_ibf.h"

#if XXBYTE_USE_YARV

/* IBF image readers: the object list, iseq body records and the tables
 * they point at, for every supported release. */

static int yarv_platform_windows(const uint8_t *platform, size_t length)
{
    size_t i;

    for (i = 0; i + 5 <= length; ++i) {
        if (memcmp(platform + i, "mingw", 5) == 0 || memcmp(platform + i, "mswin", 5) == 0) {
            return 1;
        }
    }
    return 0;
}

int yarv_image_init(yarv_image *image, const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header)
{
    uint32_t version;

    memset(image, 0, sizeof(*image));
    if (header == NULL || (data == NULL && size != 0) || xxbyte_check_mode(XXBYTE_FAMILY_YARV, header->mode) != XXBYTE_STATUS_OK) {
        return 0;
    }
    version = YARV_VERSION(header->mode);
    if (version != YARV_MODE(header->major_version, header->minor_version >= 10000 ? header->minor_version / 10000 : header->minor_version)) {
        return 0;
    }
    image->data = data;
    image->size = header->size < size ? header->size : (uint32_t)(size > UINT32_MAX ? UINT32_MAX : size);
    image->header = *header;
    image->version = version;
    image->word = (header->mode & XXBYTE_YARV_MODE_WORD32) != 0 ? 4u : 8u;
    image->big = (header->mode & XXBYTE_YARV_MODE_BIG_ENDIAN) != 0;
    image->lng = image->word;
    if (version <= YARV_MODE(3, 2) && header->platform_offset != 0 && (uint64_t)header->platform_offset + header->platform_size <= image->size &&
        yarv_platform_windows(data + header->platform_offset, header->platform_size)) {
        image->lng = 4;
    }
    return 1;
}

int yarv_read_byte(const yarv_image *image, uint32_t *pos, uint8_t *value)
{
    if (*pos >= image->size) {
        return 0;
    }
    *value = image->data[*pos];
    ++*pos;
    return 1;
}

/* ibf_load_small_value: 1 to 9 bytes, the count of trailing zero bits of
 * the first byte plus one (0x00: 9), big-endian after it. */
int yarv_read_small(const yarv_image *image, uint32_t *pos, uint64_t *value)
{
    const uint8_t *p;
    unsigned n = 1;
    unsigned i;
    uint8_t c;
    uint64_t x;

    if (*pos >= image->size) {
        return 0;
    }
    p = image->data + *pos;
    c = p[0];
    if (c == 0) {
        n = 9;
    } else {
        while ((c & 1u) == 0) {
            c = (uint8_t)(c >> 1);
            ++n;
        }
    }
    if ((uint64_t)*pos + n > image->size) {
        return 0;
    }
    x = (uint64_t)p[0] >> n;
    for (i = 1; i < n; ++i) {
        x = (x << 8) | p[i];
    }
    if (image->word == 4) {
        x &= 0xFFFFFFFFu;
    }
    *value = x;
    *pos += n;
    return 1;
}

int yarv_read_word(const yarv_image *image, uint64_t offset, unsigned width, uint64_t *value)
{
    uint64_t x = 0;
    unsigned i;

    if (offset > image->size || width > image->size - offset) {
        return 0;
    }
    for (i = 0; i < width; ++i) {
        unsigned shift = image->big ? 8u * (width - 1u - i) : 8u * i;
        x |= (uint64_t)image->data[offset + i] << shift;
    }
    *value = x;
    return 1;
}

int yarv_read_i32(const yarv_image *image, uint64_t offset, int32_t *value)
{
    uint64_t x;

    if (!yarv_read_word(image, offset, 4, &x)) {
        return 0;
    }
    *value = (int32_t)xxbyte_sext(x, 32);
    return 1;
}

/* A C long or a VALUE-sized index, sign-extended as the loader's casts do. */
static int yarv_read_long(const yarv_image *image, uint64_t offset, uint64_t *value)
{
    uint64_t x;

    if (!yarv_read_word(image, offset, image->lng, &x)) {
        return 0;
    }
    *value = (uint64_t)xxbyte_sext(x, 8u * image->lng);
    return 1;
}

static uint64_t yarv_align(uint64_t offset, unsigned align)
{
    return offset == 0 ? 0 : ((offset - 1) / align + 1) * align;
}

static int yarv_fits(const yarv_image *image, uint64_t offset, uint64_t length)
{
    return offset <= image->size && length <= image->size - offset;
}

/* Ruby 2.6: native structs after a 4-byte bit-field header. */
static int yarv_object_2_6(const yarv_image *image, uint32_t offset, xxbyte_yarv_object *object)
{
    uint64_t at = yarv_align(offset, 4);
    uint64_t word;
    uint64_t body;
    unsigned lng = image->lng;

    if (!yarv_read_word(image, at, 4, &word)) {
        return 0;
    }
    if (image->big) {
        object->type = (uint8_t)((word >> 27) & 0x1Fu);
        object->special_const = (uint8_t)((word >> 26) & 1u);
        object->frozen = (uint8_t)((word >> 25) & 1u);
        object->internal = (uint8_t)((word >> 24) & 1u);
    } else {
        object->type = (uint8_t)(word & 0x1Fu);
        object->special_const = (uint8_t)((word >> 5) & 1u);
        object->frozen = (uint8_t)((word >> 6) & 1u);
        object->internal = (uint8_t)((word >> 7) & 1u);
    }
    at += 4;
    if (object->special_const) {
        body = yarv_align(at, image->word);
        object->body_offset = (uint32_t)body;
        return body <= image->size && yarv_read_word(image, body, image->word, &object->value);
    }
    switch (object->type) {
        case XXBYTE_YARV_T_CLASS: body = yarv_align(at, 4); break;
        case XXBYTE_YARV_T_FLOAT: body = yarv_align(at, 8); break;
        case XXBYTE_YARV_T_BIGNUM: body = yarv_align(at, image->word); break;
        default: body = yarv_align(at, lng); break;
    }
    if (body > image->size) {
        return 0;
    }
    object->body_offset = (uint32_t)body;
    switch (object->type) {
        case XXBYTE_YARV_T_CLASS: return yarv_read_word(image, body, 4, &object->value);
        case XXBYTE_YARV_T_FLOAT: return yarv_read_word(image, body, 8, &object->value);
        case XXBYTE_YARV_T_STRING: {
            uint64_t encoding;
            if (!yarv_read_long(image, body, &encoding) || !yarv_read_long(image, body + lng, &object->length)) {
                return 0;
            }
            object->encoding = (uint32_t)encoding;
            object->data_offset = (uint32_t)(body + 2u * lng);
            return yarv_fits(image, object->data_offset, object->length);
        }
        case XXBYTE_YARV_T_SYMBOL: return yarv_read_long(image, body, &object->a);
        case XXBYTE_YARV_T_REGEXP: {
            uint64_t option;
            if (!yarv_read_long(image, body, &object->a) || !yarv_read_word(image, body + lng, 1, &option)) {
                return 0;
            }
            object->flags = (uint32_t)option;
            return 1;
        }
        case XXBYTE_YARV_T_ARRAY:
        case XXBYTE_YARV_T_HASH: {
            uint64_t count;
            if (!yarv_read_long(image, body, &object->length)) {
                return 0;
            }
            object->data_offset = (uint32_t)(body + lng);
            count = object->type == XXBYTE_YARV_T_HASH ? 2u * (object->length & 0x7FFFFFFFFFFFFFFFu) : object->length;
            return object->length <= UINT32_MAX && yarv_fits(image, object->data_offset, count * lng);
        }
        case XXBYTE_YARV_T_STRUCT: {
            uint64_t excl;
            if (!yarv_read_long(image, body + 2u * lng, &object->a) || !yarv_read_long(image, body + 3u * lng, &object->b) ||
                !yarv_read_word(image, body + 4u * lng, 4, &excl)) {
                return 0;
            }
            object->flags = excl != 0;
            return 1;
        }
        case XXBYTE_YARV_T_BIGNUM: {
            uint64_t slen;
            int64_t signed_length;
            if (!yarv_read_word(image, body, image->word, &slen)) {
                return 0;
            }
            signed_length = xxbyte_sext(slen, 8u * image->word);
            object->flags = signed_length > 0 ? 0u : 1u;
            object->length = signed_length > 0 ? (uint64_t)signed_length : 0u - (uint64_t)signed_length;
            object->data_offset = (uint32_t)(body + image->word);
            return object->length <= UINT32_MAX && yarv_fits(image, object->data_offset, object->length * 4u);
        }
        case XXBYTE_YARV_T_DATA:
            if (!yarv_read_long(image, body, &object->value) || !yarv_read_long(image, body + lng, &object->length)) {
                return 0;
            }
            object->data_offset = (uint32_t)(body + 2u * lng);
            return yarv_fits(image, object->data_offset, object->length);
        case XXBYTE_YARV_T_COMPLEX:
        case XXBYTE_YARV_T_RATIONAL: return yarv_read_long(image, body, &object->a) && yarv_read_long(image, body + lng, &object->b);
        default: return -1;
    }
}

/* Ruby 2.7+: a header byte, then small values or aligned native structs. */
static int yarv_object_compact(const yarv_image *image, uint32_t offset, xxbyte_yarv_object *object)
{
    uint32_t pos = offset;
    uint64_t body;
    uint64_t x;
    uint8_t byte;
    unsigned lng = image->lng;

    if (!yarv_read_byte(image, &pos, &byte) || pos >= image->size) {
        return 0;
    }
    object->type = (uint8_t)(byte & 0x1Fu);
    object->special_const = (uint8_t)((byte >> 5) & 1u);
    object->frozen = (uint8_t)((byte >> 6) & 1u);
    object->internal = (uint8_t)((byte >> 7) & 1u);
    object->body_offset = pos;
    if (object->special_const) {
        return yarv_read_small(image, &pos, &object->value);
    }
    switch (object->type) {
        case XXBYTE_YARV_T_CLASS: return yarv_read_small(image, &pos, &object->value);
        case XXBYTE_YARV_T_FLOAT:
            body = yarv_align(pos, 8);
            object->body_offset = (uint32_t)(body > UINT32_MAX ? UINT32_MAX : body);
            return yarv_read_word(image, body, 8, &object->value);
        case XXBYTE_YARV_T_SYMBOL:
            if (image->version < YARV_MODE(3, 1)) {
                /* The index of its String. */
                return yarv_read_small(image, &pos, &object->a);
            }
            /* fall through */
        case XXBYTE_YARV_T_STRING:
            if (!yarv_read_small(image, &pos, &x) || !yarv_read_small(image, &pos, &object->length)) {
                return 0;
            }
            object->encoding = (uint32_t)x;
            object->data_offset = pos;
            return yarv_fits(image, pos, object->length);
        case XXBYTE_YARV_T_REGEXP:
            if (!yarv_read_byte(image, &pos, &byte) || !yarv_read_small(image, &pos, &object->a)) {
                return 0;
            }
            object->flags = byte;
            return 1;
        case XXBYTE_YARV_T_ARRAY:
        case XXBYTE_YARV_T_HASH:
            if (!yarv_read_small(image, &pos, &object->length)) {
                return 0;
            }
            object->data_offset = pos;
            /* Every element takes at least one byte. */
            return object->length <= image->size && (object->type == XXBYTE_YARV_T_ARRAY ? object->length : 2u * object->length) <= (uint64_t)image->size - pos;
        case XXBYTE_YARV_T_STRUCT:
            body = yarv_align(pos, lng);
            object->body_offset = (uint32_t)(body > UINT32_MAX ? UINT32_MAX : body);
            if (!yarv_read_long(image, body + 2u * lng, &object->a) || !yarv_read_long(image, body + 3u * lng, &object->b) ||
                !yarv_read_word(image, body + 4u * lng, 4, &x)) {
                return 0;
            }
            object->flags = x != 0;
            return 1;
        case XXBYTE_YARV_T_BIGNUM: {
            int64_t signed_length;
            body = yarv_align(pos, image->word);
            object->body_offset = (uint32_t)(body > UINT32_MAX ? UINT32_MAX : body);
            if (!yarv_read_word(image, body, image->word, &x)) {
                return 0;
            }
            signed_length = xxbyte_sext(x, 8u * image->word);
            object->flags = signed_length > 0 ? 0u : 1u;
            object->length = signed_length > 0 ? (uint64_t)signed_length : 0u - (uint64_t)signed_length;
            object->data_offset = (uint32_t)(body + image->word);
            return object->length <= UINT32_MAX && yarv_fits(image, object->data_offset, object->length * 4u);
        }
        case XXBYTE_YARV_T_DATA:
            body = yarv_align(pos, lng);
            object->body_offset = (uint32_t)(body > UINT32_MAX ? UINT32_MAX : body);
            if (!yarv_read_long(image, body, &object->value) || !yarv_read_long(image, body + lng, &object->length)) {
                return 0;
            }
            object->data_offset = (uint32_t)(body + 2u * lng);
            return yarv_fits(image, object->data_offset, object->length);
        case XXBYTE_YARV_T_COMPLEX:
        case XXBYTE_YARV_T_RATIONAL:
            body = yarv_align(pos, lng);
            object->body_offset = (uint32_t)(body > UINT32_MAX ? UINT32_MAX : body);
            return yarv_read_long(image, body, &object->a) && yarv_read_long(image, body + lng, &object->b);
        default: return -1;
    }
}

/* 1 on success, 0 when the object runs out of the image, -1 for an
 * unknown type. */
static int yarv_object_status(const yarv_image *image, uint64_t index, xxbyte_yarv_object *object)
{
    uint64_t entry;
    uint64_t offset;
    int result;

    memset(object, 0, sizeof(*object));
    if (index == 0 || index >= image->header.object_list_size) {
        return -2;
    }
    entry = (uint64_t)image->header.object_list_offset + 4u * index;
    if (!yarv_read_word(image, entry, 4, &offset)) {
        return 0;
    }
    object->offset = (uint32_t)offset;
    result = image->version < YARV_MODE(2, 7) ? yarv_object_2_6(image, (uint32_t)offset, object) : yarv_object_compact(image, (uint32_t)offset, object);
    if (result != 1) {
        memset(object, 0, sizeof(*object));
    }
    return result;
}

int yarv_object(const yarv_image *image, uint64_t index, xxbyte_yarv_object *object)
{
    return yarv_object_status(image, index, object) == 1;
}

int yarv_string_bytes(const yarv_image *image, const xxbyte_yarv_object *object, const uint8_t **bytes, uint32_t *length, uint32_t *encoding)
{
    xxbyte_yarv_object string;

    if (object->special_const) {
        return 0;
    }
    if (object->type == XXBYTE_YARV_T_SYMBOL && image->version < YARV_MODE(3, 1)) {
        if (!yarv_object(image, object->a, &string) || string.special_const || string.type != XXBYTE_YARV_T_STRING) {
            return 0;
        }
        object = &string;
    } else if (object->type != XXBYTE_YARV_T_STRING && object->type != XXBYTE_YARV_T_SYMBOL) {
        return 0;
    }
    if (object->length > UINT32_MAX) {
        return 0;
    }
    *bytes = image->data + object->data_offset;
    *length = (uint32_t)object->length;
    *encoding = object->encoding;
    return 1;
}

void yarv_elements_init(yarv_elements *it, const yarv_image *image, const xxbyte_yarv_object *object)
{
    it->image = image;
    it->object = object;
    it->next = 0;
    it->pos = object->data_offset;
}

int yarv_elements_next(yarv_elements *it, uint64_t *index)
{
    const xxbyte_yarv_object *object = it->object;
    uint64_t count;

    if (object->special_const || (object->type != XXBYTE_YARV_T_ARRAY && object->type != XXBYTE_YARV_T_HASH)) {
        return 0;
    }
    count = object->type == XXBYTE_YARV_T_HASH ? 2u * object->length : object->length;
    if (it->next >= count) {
        return 0;
    }
    if (it->image->version < YARV_MODE(2, 7)) {
        if (!yarv_read_long(it->image, (uint64_t)object->data_offset + it->next * it->image->lng, index)) {
            return 0;
        }
    } else if (!yarv_read_small(it->image, &it->pos, index)) {
        return 0;
    }
    ++it->next;
    return 1;
}

int yarv_id_object(const yarv_image *image, uint64_t id, uint64_t *object_index)
{
    *object_index = 0;
    if (id == 0) {
        return 1;
    }
    if (image->version >= YARV_MODE(2, 7)) {
        *object_index = id;
        return 1;
    }
    if (id >= image->header.id_list_size) {
        return 0;
    }
    return yarv_read_long(image, (uint64_t)image->header.id_list_offset + id * image->lng, object_index);
}

/* Ruby 2.6 rb_iseq_constant_body field offsets for a word size. */
typedef struct yarv_layout_2_6 {
    unsigned param;
    unsigned opt_table;
    unsigned keyword;
    unsigned location;
    unsigned node_id;
    unsigned insns_info;
    unsigned insns_size;
    unsigned local_table;
    unsigned local_table_size;
} yarv_layout_2_6;

static unsigned yarv_up(unsigned offset, unsigned align)
{
    return (offset + align - 1u) / align * align;
}

static void yarv_layout(unsigned w, yarv_layout_2_6 *layout)
{
    unsigned end;

    layout->param = yarv_up(8, w) + w; /* type, iseq_size, iseq_encoded */
    layout->opt_table = yarv_up(layout->param + 32u, w);
    layout->keyword = layout->opt_table + w;
    layout->location = layout->keyword + w;
    layout->node_id = layout->location + 4u * w;
    end = yarv_up(layout->node_id + 20u, w);
    layout->insns_info = end;
    layout->insns_size = end + 2u * w;
    layout->local_table = yarv_up(layout->insns_size + 4u, w) + w;
    /* local_table, catch_table, parent_iseq, local_iseq, is_entries,
     * ci_entries, cc_entries, then variable (4 words). */
    layout->local_table_size = layout->local_table + 11u * w;
}

static int yarv_fixnum(uint64_t value, unsigned word, int64_t *result)
{
    if ((value & 1u) == 0) {
        return 0;
    }
    *result = xxbyte_sext(value, 8u * word) >> 1;
    return 1;
}

static int yarv_iseq_info_2_6(const yarv_image *image, xxbyte_yarv_iseq_info *info)
{
    yarv_layout_2_6 l;
    unsigned w = image->word;
    uint64_t b = info->code.body_offset;
    uint64_t x = 0;
    uint64_t end;
    uint32_t sizes[5];
    unsigned i;

    yarv_layout(w, &l);
    if (!yarv_fits(image, b, l.local_table_size + 20u)) {
        return 0;
    }
    yarv_read_word(image, b + l.param, 4, &x);
    if (image->big) {
        uint32_t flags = 0;
        for (i = 0; i < 8; ++i) {
            flags |= (uint32_t)((x >> (31u - i)) & 1u) << i;
        }
        info->param_flags = flags;
    } else {
        info->param_flags = (uint32_t)(x & 0xFFu);
    }
    yarv_read_word(image, b + l.param + 4u, 4, &x);
    info->param_size = (uint32_t)x;
    yarv_read_i32(image, b + l.param + 8u, &info->lead_num);
    yarv_read_i32(image, b + l.param + 12u, &info->opt_num);
    yarv_read_i32(image, b + l.param + 16u, &info->rest_start);
    yarv_read_i32(image, b + l.param + 20u, &info->post_start);
    yarv_read_i32(image, b + l.param + 24u, &info->post_num);
    yarv_read_i32(image, b + l.param + 28u, &info->block_start);
    yarv_read_word(image, b + l.opt_table, w, &x);
    info->opt_table_offset = (uint32_t)x;
    yarv_read_word(image, b + l.keyword, w, &x);
    info->keyword_offset = (uint32_t)x;
    yarv_read_word(image, b + l.location, w, &info->pathobj);
    yarv_read_word(image, b + l.location + w, w, &info->base_label);
    yarv_read_word(image, b + l.location + 2u * w, w, &info->label);
    yarv_read_word(image, b + l.location + 3u * w, w, &x);
    if (!yarv_fixnum(x, w, &info->first_lineno)) {
        info->first_lineno = 0;
    }
    yarv_read_i32(image, b + l.node_id, &info->node_id);
    yarv_read_i32(image, b + l.node_id + 4u, &info->beg_lineno);
    yarv_read_i32(image, b + l.node_id + 8u, &info->beg_column);
    yarv_read_i32(image, b + l.node_id + 12u, &info->end_lineno);
    yarv_read_i32(image, b + l.node_id + 16u, &info->end_column);
    yarv_read_word(image, b + l.insns_info, w, &x);
    info->insns_info_offset = (uint32_t)x;
    yarv_read_word(image, b + l.insns_info + w, w, &x);
    info->insns_positions_offset = (uint32_t)x;
    yarv_read_word(image, b + l.insns_size, 4, &x);
    info->insns_info_size = (uint32_t)x;
    yarv_read_word(image, b + l.local_table, w, &x);
    info->local_table_offset = (uint32_t)x;
    yarv_read_word(image, b + l.local_table + w, w, &x);
    info->catch_table_offset = (uint32_t)x;
    yarv_read_word(image, b + l.local_table + 2u * w, w, &x);
    info->parent_iseq = (int32_t)xxbyte_sext(x, 32);
    yarv_read_word(image, b + l.local_table + 3u * w, w, &x);
    info->local_iseq = (int32_t)xxbyte_sext(x, 32);
    info->mandatory_only_iseq = -1;
    yarv_read_word(image, b + l.local_table + 5u * w, w, &x);
    info->ci_offset = (uint32_t)x;
    yarv_read_word(image, b + l.local_table + 7u * w, w, &x);
    info->flip_count = xxbyte_sext(x, 8u * w);
    for (i = 0; i < 5; ++i) {
        yarv_read_word(image, b + l.local_table_size + 4u * i, 4, &x);
        sizes[i] = (uint32_t)x;
    }
    info->local_table_size = sizes[0];
    info->is_size = sizes[1];
    info->ci_size = sizes[2];
    info->ci_kw_size = sizes[3];
    info->stack_max = sizes[4];
    if (info->catch_table_offset != 0) {
        if (!yarv_read_word(image, info->catch_table_offset, 4, &x)) {
            return 0;
        }
        info->catch_table_size = (uint32_t)x;
    }
    if (info->keyword_offset != 0) {
        if (!yarv_read_i32(image, info->keyword_offset, &info->keyword_num) || !yarv_read_i32(image, info->keyword_offset + 4u, &info->keyword_required_num) ||
            !yarv_read_i32(image, info->keyword_offset + 8u, &info->keyword_bits_start) || !yarv_read_i32(image, info->keyword_offset + 12u, &info->keyword_rest_start)) {
            return 0;
        }
    }
    /* catch_except_p is the last member, after the MJIT fields when the
     * writer had MJIT. The body of iseq 0 is written last, just before the
     * iseq list, which tells the struct size. */
    end = l.local_table_size + 20u; /* after stack_max */
    {
        uint64_t body0 = 0;
        uint64_t size_mjit = yarv_up((unsigned)(yarv_up((unsigned)end, w) + 3u * w + 1u), w);
        uint64_t offset = end;
        if (yarv_read_word(image, image->header.iseq_list_offset, 4, &body0) && body0 < image->header.iseq_list_offset &&
            image->header.iseq_list_offset - body0 >= size_mjit) {
            offset = yarv_up((unsigned)end, w) + 3u * w;
        }
        if (yarv_read_word(image, b + offset, 1, &x)) {
            info->catch_except_p = x != 0;
        }
    }
    return 1;
}

/* The fields of the 2.7+ body record, in dump order. */
enum {
    F_TYPE,
    F_ISEQ_SIZE,
    F_BYTECODE,
    F_BYTECODE_SIZE,
    F_PARAM_FLAGS,
    F_PARAM_SIZE,
    F_LEAD,
    F_OPT,
    F_REST,
    F_POST_START,
    F_POST_NUM,
    F_BLOCK,
    F_OPT_TABLE,
    F_KEYWORD,
    F_PATHOBJ,
    F_BASE_LABEL,
    F_LABEL,
    F_FIRST_LINENO,
    F_NODE_ID,
    F_BEG_LINENO,
    F_BEG_COLUMN,
    F_END_LINENO,
    F_END_COLUMN,
    F_INSNS_BODY,
    F_INSNS_POSITIONS,
    F_INSNS_SIZE,
    F_LOCAL_TABLE,
    F_LVAR_STATES,
    F_CATCH_SIZE,
    F_CATCH,
    F_PARENT,
    F_LOCAL,
    F_MANDATORY,
    F_CI,
    F_OUTER,
    F_FLIP,
    F_LOCAL_SIZE,
    F_IS_SIZE,
    F_IVC,
    F_ICVARC,
    F_ISE,
    F_IC,
    F_CI_SIZE,
    F_CI_KW_SIZE,
    F_STACK_MAX,
    F_CATCH_EXCEPT,
    F_BUILTIN,
    F_PRISM,
    F_END
};

/* The record layout of a release: the fields in dump order. */
static size_t yarv_fields(uint32_t version, uint8_t *fields)
{
    size_t n = 0;
    unsigned f;

    for (f = F_TYPE; f <= F_LOCAL_TABLE; ++f) {
        fields[n++] = (uint8_t)f;
    }
    if (version >= YARV_MODE(4, 0)) {
        fields[n++] = F_LVAR_STATES;
    }
    fields[n++] = F_CATCH_SIZE;
    fields[n++] = F_CATCH;
    fields[n++] = F_PARENT;
    fields[n++] = F_LOCAL;
    if (version >= YARV_MODE(3, 1)) {
        fields[n++] = F_MANDATORY;
    }
    fields[n++] = F_CI;
    if (version >= YARV_MODE(3, 0)) {
        fields[n++] = F_OUTER;
    }
    fields[n++] = F_FLIP;
    fields[n++] = F_LOCAL_SIZE;
    if (version >= YARV_MODE(3, 2)) {
        fields[n++] = F_IVC;
        fields[n++] = F_ICVARC;
        fields[n++] = F_ISE;
        fields[n++] = F_IC;
    } else {
        fields[n++] = F_IS_SIZE;
    }
    fields[n++] = F_CI_SIZE;
    if (version == YARV_MODE(2, 7)) {
        fields[n++] = F_CI_KW_SIZE;
    }
    fields[n++] = F_STACK_MAX;
    if (version <= YARV_MODE(3, 2)) {
        fields[n++] = F_CATCH_EXCEPT;
    }
    if (version >= YARV_MODE(3, 0)) {
        fields[n++] = F_BUILTIN;
    }
    if (version >= YARV_MODE(3, 4)) {
        fields[n++] = F_PRISM;
    }
    return n;
}

static int yarv_iseq_info_compact(const yarv_image *image, xxbyte_yarv_iseq_info *info)
{
    uint8_t fields[F_END];
    size_t count = yarv_fields(image->version, fields);
    uint32_t pos = info->code.body_offset;
    uint32_t body = info->code.body_offset;
    size_t i;

    for (i = 0; i < count; ++i) {
        uint64_t v;
        uint32_t u;
        int32_t s;
        uint32_t relative;

        if (!yarv_read_small(image, &pos, &v)) {
            return 0;
        }
        u = (uint32_t)v;
        s = (int32_t)xxbyte_sext(v, 32);
        relative = v <= body ? body - (uint32_t)v : 0;
        switch (fields[i]) {
            case F_PARAM_FLAGS: info->param_flags = u; break;
            case F_PARAM_SIZE: info->param_size = u; break;
            case F_LEAD: info->lead_num = s; break;
            case F_OPT: info->opt_num = s; break;
            case F_REST: info->rest_start = s; break;
            case F_POST_START: info->post_start = s; break;
            case F_POST_NUM: info->post_num = s; break;
            case F_BLOCK: info->block_start = s; break;
            case F_OPT_TABLE: info->opt_table_offset = relative; break;
            case F_KEYWORD: info->keyword_offset = u; break;
            case F_PATHOBJ: info->pathobj = v; break;
            case F_BASE_LABEL: info->base_label = v; break;
            case F_LABEL: info->label = v; break;
            case F_FIRST_LINENO:
                if (image->version <= YARV_MODE(3, 1)) {
                    if (!yarv_fixnum(v, image->word, &info->first_lineno)) {
                        info->first_lineno = 0;
                    }
                } else {
                    info->first_lineno = s;
                }
                break;
            case F_NODE_ID: info->node_id = s; break;
            case F_BEG_LINENO: info->beg_lineno = s; break;
            case F_BEG_COLUMN: info->beg_column = s; break;
            case F_END_LINENO: info->end_lineno = s; break;
            case F_END_COLUMN: info->end_column = s; break;
            case F_INSNS_BODY: info->insns_info_offset = relative; break;
            case F_INSNS_POSITIONS: info->insns_positions_offset = relative; break;
            case F_INSNS_SIZE: info->insns_info_size = u; break;
            case F_LOCAL_TABLE: info->local_table_offset = relative; break;
            case F_CATCH_SIZE: info->catch_table_size = u; break;
            case F_CATCH: info->catch_table_offset = relative; break;
            case F_PARENT: info->parent_iseq = s; break;
            case F_LOCAL: info->local_iseq = s; break;
            case F_MANDATORY: info->mandatory_only_iseq = s; break;
            case F_CI: info->ci_offset = relative; break;
            case F_OUTER: info->outer_variables_offset = relative; break;
            case F_FLIP: info->flip_count = (int64_t)v; break;
            case F_LOCAL_SIZE: info->local_table_size = u; break;
            case F_IS_SIZE: info->is_size = u; break;
            case F_IVC: info->ivc_size = u; break;
            case F_ICVARC: info->icvarc_size = u; break;
            case F_ISE: info->ise_size = u; break;
            case F_IC: info->ic_size = u; break;
            case F_CI_SIZE: info->ci_size = u; break;
            case F_CI_KW_SIZE: info->ci_kw_size = u; break;
            case F_STACK_MAX: info->stack_max = u; break;
            case F_CATCH_EXCEPT: info->catch_except_p = u != 0; break;
            case F_BUILTIN: info->builtin_attrs = u; break;
            case F_PRISM: info->prism = u; break;
            default: break;
        }
    }
    if (image->version >= YARV_MODE(3, 2)) {
        info->is_size = info->ivc_size + info->icvarc_size + info->ise_size + info->ic_size;
    }
    if (info->keyword_offset != 0) {
        if (!yarv_read_i32(image, info->keyword_offset, &info->keyword_num) || !yarv_read_i32(image, info->keyword_offset + 4u, &info->keyword_required_num) ||
            !yarv_read_i32(image, info->keyword_offset + 8u, &info->keyword_bits_start) || !yarv_read_i32(image, info->keyword_offset + 12u, &info->keyword_rest_start)) {
            return 0;
        }
    }
    return 1;
}

int yarv_iseq_info(const yarv_image *image, uint32_t index, xxbyte_yarv_iseq_info *info, xxbyte_status *status)
{
    memset(info, 0, sizeof(*info));
    *status = xxbyte_yarv_locate_iseq(image->data, image->size, &image->header, index, &info->code);
    if (*status != XXBYTE_STATUS_OK) {
        return 0;
    }
    info->parent_iseq = -1;
    info->local_iseq = -1;
    info->mandatory_only_iseq = -1;
    if (!(image->version < YARV_MODE(2, 7) ? yarv_iseq_info_2_6(image, info) : yarv_iseq_info_compact(image, info))) {
        memset(info, 0, sizeof(*info));
        *status = XXBYTE_STATUS_TRUNCATED;
        return 0;
    }
    return 1;
}

/* Public wrappers. */

static xxbyte_status yarv_setup(yarv_image *image, const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header)
{
    return yarv_image_init(image, data, size, header) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_INVALID_ARGUMENT;
}

xxbyte_status xxbyte_yarv_read_object(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint64_t index, xxbyte_yarv_object *object)
{
    yarv_image image;
    xxbyte_status status;
    int result;

    if (object == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(object, 0, sizeof(*object));
    status = yarv_setup(&image, data, size, header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    result = yarv_object_status(&image, index, object);
    switch (result) {
        case 1: return XXBYTE_STATUS_OK;
        case 0: return XXBYTE_STATUS_TRUNCATED;
        case -1: return XXBYTE_STATUS_INVALID_INSTRUCTION;
        default: return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
}

xxbyte_status xxbyte_yarv_object_elements(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_object *object, uint64_t *indexes,
                                          size_t capacity, size_t *count)
{
    yarv_image image;
    yarv_elements it;
    uint64_t total;
    uint64_t index;
    size_t n = 0;
    xxbyte_status status;

    if (count == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    if (object == NULL || (indexes == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = yarv_setup(&image, data, size, header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (object->special_const || (object->type != XXBYTE_YARV_T_ARRAY && object->type != XXBYTE_YARV_T_HASH)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    total = object->type == XXBYTE_YARV_T_HASH ? 2u * object->length : object->length;
    yarv_elements_init(&it, &image, object);
    while ((uint64_t)n < total) {
        if (!yarv_elements_next(&it, &index)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (n < capacity) {
            indexes[n] = index;
        }
        ++n;
    }
    *count = n;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_yarv_id_object(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint64_t id, uint64_t *object_index)
{
    yarv_image image;
    xxbyte_status status;

    if (object_index == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *object_index = 0;
    status = yarv_setup(&image, data, size, header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (image.version < YARV_MODE(2, 7) && id >= image.header.id_list_size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return yarv_id_object(&image, id, object_index) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_TRUNCATED;
}

xxbyte_status xxbyte_yarv_parse_iseq(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint32_t index, xxbyte_yarv_iseq_info *info)
{
    yarv_image image;
    xxbyte_status status;

    if (info == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(info, 0, sizeof(*info));
    status = yarv_setup(&image, data, size, header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    yarv_iseq_info(&image, index, info, &status);
    return status;
}

xxbyte_status xxbyte_yarv_local_id(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_iseq_info *info, uint32_t i, uint64_t *id)
{
    yarv_image image;
    xxbyte_status status;

    if (id == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *id = 0;
    if (info == NULL || i >= info->local_table_size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = yarv_setup(&image, data, size, header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    return yarv_read_word(&image, (uint64_t)info->local_table_offset + (uint64_t)i * image.word, image.word, id) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_TRUNCATED;
}

xxbyte_status xxbyte_yarv_read_catch_table(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_iseq_info *info,
                                           xxbyte_yarv_catch_entry *entries, size_t capacity, size_t *count)
{
    yarv_image image;
    xxbyte_status status;
    uint32_t i;
    uint32_t pos;

    if (count == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    if (info == NULL || (entries == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = yarv_setup(&image, data, size, header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    pos = info->catch_table_offset;
    for (i = 0; i < info->catch_table_size; ++i) {
        xxbyte_yarv_catch_entry entry;
        uint64_t v[6];
        unsigned k;

        if (image.version < YARV_MODE(2, 7)) {
            unsigned w = image.word;
            uint64_t at = (uint64_t)info->catch_table_offset + 4u + (uint64_t)i * (w == 8 ? 32u : 24u);
            if (!yarv_read_word(&image, at, 4, &v[1]) || !yarv_read_word(&image, at + w, w, &v[0]) || !yarv_read_word(&image, at + 2u * w, 4, &v[2]) ||
                !yarv_read_word(&image, at + 2u * w + 4u, 4, &v[3]) || !yarv_read_word(&image, at + 2u * w + 8u, 4, &v[4]) ||
                !yarv_read_word(&image, at + 2u * w + 12u, 4, &v[5])) {
                return XXBYTE_STATUS_TRUNCATED;
            }
        } else {
            for (k = 0; k < 6; ++k) {
                if (!yarv_read_small(&image, &pos, &v[k])) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
            }
        }
        entry.iseq = (int32_t)xxbyte_sext(v[0], 32);
        entry.type = (uint32_t)(v[1] >> 1); /* stored as INT2FIX(type) */
        entry.start = (uint32_t)v[2];
        entry.end = (uint32_t)v[3];
        entry.cont = (uint32_t)v[4];
        entry.sp = (uint32_t)v[5];
        if (i < capacity) {
            entries[i] = entry;
        }
    }
    *count = info->catch_table_size;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_yarv_read_call_info(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_iseq_info *info,
                                         xxbyte_yarv_call_info *infos, size_t capacity, size_t *count)
{
    yarv_image image;
    xxbyte_status status;
    uint64_t total;
    uint64_t i;
    uint32_t pos;

    if (count == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    if (info == NULL || (infos == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = yarv_setup(&image, data, size, header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    total = (uint64_t)info->ci_size + info->ci_kw_size;
    pos = info->ci_offset;
    for (i = 0; i < total; ++i) {
        xxbyte_yarv_call_info ci;
        int keyword = i >= info->ci_size;

        memset(&ci, 0, sizeof(ci));
        ci.present = 1;
        if (image.version < YARV_MODE(2, 7)) {
            unsigned w = image.word;
            unsigned plain = (unsigned)yarv_up(w + 8u, w);
            unsigned kw = (unsigned)yarv_up(plain + w, w);
            uint64_t at = keyword ? (uint64_t)info->ci_offset + (uint64_t)info->ci_size * plain + (i - info->ci_size) * kw : (uint64_t)info->ci_offset + i * plain;
            uint64_t flag;
            uint64_t argc;
            if (!yarv_read_word(&image, at, w, &ci.mid) || !yarv_read_word(&image, at + w, 4, &flag) || !yarv_read_word(&image, at + w + 4u, 4, &argc)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            ci.flag = (uint32_t)flag;
            ci.argc = (uint32_t)argc;
            if (keyword) {
                uint64_t kw_arg;
                int32_t length;
                if (!yarv_read_word(&image, at + plain, w, &kw_arg) || !yarv_read_i32(&image, kw_arg, &length) || length < 0 ||
                    !yarv_fits(&image, kw_arg + 4u, (uint64_t)length * w)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                ci.keyword_count = (uint32_t)length;
                ci.keyword_offset = (uint32_t)kw_arg + 4u;
            }
        } else {
            uint64_t v;
            uint64_t none = image.word == 4 ? 0xFFFFFFFFu : UINT64_MAX;
            if (!yarv_read_small(&image, &pos, &ci.mid)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            if (image.version >= YARV_MODE(3, 0) && ci.mid == none) {
                ci.mid = 0;
                ci.present = 0;
            } else {
                if (!yarv_read_small(&image, &pos, &v)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                ci.flag = (uint32_t)v;
                if (!yarv_read_small(&image, &pos, &v)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                ci.argc = (uint32_t)v;
                if (keyword || image.version >= YARV_MODE(3, 0)) {
                    uint64_t k;
                    int32_t length;
                    if (!yarv_read_small(&image, &pos, &v)) {
                        return XXBYTE_STATUS_TRUNCATED;
                    }
                    length = (int32_t)xxbyte_sext(v, 32);
                    ci.keyword_offset = pos;
                    if (length > 0) {
                        if ((uint64_t)length > image.size) {
                            return XXBYTE_STATUS_TRUNCATED;
                        }
                        ci.keyword_count = (uint32_t)length;
                        for (k = 0; k < ci.keyword_count; ++k) {
                            if (!yarv_read_small(&image, &pos, &v)) {
                                return XXBYTE_STATUS_TRUNCATED;
                            }
                        }
                    }
                }
            }
        }
        if (i < capacity) {
            infos[i] = ci;
        }
    }
    *count = (size_t)total;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_yarv_call_info_keyword(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_call_info *ci, uint32_t i,
                                            uint64_t *object_index)
{
    yarv_image image;
    xxbyte_status status;
    uint32_t pos;
    uint32_t k;

    if (object_index == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *object_index = 0;
    if (ci == NULL || i >= ci->keyword_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = yarv_setup(&image, data, size, header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (image.version < YARV_MODE(2, 7)) {
        return yarv_read_word(&image, (uint64_t)ci->keyword_offset + (uint64_t)i * image.word, image.word, object_index) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_TRUNCATED;
    }
    pos = ci->keyword_offset;
    for (k = 0; k <= i; ++k) {
        if (!yarv_read_small(&image, &pos, object_index)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_yarv_read_insns_info(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_iseq_info *info,
                                          xxbyte_yarv_insn_info *entries, size_t capacity, size_t *count)
{
    yarv_image image;
    xxbyte_status status;
    uint32_t i;
    uint32_t body;
    uint32_t positions;
    uint32_t last = 0;

    if (count == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    if (info == NULL || (entries == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = yarv_setup(&image, data, size, header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    body = info->insns_info_offset;
    positions = info->insns_positions_offset;
    for (i = 0; i < info->insns_info_size; ++i) {
        xxbyte_yarv_insn_info entry;
        uint64_t v;

        memset(&entry, 0, sizeof(entry));
        if (image.version < YARV_MODE(2, 7)) {
            uint64_t at = (uint64_t)info->insns_info_offset + 8u * (uint64_t)i;
            if (!yarv_read_i32(&image, at, &entry.line_no) || !yarv_read_word(&image, at + 4u, 4, &v)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            entry.events = (uint32_t)v;
            if (!yarv_read_word(&image, (uint64_t)info->insns_positions_offset + 4u * (uint64_t)i, 4, &v)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            entry.position = (uint32_t)v;
        } else {
            if (!yarv_read_small(&image, &body, &v)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            entry.line_no = (int32_t)xxbyte_sext(v, 32);
            if (image.version >= YARV_MODE(3, 1)) {
                if (!yarv_read_small(&image, &body, &v)) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                entry.node_id = (int32_t)xxbyte_sext(v, 32);
            }
            if (!yarv_read_small(&image, &body, &v)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            entry.events = (uint32_t)v;
            if (!yarv_read_small(&image, &positions, &v)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            last += (uint32_t)v;
            entry.position = last;
        }
        if (i < capacity) {
            entries[i] = entry;
        }
    }
    *count = info->insns_info_size;
    return XXBYTE_STATUS_OK;
}

#else
typedef int xxbyte_yarv_ibf_disabled;
#endif
