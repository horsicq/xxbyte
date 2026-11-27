/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_php_internal.h"

#if XXBYTE_USE_PHP

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#define PHP_USER_FUNCTION 2u
#define PHP_MAX_DEPTH 64u

/* The memory image and interned strings of a parsed file, read with one
 * release's layout. */
typedef struct php_img {
    const php_version *v;
    uint32_t vi;
    const uint8_t *mem;
    uint64_t mem_size;
    const uint8_t *str;
    uint64_t str_size;
    uint64_t mem_offset; /* file offset of mem */
} php_img;

static void php_img_init(php_img *m, const xxbyte_php_file *f, uint32_t vi)
{
    m->vi = vi;
    m->v = &php_versions[vi];
    m->mem = f->data + f->mem_offset;
    m->mem_size = f->mem_size;
    m->str = f->data + f->str_offset;
    m->str_size = f->str_size;
    m->mem_offset = f->mem_offset;
}

static int php_in(const php_img *m, uint64_t off, uint64_t len)
{
    return off <= m->mem_size && len <= m->mem_size - off;
}

static uint32_t php_u32(const php_img *m, uint64_t off)
{
    return xxbyte_u32le(m->mem + off);
}

static uint64_t php_u64(const php_img *m, uint64_t off)
{
    return xxbyte_u64le(m->mem + off);
}

/* A mem pointer to an object of len bytes: nonzero, in range, 8-aligned. */
static int php_ptr_ok(const php_img *m, uint64_t ptr, uint64_t len)
{
    return ptr != 0u && (ptr & 7u) == 0u && php_in(m, ptr, len);
}

/* A serialized zend_string pointer. 0 gives NULL. */
static int php_string(const php_img *m, uint64_t ptr, const uint8_t **text, size_t *size)
{
    const uint8_t *base;
    uint64_t region, off, len;

    *text = NULL;
    *size = 0;
    if (ptr == 0u) {
        return 1;
    }
    if ((ptr & 1u) != 0u) {
        base = m->str;
        region = m->str_size;
        off = ptr & ~UINT64_C(1);
    } else {
        base = m->mem;
        region = m->mem_size;
        off = ptr;
    }
    if ((off & 7u) != 0u || off > region || region - off < 24u) {
        return 0;
    }
    len = xxbyte_u64le(base + off + 16);
    if (len > region - off - 24u) {
        return 0;
    }
    *text = base + off + 24;
    *size = (size_t)len;
    return 1;
}

/* ---- hash tables ---- */

typedef struct php_hash {
    uint64_t data;  /* arData (mem offset) */
    uint32_t used;  /* nNumUsed */
    uint32_t count; /* nNumOfElements */
    uint32_t esize; /* 32 (Bucket) or 16 (8.2+ packed zval) */
    int packed;
} php_hash;

static int php_hash_read(const php_img *m, uint64_t ht, php_hash *h)
{
    uint32_t flags;
    const php_version *v = m->v;

    memset(h, 0, sizeof(*h));
    if (!php_in(m, ht, 56u) || (ht & 7u) != 0u) {
        return 0;
    }
    /* A zend_array: GC type IS_ARRAY, a power-of-two table size. */
    if ((php_u32(m, ht + 4) & 15u) != 7u) {
        return 0;
    }
    {
        uint32_t table_size = php_u32(m, ht + 32);
        if (table_size == 0u || (table_size & (table_size - 1u)) != 0u) {
            return 0;
        }
    }
    flags = php_u32(m, ht + 8);
    h->data = php_u64(m, ht + 16);
    if ((v->hash_uninit != 0u && (flags & v->hash_uninit) != 0u) || (v->hash_initialized != 0u && (flags & v->hash_initialized) == 0u) || h->data == 0u) {
        h->data = 0;
        return 1;
    }
    h->used = php_u32(m, ht + 24);
    h->count = php_u32(m, ht + 28);
    h->packed = (flags & v->hash_packed) != 0u;
    h->esize = (h->packed && v->packed_zvals) ? 16u : 32u;
    if (h->count > h->used || h->used > php_u32(m, ht + 32) || (h->data & 7u) != 0u || !php_in(m, h->data, (uint64_t)h->used * h->esize)) {
        return 0;
    }
    return 1;
}

/* Element i (slot, not counting holes): its zval's offset, or 0 for a hole. */
static uint64_t php_hash_slot(const php_img *m, const php_hash *h, uint32_t i)
{
    uint64_t off = h->data + (uint64_t)i * h->esize;
    return m->mem[off + 8] == 0u ? 0u : off;
}

/* The key of slot i: *is_string and the string, or the integer. */
static int php_hash_key(const php_img *m, const php_hash *h, uint32_t i, int *is_string, int64_t *key, const uint8_t **text, size_t *size)
{
    uint64_t off = h->data + (uint64_t)i * h->esize;
    *is_string = 0;
    *text = NULL;
    *size = 0;
    if (h->esize == 16u) {
        *key = (int64_t)i;
        return 1;
    }
    if (php_u64(m, off + 24) == 0u) {
        *key = (int64_t)php_u64(m, off + 16);
        return 1;
    }
    *is_string = 1;
    *key = 0;
    return php_string(m, php_u64(m, off + 24), text, size) && *text != NULL;
}

/* ---- op_arrays ---- */

static int php_read_op_array(const php_img *m, uint64_t off, uint32_t kind, xxbyte_php_op_array *a)
{
    const php_version *v = m->v;
    uint64_t p;
    int32_t n;

    memset(a, 0, sizeof(*a));
    a->kind = kind;
    a->parent = XXBYTE_PHP_NO_PARENT;
    if (!php_ptr_ok(m, off, v->oa_size) || m->mem[off + v->oa_type] != PHP_USER_FUNCTION) {
        return 0;
    }
    a->offset = m->mem_offset + off;
    a->fn_flags = php_u32(m, off + v->oa_fn_flags);
    if (!php_string(m, php_u64(m, off + v->oa_function_name), &a->name, &a->name_size)) {
        return 0;
    }
    p = php_u64(m, off + v->oa_scope);
    if (p != 0u) {
        if (!php_ptr_ok(m, p, (uint64_t)v->ce_name + 8u) || !php_string(m, php_u64(m, p + v->ce_name), &a->scope, &a->scope_size)) {
            return 0;
        }
    }
    if (!php_string(m, php_u64(m, off + v->oa_filename), &a->filename, &a->filename_size)) {
        return 0;
    }
    a->line_start = php_u32(m, off + v->oa_line_start);
    a->line_end = php_u32(m, off + v->oa_line_end);
    a->num_args = php_u32(m, off + v->oa_num_args);
    a->required_num_args = php_u32(m, off + v->oa_required_num_args);
    a->T = php_u32(m, off + v->oa_T);

    a->opcode_count = php_u32(m, off + v->oa_last);
    p = php_u64(m, off + v->oa_opcodes);
    if (a->opcode_count != 0u) {
        if (!php_ptr_ok(m, p, (uint64_t)a->opcode_count * XXBYTE_PHP_OP_SIZE)) {
            return 0;
        }
        a->opcodes_offset = m->mem_offset + p;
    }
    n = (int32_t)php_u32(m, off + v->oa_last_var);
    p = php_u64(m, off + v->oa_vars);
    if (n < 0 || (n > 0 && !php_ptr_ok(m, p, (uint64_t)n * 8u))) {
        return 0;
    }
    a->last_var = (uint32_t)n;
    a->vars_offset = n > 0 ? m->mem_offset + p : 0u;
    n = (int32_t)php_u32(m, off + v->oa_last_literal);
    p = php_u64(m, off + v->oa_literals);
    if (n < 0 || (n > 0 && !php_ptr_ok(m, p, (uint64_t)n * 16u))) {
        return 0;
    }
    a->literal_count = (uint32_t)n;
    a->literals_offset = n > 0 ? m->mem_offset + p : 0u;
    if (v->oa_last_live_range != 0u) {
        n = (int32_t)php_u32(m, off + v->oa_last_live_range);
        p = php_u64(m, off + v->oa_live_range);
        if (n < 0 || (n > 0 && !php_ptr_ok(m, p, (uint64_t)n * v->live_range_size))) {
            return 0;
        }
        a->live_range_count = (uint32_t)n;
        a->live_range_offset = n > 0 ? m->mem_offset + p : 0u;
    }
    n = (int32_t)php_u32(m, off + v->oa_last_try_catch);
    p = php_u64(m, off + v->oa_try_catch);
    if (n < 0 || (n > 0 && !php_ptr_ok(m, p, (uint64_t)n * 16u))) {
        return 0;
    }
    a->try_catch_count = (uint32_t)n;
    a->try_catch_offset = n > 0 ? m->mem_offset + p : 0u;
    return 1;
}

typedef struct php_walk {
    const php_img *m;
    xxbyte_php_op_array *arrays;
    size_t capacity;
    size_t count;
} php_walk;

static int php_walk_add(php_walk *w, uint64_t off, uint32_t kind, uint32_t parent, uint32_t depth)
{
    const php_version *v = w->m->v;
    xxbyte_php_op_array a;
    uint32_t index = (uint32_t)w->count;

    if (depth > PHP_MAX_DEPTH || w->count >= XXBYTE_PHP_MAX_OP_ARRAYS || !php_read_op_array(w->m, off, kind, &a)) {
        return 0;
    }
    a.index = index;
    a.parent = parent;
    if (w->count < w->capacity) {
        w->arrays[w->count] = a;
    }
    ++w->count;
    if (v->oa_dynamic != 0u) {
        uint32_t i, n = php_u32(w->m, off + v->oa_num_dynamic);
        uint64_t p = php_u64(w->m, off + v->oa_dynamic);
        if (n != 0u && !php_ptr_ok(w->m, p, (uint64_t)n * 8u)) {
            return 0;
        }
        for (i = 0; i < n; ++i) {
            if (!php_walk_add(w, php_u64(w->m, p + (uint64_t)i * 8u), XXBYTE_PHP_OP_ARRAY_DYNAMIC, index, depth + 1u)) {
                return 0;
            }
        }
    }
    return 1;
}

/* A method or hook op_array of class ce that the dumper lists. */
static int php_listed_member(const php_img *m, uint64_t fn, uint64_t ce, int hook)
{
    const php_version *v = m->v;
    uint32_t flags;
    if (!php_ptr_ok(m, fn, v->oa_size)) {
        return -1;
    }
    if (m->mem[fn + v->oa_type] != PHP_USER_FUNCTION || php_u64(m, fn + v->oa_scope) != ce) {
        return 0;
    }
    flags = php_u32(m, fn + v->oa_fn_flags);
    if (v->acc_trait_clone != 0u && (v->major > 7 || v->minor >= 4) && (flags & v->acc_trait_clone) != 0u) {
        return 0;
    }
    if (!hook && v->major == 8 && v->minor >= 2 && (flags & v->acc_abstract) != 0u) {
        return 0;
    }
    return 1;
}

static int php_walk_class(php_walk *w, uint64_t ce)
{
    const php_img *m = w->m;
    const php_version *v = m->v;
    php_hash h;
    uint32_t i;

    if (!php_ptr_ok(m, ce, (uint64_t)v->ce_properties_info + 56u) || !php_hash_read(m, ce + v->ce_function_table, &h)) {
        return 0;
    }
    for (i = 0; i < h.used; ++i) {
        uint64_t zv = php_hash_slot(m, &h, i);
        int r;
        if (zv == 0u || m->mem[zv + 8] != v->t_ptr) {
            continue;
        }
        r = php_listed_member(m, php_u64(m, zv), ce, 0);
        if (r < 0 || (r > 0 && !php_walk_add(w, php_u64(m, zv), XXBYTE_PHP_OP_ARRAY_METHOD, XXBYTE_PHP_NO_PARENT, 0))) {
            return 0;
        }
    }
    if (v->pi_hooks == 0u) {
        return 1;
    }
    if (!php_hash_read(m, ce + v->ce_properties_info, &h)) {
        return 0;
    }
    for (i = 0; i < h.used; ++i) {
        uint64_t zv = php_hash_slot(m, &h, i), pi, hooks;
        uint32_t k;
        if (zv == 0u || m->mem[zv + 8] != v->t_ptr) {
            continue;
        }
        pi = php_u64(m, zv);
        if (!php_ptr_ok(m, pi, (uint64_t)v->pi_hooks + 8u)) {
            return 0;
        }
        hooks = php_u64(m, pi + v->pi_hooks);
        if (php_u64(m, pi + v->pi_ce) != ce || hooks == 0u) {
            continue;
        }
        if (!php_ptr_ok(m, hooks, (uint64_t)v->hook_count * 8u)) {
            return 0;
        }
        for (k = 0; k < v->hook_count; ++k) {
            uint64_t fn = php_u64(m, hooks + (uint64_t)k * 8u);
            int r;
            if (fn == 0u) {
                continue;
            }
            r = php_listed_member(m, fn, ce, 1);
            if (r < 0 || (r > 0 && !php_walk_add(w, fn, XXBYTE_PHP_OP_ARRAY_HOOK, XXBYTE_PHP_NO_PARENT, 0))) {
                return 0;
            }
        }
    }
    return 1;
}

static int php_walk_script(php_walk *w, uint64_t script)
{
    const php_img *m = w->m;
    const php_version *v = m->v;
    php_hash h;
    uint32_t i;

    if (!php_walk_add(w, script + v->script_main, XXBYTE_PHP_OP_ARRAY_MAIN, XXBYTE_PHP_NO_PARENT, 0)) {
        return 0;
    }
    if (!php_hash_read(m, script + v->script_functions, &h)) {
        return 0;
    }
    for (i = 0; i < h.used; ++i) {
        uint64_t zv = php_hash_slot(m, &h, i);
        if (zv == 0u) {
            continue;
        }
        if (m->mem[zv + 8] != v->t_ptr || !php_walk_add(w, php_u64(m, zv), XXBYTE_PHP_OP_ARRAY_FUNCTION, XXBYTE_PHP_NO_PARENT, 0)) {
            return 0;
        }
    }
    if (!php_hash_read(m, script + v->script_classes, &h)) {
        return 0;
    }
    for (i = 0; i < h.used; ++i) {
        uint64_t zv = php_hash_slot(m, &h, i);
        uint8_t type;
        if (zv == 0u) {
            continue;
        }
        type = m->mem[zv + 8];
        if (type == v->t_alias_ptr && v->major == 8 && v->minor >= 3) {
            continue;
        }
        if ((type != v->t_ptr && type != v->t_alias_ptr) || !php_walk_class(w, php_u64(m, zv))) {
            return 0;
        }
    }
    return 1;
}

/* ---- header and release detection ---- */

static uint32_t php_adler32(uint32_t adler, const uint8_t *p, size_t n)
{
    uint32_t a = adler & 0xFFFFu, b = adler >> 16;
    while (n != 0u) {
        size_t chunk = n < 5552u ? n : 5552u;
        n -= chunk;
        while (chunk-- != 0u) {
            a += *p++;
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
    }
    return (b << 16) | a;
}

/* The script and main op_array read with release vi; 0 when they do not. */
static int php_script_ok(const xxbyte_php_file *f, uint32_t vi)
{
    php_img m;
    const php_version *v;
    xxbyte_php_op_array a;
    uint64_t script = f->script_offset;
    const uint8_t *name;
    size_t name_size;
    const php_opcode_table *t = &php_opcode_tables[vi];
    const uint8_t *last;

    php_img_init(&m, f, vi);
    v = m.v;
    if ((script & 7u) != 0u || !php_in(&m, script, v->script_size) || !php_string(&m, php_u64(&m, script + v->script_filename), &name, &name_size) || name == NULL) {
        return 0;
    }
    if (!php_read_op_array(&m, script + v->script_main, XXBYTE_PHP_OP_ARRAY_MAIN, &a) || a.name != NULL || a.opcode_count == 0u) {
        return 0;
    }
    last = f->data + a.opcodes_offset + (uint64_t)(a.opcode_count - 1u) * XXBYTE_PHP_OP_SIZE;
    /* Code ends in a RETURN, or after dead-code removal in a THROW, JMP
     * or another instruction that does not fall through. */
    return last[28] < t->count && t->ops[last[28]].name != NULL && (t->ops[last[28]].group & XXBYTE_GROUP_STOP) != 0u;
}

static uint32_t php_spec_code(const php_version *v, uint8_t type)
{
    if (type == 1u) {
        return 0; /* _CONST_CODE */
    }
    if (type == 2u) {
        return 1; /* _TMP_CODE */
    }
    if (type == 4u) {
        return 2; /* _VAR_CODE */
    }
    return type == v->is_cv ? 4u : 3u; /* _CV_CODE, _UNUSED_CODE */
}

/* The specialization offset zend_vm_get_opcode_handler_idx adds for rules
 * (observers off). */
static uint32_t php_spec_offset(uint32_t vi, uint32_t rules, const php_raw_op *r, const uint8_t *next)
{
    const php_version *v = &php_versions[vi];
    int cumulative = v->major == 7 && v->minor <= 2;
    uint32_t off = 0;

    if ((rules & PHP_HRULE_OP1) != 0u) {
        off = off * 5u + php_spec_code(v, r->op1_type);
    }
    if ((rules & PHP_HRULE_OP2) != 0u) {
        off = off * 5u + php_spec_code(v, r->op2_type);
    }
    /* 7.3+ applies one of the remaining rules, 7.1/7.2 all of them. */
    if ((rules & PHP_HRULE_OP_DATA) != 0u) {
        off = off * 5u + (next != NULL ? php_spec_code(v, next[29]) : 3u);
        rules = cumulative ? rules : 0u;
    }
    if ((rules & PHP_HRULE_RETVAL) != 0u) {
        off = off * 2u + (r->result_type != v->is_unused && r->result_type != 0u);
        rules = cumulative ? rules : 0u;
    }
    if ((rules & PHP_HRULE_QUICK_ARG) != 0u) {
        off = off * 2u + (cumulative ? r->op2 < 12u : r->op2 <= 12u);
        rules = cumulative ? rules : 0u;
    }
    if ((rules & PHP_HRULE_SMART_BRANCH) != 0u) {
        off *= 3u;
        if (v->major == 7) {
            const php_opcode_table *ot = &php_opcode_tables[vi];
            uint32_t id = next != NULL && next[28] < ot->count ? ot->ops[next[28]].id : 0u;
            off += id == XXBYTE_PHP_ID_JMPZ ? 1u : (id == XXBYTE_PHP_ID_JMPNZ ? 2u : 0u);
        } else {
            off += r->result_type == (0x10u | 2u) ? 1u : (r->result_type == (0x20u | 2u) ? 2u : 0u);
        }
        rules = cumulative ? rules : 0u;
    }
    if ((rules & PHP_HRULE_DIM_OBJ) != 0u) {
        const php_opcode_table *ot = &php_opcode_tables[vi];
        uint32_t id = r->extended_value < ot->count ? ot->ops[r->extended_value].id : 0u;
        off = off * 3u + (id == XXBYTE_PHP_ID_ASSIGN_DIM ? 1u : (id == XXBYTE_PHP_ID_ASSIGN_OBJ ? 2u : 0u));
        rules = cumulative ? rules : 0u;
    }
    if ((rules & PHP_HRULE_ISSET) != 0u) {
        off = off * 2u + ((r->extended_value & v->isempty) != 0u ? 1u : 0u);
        rules = cumulative ? rules : 0u;
    }
    if ((rules & PHP_HRULE_OBSERVER) != 0u) {
        off *= 2u;
    }
    return off;
}

/* Whether op's handler field is a VM handler release vi would store for
 * it: the opcode's own handler for its operand types, or one of the
 * type-specialized handlers the optimizer may pick for the opcode,
 * allowing for handlers shared by several specializations (the serializer
 * stores the first number of a handler). next is the following
 * instruction or NULL. */
static int php_handler_ok(uint32_t vi, const uint8_t *op, const uint8_t *next)
{
    const php_handler_table *t = &php_handler_tables[vi];
    const php_special_table *st = &php_special_tables[vi];
    php_raw_op r;
    uint32_t i, base;

    if (t->base == NULL) {
        return 1;
    }
    php_read_raw(op, &r);
    if (r.opcode >= t->count || r.opcode >= php_opcode_tables[vi].count || r.handler >= t->labels) {
        return 0;
    }
    /* OP_DATA is never dispatched: it keeps the null handler. */
    if (php_opcode_tables[vi].ops[r.opcode].id == XXBYTE_PHP_ID_OP_DATA) {
        return 1;
    }
    if (r.handler < t->special) {
        base = t->base[r.opcode];
        return r.handler >= base && r.handler <= (uint64_t)base + php_spec_offset(vi, t->rules[r.opcode], &r, next);
    }
    for (i = 0; i < st->count; ++i) {
        const php_special_handler *e = &st->entries[i];
        if (e->opcode == r.opcode && r.handler >= e->base && r.handler <= (uint64_t)e->base + php_spec_offset(vi, e->rules, &r, next)) {
            return 1;
        }
    }
    return 0;
}

/* Decodes every instruction of every op_array: *good and *total, the
 * op_array count and the instructions whose handler fits the release. */
static int php_score(xxbyte_php_file *f, uint32_t vi, uint64_t *good, uint64_t *total, uint64_t *arrays_found, uint64_t *handlers)
{
    php_img m;
    php_walk w;
    xxbyte_php_op_array *arrays;
    size_t i;
    uint32_t mode = php_versions[vi].major == 7 ? XXBYTE_PHP_MODE(7, php_versions[vi].minor) : XXBYTE_PHP_MODE(8, php_versions[vi].minor);
    uint32_t saved = f->mode;

    *good = 0;
    *total = 0;
    *arrays_found = 0;
    *handlers = 0;
    php_img_init(&m, f, vi);
    memset(&w, 0, sizeof(w));
    w.m = &m;
    if (!php_walk_script(&w, f->script_offset)) {
        return 0;
    }
    arrays = (xxbyte_php_op_array *)xx_mem_calloc(w.count, sizeof(*arrays));
    if (arrays == NULL) {
        return 0;
    }
    w.arrays = arrays;
    w.capacity = w.count;
    w.count = 0;
    f->mode = mode;
    if (php_walk_script(&w, f->script_offset)) {
        *arrays_found = w.count;
        for (i = 0; i < w.count; ++i) {
            xxbyte_php_unit unit;
            uint32_t k;
            unit.file = f;
            unit.op_array = &arrays[i];
            for (k = 0; k < arrays[i].opcode_count; ++k) {
                xxbyte_instruction ins;
                const uint8_t *p = f->data + arrays[i].opcodes_offset + (uint64_t)k * XXBYTE_PHP_OP_SIZE;
                ++*total;
                *handlers += (uint64_t)php_handler_ok(vi, p, k + 1u < arrays[i].opcode_count ? p + XXBYTE_PHP_OP_SIZE : NULL);
                if (xxbyte_decode_ex(XXBYTE_FAMILY_PHP, mode, &unit, p, XXBYTE_PHP_OP_SIZE, (uint64_t)k * XXBYTE_PHP_OP_SIZE, &ins) == XXBYTE_STATUS_OK) {
                    ++*good;
                }
            }
        }
    }
    f->mode = saved;
    xx_mem_free(arrays);
    return 1;
}

static uint32_t php_mode_of(uint32_t vi)
{
    return XXBYTE_PHP_MODE(php_versions[vi].major, php_versions[vi].minor);
}

xxbyte_status xxbyte_php_parse_file(const uint8_t *data, size_t size, uint32_t mode, xxbyte_php_file *file)
{
    xxbyte_php_file f;
    uint32_t vi;
    php_img m;

    if (file == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(file, 0, sizeof(*file));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (mode != XXBYTE_MODE_AUTO && xxbyte_check_mode(XXBYTE_FAMILY_PHP, mode) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if (size < XXBYTE_PHP_HEADER_SIZE) {
        return size >= 8u && memcmp(data, "OPCACHE", 8) != 0 ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_TRUNCATED;
    }
    if (memcmp(data, "OPCACHE", 8) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    memset(&f, 0, sizeof(f));
    f.data = data;
    f.size = size;
    memcpy(f.system_id, data + 8, 32);
    f.system_id[32] = '\0';
    f.mem_size = xxbyte_u64le(data + 40);
    f.str_size = xxbyte_u64le(data + 48);
    f.script_offset = xxbyte_u64le(data + 56);
    f.timestamp = xxbyte_u64le(data + 64);
    f.checksum = xxbyte_u32le(data + 72);
    f.mem_offset = XXBYTE_PHP_HEADER_SIZE;
    if (f.mem_size > size - XXBYTE_PHP_HEADER_SIZE || f.str_size > size - XXBYTE_PHP_HEADER_SIZE - f.mem_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    f.str_offset = XXBYTE_PHP_HEADER_SIZE + f.mem_size;
    f.checksum_ok = php_adler32(php_adler32(1u, data + f.mem_offset, (size_t)f.mem_size), data + f.str_offset, (size_t)f.str_size) == f.checksum;

    if (mode != XXBYTE_MODE_AUTO) {
        vi = php_version_index(mode);
        if (!php_script_ok(&f, vi)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    } else {
        /* Best: every instruction decodes, then the most op_arrays, the
         * most instructions decoded, the most handlers that fit, and the
         * newest release. */
        uint32_t best = PHP_VERSION_COUNT, i;
        uint64_t bk[4] = {0, 0, 0, 0};
        for (i = PHP_VERSION_COUNT; i-- > 0u;) {
            uint64_t good, total, found, handlers, k[4];
            int better = 0, j;
            if (!php_script_ok(&f, i) || !php_score(&f, i, &good, &total, &found, &handlers)) {
                continue;
            }
            k[0] = good == total;
            k[1] = found;
            k[2] = good;
            k[3] = handlers;
            for (j = 0; j < 4 && best != PHP_VERSION_COUNT; ++j) {
                if (k[j] != bk[j]) {
                    better = k[j] > bk[j];
                    break;
                }
            }
            if (best == PHP_VERSION_COUNT || better) {
                best = i;
                memcpy(bk, k, sizeof(bk));
            }
        }
        if (best == PHP_VERSION_COUNT) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        vi = best;
    }
    f.mode = php_mode_of(vi);
    php_img_init(&m, &f, vi);
    (void)php_string(&m, php_u64(&m, f.script_offset + m.v->script_filename), &f.filename, &f.filename_size);
    *file = f;
    return XXBYTE_STATUS_OK;
}

static int php_file_img(const xxbyte_php_file *file, php_img *m)
{
    if (file == NULL || file->data == NULL || xxbyte_check_mode(XXBYTE_FAMILY_PHP, file->mode) != XXBYTE_STATUS_OK) {
        return 0;
    }
    php_img_init(m, file, php_version_index(file->mode));
    return 1;
}

xxbyte_status xxbyte_php_find_op_arrays(const xxbyte_php_file *file, xxbyte_php_op_array *arrays, size_t capacity, size_t *count)
{
    php_img m;
    php_walk w;

    if (count == NULL || (arrays == NULL && capacity != 0u) || !php_file_img(file, &m)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    memset(&w, 0, sizeof(w));
    w.m = &m;
    w.arrays = arrays;
    w.capacity = capacity;
    if (!php_walk_script(&w, file->script_offset)) {
        *count = w.count;
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *count = w.count;
    return XXBYTE_STATUS_OK;
}

/* ---- literals, names, tables ---- */

static int php_literal_at(const php_img *m, const xxbyte_php_op_array *a, uint32_t index, uint64_t *zv)
{
    uint64_t off;
    if (a == NULL || index >= a->literal_count || a->literals_offset < m->mem_offset) {
        return 0;
    }
    off = a->literals_offset - m->mem_offset + (uint64_t)index * 16u;
    if (!php_in(m, off, 16u)) {
        return 0;
    }
    *zv = off;
    return 1;
}

xxbyte_status xxbyte_php_get_literal(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t index, xxbyte_php_literal *literal)
{
    php_img m;
    uint64_t zv, value;
    uint8_t type;

    if (literal == NULL || !php_file_img(file, &m) || op_array == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(literal, 0, sizeof(*literal));
    if (!php_literal_at(&m, op_array, index, &zv)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    type = m.mem[zv + 8];
    value = php_u64(&m, zv);
    literal->type_code = type;
    literal->type = type <= XXBYTE_PHP_LITERAL_ARRAY ? type : XXBYTE_PHP_LITERAL_OTHER;
    switch (literal->type) {
        case XXBYTE_PHP_LITERAL_LONG: literal->integer = (int64_t)value; break;
        case XXBYTE_PHP_LITERAL_DOUBLE: memcpy(&literal->number, &value, sizeof(double)); break;
        case XXBYTE_PHP_LITERAL_STRING:
            if (!php_string(&m, value, &literal->string, &literal->string_size) || literal->string == NULL) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            break;
        case XXBYTE_PHP_LITERAL_ARRAY: {
            php_hash h;
            if (!php_hash_read(&m, value, &h)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            literal->array_count = h.count;
            break;
        }
        default: break;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_php_get_var_name(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t index, const uint8_t **name, size_t *size)
{
    php_img m;
    uint64_t off;

    if (name == NULL || size == NULL || !php_file_img(file, &m) || op_array == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *name = NULL;
    *size = 0;
    if (index >= op_array->last_var || op_array->vars_offset < m.mem_offset) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    off = op_array->vars_offset - m.mem_offset + (uint64_t)index * 8u;
    if (!php_in(&m, off, 8u) || !php_string(&m, php_u64(&m, off), name, size) || *name == NULL) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return XXBYTE_STATUS_OK;
}

/* The jump table of a literal: every element an integer offset. */
static int php_jump_hash(const php_img *m, const xxbyte_php_op_array *a, uint32_t literal, php_hash *h)
{
    uint64_t zv;
    uint32_t i;
    if (!php_literal_at(m, a, literal, &zv) || m->mem[zv + 8] != XXBYTE_PHP_LITERAL_ARRAY || !php_hash_read(m, php_u64(m, zv), h)) {
        return 0;
    }
    for (i = 0; i < h->used; ++i) {
        uint64_t slot = php_hash_slot(m, h, i);
        int is_string;
        int64_t key;
        const uint8_t *text;
        size_t size;
        if (slot == 0u) {
            continue;
        }
        if (m->mem[slot + 8] != XXBYTE_PHP_LITERAL_LONG || !php_hash_key(m, h, i, &is_string, &key, &text, &size)) {
            return 0;
        }
    }
    return 1;
}

static int php_jump_get(const php_img *m, const xxbyte_php_op_array *a, uint32_t literal, uint32_t index, xxbyte_php_jump_entry *entry)
{
    php_hash h;
    uint32_t i, k = 0;
    memset(entry, 0, sizeof(*entry));
    if (!php_jump_hash(m, a, literal, &h) || index >= h.count) {
        return 0;
    }
    for (i = h.used == h.count ? index : 0u; i < h.used; ++i) {
        uint64_t slot = php_hash_slot(m, &h, i);
        if (slot == 0u) {
            continue;
        }
        if (h.used == h.count || k == index) {
            entry->offset = (int64_t)php_u64(m, slot);
            return php_hash_key(m, &h, i, &entry->is_string, &entry->key, &entry->string, &entry->string_size);
        }
        ++k;
    }
    return 0;
}

xxbyte_status xxbyte_php_get_jump_table(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t literal, uint32_t *count)
{
    php_img m;
    php_hash h;
    if (count == NULL || !php_file_img(file, &m) || op_array == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    if (!php_jump_hash(&m, op_array, literal, &h)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *count = h.count;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_php_get_jump_entry(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t literal, uint32_t index, xxbyte_php_jump_entry *entry)
{
    php_img m;
    if (entry == NULL || !php_file_img(file, &m) || op_array == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return php_jump_get(&m, op_array, literal, index, entry) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_INVALID_INSTRUCTION;
}

int php_unit_jump_table(const xxbyte_php_unit *unit, uint32_t literal, uint32_t *count)
{
    return xxbyte_php_get_jump_table(unit->file, unit->op_array, literal, count) == XXBYTE_STATUS_OK;
}

int php_unit_jump_entry(const xxbyte_php_unit *unit, uint32_t literal, uint32_t index, xxbyte_php_jump_entry *entry)
{
    return xxbyte_php_get_jump_entry(unit->file, unit->op_array, literal, index, entry) == XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_php_get_live_range(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t index, xxbyte_php_live_range *range)
{
    php_img m;
    uint64_t off;
    uint32_t var;
    if (range == NULL || !php_file_img(file, &m) || op_array == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(range, 0, sizeof(*range));
    if (index >= op_array->live_range_count || op_array->live_range_offset < m.mem_offset) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    off = op_array->live_range_offset - m.mem_offset + (uint64_t)index * m.v->live_range_size;
    if (!php_in(&m, off, 12u)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    var = php_u32(&m, off);
    range->kind = var & m.v->live_mask;
    var &= ~m.v->live_mask;
    range->var = (var >> 4) >= m.v->frame_slot ? (var >> 4) - m.v->frame_slot : 0u;
    range->start = php_u32(&m, off + 4);
    range->end = php_u32(&m, off + 8);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_php_get_try_catch(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t index, xxbyte_php_try_catch *region)
{
    php_img m;
    uint64_t off;
    if (region == NULL || !php_file_img(file, &m) || op_array == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(region, 0, sizeof(*region));
    if (index >= op_array->try_catch_count || op_array->try_catch_offset < m.mem_offset) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    off = op_array->try_catch_offset - m.mem_offset + (uint64_t)index * 16u;
    if (!php_in(&m, off, 16u)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    region->try_op = php_u32(&m, off);
    region->catch_op = php_u32(&m, off + 4);
    region->finally_op = php_u32(&m, off + 8);
    region->finally_end = php_u32(&m, off + 12);
    return XXBYTE_STATUS_OK;
}

/* ---- text ---- */

int php_fmt_escaped(xx_buf_t *out, const uint8_t *text, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i, start = 0;
    for (i = 0; i < size; ++i) {
        uint8_t c = text[i];
        char esc[4];
        size_t n = 2;
        if (c >= 32u && c <= 126u && c != '\\' && c != '"') {
            continue;
        }
        if (i > start && !xx_buf_append(out, text + start, i - start)) {
            return 0;
        }
        esc[0] = '\\';
        switch (c) {
            case '\n': esc[1] = 'n'; break;
            case '\r': esc[1] = 'r'; break;
            case '\t': esc[1] = 't'; break;
            case '\f': esc[1] = 'f'; break;
            case '\v': esc[1] = 'v'; break;
            case '\\': esc[1] = '\\'; break;
            case '"': esc[1] = '"'; break;
            case 27: esc[1] = 'e'; break;
            default:
                esc[1] = 'x';
                esc[2] = hex[c >> 4];
                esc[3] = hex[c & 15u];
                n = 4;
                break;
        }
        if (!xx_buf_append(out, esc, n)) {
            return 0;
        }
        start = i + 1;
    }
    return size == start || xx_buf_append(out, text + start, size - start);
}

/* Names and paths: bytes outside 0x20-0x7E as \xHH, the rest as is. */
static int php_fmt_name(xx_buf_t *out, const uint8_t *text, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i, start = 0;
    for (i = 0; i < size; ++i) {
        uint8_t c = text[i];
        char esc[4];
        if (c >= 32u && c <= 126u) {
            continue;
        }
        if (i > start && !xx_buf_append(out, text + start, i - start)) {
            return 0;
        }
        esc[0] = '\\';
        esc[1] = 'x';
        esc[2] = hex[c >> 4];
        esc[3] = hex[c & 15u];
        if (!xx_buf_append(out, esc, 4)) {
            return 0;
        }
        start = i + 1;
    }
    return size == start || xx_buf_append(out, text + start, size - start);
}

/* The dumper's text of a literal. */
static int php_literal_text(xx_buf_t *out, const xxbyte_php_file *file, const xxbyte_php_op_array *a, uint32_t index)
{
    xxbyte_php_literal lit;
    xxbyte_fmt fmt;

    if (xxbyte_php_get_literal(file, a, index, &lit) != XXBYTE_STATUS_OK) {
        return xx_buf_appendf(out, "#%u", index);
    }
    switch (lit.type) {
        case XXBYTE_PHP_LITERAL_NULL: return xx_buf_append_str(out, "null");
        case XXBYTE_PHP_LITERAL_FALSE: return xx_buf_append_str(out, "bool(false)");
        case XXBYTE_PHP_LITERAL_TRUE: return xx_buf_append_str(out, "bool(true)");
        case XXBYTE_PHP_LITERAL_LONG: return xx_buf_appendf(out, "int(%lld)", (long long)lit.integer);
        case XXBYTE_PHP_LITERAL_DOUBLE:
            /* glibc's %g, which the dumper uses, prints the sign of a NaN. */
            if (lit.number != lit.number) {
                uint64_t bits;
                memcpy(&bits, &lit.number, sizeof(bits));
                return xx_buf_append_str(out, (bits >> 63) != 0u ? "float(-nan)" : "float(nan)");
            }
            memset(&fmt, 0, sizeof(fmt));
            fmt.out = out;
            return xx_buf_append_str(out, "float(") && xxbyte_fmt_double(&fmt, lit.number) && xx_buf_append_char(out, ')');
        case XXBYTE_PHP_LITERAL_STRING: return xx_buf_append_str(out, "string(\"") && php_fmt_escaped(out, lit.string, lit.string_size) && xx_buf_append_str(out, "\")");
        case XXBYTE_PHP_LITERAL_ARRAY: return xx_buf_append_str(out, "array(...)");
        default: return xx_buf_appendf(out, "zval(type=%u)", lit.type_code);
    }
}

/* Frameless functions of official builds, by release (8.4+): the order in
 * which Zend core, pcre and standard register them. */
/* One entry per (function, argument count) frameless variant, as
 * zend_flf_functions numbers them (from 8.4.27, 8.5.13 and 8.6.0 builds). */
static const char *const php_flf_84[] = {"property_exists", "class_exists", "class_exists", "preg_match", "preg_replace", "min",          "max",
                                         "in_array",        "in_array",     "trim",         "trim",       "implode",      "implode",      "dirname",
                                         "dirname",         "strstr",       "strstr",       "strpos",     "strpos",       "str_contains", "str_starts_with",
                                         "substr",          "substr",       "strtr",        "strtr",      "str_replace",  "dechex",       "is_numeric"};
static const char *const php_flf_86[] = {
    "property_exists", "class_exists",  "class_exists", "preg_match", "preg_replace", "min",    "max",         "clamp",   "in_array",
    "in_array",        "trim",          "trim",         "rtrim",      "rtrim",        "ltrim",  "ltrim",       "implode", "implode",
    "strtoupper",      "strtolower",    "dirname",      "dirname",    "strstr",       "strstr", "strpos",      "strpos",  "str_contains",
    "str_starts_with", "str_ends_with", "substr",       "substr",     "strtr",        "strtr",  "str_replace", "dechex",  "is_numeric"};

static const char *php_frameless_name(uint32_t mode, uint64_t index)
{
    const char *const *t = NULL;
    size_t n = 0;
    if (mode == XXBYTE_PHP_MODE(8, 4) || mode == XXBYTE_PHP_MODE(8, 5)) {
        t = php_flf_84;
        n = sizeof(php_flf_84) / sizeof(php_flf_84[0]);
    } else if (mode == XXBYTE_PHP_MODE(8, 6)) {
        t = php_flf_86;
        n = sizeof(php_flf_86) / sizeof(php_flf_86[0]);
    }
    return t != NULL && index < n ? t[index] : NULL;
}

/* The resolver's text for an operand, unbounded; 0 when it declines. */
static int php_resolve_text(const xxbyte_php_unit *unit, const xxbyte_instruction *ins, uint32_t operand_index, xx_buf_t *out, int *declined)
{
    const xxbyte_operand *o = &ins->operands[operand_index];
    *declined = 1;
    if (unit == NULL || unit->file == NULL || unit->op_array == NULL || o->type != XXBYTE_OPERAND_INDEX) {
        return 1;
    }
    switch (o->kind) {
        case XXBYTE_INDEX_CONSTANT: *declined = 0; return php_literal_text(out, unit->file, unit->op_array, (uint32_t)o->value);
        case XXBYTE_INDEX_LOCAL: {
            const uint8_t *name;
            size_t size;
            *declined = 0;
            if ((uint64_t)o->value >= unit->op_array->last_var ||
                xxbyte_php_get_var_name(unit->file, unit->op_array, (uint32_t)o->value, &name, &size) != XXBYTE_STATUS_OK) {
                return xx_buf_appendf(out, "X%llu", (unsigned long long)o->value);
            }
            return xx_buf_appendf(out, "CV%llu($", (unsigned long long)o->value) && php_fmt_name(out, name, size) && xx_buf_append_char(out, ')');
        }
        case XXBYTE_PHP_INDEX_FRAMELESS: {
            const char *name = php_frameless_name(ins->mode, (uint64_t)o->value);
            if (name == NULL) {
                return 1;
            }
            *declined = 0;
            return xx_buf_append_str(out, name);
        }
        default: return 1;
    }
}

int php_append_resolved(const xxbyte_php_unit *unit, const xxbyte_instruction *ins, uint32_t operand_index, xx_buf_t *out, int *declined)
{
    return php_resolve_text(unit, ins, operand_index, out, declined);
}

size_t xxbyte_php_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    xx_buf_t text;
    int declined = 1;
    size_t n = 0;

    if (context == NULL || instruction == NULL || buffer == NULL || buffer_size == 0u || operand_index >= instruction->operand_count) {
        return 0;
    }
    xx_buf_init(&text);
    if (php_resolve_text((const xxbyte_php_unit *)context, instruction, operand_index, &text, &declined) && !declined && xx_buf_ok(&text) && text.size != 0u) {
        /* snprintf-like: the text that fits, and the full length. */
        size_t copy = text.size < buffer_size - 1u ? text.size : buffer_size - 1u;
        memcpy(buffer, text.data, copy);
        buffer[copy] = '\0';
        n = text.size;
    }
    xx_buf_free(&text);
    return n;
}

/* ---- list_file ---- */

static int php_append_name(xx_buf_t *out, const xxbyte_php_op_array *a)
{
    if (a->name == NULL) {
        return xx_buf_append_str(out, "$_main");
    }
    if (a->scope != NULL && (!php_fmt_name(out, a->scope, a->scope_size) || !xx_buf_append_str(out, "::"))) {
        return 0;
    }
    return php_fmt_name(out, a->name, a->name_size);
}

static const char *php_live_kind(const php_version *v, uint32_t kind)
{
    if (kind == v->live_tmpvar) {
        return "tmp/var";
    }
    if (kind == v->live_loop) {
        return "loop";
    }
    if (kind == v->live_silence) {
        return "silence";
    }
    if (kind == v->live_rope) {
        return "rope";
    }
    if (kind == v->live_new) {
        return "new";
    }
    return "?";
}

static int php_append_op(xx_buf_t *out, const char *label, uint32_t op, int optional)
{
    if (optional && op == 0u) {
        return xx_buf_appendf(out, " %s -", label);
    }
    return xx_buf_appendf(out, " %s 0x%llX", label, (unsigned long long)op * XXBYTE_PHP_OP_SIZE);
}

xxbyte_status php_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_php_file file;
    xxbyte_php_op_array *arrays = NULL;
    size_t count = 0, i;
    xxbyte_status st;
    const php_version *v;

    st = xxbyte_php_parse_file(data, size, mode, &file);
    if (st != XXBYTE_STATUS_OK) {
        return st;
    }
    v = &php_versions[php_version_index(file.mode)];
    st = xxbyte_php_find_op_arrays(&file, NULL, 0, &count);
    if (st != XXBYTE_STATUS_OK && count == 0u) {
        return st;
    }
    arrays = (xxbyte_php_op_array *)xx_mem_calloc(count != 0u ? count : 1u, sizeof(*arrays));
    if (arrays == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    {
        size_t got = 0;
        xxbyte_status st2 = xxbyte_php_find_op_arrays(&file, arrays, count, &got);
        if (got < count) {
            count = got;
        }
        if (st == XXBYTE_STATUS_OK) {
            st = st2;
        }
    }
    for (i = 0; i < count; ++i) {
        const xxbyte_php_op_array *a = &arrays[i];
        xxbyte_php_unit unit;
        uint32_t k;
        xxbyte_status ls;

        if (i != 0u && !xx_buf_append_char(out, '\n')) {
            st = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        if (!php_append_name(out, a) || !xx_buf_appendf(out, ": ; (lines=%u, args=%u, vars=%u, tmps=%u) ; ", a->opcode_count, a->num_args, a->last_var, a->T) ||
            !php_fmt_name(out, a->filename, a->filename_size) || !xx_buf_appendf(out, ":%u-%u\n", a->line_start, a->line_end)) {
            st = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        unit.file = &file;
        unit.op_array = a;
        ls = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_PHP, file.mode, &unit, a->opcode_count != 0u ? data + a->opcodes_offset : data,
                                  (size_t)a->opcode_count * XXBYTE_PHP_OP_SIZE, 0, listing_flags, xxbyte_php_resolve, &unit, out);
        if (ls != XXBYTE_STATUS_OK) {
            st = ls;
            break;
        }
        for (k = 0; k < a->live_range_count; ++k) {
            xxbyte_php_live_range r;
            if (xxbyte_php_get_live_range(&file, a, k, &r) != XXBYTE_STATUS_OK) {
                break;
            }
            if (!xx_buf_appendf(out, "; live range: %u 0x%llX-0x%llX (%s)\n", r.var, (unsigned long long)r.start * XXBYTE_PHP_OP_SIZE,
                                (unsigned long long)r.end * XXBYTE_PHP_OP_SIZE, php_live_kind(v, r.kind))) {
                st = XXBYTE_STATUS_OUT_OF_MEMORY;
                break;
            }
        }
        for (k = 0; k < a->try_catch_count; ++k) {
            xxbyte_php_try_catch r;
            if (xxbyte_php_get_try_catch(&file, a, k, &r) != XXBYTE_STATUS_OK) {
                break;
            }
            if (!xx_buf_append_str(out, ";") || !php_append_op(out, "try", r.try_op, 0) || !php_append_op(out, "catch", r.catch_op, 1) ||
                !php_append_op(out, "finally", r.finally_op, 1) || !php_append_op(out, "end", r.finally_end, 1) || !xx_buf_append_char(out, '\n')) {
                st = XXBYTE_STATUS_OUT_OF_MEMORY;
                break;
            }
        }
    }
    xx_mem_free(arrays);
    if (!xx_buf_ok(out)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return st;
}

#else
typedef int xxbyte_php_file_disabled;
#endif
