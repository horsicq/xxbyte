/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_ifps_internal.h"

#if XXBYTE_USE_IFPS

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

/*
 * PascalScript names and whole-script listings: the resolver of
 * xxbyte_ifps.h and the list_file of the ops table. The layout follows
 * IFPS3DataToText (uPSDisassembly.pas).
 */

#define BT2S_LIMIT 256u
#define MAX_LONGINT 0x7FFFFFFFu

/* ---------------------------------------------------------------- text */

/* Text goes either to a fixed buffer (cut, with the full length counted)
 * or to an xx_buf. */
typedef struct ifps_sink {
    char *buffer;
    size_t capacity;
    size_t length;
    xx_buf_t *out;
} ifps_sink;

static int sink_char(ifps_sink *sink, char c)
{
    if (sink->out != NULL) {
        return xx_buf_append_char(sink->out, c) ? 1 : 0;
    }
    if (sink->buffer != NULL && sink->length + 1u < sink->capacity) {
        sink->buffer[sink->length] = c;
    }
    ++sink->length;
    return 1;
}

/* Bytes as a name: printable ASCII, '\' doubled, else \XX. */
static int sink_escaped(ifps_sink *sink, const uint8_t *bytes, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;

    for (i = 0; i < size; ++i) {
        uint8_t c = bytes[i];
        if (c == '\\') {
            if (!sink_char(sink, '\\') || !sink_char(sink, '\\')) {
                return 0;
            }
        } else if (c >= 0x20u && c <= 0x7Eu) {
            if (!sink_char(sink, (char)c)) {
                return 0;
            }
        } else if (!sink_char(sink, '\\') || !sink_char(sink, hex[c >> 4]) || !sink_char(sink, hex[c & 15u])) {
            return 0;
        }
    }
    return 1;
}

static size_t sink_finish(ifps_sink *sink)
{
    if (sink->buffer != NULL && sink->capacity > 0) {
        sink->buffer[sink->length < sink->capacity ? sink->length : sink->capacity - 1u] = '\0';
    }
    return sink->length;
}

static size_t clamp32(size_t count)
{
    return count > UINT32_MAX ? UINT32_MAX : count;
}

/* data[offset..offset + size) lies in the script. */
static int in_data(const xxbyte_ifps_names *names, uint32_t offset, uint32_t size)
{
    return names->data != NULL && (uint64_t)offset + size <= (uint64_t)names->size;
}

static const uint8_t *find_byte(const uint8_t *p, size_t size, uint8_t c)
{
    return size == 0 ? NULL : (const uint8_t *)memchr(p, c, size);
}

/* ---------------------------------------------------------------- names */

void xxbyte_ifps_init_names(xxbyte_ifps_names *names, const uint8_t *data, size_t size, const xxbyte_ifps_type *types, size_t type_count, const xxbyte_ifps_proc *procs,
                            size_t proc_count, const xxbyte_ifps_var *vars, size_t var_count)
{
    if (names == NULL) {
        return;
    }
    names->data = data;
    names->size = data == NULL ? 0 : size;
    names->types = types;
    names->type_count = types == NULL ? 0u : (uint32_t)clamp32(type_count);
    names->procs = procs;
    names->proc_count = procs == NULL ? 0u : (uint32_t)clamp32(proc_count);
    names->vars = vars;
    names->var_count = vars == NULL ? 0u : (uint32_t)clamp32(var_count);
}

/* An external proc without an import name, by its declaration. */
static int decl_name(ifps_sink *sink, const uint8_t *decl, size_t size)
{
    const uint8_t *end = decl + size;
    const uint8_t *a;
    const uint8_t *b;

    if (size >= 6 && memcmp(decl, "class:", 6) == 0) {
        a = decl + 6;
        if (end - a == 1 && (*a == '-' || *a == '+')) {
            return sink_escaped(sink, decl, size) ? 1 : -1;
        }
        b = find_byte(a, (size_t)(end - a), '|');
        if (b == NULL || b == a) {
            return 0;
        }
        if (!sink_escaped(sink, a, (size_t)(b - a)) || !sink_char(sink, '.')) {
            return -1;
        }
        a = b + 1;
        b = find_byte(a, (size_t)(end - a), '|');
        if (b == NULL || b == a) {
            return 0;
        }
        return sink_escaped(sink, a, (size_t)(b - a)) ? 1 : -1;
    }
    if (size >= 4 && memcmp(decl, "dll:", 4) == 0) {
        const uint8_t *library = decl + 4;
        const uint8_t *library_end = find_byte(library, (size_t)(end - library), 0);
        if (library_end == NULL) {
            return 0;
        }
        a = library_end + 1;
        b = find_byte(a, (size_t)(end - a), 0);
        if (b == NULL || b == a) {
            return 0;
        }
        if (!sink_escaped(sink, a, (size_t)(b - a))) {
            return -1;
        }
        if (library_end != library && (!sink_char(sink, '@') || !sink_escaped(sink, library, (size_t)(library_end - library)))) {
            return -1;
        }
        return 1;
    }
    return 0;
}

/* 1 when a name was written, 0 when there is none, -1 on failure. A
 * partly written name is dropped by the callers (length reset). */
static int proc_name(const xxbyte_ifps_names *names, uint32_t index, ifps_sink *sink)
{
    const xxbyte_ifps_proc *proc;
    size_t mark = sink->length;
    int result;

    if (names == NULL || names->procs == NULL || index >= names->proc_count) {
        return 0;
    }
    proc = &names->procs[index];
    if ((proc->flags & XXBYTE_IFPS_PROC_EXTERNAL) != 0) {
        if (proc->name_size != 0) {
            if (!in_data(names, proc->name_offset, proc->name_size)) {
                return 0;
            }
            return sink_escaped(sink, names->data + proc->name_offset, proc->name_size) ? 1 : -1;
        }
        if ((proc->flags & XXBYTE_IFPS_PROC_EXPORTED) == 0 || !in_data(names, proc->decl_offset, proc->decl_size)) {
            return 0;
        }
        result = decl_name(sink, names->data + proc->decl_offset, proc->decl_size);
        if (result == 0) {
            sink->length = mark;
        }
        return result;
    }
    if ((proc->flags & XXBYTE_IFPS_PROC_EXPORTED) == 0 || proc->name_size == 0 || !in_data(names, proc->name_offset, proc->name_size)) {
        return 0;
    }
    return sink_escaped(sink, names->data + proc->name_offset, proc->name_size) ? 1 : -1;
}

static int var_name(const xxbyte_ifps_names *names, uint32_t index, ifps_sink *sink)
{
    const xxbyte_ifps_var *var;

    if (names == NULL || names->vars == NULL || index >= names->var_count) {
        return 0;
    }
    var = &names->vars[index];
    if ((var->flags & XXBYTE_IFPS_VAR_EXPORTED) == 0 || var->name_size == 0 || !in_data(names, var->name_offset, var->name_size)) {
        return 0;
    }
    return sink_escaped(sink, names->data + var->name_offset, var->name_size) ? 1 : -1;
}

static int type_name(const xxbyte_ifps_names *names, uint32_t index, ifps_sink *sink)
{
    const xxbyte_ifps_type *type;

    if (names == NULL || names->types == NULL || index >= names->type_count) {
        return 0;
    }
    type = &names->types[index];
    if ((type->flags & XXBYTE_IFPS_TYPE_EXPORTED) == 0 || type->name_size == 0 || !in_data(names, type->name_offset, type->name_size)) {
        return 0;
    }
    return sink_escaped(sink, names->data + type->name_offset, type->name_size) ? 1 : -1;
}

typedef int (*ifps_namer)(const xxbyte_ifps_names *names, uint32_t index, ifps_sink *sink);

static size_t name_to_buffer(ifps_namer namer, const xxbyte_ifps_names *names, uint32_t index, char *buffer, size_t buffer_size)
{
    ifps_sink sink;

    sink.buffer = buffer;
    sink.capacity = buffer == NULL ? 0 : buffer_size;
    sink.length = 0;
    sink.out = NULL;
    if (namer(names, index, &sink) <= 0) {
        sink.length = 0;
    }
    return sink_finish(&sink);
}

size_t xxbyte_ifps_proc_name(const xxbyte_ifps_names *names, uint32_t proc, char *buffer, size_t buffer_size)
{
    return name_to_buffer(proc_name, names, proc, buffer, buffer_size);
}

size_t xxbyte_ifps_var_name(const xxbyte_ifps_names *names, uint32_t var, char *buffer, size_t buffer_size)
{
    return name_to_buffer(var_name, names, var, buffer, buffer_size);
}

size_t xxbyte_ifps_type_name(const xxbyte_ifps_names *names, uint32_t type, char *buffer, size_t buffer_size)
{
    return name_to_buffer(type_name, names, type, buffer, buffer_size);
}

size_t xxbyte_ifps_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_ifps_names *names = (const xxbyte_ifps_names *)context;
    const xxbyte_operand *operand;
    uint32_t value;
    size_t length;
    ifps_namer namer;

    if (buffer != NULL && buffer_size > 0) {
        buffer[0] = '\0';
    }
    if (names == NULL || instruction == NULL || buffer == NULL || buffer_size == 0 || instruction->family != XXBYTE_FAMILY_IFPS ||
        operand_index >= instruction->operand_count) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (operand->type != XXBYTE_OPERAND_INDEX || operand->value < 0 || operand->value > (int64_t)UINT32_MAX) {
        return 0;
    }
    value = (uint32_t)operand->value;
    switch (operand->kind) {
        case XXBYTE_INDEX_FUNCTION: namer = proc_name; break;
        case XXBYTE_INDEX_GLOBAL:
        case XXBYTE_IFPS_INDEX_ELEMENT_GLOBAL: namer = var_name; break;
        case XXBYTE_INDEX_TYPE: namer = type_name; break;
        default: return 0;
    }
    length = name_to_buffer(namer, names, value, buffer, buffer_size);
    return length >= buffer_size ? buffer_size - 1u : length;
}

/* -------------------------------------------------------------- listing */

typedef struct ifps_listing {
    const uint8_t *data;
    size_t size;
    uint32_t mode;
    xxbyte_ifps_script script;
    xxbyte_ifps_type *types;
    xxbyte_ifps_proc *procs;
    xxbyte_ifps_var *vars;
    xxbyte_ifps_names names;
    xx_buf_t *out;
} ifps_listing;

/* BT2S of uPSDisassembly.pas with a budget of characters: emits text,
 * cut to the budget; returns 0 once the budget is spent before the text
 * ends (the caller then appends "..."). Every step emits text before it
 * recurses, so the depth is bounded by the budget. */
typedef struct ifps_bt2s {
    const ifps_listing *listing;
    uint32_t budget;
} ifps_bt2s;

static int bt2s_emit(ifps_bt2s *state, const char *text)
{
    size_t length = strlen(text);

    if (state->budget == 0) {
        return 0;
    }
    if (length > state->budget) {
        xx_buf_append(state->listing->out, text, state->budget);
        state->budget = 0;
        return 0;
    }
    xx_buf_append(state->listing->out, text, length);
    state->budget -= (uint32_t)length;
    return 1;
}

static int bt2s_number(ifps_bt2s *state, const char *before, uint32_t value, const char *after)
{
    char text[48];
    char digits[12];
    size_t n = 0;
    size_t length = 0;

    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    while (*before != '\0') {
        text[length++] = *before++;
    }
    while (n > 0) {
        text[length++] = digits[--n];
    }
    while (*after != '\0') {
        text[length++] = *after++;
    }
    text[length] = '\0';
    return bt2s_emit(state, text);
}

static int bt2s_type(ifps_bt2s *state, uint32_t number)
{
    const ifps_listing *listing = state->listing;
    const xxbyte_ifps_type *type;
    const char *name;
    uint32_t i;

    if (number >= listing->script.type_count) {
        return bt2s_number(state, "Type ", number, "");
    }
    type = &listing->types[number];
    switch (type->base_type) {
        case XXBYTE_IFPS_BT_RECORD:
            if (!bt2s_emit(state, "Record(")) {
                return 0;
            }
            for (i = 0; i < type->count; ++i) {
                uint64_t at = (uint64_t)type->data_offset + 4u * (uint64_t)i;
                if (at + 4u > listing->size || (i != 0 && !bt2s_emit(state, ",")) || !bt2s_type(state, xxbyte_u32le(listing->data + at))) {
                    return 0;
                }
            }
            return bt2s_emit(state, ")");
        case XXBYTE_IFPS_BT_ARRAY: return bt2s_emit(state, "Array of ") && bt2s_type(state, type->element_type);
        case XXBYTE_IFPS_BT_STATIC_ARRAY: return bt2s_number(state, "StaticArray[", type->count, "] of ") && bt2s_type(state, type->element_type);
        default: name = xxbyte_ifps_base_type_name(type->base_type); return name != NULL ? bt2s_emit(state, name) : bt2s_number(state, "Unknown ", type->base_type, "");
    }
}

static void put_bt2s(const ifps_listing *listing, uint32_t number)
{
    ifps_bt2s state;

    state.listing = listing;
    state.budget = BT2S_LIMIT;
    if (!bt2s_type(&state, number)) {
        xx_buf_append_str(listing->out, "...");
    }
}

static void put_escaped(const ifps_listing *listing, uint32_t offset, uint32_t size)
{
    ifps_sink sink;

    sink.buffer = NULL;
    sink.capacity = 0;
    sink.length = 0;
    sink.out = listing->out;
    if ((uint64_t)offset + size <= (uint64_t)listing->size) {
        sink_escaped(&sink, listing->data + offset, size);
    }
}

/* A bounded reader over the (already validated) script. */
typedef struct ifps_walk {
    const uint8_t *data;
    size_t size;
    uint64_t pos;
} ifps_walk;

static const uint8_t *walk_take(ifps_walk *walk, uint64_t count)
{
    const uint8_t *p;

    if (walk->pos > walk->size || count > walk->size - walk->pos) {
        return NULL;
    }
    p = walk->data + walk->pos;
    walk->pos += count;
    return p;
}

static int walk_u32(ifps_walk *walk, uint32_t *value)
{
    const uint8_t *p = walk_take(walk, 4);

    if (p == NULL) {
        return 0;
    }
    *value = xxbyte_u32le(p);
    return 1;
}

/* One attribute value of the given type, as the immediates print. */
static int put_value(const ifps_listing *listing, ifps_walk *walk, const xxbyte_ifps_type *type)
{
    xxbyte_fmt fmt;
    const uint8_t *p;
    uint32_t length;
    uint32_t width;
    uint64_t raw;
    int wide;

    fmt.out = listing->out;
    fmt.flags = 0;
    fmt.resolver = NULL;
    fmt.context = NULL;
    switch (type->base_type) {
        case XXBYTE_IFPS_BT_SET:
            width = (type->set_bits + 7u) / 8u;
            p = walk_take(walk, width);
            return p != NULL && xxbyte_ifps_format_set(&fmt, p, width);
        case XXBYTE_IFPS_BT_U8:
        case XXBYTE_IFPS_BT_S8:
        case XXBYTE_IFPS_BT_CHAR: width = 1; break;
        case XXBYTE_IFPS_BT_U16:
        case XXBYTE_IFPS_BT_S16:
        case XXBYTE_IFPS_BT_WIDE_CHAR: width = 2; break;
        case XXBYTE_IFPS_BT_U32:
        case XXBYTE_IFPS_BT_S32:
        case XXBYTE_IFPS_BT_PROC_PTR:
        case XXBYTE_IFPS_BT_SINGLE: width = 4; break;
        case XXBYTE_IFPS_BT_U64:
        case XXBYTE_IFPS_BT_S64:
        case XXBYTE_IFPS_BT_CURRENCY:
        case XXBYTE_IFPS_BT_DOUBLE: width = 8; break;
        case XXBYTE_IFPS_BT_EXTENDED: {
            double value;
            width = xxbyte_ifps_extended_size(listing->mode);
            p = walk_take(walk, width);
            if (p == NULL) {
                return 0;
            }
            raw = width == 8u ? xxbyte_u64le(p) : xxbyte_ifps_extended_bits(p);
            memcpy(&value, &raw, sizeof(value));
            return xxbyte_fmt_double(&fmt, value);
        }
        case XXBYTE_IFPS_BT_PCHAR:
        case XXBYTE_IFPS_BT_STRING:
        case XXBYTE_IFPS_BT_WIDE_STRING:
        case XXBYTE_IFPS_BT_UNICODE_STRING:
            wide = type->base_type == XXBYTE_IFPS_BT_WIDE_STRING || type->base_type == XXBYTE_IFPS_BT_UNICODE_STRING;
            if (!walk_u32(walk, &length) || length > MAX_LONGINT || (wide && length > MAX_LONGINT / 2u)) {
                return 0;
            }
            width = wide ? length * 2u : length;
            p = walk_take(walk, width);
            return p != NULL && xxbyte_ifps_format_string(&fmt, p, width, wide);
        default: return 0;
    }
    p = walk_take(walk, width);
    if (p == NULL) {
        return 0;
    }
    raw = width == 1u ? p[0] : width == 2u ? xxbyte_u16le(p) : width == 4u ? xxbyte_u32le(p) : xxbyte_u64le(p);
    switch (type->base_type) {
        case XXBYTE_IFPS_BT_S8:
        case XXBYTE_IFPS_BT_S16:
        case XXBYTE_IFPS_BT_S32:
        case XXBYTE_IFPS_BT_S64: return xxbyte_fmt_printf(&fmt, "%lld", (long long)xxbyte_sext(raw, width * 8u));
        case XXBYTE_IFPS_BT_CHAR:
        case XXBYTE_IFPS_BT_WIDE_CHAR: return xxbyte_fmt_printf(&fmt, "#%llu", (unsigned long long)raw);
        case XXBYTE_IFPS_BT_CURRENCY: return xxbyte_ifps_format_currency(&fmt, xxbyte_sext(raw, 64));
        case XXBYTE_IFPS_BT_SINGLE: {
            uint32_t bits = (uint32_t)raw;
            float value;
            memcpy(&value, &bits, sizeof(value));
            return xxbyte_fmt_float(&fmt, value);
        }
        case XXBYTE_IFPS_BT_DOUBLE: {
            double value;
            memcpy(&value, &raw, sizeof(value));
            return xxbyte_fmt_double(&fmt, value);
        }
        default: return xxbyte_fmt_printf(&fmt, "%llu", (unsigned long long)raw);
    }
}

/* " Attributes: NAME(v, ...), ..." from an attribute list (the parser
 * checked it); nothing for an empty list. */
static int put_attributes(const ifps_listing *listing, uint32_t offset, uint32_t count)
{
    ifps_walk walk;
    uint32_t stored;
    uint32_t i;

    if (count == 0) {
        return 1;
    }
    walk.data = listing->data;
    walk.size = listing->size;
    walk.pos = offset;
    if (!walk_u32(&walk, &stored) || !xx_buf_append_str(listing->out, " Attributes: ")) {
        return 0;
    }
    for (i = 0; i < stored; ++i) {
        uint32_t length;
        uint32_t fields;
        uint32_t j;
        uint64_t name;

        if ((i != 0 && !xx_buf_append_str(listing->out, ", ")) || !walk_u32(&walk, &length)) {
            return 0;
        }
        name = walk.pos;
        if (walk_take(&walk, length) == NULL || !walk_u32(&walk, &fields)) {
            return 0;
        }
        put_escaped(listing, (uint32_t)name, length);
        if (fields > MAX_LONGINT) {
            fields = 0;
        }
        for (j = 0; j < fields; ++j) {
            uint32_t number;
            if (!walk_u32(&walk, &number) || number >= listing->script.type_count || !xx_buf_append_str(listing->out, j == 0 ? "(" : ", ") ||
                !put_value(listing, &walk, &listing->types[number])) {
                return 0;
            }
        }
        if (fields != 0 && !xx_buf_append_char(listing->out, ')')) {
            return 0;
        }
    }
    return 1;
}

static void put_type_extras(const ifps_listing *listing, const xxbyte_ifps_type *type)
{
    const uint8_t *g;

    switch (type->base_type) {
        case XXBYTE_IFPS_BT_CLASS:
            xx_buf_append_str(listing->out, " ClassName: ");
            put_escaped(listing, type->data_offset, type->data_size);
            break;
        case XXBYTE_IFPS_BT_PROC_PTR:
            xx_buf_append_str(listing->out, " Params: ");
            put_escaped(listing, type->data_offset, type->data_size);
            break;
        case XXBYTE_IFPS_BT_INTERFACE:
            if ((uint64_t)type->data_offset + 16u > listing->size) {
                break;
            }
            g = listing->data + type->data_offset;
            xx_buf_appendf(listing->out, " GUID: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}", (unsigned)xxbyte_u32le(g), (unsigned)xxbyte_u16le(g + 4),
                           (unsigned)xxbyte_u16le(g + 6), g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
            break;
        case XXBYTE_IFPS_BT_SET: xx_buf_appendf(listing->out, " Bits: %u", (unsigned)type->set_bits); break;
        case XXBYTE_IFPS_BT_STATIC_ARRAY:
            if (type->start_index != 0) {
                xx_buf_appendf(listing->out, " Start: %d", (int)type->start_index);
            }
            break;
        default: break;
    }
}

static xxbyte_status list_tables(const ifps_listing *listing, uint32_t listing_flags)
{
    const xxbyte_ifps_script *script = &listing->script;
    xx_buf_t *out = listing->out;
    xxbyte_ifps_context context;
    uint32_t i;

    xx_buf_appendf(out, "; IFPS build %u: %u types, %u procs, %u vars, main proc ", (unsigned)script->build, (unsigned)script->type_count, (unsigned)script->proc_count,
                   (unsigned)script->var_count);
    if (script->main_proc == XXBYTE_IFPS_NONE) {
        xx_buf_append_str(out, "none\n[TYPES]\n");
    } else {
        xx_buf_appendf(out, "%u\n[TYPES]\n", (unsigned)script->main_proc);
    }
    for (i = 0; i < script->type_count; ++i) {
        const xxbyte_ifps_type *type = &listing->types[i];
        xx_buf_appendf(out, "Type [%u]: ", (unsigned)i);
        put_bt2s(listing, i);
        put_type_extras(listing, type);
        if ((type->flags & XXBYTE_IFPS_TYPE_EXPORTED) != 0) {
            xx_buf_append_str(out, " Export: ");
            put_escaped(listing, type->name_offset, type->name_size);
        }
        if (!put_attributes(listing, type->attributes_offset, type->attribute_count) || !xx_buf_append_char(out, '\n')) {
            return xx_buf_ok(out) ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    xx_buf_append_str(out, "[VARS]\n");
    for (i = 0; i < script->var_count; ++i) {
        const xxbyte_ifps_var *var = &listing->vars[i];
        const xxbyte_ifps_type *type = &listing->types[var->type];
        xx_buf_appendf(out, "Var [%u]: %u ", (unsigned)i, (unsigned)var->type);
        put_bt2s(listing, var->type);
        if (type->name_size != 0) {
            xx_buf_append_char(out, ' ');
            put_escaped(listing, type->name_offset, type->name_size);
        }
        if ((var->flags & XXBYTE_IFPS_VAR_EXPORTED) != 0) {
            xx_buf_append_str(out, " Export: ");
            put_escaped(listing, var->name_offset, var->name_size);
        }
        xx_buf_append_char(out, '\n');
    }
    xx_buf_append_str(out, "[PROCS]\n");
    xxbyte_ifps_init_context(&context, listing->types, script->type_count);
    for (i = 0; i < script->proc_count; ++i) {
        const xxbyte_ifps_proc *proc = &listing->procs[i];
        xxbyte_status status;

        xx_buf_appendf(out, "Proc [%u]", (unsigned)i);
        if ((proc->flags & XXBYTE_IFPS_PROC_EXTERNAL) != 0) {
            if ((proc->flags & XXBYTE_IFPS_PROC_EXPORTED) == 0) {
                xx_buf_append_str(out, ": External: ");
                put_escaped(listing, proc->name_offset, proc->name_size);
            } else {
                xx_buf_append_str(out, ": External Decl: ");
                put_escaped(listing, proc->decl_offset, proc->decl_size);
                if (proc->name_size != 0) {
                    xx_buf_append_char(out, ' ');
                    put_escaped(listing, proc->name_offset, proc->name_size);
                }
            }
        } else if ((proc->flags & XXBYTE_IFPS_PROC_EXPORTED) != 0) {
            xx_buf_append_str(out, ": Export: ");
            put_escaped(listing, proc->name_offset, proc->name_size);
            xx_buf_append_char(out, ' ');
            put_escaped(listing, proc->decl_offset, proc->decl_size);
        }
        if ((proc->flags & XXBYTE_IFPS_PROC_ATTRIBUTES) != 0 && !put_attributes(listing, proc->attributes_offset, proc->attribute_count)) {
            return xx_buf_ok(out) ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        if (i == script->main_proc) {
            xx_buf_append_str(out, " (main)");
        }
        if (!xx_buf_append_char(out, '\n')) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        if ((proc->flags & XXBYTE_IFPS_PROC_EXTERNAL) != 0) {
            continue;
        }
        if ((uint64_t)proc->code_offset + proc->code_size > listing->size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_IFPS, listing->mode, &context, listing->data + proc->code_offset, proc->code_size, 0, listing_flags,
                                      xxbyte_ifps_resolve, (void *)&listing->names, out);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return xx_buf_ok(out) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

/* Room for count entries, but never more than one per byte of the script:
 * every record takes at least one byte, so a larger count fails as
 * truncated before the array runs out. */
static void *alloc_table(uint32_t count, size_t size, size_t element)
{
    size_t n = count < size ? (size_t)count : size;
    return xx_mem_calloc(n + 1u, element);
}

xxbyte_status xxbyte_ifps_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    ifps_listing listing;
    xxbyte_status status;
    size_t type_capacity;
    size_t proc_capacity;
    size_t var_capacity;

    if (out == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (mode == XXBYTE_MODE_AUTO || mode == XXBYTE_IFPS_MODE_DEFAULT) {
        mode = XXBYTE_IFPS_MODE_32BIT;
    }
    if (xxbyte_ifps_check_mode(mode) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if (data == NULL || size < XXBYTE_IFPS_HEADER_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    memset(&listing, 0, sizeof(listing));
    listing.data = data;
    listing.size = size > UINT32_MAX ? UINT32_MAX : size;
    listing.mode = mode;
    listing.out = out;
    status = xxbyte_ifps_parse_header(data, size, &listing.script);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    type_capacity = listing.script.type_count < listing.size ? listing.script.type_count : listing.size;
    proc_capacity = listing.script.proc_count < listing.size ? listing.script.proc_count : listing.size;
    var_capacity = listing.script.var_count < listing.size ? listing.script.var_count : listing.size;
    listing.types = (xxbyte_ifps_type *)alloc_table(listing.script.type_count, listing.size, sizeof(xxbyte_ifps_type));
    listing.procs = (xxbyte_ifps_proc *)alloc_table(listing.script.proc_count, listing.size, sizeof(xxbyte_ifps_proc));
    listing.vars = (xxbyte_ifps_var *)alloc_table(listing.script.var_count, listing.size, sizeof(xxbyte_ifps_var));
    if (listing.types == NULL || listing.procs == NULL || listing.vars == NULL) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    } else {
        status =
            xxbyte_ifps_parse_script(data, listing.size, mode, &listing.script, listing.types, type_capacity, listing.procs, proc_capacity, listing.vars, var_capacity);
    }
    if (status == XXBYTE_STATUS_OK) {
        xxbyte_ifps_init_names(&listing.names, data, listing.size, listing.types, listing.script.type_count, listing.procs, listing.script.proc_count, listing.vars,
                               listing.script.var_count);
        status = list_tables(&listing, listing_flags);
    }
    xx_mem_free(listing.types);
    xx_mem_free(listing.procs);
    xx_mem_free(listing.vars);
    return status;
}

#else
typedef int xxbyte_ifps_file_disabled;
#endif
