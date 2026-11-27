/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_BEAM_H
#define XXBYTE_XXBYTE_BEAM_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_BEAM
#error "the Erlang/OTP BEAM decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Erlang/OTP BEAM generic instructions, as the compiler stores them in the
 * "Code" chunk of a .beam file (lib/compiler/src/genop.tab): an opcode byte
 * followed by "arity" operands in the compact term encoding.
 *
 * Modes. A mode is an Erlang/OTP major release, XXBYTE_BEAM_MODE_OTP(n),
 * from 17 to 29; it sets the highest opcode the decoder accepts (the "max
 * opcode" of genop.tab in that release):
 *   17-19: 158   20: 159 is_tagged_tuple   21: 163 build_stacktrace..get_tl
 *   22: 168 put_tuple2..bs_set_position   23: 170 swap, bs_start_match4
 *   24: 176 make_fun3, init_yregs, recv_marker_*
 *   25: 180 bs_create_bin, call_fun2, nif_start, badrecord
 *   26: 182 update_record, bs_match   27: 183 executable_line
 *   28: 184 debug_line   29: 191 bif3 and the native-record instructions
 * Type-tagged registers (extended tag 5) exist from OTP 25 and are
 * XXBYTE_STATUS_INVALID_INSTRUCTION in earlier modes. Opcodes that later
 * releases deprecated (marked "-" in genop.tab: put_tuple, bs_put_integer,
 * allocate_zero, m_plus, ...) still decode in every mode, as beam_disasm
 * decodes them, so that files of older compilers list completely.
 * XXBYTE_BEAM_MODE_DEFAULT (0) is the newest release, OTP 29. Any other
 * value is XXBYTE_STATUS_UNSUPPORTED_MODE.
 */
#define XXBYTE_BEAM_MODE_DEFAULT 0u
#define XXBYTE_BEAM_MODE_OTP(release) ((uint32_t)(release))
#define XXBYTE_BEAM_MODE_OTP_MIN 17u
#define XXBYTE_BEAM_MODE_OTP_MAX 29u
/* The first release with type-tagged registers and the Type chunk. */
#define XXBYTE_BEAM_MODE_TYPES 25u
/* The highest opcode of any mode. */
#define XXBYTE_BEAM_OPCODE_MAX 191u

/*
 * Mnemonic identifiers: instruction.id and instruction.opcode are both the
 * genop.tab opcode number, and the mnemonic is the genop.tab name
 * ("catch" and "try" without quotes).
 */
typedef enum xxbyte_beam_op {
    XXBYTE_BEAM_OP_INVALID = 0,
    XXBYTE_BEAM_OP_LABEL = 1,
    XXBYTE_BEAM_OP_FUNC_INFO = 2,
    XXBYTE_BEAM_OP_INT_CODE_END = 3,
    XXBYTE_BEAM_OP_CALL = 4,
    XXBYTE_BEAM_OP_CALL_LAST = 5,
    XXBYTE_BEAM_OP_CALL_ONLY = 6,
    XXBYTE_BEAM_OP_CALL_EXT = 7,
    XXBYTE_BEAM_OP_CALL_EXT_LAST = 8,
    XXBYTE_BEAM_OP_BIF0 = 9,
    XXBYTE_BEAM_OP_BIF1 = 10,
    XXBYTE_BEAM_OP_BIF2 = 11,
    XXBYTE_BEAM_OP_ALLOCATE = 12,
    XXBYTE_BEAM_OP_ALLOCATE_HEAP = 13,
    XXBYTE_BEAM_OP_ALLOCATE_ZERO = 14,
    XXBYTE_BEAM_OP_ALLOCATE_HEAP_ZERO = 15,
    XXBYTE_BEAM_OP_TEST_HEAP = 16,
    XXBYTE_BEAM_OP_INIT = 17,
    XXBYTE_BEAM_OP_DEALLOCATE = 18,
    XXBYTE_BEAM_OP_RETURN = 19,
    XXBYTE_BEAM_OP_SEND = 20,
    XXBYTE_BEAM_OP_REMOVE_MESSAGE = 21,
    XXBYTE_BEAM_OP_TIMEOUT = 22,
    XXBYTE_BEAM_OP_LOOP_REC = 23,
    XXBYTE_BEAM_OP_LOOP_REC_END = 24,
    XXBYTE_BEAM_OP_WAIT = 25,
    XXBYTE_BEAM_OP_WAIT_TIMEOUT = 26,
    XXBYTE_BEAM_OP_M_PLUS = 27,
    XXBYTE_BEAM_OP_M_MINUS = 28,
    XXBYTE_BEAM_OP_M_TIMES = 29,
    XXBYTE_BEAM_OP_M_DIV = 30,
    XXBYTE_BEAM_OP_INT_DIV = 31,
    XXBYTE_BEAM_OP_INT_REM = 32,
    XXBYTE_BEAM_OP_INT_BAND = 33,
    XXBYTE_BEAM_OP_INT_BOR = 34,
    XXBYTE_BEAM_OP_INT_BXOR = 35,
    XXBYTE_BEAM_OP_INT_BSL = 36,
    XXBYTE_BEAM_OP_INT_BSR = 37,
    XXBYTE_BEAM_OP_INT_BNOT = 38,
    XXBYTE_BEAM_OP_IS_LT = 39,
    XXBYTE_BEAM_OP_IS_GE = 40,
    XXBYTE_BEAM_OP_IS_EQ = 41,
    XXBYTE_BEAM_OP_IS_NE = 42,
    XXBYTE_BEAM_OP_IS_EQ_EXACT = 43,
    XXBYTE_BEAM_OP_IS_NE_EXACT = 44,
    XXBYTE_BEAM_OP_IS_INTEGER = 45,
    XXBYTE_BEAM_OP_IS_FLOAT = 46,
    XXBYTE_BEAM_OP_IS_NUMBER = 47,
    XXBYTE_BEAM_OP_IS_ATOM = 48,
    XXBYTE_BEAM_OP_IS_PID = 49,
    XXBYTE_BEAM_OP_IS_REFERENCE = 50,
    XXBYTE_BEAM_OP_IS_PORT = 51,
    XXBYTE_BEAM_OP_IS_NIL = 52,
    XXBYTE_BEAM_OP_IS_BINARY = 53,
    XXBYTE_BEAM_OP_IS_CONSTANT = 54,
    XXBYTE_BEAM_OP_IS_LIST = 55,
    XXBYTE_BEAM_OP_IS_NONEMPTY_LIST = 56,
    XXBYTE_BEAM_OP_IS_TUPLE = 57,
    XXBYTE_BEAM_OP_TEST_ARITY = 58,
    XXBYTE_BEAM_OP_SELECT_VAL = 59,
    XXBYTE_BEAM_OP_SELECT_TUPLE_ARITY = 60,
    XXBYTE_BEAM_OP_JUMP = 61,
    XXBYTE_BEAM_OP_CATCH = 62,
    XXBYTE_BEAM_OP_CATCH_END = 63,
    XXBYTE_BEAM_OP_MOVE = 64,
    XXBYTE_BEAM_OP_GET_LIST = 65,
    XXBYTE_BEAM_OP_GET_TUPLE_ELEMENT = 66,
    XXBYTE_BEAM_OP_SET_TUPLE_ELEMENT = 67,
    XXBYTE_BEAM_OP_PUT_STRING = 68,
    XXBYTE_BEAM_OP_PUT_LIST = 69,
    XXBYTE_BEAM_OP_PUT_TUPLE = 70,
    XXBYTE_BEAM_OP_PUT = 71,
    XXBYTE_BEAM_OP_BADMATCH = 72,
    XXBYTE_BEAM_OP_IF_END = 73,
    XXBYTE_BEAM_OP_CASE_END = 74,
    XXBYTE_BEAM_OP_CALL_FUN = 75,
    XXBYTE_BEAM_OP_MAKE_FUN = 76,
    XXBYTE_BEAM_OP_IS_FUNCTION = 77,
    XXBYTE_BEAM_OP_CALL_EXT_ONLY = 78,
    XXBYTE_BEAM_OP_BS_START_MATCH = 79,
    XXBYTE_BEAM_OP_BS_GET_INTEGER = 80,
    XXBYTE_BEAM_OP_BS_GET_FLOAT = 81,
    XXBYTE_BEAM_OP_BS_GET_BINARY = 82,
    XXBYTE_BEAM_OP_BS_SKIP_BITS = 83,
    XXBYTE_BEAM_OP_BS_TEST_TAIL = 84,
    XXBYTE_BEAM_OP_BS_SAVE = 85,
    XXBYTE_BEAM_OP_BS_RESTORE = 86,
    XXBYTE_BEAM_OP_BS_INIT = 87,
    XXBYTE_BEAM_OP_BS_FINAL = 88,
    XXBYTE_BEAM_OP_BS_PUT_INTEGER = 89,
    XXBYTE_BEAM_OP_BS_PUT_BINARY = 90,
    XXBYTE_BEAM_OP_BS_PUT_FLOAT = 91,
    XXBYTE_BEAM_OP_BS_PUT_STRING = 92,
    XXBYTE_BEAM_OP_BS_NEED_BUF = 93,
    XXBYTE_BEAM_OP_FCLEARERROR = 94,
    XXBYTE_BEAM_OP_FCHECKERROR = 95,
    XXBYTE_BEAM_OP_FMOVE = 96,
    XXBYTE_BEAM_OP_FCONV = 97,
    XXBYTE_BEAM_OP_FADD = 98,
    XXBYTE_BEAM_OP_FSUB = 99,
    XXBYTE_BEAM_OP_FMUL = 100,
    XXBYTE_BEAM_OP_FDIV = 101,
    XXBYTE_BEAM_OP_FNEGATE = 102,
    XXBYTE_BEAM_OP_MAKE_FUN2 = 103,
    XXBYTE_BEAM_OP_TRY = 104,
    XXBYTE_BEAM_OP_TRY_END = 105,
    XXBYTE_BEAM_OP_TRY_CASE = 106,
    XXBYTE_BEAM_OP_TRY_CASE_END = 107,
    XXBYTE_BEAM_OP_RAISE = 108,
    XXBYTE_BEAM_OP_BS_INIT2 = 109,
    XXBYTE_BEAM_OP_BS_BITS_TO_BYTES = 110,
    XXBYTE_BEAM_OP_BS_ADD = 111,
    XXBYTE_BEAM_OP_APPLY = 112,
    XXBYTE_BEAM_OP_APPLY_LAST = 113,
    XXBYTE_BEAM_OP_IS_BOOLEAN = 114,
    XXBYTE_BEAM_OP_IS_FUNCTION2 = 115,
    XXBYTE_BEAM_OP_BS_START_MATCH2 = 116,
    XXBYTE_BEAM_OP_BS_GET_INTEGER2 = 117,
    XXBYTE_BEAM_OP_BS_GET_FLOAT2 = 118,
    XXBYTE_BEAM_OP_BS_GET_BINARY2 = 119,
    XXBYTE_BEAM_OP_BS_SKIP_BITS2 = 120,
    XXBYTE_BEAM_OP_BS_TEST_TAIL2 = 121,
    XXBYTE_BEAM_OP_BS_SAVE2 = 122,
    XXBYTE_BEAM_OP_BS_RESTORE2 = 123,
    XXBYTE_BEAM_OP_GC_BIF1 = 124,
    XXBYTE_BEAM_OP_GC_BIF2 = 125,
    XXBYTE_BEAM_OP_BS_FINAL2 = 126,
    XXBYTE_BEAM_OP_BS_BITS_TO_BYTES2 = 127,
    XXBYTE_BEAM_OP_PUT_LITERAL = 128,
    XXBYTE_BEAM_OP_IS_BITSTR = 129,
    XXBYTE_BEAM_OP_BS_CONTEXT_TO_BINARY = 130,
    XXBYTE_BEAM_OP_BS_TEST_UNIT = 131,
    XXBYTE_BEAM_OP_BS_MATCH_STRING = 132,
    XXBYTE_BEAM_OP_BS_INIT_WRITABLE = 133,
    XXBYTE_BEAM_OP_BS_APPEND = 134,
    XXBYTE_BEAM_OP_BS_PRIVATE_APPEND = 135,
    XXBYTE_BEAM_OP_TRIM = 136,
    XXBYTE_BEAM_OP_BS_INIT_BITS = 137,
    XXBYTE_BEAM_OP_BS_GET_UTF8 = 138,
    XXBYTE_BEAM_OP_BS_SKIP_UTF8 = 139,
    XXBYTE_BEAM_OP_BS_GET_UTF16 = 140,
    XXBYTE_BEAM_OP_BS_SKIP_UTF16 = 141,
    XXBYTE_BEAM_OP_BS_GET_UTF32 = 142,
    XXBYTE_BEAM_OP_BS_SKIP_UTF32 = 143,
    XXBYTE_BEAM_OP_BS_UTF8_SIZE = 144,
    XXBYTE_BEAM_OP_BS_PUT_UTF8 = 145,
    XXBYTE_BEAM_OP_BS_UTF16_SIZE = 146,
    XXBYTE_BEAM_OP_BS_PUT_UTF16 = 147,
    XXBYTE_BEAM_OP_BS_PUT_UTF32 = 148,
    XXBYTE_BEAM_OP_ON_LOAD = 149,
    XXBYTE_BEAM_OP_RECV_MARK = 150,
    XXBYTE_BEAM_OP_RECV_SET = 151,
    XXBYTE_BEAM_OP_GC_BIF3 = 152,
    XXBYTE_BEAM_OP_LINE = 153,
    XXBYTE_BEAM_OP_PUT_MAP_ASSOC = 154,
    XXBYTE_BEAM_OP_PUT_MAP_EXACT = 155,
    XXBYTE_BEAM_OP_IS_MAP = 156,
    XXBYTE_BEAM_OP_HAS_MAP_FIELDS = 157,
    XXBYTE_BEAM_OP_GET_MAP_ELEMENTS = 158,
    XXBYTE_BEAM_OP_IS_TAGGED_TUPLE = 159,
    XXBYTE_BEAM_OP_BUILD_STACKTRACE = 160,
    XXBYTE_BEAM_OP_RAW_RAISE = 161,
    XXBYTE_BEAM_OP_GET_HD = 162,
    XXBYTE_BEAM_OP_GET_TL = 163,
    XXBYTE_BEAM_OP_PUT_TUPLE2 = 164,
    XXBYTE_BEAM_OP_BS_GET_TAIL = 165,
    XXBYTE_BEAM_OP_BS_START_MATCH3 = 166,
    XXBYTE_BEAM_OP_BS_GET_POSITION = 167,
    XXBYTE_BEAM_OP_BS_SET_POSITION = 168,
    XXBYTE_BEAM_OP_SWAP = 169,
    XXBYTE_BEAM_OP_BS_START_MATCH4 = 170,
    XXBYTE_BEAM_OP_MAKE_FUN3 = 171,
    XXBYTE_BEAM_OP_INIT_YREGS = 172,
    XXBYTE_BEAM_OP_RECV_MARKER_BIND = 173,
    XXBYTE_BEAM_OP_RECV_MARKER_CLEAR = 174,
    XXBYTE_BEAM_OP_RECV_MARKER_RESERVE = 175,
    XXBYTE_BEAM_OP_RECV_MARKER_USE = 176,
    XXBYTE_BEAM_OP_BS_CREATE_BIN = 177,
    XXBYTE_BEAM_OP_CALL_FUN2 = 178,
    XXBYTE_BEAM_OP_NIF_START = 179,
    XXBYTE_BEAM_OP_BADRECORD = 180,
    XXBYTE_BEAM_OP_UPDATE_RECORD = 181,
    XXBYTE_BEAM_OP_BS_MATCH = 182,
    XXBYTE_BEAM_OP_EXECUTABLE_LINE = 183,
    XXBYTE_BEAM_OP_DEBUG_LINE = 184,
    XXBYTE_BEAM_OP_BIF3 = 185,
    XXBYTE_BEAM_OP_IS_ANY_NATIVE_RECORD = 186,
    XXBYTE_BEAM_OP_IS_NATIVE_RECORD = 187,
    XXBYTE_BEAM_OP_GET_RECORD_ELEMENTS = 188,
    XXBYTE_BEAM_OP_PUT_RECORD = 189,
    XXBYTE_BEAM_OP_IS_RECORD_ACCESSIBLE = 190,
    XXBYTE_BEAM_OP_GET_RECORD_FIELD = 191,
} xxbyte_beam_op;

/* The highest opcode of mode, or 0 when mode is not a mode. */
XXBYTE_API uint32_t xxbyte_beam_max_opcode(uint32_t mode);
/* The oldest mode (OTP release) whose instruction set reaches max_opcode,
 * the field of a Code chunk header; XXBYTE_BEAM_MODE_OTP_MAX when
 * max_opcode is above every release. */
XXBYTE_API uint32_t xxbyte_beam_mode_for_max_opcode(uint32_t max_opcode);
/* The mnemonic of opcode, or NULL when it is not 1..XXBYTE_BEAM_OPCODE_MAX. */
XXBYTE_API const char *xxbyte_beam_mnemonic(uint32_t opcode);
/* The genop.tab arity of opcode (0 for an invalid opcode). */
XXBYTE_API uint32_t xxbyte_beam_arity(uint32_t opcode);
/* The OTP release whose genop.tab added opcode (17 for opcodes that
 * predate OTP 17), or 0 for an invalid opcode. */
XXBYTE_API uint32_t xxbyte_beam_opcode_since(uint32_t opcode);
/* The OTP release whose genop.tab marks opcode deprecated ("-"), 17 for
 * those already deprecated in OTP 17, or 0 when it is current. */
XXBYTE_API uint32_t xxbyte_beam_opcode_deprecated(uint32_t opcode);

/*
 * The compact term encoding. Every operand is one tagged term; its low three
 * bits are the tag: 0 u (literal), 1 i (integer), 2 a (atom), 3 x (X
 * register), 4 y (Y register), 5 f (label), 6 h (character), 7 z
 * (extended). A value below 16 is stored in the high nibble (bit 3 clear);
 * one below 2048 in bits 5-7 and the next byte (bits 3:4 = 01); a longer one
 * in 2 to 8 big-endian bytes counted by bits 5-7 (bits 3:4 = 11, count =
 * bits 5-7 + 2) or, when bits 5-7 are all set, in a byte count given by a
 * nested u term + 9 (which must fit 31 bits). An i value is two's
 * complement and may be a bignum; a u value may also exceed 64 bits; the
 * other tags must fit 63 bits or the term is invalid, as the OTP loader
 * (erts beam_file.c) has it. The extended tag's value (read the same way)
 * selects: 0 a float (8 big-endian IEEE bytes; beam_disasm reads it, the
 * OTP 17+ loader rejects it), 1 an extended list (a u count, then that many
 * terms; the last operand of the instruction), 2 a floating-point register
 * (a u index), 3 an allocation list (a u count of at most 3, then u kind and
 * u count pairs: 0 words, 1 floats, 2 funs), 4 a literal-table index (u), 5
 * a type-tagged register (OTP 25+: an x or y term, then a u Type-chunk
 * index). Lists cannot nest and cannot hold an allocation list.
 *
 * Operands. Every operand's extra holds the term's tag in its low 8 bits
 * (XXBYTE_BEAM_OPERAND_TAG, an xxbyte_beam_tag) and an auxiliary value
 * above (XXBYTE_BEAM_OPERAND_AUX). size is the encoded size in bytes,
 * except for a FLOAT (8).
 *   u: IMMEDIATE (unsigned), or the INDEX described below for the operands
 *      that index a table. A u of 2^63 or more is BYTES.
 *   i: IMMEDIATE with SIGNED; an integer that does not fit int64_t is BYTES
 *      with SIGNED: value is the offset of its big-endian two's-complement
 *      bytes from the instruction's first byte, size their count (bytes of
 *      a u bignum are unsigned).
 *   a: INDEX XXBYTE_BEAM_INDEX_ATOM; atom 0 is [] (nil).
 *   x, y: REGISTER (tag X or Y); value = register number.
 *   f: INDEX XXBYTE_INDEX_LABEL (value = label number), or, when a decode
 *      context knows the label's address, BRANCH (value = that address,
 *      aux = label number). Label 0 means "no fail label" and stays an
 *      INDEX.
 *   h: IMMEDIATE (tag H), a character.
 *   float: FLOAT (size 8, value = IEEE bits).
 *   list: IMMEDIATE (tag LIST), value = element count; instruction
 *      table_count is the count and table_offset the offset of the first
 *      element from instruction.code. Read elements with
 *      xxbyte_beam_list_entry.
 *   fr: REGISTER (tag FR).
 *   allocation list: IMMEDIATE (tag ALLOC), value = the words entry (kind
 *      0), aux = entry count; xxbyte_beam_alloc_list reads every entry.
 *   literal: INDEX XXBYTE_INDEX_CONSTANT (literal-table index).
 *   type-tagged register: REGISTER (tag TR_X or TR_Y), aux = type index.
 * u operands that index a table (by opcode and position, as beam_disasm
 * resolves them): the import of call_ext, call_ext_last, call_ext_only and
 * of the bif0-3 and gc_bif1-3 BIF: INDEX XXBYTE_INDEX_IMPORT (ImpT); the
 * fun of make_fun2, make_fun3 and call_fun2: INDEX XXBYTE_INDEX_FUNCTION
 * (FunT); the string of bs_put_string, put_string and bs_match_string, and
 * the u value of a bs_create_bin string segment: INDEX XXBYTE_INDEX_STRING
 * (a StrT byte offset); the operand of line: INDEX XXBYTE_BEAM_INDEX_LINE
 * (Line chunk index, 0 = none). The label instruction's operand (the label
 * it defines) is a plain IMMEDIATE.
 *
 * Groups: return RETURN|STOP; call, call_ext, call_fun, call_fun2 and apply
 * CALL; call_last, call_only, call_ext_last, call_ext_only and apply_last
 * CALL|RETURN|STOP (tail calls); bif0-3 and gc_bif1-3 CALL; jump,
 * loop_rec_end and wait JUMP|STOP; select_val and select_tuple_arity
 * SWITCH|STOP; func_info, badmatch, if_end, case_end, try_case_end, raise,
 * raw_raise and badrecord THROW|STOP; int_code_end STOP. The tests (is_*,
 * test_arity, the bs_* matching, has_map_fields, get_map_elements,
 * is_tagged_tuple, bs_match, the native-record tests, ...) and the other
 * instructions with a fail label (BIFs, fadd.., put_map_*, bs_create_bin,
 * the old bs_put_*, loop_rec, wait_timeout, ...) are CONDITIONAL when their
 * first label operand is not 0 (label 0 raises an exception instead).
 * Without a decode context the transfer targets are label numbers (INDEX
 * LABEL), not addresses; see xxbyte_beam_context. xxbyte_switch_entry is
 * not available for this family (select targets need the context); use
 * xxbyte_beam_list_entry.
 *
 * Text: the mnemonic, then the operands separated by ", ", written like
 * the terms of beam_disasm where no table is needed:
 *   u: 5    i: {integer,-1}    a: {a,3} (nil: nil)    x: {x,0}    y: {y,1}
 *   f: {f,7}    h: {h,65}    float: {float,1.5} (Erlang float syntax)
 *   fr: {fr,0}    literal: {lit,2}    type-tagged: {tr,{x,0},4}
 *   list: {list,[{integer,1},{f,4},{a,5},{f,6}]}
 *   allocation list: {alloc,[{words,3},{floats,1}]}
 *   import: {import,2}    fun: {fun,0}    string, line: decimal
 * for example "move {x,0}, {y,1}", "call_ext 1, {import,0}",
 * "select_val {x,0}, {f,3}, {list,[{integer,1},{f,5}]}",
 * "is_eq_exact {f,9}, {x,0}, {a,4}", "label 2".
 * A resolver replaces the whole text of an operand. It is asked about every
 * INDEX operand except nil and every BRANCH operand, about type-tagged
 * REGISTER operands, and (through xxbyte_fmt_resolve_entry) about every such
 * element of a list. xxbyte_disasm_file uses it to print beam_disasm's
 * resolved forms: {atom,ok}, {extfunc,erlang,display,1}, {literal,[1,2]},
 * {M,F,A} for a call target or a fun, {tr,{x,0},integer(0..255)}, and the
 * locations of line instructions.
 */
typedef enum xxbyte_beam_tag {
    XXBYTE_BEAM_TAG_U = 0,
    XXBYTE_BEAM_TAG_I = 1,
    XXBYTE_BEAM_TAG_A = 2,
    XXBYTE_BEAM_TAG_X = 3,
    XXBYTE_BEAM_TAG_Y = 4,
    XXBYTE_BEAM_TAG_F = 5,
    XXBYTE_BEAM_TAG_H = 6,
    XXBYTE_BEAM_TAG_FLOAT = 7,    /* extended 0 */
    XXBYTE_BEAM_TAG_LIST = 8,     /* extended 1 */
    XXBYTE_BEAM_TAG_FR = 9,       /* extended 2 */
    XXBYTE_BEAM_TAG_ALLOC = 10,   /* extended 3 */
    XXBYTE_BEAM_TAG_LITERAL = 11, /* extended 4 */
    XXBYTE_BEAM_TAG_TR_X = 12,    /* extended 5 with an x register */
    XXBYTE_BEAM_TAG_TR_Y = 13     /* extended 5 with a y register */
} xxbyte_beam_tag;

#define XXBYTE_BEAM_OPERAND_TAG(operand) ((uint32_t)((operand)->extra & 0xFFu))
#define XXBYTE_BEAM_OPERAND_AUX(operand) ((uint64_t)((operand)->extra >> 8))

/* INDEX kinds of this family, besides XXBYTE_INDEX_LABEL, CONSTANT (literal
 * table), IMPORT (ImpT), FUNCTION (FunT) and STRING (StrT offset). */
typedef enum xxbyte_beam_index_kind {
    XXBYTE_BEAM_INDEX_ATOM = XXBYTE_INDEX_FAMILY_FIRST, /* atom table, 1-based; 0 = nil */
    XXBYTE_BEAM_INDEX_LINE                              /* Line chunk item, 1-based; 0 = none */
} xxbyte_beam_index_kind;

/*
 * Decode context for xxbyte_decode_ex and xxbyte_disasm_buf_ex: the label
 * table. labels[n] is the address of the "label n" instruction, or
 * XXBYTE_BEAM_NO_ADDRESS. A label operand n with n != 0, n < label_count
 * and a known address decodes as a BRANCH to it. The decoder never needs
 * the context otherwise; NULL is valid.
 */
#define XXBYTE_BEAM_NO_ADDRESS UINT64_MAX
typedef struct xxbyte_beam_context {
    const uint64_t *labels;
    size_t label_count;
} xxbyte_beam_context;

/*
 * Fills labels[0..label_count) by a linear sweep of code[0..code_size)
 * starting at address: every "label n" with n < label_count stores its
 * address, the rest stay XXBYTE_BEAM_NO_ADDRESS. Bytes that do not decode
 * are skipped one at a time, as a listing does. Returns
 * XXBYTE_STATUS_UNSUPPORTED_MODE or INVALID_ARGUMENT for bad arguments,
 * else OK.
 */
XXBYTE_API xxbyte_status xxbyte_beam_scan_labels(uint32_t mode, const uint8_t *code, size_t code_size, uint64_t address, uint64_t *labels, size_t label_count);

/*
 * Reads one compact term at code[*offset] (code_size bounds it) into
 * *operand, with the operand model above (an f term is always an INDEX
 * LABEL, a u term an IMMEDIATE; BYTES values are offsets from code[0]),
 * and advances *offset. in_list nonzero reads a list element (lists and
 * allocation lists are then invalid). A list term reads only its count:
 * the elements follow at the new *offset. Returns TRUNCATED, or
 * INVALID_INSTRUCTION for a malformed term or one the mode does not have.
 */
XXBYTE_API xxbyte_status xxbyte_beam_read_term(uint32_t mode, const uint8_t *code, size_t code_size, size_t *offset, int in_list, xxbyte_operand *operand);

/*
 * Element index of the extended list of a decoded instruction (index <
 * table_count), read from instruction->code, in the operand model of
 * xxbyte_decode (labels are INDEX LABEL; BYTES offsets are from the
 * instruction's first byte; u elements of a bs_create_bin string segment
 * are INDEX STRING).
 */
XXBYTE_API xxbyte_status xxbyte_beam_list_entry(const xxbyte_instruction *instruction, uint32_t index, xxbyte_operand *entry);

/* The allocation list of operand operand_index (tag ALLOC): words, floats
 * and funs needed (each may be NULL). */
XXBYTE_API xxbyte_status xxbyte_beam_alloc_list(const xxbyte_instruction *instruction, uint32_t operand_index, uint64_t *words, uint64_t *floats, uint64_t *funs);

/*
 * Containers. xxbyte_disasm_file lists a .beam file: an IFF form "FOR1"
 * <size> "BEAM" of chunks (4-byte id, 4-byte big-endian size, data padded
 * to 4 bytes), or such a file gzip-compressed (the "compressed" compiler
 * option). It reads the atom table (AtU8, the OTP 28+ long-atom AtU8 with
 * a negative count, or the old Latin-1 Atom), Code (header: sub-size,
 * instruction set 0, max opcode, label count, function count), StrT, ImpT,
 * ExpT, LocT, FunT, LitT (zlib-compressed up to OTP 27, stored from OTP
 * 28), Line (version 0), Type (versions 1 to 4: the stored entry with its
 * type bits renumbered to the OTP 29 layout; beam_disasm of OTP 29 instead
 * re-derives old entries through beam_types, which can drop or add a
 * bitstring unit, so its text may differ for OTP 26-28 files) and CInf
 * (the source file name for
 * locations). With XXBYTE_MODE_AUTO the mode is the oldest release whose
 * max opcode reaches the Code chunk's (at least 25 when there is a Type
 * chunk).
 *
 * The listing starts with a "; BEAM module NAME: ..." summary line, then,
 * like beam_disasm:file/1, cuts the code into functions: each starts with
 * the labels and line instructions before its func_info and ends before the
 * next one's. Each function gets a header line
 *     ; function keyfind/3 (entry {f,2})
 * and a normal listing (addresses are offsets from the first instruction of
 * the Code chunk) whose label operands (outside lists) are BRANCH operands
 * through a label table context, and whose operands are resolved as beam_disasm resolves
 * them: {atom,lists}, {literal,{a,b}}, {float,1.5},
 * {extfunc,erlang,display,1}, the {M,F,A} of the function of a local call
 * target (call, call_last, call_only) or of a fun (make_fun2, make_fun3),
 * the {f,Label} of call_fun2's fun, {string,"abc"} (bs_put_string) or
 * {string,<<"abc">>} (bs_match_string, bs_create_bin), the
 * [{location,"lists.erl",122}] of a line instruction ([] for none), and
 * {tr,{x,0},integer(0..255)} for a typed register. Types print as a
 * "|"-separated union of atom, bitstring, cons, float, fun, integer, map,
 * nil, pid, port, reference, tuple and record ("any" for all of them), with
 * the integer range "integer(Min..Max)" (-inf, +inf for a missing bound) and
 * the bitstring unit "bitstring(Unit)". Terms print as
 * io_lib:format("~kp") prints them with an unlimited line length (Latin-1
 * printable range, Latin-1 characters written as UTF-8, maps ordered). Code after int_code_end follows a
 * "; end of code" line. Text outside the operands (the summary and headers)
 * is informative.
 *
 * Limitations: map pairs print in term order (as ~kp, maps:iterator(M,
 *  ordered), prints them). The VM orders maps used as keys by the atom-table
 *  indexes of their atom keys, which no file determines: a map stored in
 *  term order apart from such keys (deterministic term_to_binary, which
 *  newer compilers use) keeps its stored order, and any other map is sorted
 *  with atoms compared by name, which can differ there; integers of more
 *  than 4096 bytes print in
 *  base 16 (16#...); the pre-R12 FLOAT_EXT, pids, ports, references and local
 *  funs in literals are not printed (the literal shows as {lit,N}).
 */
typedef enum xxbyte_beam_chunk_id {
    XXBYTE_BEAM_CHUNK_ATOMS = 0, /* AtU8 or Atom */
    XXBYTE_BEAM_CHUNK_CODE,
    XXBYTE_BEAM_CHUNK_STRT,
    XXBYTE_BEAM_CHUNK_IMPT,
    XXBYTE_BEAM_CHUNK_EXPT,
    XXBYTE_BEAM_CHUNK_LOCT,
    XXBYTE_BEAM_CHUNK_FUNT,
    XXBYTE_BEAM_CHUNK_LITT,
    XXBYTE_BEAM_CHUNK_LINE,
    XXBYTE_BEAM_CHUNK_TYPE,
    XXBYTE_BEAM_CHUNK_CINF,
    XXBYTE_BEAM_CHUNK_ATTR,
    XXBYTE_BEAM_CHUNK_COUNT
} xxbyte_beam_chunk_id;

/* A chunk's data: offset from data[0] and size (without the padding);
 * size 0 and offset 0 when the file has no such chunk. */
typedef struct xxbyte_beam_chunk {
    uint32_t offset;
    uint32_t size;
} xxbyte_beam_chunk;

typedef struct xxbyte_beam_file {
    uint32_t size;        /* bytes of the form from data[0] */
    uint32_t chunk_count; /* all chunks of the form */
    xxbyte_beam_chunk chunks[XXBYTE_BEAM_CHUNK_COUNT];
    uint8_t atoms_utf8; /* AtU8 */
    uint8_t atoms_long; /* OTP 28+ table: lengths are compact u terms */
    uint16_t reserved;
    uint32_t atom_count; /* atoms 1..atom_count; atom 1 is the module */
    uint32_t instruction_set;
    uint32_t max_opcode;
    uint32_t label_count;
    uint32_t function_count;
    uint32_t code_offset; /* the first instruction, from data[0] */
    uint32_t code_size;
    uint32_t import_count;
    uint32_t export_count;
    uint32_t local_count;
    uint32_t lambda_count;
    uint32_t type_version; /* 0 without a Type chunk */
    uint32_t type_count;
    uint32_t mode; /* the mode XXBYTE_MODE_AUTO picks */
} xxbyte_beam_file;

/*
 * Parses the form at data[0] (uncompressed; size may extend past it) and
 * checks the tables listed above against their chunk sizes. Returns
 * XXBYTE_STATUS_INVALID_INSTRUCTION when data is not a BEAM form or a table
 * is inconsistent (no Code or atom chunk, an instruction set other than 0,
 * a count that does not fit its chunk), and TRUNCATED when a chunk runs
 * past size. Only the first 4 GiB are looked at. On failure *file is
 * zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_beam_parse_file(const uint8_t *data, size_t size, xxbyte_beam_file *file);

/* Atom index (1..atom_count): *text points into data (UTF-8 for AtU8,
 * Latin-1 for Atom). */
XXBYTE_API xxbyte_status xxbyte_beam_atom(const uint8_t *data, size_t size, const xxbyte_beam_file *file, uint32_t index, const uint8_t **text, uint32_t *length);

/* ImpT entry index (0-based): module and function atom indexes, arity. */
XXBYTE_API xxbyte_status xxbyte_beam_import(const uint8_t *data, size_t size, const xxbyte_beam_file *file, uint32_t index, uint32_t *module, uint32_t *function,
                                            uint32_t *arity);

/* ExpT (local == 0) or LocT entry index: function atom, arity, entry label. */
XXBYTE_API xxbyte_status xxbyte_beam_export(const uint8_t *data, size_t size, const xxbyte_beam_file *file, int local, uint32_t index, uint32_t *function,
                                            uint32_t *arity, uint32_t *label);

/* A FunT entry. */
typedef struct xxbyte_beam_lambda {
    uint32_t function; /* atom index */
    uint32_t arity;
    uint32_t label;
    uint32_t index;
    uint32_t free_count;
    uint32_t old_uniq;
} xxbyte_beam_lambda;

XXBYTE_API xxbyte_status xxbyte_beam_lambda_entry(const uint8_t *data, size_t size, const xxbyte_beam_file *file, uint32_t index, xxbyte_beam_lambda *lambda);

/*
 * The literal table of LitT, decompressed when needed: a 4-byte big-endian
 * literal count, then for each literal a 4-byte size and an
 * external-term-format term (with its 131 version byte). *table_size
 * receives the table's size; out may be NULL with capacity 0 to measure
 * (XXBYTE_STATUS_OUT_OF_MEMORY when it does not fit). A file without LitT
 * has an empty table (*table_size 0).
 */
XXBYTE_API xxbyte_status xxbyte_beam_literal_table(const uint8_t *data, size_t size, const xxbyte_beam_file *file, uint8_t *out, size_t capacity, size_t *table_size);

/* Literal index of such a table: *term points into table. */
XXBYTE_API xxbyte_status xxbyte_beam_literal(const uint8_t *table, size_t table_size, uint32_t index, const uint8_t **term, size_t *term_size);

/*
 * Formats an external-term-format term (starting with 131) as the listing
 * prints terms. Returns the complete length, or 0 for a term it cannot
 * print; buffer is always NUL-terminated when buffer_size != 0, truncating
 * if needed.
 */
XXBYTE_API size_t xxbyte_beam_term_text(const uint8_t *term, size_t term_size, char *buffer, size_t buffer_size);

/* Formats Type chunk entry index as the listing prints types; returns the
 * length as xxbyte_beam_term_text does. */
XXBYTE_API size_t xxbyte_beam_type_text(const uint8_t *data, size_t size, const xxbyte_beam_file *file, uint32_t index, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
