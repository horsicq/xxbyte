#include "xxbyte_squirrel_internal.h"

#if XXBYTE_USE_SQUIRREL

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#include <stdlib.h>

/*
 * .cnut walking (sqobject.cpp SQClosure::Save/Load, SQFunctionProto::Save/
 * Load, WriteObject/ReadObject; sqapi.cpp sq_writeclosure). Every record
 * takes at least one byte, so a walk ends after at most size steps.
 */

#define SQ_TAG_PART 0x50415254u /* 'PART' */
#define SQ_TAG_TAIL 0x5441494Cu /* 'TAIL' */
#define SQ_OP_CLOSURE_3 0x30u

typedef struct sq_reader {
    const uint8_t *data;
    size_t size;
    size_t pos;
    int big;
    uint32_t major;
    uint32_t char_size;
    uint32_t int_size;
    uint32_t float_size;
} sq_reader;

static void sq_reader_init(sq_reader *r, const uint8_t *data, size_t size, const xxbyte_squirrel_header *header)
{
    r->data = data;
    r->size = data != NULL ? size : 0;
    r->pos = 0;
    r->big = header->big_endian != 0;
    r->major = header->major;
    r->char_size = header->char_size;
    r->int_size = header->int_size;
    r->float_size = header->float_size;
}

static int sq_header_ok(const xxbyte_squirrel_header *header)
{
    return header != NULL && (header->major == 2 || header->major == 3) && (header->char_size == 1 || header->char_size == 2 || header->char_size == 4) &&
           (header->int_size == 4 || header->int_size == 8) && (header->float_size == 4 || header->float_size == 8);
}

static xxbyte_status sq_need(const sq_reader *r, uint64_t n)
{
    return n <= (uint64_t)(r->size - r->pos) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_TRUNCATED;
}

static xxbyte_status sq_skip(sq_reader *r, uint64_t n)
{
    xxbyte_status status = sq_need(r, n);
    if (status == XXBYTE_STATUS_OK) {
        r->pos += (size_t)n;
    }
    return status;
}

static uint32_t sq_get32(const sq_reader *r, const uint8_t *p)
{
    return r->big ? xxbyte_u32be(p) : xxbyte_u32le(p);
}

static xxbyte_status sq_u32(sq_reader *r, uint32_t *value)
{
    xxbyte_status status = sq_need(r, 4);
    if (status == XXBYTE_STATUS_OK) {
        *value = sq_get32(r, r->data + r->pos);
        r->pos += 4;
    }
    return status;
}

/* An SQInteger (int_size bytes), sign-extended. */
static xxbyte_status sq_int(sq_reader *r, int64_t *value)
{
    const uint8_t *p;
    xxbyte_status status = sq_need(r, r->int_size);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    p = r->data + r->pos;
    if (r->int_size == 4) {
        *value = xxbyte_sext(sq_get32(r, p), 32);
    } else {
        uint64_t v = r->big ? ((uint64_t)xxbyte_u32be(p) << 32) | xxbyte_u32be(p + 4) : xxbyte_u64le(p);
        *value = (int64_t)v;
    }
    r->pos += r->int_size;
    return XXBYTE_STATUS_OK;
}

/* A stream tag: an SQUnsignedInteger32 in 3.x, an SQInteger in 2.2. */
static xxbyte_status sq_tag(sq_reader *r, uint32_t expected)
{
    xxbyte_status status;
    int64_t tag;

    if (r->major == 2) {
        status = sq_int(r, &tag);
    } else {
        uint32_t t = 0;
        status = sq_u32(r, &t);
        tag = (int64_t)t;
    }
    if (status == XXBYTE_STATUS_OK && tag != (int64_t)expected) {
        status = XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return status;
}

static xxbyte_status sq_object(sq_reader *r, xxbyte_squirrel_object *object)
{
    xxbyte_squirrel_object o;
    xxbyte_status status;
    int64_t length;

    memset(&o, 0, sizeof(o));
    o.offset = r->pos;
    status = sq_u32(r, &o.type);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    switch (o.type) {
        case XXBYTE_SQUIRREL_OBJECT_NULL: break;
        case XXBYTE_SQUIRREL_OBJECT_BOOL:
            if (r->major == 2) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            /* fall through */
        case XXBYTE_SQUIRREL_OBJECT_INTEGER:
            o.size = r->int_size;
            status = sq_int(r, &o.value);
            break;
        case XXBYTE_SQUIRREL_OBJECT_FLOAT:
            o.size = r->float_size;
            status = sq_need(r, r->float_size);
            if (status == XXBYTE_STATUS_OK) {
                const uint8_t *p = r->data + r->pos;
                if (r->float_size == 4) {
                    o.value = (int64_t)sq_get32(r, p);
                } else {
                    o.value = (int64_t)(r->big ? ((uint64_t)xxbyte_u32be(p) << 32) | xxbyte_u32be(p + 4) : xxbyte_u64le(p));
                }
                r->pos += r->float_size;
            }
            break;
        case XXBYTE_SQUIRREL_OBJECT_STRING:
            status = sq_int(r, &length);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (length < 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if ((uint64_t)length > (uint64_t)(r->size - r->pos) / r->char_size) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            o.string_offset = r->pos;
            o.string_length = (uint64_t)length;
            r->pos += (size_t)((uint64_t)length * r->char_size);
            break;
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    o.end_offset = r->pos;
    if (object != NULL) {
        *object = o;
    }
    return XXBYTE_STATUS_OK;
}

/* A count: an SQInteger in 0..(remaining bytes / min_record), min_record
 * being the least size of one record of its list. */
static xxbyte_status sq_count(sq_reader *r, uint32_t min_record, uint32_t *count)
{
    int64_t v;
    xxbyte_status status = sq_int(r, &v);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (v < 0 || (uint64_t)v > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    /* Each later record of the list takes at least min_record bytes. */
    if ((uint64_t)v > (uint64_t)(r->size - r->pos) / min_record) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *count = (uint32_t)v;
    return XXBYTE_STATUS_OK;
}

typedef struct sq_walk {
    sq_reader r;
    xxbyte_squirrel_function *functions;
    size_t capacity;
    size_t count;
    int closure_seen;  /* a 3.x CLOSURE */
    int closure_bound; /* one with _arg2 other than 0 or 1 */
} sq_walk;

static xxbyte_status sq_objects(sq_reader *r, uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; ++i) {
        xxbyte_status status = sq_object(r, NULL);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status sq_function(sq_walk *w, uint32_t parent, uint32_t child, uint32_t depth)
{
    sq_reader *r = &w->r;
    xxbyte_squirrel_function f;
    xxbyte_status status;
    size_t index = w->count;
    /* Literals, parameters, outers, locals, line infos, defaults,
     * instructions, functions. */
    static const uint32_t min_records[8] = {4, 4, 12, 16, 8, 4, 8, 4};
    uint32_t counts[8];
    uint32_t i;
    uint64_t bytes;

    if (depth > XXBYTE_SQUIRREL_MAX_DEPTH) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    memset(&f, 0, sizeof(f));
    f.parent = parent;
    f.child = child;
    f.depth = depth;
    f.offset = r->pos;
    if ((status = sq_tag(r, SQ_TAG_PART)) != XXBYTE_STATUS_OK) {
        return status;
    }
    f.source_offset = r->pos;
    if ((status = sq_object(r, NULL)) != XXBYTE_STATUS_OK) {
        return status;
    }
    f.name_offset = r->pos;
    if ((status = sq_object(r, NULL)) != XXBYTE_STATUS_OK || (status = sq_tag(r, SQ_TAG_PART)) != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < 8; ++i) {
        if ((status = sq_count(r, min_records[i], &counts[i])) != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    f.literal_count = counts[0];
    f.parameter_count = counts[1];
    f.outer_count = counts[2];
    f.local_count = counts[3];
    f.lineinfo_count = counts[4];
    f.default_count = counts[5];
    f.instruction_count = counts[6];
    f.function_count = counts[7];

    if ((status = sq_tag(r, SQ_TAG_PART)) != XXBYTE_STATUS_OK) {
        return status;
    }
    f.literals_offset = r->pos;
    if ((status = sq_objects(r, f.literal_count)) != XXBYTE_STATUS_OK || (status = sq_tag(r, SQ_TAG_PART)) != XXBYTE_STATUS_OK) {
        return status;
    }
    f.parameters_offset = r->pos;
    if ((status = sq_objects(r, f.parameter_count)) != XXBYTE_STATUS_OK || (status = sq_tag(r, SQ_TAG_PART)) != XXBYTE_STATUS_OK) {
        return status;
    }
    f.outers_offset = r->pos;
    for (i = 0; i < f.outer_count; ++i) {
        if ((status = sq_skip(r, r->int_size)) != XXBYTE_STATUS_OK || (status = sq_object(r, NULL)) != XXBYTE_STATUS_OK ||
            (status = sq_object(r, NULL)) != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    if ((status = sq_tag(r, SQ_TAG_PART)) != XXBYTE_STATUS_OK) {
        return status;
    }
    f.locals_offset = r->pos;
    for (i = 0; i < f.local_count; ++i) {
        if ((status = sq_object(r, NULL)) != XXBYTE_STATUS_OK || (status = sq_skip(r, 3u * (uint64_t)r->int_size)) != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    if ((status = sq_tag(r, SQ_TAG_PART)) != XXBYTE_STATUS_OK) {
        return status;
    }
    f.lineinfos_offset = r->pos;
    bytes = (uint64_t)f.lineinfo_count * 2u * r->int_size;
    if ((status = sq_skip(r, bytes)) != XXBYTE_STATUS_OK || (status = sq_tag(r, SQ_TAG_PART)) != XXBYTE_STATUS_OK) {
        return status;
    }
    f.defaults_offset = r->pos;
    bytes = (uint64_t)f.default_count * r->int_size;
    if ((status = sq_skip(r, bytes)) != XXBYTE_STATUS_OK || (status = sq_tag(r, SQ_TAG_PART)) != XXBYTE_STATUS_OK) {
        return status;
    }
    f.instructions_offset = r->pos;
    bytes = (uint64_t)f.instruction_count * 8u;
    if ((status = sq_need(r, bytes)) != XXBYTE_STATUS_OK) {
        return status;
    }
    if (r->major == 3) {
        for (i = 0; i < f.instruction_count; ++i) {
            const uint8_t *p = r->data + r->pos + (size_t)i * 8u;
            if (p[4] == SQ_OP_CLOSURE_3) {
                w->closure_seen = 1;
                if (p[6] > 1) {
                    w->closure_bound = 1;
                }
            }
        }
    }
    r->pos += (size_t)bytes;
    if ((status = sq_tag(r, SQ_TAG_PART)) != XXBYTE_STATUS_OK) {
        return status;
    }
    if (index < w->capacity) {
        w->functions[index] = f;
    }
    ++w->count;
    for (i = 0; i < f.function_count; ++i) {
        if ((status = sq_function(w, (uint32_t)index, i, depth + 1u)) != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    if ((status = sq_int(r, &f.stack_size)) != XXBYTE_STATUS_OK || (status = sq_need(r, 1)) != XXBYTE_STATUS_OK) {
        return status;
    }
    f.generator = r->data[r->pos] != 0;
    r->pos += 1;
    if (r->major == 2) {
        if ((status = sq_need(r, 1)) != XXBYTE_STATUS_OK) {
            return status;
        }
        f.varparams = r->data[r->pos] != 0;
        r->pos += 1;
    } else {
        int64_t v;
        if ((status = sq_int(r, &v)) != XXBYTE_STATUS_OK) {
            return status;
        }
        f.varparams = v != 0;
    }
    f.end_offset = r->pos;
    if (index < w->capacity) {
        w->functions[index] = f;
    }
    return XXBYTE_STATUS_OK;
}

/* Walks the main function and the TAIL tag from header->function_offset. */
static xxbyte_status sq_walk_file(sq_walk *w, const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, uint64_t *end)
{
    xxbyte_status status;

    sq_reader_init(&w->r, data, size, header);
    if (header->function_offset > (uint64_t)w->r.size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    w->r.pos = (size_t)header->function_offset;
    w->count = 0;
    w->closure_seen = 0;
    w->closure_bound = 0;
    status = sq_function(w, XXBYTE_SQUIRREL_NO_PARENT, 0, 0);
    if (status == XXBYTE_STATUS_OK) {
        status = sq_tag(&w->r, SQ_TAG_TAIL);
    }
    if (status == XXBYTE_STATUS_OK && end != NULL) {
        *end = w->r.pos;
    }
    return status;
}

/* Matches data[0..size) against a prefix of a header start. */
static int sq_prefix(const uint8_t *data, size_t size, const uint8_t *start, size_t n)
{
    return memcmp(data, start, size < n ? size : n) == 0;
}

xxbyte_status xxbyte_squirrel_parse_header(const uint8_t *data, size_t size, xxbyte_squirrel_header *header)
{
    /* The stream tag and 'SQIR' as a 32-bit little- or big-endian tag, and
     * as a 2.2 64-bit big-endian one (the little-endian one starts as the
     * 32-bit one). */
    static const uint8_t start_le[6] = {0xFA, 0xFA, 'R', 'I', 'Q', 'S'};
    static const uint8_t start_be[6] = {0xFA, 0xFA, 'S', 'Q', 'I', 'R'};
    static const uint8_t start_be64[10] = {0xFA, 0xFA, 0, 0, 0, 0, 'S', 'Q', 'I', 'R'};
    xxbyte_squirrel_header h;
    xxbyte_status status;
    sq_walk w;
    sq_reader r;
    int64_t value;
    uint32_t i;

    if (header == NULL || (data == NULL && size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(header, 0, sizeof(*header));
    memset(&h, 0, sizeof(h));
    memset(&w, 0, sizeof(w));
    if (size < 10) {
        if (size == 0 || sq_prefix(data, size, start_le, 6) || sq_prefix(data, size, start_be, 6) || sq_prefix(data, size, start_be64, 10)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (memcmp(data, start_le, 6) == 0) {
        h.big_endian = 0;
        /* 2.2 with a 64-bit SQInteger writes 8-byte tags: the high half of
         * 'SQIR' is 0 where 3.x and 32-bit 2.2 have the character size. */
        h.int_size = xxbyte_u32le(data + 6) == 0 ? 8u : 4u;
    } else if (memcmp(data, start_be, 6) == 0) {
        h.big_endian = 1;
        h.int_size = 4;
    } else if (memcmp(data, start_be64, 10) == 0) {
        h.big_endian = 1;
        h.int_size = 8;
    } else {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    h.major = 3;
    h.char_size = 1;
    h.float_size = 4;
    if (h.int_size == 8) {
        h.major = 2;
    } else {
        /* 32-bit tags: 3.x has three size tags, 2.2 one before 'PART'. */
        if (size < 14) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if ((h.big_endian ? xxbyte_u32be(data + 10) : xxbyte_u32le(data + 10)) == SQ_TAG_PART) {
            h.major = 2;
        }
    }
    sq_reader_init(&r, data, size, &h);
    r.pos = h.int_size == 8 ? 10u : 6u; /* after the 'SQIR' tag */
    if (h.major == 2) {
        status = sq_int(&r, &value);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (value != 1 && value != 2 && value != 4) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        h.char_size = (uint32_t)value;
        h.function_offset = r.pos;
        /* sizeof(SQFloat) is not recorded: the first that parses. When
         * neither does, a truncation under either wins (a cut file). */
        status = XXBYTE_STATUS_INVALID_INSTRUCTION;
        for (i = 0; i < 2; ++i) {
            xxbyte_status s;
            h.float_size = i == 0 ? 4u : 8u;
            s = sq_walk_file(&w, data, size, &h, &h.end_offset);
            if (s == XXBYTE_STATUS_OK || i == 0 || s == XXBYTE_STATUS_TRUNCATED) {
                status = s;
            }
            if (s == XXBYTE_STATUS_OK) {
                break;
            }
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        h.mode = XXBYTE_SQUIRREL_MODE(2, 2);
    } else {
        uint32_t sizes[3];
        if (size < 18) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        for (i = 0; i < 3; ++i) {
            sizes[i] = h.big_endian ? xxbyte_u32be(data + 6 + 4 * i) : xxbyte_u32le(data + 6 + 4 * i);
        }
        if ((sizes[0] != 1 && sizes[0] != 2 && sizes[0] != 4) || (sizes[1] != 4 && sizes[1] != 8) || (sizes[2] != 4 && sizes[2] != 8)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        h.char_size = sizes[0];
        h.int_size = sizes[1];
        h.float_size = sizes[2];
        h.function_offset = 18;
        status = sq_walk_file(&w, data, size, &h, &h.end_offset);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        h.mode = XXBYTE_SQUIRREL_MODE(3, (w.closure_seen && !w.closure_bound) ? 1 : 2);
    }
    if (h.big_endian) {
        h.mode |= XXBYTE_SQUIRREL_MODE_BIG_ENDIAN;
    }
    if (h.int_size == 8) {
        h.mode |= XXBYTE_SQUIRREL_MODE_INT64;
    }
    if (h.float_size == 8) {
        h.mode |= XXBYTE_SQUIRREL_MODE_DOUBLE;
    }
    *header = h;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_squirrel_read_object(const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, uint64_t offset, xxbyte_squirrel_object *object)
{
    sq_reader r;
    xxbyte_status status;

    if (object != NULL) {
        memset(object, 0, sizeof(*object));
    }
    if (object == NULL || (data == NULL && size != 0) || !sq_header_ok(header)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    sq_reader_init(&r, data, size, header);
    if (offset > (uint64_t)r.size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    r.pos = (size_t)offset;
    status = sq_object(&r, object);
    if (status != XXBYTE_STATUS_OK) {
        memset(object, 0, sizeof(*object));
    }
    return status;
}

xxbyte_status xxbyte_squirrel_find_functions(const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, xxbyte_squirrel_function *functions, size_t capacity,
                                             size_t *count)
{
    sq_walk w;
    xxbyte_status status;

    if (count == NULL || (data == NULL && size != 0) || !sq_header_ok(header) || (functions == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    memset(&w, 0, sizeof(w));
    w.functions = functions;
    w.capacity = capacity;
    status = sq_walk_file(&w, data, size, header, NULL);
    if (status == XXBYTE_STATUS_OK) {
        *count = w.count;
    }
    return status;
}

/* Positions a reader at offset of *function's record. */
static xxbyte_status sq_reader_at(sq_reader *r, const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, uint64_t offset)
{
    if ((data == NULL && size != 0) || !sq_header_ok(header)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    sq_reader_init(r, data, size, header);
    if (offset > (uint64_t)r->size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    r->pos = (size_t)offset;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_squirrel_get_objects(const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, const xxbyte_squirrel_function *function, uint32_t list,
                                          uint32_t first, xxbyte_squirrel_object *objects, size_t count)
{
    sq_reader r;
    xxbyte_status status;
    uint32_t total;
    size_t i;

    if (function == NULL || list > 1 || (objects == NULL && count != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    total = list == 0 ? function->literal_count : function->parameter_count;
    if (first > total || count > (size_t)(total - first)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = sq_reader_at(&r, data, size, header, list == 0 ? function->literals_offset : function->parameters_offset);
    for (i = 0; status == XXBYTE_STATUS_OK && i < (size_t)first + count; ++i) {
        status = sq_object(&r, i >= first ? &objects[i - first] : NULL);
    }
    if (status != XXBYTE_STATUS_OK && count != 0) {
        memset(objects, 0, count * sizeof(*objects));
    }
    return status;
}

static xxbyte_status sq_uint(sq_reader *r, uint64_t *value)
{
    int64_t v = 0;
    xxbyte_status status = sq_int(r, &v);
    *value = r->int_size == 4 ? (uint64_t)(uint32_t)v : (uint64_t)v;
    return status;
}

xxbyte_status xxbyte_squirrel_get_locals(const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, const xxbyte_squirrel_function *function,
                                         xxbyte_squirrel_local *locals)
{
    sq_reader r;
    xxbyte_status status;
    uint32_t i;

    if (function == NULL || (locals == NULL && function->local_count != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = sq_reader_at(&r, data, size, header, function->locals_offset);
    for (i = 0; status == XXBYTE_STATUS_OK && i < function->local_count; ++i) {
        xxbyte_squirrel_local *l = &locals[i];
        memset(l, 0, sizeof(*l));
        status = sq_object(&r, &l->name);
        if (status == XXBYTE_STATUS_OK) {
            status = sq_uint(&r, &l->pos);
        }
        if (status == XXBYTE_STATUS_OK) {
            status = sq_uint(&r, &l->start_op);
        }
        if (status == XXBYTE_STATUS_OK) {
            status = sq_uint(&r, &l->end_op);
        }
    }
    if (status != XXBYTE_STATUS_OK && function->local_count != 0) {
        memset(locals, 0, function->local_count * sizeof(*locals));
    }
    return status;
}

xxbyte_status xxbyte_squirrel_get_outers(const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, const xxbyte_squirrel_function *function,
                                         xxbyte_squirrel_outer *outers)
{
    sq_reader r;
    xxbyte_status status;
    uint32_t i;

    if (function == NULL || (outers == NULL && function->outer_count != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = sq_reader_at(&r, data, size, header, function->outers_offset);
    for (i = 0; status == XXBYTE_STATUS_OK && i < function->outer_count; ++i) {
        xxbyte_squirrel_outer *o = &outers[i];
        memset(o, 0, sizeof(*o));
        status = sq_uint(&r, &o->type);
        if (status == XXBYTE_STATUS_OK) {
            status = sq_object(&r, &o->source);
        }
        if (status == XXBYTE_STATUS_OK) {
            status = sq_object(&r, &o->name);
        }
    }
    if (status != XXBYTE_STATUS_OK && function->outer_count != 0) {
        memset(outers, 0, function->outer_count * sizeof(*outers));
    }
    return status;
}

/* ---- Text ---- */

/* A bounded text sink over a char buffer. */
typedef struct sq_text {
    char *p;
    size_t cap; /* including the NUL */
    size_t len;
    int full;
} sq_text;

static void sq_text_str(sq_text *t, const char *s)
{
    while (*s != '\0') {
        if (t->len + 1 >= t->cap) {
            t->full = 1;
            return;
        }
        t->p[t->len++] = *s++;
    }
    t->p[t->len] = '\0';
}

static void sq_text_hex(sq_text *t, const char *prefix, uint32_t value, int digits)
{
    static const char hex[] = "0123456789ABCDEF";
    char tmp[16];
    size_t n = strlen(prefix);
    int i;

    memcpy(tmp, prefix, n);
    for (i = digits - 1; i >= 0; --i) {
        tmp[n++] = hex[(value >> (4 * i)) & 0xFu];
    }
    tmp[n] = '\0';
    sq_text_str(t, tmp);
}

/* Appends the characters of a string object, escaped; quoted adds the
 * quotes and escapes them. Stops at limit characters with "...". */
static void sq_text_string(sq_text *t, const uint8_t *data, const xxbyte_squirrel_header *header, const xxbyte_squirrel_object *o, int quoted, size_t limit)
{
    uint64_t i;
    const uint8_t *p = data + o->string_offset;

    if (quoted) {
        sq_text_str(t, "\"");
    }
    for (i = 0; i < o->string_length && !t->full; ++i) {
        uint32_t c;
        char one[2];
        if (i >= limit) {
            sq_text_str(t, "...");
            break;
        }
        switch (header->char_size) {
            case 1: c = p[i]; break;
            case 2: c = header->big_endian ? xxbyte_u16be(p + 2 * i) : xxbyte_u16le(p + 2 * i); break;
            default: c = header->big_endian ? xxbyte_u32be(p + 4 * i) : xxbyte_u32le(p + 4 * i); break;
        }
        switch (c) {
            case '\n': sq_text_str(t, "\\n"); continue;
            case '\r': sq_text_str(t, "\\r"); continue;
            case '\t': sq_text_str(t, "\\t"); continue;
            case '\\': sq_text_str(t, "\\\\"); continue;
            case '"': sq_text_str(t, quoted ? "\\\"" : "\""); continue;
            default: break;
        }
        if (c >= 0x20 && c < 0x7F) {
            one[0] = (char)c;
            one[1] = '\0';
            sq_text_str(t, one);
        } else if (header->char_size == 1 || c < 0x80) {
            sq_text_hex(t, "\\x", c, 2);
        } else if (c <= 0xFFFFu) {
            sq_text_hex(t, "\\u", c, 4);
        } else {
            sq_text_hex(t, "\\U", c, 8);
        }
    }
    if (quoted) {
        sq_text_str(t, "\"");
    }
}

#define SQ_LITERAL_LIMIT 200u

/* A literal as text: quoted string, number, true, false or null. */
static size_t sq_literal_text(const squirrel_names *names, const xxbyte_squirrel_object *o, char *buffer, size_t buffer_size)
{
    sq_text t;
    xx_buf_t tmp;
    xxbyte_fmt fmt;
    int ok;

    t.p = buffer;
    t.cap = buffer_size;
    t.len = 0;
    t.full = 0;
    buffer[0] = '\0';
    switch (o->type) {
        case XXBYTE_SQUIRREL_OBJECT_STRING: sq_text_string(&t, names->data, names->header, o, 1, SQ_LITERAL_LIMIT); break;
        case XXBYTE_SQUIRREL_OBJECT_NULL: sq_text_str(&t, "null"); break;
        case XXBYTE_SQUIRREL_OBJECT_BOOL: sq_text_str(&t, o->value != 0 ? "true" : "false"); break;
        case XXBYTE_SQUIRREL_OBJECT_INTEGER:
        case XXBYTE_SQUIRREL_OBJECT_FLOAT:
            xx_buf_init(&tmp);
            fmt.out = &tmp;
            fmt.flags = 0;
            fmt.resolver = NULL;
            fmt.context = NULL;
            if (o->type == XXBYTE_SQUIRREL_OBJECT_INTEGER) {
                ok = xxbyte_fmt_printf(&fmt, "%lld", (long long)o->value);
            } else if (o->size == 8) {
                double d;
                uint64_t bits = (uint64_t)o->value;
                memcpy(&d, &bits, sizeof(d));
                ok = xxbyte_fmt_double(&fmt, d);
            } else {
                float f;
                uint32_t bits = (uint32_t)o->value;
                memcpy(&f, &bits, sizeof(f));
                ok = xxbyte_fmt_float(&fmt, f);
            }
            if (ok && tmp.data != NULL) {
                sq_text_str(&t, tmp.data);
            }
            xx_buf_free(&tmp);
            break;
        default: break;
    }
    return t.full ? 0 : t.len;
}

/* A name object as bare text, or 0 when it is not a string. */
static size_t sq_name_text(const squirrel_names *names, const xxbyte_squirrel_object *o, char *buffer, size_t buffer_size)
{
    sq_text t;

    if (o->type != XXBYTE_SQUIRREL_OBJECT_STRING || o->string_length == 0) {
        return 0;
    }
    t.p = buffer;
    t.cap = buffer_size;
    t.len = 0;
    t.full = 0;
    buffer[0] = '\0';
    sq_text_string(&t, names->data, names->header, o, 0, SQ_LITERAL_LIMIT);
    return t.full ? 0 : t.len;
}

/* How many earlier locals of a register the resolver looks at. */
#define SQ_LOCAL_STEPS 32u

size_t xxbyte_squirrel_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const squirrel_names *names = (const squirrel_names *)context;
    const xxbyte_squirrel_function *f;
    const xxbyte_operand *operand;
    uint64_t value;

    if (names == NULL || instruction == NULL || operand_index >= instruction->operand_count || buffer == NULL || buffer_size < 2) {
        return 0;
    }
    f = names->function;
    operand = &instruction->operands[operand_index];
    value = (uint64_t)operand->value;
    if (operand->type == XXBYTE_OPERAND_REGISTER) {
        uint64_t pc = instruction->address / 8u;
        size_t lo = 0;
        size_t hi = f->local_count;
        size_t steps;
        if (names->locals == NULL) {
            return 0;
        }
        /* locals is sorted by (pos, start_op): find the last local of the
         * register that starts at or before pc, then the innermost one of
         * those that is still live (a few steps back at most). */
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2u;
            const xxbyte_squirrel_local *l = &names->locals[mid];
            if (l->pos < value || (l->pos == value && l->start_op <= pc)) {
                lo = mid + 1u;
            } else {
                hi = mid;
            }
        }
        for (steps = 0; lo > 0 && steps < SQ_LOCAL_STEPS; --lo, ++steps) {
            const xxbyte_squirrel_local *l = &names->locals[lo - 1u];
            if (l->pos != value) {
                break;
            }
            if (l->end_op >= pc) {
                return sq_name_text(names, &l->name, buffer, buffer_size);
            }
        }
        return 0;
    }
    if (operand->type != XXBYTE_OPERAND_INDEX) {
        return 0;
    }
    switch (operand->kind) {
        case XXBYTE_INDEX_CONSTANT:
            if (names->literals != NULL && value < f->literal_count) {
                return sq_literal_text(names, &names->literals[value], buffer, buffer_size);
            }
            return 0;
        case XXBYTE_INDEX_UPVALUE:
            if (names->outers != NULL && value < f->outer_count) {
                return sq_name_text(names, &names->outers[value].name, buffer, buffer_size);
            }
            return 0;
        case XXBYTE_INDEX_FUNCTION:
            if (names->children != NULL && value < f->function_count && names->children[value] != 0) {
                xxbyte_squirrel_object o;
                if (xxbyte_squirrel_read_object(names->data, names->size, names->header, names->children[value], &o) == XXBYTE_STATUS_OK) {
                    return sq_name_text(names, &o, buffer, buffer_size);
                }
            }
            return 0;
        default: return 0;
    }
}

/* ---- Listing ---- */

typedef struct sq_lister {
    const uint8_t *data;
    size_t size;
    xxbyte_squirrel_header header;
    uint32_t mode;
    uint32_t listing_flags;
    xx_buf_t *out;
    xxbyte_squirrel_function *functions;
    size_t count;
    xxbyte_squirrel_object *objects; /* literals, then parameters */
    xxbyte_squirrel_local *locals;
    xxbyte_squirrel_outer *outers;
    uint64_t *children;
} sq_lister;

static const char *sq_plural(uint64_t n)
{
    return n == 1 ? "" : "s";
}

/* Appends a name object, or "?". */
static int sq_put_name(const sq_lister *ls, const squirrel_names *names, uint64_t offset)
{
    xxbyte_squirrel_object o;
    char text[512];

    if (xxbyte_squirrel_read_object(ls->data, ls->size, &ls->header, offset, &o) == XXBYTE_STATUS_OK && sq_name_text(names, &o, text, sizeof(text)) != 0) {
        return xx_buf_append_str(ls->out, text);
    }
    return xx_buf_append_str(ls->out, "?");
}

/* Orders locals by register, then by first pc, then by position in the
 * file (so the sort is deterministic). */
static int sq_local_compare(const void *a, const void *b)
{
    const xxbyte_squirrel_local *x = (const xxbyte_squirrel_local *)a;
    const xxbyte_squirrel_local *y = (const xxbyte_squirrel_local *)b;

    if (x->pos != y->pos) {
        return x->pos < y->pos ? -1 : 1;
    }
    if (x->start_op != y->start_op) {
        return x->start_op < y->start_op ? -1 : 1;
    }
    if (x->name.offset != y->name.offset) {
        return x->name.offset < y->name.offset ? -1 : 1;
    }
    return 0;
}

static xxbyte_status sq_list_function(sq_lister *ls, size_t index)
{
    const xxbyte_squirrel_function *f = &ls->functions[index];
    squirrel_names names;
    xxbyte_status status;
    xx_buf_t *out = ls->out;
    size_t j;
    uint32_t i;

    status = xxbyte_squirrel_get_objects(ls->data, ls->size, &ls->header, f, 0, 0, ls->objects, f->literal_count);
    if (status == XXBYTE_STATUS_OK) {
        status = xxbyte_squirrel_get_objects(ls->data, ls->size, &ls->header, f, 1, 0, ls->objects + f->literal_count, f->parameter_count);
    }
    if (status == XXBYTE_STATUS_OK) {
        status = xxbyte_squirrel_get_locals(ls->data, ls->size, &ls->header, f, ls->locals);
    }
    if (status == XXBYTE_STATUS_OK && f->local_count > 1) {
        qsort(ls->locals, f->local_count, sizeof(*ls->locals), sq_local_compare);
    }
    if (status == XXBYTE_STATUS_OK) {
        status = xxbyte_squirrel_get_outers(ls->data, ls->size, &ls->header, f, ls->outers);
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < f->function_count; ++i) {
        ls->children[i] = 0;
    }
    for (j = index + 1u; j < ls->count && ls->functions[j].depth > f->depth; ++j) {
        if (ls->functions[j].parent == index && ls->functions[j].child < f->function_count) {
            ls->children[ls->functions[j].child] = ls->functions[j].name_offset;
        }
    }
    memset(&names, 0, sizeof(names));
    names.data = ls->data;
    names.size = ls->size;
    names.header = &ls->header;
    names.function = f;
    names.literals = ls->objects;
    names.locals = ls->locals;
    names.outers = ls->outers;
    names.children = ls->children;

    if (index != 0) {
        xx_buf_append_char(out, '\n');
    }
    xx_buf_append_str(out, "function ");
    sq_put_name(ls, &names, f->name_offset);
    xx_buf_append_str(out, " <");
    sq_put_name(ls, &names, f->source_offset);
    xx_buf_appendf(out, "> (%u instruction%s at 0x%llX)\n", f->instruction_count, sq_plural(f->instruction_count), (unsigned long long)f->offset);
    xx_buf_appendf(out, "%u%s param%s", f->parameter_count, f->varparams ? "+" : "", sq_plural(f->parameter_count));
    if (f->parameter_count != 0) {
        xx_buf_append_str(out, " (");
        for (i = 0; i < f->parameter_count; ++i) {
            char text[512];
            if (i != 0) {
                xx_buf_append_str(out, ", ");
            }
            xx_buf_append_str(out, sq_name_text(&names, &ls->objects[f->literal_count + i], text, sizeof(text)) != 0 ? text : "?");
        }
        xx_buf_append_char(out, ')');
    }
    xx_buf_appendf(out, ", %u literal%s, %u local%s, %u outer%s, %u default%s, %u function%s, stack %lld%s\n", f->literal_count, sq_plural(f->literal_count),
                   f->local_count, sq_plural(f->local_count), f->outer_count, sq_plural(f->outer_count), f->default_count, sq_plural(f->default_count), f->function_count,
                   sq_plural(f->function_count), (long long)f->stack_size, f->generator ? ", generator" : "");
    if (!xx_buf_ok(out)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return xxbyte_disasm_buf_ex(XXBYTE_FAMILY_SQUIRREL, ls->mode, NULL, ls->data + f->instructions_offset, (size_t)f->instruction_count * 8u, 0, ls->listing_flags,
                                xxbyte_squirrel_resolve, &names, out);
}

xxbyte_status xxbyte_squirrel_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    sq_lister ls;
    xxbyte_status status;
    size_t max_objects = 0;
    size_t max_locals = 0;
    size_t max_outers = 0;
    size_t max_children = 0;
    size_t i;

    memset(&ls, 0, sizeof(ls));
    ls.data = data;
    ls.size = data != NULL ? size : 0;
    ls.out = out;
    ls.listing_flags = listing_flags;
    status = xxbyte_squirrel_parse_header(data, ls.size, &ls.header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (mode == XXBYTE_MODE_AUTO) {
        ls.mode = ls.header.mode;
    } else if (XXBYTE_SQUIRREL_MODE_MAJOR(mode) != ls.header.major) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    } else {
        ls.mode = mode;
    }
    status = xxbyte_squirrel_find_functions(data, ls.size, &ls.header, NULL, 0, &ls.count);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    ls.functions = (xxbyte_squirrel_function *)xx_mem_calloc(ls.count, sizeof(*ls.functions));
    if (ls.functions == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_squirrel_find_functions(data, ls.size, &ls.header, ls.functions, ls.count, &ls.count);
    if (status != XXBYTE_STATUS_OK) {
        goto done;
    }
    for (i = 0; i < ls.count; ++i) {
        const xxbyte_squirrel_function *f = &ls.functions[i];
        size_t n = (size_t)f->literal_count + f->parameter_count;
        if (n > max_objects) {
            max_objects = n;
        }
        if (f->local_count > max_locals) {
            max_locals = f->local_count;
        }
        if (f->outer_count > max_outers) {
            max_outers = f->outer_count;
        }
        if (f->function_count > max_children) {
            max_children = f->function_count;
        }
    }
    /* Every count is bounded by the records read, so by size. */
    ls.objects = (xxbyte_squirrel_object *)xx_mem_calloc(max_objects + 1u, sizeof(*ls.objects));
    ls.locals = (xxbyte_squirrel_local *)xx_mem_calloc(max_locals + 1u, sizeof(*ls.locals));
    ls.outers = (xxbyte_squirrel_outer *)xx_mem_calloc(max_outers + 1u, sizeof(*ls.outers));
    ls.children = (uint64_t *)xx_mem_calloc(max_children + 1u, sizeof(*ls.children));
    if (ls.objects == NULL || ls.locals == NULL || ls.outers == NULL || ls.children == NULL) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    for (i = 0; i < ls.count && status == XXBYTE_STATUS_OK; ++i) {
        status = sq_list_function(&ls, i);
    }
done:
    xx_mem_free(ls.children);
    xx_mem_free(ls.outers);
    xx_mem_free(ls.locals);
    xx_mem_free(ls.objects);
    xx_mem_free(ls.functions);
    return status;
}

#else
typedef int xxbyte_squirrel_file_disabled;
#endif
