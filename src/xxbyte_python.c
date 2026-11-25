#include "xxbyte_python_tables.h"
#include "xxbyte_python_marshal.h"

#if XXBYTE_USE_PYTHON

#define PY_MODE(major, minor) XXBYTE_PYTHON_MODE(major, minor)

static const char *const python_conversions[] = {"", "str", "repr", "ascii"};
static const python_strings python_conversion_names = {python_conversions, 4};

static const struct {
    uint16_t first;
    uint16_t last;
    uint16_t mode;
} python_magics[] = {
    /* 2.x: the PC/launcher.c ranges plus the "-U" magic + 1. */
    {50823, 50824, PY_MODE(2, 0)}, {60202, 60203, PY_MODE(2, 1)}, {60717, 60718, PY_MODE(2, 2)}, {62011, 62022, PY_MODE(2, 3)}, {62041, 62062, PY_MODE(2, 4)},
    {62071, 62132, PY_MODE(2, 5)}, {62151, 62162, PY_MODE(2, 6)}, {62171, 62212, PY_MODE(2, 7)}, {3000, 3131, PY_MODE(3, 0)},   {3141, 3151, PY_MODE(3, 1)},
    {3160, 3180, PY_MODE(3, 2)},   {3190, 3230, PY_MODE(3, 3)},   {3250, 3310, PY_MODE(3, 4)},   {3320, 3351, PY_MODE(3, 5)},   {3360, 3379, PY_MODE(3, 6)},
    {3390, 3399, PY_MODE(3, 7)},   {3400, 3419, PY_MODE(3, 8)},   {3420, 3429, PY_MODE(3, 9)},   {3430, 3449, PY_MODE(3, 10)},  {3450, 3499, PY_MODE(3, 11)},
    {3500, 3549, PY_MODE(3, 12)},  {3550, 3599, PY_MODE(3, 13)},  {3600, 3649, PY_MODE(3, 14)},
};

static const python_version *python_find_version(uint32_t mode)
{
    uint32_t major = XXBYTE_PYTHON_MODE_MAJOR(mode);
    uint32_t minor = XXBYTE_PYTHON_MODE_MINOR(mode);
    size_t index;

    if ((mode & ~(uint32_t)0xFFFFu) != 0) {
        return NULL;
    }
    if (major == 2) {
        index = minor;
    } else if (major == 3) {
        index = 8u + minor;
    } else {
        return NULL;
    }
    if (index >= xxbyte_python_version_count || xxbyte_python_versions[index].mode != mode) {
        return NULL;
    }
    return &xxbyte_python_versions[index];
}

/* Python's floor division by 2**bits. */
static int64_t python_floor_shift(int64_t value, unsigned bits)
{
    int64_t divisor = (int64_t)1 << bits;

    if (value >= 0) {
        return value / divisor;
    }
    return -((-value + divisor - 1) / divisor);
}

/* items[index] with Python's negative indexing; NULL when out of range. */
static const char *python_item(const python_strings *strings, int64_t index)
{
    int64_t count = (int64_t)strings->count;

    if (index < 0) {
        index += count;
    }
    if (index < 0 || index >= count) {
        return NULL;
    }
    return strings->items[index];
}

static xxbyte_status python_check_mode(uint32_t mode)
{
    return python_find_version(mode) != NULL ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

/* The absolute target of a jump whose last opcode is at here. */
static int64_t python_target(uint32_t mode, uint8_t kind, uint64_t here, uint64_t raw, int64_t arg, unsigned caches)
{
    uint64_t base;
    uint64_t delta;

    if (kind == PY_JABS) {
        return (int64_t)(mode >= PY_MODE(3, 10) ? raw * 2u : raw);
    }
    if (mode < PY_MODE(3, 6)) {
        return (int64_t)(here + 3u + raw);
    }
    if (mode < PY_MODE(3, 10)) {
        return (int64_t)(here + 2u + raw);
    }
    if (mode < PY_MODE(3, 11)) {
        return (int64_t)(here + 2u + raw * 2u);
    }
    base = here + 2u;
    if (mode >= PY_MODE(3, 12)) {
        base += 2u * (uint64_t)caches;
    }
    delta = (uint64_t)arg * 2u;
    return (int64_t)(kind == PY_JREL ? base + delta : base - delta);
}

static xxbyte_status python_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const python_version *version = python_find_version(mode);
    const python_op *op;
    int wordcode = mode >= PY_MODE(3, 6);
    size_t pos = 0;
    size_t width;
    uint64_t raw = 0;
    uint64_t total;
    uint32_t opcode = 0;
    uint32_t prefixes = 0;
    uint32_t arg_size;
    int64_t arg;
    xxbyte_operand *operand = NULL;

    (void)context;
    if (version == NULL) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    for (;;) {
        if (pos >= code_size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        op = &version->ops[code[pos]];
        if (op->id == 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        /* Bounding the chain bounds the work of every decode, so a sweep
         * that resynchronizes inside a long run of prefixes stays linear. */
        if (op->kind == PY_EXT && prefixes == XXBYTE_PYTHON_MAX_EXTENDED_ARGS) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (wordcode) {
            width = 2;
        } else {
            width = op->kind != PY_NONE ? 3u : 1u;
        }
        if (code_size - pos < width) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        /* The last four opcode bytes: prefixes end up in the high bytes. */
        opcode = (opcode << 8) | code[pos];
        if (op->kind != PY_NONE) {
            if (wordcode) {
                raw = (raw << 8) | code[pos + 1];
            } else {
                raw = (raw << 16) | xxbyte_u16le(code + pos + 1);
            }
        }
        if (op->kind != PY_EXT) {
            break;
        }
        pos += width;
        ++prefixes;
    }
    total = (uint64_t)pos + width + 2u * (uint64_t)op->caches;
    if (total > code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (total > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (mode >= PY_MODE(3, 11)) {
        raw &= 0xFFFFFFFFu;
        arg = xxbyte_sext(raw, 32);
    } else {
        arg = (int64_t)raw;
    }

    instruction->size = (uint32_t)total;
    instruction->opcode = opcode;
    instruction->id = op->id;
    instruction->group = xxbyte_python_mnemonics[op->id].group;
    instruction->mnemonic = xxbyte_python_mnemonics[op->id].name;
    if (op->caches != 0) {
        instruction->table_count = op->caches;
        instruction->table_offset = (uint32_t)(pos + width);
    }
    if (op->kind == PY_NONE) {
        return XXBYTE_STATUS_OK;
    }

    arg_size = (prefixes + 1u) * (wordcode ? 1u : 2u);
    switch (op->kind) {
        case PY_CONST: operand = xxbyte_add_index(instruction, XXBYTE_INDEX_CONSTANT, arg_size, arg); break;
        case PY_NAME: operand = xxbyte_add_index(instruction, XXBYTE_INDEX_NAME, arg_size, arg); break;
        case PY_GLOBAL:
        case PY_ATTR: operand = xxbyte_add_index(instruction, XXBYTE_INDEX_NAME, arg_size, python_floor_shift(arg, 1)); break;
        case PY_SUPER: operand = xxbyte_add_index(instruction, XXBYTE_INDEX_NAME, arg_size, python_floor_shift(arg, 2)); break;
        case PY_LOCAL: operand = xxbyte_add_index(instruction, XXBYTE_INDEX_LOCAL, arg_size, arg); break;
        case PY_FREE: operand = xxbyte_add_index(instruction, XXBYTE_INDEX_FREE, arg_size, arg); break;
        case PY_PAIR:
            operand = xxbyte_add_index(instruction, XXBYTE_INDEX_LOCAL, arg_size, python_floor_shift(arg, 4));
            if (operand != NULL) {
                xxbyte_operand *second = xxbyte_add_index(instruction, XXBYTE_INDEX_LOCAL, arg_size, (int64_t)(raw & 15u));
                if (second != NULL) {
                    second->extra = raw;
                }
            }
            break;
        case PY_JREL:
        case PY_JBACK:
        case PY_JFROM:
        case PY_JABS:
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, arg_size, python_target(mode, op->kind, address + pos, raw, arg, op->caches));
            break;
        default:
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, arg_size, arg);
            if (operand != NULL && mode >= PY_MODE(3, 11)) {
                operand->flags |= XXBYTE_OPERAND_FLAG_SIGNED;
            }
            break;
    }
    if (operand != NULL) {
        operand->extra = raw;
    }
    return XXBYTE_STATUS_OK;
}

/* Appends " (<prefix><name><suffix>)" when the resolver names the operand
 * and nothing otherwise. */
static int python_name(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, const char *prefix, const char *suffix)
{
    size_t mark = xxbyte_fmt_mark(fmt);
    int resolved;

    if (fmt->resolver == NULL) {
        return 1;
    }
    if (!xxbyte_fmt_str(fmt, " (") || !xxbyte_fmt_str(fmt, prefix)) {
        return 0;
    }
    resolved = xxbyte_fmt_resolve(fmt, instruction, 0);
    if (resolved < 0) {
        return 0;
    }
    /* dis before 3.4 prints "()" for an empty name (IMPORT_NAME of a
     * relative import); a resolver can not return empty text, so the
     * container resolver is asked directly. */
    if (resolved == 0 && !(fmt->resolver == xxbyte_python_resolver && instruction->mode < PY_MODE(3, 4) && xxbyte_python_empty_name(fmt->context, instruction))) {
        xxbyte_fmt_rewind(fmt, mark);
        return 1;
    }
    return xxbyte_fmt_str(fmt, suffix) && xxbyte_fmt_char(fmt, ')');
}

/* Appends " (a, b)" for the two locals of a 3.13+ superinstruction. */
static int python_pair(xxbyte_fmt *fmt, const xxbyte_instruction *instruction)
{
    size_t mark = xxbyte_fmt_mark(fmt);
    int named = 0;
    uint32_t i;

    if (fmt->resolver == NULL || instruction->operand_count != 2) {
        return 1;
    }
    if (!xxbyte_fmt_str(fmt, " (")) {
        return 0;
    }
    for (i = 0; i < 2; ++i) {
        int resolved;
        if (i != 0 && !xxbyte_fmt_str(fmt, ", ")) {
            return 0;
        }
        resolved = xxbyte_fmt_resolve(fmt, instruction, i);
        if (resolved < 0) {
            return 0;
        }
        if (resolved > 0) {
            named = 1;
        } else if (!xxbyte_fmt_printf(fmt, "%lld", (long long)instruction->operands[i].value)) {
            return 0;
        }
    }
    if (!named) {
        xxbyte_fmt_rewind(fmt, mark);
        return 1;
    }
    return xxbyte_fmt_char(fmt, ')');
}

static int python_branch(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, const char *preposition)
{
    int resolved;

    if (!xxbyte_fmt_printf(fmt, " (%s ", preposition)) {
        return 0;
    }
    resolved = xxbyte_fmt_resolve(fmt, instruction, 0);
    if (resolved < 0) {
        return 0;
    }
    if (resolved == 0 && !xxbyte_fmt_printf(fmt, "%lld", (long long)instruction->operands[0].value)) {
        return 0;
    }
    return xxbyte_fmt_char(fmt, ')');
}

static int python_text(xxbyte_fmt *fmt, const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return 1;
    }
    return xxbyte_fmt_printf(fmt, " (%s)", text);
}

static int python_flags(xxbyte_fmt *fmt, const python_strings *names, uint64_t raw)
{
    uint32_t i;
    int first = 1;

    for (i = 0; i < names->count; ++i) {
        if ((raw & ((uint64_t)1 << i)) == 0) {
            continue;
        }
        if (!xxbyte_fmt_str(fmt, first ? " (" : ", ") || !xxbyte_fmt_str(fmt, names->items[i])) {
            return 0;
        }
        first = 0;
    }
    return first ? 1 : xxbyte_fmt_char(fmt, ')');
}

static int python_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const python_version *version = python_find_version(instruction->mode);
    const python_op *op;
    const xxbyte_operand *operand = &instruction->operands[0];
    uint32_t mode = instruction->mode;
    uint64_t raw;
    int64_t arg;
    int64_t index;

    if (version == NULL) {
        return 0;
    }
    op = &version->ops[instruction->opcode & 0xFFu];
    if (op->id == 0 || op->id != instruction->id) {
        return 0;
    }
    if (op->kind == PY_NONE || op->kind == PY_EXT) {
        return 1;
    }
    if (instruction->operand_count == 0) {
        return 0;
    }
    raw = operand->type == XXBYTE_OPERAND_IMMEDIATE ? (uint64_t)operand->value : operand->extra;
    if (mode >= PY_MODE(3, 11)) {
        arg = xxbyte_sext(raw, 32);
        if (!xxbyte_fmt_printf(fmt, " %lld", (long long)arg)) {
            return 0;
        }
    } else {
        /* Older dis versions see an unbounded non-negative integer. */
        arg = raw > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)raw;
        if (!xxbyte_fmt_printf(fmt, " %llu", (unsigned long long)raw)) {
            return 0;
        }
    }

    switch (op->kind) {
        case PY_CONST:
            /* 3.11 dis looks up co_consts for LOAD_CONST only. */
            if (mode == PY_MODE(3, 11) && instruction->id == XXBYTE_PYTHON_ID_KW_NAMES) {
                return 1;
            }
            return python_name(fmt, instruction, "", "");
        case PY_NAME:
        case PY_LOCAL:
        case PY_FREE: return python_name(fmt, instruction, "", "");
        case PY_GLOBAL:
            if ((raw & 1u) == 0) {
                return python_name(fmt, instruction, "", "");
            }
            if (mode >= PY_MODE(3, 13)) {
                return python_name(fmt, instruction, "", " + NULL");
            }
            return python_name(fmt, instruction, "NULL + ", "");
        case PY_ATTR:
        case PY_SUPER:
            if ((raw & 1u) == 0) {
                return python_name(fmt, instruction, "", "");
            }
            if (mode >= PY_MODE(3, 13)) {
                return python_name(fmt, instruction, "", " + NULL|self");
            }
            return python_name(fmt, instruction, "NULL|self + ", "");
        case PY_PAIR: return python_pair(fmt, instruction);
        case PY_JREL:
        case PY_JBACK:
        case PY_JABS: return python_branch(fmt, instruction, "to");
        case PY_JFROM: return python_branch(fmt, instruction, "from");
        case PY_COMPARE: {
            const char *name;
            index = arg;
            if (mode >= PY_MODE(3, 13)) {
                index = python_floor_shift(arg, 5);
            } else if (mode >= PY_MODE(3, 12)) {
                index = python_floor_shift(arg, 4);
            }
            name = python_item(&version->cmp_op, index);
            if (name == NULL) {
                return 1;
            }
            if (mode >= PY_MODE(3, 13) && (raw & 16u) != 0) {
                return xxbyte_fmt_printf(fmt, " (bool(%s))", name);
            }
            return python_text(fmt, name);
        }
        case PY_NARGS:
            /* dis shows the two argument bytes of the last opcode, which are
             * the low 16 bits of the combined argument. */
            return xxbyte_fmt_printf(fmt, " (%u positional, %u keyword pair)", (unsigned)(raw & 0xFFu), (unsigned)((raw >> 8) & 0xFFu));
        case PY_FORMAT:
            if ((raw & 4u) != 0) {
                if ((raw & 3u) == 0) {
                    return xxbyte_fmt_str(fmt, " (with format)");
                }
                return xxbyte_fmt_printf(fmt, " (%s, with format)", python_conversions[raw & 3u]);
            }
            return python_text(fmt, python_conversions[raw & 3u]);
        case PY_CONVERT: return python_text(fmt, python_item(&python_conversion_names, arg));
        case PY_FLAGS: return python_flags(fmt, &version->function_flags, raw);
        case PY_BINARY: return python_text(fmt, python_item(&version->nb_ops, arg));
        case PY_INTRINSIC1: return python_text(fmt, python_item(&version->intrinsic1, arg));
        case PY_INTRINSIC2: return python_text(fmt, python_item(&version->intrinsic2, arg));
        case PY_COMMON: return python_text(fmt, python_item(&version->common, arg));
        case PY_SPECIAL: return python_text(fmt, python_item(&version->special, arg));
        case PY_IS: return python_text(fmt, arg != 0 ? "is not" : "is");
        case PY_CONTAINS: return python_text(fmt, arg != 0 ? "not in" : "in");
        default: return 1;
    }
}

uint32_t xxbyte_python_mode_from_magic(uint16_t magic)
{
    size_t i;

    for (i = 0; i < sizeof(python_magics) / sizeof(python_magics[0]); ++i) {
        if (magic >= python_magics[i].first && magic <= python_magics[i].last) {
            return python_magics[i].mode;
        }
    }
    return 0;
}

const char *xxbyte_python_opname(uint32_t mode, uint8_t opcode)
{
    const python_version *version = python_find_version(mode);

    if (version == NULL || version->ops[opcode].id == 0) {
        return NULL;
    }
    return xxbyte_python_mnemonics[version->ops[opcode].id].name;
}

xxbyte_status xxbyte_python_parse_pyc_header(const uint8_t *data, size_t size, xxbyte_python_pyc_header *header)
{
    xxbyte_python_pyc_header parsed;

    if (header == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(header, 0, sizeof(*header));
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (data[2] != '\r' || data[3] != '\n') {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    memset(&parsed, 0, sizeof(parsed));
    parsed.magic = xxbyte_u16le(data);
    parsed.mode = xxbyte_python_mode_from_magic(parsed.magic);
    if (parsed.mode == 0) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    /* 3.3a1 (3210) added the source size, 3.7a4 (3392) the PEP 552 flags. */
    if (parsed.mode < PY_MODE(3, 0) || parsed.magic < 3210) {
        parsed.header_size = 8;
    } else if (parsed.magic < 3392) {
        parsed.header_size = 12;
    } else {
        parsed.header_size = 16;
    }
    if (size < parsed.header_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (parsed.header_size == 16) {
        parsed.flags = xxbyte_u32le(data + 4);
        if ((parsed.flags & ~(uint32_t)(XXBYTE_PYTHON_PYC_HASH_BASED | XXBYTE_PYTHON_PYC_CHECK_SOURCE)) != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if ((parsed.flags & XXBYTE_PYTHON_PYC_HASH_BASED) != 0) {
            parsed.source_hash = xxbyte_u64le(data + 8);
        } else {
            parsed.mtime = xxbyte_u32le(data + 8);
            parsed.source_size = xxbyte_u32le(data + 12);
        }
    } else {
        parsed.mtime = xxbyte_u32le(data + 4);
        if (parsed.header_size == 12) {
            parsed.source_size = xxbyte_u32le(data + 8);
        }
    }
    *header = parsed;
    return XXBYTE_STATUS_OK;
}

/* The last magic number of mode, which has the release's marshal layout. */
uint32_t xxbyte_python_release_magic(uint32_t mode)
{
    size_t i;

    for (i = 0; i < sizeof(python_magics) / sizeof(python_magics[0]); ++i) {
        if (python_magics[i].mode == mode) {
            return python_magics[i].last;
        }
    }
    return 0;
}

static xxbyte_status python_find_code(uint32_t mode, uint32_t magic, const uint8_t *data, size_t size, uint32_t *code_offset, uint32_t *code_size)
{
    size_t pos;
    size_t fields = 4;
    size_t field_size = 4;
    uint32_t length;
    uint8_t type;

    if (code_offset == NULL || code_size == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *code_offset = 0;
    *code_size = 0;
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (python_find_version(mode) == NULL || magic == 0) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if (size == 0) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    /* TYPE_CODE, possibly with FLAG_REF (0x80, marshal version 3). */
    if ((data[0] & 0x7Fu) != 'c') {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    /* The integer fields before co_code: argcount, [posonlyargcount,]
     * [kwonlyargcount,] [nlocals,] stacksize, flags. Before 2.3 (62011)
     * they are 16-bit. kwonlyargcount came with magic 3030, posonlyargcount
     * with 3410 (3.8a1) and nlocals went with 3452 (3.11a1). */
    if (mode < PY_MODE(2, 3)) {
        field_size = 2;
    } else if (mode >= PY_MODE(3, 0)) {
        if (magic >= 3030) {
            ++fields;
        }
        if (magic >= 3410) {
            ++fields;
        }
        if (magic >= 3452) {
            --fields;
        }
    }
    pos = 1 + fields * field_size;
    if (size - 1 < fields * field_size + 5) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    type = (uint8_t)(data[pos] & 0x7Fu);
    if (type != 's' && !(mode < PY_MODE(3, 0) && type == 't')) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    length = xxbyte_u32le(data + pos + 1);
    if (length > (uint32_t)INT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    pos += 5;
    if ((uint64_t)length > (uint64_t)(size - pos)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *code_offset = (uint32_t)pos;
    *code_size = length;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_python_find_code(uint32_t mode, const uint8_t *data, size_t size, uint32_t *code_offset, uint32_t *code_size)
{
    return python_find_code(mode, xxbyte_python_release_magic(mode), data, size, code_offset, code_size);
}

xxbyte_status xxbyte_python_find_code_magic(uint16_t magic, const uint8_t *data, size_t size, uint32_t *code_offset, uint32_t *code_size)
{
    return python_find_code(xxbyte_python_mode_from_magic(magic), magic, data, size, code_offset, code_size);
}

const xxbyte_family_ops xxbyte_python_ops = {
    XXBYTE_FAMILY_PYTHON, XXBYTE_PYTHON_MODE_DEFAULT, python_check_mode, python_decode, NULL, python_format_operands, NULL, xxbyte_python_list_file,
};

#else
typedef int xxbyte_python_disabled;
#endif
