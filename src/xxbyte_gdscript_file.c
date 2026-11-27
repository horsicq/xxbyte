/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_gdscript_internal.h"
#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#if XXBYTE_USE_GDSCRIPT

/*
 * .gdc containers: the layouts of GDScriptTokenizerBuffer::set_code_buffer
 * and parse_code_string (4.3-stable, 4.7.2-stable, 3.0-stable,
 * 3.6.3-stable) and the Variant encoding of core/io/marshalls.cpp.
 */

#define MAGIC_SIZE 4u
#define V4_HEADER_SIZE 12u
#define V3_HEADER_SIZE 24u
#define MAX_DEPTH 1024u /* Variant::MAX_RECURSION_DEPTH */
#define MAX_DECOMPRESSED (256u * 1024u * 1024u)
#define FLAG_64 0x10000u /* HEADER_DATA_FLAG_64, ENCODE_FLAG_64 */
#define NODE_PATH_NEW 0x80000000u

static const uint8_t magic[MAGIC_SIZE] = {'G', 'D', 'S', 'C'};

static int is_v4(uint32_t mode)
{
    return XXBYTE_GDSCRIPT_MODE_IS_V4(mode) ? 1 : 0;
}

static uint32_t rd32(const uint8_t *data, size_t offset)
{
    return xxbyte_u32le(data + offset);
}

/* Bytes left at offset, end-bounded. */
static int have(size_t end, size_t offset, uint64_t count)
{
    return offset <= end && (uint64_t)(end - offset) >= count;
}

/* ------------------------------------------------------------------------ */
/* Variants. */

/* 4.x Variant::Type. */
enum {
    V4_NIL,
    V4_BOOL,
    V4_INT,
    V4_FLOAT,
    V4_STRING,
    V4_VECTOR2,
    V4_VECTOR2I,
    V4_RECT2,
    V4_RECT2I,
    V4_VECTOR3,
    V4_VECTOR3I,
    V4_TRANSFORM2D,
    V4_VECTOR4,
    V4_VECTOR4I,
    V4_PLANE,
    V4_QUATERNION,
    V4_AABB,
    V4_BASIS,
    V4_TRANSFORM3D,
    V4_PROJECTION,
    V4_COLOR,
    V4_STRING_NAME,
    V4_NODE_PATH,
    V4_RID,
    V4_OBJECT,
    V4_CALLABLE,
    V4_SIGNAL,
    V4_DICTIONARY,
    V4_ARRAY,
    V4_PACKED_BYTE_ARRAY,
    V4_PACKED_INT32_ARRAY,
    V4_PACKED_INT64_ARRAY,
    V4_PACKED_FLOAT32_ARRAY,
    V4_PACKED_FLOAT64_ARRAY,
    V4_PACKED_STRING_ARRAY,
    V4_PACKED_VECTOR2_ARRAY,
    V4_PACKED_VECTOR3_ARRAY,
    V4_PACKED_COLOR_ARRAY,
    V4_PACKED_VECTOR4_ARRAY,
    V4_COUNT
};

/* 3.x Variant::Type. */
enum {
    V3_NIL,
    V3_BOOL,
    V3_INT,
    V3_REAL,
    V3_STRING,
    V3_VECTOR2,
    V3_RECT2,
    V3_VECTOR3,
    V3_TRANSFORM2D,
    V3_PLANE,
    V3_QUAT,
    V3_AABB,
    V3_BASIS,
    V3_TRANSFORM,
    V3_COLOR,
    V3_NODE_PATH,
    V3_RID,
    V3_OBJECT,
    V3_DICTIONARY,
    V3_ARRAY,
    V3_POOL_BYTE_ARRAY,
    V3_POOL_INT_ARRAY,
    V3_POOL_REAL_ARRAY,
    V3_POOL_STRING_ARRAY,
    V3_POOL_VECTOR2_ARRAY,
    V3_POOL_VECTOR3_ARRAY,
    V3_POOL_COLOR_ARRAY,
    V3_COUNT
};

/* Display names and component counts of the fixed-size math types. */
typedef struct math_type {
    const char *name;
    uint8_t components;
    uint8_t integer; /* 32-bit integers: no 64-bit form */
    uint8_t float32; /* always float32 (Color) */
} math_type;

static const math_type *math_type_of(int v4, uint32_t type)
{
    static const math_type v4_math[V4_COUNT] = {
        [V4_VECTOR2] = {"Vector2", 2, 0, 0},
        [V4_VECTOR2I] = {"Vector2i", 2, 1, 0},
        [V4_RECT2] = {"Rect2", 4, 0, 0},
        [V4_RECT2I] = {"Rect2i", 4, 1, 0},
        [V4_VECTOR3] = {"Vector3", 3, 0, 0},
        [V4_VECTOR3I] = {"Vector3i", 3, 1, 0},
        [V4_TRANSFORM2D] = {"Transform2D", 6, 0, 0},
        [V4_VECTOR4] = {"Vector4", 4, 0, 0},
        [V4_VECTOR4I] = {"Vector4i", 4, 1, 0},
        [V4_PLANE] = {"Plane", 4, 0, 0},
        [V4_QUATERNION] = {"Quaternion", 4, 0, 0},
        [V4_AABB] = {"AABB", 6, 0, 0},
        [V4_BASIS] = {"Basis", 9, 0, 0},
        [V4_TRANSFORM3D] = {"Transform3D", 12, 0, 0},
        [V4_PROJECTION] = {"Projection", 16, 0, 0},
        [V4_COLOR] = {"Color", 4, 0, 1},
    };
    static const math_type v3_math[V3_COUNT] = {
        [V3_VECTOR2] = {"Vector2", 2, 0, 1},      [V3_RECT2] = {"Rect2", 4, 0, 1}, [V3_VECTOR3] = {"Vector3", 3, 0, 1}, [V3_TRANSFORM2D] = {"Transform2D", 6, 0, 1},
        [V3_PLANE] = {"Plane", 4, 0, 1},          [V3_QUAT] = {"Quat", 4, 0, 1},   [V3_AABB] = {"AABB", 6, 0, 1},       [V3_BASIS] = {"Basis", 9, 0, 1},
        [V3_TRANSFORM] = {"Transform", 12, 0, 1}, [V3_COLOR] = {"Color", 4, 0, 1},
    };
    const math_type *entry;

    if (v4) {
        if (type >= V4_COUNT) {
            return NULL;
        }
        entry = &v4_math[type];
    } else {
        if (type >= V3_COUNT) {
            return NULL;
        }
        entry = &v3_math[type];
    }
    return entry->name != NULL ? entry : NULL;
}

/* Size of a math type's payload. */
static uint32_t math_size(const math_type *math, uint32_t header)
{
    uint32_t width = (!math->integer && !math->float32 && (header & FLAG_64) != 0) ? 8u : 4u;

    return (uint32_t)math->components * width;
}

/* A marshalled String (u32 length, bytes, padding to 4): *text and *length
 * describe the bytes and *next the following offset. */
static xxbyte_status read_string(const uint8_t *data, size_t end, size_t offset, size_t *text, uint32_t *length, size_t *next)
{
    uint32_t count;
    uint64_t padded;

    if (!have(end, offset, 4)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    count = rd32(data, offset);
    if (count > 0x7FFFFFFFu) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    padded = ((uint64_t)count + 3u) & ~UINT64_C(3);
    if (!have(end, offset + 4u, padded)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *text = offset + 4u;
    *length = count;
    *next = offset + 4u + (size_t)padded;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status skip_string(const uint8_t *data, size_t end, size_t *offset)
{
    size_t text;
    uint32_t length;

    return read_string(data, end, *offset, &text, &length, offset);
}

/* Bytes of a packed array of count elements of element_size bytes. */
static xxbyte_status skip_elements(size_t end, size_t *offset, uint32_t count, uint32_t element_size)
{
    uint64_t bytes;

    if (count > 0x7FFFFFFFu) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    bytes = (uint64_t)count * element_size;
    if (bytes > 0x7FFFFFFFu) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!have(end, *offset, bytes)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *offset += (size_t)bytes;
    return XXBYTE_STATUS_OK;
}

/* 4.x typed-container type (_decode_container_type). */
static xxbyte_status skip_container_type(const uint8_t *data, size_t end, size_t *offset, uint32_t kind)
{
    switch (kind) {
        case 0: return XXBYTE_STATUS_OK;
        case 1:
            if (!have(end, *offset, 4)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            if (rd32(data, *offset) >= V4_COUNT) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            *offset += 4u;
            return XXBYTE_STATUS_OK;
        case 2:
        case 3: return skip_string(data, end, offset);
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
}

/*
 * Walks the Variant at *offset like decode_variant with p_allow_objects
 * false and advances *offset past it. *type receives its type.
 */
static xxbyte_status skip_variant(const uint8_t *data, size_t end, int v4, uint32_t depth, size_t *offset, uint32_t *type)
{
    uint32_t header;
    uint32_t t;
    uint32_t count;
    uint32_t i;
    size_t p = *offset;
    const math_type *math;
    xxbyte_status status;

    if (depth > MAX_DEPTH) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!have(end, p, 4)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    header = rd32(data, p);
    t = header & 0xFFu;
    if (t >= (v4 ? (uint32_t)V4_COUNT : (uint32_t)V3_COUNT)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    p += 4u;
    *type = t;
    math = math_type_of(v4, t);
    if (math != NULL) {
        uint32_t bytes = math_size(math, header);

        if (!have(end, p, bytes)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        *offset = p + bytes;
        return XXBYTE_STATUS_OK;
    }
    /* The scalar and string types have the same numbers in both. */
    switch (t) {
        case V4_NIL: *offset = p; return XXBYTE_STATUS_OK;
        case V4_BOOL:
            if (!have(end, p, 4)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            *offset = p + 4u;
            return XXBYTE_STATUS_OK;
        case V4_INT:
        case V4_FLOAT: {
            uint32_t bytes = (header & FLAG_64) != 0 ? 8u : 4u;

            if (!have(end, p, bytes)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            *offset = p + bytes;
            return XXBYTE_STATUS_OK;
        }
        case V4_STRING:
            status = skip_string(data, end, &p);
            if (status == XXBYTE_STATUS_OK) {
                *offset = p;
            }
            return status;
        default: break;
    }
    if (v4 ? t == V4_STRING_NAME : 0) {
        status = skip_string(data, end, &p);
        if (status == XXBYTE_STATUS_OK) {
            *offset = p;
        }
        return status;
    }
    if (t == (v4 ? (uint32_t)V4_NODE_PATH : (uint32_t)V3_NODE_PATH)) {
        uint32_t names;
        uint32_t subnames;
        uint64_t total;

        if (!have(end, p, 4)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        names = rd32(data, p);
        if ((names & NODE_PATH_NEW) == 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (!have(end, p, 12)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        names &= ~NODE_PATH_NEW;
        subnames = rd32(data, p + 4u);
        total = (uint64_t)names + subnames + ((rd32(data, p + 8u) & 2u) != 0 ? 1u : 0u);
        p += 12u;
        for (; total != 0; --total) {
            status = skip_string(data, end, &p);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
        }
        *offset = p;
        return XXBYTE_STATUS_OK;
    }
    if (t == (v4 ? (uint32_t)V4_RID : (uint32_t)V3_RID)) {
        if (v4) {
            if (!have(end, p, 8)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            p += 8u;
        }
        *offset = p;
        return XXBYTE_STATUS_OK;
    }
    if (t == (v4 ? (uint32_t)V4_OBJECT : (uint32_t)V3_OBJECT)) {
        /* Only an object encoded as its id is allowed. */
        if ((header & FLAG_64) == 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (!have(end, p, 8)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        *offset = p + 8u;
        return XXBYTE_STATUS_OK;
    }
    if (v4 && t == V4_CALLABLE) {
        *offset = p;
        return XXBYTE_STATUS_OK;
    }
    if (v4 && t == V4_SIGNAL) {
        status = skip_string(data, end, &p);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (!have(end, p, 8)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        *offset = p + 8u;
        return XXBYTE_STATUS_OK;
    }
    if (t == (v4 ? (uint32_t)V4_DICTIONARY : (uint32_t)V3_DICTIONARY) || t == (v4 ? (uint32_t)V4_ARRAY : (uint32_t)V3_ARRAY)) {
        int dictionary = t == (v4 ? (uint32_t)V4_DICTIONARY : (uint32_t)V3_DICTIONARY);
        uint32_t element_type;

        if (v4) {
            status = skip_container_type(data, end, &p, (header >> 16) & 3u);
            if (status == XXBYTE_STATUS_OK && dictionary) {
                status = skip_container_type(data, end, &p, (header >> 18) & 3u);
            }
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
        }
        if (!have(end, p, 4)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        count = rd32(data, p) & 0x7FFFFFFFu;
        p += 4u;
        for (i = 0; i < count; ++i) {
            status = skip_variant(data, end, v4, depth + 1u, &p, &element_type);
            if (status == XXBYTE_STATUS_OK && dictionary) {
                status = skip_variant(data, end, v4, depth + 1u, &p, &element_type);
            }
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
        }
        *offset = p;
        return XXBYTE_STATUS_OK;
    }
    /* Packed (pool) arrays. */
    if (!have(end, p, 4)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    count = rd32(data, p);
    p += 4u;
    if (t == (v4 ? (uint32_t)V4_PACKED_BYTE_ARRAY : (uint32_t)V3_POOL_BYTE_ARRAY)) {
        status = skip_elements(end, &p, count, 1);
        if (status == XXBYTE_STATUS_OK && (count & 3u) != 0) {
            /* The engine skips the padding without checking it is there. */
            p += 4u - (count & 3u);
            if (p > end) {
                return XXBYTE_STATUS_TRUNCATED;
            }
        }
    } else if (t == (v4 ? (uint32_t)V4_PACKED_STRING_ARRAY : (uint32_t)V3_POOL_STRING_ARRAY)) {
        status = XXBYTE_STATUS_OK;
        for (i = 0; i < (count & 0x7FFFFFFFu) && status == XXBYTE_STATUS_OK; ++i) {
            status = skip_string(data, end, &p);
        }
        if (count > 0x7FFFFFFFu) {
            status = XXBYTE_STATUS_OK;
        }
    } else {
        uint32_t element;
        int wide = (header & FLAG_64) != 0;

        if (v4) {
            switch (t) {
                case V4_PACKED_INT32_ARRAY:
                case V4_PACKED_FLOAT32_ARRAY: element = 4; break;
                case V4_PACKED_INT64_ARRAY:
                case V4_PACKED_FLOAT64_ARRAY: element = 8; break;
                case V4_PACKED_VECTOR2_ARRAY: element = wide ? 16u : 8u; break;
                case V4_PACKED_VECTOR3_ARRAY: element = wide ? 24u : 12u; break;
                case V4_PACKED_COLOR_ARRAY: element = 16; break;
                default: element = wide ? 32u : 16u; break; /* PACKED_VECTOR4_ARRAY */
            }
        } else {
            switch (t) {
                case V3_POOL_INT_ARRAY:
                case V3_POOL_REAL_ARRAY: element = 4; break;
                case V3_POOL_VECTOR2_ARRAY: element = 8; break;
                case V3_POOL_VECTOR3_ARRAY: element = 12; break;
                default: element = 16; break; /* POOL_COLOR_ARRAY */
            }
        }
        status = skip_elements(end, &p, count, element);
    }
    if (status == XXBYTE_STATUS_OK) {
        *offset = p;
    }
    return status;
}

/* ------------------------------------------------------------------------ */
/* Text. */

static int put_utf8(xx_buf_t *out, uint32_t cp)
{
    char b[4];
    size_t n;

    if (cp == 0 || (cp >= 0xD800u && cp <= 0xDFFFu) || cp > 0x10FFFFu) {
        cp = 0xFFFDu;
    }
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
    return xx_buf_append(out, b, n) ? 1 : 0;
}

/* Decodes one UTF-8 sequence at text[*i] (strict), or returns 0xFFFFFFFF
 * and skips one byte. */
static uint32_t next_utf8(const uint8_t *text, size_t length, size_t *i)
{
    uint8_t c = text[*i];
    uint32_t cp;
    size_t n;
    size_t k;

    if (c < 0x80u) {
        ++*i;
        return c;
    }
    if (c >= 0xC2u && c <= 0xDFu) {
        n = 1;
        cp = c & 0x1Fu;
    } else if (c >= 0xE0u && c <= 0xEFu) {
        n = 2;
        cp = c & 0x0Fu;
    } else if (c >= 0xF0u && c <= 0xF4u) {
        n = 3;
        cp = c & 0x07u;
    } else {
        ++*i;
        return 0xFFFFFFFFu;
    }
    if (length - *i <= n) {
        ++*i;
        return 0xFFFFFFFFu;
    }
    for (k = 1; k <= n; ++k) {
        uint8_t d = text[*i + k];

        if ((d & 0xC0u) != 0x80u) {
            ++*i;
            return 0xFFFFFFFFu;
        }
        cp = (cp << 6) | (d & 0x3Fu);
    }
    if ((n == 2 && cp < 0x800u) || (n == 3 && (cp < 0x10000u || cp > 0x10FFFFu)) || (cp >= 0xD800u && cp <= 0xDFFFu)) {
        ++*i;
        return 0xFFFFFFFFu;
    }
    *i += n + 1;
    return cp;
}

/* A GDScript string literal body, escaped as the tokenizers read it. */
static int put_quoted(xx_buf_t *out, const uint8_t *text, size_t length)
{
    size_t i = 0;

    if (!xx_buf_append_char(out, '"')) {
        return 0;
    }
    while (i < length) {
        uint32_t cp = next_utf8(text, length, &i);
        int ok;

        switch (cp) {
            case '"': ok = xx_buf_append_str(out, "\\\""); break;
            case '\\': ok = xx_buf_append_str(out, "\\\\"); break;
            case '\n': ok = xx_buf_append_str(out, "\\n"); break;
            case '\t': ok = xx_buf_append_str(out, "\\t"); break;
            case '\r': ok = xx_buf_append_str(out, "\\r"); break;
            case 0xFFFFFFFFu: ok = xx_buf_append_str(out, "\\uFFFD"); break;
            default:
                if (cp < 0x20u || cp == 0x7Fu) {
                    ok = xx_buf_appendf(out, "\\u%04X", (unsigned)cp);
                } else {
                    ok = put_utf8(out, cp);
                }
                break;
        }
        if (!ok) {
            return 0;
        }
    }
    return xx_buf_append_char(out, '"') ? 1 : 0;
}

/* A double that reads back as a float literal ("1.0", not "1"). */
static int put_real(xx_buf_t *out, double value)
{
    xxbyte_fmt fmt;
    size_t start = out->size;
    size_t i;

    if (value != value) {
        return xx_buf_append_str(out, "NAN") ? 1 : 0;
    }
    if (value > 1.7976931348623157e308 || value < -1.7976931348623157e308) {
        return xx_buf_append_str(out, value < 0 ? "-INF" : "INF") ? 1 : 0;
    }
    fmt.out = out;
    fmt.flags = 0;
    fmt.resolver = NULL;
    fmt.context = NULL;
    if (!xxbyte_fmt_double(&fmt, value) || !xx_buf_ok(out)) {
        return 0;
    }
    for (i = start; i < out->size; ++i) {
        if (out->data[i] == '.' || out->data[i] == 'e') {
            return 1;
        }
    }
    return xx_buf_append_str(out, ".0") ? 1 : 0;
}

static double read_f32(const uint8_t *p)
{
    uint32_t bits = xxbyte_u32le(p);
    float value;

    memcpy(&value, &bits, sizeof(value));
    return (double)value;
}

static double read_f64(const uint8_t *p)
{
    uint64_t bits = xxbyte_u64le(p);
    double value;

    memcpy(&value, &bits, sizeof(value));
    return value;
}

/* Appends the source text of the Variant at offset (already walked). */
static int put_variant_unchecked(xx_buf_t *out, const uint8_t *data, size_t end, int v4, size_t offset)
{
    uint32_t header = rd32(data, offset);
    uint32_t t = header & 0xFFu;
    size_t p = offset + 4u;
    const math_type *math = math_type_of(v4, t);
    size_t text;
    uint32_t length;
    size_t next;

    if (math != NULL) {
        uint32_t i;
        int wide = !math->integer && !math->float32 && (header & FLAG_64) != 0;

        if (!xx_buf_append_str(out, math->name) || !xx_buf_append_char(out, '(')) {
            return 0;
        }
        for (i = 0; i < math->components; ++i) {
            int ok;

            if (i != 0 && !xx_buf_append_str(out, ", ")) {
                return 0;
            }
            if (math->integer) {
                ok = xx_buf_appendf(out, "%d", (int)(int32_t)rd32(data, p + 4u * i));
            } else if (wide) {
                ok = put_real(out, read_f64(data + p + 8u * i));
            } else {
                ok = put_real(out, read_f32(data + p + 4u * i));
            }
            if (!ok) {
                return 0;
            }
        }
        return xx_buf_append_char(out, ')') ? 1 : 0;
    }
    switch (t) {
        case V4_NIL: return xx_buf_append_str(out, "null") ? 1 : 0;
        case V4_BOOL: return xx_buf_append_str(out, rd32(data, p) != 0 ? "true" : "false") ? 1 : 0;
        case V4_INT:
            if ((header & FLAG_64) != 0) {
                return xx_buf_appendf(out, "%lld", (long long)(int64_t)xxbyte_u64le(data + p)) ? 1 : 0;
            }
            return xx_buf_appendf(out, "%d", (int)(int32_t)rd32(data, p)) ? 1 : 0;
        case V4_FLOAT: return put_real(out, (header & FLAG_64) != 0 ? read_f64(data + p) : read_f32(data + p));
        case V4_STRING:
            if (read_string(data, end, p, &text, &length, &next) != XXBYTE_STATUS_OK) {
                return xx_buf_append_str(out, "<bad>") ? 1 : 0;
            }
            return put_quoted(out, data + text, length);
        default: break;
    }
    if (v4 && t == V4_STRING_NAME) {
        if (read_string(data, end, p, &text, &length, &next) != XXBYTE_STATUS_OK) {
            return xx_buf_append_str(out, "<bad>") ? 1 : 0;
        }
        return xx_buf_append_char(out, '&') && put_quoted(out, data + text, length);
    }
    if (t == (v4 ? (uint32_t)V4_NODE_PATH : (uint32_t)V3_NODE_PATH)) {
        /* NodePath::operator String: "/" when absolute, the names joined by
         * "/", then ":" and each subname. The text goes through one
         * quoting pass, so it is assembled first. */
        uint32_t names = rd32(data, p) & ~NODE_PATH_NEW;
        uint32_t subnames = rd32(data, p + 4u);
        uint32_t flags = rd32(data, p + 8u);
        uint64_t total = (uint64_t)names + subnames + ((flags & 2u) != 0 ? 1u : 0u);
        uint64_t i;
        xx_buf_t path;
        int ok;

        xx_buf_init(&path);
        if ((flags & 1u) != 0) {
            xx_buf_append_char(&path, '/');
        }
        p += 12u;
        for (i = 0; i < total; ++i) {
            if (read_string(data, end, p, &text, &length, &next) != XXBYTE_STATUS_OK) {
                xx_buf_free(&path);
                return xx_buf_append_str(out, "<bad>") ? 1 : 0;
            }
            if (i >= names) {
                xx_buf_append_char(&path, ':');
            } else if (i != 0) {
                xx_buf_append_char(&path, '/');
            }
            xx_buf_append(&path, data + text, length);
            p = next;
        }
        ok = xx_buf_ok(&path) && xx_buf_append_str(out, v4 ? "^" : "@") && put_quoted(out, (const uint8_t *)path.data, path.size);
        xx_buf_free(&path);
        return ok;
    }
    if (t == (v4 ? (uint32_t)V4_RID : (uint32_t)V3_RID)) {
        if (v4) {
            return xx_buf_appendf(out, "RID(%llu)", (unsigned long long)xxbyte_u64le(data + p)) ? 1 : 0;
        }
        return xx_buf_append_str(out, "RID()") ? 1 : 0;
    }
    if (t == (v4 ? (uint32_t)V4_OBJECT : (uint32_t)V3_OBJECT)) {
        return xx_buf_appendf(out, "Object(%llu)", (unsigned long long)xxbyte_u64le(data + p)) ? 1 : 0;
    }
    if (v4 && t == V4_CALLABLE) {
        return xx_buf_append_str(out, "Callable()") ? 1 : 0;
    }
    if (v4 && t == V4_SIGNAL) {
        if (read_string(data, end, p, &text, &length, &next) != XXBYTE_STATUS_OK) {
            return xx_buf_append_str(out, "<bad>") ? 1 : 0;
        }
        if (!have(end, next, 8)) {
            return xx_buf_append_str(out, "<bad>") ? 1 : 0;
        }
        return xx_buf_append_str(out, "Signal(") && put_quoted(out, data + text, length) && xx_buf_appendf(out, ", %llu)", (unsigned long long)xxbyte_u64le(data + next));
    }
    {
        /* Containers and packed arrays: the type and the element count. */
        static const char *const v4_names[] = {"Dictionary",         "Array",
                                               "PackedByteArray",    "PackedInt32Array",
                                               "PackedInt64Array",   "PackedFloat32Array",
                                               "PackedFloat64Array", "PackedStringArray",
                                               "PackedVector2Array", "PackedVector3Array",
                                               "PackedColorArray",   "PackedVector4Array"};
        static const char *const v3_names[] = {
            "Dictionary", "Array", "PoolByteArray", "PoolIntArray", "PoolRealArray", "PoolStringArray", "PoolVector2Array", "PoolVector3Array", "PoolColorArray"};
        const char *name;
        uint32_t count;

        if (v4) {
            name = t >= V4_DICTIONARY ? v4_names[t - V4_DICTIONARY] : "?";
            if (t == V4_DICTIONARY || t == V4_ARRAY) {
                size_t q = p;

                /* The container types were walked before. */
                if (skip_container_type(data, end, &q, (header >> 16) & 3u) != XXBYTE_STATUS_OK ||
                    (t == V4_DICTIONARY && skip_container_type(data, end, &q, (header >> 18) & 3u) != XXBYTE_STATUS_OK)) {
                    return xx_buf_append_str(out, "<bad>") ? 1 : 0;
                }
                p = q;
            }
        } else {
            name = t >= V3_DICTIONARY ? v3_names[t - V3_DICTIONARY] : "?";
        }
        if (!have(end, p, 4)) {
            return xx_buf_append_str(out, "<bad>") ? 1 : 0;
        }
        count = rd32(data, p);
        if (t == (v4 ? (uint32_t)V4_DICTIONARY : (uint32_t)V3_DICTIONARY) || t == (v4 ? (uint32_t)V4_ARRAY : (uint32_t)V3_ARRAY)) {
            count &= 0x7FFFFFFFu;
        }
        return xx_buf_appendf(out, "%s(%u)", name, (unsigned)count) ? 1 : 0;
    }
}

/* put_variant_unchecked after checking the fixed fields it reads; a
 * Variant that is cut short prints as "<bad>". Never reads past end. */
static int put_variant(xx_buf_t *out, const uint8_t *data, size_t end, int v4, size_t offset)
{
    uint32_t header;
    uint32_t t;
    const math_type *math;
    uint64_t need;

    if (!have(end, offset, 4)) {
        return xx_buf_append_str(out, "<bad>") ? 1 : 0;
    }
    header = rd32(data, offset);
    t = header & 0xFFu;
    if (t >= (v4 ? (uint32_t)V4_COUNT : (uint32_t)V3_COUNT)) {
        return xx_buf_append_str(out, "<bad>") ? 1 : 0;
    }
    math = math_type_of(v4, t);
    if (math != NULL) {
        need = math_size(math, header);
    } else if (t == V4_NIL || (v4 && t == V4_CALLABLE) || (!v4 && t == V3_RID) || t == V4_STRING || (v4 && t == V4_STRING_NAME) || (!v4 && t == V3_DICTIONARY) ||
               (!v4 && t == V3_ARRAY) || (v4 && (t == V4_DICTIONARY || t == V4_ARRAY || t == V4_SIGNAL))) {
        need = 0; /* bounded where they are read */
    } else if (t == V4_BOOL) {
        need = 4;
    } else if (t == V4_INT || t == V4_FLOAT) {
        need = (header & FLAG_64) != 0 ? 8u : 4u;
    } else if (t == (v4 ? (uint32_t)V4_NODE_PATH : (uint32_t)V3_NODE_PATH)) {
        need = 12;
    } else if ((v4 && t == V4_RID) || t == (v4 ? (uint32_t)V4_OBJECT : (uint32_t)V3_OBJECT)) {
        need = 8;
    } else {
        need = 4; /* packed array count */
    }
    if (!have(end, offset + 4u, need)) {
        return xx_buf_append_str(out, "<bad>") ? 1 : 0;
    }
    return put_variant_unchecked(out, data, end, v4, offset);
}

/* ------------------------------------------------------------------------ */
/* Parsing. */

static uint32_t auto_mode(uint32_t version)
{
    switch (version) {
        case 101: return XXBYTE_GDSCRIPT_MODE_V4_5;
        case 100: return XXBYTE_GDSCRIPT_MODE_V4_3;
        case 13: return XXBYTE_GDSCRIPT_MODE_V3_5;
        case 12: return XXBYTE_GDSCRIPT_MODE_V3_0;
        default: return XXBYTE_MODE_AUTO;
    }
}

/* Offset of the entry after the identifier at offset. */
static xxbyte_status skip_identifier(const uint8_t *data, size_t end, int v4, size_t *offset)
{
    uint64_t bytes;

    if (!have(end, *offset, 4)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    bytes = rd32(data, *offset);
    if (v4) {
        bytes *= 4u;
    }
    if (!have(end, *offset + 4u, bytes)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *offset += 4u + (size_t)bytes;
    return XXBYTE_STATUS_OK;
}

static uint32_t token_size_at(const uint8_t *data, size_t offset, int v4)
{
    if ((data[offset] & 0x80u) != 0) {
        return v4 ? 8u : 4u;
    }
    return v4 ? 5u : 1u;
}

xxbyte_status xxbyte_gdscript_parse(const uint8_t *data, size_t size, uint32_t mode, xxbyte_gdscript_file *file)
{
    xxbyte_gdscript_file f;
    size_t end;
    size_t p;
    uint32_t i;
    uint32_t type;
    int v4;
    xxbyte_status status;

    if (file != NULL) {
        memset(file, 0, sizeof(*file));
    }
    if (file == NULL || (data == NULL && size != 0) || (mode != XXBYTE_MODE_AUTO && mode > XXBYTE_GDSCRIPT_MODE_V3_0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(&f, 0, sizeof(f));
    if (memcmp(data == NULL ? magic : data, magic, size < MAGIC_SIZE ? size : MAGIC_SIZE) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (size < 8) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (size > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    end = size;
    f.version = rd32(data, 4);
    if (mode == XXBYTE_MODE_AUTO) {
        mode = auto_mode(f.version);
        if (mode == XXBYTE_MODE_AUTO) {
            file->version = f.version;
            return XXBYTE_STATUS_UNSUPPORTED_MODE;
        }
    }
    if (mode == XXBYTE_GDSCRIPT_MODE_DEFAULT) {
        mode = XXBYTE_GDSCRIPT_MODE_V4_5;
    }
    f.mode = mode;
    v4 = is_v4(mode);
    if (v4) {
        uint32_t counts = mode == XXBYTE_GDSCRIPT_MODE_V4_3 ? 20u : 16u;

        if (size < V4_HEADER_SIZE) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        f.decompressed_size = rd32(data, 8);
        if (f.decompressed_size != 0) {
            file->version = f.version;
            file->mode = mode;
            file->decompressed_size = f.decompressed_size;
            return XXBYTE_STATUS_UNSUPPORTED_MODE;
        }
        if (!have(end, V4_HEADER_SIZE, counts)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        f.identifier_count = rd32(data, 12);
        f.constant_count = rd32(data, 16);
        f.line_count = rd32(data, 20);
        f.token_count = rd32(data, V4_HEADER_SIZE + counts - 4u);
        p = V4_HEADER_SIZE + counts;
    } else {
        if (size < V3_HEADER_SIZE) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        f.identifier_count = rd32(data, 8);
        f.constant_count = rd32(data, 12);
        f.line_count = rd32(data, 16);
        f.token_count = rd32(data, 20);
        p = V3_HEADER_SIZE;
    }
    f.identifier_offset = (uint32_t)p;
    for (i = 0; i < f.identifier_count; ++i) {
        status = skip_identifier(data, end, v4, &p);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    f.constant_offset = (uint32_t)p;
    for (i = 0; i < f.constant_count; ++i) {
        status = skip_variant(data, end, v4, 0, &p, &type);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    f.line_offset = (uint32_t)p;
    if (!have(end, p, (uint64_t)f.line_count * (v4 ? 16u : 8u))) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    p += (size_t)f.line_count * 8u;
    if (v4) {
        f.column_offset = (uint32_t)p;
        p += (size_t)f.line_count * 8u;
    }
    f.token_offset = (uint32_t)p;
    for (i = 0; i < f.token_count; ++i) {
        uint32_t n;

        if (p >= end) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        n = token_size_at(data, p, v4);
        if (!have(end, p, n)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        p += n;
    }
    f.token_size = (uint32_t)(p - f.token_offset);
    f.trailing_size = (uint32_t)(end - p);
    *file = f;
    return XXBYTE_STATUS_OK;
}

/* Checks that file was parsed from data[0..size). */
static int file_ok(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file)
{
    return file != NULL && (data != NULL || size == 0) && file->mode <= XXBYTE_GDSCRIPT_MODE_V3_0 && file->mode != XXBYTE_GDSCRIPT_MODE_DEFAULT &&
           file->decompressed_size == 0 && (uint64_t)file->token_offset + file->token_size + file->trailing_size == size &&
           file->identifier_offset <= file->constant_offset && file->constant_offset <= file->line_offset && file->line_offset <= file->token_offset;
}

xxbyte_status xxbyte_gdscript_table_offsets(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, xxbyte_gdscript_table table, uint32_t *offsets,
                                            size_t capacity, size_t *count)
{
    size_t p;
    size_t end;
    uint32_t n;
    uint32_t i;
    uint32_t type;
    int v4;
    xxbyte_status status = XXBYTE_STATUS_OK;

    if (count != NULL) {
        *count = 0;
    }
    if (count == NULL || (offsets == NULL && capacity != 0) || !file_ok(data, size, file)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    v4 = is_v4(file->mode);
    switch (table) {
        case XXBYTE_GDSCRIPT_TABLE_IDENTIFIER:
            p = file->identifier_offset;
            end = file->constant_offset;
            n = file->identifier_count;
            break;
        case XXBYTE_GDSCRIPT_TABLE_CONSTANT:
            p = file->constant_offset;
            end = file->line_offset;
            n = file->constant_count;
            break;
        case XXBYTE_GDSCRIPT_TABLE_TOKEN:
            p = file->token_offset;
            end = (size_t)file->token_offset + file->token_size;
            n = file->token_count;
            break;
        default: return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0; i < n; ++i) {
        if (i < capacity) {
            offsets[i] = (uint32_t)p;
        }
        if (table == XXBYTE_GDSCRIPT_TABLE_IDENTIFIER) {
            status = skip_identifier(data, end, v4, &p);
        } else if (table == XXBYTE_GDSCRIPT_TABLE_CONSTANT) {
            status = skip_variant(data, end, v4, 0, &p, &type);
        } else if (p < end && have(end, p, token_size_at(data, p, v4))) {
            p += token_size_at(data, p, v4);
        } else {
            status = XXBYTE_STATUS_TRUNCATED;
        }
        if (status != XXBYTE_STATUS_OK) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
    }
    *count = n;
    return XXBYTE_STATUS_OK;
}

/* Offset of entry index of a table, walking from its start. */
static xxbyte_status entry_offset(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, xxbyte_gdscript_table table, uint32_t index, size_t *offset)
{
    size_t p;
    size_t end;
    uint32_t i;
    uint32_t type;
    int v4;
    xxbyte_status status;

    if (!file_ok(data, size, file)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    v4 = is_v4(file->mode);
    if (table == XXBYTE_GDSCRIPT_TABLE_IDENTIFIER) {
        if (index >= file->identifier_count) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
        p = file->identifier_offset;
        end = file->constant_offset;
        for (i = 0; i < index; ++i) {
            status = skip_identifier(data, end, v4, &p);
            if (status != XXBYTE_STATUS_OK) {
                return XXBYTE_STATUS_INVALID_ARGUMENT;
            }
        }
    } else if (table == XXBYTE_GDSCRIPT_TABLE_CONSTANT) {
        if (index >= file->constant_count) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
        p = file->constant_offset;
        end = file->line_offset;
        for (i = 0; i < index; ++i) {
            status = skip_variant(data, end, v4, 0, &p, &type);
            if (status != XXBYTE_STATUS_OK) {
                return XXBYTE_STATUS_INVALID_ARGUMENT;
            }
        }
    } else {
        if (index >= file->token_count) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
        p = file->token_offset;
        end = (size_t)file->token_offset + file->token_size;
        for (i = 0; i < index; ++i) {
            if (p >= end) {
                return XXBYTE_STATUS_INVALID_ARGUMENT;
            }
            p += token_size_at(data, p, v4);
        }
    }
    *offset = p;
    return XXBYTE_STATUS_OK;
}

/* Appends identifier text at offset (in the identifier table). */
static int put_identifier(xx_buf_t *out, const uint8_t *data, int v4, size_t offset)
{
    uint32_t length = rd32(data, offset);
    const uint8_t *p = data + offset + 4u;
    uint32_t i;

    if (v4) {
        for (i = 0; i < length; ++i) {
            uint32_t cp = (uint32_t)(p[4u * i] ^ 0xB6u) | ((uint32_t)(p[4u * i + 1u] ^ 0xB6u) << 8) | ((uint32_t)(p[4u * i + 2u] ^ 0xB6u) << 16) |
                          ((uint32_t)(p[4u * i + 3u] ^ 0xB6u) << 24);

            if (!put_utf8(out, cp)) {
                return 0;
            }
        }
        return 1;
    }
    /* The engine NUL-terminates the last byte and reads up to the first
     * NUL. */
    for (i = 0; i + 1u < length; ++i) {
        uint8_t c = (uint8_t)(p[i] ^ 0xB6u);

        if (c == 0) {
            break;
        }
        if (!xx_buf_append_char(out, (char)c)) {
            return 0;
        }
    }
    return 1;
}

/* Copies text into buffer, truncating at a UTF-8 boundary. Returns the
 * copied length. */
static size_t copy_out(const xx_buf_t *text, char *buffer, size_t buffer_size)
{
    size_t n = text->size;

    if (buffer_size == 0) {
        return 0;
    }
    if (n > buffer_size - 1u) {
        n = buffer_size - 1u;
        while (n > 0 && ((uint8_t)text->data[n] & 0xC0u) == 0x80u) {
            --n;
        }
    }
    if (n != 0) {
        memcpy(buffer, text->data, n);
    }
    buffer[n] = '\0';
    return n;
}

xxbyte_status xxbyte_gdscript_identifier(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, uint32_t index, char *buffer, size_t buffer_size,
                                         size_t *length)
{
    size_t offset;
    xx_buf_t text;
    xxbyte_status status;

    if (length != NULL) {
        *length = 0;
    }
    if (buffer_size != 0 && buffer != NULL) {
        buffer[0] = '\0';
    }
    if (length == NULL || (buffer == NULL && buffer_size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = entry_offset(data, size, file, XXBYTE_GDSCRIPT_TABLE_IDENTIFIER, index, &offset);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    xx_buf_init(&text);
    if (!put_identifier(&text, data, is_v4(file->mode), offset) || !xx_buf_ok(&text)) {
        xx_buf_free(&text);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    *length = text.size;
    copy_out(&text, buffer, buffer_size);
    xx_buf_free(&text);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_gdscript_constant(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, uint32_t index, char *buffer, size_t buffer_size,
                                       size_t *length, uint32_t *type)
{
    size_t offset;
    xx_buf_t text;
    xxbyte_status status;

    if (length != NULL) {
        *length = 0;
    }
    if (type != NULL) {
        *type = 0;
    }
    if (buffer_size != 0 && buffer != NULL) {
        buffer[0] = '\0';
    }
    if (length == NULL || (buffer == NULL && buffer_size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = entry_offset(data, size, file, XXBYTE_GDSCRIPT_TABLE_CONSTANT, index, &offset);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    xx_buf_init(&text);
    if (!put_variant(&text, data, file->line_offset, is_v4(file->mode), offset) || !xx_buf_ok(&text)) {
        xx_buf_free(&text);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (type != NULL) {
        *type = rd32(data, offset) & 0xFFu;
    }
    *length = text.size;
    copy_out(&text, buffer, buffer_size);
    xx_buf_free(&text);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_gdscript_token_line(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, uint32_t index, uint32_t *line, uint32_t *column,
                                         int *starts_line)
{
    uint32_t i;
    uint32_t found_line = 0;
    uint32_t found_column = 0;
    int starts = 0;

    if (line != NULL) {
        *line = 0;
    }
    if (column != NULL) {
        *column = 0;
    }
    if (starts_line != NULL) {
        *starts_line = 0;
    }
    if (!file_ok(data, size, file) || index >= file->token_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (is_v4(file->mode)) {
        size_t offset;
        xxbyte_status status = entry_offset(data, size, file, XXBYTE_GDSCRIPT_TABLE_TOKEN, index, &offset);

        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        found_line = rd32(data, offset + token_size_at(data, offset, 1) - 4u);
        /* HashMap semantics: the last pair for a token wins. */
        for (i = 0; i < file->line_count; ++i) {
            if (rd32(data, (size_t)file->line_offset + 8u * i) == index) {
                starts = 1;
            }
            if (rd32(data, (size_t)file->column_offset + 8u * i) == index) {
                found_column = rd32(data, (size_t)file->column_offset + 8u * i + 4u);
            }
        }
        if (!starts) {
            found_column = 0;
        }
    } else {
        uint32_t best = 0;
        int any = 0;

        for (i = 0; i < file->line_count; ++i) {
            uint32_t token = rd32(data, (size_t)file->line_offset + 8u * i);

            if (token <= index && (!any || token >= best)) {
                best = token;
                any = 1;
                found_line = rd32(data, (size_t)file->line_offset + 8u * i + 4u) & 0xFFFFFFu;
                found_column = rd32(data, (size_t)file->line_offset + 8u * i + 4u) >> 24;
                starts = token == index;
            }
        }
    }
    if (line != NULL) {
        *line = found_line;
    }
    if (column != NULL) {
        *column = found_column;
    }
    if (starts_line != NULL) {
        *starts_line = starts;
    }
    return XXBYTE_STATUS_OK;
}

/* ------------------------------------------------------------------------ */
/* Resolver. */

typedef struct name_lookup {
    const uint8_t *data;
    size_t size;
    const xxbyte_gdscript_file *file;
    const uint32_t *identifier_offsets;
    const uint32_t *constant_offsets;
} name_lookup;

/* Appends the resolved text of an identifier or constant index; returns 1,
 * 0 when there is nothing (index past the table), -1 on failure. */
static int lookup_text(const name_lookup *names, int constant, uint64_t index, xx_buf_t *out)
{
    const xxbyte_gdscript_file *file = names->file;
    size_t offset;
    int v4 = is_v4(file->mode);

    if (constant) {
        if (index >= file->constant_count) {
            return 0;
        }
        if (names->constant_offsets != NULL) {
            offset = names->constant_offsets[index];
            if (offset < file->constant_offset || offset >= file->line_offset) {
                return 0;
            }
        } else if (entry_offset(names->data, names->size, file, XXBYTE_GDSCRIPT_TABLE_CONSTANT, (uint32_t)index, &offset) != XXBYTE_STATUS_OK) {
            return 0;
        }
        return put_variant(out, names->data, file->line_offset, v4, offset) ? 1 : -1;
    }
    if (index >= file->identifier_count) {
        return 0;
    }
    if (names->identifier_offsets != NULL) {
        size_t check;

        offset = names->identifier_offsets[index];
        check = offset;
        if (offset < file->identifier_offset || skip_identifier(names->data, file->constant_offset, v4, &check) != XXBYTE_STATUS_OK) {
            return 0;
        }
    } else if (entry_offset(names->data, names->size, file, XXBYTE_GDSCRIPT_TABLE_IDENTIFIER, (uint32_t)index, &offset) != XXBYTE_STATUS_OK) {
        return 0;
    }
    return put_identifier(out, names->data, v4, offset) ? 1 : -1;
}

size_t xxbyte_gdscript_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_gdscript_names *names = (const xxbyte_gdscript_names *)context;
    const xxbyte_operand *operand;
    name_lookup lookup;
    xx_buf_t text;
    size_t n = 0;
    int got;

    if (names == NULL || instruction == NULL || buffer == NULL || buffer_size == 0 || operand_index >= instruction->operand_count ||
        operand_index >= XXBYTE_MAX_OPERANDS || !file_ok(names->data, names->size, names->file)) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (operand->type != XXBYTE_OPERAND_INDEX || (operand->kind != XXBYTE_INDEX_NAME && operand->kind != XXBYTE_INDEX_CONSTANT)) {
        return 0;
    }
    lookup.data = names->data;
    lookup.size = names->size;
    lookup.file = names->file;
    lookup.identifier_offsets = names->identifier_offsets;
    lookup.constant_offsets = names->constant_offsets;
    xx_buf_init(&text);
    got = lookup_text(&lookup, operand->kind == XXBYTE_INDEX_CONSTANT, (uint64_t)operand->value, &text);
    if (got > 0 && xx_buf_ok(&text) && text.size != 0) {
        n = copy_out(&text, buffer, buffer_size);
    }
    xx_buf_free(&text);
    return n;
}

/* ------------------------------------------------------------------------ */
/* Source reconstruction. */

typedef struct token_view {
    uint32_t id;
    uint32_t value; /* identifier/constant/type/function index or indent */
    uint32_t line;  /* 4.x */
} token_view;

static void view_token(const uint8_t *data, size_t offset, uint32_t mode, token_view *view)
{
    xxbyte_instruction insn;
    size_t n = token_size_at(data, offset, is_v4(mode));

    memset(view, 0, sizeof(*view));
    if (xxbyte_decode(XXBYTE_FAMILY_GDSCRIPT, mode, data + offset, n, 0, &insn) != XXBYTE_STATUS_OK) {
        return;
    }
    view->id = insn.id;
    if (insn.operand_count != 0 && insn.operands[0].type == XXBYTE_OPERAND_INDEX) {
        view->value = (uint32_t)insn.operands[0].value;
    } else if (insn.operand_count != 0 && (insn.operands[0].flags & XXBYTE_GDSCRIPT_OPERAND_FLAG_INDENT) != 0) {
        view->value = (uint32_t)insn.operands[0].value;
    }
    if (is_v4(mode)) {
        view->line = rd32(data, offset + n - 4u);
    }
}

static int is_numeric_literal(const uint8_t *data, const xxbyte_gdscript_file *file, const uint32_t *constant_offsets, const token_view *t)
{
    uint32_t type;

    if (t->id != XXBYTE_GDSCRIPT_ID_LITERAL && t->id != XXBYTE_GDSCRIPT_ID_V3_CONSTANT) {
        return 0;
    }
    if (t->value >= file->constant_count) {
        return 0;
    }
    type = rd32(data, constant_offsets[t->value]) & 0xFFu;
    return type == V4_INT || type == V4_FLOAT;
}

/* Tokens that end an operand: a binary operator may follow them. */
static int ends_operand(uint32_t id)
{
    switch (id) {
        case XXBYTE_GDSCRIPT_ID_IDENTIFIER:
        case XXBYTE_GDSCRIPT_ID_LITERAL:
        case XXBYTE_GDSCRIPT_ID_PARENTHESIS_CLOSE:
        case XXBYTE_GDSCRIPT_ID_BRACKET_CLOSE:
        case XXBYTE_GDSCRIPT_ID_BRACE_CLOSE:
        case XXBYTE_GDSCRIPT_ID_SELF:
        case XXBYTE_GDSCRIPT_ID_CONST_PI:
        case XXBYTE_GDSCRIPT_ID_CONST_TAU:
        case XXBYTE_GDSCRIPT_ID_CONST_INF:
        case XXBYTE_GDSCRIPT_ID_CONST_NAN:
        case XXBYTE_GDSCRIPT_ID_UNDERSCORE:
        case XXBYTE_GDSCRIPT_ID_V3_IDENTIFIER:
        case XXBYTE_GDSCRIPT_ID_V3_CONSTANT:
        case XXBYTE_GDSCRIPT_ID_V3_BUILT_IN_TYPE:
        case XXBYTE_GDSCRIPT_ID_V3_PARENTHESIS_CLOSE:
        case XXBYTE_GDSCRIPT_ID_V3_BRACKET_CLOSE:
        case XXBYTE_GDSCRIPT_ID_V3_CURLY_BRACKET_CLOSE:
        case XXBYTE_GDSCRIPT_ID_V3_SELF:
        case XXBYTE_GDSCRIPT_ID_V3_CONST_PI:
        case XXBYTE_GDSCRIPT_ID_V3_CONST_TAU:
        case XXBYTE_GDSCRIPT_ID_V3_CONST_INF:
        case XXBYTE_GDSCRIPT_ID_V3_CONST_NAN:
        case XXBYTE_GDSCRIPT_ID_V3_WILDCARD: return 1;
        default: return 0;
    }
}

/* Tokens that start an operand. */
static int starts_operand(uint32_t id)
{
    switch (id) {
        case XXBYTE_GDSCRIPT_ID_IDENTIFIER:
        case XXBYTE_GDSCRIPT_ID_LITERAL:
        case XXBYTE_GDSCRIPT_ID_PARENTHESIS_OPEN:
        case XXBYTE_GDSCRIPT_ID_BRACKET_OPEN:
        case XXBYTE_GDSCRIPT_ID_SELF:
        case XXBYTE_GDSCRIPT_ID_CONST_PI:
        case XXBYTE_GDSCRIPT_ID_CONST_TAU:
        case XXBYTE_GDSCRIPT_ID_CONST_INF:
        case XXBYTE_GDSCRIPT_ID_CONST_NAN:
        case XXBYTE_GDSCRIPT_ID_DOLLAR:
        case XXBYTE_GDSCRIPT_ID_V3_IDENTIFIER:
        case XXBYTE_GDSCRIPT_ID_V3_CONSTANT:
        case XXBYTE_GDSCRIPT_ID_V3_BUILT_IN_TYPE:
        case XXBYTE_GDSCRIPT_ID_V3_BUILT_IN_FUNC:
        case XXBYTE_GDSCRIPT_ID_V3_PARENTHESIS_OPEN:
        case XXBYTE_GDSCRIPT_ID_V3_BRACKET_OPEN:
        case XXBYTE_GDSCRIPT_ID_V3_SELF:
        case XXBYTE_GDSCRIPT_ID_V3_CONST_PI:
        case XXBYTE_GDSCRIPT_ID_V3_CONST_TAU:
        case XXBYTE_GDSCRIPT_ID_V3_CONST_INF:
        case XXBYTE_GDSCRIPT_ID_V3_CONST_NAN:
        case XXBYTE_GDSCRIPT_ID_V3_DOLLAR: return 1;
        default: return 0;
    }
}

/* Operators that can be prefix ones. */
static int may_be_unary(uint32_t id)
{
    switch (id) {
        case XXBYTE_GDSCRIPT_ID_MINUS:
        case XXBYTE_GDSCRIPT_ID_PLUS:
        case XXBYTE_GDSCRIPT_ID_TILDE:
        case XXBYTE_GDSCRIPT_ID_BANG:
        case XXBYTE_GDSCRIPT_ID_PERCENT:
        case XXBYTE_GDSCRIPT_ID_V3_OP_SUB:
        case XXBYTE_GDSCRIPT_ID_V3_OP_ADD:
        case XXBYTE_GDSCRIPT_ID_V3_OP_BIT_INVERT: return 1;
        default: return 0;
    }
}

/* Whether a space separates prev and cur (both nonzero ids); before_operand
 * tells whether the token before prev ends an operand. */
static int need_space(uint32_t prev, uint32_t cur, int prev_numeric, int cur_numeric, int before_operand)
{
    int prev_open;
    int cur_close;
    int prev_dot = prev == XXBYTE_GDSCRIPT_ID_PERIOD || prev == XXBYTE_GDSCRIPT_ID_PERIOD_PERIOD || prev == XXBYTE_GDSCRIPT_ID_PERIOD_PERIOD_PERIOD ||
                   prev == XXBYTE_GDSCRIPT_ID_V3_PERIOD;
    int cur_dot = cur == XXBYTE_GDSCRIPT_ID_PERIOD || cur == XXBYTE_GDSCRIPT_ID_PERIOD_PERIOD || cur == XXBYTE_GDSCRIPT_ID_PERIOD_PERIOD_PERIOD ||
                  cur == XXBYTE_GDSCRIPT_ID_V3_PERIOD;

    if ((prev_dot && (cur_dot || cur_numeric)) || (prev_numeric && cur_dot)) {
        return 1;
    }
    /* A 4.x "+" or "-" right before a digit after a non-operand starts a
     * signed number literal, so it keeps its space before a number. */
    if (may_be_unary(prev) && !before_operand && starts_operand(cur) && !(cur_numeric && cur < XXBYTE_GDSCRIPT_ID_V3_EMPTY)) {
        return 0;
    }
    if ((prev == XXBYTE_GDSCRIPT_ID_COLON && cur == XXBYTE_GDSCRIPT_ID_EQUAL) || (prev == XXBYTE_GDSCRIPT_ID_V3_COLON && cur == XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN)) {
        return 0;
    }
    switch (prev) {
        case XXBYTE_GDSCRIPT_ID_PARENTHESIS_OPEN:
        case XXBYTE_GDSCRIPT_ID_BRACKET_OPEN:
        case XXBYTE_GDSCRIPT_ID_BRACE_OPEN:
        case XXBYTE_GDSCRIPT_ID_PERIOD:
        case XXBYTE_GDSCRIPT_ID_DOLLAR:
        case XXBYTE_GDSCRIPT_ID_V3_PARENTHESIS_OPEN:
        case XXBYTE_GDSCRIPT_ID_V3_BRACKET_OPEN:
        case XXBYTE_GDSCRIPT_ID_V3_CURLY_BRACKET_OPEN:
        case XXBYTE_GDSCRIPT_ID_V3_PERIOD:
        case XXBYTE_GDSCRIPT_ID_V3_DOLLAR: prev_open = 1; break;
        default: prev_open = 0; break;
    }
    switch (cur) {
        case XXBYTE_GDSCRIPT_ID_PARENTHESIS_CLOSE:
        case XXBYTE_GDSCRIPT_ID_BRACKET_CLOSE:
        case XXBYTE_GDSCRIPT_ID_BRACE_CLOSE:
        case XXBYTE_GDSCRIPT_ID_COMMA:
        case XXBYTE_GDSCRIPT_ID_COLON:
        case XXBYTE_GDSCRIPT_ID_SEMICOLON:
        case XXBYTE_GDSCRIPT_ID_PERIOD:
        case XXBYTE_GDSCRIPT_ID_PARENTHESIS_OPEN:
        case XXBYTE_GDSCRIPT_ID_V3_PARENTHESIS_CLOSE:
        case XXBYTE_GDSCRIPT_ID_V3_BRACKET_CLOSE:
        case XXBYTE_GDSCRIPT_ID_V3_CURLY_BRACKET_CLOSE:
        case XXBYTE_GDSCRIPT_ID_V3_COMMA:
        case XXBYTE_GDSCRIPT_ID_V3_COLON:
        case XXBYTE_GDSCRIPT_ID_V3_SEMICOLON:
        case XXBYTE_GDSCRIPT_ID_V3_PERIOD:
        case XXBYTE_GDSCRIPT_ID_V3_PARENTHESIS_OPEN: cur_close = 1; break;
        default: cur_close = 0; break;
    }
    return !prev_open && !cur_close;
}

typedef struct source_writer {
    xx_buf_t *out;
    int numbered;
    uint32_t line;   /* the line being written */
    int started;     /* anything written */
    int any;         /* a token written */
    int pending;     /* the line number is still to be printed */
    int content;     /* the current line has text */
    uint32_t budget; /* line gaps still allowed */
} source_writer;

static int writer_newline(source_writer *w, const char *before)
{
    if (w->pending) {
        /* An empty line: its number alone. */
        w->pending = 0;
        if (!xx_buf_appendf(w->out, "%4u", (unsigned)w->line)) {
            return 0;
        }
    }
    if (!xx_buf_append_str(w->out, before) || !xx_buf_append_char(w->out, '\n')) {
        return 0;
    }
    ++w->line;
    w->pending = w->numbered;
    w->content = 0;
    return 1;
}

static int writer_start(source_writer *w)
{
    if (!w->started) {
        w->started = 1;
        w->pending = w->numbered;
    }
    return 1;
}

/* Moves to line target with newlines (each preceded by before). Gaps
 * longer than MAX_GAP lines, or past the budget of a crafted file, become
 * one newline and the numbering jumps. */
#define MAX_GAP 65536u
#define LINE_BUDGET (4u * 1024u * 1024u)

static int writer_advance(source_writer *w, uint32_t target, const char *before)
{
    uint32_t gap;

    if (target <= w->line) {
        return 1;
    }
    gap = target - w->line;
    if (gap > MAX_GAP || gap > w->budget) {
        if (!writer_newline(w, before)) {
            return 0;
        }
        w->line = target;
        return 1;
    }
    w->budget -= gap;
    while (w->line < target) {
        if (!writer_newline(w, before)) {
            return 0;
        }
    }
    return 1;
}

/* Called before text is written on the current line. */
static int writer_begin(source_writer *w)
{
    w->content = 1;
    if (w->pending) {
        w->pending = 0;
        return xx_buf_appendf(w->out, "%4u  ", (unsigned)w->line) ? 1 : 0;
    }
    return 1;
}

/* Text of a token: keyword or punctuation, identifier, constant, ... */
static int put_token_text(xx_buf_t *out, const name_lookup *names, const token_view *t)
{
    const char *source = xxbyte_gdscript_tokens[t->id].source;

    switch (t->id) {
        case XXBYTE_GDSCRIPT_ID_IDENTIFIER:
        case XXBYTE_GDSCRIPT_ID_ANNOTATION:
        case XXBYTE_GDSCRIPT_ID_V3_IDENTIFIER:
        case XXBYTE_GDSCRIPT_ID_LITERAL:
        case XXBYTE_GDSCRIPT_ID_V3_CONSTANT: {
            int constant = t->id == XXBYTE_GDSCRIPT_ID_LITERAL || t->id == XXBYTE_GDSCRIPT_ID_V3_CONSTANT;
            int got = lookup_text(names, constant, t->value, out);

            if (got < 0) {
                return 0;
            }
            if (got == 0) {
                return xx_buf_appendf(out, constant ? "const[%u]" : "ident[%u]", (unsigned)t->value) ? 1 : 0;
            }
            return 1;
        }
        case XXBYTE_GDSCRIPT_ID_ERROR:
            /* The literal of an error token is the tokenizer's message. */
            if (!xx_buf_append_str(out, "<error ") || lookup_text(names, 1, t->value, out) < 0) {
                return 0;
            }
            return xx_buf_append_char(out, '>') ? 1 : 0;
        case XXBYTE_GDSCRIPT_ID_V3_BUILT_IN_TYPE: return xx_buf_append_str(out, xxbyte_gdscript_v3_type_name(t->value)) ? 1 : 0;
        case XXBYTE_GDSCRIPT_ID_V3_BUILT_IN_FUNC: return xx_buf_append_str(out, xxbyte_gdscript_builtin_name(names->file->mode, t->value)) ? 1 : 0;
        default: break;
    }
    if (source != NULL) {
        return xx_buf_append_str(out, source) ? 1 : 0;
    }
    /* Tokens without text (error, empty, VCS markers): a placeholder that
     * does not tokenize, so that it shows. */
    return xx_buf_appendf(out, "<%s>", xxbyte_gdscript_tokens[t->id].mnemonic) ? 1 : 0;
}

static int has_text(uint32_t id)
{
    switch (id) {
        case 0:
        case XXBYTE_GDSCRIPT_ID_EOF:
        case XXBYTE_GDSCRIPT_ID_NEWLINE:
        case XXBYTE_GDSCRIPT_ID_INDENT:
        case XXBYTE_GDSCRIPT_ID_DEDENT:
        case XXBYTE_GDSCRIPT_ID_V3_EOF:
        case XXBYTE_GDSCRIPT_ID_V3_NEWLINE:
        case XXBYTE_GDSCRIPT_ID_V3_EMPTY: return 0;
        default: return 1;
    }
}

static xxbyte_status rebuild(const name_lookup *names, const uint32_t *token_offsets, int numbered, xx_buf_t *out)
{
    const xxbyte_gdscript_file *file = names->file;
    const uint8_t *data = names->data;
    int v4 = is_v4(file->mode);
    uint32_t *starts = NULL; /* 4.x: line + 1 of line-starting tokens, column in starts_column */
    uint32_t *starts_column = NULL;
    uint32_t i;
    source_writer w;
    token_view prev;
    int prev_numeric = 0;
    int before_operand = 0;
    uint32_t tab_width = 1;
    uint32_t v3_entry = 0; /* 3.x: next line-map entry to consume */
    uint32_t v3_line = 1;

    memset(&w, 0, sizeof(w));
    memset(&prev, 0, sizeof(prev));
    w.out = out;
    w.numbered = numbered;
    w.line = 1;
    w.budget = LINE_BUDGET;
    if (file->token_count == 0) {
        return XXBYTE_STATUS_OK;
    }
    if (v4) {
        starts = (uint32_t *)xx_mem_calloc(file->token_count, sizeof(uint32_t));
        starts_column = (uint32_t *)xx_mem_calloc(file->token_count, sizeof(uint32_t));
        if (starts == NULL || starts_column == NULL) {
            xx_mem_free(starts);
            xx_mem_free(starts_column);
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        for (i = 0; i < file->line_count; ++i) {
            uint32_t token = rd32(data, (size_t)file->line_offset + 8u * i);
            uint32_t column_token = rd32(data, (size_t)file->column_offset + 8u * i);

            if (token < file->token_count) {
                starts[token] = rd32(data, (size_t)file->line_offset + 8u * i + 4u) + 1u;
                if (starts[token] == 0) {
                    starts[token] = UINT32_MAX;
                }
            }
            if (column_token < file->token_count) {
                starts_column[column_token] = rd32(data, (size_t)file->column_offset + 8u * i + 4u);
            }
        }
        /* The tokenizers of 4.3 to 4.6 count an indenting tab as tab_size
         * (4) columns, the one of 4.7 as one. 4.3 and 4.4 files and 4.5+
         * files whose columns are all 1 modulo 4 are rebuilt with a tab
         * per 4 columns, others with a tab per column. */
        tab_width = 4;
        if (file->mode != XXBYTE_GDSCRIPT_MODE_V4_3) {
            for (i = 0; i < file->token_count; ++i) {
                if (starts[i] != 0 && starts_column[i] != 0 && (starts_column[i] - 1u) % 4u != 0) {
                    tab_width = 1;
                    break;
                }
            }
        }
    }
    for (i = 0; i < file->token_count; ++i) {
        token_view t;
        uint32_t target;
        int line_start = 0;
        int numeric;
        int first;

        view_token(data, token_offsets[i], file->mode, &t);
        if (t.id == 0) {
            if (!writer_start(&w) || !writer_begin(&w) || !xx_buf_appendf(out, "%s<invalid>", prev.id != 0 ? " " : "")) {
                break;
            }
            prev.id = XXBYTE_GDSCRIPT_ID_IDENTIFIER;
            continue;
        }
        if (v4) {
            if (starts[i] != 0) {
                target = starts[i] - 1u;
                line_start = 1;
            } else {
                target = t.line;
            }
        } else {
            /* The line of the nearest line-map entry at or before i. */
            while (v3_entry < file->line_count && rd32(data, (size_t)file->line_offset + 8u * v3_entry) <= i) {
                v3_line = rd32(data, (size_t)file->line_offset + 8u * v3_entry + 4u) & 0xFFFFFFu;
                ++v3_entry;
            }
            target = v3_line;
        }
        if (t.id == XXBYTE_GDSCRIPT_ID_V3_NEWLINE) {
            uint32_t k;

            if (!writer_start(&w) || !writer_newline(&w, "")) {
                break;
            }
            w.any = 1;
            if (t.value != 0 && !writer_begin(&w)) {
                break;
            }
            for (k = 0; k < t.value && k < 4096u; ++k) {
                if (!xx_buf_append_char(out, '\t')) {
                    break;
                }
            }
            prev.id = 0;
            continue;
        }
        if (!has_text(t.id)) {
            continue;
        }
        first = !w.any;
        if (!writer_start(&w)) {
            break;
        }
        w.any = 1;
        if (line_start) {
            uint32_t k;
            uint32_t column = starts_column[i];

            if (!first && !writer_newline(&w, "")) {
                break;
            }
            if (!writer_advance(&w, target, "")) {
                break;
            }
            if (column > 1 && !writer_begin(&w)) {
                break;
            }
            if (column > 1) {
                if (column > 4097u) {
                    column = 4097u; /* crafted files */
                }
                for (k = 0; k < (column - 1u) / tab_width; ++k) {
                    if (!xx_buf_append_char(out, '\t')) {
                        break;
                    }
                }
                for (k = 0; k < (column - 1u) % tab_width; ++k) {
                    if (!xx_buf_append_char(out, ' ')) {
                        break;
                    }
                }
            }
            prev.id = 0;
        } else if (target > w.line) {
            if (!writer_advance(&w, target, first ? "" : (prev.id != 0 ? " \\" : "\\"))) {
                break;
            }
            prev.id = 0;
        }
        numeric = is_numeric_literal(data, file, names->constant_offsets, &t);
        if (!writer_begin(&w)) {
            break;
        }
        if (prev.id != 0 && need_space(prev.id, t.id, prev_numeric, numeric, before_operand) && !xx_buf_append_char(out, ' ')) {
            break;
        }
        before_operand = prev.id != 0 && ends_operand(prev.id);
        if (!put_token_text(out, names, &t)) {
            break;
        }
        prev = t;
        prev_numeric = numeric;
    }
    if (w.content) {
        xx_buf_append_char(out, '\n');
    }
    xx_mem_free(starts);
    xx_mem_free(starts_column);
    return xx_buf_ok(out) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

/* Fills the offset arrays of a parsed file; the caller frees them. */
static xxbyte_status make_offsets(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, uint32_t **identifiers, uint32_t **constants, uint32_t **tokens)
{
    size_t count;
    xxbyte_status status;

    *identifiers = (uint32_t *)xx_mem_calloc((size_t)file->identifier_count + 1u, sizeof(uint32_t));
    *constants = (uint32_t *)xx_mem_calloc((size_t)file->constant_count + 1u, sizeof(uint32_t));
    *tokens = (uint32_t *)xx_mem_calloc((size_t)file->token_count + 1u, sizeof(uint32_t));
    if (*identifiers == NULL || *constants == NULL || *tokens == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_gdscript_table_offsets(data, size, file, XXBYTE_GDSCRIPT_TABLE_IDENTIFIER, *identifiers, file->identifier_count, &count);
    if (status == XXBYTE_STATUS_OK) {
        status = xxbyte_gdscript_table_offsets(data, size, file, XXBYTE_GDSCRIPT_TABLE_CONSTANT, *constants, file->constant_count, &count);
    }
    if (status == XXBYTE_STATUS_OK) {
        status = xxbyte_gdscript_table_offsets(data, size, file, XXBYTE_GDSCRIPT_TABLE_TOKEN, *tokens, file->token_count, &count);
    }
    return status;
}

static void free_offsets(uint32_t *identifiers, uint32_t *constants, uint32_t *tokens)
{
    xx_mem_free(identifiers);
    xx_mem_free(constants);
    xx_mem_free(tokens);
}

xxbyte_status xxbyte_gdscript_source(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, int numbered, void *out)
{
    uint32_t *identifiers = NULL;
    uint32_t *constants = NULL;
    uint32_t *tokens = NULL;
    name_lookup names;
    xxbyte_status status;

    if (out == NULL || !file_ok(data, size, file)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = make_offsets(data, size, file, &identifiers, &constants, &tokens);
    if (status == XXBYTE_STATUS_OK) {
        names.data = data;
        names.size = size;
        names.file = file;
        names.identifier_offsets = identifiers;
        names.constant_offsets = constants;
        status = rebuild(&names, tokens, numbered, (xx_buf_t *)out);
    }
    free_offsets(identifiers, constants, tokens);
    return status;
}

/* ------------------------------------------------------------------------ */
/* Decompression. */

xxbyte_status xxbyte_gdscript_decompress(const uint8_t *data, size_t size, uint8_t *out, size_t out_capacity, size_t *out_size)
{
    uint32_t version;
    uint32_t decompressed;
    size_t needed;
    xxbyte_status status;

    if (out_size != NULL) {
        *out_size = 0;
    }
    if (out_size == NULL || (data == NULL && size != 0) || (out == NULL && out_capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (memcmp(data == NULL ? magic : data, magic, size < MAGIC_SIZE ? size : MAGIC_SIZE) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (size < V4_HEADER_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    version = rd32(data, 4);
    if (version != 100 && version != 101) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    decompressed = rd32(data, 8);
    needed = decompressed == 0 ? size : (size_t)decompressed + V4_HEADER_SIZE;
    *out_size = needed;
    if (out_capacity < needed) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (decompressed == 0) {
        memcpy(out, data, size);
        return XXBYTE_STATUS_OK;
    }
    status = xxbyte_gdscript_zstd(data + V4_HEADER_SIZE, size - V4_HEADER_SIZE, out + V4_HEADER_SIZE, decompressed);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    memcpy(out, data, 8);
    memset(out + 8, 0, 4);
    return XXBYTE_STATUS_OK;
}

/* ------------------------------------------------------------------------ */
/* list_file. */

static size_t list_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const name_lookup *names = (const name_lookup *)context;
    const xxbyte_operand *operand = &instruction->operands[operand_index];
    xx_buf_t text;
    size_t n = 0;

    if (operand->type != XXBYTE_OPERAND_INDEX || (operand->kind != XXBYTE_INDEX_NAME && operand->kind != XXBYTE_INDEX_CONSTANT)) {
        return 0;
    }
    xx_buf_init(&text);
    if (lookup_text(names, operand->kind == XXBYTE_INDEX_CONSTANT, (uint64_t)operand->value, &text) > 0 && xx_buf_ok(&text) && text.size != 0) {
        n = copy_out(&text, buffer, buffer_size);
    }
    xx_buf_free(&text);
    return n;
}

static const char *mode_title(uint32_t mode)
{
    switch (mode) {
        case XXBYTE_GDSCRIPT_MODE_V4_3: return "4.3-4.4";
        case XXBYTE_GDSCRIPT_MODE_V3_5: return "3.5-3.6";
        case XXBYTE_GDSCRIPT_MODE_V3_2: return "3.2-3.4";
        case XXBYTE_GDSCRIPT_MODE_V3_1_1: return "3.1.1-3.1.2";
        case XXBYTE_GDSCRIPT_MODE_V3_1: return "3.1";
        case XXBYTE_GDSCRIPT_MODE_V3_0: return "3.0";
        default: return "4.5+";
    }
}

static xxbyte_status list_parsed(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, uint32_t compressed, uint32_t listing_flags, xx_buf_t *out)
{
    uint32_t *identifiers = NULL;
    uint32_t *constants = NULL;
    uint32_t *tokens = NULL;
    name_lookup names;
    uint32_t i;
    int v4 = is_v4(file->mode);
    xxbyte_status status;

    xx_buf_appendf(out, "; GDScript %s tokens (%s version %u", mode_title(file->mode), v4 ? "tokenizer" : "bytecode", (unsigned)file->version);
    if (compressed != 0) {
        xx_buf_appendf(out, ", zstd %u bytes", (unsigned)compressed);
    }
    xx_buf_appendf(out, "): %u identifiers, %u constants, %u lines, %u tokens\n", (unsigned)file->identifier_count, (unsigned)file->constant_count,
                   (unsigned)file->line_count, (unsigned)file->token_count);
    if (file->trailing_size != 0) {
        xx_buf_appendf(out, "; %u bytes after the last token%s\n", (unsigned)file->trailing_size, v4 ? " (the engine rejects the file)" : "");
    }
    status = make_offsets(data, size, file, &identifiers, &constants, &tokens);
    if (status != XXBYTE_STATUS_OK) {
        free_offsets(identifiers, constants, tokens);
        return status;
    }
    names.data = data;
    names.size = size;
    names.file = file;
    names.identifier_offsets = identifiers;
    names.constant_offsets = constants;
    for (i = 0; i < file->identifier_count && xx_buf_ok(out); ++i) {
        xx_buf_appendf(out, "; ident[%u] = ", (unsigned)i);
        put_identifier(out, data, v4, identifiers[i]);
        xx_buf_append_char(out, '\n');
    }
    for (i = 0; i < file->constant_count && xx_buf_ok(out); ++i) {
        xx_buf_appendf(out, "; const[%u] = ", (unsigned)i);
        put_variant(out, data, file->line_offset, v4, constants[i]);
        xx_buf_append_char(out, '\n');
    }
    xx_buf_append_str(out, "; tokens\n");
    status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_GDSCRIPT, file->mode, NULL, data + file->token_offset, file->token_size, file->token_offset, listing_flags, list_resolver,
                                  &names, out);
    if (status == XXBYTE_STATUS_OK) {
        xx_buf_append_str(out, "; source\n");
        status = rebuild(&names, tokens, 1, out);
    }
    free_offsets(identifiers, constants, tokens);
    if (status == XXBYTE_STATUS_OK && !xx_buf_ok(out)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return status;
}

xxbyte_status xxbyte_gdscript_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_gdscript_file file;
    xxbyte_status status = xxbyte_gdscript_parse(data, size, mode, &file);
    uint8_t *image;
    size_t image_size;

    if (status == XXBYTE_STATUS_OK) {
        return list_parsed(data, size, &file, 0, listing_flags, out);
    }
    if (status != XXBYTE_STATUS_UNSUPPORTED_MODE || file.decompressed_size == 0) {
        if (status == XXBYTE_STATUS_UNSUPPORTED_MODE) {
            xx_buf_appendf(out, "; GDScript tokens, unsupported version %u\n", (unsigned)file.version);
        }
        return status;
    }
    if (file.decompressed_size > MAX_DECOMPRESSED) {
        xx_buf_appendf(out, "; GDScript tokens, zstd payload of %u bytes is too large\n", (unsigned)file.decompressed_size);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    image_size = (size_t)file.decompressed_size + V4_HEADER_SIZE;
    image = (uint8_t *)xx_mem_alloc(image_size);
    if (image == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_gdscript_decompress(data, size, image, image_size, &image_size);
    if (status == XXBYTE_STATUS_OK) {
        uint32_t compressed = (uint32_t)(size - V4_HEADER_SIZE);

        status = xxbyte_gdscript_parse(image, image_size, mode, &file);
        if (status == XXBYTE_STATUS_OK) {
            status = list_parsed(image, image_size, &file, compressed, listing_flags, out);
        }
    } else {
        xx_buf_appendf(out, "; GDScript tokens, corrupt zstd payload\n");
    }
    xx_mem_free(image);
    return status;
}

#else
typedef int xxbyte_gdscript_file_disabled;
#endif
