/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_GDSCRIPT_H
#define XXBYTE_XXBYTE_GDSCRIPT_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_GDSCRIPT
#error "the Godot GDScript bytecode decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Godot GDScript binary token streams: the "compiled" scripts (.gdc) that
 * the Godot editor exports.
 *
 * What Godot stores. GDScript is compiled to its VM bytecode only in memory,
 * when a script is loaded; no Godot version writes that bytecode to a file.
 * What an exported project contains instead is the script's token stream:
 * GDScriptTokenizerBuffer::parse_code_string turns the source into a "GDSC"
 * file with an identifier table, a constant table (marshalled Variants), a
 * line map and the tokens, and the engine feeds those tokens to its parser
 * when it loads the .gdc. This family decodes that token format: one
 * instruction is one token. Godot 4.0 to 4.2 export scripts only as text,
 * so they have no mode. The in-memory VM opcodes (GDScriptFunction::Opcode)
 * are not decoded: no file holds them.
 *
 * Modes (the format and the token and built-in tables changed between
 * releases; the version field alone does not tell 3.1 from 3.5):
 *   XXBYTE_GDSCRIPT_MODE_V4_5    Godot 4.5 and later, TOKENIZER_VERSION 101
 *                                (adds "..." to the token enum)
 *   XXBYTE_GDSCRIPT_MODE_V4_3    Godot 4.3 and 4.4, TOKENIZER_VERSION 100
 *   XXBYTE_GDSCRIPT_MODE_V3_5    Godot 3.5 and 3.6, BYTECODE_VERSION 13,
 *                                91 built-in functions (adds deep_equal)
 *   XXBYTE_GDSCRIPT_MODE_V3_2    Godot 3.2 to 3.4, BYTECODE_VERSION 13, 90
 *   XXBYTE_GDSCRIPT_MODE_V3_1_1  Godot 3.1.1 and 3.1.2, BYTECODE_VERSION 13,
 *                                83 (adds smoothstep)
 *   XXBYTE_GDSCRIPT_MODE_V3_1    Godot 3.1, BYTECODE_VERSION 13, 82
 *   XXBYTE_GDSCRIPT_MODE_V3_0    Godot 3.0 to 3.0.6, BYTECODE_VERSION 12, 77
 *                                (the 3.0 token enum, with do, switch and
 *                                case and without class_name, as, void,
 *                                puppet, remotesync, mastersync, puppetsync
 *                                and ->)
 * XXBYTE_GDSCRIPT_MODE_DEFAULT (0) is XXBYTE_GDSCRIPT_MODE_V4_5. Any other
 * mode returns XXBYTE_STATUS_UNSUPPORTED_MODE. Godot 1.x and 2.x token
 * streams (BYTECODE_VERSION 3, 4 and 10) are not supported.
 *
 * Token encodings. Addresses are byte offsets, normally in the .gdc file
 * (for a compressed 4.x file, in its decompressed image). Decoding needs no
 * context; xxbyte_decode_ex ignores it.
 *   4.x  5 bytes: a type byte below 0x80, then the source line as a u32; or
 *        8 bytes: a u32 whose bit 7 is set, with the type in bits 0-6 and
 *        an index in bits 8-31, then the line as a u32. The type must be
 *        below the mode's token count (100 in V4_5, 99 in V4_3). Godot
 *        writes every token except "empty" in the 8-byte form. As in the
 *        engine, the index of an identifier, annotation, literal or error
 *        token in the 5-byte form is the u32 at the token's first byte
 *        shifted right by 8 (the low 24 bits of the line); the index bits
 *        of other tokens are ignored, as the engine ignores them.
 *   3.x  1 byte: a type below 0x80; or 4 bytes: a u32 whose bit 7 is set,
 *        with the type in bits 0-6 and an operand in bits 8-31 (Godot uses
 *        this form only when the operand is not 0). The type must be below
 *        the mode's token count (98, or 93 in V3_0). The operand of other
 *        tokens than identifier, constant, built-in type, built-in function
 *        and newline is ignored. 3.x tokens carry no line: the line map of
 *        the file gives it (see xxbyte_gdscript_file).
 *
 * Mnemonics are the token enum names of the Godot tokenizer, lower-cased
 * and without the TK_ prefix: 4.x "identifier", "literal", "func",
 * "less_equal", "period_period_period", "const", "eof"; 3.x "identifier",
 * "constant", "built_in_func", "op_assign_add", "pr_function", "cf_if",
 * "newline", "eof".
 *
 * Operands, in this order:
 *   identifier, annotation (4.x), identifier (3.x)
 *       INDEX, kind XXBYTE_INDEX_NAME: the identifier table entry, size 3.
 *       Without a resolver "ident[5]".
 *   literal, error (4.x), constant (3.x)
 *       INDEX, kind XXBYTE_INDEX_CONSTANT: the constant table entry, size 3.
 *       Without a resolver "const[2]".
 *   built_in_type (3.x)
 *       INDEX, kind XXBYTE_GDSCRIPT_INDEX_TYPE: a Variant::Type (0 to 26),
 *       size 3, printed as its source name ("int", "Vector2",
 *       "PoolByteArray") when the resolver gives nothing.
 *   built_in_func (3.x)
 *       INDEX, kind XXBYTE_GDSCRIPT_INDEX_BUILTIN: a GDScriptFunctions
 *       index into the mode's table, size 3, printed as the function name
 *       ("sin", "print", "deep_equal") when the resolver gives nothing. An
 *       index past the table is INVALID_INSTRUCTION.
 *   newline (3.x)
 *       IMMEDIATE with XXBYTE_GDSCRIPT_OPERAND_FLAG_INDENT: the indentation
 *       (in characters) of the next line, size 3 (0 in the 1-byte form),
 *       printed "indent 1".
 *   every 4.x token, last
 *       IMMEDIATE with XXBYTE_GDSCRIPT_OPERAND_FLAG_LINE: the source line,
 *       size 4, printed "line 3".
 * Examples (V4_5):
 *   82 01 00 00 03 00 00 00  ->  identifier ident[1], line 3
 *                                identifier position, line 3 (resolved)
 *   83 00 00 00 03 00 00 00  ->  literal const[0], line 3
 *                                literal "hello", line 3 (resolved)
 *   bc 00 00 00 07 00 00 00  ->  func line 7
 *   00 01 00 00 00           ->  empty line 1
 * Examples (V3_5):
 *   01                       ->  identifier ident[0]
 *   81 05 00 00              ->  identifier ident[5]
 *   85 3f 00 00              ->  built_in_func print
 *   84 02 00 00              ->  built_in_type int
 *   d9 01 00 00              ->  newline indent 1
 *   59                       ->  newline indent 0
 *   26                       ->  cf_if
 * Groups: tokens are not executed, so every token is XXBYTE_GROUP_NONE.
 */
#define XXBYTE_GDSCRIPT_MODE_DEFAULT 0u
#define XXBYTE_GDSCRIPT_MODE_V4_5 1u
#define XXBYTE_GDSCRIPT_MODE_V4_3 2u
#define XXBYTE_GDSCRIPT_MODE_V3_5 3u
#define XXBYTE_GDSCRIPT_MODE_V3_2 4u
#define XXBYTE_GDSCRIPT_MODE_V3_1_1 5u
#define XXBYTE_GDSCRIPT_MODE_V3_1 6u
#define XXBYTE_GDSCRIPT_MODE_V3_0 7u
#define XXBYTE_GDSCRIPT_MODE_NEWEST XXBYTE_GDSCRIPT_MODE_V4_5
/* Nonzero for the Godot 4 modes (DEFAULT included). */
#define XXBYTE_GDSCRIPT_MODE_IS_V4(mode) ((mode) <= XXBYTE_GDSCRIPT_MODE_V4_3)

/* Family index kinds. */
#define XXBYTE_GDSCRIPT_INDEX_TYPE ((xxbyte_index_kind)(XXBYTE_INDEX_FAMILY_FIRST + 0))    /* 3.x Variant::Type */
#define XXBYTE_GDSCRIPT_INDEX_BUILTIN ((xxbyte_index_kind)(XXBYTE_INDEX_FAMILY_FIRST + 1)) /* 3.x built-in function */

/* Family operand flags. */
#define XXBYTE_GDSCRIPT_OPERAND_FLAG_LINE 0x1000u   /* a 4.x token's source line */
#define XXBYTE_GDSCRIPT_OPERAND_FLAG_INDENT 0x2000u /* a 3.x newline's indentation */

/*
 * Mnemonic identifiers (xxbyte_instruction.id). 1 to 100 are the 4.x
 * tokens in TOKENIZER_VERSION 101 order (the type plus one; in V4_3, which
 * lacks PERIOD_PERIOD_PERIOD, a type t from 83 up has id t + 2). 101 to 201
 * are the 3.x tokens: the 3.1 to 3.6 enum in its order, then the three
 * tokens only 3.0 has (do, switch, case). Valid ids are 1 to
 * XXBYTE_GDSCRIPT_ID_COUNT - 1.
 */
typedef enum xxbyte_gdscript_id {
    XXBYTE_GDSCRIPT_ID_INVALID = 0,
    XXBYTE_GDSCRIPT_ID_EMPTY = 1,
    XXBYTE_GDSCRIPT_ID_ANNOTATION = 2,
    XXBYTE_GDSCRIPT_ID_IDENTIFIER = 3,
    XXBYTE_GDSCRIPT_ID_LITERAL = 4,
    XXBYTE_GDSCRIPT_ID_LESS = 5,
    XXBYTE_GDSCRIPT_ID_LESS_EQUAL = 6,
    XXBYTE_GDSCRIPT_ID_GREATER = 7,
    XXBYTE_GDSCRIPT_ID_GREATER_EQUAL = 8,
    XXBYTE_GDSCRIPT_ID_EQUAL_EQUAL = 9,
    XXBYTE_GDSCRIPT_ID_BANG_EQUAL = 10,
    XXBYTE_GDSCRIPT_ID_AND = 11,
    XXBYTE_GDSCRIPT_ID_OR = 12,
    XXBYTE_GDSCRIPT_ID_NOT = 13,
    XXBYTE_GDSCRIPT_ID_AMPERSAND_AMPERSAND = 14,
    XXBYTE_GDSCRIPT_ID_PIPE_PIPE = 15,
    XXBYTE_GDSCRIPT_ID_BANG = 16,
    XXBYTE_GDSCRIPT_ID_AMPERSAND = 17,
    XXBYTE_GDSCRIPT_ID_PIPE = 18,
    XXBYTE_GDSCRIPT_ID_TILDE = 19,
    XXBYTE_GDSCRIPT_ID_CARET = 20,
    XXBYTE_GDSCRIPT_ID_LESS_LESS = 21,
    XXBYTE_GDSCRIPT_ID_GREATER_GREATER = 22,
    XXBYTE_GDSCRIPT_ID_PLUS = 23,
    XXBYTE_GDSCRIPT_ID_MINUS = 24,
    XXBYTE_GDSCRIPT_ID_STAR = 25,
    XXBYTE_GDSCRIPT_ID_STAR_STAR = 26,
    XXBYTE_GDSCRIPT_ID_SLASH = 27,
    XXBYTE_GDSCRIPT_ID_PERCENT = 28,
    XXBYTE_GDSCRIPT_ID_EQUAL = 29,
    XXBYTE_GDSCRIPT_ID_PLUS_EQUAL = 30,
    XXBYTE_GDSCRIPT_ID_MINUS_EQUAL = 31,
    XXBYTE_GDSCRIPT_ID_STAR_EQUAL = 32,
    XXBYTE_GDSCRIPT_ID_STAR_STAR_EQUAL = 33,
    XXBYTE_GDSCRIPT_ID_SLASH_EQUAL = 34,
    XXBYTE_GDSCRIPT_ID_PERCENT_EQUAL = 35,
    XXBYTE_GDSCRIPT_ID_LESS_LESS_EQUAL = 36,
    XXBYTE_GDSCRIPT_ID_GREATER_GREATER_EQUAL = 37,
    XXBYTE_GDSCRIPT_ID_AMPERSAND_EQUAL = 38,
    XXBYTE_GDSCRIPT_ID_PIPE_EQUAL = 39,
    XXBYTE_GDSCRIPT_ID_CARET_EQUAL = 40,
    XXBYTE_GDSCRIPT_ID_IF = 41,
    XXBYTE_GDSCRIPT_ID_ELIF = 42,
    XXBYTE_GDSCRIPT_ID_ELSE = 43,
    XXBYTE_GDSCRIPT_ID_FOR = 44,
    XXBYTE_GDSCRIPT_ID_WHILE = 45,
    XXBYTE_GDSCRIPT_ID_BREAK = 46,
    XXBYTE_GDSCRIPT_ID_CONTINUE = 47,
    XXBYTE_GDSCRIPT_ID_PASS = 48,
    XXBYTE_GDSCRIPT_ID_RETURN = 49,
    XXBYTE_GDSCRIPT_ID_MATCH = 50,
    XXBYTE_GDSCRIPT_ID_WHEN = 51,
    XXBYTE_GDSCRIPT_ID_AS = 52,
    XXBYTE_GDSCRIPT_ID_ASSERT = 53,
    XXBYTE_GDSCRIPT_ID_AWAIT = 54,
    XXBYTE_GDSCRIPT_ID_BREAKPOINT = 55,
    XXBYTE_GDSCRIPT_ID_CLASS = 56,
    XXBYTE_GDSCRIPT_ID_CLASS_NAME = 57,
    XXBYTE_GDSCRIPT_ID_CONST = 58,
    XXBYTE_GDSCRIPT_ID_ENUM = 59,
    XXBYTE_GDSCRIPT_ID_EXTENDS = 60,
    XXBYTE_GDSCRIPT_ID_FUNC = 61,
    XXBYTE_GDSCRIPT_ID_IN = 62,
    XXBYTE_GDSCRIPT_ID_IS = 63,
    XXBYTE_GDSCRIPT_ID_NAMESPACE = 64,
    XXBYTE_GDSCRIPT_ID_PRELOAD = 65,
    XXBYTE_GDSCRIPT_ID_SELF = 66,
    XXBYTE_GDSCRIPT_ID_SIGNAL = 67,
    XXBYTE_GDSCRIPT_ID_STATIC = 68,
    XXBYTE_GDSCRIPT_ID_SUPER = 69,
    XXBYTE_GDSCRIPT_ID_TRAIT = 70,
    XXBYTE_GDSCRIPT_ID_VAR = 71,
    XXBYTE_GDSCRIPT_ID_VOID = 72,
    XXBYTE_GDSCRIPT_ID_YIELD = 73,
    XXBYTE_GDSCRIPT_ID_BRACKET_OPEN = 74,
    XXBYTE_GDSCRIPT_ID_BRACKET_CLOSE = 75,
    XXBYTE_GDSCRIPT_ID_BRACE_OPEN = 76,
    XXBYTE_GDSCRIPT_ID_BRACE_CLOSE = 77,
    XXBYTE_GDSCRIPT_ID_PARENTHESIS_OPEN = 78,
    XXBYTE_GDSCRIPT_ID_PARENTHESIS_CLOSE = 79,
    XXBYTE_GDSCRIPT_ID_COMMA = 80,
    XXBYTE_GDSCRIPT_ID_SEMICOLON = 81,
    XXBYTE_GDSCRIPT_ID_PERIOD = 82,
    XXBYTE_GDSCRIPT_ID_PERIOD_PERIOD = 83,
    XXBYTE_GDSCRIPT_ID_PERIOD_PERIOD_PERIOD = 84,
    XXBYTE_GDSCRIPT_ID_COLON = 85,
    XXBYTE_GDSCRIPT_ID_DOLLAR = 86,
    XXBYTE_GDSCRIPT_ID_FORWARD_ARROW = 87,
    XXBYTE_GDSCRIPT_ID_UNDERSCORE = 88,
    XXBYTE_GDSCRIPT_ID_NEWLINE = 89,
    XXBYTE_GDSCRIPT_ID_INDENT = 90,
    XXBYTE_GDSCRIPT_ID_DEDENT = 91,
    XXBYTE_GDSCRIPT_ID_CONST_PI = 92,
    XXBYTE_GDSCRIPT_ID_CONST_TAU = 93,
    XXBYTE_GDSCRIPT_ID_CONST_INF = 94,
    XXBYTE_GDSCRIPT_ID_CONST_NAN = 95,
    XXBYTE_GDSCRIPT_ID_VCS_CONFLICT_MARKER = 96,
    XXBYTE_GDSCRIPT_ID_BACKTICK = 97,
    XXBYTE_GDSCRIPT_ID_QUESTION_MARK = 98,
    XXBYTE_GDSCRIPT_ID_ERROR = 99,
    XXBYTE_GDSCRIPT_ID_EOF = 100,
    XXBYTE_GDSCRIPT_ID_V3_EMPTY = 101,
    XXBYTE_GDSCRIPT_ID_V3_IDENTIFIER = 102,
    XXBYTE_GDSCRIPT_ID_V3_CONSTANT = 103,
    XXBYTE_GDSCRIPT_ID_V3_SELF = 104,
    XXBYTE_GDSCRIPT_ID_V3_BUILT_IN_TYPE = 105,
    XXBYTE_GDSCRIPT_ID_V3_BUILT_IN_FUNC = 106,
    XXBYTE_GDSCRIPT_ID_V3_OP_IN = 107,
    XXBYTE_GDSCRIPT_ID_V3_OP_EQUAL = 108,
    XXBYTE_GDSCRIPT_ID_V3_OP_NOT_EQUAL = 109,
    XXBYTE_GDSCRIPT_ID_V3_OP_LESS = 110,
    XXBYTE_GDSCRIPT_ID_V3_OP_LESS_EQUAL = 111,
    XXBYTE_GDSCRIPT_ID_V3_OP_GREATER = 112,
    XXBYTE_GDSCRIPT_ID_V3_OP_GREATER_EQUAL = 113,
    XXBYTE_GDSCRIPT_ID_V3_OP_AND = 114,
    XXBYTE_GDSCRIPT_ID_V3_OP_OR = 115,
    XXBYTE_GDSCRIPT_ID_V3_OP_NOT = 116,
    XXBYTE_GDSCRIPT_ID_V3_OP_ADD = 117,
    XXBYTE_GDSCRIPT_ID_V3_OP_SUB = 118,
    XXBYTE_GDSCRIPT_ID_V3_OP_MUL = 119,
    XXBYTE_GDSCRIPT_ID_V3_OP_DIV = 120,
    XXBYTE_GDSCRIPT_ID_V3_OP_MOD = 121,
    XXBYTE_GDSCRIPT_ID_V3_OP_SHIFT_LEFT = 122,
    XXBYTE_GDSCRIPT_ID_V3_OP_SHIFT_RIGHT = 123,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN = 124,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN_ADD = 125,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN_SUB = 126,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN_MUL = 127,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN_DIV = 128,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN_MOD = 129,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN_SHIFT_LEFT = 130,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN_SHIFT_RIGHT = 131,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN_BIT_AND = 132,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN_BIT_OR = 133,
    XXBYTE_GDSCRIPT_ID_V3_OP_ASSIGN_BIT_XOR = 134,
    XXBYTE_GDSCRIPT_ID_V3_OP_BIT_AND = 135,
    XXBYTE_GDSCRIPT_ID_V3_OP_BIT_OR = 136,
    XXBYTE_GDSCRIPT_ID_V3_OP_BIT_XOR = 137,
    XXBYTE_GDSCRIPT_ID_V3_OP_BIT_INVERT = 138,
    XXBYTE_GDSCRIPT_ID_V3_CF_IF = 139,
    XXBYTE_GDSCRIPT_ID_V3_CF_ELIF = 140,
    XXBYTE_GDSCRIPT_ID_V3_CF_ELSE = 141,
    XXBYTE_GDSCRIPT_ID_V3_CF_FOR = 142,
    XXBYTE_GDSCRIPT_ID_V3_CF_WHILE = 143,
    XXBYTE_GDSCRIPT_ID_V3_CF_BREAK = 144,
    XXBYTE_GDSCRIPT_ID_V3_CF_CONTINUE = 145,
    XXBYTE_GDSCRIPT_ID_V3_CF_PASS = 146,
    XXBYTE_GDSCRIPT_ID_V3_CF_RETURN = 147,
    XXBYTE_GDSCRIPT_ID_V3_CF_MATCH = 148,
    XXBYTE_GDSCRIPT_ID_V3_PR_FUNCTION = 149,
    XXBYTE_GDSCRIPT_ID_V3_PR_CLASS = 150,
    XXBYTE_GDSCRIPT_ID_V3_PR_CLASS_NAME = 151,
    XXBYTE_GDSCRIPT_ID_V3_PR_EXTENDS = 152,
    XXBYTE_GDSCRIPT_ID_V3_PR_IS = 153,
    XXBYTE_GDSCRIPT_ID_V3_PR_ONREADY = 154,
    XXBYTE_GDSCRIPT_ID_V3_PR_TOOL = 155,
    XXBYTE_GDSCRIPT_ID_V3_PR_STATIC = 156,
    XXBYTE_GDSCRIPT_ID_V3_PR_EXPORT = 157,
    XXBYTE_GDSCRIPT_ID_V3_PR_SETGET = 158,
    XXBYTE_GDSCRIPT_ID_V3_PR_CONST = 159,
    XXBYTE_GDSCRIPT_ID_V3_PR_VAR = 160,
    XXBYTE_GDSCRIPT_ID_V3_PR_AS = 161,
    XXBYTE_GDSCRIPT_ID_V3_PR_VOID = 162,
    XXBYTE_GDSCRIPT_ID_V3_PR_ENUM = 163,
    XXBYTE_GDSCRIPT_ID_V3_PR_PRELOAD = 164,
    XXBYTE_GDSCRIPT_ID_V3_PR_ASSERT = 165,
    XXBYTE_GDSCRIPT_ID_V3_PR_YIELD = 166,
    XXBYTE_GDSCRIPT_ID_V3_PR_SIGNAL = 167,
    XXBYTE_GDSCRIPT_ID_V3_PR_BREAKPOINT = 168,
    XXBYTE_GDSCRIPT_ID_V3_PR_REMOTE = 169,
    XXBYTE_GDSCRIPT_ID_V3_PR_SYNC = 170,
    XXBYTE_GDSCRIPT_ID_V3_PR_MASTER = 171,
    XXBYTE_GDSCRIPT_ID_V3_PR_SLAVE = 172,
    XXBYTE_GDSCRIPT_ID_V3_PR_PUPPET = 173,
    XXBYTE_GDSCRIPT_ID_V3_PR_REMOTESYNC = 174,
    XXBYTE_GDSCRIPT_ID_V3_PR_MASTERSYNC = 175,
    XXBYTE_GDSCRIPT_ID_V3_PR_PUPPETSYNC = 176,
    XXBYTE_GDSCRIPT_ID_V3_BRACKET_OPEN = 177,
    XXBYTE_GDSCRIPT_ID_V3_BRACKET_CLOSE = 178,
    XXBYTE_GDSCRIPT_ID_V3_CURLY_BRACKET_OPEN = 179,
    XXBYTE_GDSCRIPT_ID_V3_CURLY_BRACKET_CLOSE = 180,
    XXBYTE_GDSCRIPT_ID_V3_PARENTHESIS_OPEN = 181,
    XXBYTE_GDSCRIPT_ID_V3_PARENTHESIS_CLOSE = 182,
    XXBYTE_GDSCRIPT_ID_V3_COMMA = 183,
    XXBYTE_GDSCRIPT_ID_V3_SEMICOLON = 184,
    XXBYTE_GDSCRIPT_ID_V3_PERIOD = 185,
    XXBYTE_GDSCRIPT_ID_V3_QUESTION_MARK = 186,
    XXBYTE_GDSCRIPT_ID_V3_COLON = 187,
    XXBYTE_GDSCRIPT_ID_V3_DOLLAR = 188,
    XXBYTE_GDSCRIPT_ID_V3_FORWARD_ARROW = 189,
    XXBYTE_GDSCRIPT_ID_V3_NEWLINE = 190,
    XXBYTE_GDSCRIPT_ID_V3_CONST_PI = 191,
    XXBYTE_GDSCRIPT_ID_V3_CONST_TAU = 192,
    XXBYTE_GDSCRIPT_ID_V3_WILDCARD = 193,
    XXBYTE_GDSCRIPT_ID_V3_CONST_INF = 194,
    XXBYTE_GDSCRIPT_ID_V3_CONST_NAN = 195,
    XXBYTE_GDSCRIPT_ID_V3_ERROR = 196,
    XXBYTE_GDSCRIPT_ID_V3_EOF = 197,
    XXBYTE_GDSCRIPT_ID_V3_CURSOR = 198,
    XXBYTE_GDSCRIPT_ID_V3_CF_DO = 199,
    XXBYTE_GDSCRIPT_ID_V3_CF_SWITCH = 200,
    XXBYTE_GDSCRIPT_ID_V3_CF_CASE = 201,
    XXBYTE_GDSCRIPT_ID_COUNT = 202
} xxbyte_gdscript_id;

/*
 * .gdc files.
 *
 * 4.x: "GDSC", u32 version, u32 decompressed size (0: the rest is stored;
 * otherwise the rest is one Zstandard frame that inflates to that many
 * bytes), then the content: u32 identifier, constant, line and token counts
 * (V4_3 has an unused u32 between the line and the token count), the
 * identifiers (u32 length in characters, then one u32 UTF-32 code unit per
 * character, every byte XOR 0xB6), the constants (marshalled Variants,
 * core/io/marshalls.cpp), line_count (token index, line) pairs, line_count
 * (token index, column) pairs, and the tokens. The line pairs mark the
 * tokens that start a source line; the column (1-based; an indenting tab
 * counts 4 columns up to 4.6 and 1 in 4.7) gives their indentation.
 *
 * 3.x: "GDSC", u32 version, u32 identifier, constant, line and token
 * counts, the identifiers (u32 byte length, then that many UTF-8 bytes XOR
 * 0xB6, NUL-padded to a multiple of 4), the constants, line_count (token
 * index, line | column << 24) pairs, sorted by token index, of the tokens
 * that start a line (a token's line is that of the nearest pair at or
 * before it), and the tokens.
 *
 * Offsets are from data[0]. Counts and offsets are uint32_t: files of 4
 * GiB and more are not read.
 */
typedef struct xxbyte_gdscript_file {
    uint32_t version;           /* the header's version field */
    uint32_t mode;              /* the mode the tables were read with */
    uint32_t decompressed_size; /* 4.x: the header's size field (0 when the
                                   content is stored), else 0 */
    uint32_t identifier_count;
    uint32_t constant_count;
    uint32_t line_count; /* entries in each line table */
    uint32_t token_count;
    uint32_t identifier_offset; /* the first identifier */
    uint32_t constant_offset;   /* the first constant */
    uint32_t line_offset;       /* the (token, line) pairs */
    uint32_t column_offset;     /* 4.x: the (token, column) pairs, else 0 */
    uint32_t token_offset;      /* the first token */
    uint32_t token_size;        /* bytes of the token_count tokens */
    uint32_t trailing_size;     /* bytes after the last token (a 4.x engine
                                   rejects a file that has any) */
} xxbyte_gdscript_file;

/*
 * Parses the .gdc file in data[0..size): the header, then every table, with
 * the checks of the engine's GDScriptTokenizerBuffer::set_code_buffer.
 * mode is XXBYTE_MODE_AUTO (version 101 selects V4_5, 100 V4_3, 13 V3_5
 * and 12 V3_0) or a mode, which selects the layout and the tables whatever
 * the version says. Returns XXBYTE_STATUS_UNSUPPORTED_MODE for a version
 * XXBYTE_MODE_AUTO does not know and for a compressed 4.x file (version
 * and decompressed_size are filled in; see xxbyte_gdscript_decompress),
 * XXBYTE_STATUS_TRUNCATED when a table or a token runs past size,
 * XXBYTE_STATUS_INVALID_INSTRUCTION for a bad magic or a constant the
 * engine would refuse (an unknown Variant type, an object, an old-format
 * NodePath, a length past 2^31, nesting past 1024 levels). Token types are
 * not checked here: the decoder reports them. Unlike the engine, the walk
 * does not reject strings that are not valid UTF-8. On failure the other
 * fields of *file are zero.
 */
XXBYTE_API xxbyte_status xxbyte_gdscript_parse(const uint8_t *data, size_t size, uint32_t mode, xxbyte_gdscript_file *file);

/*
 * Inflates a compressed 4.x .gdc (decompressed_size != 0) into the
 * equivalent stored file: out receives the 12-byte header with a zero size
 * field and then the decompressed content, 12 + decompressed_size bytes.
 * The Zstandard decoder reads any single frame without a dictionary
 * (RFC 8878) and checks the content checksum when there is one; the frame
 * must inflate to exactly decompressed_size bytes. A stored file is copied
 * unchanged. *out_size receives the size needed; with out_capacity too
 * small (out may then be NULL) the result is XXBYTE_STATUS_OUT_OF_MEMORY
 * and nothing is written. Returns XXBYTE_STATUS_INVALID_INSTRUCTION when
 * data is not a 4.x .gdc or the frame is corrupt and
 * XXBYTE_STATUS_TRUNCATED when it ends early.
 */
XXBYTE_API xxbyte_status xxbyte_gdscript_decompress(const uint8_t *data, size_t size, uint8_t *out, size_t out_capacity, size_t *out_size);

/* The tables of a .gdc. */
typedef enum xxbyte_gdscript_table {
    XXBYTE_GDSCRIPT_TABLE_IDENTIFIER = 0,
    XXBYTE_GDSCRIPT_TABLE_CONSTANT,
    XXBYTE_GDSCRIPT_TABLE_TOKEN
} xxbyte_gdscript_table;

/*
 * Byte offsets from data[0] of the entries of table in the file parsed
 * into *file from the same bytes. *count receives the number of entries and
 * the first min(capacity, count) offsets are stored; offsets may be NULL
 * when capacity is 0.
 */
XXBYTE_API xxbyte_status xxbyte_gdscript_table_offsets(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, xxbyte_gdscript_table table, uint32_t *offsets,
                                                       size_t capacity, size_t *count);

/*
 * Writes identifier index as UTF-8 into buffer (always NUL-terminated when
 * buffer_size is not 0, truncated at a character boundary if needed) and
 * its complete length to *length. Code points that are not valid Unicode
 * scalar values become U+FFFD, as in the engine. 3.x identifiers end at
 * their first NUL byte; their bytes are copied as they are.
 */
XXBYTE_API xxbyte_status xxbyte_gdscript_identifier(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, uint32_t index, char *buffer, size_t buffer_size,
                                                    size_t *length);

/* Variant types of the constant table (Variant::Type of the file's major
 * version: 4.x has 39, 3.x 27). */
#define XXBYTE_GDSCRIPT_V4_TYPE_NIL 0u
#define XXBYTE_GDSCRIPT_V4_TYPE_BOOL 1u
#define XXBYTE_GDSCRIPT_V4_TYPE_INT 2u
#define XXBYTE_GDSCRIPT_V4_TYPE_FLOAT 3u
#define XXBYTE_GDSCRIPT_V4_TYPE_STRING 4u
#define XXBYTE_GDSCRIPT_V4_TYPE_STRING_NAME 21u
#define XXBYTE_GDSCRIPT_V4_TYPE_NODE_PATH 22u
#define XXBYTE_GDSCRIPT_V3_TYPE_NIL 0u
#define XXBYTE_GDSCRIPT_V3_TYPE_BOOL 1u
#define XXBYTE_GDSCRIPT_V3_TYPE_INT 2u
#define XXBYTE_GDSCRIPT_V3_TYPE_REAL 3u
#define XXBYTE_GDSCRIPT_V3_TYPE_STRING 4u
#define XXBYTE_GDSCRIPT_V3_TYPE_NODE_PATH 15u

/*
 * Writes constant index as GDScript source text into buffer (NUL-terminated
 * and truncated like xxbyte_gdscript_identifier) and its complete length to
 * *length; *type, when not NULL, receives its Variant type. The tokenizer
 * stores null, true, false, int (42, -7), float (1.5, 0.1, 1e+20,
 * 100.0; the shortest text that reads back as the same double), String
 * ("a\n\"b\"", escaped as GDScript parses it), StringName (&"name") and
 * NodePath (^"A/B:c", 3.x @"A/B:c"). Other Variant types, which the
 * tokenizer never writes, are printed as constructor-like summaries
 * ("Vector2(1, 2)", "Array(3)", "PackedByteArray(16)").
 */
XXBYTE_API xxbyte_status xxbyte_gdscript_constant(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, uint32_t index, char *buffer, size_t buffer_size,
                                                  size_t *length, uint32_t *type);

/*
 * The source line and, in 4.x, the indentation column (1-based, 0 when the
 * token does not start a line; see xxbyte_gdscript_source for tabs) of
 * token index. For 4.x the line is the one
 * stored in the token; *starts_line is nonzero when the line table lists
 * the token. For 3.x the line comes from the line map (0 before the first
 * entry). Any pointer may be NULL.
 */
XXBYTE_API xxbyte_status xxbyte_gdscript_token_line(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, uint32_t index, uint32_t *line, uint32_t *column,
                                                    int *starts_line);

/*
 * A resolver (xxbyte_resolver) for tokens decoded from a parsed file, with
 * context a xxbyte_gdscript_names: identifier operands become the
 * identifier and constant operands the constant source text. Other
 * operands keep their default text. identifier_offsets and
 * constant_offsets (from xxbyte_gdscript_table_offsets) are optional and
 * make each lookup O(1); without them every lookup walks the table.
 */
typedef struct xxbyte_gdscript_names {
    const uint8_t *data;
    size_t size;
    const xxbyte_gdscript_file *file;
    const uint32_t *identifier_offsets; /* identifier_count entries or NULL */
    const uint32_t *constant_offsets;   /* constant_count entries or NULL */
} xxbyte_gdscript_names;

XXBYTE_API size_t xxbyte_gdscript_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/*
 * Rebuilds GDScript source from the tokens of the file parsed into *file
 * and appends it to out (an xx_buf_t *, see xxbyte_xxfc.h; passed as void *
 * so that this header does not need xxfclib). Every token is put on its
 * recorded line (3.x: after the newline tokens), line-starting tokens get
 * their indentation, and a token on a later line that does not start one
 * is put there after "\" continuations. Indentation: a 3.x newline's
 * indent is that many tabs. A 4.x column counts an indenting tab as 4
 * columns in Godot 4.3 to 4.6 and as one in 4.7, so 4.3 and 4.4 files, and
 * 4.5+ files whose columns are all 1 modulo 4, get a tab per 4 columns
 * (and spaces for a remainder), other 4.5+ files a tab per column. With
 * numbered nonzero each line is prefixed with its number ("%4u  "; an
 * empty line is its number alone). The result tokenizes back, in the
 * engine, to the same identifiers, constants, tokens, lines and
 * indentation (comments and spacing are not stored in a .gdc; an error
 * token is written as "<error "message">", which does not tokenize back).
 * Line gaps of more than 65536 lines, and gaps past a budget of 4M lines
 * in all, become one line break with a jump in the numbering. Returns
 * XXBYTE_STATUS_OUT_OF_MEMORY when out fails.
 */
XXBYTE_API xxbyte_status xxbyte_gdscript_source(const uint8_t *data, size_t size, const xxbyte_gdscript_file *file, int numbered, void *out);

/*
 * xxbyte_disasm_file reads a .gdc file of any mode above (compressed 4.x
 * files included). It prints a header line ("; GDScript 4.5+ tokens
 * (tokenizer version 101, zstd 909 bytes): 27 identifiers, 21 constants,
 * 21 lines, 166 tokens"; a note follows when bytes trail the tokens), the
 * identifier and constant tables as "; ident[0] = @tool" and
 * "; const[0] = 1.5" comment lines, a "; tokens" line and a listing of the
 * tokens with names resolved (addresses are offsets in the file, or in the
 * decompressed image of a compressed one), then "; source" and the
 * reconstructed source of xxbyte_gdscript_source with numbered lines.
 * XXBYTE_MODE_AUTO takes the mode from the version field. Encrypted scripts
 * (3.x .gde) and PCK
 * archives are not read: extract the .gdc first.
 */

#ifdef __cplusplus
}
#endif

#endif
