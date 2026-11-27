/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_lua_names.h"
#include "xxbyte_lua_tables.h"

#if XXBYTE_USE_LUA

#define LUA_MODE_FLAGS (XXBYTE_LUA_MODE_BIG_ENDIAN | XXBYTE_LUA_MODE_WIDE)

/* Excess-K offsets of the signed fields. */
#define LUA_OFFSET_SBX_51 131071 /* 18-bit sBx, 5.0 to 5.3 */
#define LUA_OFFSET_SBX_54 65535  /* 17-bit sBx, 5.4+ */
#define LUA_OFFSET_SJ 16777215   /* 25-bit sJ */
#define LUA_OFFSET_SC 127        /* 8-bit sB and sC */

/* The 5.0 RK split: MAXSTACK in llimits.h. */
#define LUA_MAXSTACK_50 250u

static const lua_version *lua_find_version(uint32_t mode)
{
    uint32_t minor = XXBYTE_LUA_MODE_MINOR(mode);

    if ((mode & ~(uint32_t)(0xFFFFu | LUA_MODE_FLAGS)) != 0 || XXBYTE_LUA_MODE_MAJOR(mode) != 5 || minor >= XXBYTE_LUA_VERSION_COUNT ||
        ((mode & XXBYTE_LUA_MODE_WIDE) != 0 && minor != 0)) {
        return NULL;
    }
    return &xxbyte_lua_versions[minor];
}

static xxbyte_status lua_check_mode(uint32_t mode)
{
    return lua_find_version(mode) != NULL ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static uint32_t lua_unit(uint32_t mode)
{
    return (mode & XXBYTE_LUA_MODE_WIDE) != 0 ? 8u : 4u;
}

static uint32_t lua_read32(uint32_t mode, const uint8_t *p)
{
    return (mode & XXBYTE_LUA_MODE_BIG_ENDIAN) != 0 ? xxbyte_u32be(p) : xxbyte_u32le(p);
}

/* The raw (unsigned) value of a field. */
static uint32_t lua_field_raw(uint32_t layout, uint32_t source, uint32_t word)
{
    if (layout == LUA_LAYOUT_50) {
        switch (source) {
            case LF_A: return word >> 24;
            case LF_B: return (word >> 15) & 0x1FFu;
            case LF_C: return (word >> 6) & 0x1FFu;
            default: return (word >> 6) & 0x3FFFFu; /* Bx, sBx */
        }
    }
    if (layout == LUA_LAYOUT_51) {
        switch (source) {
            case LF_A: return (word >> 6) & 0xFFu;
            case LF_B: return word >> 23;
            case LF_C: return (word >> 14) & 0x1FFu;
            case LF_AX: return word >> 6;
            default: return word >> 14; /* Bx, sBx */
        }
    }
    switch (source) {
        case LF_A: return (word >> 7) & 0xFFu;
        case LF_K: return (word >> 15) & 1u;
        case LF_B:
        case LF_SB: return (word >> 16) & 0xFFu;
        case LF_C:
        case LF_SC: return word >> 24;
        case LF_VB: return (word >> 16) & 0x3Fu;
        case LF_VC: return word >> 22;
        case LF_AX:
        case LF_SJ: return word >> 7;
        default: return word >> 15; /* Bx, sBx */
    }
}

/* The field's value: excess-K fields are signed. */
static int64_t lua_field_value(uint32_t layout, uint32_t source, uint32_t raw)
{
    switch (source) {
        case LF_SBX: return (int64_t)raw - (layout == LUA_LAYOUT_54 ? LUA_OFFSET_SBX_54 : LUA_OFFSET_SBX_51);
        case LF_SJ: return (int64_t)raw - LUA_OFFSET_SJ;
        case LF_SB:
        case LF_SC: return (int64_t)raw - LUA_OFFSET_SC;
        default: return (int64_t)raw;
    }
}

/* Field width in bits, rounded up to bytes. */
static uint32_t lua_field_size(uint32_t layout, uint32_t source)
{
    switch (source) {
        case LF_A:
        case LF_K:
        case LF_VB:
        case LF_SB:
        case LF_SC: return 1;
        case LF_B:
        case LF_C: return layout == LUA_LAYOUT_54 ? 1u : 2u;
        case LF_VC: return 2;
        case LF_BX:
        case LF_SBX: return 3;
        default: return 4; /* Ax, sJ */
    }
}

/* address + unit * steps with 64-bit wrap-around. */
static int64_t lua_target(uint64_t address, uint32_t unit, int64_t steps)
{
    return (int64_t)(address + (uint64_t)steps * unit);
}

static void lua_add(xxbyte_instruction *instruction, xxbyte_operand_type type, uint8_t kind, uint16_t flags, uint32_t size, int64_t value, uint32_t raw)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, type, size, value);
    if (operand != NULL) {
        operand->kind = kind;
        operand->flags = flags;
        operand->extra = raw;
    }
}

static void lua_add_field(xxbyte_instruction *instruction, const lua_field *field, uint32_t layout, uint32_t unit, uint32_t word, uint64_t address)
{
    uint32_t raw = lua_field_raw(layout, field->source, word);
    int64_t value = lua_field_value(layout, field->source, raw);
    uint32_t size = lua_field_size(layout, field->source);
    xxbyte_operand_type type = XXBYTE_OPERAND_IMMEDIATE;
    uint8_t kind = XXBYTE_INDEX_NONE;
    uint16_t flags = 0;

    switch (field->kind) {
        case LK_REG: type = XXBYTE_OPERAND_REGISTER; break;
        case LK_SINT: flags = XXBYTE_OPERAND_FLAG_SIGNED; break;
        case LK_CONST:
            type = XXBYTE_OPERAND_INDEX;
            kind = XXBYTE_INDEX_CONSTANT;
            break;
        case LK_UPVAL:
            type = XXBYTE_OPERAND_INDEX;
            kind = XXBYTE_INDEX_UPVALUE;
            break;
        case LK_PROTO:
            type = XXBYTE_OPERAND_INDEX;
            kind = XXBYTE_INDEX_FUNCTION;
            break;
        case LK_RK50:
            if (raw >= LUA_MAXSTACK_50) {
                type = XXBYTE_OPERAND_INDEX;
                kind = XXBYTE_INDEX_CONSTANT;
                value = (int64_t)(raw - LUA_MAXSTACK_50);
            } else {
                type = XXBYTE_OPERAND_REGISTER;
            }
            break;
        case LK_RK51:
            if ((raw & 0x100u) != 0) {
                type = XXBYTE_OPERAND_INDEX;
                kind = XXBYTE_INDEX_CONSTANT;
                value = (int64_t)(raw & 0xFFu);
            } else {
                type = XXBYTE_OPERAND_REGISTER;
            }
            break;
        case LK_RK54:
            if (lua_field_raw(layout, LF_K, word) != 0) {
                type = XXBYTE_OPERAND_INDEX;
                kind = XXBYTE_INDEX_CONSTANT;
            } else {
                type = XXBYTE_OPERAND_REGISTER;
            }
            break;
        case LK_ERRNNIL:
            if (raw != 0) {
                type = XXBYTE_OPERAND_INDEX;
                kind = XXBYTE_INDEX_CONSTANT;
                value = (int64_t)raw - 1;
            }
            break;
        case LK_JUMP:
            type = XXBYTE_OPERAND_BRANCH;
            value = lua_target(address, unit, 1 + value);
            break;
        case LK_BACK:
            type = XXBYTE_OPERAND_BRANCH;
            value = lua_target(address, unit, 1 - value);
            break;
        case LK_EXIT:
            type = XXBYTE_OPERAND_BRANCH;
            value = lua_target(address, unit, 2 + value);
            break;
        default: /* LK_UINT, LK_KFLAG */ break;
    }
    lua_add(instruction, type, kind, flags, size, value, raw);
}

static xxbyte_status lua_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const lua_version *version = lua_find_version(mode);
    const lua_op *op;
    uint32_t unit = lua_unit(mode);
    uint32_t layout;
    uint32_t word;
    uint32_t opcode;
    uint32_t group;
    uint32_t i;
    int skip;

    (void)context;
    if (version == NULL) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if (code_size < unit) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (unit == 8) {
        /* A 64-bit 5.0 Instruction: the fields live in the low 32 bits. */
        int big = (mode & XXBYTE_LUA_MODE_BIG_ENDIAN) != 0;
        uint32_t high = lua_read32(mode, code + (big ? 0 : 4));
        if (high != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        word = lua_read32(mode, code + (big ? 4 : 0));
    } else {
        word = lua_read32(mode, code);
    }
    layout = version->layout;
    opcode = word & (layout == LUA_LAYOUT_54 ? 0x7Fu : 0x3Fu);
    if (opcode >= version->opcode_count) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    op = &version->ops[opcode];
    group = op->group;
    instruction->size = unit;
    if ((op->flags & LO_SETLIST_WORD) != 0 && lua_field_raw(layout, LF_C, word) == 0) {
        if (code_size < 2u * unit) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        instruction->size = 2u * unit;
    }
    instruction->opcode = opcode;
    instruction->id = op->id;
    instruction->mnemonic = xxbyte_lua_mnemonics[op->id - 1];
    for (i = 0; i < op->count; ++i) {
        lua_add_field(instruction, &op->fields[i], layout, unit, word, address);
    }
    if (instruction->size == 2u * unit) {
        /* 5.1 SETLIST block number, read by the VM as an int. */
        uint32_t next = lua_read32(mode, code + unit);
        lua_add(instruction, XXBYTE_OPERAND_IMMEDIATE, XXBYTE_INDEX_NONE, XXBYTE_OPERAND_FLAG_SIGNED, 4, xxbyte_sext(next, 32), next);
    }
    skip = (op->flags & LO_SKIP) != 0;
    if ((op->flags & LO_SKIP_IF_C) != 0 && lua_field_raw(layout, LF_C, word) != 0) {
        skip = 1;
        group = XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP;
    }
    if (skip) {
        lua_add(instruction, XXBYTE_OPERAND_BRANCH, XXBYTE_INDEX_NONE, XXBYTE_OPERAND_FLAG_IMPLICIT, 0, lua_target(address, unit, 2), 0);
    }
    instruction->group = group;
    return XXBYTE_STATUS_OK;
}

/* luac's pc of a byte target: floor(target / unit) + 1. */
static long long lua_luac_pc(int64_t target, uint32_t unit)
{
    int64_t pc = target >= 0 ? target / (int64_t)unit : -((-(target + 1)) / (int64_t)unit) - 1;
    return (long long)pc + 1;
}

static int lua_format_column(const xxbyte_operand *operand, const lua_field *field, uint32_t layout, xxbyte_fmt *fmt)
{
    uint32_t raw = (uint32_t)operand->extra;

    switch (field->display) {
        case LD_MYK: return xxbyte_fmt_printf(fmt, " %lld", -1LL - (long long)raw);
        case LD_ISK:
            if ((raw & 0x100u) != 0) {
                return xxbyte_fmt_printf(fmt, " %lld", -1LL - (long long)(raw & 0xFFu));
            }
            return xxbyte_fmt_printf(fmt, " %u", raw);
        default: return xxbyte_fmt_printf(fmt, " %lld", (long long)lua_field_value(layout, field->source, raw));
    }
}

/* Branch comments, then the resolved names of the INDEX operands. */
static int lua_format_comment(const xxbyte_instruction *instruction, const lua_op *op, uint32_t unit, xxbyte_fmt *fmt)
{
    uint32_t i;
    int named = 0;

    for (i = 0; i < op->count; ++i) {
        const xxbyte_operand *operand = &instruction->operands[i];
        int resolved;
        if (operand->type != XXBYTE_OPERAND_BRANCH) {
            continue;
        }
        if (!xxbyte_fmt_str(fmt, op->fields[i].kind == LK_EXIT ? " ; exit to " : " ; to ")) {
            return 0;
        }
        resolved = xxbyte_fmt_resolve(fmt, instruction, i);
        if (resolved < 0 || (resolved == 0 && !xxbyte_fmt_printf(fmt, "%lld", lua_luac_pc(operand->value, unit)))) {
            return 0;
        }
    }
    for (i = 0; i < op->count; ++i) {
        size_t mark;
        int resolved;
        if (instruction->operands[i].type != XXBYTE_OPERAND_INDEX || fmt->resolver == NULL) {
            continue;
        }
        mark = xxbyte_fmt_mark(fmt);
        if (!xxbyte_fmt_str(fmt, named ? " " : " ; ")) {
            return 0;
        }
        resolved = xxbyte_fmt_resolve(fmt, instruction, i);
        if (resolved < 0) {
            return 0;
        }
        if (resolved == 0) {
            xxbyte_fmt_rewind(fmt, mark);
        } else {
            named = 1;
        }
    }
    return 1;
}

static int lua_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const lua_version *version = lua_find_version(instruction->mode);
    const lua_op *op;
    uint32_t i;

    if (version == NULL || instruction->opcode >= version->opcode_count) {
        return 0;
    }
    op = &version->ops[instruction->opcode];
    if (instruction->operand_count < op->count) {
        return 0;
    }
    for (i = 0; i < op->count; ++i) {
        const lua_field *field = &op->fields[i];
        if (field->display == LD_HIDDEN) {
            continue;
        }
        if (field->display == LD_SUFFIX) {
            if (instruction->operands[i].extra != 0 && !xxbyte_fmt_char(fmt, 'k')) {
                return 0;
            }
            continue;
        }
        if (!lua_format_column(&instruction->operands[i], field, version->layout, fmt)) {
            return 0;
        }
    }
    if (fmt->resolver == xxbyte_lua_resolve) {
        /* The ready-made names: everything luac -l -l prints. */
        return xxbyte_lua_luac_comment(instruction, fmt);
    }
    if ((op->flags & LO_SETLIST_WORD) != 0 && instruction->operand_count > op->count && instruction->operands[op->count].type == XXBYTE_OPERAND_IMMEDIATE &&
        !xxbyte_fmt_printf(fmt, " ; %lld", (long long)instruction->operands[op->count].value)) {
        return 0;
    }
    return lua_format_comment(instruction, op, lua_unit(instruction->mode), fmt);
}

static size_t lua_unit_size(uint32_t mode)
{
    return (mode & XXBYTE_LUA_MODE_WIDE) != 0 ? 8u : 4u;
}

const xxbyte_family_ops xxbyte_lua_ops = {
    XXBYTE_FAMILY_LUA, XXBYTE_LUA_MODE_DEFAULT, lua_check_mode, lua_decode, NULL, lua_format_operands, lua_unit_size, xxbyte_lua_list_file,
};

const char *xxbyte_lua_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_LUA_ID_COUNT) {
        return NULL;
    }
    return xxbyte_lua_mnemonics[id - 1];
}

const char *xxbyte_lua_opname(uint32_t mode, uint32_t opcode)
{
    const lua_version *version = lua_find_version(mode & 0xFFFFu);

    if (version == NULL || opcode >= version->opcode_count) {
        return NULL;
    }
    return xxbyte_lua_mnemonics[version->ops[opcode].id - 1];
}

#else
typedef int xxbyte_lua_disabled;
#endif
