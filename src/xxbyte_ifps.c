#include "xxbyte_ifps_internal.h"

#if XXBYTE_USE_IFPS

/*
 * RemObjects PascalScript bytecode. Instruction layouts follow
 * TPSExec.RunScript and TPSExec.ReadVariable (uPSRuntime.pas), names and
 * operand order follow IFPS3DataToText (uPSDisassembly.pas).
 */

#define G_NONE XXBYTE_GROUP_NONE
#define G_CALL XXBYTE_GROUP_CALL
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)

/* Operand layouts after the opcode byte (V = variable). */
enum {
    F_NONE,          /* pop, ret, nop */
    F_VAR,           /* V */
    F_VAR2,          /* V, V */
    F_CALC,          /* u8 operator, V, V */
    F_COMPARE,       /* u8 operator, V, V, V */
    F_PROC,          /* u32 proc number */
    F_TYPE,          /* u32 type number */
    F_BRANCH,        /* s32 offset from the end */
    F_COND,          /* s32 offset from the end, V */
    F_SETFLAG,       /* V, u8 negate */
    F_STACKTYPE,     /* u32 base offset, u32 type number */
    F_PUSHEXCEPTION, /* 4 x u32 offset from the end, or none */
    F_BYTE           /* u8 */
};

/* Indexed by id - 1. */
static const char *const mnemonics[XXBYTE_IFPS_ID_COUNT - 1] = {
    "assign",       "calc",     "push",           "pushvar", "pop",        "call",     "goto",      "cond_goto", "cond_not_goto", "ret",
    "setstacktype", "pushtype", "compare",        "callvar", "setpointer", "bnot",     "minus",     "setflag",   "flaggoto",      "pushexception",
    "popexception", "inot",     "setcopypointer", "inc",     "dec",        "pop/goto", "pop2/goto", "nop",
};

typedef struct ifps_opcode {
    uint8_t id;     /* XXBYTE_IFPS_ID_*, 0 for an undefined opcode */
    uint8_t form;   /* F_* */
    uint16_t group; /* XXBYTE_GROUP_* */
} ifps_opcode;

static const ifps_opcode opcodes[256] = {
    [0x00] = {XXBYTE_IFPS_ID_ASSIGN, F_VAR2, G_NONE},
    [0x01] = {XXBYTE_IFPS_ID_CALC, F_CALC, G_NONE},
    [0x02] = {XXBYTE_IFPS_ID_PUSH, F_VAR, G_NONE},
    [0x03] = {XXBYTE_IFPS_ID_PUSHVAR, F_VAR, G_NONE},
    [0x04] = {XXBYTE_IFPS_ID_POP, F_NONE, G_NONE},
    [0x05] = {XXBYTE_IFPS_ID_CALL, F_PROC, G_CALL},
    [0x06] = {XXBYTE_IFPS_ID_GOTO, F_BRANCH, G_JUMP},
    [0x07] = {XXBYTE_IFPS_ID_COND_GOTO, F_COND, G_COND},
    [0x08] = {XXBYTE_IFPS_ID_COND_NOT_GOTO, F_COND, G_COND},
    [0x09] = {XXBYTE_IFPS_ID_RET, F_NONE, G_RETURN},
    [0x0A] = {XXBYTE_IFPS_ID_SETSTACKTYPE, F_STACKTYPE, G_NONE},
    [0x0B] = {XXBYTE_IFPS_ID_PUSHTYPE, F_TYPE, G_NONE},
    [0x0C] = {XXBYTE_IFPS_ID_COMPARE, F_COMPARE, G_NONE},
    [0x0D] = {XXBYTE_IFPS_ID_CALLVAR, F_VAR, G_CALL},
    [0x0E] = {XXBYTE_IFPS_ID_SETPOINTER, F_VAR2, G_NONE},
    [0x0F] = {XXBYTE_IFPS_ID_BNOT, F_VAR, G_NONE},
    [0x10] = {XXBYTE_IFPS_ID_MINUS, F_VAR, G_NONE},
    [0x11] = {XXBYTE_IFPS_ID_SETFLAG, F_SETFLAG, G_NONE},
    [0x12] = {XXBYTE_IFPS_ID_FLAGGOTO, F_BRANCH, G_COND},
    [0x13] = {XXBYTE_IFPS_ID_PUSHEXCEPTION, F_PUSHEXCEPTION, G_NONE},
    [0x14] = {XXBYTE_IFPS_ID_POPEXCEPTION, F_BYTE, G_NONE},
    [0x15] = {XXBYTE_IFPS_ID_INOT, F_VAR, G_NONE},
    [0x16] = {XXBYTE_IFPS_ID_SETCOPYPOINTER, F_VAR2, G_NONE},
    [0x17] = {XXBYTE_IFPS_ID_INC, F_VAR, G_NONE},
    [0x18] = {XXBYTE_IFPS_ID_DEC, F_VAR, G_NONE},
    [0x19] = {XXBYTE_IFPS_ID_POP_GOTO, F_BRANCH, G_JUMP},
    [0x1A] = {XXBYTE_IFPS_ID_POP2_GOTO, F_BRANCH, G_JUMP},
    [0xFF] = {XXBYTE_IFPS_ID_NOP, F_NONE, G_NONE},
};

static const char *const calc_operators[] = {
    "+", "-", "*", "/", "mod", "shl", "shr", "and", "or", "xor", "as",
};

static const char *const compare_operators[] = {
    ">=", "<=", ">", "<", "<>", "=", "in", "is",
};

/* BT2S of uPSDisassembly.pas as Inno Setup builds it (PS_PANSICHAR); the
 * uPSUtils.pas constant names for the base types BT2S does not name. */
static const char *const base_type_names[] = {
    "ReturnAddress", "U8",       "S8",          "U16",
    "S16",           "U32",      "S32",         "Single",
    "Double",        "Extended", "AnsiString",  "Record",
    "Array",         "Pointer",  "PChar",       "ResourcePointer",
    "Variant",       "S64",      "AnsiChar",    "WideString",
    "WideChar",      "ProcPtr",  "StaticArray", "Set",
    "Currency",      "Class",    "Interface",   "NotificationVariant",
    "UnicodeString", "U64",
};

#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))
#define MAX_SET_BITS 256u
#define MAX_LONGINT 0x7FFFFFFFu /* lengths and offsets are Longints */

xxbyte_status xxbyte_ifps_check_mode(uint32_t mode)
{
    return (mode == XXBYTE_IFPS_MODE_DEFAULT || mode == XXBYTE_IFPS_MODE_32BIT || mode == XXBYTE_IFPS_MODE_64BIT) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

uint32_t xxbyte_ifps_extended_size(uint32_t mode)
{
    return mode == XXBYTE_IFPS_MODE_64BIT ? 8u : 10u;
}

/* ------------------------------------------------------------- decoding */

typedef struct ifps_cursor {
    const uint8_t *code;
    uint64_t size;
    uint64_t pos;
    const xxbyte_ifps_context *context;
    uint32_t mode;
    xxbyte_instruction *instruction;
} ifps_cursor;

static int has(const ifps_cursor *c, uint64_t count)
{
    return count <= c->size - c->pos;
}

static uint32_t take_u32(ifps_cursor *c)
{
    uint32_t value = xxbyte_u32le(c->code + c->pos);
    c->pos += 4;
    return value;
}

static xxbyte_operand *add(ifps_cursor *c, xxbyte_operand_type type, uint32_t size, int64_t value, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_operand(c->instruction, type, size, value);
    if (operand != NULL) {
        operand->flags = flags;
    }
    return operand;
}

static void add_index(ifps_cursor *c, uint32_t kind, uint32_t size, int64_t value, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_index(c->instruction, (xxbyte_index_kind)kind, size, value);
    if (operand != NULL) {
        operand->flags = flags;
    }
}

/*
 * A variable address: a global below XXBYTE_IFPS_ADDR_STACK_LIMIT, else
 * Base[n] with n = address - XXBYTE_IFPS_ADDR_STACK_BASE in 32 bits.
 * element selects the family kinds of a form-3 element selector.
 */
static void add_address(ifps_cursor *c, uint32_t address, int element)
{
    int64_t slot;

    if (address < XXBYTE_IFPS_ADDR_STACK_LIMIT) {
        add_index(c, element ? (uint32_t)XXBYTE_IFPS_INDEX_ELEMENT_GLOBAL : (uint32_t)XXBYTE_INDEX_GLOBAL, 4, address, 0);
        return;
    }
    slot = xxbyte_sext(address - XXBYTE_IFPS_ADDR_STACK_BASE, 32);
    if (slot >= 0) {
        add_index(c, element ? (uint32_t)XXBYTE_IFPS_INDEX_ELEMENT_LOCAL : (uint32_t)XXBYTE_INDEX_LOCAL, 4, slot, 0);
    } else {
        add_index(c, element ? (uint32_t)XXBYTE_IFPS_INDEX_ELEMENT_ARGUMENT : (uint32_t)XXBYTE_INDEX_ARGUMENT, 4, slot, XXBYTE_OPERAND_FLAG_SIGNED);
    }
}

/*
 * A little-endian x87 80-bit value as the x87 stores it to a double: the
 * nearest binary64 (round half to even); NaNs keep the sign and the top
 * payload bits and become quiet; values beyond the double range become
 * infinities. Unsupported encodings (integer bit clear with a nonzero
 * exponent: unnormals, pseudo-zeros, pseudo-infinities, pseudo-NaNs) are
 * invalid operands and become the default NaN. Pseudo-denormals (integer
 * bit set, exponent 0) are valid and weigh 2^-16382.
 */
uint64_t xxbyte_ifps_extended_bits(const uint8_t *p)
{
    uint64_t mantissa = xxbyte_u64le(p);
    uint32_t sign_exponent = xxbyte_u16le(p + 8);
    uint64_t sign = (uint64_t)(sign_exponent >> 15) << 63;
    int32_t exponent = (int32_t)(sign_exponent & 0x7FFFu);
    int32_t top;
    uint64_t rest;
    uint64_t half;
    uint64_t quotient;
    uint32_t shift;

    if (exponent != 0 && (mantissa & UINT64_C(0x8000000000000000)) == 0) {
        return UINT64_C(0xFFF8000000000000);
    }
    if (exponent == 0x7FFF) {
        if ((mantissa & UINT64_C(0x7FFFFFFFFFFFFFFF)) == 0) {
            return sign | UINT64_C(0x7FF0000000000000);
        }
        return sign | UINT64_C(0x7FF8000000000000) | ((mantissa >> 11) & UINT64_C(0x0007FFFFFFFFFFFF));
    }
    if (mantissa == 0) {
        return sign;
    }
    /* value = mantissa * 2^(e - 16383 - 63), e = 1 for the denormals */
    top = (exponent == 0 ? 1 : exponent) - 16383;
    while ((mantissa & UINT64_C(0x8000000000000000)) == 0) {
        mantissa <<= 1;
        --top;
    }
    /* now value = 1.f * 2^top with the leading 1 in bit 63 */
    if (top > 1023) {
        return sign | UINT64_C(0x7FF0000000000000);
    }
    if (top >= -1022) {
        quotient = mantissa >> 11;
        rest = mantissa & 0x7FFu;
        if (rest > 0x400u || (rest == 0x400u && (quotient & 1u) != 0)) {
            ++quotient;
            if (quotient == (UINT64_C(1) << 53)) {
                quotient >>= 1;
                ++top;
                if (top > 1023) {
                    return sign | UINT64_C(0x7FF0000000000000);
                }
            }
        }
        return sign | ((uint64_t)(top + 1023) << 52) | (quotient & UINT64_C(0x000FFFFFFFFFFFFF));
    }
    /* subnormal: units of 2^-1074, bit 63 weighs 2^top */
    shift = (uint32_t)(-1011 - top);
    if (shift > 64u) {
        return sign;
    }
    if (shift == 64u) {
        return sign | (mantissa > UINT64_C(0x8000000000000000) ? 1u : 0u);
    }
    quotient = mantissa >> shift;
    rest = mantissa & ((UINT64_C(1) << shift) - 1u);
    half = UINT64_C(1) << (shift - 1u);
    if (rest > half || (rest == half && (quotient & 1u) != 0)) {
        ++quotient;
    }
    return sign | quotient;
}

static void add_integer(ifps_cursor *c, uint32_t width, int is_signed, uint64_t extra)
{
    const uint8_t *p = c->code + c->pos;
    uint64_t raw;
    int64_t value;
    uint16_t flags = 0;
    xxbyte_operand *operand;

    switch (width) {
        case 1: raw = p[0]; break;
        case 2: raw = xxbyte_u16le(p); break;
        case 4: raw = xxbyte_u32le(p); break;
        default:
            raw = xxbyte_u64le(p);
            flags |= XXBYTE_OPERAND_FLAG_WIDE;
            break;
    }
    if (is_signed) {
        value = xxbyte_sext(raw, width * 8u);
        flags |= XXBYTE_OPERAND_FLAG_SIGNED;
    } else {
        value = (int64_t)raw;
    }
    operand = add(c, XXBYTE_OPERAND_IMMEDIATE, width, value, flags);
    if (operand != NULL) {
        operand->extra = extra;
    }
    c->pos += width;
}

/* xxbyte_operand.extra naming a type of the decode context. */
static uint64_t type_extra(uint32_t type_number, const xxbyte_ifps_type *type)
{
    return (uint64_t)type_number | ((uint64_t)type->base_type << 32) | XXBYTE_IFPS_EXTRA_HAS_TYPE;
}

/* A type number operand; extra names the type when the context has it. */
static void add_type(ifps_cursor *c, uint32_t type_number)
{
    xxbyte_operand *operand = xxbyte_add_index(c->instruction, XXBYTE_INDEX_TYPE, 4, type_number);

    if (operand != NULL && c->context != NULL && c->context->types != NULL && type_number < c->context->type_count) {
        operand->extra = type_extra(type_number, &c->context->types[type_number]);
    }
}

/* Form 1: the type number has been read; the data follows. */
static xxbyte_status read_immediate(ifps_cursor *c, uint32_t type_number)
{
    const xxbyte_ifps_type *type;
    uint64_t extra;
    uint64_t length;
    uint64_t bytes;
    uint32_t width = 0;
    int is_signed = 0;
    int wide;
    xxbyte_operand *operand;

    if (c->context == NULL || c->context->types == NULL || type_number >= c->context->type_count) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    type = &c->context->types[type_number];
    extra = type_extra(type_number, type);
    switch (type->base_type) {
        case XXBYTE_IFPS_BT_U8:
        case XXBYTE_IFPS_BT_CHAR: width = 1; break;
        case XXBYTE_IFPS_BT_S8:
            width = 1;
            is_signed = 1;
            break;
        case XXBYTE_IFPS_BT_U16:
        case XXBYTE_IFPS_BT_WIDE_CHAR: width = 2; break;
        case XXBYTE_IFPS_BT_S16:
            width = 2;
            is_signed = 1;
            break;
        case XXBYTE_IFPS_BT_U32: width = 4; break;
        case XXBYTE_IFPS_BT_S32:
            width = 4;
            is_signed = 1;
            break;
        case XXBYTE_IFPS_BT_U64: width = 8; break;
        case XXBYTE_IFPS_BT_S64:
        case XXBYTE_IFPS_BT_CURRENCY:
            width = 8;
            is_signed = 1;
            break;
        case XXBYTE_IFPS_BT_PROC_PTR:
            if (!has(c, 4)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            operand = xxbyte_add_index(c->instruction, XXBYTE_INDEX_FUNCTION, 4, take_u32(c));
            if (operand != NULL) {
                operand->extra = extra;
            }
            return XXBYTE_STATUS_OK;
        case XXBYTE_IFPS_BT_SINGLE:
            if (!has(c, 4)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            operand = add(c, XXBYTE_OPERAND_FLOAT, 4, take_u32(c), 0);
            if (operand != NULL) {
                operand->extra = extra;
            }
            return XXBYTE_STATUS_OK;
        case XXBYTE_IFPS_BT_DOUBLE:
        case XXBYTE_IFPS_BT_EXTENDED:
            width = type->base_type == XXBYTE_IFPS_BT_DOUBLE ? 8u : xxbyte_ifps_extended_size(c->mode);
            if (!has(c, width)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            operand = add(c, XXBYTE_OPERAND_FLOAT, 8, (int64_t)(width == 8u ? xxbyte_u64le(c->code + c->pos) : xxbyte_ifps_extended_bits(c->code + c->pos)), 0);
            if (operand != NULL) {
                operand->extra = extra;
            }
            c->pos += width;
            return XXBYTE_STATUS_OK;
        case XXBYTE_IFPS_BT_STRING:
        case XXBYTE_IFPS_BT_PCHAR:
        case XXBYTE_IFPS_BT_WIDE_STRING:
        case XXBYTE_IFPS_BT_UNICODE_STRING:
            if (!has(c, 4)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            wide = type->base_type == XXBYTE_IFPS_BT_WIDE_STRING || type->base_type == XXBYTE_IFPS_BT_UNICODE_STRING;
            length = take_u32(c);
            bytes = wide ? length * 2u : length;
            if (!has(c, bytes)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            if (c->pos + bytes > UINT32_MAX) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            operand = add(c, XXBYTE_OPERAND_BYTES, (uint32_t)bytes, (int64_t)c->pos, wide ? XXBYTE_OPERAND_FLAG_WIDE : 0);
            if (operand != NULL) {
                operand->extra = extra;
            }
            c->pos += bytes;
            return XXBYTE_STATUS_OK;
        case XXBYTE_IFPS_BT_SET:
            if (type->set_bits > MAX_SET_BITS) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            bytes = (type->set_bits + 7u) / 8u;
            if (!has(c, bytes)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            operand = add(c, XXBYTE_OPERAND_BYTES, (uint32_t)bytes, (int64_t)c->pos, 0);
            if (operand != NULL) {
                operand->extra = extra;
            }
            c->pos += bytes;
            return XXBYTE_STATUS_OK;
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!has(c, width)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    add_integer(c, width, is_signed, extra);
    return XXBYTE_STATUS_OK;
}

/* One variable: form byte, 32-bit address or type number, and for forms 2
 * and 3 a 32-bit element selector. */
static xxbyte_status read_variable(ifps_cursor *c)
{
    uint32_t form;
    uint32_t address;
    uint32_t selector;

    if (!has(c, 1)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    form = c->code[c->pos];
    if (form > 3u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!has(c, 5)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    ++c->pos;
    address = take_u32(c);
    if (form == 1u) {
        return read_immediate(c, address);
    }
    add_address(c, address, 0);
    if (form == 0u) {
        return XXBYTE_STATUS_OK;
    }
    if (!has(c, 4)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    selector = take_u32(c);
    if (form == 2u) {
        add_index(c, XXBYTE_IFPS_INDEX_ELEMENT, 4, selector, 0);
    } else {
        add_address(c, selector, 1);
    }
    return XXBYTE_STATUS_OK;
}

static int64_t branch_target(uint64_t address, uint64_t end, uint32_t offset)
{
    return (int64_t)(address + end + (uint64_t)xxbyte_sext(offset, 32));
}

static xxbyte_status read_operator(ifps_cursor *c, uint32_t limit)
{
    uint32_t value;

    if (!has(c, 1)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    value = c->code[c->pos];
    if (value > limit) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    ++c->pos;
    add(c, XXBYTE_OPERAND_IMMEDIATE, 1, value, 0);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status read_variables(ifps_cursor *c, int count)
{
    xxbyte_status status = XXBYTE_STATUS_OK;

    while (count-- > 0 && status == XXBYTE_STATUS_OK) {
        status = read_variable(c);
    }
    return status;
}

static xxbyte_status decode_operands(const ifps_opcode *op, ifps_cursor *c, uint64_t address)
{
    xxbyte_status status;
    uint32_t value;
    uint32_t offsets[4];
    int i;

    switch (op->form) {
        case F_VAR: return read_variables(c, 1);
        case F_VAR2: return read_variables(c, 2);
        case F_CALC: status = read_operator(c, XXBYTE_IFPS_CALC_AS); return status == XXBYTE_STATUS_OK ? read_variables(c, 2) : status;
        case F_COMPARE: status = read_operator(c, XXBYTE_IFPS_COMPARE_IS); return status == XXBYTE_STATUS_OK ? read_variables(c, 3) : status;
        case F_PROC:
            if (!has(c, 4)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            add_index(c, XXBYTE_INDEX_FUNCTION, 4, take_u32(c), 0);
            return XXBYTE_STATUS_OK;
        case F_TYPE:
            if (!has(c, 4)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            add_type(c, take_u32(c));
            return XXBYTE_STATUS_OK;
        case F_BRANCH:
            if (!has(c, 4)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            value = take_u32(c);
            add(c, XXBYTE_OPERAND_BRANCH, 4, branch_target(address, c->pos, value), 0);
            return XXBYTE_STATUS_OK;
        case F_COND:
            if (!has(c, 4)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            value = take_u32(c);
            add(c, XXBYTE_OPERAND_BRANCH, 4, 0, 0);
            status = read_variable(c);
            /* relative to the end, after the variable */
            c->instruction->operands[0].value = branch_target(address, c->pos, value);
            return status;
        case F_SETFLAG:
            status = read_variable(c);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (!has(c, 1)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            add(c, XXBYTE_OPERAND_IMMEDIATE, 1, c->code[c->pos++], 0);
            return XXBYTE_STATUS_OK;
        case F_STACKTYPE:
            if (!has(c, 8)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            add_index(c, XXBYTE_INDEX_LOCAL, 4, take_u32(c), 0);
            add_type(c, take_u32(c));
            return XXBYTE_STATUS_OK;
        case F_PUSHEXCEPTION:
            if (!has(c, 16)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            for (i = 0; i < 4; ++i) {
                offsets[i] = take_u32(c);
            }
            for (i = 0; i < 4; ++i) {
                if (offsets[i] == XXBYTE_IFPS_NONE) {
                    add(c, XXBYTE_OPERAND_IMMEDIATE, 4, XXBYTE_IFPS_NONE, 0);
                } else {
                    add(c, XXBYTE_OPERAND_BRANCH, 4, branch_target(address, c->pos, offsets[i]), 0);
                }
            }
            return XXBYTE_STATUS_OK;
        case F_BYTE:
            if (!has(c, 1)) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            add(c, XXBYTE_OPERAND_IMMEDIATE, 1, c->code[c->pos++], 0);
            return XXBYTE_STATUS_OK;
        default: return XXBYTE_STATUS_OK;
    }
}

static xxbyte_status ifps_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const ifps_opcode *op = &opcodes[code[0]];
    ifps_cursor cursor;
    xxbyte_status status;

    if (op->id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    cursor.code = code;
    cursor.size = (uint64_t)code_size;
    cursor.pos = 1;
    cursor.context = (const xxbyte_ifps_context *)context;
    cursor.mode = mode;
    cursor.instruction = instruction;
    status = decode_operands(op, &cursor, address);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (cursor.pos > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->size = (uint32_t)cursor.pos;
    instruction->opcode = code[0];
    instruction->id = op->id;
    instruction->group = op->group;
    /* positions 0 to 3 continue at an offset the matching pushexception
     * registered (or an exit point, or re-raise); the runtime skips other
     * values */
    if (op->id == XXBYTE_IFPS_ID_POPEXCEPTION && code[1] <= XXBYTE_IFPS_POPEXCEPTION_FINALLY2) {
        instruction->group = G_JUMP;
    }
    instruction->mnemonic = mnemonics[op->id - 1];
    return XXBYTE_STATUS_OK;
}

/* ----------------------------------------------------------- formatting */

static int is_element(const xxbyte_operand *operand)
{
    return operand->type == XXBYTE_OPERAND_INDEX && operand->kind >= XXBYTE_IFPS_INDEX_ELEMENT && operand->kind <= XXBYTE_IFPS_INDEX_ELEMENT_ARGUMENT;
}

/* Pascal literal: printable ASCII quoted, everything else as #n. */
int xxbyte_ifps_format_string(xxbyte_fmt *fmt, const uint8_t *data, uint32_t size, int wide)
{
    uint32_t i;
    uint32_t step = wide ? 2u : 1u;
    int quoted = 0;

    if (size / step == 0) {
        return xxbyte_fmt_str(fmt, "''");
    }
    for (i = 0; i + step <= size; i += step) {
        uint32_t unit = wide ? xxbyte_u16le(data + i) : data[i];
        if (unit >= 0x20u && unit <= 0x7Eu) {
            if ((!quoted && !xxbyte_fmt_char(fmt, '\'')) || (unit == '\'' && !xxbyte_fmt_char(fmt, '\'')) || !xxbyte_fmt_char(fmt, (char)unit)) {
                return 0;
            }
            quoted = 1;
        } else {
            if ((quoted && !xxbyte_fmt_char(fmt, '\'')) || !xxbyte_fmt_printf(fmt, "#%u", (unsigned)unit)) {
                return 0;
            }
            quoted = 0;
        }
    }
    return !quoted || xxbyte_fmt_char(fmt, '\'');
}

/* Set members, least significant bit first, with runs as a..b. */
int xxbyte_ifps_format_set(xxbyte_fmt *fmt, const uint8_t *data, uint32_t size)
{
    uint32_t bit = 0;
    uint32_t bits = size * 8u;
    int first = 1;

    if (!xxbyte_fmt_char(fmt, '[')) {
        return 0;
    }
    while (bit < bits) {
        uint32_t last;
        if ((((uint32_t)data[bit / 8u] >> (bit % 8u)) & 1u) == 0) {
            ++bit;
            continue;
        }
        last = bit;
        while (last + 1u < bits && (((uint32_t)data[(last + 1u) / 8u] >> ((last + 1u) % 8u)) & 1u) != 0) {
            ++last;
        }
        if (!xxbyte_fmt_str(fmt, first ? "" : ", ") || !xxbyte_fmt_printf(fmt, "%u", (unsigned)bit) || (last != bit && !xxbyte_fmt_printf(fmt, "..%u", (unsigned)last))) {
            return 0;
        }
        first = 0;
        bit = last + 1u;
    }
    return xxbyte_fmt_char(fmt, ']');
}

/* Currency: the raw value divided by 10000, without trailing zeros. */
int xxbyte_ifps_format_currency(xxbyte_fmt *fmt, int64_t value)
{
    uint64_t magnitude = value < 0 ? (uint64_t)(-(value + 1)) + 1u : (uint64_t)value;
    unsigned fraction = (unsigned)(magnitude % 10000u);
    char digits[6];
    int length = 4;
    int i;

    if (!xxbyte_fmt_printf(fmt, "%s%llu", value < 0 ? "-" : "", (unsigned long long)(magnitude / 10000u))) {
        return 0;
    }
    if (fraction == 0) {
        return 1;
    }
    digits[0] = '.';
    for (i = 4; i >= 1; --i) {
        digits[i] = (char)('0' + fraction % 10u);
        fraction /= 10u;
    }
    while (digits[length] == '0') {
        --length;
    }
    digits[length + 1] = '\0';
    return xxbyte_fmt_str(fmt, digits);
}

static int format_float(xxbyte_fmt *fmt, const xxbyte_operand *operand)
{
    if (operand->size == 4) {
        uint32_t bits = (uint32_t)operand->value;
        float value;
        memcpy(&value, &bits, sizeof(value));
        return xxbyte_fmt_float(fmt, value);
    } else {
        uint64_t bits = (uint64_t)operand->value;
        double value;
        memcpy(&value, &bits, sizeof(value));
        return xxbyte_fmt_double(fmt, value);
    }
}

static int format_immediate(const xxbyte_instruction *instruction, const xxbyte_operand *operand, xxbyte_fmt *fmt)
{
    uint32_t base_type = XXBYTE_IFPS_EXTRA_BASE_TYPE(operand->extra);

    if (operand->type == XXBYTE_OPERAND_FLOAT) {
        return format_float(fmt, operand);
    }
    if (operand->type == XXBYTE_OPERAND_BYTES) {
        if (operand->value < 0 || (uint64_t)operand->value + operand->size > instruction->size) {
            return 0;
        }
        if (base_type == XXBYTE_IFPS_BT_SET) {
            return xxbyte_ifps_format_set(fmt, instruction->code + operand->value, operand->size);
        }
        return xxbyte_ifps_format_string(fmt, instruction->code + operand->value, operand->size, (operand->flags & XXBYTE_OPERAND_FLAG_WIDE) != 0);
    }
    switch (base_type) {
        case XXBYTE_IFPS_BT_CHAR:
        case XXBYTE_IFPS_BT_WIDE_CHAR: return xxbyte_fmt_printf(fmt, "#%llu", (unsigned long long)operand->value);
        case XXBYTE_IFPS_BT_CURRENCY: return xxbyte_ifps_format_currency(fmt, operand->value);
        default: break;
    }
    if ((operand->flags & XXBYTE_OPERAND_FLAG_SIGNED) != 0) {
        return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
    }
    return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
}

/* An immediate (form 1): extra names its type. INDEX TYPE operands may
 * name their type too but are type numbers, not immediates. */
static int is_literal(const xxbyte_operand *operand)
{
    return (operand->extra & XXBYTE_IFPS_EXTRA_HAS_TYPE) != 0 && !(operand->type == XXBYTE_OPERAND_INDEX && operand->kind == XXBYTE_INDEX_TYPE);
}

/* One operand without the brackets of an immediate: a variable, an
 * element selector's inside, an immediate's value, a proc or type number,
 * a branch target or a plain number. */
static int format_operand_text(const xxbyte_instruction *instruction, uint32_t index, xxbyte_fmt *fmt)
{
    const xxbyte_operand *operand = &instruction->operands[index];
    int resolved;

    if (operand->type == XXBYTE_OPERAND_INDEX || operand->type == XXBYTE_OPERAND_BRANCH) {
        resolved = xxbyte_fmt_resolve(fmt, instruction, index);
        if (resolved != 0) {
            return resolved > 0;
        }
    }
    switch (operand->type) {
        case XXBYTE_OPERAND_INDEX:
            switch (operand->kind) {
                case XXBYTE_INDEX_GLOBAL:
                case XXBYTE_IFPS_INDEX_ELEMENT_GLOBAL: return xxbyte_fmt_printf(fmt, "GlobalVar[%llu]", (unsigned long long)operand->value);
                case XXBYTE_INDEX_LOCAL:
                case XXBYTE_INDEX_ARGUMENT:
                case XXBYTE_IFPS_INDEX_ELEMENT_LOCAL:
                case XXBYTE_IFPS_INDEX_ELEMENT_ARGUMENT: return xxbyte_fmt_printf(fmt, "Base[%lld]", (long long)operand->value);
                default: return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
            }
        case XXBYTE_OPERAND_BRANCH: return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
        default:
            if (is_literal(operand)) {
                return format_immediate(instruction, operand, fmt);
            }
            if (instruction->opcode == XXBYTE_IFPS_OP_PUSHEXCEPTION) {
                return xxbyte_fmt_str(fmt, "none");
            }
            return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
    }
}

/* One operand; an immediate in brackets as uPSDisassembly prints it:
 * "[5]", "['Setup']", "[PROC: 12]". */
static int format_operand(const xxbyte_instruction *instruction, uint32_t index, xxbyte_fmt *fmt)
{
    const xxbyte_operand *operand = &instruction->operands[index];

    if (!is_literal(operand)) {
        return format_operand_text(instruction, index, fmt);
    }
    return xxbyte_fmt_char(fmt, '[') && (operand->type != XXBYTE_OPERAND_INDEX || xxbyte_fmt_str(fmt, "PROC: ")) && format_operand_text(instruction, index, fmt) &&
           xxbyte_fmt_char(fmt, ']');
}

/* A variable and its element selector, if any: "Base[1].[2]" (form 2),
 * "Base[1].GlobalVar[0]" (form 3). */
static int format_variable(const xxbyte_instruction *instruction, uint32_t *index, xxbyte_fmt *fmt)
{
    uint32_t i = *index;
    int constant;

    if (i >= instruction->operand_count || !format_operand(instruction, i, fmt)) {
        return 0;
    }
    ++i;
    if (i < instruction->operand_count && is_element(&instruction->operands[i])) {
        constant = instruction->operands[i].kind == XXBYTE_IFPS_INDEX_ELEMENT;
        if (!xxbyte_fmt_str(fmt, constant ? ".[" : ".") || !format_operand(instruction, i, fmt) || (constant && !xxbyte_fmt_char(fmt, ']'))) {
            return 0;
        }
        ++i;
    }
    *index = i;
    return 1;
}

/* Every operand as a variable, separated by separator. */
static int format_list(const xxbyte_instruction *instruction, const char *separator, xxbyte_fmt *fmt)
{
    uint32_t index = 0;
    int first = 1;

    while (index < instruction->operand_count) {
        if (!xxbyte_fmt_str(fmt, first ? " " : separator) || !format_variable(instruction, &index, fmt)) {
            return 0;
        }
        first = 0;
    }
    return 1;
}

/* calc D op S and compare into R: A op B; operand 0 is the operator. */
static int format_operation(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    int compare = instruction->id == XXBYTE_IFPS_ID_COMPARE;
    uint32_t index = 1;
    uint32_t code;
    const char *symbol;

    if (instruction->operand_count == 0) {
        return 0;
    }
    code = (uint32_t)instruction->operands[0].value;
    symbol = compare ? xxbyte_ifps_compare_operator(code) : xxbyte_ifps_calc_operator(code);
    if (symbol == NULL || !xxbyte_fmt_char(fmt, ' ')) {
        return 0;
    }
    if (compare && (!xxbyte_fmt_str(fmt, "into ") || !format_variable(instruction, &index, fmt) || !xxbyte_fmt_str(fmt, ": "))) {
        return 0;
    }
    return format_variable(instruction, &index, fmt) && xxbyte_fmt_printf(fmt, " %s ", symbol) && format_variable(instruction, &index, fmt) &&
           index == instruction->operand_count;
}

/* pushtype t(BaseType): the base type when the decode context had type t,
 * named as BT2S names it ("Unknown n" for a base type it does not know). */
static int format_pushtype(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint64_t extra;
    uint32_t base_type;
    const char *name;

    if (instruction->operand_count != 1 || !format_list(instruction, ", ", fmt)) {
        return 0;
    }
    extra = instruction->operands[0].extra;
    if ((extra & XXBYTE_IFPS_EXTRA_HAS_TYPE) == 0) {
        return 1;
    }
    base_type = XXBYTE_IFPS_EXTRA_BASE_TYPE(extra);
    name = xxbyte_ifps_base_type_name(base_type);
    return name != NULL ? xxbyte_fmt_printf(fmt, "(%s)", name) : xxbyte_fmt_printf(fmt, "(Unknown %u)", (unsigned)base_type);
}

/* setflag V or setflag not V: the negate byte is the last operand. */
static int format_setflag(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const xxbyte_operand *negate;
    uint32_t index = 0;

    if (instruction->operand_count < 2) {
        return 0;
    }
    negate = &instruction->operands[instruction->operand_count - 1];
    return xxbyte_fmt_str(fmt, negate->value != 0 ? " not " : " ") && format_variable(instruction, &index, fmt) && index + 1 == instruction->operand_count;
}

static int ifps_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    switch (instruction->id) {
        case XXBYTE_IFPS_ID_CALC:
        case XXBYTE_IFPS_ID_COMPARE: return format_operation(instruction, fmt);
        case XXBYTE_IFPS_ID_SETFLAG: return format_setflag(instruction, fmt);
        case XXBYTE_IFPS_ID_SETPOINTER:
        case XXBYTE_IFPS_ID_SETCOPYPOINTER: return format_list(instruction, ": ", fmt);
        case XXBYTE_IFPS_ID_SETSTACKTYPE:
        case XXBYTE_IFPS_ID_PUSHEXCEPTION: return format_list(instruction, " ", fmt);
        case XXBYTE_IFPS_ID_PUSHTYPE: return format_pushtype(instruction, fmt);
        default: return format_list(instruction, ", ", fmt);
    }
}

const xxbyte_family_ops xxbyte_ifps_ops = {
    XXBYTE_FAMILY_IFPS, XXBYTE_IFPS_MODE_DEFAULT, xxbyte_ifps_check_mode, ifps_decode, NULL, ifps_format_operands, NULL, xxbyte_ifps_list_file,
};

/* -------------------------------------------------------------- helpers */

const char *xxbyte_ifps_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_IFPS_ID_COUNT) {
        return NULL;
    }
    return mnemonics[id - 1];
}

const char *xxbyte_ifps_base_type_name(uint32_t base_type)
{
    return base_type < COUNT_OF(base_type_names) ? base_type_names[base_type] : NULL;
}

const char *xxbyte_ifps_calc_operator(uint32_t calc)
{
    return calc < COUNT_OF(calc_operators) ? calc_operators[calc] : NULL;
}

const char *xxbyte_ifps_compare_operator(uint32_t compare)
{
    return compare < COUNT_OF(compare_operators) ? compare_operators[compare] : NULL;
}

void xxbyte_ifps_init_context(xxbyte_ifps_context *context, const xxbyte_ifps_type *types, size_t count)
{
    if (context == NULL) {
        return;
    }
    context->types = types;
    context->type_count = types == NULL ? 0u : (count > UINT32_MAX ? UINT32_MAX : (uint32_t)count);
}

/* ------------------------------------------------------------ container */

typedef struct ifps_reader {
    const uint8_t *data;
    uint64_t size;
    uint64_t pos;
    uint32_t mode;
} ifps_reader;

static int rd_has(const ifps_reader *r, uint64_t count)
{
    return count <= r->size - r->pos;
}

static xxbyte_status rd_u8(ifps_reader *r, uint32_t *value)
{
    if (!rd_has(r, 1)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *value = r->data[r->pos++];
    return XXBYTE_STATUS_OK;
}

static xxbyte_status rd_u32(ifps_reader *r, uint32_t *value)
{
    if (!rd_has(r, 4)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *value = xxbyte_u32le(r->data + r->pos);
    r->pos += 4;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status rd_skip(ifps_reader *r, uint64_t count)
{
    if (!rd_has(r, count)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    r->pos += count;
    return XXBYTE_STATUS_OK;
}

/* A 32-bit length, then that many bytes; limit bounds the length as the
 * runtime does (a negative Longint is invalid). */
static xxbyte_status rd_name(ifps_reader *r, uint32_t limit, uint32_t *offset, uint32_t *size)
{
    uint32_t length;
    xxbyte_status status = rd_u32(r, &length);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (length > limit) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *offset = (uint32_t)r->pos;
    *size = length;
    return rd_skip(r, length);
}

/* The caller's type array: the stored types size attribute values. */
typedef struct ifps_types {
    const xxbyte_ifps_type *stored;
    size_t capacity;
} ifps_types;

static xxbyte_status attribute_value(ifps_reader *r, const xxbyte_ifps_type *type)
{
    uint32_t length;
    xxbyte_status status;

    switch (type->base_type) {
        case XXBYTE_IFPS_BT_SET: return rd_skip(r, (type->set_bits + 7u) / 8u);
        case XXBYTE_IFPS_BT_S8:
        case XXBYTE_IFPS_BT_CHAR:
        case XXBYTE_IFPS_BT_U8: return rd_skip(r, 1);
        case XXBYTE_IFPS_BT_S16:
        case XXBYTE_IFPS_BT_WIDE_CHAR:
        case XXBYTE_IFPS_BT_U16: return rd_skip(r, 2);
        case XXBYTE_IFPS_BT_S32:
        case XXBYTE_IFPS_BT_U32:
        case XXBYTE_IFPS_BT_PROC_PTR:
        case XXBYTE_IFPS_BT_SINGLE: return rd_skip(r, 4);
        case XXBYTE_IFPS_BT_S64:
        case XXBYTE_IFPS_BT_U64:
        case XXBYTE_IFPS_BT_DOUBLE:
        case XXBYTE_IFPS_BT_CURRENCY: return rd_skip(r, 8);
        case XXBYTE_IFPS_BT_EXTENDED: return rd_skip(r, xxbyte_ifps_extended_size(r->mode));
        case XXBYTE_IFPS_BT_PCHAR:
        case XXBYTE_IFPS_BT_STRING:
        case XXBYTE_IFPS_BT_WIDE_STRING:
        case XXBYTE_IFPS_BT_UNICODE_STRING:
            status = rd_u32(r, &length);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (length > MAX_LONGINT) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            return rd_skip(r, (type->base_type == XXBYTE_IFPS_BT_PCHAR || type->base_type == XXBYTE_IFPS_BT_STRING) ? (uint64_t)length : (uint64_t)length * 2u);
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
}

/*
 * An attribute list (ReadAttributes in TPSExec.LoadData): a count, then
 * per attribute a name and a field count, and per field a type number
 * below defined and a value sized by that type.
 */
static xxbyte_status read_attributes(ifps_reader *r, const ifps_types *types, uint32_t defined, uint32_t *offset, uint32_t *count)
{
    uint32_t attributes;
    uint32_t i;
    xxbyte_status status;

    *offset = (uint32_t)r->pos;
    status = rd_u32(r, &attributes);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    *count = attributes;
    for (i = 0; i < attributes; ++i) {
        uint32_t name_offset;
        uint32_t name_size;
        uint32_t fields;
        uint32_t j;

        status = rd_name(r, MAX_LONGINT, &name_offset, &name_size);
        if (status == XXBYTE_STATUS_OK) {
            status = rd_u32(r, &fields);
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        /* a negative Longint field count reads no fields */
        if (fields > MAX_LONGINT) {
            fields = 0;
        }
        for (j = 0; j < fields; ++j) {
            uint32_t number;

            status = rd_u32(r, &number);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (number >= defined) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (number >= types->capacity) {
                return XXBYTE_STATUS_INVALID_ARGUMENT;
            }
            status = attribute_value(r, &types->stored[number]);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
        }
    }
    return XXBYTE_STATUS_OK;
}

/* One type record up to its attributes; defined is the number of types
 * before it (element and field types must be below it). */
static xxbyte_status read_type(ifps_reader *r, uint32_t build, uint32_t defined, xxbyte_ifps_type *type)
{
    uint32_t stored;
    uint32_t value = 0;
    uint32_t i;
    xxbyte_status status;

    memset(type, 0, sizeof(*type));
    type->offset = (uint32_t)r->pos;
    status = rd_u8(r, &stored);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    type->base_type = (uint8_t)(stored & 0x7Fu);
    if ((stored & 0x80u) != 0) {
        type->flags |= XXBYTE_IFPS_TYPE_EXPORTED;
    }
    switch (type->base_type) {
        case XXBYTE_IFPS_BT_S64:
        case XXBYTE_IFPS_BT_U64:
        case XXBYTE_IFPS_BT_U8:
        case XXBYTE_IFPS_BT_S8:
        case XXBYTE_IFPS_BT_U16:
        case XXBYTE_IFPS_BT_S16:
        case XXBYTE_IFPS_BT_U32:
        case XXBYTE_IFPS_BT_S32:
        case XXBYTE_IFPS_BT_SINGLE:
        case XXBYTE_IFPS_BT_DOUBLE:
        case XXBYTE_IFPS_BT_CURRENCY:
        case XXBYTE_IFPS_BT_EXTENDED:
        case XXBYTE_IFPS_BT_STRING:
        case XXBYTE_IFPS_BT_POINTER:
        case XXBYTE_IFPS_BT_PCHAR:
        case XXBYTE_IFPS_BT_VARIANT:
        case XXBYTE_IFPS_BT_CHAR:
        case XXBYTE_IFPS_BT_UNICODE_STRING:
        case XXBYTE_IFPS_BT_WIDE_STRING:
        case XXBYTE_IFPS_BT_WIDE_CHAR: break;
        case XXBYTE_IFPS_BT_CLASS:
        case XXBYTE_IFPS_BT_PROC_PTR: status = rd_name(r, 255u, &type->data_offset, &type->data_size); break;
        case XXBYTE_IFPS_BT_INTERFACE:
            type->data_offset = (uint32_t)r->pos;
            type->data_size = 16;
            status = rd_skip(r, 16);
            break;
        case XXBYTE_IFPS_BT_SET:
            status = rd_u32(r, &type->set_bits);
            if (status == XXBYTE_STATUS_OK && type->set_bits > MAX_SET_BITS) {
                status = XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            break;
        case XXBYTE_IFPS_BT_STATIC_ARRAY:
        case XXBYTE_IFPS_BT_ARRAY:
            status = rd_u32(r, &type->element_type);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (type->element_type >= defined) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (type->base_type == XXBYTE_IFPS_BT_ARRAY) {
                break;
            }
            status = rd_u32(r, &type->count);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (type->count > MAX_LONGINT / 4u) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            /* the low bound came with build 23 */
            if (build >= 23u) {
                status = rd_u32(r, &value);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                type->start_index = (int32_t)xxbyte_sext(value, 32);
            }
            break;
        case XXBYTE_IFPS_BT_RECORD:
            status = rd_u32(r, &type->count);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (type->count == 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            type->data_offset = (uint32_t)r->pos;
            for (i = 0; i < type->count; ++i) {
                status = rd_u32(r, &value);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                if (value >= defined) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
            }
            type->data_size = (uint32_t)(r->pos - type->data_offset);
            break;
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (status == XXBYTE_STATUS_OK && (type->flags & XXBYTE_IFPS_TYPE_EXPORTED) != 0) {
        status = rd_name(r, XXBYTE_IFPS_ADDR_STACK_LIMIT, &type->name_offset, &type->name_size);
    }
    return status;
}

static xxbyte_status read_proc(ifps_reader *r, const ifps_types *types, uint32_t type_count, xxbyte_ifps_proc *proc)
{
    uint32_t flags;
    uint32_t length;
    xxbyte_status status;

    memset(proc, 0, sizeof(*proc));
    proc->offset = (uint32_t)r->pos;
    status = rd_u8(r, &flags);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    proc->flags = (uint8_t)flags;
    if ((flags & XXBYTE_IFPS_PROC_EXTERNAL) != 0) {
        status = rd_u8(r, &length);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        proc->name_offset = (uint32_t)r->pos;
        proc->name_size = length;
        status = rd_skip(r, length);
        if (status == XXBYTE_STATUS_OK && (flags & XXBYTE_IFPS_PROC_EXPORTED) != 0) {
            status = rd_name(r, MAX_LONGINT, &proc->decl_offset, &proc->decl_size);
        }
    } else {
        status = rd_u32(r, &proc->code_offset);
        if (status == XXBYTE_STATUS_OK) {
            status = rd_u32(r, &proc->code_size);
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (proc->code_offset > MAX_LONGINT || proc->code_size == 0 || proc->code_size > MAX_LONGINT) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if ((uint64_t)proc->code_offset + proc->code_size > r->size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if ((flags & XXBYTE_IFPS_PROC_EXPORTED) != 0) {
            status = rd_name(r, XXBYTE_IFPS_ADDR_STACK_LIMIT, &proc->name_offset, &proc->name_size);
            if (status == XXBYTE_STATUS_OK) {
                status = rd_name(r, XXBYTE_IFPS_ADDR_STACK_LIMIT, &proc->decl_offset, &proc->decl_size);
            }
        }
    }
    if (status == XXBYTE_STATUS_OK && (flags & XXBYTE_IFPS_PROC_ATTRIBUTES) != 0) {
        status = read_attributes(r, types, type_count, &proc->attributes_offset, &proc->attribute_count);
    }
    return status;
}

static xxbyte_status read_var(ifps_reader *r, uint32_t type_count, xxbyte_ifps_var *var)
{
    uint32_t flags;
    xxbyte_status status;

    memset(var, 0, sizeof(*var));
    var->offset = (uint32_t)r->pos;
    status = rd_u32(r, &var->type);
    if (status == XXBYTE_STATUS_OK) {
        status = rd_u8(r, &flags);
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    var->flags = (uint8_t)flags;
    if (var->type >= type_count) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if ((flags & XXBYTE_IFPS_VAR_EXPORTED) != 0) {
        status = rd_name(r, MAX_LONGINT, &var->name_offset, &var->name_size);
    }
    return status;
}

xxbyte_status xxbyte_ifps_parse_header(const uint8_t *data, size_t size, xxbyte_ifps_script *script)
{
    if (script == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(script, 0, sizeof(*script));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < XXBYTE_IFPS_HEADER_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (xxbyte_u32le(data) != XXBYTE_IFPS_SIGNATURE) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    script->build = xxbyte_u32le(data + 4);
    script->type_count = xxbyte_u32le(data + 8);
    script->proc_count = xxbyte_u32le(data + 12);
    script->var_count = xxbyte_u32le(data + 16);
    script->main_proc = xxbyte_u32le(data + 20);
    script->import_table_size = xxbyte_u32le(data + 24);
    script->types_offset = XXBYTE_IFPS_HEADER_SIZE;
    if (script->build < XXBYTE_IFPS_BUILD_MIN || script->build > XXBYTE_IFPS_BUILD_MAX) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_ifps_parse_script(const uint8_t *data, size_t size, uint32_t mode, xxbyte_ifps_script *script, xxbyte_ifps_type *types, size_t type_capacity,
                                       xxbyte_ifps_proc *procs, size_t proc_capacity, xxbyte_ifps_var *vars, size_t var_capacity)
{
    ifps_reader reader;
    ifps_types stored;
    xxbyte_ifps_script parsed;
    xxbyte_status status;
    uint32_t i;

    if (script != NULL) {
        memset(script, 0, sizeof(*script));
    }
    if (script == NULL || data == NULL || (types == NULL && type_capacity != 0) || (procs == NULL && proc_capacity != 0) || (vars == NULL && var_capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (xxbyte_ifps_check_mode(mode) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    status = xxbyte_ifps_parse_header(data, size, &parsed);
    *script = parsed;
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    reader.data = data;
    /* every offset is 32-bit: the runtime's positions are Longints */
    reader.size = (uint64_t)size > UINT32_MAX ? UINT32_MAX : (uint64_t)size;
    reader.pos = XXBYTE_IFPS_HEADER_SIZE;
    reader.mode = mode;
    stored.stored = types;
    stored.capacity = type_capacity;

    for (i = 0; i < parsed.type_count; ++i) {
        xxbyte_ifps_type type;
        status = read_type(&reader, parsed.build, i, &type);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (i < type_capacity) {
            types[i] = type;
        }
        /* attributes came with build 21 and may use the type itself */
        if (parsed.build >= 21u) {
            status = read_attributes(&reader, &stored, i + 1u, &type.attributes_offset, &type.attribute_count);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (i < type_capacity) {
                types[i].attributes_offset = type.attributes_offset;
                types[i].attribute_count = type.attribute_count;
            }
        }
    }
    parsed.procs_offset = (uint32_t)reader.pos;
    script->procs_offset = parsed.procs_offset;
    for (i = 0; i < parsed.proc_count; ++i) {
        xxbyte_ifps_proc proc;
        status = read_proc(&reader, &stored, parsed.type_count, &proc);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (i < proc_capacity) {
            procs[i] = proc;
        }
    }
    parsed.vars_offset = (uint32_t)reader.pos;
    script->vars_offset = parsed.vars_offset;
    for (i = 0; i < parsed.var_count; ++i) {
        xxbyte_ifps_var var;
        status = read_var(&reader, parsed.type_count, &var);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (i < var_capacity) {
            vars[i] = var;
        }
    }
    script->end_offset = (uint32_t)reader.pos;
    if (parsed.main_proc >= parsed.proc_count && parsed.main_proc != XXBYTE_IFPS_NONE) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return XXBYTE_STATUS_OK;
}

#else
typedef int xxbyte_ifps_disabled;
#endif
