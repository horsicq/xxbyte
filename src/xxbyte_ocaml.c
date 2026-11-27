/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_internal.h"

#if XXBYTE_USE_OCAML

#include "xxbyte_ocaml_internal.h"

#define G_NONE XXBYTE_GROUP_NONE
#define G_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_CALL XXBYTE_GROUP_CALL
#define G_TAILCALL (XXBYTE_GROUP_CALL | XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)
#define G_SWITCH (XXBYTE_GROUP_SWITCH | XXBYTE_GROUP_STOP)

/* Operand layouts, after tools/dumpobj's shapes and runtime/interp.c. */
enum {
    S_NONE,
    S_UINT,
    S_SINT,
    S_UINT_UINT,
    S_DISP,
    S_UINT_DISP,
    S_SINT_DISP,
    S_GLOBAL,
    S_GLOBAL_UINT,
    S_PRIM,
    S_UINT_PRIM,
    S_SWITCH,
    S_CLOSUREREC,
    S_PUBMET
};

typedef struct ocaml_info {
    const char *name;
    uint8_t shape;
    uint16_t group;
} ocaml_info;

/* Generated from runtime/caml/instruct.h of 4.02.3, 4.07.1, 4.12.1, 5.0.0
 * and 5.5.2. Indexed by id - 1. */
static const ocaml_info infos[XXBYTE_OCAML_ID_COUNT - 1] = {
    {"ACC0", S_NONE, G_NONE},
    {"ACC1", S_NONE, G_NONE},
    {"ACC2", S_NONE, G_NONE},
    {"ACC3", S_NONE, G_NONE},
    {"ACC4", S_NONE, G_NONE},
    {"ACC5", S_NONE, G_NONE},
    {"ACC6", S_NONE, G_NONE},
    {"ACC7", S_NONE, G_NONE},
    {"ACC", S_UINT, G_NONE},
    {"PUSH", S_NONE, G_NONE},
    {"PUSHACC0", S_NONE, G_NONE},
    {"PUSHACC1", S_NONE, G_NONE},
    {"PUSHACC2", S_NONE, G_NONE},
    {"PUSHACC3", S_NONE, G_NONE},
    {"PUSHACC4", S_NONE, G_NONE},
    {"PUSHACC5", S_NONE, G_NONE},
    {"PUSHACC6", S_NONE, G_NONE},
    {"PUSHACC7", S_NONE, G_NONE},
    {"PUSHACC", S_UINT, G_NONE},
    {"POP", S_UINT, G_NONE},
    {"ASSIGN", S_UINT, G_NONE},
    {"ENVACC1", S_NONE, G_NONE},
    {"ENVACC2", S_NONE, G_NONE},
    {"ENVACC3", S_NONE, G_NONE},
    {"ENVACC4", S_NONE, G_NONE},
    {"ENVACC", S_UINT, G_NONE},
    {"PUSHENVACC1", S_NONE, G_NONE},
    {"PUSHENVACC2", S_NONE, G_NONE},
    {"PUSHENVACC3", S_NONE, G_NONE},
    {"PUSHENVACC4", S_NONE, G_NONE},
    {"PUSHENVACC", S_UINT, G_NONE},
    {"PUSH_RETADDR", S_DISP, G_NONE},
    {"APPLY", S_UINT, G_CALL},
    {"APPLY1", S_NONE, G_CALL},
    {"APPLY2", S_NONE, G_CALL},
    {"APPLY3", S_NONE, G_CALL},
    {"APPTERM", S_UINT_UINT, G_TAILCALL},
    {"APPTERM1", S_UINT, G_TAILCALL},
    {"APPTERM2", S_UINT, G_TAILCALL},
    {"APPTERM3", S_UINT, G_TAILCALL},
    {"RETURN", S_UINT, G_RETURN},
    {"RESTART", S_NONE, G_NONE},
    {"GRAB", S_UINT, G_NONE},
    {"CLOSURE", S_UINT_DISP, G_NONE},
    {"CLOSUREREC", S_CLOSUREREC, G_NONE},
    {"OFFSETCLOSUREM3", S_NONE, G_NONE},
    {"OFFSETCLOSURE0", S_NONE, G_NONE},
    {"OFFSETCLOSURE3", S_NONE, G_NONE},
    {"OFFSETCLOSURE", S_SINT, G_NONE},
    {"PUSHOFFSETCLOSUREM3", S_NONE, G_NONE},
    {"PUSHOFFSETCLOSURE0", S_NONE, G_NONE},
    {"PUSHOFFSETCLOSURE3", S_NONE, G_NONE},
    {"PUSHOFFSETCLOSURE", S_SINT, G_NONE},
    {"GETGLOBAL", S_GLOBAL, G_NONE},
    {"PUSHGETGLOBAL", S_GLOBAL, G_NONE},
    {"GETGLOBALFIELD", S_GLOBAL_UINT, G_NONE},
    {"PUSHGETGLOBALFIELD", S_GLOBAL_UINT, G_NONE},
    {"SETGLOBAL", S_GLOBAL, G_NONE},
    {"ATOM0", S_NONE, G_NONE},
    {"ATOM", S_UINT, G_NONE},
    {"PUSHATOM0", S_NONE, G_NONE},
    {"PUSHATOM", S_UINT, G_NONE},
    {"MAKEBLOCK", S_UINT_UINT, G_NONE},
    {"MAKEBLOCK1", S_UINT, G_NONE},
    {"MAKEBLOCK2", S_UINT, G_NONE},
    {"MAKEBLOCK3", S_UINT, G_NONE},
    {"MAKEFLOATBLOCK", S_UINT, G_NONE},
    {"GETFIELD0", S_NONE, G_NONE},
    {"GETFIELD1", S_NONE, G_NONE},
    {"GETFIELD2", S_NONE, G_NONE},
    {"GETFIELD3", S_NONE, G_NONE},
    {"GETFIELD", S_UINT, G_NONE},
    {"GETFLOATFIELD", S_UINT, G_NONE},
    {"SETFIELD0", S_NONE, G_NONE},
    {"SETFIELD1", S_NONE, G_NONE},
    {"SETFIELD2", S_NONE, G_NONE},
    {"SETFIELD3", S_NONE, G_NONE},
    {"SETFIELD", S_UINT, G_NONE},
    {"SETFLOATFIELD", S_UINT, G_NONE},
    {"VECTLENGTH", S_NONE, G_NONE},
    {"GETVECTITEM", S_NONE, G_NONE},
    {"SETVECTITEM", S_NONE, G_NONE},
    {"GETBYTESCHAR", S_NONE, G_NONE},
    {"SETBYTESCHAR", S_NONE, G_NONE},
    {"BRANCH", S_DISP, G_JUMP},
    {"BRANCHIF", S_DISP, G_COND},
    {"BRANCHIFNOT", S_DISP, G_COND},
    {"SWITCH", S_SWITCH, G_SWITCH},
    {"BOOLNOT", S_NONE, G_NONE},
    {"PUSHTRAP", S_DISP, G_NONE},
    {"POPTRAP", S_NONE, G_NONE},
    {"RAISE", S_NONE, G_THROW},
    {"CHECK_SIGNALS", S_NONE, G_NONE},
    {"C_CALL1", S_PRIM, G_CALL},
    {"C_CALL2", S_PRIM, G_CALL},
    {"C_CALL3", S_PRIM, G_CALL},
    {"C_CALL4", S_PRIM, G_CALL},
    {"C_CALL5", S_PRIM, G_CALL},
    {"C_CALLN", S_UINT_PRIM, G_CALL},
    {"CONST0", S_NONE, G_NONE},
    {"CONST1", S_NONE, G_NONE},
    {"CONST2", S_NONE, G_NONE},
    {"CONST3", S_NONE, G_NONE},
    {"CONSTINT", S_SINT, G_NONE},
    {"PUSHCONST0", S_NONE, G_NONE},
    {"PUSHCONST1", S_NONE, G_NONE},
    {"PUSHCONST2", S_NONE, G_NONE},
    {"PUSHCONST3", S_NONE, G_NONE},
    {"PUSHCONSTINT", S_SINT, G_NONE},
    {"NEGINT", S_NONE, G_NONE},
    {"ADDINT", S_NONE, G_NONE},
    {"SUBINT", S_NONE, G_NONE},
    {"MULINT", S_NONE, G_NONE},
    {"DIVINT", S_NONE, G_NONE},
    {"MODINT", S_NONE, G_NONE},
    {"ANDINT", S_NONE, G_NONE},
    {"ORINT", S_NONE, G_NONE},
    {"XORINT", S_NONE, G_NONE},
    {"LSLINT", S_NONE, G_NONE},
    {"LSRINT", S_NONE, G_NONE},
    {"ASRINT", S_NONE, G_NONE},
    {"EQ", S_NONE, G_NONE},
    {"NEQ", S_NONE, G_NONE},
    {"LTINT", S_NONE, G_NONE},
    {"LEINT", S_NONE, G_NONE},
    {"GTINT", S_NONE, G_NONE},
    {"GEINT", S_NONE, G_NONE},
    {"OFFSETINT", S_SINT, G_NONE},
    {"OFFSETREF", S_SINT, G_NONE},
    {"ISINT", S_NONE, G_NONE},
    {"GETMETHOD", S_NONE, G_NONE},
    {"BEQ", S_SINT_DISP, G_COND},
    {"BNEQ", S_SINT_DISP, G_COND},
    {"BLTINT", S_SINT_DISP, G_COND},
    {"BLEINT", S_SINT_DISP, G_COND},
    {"BGTINT", S_SINT_DISP, G_COND},
    {"BGEINT", S_SINT_DISP, G_COND},
    {"ULTINT", S_NONE, G_NONE},
    {"UGEINT", S_NONE, G_NONE},
    {"BULTINT", S_UINT_DISP, G_COND},
    {"BUGEINT", S_UINT_DISP, G_COND},
    {"GETPUBMET", S_PUBMET, G_NONE},
    {"GETDYNMET", S_NONE, G_NONE},
    {"STOP", S_NONE, G_RETURN},
    {"EVENT", S_NONE, G_NONE},
    {"BREAK", S_NONE, G_NONE},
    {"RERAISE", S_NONE, G_THROW},
    {"RAISE_NOTRACE", S_NONE, G_THROW},
    {"GETSTRINGCHAR", S_NONE, G_NONE},
    {"PERFORM", S_NONE, G_CALL},
    {"RESUME", S_NONE, G_CALL},
    {"RESUMETERM", S_UINT, G_TAILCALL},
    {"REPERFORMTERM", S_UINT, G_TAILCALL},
    {"OFFSETCLOSUREM2", S_NONE, G_NONE},
    {"OFFSETCLOSURE2", S_NONE, G_NONE},
    {"PUSHOFFSETCLOSUREM2", S_NONE, G_NONE},
    {"PUSHOFFSETCLOSURE2", S_NONE, G_NONE},
    {"SETSTRINGCHAR", S_NONE, G_NONE},
};

/* Opcode 45, 47, 49 and 51 before 4.12. */
#define OP_OFFSETCLOSUREM3 45u
#define OP_GETBYTESCHAR 82u
#define OP_SETBYTESCHAR 83u

static const uint32_t opcode_counts[XXBYTE_OCAML_MODE_NEWEST + 1] = {148, 149, 149, 153};

static xxbyte_status ocaml_check_mode(uint32_t mode)
{
    return mode <= XXBYTE_OCAML_MODE_NEWEST ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static size_t ocaml_unit_size(uint32_t mode)
{
    (void)mode;
    return 4u;
}

uint32_t xxbyte_ocaml_opcode_count(uint32_t mode)
{
    return mode <= XXBYTE_OCAML_MODE_NEWEST ? opcode_counts[mode] : 0u;
}

const char *xxbyte_ocaml_mnemonic(uint32_t id)
{
    return id != 0 && id < XXBYTE_OCAML_ID_COUNT ? infos[id - 1].name : NULL;
}

/* The id of opcode in mode, 0 when it is not defined. */
static uint32_t opcode_id(uint32_t mode, uint32_t opcode)
{
    if (opcode >= opcode_counts[mode]) {
        return 0;
    }
    if (mode < XXBYTE_OCAML_MODE_4_12 && opcode >= OP_OFFSETCLOSUREM3 && opcode <= OP_OFFSETCLOSUREM3 + 6u && ((opcode - OP_OFFSETCLOSUREM3) & 1u) == 0) {
        return XXBYTE_OCAML_ID_OFFSETCLOSUREM2 + (opcode - OP_OFFSETCLOSUREM3) / 2u;
    }
    if (mode == XXBYTE_OCAML_MODE_4_02 && opcode == OP_GETBYTESCHAR) {
        return XXBYTE_OCAML_ID_GETSTRINGCHAR;
    }
    if (mode == XXBYTE_OCAML_MODE_4_02 && opcode == OP_SETBYTESCHAR) {
        return XXBYTE_OCAML_ID_SETSTRINGCHAR;
    }
    return opcode + 1u;
}

static xxbyte_operand *add_word(xxbyte_instruction *instruction, xxbyte_operand_type type, int64_t value, uint32_t offset)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, type, 4, value);
    if (operand != NULL) {
        operand->extra = offset;
    }
    return operand;
}

static int64_t word_target(uint64_t address, uint32_t offset, uint32_t word)
{
    return (int64_t)(address + offset) + (int64_t)(int32_t)word * 4;
}

static xxbyte_status ocaml_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const ocaml_info *info;
    uint32_t opcode;
    uint32_t id;
    uint32_t words;
    uint32_t w1 = 0;
    uint32_t w2 = 0;
    xxbyte_operand *operand;

    (void)context;
    if (code_size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    opcode = xxbyte_u32le(code);
    id = opcode_id(mode, opcode);
    if (id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    info = &infos[id - 1];
    switch (info->shape) {
        case S_NONE: words = 1; break;
        case S_UINT:
        case S_SINT:
        case S_DISP:
        case S_GLOBAL:
        case S_PRIM:
        case S_SWITCH: words = 2; break;
        default: words = 3; break;
    }
    if (code_size < (size_t)words * 4u) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (words >= 2) {
        w1 = xxbyte_u32le(code + 4);
    }
    if (words >= 3) {
        w2 = xxbyte_u32le(code + 8);
    }
    instruction->opcode = opcode;
    instruction->id = id;
    instruction->group = info->group;
    instruction->mnemonic = info->name;
    instruction->size = words * 4u;
    switch (info->shape) {
        case S_NONE: break;
        case S_UINT: add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, w1, 4); break;
        case S_SINT: add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, (int32_t)w1, 4)->flags = XXBYTE_OPERAND_FLAG_SIGNED; break;
        case S_UINT_UINT:
            add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, w1, 4);
            add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, w2, 8);
            break;
        case S_DISP: add_word(instruction, XXBYTE_OPERAND_BRANCH, word_target(address, 4, w1), 4); break;
        case S_UINT_DISP:
            add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, w1, 4);
            add_word(instruction, XXBYTE_OPERAND_BRANCH, word_target(address, 8, w2), 8);
            break;
        case S_SINT_DISP:
            add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, (int32_t)w1, 4)->flags = XXBYTE_OPERAND_FLAG_SIGNED;
            add_word(instruction, XXBYTE_OPERAND_BRANCH, word_target(address, 8, w2), 8);
            break;
        case S_GLOBAL: add_word(instruction, XXBYTE_OPERAND_INDEX, w1, 4)->kind = XXBYTE_INDEX_GLOBAL; break;
        case S_GLOBAL_UINT:
            add_word(instruction, XXBYTE_OPERAND_INDEX, w1, 4)->kind = XXBYTE_INDEX_GLOBAL;
            add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, w2, 8);
            break;
        case S_PRIM: add_word(instruction, XXBYTE_OPERAND_INDEX, w1, 4)->kind = (uint8_t)XXBYTE_OCAML_INDEX_PRIMITIVE; break;
        case S_UINT_PRIM:
            add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, w1, 4);
            add_word(instruction, XXBYTE_OPERAND_INDEX, w2, 8)->kind = (uint8_t)XXBYTE_OCAML_INDEX_PRIMITIVE;
            break;
        case S_PUBMET:
            add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, (int32_t)w1, 4)->flags = XXBYTE_OPERAND_FLAG_SIGNED;
            add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, w2, 8);
            break;
        case S_SWITCH: {
            /* The sizes word, then (sizes & 0xFFFF) int cases and
             * (sizes >> 16) tag cases. */
            uint64_t count = (uint64_t)(w1 & 0xFFFFu) + (w1 >> 16);
            uint64_t total = 8u + count * 4u;
            if (total > code_size) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            instruction->size = (uint32_t)total;
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, w1 & 0xFFFFu);
            operand->extra = 4;
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, w1 >> 16);
            operand->extra = 6;
            instruction->table_count = (uint32_t)count;
            instruction->table_offset = 8;
            break;
        }
        case S_CLOSUREREC: {
            uint64_t total = 12u + (uint64_t)w1 * 4u;
            if (total > UINT32_MAX) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (total > code_size) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            instruction->size = (uint32_t)total;
            add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, w1, 4);
            add_word(instruction, XXBYTE_OPERAND_IMMEDIATE, w2, 8);
            instruction->table_count = w1;
            instruction->table_offset = 12;
            break;
        }
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status ocaml_switch_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target)
{
    uint32_t ints;

    if (instruction->id != XXBYTE_OCAML_ID_SWITCH || instruction->operand_count < 2 || instruction->table_offset != 8 ||
        (uint64_t)instruction->table_offset + ((uint64_t)index + 1u) * 4u > instruction->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    ints = (uint32_t)instruction->operands[0].value;
    *key = index < ints ? (int64_t)index : (XXBYTE_OCAML_SWITCH_TAG | (int64_t)(index - ints));
    *target = word_target(instruction->address, 8, xxbyte_u32le(instruction->code + 8 + (size_t)index * 4u));
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_ocaml_closure_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *target)
{
    if (instruction == NULL || target == NULL || instruction->code == NULL || instruction->family != XXBYTE_FAMILY_OCAML ||
        instruction->id != XXBYTE_OCAML_ID_CLOSUREREC || index >= instruction->table_count || instruction->table_offset != 12 ||
        (uint64_t)instruction->table_offset + ((uint64_t)index + 1u) * 4u > instruction->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *target = word_target(instruction->address, 12, xxbyte_u32le(instruction->code + 12 + (size_t)index * 4u));
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_ocaml_mode_from_magic(const uint8_t *magic, size_t size, uint32_t *mode)
{
    uint32_t number;

    if (magic == NULL || mode == NULL || size < 12 || memcmp(magic, "Caml1999", 8) != 0 || magic[8] < 'A' || magic[8] > 'Z' || magic[9] < '0' || magic[9] > '9' ||
        magic[10] < '0' || magic[10] > '9' || magic[11] < '0' || magic[11] > '9') {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    number = (uint32_t)(magic[9] - '0') * 100u + (uint32_t)(magic[10] - '0') * 10u + (uint32_t)(magic[11] - '0');
    if (number <= 22u) {
        *mode = XXBYTE_OCAML_MODE_4_02;
    } else if (number <= 28u) {
        *mode = XXBYTE_OCAML_MODE_4_07;
    } else if (number <= 31u) {
        *mode = XXBYTE_OCAML_MODE_4_12;
    } else {
        *mode = XXBYTE_OCAML_MODE_5_00;
    }
    return XXBYTE_STATUS_OK;
}

/* "0x1c"; "-0x8" when negative. */
static int format_target(xxbyte_fmt *fmt, int64_t target)
{
    if (target < 0) {
        return xxbyte_fmt_printf(fmt, "-0x%llx", (unsigned long long)(0u - (uint64_t)target));
    }
    return xxbyte_fmt_printf(fmt, "0x%llx", (unsigned long long)target);
}

static int format_entry(const xxbyte_instruction *instruction, xxbyte_fmt *fmt, int64_t target)
{
    xxbyte_operand entry;
    int resolved;

    memset(&entry, 0, sizeof(entry));
    entry.type = XXBYTE_OPERAND_BRANCH;
    entry.size = 4;
    entry.value = target;
    resolved = xxbyte_fmt_resolve_entry(fmt, instruction, &entry);
    if (resolved != 0) {
        return resolved > 0;
    }
    return format_target(fmt, target);
}

static int format_operand(const xxbyte_instruction *instruction, uint32_t index, xxbyte_fmt *fmt)
{
    const xxbyte_operand *operand = &instruction->operands[index];

    if (operand->type == XXBYTE_OPERAND_INDEX || operand->type == XXBYTE_OPERAND_BRANCH) {
        int resolved = xxbyte_fmt_resolve(fmt, instruction, index);
        if (resolved != 0) {
            return resolved > 0;
        }
    }
    if (operand->type == XXBYTE_OPERAND_BRANCH) {
        return format_target(fmt, operand->value);
    }
    if ((operand->flags & XXBYTE_OPERAND_FLAG_SIGNED) != 0) {
        return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
    }
    return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
}

static int format_switch(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;

    if (!xxbyte_fmt_str(fmt, " {")) {
        return 0;
    }
    for (i = 0; i < instruction->table_count; ++i) {
        int64_t key;
        int64_t target;
        if (ocaml_switch_entry(instruction, i, &key, &target) != XXBYTE_STATUS_OK || (i != 0 && !xxbyte_fmt_str(fmt, ", ")) ||
            !xxbyte_fmt_printf(fmt, (key & XXBYTE_OCAML_SWITCH_TAG) != 0 ? "tag %lld: " : "int %lld: ", (long long)(key & ~XXBYTE_OCAML_SWITCH_TAG)) ||
            !format_entry(instruction, fmt, target)) {
            return 0;
        }
    }
    return xxbyte_fmt_char(fmt, '}');
}

static int format_closurerec(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;

    if (instruction->operand_count < 2 || !xxbyte_fmt_char(fmt, ' ') || !format_operand(instruction, 0, fmt) || !xxbyte_fmt_str(fmt, ", ") ||
        !format_operand(instruction, 1, fmt) || !xxbyte_fmt_str(fmt, ", [")) {
        return 0;
    }
    for (i = 0; i < instruction->table_count; ++i) {
        int64_t target;
        if (xxbyte_ocaml_closure_entry(instruction, i, &target) != XXBYTE_STATUS_OK || (i != 0 && !xxbyte_fmt_str(fmt, ", ")) ||
            !format_entry(instruction, fmt, target)) {
            return 0;
        }
    }
    return xxbyte_fmt_char(fmt, ']');
}

static int ocaml_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;

    if (instruction->id == XXBYTE_OCAML_ID_SWITCH) {
        return format_switch(instruction, fmt);
    }
    if (instruction->id == XXBYTE_OCAML_ID_CLOSUREREC) {
        return format_closurerec(instruction, fmt);
    }
    for (i = 0; i < instruction->operand_count; ++i) {
        if (!xxbyte_fmt_str(fmt, i == 0 ? " " : ", ") || !format_operand(instruction, i, fmt)) {
            return 0;
        }
    }
    return 1;
}

const xxbyte_family_ops xxbyte_ocaml_ops = {
    XXBYTE_FAMILY_OCAML, XXBYTE_OCAML_MODE_DEFAULT, ocaml_check_mode, ocaml_decode, ocaml_switch_entry, ocaml_format_operands, ocaml_unit_size, xxbyte_ocaml_list_file,
};

#else
typedef int xxbyte_ocaml_disabled;
#endif
