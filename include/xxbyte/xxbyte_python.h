/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_PYTHON_H
#define XXBYTE_XXBYTE_PYTHON_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_PYTHON
#error "the CPython decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CPython decode mode: the interpreter version whose opcode table applies.
 * Versions 2.0 to 2.7 and 3.0 to 3.14 are supported; any other mode is
 * XXBYTE_STATUS_UNSUPPORTED_MODE.
 *
 * One decoded instruction is one opcode together with everything the
 * interpreter consumes with it: its EXTENDED_ARG prefixes (merged into the
 * argument) and, from 3.11, its inline CACHE entries (table_count entries
 * of 2 bytes at table_offset). A CACHE unit found on its own decodes as
 * "CACHE". Specialized and instrumented opcodes decode with their own names
 * and the argument and cache layout of the instruction they replace.
 *
 * Operands, when the opcode takes an argument (one operand, two for the
 * 3.13+ LOAD_FAST_LOAD_FAST family):
 *   - XXBYTE_OPERAND_INDEX: value is the index (co_consts, co_names,
 *     co_varnames or the cell/free slot; from 3.11 locals and cells are
 *     both "fast locals" numbered as code._varname_from_oparg does), after
 *     the version's shifts such as LOAD_GLOBAL arg >> 1; extra is the raw
 *     argument.
 *   - XXBYTE_OPERAND_BRANCH: value is the absolute target, extra the raw
 *     argument. Targets of absolute jumps (hasjabs) are offsets from the
 *     start of co_code, so pass address 0 for the first instruction.
 *   - XXBYTE_OPERAND_IMMEDIATE: value is the argument and extra the raw
 *     argument. From 3.11 the argument is a signed 32-bit C int, so value
 *     is sign-extended and the SIGNED flag is set.
 * operand.size is the number of argument bytes, prefixes included.
 * instruction.opcode holds the opcode in its low byte and the opcodes of
 * up to three EXTENDED_ARG prefixes above it.
 *
 * The combined argument equals the one dis computes for every chain of up
 * to three EXTENDED_ARG prefixes (the most a compiler emits) and, in 3.6 to
 * 3.10, up to seven. Longer chains differ when their high bytes are not
 * zero, because dis then builds a Python integer wider than 64 bits (from
 * 3.11 one that is not even wrapped to 32 bits): before 3.11 the argument
 * keeps the low 64 bits of that integer, from 3.11 it is the signed 32-bit
 * value the interpreter uses (dis shows LOAD_SMALL_INT -549755813888 for
 * 45 80 45 00 45 00 45 00 5E 00, xxbyte LOAD_SMALL_INT 0).
 *
 * At most XXBYTE_PYTHON_MAX_EXTENDED_ARGS prefixes merge. A longer chain is
 * XXBYTE_STATUS_INVALID_INSTRUCTION as soon as the opcode of the prefix
 * past the limit is seen, whatever follows it (dis would list the surplus
 * prefixes one by one). The limit bounds the work of one decode, so a sweep
 * that resynchronizes byte by byte after a failure stays linear on a long
 * run of prefixes.
 *
 * Text follows dis: "OPNAME", "OPNAME arg" with the combined argument, and
 * " (argrepr)" where dis shows one without the code object: "(to N)" for
 * jumps (N the byte offset, also for 3.13+ where dis prints labels, which
 * xxbyte_python_resolver supplies; also for the absolute jumps of 2.x to
 * 3.9, where dis shows nothing; 3.14 END_ASYNC_FOR shows "(from N)"),
 * comparison operators, BINARY_OP
 * symbols, conversions, function flags, intrinsics and so on. Index
 * operands are shown in parentheses only when a resolver names them, in
 * the version's style such as "(NULL + name)" or "(name + NULL|self)", and
 * only where that version's dis shows the name (3.11 dis shows no constant
 * for KW_NAMES, so neither does xxbyte). The formatter reads only the
 * instruction structure, never the code bytes.
 */
#define XXBYTE_PYTHON_MODE(major, minor) ((((uint32_t)(major) & 0xFFu) << 8) | ((uint32_t)(minor) & 0xFFu))
#define XXBYTE_PYTHON_MODE_MAJOR(mode) (((uint32_t)(mode) >> 8) & 0xFFu)
#define XXBYTE_PYTHON_MODE_MINOR(mode) ((uint32_t)(mode) & 0xFFu)
#define XXBYTE_PYTHON_MODE_DEFAULT XXBYTE_PYTHON_MODE(3, 14)

/* The most EXTENDED_ARG prefixes merged into one instruction. */
#define XXBYTE_PYTHON_MAX_EXTENDED_ARGS 16u

/*
 * Mnemonic identifiers (xxbyte_instruction.id), one per opcode name of all
 * supported versions, so an opcode keeps its id across versions. The enum
 * name is the opcode name with each run of characters other than A-Z and
 * 0-9 replaced by '_'; the "__" of the 3.11/3.12 superinstructions such as
 * LOAD_FAST__LOAD_FAST becomes "_THEN_".
 */
typedef enum xxbyte_python_id {
    XXBYTE_PYTHON_ID_INVALID = 0,
    XXBYTE_PYTHON_ID_STOP_CODE = 1,
    XXBYTE_PYTHON_ID_POP_TOP = 2,
    XXBYTE_PYTHON_ID_ROT_TWO = 3,
    XXBYTE_PYTHON_ID_ROT_THREE = 4,
    XXBYTE_PYTHON_ID_DUP_TOP = 5,
    XXBYTE_PYTHON_ID_ROT_FOUR = 6,
    XXBYTE_PYTHON_ID_UNARY_POSITIVE = 7,
    XXBYTE_PYTHON_ID_UNARY_NEGATIVE = 8,
    XXBYTE_PYTHON_ID_UNARY_NOT = 9,
    XXBYTE_PYTHON_ID_UNARY_CONVERT = 10,
    XXBYTE_PYTHON_ID_UNARY_INVERT = 11,
    XXBYTE_PYTHON_ID_BINARY_POWER = 12,
    XXBYTE_PYTHON_ID_BINARY_MULTIPLY = 13,
    XXBYTE_PYTHON_ID_BINARY_DIVIDE = 14,
    XXBYTE_PYTHON_ID_BINARY_MODULO = 15,
    XXBYTE_PYTHON_ID_BINARY_ADD = 16,
    XXBYTE_PYTHON_ID_BINARY_SUBTRACT = 17,
    XXBYTE_PYTHON_ID_BINARY_SUBSCR = 18,
    XXBYTE_PYTHON_ID_SLICE_0 = 19,
    XXBYTE_PYTHON_ID_SLICE_1 = 20,
    XXBYTE_PYTHON_ID_SLICE_2 = 21,
    XXBYTE_PYTHON_ID_SLICE_3 = 22,
    XXBYTE_PYTHON_ID_STORE_SLICE_0 = 23,
    XXBYTE_PYTHON_ID_STORE_SLICE_1 = 24,
    XXBYTE_PYTHON_ID_STORE_SLICE_2 = 25,
    XXBYTE_PYTHON_ID_STORE_SLICE_3 = 26,
    XXBYTE_PYTHON_ID_DELETE_SLICE_0 = 27,
    XXBYTE_PYTHON_ID_DELETE_SLICE_1 = 28,
    XXBYTE_PYTHON_ID_DELETE_SLICE_2 = 29,
    XXBYTE_PYTHON_ID_DELETE_SLICE_3 = 30,
    XXBYTE_PYTHON_ID_INPLACE_ADD = 31,
    XXBYTE_PYTHON_ID_INPLACE_SUBTRACT = 32,
    XXBYTE_PYTHON_ID_INPLACE_MULTIPLY = 33,
    XXBYTE_PYTHON_ID_INPLACE_DIVIDE = 34,
    XXBYTE_PYTHON_ID_INPLACE_MODULO = 35,
    XXBYTE_PYTHON_ID_STORE_SUBSCR = 36,
    XXBYTE_PYTHON_ID_DELETE_SUBSCR = 37,
    XXBYTE_PYTHON_ID_BINARY_LSHIFT = 38,
    XXBYTE_PYTHON_ID_BINARY_RSHIFT = 39,
    XXBYTE_PYTHON_ID_BINARY_AND = 40,
    XXBYTE_PYTHON_ID_BINARY_XOR = 41,
    XXBYTE_PYTHON_ID_BINARY_OR = 42,
    XXBYTE_PYTHON_ID_INPLACE_POWER = 43,
    XXBYTE_PYTHON_ID_PRINT_EXPR = 44,
    XXBYTE_PYTHON_ID_PRINT_ITEM = 45,
    XXBYTE_PYTHON_ID_PRINT_NEWLINE = 46,
    XXBYTE_PYTHON_ID_PRINT_ITEM_TO = 47,
    XXBYTE_PYTHON_ID_PRINT_NEWLINE_TO = 48,
    XXBYTE_PYTHON_ID_INPLACE_LSHIFT = 49,
    XXBYTE_PYTHON_ID_INPLACE_RSHIFT = 50,
    XXBYTE_PYTHON_ID_INPLACE_AND = 51,
    XXBYTE_PYTHON_ID_INPLACE_XOR = 52,
    XXBYTE_PYTHON_ID_INPLACE_OR = 53,
    XXBYTE_PYTHON_ID_BREAK_LOOP = 54,
    XXBYTE_PYTHON_ID_LOAD_LOCALS = 55,
    XXBYTE_PYTHON_ID_RETURN_VALUE = 56,
    XXBYTE_PYTHON_ID_IMPORT_STAR = 57,
    XXBYTE_PYTHON_ID_EXEC_STMT = 58,
    XXBYTE_PYTHON_ID_POP_BLOCK = 59,
    XXBYTE_PYTHON_ID_END_FINALLY = 60,
    XXBYTE_PYTHON_ID_BUILD_CLASS = 61,
    XXBYTE_PYTHON_ID_STORE_NAME = 62,
    XXBYTE_PYTHON_ID_DELETE_NAME = 63,
    XXBYTE_PYTHON_ID_UNPACK_SEQUENCE = 64,
    XXBYTE_PYTHON_ID_STORE_ATTR = 65,
    XXBYTE_PYTHON_ID_DELETE_ATTR = 66,
    XXBYTE_PYTHON_ID_STORE_GLOBAL = 67,
    XXBYTE_PYTHON_ID_DELETE_GLOBAL = 68,
    XXBYTE_PYTHON_ID_DUP_TOPX = 69,
    XXBYTE_PYTHON_ID_LOAD_CONST = 70,
    XXBYTE_PYTHON_ID_LOAD_NAME = 71,
    XXBYTE_PYTHON_ID_BUILD_TUPLE = 72,
    XXBYTE_PYTHON_ID_BUILD_LIST = 73,
    XXBYTE_PYTHON_ID_BUILD_MAP = 74,
    XXBYTE_PYTHON_ID_LOAD_ATTR = 75,
    XXBYTE_PYTHON_ID_COMPARE_OP = 76,
    XXBYTE_PYTHON_ID_IMPORT_NAME = 77,
    XXBYTE_PYTHON_ID_IMPORT_FROM = 78,
    XXBYTE_PYTHON_ID_JUMP_FORWARD = 79,
    XXBYTE_PYTHON_ID_JUMP_IF_FALSE = 80,
    XXBYTE_PYTHON_ID_JUMP_IF_TRUE = 81,
    XXBYTE_PYTHON_ID_JUMP_ABSOLUTE = 82,
    XXBYTE_PYTHON_ID_FOR_LOOP = 83,
    XXBYTE_PYTHON_ID_LOAD_GLOBAL = 84,
    XXBYTE_PYTHON_ID_SETUP_LOOP = 85,
    XXBYTE_PYTHON_ID_SETUP_EXCEPT = 86,
    XXBYTE_PYTHON_ID_SETUP_FINALLY = 87,
    XXBYTE_PYTHON_ID_LOAD_FAST = 88,
    XXBYTE_PYTHON_ID_STORE_FAST = 89,
    XXBYTE_PYTHON_ID_DELETE_FAST = 90,
    XXBYTE_PYTHON_ID_SET_LINENO = 91,
    XXBYTE_PYTHON_ID_RAISE_VARARGS = 92,
    XXBYTE_PYTHON_ID_CALL_FUNCTION = 93,
    XXBYTE_PYTHON_ID_MAKE_FUNCTION = 94,
    XXBYTE_PYTHON_ID_BUILD_SLICE = 95,
    XXBYTE_PYTHON_ID_CALL_FUNCTION_VAR = 96,
    XXBYTE_PYTHON_ID_CALL_FUNCTION_KW = 97,
    XXBYTE_PYTHON_ID_CALL_FUNCTION_VAR_KW = 98,
    XXBYTE_PYTHON_ID_EXTENDED_ARG = 99,
    XXBYTE_PYTHON_ID_CONTINUE_LOOP = 100,
    XXBYTE_PYTHON_ID_MAKE_CLOSURE = 101,
    XXBYTE_PYTHON_ID_LOAD_CLOSURE = 102,
    XXBYTE_PYTHON_ID_LOAD_DEREF = 103,
    XXBYTE_PYTHON_ID_STORE_DEREF = 104,
    XXBYTE_PYTHON_ID_BINARY_FLOOR_DIVIDE = 105,
    XXBYTE_PYTHON_ID_BINARY_TRUE_DIVIDE = 106,
    XXBYTE_PYTHON_ID_INPLACE_FLOOR_DIVIDE = 107,
    XXBYTE_PYTHON_ID_INPLACE_TRUE_DIVIDE = 108,
    XXBYTE_PYTHON_ID_GET_ITER = 109,
    XXBYTE_PYTHON_ID_YIELD_STMT = 110,
    XXBYTE_PYTHON_ID_FOR_ITER = 111,
    XXBYTE_PYTHON_ID_YIELD_VALUE = 112,
    XXBYTE_PYTHON_ID_NOP = 113,
    XXBYTE_PYTHON_ID_LIST_APPEND = 114,
    XXBYTE_PYTHON_ID_WITH_CLEANUP = 115,
    XXBYTE_PYTHON_ID_STORE_MAP = 116,
    XXBYTE_PYTHON_ID_BUILD_SET = 117,
    XXBYTE_PYTHON_ID_JUMP_IF_FALSE_OR_POP = 118,
    XXBYTE_PYTHON_ID_JUMP_IF_TRUE_OR_POP = 119,
    XXBYTE_PYTHON_ID_POP_JUMP_IF_FALSE = 120,
    XXBYTE_PYTHON_ID_POP_JUMP_IF_TRUE = 121,
    XXBYTE_PYTHON_ID_SETUP_WITH = 122,
    XXBYTE_PYTHON_ID_SET_ADD = 123,
    XXBYTE_PYTHON_ID_MAP_ADD = 124,
    XXBYTE_PYTHON_ID_STORE_LOCALS = 125,
    XXBYTE_PYTHON_ID_LOAD_BUILD_CLASS = 126,
    XXBYTE_PYTHON_ID_POP_EXCEPT = 127,
    XXBYTE_PYTHON_ID_UNPACK_EX = 128,
    XXBYTE_PYTHON_ID_DUP_TOP_TWO = 129,
    XXBYTE_PYTHON_ID_DELETE_DEREF = 130,
    XXBYTE_PYTHON_ID_YIELD_FROM = 131,
    XXBYTE_PYTHON_ID_LOAD_CLASSDEREF = 132,
    XXBYTE_PYTHON_ID_BINARY_MATRIX_MULTIPLY = 133,
    XXBYTE_PYTHON_ID_INPLACE_MATRIX_MULTIPLY = 134,
    XXBYTE_PYTHON_ID_GET_AITER = 135,
    XXBYTE_PYTHON_ID_GET_ANEXT = 136,
    XXBYTE_PYTHON_ID_BEFORE_ASYNC_WITH = 137,
    XXBYTE_PYTHON_ID_GET_YIELD_FROM_ITER = 138,
    XXBYTE_PYTHON_ID_GET_AWAITABLE = 139,
    XXBYTE_PYTHON_ID_WITH_CLEANUP_START = 140,
    XXBYTE_PYTHON_ID_WITH_CLEANUP_FINISH = 141,
    XXBYTE_PYTHON_ID_BUILD_LIST_UNPACK = 142,
    XXBYTE_PYTHON_ID_BUILD_MAP_UNPACK = 143,
    XXBYTE_PYTHON_ID_BUILD_MAP_UNPACK_WITH_CALL = 144,
    XXBYTE_PYTHON_ID_BUILD_TUPLE_UNPACK = 145,
    XXBYTE_PYTHON_ID_BUILD_SET_UNPACK = 146,
    XXBYTE_PYTHON_ID_SETUP_ASYNC_WITH = 147,
    XXBYTE_PYTHON_ID_SETUP_ANNOTATIONS = 148,
    XXBYTE_PYTHON_ID_STORE_ANNOTATION = 149,
    XXBYTE_PYTHON_ID_CALL_FUNCTION_EX = 150,
    XXBYTE_PYTHON_ID_FORMAT_VALUE = 151,
    XXBYTE_PYTHON_ID_BUILD_CONST_KEY_MAP = 152,
    XXBYTE_PYTHON_ID_BUILD_STRING = 153,
    XXBYTE_PYTHON_ID_BUILD_TUPLE_UNPACK_WITH_CALL = 154,
    XXBYTE_PYTHON_ID_LOAD_METHOD = 155,
    XXBYTE_PYTHON_ID_CALL_METHOD = 156,
    XXBYTE_PYTHON_ID_BEGIN_FINALLY = 157,
    XXBYTE_PYTHON_ID_END_ASYNC_FOR = 158,
    XXBYTE_PYTHON_ID_CALL_FINALLY = 159,
    XXBYTE_PYTHON_ID_POP_FINALLY = 160,
    XXBYTE_PYTHON_ID_RERAISE = 161,
    XXBYTE_PYTHON_ID_WITH_EXCEPT_START = 162,
    XXBYTE_PYTHON_ID_LOAD_ASSERTION_ERROR = 163,
    XXBYTE_PYTHON_ID_LIST_TO_TUPLE = 164,
    XXBYTE_PYTHON_ID_IS_OP = 165,
    XXBYTE_PYTHON_ID_CONTAINS_OP = 166,
    XXBYTE_PYTHON_ID_JUMP_IF_NOT_EXC_MATCH = 167,
    XXBYTE_PYTHON_ID_LIST_EXTEND = 168,
    XXBYTE_PYTHON_ID_SET_UPDATE = 169,
    XXBYTE_PYTHON_ID_DICT_MERGE = 170,
    XXBYTE_PYTHON_ID_DICT_UPDATE = 171,
    XXBYTE_PYTHON_ID_GET_LEN = 172,
    XXBYTE_PYTHON_ID_MATCH_MAPPING = 173,
    XXBYTE_PYTHON_ID_MATCH_SEQUENCE = 174,
    XXBYTE_PYTHON_ID_MATCH_KEYS = 175,
    XXBYTE_PYTHON_ID_COPY_DICT_WITHOUT_KEYS = 176,
    XXBYTE_PYTHON_ID_ROT_N = 177,
    XXBYTE_PYTHON_ID_GEN_START = 178,
    XXBYTE_PYTHON_ID_MATCH_CLASS = 179,
    XXBYTE_PYTHON_ID_CACHE = 180,
    XXBYTE_PYTHON_ID_PUSH_NULL = 181,
    XXBYTE_PYTHON_ID_BINARY_OP_ADAPTIVE = 182,
    XXBYTE_PYTHON_ID_BINARY_OP_ADD_FLOAT = 183,
    XXBYTE_PYTHON_ID_BINARY_OP_ADD_INT = 184,
    XXBYTE_PYTHON_ID_BINARY_OP_ADD_UNICODE = 185,
    XXBYTE_PYTHON_ID_BINARY_OP_INPLACE_ADD_UNICODE = 186,
    XXBYTE_PYTHON_ID_BINARY_OP_MULTIPLY_FLOAT = 187,
    XXBYTE_PYTHON_ID_BINARY_OP_MULTIPLY_INT = 188,
    XXBYTE_PYTHON_ID_BINARY_OP_SUBTRACT_FLOAT = 189,
    XXBYTE_PYTHON_ID_BINARY_OP_SUBTRACT_INT = 190,
    XXBYTE_PYTHON_ID_BINARY_SUBSCR_ADAPTIVE = 191,
    XXBYTE_PYTHON_ID_BINARY_SUBSCR_DICT = 192,
    XXBYTE_PYTHON_ID_BINARY_SUBSCR_GETITEM = 193,
    XXBYTE_PYTHON_ID_BINARY_SUBSCR_LIST_INT = 194,
    XXBYTE_PYTHON_ID_BINARY_SUBSCR_TUPLE_INT = 195,
    XXBYTE_PYTHON_ID_CALL_ADAPTIVE = 196,
    XXBYTE_PYTHON_ID_CALL_PY_EXACT_ARGS = 197,
    XXBYTE_PYTHON_ID_CALL_PY_WITH_DEFAULTS = 198,
    XXBYTE_PYTHON_ID_COMPARE_OP_ADAPTIVE = 199,
    XXBYTE_PYTHON_ID_COMPARE_OP_FLOAT_JUMP = 200,
    XXBYTE_PYTHON_ID_COMPARE_OP_INT_JUMP = 201,
    XXBYTE_PYTHON_ID_COMPARE_OP_STR_JUMP = 202,
    XXBYTE_PYTHON_ID_EXTENDED_ARG_QUICK = 203,
    XXBYTE_PYTHON_ID_PUSH_EXC_INFO = 204,
    XXBYTE_PYTHON_ID_CHECK_EXC_MATCH = 205,
    XXBYTE_PYTHON_ID_CHECK_EG_MATCH = 206,
    XXBYTE_PYTHON_ID_JUMP_BACKWARD_QUICK = 207,
    XXBYTE_PYTHON_ID_LOAD_ATTR_ADAPTIVE = 208,
    XXBYTE_PYTHON_ID_LOAD_ATTR_INSTANCE_VALUE = 209,
    XXBYTE_PYTHON_ID_LOAD_ATTR_MODULE = 210,
    XXBYTE_PYTHON_ID_LOAD_ATTR_SLOT = 211,
    XXBYTE_PYTHON_ID_LOAD_ATTR_WITH_HINT = 212,
    XXBYTE_PYTHON_ID_LOAD_CONST_THEN_LOAD_FAST = 213,
    XXBYTE_PYTHON_ID_LOAD_FAST_THEN_LOAD_CONST = 214,
    XXBYTE_PYTHON_ID_LOAD_FAST_THEN_LOAD_FAST = 215,
    XXBYTE_PYTHON_ID_LOAD_GLOBAL_ADAPTIVE = 216,
    XXBYTE_PYTHON_ID_LOAD_GLOBAL_BUILTIN = 217,
    XXBYTE_PYTHON_ID_BEFORE_WITH = 218,
    XXBYTE_PYTHON_ID_LOAD_GLOBAL_MODULE = 219,
    XXBYTE_PYTHON_ID_LOAD_METHOD_ADAPTIVE = 220,
    XXBYTE_PYTHON_ID_LOAD_METHOD_CLASS = 221,
    XXBYTE_PYTHON_ID_LOAD_METHOD_MODULE = 222,
    XXBYTE_PYTHON_ID_LOAD_METHOD_NO_DICT = 223,
    XXBYTE_PYTHON_ID_LOAD_METHOD_WITH_DICT = 224,
    XXBYTE_PYTHON_ID_LOAD_METHOD_WITH_VALUES = 225,
    XXBYTE_PYTHON_ID_PRECALL_ADAPTIVE = 226,
    XXBYTE_PYTHON_ID_PRECALL_BOUND_METHOD = 227,
    XXBYTE_PYTHON_ID_PRECALL_BUILTIN_CLASS = 228,
    XXBYTE_PYTHON_ID_PRECALL_BUILTIN_FAST_WITH_KEYWORDS = 229,
    XXBYTE_PYTHON_ID_PRECALL_METHOD_DESCRIPTOR_FAST_WITH_KEYWORDS = 230,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_BUILTIN_FAST = 231,
    XXBYTE_PYTHON_ID_RETURN_GENERATOR = 232,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_BUILTIN_O = 233,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_ISINSTANCE = 234,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_LEN = 235,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_LIST_APPEND = 236,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_METHOD_DESCRIPTOR_FAST = 237,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_METHOD_DESCRIPTOR_NOARGS = 238,
    XXBYTE_PYTHON_ID_ASYNC_GEN_WRAP = 239,
    XXBYTE_PYTHON_ID_PREP_RERAISE_STAR = 240,
    XXBYTE_PYTHON_ID_SWAP = 241,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_METHOD_DESCRIPTOR_O = 242,
    XXBYTE_PYTHON_ID_POP_JUMP_FORWARD_IF_FALSE = 243,
    XXBYTE_PYTHON_ID_POP_JUMP_FORWARD_IF_TRUE = 244,
    XXBYTE_PYTHON_ID_COPY = 245,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_STR_1 = 246,
    XXBYTE_PYTHON_ID_BINARY_OP = 247,
    XXBYTE_PYTHON_ID_SEND = 248,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_TUPLE_1 = 249,
    XXBYTE_PYTHON_ID_POP_JUMP_FORWARD_IF_NOT_NONE = 250,
    XXBYTE_PYTHON_ID_POP_JUMP_FORWARD_IF_NONE = 251,
    XXBYTE_PYTHON_ID_JUMP_BACKWARD_NO_INTERRUPT = 252,
    XXBYTE_PYTHON_ID_MAKE_CELL = 253,
    XXBYTE_PYTHON_ID_JUMP_BACKWARD = 254,
    XXBYTE_PYTHON_ID_PRECALL_NO_KW_TYPE_1 = 255,
    XXBYTE_PYTHON_ID_PRECALL_PYFUNC = 256,
    XXBYTE_PYTHON_ID_COPY_FREE_VARS = 257,
    XXBYTE_PYTHON_ID_RESUME_QUICK = 258,
    XXBYTE_PYTHON_ID_RESUME = 259,
    XXBYTE_PYTHON_ID_STORE_ATTR_ADAPTIVE = 260,
    XXBYTE_PYTHON_ID_STORE_ATTR_INSTANCE_VALUE = 261,
    XXBYTE_PYTHON_ID_STORE_ATTR_SLOT = 262,
    XXBYTE_PYTHON_ID_STORE_ATTR_WITH_HINT = 263,
    XXBYTE_PYTHON_ID_STORE_FAST_THEN_LOAD_FAST = 264,
    XXBYTE_PYTHON_ID_PRECALL = 265,
    XXBYTE_PYTHON_ID_STORE_FAST_THEN_STORE_FAST = 266,
    XXBYTE_PYTHON_ID_STORE_SUBSCR_ADAPTIVE = 267,
    XXBYTE_PYTHON_ID_STORE_SUBSCR_DICT = 268,
    XXBYTE_PYTHON_ID_STORE_SUBSCR_LIST_INT = 269,
    XXBYTE_PYTHON_ID_CALL = 270,
    XXBYTE_PYTHON_ID_KW_NAMES = 271,
    XXBYTE_PYTHON_ID_POP_JUMP_BACKWARD_IF_NOT_NONE = 272,
    XXBYTE_PYTHON_ID_POP_JUMP_BACKWARD_IF_NONE = 273,
    XXBYTE_PYTHON_ID_POP_JUMP_BACKWARD_IF_FALSE = 274,
    XXBYTE_PYTHON_ID_POP_JUMP_BACKWARD_IF_TRUE = 275,
    XXBYTE_PYTHON_ID_UNPACK_SEQUENCE_ADAPTIVE = 276,
    XXBYTE_PYTHON_ID_UNPACK_SEQUENCE_LIST = 277,
    XXBYTE_PYTHON_ID_UNPACK_SEQUENCE_TUPLE = 278,
    XXBYTE_PYTHON_ID_UNPACK_SEQUENCE_TWO_TUPLE = 279,
    XXBYTE_PYTHON_ID_INTERPRETER_EXIT = 280,
    XXBYTE_PYTHON_ID_END_FOR = 281,
    XXBYTE_PYTHON_ID_END_SEND = 282,
    XXBYTE_PYTHON_ID_RESERVED = 283,
    XXBYTE_PYTHON_ID_BINARY_SLICE = 284,
    XXBYTE_PYTHON_ID_STORE_SLICE = 285,
    XXBYTE_PYTHON_ID_CALL_BOUND_METHOD_EXACT_ARGS = 286,
    XXBYTE_PYTHON_ID_CALL_BUILTIN_CLASS = 287,
    XXBYTE_PYTHON_ID_CALL_BUILTIN_FAST_WITH_KEYWORDS = 288,
    XXBYTE_PYTHON_ID_CALL_METHOD_DESCRIPTOR_FAST_WITH_KEYWORDS = 289,
    XXBYTE_PYTHON_ID_CALL_NO_KW_BUILTIN_FAST = 290,
    XXBYTE_PYTHON_ID_CALL_NO_KW_BUILTIN_O = 291,
    XXBYTE_PYTHON_ID_CALL_NO_KW_ISINSTANCE = 292,
    XXBYTE_PYTHON_ID_CALL_NO_KW_LEN = 293,
    XXBYTE_PYTHON_ID_CALL_NO_KW_LIST_APPEND = 294,
    XXBYTE_PYTHON_ID_CALL_NO_KW_METHOD_DESCRIPTOR_FAST = 295,
    XXBYTE_PYTHON_ID_CALL_NO_KW_METHOD_DESCRIPTOR_NOARGS = 296,
    XXBYTE_PYTHON_ID_CALL_NO_KW_METHOD_DESCRIPTOR_O = 297,
    XXBYTE_PYTHON_ID_CALL_NO_KW_STR_1 = 298,
    XXBYTE_PYTHON_ID_CALL_NO_KW_TUPLE_1 = 299,
    XXBYTE_PYTHON_ID_CLEANUP_THROW = 300,
    XXBYTE_PYTHON_ID_CALL_NO_KW_TYPE_1 = 301,
    XXBYTE_PYTHON_ID_COMPARE_OP_FLOAT = 302,
    XXBYTE_PYTHON_ID_COMPARE_OP_INT = 303,
    XXBYTE_PYTHON_ID_COMPARE_OP_STR = 304,
    XXBYTE_PYTHON_ID_FOR_ITER_LIST = 305,
    XXBYTE_PYTHON_ID_FOR_ITER_TUPLE = 306,
    XXBYTE_PYTHON_ID_FOR_ITER_RANGE = 307,
    XXBYTE_PYTHON_ID_FOR_ITER_GEN = 308,
    XXBYTE_PYTHON_ID_LOAD_SUPER_ATTR_ATTR = 309,
    XXBYTE_PYTHON_ID_LOAD_SUPER_ATTR_METHOD = 310,
    XXBYTE_PYTHON_ID_LOAD_ATTR_CLASS = 311,
    XXBYTE_PYTHON_ID_LOAD_ATTR_GETATTRIBUTE_OVERRIDDEN = 312,
    XXBYTE_PYTHON_ID_LOAD_ATTR_PROPERTY = 313,
    XXBYTE_PYTHON_ID_LOAD_ATTR_METHOD_LAZY_DICT = 314,
    XXBYTE_PYTHON_ID_LOAD_ATTR_METHOD_NO_DICT = 315,
    XXBYTE_PYTHON_ID_LOAD_ATTR_METHOD_WITH_VALUES = 316,
    XXBYTE_PYTHON_ID_RETURN_CONST = 317,
    XXBYTE_PYTHON_ID_LOAD_FAST_CHECK = 318,
    XXBYTE_PYTHON_ID_POP_JUMP_IF_NOT_NONE = 319,
    XXBYTE_PYTHON_ID_POP_JUMP_IF_NONE = 320,
    XXBYTE_PYTHON_ID_LOAD_SUPER_ATTR = 321,
    XXBYTE_PYTHON_ID_LOAD_FAST_AND_CLEAR = 322,
    XXBYTE_PYTHON_ID_SEND_GEN = 323,
    XXBYTE_PYTHON_ID_CALL_INTRINSIC_1 = 324,
    XXBYTE_PYTHON_ID_CALL_INTRINSIC_2 = 325,
    XXBYTE_PYTHON_ID_LOAD_FROM_DICT_OR_GLOBALS = 326,
    XXBYTE_PYTHON_ID_LOAD_FROM_DICT_OR_DEREF = 327,
    XXBYTE_PYTHON_ID_INSTRUMENTED_LOAD_SUPER_ATTR = 328,
    XXBYTE_PYTHON_ID_INSTRUMENTED_POP_JUMP_IF_NONE = 329,
    XXBYTE_PYTHON_ID_INSTRUMENTED_POP_JUMP_IF_NOT_NONE = 330,
    XXBYTE_PYTHON_ID_INSTRUMENTED_RESUME = 331,
    XXBYTE_PYTHON_ID_INSTRUMENTED_CALL = 332,
    XXBYTE_PYTHON_ID_INSTRUMENTED_RETURN_VALUE = 333,
    XXBYTE_PYTHON_ID_INSTRUMENTED_YIELD_VALUE = 334,
    XXBYTE_PYTHON_ID_INSTRUMENTED_CALL_FUNCTION_EX = 335,
    XXBYTE_PYTHON_ID_INSTRUMENTED_JUMP_FORWARD = 336,
    XXBYTE_PYTHON_ID_INSTRUMENTED_JUMP_BACKWARD = 337,
    XXBYTE_PYTHON_ID_INSTRUMENTED_RETURN_CONST = 338,
    XXBYTE_PYTHON_ID_INSTRUMENTED_FOR_ITER = 339,
    XXBYTE_PYTHON_ID_INSTRUMENTED_POP_JUMP_IF_FALSE = 340,
    XXBYTE_PYTHON_ID_INSTRUMENTED_POP_JUMP_IF_TRUE = 341,
    XXBYTE_PYTHON_ID_INSTRUMENTED_END_FOR = 342,
    XXBYTE_PYTHON_ID_INSTRUMENTED_END_SEND = 343,
    XXBYTE_PYTHON_ID_INSTRUMENTED_INSTRUCTION = 344,
    XXBYTE_PYTHON_ID_INSTRUMENTED_LINE = 345,
    XXBYTE_PYTHON_ID_EXIT_INIT_CHECK = 346,
    XXBYTE_PYTHON_ID_FORMAT_SIMPLE = 347,
    XXBYTE_PYTHON_ID_FORMAT_WITH_SPEC = 348,
    XXBYTE_PYTHON_ID_TO_BOOL = 349,
    XXBYTE_PYTHON_ID_CALL_KW = 350,
    XXBYTE_PYTHON_ID_CONVERT_VALUE = 351,
    XXBYTE_PYTHON_ID_ENTER_EXECUTOR = 352,
    XXBYTE_PYTHON_ID_LOAD_FAST_LOAD_FAST = 353,
    XXBYTE_PYTHON_ID_SET_FUNCTION_ATTRIBUTE = 354,
    XXBYTE_PYTHON_ID_STORE_FAST_LOAD_FAST = 355,
    XXBYTE_PYTHON_ID_STORE_FAST_STORE_FAST = 356,
    XXBYTE_PYTHON_ID_BINARY_SUBSCR_STR_INT = 357,
    XXBYTE_PYTHON_ID_CALL_ALLOC_AND_ENTER_INIT = 358,
    XXBYTE_PYTHON_ID_CALL_BOUND_METHOD_GENERAL = 359,
    XXBYTE_PYTHON_ID_CALL_BUILTIN_FAST = 360,
    XXBYTE_PYTHON_ID_CALL_BUILTIN_O = 361,
    XXBYTE_PYTHON_ID_CALL_ISINSTANCE = 362,
    XXBYTE_PYTHON_ID_CALL_LEN = 363,
    XXBYTE_PYTHON_ID_CALL_LIST_APPEND = 364,
    XXBYTE_PYTHON_ID_CALL_METHOD_DESCRIPTOR_FAST = 365,
    XXBYTE_PYTHON_ID_CALL_METHOD_DESCRIPTOR_NOARGS = 366,
    XXBYTE_PYTHON_ID_CALL_METHOD_DESCRIPTOR_O = 367,
    XXBYTE_PYTHON_ID_CALL_NON_PY_GENERAL = 368,
    XXBYTE_PYTHON_ID_CALL_PY_GENERAL = 369,
    XXBYTE_PYTHON_ID_CALL_STR_1 = 370,
    XXBYTE_PYTHON_ID_CALL_TUPLE_1 = 371,
    XXBYTE_PYTHON_ID_CALL_TYPE_1 = 372,
    XXBYTE_PYTHON_ID_CONTAINS_OP_DICT = 373,
    XXBYTE_PYTHON_ID_CONTAINS_OP_SET = 374,
    XXBYTE_PYTHON_ID_LOAD_ATTR_NONDESCRIPTOR_NO_DICT = 375,
    XXBYTE_PYTHON_ID_LOAD_ATTR_NONDESCRIPTOR_WITH_VALUES = 376,
    XXBYTE_PYTHON_ID_RESUME_CHECK = 377,
    XXBYTE_PYTHON_ID_TO_BOOL_ALWAYS_TRUE = 378,
    XXBYTE_PYTHON_ID_TO_BOOL_BOOL = 379,
    XXBYTE_PYTHON_ID_TO_BOOL_INT = 380,
    XXBYTE_PYTHON_ID_TO_BOOL_LIST = 381,
    XXBYTE_PYTHON_ID_TO_BOOL_NONE = 382,
    XXBYTE_PYTHON_ID_TO_BOOL_STR = 383,
    XXBYTE_PYTHON_ID_INSTRUMENTED_CALL_KW = 384,
    XXBYTE_PYTHON_ID_BUILD_TEMPLATE = 385,
    XXBYTE_PYTHON_ID_NOT_TAKEN = 386,
    XXBYTE_PYTHON_ID_POP_ITER = 387,
    XXBYTE_PYTHON_ID_BUILD_INTERPOLATION = 388,
    XXBYTE_PYTHON_ID_LOAD_COMMON_CONSTANT = 389,
    XXBYTE_PYTHON_ID_LOAD_FAST_BORROW = 390,
    XXBYTE_PYTHON_ID_LOAD_FAST_BORROW_LOAD_FAST_BORROW = 391,
    XXBYTE_PYTHON_ID_LOAD_SMALL_INT = 392,
    XXBYTE_PYTHON_ID_LOAD_SPECIAL = 393,
    XXBYTE_PYTHON_ID_BINARY_OP_EXTEND = 394,
    XXBYTE_PYTHON_ID_BINARY_OP_SUBSCR_DICT = 395,
    XXBYTE_PYTHON_ID_BINARY_OP_SUBSCR_GETITEM = 396,
    XXBYTE_PYTHON_ID_BINARY_OP_SUBSCR_LIST_INT = 397,
    XXBYTE_PYTHON_ID_BINARY_OP_SUBSCR_LIST_SLICE = 398,
    XXBYTE_PYTHON_ID_BINARY_OP_SUBSCR_STR_INT = 399,
    XXBYTE_PYTHON_ID_BINARY_OP_SUBSCR_TUPLE_INT = 400,
    XXBYTE_PYTHON_ID_CALL_KW_BOUND_METHOD = 401,
    XXBYTE_PYTHON_ID_CALL_KW_NON_PY = 402,
    XXBYTE_PYTHON_ID_CALL_KW_PY = 403,
    XXBYTE_PYTHON_ID_JUMP_BACKWARD_JIT = 404,
    XXBYTE_PYTHON_ID_JUMP_BACKWARD_NO_JIT = 405,
    XXBYTE_PYTHON_ID_LOAD_ATTR_CLASS_WITH_METACLASS_CHECK = 406,
    XXBYTE_PYTHON_ID_LOAD_CONST_IMMORTAL = 407,
    XXBYTE_PYTHON_ID_LOAD_CONST_MORTAL = 408,
    XXBYTE_PYTHON_ID_INSTRUMENTED_POP_ITER = 409,
    XXBYTE_PYTHON_ID_INSTRUMENTED_NOT_TAKEN = 410,
    XXBYTE_PYTHON_ID_INSTRUMENTED_END_ASYNC_FOR = 411,
    XXBYTE_PYTHON_ID_COUNT = 412 /* one past the last id */
} xxbyte_python_id;

/* Maps a .pyc magic number (first two bytes, little-endian) to a decode
 * mode. Alpha, beta and release candidate magics map to their release, and
 * the "-U" variants of 2.x (magic + 1) are accepted. Returns 0 for an
 * unknown magic number. */
XXBYTE_API uint32_t xxbyte_python_mode_from_magic(uint16_t magic);

/* The name of opcode in mode, or NULL when it is undefined there or mode is
 * unsupported. */
XXBYTE_API const char *xxbyte_python_opname(uint32_t mode, uint8_t opcode);

/* PEP 552 flags of a 3.7+ .pyc header. */
#define XXBYTE_PYTHON_PYC_HASH_BASED 0x1u
#define XXBYTE_PYTHON_PYC_CHECK_SOURCE 0x2u

/* A parsed .pyc header. */
typedef struct xxbyte_python_pyc_header {
    uint16_t magic;
    uint32_t mode;        /* xxbyte_python_mode_from_magic(magic) */
    uint32_t header_size; /* 8, 12 (3.3 to 3.6) or 16 (3.7+) */
    uint32_t flags;       /* XXBYTE_PYTHON_PYC_*, 3.7+ */
    uint32_t mtime;       /* source mtime, 0 for hash-based files */
    uint32_t source_size; /* source size modulo 2**32, 3.3+ */
    uint64_t source_hash; /* SipHash of the source, hash-based files */
} xxbyte_python_pyc_header;

/*
 * Parses the .pyc header at data[0]. Returns XXBYTE_STATUS_TRUNCATED when
 * size is too small, XXBYTE_STATUS_INVALID_INSTRUCTION when the magic is not
 * followed by "\r\n" or the PEP 552 flags are invalid, and
 * XXBYTE_STATUS_UNSUPPORTED_MODE for an unknown magic number. The marshalled
 * module code object starts at header_size.
 */
XXBYTE_API xxbyte_status xxbyte_python_parse_pyc_header(const uint8_t *data, size_t size, xxbyte_python_pyc_header *header);

/*
 * Locates co_code of the marshalled code object at data[0] (for a .pyc, at
 * header_size) using the code-object layout of the release of mode:
 * code_offset receives the offset of the first bytecode byte from data[0]
 * and code_size its length. Returns XXBYTE_STATUS_INVALID_INSTRUCTION when
 * data does not hold a code object or co_code is not stored inline, and
 * XXBYTE_STATUS_TRUNCATED when co_code runs past size.
 *
 * A few development magic numbers marshal code objects differently from
 * their release; use xxbyte_python_find_code_magic for files that may
 * carry one.
 */
XXBYTE_API xxbyte_status xxbyte_python_find_code(uint32_t mode, const uint8_t *data, size_t size, uint32_t *code_offset, uint32_t *code_size);

/*
 * Like xxbyte_python_find_code, with the code-object layout of the .pyc
 * magic number (xxbyte_python_pyc_header.magic), which also covers the
 * magics whose layout differs from their release: 3000 to 3029 (before
 * kwonlyargcount), 3.8a1 3400 and 3401 (before posonlyargcount) and 3.11a1
 * 3450 and 3451 (still with nlocals). Returns
 * XXBYTE_STATUS_UNSUPPORTED_MODE for an unknown magic number.
 */
XXBYTE_API xxbyte_status xxbyte_python_find_code_magic(uint16_t magic, const uint8_t *data, size_t size, uint32_t *code_offset, uint32_t *code_size);

/*
 * Container model: the marshal reader.
 *
 * xxbyte_python_open_pyc parses a whole .pyc file (header and marshalled
 * module) into an xxbyte_python_file; xxbyte_python_open_marshal parses a
 * bare marshal stream (for example a code object embedded by a freezer or
 * a PyInstaller archive, whose header was stripped). The reader follows
 * CPython's marshal.c for the reader of the decode mode:
 *   - every type code of marshal versions 0 to 5: None, True, False,
 *     Ellipsis, StopIteration, NULL (dict terminator), int, int64, long
 *     (15-bit digits), text and binary float and complex, str/bytes,
 *     unicode, interned, ASCII, short ASCII (and their interned forms),
 *     tuple, small tuple, list, dict, set, frozenset, code and the 3.14
 *     slice;
 *   - references: TYPE_INTERNED/TYPE_STRINGREF in 2.x, FLAG_REF/TYPE_REF
 *     from 3.4 (a code object, non-empty frozenset or slice is registered
 *     only once complete, as CPython does, so a reference to it from inside
 *     itself is invalid; a tuple, list, dict or set may contain itself);
 *   - code objects of every layout: 2.0 (16-bit fields, no cell/free
 *     names), 2.1/2.2 (16-bit), 2.3 to 2.7, 3.0 to 3.7 (kwonlyargcount from
 *     magic 3030), 3.8 to 3.10 (posonlyargcount), 3.11+ (no nlocals,
 *     co_localsplusnames/co_localspluskinds, co_qualname, exception table).
 *     The 3.11 alpha magics 3450 to 3493 follow their documented steps
 *     (nlocals dropped at 3452, localsplus at 3453, qualname at 3460, the
 *     end-line and column tables of 3459 to 3493); those intermediate
 *     layouts are not verified against a running interpreter.
 * The mode decides the reader (2.x readers know no FLAG_REF and treat 't'
 * as an interned byte string; 3.x readers know no TYPE_STRINGREF) and the
 * magic number the code-object layout.
 *
 * The reader is bounded: it never reads past size, nesting is limited to
 * XXBYTE_PYTHON_MAX_MARSHAL_DEPTH objects (CPython allows 2000, 1000 on
 * Windows), every count is checked against the bytes left before anything
 * is allocated (memory stays linear in the input), and malformed input is
 * XXBYTE_STATUS_INVALID_INSTRUCTION (XXBYTE_STATUS_TRUNCATED when the
 * stream ends early). It is lenient where CPython would only fail later:
 * set and dict elements need not be hashable, strings need not be valid
 * UTF-8 (repr shows an invalid byte NN as "\udcNN", its surrogateescape
 * value), and a long need not be normalized. The only field checked is
 * co_code, which must be a byte string. The parsed tree takes at most a
 * few tens of bytes per input byte (20 to 30 for a stream of empty tuples).
 *
 * The code objects are walked in the order dis.dis prints them: the
 * module, then depth-first every code object among each code object's
 * co_consts (not inside tuples). Shared references are listed each time
 * they occur, as dis does; the walk stops after
 * XXBYTE_PYTHON_MAX_CODE_OBJECTS entries, because references can make it
 * exponential in the file size.
 */
#define XXBYTE_PYTHON_MAX_MARSHAL_DEPTH 500u
#define XXBYTE_PYTHON_MAX_CODE_OBJECTS 65536u
#define XXBYTE_PYTHON_NO_PARENT 0xFFFFFFFFu

typedef struct xxbyte_python_file xxbyte_python_file;

/*
 * Parses a .pyc file. With mode XXBYTE_MODE_AUTO (0xFFFFFFFF) the mode and
 * the code-object layout come from the magic number; another mode
 * overrides them with that release's reader and layout, and then an unknown
 * magic number is accepted too (the header is taken to be 8 bytes before
 * 3.3, 12 for 3.3 to 3.6 and 16 from 3.7). The root object must be a code
 * object. file receives a new object to release with xxbyte_python_close;
 * it borrows data, which must outlive it.
 */
XXBYTE_API xxbyte_status xxbyte_python_open_pyc(uint32_t mode, const uint8_t *data, size_t size, xxbyte_python_file **file);

/*
 * Parses the marshal stream at data[offset] with the reader of the
 * release of magic (a .pyc magic number) and its code-object layout. Any
 * root object is accepted; only a code object has code objects to walk.
 */
XXBYTE_API xxbyte_status xxbyte_python_open_marshal(uint16_t magic, const uint8_t *data, size_t size, size_t offset, xxbyte_python_file **file);

XXBYTE_API void xxbyte_python_close(xxbyte_python_file *file);

/* The decode mode of the file, and the offset just past its root object. */
XXBYTE_API uint32_t xxbyte_python_file_mode(const xxbyte_python_file *file);
XXBYTE_API size_t xxbyte_python_file_end(const xxbyte_python_file *file);

/* Nonzero when the walk stopped at XXBYTE_PYTHON_MAX_CODE_OBJECTS (or at
 * the depth limit) before every code object was listed. */
XXBYTE_API int xxbyte_python_walk_truncated(const xxbyte_python_file *file);

/* One code object of the walk. Offsets are from data[0] of the buffer
 * passed to the open call. A text field that is not a string has offset
 * and size 0. */
typedef struct xxbyte_python_code_object {
    uint32_t offset;      /* of the code object's type byte */
    uint32_t parent;      /* walk index of the code object whose co_consts holds it */
    uint32_t depth;       /* 0 for the root */
    uint32_t code_offset; /* co_code: first bytecode byte */
    uint32_t code_size;
    int32_t argcount;
    int32_t posonlyargcount; /* 3.8+ */
    int32_t kwonlyargcount;  /* 3.0+ */
    int32_t nlocals;         /* until 3.10 */
    int32_t stacksize;
    uint32_t flags;       /* co_flags */
    int32_t first_line;   /* co_firstlineno */
    uint32_t name_offset; /* co_name bytes (UTF-8, Latin-1 for the ASCII types, raw for 2.x str) */
    uint32_t name_size;
    uint32_t qualname_offset; /* co_qualname from 3.11, else co_name */
    uint32_t qualname_size;
    uint32_t filename_offset; /* co_filename */
    uint32_t filename_size;
    uint32_t line_table_offset; /* co_lnotab, or co_linetable from 3.10 */
    uint32_t line_table_size;
    uint32_t exception_table_offset; /* 3.11+ co_exceptiontable */
    uint32_t exception_table_size;
    uint32_t const_count; /* len(co_consts) */
    uint32_t name_count;  /* len(co_names) */
    uint32_t local_count; /* len(co_varnames) */
    uint32_t cell_count;  /* len(co_cellvars) */
    uint32_t free_count;  /* len(co_freevars) */
    uint32_t fast_count;  /* 3.11+ len(co_localsplusnames), else 0 */
} xxbyte_python_code_object;

/* The number of code objects in the walk, and entry index of it. */
XXBYTE_API size_t xxbyte_python_code_count(const xxbyte_python_file *file);
XXBYTE_API xxbyte_status xxbyte_python_get_code(const xxbyte_python_file *file, size_t index, xxbyte_python_code_object *object);

/*
 * Fills objects[0 .. capacity) with the walk of a .pyc (as
 * xxbyte_python_open_pyc reads it) and sets *count to the total number of
 * entries, which may exceed capacity. objects may be NULL when capacity is
 * 0, to count.
 */
XXBYTE_API xxbyte_status xxbyte_python_find_code_objects(uint32_t mode, const uint8_t *data, size_t size, xxbyte_python_code_object *objects, size_t capacity,
                                                         size_t *count);

/* One 3.11+ exception-table entry, as dis parses it (byte offsets; end is
 * exclusive). */
typedef struct xxbyte_python_exception_entry {
    uint32_t start;
    uint32_t end;
    uint32_t target;
    uint32_t depth;
    uint32_t lasti;
} xxbyte_python_exception_entry;

/* Fills entries with the exception table of code object index and sets
 * *count to the total entry count (0 before 3.11). A trailing partial entry
 * is dropped, as dis drops it. */
XXBYTE_API xxbyte_status xxbyte_python_exception_entries(const xxbyte_python_file *file, size_t index, xxbyte_python_exception_entry *entries, size_t capacity,
                                                         size_t *count);

/*
 * Text helpers. Each writes a NUL-terminated string of at most
 * buffer_size - 1 bytes (UTF-8 for Python 3 strings, the raw bytes of a
 * Python 2 str) and returns its length, truncating silently; 0 when the
 * item does not exist or buffer_size is 0.
 *
 * xxbyte_python_code_repr: the repr dis prints for code object index,
 * "<code object NAME at 0x..., file \"FILE\", line N>", where the address
 * (a memory address in CPython) is the file offset of the code object and
 * N is -1 when co_firstlineno is 0. 2.x and 3.0/3.1 cut the name at 100
 * and the file name at 300 bytes, as they do.
 *
 * xxbyte_python_const_repr: repr(co_consts[i]) for code object index, with
 * the repr rules of the file's version: str quoting and escapes (Python 3
 * keeps the non-ASCII characters that str.isprintable() accepts in that
 * release, Unicode 5.1 in 3.0 to 16.0 in 3.14; Python 2 unicode escapes all
 * of them, with u'' quotes; 2.0 uses octal escapes and upper-case \u,
 * 2.0 and 2.1 show astral characters as two surrogates), b'' bytes, 2.x
 * "L" longs, float repr (the shortest round trip from 2.7 and 3.1, "%.17g"
 * before, ".0" added to integral values; the text floats of marshal
 * versions 0 and 1 are parsed, correctly rounded, first), complex ("%.12g"
 * parts in 2.0), tuple, list, dict, set
 * ("set()", "set([...])" in 2.x), frozenset ("frozenset({...})",
 * "frozenset([...])" in 2.x), slice, code objects as above, and "(...)"
 * for a container inside itself. Set and dict items appear in file order
 * (CPython shows hash order, which differs between runs for strings).
 * Negative indexes count from the end once, as in Python.
 *
 * xxbyte_python_name_text: the name dis shows for an index operand of
 * kind XXBYTE_INDEX_NAME (co_names), XXBYTE_INDEX_LOCAL (co_varnames;
 * co_localsplusnames from 3.11) or XXBYTE_INDEX_FREE (co_cellvars +
 * co_freevars; co_localsplusnames from 3.11).
 *
 * xxbyte_python_code_text: co_name (what 0), co_qualname (1, co_name before
 * 3.11) or co_filename (2) of code object index.
 */
XXBYTE_API size_t xxbyte_python_code_repr(const xxbyte_python_file *file, size_t index, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_python_const_repr(const xxbyte_python_file *file, size_t index, int64_t const_index, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_python_name_text(const xxbyte_python_file *file, size_t index, uint32_t index_kind, int64_t item, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_python_code_text(const xxbyte_python_file *file, size_t index, uint32_t what, char *buffer, size_t buffer_size);

/*
 * The resolver. Select the code object first; its context argument is the
 * xxbyte_python_file. It names
 *   - XXBYTE_INDEX_CONSTANT operands with xxbyte_python_const_repr,
 *   - XXBYTE_INDEX_NAME, _LOCAL and _FREE operands with
 *     xxbyte_python_name_text,
 *   - from 3.13, BRANCH operands with the label dis prints ("L3"), which
 *     numbers every jump target and exception-table start, end and target
 *     of the code object in offset order; before 3.13 it leaves them to
 *     the formatter's "(to N)".
 * Decode co_code with address 0 at its first byte, as dis numbers it. The
 * decoder's formatter adds what dis adds around the names ("NULL + ",
 * " + NULL|self" and so on), so with this resolver an instruction formats
 * as dis prints opname, argument and "(argrepr)". The resolver writes at
 * most buffer_size - 1 bytes; xxbyte_format passes 512, so a longer repr
 * is cut there.
 *
 * xxbyte_python_select prepares code object index (the 3.13+ labels need a
 * sweep of its code) and returns XXBYTE_STATUS_OUT_OF_MEMORY or
 * XXBYTE_STATUS_INVALID_ARGUMENT for an index past the walk. The file holds
 * one selection, so share a file between threads only for reading.
 */
XXBYTE_API xxbyte_status xxbyte_python_select(xxbyte_python_file *file, size_t index);
XXBYTE_API size_t xxbyte_python_resolver(void *file, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/*
 * The container listing (xxbyte_disasm_file with XXBYTE_FAMILY_PYTHON):
 * a .pyc file as xxbyte_python_open_pyc reads it (mode XXBYTE_MODE_AUTO
 * takes the version from the magic number). For every code object of the
 * walk it prints
 *     Disassembly of <code object f at 0x1a2, file "m.py", line 3>:
 * then the linear listing of co_code from offset 0 with the resolver above;
 * from 3.13 a line "L<n>:" before each labelled instruction, as dis shows
 * labels; from 3.11 the "ExceptionTable:" block in the version's dis
 * format ("  4 to 20 -> 24 [0] lasti" in 3.11 and 3.12, labels from 3.13);
 * and an empty line. dis's line-number column, ">>" markers, CACHE entries
 * and EXTENDED_ARG lines are not shown (EXTENDED_ARG is merged as in every
 * xxbyte decode). A walk cut at XXBYTE_PYTHON_MAX_CODE_OBJECTS ends with
 * the line "# walk truncated", and so does a listing stopped once the
 * bytecode listed would exceed four times the file size plus 1 MiB (shared
 * code objects are listed at each occurrence).
 *
 * Checked against dis: the stdlib of CPython 2.1 to 3.14 (every release
 * line, one build each), compiled by that release, every code object
 * compared with what its dis prints (dis.Bytecode from 3.4, the text of
 * dis.disassemble before), opname, argument, argrepr and exception table.
 * Known differences:
 *   - "(to N)" on the absolute jumps of 2.x to 3.9 (see above);
 *   - set and frozenset items in file order, where CPython prints them in
 *     hash order (which changes from run to run for strings);
 *   - text longer than the 511 bytes of the resolver buffer is cut, and a
 *     long of more than 120 digits of 15 bits (1800 bits, 542 decimal
 *     digits) shows as "<int of N bits>";
 *   - code-object addresses are file offsets;
 *   - 3.0 to 3.2 treat astral characters as the repr of their default
 *     narrow builds does (3.0 as 3.1; a wide build may differ); 3.0 and 3.1 cut
 *     the name and file name of a code-object repr at 100 and 300 bytes,
 *     not characters;
 *   - 2.3 optimizes code objects as it loads them (jump threading in
 *     PyCode_New), so its dis shows jumps that differ from the file;
 *     xxbyte shows the bytes in the file.
 */

#ifdef __cplusplus
}
#endif

#endif
