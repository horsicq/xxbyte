#ifndef XXBYTE_V8_INTERNAL_H
#define XXBYTE_V8_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_V8

/* Jump kinds (src/interpreter/bytecodes.h JUMP_*_BYTECODE_LIST). */
enum v8_jump {
    V8J_NONE = 0,
    V8J_IMMEDIATE, /* UImm forward offset */
    V8J_LOOP,      /* JumpLoop: UImm backward offset */
    V8J_CONSTANT   /* Idx of a Smi offset in the constant pool */
};

/* What an index operand indexes. */
enum v8_kind {
    V8K_NONE = 0,
    V8K_CONSTANT, /* constant pool */
    V8K_FEEDBACK, /* feedback vector slot */
    V8K_CONTEXT,  /* context slot */
    V8K_COVERAGE, /* block coverage slot */
    V8K_ABORT     /* AbortReason */
};

/* Encoded operand sizes (OperandTypeInfo). */
enum v8_size {
    V8S_SCALABLE = 1, /* 1, 2 or 4 bytes by the Wide/ExtraWide prefix */
    V8S_BYTE,         /* always 1 byte */
    V8S_SHORT         /* always 2 bytes */
};

/* Shape flags. */
#define V8F_PREFIX 0x01u      /* Wide, ExtraWide, DebugBreakWide, DebugBreakExtraWide */
#define V8F_DEBUG_BREAK 0x02u /* DebugBreak0..6: operands belong to the patched bytecode */
#define V8F_SCALABLE 0x04u    /* has scalable operands (accepts a prefix) */
#define V8F_SHORT_STAR 0x08u  /* Star0..Star15 */
#define V8F_FB_COMPARE 0x10u  /* EmbeddedFeedback is CompareOperationFeedback */
#define V8F_FB_BINARY 0x20u   /* EmbeddedFeedback is BinaryOperationFeedback */

#define V8_MAX_OPERANDS 6u

/* One bytecode layout of one or more versions. */
typedef struct v8_shape {
    uint16_t id;
    uint8_t count;
    uint16_t group;
    uint8_t jump;                   /* enum v8_jump */
    uint8_t flags;                  /* V8F_* */
    uint8_t types[V8_MAX_OPERANDS]; /* XXBYTE_V8_OPERAND_* */
    uint8_t kinds[V8_MAX_OPERANDS]; /* enum v8_kind */
    uint8_t sizes[V8_MAX_OPERANDS]; /* enum v8_size */
} v8_shape;

/* One V8 version. Name tables index xxbyte_v8_strings. */
typedef struct v8_version {
    uint16_t mode;
    uint16_t op_count;
    int8_t register_file_start;  /* Register::kRegisterFileStartOffset (slots) */
    uint8_t native_context_base; /* Context::Field of the first NATIVE_CONTEXT_FIELDS entry */
    uint16_t runtime_count;
    uint16_t intrinsic_count;
    uint16_t native_count;
    uint16_t abort_count;
    uint16_t binary_count;
    uint16_t compare_count;
    uint8_t hex_flags;         /* Flag8/Flag16 print in hex, constants as [idx:...] (14.6+) */
    const uint16_t *ops;       /* opcode -> xxbyte_v8_shapes index */
    const uint16_t *runtime;   /* Runtime::FunctionId -> name */
    const uint16_t *intrinsic; /* IntrinsicsHelper::IntrinsicId -> runtime name */
    const uint16_t *native;    /* native context field - base -> name */
    const uint16_t *abort;     /* AbortReason -> message */
    const uint16_t *binary;    /* BinaryOperationFeedback::TypeIndex -> name */
    const uint16_t *compare;   /* CompareOperationFeedback::TypeIndex -> name */
} v8_version;

#define XXBYTE_V8_VERSION_COUNT 6u

extern const char *const xxbyte_v8_names[XXBYTE_V8_ID_COUNT];
extern const char *const xxbyte_v8_names_wide[XXBYTE_V8_ID_COUNT];
extern const char *const xxbyte_v8_names_extra_wide[XXBYTE_V8_ID_COUNT];
extern const v8_shape xxbyte_v8_shapes[];
extern const char *const xxbyte_v8_strings[];
extern const v8_version xxbyte_v8_versions[XXBYTE_V8_VERSION_COUNT];

static inline const v8_version *v8_find_version(uint32_t mode)
{
    uint32_t i;
    for (i = 0; i < XXBYTE_V8_VERSION_COUNT; ++i) {
        if (xxbyte_v8_versions[i].mode == mode) {
            return &xxbyte_v8_versions[i];
        }
    }
    return NULL;
}

#endif

#endif
