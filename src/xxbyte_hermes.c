#include "xxbyte_hermes_internal.h"

#if XXBYTE_USE_HERMES

static xxbyte_status hermes_check_mode(uint32_t mode)
{
    return hermes_find_version(mode) != NULL ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

const char *xxbyte_hermes_mnemonic(uint32_t id)
{
    return id != 0 && id < XXBYTE_HERMES_ID_COUNT ? xxbyte_hermes_names[id] : NULL;
}

const char *xxbyte_hermes_opname(uint32_t mode, uint32_t opcode)
{
    const hermes_version *version = hermes_find_version(mode);

    if (version == NULL || opcode >= version->op_count) {
        return NULL;
    }
    return xxbyte_hermes_names[xxbyte_hermes_shapes[version->ops[opcode]].id];
}

uint32_t xxbyte_hermes_opcode_count(uint32_t mode)
{
    const hermes_version *version = hermes_find_version(mode);
    return version != NULL ? version->op_count : 0u;
}

const char *xxbyte_hermes_builtin_name(const hermes_version *version, uint32_t index)
{
    if (version == NULL || index >= version->builtin_count) {
        return NULL;
    }
    return xxbyte_hermes_builtin_names[version->builtins[index]];
}

const char *xxbyte_hermes_builtin(uint32_t mode, uint32_t index)
{
    return xxbyte_hermes_builtin_name(hermes_find_version(mode), index);
}

static uint32_t hermes_type_size(uint8_t type)
{
    switch (type) {
        case HT_REG8:
        case HT_UINT8:
        case HT_ADDR8: return 1;
        case HT_UINT16: return 2;
        case HT_DOUBLE: return 8;
        default: return 4; /* Reg32, UInt32, Addr32, Imm32 */
    }
}

static void hermes_add(xxbyte_instruction *instruction, xxbyte_operand_type type, uint8_t kind, uint16_t flags, uint32_t size, int64_t value, uint64_t extra)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, type, size, value);
    if (operand != NULL) {
        operand->kind = kind;
        operand->flags = flags;
        operand->extra = extra;
    }
}

/* Locates a switch table of count entries of entry_size bytes at
 * align4(address + raw) inside the decode context; sets table_count and
 * table_offset when it lies there, after the instruction. */
static void hermes_locate_table(const xxbyte_hermes_context *context, const uint8_t *code, uint64_t address, uint32_t raw, uint64_t count, uint32_t entry_size,
                                xxbyte_instruction *instruction)
{
    uintptr_t base;
    uintptr_t here;
    uint64_t code_pos;
    uint64_t table;
    uint64_t table_pos;

    if (context == NULL || context->data == NULL || count == 0) {
        return;
    }
    base = (uintptr_t)context->data;
    here = (uintptr_t)code;
    if (here < base || here - base >= context->size) {
        return;
    }
    code_pos = (uint64_t)(here - base);
    /* The instruction's address within the context must match its position. */
    if (address - context->address != code_pos) {
        return;
    }
    table = (address + raw + 3u) & ~(uint64_t)3u;
    table_pos = table - context->address;
    if (table < address || table_pos < code_pos || table_pos > context->size || count > (context->size - table_pos) / entry_size || table_pos - code_pos > UINT32_MAX ||
        count > UINT32_MAX) {
        return;
    }
    instruction->table_count = (uint32_t)count;
    instruction->table_offset = (uint32_t)(table_pos - code_pos);
}

static xxbyte_status hermes_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const hermes_version *version = hermes_find_version(mode);
    const hermes_shape *shape;
    uint32_t i;
    uint32_t size = 1;
    const uint8_t *p;

    if (code[0] >= version->op_count) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    shape = &xxbyte_hermes_shapes[version->ops[code[0]]];
    for (i = 0; i < shape->count; ++i) {
        size += hermes_type_size(shape->types[i]);
    }
    if (size > code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    instruction->size = size;
    instruction->opcode = code[0];
    instruction->id = shape->id;
    instruction->group = shape->group;
    instruction->mnemonic = xxbyte_hermes_names[shape->id];
    p = code + 1;
    for (i = 0; i < shape->count; ++i) {
        uint8_t type = shape->types[i];
        uint32_t width = hermes_type_size(type);
        uint64_t raw;

        switch (width) {
            case 1: raw = p[0]; break;
            case 2: raw = xxbyte_u16le(p); break;
            case 8: raw = xxbyte_u64le(p); break;
            default: raw = xxbyte_u32le(p); break;
        }
        p += width;
        switch (type) {
            case HT_REG8:
            case HT_REG32: hermes_add(instruction, XXBYTE_OPERAND_REGISTER, 0, 0, width, (int64_t)raw, raw); break;
            case HT_ADDR8:
            case HT_ADDR32: {
                int64_t offset = xxbyte_sext(raw, width * 8u);
                hermes_add(instruction, XXBYTE_OPERAND_BRANCH, 0, XXBYTE_OPERAND_FLAG_SIGNED, width, (int64_t)(address + (uint64_t)offset), raw);
                break;
            }
            case HT_IMM32: hermes_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, XXBYTE_OPERAND_FLAG_SIGNED, width, xxbyte_sext(raw, 32), raw); break;
            case HT_DOUBLE: hermes_add(instruction, XXBYTE_OPERAND_FLOAT, 0, 0, 8, (int64_t)raw, raw); break;
            default:
                switch (shape->kinds[i]) {
                    case HK_STRING: hermes_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_STRING, 0, width, (int64_t)raw, raw); break;
                    case HK_FUNCTION: hermes_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_FUNCTION, 0, width, (int64_t)raw, raw); break;
                    case HK_BIGINT: hermes_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_HERMES_INDEX_BIGINT, 0, width, (int64_t)raw, raw); break;
                    case HK_BUILTIN: hermes_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_HERMES_INDEX_BUILTIN, 0, width, (int64_t)raw, raw); break;
                    default: hermes_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, width, (int64_t)raw, raw); break;
                }
                break;
        }
    }
    if (shape->id == XXBYTE_HERMES_ID_SWITCH_IMM || shape->id == XXBYTE_HERMES_ID_U_INT_SWITCH_IMM) {
        /* value, table, default, min, max */
        xxbyte_operand *table = &instruction->operands[1];
        uint64_t min = (uint64_t)instruction->operands[3].value;
        uint64_t max = (uint64_t)instruction->operands[4].value;
        table->type = XXBYTE_OPERAND_DATA;
        table->value = (int64_t)(address + table->extra);
        if (min <= max) {
            hermes_locate_table((const xxbyte_hermes_context *)context, code, address, (uint32_t)table->extra, max - min + 1u, 4u, instruction);
        }
    } else if (shape->id == XXBYTE_HERMES_ID_STRING_SWITCH_IMM) {
        /* value, global index, table, default, size */
        xxbyte_operand *table = &instruction->operands[2];
        table->type = XXBYTE_OPERAND_DATA;
        table->value = (int64_t)(address + table->extra);
        hermes_locate_table((const xxbyte_hermes_context *)context, code, address, (uint32_t)table->extra, (uint64_t)instruction->operands[4].value, 8u, instruction);
    }
    return XXBYTE_STATUS_OK;
}

static int hermes_is_string_switch(const xxbyte_instruction *instruction)
{
    return instruction->id == XXBYTE_HERMES_ID_STRING_SWITCH_IMM;
}

static xxbyte_status hermes_switch_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target)
{
    const uint8_t *entry;

    if (hermes_is_string_switch(instruction)) {
        if (instruction->operand_count != 5) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
        entry = instruction->code + instruction->table_offset + (size_t)index * 8u;
        *key = (int64_t)xxbyte_u32le(entry);
        *target = (int64_t)(instruction->address + (uint64_t)xxbyte_sext(xxbyte_u32le(entry + 4), 32));
        return XXBYTE_STATUS_OK;
    }
    if (instruction->operand_count != 5 || (instruction->id != XXBYTE_HERMES_ID_SWITCH_IMM && instruction->id != XXBYTE_HERMES_ID_U_INT_SWITCH_IMM)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    entry = instruction->code + instruction->table_offset + (size_t)index * 4u;
    *key = (int64_t)((uint64_t)instruction->operands[3].value + index);
    *target = (int64_t)(instruction->address + (uint64_t)xxbyte_sext(xxbyte_u32le(entry), 32));
    return XXBYTE_STATUS_OK;
}

/* "0x01b0": at least four lowercase hex digits; "-0x0010" when negative. */
static int hermes_format_address(xxbyte_fmt *fmt, int64_t target)
{
    if (target < 0) {
        return xxbyte_fmt_printf(fmt, "-0x%04llx", (unsigned long long)(0u - (uint64_t)target));
    }
    return xxbyte_fmt_printf(fmt, "0x%04llx", (unsigned long long)target);
}

static int hermes_format_operand(const xxbyte_instruction *instruction, uint32_t index, xxbyte_fmt *fmt)
{
    const xxbyte_operand *operand = &instruction->operands[index];

    if (operand->type == XXBYTE_OPERAND_INDEX || operand->type == XXBYTE_OPERAND_BRANCH || operand->type == XXBYTE_OPERAND_DATA) {
        int resolved = xxbyte_fmt_resolve(fmt, instruction, index);
        if (resolved != 0) {
            return resolved > 0;
        }
    }
    switch (operand->type) {
        case XXBYTE_OPERAND_REGISTER: return xxbyte_fmt_printf(fmt, "r%llu", (unsigned long long)operand->value);
        case XXBYTE_OPERAND_BRANCH:
        case XXBYTE_OPERAND_DATA: return hermes_format_address(fmt, operand->value);
        case XXBYTE_OPERAND_FLOAT: {
            double d;
            uint64_t bits = (uint64_t)operand->value;
            memcpy(&d, &bits, sizeof(d));
            return xxbyte_fmt_double(fmt, d);
        }
        case XXBYTE_OPERAND_INDEX:
            if (operand->kind == XXBYTE_HERMES_INDEX_BUILTIN) {
                const char *name = xxbyte_hermes_builtin_name(hermes_find_version(instruction->mode), (uint32_t)operand->value);
                if (name != NULL) {
                    return xxbyte_fmt_printf(fmt, "\"%s\"", name);
                }
            }
            return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
        default:
            if ((operand->flags & XXBYTE_OPERAND_FLAG_SIGNED) != 0) {
                return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
            }
            return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
    }
}

/* " [0x0010, 0x0020]" or, for StringSwitchImm, " [5: 0x0010, ...]". */
static int hermes_format_table(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    xxbyte_operand entry;
    uint32_t i;
    int strings = hermes_is_string_switch(instruction);

    if (!xxbyte_fmt_str(fmt, " [")) {
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    for (i = 0; i < instruction->table_count; ++i) {
        int64_t key;
        int64_t target;
        int resolved;

        if (hermes_switch_entry(instruction, i, &key, &target) != XXBYTE_STATUS_OK || (i != 0 && !xxbyte_fmt_str(fmt, ", "))) {
            return 0;
        }
        if (strings) {
            entry.type = XXBYTE_OPERAND_INDEX;
            entry.kind = XXBYTE_INDEX_STRING;
            entry.size = 4;
            entry.value = key;
            entry.extra = (uint64_t)key;
            resolved = xxbyte_fmt_resolve_entry(fmt, instruction, &entry);
            if (resolved < 0 || (resolved == 0 && !xxbyte_fmt_printf(fmt, "%lld", (long long)key)) || !xxbyte_fmt_str(fmt, ": ")) {
                return 0;
            }
        }
        entry.type = XXBYTE_OPERAND_BRANCH;
        entry.kind = 0;
        entry.size = 4;
        entry.value = target;
        entry.extra = (uint64_t)(target - (int64_t)instruction->address) & 0xFFFFFFFFu;
        resolved = xxbyte_fmt_resolve_entry(fmt, instruction, &entry);
        if (resolved < 0 || (resolved == 0 && !hermes_format_address(fmt, target))) {
            return 0;
        }
    }
    return xxbyte_fmt_char(fmt, ']');
}

static int hermes_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;

    for (i = 0; i < instruction->operand_count; ++i) {
        if (!xxbyte_fmt_str(fmt, i == 0 ? " " : ", ") || !hermes_format_operand(instruction, i, fmt)) {
            return 0;
        }
    }
    if (instruction->table_count != 0 && (instruction->group & XXBYTE_GROUP_SWITCH) != 0) {
        return hermes_format_table(instruction, fmt);
    }
    return 1;
}

const xxbyte_family_ops xxbyte_hermes_ops = {
    XXBYTE_FAMILY_HERMES, XXBYTE_HERMES_MODE_DEFAULT, hermes_check_mode, hermes_decode, hermes_switch_entry, hermes_format_operands, NULL, xxbyte_hermes_list_file,
};

#else
typedef int xxbyte_hermes_disabled;
#endif
