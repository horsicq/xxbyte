/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_squirrel_internal.h"

#if XXBYTE_USE_SQUIRREL

#define SQ_MODE_FLAGS (XXBYTE_SQUIRREL_MODE_BIG_ENDIAN | XXBYTE_SQUIRREL_MODE_INT64 | XXBYTE_SQUIRREL_MODE_DOUBLE)
#define SQ_UNIT 8u
#define SQ_NONE 0xFFu

#define G_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)

#define ID(name) XXBYTE_SQUIRREL_ID_##name

/* Indexed by id - 1. */
static const char *const sq_mnemonics[XXBYTE_SQUIRREL_ID_COUNT - 1] = {
    "LINE",     "LOAD",        "LOADINT",   "LOADFLOAT", "DLOAD",      "TAILCALL", "CALL",   "PREPCALL",      "PREPCALLK",   "GETK",        "MOVE",
    "NEWSLOT",  "DELETE",      "SET",       "GET",       "EQ",         "NE",       "ADD",    "SUB",           "MUL",         "DIV",         "MOD",
    "BITW",     "RETURN",      "LOADNULLS", "LOADROOT",  "LOADBOOL",   "DMOVE",    "JMP",    "JCMP",          "JZ",          "SETOUTER",    "GETOUTER",
    "NEWOBJ",   "APPENDARRAY", "COMPARITH", "INC",       "INCL",       "PINC",     "PINCL",  "CMP",           "EXISTS",      "INSTANCEOF",  "AND",
    "OR",       "NEG",         "NOT",       "BWNOT",     "CLOSURE",    "YIELD",    "RESUME", "FOREACH",       "POSTFOREACH", "CLONE",       "TYPEOF",
    "PUSHTRAP", "POPTRAP",     "THROW",     "NEWSLOTA",  "GETBASE",    "CLOSE",    "ARITH",  "LOADROOTTABLE", "JNZ",         "LOADFREEVAR", "VARGC",
    "GETVARGV", "NEWTABLE",    "NEWARRAY",  "GETPARENT", "COMPARITHL", "DELEGATE", "CLASS",
};

/* 2.2 sqopcodes.h, opcode -> id. */
static const uint8_t sq_ops22[61] = {
    ID(LINE),        ID(LOAD),   ID(LOADINT),   ID(LOADFLOAT),     ID(DLOAD),    ID(TAILCALL),    ID(CALL),       ID(PREPCALL),    ID(PREPCALLK),
    ID(GETK),        ID(MOVE),   ID(NEWSLOT),   ID(DELETE),        ID(SET),      ID(GET),         ID(EQ),         ID(NE),          ID(ARITH),
    ID(BITW),        ID(RETURN), ID(LOADNULLS), ID(LOADROOTTABLE), ID(LOADBOOL), ID(DMOVE),       ID(JMP),        ID(JNZ),         ID(JZ),
    ID(LOADFREEVAR), ID(VARGC),  ID(GETVARGV),  ID(NEWTABLE),      ID(NEWARRAY), ID(APPENDARRAY), ID(GETPARENT),  ID(COMPARITH),   ID(COMPARITHL),
    ID(INC),         ID(INCL),   ID(PINC),      ID(PINCL),         ID(CMP),      ID(EXISTS),      ID(INSTANCEOF), ID(AND),         ID(OR),
    ID(NEG),         ID(NOT),    ID(BWNOT),     ID(CLOSURE),       ID(YIELD),    ID(RESUME),      ID(FOREACH),    ID(POSTFOREACH), ID(DELEGATE),
    ID(CLONE),       ID(TYPEOF), ID(PUSHTRAP),  ID(POPTRAP),       ID(THROW),    ID(CLASS),       ID(NEWSLOTA),
};

#define SQ_OPCODES_3 61u /* 3.x: opcode + 1 == id */

int xxbyte_squirrel_mode_ok(uint32_t mode)
{
    uint32_t major = XXBYTE_SQUIRREL_MODE_MAJOR(mode);
    uint32_t minor = XXBYTE_SQUIRREL_MODE_MINOR(mode);

    if ((mode & ~(uint32_t)(0xFFFFu | SQ_MODE_FLAGS)) != 0) {
        return 0;
    }
    return (major == 2 && minor == 2) || (major == 3 && minor <= 2);
}

static xxbyte_status squirrel_check_mode(uint32_t mode)
{
    return xxbyte_squirrel_mode_ok(mode) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

/* The id of opcode in mode's series, or 0. */
static uint32_t sq_id(uint32_t mode, uint32_t opcode)
{
    if (XXBYTE_SQUIRREL_MODE_MAJOR(mode) == 2) {
        return opcode < sizeof(sq_ops22) ? sq_ops22[opcode] : 0u;
    }
    return opcode < SQ_OPCODES_3 ? opcode + 1u : 0u;
}

/* The fields of one instruction. */
typedef struct sq_fields {
    uint32_t arg1;
    uint32_t arg0;
    uint32_t arg2;
    uint32_t arg3;
} sq_fields;

static xxbyte_operand *sq_add(xxbyte_instruction *instruction, xxbyte_operand_type type, uint8_t kind, uint16_t flags, uint32_t size, int64_t value, uint64_t raw)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, type, size, value);
    if (operand != NULL) {
        operand->kind = kind;
        operand->flags = flags;
        operand->extra = raw;
    }
    return operand;
}

/* A register in a byte field. */
static void sq_reg(xxbyte_instruction *instruction, uint32_t field)
{
    sq_add(instruction, XXBYTE_OPERAND_REGISTER, XXBYTE_INDEX_NONE, 0, 1, (int64_t)field, field);
}

/* A register in a byte field that may be 0xFF ("-"). */
static void sq_target(xxbyte_instruction *instruction, uint32_t field)
{
    sq_add(instruction, XXBYTE_OPERAND_REGISTER, XXBYTE_INDEX_NONE, field == SQ_NONE ? XXBYTE_SQUIRREL_OPERAND_FLAG_NONE : 0, 1, (int64_t)field, field);
}

static void sq_index(xxbyte_instruction *instruction, xxbyte_index_kind kind, uint32_t size, uint32_t field)
{
    sq_add(instruction, XXBYTE_OPERAND_INDEX, (uint8_t)kind, 0, size, (int64_t)field, field);
}

static void sq_imm(xxbyte_instruction *instruction, uint16_t flags, uint32_t size, int64_t value, uint64_t raw)
{
    sq_add(instruction, XXBYTE_OPERAND_IMMEDIATE, XXBYTE_INDEX_NONE, flags, size, value, raw);
}

static void sq_signed32(xxbyte_instruction *instruction, uint32_t arg1)
{
    sq_imm(instruction, XXBYTE_OPERAND_FLAG_SIGNED, 4, xxbyte_sext(arg1, 32), arg1);
}

/* A LOADINT or integer APPENDARRAY value. */
static void sq_int(xxbyte_instruction *instruction, uint32_t mode, uint32_t arg1)
{
    if (XXBYTE_SQUIRREL_MODE_MAJOR(mode) == 3 && XXBYTE_SQUIRREL_MODE_MINOR(mode) == 0 && (mode & XXBYTE_SQUIRREL_MODE_INT64) != 0) {
        sq_imm(instruction, 0, 4, (int64_t)arg1, arg1);
    } else {
        sq_signed32(instruction, arg1);
    }
}

/* A LOADFLOAT or float APPENDARRAY value: the SQFloat at &_arg1. */
static void sq_float(xxbyte_instruction *instruction, uint32_t mode, const uint8_t *code, uint32_t arg1)
{
    if ((mode & XXBYTE_SQUIRREL_MODE_DOUBLE) != 0) {
        uint64_t bits = (mode & XXBYTE_SQUIRREL_MODE_BIG_ENDIAN) != 0 ? ((uint64_t)xxbyte_u32be(code) << 32) | xxbyte_u32be(code + 4) : xxbyte_u64le(code);
        sq_add(instruction, XXBYTE_OPERAND_FLOAT, XXBYTE_INDEX_NONE, 0, 8, (int64_t)bits, arg1);
    } else {
        sq_add(instruction, XXBYTE_OPERAND_FLOAT, XXBYTE_INDEX_NONE, 0, 4, (int64_t)arg1, arg1);
    }
}

static void sq_bool(xxbyte_instruction *instruction, uint32_t size, uint32_t field)
{
    sq_imm(instruction, XXBYTE_SQUIRREL_OPERAND_FLAG_BOOL, size, field != 0, field);
}

static void sq_operator(xxbyte_instruction *instruction, uint32_t field)
{
    sq_imm(instruction, XXBYTE_SQUIRREL_OPERAND_FLAG_OPERATOR, 1, (int64_t)field, field);
}

/* address + 8 * steps, wrapping. */
static void sq_branch(xxbyte_instruction *instruction, uint64_t address, int64_t steps, uint32_t arg1)
{
    sq_add(instruction, XXBYTE_OPERAND_BRANCH, XXBYTE_INDEX_NONE, 0, 4, (int64_t)(address + (uint64_t)steps * SQ_UNIT), arg1);
}

/* A literal or register chosen by a flag field (COND_LITERAL). */
static void sq_literal_or_reg(xxbyte_instruction *instruction, uint32_t arg1, uint32_t flag)
{
    if (flag != 0) {
        sq_index(instruction, XXBYTE_INDEX_CONSTANT, 4, arg1);
    } else {
        sq_add(instruction, XXBYTE_OPERAND_REGISTER, XXBYTE_INDEX_NONE, 0, 4, (int64_t)arg1, arg1);
    }
}

/* A NEWOBJ/CLASS base class (-1: none) and attributes (0xFF: none). */
static void sq_class(xxbyte_instruction *instruction, const sq_fields *f)
{
    if (f->arg1 == UINT32_MAX) {
        sq_add(instruction, XXBYTE_OPERAND_REGISTER, XXBYTE_INDEX_NONE, XXBYTE_SQUIRREL_OPERAND_FLAG_NONE, 4, -1, f->arg1);
    } else {
        sq_add(instruction, XXBYTE_OPERAND_REGISTER, XXBYTE_INDEX_NONE, 0, 4, (int64_t)f->arg1, f->arg1);
    }
    sq_target(instruction, f->arg2);
}

static xxbyte_status squirrel_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    int v2 = XXBYTE_SQUIRREL_MODE_MAJOR(mode) == 2;
    uint32_t minor = XXBYTE_SQUIRREL_MODE_MINOR(mode);
    uint32_t opcode;
    uint32_t id;
    uint32_t group = XXBYTE_GROUP_NONE;
    sq_fields f;
    int64_t s1;

    (void)context;
    if (code_size < SQ_UNIT) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    opcode = code[4];
    id = sq_id(mode, opcode);
    if (id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    f.arg1 = (mode & XXBYTE_SQUIRREL_MODE_BIG_ENDIAN) != 0 ? xxbyte_u32be(code) : xxbyte_u32le(code);
    f.arg0 = code[5];
    f.arg2 = code[6];
    f.arg3 = code[7];
    s1 = xxbyte_sext(f.arg1, 32);

    switch (id) {
        case ID(LINE): sq_signed32(instruction, f.arg1); break;
        case ID(LOAD):
            sq_reg(instruction, f.arg0);
            sq_index(instruction, XXBYTE_INDEX_CONSTANT, 4, f.arg1);
            break;
        case ID(LOADINT):
            sq_reg(instruction, f.arg0);
            sq_int(instruction, mode, f.arg1);
            break;
        case ID(LOADFLOAT):
            sq_reg(instruction, f.arg0);
            sq_float(instruction, mode, code, f.arg1);
            break;
        case ID(DLOAD):
            sq_reg(instruction, f.arg0);
            sq_index(instruction, XXBYTE_INDEX_CONSTANT, 4, f.arg1);
            sq_reg(instruction, f.arg2);
            sq_index(instruction, XXBYTE_INDEX_CONSTANT, 1, f.arg3);
            break;
        case ID(TAILCALL):
        case ID(CALL):
            if (v2) {
                sq_reg(instruction, f.arg0);
            } else {
                sq_target(instruction, f.arg0);
            }
            sq_add(instruction, XXBYTE_OPERAND_REGISTER, XXBYTE_INDEX_NONE, 0, 4, (int64_t)f.arg1, f.arg1);
            sq_reg(instruction, f.arg2);
            sq_imm(instruction, 0, 1, (int64_t)f.arg3, f.arg3);
            group = id == ID(CALL) ? XXBYTE_GROUP_CALL : (XXBYTE_GROUP_CALL | XXBYTE_GROUP_RETURN);
            break;
        case ID(PREPCALL):
        case ID(PREPCALLK):
            sq_reg(instruction, f.arg0);
            sq_reg(instruction, f.arg2);
            sq_literal_or_reg(instruction, f.arg1, id == ID(PREPCALLK));
            sq_reg(instruction, f.arg3);
            break;
        case ID(GETK):
            sq_reg(instruction, f.arg0);
            sq_reg(instruction, f.arg2);
            sq_index(instruction, XXBYTE_INDEX_CONSTANT, 4, f.arg1);
            break;
        case ID(NEWSLOT):
        case ID(SET):
            if (v2) {
                sq_reg(instruction, f.arg0);
            } else {
                sq_target(instruction, f.arg0);
            }
            sq_literal_or_reg(instruction, f.arg1, 0);
            sq_reg(instruction, f.arg2);
            sq_reg(instruction, f.arg3);
            break;
        case ID(DELETE):
        case ID(GET):
        case ID(EXISTS):
        case ID(DELEGATE):
            sq_reg(instruction, f.arg0);
            sq_literal_or_reg(instruction, f.arg1, 0);
            sq_reg(instruction, f.arg2);
            break;
        case ID(EQ):
        case ID(NE):
            sq_reg(instruction, f.arg0);
            sq_reg(instruction, f.arg2);
            sq_literal_or_reg(instruction, f.arg1, f.arg3);
            break;
        case ID(ADD):
        case ID(SUB):
        case ID(MUL):
        case ID(DIV):
        case ID(MOD):
        case ID(INSTANCEOF):
            sq_reg(instruction, f.arg0);
            sq_reg(instruction, f.arg2);
            sq_literal_or_reg(instruction, f.arg1, 0);
            break;
        case ID(ARITH):
        case ID(BITW):
        case ID(CMP):
            sq_reg(instruction, f.arg0);
            sq_reg(instruction, f.arg2);
            sq_literal_or_reg(instruction, f.arg1, 0);
            sq_operator(instruction, f.arg3);
            break;
        case ID(RETURN):
            if (f.arg0 != SQ_NONE) {
                sq_literal_or_reg(instruction, f.arg1, 0);
            }
            group = G_RETURN;
            break;
        case ID(LOADNULLS):
            sq_reg(instruction, f.arg0);
            sq_signed32(instruction, f.arg1);
            break;
        case ID(LOADROOT):
        case ID(LOADROOTTABLE):
        case ID(VARGC):
        case ID(GETBASE):
        case ID(THROW):
            sq_reg(instruction, f.arg0);
            if (id == ID(THROW)) {
                group = XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP;
            }
            break;
        case ID(LOADBOOL):
            sq_reg(instruction, f.arg0);
            sq_bool(instruction, 4, f.arg1);
            break;
        case ID(DMOVE):
            sq_reg(instruction, f.arg0);
            sq_literal_or_reg(instruction, f.arg1, 0);
            sq_reg(instruction, f.arg2);
            sq_reg(instruction, f.arg3);
            break;
        case ID(JMP):
            sq_branch(instruction, address, 1 + s1, f.arg1);
            group = G_JUMP;
            break;
        case ID(JCMP):
            sq_reg(instruction, f.arg2);
            sq_reg(instruction, f.arg0);
            sq_operator(instruction, f.arg3);
            sq_branch(instruction, address, 1 + s1, f.arg1);
            group = G_COND;
            break;
        case ID(JZ):
        case ID(JNZ):
            sq_reg(instruction, f.arg0);
            sq_branch(instruction, address, 1 + s1, f.arg1);
            group = G_COND;
            break;
        case ID(SETOUTER):
            sq_target(instruction, f.arg0);
            sq_index(instruction, XXBYTE_INDEX_UPVALUE, 4, f.arg1);
            sq_reg(instruction, f.arg2);
            break;
        case ID(GETOUTER):
        case ID(LOADFREEVAR):
            sq_reg(instruction, f.arg0);
            sq_index(instruction, XXBYTE_INDEX_UPVALUE, 4, f.arg1);
            break;
        case ID(NEWOBJ):
            if (f.arg3 > 2) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            sq_reg(instruction, f.arg0);
            sq_imm(instruction, XXBYTE_SQUIRREL_OPERAND_FLAG_KIND, 1, (int64_t)f.arg3, f.arg3);
            if (f.arg3 == 2) {
                sq_class(instruction, &f);
            } else {
                sq_signed32(instruction, f.arg1);
            }
            break;
        case ID(NEWTABLE):
        case ID(NEWARRAY):
            sq_reg(instruction, f.arg0);
            sq_signed32(instruction, f.arg1);
            break;
        case ID(CLASS):
            sq_reg(instruction, f.arg0);
            sq_class(instruction, &f);
            break;
        case ID(APPENDARRAY):
            sq_reg(instruction, f.arg0);
            if (v2) {
                sq_literal_or_reg(instruction, f.arg1, f.arg3);
                break;
            }
            switch (f.arg2) {
                case 0: sq_literal_or_reg(instruction, f.arg1, 0); break;
                case 1: sq_index(instruction, XXBYTE_INDEX_CONSTANT, 4, f.arg1); break;
                case 2: sq_int(instruction, mode, f.arg1); break;
                case 3: sq_float(instruction, mode, code, f.arg1); break;
                case 4: sq_bool(instruction, 4, f.arg1); break;
                default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            break;
        case ID(COMPARITH):
            sq_reg(instruction, f.arg0);
            sq_add(instruction, XXBYTE_OPERAND_REGISTER, XXBYTE_INDEX_NONE, 0, 2, (int64_t)(f.arg1 >> 16), f.arg1 >> 16);
            sq_reg(instruction, f.arg2);
            sq_add(instruction, XXBYTE_OPERAND_REGISTER, XXBYTE_INDEX_NONE, 0, 2, (int64_t)(f.arg1 & 0xFFFFu), f.arg1 & 0xFFFFu);
            sq_operator(instruction, f.arg3);
            break;
        case ID(COMPARITHL):
            sq_reg(instruction, f.arg0);
            sq_literal_or_reg(instruction, f.arg1, 0);
            sq_reg(instruction, f.arg2);
            sq_operator(instruction, f.arg3);
            break;
        case ID(INC):
        case ID(PINC):
            sq_reg(instruction, f.arg0);
            sq_literal_or_reg(instruction, f.arg1, 0);
            sq_reg(instruction, f.arg2);
            sq_imm(instruction, XXBYTE_OPERAND_FLAG_SIGNED, 1, xxbyte_sext(f.arg3, 8), f.arg3);
            break;
        case ID(INCL):
        case ID(PINCL):
            if (v2 || id == ID(PINCL)) {
                sq_reg(instruction, f.arg0);
            }
            sq_literal_or_reg(instruction, f.arg1, 0);
            sq_imm(instruction, XXBYTE_OPERAND_FLAG_SIGNED, 1, xxbyte_sext(f.arg3, 8), f.arg3);
            break;
        case ID(AND):
        case ID(OR):
            sq_reg(instruction, f.arg0);
            sq_reg(instruction, f.arg2);
            sq_branch(instruction, address, 1 + s1, f.arg1);
            group = G_COND;
            break;
        case ID(MOVE):
        case ID(NEG):
        case ID(NOT):
        case ID(BWNOT):
        case ID(CLONE):
        case ID(TYPEOF):
        case ID(GETVARGV):
        case ID(GETPARENT):
        case ID(RESUME):
            sq_reg(instruction, f.arg0);
            sq_literal_or_reg(instruction, f.arg1, 0);
            if (id == ID(RESUME)) {
                group = XXBYTE_GROUP_CALL;
            }
            break;
        case ID(CLOSURE):
            sq_reg(instruction, f.arg0);
            sq_index(instruction, XXBYTE_INDEX_FUNCTION, 4, f.arg1);
            if (!v2 && minor >= 2 && f.arg2 != SQ_NONE) {
                sq_reg(instruction, f.arg2);
            }
            break;
        case ID(YIELD):
            if (f.arg0 != SQ_NONE) {
                sq_literal_or_reg(instruction, f.arg1, 0);
            } else if (!v2) {
                sq_target(instruction, SQ_NONE);
            }
            if (!v2) {
                /* The registers the generator saves (Yield's target). */
                sq_imm(instruction, 0, 1, (int64_t)f.arg2, f.arg2);
            }
            group = XXBYTE_GROUP_RETURN;
            break;
        case ID(FOREACH):
            sq_reg(instruction, f.arg0);
            sq_reg(instruction, f.arg2);
            sq_branch(instruction, address, 1 + s1, f.arg1);
            group = G_COND;
            break;
        case ID(POSTFOREACH):
            sq_reg(instruction, f.arg0);
            sq_branch(instruction, address, s1, f.arg1);
            group = G_COND;
            break;
        case ID(PUSHTRAP):
            sq_reg(instruction, f.arg0);
            sq_branch(instruction, address, 1 + s1, f.arg1);
            break;
        case ID(POPTRAP): sq_imm(instruction, 0, 1, (int64_t)f.arg0, f.arg0); break;
        case ID(NEWSLOTA):
            sq_imm(instruction, 0, 1, (int64_t)f.arg0, f.arg0);
            sq_literal_or_reg(instruction, f.arg1, 0);
            sq_reg(instruction, f.arg2);
            sq_reg(instruction, f.arg3);
            break;
        case ID(CLOSE): sq_literal_or_reg(instruction, f.arg1, 0); break;
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->size = SQ_UNIT;
    instruction->opcode = opcode;
    instruction->id = id;
    instruction->group = group;
    instruction->mnemonic = sq_mnemonics[id - 1];
    return XXBYTE_STATUS_OK;
}

static const char *sq_operator_text(uint32_t mode, uint32_t id, int64_t value)
{
    if (id == ID(BITW)) {
        switch (value) {
            case 0: return "&";
            case 2: return "|";
            case 3: return "^";
            case 4: return "<<";
            case 5: return ">>";
            case 6: return ">>>";
            default: return NULL;
        }
    }
    if (id == ID(CMP) || id == ID(JCMP)) {
        switch (value) {
            case 0: return ">";
            case 2: return ">=";
            case 3: return "<";
            case 4: return "<=";
            case 5: return XXBYTE_SQUIRREL_MODE_MAJOR(mode) == 3 ? "<=>" : NULL;
            default: return NULL;
        }
    }
    switch (value) {
        case '+': return "+";
        case '-': return "-";
        case '*': return "*";
        case '/': return "/";
        case '%': return "%";
        default: return NULL;
    }
}

static int sq_format_operand(const xxbyte_instruction *instruction, uint32_t index, xxbyte_fmt *fmt)
{
    const xxbyte_operand *operand = &instruction->operands[index];
    int resolved;

    if ((operand->flags & XXBYTE_SQUIRREL_OPERAND_FLAG_NONE) != 0) {
        return xxbyte_fmt_char(fmt, '-');
    }
    switch (operand->type) {
        case XXBYTE_OPERAND_REGISTER:
        case XXBYTE_OPERAND_INDEX:
        case XXBYTE_OPERAND_BRANCH:
            resolved = xxbyte_fmt_resolve(fmt, instruction, index);
            if (resolved != 0) {
                return resolved > 0;
            }
            break;
        default: break;
    }
    switch (operand->type) {
        case XXBYTE_OPERAND_REGISTER: return xxbyte_fmt_printf(fmt, "r%lld", (long long)operand->value);
        case XXBYTE_OPERAND_INDEX: {
            char prefix = operand->kind == XXBYTE_INDEX_CONSTANT ? 'k' : (operand->kind == XXBYTE_INDEX_UPVALUE ? 'o' : 'f');
            return xxbyte_fmt_printf(fmt, "%c%llu", prefix, (unsigned long long)operand->value);
        }
        case XXBYTE_OPERAND_BRANCH: return xxbyte_fmt_printf(fmt, "0x%llX", (unsigned long long)operand->value);
        case XXBYTE_OPERAND_FLOAT: {
            if (operand->size == 8) {
                double d;
                uint64_t bits = (uint64_t)operand->value;
                memcpy(&d, &bits, sizeof(d));
                return xxbyte_fmt_double(fmt, d);
            } else {
                float v;
                uint32_t bits = (uint32_t)operand->value;
                memcpy(&v, &bits, sizeof(v));
                return xxbyte_fmt_float(fmt, v);
            }
        }
        default: break;
    }
    if ((operand->flags & XXBYTE_SQUIRREL_OPERAND_FLAG_BOOL) != 0) {
        return xxbyte_fmt_str(fmt, operand->value != 0 ? "true" : "false");
    }
    if ((operand->flags & XXBYTE_SQUIRREL_OPERAND_FLAG_KIND) != 0) {
        return xxbyte_fmt_str(fmt, operand->value == 0 ? "table" : (operand->value == 1 ? "array" : "class"));
    }
    if ((operand->flags & XXBYTE_SQUIRREL_OPERAND_FLAG_OPERATOR) != 0) {
        const char *text = sq_operator_text(instruction->mode, instruction->id, operand->value);
        if (text != NULL) {
            return xxbyte_fmt_str(fmt, text);
        }
    }
    if ((operand->flags & XXBYTE_OPERAND_FLAG_SIGNED) != 0) {
        return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
    }
    return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
}

static int squirrel_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;

    for (i = 0; i < instruction->operand_count; ++i) {
        if (!xxbyte_fmt_str(fmt, i == 0 ? " " : ", ") || !sq_format_operand(instruction, i, fmt)) {
            return 0;
        }
    }
    return 1;
}

static size_t squirrel_unit_size(uint32_t mode)
{
    (void)mode;
    return SQ_UNIT;
}

const xxbyte_family_ops xxbyte_squirrel_ops = {
    XXBYTE_FAMILY_SQUIRREL,   XXBYTE_SQUIRREL_MODE_DEFAULT, squirrel_check_mode,       squirrel_decode, NULL,
    squirrel_format_operands, squirrel_unit_size,           xxbyte_squirrel_list_file,
};

const char *xxbyte_squirrel_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_SQUIRREL_ID_COUNT) {
        return NULL;
    }
    return sq_mnemonics[id - 1];
}

const char *xxbyte_squirrel_opname(uint32_t mode, uint32_t opcode)
{
    uint32_t id;

    if (!xxbyte_squirrel_mode_ok(mode & 0xFFFFu)) {
        return NULL;
    }
    id = sq_id(mode, opcode);
    return id != 0 ? sq_mnemonics[id - 1] : NULL;
}

#else
typedef int xxbyte_squirrel_disabled;
#endif
