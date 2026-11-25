#include "xxbyte_v8_internal.h"

#if XXBYTE_USE_V8

#define V8_PREFIX_WIDE 2u
#define V8_PREFIX_EXTRA_WIDE 4u

static xxbyte_status v8_check_mode(uint32_t mode)
{
    return v8_find_version(mode) != NULL ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static const v8_shape *v8_shape_of(const v8_version *version, uint32_t opcode)
{
    return opcode < version->op_count ? &xxbyte_v8_shapes[version->ops[opcode]] : NULL;
}

const char *xxbyte_v8_mnemonic(uint32_t id)
{
    return id != 0 && id < XXBYTE_V8_ID_COUNT ? xxbyte_v8_names[id] : NULL;
}

const char *xxbyte_v8_opname(uint32_t mode, uint32_t opcode)
{
    const v8_version *version = v8_find_version(mode);
    const v8_shape *shape = version != NULL ? v8_shape_of(version, opcode) : NULL;
    return shape != NULL ? xxbyte_v8_names[shape->id] : NULL;
}

uint32_t xxbyte_v8_opcode_count(uint32_t mode)
{
    const v8_version *version = v8_find_version(mode);
    return version != NULL ? version->op_count : 0u;
}

static const char *v8_table_name(const uint16_t *table, uint32_t count, uint32_t index)
{
    return table != NULL && index < count ? xxbyte_v8_strings[table[index]] : NULL;
}

const char *xxbyte_v8_runtime_name(uint32_t mode, uint32_t id)
{
    const v8_version *version = v8_find_version(mode);
    return version != NULL ? v8_table_name(version->runtime, version->runtime_count, id) : NULL;
}

uint32_t xxbyte_v8_runtime_count(uint32_t mode)
{
    const v8_version *version = v8_find_version(mode);
    return version != NULL ? version->runtime_count : 0u;
}

const char *xxbyte_v8_intrinsic_name(uint32_t mode, uint32_t id)
{
    const v8_version *version = v8_find_version(mode);
    return version != NULL ? v8_table_name(version->intrinsic, version->intrinsic_count, id) : NULL;
}

const char *xxbyte_v8_native_context_name(uint32_t mode, uint32_t index)
{
    const v8_version *version = v8_find_version(mode);
    if (version == NULL || index < version->native_context_base) {
        return NULL;
    }
    return v8_table_name(version->native, version->native_count, index - version->native_context_base);
}

const char *xxbyte_v8_abort_reason(uint32_t mode, uint32_t reason)
{
    const v8_version *version = v8_find_version(mode);
    return version != NULL ? v8_table_name(version->abort, version->abort_count, reason) : NULL;
}

/* ---- registers ---------------------------------------------------------- */

/* Operand -> register index, and the classification flags. Operands -1, -2
 * and 1 are <context>, <closure> and <accumulator> (fixed frame slots below
 * and above fp); other indexes below 0 are parameters. */
static int64_t v8_register_index(const v8_version *version, int64_t operand)
{
    return (int64_t)version->register_file_start - operand;
}

static uint16_t v8_register_flags(const v8_version *version, int64_t index)
{
    int64_t operand = (int64_t)version->register_file_start - index;
    if (operand == -1 || operand == -2 || operand == 1) {
        return XXBYTE_V8_OPERAND_FLAG_SPECIAL;
    }
    return index < 0 ? XXBYTE_V8_OPERAND_FLAG_PARAMETER : 0u;
}

static int v8_append_register(xx_buf_t *out, const v8_version *version, int64_t index)
{
    int64_t operand = (int64_t)version->register_file_start - index;
    if (operand == -1) {
        return xx_buf_append_str(out, "<context>") ? 1 : 0;
    }
    if (operand == -2) {
        return xx_buf_append_str(out, "<closure>") ? 1 : 0;
    }
    if (operand == 1) {
        return xx_buf_append_str(out, "<accumulator>") ? 1 : 0;
    }
    if (index < 0) {
        int64_t parameter = operand - 2;
        if (parameter == 0) {
            return xx_buf_append_str(out, "<this>") ? 1 : 0;
        }
        return xx_buf_appendf(out, "a%lld", (long long)(parameter - 1)) ? 1 : 0;
    }
    return xx_buf_appendf(out, "r%lld", (long long)index) ? 1 : 0;
}

size_t xxbyte_v8_register_name(uint32_t mode, int64_t index, char *buffer, size_t buffer_size)
{
    const v8_version *version = v8_find_version(mode);
    xx_buf_t out;
    size_t length = 0;

    if (version == NULL) {
        if (buffer != NULL && buffer_size != 0) {
            buffer[0] = '\0';
        }
        return 0;
    }
    xx_buf_init(&out);
    if (v8_append_register(&out, version, index) && xx_buf_ok(&out)) {
        length = out.size;
        if (buffer != NULL && buffer_size != 0) {
            size_t n = length < buffer_size - 1 ? length : buffer_size - 1;
            memcpy(buffer, out.data, n);
            buffer[n] = '\0';
        }
    } else if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    xx_buf_free(&out);
    return length;
}

/* ---- decoding ----------------------------------------------------------- */

static uint32_t v8_operand_width(uint8_t size, uint32_t scale)
{
    switch (size) {
        case V8S_BYTE: return 1;
        case V8S_SHORT: return 2;
        default: return scale;
    }
}

static uint64_t v8_read(const uint8_t *p, uint32_t width)
{
    switch (width) {
        case 1: return p[0];
        case 2: return xxbyte_u16le(p);
        default: return xxbyte_u32le(p);
    }
}

static int v8_is_signed(uint8_t type)
{
    return type == XXBYTE_V8_OPERAND_IMM || (type >= XXBYTE_V8_OPERAND_REG && type <= XXBYTE_V8_OPERAND_REG_IN_OUT);
}

static xxbyte_operand *v8_add(xxbyte_instruction *instruction, xxbyte_operand_type type, uint8_t kind, uint16_t flags, uint32_t size, int64_t value, uint64_t extra)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, type, size, value);
    if (operand != NULL) {
        operand->kind = kind;
        operand->flags = flags;
        operand->extra = extra;
    }
    return operand;
}

static uint8_t v8_index_kind(uint8_t type, uint8_t kind)
{
    switch (type) {
        case XXBYTE_V8_OPERAND_RUNTIME_ID: return XXBYTE_V8_INDEX_RUNTIME;
        case XXBYTE_V8_OPERAND_INTRINSIC_ID: return XXBYTE_V8_INDEX_INTRINSIC;
        case XXBYTE_V8_OPERAND_NATIVE_CONTEXT_INDEX: return XXBYTE_V8_INDEX_NATIVE_CONTEXT;
        case XXBYTE_V8_OPERAND_ABORT_REASON: return XXBYTE_V8_INDEX_ABORT_REASON;
        default: break;
    }
    switch (kind) {
        case V8K_CONSTANT: return XXBYTE_INDEX_CONSTANT;
        case V8K_FEEDBACK: return XXBYTE_V8_INDEX_FEEDBACK;
        case V8K_CONTEXT: return XXBYTE_V8_INDEX_CONTEXT_SLOT;
        case V8K_COVERAGE: return XXBYTE_V8_INDEX_COVERAGE;
        case V8K_ABORT: return XXBYTE_V8_INDEX_ABORT_REASON;
        default: return XXBYTE_V8_INDEX_IDX;
    }
}

/* Parses a constant's text as a Smi ("-12"); returns 1 on success. */
static int v8_parse_smi(const char *text, int64_t *value)
{
    int64_t v = 0;
    int negative = 0;
    int digits = 0;

    if (text == NULL) {
        return 0;
    }
    if (*text == '-') {
        negative = 1;
        ++text;
    }
    while (*text >= '0' && *text <= '9') {
        v = v * 10 + (*text - '0');
        if (v > INT64_C(0x7FFFFFFF) + 1) {
            return 0;
        }
        ++text;
        ++digits;
    }
    if (digits == 0 || *text != '\0' || (!negative && v > INT64_C(0x7FFFFFFF))) {
        return 0;
    }
    *value = negative ? -v : v;
    return 1;
}

static int v8_constant_smi(const xxbyte_v8_context *context, uint64_t index, int64_t *value)
{
    if (context == NULL || context->constants == NULL || index >= context->constant_count) {
        return 0;
    }
    return v8_parse_smi(context->constants[index], value);
}

static xxbyte_status v8_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const v8_version *version = v8_find_version(mode);
    const v8_shape *shape = v8_shape_of(version, code[0]);
    uint32_t scale = 1;
    uint32_t prefix = 0;
    uint32_t size;
    uint32_t i;
    const uint8_t *p;
    uint64_t base;

    if (shape == NULL) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if ((shape->flags & V8F_PREFIX) != 0) {
        scale = (shape->id == XXBYTE_V8_ID_WIDE || shape->id == XXBYTE_V8_ID_DEBUG_BREAK_WIDE) ? V8_PREFIX_WIDE : V8_PREFIX_EXTRA_WIDE;
        if (code_size < 2) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        shape = v8_shape_of(version, code[1]);
        if (shape == NULL || (shape->flags & V8F_SCALABLE) == 0 || (shape->flags & V8F_PREFIX) != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        prefix = 1;
    }
    size = 1 + prefix;
    for (i = 0; i < shape->count; ++i) {
        size += v8_operand_width(shape->sizes[i], scale);
    }
    if (size > code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    instruction->size = size;
    instruction->opcode = prefix != 0 ? (XXBYTE_V8_OPCODE_PREFIXED | (uint32_t)code[0] << 8 | code[1]) : code[0];
    instruction->id = shape->id;
    instruction->group = shape->group;
    instruction->mnemonic = scale == 1 ? xxbyte_v8_names[shape->id] : (scale == V8_PREFIX_WIDE ? xxbyte_v8_names_wide[shape->id] : xxbyte_v8_names_extra_wide[shape->id]);
    if ((shape->flags & V8F_DEBUG_BREAK) != 0) {
        return XXBYTE_STATUS_OK;
    }
    /* Jump offsets are relative to the bytecode after its prefix. */
    base = address + prefix;
    p = code + 1 + prefix;
    for (i = 0; i < shape->count; ++i) {
        uint8_t type = shape->types[i];
        uint32_t width = v8_operand_width(shape->sizes[i], scale);
        uint64_t raw = v8_read(p, width);
        int64_t value = v8_is_signed(type) ? xxbyte_sext(raw, width * 8u) : (int64_t)raw;
        p += width;

        if (i == 0 && shape->jump != V8J_NONE) {
            if (shape->jump == V8J_IMMEDIATE) {
                v8_add(instruction, XXBYTE_OPERAND_BRANCH, 0, 0, width, (int64_t)(base + raw), raw);
                continue;
            }
            if (shape->jump == V8J_LOOP) {
                v8_add(instruction, XXBYTE_OPERAND_BRANCH, 0, 0, width, (int64_t)(base - raw), raw);
                continue;
            }
            {
                int64_t smi;
                if (v8_constant_smi((const xxbyte_v8_context *)context, raw, &smi)) {
                    v8_add(instruction, XXBYTE_OPERAND_BRANCH, 0, XXBYTE_V8_OPERAND_FLAG_CONSTANT, width, (int64_t)(base + (uint64_t)smi), raw);
                    continue;
                }
            }
        }
        switch (type) {
            case XXBYTE_V8_OPERAND_REG:
            case XXBYTE_V8_OPERAND_REG_OUT:
            case XXBYTE_V8_OPERAND_REG_IN_OUT: {
                int64_t index = v8_register_index(version, value);
                uint16_t flags =
                    (uint16_t)(XXBYTE_OPERAND_FLAG_SIGNED | v8_register_flags(version, index) | (type != XXBYTE_V8_OPERAND_REG ? XXBYTE_V8_OPERAND_FLAG_OUTPUT : 0u));
                v8_add(instruction, XXBYTE_OPERAND_REGISTER, 0, flags, width, index, (uint64_t)value);
                break;
            }
            case XXBYTE_V8_OPERAND_REG_LIST:
            case XXBYTE_V8_OPERAND_REG_OUT_LIST:
            case XXBYTE_V8_OPERAND_REG_PAIR:
            case XXBYTE_V8_OPERAND_REG_OUT_PAIR:
            case XXBYTE_V8_OPERAND_REG_OUT_TRIPLE: {
                int64_t index = v8_register_index(version, value);
                uint64_t count = type == XXBYTE_V8_OPERAND_REG_OUT_TRIPLE ? 3u : 2u;
                uint16_t flags =
                    (uint16_t)(XXBYTE_OPERAND_FLAG_SIGNED | v8_register_flags(version, index) | (type >= XXBYTE_V8_OPERAND_REG_OUT ? XXBYTE_V8_OPERAND_FLAG_OUTPUT : 0u));
                if ((type == XXBYTE_V8_OPERAND_REG_LIST || type == XXBYTE_V8_OPERAND_REG_OUT_LIST) && i + 1 < shape->count &&
                    shape->types[i + 1] == XXBYTE_V8_OPERAND_REG_COUNT) {
                    uint32_t count_width = v8_operand_width(shape->sizes[i + 1], scale);
                    count = v8_read(p, count_width);
                    p += count_width;
                    ++i;
                }
                v8_add(instruction, XXBYTE_OPERAND_REGISTER_RANGE, 0, flags, width, index, count);
                break;
            }
            case XXBYTE_V8_OPERAND_IMM: v8_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, XXBYTE_OPERAND_FLAG_SIGNED, width, value, raw); break;
            case XXBYTE_V8_OPERAND_UIMM:
            case XXBYTE_V8_OPERAND_REG_COUNT:
            case XXBYTE_V8_OPERAND_FLAG8:
            case XXBYTE_V8_OPERAND_FLAG16:
            case XXBYTE_V8_OPERAND_EMBEDDED_FEEDBACK: v8_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, width, value, raw); break;
            default: v8_add(instruction, XXBYTE_OPERAND_INDEX, v8_index_kind(type, shape->kinds[i]), 0, width, value, raw); break;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* The shape of a decoded instruction, or NULL. */
static const v8_shape *v8_instruction_shape(const xxbyte_instruction *instruction, const v8_version **version_out)
{
    const v8_version *version;
    const v8_shape *shape;

    if (instruction == NULL || instruction->family != XXBYTE_FAMILY_V8 || instruction->id == 0) {
        return NULL;
    }
    version = v8_find_version(instruction->mode);
    if (version == NULL) {
        return NULL;
    }
    shape = v8_shape_of(version, instruction->opcode & 0xFFu);
    if (shape == NULL || shape->id != instruction->id) {
        return NULL;
    }
    if (version_out != NULL) {
        *version_out = version;
    }
    return shape;
}

/* The table operand type behind instruction operand operand_index. */
static uint8_t v8_operand_type_of(const v8_shape *shape, uint32_t operand_index)
{
    uint32_t i;
    uint32_t n = 0;

    if ((shape->flags & V8F_DEBUG_BREAK) != 0) {
        return XXBYTE_V8_OPERAND_NONE;
    }
    for (i = 0; i < shape->count; ++i) {
        uint8_t type = shape->types[i];
        if (n == operand_index) {
            return type;
        }
        if ((type == XXBYTE_V8_OPERAND_REG_LIST || type == XXBYTE_V8_OPERAND_REG_OUT_LIST) && i + 1 < shape->count &&
            shape->types[i + 1] == XXBYTE_V8_OPERAND_REG_COUNT) {
            ++i;
        }
        ++n;
    }
    return XXBYTE_V8_OPERAND_NONE;
}

uint32_t xxbyte_v8_operand_type(const xxbyte_instruction *instruction, uint32_t operand_index)
{
    const v8_shape *shape = v8_instruction_shape(instruction, NULL);
    if (shape == NULL || operand_index >= instruction->operand_count) {
        return XXBYTE_V8_OPERAND_NONE;
    }
    return v8_operand_type_of(shape, operand_index);
}

/* ---- switches ----------------------------------------------------------- */

uint32_t xxbyte_v8_switch_count(const xxbyte_instruction *instruction)
{
    const v8_shape *shape = v8_instruction_shape(instruction, NULL);
    if (shape == NULL || (shape->group & XXBYTE_GROUP_SWITCH) == 0 || instruction->operand_count != 3) {
        return 0;
    }
    /* SwitchOnSmiNoFeedback: table_start, table_length, case_value_base;
     * SwitchOnGeneratorState: generator, table_start, table_length. */
    return (uint32_t)instruction->operands[shape->id == XXBYTE_V8_ID_SWITCH_ON_SMI_NO_FEEDBACK ? 1 : 2].value;
}

xxbyte_status xxbyte_v8_switch_entry(const xxbyte_v8_context *context, const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target)
{
    const v8_shape *shape = v8_instruction_shape(instruction, NULL);
    uint64_t start;
    int64_t base_key = 0;
    int64_t smi;
    uint64_t prefix;

    if (context == NULL || key == NULL || target == NULL || shape == NULL || index >= xxbyte_v8_switch_count(instruction)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (shape->id == XXBYTE_V8_ID_SWITCH_ON_SMI_NO_FEEDBACK) {
        start = (uint64_t)instruction->operands[0].value;
        base_key = instruction->operands[2].value;
    } else {
        start = (uint64_t)instruction->operands[1].value;
    }
    if (!v8_constant_smi(context, start + index, &smi)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    prefix = (instruction->opcode & XXBYTE_V8_OPCODE_PREFIXED) != 0 ? 1u : 0u;
    *key = base_key + (int64_t)index;
    *target = (int64_t)(instruction->address + prefix + (uint64_t)smi);
    return XXBYTE_STATUS_OK;
}

/* ---- formatting --------------------------------------------------------- */

/* "0x0026": at least four lowercase hex digits; "-0x0004" when negative. */
static int v8_format_address(xxbyte_fmt *fmt, int64_t target)
{
    if (target < 0) {
        return xxbyte_fmt_printf(fmt, "-0x%04llx", (unsigned long long)(0u - (uint64_t)target));
    }
    return xxbyte_fmt_printf(fmt, "0x%04llx", (unsigned long long)target);
}

static int v8_format_name(xxbyte_fmt *fmt, const char *name, int64_t value)
{
    if (name != NULL) {
        return xxbyte_fmt_printf(fmt, "[%s]", name);
    }
    return xxbyte_fmt_printf(fmt, "[%llu]", (unsigned long long)value);
}

static int v8_format_operand(const xxbyte_instruction *instruction, const v8_version *version, const v8_shape *shape, uint32_t index, xxbyte_fmt *fmt)
{
    const xxbyte_operand *operand = &instruction->operands[index];
    uint8_t type = v8_operand_type_of(shape, index);

    if (operand->type == XXBYTE_OPERAND_INDEX || operand->type == XXBYTE_OPERAND_BRANCH) {
        int resolved = xxbyte_fmt_resolve(fmt, instruction, index);
        if (resolved != 0) {
            return resolved > 0;
        }
    }
    switch (operand->type) {
        case XXBYTE_OPERAND_BRANCH: return v8_format_address(fmt, operand->value);
        case XXBYTE_OPERAND_REGISTER: return v8_append_register(fmt->out, version, operand->value);
        case XXBYTE_OPERAND_REGISTER_RANGE: {
            /* RegisterList::last_register() is r0 for an empty list. */
            int64_t last = operand->extra == 0 ? 0 : operand->value + (int64_t)(operand->extra - 1u);
            return v8_append_register(fmt->out, version, operand->value) && xxbyte_fmt_char(fmt, '-') && v8_append_register(fmt->out, version, last);
        }
        case XXBYTE_OPERAND_INDEX:
            switch (type) {
                case XXBYTE_V8_OPERAND_RUNTIME_ID:
                    return v8_format_name(fmt, v8_table_name(version->runtime, version->runtime_count, (uint32_t)operand->value), operand->value);
                case XXBYTE_V8_OPERAND_INTRINSIC_ID:
                    return v8_format_name(fmt, v8_table_name(version->intrinsic, version->intrinsic_count, (uint32_t)operand->value), operand->value);
                case XXBYTE_V8_OPERAND_NATIVE_CONTEXT_INDEX:
                    return v8_format_name(fmt, xxbyte_v8_native_context_name(version->mode, (uint32_t)operand->value), operand->value);
                case XXBYTE_V8_OPERAND_ABORT_REASON:
                    return v8_format_name(fmt, v8_table_name(version->abort, version->abort_count, (uint32_t)operand->value), operand->value);
                case XXBYTE_V8_OPERAND_FEEDBACK_SLOT: return xxbyte_fmt_printf(fmt, "FBV[%llu]", (unsigned long long)operand->value);
                default: return xxbyte_fmt_printf(fmt, "[%llu]", (unsigned long long)operand->value);
            }
        default:
            switch (type) {
                case XXBYTE_V8_OPERAND_IMM: return xxbyte_fmt_printf(fmt, "[%lld]", (long long)operand->value);
                case XXBYTE_V8_OPERAND_FLAG8:
                case XXBYTE_V8_OPERAND_FLAG16:
                    if (version->hex_flags) {
                        return xxbyte_fmt_printf(fmt, "#%llx", (unsigned long long)operand->value);
                    }
                    return xxbyte_fmt_printf(fmt, "#%llu", (unsigned long long)operand->value);
                case XXBYTE_V8_OPERAND_EMBEDDED_FEEDBACK: {
                    const char *name = NULL;
                    if ((shape->flags & V8F_FB_BINARY) != 0 && version->binary != NULL) {
                        name = v8_table_name(version->binary, version->binary_count, (uint32_t)operand->value);
                    } else if ((shape->flags & V8F_FB_COMPARE) != 0 && version->compare != NULL) {
                        name = v8_table_name(version->compare, version->compare_count, (uint32_t)operand->value);
                    } else if (version->binary == NULL) {
                        return xxbyte_fmt_printf(fmt, "EmbeddedFeedback[0x%llx]", (unsigned long long)operand->value);
                    } else {
                        return xxbyte_fmt_printf(fmt, "EmbeddedFeedback[%llu]", (unsigned long long)operand->value);
                    }
                    return xxbyte_fmt_printf(fmt, "EmbeddedFeedback[%s]", name != NULL ? name : "Unknown");
                }
                default: return xxbyte_fmt_printf(fmt, "[%llu]", (unsigned long long)operand->value);
            }
    }
}

static int v8_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const v8_version *version = NULL;
    const v8_shape *shape = v8_instruction_shape(instruction, &version);
    uint32_t i;

    if (shape == NULL) {
        return 0;
    }
    for (i = 0; i < instruction->operand_count; ++i) {
        if (!xxbyte_fmt_str(fmt, i == 0 ? " " : ", ") || !v8_format_operand(instruction, version, shape, i, fmt)) {
            return 0;
        }
    }
    return 1;
}

size_t xxbyte_v8_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_v8_context *v8 = (const xxbyte_v8_context *)context;
    const xxbyte_operand *operand;
    const char *text;
    xx_buf_t out;
    size_t length = 0;

    if (v8 == NULL || instruction == NULL || buffer == NULL || buffer_size == 0 || operand_index >= instruction->operand_count) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (operand->type != XXBYTE_OPERAND_INDEX || operand->kind != XXBYTE_INDEX_CONSTANT || v8->constants == NULL || operand->value < 0 ||
        (uint64_t)operand->value >= v8->constant_count) {
        return 0;
    }
    text = v8->constants[operand->value];
    if (text == NULL) {
        return 0;
    }
    xx_buf_init(&out);
    if (xx_buf_appendf(&out, "[%llu:%s]", (unsigned long long)operand->value, text) && xx_buf_ok(&out)) {
        length = out.size;
        if (length >= buffer_size) {
            length = buffer_size - 1;
        }
        memcpy(buffer, out.data, length);
        buffer[length] = '\0';
    }
    xx_buf_free(&out);
    return length;
}

const xxbyte_family_ops xxbyte_v8_ops = {
    XXBYTE_FAMILY_V8, XXBYTE_V8_MODE_DEFAULT, v8_check_mode, v8_decode, NULL, v8_format_operands, NULL, NULL,
};

#else
typedef int xxbyte_v8_disabled;
#endif
