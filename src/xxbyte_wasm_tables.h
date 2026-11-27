/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_WASM_TABLES_H
#define XXBYTE_WASM_TABLES_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_WASM

/* Immediate layouts after the opcode. */
enum {
    WF_NONE,
    WF_BLOCK,         /* blocktype */
    WF_TRY_TABLE,     /* blocktype, vec(catch) */
    WF_LABEL,         /* labelidx */
    WF_BR_TABLE,      /* vec(labelidx), labelidx */
    WF_FUNC,          /* funcidx */
    WF_CALL_INDIRECT, /* typeidx, tableidx (0x00 before reference types) */
    WF_TYPE,          /* typeidx */
    WF_TAG,           /* tagidx */
    WF_SELECT_T,      /* vec(valtype) */
    WF_LOCAL,         /* localidx */
    WF_GLOBAL,        /* globalidx */
    WF_TABLE,         /* tableidx */
    WF_MEMARG,        /* memarg */
    WF_MEMARG_LANE,   /* memarg, lane byte */
    WF_MEMORY,        /* memidx (0x00 before multi-memory) */
    WF_MEMORY_COPY,   /* memidx, memidx */
    WF_MEMORY_INIT,   /* dataidx, memidx */
    WF_DATA,          /* dataidx */
    WF_ELEM,          /* elemidx */
    WF_TABLE_INIT,    /* elemidx, tableidx */
    WF_TABLE_COPY,    /* tableidx, tableidx */
    WF_I32,           /* s32 */
    WF_I64,           /* s64 */
    WF_F32,           /* 4 bytes */
    WF_F64,           /* 8 bytes */
    WF_V128,          /* 16 bytes */
    WF_SHUFFLE,       /* 16 lane bytes */
    WF_LANE,          /* lane byte */
    WF_HEAPTYPE,      /* heaptype */
    WF_FENCE,         /* reserved ordering byte 0x00 */
    WF_TYPE_FIELD,    /* typeidx, fieldidx */
    WF_TYPE_COUNT,    /* typeidx, u32 */
    WF_TYPE_DATA,     /* typeidx, dataidx */
    WF_TYPE_ELEM,     /* typeidx, elemidx */
    WF_TYPE_TYPE,     /* typeidx, typeidx */
    WF_REF,           /* heaptype of (ref ht) */
    WF_REF_NULL,      /* heaptype of (ref null ht) */
    WF_BR_ON_CAST,    /* castflags, labelidx, heaptype, heaptype */
    WF_COUNT
};

#define WNO_SIGN_EXTENSION XXBYTE_WASM_MODE_NO_SIGN_EXTENSION
#define WNO_SAT_FLOAT_TO_INT XXBYTE_WASM_MODE_NO_SAT_FLOAT_TO_INT
#define WNO_MULTI_VALUE XXBYTE_WASM_MODE_NO_MULTI_VALUE
#define WNO_REFERENCE_TYPES XXBYTE_WASM_MODE_NO_REFERENCE_TYPES
#define WNO_BULK_MEMORY XXBYTE_WASM_MODE_NO_BULK_MEMORY
#define WNO_SIMD XXBYTE_WASM_MODE_NO_SIMD
#define WNO_RELAXED_SIMD XXBYTE_WASM_MODE_NO_RELAXED_SIMD
#define WNO_THREADS XXBYTE_WASM_MODE_NO_THREADS
#define WNO_TAIL_CALL XXBYTE_WASM_MODE_NO_TAIL_CALL
#define WNO_EXCEPTIONS XXBYTE_WASM_MODE_NO_EXCEPTIONS
#define WNO_LEGACY_EXCEPTIONS XXBYTE_WASM_MODE_NO_LEGACY_EXCEPTIONS
#define WNO_FUNCTION_REFERENCES XXBYTE_WASM_MODE_NO_FUNCTION_REFERENCES
#define WNO_GC XXBYTE_WASM_MODE_NO_GC
#define WNO_MEMORY64 XXBYTE_WASM_MODE_NO_MEMORY64
#define WNO_MULTI_MEMORY XXBYTE_WASM_MODE_NO_MULTI_MEMORY
#define WNO_WIDE_ARITHMETIC XXBYTE_WASM_MODE_NO_WIDE_ARITHMETIC

/* Groups. */
#define G_NONE XXBYTE_GROUP_NONE
#define G_STOP XXBYTE_GROUP_STOP
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_GOTO (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_SWITCH (XXBYTE_GROUP_SWITCH | XXBYTE_GROUP_STOP)
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_CALL XXBYTE_GROUP_CALL
#define G_TAILCALL (XXBYTE_GROUP_CALL | XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)

/* The opcode needs any one of its features instead of all of them. */
#define OPF_ANY 0x01u

typedef struct xxbyte_wasm_op {
    uint16_t id;       /* XXBYTE_WASM_ID_*, 0 for an undefined opcode */
    uint8_t form;      /* WF_* */
    uint8_t width;     /* log2 of the natural access width (memory forms) */
    uint16_t features; /* XXBYTE_WASM_MODE_NO_* bits of the needed features */
    uint16_t group;    /* XXBYTE_GROUP_* */
    uint8_t flags;     /* OPF_* */
} xxbyte_wasm_op;

#define XXBYTE_WASM_FB_COUNT 31u
#define XXBYTE_WASM_FC_COUNT 23u
#define XXBYTE_WASM_FD_COUNT 276u
#define XXBYTE_WASM_FE_COUNT 79u

extern const char *const xxbyte_wasm_mnemonics[XXBYTE_WASM_ID_COUNT - 1];
extern const xxbyte_wasm_op xxbyte_wasm_ops_single[256];
extern const xxbyte_wasm_op xxbyte_wasm_ops_fb[XXBYTE_WASM_FB_COUNT];
extern const xxbyte_wasm_op xxbyte_wasm_ops_fc[XXBYTE_WASM_FC_COUNT];
extern const xxbyte_wasm_op xxbyte_wasm_ops_fd[XXBYTE_WASM_FD_COUNT];
extern const xxbyte_wasm_op xxbyte_wasm_ops_fe[XXBYTE_WASM_FE_COUNT];

#endif

#endif
