#include "xxbyte_avm2_internal.h"

#if XXBYTE_USE_AVM2

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

/*
 * Containers: SWF files (avmplus shell/swf.cpp and utils/abcdump.as read
 * them the same way) and abcFiles.
 */

#define SWF_HEADER 8u
/* The largest DEFLATE expansion: 258 bytes from a 2-bit code, about 1032
 * output bytes per input byte. */
#define DEFLATE_RATIO 1032u

static int is_swf_signature(const uint8_t *data, size_t size)
{
    return size >= 3 && (data[0] == 'F' || data[0] == 'C' || data[0] == 'Z') && data[1] == 'W' && data[2] == 'S';
}

xxbyte_status xxbyte_avm2_swf_open(const uint8_t *data, size_t size, uint8_t **swf, size_t *swf_size, uint32_t *notice)
{
    uint32_t file_length;
    size_t capacity;
    uint8_t *out;
    uint32_t flags = 0;

    if (swf != NULL) {
        *swf = NULL;
    }
    if (swf_size != NULL) {
        *swf_size = 0;
    }
    if (notice != NULL) {
        *notice = 0;
    }
    if (data == NULL || swf == NULL || swf_size == NULL || notice == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (!is_swf_signature(data, size)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (data[0] == 'Z') {
        return XXBYTE_STATUS_UNSUPPORTED_FORMAT;
    }
    if (size < SWF_HEADER) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    file_length = xxbyte_u32le(data + 4);
    if (file_length < SWF_HEADER) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (data[0] == 'F') {
        capacity = file_length;
        if (size < capacity) {
            capacity = size;
            flags |= XXBYTE_AVM2_SWF_NOTICE_TRUNCATED;
        } else if (size > capacity) {
            flags |= XXBYTE_AVM2_SWF_NOTICE_LENGTH;
        }
        out = (uint8_t *)xx_mem_alloc(capacity);
        if (out == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        memcpy(out, data, capacity);
        *swf = out;
        *swf_size = capacity;
        *notice = flags;
        return XXBYTE_STATUS_OK;
    } else {
        size_t in_size = size - SWF_HEADER;
        size_t produced = 0;
        uint64_t bound = (uint64_t)in_size * DEFLATE_RATIO + 64u;
        xxbyte_status status;
        int capped = 0;

        capacity = file_length - SWF_HEADER;
        if (capacity > XXBYTE_AVM2_SWF_LIMIT - SWF_HEADER) {
            capacity = XXBYTE_AVM2_SWF_LIMIT - SWF_HEADER;
            capped = 1;
        }
        if (capacity > bound) {
            capacity = (size_t)bound;
            capped = 1;
        }
        out = (uint8_t *)xx_mem_alloc(capacity + SWF_HEADER);
        if (out == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        memcpy(out, data, SWF_HEADER);
        status = xxbyte_inflate(data + SWF_HEADER, in_size, 1, out + SWF_HEADER, capacity, &produced, NULL);
        switch (status) {
            case XXBYTE_STATUS_OK:
                if (produced < file_length - SWF_HEADER) {
                    flags |= XXBYTE_AVM2_SWF_NOTICE_LENGTH;
                }
                break;
            case XXBYTE_STATUS_TRUNCATED: flags |= XXBYTE_AVM2_SWF_NOTICE_TRUNCATED; break;
            case XXBYTE_STATUS_OUT_OF_MEMORY:
                /* More output than FileLength (or the limit) allows. */
                flags |= capped ? XXBYTE_AVM2_SWF_NOTICE_TRUNCATED : XXBYTE_AVM2_SWF_NOTICE_LENGTH;
                break;
            default:
                if (produced == 0) {
                    xx_mem_free(out);
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                flags |= XXBYTE_AVM2_SWF_NOTICE_CHECKSUM;
                break;
        }
        *swf = out;
        *swf_size = produced + SWF_HEADER;
        *notice = flags;
        return XXBYTE_STATUS_OK;
    }
}

/* The offset of the first tag: after the header, the frame RECT, the frame
 * rate and the frame count. */
static int first_tag(const uint8_t *swf, size_t size, size_t *pos, uint32_t *frame_count)
{
    size_t rect;

    if (size < SWF_HEADER + 1u) {
        return 0;
    }
    rect = (5u + 4u * (size_t)(swf[SWF_HEADER] >> 3) + 7u) / 8u;
    if (size - SWF_HEADER < rect + 4u) {
        return 0;
    }
    *pos = SWF_HEADER + rect + 4u;
    if (frame_count != NULL) {
        *frame_count = xxbyte_u16le(swf + SWF_HEADER + rect + 2u);
    }
    return 1;
}

xxbyte_status xxbyte_avm2_swf_abc_tags(const uint8_t *swf, size_t size, xxbyte_avm2_swf_abc *tags, size_t capacity, size_t *count)
{
    size_t pos;
    size_t found = 0;
    xxbyte_status status = XXBYTE_STATUS_OK;

    if (count != NULL) {
        *count = 0;
    }
    if (swf == NULL || count == NULL || (tags == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size > UINT32_MAX) {
        size = UINT32_MAX;
    }
    if (!first_tag(swf, size, &pos, NULL)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    while (pos < size) {
        uint16_t header;
        uint32_t code;
        uint64_t length;
        size_t tag_offset = pos;

        if (size - pos < 2u) {
            status = XXBYTE_STATUS_TRUNCATED;
            break;
        }
        header = xxbyte_u16le(swf + pos);
        pos += 2;
        code = (uint32_t)header >> 6;
        length = header & 0x3Fu;
        if (length == 0x3Fu) {
            if (size - pos < 4u) {
                status = XXBYTE_STATUS_TRUNCATED;
                break;
            }
            length = xxbyte_u32le(swf + pos);
            pos += 4;
        }
        if (length > size - pos) {
            status = XXBYTE_STATUS_TRUNCATED;
            break;
        }
        if (code == 0) {
            break;
        }
        if (code == XXBYTE_AVM2_SWF_TAG_DO_ABC || code == XXBYTE_AVM2_SWF_TAG_DO_ABC2) {
            xxbyte_avm2_swf_abc tag;
            size_t abc = pos;
            size_t end = pos + (size_t)length;

            memset(&tag, 0, sizeof(tag));
            tag.tag_code = code;
            tag.tag_offset = (uint32_t)tag_offset;
            if (code == XXBYTE_AVM2_SWF_TAG_DO_ABC2) {
                size_t name;
                if (length < 5u) {
                    status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                    break;
                }
                tag.flags = xxbyte_u32le(swf + pos);
                name = pos + 4u;
                abc = name;
                while (abc < end && swf[abc] != 0) {
                    ++abc;
                }
                if (abc == end) {
                    status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                    break;
                }
                tag.name_offset = (uint32_t)name;
                tag.name_length = (uint32_t)(abc - name);
                ++abc;
            }
            tag.abc_offset = (uint32_t)abc;
            tag.abc_size = (uint32_t)(end - abc);
            if (found < capacity) {
                tags[found] = tag;
            }
            ++found;
        }
        pos += (size_t)length;
    }
    *count = found;
    return status;
}

/* ---- listing ---- */

static int append(xx_buf_t *out, const char *text)
{
    return xx_buf_append_str(out, text) ? 1 : 0;
}

static const char *trait_kind_word(uint32_t kind)
{
    switch (kind) {
        case XXBYTE_AVM2_TRAIT_GETTER: return "getter";
        case XXBYTE_AVM2_TRAIT_SETTER: return "setter";
        case XXBYTE_AVM2_TRAIT_FUNCTION: return "function";
        default: return "method";
    }
}

/* "instance method QName(...) disp_id 1", "closure in method 0", ... */
static int append_role(const xxbyte_avm2_names *names, uint32_t method, xx_buf_t *out)
{
    xxbyte_avm2_method_owner owner;
    char text[1024];
    const char *scope = NULL;

    if (xxbyte_avm2_names_owner(names, method, &owner) != XXBYTE_STATUS_OK) {
        return append(out, "unknown method");
    }
    switch (owner.role) {
        case XXBYTE_AVM2_OWNER_INSTANCE_INIT: return xx_buf_appendf(out, "instance initializer of class %u", (unsigned)owner.index) ? 1 : 0;
        case XXBYTE_AVM2_OWNER_CLASS_INIT: return xx_buf_appendf(out, "class initializer of class %u", (unsigned)owner.index) ? 1 : 0;
        case XXBYTE_AVM2_OWNER_SCRIPT_INIT: return xx_buf_appendf(out, "initializer of script %u", (unsigned)owner.index) ? 1 : 0;
        case XXBYTE_AVM2_OWNER_INSTANCE_TRAIT: scope = "instance"; break;
        case XXBYTE_AVM2_OWNER_CLASS_TRAIT: scope = "class"; break;
        case XXBYTE_AVM2_OWNER_SCRIPT_TRAIT: scope = "script"; break;
        case XXBYTE_AVM2_OWNER_CLOSURE: return xx_buf_appendf(out, "closure created in method %u", (unsigned)owner.index) ? 1 : 0;
        default: return append(out, "not referenced");
    }
    xxbyte_avm2_format_multiname(names, owner.trait_name, text, sizeof(text));
    return xx_buf_appendf(out, "%s %s trait %s of %s %u, disp_id %u", scope, trait_kind_word(owner.trait_kind), text,
                          owner.role == XXBYTE_AVM2_OWNER_SCRIPT_TRAIT ? "script" : "class", (unsigned)owner.index, (unsigned)owner.disp_id)
               ? 1
               : 0;
}

static int append_header(const xxbyte_avm2_names *names, uint32_t body_index, const xxbyte_avm2_method_body *body, xx_buf_t *out)
{
    char name[1024];
    char signature[1024];

    xxbyte_avm2_method_name(names, body->method, name, sizeof(name));
    if (xxbyte_avm2_method_signature(names, body->method, signature, sizeof(signature)) == 0) {
        memcpy(signature, "(?)", 4);
    }
    return xx_buf_appendf(out, "method %u %s%s; ", (unsigned)body->method, name, signature) && append_role(names, body->method, out) &&
           xx_buf_appendf(out, "; body %u, max_stack %u, local_count %u, scope_depth %u..%u, code_length %u\n", (unsigned)body_index, (unsigned)body->max_stack,
                          (unsigned)body->local_count, (unsigned)body->init_scope_depth, (unsigned)body->max_scope_depth, (unsigned)body->code_length);
}

static int append_target(xx_buf_t *out, uint32_t target)
{
    return xx_buf_appendf(out, "0x%04x", (unsigned)target) ? 1 : 0;
}

/* A multiname operand of the exception table, or multiname@N. */
static int append_multiname(const xxbyte_avm2_names *names, uint32_t index, xx_buf_t *out)
{
    char text[1024];

    if (xxbyte_avm2_format_multiname(names, index, text, sizeof(text)) == 0) {
        return xx_buf_appendf(out, "multiname@%u", (unsigned)index) ? 1 : 0;
    }
    return append(out, text);
}

static int append_exceptions(const uint8_t *data, size_t size, const xxbyte_avm2_names *names, const xxbyte_avm2_method_body *body, xx_buf_t *out)
{
    uint32_t i;
    size_t pos = body->exception_offset;

    for (i = 0; i < body->exception_count; ++i) {
        xxbyte_avm2_method_body one = *body;
        xxbyte_avm2_exception entry;
        size_t count = 0;
        uint32_t k;

        one.exception_offset = (uint32_t)pos;
        one.exception_count = 1;
        if (xxbyte_avm2_parse_exceptions(data, size, &one, &entry, 1, &count) != XXBYTE_STATUS_OK || count != 1) {
            return append(out, "exception table: malformed\n");
        }
        for (k = 0; k < 5; ++k) {
            uint32_t skip;
            if (xxbyte_avm2_read_u30(data, size, &pos, &skip) != XXBYTE_STATUS_OK) {
                return append(out, "exception table: malformed\n");
            }
        }
        if (!xx_buf_appendf(out, "exception %u: from ", (unsigned)i) || !append_target(out, entry.from) || !append(out, " to ") || !append_target(out, entry.to) ||
            !append(out, ", target ") || !append_target(out, entry.target) || !append(out, ", type ") || !append_multiname(names, entry.exc_type, out)) {
            return 0;
        }
        if (entry.var_name != 0 && (!append(out, ", name ") || !append_multiname(names, entry.var_name, out))) {
            return 0;
        }
        if (!append(out, "\n")) {
            return 0;
        }
    }
    return 1;
}

/* Lists the abcFile at data[0..size). */
static xxbyte_status list_abc(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_avm2_names *names = NULL;
    const xxbyte_avm2_abc *abc;
    const xxbyte_avm2_method_body *bodies;
    size_t body_count;
    size_t b;
    uint32_t decode_mode;
    xxbyte_status status = xxbyte_avm2_names_create(data, size, &names);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    abc = xxbyte_avm2_names_abc(names);
    bodies = xxbyte_avm2_names_bodies(names, &body_count);
    decode_mode = mode == XXBYTE_MODE_AUTO ? abc->mode : mode;
    if (!xx_buf_appendf(out, "abc %u.%u, %u bytes, %u methods, %u bodies, %u classes, %u scripts\n", (unsigned)abc->major_version, (unsigned)abc->minor_version,
                        (unsigned)abc->size, (unsigned)abc->tables[XXBYTE_AVM2_TABLE_METHOD].count, (unsigned)body_count,
                        (unsigned)abc->tables[XXBYTE_AVM2_TABLE_INSTANCE].count, (unsigned)abc->tables[XXBYTE_AVM2_TABLE_SCRIPT].count)) {
        xxbyte_avm2_names_destroy(names);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (b = 0; b < body_count && status == XXBYTE_STATUS_OK; ++b) {
        const xxbyte_avm2_method_body *body = &bodies[b];

        (void)xxbyte_avm2_names_select_body(names, (uint32_t)b);
        if (!append(out, "\n") || !append_header(names, (uint32_t)b, body, out)) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        status =
            xxbyte_disasm_buf_ex(XXBYTE_FAMILY_AVM2, decode_mode, NULL, data + body->code_offset, body->code_length, 0, listing_flags, xxbyte_avm2_resolve, names, out);
        if (status == XXBYTE_STATUS_OK && !append_exceptions(data, abc->size, names, body, out)) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    xxbyte_avm2_names_destroy(names);
    if (status == XXBYTE_STATUS_OK && !xx_buf_ok(out)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return status;
}

static int append_notices(uint32_t notice, xx_buf_t *out)
{
    if ((notice & XXBYTE_AVM2_SWF_NOTICE_TRUNCATED) != 0 && !append(out, "notice: the SWF data ends early\n")) {
        return 0;
    }
    if ((notice & XXBYTE_AVM2_SWF_NOTICE_CHECKSUM) != 0 && !append(out, "notice: the zlib stream is corrupt after its last output\n")) {
        return 0;
    }
    if ((notice & XXBYTE_AVM2_SWF_NOTICE_LENGTH) != 0 && !append(out, "notice: the data size disagrees with FileLength\n")) {
        return 0;
    }
    return 1;
}

static xxbyte_status list_swf(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    uint8_t *swf = NULL;
    size_t swf_size = 0;
    uint32_t notice = 0;
    uint32_t frame_count = 0;
    size_t pos;
    size_t count = 0;
    size_t i;
    xxbyte_avm2_swf_abc *tags = NULL;
    xxbyte_status status;
    xxbyte_status walk;
    xxbyte_status result = XXBYTE_STATUS_OK;

    if (size >= 4 && data[0] == 'Z') {
        (void)xx_buf_appendf(out, "swf ZWS version %u: LZMA-compressed SWF files are not supported\n", (unsigned)data[3]);
        return XXBYTE_STATUS_UNSUPPORTED_FORMAT;
    }
    status = xxbyte_avm2_swf_open(data, size, &swf, &swf_size, &notice);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (!first_tag(swf, swf_size, &pos, &frame_count)) {
        xx_mem_free(swf);
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (!xx_buf_appendf(out, "swf %cWS version %u, file_length %u, frame_count %u\n", (char)data[0], (unsigned)data[3], (unsigned)xxbyte_u32le(data + 4),
                        (unsigned)frame_count) ||
        !append_notices(notice, out)) {
        xx_mem_free(swf);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    walk = xxbyte_avm2_swf_abc_tags(swf, swf_size, NULL, 0, &count);
    if (count != 0) {
        tags = (xxbyte_avm2_swf_abc *)xx_mem_calloc(count, sizeof(*tags));
        if (tags == NULL) {
            xx_mem_free(swf);
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        (void)xxbyte_avm2_swf_abc_tags(swf, swf_size, tags, count, &count);
    }
    for (i = 0; i < count; ++i) {
        const xxbyte_avm2_swf_abc *tag = &tags[i];
        int ok;

        if (tag->tag_code == XXBYTE_AVM2_SWF_TAG_DO_ABC2) {
            ok = xx_buf_appendf(out, "\nDoABC2 tag at 0x%x, flags 0x%x%s, name ", (unsigned)tag->tag_offset, (unsigned)tag->flags,
                                (tag->flags & XXBYTE_AVM2_DO_ABC_LAZY_INITIALIZE) != 0 ? " (lazy initialize)" : "") &&
                 xxbyte_avm2_append_quoted(out, swf + tag->name_offset, tag->name_length) &&
                 xx_buf_appendf(out, ", abc at 0x%x, %u bytes\n", (unsigned)tag->abc_offset, (unsigned)tag->abc_size);
        } else {
            ok = xx_buf_appendf(out, "\nDoABC tag at 0x%x, abc at 0x%x, %u bytes\n", (unsigned)tag->tag_offset, (unsigned)tag->abc_offset, (unsigned)tag->abc_size);
        }
        if (!ok) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        status = list_abc(mode, swf + tag->abc_offset, tag->abc_size, listing_flags, out);
        if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
            result = status;
            break;
        }
        if (status != XXBYTE_STATUS_OK) {
            (void)xx_buf_appendf(out, "error: abcFile: %s\n", xxbyte_status_string(status));
            if (result == XXBYTE_STATUS_OK) {
                result = status;
            }
        }
    }
    if (result != XXBYTE_STATUS_OUT_OF_MEMORY) {
        if (count == 0 && !append(out, "no DoABC or DoABC2 tags\n")) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        if (walk != XXBYTE_STATUS_OK) {
            (void)xx_buf_appendf(out, "error: tags: %s\n", xxbyte_status_string(walk));
            if (result == XXBYTE_STATUS_OK) {
                result = walk;
            }
        }
    }
    xx_mem_free(tags);
    xx_mem_free(swf);
    return result;
}

xxbyte_status xxbyte_avm2_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_avm2_abc abc;
    xxbyte_status status;

    if (out == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (data == NULL || size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (is_swf_signature(data, size)) {
        return list_swf(mode, data, size, listing_flags, out);
    }
    status = xxbyte_avm2_parse_abc(data, size, &abc);
    if (status == XXBYTE_STATUS_UNSUPPORTED_MODE) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION; /* neither a SWF nor an abcFile */
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    return list_abc(mode, data, size, listing_flags, out);
}

#else
typedef int xxbyte_avm2_file_disabled;
#endif
