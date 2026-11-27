/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_HERMES_TABLES_H
#define XXBYTE_HERMES_TABLES_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_HERMES

/* Operand types of BytecodeList.def (DEFINE_OPERAND_TYPE). */
enum hermes_type {
    HT_NONE = 0,
    HT_REG8,   /* uint8_t register */
    HT_REG32,  /* uint32_t register */
    HT_UINT8,  /* uint8_t */
    HT_UINT16, /* uint16_t */
    HT_UINT32, /* uint32_t */
    HT_ADDR8,  /* int8_t, relative to the instruction */
    HT_ADDR32, /* int32_t, relative to the instruction */
    HT_IMM32,  /* int32_t */
    HT_DOUBLE  /* IEEE-754 double */
};

/* What an operand indexes (OPERAND_STRING_ID, OPERAND_FUNCTION_ID,
 * OPERAND_BIGINT_ID, and the builtin operand of CallBuiltin and
 * GetBuiltinClosure). */
enum hermes_kind {
    HK_NONE = 0,
    HK_STRING,
    HK_FUNCTION,
    HK_BIGINT,
    HK_BUILTIN
};

#define HERMES_MAX_OPERANDS 6u

/* One opcode layout: mnemonic id, operand count, group, operand types and
 * kinds. */
typedef struct hermes_shape {
    uint16_t id;
    uint8_t count;
    uint16_t group;
    uint8_t types[HERMES_MAX_OPERANDS];
    uint8_t kinds[HERMES_MAX_OPERANDS];
} hermes_shape;

/* One bytecode version: opcode -> shape index, and the builtin method table
 * (indexes into xxbyte_hermes_builtin_names). */
typedef struct hermes_version {
    uint16_t version;
    uint16_t op_count;
    uint16_t builtin_count;
    const uint16_t *ops;
    const uint8_t *builtins;
} hermes_version;

#define XXBYTE_HERMES_VERSION_COUNT (XXBYTE_HERMES_MODE_NEWEST - XXBYTE_HERMES_MODE_OLDEST + 1u)

extern const char *const xxbyte_hermes_names[XXBYTE_HERMES_ID_COUNT];
extern const hermes_shape xxbyte_hermes_shapes[];
extern const char *const xxbyte_hermes_builtin_names[];
extern const hermes_version xxbyte_hermes_versions[XXBYTE_HERMES_VERSION_COUNT];

/* The table of a supported version, or NULL. */
static inline const hermes_version *hermes_find_version(uint32_t version)
{
    if (version < XXBYTE_HERMES_MODE_OLDEST || version > XXBYTE_HERMES_MODE_NEWEST) {
        return NULL;
    }
    return &xxbyte_hermes_versions[version - XXBYTE_HERMES_MODE_OLDEST];
}

/* File-format helpers shared by the decoder and the container code. */
const char *xxbyte_hermes_builtin_name(const hermes_version *version, uint32_t index);

#endif

#endif
