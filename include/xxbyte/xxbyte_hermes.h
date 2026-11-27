/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_HERMES_H
#define XXBYTE_XXBYTE_HERMES_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_HERMES
#error "the Hermes (React Native) bytecode decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hermes bytecode (HBC), the bytecode of the Hermes JavaScript engine of
 * React Native: .hbc files and HBC index.android.bundle / main.jsbundle.
 *
 * Modes. A mode is an HBC bytecode version (BYTECODE_VERSION), 59 to 100;
 * any other mode is XXBYTE_STATUS_UNSUPPORTED_MODE. The opcode numbers,
 * operand types and builtin method names of every version are tabulated
 * from the official facebook/hermes BytecodeList.def and Builtins.def of
 * the source that defines it: the newest release tag with that version,
 * or, for versions no release carries, the newest commit with it on the
 * branch that introduced it (main for 59 to 96, static_h for 97 to 100):
 *
 *   59 v0.1.0 (RN 0.60)   60 v0.1.1        62 v0.2.1 (RN 0.61)
 *   71 v0.3.0             74 v0.5.3 (RN 0.62, 0.63; also v0.4.x, v0.6.0)
 *   76 v0.7.2 (RN 0.64)   83 v0.8.0        84 v0.11.0 (RN 0.65 to 0.68)
 *   85 RN 0.69            89 RN 0.70 (v0.12.0)   90 RN 0.71
 *   94 RN 0.72            96 v0.17.0 (RN 0.73 to 0.84, v0.13 to v0.17)
 *   98 Hermes V1 (static_h) releases 250829098.0.x
 *   99 Hermes V1 260318099.0.x
 *   100 static_h 310f6090bcdd (2026-11-20, current development version)
 *   dev-only versions: 61, 63 to 70, 72, 73, 75, 77 to 82, 86 to 88,
 *   91 to 93, 95 (main) and 97 (static_h e5c8ebf2fa69).
 *
 * XXBYTE_HERMES_MODE_DEFAULT is the newest, 100. These versions share an
 * opcode table and decode alike, apart from builtin names, whose list
 * changes on its own: 59/60; 61 to 72; 73 to 79; 81/82; 83/84; 85/86;
 * 87 to 91 and 93; 92/94; 95/96. A file says its version
 * in its header (xxbyte_hermes_parse_header); xxbyte_disasm_file with
 * XXBYTE_MODE_AUTO uses it.
 *
 * Instructions are one opcode byte followed by the operands of its
 * BytecodeList.def entry, little-endian and unaligned: Reg8, UInt8 and
 * Addr8 1 byte, UInt16 2, Reg32, UInt32, Addr32 and Imm32 4, Double 8.
 * Opcodes beyond the version's table are XXBYTE_STATUS_INVALID_INSTRUCTION
 * (so is no other byte: every operand value decodes), and an instruction
 * that runs past code_size is XXBYTE_STATUS_TRUNCATED. Pass the address of
 * the instruction as its offset in the file (or anything congruent to it
 * modulo 4): jump tables are aligned to 4 bytes of the mapped file.
 *
 * Text follows hbcdump -pretty-disassemble / hermesc -dump-bytecode, one
 * space after the mnemonic and ", " between operands:
 *   - registers (Reg8, Reg32) as r<N>;
 *   - UInt8/16/32 in decimal, Imm32 signed decimal, Double as the shortest
 *     round-trip text of xxbyte_fmt_double ("2.5", "1e+16", "nan", "inf";
 *     hbcdump prints JavaScript Number text, "1e+21", "NaN", "Infinity");
 *   - branch targets (Addr8, Addr32, relative to the instruction) as the
 *     absolute address, "0x01b0" (hbcdump prints a label "L3" there);
 *   - string, function and BigInt ids (OPERAND_STRING_ID,
 *     OPERAND_FUNCTION_ID, OPERAND_BIGINT_ID of the newest def that
 *     annotates the opcode, so version 89 CreateClosure is a function id
 *     too) in decimal unless the resolver names them; the container
 *     listing resolves strings to "text" (escaped, not truncated), functions
 *     to Function<name> and BigInts to <decimal>n as hbcdump does;
 *   - the builtin of CallBuiltin, CallBuiltinLong and GetBuiltinClosure as
 *     the quoted name from the version's Builtins.def, "Math.floor"
 *     (decimal when the index is beyond the table).
 *
 * Examples in mode 96 (opcode numbers change between versions):
 *   34 06 00 00 00              DeclareGlobalVar 6
 *   6E 01 01                    LoadConstUInt8 r1, 1
 *   3B 00 01 01 03 00           PutById r0, r1, 1, 3
 *   98 4E 01 00 (at 0x162)      JLess 0x01b0, r1, r0
 *   70 02 00 00 00 00 00 00 04 40  LoadConstDouble r2, 2.5
 *   59 00 0D 02                 CallBuiltin r0, "Math.floor", 2
 *   43 06 00 00 00 (99)         DeclareGlobalVar 6
 *
 * Switches. SwitchImm (UIntSwitchImm from 98) has Reg8 value, UInt32
 * table, Addr32 default, UInt32 min, UInt32 max; its jump table of
 * max - min + 1 int32 offsets (relative to the instruction) starts at
 * align4(address + table) after the function's bytecode. StringSwitchImm
 * (98+) has Reg8 value, UInt32 global index, UInt32 table, Addr32 default,
 * UInt32 size; its table holds size pairs {UInt32 string id, Int32 offset}
 * at align4(address + table). The table operand is an XXBYTE_OPERAND_DATA
 * whose value is address + table (unaligned, as encoded) and prints like a
 * branch. The tables are outside the instruction, so a plain decode leaves
 * table_count 0. With a decode context (xxbyte_hermes_context: the buffer
 * that holds the instruction and its table, typically the whole file) whose
 * data contains the whole table, the decoder sets table_count (entries)
 * and table_offset (from the instruction's code pointer), and
 * xxbyte_switch_entry returns key min + index and the absolute target, or
 * for StringSwitchImm the string id and the target; the text then appends
 * the targets, " [0x0010, 0x0020]" (StringSwitchImm: " [5: 0x0010, ...]",
 * the key through the resolver as a string INDEX entry, "\"a\": 0x0010").
 * A table outside the context, before the instruction or with min > max
 * leaves table_count 0.
 *
 * Operands, one per BytecodeList.def operand in order:
 *   - XXBYTE_OPERAND_REGISTER: Reg8 (size 1), Reg32 (size 4);
 *   - XXBYTE_OPERAND_IMMEDIATE: UInt8/16/32 (unsigned), Imm32 (SIGNED);
 *   - XXBYTE_OPERAND_FLOAT: Double (size 8, value = the IEEE-754 bits);
 *   - XXBYTE_OPERAND_BRANCH: Addr8/Addr32 (SIGNED relative offset in
 *     extra as encoded, value = address + offset), including the resume
 *     address of SaveGenerator, which does not branch;
 *   - XXBYTE_OPERAND_DATA: the switch table operand (extra = the raw
 *     offset);
 *   - XXBYTE_OPERAND_INDEX: XXBYTE_INDEX_STRING (string table id),
 *     XXBYTE_INDEX_FUNCTION (function header index),
 *     XXBYTE_HERMES_INDEX_BIGINT (BigInt table index) and
 *     XXBYTE_HERMES_INDEX_BUILTIN (builtin method index).
 * operand.size is the encoded width.
 *
 * Groups: Jmp, JmpLong: JUMP|STOP; every other jump (JmpTrue, JLess,
 * JStrictEqual, JmpUndefined, JmpBuiltinIs, ... and their Long forms):
 * CONDITIONAL; SwitchImm, UIntSwitchImm, StringSwitchImm: SWITCH|STOP;
 * Ret: RETURN|STOP; Throw: THROW|STOP; ThrowIf...: THROW; Unreachable:
 * STOP; Call..., Construct..., CallDirect..., CallBuiltin...,
 * CallWithNewTarget..., CallRequire and DirectEval: CALL.
 */
#define XXBYTE_HERMES_MODE(version) ((uint32_t)(version))
#define XXBYTE_HERMES_MODE_OLDEST 59u
#define XXBYTE_HERMES_MODE_NEWEST 100u
#define XXBYTE_HERMES_MODE_DEFAULT XXBYTE_HERMES_MODE_NEWEST

/* Family index kinds. */
#define XXBYTE_HERMES_INDEX_BIGINT (XXBYTE_INDEX_FAMILY_FIRST + 0)  /* BigInt table */
#define XXBYTE_HERMES_INDEX_BUILTIN (XXBYTE_INDEX_FAMILY_FIRST + 1) /* builtin method */

/*
 * Decode context for xxbyte_decode_ex and xxbyte_disasm_buf_ex: the buffer
 * that holds the instructions and their switch tables (for example the
 * whole file, address 0). Instructions decoded with it must lie inside
 * data. NULL, or an instruction outside data, decodes without tables.
 */
typedef struct xxbyte_hermes_context {
    const uint8_t *data;
    size_t size;
    uint64_t address; /* address of data[0] */
} xxbyte_hermes_context;

/*
 * Mnemonic identifiers (xxbyte_instruction.id): every opcode name of the
 * supported versions in order of first appearance (59, then the names
 * later versions added), so an opcode keeps its id across versions even
 * when its number changes. The name is the mnemonic in upper snake case.
 */
typedef enum xxbyte_hermes_id {
    XXBYTE_HERMES_ID_INVALID = 0,
    XXBYTE_HERMES_ID_NEW_OBJECT_WITH_BUFFER = 1,
    XXBYTE_HERMES_ID_NEW_OBJECT_WITH_BUFFER_LONG = 2,
    XXBYTE_HERMES_ID_NEW_OBJECT = 3,
    XXBYTE_HERMES_ID_NEW_OBJECT_WITH_PARENT = 4,
    XXBYTE_HERMES_ID_NEW_ARRAY_WITH_BUFFER = 5,
    XXBYTE_HERMES_ID_NEW_ARRAY_WITH_BUFFER_LONG = 6,
    XXBYTE_HERMES_ID_NEW_ARRAY = 7,
    XXBYTE_HERMES_ID_MOV = 8,
    XXBYTE_HERMES_ID_MOV_LONG = 9,
    XXBYTE_HERMES_ID_NEGATE = 10,
    XXBYTE_HERMES_ID_NOT = 11,
    XXBYTE_HERMES_ID_BIT_NOT = 12,
    XXBYTE_HERMES_ID_TYPE_OF = 13,
    XXBYTE_HERMES_ID_EQ = 14,
    XXBYTE_HERMES_ID_STRICT_EQ = 15,
    XXBYTE_HERMES_ID_NEQ = 16,
    XXBYTE_HERMES_ID_STRICT_NEQ = 17,
    XXBYTE_HERMES_ID_LESS = 18,
    XXBYTE_HERMES_ID_LESS_EQ = 19,
    XXBYTE_HERMES_ID_GREATER = 20,
    XXBYTE_HERMES_ID_GREATER_EQ = 21,
    XXBYTE_HERMES_ID_ADD = 22,
    XXBYTE_HERMES_ID_ADD_N = 23,
    XXBYTE_HERMES_ID_MUL = 24,
    XXBYTE_HERMES_ID_MUL_N = 25,
    XXBYTE_HERMES_ID_DIV = 26,
    XXBYTE_HERMES_ID_DIV_N = 27,
    XXBYTE_HERMES_ID_MOD = 28,
    XXBYTE_HERMES_ID_SUB = 29,
    XXBYTE_HERMES_ID_SUB_N = 30,
    XXBYTE_HERMES_ID_L_SHIFT = 31,
    XXBYTE_HERMES_ID_R_SHIFT = 32,
    XXBYTE_HERMES_ID_U_RSHIFT = 33,
    XXBYTE_HERMES_ID_BIT_AND = 34,
    XXBYTE_HERMES_ID_BIT_XOR = 35,
    XXBYTE_HERMES_ID_BIT_OR = 36,
    XXBYTE_HERMES_ID_INSTANCE_OF = 37,
    XXBYTE_HERMES_ID_IS_IN = 38,
    XXBYTE_HERMES_ID_GET_ENVIRONMENT = 39,
    XXBYTE_HERMES_ID_STORE_TO_ENVIRONMENT = 40,
    XXBYTE_HERMES_ID_STORE_TO_ENVIRONMENT_L = 41,
    XXBYTE_HERMES_ID_STORE_NP_TO_ENVIRONMENT = 42,
    XXBYTE_HERMES_ID_STORE_NP_TO_ENVIRONMENT_L = 43,
    XXBYTE_HERMES_ID_LOAD_FROM_ENVIRONMENT = 44,
    XXBYTE_HERMES_ID_LOAD_FROM_ENVIRONMENT_L = 45,
    XXBYTE_HERMES_ID_GET_GLOBAL_OBJECT = 46,
    XXBYTE_HERMES_ID_GET_NEW_TARGET = 47,
    XXBYTE_HERMES_ID_CREATE_ENVIRONMENT = 48,
    XXBYTE_HERMES_ID_DECLARE_GLOBAL_VAR = 49,
    XXBYTE_HERMES_ID_GET_BY_ID_SHORT = 50,
    XXBYTE_HERMES_ID_GET_BY_ID = 51,
    XXBYTE_HERMES_ID_GET_BY_ID_LONG = 52,
    XXBYTE_HERMES_ID_TRY_GET_BY_ID = 53,
    XXBYTE_HERMES_ID_TRY_GET_BY_ID_LONG = 54,
    XXBYTE_HERMES_ID_PUT_BY_ID = 55,
    XXBYTE_HERMES_ID_PUT_BY_ID_LONG = 56,
    XXBYTE_HERMES_ID_TRY_PUT_BY_ID = 57,
    XXBYTE_HERMES_ID_TRY_PUT_BY_ID_LONG = 58,
    XXBYTE_HERMES_ID_PUT_NEW_OWN_BY_ID_SHORT = 59,
    XXBYTE_HERMES_ID_PUT_NEW_OWN_BY_ID = 60,
    XXBYTE_HERMES_ID_PUT_NEW_OWN_BY_ID_LONG = 61,
    XXBYTE_HERMES_ID_PUT_NEW_OWN_NE_BY_ID = 62,
    XXBYTE_HERMES_ID_PUT_NEW_OWN_NE_BY_ID_LONG = 63,
    XXBYTE_HERMES_ID_PUT_OWN_BY_INDEX = 64,
    XXBYTE_HERMES_ID_PUT_OWN_BY_INDEX_L = 65,
    XXBYTE_HERMES_ID_PUT_OWN_BY_VAL = 66,
    XXBYTE_HERMES_ID_DEL_BY_ID = 67,
    XXBYTE_HERMES_ID_DEL_BY_ID_LONG = 68,
    XXBYTE_HERMES_ID_GET_BY_VAL = 69,
    XXBYTE_HERMES_ID_PUT_BY_VAL = 70,
    XXBYTE_HERMES_ID_DEL_BY_VAL = 71,
    XXBYTE_HERMES_ID_PUT_OWN_GETTER_SETTER_BY_VAL = 72,
    XXBYTE_HERMES_ID_GET_P_NAME_LIST = 73,
    XXBYTE_HERMES_ID_GET_NEXT_P_NAME = 74,
    XXBYTE_HERMES_ID_CALL = 75,
    XXBYTE_HERMES_ID_CONSTRUCT = 76,
    XXBYTE_HERMES_ID_CALL1 = 77,
    XXBYTE_HERMES_ID_CALL_DIRECT = 78,
    XXBYTE_HERMES_ID_CALL2 = 79,
    XXBYTE_HERMES_ID_CALL3 = 80,
    XXBYTE_HERMES_ID_CALL4 = 81,
    XXBYTE_HERMES_ID_CALL_LONG = 82,
    XXBYTE_HERMES_ID_CONSTRUCT_LONG = 83,
    XXBYTE_HERMES_ID_CALL_DIRECT_LONG_INDEX = 84,
    XXBYTE_HERMES_ID_CALL_BUILTIN = 85,
    XXBYTE_HERMES_ID_RET = 86,
    XXBYTE_HERMES_ID_CATCH = 87,
    XXBYTE_HERMES_ID_DIRECT_EVAL = 88,
    XXBYTE_HERMES_ID_THROW = 89,
    XXBYTE_HERMES_ID_THROW_IF_UNDEFINED_INST = 90,
    XXBYTE_HERMES_ID_DEBUGGER = 91,
    XXBYTE_HERMES_ID_DEBUGGER_CHECK_BREAK = 92,
    XXBYTE_HERMES_ID_PROFILE_POINT = 93,
    XXBYTE_HERMES_ID_UNREACHABLE = 94,
    XXBYTE_HERMES_ID_CREATE_CLOSURE = 95,
    XXBYTE_HERMES_ID_CREATE_CLOSURE_LONG_INDEX = 96,
    XXBYTE_HERMES_ID_CREATE_GENERATOR_CLOSURE = 97,
    XXBYTE_HERMES_ID_CREATE_GENERATOR_CLOSURE_LONG_INDEX = 98,
    XXBYTE_HERMES_ID_CREATE_THIS = 99,
    XXBYTE_HERMES_ID_SELECT_OBJECT = 100,
    XXBYTE_HERMES_ID_LOAD_PARAM = 101,
    XXBYTE_HERMES_ID_LOAD_PARAM_LONG = 102,
    XXBYTE_HERMES_ID_LOAD_CONST_U_INT8 = 103,
    XXBYTE_HERMES_ID_LOAD_CONST_INT = 104,
    XXBYTE_HERMES_ID_LOAD_CONST_DOUBLE = 105,
    XXBYTE_HERMES_ID_LOAD_CONST_STRING = 106,
    XXBYTE_HERMES_ID_LOAD_CONST_STRING_LONG_INDEX = 107,
    XXBYTE_HERMES_ID_LOAD_CONST_UNDEFINED = 108,
    XXBYTE_HERMES_ID_LOAD_CONST_NULL = 109,
    XXBYTE_HERMES_ID_LOAD_CONST_TRUE = 110,
    XXBYTE_HERMES_ID_LOAD_CONST_FALSE = 111,
    XXBYTE_HERMES_ID_LOAD_CONST_ZERO = 112,
    XXBYTE_HERMES_ID_COERCE_THIS_NS = 113,
    XXBYTE_HERMES_ID_LOAD_THIS_NS = 114,
    XXBYTE_HERMES_ID_TO_NUMBER = 115,
    XXBYTE_HERMES_ID_TO_INT32 = 116,
    XXBYTE_HERMES_ID_ADD_EMPTY_STRING = 117,
    XXBYTE_HERMES_ID_GET_ARGUMENTS_PROP_BY_VAL = 118,
    XXBYTE_HERMES_ID_GET_ARGUMENTS_LENGTH = 119,
    XXBYTE_HERMES_ID_REIFY_ARGUMENTS = 120,
    XXBYTE_HERMES_ID_CREATE_REG_EXP = 121,
    XXBYTE_HERMES_ID_SWITCH_IMM = 122,
    XXBYTE_HERMES_ID_START_GENERATOR = 123,
    XXBYTE_HERMES_ID_RESUME_GENERATOR = 124,
    XXBYTE_HERMES_ID_COMPLETE_GENERATOR = 125,
    XXBYTE_HERMES_ID_CREATE_GENERATOR = 126,
    XXBYTE_HERMES_ID_CREATE_GENERATOR_LONG_INDEX = 127,
    XXBYTE_HERMES_ID_JMP = 128,
    XXBYTE_HERMES_ID_JMP_LONG = 129,
    XXBYTE_HERMES_ID_JMP_TRUE = 130,
    XXBYTE_HERMES_ID_JMP_TRUE_LONG = 131,
    XXBYTE_HERMES_ID_JMP_FALSE = 132,
    XXBYTE_HERMES_ID_JMP_FALSE_LONG = 133,
    XXBYTE_HERMES_ID_JMP_UNDEFINED = 134,
    XXBYTE_HERMES_ID_JMP_UNDEFINED_LONG = 135,
    XXBYTE_HERMES_ID_SAVE_GENERATOR = 136,
    XXBYTE_HERMES_ID_SAVE_GENERATOR_LONG = 137,
    XXBYTE_HERMES_ID_J_LESS = 138,
    XXBYTE_HERMES_ID_J_LESS_LONG = 139,
    XXBYTE_HERMES_ID_J_NOT_LESS = 140,
    XXBYTE_HERMES_ID_J_NOT_LESS_LONG = 141,
    XXBYTE_HERMES_ID_J_LESS_N = 142,
    XXBYTE_HERMES_ID_J_LESS_N_LONG = 143,
    XXBYTE_HERMES_ID_J_NOT_LESS_N = 144,
    XXBYTE_HERMES_ID_J_NOT_LESS_N_LONG = 145,
    XXBYTE_HERMES_ID_J_LESS_EQUAL = 146,
    XXBYTE_HERMES_ID_J_LESS_EQUAL_LONG = 147,
    XXBYTE_HERMES_ID_J_NOT_LESS_EQUAL = 148,
    XXBYTE_HERMES_ID_J_NOT_LESS_EQUAL_LONG = 149,
    XXBYTE_HERMES_ID_J_LESS_EQUAL_N = 150,
    XXBYTE_HERMES_ID_J_LESS_EQUAL_N_LONG = 151,
    XXBYTE_HERMES_ID_J_NOT_LESS_EQUAL_N = 152,
    XXBYTE_HERMES_ID_J_NOT_LESS_EQUAL_N_LONG = 153,
    XXBYTE_HERMES_ID_J_GREATER = 154,
    XXBYTE_HERMES_ID_J_GREATER_LONG = 155,
    XXBYTE_HERMES_ID_J_NOT_GREATER = 156,
    XXBYTE_HERMES_ID_J_NOT_GREATER_LONG = 157,
    XXBYTE_HERMES_ID_J_GREATER_N = 158,
    XXBYTE_HERMES_ID_J_GREATER_N_LONG = 159,
    XXBYTE_HERMES_ID_J_NOT_GREATER_N = 160,
    XXBYTE_HERMES_ID_J_NOT_GREATER_N_LONG = 161,
    XXBYTE_HERMES_ID_J_GREATER_EQUAL = 162,
    XXBYTE_HERMES_ID_J_GREATER_EQUAL_LONG = 163,
    XXBYTE_HERMES_ID_J_NOT_GREATER_EQUAL = 164,
    XXBYTE_HERMES_ID_J_NOT_GREATER_EQUAL_LONG = 165,
    XXBYTE_HERMES_ID_J_GREATER_EQUAL_N = 166,
    XXBYTE_HERMES_ID_J_GREATER_EQUAL_N_LONG = 167,
    XXBYTE_HERMES_ID_J_NOT_GREATER_EQUAL_N = 168,
    XXBYTE_HERMES_ID_J_NOT_GREATER_EQUAL_N_LONG = 169,
    XXBYTE_HERMES_ID_J_EQUAL = 170,
    XXBYTE_HERMES_ID_J_EQUAL_LONG = 171,
    XXBYTE_HERMES_ID_J_NOT_EQUAL = 172,
    XXBYTE_HERMES_ID_J_NOT_EQUAL_LONG = 173,
    XXBYTE_HERMES_ID_J_STRICT_EQUAL = 174,
    XXBYTE_HERMES_ID_J_STRICT_EQUAL_LONG = 175,
    XXBYTE_HERMES_ID_J_STRICT_NOT_EQUAL = 176,
    XXBYTE_HERMES_ID_J_STRICT_NOT_EQUAL_LONG = 177,
    XXBYTE_HERMES_ID_ASYNC_BREAK_CHECK = 178,
    XXBYTE_HERMES_ID_ITERATOR_BEGIN = 179,
    XXBYTE_HERMES_ID_ITERATOR_NEXT = 180,
    XXBYTE_HERMES_ID_ITERATOR_CLOSE = 181,
    XXBYTE_HERMES_ID_CALL_BUILTIN_LONG = 182,
    XXBYTE_HERMES_ID_GET_BUILTIN_CLOSURE = 183,
    XXBYTE_HERMES_ID_THROW_IF_EMPTY = 184,
    XXBYTE_HERMES_ID_CREATE_ASYNC_CLOSURE = 185,
    XXBYTE_HERMES_ID_CREATE_ASYNC_CLOSURE_LONG_INDEX = 186,
    XXBYTE_HERMES_ID_LOAD_CONST_EMPTY = 187,
    XXBYTE_HERMES_ID_INC = 188,
    XXBYTE_HERMES_ID_DEC = 189,
    XXBYTE_HERMES_ID_LOAD_CONST_BIG_INT = 190,
    XXBYTE_HERMES_ID_LOAD_CONST_BIG_INT_LONG_INDEX = 191,
    XXBYTE_HERMES_ID_TO_NUMERIC = 192,
    XXBYTE_HERMES_ID_CREATE_INNER_ENVIRONMENT = 193,
    XXBYTE_HERMES_ID_THROW_IF_HAS_RESTRICTED_GLOBAL_PROPERTY = 194,
    XXBYTE_HERMES_ID_ADD_S = 195,
    XXBYTE_HERMES_ID_GET_PARENT_ENVIRONMENT = 196,
    XXBYTE_HERMES_ID_GET_CLOSURE_ENVIRONMENT = 197,
    XXBYTE_HERMES_ID_CREATE_FUNCTION_ENVIRONMENT = 198,
    XXBYTE_HERMES_ID_CREATE_TOP_LEVEL_ENVIRONMENT = 199,
    XXBYTE_HERMES_ID_PUT_BY_ID_LOOSE = 200,
    XXBYTE_HERMES_ID_PUT_BY_ID_STRICT = 201,
    XXBYTE_HERMES_ID_PUT_BY_ID_LOOSE_LONG = 202,
    XXBYTE_HERMES_ID_PUT_BY_ID_STRICT_LONG = 203,
    XXBYTE_HERMES_ID_TRY_PUT_BY_ID_LOOSE = 204,
    XXBYTE_HERMES_ID_TRY_PUT_BY_ID_STRICT = 205,
    XXBYTE_HERMES_ID_TRY_PUT_BY_ID_LOOSE_LONG = 206,
    XXBYTE_HERMES_ID_TRY_PUT_BY_ID_STRICT_LONG = 207,
    XXBYTE_HERMES_ID_PUT_OWN_BY_SLOT_IDX = 208,
    XXBYTE_HERMES_ID_PUT_OWN_BY_SLOT_IDX_LONG = 209,
    XXBYTE_HERMES_ID_DEL_BY_ID_LOOSE = 210,
    XXBYTE_HERMES_ID_DEL_BY_ID_STRICT = 211,
    XXBYTE_HERMES_ID_DEL_BY_ID_LOOSE_LONG = 212,
    XXBYTE_HERMES_ID_DEL_BY_ID_STRICT_LONG = 213,
    XXBYTE_HERMES_ID_GET_BY_INDEX = 214,
    XXBYTE_HERMES_ID_PUT_BY_VAL_LOOSE = 215,
    XXBYTE_HERMES_ID_PUT_BY_VAL_STRICT = 216,
    XXBYTE_HERMES_ID_DEL_BY_VAL_LOOSE = 217,
    XXBYTE_HERMES_ID_DEL_BY_VAL_STRICT = 218,
    XXBYTE_HERMES_ID_CALL_WITH_NEW_TARGET = 219,
    XXBYTE_HERMES_ID_CALL_WITH_NEW_TARGET_LONG = 220,
    XXBYTE_HERMES_ID_GET_ARGUMENTS_PROP_BY_VAL_LOOSE = 221,
    XXBYTE_HERMES_ID_GET_ARGUMENTS_PROP_BY_VAL_STRICT = 222,
    XXBYTE_HERMES_ID_REIFY_ARGUMENTS_STRICT = 223,
    XXBYTE_HERMES_ID_REIFY_ARGUMENTS_LOOSE = 224,
    XXBYTE_HERMES_ID_TYPED_LOAD_PARENT = 225,
    XXBYTE_HERMES_ID_TYPED_STORE_PARENT = 226,
    XXBYTE_HERMES_ID_NEW_OBJECT_WITH_BUFFER_AND_PARENT = 227,
    XXBYTE_HERMES_ID_NEW_FAST_ARRAY = 228,
    XXBYTE_HERMES_ID_FAST_ARRAY_LENGTH = 229,
    XXBYTE_HERMES_ID_FAST_ARRAY_LOAD = 230,
    XXBYTE_HERMES_ID_FAST_ARRAY_STORE = 231,
    XXBYTE_HERMES_ID_FAST_ARRAY_PUSH = 232,
    XXBYTE_HERMES_ID_FAST_ARRAY_APPEND = 233,
    XXBYTE_HERMES_ID_CACHE_NEW_OBJECT = 234,
    XXBYTE_HERMES_ID_PRIVATE_IS_IN = 235,
    XXBYTE_HERMES_ID_TYPE_OF_IS = 236,
    XXBYTE_HERMES_ID_LOAD_PARENT_NO_TRAPS = 237,
    XXBYTE_HERMES_ID_GET_BY_ID_WITH_RECEIVER_LONG = 238,
    XXBYTE_HERMES_ID_GET_OWN_BY_SLOT_IDX = 239,
    XXBYTE_HERMES_ID_GET_OWN_BY_SLOT_IDX_LONG = 240,
    XXBYTE_HERMES_ID_DEFINE_OWN_BY_ID = 241,
    XXBYTE_HERMES_ID_DEFINE_OWN_BY_ID_LONG = 242,
    XXBYTE_HERMES_ID_DEFINE_OWN_BY_INDEX = 243,
    XXBYTE_HERMES_ID_DEFINE_OWN_BY_INDEX_L = 244,
    XXBYTE_HERMES_ID_DEFINE_OWN_IN_DENSE_ARRAY = 245,
    XXBYTE_HERMES_ID_DEFINE_OWN_IN_DENSE_ARRAY_L = 246,
    XXBYTE_HERMES_ID_DEFINE_OWN_BY_VAL = 247,
    XXBYTE_HERMES_ID_GET_BY_VAL_WITH_RECEIVER = 248,
    XXBYTE_HERMES_ID_PUT_BY_VAL_WITH_RECEIVER = 249,
    XXBYTE_HERMES_ID_ADD_OWN_PRIVATE_BY_SYM = 250,
    XXBYTE_HERMES_ID_GET_OWN_PRIVATE_BY_SYM = 251,
    XXBYTE_HERMES_ID_PUT_OWN_PRIVATE_BY_SYM = 252,
    XXBYTE_HERMES_ID_DEFINE_OWN_GETTER_SETTER_BY_VAL = 253,
    XXBYTE_HERMES_ID_CALL_REQUIRE = 254,
    XXBYTE_HERMES_ID_THROW_IF_UNDEFINED = 255,
    XXBYTE_HERMES_ID_THROW_IF_THIS_INITIALIZED = 256,
    XXBYTE_HERMES_ID_CREATE_BASE_CLASS = 257,
    XXBYTE_HERMES_ID_CREATE_BASE_CLASS_LONG_INDEX = 258,
    XXBYTE_HERMES_ID_CREATE_DERIVED_CLASS = 259,
    XXBYTE_HERMES_ID_CREATE_DERIVED_CLASS_LONG_INDEX = 260,
    XXBYTE_HERMES_ID_CREATE_THIS_FOR_NEW = 261,
    XXBYTE_HERMES_ID_CREATE_THIS_FOR_SUPER = 262,
    XXBYTE_HERMES_ID_CREATE_PRIVATE_NAME = 263,
    XXBYTE_HERMES_ID_TO_PROPERTY_KEY = 264,
    XXBYTE_HERMES_ID_U_INT_SWITCH_IMM = 265,
    XXBYTE_HERMES_ID_STRING_SWITCH_IMM = 266,
    XXBYTE_HERMES_ID_JMP_TYPE_OF_IS = 267,
    XXBYTE_HERMES_ID_JMP_BUILTIN_IS = 268,
    XXBYTE_HERMES_ID_JMP_BUILTIN_IS_LONG = 269,
    XXBYTE_HERMES_ID_JMP_BUILTIN_IS_NOT = 270,
    XXBYTE_HERMES_ID_JMP_BUILTIN_IS_NOT_LONG = 271,
    XXBYTE_HERMES_ID_NEW_TYPED_OBJECT_WITH_BUFFER = 272,
    XXBYTE_HERMES_ID_TO_UINT32 = 273,
    XXBYTE_HERMES_ID_IMUL = 274,
    XXBYTE_HERMES_ID_COUNT = 275 /* one past the last id */
} xxbyte_hermes_id;

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_hermes_mnemonic(uint32_t id);

/* The name of opcode in mode, or NULL when the opcode is undefined there or
 * the mode is unsupported. */
XXBYTE_API const char *xxbyte_hermes_opname(uint32_t mode, uint32_t opcode);

/* The number of opcodes of mode (0 when unsupported). */
XXBYTE_API uint32_t xxbyte_hermes_opcode_count(uint32_t mode);

/* The builtin method name of index in mode ("Math.floor"), or NULL. */
XXBYTE_API const char *xxbyte_hermes_builtin(uint32_t mode, uint32_t index);

/* ------------------------------------------------------------------------
 * HBC files.
 *
 * Layout: a 128-byte header (magic 0x1F1903C103BC1FC6, version, SHA-1 of
 * the source, file length, global function index, counts and sizes), then,
 * each aligned to 4: the small function headers, string kinds, identifier
 * hashes, the small string table, the overflow string table, string
 * storage, the array buffer, object key buffer and object value buffer
 * (97+: literal value buffer, object key buffer, object shape table),
 * BigInt table and storage (87+), RegExp table and storage, the CommonJS
 * module table and the function source table (84+); function bytecode and
 * the debug info follow. The header field layouts are those of the
 * official BytecodeFileFormat.h: 59 to 86 have no BigInt fields, 59 to 77
 * call segmentID cjsModuleOffset, 59 to 83 have no function source count,
 * 98+ count StringSwitchImm instructions. Small function headers are 16
 * bytes up to 96 (offset:25 paramCount:7 | bytecodeSize:15 name:17 |
 * infoOffset:25 frameSize:7 | environmentSize, highestReadCacheIndex,
 * highestWriteCacheIndex, flags) and 12 bytes from 98 (offset:25
 * paramCount:5 loopDepth:2 | bytecodeSize:14 name:8 numberRegs:5
 * nonPtrRegs:5 | frameSize, readCacheSize, writeCacheSize and cache bits,
 * flags); a header with the overflowed flag points to a large header with
 * full-width fields. Version 97 (development only) uses yet another layout
 * and is not read; its instructions decode with an explicit mode.
 */
#define XXBYTE_HERMES_MAGIC UINT64_C(0x1F1903C103BC1FC6)
#define XXBYTE_HERMES_HEADER_SIZE 128u

/* xxbyte_hermes_header.options */
#define XXBYTE_HERMES_OPTION_STATIC_BUILTINS 0x01u
#define XXBYTE_HERMES_OPTION_CJS_STATIC 0x02u /* cjsModulesStaticallyResolved */
#define XXBYTE_HERMES_OPTION_HAS_ASYNC 0x04u  /* 96 */

/* A parsed file header and the offsets of the sections (from data[0]). */
typedef struct xxbyte_hermes_header {
    uint32_t version;
    uint8_t source_hash[20];
    uint32_t file_length;
    uint32_t global_code_index;
    uint32_t function_count;
    uint32_t string_kind_count;
    uint32_t identifier_count;
    uint32_t string_count;
    uint32_t overflow_string_count;
    uint32_t string_storage_size;
    uint32_t bigint_count;        /* 0 before 87 */
    uint32_t bigint_storage_size; /* 0 before 87 */
    uint32_t regexp_count;
    uint32_t regexp_storage_size;
    uint32_t array_buffer_size; /* 97+: literal value buffer size */
    uint32_t obj_key_buffer_size;
    uint32_t obj_value_buffer_size; /* 97+: object shape table count */
    uint32_t string_switch_count;   /* 98+: numStringSwitchImms, else 0 */
    uint32_t segment_id;            /* before 78: cjsModuleOffset */
    uint32_t cjs_module_count;
    uint32_t function_source_count; /* 0 before 84 */
    uint32_t debug_info_offset;
    uint8_t options;              /* XXBYTE_HERMES_OPTION_* */
    uint8_t function_header_size; /* 16 (59-96) or 12 (98+) */
    uint8_t reserved[2];
    uint32_t function_headers_offset;
    uint32_t string_kinds_offset;
    uint32_t identifier_hashes_offset;
    uint32_t small_string_table_offset;
    uint32_t overflow_string_table_offset;
    uint32_t string_storage_offset;
    uint32_t array_buffer_offset; /* 97+: literal value buffer */
    uint32_t obj_key_buffer_offset;
    uint32_t obj_value_buffer_offset; /* 97+: object shape table */
    uint32_t bigint_table_offset;
    uint32_t bigint_storage_offset;
    uint32_t regexp_table_offset;
    uint32_t regexp_storage_offset;
    uint32_t cjs_module_table_offset;
    uint32_t function_source_table_offset;
    uint32_t sections_end; /* end of the function source table */
} xxbyte_hermes_header;

/*
 * Parses the header at data[0] and lays out the sections as the official
 * loader does. Returns XXBYTE_STATUS_TRUNCATED when size < 128 or a section
 * runs past size, XXBYTE_STATUS_INVALID_INSTRUCTION for a bad magic (also
 * the delta-form magic, which apps do not ship), and
 * XXBYTE_STATUS_UNSUPPORTED_MODE for a version this file reader does not
 * read (outside 59 to 100, or 97). The file_length field is not checked
 * against size (a truncated file still lists what it holds). On failure
 * *header is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_hermes_parse_header(const uint8_t *data, size_t size, xxbyte_hermes_header *header);

/* xxbyte_hermes_function.prohibit_invoke */
#define XXBYTE_HERMES_PROHIBIT_CALL 0u
#define XXBYTE_HERMES_PROHIBIT_CONSTRUCT 1u
#define XXBYTE_HERMES_PROHIBIT_NONE 2u

/* xxbyte_hermes_function.flags (the FunctionHeaderFlag byte) */
#define XXBYTE_HERMES_FUNCTION_STRICT 0x04u
#define XXBYTE_HERMES_FUNCTION_EXCEPTION_HANDLER 0x08u
#define XXBYTE_HERMES_FUNCTION_DEBUG_INFO 0x10u
#define XXBYTE_HERMES_FUNCTION_OVERFLOWED 0x20u

/* One function header (the large header when the small one overflowed). */
typedef struct xxbyte_hermes_function {
    uint32_t index;
    uint32_t offset; /* bytecode, from data[0] */
    uint32_t bytecode_size;
    uint32_t param_count;         /* including "this" */
    uint32_t frame_size;          /* registers */
    uint32_t function_name;       /* string id */
    uint32_t environment_size;    /* 59-96 */
    uint32_t info_offset;         /* 59-96: exception and debug info */
    uint32_t loop_depth;          /* 98+ */
    uint32_t number_reg_count;    /* 98+ */
    uint32_t non_ptr_reg_count;   /* 98+ */
    uint32_t read_cache_size;     /* highestReadCacheIndex before 98 */
    uint32_t write_cache_size;    /* highestWriteCacheIndex before 98 */
    uint32_t header_offset;       /* the small header, from data[0] */
    uint32_t large_header_offset; /* 0 unless overflowed */
    uint8_t flags;                /* the flag byte, XXBYTE_HERMES_FUNCTION_* */
    uint8_t prohibit_invoke;      /* flags & 3 */
    uint8_t kind;                 /* 98+: flags >> 6 (FuncKind), else 0 */
    uint8_t reserved;
} xxbyte_hermes_function;

/*
 * Reads function header index. Returns XXBYTE_STATUS_INVALID_ARGUMENT when
 * index >= function_count or *header was not parsed from data, and
 * XXBYTE_STATUS_TRUNCATED when the large header or the bytecode runs past
 * size. When only the bytecode does, *function is filled in all the same;
 * on any other failure it is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_hermes_get_function(const uint8_t *data, size_t size, const xxbyte_hermes_header *header, uint32_t index,
                                                    xxbyte_hermes_function *function);

/* One string: its bytes are data[offset .. offset + length * (utf16 ? 2 : 1)). */
typedef struct xxbyte_hermes_string {
    uint32_t offset;    /* from data[0] */
    uint32_t length;    /* in code units */
    uint8_t utf16;      /* UTF-16LE code units, else Latin-1 bytes */
    uint8_t identifier; /* from the string kinds */
    uint8_t reserved[2];
} xxbyte_hermes_string;

/*
 * Reads string id (small table entry, or the overflow entry it points to).
 * Returns XXBYTE_STATUS_INVALID_ARGUMENT when id >= string_count,
 * XXBYTE_STATUS_TRUNCATED when the overflow entry or the characters lie
 * outside the string storage. On failure *string is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_hermes_get_string(const uint8_t *data, size_t size, const xxbyte_hermes_header *header, uint32_t id, xxbyte_hermes_string *string);

/*
 * Reads BigInt index: *offset and *length receive its bytes (little-endian
 * two's complement digits) in the BigInt storage. Same statuses as
 * xxbyte_hermes_get_string.
 */
XXBYTE_API xxbyte_status xxbyte_hermes_get_bigint(const uint8_t *data, size_t size, const xxbyte_hermes_header *header, uint32_t index, uint32_t *offset,
                                                  uint32_t *length);

/*
 * xxbyte_disasm_file reads one HBC file and lists:
 *   - "; hbc version V, N functions, S strings, B bigints, R regexps,
 *     global function G" and "; source hash <40 hex digits>";
 *   - per function, in index order, a blank line and the header line
 *     "; Function<name>(P params, R registers) #I @0xOFFSET, SIZE bytes"
 *     (Constructor<...>/NCFunction<...> as hbcdump names them; 98+ add
 *     ", N numbers, M non-pointers" inside the parentheses), then its
 *     bytecode listed from the function's file offset (addresses are file
 *     offsets, as hbcdump -objdump-disassemble shows them) with switch
 *     tables decoded through the whole file and names resolved: string ids
 *     as "text" (escaped: \" \\ \xHH for bytes and UTF-16 units below 32,
 *     127 and 128 to 255, \uHHHH above; cut with "... after 200
 *     characters), function ids as Function<name> and BigInts as
 *     <decimal>n (very long ones as "<digits>... (N decimal digits)n").
 *   00000118  34 06 00 00 00           DeclareGlobalVar "x"
 *   00000124  64 01 00 01 00           CreateClosure r1, r0, Function<foo>
 * The opcode table is the file's version (XXBYTE_MODE_AUTO) or the mode
 * passed. A function whose bytecode lies outside the file gets its header
 * line and "; bytecode outside the file" (a large header outside the
 * file: "; function #I: large header outside the file"); the listing then
 * ends with XXBYTE_STATUS_TRUNCATED. Exception handler tables, debug
 * info, literal buffers, RegExp bytecode and the CommonJS module table are
 * not listed.
 */

#ifdef __cplusplus
}
#endif

#endif
