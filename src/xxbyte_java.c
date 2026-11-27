/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_java_internal.h"

#if XXBYTE_USE_JAVA

/* Groups. jsr and jsr_w transfer to a subroutine that returns with ret, so
 * they are classed as calls. */
#define G_NONE XXBYTE_GROUP_NONE
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_GOTO (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_CALL XXBYTE_GROUP_CALL
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_SWITCH (XXBYTE_GROUP_SWITCH | XXBYTE_GROUP_STOP)
#define G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)

/* Operand layouts after the opcode byte. */
enum {
    F_NONE,
    F_ICONST,          /* implicit int constant */
    F_LCONST,          /* implicit long constant */
    F_FCONST,          /* implicit float constant */
    F_DCONST,          /* implicit double constant */
    F_BIPUSH,          /* s1 */
    F_SIPUSH,          /* s2 */
    F_LDC,             /* u1 constant-pool index */
    F_CPOOL,           /* u2 constant-pool index */
    F_LOCAL,           /* u1 local slot */
    F_LOCAL2,          /* u1 local slot of a long or double */
    F_LOCAL_N,         /* implicit local slot */
    F_LOCAL2_N,        /* implicit local slot of a long or double */
    F_IINC,            /* u1 local slot, s1 constant */
    F_BRANCH,          /* s2 offset */
    F_BRANCH_W,        /* s4 offset */
    F_TABLESWITCH,     /* padding, s4 default, s4 low, s4 high, s4 offsets */
    F_LOOKUPSWITCH,    /* padding, s4 default, s4 npairs, s4 pairs */
    F_INVOKEINTERFACE, /* u2 index, u1 count, u1 0 */
    F_INVOKEDYNAMIC,   /* u2 index, u1 0, u1 0 */
    F_NEWARRAY,        /* u1 atype */
    F_MULTIANEWARRAY,  /* u2 index, u1 dimensions */
    F_COUNT
};

/* Instruction sizes by layout; the switches only count their opcode here. */
static const uint8_t form_sizes[F_COUNT] = {
    [F_NONE] = 1,        [F_ICONST] = 1,       [F_LCONST] = 1,          [F_FCONST] = 1,        [F_DCONST] = 1,   [F_BIPUSH] = 2,         [F_SIPUSH] = 3, [F_LDC] = 2,
    [F_CPOOL] = 3,       [F_LOCAL] = 2,        [F_LOCAL2] = 2,          [F_LOCAL_N] = 1,       [F_LOCAL2_N] = 1, [F_IINC] = 3,           [F_BRANCH] = 3, [F_BRANCH_W] = 5,
    [F_TABLESWITCH] = 1, [F_LOOKUPSWITCH] = 1, [F_INVOKEINTERFACE] = 5, [F_INVOKEDYNAMIC] = 5, [F_NEWARRAY] = 2, [F_MULTIANEWARRAY] = 4,
};

#define WIDE_PREFIX 0xC4u
#define OP_TABLESWITCH 0xAAu
#define OP_LOOKUPSWITCH 0xABu
#define OP_INVOKEDYNAMIC 0xBAu
#define OP_NEWARRAY 0xBCu

/* Indexed by id - 1. */
static const char *const mnemonics[XXBYTE_JAVA_ID_COUNT - 1] = {
    "nop",
    "aconst_null",
    "iconst_m1",
    "iconst_0",
    "iconst_1",
    "iconst_2",
    "iconst_3",
    "iconst_4",
    "iconst_5",
    "lconst_0",
    "lconst_1",
    "fconst_0",
    "fconst_1",
    "fconst_2",
    "dconst_0",
    "dconst_1",
    "bipush",
    "sipush",
    "ldc",
    "ldc_w",
    "ldc2_w",
    "iload",
    "lload",
    "fload",
    "dload",
    "aload",
    "iload_0",
    "iload_1",
    "iload_2",
    "iload_3",
    "lload_0",
    "lload_1",
    "lload_2",
    "lload_3",
    "fload_0",
    "fload_1",
    "fload_2",
    "fload_3",
    "dload_0",
    "dload_1",
    "dload_2",
    "dload_3",
    "aload_0",
    "aload_1",
    "aload_2",
    "aload_3",
    "iaload",
    "laload",
    "faload",
    "daload",
    "aaload",
    "baload",
    "caload",
    "saload",
    "istore",
    "lstore",
    "fstore",
    "dstore",
    "astore",
    "istore_0",
    "istore_1",
    "istore_2",
    "istore_3",
    "lstore_0",
    "lstore_1",
    "lstore_2",
    "lstore_3",
    "fstore_0",
    "fstore_1",
    "fstore_2",
    "fstore_3",
    "dstore_0",
    "dstore_1",
    "dstore_2",
    "dstore_3",
    "astore_0",
    "astore_1",
    "astore_2",
    "astore_3",
    "iastore",
    "lastore",
    "fastore",
    "dastore",
    "aastore",
    "bastore",
    "castore",
    "sastore",
    "pop",
    "pop2",
    "dup",
    "dup_x1",
    "dup_x2",
    "dup2",
    "dup2_x1",
    "dup2_x2",
    "swap",
    "iadd",
    "ladd",
    "fadd",
    "dadd",
    "isub",
    "lsub",
    "fsub",
    "dsub",
    "imul",
    "lmul",
    "fmul",
    "dmul",
    "idiv",
    "ldiv",
    "fdiv",
    "ddiv",
    "irem",
    "lrem",
    "frem",
    "drem",
    "ineg",
    "lneg",
    "fneg",
    "dneg",
    "ishl",
    "lshl",
    "ishr",
    "lshr",
    "iushr",
    "lushr",
    "iand",
    "land",
    "ior",
    "lor",
    "ixor",
    "lxor",
    "iinc",
    "i2l",
    "i2f",
    "i2d",
    "l2i",
    "l2f",
    "l2d",
    "f2i",
    "f2l",
    "f2d",
    "d2i",
    "d2l",
    "d2f",
    "i2b",
    "i2c",
    "i2s",
    "lcmp",
    "fcmpl",
    "fcmpg",
    "dcmpl",
    "dcmpg",
    "ifeq",
    "ifne",
    "iflt",
    "ifge",
    "ifgt",
    "ifle",
    "if_icmpeq",
    "if_icmpne",
    "if_icmplt",
    "if_icmpge",
    "if_icmpgt",
    "if_icmple",
    "if_acmpeq",
    "if_acmpne",
    "goto",
    "jsr",
    "ret",
    "tableswitch",
    "lookupswitch",
    "ireturn",
    "lreturn",
    "freturn",
    "dreturn",
    "areturn",
    "return",
    "getstatic",
    "putstatic",
    "getfield",
    "putfield",
    "invokevirtual",
    "invokespecial",
    "invokestatic",
    "invokeinterface",
    "invokedynamic",
    "new",
    "newarray",
    "anewarray",
    "arraylength",
    "athrow",
    "checkcast",
    "instanceof",
    "monitorenter",
    "monitorexit",
    "multianewarray",
    "ifnull",
    "ifnonnull",
    "goto_w",
    "jsr_w",
    "breakpoint",
    "impdep1",
    "impdep2",
    "iload_w",
    "lload_w",
    "fload_w",
    "dload_w",
    "aload_w",
    "istore_w",
    "lstore_w",
    "fstore_w",
    "dstore_w",
    "astore_w",
    "iinc_w",
    "ret_w",
};

typedef struct java_opcode {
    uint8_t id;     /* XXBYTE_JAVA_ID_*, 0 for an undefined opcode */
    uint8_t form;   /* F_* */
    int8_t value;   /* implicit local slot or constant */
    uint16_t group; /* XXBYTE_GROUP_* */
} java_opcode;

/* JVMS SE 25 chapter 6 by opcode. wide (0xC4) is decoded separately. */
static const java_opcode opcodes[256] = {
    [0x00] = {XXBYTE_JAVA_ID_NOP, F_NONE, 0, G_NONE},
    [0x01] = {XXBYTE_JAVA_ID_ACONST_NULL, F_NONE, 0, G_NONE},
    [0x02] = {XXBYTE_JAVA_ID_ICONST_M1, F_ICONST, -1, G_NONE},
    [0x03] = {XXBYTE_JAVA_ID_ICONST_0, F_ICONST, 0, G_NONE},
    [0x04] = {XXBYTE_JAVA_ID_ICONST_1, F_ICONST, 1, G_NONE},
    [0x05] = {XXBYTE_JAVA_ID_ICONST_2, F_ICONST, 2, G_NONE},
    [0x06] = {XXBYTE_JAVA_ID_ICONST_3, F_ICONST, 3, G_NONE},
    [0x07] = {XXBYTE_JAVA_ID_ICONST_4, F_ICONST, 4, G_NONE},
    [0x08] = {XXBYTE_JAVA_ID_ICONST_5, F_ICONST, 5, G_NONE},
    [0x09] = {XXBYTE_JAVA_ID_LCONST_0, F_LCONST, 0, G_NONE},
    [0x0A] = {XXBYTE_JAVA_ID_LCONST_1, F_LCONST, 1, G_NONE},
    [0x0B] = {XXBYTE_JAVA_ID_FCONST_0, F_FCONST, 0, G_NONE},
    [0x0C] = {XXBYTE_JAVA_ID_FCONST_1, F_FCONST, 1, G_NONE},
    [0x0D] = {XXBYTE_JAVA_ID_FCONST_2, F_FCONST, 2, G_NONE},
    [0x0E] = {XXBYTE_JAVA_ID_DCONST_0, F_DCONST, 0, G_NONE},
    [0x0F] = {XXBYTE_JAVA_ID_DCONST_1, F_DCONST, 1, G_NONE},
    [0x10] = {XXBYTE_JAVA_ID_BIPUSH, F_BIPUSH, 0, G_NONE},
    [0x11] = {XXBYTE_JAVA_ID_SIPUSH, F_SIPUSH, 0, G_NONE},
    [0x12] = {XXBYTE_JAVA_ID_LDC, F_LDC, 0, G_NONE},
    [0x13] = {XXBYTE_JAVA_ID_LDC_W, F_CPOOL, 0, G_NONE},
    [0x14] = {XXBYTE_JAVA_ID_LDC2_W, F_CPOOL, 0, G_NONE},
    [0x15] = {XXBYTE_JAVA_ID_ILOAD, F_LOCAL, 0, G_NONE},
    [0x16] = {XXBYTE_JAVA_ID_LLOAD, F_LOCAL2, 0, G_NONE},
    [0x17] = {XXBYTE_JAVA_ID_FLOAD, F_LOCAL, 0, G_NONE},
    [0x18] = {XXBYTE_JAVA_ID_DLOAD, F_LOCAL2, 0, G_NONE},
    [0x19] = {XXBYTE_JAVA_ID_ALOAD, F_LOCAL, 0, G_NONE},
    [0x1A] = {XXBYTE_JAVA_ID_ILOAD_0, F_LOCAL_N, 0, G_NONE},
    [0x1B] = {XXBYTE_JAVA_ID_ILOAD_1, F_LOCAL_N, 1, G_NONE},
    [0x1C] = {XXBYTE_JAVA_ID_ILOAD_2, F_LOCAL_N, 2, G_NONE},
    [0x1D] = {XXBYTE_JAVA_ID_ILOAD_3, F_LOCAL_N, 3, G_NONE},
    [0x1E] = {XXBYTE_JAVA_ID_LLOAD_0, F_LOCAL2_N, 0, G_NONE},
    [0x1F] = {XXBYTE_JAVA_ID_LLOAD_1, F_LOCAL2_N, 1, G_NONE},
    [0x20] = {XXBYTE_JAVA_ID_LLOAD_2, F_LOCAL2_N, 2, G_NONE},
    [0x21] = {XXBYTE_JAVA_ID_LLOAD_3, F_LOCAL2_N, 3, G_NONE},
    [0x22] = {XXBYTE_JAVA_ID_FLOAD_0, F_LOCAL_N, 0, G_NONE},
    [0x23] = {XXBYTE_JAVA_ID_FLOAD_1, F_LOCAL_N, 1, G_NONE},
    [0x24] = {XXBYTE_JAVA_ID_FLOAD_2, F_LOCAL_N, 2, G_NONE},
    [0x25] = {XXBYTE_JAVA_ID_FLOAD_3, F_LOCAL_N, 3, G_NONE},
    [0x26] = {XXBYTE_JAVA_ID_DLOAD_0, F_LOCAL2_N, 0, G_NONE},
    [0x27] = {XXBYTE_JAVA_ID_DLOAD_1, F_LOCAL2_N, 1, G_NONE},
    [0x28] = {XXBYTE_JAVA_ID_DLOAD_2, F_LOCAL2_N, 2, G_NONE},
    [0x29] = {XXBYTE_JAVA_ID_DLOAD_3, F_LOCAL2_N, 3, G_NONE},
    [0x2A] = {XXBYTE_JAVA_ID_ALOAD_0, F_LOCAL_N, 0, G_NONE},
    [0x2B] = {XXBYTE_JAVA_ID_ALOAD_1, F_LOCAL_N, 1, G_NONE},
    [0x2C] = {XXBYTE_JAVA_ID_ALOAD_2, F_LOCAL_N, 2, G_NONE},
    [0x2D] = {XXBYTE_JAVA_ID_ALOAD_3, F_LOCAL_N, 3, G_NONE},
    [0x2E] = {XXBYTE_JAVA_ID_IALOAD, F_NONE, 0, G_NONE},
    [0x2F] = {XXBYTE_JAVA_ID_LALOAD, F_NONE, 0, G_NONE},
    [0x30] = {XXBYTE_JAVA_ID_FALOAD, F_NONE, 0, G_NONE},
    [0x31] = {XXBYTE_JAVA_ID_DALOAD, F_NONE, 0, G_NONE},
    [0x32] = {XXBYTE_JAVA_ID_AALOAD, F_NONE, 0, G_NONE},
    [0x33] = {XXBYTE_JAVA_ID_BALOAD, F_NONE, 0, G_NONE},
    [0x34] = {XXBYTE_JAVA_ID_CALOAD, F_NONE, 0, G_NONE},
    [0x35] = {XXBYTE_JAVA_ID_SALOAD, F_NONE, 0, G_NONE},
    [0x36] = {XXBYTE_JAVA_ID_ISTORE, F_LOCAL, 0, G_NONE},
    [0x37] = {XXBYTE_JAVA_ID_LSTORE, F_LOCAL2, 0, G_NONE},
    [0x38] = {XXBYTE_JAVA_ID_FSTORE, F_LOCAL, 0, G_NONE},
    [0x39] = {XXBYTE_JAVA_ID_DSTORE, F_LOCAL2, 0, G_NONE},
    [0x3A] = {XXBYTE_JAVA_ID_ASTORE, F_LOCAL, 0, G_NONE},
    [0x3B] = {XXBYTE_JAVA_ID_ISTORE_0, F_LOCAL_N, 0, G_NONE},
    [0x3C] = {XXBYTE_JAVA_ID_ISTORE_1, F_LOCAL_N, 1, G_NONE},
    [0x3D] = {XXBYTE_JAVA_ID_ISTORE_2, F_LOCAL_N, 2, G_NONE},
    [0x3E] = {XXBYTE_JAVA_ID_ISTORE_3, F_LOCAL_N, 3, G_NONE},
    [0x3F] = {XXBYTE_JAVA_ID_LSTORE_0, F_LOCAL2_N, 0, G_NONE},
    [0x40] = {XXBYTE_JAVA_ID_LSTORE_1, F_LOCAL2_N, 1, G_NONE},
    [0x41] = {XXBYTE_JAVA_ID_LSTORE_2, F_LOCAL2_N, 2, G_NONE},
    [0x42] = {XXBYTE_JAVA_ID_LSTORE_3, F_LOCAL2_N, 3, G_NONE},
    [0x43] = {XXBYTE_JAVA_ID_FSTORE_0, F_LOCAL_N, 0, G_NONE},
    [0x44] = {XXBYTE_JAVA_ID_FSTORE_1, F_LOCAL_N, 1, G_NONE},
    [0x45] = {XXBYTE_JAVA_ID_FSTORE_2, F_LOCAL_N, 2, G_NONE},
    [0x46] = {XXBYTE_JAVA_ID_FSTORE_3, F_LOCAL_N, 3, G_NONE},
    [0x47] = {XXBYTE_JAVA_ID_DSTORE_0, F_LOCAL2_N, 0, G_NONE},
    [0x48] = {XXBYTE_JAVA_ID_DSTORE_1, F_LOCAL2_N, 1, G_NONE},
    [0x49] = {XXBYTE_JAVA_ID_DSTORE_2, F_LOCAL2_N, 2, G_NONE},
    [0x4A] = {XXBYTE_JAVA_ID_DSTORE_3, F_LOCAL2_N, 3, G_NONE},
    [0x4B] = {XXBYTE_JAVA_ID_ASTORE_0, F_LOCAL_N, 0, G_NONE},
    [0x4C] = {XXBYTE_JAVA_ID_ASTORE_1, F_LOCAL_N, 1, G_NONE},
    [0x4D] = {XXBYTE_JAVA_ID_ASTORE_2, F_LOCAL_N, 2, G_NONE},
    [0x4E] = {XXBYTE_JAVA_ID_ASTORE_3, F_LOCAL_N, 3, G_NONE},
    [0x4F] = {XXBYTE_JAVA_ID_IASTORE, F_NONE, 0, G_NONE},
    [0x50] = {XXBYTE_JAVA_ID_LASTORE, F_NONE, 0, G_NONE},
    [0x51] = {XXBYTE_JAVA_ID_FASTORE, F_NONE, 0, G_NONE},
    [0x52] = {XXBYTE_JAVA_ID_DASTORE, F_NONE, 0, G_NONE},
    [0x53] = {XXBYTE_JAVA_ID_AASTORE, F_NONE, 0, G_NONE},
    [0x54] = {XXBYTE_JAVA_ID_BASTORE, F_NONE, 0, G_NONE},
    [0x55] = {XXBYTE_JAVA_ID_CASTORE, F_NONE, 0, G_NONE},
    [0x56] = {XXBYTE_JAVA_ID_SASTORE, F_NONE, 0, G_NONE},
    [0x57] = {XXBYTE_JAVA_ID_POP, F_NONE, 0, G_NONE},
    [0x58] = {XXBYTE_JAVA_ID_POP2, F_NONE, 0, G_NONE},
    [0x59] = {XXBYTE_JAVA_ID_DUP, F_NONE, 0, G_NONE},
    [0x5A] = {XXBYTE_JAVA_ID_DUP_X1, F_NONE, 0, G_NONE},
    [0x5B] = {XXBYTE_JAVA_ID_DUP_X2, F_NONE, 0, G_NONE},
    [0x5C] = {XXBYTE_JAVA_ID_DUP2, F_NONE, 0, G_NONE},
    [0x5D] = {XXBYTE_JAVA_ID_DUP2_X1, F_NONE, 0, G_NONE},
    [0x5E] = {XXBYTE_JAVA_ID_DUP2_X2, F_NONE, 0, G_NONE},
    [0x5F] = {XXBYTE_JAVA_ID_SWAP, F_NONE, 0, G_NONE},
    [0x60] = {XXBYTE_JAVA_ID_IADD, F_NONE, 0, G_NONE},
    [0x61] = {XXBYTE_JAVA_ID_LADD, F_NONE, 0, G_NONE},
    [0x62] = {XXBYTE_JAVA_ID_FADD, F_NONE, 0, G_NONE},
    [0x63] = {XXBYTE_JAVA_ID_DADD, F_NONE, 0, G_NONE},
    [0x64] = {XXBYTE_JAVA_ID_ISUB, F_NONE, 0, G_NONE},
    [0x65] = {XXBYTE_JAVA_ID_LSUB, F_NONE, 0, G_NONE},
    [0x66] = {XXBYTE_JAVA_ID_FSUB, F_NONE, 0, G_NONE},
    [0x67] = {XXBYTE_JAVA_ID_DSUB, F_NONE, 0, G_NONE},
    [0x68] = {XXBYTE_JAVA_ID_IMUL, F_NONE, 0, G_NONE},
    [0x69] = {XXBYTE_JAVA_ID_LMUL, F_NONE, 0, G_NONE},
    [0x6A] = {XXBYTE_JAVA_ID_FMUL, F_NONE, 0, G_NONE},
    [0x6B] = {XXBYTE_JAVA_ID_DMUL, F_NONE, 0, G_NONE},
    [0x6C] = {XXBYTE_JAVA_ID_IDIV, F_NONE, 0, G_NONE},
    [0x6D] = {XXBYTE_JAVA_ID_LDIV, F_NONE, 0, G_NONE},
    [0x6E] = {XXBYTE_JAVA_ID_FDIV, F_NONE, 0, G_NONE},
    [0x6F] = {XXBYTE_JAVA_ID_DDIV, F_NONE, 0, G_NONE},
    [0x70] = {XXBYTE_JAVA_ID_IREM, F_NONE, 0, G_NONE},
    [0x71] = {XXBYTE_JAVA_ID_LREM, F_NONE, 0, G_NONE},
    [0x72] = {XXBYTE_JAVA_ID_FREM, F_NONE, 0, G_NONE},
    [0x73] = {XXBYTE_JAVA_ID_DREM, F_NONE, 0, G_NONE},
    [0x74] = {XXBYTE_JAVA_ID_INEG, F_NONE, 0, G_NONE},
    [0x75] = {XXBYTE_JAVA_ID_LNEG, F_NONE, 0, G_NONE},
    [0x76] = {XXBYTE_JAVA_ID_FNEG, F_NONE, 0, G_NONE},
    [0x77] = {XXBYTE_JAVA_ID_DNEG, F_NONE, 0, G_NONE},
    [0x78] = {XXBYTE_JAVA_ID_ISHL, F_NONE, 0, G_NONE},
    [0x79] = {XXBYTE_JAVA_ID_LSHL, F_NONE, 0, G_NONE},
    [0x7A] = {XXBYTE_JAVA_ID_ISHR, F_NONE, 0, G_NONE},
    [0x7B] = {XXBYTE_JAVA_ID_LSHR, F_NONE, 0, G_NONE},
    [0x7C] = {XXBYTE_JAVA_ID_IUSHR, F_NONE, 0, G_NONE},
    [0x7D] = {XXBYTE_JAVA_ID_LUSHR, F_NONE, 0, G_NONE},
    [0x7E] = {XXBYTE_JAVA_ID_IAND, F_NONE, 0, G_NONE},
    [0x7F] = {XXBYTE_JAVA_ID_LAND, F_NONE, 0, G_NONE},
    [0x80] = {XXBYTE_JAVA_ID_IOR, F_NONE, 0, G_NONE},
    [0x81] = {XXBYTE_JAVA_ID_LOR, F_NONE, 0, G_NONE},
    [0x82] = {XXBYTE_JAVA_ID_IXOR, F_NONE, 0, G_NONE},
    [0x83] = {XXBYTE_JAVA_ID_LXOR, F_NONE, 0, G_NONE},
    [0x84] = {XXBYTE_JAVA_ID_IINC, F_IINC, 0, G_NONE},
    [0x85] = {XXBYTE_JAVA_ID_I2L, F_NONE, 0, G_NONE},
    [0x86] = {XXBYTE_JAVA_ID_I2F, F_NONE, 0, G_NONE},
    [0x87] = {XXBYTE_JAVA_ID_I2D, F_NONE, 0, G_NONE},
    [0x88] = {XXBYTE_JAVA_ID_L2I, F_NONE, 0, G_NONE},
    [0x89] = {XXBYTE_JAVA_ID_L2F, F_NONE, 0, G_NONE},
    [0x8A] = {XXBYTE_JAVA_ID_L2D, F_NONE, 0, G_NONE},
    [0x8B] = {XXBYTE_JAVA_ID_F2I, F_NONE, 0, G_NONE},
    [0x8C] = {XXBYTE_JAVA_ID_F2L, F_NONE, 0, G_NONE},
    [0x8D] = {XXBYTE_JAVA_ID_F2D, F_NONE, 0, G_NONE},
    [0x8E] = {XXBYTE_JAVA_ID_D2I, F_NONE, 0, G_NONE},
    [0x8F] = {XXBYTE_JAVA_ID_D2L, F_NONE, 0, G_NONE},
    [0x90] = {XXBYTE_JAVA_ID_D2F, F_NONE, 0, G_NONE},
    [0x91] = {XXBYTE_JAVA_ID_I2B, F_NONE, 0, G_NONE},
    [0x92] = {XXBYTE_JAVA_ID_I2C, F_NONE, 0, G_NONE},
    [0x93] = {XXBYTE_JAVA_ID_I2S, F_NONE, 0, G_NONE},
    [0x94] = {XXBYTE_JAVA_ID_LCMP, F_NONE, 0, G_NONE},
    [0x95] = {XXBYTE_JAVA_ID_FCMPL, F_NONE, 0, G_NONE},
    [0x96] = {XXBYTE_JAVA_ID_FCMPG, F_NONE, 0, G_NONE},
    [0x97] = {XXBYTE_JAVA_ID_DCMPL, F_NONE, 0, G_NONE},
    [0x98] = {XXBYTE_JAVA_ID_DCMPG, F_NONE, 0, G_NONE},
    [0x99] = {XXBYTE_JAVA_ID_IFEQ, F_BRANCH, 0, G_COND},
    [0x9A] = {XXBYTE_JAVA_ID_IFNE, F_BRANCH, 0, G_COND},
    [0x9B] = {XXBYTE_JAVA_ID_IFLT, F_BRANCH, 0, G_COND},
    [0x9C] = {XXBYTE_JAVA_ID_IFGE, F_BRANCH, 0, G_COND},
    [0x9D] = {XXBYTE_JAVA_ID_IFGT, F_BRANCH, 0, G_COND},
    [0x9E] = {XXBYTE_JAVA_ID_IFLE, F_BRANCH, 0, G_COND},
    [0x9F] = {XXBYTE_JAVA_ID_IF_ICMPEQ, F_BRANCH, 0, G_COND},
    [0xA0] = {XXBYTE_JAVA_ID_IF_ICMPNE, F_BRANCH, 0, G_COND},
    [0xA1] = {XXBYTE_JAVA_ID_IF_ICMPLT, F_BRANCH, 0, G_COND},
    [0xA2] = {XXBYTE_JAVA_ID_IF_ICMPGE, F_BRANCH, 0, G_COND},
    [0xA3] = {XXBYTE_JAVA_ID_IF_ICMPGT, F_BRANCH, 0, G_COND},
    [0xA4] = {XXBYTE_JAVA_ID_IF_ICMPLE, F_BRANCH, 0, G_COND},
    [0xA5] = {XXBYTE_JAVA_ID_IF_ACMPEQ, F_BRANCH, 0, G_COND},
    [0xA6] = {XXBYTE_JAVA_ID_IF_ACMPNE, F_BRANCH, 0, G_COND},
    [0xA7] = {XXBYTE_JAVA_ID_GOTO, F_BRANCH, 0, G_GOTO},
    [0xA8] = {XXBYTE_JAVA_ID_JSR, F_BRANCH, 0, G_CALL},
    [0xA9] = {XXBYTE_JAVA_ID_RET, F_LOCAL, 0, G_RETURN},
    [0xAA] = {XXBYTE_JAVA_ID_TABLESWITCH, F_TABLESWITCH, 0, G_SWITCH},
    [0xAB] = {XXBYTE_JAVA_ID_LOOKUPSWITCH, F_LOOKUPSWITCH, 0, G_SWITCH},
    [0xAC] = {XXBYTE_JAVA_ID_IRETURN, F_NONE, 0, G_RETURN},
    [0xAD] = {XXBYTE_JAVA_ID_LRETURN, F_NONE, 0, G_RETURN},
    [0xAE] = {XXBYTE_JAVA_ID_FRETURN, F_NONE, 0, G_RETURN},
    [0xAF] = {XXBYTE_JAVA_ID_DRETURN, F_NONE, 0, G_RETURN},
    [0xB0] = {XXBYTE_JAVA_ID_ARETURN, F_NONE, 0, G_RETURN},
    [0xB1] = {XXBYTE_JAVA_ID_RETURN, F_NONE, 0, G_RETURN},
    [0xB2] = {XXBYTE_JAVA_ID_GETSTATIC, F_CPOOL, 0, G_NONE},
    [0xB3] = {XXBYTE_JAVA_ID_PUTSTATIC, F_CPOOL, 0, G_NONE},
    [0xB4] = {XXBYTE_JAVA_ID_GETFIELD, F_CPOOL, 0, G_NONE},
    [0xB5] = {XXBYTE_JAVA_ID_PUTFIELD, F_CPOOL, 0, G_NONE},
    [0xB6] = {XXBYTE_JAVA_ID_INVOKEVIRTUAL, F_CPOOL, 0, G_CALL},
    [0xB7] = {XXBYTE_JAVA_ID_INVOKESPECIAL, F_CPOOL, 0, G_CALL},
    [0xB8] = {XXBYTE_JAVA_ID_INVOKESTATIC, F_CPOOL, 0, G_CALL},
    [0xB9] = {XXBYTE_JAVA_ID_INVOKEINTERFACE, F_INVOKEINTERFACE, 0, G_CALL},
    [0xBA] = {XXBYTE_JAVA_ID_INVOKEDYNAMIC, F_INVOKEDYNAMIC, 0, G_CALL},
    [0xBB] = {XXBYTE_JAVA_ID_NEW, F_CPOOL, 0, G_NONE},
    [0xBC] = {XXBYTE_JAVA_ID_NEWARRAY, F_NEWARRAY, 0, G_NONE},
    [0xBD] = {XXBYTE_JAVA_ID_ANEWARRAY, F_CPOOL, 0, G_NONE},
    [0xBE] = {XXBYTE_JAVA_ID_ARRAYLENGTH, F_NONE, 0, G_NONE},
    [0xBF] = {XXBYTE_JAVA_ID_ATHROW, F_NONE, 0, G_THROW},
    [0xC0] = {XXBYTE_JAVA_ID_CHECKCAST, F_CPOOL, 0, G_NONE},
    [0xC1] = {XXBYTE_JAVA_ID_INSTANCEOF, F_CPOOL, 0, G_NONE},
    [0xC2] = {XXBYTE_JAVA_ID_MONITORENTER, F_NONE, 0, G_NONE},
    [0xC3] = {XXBYTE_JAVA_ID_MONITOREXIT, F_NONE, 0, G_NONE},
    [0xC5] = {XXBYTE_JAVA_ID_MULTIANEWARRAY, F_MULTIANEWARRAY, 0, G_NONE},
    [0xC6] = {XXBYTE_JAVA_ID_IFNULL, F_BRANCH, 0, G_COND},
    [0xC7] = {XXBYTE_JAVA_ID_IFNONNULL, F_BRANCH, 0, G_COND},
    [0xC8] = {XXBYTE_JAVA_ID_GOTO_W, F_BRANCH_W, 0, G_GOTO},
    [0xC9] = {XXBYTE_JAVA_ID_JSR_W, F_BRANCH_W, 0, G_CALL},
    [0xCA] = {XXBYTE_JAVA_ID_BREAKPOINT, F_NONE, 0, G_NONE},
    [0xFE] = {XXBYTE_JAVA_ID_IMPDEP1, F_NONE, 0, G_NONE},
    [0xFF] = {XXBYTE_JAVA_ID_IMPDEP2, F_NONE, 0, G_NONE},
};

/* The opcodes wide can modify, with the ids of the combined forms. */
static const struct {
    uint8_t opcode;
    uint8_t id;
} wide_forms[] = {
    {0x15, XXBYTE_JAVA_ID_ILOAD_W},  {0x16, XXBYTE_JAVA_ID_LLOAD_W},  {0x17, XXBYTE_JAVA_ID_FLOAD_W},  {0x18, XXBYTE_JAVA_ID_DLOAD_W},
    {0x19, XXBYTE_JAVA_ID_ALOAD_W},  {0x36, XXBYTE_JAVA_ID_ISTORE_W}, {0x37, XXBYTE_JAVA_ID_LSTORE_W}, {0x38, XXBYTE_JAVA_ID_FSTORE_W},
    {0x39, XXBYTE_JAVA_ID_DSTORE_W}, {0x3A, XXBYTE_JAVA_ID_ASTORE_W}, {0x84, XXBYTE_JAVA_ID_IINC_W},   {0xA9, XXBYTE_JAVA_ID_RET_W},
};

static const char *const array_type_names[] = {
    "boolean", "char", "float", "double", "byte", "short", "int", "long",
};

static const uint32_t fconst_bits[] = {0x00000000u, 0x3F800000u, 0x40000000u};
static const uint64_t dconst_bits[] = {
    UINT64_C(0x0000000000000000),
    UINT64_C(0x3FF0000000000000),
};

static xxbyte_status java_check_mode(uint32_t mode)
{
    return mode == XXBYTE_JAVA_MODE_DEFAULT ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static int32_t s32be(const uint8_t *p)
{
    return (int32_t)xxbyte_sext(xxbyte_u32be(p), 32);
}

/* Branch offsets are relative to the opcode of the branching instruction. */
static int64_t branch_target(uint64_t address, int64_t offset)
{
    return (int64_t)(address + (uint64_t)offset);
}

static void add_value(xxbyte_instruction *instruction, xxbyte_operand_type type, uint32_t size, int64_t value, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, type, size, value);
    if (operand != NULL) {
        operand->flags = flags;
    }
}

static void add_slot(xxbyte_instruction *instruction, xxbyte_index_kind kind, uint32_t size, int64_t value, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_index(instruction, kind, size, value);
    if (operand != NULL) {
        operand->flags = flags;
    }
}

static void set_mnemonic(xxbyte_instruction *instruction, uint32_t opcode, uint32_t id, uint32_t group, uint32_t size)
{
    instruction->opcode = opcode;
    instruction->id = id;
    instruction->group = group;
    instruction->mnemonic = mnemonics[id - 1];
    instruction->size = size;
}

/*
 * tableswitch and lookupswitch: 0-3 padding bytes so that the default
 * offset starts at a multiple of 4 from the start of the code array, then
 * the s4 fields. The padding is not required to be zero.
 */
static xxbyte_status decode_switch(const java_opcode *op, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    int lookup = op->form == F_LOOKUPSWITCH;
    uint64_t pad = (4u - ((address + 1u) & 3u)) & 3u;
    uint64_t header = 1u + pad + (lookup ? 8u : 12u);
    uint64_t count;
    uint64_t total;
    const uint8_t *p;
    int64_t low = 0;
    int64_t high = 0;

    if (header > code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    p = code + 1 + pad;
    if (lookup) {
        int64_t npairs = s32be(p + 4);
        if (npairs < 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        count = (uint64_t)npairs;
        total = header + count * 8u;
    } else {
        low = s32be(p + 4);
        high = s32be(p + 8);
        if (high < low) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        count = (uint64_t)(high - low) + 1u;
        total = header + count * 4u;
    }
    if (total > code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (total > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    set_mnemonic(instruction, code[0], op->id, op->group, (uint32_t)total);
    instruction->table_count = (uint32_t)count;
    instruction->table_offset = (uint32_t)header;
    add_value(instruction, XXBYTE_OPERAND_BRANCH, 4, branch_target(address, s32be(p)), 0);
    if (lookup) {
        add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, (int64_t)count, 0);
    } else {
        add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, low, XXBYTE_OPERAND_FLAG_SIGNED);
        add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, high, XXBYTE_OPERAND_FLAG_SIGNED);
    }
    return XXBYTE_STATUS_OK;
}

/* wide followed by the opcode it modifies, as one instruction. */
static xxbyte_status decode_wide(const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    const java_opcode *op;
    size_t i;
    uint32_t size;

    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    for (i = 0; i < sizeof(wide_forms) / sizeof(wide_forms[0]); ++i) {
        if (wide_forms[i].opcode == code[1]) {
            break;
        }
    }
    if (i == sizeof(wide_forms) / sizeof(wide_forms[0])) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    op = &opcodes[code[1]];
    size = op->form == F_IINC ? 6u : 4u;
    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    set_mnemonic(instruction, (WIDE_PREFIX << 8) | code[1], wide_forms[i].id, op->group, size);
    add_slot(instruction, XXBYTE_INDEX_LOCAL, 2, xxbyte_u16be(code + 2), op->form == F_LOCAL2 ? XXBYTE_OPERAND_FLAG_WIDE : 0);
    if (op->form == F_IINC) {
        add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, xxbyte_sext(xxbyte_u16be(code + 4), 16), XXBYTE_OPERAND_FLAG_SIGNED);
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status java_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const java_opcode *op = &opcodes[code[0]];
    const uint8_t *p = code + 1;
    const uint16_t implicit = XXBYTE_OPERAND_FLAG_IMPLICIT;
    const uint16_t is_signed = XXBYTE_OPERAND_FLAG_SIGNED;
    const uint16_t wide = XXBYTE_OPERAND_FLAG_WIDE;

    (void)context;
    (void)mode;
    if (code[0] == WIDE_PREFIX) {
        return decode_wide(code, code_size, instruction);
    }
    if (op->id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (op->form == F_TABLESWITCH || op->form == F_LOOKUPSWITCH) {
        return decode_switch(op, code, code_size, address, instruction);
    }
    if (code_size < form_sizes[op->form]) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    set_mnemonic(instruction, code[0], op->id, op->group, form_sizes[op->form]);

    switch (op->form) {
        case F_ICONST: add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, op->value, implicit | is_signed); break;
        case F_LCONST: add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, op->value, implicit | is_signed | wide); break;
        /* A FLOAT operand's size is the width of its value even when the
         * constant is implicit: consumers tell float from double by it. */
        case F_FCONST: add_value(instruction, XXBYTE_OPERAND_FLOAT, 4, (int64_t)fconst_bits[op->value], implicit); break;
        case F_DCONST: add_value(instruction, XXBYTE_OPERAND_FLOAT, 8, (int64_t)dconst_bits[op->value], implicit | wide); break;
        case F_BIPUSH: add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, xxbyte_sext(p[0], 8), is_signed); break;
        case F_SIPUSH: add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, xxbyte_sext(xxbyte_u16be(p), 16), is_signed); break;
        case F_LDC: add_slot(instruction, XXBYTE_INDEX_CONSTANT, 1, p[0], 0); break;
        case F_CPOOL: add_slot(instruction, XXBYTE_INDEX_CONSTANT, 2, xxbyte_u16be(p), 0); break;
        case F_LOCAL: add_slot(instruction, XXBYTE_INDEX_LOCAL, 1, p[0], 0); break;
        case F_LOCAL2: add_slot(instruction, XXBYTE_INDEX_LOCAL, 1, p[0], wide); break;
        case F_LOCAL_N: add_slot(instruction, XXBYTE_INDEX_LOCAL, 0, op->value, implicit); break;
        case F_LOCAL2_N: add_slot(instruction, XXBYTE_INDEX_LOCAL, 0, op->value, implicit | wide); break;
        case F_IINC:
            add_slot(instruction, XXBYTE_INDEX_LOCAL, 1, p[0], 0);
            add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, xxbyte_sext(p[1], 8), is_signed);
            break;
        case F_BRANCH: add_value(instruction, XXBYTE_OPERAND_BRANCH, 2, branch_target(address, xxbyte_sext(xxbyte_u16be(p), 16)), 0); break;
        case F_BRANCH_W: add_value(instruction, XXBYTE_OPERAND_BRANCH, 4, branch_target(address, s32be(p)), 0); break;
        case F_INVOKEINTERFACE:
            add_slot(instruction, XXBYTE_INDEX_CONSTANT, 2, xxbyte_u16be(p), 0);
            add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, p[2], 0);
            break;
        case F_INVOKEDYNAMIC: add_slot(instruction, XXBYTE_INDEX_CONSTANT, 2, xxbyte_u16be(p), 0); break;
        case F_NEWARRAY: add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, p[0], 0); break;
        case F_MULTIANEWARRAY:
            add_slot(instruction, XXBYTE_INDEX_CONSTANT, 2, xxbyte_u16be(p), 0);
            add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, p[2], 0);
            break;
        default: break;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status java_switch_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target)
{
    const uint8_t *table;
    uint64_t entry_size;
    uint64_t header;

    if (instruction->opcode == OP_TABLESWITCH) {
        entry_size = 4;
        header = 13;
    } else if (instruction->opcode == OP_LOOKUPSWITCH) {
        entry_size = 8;
        header = 9;
    } else {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->table_offset < header || instruction->table_offset + ((uint64_t)index + 1u) * entry_size > instruction->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    table = instruction->code + instruction->table_offset;
    if (entry_size == 8) {
        *key = s32be(table + (size_t)index * 8u);
        *target = branch_target(instruction->address, s32be(table + (size_t)index * 8u + 4u));
    } else {
        *key = (int64_t)s32be(table - 8) + (int64_t)index;
        *target = branch_target(instruction->address, s32be(table + (size_t)index * 4u));
    }
    return XXBYTE_STATUS_OK;
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
        case XXBYTE_OPERAND_INDEX: return xxbyte_fmt_printf(fmt, operand->kind == XXBYTE_INDEX_CONSTANT ? "#%llu" : "%llu", (unsigned long long)operand->value);
        case XXBYTE_OPERAND_IMMEDIATE:
            if (instruction->opcode == OP_NEWARRAY) {
                const char *name = xxbyte_java_array_type_name((uint32_t)operand->value);
                if (name != NULL) {
                    return xxbyte_fmt_str(fmt, name);
                }
            }
            if ((operand->flags & XXBYTE_OPERAND_FLAG_SIGNED) == 0) {
                return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
            }
            return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
        default: return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
    }
}

/* javap style: "tableswitch {0: 28, 1: 33, default: 40}". */
static int format_switch(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    xxbyte_operand entry;
    uint32_t i;

    if (!xxbyte_fmt_str(fmt, " {")) {
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    entry.type = XXBYTE_OPERAND_BRANCH;
    entry.size = 4;
    for (i = 0; i < instruction->table_count; ++i) {
        int64_t key;
        int resolved;
        if (java_switch_entry(instruction, i, &key, &entry.value) != XXBYTE_STATUS_OK || !xxbyte_fmt_printf(fmt, "%lld: ", (long long)key)) {
            return 0;
        }
        resolved = xxbyte_fmt_resolve_entry(fmt, instruction, &entry);
        if (resolved < 0 || (resolved == 0 && !xxbyte_fmt_printf(fmt, "%lld", (long long)entry.value)) || !xxbyte_fmt_str(fmt, ", ")) {
            return 0;
        }
    }
    return instruction->operand_count != 0 && xxbyte_fmt_str(fmt, "default: ") && format_operand(instruction, 0, fmt) && xxbyte_fmt_char(fmt, '}');
}

static int java_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;
    int first = 1;

    if (instruction->opcode == OP_TABLESWITCH || instruction->opcode == OP_LOOKUPSWITCH) {
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
    /* javap prints the two reserved bytes of invokedynamic as one 0. */
    if (instruction->opcode == OP_INVOKEDYNAMIC) {
        return xxbyte_fmt_str(fmt, ", 0");
    }
    return 1;
}

const xxbyte_family_ops xxbyte_java_ops = {
    XXBYTE_FAMILY_JAVA, XXBYTE_JAVA_MODE_DEFAULT, java_check_mode, java_decode, java_switch_entry, java_format_operands, NULL, xxbyte_java_list_file,
};

const char *xxbyte_java_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_JAVA_ID_COUNT) {
        return NULL;
    }
    return mnemonics[id - 1];
}

const char *xxbyte_java_array_type_name(uint32_t atype)
{
    if (atype < XXBYTE_JAVA_T_BOOLEAN || atype > XXBYTE_JAVA_T_LONG) {
        return NULL;
    }
    return array_type_names[atype - XXBYTE_JAVA_T_BOOLEAN];
}

xxbyte_status xxbyte_java_parse_code_attribute(const uint8_t *data, size_t size, xxbyte_java_code_attribute *attribute)
{
    xxbyte_java_code_attribute parsed;
    uint64_t offset;
    uint32_t i;

    if (attribute == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(attribute, 0, sizeof(*attribute));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(&parsed, 0, sizeof(parsed));
    if (size < 8) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    parsed.max_stack = xxbyte_u16be(data);
    parsed.max_locals = xxbyte_u16be(data + 2);
    parsed.code_length = xxbyte_u32be(data + 4);
    parsed.code_offset = 8;
    if (parsed.code_length == 0 || parsed.code_length > 65535u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    offset = 8u + (uint64_t)parsed.code_length;
    if (offset + 2u > size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    parsed.exception_table_length = xxbyte_u16be(data + offset);
    parsed.exception_table_offset = (uint32_t)(offset + 2u);
    offset += 2u + (uint64_t)parsed.exception_table_length * 8u;
    if (offset + 2u > size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    parsed.attributes_count = xxbyte_u16be(data + offset);
    parsed.attributes_offset = (uint32_t)(offset + 2u);
    offset += 2u;
    for (i = 0; i < parsed.attributes_count; ++i) {
        if (offset + 6u > size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        offset += 6u + (uint64_t)xxbyte_u32be(data + offset + 2);
        if (offset > size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
    }
    *attribute = parsed;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_java_parse_exception_table(const uint8_t *data, size_t size, const xxbyte_java_code_attribute *attribute, xxbyte_java_exception_handler *handlers,
                                                size_t capacity, size_t *count)
{
    uint32_t i;

    if (count != NULL) {
        *count = 0;
    }
    if (data == NULL || attribute == NULL || count == NULL || (handlers == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if ((uint64_t)attribute->exception_table_offset + (uint64_t)attribute->exception_table_length * 8u > size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    for (i = 0; i < attribute->exception_table_length && i < capacity; ++i) {
        const uint8_t *p = data + attribute->exception_table_offset + i * 8u;
        handlers[i].start_pc = xxbyte_u16be(p);
        handlers[i].end_pc = xxbyte_u16be(p + 2);
        handlers[i].handler_pc = xxbyte_u16be(p + 4);
        handlers[i].catch_type = xxbyte_u16be(p + 6);
    }
    *count = attribute->exception_table_length;
    return XXBYTE_STATUS_OK;
}

#else
typedef int xxbyte_java_disabled;
#endif
