/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_internal.h"

#if XXBYTE_USE_OCAML

#include "xxbyte/xxbyte_xxfc.h"
#include "xxbyte_ocaml_internal.h"

#include <xxfclib/memory/xx_memory.h>

/*
 * Container listing for OCaml bytecode: executables (section table
 * trailer), .cmo objects and .cma libraries. Their descriptors are OCaml
 * values written by output_value; a small reader of the Marshal format
 * (runtime/intern.c, caml/intext.h) turns them into a node array.
 */

/* ---- Marshal reader ---------------------------------------------------- */

enum {
    ML_INT,
    ML_BLOCK,
    ML_STRING,
    ML_DOUBLE,
    ML_DOUBLE_ARRAY,
    ML_INT32,
    ML_INT64,
    ML_NATIVEINT,
    ML_CODE
};

#define ML_NONE UINT32_MAX
#define ML_NODE_LIMIT (64u * 1024u * 1024u)

typedef struct ml_node {
    uint8_t kind;
    uint8_t tag;   /* ML_BLOCK */
    uint32_t size; /* fields, string bytes or doubles */
    uint64_t at;   /* ML_BLOCK: first child in kids; ML_STRING and
                      ML_DOUBLE_ARRAY: offset of the bytes in data */
    int64_t value; /* integers; the bits of a double */
} ml_node;

typedef struct ml_value {
    const uint8_t *data; /* the marshalled bytes (header included) */
    size_t size;
    ml_node *nodes;
    size_t node_count;
    size_t node_capacity;
    uint32_t *kids;
    size_t kid_count;
    size_t kid_capacity;
    uint32_t *objects; /* the shared-reference table */
    size_t object_count;
    size_t object_capacity;
    uint32_t root;
} ml_value;

typedef struct ml_frame {
    size_t next; /* the next child slot in kids */
    size_t left;
} ml_frame;

static void ml_free(ml_value *v)
{
    xx_mem_free(v->nodes);
    xx_mem_free(v->kids);
    xx_mem_free(v->objects);
    memset(v, 0, sizeof(*v));
    v->root = ML_NONE;
}

static int ml_grow(void **array, size_t *capacity, size_t need, size_t item)
{
    size_t n = *capacity == 0 ? 64u : *capacity;
    void *p;

    if (need <= *capacity) {
        return 1;
    }
    while (n < need) {
        n *= 2u;
    }
    if (n > ML_NODE_LIMIT * 2u) {
        return 0;
    }
    p = xx_mem_realloc(*array, n * item);
    if (p == NULL) {
        return 0;
    }
    *array = p;
    *capacity = n;
    return 1;
}

static uint32_t ml_new(ml_value *v, uint8_t kind, int record)
{
    ml_node *node;

    if (v->node_count >= ML_NODE_LIMIT || !ml_grow((void **)&v->nodes, &v->node_capacity, v->node_count + 1u, sizeof(ml_node))) {
        return ML_NONE;
    }
    node = &v->nodes[v->node_count];
    memset(node, 0, sizeof(*node));
    node->kind = kind;
    if (record) {
        if (!ml_grow((void **)&v->objects, &v->object_capacity, v->object_count + 1u, sizeof(uint32_t))) {
            return ML_NONE;
        }
        v->objects[v->object_count++] = (uint32_t)v->node_count;
    }
    return (uint32_t)v->node_count++;
}

static uint64_t ml_be(const uint8_t *p, unsigned bytes)
{
    uint64_t r = 0;
    unsigned i;

    for (i = 0; i < bytes; ++i) {
        r = (r << 8) | p[i];
    }
    return r;
}

/* Reads one custom block's payload (_i, _j, _n, the ones the compiler's
 * descriptors hold). Returns the node or ML_NONE. */
static uint32_t ml_custom(ml_value *v, size_t *pos, size_t end, uint8_t code)
{
    const uint8_t *name = v->data + *pos;
    const uint8_t *nul = (const uint8_t *)memchr(name, 0, end - *pos);
    size_t p;
    uint32_t n;
    uint8_t kind;
    uint64_t bits;
    unsigned width;

    if (nul == NULL) {
        return ML_NONE;
    }
    p = (size_t)(nul - v->data) + 1u;
    if (nul - name != 2 || name[0] != '_') {
        return ML_NONE;
    }
    if (code == 0x18) { /* CODE_CUSTOM_LEN: size_32, size_64 */
        if (end - p < 12u) {
            return ML_NONE;
        }
        p += 12u;
    }
    switch (name[1]) {
        case 'i':
            kind = ML_INT32;
            width = 4;
            break;
        case 'j':
            kind = ML_INT64;
            width = 8;
            break;
        case 'n':
            kind = ML_NATIVEINT;
            if (p >= end) {
                return ML_NONE;
            }
            width = v->data[p] == 1 ? 4u : v->data[p] == 2 ? 8u : 0u;
            ++p;
            if (width == 0) {
                return ML_NONE;
            }
            break;
        default: return ML_NONE;
    }
    if (end - p < width) {
        return ML_NONE;
    }
    bits = ml_be(v->data + p, width);
    n = ml_new(v, kind, 1);
    if (n == ML_NONE) {
        return ML_NONE;
    }
    v->nodes[n].value = width == 4 ? (int64_t)(int32_t)(uint32_t)bits : (int64_t)bits;
    *pos = p + width;
    return n;
}

/* Parses the item at *pos. A block with fields reserves its child slots
 * and pushes a frame. Returns the node or ML_NONE. */
static uint32_t ml_item(ml_value *v, size_t *pos, size_t end, ml_frame **stack, size_t *depth, size_t *stack_capacity)
{
    for (;;) {
        uint8_t code;
        uint64_t len;
        uint64_t size;
        uint32_t tag;
        uint32_t n;

        if (*pos >= end) {
            return ML_NONE;
        }
        code = v->data[(*pos)++];
        if (code >= 0x80) {
            tag = code & 0x0Fu;
            size = (code >> 4) & 0x07u;
            goto block;
        }
        if (code >= 0x40) {
            n = ml_new(v, ML_INT, 0);
            if (n != ML_NONE) {
                v->nodes[n].value = code & 0x3F;
            }
            return n;
        }
        if (code >= 0x20) {
            len = code & 0x1Fu;
            goto string;
        }
        switch (code) {
            case 0x00:
            case 0x01:
            case 0x02:
            case 0x03: {
                unsigned width = 1u << code;
                if (end - *pos < width) {
                    return ML_NONE;
                }
                n = ml_new(v, ML_INT, 0);
                if (n != ML_NONE) {
                    v->nodes[n].value = xxbyte_sext(ml_be(v->data + *pos, width), width * 8u);
                }
                *pos += width;
                return n;
            }
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x14: {
                unsigned width = code == 0x04 ? 1u : code == 0x05 ? 2u : code == 0x06 ? 4u : 8u;
                uint64_t ofs;
                if (end - *pos < width) {
                    return ML_NONE;
                }
                ofs = ml_be(v->data + *pos, width);
                *pos += width;
                if (ofs == 0 || ofs > v->object_count) {
                    return ML_NONE;
                }
                return v->objects[v->object_count - ofs];
            }
            case 0x08:
            case 0x13: {
                unsigned width = code == 0x08 ? 4u : 8u;
                uint64_t header;
                if (end - *pos < width) {
                    return ML_NONE;
                }
                header = ml_be(v->data + *pos, width);
                *pos += width;
                tag = (uint32_t)(header & 0xFFu);
                size = header >> 10;
                if (tag >= 251u) {
                    return ML_NONE;
                }
                goto block;
            }
            case 0x09:
            case 0x0A:
            case 0x15: {
                unsigned width = code == 0x09 ? 1u : code == 0x0A ? 4u : 8u;
                if (end - *pos < width) {
                    return ML_NONE;
                }
                len = ml_be(v->data + *pos, width);
                *pos += width;
                goto string;
            }
            case 0x0B:
            case 0x0C:
                if (end - *pos < 8u) {
                    return ML_NONE;
                }
                n = ml_new(v, ML_DOUBLE, 1);
                if (n != ML_NONE) {
                    v->nodes[n].value = (int64_t)(code == 0x0B ? ml_be(v->data + *pos, 8) : xxbyte_u64le(v->data + *pos));
                    v->nodes[n].tag = code == 0x0B; /* big-endian */
                }
                *pos += 8u;
                return n;
            case 0x0D:
            case 0x0E:
            case 0x0F:
            case 0x07:
            case 0x16:
            case 0x17: {
                unsigned width = (code == 0x0D || code == 0x0E) ? 1u : (code == 0x0F || code == 0x07) ? 4u : 8u;
                if (end - *pos < width) {
                    return ML_NONE;
                }
                len = ml_be(v->data + *pos, width);
                *pos += width;
                if (len == 0 || len > (end - *pos) / 8u) {
                    return ML_NONE;
                }
                n = ml_new(v, ML_DOUBLE_ARRAY, 1);
                if (n != ML_NONE) {
                    v->nodes[n].size = (uint32_t)len;
                    v->nodes[n].at = *pos;
                    v->nodes[n].tag = (code == 0x0D || code == 0x0F || code == 0x16);
                }
                *pos += (size_t)len * 8u;
                return n;
            }
            case 0x10: /* CODE_CODEPOINTER: offset, digest */
                if (end - *pos < 20u) {
                    return ML_NONE;
                }
                *pos += 20u;
                return ml_new(v, ML_CODE, 0);
            case 0x11: /* CODE_INFIXPOINTER: offset, then the closure */
                if (end - *pos < 4u) {
                    return ML_NONE;
                }
                *pos += 4u;
                continue;
            case 0x12:
            case 0x18:
            case 0x19: return ml_custom(v, pos, end, code);
            default: return ML_NONE;
        }
    string:
        if (len > end - *pos || len > UINT32_MAX) {
            return ML_NONE;
        }
        n = ml_new(v, ML_STRING, 1);
        if (n != ML_NONE) {
            v->nodes[n].size = (uint32_t)len;
            v->nodes[n].at = *pos;
        }
        *pos += (size_t)len;
        return n;
    block:
        if (size == 0) {
            n = ml_new(v, ML_BLOCK, 0);
            if (n != ML_NONE) {
                v->nodes[n].tag = (uint8_t)tag;
            }
            return n;
        }
        /* Every field takes at least one byte. */
        if (size > end - *pos || size > UINT32_MAX) {
            return ML_NONE;
        }
        n = ml_new(v, ML_BLOCK, 1);
        if (n == ML_NONE || !ml_grow((void **)&v->kids, &v->kid_capacity, v->kid_count + (size_t)size, sizeof(uint32_t)) ||
            !ml_grow((void **)stack, stack_capacity, *depth + 1u, sizeof(ml_frame))) {
            return ML_NONE;
        }
        v->nodes[n].tag = (uint8_t)tag;
        v->nodes[n].size = (uint32_t)size;
        v->nodes[n].at = v->kid_count;
        memset(v->kids + v->kid_count, 0xFF, (size_t)size * sizeof(uint32_t));
        (*stack)[*depth].next = v->kid_count;
        (*stack)[*depth].left = (size_t)size;
        ++*depth;
        v->kid_count += (size_t)size;
        return n;
    }
}

/*
 * Reads the marshalled value at data[offset..size). Returns OK,
 * TRUNCATED when it runs past the end, INVALID_INSTRUCTION when it is
 * malformed or compressed, OUT_OF_MEMORY.
 */
static xxbyte_status ml_read(const uint8_t *data, size_t size, size_t offset, ml_value *v)
{
    uint32_t magic;
    uint64_t data_len;
    size_t header;
    size_t pos;
    size_t end;
    ml_frame *stack = NULL;
    size_t depth = 0;
    size_t stack_capacity = 0;
    xxbyte_status status = XXBYTE_STATUS_OK;

    memset(v, 0, sizeof(*v));
    v->root = ML_NONE;
    if (offset > size || size - offset < 20u) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    magic = xxbyte_u32be(data + offset);
    if (magic == 0x8495A6BEu) {
        header = 20;
        data_len = xxbyte_u32be(data + offset + 4);
    } else if (magic == 0x8495A6BFu) {
        if (size - offset < 32u) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        header = 32;
        data_len = ml_be(data + offset + 8, 8);
    } else {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (data_len > size - offset - header) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    v->data = data;
    v->size = size;
    pos = offset + header;
    end = pos + (size_t)data_len;
    v->root = ml_item(v, &pos, end, &stack, &depth, &stack_capacity);
    if (v->root == ML_NONE) {
        status = XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    while (status == XXBYTE_STATUS_OK && depth != 0) {
        ml_frame *frame = &stack[depth - 1u];
        size_t slot;
        uint32_t n;
        if (frame->left == 0) {
            --depth;
            continue;
        }
        slot = frame->next++;
        --frame->left;
        n = ml_item(v, &pos, end, &stack, &depth, &stack_capacity);
        if (n == ML_NONE) {
            status = XXBYTE_STATUS_INVALID_INSTRUCTION;
            break;
        }
        v->kids[slot] = n;
    }
    xx_mem_free(stack);
    if (status != XXBYTE_STATUS_OK) {
        ml_free(v);
    }
    return status;
}

static const ml_node *ml_get(const ml_value *v, uint32_t n)
{
    return (v->nodes != NULL && n < v->node_count) ? &v->nodes[n] : NULL;
}

/* Field i of block n, or ML_NONE. */
static uint32_t ml_field(const ml_value *v, uint32_t n, uint32_t i)
{
    const ml_node *node = ml_get(v, n);

    if (node == NULL || node->kind != ML_BLOCK || i >= node->size) {
        return ML_NONE;
    }
    return v->kids[node->at + i];
}

static int ml_int(const ml_value *v, uint32_t n, int64_t *value)
{
    const ml_node *node = ml_get(v, n);

    if (node == NULL || node->kind != ML_INT) {
        return 0;
    }
    *value = node->value;
    return 1;
}

static int ml_string(const ml_value *v, uint32_t n, const char **text, size_t *length)
{
    const ml_node *node = ml_get(v, n);

    if (node == NULL || node->kind != ML_STRING) {
        return 0;
    }
    *text = (const char *)(v->data + node->at);
    *length = node->size;
    return 1;
}

/* The first string field of block n (an Ident.t or a Symtable.Global.t),
 * or n itself when it is a string. */
static int ml_name(const ml_value *v, uint32_t n, const char **text, size_t *length)
{
    const ml_node *node = ml_get(v, n);
    uint32_t i;

    if (ml_string(v, n, text, length)) {
        return 1;
    }
    if (node == NULL || node->kind != ML_BLOCK) {
        return 0;
    }
    for (i = 0; i < node->size && i < 3u; ++i) {
        if (ml_string(v, v->kids[node->at + i], text, length)) {
            return 1;
        }
    }
    return 0;
}

/* ---- Value printing ---------------------------------------------------- */

#define PRINT_LIMIT 200u

typedef struct printer {
    xx_buf_t *out;
    size_t start;
    const ml_value *v;
} printer;

static int full(const printer *p)
{
    return p->out->size - p->start > PRINT_LIMIT;
}

/* One character as String.escaped and Char.escaped write it: printable
 * ASCII as is, the usual backslash escapes, \ddd otherwise. quote is the
 * delimiter that needs a backslash. */
static void print_escaped(printer *p, unsigned char c, char quote)
{
    switch (c) {
        case '\\': xx_buf_append_str(p->out, "\\\\"); break;
        case '\n': xx_buf_append_str(p->out, "\\n"); break;
        case '\t': xx_buf_append_str(p->out, "\\t"); break;
        case '\r': xx_buf_append_str(p->out, "\\r"); break;
        case '\b': xx_buf_append_str(p->out, "\\b"); break;
        default:
            if (c == (unsigned char)quote) {
                xx_buf_append_char(p->out, '\\');
                xx_buf_append_char(p->out, quote);
            } else if (c >= 0x20 && c < 0x7F) {
                xx_buf_append_char(p->out, (char)c);
            } else {
                xx_buf_appendf(p->out, "\\%03u", (unsigned)c);
            }
            break;
    }
}

/* OCaml's %S. */
static void print_quoted(printer *p, const char *text, size_t length)
{
    size_t i;

    xx_buf_append_char(p->out, '"');
    for (i = 0; i < length; ++i) {
        if (full(p)) {
            xx_buf_append_str(p->out, "...");
            break;
        }
        print_escaped(p, (unsigned char)text[i], '"');
    }
    xx_buf_append_char(p->out, '"');
}

/* The shortest round-trip text, with a '.' when it would read as an
 * integer ("3.5", "100.", "1e+16", "nan"), as string_of_float does. */
static void print_double(printer *p, uint64_t bits)
{
    xxbyte_fmt fmt;
    double d;
    size_t start = p->out->size;
    size_t i;

    memcpy(&d, &bits, sizeof(d));
    fmt.out = p->out;
    fmt.flags = 0;
    fmt.resolver = NULL;
    fmt.context = NULL;
    if (!xxbyte_fmt_double(&fmt, d) || p->out->data == NULL) {
        return;
    }
    for (i = start; i < p->out->size; ++i) {
        char c = p->out->data[i];
        if (c == '.' || c == 'e' || c == 'n' || c == 'i') {
            return;
        }
    }
    xx_buf_append_char(p->out, '.');
}

static uint64_t array_double(const ml_value *v, const ml_node *node, uint32_t i)
{
    const uint8_t *q = v->data + node->at + (size_t)i * 8u;
    return node->tag ? ml_be(q, 8) : xxbyte_u64le(q);
}

/* A raw value (an executable's DATA slot, a 5.1+ Reloc_literal), as
 * tools/dumpobj's print_obj shows it. */
static void print_obj(printer *p, uint32_t n, unsigned depth)
{
    const ml_node *node = ml_get(p->v, n);
    uint32_t i;

    if (node == NULL || depth > 8u) {
        xx_buf_append_str(p->out, "...");
        return;
    }
    switch (node->kind) {
        case ML_INT: xx_buf_appendf(p->out, "%lld", (long long)node->value); break;
        case ML_STRING: print_quoted(p, (const char *)(p->v->data + node->at), node->size); break;
        case ML_DOUBLE: print_double(p, (uint64_t)node->value); break;
        case ML_DOUBLE_ARRAY:
            xx_buf_append_str(p->out, "[|");
            for (i = 0; i < node->size; ++i) {
                if (i != 0) {
                    xx_buf_append_str(p->out, ", ");
                }
                if (full(p)) {
                    xx_buf_append_str(p->out, "...");
                    break;
                }
                print_double(p, array_double(p->v, node, i));
            }
            xx_buf_append_str(p->out, "|]");
            break;
        case ML_INT32: xx_buf_appendf(p->out, "%lldl", (long long)node->value); break;
        case ML_INT64: xx_buf_appendf(p->out, "%lldL", (long long)node->value); break;
        case ML_NATIVEINT: xx_buf_appendf(p->out, "%lldn", (long long)node->value); break;
        case ML_CODE: xx_buf_append_str(p->out, "<code>"); break;
        default:
            xx_buf_appendf(p->out, "<%u>", (unsigned)node->tag);
            if (node->size == 0) {
                break;
            }
            xx_buf_append_char(p->out, '(');
            for (i = 0; i < node->size; ++i) {
                if (i != 0) {
                    xx_buf_append_str(p->out, ", ");
                }
                if (full(p)) {
                    xx_buf_append_str(p->out, "...");
                    break;
                }
                print_obj(p, p->v->kids[node->at + i], depth + 1u);
            }
            xx_buf_append_char(p->out, ')');
            break;
    }
}

/* Lambda.structured_constant (the Reloc_literal of 5.0 and earlier):
 * Const_base, [Const_pointer before 4.12,] Const_block, Const_float_array,
 * Const_immstring. */
static void print_struct_const(printer *p, uint32_t n, int pointer, unsigned depth)
{
    const ml_node *node = ml_get(p->v, n);
    uint32_t arg = ml_field(p->v, n, 0);
    int tag;
    const char *text;
    size_t length;
    int64_t value;

    if (node == NULL || depth > 8u || node->kind != ML_BLOCK || node->size == 0) {
        if (node != NULL && node->kind != ML_BLOCK) {
            print_obj(p, n, depth);
        } else {
            xx_buf_append_str(p->out, "...");
        }
        return;
    }
    tag = node->tag;
    if (!pointer && tag >= 1) {
        ++tag; /* no Const_pointer from 4.12 */
    }
    switch (tag) {
        case 0: { /* Const_base of Asttypes.constant */
            const ml_node *base = ml_get(p->v, arg);
            uint32_t inner = ml_field(p->v, arg, 0);
            if (base == NULL || base->kind != ML_BLOCK) {
                xx_buf_append_str(p->out, "...");
                return;
            }
            switch (base->tag) {
                case 1:
                    if (ml_int(p->v, inner, &value)) {
                        xx_buf_append_char(p->out, '\'');
                        print_escaped(p, (unsigned char)(value & 0xFF), '\'');
                        xx_buf_append_char(p->out, '\'');
                        return;
                    }
                    break;
                case 3:
                    if (ml_string(p->v, inner, &text, &length)) {
                        xx_buf_append(p->out, text, length < PRINT_LIMIT ? length : PRINT_LIMIT);
                        return;
                    }
                    break;
                default: break;
            }
            print_obj(p, inner, depth + 1u);
            return;
        }
        case 1: /* Const_pointer of int */ print_obj(p, arg, depth + 1u); return;
        case 2: { /* Const_block of int * structured_constant list */
            uint32_t list = ml_field(p->v, n, 1);
            unsigned count = 0;
            if (ml_int(p->v, arg, &value)) {
                xx_buf_appendf(p->out, "<%lld>", (long long)value);
            }
            if (ml_get(p->v, list) == NULL || ml_get(p->v, list)->kind != ML_BLOCK) {
                return;
            }
            xx_buf_append_char(p->out, '(');
            while (ml_get(p->v, list) != NULL && ml_get(p->v, list)->kind == ML_BLOCK && ml_get(p->v, list)->size == 2) {
                if (count++ != 0) {
                    xx_buf_append_str(p->out, ", ");
                }
                if (full(p) || count > 64u) {
                    xx_buf_append_str(p->out, "...");
                    break;
                }
                print_struct_const(p, ml_field(p->v, list, 0), pointer, depth + 1u);
                list = ml_field(p->v, list, 1);
            }
            xx_buf_append_char(p->out, ')');
            return;
        }
        case 3: { /* Const_float_array of string list */
            uint32_t list = arg;
            unsigned count = 0;
            xx_buf_append_str(p->out, "[|");
            while (ml_get(p->v, list) != NULL && ml_get(p->v, list)->kind == ML_BLOCK && ml_get(p->v, list)->size == 2) {
                if (count++ != 0) {
                    xx_buf_append_str(p->out, ", ");
                }
                if (full(p) || count > 64u) {
                    xx_buf_append_str(p->out, "...");
                    break;
                }
                if (ml_string(p->v, ml_field(p->v, list, 0), &text, &length)) {
                    xx_buf_append(p->out, text, length < PRINT_LIMIT ? length : PRINT_LIMIT);
                }
                list = ml_field(p->v, list, 1);
            }
            xx_buf_append_str(p->out, "|]");
            return;
        }
        default: /* Const_immstring of string */ print_obj(p, arg, depth + 1u); return;
    }
}

/* ---- Containers -------------------------------------------------------- */

enum {
    REL_LITERAL,
    REL_GET,
    REL_PREDEF,
    REL_SET,
    REL_PRIM
};

typedef struct ocaml_reloc {
    uint64_t pos;  /* byte offset of the patched word in the unit's code */
    uint32_t node; /* the literal, or the name's node */
    uint8_t kind;
} ocaml_reloc;

/* How Reloc_literal values are written. */
enum {
    LIT_OBJ,          /* Obj.t (5.1 and later) */
    LIT_CONST,        /* Lambda.structured_constant, 4.12 to 5.0 */
    LIT_CONST_POINTER /* the same with Const_pointer, before 4.12 */
};

typedef struct name_ref {
    const char *text;
    size_t length;
    uint32_t constant; /* a DATA node when text is NULL */
} name_ref;

typedef struct list_context {
    /* executables */
    const char **prim_text;
    size_t *prim_length;
    size_t prim_count;
    name_ref *globals;
    size_t global_count;
    const ml_value *data;
    /* objects */
    const ml_value *unit;
    ocaml_reloc *relocs;
    size_t reloc_count;
    int literal_style;
    /* labels: one byte per code word */
    uint8_t *marks;
    size_t mark_count;
} list_context;

#define MARK_START 1u    /* an instruction starts here */
#define MARK_LABEL 2u    /* some instruction targets it */
#define MARK_FUNCTION 4u /* a closure's code starts here */

static size_t copy_out(xx_buf_t *text, char *buffer, size_t buffer_size)
{
    size_t length = 0;

    if (xx_buf_ok(text) && text->size != 0 && buffer_size != 0) {
        length = text->size < buffer_size - 1u ? text->size : buffer_size - 1u;
        memcpy(buffer, text->data, length);
        buffer[length] = '\0';
    }
    xx_buf_free(text);
    return length;
}

static const ocaml_reloc *find_reloc(const list_context *c, uint64_t pos)
{
    size_t lo = 0;
    size_t hi = c->reloc_count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        if (c->relocs[mid].pos < pos) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return (lo < c->reloc_count && c->relocs[lo].pos == pos) ? &c->relocs[lo] : NULL;
}

static size_t list_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const list_context *c = (const list_context *)context;
    const xxbyte_operand *operand = &instruction->operands[operand_index];
    xx_buf_t text;
    printer p;

    xx_buf_init(&text);
    if (operand->type == XXBYTE_OPERAND_BRANCH) {
        if (operand->value >= 0 && (operand->value & 3) == 0 && (uint64_t)operand->value / 4u < c->mark_count &&
            (c->marks[(size_t)((uint64_t)operand->value / 4u)] & MARK_LABEL) != 0) {
            xx_buf_appendf(&text, "L_%04llX", (unsigned long long)operand->value);
        }
        return copy_out(&text, buffer, buffer_size);
    }
    if (operand->type != XXBYTE_OPERAND_INDEX) {
        return 0;
    }
    p.out = &text;
    p.start = 0;
    if (c->unit != NULL) {
        const ocaml_reloc *r = find_reloc(c, instruction->address + operand->extra);
        const char *name;
        size_t length;
        if (r == NULL) {
            return 0;
        }
        p.v = c->unit;
        if (r->kind == REL_LITERAL) {
            if ((uint8_t)operand->kind != XXBYTE_INDEX_GLOBAL) {
                return 0;
            }
            if (c->literal_style == LIT_OBJ) {
                print_obj(&p, r->node, 0);
            } else {
                print_struct_const(&p, r->node, c->literal_style == LIT_CONST_POINTER, 0);
            }
        } else if (ml_name(c->unit, r->node, &name, &length)) {
            if ((r->kind == REL_PRIM) != ((uint8_t)operand->kind == (uint8_t)XXBYTE_OCAML_INDEX_PRIMITIVE)) {
                return 0;
            }
            xx_buf_append(&text, name, length);
        }
        return copy_out(&text, buffer, buffer_size);
    }
    if ((uint8_t)operand->kind == (uint8_t)XXBYTE_OCAML_INDEX_PRIMITIVE) {
        if (operand->value < 0 || (uint64_t)operand->value >= c->prim_count) {
            return 0;
        }
        xx_buf_append(&text, c->prim_text[(size_t)operand->value], c->prim_length[(size_t)operand->value]);
        return copy_out(&text, buffer, buffer_size);
    }
    if ((uint8_t)operand->kind == XXBYTE_INDEX_GLOBAL && operand->value >= 0 && (uint64_t)operand->value < c->global_count) {
        const name_ref *g = &c->globals[(size_t)operand->value];
        if (g->text != NULL) {
            xx_buf_append(&text, g->text, g->length);
        } else if (g->constant != ML_NONE && c->data != NULL) {
            p.v = c->data;
            print_obj(&p, g->constant, 0);
        }
        return copy_out(&text, buffer, buffer_size);
    }
    return 0;
}

static void mark_target(list_context *c, int64_t target, uint8_t mark)
{
    if (target >= 0 && (target & 3) == 0 && (uint64_t)target / 4u < c->mark_count) {
        c->marks[(size_t)((uint64_t)target / 4u)] |= mark;
    }
}

/* Lists code[0..size) at address 0 with labels. */
static xxbyte_status list_code(uint32_t mode, const uint8_t *code, size_t size, uint32_t listing_flags, list_context *c, xx_buf_t *out)
{
    size_t offset = 0;
    size_t chunk;
    xxbyte_status status = XXBYTE_STATUS_OK;

    c->mark_count = size / 4u + 1u;
    c->marks = (uint8_t *)xx_mem_calloc(c->mark_count, 1);
    if (c->marks == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    while (offset < size) {
        xxbyte_instruction insn;
        uint32_t i;
        if ((offset & 3u) == 0) {
            c->marks[offset / 4u] |= MARK_START;
        }
        if (xxbyte_decode(XXBYTE_FAMILY_OCAML, mode, code + offset, size - offset, offset, &insn) != XXBYTE_STATUS_OK) {
            offset += size - offset < 4u ? size - offset : 4u;
            continue;
        }
        for (i = 0; i < insn.operand_count; ++i) {
            if (insn.operands[i].type == XXBYTE_OPERAND_BRANCH) {
                mark_target(c, insn.operands[i].value, insn.id == XXBYTE_OCAML_ID_CLOSURE ? (MARK_LABEL | MARK_FUNCTION) : MARK_LABEL);
            }
        }
        for (i = 0; i < insn.table_count; ++i) {
            int64_t key;
            int64_t target;
            if (insn.id == XXBYTE_OCAML_ID_SWITCH && xxbyte_switch_entry(&insn, i, &key, &target) == XXBYTE_STATUS_OK) {
                mark_target(c, target, MARK_LABEL);
            } else if (xxbyte_ocaml_closure_entry(&insn, i, &target) == XXBYTE_STATUS_OK) {
                mark_target(c, target, MARK_LABEL | MARK_FUNCTION);
            }
        }
        offset += insn.size;
    }
    /* Keep the labels that fall on instruction starts. */
    for (chunk = 0; chunk < c->mark_count; ++chunk) {
        if ((c->marks[chunk] & MARK_START) == 0) {
            c->marks[chunk] = 0;
        }
    }
    offset = 0;
    while (offset < size && status == XXBYTE_STATUS_OK) {
        size_t next = offset + 4u;
        uint8_t mark = (offset & 3u) == 0 ? c->marks[offset / 4u] : 0;
        if ((mark & MARK_FUNCTION) != 0 && !xx_buf_appendf(out, "; function L_%04llX\n", (unsigned long long)offset)) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        if ((mark & MARK_LABEL) != 0 && !xx_buf_appendf(out, "L_%04llX:\n", (unsigned long long)offset)) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        while (next < size && (c->marks[next / 4u] & MARK_LABEL) == 0) {
            next += 4u;
        }
        if (next > size) {
            next = size;
        }
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_OCAML, mode, NULL, code + offset, next - offset, offset, listing_flags, list_resolver, c, out);
        offset = next;
    }
    xx_mem_free(c->marks);
    c->marks = NULL;
    c->mark_count = 0;
    return status;
}

static int read_be32(const uint8_t *data, size_t size, size_t offset, uint32_t *value)
{
    if (offset > size || size - offset < 4u) {
        return 0;
    }
    *value = xxbyte_u32be(data + offset);
    return 1;
}

static uint32_t magic_number(const uint8_t *magic)
{
    return (uint32_t)(magic[9] - '0') * 100u + (uint32_t)(magic[10] - '0') * 10u + (uint32_t)(magic[11] - '0');
}

/* ---- Executables ------------------------------------------------------- */

typedef struct section {
    char name[5];
    size_t pos;
    size_t len;
} section;

static const section *find_section(const section *sections, size_t count, const char *name)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (memcmp(sections[i].name, name, 4) == 0) {
            return &sections[i];
        }
    }
    return NULL;
}

/* Fills the slot names of a global_map (a {cnt; tbl} record whose tbl is a
 * Map/Tbl tree of Node (l, key, slot, r, h)). */
static void symb_names(const ml_value *symb, list_context *c)
{
    uint32_t *stack;
    size_t depth = 0;
    size_t steps = 0;

    stack = (uint32_t *)xx_mem_alloc((symb->node_count + 1u) * sizeof(uint32_t));
    if (stack == NULL) {
        return;
    }
    stack[depth++] = ml_field(symb, symb->root, 1);
    while (depth != 0 && steps++ < symb->node_count) {
        uint32_t n = stack[--depth];
        const ml_node *node = ml_get(symb, n);
        int64_t slot;
        if (node == NULL || node->kind != ML_BLOCK || node->size != 5) {
            continue;
        }
        if (ml_int(symb, ml_field(symb, n, 2), &slot) && slot >= 0 && (uint64_t)slot < c->global_count) {
            name_ref *g = &c->globals[(size_t)slot];
            if (ml_name(symb, ml_field(symb, n, 1), &g->text, &g->length)) {
                g->constant = ML_NONE;
            }
        }
        if (depth + 2u <= symb->node_count + 1u) {
            stack[depth++] = ml_field(symb, n, 3);
            stack[depth++] = ml_field(symb, n, 0);
        }
    }
    xx_mem_free(stack);
}

static xxbyte_status list_executable(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    const uint8_t *magic = data + size - 12u;
    uint32_t count;
    size_t toc;
    size_t pos;
    size_t i;
    section *sections;
    const section *code;
    const section *prim;
    const section *sdata;
    const section *symb;
    ml_value data_value;
    ml_value symb_value;
    list_context c;
    xxbyte_status status;

    count = xxbyte_u32be(data + size - 16u);
    if (count > (size - 16u) / 8u) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    toc = size - 16u - (size_t)count * 8u;
    sections = (section *)xx_mem_calloc(count + 1u, sizeof(section));
    if (sections == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    pos = toc;
    for (i = count; i-- > 0;) {
        const uint8_t *entry = data + toc + i * 8u;
        size_t len = xxbyte_u32be(entry + 4);
        if (len > pos) {
            xx_mem_free(sections);
            return XXBYTE_STATUS_TRUNCATED;
        }
        memcpy(sections[i].name, entry, 4);
        pos -= len;
        sections[i].pos = pos;
        sections[i].len = len;
    }
    code = find_section(sections, count, "CODE");
    if (code == NULL) {
        xx_mem_free(sections);
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    prim = find_section(sections, count, "PRIM");
    sdata = find_section(sections, count, "DATA");
    symb = find_section(sections, count, "SYMB");
    memset(&c, 0, sizeof(c));
    memset(&data_value, 0, sizeof(data_value));
    memset(&symb_value, 0, sizeof(symb_value));
    status = XXBYTE_STATUS_OK;
    if (prim != NULL) {
        size_t n = 0;
        for (i = 0; i < prim->len; ++i) {
            n += data[prim->pos + i] == 0;
        }
        c.prim_text = (const char **)xx_mem_calloc(n + 1u, sizeof(const char *));
        c.prim_length = (size_t *)xx_mem_calloc(n + 1u, sizeof(size_t));
        if (c.prim_text == NULL || c.prim_length == NULL) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
        } else {
            size_t start = 0;
            for (i = 0; i < prim->len; ++i) {
                if (data[prim->pos + i] == 0) {
                    c.prim_text[c.prim_count] = (const char *)data + prim->pos + start;
                    c.prim_length[c.prim_count++] = i - start;
                    start = i + 1u;
                }
            }
        }
    }
    if (status == XXBYTE_STATUS_OK && sdata != NULL && ml_read(data, sdata->pos + sdata->len, sdata->pos, &data_value) == XXBYTE_STATUS_OK) {
        const ml_node *root = ml_get(&data_value, data_value.root);
        if (root != NULL && root->kind == ML_BLOCK) {
            c.global_count = root->size;
            c.globals = (name_ref *)xx_mem_calloc(c.global_count + 1u, sizeof(name_ref));
            if (c.globals == NULL) {
                status = XXBYTE_STATUS_OUT_OF_MEMORY;
            } else {
                for (i = 0; i < c.global_count; ++i) {
                    c.globals[i].constant = data_value.kids[root->at + i];
                }
                c.data = &data_value;
            }
        }
    }
    if (status == XXBYTE_STATUS_OK && c.globals != NULL && symb != NULL && ml_read(data, symb->pos + symb->len, symb->pos, &symb_value) == XXBYTE_STATUS_OK) {
        symb_names(&symb_value, &c);
    }
    if (status == XXBYTE_STATUS_OK) {
        if (!xx_buf_appendf(out, "; CODE section at 0x%llx, %llu bytes, %llu primitives, %llu globals (%.12s)\n", (unsigned long long)code->pos,
                            (unsigned long long)code->len, (unsigned long long)c.prim_count, (unsigned long long)c.global_count, (const char *)magic)) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
        } else {
            status = list_code(mode, data + code->pos, code->len, listing_flags, &c, out);
        }
    }
    ml_free(&symb_value);
    ml_free(&data_value);
    xx_mem_free(c.globals);
    xx_mem_free(c.prim_text);
    xx_mem_free(c.prim_length);
    xx_mem_free(sections);
    return status;
}

/* ---- Objects and libraries --------------------------------------------- */

static int reloc_less(const ocaml_reloc *a, const ocaml_reloc *b)
{
    return a->pos < b->pos;
}

/* Lists one compilation_unit record: {cu_name; cu_pos; cu_codesize;
 * cu_reloc; ...}. */
static xxbyte_status list_unit(uint32_t mode, const uint8_t *data, size_t size, const uint8_t *magic, const ml_value *v, uint32_t cu, uint32_t listing_flags,
                               xx_buf_t *out)
{
    const char *name = "?";
    size_t name_length = 1;
    int64_t pos;
    int64_t codesize;
    uint32_t list;
    size_t count = 0;
    size_t steps = 0;
    size_t i;
    uint32_t number = magic_number(magic);
    int new_relocs = number >= 34u; /* Reloc_getcompunit, 5.2 */
    list_context c;
    xxbyte_status status;

    if (!ml_int(v, ml_field(v, cu, 1), &pos) || !ml_int(v, ml_field(v, cu, 2), &codesize) || pos < 0 || codesize < 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if ((uint64_t)pos > size || (uint64_t)codesize > size - (uint64_t)pos) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    (void)ml_name(v, ml_field(v, cu, 0), &name, &name_length);
    memset(&c, 0, sizeof(c));
    c.unit = v;
    c.literal_style = number >= 33u ? LIT_OBJ : number >= 29u ? LIT_CONST : LIT_CONST_POINTER;
    for (list = ml_field(v, cu, 3); ml_field(v, list, 1) != ML_NONE && steps < v->node_count; list = ml_field(v, list, 1), ++steps) {
        ++count;
    }
    c.relocs = (ocaml_reloc *)xx_mem_calloc(count + 1u, sizeof(ocaml_reloc));
    if (c.relocs == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    list = ml_field(v, cu, 3);
    for (i = 0; i < count; ++i, list = ml_field(v, list, 1)) {
        uint32_t pair = ml_field(v, list, 0);
        uint32_t info = ml_field(v, pair, 0);
        const ml_node *node = ml_get(v, info);
        int64_t at;
        ocaml_reloc *r = &c.relocs[c.reloc_count];
        if (node == NULL || node->kind != ML_BLOCK || node->size < 1 || !ml_int(v, ml_field(v, pair, 1), &at) || at < 0) {
            continue;
        }
        r->pos = (uint64_t)at;
        r->node = ml_field(v, info, 0);
        if (new_relocs) {
            static const uint8_t kinds[5] = {REL_LITERAL, REL_GET, REL_PREDEF, REL_SET, REL_PRIM};
            if (node->tag > 4) {
                continue;
            }
            r->kind = kinds[node->tag];
        } else {
            static const uint8_t kinds[4] = {REL_LITERAL, REL_GET, REL_SET, REL_PRIM};
            if (node->tag > 3) {
                continue;
            }
            r->kind = kinds[node->tag];
        }
        ++c.reloc_count;
    }
    /* Insertion sort by position: the compiler writes them in reverse. */
    for (i = 1; i < c.reloc_count; ++i) {
        ocaml_reloc key = c.relocs[i];
        size_t j = i;
        while (j > 0 && reloc_less(&key, &c.relocs[j - 1u])) {
            c.relocs[j] = c.relocs[j - 1u];
            --j;
        }
        c.relocs[j] = key;
    }
    if (!xx_buf_append_str(out, "; compilation unit ") || !xx_buf_append(out, name, name_length < 256u ? name_length : 256u) ||
        !xx_buf_appendf(out, " at 0x%llx, %llu bytes, %llu relocations (%.12s)\n", (unsigned long long)pos, (unsigned long long)codesize,
                        (unsigned long long)c.reloc_count, (const char *)magic)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    } else {
        status = list_code(mode, data + pos, (size_t)codesize, listing_flags, &c, out);
    }
    xx_mem_free(c.relocs);
    return status;
}

static xxbyte_status list_object(uint32_t mode, const uint8_t *data, size_t size, int library, uint32_t listing_flags, xx_buf_t *out)
{
    uint32_t descriptor;
    ml_value v;
    xxbyte_status status;

    if (!read_be32(data, size, 12, &descriptor)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    status = ml_read(data, size, descriptor, &v);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (!library) {
        status = list_unit(mode, data, size, data, &v, v.root, listing_flags, out);
    } else {
        uint32_t list = ml_field(&v, v.root, 0);
        size_t steps = 0;
        for (; status == XXBYTE_STATUS_OK && ml_field(&v, list, 1) != ML_NONE && steps < v.node_count; list = ml_field(&v, list, 1), ++steps) {
            status = list_unit(mode, data, size, data, &v, ml_field(&v, list, 0), listing_flags, out);
        }
    }
    ml_free(&v);
    return status;
}

xxbyte_status xxbyte_ocaml_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    uint32_t file_mode;

    if (size >= 16u && xxbyte_ocaml_mode_from_magic(data + size - 12u, 12, &file_mode) == XXBYTE_STATUS_OK && data[size - 12u + 8u] == 'X') {
        return list_executable(mode == XXBYTE_MODE_AUTO ? file_mode : mode, data, size, listing_flags, out);
    }
    if (size >= 12u && xxbyte_ocaml_mode_from_magic(data, size, &file_mode) == XXBYTE_STATUS_OK && (data[8] == 'O' || data[8] == 'A')) {
        return list_object(mode == XXBYTE_MODE_AUTO ? file_mode : mode, data, size, data[8] == 'A', listing_flags, out);
    }
    if (size < 16u) {
        return size == 0 || (size >= 8u && memcmp(data, "Caml1999", 8) == 0) ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

#else
typedef int xxbyte_ocaml_file_disabled;
#endif
