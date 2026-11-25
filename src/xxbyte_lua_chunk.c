#include "xxbyte_internal.h"

#if XXBYTE_USE_LUA

/* Precompiled-chunk headers and prototypes (lundump.c of each series). */

/* Constant tags written by ldump.c. */
#define LUA_TAG_NIL 0u
#define LUA_TAG_BOOLEAN 1u /* 5.1 to 5.3: followed by a byte */
#define LUA_TAG_NUMBER 3u  /* lua_Number to 5.3 (float), 5.4+ integer */
#define LUA_TAG_STRING 4u
#define LUA_TAG_TRUE 17u     /* 5.4+ */
#define LUA_TAG_NUMBER19 19u /* 5.3 integer, 5.4+ float */
#define LUA_TAG_LONGSTR 20u  /* 5.3+ long string */

#define LUA_INT_MAX UINT64_C(0x7FFFFFFF)

/* xxbyte_lua_function.index limit; reaching it is malformed. */
#define LUA_MAX_FUNCTIONS UINT32_C(0xFFFFFFFE)

static const uint8_t lua_signature[4] = {0x1B, 'L', 'u', 'a'};
static const uint8_t lua_tail[6] = {0x19, 0x93, '\r', '\n', 0x1A, '\n'};

typedef struct lua_reader {
    const uint8_t *data;
    size_t size;
    uint64_t pos;
    const xxbyte_lua_header *header;
    uint32_t minor;
    xxbyte_lua_function *functions;
    size_t capacity;
    uint32_t next_index;
    uint64_t saved; /* 5.5: strings saved so far */
    uint64_t want;  /* 5.5: saved string to look for, 0 for none */
    uint64_t found_offset;
    uint64_t found_length;
    int any_reference;            /* 5.5: accept references without checking
                                     them against saved */
    xxbyte_lua_string *saved_out; /* 5.5: saved strings collected */
    size_t saved_capacity;
} lua_reader;

/* A string read from the chunk; 5.5 may refer to a saved one instead. */
typedef struct lua_string {
    int present;
    uint64_t offset;
    uint64_t length;
    uint64_t reference; /* 5.5: ordinal of the saved string, else 0 */
} lua_string;

/* Internal: lua_read_string found the wanted 5.5 string; the walk stops. */
#define LUA_FOUND XXBYTE_STATUS_END_OF_INPUT

static xxbyte_status lua_need(const lua_reader *r, uint64_t count)
{
    return count > (uint64_t)r->size - r->pos ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OK;
}

static xxbyte_status lua_skip(lua_reader *r, uint64_t count)
{
    xxbyte_status status = lua_need(r, count);
    if (status == XXBYTE_STATUS_OK) {
        r->pos += count;
    }
    return status;
}

static xxbyte_status lua_read_byte(lua_reader *r, uint32_t *value)
{
    if (r->pos >= (uint64_t)r->size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *value = r->data[r->pos++];
    return XXBYTE_STATUS_OK;
}

/* An unsigned integer of width bytes (1 to 8) in the given byte order. */
static uint64_t lua_load(const uint8_t *p, uint32_t width, int big)
{
    uint64_t value = 0;
    uint32_t i;

    for (i = 0; i < width; ++i) {
        uint32_t index = big ? i : width - 1u - i;
        value = (value << 8) | p[index];
    }
    return value;
}

static xxbyte_status lua_read_fixed(lua_reader *r, uint32_t width, uint64_t *value)
{
    xxbyte_status status = lua_need(r, width);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    *value = lua_load(r->data + r->pos, width, r->header->big_endian != 0);
    r->pos += width;
    return XXBYTE_STATUS_OK;
}

/* 5.4 loadUnsigned: 7-bit groups, most significant first, 0x80 marks the
 * last byte. */
static xxbyte_status lua_read_varint54(lua_reader *r, uint64_t limit, uint64_t *value)
{
    uint64_t x = 0;
    uint32_t b;

    limit >>= 7;
    do {
        xxbyte_status status = lua_read_byte(r, &b);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (x >= limit) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        x = (x << 7) | (b & 0x7Fu);
    } while ((b & 0x80u) == 0);
    *value = x;
    return XXBYTE_STATUS_OK;
}

/* 5.5 loadVarint: 0x80 marks every byte but the last. */
static xxbyte_status lua_read_varint55(lua_reader *r, uint64_t limit, uint64_t *value)
{
    uint64_t x = 0;
    uint32_t b;

    limit >>= 7;
    do {
        xxbyte_status status = lua_read_byte(r, &b);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (x > limit) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        x = (x << 7) | (b & 0x7Fu);
    } while ((b & 0x80u) != 0);
    *value = x;
    return XXBYTE_STATUS_OK;
}

/* A non-negative C int (counts, line numbers, pcs). */
static xxbyte_status lua_read_int(lua_reader *r, uint64_t *value)
{
    xxbyte_status status;
    uint32_t width = r->header->int_size;

    if (r->minor == 4) {
        return lua_read_varint54(r, LUA_INT_MAX, value);
    }
    if (r->minor == 5) {
        return lua_read_varint55(r, LUA_INT_MAX, value);
    }
    status = lua_read_fixed(r, width, value);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    /* Negative (sign bit set) or wider than a 32-bit int. */
    if ((*value >> (8u * width - 1u)) != 0 || *value > LUA_INT_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return XXBYTE_STATUS_OK;
}

static void lua_string_init(lua_string *s)
{
    memset(s, 0, sizeof(*s));
}

static xxbyte_status lua_take_string(lua_reader *r, uint64_t length, uint64_t stored, lua_string *s)
{
    xxbyte_status status = lua_need(r, stored);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    s->present = 1;
    s->offset = r->pos;
    s->length = length;
    r->pos += stored;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status lua_read_string(lua_reader *r, lua_string *s)
{
    xxbyte_status status;
    uint64_t size = 0;
    uint32_t b;

    lua_string_init(s);
    switch (r->minor) {
        case 0:
        case 1:
        case 2:
            /* size_t size including the NUL, 0 for none. */
            status = lua_read_fixed(r, r->header->size_t_size, &size);
            if (status != XXBYTE_STATUS_OK || size == 0) {
                return status;
            }
            return lua_take_string(r, size - 1u, size, s);
        case 3:
            /* A byte size + 1, or 0xFF and a size_t size + 1; no NUL. */
            status = lua_read_byte(r, &b);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            size = b;
            if (b == 0xFFu) {
                status = lua_read_fixed(r, r->header->size_t_size, &size);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
            }
            if (size == 0) {
                return XXBYTE_STATUS_OK;
            }
            return lua_take_string(r, size - 1u, size - 1u, s);
        case 4:
            status = lua_read_varint54(r, UINT64_MAX, &size);
            if (status != XXBYTE_STATUS_OK || size == 0) {
                return status;
            }
            return lua_take_string(r, size - 1u, size - 1u, s);
        default:
            /* 5.5: size + 1 and the bytes with their NUL, or 0 and the
             * ordinal of a saved string (0 for none). */
            status = lua_read_varint55(r, UINT64_C(0x7FFFFFFFFFFFFFFF), &size);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (size == 0) {
                uint64_t ordinal;
                status = lua_read_varint55(r, UINT64_MAX, &ordinal);
                if (status != XXBYTE_STATUS_OK || ordinal == 0) {
                    return status;
                }
                if (!r->any_reference && ordinal > r->saved) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                s->present = 1;
                s->reference = ordinal;
                return XXBYTE_STATUS_OK;
            }
            status = lua_take_string(r, size - 1u, size, s);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (r->saved < (uint64_t)r->saved_capacity) {
                r->saved_out[r->saved].offset = s->offset;
                r->saved_out[r->saved].length = s->length;
            }
            ++r->saved;
            if (r->want != 0 && r->saved == r->want) {
                r->found_offset = s->offset;
                r->found_length = s->length;
                return LUA_FOUND;
            }
            return XXBYTE_STATUS_OK;
    }
}

static xxbyte_status lua_skip_string(lua_reader *r)
{
    lua_string s;
    return lua_read_string(r, &s);
}

/* One constant record; *constant may be NULL. */
static xxbyte_status lua_read_constant(lua_reader *r, xxbyte_lua_constant *constant, lua_string *string)
{
    const xxbyte_lua_header *h = r->header;
    xxbyte_lua_constant k;
    xxbyte_status status;
    uint64_t bits = 0;
    uint32_t tag;
    uint32_t number_tag;
    uint32_t integer_tag = 0;

    memset(&k, 0, sizeof(k));
    lua_string_init(string);
    status = lua_read_byte(r, &tag);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    number_tag = r->minor >= 4 ? LUA_TAG_NUMBER19 : LUA_TAG_NUMBER;
    if (r->minor == 3) {
        integer_tag = LUA_TAG_NUMBER19;
    } else if (r->minor >= 4) {
        integer_tag = LUA_TAG_NUMBER;
    }
    if (tag == LUA_TAG_NIL) {
        k.type = XXBYTE_LUA_CONSTANT_NIL;
    } else if (tag == LUA_TAG_BOOLEAN && r->minor >= 1) {
        k.type = XXBYTE_LUA_CONSTANT_BOOLEAN;
        if (r->minor <= 3) {
            uint32_t b;
            status = lua_read_byte(r, &b);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            k.value = b != 0;
        }
    } else if (tag == LUA_TAG_TRUE && r->minor >= 4) {
        k.type = XXBYTE_LUA_CONSTANT_BOOLEAN;
        k.value = 1;
    } else if (tag == number_tag) {
        status = lua_read_fixed(r, h->number_size, &bits);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        k.type = XXBYTE_LUA_CONSTANT_NUMBER;
        k.size = h->number_size;
        k.value = h->number_integral != 0 ? xxbyte_sext(bits, 8u * h->number_size) : (int64_t)bits;
    } else if (integer_tag != 0 && tag == integer_tag) {
        k.type = XXBYTE_LUA_CONSTANT_INTEGER;
        k.size = h->integer_size;
        if (r->minor == 5) {
            /* Zigzag: 2x for x >= 0, -2x - 1 for x < 0. */
            status = lua_read_varint55(r, UINT64_MAX, &bits);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            k.value = (bits & 1u) != 0 ? (int64_t)~(bits >> 1) : (int64_t)(bits >> 1);
        } else {
            status = lua_read_fixed(r, h->integer_size, &bits);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            k.value = xxbyte_sext(bits, 8u * h->integer_size);
        }
    } else if (tag == LUA_TAG_STRING || (tag == LUA_TAG_LONGSTR && r->minor >= 3)) {
        k.type = XXBYTE_LUA_CONSTANT_STRING;
        status = lua_read_string(r, string);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (!string->present) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        k.string_offset = string->offset;
        k.string_length = string->length;
    } else {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (constant != NULL) {
        *constant = k;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status lua_read_constants(lua_reader *r, xxbyte_lua_function *f)
{
    uint64_t n;
    uint64_t i;
    xxbyte_status status = lua_read_int(r, &n);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    f->constant_count = (uint32_t)n;
    f->constants_offset = r->pos;
    for (i = 0; i < n; ++i) {
        lua_string s;
        status = lua_read_constant(r, NULL, &s);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* A counted list of strings (upvalue names). */
static xxbyte_status lua_read_names(lua_reader *r, uint64_t n, xxbyte_lua_function *f)
{
    uint64_t i;

    f->upvalue_name_count = (uint32_t)n;
    f->upvalue_names_offset = r->pos;
    for (i = 0; i < n; ++i) {
        xxbyte_status status = lua_skip_string(r);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* lineinfo, locvars and upvalue names (5.0 keeps its names elsewhere). */
static xxbyte_status lua_read_debug(lua_reader *r, xxbyte_lua_function *f)
{
    xxbyte_status status;
    uint64_t n;
    uint64_t i;
    uint64_t width = r->minor >= 4 ? 1u : r->header->int_size;

    status = lua_read_int(r, &n);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    f->lineinfo_count = (uint32_t)n;
    f->lineinfo_offset = r->pos;
    if ((status = lua_skip(r, n * width)) != XXBYTE_STATUS_OK) {
        return status;
    }
    if (r->minor >= 4) {
        /* abslineinfo: two varints each (5.4), or 4-aligned pairs of
         * ints (5.5). */
        status = lua_read_int(r, &n);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        f->abslineinfo_count = (uint32_t)n;
        f->abslineinfo_offset = r->pos;
        if (r->minor == 4) {
            for (i = 0; i < 2u * n; ++i) {
                uint64_t v;
                status = lua_read_int(r, &v);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
            }
        } else if (n > 0) {
            uint64_t align = r->header->int_size;
            uint64_t pad = (align - r->pos % align) % align;
            status = lua_skip(r, pad);
            f->abslineinfo_offset = r->pos;
            if (status != XXBYTE_STATUS_OK || (status = lua_skip(r, n * 2u * align)) != XXBYTE_STATUS_OK) {
                return status;
            }
        }
    }
    status = lua_read_int(r, &n);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    f->local_count = (uint32_t)n;
    f->locals_offset = r->pos;
    for (i = 0; i < n; ++i) {
        uint64_t v;
        if ((status = lua_skip_string(r)) != XXBYTE_STATUS_OK || (status = lua_read_int(r, &v)) != XXBYTE_STATUS_OK ||
            (status = lua_read_int(r, &v)) != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    status = lua_read_int(r, &n);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (r->minor >= 4 && n != 0) {
        n = f->upvalue_count; /* names for all upvalues or none */
    }
    return lua_read_names(r, n, f);
}

static xxbyte_status lua_read_code(lua_reader *r, xxbyte_lua_function *f)
{
    uint64_t n;
    xxbyte_status status = lua_read_int(r, &n);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (r->minor == 5) {
        uint64_t align = r->header->instruction_size;
        status = lua_skip(r, (align - r->pos % align) % align);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    f->code_count = (uint32_t)n;
    f->code_offset = r->pos;
    return lua_skip(r, n * r->header->instruction_size);
}

static xxbyte_status lua_read_function(lua_reader *r, uint32_t depth, uint32_t parent, uint32_t child);

static xxbyte_status lua_read_prototypes(lua_reader *r, xxbyte_lua_function *f, uint32_t depth)
{
    uint64_t n;
    uint64_t i;
    xxbyte_status status = lua_read_int(r, &n);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    f->prototype_count = (uint32_t)n;
    for (i = 0; i < n; ++i) {
        status = lua_read_function(r, depth + 1u, f->index, (uint32_t)i);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* 5.2+ upvalue descriptors: instack, idx (and kind from 5.4). */
static xxbyte_status lua_read_upvalues(lua_reader *r, xxbyte_lua_function *f)
{
    uint64_t n;
    xxbyte_status status = lua_read_int(r, &n);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    f->upvalue_count = (uint32_t)n;
    f->upvalues_offset = r->pos;
    return lua_skip(r, n * (r->minor >= 4 ? 3u : 2u));
}

static xxbyte_status lua_read_header_fields(lua_reader *r, xxbyte_lua_function *f)
{
    xxbyte_status status;
    uint64_t v;
    uint32_t b[4];
    uint32_t count = r->minor <= 1 ? 4u : 3u;
    uint32_t i;

    status = lua_read_int(r, &v);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    f->line_defined = (uint32_t)v;
    if (r->minor >= 1) {
        status = lua_read_int(r, &v);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        f->last_line_defined = (uint32_t)v;
    }
    /* 5.0/5.1: nups, numparams, is_vararg, maxstacksize; 5.2+ without
     * nups. */
    for (i = 0; i < count; ++i) {
        status = lua_read_byte(r, &b[i]);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    i = 0;
    if (count == 4) {
        f->upvalue_count = b[i++];
    }
    f->num_params = (uint8_t)b[i];
    f->is_vararg = (uint8_t)b[i + 1];
    f->max_stack_size = (uint8_t)b[i + 2];
    return XXBYTE_STATUS_OK;
}

static xxbyte_status lua_read_body(lua_reader *r, xxbyte_lua_function *f, uint32_t depth)
{
    xxbyte_status status;
    uint64_t n;
    uint64_t i;

    switch (r->minor) {
        case 0:
            /* source, line, nups..., lines, locals, upvalue names,
             * constants, prototypes, code */
            f->source_offset = r->pos;
            if ((status = lua_skip_string(r)) != XXBYTE_STATUS_OK || (status = lua_read_header_fields(r, f)) != XXBYTE_STATUS_OK ||
                (status = lua_read_int(r, &n)) != XXBYTE_STATUS_OK) {
                return status;
            }
            f->lineinfo_count = (uint32_t)n;
            f->lineinfo_offset = r->pos;
            if ((status = lua_skip(r, n * r->header->int_size)) != XXBYTE_STATUS_OK || (status = lua_read_int(r, &n)) != XXBYTE_STATUS_OK) {
                return status;
            }
            f->local_count = (uint32_t)n;
            f->locals_offset = r->pos;
            for (i = 0; i < n; ++i) {
                uint64_t v;
                if ((status = lua_skip_string(r)) != XXBYTE_STATUS_OK || (status = lua_read_int(r, &v)) != XXBYTE_STATUS_OK ||
                    (status = lua_read_int(r, &v)) != XXBYTE_STATUS_OK) {
                    return status;
                }
            }
            if ((status = lua_read_int(r, &n)) != XXBYTE_STATUS_OK) {
                return status;
            }
            if (n != 0 && n != f->upvalue_count) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if ((status = lua_read_names(r, n, f)) != XXBYTE_STATUS_OK || (status = lua_read_constants(r, f)) != XXBYTE_STATUS_OK ||
                (status = lua_read_prototypes(r, f, depth)) != XXBYTE_STATUS_OK) {
                return status;
            }
            return lua_read_code(r, f);
        case 1:
            /* source, lines, nups..., code, constants, prototypes, debug */
            f->source_offset = r->pos;
            if ((status = lua_skip_string(r)) != XXBYTE_STATUS_OK || (status = lua_read_header_fields(r, f)) != XXBYTE_STATUS_OK ||
                (status = lua_read_code(r, f)) != XXBYTE_STATUS_OK || (status = lua_read_constants(r, f)) != XXBYTE_STATUS_OK ||
                (status = lua_read_prototypes(r, f, depth)) != XXBYTE_STATUS_OK) {
                return status;
            }
            return lua_read_debug(r, f);
        case 2:
            /* lines, params..., code, constants, prototypes, upvalues,
             * source and debug */
            if ((status = lua_read_header_fields(r, f)) != XXBYTE_STATUS_OK || (status = lua_read_code(r, f)) != XXBYTE_STATUS_OK ||
                (status = lua_read_constants(r, f)) != XXBYTE_STATUS_OK || (status = lua_read_prototypes(r, f, depth)) != XXBYTE_STATUS_OK ||
                (status = lua_read_upvalues(r, f)) != XXBYTE_STATUS_OK) {
                return status;
            }
            f->source_offset = r->pos;
            if ((status = lua_skip_string(r)) != XXBYTE_STATUS_OK) {
                return status;
            }
            return lua_read_debug(r, f);
        case 3:
        case 4:
            /* source, lines, params..., code, constants, upvalues,
             * prototypes, debug */
            f->source_offset = r->pos;
            if ((status = lua_skip_string(r)) != XXBYTE_STATUS_OK || (status = lua_read_header_fields(r, f)) != XXBYTE_STATUS_OK ||
                (status = lua_read_code(r, f)) != XXBYTE_STATUS_OK || (status = lua_read_constants(r, f)) != XXBYTE_STATUS_OK ||
                (status = lua_read_upvalues(r, f)) != XXBYTE_STATUS_OK || (status = lua_read_prototypes(r, f, depth)) != XXBYTE_STATUS_OK) {
                return status;
            }
            return lua_read_debug(r, f);
        default:
            /* lines, params..., code, constants, upvalues, prototypes,
             * source, debug */
            if ((status = lua_read_header_fields(r, f)) != XXBYTE_STATUS_OK || (status = lua_read_code(r, f)) != XXBYTE_STATUS_OK ||
                (status = lua_read_constants(r, f)) != XXBYTE_STATUS_OK || (status = lua_read_upvalues(r, f)) != XXBYTE_STATUS_OK ||
                (status = lua_read_prototypes(r, f, depth)) != XXBYTE_STATUS_OK) {
                return status;
            }
            f->source_offset = r->pos;
            if ((status = lua_skip_string(r)) != XXBYTE_STATUS_OK) {
                return status;
            }
            return lua_read_debug(r, f);
    }
}

static xxbyte_status lua_read_function(lua_reader *r, uint32_t depth, uint32_t parent, uint32_t child)
{
    xxbyte_lua_function f;
    xxbyte_status status;

    if (depth > XXBYTE_LUA_MAX_DEPTH || r->next_index >= LUA_MAX_FUNCTIONS) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    memset(&f, 0, sizeof(f));
    f.index = r->next_index++;
    f.parent = parent;
    f.child = child;
    f.depth = depth;
    f.offset = r->pos;
    status = lua_read_body(r, &f, depth);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    f.end_offset = r->pos;
    if (f.index < r->capacity) {
        r->functions[f.index] = f;
    }
    return XXBYTE_STATUS_OK;
}

static int lua_valid_size(uint32_t size, int allow_two)
{
    return size == 4 || size == 8 || (allow_two && size == 2);
}

/* Reads the two's-complement test value expected in width bytes and
 * tells the byte order: 0 little, 1 big, -1 neither. */
static int lua_byte_order(const uint8_t *p, uint32_t width, int64_t expected)
{
    uint64_t mask = width >= 8 ? UINT64_MAX : ((UINT64_C(1) << (8u * width)) - 1u);
    uint64_t want = (uint64_t)expected & mask;

    if (lua_load(p, width, 0) == want) {
        return 0;
    }
    if (lua_load(p, width, 1) == want) {
        return 1;
    }
    return -1;
}

/* The IEEE-754 bits of value in width bytes (4 or 8): 370.5 or -370.5. */
static uint64_t lua_test_float(uint32_t width, int negative)
{
    if (width == 4) {
        return negative ? UINT64_C(0xC3B94000) : UINT64_C(0x43B94000);
    }
    return negative ? UINT64_C(0xC077280000000000) : UINT64_C(0x4077280000000000);
}

/* 5.0 TEST_NUMBER 3.14159265358979323846E7, compared as luaU does
 * through (long): 1 float, 2 integral, 0 neither. */
static int lua_test_number50(uint64_t bits, uint32_t width)
{
    uint32_t exponent;
    uint64_t mantissa;
    uint32_t mantissa_bits = width == 4 ? 23u : 52u;
    uint32_t bias = width == 4 ? 127u : 1023u;
    uint64_t integer;

    if ((uint64_t)xxbyte_sext(bits, 8u * width) == UINT64_C(31415926)) {
        return 2;
    }
    if ((bits >> (8u * width - 1u)) != 0) {
        return 0; /* negative */
    }
    exponent = (uint32_t)(bits >> mantissa_bits) & (width == 4 ? 0xFFu : 0x7FFu);
    mantissa = bits & ((UINT64_C(1) << mantissa_bits) - 1u);
    /* 31415926 needs an unbiased exponent of 24. */
    if (exponent != bias + 24u) {
        return 0;
    }
    integer = (UINT64_C(1) << mantissa_bits) | mantissa;
    integer = mantissa_bits >= 24u ? integer >> (mantissa_bits - 24u) : integer << (24u - mantissa_bits);
    return integer == UINT64_C(31415926) ? 1 : 0;
}

static xxbyte_status lua_header_need(size_t size, uint64_t count)
{
    return count > (uint64_t)size ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OK;
}

static xxbyte_status lua_parse_50(const uint8_t *data, size_t size, xxbyte_lua_header *h)
{
    uint64_t bits;
    int kind;

    if (lua_header_need(size, 14) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (data[5] > 1) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    h->big_endian = data[5] == 0;
    h->int_size = data[6];
    h->size_t_size = data[7];
    h->instruction_size = data[8];
    h->op_bits = data[9];
    h->a_bits = data[10];
    h->b_bits = data[11];
    h->c_bits = data[12];
    h->number_size = data[13];
    if (!lua_valid_size(h->int_size, 1) || !lua_valid_size(h->size_t_size, 1) || !lua_valid_size(h->instruction_size, 0) || !lua_valid_size(h->number_size, 0)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (lua_header_need(size, 14u + h->number_size) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    bits = lua_load(data + 14, h->number_size, h->big_endian);
    kind = lua_test_number50(bits, h->number_size);
    if (kind == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    h->number_integral = kind == 2;
    h->header_size = 14u + h->number_size;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status lua_parse_51(const uint8_t *data, size_t size, xxbyte_lua_header *h)
{
    uint32_t total = h->version == 0x52 ? 18u : 12u;

    if (lua_header_need(size, 6) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    h->format = data[5];
    if (h->format != 0) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if (lua_header_need(size, total) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (data[6] > 1 || data[11] > 1 || (total == 18 && memcmp(data + 12, lua_tail, 6) != 0)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    h->big_endian = data[6] == 0;
    h->int_size = data[7];
    h->size_t_size = data[8];
    h->instruction_size = data[9];
    h->number_size = data[10];
    h->number_integral = data[11];
    if (!lua_valid_size(h->int_size, 1) || !lua_valid_size(h->size_t_size, 1) || !lua_valid_size(h->instruction_size, 0) || !lua_valid_size(h->number_size, 0)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    h->header_size = total;
    return XXBYTE_STATUS_OK;
}

/* 5.3+: format, LUAC_DATA, sizes, test values, main upvalue count. */
static xxbyte_status lua_parse_53(const uint8_t *data, size_t size, xxbyte_lua_header *h)
{
    uint64_t pos;
    int order;

    if (lua_header_need(size, 6) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    h->format = data[5];
    if (h->format != 0) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if (lua_header_need(size, 12) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (memcmp(data + 6, lua_tail, 6) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (h->version == 0x55) {
        /* (size, value) pairs: int, Instruction, lua_Integer, lua_Number. */
        static const int64_t ints[3] = {-0x5678, 0x12345678, -0x5678};
        uint8_t *sizes[3];
        uint32_t i;

        sizes[0] = &h->int_size;
        sizes[1] = &h->instruction_size;
        sizes[2] = &h->integer_size;
        pos = 12;
        order = -1;
        for (i = 0; i < 3; ++i) {
            int this_order;
            if (lua_header_need(size, pos + 1u) != XXBYTE_STATUS_OK) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            *sizes[i] = data[pos];
            if (!lua_valid_size(*sizes[i], i == 0)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (lua_header_need(size, pos + 1u + *sizes[i]) != XXBYTE_STATUS_OK) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            this_order = lua_byte_order(data + pos + 1, *sizes[i], ints[i]);
            if (this_order < 0 || (order >= 0 && this_order != order)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            order = this_order;
            pos += 1u + *sizes[i];
        }
        if (lua_header_need(size, pos + 1u) != XXBYTE_STATUS_OK) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        h->number_size = data[pos++];
    } else {
        uint32_t first = h->version == 0x53 ? 14u : 12u;
        if (lua_header_need(size, 17u - (h->version == 0x53 ? 0u : 2u)) != XXBYTE_STATUS_OK) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (h->version == 0x53) {
            h->int_size = data[12];
            h->size_t_size = data[13];
            if (!lua_valid_size(h->int_size, 1) || !lua_valid_size(h->size_t_size, 1)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
        }
        h->instruction_size = data[first];
        h->integer_size = data[first + 1u];
        h->number_size = data[first + 2u];
        pos = first + 3u;
        if (!lua_valid_size(h->instruction_size, 0) || !lua_valid_size(h->integer_size, 0)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (lua_header_need(size, pos + h->integer_size) != XXBYTE_STATUS_OK) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        order = lua_byte_order(data + pos, h->integer_size, 0x5678);
        if (order < 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        pos += h->integer_size;
    }
    if (!lua_valid_size(h->number_size, 0)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    h->big_endian = (uint8_t)order;
    if (lua_header_need(size, pos + h->number_size + 1u) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (lua_load(data + pos, h->number_size, order) != lua_test_float(h->number_size, h->version == 0x55)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    pos += h->number_size;
    h->main_upvalues = data[pos];
    h->header_size = (uint32_t)(pos + 1u);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_lua_parse_header(const uint8_t *data, size_t size, xxbyte_lua_header *header)
{
    xxbyte_lua_header h;
    xxbyte_status status;
    size_t i;
    uint32_t minor;

    if (header == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(header, 0, sizeof(*header));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0; i < 4 && i < size; ++i) {
        if (data[i] != lua_signature[i]) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    if (size < 5) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (data[4] < 0x50 || data[4] > 0x55) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    memset(&h, 0, sizeof(h));
    h.version = data[4];
    minor = h.version - 0x50u;
    if (minor == 0) {
        status = lua_parse_50(data, size, &h);
    } else if (minor <= 2) {
        status = lua_parse_51(data, size, &h);
    } else {
        status = lua_parse_53(data, size, &h);
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    h.mode = XXBYTE_LUA_MODE(5, minor) | (h.big_endian ? XXBYTE_LUA_MODE_BIG_ENDIAN : 0u);
    if (minor == 0 && (h.op_bits != 6 || h.a_bits != 8 || h.b_bits != 9 || h.c_bits != 9)) {
        h.mode = 0;
    } else if (h.instruction_size == 8) {
        h.mode = minor == 0 ? h.mode | XXBYTE_LUA_MODE_WIDE : 0u;
    }
    *header = h;
    return XXBYTE_STATUS_OK;
}

/* The fields the walker relies on, as xxbyte_lua_parse_header sets them. */
static int lua_header_ok(const xxbyte_lua_header *header)
{
    uint32_t version = header->version;

    if (version < 0x50 || version > 0x55 || !lua_valid_size(header->instruction_size, 0) || !lua_valid_size(header->number_size, 0) || header->header_size < 12u) {
        return 0;
    }
    if (version <= 0x53 && (!lua_valid_size(header->int_size, 1) || !lua_valid_size(header->size_t_size, 1))) {
        return 0;
    }
    if (version >= 0x53 && !lua_valid_size(header->integer_size, 0)) {
        return 0;
    }
    return version != 0x55 || lua_valid_size(header->int_size, 1);
}

static void lua_reader_init(lua_reader *r, const uint8_t *data, size_t size, const xxbyte_lua_header *header)
{
    memset(r, 0, sizeof(*r));
    r->data = data;
    r->size = size;
    r->header = header;
    r->minor = header->version - 0x50u;
    r->pos = header->header_size;
}

xxbyte_status xxbyte_lua_find_functions(const uint8_t *data, size_t size, const xxbyte_lua_header *header, xxbyte_lua_function *functions, size_t capacity, size_t *count)
{
    lua_reader r;
    xxbyte_status status;

    if (count != NULL) {
        *count = 0;
    }
    if (data == NULL || header == NULL || count == NULL || (functions == NULL && capacity != 0) || !lua_header_ok(header)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    lua_reader_init(&r, data, size, header);
    if (r.pos > (uint64_t)size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    r.functions = functions;
    r.capacity = capacity;
    status = lua_read_function(&r, 0, XXBYTE_LUA_NO_PARENT, 0);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    *count = r.next_index;
    return XXBYTE_STATUS_OK;
}

/* Finds the 5.5 saved string with the given ordinal. */
static xxbyte_status lua_find_saved(const uint8_t *data, size_t size, const xxbyte_lua_header *header, uint64_t ordinal, lua_string *s)
{
    lua_reader r;
    xxbyte_status status;

    lua_reader_init(&r, data, size, header);
    r.want = ordinal;
    status = lua_read_function(&r, 0, XXBYTE_LUA_NO_PARENT, 0);
    if (status == LUA_FOUND) {
        s->present = 1;
        s->offset = r.found_offset;
        s->length = r.found_length;
        s->reference = 0;
        return XXBYTE_STATUS_OK;
    }
    return status == XXBYTE_STATUS_OK ? XXBYTE_STATUS_INVALID_INSTRUCTION : status;
}

static xxbyte_status lua_function_reader(lua_reader *r, const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function,
                                         uint64_t offset)
{
    if (data == NULL || header == NULL || function == NULL || !lua_header_ok(header)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    lua_reader_init(r, data, size, header);
    if (offset > (uint64_t)size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    r->pos = offset;
    /* A 5.5 string reference must name a string saved before it; the
     * count is not known here, so references are checked when they are
     * resolved. */
    r->any_reference = 1;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_lua_get_saved_strings(const uint8_t *data, size_t size, const xxbyte_lua_header *header, xxbyte_lua_string *strings, size_t capacity, size_t *count)
{
    lua_reader r;
    xxbyte_status status;

    if (count != NULL) {
        *count = 0;
    }
    if (data == NULL || header == NULL || count == NULL || (strings == NULL && capacity != 0) || !lua_header_ok(header)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    lua_reader_init(&r, data, size, header);
    if (r.pos > (uint64_t)size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    r.saved_out = strings;
    r.saved_capacity = capacity;
    status = lua_read_function(&r, 0, XXBYTE_LUA_NO_PARENT, 0);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    *count = (size_t)r.saved;
    return XXBYTE_STATUS_OK;
}

/* Turns a 5.5 reference into the saved string it names, from the table
 * when there is one, else by walking the chunk. */
static xxbyte_status lua_resolve(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_string *saved, size_t saved_count, lua_string *s)
{
    const xxbyte_lua_string *e;

    if (s->reference == 0) {
        return XXBYTE_STATUS_OK;
    }
    if (saved == NULL) {
        return lua_find_saved(data, size, header, s->reference, s);
    }
    if (s->reference > (uint64_t)saved_count) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    e = &saved[s->reference - 1u];
    if (e->length > (uint64_t)size || e->offset > (uint64_t)size - e->length) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    s->offset = e->offset;
    s->length = e->length;
    s->reference = 0;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_lua_get_constants_ex(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t first,
                                          xxbyte_lua_constant *constants, size_t count, const xxbyte_lua_string *saved, size_t saved_count)
{
    lua_reader r;
    xxbyte_status status;
    size_t i;

    /* Validate the range before touching the buffer. */
    if ((constants == NULL && count != 0) || function == NULL || first > function->constant_count || count > (size_t)(function->constant_count - first) ||
        (saved == NULL && saved_count != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (count != 0) {
        memset(constants, 0, count * sizeof(*constants));
    }
    status = lua_function_reader(&r, data, size, header, function, function->constants_offset);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < (size_t)first + count; ++i) {
        xxbyte_lua_constant k;
        lua_string s;
        status = lua_read_constant(&r, &k, &s);
        if (status == XXBYTE_STATUS_OK && k.type == XXBYTE_LUA_CONSTANT_STRING && s.reference != 0 && i >= first) {
            status = lua_resolve(data, size, header, saved, saved_count, &s);
            k.string_offset = s.offset;
            k.string_length = s.length;
        }
        if (status != XXBYTE_STATUS_OK) {
            if (count != 0) {
                memset(constants, 0, count * sizeof(*constants));
            }
            return status;
        }
        if (i >= first) {
            constants[i - first] = k;
        }
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_lua_get_constants(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t first,
                                       xxbyte_lua_constant *constants, size_t count)
{
    return xxbyte_lua_get_constants_ex(data, size, header, function, first, constants, count, NULL, 0);
}

xxbyte_status xxbyte_lua_get_constant(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t index,
                                      xxbyte_lua_constant *constant)
{
    if (constant != NULL) {
        memset(constant, 0, sizeof(*constant));
    }
    if (constant == NULL || function == NULL || index >= function->constant_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return xxbyte_lua_get_constants_ex(data, size, header, function, index, constant, 1, NULL, 0);
}

xxbyte_status xxbyte_lua_get_upvalue_names_ex(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t first,
                                              xxbyte_lua_string *names, size_t count, const xxbyte_lua_string *saved, size_t saved_count)
{
    lua_reader r;
    xxbyte_status status;
    size_t i;

    /* Validate the range before touching the buffer. */
    if ((names == NULL && count != 0) || function == NULL || first > function->upvalue_name_count || count > (size_t)(function->upvalue_name_count - first) ||
        (saved == NULL && saved_count != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (count != 0) {
        memset(names, 0, count * sizeof(*names));
    }
    status = lua_function_reader(&r, data, size, header, function, function->upvalue_names_offset);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < (size_t)first + count; ++i) {
        lua_string s;
        status = lua_read_string(&r, &s);
        if (status == XXBYTE_STATUS_OK && i >= first) {
            status = lua_resolve(data, size, header, saved, saved_count, &s);
        }
        if (status != XXBYTE_STATUS_OK) {
            if (count != 0) {
                memset(names, 0, count * sizeof(*names));
            }
            return status;
        }
        if (i >= first && s.present) {
            names[i - first].offset = s.offset;
            names[i - first].length = s.length;
        }
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_lua_get_upvalue_names(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t first,
                                           xxbyte_lua_string *names, size_t count)
{
    return xxbyte_lua_get_upvalue_names_ex(data, size, header, function, first, names, count, NULL, 0);
}

xxbyte_status xxbyte_lua_get_upvalue_name_ex(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t index,
                                             uint64_t *offset, uint64_t *length, const xxbyte_lua_string *saved, size_t saved_count)
{
    xxbyte_lua_string name;
    xxbyte_status status;

    if (offset != NULL) {
        *offset = 0;
    }
    if (length != NULL) {
        *length = 0;
    }
    if (offset == NULL || length == NULL || function == NULL || index >= function->upvalue_name_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = xxbyte_lua_get_upvalue_names_ex(data, size, header, function, index, &name, 1, saved, saved_count);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    *offset = name.offset;
    *length = name.length;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_lua_get_upvalue_name(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t index,
                                          uint64_t *offset, uint64_t *length)
{
    return xxbyte_lua_get_upvalue_name_ex(data, size, header, function, index, offset, length, NULL, 0);
}

xxbyte_status xxbyte_lua_get_source(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function,
                                    const xxbyte_lua_string *saved, size_t saved_count, xxbyte_lua_string *source, int *present)
{
    lua_reader r;
    lua_string s;
    xxbyte_status status;

    if (source != NULL) {
        memset(source, 0, sizeof(*source));
    }
    if (present != NULL) {
        *present = 0;
    }
    if (source == NULL || present == NULL || (saved == NULL && saved_count != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = lua_function_reader(&r, data, size, header, function, function != NULL ? function->source_offset : 0);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    status = lua_read_string(&r, &s);
    if (status == XXBYTE_STATUS_OK) {
        status = lua_resolve(data, size, header, saved, saved_count, &s);
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (s.present) {
        source->offset = s.offset;
        source->length = s.length;
        *present = 1;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_lua_get_locals(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t first,
                                    xxbyte_lua_local *locals, size_t count, const xxbyte_lua_string *saved, size_t saved_count)
{
    lua_reader r;
    xxbyte_status status;
    size_t i;

    if ((locals == NULL && count != 0) || function == NULL || first > function->local_count || count > (size_t)(function->local_count - first) ||
        (saved == NULL && saved_count != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (count != 0) {
        memset(locals, 0, count * sizeof(*locals));
    }
    status = lua_function_reader(&r, data, size, header, function, function->locals_offset);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < (size_t)first + count; ++i) {
        lua_string s;
        uint64_t start = 0;
        uint64_t end = 0;
        status = lua_read_string(&r, &s);
        if (status == XXBYTE_STATUS_OK) {
            status = lua_read_int(&r, &start);
        }
        if (status == XXBYTE_STATUS_OK) {
            status = lua_read_int(&r, &end);
        }
        if (status == XXBYTE_STATUS_OK && i >= first) {
            status = lua_resolve(data, size, header, saved, saved_count, &s);
        }
        if (status != XXBYTE_STATUS_OK) {
            if (count != 0) {
                memset(locals, 0, count * sizeof(*locals));
            }
            return status;
        }
        if (i >= first) {
            xxbyte_lua_local *l = &locals[i - first];
            l->present = (uint8_t)(s.present != 0);
            l->name_offset = s.offset;
            l->name_length = s.length;
            l->start_pc = (uint32_t)start;
            l->end_pc = (uint32_t)end;
        }
    }
    return XXBYTE_STATUS_OK;
}

uint32_t xxbyte_lua_local_register(const xxbyte_lua_local *locals, size_t count, uint32_t reg, uint32_t pc)
{
    size_t i;
    uint64_t n = (uint64_t)reg + 1u;

    if (locals == NULL) {
        return XXBYTE_LUA_NO_LOCAL;
    }
    /* luaF_getlocalname: the reg + 1-th variable active at pc. */
    for (i = 0; i < count && locals[i].start_pc <= pc; ++i) {
        if (pc < locals[i].end_pc && --n == 0) {
            return (uint32_t)i;
        }
    }
    return XXBYTE_LUA_NO_LOCAL;
}

xxbyte_status xxbyte_lua_get_upvalues(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t first,
                                      xxbyte_lua_upvalue *upvalues, size_t count)
{
    uint32_t record;
    size_t i;

    if ((upvalues == NULL && count != 0) || function == NULL || header == NULL || header->version < 0x52 || first > function->upvalue_count ||
        count > (size_t)(function->upvalue_count - first)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (count != 0) {
        memset(upvalues, 0, count * sizeof(*upvalues));
    }
    if (data == NULL || !lua_header_ok(header)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    record = header->version >= 0x54 ? 3u : 2u;
    if (function->upvalues_offset > (uint64_t)size || (uint64_t)function->upvalue_count * record > (uint64_t)size - function->upvalues_offset) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    for (i = 0; i < count; ++i) {
        const uint8_t *p = data + function->upvalues_offset + ((uint64_t)first + i) * record;
        upvalues[i].in_stack = p[0];
        upvalues[i].index = p[1];
        upvalues[i].kind = record == 3 ? p[2] : 0u;
    }
    return XXBYTE_STATUS_OK;
}

/* Reads one signed int of the chunk's int size at data[offset]. */
static int64_t lua_load_int(const uint8_t *data, uint64_t offset, const xxbyte_lua_header *header)
{
    uint32_t width = header->int_size;
    return xxbyte_sext(lua_load(data + offset, width, header->big_endian != 0), 8u * width);
}

xxbyte_status xxbyte_lua_get_lines(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, int64_t *lines, size_t count)
{
    lua_reader r;
    xxbyte_status status;
    uint64_t width;
    uint64_t abs_pc = 0;
    uint64_t abs_line = 0;
    uint64_t abs_index = 0;
    int have_abs = 0;
    int64_t line = 0;
    size_t pc;

    if ((lines == NULL && count != 0) || function == NULL || count > (size_t)function->code_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (count != 0) {
        memset(lines, 0, count * sizeof(*lines));
    }
    status = lua_function_reader(&r, data, size, header, function, function->lineinfo_offset);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    width = r.minor >= 4 ? 1u : header->int_size;
    if (function->lineinfo_offset > (uint64_t)size || (uint64_t)function->lineinfo_count * width > (uint64_t)size - function->lineinfo_offset) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (r.minor < 4) {
        /* Absolute lines; luac shows 0 and below as "[-]". */
        for (pc = 0; pc < count && pc < function->lineinfo_count; ++pc) {
            lines[pc] = lua_load_int(data, function->lineinfo_offset + pc * width, header);
        }
        return XXBYTE_STATUS_OK;
    }
    if (function->lineinfo_count == 0) {
        return XXBYTE_STATUS_OK; /* no line info: luac's "[-]" */
    }
    /* luaG_getfuncline: the last absolute line at or before pc, then the
     * deltas after it. */
    r.pos = function->abslineinfo_offset;
    for (pc = 0; pc < count; ++pc) {
        int advanced = 0;
        while (abs_index < function->abslineinfo_count) {
            uint64_t next_pc;
            uint64_t next_line;
            if (r.minor == 4) {
                uint64_t save = r.pos;
                if ((status = lua_read_int(&r, &next_pc)) != XXBYTE_STATUS_OK || (status = lua_read_int(&r, &next_line)) != XXBYTE_STATUS_OK) {
                    memset(lines, 0, count * sizeof(*lines));
                    return status;
                }
                if (next_pc > pc) {
                    r.pos = save;
                    break;
                }
            } else {
                uint64_t at = r.pos;
                if ((uint64_t)size < at || 2u * (uint64_t)header->int_size > (uint64_t)size - at) {
                    memset(lines, 0, count * sizeof(*lines));
                    return XXBYTE_STATUS_TRUNCATED;
                }
                next_pc = (uint64_t)lua_load_int(data, at, header);
                next_line = (uint64_t)lua_load_int(data, at + header->int_size, header);
                if ((int64_t)next_pc > (int64_t)pc) {
                    break;
                }
                r.pos = at + 2u * header->int_size;
            }
            abs_pc = next_pc;
            abs_line = next_line;
            have_abs = 1;
            advanced = 1;
            ++abs_index;
        }
        if (advanced) {
            /* Rebase at the absolute entry, then add the deltas after it. */
            uint64_t i;
            line = (int64_t)(int32_t)abs_line;
            for (i = abs_pc + 1u; i <= pc && i < function->lineinfo_count; ++i) {
                line += (int8_t)data[function->lineinfo_offset + i];
            }
        } else if (!have_abs && pc == 0) {
            line = (int64_t)function->line_defined;
            if (function->lineinfo_count > 0) {
                line += (int8_t)data[function->lineinfo_offset];
            }
        } else if (pc < function->lineinfo_count) {
            line += (int8_t)data[function->lineinfo_offset + pc];
        }
        lines[pc] = (int64_t)(int32_t)line;
    }
    return XXBYTE_STATUS_OK;
}

#else
typedef int xxbyte_lua_chunk_disabled;
#endif
