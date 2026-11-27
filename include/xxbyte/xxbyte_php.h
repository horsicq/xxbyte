/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_PHP_H
#define XXBYTE_XXBYTE_PHP_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_PHP
#error "the PHP Zend opcodes (OPcache) decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PHP Zend Engine opcodes as OPcache stores them in its file cache
 * (opcache.file_cache: <dir>/<system id>/<path>/<file>.php.bin).
 *
 * Mode: the PHP release series, XXBYTE_PHP_MODE(7, 0) to
 * XXBYTE_PHP_MODE(7, 4) and XXBYTE_PHP_MODE(8, 0) to XXBYTE_PHP_MODE(8, 6)
 * (default 8.6). Every patch release of a series shares its opcode
 * numbers, names and operand meanings (Zend/zend_vm_opcodes.h of the
 * series' last release: 7.0.33, 7.1.33, 7.2.34, 7.3.33, 7.4.33, 8.0.30,
 * 8.1.34, 8.2.36, 8.3.37, 8.4.28, 8.5.13 and 8.6.0). Only 64-bit builds
 * are supported: 32-bit builds use absolute addresses for constants and
 * jump targets (ZEND_USE_ABS_CONST_ADDR, ZEND_USE_ABS_JMP_ADDR) and
 * another structure layout. Any other mode is
 * XXBYTE_STATUS_UNSUPPORTED_MODE.
 *
 * One instruction is one 32-byte struct _zend_op (XXBYTE_PHP_OP_SIZE,
 * little-endian, the xxbyte_unit_size of every mode):
 *   +0  handler         (u64: 7.1+ the VM handler's number, 7.0 its
 *                        address)
 *   +8  op1             (u32 znode_op)
 *   +12 op2             (u32 znode_op)
 *   +16 result          (u32 znode_op)
 *   +20 extended_value  (u32)
 *   +24 lineno          (u32)
 *   +28 opcode, op1_type, op2_type, result_type (u8 each)
 * The encoding is that of the file cache, which differs from the one in
 * OPcache's shared memory only in CONST operands:
 *   - CONST (op_type 1): the literal index (7.3+), or the literal's byte
 *     offset index * 16 in the literals array (7.0 to 7.2);
 *   - TMP_VAR (2), VAR (4), CV (7.0 to 7.2: 16, 7.3+: 8): the byte offset
 *     of the slot in the call frame, (ZEND_CALL_FRAME_SLOT + n) * 16, with
 *     ZEND_CALL_FRAME_SLOT 6 for 7.0 and 5 later. n numbers the CVs from
 *     0 and the temporaries after them, as the dumper prints them;
 *   - UNUSED (0; 7.0 to 7.2 also 8, their IS_UNUSED): what the opcode's
 *     VM spec says: a number, a jump offset, a try/catch or live-range
 *     index, a class fetch type, ... (or nothing);
 *   - jump targets (op1, op2 or extended_value, as the opcode's spec says)
 *     are signed byte offsets from the instruction itself, except in 7.0:
 *     CATCH's next catch (extended_value, unless result.num marks the last
 *     catch) and the op2 of FAST_CALL and FAST_RET (when extended_value is
 *     set) are absolute opline numbers there.
 * result_type may also carry EXT_TYPE_UNUSED (0x20, 7.0: a result nobody
 * reads; it is decoded but not printed) or IS_SMART_BRANCH_JMPZ/JMPNZ
 * (0x10/0x20, 8.0+). The decoder rejects
 * (XXBYTE_STATUS_INVALID_INSTRUCTION) an undefined opcode, an operand type
 * other than these, a TMP_VAR/VAR/CV offset that is not a multiple of 16 or
 * lies inside the call frame header, a 7.0-7.2 CONST offset that is not a
 * multiple of 16, a jump offset that is not a multiple of 32 and, with a
 * context (xxbyte_decode_ex), a literal index past the op_array's
 * literals. The handler field is not checked, and fields the opcode does
 * not use are ignored.
 *
 * Addresses: pass the byte offset of the zend_op within the op_array's
 * opcodes (opline number * 32). BRANCH targets are absolute byte targets,
 * address + the signed offset (7.0's absolute ones: number * 32), and the
 * instruction text prints them in hex (0x260 is opline 19).
 *
 * Text follows OPcache's opcode dumper (opcache.opt_debug_level, Zend's
 * zend_dump_op) of the mode's release, with the result moved to the end
 * after "->", because xxbyte prints the mnemonic first:
 *   dumper:  0002 T3 = MUL CV1($b) int(2)
 *   xxbyte:  MUL CV1 #1 -> T3                (no context or resolver)
 *            MUL CV1($b) int(2) -> T3        (xxbyte_php_resolve)
 * 7.0 has no dumper: its text uses the 7.1 forms. The mnemonic is the
 * opcode name without "ZEND_" (as the dumper prints it); then, separated
 * by single spaces:
 *   1. extended_value as the opcode's spec reads it: a number ("1"), an
 *      opcode name in parentheses (ASSIGN_OP "(ADD)"), a type ("(long)",
 *      or from 8.0 for TYPE_CHECK masks that name more than one type
 *      "TYPE [null, long]"), an include kind ("(require_once)"),
 *      "(value)" or "(function)" for return-by-reference sources, fetch
 *      flags "(global)", "(local)", "(isset)", "(empty)", "(ref)",
 *      "(dim write)", the INIT_ARRAY size and "(packed)", and the 7.x
 *      forms ("(dim)", "(obj)", "(quick)", "(obj write)", class and
 *      constant fetch flags, the 7.1/7.2 argument number);
 *   2. op1, then op2: CONST operands as "#N" (literal N; a resolver
 *      prints the literal: null, bool(true), int(1), float(0.5),
 *      string("a\n"), array(...)), CVs as CV<n> (resolved:
 *      "CV<n>($name)"), temporaries as T<n> (TMP_VAR) and V<n> (VAR),
 *      jump targets in hex, numbers in decimal, and the keywords and
 *      flags of UNUSED operands as the dumper prints them: THIS, NEXT,
 *      CONSTRUCTOR, "(self)", "(parent)", "(static)", "(no-autoload)",
 *      "(exception)", "(unqualified-in-namespace)", "try-catch(N)",
 *      "loop-end(+N)" (8.4+) or "live-range(N)" (7.1/7.2) when the
 *      operand is freed on return;
 *   3. a jump target held in extended_value (JMPZNZ, FE_FETCH_R, CATCH,
 *      the default of SWITCH_LONG, SWITCH_STRING and MATCH);
 *   4. a CONST result, then " -> " and a TMP_VAR, VAR or CV result.
 * SWITCH_LONG, SWITCH_STRING and MATCH decoded with a context print their
 * jump table as the dumper does: "SWITCH_LONG CV0($x) 1: 0x60, 2: 0xA0,
 * default: 0xE0" ("\"a\": 0x60" for string keys); without one, the table
 * is the literal "#N" and only the default follows. The 8.4+
 * FRAMELESS_ICALL_0..3 append the internal function, "(#N)" or resolved
 * "(in_array)", right after the mnemonic. Unknown codes print as the
 * dumper would ("(???)", "zval(type=N)"). Differences from the dumper's
 * text: strings are always escaped as the 8.5+ dumper escapes them, so a
 * line never breaks (earlier dumpers print them raw and cut them at a NUL,
 * 8.3/8.4 escape only '"' and '\'); floats print in the shortest form that
 * reads back as the same double (the dumper uses %g: "0.1" for both,
 * "3.14159265358979" versus "3.14159"; a negative NaN is "-nan" for
 * both); "(no-autoload)" is spelled so for every release (the dumpers
 * before 8.1 print "(no-autolod)"); and the 8.6 CALLABLE_CONVERT_PARTIAL
 * prints its extended_value with the ZEND_PARTIAL_CACHEABLE_IN_SHM bit
 * (2) that the file cache sets in it.
 *
 * Operands, in print order; operand.flags carries the zend_op field it
 * comes from (XXBYTE_PHP_OPERAND_FLAG_OP1, _OP2, _RESULT or _EXTENDED) and
 * operand.extra the raw 32-bit field in its low 32 bits and, for op1, op2
 * and result operands, the field's op_type byte in bits 32-39 (bit 40: a
 * live-range or loop-end operand whose extended_value has
 * ZEND_FREE_ON_RETURN; for jump tables see below):
 *   - XXBYTE_OPERAND_INDEX XXBYTE_INDEX_CONSTANT: a CONST operand or
 *     result, value = literal index;
 *   - XXBYTE_OPERAND_INDEX XXBYTE_INDEX_LOCAL (CV), XXBYTE_PHP_INDEX_TMP
 *     (TMP_VAR), XXBYTE_PHP_INDEX_VAR (VAR): value = n as above;
 *   - XXBYTE_OPERAND_BRANCH: jump targets;
 *   - XXBYTE_OPERAND_INDEX XXBYTE_PHP_INDEX_FRAMELESS: the 8.4+ frameless
 *     internal function number (extended_value of FRAMELESS_ICALL_*);
 *   - XXBYTE_OPERAND_IMMEDIATE: numbers, fetch types and flags (the raw
 *     field, printed as the dumper prints it); THIS, NEXT and CONSTRUCTOR
 *     are IMMEDIATE operands with XXBYTE_OPERAND_FLAG_IMPLICIT and size
 *     0. UNUSED operands the dumper prints nothing for, such as cache
 *     slots, are not operands.
 * A SWITCH_LONG, SWITCH_STRING or MATCH decoded with a context whose
 * literal is a jump table has group XXBYTE_GROUP_SWITCH, table_count =
 * the number of cases and table_offset 0 (the table is not inline); the
 * table operand's extra then holds the context pointer, which
 * xxbyte_switch_entry and the formatter read, so keep the context, its
 * file and op_array alive while using the instruction. xxbyte_switch_entry
 * gives the case's integer key (for a string key: the case's index, see
 * xxbyte_php_get_jump_entry) and its absolute target.
 *
 * Groups: JMP and FAST_RET JUMP|STOP; JMPZNZ (7.x, 8.0, 8.1)
 * CONDITIONAL|STOP; the other jumps (JMPZ, JMPNZ, JMPZ_EX, JMPNZ_EX,
 * JMP_SET, COALESCE, JMP_NULL, FE_RESET_*, FE_FETCH_*, ASSERT_CHECK,
 * BIND_INIT_STATIC_OR_JMP, JMP_FRAMELESS and a CATCH that is not the
 * last) CONDITIONAL; SWITCH_LONG and SWITCH_STRING SWITCH (they fall
 * through when the operand's type does not match), MATCH SWITCH|STOP;
 * DO_FCALL, DO_ICALL, DO_UCALL, DO_FCALL_BY_NAME, FRAMELESS_ICALL_*,
 * INCLUDE_OR_EVAL and FAST_CALL CALL; RETURN, RETURN_BY_REF and
 * GENERATOR_RETURN RETURN|STOP; THROW, MATCH_ERROR and VERIFY_NEVER_TYPE
 * THROW|STOP; EXIT (before 8.4) STOP.
 */
#define XXBYTE_PHP_MODE(major, minor) ((((uint32_t)(major) & 0xFFu) << 8) | ((uint32_t)(minor) & 0xFFu))
#define XXBYTE_PHP_MODE_MAJOR(mode) (((uint32_t)(mode) >> 8) & 0xFFu)
#define XXBYTE_PHP_MODE_MINOR(mode) ((uint32_t)(mode) & 0xFFu)
#define XXBYTE_PHP_MODE_DEFAULT XXBYTE_PHP_MODE(8, 6)

#define XXBYTE_PHP_OP_SIZE 32u

/* Index kinds. */
#define XXBYTE_PHP_INDEX_TMP (XXBYTE_INDEX_FAMILY_FIRST + 0u)       /* T<n> */
#define XXBYTE_PHP_INDEX_VAR (XXBYTE_INDEX_FAMILY_FIRST + 1u)       /* V<n> */
#define XXBYTE_PHP_INDEX_FRAMELESS (XXBYTE_INDEX_FAMILY_FIRST + 2u) /* frameless function */

/* The zend_op field an operand comes from. */
#define XXBYTE_PHP_OPERAND_FLAG_OP1 0x1000u
#define XXBYTE_PHP_OPERAND_FLAG_OP2 0x2000u
#define XXBYTE_PHP_OPERAND_FLAG_RESULT 0x4000u
#define XXBYTE_PHP_OPERAND_FLAG_EXTENDED 0x8000u

/*
 * Mnemonic identifiers (xxbyte_instruction.id): one per opcode name of all
 * supported releases, in order of first appearance (7.0 by opcode number,
 * then the names 7.1 to 8.6 added), so an opcode keeps its id although
 * 7.4 renumbered most opcodes.
 */
typedef enum xxbyte_php_id {
    XXBYTE_PHP_ID_INVALID = 0,
    XXBYTE_PHP_ID_NOP = 1,
    XXBYTE_PHP_ID_ADD = 2,
    XXBYTE_PHP_ID_SUB = 3,
    XXBYTE_PHP_ID_MUL = 4,
    XXBYTE_PHP_ID_DIV = 5,
    XXBYTE_PHP_ID_MOD = 6,
    XXBYTE_PHP_ID_SL = 7,
    XXBYTE_PHP_ID_SR = 8,
    XXBYTE_PHP_ID_CONCAT = 9,
    XXBYTE_PHP_ID_BW_OR = 10,
    XXBYTE_PHP_ID_BW_AND = 11,
    XXBYTE_PHP_ID_BW_XOR = 12,
    XXBYTE_PHP_ID_BW_NOT = 13,
    XXBYTE_PHP_ID_BOOL_NOT = 14,
    XXBYTE_PHP_ID_BOOL_XOR = 15,
    XXBYTE_PHP_ID_IS_IDENTICAL = 16,
    XXBYTE_PHP_ID_IS_NOT_IDENTICAL = 17,
    XXBYTE_PHP_ID_IS_EQUAL = 18,
    XXBYTE_PHP_ID_IS_NOT_EQUAL = 19,
    XXBYTE_PHP_ID_IS_SMALLER = 20,
    XXBYTE_PHP_ID_IS_SMALLER_OR_EQUAL = 21,
    XXBYTE_PHP_ID_CAST = 22,
    XXBYTE_PHP_ID_QM_ASSIGN = 23,
    XXBYTE_PHP_ID_ASSIGN_ADD = 24,
    XXBYTE_PHP_ID_ASSIGN_SUB = 25,
    XXBYTE_PHP_ID_ASSIGN_MUL = 26,
    XXBYTE_PHP_ID_ASSIGN_DIV = 27,
    XXBYTE_PHP_ID_ASSIGN_MOD = 28,
    XXBYTE_PHP_ID_ASSIGN_SL = 29,
    XXBYTE_PHP_ID_ASSIGN_SR = 30,
    XXBYTE_PHP_ID_ASSIGN_CONCAT = 31,
    XXBYTE_PHP_ID_ASSIGN_BW_OR = 32,
    XXBYTE_PHP_ID_ASSIGN_BW_AND = 33,
    XXBYTE_PHP_ID_ASSIGN_BW_XOR = 34,
    XXBYTE_PHP_ID_PRE_INC = 35,
    XXBYTE_PHP_ID_PRE_DEC = 36,
    XXBYTE_PHP_ID_POST_INC = 37,
    XXBYTE_PHP_ID_POST_DEC = 38,
    XXBYTE_PHP_ID_ASSIGN = 39,
    XXBYTE_PHP_ID_ASSIGN_REF = 40,
    XXBYTE_PHP_ID_ECHO = 41,
    XXBYTE_PHP_ID_JMP = 42,
    XXBYTE_PHP_ID_JMPZ = 43,
    XXBYTE_PHP_ID_JMPNZ = 44,
    XXBYTE_PHP_ID_JMPZNZ = 45,
    XXBYTE_PHP_ID_JMPZ_EX = 46,
    XXBYTE_PHP_ID_JMPNZ_EX = 47,
    XXBYTE_PHP_ID_CASE = 48,
    XXBYTE_PHP_ID_BOOL = 49,
    XXBYTE_PHP_ID_FAST_CONCAT = 50,
    XXBYTE_PHP_ID_ROPE_INIT = 51,
    XXBYTE_PHP_ID_ROPE_ADD = 52,
    XXBYTE_PHP_ID_ROPE_END = 53,
    XXBYTE_PHP_ID_BEGIN_SILENCE = 54,
    XXBYTE_PHP_ID_END_SILENCE = 55,
    XXBYTE_PHP_ID_INIT_FCALL_BY_NAME = 56,
    XXBYTE_PHP_ID_DO_FCALL = 57,
    XXBYTE_PHP_ID_INIT_FCALL = 58,
    XXBYTE_PHP_ID_RETURN = 59,
    XXBYTE_PHP_ID_RECV = 60,
    XXBYTE_PHP_ID_RECV_INIT = 61,
    XXBYTE_PHP_ID_SEND_VAL = 62,
    XXBYTE_PHP_ID_SEND_VAR_EX = 63,
    XXBYTE_PHP_ID_SEND_REF = 64,
    XXBYTE_PHP_ID_NEW = 65,
    XXBYTE_PHP_ID_INIT_NS_FCALL_BY_NAME = 66,
    XXBYTE_PHP_ID_FREE = 67,
    XXBYTE_PHP_ID_INIT_ARRAY = 68,
    XXBYTE_PHP_ID_ADD_ARRAY_ELEMENT = 69,
    XXBYTE_PHP_ID_INCLUDE_OR_EVAL = 70,
    XXBYTE_PHP_ID_UNSET_VAR = 71,
    XXBYTE_PHP_ID_UNSET_DIM = 72,
    XXBYTE_PHP_ID_UNSET_OBJ = 73,
    XXBYTE_PHP_ID_FE_RESET_R = 74,
    XXBYTE_PHP_ID_FE_FETCH_R = 75,
    XXBYTE_PHP_ID_EXIT = 76,
    XXBYTE_PHP_ID_FETCH_R = 77,
    XXBYTE_PHP_ID_FETCH_DIM_R = 78,
    XXBYTE_PHP_ID_FETCH_OBJ_R = 79,
    XXBYTE_PHP_ID_FETCH_W = 80,
    XXBYTE_PHP_ID_FETCH_DIM_W = 81,
    XXBYTE_PHP_ID_FETCH_OBJ_W = 82,
    XXBYTE_PHP_ID_FETCH_RW = 83,
    XXBYTE_PHP_ID_FETCH_DIM_RW = 84,
    XXBYTE_PHP_ID_FETCH_OBJ_RW = 85,
    XXBYTE_PHP_ID_FETCH_IS = 86,
    XXBYTE_PHP_ID_FETCH_DIM_IS = 87,
    XXBYTE_PHP_ID_FETCH_OBJ_IS = 88,
    XXBYTE_PHP_ID_FETCH_FUNC_ARG = 89,
    XXBYTE_PHP_ID_FETCH_DIM_FUNC_ARG = 90,
    XXBYTE_PHP_ID_FETCH_OBJ_FUNC_ARG = 91,
    XXBYTE_PHP_ID_FETCH_UNSET = 92,
    XXBYTE_PHP_ID_FETCH_DIM_UNSET = 93,
    XXBYTE_PHP_ID_FETCH_OBJ_UNSET = 94,
    XXBYTE_PHP_ID_FETCH_LIST = 95,
    XXBYTE_PHP_ID_FETCH_CONSTANT = 96,
    XXBYTE_PHP_ID_EXT_STMT = 97,
    XXBYTE_PHP_ID_EXT_FCALL_BEGIN = 98,
    XXBYTE_PHP_ID_EXT_FCALL_END = 99,
    XXBYTE_PHP_ID_EXT_NOP = 100,
    XXBYTE_PHP_ID_TICKS = 101,
    XXBYTE_PHP_ID_SEND_VAR_NO_REF = 102,
    XXBYTE_PHP_ID_CATCH = 103,
    XXBYTE_PHP_ID_THROW = 104,
    XXBYTE_PHP_ID_FETCH_CLASS = 105,
    XXBYTE_PHP_ID_CLONE = 106,
    XXBYTE_PHP_ID_RETURN_BY_REF = 107,
    XXBYTE_PHP_ID_INIT_METHOD_CALL = 108,
    XXBYTE_PHP_ID_INIT_STATIC_METHOD_CALL = 109,
    XXBYTE_PHP_ID_ISSET_ISEMPTY_VAR = 110,
    XXBYTE_PHP_ID_ISSET_ISEMPTY_DIM_OBJ = 111,
    XXBYTE_PHP_ID_SEND_VAL_EX = 112,
    XXBYTE_PHP_ID_SEND_VAR = 113,
    XXBYTE_PHP_ID_INIT_USER_CALL = 114,
    XXBYTE_PHP_ID_SEND_ARRAY = 115,
    XXBYTE_PHP_ID_SEND_USER = 116,
    XXBYTE_PHP_ID_STRLEN = 117,
    XXBYTE_PHP_ID_DEFINED = 118,
    XXBYTE_PHP_ID_TYPE_CHECK = 119,
    XXBYTE_PHP_ID_VERIFY_RETURN_TYPE = 120,
    XXBYTE_PHP_ID_FE_RESET_RW = 121,
    XXBYTE_PHP_ID_FE_FETCH_RW = 122,
    XXBYTE_PHP_ID_FE_FREE = 123,
    XXBYTE_PHP_ID_INIT_DYNAMIC_CALL = 124,
    XXBYTE_PHP_ID_DO_ICALL = 125,
    XXBYTE_PHP_ID_DO_UCALL = 126,
    XXBYTE_PHP_ID_DO_FCALL_BY_NAME = 127,
    XXBYTE_PHP_ID_PRE_INC_OBJ = 128,
    XXBYTE_PHP_ID_PRE_DEC_OBJ = 129,
    XXBYTE_PHP_ID_POST_INC_OBJ = 130,
    XXBYTE_PHP_ID_POST_DEC_OBJ = 131,
    XXBYTE_PHP_ID_ASSIGN_OBJ = 132,
    XXBYTE_PHP_ID_OP_DATA = 133,
    XXBYTE_PHP_ID_INSTANCEOF = 134,
    XXBYTE_PHP_ID_DECLARE_CLASS = 135,
    XXBYTE_PHP_ID_DECLARE_INHERITED_CLASS = 136,
    XXBYTE_PHP_ID_DECLARE_FUNCTION = 137,
    XXBYTE_PHP_ID_YIELD_FROM = 138,
    XXBYTE_PHP_ID_DECLARE_CONST = 139,
    XXBYTE_PHP_ID_ADD_INTERFACE = 140,
    XXBYTE_PHP_ID_DECLARE_INHERITED_CLASS_DELAYED = 141,
    XXBYTE_PHP_ID_VERIFY_ABSTRACT_CLASS = 142,
    XXBYTE_PHP_ID_ASSIGN_DIM = 143,
    XXBYTE_PHP_ID_ISSET_ISEMPTY_PROP_OBJ = 144,
    XXBYTE_PHP_ID_HANDLE_EXCEPTION = 145,
    XXBYTE_PHP_ID_USER_OPCODE = 146,
    XXBYTE_PHP_ID_ASSERT_CHECK = 147,
    XXBYTE_PHP_ID_JMP_SET = 148,
    XXBYTE_PHP_ID_DECLARE_LAMBDA_FUNCTION = 149,
    XXBYTE_PHP_ID_ADD_TRAIT = 150,
    XXBYTE_PHP_ID_BIND_TRAITS = 151,
    XXBYTE_PHP_ID_SEPARATE = 152,
    XXBYTE_PHP_ID_FETCH_CLASS_NAME = 153,
    XXBYTE_PHP_ID_CALL_TRAMPOLINE = 154,
    XXBYTE_PHP_ID_DISCARD_EXCEPTION = 155,
    XXBYTE_PHP_ID_YIELD = 156,
    XXBYTE_PHP_ID_GENERATOR_RETURN = 157,
    XXBYTE_PHP_ID_FAST_CALL = 158,
    XXBYTE_PHP_ID_FAST_RET = 159,
    XXBYTE_PHP_ID_RECV_VARIADIC = 160,
    XXBYTE_PHP_ID_SEND_UNPACK = 161,
    XXBYTE_PHP_ID_POW = 162,
    XXBYTE_PHP_ID_ASSIGN_POW = 163,
    XXBYTE_PHP_ID_BIND_GLOBAL = 164,
    XXBYTE_PHP_ID_COALESCE = 165,
    XXBYTE_PHP_ID_SPACESHIP = 166,
    XXBYTE_PHP_ID_DECLARE_ANON_CLASS = 167,
    XXBYTE_PHP_ID_DECLARE_ANON_INHERITED_CLASS = 168,
    XXBYTE_PHP_ID_GENERATOR_CREATE = 169,
    XXBYTE_PHP_ID_CHECK_VAR = 170,
    XXBYTE_PHP_ID_SEND_VAR_NO_REF_EX = 171,
    XXBYTE_PHP_ID_MAKE_REF = 172,
    XXBYTE_PHP_ID_FETCH_STATIC_PROP_R = 173,
    XXBYTE_PHP_ID_FETCH_STATIC_PROP_W = 174,
    XXBYTE_PHP_ID_FETCH_STATIC_PROP_RW = 175,
    XXBYTE_PHP_ID_FETCH_STATIC_PROP_IS = 176,
    XXBYTE_PHP_ID_FETCH_STATIC_PROP_FUNC_ARG = 177,
    XXBYTE_PHP_ID_FETCH_STATIC_PROP_UNSET = 178,
    XXBYTE_PHP_ID_UNSET_STATIC_PROP = 179,
    XXBYTE_PHP_ID_ISSET_ISEMPTY_STATIC_PROP = 180,
    XXBYTE_PHP_ID_FETCH_CLASS_CONSTANT = 181,
    XXBYTE_PHP_ID_BIND_LEXICAL = 182,
    XXBYTE_PHP_ID_BIND_STATIC = 183,
    XXBYTE_PHP_ID_FETCH_THIS = 184,
    XXBYTE_PHP_ID_ISSET_ISEMPTY_THIS = 185,
    XXBYTE_PHP_ID_SWITCH_LONG = 186,
    XXBYTE_PHP_ID_SWITCH_STRING = 187,
    XXBYTE_PHP_ID_IN_ARRAY = 188,
    XXBYTE_PHP_ID_COUNT = 189,
    XXBYTE_PHP_ID_GET_CLASS = 190,
    XXBYTE_PHP_ID_GET_CALLED_CLASS = 191,
    XXBYTE_PHP_ID_GET_TYPE = 192,
    XXBYTE_PHP_ID_FUNC_NUM_ARGS = 193,
    XXBYTE_PHP_ID_FUNC_GET_ARGS = 194,
    XXBYTE_PHP_ID_UNSET_CV = 195,
    XXBYTE_PHP_ID_ISSET_ISEMPTY_CV = 196,
    XXBYTE_PHP_ID_FETCH_LIST_R = 197,
    XXBYTE_PHP_ID_CHECK_FUNC_ARG = 198,
    XXBYTE_PHP_ID_SEND_FUNC_ARG = 199,
    XXBYTE_PHP_ID_FETCH_LIST_W = 200,
    XXBYTE_PHP_ID_ASSIGN_STATIC_PROP = 201,
    XXBYTE_PHP_ID_ASSIGN_OP = 202,
    XXBYTE_PHP_ID_ASSIGN_DIM_OP = 203,
    XXBYTE_PHP_ID_ASSIGN_OBJ_OP = 204,
    XXBYTE_PHP_ID_ASSIGN_STATIC_PROP_OP = 205,
    XXBYTE_PHP_ID_ASSIGN_OBJ_REF = 206,
    XXBYTE_PHP_ID_ASSIGN_STATIC_PROP_REF = 207,
    XXBYTE_PHP_ID_PRE_INC_STATIC_PROP = 208,
    XXBYTE_PHP_ID_PRE_DEC_STATIC_PROP = 209,
    XXBYTE_PHP_ID_POST_INC_STATIC_PROP = 210,
    XXBYTE_PHP_ID_POST_DEC_STATIC_PROP = 211,
    XXBYTE_PHP_ID_DECLARE_CLASS_DELAYED = 212,
    XXBYTE_PHP_ID_ADD_ARRAY_UNPACK = 213,
    XXBYTE_PHP_ID_COPY_TMP = 214,
    XXBYTE_PHP_ID_ARRAY_KEY_EXISTS = 215,
    XXBYTE_PHP_ID_MATCH = 216,
    XXBYTE_PHP_ID_CASE_STRICT = 217,
    XXBYTE_PHP_ID_MATCH_ERROR = 218,
    XXBYTE_PHP_ID_JMP_NULL = 219,
    XXBYTE_PHP_ID_CHECK_UNDEF_ARGS = 220,
    XXBYTE_PHP_ID_FETCH_GLOBALS = 221,
    XXBYTE_PHP_ID_VERIFY_NEVER_TYPE = 222,
    XXBYTE_PHP_ID_CALLABLE_CONVERT = 223,
    XXBYTE_PHP_ID_BIND_INIT_STATIC_OR_JMP = 224,
    XXBYTE_PHP_ID_FRAMELESS_ICALL_0 = 225,
    XXBYTE_PHP_ID_FRAMELESS_ICALL_1 = 226,
    XXBYTE_PHP_ID_FRAMELESS_ICALL_2 = 227,
    XXBYTE_PHP_ID_FRAMELESS_ICALL_3 = 228,
    XXBYTE_PHP_ID_JMP_FRAMELESS = 229,
    XXBYTE_PHP_ID_INIT_PARENT_PROPERTY_HOOK_CALL = 230,
    XXBYTE_PHP_ID_DECLARE_ATTRIBUTED_CONST = 231,
    XXBYTE_PHP_ID_TYPE_ASSERT = 232,
    XXBYTE_PHP_ID_CALLABLE_CONVERT_PARTIAL = 233,
    XXBYTE_PHP_ID_SEND_PLACEHOLDER = 234,
    XXBYTE_PHP_ID_MAX = 235 /* one past the last id */
} xxbyte_php_id;

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_php_mnemonic(uint32_t id);

/* The name of opcode in mode, or NULL when it is undefined there or the
 * mode is unsupported. */
XXBYTE_API const char *xxbyte_php_opname(uint32_t mode, uint32_t opcode);

/* The raw fields of one zend_op. */
typedef struct xxbyte_php_op {
    uint64_t handler;
    uint32_t op1;
    uint32_t op2;
    uint32_t result;
    uint32_t extended_value;
    uint32_t lineno;
    uint8_t opcode;
    uint8_t op1_type;
    uint8_t op2_type;
    uint8_t result_type;
} xxbyte_php_op;

/* Reads the zend_op at code[0]: XXBYTE_STATUS_TRUNCATED when size is
 * below XXBYTE_PHP_OP_SIZE, XXBYTE_STATUS_INVALID_ARGUMENT for NULL
 * pointers. Nothing is validated. */
XXBYTE_API xxbyte_status xxbyte_php_read_op(const uint8_t *code, size_t size, xxbyte_php_op *op);

/*
 * File cache files. A file is an 80-byte zend_file_cache_metainfo header
 * ("OPCACHE\0", the 32-character system id, u64 mem_size, str_size,
 * script_offset and timestamp, u32 checksum), the memory image of the
 * persistent script (mem_size bytes) and the interned strings (str_size
 * bytes). Pointers inside the image are offsets into it; a pointer with
 * bit 0 set is the offset + 1 of a zend_string in the interned strings.
 * The checksum is the Adler-32 of image and strings.
 *
 * The release is not recorded: the system id is an MD5 over the PHP
 * version, the build and (8.x) the loaded functions and modules. With
 * XXBYTE_MODE_AUTO, xxbyte_php_parse_file reads the file with the
 * structure layout and opcode table of every supported release and keeps
 * the releases under which the script and its main op_array are
 * consistent (pointers, counts and sizes in range, zend_array headers
 * valid, the code ending in an instruction that does not fall through).
 * Of those it takes the one under which every instruction of every
 * op_array decodes, then the one that finds the most op_arrays and decodes
 * the most instructions, then the one whose VM handler numbering fits the
 * most handler fields (from 7.1 a handler field holds the number of the
 * handler the release's zend_vm_get_opcode_handler_idx or, for
 * type-specialized handlers, zend_vm_set_opcode_handler_ex picks for the
 * instruction), and then the newest. Releases share layouts (8.4 to 8.6
 * differ only in 8.6's class entry), so the handler numbers decide between
 * them; code whose handlers have the same numbers in two releases reads
 * as the newer: an 8.4 file without the type-specialized handlers whose
 * numbers 8.5 moved reads as 8.5 (the text is the same), and an 8.5 file
 * without classes, frameless calls or the other opcodes whose handlers
 * 8.6 renumbered reads as 8.6, whose DECLARE_LAMBDA_FUNCTION and
 * CALLABLE_CONVERT also print extended_value. Pass the mode explicitly to
 * choose.
 */
typedef struct xxbyte_php_file {
    const uint8_t *data; /* the whole file, borrowed */
    size_t size;
    uint32_t mode;       /* the release whose layout the file is read with */
    uint32_t checksum;   /* stored checksum */
    int checksum_ok;     /* nonzero when it matches image and strings */
    char system_id[33];  /* NUL-terminated */
    uint64_t mem_offset; /* file offset of the image (80) */
    uint64_t mem_size;
    uint64_t str_offset; /* file offset of the interned strings */
    uint64_t str_size;
    uint64_t script_offset;  /* image offset of the zend_persistent_script */
    uint64_t timestamp;      /* the script's modification time */
    const uint8_t *filename; /* the script's path (not NUL-terminated) */
    size_t filename_size;
} xxbyte_php_file;

#define XXBYTE_PHP_HEADER_SIZE 80u

/*
 * Parses the file-cache file data[0..size): the header, the script and
 * its main op_array, and sets *file. mode is XXBYTE_MODE_AUTO or a mode
 * whose layout to use. Returns XXBYTE_STATUS_TRUNCATED when the file is
 * shorter than its header says, XXBYTE_STATUS_INVALID_INSTRUCTION for a
 * bad magic or a script no supported layout reads (or, with an explicit
 * mode, one that the mode's layout does not read),
 * XXBYTE_STATUS_UNSUPPORTED_MODE for an unsupported mode. A checksum
 * mismatch is reported in checksum_ok, not as an error. On failure *file
 * is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_php_parse_file(const uint8_t *data, size_t size, uint32_t mode, xxbyte_php_file *file);

/* Kinds of op_array. */
#define XXBYTE_PHP_OP_ARRAY_MAIN 0u     /* the script's main code */
#define XXBYTE_PHP_OP_ARRAY_FUNCTION 1u /* a function_table entry */
#define XXBYTE_PHP_OP_ARRAY_METHOD 2u   /* a method of a class */
#define XXBYTE_PHP_OP_ARRAY_HOOK 3u     /* an 8.4+ property hook */
/* an 8.1+ dynamic_func_defs entry (closures, conditional functions) */
#define XXBYTE_PHP_OP_ARRAY_DYNAMIC 4u

#define XXBYTE_PHP_NO_PARENT 0xFFFFFFFFu

/* One op_array. Offsets are file offsets; strings point into the file and
 * are not NUL-terminated (NULL with size 0 when absent). */
typedef struct xxbyte_php_op_array {
    uint32_t index;  /* position in xxbyte_php_find_op_arrays order */
    uint32_t kind;   /* XXBYTE_PHP_OP_ARRAY_* */
    uint32_t parent; /* DYNAMIC: index of the op_array holding it, else
                        XXBYTE_PHP_NO_PARENT */
    uint32_t fn_flags;
    uint64_t offset;     /* the zend_op_array structure */
    const uint8_t *name; /* function_name; NULL for the main code */
    size_t name_size;
    const uint8_t *scope; /* the class name of a method or hook */
    size_t scope_size;
    const uint8_t *filename;
    size_t filename_size;
    uint32_t line_start;
    uint32_t line_end;
    uint32_t num_args;
    uint32_t required_num_args;
    uint32_t last_var; /* CVs */
    uint32_t T;        /* temporaries */
    uint32_t opcode_count;
    uint64_t opcodes_offset;
    uint32_t literal_count;
    uint64_t literals_offset;
    uint64_t vars_offset; /* the CV name pointers */
    uint32_t live_range_count;
    uint64_t live_range_offset;
    uint32_t try_catch_count;
    uint64_t try_catch_offset;
} xxbyte_php_op_array;

/* Most op_arrays xxbyte_php_find_op_arrays visits. */
#define XXBYTE_PHP_MAX_OP_ARRAYS 65536u

/*
 * Lists the op_arrays of a parsed file in the order the opcode dumper of
 * the file's release prints them: the main code, the functions of the
 * function table, then per class its methods (scope is the class; from
 * 7.4 trait copies, from 8.2 abstract methods, and from 8.3 class aliases
 * skipped, as those dumpers do) and, 8.4+, its property hooks; each
 * followed, 8.1+, by its dynamic_func_defs, depth-first. Stores the first
 * min(capacity, count) in arrays (NULL when capacity is 0); *count
 * receives the total. Returns XXBYTE_STATUS_INVALID_INSTRUCTION for a
 * structure that points outside the image or is malformed, nesting deeper
 * than 64, or more than XXBYTE_PHP_MAX_OP_ARRAYS op_arrays (*count then
 * holds those listed before the failure).
 */
XXBYTE_API xxbyte_status xxbyte_php_find_op_arrays(const xxbyte_php_file *file, xxbyte_php_op_array *arrays, size_t capacity, size_t *count);

/* Literal (zval) types, the PHP 8 type codes. */
#define XXBYTE_PHP_LITERAL_UNDEF 0u
#define XXBYTE_PHP_LITERAL_NULL 1u
#define XXBYTE_PHP_LITERAL_FALSE 2u
#define XXBYTE_PHP_LITERAL_TRUE 3u
#define XXBYTE_PHP_LITERAL_LONG 4u
#define XXBYTE_PHP_LITERAL_DOUBLE 5u
#define XXBYTE_PHP_LITERAL_STRING 6u
#define XXBYTE_PHP_LITERAL_ARRAY 7u
#define XXBYTE_PHP_LITERAL_OTHER 0xFFu /* anything else (type_code tells) */

typedef struct xxbyte_php_literal {
    uint32_t type;         /* XXBYTE_PHP_LITERAL_* */
    uint32_t type_code;    /* the zval type byte of the release */
    int64_t integer;       /* LONG */
    double number;         /* DOUBLE */
    const uint8_t *string; /* STRING: bytes in the file */
    size_t string_size;
    uint32_t array_count; /* ARRAY: elements (nNumOfElements) */
} xxbyte_php_literal;

/* Reads literal index of op_array. XXBYTE_STATUS_INVALID_ARGUMENT for an
 * index past the literals, XXBYTE_STATUS_INVALID_INSTRUCTION for a string
 * or array pointer outside the file. */
XXBYTE_API xxbyte_status xxbyte_php_get_literal(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t index, xxbyte_php_literal *literal);

/* The name (without '$') of CV index of op_array. */
XXBYTE_API xxbyte_status xxbyte_php_get_var_name(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t index, const uint8_t **name, size_t *size);

/* One case of a SWITCH_LONG, SWITCH_STRING or MATCH jump table. */
typedef struct xxbyte_php_jump_entry {
    int is_string;         /* string key */
    int64_t key;           /* integer key */
    const uint8_t *string; /* string key bytes in the file */
    size_t string_size;
    int64_t offset; /* the jump's byte offset from the switch instruction */
} xxbyte_php_jump_entry;

/* The case count (*count) of the jump table in literal of op_array, and
 * case index of it. XXBYTE_STATUS_INVALID_INSTRUCTION when the literal is
 * not an array of integer offsets. */
XXBYTE_API xxbyte_status xxbyte_php_get_jump_table(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t literal, uint32_t *count);
XXBYTE_API xxbyte_status xxbyte_php_get_jump_entry(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t literal, uint32_t index,
                                                   xxbyte_php_jump_entry *entry);

/* Live ranges (7.1+) and try/catch regions of an op_array. */
typedef struct xxbyte_php_live_range {
    uint32_t var;   /* the variable's n (as T<n>/V<n> print it) */
    uint32_t kind;  /* 0 tmp/var, 1 loop, 2 silence, 3 rope, 4 new */
    uint32_t start; /* opline numbers */
    uint32_t end;
} xxbyte_php_live_range;

typedef struct xxbyte_php_try_catch {
    uint32_t try_op; /* opline numbers; 0 when absent (except try_op) */
    uint32_t catch_op;
    uint32_t finally_op;
    uint32_t finally_end;
} xxbyte_php_try_catch;

XXBYTE_API xxbyte_status xxbyte_php_get_live_range(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t index, xxbyte_php_live_range *range);
XXBYTE_API xxbyte_status xxbyte_php_get_try_catch(const xxbyte_php_file *file, const xxbyte_php_op_array *op_array, uint32_t index, xxbyte_php_try_catch *region);

/*
 * Decode context and resolver context for the code of one op_array: pass
 * it to xxbyte_decode_ex or xxbyte_disasm_buf_ex as the decode context
 * (literal indexes are then checked and jump tables decoded) and, with
 * xxbyte_php_resolve, as the resolver context, decoding the op_array's
 * opcodes at addresses from 0 in file->mode.
 */
typedef struct xxbyte_php_unit {
    const xxbyte_php_file *file;
    const xxbyte_php_op_array *op_array;
} xxbyte_php_unit;

/*
 * A ready-made xxbyte_resolver; context is an xxbyte_php_unit. It gives
 * XXBYTE_INDEX_CONSTANT operands as the dumper prints literals (null,
 * bool(false), bool(true), int(N), float(X), string("..."), array(...),
 * zval(type=N)), CVs as CV<n>($name) (X<n> past last_var) and 8.4+
 * frameless functions by name for the function order of official builds
 * (Zend core, then pcre, then standard, as 8.4.27, 8.5.13 and 8.6.0
 * number them; a build with other frameless extensions or
 * disable_functions may differ). It declines temporaries, jump targets
 * and frameless numbers it does not know (the formatter prints "#N").
 * A text longer than buffer_size - 1 is reported as snprintf does (the
 * part that fits, and the full length). Passed to xxbyte_format_ex,
 * xxbyte_format_buf or xxbyte_disasm_buf_ex itself (not wrapped), it makes
 * the formatter print the text directly, without the resolver round trip.
 */
XXBYTE_API size_t xxbyte_php_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/*
 * xxbyte_disasm_file (list_file) prints, per op_array in
 * xxbyte_php_find_op_arrays order, a header line in the dumper's terms
 *   "K::m: ; (lines=5, args=1, vars=1, tmps=2) ; /path/a.php:3-3"
 * ("$_main" for the main code, "Class::method", "{closure}", 8.4+ hooks
 * as "Class::$prop::get"; bytes outside 0x20-0x7E of names and paths as
 * \xHH, where the dumper prints them raw and cuts them at a NUL), then its
 * code listed with a context and xxbyte_php_resolve at addresses from 0,
 * then its live ranges as "; live range: 2 0x20-0x40 (new)" (variable
 * number, start and end, kind: tmp/var, loop, silence, rope or new) and
 * its try/catch regions as "; try 0x0 catch 0x80 finally - end -", and a
 * blank line between op_arrays. The file's mode is used unless mode
 * overrides it; a listing cut short by a malformed structure ends with
 * the op_arrays read until then and the failure status.
 */

#ifdef __cplusplus
}
#endif

#endif
