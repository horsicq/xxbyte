/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_avm2_internal.h"

#if XXBYTE_USE_AVM2

/* Groups. lookupswitch always transfers, to a case or to its default. */
#define G_NONE XXBYTE_GROUP_NONE
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_CALL XXBYTE_GROUP_CALL
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_SWITCH (XXBYTE_GROUP_SWITCH | XXBYTE_GROUP_STOP)
#define G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)

/* Operand layouts after the opcode byte (avmplus AvmCore::readOperands). */
enum {
    F_NONE,
    F_LOCAL_N,   /* implicit register: the low two opcode bits */
    F_U30,       /* u30 */
    F_U30_U30,   /* u30, u30 */
    F_BRANCH,    /* s24 offset from the next instruction */
    F_SWITCH,    /* s24 default, u30 case_count, case_count + 1 s24 */
    F_PUSHBYTE,  /* u8, sign-extended */
    F_PUSHSHORT, /* variable-length s32; its low 16 bits, sign-extended */
    F_SCOPE,     /* u8 scope index */
    F_DEBUG      /* u8 type, u30 string, u8 register, u30 extra */
};

/* What a u30 operand is. */
enum {
    K_NONE,
    K_IMM,
    K_LOCAL,
    K_STRING,
    K_INT,
    K_UINT,
    K_DOUBLE,
    K_FLOAT,
    K_FLOAT4,
    K_NAMESPACE,
    K_MULTINAME,
    K_METHOD,
    K_CLASS,
    K_EXCEPTION,
    K_SLOT,
    K_DISP_ID,
    K_OUTER_SCOPE,
    K_COUNT
};

/* The INDEX kind of each operand kind (K_IMM is an IMMEDIATE). */
static const uint8_t index_kinds[K_COUNT] = {
    [K_NONE] = XXBYTE_INDEX_NONE,
    [K_IMM] = XXBYTE_INDEX_NONE,
    [K_LOCAL] = XXBYTE_INDEX_LOCAL,
    [K_STRING] = XXBYTE_INDEX_STRING,
    [K_INT] = XXBYTE_AVM2_INDEX_INT,
    [K_UINT] = XXBYTE_AVM2_INDEX_UINT,
    [K_DOUBLE] = XXBYTE_AVM2_INDEX_DOUBLE,
    [K_FLOAT] = XXBYTE_AVM2_INDEX_FLOAT,
    [K_FLOAT4] = XXBYTE_AVM2_INDEX_FLOAT4,
    [K_NAMESPACE] = XXBYTE_AVM2_INDEX_NAMESPACE,
    [K_MULTINAME] = XXBYTE_AVM2_INDEX_MULTINAME,
    [K_METHOD] = XXBYTE_INDEX_METHOD,
    [K_CLASS] = XXBYTE_INDEX_CLASS,
    [K_EXCEPTION] = XXBYTE_AVM2_INDEX_EXCEPTION,
    [K_SLOT] = XXBYTE_AVM2_INDEX_SLOT,
    [K_DISP_ID] = XXBYTE_AVM2_INDEX_DISP_ID,
    [K_OUTER_SCOPE] = XXBYTE_AVM2_INDEX_OUTER_SCOPE,
};

#define OPF_FLOAT 0x01u /* needs ABC 47.16 or later */

#define OP_LOOKUPSWITCH 0x1Bu

typedef struct avm2_opcode {
    uint8_t id;     /* XXBYTE_AVM2_ID_*, 0 for an undefined opcode */
    uint8_t form;   /* F_* */
    uint8_t kind1;  /* K_* of the first u30 */
    uint8_t kind2;  /* K_* of the second u30 */
    uint16_t group; /* XXBYTE_GROUP_* */
    uint8_t flags;  /* OPF_* */
} avm2_opcode;

#define OP(id, form, kind1, kind2, group, flags) {XXBYTE_AVM2_ID_##id, F_##form, K_##kind1, K_##kind2, G_##group, flags}

/* avmplus core/opcodes.tbl without the unused and VM-internal opcodes; the
 * register shortcuts are named as in the AVM2 Overview (getlocal_0). */
static const avm2_opcode opcodes[256] = {
    [0x01] = OP(BKPT, NONE, NONE, NONE, NONE, 0),
    [0x02] = OP(NOP, NONE, NONE, NONE, NONE, 0),
    [0x03] = OP(THROW, NONE, NONE, NONE, THROW, 0),
    [0x04] = OP(GETSUPER, U30, MULTINAME, NONE, NONE, 0),
    [0x05] = OP(SETSUPER, U30, MULTINAME, NONE, NONE, 0),
    [0x06] = OP(DXNS, U30, STRING, NONE, NONE, 0),
    [0x07] = OP(DXNSLATE, NONE, NONE, NONE, NONE, 0),
    [0x08] = OP(KILL, U30, LOCAL, NONE, NONE, 0),
    [0x09] = OP(LABEL, NONE, NONE, NONE, NONE, 0),
    [0x0A] = OP(LF32X4, NONE, NONE, NONE, NONE, OPF_FLOAT),
    [0x0B] = OP(SF32X4, NONE, NONE, NONE, NONE, OPF_FLOAT),
    [0x0C] = OP(IFNLT, BRANCH, NONE, NONE, COND, 0),
    [0x0D] = OP(IFNLE, BRANCH, NONE, NONE, COND, 0),
    [0x0E] = OP(IFNGT, BRANCH, NONE, NONE, COND, 0),
    [0x0F] = OP(IFNGE, BRANCH, NONE, NONE, COND, 0),
    [0x10] = OP(JUMP, BRANCH, NONE, NONE, JUMP, 0),
    [0x11] = OP(IFTRUE, BRANCH, NONE, NONE, COND, 0),
    [0x12] = OP(IFFALSE, BRANCH, NONE, NONE, COND, 0),
    [0x13] = OP(IFEQ, BRANCH, NONE, NONE, COND, 0),
    [0x14] = OP(IFNE, BRANCH, NONE, NONE, COND, 0),
    [0x15] = OP(IFLT, BRANCH, NONE, NONE, COND, 0),
    [0x16] = OP(IFLE, BRANCH, NONE, NONE, COND, 0),
    [0x17] = OP(IFGT, BRANCH, NONE, NONE, COND, 0),
    [0x18] = OP(IFGE, BRANCH, NONE, NONE, COND, 0),
    [0x19] = OP(IFSTRICTEQ, BRANCH, NONE, NONE, COND, 0),
    [0x1A] = OP(IFSTRICTNE, BRANCH, NONE, NONE, COND, 0),
    [0x1B] = OP(LOOKUPSWITCH, SWITCH, NONE, NONE, SWITCH, 0),
    [0x1C] = OP(PUSHWITH, NONE, NONE, NONE, NONE, 0),
    [0x1D] = OP(POPSCOPE, NONE, NONE, NONE, NONE, 0),
    [0x1E] = OP(NEXTNAME, NONE, NONE, NONE, NONE, 0),
    [0x1F] = OP(HASNEXT, NONE, NONE, NONE, NONE, 0),
    [0x20] = OP(PUSHNULL, NONE, NONE, NONE, NONE, 0),
    [0x21] = OP(PUSHUNDEFINED, NONE, NONE, NONE, NONE, 0),
    [0x22] = OP(PUSHFLOAT, U30, FLOAT, NONE, NONE, OPF_FLOAT),
    [0x23] = OP(NEXTVALUE, NONE, NONE, NONE, NONE, 0),
    [0x24] = OP(PUSHBYTE, PUSHBYTE, NONE, NONE, NONE, 0),
    [0x25] = OP(PUSHSHORT, PUSHSHORT, NONE, NONE, NONE, 0),
    [0x26] = OP(PUSHTRUE, NONE, NONE, NONE, NONE, 0),
    [0x27] = OP(PUSHFALSE, NONE, NONE, NONE, NONE, 0),
    [0x28] = OP(PUSHNAN, NONE, NONE, NONE, NONE, 0),
    [0x29] = OP(POP, NONE, NONE, NONE, NONE, 0),
    [0x2A] = OP(DUP, NONE, NONE, NONE, NONE, 0),
    [0x2B] = OP(SWAP, NONE, NONE, NONE, NONE, 0),
    [0x2C] = OP(PUSHSTRING, U30, STRING, NONE, NONE, 0),
    [0x2D] = OP(PUSHINT, U30, INT, NONE, NONE, 0),
    [0x2E] = OP(PUSHUINT, U30, UINT, NONE, NONE, 0),
    [0x2F] = OP(PUSHDOUBLE, U30, DOUBLE, NONE, NONE, 0),
    [0x30] = OP(PUSHSCOPE, NONE, NONE, NONE, NONE, 0),
    [0x31] = OP(PUSHNAMESPACE, U30, NAMESPACE, NONE, NONE, 0),
    [0x32] = OP(HASNEXT2, U30_U30, LOCAL, LOCAL, NONE, 0),
    [0x35] = OP(LI8, NONE, NONE, NONE, NONE, 0),
    [0x36] = OP(LI16, NONE, NONE, NONE, NONE, 0),
    [0x37] = OP(LI32, NONE, NONE, NONE, NONE, 0),
    [0x38] = OP(LF32, NONE, NONE, NONE, NONE, 0),
    [0x39] = OP(LF64, NONE, NONE, NONE, NONE, 0),
    [0x3A] = OP(SI8, NONE, NONE, NONE, NONE, 0),
    [0x3B] = OP(SI16, NONE, NONE, NONE, NONE, 0),
    [0x3C] = OP(SI32, NONE, NONE, NONE, NONE, 0),
    [0x3D] = OP(SF32, NONE, NONE, NONE, NONE, 0),
    [0x3E] = OP(SF64, NONE, NONE, NONE, NONE, 0),
    [0x40] = OP(NEWFUNCTION, U30, METHOD, NONE, NONE, 0),
    [0x41] = OP(CALL, U30, IMM, NONE, CALL, 0),
    [0x42] = OP(CONSTRUCT, U30, IMM, NONE, CALL, 0),
    [0x43] = OP(CALLMETHOD, U30_U30, DISP_ID, IMM, CALL, 0),
    [0x44] = OP(CALLSTATIC, U30_U30, METHOD, IMM, CALL, 0),
    [0x45] = OP(CALLSUPER, U30_U30, MULTINAME, IMM, CALL, 0),
    [0x46] = OP(CALLPROPERTY, U30_U30, MULTINAME, IMM, CALL, 0),
    [0x47] = OP(RETURNVOID, NONE, NONE, NONE, RETURN, 0),
    [0x48] = OP(RETURNVALUE, NONE, NONE, NONE, RETURN, 0),
    [0x49] = OP(CONSTRUCTSUPER, U30, IMM, NONE, CALL, 0),
    [0x4A] = OP(CONSTRUCTPROP, U30_U30, MULTINAME, IMM, CALL, 0),
    [0x4C] = OP(CALLPROPLEX, U30_U30, MULTINAME, IMM, CALL, 0),
    [0x4E] = OP(CALLSUPERVOID, U30_U30, MULTINAME, IMM, CALL, 0),
    [0x4F] = OP(CALLPROPVOID, U30_U30, MULTINAME, IMM, CALL, 0),
    [0x50] = OP(SXI1, NONE, NONE, NONE, NONE, 0),
    [0x51] = OP(SXI8, NONE, NONE, NONE, NONE, 0),
    [0x52] = OP(SXI16, NONE, NONE, NONE, NONE, 0),
    [0x53] = OP(APPLYTYPE, U30, IMM, NONE, NONE, 0),
    [0x54] = OP(PUSHFLOAT4, U30, FLOAT4, NONE, NONE, OPF_FLOAT),
    [0x55] = OP(NEWOBJECT, U30, IMM, NONE, NONE, 0),
    [0x56] = OP(NEWARRAY, U30, IMM, NONE, NONE, 0),
    [0x57] = OP(NEWACTIVATION, NONE, NONE, NONE, NONE, 0),
    [0x58] = OP(NEWCLASS, U30, CLASS, NONE, NONE, 0),
    [0x59] = OP(GETDESCENDANTS, U30, MULTINAME, NONE, NONE, 0),
    [0x5A] = OP(NEWCATCH, U30, EXCEPTION, NONE, NONE, 0),
    [0x5D] = OP(FINDPROPSTRICT, U30, MULTINAME, NONE, NONE, 0),
    [0x5E] = OP(FINDPROPERTY, U30, MULTINAME, NONE, NONE, 0),
    [0x5F] = OP(FINDDEF, U30, MULTINAME, NONE, NONE, 0),
    [0x60] = OP(GETLEX, U30, MULTINAME, NONE, NONE, 0),
    [0x61] = OP(SETPROPERTY, U30, MULTINAME, NONE, NONE, 0),
    [0x62] = OP(GETLOCAL, U30, LOCAL, NONE, NONE, 0),
    [0x63] = OP(SETLOCAL, U30, LOCAL, NONE, NONE, 0),
    [0x64] = OP(GETGLOBALSCOPE, NONE, NONE, NONE, NONE, 0),
    [0x65] = OP(GETSCOPEOBJECT, SCOPE, NONE, NONE, NONE, 0),
    [0x66] = OP(GETPROPERTY, U30, MULTINAME, NONE, NONE, 0),
    [0x67] = OP(GETOUTERSCOPE, U30, OUTER_SCOPE, NONE, NONE, 0),
    [0x68] = OP(INITPROPERTY, U30, MULTINAME, NONE, NONE, 0),
    [0x6A] = OP(DELETEPROPERTY, U30, MULTINAME, NONE, NONE, 0),
    [0x6C] = OP(GETSLOT, U30, SLOT, NONE, NONE, 0),
    [0x6D] = OP(SETSLOT, U30, SLOT, NONE, NONE, 0),
    [0x6E] = OP(GETGLOBALSLOT, U30, SLOT, NONE, NONE, 0),
    [0x6F] = OP(SETGLOBALSLOT, U30, SLOT, NONE, NONE, 0),
    [0x70] = OP(CONVERT_S, NONE, NONE, NONE, NONE, 0),
    [0x71] = OP(ESC_XELEM, NONE, NONE, NONE, NONE, 0),
    [0x72] = OP(ESC_XATTR, NONE, NONE, NONE, NONE, 0),
    [0x73] = OP(CONVERT_I, NONE, NONE, NONE, NONE, 0),
    [0x74] = OP(CONVERT_U, NONE, NONE, NONE, NONE, 0),
    [0x75] = OP(CONVERT_D, NONE, NONE, NONE, NONE, 0),
    [0x76] = OP(CONVERT_B, NONE, NONE, NONE, NONE, 0),
    [0x77] = OP(CONVERT_O, NONE, NONE, NONE, NONE, 0),
    [0x78] = OP(CHECKFILTER, NONE, NONE, NONE, NONE, 0),
    [0x79] = OP(CONVERT_F, NONE, NONE, NONE, NONE, OPF_FLOAT),
    [0x7A] = OP(UNPLUS, NONE, NONE, NONE, NONE, OPF_FLOAT),
    [0x7B] = OP(CONVERT_F4, NONE, NONE, NONE, NONE, OPF_FLOAT),
    [0x80] = OP(COERCE, U30, MULTINAME, NONE, NONE, 0),
    [0x81] = OP(COERCE_B, NONE, NONE, NONE, NONE, 0),
    [0x82] = OP(COERCE_A, NONE, NONE, NONE, NONE, 0),
    [0x83] = OP(COERCE_I, NONE, NONE, NONE, NONE, 0),
    [0x84] = OP(COERCE_D, NONE, NONE, NONE, NONE, 0),
    [0x85] = OP(COERCE_S, NONE, NONE, NONE, NONE, 0),
    [0x86] = OP(ASTYPE, U30, MULTINAME, NONE, NONE, 0),
    [0x87] = OP(ASTYPELATE, NONE, NONE, NONE, NONE, 0),
    [0x88] = OP(COERCE_U, NONE, NONE, NONE, NONE, 0),
    [0x89] = OP(COERCE_O, NONE, NONE, NONE, NONE, 0),
    [0x90] = OP(NEGATE, NONE, NONE, NONE, NONE, 0),
    [0x91] = OP(INCREMENT, NONE, NONE, NONE, NONE, 0),
    [0x92] = OP(INCLOCAL, U30, LOCAL, NONE, NONE, 0),
    [0x93] = OP(DECREMENT, NONE, NONE, NONE, NONE, 0),
    [0x94] = OP(DECLOCAL, U30, LOCAL, NONE, NONE, 0),
    [0x95] = OP(TYPEOF, NONE, NONE, NONE, NONE, 0),
    [0x96] = OP(NOT, NONE, NONE, NONE, NONE, 0),
    [0x97] = OP(BITNOT, NONE, NONE, NONE, NONE, 0),
    [0xA0] = OP(ADD, NONE, NONE, NONE, NONE, 0),
    [0xA1] = OP(SUBTRACT, NONE, NONE, NONE, NONE, 0),
    [0xA2] = OP(MULTIPLY, NONE, NONE, NONE, NONE, 0),
    [0xA3] = OP(DIVIDE, NONE, NONE, NONE, NONE, 0),
    [0xA4] = OP(MODULO, NONE, NONE, NONE, NONE, 0),
    [0xA5] = OP(LSHIFT, NONE, NONE, NONE, NONE, 0),
    [0xA6] = OP(RSHIFT, NONE, NONE, NONE, NONE, 0),
    [0xA7] = OP(URSHIFT, NONE, NONE, NONE, NONE, 0),
    [0xA8] = OP(BITAND, NONE, NONE, NONE, NONE, 0),
    [0xA9] = OP(BITOR, NONE, NONE, NONE, NONE, 0),
    [0xAA] = OP(BITXOR, NONE, NONE, NONE, NONE, 0),
    [0xAB] = OP(EQUALS, NONE, NONE, NONE, NONE, 0),
    [0xAC] = OP(STRICTEQUALS, NONE, NONE, NONE, NONE, 0),
    [0xAD] = OP(LESSTHAN, NONE, NONE, NONE, NONE, 0),
    [0xAE] = OP(LESSEQUALS, NONE, NONE, NONE, NONE, 0),
    [0xAF] = OP(GREATERTHAN, NONE, NONE, NONE, NONE, 0),
    [0xB0] = OP(GREATEREQUALS, NONE, NONE, NONE, NONE, 0),
    [0xB1] = OP(INSTANCEOF, NONE, NONE, NONE, NONE, 0),
    [0xB2] = OP(ISTYPE, U30, MULTINAME, NONE, NONE, 0),
    [0xB3] = OP(ISTYPELATE, NONE, NONE, NONE, NONE, 0),
    [0xB4] = OP(IN, NONE, NONE, NONE, NONE, 0),
    [0xC0] = OP(INCREMENT_I, NONE, NONE, NONE, NONE, 0),
    [0xC1] = OP(DECREMENT_I, NONE, NONE, NONE, NONE, 0),
    [0xC2] = OP(INCLOCAL_I, U30, LOCAL, NONE, NONE, 0),
    [0xC3] = OP(DECLOCAL_I, U30, LOCAL, NONE, NONE, 0),
    [0xC4] = OP(NEGATE_I, NONE, NONE, NONE, NONE, 0),
    [0xC5] = OP(ADD_I, NONE, NONE, NONE, NONE, 0),
    [0xC6] = OP(SUBTRACT_I, NONE, NONE, NONE, NONE, 0),
    [0xC7] = OP(MULTIPLY_I, NONE, NONE, NONE, NONE, 0),
    [0xD0] = OP(GETLOCAL_0, LOCAL_N, NONE, NONE, NONE, 0),
    [0xD1] = OP(GETLOCAL_1, LOCAL_N, NONE, NONE, NONE, 0),
    [0xD2] = OP(GETLOCAL_2, LOCAL_N, NONE, NONE, NONE, 0),
    [0xD3] = OP(GETLOCAL_3, LOCAL_N, NONE, NONE, NONE, 0),
    [0xD4] = OP(SETLOCAL_0, LOCAL_N, NONE, NONE, NONE, 0),
    [0xD5] = OP(SETLOCAL_1, LOCAL_N, NONE, NONE, NONE, 0),
    [0xD6] = OP(SETLOCAL_2, LOCAL_N, NONE, NONE, NONE, 0),
    [0xD7] = OP(SETLOCAL_3, LOCAL_N, NONE, NONE, NONE, 0),
    [0xEF] = OP(DEBUG, DEBUG, NONE, NONE, NONE, 0),
    [0xF0] = OP(DEBUGLINE, U30, IMM, NONE, NONE, 0),
    [0xF1] = OP(DEBUGFILE, U30, STRING, NONE, NONE, 0),
    [0xF2] = OP(BKPTLINE, U30, IMM, NONE, NONE, 0),
    [0xF3] = OP(TIMESTAMP, NONE, NONE, NONE, NONE, 0),
};

/* Indexed by id - 1. */
static const char *const mnemonics[XXBYTE_AVM2_ID_COUNT - 1] = {
    "bkpt",
    "nop",
    "throw",
    "getsuper",
    "setsuper",
    "dxns",
    "dxnslate",
    "kill",
    "label",
    "lf32x4",
    "sf32x4",
    "ifnlt",
    "ifnle",
    "ifngt",
    "ifnge",
    "jump",
    "iftrue",
    "iffalse",
    "ifeq",
    "ifne",
    "iflt",
    "ifle",
    "ifgt",
    "ifge",
    "ifstricteq",
    "ifstrictne",
    "lookupswitch",
    "pushwith",
    "popscope",
    "nextname",
    "hasnext",
    "pushnull",
    "pushundefined",
    "pushfloat",
    "nextvalue",
    "pushbyte",
    "pushshort",
    "pushtrue",
    "pushfalse",
    "pushnan",
    "pop",
    "dup",
    "swap",
    "pushstring",
    "pushint",
    "pushuint",
    "pushdouble",
    "pushscope",
    "pushnamespace",
    "hasnext2",
    "li8",
    "li16",
    "li32",
    "lf32",
    "lf64",
    "si8",
    "si16",
    "si32",
    "sf32",
    "sf64",
    "newfunction",
    "call",
    "construct",
    "callmethod",
    "callstatic",
    "callsuper",
    "callproperty",
    "returnvoid",
    "returnvalue",
    "constructsuper",
    "constructprop",
    "callproplex",
    "callsupervoid",
    "callpropvoid",
    "sxi1",
    "sxi8",
    "sxi16",
    "applytype",
    "pushfloat4",
    "newobject",
    "newarray",
    "newactivation",
    "newclass",
    "getdescendants",
    "newcatch",
    "findpropstrict",
    "findproperty",
    "finddef",
    "getlex",
    "setproperty",
    "getlocal",
    "setlocal",
    "getglobalscope",
    "getscopeobject",
    "getproperty",
    "getouterscope",
    "initproperty",
    "deleteproperty",
    "getslot",
    "setslot",
    "getglobalslot",
    "setglobalslot",
    "convert_s",
    "esc_xelem",
    "esc_xattr",
    "convert_i",
    "convert_u",
    "convert_d",
    "convert_b",
    "convert_o",
    "checkfilter",
    "convert_f",
    "unplus",
    "convert_f4",
    "coerce",
    "coerce_b",
    "coerce_a",
    "coerce_i",
    "coerce_d",
    "coerce_s",
    "astype",
    "astypelate",
    "coerce_u",
    "coerce_o",
    "negate",
    "increment",
    "inclocal",
    "decrement",
    "declocal",
    "typeof",
    "not",
    "bitnot",
    "add",
    "subtract",
    "multiply",
    "divide",
    "modulo",
    "lshift",
    "rshift",
    "urshift",
    "bitand",
    "bitor",
    "bitxor",
    "equals",
    "strictequals",
    "lessthan",
    "lessequals",
    "greaterthan",
    "greaterequals",
    "instanceof",
    "istype",
    "istypelate",
    "in",
    "increment_i",
    "decrement_i",
    "inclocal_i",
    "declocal_i",
    "negate_i",
    "add_i",
    "subtract_i",
    "multiply_i",
    "getlocal_0",
    "getlocal_1",
    "getlocal_2",
    "getlocal_3",
    "setlocal_0",
    "setlocal_1",
    "setlocal_2",
    "setlocal_3",
    "debug",
    "debugline",
    "debugfile",
    "bkptline",
    "timestamp",
};

/* The index prefixes of the text syntax; kinds without one (registers,
 * slots, dispatch ids and scopes) print as plain decimals. */
static const char *index_prefix(uint32_t kind)
{
    switch (kind) {
        case XXBYTE_INDEX_STRING: return "string@";
        case XXBYTE_INDEX_METHOD: return "method@";
        case XXBYTE_INDEX_CLASS: return "class@";
        case XXBYTE_AVM2_INDEX_INT: return "int@";
        case XXBYTE_AVM2_INDEX_UINT: return "uint@";
        case XXBYTE_AVM2_INDEX_DOUBLE: return "double@";
        case XXBYTE_AVM2_INDEX_FLOAT: return "float@";
        case XXBYTE_AVM2_INDEX_FLOAT4: return "float4@";
        case XXBYTE_AVM2_INDEX_NAMESPACE: return "namespace@";
        case XXBYTE_AVM2_INDEX_MULTINAME: return "multiname@";
        case XXBYTE_AVM2_INDEX_EXCEPTION: return "exception@";
        default: return "";
    }
}

static int is_version_mode(uint32_t mode)
{
    uint32_t minor = XXBYTE_AVM2_MODE_MINOR(mode);

    switch (XXBYTE_AVM2_MODE_MAJOR(mode)) {
        case 46: return minor == 16;
        case 47: return minor >= 12 && minor <= 20;
        default: return 0;
    }
}

static xxbyte_status avm2_check_mode(uint32_t mode)
{
    return mode == XXBYTE_AVM2_MODE_DEFAULT || is_version_mode(mode) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

/*
 * The largest fifth byte of a variable-length integer, which contributes
 * bits 28 to 31: a u30 has no bits above 29 and a u32 none above 31. An s32
 * (the integer pools, read by avmplus AbcParser::readS32) and the pushshort
 * operand (AvmCore::readU32, exempt from the verifier's u30 range check) take
 * any fifth byte without the continuation bit and keep its low four bits,
 * so a sign-extended encoding (0x70 to 0x7F) is accepted.
 */
#define LAST_U30 0x03u
#define LAST_U32 0x0Fu
#define LAST_S32 0x7Fu

/* A variable-length integer of at most five bytes at code[*pos]; its fifth
 * byte must not exceed last. */
static xxbyte_status read_var(const uint8_t *code, size_t code_size, size_t *pos, unsigned last, uint32_t *value)
{
    uint32_t result = 0;
    size_t i;

    for (i = 0; i < 5; ++i) {
        uint8_t byte;

        if (*pos + i >= code_size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        byte = code[*pos + i];
        if (i == 4) {
            if (byte > last) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            result |= (uint32_t)(byte & 0x0Fu) << 28;
            break;
        }
        result |= (uint32_t)(byte & 0x7Fu) << (7u * i);
        if ((byte & 0x80u) == 0) {
            break;
        }
    }
    *pos += i + 1;
    *value = result;
    return XXBYTE_STATUS_OK;
}

static int32_t s24le(const uint8_t *p)
{
    return (int32_t)xxbyte_sext((uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16), 24);
}

static int64_t relative_target(uint64_t base, int64_t offset)
{
    return (int64_t)(base + (uint64_t)offset);
}

static void add_value(xxbyte_instruction *instruction, xxbyte_operand_type type, uint32_t size, int64_t value, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, type, size, value);
    if (operand != NULL) {
        operand->flags = flags;
    }
}

/* A u30 operand of kind (K_*). */
static void add_kind(xxbyte_instruction *instruction, uint8_t kind, uint32_t size, uint32_t value)
{
    if (kind == K_IMM) {
        add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, size, value, 0);
    } else {
        xxbyte_add_index(instruction, (xxbyte_index_kind)index_kinds[kind], size, value);
    }
}

static xxbyte_status decode_switch(const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    size_t pos = 4;
    uint32_t case_count;
    uint64_t total;
    xxbyte_status status;

    if (code_size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    status = read_var(code, code_size, &pos, LAST_U30, &case_count);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    /* case_count < 2^30, so the table fits in 32 bits. */
    total = (uint64_t)pos + ((uint64_t)case_count + 1u) * 3u;
    if (total > code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    instruction->size = (uint32_t)total;
    instruction->table_count = case_count + 1u;
    instruction->table_offset = (uint32_t)pos;
    /* lookupswitch offsets are relative to its own address. */
    add_value(instruction, XXBYTE_OPERAND_BRANCH, 3, relative_target(address, s24le(code + 1)), 0);
    add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, (uint32_t)(pos - 4), case_count, 0);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status decode_debug(const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    size_t pos = 2;
    size_t start;
    uint32_t index;
    uint32_t extra;
    uint8_t reg;
    xxbyte_status status;

    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    status = read_var(code, code_size, &pos, LAST_U30, &index);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (pos >= code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    reg = code[pos++];
    start = pos;
    status = read_var(code, code_size, &pos, LAST_U30, &extra);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    instruction->size = (uint32_t)pos;
    add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, code[1], 0);
    xxbyte_add_index(instruction, XXBYTE_INDEX_STRING, (uint32_t)(start - 3), index);
    xxbyte_add_index(instruction, XXBYTE_INDEX_LOCAL, 1, reg);
    add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, (uint32_t)(pos - start), extra, 0);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status avm2_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const avm2_opcode *op = &opcodes[code[0]];
    size_t pos = 1;
    uint32_t first;
    uint32_t second = 0;
    xxbyte_status status = XXBYTE_STATUS_OK;

    (void)context;
    if (op->id == 0 || ((op->flags & OPF_FLOAT) != 0 && mode != XXBYTE_AVM2_MODE_DEFAULT && mode < XXBYTE_AVM2_MODE_FLOAT)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->opcode = code[0];
    instruction->id = op->id;
    instruction->group = op->group;
    instruction->mnemonic = mnemonics[op->id - 1];

    switch (op->form) {
        case F_LOCAL_N: {
            xxbyte_operand *operand = xxbyte_add_index(instruction, XXBYTE_INDEX_LOCAL, 0, code[0] & 3);
            if (operand != NULL) {
                operand->flags = XXBYTE_OPERAND_FLAG_IMPLICIT;
            }
            break;
        }
        case F_U30:
            status = read_var(code, code_size, &pos, LAST_U30, &first);
            if (status == XXBYTE_STATUS_OK) {
                add_kind(instruction, op->kind1, (uint32_t)(pos - 1), first);
            }
            break;
        case F_U30_U30: {
            size_t middle;
            status = read_var(code, code_size, &pos, LAST_U30, &first);
            middle = pos;
            if (status == XXBYTE_STATUS_OK) {
                status = read_var(code, code_size, &pos, LAST_U30, &second);
            }
            if (status == XXBYTE_STATUS_OK) {
                add_kind(instruction, op->kind1, (uint32_t)(middle - 1), first);
                add_kind(instruction, op->kind2, (uint32_t)(pos - middle), second);
            }
            break;
        }
        case F_BRANCH:
            if (code_size < 4) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            pos = 4;
            add_value(instruction, XXBYTE_OPERAND_BRANCH, 3, relative_target(address + 4u, s24le(code + 1)), 0);
            break;
        case F_SWITCH: return decode_switch(code, code_size, address, instruction);
        case F_PUSHBYTE:
            if (code_size < 2) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            pos = 2;
            add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, xxbyte_sext(code[1], 8), XXBYTE_OPERAND_FLAG_SIGNED);
            break;
        case F_PUSHSHORT:
            /* The verifier exempts pushshort from the u30 range check and the
             * VM pushes the low 16 bits sign-extended; extra keeps the raw
             * 32-bit value. */
            status = read_var(code, code_size, &pos, LAST_S32, &first);
            if (status == XXBYTE_STATUS_OK) {
                xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, (uint32_t)(pos - 1), xxbyte_sext(first, 16));
                if (operand != NULL) {
                    operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
                    operand->extra = first;
                }
            }
            break;
        case F_SCOPE:
            if (code_size < 2) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            pos = 2;
            xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_AVM2_INDEX_SCOPE, 1, code[1]);
            break;
        case F_DEBUG: return decode_debug(code, code_size, instruction);
        default: break;
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    instruction->size = (uint32_t)pos;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status avm2_switch_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target)
{
    const uint8_t *entry;

    if (instruction->opcode != OP_LOOKUPSWITCH || instruction->table_offset < 5 ||
        (uint64_t)instruction->table_offset + ((uint64_t)index + 1u) * 3u > instruction->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    entry = instruction->code + instruction->table_offset + (size_t)index * 3u;
    *key = index;
    *target = relative_target(instruction->address, s24le(entry));
    return XXBYTE_STATUS_OK;
}

/* "0x0024": at least four lowercase hex digits; "-0x0010" when negative. */
static int format_target(xxbyte_fmt *fmt, int64_t target)
{
    if (target < 0) {
        return xxbyte_fmt_printf(fmt, "-0x%04llx", (unsigned long long)(0u - (uint64_t)target));
    }
    return xxbyte_fmt_printf(fmt, "0x%04llx", (unsigned long long)target);
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
    switch (operand->type) {
        case XXBYTE_OPERAND_BRANCH: return format_target(fmt, operand->value);
        case XXBYTE_OPERAND_INDEX: return xxbyte_fmt_printf(fmt, "%s%llu", index_prefix(operand->kind), (unsigned long long)operand->value);
        default:
            if ((operand->flags & XXBYTE_OPERAND_FLAG_SIGNED) != 0) {
                return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
            }
            return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
    }
}

/* "lookupswitch 0x0030, [0x0010, 0x0020]": the default target, then every
 * case target in key order. The case count is not printed. */
static int format_switch(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    xxbyte_operand entry;
    uint32_t i;

    if (instruction->operand_count == 0 || !xxbyte_fmt_char(fmt, ' ') || !format_operand(instruction, 0, fmt) || !xxbyte_fmt_str(fmt, ", [")) {
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    entry.type = XXBYTE_OPERAND_BRANCH;
    entry.size = 3;
    for (i = 0; i < instruction->table_count; ++i) {
        int64_t key;
        int resolved;
        if (avm2_switch_entry(instruction, i, &key, &entry.value) != XXBYTE_STATUS_OK || (i != 0 && !xxbyte_fmt_str(fmt, ", "))) {
            return 0;
        }
        resolved = xxbyte_fmt_resolve_entry(fmt, instruction, &entry);
        if (resolved < 0 || (resolved == 0 && !format_target(fmt, entry.value))) {
            return 0;
        }
    }
    return xxbyte_fmt_char(fmt, ']');
}

static int avm2_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;
    int first = 1;

    if (instruction->opcode == OP_LOOKUPSWITCH) {
        return format_switch(instruction, fmt);
    }
    for (i = 0; i < instruction->operand_count; ++i) {
        if ((instruction->operands[i].flags & XXBYTE_OPERAND_FLAG_IMPLICIT) != 0) {
            continue;
        }
        if (!xxbyte_fmt_str(fmt, first ? " " : ", ") || !format_operand(instruction, i, fmt)) {
            return 0;
        }
        first = 0;
    }
    return 1;
}

const xxbyte_family_ops xxbyte_avm2_ops = {
    XXBYTE_FAMILY_AVM2, XXBYTE_AVM2_MODE_DEFAULT, avm2_check_mode, avm2_decode, avm2_switch_entry, avm2_format_operands, NULL, xxbyte_avm2_list_file,
};

const char *xxbyte_avm2_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_AVM2_ID_COUNT) {
        return NULL;
    }
    return mnemonics[id - 1];
}

static xxbyte_status read_checked(const uint8_t *data, size_t size, size_t *offset, unsigned last, uint32_t *value)
{
    size_t pos;
    xxbyte_status status;

    if (value != NULL) {
        *value = 0;
    }
    if ((data == NULL && size != 0) || offset == NULL || value == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    pos = *offset;
    if (pos >= size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    status = read_var(data, size, &pos, last, value);
    if (status == XXBYTE_STATUS_OK) {
        *offset = pos;
    }
    return status;
}

xxbyte_status xxbyte_avm2_read_u30(const uint8_t *data, size_t size, size_t *offset, uint32_t *value)
{
    return read_checked(data, size, offset, LAST_U30, value);
}

xxbyte_status xxbyte_avm2_read_u32(const uint8_t *data, size_t size, size_t *offset, uint32_t *value)
{
    return read_checked(data, size, offset, LAST_U32, value);
}

xxbyte_status xxbyte_avm2_read_s32(const uint8_t *data, size_t size, size_t *offset, int32_t *value)
{
    uint32_t bits = 0;
    xxbyte_status status;

    if (value == NULL) {
        return read_checked(data, size, offset, LAST_S32, NULL);
    }
    status = read_checked(data, size, offset, LAST_S32, &bits);
    *value = (int32_t)xxbyte_sext(bits, 32);
    return status;
}

#else
typedef int xxbyte_avm2_disabled;
#endif
