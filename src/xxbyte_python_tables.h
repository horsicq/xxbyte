#ifndef XXBYTE_PYTHON_TABLES_H
#define XXBYTE_PYTHON_TABLES_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_PYTHON

/* How an opcode's argument is shown (python_op.kind), following dis. */
enum python_kind {
    PY_NONE = 0,   /* no argument */
    PY_EXT,        /* EXTENDED_ARG */
    PY_IMM,        /* plain number */
    PY_CONST,      /* co_consts index */
    PY_NAME,       /* co_names index */
    PY_GLOBAL,     /* 3.11+ LOAD_GLOBAL: name << 1 | push NULL */
    PY_ATTR,       /* 3.12+ LOAD_ATTR: name << 1 | method */
    PY_SUPER,      /* 3.12+ LOAD_SUPER_ATTR: name << 2 | flags */
    PY_LOCAL,      /* local variable */
    PY_PAIR,       /* 3.13+: two locals, first << 4 | second */
    PY_FREE,       /* cell or free variable */
    PY_JREL,       /* forward relative jump */
    PY_JBACK,      /* backward relative jump */
    PY_JFROM,      /* 3.14 END_ASYNC_FOR: backward, shown as "from" */
    PY_JABS,       /* absolute jump */
    PY_COMPARE,    /* cmp_op index */
    PY_NARGS,      /* 3.3 to 3.5 CALL_FUNCTION family */
    PY_FORMAT,     /* 3.6 to 3.12 FORMAT_VALUE */
    PY_CONVERT,    /* 3.13+ CONVERT_VALUE */
    PY_FLAGS,      /* MAKE_FUNCTION / SET_FUNCTION_ATTRIBUTE flags */
    PY_BINARY,     /* 3.11+ BINARY_OP */
    PY_INTRINSIC1, /* 3.12+ CALL_INTRINSIC_1 */
    PY_INTRINSIC2, /* 3.12+ CALL_INTRINSIC_2 */
    PY_COMMON,     /* 3.14 LOAD_COMMON_CONSTANT */
    PY_SPECIAL,    /* 3.14 LOAD_SPECIAL */
    PY_IS,         /* 3.14 IS_OP */
    PY_CONTAINS    /* 3.14 CONTAINS_OP */
};

typedef struct python_op {
    uint16_t id;    /* xxbyte_python_id, 0 when the opcode is undefined */
    uint8_t kind;   /* python_kind */
    uint8_t caches; /* inline CACHE entries that follow the instruction */
} python_op;

typedef struct python_strings {
    const char *const *items;
    uint32_t count;
} python_strings;

typedef struct python_version {
    uint32_t mode;
    const python_op *ops; /* indexed by opcode */
    python_strings cmp_op;
    python_strings nb_ops;
    python_strings intrinsic1;
    python_strings intrinsic2;
    python_strings function_flags;
    python_strings special;
    python_strings common;
} python_version;

typedef struct python_mnemonic {
    const char *name;
    uint32_t group;
} python_mnemonic;

extern const python_mnemonic xxbyte_python_mnemonics[XXBYTE_PYTHON_ID_COUNT];
extern const python_version xxbyte_python_versions[];
extern const size_t xxbyte_python_version_count;

#endif

#endif
