/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_yarv_tables.h"
#include "xxbyte_yarv_ibf.h"

#if XXBYTE_USE_YARV

#define YARV_MODE(major, minor) XXBYTE_YARV_MODE(major, minor)
#define YARV_VERSION(mode) ((uint32_t)(mode) & XXBYTE_YARV_MODE_VERSION_MASK)

/* Reads the VALUE-sized units of one instruction. */
typedef struct yarv_reader {
    const uint8_t *code;
    size_t size;
    size_t pos;
    int raw;    /* Ruby 2.6: raw VALUE words */
    int word32; /* VALUE is 32 bits */
    int big;    /* raw words are big-endian */
} yarv_reader;

static const yarv_version *yarv_find_version(uint32_t mode)
{
    uint32_t version = YARV_VERSION(mode);
    size_t i;

    if ((mode & ~(uint32_t)(XXBYTE_YARV_MODE_VERSION_MASK | XXBYTE_YARV_MODE_FLAGS)) != 0) {
        return NULL;
    }
    for (i = 0; i < YARV_VERSION_COUNT; ++i) {
        if (xxbyte_yarv_versions[i].mode == version) {
            return &xxbyte_yarv_versions[i];
        }
    }
    return NULL;
}

static void yarv_reader_init(yarv_reader *reader, uint32_t mode, const uint8_t *code, size_t size)
{
    reader->code = code;
    reader->size = size;
    reader->pos = 0;
    reader->raw = YARV_VERSION(mode) < YARV_MODE(2, 7);
    reader->word32 = (mode & XXBYTE_YARV_MODE_WORD32) != 0;
    reader->big = (mode & XXBYTE_YARV_MODE_BIG_ENDIAN) != 0;
}

static uint64_t yarv_word(const uint8_t *p, unsigned width, int big)
{
    uint64_t value = 0;
    unsigned i;

    for (i = 0; i < width; ++i) {
        unsigned shift = big ? 8u * (width - 1u - i) : 8u * i;
        value |= (uint64_t)p[i] << shift;
    }
    return value;
}

/* The length of the small value whose first byte is c: one plus the
 * number of trailing zero bits, 9 for 0x00 (ibf_load_small_value). */
static unsigned yarv_small_length(uint8_t c)
{
    unsigned n = 1;

    if (c == 0) {
        return 9;
    }
    while ((c & 1u) == 0) {
        c = (uint8_t)(c >> 1);
        ++n;
    }
    return n;
}

/* Reads one raw word or small value. */
static xxbyte_status yarv_read(yarv_reader *reader, uint64_t *value)
{
    const uint8_t *p;
    size_t left = reader->size - reader->pos;
    unsigned n;
    unsigned i;
    uint64_t x;

    if (reader->raw) {
        n = reader->word32 ? 4u : 8u;
        if (left < n) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        *value = yarv_word(reader->code + reader->pos, n, reader->big);
        reader->pos += n;
        return XXBYTE_STATUS_OK;
    }
    if (left == 0) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    p = reader->code + reader->pos;
    n = yarv_small_length(p[0]);
    if (left < n) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    x = (uint64_t)p[0] >> n;
    for (i = 1; i < n; ++i) {
        x = (x << 8) | p[i];
    }
    if (reader->word32) {
        x &= 0xFFFFFFFFu;
    }
    *value = x;
    reader->pos += n;
    return XXBYTE_STATUS_OK;
}

/* A VALUE read as a signed quantity (SIGNED_VALUE). */
static int64_t yarv_signed(const yarv_reader *reader, uint64_t value)
{
    return reader->word32 ? xxbyte_sext(value, 32) : (int64_t)value;
}

/* The value cast to int, as ibf_load_iseq and ibf_load_builtin do. */
static int64_t yarv_int(uint64_t value)
{
    return xxbyte_sext(value, 32);
}

/* The slot of the instruction at address in the context's slot map.
 * Addresses wrap like the caller's base + offset. */
static int yarv_context_slot(const xxbyte_yarv_context *context, uint64_t address, uint64_t *slot)
{
    uint64_t offset = address - context->base;
    size_t low = 0;
    size_t high;

    if (context->offsets == NULL || offset > UINT32_MAX) {
        return 0;
    }
    high = context->slot_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (context->offsets[middle] < offset) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low >= context->slot_count || context->offsets[low] != offset) {
        return 0;
    }
    *slot = low;
    return 1;
}

/* The byte address of the branch target slot: the instruction starting at
 * slot; for a slot outside the map, when the context gives a code_size
 * that covers it, base + code_size + (slot - slot_count) past the end or
 * base + slot before the start. A slot inside an instruction fails. */
static int yarv_context_target(const xxbyte_yarv_context *context, int64_t slot, int64_t *target)
{
    size_t index;

    if (slot < 0 || (uint64_t)slot >= (uint64_t)context->slot_count) {
        if (context->slot_count == 0 || (uint64_t)context->code_size <= context->offsets[context->slot_count - 1]) {
            return 0;
        }
        if (slot < 0) {
            *target = (int64_t)(context->base + (uint64_t)slot);
        } else {
            *target = (int64_t)(context->base + (uint64_t)context->code_size + ((uint64_t)slot - (uint64_t)context->slot_count));
        }
        return 1;
    }
    index = (size_t)slot;
    if (index != 0 && context->offsets[index - 1] == context->offsets[index]) {
        return 0;
    }
    *target = (int64_t)(context->base + context->offsets[index]);
    return 1;
}

static void yarv_add_index(xxbyte_instruction *instruction, uint32_t kind, uint32_t size, int64_t value, uint64_t extra, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_index(instruction, (xxbyte_index_kind)kind, size, value);
    if (operand != NULL) {
        operand->extra = extra;
        operand->flags = flags;
    }
}

static void yarv_add_immediate(xxbyte_instruction *instruction, uint32_t size, int64_t value, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, size, value);
    if (operand != NULL) {
        operand->flags = flags;
    }
}

/* The index kind of an operand type that is a plain table index. */
static uint32_t yarv_index_kind(char type)
{
    switch (type) {
        case 'V': return XXBYTE_YARV_INDEX_OBJECT;
        case 'I':
        case 'G': return XXBYTE_YARV_INDEX_ID;
        case 'H': return XXBYTE_YARV_INDEX_CDHASH;
        case 'K': return XXBYTE_YARV_INDEX_IC;
        case 'A': return XXBYTE_YARV_INDEX_IVC;
        case 'J': return XXBYTE_YARV_INDEX_ICVARC;
        case 'T': return XXBYTE_YARV_INDEX_ISE;
        default: return 0;
    }
}

/* The level implied by the operand-unified local accessors, or -1. */
static int64_t yarv_unified_level(uint32_t base)
{
    switch (base) {
        case XXBYTE_YARV_ID_GETLOCAL_WC_0:
        case XXBYTE_YARV_ID_SETLOCAL_WC_0: return 0;
        case XXBYTE_YARV_ID_GETLOCAL_WC_1:
        case XXBYTE_YARV_ID_SETLOCAL_WC_1: return 1;
        default: return -1;
    }
}

/* Reads a builtin function: its index, then its name length and name. */
static xxbyte_status yarv_builtin(yarv_reader *reader, xxbyte_instruction *instruction, uint32_t size, uint64_t index)
{
    uint64_t name_length;
    size_t name_start;
    xxbyte_status status;

    yarv_add_index(instruction, XXBYTE_YARV_INDEX_BUILTIN, size, yarv_int(index), index, 0);
    status = yarv_read(reader, &name_length);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (name_length > (uint64_t)INT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    name_start = reader->pos;
    if ((uint64_t)(reader->size - reader->pos) < name_length) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    reader->pos += (size_t)name_length;
    if (reader->pos > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    xxbyte_add_operand(instruction, XXBYTE_OPERAND_BYTES, (uint32_t)name_length, (int64_t)name_start);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status yarv_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const yarv_version *version = yarv_find_version(mode);
    const xxbyte_yarv_context *map = NULL;
    const yarv_mnemonic *mnemonic;
    const yarv_op *op;
    const char *types;
    yarv_reader reader;
    xxbyte_status status;
    uint64_t number;
    uint64_t slot = 0;
    uint32_t length;
    uint32_t i;
    int64_t level;

    if (version == NULL) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    yarv_reader_init(&reader, mode, code, code_size);
    status = yarv_read(&reader, &number);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (number >= version->count) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    op = &version->ops[number];
    types = xxbyte_yarv_signatures[op->signature];
    length = 1u + (uint32_t)strlen(types);
    mnemonic = &xxbyte_yarv_mnemonics[op->id];
    if (!reader.raw && context != NULL) {
        map = (const xxbyte_yarv_context *)context;
        if (!yarv_context_slot(map, address, &slot)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    instruction->opcode = (uint32_t)number;
    instruction->id = op->id;
    instruction->group = mnemonic->group;
    instruction->mnemonic = mnemonic->name;

    for (i = 0; types[i] != '\0'; ++i) {
        size_t start = reader.pos;
        char type = types[i];
        uint32_t kind;
        uint32_t size;

        if (type == 'F' || (type == 'R' && reader.raw)) {
            /* The IBF dumper and loader reject function pointers. */
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (type == 'C' && !reader.raw) {
            if (YARV_VERSION(mode) >= YARV_MODE(3, 0)) {
                yarv_add_index(instruction, XXBYTE_YARV_INDEX_CALLDATA, 0, -1, 0, XXBYTE_OPERAND_FLAG_IMPLICIT);
                continue;
            }
            /* 2.7: one raw byte, nonzero for a keyword call. */
            if (reader.pos >= reader.size) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            yarv_add_index(instruction, XXBYTE_YARV_INDEX_CALLDATA, 1, -1, reader.code[reader.pos], 0);
            ++reader.pos;
            continue;
        }
        status = yarv_read(&reader, &number);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        size = (uint32_t)(reader.pos - start);
        kind = yarv_index_kind(type);
        if (kind != 0) {
            yarv_add_index(instruction, kind, size, (int64_t)number, number, 0);
            continue;
        }
        switch (type) {
            case 'O': {
                int64_t delta = yarv_signed(&reader, number);
                int64_t target;
                xxbyte_operand *operand;
                if (reader.raw) {
                    target = (int64_t)(address + ((uint64_t)length + (uint64_t)delta) * (reader.word32 ? 4u : 8u));
                } else if (map != NULL) {
                    if (!yarv_context_target(map, (int64_t)(slot + length + (uint64_t)delta), &target)) {
                        return XXBYTE_STATUS_INVALID_INSTRUCTION;
                    }
                } else {
                    target = (int64_t)(address + length + (uint64_t)delta);
                }
                operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, size, target);
                if (operand != NULL) {
                    operand->extra = number;
                }
                break;
            }
            case 'N':
                yarv_add_immediate(instruction, size, (int64_t)number, 0);
                if (i > 0 && types[i - 1] == 'L') {
                    /* The level of getlocal and its relatives. */
                    instruction->operands[instruction->operand_count - 2].extra = number;
                }
                break;
            case 'L':
                if (i > 0 && mnemonic->base == XXBYTE_YARV_ID_CHECKKEYWORD) {
                    /* keyword_index: a position in the keyword list. */
                    yarv_add_immediate(instruction, size, yarv_signed(&reader, number), XXBYTE_OPERAND_FLAG_SIGNED);
                } else {
                    /* A signed long, as #to_a shows it (prism emits negative indexes). */
                    yarv_add_index(instruction, XXBYTE_INDEX_LOCAL, size, yarv_signed(&reader, number), 0, 0);
                }
                break;
            case 'S': yarv_add_index(instruction, XXBYTE_INDEX_FUNCTION, size, yarv_int(number), number, 0); break;
            case 'C':
                /* 2.6 TS_CALLINFO: Qtrue for a keyword call, else Qfalse. */
                yarv_add_index(instruction, XXBYTE_YARV_INDEX_CALLDATA, size, -1, number, 0);
                break;
            case 'E': yarv_add_index(instruction, XXBYTE_YARV_INDEX_CALLCACHE, size, -1, number, 0); break;
            case 'R':
                status = yarv_builtin(&reader, instruction, size, number);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                break;
            default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }

    level = yarv_unified_level(mnemonic->base);
    if (level >= 0) {
        instruction->operands[0].extra = (uint64_t)level;
        yarv_add_immediate(instruction, 0, level, XXBYTE_OPERAND_FLAG_IMPLICIT);
    } else if (mnemonic->base == XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_ || mnemonic->base == XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_) {
        yarv_add_immediate(instruction, 0, mnemonic->base == XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_ ? 1 : 0, XXBYTE_OPERAND_FLAG_IMPLICIT | XXBYTE_OPERAND_FLAG_SIGNED);
    }
    if (reader.pos > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->size = (uint32_t)reader.pos;
    return XXBYTE_STATUS_OK;
}

/* Names of enum defined_type values for the first operand of defined. */
static const char *const yarv_defined_names[] = {
    NULL,   "nil",   "instance-variable", "local-variable", "global-variable", "class variable", "constant", "method", "yield", "super", "self",
    "true", "false", "assignment",        "expression",
};

static const char *yarv_defined_name(uint32_t mode, uint64_t type)
{
    /* Up to 3.0, DEFINED_IVAR2 (15) came before DEFINED_REF. */
    uint64_t ref = YARV_VERSION(mode) <= YARV_MODE(3, 0) ? 16u : 15u;

    if (type != 0 && type < sizeof(yarv_defined_names) / sizeof(yarv_defined_names[0])) {
        return yarv_defined_names[type];
    }
    if (type == ref) {
        return "ref";
    }
    if (type == ref + 1) {
        return "func";
    }
    if (type == ref + 2 && YARV_VERSION(mode) >= YARV_MODE(2, 7)) {
        return "constant-from";
    }
    return NULL;
}

/* rb_type_str: names of enum ruby_value_type for checktype. */
static const char *const yarv_type_names[] = {
    "T_NONE",  "T_OBJECT", "T_CLASS", "T_MODULE",  "T_FLOAT",    "T_STRING", "T_REGEXP", "T_ARRAY",  "T_HASH",  "T_STRUCT", "T_BIGNUM",
    "T_FILE",  "T_DATA",   "T_MATCH", "T_COMPLEX", "T_RATIONAL", NULL,       "T_NIL",    "T_TRUE",   "T_FALSE", "T_SYMBOL", "T_FIXNUM",
    "T_UNDEF", NULL,       NULL,      NULL,        "T_IMEMO",    "T_NODE",   "T_ICLASS", "T_ZOMBIE", "T_MOVED",
};

static const char *yarv_type_name(uint32_t mode, uint64_t type)
{
    if (type >= sizeof(yarv_type_names) / sizeof(yarv_type_names[0])) {
        return NULL;
    }
    if (type == 0x1Eu && YARV_VERSION(mode) < YARV_MODE(2, 7)) {
        return NULL;
    }
    return yarv_type_names[type];
}

static int yarv_format_name(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, const xxbyte_operand *name)
{
    uint64_t start = (uint64_t)name->value;
    uint32_t i;

    if (instruction->code == NULL || start > instruction->size || name->size > instruction->size - start) {
        return 0;
    }
    for (i = 0; i < name->size; ++i) {
        uint8_t c = instruction->code[start + i];
        if (c < 0x21 || c > 0x7E || c == '\\' || c == '(' || c == ')') {
            if (!xxbyte_fmt_printf(fmt, "\\x%02X", (unsigned)c)) {
                return 0;
            }
        } else if (!xxbyte_fmt_char(fmt, (char)c)) {
            return 0;
        }
    }
    return 1;
}

static int yarv_format_index(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t index)
{
    const xxbyte_operand *operand = &instruction->operands[index];
    unsigned long long value = (unsigned long long)(uint64_t)operand->value;

    switch (operand->kind) {
        case XXBYTE_INDEX_LOCAL: return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
        case XXBYTE_INDEX_FUNCTION:
            if (operand->value == -1) {
                return xxbyte_fmt_str(fmt, "nil");
            }
            return xxbyte_fmt_printf(fmt, "iseq@%lld", (long long)operand->value);
        case XXBYTE_YARV_INDEX_OBJECT: return xxbyte_fmt_printf(fmt, "obj@%llu", value);
        case XXBYTE_YARV_INDEX_ID: return xxbyte_fmt_printf(fmt, "id@%llu", value);
        case XXBYTE_YARV_INDEX_CDHASH: return xxbyte_fmt_printf(fmt, "cdhash@%llu", value);
        case XXBYTE_YARV_INDEX_IC: return xxbyte_fmt_printf(fmt, "ic@%llu", value);
        case XXBYTE_YARV_INDEX_IVC: return xxbyte_fmt_printf(fmt, "ivc@%llu", value);
        case XXBYTE_YARV_INDEX_ICVARC: return xxbyte_fmt_printf(fmt, "icvarc@%llu", value);
        case XXBYTE_YARV_INDEX_ISE: return xxbyte_fmt_printf(fmt, "ise@%llu", value);
        case XXBYTE_YARV_INDEX_CALLDATA:
            if (YARV_VERSION(instruction->mode) < YARV_MODE(2, 7)) {
                return xxbyte_fmt_str(fmt, operand->extra != 0 ? "<callinfo!kw>" : "<callinfo>");
            }
            return xxbyte_fmt_str(fmt, operand->extra != 0 ? "<calldata!kw>" : "<calldata>");
        case XXBYTE_YARV_INDEX_CALLCACHE: return xxbyte_fmt_str(fmt, "<callcache>");
        case XXBYTE_YARV_INDEX_BUILTIN:
            if (index + 1 >= instruction->operand_count || instruction->operands[index + 1].type != XXBYTE_OPERAND_BYTES) {
                return 0;
            }
            return xxbyte_fmt_printf(fmt, "builtin@%lld(", (long long)operand->value) && yarv_format_name(fmt, instruction, &instruction->operands[index + 1]) &&
                   xxbyte_fmt_char(fmt, ')');
        default: return 0;
    }
}

static int yarv_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t base;
    uint32_t i;
    int first = 1;

    if (instruction->id == 0 || instruction->id >= XXBYTE_YARV_ID_COUNT) {
        return 0;
    }
    base = xxbyte_yarv_mnemonics[instruction->id].base;
    for (i = 0; i < instruction->operand_count; ++i) {
        const xxbyte_operand *operand = &instruction->operands[i];
        int resolved;

        if (operand->type == XXBYTE_OPERAND_BYTES) {
            continue;
        }
        if ((operand->flags & XXBYTE_OPERAND_FLAG_IMPLICIT) != 0 && !(operand->type == XXBYTE_OPERAND_INDEX && operand->kind == XXBYTE_YARV_INDEX_CALLDATA)) {
            continue;
        }
        if (!xxbyte_fmt_str(fmt, first ? " " : ", ")) {
            return 0;
        }
        first = 0;
        switch (operand->type) {
            case XXBYTE_OPERAND_BRANCH:
                resolved = xxbyte_fmt_resolve(fmt, instruction, i);
                if (resolved < 0) {
                    return 0;
                }
                if (resolved == 0 && !xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value)) {
                    return 0;
                }
                break;
            case XXBYTE_OPERAND_IMMEDIATE: {
                uint64_t value = (uint64_t)operand->value;
                const char *name = NULL;
                if (i == 0 && base == XXBYTE_YARV_ID_DEFINED) {
                    name = yarv_defined_name(instruction->mode, value);
                } else if (i == 0 && base == XXBYTE_YARV_ID_CHECKTYPE) {
                    name = yarv_type_name(instruction->mode, value);
                }
                if (name != NULL) {
                    if (!xxbyte_fmt_str(fmt, name)) {
                        return 0;
                    }
                } else if (operand->flags & XXBYTE_OPERAND_FLAG_SIGNED) {
                    if (!xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value)) {
                        return 0;
                    }
                } else if (!xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)value)) {
                    return 0;
                }
                break;
            }
            case XXBYTE_OPERAND_INDEX:
                resolved = xxbyte_fmt_resolve(fmt, instruction, i);
                if (resolved < 0) {
                    return 0;
                }
                if (resolved == 0 && !yarv_format_index(fmt, instruction, i)) {
                    return 0;
                }
                break;
            default: return 0;
        }
    }
    return 1;
}

static xxbyte_status yarv_check_mode(uint32_t mode)
{
    return yarv_find_version(mode) != NULL ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static const yarv_op *yarv_find_op(uint32_t mode, uint32_t opcode)
{
    const yarv_version *version = yarv_find_version(mode);

    if (version == NULL || opcode >= version->count) {
        return NULL;
    }
    return &version->ops[opcode];
}

const char *xxbyte_yarv_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_YARV_ID_COUNT) {
        return NULL;
    }
    return xxbyte_yarv_mnemonics[id].name;
}

const char *xxbyte_yarv_insn_name(uint32_t mode, uint32_t opcode)
{
    const yarv_op *op = yarv_find_op(mode, opcode);
    return op != NULL ? xxbyte_yarv_mnemonics[op->id].name : NULL;
}

uint32_t xxbyte_yarv_insn_len(uint32_t mode, uint32_t opcode)
{
    const yarv_op *op = yarv_find_op(mode, opcode);

    if (op == NULL) {
        return 0;
    }
    return 1u + (uint32_t)strlen(xxbyte_yarv_signatures[op->signature]);
}

const char *xxbyte_yarv_operand_types(uint32_t mode, uint32_t opcode)
{
    const yarv_op *op = yarv_find_op(mode, opcode);
    return op != NULL ? xxbyte_yarv_signatures[op->signature] : NULL;
}

xxbyte_status xxbyte_yarv_build_slot_map(uint32_t mode, const uint8_t *code, size_t code_size, uint32_t *offsets, size_t capacity, size_t *slot_count)
{
    size_t pos = 0;
    size_t slots = 0;

    if (slot_count == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *slot_count = 0;
    if ((code == NULL && code_size != 0) || (offsets == NULL && capacity != 0) || (uint64_t)code_size > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (yarv_find_version(mode) == NULL) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    while (pos < code_size) {
        xxbyte_instruction instruction;
        xxbyte_status status;
        uint32_t length;
        uint32_t k;

        memset(&instruction, 0, sizeof(instruction));
        instruction.family = XXBYTE_FAMILY_YARV;
        instruction.mode = mode;
        instruction.address = pos;
        instruction.code = code + pos;
        status = yarv_decode(mode, NULL, code + pos, code_size - pos, slots, &instruction);
        if (status != XXBYTE_STATUS_OK) {
            *slot_count = slots;
            return status;
        }
        length = xxbyte_yarv_insn_len(mode, instruction.opcode);
        for (k = 0; k < length; ++k) {
            if (slots + k < capacity) {
                offsets[slots + k] = (uint32_t)pos;
            }
        }
        slots += length;
        pos += instruction.size;
    }
    *slot_count = slots;
    return XXBYTE_STATUS_OK;
}

/* IBF images. */

static uint32_t yarv_u32(const uint8_t *p, int big)
{
    return big ? xxbyte_u32be(p) : xxbyte_u32le(p);
}

/* The CPU part platform[0..cpu) is name, or starts with it if prefix. */
static int yarv_platform_cpu_is(const uint8_t *platform, size_t cpu, const char *name, int prefix)
{
    size_t length = strlen(name);
    return (cpu == length || (prefix && cpu > length)) && memcmp(platform, name, length) == 0;
}

/* 8 for a 64-bit RUBY_PLATFORM, else 4 (a heuristic): 4 for an x32 ABI
 * (the OS part ends in "x32", as in x86_64-linux-gnux32); otherwise 8 when
 * the CPU part, up to the first '-', contains "64", starts with
 * "universal" (a macOS fat build), "alpha" or "e2k", or is s390x or
 * sparcv9. */
static uint8_t yarv_platform_word_size(const uint8_t *platform, size_t length)
{
    size_t cpu = 0;
    size_t i;

    while (cpu < length && platform[cpu] != '-') {
        ++cpu;
    }
    if (cpu < length && length - cpu >= 4 && memcmp(platform + length - 3, "x32", 3) == 0) {
        return 4;
    }
    for (i = 0; i + 1 < cpu; ++i) {
        if (platform[i] == '6' && platform[i + 1] == '4') {
            return 8;
        }
    }
    if (yarv_platform_cpu_is(platform, cpu, "universal", 1) || yarv_platform_cpu_is(platform, cpu, "alpha", 1) || yarv_platform_cpu_is(platform, cpu, "e2k", 1) ||
        yarv_platform_cpu_is(platform, cpu, "s390x", 0) || yarv_platform_cpu_is(platform, cpu, "sparcv9", 0)) {
        return 8;
    }
    return 4;
}

static int yarv_platform_is_windows(const uint8_t *platform, size_t length)
{
    size_t i;

    for (i = 0; i + 5 <= length; ++i) {
        if (memcmp(platform + i, "mingw", 5) == 0 || memcmp(platform + i, "mswin", 5) == 0) {
            return 1;
        }
    }
    return 0;
}

/* A list of count entries of width bytes at offset lies within size and
 * is 4-byte aligned. */
static int yarv_list_fits(uint32_t offset, uint32_t count, uint32_t width, uint32_t size)
{
    return offset % 4u == 0 && (uint64_t)offset + (uint64_t)count * width <= size;
}

xxbyte_status xxbyte_yarv_parse_ibf_header(const uint8_t *data, size_t size, xxbyte_yarv_ibf_header *header)
{
    xxbyte_yarv_ibf_header parsed;
    uint32_t release_minor;
    uint32_t version;
    uint32_t le;
    int big;

    if (header == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(header, 0, sizeof(*header));
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < 12) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (memcmp(data, "YARB", 4) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    memset(&parsed, 0, sizeof(parsed));
    le = xxbyte_u32le(data + 4);
    if (le != 0 && le < 256) {
        big = 0;
    } else if (xxbyte_u32be(data + 4) != 0 && xxbyte_u32be(data + 4) < 256) {
        big = 1;
    } else {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    parsed.endian = big ? (uint8_t)'b' : (uint8_t)'l';
    parsed.major_version = yarv_u32(data + 4, big);
    parsed.minor_version = yarv_u32(data + 8, big);
    release_minor = parsed.minor_version;
    if (release_minor >= 10000) {
        parsed.development = 1;
        release_minor /= 10000;
    }
    version = release_minor < 256 ? YARV_MODE(parsed.major_version, release_minor) : 0;
    if (version == 0 || yarv_find_version(version) == NULL) {
        header->major_version = parsed.major_version;
        header->minor_version = parsed.minor_version;
        header->endian = parsed.endian;
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if (version == YARV_MODE(2, 6)) {
        parsed.header_size = 44;
    } else if (version < YARV_MODE(3, 3)) {
        parsed.header_size = 36;
    } else {
        parsed.header_size = 40;
    }
    if (size < parsed.header_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    parsed.size = yarv_u32(data + 12, big);
    parsed.extra_size = yarv_u32(data + 16, big);
    parsed.iseq_list_size = yarv_u32(data + 20, big);
    if (version == YARV_MODE(2, 6)) {
        parsed.id_list_size = yarv_u32(data + 24, big);
        parsed.object_list_size = yarv_u32(data + 28, big);
        parsed.iseq_list_offset = yarv_u32(data + 32, big);
        parsed.id_list_offset = yarv_u32(data + 36, big);
        parsed.object_list_offset = yarv_u32(data + 40, big);
    } else {
        parsed.object_list_size = yarv_u32(data + 24, big);
        parsed.iseq_list_offset = yarv_u32(data + 28, big);
        parsed.object_list_offset = yarv_u32(data + 32, big);
    }
    if (parsed.size > size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (parsed.size < parsed.header_size) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (version >= YARV_MODE(3, 3)) {
        if (data[36] != parsed.endian || (data[37] != 4 && data[37] != 8)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        parsed.word_size = data[37];
    } else {
        const uint8_t *platform = data + parsed.header_size;
        const uint8_t *end = (const uint8_t *)memchr(platform, 0, parsed.size - parsed.header_size);
        if (end == NULL) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        parsed.platform_offset = parsed.header_size;
        parsed.platform_size = (uint32_t)(end - platform);
        parsed.word_size = yarv_platform_word_size(platform, parsed.platform_size);
    }
    if (!yarv_list_fits(parsed.iseq_list_offset, parsed.iseq_list_size, 4, parsed.size) ||
        !yarv_list_fits(parsed.object_list_offset, parsed.object_list_size, 4, parsed.size)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (version == YARV_MODE(2, 6)) {
        /* A C long per ID: 4 bytes on 64-bit Windows too. */
        uint32_t width = parsed.word_size;
        if (yarv_platform_is_windows(data + parsed.platform_offset, parsed.platform_size)) {
            width = 4;
        }
        if (!yarv_list_fits(parsed.id_list_offset, parsed.id_list_size, width, parsed.size)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    parsed.mode = version;
    if (parsed.word_size == 4) {
        parsed.mode |= XXBYTE_YARV_MODE_WORD32;
    }
    if (big) {
        parsed.mode |= XXBYTE_YARV_MODE_BIG_ENDIAN;
    }
    *header = parsed;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_yarv_locate_iseq(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint32_t index, xxbyte_yarv_iseq *iseq)
{
    xxbyte_yarv_iseq located;
    yarv_reader reader;
    uint64_t entry;
    uint64_t fields[4];
    size_t image_size;
    int big;
    unsigned i;

    if (iseq == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(iseq, 0, sizeof(*iseq));
    if (header == NULL || (data == NULL && size != 0) || yarv_find_version(header->mode) == NULL || index >= header->iseq_list_size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    big = (header->mode & XXBYTE_YARV_MODE_BIG_ENDIAN) != 0;
    image_size = header->size < size ? header->size : size;
    entry = (uint64_t)header->iseq_list_offset + 4u * (uint64_t)index;
    if (entry + 4u > image_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    memset(&located, 0, sizeof(located));
    located.body_offset = yarv_u32(data + entry, big);
    if (YARV_VERSION(header->mode) < YARV_MODE(2, 7)) {
        /* The dumped rb_iseq_constant_body: type, iseq_size and the
         * iseq_encoded offset as the first three members. */
        unsigned width = (header->mode & XXBYTE_YARV_MODE_WORD32) != 0 ? 4u : 8u;
        uint64_t encoded;
        uint64_t code_size;
        if ((uint64_t)located.body_offset + 8u + width > image_size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        located.type = yarv_u32(data + located.body_offset, big);
        located.iseq_size = yarv_u32(data + located.body_offset + 4, big);
        encoded = yarv_word(data + located.body_offset + 8, width, big);
        code_size = (uint64_t)located.iseq_size * width;
        if (encoded > UINT32_MAX || code_size > UINT32_MAX) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (encoded + code_size > image_size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        located.code_offset = (uint32_t)encoded;
        located.code_size = (uint32_t)code_size;
        *iseq = located;
        return XXBYTE_STATUS_OK;
    }
    /* type, iseq_size, body offset - bytecode offset, bytecode size. */
    yarv_reader_init(&reader, header->mode, data, image_size);
    if (located.body_offset > image_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    reader.pos = located.body_offset;
    for (i = 0; i < 4; ++i) {
        xxbyte_status status = yarv_read(&reader, &fields[i]);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (fields[i] > UINT32_MAX) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    if (fields[2] > located.body_offset) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    located.type = (uint32_t)fields[0];
    located.iseq_size = (uint32_t)fields[1];
    located.code_offset = located.body_offset - (uint32_t)fields[2];
    located.code_size = (uint32_t)fields[3];
    if ((uint64_t)located.code_offset + located.code_size > image_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *iseq = located;
    return XXBYTE_STATUS_OK;
}

/* Ruby 2.6 code is raw VALUE words; later versions are byte-packed. */
static size_t yarv_unit_size(uint32_t mode)
{
    if (XXBYTE_YARV_MODE_MAJOR(mode) == 2 && XXBYTE_YARV_MODE_MINOR(mode) == 6) {
        return (mode & XXBYTE_YARV_MODE_WORD32) != 0 ? 4u : 8u;
    }
    return 1;
}

const xxbyte_family_ops xxbyte_yarv_ops = {
    XXBYTE_FAMILY_YARV, XXBYTE_YARV_MODE_DEFAULT, yarv_check_mode, yarv_decode, NULL, yarv_format_operands, yarv_unit_size, yarv_list_file,
};

#else
typedef int xxbyte_yarv_disabled;
#endif
