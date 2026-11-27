/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_gdscript_internal.h"

#if XXBYTE_USE_GDSCRIPT

/*
 * Token tables generated from the official Godot sources: the Token::Type
 * enums and token_names of modules/gdscript/gdscript_tokenizer.{h,cpp} at
 * 4.3-stable and 4.7.2-stable, the Token enums, token_names and
 * _keyword_list of 3.0-stable and 3.6.3-stable, and the GDScriptFunctions
 * _names tables of 3.0, 3.1, 3.1.1, 3.2 and 3.5-stable (unchanged through
 * the other releases of each mode). Names and numbers are facts.
 */

/* Indexed by id: mnemonic and source text (NULL for tokens that carry
 * their text in a table or have none). */
const xxbyte_gdscript_token_info xxbyte_gdscript_tokens[XXBYTE_GDSCRIPT_ID_COUNT] = {
    {NULL, NULL},
    {"empty", NULL},
    {"annotation", NULL},
    {"identifier", NULL},
    {"literal", NULL},
    {"less", "<"},
    {"less_equal", "<="},
    {"greater", ">"},
    {"greater_equal", ">="},
    {"equal_equal", "=="},
    {"bang_equal", "!="},
    {"and", "and"},
    {"or", "or"},
    {"not", "not"},
    {"ampersand_ampersand", "&&"},
    {"pipe_pipe", "||"},
    {"bang", "!"},
    {"ampersand", "&"},
    {"pipe", "|"},
    {"tilde", "~"},
    {"caret", "^"},
    {"less_less", "<<"},
    {"greater_greater", ">>"},
    {"plus", "+"},
    {"minus", "-"},
    {"star", "*"},
    {"star_star", "**"},
    {"slash", "/"},
    {"percent", "%"},
    {"equal", "="},
    {"plus_equal", "+="},
    {"minus_equal", "-="},
    {"star_equal", "*="},
    {"star_star_equal", "**="},
    {"slash_equal", "/="},
    {"percent_equal", "%="},
    {"less_less_equal", "<<="},
    {"greater_greater_equal", ">>="},
    {"ampersand_equal", "&="},
    {"pipe_equal", "|="},
    {"caret_equal", "^="},
    {"if", "if"},
    {"elif", "elif"},
    {"else", "else"},
    {"for", "for"},
    {"while", "while"},
    {"break", "break"},
    {"continue", "continue"},
    {"pass", "pass"},
    {"return", "return"},
    {"match", "match"},
    {"when", "when"},
    {"as", "as"},
    {"assert", "assert"},
    {"await", "await"},
    {"breakpoint", "breakpoint"},
    {"class", "class"},
    {"class_name", "class_name"},
    {"const", "const"},
    {"enum", "enum"},
    {"extends", "extends"},
    {"func", "func"},
    {"in", "in"},
    {"is", "is"},
    {"namespace", "namespace"},
    {"preload", "preload"},
    {"self", "self"},
    {"signal", "signal"},
    {"static", "static"},
    {"super", "super"},
    {"trait", "trait"},
    {"var", "var"},
    {"void", "void"},
    {"yield", "yield"},
    {"bracket_open", "["},
    {"bracket_close", "]"},
    {"brace_open", "{"},
    {"brace_close", "}"},
    {"parenthesis_open", "("},
    {"parenthesis_close", ")"},
    {"comma", ","},
    {"semicolon", ";"},
    {"period", "."},
    {"period_period", ".."},
    {"period_period_period", "..."},
    {"colon", ":"},
    {"dollar", "$"},
    {"forward_arrow", "->"},
    {"underscore", "_"},
    {"newline", NULL},
    {"indent", NULL},
    {"dedent", NULL},
    {"const_pi", "PI"},
    {"const_tau", "TAU"},
    {"const_inf", "INF"},
    {"const_nan", "NAN"},
    {"vcs_conflict_marker", NULL},
    {"backtick", "`"},
    {"question_mark", "?"},
    {"error", NULL},
    {"eof", NULL},
    {"empty", NULL},
    {"identifier", NULL},
    {"constant", NULL},
    {"self", "self"},
    {"built_in_type", NULL},
    {"built_in_func", NULL},
    {"op_in", "in"},
    {"op_equal", "=="},
    {"op_not_equal", "!="},
    {"op_less", "<"},
    {"op_less_equal", "<="},
    {"op_greater", ">"},
    {"op_greater_equal", ">="},
    {"op_and", "and"},
    {"op_or", "or"},
    {"op_not", "not"},
    {"op_add", "+"},
    {"op_sub", "-"},
    {"op_mul", "*"},
    {"op_div", "/"},
    {"op_mod", "%"},
    {"op_shift_left", "<<"},
    {"op_shift_right", ">>"},
    {"op_assign", "="},
    {"op_assign_add", "+="},
    {"op_assign_sub", "-="},
    {"op_assign_mul", "*="},
    {"op_assign_div", "/="},
    {"op_assign_mod", "%="},
    {"op_assign_shift_left", "<<="},
    {"op_assign_shift_right", ">>="},
    {"op_assign_bit_and", "&="},
    {"op_assign_bit_or", "|="},
    {"op_assign_bit_xor", "^="},
    {"op_bit_and", "&"},
    {"op_bit_or", "|"},
    {"op_bit_xor", "^"},
    {"op_bit_invert", "~"},
    {"cf_if", "if"},
    {"cf_elif", "elif"},
    {"cf_else", "else"},
    {"cf_for", "for"},
    {"cf_while", "while"},
    {"cf_break", "break"},
    {"cf_continue", "continue"},
    {"cf_pass", "pass"},
    {"cf_return", "return"},
    {"cf_match", "match"},
    {"pr_function", "func"},
    {"pr_class", "class"},
    {"pr_class_name", "class_name"},
    {"pr_extends", "extends"},
    {"pr_is", "is"},
    {"pr_onready", "onready"},
    {"pr_tool", "tool"},
    {"pr_static", "static"},
    {"pr_export", "export"},
    {"pr_setget", "setget"},
    {"pr_const", "const"},
    {"pr_var", "var"},
    {"pr_as", "as"},
    {"pr_void", "void"},
    {"pr_enum", "enum"},
    {"pr_preload", "preload"},
    {"pr_assert", "assert"},
    {"pr_yield", "yield"},
    {"pr_signal", "signal"},
    {"pr_breakpoint", "breakpoint"},
    {"pr_remote", "remote"},
    {"pr_sync", "sync"},
    {"pr_master", "master"},
    {"pr_slave", "slave"},
    {"pr_puppet", "puppet"},
    {"pr_remotesync", "remotesync"},
    {"pr_mastersync", "mastersync"},
    {"pr_puppetsync", "puppetsync"},
    {"bracket_open", "["},
    {"bracket_close", "]"},
    {"curly_bracket_open", "{"},
    {"curly_bracket_close", "}"},
    {"parenthesis_open", "("},
    {"parenthesis_close", ")"},
    {"comma", ","},
    {"semicolon", ";"},
    {"period", "."},
    {"question_mark", "?"},
    {"colon", ":"},
    {"dollar", "$"},
    {"forward_arrow", "->"},
    {"newline", NULL},
    {"const_pi", "PI"},
    {"const_tau", "TAU"},
    {"wildcard", "_"},
    {"const_inf", "INF"},
    {"const_nan", "NAN"},
    {"error", NULL},
    {"eof", NULL},
    {"cursor", NULL},
    {"cf_do", "do"},
    {"cf_switch", "switch"},
    {"cf_case", "case"},
};

/* TOKENIZER_VERSION 100 (4.3, 4.4) type -> id. */
static const uint8_t ids_v100[99] = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
    34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66,
    67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
};

/* 3.0 type -> id. */
static const uint8_t ids_v30[93] = {
    101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131,
    132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 199, 143, 200, 201, 144, 145, 146, 147, 148, 149, 150, 152, 153, 154, 155, 156, 157, 158, 159, 160,
    163, 164, 165, 166, 167, 168, 169, 170, 171, 172, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 190, 191, 192, 193, 194, 195, 196, 197, 198,
};

/* 3.1 to 3.6 type -> id. */
static const uint8_t ids_v31[98] = {
    101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
    134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166,
    167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 193, 194, 195, 196, 197, 198,
};

/* GDScriptFunctions names, Godot 3.0. */
static const char *const builtins_v30[77] = {
    "sin",
    "cos",
    "tan",
    "sinh",
    "cosh",
    "tanh",
    "asin",
    "acos",
    "atan",
    "atan2",
    "sqrt",
    "fmod",
    "fposmod",
    "floor",
    "ceil",
    "round",
    "abs",
    "sign",
    "pow",
    "log",
    "exp",
    "is_nan",
    "is_inf",
    "ease",
    "decimals",
    "stepify",
    "lerp",
    "inverse_lerp",
    "range_lerp",
    "dectime",
    "randomize",
    "randi",
    "randf",
    "rand_range",
    "seed",
    "rand_seed",
    "deg2rad",
    "rad2deg",
    "linear2db",
    "db2linear",
    "polar2cartesian",
    "cartesian2polar",
    "wrapi",
    "wrapf",
    "max",
    "min",
    "clamp",
    "nearest_po2",
    "weakref",
    "funcref",
    "convert",
    "typeof",
    "type_exists",
    "char",
    "str",
    "print",
    "printt",
    "prints",
    "printerr",
    "printraw",
    "var2str",
    "str2var",
    "var2bytes",
    "bytes2var",
    "range",
    "load",
    "inst2dict",
    "dict2inst",
    "validate_json",
    "parse_json",
    "to_json",
    "hash",
    "Color8",
    "ColorN",
    "print_stack",
    "instance_from_id",
    "len",
};

/* GDScriptFunctions names, Godot 3.1. */
static const char *const builtins_v31[82] = {
    "sin",
    "cos",
    "tan",
    "sinh",
    "cosh",
    "tanh",
    "asin",
    "acos",
    "atan",
    "atan2",
    "sqrt",
    "fmod",
    "fposmod",
    "floor",
    "ceil",
    "round",
    "abs",
    "sign",
    "pow",
    "log",
    "exp",
    "is_nan",
    "is_inf",
    "ease",
    "decimals",
    "stepify",
    "lerp",
    "inverse_lerp",
    "range_lerp",
    "dectime",
    "randomize",
    "randi",
    "randf",
    "rand_range",
    "seed",
    "rand_seed",
    "deg2rad",
    "rad2deg",
    "linear2db",
    "db2linear",
    "polar2cartesian",
    "cartesian2polar",
    "wrapi",
    "wrapf",
    "max",
    "min",
    "clamp",
    "nearest_po2",
    "weakref",
    "funcref",
    "convert",
    "typeof",
    "type_exists",
    "char",
    "str",
    "print",
    "printt",
    "prints",
    "printerr",
    "printraw",
    "print_debug",
    "push_error",
    "push_warning",
    "var2str",
    "str2var",
    "var2bytes",
    "bytes2var",
    "range",
    "load",
    "inst2dict",
    "dict2inst",
    "validate_json",
    "parse_json",
    "to_json",
    "hash",
    "Color8",
    "ColorN",
    "print_stack",
    "get_stack",
    "instance_from_id",
    "len",
    "is_instance_valid",
};

/* GDScriptFunctions names, Godot 3.1.1. */
static const char *const builtins_v311[83] = {
    "sin",
    "cos",
    "tan",
    "sinh",
    "cosh",
    "tanh",
    "asin",
    "acos",
    "atan",
    "atan2",
    "sqrt",
    "fmod",
    "fposmod",
    "floor",
    "ceil",
    "round",
    "abs",
    "sign",
    "pow",
    "log",
    "exp",
    "is_nan",
    "is_inf",
    "ease",
    "decimals",
    "stepify",
    "lerp",
    "inverse_lerp",
    "range_lerp",
    "smoothstep",
    "dectime",
    "randomize",
    "randi",
    "randf",
    "rand_range",
    "seed",
    "rand_seed",
    "deg2rad",
    "rad2deg",
    "linear2db",
    "db2linear",
    "polar2cartesian",
    "cartesian2polar",
    "wrapi",
    "wrapf",
    "max",
    "min",
    "clamp",
    "nearest_po2",
    "weakref",
    "funcref",
    "convert",
    "typeof",
    "type_exists",
    "char",
    "str",
    "print",
    "printt",
    "prints",
    "printerr",
    "printraw",
    "print_debug",
    "push_error",
    "push_warning",
    "var2str",
    "str2var",
    "var2bytes",
    "bytes2var",
    "range",
    "load",
    "inst2dict",
    "dict2inst",
    "validate_json",
    "parse_json",
    "to_json",
    "hash",
    "Color8",
    "ColorN",
    "print_stack",
    "get_stack",
    "instance_from_id",
    "len",
    "is_instance_valid",
};

/* GDScriptFunctions names, Godot 3.2. */
static const char *const builtins_v32[90] = {
    "sin",
    "cos",
    "tan",
    "sinh",
    "cosh",
    "tanh",
    "asin",
    "acos",
    "atan",
    "atan2",
    "sqrt",
    "fmod",
    "fposmod",
    "posmod",
    "floor",
    "ceil",
    "round",
    "abs",
    "sign",
    "pow",
    "log",
    "exp",
    "is_nan",
    "is_inf",
    "is_equal_approx",
    "is_zero_approx",
    "ease",
    "decimals",
    "step_decimals",
    "stepify",
    "lerp",
    "lerp_angle",
    "inverse_lerp",
    "range_lerp",
    "smoothstep",
    "move_toward",
    "dectime",
    "randomize",
    "randi",
    "randf",
    "rand_range",
    "seed",
    "rand_seed",
    "deg2rad",
    "rad2deg",
    "linear2db",
    "db2linear",
    "polar2cartesian",
    "cartesian2polar",
    "wrapi",
    "wrapf",
    "max",
    "min",
    "clamp",
    "nearest_po2",
    "weakref",
    "funcref",
    "convert",
    "typeof",
    "type_exists",
    "char",
    "ord",
    "str",
    "print",
    "printt",
    "prints",
    "printerr",
    "printraw",
    "print_debug",
    "push_error",
    "push_warning",
    "var2str",
    "str2var",
    "var2bytes",
    "bytes2var",
    "range",
    "load",
    "inst2dict",
    "dict2inst",
    "validate_json",
    "parse_json",
    "to_json",
    "hash",
    "Color8",
    "ColorN",
    "print_stack",
    "get_stack",
    "instance_from_id",
    "len",
    "is_instance_valid",
};

/* GDScriptFunctions names, Godot 3.5. */
static const char *const builtins_v35[91] = {
    "sin",
    "cos",
    "tan",
    "sinh",
    "cosh",
    "tanh",
    "asin",
    "acos",
    "atan",
    "atan2",
    "sqrt",
    "fmod",
    "fposmod",
    "posmod",
    "floor",
    "ceil",
    "round",
    "abs",
    "sign",
    "pow",
    "log",
    "exp",
    "is_nan",
    "is_inf",
    "is_equal_approx",
    "is_zero_approx",
    "ease",
    "decimals",
    "step_decimals",
    "stepify",
    "lerp",
    "lerp_angle",
    "inverse_lerp",
    "range_lerp",
    "smoothstep",
    "move_toward",
    "dectime",
    "randomize",
    "randi",
    "randf",
    "rand_range",
    "seed",
    "rand_seed",
    "deg2rad",
    "rad2deg",
    "linear2db",
    "db2linear",
    "polar2cartesian",
    "cartesian2polar",
    "wrapi",
    "wrapf",
    "max",
    "min",
    "clamp",
    "nearest_po2",
    "weakref",
    "funcref",
    "convert",
    "typeof",
    "type_exists",
    "char",
    "ord",
    "str",
    "print",
    "printt",
    "prints",
    "printerr",
    "printraw",
    "print_debug",
    "push_error",
    "push_warning",
    "var2str",
    "str2var",
    "var2bytes",
    "bytes2var",
    "range",
    "load",
    "inst2dict",
    "dict2inst",
    "validate_json",
    "parse_json",
    "to_json",
    "hash",
    "Color8",
    "ColorN",
    "print_stack",
    "get_stack",
    "instance_from_id",
    "len",
    "is_instance_valid",
    "deep_equal",
};

/* 3.x source names of Variant::Type (gdscript_tokenizer.cpp _type_list;
 * NIL has no keyword). */
static const char *const v3_types[27] = {
    "Nil",
    "bool",
    "int",
    "float",
    "String",
    "Vector2",
    "Rect2",
    "Vector3",
    "Transform2D",
    "Plane",
    "Quat",
    "AABB",
    "Basis",
    "Transform",
    "Color",
    "NodePath",
    "RID",
    "Object",
    "Dictionary",
    "Array",
    "PoolByteArray",
    "PoolIntArray",
    "PoolRealArray",
    "PoolStringArray",
    "PoolVector2Array",
    "PoolVector3Array",
    "PoolColorArray",
};

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define V4_FIRST_V3_ID ((uint32_t)XXBYTE_GDSCRIPT_ID_V3_EMPTY)

uint32_t xxbyte_gdscript_token_id(uint32_t mode, uint32_t type)
{
    switch (mode) {
        case XXBYTE_GDSCRIPT_MODE_DEFAULT:
        case XXBYTE_GDSCRIPT_MODE_V4_5: return type < V4_FIRST_V3_ID - 1u ? type + 1u : 0u;
        case XXBYTE_GDSCRIPT_MODE_V4_3: return type < ARRAY_COUNT(ids_v100) ? ids_v100[type] : 0u;
        case XXBYTE_GDSCRIPT_MODE_V3_0: return type < ARRAY_COUNT(ids_v30) ? ids_v30[type] : 0u;
        case XXBYTE_GDSCRIPT_MODE_V3_1:
        case XXBYTE_GDSCRIPT_MODE_V3_1_1:
        case XXBYTE_GDSCRIPT_MODE_V3_2:
        case XXBYTE_GDSCRIPT_MODE_V3_5: return type < ARRAY_COUNT(ids_v31) ? ids_v31[type] : 0u;
        default: return 0u;
    }
}

const char *xxbyte_gdscript_builtin_name(uint32_t mode, uint32_t index)
{
    const char *const *table;
    size_t count;

    switch (mode) {
        case XXBYTE_GDSCRIPT_MODE_V3_0:
            table = builtins_v30;
            count = ARRAY_COUNT(builtins_v30);
            break;
        case XXBYTE_GDSCRIPT_MODE_V3_1:
            table = builtins_v31;
            count = ARRAY_COUNT(builtins_v31);
            break;
        case XXBYTE_GDSCRIPT_MODE_V3_1_1:
            table = builtins_v311;
            count = ARRAY_COUNT(builtins_v311);
            break;
        case XXBYTE_GDSCRIPT_MODE_V3_2:
            table = builtins_v32;
            count = ARRAY_COUNT(builtins_v32);
            break;
        case XXBYTE_GDSCRIPT_MODE_V3_5:
            table = builtins_v35;
            count = ARRAY_COUNT(builtins_v35);
            break;
        default: return NULL;
    }
    return index < count ? table[index] : NULL;
}

const char *xxbyte_gdscript_v3_type_name(uint32_t index)
{
    return index < ARRAY_COUNT(v3_types) ? v3_types[index] : NULL;
}

static xxbyte_status gdscript_check_mode(uint32_t mode)
{
    return mode <= XXBYTE_GDSCRIPT_MODE_V3_0 ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static xxbyte_status decode_v4(uint32_t mode, const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    uint32_t size = (code[0] & 0x80u) != 0 ? 8u : 5u;
    uint32_t word;
    uint32_t type;
    uint32_t id;
    xxbyte_operand *operand;

    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    /* The engine reads the first u32 in both forms; in the 5-byte form its
     * high bytes are the low bytes of the line. */
    word = xxbyte_u32le(code);
    type = word & 0x7Fu;
    id = xxbyte_gdscript_token_id(mode, type);
    if (id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->size = size;
    instruction->opcode = type;
    instruction->id = id;
    instruction->group = XXBYTE_GROUP_NONE;
    instruction->mnemonic = xxbyte_gdscript_tokens[id].mnemonic;
    switch (id) {
        case XXBYTE_GDSCRIPT_ID_IDENTIFIER:
        case XXBYTE_GDSCRIPT_ID_ANNOTATION: xxbyte_add_index(instruction, XXBYTE_INDEX_NAME, 3, (int64_t)(word >> 8)); break;
        case XXBYTE_GDSCRIPT_ID_LITERAL:
        case XXBYTE_GDSCRIPT_ID_ERROR: xxbyte_add_index(instruction, XXBYTE_INDEX_CONSTANT, 3, (int64_t)(word >> 8)); break;
        default: break;
    }
    operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, (int64_t)xxbyte_u32le(code + size - 4));
    operand->flags = XXBYTE_GDSCRIPT_OPERAND_FLAG_LINE;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status decode_v3(uint32_t mode, const uint8_t *code, size_t code_size, xxbyte_instruction *instruction)
{
    uint32_t size = (code[0] & 0x80u) != 0 ? 4u : 1u;
    uint32_t word;
    uint32_t type;
    uint32_t id;
    uint32_t value;
    uint32_t value_size = size == 4u ? 3u : 0u;
    xxbyte_operand *operand;

    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    word = size == 4u ? xxbyte_u32le(code) & ~UINT32_C(0x80) : code[0];
    type = word & 0xFFu;
    value = word >> 8;
    id = xxbyte_gdscript_token_id(mode, type);
    if (id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->size = size;
    instruction->opcode = type;
    instruction->id = id;
    instruction->group = XXBYTE_GROUP_NONE;
    instruction->mnemonic = xxbyte_gdscript_tokens[id].mnemonic;
    switch (id) {
        case XXBYTE_GDSCRIPT_ID_V3_IDENTIFIER: xxbyte_add_index(instruction, XXBYTE_INDEX_NAME, value_size, (int64_t)value); break;
        case XXBYTE_GDSCRIPT_ID_V3_CONSTANT: xxbyte_add_index(instruction, XXBYTE_INDEX_CONSTANT, value_size, (int64_t)value); break;
        case XXBYTE_GDSCRIPT_ID_V3_BUILT_IN_TYPE:
            if (xxbyte_gdscript_v3_type_name(value) == NULL) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            xxbyte_add_index(instruction, XXBYTE_GDSCRIPT_INDEX_TYPE, value_size, (int64_t)value);
            break;
        case XXBYTE_GDSCRIPT_ID_V3_BUILT_IN_FUNC:
            if (xxbyte_gdscript_builtin_name(mode, value) == NULL) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            xxbyte_add_index(instruction, XXBYTE_GDSCRIPT_INDEX_BUILTIN, value_size, (int64_t)value);
            break;
        case XXBYTE_GDSCRIPT_ID_V3_NEWLINE:
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, value_size, (int64_t)value);
            operand->flags = XXBYTE_GDSCRIPT_OPERAND_FLAG_INDENT;
            break;
        default: break;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status gdscript_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    (void)context;
    (void)address;
    if (XXBYTE_GDSCRIPT_MODE_IS_V4(mode)) {
        return decode_v4(mode, code, code_size, instruction);
    }
    return decode_v3(mode, code, code_size, instruction);
}

static int gdscript_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;

    for (i = 0; i < instruction->operand_count; ++i) {
        const xxbyte_operand *operand = &instruction->operands[i];
        int resolved;

        if (!xxbyte_fmt_str(fmt, i == 0 ? " " : ", ")) {
            return 0;
        }
        if (operand->type == XXBYTE_OPERAND_IMMEDIATE) {
            if (!xxbyte_fmt_printf(fmt, (operand->flags & XXBYTE_GDSCRIPT_OPERAND_FLAG_LINE) != 0 ? "line %llu" : "indent %llu", (unsigned long long)operand->value)) {
                return 0;
            }
            continue;
        }
        resolved = xxbyte_fmt_resolve(fmt, instruction, i);
        if (resolved < 0) {
            return 0;
        }
        if (resolved > 0) {
            continue;
        }
        switch (operand->kind) {
            case XXBYTE_INDEX_NAME:
                if (!xxbyte_fmt_printf(fmt, "ident[%llu]", (unsigned long long)operand->value)) {
                    return 0;
                }
                break;
            case XXBYTE_INDEX_CONSTANT:
                if (!xxbyte_fmt_printf(fmt, "const[%llu]", (unsigned long long)operand->value)) {
                    return 0;
                }
                break;
            case XXBYTE_GDSCRIPT_INDEX_TYPE:
                if (!xxbyte_fmt_str(fmt, xxbyte_gdscript_v3_type_name((uint32_t)operand->value))) {
                    return 0;
                }
                break;
            default: {
                const char *name = xxbyte_gdscript_builtin_name(instruction->mode, (uint32_t)operand->value);

                if (!xxbyte_fmt_str(fmt, name != NULL ? name : "?")) {
                    return 0;
                }
                break;
            }
        }
    }
    return 1;
}

const xxbyte_family_ops xxbyte_gdscript_ops = {
    XXBYTE_FAMILY_GDSCRIPT, XXBYTE_GDSCRIPT_MODE_DEFAULT, gdscript_check_mode, gdscript_decode, NULL, gdscript_format_operands, NULL, xxbyte_gdscript_list_file,
};

#else
typedef int xxbyte_gdscript_disabled;
#endif
