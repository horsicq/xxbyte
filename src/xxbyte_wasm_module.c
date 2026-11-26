#include "xxbyte_internal.h"

#if XXBYTE_USE_WASM

#include "xxbyte_wasm_internal.h"
#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#define NONE XXBYTE_WASM_NONE
#define NAME_SPACES 12u
#define SLOT_EMPTY 0xFFFFFFFFu

/* ---- byte reader */

typedef struct rd {
    const uint8_t *p;
    size_t pos;
    size_t end;
} rd;

static xxbyte_status rd_leb(rd *r, unsigned bits, int is_signed, uint64_t *value)
{
    unsigned max_bytes = (bits + 6u) / 7u;
    uint64_t result = 0;
    unsigned i;

    for (i = 0; i < max_bytes; ++i) {
        uint8_t byte;
        if (r->pos + i >= r->end) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        byte = r->p[r->pos + i];
        if (i + 1u == max_bytes) {
            unsigned used = bits - 7u * i;
            if ((byte & 0x80u) != 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (used < 7u) {
                unsigned low = is_signed ? used - 1u : used;
                unsigned rest = (unsigned)byte >> low;
                if (rest != 0 && !(is_signed && rest == (0x7Fu >> low))) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
            }
        }
        result |= (uint64_t)(byte & 0x7Fu) << (7u * i);
        if ((byte & 0x80u) == 0) {
            unsigned shift = 7u * (i + 1u);
            if (is_signed && shift < 64u && (byte & 0x40u) != 0) {
                result |= ~UINT64_C(0) << shift;
            }
            *value = result;
            r->pos += i + 1u;
            return XXBYTE_STATUS_OK;
        }
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

static xxbyte_status rd_u32(rd *r, uint32_t *value)
{
    uint64_t v = 0;
    xxbyte_status status = rd_leb(r, 32, 0, &v);

    *value = (uint32_t)v;
    return status;
}

static xxbyte_status rd_byte(rd *r, uint8_t *value)
{
    if (r->pos >= r->end) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *value = r->p[r->pos++];
    return XXBYTE_STATUS_OK;
}

/* A vector count: every entry takes at least one byte. */
static xxbyte_status rd_count(rd *r, uint32_t *count)
{
    xxbyte_status status = rd_u32(r, count);

    if (status == XXBYTE_STATUS_OK && *count > r->end - r->pos) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    return status;
}

static xxbyte_status rd_string(rd *r, xxbyte_wasm_string *string)
{
    uint32_t size;
    xxbyte_status status = rd_u32(r, &size);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (size > r->end - r->pos) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    string->offset = (uint32_t)r->pos;
    string->size = size;
    r->pos += size;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status rd_value_type(rd *r, xxbyte_operand *type)
{
    return xxbyte_wasm_read_value_type_at(XXBYTE_WASM_MODE_DEFAULT, r->p, r->end, &r->pos, type);
}

static xxbyte_status rd_field(rd *r, xxbyte_operand *type, uint32_t *is_mutable)
{
    uint8_t mut;
    xxbyte_status status;

    if (r->pos < r->end && (r->p[r->pos] == XXBYTE_WASM_TYPE_I8 || r->p[r->pos] == XXBYTE_WASM_TYPE_I16)) {
        memset(type, 0, sizeof(*type));
        type->type = (uint8_t)XXBYTE_OPERAND_INDEX;
        type->kind = (uint8_t)XXBYTE_WASM_INDEX_VALTYPE;
        type->size = 1;
        type->value = r->p[r->pos++];
    } else {
        status = rd_value_type(r, type);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    status = rd_byte(r, &mut);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (mut > 1u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *is_mutable = mut;
    return XXBYTE_STATUS_OK;
}

/* Limits of a table (table64 bit 2) or memory (shared bit 1, memory64 bit
 * 2, custom page size bit 3). */
static xxbyte_status rd_limits(rd *r, int memory)
{
    uint8_t flags;
    uint64_t value;
    unsigned bits;
    xxbyte_status status = rd_byte(r, &flags);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if ((flags & ~(memory ? 0x0Fu : 0x07u)) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    bits = (flags & 0x04u) != 0 ? 64u : 32u;
    status = rd_leb(r, bits, 0, &value);
    if (status == XXBYTE_STATUS_OK && (flags & 0x01u) != 0) {
        status = rd_leb(r, bits, 0, &value);
    }
    if (status == XXBYTE_STATUS_OK && (flags & 0x08u) != 0) {
        status = rd_leb(r, 32, 0, &value);
    }
    return status;
}

/* Skips a constant expression through its final end. */
static xxbyte_status rd_expr(rd *r)
{
    uint32_t depth = 0;

    for (;;) {
        xxbyte_instruction insn;
        xxbyte_status status;
        if (r->pos >= r->end) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        status = xxbyte_decode(XXBYTE_FAMILY_WASM, XXBYTE_WASM_MODE_DEFAULT, r->p + r->pos, r->end - r->pos, 0, &insn);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        r->pos += insn.size;
        switch (insn.id) {
            case XXBYTE_WASM_ID_BLOCK:
            case XXBYTE_WASM_ID_LOOP:
            case XXBYTE_WASM_ID_IF:
            case XXBYTE_WASM_ID_TRY:
            case XXBYTE_WASM_ID_TRY_TABLE: ++depth; break;
            case XXBYTE_WASM_ID_DELEGATE:
            case XXBYTE_WASM_ID_END:
                if (depth == 0) {
                    return XXBYTE_STATUS_OK;
                }
                --depth;
                break;
            default: break;
        }
    }
}

/* ---- growable arrays */

static int grow(void **items, uint32_t *capacity, uint32_t count, size_t item_size)
{
    uint32_t wanted;
    void *p;

    if (count < *capacity) {
        return 1;
    }
    if (*capacity > 0x7FFFFFFFu / 2u) {
        return 0;
    }
    wanted = *capacity == 0 ? 16u : *capacity * 2u;
    if ((size_t)wanted > SIZE_MAX / item_size) {
        return 0;
    }
    p = xx_mem_realloc(*items, (size_t)wanted * item_size);
    if (p == NULL) {
        return 0;
    }
    *items = p;
    *capacity = wanted;
    return 1;
}

/* ---- names */

typedef struct name_entry {
    uint32_t outer;
    uint32_t index;
    uint32_t offset;
    uint32_t size;
    uint32_t suffix; /* 0 = none, else ".N" */
    uint32_t hint;   /* the next suffix to try for names equal to this one */
    uint32_t seq;
} name_entry;

typedef struct name_space {
    name_entry *entries;
    uint32_t count;
    uint32_t capacity;
    uint32_t *slots; /* hash of the bindings: entry indexes */
    uint32_t slot_count;
    uint32_t used;        /* slots that are not empty, stale ones included */
    uint32_t group_start; /* indirect spaces: first entry of the group */
    uint32_t group_outer;
} name_space;

typedef struct wasm_internal {
    name_space spaces[NAME_SPACES];
    xxbyte_wasm_string module_name;
    uint32_t has_module_name;
    uint8_t *function_named; /* bitmap: a name (not a symbol) named it */
    uint32_t seq;
    /* the model's arrays */
    xxbyte_wasm_type *types;
    uint32_t types_capacity;
    xxbyte_wasm_import *imports;
    uint32_t imports_capacity;
    xxbyte_wasm_func *functions;
    uint32_t functions_capacity;
    xxbyte_wasm_entity *tables;
    uint32_t tables_capacity;
    xxbyte_wasm_entity *memories;
    uint32_t memories_capacity;
    xxbyte_wasm_entity *globals;
    uint32_t globals_capacity;
    xxbyte_wasm_entity *tags;
    uint32_t tags_capacity;
    xxbyte_wasm_export *exports;
    uint32_t exports_capacity;
} wasm_internal;

static int is_indirect(uint32_t space)
{
    return space == XXBYTE_WASM_NAME_LOCAL || space == XXBYTE_WASM_NAME_LABEL || space == XXBYTE_WASM_NAME_FIELD;
}

static size_t suffix_text(uint32_t suffix, char text[12])
{
    char digits[10];
    size_t n = 0;
    size_t i;

    if (suffix == 0) {
        return 0;
    }
    while (suffix != 0) {
        digits[n++] = (char)('0' + suffix % 10u);
        suffix /= 10u;
    }
    text[0] = '.';
    for (i = 0; i < n; ++i) {
        text[1 + i] = digits[n - 1 - i];
    }
    return n + 1;
}

static uint32_t name_hash(const uint8_t *data, uint32_t offset, uint32_t size, uint32_t suffix)
{
    char text[12];
    size_t n = suffix_text(suffix, text);
    uint32_t h = 2166136261u;
    uint32_t i;

    for (i = 0; i < size; ++i) {
        h = (h ^ data[offset + i]) * 16777619u;
    }
    for (i = 0; i < n; ++i) {
        h = (h ^ (uint8_t)text[i]) * 16777619u;
    }
    return h;
}

/* Whether the full names raw_a + suffix_a and raw_b + suffix_b are equal. */
static int name_equal(const uint8_t *data, uint32_t a_offset, uint32_t a_size, uint32_t a_suffix, uint32_t b_offset, uint32_t b_size, uint32_t b_suffix)
{
    char a_text[12];
    char b_text[12];
    size_t a_n = suffix_text(a_suffix, a_text);
    size_t b_n = suffix_text(b_suffix, b_text);
    size_t length = a_size + a_n;
    size_t i;

    if (length != b_size + b_n) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        uint8_t a = i < a_size ? data[a_offset + i] : (uint8_t)a_text[i - a_size];
        uint8_t b = i < b_size ? data[b_offset + i] : (uint8_t)b_text[i - b_size];
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

/* The binding of the current group equal to (offset, size, suffix), or
 * SLOT_EMPTY. */
static uint32_t binding_find(const name_space *space, const uint8_t *data, uint32_t offset, uint32_t size, uint32_t suffix)
{
    uint32_t mask;
    uint32_t slot;

    if (space->slot_count == 0) {
        return SLOT_EMPTY;
    }
    mask = space->slot_count - 1u;
    slot = name_hash(data, offset, size, suffix) & mask;
    for (;;) {
        uint32_t e = space->slots[slot];
        if (e == SLOT_EMPTY) {
            return SLOT_EMPTY;
        }
        if (e >= space->group_start) {
            const name_entry *entry = &space->entries[e];
            if (name_equal(data, entry->offset, entry->size, entry->suffix, offset, size, suffix)) {
                return e;
            }
        }
        slot = (slot + 1u) & mask;
    }
}

static void binding_insert(name_space *space, const uint8_t *data, uint32_t e)
{
    const name_entry *entry = &space->entries[e];
    uint32_t mask = space->slot_count - 1u;
    uint32_t slot = name_hash(data, entry->offset, entry->size, entry->suffix) & mask;

    while (space->slots[slot] != SLOT_EMPTY && space->slots[slot] >= space->group_start) {
        slot = (slot + 1u) & mask;
    }
    if (space->slots[slot] == SLOT_EMPTY) {
        space->used++;
    }
    space->slots[slot] = e;
}

/*
 * Keeps at least half of the slots empty, so that every probe ends. Slots
 * of earlier groups (indirect spaces) stay behind as stale entries that
 * insertions may reuse; when they fill the table it is rebuilt from the
 * current group, growing only when that group needs it, so the work stays
 * amortized constant per name.
 */
static int bindings_reserve(name_space *space, const uint8_t *data)
{
    uint32_t live = space->count - space->group_start + 1u;
    uint32_t wanted;
    uint32_t *slots;
    uint32_t i;

    if (space->slot_count != 0 && (space->used + 1u) * 2u <= space->slot_count) {
        return 1;
    }
    wanted = space->slot_count == 0 ? 32u : space->slot_count;
    while ((uint64_t)live * 4u > wanted) {
        if (wanted > 0x20000000u) {
            return 0;
        }
        wanted *= 2u;
    }
    if (wanted == space->slot_count) {
        slots = space->slots;
    } else {
        slots = (uint32_t *)xx_mem_alloc((size_t)wanted * sizeof(uint32_t));
        if (slots == NULL) {
            return 0;
        }
        xx_mem_free(space->slots);
    }
    for (i = 0; i < wanted; ++i) {
        slots[i] = SLOT_EMPTY;
    }
    space->slots = slots;
    space->slot_count = wanted;
    space->used = 0;
    for (i = space->group_start; i < space->count; ++i) {
        binding_insert(space, data, i);
    }
    return 1;
}

/* Applies a name as WABT's GetUniqueName does: the first of name, name.1,
 * name.2, ... that is not bound yet. 0 on allocation failure. */
static int add_name(wasm_internal *in, const uint8_t *data, uint32_t space_id, uint32_t outer, uint32_t index, xxbyte_wasm_string name)
{
    name_space *space = &in->spaces[space_id];
    name_entry *entry;
    uint32_t same;
    uint32_t suffix = 0;

    if (is_indirect(space_id) && (space->count == space->group_start || space->group_outer != outer)) {
        space->group_start = space->count;
        space->group_outer = outer;
    }
    if (!grow((void **)&space->entries, &space->capacity, space->count, sizeof(name_entry)) || !bindings_reserve(space, data)) {
        return 0;
    }
    same = binding_find(space, data, name.offset, name.size, 0);
    if (same != SLOT_EMPTY) {
        suffix = space->entries[same].hint < 1u ? 1u : space->entries[same].hint;
        while (binding_find(space, data, name.offset, name.size, suffix) != SLOT_EMPTY) {
            ++suffix;
        }
        space->entries[same].hint = suffix + 1u;
    }
    entry = &space->entries[space->count];
    entry->outer = outer;
    entry->index = index;
    entry->offset = name.offset;
    entry->size = name.size;
    entry->suffix = suffix;
    entry->hint = 0;
    entry->seq = in->seq++;
    binding_insert(space, data, space->count);
    space->count++;
    return 1;
}

static int entry_before(const name_entry *a, const name_entry *b)
{
    if (a->outer != b->outer) {
        return a->outer < b->outer;
    }
    if (a->index != b->index) {
        return a->index < b->index;
    }
    return a->seq < b->seq;
}

/* Sorts by (outer, index, seq) and keeps the last entry of each index. */
static int finish_space(name_space *space)
{
    name_entry *tmp;
    uint32_t width;
    uint32_t n = space->count;
    uint32_t i;
    uint32_t kept = 0;

    xx_mem_free(space->slots);
    space->slots = NULL;
    space->slot_count = 0;
    space->used = 0;
    if (n < 2) {
        return 1;
    }
    tmp = (name_entry *)xx_mem_alloc((size_t)n * sizeof(name_entry));
    if (tmp == NULL) {
        return 0;
    }
    for (width = 1; width < n; width *= 2u) {
        for (i = 0; i < n; i += 2u * width) {
            uint32_t mid = i + width < n ? i + width : n;
            uint32_t hi = i + 2u * width < n ? i + 2u * width : n;
            uint32_t a = i;
            uint32_t b = mid;
            uint32_t k = i;
            while (a < mid && b < hi) {
                tmp[k++] = entry_before(&space->entries[b], &space->entries[a]) ? space->entries[b++] : space->entries[a++];
            }
            while (a < mid) {
                tmp[k++] = space->entries[a++];
            }
            while (b < hi) {
                tmp[k++] = space->entries[b++];
            }
        }
        memcpy(space->entries, tmp, (size_t)n * sizeof(name_entry));
        if (width > 0x7FFFFFFFu) {
            break;
        }
    }
    xx_mem_free(tmp);
    for (i = 0; i < n; ++i) {
        if (i + 1u < n && space->entries[i + 1u].outer == space->entries[i].outer && space->entries[i + 1u].index == space->entries[i].index) {
            continue;
        }
        space->entries[kept++] = space->entries[i];
    }
    space->count = kept;
    return 1;
}

static const name_entry *find_name(const wasm_internal *in, uint32_t space_id, uint32_t outer, uint32_t index)
{
    const name_space *space = &in->spaces[space_id];
    uint32_t lo = 0;
    uint32_t hi = space->count;

    if (!is_indirect(space_id)) {
        outer = 0;
    }
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        const name_entry *e = &space->entries[mid];
        if (e->outer < outer || (e->outer == outer && e->index < index)) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo < space->count && space->entries[lo].outer == outer && space->entries[lo].index == index) {
        return &space->entries[lo];
    }
    return NULL;
}

/* WAT identifier characters (WABT's s_valid_name_chars). */
static int id_char(uint8_t c)
{
    if (c >= 0x7Fu || c <= 0x20u) {
        return 0;
    }
    switch (c) {
        case '"':
        case '(':
        case ')':
        case ',':
        case ';':
        case '[':
        case ']':
        case '{':
        case '}': return 0;
        default: return 1;
    }
}

static int utf8_valid(const uint8_t *p, size_t n)
{
    size_t i = 0;

    while (i < n) {
        uint8_t c = p[i];
        size_t length;
        size_t k;
        if (c < 0x80u) {
            ++i;
            continue;
        }
        if (c >= 0xC2u && c <= 0xDFu) {
            length = 2;
        } else if (c >= 0xE0u && c <= 0xEFu) {
            length = 3;
        } else if (c >= 0xF0u && c <= 0xF4u) {
            length = 4;
        } else {
            return 0;
        }
        if (length > n - i) {
            return 0;
        }
        for (k = 1; k < length; ++k) {
            if ((p[i + k] & 0xC0u) != 0x80u) {
                return 0;
            }
        }
        if ((c == 0xE0u && p[i + 1] < 0xA0u) || (c == 0xEDu && p[i + 1] >= 0xA0u) || (c == 0xF0u && p[i + 1] < 0x90u) || (c == 0xF4u && p[i + 1] >= 0x90u)) {
            return 0;
        }
        i += length;
    }
    return 1;
}

/* ---- text sinks: a caller buffer or an xx_buf */

typedef struct sink {
    xx_buf_t *out;
    char *text;
    size_t capacity;
    size_t length;
    int failed;
} sink;

static void put_bytes(sink *s, const char *p, size_t n)
{
    if (s->failed) {
        return;
    }
    if (s->out != NULL) {
        if (!xx_buf_append(s->out, p, n)) {
            s->failed = 1;
        }
        return;
    }
    if (n >= s->capacity - s->length) {
        s->failed = 1;
        return;
    }
    memcpy(s->text + s->length, p, n);
    s->length += n;
    s->text[s->length] = '\0';
}

static void put_str(sink *s, const char *p)
{
    put_bytes(s, p, strlen(p));
}

static void put_u32(sink *s, uint32_t value)
{
    char text[12];
    char digits[10];
    size_t n = 0;
    size_t i;

    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    for (i = 0; i < n; ++i) {
        text[i] = digits[n - 1 - i];
    }
    put_bytes(s, text, n);
}

/* "$name.N" with WABT's replacement of invalid characters. */
static void put_name(sink *s, const uint8_t *data, const name_entry *e)
{
    char text[12];
    size_t n = suffix_text(e->suffix, text);
    uint32_t i;

    put_bytes(s, "$", 1);
    for (i = 0; i < e->size && !s->failed; ++i) {
        uint8_t c = data[e->offset + i];
        char ch = id_char(c) ? (char)c : '_';
        put_bytes(s, &ch, 1);
    }
    put_bytes(s, text, n);
}

static const wasm_internal *internal_of(const xxbyte_wasm_module *module)
{
    return module != NULL ? (const wasm_internal *)module->internal : NULL;
}

/* Puts the name of (space, outer, index); 0 when it has none. */
static int put_entity(sink *s, const xxbyte_wasm_module *module, uint32_t space, uint32_t outer, uint32_t index)
{
    const wasm_internal *in = internal_of(module);
    const name_entry *e;

    if (in == NULL || space >= NAME_SPACES) {
        return 0;
    }
    if (space == XXBYTE_WASM_NAME_MODULE) {
        name_entry m;
        if (!in->has_module_name) {
            return 0;
        }
        memset(&m, 0, sizeof(m));
        m.offset = in->module_name.offset;
        m.size = in->module_name.size;
        put_name(s, module->data, &m);
        return 1;
    }
    e = find_name(in, space, outer, index);
    if (e == NULL) {
        return 0;
    }
    put_name(s, module->data, e);
    return 1;
}

static void put_entity_or_number(sink *s, const xxbyte_wasm_module *module, uint32_t space, uint32_t outer, uint32_t index)
{
    if (!put_entity(s, module, space, outer, index)) {
        put_u32(s, index);
    }
}

/* A value type operand in WAT syntax with type names. */
static void put_value_type(sink *s, const xxbyte_wasm_module *module, const xxbyte_operand *type)
{
    if (type->kind == XXBYTE_INDEX_TYPE) {
        put_str(s, type->extra == XXBYTE_WASM_TYPE_REF ? "(ref " : "(ref null ");
        put_entity_or_number(s, module, XXBYTE_WASM_NAME_TYPE, 0, (uint32_t)type->value);
        put_str(s, ")");
        return;
    }
    if (type->kind == XXBYTE_WASM_INDEX_VALTYPE && type->value == XXBYTE_WASM_TYPE_I8) {
        put_str(s, "i8");
        return;
    }
    if (type->kind == XXBYTE_WASM_INDEX_VALTYPE && type->value == XXBYTE_WASM_TYPE_I16) {
        put_str(s, "i16");
        return;
    }
    {
        const char *name = xxbyte_wasm_type_name((uint32_t)type->value, type->kind == XXBYTE_WASM_INDEX_HEAPTYPE ? (uint32_t)type->extra : 0u);
        put_str(s, name != NULL ? name : "?");
    }
}

/* Puts count value types from offset, each preceded by a space. */
static int put_types(sink *s, const xxbyte_wasm_module *module, uint32_t offset, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        xxbyte_operand type;
        if (xxbyte_wasm_module_value_type(module, &offset, &type) != XXBYTE_STATUS_OK) {
            return 0;
        }
        put_str(s, " ");
        put_value_type(s, module, &type);
    }
    return 1;
}

static const xxbyte_wasm_type *func_type(const xxbyte_wasm_module *module, uint32_t type)
{
    if (module == NULL || type >= module->type_count || module->types[type].form != XXBYTE_WASM_FORM_FUNC) {
        return NULL;
    }
    return &module->types[type];
}

/*
 * The params of a function type as WABT's WriteTypeBindings writes them:
 * named params alone ("(param $x i32)"), runs of unnamed ones together
 * ("(param i32 i64)"). function is NONE for a block type (no names).
 */
static int put_params(sink *s, const xxbyte_wasm_module *module, const xxbyte_wasm_type *type, uint32_t function, const char *prefix, uint32_t first, uint32_t offset,
                      uint32_t count)
{
    uint32_t i;
    int open = 0;
    int previous_named = 0;

    for (i = 0; i < count; ++i) {
        xxbyte_operand value;
        int named = 0;
        if (xxbyte_wasm_module_value_type(module, &offset, &value) != XXBYTE_STATUS_OK) {
            if (open) {
                put_str(s, ")");
            }
            return 0;
        }
        if (function != NONE) {
            named = find_name(internal_of(module), XXBYTE_WASM_NAME_LOCAL, function, first + i) != NULL;
        }
        if (open && (named || previous_named)) {
            put_str(s, ")");
            open = 0;
        }
        if (!open) {
            put_str(s, " (");
            put_str(s, prefix);
            open = 1;
        }
        if (named) {
            put_str(s, " ");
            put_entity(s, module, XXBYTE_WASM_NAME_LOCAL, function, first + i);
        }
        put_str(s, " ");
        put_value_type(s, module, &value);
        previous_named = named;
    }
    if (open) {
        put_str(s, ")");
    }
    (void)type;
    return 1;
}

/* " (param ...) (result ...)" of a function type, without names. */
static int put_signature(sink *s, const xxbyte_wasm_module *module, const xxbyte_wasm_type *type, uint32_t function)
{
    if (!put_params(s, module, type, function, "param", 0, type->params_offset, type->param_count)) {
        return 0;
    }
    if (type->result_count != 0) {
        put_str(s, " (result");
        if (!put_types(s, module, type->results_offset, type->result_count)) {
            return 0;
        }
        put_str(s, ")");
    }
    return 1;
}

/* ---- name sections */

/* One name map: vec(index name), applied to space. */
static xxbyte_status read_name_map(rd *r, wasm_internal *in, const xxbyte_wasm_module *module, uint32_t space, uint32_t outer, uint32_t limit, int ordered)
{
    uint32_t count;
    uint32_t i;
    uint32_t last = NONE;
    xxbyte_status status = rd_count(r, &count);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < count; ++i) {
        uint32_t index;
        xxbyte_wasm_string name;
        status = rd_u32(r, &index);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (ordered && last != NONE && index <= last) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (ordered && index >= limit) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        last = index;
        status = rd_string(r, &name);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (!utf8_valid(module->data + name.offset, name.size)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (name.size == 0) {
            continue;
        }
        if (!ordered && index >= limit) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (!add_name(in, module->data, space, outer, index, name)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        if (space == XXBYTE_WASM_NAME_FUNCTION && in->function_named != NULL) {
            in->function_named[index >> 3] = (uint8_t)(in->function_named[index >> 3] | (1u << (index & 7u)));
        }
    }
    return XXBYTE_STATUS_OK;
}

static uint32_t params_and_locals(const xxbyte_wasm_module *module, uint32_t function)
{
    const xxbyte_wasm_func *f = &module->functions[function];
    const xxbyte_wasm_type *type = func_type(module, f->type);
    uint64_t total = (uint64_t)(type != NULL ? type->param_count : 0u) + f->local_count;

    return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

static uint32_t space_limit(const xxbyte_wasm_module *module, uint32_t space)
{
    switch (space) {
        case XXBYTE_WASM_NAME_FUNCTION: return module->function_count;
        case XXBYTE_WASM_NAME_TYPE: return module->type_count;
        case XXBYTE_WASM_NAME_TABLE: return module->table_count;
        case XXBYTE_WASM_NAME_MEMORY: return module->memory_count;
        case XXBYTE_WASM_NAME_GLOBAL: return module->global_count;
        case XXBYTE_WASM_NAME_ELEM: return module->elem_count;
        case XXBYTE_WASM_NAME_DATA:
            if ((module->sections & (1u << XXBYTE_WASM_SECTION_DATA)) != 0) {
                return module->data_segment_count;
            }
            return module->data_count == NONE ? 0u : module->data_count;
        case XXBYTE_WASM_NAME_TAG: return module->tag_count;
        default: return UINT32_MAX;
    }
}

/* The "name" section, following WABT's ReadNameSection. */
static xxbyte_status read_names(rd *r, wasm_internal *in, const xxbyte_wasm_module *module)
{
    uint32_t previous = 0;
    int first = 1;

    while (r->pos < r->end) {
        uint32_t id;
        uint32_t size;
        rd sub;
        xxbyte_status status = rd_u32(r, &id);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (!first && id <= previous) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        first = 0;
        previous = id;
        status = rd_u32(r, &size);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (size > r->end - r->pos) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        sub.p = r->p;
        sub.pos = r->pos;
        sub.end = r->pos + size;
        r->pos = sub.end;
        if (size == 0) {
            continue;
        }
        switch (id) {
            case XXBYTE_WASM_NAME_MODULE: {
                xxbyte_wasm_string name;
                status = rd_string(&sub, &name);
                if (status == XXBYTE_STATUS_OK && !utf8_valid(module->data + name.offset, name.size)) {
                    status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                if (name.size != 0) {
                    in->module_name = name;
                    in->has_module_name = 1;
                }
                break;
            }
            case XXBYTE_WASM_NAME_FUNCTION:
                status = read_name_map(&sub, in, module, id, 0, module->function_count, 1);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                break;
            case XXBYTE_WASM_NAME_LOCAL:
            case XXBYTE_WASM_NAME_LABEL:
            case XXBYTE_WASM_NAME_FIELD: {
                uint32_t count;
                uint32_t i;
                uint32_t last = NONE;
                status = rd_count(&sub, &count);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                for (i = 0; i < count; ++i) {
                    uint32_t outer;
                    uint32_t limit = UINT32_MAX;
                    uint32_t names;
                    rd peek;
                    status = rd_u32(&sub, &outer);
                    if (status != XXBYTE_STATUS_OK) {
                        return status;
                    }
                    if (id == XXBYTE_WASM_NAME_FIELD ? outer >= module->type_count : outer >= module->function_count) {
                        return XXBYTE_STATUS_INVALID_INSTRUCTION;
                    }
                    if (last != NONE && outer <= last) {
                        return XXBYTE_STATUS_INVALID_INSTRUCTION;
                    }
                    last = outer;
                    if (id == XXBYTE_WASM_NAME_LOCAL) {
                        peek = sub;
                        status = rd_count(&peek, &names);
                        if (status != XXBYTE_STATUS_OK) {
                            return status;
                        }
                        if (names > params_and_locals(module, outer)) {
                            return XXBYTE_STATUS_INVALID_INSTRUCTION;
                        }
                    }
                    status = read_name_map(&sub, in, module, id, outer, limit, 1);
                    if (status != XXBYTE_STATUS_OK) {
                        return status;
                    }
                }
                break;
            }
            case XXBYTE_WASM_NAME_TYPE:
            case XXBYTE_WASM_NAME_TABLE:
            case XXBYTE_WASM_NAME_MEMORY:
            case XXBYTE_WASM_NAME_GLOBAL:
            case XXBYTE_WASM_NAME_ELEM:
            case XXBYTE_WASM_NAME_DATA:
            case XXBYTE_WASM_NAME_TAG:
                status = read_name_map(&sub, in, module, id, 0, space_limit(module, id), 0);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                break;
            default: sub.pos = sub.end; break;
        }
        if (sub.pos != sub.end) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    return XXBYTE_STATUS_OK;
}

#define SYM_UNDEFINED 0x10u
#define SYM_EXPLICIT_NAME 0x40u

/* The symbol table of a "linking" section (version 2), following WABT. */
static xxbyte_status read_linking(rd *r, wasm_internal *in, const xxbyte_wasm_module *module)
{
    uint32_t version;
    xxbyte_status status = rd_u32(r, &version);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (version != 2) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    while (r->pos < r->end) {
        uint32_t type;
        uint32_t size = 0;
        uint32_t count;
        uint32_t i;
        rd sub;
        status = rd_u32(r, &type);
        if (status == XXBYTE_STATUS_OK) {
            status = rd_u32(r, &size);
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (size > r->end - r->pos) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        sub.p = r->p;
        sub.pos = r->pos;
        sub.end = r->pos + size;
        r->pos = sub.end;
        if (type != 8u) { /* WASM_SYMBOL_TABLE */
            continue;
        }
        status = rd_u32(&sub, &count);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        for (i = 0; i < count; ++i) {
            uint32_t kind;
            uint32_t flags = 0;
            uint32_t index = 0;
            xxbyte_wasm_string name;
            status = rd_u32(&sub, &kind);
            if (status == XXBYTE_STATUS_OK) {
                status = rd_u32(&sub, &flags);
            }
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            memset(&name, 0, sizeof(name));
            if (kind == 0 || kind == 2 || kind == 4 || kind == 5) { /* function, global, tag, table */
                uint32_t space =
                    kind == 0 ? XXBYTE_WASM_NAME_FUNCTION : (kind == 2 ? XXBYTE_WASM_NAME_GLOBAL : (kind == 4 ? XXBYTE_WASM_NAME_TAG : XXBYTE_WASM_NAME_TABLE));
                status = rd_u32(&sub, &index);
                if (status == XXBYTE_STATUS_OK && ((flags & SYM_UNDEFINED) == 0 || (flags & SYM_EXPLICIT_NAME) != 0)) {
                    status = rd_string(&sub, &name);
                    if (status == XXBYTE_STATUS_OK && !utf8_valid(module->data + name.offset, name.size)) {
                        status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                    }
                }
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                if (name.size == 0) {
                    continue;
                }
                if (index >= space_limit(module, space)) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                if (kind == 0 && in->function_named != NULL && (in->function_named[index >> 3] & (1u << (index & 7u))) != 0) {
                    continue;
                }
                if (!add_name(in, module->data, space, 0, index, name)) {
                    return XXBYTE_STATUS_OUT_OF_MEMORY;
                }
                if (kind == 0 && in->function_named != NULL) {
                    in->function_named[index >> 3] = (uint8_t)(in->function_named[index >> 3] | (1u << (index & 7u)));
                }
            } else if (kind == 1) { /* data */
                uint32_t segment = 0;
                uint32_t offset = 0;
                uint32_t data_size = 0;
                status = rd_string(&sub, &name);
                if (status == XXBYTE_STATUS_OK && !utf8_valid(module->data + name.offset, name.size)) {
                    status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                if (status == XXBYTE_STATUS_OK && (flags & SYM_UNDEFINED) == 0) {
                    status = rd_u32(&sub, &segment);
                    if (status == XXBYTE_STATUS_OK) {
                        status = rd_u32(&sub, &offset);
                    }
                    if (status == XXBYTE_STATUS_OK) {
                        status = rd_u32(&sub, &data_size);
                    }
                }
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                if (name.size == 0 || (flags & SYM_UNDEFINED) != 0 || offset != 0) {
                    continue;
                }
                if (segment >= space_limit(module, XXBYTE_WASM_NAME_DATA)) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                if (!add_name(in, module->data, XXBYTE_WASM_NAME_DATA, 0, segment, name)) {
                    return XXBYTE_STATUS_OUT_OF_MEMORY;
                }
            } else if (kind == 3) { /* section */
                status = rd_u32(&sub, &index);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
            }
            /* WABT reads nothing more for an unknown kind. */
        }
    }
    return XXBYTE_STATUS_OK;
}

/* ---- standard sections */

#define ADD(in, array, module_field, item) \
    (grow((void **)&(in)->array, &(in)->array##_capacity, (module)->module_field, sizeof(*(in)->array)) ? (((in)->array[(module)->module_field++] = (item)), 1) : 0)

static xxbyte_status read_type_section(rd *r, wasm_internal *in, xxbyte_wasm_module *module)
{
    uint32_t count;
    uint32_t i;
    xxbyte_status status = rd_count(r, &count);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < count; ++i) {
        uint32_t group = 1;
        uint32_t first = module->type_count;
        uint32_t k;
        if (r->pos < r->end && r->p[r->pos] == 0x4Eu) {
            ++r->pos;
            status = rd_count(r, &group);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
        }
        for (k = 0; k < group; ++k) {
            xxbyte_wasm_type type;
            uint8_t form;
            memset(&type, 0, sizeof(type));
            type.rec_first = first;
            type.rec_count = group;
            type.is_final = 1;
            type.supertype = NONE;
            type.cont_type = NONE;
            if (r->pos < r->end && (r->p[r->pos] == 0x50u || r->p[r->pos] == 0x4Fu)) {
                uint32_t supers;
                uint32_t j;
                type.is_final = r->p[r->pos] == 0x4Fu;
                ++r->pos;
                status = rd_count(r, &supers);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                type.supertype_count = supers;
                for (j = 0; j < supers; ++j) {
                    uint32_t super;
                    status = rd_u32(r, &super);
                    if (status != XXBYTE_STATUS_OK) {
                        return status;
                    }
                    if (j == 0) {
                        type.supertype = super;
                    }
                }
            }
            type.offset = (uint32_t)r->pos;
            status = rd_byte(r, &form);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            type.form = form;
            if (form == XXBYTE_WASM_FORM_FUNC) {
                uint32_t j;
                xxbyte_operand value;
                status = rd_count(r, &type.param_count);
                type.params_offset = (uint32_t)r->pos;
                for (j = 0; status == XXBYTE_STATUS_OK && j < type.param_count; ++j) {
                    status = rd_value_type(r, &value);
                }
                if (status == XXBYTE_STATUS_OK) {
                    status = rd_count(r, &type.result_count);
                }
                type.results_offset = (uint32_t)r->pos;
                for (j = 0; status == XXBYTE_STATUS_OK && j < type.result_count; ++j) {
                    status = rd_value_type(r, &value);
                }
            } else if (form == XXBYTE_WASM_FORM_STRUCT || form == XXBYTE_WASM_FORM_ARRAY) {
                uint32_t j;
                uint32_t is_mutable;
                xxbyte_operand value;
                if (form == XXBYTE_WASM_FORM_STRUCT) {
                    status = rd_count(r, &type.param_count);
                } else {
                    type.param_count = 1;
                }
                type.params_offset = (uint32_t)r->pos;
                for (j = 0; status == XXBYTE_STATUS_OK && j < type.param_count; ++j) {
                    status = rd_field(r, &value, &is_mutable);
                }
            } else if (form == XXBYTE_WASM_FORM_CONT) {
                uint64_t index;
                status = rd_leb(r, 33, 1, &index);
                if (status == XXBYTE_STATUS_OK && (int64_t)index < 0) {
                    status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                type.cont_type = (uint32_t)index;
            } else {
                status = XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (!ADD(in, types, type_count, type)) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status add_entity(wasm_internal *in, xxbyte_wasm_module *module, uint32_t kind, xxbyte_wasm_entity entity)
{
    int ok;

    switch (kind) {
        case XXBYTE_WASM_EXTERNAL_TABLE: ok = ADD(in, tables, table_count, entity); break;
        case XXBYTE_WASM_EXTERNAL_MEMORY: ok = ADD(in, memories, memory_count, entity); break;
        case XXBYTE_WASM_EXTERNAL_GLOBAL: ok = ADD(in, globals, global_count, entity); break;
        default: ok = ADD(in, tags, tag_count, entity); break;
    }
    return ok ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

/* A table, memory, global or tag descriptor at r (after the kind byte). */
static xxbyte_status read_entity(rd *r, uint32_t kind, xxbyte_wasm_entity *entity)
{
    xxbyte_operand type;
    xxbyte_status status;
    uint8_t byte = 0;

    entity->type_offset = (uint32_t)r->pos;
    entity->type = 0;
    switch (kind) {
        case XXBYTE_WASM_EXTERNAL_TABLE: status = rd_value_type(r, &type); return status != XXBYTE_STATUS_OK ? status : rd_limits(r, 0);
        case XXBYTE_WASM_EXTERNAL_MEMORY: return rd_limits(r, 1);
        case XXBYTE_WASM_EXTERNAL_GLOBAL:
            status = rd_value_type(r, &type);
            if (status == XXBYTE_STATUS_OK) {
                status = rd_byte(r, &byte);
            }
            if (status == XXBYTE_STATUS_OK && byte > 1u) {
                status = XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            entity->type = byte;
            return status;
        default:
            entity->type_offset = 0;
            status = rd_byte(r, &byte);
            if (status == XXBYTE_STATUS_OK && byte != 0) {
                status = XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (status == XXBYTE_STATUS_OK) {
                status = rd_u32(r, &entity->type);
            }
            return status;
    }
}

/* One import whose kind byte and descriptor are at r. */
static xxbyte_status read_import(rd *r, wasm_internal *in, xxbyte_wasm_module *module, xxbyte_wasm_string module_name, xxbyte_wasm_string field_name)
{
    xxbyte_wasm_import import;
    uint8_t kind;
    xxbyte_status status = rd_byte(r, &kind);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (kind > XXBYTE_WASM_EXTERNAL_TAG) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    memset(&import, 0, sizeof(import));
    import.module_name = module_name;
    import.field_name = field_name;
    import.kind = kind;
    import.desc_offset = (uint32_t)r->pos;
    if (kind == XXBYTE_WASM_EXTERNAL_FUNC) {
        xxbyte_wasm_func func;
        status = rd_u32(r, &import.type);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        memset(&func, 0, sizeof(func));
        func.type = import.type;
        func.import = module->import_count;
        import.index = module->function_count;
        if (!ADD(in, functions, function_count, func)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        module->imported_function_count++;
    } else {
        xxbyte_wasm_entity entity;
        status = read_entity(r, kind, &entity);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        entity.import = module->import_count;
        switch (kind) {
            case XXBYTE_WASM_EXTERNAL_TABLE: import.index = module->imported_table_count++; break;
            case XXBYTE_WASM_EXTERNAL_MEMORY: import.index = module->imported_memory_count++; break;
            case XXBYTE_WASM_EXTERNAL_GLOBAL: import.index = module->imported_global_count++; break;
            default:
                import.index = module->imported_tag_count++;
                import.type = entity.type;
                break;
        }
        status = add_entity(in, module, kind, entity);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return ADD(in, imports, import_count, import) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

/* Imports, also in the compact encodings (an empty field name and 0x7F:
 * names with their own kinds; 0x7E: one kind shared by the names). */
static xxbyte_status read_import_section(rd *r, wasm_internal *in, xxbyte_wasm_module *module)
{
    uint32_t count;
    uint32_t i;
    xxbyte_status status = rd_count(r, &count);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < count; ++i) {
        xxbyte_wasm_string module_name;
        xxbyte_wasm_string field_name = {0};
        uint8_t compact;
        uint32_t names = 0;
        uint32_t k;
        status = rd_string(r, &module_name);
        if (status == XXBYTE_STATUS_OK) {
            status = rd_string(r, &field_name);
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        compact = r->pos < r->end ? r->p[r->pos] : 0;
        if (field_name.size != 0 || (compact != 0x7Fu && compact != 0x7Eu)) {
            status = read_import(r, in, module, module_name, field_name);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            continue;
        }
        ++r->pos;
        if (compact == 0x7Fu) {
            status = rd_count(r, &names);
            for (k = 0; status == XXBYTE_STATUS_OK && k < names; ++k) {
                status = rd_string(r, &field_name);
                if (status == XXBYTE_STATUS_OK) {
                    status = read_import(r, in, module, module_name, field_name);
                }
            }
        } else {
            size_t desc = r->pos;
            rd skip = *r;
            uint8_t kind;
            xxbyte_wasm_entity entity;
            uint32_t type;
            /* measure the shared descriptor */
            status = rd_byte(&skip, &kind);
            if (status == XXBYTE_STATUS_OK && kind > XXBYTE_WASM_EXTERNAL_TAG) {
                status = XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (status == XXBYTE_STATUS_OK) {
                status = kind == XXBYTE_WASM_EXTERNAL_FUNC ? rd_u32(&skip, &type) : read_entity(&skip, kind, &entity);
            }
            if (status == XXBYTE_STATUS_OK) {
                r->pos = skip.pos;
                status = rd_count(r, &names);
            }
            for (k = 0; status == XXBYTE_STATUS_OK && k < names; ++k) {
                status = rd_string(r, &field_name);
                if (status == XXBYTE_STATUS_OK) {
                    rd shared = *r;
                    shared.pos = desc;
                    status = read_import(&shared, in, module, module_name, field_name);
                }
            }
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status read_function_section(rd *r, wasm_internal *in, xxbyte_wasm_module *module)
{
    uint32_t count;
    uint32_t i;
    xxbyte_status status = rd_count(r, &count);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < count; ++i) {
        xxbyte_wasm_func func;
        memset(&func, 0, sizeof(func));
        func.import = NONE;
        status = rd_u32(r, &func.type);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (!ADD(in, functions, function_count, func)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* Table, memory, global and tag sections. */
static xxbyte_status read_entity_section(rd *r, wasm_internal *in, xxbyte_wasm_module *module, uint32_t kind)
{
    uint32_t count;
    uint32_t i;
    xxbyte_status status = rd_count(r, &count);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < count; ++i) {
        xxbyte_wasm_entity entity;
        int with_init = 0;
        if (kind == XXBYTE_WASM_EXTERNAL_TABLE && r->end - r->pos >= 2 && r->p[r->pos] == 0x40u) {
            if (r->p[r->pos + 1] != 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            r->pos += 2;
            with_init = 1;
        }
        status = read_entity(r, kind, &entity);
        if (status == XXBYTE_STATUS_OK && (kind == XXBYTE_WASM_EXTERNAL_GLOBAL || with_init)) {
            status = rd_expr(r);
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        entity.import = NONE;
        status = add_entity(in, module, kind, entity);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status read_export_section(rd *r, wasm_internal *in, xxbyte_wasm_module *module)
{
    uint32_t count;
    uint32_t i;
    xxbyte_status status = rd_count(r, &count);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < count; ++i) {
        xxbyte_wasm_export entry;
        uint8_t kind = 0;
        memset(&entry, 0, sizeof(entry));
        status = rd_string(r, &entry.name);
        if (status == XXBYTE_STATUS_OK) {
            status = rd_byte(r, &kind);
        }
        if (status == XXBYTE_STATUS_OK && kind > XXBYTE_WASM_EXTERNAL_TAG) {
            status = XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (status == XXBYTE_STATUS_OK) {
            entry.kind = kind;
            status = rd_u32(r, &entry.index);
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (!ADD(in, exports, export_count, entry)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* The code section: body offsets and local counts of the defined
 * functions. A cut section (clipped) reads the bodies that fit. */
static xxbyte_status read_code_section(rd *r, wasm_internal *in, xxbyte_wasm_module *module)
{
    uint32_t count;
    uint32_t i;
    xxbyte_status status = rd_count(r, &count);

    (void)in;
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    for (i = 0; i < count; ++i) {
        uint32_t size;
        uint64_t index = (uint64_t)module->imported_function_count + i;
        status = rd_u32(r, &size);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (size > r->end - r->pos) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (index < module->function_count) {
            xxbyte_wasm_func *f = &in->functions[index];
            xxbyte_wasm_function_body body;
            f->body_offset = (uint32_t)r->pos;
            f->body_size = size;
            if (xxbyte_wasm_parse_function_body(r->p + r->pos, size, &body) == XXBYTE_STATUS_OK) {
                f->local_count = body.local_count;
            }
        }
        r->pos += size;
        module->body_count++;
    }
    return r->pos == r->end ? XXBYTE_STATUS_OK : XXBYTE_STATUS_INVALID_INSTRUCTION;
}

static void sync_arrays(xxbyte_wasm_module *module, const wasm_internal *in)
{
    module->types = in->types;
    module->imports = in->imports;
    module->functions = in->functions;
    module->tables = in->tables;
    module->memories = in->memories;
    module->globals = in->globals;
    module->tags = in->tags;
    module->exports = in->exports;
}

void xxbyte_wasm_module_free(xxbyte_wasm_module *module)
{
    wasm_internal *in;
    uint32_t i;

    if (module == NULL) {
        return;
    }
    in = (wasm_internal *)module->internal;
    if (in != NULL) {
        for (i = 0; i < NAME_SPACES; ++i) {
            xx_mem_free(in->spaces[i].entries);
            xx_mem_free(in->spaces[i].slots);
        }
        xx_mem_free(in->function_named);
        xx_mem_free(in->types);
        xx_mem_free(in->imports);
        xx_mem_free(in->functions);
        xx_mem_free(in->tables);
        xx_mem_free(in->memories);
        xx_mem_free(in->globals);
        xx_mem_free(in->tags);
        xx_mem_free(in->exports);
        xx_mem_free(in);
    }
    memset(module, 0, sizeof(*module));
}

static int custom_named(const uint8_t *data, const xxbyte_wasm_section *section, const char *name)
{
    size_t n = strlen(name);

    return section->name_size == n && memcmp(data + section->name_offset, name, n) == 0;
}

/* Reads the id and size of the section at offset, clipped to size, for a
 * section that runs past the end of the file. */
static int clip_section(const uint8_t *data, size_t size, size_t offset, xxbyte_wasm_section *section)
{
    rd r;
    uint32_t content;

    memset(section, 0, sizeof(*section));
    if (offset >= size) {
        return 0;
    }
    r.p = data;
    r.pos = offset + 1u;
    r.end = size;
    if (rd_u32(&r, &content) != XXBYTE_STATUS_OK) {
        return 0;
    }
    section->id = data[offset];
    section->offset = (uint32_t)offset;
    section->content_offset = (uint32_t)r.pos;
    section->content_size = (uint32_t)(size - r.pos);
    return 1;
}

xxbyte_status xxbyte_wasm_module_parse(const uint8_t *data, size_t size, xxbyte_wasm_module *module)
{
    wasm_internal *in;
    size_t offset = 8;
    xxbyte_status result = XXBYTE_STATUS_OK;
    xxbyte_status status;
    uint32_t pass;
    uint32_t i;

    if (module != NULL) {
        memset(module, 0, sizeof(*module));
    }
    if (data == NULL || module == NULL || size > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = xxbyte_wasm_check_header(data, size);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    in = (wasm_internal *)xx_mem_calloc(1, sizeof(wasm_internal));
    if (in == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    module->data = data;
    module->size = (uint32_t)size;
    module->internal = in;
    module->start = NONE;
    module->data_count = NONE;
    /* Pass 0 reads the standard sections, pass 1 the names. */
    for (pass = 0; pass < 2; ++pass) {
        offset = 8;
        for (;;) {
            xxbyte_wasm_section section;
            rd r;
            uint32_t bit;
            int clipped = 0;
            status = xxbyte_wasm_next_section(data, size, &offset, &section);
            if (status == XXBYTE_STATUS_END_OF_INPUT) {
                break;
            }
            if (status != XXBYTE_STATUS_OK) {
                if (status == XXBYTE_STATUS_TRUNCATED && clip_section(data, size, offset, &section) && section.id == XXBYTE_WASM_SECTION_CODE) {
                    clipped = 1;
                } else {
                    if (pass == 0 && result == XXBYTE_STATUS_OK) {
                        result = status;
                    }
                    break;
                }
            }
            r.p = data;
            r.pos = section.content_offset;
            r.end = (size_t)section.content_offset + section.content_size;
            bit = 1u << section.id;
            status = XXBYTE_STATUS_OK;
            if (section.id == XXBYTE_WASM_SECTION_CUSTOM) {
                if (pass == 1) {
                    r.pos = (size_t)section.name_offset + section.name_size;
                    if (custom_named(data, &section, "name")) {
                        status = read_names(&r, in, module);
                    } else if (custom_named(data, &section, "linking")) {
                        status = read_linking(&r, in, module);
                    }
                    if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
                        result = status;
                    } else if (status != XXBYTE_STATUS_OK) {
                        module->bad_sections |= 1u;
                    }
                }
                module->sections |= 1u;
            } else if (pass == 0) {
                if ((module->sections & bit) != 0) {
                    module->bad_sections |= bit; /* a second copy */
                    status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                } else {
                    module->sections |= bit;
                    switch (section.id) {
                        case XXBYTE_WASM_SECTION_TYPE: status = read_type_section(&r, in, module); break;
                        case XXBYTE_WASM_SECTION_IMPORT: status = read_import_section(&r, in, module); break;
                        case XXBYTE_WASM_SECTION_FUNCTION: status = read_function_section(&r, in, module); break;
                        case XXBYTE_WASM_SECTION_TABLE: status = read_entity_section(&r, in, module, XXBYTE_WASM_EXTERNAL_TABLE); break;
                        case XXBYTE_WASM_SECTION_MEMORY: status = read_entity_section(&r, in, module, XXBYTE_WASM_EXTERNAL_MEMORY); break;
                        case XXBYTE_WASM_SECTION_GLOBAL: status = read_entity_section(&r, in, module, XXBYTE_WASM_EXTERNAL_GLOBAL); break;
                        case XXBYTE_WASM_SECTION_TAG: status = read_entity_section(&r, in, module, XXBYTE_WASM_EXTERNAL_TAG); break;
                        case XXBYTE_WASM_SECTION_EXPORT: status = read_export_section(&r, in, module); break;
                        case XXBYTE_WASM_SECTION_START: status = rd_u32(&r, &module->start); break;
                        case XXBYTE_WASM_SECTION_ELEMENT: status = rd_count(&r, &module->elem_count); break;
                        case XXBYTE_WASM_SECTION_DATA: status = rd_count(&r, &module->data_segment_count); break;
                        case XXBYTE_WASM_SECTION_DATA_COUNT:
                            status = rd_u32(&r, &module->data_count);
                            if (status != XXBYTE_STATUS_OK) {
                                module->data_count = NONE;
                            }
                            break;
                        case XXBYTE_WASM_SECTION_CODE:
                            module->code = section;
                            status = read_code_section(&r, in, module);
                            break;
                        default: break;
                    }
                    sync_arrays(module, in);
                    if (status == XXBYTE_STATUS_OK && section.id != XXBYTE_WASM_SECTION_CODE && section.id != XXBYTE_WASM_SECTION_ELEMENT &&
                        section.id != XXBYTE_WASM_SECTION_DATA && r.pos != r.end) {
                        status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                    }
                    if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
                        result = status;
                        break;
                    }
                    if (status != XXBYTE_STATUS_OK) {
                        module->bad_sections |= bit;
                    }
                }
                if (status != XXBYTE_STATUS_OK && result == XXBYTE_STATUS_OK) {
                    result = XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
            }
            if (clipped) {
                if (pass == 0) {
                    result = XXBYTE_STATUS_TRUNCATED;
                }
                break;
            }
        }
        if (result == XXBYTE_STATUS_OUT_OF_MEMORY) {
            break;
        }
        if (pass == 0 && module->function_count != 0) {
            in->function_named = (uint8_t *)xx_mem_calloc(((size_t)module->function_count + 7u) / 8u, 1);
            if (in->function_named == NULL) {
                result = XXBYTE_STATUS_OUT_OF_MEMORY;
                break;
            }
        }
    }
    for (i = 0; i < NAME_SPACES; ++i) {
        if (!finish_space(&in->spaces[i])) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    sync_arrays(module, in);
    return result;
}

xxbyte_status xxbyte_wasm_module_value_type(const xxbyte_wasm_module *module, uint32_t *offset, xxbyte_operand *type)
{
    rd r;
    xxbyte_status status;

    if (type != NULL) {
        memset(type, 0, sizeof(*type));
    }
    if (module == NULL || module->data == NULL || offset == NULL || type == NULL || *offset > module->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    r.p = module->data;
    r.pos = *offset;
    r.end = module->size;
    status = rd_value_type(&r, type);
    if (status == XXBYTE_STATUS_OK) {
        *offset = (uint32_t)r.pos;
    }
    return status;
}

xxbyte_status xxbyte_wasm_module_field(const xxbyte_wasm_module *module, uint32_t *offset, xxbyte_operand *type, uint32_t *is_mutable)
{
    rd r;
    xxbyte_status status;

    if (type != NULL) {
        memset(type, 0, sizeof(*type));
    }
    if (is_mutable != NULL) {
        *is_mutable = 0;
    }
    if (module == NULL || module->data == NULL || offset == NULL || type == NULL || is_mutable == NULL || *offset > module->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    r.p = module->data;
    r.pos = *offset;
    r.end = module->size;
    status = rd_field(&r, type, is_mutable);
    if (status == XXBYTE_STATUS_OK) {
        *offset = (uint32_t)r.pos;
    }
    return status;
}

size_t xxbyte_wasm_module_name(const xxbyte_wasm_module *module, uint32_t space, uint32_t outer, uint32_t index, char *buffer, size_t buffer_size)
{
    sink s;

    if (buffer == NULL || buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';
    memset(&s, 0, sizeof(s));
    s.text = buffer;
    s.capacity = buffer_size;
    if (!put_entity(&s, module, space, outer, index) || s.failed) {
        buffer[0] = '\0';
        return 0;
    }
    return s.length;
}

int xxbyte_wasm_module_raw_name(const xxbyte_wasm_module *module, uint32_t space, uint32_t outer, uint32_t index, xxbyte_wasm_string *name)
{
    const wasm_internal *in = internal_of(module);
    const name_entry *e;

    if (name == NULL) {
        return 0;
    }
    memset(name, 0, sizeof(*name));
    if (in == NULL || space >= NAME_SPACES) {
        return 0;
    }
    if (space == XXBYTE_WASM_NAME_MODULE) {
        if (!in->has_module_name) {
            return 0;
        }
        *name = in->module_name;
        return 1;
    }
    e = find_name(in, space, outer, index);
    if (e == NULL) {
        return 0;
    }
    name->offset = e->offset;
    name->size = e->size;
    return 1;
}

/* ---- resolver */

void xxbyte_wasm_names_init(xxbyte_wasm_names *names, const xxbyte_wasm_module *module)
{
    if (names == NULL) {
        return;
    }
    memset(names, 0, sizeof(*names));
    names->module = module;
    names->function = NONE;
}

void xxbyte_wasm_names_begin(xxbyte_wasm_names *names, uint32_t function)
{
    if (names == NULL) {
        return;
    }
    names->function = function;
    names->tracking = 1;
    names->depth = 0;
    names->labels = 0;
}

void xxbyte_wasm_names_free(xxbyte_wasm_names *names)
{
    if (names == NULL) {
        return;
    }
    xx_mem_free(names->stack);
    names->stack = NULL;
    names->stack_capacity = 0;
}

static int opens_block(uint32_t id)
{
    return id == XXBYTE_WASM_ID_BLOCK || id == XXBYTE_WASM_ID_LOOP || id == XXBYTE_WASM_ID_IF || id == XXBYTE_WASM_ID_TRY || id == XXBYTE_WASM_ID_TRY_TABLE;
}

xxbyte_status xxbyte_wasm_names_step(xxbyte_wasm_names *names, const xxbyte_instruction *instruction)
{
    if (names == NULL || instruction == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->family != (uint32_t)XXBYTE_FAMILY_WASM || instruction->size == 0) {
        return XXBYTE_STATUS_OK;
    }
    if (opens_block(instruction->id)) {
        uint32_t label = names->labels++;
        if (names->depth == UINT32_MAX) {
            return XXBYTE_STATUS_OK;
        }
        if (names->stack != NULL || names->depth == 0) {
            if (!grow((void **)&names->stack, &names->stack_capacity, names->depth, sizeof(uint32_t))) {
                xx_mem_free(names->stack);
                names->stack = NULL;
                names->stack_capacity = 0;
                names->depth++;
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            names->stack[names->depth] = label;
        }
        names->depth++;
    } else if ((instruction->id == XXBYTE_WASM_ID_END || instruction->id == XXBYTE_WASM_ID_DELEGATE) && names->depth != 0) {
        names->depth--;
    }
    return XXBYTE_STATUS_OK;
}

static size_t finish(const sink *s, const xx_buf_t *text)
{
    if (s->failed || text->size == 0) {
        return 0;
    }
    return text->size;
}

static size_t resolve_label(const xxbyte_wasm_names *names, const xxbyte_instruction *instruction, uint32_t depth, sink *s)
{
    uint32_t level;

    if (!names->tracking || instruction->id == XXBYTE_WASM_ID_DELEGATE) {
        return 0;
    }
    if (depth > names->depth) {
        put_u32(s, depth);
        put_str(s, " (; INVALID ;)");
        return 1;
    }
    level = names->depth - depth;
    if (level != 0 && names->stack != NULL && names->function != NONE) {
        if (put_entity(s, names->module, XXBYTE_WASM_NAME_LABEL, names->function, names->stack[level - 1u])) {
            return 1;
        }
    }
    put_u32(s, depth);
    put_str(s, " (;@");
    put_u32(s, level);
    put_str(s, ";)");
    return 1;
}

/* Renders the name of operand operand_index into *text; its length, or 0. */
static size_t resolve_text(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, xx_buf_t *text)
{
    const xxbyte_wasm_names *names = (const xxbyte_wasm_names *)context;
    const xxbyte_wasm_module *module;
    const xxbyte_operand *operand;
    uint32_t value;
    uint32_t space;
    uint32_t outer = 0;
    sink s;

    if (names == NULL || instruction == NULL || operand_index >= instruction->operand_count || instruction->family != (uint32_t)XXBYTE_FAMILY_WASM) {
        return 0;
    }
    module = names->module;
    operand = &instruction->operands[operand_index];
    if (operand->type != XXBYTE_OPERAND_INDEX || operand->value < 0 || operand->value > (int64_t)UINT32_MAX) {
        return 0;
    }
    value = (uint32_t)operand->value;
    memset(&s, 0, sizeof(s));
    s.out = text;
    switch (operand->kind) {
        case XXBYTE_INDEX_LABEL:
            if (!resolve_label(names, instruction, value, &s)) {
                return 0;
            }
            return finish(&s, text);
        case XXBYTE_INDEX_FUNCTION: space = XXBYTE_WASM_NAME_FUNCTION; break;
        case XXBYTE_INDEX_LOCAL:
            space = XXBYTE_WASM_NAME_LOCAL;
            outer = names->function;
            if (outer == NONE) {
                return 0;
            }
            break;
        case XXBYTE_INDEX_GLOBAL: space = XXBYTE_WASM_NAME_GLOBAL; break;
        case XXBYTE_INDEX_TABLE: space = XXBYTE_WASM_NAME_TABLE; break;
        case XXBYTE_WASM_INDEX_MEMORY: space = XXBYTE_WASM_NAME_MEMORY; break;
        case XXBYTE_WASM_INDEX_TAG: space = XXBYTE_WASM_NAME_TAG; break;
        case XXBYTE_WASM_INDEX_DATA: space = XXBYTE_WASM_NAME_DATA; break;
        case XXBYTE_WASM_INDEX_ELEM: space = XXBYTE_WASM_NAME_ELEM; break;
        case XXBYTE_INDEX_FIELD: {
            const xxbyte_operand *type = operand_index > 0 ? &instruction->operands[operand_index - 1u] : NULL;
            if (type == NULL || type->type != XXBYTE_OPERAND_INDEX || type->kind != XXBYTE_INDEX_TYPE || type->value < 0 || type->value > (int64_t)UINT32_MAX) {
                return 0;
            }
            space = XXBYTE_WASM_NAME_FIELD;
            outer = (uint32_t)type->value;
            break;
        }
        case XXBYTE_INDEX_TYPE:
            if (operand_index == 0 && operand->extra == 0 && opens_block(instruction->id)) {
                const xxbyte_wasm_type *type = func_type(module, value);
                if (type == NULL) {
                    return put_entity(&s, module, XXBYTE_WASM_NAME_TYPE, 0, value) ? finish(&s, text) : 0;
                }
                put_entity_or_number(&s, module, XXBYTE_WASM_NAME_TYPE, 0, value);
                if (type->param_count != 0 || type->result_count != 0) {
                    /* "N) (param ...) (result ..." : the formatter wraps it
                     * in " (type " and ")" */
                    put_str(&s, ")");
                    if (!put_signature(&s, module, type, NONE) || s.failed || text->size == 0 || text->data[text->size - 1u] != ')') {
                        return 0;
                    }
                    text->data[--text->size] = '\0';
                }
                return finish(&s, text);
            }
            space = XXBYTE_WASM_NAME_TYPE;
            break;
        default: return 0;
    }
    if (!put_entity(&s, module, space, outer, value)) {
        return 0;
    }
    return finish(&s, text);
}

size_t xxbyte_wasm_resolve_name(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    xx_buf_t text;
    size_t length;

    if (buffer == NULL || buffer_size == 0) {
        return 0;
    }
    buffer[0] = '\0';
    xx_buf_init(&text);
    length = resolve_text(context, instruction, operand_index, &text);
    if (length != 0 && xx_buf_ok(&text) && text.data != NULL) {
        /* snprintf style: the truncated text and the full length */
        size_t copied = length < buffer_size ? length : buffer_size - 1u;
        memcpy(buffer, text.data, copied);
        buffer[copied] = '\0';
    } else {
        length = 0;
    }
    xx_buf_free(&text);
    return length;
}

/* ---- list_file */

/* Above this many locals a body's locals line is a summary comment. */
#define LOCALS_EXPAND_LIMIT 4096u

static void put_func_header(sink *s, const xxbyte_wasm_module *module, uint32_t function)
{
    const xxbyte_wasm_func *f = function < module->function_count ? &module->functions[function] : NULL;
    const xxbyte_wasm_type *type = NULL;

    put_str(s, "(func ");
    if (!put_entity(s, module, XXBYTE_WASM_NAME_FUNCTION, 0, function)) {
        put_str(s, "(;");
        put_u32(s, function);
        put_str(s, ";)");
    }
    if (f != NULL && f->type != NONE) {
        put_str(s, " (type ");
        put_entity_or_number(s, module, XXBYTE_WASM_NAME_TYPE, 0, f->type);
        put_str(s, ")");
        type = func_type(module, f->type);
    }
    if (type != NULL) {
        if (put_params(s, module, type, function, "param", 0, type->params_offset, type->param_count) && type->result_count != 0) {
            put_str(s, " (result");
            put_types(s, module, type->results_offset, type->result_count);
            put_str(s, ")");
        }
    }
    put_str(s, ")\n");
}

/* The locals line, as WABT writes it after the header. */
static void put_locals(sink *s, const xxbyte_wasm_module *module, uint32_t function, uint32_t first, const uint8_t *body, size_t body_size,
                       const xxbyte_wasm_function_body *parsed)
{
    uint32_t offset = parsed->locals_offset;
    uint32_t count;
    uint32_t index = first;
    xxbyte_operand type;
    int open = 0;
    int previous_named = 0;
    int any = 0;

    if (parsed->local_count == 0) {
        return;
    }
    if (parsed->local_count > LOCALS_EXPAND_LIMIT) {
        put_str(s, ";; locals:");
        while (xxbyte_wasm_local_group_next(body, body_size, parsed, &offset, &count, &type) == XXBYTE_STATUS_OK) {
            put_str(s, any ? ", " : " ");
            put_value_type(s, module, &type);
            put_str(s, " x ");
            put_u32(s, count);
            any = 1;
        }
        put_str(s, "\n");
        return;
    }
    while (xxbyte_wasm_local_group_next(body, body_size, parsed, &offset, &count, &type) == XXBYTE_STATUS_OK) {
        uint32_t k;
        for (k = 0; k < count; ++k, ++index) {
            int named = find_name(internal_of(module), XXBYTE_WASM_NAME_LOCAL, function, index) != NULL;
            if (open && (named || previous_named)) {
                put_str(s, ")");
                open = 0;
            }
            if (!open) {
                put_str(s, any ? " (local" : "(local");
                open = 1;
                any = 1;
            }
            if (named) {
                put_str(s, " ");
                put_entity(s, module, XXBYTE_WASM_NAME_LOCAL, function, index);
            }
            put_str(s, " ");
            put_value_type(s, module, &type);
            previous_named = named;
        }
    }
    if (open) {
        put_str(s, ")");
    }
    put_str(s, "\n");
}

/* Drops the newline that ends the listing line in out. */
static void unterminate(xx_buf_t *out)
{
    if (out->data != NULL && out->size != 0 && out->data[out->size - 1u] == '\n') {
        out->data[--out->size] = '\0';
    }
}

static xxbyte_status list_body(uint32_t mode, const xxbyte_wasm_module *module, xxbyte_wasm_names *names, uint32_t function, uint32_t code_offset, uint32_t code_size,
                               uint32_t listing_flags, xx_buf_t *out)
{
    const uint8_t *code = module->data + code_offset;
    uint32_t offset = 0;

    xxbyte_wasm_names_begin(names, function);
    while (offset < code_size) {
        xxbyte_instruction insn;
        uint64_t address = (uint64_t)code_offset + offset;
        xxbyte_status status = xxbyte_decode(XXBYTE_FAMILY_WASM, mode, code + offset, code_size - offset, address, &insn);
        uint32_t length = status == XXBYTE_STATUS_OK ? insn.size : 1u;

        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_WASM, mode, NULL, code + offset, length, address, listing_flags, xxbyte_wasm_resolve_name, names, out);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (length == insn.size && insn.size != 0) {
            if (opens_block(insn.id)) {
                sink s;
                memset(&s, 0, sizeof(s));
                s.out = out;
                unterminate(out);
                put_str(&s, "  ;; label = ");
                if (!put_entity(&s, module, XXBYTE_WASM_NAME_LABEL, function, names->labels)) {
                    put_str(&s, "@");
                    put_u32(&s, names->depth + 1u);
                }
                put_str(&s, "\n");
                if (s.failed) {
                    return XXBYTE_STATUS_OUT_OF_MEMORY;
                }
            }
            (void)xxbyte_wasm_names_step(names, &insn);
        }
        offset += length;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_wasm_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_wasm_module module;
    xxbyte_wasm_names names;
    xxbyte_status result;
    xxbyte_status status = XXBYTE_STATUS_OK;
    sink s;
    rd r;
    uint32_t count = 0;
    uint32_t i;

    if (mode == XXBYTE_MODE_AUTO) {
        mode = XXBYTE_WASM_MODE_DEFAULT;
    }
    if (data == NULL) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    result = xxbyte_wasm_module_parse(data, size, &module);
    if (module.internal == NULL) {
        return result;
    }
    if (result == XXBYTE_STATUS_OUT_OF_MEMORY) {
        xxbyte_wasm_module_free(&module);
        return result;
    }
    memset(&s, 0, sizeof(s));
    s.out = out;
    put_str(&s, "(module");
    if (internal_of(&module)->has_module_name) {
        put_str(&s, " ");
        put_entity(&s, &module, XXBYTE_WASM_NAME_MODULE, 0, 0);
    }
    put_str(&s, ")\n");
    xxbyte_wasm_names_init(&names, &module);
    if (module.code.id == XXBYTE_WASM_SECTION_CODE) {
        r.p = data;
        r.pos = module.code.content_offset;
        r.end = (size_t)module.code.content_offset + module.code.content_size;
        status = rd_u32(&r, &count);
        for (i = 0; status == XXBYTE_STATUS_OK && i < count && !s.failed; ++i) {
            uint32_t body_size;
            uint32_t function = module.imported_function_count + i;
            xxbyte_wasm_function_body body;
            const uint8_t *body_data;
            xxbyte_status parsed;

            status = rd_u32(&r, &body_size);
            if (status != XXBYTE_STATUS_OK) {
                break;
            }
            if (body_size > r.end - r.pos) {
                body_size = (uint32_t)(r.end - r.pos);
                status = r.end == size ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            body_data = data + r.pos;
            put_str(&s, "\n");
            put_func_header(&s, &module, function);
            parsed = xxbyte_wasm_parse_function_body_ex(mode, body_data, body_size, &body);
            if (parsed != XXBYTE_STATUS_OK) {
                put_str(&s, "  ;; malformed locals\n");
                if (status == XXBYTE_STATUS_OK) {
                    status = XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
            } else {
                const xxbyte_wasm_type *type = function < module.function_count ? func_type(&module, module.functions[function].type) : NULL;
                xxbyte_status listed;
                put_locals(&s, &module, function, type != NULL ? type->param_count : 0u, body_data, body_size, &body);
                if (s.failed) {
                    break;
                }
                listed = list_body(mode, &module, &names, function, (uint32_t)r.pos + body.code_offset, body.code_size, listing_flags, out);
                if (listed != XXBYTE_STATUS_OK) {
                    status = listed;
                    break;
                }
            }
            r.pos += body_size;
        }
        if (status == XXBYTE_STATUS_OK && i == count && r.pos != r.end) {
            status = XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (status == XXBYTE_STATUS_TRUNCATED && r.end != size) {
            status = XXBYTE_STATUS_INVALID_INSTRUCTION; /* the section, not the file, ends early */
        }
    }
    xxbyte_wasm_names_free(&names);
    xxbyte_wasm_module_free(&module);
    if (s.failed || !xx_buf_ok(out)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
        return status;
    }
    if (result != XXBYTE_STATUS_OK) {
        return result;
    }
    return status;
}

#else
typedef int xxbyte_wasm_module_disabled;
#endif
