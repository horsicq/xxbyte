#ifndef XXBYTE_XXBYTE_WASM_H
#define XXBYTE_XXBYTE_WASM_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_WASM
#error "the WebAssembly decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * WebAssembly decode mode. Mode 0 decodes the instructions of every
 * supported feature: the WebAssembly 3.0 core (2.0 plus tail calls,
 * exception handling with exnref, typed function references, garbage
 * collection, relaxed SIMD, multiple memories, 64-bit memories and
 * extended constant expressions), threads and atomics, legacy exception
 * handling (try, catch, catch_all, rethrow, delegate) and wide arithmetic.
 * Each XXBYTE_WASM_MODE_NO_* bit removes one feature: its opcodes and
 * immediate forms become XXBYTE_STATUS_INVALID_INSTRUCTION. Other bits are
 * XXBYTE_STATUS_UNSUPPORTED_MODE.
 *
 * Without MULTI_MEMORY the memory index of memory.size, memory.grow,
 * memory.init, memory.copy and memory.fill is the reserved byte 0x00 and
 * a memarg alignment field must be below 64; without REFERENCE_TYPES the
 * table index of call_indirect, return_call_indirect, table.init and
 * table.copy is the reserved byte 0x00; without MEMORY64 a memarg offset
 * is a u32 instead of a u64; without MULTI_VALUE a block type cannot be a
 * type index. Value types are gated by their feature: v128 by SIMD,
 * funcref and externref by REFERENCE_TYPES, exnref and nullexnref by
 * EXCEPTIONS, the other abstract heap types by GC, and the (ref ...) forms
 * and every concrete heap type (a type index, also in ref.test and the
 * other GC casts) by FUNCTION_REFERENCES. throw needs either
 * exception-handling feature.
 */
#define XXBYTE_WASM_MODE_DEFAULT 0u
#define XXBYTE_WASM_MODE_NO_SIGN_EXTENSION 0x0001u
#define XXBYTE_WASM_MODE_NO_SAT_FLOAT_TO_INT 0x0002u
#define XXBYTE_WASM_MODE_NO_MULTI_VALUE 0x0004u
#define XXBYTE_WASM_MODE_NO_REFERENCE_TYPES 0x0008u
#define XXBYTE_WASM_MODE_NO_BULK_MEMORY 0x0010u
#define XXBYTE_WASM_MODE_NO_SIMD 0x0020u
#define XXBYTE_WASM_MODE_NO_RELAXED_SIMD 0x0040u
#define XXBYTE_WASM_MODE_NO_THREADS 0x0080u
#define XXBYTE_WASM_MODE_NO_TAIL_CALL 0x0100u
#define XXBYTE_WASM_MODE_NO_EXCEPTIONS 0x0200u /* exnref */
#define XXBYTE_WASM_MODE_NO_LEGACY_EXCEPTIONS 0x0400u
#define XXBYTE_WASM_MODE_NO_FUNCTION_REFERENCES 0x0800u
#define XXBYTE_WASM_MODE_NO_GC 0x1000u
#define XXBYTE_WASM_MODE_NO_MEMORY64 0x2000u
#define XXBYTE_WASM_MODE_NO_MULTI_MEMORY 0x4000u
#define XXBYTE_WASM_MODE_NO_WIDE_ARITHMETIC 0x8000u
#define XXBYTE_WASM_MODE_KNOWN_BITS 0xFFFFu

/* WebAssembly 3.0 without the proposals that are not part of it. */
#define XXBYTE_WASM_MODE_3_0 (XXBYTE_WASM_MODE_NO_THREADS | XXBYTE_WASM_MODE_NO_LEGACY_EXCEPTIONS | XXBYTE_WASM_MODE_NO_WIDE_ARITHMETIC)
/* WebAssembly 2.0: the 1.0 MVP with sign extension, saturating truncation,
 * multi-value, reference types, bulk memory and SIMD. */
#define XXBYTE_WASM_MODE_2_0                                                                                                    \
    (XXBYTE_WASM_MODE_3_0 | XXBYTE_WASM_MODE_NO_RELAXED_SIMD | XXBYTE_WASM_MODE_NO_TAIL_CALL | XXBYTE_WASM_MODE_NO_EXCEPTIONS | \
     XXBYTE_WASM_MODE_NO_FUNCTION_REFERENCES | XXBYTE_WASM_MODE_NO_GC | XXBYTE_WASM_MODE_NO_MEMORY64 | XXBYTE_WASM_MODE_NO_MULTI_MEMORY)
/* WebAssembly 1.0 (the MVP). */
#define XXBYTE_WASM_MODE_1_0 XXBYTE_WASM_MODE_KNOWN_BITS

/*
 * One decoded instruction is one opcode with its immediates. The opcode
 * field holds a one-byte opcode as is and a prefixed one as
 * prefix << 16 | sub-opcode (0xFD000C is v128.const); the sub-opcode is a
 * u32 LEB128 that may use more bytes than needed. Addresses are byte
 * offsets of the caller's choice (a module offset or an offset in the
 * function body); no operand depends on them, because WebAssembly
 * branches name enclosing blocks by label depth.
 *
 * LEB128 immediates follow the binary format: at most ceil(N / 7) bytes
 * for an N-bit value and the unused bits of the last byte zero (unsigned)
 * or equal to the sign (signed); longer or out-of-range encodings are
 * XXBYTE_STATUS_INVALID_INSTRUCTION, padded encodings within the limit are
 * accepted. A block type or heap type that is no type code must be a
 * non-negative s33, so a negative s33 written in two or more bytes is
 * malformed (V8 and WABT read 02 C0 7F as an empty block). Instructions are
 * decoded, not validated: indexes, lane numbers and alignments are not
 * checked against a module.
 *
 * Operands, in encoding order. Every index is an XXBYTE_OPERAND_INDEX whose
 * size is its LEB128 length:
 *   - labels (br, br_if, rethrow, delegate, br_on_*): XXBYTE_INDEX_LABEL,
 *     value = relative depth; functions XXBYTE_INDEX_FUNCTION, locals
 *     XXBYTE_INDEX_LOCAL, globals XXBYTE_INDEX_GLOBAL, tables
 *     XXBYTE_INDEX_TABLE, types XXBYTE_INDEX_TYPE, struct fields
 *     XXBYTE_INDEX_FIELD, and the XXBYTE_WASM_INDEX_* kinds below for
 *     memories, tags, data and element segments. A reserved 0x00 byte
 *     that stands for index 0 (memory.size in the MVP) is an index of
 *     size 1.
 *   - types: a number or vector type, or the empty block type, is
 *     XXBYTE_WASM_INDEX_VALTYPE with value = its code (XXBYTE_WASM_TYPE_*);
 *     an abstract heap type is XXBYTE_WASM_INDEX_HEAPTYPE with value = its
 *     code; a concrete heap type or a block type index is
 *     XXBYTE_INDEX_TYPE. For a reference type, extra holds its form:
 *     XXBYTE_WASM_TYPE_REF_NULL (0x63, also for the one-byte shorthands
 *     such as funcref) or XXBYTE_WASM_TYPE_REF (0x64), and size counts
 *     the 0x63/0x64 byte when there is one; a bare heap type (ref.null)
 *     and a type index have extra 0.
 *   - memarg: an XXBYTE_WASM_INDEX_MEMORY index (IMPLICIT with size 0
 *     when bit 6 of the alignment field is clear), then an
 *     XXBYTE_OPERAND_MEMORY operand: kind 0 (the address comes from the
 *     stack), value = the offset (a u64 stored as int64), size = the
 *     natural access width in bytes, extra = the alignment exponent.
 *     Lane loads and stores add the lane index as an IMMEDIATE.
 *   - i32.const and i64.const: a SIGNED IMMEDIATE; f32.const and f64.const:
 *     an XXBYTE_OPERAND_FLOAT with the IEEE-754 bits; v128.const and the
 *     i8x16.shuffle lanes: XXBYTE_OPERAND_BYTES (16 bytes); lane indexes
 *     and the array.new_fixed length: unsigned IMMEDIATEs.
 *   - br_on_cast and br_on_cast_fail: the cast flags byte (IMMEDIATE),
 *     the label and the two reference types, whose nullability the flags
 *     give; atomic.fence: its reserved ordering byte 0 (IMMEDIATE). Neither
 *     byte is displayed.
 *
 * Inline tables (table_count entries at table_offset, variable length):
 *   - br_table: the label vector without the default, which is operand 0.
 *     xxbyte_switch_entry gives key = the entry number and target = the
 *     label DEPTH (not an address); xxbyte_wasm_br_table_next enumerates.
 *   - select with types (0x1C): the value types, read with
 *     xxbyte_wasm_select_type or xxbyte_wasm_select_type_next.
 *   - try_table: the catch clauses, read with xxbyte_wasm_catch_clause or
 *     xxbyte_wasm_catch_clause_next.
 * The index-based readers find entry i directly when every entry has the
 * minimum size (one-byte labels and value types, two-byte catch_all
 * clauses) and otherwise skip the i entries before it, so reading a whole
 * table through them is quadratic in its size. The *_next cursor readers
 * read it in linear time; use them to enumerate (a CFG builder, say).
 *
 * Groups: br JUMP|STOP; br_if, if, br_on_null, br_on_non_null, br_on_cast
 * and br_on_cast_fail CONDITIONAL; br_table SWITCH|STOP; else, catch and
 * catch_all (the end of the preceding body) JUMP|STOP; return RETURN|STOP;
 * call, call_indirect and call_ref CALL; return_call* CALL|RETURN|STOP;
 * throw, rethrow and throw_ref THROW|STOP; unreachable STOP.
 *
 * Text follows wasm2wat's flat syntax: "i32.const -1", "local.get 0",
 * "br_if 1", "call_indirect 1 (type 2)", "i32.load 1 offset=4 align=1" (a
 * memory index other than 0, an offset other than 0 and an alignment
 * other than the natural one), "block (result i32)", "block (type 3)",
 * "br_table 0 1 2" (default last), "select (result funcref)",
 * "v128.const i32x4 0x00000001 0x00000002 0x00000003 0x00000004",
 * "memory.copy 1 0", "table.init 1 2", "try_table (result i32) (catch 0 1)
 * (catch_all 0)", "ref.test (ref null 3)", "br_on_cast 0 anyref (ref i31)".
 * Floats print as the shortest decimal that reads back ("1.5", "-0",
 * "1e+16", "inf"), NaNs as WAT does ("nan", "-nan:0x200000"). Where
 * wasm2wat 1.0.41 loses information xxbyte keeps it: memory.init prints
 * the memory index before the data index as WAT requires,
 * return_call_indirect, the atomics and the splat and zero-extending
 * loads print a memory index other than 0, and tag 4294967295 is
 * "catch 4294967295", not "catch_all". A block type index prints as
 * "(type N)" because the decoder does not see the type section (the
 * module resolver xxbyte_wasm_resolve_name adds the signature).
 *
 * The resolver sees every displayed index except the
 * XXBYTE_WASM_INDEX_VALTYPE and XXBYTE_WASM_INDEX_HEAPTYPE kinds, whose
 * names are fixed; inline-table labels, tags and type indexes are passed
 * as entries (xxbyte_fmt_resolve_entry).
 */

/* Family index kinds (xxbyte_operand.kind of an XXBYTE_OPERAND_INDEX). */
#define XXBYTE_WASM_INDEX_VALTYPE (XXBYTE_INDEX_FAMILY_FIRST + 0)
#define XXBYTE_WASM_INDEX_HEAPTYPE (XXBYTE_INDEX_FAMILY_FIRST + 1)
#define XXBYTE_WASM_INDEX_MEMORY (XXBYTE_INDEX_FAMILY_FIRST + 2)
#define XXBYTE_WASM_INDEX_TAG (XXBYTE_INDEX_FAMILY_FIRST + 3)
#define XXBYTE_WASM_INDEX_DATA (XXBYTE_INDEX_FAMILY_FIRST + 4)
#define XXBYTE_WASM_INDEX_ELEM (XXBYTE_INDEX_FAMILY_FIRST + 5)

/* Type codes (the byte that encodes the type). */
#define XXBYTE_WASM_TYPE_I32 0x7Fu
#define XXBYTE_WASM_TYPE_I64 0x7Eu
#define XXBYTE_WASM_TYPE_F32 0x7Du
#define XXBYTE_WASM_TYPE_F64 0x7Cu
#define XXBYTE_WASM_TYPE_V128 0x7Bu
#define XXBYTE_WASM_TYPE_I8 0x78u  /* packed, in type definitions only */
#define XXBYTE_WASM_TYPE_I16 0x77u /* packed, in type definitions only */
#define XXBYTE_WASM_TYPE_NOEXN 0x74u
#define XXBYTE_WASM_TYPE_NOFUNC 0x73u
#define XXBYTE_WASM_TYPE_NOEXTERN 0x72u
#define XXBYTE_WASM_TYPE_NONE 0x71u
#define XXBYTE_WASM_TYPE_FUNC 0x70u
#define XXBYTE_WASM_TYPE_EXTERN 0x6Fu
#define XXBYTE_WASM_TYPE_ANY 0x6Eu
#define XXBYTE_WASM_TYPE_EQ 0x6Du
#define XXBYTE_WASM_TYPE_I31 0x6Cu
#define XXBYTE_WASM_TYPE_STRUCT 0x6Bu
#define XXBYTE_WASM_TYPE_ARRAY 0x6Au
#define XXBYTE_WASM_TYPE_EXN 0x69u
#define XXBYTE_WASM_TYPE_REF 0x64u      /* (ref ht) */
#define XXBYTE_WASM_TYPE_REF_NULL 0x63u /* (ref null ht) */
#define XXBYTE_WASM_TYPE_EMPTY 0x40u    /* the empty block type */

/*
 * Mnemonic identifiers (xxbyte_instruction.id): one per mnemonic, in
 * opcode order (one-byte opcodes, then 0xFB, 0xFC, 0xFD and 0xFE). The two
 * encodings of select, ref.test and ref.cast share an id. The name is the
 * mnemonic upper-cased with every run of other characters than A-Z and 0-9
 * replaced by '_'. Valid ids are 1 to XXBYTE_WASM_ID_COUNT - 1.
 */
typedef enum xxbyte_wasm_id {
    XXBYTE_WASM_ID_INVALID = 0,
    XXBYTE_WASM_ID_UNREACHABLE = 1,
    XXBYTE_WASM_ID_NOP = 2,
    XXBYTE_WASM_ID_BLOCK = 3,
    XXBYTE_WASM_ID_LOOP = 4,
    XXBYTE_WASM_ID_IF = 5,
    XXBYTE_WASM_ID_ELSE = 6,
    XXBYTE_WASM_ID_TRY = 7,
    XXBYTE_WASM_ID_CATCH = 8,
    XXBYTE_WASM_ID_THROW = 9,
    XXBYTE_WASM_ID_RETHROW = 10,
    XXBYTE_WASM_ID_THROW_REF = 11,
    XXBYTE_WASM_ID_END = 12,
    XXBYTE_WASM_ID_BR = 13,
    XXBYTE_WASM_ID_BR_IF = 14,
    XXBYTE_WASM_ID_BR_TABLE = 15,
    XXBYTE_WASM_ID_RETURN = 16,
    XXBYTE_WASM_ID_CALL = 17,
    XXBYTE_WASM_ID_CALL_INDIRECT = 18,
    XXBYTE_WASM_ID_RETURN_CALL = 19,
    XXBYTE_WASM_ID_RETURN_CALL_INDIRECT = 20,
    XXBYTE_WASM_ID_CALL_REF = 21,
    XXBYTE_WASM_ID_RETURN_CALL_REF = 22,
    XXBYTE_WASM_ID_DELEGATE = 23,
    XXBYTE_WASM_ID_CATCH_ALL = 24,
    XXBYTE_WASM_ID_DROP = 25,
    XXBYTE_WASM_ID_SELECT = 26,
    XXBYTE_WASM_ID_TRY_TABLE = 27,
    XXBYTE_WASM_ID_LOCAL_GET = 28,
    XXBYTE_WASM_ID_LOCAL_SET = 29,
    XXBYTE_WASM_ID_LOCAL_TEE = 30,
    XXBYTE_WASM_ID_GLOBAL_GET = 31,
    XXBYTE_WASM_ID_GLOBAL_SET = 32,
    XXBYTE_WASM_ID_TABLE_GET = 33,
    XXBYTE_WASM_ID_TABLE_SET = 34,
    XXBYTE_WASM_ID_I32_LOAD = 35,
    XXBYTE_WASM_ID_I64_LOAD = 36,
    XXBYTE_WASM_ID_F32_LOAD = 37,
    XXBYTE_WASM_ID_F64_LOAD = 38,
    XXBYTE_WASM_ID_I32_LOAD8_S = 39,
    XXBYTE_WASM_ID_I32_LOAD8_U = 40,
    XXBYTE_WASM_ID_I32_LOAD16_S = 41,
    XXBYTE_WASM_ID_I32_LOAD16_U = 42,
    XXBYTE_WASM_ID_I64_LOAD8_S = 43,
    XXBYTE_WASM_ID_I64_LOAD8_U = 44,
    XXBYTE_WASM_ID_I64_LOAD16_S = 45,
    XXBYTE_WASM_ID_I64_LOAD16_U = 46,
    XXBYTE_WASM_ID_I64_LOAD32_S = 47,
    XXBYTE_WASM_ID_I64_LOAD32_U = 48,
    XXBYTE_WASM_ID_I32_STORE = 49,
    XXBYTE_WASM_ID_I64_STORE = 50,
    XXBYTE_WASM_ID_F32_STORE = 51,
    XXBYTE_WASM_ID_F64_STORE = 52,
    XXBYTE_WASM_ID_I32_STORE8 = 53,
    XXBYTE_WASM_ID_I32_STORE16 = 54,
    XXBYTE_WASM_ID_I64_STORE8 = 55,
    XXBYTE_WASM_ID_I64_STORE16 = 56,
    XXBYTE_WASM_ID_I64_STORE32 = 57,
    XXBYTE_WASM_ID_MEMORY_SIZE = 58,
    XXBYTE_WASM_ID_MEMORY_GROW = 59,
    XXBYTE_WASM_ID_I32_CONST = 60,
    XXBYTE_WASM_ID_I64_CONST = 61,
    XXBYTE_WASM_ID_F32_CONST = 62,
    XXBYTE_WASM_ID_F64_CONST = 63,
    XXBYTE_WASM_ID_I32_EQZ = 64,
    XXBYTE_WASM_ID_I32_EQ = 65,
    XXBYTE_WASM_ID_I32_NE = 66,
    XXBYTE_WASM_ID_I32_LT_S = 67,
    XXBYTE_WASM_ID_I32_LT_U = 68,
    XXBYTE_WASM_ID_I32_GT_S = 69,
    XXBYTE_WASM_ID_I32_GT_U = 70,
    XXBYTE_WASM_ID_I32_LE_S = 71,
    XXBYTE_WASM_ID_I32_LE_U = 72,
    XXBYTE_WASM_ID_I32_GE_S = 73,
    XXBYTE_WASM_ID_I32_GE_U = 74,
    XXBYTE_WASM_ID_I64_EQZ = 75,
    XXBYTE_WASM_ID_I64_EQ = 76,
    XXBYTE_WASM_ID_I64_NE = 77,
    XXBYTE_WASM_ID_I64_LT_S = 78,
    XXBYTE_WASM_ID_I64_LT_U = 79,
    XXBYTE_WASM_ID_I64_GT_S = 80,
    XXBYTE_WASM_ID_I64_GT_U = 81,
    XXBYTE_WASM_ID_I64_LE_S = 82,
    XXBYTE_WASM_ID_I64_LE_U = 83,
    XXBYTE_WASM_ID_I64_GE_S = 84,
    XXBYTE_WASM_ID_I64_GE_U = 85,
    XXBYTE_WASM_ID_F32_EQ = 86,
    XXBYTE_WASM_ID_F32_NE = 87,
    XXBYTE_WASM_ID_F32_LT = 88,
    XXBYTE_WASM_ID_F32_GT = 89,
    XXBYTE_WASM_ID_F32_LE = 90,
    XXBYTE_WASM_ID_F32_GE = 91,
    XXBYTE_WASM_ID_F64_EQ = 92,
    XXBYTE_WASM_ID_F64_NE = 93,
    XXBYTE_WASM_ID_F64_LT = 94,
    XXBYTE_WASM_ID_F64_GT = 95,
    XXBYTE_WASM_ID_F64_LE = 96,
    XXBYTE_WASM_ID_F64_GE = 97,
    XXBYTE_WASM_ID_I32_CLZ = 98,
    XXBYTE_WASM_ID_I32_CTZ = 99,
    XXBYTE_WASM_ID_I32_POPCNT = 100,
    XXBYTE_WASM_ID_I32_ADD = 101,
    XXBYTE_WASM_ID_I32_SUB = 102,
    XXBYTE_WASM_ID_I32_MUL = 103,
    XXBYTE_WASM_ID_I32_DIV_S = 104,
    XXBYTE_WASM_ID_I32_DIV_U = 105,
    XXBYTE_WASM_ID_I32_REM_S = 106,
    XXBYTE_WASM_ID_I32_REM_U = 107,
    XXBYTE_WASM_ID_I32_AND = 108,
    XXBYTE_WASM_ID_I32_OR = 109,
    XXBYTE_WASM_ID_I32_XOR = 110,
    XXBYTE_WASM_ID_I32_SHL = 111,
    XXBYTE_WASM_ID_I32_SHR_S = 112,
    XXBYTE_WASM_ID_I32_SHR_U = 113,
    XXBYTE_WASM_ID_I32_ROTL = 114,
    XXBYTE_WASM_ID_I32_ROTR = 115,
    XXBYTE_WASM_ID_I64_CLZ = 116,
    XXBYTE_WASM_ID_I64_CTZ = 117,
    XXBYTE_WASM_ID_I64_POPCNT = 118,
    XXBYTE_WASM_ID_I64_ADD = 119,
    XXBYTE_WASM_ID_I64_SUB = 120,
    XXBYTE_WASM_ID_I64_MUL = 121,
    XXBYTE_WASM_ID_I64_DIV_S = 122,
    XXBYTE_WASM_ID_I64_DIV_U = 123,
    XXBYTE_WASM_ID_I64_REM_S = 124,
    XXBYTE_WASM_ID_I64_REM_U = 125,
    XXBYTE_WASM_ID_I64_AND = 126,
    XXBYTE_WASM_ID_I64_OR = 127,
    XXBYTE_WASM_ID_I64_XOR = 128,
    XXBYTE_WASM_ID_I64_SHL = 129,
    XXBYTE_WASM_ID_I64_SHR_S = 130,
    XXBYTE_WASM_ID_I64_SHR_U = 131,
    XXBYTE_WASM_ID_I64_ROTL = 132,
    XXBYTE_WASM_ID_I64_ROTR = 133,
    XXBYTE_WASM_ID_F32_ABS = 134,
    XXBYTE_WASM_ID_F32_NEG = 135,
    XXBYTE_WASM_ID_F32_CEIL = 136,
    XXBYTE_WASM_ID_F32_FLOOR = 137,
    XXBYTE_WASM_ID_F32_TRUNC = 138,
    XXBYTE_WASM_ID_F32_NEAREST = 139,
    XXBYTE_WASM_ID_F32_SQRT = 140,
    XXBYTE_WASM_ID_F32_ADD = 141,
    XXBYTE_WASM_ID_F32_SUB = 142,
    XXBYTE_WASM_ID_F32_MUL = 143,
    XXBYTE_WASM_ID_F32_DIV = 144,
    XXBYTE_WASM_ID_F32_MIN = 145,
    XXBYTE_WASM_ID_F32_MAX = 146,
    XXBYTE_WASM_ID_F32_COPYSIGN = 147,
    XXBYTE_WASM_ID_F64_ABS = 148,
    XXBYTE_WASM_ID_F64_NEG = 149,
    XXBYTE_WASM_ID_F64_CEIL = 150,
    XXBYTE_WASM_ID_F64_FLOOR = 151,
    XXBYTE_WASM_ID_F64_TRUNC = 152,
    XXBYTE_WASM_ID_F64_NEAREST = 153,
    XXBYTE_WASM_ID_F64_SQRT = 154,
    XXBYTE_WASM_ID_F64_ADD = 155,
    XXBYTE_WASM_ID_F64_SUB = 156,
    XXBYTE_WASM_ID_F64_MUL = 157,
    XXBYTE_WASM_ID_F64_DIV = 158,
    XXBYTE_WASM_ID_F64_MIN = 159,
    XXBYTE_WASM_ID_F64_MAX = 160,
    XXBYTE_WASM_ID_F64_COPYSIGN = 161,
    XXBYTE_WASM_ID_I32_WRAP_I64 = 162,
    XXBYTE_WASM_ID_I32_TRUNC_F32_S = 163,
    XXBYTE_WASM_ID_I32_TRUNC_F32_U = 164,
    XXBYTE_WASM_ID_I32_TRUNC_F64_S = 165,
    XXBYTE_WASM_ID_I32_TRUNC_F64_U = 166,
    XXBYTE_WASM_ID_I64_EXTEND_I32_S = 167,
    XXBYTE_WASM_ID_I64_EXTEND_I32_U = 168,
    XXBYTE_WASM_ID_I64_TRUNC_F32_S = 169,
    XXBYTE_WASM_ID_I64_TRUNC_F32_U = 170,
    XXBYTE_WASM_ID_I64_TRUNC_F64_S = 171,
    XXBYTE_WASM_ID_I64_TRUNC_F64_U = 172,
    XXBYTE_WASM_ID_F32_CONVERT_I32_S = 173,
    XXBYTE_WASM_ID_F32_CONVERT_I32_U = 174,
    XXBYTE_WASM_ID_F32_CONVERT_I64_S = 175,
    XXBYTE_WASM_ID_F32_CONVERT_I64_U = 176,
    XXBYTE_WASM_ID_F32_DEMOTE_F64 = 177,
    XXBYTE_WASM_ID_F64_CONVERT_I32_S = 178,
    XXBYTE_WASM_ID_F64_CONVERT_I32_U = 179,
    XXBYTE_WASM_ID_F64_CONVERT_I64_S = 180,
    XXBYTE_WASM_ID_F64_CONVERT_I64_U = 181,
    XXBYTE_WASM_ID_F64_PROMOTE_F32 = 182,
    XXBYTE_WASM_ID_I32_REINTERPRET_F32 = 183,
    XXBYTE_WASM_ID_I64_REINTERPRET_F64 = 184,
    XXBYTE_WASM_ID_F32_REINTERPRET_I32 = 185,
    XXBYTE_WASM_ID_F64_REINTERPRET_I64 = 186,
    XXBYTE_WASM_ID_I32_EXTEND8_S = 187,
    XXBYTE_WASM_ID_I32_EXTEND16_S = 188,
    XXBYTE_WASM_ID_I64_EXTEND8_S = 189,
    XXBYTE_WASM_ID_I64_EXTEND16_S = 190,
    XXBYTE_WASM_ID_I64_EXTEND32_S = 191,
    XXBYTE_WASM_ID_REF_NULL = 192,
    XXBYTE_WASM_ID_REF_IS_NULL = 193,
    XXBYTE_WASM_ID_REF_FUNC = 194,
    XXBYTE_WASM_ID_REF_EQ = 195,
    XXBYTE_WASM_ID_REF_AS_NON_NULL = 196,
    XXBYTE_WASM_ID_BR_ON_NULL = 197,
    XXBYTE_WASM_ID_BR_ON_NON_NULL = 198,
    XXBYTE_WASM_ID_STRUCT_NEW = 199,
    XXBYTE_WASM_ID_STRUCT_NEW_DEFAULT = 200,
    XXBYTE_WASM_ID_STRUCT_GET = 201,
    XXBYTE_WASM_ID_STRUCT_GET_S = 202,
    XXBYTE_WASM_ID_STRUCT_GET_U = 203,
    XXBYTE_WASM_ID_STRUCT_SET = 204,
    XXBYTE_WASM_ID_ARRAY_NEW = 205,
    XXBYTE_WASM_ID_ARRAY_NEW_DEFAULT = 206,
    XXBYTE_WASM_ID_ARRAY_NEW_FIXED = 207,
    XXBYTE_WASM_ID_ARRAY_NEW_DATA = 208,
    XXBYTE_WASM_ID_ARRAY_NEW_ELEM = 209,
    XXBYTE_WASM_ID_ARRAY_GET = 210,
    XXBYTE_WASM_ID_ARRAY_GET_S = 211,
    XXBYTE_WASM_ID_ARRAY_GET_U = 212,
    XXBYTE_WASM_ID_ARRAY_SET = 213,
    XXBYTE_WASM_ID_ARRAY_LEN = 214,
    XXBYTE_WASM_ID_ARRAY_FILL = 215,
    XXBYTE_WASM_ID_ARRAY_COPY = 216,
    XXBYTE_WASM_ID_ARRAY_INIT_DATA = 217,
    XXBYTE_WASM_ID_ARRAY_INIT_ELEM = 218,
    XXBYTE_WASM_ID_REF_TEST = 219,
    XXBYTE_WASM_ID_REF_CAST = 220,
    XXBYTE_WASM_ID_BR_ON_CAST = 221,
    XXBYTE_WASM_ID_BR_ON_CAST_FAIL = 222,
    XXBYTE_WASM_ID_ANY_CONVERT_EXTERN = 223,
    XXBYTE_WASM_ID_EXTERN_CONVERT_ANY = 224,
    XXBYTE_WASM_ID_REF_I31 = 225,
    XXBYTE_WASM_ID_I31_GET_S = 226,
    XXBYTE_WASM_ID_I31_GET_U = 227,
    XXBYTE_WASM_ID_I32_TRUNC_SAT_F32_S = 228,
    XXBYTE_WASM_ID_I32_TRUNC_SAT_F32_U = 229,
    XXBYTE_WASM_ID_I32_TRUNC_SAT_F64_S = 230,
    XXBYTE_WASM_ID_I32_TRUNC_SAT_F64_U = 231,
    XXBYTE_WASM_ID_I64_TRUNC_SAT_F32_S = 232,
    XXBYTE_WASM_ID_I64_TRUNC_SAT_F32_U = 233,
    XXBYTE_WASM_ID_I64_TRUNC_SAT_F64_S = 234,
    XXBYTE_WASM_ID_I64_TRUNC_SAT_F64_U = 235,
    XXBYTE_WASM_ID_MEMORY_INIT = 236,
    XXBYTE_WASM_ID_DATA_DROP = 237,
    XXBYTE_WASM_ID_MEMORY_COPY = 238,
    XXBYTE_WASM_ID_MEMORY_FILL = 239,
    XXBYTE_WASM_ID_TABLE_INIT = 240,
    XXBYTE_WASM_ID_ELEM_DROP = 241,
    XXBYTE_WASM_ID_TABLE_COPY = 242,
    XXBYTE_WASM_ID_TABLE_GROW = 243,
    XXBYTE_WASM_ID_TABLE_SIZE = 244,
    XXBYTE_WASM_ID_TABLE_FILL = 245,
    XXBYTE_WASM_ID_I64_ADD128 = 246,
    XXBYTE_WASM_ID_I64_SUB128 = 247,
    XXBYTE_WASM_ID_I64_MUL_WIDE_S = 248,
    XXBYTE_WASM_ID_I64_MUL_WIDE_U = 249,
    XXBYTE_WASM_ID_V128_LOAD = 250,
    XXBYTE_WASM_ID_V128_LOAD8X8_S = 251,
    XXBYTE_WASM_ID_V128_LOAD8X8_U = 252,
    XXBYTE_WASM_ID_V128_LOAD16X4_S = 253,
    XXBYTE_WASM_ID_V128_LOAD16X4_U = 254,
    XXBYTE_WASM_ID_V128_LOAD32X2_S = 255,
    XXBYTE_WASM_ID_V128_LOAD32X2_U = 256,
    XXBYTE_WASM_ID_V128_LOAD8_SPLAT = 257,
    XXBYTE_WASM_ID_V128_LOAD16_SPLAT = 258,
    XXBYTE_WASM_ID_V128_LOAD32_SPLAT = 259,
    XXBYTE_WASM_ID_V128_LOAD64_SPLAT = 260,
    XXBYTE_WASM_ID_V128_STORE = 261,
    XXBYTE_WASM_ID_V128_CONST = 262,
    XXBYTE_WASM_ID_I8X16_SHUFFLE = 263,
    XXBYTE_WASM_ID_I8X16_SWIZZLE = 264,
    XXBYTE_WASM_ID_I8X16_SPLAT = 265,
    XXBYTE_WASM_ID_I16X8_SPLAT = 266,
    XXBYTE_WASM_ID_I32X4_SPLAT = 267,
    XXBYTE_WASM_ID_I64X2_SPLAT = 268,
    XXBYTE_WASM_ID_F32X4_SPLAT = 269,
    XXBYTE_WASM_ID_F64X2_SPLAT = 270,
    XXBYTE_WASM_ID_I8X16_EXTRACT_LANE_S = 271,
    XXBYTE_WASM_ID_I8X16_EXTRACT_LANE_U = 272,
    XXBYTE_WASM_ID_I8X16_REPLACE_LANE = 273,
    XXBYTE_WASM_ID_I16X8_EXTRACT_LANE_S = 274,
    XXBYTE_WASM_ID_I16X8_EXTRACT_LANE_U = 275,
    XXBYTE_WASM_ID_I16X8_REPLACE_LANE = 276,
    XXBYTE_WASM_ID_I32X4_EXTRACT_LANE = 277,
    XXBYTE_WASM_ID_I32X4_REPLACE_LANE = 278,
    XXBYTE_WASM_ID_I64X2_EXTRACT_LANE = 279,
    XXBYTE_WASM_ID_I64X2_REPLACE_LANE = 280,
    XXBYTE_WASM_ID_F32X4_EXTRACT_LANE = 281,
    XXBYTE_WASM_ID_F32X4_REPLACE_LANE = 282,
    XXBYTE_WASM_ID_F64X2_EXTRACT_LANE = 283,
    XXBYTE_WASM_ID_F64X2_REPLACE_LANE = 284,
    XXBYTE_WASM_ID_I8X16_EQ = 285,
    XXBYTE_WASM_ID_I8X16_NE = 286,
    XXBYTE_WASM_ID_I8X16_LT_S = 287,
    XXBYTE_WASM_ID_I8X16_LT_U = 288,
    XXBYTE_WASM_ID_I8X16_GT_S = 289,
    XXBYTE_WASM_ID_I8X16_GT_U = 290,
    XXBYTE_WASM_ID_I8X16_LE_S = 291,
    XXBYTE_WASM_ID_I8X16_LE_U = 292,
    XXBYTE_WASM_ID_I8X16_GE_S = 293,
    XXBYTE_WASM_ID_I8X16_GE_U = 294,
    XXBYTE_WASM_ID_I16X8_EQ = 295,
    XXBYTE_WASM_ID_I16X8_NE = 296,
    XXBYTE_WASM_ID_I16X8_LT_S = 297,
    XXBYTE_WASM_ID_I16X8_LT_U = 298,
    XXBYTE_WASM_ID_I16X8_GT_S = 299,
    XXBYTE_WASM_ID_I16X8_GT_U = 300,
    XXBYTE_WASM_ID_I16X8_LE_S = 301,
    XXBYTE_WASM_ID_I16X8_LE_U = 302,
    XXBYTE_WASM_ID_I16X8_GE_S = 303,
    XXBYTE_WASM_ID_I16X8_GE_U = 304,
    XXBYTE_WASM_ID_I32X4_EQ = 305,
    XXBYTE_WASM_ID_I32X4_NE = 306,
    XXBYTE_WASM_ID_I32X4_LT_S = 307,
    XXBYTE_WASM_ID_I32X4_LT_U = 308,
    XXBYTE_WASM_ID_I32X4_GT_S = 309,
    XXBYTE_WASM_ID_I32X4_GT_U = 310,
    XXBYTE_WASM_ID_I32X4_LE_S = 311,
    XXBYTE_WASM_ID_I32X4_LE_U = 312,
    XXBYTE_WASM_ID_I32X4_GE_S = 313,
    XXBYTE_WASM_ID_I32X4_GE_U = 314,
    XXBYTE_WASM_ID_F32X4_EQ = 315,
    XXBYTE_WASM_ID_F32X4_NE = 316,
    XXBYTE_WASM_ID_F32X4_LT = 317,
    XXBYTE_WASM_ID_F32X4_GT = 318,
    XXBYTE_WASM_ID_F32X4_LE = 319,
    XXBYTE_WASM_ID_F32X4_GE = 320,
    XXBYTE_WASM_ID_F64X2_EQ = 321,
    XXBYTE_WASM_ID_F64X2_NE = 322,
    XXBYTE_WASM_ID_F64X2_LT = 323,
    XXBYTE_WASM_ID_F64X2_GT = 324,
    XXBYTE_WASM_ID_F64X2_LE = 325,
    XXBYTE_WASM_ID_F64X2_GE = 326,
    XXBYTE_WASM_ID_V128_NOT = 327,
    XXBYTE_WASM_ID_V128_AND = 328,
    XXBYTE_WASM_ID_V128_ANDNOT = 329,
    XXBYTE_WASM_ID_V128_OR = 330,
    XXBYTE_WASM_ID_V128_XOR = 331,
    XXBYTE_WASM_ID_V128_BITSELECT = 332,
    XXBYTE_WASM_ID_V128_ANY_TRUE = 333,
    XXBYTE_WASM_ID_V128_LOAD8_LANE = 334,
    XXBYTE_WASM_ID_V128_LOAD16_LANE = 335,
    XXBYTE_WASM_ID_V128_LOAD32_LANE = 336,
    XXBYTE_WASM_ID_V128_LOAD64_LANE = 337,
    XXBYTE_WASM_ID_V128_STORE8_LANE = 338,
    XXBYTE_WASM_ID_V128_STORE16_LANE = 339,
    XXBYTE_WASM_ID_V128_STORE32_LANE = 340,
    XXBYTE_WASM_ID_V128_STORE64_LANE = 341,
    XXBYTE_WASM_ID_V128_LOAD32_ZERO = 342,
    XXBYTE_WASM_ID_V128_LOAD64_ZERO = 343,
    XXBYTE_WASM_ID_F32X4_DEMOTE_F64X2_ZERO = 344,
    XXBYTE_WASM_ID_F64X2_PROMOTE_LOW_F32X4 = 345,
    XXBYTE_WASM_ID_I8X16_ABS = 346,
    XXBYTE_WASM_ID_I8X16_NEG = 347,
    XXBYTE_WASM_ID_I8X16_POPCNT = 348,
    XXBYTE_WASM_ID_I8X16_ALL_TRUE = 349,
    XXBYTE_WASM_ID_I8X16_BITMASK = 350,
    XXBYTE_WASM_ID_I8X16_NARROW_I16X8_S = 351,
    XXBYTE_WASM_ID_I8X16_NARROW_I16X8_U = 352,
    XXBYTE_WASM_ID_F32X4_CEIL = 353,
    XXBYTE_WASM_ID_F32X4_FLOOR = 354,
    XXBYTE_WASM_ID_F32X4_TRUNC = 355,
    XXBYTE_WASM_ID_F32X4_NEAREST = 356,
    XXBYTE_WASM_ID_I8X16_SHL = 357,
    XXBYTE_WASM_ID_I8X16_SHR_S = 358,
    XXBYTE_WASM_ID_I8X16_SHR_U = 359,
    XXBYTE_WASM_ID_I8X16_ADD = 360,
    XXBYTE_WASM_ID_I8X16_ADD_SAT_S = 361,
    XXBYTE_WASM_ID_I8X16_ADD_SAT_U = 362,
    XXBYTE_WASM_ID_I8X16_SUB = 363,
    XXBYTE_WASM_ID_I8X16_SUB_SAT_S = 364,
    XXBYTE_WASM_ID_I8X16_SUB_SAT_U = 365,
    XXBYTE_WASM_ID_F64X2_CEIL = 366,
    XXBYTE_WASM_ID_F64X2_FLOOR = 367,
    XXBYTE_WASM_ID_I8X16_MIN_S = 368,
    XXBYTE_WASM_ID_I8X16_MIN_U = 369,
    XXBYTE_WASM_ID_I8X16_MAX_S = 370,
    XXBYTE_WASM_ID_I8X16_MAX_U = 371,
    XXBYTE_WASM_ID_F64X2_TRUNC = 372,
    XXBYTE_WASM_ID_I8X16_AVGR_U = 373,
    XXBYTE_WASM_ID_I16X8_EXTADD_PAIRWISE_I8X16_S = 374,
    XXBYTE_WASM_ID_I16X8_EXTADD_PAIRWISE_I8X16_U = 375,
    XXBYTE_WASM_ID_I32X4_EXTADD_PAIRWISE_I16X8_S = 376,
    XXBYTE_WASM_ID_I32X4_EXTADD_PAIRWISE_I16X8_U = 377,
    XXBYTE_WASM_ID_I16X8_ABS = 378,
    XXBYTE_WASM_ID_I16X8_NEG = 379,
    XXBYTE_WASM_ID_I16X8_Q15MULR_SAT_S = 380,
    XXBYTE_WASM_ID_I16X8_ALL_TRUE = 381,
    XXBYTE_WASM_ID_I16X8_BITMASK = 382,
    XXBYTE_WASM_ID_I16X8_NARROW_I32X4_S = 383,
    XXBYTE_WASM_ID_I16X8_NARROW_I32X4_U = 384,
    XXBYTE_WASM_ID_I16X8_EXTEND_LOW_I8X16_S = 385,
    XXBYTE_WASM_ID_I16X8_EXTEND_HIGH_I8X16_S = 386,
    XXBYTE_WASM_ID_I16X8_EXTEND_LOW_I8X16_U = 387,
    XXBYTE_WASM_ID_I16X8_EXTEND_HIGH_I8X16_U = 388,
    XXBYTE_WASM_ID_I16X8_SHL = 389,
    XXBYTE_WASM_ID_I16X8_SHR_S = 390,
    XXBYTE_WASM_ID_I16X8_SHR_U = 391,
    XXBYTE_WASM_ID_I16X8_ADD = 392,
    XXBYTE_WASM_ID_I16X8_ADD_SAT_S = 393,
    XXBYTE_WASM_ID_I16X8_ADD_SAT_U = 394,
    XXBYTE_WASM_ID_I16X8_SUB = 395,
    XXBYTE_WASM_ID_I16X8_SUB_SAT_S = 396,
    XXBYTE_WASM_ID_I16X8_SUB_SAT_U = 397,
    XXBYTE_WASM_ID_F64X2_NEAREST = 398,
    XXBYTE_WASM_ID_I16X8_MUL = 399,
    XXBYTE_WASM_ID_I16X8_MIN_S = 400,
    XXBYTE_WASM_ID_I16X8_MIN_U = 401,
    XXBYTE_WASM_ID_I16X8_MAX_S = 402,
    XXBYTE_WASM_ID_I16X8_MAX_U = 403,
    XXBYTE_WASM_ID_I16X8_AVGR_U = 404,
    XXBYTE_WASM_ID_I16X8_EXTMUL_LOW_I8X16_S = 405,
    XXBYTE_WASM_ID_I16X8_EXTMUL_HIGH_I8X16_S = 406,
    XXBYTE_WASM_ID_I16X8_EXTMUL_LOW_I8X16_U = 407,
    XXBYTE_WASM_ID_I16X8_EXTMUL_HIGH_I8X16_U = 408,
    XXBYTE_WASM_ID_I32X4_ABS = 409,
    XXBYTE_WASM_ID_I32X4_NEG = 410,
    XXBYTE_WASM_ID_I32X4_ALL_TRUE = 411,
    XXBYTE_WASM_ID_I32X4_BITMASK = 412,
    XXBYTE_WASM_ID_I32X4_EXTEND_LOW_I16X8_S = 413,
    XXBYTE_WASM_ID_I32X4_EXTEND_HIGH_I16X8_S = 414,
    XXBYTE_WASM_ID_I32X4_EXTEND_LOW_I16X8_U = 415,
    XXBYTE_WASM_ID_I32X4_EXTEND_HIGH_I16X8_U = 416,
    XXBYTE_WASM_ID_I32X4_SHL = 417,
    XXBYTE_WASM_ID_I32X4_SHR_S = 418,
    XXBYTE_WASM_ID_I32X4_SHR_U = 419,
    XXBYTE_WASM_ID_I32X4_ADD = 420,
    XXBYTE_WASM_ID_I32X4_SUB = 421,
    XXBYTE_WASM_ID_I32X4_MUL = 422,
    XXBYTE_WASM_ID_I32X4_MIN_S = 423,
    XXBYTE_WASM_ID_I32X4_MIN_U = 424,
    XXBYTE_WASM_ID_I32X4_MAX_S = 425,
    XXBYTE_WASM_ID_I32X4_MAX_U = 426,
    XXBYTE_WASM_ID_I32X4_DOT_I16X8_S = 427,
    XXBYTE_WASM_ID_I32X4_EXTMUL_LOW_I16X8_S = 428,
    XXBYTE_WASM_ID_I32X4_EXTMUL_HIGH_I16X8_S = 429,
    XXBYTE_WASM_ID_I32X4_EXTMUL_LOW_I16X8_U = 430,
    XXBYTE_WASM_ID_I32X4_EXTMUL_HIGH_I16X8_U = 431,
    XXBYTE_WASM_ID_I64X2_ABS = 432,
    XXBYTE_WASM_ID_I64X2_NEG = 433,
    XXBYTE_WASM_ID_I64X2_ALL_TRUE = 434,
    XXBYTE_WASM_ID_I64X2_BITMASK = 435,
    XXBYTE_WASM_ID_I64X2_EXTEND_LOW_I32X4_S = 436,
    XXBYTE_WASM_ID_I64X2_EXTEND_HIGH_I32X4_S = 437,
    XXBYTE_WASM_ID_I64X2_EXTEND_LOW_I32X4_U = 438,
    XXBYTE_WASM_ID_I64X2_EXTEND_HIGH_I32X4_U = 439,
    XXBYTE_WASM_ID_I64X2_SHL = 440,
    XXBYTE_WASM_ID_I64X2_SHR_S = 441,
    XXBYTE_WASM_ID_I64X2_SHR_U = 442,
    XXBYTE_WASM_ID_I64X2_ADD = 443,
    XXBYTE_WASM_ID_I64X2_SUB = 444,
    XXBYTE_WASM_ID_I64X2_MUL = 445,
    XXBYTE_WASM_ID_I64X2_EQ = 446,
    XXBYTE_WASM_ID_I64X2_NE = 447,
    XXBYTE_WASM_ID_I64X2_LT_S = 448,
    XXBYTE_WASM_ID_I64X2_GT_S = 449,
    XXBYTE_WASM_ID_I64X2_LE_S = 450,
    XXBYTE_WASM_ID_I64X2_GE_S = 451,
    XXBYTE_WASM_ID_I64X2_EXTMUL_LOW_I32X4_S = 452,
    XXBYTE_WASM_ID_I64X2_EXTMUL_HIGH_I32X4_S = 453,
    XXBYTE_WASM_ID_I64X2_EXTMUL_LOW_I32X4_U = 454,
    XXBYTE_WASM_ID_I64X2_EXTMUL_HIGH_I32X4_U = 455,
    XXBYTE_WASM_ID_F32X4_ABS = 456,
    XXBYTE_WASM_ID_F32X4_NEG = 457,
    XXBYTE_WASM_ID_F32X4_SQRT = 458,
    XXBYTE_WASM_ID_F32X4_ADD = 459,
    XXBYTE_WASM_ID_F32X4_SUB = 460,
    XXBYTE_WASM_ID_F32X4_MUL = 461,
    XXBYTE_WASM_ID_F32X4_DIV = 462,
    XXBYTE_WASM_ID_F32X4_MIN = 463,
    XXBYTE_WASM_ID_F32X4_MAX = 464,
    XXBYTE_WASM_ID_F32X4_PMIN = 465,
    XXBYTE_WASM_ID_F32X4_PMAX = 466,
    XXBYTE_WASM_ID_F64X2_ABS = 467,
    XXBYTE_WASM_ID_F64X2_NEG = 468,
    XXBYTE_WASM_ID_F64X2_SQRT = 469,
    XXBYTE_WASM_ID_F64X2_ADD = 470,
    XXBYTE_WASM_ID_F64X2_SUB = 471,
    XXBYTE_WASM_ID_F64X2_MUL = 472,
    XXBYTE_WASM_ID_F64X2_DIV = 473,
    XXBYTE_WASM_ID_F64X2_MIN = 474,
    XXBYTE_WASM_ID_F64X2_MAX = 475,
    XXBYTE_WASM_ID_F64X2_PMIN = 476,
    XXBYTE_WASM_ID_F64X2_PMAX = 477,
    XXBYTE_WASM_ID_I32X4_TRUNC_SAT_F32X4_S = 478,
    XXBYTE_WASM_ID_I32X4_TRUNC_SAT_F32X4_U = 479,
    XXBYTE_WASM_ID_F32X4_CONVERT_I32X4_S = 480,
    XXBYTE_WASM_ID_F32X4_CONVERT_I32X4_U = 481,
    XXBYTE_WASM_ID_I32X4_TRUNC_SAT_F64X2_S_ZERO = 482,
    XXBYTE_WASM_ID_I32X4_TRUNC_SAT_F64X2_U_ZERO = 483,
    XXBYTE_WASM_ID_F64X2_CONVERT_LOW_I32X4_S = 484,
    XXBYTE_WASM_ID_F64X2_CONVERT_LOW_I32X4_U = 485,
    XXBYTE_WASM_ID_I8X16_RELAXED_SWIZZLE = 486,
    XXBYTE_WASM_ID_I32X4_RELAXED_TRUNC_F32X4_S = 487,
    XXBYTE_WASM_ID_I32X4_RELAXED_TRUNC_F32X4_U = 488,
    XXBYTE_WASM_ID_I32X4_RELAXED_TRUNC_F64X2_S_ZERO = 489,
    XXBYTE_WASM_ID_I32X4_RELAXED_TRUNC_F64X2_U_ZERO = 490,
    XXBYTE_WASM_ID_F32X4_RELAXED_MADD = 491,
    XXBYTE_WASM_ID_F32X4_RELAXED_NMADD = 492,
    XXBYTE_WASM_ID_F64X2_RELAXED_MADD = 493,
    XXBYTE_WASM_ID_F64X2_RELAXED_NMADD = 494,
    XXBYTE_WASM_ID_I8X16_RELAXED_LANESELECT = 495,
    XXBYTE_WASM_ID_I16X8_RELAXED_LANESELECT = 496,
    XXBYTE_WASM_ID_I32X4_RELAXED_LANESELECT = 497,
    XXBYTE_WASM_ID_I64X2_RELAXED_LANESELECT = 498,
    XXBYTE_WASM_ID_F32X4_RELAXED_MIN = 499,
    XXBYTE_WASM_ID_F32X4_RELAXED_MAX = 500,
    XXBYTE_WASM_ID_F64X2_RELAXED_MIN = 501,
    XXBYTE_WASM_ID_F64X2_RELAXED_MAX = 502,
    XXBYTE_WASM_ID_I16X8_RELAXED_Q15MULR_S = 503,
    XXBYTE_WASM_ID_I16X8_RELAXED_DOT_I8X16_I7X16_S = 504,
    XXBYTE_WASM_ID_I32X4_RELAXED_DOT_I8X16_I7X16_ADD_S = 505,
    XXBYTE_WASM_ID_MEMORY_ATOMIC_NOTIFY = 506,
    XXBYTE_WASM_ID_MEMORY_ATOMIC_WAIT32 = 507,
    XXBYTE_WASM_ID_MEMORY_ATOMIC_WAIT64 = 508,
    XXBYTE_WASM_ID_ATOMIC_FENCE = 509,
    XXBYTE_WASM_ID_I32_ATOMIC_LOAD = 510,
    XXBYTE_WASM_ID_I64_ATOMIC_LOAD = 511,
    XXBYTE_WASM_ID_I32_ATOMIC_LOAD8_U = 512,
    XXBYTE_WASM_ID_I32_ATOMIC_LOAD16_U = 513,
    XXBYTE_WASM_ID_I64_ATOMIC_LOAD8_U = 514,
    XXBYTE_WASM_ID_I64_ATOMIC_LOAD16_U = 515,
    XXBYTE_WASM_ID_I64_ATOMIC_LOAD32_U = 516,
    XXBYTE_WASM_ID_I32_ATOMIC_STORE = 517,
    XXBYTE_WASM_ID_I64_ATOMIC_STORE = 518,
    XXBYTE_WASM_ID_I32_ATOMIC_STORE8 = 519,
    XXBYTE_WASM_ID_I32_ATOMIC_STORE16 = 520,
    XXBYTE_WASM_ID_I64_ATOMIC_STORE8 = 521,
    XXBYTE_WASM_ID_I64_ATOMIC_STORE16 = 522,
    XXBYTE_WASM_ID_I64_ATOMIC_STORE32 = 523,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW_ADD = 524,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW_ADD = 525,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW8_ADD_U = 526,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW16_ADD_U = 527,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW8_ADD_U = 528,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW16_ADD_U = 529,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW32_ADD_U = 530,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW_SUB = 531,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW_SUB = 532,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW8_SUB_U = 533,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW16_SUB_U = 534,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW8_SUB_U = 535,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW16_SUB_U = 536,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW32_SUB_U = 537,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW_AND = 538,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW_AND = 539,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW8_AND_U = 540,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW16_AND_U = 541,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW8_AND_U = 542,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW16_AND_U = 543,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW32_AND_U = 544,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW_OR = 545,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW_OR = 546,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW8_OR_U = 547,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW16_OR_U = 548,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW8_OR_U = 549,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW16_OR_U = 550,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW32_OR_U = 551,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW_XOR = 552,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW_XOR = 553,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW8_XOR_U = 554,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW16_XOR_U = 555,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW8_XOR_U = 556,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW16_XOR_U = 557,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW32_XOR_U = 558,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW_XCHG = 559,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW_XCHG = 560,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW8_XCHG_U = 561,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW16_XCHG_U = 562,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW8_XCHG_U = 563,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW16_XCHG_U = 564,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW32_XCHG_U = 565,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW_CMPXCHG = 566,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW_CMPXCHG = 567,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW8_CMPXCHG_U = 568,
    XXBYTE_WASM_ID_I32_ATOMIC_RMW16_CMPXCHG_U = 569,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW8_CMPXCHG_U = 570,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW16_CMPXCHG_U = 571,
    XXBYTE_WASM_ID_I64_ATOMIC_RMW32_CMPXCHG_U = 572,
    XXBYTE_WASM_ID_COUNT = 573 /* one past the last id */
} xxbyte_wasm_id;

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_wasm_mnemonic(uint32_t id);

/* The text name of a type code: "i32" for a number or vector type, the
 * heap type name ("func", "any") for an abstract heap type when ref_form
 * is 0, its shorthand ("funcref", "nullref") for XXBYTE_WASM_TYPE_REF_NULL
 * and "(ref func)" for XXBYTE_WASM_TYPE_REF. NULL for anything else. */
XXBYTE_API const char *xxbyte_wasm_type_name(uint32_t code, uint32_t ref_form);

/*
 * Cursor readers. *offset is a byte offset from the instruction's first
 * byte: start with instruction->table_offset. Each call reads the entry at
 * *offset and advances *offset past it; after the last of the table_count
 * entries the call returns XXBYTE_STATUS_END_OF_INPUT. Enumerating a table
 * this way takes time linear in its size. An instruction of another kind,
 * an offset outside the table and (br_table) an offset inside a label are
 * XXBYTE_STATUS_INVALID_ARGUMENT; pass only table_offset or an offset
 * stored by a previous call. *offset changes only on success, and the
 * entry is zeroed unless the call succeeds.
 *
 *     uint32_t offset = insn.table_offset, label, i = 0;
 *     while (xxbyte_wasm_br_table_next(&insn, &offset, &label)
 *            == XXBYTE_STATUS_OK) {
 *         ... entry i++ branches to depth label ...
 *     }
 */
XXBYTE_API xxbyte_status xxbyte_wasm_br_table_next(const xxbyte_instruction *instruction, uint32_t *offset, uint32_t *label);

/* Reads value type index of a select with types (0x1C) into *type as an
 * operand described above (VALTYPE, HEAPTYPE or TYPE index). */
XXBYTE_API xxbyte_status xxbyte_wasm_select_type(const xxbyte_instruction *instruction, uint32_t index, xxbyte_operand *type);

/* Cursor form of xxbyte_wasm_select_type, see xxbyte_wasm_br_table_next. */
XXBYTE_API xxbyte_status xxbyte_wasm_select_type_next(const xxbyte_instruction *instruction, uint32_t *offset, xxbyte_operand *type);

/* try_table catch clause kinds. */
#define XXBYTE_WASM_CATCH 0u
#define XXBYTE_WASM_CATCH_REF 1u
#define XXBYTE_WASM_CATCH_ALL 2u
#define XXBYTE_WASM_CATCH_ALL_REF 3u

typedef struct xxbyte_wasm_catch {
    uint32_t kind;  /* XXBYTE_WASM_CATCH_* */
    uint32_t tag;   /* tag index, 0 for the catch_all forms */
    uint32_t label; /* label depth, relative to the try_table's parent */
} xxbyte_wasm_catch;

/* Reads catch clause index of a try_table. */
XXBYTE_API xxbyte_status xxbyte_wasm_catch_clause(const xxbyte_instruction *instruction, uint32_t index, xxbyte_wasm_catch *clause);

/* Cursor form of xxbyte_wasm_catch_clause, see xxbyte_wasm_br_table_next. */
XXBYTE_API xxbyte_status xxbyte_wasm_catch_clause_next(const xxbyte_instruction *instruction, uint32_t *offset, xxbyte_wasm_catch *clause);

/* A function body (code section entry without its size prefix). */
typedef struct xxbyte_wasm_function_body {
    uint32_t local_group_count; /* entries of the locals vector */
    uint32_t local_count;       /* sum of their counts */
    uint32_t locals_offset;     /* offset of the first entry from data[0] */
    uint32_t code_offset;       /* offset of the first instruction */
    uint32_t code_size;         /* bytes from code_offset to the end */
} xxbyte_wasm_function_body;

/*
 * Parses the locals of the function body at data[0..size), where size is
 * the body size from the code section (at most 4 GiB). Every count must be
 * a valid u32 LEB128, the counts must sum to less than 2^32 and every value
 * type must be valid in mode 0 (every feature); otherwise
 * XXBYTE_STATUS_INVALID_INSTRUCTION (XXBYTE_STATUS_TRUNCATED when the
 * vector runs past size). The instructions follow at code_offset and
 * normally end with "end". On failure *body is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_wasm_parse_function_body(const uint8_t *data, size_t size, xxbyte_wasm_function_body *body);

/* xxbyte_wasm_parse_function_body with the value types checked against a
 * decode mode; XXBYTE_STATUS_UNSUPPORTED_MODE for unknown mode bits. */
XXBYTE_API xxbyte_status xxbyte_wasm_parse_function_body_ex(uint32_t mode, const uint8_t *data, size_t size, xxbyte_wasm_function_body *body);

/* Reads locals entry index of a body parsed into *body from the same
 * bytes: *count receives its count and *type the value type as an operand.
 * Like the table readers it is O(1) when every entry takes two bytes and
 * O(index) otherwise; enumerate with xxbyte_wasm_local_group_next. */
XXBYTE_API xxbyte_status xxbyte_wasm_local_group(const uint8_t *data, size_t size, const xxbyte_wasm_function_body *body, uint32_t index, uint32_t *count,
                                                 xxbyte_operand *type);

/* Cursor form of xxbyte_wasm_local_group: *offset starts at
 * body->locals_offset, and XXBYTE_STATUS_END_OF_INPUT follows the last
 * entry (*offset == body->code_offset); otherwise as
 * xxbyte_wasm_br_table_next. */
XXBYTE_API xxbyte_status xxbyte_wasm_local_group_next(const uint8_t *data, size_t size, const xxbyte_wasm_function_body *body, uint32_t *offset, uint32_t *count,
                                                      xxbyte_operand *type);

/* Module sections. */
#define XXBYTE_WASM_SECTION_CUSTOM 0u
#define XXBYTE_WASM_SECTION_TYPE 1u
#define XXBYTE_WASM_SECTION_IMPORT 2u
#define XXBYTE_WASM_SECTION_FUNCTION 3u
#define XXBYTE_WASM_SECTION_TABLE 4u
#define XXBYTE_WASM_SECTION_MEMORY 5u
#define XXBYTE_WASM_SECTION_GLOBAL 6u
#define XXBYTE_WASM_SECTION_EXPORT 7u
#define XXBYTE_WASM_SECTION_START 8u
#define XXBYTE_WASM_SECTION_ELEMENT 9u
#define XXBYTE_WASM_SECTION_CODE 10u
#define XXBYTE_WASM_SECTION_DATA 11u
#define XXBYTE_WASM_SECTION_DATA_COUNT 12u
#define XXBYTE_WASM_SECTION_TAG 13u

typedef struct xxbyte_wasm_section {
    uint32_t id;             /* XXBYTE_WASM_SECTION_* */
    uint32_t offset;         /* offset of the id byte from the module start */
    uint32_t content_offset; /* offset of the contents */
    uint32_t content_size;
    uint32_t name_offset; /* custom sections: the name bytes */
    uint32_t name_size;
} xxbyte_wasm_section;

/*
 * Section walk over a module image at data[0..size), size at most 4 GiB.
 * xxbyte_wasm_check_header accepts the "\0asm" magic with version 1
 * (XXBYTE_STATUS_TRUNCATED for a shorter prefix of it, otherwise
 * XXBYTE_STATUS_INVALID_INSTRUCTION). xxbyte_wasm_next_section reads the
 * section at *offset (start with 8, the header size) into *section and
 * advances *offset past it; it returns XXBYTE_STATUS_END_OF_INPUT at the
 * end of the module, XXBYTE_STATUS_TRUNCATED when the section runs past
 * size and XXBYTE_STATUS_INVALID_INSTRUCTION for an unknown section id or a
 * malformed size or custom-section name; on failure *offset is unchanged
 * and *section zeroed. Offsets are module offsets, so decoding at address =
 * code offset gives wasm-objdump's addresses.
 */
XXBYTE_API xxbyte_status xxbyte_wasm_check_header(const uint8_t *data, size_t size);
XXBYTE_API xxbyte_status xxbyte_wasm_next_section(const uint8_t *data, size_t size, size_t *offset, xxbyte_wasm_section *section);

/* One function of the code section. */
typedef struct xxbyte_wasm_function {
    uint32_t index;       /* entry number in the code section; the function
                             index adds the count of imported functions */
    uint32_t body_offset; /* module offset of the body (after its size) */
    uint32_t body_size;
    xxbyte_wasm_function_body body; /* offsets relative to body_offset */
    uint32_t code_offset;           /* module offset of the first instruction */
    uint32_t code_size;
} xxbyte_wasm_function;

/* Walks the bodies of a code section found with xxbyte_wasm_next_section. */
typedef struct xxbyte_wasm_code_reader {
    uint32_t mode;
    uint32_t count;  /* entries announced by the section */
    uint32_t next;   /* entries read so far */
    uint32_t offset; /* module offset of the next entry */
    uint32_t end;    /* module offset of the section end */
} xxbyte_wasm_code_reader;

/* Starts reading the code section *section of the module data[0..size):
 * XXBYTE_STATUS_INVALID_ARGUMENT when *section is no code section inside
 * size, XXBYTE_STATUS_INVALID_INSTRUCTION when its entry count is
 * malformed. */
XXBYTE_API xxbyte_status xxbyte_wasm_code_begin(uint32_t mode, const uint8_t *data, size_t size, const xxbyte_wasm_section *section, xxbyte_wasm_code_reader *reader);

/* Reads the next function body; XXBYTE_STATUS_END_OF_INPUT after the last
 * one. A body that runs past the section is XXBYTE_STATUS_TRUNCATED; a
 * malformed body size or locals vector, and bytes left in the section after
 * the last body, are XXBYTE_STATUS_INVALID_INSTRUCTION. */
XXBYTE_API xxbyte_status xxbyte_wasm_code_next(const uint8_t *data, size_t size, xxbyte_wasm_code_reader *reader, xxbyte_wasm_function *function);

/*
 * ---- Module model
 *
 * xxbyte_wasm_module_parse reads a whole module image into a model of its
 * index spaces: the type section (function, struct, array and continuation
 * types, with GC rec groups and subtype declarations), imports, the
 * function, table, memory, global, export, start, element, data count,
 * code, data and tag sections, and the names of every "name" custom section
 * (subsections module, function, local, label, type, table, memory, global,
 * elem, data, field and tag) and of the symbol table of a "linking" custom
 * section (relocatable objects such as clang -c output).
 *
 * The model borrows data: keep it alive and unchanged while the model is in
 * use. Every offset in it is a module offset (from data[0]). Imported
 * entities come first in each index space, as in the binary format. The
 * arrays are owned by the model; release them with xxbyte_wasm_module_free.
 *
 * Parsing is lenient, for a disassembler: the sections may come in any
 * order; a second copy of a non-custom section is ignored; a section whose
 * contents are malformed keeps the entries read before the defect, and its
 * id is set in bad_sections. A malformed "name" or "linking" section keeps
 * the names applied before the defect (wasm2wat --ignore-custom-section-
 * errors) and sets bit 0 of bad_sections. Names are applied after the
 * standard sections, so their index ranges are those of the whole module.
 * Value types are read with every feature (mode 0).
 *
 * Returns XXBYTE_STATUS_OK, XXBYTE_STATUS_INVALID_ARGUMENT for NULL
 * arguments or a size above UINT32_MAX (*module zeroed),
 * XXBYTE_STATUS_INVALID_INSTRUCTION or XXBYTE_STATUS_TRUNCATED for a bad
 * header (*module zeroed) and XXBYTE_STATUS_OUT_OF_MEMORY. A module whose
 * section walk fails (xxbyte_wasm_next_section), or that has bad sections,
 * gives the first such status, XXBYTE_STATUS_TRUNCATED for a cut file and
 * XXBYTE_STATUS_INVALID_INSTRUCTION otherwise, with the partial model filled
 * in; free it in every case.
 */

/* External kinds (import and export descriptors). */
#define XXBYTE_WASM_EXTERNAL_FUNC 0u
#define XXBYTE_WASM_EXTERNAL_TABLE 1u
#define XXBYTE_WASM_EXTERNAL_MEMORY 2u
#define XXBYTE_WASM_EXTERNAL_GLOBAL 3u
#define XXBYTE_WASM_EXTERNAL_TAG 4u

/* Composite type forms. */
#define XXBYTE_WASM_FORM_FUNC 0x60u
#define XXBYTE_WASM_FORM_STRUCT 0x5Fu
#define XXBYTE_WASM_FORM_ARRAY 0x5Eu
#define XXBYTE_WASM_FORM_CONT 0x5Du /* stack switching: cont $ft */

#define XXBYTE_WASM_NONE 0xFFFFFFFFu /* no index */

/* Bytes of the module: data[offset..offset + size). */
typedef struct xxbyte_wasm_string {
    uint32_t offset;
    uint32_t size;
} xxbyte_wasm_string;

typedef struct xxbyte_wasm_type {
    uint32_t form;            /* XXBYTE_WASM_FORM_* */
    uint32_t offset;          /* module offset of the form byte */
    uint32_t rec_first;       /* index of the first type of its rec group */
    uint32_t rec_count;       /* types in that group, 1 outside a rec */
    uint32_t is_final;        /* 0 for a "sub" (0x50) declaration */
    uint32_t supertype_count; /* declared supertypes */
    uint32_t supertype;       /* the first of them, XXBYTE_WASM_NONE if none */
    uint32_t param_count;     /* func: params; struct: fields; array: 1 */
    uint32_t params_offset;   /* the first param or field */
    uint32_t result_count;    /* func: results */
    uint32_t results_offset;
    uint32_t cont_type; /* cont: the function type, else XXBYTE_WASM_NONE */
} xxbyte_wasm_type;

typedef struct xxbyte_wasm_import {
    xxbyte_wasm_string module_name;
    xxbyte_wasm_string field_name;
    uint32_t kind;        /* XXBYTE_WASM_EXTERNAL_* */
    uint32_t index;       /* index in the space of its kind */
    uint32_t type;        /* func and tag: the type index, else 0 */
    uint32_t desc_offset; /* the descriptor after the kind byte */
} xxbyte_wasm_import;

typedef struct xxbyte_wasm_func {
    uint32_t type;        /* type index, XXBYTE_WASM_NONE if unknown */
    uint32_t import;      /* import entry, XXBYTE_WASM_NONE if defined */
    uint32_t body_offset; /* the body after its size, 0 if none */
    uint32_t body_size;
    uint32_t local_count; /* locals declared by the body (not params) */
} xxbyte_wasm_func;

/* A table, memory, global or tag. */
typedef struct xxbyte_wasm_entity {
    uint32_t import;      /* import entry, XXBYTE_WASM_NONE if defined */
    uint32_t type_offset; /* table: the reference type; memory: the limits
                             flags; global: the value type; tag: 0 */
    uint32_t type;        /* tag: the type index; global: 1 if mutable;
                             otherwise 0 */
} xxbyte_wasm_entity;

typedef struct xxbyte_wasm_export {
    xxbyte_wasm_string name;
    uint32_t kind; /* XXBYTE_WASM_EXTERNAL_* */
    uint32_t index;
} xxbyte_wasm_export;

typedef struct xxbyte_wasm_module {
    const uint8_t *data; /* borrowed */
    uint32_t size;
    uint32_t sections;     /* bit id for each section present */
    uint32_t bad_sections; /* bit id for each malformed one (see above) */
    uint32_t type_count;
    const xxbyte_wasm_type *types;
    uint32_t import_count;
    const xxbyte_wasm_import *imports;
    uint32_t function_count; /* imported and defined */
    uint32_t imported_function_count;
    const xxbyte_wasm_func *functions;
    uint32_t table_count;
    uint32_t imported_table_count;
    const xxbyte_wasm_entity *tables;
    uint32_t memory_count;
    uint32_t imported_memory_count;
    const xxbyte_wasm_entity *memories;
    uint32_t global_count;
    uint32_t imported_global_count;
    const xxbyte_wasm_entity *globals;
    uint32_t tag_count;
    uint32_t imported_tag_count;
    const xxbyte_wasm_entity *tags;
    uint32_t export_count;
    const xxbyte_wasm_export *exports;
    uint32_t start; /* start function, XXBYTE_WASM_NONE if none */
    uint32_t elem_count;
    uint32_t data_count;         /* data count section, XXBYTE_WASM_NONE
                                    if none */
    uint32_t data_segment_count; /* entries of the data section */
    xxbyte_wasm_section code;    /* the code section (id 0 if none); when
                                    the file is cut inside it, clipped to
                                    the end of the file */
    uint32_t body_count;         /* bodies read from it */
    void *internal;              /* names; owned */
} xxbyte_wasm_module;

XXBYTE_API xxbyte_status xxbyte_wasm_module_parse(const uint8_t *data, size_t size, xxbyte_wasm_module *module);
XXBYTE_API void xxbyte_wasm_module_free(xxbyte_wasm_module *module);

/* Reads the value type at *offset (a param, result, global or table type
 * of the model) as an operand (see the operand description above) and
 * advances *offset past it. */
XXBYTE_API xxbyte_status xxbyte_wasm_module_value_type(const xxbyte_wasm_module *module, uint32_t *offset, xxbyte_operand *type);

/* Reads the struct or array field at *offset: its storage type (packed i8
 * and i16 are XXBYTE_WASM_INDEX_VALTYPE operands with value
 * XXBYTE_WASM_TYPE_I8 or XXBYTE_WASM_TYPE_I16) and *is_mutable, and
 * advances *offset past it. */
XXBYTE_API xxbyte_status xxbyte_wasm_module_field(const xxbyte_wasm_module *module, uint32_t *offset, xxbyte_operand *type, uint32_t *is_mutable);

/*
 * Name spaces: the name subsection ids. LOCAL and LABEL names are indexed
 * by function and local (params first) or label, FIELD names by type and
 * field; the others ignore outer. A label index counts the block, loop, if,
 * try and try_table instructions of a function body in order, from 0.
 */
#define XXBYTE_WASM_NAME_MODULE 0u
#define XXBYTE_WASM_NAME_FUNCTION 1u
#define XXBYTE_WASM_NAME_LOCAL 2u
#define XXBYTE_WASM_NAME_LABEL 3u
#define XXBYTE_WASM_NAME_TYPE 4u
#define XXBYTE_WASM_NAME_TABLE 5u
#define XXBYTE_WASM_NAME_MEMORY 6u
#define XXBYTE_WASM_NAME_GLOBAL 7u
#define XXBYTE_WASM_NAME_ELEM 8u
#define XXBYTE_WASM_NAME_DATA 9u
#define XXBYTE_WASM_NAME_FIELD 10u
#define XXBYTE_WASM_NAME_TAG 11u

/*
 * The name of an entity as wasm2wat (WABT 1.0.42) prints it without
 * --generate-names: "$" and the name; a name that repeats one given before
 * in its space (for locals and labels, in its function; for fields, in its
 * type) gets the first free suffix ".1", ".2", ... in the order the names
 * were applied; every byte that is not a WAT identifier character becomes
 * '_' ("a b" is "$a_b"). An empty name names nothing. A later name for the
 * same index replaces the earlier one; a linking symbol names a function
 * only if no name did. Writes a NUL-terminated string and returns its
 * length, or returns 0 when the entity has no name or the name does not fit
 * in buffer_size - 1 characters.
 */
XXBYTE_API size_t xxbyte_wasm_module_name(const xxbyte_wasm_module *module, uint32_t space, uint32_t outer, uint32_t index, char *buffer, size_t buffer_size);

/* The raw bytes of that name, without "$", suffix or replacement: 1 and
 * *name, or 0 (*name zeroed) when there is none. */
XXBYTE_API int xxbyte_wasm_module_raw_name(const xxbyte_wasm_module *module, uint32_t space, uint32_t outer, uint32_t index, xxbyte_wasm_string *name);

/*
 * ---- Name resolver
 *
 * xxbyte_wasm_resolve_name is an xxbyte_resolver whose context is an
 * xxbyte_wasm_names. With its module's names it prints what wasm2wat prints
 * for a named index: "call $printf", "local.get $x", "global.get
 * $__stack_pointer", "call_indirect (type $sig)", "struct.get $point $x",
 * "throw $exc"; unnamed indexes keep their number.
 *
 * Function context: set function to the index of the function whose body
 * is formatted (xxbyte_wasm_names_begin does) for its local and label
 * names. After xxbyte_wasm_names_begin, call xxbyte_wasm_names_step once
 * for every instruction of the body, in order, after formatting it; the
 * resolver then knows the open blocks and prints a label depth as wasm2wat
 * does: "br_if 0 (;@1;)", where @N is the nesting level of the target (@0
 * the function body), "br 3 (; INVALID ;)" for a depth beyond the function,
 * or the label's name when the name section names it ("br $exit"; wasm2wat
 * ignores label names). try_table catch labels count from outside the
 * try_table, as in the binary format; delegate prints its plain depth, as
 * wasm2wat does.
 *
 * A block type that is a type index ("block (type 3)") prints as the index
 * or type name followed by the signature of a function type, so that the
 * text reads "block (type 3) (param i32) (result i64)" (wasm2wat prints
 * only the "(param ...) (result ...)" part).
 *
 * Text that does not fit in buffer_size - 1 characters is reported as
 * snprintf does (truncated text, full length returned), so the formatter
 * asks again with a larger buffer and long names (mangled Rust or C++
 * symbols) print in full up to XXBYTE_RESOLVER_NAME_LIMIT.
 *
 * Differences from wasm2wat 1.0.42 that remain:
 *   - table 0 of call_indirect, return_call_indirect, table.init and
 *     table.copy is omitted by the decoder, so the resolver never sees it;
 *     wasm2wat prints it when the table is named ("call_indirect $tbl
 *     (type $sig)", "table.copy $t $t");
 *   - a concrete heap type ("(ref $t)", "(ref null $t)", also in params,
 *     results and locals), the type of call_ref and return_call_ref and
 *     the heap type of ref.null print the type's name; wasm2wat prints the
 *     index there ("(ref 0)", "call_ref 0") although the type is named;
 *   - label names (see above) and field names (struct.get $point $x),
 *     which wasm2wat does not read.
 */
typedef struct xxbyte_wasm_names {
    const xxbyte_wasm_module *module; /* borrowed */
    uint32_t function;                /* current function, or
                                         XXBYTE_WASM_NONE */
    uint32_t tracking;                /* nonzero after xxbyte_wasm_names_begin */
    uint32_t depth;                   /* open blocks of the body */
    uint32_t labels;                  /* labels opened so far */
    uint32_t *stack;                  /* label index of each open block */
    uint32_t stack_capacity;
} xxbyte_wasm_names;

/* Sets up a context for module (no allocation); function NONE. */
XXBYTE_API void xxbyte_wasm_names_init(xxbyte_wasm_names *names, const xxbyte_wasm_module *module);
/* Starts the body of function: no open block, label tracking on. */
XXBYTE_API void xxbyte_wasm_names_begin(xxbyte_wasm_names *names, uint32_t function);
/* Accounts for a formatted instruction: block, loop, if, try and try_table
 * open a block, end and delegate close one. XXBYTE_STATUS_OUT_OF_MEMORY when
 * the label stack cannot grow (depths stay right, label names are lost),
 * XXBYTE_STATUS_INVALID_ARGUMENT for a NULL argument. */
XXBYTE_API xxbyte_status xxbyte_wasm_names_step(xxbyte_wasm_names *names, const xxbyte_instruction *instruction);
/* Releases the label stack. */
XXBYTE_API void xxbyte_wasm_names_free(xxbyte_wasm_names *names);
XXBYTE_API size_t xxbyte_wasm_resolve_name(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/*
 * ---- Container listing
 *
 * xxbyte_disasm_file(XXBYTE_FAMILY_WASM, ...) lists a module (.wasm, also
 * a relocatable object): "(module $name)" ("(module)" without a module
 * name), then for every function body (imported functions have none),
 * after an empty line, a header in wasm2wat's syntax with the names
 * resolved as above,
 *
 *     (func $add (type 1) (param $x i32) (param i32) (result i32))
 *     (local $tmp i32) (local f64 f64)
 *
 * (the locals line only when the body declares locals; "(func (;3;) ..."
 * for an unnamed function), then the instructions at their module offsets
 * through xxbyte_wasm_resolve_name. block, loop, if, try and try_table lines
 * end with wasm2wat's "  ;; label = @N" ("  ;; label = $name" for a named
 * label). A body that declares more than 4096 locals gets a summary line
 * ";; locals: i32 x 5000, f64 x 2" instead of the locals line. Every
 * instruction of the body is listed, also an else whose branch is empty
 * (wasm2wat drops it) and the final end. XXBYTE_MODE_AUTO decodes in mode 0;
 * another mode is used for the bodies. A body whose locals do not parse in
 * the mode is listed as its header and "  ;; malformed locals".
 * The listing goes on past defects that leave the code section readable and
 * then returns the module's status as xxbyte_wasm_module_parse gives it, or
 * XXBYTE_STATUS_INVALID_INSTRUCTION or XXBYTE_STATUS_TRUNCATED for a
 * malformed or cut code section.
 */

#ifdef __cplusplus
}
#endif

#endif
