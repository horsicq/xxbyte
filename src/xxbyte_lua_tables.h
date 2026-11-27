/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_LUA_TABLES_H
#define XXBYTE_LUA_TABLES_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_LUA

/* Instruction word layouts (field positions and widths). */
enum lua_layout {
    LUA_LAYOUT_50, /* 5.0: OP 6, C 9, B 9, A 8 from bit 0 */
    LUA_LAYOUT_51, /* 5.1 to 5.3: OP 6, A 8, C 9, B 9 from bit 0 */
    LUA_LAYOUT_54  /* 5.4, 5.5: OP 7, A 8, k 1, B 8, C 8 from bit 0 */
};

/* Where an operand comes from in the instruction word. */
enum lua_source {
    LF_A,
    LF_B,
    LF_C,
    LF_K, /* the k bit (5.4+) */
    LF_BX,
    LF_SBX, /* Bx in excess-K */
    LF_AX,
    LF_SJ, /* 25-bit sJ in excess-K (5.4+) */
    LF_SB, /* B in excess-K (5.4+) */
    LF_SC, /* C in excess-K (5.4+) */
    LF_VB, /* 6-bit vB (5.5) */
    LF_VC  /* 10-bit vC (5.5) */
};

/* What the field holds. */
enum lua_kind {
    LK_REG,     /* register */
    LK_UINT,    /* unsigned immediate (also fields an opcode ignores) */
    LK_SINT,    /* signed immediate */
    LK_CONST,   /* constant index */
    LK_UPVAL,   /* upvalue index */
    LK_PROTO,   /* nested prototype index */
    LK_RK50,    /* 5.0 RK: constant x - 250 when x >= 250 */
    LK_RK51,    /* 5.1-5.3 RK: constant x & 255 when bit 8 is set */
    LK_RK54,    /* 5.4+ RK: constant when the k bit is set */
    LK_KFLAG,   /* the k bit as an immediate */
    LK_ERRNNIL, /* 5.5 ERRNNIL: constant Bx - 1, or 0 for none */
    LK_JUMP,    /* branch to address + unit * (1 + field) */
    LK_BACK,    /* branch to address + unit * (1 - field) */
    LK_EXIT     /* branch to address + unit * (2 + field), "exit to" */
};

/* How luac -l shows the field. */
enum lua_display {
    LD_VALUE,  /* the field value (signed for excess-K fields) */
    LD_MYK,    /* -1 - field (5.1-5.3 constant numbering) */
    LD_ISK,    /* 5.1-5.3 B and C: -1 - (field & 255) when bit 8 is set */
    LD_SUFFIX, /* "k" appended to the previous column when set */
    LD_HIDDEN  /* not shown */
};

/* lua_op.flags */
#define LO_SKIP                                                            \
    0x01u                     /* skips the next instruction (conditionally \
                                 unless the group has STOP) */
#define LO_SKIP_IF_C 0x02u    /* LOADBOOL: skips the next one when C != 0 */
#define LO_SETLIST_WORD 0x04u /* 5.1 SETLIST: C == 0 takes the next word */

#define LUA_MAX_FIELDS 4u

typedef struct lua_field {
    uint8_t source;  /* lua_source */
    uint8_t kind;    /* lua_kind */
    uint8_t display; /* lua_display */
} lua_field;

typedef struct lua_op {
    uint8_t id;     /* xxbyte_lua_id */
    uint16_t group; /* XXBYTE_GROUP_* */
    uint8_t flags;  /* LO_* */
    uint8_t count;  /* fields in use */
    lua_field fields[LUA_MAX_FIELDS];
} lua_op;

typedef struct lua_version {
    uint8_t layout; /* lua_layout */
    uint8_t opcode_count;
    const lua_op *ops; /* indexed by opcode */
} lua_version;

/* 5.0, 5.1, 5.2, 5.3, 5.4 and 5.5, indexed by minor version. */
#define XXBYTE_LUA_VERSION_COUNT 6u

extern const char *const xxbyte_lua_mnemonics[XXBYTE_LUA_ID_COUNT - 1];
extern const lua_version xxbyte_lua_versions[XXBYTE_LUA_VERSION_COUNT];

#endif

#endif
