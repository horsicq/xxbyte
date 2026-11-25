#include "xxbyte_python_marshal.h"

#if XXBYTE_USE_PYTHON

#include <xxfclib/memory/xx_memory.h>

#define PY_MODE(major, minor) XXBYTE_PYTHON_MODE(major, minor)

/* A reference slot whose object is still being read (a code object,
 * frozenset or slice, which CPython registers only once complete). */
#define PYM_PENDING 0xFFFFFFFEu

/* The fixed singleton nodes, shared by every occurrence. */
#define PYM_SINGLETONS 6u

/* The integer and object fields of one code-object layout. */
typedef struct pym_layout {
    unsigned short_fields; /* 16-bit integers (before 2.3) */
    unsigned kwonly;       /* co_kwonlyargcount (3.0, magic 3030) */
    unsigned posonly;      /* co_posonlyargcount (3.8, magic 3410) */
    unsigned nlocals;      /* co_nlocals (until magic 3452) */
    unsigned freecell;     /* co_freevars and co_cellvars (2.1 until 3453) */
    unsigned localsplus;   /* co_localsplusnames and kinds (magic 3453) */
    unsigned qualname;     /* co_qualname (magic 3460) */
    unsigned exctable;     /* co_exceptiontable (magic 3450) */
    unsigned extra_tables; /* co_endlinetable, co_columntable (3459-3493) */
} pym_layout;

typedef struct pym_reader {
    pym_tree *t;
    size_t pos;
    uint32_t *refs;
    size_t ref_count;
    size_t ref_capacity;
    uint32_t *stack;
    size_t stack_count;
    size_t stack_capacity;
    pym_layout layout;
    int py2;
    int flag_ref;
    unsigned depth;
} pym_reader;

static void pym_layout_of(uint32_t mode, uint32_t magic, pym_layout *l)
{
    memset(l, 0, sizeof(*l));
    if (mode < PY_MODE(3, 0)) {
        l->short_fields = mode < PY_MODE(2, 3);
        l->nlocals = 1;
        l->freecell = mode >= PY_MODE(2, 1);
        return;
    }
    l->kwonly = magic >= 3030;
    l->posonly = magic >= 3410;
    l->nlocals = magic < 3452;
    l->localsplus = magic >= 3453;
    l->freecell = !l->localsplus;
    l->qualname = magic >= 3460;
    l->exctable = magic >= 3450;
    l->extra_tables = magic >= 3459 && magic < 3494;
}

/* Grows *items to hold need elements of elem bytes. */
static int pym_grow(void **items, size_t *capacity, size_t need, size_t elem)
{
    size_t cap = *capacity;
    void *p;

    if (need <= cap) {
        return 1;
    }
    if (cap == 0) {
        cap = 64;
    }
    while (cap < need) {
        if (cap > ((size_t)-1) / 2 / elem) {
            return 0;
        }
        cap *= 2;
    }
    p = xx_mem_realloc(*items, cap * elem);
    if (p == NULL) {
        return 0;
    }
    *items = p;
    *capacity = cap;
    return 1;
}

static xxbyte_status pym_new(pym_reader *r, uint8_t kind, uint8_t type, size_t offset, uint32_t *index)
{
    pym_tree *t = r->t;
    pym_node *n;

    if (t->node_count >= 0xFFFFFFF0u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!pym_grow((void **)&t->nodes, &t->node_capacity, t->node_count + 1, sizeof(pym_node))) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    n = &t->nodes[t->node_count];
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    n->type = type;
    n->offset = (uint32_t)offset;
    *index = (uint32_t)t->node_count++;
    return XXBYTE_STATUS_OK;
}

/* Reserves count kid slots; the count was checked against the input. */
static xxbyte_status pym_kids(pym_reader *r, size_t count, uint32_t *first)
{
    pym_tree *t = r->t;

    if (t->kid_count + count >= 0xFFFFFFF0u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!pym_grow((void **)&t->kids, &t->kid_capacity, t->kid_count + count, sizeof(uint32_t))) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    *first = (uint32_t)t->kid_count;
    if (count == 0) {
        return XXBYTE_STATUS_OK;
    }
    memset(t->kids + t->kid_count, 0, count * sizeof(uint32_t));
    t->kid_count += count;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status pym_ref_add(pym_reader *r, uint32_t node, uint32_t *slot)
{
    if (r->ref_count >= 0x7FFFFFFEu) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!pym_grow((void **)&r->refs, &r->ref_capacity, r->ref_count + 1, sizeof(uint32_t))) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (slot != NULL) {
        *slot = (uint32_t)r->ref_count;
    }
    r->refs[r->ref_count++] = node;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status pym_need(const pym_reader *r, uint64_t count)
{
    return (uint64_t)(r->t->size - r->pos) < count ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OK;
}

static xxbyte_status pym_u8(pym_reader *r, uint32_t *value)
{
    if (r->pos >= r->t->size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *value = r->t->data[r->pos++];
    return XXBYTE_STATUS_OK;
}

static xxbyte_status pym_i32(pym_reader *r, int32_t *value)
{
    if (r->t->size - r->pos < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *value = (int32_t)xxbyte_u32le(r->t->data + r->pos);
    r->pos += 4;
    return XXBYTE_STATUS_OK;
}

/* A code-object integer: a signed 16-bit short before 2.3, else 32-bit. */
static xxbyte_status pym_field(pym_reader *r, int32_t *value)
{
    if (r->layout.short_fields) {
        if (r->t->size - r->pos < 2) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        *value = (int32_t)(int16_t)xxbyte_u16le(r->t->data + r->pos);
        r->pos += 2;
        return XXBYTE_STATUS_OK;
    }
    return pym_i32(r, value);
}

/* A 32-bit size in 0..INT32_MAX of count items of at least unit bytes
 * each, which must fit in the rest of the input. */
static xxbyte_status pym_count(pym_reader *r, uint32_t unit, uint32_t *count)
{
    int32_t n;
    xxbyte_status status = pym_i32(r, &n);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (n < 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    status = pym_need(r, (uint64_t)(uint32_t)n * unit);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    *count = (uint32_t)n;
    return XXBYTE_STATUS_OK;
}

static double pym_double(const uint8_t *p)
{
    uint64_t bits = xxbyte_u64le(p);
    double d;

    memcpy(&d, &bits, sizeof(d));
    return d;
}

static xxbyte_status pym_object(pym_reader *r, uint32_t *out, int allow_null);

/* Reads count objects into kids[first ..). */
static xxbyte_status pym_items(pym_reader *r, uint32_t first, uint32_t count)
{
    uint32_t i;
    xxbyte_status status;

    for (i = 0; i < count; ++i) {
        uint32_t child;
        status = pym_object(r, &child, 0);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        r->t->kids[first + i] = child;
    }
    return XXBYTE_STATUS_OK;
}

/* A string-like payload of n bytes at the read position. */
static xxbyte_status pym_payload(pym_reader *r, uint8_t kind, uint8_t type, size_t start, uint32_t n, uint8_t flags, uint32_t *out)
{
    xxbyte_status status = pym_need(r, n);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    status = pym_new(r, kind, type, start, out);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (kind == PYM_STR || kind == PYM_BYTES) {
        /* repr's quote depends on the whole text: decide it once here, so
         * that showing a long string many times stays cheap. */
        const uint8_t *p = r->t->data + r->pos;
        int single = memchr(p, '\'', n) != NULL;
        if (single && memchr(p, '"', n) == NULL) {
            flags = (uint8_t)(flags | PYM_FLAG_DQUOTE);
        }
    }
    r->t->nodes[*out].data = (uint32_t)r->pos;
    r->t->nodes[*out].size = n;
    r->t->nodes[*out].flags = flags;
    r->pos += n;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status pym_code_object(pym_reader *r, size_t start, int flag, uint32_t *out)
{
    const pym_layout *l = &r->layout;
    pym_tree *t = r->t;
    pym_code c;
    uint32_t slot = 0;
    uint32_t skip;
    uint32_t index;
    xxbyte_status status;

#define PYM_INT_FIELD(dst)                \
    do {                                  \
        status = pym_field(r, &(dst));    \
        if (status != XXBYTE_STATUS_OK) { \
            return status;                \
        }                                 \
    } while (0)
#define PYM_OBJ_FIELD(dst)                 \
    do {                                   \
        status = pym_object(r, &(dst), 0); \
        if (status != XXBYTE_STATUS_OK) {  \
            return status;                 \
        }                                  \
    } while (0)

    memset(&c, 0, sizeof(c));
    c.code = c.consts = c.names = c.varnames = c.freevars = c.cellvars = PYM_NONE_INDEX;
    c.kinds = c.filename = c.name = c.qualname = c.linetable = c.exceptiontable = PYM_NONE_INDEX;
    if (flag) {
        status = pym_ref_add(r, PYM_PENDING, &slot);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    PYM_INT_FIELD(c.argcount);
    if (l->posonly) {
        PYM_INT_FIELD(c.posonlyargcount);
    }
    if (l->kwonly) {
        PYM_INT_FIELD(c.kwonlyargcount);
    }
    if (l->nlocals) {
        PYM_INT_FIELD(c.nlocals);
    }
    PYM_INT_FIELD(c.stacksize);
    {
        int32_t flags;
        PYM_INT_FIELD(flags);
        c.flags = (uint32_t)flags;
        if (l->short_fields) {
            c.flags &= 0xFFFFu;
        }
    }
    PYM_OBJ_FIELD(c.code);
    PYM_OBJ_FIELD(c.consts);
    PYM_OBJ_FIELD(c.names);
    PYM_OBJ_FIELD(c.varnames);
    if (l->localsplus) {
        PYM_OBJ_FIELD(c.kinds);
    } else if (l->freecell) {
        PYM_OBJ_FIELD(c.freevars);
        PYM_OBJ_FIELD(c.cellvars);
    }
    PYM_OBJ_FIELD(c.filename);
    PYM_OBJ_FIELD(c.name);
    if (l->qualname) {
        PYM_OBJ_FIELD(c.qualname);
    }
    PYM_INT_FIELD(c.firstlineno);
    PYM_OBJ_FIELD(c.linetable);
    if (l->extra_tables) {
        PYM_OBJ_FIELD(skip);
        PYM_OBJ_FIELD(skip);
    }
    if (l->exctable) {
        PYM_OBJ_FIELD(c.exceptiontable);
    }
#undef PYM_INT_FIELD
#undef PYM_OBJ_FIELD
    /* CPython requires co_code to be bytes (a Python 2 str). */
    if (t->nodes[c.code].kind != PYM_BYTES) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (t->code_count >= 0xFFFFFFF0u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!pym_grow((void **)&t->codes, &t->code_capacity, t->code_count + 1, sizeof(pym_code))) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = pym_new(r, PYM_CODE, 'c', start, &index);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    c.node = index;
    t->nodes[index].first = (uint32_t)t->code_count;
    t->codes[t->code_count++] = c;
    if (flag) {
        r->refs[slot] = index;
    }
    *out = index;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status pym_dict(pym_reader *r, size_t start, int flag, uint32_t *out)
{
    pym_tree *t = r->t;
    size_t mark = r->stack_count;
    uint32_t index;
    uint32_t first;
    size_t pairs;
    xxbyte_status status = pym_new(r, PYM_DICT, '{', start, &index);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (flag) {
        status = pym_ref_add(r, index, NULL);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    for (;;) {
        uint32_t key;
        uint32_t value;
        status = pym_object(r, &key, 1);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (t->nodes[key].kind == PYM_NULL) {
            break;
        }
        status = pym_object(r, &value, 1);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        /* CPython ends the dict at a NULL value too, without an error. */
        if (t->nodes[value].kind == PYM_NULL) {
            break;
        }
        if (!pym_grow((void **)&r->stack, &r->stack_capacity, r->stack_count + 2, sizeof(uint32_t))) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        r->stack[r->stack_count++] = key;
        r->stack[r->stack_count++] = value;
    }
    pairs = (r->stack_count - mark) / 2;
    status = pym_kids(r, pairs * 2, &first);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (pairs != 0) {
        memcpy(t->kids + first, r->stack + mark, pairs * 2 * sizeof(uint32_t));
    }
    r->stack_count = mark;
    t->nodes[index].first = first;
    t->nodes[index].size = (uint32_t)pairs;
    *out = index;
    return XXBYTE_STATUS_OK;
}

/* Tuple, list, set, frozenset (count already read) and slice (count 3). */
static xxbyte_status pym_sequence(pym_reader *r, uint8_t kind, uint8_t type, size_t start, uint32_t count, int flag, uint32_t *out)
{
    uint32_t index;
    uint32_t first = 0;
    uint32_t slot = 0;
    int late = flag && (kind == PYM_FROZENSET || kind == PYM_SLICE) && !(kind == PYM_FROZENSET && count == 0);
    xxbyte_status status;

    if (late) {
        status = pym_ref_add(r, PYM_PENDING, &slot);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    status = pym_new(r, kind, type, start, &index);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (flag && !late) {
        status = pym_ref_add(r, index, NULL);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    status = pym_kids(r, count, &first);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    r->t->nodes[index].first = first;
    r->t->nodes[index].size = count;
    status = pym_items(r, first, count);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (late) {
        r->refs[slot] = index;
    }
    *out = index;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status pym_object_body(pym_reader *r, uint32_t *out, int allow_null)
{
    pym_tree *t = r->t;
    size_t start = r->pos;
    uint32_t code;
    uint32_t type;
    uint32_t n32;
    int32_t n;
    int flag = 0;
    uint32_t index;
    xxbyte_status status = pym_u8(r, &code);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    type = code;
    if ((code & 0x80u) != 0) {
        if (!r->flag_ref) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        flag = 1;
        type = code & 0x7Fu;
    }
    switch (type) {
        case '0':
            if (!allow_null) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            *out = PYM_NULL;
            return XXBYTE_STATUS_OK;
        case 'N': *out = PYM_NONE; return XXBYTE_STATUS_OK;
        case 'T': *out = PYM_TRUE; return XXBYTE_STATUS_OK;
        case 'F': *out = PYM_FALSE; return XXBYTE_STATUS_OK;
        case '.': *out = PYM_ELLIPSIS; return XXBYTE_STATUS_OK;
        case 'S': *out = PYM_STOPITER; return XXBYTE_STATUS_OK;
        case 'i':
            status = pym_i32(r, &n);
            if (status == XXBYTE_STATUS_OK) {
                status = pym_new(r, PYM_INT, (uint8_t)type, start, &index);
            }
            if (status == XXBYTE_STATUS_OK) {
                t->nodes[index].v.i = n;
            }
            break;
        case 'I':
            status = pym_need(r, 8);
            if (status == XXBYTE_STATUS_OK) {
                status = pym_new(r, PYM_INT, (uint8_t)type, start, &index);
            }
            if (status == XXBYTE_STATUS_OK) {
                t->nodes[index].v.i = (int64_t)xxbyte_u64le(t->data + r->pos);
                t->nodes[index].flags = PYM_FLAG_INT64;
                r->pos += 8;
            }
            break;
        case 'l': {
            uint32_t digits;
            status = pym_i32(r, &n);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (n == INT32_MIN) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            digits = (uint32_t)(n < 0 ? -n : n);
            status = pym_payload(r, PYM_LONG, (uint8_t)type, start, digits * 2u, (uint8_t)(n < 0 ? PYM_FLAG_NEG : 0u), &index);
            if (status == XXBYTE_STATUS_OK) {
                t->nodes[index].size = digits;
            }
            break;
        }
        case 'f':
            status = pym_u8(r, &n32);
            if (status == XXBYTE_STATUS_OK) {
                status = pym_payload(r, PYM_FLOAT_TEXT, (uint8_t)type, start, n32, 0, &index);
            }
            break;
        case 'g':
            status = pym_need(r, 8);
            if (status == XXBYTE_STATUS_OK) {
                status = pym_new(r, PYM_FLOAT, (uint8_t)type, start, &index);
            }
            if (status == XXBYTE_STATUS_OK) {
                t->nodes[index].v.d[0] = pym_double(t->data + r->pos);
                r->pos += 8;
            }
            break;
        case 'x': {
            uint32_t real_at;
            uint32_t real_size;
            status = pym_u8(r, &real_size);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            status = pym_need(r, real_size);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            real_at = (uint32_t)r->pos;
            r->pos += real_size;
            status = pym_u8(r, &n32);
            if (status == XXBYTE_STATUS_OK) {
                status = pym_payload(r, PYM_COMPLEX_TEXT, (uint8_t)type, start, n32, 0, &index);
            }
            if (status == XXBYTE_STATUS_OK) {
                pym_node *node = &t->nodes[index];
                node->first = node->data;
                node->extra = node->size;
                node->data = real_at;
                node->size = real_size;
            }
            break;
        }
        case 'y':
            status = pym_need(r, 16);
            if (status == XXBYTE_STATUS_OK) {
                status = pym_new(r, PYM_COMPLEX, (uint8_t)type, start, &index);
            }
            if (status == XXBYTE_STATUS_OK) {
                t->nodes[index].v.d[0] = pym_double(t->data + r->pos);
                t->nodes[index].v.d[1] = pym_double(t->data + r->pos + 8);
                r->pos += 16;
            }
            break;
        case 's':
        case 't':
            status = pym_count(r, 1, &n32);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (type == 't' && !r->py2) {
                status = pym_payload(r, PYM_STR, (uint8_t)type, start, n32, 0, &index);
                break;
            }
            status = pym_payload(r, PYM_BYTES, (uint8_t)type, start, n32, 0, &index);
            if (status == XXBYTE_STATUS_OK && type == 't') {
                /* Python 2: interned strings are numbered for TYPE_STRINGREF. */
                status = pym_ref_add(r, index, NULL);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                *out = index;
                return XXBYTE_STATUS_OK;
            }
            break;
        case 'R':
            if (!r->py2) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            status = pym_i32(r, &n);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (n < 0 || (size_t)n >= r->ref_count) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            *out = r->refs[n];
            return XXBYTE_STATUS_OK;
        case 'u':
            status = pym_count(r, 1, &n32);
            if (status == XXBYTE_STATUS_OK) {
                status = pym_payload(r, PYM_STR, (uint8_t)type, start, n32, r->py2 ? PYM_FLAG_PY2_UNICODE : 0, &index);
            }
            break;
        case 'a':
        case 'A':
            if (r->py2) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            status = pym_count(r, 1, &n32);
            if (status == XXBYTE_STATUS_OK) {
                status = pym_payload(r, PYM_STR, (uint8_t)type, start, n32, PYM_FLAG_LATIN1, &index);
            }
            break;
        case 'z':
        case 'Z':
            if (r->py2) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            status = pym_u8(r, &n32);
            if (status == XXBYTE_STATUS_OK) {
                status = pym_payload(r, PYM_STR, (uint8_t)type, start, n32, PYM_FLAG_LATIN1, &index);
            }
            break;
        case ')':
            if (r->py2) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            status = pym_u8(r, &n32);
            if (status == XXBYTE_STATUS_OK) {
                status = pym_need(r, n32);
            }
            if (status == XXBYTE_STATUS_OK) {
                status = pym_sequence(r, PYM_TUPLE, (uint8_t)type, start, n32, flag, out);
            }
            return status;
        case '(':
        case '[':
        case '<':
        case '>':
            status = pym_count(r, 1, &n32);
            if (status == XXBYTE_STATUS_OK) {
                uint8_t kind = type == '(' ? PYM_TUPLE : type == '[' ? PYM_LIST : type == '<' ? PYM_SET : PYM_FROZENSET;
                status = pym_sequence(r, kind, (uint8_t)type, start, n32, flag, out);
            }
            return status;
        case ':':
            if (r->py2) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            return pym_sequence(r, PYM_SLICE, (uint8_t)type, start, 3, flag, out);
        case '{': return pym_dict(r, start, flag, out);
        case 'c': return pym_code_object(r, start, flag, out);
        case 'r':
            if (!r->flag_ref) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            status = pym_i32(r, &n);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (n < 0 || (size_t)n >= r->ref_count || r->refs[n] == PYM_PENDING) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            *out = r->refs[n];
            return XXBYTE_STATUS_OK;
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (flag) {
        status = pym_ref_add(r, index, NULL);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    *out = index;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status pym_object(pym_reader *r, uint32_t *out, int allow_null)
{
    xxbyte_status status;

    if (r->depth >= XXBYTE_PYTHON_MAX_MARSHAL_DEPTH) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    ++r->depth;
    status = pym_object_body(r, out, allow_null);
    --r->depth;
    return status;
}

/* The preorder walk of the code objects reachable from the root through
 * co_consts, as dis recurses. */
static xxbyte_status pym_build_walk(pym_tree *t)
{
    typedef struct frame {
        uint32_t walk;
        uint32_t next;
    } frame;
    frame *stack = NULL;
    size_t depth = 0;
    size_t stack_capacity = 0;
    size_t walk_capacity = 0;
    xxbyte_status status = XXBYTE_STATUS_OK;

    if (t->nodes[t->root].kind != PYM_CODE) {
        return XXBYTE_STATUS_OK;
    }
    if (!pym_grow((void **)&t->walk, &walk_capacity, 1, sizeof(pym_walk)) || !pym_grow((void **)&stack, &stack_capacity, 1, sizeof(frame))) {
        xx_mem_free(stack);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    t->walk[0].code = t->nodes[t->root].first;
    t->walk[0].parent = PYM_NONE_INDEX;
    t->walk[0].depth = 0;
    t->walk_count = 1;
    stack[0].walk = 0;
    stack[0].next = 0;
    depth = 1;
    while (depth > 0) {
        frame *f = &stack[depth - 1];
        const pym_code *c = &t->codes[t->walk[f->walk].code];
        uint32_t consts = c->consts;
        uint32_t child;

        if (consts == PYM_NONE_INDEX || t->nodes[consts].kind != PYM_TUPLE || f->next >= t->nodes[consts].size) {
            --depth;
            continue;
        }
        child = t->kids[t->nodes[consts].first + f->next++];
        if (t->nodes[child].kind != PYM_CODE) {
            continue;
        }
        if (t->walk_count >= XXBYTE_PYTHON_MAX_CODE_OBJECTS || depth >= XXBYTE_PYTHON_MAX_MARSHAL_DEPTH) {
            t->walk_truncated = 1;
            if (t->walk_count >= XXBYTE_PYTHON_MAX_CODE_OBJECTS) {
                break;
            }
            continue;
        }
        if (!pym_grow((void **)&t->walk, &walk_capacity, t->walk_count + 1, sizeof(pym_walk)) || !pym_grow((void **)&stack, &stack_capacity, depth + 1, sizeof(frame))) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        f = &stack[depth - 1];
        t->walk[t->walk_count].code = t->nodes[child].first;
        t->walk[t->walk_count].parent = f->walk;
        t->walk[t->walk_count].depth = (uint32_t)depth;
        stack[depth].walk = (uint32_t)t->walk_count;
        stack[depth].next = 0;
        ++t->walk_count;
        ++depth;
    }
    xx_mem_free(stack);
    return status;
}

void xxbyte_pym_free(pym_tree *tree)
{
    if (tree == NULL) {
        return;
    }
    xx_mem_free(tree->nodes);
    xx_mem_free(tree->kids);
    xx_mem_free(tree->codes);
    xx_mem_free(tree->walk);
    memset(tree, 0, sizeof(*tree));
}

xxbyte_status xxbyte_pym_parse(pym_tree *tree, uint32_t mode, uint32_t magic, const uint8_t *data, size_t size, size_t offset)
{
    pym_reader r;
    xxbyte_status status;
    uint32_t i;

    memset(tree, 0, sizeof(*tree));
    if (size > 0xFFFFFFF0u) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    tree->data = data;
    tree->size = size;
    tree->mode = mode;
    tree->magic = magic;
    memset(&r, 0, sizeof(r));
    r.t = tree;
    r.pos = offset;
    r.py2 = mode < PY_MODE(3, 0);
    r.flag_ref = mode >= PY_MODE(3, 4);
    pym_layout_of(mode, magic, &r.layout);
    if (!pym_grow((void **)&tree->nodes, &tree->node_capacity, 64, sizeof(pym_node))) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < PYM_SINGLETONS; ++i) {
        memset(&tree->nodes[i], 0, sizeof(pym_node));
        tree->nodes[i].kind = (uint8_t)i;
    }
    tree->node_count = PYM_SINGLETONS;
    status = offset > size ? XXBYTE_STATUS_TRUNCATED : pym_object(&r, &tree->root, 0);
    if (status == XXBYTE_STATUS_OK) {
        tree->end = (uint32_t)r.pos;
        status = pym_build_walk(tree);
    }
    xx_mem_free(r.refs);
    xx_mem_free(r.stack);
    if (status != XXBYTE_STATUS_OK) {
        xxbyte_pym_free(tree);
    }
    return status;
}

uint32_t xxbyte_pym_length(const pym_tree *tree, uint32_t node)
{
    if (node == PYM_NONE_INDEX || node >= tree->node_count) {
        return 0;
    }
    switch (tree->nodes[node].kind) {
        case PYM_TUPLE:
        case PYM_LIST:
        case PYM_SET:
        case PYM_FROZENSET: return tree->nodes[node].size;
        default: return 0;
    }
}

uint32_t xxbyte_pym_item(const pym_tree *tree, uint32_t node, int64_t index)
{
    int64_t count = (int64_t)xxbyte_pym_length(tree, node);

    if (tree->nodes == NULL || node == PYM_NONE_INDEX || (tree->nodes[node].kind != PYM_TUPLE && tree->nodes[node].kind != PYM_LIST)) {
        return PYM_NONE_INDEX;
    }
    if (index < 0) {
        index += count;
    }
    if (index < 0 || index >= count) {
        return PYM_NONE_INDEX;
    }
    return tree->kids[tree->nodes[node].first + (uint32_t)index];
}

#else
typedef int xxbyte_python_marshal_disabled;
#endif
