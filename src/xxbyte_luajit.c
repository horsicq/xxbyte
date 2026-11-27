/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_luajit_dump.h"

#if XXBYTE_USE_LUAJIT

/* Groups. A comparison or test skips the JMP that follows it or takes its
 * target; CALLT and CALLMT are tail calls. */
#define G_NONE XXBYTE_GROUP_NONE
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_CALL XXBYTE_GROUP_CALL
#define G_TAILCALL (XXBYTE_GROUP_CALL | XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)

/* Operand modes, ORDER BCMode. */
#define M_NONE XXBYTE_LUAJIT_OPERAND_NONE
#define M_DST XXBYTE_LUAJIT_OPERAND_DST
#define M_BASE XXBYTE_LUAJIT_OPERAND_BASE
#define M_VAR XXBYTE_LUAJIT_OPERAND_VAR
#define M_RBASE XXBYTE_LUAJIT_OPERAND_RBASE
#define M_UV XXBYTE_LUAJIT_OPERAND_UV
#define M_LIT XXBYTE_LUAJIT_OPERAND_LIT
#define M_LITS XXBYTE_LUAJIT_OPERAND_LITS
#define M_PRI XXBYTE_LUAJIT_OPERAND_PRI
#define M_NUM XXBYTE_LUAJIT_OPERAND_NUM
#define M_STR XXBYTE_LUAJIT_OPERAND_STR
#define M_TAB XXBYTE_LUAJIT_OPERAND_TAB
#define M_FUNC XXBYTE_LUAJIT_OPERAND_FUNC
#define M_JUMP XXBYTE_LUAJIT_OPERAND_JUMP
#define M_CDATA XXBYTE_LUAJIT_OPERAND_CDATA

#define ID(name) XXBYTE_LUAJIT_ID_##name

/* Dump format constants (lj_bcdump.h). */
#define DUMP_HEAD1 0x1Bu
#define DUMP_HEAD2 0x4Cu
#define DUMP_HEAD3 0x4Au
#define DUMP_KNOWN_FLAGS_V1 0x07u
#define DUMP_KNOWN_FLAGS_V2 0x1Fu
#define KGC_CHILD 0u
#define KGC_TAB 1u
#define KGC_I64 2u
#define KGC_U64 3u
#define KGC_COMPLEX 4u
#define KGC_STR 5u
#define KTAB_INT 3u
#define KTAB_NUM 4u
#define KTAB_STR 5u

/* The longest ULEB128 LuaJIT writes. */
#define ULEB_MAX_BYTES 5u

typedef struct luajit_op {
    const char *name;
    uint8_t a;      /* operand mode of A */
    uint8_t b;      /* operand mode of B, M_NONE for the AD format */
    uint8_t cd;     /* operand mode of C or D */
    uint16_t group; /* XXBYTE_GROUP_* */
} luajit_op;

/* lj_bc.h BCDEF of the LuaJIT 2.1 branch since 2026-07-20, indexed by
 * id - 1 (= opcode in XXBYTE_LUAJIT_MODE_2_1). */
static const luajit_op ops[XXBYTE_LUAJIT_ID_COUNT - 1] = {
    {"ISLT", M_VAR, M_NONE, M_VAR, G_COND},     {"ISGE", M_VAR, M_NONE, M_VAR, G_COND},        {"ISLE", M_VAR, M_NONE, M_VAR, G_COND},
    {"ISGT", M_VAR, M_NONE, M_VAR, G_COND},     {"ISEQV", M_VAR, M_NONE, M_VAR, G_COND},       {"ISNEV", M_VAR, M_NONE, M_VAR, G_COND},
    {"ISEQS", M_VAR, M_NONE, M_STR, G_COND},    {"ISNES", M_VAR, M_NONE, M_STR, G_COND},       {"ISEQN", M_VAR, M_NONE, M_NUM, G_COND},
    {"ISNEN", M_VAR, M_NONE, M_NUM, G_COND},    {"ISEQP", M_VAR, M_NONE, M_PRI, G_COND},       {"ISNEP", M_VAR, M_NONE, M_PRI, G_COND},
    {"ISTC", M_DST, M_NONE, M_VAR, G_COND},     {"ISFC", M_DST, M_NONE, M_VAR, G_COND},        {"IST", M_NONE, M_NONE, M_VAR, G_COND},
    {"ISF", M_NONE, M_NONE, M_VAR, G_COND},     {"ISTYPE", M_VAR, M_NONE, M_LIT, G_NONE},      {"ISNUM", M_VAR, M_NONE, M_LIT, G_NONE},
    {"MOV", M_DST, M_NONE, M_VAR, G_NONE},      {"NOT", M_DST, M_NONE, M_VAR, G_NONE},         {"UNM", M_DST, M_NONE, M_VAR, G_NONE},
    {"LEN", M_DST, M_NONE, M_VAR, G_NONE},      {"ADDVN", M_DST, M_VAR, M_NUM, G_NONE},        {"SUBVN", M_DST, M_VAR, M_NUM, G_NONE},
    {"MULVN", M_DST, M_VAR, M_NUM, G_NONE},     {"DIVVN", M_DST, M_VAR, M_NUM, G_NONE},        {"MODVN", M_DST, M_VAR, M_NUM, G_NONE},
    {"ADDNV", M_DST, M_VAR, M_NUM, G_NONE},     {"SUBNV", M_DST, M_VAR, M_NUM, G_NONE},        {"MULNV", M_DST, M_VAR, M_NUM, G_NONE},
    {"DIVNV", M_DST, M_VAR, M_NUM, G_NONE},     {"MODNV", M_DST, M_VAR, M_NUM, G_NONE},        {"ADDVV", M_DST, M_VAR, M_VAR, G_NONE},
    {"SUBVV", M_DST, M_VAR, M_VAR, G_NONE},     {"MULVV", M_DST, M_VAR, M_VAR, G_NONE},        {"DIVVV", M_DST, M_VAR, M_VAR, G_NONE},
    {"MODVV", M_DST, M_VAR, M_VAR, G_NONE},     {"POW", M_DST, M_VAR, M_VAR, G_NONE},          {"CAT", M_DST, M_RBASE, M_RBASE, G_NONE},
    {"KSTR", M_DST, M_NONE, M_STR, G_NONE},     {"KCDATA", M_DST, M_NONE, M_CDATA, G_NONE},    {"KSHORT", M_DST, M_NONE, M_LITS, G_NONE},
    {"KNUM", M_DST, M_NONE, M_NUM, G_NONE},     {"KPRI", M_DST, M_NONE, M_PRI, G_NONE},        {"KNIL", M_BASE, M_NONE, M_BASE, G_NONE},
    {"UGET", M_DST, M_NONE, M_UV, G_NONE},      {"USETV", M_UV, M_NONE, M_VAR, G_NONE},        {"USETS", M_UV, M_NONE, M_STR, G_NONE},
    {"USETN", M_UV, M_NONE, M_NUM, G_NONE},     {"USETP", M_UV, M_NONE, M_PRI, G_NONE},        {"UCLO", M_RBASE, M_NONE, M_JUMP, G_JUMP},
    {"FNEW", M_DST, M_NONE, M_FUNC, G_NONE},    {"TNEW", M_DST, M_NONE, M_LIT, G_NONE},        {"TDUP", M_DST, M_NONE, M_TAB, G_NONE},
    {"GGET", M_DST, M_NONE, M_STR, G_NONE},     {"GSET", M_VAR, M_NONE, M_STR, G_NONE},        {"TGETV", M_DST, M_VAR, M_VAR, G_NONE},
    {"TGETS", M_DST, M_VAR, M_STR, G_NONE},     {"TGETB", M_DST, M_VAR, M_LIT, G_NONE},        {"TGETR", M_DST, M_VAR, M_VAR, G_NONE},
    {"TSETV", M_VAR, M_VAR, M_VAR, G_NONE},     {"TSETS", M_VAR, M_VAR, M_STR, G_NONE},        {"TSETB", M_VAR, M_VAR, M_LIT, G_NONE},
    {"TSETM", M_BASE, M_NONE, M_NUM, G_NONE},   {"TSETR", M_VAR, M_VAR, M_VAR, G_NONE},        {"CALLM", M_BASE, M_LIT, M_LIT, G_CALL},
    {"CALL", M_BASE, M_LIT, M_LIT, G_CALL},     {"CALLMT", M_BASE, M_NONE, M_LIT, G_TAILCALL}, {"CALLT", M_BASE, M_NONE, M_LIT, G_TAILCALL},
    {"ITERC", M_BASE, M_LIT, M_LIT, G_CALL},    {"ITERN", M_BASE, M_LIT, M_LIT, G_CALL},       {"VARG", M_BASE, M_LIT, M_LIT, G_NONE},
    {"ISNEXT", M_BASE, M_NONE, M_JUMP, G_JUMP}, {"RETM", M_BASE, M_NONE, M_LIT, G_RETURN},     {"RET", M_RBASE, M_NONE, M_LIT, G_RETURN},
    {"RET0", M_RBASE, M_NONE, M_LIT, G_RETURN}, {"RET1", M_RBASE, M_NONE, M_LIT, G_RETURN},    {"FORI", M_BASE, M_NONE, M_JUMP, G_COND},
    {"JFORI", M_BASE, M_NONE, M_JUMP, G_COND},  {"FORL", M_BASE, M_NONE, M_JUMP, G_COND},      {"IFORL", M_BASE, M_NONE, M_JUMP, G_COND},
    {"JFORL", M_BASE, M_NONE, M_LIT, G_COND},   {"ITERL", M_BASE, M_NONE, M_JUMP, G_COND},     {"IITERL", M_BASE, M_NONE, M_JUMP, G_COND},
    {"JITERL", M_BASE, M_NONE, M_LIT, G_COND},  {"LOOP", M_RBASE, M_NONE, M_JUMP, G_NONE},     {"ILOOP", M_RBASE, M_NONE, M_JUMP, G_NONE},
    {"JLOOP", M_RBASE, M_NONE, M_LIT, G_NONE},  {"JMP", M_RBASE, M_NONE, M_JUMP, G_JUMP},      {"BNOT", M_DST, M_NONE, M_VAR, G_NONE},
    {"BAND", M_DST, M_VAR, M_VAR, G_NONE},      {"BOR", M_DST, M_VAR, M_VAR, G_NONE},          {"BXOR", M_DST, M_VAR, M_VAR, G_NONE},
    {"BSHL", M_DST, M_VAR, M_VAR, G_NONE},      {"BSHR", M_DST, M_VAR, M_VAR, G_NONE},         {"BSAR", M_DST, M_VAR, M_VAR, G_NONE},
    {"FUNCF", M_RBASE, M_NONE, M_NONE, G_NONE}, {"IFUNCF", M_RBASE, M_NONE, M_NONE, G_NONE},   {"JFUNCF", M_RBASE, M_NONE, M_LIT, G_NONE},
    {"FUNCV", M_RBASE, M_NONE, M_NONE, G_NONE}, {"IFUNCV", M_RBASE, M_NONE, M_NONE, G_NONE},   {"JFUNCV", M_RBASE, M_NONE, M_LIT, G_NONE},
    {"FUNCC", M_RBASE, M_NONE, M_NONE, G_NONE}, {"FUNCCW", M_RBASE, M_NONE, M_NONE, G_NONE},
};

/* LuaJIT 2.0 opcodes (lj_bc.h of 2.0.5). */
static const uint8_t ids_2_0[93] = {
    ID(ISLT),  ID(ISGE),  ID(ISLE),   ID(ISGT),   ID(ISEQV), ID(ISNEV),  ID(ISEQS),  ID(ISNES),  ID(ISEQN),  ID(ISNEN),  ID(ISEQP),  ID(ISNEP),  ID(ISTC),   ID(ISFC),
    ID(IST),   ID(ISF),   ID(MOV),    ID(NOT),    ID(UNM),   ID(LEN),    ID(ADDVN),  ID(SUBVN),  ID(MULVN),  ID(DIVVN),  ID(MODVN),  ID(ADDNV),  ID(SUBNV),  ID(MULNV),
    ID(DIVNV), ID(MODNV), ID(ADDVV),  ID(SUBVV),  ID(MULVV), ID(DIVVV),  ID(MODVV),  ID(POW),    ID(CAT),    ID(KSTR),   ID(KCDATA), ID(KSHORT), ID(KNUM),   ID(KPRI),
    ID(KNIL),  ID(UGET),  ID(USETV),  ID(USETS),  ID(USETN), ID(USETP),  ID(UCLO),   ID(FNEW),   ID(TNEW),   ID(TDUP),   ID(GGET),   ID(GSET),   ID(TGETV),  ID(TGETS),
    ID(TGETB), ID(TSETV), ID(TSETS),  ID(TSETB),  ID(TSETM), ID(CALLM),  ID(CALL),   ID(CALLMT), ID(CALLT),  ID(ITERC),  ID(ITERN),  ID(VARG),   ID(ISNEXT), ID(RETM),
    ID(RET),   ID(RET0),  ID(RET1),   ID(FORI),   ID(JFORI), ID(FORL),   ID(IFORL),  ID(JFORL),  ID(ITERL),  ID(IITERL), ID(JITERL), ID(LOOP),   ID(ILOOP),  ID(JLOOP),
    ID(JMP),   ID(FUNCF), ID(IFUNCF), ID(JFUNCF), ID(FUNCV), ID(IFUNCV), ID(JFUNCV), ID(FUNCC),  ID(FUNCCW),
};

/* LuaJIT 2.1 opcodes before the bit operators. */
static const uint8_t ids_2_1_nobitop[97] = {
    ID(ISLT),   ID(ISGE),  ID(ISLE),   ID(ISGT),  ID(ISEQV), ID(ISNEV), ID(ISEQS),  ID(ISNES),  ID(ISEQN), ID(ISNEN),  ID(ISEQP),  ID(ISNEP),  ID(ISTC),   ID(ISFC),
    ID(IST),    ID(ISF),   ID(ISTYPE), ID(ISNUM), ID(MOV),   ID(NOT),   ID(UNM),    ID(LEN),    ID(ADDVN), ID(SUBVN),  ID(MULVN),  ID(DIVVN),  ID(MODVN),  ID(ADDNV),
    ID(SUBNV),  ID(MULNV), ID(DIVNV),  ID(MODNV), ID(ADDVV), ID(SUBVV), ID(MULVV),  ID(DIVVV),  ID(MODVV), ID(POW),    ID(CAT),    ID(KSTR),   ID(KCDATA), ID(KSHORT),
    ID(KNUM),   ID(KPRI),  ID(KNIL),   ID(UGET),  ID(USETV), ID(USETS), ID(USETN),  ID(USETP),  ID(UCLO),  ID(FNEW),   ID(TNEW),   ID(TDUP),   ID(GGET),   ID(GSET),
    ID(TGETV),  ID(TGETS), ID(TGETB),  ID(TGETR), ID(TSETV), ID(TSETS), ID(TSETB),  ID(TSETM),  ID(TSETR), ID(CALLM),  ID(CALL),   ID(CALLMT), ID(CALLT),  ID(ITERC),
    ID(ITERN),  ID(VARG),  ID(ISNEXT), ID(RETM),  ID(RET),   ID(RET0),  ID(RET1),   ID(FORI),   ID(JFORI), ID(FORL),   ID(IFORL),  ID(JFORL),  ID(ITERL),  ID(IITERL),
    ID(JITERL), ID(LOOP),  ID(ILOOP),  ID(JLOOP), ID(JMP),   ID(FUNCF), ID(IFUNCF), ID(JFUNCF), ID(FUNCV), ID(IFUNCV), ID(JFUNCV), ID(FUNCC),  ID(FUNCCW),
};

static int luajit_mode_ok(uint32_t mode)
{
    uint32_t set = mode & XXBYTE_LUAJIT_MODE_SET_MASK;

    return (mode & ~(uint32_t)(XXBYTE_LUAJIT_MODE_SET_MASK | XXBYTE_LUAJIT_MODE_BE)) == 0 &&
           (set == XXBYTE_LUAJIT_MODE_2_1 || set == XXBYTE_LUAJIT_MODE_2_0 || set == XXBYTE_LUAJIT_MODE_2_1_NOBITOP);
}

/* The id of opcode in a supported mode, 0 when undefined. */
static uint32_t luajit_id(uint32_t mode, uint32_t opcode)
{
    switch (mode & XXBYTE_LUAJIT_MODE_SET_MASK) {
        case XXBYTE_LUAJIT_MODE_2_0: return opcode < sizeof(ids_2_0) ? ids_2_0[opcode] : 0u;
        case XXBYTE_LUAJIT_MODE_2_1_NOBITOP: return opcode < sizeof(ids_2_1_nobitop) ? ids_2_1_nobitop[opcode] : 0u;
        default: return opcode < XXBYTE_LUAJIT_ID_COUNT - 1u ? opcode + 1u : 0u;
    }
}

static xxbyte_status luajit_check_mode(uint32_t mode)
{
    return luajit_mode_ok(mode) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

/* Appends one A, B, C or D field according to its operand mode. */
static void add_field(xxbyte_instruction *instruction, uint32_t mode, uint32_t size, uint32_t value)
{
    xxbyte_operand *operand = NULL;

    switch (mode) {
        case M_DST:
        case M_BASE:
        case M_VAR:
        case M_RBASE: operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_REGISTER, size, value); break;
        case M_UV: operand = xxbyte_add_index(instruction, XXBYTE_INDEX_UPVALUE, size, value); break;
        case M_LITS:
            /* luajit -bl sign-extends only the 16-bit D form. */
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, size, size == 2 ? xxbyte_sext(value, 16) : (int64_t)value);
            if (operand != NULL) {
                operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
            }
            break;
        case M_NUM: operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_LUAJIT_INDEX_NUM, size, value); break;
        case M_STR: operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_LUAJIT_INDEX_STR, size, value); break;
        case M_TAB: operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_LUAJIT_INDEX_TAB, size, value); break;
        case M_FUNC: operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_LUAJIT_INDEX_FUNC, size, value); break;
        case M_CDATA: operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_LUAJIT_INDEX_CDATA, size, value); break;
        case M_JUMP:
            /* Relative to the next instruction, biased by 0x8000. */
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, size, (int64_t)(instruction->address + (uint64_t)(((int64_t)value - 0x7FFF) * 4)));
            break;
        default: /* M_LIT, M_PRI */ operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, size, value); break;
    }
    if (operand != NULL) {
        operand->extra = mode;
    }
}

static xxbyte_status luajit_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    int big_endian = (mode & XXBYTE_LUAJIT_MODE_BE) != 0;
    const luajit_op *op;
    uint32_t opcode;
    uint32_t id;
    uint32_t word;

    (void)context;
    (void)address;
    if (big_endian && code_size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    opcode = big_endian ? code[3] : code[0];
    id = luajit_id(mode, opcode);
    if (id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    word = big_endian ? xxbyte_u32be(code) : xxbyte_u32le(code);
    op = &ops[id - 1];
    instruction->size = 4;
    instruction->opcode = opcode;
    instruction->id = id;
    instruction->group = op->group;
    instruction->mnemonic = op->name;
    if (op->a != M_NONE) {
        add_field(instruction, op->a, 1, (word >> 8) & 0xFFu);
    }
    if (op->b != M_NONE) {
        add_field(instruction, op->b, 1, word >> 24);
        add_field(instruction, op->cd, 1, (word >> 16) & 0xFFu);
    } else if (op->cd != M_NONE) {
        add_field(instruction, op->cd, 2, word >> 16);
    }
    return XXBYTE_STATUS_OK;
}

/* The -bl pc of a target address: target / 4, rounded down. */
static long long target_pc(int64_t target)
{
    int64_t pc = target / 4;

    if (target % 4 != 0 && target < 0) {
        --pc;
    }
    return (long long)pc;
}

static int luajit_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;
    /* The ready-made names keep the numbers and add -bl's comment. */
    int bl = fmt->resolver == xxbyte_luajit_resolve;

    for (i = 0; i < instruction->operand_count; ++i) {
        const xxbyte_operand *operand = &instruction->operands[i];
        int resolved = 0;

        if (!xxbyte_fmt_str(fmt, operand->type == XXBYTE_OPERAND_BRANCH ? " => " : " ")) {
            return 0;
        }
        if (!bl && (operand->type == XXBYTE_OPERAND_INDEX || operand->type == XXBYTE_OPERAND_BRANCH)) {
            resolved = xxbyte_fmt_resolve(fmt, instruction, i);
            if (resolved < 0) {
                return 0;
            }
        }
        if (resolved != 0) {
            continue;
        }
        if (operand->type == XXBYTE_OPERAND_BRANCH) {
            if (!xxbyte_fmt_printf(fmt, "%04lld", target_pc(operand->value))) {
                return 0;
            }
        } else if ((operand->flags & XXBYTE_OPERAND_FLAG_SIGNED) != 0) {
            if (!xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value)) {
                return 0;
            }
        } else if (!xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value)) {
            return 0;
        }
    }
    return bl ? xxbyte_luajit_bl_comment(instruction, fmt) : 1;
}

static size_t luajit_unit_size(uint32_t mode)
{
    (void)mode;
    return 4;
}

const xxbyte_family_ops xxbyte_luajit_ops = {
    XXBYTE_FAMILY_LUAJIT, XXBYTE_LUAJIT_MODE_DEFAULT, luajit_check_mode, luajit_decode, NULL, luajit_format_operands, luajit_unit_size, xxbyte_luajit_list_file,
};

const char *xxbyte_luajit_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_LUAJIT_ID_COUNT) {
        return NULL;
    }
    return ops[id - 1].name;
}

uint32_t xxbyte_luajit_opcode_id(uint32_t mode, uint32_t opcode)
{
    if (!luajit_mode_ok(mode)) {
        return 0;
    }
    return luajit_id(mode, opcode);
}

uint32_t xxbyte_luajit_operand_modes(uint32_t id)
{
    const luajit_op *op;

    if (id == 0 || id >= XXBYTE_LUAJIT_ID_COUNT) {
        return 0;
    }
    op = &ops[id - 1];
    return (uint32_t)op->a | ((uint32_t)op->b << 3) | ((uint32_t)op->cd << 7);
}

/*
 * Reads a ULEB128 of at most ULEB_MAX_BYTES bytes from data[*pos..end).
 * value may be NULL to skip one; otherwise it must fit 32 bits. Returns
 * XXBYTE_STATUS_TRUNCATED when it runs into end.
 */
static xxbyte_status read_uleb(const uint8_t *data, size_t end, size_t *pos, uint32_t *value)
{
    uint64_t result = 0;
    unsigned i;

    for (i = 0; i < ULEB_MAX_BYTES; ++i) {
        uint8_t byte;

        if (*pos >= end) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        byte = data[(*pos)++];
        result |= (uint64_t)(byte & 0x7Fu) << (7u * i);
        if (byte < 0x80u) {
            if (value != NULL) {
                if (result > UINT32_MAX) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                *value = (uint32_t)result;
            }
            return XXBYTE_STATUS_OK;
        }
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

/* Skips count bytes of data[*pos..end). */
static xxbyte_status skip_bytes(size_t end, size_t *pos, uint64_t count)
{
    if (count > end - *pos) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *pos += (size_t)count;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_luajit_parse_header(const uint8_t *data, size_t size, xxbyte_luajit_header *header)
{
    xxbyte_luajit_header parsed;
    xxbyte_status status;
    size_t pos = 4;

    if (header == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(header, 0, sizeof(*header));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(&parsed, 0, sizeof(parsed));
    if ((size > 0 && data[0] != DUMP_HEAD1) || (size > 1 && data[1] != DUMP_HEAD2) || (size > 2 && data[2] != DUMP_HEAD3)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    parsed.version = data[3];
    if (parsed.version != 1 && parsed.version != 2) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    status = read_uleb(data, size, &pos, &parsed.flags);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if ((parsed.flags & ~(parsed.version == 1 ? DUMP_KNOWN_FLAGS_V1 : DUMP_KNOWN_FLAGS_V2)) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    parsed.mode = parsed.version == 1 ? XXBYTE_LUAJIT_MODE_2_0 : XXBYTE_LUAJIT_MODE_2_1;
    if ((parsed.flags & XXBYTE_LUAJIT_FLAG_BE) != 0) {
        parsed.mode |= XXBYTE_LUAJIT_MODE_BE;
    }
    if ((parsed.flags & XXBYTE_LUAJIT_FLAG_STRIP) == 0) {
        status = read_uleb(data, size, &pos, &parsed.name_size);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if ((uint64_t)pos > UINT32_MAX) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        parsed.name_offset = (uint32_t)pos;
        status = skip_bytes(size, &pos, parsed.name_size);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    if ((uint64_t)pos > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    parsed.size = (uint32_t)pos;
    *header = parsed;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_luajit_read_uleb(const uint8_t *data, size_t end, size_t *pos, uint32_t *value)
{
    return read_uleb(data, end, pos, value);
}

/* Skips one constant-table key or value (ktabk). */
static xxbyte_status skip_ktabk(const uint8_t *data, size_t end, size_t *pos)
{
    uint32_t type;
    xxbyte_status status = read_uleb(data, end, pos, &type);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (type >= KTAB_STR) {
        return skip_bytes(end, pos, (uint64_t)type - KTAB_STR);
    }
    if (type == KTAB_INT) {
        return read_uleb(data, end, pos, NULL);
    }
    if (type == KTAB_NUM) {
        status = read_uleb(data, end, pos, NULL);
        return status == XXBYTE_STATUS_OK ? read_uleb(data, end, pos, NULL) : status;
    }
    return XXBYTE_STATUS_OK; /* nil, false, true */
}

xxbyte_status xxbyte_luajit_skip_ktabk(const uint8_t *data, size_t end, size_t *pos)
{
    return skip_ktabk(data, end, pos);
}

/* Skips the GC constants and counts the child prototypes. Every constant
 * takes at least one byte, so the loops end with the prototype. */
static xxbyte_status skip_kgc(const uint8_t *data, size_t end, size_t *pos, uint32_t count, uint32_t *children)
{
    uint32_t i;
    uint32_t j;

    for (i = 0; i < count; ++i) {
        uint32_t type;
        xxbyte_status status = read_uleb(data, end, pos, &type);

        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (type >= KGC_STR) {
            status = skip_bytes(end, pos, (uint64_t)type - KGC_STR);
        } else if (type == KGC_TAB) {
            uint32_t narray = 0;
            uint32_t nhash = 0;
            status = read_uleb(data, end, pos, &narray);
            if (status == XXBYTE_STATUS_OK) {
                status = read_uleb(data, end, pos, &nhash);
            }
            for (j = 0; status == XXBYTE_STATUS_OK && j < narray; ++j) {
                status = skip_ktabk(data, end, pos);
            }
            for (j = 0; status == XXBYTE_STATUS_OK && j < nhash; ++j) {
                status = skip_ktabk(data, end, pos);
                if (status == XXBYTE_STATUS_OK) {
                    status = skip_ktabk(data, end, pos);
                }
            }
        } else if (type == KGC_CHILD) {
            ++*children;
        } else {
            /* I64 and U64: lo, hi; COMPLEX: re lo, hi, im lo, hi. */
            unsigned words = type == KGC_COMPLEX ? 4u : 2u;
            for (j = 0; status == XXBYTE_STATUS_OK && j < words; ++j) {
                status = read_uleb(data, end, pos, NULL);
            }
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* Skips the number constants: a 33-bit ULEB128 whose low bit marks a
 * double, then the high word of a double. */
static xxbyte_status skip_knum(const uint8_t *data, size_t end, size_t *pos, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        int is_double;
        xxbyte_status status;

        if (*pos >= end) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        is_double = (data[*pos] & 1u) != 0;
        status = read_uleb(data, end, pos, NULL);
        if (status == XXBYTE_STATUS_OK && is_double) {
            status = read_uleb(data, end, pos, NULL);
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* The prototype body at data[pos..end), after its length. Running into
 * end means the contents disagree with the length. */
static xxbyte_status parse_body(const uint8_t *data, size_t pos, size_t end, uint32_t dump_flags, xxbyte_luajit_proto *proto)
{
    xxbyte_status status = XXBYTE_STATUS_OK;
    uint32_t width = 0;

    if (end - pos < 4) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    proto->flags = data[pos];
    proto->numparams = data[pos + 1];
    proto->framesize = data[pos + 2];
    proto->sizeuv = data[pos + 3];
    pos += 4;
    status = read_uleb(data, end, &pos, &proto->sizekgc);
    if (status == XXBYTE_STATUS_OK) {
        status = read_uleb(data, end, &pos, &proto->sizekn);
    }
    if (status == XXBYTE_STATUS_OK) {
        status = read_uleb(data, end, &pos, &proto->sizebc);
    }
    if (status == XXBYTE_STATUS_OK && (dump_flags & XXBYTE_LUAJIT_FLAG_STRIP) == 0) {
        status = read_uleb(data, end, &pos, &proto->sizedbg);
        if (status == XXBYTE_STATUS_OK && proto->sizedbg != 0) {
            status = read_uleb(data, end, &pos, &proto->firstline);
            if (status == XXBYTE_STATUS_OK) {
                status = read_uleb(data, end, &pos, &proto->numline);
            }
        }
    }
    if (status == XXBYTE_STATUS_OK) {
        proto->bc_offset = (uint32_t)pos;
        status = skip_bytes(end, &pos, (uint64_t)proto->sizebc * 4u);
    }
    if (status == XXBYTE_STATUS_OK) {
        proto->uv_offset = (uint32_t)pos;
        status = skip_bytes(end, &pos, (uint64_t)proto->sizeuv * 2u);
    }
    if (status == XXBYTE_STATUS_OK) {
        proto->kgc_offset = (uint32_t)pos;
        status = skip_kgc(data, end, &pos, proto->sizekgc, &proto->children);
    }
    if (status == XXBYTE_STATUS_OK) {
        proto->kn_offset = (uint32_t)pos;
        status = skip_knum(data, end, &pos, proto->sizekn);
    }
    if (status != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    /* The debug info takes the rest of the prototype. */
    if (end - pos != proto->sizedbg) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (proto->sizedbg != 0) {
        proto->debug_offset = (uint32_t)pos;
        width = proto->numline < 256u ? 1u : proto->numline < 65536u ? 2u : 4u;
        if ((uint64_t)proto->sizebc * width <= proto->sizedbg) {
            proto->lineinfo_width = width;
        }
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_luajit_parse_proto(const uint8_t *data, size_t size, size_t offset, const xxbyte_luajit_header *header, xxbyte_luajit_proto *proto)
{
    xxbyte_luajit_proto parsed;
    xxbyte_status status;
    uint32_t length;
    size_t pos = offset;

    if (proto == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(proto, 0, sizeof(*proto));
    if (data == NULL || header == NULL || offset > size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if ((uint64_t)size > UINT32_MAX) {
        size = (size_t)UINT32_MAX; /* offsets are 32-bit */
    }
    if (offset >= size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    memset(&parsed, 0, sizeof(parsed));
    parsed.offset = (uint32_t)offset;
    status = read_uleb(data, size, &pos, &length);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (length == 0) {
        /* LuaJIT ends the dump at any zero length, normally one 0 byte. */
        proto->offset = (uint32_t)offset;
        proto->size = (uint32_t)(pos - offset);
        return XXBYTE_STATUS_END_OF_INPUT;
    }
    if (length > size - pos) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    status = parse_body(data, pos, pos + length, header->flags, &parsed);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    parsed.size = (uint32_t)(pos + length - offset);
    *proto = parsed;
    return XXBYTE_STATUS_OK;
}

#else
typedef int xxbyte_luajit_disabled;
#endif
