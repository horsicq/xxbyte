#include "xxbyte_quickjs_internal.h"

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#if XXBYTE_USE_QUICKJS

/* JS_WriteObject images: reader (JS_ReadObject's layout per release),
 * release detection, names and the whole-file listing. */

#define QJS_MAX_DEPTH 400u
#define QJS_NONE 0xFFFFFFFFu
#define QJS_MAG_MAX 320u /* largest BigInt printed in decimal, in bytes */

/* Normalized object tags. */
enum {
    QV_INVALID,
    QV_NULL,
    QV_UNDEFINED,
    QV_FALSE,
    QV_TRUE,
    QV_INT32,
    QV_FLOAT64,
    QV_STRING,
    QV_OBJECT,
    QV_ARRAY,
    QV_BIG_INT,
    QV_BIG_FLOAT,
    QV_BIG_DECIMAL,
    QV_TEMPLATE,
    QV_FUNCTION,
    QV_MODULE,
    QV_TYPED_ARRAY,
    QV_ARRAY_BUFFER,
    QV_SHARED_ARRAY_BUFFER,
    QV_REGEXP,
    QV_DATE,
    QV_OBJECT_VALUE,
    QV_OBJECT_REFERENCE,
    QV_MAP,
    QV_SET,
    QV_SYMBOL
};

/* Atom table entry. */
enum {
    QA_CONST = 0,
    QA_STRING = 1,
    QA_GLOBAL_SYMBOL = 2,
    QA_SYMBOL = 3
};

typedef struct qjs_atom_entry {
    uint8_t kind;
    uint8_t wide;
    uint32_t length; /* code units */
    size_t offset;   /* of the characters */
    uint32_t value;  /* QA_CONST: the predefined atom */
} qjs_atom_entry;

/* Summary of a constant-pool entry. */
typedef struct qjs_value {
    uint8_t tag;
    uint8_t wide;
    uint8_t negative; /* BigInt/BigFloat sign (libbf) */
    uint8_t special;  /* libbf: 0 finite, 1 zero, 2 inf, 3 nan */
    uint32_t length;  /* string units, array/property count, bytes */
    size_t offset;    /* string characters, BigInt bytes, regexp source */
    int64_t number;   /* int32, float64 bits, function index, exponent */
} qjs_value;

typedef struct qjs_vardef {
    uint32_t name;
    int32_t scope_level;
    int32_t scope_next;
    uint32_t var_ref_idx;
    uint32_t flags; /* var_kind:4 is_const is_lexical is_captured [has_scope] */
} qjs_vardef;

typedef struct qjs_closure {
    uint32_t name;
    uint32_t var_idx;
    uint32_t type; /* 0 loc, 1 arg, 2 ref, 3 global_ref, 4 global_decl, ... */
    uint32_t is_const;
    uint32_t is_lexical;
    uint32_t var_kind;
} qjs_closure;

typedef struct qjs_func {
    xxbyte_quickjs_function pub;
    uint32_t vardef_first;
    uint32_t closure_first;
    uint32_t value_first;
} qjs_func;

/* Module records: requests (a = name), exports (kind 0 local: x = var_idx,
 * a = name; kind 1 indirect: x = request, b = local name, a = name), star
 * exports (x = request), imports (x = var_idx, y = request, a = name, kind
 * = is_star). */
typedef struct qjs_mod_entry {
    uint32_t kind;
    uint32_t x, y;
    uint32_t a, b;
} qjs_mod_entry;

typedef struct qjs_module_info {
    int present;
    uint32_t name;
    uint32_t req_count, export_count, star_count, import_count;
    uint32_t has_tla;
    qjs_mod_entry *entries; /* requests, exports, stars, imports in order */
} qjs_module_info;

struct xxbyte_quickjs_file {
    const uint8_t *data;
    size_t size;
    const qjs_version *version;
    uint32_t mode;
    uint32_t checksum; /* as stored (ng 0.13+) */
    uint32_t checksum_computed;
    qjs_atom_entry *atoms;
    uint32_t atom_count;
    qjs_func *funcs;
    uint32_t function_count, function_capacity;
    qjs_vardef *vardefs;
    uint32_t vardef_count, vardef_capacity;
    qjs_closure *closures;
    uint32_t closure_count, closure_capacity;
    qjs_value *values;
    uint32_t value_count, value_capacity;
    qjs_module_info module;
    size_t end; /* bytes the object tree used */
};

typedef struct qjs_reader {
    xxbyte_quickjs_file *f;
    const uint8_t *p;
    size_t size;
    size_t pos;
    xxbyte_status status;
} qjs_reader;

/* ---------------------------------------------------------------- reader */

static int rd_fail(qjs_reader *r, xxbyte_status status)
{
    if (r->status == XXBYTE_STATUS_OK) {
        r->status = status;
    }
    return 0;
}

static int rd_need(qjs_reader *r, size_t n)
{
    if (r->status != XXBYTE_STATUS_OK) {
        return 0;
    }
    if (n > r->size - r->pos) {
        return rd_fail(r, XXBYTE_STATUS_TRUNCATED);
    }
    return 1;
}

static int rd_u8(qjs_reader *r, uint32_t *v)
{
    if (!rd_need(r, 1)) {
        return 0;
    }
    *v = r->p[r->pos++];
    return 1;
}

static int rd_u16(qjs_reader *r, uint32_t *v)
{
    if (!rd_need(r, 2)) {
        return 0;
    }
    *v = xxbyte_u16le(r->p + r->pos);
    r->pos += 2;
    return 1;
}

static int rd_u32(qjs_reader *r, uint32_t *v)
{
    if (!rd_need(r, 4)) {
        return 0;
    }
    *v = xxbyte_u32le(r->p + r->pos);
    r->pos += 4;
    return 1;
}

static int rd_skip(qjs_reader *r, uint64_t n)
{
    if (r->status != XXBYTE_STATUS_OK) {
        return 0;
    }
    if (n > (uint64_t)(r->size - r->pos)) {
        return rd_fail(r, XXBYTE_STATUS_TRUNCATED);
    }
    r->pos += (size_t)n;
    return 1;
}

/* get_leb128: at most 5 bytes, bits beyond 32 dropped. */
static int rd_leb(qjs_reader *r, uint32_t *v)
{
    uint32_t value = 0;
    uint32_t i;

    if (r->status != XXBYTE_STATUS_OK) {
        return 0;
    }
    for (i = 0; i < 5; ++i) {
        uint32_t a;
        if (r->pos >= r->size) {
            return rd_fail(r, XXBYTE_STATUS_TRUNCATED);
        }
        a = r->p[r->pos++];
        value |= (a & 0x7Fu) << (i * 7u);
        if ((a & 0x80u) == 0) {
            *v = value;
            return 1;
        }
    }
    return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
}

/* A leb128 the reference reads into an int: negative values are refused. */
static int rd_count(qjs_reader *r, uint32_t *v)
{
    if (!rd_leb(r, v)) {
        return 0;
    }
    if (*v > 0x7FFFFFFFu) {
        return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
    }
    return 1;
}

static int rd_sleb(qjs_reader *r, int32_t *v)
{
    uint32_t u;

    if (!rd_leb(r, &u)) {
        return 0;
    }
    *v = (int32_t)((u >> 1) ^ (0u - (u & 1u)));
    return 1;
}

/* bc_get_atom: bit 0 set = integer atom, else a file index. Returns the raw
 * 32-bit value as the bytecode stores it. */
static int rd_atom(qjs_reader *r, uint32_t *atom)
{
    uint32_t v;

    if (!rd_leb(r, &v)) {
        return 0;
    }
    if ((v & 1u) != 0) {
        *atom = (v >> 1) | 0x80000000u;
        return 1;
    }
    v >>= 1;
    if (v > r->f->version->atom_count && v - r->f->version->atom_count - 1u >= r->f->atom_count) {
        return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
    }
    *atom = v;
    return 1;
}

static int rd_string(qjs_reader *r, uint32_t *length, uint8_t *wide, size_t *offset)
{
    uint32_t len;

    if (!rd_leb(r, &len)) {
        return 0;
    }
    *wide = (uint8_t)(len & 1u);
    *length = len >> 1;
    *offset = r->pos;
    if (*length > 0x3FFFFFFFu) {
        return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
    }
    return rd_skip(r, (uint64_t)*length << *wide);
}

static int qjs_grow(void **array, uint32_t *capacity, uint32_t count, uint32_t extra, size_t element)
{
    uint64_t want = (uint64_t)count + extra;
    uint64_t cap = *capacity;
    void *p;

    if (want <= cap) {
        return 1;
    }
    if (want > 0x7FFFFFFFu) {
        return 0;
    }
    if (cap < 16) {
        cap = 16;
    }
    while (cap < want) {
        cap *= 2;
    }
    if (cap > 0x7FFFFFFFu) {
        cap = want;
    }
    p = xx_mem_realloc(*array, (size_t)cap * element);
    if (p == NULL) {
        return 0;
    }
    *array = p;
    *capacity = (uint32_t)cap;
    return 1;
}

static int rd_object(qjs_reader *r, uint32_t depth, uint32_t parent, uint32_t slot, qjs_value *out);

/* Tag byte to normalized tag for the release. */
static uint8_t qjs_tag(const qjs_version *v, uint32_t tag)
{
    static const uint8_t bigfloat[] = {QV_INVALID,
                                       QV_NULL,
                                       QV_UNDEFINED,
                                       QV_FALSE,
                                       QV_TRUE,
                                       QV_INT32,
                                       QV_FLOAT64,
                                       QV_STRING,
                                       QV_OBJECT,
                                       QV_ARRAY,
                                       QV_BIG_INT,
                                       QV_BIG_FLOAT,
                                       QV_BIG_DECIMAL,
                                       QV_TEMPLATE,
                                       QV_FUNCTION,
                                       QV_MODULE,
                                       QV_TYPED_ARRAY,
                                       QV_ARRAY_BUFFER,
                                       QV_SHARED_ARRAY_BUFFER,
                                       QV_DATE,
                                       QV_OBJECT_VALUE,
                                       QV_OBJECT_REFERENCE};
    static const uint8_t plain[] = {QV_INVALID,
                                    QV_NULL,
                                    QV_UNDEFINED,
                                    QV_FALSE,
                                    QV_TRUE,
                                    QV_INT32,
                                    QV_FLOAT64,
                                    QV_STRING,
                                    QV_OBJECT,
                                    QV_ARRAY,
                                    QV_BIG_INT,
                                    QV_TEMPLATE,
                                    QV_FUNCTION,
                                    QV_MODULE,
                                    QV_TYPED_ARRAY,
                                    QV_ARRAY_BUFFER,
                                    QV_SHARED_ARRAY_BUFFER};
    if ((v->flags & QJS_F_TAGS_BIGFLOAT) != 0) {
        return tag < sizeof(bigfloat) ? bigfloat[tag] : (uint8_t)QV_INVALID;
    }
    if (tag < sizeof(plain)) {
        return plain[tag];
    }
    tag -= (uint32_t)sizeof(plain);
    if ((v->flags & QJS_F_TAG_REGEXP) != 0) {
        if (tag == 0) {
            return QV_REGEXP;
        }
        tag--;
    }
    switch (tag) {
        case 0: return QV_DATE;
        case 1: return QV_OBJECT_VALUE;
        case 2: return QV_OBJECT_REFERENCE;
        case 3: return (v->flags & QJS_F_TAG_MAPSET) != 0 ? (uint8_t)QV_MAP : (uint8_t)QV_INVALID;
        case 4: return (v->flags & QJS_F_TAG_MAPSET) != 0 ? (uint8_t)QV_SET : (uint8_t)QV_INVALID;
        case 5: return (v->flags & QJS_F_TAG_SYMBOL) != 0 ? (uint8_t)QV_SYMBOL : (uint8_t)QV_INVALID;
        default: return QV_INVALID;
    }
}

static int rd_ic_trailer(qjs_reader *r)
{
    uint32_t count;
    uint32_t i;
    uint32_t atom;

    if (r->size - r->pos > 3 && r->p[r->pos] == 255 && r->p[r->pos + 1] == 73 && r->p[r->pos + 2] == 67) {
        r->pos += 3;
        if (!rd_leb(r, &count)) {
            return 0;
        }
        for (i = 0; i < count; ++i) {
            if (!rd_atom(r, &atom)) {
                return 0;
            }
        }
    }
    return 1;
}

static int rd_debug(qjs_reader *r, xxbyte_quickjs_function *fn)
{
    uint32_t flags = r->f->version->flags;
    uint32_t v;

    if (!rd_atom(r, &fn->filename_atom)) {
        return 0;
    }
    if ((flags & QJS_F_LINE_NUM) != 0 && !rd_count(r, &fn->line)) {
        return 0;
    }
    if ((flags & QJS_F_COL_NUM) != 0 && !rd_count(r, &fn->column)) {
        return 0;
    }
    if (!rd_count(r, &v)) {
        return 0;
    }
    fn->pc2line_offset = r->pos;
    fn->pc2line_size = v;
    if (!rd_skip(r, v)) {
        return 0;
    }
    if ((flags & QJS_F_IC_IN_DEBUG) != 0 && !rd_ic_trailer(r)) {
        return 0;
    }
    if ((flags & QJS_F_SOURCE) != 0) {
        if (!rd_count(r, &v)) {
            return 0;
        }
        fn->source_offset = r->pos;
        fn->source_size = v;
        if (!rd_skip(r, v)) {
            return 0;
        }
    }
    return 1;
}

static int rd_cpool(qjs_reader *r, uint32_t depth, uint32_t index)
{
    xxbyte_quickjs_file *f = r->f;
    uint32_t count = f->funcs[index].pub.cpool_count;
    uint32_t first = f->funcs[index].value_first;
    uint32_t i;

    for (i = 0; i < count; ++i) {
        qjs_value value;
        memset(&value, 0, sizeof(value));
        if (!rd_object(r, depth + 1u, index, i, &value)) {
            return 0;
        }
        f->values[first + i] = value;
    }
    return 1;
}

static int rd_function(qjs_reader *r, uint32_t depth, uint32_t parent, uint32_t slot, qjs_value *out)
{
    xxbyte_quickjs_file *f = r->f;
    const qjs_version *v = f->version;
    xxbyte_quickjs_function fn;
    uint32_t index = f->function_count;
    uint32_t i;
    uint32_t x;

    memset(&fn, 0, sizeof(fn));
    fn.index = index;
    fn.parent = parent;
    fn.cpool_slot = slot;
    fn.offset = r->pos - 1u;
    if (!rd_u16(r, &fn.flags) || !rd_u8(r, &fn.js_mode) || !rd_atom(r, &fn.name_atom)) {
        return 0;
    }
    fn.func_kind = (fn.flags >> 4) & 3u;
    if ((v->flags & QJS_F_DEBUG_ALWAYS) != 0) {
        fn.has_debug = 1;
    } else {
        fn.has_debug = (fn.flags >> ((v->flags & QJS_F_DEBUG_BIT11) != 0 ? 11u : 10u)) & 1u;
    }
    if (!rd_leb(r, &fn.arg_count) || !rd_leb(r, &fn.var_count) || !rd_leb(r, &fn.defined_arg_count) || !rd_leb(r, &fn.stack_size)) {
        return 0;
    }
    fn.arg_count &= 0xFFFFu;
    fn.var_count &= 0xFFFFu;
    fn.defined_arg_count &= 0xFFFFu;
    fn.stack_size &= 0xFFFFu;
    if ((v->flags & QJS_F_VAR_REF_COUNT) != 0) {
        if (!rd_leb(r, &fn.var_ref_count)) {
            return 0;
        }
        fn.var_ref_count &= 0xFFFFu;
    }
    if (!rd_count(r, &fn.closure_var_count) || !rd_count(r, &fn.cpool_count) || !rd_count(r, &fn.bytecode_size) || !rd_count(r, &fn.local_count)) {
        return 0;
    }
    if ((v->flags & QJS_F_CV_LEB) != 0) {
        fn.closure_var_count &= 0xFFFFu; /* bc_get_leb128_u16 */
    }
    /* every entry takes at least one byte (two for vardefs and closure
     * variables) */
    if (fn.local_count > (r->size - r->pos) / 2u || fn.closure_var_count > (r->size - r->pos) / 2u || fn.cpool_count > r->size - r->pos) {
        return rd_fail(r, XXBYTE_STATUS_TRUNCATED);
    }
    if (!qjs_grow((void **)&f->funcs, &f->function_capacity, f->function_count, 1, sizeof(*f->funcs)) ||
        !qjs_grow((void **)&f->vardefs, &f->vardef_capacity, f->vardef_count, fn.local_count, sizeof(*f->vardefs)) ||
        !qjs_grow((void **)&f->closures, &f->closure_capacity, f->closure_count, fn.closure_var_count, sizeof(*f->closures)) ||
        !qjs_grow((void **)&f->values, &f->value_capacity, f->value_count, fn.cpool_count, sizeof(*f->values))) {
        return rd_fail(r, XXBYTE_STATUS_OUT_OF_MEMORY);
    }
    f->funcs[index].pub = fn;
    f->funcs[index].vardef_first = f->vardef_count;
    f->funcs[index].closure_first = f->closure_count;
    f->funcs[index].value_first = f->value_count;
    for (i = 0; i < fn.cpool_count; ++i) {
        memset(&f->values[f->value_count + i], 0, sizeof(qjs_value));
    }
    f->value_count += fn.cpool_count;
    f->function_count++;
    if (out != NULL) {
        out->number = index;
    }

    for (i = 0; i < fn.local_count; ++i) {
        qjs_vardef d;
        int32_t s;
        memset(&d, 0, sizeof(d));
        if (!rd_atom(r, &d.name)) {
            return 0;
        }
        if ((v->flags & QJS_F_VD_SCOPE_LEVEL) != 0) {
            if (!rd_leb(r, &x)) {
                return 0;
            }
            d.scope_level = (int32_t)x;
        }
        if (!rd_leb(r, &x)) {
            return 0;
        }
        s = (int32_t)x;
        d.scope_next = s - 1;
        if ((v->flags & QJS_F_VD_REF_FIRST) != 0) {
            if (!rd_leb(r, &d.var_ref_idx)) {
                return 0;
            }
            d.var_ref_idx &= 0xFFFFu;
        }
        if (!rd_u8(r, &d.flags)) {
            return 0;
        }
        if ((v->flags & QJS_F_VD_REF_CAPTURED) != 0 && (d.flags & 0x40u) != 0) {
            if (!rd_leb(r, &d.var_ref_idx)) {
                return 0;
            }
            d.var_ref_idx &= 0xFFFFu;
        }
        f->vardefs[f->vardef_count++] = d;
    }
    for (i = 0; i < fn.closure_var_count; ++i) {
        qjs_closure c;
        memset(&c, 0, sizeof(c));
        if (!rd_atom(r, &c.name) || !rd_count(r, &c.var_idx)) {
            return 0;
        }
        if ((v->flags & (QJS_F_CV_U16 | QJS_F_CV_LEB)) != 0) {
            if ((v->flags & QJS_F_CV_U16) != 0 ? !rd_u16(r, &x) : !rd_leb(r, &x)) {
                return 0;
            }
            c.type = x & 7u;
            c.is_const = (x >> 3) & 1u;
            c.is_lexical = (x >> 4) & 1u;
            c.var_kind = (x >> 5) & 15u;
        } else {
            if (!rd_u8(r, &x)) {
                return 0;
            }
            c.type = (x & 1u) != 0 ? 0u : (x & 2u) != 0 ? 1u : 2u;
            c.is_const = (x >> 2) & 1u;
            c.is_lexical = (x >> 3) & 1u;
            c.var_kind = (x >> 4) & 15u;
        }
        f->closures[f->closure_count++] = c;
    }
    if ((v->flags & QJS_F_CPOOL_FIRST) != 0 && !rd_cpool(r, depth, index)) {
        return 0;
    }
    f->funcs[index].pub.bytecode_offset = r->pos;
    if (!rd_skip(r, fn.bytecode_size)) {
        return 0;
    }
    if (f->funcs[index].pub.has_debug && !rd_debug(r, &f->funcs[index].pub)) {
        return 0;
    }
    if ((v->flags & QJS_F_IC_AFTER_DEBUG) != 0 && !rd_ic_trailer(r)) {
        return 0;
    }
    if ((v->flags & QJS_F_CPOOL_FIRST) == 0 && !rd_cpool(r, depth, index)) {
        return 0;
    }
    return 1;
}

static int rd_module(qjs_reader *r, uint32_t depth)
{
    xxbyte_quickjs_file *f = r->f;
    qjs_module_info m;
    uint32_t flags = f->version->flags;
    uint64_t total = 0;
    uint32_t section;
    uint32_t i;
    uint32_t x = 0;
    int record = !f->module.present;

    memset(&m, 0, sizeof(m));
    if (!rd_atom(r, &m.name)) {
        return 0;
    }
    for (section = 0; section < 4; ++section) {
        uint32_t count;
        if (!rd_count(r, &count)) {
            goto fail;
        }
        /* every entry takes at least one byte */
        if (count > r->size - r->pos) {
            rd_fail(r, XXBYTE_STATUS_TRUNCATED);
            goto fail;
        }
        if (record) {
            void *p = xx_mem_realloc(m.entries, (size_t)(total + count + 1u) * sizeof(*m.entries));
            if (p == NULL) {
                rd_fail(r, XXBYTE_STATUS_OUT_OF_MEMORY);
                goto fail;
            }
            m.entries = (qjs_mod_entry *)p;
        }
        for (i = 0; i < count; ++i) {
            qjs_mod_entry e;
            memset(&e, 0, sizeof(e));
            switch (section) {
                case 0:
                    if (!rd_atom(r, &e.a) || ((flags & QJS_F_MOD_ATTR) != 0 && !rd_object(r, depth + 1u, QJS_NONE, QJS_NONE, NULL))) {
                        goto fail;
                    }
                    break;
                case 1:
                    if (!rd_u8(r, &e.kind)) {
                        goto fail;
                    }
                    if (e.kind == 0 ? !rd_count(r, &e.x) : (!rd_count(r, &e.x) || !rd_atom(r, &e.b))) {
                        goto fail;
                    }
                    if (!rd_atom(r, &e.a)) {
                        goto fail;
                    }
                    break;
                case 2:
                    if (!rd_count(r, &e.x)) {
                        goto fail;
                    }
                    break;
                default:
                    if (!rd_count(r, &e.x) || ((flags & QJS_F_MOD_IS_STAR) != 0 && !rd_u8(r, &e.kind)) || !rd_atom(r, &e.a) || !rd_count(r, &e.y)) {
                        goto fail;
                    }
                    break;
            }
            if (record) {
                m.entries[total + i] = e;
            }
        }
        total += count;
        switch (section) {
            case 0: m.req_count = count; break;
            case 1: m.export_count = count; break;
            case 2: m.star_count = count; break;
            default: m.import_count = count; break;
        }
    }
    if ((flags & QJS_F_MOD_TLA) != 0 && !rd_u8(r, &x)) {
        goto fail;
    }
    m.has_tla = (flags & QJS_F_MOD_TLA) != 0 ? (x != 0) : 0u;
    if (record) {
        m.present = 1;
        f->module = m;
    }
    return rd_object(r, depth + 1u, QJS_NONE, QJS_NONE, NULL);
fail:
    xx_mem_free(m.entries);
    return 0;
}

static int rd_bigint(qjs_reader *r, uint8_t tag, qjs_value *out)
{
    uint32_t len;

    if ((r->f->version->flags & QJS_F_BIGINT_BF) != 0 || tag != QV_BIG_INT) {
        int32_t e;
        if (!rd_sleb(r, &e)) {
            return 0;
        }
        out->negative = (uint8_t)(e & 1);
        e >>= 1;
        if (e == 0 || e == 1 || e == 2) {
            out->special = (uint8_t)(e + 1);
            return 1;
        }
        out->number = e >= 3 ? (int64_t)e - 3 : (int64_t)e;
        if (!rd_leb(r, &len)) {
            return 0;
        }
        if (len == 0) {
            return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
        }
        out->length = len;
        out->offset = r->pos;
        if (tag == QV_BIG_DECIMAL) {
            size_t i;
            size_t bytes = ((size_t)len + 1u) / 2u;
            if (!rd_need(r, bytes)) {
                return 0;
            }
            for (i = 0; i < len; ++i) {
                uint32_t d = (uint32_t)(r->p[r->pos + i / 2u] >> ((i & 1u) * 4u)) & 15u;
                if (d >= 10u) {
                    return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
                }
            }
            return rd_skip(r, bytes);
        }
        return rd_skip(r, len);
    }
    if (!rd_leb(r, &len)) {
        return 0;
    }
    out->length = len;
    out->offset = r->pos;
    return rd_skip(r, len);
}

static int rd_object(qjs_reader *r, uint32_t depth, uint32_t parent, uint32_t slot, qjs_value *out)
{
    const qjs_version *v = r->f->version;
    qjs_value scratch;
    uint32_t tag;
    uint32_t n;
    uint32_t i;
    uint32_t x;
    int32_t s;

    if (out == NULL) {
        out = &scratch;
    }
    memset(out, 0, sizeof(*out));
    if (depth > QJS_MAX_DEPTH) {
        return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
    }
    if (!rd_u8(r, &tag)) {
        return 0;
    }
    out->tag = qjs_tag(v, tag);
    switch (out->tag) {
        case QV_NULL:
        case QV_UNDEFINED:
        case QV_FALSE:
        case QV_TRUE: return 1;
        case QV_INT32:
            if (!rd_sleb(r, &s)) {
                return 0;
            }
            out->number = s;
            return 1;
        case QV_FLOAT64:
            if (!rd_need(r, 8)) {
                return 0;
            }
            out->number = (int64_t)xxbyte_u64le(r->p + r->pos);
            r->pos += 8;
            return 1;
        case QV_STRING: return rd_string(r, &out->length, &out->wide, &out->offset);
        case QV_OBJECT:
            if (!rd_leb(r, &n)) {
                return 0;
            }
            out->length = n;
            for (i = 0; i < n; ++i) {
                if (!rd_atom(r, &x) || !rd_object(r, depth + 1u, parent, QJS_NONE, NULL)) {
                    return 0;
                }
            }
            return 1;
        case QV_ARRAY:
        case QV_TEMPLATE:
            if (!rd_leb(r, &n)) {
                return 0;
            }
            out->length = n;
            out->offset = r->pos;
            for (i = 0; i < n; ++i) {
                if (!rd_object(r, depth + 1u, parent, QJS_NONE, NULL)) {
                    return 0;
                }
            }
            return out->tag == QV_TEMPLATE ? rd_object(r, depth + 1u, parent, QJS_NONE, NULL) : 1;
        case QV_BIG_INT:
            if ((v->mode & XXBYTE_QUICKJS_MODE_NO_BIGNUM) != 0 && v->mode == (XXBYTE_QUICKJS_MODE_2021_03_27 | XXBYTE_QUICKJS_MODE_NO_BIGNUM)) {
                return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
            }
            return rd_bigint(r, out->tag, out);
        case QV_BIG_FLOAT:
        case QV_BIG_DECIMAL:
            if ((v->mode & XXBYTE_QUICKJS_MODE_NO_BIGNUM) != 0) {
                return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
            }
            return rd_bigint(r, out->tag, out);
        case QV_FUNCTION: return rd_function(r, depth, parent, slot, out);
        case QV_MODULE: return rd_module(r, depth);
        case QV_TYPED_ARRAY:
            if (!rd_u8(r, &x) || !rd_leb(r, &n) || !rd_leb(r, &x)) {
                return 0;
            }
            out->length = n;
            return rd_object(r, depth + 1u, parent, QJS_NONE, NULL);
        case QV_ARRAY_BUFFER:
        case QV_SHARED_ARRAY_BUFFER:
            if (!rd_leb(r, &n)) {
                return 0;
            }
            out->length = n;
            if ((v->flags & QJS_F_AB_MAX) != 0) {
                if (!rd_leb(r, &x)) {
                    return 0;
                }
                if (x < n) {
                    return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
                }
            }
            return out->tag == QV_ARRAY_BUFFER ? rd_skip(r, n) : rd_skip(r, 8);
        case QV_REGEXP:
            if (!rd_string(r, &out->length, &out->wide, &out->offset)) {
                return 0;
            }
            {
                uint32_t blen;
                uint8_t bwide;
                size_t boff;
                return rd_string(r, &blen, &bwide, &boff);
            }
        case QV_DATE:
        case QV_OBJECT_VALUE: return rd_object(r, depth + 1u, parent, QJS_NONE, NULL);
        case QV_OBJECT_REFERENCE:
            if (!rd_leb(r, &x)) {
                return 0;
            }
            out->number = x;
            return 1;
        case QV_MAP:
        case QV_SET:
            if (!rd_leb(r, &n)) {
                return 0;
            }
            out->length = n;
            for (i = 0; i < n; ++i) {
                if (!rd_object(r, depth + 1u, parent, QJS_NONE, NULL)) {
                    return 0;
                }
                if (out->tag == QV_MAP && !rd_object(r, depth + 1u, parent, QJS_NONE, NULL)) {
                    return 0;
                }
            }
            return 1;
        case QV_SYMBOL:
            if (!rd_atom(r, &x)) {
                return 0;
            }
            out->number = x;
            return 1;
        default: return rd_fail(r, XXBYTE_STATUS_INVALID_INSTRUCTION);
    }
}

/* ng bc_csum over the bytes after the version and checksum fields: u32
 * words mixed by a multiply; the tail (1 to 4 bytes, where 4 adds 0) last. */
static uint32_t qjs_checksum(const uint8_t *p, size_t n)
{
    uint32_t h = 0;
    uint32_t tail = 0;
    size_t i = 0;

    while (i + 4u < n) {
        h += xxbyte_u32le(p + i);
        h *= 0x9E370001u;
        i += 4u;
    }
    switch (n - i) {
        case 3: tail |= (uint32_t)p[i + 2] << 16; /* fall through */
        case 2: tail |= (uint32_t)p[i + 1] << 8;  /* fall through */
        case 1: tail |= (uint32_t)p[i]; break;
        default: break;
    }
    h += tail;
    h *= 0x9E370001u;
    return h;
}

static void qjs_free_parts(xxbyte_quickjs_file *f)
{
    xx_mem_free(f->atoms);
    xx_mem_free(f->funcs);
    xx_mem_free(f->module.entries);
    xx_mem_free(f->vardefs);
    xx_mem_free(f->closures);
    xx_mem_free(f->values);
}

/* Reads data with release v into f (zeroed by the caller). */
static xxbyte_status qjs_read(const qjs_version *v, const uint8_t *data, size_t size, xxbyte_quickjs_file *f)
{
    qjs_reader r;
    uint32_t version;
    uint32_t count;
    uint32_t i;

    memset(f, 0, sizeof(*f));
    f->data = data;
    f->size = size;
    f->version = v;
    f->mode = v->mode;
    memset(&r, 0, sizeof(r));
    r.f = f;
    r.p = data;
    r.size = size;
    if (!rd_u8(&r, &version)) {
        return r.status;
    }
    if (version != v->bc_version) {
        if ((v->flags & QJS_F_BE_FLAG) != 0 && version == (v->bc_version | 0x40u)) {
            return XXBYTE_STATUS_UNSUPPORTED_FORMAT;
        }
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if ((v->flags & QJS_F_CHECKSUM) != 0) {
        if (!rd_u32(&r, &f->checksum)) {
            return r.status;
        }
        f->checksum_computed = qjs_checksum(data + 5, size - 5u);
    }
    if (!rd_leb(&r, &count)) {
        return r.status;
    }
    if (count > size - r.pos) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (count != 0) {
        f->atoms = (qjs_atom_entry *)xx_mem_calloc(count, sizeof(*f->atoms));
        if (f->atoms == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    for (i = 0; i < count; ++i) {
        qjs_atom_entry *a = &f->atoms[i];
        uint32_t type = QA_STRING;

        if ((v->flags & QJS_F_ATOMS_TYPED) != 0 && !rd_u8(&r, &type)) {
            return r.status;
        }
        if ((v->flags & QJS_F_ATOMS_CONST) != 0 && (type & 0x80u) != 0) {
            uint32_t high = 0;
            if ((type & 0x40u) != 0 && !rd_leb(&r, &high)) {
                return r.status;
            }
            a->kind = QA_CONST;
            a->value = (high << 6) | (type & 0x3Fu);
            if (a->value >= v->atom_count + 1u) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
        } else if ((v->flags & QJS_F_ATOMS_CONST) == 0 && (v->flags & QJS_F_ATOMS_TYPED) != 0 && type == 0) {
            a->kind = QA_CONST;
            if (!rd_u32(&r, &a->value)) {
                return r.status;
            }
        } else if (type >= QA_STRING && type <= QA_SYMBOL) {
            a->kind = (uint8_t)type;
            if (!rd_string(&r, &a->length, &a->wide, &a->offset)) {
                return r.status;
            }
        } else {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        f->atom_count = i + 1u;
    }
    if (!rd_object(&r, 0, QJS_NONE, QJS_NONE, NULL)) {
        return r.status;
    }
    if (f->function_count == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION; /* no bytecode: not a compiled script */
    }
    f->end = r.pos;
    return XXBYTE_STATUS_OK;
}

/* Checks every function's bytecode against its tables and counts. */
static int qjs_valid(const xxbyte_quickjs_file *f)
{
    uint32_t i;

    for (i = 0; i < f->function_count; ++i) {
        const xxbyte_quickjs_function *fn = &f->funcs[i].pub;
        const uint8_t *code = f->data + fn->bytecode_offset;
        uint32_t pos = 0;

        while (pos < fn->bytecode_size) {
            xxbyte_instruction insn;
            uint32_t k;
            if (xxbyte_decode(XXBYTE_FAMILY_QUICKJS, f->mode, code + pos, fn->bytecode_size - pos, pos, &insn) != XXBYTE_STATUS_OK) {
                return 0;
            }
            for (k = 0; k < insn.operand_count; ++k) {
                const xxbyte_operand *o = &insn.operands[k];
                uint64_t limit = UINT64_MAX;
                if (o->type == XXBYTE_OPERAND_BRANCH) {
                    if (o->value < 0 || o->value >= (int64_t)fn->bytecode_size) {
                        return 0;
                    }
                    continue;
                }
                if (o->type != XXBYTE_OPERAND_INDEX) {
                    continue;
                }
                switch (o->kind) {
                    case XXBYTE_INDEX_LOCAL: limit = fn->var_count; break;
                    case XXBYTE_INDEX_ARGUMENT: limit = fn->arg_count; break;
                    case XXBYTE_INDEX_UPVALUE: limit = fn->closure_var_count; break;
                    case XXBYTE_INDEX_CONSTANT: limit = fn->cpool_count; break;
                    case XXBYTE_QUICKJS_INDEX_ATOM: limit = f->atom_count; break;
                    default: break;
                }
                if ((uint64_t)o->value >= limit) {
                    return 0;
                }
            }
            pos += insn.size;
        }
    }
    return 1;
}

static xxbyte_status qjs_open_version(const qjs_version *v, const uint8_t *data, size_t size, xxbyte_quickjs_file *f)
{
    xxbyte_status status = qjs_read(v, data, size, f);
    if (status != XXBYTE_STATUS_OK) {
        qjs_free_parts(f);
        memset(f, 0, sizeof(*f));
    }
    return status;
}

/* Reads data, detecting the release for XXBYTE_MODE_AUTO. */
static xxbyte_status qjs_open(uint32_t mode, const uint8_t *data, size_t size, xxbyte_quickjs_file *f)
{
    xxbyte_status first = XXBYTE_STATUS_INVALID_INSTRUCTION;
    xxbyte_quickjs_file fallback;
    int have_fallback = 0;
    uint32_t i;

    memset(f, 0, sizeof(*f));
    if (size == 0) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (mode != XXBYTE_MODE_AUTO) {
        const qjs_version *v = qjs_find_version(mode);
        if (v == NULL) {
            return XXBYTE_STATUS_UNSUPPORTED_MODE;
        }
        return qjs_open_version(v, data, size, f);
    }
    memset(&fallback, 0, sizeof(fallback));
    for (i = 0; i < QJS_VERSION_COUNT; ++i) {
        const qjs_version *v = &xxbyte_quickjs_versions[i];
        xxbyte_status status;

        if (data[0] != v->bc_version && !((v->flags & QJS_F_BE_FLAG) != 0 && data[0] == (v->bc_version | 0x40u))) {
            continue;
        }
        status = qjs_open_version(v, data, size, f);
        if (status == XXBYTE_STATUS_OK) {
            if (f->end == size && qjs_valid(f)) {
                if (have_fallback) {
                    qjs_free_parts(&fallback);
                }
                return XXBYTE_STATUS_OK;
            }
            if (!have_fallback) {
                fallback = *f;
                have_fallback = 1;
            } else {
                qjs_free_parts(f);
            }
            memset(f, 0, sizeof(*f));
            continue;
        }
        if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
            if (have_fallback) {
                qjs_free_parts(&fallback);
            }
            return status;
        }
        if (first == XXBYTE_STATUS_INVALID_INSTRUCTION) {
            first = status;
        }
    }
    if (have_fallback) {
        *f = fallback;
        return XXBYTE_STATUS_OK;
    }
    return first;
}

xxbyte_status xxbyte_quickjs_open(uint32_t mode, const uint8_t *data, size_t size, xxbyte_quickjs_file **file)
{
    xxbyte_quickjs_file *f;
    xxbyte_status status;

    if (file == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *file = NULL;
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    f = (xxbyte_quickjs_file *)xx_mem_calloc(1, sizeof(*f));
    if (f == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = qjs_open(mode, data, size, f);
    if (status != XXBYTE_STATUS_OK) {
        xx_mem_free(f);
        return status;
    }
    *file = f;
    return XXBYTE_STATUS_OK;
}

void xxbyte_quickjs_close(xxbyte_quickjs_file *file)
{
    if (file != NULL) {
        qjs_free_parts(file);
        xx_mem_free(file);
    }
}

uint32_t xxbyte_quickjs_file_mode(const xxbyte_quickjs_file *file)
{
    return file != NULL ? file->mode : 0u;
}

uint32_t xxbyte_quickjs_function_count(const xxbyte_quickjs_file *file)
{
    return file != NULL ? file->function_count : 0u;
}

const xxbyte_quickjs_function *xxbyte_quickjs_get_function(const xxbyte_quickjs_file *file, uint32_t index)
{
    return file != NULL && index < file->function_count ? &file->funcs[index].pub : NULL;
}

uint32_t xxbyte_quickjs_atom_count(const xxbyte_quickjs_file *file)
{
    return file != NULL ? file->atom_count : 0u;
}

xxbyte_status xxbyte_quickjs_detect(const uint8_t *data, size_t size, uint32_t *mode)
{
    xxbyte_quickjs_file f;
    xxbyte_status status;

    if (mode == NULL || (data == NULL && size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = qjs_open(XXBYTE_MODE_AUTO, data, size, &f);
    if (status == XXBYTE_STATUS_OK) {
        *mode = f.mode;
        qjs_free_parts(&f);
    }
    return status;
}

/* ----------------------------------------------------------------- names */

/* Appends the text of a raw atom. */
static int qjs_put_atom(const xxbyte_quickjs_file *f, uint32_t atom, xx_buf_t *out)
{
    const char *text;

    if ((atom & 0x80000000u) != 0) {
        return xx_buf_appendf(out, "%u", (unsigned)(atom & 0x7FFFFFFFu)) ? 1 : 0;
    }
    if (atom == 0) {
        return xx_buf_append_str(out, "<null>") ? 1 : 0;
    }
    if (atom <= f->version->atom_count) {
        text = qjs_builtin_text(f->version, atom);
        return qjs_put_atom_text(out, (const uint8_t *)text, strlen(text), 0);
    }
    atom -= f->version->atom_count + 1u;
    if (atom >= f->atom_count) {
        return xx_buf_appendf(out, "atom:%u", (unsigned)atom) ? 1 : 0;
    }
    if (f->atoms[atom].kind == QA_CONST) {
        return qjs_put_atom(f, f->atoms[atom].value, out);
    }
    return qjs_put_atom_text(out, f->data + f->atoms[atom].offset, f->atoms[atom].length, f->atoms[atom].wide);
}

static size_t qjs_copy_out(xx_buf_t *tmp, char *buffer, size_t buffer_size)
{
    size_t length = tmp->size;

    if (!xx_buf_ok(tmp)) {
        xx_buf_free(tmp);
        return 0;
    }
    if (buffer != NULL && buffer_size != 0) {
        size_t n = length < buffer_size - 1u ? length : buffer_size - 1u;
        if (n != 0) {
            memcpy(buffer, tmp->data, n);
        }
        buffer[n] = '\0';
    }
    xx_buf_free(tmp);
    return length;
}

size_t xxbyte_quickjs_atom_text(const xxbyte_quickjs_file *file, uint32_t atom, char *buffer, size_t buffer_size)
{
    xx_buf_t tmp;

    if (file == NULL) {
        return 0;
    }
    xx_buf_init(&tmp);
    if (!qjs_put_atom(file, atom, &tmp)) {
        xx_buf_free(&tmp);
        return 0;
    }
    return qjs_copy_out(&tmp, buffer, buffer_size);
}

static int qjs_put_value(const xxbyte_quickjs_file *f, const qjs_value *v, xx_buf_t *out);

static const char hexdigits[] = "0123456789abcdef";

/* Decimal text of the little-endian magnitude bytes[0..n). */
static int qjs_put_decimal(xx_buf_t *out, const uint8_t *bytes, size_t n, int negative, const char *suffix)
{
    uint32_t limbs[QJS_MAG_MAX / 4u];
    char digits[QJS_MAG_MAX * 3u];
    size_t count = (n + 3u) / 4u;
    size_t nd = 0;
    size_t i;

    while (n > 0 && bytes[n - 1] == 0) {
        n--;
    }
    count = (n + 3u) / 4u;
    if (count > sizeof(limbs) / sizeof(limbs[0])) {
        /* too long for decimal: hexadecimal */
        if ((negative && !xx_buf_append_char(out, '-')) || !xx_buf_append_str(out, "0x")) {
            return 0;
        }
        for (i = n; i-- > 0;) {
            if (!xx_buf_append_char(out, hexdigits[bytes[i] >> 4]) || !xx_buf_append_char(out, hexdigits[bytes[i] & 15u])) {
                return 0;
            }
        }
        return xx_buf_append_str(out, suffix) ? 1 : 0;
    }
    memset(limbs, 0, sizeof(limbs));
    for (i = 0; i < n; ++i) {
        limbs[i / 4u] |= (uint32_t)bytes[i] << ((i % 4u) * 8u);
    }
    while (count > 0) {
        uint64_t rem = 0;
        for (i = count; i-- > 0;) {
            uint64_t cur = (rem << 32) | limbs[i];
            limbs[i] = (uint32_t)(cur / 10u);
            rem = cur % 10u;
        }
        digits[nd++] = (char)('0' + rem);
        while (count > 0 && limbs[count - 1] == 0) {
            count--;
        }
    }
    if (nd == 0) {
        digits[nd++] = '0';
    }
    if (negative && !(nd == 1 && digits[0] == '0') && !xx_buf_append_char(out, '-')) {
        return 0;
    }
    while (nd > 0) {
        if (!xx_buf_append_char(out, digits[--nd])) {
            return 0;
        }
    }
    return xx_buf_append_str(out, suffix) ? 1 : 0;
}

static int qjs_put_double(xx_buf_t *out, double d)
{
    xxbyte_fmt fmt;

    memset(&fmt, 0, sizeof(fmt));
    fmt.out = out;
    return xxbyte_fmt_double(&fmt, d);
}

/* BigInt / BigFloat / BigDecimal constant text. */
static int qjs_put_bignum(const xxbyte_quickjs_file *f, const qjs_value *v, xx_buf_t *out)
{
    const uint8_t *b = f->data + v->offset;
    uint8_t mag[QJS_MAG_MAX];
    size_t n = v->length;
    size_t i;

    if (v->special == 1) {
        return xx_buf_append_str(out, v->tag == QV_BIG_INT ? "0n" : v->negative ? "-0" : "0") ? 1 : 0;
    }
    if (v->special == 2) {
        return xx_buf_append_str(out, v->negative ? "-Infinity" : "Infinity") ? 1 : 0;
    }
    if (v->special == 3) {
        return xx_buf_append_str(out, "NaN") ? 1 : 0;
    }
    if (v->tag == QV_BIG_DECIMAL) {
        /* digits from the least significant: value = 0.DIGITS * 10^expn */
        if ((v->negative && !xx_buf_append_char(out, '-')) || !xx_buf_append_str(out, "0.")) {
            return 0;
        }
        for (i = n; i-- > 0;) {
            uint32_t d = (uint32_t)(b[i / 2u] >> ((i & 1u) * 4u)) & 15u;
            if (!xx_buf_append_char(out, (char)('0' + d))) {
                return 0;
            }
        }
        return xx_buf_appendf(out, "e%lldm", (long long)v->number) ? 1 : 0;
    }
    if (v->tag == QV_BIG_FLOAT || (f->version->flags & QJS_F_BIGINT_BF) != 0) {
        /* libbf: value = B * 2^(expn - 8 * len), B the bytes little-endian */
        int64_t shift = (int64_t)8 * (int64_t)n - v->number;
        if (v->tag == QV_BIG_INT && shift > -(int64_t)(8u * QJS_MAG_MAX) && shift < (int64_t)(8u * (n + 1u))) {
            int64_t m = ((int64_t)8 * (int64_t)n - shift + 7) / 8;
            int64_t k;
            if (m > (int64_t)QJS_MAG_MAX) {
                return xx_buf_appendf(out, "[BigInt %u bytes]", (unsigned)n) ? 1 : 0;
            }
            for (k = 0; k < m; ++k) {
                uint32_t byte = 0;
                uint32_t bit;
                for (bit = 0; bit < 8u; ++bit) {
                    int64_t src = k * 8 + (int64_t)bit + shift;
                    if (src >= 0 && src < (int64_t)(8u * n) && (((uint32_t)b[src / 8] >> (uint32_t)(src % 8)) & 1u) != 0) {
                        byte |= 1u << bit;
                    }
                }
                mag[k] = (uint8_t)byte;
            }
            return qjs_put_decimal(out, mag, m > 0 ? (size_t)m : 0u, v->negative, "n");
        } else {
            double d = 0.0;
            double scale = 1.0;
            for (i = 0; i < n; ++i) {
                d += (double)b[i] * scale;
                scale *= 256.0;
            }
            for (; shift > 0 && d != 0.0; --shift) {
                d *= 0.5;
            }
            for (; shift < 0 && d < 1e308; ++shift) {
                d *= 2.0;
            }
            if (v->negative) {
                d = -d;
            }
            if (!qjs_put_double(out, d)) {
                return 0;
            }
            return xx_buf_append_str(out, v->tag == QV_BIG_INT ? "n" : "l") ? 1 : 0;
        }
    }
    /* two's complement little-endian */
    if (n == 0) {
        return xx_buf_append_str(out, "0n") ? 1 : 0;
    }
    if (n > QJS_MAG_MAX) {
        return xx_buf_appendf(out, "[BigInt %u bytes]", (unsigned)n) ? 1 : 0;
    }
    if ((b[n - 1] & 0x80u) != 0) {
        unsigned carry = 1;
        for (i = 0; i < n; ++i) {
            unsigned x = (unsigned)(uint8_t)~b[i] + carry;
            mag[i] = (uint8_t)x;
            carry = x >> 8;
        }
        return qjs_put_decimal(out, mag, n, 1, "n");
    }
    memcpy(mag, b, n);
    return qjs_put_decimal(out, mag, n, 0, "n");
}

/* Array and template constants: up to 8 scalar elements, "..." for more
 * or for nested objects. */
static int qjs_put_elements(const xxbyte_quickjs_file *f, const qjs_value *v, xx_buf_t *out)
{
    qjs_reader r;
    uint32_t i;

    memset(&r, 0, sizeof(r));
    r.f = (xxbyte_quickjs_file *)(uintptr_t)f;
    r.p = f->data;
    r.size = f->size;
    r.pos = v->offset;
    if (!xx_buf_append_str(out, v->tag == QV_TEMPLATE ? "[template " : "[")) {
        return 0;
    }
    for (i = 0; i < v->length; ++i) {
        qjs_value e;
        uint32_t tag;
        int32_t sv;
        int ok = 1;

        if (i == 8) {
            return xx_buf_append_str(out, ", ...]") ? 1 : 0;
        }
        if ((i != 0 && !xx_buf_append_str(out, ", ")) || !rd_u8(&r, &tag)) {
            break;
        }
        memset(&e, 0, sizeof(e));
        e.tag = qjs_tag(f->version, tag);
        switch (e.tag) {
            case QV_NULL:
            case QV_UNDEFINED:
            case QV_FALSE:
            case QV_TRUE: break;
            case QV_INT32:
                ok = rd_sleb(&r, &sv);
                e.number = sv;
                break;
            case QV_FLOAT64:
                ok = rd_need(&r, 8);
                if (ok) {
                    e.number = (int64_t)xxbyte_u64le(r.p + r.pos);
                    r.pos += 8;
                }
                break;
            case QV_STRING: ok = rd_string(&r, &e.length, &e.wide, &e.offset); break;
            default: ok = 0; break;
        }
        if (!ok) {
            return xx_buf_append_str(out, "...]") ? 1 : 0;
        }
        if (!qjs_put_value(f, &e, out)) {
            return 0;
        }
    }
    return xx_buf_append_char(out, ']') ? 1 : 0;
}

static int qjs_put_value(const xxbyte_quickjs_file *f, const qjs_value *v, xx_buf_t *out)
{
    union {
        uint64_t u;
        double d;
    } u;

    switch (v->tag) {
        case QV_NULL: return xx_buf_append_str(out, "null") ? 1 : 0;
        case QV_UNDEFINED: return xx_buf_append_str(out, "undefined") ? 1 : 0;
        case QV_FALSE: return xx_buf_append_str(out, "false") ? 1 : 0;
        case QV_TRUE: return xx_buf_append_str(out, "true") ? 1 : 0;
        case QV_INT32: return xx_buf_appendf(out, "%lld", (long long)v->number) ? 1 : 0;
        case QV_FLOAT64: u.u = (uint64_t)v->number; return qjs_put_double(out, u.d);
        case QV_STRING: return qjs_put_quoted(out, f->data + v->offset, v->length, v->wide, 200);
        case QV_FUNCTION:
            if (!xx_buf_append_str(out, "[bytecode ") || (v->number < (int64_t)f->function_count && !qjs_put_atom(f, f->funcs[v->number].pub.name_atom, out))) {
                return 0;
            }
            return xx_buf_append_char(out, ']') ? 1 : 0;
        case QV_BIG_INT:
        case QV_BIG_FLOAT:
        case QV_BIG_DECIMAL: return qjs_put_bignum(f, v, out);
        case QV_TEMPLATE:
        case QV_ARRAY: return qjs_put_elements(f, v, out);
        case QV_OBJECT: return xx_buf_appendf(out, "[object %u]", (unsigned)v->length) ? 1 : 0;
        case QV_REGEXP:
            if (!xx_buf_append_str(out, "[regexp ")) {
                return 0;
            }
            if (!qjs_put_quoted(out, f->data + v->offset, v->length, v->wide, 200)) {
                return 0;
            }
            return xx_buf_append_char(out, ']') ? 1 : 0;
        case QV_TYPED_ARRAY: return xx_buf_appendf(out, "[typed array %u]", (unsigned)v->length) ? 1 : 0;
        case QV_ARRAY_BUFFER: return xx_buf_appendf(out, "[ArrayBuffer %u]", (unsigned)v->length) ? 1 : 0;
        case QV_SHARED_ARRAY_BUFFER: return xx_buf_appendf(out, "[SharedArrayBuffer %u]", (unsigned)v->length) ? 1 : 0;
        case QV_DATE: return xx_buf_append_str(out, "[Date]") ? 1 : 0;
        case QV_OBJECT_VALUE: return xx_buf_append_str(out, "[object value]") ? 1 : 0;
        case QV_OBJECT_REFERENCE: return xx_buf_appendf(out, "[reference %lld]", (long long)v->number) ? 1 : 0;
        case QV_MAP: return xx_buf_appendf(out, "[Map %u]", (unsigned)v->length) ? 1 : 0;
        case QV_SET: return xx_buf_appendf(out, "[Set %u]", (unsigned)v->length) ? 1 : 0;
        case QV_SYMBOL:
            if (!xx_buf_append_str(out, "Symbol(") || !qjs_put_atom(f, (uint32_t)v->number, out)) {
                return 0;
            }
            return xx_buf_append_char(out, ')') ? 1 : 0;
        case QV_MODULE: return xx_buf_append_str(out, "[module]") ? 1 : 0;
        default: return xx_buf_append_str(out, "?") ? 1 : 0;
    }
}

static int qjs_put_name_of(const xxbyte_quickjs_file *f, uint32_t function, uint32_t kind, int64_t index, xx_buf_t *out)
{
    const xxbyte_quickjs_function *fn;
    const qjs_func *ext;

    if (function >= f->function_count || index < 0) {
        return -1;
    }
    fn = &f->funcs[function].pub;
    ext = &f->funcs[function];
    switch (kind) {
        case XXBYTE_INDEX_LOCAL:
            if ((uint64_t)index >= fn->var_count || (uint64_t)fn->arg_count + (uint64_t)index >= fn->local_count) {
                return -1;
            }
            return qjs_put_atom(f, f->vardefs[ext->vardef_first + fn->arg_count + (uint32_t)index].name, out);
        case XXBYTE_INDEX_ARGUMENT:
            if ((uint64_t)index >= fn->arg_count || (uint64_t)index >= fn->local_count) {
                return -1;
            }
            return qjs_put_atom(f, f->vardefs[ext->vardef_first + (uint32_t)index].name, out);
        case XXBYTE_INDEX_UPVALUE:
            if ((uint64_t)index >= fn->closure_var_count) {
                return -1;
            }
            return qjs_put_atom(f, f->closures[ext->closure_first + (uint32_t)index].name, out);
        case XXBYTE_INDEX_CONSTANT:
            if ((uint64_t)index >= fn->cpool_count) {
                return -1;
            }
            return qjs_put_value(f, &f->values[ext->value_first + (uint32_t)index], out);
        default: return -1;
    }
}

size_t xxbyte_quickjs_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_quickjs_names *names = (const xxbyte_quickjs_names *)context;
    const xxbyte_operand *operand;
    xx_buf_t tmp;
    int r;

    if (names == NULL || names->file == NULL || instruction == NULL || operand_index >= instruction->operand_count || instruction->family != XXBYTE_FAMILY_QUICKJS) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (operand->type != XXBYTE_OPERAND_INDEX) {
        return 0;
    }
    xx_buf_init(&tmp);
    if (operand->kind == XXBYTE_QUICKJS_INDEX_ATOM || operand->kind == XXBYTE_QUICKJS_INDEX_BUILTIN_ATOM) {
        r = qjs_put_atom(names->file, (uint32_t)operand->extra, &tmp);
    } else {
        r = qjs_put_name_of(names->file, names->function, operand->kind, operand->value, &tmp);
    }
    if (r <= 0) {
        xx_buf_free(&tmp);
        return 0;
    }
    if (buffer_size > 8u && tmp.size >= buffer_size) {
        /* too long for the buffer: cut, marking the cut */
        const char *mark = tmp.data[0] == '"' ? "...\"" : "...";
        size_t keep = buffer_size - 1u - strlen(mark);
        memcpy(buffer, tmp.data, keep);
        memcpy(buffer + keep, mark, strlen(mark) + 1u);
        xx_buf_free(&tmp);
        return buffer_size - 1u;
    }
    return qjs_copy_out(&tmp, buffer, buffer_size);
}

/* --------------------------------------------------------------- listing */

static const char *qjs_var_kind_text(const qjs_vardef *d)
{
    uint32_t kind = d->flags & 15u;

    if (kind == 3u) {
        return "catch";
    }
    if (kind == 1u || kind == 2u) {
        return "function";
    }
    if ((d->flags & 0x10u) != 0) {
        return "const";
    }
    return (d->flags & 0x20u) != 0 ? "let" : "var";
}

static const char *qjs_closure_type_text(uint32_t type)
{
    static const char *const names[] = {"loc", "arg", "ref", "global_ref", "global_decl", "global", "module_decl", "module_import"};
    return type < 8u ? names[type] : "?";
}

static int qjs_list_function(const xxbyte_quickjs_file *f, uint32_t index, xx_buf_t *out)
{
    const xxbyte_quickjs_function *fn = &f->funcs[index].pub;
    const qjs_func *ext = &f->funcs[index];
    static const char *const kinds[] = {"", ", generator", ", async", ", async generator"};
    uint32_t i;

    if (!xx_buf_appendf(out, "\n; function #%u ", (unsigned)index) || !qjs_put_atom(f, fn->name_atom, out)) {
        return 0;
    }
    if (fn->parent == QJS_NONE) {
        if (!xx_buf_append_str(out, " (root)")) {
            return 0;
        }
    } else if (!xx_buf_appendf(out, " (parent #%u, constant %u)", (unsigned)fn->parent, (unsigned)fn->cpool_slot)) {
        return 0;
    }
    if (!xx_buf_appendf(out, ": %u args (%u defined), %u locals, %u closure vars, %u constants, stack %u", (unsigned)fn->arg_count, (unsigned)fn->defined_arg_count,
                        (unsigned)fn->var_count, (unsigned)fn->closure_var_count, (unsigned)fn->cpool_count, (unsigned)fn->stack_size)) {
        return 0;
    }
    if ((f->version->flags & QJS_F_VAR_REF_COUNT) != 0 && !xx_buf_appendf(out, ", var refs %u", (unsigned)fn->var_ref_count)) {
        return 0;
    }
    if (!xx_buf_appendf(out, ", %u bytes%s%s, flags 0x%04x", (unsigned)fn->bytecode_size, (fn->js_mode & 1u) != 0 ? ", strict" : "", kinds[fn->func_kind & 3u],
                        (unsigned)fn->flags)) {
        return 0;
    }
    if (fn->has_debug && fn->filename_atom != 0) {
        if (!xx_buf_append_str(out, ", file ") || !qjs_put_atom(f, fn->filename_atom, out)) {
            return 0;
        }
        if ((f->version->flags & QJS_F_LINE_NUM) != 0 && !xx_buf_appendf(out, ":%u", (unsigned)fn->line)) {
            return 0;
        }
        if ((f->version->flags & QJS_F_COL_NUM) != 0 && !xx_buf_appendf(out, ":%u", (unsigned)fn->column)) {
            return 0;
        }
    }
    if (!xx_buf_append_char(out, '\n')) {
        return 0;
    }
    if (fn->arg_count != 0) {
        if (!xx_buf_append_str(out, ";   args:")) {
            return 0;
        }
        for (i = 0; i < fn->arg_count && i < fn->local_count; ++i) {
            if (!xx_buf_append_char(out, ' ') || !qjs_put_atom(f, f->vardefs[ext->vardef_first + i].name, out)) {
                return 0;
            }
        }
        if (!xx_buf_append_char(out, '\n')) {
            return 0;
        }
    }
    if (fn->local_count > fn->arg_count) {
        if (!xx_buf_append_str(out, ";   locals:\n")) {
            return 0;
        }
        for (i = fn->arg_count; i < fn->local_count; ++i) {
            const qjs_vardef *d = &f->vardefs[ext->vardef_first + i];
            if (!xx_buf_appendf(out, ";     %u: %s ", (unsigned)(i - fn->arg_count), qjs_var_kind_text(d)) || !qjs_put_atom(f, d->name, out)) {
                return 0;
            }
            if ((f->version->flags & QJS_F_VD_SCOPE_LEVEL) != 0 && !xx_buf_appendf(out, " [level:%d next:%d]", (int)d->scope_level, (int)d->scope_next)) {
                return 0;
            }
            if ((f->version->flags & QJS_F_VD_HAS_SCOPE) != 0 && (d->flags & 0x80u) != 0 && !xx_buf_appendf(out, " [next:%d]", (int)d->scope_next)) {
                return 0;
            }
            if ((d->flags & 0x40u) != 0 && !xx_buf_append_str(out, " captured")) {
                return 0;
            }
            if (!xx_buf_append_char(out, '\n')) {
                return 0;
            }
        }
    }
    if (fn->closure_var_count != 0) {
        if (!xx_buf_append_str(out, ";   closure vars:\n")) {
            return 0;
        }
        for (i = 0; i < fn->closure_var_count; ++i) {
            const qjs_closure *c = &f->closures[ext->closure_first + i];
            if (!xx_buf_appendf(out, ";     %u: %s ", (unsigned)i, c->is_const ? "const" : c->is_lexical ? "let" : "var") || !qjs_put_atom(f, c->name, out)) {
                return 0;
            }
            if (c->type < 4u ? !xx_buf_appendf(out, " [%s%u]\n", qjs_closure_type_text(c->type), (unsigned)c->var_idx)
                             : !xx_buf_appendf(out, " [%s]\n", qjs_closure_type_text(c->type))) {
                return 0;
            }
        }
    }
    if (fn->cpool_count != 0) {
        if (!xx_buf_append_str(out, ";   constants:\n")) {
            return 0;
        }
        for (i = 0; i < fn->cpool_count; ++i) {
            const qjs_value *v = &f->values[ext->value_first + i];
            if (!xx_buf_appendf(out, ";     %u: ", (unsigned)i) || !qjs_put_value(f, v, out)) {
                return 0;
            }
            if (v->tag == QV_FUNCTION && !xx_buf_appendf(out, " #%lld", (long long)v->number)) {
                return 0;
            }
            if (!xx_buf_append_char(out, '\n')) {
                return 0;
            }
        }
    }
    return 1;
}

static int qjs_list_header(const xxbyte_quickjs_file *f, xx_buf_t *out)
{
    uint32_t i;

    if (!xx_buf_appendf(out, "; QuickJS %s (BC_VERSION %u), %u atoms, %u functions\n", f->version->name, (unsigned)f->version->bc_version, (unsigned)f->atom_count,
                        (unsigned)f->function_count)) {
        return 0;
    }
    if ((f->version->flags & QJS_F_CHECKSUM) != 0 && !xx_buf_appendf(out, "; checksum 0x%08x (%s)\n", (unsigned)f->checksum,
                                                                     f->checksum == 0xFFFFFFFFu            ? "not checked"
                                                                     : f->checksum == f->checksum_computed ? "ok"
                                                                                                           : "mismatch")) {
        return 0;
    }
    if (f->end != f->size && !xx_buf_appendf(out, "; %llu trailing bytes\n", (unsigned long long)(f->size - f->end))) {
        return 0;
    }
    if (f->atom_count != 0) {
        if (!xx_buf_append_str(out, "; atoms:\n")) {
            return 0;
        }
        for (i = 0; i < f->atom_count; ++i) {
            const qjs_atom_entry *a = &f->atoms[i];
            static const char *const kinds[] = {"", "", " (global symbol)", " (symbol)"};
            if (!xx_buf_appendf(out, ";   %u: ", (unsigned)i)) {
                return 0;
            }
            if (a->kind == QA_CONST) {
                if (!qjs_put_atom(f, a->value, out) || !xx_buf_append_str(out, " (predefined)")) {
                    return 0;
                }
            } else if (!qjs_put_atom_text(out, f->data + a->offset, a->length, a->wide) || !xx_buf_append_str(out, kinds[a->kind])) {
                return 0;
            }
            if (!xx_buf_append_char(out, '\n')) {
                return 0;
            }
        }
    }
    if (f->module.present) {
        if (!xx_buf_append_str(out, "; module ") || !qjs_put_atom(f, f->module.name, out) ||
            !xx_buf_appendf(out, ": %u requests, %u exports, %u star exports, %u imports%s\n", (unsigned)f->module.req_count, (unsigned)f->module.export_count,
                            (unsigned)f->module.star_count, (unsigned)f->module.import_count, f->module.has_tla ? ", top-level await" : "")) {
            return 0;
        }
    }
    return 1;
}

/* Lists the module records. */
static int qjs_list_module(const xxbyte_quickjs_file *f, xx_buf_t *out)
{
    const qjs_module_info *m = &f->module;
    const qjs_mod_entry *e = m->entries;
    uint32_t i;

    for (i = 0; i < m->req_count; ++i, ++e) {
        if (!xx_buf_appendf(out, ";   request %u: ", (unsigned)i) || !qjs_put_atom(f, e->a, out) || !xx_buf_append_char(out, '\n')) {
            return 0;
        }
    }
    for (i = 0; i < m->export_count; ++i, ++e) {
        if (e->kind == 0) {
            if (!xx_buf_appendf(out, ";   export local %u as ", (unsigned)e->x) || !qjs_put_atom(f, e->a, out) || !xx_buf_append_char(out, '\n')) {
                return 0;
            }
        } else if (!xx_buf_append_str(out, ";   export ") || !qjs_put_atom(f, e->b, out) || !xx_buf_appendf(out, " from request %u as ", (unsigned)e->x) ||
                   !qjs_put_atom(f, e->a, out) || !xx_buf_append_char(out, '\n')) {
            return 0;
        }
    }
    for (i = 0; i < m->star_count; ++i, ++e) {
        if (!xx_buf_appendf(out, ";   export * from request %u\n", (unsigned)e->x)) {
            return 0;
        }
    }
    for (i = 0; i < m->import_count; ++i, ++e) {
        if (!xx_buf_append_str(out, e->kind != 0 ? ";   import * as " : ";   import ") || !qjs_put_atom(f, e->a, out) ||
            !xx_buf_appendf(out, " from request %u into closure var %u\n", (unsigned)e->y, (unsigned)e->x)) {
            return 0;
        }
    }
    return 1;
}

xxbyte_status xxbyte_quickjs_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_quickjs_file f;
    xxbyte_status status;
    uint32_t i;

    status = qjs_open(mode, data, size, &f);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (!qjs_list_header(&f, out) || (f.module.present && !qjs_list_module(&f, out))) {
        qjs_free_parts(&f);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < f.function_count; ++i) {
        xxbyte_quickjs_names names;
        const xxbyte_quickjs_function *fn = &f.funcs[i].pub;

        if (!qjs_list_function(&f, i, out)) {
            qjs_free_parts(&f);
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        names.file = &f;
        names.function = i;
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_QUICKJS, f.mode, NULL, data + fn->bytecode_offset, fn->bytecode_size, 0, listing_flags, xxbyte_quickjs_resolve,
                                      &names, out);
        if (status != XXBYTE_STATUS_OK) {
            qjs_free_parts(&f);
            return status;
        }
    }
    qjs_free_parts(&f);
    return XXBYTE_STATUS_OK;
}

#else
typedef int xxbyte_quickjs_file_disabled;
#endif
