/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_lua_names.h"
#include "xxbyte_lua_number.h"

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#if XXBYTE_USE_LUA

/* Names, luac -l -l comments and whole-chunk listings (luac.c/print.c of
 * each series). */

#define LUA_MAXSTACK_50 250u
#define LUA_TM_COUNT 25u

static const char *const lua_events[LUA_TM_COUNT] = {
    "__index", "__newindex", "__gc",   "__mode", "__len", "__eq",  "__add",  "__sub", "__mul", "__mod",    "__pow",  "__div",   "__idiv",
    "__band",  "__bor",      "__bxor", "__shl",  "__shr", "__unm", "__bnot", "__lt",  "__le",  "__concat", "__call", "__close",
};

/* Text goes either to an xx_buf or to a fixed buffer (the resolver's). */
typedef struct lua_sink {
    xx_buf_t *out;
    char *buffer;
    size_t capacity; /* without the NUL */
    size_t length;
    int ok; /* 0 after an allocation failure or when the buffer is full */
} lua_sink;

static void sink_buf(lua_sink *s, xx_buf_t *out)
{
    memset(s, 0, sizeof(*s));
    s->out = out;
    s->ok = 1;
}

static void sink_put(lua_sink *s, const char *p, size_t n)
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

static void sink_str(lua_sink *s, const char *text)
{
    sink_put(s, text, strlen(text));
}

static void sink_char(lua_sink *s, char c)
{
    sink_put(s, &c, 1);
}

static void sink_int(lua_sink *s, long long value)
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
    sink_put(s, text + at, sizeof(text) - at);
}

static void sink_hex(lua_sink *s, unsigned long long value)
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
    sink_put(s, text + at, sizeof(text) - at);
}

/* "\ddd" for byte c. */
static void sink_decimal_escape(lua_sink *s, uint8_t c)
{
    char text[4];

    text[0] = '\\';
    text[1] = (char)('0' + c / 100u);
    text[2] = (char)('0' + c / 10u % 10u);
    text[3] = (char)('0' + c % 10u);
    sink_put(s, text, 4);
}

/* A name as luac's %s prints it (up to the first NUL), with control
 * characters as \ddd so that the listing keeps one line per entry. */
static void sink_name(lua_sink *s, const uint8_t *p, uint64_t n)
{
    uint64_t i;
    uint64_t run = 0;

    for (i = 0; i < n && p[i] != 0; ++i) {
        if (p[i] < 0x20u || p[i] == 0x7Fu) {
            sink_put(s, (const char *)p + run, (size_t)(i - run));
            sink_decimal_escape(s, p[i]);
            run = i + 1u;
        }
    }
    sink_put(s, (const char *)p + run, (size_t)(i - run));
}

/* PrintString: 5.0 stops at a NUL and escapes only the C escapes and the
 * quote; 5.1+ also escapes the backslash and every non-printing byte. */
static void sink_quoted(lua_sink *s, uint32_t minor, const uint8_t *p, uint64_t n)
{
    uint64_t i;

    sink_char(s, '"');
    for (i = 0; i < n; ++i) {
        uint8_t c = p[i];
        const char *escape = NULL;
        switch (c) {
            case '"': escape = "\\\""; break;
            case '\a': escape = "\\a"; break;
            case '\b': escape = "\\b"; break;
            case '\f': escape = "\\f"; break;
            case '\n': escape = "\\n"; break;
            case '\r': escape = "\\r"; break;
            case '\t': escape = "\\t"; break;
            case '\v': escape = "\\v"; break;
            case '\\':
                if (minor >= 1) {
                    escape = "\\\\";
                }
                break;
            default: break;
        }
        if (minor == 0 && c == 0) {
            break;
        }
        if (escape != NULL) {
            sink_str(s, escape);
        } else if (minor == 0 || (c >= 0x20u && c < 0x7Fu)) {
            sink_put(s, (const char *)&c, 1);
        } else {
            sink_decimal_escape(s, c);
        }
    }
    sink_char(s, '"');
}

/* A string of the chunk, checked against its size. */
static const uint8_t *lua_bytes(const xxbyte_lua_names *names, uint64_t offset, uint64_t length)
{
    if (offset > (uint64_t)names->size || length > (uint64_t)names->size - offset) {
        return NULL;
    }
    return names->data + offset;
}

static uint32_t lua_minor(const xxbyte_lua_names *names)
{
    return (uint32_t)names->header->version - 0x50u;
}

static const xxbyte_lua_function *lua_current(const xxbyte_lua_names *names)
{
    if (names == NULL || names->data == NULL || names->header == NULL || names->functions == NULL || names->function >= names->function_count ||
        names->header->version < 0x50 || names->header->version > 0x55) {
        return NULL;
    }
    return &names->functions[names->function];
}

static int lua_constant(const xxbyte_lua_names *names, const xxbyte_lua_function *f, uint64_t index, xxbyte_lua_constant *k)
{
    if (index >= f->constant_count) {
        return 0;
    }
    if (names->constants != NULL) {
        *k = names->constants[index];
        return 1;
    }
    return xxbyte_lua_get_constants_ex(names->data, names->size, names->header, f, (uint32_t)index, k, 1, names->saved, names->saved_count) == XXBYTE_STATUS_OK;
}

static void sink_number(lua_sink *s, const xxbyte_lua_header *header, uint32_t minor, const xxbyte_lua_constant *k)
{
    char text[XXBYTE_LUA_G_SIZE];
    uint64_t bits = (uint64_t)k->value;
    uint32_t precision = minor >= 5 ? 15u : 14u; /* LUA_NUMBER_FMT */
    size_t i;

    if (header->number_integral) {
        sink_int(s, (long long)k->value);
        return;
    }
    if (k->size == 4) {
        /* A float lua_Number: "%.7g" of the value promoted to double. */
        bits = xxbyte_lua_float_bits((uint32_t)bits);
        precision = 7;
    }
    xxbyte_lua_format_g(bits, precision, 1, text);
    sink_str(s, text);
    if (minor >= 3) {
        /* lua_Number2str's "looks like an int" test of luac 5.3+. */
        for (i = 0; text[i] != '\0'; ++i) {
            if (text[i] != '-' && (text[i] < '0' || text[i] > '9')) {
                return;
            }
        }
        sink_str(s, ".0");
    }
}

/* PrintConstant (bare: the %s of 5.0/5.1 GETGLOBAL and SETGLOBAL).
 * Returns 0 when the constant is missing. */
static int sink_constant(lua_sink *s, const xxbyte_lua_names *names, const xxbyte_lua_function *f, uint64_t index, int bare)
{
    xxbyte_lua_constant k;
    uint32_t minor = lua_minor(names);
    const uint8_t *p;

    if (!lua_constant(names, f, index, &k)) {
        return 0;
    }
    switch (k.type) {
        case XXBYTE_LUA_CONSTANT_NIL: sink_str(s, "nil"); break;
        case XXBYTE_LUA_CONSTANT_BOOLEAN: sink_str(s, k.value != 0 ? "true" : "false"); break;
        case XXBYTE_LUA_CONSTANT_NUMBER: sink_number(s, names->header, minor, &k); break;
        case XXBYTE_LUA_CONSTANT_INTEGER: sink_int(s, (long long)k.value); break;
        default:
            p = lua_bytes(names, k.string_offset, k.string_length);
            if (p == NULL) {
                return 0;
            }
            if (bare) {
                sink_name(s, p, k.string_length);
            } else {
                sink_quoted(s, minor, p, k.string_length);
            }
            break;
    }
    return 1;
}

/* UPVALNAME: "-" when the chunk keeps no name. Returns 0 when the index
 * is outside the function's upvalues. */
static int sink_upvalue(lua_sink *s, const xxbyte_lua_names *names, const xxbyte_lua_function *f, uint64_t index)
{
    xxbyte_lua_string name;
    const uint8_t *p;
    uint32_t minor = lua_minor(names);

    if (minor <= 1) {
        /* f->sizeupvalues is the name count; luac prints "-" for none. */
        if (f->upvalue_name_count == 0) {
            sink_char(s, '-');
            return 1;
        }
    } else if (index >= f->upvalue_count) {
        return 0;
    }
    if (index >= f->upvalue_name_count) {
        if (minor <= 1) {
            return 0;
        }
        sink_char(s, '-');
        return 1;
    }
    if (names->upvalue_names != NULL) {
        name = names->upvalue_names[index];
    } else if (xxbyte_lua_get_upvalue_names_ex(names->data, names->size, names->header, f, (uint32_t)index, &name, 1, names->saved, names->saved_count) !=
               XXBYTE_STATUS_OK) {
        return 0;
    }
    if (name.offset == 0) {
        if (minor <= 1) {
            return 0;
        }
        sink_char(s, '-');
        return 1;
    }
    p = lua_bytes(names, name.offset, name.length);
    if (p == NULL) {
        return 0;
    }
    sink_name(s, p, name.length);
    return 1;
}

/* The function CLOSURE index creates, or NULL. */
static const xxbyte_lua_function *lua_child(const xxbyte_lua_names *names, const xxbyte_lua_function *f, uint64_t index)
{
    size_t j;

    if (index >= f->prototype_count) {
        return NULL;
    }
    if (names->children != NULL) {
        uint32_t c = names->children[index];
        return c < names->function_count ? &names->functions[c] : NULL;
    }
    for (j = (size_t)names->function + 1u; j < names->function_count && names->functions[j].depth > f->depth; ++j) {
        if (names->functions[j].parent == names->function && names->functions[j].child == index) {
            return &names->functions[j];
        }
    }
    return NULL;
}

static int sink_child(lua_sink *s, const xxbyte_lua_names *names, const xxbyte_lua_function *f, uint64_t index)
{
    const xxbyte_lua_function *child = lua_child(names, f, index);

    if (child == NULL) {
        return 0;
    }
    sink_hex(s, (unsigned long long)child->offset);
    return 1;
}

void xxbyte_lua_names_init(xxbyte_lua_names *names, const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *functions,
                           size_t function_count, const xxbyte_lua_string *saved, size_t saved_count)
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
    names->saved = saved;
    names->saved_count = saved_count;
}

size_t xxbyte_lua_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_lua_names *names = (const xxbyte_lua_names *)context;
    const xxbyte_lua_function *f = lua_current(names);
    const xxbyte_operand *operand;
    lua_sink s;
    int found = 0;

    if (f == NULL || instruction == NULL || buffer == NULL || buffer_size == 0 || operand_index >= instruction->operand_count ||
        instruction->family != XXBYTE_FAMILY_LUA) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (operand->type != XXBYTE_OPERAND_INDEX || operand->value < 0) {
        return 0;
    }
    memset(&s, 0, sizeof(s));
    s.buffer = buffer;
    s.capacity = buffer_size - 1u;
    s.ok = 1;
    switch (operand->kind) {
        case XXBYTE_INDEX_CONSTANT: {
            int bare = lua_minor(names) <= 1 && (instruction->id == XXBYTE_LUA_ID_GETGLOBAL || instruction->id == XXBYTE_LUA_ID_SETGLOBAL);
            found = sink_constant(&s, names, f, (uint64_t)operand->value, bare);
            break;
        }
        case XXBYTE_INDEX_UPVALUE: found = sink_upvalue(&s, names, f, (uint64_t)operand->value); break;
        case XXBYTE_INDEX_FUNCTION: found = sink_child(&s, names, f, (uint64_t)operand->value); break;
        default: break;
    }
    if (!found || !s.ok || s.length == 0) {
        buffer[0] = '\0';
        return 0;
    }
    buffer[s.length] = '\0';
    return s.length;
}

/* -- luac -l -l comments ------------------------------------------------ */

typedef struct luac_line {
    const xxbyte_lua_names *names;
    const xxbyte_lua_function *f;
    const xxbyte_instruction *instruction;
    lua_sink *s;
    uint32_t minor;
    uint32_t unit;
    uint64_t pc;
    int in_code; /* the instruction is word pc of f's code */
} luac_line;

static uint32_t luac_read_word(uint32_t mode, const uint8_t *p, uint32_t unit)
{
    int big = (mode & XXBYTE_LUA_MODE_BIG_ENDIAN) != 0;

    if (unit == 8) {
        p += big ? 4 : 0;
    }
    return big ? xxbyte_u32be(p) : xxbyte_u32le(p);
}

/* Word pc + delta of the function's code, when the instruction is in it. */
static int luac_word(const luac_line *l, int delta, uint32_t *word)
{
    uint64_t pc = l->pc + (uint64_t)(int64_t)delta;

    if (!l->in_code || (delta < 0 && l->pc < (uint64_t)-delta) || pc >= l->f->code_count) {
        return 0;
    }
    *word = luac_read_word(l->instruction->mode, l->names->data + l->f->code_offset + pc * l->unit, l->unit);
    return 1;
}

static void luac_k(const luac_line *l, uint64_t index)
{
    if (!sink_constant(l->s, l->names, l->f, index, 0)) {
        sink_char(l->s, '?');
    }
}

static void luac_bare(const luac_line *l, uint64_t index)
{
    if (!sink_constant(l->s, l->names, l->f, index, 1)) {
        sink_char(l->s, '?');
    }
}

static void luac_uv(const luac_line *l, uint64_t index)
{
    if (!sink_upvalue(l->s, l->names, l->f, index)) {
        sink_char(l->s, '?');
    }
}

static void luac_to(const luac_line *l, const char *prefix, long long target)
{
    sink_str(l->s, " ; ");
    sink_str(l->s, prefix);
    sink_int(l->s, target);
}

/* "; K K", "; - K", ... of the RK binary operators before 5.4. */
static void luac_rk_pair(const luac_line *l, uint32_t b, uint32_t c, int b_const, int c_const, uint32_t b_index, uint32_t c_index)
{
    (void)b;
    (void)c;
    if (!b_const && !c_const) {
        return;
    }
    sink_str(l->s, " ; ");
    if (b_const) {
        luac_k(l, b_index);
    } else {
        sink_char(l->s, '-');
    }
    sink_char(l->s, ' ');
    if (c_const) {
        luac_k(l, c_index);
    } else if (l->minor >= 1) {
        sink_char(l->s, '-');
    }
}

static void luac_closure(const luac_line *l, uint64_t index)
{
    sink_str(l->s, " ; ");
    if (!sink_child(l->s, l->names, l->f, index)) {
        sink_char(l->s, '?');
    }
}

static void luac_50(const luac_line *l, uint32_t id, uint32_t w)
{
    uint32_t a = w >> 24;
    uint32_t b = (w >> 15) & 0x1FFu;
    uint32_t c = (w >> 6) & 0x1FFu;
    uint32_t bx = (w >> 6) & 0x3FFFFu;
    long long sbx = (long long)bx - 131071;

    (void)a;
    switch (id) {
        case XXBYTE_LUA_ID_LOADK:
            sink_str(l->s, " ; ");
            luac_k(l, bx);
            break;
        case XXBYTE_LUA_ID_GETUPVAL:
        case XXBYTE_LUA_ID_SETUPVAL:
            sink_str(l->s, " ; ");
            luac_uv(l, b);
            break;
        case XXBYTE_LUA_ID_GETGLOBAL:
        case XXBYTE_LUA_ID_SETGLOBAL:
            sink_str(l->s, " ; ");
            luac_bare(l, bx);
            break;
        case XXBYTE_LUA_ID_GETTABLE:
        case XXBYTE_LUA_ID_SELF:
            if (c >= LUA_MAXSTACK_50) {
                sink_str(l->s, " ; ");
                luac_k(l, c - LUA_MAXSTACK_50);
            }
            break;
        case XXBYTE_LUA_ID_SETTABLE:
        case XXBYTE_LUA_ID_ADD:
        case XXBYTE_LUA_ID_SUB:
        case XXBYTE_LUA_ID_MUL:
        case XXBYTE_LUA_ID_DIV:
        case XXBYTE_LUA_ID_POW:
        case XXBYTE_LUA_ID_EQ:
        case XXBYTE_LUA_ID_LT:
        case XXBYTE_LUA_ID_LE: luac_rk_pair(l, b, c, b >= LUA_MAXSTACK_50, c >= LUA_MAXSTACK_50, b - LUA_MAXSTACK_50, c - LUA_MAXSTACK_50); break;
        case XXBYTE_LUA_ID_JMP:
        case XXBYTE_LUA_ID_FORLOOP:
        case XXBYTE_LUA_ID_TFORPREP: luac_to(l, "to ", sbx + (long long)l->pc + 2); break;
        case XXBYTE_LUA_ID_CLOSURE: luac_closure(l, bx); break;
        default: break;
    }
}

/* 5.1 to 5.3. */
static void luac_51(const luac_line *l, uint32_t id, uint32_t w)
{
    uint32_t a = (w >> 6) & 0xFFu;
    uint32_t c = (w >> 14) & 0x1FFu;
    uint32_t b = w >> 23;
    uint32_t bx = w >> 14;
    uint32_t ax = w >> 6;
    long long sbx = (long long)bx - 131071;
    uint32_t minor = l->minor;
    int bk = (b & 0x100u) != 0;
    int ck = (c & 0x100u) != 0;
    uint32_t word;

    switch (id) {
        case XXBYTE_LUA_ID_LOADK:
            sink_str(l->s, " ; ");
            luac_k(l, bx);
            return;
        case XXBYTE_LUA_ID_GETUPVAL:
        case XXBYTE_LUA_ID_SETUPVAL:
            sink_str(l->s, " ; ");
            luac_uv(l, b);
            return;
        case XXBYTE_LUA_ID_GETGLOBAL:
        case XXBYTE_LUA_ID_SETGLOBAL:
            sink_str(l->s, " ; ");
            luac_bare(l, bx);
            return;
        case XXBYTE_LUA_ID_GETTABUP:
            sink_str(l->s, " ; ");
            luac_uv(l, b);
            if (ck) {
                sink_char(l->s, ' ');
                luac_k(l, c & 0xFFu);
            }
            return;
        case XXBYTE_LUA_ID_SETTABUP:
            sink_str(l->s, " ; ");
            luac_uv(l, a);
            if (bk) {
                sink_char(l->s, ' ');
                luac_k(l, b & 0xFFu);
            }
            if (ck) {
                sink_char(l->s, ' ');
                luac_k(l, c & 0xFFu);
            }
            return;
        case XXBYTE_LUA_ID_GETTABLE:
        case XXBYTE_LUA_ID_SELF:
            if (ck) {
                sink_str(l->s, " ; ");
                luac_k(l, c & 0xFFu);
            }
            return;
        case XXBYTE_LUA_ID_SETTABLE:
        case XXBYTE_LUA_ID_ADD:
        case XXBYTE_LUA_ID_SUB:
        case XXBYTE_LUA_ID_MUL:
        case XXBYTE_LUA_ID_DIV:
        case XXBYTE_LUA_ID_POW:
        case XXBYTE_LUA_ID_EQ:
        case XXBYTE_LUA_ID_LT:
        case XXBYTE_LUA_ID_LE: luac_rk_pair(l, b, c, bk, ck, b & 0xFFu, c & 0xFFu); return;
        case XXBYTE_LUA_ID_MOD:
        case XXBYTE_LUA_ID_IDIV:
        case XXBYTE_LUA_ID_BAND:
        case XXBYTE_LUA_ID_BOR:
        case XXBYTE_LUA_ID_BXOR:
        case XXBYTE_LUA_ID_SHL:
        case XXBYTE_LUA_ID_SHR:
            if (minor == 3) {
                luac_rk_pair(l, b, c, bk, ck, b & 0xFFu, c & 0xFFu);
            }
            return;
        case XXBYTE_LUA_ID_TFORLOOP:
            if (minor >= 2) {
                luac_to(l, "to ", sbx + (long long)l->pc + 2);
            }
            return;
        case XXBYTE_LUA_ID_JMP:
        case XXBYTE_LUA_ID_FORLOOP:
        case XXBYTE_LUA_ID_FORPREP: luac_to(l, "to ", sbx + (long long)l->pc + 2); return;
        case XXBYTE_LUA_ID_CLOSURE: luac_closure(l, bx); return;
        case XXBYTE_LUA_ID_SETLIST:
            sink_str(l->s, " ; ");
            if (c != 0) {
                sink_int(l->s, (long long)c);
            } else if (minor == 1 && l->instruction->size >= 2u * l->unit) {
                /* The block number word the decoder took with it. */
                sink_int(l->s, (long long)(int32_t)luac_read_word(l->instruction->mode, l->instruction->code + l->unit, l->unit));
            } else if (luac_word(l, 1, &word)) {
                sink_int(l->s, (long long)(int32_t)word);
            } else {
                sink_char(l->s, '?');
            }
            return;
        case XXBYTE_LUA_ID_EXTRAARG:
            /* luac skips the word after a SETLIST with C == 0. */
            if (luac_word(l, -1, &word) && (word & 0x3Fu) < 64u) {
                const char *name = xxbyte_lua_opname(XXBYTE_LUA_MODE(5, minor), word & 0x3Fu);
                if (name != NULL && strcmp(name, "SETLIST") == 0 && ((word >> 14) & 0x1FFu) == 0) {
                    return;
                }
            }
            sink_str(l->s, " ; ");
            luac_k(l, ax);
            return;
        default: return;
    }
}

static void luac_count(const luac_line *l, uint32_t n, const char *what)
{
    if (n == 0) {
        sink_str(l->s, "all");
    } else {
        sink_int(l->s, (long long)n - 1);
    }
    sink_str(l->s, what);
}

static void luac_event(const luac_line *l, uint32_t c)
{
    sink_str(l->s, c < LUA_TM_COUNT ? lua_events[c] : "?");
}

/* The EXTRAARG of the next word, as luac reads it (0 when it is missing). */
static uint32_t luac_extra(const luac_line *l)
{
    uint32_t word;

    return luac_word(l, 1, &word) ? word >> 7 : 0u;
}

/* 5.4 and 5.5. */
static void luac_54(const luac_line *l, uint32_t id, uint32_t w)
{
    uint32_t a = (w >> 7) & 0xFFu;
    uint32_t k = (w >> 15) & 1u;
    uint32_t b = (w >> 16) & 0xFFu;
    uint32_t c = w >> 24;
    uint32_t bx = w >> 15;
    long long sj = (long long)(w >> 7) - 16777215;
    long long pc = (long long)l->pc;
    uint32_t vc = w >> 22;
    uint32_t word;

    switch (id) {
        case XXBYTE_LUA_ID_LOADK:
            sink_str(l->s, " ; ");
            luac_k(l, bx);
            break;
        case XXBYTE_LUA_ID_LOADKX:
            sink_str(l->s, " ; ");
            if (luac_word(l, 1, &word)) {
                luac_k(l, word >> 7);
            } else {
                sink_char(l->s, '?');
            }
            break;
        case XXBYTE_LUA_ID_LOADNIL:
            sink_str(l->s, " ; ");
            sink_int(l->s, (long long)b + 1);
            sink_str(l->s, " out");
            break;
        case XXBYTE_LUA_ID_GETUPVAL:
        case XXBYTE_LUA_ID_SETUPVAL:
            sink_str(l->s, " ; ");
            luac_uv(l, b);
            break;
        case XXBYTE_LUA_ID_GETTABUP:
            sink_str(l->s, " ; ");
            luac_uv(l, b);
            sink_char(l->s, ' ');
            luac_k(l, c);
            break;
        case XXBYTE_LUA_ID_GETFIELD:
        case XXBYTE_LUA_ID_ADDK:
        case XXBYTE_LUA_ID_SUBK:
        case XXBYTE_LUA_ID_MULK:
        case XXBYTE_LUA_ID_MODK:
        case XXBYTE_LUA_ID_POWK:
        case XXBYTE_LUA_ID_DIVK:
        case XXBYTE_LUA_ID_IDIVK:
        case XXBYTE_LUA_ID_BANDK:
        case XXBYTE_LUA_ID_BORK:
        case XXBYTE_LUA_ID_BXORK:
            sink_str(l->s, " ; ");
            luac_k(l, c);
            break;
        case XXBYTE_LUA_ID_SETTABUP:
            sink_str(l->s, " ; ");
            luac_uv(l, a);
            sink_char(l->s, ' ');
            luac_k(l, b);
            if (k) {
                sink_char(l->s, ' ');
                luac_k(l, c);
            }
            break;
        case XXBYTE_LUA_ID_SETTABLE:
        case XXBYTE_LUA_ID_SETI:
        case XXBYTE_LUA_ID_SELF:
            if (k) {
                sink_str(l->s, " ; ");
                luac_k(l, c);
            }
            break;
        case XXBYTE_LUA_ID_SETFIELD:
            sink_str(l->s, " ; ");
            luac_k(l, b);
            if (k) {
                sink_char(l->s, ' ');
                luac_k(l, c);
            }
            break;
        case XXBYTE_LUA_ID_NEWTABLE:
            sink_str(l->s, " ; ");
            sink_int(l->s, (long long)(l->minor >= 5 ? vc : c) + (long long)luac_extra(l) * 256);
            break;
        case XXBYTE_LUA_ID_SETLIST:
            if (k) {
                /* 5.5's luac still adds the 8-bit C here, not vC. */
                sink_str(l->s, " ; ");
                sink_int(l->s, (long long)c + (long long)luac_extra(l) * 256);
            }
            break;
        case XXBYTE_LUA_ID_MMBIN:
            sink_str(l->s, " ; ");
            luac_event(l, c);
            break;
        case XXBYTE_LUA_ID_MMBINI:
            sink_str(l->s, " ; ");
            luac_event(l, c);
            if (k) {
                sink_str(l->s, " flip");
            }
            break;
        case XXBYTE_LUA_ID_MMBINK:
            sink_str(l->s, " ; ");
            luac_event(l, c);
            sink_char(l->s, ' ');
            luac_k(l, b);
            if (k) {
                sink_str(l->s, " flip");
            }
            break;
        case XXBYTE_LUA_ID_JMP: luac_to(l, "to ", sj + pc + 2); break;
        case XXBYTE_LUA_ID_EQK:
            sink_str(l->s, " ; ");
            luac_k(l, b);
            break;
        case XXBYTE_LUA_ID_CALL:
            sink_str(l->s, " ; ");
            luac_count(l, b, " in ");
            luac_count(l, c, " out");
            break;
        case XXBYTE_LUA_ID_TAILCALL:
            sink_str(l->s, " ; ");
            sink_int(l->s, (long long)b - 1);
            sink_str(l->s, " in");
            break;
        case XXBYTE_LUA_ID_RETURN:
            sink_str(l->s, " ; ");
            luac_count(l, b, " out");
            break;
        case XXBYTE_LUA_ID_FORLOOP:
        case XXBYTE_LUA_ID_TFORLOOP: luac_to(l, "to ", pc - (long long)bx + 2); break;
        case XXBYTE_LUA_ID_FORPREP: luac_to(l, "exit to ", pc + (long long)bx + 3); break;
        case XXBYTE_LUA_ID_TFORPREP: luac_to(l, "to ", pc + (long long)bx + 2); break;
        case XXBYTE_LUA_ID_CLOSURE: luac_closure(l, bx); break;
        case XXBYTE_LUA_ID_VARARG:
            sink_str(l->s, " ; ");
            luac_count(l, c, " out");
            break;
        case XXBYTE_LUA_ID_ERRNNIL:
            sink_str(l->s, " ; ");
            if (bx == 0) {
                sink_char(l->s, '?');
            } else {
                luac_k(l, (uint64_t)bx - 1u);
            }
            break;
        default: break;
    }
}

int xxbyte_lua_luac_comment(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const xxbyte_lua_names *names = (const xxbyte_lua_names *)fmt->context;
    luac_line l;
    lua_sink s;
    uint32_t word;

    memset(&l, 0, sizeof(l));
    l.f = lua_current(names);
    if (l.f == NULL) {
        return 1; /* nothing to name */
    }
    l.names = names;
    l.instruction = instruction;
    l.minor = lua_minor(names);
    l.unit = (instruction->mode & XXBYTE_LUA_MODE_WIDE) != 0 ? 8u : 4u;
    l.pc = instruction->address / l.unit;
    if (instruction->address % l.unit == 0 && l.pc < l.f->code_count && l.f->code_offset <= (uint64_t)names->size &&
        (uint64_t)l.f->code_count * l.unit <= (uint64_t)names->size - l.f->code_offset &&
        memcmp(names->data + l.f->code_offset + l.pc * l.unit, instruction->code, l.unit) == 0) {
        l.in_code = 1;
    }
    sink_buf(&s, fmt->out);
    l.s = &s;
    word = luac_read_word(instruction->mode, instruction->code, l.unit);
    if (XXBYTE_LUA_MODE_MINOR(instruction->mode) != l.minor) {
        return 1; /* the names belong to another series */
    }
    if (l.minor == 0) {
        luac_50(&l, instruction->id, word);
    } else if (l.minor <= 3) {
        luac_51(&l, instruction->id, word);
    } else {
        luac_54(&l, instruction->id, word);
    }
    return s.ok;
}

/* -- Whole chunks ------------------------------------------------------- */

typedef struct lua_lister {
    const uint8_t *data;
    size_t size;
    xxbyte_lua_header header;
    uint32_t mode;
    uint32_t listing_flags;
    xx_buf_t *out;
    xxbyte_lua_function *functions;
    size_t count;
    xxbyte_lua_string *saved;
    size_t saved_count;
    uint32_t minor;
} lua_lister;

static const char *lua_plural(uint64_t n)
{
    return n == 1 ? "" : "s";
}

static int lua_appendf_count(lua_sink *s, uint64_t n, const char *word)
{
    sink_int(s, (long long)n);
    sink_char(s, ' ');
    sink_str(s, word);
    sink_str(s, lua_plural(n));
    return s->ok;
}

/* The source luac shows for function index: its own, its parent's
 * (5.0, 5.1, 5.3, 5.4), or "=?". */
static xxbyte_status lua_effective_source(const lua_lister *ls, size_t index, xxbyte_lua_string *source, int *present)
{
    for (;;) {
        const xxbyte_lua_function *f = &ls->functions[index];
        xxbyte_status status = xxbyte_lua_get_source(ls->data, ls->size, &ls->header, f, ls->saved, ls->saved_count, source, present);
        if (status != XXBYTE_STATUS_OK || *present || ls->minor == 2 || ls->minor == 5 || f->parent == XXBYTE_LUA_NO_PARENT || f->parent >= ls->count) {
            return status;
        }
        index = f->parent;
    }
}

static xxbyte_status lua_list_header(const lua_lister *ls, size_t index, lua_sink *s)
{
    const xxbyte_lua_function *f = &ls->functions[index];
    xxbyte_lua_string source;
    int present = 0;
    xxbyte_status status = lua_effective_source(ls, index, &source, &present);
    uint32_t minor = ls->minor;
    int vararg = minor == 5 ? (f->is_vararg & 3u) != 0 : f->is_vararg != 0;

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    sink_str(s, f->line_defined == 0 ? "main <" : "function <");
    if (!present) {
        sink_char(s, '?');
    } else {
        const uint8_t *p = ls->data + source.offset;
        if (source.length > 0 && (p[0] == '@' || p[0] == '=')) {
            sink_name(s, p + 1, source.length - 1u);
        } else if (source.length > 0 && p[0] == 0x1B) {
            sink_str(s, "(bstring)");
        } else {
            sink_str(s, "(string)");
        }
    }
    sink_char(s, ':');
    sink_int(s, (long long)f->line_defined);
    if (minor >= 1) {
        sink_char(s, ',');
        sink_int(s, (long long)f->last_line_defined);
    }
    sink_str(s, "> (");
    lua_appendf_count(s, f->code_count, "instruction");
    if (minor <= 1) {
        sink_str(s, ", ");
        sink_int(s, (long long)((uint64_t)f->code_count * ls->header.instruction_size));
        sink_str(s, " bytes");
    }
    sink_str(s, " at ");
    sink_hex(s, (unsigned long long)f->offset);
    sink_str(s, ")\n");
    sink_int(s, (long long)f->num_params);
    sink_str(s, vararg ? "+" : "");
    sink_str(s, " param");
    sink_str(s, lua_plural(f->num_params));
    sink_str(s, ", ");
    lua_appendf_count(s, f->max_stack_size, minor == 0 ? "stack" : "slot");
    sink_str(s, ", ");
    lua_appendf_count(s, f->upvalue_count, "upvalue");
    sink_str(s, ", ");
    lua_appendf_count(s, f->local_count, "local");
    sink_str(s, ", ");
    lua_appendf_count(s, f->constant_count, "constant");
    sink_str(s, ", ");
    lua_appendf_count(s, f->prototype_count, "function");
    sink_char(s, '\n');
    return s->ok ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

static const char *lua_type_letter(uint32_t type)
{
    switch (type) {
        case XXBYTE_LUA_CONSTANT_NIL: return "N";
        case XXBYTE_LUA_CONSTANT_BOOLEAN: return "B";
        case XXBYTE_LUA_CONSTANT_NUMBER: return "F";
        case XXBYTE_LUA_CONSTANT_INTEGER: return "I";
        default: return "S";
    }
}

/* luac -l -l's constants, locals and upvalues tables (5.1+). */
static xxbyte_status lua_list_tables(const lua_lister *ls, xxbyte_lua_names *names, lua_sink *s)
{
    const xxbyte_lua_function *f = &ls->functions[names->function];
    xxbyte_lua_local *locals = NULL;
    xxbyte_lua_upvalue *upvalues = NULL;
    xxbyte_status status = XXBYTE_STATUS_OK;
    uint32_t minor = ls->minor;
    uint32_t i;

    sink_str(s, "constants (");
    sink_int(s, (long long)f->constant_count);
    sink_str(s, ") for ");
    sink_hex(s, (unsigned long long)f->offset);
    sink_str(s, ":\n");
    for (i = 0; i < f->constant_count; ++i) {
        sink_char(s, '\t');
        sink_int(s, (long long)i + (minor <= 3 ? 1 : 0));
        sink_char(s, '\t');
        if (minor >= 4) {
            sink_str(s, lua_type_letter(names->constants[i].type));
            sink_char(s, '\t');
        }
        if (!sink_constant(s, names, f, i, 0)) {
            sink_char(s, '?');
        }
        sink_char(s, '\n');
    }
    if (f->local_count != 0) {
        locals = (xxbyte_lua_local *)xx_mem_calloc(f->local_count, sizeof(*locals));
        if (locals == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        status = xxbyte_lua_get_locals(ls->data, ls->size, &ls->header, f, 0, locals, f->local_count, ls->saved, ls->saved_count);
    }
    if (status == XXBYTE_STATUS_OK) {
        sink_str(s, "locals (");
        sink_int(s, (long long)f->local_count);
        sink_str(s, ") for ");
        sink_hex(s, (unsigned long long)f->offset);
        sink_str(s, ":\n");
        for (i = 0; i < f->local_count; ++i) {
            const xxbyte_lua_local *v = &locals[i];
            sink_char(s, '\t');
            sink_int(s, (long long)i);
            sink_char(s, '\t');
            if (v->present) {
                sink_name(s, ls->data + v->name_offset, v->name_length);
            }
            sink_char(s, '\t');
            sink_int(s, (long long)v->start_pc + 1);
            sink_char(s, '\t');
            sink_int(s, (long long)v->end_pc + 1);
            sink_char(s, '\n');
        }
    }
    xx_mem_free(locals);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (minor <= 1) {
        /* 5.1: the upvalue names. */
        sink_str(s, "upvalues (");
        sink_int(s, (long long)f->upvalue_name_count);
        sink_str(s, ") for ");
        sink_hex(s, (unsigned long long)f->offset);
        sink_str(s, ":\n");
        for (i = 0; i < f->upvalue_name_count; ++i) {
            sink_char(s, '\t');
            sink_int(s, (long long)i);
            sink_char(s, '\t');
            if (!sink_upvalue(s, names, f, i)) {
                sink_char(s, '?');
            }
            sink_char(s, '\n');
        }
        return s->ok ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (f->upvalue_count != 0) {
        upvalues = (xxbyte_lua_upvalue *)xx_mem_calloc(f->upvalue_count, sizeof(*upvalues));
        if (upvalues == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        status = xxbyte_lua_get_upvalues(ls->data, ls->size, &ls->header, f, 0, upvalues, f->upvalue_count);
    }
    if (status == XXBYTE_STATUS_OK) {
        sink_str(s, "upvalues (");
        sink_int(s, (long long)f->upvalue_count);
        sink_str(s, ") for ");
        sink_hex(s, (unsigned long long)f->offset);
        sink_str(s, ":\n");
        for (i = 0; i < f->upvalue_count; ++i) {
            sink_char(s, '\t');
            sink_int(s, (long long)i);
            sink_char(s, '\t');
            if (!sink_upvalue(s, names, f, i)) {
                sink_char(s, '?');
            }
            sink_char(s, '\t');
            sink_int(s, (long long)upvalues[i].in_stack);
            sink_char(s, '\t');
            sink_int(s, (long long)upvalues[i].index);
            sink_char(s, '\n');
        }
    }
    xx_mem_free(upvalues);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    return s->ok ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

static xxbyte_status lua_list_function(lua_lister *ls, size_t index, uint32_t *children, xxbyte_lua_constant *constants, xxbyte_lua_string *upvalue_names)
{
    const xxbyte_lua_function *f = &ls->functions[index];
    xxbyte_lua_names names;
    xxbyte_status status;
    lua_sink s;
    size_t j;
    uint64_t code_size;

    xxbyte_lua_names_init(&names, ls->data, ls->size, &ls->header, ls->functions, ls->count, ls->saved, ls->saved_count);
    names.function = (uint32_t)index;
    /* Caches: constants, upvalue names and the children's indexes. */
    status = xxbyte_lua_get_constants_ex(ls->data, ls->size, &ls->header, f, 0, constants, f->constant_count, ls->saved, ls->saved_count);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    names.constants = constants;
    status = xxbyte_lua_get_upvalue_names_ex(ls->data, ls->size, &ls->header, f, 0, upvalue_names, f->upvalue_name_count, ls->saved, ls->saved_count);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    names.upvalue_names = upvalue_names;
    for (j = 0; j < f->prototype_count; ++j) {
        children[j] = UINT32_MAX;
    }
    for (j = index + 1u; j < ls->count && ls->functions[j].depth > f->depth; ++j) {
        if (ls->functions[j].parent == index && ls->functions[j].child < f->prototype_count) {
            children[ls->functions[j].child] = (uint32_t)j;
        }
    }
    names.children = children;

    sink_buf(&s, ls->out);
    if (index != 0) {
        sink_char(&s, '\n');
    }
    status = lua_list_header(ls, index, &s);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    code_size = (uint64_t)f->code_count * ls->header.instruction_size;
    if (f->code_offset > (uint64_t)ls->size || code_size > (uint64_t)ls->size - f->code_offset) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    status =
        xxbyte_disasm_buf_ex(XXBYTE_FAMILY_LUA, ls->mode, NULL, ls->data + f->code_offset, (size_t)code_size, 0, ls->listing_flags, xxbyte_lua_resolve, &names, ls->out);
    if (status != XXBYTE_STATUS_OK || ls->minor == 0) {
        return status;
    }
    return lua_list_tables(ls, &names, &s);
}

xxbyte_status xxbyte_lua_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    lua_lister ls;
    xxbyte_status status;
    uint32_t max_constants = 0;
    uint32_t max_names = 0;
    uint32_t max_children = 0;
    uint32_t *children = NULL;
    xxbyte_lua_constant *constants = NULL;
    xxbyte_lua_string *upvalue_names = NULL;
    size_t i;

    memset(&ls, 0, sizeof(ls));
    ls.data = data;
    ls.size = size;
    ls.out = out;
    ls.listing_flags = listing_flags;
    status = xxbyte_lua_parse_header(data, size, &ls.header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    ls.minor = (uint32_t)ls.header.version - 0x50u;
    ls.mode = mode == XXBYTE_MODE_AUTO ? ls.header.mode : mode;
    if (ls.mode == 0 || (mode != XXBYTE_MODE_AUTO && XXBYTE_LUA_MODE_MINOR(mode) != ls.minor)) {
        /* An instruction size or 5.0 field layout xxbyte does not decode,
         * or a mode of another series than the chunk's. */
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    status = xxbyte_lua_find_functions(data, size, &ls.header, NULL, 0, &ls.count);
    if (status == XXBYTE_STATUS_OK) {
        status = xxbyte_lua_get_saved_strings(data, size, &ls.header, NULL, 0, &ls.saved_count);
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    ls.functions = (xxbyte_lua_function *)xx_mem_calloc(ls.count, sizeof(*ls.functions));
    if (ls.saved_count != 0) {
        ls.saved = (xxbyte_lua_string *)xx_mem_calloc(ls.saved_count, sizeof(*ls.saved));
    }
    if (ls.functions == NULL || (ls.saved_count != 0 && ls.saved == NULL)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    status = xxbyte_lua_find_functions(data, size, &ls.header, ls.functions, ls.count, &ls.count);
    if (status == XXBYTE_STATUS_OK && ls.saved_count != 0) {
        status = xxbyte_lua_get_saved_strings(data, size, &ls.header, ls.saved, ls.saved_count, &ls.saved_count);
    }
    if (status != XXBYTE_STATUS_OK) {
        goto done;
    }
    for (i = 0; i < ls.count; ++i) {
        const xxbyte_lua_function *f = &ls.functions[i];
        if (f->constant_count > max_constants) {
            max_constants = f->constant_count;
        }
        if (f->upvalue_name_count > max_names) {
            max_names = f->upvalue_name_count;
        }
        if (f->prototype_count > max_children) {
            max_children = f->prototype_count;
        }
    }
    /* Every count is bounded by the records read, so by size. */
    children = (uint32_t *)xx_mem_calloc((size_t)max_children + 1u, sizeof(*children));
    constants = (xxbyte_lua_constant *)xx_mem_calloc((size_t)max_constants + 1u, sizeof(*constants));
    upvalue_names = (xxbyte_lua_string *)xx_mem_calloc((size_t)max_names + 1u, sizeof(*upvalue_names));
    if (children == NULL || constants == NULL || upvalue_names == NULL) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    for (i = 0; i < ls.count && status == XXBYTE_STATUS_OK; ++i) {
        status = lua_list_function(&ls, i, children, constants, upvalue_names);
    }
done:
    xx_mem_free(children);
    xx_mem_free(constants);
    xx_mem_free(upvalue_names);
    xx_mem_free(ls.saved);
    xx_mem_free(ls.functions);
    return status;
}

#else
typedef int xxbyte_lua_list_disabled;
#endif
