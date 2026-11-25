#include "xxbyte_luajit_dump.h"
#include "xxbyte_lua_number.h"

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#if XXBYTE_USE_LUAJIT

/* Dump contents (lj_bcread.c), names and luajit -bl listings (jit/bc.lua). */

#define LJ_STR_SHOWN 40u /* bc.lua shows "%.40s" of a longer string */
#define LJ_BUILTIN_COUNT 6u
#define LJ_LOC_LONG_NAME 40u /* lj_debug_pushloc: "%p" for longer names */

static const char *const lj_builtin_names[LJ_BUILTIN_COUNT] = {
    "(for index)", "(for limit)", "(for step)", "(for generator)", "(for state)", "(for control)",
};

/* The end of the prototype's bytes, within size. */
static size_t lj_proto_end(const xxbyte_luajit_proto *proto, size_t size)
{
    uint64_t end = (uint64_t)proto->offset + proto->size;

    return end > (uint64_t)size ? size : (size_t)end;
}

xxbyte_status xxbyte_luajit_get_kgc(const uint8_t *data, size_t size, const xxbyte_luajit_proto *proto, uint32_t first, xxbyte_luajit_kgc *kgc, size_t count)
{
    size_t end;
    size_t pos;
    size_t i;

    if (data == NULL || proto == NULL || (kgc == NULL && count != 0) || first > proto->sizekgc || count > (size_t)(proto->sizekgc - first)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (count != 0) {
        memset(kgc, 0, count * sizeof(*kgc));
    }
    end = lj_proto_end(proto, size);
    pos = proto->kgc_offset;
    if (pos > end) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    for (i = 0; i < (size_t)first + count; ++i) {
        xxbyte_luajit_kgc k;
        xxbyte_status status;
        uint32_t type;
        uint32_t words[4] = {0, 0, 0, 0};
        uint32_t j;

        memset(&k, 0, sizeof(k));
        status = xxbyte_luajit_read_uleb(data, end, &pos, &type);
        if (status == XXBYTE_STATUS_OK) {
            if (type >= XXBYTE_LUAJIT_KGC_STR) {
                k.type = XXBYTE_LUAJIT_KGC_STR;
                k.offset = (uint32_t)pos;
                k.length = type - XXBYTE_LUAJIT_KGC_STR;
                if (k.length > end - pos) {
                    status = XXBYTE_STATUS_TRUNCATED;
                } else {
                    pos += k.length;
                }
            } else if (type == XXBYTE_LUAJIT_KGC_TAB) {
                uint32_t narray = 0;
                uint32_t nhash = 0;
                k.type = type;
                k.offset = (uint32_t)pos;
                status = xxbyte_luajit_read_uleb(data, end, &pos, &narray);
                if (status == XXBYTE_STATUS_OK) {
                    status = xxbyte_luajit_read_uleb(data, end, &pos, &nhash);
                }
                for (j = 0; status == XXBYTE_STATUS_OK && j < narray; ++j) {
                    status = xxbyte_luajit_skip_ktabk(data, end, &pos);
                }
                for (j = 0; status == XXBYTE_STATUS_OK && j < nhash; ++j) {
                    status = xxbyte_luajit_skip_ktabk(data, end, &pos);
                    if (status == XXBYTE_STATUS_OK) {
                        status = xxbyte_luajit_skip_ktabk(data, end, &pos);
                    }
                }
                k.length = (uint32_t)(pos - k.offset);
            } else if (type == XXBYTE_LUAJIT_KGC_CHILD) {
                k.type = type;
            } else {
                uint32_t n = type == XXBYTE_LUAJIT_KGC_COMPLEX ? 4u : 2u;
                k.type = type;
                for (j = 0; status == XXBYTE_STATUS_OK && j < n; ++j) {
                    status = xxbyte_luajit_read_uleb(data, end, &pos, &words[j]);
                }
                k.value = (uint64_t)words[0] | ((uint64_t)words[1] << 32);
                k.imag = (uint64_t)words[2] | ((uint64_t)words[3] << 32);
            }
        }
        if (status != XXBYTE_STATUS_OK) {
            if (count != 0) {
                memset(kgc, 0, count * sizeof(*kgc));
            }
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (i >= first) {
            kgc[i - first] = k;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* bcread_uleb128_33: the low bit of the first byte is a flag. */
static xxbyte_status lj_read_uleb33(const uint8_t *data, size_t end, size_t *pos, uint32_t *value)
{
    uint64_t v;
    uint32_t shift = 6;
    uint32_t i;

    if (*pos >= end) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    v = (uint64_t)(data[*pos] >> 1);
    if (data[(*pos)++] < 0x80u) {
        *value = (uint32_t)v;
        return XXBYTE_STATUS_OK;
    }
    v &= 0x3Fu;
    for (i = 1; i < 5u; ++i) {
        uint8_t byte;
        if (*pos >= end) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        byte = data[(*pos)++];
        v |= (uint64_t)(byte & 0x7Fu) << shift;
        shift += 7;
        if (byte < 0x80u) {
            *value = (uint32_t)v; /* LuaJIT keeps 32 bits */
            return XXBYTE_STATUS_OK;
        }
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

static uint64_t lj_double_bits(double value)
{
    uint64_t bits;

    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static double lj_bits_double(uint64_t bits)
{
    double value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

xxbyte_status xxbyte_luajit_get_knum(const uint8_t *data, size_t size, const xxbyte_luajit_proto *proto, uint32_t first, xxbyte_luajit_knum *knum, size_t count)
{
    size_t end;
    size_t pos;
    size_t i;

    if (data == NULL || proto == NULL || (knum == NULL && count != 0) || first > proto->sizekn || count > (size_t)(proto->sizekn - first)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (count != 0) {
        memset(knum, 0, count * sizeof(*knum));
    }
    end = lj_proto_end(proto, size);
    pos = proto->kn_offset;
    if (pos > end) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    for (i = 0; i < (size_t)first + count; ++i) {
        xxbyte_luajit_knum k;
        uint32_t lo = 0;
        uint32_t hi = 0;
        int is_double;
        xxbyte_status status;

        memset(&k, 0, sizeof(k));
        is_double = pos < end && (data[pos] & 1u) != 0;
        status = lj_read_uleb33(data, end, &pos, &lo);
        if (status == XXBYTE_STATUS_OK && is_double) {
            status = xxbyte_luajit_read_uleb(data, end, &pos, &hi);
        }
        if (status != XXBYTE_STATUS_OK) {
            if (count != 0) {
                memset(knum, 0, count * sizeof(*knum));
            }
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (is_double) {
            k.bits = (uint64_t)lo | ((uint64_t)hi << 32);
        } else {
            k.is_integer = 1;
            k.integer = (int32_t)lo;
            k.bits = lj_double_bits((double)k.integer);
        }
        if (i >= first) {
            knum[i - first] = k;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* The debug info's upvalue names start after the line info. */
static xxbyte_status lj_uvinfo(const xxbyte_luajit_proto *proto, size_t size, size_t *pos, size_t *end)
{
    uint64_t start;
    uint64_t stop;

    if (proto->sizedbg == 0 || proto->lineinfo_width == 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    start = (uint64_t)proto->debug_offset + (uint64_t)proto->sizebc * proto->lineinfo_width;
    stop = (uint64_t)proto->debug_offset + proto->sizedbg;
    if (stop > (uint64_t)size || start > stop) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *pos = (size_t)start;
    *end = (size_t)stop;
    return XXBYTE_STATUS_OK;
}

/* Reads one NUL-terminated name. */
static xxbyte_status lj_read_name(const uint8_t *data, size_t end, size_t *pos, xxbyte_luajit_string *name)
{
    size_t start = *pos;

    while (*pos < end && data[*pos] != 0) {
        ++*pos;
    }
    if (*pos >= end) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (name != NULL) {
        name->offset = (uint32_t)start;
        name->length = (uint32_t)(*pos - start);
    }
    ++*pos;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_luajit_get_uv_names(const uint8_t *data, size_t size, const xxbyte_luajit_proto *proto, xxbyte_luajit_string *names, size_t count)
{
    size_t pos;
    size_t end;
    size_t i;
    xxbyte_status status;

    if (data == NULL || proto == NULL || (names == NULL && count != 0) || count > proto->sizeuv) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (count != 0) {
        memset(names, 0, count * sizeof(*names));
    }
    status = lj_uvinfo(proto, size, &pos, &end);
    for (i = 0; status == XXBYTE_STATUS_OK && i < count; ++i) {
        status = lj_read_name(data, end, &pos, &names[i]);
    }
    if (status != XXBYTE_STATUS_OK && count != 0) {
        memset(names, 0, count * sizeof(*names));
    }
    return status;
}

xxbyte_status xxbyte_luajit_get_uv_refs(const uint8_t *data, size_t size, const xxbyte_luajit_header *header, const xxbyte_luajit_proto *proto, uint16_t *refs,
                                        size_t count)
{
    size_t i;
    int big;

    if (data == NULL || header == NULL || proto == NULL || (refs == NULL && count != 0) || count > proto->sizeuv ||
        (uint64_t)proto->uv_offset + 2u * (uint64_t)count > (uint64_t)size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    big = (header->flags & XXBYTE_LUAJIT_FLAG_BE) != 0;
    for (i = 0; i < count; ++i) {
        const uint8_t *p = data + proto->uv_offset + 2u * i;
        refs[i] = big ? xxbyte_u16be(p) : xxbyte_u16le(p);
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_luajit_get_lines(const uint8_t *data, size_t size, const xxbyte_luajit_header *header, const xxbyte_luajit_proto *proto, uint32_t *lines,
                                      size_t count)
{
    size_t i;
    uint32_t width;
    int big;

    if (data == NULL || header == NULL || proto == NULL || (lines == NULL && count != 0) || count > proto->sizebc) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (count != 0) {
        memset(lines, 0, count * sizeof(*lines));
    }
    width = proto->lineinfo_width;
    if (width == 0 || (uint64_t)proto->debug_offset + (uint64_t)count * width > (uint64_t)size) {
        return XXBYTE_STATUS_OK; /* no line info */
    }
    big = (header->flags & XXBYTE_LUAJIT_FLAG_BE) != 0;
    for (i = 0; i < count; ++i) {
        const uint8_t *p = data + proto->debug_offset + i * width;
        uint32_t delta = width == 1 ? p[0] : width == 2 ? (big ? xxbyte_u16be(p) : xxbyte_u16le(p)) : (big ? xxbyte_u32be(p) : xxbyte_u32le(p));
        lines[i] = proto->firstline + delta;
    }
    return XXBYTE_STATUS_OK;
}

/* lj_buf_ruleb128, limited to 5 bytes. */
static xxbyte_status lj_read_var_uleb(const uint8_t *data, size_t end, size_t *pos, uint32_t *value)
{
    uint64_t v = 0;
    uint32_t i;

    for (i = 0; i < 5u; ++i) {
        uint8_t byte;
        if (*pos >= end) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        byte = data[(*pos)++];
        v |= (uint64_t)(byte & 0x7Fu) << (7u * i);
        if (byte < 0x80u) {
            *value = (uint32_t)v;
            return XXBYTE_STATUS_OK;
        }
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

xxbyte_status xxbyte_luajit_get_vars(const uint8_t *data, size_t size, const xxbyte_luajit_proto *proto, xxbyte_luajit_var *vars, size_t capacity, size_t *count)
{
    size_t pos;
    size_t end;
    size_t n = 0;
    uint32_t i;
    uint32_t last = 0;
    xxbyte_status status;

    if (count != NULL) {
        *count = 0;
    }
    if (data == NULL || proto == NULL || count == NULL || (vars == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = lj_uvinfo(proto, size, &pos, &end);
    for (i = 0; status == XXBYTE_STATUS_OK && i < proto->sizeuv; ++i) {
        status = lj_read_name(data, end, &pos, NULL);
    }
    while (status == XXBYTE_STATUS_OK) {
        xxbyte_luajit_var v;
        uint32_t start;
        uint32_t length;
        memset(&v, 0, sizeof(v));
        if (pos >= end) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (data[pos] == 0) {
            break; /* VARNAME_END */
        }
        if (data[pos] <= LJ_BUILTIN_COUNT) {
            v.builtin = data[pos++];
        } else {
            xxbyte_luajit_string name;
            status = lj_read_name(data, end, &pos, &name);
            v.name_offset = name.offset;
            v.name_length = name.length;
        }
        if (status == XXBYTE_STATUS_OK) {
            status = lj_read_var_uleb(data, end, &pos, &start);
        }
        if (status == XXBYTE_STATUS_OK) {
            status = lj_read_var_uleb(data, end, &pos, &length);
        }
        if (status != XXBYTE_STATUS_OK) {
            break;
        }
        last += start;
        v.start_pc = last;
        v.end_pc = last + length;
        if (n < capacity) {
            vars[n] = v;
        }
        ++n;
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    *count = n;
    return XXBYTE_STATUS_OK;
}

uint32_t xxbyte_luajit_var_slot(const xxbyte_luajit_var *vars, size_t count, uint32_t slot, uint32_t pc)
{
    size_t i;

    if (vars == NULL) {
        return XXBYTE_LUAJIT_NO_VAR;
    }
    for (i = 0; i < count && vars[i].start_pc <= pc; ++i) {
        if (pc < vars[i].end_pc && slot-- == 0) {
            return (uint32_t)i;
        }
    }
    return XXBYTE_LUAJIT_NO_VAR;
}

const char *xxbyte_luajit_builtin_name(uint32_t builtin)
{
    return builtin >= 1 && builtin <= LJ_BUILTIN_COUNT ? lj_builtin_names[builtin - 1] : NULL;
}

/* Walks the GC constant types of a prototype for the pairing. */
static xxbyte_status lj_pair_children(const uint8_t *data, size_t size, const xxbyte_luajit_proto *proto, uint32_t index, uint32_t *stack, size_t *depth,
                                      xxbyte_luajit_function *functions, size_t capacity)
{
    size_t end = lj_proto_end(proto, size);
    size_t pos = proto->kgc_offset;
    uint32_t i;

    for (i = 0; i < proto->sizekgc; ++i) {
        uint32_t type;
        xxbyte_status status = xxbyte_luajit_read_uleb(data, end, &pos, &type);
        uint32_t j;
        if (status != XXBYTE_STATUS_OK) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (type >= XXBYTE_LUAJIT_KGC_STR) {
            uint32_t length = type - XXBYTE_LUAJIT_KGC_STR;
            if (length > end - pos) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            pos += length;
        } else if (type == XXBYTE_LUAJIT_KGC_TAB) {
            uint32_t narray = 0;
            uint32_t nhash = 0;
            status = xxbyte_luajit_read_uleb(data, end, &pos, &narray);
            if (status == XXBYTE_STATUS_OK) {
                status = xxbyte_luajit_read_uleb(data, end, &pos, &nhash);
            }
            for (j = 0; status == XXBYTE_STATUS_OK && j < narray; ++j) {
                status = xxbyte_luajit_skip_ktabk(data, end, &pos);
            }
            for (j = 0; status == XXBYTE_STATUS_OK && j < 2u * (uint64_t)nhash; ++j) {
                status = xxbyte_luajit_skip_ktabk(data, end, &pos);
            }
        } else if (type == XXBYTE_LUAJIT_KGC_CHILD) {
            uint32_t child;
            if (*depth == 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION; /* stack underflow */
            }
            child = stack[--*depth];
            if (child < capacity) {
                functions[child].parent = index;
                functions[child].kgc_index = i;
                functions[child].operand = proto->sizekgc - 1u - i;
            }
        } else {
            uint32_t n = type == XXBYTE_LUAJIT_KGC_COMPLEX ? 4u : 2u;
            for (j = 0; status == XXBYTE_STATUS_OK && j < n; ++j) {
                status = xxbyte_luajit_read_uleb(data, end, &pos, NULL);
            }
        }
        if (status != XXBYTE_STATUS_OK) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    stack[(*depth)++] = index;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_luajit_find_functions(const uint8_t *data, size_t size, const xxbyte_luajit_header *header, xxbyte_luajit_function *functions, size_t capacity,
                                           size_t *count)
{
    xxbyte_luajit_proto proto;
    xxbyte_status status;
    uint32_t *stack;
    size_t offset;
    size_t n = 0;
    size_t depth = 0;
    size_t i;

    if (count != NULL) {
        *count = 0;
    }
    if (data == NULL || header == NULL || count == NULL || (functions == NULL && capacity != 0) || header->size > size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    /* Count the prototypes first: the pairing needs a stack of them.
     * lj_bcread needs the terminating 0. */
    for (offset = header->size;; offset += proto.size) {
        status = xxbyte_luajit_parse_proto(data, size, offset, header, &proto);
        if (status == XXBYTE_STATUS_END_OF_INPUT) {
            break;
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        ++n;
    }
    if (n == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    stack = (uint32_t *)xx_mem_calloc(n, sizeof(*stack));
    if (stack == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    offset = header->size;
    status = XXBYTE_STATUS_OK;
    for (i = 0; i < n && status == XXBYTE_STATUS_OK; ++i) {
        status = xxbyte_luajit_parse_proto(data, size, offset, header, &proto);
        if (status != XXBYTE_STATUS_OK) {
            break;
        }
        if (i < capacity) {
            memset(&functions[i], 0, sizeof(functions[i]));
            functions[i].proto = proto;
            functions[i].index = (uint32_t)i;
            functions[i].parent = XXBYTE_LUAJIT_NO_PARENT;
            functions[i].kgc_index = 0;
            functions[i].operand = 0;
        }
        status = lj_pair_children(data, size, &proto, (uint32_t)i, stack, &depth, functions, capacity);
        offset += proto.size;
    }
    xx_mem_free(stack);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (depth != 1) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *count = n;
    return XXBYTE_STATUS_OK;
}

/* -- Names ---------------------------------------------------------------- */

typedef struct lj_sink {
    xx_buf_t *out;
    char *buffer;
    size_t capacity;
    size_t length;
    int ok;
} lj_sink;

static void lj_put(lj_sink *s, const char *p, size_t n)
{
    if (!s->ok || n == 0) {
        return;
    }
    if (s->out != NULL) {
        if (!xx_buf_append(s->out, p, n)) {
            s->ok = 0;
        }
        return;
    }
    if (n > s->capacity - s->length) {
        s->ok = 0;
        return;
    }
    memcpy(s->buffer + s->length, p, n);
    s->length += n;
}

static void lj_str(lj_sink *s, const char *text)
{
    lj_put(s, text, strlen(text));
}

static void lj_int(lj_sink *s, long long value)
{
    char text[24];
    size_t at = sizeof(text);
    unsigned long long u = value < 0 ? 0ull - (unsigned long long)value : (unsigned long long)value;

    do {
        text[--at] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u != 0);
    if (value < 0) {
        text[--at] = '-';
    }
    lj_put(s, text + at, sizeof(text) - at);
}

static void lj_hex(lj_sink *s, unsigned long long value)
{
    static const char digits[] = "0123456789abcdef";
    char text[24];
    size_t at = sizeof(text);

    do {
        text[--at] = digits[value & 15u];
        value >>= 4;
    } while (value != 0);
    text[--at] = 'x';
    text[--at] = '0';
    lj_put(s, text + at, sizeof(text) - at);
}

static const xxbyte_luajit_function *lj_current(const xxbyte_luajit_names *names)
{
    if (names == NULL || names->data == NULL || names->header == NULL || names->functions == NULL || names->function >= names->function_count) {
        return NULL;
    }
    return &names->functions[names->function];
}

/* bc.lua: gsub(s, "%c", ctlsub), then "%.40s" and "~" when longer. */
static void lj_quoted(lj_sink *s, const uint8_t *p, uint32_t n)
{
    uint32_t shown = 0;
    uint32_t i;

    lj_put(s, "\"", 1);
    for (i = 0; i < n && (n <= LJ_STR_SHOWN || shown < LJ_STR_SHOWN); ++i) {
        uint8_t c = p[i];
        char text[4];
        size_t len = 1;
        if (c == '\n') {
            text[0] = '\\';
            text[1] = 'n';
            len = 2;
        } else if (c == '\r') {
            text[0] = '\\';
            text[1] = 'r';
            len = 2;
        } else if (c == '\t') {
            text[0] = '\\';
            text[1] = 't';
            len = 2;
        } else if (c < 0x20u || c == 0x7Fu) {
            text[0] = '\\';
            text[1] = (char)('0' + c / 100u);
            text[2] = (char)('0' + c / 10u % 10u);
            text[3] = (char)('0' + c % 10u);
            len = 4;
        } else {
            text[0] = (char)c;
        }
        if (n > LJ_STR_SHOWN && shown + len > LJ_STR_SHOWN) {
            len = LJ_STR_SHOWN - shown; /* the escape is cut */
        }
        lj_put(s, text, len);
        shown += (uint32_t)len;
    }
    lj_str(s, n > LJ_STR_SHOWN ? "\"~" : "\"");
}

static int lj_kgc(const xxbyte_luajit_names *names, const xxbyte_luajit_function *f, uint64_t operand, xxbyte_luajit_kgc *k, uint32_t *index)
{
    uint32_t i;

    if (operand >= f->proto.sizekgc) {
        return 0;
    }
    i = f->proto.sizekgc - 1u - (uint32_t)operand;
    *index = i;
    if (names->kgc != NULL) {
        *k = names->kgc[i];
        return 1;
    }
    return xxbyte_luajit_get_kgc(names->data, names->size, &f->proto, i, k, 1) == XXBYTE_STATUS_OK;
}

static int lj_str_constant(lj_sink *s, const xxbyte_luajit_names *names, const xxbyte_luajit_function *f, uint64_t operand)
{
    xxbyte_luajit_kgc k;
    uint32_t i;

    if (!lj_kgc(names, f, operand, &k, &i) || k.type != XXBYTE_LUAJIT_KGC_STR || (uint64_t)k.offset + k.length > (uint64_t)names->size) {
        return 0;
    }
    lj_quoted(s, names->data + k.offset, k.length);
    return 1;
}

static int lj_num_constant(lj_sink *s, const xxbyte_luajit_names *names, const xxbyte_luajit_function *f, uint64_t operand, int tsetm)
{
    xxbyte_luajit_knum k;
    char text[XXBYTE_LUA_G_SIZE];
    uint64_t bits;

    if (operand >= f->proto.sizekn) {
        return 0;
    }
    if (names->knum != NULL) {
        k = names->knum[operand];
    } else if (xxbyte_luajit_get_knum(names->data, names->size, &f->proto, (uint32_t)operand, &k, 1) != XXBYTE_STATUS_OK) {
        return 0;
    }
    bits = k.bits;
    if (tsetm) {
        /* bc.lua: kc - 2^52, the first array index. */
        volatile double value = lj_bits_double(bits);
        value -= 4503599627370496.0;
        bits = lj_double_bits(value);
    }
    xxbyte_lua_format_g(bits, 14, 0, text);
    lj_str(s, text);
    return 1;
}

static int lj_uv_name(lj_sink *s, const xxbyte_luajit_names *names, const xxbyte_luajit_function *f, uint64_t index)
{
    xxbyte_luajit_string name;

    if (index >= f->proto.sizeuv) {
        return 0;
    }
    if (f->proto.sizedbg == 0) {
        return 1; /* lj_debug_uvname: "" without debug info */
    }
    if (names->uv_names != NULL) {
        name = names->uv_names[index];
    } else {
        xxbyte_luajit_string all[256];
        if (xxbyte_luajit_get_uv_names(names->data, names->size, &f->proto, all, (size_t)index + 1u) != XXBYTE_STATUS_OK) {
            return 0;
        }
        name = all[index];
    }
    if ((uint64_t)name.offset + name.length > (uint64_t)names->size) {
        return 0;
    }
    lj_put(s, (const char *)names->data + name.offset, name.length);
    return 1;
}

/* Appends p[0..length) up to its first NUL, as "%s" prints it. */
static void lj_put_cstr(lj_sink *s, const uint8_t *p, uint32_t length)
{
    uint32_t i;

    for (i = 0; i < length && p[i] != 0; ++i) {
    }
    lj_put(s, (const char *)p, i);
}

/* lj_debug_pushloc for pc 0 of a prototype. */
static void lj_loc(lj_sink *s, const xxbyte_luajit_names *names, const xxbyte_luajit_function *f)
{
    const xxbyte_luajit_header *h = names->header;
    const uint8_t *p;
    uint32_t length;
    uint32_t i;
    int32_t line = f->proto.sizedbg != 0 ? (int32_t)f->proto.firstline : 0;

    if ((h->flags & XXBYTE_LUAJIT_FLAG_STRIP) != 0 || (uint64_t)h->name_offset + h->name_size > (uint64_t)names->size) {
        lj_str(s, "?"); /* the loader's name for a stripped dump */
    } else {
        p = names->data + h->name_offset;
        length = h->name_size;
        if (h->version >= 2 && f->proto.firstline == UINT32_MAX) {
            lj_str(s, "builtin:");
            lj_put_cstr(s, p, length);
            return;
        }
        if (length > 0 && p[0] == '@') {
            /* The part after the last slash (s[len], the NUL, included). */
            ++p;
            --length;
            for (i = length; i > 0; --i) {
                if (i < length && (p[i] == '/' || p[i] == '\\')) {
                    p += i + 1u;
                    length -= i + 1u;
                    break;
                }
            }
            lj_put_cstr(s, p, length);
        } else if (length > LJ_LOC_LONG_NAME) {
            lj_hex(s, (unsigned long long)f->proto.offset); /* "%p" of the prototype */
        } else if (length > 0 && p[0] == '=') {
            lj_put_cstr(s, p + 1, length - 1u);
        } else {
            lj_put(s, "\"", 1);
            lj_put_cstr(s, p, length);
            lj_put(s, "\"", 1);
        }
    }
    lj_put(s, ":", 1);
    lj_int(s, line);
}

static const xxbyte_luajit_function *lj_child(const xxbyte_luajit_names *names, const xxbyte_luajit_function *f, uint64_t operand)
{
    xxbyte_luajit_kgc k;
    uint32_t i;
    size_t j;

    if (!lj_kgc(names, f, operand, &k, &i) || k.type != XXBYTE_LUAJIT_KGC_CHILD) {
        return NULL;
    }
    if (names->children != NULL) {
        uint32_t c = names->children[i];
        return c < names->function_count ? &names->functions[c] : NULL;
    }
    for (j = 0; j < names->function_count; ++j) {
        if (names->functions[j].parent == names->function && names->functions[j].kgc_index == i) {
            return &names->functions[j];
        }
    }
    return NULL;
}

void xxbyte_luajit_names_init(xxbyte_luajit_names *names, const uint8_t *data, size_t size, const xxbyte_luajit_header *header, const xxbyte_luajit_function *functions,
                              size_t function_count)
{
    if (names == NULL) {
        return;
    }
    memset(names, 0, sizeof(*names));
    names->data = data;
    names->size = size;
    names->header = header;
    names->functions = functions;
    names->function_count = function_count;
}

/* The -bl text of an INDEX operand; 0 when there is none. */
static int lj_operand_text(lj_sink *s, const xxbyte_luajit_names *names, const xxbyte_luajit_function *f, const xxbyte_instruction *instruction,
                           const xxbyte_operand *operand)
{
    const xxbyte_luajit_function *child;

    if (operand->type != XXBYTE_OPERAND_INDEX || operand->value < 0) {
        return 0;
    }
    switch (operand->extra) {
        case XXBYTE_LUAJIT_OPERAND_STR: return lj_str_constant(s, names, f, (uint64_t)operand->value);
        case XXBYTE_LUAJIT_OPERAND_NUM: return lj_num_constant(s, names, f, (uint64_t)operand->value, instruction->id == XXBYTE_LUAJIT_ID_TSETM);
        case XXBYTE_LUAJIT_OPERAND_UV: return lj_uv_name(s, names, f, (uint64_t)operand->value);
        case XXBYTE_LUAJIT_OPERAND_FUNC:
            child = lj_child(names, f, (uint64_t)operand->value);
            if (child == NULL) {
                return 0;
            }
            lj_loc(s, names, child);
            return 1;
        default: return 0;
    }
}

size_t xxbyte_luajit_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_luajit_names *names = (const xxbyte_luajit_names *)context;
    const xxbyte_luajit_function *f = lj_current(names);
    lj_sink s;

    if (f == NULL || instruction == NULL || buffer == NULL || buffer_size == 0 || operand_index >= instruction->operand_count ||
        instruction->family != XXBYTE_FAMILY_LUAJIT) {
        return 0;
    }
    memset(&s, 0, sizeof(s));
    s.buffer = buffer;
    s.capacity = buffer_size - 1u;
    s.ok = 1;
    if (!lj_operand_text(&s, names, f, instruction, &instruction->operands[operand_index]) || !s.ok || s.length == 0) {
        buffer[0] = '\0';
        return 0;
    }
    buffer[s.length] = '\0';
    return s.length;
}

int xxbyte_luajit_bl_comment(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const xxbyte_luajit_names *names = (const xxbyte_luajit_names *)fmt->context;
    const xxbyte_luajit_function *f = lj_current(names);
    const xxbyte_operand *a = NULL;
    const xxbyte_operand *cd = NULL;
    xx_buf_t kc;
    xx_buf_t ka;
    lj_sink s;
    lj_sink t;
    int has_kc = 0;
    int has_ka = 0;
    int ok;

    if (f == NULL || instruction->operand_count == 0) {
        return 1;
    }
    /* bc.lua: the text of the C or D operand, after the name of an
     * upvalue A: "ka ; kc", or ka alone. */
    if (XXBYTE_LUAJIT_MODES_A(xxbyte_luajit_operand_modes(instruction->id)) == XXBYTE_LUAJIT_OPERAND_UV) {
        a = &instruction->operands[0];
    }
    if (XXBYTE_LUAJIT_MODES_CD(xxbyte_luajit_operand_modes(instruction->id)) != XXBYTE_LUAJIT_OPERAND_NONE) {
        cd = &instruction->operands[instruction->operand_count - 1u];
    }
    xx_buf_init(&kc);
    xx_buf_init(&ka);
    memset(&s, 0, sizeof(s));
    s.out = &kc;
    s.ok = 1;
    t = s;
    t.out = &ka;
    if (cd != NULL && cd != a) {
        has_kc = lj_operand_text(&s, names, f, instruction, cd);
    }
    if (a != NULL) {
        has_ka = lj_uv_name(&t, names, f, (uint64_t)a->value);
    }
    ok = s.ok && t.ok;
    if (ok && (has_kc || has_ka)) {
        ok = xxbyte_fmt_str(fmt, " ; ");
        if (ok && has_ka) {
            ok = xx_buf_append(fmt->out, ka.data, ka.size) && (!has_kc || xxbyte_fmt_str(fmt, " ; "));
        }
        if (ok && has_kc) {
            ok = xx_buf_append(fmt->out, kc.data, kc.size);
        }
    }
    xx_buf_free(&kc);
    xx_buf_free(&ka);
    return ok;
}

/* -- Whole dumps ---------------------------------------------------------- */

xxbyte_status xxbyte_luajit_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_luajit_header header;
    xxbyte_luajit_function *functions = NULL;
    xxbyte_luajit_names names;
    xxbyte_luajit_kgc *kgc = NULL;
    xxbyte_luajit_knum *knum = NULL;
    xxbyte_luajit_string uv_names[256];
    uint32_t *children = NULL;
    uint32_t *first_child = NULL;
    uint32_t *next_sibling = NULL;
    uint32_t *stack = NULL;
    uint32_t *cursor = NULL;
    uint32_t *order = NULL;
    size_t count = 0;
    size_t emitted = 0;
    size_t depth = 0;
    uint32_t max_kgc = 0;
    uint32_t max_kn = 0;
    xxbyte_status status;
    size_t i;

    status = xxbyte_luajit_parse_header(data, size, &header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (mode == XXBYTE_MODE_AUTO) {
        mode = header.mode;
    }
    status = xxbyte_luajit_find_functions(data, size, &header, NULL, 0, &count);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    functions = (xxbyte_luajit_function *)xx_mem_calloc(count, sizeof(*functions));
    first_child = (uint32_t *)xx_mem_calloc(count, sizeof(*first_child));
    next_sibling = (uint32_t *)xx_mem_calloc(count, sizeof(*next_sibling));
    stack = (uint32_t *)xx_mem_calloc(count, sizeof(*stack));
    cursor = (uint32_t *)xx_mem_calloc(count, sizeof(*cursor));
    order = (uint32_t *)xx_mem_calloc(count, sizeof(*order));
    if (functions == NULL || first_child == NULL || next_sibling == NULL || stack == NULL || cursor == NULL || order == NULL) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    status = xxbyte_luajit_find_functions(data, size, &header, functions, count, &count);
    if (status != XXBYTE_STATUS_OK) {
        goto done;
    }
    /* Children in descending constant order: bc.lua walks funck(-1),
     * funck(-2), ..., the last dumped constant first. */
    for (i = 0; i < count; ++i) {
        first_child[i] = XXBYTE_LUAJIT_NO_PARENT;
        next_sibling[i] = XXBYTE_LUAJIT_NO_PARENT;
    }
    for (i = count; i-- > 0;) {
        /* A parent's first child constant takes the latest prototype, so
         * walking the prototypes backwards meets its children in
         * ascending constant order and each insertion is at the head. */
        uint32_t parent = functions[i].parent;
        uint32_t *link;
        if (parent == XXBYTE_LUAJIT_NO_PARENT || parent >= count) {
            continue;
        }
        link = &first_child[parent];
        while (*link != XXBYTE_LUAJIT_NO_PARENT && functions[*link].kgc_index > functions[i].kgc_index) {
            link = &next_sibling[*link];
        }
        next_sibling[i] = *link;
        *link = (uint32_t)i;
    }
    /* Post-order from the main chunk (the last prototype); the pairing
     * makes a tree, children before their parent in the dump. */
    for (i = 0; i < count; ++i) {
        cursor[i] = first_child[i];
    }
    stack[depth++] = (uint32_t)(count - 1u);
    while (depth > 0) {
        uint32_t top = stack[depth - 1u];
        uint32_t c = cursor[top];
        if (c != XXBYTE_LUAJIT_NO_PARENT) {
            cursor[top] = next_sibling[c];
            if (depth < count) {
                stack[depth++] = c;
            }
            continue;
        }
        if (emitted < count) {
            order[emitted++] = top;
        }
        --depth;
    }
    for (i = 0; i < count; ++i) {
        if (functions[i].proto.sizekgc > max_kgc) {
            max_kgc = functions[i].proto.sizekgc;
        }
        if (functions[i].proto.sizekn > max_kn) {
            max_kn = functions[i].proto.sizekn;
        }
    }
    kgc = (xxbyte_luajit_kgc *)xx_mem_calloc((size_t)max_kgc + 1u, sizeof(*kgc));
    knum = (xxbyte_luajit_knum *)xx_mem_calloc((size_t)max_kn + 1u, sizeof(*knum));
    children = (uint32_t *)xx_mem_calloc((size_t)max_kgc + 1u, sizeof(*children));
    if (kgc == NULL || knum == NULL || children == NULL) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    xxbyte_luajit_names_init(&names, data, size, &header, functions, count);
    for (i = 0; i < emitted && status == XXBYTE_STATUS_OK; ++i) {
        const xxbyte_luajit_function *f = &functions[order[i]];
        lj_sink s;
        size_t j;
        uint32_t c;

        names.function = order[i];
        status = xxbyte_luajit_get_kgc(data, size, &f->proto, 0, kgc, f->proto.sizekgc);
        if (status == XXBYTE_STATUS_OK) {
            status = xxbyte_luajit_get_knum(data, size, &f->proto, 0, knum, f->proto.sizekn);
        }
        if (status != XXBYTE_STATUS_OK) {
            break;
        }
        names.kgc = kgc;
        names.knum = knum;
        names.uv_names = NULL;
        if (f->proto.sizedbg != 0 && xxbyte_luajit_get_uv_names(data, size, &f->proto, uv_names, f->proto.sizeuv) == XXBYTE_STATUS_OK) {
            names.uv_names = uv_names;
        }
        for (j = 0; j < f->proto.sizekgc; ++j) {
            children[j] = XXBYTE_LUAJIT_NO_PARENT;
        }
        for (c = first_child[order[i]]; c != XXBYTE_LUAJIT_NO_PARENT; c = next_sibling[c]) {
            if (functions[c].kgc_index < f->proto.sizekgc) {
                children[functions[c].kgc_index] = c;
            }
        }
        names.children = children;
        memset(&s, 0, sizeof(s));
        s.out = out;
        s.ok = 1;
        if (i != 0) {
            lj_str(&s, "\n");
        }
        lj_str(&s, "-- BYTECODE -- ");
        lj_loc(&s, &names, f);
        lj_str(&s, "-");
        lj_int(&s, (long long)(int32_t)(f->proto.firstline + f->proto.numline));
        lj_str(&s, "\n");
        if (!s.ok) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_LUAJIT, mode, NULL, data + f->proto.bc_offset, (size_t)f->proto.sizebc * 4u, 4, listing_flags, xxbyte_luajit_resolve,
                                      &names, out);
    }
done:
    xx_mem_free(children);
    xx_mem_free(knum);
    xx_mem_free(kgc);
    xx_mem_free(order);
    xx_mem_free(cursor);
    xx_mem_free(stack);
    xx_mem_free(next_sibling);
    xx_mem_free(first_child);
    xx_mem_free(functions);
    return status;
}

#else
typedef int xxbyte_luajit_list_disabled;
#endif
