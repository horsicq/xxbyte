/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_DOTNET_H
#define XXBYTE_XXBYTE_DOTNET_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_DOTNET
#error "the .NET CIL decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ECMA-335 Common Intermediate Language (MSIL) method bodies.
 *
 * Mode: CIL has a single encoding, XXBYTE_DOTNET_MODE_DEFAULT; any other
 * mode is XXBYTE_STATUS_UNSUPPORTED_MODE. xxbyte_disasm_file also takes
 * XXBYTE_MODE_AUTO, which means the same. Addresses are IL byte offsets from
 * the first byte after the method header. Decoding needs no context.
 *
 * Text: the mnemonic, a space and the operand, as ildasm prints it with
 * numeric tokens: "ret", "ldc.i4.s 123", "ldarg.s 5", "ldc.r4 0.1",
 * "br.s IL_0010", "switch (IL_000e, IL_000c)", "call 0x0A000001",
 * "ldstr 0x70000001". Integers are decimal, floats the shortest round-trip
 * text (xxbyte_fmt_double), branch targets "IL_" and at least four
 * lowercase hex digits ("IL_-0004" before the start) and tokens "0x" and
 * eight uppercase hex digits. With the xxbyte_dotnet_resolve resolver tokens
 * print as names: "call void [mscorlib]System.Console::WriteLine(string)",
 * "ldstr \"Hello\\n\"", "newobj instance void Demo.Foo::.ctor()",
 * "ldfld int32 class Demo.Box`1<!0>::Value", "box [mscorlib]System.Int32",
 * "ldtoken method void Demo.Foo::Bar()" (see xxbyte_dotnet_token_name).
 *
 * Operands (xxbyte_instruction.operands):
 * - ldarg.N, ldloc.N, stloc.N: an IMPLICIT INDEX ARGUMENT or LOCAL; the
 *   .s and long forms an INDEX of encoded size 1 or 2.
 * - ldc.i4.N and ldc.i4.m1: an IMPLICIT SIGNED IMMEDIATE; ldc.i4.s, ldc.i4
 *   and ldc.i8 a SIGNED IMMEDIATE (ldc.i8 also WIDE); unaligned. and no. an
 *   unsigned IMMEDIATE.
 * - ldc.r4, ldc.r8: a FLOAT holding the IEEE-754 bits.
 * - branches: a BRANCH whose value is the absolute target.
 * - switch: no operand; table_count targets at table_offset, read with
 *   xxbyte_switch_entry (keys 0 to n - 1), each shown to a resolver as a
 *   BRANCH entry.
 * - InlineMethod, InlineField, InlineType, InlineTok, InlineSig and
 *   InlineString (ldstr): a TOKEN holding the 32-bit metadata token.
 * Groups follow System.Reflection.Emit.FlowControl: br, br.s, leave and
 * leave.s JUMP|STOP, conditional branches CONDITIONAL, switch
 * SWITCH|CONDITIONAL, call, calli, callvirt and newobj CALL, jmp
 * CALL|JUMP|STOP, ret, endfinally and endfilter RETURN|STOP, throw and
 * rethrow THROW|STOP, the prefixes (unaligned., volatile., tail.,
 * constrained., no., readonly.) PREFIX.
 *
 * Containers: xxbyte_disasm_file reads PE32 and PE32+ files with CLI
 * metadata (.exe, .dll, .netmodule; native ReadyToRun code is ignored, the
 * IL is listed). For every MethodDef with an IL body, in table order, it
 * prints
 *   // Demo.Space.Program::Main(string[])  RVA 0x2050
 *   // token 0x06000001  returns void  fat header  code size 26  max stack 2  locals init (int32)
 *   // .try IL_0001 to IL_000b catch [mscorlib]System.Exception handler IL_000b to IL_0015
 * (the method title of xxbyte_dotnet_method_title, its token, calling
 * convention ("instance, returns void", "vararg, returns int32") and return
 * type, the body header, the local signature, one line
 * per exception clause as ildasm writes .try: "catch T", "filter IL_xxxx",
 * "finally" or "fault", ends exclusive), then the listing of the IL with
 * names resolved by xxbyte_dotnet_resolve, then an empty line. A body that
 * does not parse prints "// token 0x...  bad method body: <status>" and the
 * listing goes on; the first such status is returned at the end. Methods
 * without IL (abstract, pinvokeimpl, runtime, native, RVA 0) are skipped.
 * When the bodies listed add up to more than 8 times the file size plus
 * 1 MiB (MethodDefs sharing one RVA), the listing stops with
 * XXBYTE_STATUS_INVALID_INSTRUCTION.
 *
 * Limitations: names are those of the operand text of ildasm, checked
 * against mono's monodis (see the differences under
 * xxbyte_dotnet_token_name); names longer than XXBYTE_RESOLVER_NAME_LIMIT
 * (1 MiB) are cut; generic parameters print by number; portable PDB tables (0x30 and
 * up) and the pre-2.0 GenericParam layout are not read; the CLI header's
 * metadata size is not enforced (streams must lie in the section).
 */
#define XXBYTE_DOTNET_MODE_DEFAULT 0u

/*
 * Mnemonic identifiers (xxbyte_instruction.id): one per ECMA-335 opcode, in
 * opcode order, one-byte opcodes first. The name is the mnemonic with every
 * run of non-alphanumeric characters replaced by '_' ("unaligned." is
 * XXBYTE_DOTNET_ID_UNALIGNED_). Valid ids are 1 to XXBYTE_DOTNET_ID_COUNT - 1.
 */
typedef enum xxbyte_dotnet_id {
    XXBYTE_DOTNET_ID_INVALID = 0,
    XXBYTE_DOTNET_ID_NOP = 1,
    XXBYTE_DOTNET_ID_BREAK = 2,
    XXBYTE_DOTNET_ID_LDARG_0 = 3,
    XXBYTE_DOTNET_ID_LDARG_1 = 4,
    XXBYTE_DOTNET_ID_LDARG_2 = 5,
    XXBYTE_DOTNET_ID_LDARG_3 = 6,
    XXBYTE_DOTNET_ID_LDLOC_0 = 7,
    XXBYTE_DOTNET_ID_LDLOC_1 = 8,
    XXBYTE_DOTNET_ID_LDLOC_2 = 9,
    XXBYTE_DOTNET_ID_LDLOC_3 = 10,
    XXBYTE_DOTNET_ID_STLOC_0 = 11,
    XXBYTE_DOTNET_ID_STLOC_1 = 12,
    XXBYTE_DOTNET_ID_STLOC_2 = 13,
    XXBYTE_DOTNET_ID_STLOC_3 = 14,
    XXBYTE_DOTNET_ID_LDARG_S = 15,
    XXBYTE_DOTNET_ID_LDARGA_S = 16,
    XXBYTE_DOTNET_ID_STARG_S = 17,
    XXBYTE_DOTNET_ID_LDLOC_S = 18,
    XXBYTE_DOTNET_ID_LDLOCA_S = 19,
    XXBYTE_DOTNET_ID_STLOC_S = 20,
    XXBYTE_DOTNET_ID_LDNULL = 21,
    XXBYTE_DOTNET_ID_LDC_I4_M1 = 22,
    XXBYTE_DOTNET_ID_LDC_I4_0 = 23,
    XXBYTE_DOTNET_ID_LDC_I4_1 = 24,
    XXBYTE_DOTNET_ID_LDC_I4_2 = 25,
    XXBYTE_DOTNET_ID_LDC_I4_3 = 26,
    XXBYTE_DOTNET_ID_LDC_I4_4 = 27,
    XXBYTE_DOTNET_ID_LDC_I4_5 = 28,
    XXBYTE_DOTNET_ID_LDC_I4_6 = 29,
    XXBYTE_DOTNET_ID_LDC_I4_7 = 30,
    XXBYTE_DOTNET_ID_LDC_I4_8 = 31,
    XXBYTE_DOTNET_ID_LDC_I4_S = 32,
    XXBYTE_DOTNET_ID_LDC_I4 = 33,
    XXBYTE_DOTNET_ID_LDC_I8 = 34,
    XXBYTE_DOTNET_ID_LDC_R4 = 35,
    XXBYTE_DOTNET_ID_LDC_R8 = 36,
    XXBYTE_DOTNET_ID_DUP = 37,
    XXBYTE_DOTNET_ID_POP = 38,
    XXBYTE_DOTNET_ID_JMP = 39,
    XXBYTE_DOTNET_ID_CALL = 40,
    XXBYTE_DOTNET_ID_CALLI = 41,
    XXBYTE_DOTNET_ID_RET = 42,
    XXBYTE_DOTNET_ID_BR_S = 43,
    XXBYTE_DOTNET_ID_BRFALSE_S = 44,
    XXBYTE_DOTNET_ID_BRTRUE_S = 45,
    XXBYTE_DOTNET_ID_BEQ_S = 46,
    XXBYTE_DOTNET_ID_BGE_S = 47,
    XXBYTE_DOTNET_ID_BGT_S = 48,
    XXBYTE_DOTNET_ID_BLE_S = 49,
    XXBYTE_DOTNET_ID_BLT_S = 50,
    XXBYTE_DOTNET_ID_BNE_UN_S = 51,
    XXBYTE_DOTNET_ID_BGE_UN_S = 52,
    XXBYTE_DOTNET_ID_BGT_UN_S = 53,
    XXBYTE_DOTNET_ID_BLE_UN_S = 54,
    XXBYTE_DOTNET_ID_BLT_UN_S = 55,
    XXBYTE_DOTNET_ID_BR = 56,
    XXBYTE_DOTNET_ID_BRFALSE = 57,
    XXBYTE_DOTNET_ID_BRTRUE = 58,
    XXBYTE_DOTNET_ID_BEQ = 59,
    XXBYTE_DOTNET_ID_BGE = 60,
    XXBYTE_DOTNET_ID_BGT = 61,
    XXBYTE_DOTNET_ID_BLE = 62,
    XXBYTE_DOTNET_ID_BLT = 63,
    XXBYTE_DOTNET_ID_BNE_UN = 64,
    XXBYTE_DOTNET_ID_BGE_UN = 65,
    XXBYTE_DOTNET_ID_BGT_UN = 66,
    XXBYTE_DOTNET_ID_BLE_UN = 67,
    XXBYTE_DOTNET_ID_BLT_UN = 68,
    XXBYTE_DOTNET_ID_SWITCH = 69,
    XXBYTE_DOTNET_ID_LDIND_I1 = 70,
    XXBYTE_DOTNET_ID_LDIND_U1 = 71,
    XXBYTE_DOTNET_ID_LDIND_I2 = 72,
    XXBYTE_DOTNET_ID_LDIND_U2 = 73,
    XXBYTE_DOTNET_ID_LDIND_I4 = 74,
    XXBYTE_DOTNET_ID_LDIND_U4 = 75,
    XXBYTE_DOTNET_ID_LDIND_I8 = 76,
    XXBYTE_DOTNET_ID_LDIND_I = 77,
    XXBYTE_DOTNET_ID_LDIND_R4 = 78,
    XXBYTE_DOTNET_ID_LDIND_R8 = 79,
    XXBYTE_DOTNET_ID_LDIND_REF = 80,
    XXBYTE_DOTNET_ID_STIND_REF = 81,
    XXBYTE_DOTNET_ID_STIND_I1 = 82,
    XXBYTE_DOTNET_ID_STIND_I2 = 83,
    XXBYTE_DOTNET_ID_STIND_I4 = 84,
    XXBYTE_DOTNET_ID_STIND_I8 = 85,
    XXBYTE_DOTNET_ID_STIND_R4 = 86,
    XXBYTE_DOTNET_ID_STIND_R8 = 87,
    XXBYTE_DOTNET_ID_ADD = 88,
    XXBYTE_DOTNET_ID_SUB = 89,
    XXBYTE_DOTNET_ID_MUL = 90,
    XXBYTE_DOTNET_ID_DIV = 91,
    XXBYTE_DOTNET_ID_DIV_UN = 92,
    XXBYTE_DOTNET_ID_REM = 93,
    XXBYTE_DOTNET_ID_REM_UN = 94,
    XXBYTE_DOTNET_ID_AND = 95,
    XXBYTE_DOTNET_ID_OR = 96,
    XXBYTE_DOTNET_ID_XOR = 97,
    XXBYTE_DOTNET_ID_SHL = 98,
    XXBYTE_DOTNET_ID_SHR = 99,
    XXBYTE_DOTNET_ID_SHR_UN = 100,
    XXBYTE_DOTNET_ID_NEG = 101,
    XXBYTE_DOTNET_ID_NOT = 102,
    XXBYTE_DOTNET_ID_CONV_I1 = 103,
    XXBYTE_DOTNET_ID_CONV_I2 = 104,
    XXBYTE_DOTNET_ID_CONV_I4 = 105,
    XXBYTE_DOTNET_ID_CONV_I8 = 106,
    XXBYTE_DOTNET_ID_CONV_R4 = 107,
    XXBYTE_DOTNET_ID_CONV_R8 = 108,
    XXBYTE_DOTNET_ID_CONV_U4 = 109,
    XXBYTE_DOTNET_ID_CONV_U8 = 110,
    XXBYTE_DOTNET_ID_CALLVIRT = 111,
    XXBYTE_DOTNET_ID_CPOBJ = 112,
    XXBYTE_DOTNET_ID_LDOBJ = 113,
    XXBYTE_DOTNET_ID_LDSTR = 114,
    XXBYTE_DOTNET_ID_NEWOBJ = 115,
    XXBYTE_DOTNET_ID_CASTCLASS = 116,
    XXBYTE_DOTNET_ID_ISINST = 117,
    XXBYTE_DOTNET_ID_CONV_R_UN = 118,
    XXBYTE_DOTNET_ID_UNBOX = 119,
    XXBYTE_DOTNET_ID_THROW = 120,
    XXBYTE_DOTNET_ID_LDFLD = 121,
    XXBYTE_DOTNET_ID_LDFLDA = 122,
    XXBYTE_DOTNET_ID_STFLD = 123,
    XXBYTE_DOTNET_ID_LDSFLD = 124,
    XXBYTE_DOTNET_ID_LDSFLDA = 125,
    XXBYTE_DOTNET_ID_STSFLD = 126,
    XXBYTE_DOTNET_ID_STOBJ = 127,
    XXBYTE_DOTNET_ID_CONV_OVF_I1_UN = 128,
    XXBYTE_DOTNET_ID_CONV_OVF_I2_UN = 129,
    XXBYTE_DOTNET_ID_CONV_OVF_I4_UN = 130,
    XXBYTE_DOTNET_ID_CONV_OVF_I8_UN = 131,
    XXBYTE_DOTNET_ID_CONV_OVF_U1_UN = 132,
    XXBYTE_DOTNET_ID_CONV_OVF_U2_UN = 133,
    XXBYTE_DOTNET_ID_CONV_OVF_U4_UN = 134,
    XXBYTE_DOTNET_ID_CONV_OVF_U8_UN = 135,
    XXBYTE_DOTNET_ID_CONV_OVF_I_UN = 136,
    XXBYTE_DOTNET_ID_CONV_OVF_U_UN = 137,
    XXBYTE_DOTNET_ID_BOX = 138,
    XXBYTE_DOTNET_ID_NEWARR = 139,
    XXBYTE_DOTNET_ID_LDLEN = 140,
    XXBYTE_DOTNET_ID_LDELEMA = 141,
    XXBYTE_DOTNET_ID_LDELEM_I1 = 142,
    XXBYTE_DOTNET_ID_LDELEM_U1 = 143,
    XXBYTE_DOTNET_ID_LDELEM_I2 = 144,
    XXBYTE_DOTNET_ID_LDELEM_U2 = 145,
    XXBYTE_DOTNET_ID_LDELEM_I4 = 146,
    XXBYTE_DOTNET_ID_LDELEM_U4 = 147,
    XXBYTE_DOTNET_ID_LDELEM_I8 = 148,
    XXBYTE_DOTNET_ID_LDELEM_I = 149,
    XXBYTE_DOTNET_ID_LDELEM_R4 = 150,
    XXBYTE_DOTNET_ID_LDELEM_R8 = 151,
    XXBYTE_DOTNET_ID_LDELEM_REF = 152,
    XXBYTE_DOTNET_ID_STELEM_I = 153,
    XXBYTE_DOTNET_ID_STELEM_I1 = 154,
    XXBYTE_DOTNET_ID_STELEM_I2 = 155,
    XXBYTE_DOTNET_ID_STELEM_I4 = 156,
    XXBYTE_DOTNET_ID_STELEM_I8 = 157,
    XXBYTE_DOTNET_ID_STELEM_R4 = 158,
    XXBYTE_DOTNET_ID_STELEM_R8 = 159,
    XXBYTE_DOTNET_ID_STELEM_REF = 160,
    XXBYTE_DOTNET_ID_LDELEM = 161,
    XXBYTE_DOTNET_ID_STELEM = 162,
    XXBYTE_DOTNET_ID_UNBOX_ANY = 163,
    XXBYTE_DOTNET_ID_CONV_OVF_I1 = 164,
    XXBYTE_DOTNET_ID_CONV_OVF_U1 = 165,
    XXBYTE_DOTNET_ID_CONV_OVF_I2 = 166,
    XXBYTE_DOTNET_ID_CONV_OVF_U2 = 167,
    XXBYTE_DOTNET_ID_CONV_OVF_I4 = 168,
    XXBYTE_DOTNET_ID_CONV_OVF_U4 = 169,
    XXBYTE_DOTNET_ID_CONV_OVF_I8 = 170,
    XXBYTE_DOTNET_ID_CONV_OVF_U8 = 171,
    XXBYTE_DOTNET_ID_REFANYVAL = 172,
    XXBYTE_DOTNET_ID_CKFINITE = 173,
    XXBYTE_DOTNET_ID_MKREFANY = 174,
    XXBYTE_DOTNET_ID_LDTOKEN = 175,
    XXBYTE_DOTNET_ID_CONV_U2 = 176,
    XXBYTE_DOTNET_ID_CONV_U1 = 177,
    XXBYTE_DOTNET_ID_CONV_I = 178,
    XXBYTE_DOTNET_ID_CONV_OVF_I = 179,
    XXBYTE_DOTNET_ID_CONV_OVF_U = 180,
    XXBYTE_DOTNET_ID_ADD_OVF = 181,
    XXBYTE_DOTNET_ID_ADD_OVF_UN = 182,
    XXBYTE_DOTNET_ID_MUL_OVF = 183,
    XXBYTE_DOTNET_ID_MUL_OVF_UN = 184,
    XXBYTE_DOTNET_ID_SUB_OVF = 185,
    XXBYTE_DOTNET_ID_SUB_OVF_UN = 186,
    XXBYTE_DOTNET_ID_ENDFINALLY = 187,
    XXBYTE_DOTNET_ID_LEAVE = 188,
    XXBYTE_DOTNET_ID_LEAVE_S = 189,
    XXBYTE_DOTNET_ID_STIND_I = 190,
    XXBYTE_DOTNET_ID_CONV_U = 191,
    XXBYTE_DOTNET_ID_ARGLIST = 192,
    XXBYTE_DOTNET_ID_CEQ = 193,
    XXBYTE_DOTNET_ID_CGT = 194,
    XXBYTE_DOTNET_ID_CGT_UN = 195,
    XXBYTE_DOTNET_ID_CLT = 196,
    XXBYTE_DOTNET_ID_CLT_UN = 197,
    XXBYTE_DOTNET_ID_LDFTN = 198,
    XXBYTE_DOTNET_ID_LDVIRTFTN = 199,
    XXBYTE_DOTNET_ID_LDARG = 200,
    XXBYTE_DOTNET_ID_LDARGA = 201,
    XXBYTE_DOTNET_ID_STARG = 202,
    XXBYTE_DOTNET_ID_LDLOC = 203,
    XXBYTE_DOTNET_ID_LDLOCA = 204,
    XXBYTE_DOTNET_ID_STLOC = 205,
    XXBYTE_DOTNET_ID_LOCALLOC = 206,
    XXBYTE_DOTNET_ID_ENDFILTER = 207,
    XXBYTE_DOTNET_ID_UNALIGNED_ = 208,
    XXBYTE_DOTNET_ID_VOLATILE_ = 209,
    XXBYTE_DOTNET_ID_TAIL_ = 210,
    XXBYTE_DOTNET_ID_INITOBJ = 211,
    XXBYTE_DOTNET_ID_CONSTRAINED_ = 212,
    XXBYTE_DOTNET_ID_CPBLK = 213,
    XXBYTE_DOTNET_ID_INITBLK = 214,
    XXBYTE_DOTNET_ID_NO_ = 215,
    XXBYTE_DOTNET_ID_RETHROW = 216,
    XXBYTE_DOTNET_ID_SIZEOF = 217,
    XXBYTE_DOTNET_ID_REFANYTYPE = 218,
    XXBYTE_DOTNET_ID_READONLY_ = 219,
    XXBYTE_DOTNET_ID_COUNT = 220
} xxbyte_dotnet_id;

/* Operand encodings (ECMA-335 III.1.9, System.Reflection.Emit.OperandType). */
typedef enum xxbyte_dotnet_operand_type {
    XXBYTE_DOTNET_OPERAND_INVALID = 0, /* not a valid id */
    XXBYTE_DOTNET_OPERAND_INLINE_NONE,
    XXBYTE_DOTNET_OPERAND_SHORT_INLINE_BR_TARGET, /* int8 branch displacement */
    XXBYTE_DOTNET_OPERAND_INLINE_BR_TARGET,       /* int32 branch displacement */
    XXBYTE_DOTNET_OPERAND_SHORT_INLINE_I,         /* int8 (uint8 for unaligned. and no.) */
    XXBYTE_DOTNET_OPERAND_INLINE_I,               /* int32 */
    XXBYTE_DOTNET_OPERAND_INLINE_I8,              /* int64 */
    XXBYTE_DOTNET_OPERAND_SHORT_INLINE_R,         /* float32 */
    XXBYTE_DOTNET_OPERAND_INLINE_R,               /* float64 */
    XXBYTE_DOTNET_OPERAND_SHORT_INLINE_VAR,       /* uint8 argument or local */
    XXBYTE_DOTNET_OPERAND_INLINE_VAR,             /* uint16 argument or local */
    XXBYTE_DOTNET_OPERAND_INLINE_METHOD,          /* MethodDef, MemberRef or MethodSpec token */
    XXBYTE_DOTNET_OPERAND_INLINE_FIELD,           /* FieldDef or MemberRef token */
    XXBYTE_DOTNET_OPERAND_INLINE_TYPE,            /* TypeDef, TypeRef or TypeSpec token */
    XXBYTE_DOTNET_OPERAND_INLINE_TOK,             /* any of the above */
    XXBYTE_DOTNET_OPERAND_INLINE_SIG,             /* StandAloneSig token */
    XXBYTE_DOTNET_OPERAND_INLINE_STRING,          /* user-string token (0x70xxxxxx) */
    XXBYTE_DOTNET_OPERAND_INLINE_SWITCH           /* uint32 count + int32 targets */
} xxbyte_dotnet_operand_type;

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_dotnet_mnemonic(uint32_t id);

/* The operand encoding of id (xxbyte_dotnet_operand_type). */
XXBYTE_API xxbyte_dotnet_operand_type xxbyte_dotnet_operand_type_of(uint32_t id);

/* Method body header flags (CorILMethod_*). */
#define XXBYTE_DOTNET_METHOD_FORMAT_MASK 0x0003u
#define XXBYTE_DOTNET_METHOD_TINY_FORMAT 0x0002u
#define XXBYTE_DOTNET_METHOD_FAT_FORMAT 0x0003u
#define XXBYTE_DOTNET_METHOD_MORE_SECTS 0x0008u
#define XXBYTE_DOTNET_METHOD_INIT_LOCALS 0x0010u

/* A parsed method body header (ECMA-335 II.25.4). */
typedef struct xxbyte_dotnet_method_header {
    uint32_t header_size; /* bytes before the first IL byte */
    uint32_t code_size;   /* IL bytes */
    uint32_t max_stack;
    uint32_t local_var_sig_token; /* 0 for a tiny header */
    uint32_t flags;               /* raw CorILMethod flags */
    int is_fat;
    int has_more_sections; /* extra data sections follow the IL */
} xxbyte_dotnet_method_header;

/*
 * Parses the tiny or fat header at data[0] (the method's RVA). A tiny header
 * is one byte (format bits 0x2, code size in the upper six bits, max_stack
 * 8); a fat header is twelve bytes (format bits 0x3, 12 flag bits, a size
 * nibble of 3 dwords, max_stack, code_size and local_var_sig_token). Returns
 * XXBYTE_STATUS_TRUNCATED when the header or the IL runs past size and
 * XXBYTE_STATUS_INVALID_INSTRUCTION for an unknown format or header size.
 */
XXBYTE_API xxbyte_status xxbyte_dotnet_parse_method_header(const uint8_t *data, size_t size, xxbyte_dotnet_method_header *header);

/* Extra data section kinds (CorILMethod_Sect_*). */
#define XXBYTE_DOTNET_SECT_EH_TABLE 0x01u
#define XXBYTE_DOTNET_SECT_OPT_IL_TABLE 0x02u
#define XXBYTE_DOTNET_SECT_KIND_MASK 0x3Fu
#define XXBYTE_DOTNET_SECT_FAT_FORMAT 0x40u
#define XXBYTE_DOTNET_SECT_MORE_SECTS 0x80u

/* Exception clause kinds (COR_ILEXCEPTION_CLAUSE_*). */
#define XXBYTE_DOTNET_CLAUSE_EXCEPTION 0x0000u /* typed catch */
#define XXBYTE_DOTNET_CLAUSE_FILTER 0x0001u
#define XXBYTE_DOTNET_CLAUSE_FINALLY 0x0002u
#define XXBYTE_DOTNET_CLAUSE_FAULT 0x0004u

/* One exception handling clause (ECMA-335 II.25.4.6). Offsets are IL byte
 * offsets from the first IL byte. */
typedef struct xxbyte_dotnet_exception_clause {
    uint32_t flags; /* XXBYTE_DOTNET_CLAUSE_* */
    uint32_t try_offset;
    uint32_t try_length;
    uint32_t handler_offset;
    uint32_t handler_length;
    /* The catch type token for XXBYTE_DOTNET_CLAUSE_EXCEPTION, the filter's
     * IL offset for XXBYTE_DOTNET_CLAUSE_FILTER, otherwise as encoded. */
    uint32_t class_token_or_filter_offset;
} xxbyte_dotnet_exception_clause;

/*
 * Reads the exception clauses of the method body at method[0], whose header
 * was parsed into *header from the same bytes. The data sections start at
 * the first 4-byte boundary after the IL; small (12-byte clauses) and fat
 * (24-byte clauses) EH tables are read from every section of the
 * MoreSects chain, other section kinds are skipped. *count receives the
 * total number of clauses and the first min(capacity, total) are stored in
 * clauses, which may be NULL when capacity is 0. A method without extra
 * sections has no clauses. Returns XXBYTE_STATUS_TRUNCATED when a section
 * runs past size and XXBYTE_STATUS_INVALID_INSTRUCTION when a section's
 * DataSize is smaller than its 4-byte header.
 */
XXBYTE_API xxbyte_status xxbyte_dotnet_parse_exception_clauses(const uint8_t *method, size_t size, const xxbyte_dotnet_method_header *header,
                                                               xxbyte_dotnet_exception_clause *clauses, size_t capacity, size_t *count);

/* ---------------------------------------------------------------------- */
/* Assembly images: PE files and ECMA-335 metadata (Partition II)          */
/* ---------------------------------------------------------------------- */

/* Metadata tables (ECMA-335 II.22), by table number. */
typedef enum xxbyte_dotnet_table {
    XXBYTE_DOTNET_TABLE_MODULE = 0x00,
    XXBYTE_DOTNET_TABLE_TYPEREF = 0x01,
    XXBYTE_DOTNET_TABLE_TYPEDEF = 0x02,
    XXBYTE_DOTNET_TABLE_FIELDPTR = 0x03, /* uncompressed (#-) tables only */
    XXBYTE_DOTNET_TABLE_FIELD = 0x04,
    XXBYTE_DOTNET_TABLE_METHODPTR = 0x05, /* uncompressed (#-) tables only */
    XXBYTE_DOTNET_TABLE_METHODDEF = 0x06,
    XXBYTE_DOTNET_TABLE_PARAMPTR = 0x07, /* uncompressed (#-) tables only */
    XXBYTE_DOTNET_TABLE_PARAM = 0x08,
    XXBYTE_DOTNET_TABLE_INTERFACEIMPL = 0x09,
    XXBYTE_DOTNET_TABLE_MEMBERREF = 0x0A,
    XXBYTE_DOTNET_TABLE_CONSTANT = 0x0B,
    XXBYTE_DOTNET_TABLE_CUSTOMATTRIBUTE = 0x0C,
    XXBYTE_DOTNET_TABLE_FIELDMARSHAL = 0x0D,
    XXBYTE_DOTNET_TABLE_DECLSECURITY = 0x0E,
    XXBYTE_DOTNET_TABLE_CLASSLAYOUT = 0x0F,
    XXBYTE_DOTNET_TABLE_FIELDLAYOUT = 0x10,
    XXBYTE_DOTNET_TABLE_STANDALONESIG = 0x11,
    XXBYTE_DOTNET_TABLE_EVENTMAP = 0x12,
    XXBYTE_DOTNET_TABLE_EVENTPTR = 0x13, /* uncompressed (#-) tables only */
    XXBYTE_DOTNET_TABLE_EVENT = 0x14,
    XXBYTE_DOTNET_TABLE_PROPERTYMAP = 0x15,
    XXBYTE_DOTNET_TABLE_PROPERTYPTR = 0x16, /* uncompressed (#-) tables only */
    XXBYTE_DOTNET_TABLE_PROPERTY = 0x17,
    XXBYTE_DOTNET_TABLE_METHODSEMANTICS = 0x18,
    XXBYTE_DOTNET_TABLE_METHODIMPL = 0x19,
    XXBYTE_DOTNET_TABLE_MODULEREF = 0x1A,
    XXBYTE_DOTNET_TABLE_TYPESPEC = 0x1B,
    XXBYTE_DOTNET_TABLE_IMPLMAP = 0x1C,
    XXBYTE_DOTNET_TABLE_FIELDRVA = 0x1D,
    XXBYTE_DOTNET_TABLE_ENCLOG = 0x1E,
    XXBYTE_DOTNET_TABLE_ENCMAP = 0x1F,
    XXBYTE_DOTNET_TABLE_ASSEMBLY = 0x20,
    XXBYTE_DOTNET_TABLE_ASSEMBLYPROCESSOR = 0x21,
    XXBYTE_DOTNET_TABLE_ASSEMBLYOS = 0x22,
    XXBYTE_DOTNET_TABLE_ASSEMBLYREF = 0x23,
    XXBYTE_DOTNET_TABLE_ASSEMBLYREFPROCESSOR = 0x24,
    XXBYTE_DOTNET_TABLE_ASSEMBLYREFOS = 0x25,
    XXBYTE_DOTNET_TABLE_FILE = 0x26,
    XXBYTE_DOTNET_TABLE_EXPORTEDTYPE = 0x27,
    XXBYTE_DOTNET_TABLE_MANIFESTRESOURCE = 0x28,
    XXBYTE_DOTNET_TABLE_NESTEDCLASS = 0x29,
    XXBYTE_DOTNET_TABLE_GENERICPARAM = 0x2A,
    XXBYTE_DOTNET_TABLE_METHODSPEC = 0x2B,
    XXBYTE_DOTNET_TABLE_GENERICPARAMCONSTRAINT = 0x2C
} xxbyte_dotnet_table;

/* Tables 0x00 to 0x2C have a known layout. The Valid mask has 64 bits. */
#define XXBYTE_DOTNET_TABLE_KNOWN 0x2Du
#define XXBYTE_DOTNET_TABLE_MAX 64u

/* Metadata tokens: the table number in the top byte, the 1-based row below. */
#define XXBYTE_DOTNET_TOKEN(table, row) ((((uint32_t)(table)) << 24) | ((uint32_t)(row) & 0x00FFFFFFu))
#define XXBYTE_DOTNET_TOKEN_TABLE(token) ((uint32_t)(token) >> 24)
#define XXBYTE_DOTNET_TOKEN_ROW(token) ((uint32_t)(token) & 0x00FFFFFFu)
/* ldstr tokens: 0x70 and a #US heap offset. */
#define XXBYTE_DOTNET_TOKEN_USER_STRING 0x70u
/* The table byte xxbyte_dotnet_read_row gives a coded index whose tag names
 * no table (for example tag 0 of CustomAttributeType). */
#define XXBYTE_DOTNET_TOKEN_BAD_TAG 0xFFu

/* A metadata stream: file offset (from data[0]) and size; 0, 0 if absent. */
typedef struct xxbyte_dotnet_stream {
    uint32_t offset;
    uint32_t size;
} xxbyte_dotnet_stream;

/*
 * An opened image. Every field is filled by xxbyte_dotnet_open_image (or,
 * without the PE fields, xxbyte_dotnet_open_metadata), which checks every
 * offset and size against the input once, so the accessors below never read
 * out of bounds. The image borrows data; keep it alive while using it.
 */
typedef struct xxbyte_dotnet_image {
    const uint8_t *data;
    size_t size;
    /* PE (II.25); all zero for raw metadata. */
    int is_pe;
    int pe32_plus;    /* optional header magic 0x20B */
    uint16_t machine; /* COFF Machine */
    uint16_t characteristics;
    uint32_t section_table; /* file offset of the first section header */
    uint32_t section_count;
    uint32_t cli_header;    /* file offset of the CLI header (II.25.3.3) */
    uint16_t runtime_major; /* MajorRuntimeVersion */
    uint16_t runtime_minor;
    uint32_t cli_flags;         /* COMIMAGE_FLAGS_* */
    uint32_t entry_point_token; /* EntryPointToken (or native entry RVA) */
    uint32_t metadata_rva;
    /* Metadata root (II.24.2.1). */
    uint32_t metadata_offset; /* file offset of the BSJB signature */
    uint32_t metadata_size;
    uint16_t metadata_major;
    uint16_t metadata_minor;
    uint32_t version_offset; /* file offset of the version string */
    uint32_t version_length; /* without the NUL padding */
    /* Streams (II.24.2.2). Where a name occurs twice the last one is used,
     * as the mono and CLR loaders do. */
    xxbyte_dotnet_stream tables;       /* "#~" or "#-" */
    xxbyte_dotnet_stream strings;      /* "#Strings" */
    xxbyte_dotnet_stream user_strings; /* "#US" */
    xxbyte_dotnet_stream blob;         /* "#Blob" */
    xxbyte_dotnet_stream guid;         /* "#GUID" */
    int uncompressed;                  /* the tables stream is "#-" */
    /* Tables stream header (II.24.2.6). */
    uint8_t tables_major;
    uint8_t tables_minor;
    uint8_t heap_sizes; /* 0x01 #Strings, 0x02 #GUID, 0x04 #Blob: 4-byte indexes */
    uint64_t valid;     /* bit n: table n is present */
    uint64_t sorted;
    uint32_t rows[XXBYTE_DOTNET_TABLE_MAX];
    /* Row size and file offset of each known table; 0 for tables that are
     * absent or have no known layout (bits 0x2D to 0x3F of Valid). */
    uint32_t row_size[XXBYTE_DOTNET_TABLE_MAX];
    uint32_t table_offset[XXBYTE_DOTNET_TABLE_MAX];
} xxbyte_dotnet_image;

/*
 * Opens a PE32 or PE32+ file in data[0..size): the DOS and PE headers, the
 * section table, the CLI header (data directory 14), the metadata root, its
 * streams and the tables stream (#~ or #-, with the heap-size flags, the 4
 * extra bytes after the row counts that heap flag 0x40 announces, as the
 * CLR reads them, and every row size of II.22 including all coded indexes;
 * the Ptr tables of an uncompressed stream are supported). Returns
 * XXBYTE_STATUS_INVALID_INSTRUCTION when data is not a PE file with CLI
 * metadata or a header field is out of range, XXBYTE_STATUS_TRUNCATED when a
 * header, a stream or a table runs past the end of its section or of data,
 * and XXBYTE_STATUS_INVALID_ARGUMENT for NULL pointers. On failure *image is
 * zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_dotnet_open_image(const uint8_t *data, size_t size, xxbyte_dotnet_image *image);

/* Opens bare metadata whose root (the "BSJB" signature) is at data[0], as
 * stored in a PE file or in a portable PDB. RVAs then map to nothing. */
XXBYTE_API xxbyte_status xxbyte_dotnet_open_metadata(const uint8_t *data, size_t size, xxbyte_dotnet_image *image);

/* Maps an RVA to a file offset through the section table: the section
 * whose [VirtualAddress, VirtualAddress + SizeOfRawData) holds rva, as mono
 * maps it. Returns 0 and leaves *offset alone when no section holds it or
 * the offset lies past the end of data; *available (may be NULL) receives
 * the bytes from the offset to the end of the section's raw data, clipped
 * to the end of data. */
XXBYTE_API int xxbyte_dotnet_rva_to_offset(const xxbyte_dotnet_image *image, uint32_t rva, size_t *offset, size_t *available);

/*
 * Heap readers. Each returns XXBYTE_STATUS_OK and a pointer into data, or
 * XXBYTE_STATUS_TRUNCATED when the index or the item lies outside its heap
 * (XXBYTE_STATUS_INVALID_INSTRUCTION for a bad blob length prefix).
 * - #Strings: the UTF-8 string at index; *length excludes the NUL. A string
 *   with no NUL before the end of the heap ends at the heap's end.
 * - #Blob and #US: the item at index after its compressed length; for #US
 *   *length counts the UTF-16LE bytes and excludes the trailing flag byte.
 * - #GUID: the 16 bytes of 1-based GUID index (0 is the null GUID: TRUNCATED).
 */
XXBYTE_API xxbyte_status xxbyte_dotnet_get_string(const xxbyte_dotnet_image *image, uint32_t index, const char **text, size_t *length);
XXBYTE_API xxbyte_status xxbyte_dotnet_get_blob(const xxbyte_dotnet_image *image, uint32_t index, const uint8_t **blob, size_t *length);
XXBYTE_API xxbyte_status xxbyte_dotnet_get_user_string(const xxbyte_dotnet_image *image, uint32_t index, const uint8_t **utf16, size_t *length);
XXBYTE_API xxbyte_status xxbyte_dotnet_get_guid(const xxbyte_dotnet_image *image, uint32_t index, const uint8_t **guid);

/*
 * Compressed integers of signatures and blob heads (II.23.2): one byte
 * 0xxxxxxx, two bytes 10xxxxxx, four bytes 110xxxxx, big-endian. Reads at
 * data[*offset] and advances it. The signed form rotates the sign bit into
 * bit 0 (II.23.2: 1 byte -2^6..2^6-1, 2 bytes -2^13.., 4 bytes -2^28..).
 * Returns XXBYTE_STATUS_TRUNCATED past size and
 * XXBYTE_STATUS_INVALID_INSTRUCTION for a first byte 111xxxxx.
 */
XXBYTE_API xxbyte_status xxbyte_dotnet_read_compressed(const uint8_t *data, size_t size, size_t *offset, uint32_t *value);
XXBYTE_API xxbyte_status xxbyte_dotnet_read_compressed_signed(const uint8_t *data, size_t size, size_t *offset, int32_t *value);

/*
 * Table rows. xxbyte_dotnet_column_count gives the number of columns of a
 * known table (0 otherwise); xxbyte_dotnet_read_row reads row (1-based) of
 * table into values[0..min(capacity, columns)) and sets *count (may be
 * NULL) to the column count. Columns are in II.22 order: constants as read
 * (Constant.Type and its padding byte are two columns), heap indexes as
 * offsets, simple table indexes as row numbers, and coded indexes as
 * tokens (XXBYTE_DOTNET_TOKEN of the tagged table and row; a null index is
 * the tagged table with row 0, an undefined tag has table
 * XXBYTE_DOTNET_TOKEN_BAD_TAG). Returns XXBYTE_STATUS_INVALID_ARGUMENT for
 * an unknown table and XXBYTE_STATUS_TRUNCATED when row is 0 or past the
 * table.
 */
XXBYTE_API uint32_t xxbyte_dotnet_column_count(xxbyte_dotnet_table table);
XXBYTE_API xxbyte_status xxbyte_dotnet_read_row(const xxbyte_dotnet_image *image, xxbyte_dotnet_table table, uint32_t row, uint32_t *values, size_t capacity,
                                                size_t *count);

/* A MethodDef row with its body. */
typedef struct xxbyte_dotnet_method {
    uint32_t token;      /* 0x06xxxxxx */
    uint32_t type_token; /* the owning TypeDef (0x02xxxxxx), 0 if none */
    uint32_t rva;
    uint16_t impl_flags; /* MethodImplAttributes */
    uint16_t flags;      /* MethodAttributes */
    uint32_t name;       /* #Strings index */
    uint32_t signature;  /* #Blob index */
    uint32_t param_list; /* first Param (or ParamPtr) row */
    /* XXBYTE_STATUS_OK when the method has an IL body that parsed,
     * XXBYTE_STATUS_END_OF_INPUT when it has none (RVA 0, abstract,
     * pinvokeimpl, runtime or native code), XXBYTE_STATUS_TRUNCATED when the
     * RVA maps to nothing or the body runs past its section, and the
     * statuses of xxbyte_dotnet_parse_method_header otherwise. */
    xxbyte_status body_status;
    size_t body_offset; /* file offset of the method header */
    size_t body_size;   /* header, IL and extra data sections */
    size_t available;   /* bytes from body_offset to the end of the section */
    xxbyte_dotnet_method_header header;
} xxbyte_dotnet_method;

/*
 * Reads MethodDef row (1-based) and its body header. The owner is found
 * from the TypeDef MethodList ranges (by binary search; through MethodPtr
 * when an uncompressed stream has one, by a linear scan). The IL starts at
 * data[body_offset + header.header_size]; pass data + body_offset and
 * available to xxbyte_dotnet_parse_exception_clauses. Returns
 * XXBYTE_STATUS_TRUNCATED when row is 0 or past the table; a bad body is
 * reported in body_status only.
 */
XXBYTE_API xxbyte_status xxbyte_dotnet_get_method(const xxbyte_dotnet_image *image, uint32_t row, xxbyte_dotnet_method *method);

/* ---------------------------------------------------------------------- */
/* Names                                                                   */
/* ---------------------------------------------------------------------- */

/*
 * Name tables built from an image: the enclosing type of every nested
 * TypeDef (NestedClass) and the owning TypeDef of every MethodDef and
 * Field row (MethodList and FieldList ranges, through MethodPtr and
 * FieldPtr when present). xxbyte_dotnet_names_init allocates them with
 * xx_mem_alloc; xxbyte_dotnet_names_free releases them. The image (and its
 * data) must outlive the names.
 */
typedef struct xxbyte_dotnet_names {
    const xxbyte_dotnet_image *image;
    uint32_t *enclosing;    /* [TypeDef row] enclosing TypeDef row, 0 if none */
    uint32_t *method_owner; /* [MethodDef row] TypeDef row, 0 if none */
    uint32_t *field_owner;  /* [Field row] TypeDef row, 0 if none */
} xxbyte_dotnet_names;

XXBYTE_API xxbyte_status xxbyte_dotnet_names_init(xxbyte_dotnet_names *names, const xxbyte_dotnet_image *image);
XXBYTE_API void xxbyte_dotnet_names_free(xxbyte_dotnet_names *names);

/*
 * Writes the text of token into buffer, NUL-terminated and cut to
 * buffer_size - 1 characters, and returns its full length, snprintf style
 * (a result >= buffer_size means the text was cut; text longer than
 * XXBYTE_RESOLVER_NAME_LIMIT reports the cut length); returns 0 when the
 * token does not resolve (row 0 or out of range,
 * a table without a name, or a malformed signature or heap index). The text
 * is the operand form of ildasm:
 * - TypeDef: "Ns.Outer/Inner"; TypeRef: "[Assembly]Ns.Name" (ResolutionScope
 *   AssemblyRef), "[.module Name]Ns.Name" (ModuleRef), the enclosing
 *   TypeRef then "/Name" (TypeRef), no scope for Module or null; TypeSpec:
 *   the type of its signature ("int32[]", "!0",
 *   "class [mscorlib]System.Collections.Generic.List`1<int32>").
 * - Field: "type Parent::name"; MethodDef and MemberRef:
 *   "[instance] [explicit] [vararg|unmanaged cdecl|...] ret Parent::name(params)",
 *   a vararg call site's extra arguments after "...": the parent of a
 *   TypeDef or TypeRef is its name, of a TypeSpec its signature type, and
 *   members of <Module> (TypeDef 1, global functions and fields) have no
 *   parent ("void Helper(int32)"), as in ildasm and monodis
 *   ("instance void class [mscorlib]System.Collections.Generic.List`1<int32>::Add(!0)").
 * - MethodSpec: the method with "<args>" after its name; a MemberRef to a
 *   generic method without instantiation shows its arity,
 *   "Contains<[1]>(...)", as monodis does (a MethodDef does not).
 * - StandAloneSig: a method signature as calli prints it, "void(int32)", or
 *   a local signature "(int32, string)".
 * - User string (0x70): the string in double quotes with C# escapes
 *   (\" \\ \0 \a \b \f \n \r \t \v, \uXXXX for every other UTF-16 unit
 *   outside 0x20..0x7E).
 * - ModuleRef: "[.module name]"; AssemblyRef: "[name]".
 * Signature types print as in ildasm: void bool char int8 uint8 int16 uint16
 * int32 uint32 int64 uint64 float32 float64 string object typedref
 * "native int" "native uint", "class X", "valuetype X", "T[]", "T*", "T&",
 * "T[0...2,0...]" for ARRAY bounds, "T pinned", "T modreq(X)",
 * "T modopt(X)", "method ret *(params)" for FNPTR, "!0" and "!!0" for
 * generic parameters (by number, never by name), and "class X<int32,string>"
 * for instantiations. Names that are not dotted ILAsm identifiers, or are
 * ILAsm keywords, are quoted: Demo.Program/'<>c'::'<Main>b__0_0', but
 * ".ctor" and ".cctor" are not.
 *
 * Checked against monodis (mono 6.14), whose text differs in form only:
 * monodis writes "class" or "valuetype" before a member's parent, corlib
 * primitives as parents by keyword ("string::Concat" for
 * [mscorlib]System.String::Concat), "unsigned int8" and "native unsigned
 * int", generic parameters by name where it knows the context ("!T"),
 * "int32[,]" for "int32[0...,0...]", "method default ..." for function
 * pointers, quotes on ".ctor", spaces after some commas and before the
 * argument list of generic methods, "[out]" parameter attributes, strings
 * with non-ASCII characters as bytearray(...), and calli operands as
 * "signature-0x11000001", and the C# 9 "unmanaged" function-pointer calling
 * convention (9) as "invalid-flags". It drops the instantiation of a generic type
 * nested in a generic type in field types ("Outer`1/Node[]" where the
 * signature has "Outer`1/Node<!0>[]"); xxbyte prints the signature. For
 * .NET Core assemblies monodis follows type forwarders (it prints
 * [System.Private.CoreLib] where the TypeRef says [System.Runtime]), prints
 * value types as "class" because it cannot load that corlib's ValueType,
 * and drops "Parent::name" from MethodSpecs whose parent it cannot load;
 * xxbyte prints the metadata as stored.
 */
XXBYTE_API size_t xxbyte_dotnet_token_name(const xxbyte_dotnet_names *names, uint32_t token, char *buffer, size_t buffer_size);

/*
 * The listing header text of MethodDef row: "Ns.Type::Name(params)",
 * parameter types only, as xxbyte_disasm_file prints it; a generic method
 * shows its GenericParam names ("Box`1::Map<U>(class [mscorlib]System.Func`2<!0,!!0>)"),
 * a global function its name alone. Same return value as
 * xxbyte_dotnet_token_name.
 */
XXBYTE_API size_t xxbyte_dotnet_method_title(const xxbyte_dotnet_names *names, uint32_t row, char *buffer, size_t buffer_size);

/*
 * A ready-made xxbyte_resolver: context is a const xxbyte_dotnet_names *.
 * It names every displayed TOKEN operand with xxbyte_dotnet_token_name,
 * adding "method " or "field " before a member token of ldtoken (as ildasm
 * does), and leaves branches, switch targets, arguments, locals and tokens
 * that do not resolve numeric. It returns full lengths as
 * xxbyte_dotnet_token_name does, so the formatter retries with a buffer
 * large enough for long names (up to XXBYTE_RESOLVER_NAME_LIMIT).
 */
XXBYTE_API size_t xxbyte_dotnet_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
