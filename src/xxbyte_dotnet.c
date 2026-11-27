/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_dotnet_internal.h"

#if XXBYTE_USE_DOTNET

/* Groups derived from System.Reflection.Emit.FlowControl. */
#define G_NONE XXBYTE_GROUP_NONE
#define G_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_SWITCH (XXBYTE_GROUP_SWITCH | XXBYTE_GROUP_CONDITIONAL)
#define G_CALL XXBYTE_GROUP_CALL
#define G_JMP (XXBYTE_GROUP_CALL | XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)
#define G_PREFIX XXBYTE_GROUP_PREFIX

/* How an opcode's operand is modelled beyond its encoding. */
#define X_ARG 1      /* argument slot; implicit for ldarg.N */
#define X_LOCAL 2    /* local slot; implicit for ldloc.N and stloc.N */
#define X_CONST 3    /* implicit ldc.i4.N constant */
#define X_UNSIGNED 4 /* unsigned ShortInlineI (unaligned., no.) */

typedef struct dotnet_opcode {
    const char *name;
    uint16_t opcode;
    uint16_t group;
    uint8_t operand; /* xxbyte_dotnet_operand_type */
    uint8_t extra;   /* X_* */
    int8_t value;    /* implicit operand value */
} dotnet_opcode;

/* ECMA-335 Partition III, in opcode order; the index + 1 is the id. */
static const dotnet_opcode opcodes[] = {
    {"nop", 0x00, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"break", 0x01, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldarg.0", 0x02, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_ARG, 0},
    {"ldarg.1", 0x03, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_ARG, 1},
    {"ldarg.2", 0x04, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_ARG, 2},
    {"ldarg.3", 0x05, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_ARG, 3},
    {"ldloc.0", 0x06, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_LOCAL, 0},
    {"ldloc.1", 0x07, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_LOCAL, 1},
    {"ldloc.2", 0x08, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_LOCAL, 2},
    {"ldloc.3", 0x09, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_LOCAL, 3},
    {"stloc.0", 0x0A, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_LOCAL, 0},
    {"stloc.1", 0x0B, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_LOCAL, 1},
    {"stloc.2", 0x0C, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_LOCAL, 2},
    {"stloc.3", 0x0D, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_LOCAL, 3},
    {"ldarg.s", 0x0E, G_NONE, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_VAR, X_ARG, 0},
    {"ldarga.s", 0x0F, G_NONE, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_VAR, X_ARG, 0},
    {"starg.s", 0x10, G_NONE, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_VAR, X_ARG, 0},
    {"ldloc.s", 0x11, G_NONE, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_VAR, X_LOCAL, 0},
    {"ldloca.s", 0x12, G_NONE, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_VAR, X_LOCAL, 0},
    {"stloc.s", 0x13, G_NONE, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_VAR, X_LOCAL, 0},
    {"ldnull", 0x14, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldc.i4.m1", 0x15, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_CONST, -1},
    {"ldc.i4.0", 0x16, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_CONST, 0},
    {"ldc.i4.1", 0x17, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_CONST, 1},
    {"ldc.i4.2", 0x18, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_CONST, 2},
    {"ldc.i4.3", 0x19, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_CONST, 3},
    {"ldc.i4.4", 0x1A, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_CONST, 4},
    {"ldc.i4.5", 0x1B, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_CONST, 5},
    {"ldc.i4.6", 0x1C, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_CONST, 6},
    {"ldc.i4.7", 0x1D, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_CONST, 7},
    {"ldc.i4.8", 0x1E, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, X_CONST, 8},
    {"ldc.i4.s", 0x1F, G_NONE, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_I, 0, 0},
    {"ldc.i4", 0x20, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_I, 0, 0},
    {"ldc.i8", 0x21, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_I8, 0, 0},
    {"ldc.r4", 0x22, G_NONE, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_R, 0, 0},
    {"ldc.r8", 0x23, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_R, 0, 0},
    {"dup", 0x25, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"pop", 0x26, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"jmp", 0x27, G_JMP, XXBYTE_DOTNET_OPERAND_INLINE_METHOD, 0, 0},
    {"call", 0x28, G_CALL, XXBYTE_DOTNET_OPERAND_INLINE_METHOD, 0, 0},
    {"calli", 0x29, G_CALL, XXBYTE_DOTNET_OPERAND_INLINE_SIG, 0, 0},
    {"ret", 0x2A, G_RETURN, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"br.s", 0x2B, G_JUMP, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"brfalse.s", 0x2C, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"brtrue.s", 0x2D, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"beq.s", 0x2E, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"bge.s", 0x2F, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"bgt.s", 0x30, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"ble.s", 0x31, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"blt.s", 0x32, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"bne.un.s", 0x33, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"bge.un.s", 0x34, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"bgt.un.s", 0x35, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"ble.un.s", 0x36, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"blt.un.s", 0x37, G_COND, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"br", 0x38, G_JUMP, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"brfalse", 0x39, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"brtrue", 0x3A, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"beq", 0x3B, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"bge", 0x3C, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"bgt", 0x3D, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"ble", 0x3E, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"blt", 0x3F, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"bne.un", 0x40, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"bge.un", 0x41, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"bgt.un", 0x42, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"ble.un", 0x43, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"blt.un", 0x44, G_COND, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"switch", 0x45, G_SWITCH, XXBYTE_DOTNET_OPERAND_INLINE_SWITCH, 0, 0},
    {"ldind.i1", 0x46, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldind.u1", 0x47, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldind.i2", 0x48, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldind.u2", 0x49, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldind.i4", 0x4A, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldind.u4", 0x4B, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldind.i8", 0x4C, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldind.i", 0x4D, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldind.r4", 0x4E, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldind.r8", 0x4F, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldind.ref", 0x50, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stind.ref", 0x51, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stind.i1", 0x52, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stind.i2", 0x53, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stind.i4", 0x54, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stind.i8", 0x55, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stind.r4", 0x56, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stind.r8", 0x57, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"add", 0x58, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"sub", 0x59, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"mul", 0x5A, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"div", 0x5B, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"div.un", 0x5C, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"rem", 0x5D, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"rem.un", 0x5E, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"and", 0x5F, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"or", 0x60, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"xor", 0x61, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"shl", 0x62, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"shr", 0x63, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"shr.un", 0x64, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"neg", 0x65, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"not", 0x66, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.i1", 0x67, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.i2", 0x68, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.i4", 0x69, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.i8", 0x6A, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.r4", 0x6B, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.r8", 0x6C, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.u4", 0x6D, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.u8", 0x6E, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"callvirt", 0x6F, G_CALL, XXBYTE_DOTNET_OPERAND_INLINE_METHOD, 0, 0},
    {"cpobj", 0x70, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"ldobj", 0x71, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"ldstr", 0x72, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_STRING, 0, 0},
    {"newobj", 0x73, G_CALL, XXBYTE_DOTNET_OPERAND_INLINE_METHOD, 0, 0},
    {"castclass", 0x74, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"isinst", 0x75, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"conv.r.un", 0x76, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"unbox", 0x79, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"throw", 0x7A, G_THROW, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldfld", 0x7B, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_FIELD, 0, 0},
    {"ldflda", 0x7C, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_FIELD, 0, 0},
    {"stfld", 0x7D, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_FIELD, 0, 0},
    {"ldsfld", 0x7E, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_FIELD, 0, 0},
    {"ldsflda", 0x7F, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_FIELD, 0, 0},
    {"stsfld", 0x80, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_FIELD, 0, 0},
    {"stobj", 0x81, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"conv.ovf.i1.un", 0x82, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.i2.un", 0x83, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.i4.un", 0x84, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.i8.un", 0x85, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.u1.un", 0x86, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.u2.un", 0x87, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.u4.un", 0x88, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.u8.un", 0x89, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.i.un", 0x8A, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.u.un", 0x8B, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"box", 0x8C, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"newarr", 0x8D, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"ldlen", 0x8E, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelema", 0x8F, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"ldelem.i1", 0x90, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem.u1", 0x91, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem.i2", 0x92, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem.u2", 0x93, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem.i4", 0x94, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem.u4", 0x95, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem.i8", 0x96, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem.i", 0x97, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem.r4", 0x98, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem.r8", 0x99, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem.ref", 0x9A, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stelem.i", 0x9B, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stelem.i1", 0x9C, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stelem.i2", 0x9D, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stelem.i4", 0x9E, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stelem.i8", 0x9F, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stelem.r4", 0xA0, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stelem.r8", 0xA1, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"stelem.ref", 0xA2, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldelem", 0xA3, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"stelem", 0xA4, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"unbox.any", 0xA5, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"conv.ovf.i1", 0xB3, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.u1", 0xB4, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.i2", 0xB5, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.u2", 0xB6, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.i4", 0xB7, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.u4", 0xB8, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.i8", 0xB9, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.u8", 0xBA, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"refanyval", 0xC2, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"ckfinite", 0xC3, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"mkrefany", 0xC6, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"ldtoken", 0xD0, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TOK, 0, 0},
    {"conv.u2", 0xD1, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.u1", 0xD2, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.i", 0xD3, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.i", 0xD4, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.ovf.u", 0xD5, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"add.ovf", 0xD6, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"add.ovf.un", 0xD7, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"mul.ovf", 0xD8, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"mul.ovf.un", 0xD9, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"sub.ovf", 0xDA, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"sub.ovf.un", 0xDB, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"endfinally", 0xDC, G_RETURN, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"leave", 0xDD, G_JUMP, XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET, 0, 0},
    {"leave.s", 0xDE, G_JUMP, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, 0, 0},
    {"stind.i", 0xDF, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"conv.u", 0xE0, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"arglist", 0xFE00, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ceq", 0xFE01, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"cgt", 0xFE02, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"cgt.un", 0xFE03, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"clt", 0xFE04, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"clt.un", 0xFE05, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"ldftn", 0xFE06, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_METHOD, 0, 0},
    {"ldvirtftn", 0xFE07, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_METHOD, 0, 0},
    {"ldarg", 0xFE09, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_VAR, X_ARG, 0},
    {"ldarga", 0xFE0A, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_VAR, X_ARG, 0},
    {"starg", 0xFE0B, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_VAR, X_ARG, 0},
    {"ldloc", 0xFE0C, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_VAR, X_LOCAL, 0},
    {"ldloca", 0xFE0D, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_VAR, X_LOCAL, 0},
    {"stloc", 0xFE0E, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_VAR, X_LOCAL, 0},
    {"localloc", 0xFE0F, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"endfilter", 0xFE11, G_RETURN, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"unaligned.", 0xFE12, G_PREFIX, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_I, X_UNSIGNED, 0},
    {"volatile.", 0xFE13, G_PREFIX, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"tail.", 0xFE14, G_PREFIX, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"initobj", 0xFE15, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"constrained.", 0xFE16, G_PREFIX, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"cpblk", 0xFE17, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"initblk", 0xFE18, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"no.", 0xFE19, G_PREFIX, XXBYTE_DOTNET_OPERAND_SHORT_INLINE_I, X_UNSIGNED, 0},
    {"rethrow", 0xFE1A, G_THROW, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"sizeof", 0xFE1C, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_TYPE, 0, 0},
    {"refanytype", 0xFE1D, G_NONE, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
    {"readonly.", 0xFE1E, G_PREFIX, XXBYTE_DOTNET_OPERAND_INLINE_NONE, 0, 0},
};

#define OPCODE_COUNT (sizeof(opcodes) / sizeof(opcodes[0]))

/* Opcode byte to id, 0 for undefined (0xFE is the two-byte prefix). */
static const uint8_t one_byte_ids[256] = {
    1,   2,   3,   4,   5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,
    33,  34,  35,  36,  0,   37,  38,  39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,  60,  61,  62,  63,
    64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,  90,  91,  92,  93,  94,  95,
    96,  97,  98,  99,  100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 0,   0,   119, 120, 121, 122, 123, 124, 125,
    126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157,
    158, 159, 160, 161, 162, 163, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   164, 165, 166, 167, 168, 169, 170, 171, 0,   0,   0,   0,   0,
    0,   0,   172, 173, 0,   0,   174, 0,   0,   0,   0,   0,   0,   0,   0,   0,   175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190,
    191, 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

/* Second byte after 0xFE to id. */
static const uint8_t two_byte_ids[0x1F] = {
    192, 193, 194, 195, 196, 197, 198, 199, 0, 200, 201, 202, 203, 204, 205, 206, 0, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 0, 217, 218, 219,
};

/* Encoded operand bytes by xxbyte_dotnet_operand_type (for a switch, its
 * count; the targets follow). */
static const uint8_t operand_sizes[] = {
    0, 0, 1, 4, 1, 4, 8, 4, 8, 1, 2, 4, 4, 4, 4, 4, 4, 4,
};

static xxbyte_status dotnet_check_mode(uint32_t mode)
{
    return mode == XXBYTE_DOTNET_MODE_DEFAULT ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static const dotnet_opcode *find_opcode(uint32_t id)
{
    return (id != 0 && id <= OPCODE_COUNT) ? &opcodes[id - 1] : NULL;
}

/* address + size + displacement, wrapping like the 64-bit address space. */
static int64_t branch_target(uint64_t address, uint64_t size, int64_t disp)
{
    return (int64_t)(address + size + (uint64_t)disp);
}

static void add_value(xxbyte_instruction *instruction, xxbyte_operand_type type, uint32_t size, int64_t value, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, type, size, value);
    if (operand != NULL) {
        operand->flags = flags;
    }
}

static void add_slot(xxbyte_instruction *instruction, const dotnet_opcode *op, uint32_t size, int64_t value, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_index(instruction, op->extra == X_ARG ? XXBYTE_INDEX_ARGUMENT : XXBYTE_INDEX_LOCAL, size, value);
    if (operand != NULL) {
        operand->flags = flags;
    }
}

static xxbyte_status dotnet_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const dotnet_opcode *op;
    const uint8_t *p;
    uint32_t length;
    uint32_t size;
    uint8_t id;

    (void)context;
    (void)mode;
    if (code[0] == 0xFE) {
        if (code_size < 2) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        id = code[1] < sizeof(two_byte_ids) ? two_byte_ids[code[1]] : 0;
        length = 2;
    } else {
        id = one_byte_ids[code[0]];
        length = 1;
    }
    if (id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    op = &opcodes[id - 1];
    if (operand_sizes[op->operand] > code_size - length) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    p = code + length;
    size = length + operand_sizes[op->operand];

    switch (op->operand) {
        case XXBYTE_DOTNET_OPERAND_INLINE_NONE:
            if (op->extra == X_CONST) {
                add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, op->value, XXBYTE_OPERAND_FLAG_IMPLICIT | XXBYTE_OPERAND_FLAG_SIGNED);
            } else if (op->extra != 0) {
                add_slot(instruction, op, 0, op->value, XXBYTE_OPERAND_FLAG_IMPLICIT);
            }
            break;
        case XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET: add_value(instruction, XXBYTE_OPERAND_BRANCH, 1, branch_target(address, size, xxbyte_sext(p[0], 8)), 0); break;
        case XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET:
            add_value(instruction, XXBYTE_OPERAND_BRANCH, 4, branch_target(address, size, xxbyte_sext(xxbyte_u32le(p), 32)), 0);
            break;
        case XXBYTE_DOTNET_OPERAND_SHORT_INLINE_I:
            if (op->extra == X_UNSIGNED) {
                add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, p[0], 0);
            } else {
                add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, xxbyte_sext(p[0], 8), XXBYTE_OPERAND_FLAG_SIGNED);
            }
            break;
        case XXBYTE_DOTNET_OPERAND_INLINE_I: add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, xxbyte_sext(xxbyte_u32le(p), 32), XXBYTE_OPERAND_FLAG_SIGNED); break;
        case XXBYTE_DOTNET_OPERAND_INLINE_I8:
            add_value(instruction, XXBYTE_OPERAND_IMMEDIATE, 8, (int64_t)xxbyte_u64le(p), XXBYTE_OPERAND_FLAG_SIGNED | XXBYTE_OPERAND_FLAG_WIDE);
            break;
        case XXBYTE_DOTNET_OPERAND_SHORT_INLINE_R: add_value(instruction, XXBYTE_OPERAND_FLOAT, 4, (int64_t)xxbyte_u32le(p), 0); break;
        case XXBYTE_DOTNET_OPERAND_INLINE_R: add_value(instruction, XXBYTE_OPERAND_FLOAT, 8, (int64_t)xxbyte_u64le(p), XXBYTE_OPERAND_FLAG_WIDE); break;
        case XXBYTE_DOTNET_OPERAND_SHORT_INLINE_VAR: add_slot(instruction, op, 1, p[0], 0); break;
        case XXBYTE_DOTNET_OPERAND_INLINE_VAR: add_slot(instruction, op, 2, xxbyte_u16le(p), 0); break;
        case XXBYTE_DOTNET_OPERAND_INLINE_SWITCH: {
            uint32_t count = xxbyte_u32le(p);
            if (count > (code_size - size) / 4u) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            if ((uint64_t)size + (uint64_t)count * 4u > UINT32_MAX) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            instruction->table_count = count;
            instruction->table_offset = size;
            size += count * 4u;
            break;
        }
        default: add_value(instruction, XXBYTE_OPERAND_TOKEN, 4, (int64_t)xxbyte_u32le(p), 0); break;
    }
    instruction->size = size;
    instruction->opcode = op->opcode;
    instruction->id = id;
    instruction->group = op->group;
    instruction->mnemonic = op->name;
    return XXBYTE_STATUS_OK;
}

static int64_t switch_target(const xxbyte_instruction *instruction, uint32_t index)
{
    const uint8_t *entry = instruction->code + instruction->table_offset + (size_t)index * 4u;
    return branch_target(instruction->address, instruction->size, xxbyte_sext(xxbyte_u32le(entry), 32));
}

static xxbyte_status dotnet_switch_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target)
{
    *key = index;
    *target = switch_target(instruction, index);
    return XXBYTE_STATUS_OK;
}

static int format_label(xxbyte_fmt *fmt, int64_t target)
{
    if (target < 0) {
        return xxbyte_fmt_printf(fmt, "IL_-%04llx", (unsigned long long)(0u - (uint64_t)target));
    }
    return xxbyte_fmt_printf(fmt, "IL_%04llx", (unsigned long long)target);
}

/* Switch targets are not operands; the resolver sees each one as a BRANCH
 * entry (see xxbyte_fmt_resolve_entry). */
static int format_switch(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    xxbyte_operand entry;
    uint32_t i;

    if (!xxbyte_fmt_str(fmt, " (")) {
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    entry.type = XXBYTE_OPERAND_BRANCH;
    entry.size = 4;
    for (i = 0; i < instruction->table_count; ++i) {
        int64_t target = switch_target(instruction, i);
        int resolved;
        if (i != 0 && !xxbyte_fmt_str(fmt, ", ")) {
            return 0;
        }
        entry.value = target;
        resolved = xxbyte_fmt_resolve_entry(fmt, instruction, &entry);
        if (resolved < 0) {
            return 0;
        }
        if (resolved == 0 && !format_label(fmt, target)) {
            return 0;
        }
    }
    return xxbyte_fmt_char(fmt, ')');
}

static int format_float(xxbyte_fmt *fmt, const xxbyte_operand *operand)
{
    if (operand->size == 4) {
        uint32_t bits = (uint32_t)operand->value;
        float value;
        memcpy(&value, &bits, sizeof(value));
        return xxbyte_fmt_float(fmt, value);
    } else {
        uint64_t bits = (uint64_t)operand->value;
        double value;
        memcpy(&value, &bits, sizeof(value));
        return xxbyte_fmt_double(fmt, value);
    }
}

static int dotnet_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;

    if ((instruction->group & XXBYTE_GROUP_SWITCH) != 0) {
        return format_switch(instruction, fmt);
    }
    for (i = 0; i < instruction->operand_count; ++i) {
        const xxbyte_operand *operand = &instruction->operands[i];
        int ok;

        if ((operand->flags & XXBYTE_OPERAND_FLAG_IMPLICIT) != 0) {
            continue;
        }
        if (!xxbyte_fmt_char(fmt, ' ')) {
            return 0;
        }
        if (operand->type == XXBYTE_OPERAND_BRANCH || operand->type == XXBYTE_OPERAND_TOKEN || operand->type == XXBYTE_OPERAND_INDEX) {
            int resolved = xxbyte_fmt_resolve(fmt, instruction, i);
            if (resolved < 0) {
                return 0;
            }
            if (resolved > 0) {
                continue;
            }
        }
        switch (operand->type) {
            case XXBYTE_OPERAND_BRANCH: ok = format_label(fmt, operand->value); break;
            case XXBYTE_OPERAND_TOKEN: ok = xxbyte_fmt_printf(fmt, "0x%08X", (unsigned)operand->value); break;
            case XXBYTE_OPERAND_FLOAT: ok = format_float(fmt, operand); break;
            default: ok = xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value); break;
        }
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

const xxbyte_family_ops xxbyte_dotnet_ops = {
    XXBYTE_FAMILY_DOTNET, XXBYTE_DOTNET_MODE_DEFAULT, dotnet_check_mode, dotnet_decode, dotnet_switch_entry, dotnet_format_operands, NULL, xxbyte_dotnet_list_file,
};

const char *xxbyte_dotnet_mnemonic(uint32_t id)
{
    const dotnet_opcode *op = find_opcode(id);
    return op != NULL ? op->name : NULL;
}

xxbyte_dotnet_operand_type xxbyte_dotnet_operand_type_of(uint32_t id)
{
    const dotnet_opcode *op = find_opcode(id);
    return op != NULL ? (xxbyte_dotnet_operand_type)op->operand : XXBYTE_DOTNET_OPERAND_INVALID;
}

xxbyte_status xxbyte_dotnet_parse_method_header(const uint8_t *data, size_t size, xxbyte_dotnet_method_header *header)
{
    xxbyte_dotnet_method_header h;
    uint32_t flags;

    if (header == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(header, 0, sizeof(*header));
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size == 0) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    memset(&h, 0, sizeof(h));
    switch (data[0] & XXBYTE_DOTNET_METHOD_FORMAT_MASK) {
        case XXBYTE_DOTNET_METHOD_TINY_FORMAT:
            h.header_size = 1;
            h.code_size = (uint32_t)(data[0] >> 2);
            h.max_stack = 8;
            h.flags = XXBYTE_DOTNET_METHOD_TINY_FORMAT;
            break;
        case XXBYTE_DOTNET_METHOD_FAT_FORMAT:
            if (size < 2) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            flags = xxbyte_u16le(data);
            /* The header size nibble counts dwords; only 3 is defined. */
            if ((flags >> 12) != 3) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (size < 12) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            h.header_size = 12;
            h.flags = flags & 0x0FFFu;
            h.max_stack = xxbyte_u16le(data + 2);
            h.code_size = xxbyte_u32le(data + 4);
            h.local_var_sig_token = xxbyte_u32le(data + 8);
            h.is_fat = 1;
            h.has_more_sections = (flags & XXBYTE_DOTNET_METHOD_MORE_SECTS) != 0;
            break;
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if ((uint64_t)h.header_size + h.code_size > size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *header = h;
    return XXBYTE_STATUS_OK;
}

static void read_clause(const uint8_t *p, int fat, xxbyte_dotnet_exception_clause *clause)
{
    if (fat) {
        clause->flags = xxbyte_u32le(p);
        clause->try_offset = xxbyte_u32le(p + 4);
        clause->try_length = xxbyte_u32le(p + 8);
        clause->handler_offset = xxbyte_u32le(p + 12);
        clause->handler_length = xxbyte_u32le(p + 16);
        clause->class_token_or_filter_offset = xxbyte_u32le(p + 20);
    } else {
        clause->flags = xxbyte_u16le(p);
        clause->try_offset = xxbyte_u16le(p + 2);
        clause->try_length = p[4];
        clause->handler_offset = xxbyte_u16le(p + 5);
        clause->handler_length = p[7];
        clause->class_token_or_filter_offset = xxbyte_u32le(p + 8);
    }
}

xxbyte_status xxbyte_dotnet_parse_exception_clauses(const uint8_t *method, size_t size, const xxbyte_dotnet_method_header *header,
                                                    xxbyte_dotnet_exception_clause *clauses, size_t capacity, size_t *count)
{
    uint64_t offset;
    size_t total = 0;

    if (count != NULL) {
        *count = 0;
    }
    if (header == NULL || count == NULL || (method == NULL && size != 0) || (clauses == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    offset = (uint64_t)header->header_size + header->code_size;
    if (offset > size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (!header->has_more_sections) {
        return XXBYTE_STATUS_OK;
    }
    for (;;) {
        uint32_t kind, data_size, clause_size, n, i;
        const uint8_t *section;

        offset = (offset + 3u) & ~(uint64_t)3u;
        if (offset + 4u > size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        section = method + offset;
        kind = section[0];
        if ((kind & XXBYTE_DOTNET_SECT_FAT_FORMAT) != 0) {
            data_size = (uint32_t)section[1] | ((uint32_t)section[2] << 8) | ((uint32_t)section[3] << 16);
            clause_size = 24;
        } else {
            data_size = section[1];
            clause_size = 12;
        }
        if (data_size < 4) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (offset + data_size > size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if ((kind & XXBYTE_DOTNET_SECT_KIND_MASK) == XXBYTE_DOTNET_SECT_EH_TABLE) {
            n = (data_size - 4) / clause_size;
            for (i = 0; i < n; ++i, ++total) {
                if (total < capacity) {
                    read_clause(section + 4 + (size_t)i * clause_size, clause_size == 24, &clauses[total]);
                }
            }
        }
        if ((kind & XXBYTE_DOTNET_SECT_MORE_SECTS) == 0) {
            break;
        }
        offset += data_size;
    }
    *count = total;
    return XXBYTE_STATUS_OK;
}

#else
typedef int xxbyte_dotnet_disabled;
#endif
