/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_ebc_file.h"
#include "xxbyte_internal.h"

#if XXBYTE_USE_EBC

/*
 * EFI Byte Code, UEFI 2.10 chapter 22. Byte 0 holds the opcode in bits 0-5
 * and two modifier bits; most instructions follow it with an operands byte
 * (operand 1 register in bits 0-2, indirect bit 3, operand 2 register in
 * bits 4-6, indirect bit 7) and optional immediates or natural indexes.
 *
 * An encoding is valid when the specification defines it with every
 * reserved bit clear and neither EDK II's interpreter (EbcExecute.c) nor its
 * debugger (EdbDisasm.c) rejects it.
 */

/*
 * The family operand flags must stay clear of the core's. The core reserves
 * no family range for operand flags yet (see xxbyte_ebc.h); these checks
 * turn a future collision into a compile error instead of silently changed
 * text and structure.
 */
#define EBC_CORE_OPERAND_FLAGS \
    (XXBYTE_OPERAND_FLAG_SIGNED | XXBYTE_OPERAND_FLAG_IMPLICIT | XXBYTE_OPERAND_FLAG_LIST | XXBYTE_OPERAND_FLAG_WIDE | XXBYTE_OPERAND_FLAG_HIGH16)
typedef char xxbyte_ebc_flags_clear_of_core[(XXBYTE_EBC_OPERAND_FLAG_MASK & EBC_CORE_OPERAND_FLAGS) == 0 ? 1 : -1];
typedef char xxbyte_ebc_flags_fit[(XXBYTE_EBC_OPERAND_FLAG_MASK & ~0xFFFFu) == 0 ? 1 : -1];
#ifdef XXBYTE_OPERAND_FLAG_FAMILY_FIRST
typedef char xxbyte_ebc_flags_in_family_range
    [XXBYTE_EBC_OPERAND_FLAG_INDEX >= XXBYTE_OPERAND_FLAG_FAMILY_FIRST && XXBYTE_EBC_OPERAND_FLAG_NEGATIVE >= XXBYTE_OPERAND_FLAG_FAMILY_FIRST ? 1 : -1];
#endif

#define G_NONE XXBYTE_GROUP_NONE
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_CALL XXBYTE_GROUP_CALL
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)

/* Instruction kinds by the layout of their operands. */
enum {
    K_UNDEFINED,
    K_BREAK,
    K_JMP,
    K_JMP8,
    K_CALL,
    K_RET,
    K_CMP,   /* R1, {@}R2 {Index16|Immed16} */
    K_ARITH, /* {@}R1, {@}R2 {Index16|Immed16} */
    K_MOV,   /* {@}R1 {Index}, {@}R2 {Index} */
    K_MOVSN, /* {@}R1 {Index}, {@}R2 {Index|Immed} */
    K_LOADSP,
    K_STORESP,
    K_PUSHPOP, /* PUSH/POP and PUSHn/POPn: {@}R1 {Index16|Immed16} */
    K_CMPI,
    K_MOVI,
    K_MOVIN,
    K_MOVREL
};

typedef struct ebc_opcode {
    uint8_t kind;
    uint8_t id;    /* first id of the opcode's mnemonics */
    uint8_t width; /* MOV move width (0 = natural); EXTNDx source width */
    uint8_t index; /* MOV/MOVsn index width */
} ebc_opcode;

#define ID(name) XXBYTE_EBC_ID_##name

static const ebc_opcode opcodes[64] = {
    [0x00] = {K_BREAK, ID(BREAK), 0, 0},      [0x01] = {K_JMP, ID(JMP32), 0, 0},       [0x02] = {K_JMP8, ID(JMP8), 0, 0},       [0x03] = {K_CALL, ID(CALL32), 0, 0},
    [0x04] = {K_RET, ID(RET), 0, 0},          [0x05] = {K_CMP, ID(CMP32EQ), 0, 0},     [0x06] = {K_CMP, ID(CMP32LTE), 0, 0},    [0x07] = {K_CMP, ID(CMP32GTE), 0, 0},
    [0x08] = {K_CMP, ID(CMP32ULTE), 0, 0},    [0x09] = {K_CMP, ID(CMP32UGTE), 0, 0},   [0x0A] = {K_ARITH, ID(NOT32), 0, 0},     [0x0B] = {K_ARITH, ID(NEG32), 0, 0},
    [0x0C] = {K_ARITH, ID(ADD32), 0, 0},      [0x0D] = {K_ARITH, ID(SUB32), 0, 0},     [0x0E] = {K_ARITH, ID(MUL32), 0, 0},     [0x0F] = {K_ARITH, ID(MULU32), 0, 0},
    [0x10] = {K_ARITH, ID(DIV32), 0, 0},      [0x11] = {K_ARITH, ID(DIVU32), 0, 0},    [0x12] = {K_ARITH, ID(MOD32), 0, 0},     [0x13] = {K_ARITH, ID(MODU32), 0, 0},
    [0x14] = {K_ARITH, ID(AND32), 0, 0},      [0x15] = {K_ARITH, ID(OR32), 0, 0},      [0x16] = {K_ARITH, ID(XOR32), 0, 0},     [0x17] = {K_ARITH, ID(SHL32), 0, 0},
    [0x18] = {K_ARITH, ID(SHR32), 0, 0},      [0x19] = {K_ARITH, ID(ASHR32), 0, 0},    [0x1A] = {K_ARITH, ID(EXTNDB32), 1, 0},  [0x1B] = {K_ARITH, ID(EXTNDW32), 2, 0},
    [0x1C] = {K_ARITH, ID(EXTNDD32), 4, 0},   [0x1D] = {K_MOV, ID(MOVBW), 1, 2},       [0x1E] = {K_MOV, ID(MOVWW), 2, 2},       [0x1F] = {K_MOV, ID(MOVDW), 4, 2},
    [0x20] = {K_MOV, ID(MOVQW), 8, 2},        [0x21] = {K_MOV, ID(MOVBD), 1, 4},       [0x22] = {K_MOV, ID(MOVWD), 2, 4},       [0x23] = {K_MOV, ID(MOVDD), 4, 4},
    [0x24] = {K_MOV, ID(MOVQD), 8, 4},        [0x25] = {K_MOVSN, ID(MOVSNW), 0, 2},    [0x26] = {K_MOVSN, ID(MOVSND), 0, 4},    [0x28] = {K_MOV, ID(MOVQQ), 8, 8},
    [0x29] = {K_LOADSP, ID(LOADSP), 0, 0},    [0x2A] = {K_STORESP, ID(STORESP), 0, 0}, [0x2B] = {K_PUSHPOP, ID(PUSH32), 0, 0},  [0x2C] = {K_PUSHPOP, ID(POP32), 0, 0},
    [0x2D] = {K_CMPI, ID(CMPI32WEQ), 0, 0},   [0x2E] = {K_CMPI, ID(CMPI32WLTE), 0, 0}, [0x2F] = {K_CMPI, ID(CMPI32WGTE), 0, 0}, [0x30] = {K_CMPI, ID(CMPI32WULTE), 0, 0},
    [0x31] = {K_CMPI, ID(CMPI32WUGTE), 0, 0}, [0x32] = {K_MOV, ID(MOVNW), 0, 2},       [0x33] = {K_MOV, ID(MOVND), 0, 4},       [0x35] = {K_PUSHPOP, ID(PUSHN), 0, 0},
    [0x36] = {K_PUSHPOP, ID(POPN), 0, 0},     [0x37] = {K_MOVI, ID(MOVIBW), 0, 0},     [0x38] = {K_MOVIN, ID(MOVINW), 0, 0},    [0x39] = {K_MOVREL, ID(MOVRELW), 0, 0},
};

#define OP_PUSHN 0x35u
#define OP_POPN 0x36u

/* Indexed by id - 1. */
static const char *const mnemonics[XXBYTE_EBC_ID_COUNT - 1] = {
    "BREAK",      "JMP32",      "JMP32a",      "JMP32cc",     "JMP32cca",    "JMP32cs",    "JMP32csa",   "JMP64",       "JMP64a",      "JMP64cc",     "JMP64cca",
    "JMP64cs",    "JMP64csa",   "JMP8",        "JMP8cc",      "JMP8cs",      "CALL32",     "CALL32a",    "CALL32EX",    "CALL32EXa",   "CALL64a",     "CALL64EXa",
    "RET",        "CMP32eq",    "CMP32lte",    "CMP32gte",    "CMP32ulte",   "CMP32ugte",  "CMP64eq",    "CMP64lte",    "CMP64gte",    "CMP64ulte",   "CMP64ugte",
    "NOT32",      "NOT64",      "NEG32",       "NEG64",       "ADD32",       "ADD64",      "SUB32",      "SUB64",       "MUL32",       "MUL64",       "MULU32",
    "MULU64",     "DIV32",      "DIV64",       "DIVU32",      "DIVU64",      "MOD32",      "MOD64",      "MODU32",      "MODU64",      "AND32",       "AND64",
    "OR32",       "OR64",       "XOR32",       "XOR64",       "SHL32",       "SHL64",      "SHR32",      "SHR64",       "ASHR32",      "ASHR64",      "EXTNDB32",
    "EXTNDB64",   "EXTNDW32",   "EXTNDW64",    "EXTNDD32",    "EXTNDD64",    "MOVbw",      "MOVww",      "MOVdw",       "MOVqw",       "MOVbd",       "MOVwd",
    "MOVdd",      "MOVqd",      "MOVqq",       "MOVsnw",      "MOVsnd",      "LOADSP",     "STORESP",    "PUSH32",      "PUSH64",      "POP32",       "POP64",
    "CMPI32weq",  "CMPI32wlte", "CMPI32wgte",  "CMPI32wulte", "CMPI32wugte", "CMPI32deq",  "CMPI32dlte", "CMPI32dgte",  "CMPI32dulte", "CMPI32dugte", "CMPI64weq",
    "CMPI64wlte", "CMPI64wgte", "CMPI64wulte", "CMPI64wugte", "CMPI64deq",   "CMPI64dlte", "CMPI64dgte", "CMPI64dulte", "CMPI64dugte", "MOVnw",       "MOVnd",
    "PUSHn",      "POPn",       "MOVIbw",      "MOVIbd",      "MOVIbq",      "MOVIww",     "MOVIwd",     "MOVIwq",      "MOVIdw",      "MOVIdd",      "MOVIdq",
    "MOVIqw",     "MOVIqd",     "MOVIqq",      "MOVInw",      "MOVInd",      "MOVInq",     "MOVRELw",    "MOVRELd",     "MOVRELq",
};

/* Immediate widths selected by the two modifier bits of MOVI, MOVIn and
 * MOVREL (0 is reserved). */
static const uint8_t data_widths[4] = {0, 2, 4, 8};

static xxbyte_status ebc_check_mode(uint32_t mode)
{
    return mode == XXBYTE_EBC_MODE_DEFAULT ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static uint64_t read_le(const uint8_t *p, uint32_t width)
{
    switch (width) {
        case 2: return xxbyte_u16le(p);
        case 4: return xxbyte_u32le(p);
        default: return xxbyte_u64le(p);
    }
}

/* Splits a natural index; 0 when its natural-units field does not fit. */
static int split_index(uint64_t raw, uint32_t width, xxbyte_ebc_index *index)
{
    uint32_t bits = width * 8u;
    uint32_t natural_bits = (uint32_t)((raw >> (bits - 4u)) & 7u) * width;
    uint32_t constant_bits;

    if (natural_bits > bits - 4u) {
        return 0;
    }
    constant_bits = bits - 4u - natural_bits;
    index->negative = (uint32_t)((raw >> (bits - 1u)) & 1u);
    index->natural = raw & ((UINT64_C(1) << natural_bits) - 1u);
    index->constant = (raw >> natural_bits) & ((UINT64_C(1) << constant_bits) - 1u);
    return 1;
}

static void set_mnemonic(xxbyte_instruction *instruction, uint32_t id, uint32_t group, uint32_t size)
{
    instruction->opcode = instruction->code[0];
    instruction->id = id;
    instruction->group = group;
    instruction->mnemonic = mnemonics[id - 1];
    instruction->size = size;
}

static void add_register(xxbyte_instruction *instruction, uint32_t kind, uint32_t number)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_REGISTER, 1, (int64_t)number);
    if (operand != NULL) {
        operand->kind = (uint8_t)kind;
    }
}

static void add_immediate(xxbyte_instruction *instruction, uint32_t size, int64_t value, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, size, value);
    if (operand != NULL) {
        operand->flags = flags;
    }
}

/* Stores a natural index as value = signed constant units and extra =
 * signed natural units, and marks the operand INDEX (and NEGATIVE when the
 * sign bit is set, which keeps "(-0,-0)" apart from "(+0,+0)"). */
static void set_natural(xxbyte_operand *operand, const xxbyte_ebc_index *index)
{
    int64_t constant = (int64_t)index->constant;
    int64_t natural = (int64_t)index->natural;

    operand->flags = (uint16_t)(operand->flags | XXBYTE_EBC_OPERAND_FLAG_INDEX);
    if (index->negative != 0) {
        constant = -constant;
        natural = -natural;
        operand->flags = (uint16_t)(operand->flags | XXBYTE_EBC_OPERAND_FLAG_NEGATIVE);
    }
    operand->value = constant;
    operand->extra = (uint64_t)natural;
}

/* @Rn with an optional natural index of index_width bytes at p. */
static xxbyte_status add_memory(xxbyte_instruction *instruction, uint32_t base, const uint8_t *p, uint32_t index_width, uint32_t access_width)
{
    xxbyte_ebc_index index;
    xxbyte_operand *operand;

    if (index_width != 0 && !split_index(read_le(p, index_width), index_width, &index)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_MEMORY, access_width, 0);
    if (operand != NULL) {
        operand->kind = (uint8_t)base;
        if (index_width != 0) {
            set_natural(operand, &index);
        }
    }
    return XXBYTE_STATUS_OK;
}

/* A natural index used as a value: MOV operand 2 direct, MOVIn data. */
static xxbyte_status add_natural_immediate(xxbyte_instruction *instruction, const uint8_t *p, uint32_t width)
{
    xxbyte_ebc_index index;
    xxbyte_operand *operand;

    if (!split_index(read_le(p, width), width, &index)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, width, 0);
    if (operand != NULL) {
        operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
        set_natural(operand, &index);
    }
    return XXBYTE_STATUS_OK;
}

static int64_t wrap_add(uint64_t a, uint64_t b)
{
    return (int64_t)(a + b);
}

static xxbyte_status decode_break(const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    if ((code[0] & 0xC0u) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    /* Codes 0, 1 and 3-6; BREAK 0 is the runaway-program trap. */
    if (code[1] > 6 || code[1] == 2) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    set_mnemonic(instruction, XXBYTE_EBC_ID_BREAK, code[1] == 0 ? G_THROW : G_NONE, 2);
    add_immediate(instruction, 1, code[1], 0);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status decode_jmp8(const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    uint32_t condition = code[0] >> 6;
    uint32_t id = XXBYTE_EBC_ID_JMP8;

    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if ((condition & 2u) != 0) {
        id += (condition & 1u) != 0 ? 2u : 1u;
    }
    set_mnemonic(instruction, id, (condition & 2u) != 0 ? G_COND : G_JUMP, 2);
    xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, 1, wrap_add(address + 2u, (uint64_t)(xxbyte_sext(code[1], 8) * 2)));
    return XXBYTE_STATUS_OK;
}

/*
 * JMP and CALL. Modifier bit 7: immediate data present, bit 6: 64-bit
 * immediate (which bit 7 must accompany). Operands byte: JMP bit 7
 * conditional, bit 6 condition set, bit 5 reserved; CALL bits 6-7 reserved,
 * bit 5 native (EX); both bit 4 relative.
 */
static xxbyte_status decode_jmp_call(const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    static const uint8_t sizes[4] = {2, 0, 6, 10};
    uint32_t modifiers = code[0] >> 6;
    int is_call = (code[0] & 0x3Fu) == 0x03u;
    uint32_t operands;
    uint32_t size = sizes[modifiers];
    uint32_t id;
    uint32_t group;
    int relative;
    uint32_t reg;

    if (modifiers == 1) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    operands = code[1];
    relative = (operands & 0x10u) != 0;
    if (is_call) {
        if ((operands & 0xC0u) != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (modifiers == 3) {
            /* CALL64 is always absolute; bit 4 is ignored. */
            relative = 0;
            id = XXBYTE_EBC_ID_CALL64A + ((operands & 0x20u) != 0 ? 1u : 0u);
        } else {
            id = XXBYTE_EBC_ID_CALL32 + ((operands & 0x20u) != 0 ? 2u : 0u) + (relative ? 0u : 1u);
        }
        group = G_CALL;
    } else {
        if ((operands & 0x20u) != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        id = XXBYTE_EBC_ID_JMP32 + (modifiers == 3 ? 6u : 0u) + (relative ? 0u : 1u);
        if ((operands & 0x80u) != 0) {
            id += (operands & 0x40u) != 0 ? 4u : 2u;
            group = G_COND;
        } else {
            group = G_JUMP;
        }
    }
    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    reg = operands & 7u;
    if (modifiers == 3) {
        uint64_t immediate = xxbyte_u64le(code + 2);
        set_mnemonic(instruction, id, group, size);
        xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, 8, relative ? wrap_add(address + size, immediate) : (int64_t)immediate);
        return XXBYTE_STATUS_OK;
    }
    if ((operands & 0x08u) != 0) {
        xxbyte_status status = add_memory(instruction, reg, code + 2, modifiers == 2 ? 4u : 0u, 0);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    } else {
        int64_t immediate = modifiers == 2 ? xxbyte_sext(xxbyte_u32le(code + 2), 32) : 0;
        if (reg == 0) {
            /* The register is ignored: a fixed target. */
            xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, modifiers == 2 ? 4u : 0u, relative ? wrap_add(address + size, (uint64_t)immediate) : immediate);
        } else {
            add_register(instruction, XXBYTE_EBC_REGISTER_GENERAL, reg);
            if (modifiers == 2) {
                add_immediate(instruction, 4, immediate, XXBYTE_OPERAND_FLAG_SIGNED);
            }
        }
    }
    set_mnemonic(instruction, id, group, size);
    return XXBYTE_STATUS_OK;
}

/* Operand 2 of CMP and the arithmetic forms, and operand 1 of PUSH/POP:
 * @R {Index16}, or R {Immed16}. */
static xxbyte_status add_source16(xxbyte_instruction *instruction, uint32_t reg, int indirect, int has_data, const uint8_t *p, uint32_t access_width)
{
    if (indirect) {
        return add_memory(instruction, reg, p, has_data ? 2u : 0u, access_width);
    }
    add_register(instruction, XXBYTE_EBC_REGISTER_GENERAL, reg);
    if (has_data) {
        add_immediate(instruction, 2, xxbyte_sext(xxbyte_u16le(p), 16), XXBYTE_OPERAND_FLAG_SIGNED);
    }
    return XXBYTE_STATUS_OK;
}

/* CMP: bit 7 immediate/index, bit 6 64-bit; operand 1 is always direct
 * (operands bit 3 reserved). The arithmetic forms allow @R1. */
static xxbyte_status decode_cmp_arith(const ebc_opcode *op, const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    uint32_t modifiers = code[0] >> 6;
    int has_data = (modifiers & 2u) != 0;
    int wide = (modifiers & 1u) != 0;
    uint32_t width = wide ? 8u : 4u;
    uint32_t size = has_data ? 4u : 2u;
    uint32_t operands;
    uint32_t id = op->id;
    xxbyte_status status;

    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    operands = code[1];
    if (op->kind == K_CMP) {
        if ((operands & 0x08u) != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        id += wide ? 5u : 0u;
    } else {
        id += wide ? 1u : 0u;
    }
    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if ((operands & 0x08u) != 0) {
        status = add_memory(instruction, operands & 7u, NULL, 0, width);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    } else {
        add_register(instruction, XXBYTE_EBC_REGISTER_GENERAL, operands & 7u);
    }
    /* EXTNDB/W/D read a byte, word or dword source. */
    status = add_source16(instruction, (operands >> 4) & 7u, (operands & 0x80u) != 0, has_data, code + 2, op->width != 0 ? op->width : width);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    set_mnemonic(instruction, id, G_NONE, size);
    return XXBYTE_STATUS_OK;
}

/*
 * MOV, MOVn and MOVsn: modifier bit 7 operand 1 index, bit 6 operand 2
 * index (an immediate for a direct MOVsn operand 2). An index on a direct
 * operand 1 is an encoding error.
 */
static xxbyte_status decode_mov(const ebc_opcode *op, const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    uint32_t modifiers = code[0] >> 6;
    uint32_t iw = op->index;
    int has1 = (modifiers & 2u) != 0;
    int has2 = (modifiers & 1u) != 0;
    uint32_t size = 2u + (has1 ? iw : 0u) + (has2 ? iw : 0u);
    uint32_t operands;
    const uint8_t *p2;
    xxbyte_status status;

    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    operands = code[1];
    if (has1 && (operands & 0x08u) == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    p2 = code + 2 + (has1 ? iw : 0u);
    if ((operands & 0x08u) != 0) {
        status = add_memory(instruction, operands & 7u, code + 2, has1 ? iw : 0u, op->width);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    } else {
        add_register(instruction, XXBYTE_EBC_REGISTER_GENERAL, operands & 7u);
    }
    if ((operands & 0x80u) != 0) {
        status = add_memory(instruction, (operands >> 4) & 7u, p2, has2 ? iw : 0u, op->width);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    } else {
        add_register(instruction, XXBYTE_EBC_REGISTER_GENERAL, (operands >> 4) & 7u);
        if (has2) {
            if (op->kind == K_MOVSN) {
                add_immediate(instruction, iw, xxbyte_sext(read_le(p2, iw), iw * 8u), XXBYTE_OPERAND_FLAG_SIGNED);
            } else {
                /* The interpreter decodes a direct MOV operand 2's index
                 * as a natural index too and adds it to the register. */
                status = add_natural_immediate(instruction, p2, iw);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
            }
        }
    }
    set_mnemonic(instruction, op->id, G_NONE, size);
    return XXBYTE_STATUS_OK;
}

/* LOADSP [FLAGS], R2 and STORESP R1, [FLAGS|IP]. Operands bits 3 and 7 are
 * reserved; only FLAGS can be loaded. */
static xxbyte_status decode_sp(const ebc_opcode *op, const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    uint32_t operands;

    if ((code[0] & 0xC0u) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    operands = code[1];
    if ((operands & 0x88u) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (op->kind == K_LOADSP) {
        if ((operands & 7u) != XXBYTE_EBC_DEDICATED_FLAGS) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        add_register(instruction, XXBYTE_EBC_REGISTER_DEDICATED, XXBYTE_EBC_DEDICATED_FLAGS);
        add_register(instruction, XXBYTE_EBC_REGISTER_GENERAL, (operands >> 4) & 7u);
    } else {
        if (((operands >> 4) & 7u) > XXBYTE_EBC_DEDICATED_IP) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        add_register(instruction, XXBYTE_EBC_REGISTER_GENERAL, operands & 7u);
        add_register(instruction, XXBYTE_EBC_REGISTER_DEDICATED, (operands >> 4) & 7u);
    }
    set_mnemonic(instruction, op->id, G_NONE, 2);
    return XXBYTE_STATUS_OK;
}

/* PUSH/POP (bit 6 64-bit) and PUSHn/POPn (bit 6 reserved); bit 7
 * immediate/index. Operands bits 4-7 are reserved. */
static xxbyte_status decode_pushpop(const ebc_opcode *op, const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    uint32_t modifiers = code[0] >> 6;
    uint32_t opcode = code[0] & 0x3Fu;
    int natural = opcode == OP_PUSHN || opcode == OP_POPN;
    uint32_t size = (modifiers & 2u) != 0 ? 4u : 2u;
    uint32_t operands;
    uint32_t id = op->id;
    uint32_t width = 0;
    xxbyte_status status;

    if (natural && (modifiers & 1u) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    operands = code[1];
    if ((operands & 0xF0u) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (!natural) {
        id += (modifiers & 1u) != 0 ? 1u : 0u;
        width = (modifiers & 1u) != 0 ? 8u : 4u;
    }
    status = add_source16(instruction, operands & 7u, (operands & 0x08u) != 0, (modifiers & 2u) != 0, code + 2, width);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    set_mnemonic(instruction, id, G_NONE, size);
    return XXBYTE_STATUS_OK;
}

/* CMPI: bit 7 32-bit immediate, bit 6 64-bit comparison; operands bits 5-7
 * reserved, bit 4 operand 1 index, which only an indirect operand 1 may have
 * (EDK II raises an instruction-encoding exception for a direct one). */
static xxbyte_status decode_cmpi(const ebc_opcode *op, const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    uint32_t modifiers = code[0] >> 6;
    uint32_t data_width = (modifiers & 2u) != 0 ? 4u : 2u;
    uint32_t operands;
    int has_index;
    uint32_t size;
    const uint8_t *p;

    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    operands = code[1];
    has_index = (operands & 0x10u) != 0;
    if ((operands & 0xE0u) != 0 || (has_index && (operands & 0x08u) == 0)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    size = 2u + (has_index ? 2u : 0u) + data_width;
    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if ((operands & 0x08u) != 0) {
        xxbyte_status status = add_memory(instruction, operands & 7u, code + 2, has_index ? 2u : 0u, (modifiers & 1u) != 0 ? 8u : 4u);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    } else {
        add_register(instruction, XXBYTE_EBC_REGISTER_GENERAL, operands & 7u);
    }
    p = code + (has_index ? 4 : 2);
    add_immediate(instruction, data_width, xxbyte_sext(read_le(p, data_width), data_width * 8u), XXBYTE_OPERAND_FLAG_SIGNED);
    set_mnemonic(instruction, op->id + ((modifiers & 1u) != 0 ? 10u : 0u) + (data_width == 4 ? 5u : 0u), G_NONE, size);
    return XXBYTE_STATUS_OK;
}

/*
 * MOVI, MOVIn and MOVREL: the modifiers give the immediate width (0 is
 * reserved); operands bit 7 is reserved, bit 6 is the operand 1 index
 * (indirect only), bits 4-5 the MOVI move width (reserved for MOVIn and
 * MOVREL).
 */
static xxbyte_status decode_movi(const ebc_opcode *op, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    uint32_t data_width = data_widths[code[0] >> 6];
    uint32_t operands;
    int has_index;
    uint32_t size;
    uint32_t move_width = 0;
    uint32_t id = op->id;
    const uint8_t *p;
    xxbyte_status status;

    if (data_width == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    operands = code[1];
    has_index = (operands & 0x40u) != 0;
    if ((operands & 0x80u) != 0 || (op->kind != K_MOVI && (operands & 0x30u) != 0) || (has_index && (operands & 0x08u) == 0)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    size = 2u + (has_index ? 2u : 0u) + data_width;
    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (op->kind == K_MOVI) {
        move_width = 1u << ((operands >> 4) & 3u);
        id += ((operands >> 4) & 3u) * 3u;
    }
    id += (uint32_t)(code[0] >> 6) - 1u;
    if ((operands & 0x08u) != 0) {
        /* MOVIn and MOVREL store a natural-width value. */
        status = add_memory(instruction, operands & 7u, code + 2, has_index ? 2u : 0u, move_width);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    } else {
        add_register(instruction, XXBYTE_EBC_REGISTER_GENERAL, operands & 7u);
    }
    p = code + (has_index ? 4 : 2);
    if (op->kind == K_MOVIN) {
        status = add_natural_immediate(instruction, p, data_width);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    } else if (op->kind == K_MOVI) {
        add_immediate(instruction, data_width, xxbyte_sext(read_le(p, data_width), data_width * 8u), XXBYTE_OPERAND_FLAG_SIGNED);
    } else {
        /* EDK II's interpreter loads the address itself (IP-relative). */
        xxbyte_add_operand(instruction, XXBYTE_OPERAND_DATA, data_width, wrap_add(address + size, (uint64_t)xxbyte_sext(read_le(p, data_width), data_width * 8u)));
    }
    set_mnemonic(instruction, id, G_NONE, size);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status ebc_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const ebc_opcode *op = &opcodes[code[0] & 0x3Fu];

    (void)mode;
    (void)context;
    switch (op->kind) {
        case K_BREAK: return decode_break(code, code_size, instruction);
        case K_JMP:
        case K_CALL: return decode_jmp_call(code, code_size, address, instruction);
        case K_JMP8: return decode_jmp8(code, code_size, address, instruction);
        case K_RET:
            if ((code[0] & 0xC0u) != 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (code_size < 2) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            if (code[1] != 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            set_mnemonic(instruction, XXBYTE_EBC_ID_RET, G_RETURN, 2);
            return XXBYTE_STATUS_OK;
        case K_CMP:
        case K_ARITH: return decode_cmp_arith(op, code, code_size, instruction);
        case K_MOV:
        case K_MOVSN: return decode_mov(op, code, code_size, instruction);
        case K_LOADSP:
        case K_STORESP: return decode_sp(op, code, code_size, instruction);
        case K_PUSHPOP: return decode_pushpop(op, code, code_size, instruction);
        case K_CMPI: return decode_cmpi(op, code, code_size, instruction);
        case K_MOVI:
        case K_MOVIN:
        case K_MOVREL: return decode_movi(op, code, code_size, address, instruction);
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
}

/* ------------------------------------------------------------------ text */

/*
 * The text is rendered from the decoded operands and the opcode alone; the
 * formatter never reads instruction->code.
 */

/* "(+n,+c)" from an INDEX operand: the sign is printed even for zero units,
 * as EDK II's debugger does. */
static int format_index(xxbyte_fmt *fmt, const xxbyte_operand *operand)
{
    int negative = (operand->flags & XXBYTE_EBC_OPERAND_FLAG_NEGATIVE) != 0;
    char sign = negative ? '-' : '+';
    uint64_t natural = operand->extra;
    uint64_t constant = (uint64_t)operand->value;

    if (negative) {
        natural = (uint64_t)0 - natural;
        constant = (uint64_t)0 - constant;
    }
    return xxbyte_fmt_printf(fmt, "(%c%llu,%c%llu)", sign, (unsigned long long)natural, sign, (unsigned long long)constant);
}

static int format_target(const xxbyte_instruction *instruction, uint32_t i, xxbyte_fmt *fmt)
{
    int resolved = xxbyte_fmt_resolve(fmt, instruction, i);

    if (resolved != 0) {
        return resolved > 0;
    }
    return xxbyte_fmt_printf(fmt, "0x%llx", (unsigned long long)instruction->operands[i].value);
}

static int format_immediate(xxbyte_fmt *fmt, const xxbyte_operand *operand)
{
    if ((operand->flags & XXBYTE_EBC_OPERAND_FLAG_INDEX) != 0) {
        return format_index(fmt, operand);
    }
    if ((operand->flags & XXBYTE_OPERAND_FLAG_SIGNED) == 0) {
        return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
    }
    return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
}

/*
 * Prints operand *i and advances *i past it. With addend set, a direct
 * register also prints the IMMEDIATE that follows it: "R2(8)", MOV's
 * "R2(+1,+8)". An INDEX MEMORY operand prints "@R1(+0,+16)", MOVIn's INDEX
 * immediate "(+1,+8)".
 */
static int format_operand(const xxbyte_instruction *instruction, uint32_t *i, xxbyte_fmt *fmt, int addend)
{
    const xxbyte_operand *operand;

    if (*i >= instruction->operand_count) {
        return 0;
    }
    operand = &instruction->operands[*i];
    ++*i;
    switch (operand->type) {
        case XXBYTE_OPERAND_REGISTER:
            if (operand->kind == XXBYTE_EBC_REGISTER_DEDICATED) {
                return xxbyte_fmt_str(fmt, operand->value == XXBYTE_EBC_DEDICATED_FLAGS ? "[FLAGS]" : "[IP]");
            }
            if (!xxbyte_fmt_printf(fmt, "R%u", (unsigned)operand->value)) {
                return 0;
            }
            if (addend && *i < instruction->operand_count && instruction->operands[*i].type == XXBYTE_OPERAND_IMMEDIATE) {
                const xxbyte_operand *value = &instruction->operands[*i];
                ++*i;
                if ((value->flags & XXBYTE_EBC_OPERAND_FLAG_INDEX) != 0) {
                    return format_index(fmt, value);
                }
                return xxbyte_fmt_printf(fmt, "(%lld)", (long long)value->value);
            }
            return 1;
        case XXBYTE_OPERAND_MEMORY:
            if (!xxbyte_fmt_printf(fmt, "@R%u", (unsigned)operand->kind)) {
                return 0;
            }
            return (operand->flags & XXBYTE_EBC_OPERAND_FLAG_INDEX) == 0 || format_index(fmt, operand);
        case XXBYTE_OPERAND_BRANCH:
        case XXBYTE_OPERAND_DATA: return format_target(instruction, *i - 1, fmt);
        case XXBYTE_OPERAND_IMMEDIATE: return format_immediate(fmt, operand);
        default: return 0;
    }
}

static int ebc_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t kind = opcodes[instruction->opcode & 0x3Fu].kind;
    uint32_t i = 0;
    /* Which text operand takes a register's addend: JMP/CALL and PUSH/POP
     * operand 1, CMP, the arithmetic forms and MOV operand 2. */
    int addend1 = kind == K_JMP || kind == K_CALL || kind == K_PUSHPOP;
    int addend2 = kind == K_CMP || kind == K_ARITH || kind == K_MOV || kind == K_MOVSN;

    if (instruction->operand_count == 0) {
        return 1;
    }
    if (!xxbyte_fmt_char(fmt, ' ') || !format_operand(instruction, &i, fmt, addend1)) {
        return 0;
    }
    if (i < instruction->operand_count && (!xxbyte_fmt_str(fmt, ", ") || !format_operand(instruction, &i, fmt, addend2))) {
        return 0;
    }
    return i == instruction->operand_count;
}

const xxbyte_family_ops xxbyte_ebc_ops = {
    XXBYTE_FAMILY_EBC, XXBYTE_EBC_MODE_DEFAULT, ebc_check_mode, ebc_decode, NULL, ebc_format_operands, NULL, xxbyte_ebc_list_file,
};

const char *xxbyte_ebc_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_EBC_ID_COUNT) {
        return NULL;
    }
    return mnemonics[id - 1];
}

xxbyte_status xxbyte_ebc_decode_index(uint64_t raw, uint32_t width, xxbyte_ebc_index *index)
{
    if (index == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(index, 0, sizeof(*index));
    if (width != 2 && width != 4 && width != 8) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (width < 8) {
        raw &= (UINT64_C(1) << (width * 8u)) - 1u;
    }
    if (!split_index(raw, width, index)) {
        memset(index, 0, sizeof(*index));
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return XXBYTE_STATUS_OK;
}

int64_t xxbyte_ebc_index_offset(const xxbyte_ebc_index *index, uint32_t pointer_size)
{
    uint64_t offset;

    if (index == NULL) {
        return 0;
    }
    offset = index->constant + index->natural * (uint64_t)pointer_size;
    return (int64_t)(index->negative != 0 ? (uint64_t)0 - offset : offset);
}

int64_t xxbyte_ebc_operand_offset(const xxbyte_operand *operand, uint32_t pointer_size)
{
    if (operand == NULL) {
        return 0;
    }
    if (operand->type != XXBYTE_OPERAND_MEMORY && operand->type != XXBYTE_OPERAND_IMMEDIATE) {
        return operand->value;
    }
    return (int64_t)((uint64_t)operand->value + operand->extra * (uint64_t)pointer_size);
}

#else
typedef int xxbyte_ebc_disabled;
#endif
