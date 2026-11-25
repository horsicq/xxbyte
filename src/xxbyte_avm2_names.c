#include "xxbyte_avm2_internal.h"

#if XXBYTE_USE_AVM2

#include <xxfclib/memory/xx_memory.h>

#include <stdlib.h>

/*
 * Names from an abcFile: the constant pools, method names and signatures in
 * the text syntax documented in xxbyte_avm2.h, and the resolver built on
 * them.
 */

/* The deepest TypeName nesting that is printed. */
#define MAX_TYPE_DEPTH 8u

/* Option and slot value kinds (avmplus CONSTANT_*). */
#define VALUE_UNDEFINED 0x00u
#define VALUE_UTF8 0x01u
#define VALUE_FLOAT 0x02u
#define VALUE_INT 0x03u
#define VALUE_UINT 0x04u
#define VALUE_DOUBLE 0x06u
#define VALUE_FALSE 0x0Au
#define VALUE_TRUE 0x0Bu
#define VALUE_NULL 0x0Cu
#define VALUE_FLOAT4 0x1Eu

struct xxbyte_avm2_names {
    const uint8_t *data;
    size_t size;
    xxbyte_avm2_abc abc;
    /* Entry offsets of every table but METHOD_BODY; counts include the
     * implicit entry 0 of the pools. */
    uint32_t *offsets[XXBYTE_AVM2_TABLE_COUNT];
    size_t counts[XXBYTE_AVM2_TABLE_COUNT];
    xxbyte_avm2_method_body *bodies;
    size_t body_count;
    uint32_t *body_of;                /* per method_info */
    xxbyte_avm2_method_owner *owners; /* per method_info */
    uint8_t *private_shared;          /* per namespace: a private namespace
                                         whose name another one shares */
    uint32_t selected;                /* body index, or body_count */
};

/* ---- bounded text ---- */

typedef struct avm2_text {
    char *buffer;
    size_t capacity; /* bytes of buffer, room for the NUL included */
    size_t length;
    int full;
} avm2_text;

static void text_init(avm2_text *text, char *buffer, size_t capacity)
{
    text->buffer = buffer;
    text->capacity = capacity;
    text->length = 0;
    text->full = capacity == 0;
    if (capacity != 0) {
        buffer[0] = '\0';
    }
}

static void text_bytes(avm2_text *text, const char *bytes, size_t count)
{
    if (text->full) {
        return;
    }
    if (count > text->capacity - 1 - text->length) {
        count = text->capacity - 1 - text->length;
        text->full = 1;
    }
    memcpy(text->buffer + text->length, bytes, count);
    text->length += count;
    text->buffer[text->length] = '\0';
}

/* A whole unit (one UTF-8 character or escape) or nothing. */
static void text_unit(avm2_text *text, const char *bytes, size_t count)
{
    if (text->full) {
        return;
    }
    if (count > text->capacity - 1 - text->length) {
        text->full = 1;
        return;
    }
    text_bytes(text, bytes, count);
}

static void text_str(avm2_text *text, const char *str)
{
    text_bytes(text, str, strlen(str));
}

static void text_char(avm2_text *text, char c)
{
    text_bytes(text, &c, 1);
}

static void text_u64(avm2_text *text, uint64_t value)
{
    char digits[24];
    size_t n = sizeof(digits);

    do {
        digits[--n] = (char)('0' + (int)(value % 10u));
        value /= 10u;
    } while (value != 0);
    text_bytes(text, digits + n, sizeof(digits) - n);
}

static void text_s64(avm2_text *text, int64_t value)
{
    if (value < 0) {
        text_char(text, '-');
        text_u64(text, 0u - (uint64_t)value);
    } else {
        text_u64(text, (uint64_t)value);
    }
}

static void text_double(avm2_text *text, double value, int single)
{
    xx_buf_t buf;
    xxbyte_fmt fmt;

    xx_buf_init(&buf);
    memset(&fmt, 0, sizeof(fmt));
    fmt.out = &buf;
    if ((single ? xxbyte_fmt_float(&fmt, (float)value) : xxbyte_fmt_double(&fmt, value)) && xx_buf_ok(&buf) && buf.data != NULL) {
        text_bytes(text, buf.data, buf.size);
    } else {
        text->full = 1;
    }
    xx_buf_free(&buf);
}

/* The final length; text that did not fit ends in "...". */
static size_t text_finish(avm2_text *text)
{
    if (text->capacity == 0) {
        return 0;
    }
    if (text->full) {
        size_t keep = text->capacity - 1;
        if (keep < 3) {
            text->buffer[0] = '\0';
            return 0;
        }
        keep -= 3;
        if (keep > text->length) {
            keep = text->length;
        }
        /* Do not cut a UTF-8 character. */
        while (keep > 0 && ((uint8_t)text->buffer[keep] & 0xC0u) == 0x80u) {
            --keep;
        }
        memcpy(text->buffer + keep, "...", 4);
        text->length = keep + 3;
    }
    return text->length;
}

/* Returns 0 after clearing the buffer. */
static size_t text_fail(avm2_text *text)
{
    if (text->capacity != 0) {
        text->buffer[0] = '\0';
    }
    text->length = 0;
    return 0;
}

/* The length of the valid UTF-8 character at bytes[0], or 0. */
static size_t utf8_length(const uint8_t *bytes, size_t available)
{
    uint8_t c = bytes[0];
    size_t length;
    uint8_t low = 0x80u;
    uint8_t high = 0xBFu;
    size_t i;

    if (c < 0x80u) {
        return 1;
    }
    if (c >= 0xC2u && c <= 0xDFu) {
        length = 2;
    } else if (c >= 0xE0u && c <= 0xEFu) {
        length = 3;
        if (c == 0xE0u) {
            low = 0xA0u;
        } else if (c == 0xEDu) {
            high = 0x9Fu;
        }
    } else if (c >= 0xF0u && c <= 0xF4u) {
        length = 4;
        if (c == 0xF0u) {
            low = 0x90u;
        } else if (c == 0xF4u) {
            high = 0x8Fu;
        }
    } else {
        return 0;
    }
    if (length > available) {
        return 0;
    }
    for (i = 1; i < length; ++i) {
        uint8_t b = bytes[i];
        if (b < (i == 1 ? low : 0x80u) || b > (i == 1 ? high : 0xBFu)) {
            return 0;
        }
    }
    return length;
}

static const char hex_digits[] = "0123456789abcdef";

/* The escaped body of a string (without quotes). */
static void text_escaped(avm2_text *text, const uint8_t *bytes, size_t length)
{
    size_t i = 0;

    while (i < length && !text->full) {
        uint8_t c = bytes[i];
        size_t n = utf8_length(bytes + i, length - i);
        char unit[4];

        if (n == 1 && c >= 0x20u && c != 0x7Fu && c != '"' && c != '\\') {
            text_unit(text, (const char *)bytes + i, 1);
        } else if (n > 1) {
            text_unit(text, (const char *)bytes + i, n);
        } else {
            unit[0] = '\\';
            n = 1;
            switch (c) {
                case '"': unit[1] = '"'; break;
                case '\\': unit[1] = '\\'; break;
                case '\n': unit[1] = 'n'; break;
                case '\r': unit[1] = 'r'; break;
                case '\t': unit[1] = 't'; break;
                default:
                    unit[1] = 'x';
                    unit[2] = hex_digits[c >> 4];
                    unit[3] = hex_digits[c & 15u];
                    text_unit(text, unit, 4);
                    i += n;
                    continue;
            }
            text_unit(text, unit, 2);
        }
        i += n;
    }
}

/* ---- pool access ---- */

static int entry_offset(const xxbyte_avm2_names *names, xxbyte_avm2_table table, uint32_t index, size_t *offset)
{
    if (index >= names->counts[table] || names->offsets[table] == NULL) {
        return 0;
    }
    *offset = names->offsets[table][index];
    return 1;
}

static int read_u30(const xxbyte_avm2_names *names, size_t *pos, uint32_t *value)
{
    return xxbyte_avm2_read_u30(names->data, names->size, pos, value) == XXBYTE_STATUS_OK;
}

static int read_u8(const xxbyte_avm2_names *names, size_t *pos, uint8_t *value)
{
    if (*pos >= names->size) {
        return 0;
    }
    *value = names->data[(*pos)++];
    return 1;
}

/* String index 1.. (0 has no bytes). */
static int get_string(const xxbyte_avm2_names *names, uint32_t index, const uint8_t **bytes, uint32_t *length)
{
    size_t offset;

    return index != 0 && entry_offset(names, XXBYTE_AVM2_TABLE_STRING, index, &offset) &&
           xxbyte_avm2_read_string(names->data, names->size, offset, bytes, length) == XXBYTE_STATUS_OK;
}

static int get_namespace(const xxbyte_avm2_names *names, uint32_t index, uint8_t *kind, uint32_t *name)
{
    size_t pos;

    return index != 0 && entry_offset(names, XXBYTE_AVM2_TABLE_NAMESPACE, index, &pos) && read_u8(names, &pos, kind) && read_u30(names, &pos, name);
}

typedef struct avm2_mn {
    uint8_t kind;
    uint32_t ns;     /* QName */
    uint32_t name;   /* QName, RTQName, Multiname */
    uint32_t ns_set; /* Multiname, MultinameL */
    uint32_t base;   /* TypeName */
    uint32_t param_count;
    size_t params; /* offset of the TypeName parameters */
} avm2_mn;

static int get_multiname(const xxbyte_avm2_names *names, uint32_t index, avm2_mn *mn)
{
    size_t pos;

    memset(mn, 0, sizeof(*mn));
    if (index == 0 || !entry_offset(names, XXBYTE_AVM2_TABLE_MULTINAME, index, &pos) || !read_u8(names, &pos, &mn->kind)) {
        return 0;
    }
    switch (mn->kind) {
        case XXBYTE_AVM2_MN_QNAME:
        case XXBYTE_AVM2_MN_QNAME_A: return read_u30(names, &pos, &mn->ns) && read_u30(names, &pos, &mn->name);
        case XXBYTE_AVM2_MN_RTQNAME:
        case XXBYTE_AVM2_MN_RTQNAME_A: return read_u30(names, &pos, &mn->name);
        case XXBYTE_AVM2_MN_RTQNAME_L:
        case XXBYTE_AVM2_MN_RTQNAME_LA: return 1;
        case XXBYTE_AVM2_MN_MULTINAME:
        case XXBYTE_AVM2_MN_MULTINAME_A: return read_u30(names, &pos, &mn->name) && read_u30(names, &pos, &mn->ns_set);
        case XXBYTE_AVM2_MN_MULTINAME_L:
        case XXBYTE_AVM2_MN_MULTINAME_LA: return read_u30(names, &pos, &mn->ns_set);
        case XXBYTE_AVM2_MN_TYPENAME:
            if (!read_u30(names, &pos, &mn->base) || !read_u30(names, &pos, &mn->param_count)) {
                return 0;
            }
            mn->params = pos;
            return 1;
        default: return 0;
    }
}

static int is_attribute(uint8_t kind)
{
    switch (kind) {
        case XXBYTE_AVM2_MN_QNAME_A:
        case XXBYTE_AVM2_MN_RTQNAME_A:
        case XXBYTE_AVM2_MN_RTQNAME_LA:
        case XXBYTE_AVM2_MN_MULTINAME_A:
        case XXBYTE_AVM2_MN_MULTINAME_LA: return 1;
        default: return 0;
    }
}

static const char *namespace_kind_name(uint8_t kind)
{
    switch (kind) {
        case XXBYTE_AVM2_NS_PRIVATE: return "PrivateNamespace";
        case XXBYTE_AVM2_NS_NAMESPACE: return "Namespace";
        case XXBYTE_AVM2_NS_PACKAGE: return "PackageNamespace";
        case XXBYTE_AVM2_NS_PACKAGE_INTERNAL: return "PackageInternalNs";
        case XXBYTE_AVM2_NS_PROTECTED: return "ProtectedNamespace";
        case XXBYTE_AVM2_NS_EXPLICIT: return "ExplicitNamespace";
        case XXBYTE_AVM2_NS_STATIC_PROTECTED: return "StaticProtectedNs";
        default: return NULL;
    }
}

static const char *multiname_kind_name(uint8_t kind)
{
    switch (kind) {
        case XXBYTE_AVM2_MN_QNAME: return "QName";
        case XXBYTE_AVM2_MN_QNAME_A: return "QNameA";
        case XXBYTE_AVM2_MN_RTQNAME: return "RTQName";
        case XXBYTE_AVM2_MN_RTQNAME_A: return "RTQNameA";
        case XXBYTE_AVM2_MN_RTQNAME_L: return "RTQNameL";
        case XXBYTE_AVM2_MN_RTQNAME_LA: return "RTQNameLA";
        case XXBYTE_AVM2_MN_MULTINAME: return "Multiname";
        case XXBYTE_AVM2_MN_MULTINAME_A: return "MultinameA";
        case XXBYTE_AVM2_MN_MULTINAME_L: return "MultinameL";
        case XXBYTE_AVM2_MN_MULTINAME_LA: return "MultinameLA";
        case XXBYTE_AVM2_MN_TYPENAME: return "TypeName";
        default: return NULL;
    }
}

/* ---- text of pool entries ---- */

static void text_index(avm2_text *text, const char *prefix, uint32_t index)
{
    text_str(text, prefix);
    text_u64(text, index);
}

/* "text", or null for string 0, or string@N. */
static void text_string(const xxbyte_avm2_names *names, avm2_text *text, uint32_t index)
{
    const uint8_t *bytes;
    uint32_t length;

    if (index == 0) {
        text_str(text, "null");
    } else if (get_string(names, index, &bytes, &length)) {
        text_char(text, '"');
        text_escaped(text, bytes, length);
        text_char(text, '"');
    } else {
        text_index(text, "string@", index);
    }
}

/* A name inside a multiname: string 0 is the any name. */
static void text_name(const xxbyte_avm2_names *names, avm2_text *text, uint32_t index)
{
    if (index == 0) {
        text_char(text, '*');
    } else {
        text_string(names, text, index);
    }
}

static void text_namespace(const xxbyte_avm2_names *names, avm2_text *text, uint32_t index)
{
    uint8_t kind;
    uint32_t name;
    const char *kind_name;

    if (index == 0) {
        text_char(text, '*');
        return;
    }
    if (!get_namespace(names, index, &kind, &name) || (kind_name = namespace_kind_name(kind)) == NULL) {
        text_index(text, "namespace@", index);
        return;
    }
    text_str(text, kind_name);
    text_char(text, '(');
    text_string(names, text, name);
    if (names->private_shared != NULL && names->private_shared[index]) {
        text_str(text, ", \"#");
        text_u64(text, index);
        text_char(text, '"');
    }
    text_char(text, ')');
}

static void text_ns_set(const xxbyte_avm2_names *names, avm2_text *text, uint32_t index)
{
    size_t pos;
    uint32_t count;
    uint32_t i;

    if (index == 0 || !entry_offset(names, XXBYTE_AVM2_TABLE_NS_SET, index, &pos) || !read_u30(names, &pos, &count)) {
        text_index(text, "nsset@", index);
        return;
    }
    text_char(text, '[');
    for (i = 0; i < count && !text->full; ++i) {
        uint32_t ns;
        if (!read_u30(names, &pos, &ns)) {
            break;
        }
        if (i != 0) {
            text_str(text, ", ");
        }
        text_namespace(names, text, ns);
    }
    text_char(text, ']');
}

static void text_multiname(const xxbyte_avm2_names *names, avm2_text *text, uint32_t index, unsigned depth)
{
    avm2_mn mn;
    uint32_t i;
    size_t pos;

    if (index == 0) {
        text_char(text, '*');
        return;
    }
    if (depth > MAX_TYPE_DEPTH || !get_multiname(names, index, &mn)) {
        text_index(text, "multiname@", index);
        return;
    }
    text_str(text, multiname_kind_name(mn.kind));
    text_char(text, '(');
    switch (mn.kind) {
        case XXBYTE_AVM2_MN_QNAME:
        case XXBYTE_AVM2_MN_QNAME_A:
            text_namespace(names, text, mn.ns);
            text_str(text, ", ");
            text_name(names, text, mn.name);
            break;
        case XXBYTE_AVM2_MN_RTQNAME:
        case XXBYTE_AVM2_MN_RTQNAME_A: text_name(names, text, mn.name); break;
        case XXBYTE_AVM2_MN_MULTINAME:
        case XXBYTE_AVM2_MN_MULTINAME_A:
            text_name(names, text, mn.name);
            text_str(text, ", ");
            text_ns_set(names, text, mn.ns_set);
            break;
        case XXBYTE_AVM2_MN_MULTINAME_L:
        case XXBYTE_AVM2_MN_MULTINAME_LA: text_ns_set(names, text, mn.ns_set); break;
        case XXBYTE_AVM2_MN_TYPENAME:
            text_multiname(names, text, mn.base, depth + 1);
            text_char(text, '<');
            pos = mn.params;
            for (i = 0; i < mn.param_count && !text->full; ++i) {
                uint32_t param;
                if (!read_u30(names, &pos, &param)) {
                    break;
                }
                if (i != 0) {
                    text_str(text, ", ");
                }
                text_multiname(names, text, param, depth + 1);
            }
            text_char(text, '>');
            break;
        default: break;
    }
    text_char(text, ')');
}

/* ---- compact names (avmplus Multiname::print) ---- */

/* A namespace URI without its trailing API version marks (U+E000 to
 * U+F8FF, three UTF-8 bytes EE 80 80 to EF A3 BF). */
static size_t strip_version_marks(const uint8_t *bytes, size_t length)
{
    while (length >= 3) {
        const uint8_t *p = bytes + length - 3;
        uint32_t cp;
        if ((p[0] & 0xF0u) != 0xE0u || (p[1] & 0xC0u) != 0x80u || (p[2] & 0xC0u) != 0x80u) {
            break;
        }
        cp = ((uint32_t)(p[0] & 0x0Fu) << 12) | ((uint32_t)(p[1] & 0x3Fu) << 6) | (uint32_t)(p[2] & 0x3Fu);
        if (cp < 0xE000u || cp > 0xF8FFu) {
            break;
        }
        length -= 3;
    }
    return length;
}

/* The raw escaped bytes of string index, or "" for 0 / an invalid index. */
static void text_raw_string(const xxbyte_avm2_names *names, avm2_text *text, uint32_t index, int uri)
{
    const uint8_t *bytes;
    uint32_t length;

    if (get_string(names, index, &bytes, &length)) {
        text_escaped(text, bytes, uri ? strip_version_marks(bytes, length) : length);
    }
}

/* A local name: * for the any name. */
static void text_raw_name(const xxbyte_avm2_names *names, avm2_text *text, uint32_t index)
{
    if (index == 0) {
        text_char(text, '*');
    } else {
        text_raw_string(names, text, index, 0);
    }
}

static int namespace_is_public_kind(uint8_t kind)
{
    return kind == XXBYTE_AVM2_NS_NAMESPACE || kind == XXBYTE_AVM2_NS_PACKAGE;
}

/* Nonzero for a namespace whose URI (version marks dropped) is empty. */
static int namespace_uri_empty(const xxbyte_avm2_names *names, uint32_t name)
{
    const uint8_t *bytes;
    uint32_t length;

    return get_string(names, name, &bytes, &length) && strip_version_marks(bytes, length) == 0;
}

/* The URI of a namespace (Multiname::print prints NS_Public with an empty
 * URI as "public" in namespace sets). */
static void text_ns_uri(const xxbyte_avm2_names *names, avm2_text *text, uint32_t ns, int public_word)
{
    uint8_t kind;
    uint32_t name;

    if (ns == 0) {
        text_char(text, '*');
        return;
    }
    if (!get_namespace(names, ns, &kind, &name)) {
        text_index(text, "namespace@", ns);
        return;
    }
    if (name == 0) {
        /* avmplus gives a namespace without a name an undefined URI. */
        text_str(text, "undefined");
    } else if (public_word && namespace_is_public_kind(kind) && namespace_uri_empty(names, name)) {
        text_str(text, "public");
    } else {
        text_raw_string(names, text, name, 1);
    }
}

/*
 * hide_non_public: Traits::print, which prints a QName in a namespace that
 * is not NS_Public (private, protected, internal) as its bare name.
 */
static void text_compact(const xxbyte_avm2_names *names, avm2_text *text, uint32_t index, unsigned depth, int hide_non_public)
{
    avm2_mn mn;
    uint8_t ns_kind = 0;
    uint32_t ns_name = 0;
    const char *attr;
    size_t pos;
    uint32_t i;

    if (index == 0) {
        text_char(text, '*');
        return;
    }
    if (depth > MAX_TYPE_DEPTH || !get_multiname(names, index, &mn)) {
        text_index(text, "multiname@", index);
        return;
    }
    attr = is_attribute(mn.kind) ? "@" : "";
    switch (mn.kind) {
        case XXBYTE_AVM2_MN_QNAME:
        case XXBYTE_AVM2_MN_QNAME_A:
            if (mn.ns == 0) {
                text_str(text, attr);
                text_str(text, "*::");
            } else if (!get_namespace(names, mn.ns, &ns_kind, &ns_name)) {
                text_index(text, "namespace@", mn.ns);
                text_str(text, "::");
                text_str(text, attr);
            } else if ((namespace_is_public_kind(ns_kind) && ns_name != 0 && namespace_uri_empty(names, ns_name)) ||
                       (hide_non_public && !namespace_is_public_kind(ns_kind))) {
                text_str(text, attr);
            } else {
                text_ns_uri(names, text, mn.ns, 0);
                text_str(text, "::");
                text_str(text, attr);
            }
            text_raw_name(names, text, mn.name);
            break;
        case XXBYTE_AVM2_MN_RTQNAME:
        case XXBYTE_AVM2_MN_RTQNAME_A:
        case XXBYTE_AVM2_MN_RTQNAME_L:
        case XXBYTE_AVM2_MN_RTQNAME_LA:
            text_str(text, attr);
            text_str(text, "[]::");
            if (mn.kind == XXBYTE_AVM2_MN_RTQNAME || mn.kind == XXBYTE_AVM2_MN_RTQNAME_A) {
                text_raw_name(names, text, mn.name);
            } else {
                text_str(text, "[]");
            }
            break;
        case XXBYTE_AVM2_MN_MULTINAME:
        case XXBYTE_AVM2_MN_MULTINAME_A:
        case XXBYTE_AVM2_MN_MULTINAME_L:
        case XXBYTE_AVM2_MN_MULTINAME_LA: {
            uint32_t count = 0;
            text_str(text, attr);
            text_char(text, '{');
            if (entry_offset(names, XXBYTE_AVM2_TABLE_NS_SET, mn.ns_set, &pos) && mn.ns_set != 0 && read_u30(names, &pos, &count)) {
                for (i = 0; i < count && !text->full; ++i) {
                    uint32_t ns;
                    if (!read_u30(names, &pos, &ns)) {
                        break;
                    }
                    if (i != 0) {
                        text_char(text, ',');
                    }
                    text_ns_uri(names, text, ns, 1);
                }
            } else {
                text_index(text, "nsset@", mn.ns_set);
            }
            text_str(text, "}::");
            if (mn.kind == XXBYTE_AVM2_MN_MULTINAME || mn.kind == XXBYTE_AVM2_MN_MULTINAME_A) {
                text_raw_name(names, text, mn.name);
            } else {
                text_str(text, "[]");
            }
            break;
        }
        case XXBYTE_AVM2_MN_TYPENAME:
            text_compact(names, text, mn.base, depth + 1, hide_non_public);
            text_str(text, ".<");
            pos = mn.params;
            for (i = 0; i < mn.param_count && !text->full; ++i) {
                uint32_t param;
                if (!read_u30(names, &pos, &param)) {
                    break;
                }
                if (i != 0) {
                    text_char(text, ',');
                }
                text_compact(names, text, param, depth + 1, hide_non_public);
            }
            text_char(text, '>');
            break;
        default: break;
    }
}

/*
 * A trait or class name as the VM names methods (Multiname::Format and
 * Traits::print): the first namespace of a Multiname, the bare name for a
 * public or non-NS_Public namespace, else "uri::name".
 */
static void text_vm_name(const xxbyte_avm2_names *names, avm2_text *text, uint32_t index)
{
    avm2_mn mn;
    uint32_t ns = 0;
    uint8_t ns_kind;
    uint32_t ns_name;

    if (index == 0 || !get_multiname(names, index, &mn)) {
        text_compact(names, text, index, 0, 1);
        return;
    }
    if (mn.kind == XXBYTE_AVM2_MN_QNAME || mn.kind == XXBYTE_AVM2_MN_QNAME_A) {
        ns = mn.ns;
    } else if (mn.kind == XXBYTE_AVM2_MN_MULTINAME || mn.kind == XXBYTE_AVM2_MN_MULTINAME_A) {
        size_t pos;
        uint32_t count = 0;
        if (mn.ns_set == 0 || !entry_offset(names, XXBYTE_AVM2_TABLE_NS_SET, mn.ns_set, &pos) || !read_u30(names, &pos, &count) || count == 0 ||
            !read_u30(names, &pos, &ns)) {
            text_compact(names, text, index, 0, 1);
            return;
        }
    } else {
        text_compact(names, text, index, 0, 1);
        return;
    }
    if (ns == 0) {
        text_str(text, "*::");
    } else if (!get_namespace(names, ns, &ns_kind, &ns_name)) {
        text_index(text, "namespace@", ns);
        text_str(text, "::");
    } else if (namespace_is_public_kind(ns_kind) && !(ns_name != 0 && namespace_uri_empty(names, ns_name))) {
        text_ns_uri(names, text, ns, 0);
        text_str(text, "::");
    }
    text_raw_name(names, text, mn.name);
}

/* ---- values ---- */

static int read_pool_s32(const xxbyte_avm2_names *names, xxbyte_avm2_table table, uint32_t index, int32_t *value)
{
    size_t pos;

    *value = 0;
    if (index == 0) {
        return 1;
    }
    return entry_offset(names, table, index, &pos) && xxbyte_avm2_read_s32(names->data, names->size, &pos, value) == XXBYTE_STATUS_OK;
}

static int read_pool_bytes(const xxbyte_avm2_names *names, xxbyte_avm2_table table, uint32_t index, size_t width, const uint8_t **bytes)
{
    size_t pos;

    if (!entry_offset(names, table, index, &pos) || index == 0 || pos > names->size || width > names->size - pos) {
        return 0;
    }
    *bytes = names->data + pos;
    return 1;
}

static double double_at(const uint8_t *p)
{
    uint64_t bits = xxbyte_u64le(p);
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static double float_at(const uint8_t *p)
{
    uint32_t bits = xxbyte_u32le(p);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return (double)value;
}

/* Pool entry index of table as a number; 0 when out of range. */
static int text_number(const xxbyte_avm2_names *names, avm2_text *text, xxbyte_avm2_table table, uint32_t index)
{
    const uint8_t *bytes = NULL;
    int32_t value;
    uint32_t i;

    if (index != 0 && index >= names->counts[table]) {
        return 0;
    }
    switch (table) {
        case XXBYTE_AVM2_TABLE_INT:
            if (!read_pool_s32(names, table, index, &value)) {
                return 0;
            }
            text_s64(text, value);
            return 1;
        case XXBYTE_AVM2_TABLE_UINT:
            if (!read_pool_s32(names, table, index, &value)) {
                return 0;
            }
            text_u64(text, (uint32_t)value);
            return 1;
        case XXBYTE_AVM2_TABLE_DOUBLE:
            if (index == 0) {
                text_str(text, "nan");
                return 1;
            }
            if (!read_pool_bytes(names, table, index, 8, &bytes)) {
                return 0;
            }
            text_double(text, double_at(bytes), 0);
            return 1;
        case XXBYTE_AVM2_TABLE_FLOAT:
            if (index == 0) {
                text_str(text, "nan");
                return 1;
            }
            if (!read_pool_bytes(names, table, index, 4, &bytes)) {
                return 0;
            }
            text_double(text, float_at(bytes), 1);
            return 1;
        case XXBYTE_AVM2_TABLE_FLOAT4:
            if (index != 0 && !read_pool_bytes(names, table, index, 16, &bytes)) {
                return 0;
            }
            text_str(text, "float4(");
            for (i = 0; i < 4; ++i) {
                if (i != 0) {
                    text_str(text, ", ");
                }
                if (bytes == NULL) {
                    text_str(text, "nan");
                } else {
                    text_double(text, float_at(bytes + 4u * i), 1);
                }
            }
            text_char(text, ')');
            return 1;
        default: return 0;
    }
}

static void text_value(const xxbyte_avm2_names *names, avm2_text *text, uint32_t index, uint8_t kind)
{
    switch (kind) {
        case VALUE_UNDEFINED: text_str(text, "undefined"); return;
        case VALUE_TRUE: text_str(text, "true"); return;
        case VALUE_FALSE: text_str(text, "false"); return;
        case VALUE_NULL: text_str(text, "null"); return;
        case VALUE_UTF8: text_string(names, text, index); return;
        case VALUE_INT:
            if (!text_number(names, text, XXBYTE_AVM2_TABLE_INT, index)) {
                text_index(text, "int@", index);
            }
            return;
        case VALUE_UINT:
            if (!text_number(names, text, XXBYTE_AVM2_TABLE_UINT, index)) {
                text_index(text, "uint@", index);
            }
            return;
        case VALUE_DOUBLE:
            if (!text_number(names, text, XXBYTE_AVM2_TABLE_DOUBLE, index)) {
                text_index(text, "double@", index);
            }
            return;
        case VALUE_FLOAT:
            if (!text_number(names, text, XXBYTE_AVM2_TABLE_FLOAT, index)) {
                text_index(text, "float@", index);
            }
            return;
        case VALUE_FLOAT4:
            if (!text_number(names, text, XXBYTE_AVM2_TABLE_FLOAT4, index)) {
                text_index(text, "float4@", index);
            }
            return;
        default:
            if (namespace_kind_name(kind) != NULL) {
                text_namespace(names, text, index);
            } else {
                text_str(text, "value(");
                text_u64(text, index);
                text_str(text, ", 0x");
                text_char(text, hex_digits[kind >> 4]);
                text_char(text, hex_digits[kind & 15u]);
                text_char(text, ')');
            }
            return;
    }
}

/* ---- methods ---- */

static void text_class_name(const xxbyte_avm2_names *names, avm2_text *text, uint32_t class_index)
{
    size_t pos;
    uint32_t name;

    if (entry_offset(names, XXBYTE_AVM2_TABLE_INSTANCE, class_index, &pos) && read_u30(names, &pos, &name)) {
        text_vm_name(names, text, name);
    } else {
        text_index(text, "class@", class_index);
    }
}

static void text_debug_name(const xxbyte_avm2_names *names, avm2_text *text, uint32_t method)
{
    size_t pos;
    xxbyte_avm2_method_info info;
    const uint8_t *bytes;
    uint32_t length;

    if (entry_offset(names, XXBYTE_AVM2_TABLE_METHOD, method, &pos) && xxbyte_avm2_parse_method_info(names->data, names->size, pos, &info) == XXBYTE_STATUS_OK &&
        get_string(names, info.name, &bytes, &length) && length != 0) {
        text_escaped(text, bytes, length);
    } else {
        text_str(text, "<anonymous>");
    }
}

static void text_trait_name(const xxbyte_avm2_names *names, avm2_text *text, const xxbyte_avm2_method_owner *owner)
{
    if (owner->trait_kind == XXBYTE_AVM2_TRAIT_GETTER) {
        text_str(text, "/get ");
    } else if (owner->trait_kind == XXBYTE_AVM2_TRAIT_SETTER) {
        text_str(text, "/set ");
    } else {
        text_char(text, '/');
    }
    text_vm_name(names, text, owner->trait_name);
}

static int text_method_name(const xxbyte_avm2_names *names, avm2_text *text, uint32_t method)
{
    const xxbyte_avm2_method_owner *owner;

    if (method >= names->counts[XXBYTE_AVM2_TABLE_METHOD]) {
        return 0;
    }
    owner = &names->owners[method];
    switch (owner->role) {
        case XXBYTE_AVM2_OWNER_INSTANCE_INIT: text_class_name(names, text, owner->index); break;
        case XXBYTE_AVM2_OWNER_CLASS_INIT:
            text_class_name(names, text, owner->index);
            text_str(text, "$cinit");
            break;
        case XXBYTE_AVM2_OWNER_SCRIPT_INIT: text_str(text, "global$init"); break;
        case XXBYTE_AVM2_OWNER_INSTANCE_TRAIT:
            text_class_name(names, text, owner->index);
            text_trait_name(names, text, owner);
            break;
        case XXBYTE_AVM2_OWNER_CLASS_TRAIT:
            text_class_name(names, text, owner->index);
            text_char(text, '$');
            text_trait_name(names, text, owner);
            break;
        case XXBYTE_AVM2_OWNER_SCRIPT_TRAIT:
            text_str(text, "global");
            text_trait_name(names, text, owner);
            break;
        case XXBYTE_AVM2_OWNER_CLOSURE:
            text_str(text, "Function/");
            text_debug_name(names, text, method);
            break;
        default: text_debug_name(names, text, method); break;
    }
    return 1;
}

static int text_signature(const xxbyte_avm2_names *names, avm2_text *text, uint32_t method)
{
    size_t pos;
    xxbyte_avm2_method_info info;
    size_t types;
    size_t options;
    size_t param_names;
    uint32_t i;
    uint32_t first_optional;

    if (!entry_offset(names, XXBYTE_AVM2_TABLE_METHOD, method, &pos) || xxbyte_avm2_parse_method_info(names->data, names->size, pos, &info) != XXBYTE_STATUS_OK) {
        return 0;
    }
    types = info.param_types_offset;
    options = info.options_offset;
    param_names = info.param_names_offset;
    first_optional = info.option_count <= info.param_count ? info.param_count - info.option_count : 0;
    text_char(text, '(');
    for (i = 0; i < info.param_count && !text->full; ++i) {
        uint32_t type = 0;
        if (i != 0) {
            text_str(text, ", ");
        }
        if (param_names != 0) {
            uint32_t name = 0;
            if (!read_u30(names, &param_names, &name)) {
                return 0;
            }
            text_raw_string(names, text, name, 0);
            text_char(text, ':');
        }
        if (!read_u30(names, &types, &type)) {
            return 0;
        }
        text_compact(names, text, type, 0, 0);
        if ((info.flags & XXBYTE_AVM2_METHOD_HAS_OPTIONAL) != 0 && i >= first_optional) {
            uint32_t value = 0;
            uint8_t kind = 0;
            if (!read_u30(names, &options, &value) || !read_u8(names, &options, &kind)) {
                return 0;
            }
            text_str(text, " = ");
            text_value(names, text, value, kind);
        }
    }
    if ((info.flags & XXBYTE_AVM2_METHOD_NEED_REST) != 0) {
        text_str(text, info.param_count != 0 ? ", ..." : "...");
    }
    text_str(text, "):");
    text_compact(names, text, info.return_type, 0, 0);
    return 1;
}

/* ---- construction ---- */

static void set_owner(xxbyte_avm2_names *names, uint32_t method, uint32_t role, uint32_t index, const xxbyte_avm2_trait *trait)
{
    xxbyte_avm2_method_owner *owner;

    if (method >= names->counts[XXBYTE_AVM2_TABLE_METHOD] || names->owners[method].role != XXBYTE_AVM2_OWNER_NONE) {
        return;
    }
    owner = &names->owners[method];
    owner->role = role;
    owner->index = index;
    if (trait != NULL) {
        owner->trait_kind = trait->kind;
        owner->trait_name = trait->name;
        owner->disp_id = trait->id;
    }
}

/* trait_count, then the traits at *pos; method traits name their methods. */
static int walk_traits(xxbyte_avm2_names *names, size_t *pos, uint32_t role, uint32_t index)
{
    uint32_t count;
    uint32_t i;

    if (!read_u30(names, pos, &count)) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        xxbyte_avm2_trait trait;
        if (xxbyte_avm2_parse_trait(names->data, names->size, *pos, &trait) != XXBYTE_STATUS_OK) {
            return 0;
        }
        *pos += trait.size;
        switch (trait.kind) {
            case XXBYTE_AVM2_TRAIT_METHOD:
            case XXBYTE_AVM2_TRAIT_GETTER:
            case XXBYTE_AVM2_TRAIT_SETTER:
            case XXBYTE_AVM2_TRAIT_FUNCTION: set_owner(names, trait.index, role, index, &trait); break;
            default: break;
        }
    }
    return 1;
}

static int find_owners(xxbyte_avm2_names *names)
{
    uint32_t i;
    size_t pos;
    uint32_t value;

    for (i = 0; i < names->counts[XXBYTE_AVM2_TABLE_INSTANCE]; ++i) {
        uint8_t flags;
        uint32_t interface_count;
        uint32_t j;
        uint32_t init;

        pos = names->offsets[XXBYTE_AVM2_TABLE_INSTANCE][i];
        if (!read_u30(names, &pos, &value) || !read_u30(names, &pos, &value) || !read_u8(names, &pos, &flags) ||
            ((flags & 0x08u) != 0 && !read_u30(names, &pos, &value)) || !read_u30(names, &pos, &interface_count)) {
            return 0;
        }
        for (j = 0; j < interface_count; ++j) {
            if (!read_u30(names, &pos, &value)) {
                return 0;
            }
        }
        if (!read_u30(names, &pos, &init)) {
            return 0;
        }
        set_owner(names, init, XXBYTE_AVM2_OWNER_INSTANCE_INIT, i, NULL);
        if (!walk_traits(names, &pos, XXBYTE_AVM2_OWNER_INSTANCE_TRAIT, i)) {
            return 0;
        }
    }
    for (i = 0; i < names->counts[XXBYTE_AVM2_TABLE_CLASS]; ++i) {
        pos = names->offsets[XXBYTE_AVM2_TABLE_CLASS][i];
        if (!read_u30(names, &pos, &value)) {
            return 0;
        }
        set_owner(names, value, XXBYTE_AVM2_OWNER_CLASS_INIT, i, NULL);
        if (!walk_traits(names, &pos, XXBYTE_AVM2_OWNER_CLASS_TRAIT, i)) {
            return 0;
        }
    }
    for (i = 0; i < names->counts[XXBYTE_AVM2_TABLE_SCRIPT]; ++i) {
        pos = names->offsets[XXBYTE_AVM2_TABLE_SCRIPT][i];
        if (!read_u30(names, &pos, &value)) {
            return 0;
        }
        set_owner(names, value, XXBYTE_AVM2_OWNER_SCRIPT_INIT, i, NULL);
        if (!walk_traits(names, &pos, XXBYTE_AVM2_OWNER_SCRIPT_TRAIT, i)) {
            return 0;
        }
    }
    return 1;
}

/* Functions created by newfunction: a linear sweep of every body. */
static void find_closures(xxbyte_avm2_names *names)
{
    size_t b;

    for (b = 0; b < names->body_count; ++b) {
        const xxbyte_avm2_method_body *body = &names->bodies[b];
        const uint8_t *code = names->data + body->code_offset;
        size_t offset = 0;

        while (offset < body->code_length) {
            xxbyte_instruction insn;
            if (xxbyte_decode(XXBYTE_FAMILY_AVM2, names->abc.mode, code + offset, body->code_length - offset, offset, &insn) != XXBYTE_STATUS_OK) {
                ++offset;
                continue;
            }
            if (insn.id == XXBYTE_AVM2_ID_NEWFUNCTION && insn.operand_count == 1) {
                set_owner(names, (uint32_t)insn.operands[0].value, XXBYTE_AVM2_OWNER_CLOSURE, body->method, NULL);
            }
            offset += insn.size;
        }
    }
}

typedef struct private_entry {
    const uint8_t *bytes; /* NULL for a namespace without a name */
    uint32_t length;
    uint32_t index;
} private_entry;

static int compare_private(const void *a, const void *b)
{
    const private_entry *x = (const private_entry *)a;
    const private_entry *y = (const private_entry *)b;
    int c;

    if ((x->bytes == NULL) != (y->bytes == NULL)) {
        return x->bytes == NULL ? -1 : 1;
    }
    if (x->length != y->length) {
        return x->length < y->length ? -1 : 1;
    }
    c = x->length != 0 && x->bytes != NULL ? memcmp(x->bytes, y->bytes, x->length) : 0;
    if (c != 0) {
        return c;
    }
    return x->index < y->index ? -1 : (x->index > y->index ? 1 : 0);
}

static int same_private(const private_entry *x, const private_entry *y)
{
    return (x->bytes == NULL) == (y->bytes == NULL) && x->length == y->length && (x->length == 0 || memcmp(x->bytes, y->bytes, x->length) == 0);
}

static int find_shared_private(xxbyte_avm2_names *names)
{
    size_t count = names->counts[XXBYTE_AVM2_TABLE_NAMESPACE];
    private_entry *entries;
    size_t used = 0;
    uint32_t i;
    size_t j;

    if (count == 0) {
        return 1;
    }
    names->private_shared = (uint8_t *)xx_mem_calloc(count, 1);
    entries = (private_entry *)xx_mem_calloc(count, sizeof(private_entry));
    if (names->private_shared == NULL || entries == NULL) {
        xx_mem_free(entries);
        return 0;
    }
    for (i = 1; i < count; ++i) {
        uint8_t kind;
        uint32_t name;
        if (get_namespace(names, i, &kind, &name) && kind == XXBYTE_AVM2_NS_PRIVATE) {
            private_entry *e = &entries[used++];
            e->index = i;
            if (!get_string(names, name, &e->bytes, &e->length)) {
                e->bytes = NULL;
                e->length = 0;
            }
        }
    }
    if (used > 1) {
        qsort(entries, used, sizeof(private_entry), compare_private);
        for (j = 1; j < used; ++j) {
            if (same_private(&entries[j - 1], &entries[j])) {
                names->private_shared[entries[j - 1].index] = 1;
                names->private_shared[entries[j].index] = 1;
            }
        }
    }
    xx_mem_free(entries);
    return 1;
}

void xxbyte_avm2_names_destroy(xxbyte_avm2_names *names)
{
    int t;

    if (names == NULL) {
        return;
    }
    for (t = 0; t < (int)XXBYTE_AVM2_TABLE_COUNT; ++t) {
        xx_mem_free(names->offsets[t]);
    }
    xx_mem_free(names->bodies);
    xx_mem_free(names->body_of);
    xx_mem_free(names->owners);
    xx_mem_free(names->private_shared);
    xx_mem_free(names);
}

xxbyte_status xxbyte_avm2_names_create(const uint8_t *data, size_t size, xxbyte_avm2_names **result)
{
    xxbyte_avm2_names *names;
    xxbyte_status status;
    int t;
    size_t i;
    size_t method_count;

    if (result == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *result = NULL;
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    names = (xxbyte_avm2_names *)xx_mem_calloc(1, sizeof(*names));
    if (names == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    names->data = data;
    status = xxbyte_avm2_parse_abc(data, size, &names->abc);
    if (status != XXBYTE_STATUS_OK) {
        xx_mem_free(names);
        return status;
    }
    names->size = names->abc.size;
    for (t = 0; t < (int)XXBYTE_AVM2_TABLE_COUNT; ++t) {
        size_t count = 0;
        if (t == (int)XXBYTE_AVM2_TABLE_METHOD_BODY || names->abc.tables[t].count == 0) {
            continue;
        }
        names->offsets[t] = (uint32_t *)xx_mem_calloc(names->abc.tables[t].count, sizeof(uint32_t));
        if (names->offsets[t] == NULL) {
            xxbyte_avm2_names_destroy(names);
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        status = xxbyte_avm2_table_offsets(data, names->size, &names->abc, (xxbyte_avm2_table)t, names->offsets[t], names->abc.tables[t].count, &count);
        if (status != XXBYTE_STATUS_OK) {
            xxbyte_avm2_names_destroy(names);
            return status;
        }
        names->counts[t] = count;
    }
    names->body_count = names->abc.tables[XXBYTE_AVM2_TABLE_METHOD_BODY].count;
    method_count = names->counts[XXBYTE_AVM2_TABLE_METHOD];
    names->bodies = (xxbyte_avm2_method_body *)xx_mem_calloc(names->body_count + 1u, sizeof(xxbyte_avm2_method_body));
    names->body_of = (uint32_t *)xx_mem_calloc(method_count + 1u, sizeof(uint32_t));
    names->owners = (xxbyte_avm2_method_owner *)xx_mem_calloc(method_count + 1u, sizeof(xxbyte_avm2_method_owner));
    if (names->bodies == NULL || names->body_of == NULL || names->owners == NULL) {
        xxbyte_avm2_names_destroy(names);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_avm2_parse_method_bodies(data, names->size, &names->abc, names->bodies, names->body_count, &i);
    if (status != XXBYTE_STATUS_OK) {
        xxbyte_avm2_names_destroy(names);
        return status;
    }
    for (i = 0; i < method_count; ++i) {
        names->body_of[i] = (uint32_t)names->body_count;
    }
    for (i = names->body_count; i-- > 0;) {
        if (names->bodies[i].method < method_count) {
            names->body_of[names->bodies[i].method] = (uint32_t)i;
        }
    }
    names->selected = (uint32_t)names->body_count;
    if (!find_owners(names)) {
        xxbyte_avm2_names_destroy(names);
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    find_closures(names);
    if (!find_shared_private(names)) {
        xxbyte_avm2_names_destroy(names);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    *result = names;
    return XXBYTE_STATUS_OK;
}

const xxbyte_avm2_abc *xxbyte_avm2_names_abc(const xxbyte_avm2_names *names)
{
    return names != NULL ? &names->abc : NULL;
}

const xxbyte_avm2_method_body *xxbyte_avm2_names_bodies(const xxbyte_avm2_names *names, size_t *count)
{
    if (count != NULL) {
        *count = names != NULL ? names->body_count : 0;
    }
    return names != NULL ? names->bodies : NULL;
}

uint32_t xxbyte_avm2_names_body_of(const xxbyte_avm2_names *names, uint32_t method)
{
    if (names == NULL) {
        return 0;
    }
    return method < names->counts[XXBYTE_AVM2_TABLE_METHOD] ? names->body_of[method] : (uint32_t)names->body_count;
}

xxbyte_status xxbyte_avm2_names_select_body(xxbyte_avm2_names *names, uint32_t body)
{
    if (names == NULL || body > names->body_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    names->selected = body;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm2_names_owner(const xxbyte_avm2_names *names, uint32_t method, xxbyte_avm2_method_owner *owner)
{
    if (owner != NULL) {
        memset(owner, 0, sizeof(*owner));
    }
    if (names == NULL || owner == NULL || method >= names->counts[XXBYTE_AVM2_TABLE_METHOD]) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *owner = names->owners[method];
    return XXBYTE_STATUS_OK;
}

/* ---- public formatters ---- */

size_t xxbyte_avm2_format_string(const xxbyte_avm2_names *names, uint32_t index, char *buffer, size_t buffer_size)
{
    avm2_text text;
    const uint8_t *bytes;
    uint32_t length;

    if (buffer == NULL) {
        buffer_size = 0;
    }
    text_init(&text, buffer, buffer_size);
    if (names == NULL || (index != 0 && !get_string(names, index, &bytes, &length))) {
        return text_fail(&text);
    }
    text_string(names, &text, index);
    return text_finish(&text);
}

size_t xxbyte_avm2_format_namespace(const xxbyte_avm2_names *names, uint32_t index, char *buffer, size_t buffer_size)
{
    avm2_text text;
    uint8_t kind;
    uint32_t name;

    if (buffer == NULL) {
        buffer_size = 0;
    }
    text_init(&text, buffer, buffer_size);
    if (names == NULL || (index != 0 && (!get_namespace(names, index, &kind, &name) || namespace_kind_name(kind) == NULL))) {
        return text_fail(&text);
    }
    text_namespace(names, &text, index);
    return text_finish(&text);
}

size_t xxbyte_avm2_format_ns_set(const xxbyte_avm2_names *names, uint32_t index, char *buffer, size_t buffer_size)
{
    avm2_text text;
    size_t pos;

    if (buffer == NULL) {
        buffer_size = 0;
    }
    text_init(&text, buffer, buffer_size);
    if (names == NULL || index == 0 || !entry_offset(names, XXBYTE_AVM2_TABLE_NS_SET, index, &pos)) {
        return text_fail(&text);
    }
    text_ns_set(names, &text, index);
    return text_finish(&text);
}

size_t xxbyte_avm2_format_multiname(const xxbyte_avm2_names *names, uint32_t index, char *buffer, size_t buffer_size)
{
    avm2_text text;
    avm2_mn mn;

    if (buffer == NULL) {
        buffer_size = 0;
    }
    text_init(&text, buffer, buffer_size);
    if (names == NULL || (index != 0 && !get_multiname(names, index, &mn))) {
        return text_fail(&text);
    }
    text_multiname(names, &text, index, 0);
    return text_finish(&text);
}

size_t xxbyte_avm2_format_compact(const xxbyte_avm2_names *names, uint32_t index, char *buffer, size_t buffer_size)
{
    avm2_text text;
    avm2_mn mn;

    if (buffer == NULL) {
        buffer_size = 0;
    }
    text_init(&text, buffer, buffer_size);
    if (names == NULL || (index != 0 && !get_multiname(names, index, &mn))) {
        return text_fail(&text);
    }
    text_compact(names, &text, index, 0, 0);
    return text_finish(&text);
}

size_t xxbyte_avm2_format_value(const xxbyte_avm2_names *names, uint32_t value_index, uint8_t value_kind, char *buffer, size_t buffer_size)
{
    avm2_text text;

    if (buffer == NULL) {
        buffer_size = 0;
    }
    text_init(&text, buffer, buffer_size);
    if (names == NULL) {
        return text_fail(&text);
    }
    text_value(names, &text, value_index, value_kind);
    return text_finish(&text);
}

size_t xxbyte_avm2_method_name(const xxbyte_avm2_names *names, uint32_t method, char *buffer, size_t buffer_size)
{
    avm2_text text;

    if (buffer == NULL) {
        buffer_size = 0;
    }
    text_init(&text, buffer, buffer_size);
    if (names == NULL || !text_method_name(names, &text, method)) {
        return text_fail(&text);
    }
    return text_finish(&text);
}

size_t xxbyte_avm2_method_signature(const xxbyte_avm2_names *names, uint32_t method, char *buffer, size_t buffer_size)
{
    avm2_text text;

    if (buffer == NULL) {
        buffer_size = 0;
    }
    text_init(&text, buffer, buffer_size);
    if (names == NULL || !text_signature(names, &text, method)) {
        return text_fail(&text);
    }
    return text_finish(&text);
}

int xxbyte_avm2_append_quoted(xx_buf_t *out, const uint8_t *bytes, size_t length)
{
    char chunk[512];
    avm2_text text;
    size_t i = 0;

    if (!xx_buf_append_char(out, '"')) {
        return 0;
    }
    /* Escape in pieces; a piece ends before a character that does not fit,
     * so no character is cut. */
    while (i < length) {
        size_t piece = length - i < 64 ? length - i : 64;
        size_t end = i + piece;
        /* Extend the piece to the end of the character it cuts. */
        while (end < length && (bytes[end] & 0xC0u) == 0x80u && end - i < 68) {
            ++end;
        }
        text_init(&text, chunk, sizeof(chunk));
        text_escaped(&text, bytes + i, end - i);
        if (!xx_buf_append(out, chunk, text.length)) {
            return 0;
        }
        i = end;
    }
    return xx_buf_append_char(out, '"') ? 1 : 0;
}

/* ---- the resolver ---- */

size_t xxbyte_avm2_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_avm2_names *names = (const xxbyte_avm2_names *)context;
    const xxbyte_operand *operand;
    avm2_text text;
    uint32_t index;

    if (buffer == NULL) {
        buffer_size = 0;
    }
    text_init(&text, buffer, buffer_size);
    if (names == NULL || instruction == NULL || operand_index >= instruction->operand_count || instruction->family != XXBYTE_FAMILY_AVM2) {
        return text_fail(&text);
    }
    operand = &instruction->operands[operand_index];
    if (operand->type != XXBYTE_OPERAND_INDEX || operand->value < 0 || operand->value > (int64_t)UINT32_MAX) {
        return text_fail(&text);
    }
    index = (uint32_t)operand->value;
    switch (operand->kind) {
        case XXBYTE_INDEX_STRING:
            if (index != 0 && index >= names->counts[XXBYTE_AVM2_TABLE_STRING]) {
                return text_fail(&text);
            }
            text_string(names, &text, index);
            break;
        case XXBYTE_AVM2_INDEX_INT:
            if (!text_number(names, &text, XXBYTE_AVM2_TABLE_INT, index)) {
                return text_fail(&text);
            }
            break;
        case XXBYTE_AVM2_INDEX_UINT:
            if (!text_number(names, &text, XXBYTE_AVM2_TABLE_UINT, index)) {
                return text_fail(&text);
            }
            break;
        case XXBYTE_AVM2_INDEX_DOUBLE:
            if (!text_number(names, &text, XXBYTE_AVM2_TABLE_DOUBLE, index)) {
                return text_fail(&text);
            }
            break;
        case XXBYTE_AVM2_INDEX_FLOAT:
            if (!text_number(names, &text, XXBYTE_AVM2_TABLE_FLOAT, index)) {
                return text_fail(&text);
            }
            break;
        case XXBYTE_AVM2_INDEX_FLOAT4:
            if (!text_number(names, &text, XXBYTE_AVM2_TABLE_FLOAT4, index)) {
                return text_fail(&text);
            }
            break;
        case XXBYTE_AVM2_INDEX_NAMESPACE: {
            uint8_t kind;
            uint32_t name;
            if (index != 0 && (!get_namespace(names, index, &kind, &name) || namespace_kind_name(kind) == NULL)) {
                return text_fail(&text);
            }
            text_namespace(names, &text, index);
            break;
        }
        case XXBYTE_AVM2_INDEX_MULTINAME: {
            avm2_mn mn;
            if (index != 0 && !get_multiname(names, index, &mn)) {
                return text_fail(&text);
            }
            text_multiname(names, &text, index, 0);
            break;
        }
        case XXBYTE_INDEX_METHOD:
            if (index >= names->counts[XXBYTE_AVM2_TABLE_METHOD]) {
                return text_fail(&text);
            }
            text_str(&text, "method(");
            text_u64(&text, index);
            text_str(&text, ", \"");
            text_method_name(names, &text, index);
            text_str(&text, "\")");
            break;
        case XXBYTE_INDEX_CLASS: {
            size_t pos;
            uint32_t name;
            if (!entry_offset(names, XXBYTE_AVM2_TABLE_INSTANCE, index, &pos) || !read_u30(names, &pos, &name)) {
                return text_fail(&text);
            }
            text_str(&text, "class(");
            text_u64(&text, index);
            text_str(&text, ", ");
            text_multiname(names, &text, name, 0);
            text_char(&text, ')');
            break;
        }
        case XXBYTE_AVM2_INDEX_EXCEPTION: {
            const xxbyte_avm2_method_body *body;
            xxbyte_avm2_exception exceptions[1];
            size_t count = 0;
            size_t pos;
            uint32_t i;
            if (names->selected >= names->body_count) {
                return text_fail(&text);
            }
            body = &names->bodies[names->selected];
            if (index >= body->exception_count) {
                return text_fail(&text);
            }
            /* Skip to entry index: five u30s each. */
            pos = body->exception_offset;
            for (i = 0; i < 5u * index; ++i) {
                uint32_t skip;
                if (!read_u30(names, &pos, &skip)) {
                    return text_fail(&text);
                }
            }
            {
                xxbyte_avm2_method_body one = *body;
                one.exception_offset = (uint32_t)pos;
                one.exception_count = 1;
                if (xxbyte_avm2_parse_exceptions(names->data, names->size, &one, exceptions, 1, &count) != XXBYTE_STATUS_OK || count != 1) {
                    return text_fail(&text);
                }
            }
            text_str(&text, "exception(");
            text_u64(&text, index);
            text_str(&text, ", ");
            text_multiname(names, &text, exceptions[0].exc_type, 0);
            text_char(&text, ')');
            break;
        }
        default: return text_fail(&text);
    }
    return text_finish(&text);
}

#else
typedef int xxbyte_avm2_names_disabled;
#endif
