/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_VB_TABLES_H
#define XXBYTE_VB_TABLES_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_VB

/*
 * Operand layout of a mnemonic (vb_mnemonic.layout), one character per
 * encoded field in order:
 *   a  int16 stack-frame offset
 *   c  uint16 constant-pool index
 *   t  uint16 Redim element type, followed by the element size and the
 *      FADF flags: a constant-pool index, or an IMMEDIATE VARTYPE when it
 *      is FFFF or, except in VB5 mode, the flags have FADF_HAVEVARTYPE
 *   i  uint16 import slot (a constant-pool index)
 *   v  uint16 vtable offset
 *   l  uint16 branch target
 *   e  uint16 OnErrorGoto target; 0, FFFE and FFFF are not branches
 *   r  uint16 Resume target; FFFE and FFFF are not branches
 *   m  uint16 member offset
 *   M  int16 member offset
 *   n  uint16 count, size or flags
 *   b  uint8 immediate
 *   B  int8 literal
 *   w  int16 literal
 *   u  uint16 field whose low byte is a literal
 *   d  int32 literal
 *   D  uint32 immediate (DISPID, event number)
 *   f  float literal
 *   F  double literal
 *   y  int64 currency literal
 *   Y  int32 currency literal
 *   x  uint8 data (list entries only)
 * A layout starting with '*' is variable-length: a uint16 byte count
 * follows the opcode, then the fields up to ':', then list entries of the
 * kind after ':' up to the counted end.
 */
typedef struct vb_mnemonic {
    const char *name;
    const char *layout;
    uint32_t group;
} vb_mnemonic;

#define VB_OP_VB6 0x01u /* undefined in MSVBVM50 */
/* MSVBVM50 does not read the leading 'v' field: in VB5 mode the layout
 * starts after it and the instruction is 2 bytes shorter. */
#define VB_OP_VB5_NO_VTABLE 0x02u

typedef struct vb_op {
    uint16_t id;   /* xxbyte_vb_id, 0 when the slot is undefined */
    uint8_t size;  /* total encoded size, 0 for variable-length forms */
    uint8_t flags; /* VB_OP_* */
} vb_op;

extern const vb_mnemonic xxbyte_vb_mnemonics[XXBYTE_VB_ID_COUNT];
extern const vb_op xxbyte_vb_ops_primary[256];
extern const vb_op xxbyte_vb_ops_lead[5][256];

#endif

#endif
