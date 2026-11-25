#include "xxbyte_internal.h"
#include "xxbyte_dex_file.h"

#if XXBYTE_USE_DEX

/*
 * Dalvik bytecode. Code units are 16-bit little-endian; every format is
 * named after the AOSP instruction-formats document. The opcode table was
 * checked against dexlib2's Opcode list (smali 2.5.2) and AOSP
 * dalvik/libdex/DexOpcodes.h for the odex opcodes.
 */

#define DEX_ODEX_FIRST 0xE3u
/* invoke-polymorphic and later opcodes reuse odex opcodes from here on. */
#define DEX_ODEX_SHARED_FIRST 0xFAu
#define DEX_ODEX_SHARED_VERSION 38u

enum dex_format {
    DEX_FMT_10X,
    DEX_FMT_12X,
    DEX_FMT_11N,
    DEX_FMT_11X,
    DEX_FMT_10T,
    DEX_FMT_20T,
    DEX_FMT_20BC,
    DEX_FMT_22X,
    DEX_FMT_21T,
    DEX_FMT_21S,
    DEX_FMT_21H,
    DEX_FMT_21C,
    DEX_FMT_23X,
    DEX_FMT_22B,
    DEX_FMT_22T,
    DEX_FMT_22S,
    DEX_FMT_22C,
    DEX_FMT_22CS,
    DEX_FMT_30T,
    DEX_FMT_32X,
    DEX_FMT_31I,
    DEX_FMT_31T,
    DEX_FMT_31C,
    DEX_FMT_35C,
    DEX_FMT_35MS,
    DEX_FMT_35MI,
    DEX_FMT_3RC,
    DEX_FMT_3RMS,
    DEX_FMT_3RMI,
    DEX_FMT_45CC,
    DEX_FMT_4RCC,
    DEX_FMT_51L,
    DEX_FMT_PAYLOAD
};

/* Code units of each format, in enum dex_format order. */
static const uint8_t dex_format_units[] = {
    1, 1, 1, 1, 1,       /* 10x 12x 11n 11x 10t */
    2, 2, 2, 2, 2, 2,    /* 20t 20bc 22x 21t 21s 21h */
    2, 2, 2, 2, 2, 2, 2, /* 21c 23x 22b 22t 22s 22c 22cs */
    3, 3, 3, 3, 3,       /* 30t 32x 31i 31t 31c */
    3, 3, 3, 3, 3, 3,    /* 35c 35ms 35mi 3rc 3rms 3rmi */
    4, 4, 5, 1,          /* 45cc 4rcc 51l, payload header */
};

#define DEX_REF_NONE XXBYTE_INDEX_NONE
#define DEX_REF_STRING XXBYTE_INDEX_STRING
#define DEX_REF_TYPE XXBYTE_INDEX_TYPE
#define DEX_REF_FIELD XXBYTE_INDEX_FIELD
#define DEX_REF_METHOD XXBYTE_INDEX_METHOD
#define DEX_REF_PROTO XXBYTE_INDEX_PROTO
#define DEX_REF_CALL_SITE XXBYTE_INDEX_CALL_SITE
#define DEX_REF_METHOD_HANDLE XXBYTE_INDEX_METHOD_HANDLE
#define DEX_REF_INLINE XXBYTE_INDEX_INLINE
#define DEX_REF_VTABLE_SLOT XXBYTE_INDEX_VTABLE_SLOT
#define DEX_REF_FIELD_OFFSET XXBYTE_INDEX_FIELD_OFFSET

/* Register pairs and 64-bit literals (XXBYTE_OPERAND_FLAG_WIDE). */
#define DEX_W_A 0x01u /* the first register operand */
#define DEX_W_B 0x02u /* the second register operand */
#define DEX_W_C 0x04u /* the third register operand */
#define DEX_W_L 0x08u /* the literal */
#define DEX_W_AB (DEX_W_A | DEX_W_B)
#define DEX_W_BC (DEX_W_B | DEX_W_C)
#define DEX_W_ABC (DEX_W_A | DEX_W_B | DEX_W_C)
#define DEX_W_AL (DEX_W_A | DEX_W_L)

#define DEX_G_GOTO (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define DEX_G_IF XXBYTE_GROUP_CONDITIONAL
#define DEX_G_SWITCH (XXBYTE_GROUP_SWITCH | XXBYTE_GROUP_CONDITIONAL)
#define DEX_G_CALL XXBYTE_GROUP_CALL
#define DEX_G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define DEX_G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)
#define DEX_G_TABLE (XXBYTE_GROUP_PAYLOAD | XXBYTE_GROUP_SWITCH)
#define DEX_G_DATA XXBYTE_GROUP_PAYLOAD

typedef struct dex_op {
    const char *mnemonic;
    uint8_t format;  /* enum dex_format */
    uint8_t kind;    /* xxbyte_index_kind of the reference */
    uint8_t wide;    /* DEX_W_* */
    uint8_t version; /* first DEX version with the opcode, 0 for all */
    uint16_t group;
} dex_op;

/* Indexed by xxbyte_dex_id - 1. */
static const dex_op dex_ops[] = {
    /* Standard opcodes 0x00..0xff. */
    {"nop", DEX_FMT_10X, DEX_REF_NONE, 0, 0, 0},
    {"move", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"move/from16", DEX_FMT_22X, DEX_REF_NONE, 0, 0, 0},
    {"move/16", DEX_FMT_32X, DEX_REF_NONE, 0, 0, 0},
    {"move-wide", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"move-wide/from16", DEX_FMT_22X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"move-wide/16", DEX_FMT_32X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"move-object", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"move-object/from16", DEX_FMT_22X, DEX_REF_NONE, 0, 0, 0},
    {"move-object/16", DEX_FMT_32X, DEX_REF_NONE, 0, 0, 0},
    {"move-result", DEX_FMT_11X, DEX_REF_NONE, 0, 0, 0},
    {"move-result-wide", DEX_FMT_11X, DEX_REF_NONE, DEX_W_A, 0, 0},
    {"move-result-object", DEX_FMT_11X, DEX_REF_NONE, 0, 0, 0},
    {"move-exception", DEX_FMT_11X, DEX_REF_NONE, 0, 0, 0},
    {"return-void", DEX_FMT_10X, DEX_REF_NONE, 0, 0, DEX_G_RETURN},
    {"return", DEX_FMT_11X, DEX_REF_NONE, 0, 0, DEX_G_RETURN},
    {"return-wide", DEX_FMT_11X, DEX_REF_NONE, DEX_W_A, 0, DEX_G_RETURN},
    {"return-object", DEX_FMT_11X, DEX_REF_NONE, 0, 0, DEX_G_RETURN},
    {"const/4", DEX_FMT_11N, DEX_REF_NONE, 0, 0, 0},
    {"const/16", DEX_FMT_21S, DEX_REF_NONE, 0, 0, 0},
    {"const", DEX_FMT_31I, DEX_REF_NONE, 0, 0, 0},
    {"const/high16", DEX_FMT_21H, DEX_REF_NONE, 0, 0, 0},
    {"const-wide/16", DEX_FMT_21S, DEX_REF_NONE, DEX_W_AL, 0, 0},
    {"const-wide/32", DEX_FMT_31I, DEX_REF_NONE, DEX_W_AL, 0, 0},
    {"const-wide", DEX_FMT_51L, DEX_REF_NONE, DEX_W_AL, 0, 0},
    {"const-wide/high16", DEX_FMT_21H, DEX_REF_NONE, DEX_W_AL, 0, 0},
    {"const-string", DEX_FMT_21C, DEX_REF_STRING, 0, 0, 0},
    {"const-string/jumbo", DEX_FMT_31C, DEX_REF_STRING, 0, 0, 0},
    {"const-class", DEX_FMT_21C, DEX_REF_TYPE, 0, 0, 0},
    {"monitor-enter", DEX_FMT_11X, DEX_REF_NONE, 0, 0, 0},
    {"monitor-exit", DEX_FMT_11X, DEX_REF_NONE, 0, 0, 0},
    {"check-cast", DEX_FMT_21C, DEX_REF_TYPE, 0, 0, 0},
    {"instance-of", DEX_FMT_22C, DEX_REF_TYPE, 0, 0, 0},
    {"array-length", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"new-instance", DEX_FMT_21C, DEX_REF_TYPE, 0, 0, 0},
    {"new-array", DEX_FMT_22C, DEX_REF_TYPE, 0, 0, 0},
    {"filled-new-array", DEX_FMT_35C, DEX_REF_TYPE, 0, 0, 0},
    {"filled-new-array/range", DEX_FMT_3RC, DEX_REF_TYPE, 0, 0, 0},
    {"fill-array-data", DEX_FMT_31T, DEX_REF_NONE, 0, 0, 0},
    {"throw", DEX_FMT_11X, DEX_REF_NONE, 0, 0, DEX_G_THROW},
    {"goto", DEX_FMT_10T, DEX_REF_NONE, 0, 0, DEX_G_GOTO},
    {"goto/16", DEX_FMT_20T, DEX_REF_NONE, 0, 0, DEX_G_GOTO},
    {"goto/32", DEX_FMT_30T, DEX_REF_NONE, 0, 0, DEX_G_GOTO},
    {"packed-switch", DEX_FMT_31T, DEX_REF_NONE, 0, 0, DEX_G_SWITCH},
    {"sparse-switch", DEX_FMT_31T, DEX_REF_NONE, 0, 0, DEX_G_SWITCH},
    {"cmpl-float", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"cmpg-float", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"cmpl-double", DEX_FMT_23X, DEX_REF_NONE, DEX_W_BC, 0, 0},
    {"cmpg-double", DEX_FMT_23X, DEX_REF_NONE, DEX_W_BC, 0, 0},
    {"cmp-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_BC, 0, 0},
    {"if-eq", DEX_FMT_22T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-ne", DEX_FMT_22T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-lt", DEX_FMT_22T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-ge", DEX_FMT_22T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-gt", DEX_FMT_22T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-le", DEX_FMT_22T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-eqz", DEX_FMT_21T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-nez", DEX_FMT_21T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-ltz", DEX_FMT_21T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-gez", DEX_FMT_21T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-gtz", DEX_FMT_21T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"if-lez", DEX_FMT_21T, DEX_REF_NONE, 0, 0, DEX_G_IF},
    {"aget", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aget-wide", DEX_FMT_23X, DEX_REF_NONE, DEX_W_A, 0, 0},
    {"aget-object", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aget-boolean", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aget-byte", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aget-char", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aget-short", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aput", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aput-wide", DEX_FMT_23X, DEX_REF_NONE, DEX_W_A, 0, 0},
    {"aput-object", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aput-boolean", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aput-byte", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aput-char", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"aput-short", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"iget", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iget-wide", DEX_FMT_22C, DEX_REF_FIELD, DEX_W_A, 0, 0},
    {"iget-object", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iget-boolean", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iget-byte", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iget-char", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iget-short", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iput", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iput-wide", DEX_FMT_22C, DEX_REF_FIELD, DEX_W_A, 0, 0},
    {"iput-object", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iput-boolean", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iput-byte", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iput-char", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iput-short", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"sget", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sget-wide", DEX_FMT_21C, DEX_REF_FIELD, DEX_W_A, 0, 0},
    {"sget-object", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sget-boolean", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sget-byte", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sget-char", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sget-short", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sput", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sput-wide", DEX_FMT_21C, DEX_REF_FIELD, DEX_W_A, 0, 0},
    {"sput-object", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sput-boolean", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sput-byte", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sput-char", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sput-short", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"invoke-virtual", DEX_FMT_35C, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"invoke-super", DEX_FMT_35C, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"invoke-direct", DEX_FMT_35C, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"invoke-static", DEX_FMT_35C, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"invoke-interface", DEX_FMT_35C, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"invoke-virtual/range", DEX_FMT_3RC, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"invoke-super/range", DEX_FMT_3RC, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"invoke-direct/range", DEX_FMT_3RC, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"invoke-static/range", DEX_FMT_3RC, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"invoke-interface/range", DEX_FMT_3RC, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"neg-int", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"not-int", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"neg-long", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"not-long", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"neg-float", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"neg-double", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"int-to-long", DEX_FMT_12X, DEX_REF_NONE, DEX_W_A, 0, 0},
    {"int-to-float", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"int-to-double", DEX_FMT_12X, DEX_REF_NONE, DEX_W_A, 0, 0},
    {"long-to-int", DEX_FMT_12X, DEX_REF_NONE, DEX_W_B, 0, 0},
    {"long-to-float", DEX_FMT_12X, DEX_REF_NONE, DEX_W_B, 0, 0},
    {"long-to-double", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"float-to-int", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"float-to-long", DEX_FMT_12X, DEX_REF_NONE, DEX_W_A, 0, 0},
    {"float-to-double", DEX_FMT_12X, DEX_REF_NONE, DEX_W_A, 0, 0},
    {"double-to-int", DEX_FMT_12X, DEX_REF_NONE, DEX_W_B, 0, 0},
    {"double-to-long", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"double-to-float", DEX_FMT_12X, DEX_REF_NONE, DEX_W_B, 0, 0},
    {"int-to-byte", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"int-to-char", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"int-to-short", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"add-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"sub-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"mul-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"div-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"rem-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"and-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"or-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"xor-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"shl-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"shr-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"ushr-int", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"add-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"sub-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"mul-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"div-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"rem-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"and-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"or-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"xor-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"shl-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"shr-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"ushr-long", DEX_FMT_23X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"add-float", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"sub-float", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"mul-float", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"div-float", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"rem-float", DEX_FMT_23X, DEX_REF_NONE, 0, 0, 0},
    {"add-double", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"sub-double", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"mul-double", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"div-double", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"rem-double", DEX_FMT_23X, DEX_REF_NONE, DEX_W_ABC, 0, 0},
    {"add-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"sub-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"mul-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"div-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"rem-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"and-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"or-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"xor-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"shl-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"shr-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"ushr-int/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"add-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"sub-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"mul-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"div-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"rem-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"and-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"or-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"xor-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"shl-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_A, 0, 0},
    {"shr-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_A, 0, 0},
    {"ushr-long/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_A, 0, 0},
    {"add-float/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"sub-float/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"mul-float/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"div-float/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"rem-float/2addr", DEX_FMT_12X, DEX_REF_NONE, 0, 0, 0},
    {"add-double/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"sub-double/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"mul-double/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"div-double/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"rem-double/2addr", DEX_FMT_12X, DEX_REF_NONE, DEX_W_AB, 0, 0},
    {"add-int/lit16", DEX_FMT_22S, DEX_REF_NONE, 0, 0, 0},
    {"rsub-int", DEX_FMT_22S, DEX_REF_NONE, 0, 0, 0},
    {"mul-int/lit16", DEX_FMT_22S, DEX_REF_NONE, 0, 0, 0},
    {"div-int/lit16", DEX_FMT_22S, DEX_REF_NONE, 0, 0, 0},
    {"rem-int/lit16", DEX_FMT_22S, DEX_REF_NONE, 0, 0, 0},
    {"and-int/lit16", DEX_FMT_22S, DEX_REF_NONE, 0, 0, 0},
    {"or-int/lit16", DEX_FMT_22S, DEX_REF_NONE, 0, 0, 0},
    {"xor-int/lit16", DEX_FMT_22S, DEX_REF_NONE, 0, 0, 0},
    {"add-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"rsub-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"mul-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"div-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"rem-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"and-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"or-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"xor-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"shl-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"shr-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"ushr-int/lit8", DEX_FMT_22B, DEX_REF_NONE, 0, 0, 0},
    {"invoke-polymorphic", DEX_FMT_45CC, DEX_REF_METHOD, 0, 38, DEX_G_CALL},
    {"invoke-polymorphic/range", DEX_FMT_4RCC, DEX_REF_METHOD, 0, 38, DEX_G_CALL},
    {"invoke-custom", DEX_FMT_35C, DEX_REF_CALL_SITE, 0, 38, DEX_G_CALL},
    {"invoke-custom/range", DEX_FMT_3RC, DEX_REF_CALL_SITE, 0, 38, DEX_G_CALL},
    {"const-method-handle", DEX_FMT_21C, DEX_REF_METHOD_HANDLE, 0, 39, 0},
    {"const-method-type", DEX_FMT_21C, DEX_REF_PROTO, 0, 39, 0},
    /* Payload pseudo-instructions 0x0100, 0x0200, 0x0300. */
    {"packed-switch-payload", DEX_FMT_PAYLOAD, DEX_REF_NONE, 0, 0, DEX_G_TABLE},
    {"sparse-switch-payload", DEX_FMT_PAYLOAD, DEX_REF_NONE, 0, 0, DEX_G_TABLE},
    {"fill-array-data-payload", DEX_FMT_PAYLOAD, DEX_REF_NONE, 0, 0, DEX_G_DATA},
    /* Dalvik VM odex opcodes 0xe3..0xfe (XXBYTE_DEX_MODE_ODEX). */
    {"iget-volatile", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iput-volatile", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"sget-volatile", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sput-volatile", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"iget-object-volatile", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"iget-wide-volatile", DEX_FMT_22C, DEX_REF_FIELD, DEX_W_A, 0, 0},
    {"iput-wide-volatile", DEX_FMT_22C, DEX_REF_FIELD, DEX_W_A, 0, 0},
    {"sget-wide-volatile", DEX_FMT_21C, DEX_REF_FIELD, DEX_W_A, 0, 0},
    {"sput-wide-volatile", DEX_FMT_21C, DEX_REF_FIELD, DEX_W_A, 0, 0},
    {"throw-verification-error", DEX_FMT_20BC, DEX_REF_NONE, 0, 0, DEX_G_THROW},
    {"execute-inline", DEX_FMT_35MI, DEX_REF_INLINE, 0, 0, DEX_G_CALL},
    {"execute-inline/range", DEX_FMT_3RMI, DEX_REF_INLINE, 0, 0, DEX_G_CALL},
    {"invoke-object-init/range", DEX_FMT_3RC, DEX_REF_METHOD, 0, 0, DEX_G_CALL},
    {"return-void-barrier", DEX_FMT_10X, DEX_REF_NONE, 0, 0, DEX_G_RETURN},
    {"iget-quick", DEX_FMT_22CS, DEX_REF_FIELD_OFFSET, 0, 0, 0},
    {"iget-wide-quick", DEX_FMT_22CS, DEX_REF_FIELD_OFFSET, DEX_W_A, 0, 0},
    {"iget-object-quick", DEX_FMT_22CS, DEX_REF_FIELD_OFFSET, 0, 0, 0},
    {"iput-quick", DEX_FMT_22CS, DEX_REF_FIELD_OFFSET, 0, 0, 0},
    {"iput-wide-quick", DEX_FMT_22CS, DEX_REF_FIELD_OFFSET, DEX_W_A, 0, 0},
    {"iput-object-quick", DEX_FMT_22CS, DEX_REF_FIELD_OFFSET, 0, 0, 0},
    {"invoke-virtual-quick", DEX_FMT_35MS, DEX_REF_VTABLE_SLOT, 0, 0, DEX_G_CALL},
    {"invoke-virtual-quick/range", DEX_FMT_3RMS, DEX_REF_VTABLE_SLOT, 0, 0, DEX_G_CALL},
    {"invoke-super-quick", DEX_FMT_35MS, DEX_REF_VTABLE_SLOT, 0, 0, DEX_G_CALL},
    {"invoke-super-quick/range", DEX_FMT_3RMS, DEX_REF_VTABLE_SLOT, 0, 0, DEX_G_CALL},
    {"iput-object-volatile", DEX_FMT_22C, DEX_REF_FIELD, 0, 0, 0},
    {"sget-object-volatile", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
    {"sput-object-volatile", DEX_FMT_21C, DEX_REF_FIELD, 0, 0, 0},
};

static const uint8_t dex_standard_ids[256] = {
    /* 00 */ 1,   2,   3,   4,   5,   6,   7,   8,
    /* 08 */ 9,   10,  11,  12,  13,  14,  15,  16,
    /* 10 */ 17,  18,  19,  20,  21,  22,  23,  24,
    /* 18 */ 25,  26,  27,  28,  29,  30,  31,  32,
    /* 20 */ 33,  34,  35,  36,  37,  38,  39,  40,
    /* 28 */ 41,  42,  43,  44,  45,  46,  47,  48,
    /* 30 */ 49,  50,  51,  52,  53,  54,  55,  56,
    /* 38 */ 57,  58,  59,  60,  61,  62,  0,   0,
    /* 40 */ 0,   0,   0,   0,   63,  64,  65,  66,
    /* 48 */ 67,  68,  69,  70,  71,  72,  73,  74,
    /* 50 */ 75,  76,  77,  78,  79,  80,  81,  82,
    /* 58 */ 83,  84,  85,  86,  87,  88,  89,  90,
    /* 60 */ 91,  92,  93,  94,  95,  96,  97,  98,
    /* 68 */ 99,  100, 101, 102, 103, 104, 105, 106,
    /* 70 */ 107, 108, 109, 0,   110, 111, 112, 113,
    /* 78 */ 114, 0,   0,   115, 116, 117, 118, 119,
    /* 80 */ 120, 121, 122, 123, 124, 125, 126, 127,
    /* 88 */ 128, 129, 130, 131, 132, 133, 134, 135,
    /* 90 */ 136, 137, 138, 139, 140, 141, 142, 143,
    /* 98 */ 144, 145, 146, 147, 148, 149, 150, 151,
    /* a0 */ 152, 153, 154, 155, 156, 157, 158, 159,
    /* a8 */ 160, 161, 162, 163, 164, 165, 166, 167,
    /* b0 */ 168, 169, 170, 171, 172, 173, 174, 175,
    /* b8 */ 176, 177, 178, 179, 180, 181, 182, 183,
    /* c0 */ 184, 185, 186, 187, 188, 189, 190, 191,
    /* c8 */ 192, 193, 194, 195, 196, 197, 198, 199,
    /* d0 */ 200, 201, 202, 203, 204, 205, 206, 207,
    /* d8 */ 208, 209, 210, 211, 212, 213, 214, 215,
    /* e0 */ 216, 217, 218, 0,   0,   0,   0,   0,
    /* e8 */ 0,   0,   0,   0,   0,   0,   0,   0,
    /* f0 */ 0,   0,   0,   0,   0,   0,   0,   0,
    /* f8 */ 0,   0,   219, 220, 221, 222, 223, 224,
};

/* Indexed by opcode - DEX_ODEX_FIRST. */
static const uint8_t dex_odex_ids[256 - DEX_ODEX_FIRST] = {
    /* e3 */ 228, 229, 230, 231, 232, 233, 234, 235,
    /* eb */ 236, 0,   237, 238, 239, 240, 241, 242,
    /* f3 */ 243, 244, 245, 246, 247, 248, 249, 250,
    /* fb */ 251, 252, 253, 254, 0,
};

typedef char dex_ops_match_ids[sizeof(dex_ops) / sizeof(dex_ops[0]) == XXBYTE_DEX_ID_COUNT - 1 ? 1 : -1];
typedef char dex_format_units_match[sizeof(dex_format_units) == DEX_FMT_PAYLOAD + 1 ? 1 : -1];

static const char *const dex_verify_errors[] = {
    NULL,
    "generic-error",
    "no-such-class",
    "no-such-field",
    "no-such-method",
    "illegal-class-access",
    "illegal-field-access",
    "illegal-method-access",
    "class-change-error",
    "instantiation-error",
};

#define DEX_VERIFY_ERROR_COUNT (sizeof(dex_verify_errors) / sizeof(dex_verify_errors[0]))

/* throw-verification-error: the reference kind in the top two bits. */
static const uint8_t dex_verify_kinds[3] = {
    XXBYTE_INDEX_TYPE,
    XXBYTE_INDEX_FIELD,
    XXBYTE_INDEX_METHOD,
};

static uint32_t dex_version(uint32_t mode)
{
    uint32_t version = mode & XXBYTE_DEX_MODE_VERSION_MASK;
    return version == 0 ? XXBYTE_DEX_VERSION_NEWEST : version;
}

static xxbyte_status dex_check_mode(uint32_t mode)
{
    if ((mode & ~(XXBYTE_DEX_MODE_VERSION_MASK | XXBYTE_DEX_MODE_ODEX)) != 0) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    switch (mode & XXBYTE_DEX_MODE_VERSION_MASK) {
        case 0:
        case 35:
        case 37:
        case 38:
        case 39:
        case 40:
        case 41: return XXBYTE_STATUS_OK;
        default: return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
}

/* The id of opcode in mode, or 0 when it is undefined there. */
static uint32_t dex_lookup(uint32_t mode, uint32_t opcode)
{
    uint32_t version = dex_version(mode);
    uint32_t id;

    if ((mode & XXBYTE_DEX_MODE_ODEX) != 0 && opcode >= DEX_ODEX_FIRST) {
        id = dex_odex_ids[opcode - DEX_ODEX_FIRST];
        if (id != 0 && (opcode < DEX_ODEX_SHARED_FIRST || version < DEX_ODEX_SHARED_VERSION)) {
            return id;
        }
    }
    id = dex_standard_ids[opcode];
    if (id != 0 && version < dex_ops[id - 1].version) {
        return 0;
    }
    return id;
}

static void dex_add_register(xxbyte_instruction *instruction, const dex_op *op, uint32_t which, uint32_t size, uint32_t reg)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_REGISTER, size, (int64_t)reg);
    if (operand != NULL && (op->wide & which) != 0) {
        operand->flags = XXBYTE_OPERAND_FLAG_WIDE;
    }
}

static void dex_add_list_register(xxbyte_instruction *instruction, uint32_t reg)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_REGISTER, 1, (int64_t)reg);
    if (operand != NULL) {
        operand->flags = XXBYTE_OPERAND_FLAG_LIST;
    }
}

static void dex_add_literal(xxbyte_instruction *instruction, const dex_op *op, uint32_t size, int64_t value, uint32_t flags)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, size, value);
    if (operand != NULL) {
        flags |= XXBYTE_OPERAND_FLAG_SIGNED;
        if ((op->wide & DEX_W_L) != 0) {
            flags |= XXBYTE_OPERAND_FLAG_WIDE;
        }
        operand->flags = (uint16_t)flags;
    }
}

/* units is the signed code-unit offset from the instruction. */
static void dex_add_target(xxbyte_instruction *instruction, xxbyte_operand_type type, uint32_t size, uint64_t address, int64_t units)
{
    uint64_t target = address + (uint64_t)units * 2u;
    xxbyte_add_operand(instruction, type, size, (int64_t)target);
}

/* The registers of formats 35c, 35ms, 35mi and 45cc: A|G|op BBBB F|E|D|C. */
static xxbyte_status dex_add_register_list(xxbyte_instruction *instruction, const uint8_t *code)
{
    uint32_t count = (uint32_t)code[1] >> 4;
    uint32_t regs[5];
    uint32_t i;

    if (count > 5) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    regs[0] = code[4] & 0x0Fu;
    regs[1] = (uint32_t)code[4] >> 4;
    regs[2] = code[5] & 0x0Fu;
    regs[3] = (uint32_t)code[5] >> 4;
    regs[4] = code[1] & 0x0Fu;
    for (i = 0; i < count; ++i) {
        dex_add_list_register(instruction, regs[i]);
    }
    return XXBYTE_STATUS_OK;
}

static void dex_add_register_range(xxbyte_instruction *instruction, const uint8_t *code)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_REGISTER_RANGE, 2, (int64_t)xxbyte_u16le(code + 4));
    if (operand != NULL) {
        operand->extra = code[1];
    }
}

static xxbyte_status dex_decode_operands(const dex_op *op, const uint8_t *code, uint64_t address, xxbyte_instruction *instruction)
{
    uint32_t aa = code[1];
    uint32_t a = aa & 0x0Fu;
    uint32_t b = aa >> 4;
    uint32_t kind = op->kind;
    xxbyte_status status;

    switch (op->format) {
        case DEX_FMT_10X: return aa == 0 ? XXBYTE_STATUS_OK : XXBYTE_STATUS_INVALID_INSTRUCTION;
        case DEX_FMT_12X:
            dex_add_register(instruction, op, DEX_W_A, 1, a);
            dex_add_register(instruction, op, DEX_W_B, 1, b);
            return XXBYTE_STATUS_OK;
        case DEX_FMT_11N:
            dex_add_register(instruction, op, DEX_W_A, 1, a);
            dex_add_literal(instruction, op, 1, xxbyte_sext(b, 4), 0);
            return XXBYTE_STATUS_OK;
        case DEX_FMT_11X: dex_add_register(instruction, op, DEX_W_A, 1, aa); return XXBYTE_STATUS_OK;
        case DEX_FMT_10T: dex_add_target(instruction, XXBYTE_OPERAND_BRANCH, 1, address, xxbyte_sext(aa, 8)); return XXBYTE_STATUS_OK;
        case DEX_FMT_20T:
            if (aa != 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            dex_add_target(instruction, XXBYTE_OPERAND_BRANCH, 2, address, xxbyte_sext(xxbyte_u16le(code + 2), 16));
            return XXBYTE_STATUS_OK;
        case DEX_FMT_20BC: {
            uint32_t error = aa & 0x3Fu;
            uint32_t ref = aa >> 6;
            if (error == 0 || error >= DEX_VERIFY_ERROR_COUNT || ref >= sizeof(dex_verify_kinds)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, (int64_t)error);
            xxbyte_add_index(instruction, (xxbyte_index_kind)dex_verify_kinds[ref], 2, (int64_t)xxbyte_u16le(code + 2));
            return XXBYTE_STATUS_OK;
        }
        case DEX_FMT_22X:
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            dex_add_register(instruction, op, DEX_W_B, 2, xxbyte_u16le(code + 2));
            return XXBYTE_STATUS_OK;
        case DEX_FMT_21T:
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            dex_add_target(instruction, XXBYTE_OPERAND_BRANCH, 2, address, xxbyte_sext(xxbyte_u16le(code + 2), 16));
            return XXBYTE_STATUS_OK;
        case DEX_FMT_21S:
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            dex_add_literal(instruction, op, 2, xxbyte_sext(xxbyte_u16le(code + 2), 16), 0);
            return XXBYTE_STATUS_OK;
        case DEX_FMT_21H: {
            uint64_t high = xxbyte_u16le(code + 2);
            int64_t value = (op->wide & DEX_W_L) != 0 ? xxbyte_sext(high << 48, 64) : xxbyte_sext(high << 16, 32);
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            dex_add_literal(instruction, op, 2, value, XXBYTE_OPERAND_FLAG_HIGH16);
            return XXBYTE_STATUS_OK;
        }
        case DEX_FMT_21C:
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            xxbyte_add_index(instruction, (xxbyte_index_kind)kind, 2, (int64_t)xxbyte_u16le(code + 2));
            return XXBYTE_STATUS_OK;
        case DEX_FMT_23X:
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            dex_add_register(instruction, op, DEX_W_B, 1, code[2]);
            dex_add_register(instruction, op, DEX_W_C, 1, code[3]);
            return XXBYTE_STATUS_OK;
        case DEX_FMT_22B:
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            dex_add_register(instruction, op, DEX_W_B, 1, code[2]);
            dex_add_literal(instruction, op, 1, xxbyte_sext(code[3], 8), 0);
            return XXBYTE_STATUS_OK;
        case DEX_FMT_22T:
            dex_add_register(instruction, op, DEX_W_A, 1, a);
            dex_add_register(instruction, op, DEX_W_B, 1, b);
            dex_add_target(instruction, XXBYTE_OPERAND_BRANCH, 2, address, xxbyte_sext(xxbyte_u16le(code + 2), 16));
            return XXBYTE_STATUS_OK;
        case DEX_FMT_22S:
            dex_add_register(instruction, op, DEX_W_A, 1, a);
            dex_add_register(instruction, op, DEX_W_B, 1, b);
            dex_add_literal(instruction, op, 2, xxbyte_sext(xxbyte_u16le(code + 2), 16), 0);
            return XXBYTE_STATUS_OK;
        case DEX_FMT_22C:
        case DEX_FMT_22CS:
            dex_add_register(instruction, op, DEX_W_A, 1, a);
            dex_add_register(instruction, op, DEX_W_B, 1, b);
            xxbyte_add_index(instruction, (xxbyte_index_kind)kind, 2, (int64_t)xxbyte_u16le(code + 2));
            return XXBYTE_STATUS_OK;
        case DEX_FMT_30T:
            if (aa != 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            dex_add_target(instruction, XXBYTE_OPERAND_BRANCH, 4, address, xxbyte_sext(xxbyte_u32le(code + 2), 32));
            return XXBYTE_STATUS_OK;
        case DEX_FMT_32X:
            if (aa != 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            dex_add_register(instruction, op, DEX_W_A, 2, xxbyte_u16le(code + 2));
            dex_add_register(instruction, op, DEX_W_B, 2, xxbyte_u16le(code + 4));
            return XXBYTE_STATUS_OK;
        case DEX_FMT_31I:
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            dex_add_literal(instruction, op, 4, xxbyte_sext(xxbyte_u32le(code + 2), 32), 0);
            return XXBYTE_STATUS_OK;
        case DEX_FMT_31T:
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            dex_add_target(instruction, XXBYTE_OPERAND_DATA, 4, address, xxbyte_sext(xxbyte_u32le(code + 2), 32));
            return XXBYTE_STATUS_OK;
        case DEX_FMT_31C:
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            xxbyte_add_index(instruction, (xxbyte_index_kind)kind, 4, (int64_t)xxbyte_u32le(code + 2));
            return XXBYTE_STATUS_OK;
        case DEX_FMT_35C:
        case DEX_FMT_35MS:
        case DEX_FMT_35MI:
        case DEX_FMT_45CC:
            status = dex_add_register_list(instruction, code);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            xxbyte_add_index(instruction, (xxbyte_index_kind)kind, 2, (int64_t)xxbyte_u16le(code + 2));
            if (op->format == DEX_FMT_45CC) {
                xxbyte_add_index(instruction, XXBYTE_INDEX_PROTO, 2, (int64_t)xxbyte_u16le(code + 6));
            }
            return XXBYTE_STATUS_OK;
        case DEX_FMT_3RC:
        case DEX_FMT_3RMS:
        case DEX_FMT_3RMI:
        case DEX_FMT_4RCC:
            dex_add_register_range(instruction, code);
            xxbyte_add_index(instruction, (xxbyte_index_kind)kind, 2, (int64_t)xxbyte_u16le(code + 2));
            if (op->format == DEX_FMT_4RCC) {
                xxbyte_add_index(instruction, XXBYTE_INDEX_PROTO, 2, (int64_t)xxbyte_u16le(code + 6));
            }
            return XXBYTE_STATUS_OK;
        case DEX_FMT_51L:
            dex_add_register(instruction, op, DEX_W_A, 1, aa);
            dex_add_literal(instruction, op, 8, (int64_t)xxbyte_u64le(code + 2), 0);
            return XXBYTE_STATUS_OK;
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
}

/* Bytes needed by a payload whose header fields are known. */
static uint64_t dex_payload_size(uint32_t ident, uint32_t width, uint32_t count)
{
    uint64_t data;

    switch (ident) {
        case XXBYTE_DEX_PACKED_SWITCH_PAYLOAD: return 8u + 4u * (uint64_t)count;
        case XXBYTE_DEX_SPARSE_SWITCH_PAYLOAD: return 4u + 8u * (uint64_t)count;
        default: data = (uint64_t)width * count; return 8u + data + (data & 1u);
    }
}

static int dex_valid_width(uint32_t width)
{
    return width == 1 || width == 2 || width == 4 || width == 8;
}

/* code[0] is 0x00 and code[1] is nonzero. */
static xxbyte_status dex_decode_payload(const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    uint32_t ident = (uint32_t)code[1] << 8;
    uint32_t width = 0;
    uint32_t count;
    uint32_t id;
    uint64_t size;

    switch (ident) {
        case XXBYTE_DEX_PACKED_SWITCH_PAYLOAD: id = XXBYTE_DEX_ID_PACKED_SWITCH_PAYLOAD; break;
        case XXBYTE_DEX_SPARSE_SWITCH_PAYLOAD: id = XXBYTE_DEX_ID_SPARSE_SWITCH_PAYLOAD; break;
        case XXBYTE_DEX_FILL_ARRAY_DATA_PAYLOAD: id = XXBYTE_DEX_ID_FILL_ARRAY_DATA_PAYLOAD; break;
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    count = xxbyte_u16le(code + 2);
    if (ident == XXBYTE_DEX_FILL_ARRAY_DATA_PAYLOAD) {
        width = count;
        if (!dex_valid_width(width)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (code_size < 8) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        count = xxbyte_u32le(code + 4);
    }
    size = dex_payload_size(ident, width, count);
    if (size > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (size > (uint64_t)code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    instruction->size = (uint32_t)size;
    instruction->opcode = ident;
    instruction->id = id;
    instruction->group = dex_ops[id - 1].group;
    instruction->mnemonic = dex_ops[id - 1].mnemonic;
    instruction->table_count = count;
    if (ident == XXBYTE_DEX_PACKED_SWITCH_PAYLOAD) {
        xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, xxbyte_sext(xxbyte_u32le(code + 4), 32));
        if (operand != NULL) {
            operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
        }
        instruction->table_offset = 8;
    } else if (ident == XXBYTE_DEX_SPARSE_SWITCH_PAYLOAD) {
        instruction->table_offset = 4;
    } else {
        xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, (int64_t)width);
        xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, (int64_t)count);
        instruction->table_offset = 8;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status dex_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    uint32_t id = dex_lookup(mode, code[0]);
    const dex_op *op;
    uint32_t size;

    (void)context;
    if (id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    op = &dex_ops[id - 1];
    size = 2u * dex_format_units[op->format];
    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (code[0] == 0x00 && code[1] != 0) {
        return dex_decode_payload(code, code_size, instruction);
    }
    instruction->size = size;
    instruction->opcode = code[0];
    instruction->id = id;
    instruction->group = op->group;
    instruction->mnemonic = op->mnemonic;
    return dex_decode_operands(op, code, address, instruction);
}

/* The element width of a decoded fill-array-data-payload, from its first
 * operand, or 0 when the operands do not describe one. */
static uint32_t dex_array_width(const xxbyte_instruction *instruction)
{
    const xxbyte_operand *operand = &instruction->operands[0];

    if (instruction->operand_count < 2 || operand->type != XXBYTE_OPERAND_IMMEDIATE || operand->value < 1 || operand->value > 8 ||
        !dex_valid_width((uint32_t)operand->value)) {
        return 0;
    }
    return (uint32_t)operand->value;
}

/* The bytes of a decoded switch or array pseudo-instruction, or NULL when
 * instruction does not describe one whose table_count entries fit in its
 * size. Only the entries are read from them: the header fields are in the
 * operands, so an empty table never touches the borrowed code buffer. */
static const uint8_t *dex_payload(const xxbyte_instruction *instruction, uint32_t *width)
{
    uint32_t ident = instruction->opcode;

    *width = 0;
    if (ident == XXBYTE_DEX_FILL_ARRAY_DATA_PAYLOAD) {
        *width = dex_array_width(instruction);
        if (*width == 0) {
            return NULL;
        }
    } else if (ident != XXBYTE_DEX_PACKED_SWITCH_PAYLOAD && ident != XXBYTE_DEX_SPARSE_SWITCH_PAYLOAD) {
        return NULL;
    }
    if (instruction->code == NULL || dex_payload_size(ident, *width, instruction->table_count) > instruction->size) {
        return NULL;
    }
    return instruction->code;
}

static xxbyte_status dex_switch_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target)
{
    uint32_t width;
    const uint8_t *code = dex_payload(instruction, &width);
    size_t at;

    if (code == NULL || index >= instruction->table_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->opcode == XXBYTE_DEX_PACKED_SWITCH_PAYLOAD) {
        /* Keys wrap like the 32-bit switch value they are compared with. */
        uint32_t first;
        if (instruction->operand_count < 1 || instruction->operands[0].type != XXBYTE_OPERAND_IMMEDIATE) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
        first = (uint32_t)instruction->operands[0].value;
        at = 8u + 4u * (size_t)index;
        *key = xxbyte_sext((uint32_t)(first + index), 32);
    } else if (instruction->opcode == XXBYTE_DEX_SPARSE_SWITCH_PAYLOAD) {
        *key = xxbyte_sext(xxbyte_u32le(code + 4 + 4u * (size_t)index), 32);
        at = 4u + 4u * ((size_t)instruction->table_count + index);
    } else {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *target = 2 * xxbyte_sext(xxbyte_u32le(code + at), 32);
    return XXBYTE_STATUS_OK;
}

static const char *dex_kind_name(uint32_t kind)
{
    switch (kind) {
        case XXBYTE_INDEX_STRING: return "string";
        case XXBYTE_INDEX_TYPE: return "type";
        case XXBYTE_INDEX_FIELD: return "field";
        case XXBYTE_INDEX_METHOD: return "method";
        case XXBYTE_INDEX_PROTO: return "proto";
        case XXBYTE_INDEX_CALL_SITE: return "call_site";
        case XXBYTE_INDEX_METHOD_HANDLE: return "method_handle";
        case XXBYTE_INDEX_INLINE: return "inline";
        case XXBYTE_INDEX_VTABLE_SLOT: return "vtable";
        case XXBYTE_INDEX_FIELD_OFFSET: return "field_offset";
        default: return "index";
    }
}

/* "0x1f", "-0x1f", or with plus "+0x1f". */
static int dex_fmt_hex(xxbyte_fmt *fmt, int64_t value, int plus)
{
    uint64_t magnitude = (uint64_t)value;
    const char *sign = plus ? "+" : "";

    if (value < 0) {
        magnitude = 0u - magnitude;
        sign = "-";
    }
    return xxbyte_fmt_printf(fmt, "%s0x%llx", sign, (unsigned long long)magnitude);
}

static int dex_format_operand(const xxbyte_instruction *instruction, uint32_t index, xxbyte_fmt *fmt)
{
    const xxbyte_operand *operand = &instruction->operands[index];
    unsigned long long value = (unsigned long long)operand->value;
    int resolved;

    switch (operand->type) {
        case XXBYTE_OPERAND_REGISTER: return xxbyte_fmt_printf(fmt, "v%llu", value);
        case XXBYTE_OPERAND_REGISTER_RANGE:
            if (operand->extra == 0) {
                return xxbyte_fmt_str(fmt, "{}");
            }
            return xxbyte_fmt_printf(fmt, "{v%llu .. v%llu}", value, value + (unsigned long long)operand->extra - 1u);
        case XXBYTE_OPERAND_IMMEDIATE:
            if (instruction->id == XXBYTE_DEX_ID_THROW_VERIFICATION_ERROR && index == 0 && value != 0 && value < DEX_VERIFY_ERROR_COUNT) {
                return xxbyte_fmt_str(fmt, dex_verify_errors[value]);
            }
            return dex_fmt_hex(fmt, operand->value, 0);
        case XXBYTE_OPERAND_BRANCH:
        case XXBYTE_OPERAND_DATA:
            resolved = xxbyte_fmt_resolve(fmt, instruction, index);
            if (resolved != 0) {
                return resolved > 0;
            }
            return xxbyte_fmt_printf(fmt, "0x%04llx", value);
        case XXBYTE_OPERAND_INDEX:
            resolved = xxbyte_fmt_resolve(fmt, instruction, index);
            if (resolved != 0) {
                return resolved > 0;
            }
            return xxbyte_fmt_printf(fmt, "%s@%04llx", dex_kind_name(operand->kind), value);
        default: return dex_fmt_hex(fmt, operand->value, 0);
    }
}

static int dex_format_payload(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t width = 0;
    const uint8_t *code = NULL;
    uint32_t count = instruction->table_count;
    uint32_t i;

    /* code is borrowed and only needed for the entries. */
    if (count != 0) {
        code = dex_payload(instruction, &width);
        if (code == NULL) {
            return 0;
        }
    }
    switch (instruction->opcode) {
        case XXBYTE_DEX_PACKED_SWITCH_PAYLOAD:
            if (instruction->operand_count < 1 || instruction->operands[0].type != XXBYTE_OPERAND_IMMEDIATE || !xxbyte_fmt_char(fmt, ' ') ||
                !dex_fmt_hex(fmt, instruction->operands[0].value, 0)) {
                return 0;
            }
            break;
        case XXBYTE_DEX_SPARSE_SWITCH_PAYLOAD: break;
        case XXBYTE_DEX_FILL_ARRAY_DATA_PAYLOAD:
            width = dex_array_width(instruction);
            if (width == 0 || !xxbyte_fmt_printf(fmt, " %u, %u", (unsigned)width, (unsigned)count)) {
                return 0;
            }
            break;
        default: return 0;
    }
    if (!xxbyte_fmt_str(fmt, " {")) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        int ok = i == 0 || xxbyte_fmt_str(fmt, ", ");
        if (ok && instruction->opcode == XXBYTE_DEX_PACKED_SWITCH_PAYLOAD) {
            ok = dex_fmt_hex(fmt, 2 * xxbyte_sext(xxbyte_u32le(code + 8 + 4u * i), 32), 1);
        } else if (ok && instruction->opcode == XXBYTE_DEX_SPARSE_SWITCH_PAYLOAD) {
            const uint8_t *keys = code + 4;
            const uint8_t *targets = keys + 4u * (size_t)count;
            ok = dex_fmt_hex(fmt, xxbyte_sext(xxbyte_u32le(keys + 4u * i), 32), 0) && xxbyte_fmt_str(fmt, ": ") &&
                 dex_fmt_hex(fmt, 2 * xxbyte_sext(xxbyte_u32le(targets + 4u * i), 32), 1);
        } else if (ok) {
            const uint8_t *element = code + 8 + (size_t)width * i;
            uint64_t value = 0;
            uint32_t b;
            for (b = 0; b < width; ++b) {
                value |= (uint64_t)element[b] << (8u * b);
            }
            ok = xxbyte_fmt_printf(fmt, "0x%llx", (unsigned long long)value);
        }
        if (!ok) {
            return 0;
        }
    }
    return xxbyte_fmt_char(fmt, '}');
}

static int dex_is_list_format(uint32_t format)
{
    return format == DEX_FMT_35C || format == DEX_FMT_35MS || format == DEX_FMT_35MI || format == DEX_FMT_45CC;
}

static int dex_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const dex_op *op;
    uint32_t i = 0;
    int first = 1;

    if (instruction->id == 0 || instruction->id >= XXBYTE_DEX_ID_COUNT) {
        return 0;
    }
    op = &dex_ops[instruction->id - 1];
    if (op->format == DEX_FMT_PAYLOAD) {
        return dex_format_payload(instruction, fmt);
    }
    if (dex_is_list_format(op->format)) {
        if (!xxbyte_fmt_str(fmt, " {")) {
            return 0;
        }
        for (; i < instruction->operand_count && (instruction->operands[i].flags & XXBYTE_OPERAND_FLAG_LIST) != 0; ++i) {
            if ((i != 0 && !xxbyte_fmt_str(fmt, ", ")) || !dex_format_operand(instruction, i, fmt)) {
                return 0;
            }
        }
        if (!xxbyte_fmt_char(fmt, '}')) {
            return 0;
        }
        first = 0;
    }
    for (; i < instruction->operand_count; ++i) {
        if (!xxbyte_fmt_str(fmt, first ? " " : ", ") || !dex_format_operand(instruction, i, fmt)) {
            return 0;
        }
        first = 0;
    }
    return 1;
}

xxbyte_status xxbyte_dex_parse_code_item(const uint8_t *data, size_t size, xxbyte_dex_code_item *item)
{
    uint64_t end;
    uint64_t tries = 0;

    if (item == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(item, 0, sizeof(*item));
    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < 16) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    end = 16u + 2u * (uint64_t)xxbyte_u32le(data + 12);
    if (xxbyte_u16le(data + 6) != 0) {
        tries = (end + 3u) & ~(uint64_t)3u;
        end = tries + 8u * (uint64_t)xxbyte_u16le(data + 6);
    }
    if (end > (uint64_t)size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (tries > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    item->registers_size = xxbyte_u16le(data);
    item->ins_size = xxbyte_u16le(data + 2);
    item->outs_size = xxbyte_u16le(data + 4);
    item->tries_size = xxbyte_u16le(data + 6);
    item->debug_info_off = xxbyte_u32le(data + 8);
    item->insns_size = xxbyte_u32le(data + 12);
    item->insns_offset = 16;
    item->tries_offset = (uint32_t)tries;
    return XXBYTE_STATUS_OK;
}

static size_t dex_unit_size(uint32_t mode)
{
    (void)mode;
    return 2;
}

const xxbyte_family_ops xxbyte_dex_ops = {
    XXBYTE_FAMILY_DEX, XXBYTE_DEX_MODE_DEFAULT, dex_check_mode, dex_decode, dex_switch_entry, dex_format_operands, dex_unit_size, xxbyte_dex_list_file,
};

#else
typedef int xxbyte_dex_disabled;
#endif
