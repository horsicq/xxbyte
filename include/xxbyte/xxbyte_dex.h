/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_DEX_H
#define XXBYTE_XXBYTE_DEX_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_DEX
#error "the Dalvik decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Dalvik decode mode: the DEX format version (35, 37, 38, 39, 40 or 41) in
 * the low 16 bits, 0 for the newest. Opcodes introduced by later versions
 * are invalid in earlier ones: invoke-polymorphic(/range) and
 * invoke-custom(/range) need 38, const-method-handle and const-method-type
 * need 39.
 *
 * XXBYTE_DEX_MODE_ODEX also accepts the optimized opcodes of the Dalvik VM
 * (dexopt, Android 4.x) in the otherwise unused opcode space 0xe3..0xfe.
 * Where they collide with the opcodes of version 38 and later (0xfa..0xfe),
 * the odex meaning applies only to versions below 38, so real odex code
 * should be decoded with XXBYTE_DEX_MODE_ODEX | XXBYTE_DEX_MODE(35).
 *
 * Addresses are byte offsets (code-unit offset * 2); branch and payload
 * targets are absolute byte addresses.
 */
#define XXBYTE_DEX_MODE_VERSION_MASK 0xFFFFu
#define XXBYTE_DEX_MODE_ODEX 0x10000u
#define XXBYTE_DEX_MODE(version) ((uint32_t)(version) & XXBYTE_DEX_MODE_VERSION_MASK)
#define XXBYTE_DEX_MODE_DEFAULT 0u

/* The oldest and newest supported DEX format versions. */
#define XXBYTE_DEX_VERSION_OLDEST 35u
#define XXBYTE_DEX_VERSION_NEWEST 41u

/* Payload pseudo-instruction idents (the first code unit). */
#define XXBYTE_DEX_PACKED_SWITCH_PAYLOAD 0x0100u
#define XXBYTE_DEX_SPARSE_SWITCH_PAYLOAD 0x0200u
#define XXBYTE_DEX_FILL_ARRAY_DATA_PAYLOAD 0x0300u

/*
 * Operands and text (smali syntax with numeric references). Registers are
 * REGISTER operands printed as vN, with XXBYTE_OPERAND_FLAG_WIDE on register
 * pairs. Format 35c-style lists are REGISTER operands flagged
 * XXBYTE_OPERAND_FLAG_LIST and print as {v0, v1}; ranges are one
 * REGISTER_RANGE printed as {v0 .. v5}. Literals are signed IMMEDIATE
 * operands printed as 0x1 or -0x1; the /high16 forms carry the shifted value
 * and XXBYTE_OPERAND_FLAG_HIGH16. References are INDEX operands printed as
 * kind@hhhh: string, type, field, method, proto, call_site, method_handle,
 * and for odex code inline, vtable and field_offset. Branches are BRANCH and
 * payload references DATA operands, printed as 0x%04llx.
 *
 * A payload pseudo-instruction decodes as one instruction of group
 * XXBYTE_GROUP_PAYLOAD with opcode 0x0100, 0x0200 or 0x0300 and table_count
 * entries. packed-switch-payload has the first key as its operand,
 * fill-array-data-payload the element width and count. The switch payloads
 * are also XXBYTE_GROUP_SWITCH: xxbyte_switch_entry returns the key and the
 * byte offset relative to the referencing switch instruction. Only the
 * entries are read from the borrowed code buffer, so an empty payload
 * (table_count 0) formats from its operands alone.
 */

/*
 * Instruction identifiers (xxbyte_instruction.id): the mnemonic upper-cased
 * with every run of other characters replaced by '_'. The odex-only
 * opcodes follow the payload pseudo-instructions.
 */
typedef enum xxbyte_dex_id {
    XXBYTE_DEX_ID_INVALID = 0,
    XXBYTE_DEX_ID_NOP = 1,
    XXBYTE_DEX_ID_MOVE = 2,
    XXBYTE_DEX_ID_MOVE_FROM16 = 3,
    XXBYTE_DEX_ID_MOVE_16 = 4,
    XXBYTE_DEX_ID_MOVE_WIDE = 5,
    XXBYTE_DEX_ID_MOVE_WIDE_FROM16 = 6,
    XXBYTE_DEX_ID_MOVE_WIDE_16 = 7,
    XXBYTE_DEX_ID_MOVE_OBJECT = 8,
    XXBYTE_DEX_ID_MOVE_OBJECT_FROM16 = 9,
    XXBYTE_DEX_ID_MOVE_OBJECT_16 = 10,
    XXBYTE_DEX_ID_MOVE_RESULT = 11,
    XXBYTE_DEX_ID_MOVE_RESULT_WIDE = 12,
    XXBYTE_DEX_ID_MOVE_RESULT_OBJECT = 13,
    XXBYTE_DEX_ID_MOVE_EXCEPTION = 14,
    XXBYTE_DEX_ID_RETURN_VOID = 15,
    XXBYTE_DEX_ID_RETURN = 16,
    XXBYTE_DEX_ID_RETURN_WIDE = 17,
    XXBYTE_DEX_ID_RETURN_OBJECT = 18,
    XXBYTE_DEX_ID_CONST_4 = 19,
    XXBYTE_DEX_ID_CONST_16 = 20,
    XXBYTE_DEX_ID_CONST = 21,
    XXBYTE_DEX_ID_CONST_HIGH16 = 22,
    XXBYTE_DEX_ID_CONST_WIDE_16 = 23,
    XXBYTE_DEX_ID_CONST_WIDE_32 = 24,
    XXBYTE_DEX_ID_CONST_WIDE = 25,
    XXBYTE_DEX_ID_CONST_WIDE_HIGH16 = 26,
    XXBYTE_DEX_ID_CONST_STRING = 27,
    XXBYTE_DEX_ID_CONST_STRING_JUMBO = 28,
    XXBYTE_DEX_ID_CONST_CLASS = 29,
    XXBYTE_DEX_ID_MONITOR_ENTER = 30,
    XXBYTE_DEX_ID_MONITOR_EXIT = 31,
    XXBYTE_DEX_ID_CHECK_CAST = 32,
    XXBYTE_DEX_ID_INSTANCE_OF = 33,
    XXBYTE_DEX_ID_ARRAY_LENGTH = 34,
    XXBYTE_DEX_ID_NEW_INSTANCE = 35,
    XXBYTE_DEX_ID_NEW_ARRAY = 36,
    XXBYTE_DEX_ID_FILLED_NEW_ARRAY = 37,
    XXBYTE_DEX_ID_FILLED_NEW_ARRAY_RANGE = 38,
    XXBYTE_DEX_ID_FILL_ARRAY_DATA = 39,
    XXBYTE_DEX_ID_THROW = 40,
    XXBYTE_DEX_ID_GOTO = 41,
    XXBYTE_DEX_ID_GOTO_16 = 42,
    XXBYTE_DEX_ID_GOTO_32 = 43,
    XXBYTE_DEX_ID_PACKED_SWITCH = 44,
    XXBYTE_DEX_ID_SPARSE_SWITCH = 45,
    XXBYTE_DEX_ID_CMPL_FLOAT = 46,
    XXBYTE_DEX_ID_CMPG_FLOAT = 47,
    XXBYTE_DEX_ID_CMPL_DOUBLE = 48,
    XXBYTE_DEX_ID_CMPG_DOUBLE = 49,
    XXBYTE_DEX_ID_CMP_LONG = 50,
    XXBYTE_DEX_ID_IF_EQ = 51,
    XXBYTE_DEX_ID_IF_NE = 52,
    XXBYTE_DEX_ID_IF_LT = 53,
    XXBYTE_DEX_ID_IF_GE = 54,
    XXBYTE_DEX_ID_IF_GT = 55,
    XXBYTE_DEX_ID_IF_LE = 56,
    XXBYTE_DEX_ID_IF_EQZ = 57,
    XXBYTE_DEX_ID_IF_NEZ = 58,
    XXBYTE_DEX_ID_IF_LTZ = 59,
    XXBYTE_DEX_ID_IF_GEZ = 60,
    XXBYTE_DEX_ID_IF_GTZ = 61,
    XXBYTE_DEX_ID_IF_LEZ = 62,
    XXBYTE_DEX_ID_AGET = 63,
    XXBYTE_DEX_ID_AGET_WIDE = 64,
    XXBYTE_DEX_ID_AGET_OBJECT = 65,
    XXBYTE_DEX_ID_AGET_BOOLEAN = 66,
    XXBYTE_DEX_ID_AGET_BYTE = 67,
    XXBYTE_DEX_ID_AGET_CHAR = 68,
    XXBYTE_DEX_ID_AGET_SHORT = 69,
    XXBYTE_DEX_ID_APUT = 70,
    XXBYTE_DEX_ID_APUT_WIDE = 71,
    XXBYTE_DEX_ID_APUT_OBJECT = 72,
    XXBYTE_DEX_ID_APUT_BOOLEAN = 73,
    XXBYTE_DEX_ID_APUT_BYTE = 74,
    XXBYTE_DEX_ID_APUT_CHAR = 75,
    XXBYTE_DEX_ID_APUT_SHORT = 76,
    XXBYTE_DEX_ID_IGET = 77,
    XXBYTE_DEX_ID_IGET_WIDE = 78,
    XXBYTE_DEX_ID_IGET_OBJECT = 79,
    XXBYTE_DEX_ID_IGET_BOOLEAN = 80,
    XXBYTE_DEX_ID_IGET_BYTE = 81,
    XXBYTE_DEX_ID_IGET_CHAR = 82,
    XXBYTE_DEX_ID_IGET_SHORT = 83,
    XXBYTE_DEX_ID_IPUT = 84,
    XXBYTE_DEX_ID_IPUT_WIDE = 85,
    XXBYTE_DEX_ID_IPUT_OBJECT = 86,
    XXBYTE_DEX_ID_IPUT_BOOLEAN = 87,
    XXBYTE_DEX_ID_IPUT_BYTE = 88,
    XXBYTE_DEX_ID_IPUT_CHAR = 89,
    XXBYTE_DEX_ID_IPUT_SHORT = 90,
    XXBYTE_DEX_ID_SGET = 91,
    XXBYTE_DEX_ID_SGET_WIDE = 92,
    XXBYTE_DEX_ID_SGET_OBJECT = 93,
    XXBYTE_DEX_ID_SGET_BOOLEAN = 94,
    XXBYTE_DEX_ID_SGET_BYTE = 95,
    XXBYTE_DEX_ID_SGET_CHAR = 96,
    XXBYTE_DEX_ID_SGET_SHORT = 97,
    XXBYTE_DEX_ID_SPUT = 98,
    XXBYTE_DEX_ID_SPUT_WIDE = 99,
    XXBYTE_DEX_ID_SPUT_OBJECT = 100,
    XXBYTE_DEX_ID_SPUT_BOOLEAN = 101,
    XXBYTE_DEX_ID_SPUT_BYTE = 102,
    XXBYTE_DEX_ID_SPUT_CHAR = 103,
    XXBYTE_DEX_ID_SPUT_SHORT = 104,
    XXBYTE_DEX_ID_INVOKE_VIRTUAL = 105,
    XXBYTE_DEX_ID_INVOKE_SUPER = 106,
    XXBYTE_DEX_ID_INVOKE_DIRECT = 107,
    XXBYTE_DEX_ID_INVOKE_STATIC = 108,
    XXBYTE_DEX_ID_INVOKE_INTERFACE = 109,
    XXBYTE_DEX_ID_INVOKE_VIRTUAL_RANGE = 110,
    XXBYTE_DEX_ID_INVOKE_SUPER_RANGE = 111,
    XXBYTE_DEX_ID_INVOKE_DIRECT_RANGE = 112,
    XXBYTE_DEX_ID_INVOKE_STATIC_RANGE = 113,
    XXBYTE_DEX_ID_INVOKE_INTERFACE_RANGE = 114,
    XXBYTE_DEX_ID_NEG_INT = 115,
    XXBYTE_DEX_ID_NOT_INT = 116,
    XXBYTE_DEX_ID_NEG_LONG = 117,
    XXBYTE_DEX_ID_NOT_LONG = 118,
    XXBYTE_DEX_ID_NEG_FLOAT = 119,
    XXBYTE_DEX_ID_NEG_DOUBLE = 120,
    XXBYTE_DEX_ID_INT_TO_LONG = 121,
    XXBYTE_DEX_ID_INT_TO_FLOAT = 122,
    XXBYTE_DEX_ID_INT_TO_DOUBLE = 123,
    XXBYTE_DEX_ID_LONG_TO_INT = 124,
    XXBYTE_DEX_ID_LONG_TO_FLOAT = 125,
    XXBYTE_DEX_ID_LONG_TO_DOUBLE = 126,
    XXBYTE_DEX_ID_FLOAT_TO_INT = 127,
    XXBYTE_DEX_ID_FLOAT_TO_LONG = 128,
    XXBYTE_DEX_ID_FLOAT_TO_DOUBLE = 129,
    XXBYTE_DEX_ID_DOUBLE_TO_INT = 130,
    XXBYTE_DEX_ID_DOUBLE_TO_LONG = 131,
    XXBYTE_DEX_ID_DOUBLE_TO_FLOAT = 132,
    XXBYTE_DEX_ID_INT_TO_BYTE = 133,
    XXBYTE_DEX_ID_INT_TO_CHAR = 134,
    XXBYTE_DEX_ID_INT_TO_SHORT = 135,
    XXBYTE_DEX_ID_ADD_INT = 136,
    XXBYTE_DEX_ID_SUB_INT = 137,
    XXBYTE_DEX_ID_MUL_INT = 138,
    XXBYTE_DEX_ID_DIV_INT = 139,
    XXBYTE_DEX_ID_REM_INT = 140,
    XXBYTE_DEX_ID_AND_INT = 141,
    XXBYTE_DEX_ID_OR_INT = 142,
    XXBYTE_DEX_ID_XOR_INT = 143,
    XXBYTE_DEX_ID_SHL_INT = 144,
    XXBYTE_DEX_ID_SHR_INT = 145,
    XXBYTE_DEX_ID_USHR_INT = 146,
    XXBYTE_DEX_ID_ADD_LONG = 147,
    XXBYTE_DEX_ID_SUB_LONG = 148,
    XXBYTE_DEX_ID_MUL_LONG = 149,
    XXBYTE_DEX_ID_DIV_LONG = 150,
    XXBYTE_DEX_ID_REM_LONG = 151,
    XXBYTE_DEX_ID_AND_LONG = 152,
    XXBYTE_DEX_ID_OR_LONG = 153,
    XXBYTE_DEX_ID_XOR_LONG = 154,
    XXBYTE_DEX_ID_SHL_LONG = 155,
    XXBYTE_DEX_ID_SHR_LONG = 156,
    XXBYTE_DEX_ID_USHR_LONG = 157,
    XXBYTE_DEX_ID_ADD_FLOAT = 158,
    XXBYTE_DEX_ID_SUB_FLOAT = 159,
    XXBYTE_DEX_ID_MUL_FLOAT = 160,
    XXBYTE_DEX_ID_DIV_FLOAT = 161,
    XXBYTE_DEX_ID_REM_FLOAT = 162,
    XXBYTE_DEX_ID_ADD_DOUBLE = 163,
    XXBYTE_DEX_ID_SUB_DOUBLE = 164,
    XXBYTE_DEX_ID_MUL_DOUBLE = 165,
    XXBYTE_DEX_ID_DIV_DOUBLE = 166,
    XXBYTE_DEX_ID_REM_DOUBLE = 167,
    XXBYTE_DEX_ID_ADD_INT_2ADDR = 168,
    XXBYTE_DEX_ID_SUB_INT_2ADDR = 169,
    XXBYTE_DEX_ID_MUL_INT_2ADDR = 170,
    XXBYTE_DEX_ID_DIV_INT_2ADDR = 171,
    XXBYTE_DEX_ID_REM_INT_2ADDR = 172,
    XXBYTE_DEX_ID_AND_INT_2ADDR = 173,
    XXBYTE_DEX_ID_OR_INT_2ADDR = 174,
    XXBYTE_DEX_ID_XOR_INT_2ADDR = 175,
    XXBYTE_DEX_ID_SHL_INT_2ADDR = 176,
    XXBYTE_DEX_ID_SHR_INT_2ADDR = 177,
    XXBYTE_DEX_ID_USHR_INT_2ADDR = 178,
    XXBYTE_DEX_ID_ADD_LONG_2ADDR = 179,
    XXBYTE_DEX_ID_SUB_LONG_2ADDR = 180,
    XXBYTE_DEX_ID_MUL_LONG_2ADDR = 181,
    XXBYTE_DEX_ID_DIV_LONG_2ADDR = 182,
    XXBYTE_DEX_ID_REM_LONG_2ADDR = 183,
    XXBYTE_DEX_ID_AND_LONG_2ADDR = 184,
    XXBYTE_DEX_ID_OR_LONG_2ADDR = 185,
    XXBYTE_DEX_ID_XOR_LONG_2ADDR = 186,
    XXBYTE_DEX_ID_SHL_LONG_2ADDR = 187,
    XXBYTE_DEX_ID_SHR_LONG_2ADDR = 188,
    XXBYTE_DEX_ID_USHR_LONG_2ADDR = 189,
    XXBYTE_DEX_ID_ADD_FLOAT_2ADDR = 190,
    XXBYTE_DEX_ID_SUB_FLOAT_2ADDR = 191,
    XXBYTE_DEX_ID_MUL_FLOAT_2ADDR = 192,
    XXBYTE_DEX_ID_DIV_FLOAT_2ADDR = 193,
    XXBYTE_DEX_ID_REM_FLOAT_2ADDR = 194,
    XXBYTE_DEX_ID_ADD_DOUBLE_2ADDR = 195,
    XXBYTE_DEX_ID_SUB_DOUBLE_2ADDR = 196,
    XXBYTE_DEX_ID_MUL_DOUBLE_2ADDR = 197,
    XXBYTE_DEX_ID_DIV_DOUBLE_2ADDR = 198,
    XXBYTE_DEX_ID_REM_DOUBLE_2ADDR = 199,
    XXBYTE_DEX_ID_ADD_INT_LIT16 = 200,
    XXBYTE_DEX_ID_RSUB_INT = 201,
    XXBYTE_DEX_ID_MUL_INT_LIT16 = 202,
    XXBYTE_DEX_ID_DIV_INT_LIT16 = 203,
    XXBYTE_DEX_ID_REM_INT_LIT16 = 204,
    XXBYTE_DEX_ID_AND_INT_LIT16 = 205,
    XXBYTE_DEX_ID_OR_INT_LIT16 = 206,
    XXBYTE_DEX_ID_XOR_INT_LIT16 = 207,
    XXBYTE_DEX_ID_ADD_INT_LIT8 = 208,
    XXBYTE_DEX_ID_RSUB_INT_LIT8 = 209,
    XXBYTE_DEX_ID_MUL_INT_LIT8 = 210,
    XXBYTE_DEX_ID_DIV_INT_LIT8 = 211,
    XXBYTE_DEX_ID_REM_INT_LIT8 = 212,
    XXBYTE_DEX_ID_AND_INT_LIT8 = 213,
    XXBYTE_DEX_ID_OR_INT_LIT8 = 214,
    XXBYTE_DEX_ID_XOR_INT_LIT8 = 215,
    XXBYTE_DEX_ID_SHL_INT_LIT8 = 216,
    XXBYTE_DEX_ID_SHR_INT_LIT8 = 217,
    XXBYTE_DEX_ID_USHR_INT_LIT8 = 218,
    XXBYTE_DEX_ID_INVOKE_POLYMORPHIC = 219,
    XXBYTE_DEX_ID_INVOKE_POLYMORPHIC_RANGE = 220,
    XXBYTE_DEX_ID_INVOKE_CUSTOM = 221,
    XXBYTE_DEX_ID_INVOKE_CUSTOM_RANGE = 222,
    XXBYTE_DEX_ID_CONST_METHOD_HANDLE = 223,
    XXBYTE_DEX_ID_CONST_METHOD_TYPE = 224,
    XXBYTE_DEX_ID_PACKED_SWITCH_PAYLOAD = 225,
    XXBYTE_DEX_ID_SPARSE_SWITCH_PAYLOAD = 226,
    XXBYTE_DEX_ID_FILL_ARRAY_DATA_PAYLOAD = 227,
    XXBYTE_DEX_ID_IGET_VOLATILE = 228,
    XXBYTE_DEX_ID_IPUT_VOLATILE = 229,
    XXBYTE_DEX_ID_SGET_VOLATILE = 230,
    XXBYTE_DEX_ID_SPUT_VOLATILE = 231,
    XXBYTE_DEX_ID_IGET_OBJECT_VOLATILE = 232,
    XXBYTE_DEX_ID_IGET_WIDE_VOLATILE = 233,
    XXBYTE_DEX_ID_IPUT_WIDE_VOLATILE = 234,
    XXBYTE_DEX_ID_SGET_WIDE_VOLATILE = 235,
    XXBYTE_DEX_ID_SPUT_WIDE_VOLATILE = 236,
    XXBYTE_DEX_ID_THROW_VERIFICATION_ERROR = 237,
    XXBYTE_DEX_ID_EXECUTE_INLINE = 238,
    XXBYTE_DEX_ID_EXECUTE_INLINE_RANGE = 239,
    XXBYTE_DEX_ID_INVOKE_OBJECT_INIT_RANGE = 240,
    XXBYTE_DEX_ID_RETURN_VOID_BARRIER = 241,
    XXBYTE_DEX_ID_IGET_QUICK = 242,
    XXBYTE_DEX_ID_IGET_WIDE_QUICK = 243,
    XXBYTE_DEX_ID_IGET_OBJECT_QUICK = 244,
    XXBYTE_DEX_ID_IPUT_QUICK = 245,
    XXBYTE_DEX_ID_IPUT_WIDE_QUICK = 246,
    XXBYTE_DEX_ID_IPUT_OBJECT_QUICK = 247,
    XXBYTE_DEX_ID_INVOKE_VIRTUAL_QUICK = 248,
    XXBYTE_DEX_ID_INVOKE_VIRTUAL_QUICK_RANGE = 249,
    XXBYTE_DEX_ID_INVOKE_SUPER_QUICK = 250,
    XXBYTE_DEX_ID_INVOKE_SUPER_QUICK_RANGE = 251,
    XXBYTE_DEX_ID_IPUT_OBJECT_VOLATILE = 252,
    XXBYTE_DEX_ID_SGET_OBJECT_VOLATILE = 253,
    XXBYTE_DEX_ID_SPUT_OBJECT_VOLATILE = 254,
    XXBYTE_DEX_ID_COUNT = 255
} xxbyte_dex_id;

/* A parsed code_item (DEX format, "code_item"). */
typedef struct xxbyte_dex_code_item {
    uint16_t registers_size;
    uint16_t ins_size;
    uint16_t outs_size;
    uint16_t tries_size;
    uint32_t debug_info_off;
    uint32_t insns_size;   /* in 16-bit code units */
    uint32_t insns_offset; /* byte offset of insns from data[0] */
    uint32_t tries_offset; /* byte offset of try_items from data[0], 0 if none */
} xxbyte_dex_code_item;

/*
 * Parses the little-endian code_item at data[0], which must be 4-byte
 * aligned in the file for the padding rule to hold. Checks that the header,
 * the insns array and, when tries_size is nonzero, the padding and the
 * try_item array fit in size bytes; the encoded_catch_handler_list that
 * follows the tries (at tries_offset + 8 * tries_size) is not parsed.
 * Returns XXBYTE_STATUS_TRUNCATED when the item does not fit, with *item
 * zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_dex_parse_code_item(const uint8_t *data, size_t size, xxbyte_dex_code_item *item);

/*
 * DEX files.
 *
 * xxbyte_dex_open checks the header of a .dex image in memory and records
 * where its tables are; every other function below reads the image through
 * that record with bounds checks, so any input is safe. Nothing is
 * allocated and the image is borrowed: keep it alive while the record is in
 * use. Supported are the little-endian versions 035, 037, 038, 039, 040 and
 * 041 ("dex\n0NN\0"). Version 041 (Android 16, experimental) may hold
 * several dex headers in one container file; only the first, the one at
 * offset 0, is read, and as in every version all offsets are from the start
 * of the file. Big-endian images (endian_tag 0x78563412), the pre-release
 * versions 009 and 013, odex ("dey\n") and compact dex ("cdex") are not
 * read.
 *
 * Indexes and counts are those of the format. A missing index (for example
 * a class without a superclass, or a catch-all handler) is
 * XXBYTE_DEX_NO_INDEX.
 */
#define XXBYTE_DEX_NO_INDEX 0xFFFFFFFFu

/* map_list item types used by the reader. */
#define XXBYTE_DEX_TYPE_HEADER_ITEM 0x0000u
#define XXBYTE_DEX_TYPE_CALL_SITE_ID_ITEM 0x0007u
#define XXBYTE_DEX_TYPE_METHOD_HANDLE_ITEM 0x0008u
#define XXBYTE_DEX_TYPE_MAP_LIST 0x1000u

/* A table of fixed-size items: count items from offset (in data[0..]). */
typedef struct xxbyte_dex_section {
    uint32_t count;
    uint32_t offset;
} xxbyte_dex_section;

typedef struct xxbyte_dex_file {
    const uint8_t *data; /* borrowed image */
    size_t size;         /* bytes of data (at most 4 GiB are looked at) */
    uint32_t version;    /* 35 .. 41 */
    uint32_t mode;       /* XXBYTE_DEX_MODE(version) */
    uint32_t checksum;   /* as stored; not verified */
    uint32_t file_size;
    uint32_t header_size;
    uint32_t endian_tag;
    uint32_t link_size;
    uint32_t link_off;
    uint32_t map_off;   /* 0 when the file has no map_list */
    uint32_t map_count; /* map_list entries */
    xxbyte_dex_section string_ids;
    xxbyte_dex_section type_ids;
    xxbyte_dex_section proto_ids;
    xxbyte_dex_section field_ids;
    xxbyte_dex_section method_ids;
    xxbyte_dex_section class_defs;
    xxbyte_dex_section call_site_ids;  /* from the map_list (version 038+) */
    xxbyte_dex_section method_handles; /* from the map_list (version 038+) */
    uint32_t data_size;                /* unused from version 041 */
    uint32_t data_off;
    uint32_t container_size; /* version 041 with a 0x78-byte header, else file_size */
    uint32_t header_offset;  /* version 041 with a 0x78-byte header, else 0 */
} xxbyte_dex_file;

/*
 * Opens the dex image data[0..size). Checks the magic and version, the
 * endian tag, header_size (at least 0x70; 0x78 for the version 041 fields)
 * and that file_size (container_size for version 041) and every id table
 * and the map_list fit in size. Returns XXBYTE_STATUS_INVALID_INSTRUCTION
 * when data is not a dex image this reader supports, XXBYTE_STATUS_TRUNCATED
 * when the image or a table runs past size. On failure *dex is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_dex_open(const uint8_t *data, size_t size, xxbyte_dex_file *dex);

/* Entry index of the map_list. */
XXBYTE_API xxbyte_status xxbyte_dex_map_item(const xxbyte_dex_file *dex, uint32_t index, uint16_t *type, uint32_t *count, uint32_t *offset);

/*
 * The string_data_item of string_idx: *mutf8 points at its MUTF-8 bytes in
 * the image, *length is their count up to the terminating NUL and
 * *utf16_size the stored UTF-16 length. Any output pointer may be NULL.
 */
XXBYTE_API xxbyte_status xxbyte_dex_string_data(const xxbyte_dex_file *dex, uint32_t string_idx, const uint8_t **mutf8, size_t *length, uint32_t *utf16_size);

/*
 * Text of a string. MUTF-8 is decoded to UTF-16 code units. A byte that
 * starts no valid sequence decodes as the code unit of its own value.
 *
 * Default: UTF-8 text (surrogate pairs combined), except that the code units
 * 0x00-0x1f, 0x7f-0x9f and unpaired surrogates are written as \uXXXX.
 * XXBYTE_DEX_TEXT_QUOTED: a Java string literal as smali writes it:
 * "...", with \" \' \\ \n \r \t and every other code unit outside
 * 0x20-0x7e as \uXXXX (lower-case hex), so the text is ASCII.
 *
 * The complete length (without the NUL) is stored in *length (may be NULL);
 * buffer receives as much as fits and is NUL-terminated when buffer_size is
 * nonzero. Returns XXBYTE_STATUS_INVALID_INSTRUCTION for an index out of
 * range, XXBYTE_STATUS_TRUNCATED when the string runs past the image.
 */
#define XXBYTE_DEX_TEXT_DEFAULT 0x0000u
#define XXBYTE_DEX_TEXT_QUOTED 0x0001u
XXBYTE_API xxbyte_status xxbyte_dex_string(const xxbyte_dex_file *dex, uint32_t string_idx, uint32_t flags, char *buffer, size_t buffer_size, size_t *length);

/* type_ids[type_idx]: the string index of its descriptor. */
XXBYTE_API xxbyte_status xxbyte_dex_type(const xxbyte_dex_file *dex, uint32_t type_idx, uint32_t *descriptor_idx);

typedef struct xxbyte_dex_proto_id {
    uint32_t shorty_idx;      /* string */
    uint32_t return_type_idx; /* type */
    uint32_t parameters_off;  /* type_list, 0 for none */
} xxbyte_dex_proto_id;
XXBYTE_API xxbyte_status xxbyte_dex_proto(const xxbyte_dex_file *dex, uint32_t proto_idx, xxbyte_dex_proto_id *proto);

/*
 * The type_list at offset (0 is the empty list): *count receives its size
 * and the first min(capacity, count) type indexes are stored; types may be
 * NULL when capacity is 0.
 */
XXBYTE_API xxbyte_status xxbyte_dex_type_list(const xxbyte_dex_file *dex, uint32_t offset, uint16_t *types, size_t capacity, size_t *count);

typedef struct xxbyte_dex_field_id {
    uint16_t class_idx; /* type */
    uint16_t type_idx;  /* type */
    uint32_t name_idx;  /* string */
} xxbyte_dex_field_id;
XXBYTE_API xxbyte_status xxbyte_dex_field(const xxbyte_dex_file *dex, uint32_t field_idx, xxbyte_dex_field_id *field);

typedef struct xxbyte_dex_method_id {
    uint16_t class_idx; /* type */
    uint16_t proto_idx; /* proto */
    uint32_t name_idx;  /* string */
} xxbyte_dex_method_id;
XXBYTE_API xxbyte_status xxbyte_dex_method(const xxbyte_dex_file *dex, uint32_t method_idx, xxbyte_dex_method_id *method);

typedef struct xxbyte_dex_class_def {
    uint32_t class_idx; /* type */
    uint32_t access_flags;
    uint32_t superclass_idx;  /* type or XXBYTE_DEX_NO_INDEX */
    uint32_t interfaces_off;  /* type_list or 0 */
    uint32_t source_file_idx; /* string or XXBYTE_DEX_NO_INDEX */
    uint32_t annotations_off;
    uint32_t class_data_off; /* 0 for a class without fields and methods */
    uint32_t static_values_off;
} xxbyte_dex_class_def;
XXBYTE_API xxbyte_status xxbyte_dex_class(const xxbyte_dex_file *dex, uint32_t class_def_idx, xxbyte_dex_class_def *class_def);

/* method_handle_type values. */
#define XXBYTE_DEX_METHOD_HANDLE_STATIC_PUT 0u
#define XXBYTE_DEX_METHOD_HANDLE_STATIC_GET 1u
#define XXBYTE_DEX_METHOD_HANDLE_INSTANCE_PUT 2u
#define XXBYTE_DEX_METHOD_HANDLE_INSTANCE_GET 3u
#define XXBYTE_DEX_METHOD_HANDLE_INVOKE_STATIC 4u
#define XXBYTE_DEX_METHOD_HANDLE_INVOKE_INSTANCE 5u
#define XXBYTE_DEX_METHOD_HANDLE_INVOKE_CONSTRUCTOR 6u
#define XXBYTE_DEX_METHOD_HANDLE_INVOKE_DIRECT 7u
#define XXBYTE_DEX_METHOD_HANDLE_INVOKE_INTERFACE 8u

typedef struct xxbyte_dex_method_handle {
    uint16_t type;       /* XXBYTE_DEX_METHOD_HANDLE_* */
    uint16_t member_idx; /* field (types 0-3) or method (4-8) */
} xxbyte_dex_method_handle;
XXBYTE_API xxbyte_status xxbyte_dex_method_handle_item(const xxbyte_dex_file *dex, uint32_t handle_idx, xxbyte_dex_method_handle *handle);

/* call_site_ids[call_site_idx]: the offset of its encoded_array_item. */
XXBYTE_API xxbyte_status xxbyte_dex_call_site(const xxbyte_dex_file *dex, uint32_t call_site_idx, uint32_t *offset);

/* The class_data_item header of a class. All zero when class_data_off is 0. */
typedef struct xxbyte_dex_class_data {
    uint32_t static_fields_size;
    uint32_t instance_fields_size;
    uint32_t direct_methods_size;
    uint32_t virtual_methods_size;
    uint32_t offset; /* class_data_off */
} xxbyte_dex_class_data;
XXBYTE_API xxbyte_status xxbyte_dex_class_data_header(const xxbyte_dex_file *dex, uint32_t class_def_idx, xxbyte_dex_class_data *class_data);

/* An encoded_field with its index difference applied. */
typedef struct xxbyte_dex_field_def {
    uint32_t field_idx;
    uint32_t access_flags;
    uint32_t is_instance; /* 0 for static_fields, 1 for instance_fields */
} xxbyte_dex_field_def;

/* An encoded_method with its index difference applied. */
typedef struct xxbyte_dex_method_def {
    uint32_t method_idx;
    uint32_t access_flags;
    uint32_t code_off;   /* code_item, 0 for abstract and native methods */
    uint32_t is_virtual; /* 0 for direct_methods, 1 for virtual_methods */
} xxbyte_dex_method_def;

/*
 * The fields (static, then instance) or methods (direct, then virtual) of a
 * class, in class_data_item order. *count receives the total and the first
 * min(capacity, count) are stored; the array may be NULL when capacity is
 * 0. Returns XXBYTE_STATUS_TRUNCATED when the class_data_item runs past the
 * image and XXBYTE_STATUS_INVALID_INSTRUCTION for an overlong ULEB128.
 */
XXBYTE_API xxbyte_status xxbyte_dex_class_fields(const xxbyte_dex_file *dex, uint32_t class_def_idx, xxbyte_dex_field_def *fields, size_t capacity, size_t *count);
XXBYTE_API xxbyte_status xxbyte_dex_class_methods(const xxbyte_dex_file *dex, uint32_t class_def_idx, xxbyte_dex_method_def *methods, size_t capacity, size_t *count);

/*
 * Parses the code_item at code_off. Unlike xxbyte_dex_parse_code_item, the
 * offsets in *item are from the start of the image, the try_items are
 * aligned in the image, and the encoded_catch_handler_list is walked too:
 * *size (may be NULL) receives the byte size of the whole code_item. On
 * failure *item is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_dex_code(const xxbyte_dex_file *dex, uint32_t code_off, xxbyte_dex_code_item *item, uint32_t *size);

/* A try_item. Addresses are in 16-bit code units from the insns. */
typedef struct xxbyte_dex_try {
    uint32_t start_addr;
    uint16_t insn_count;
    uint16_t handler_off; /* from the start of the encoded_catch_handler_list */
} xxbyte_dex_try;

/* One handler of an encoded_catch_handler. */
typedef struct xxbyte_dex_catch {
    uint32_t type_idx; /* XXBYTE_DEX_NO_INDEX for the catch-all */
    uint32_t addr;     /* code units */
} xxbyte_dex_catch;

/* The try_items of a code_item parsed by xxbyte_dex_code; capacity and
 * count as for xxbyte_dex_class_methods. */
XXBYTE_API xxbyte_status xxbyte_dex_tries(const xxbyte_dex_file *dex, const xxbyte_dex_code_item *item, xxbyte_dex_try *tries, size_t capacity, size_t *count);

/* The handlers of the encoded_catch_handler at handler_off: the typed ones
 * in order, then the catch-all if there is one. */
XXBYTE_API xxbyte_status xxbyte_dex_catches(const xxbyte_dex_file *dex, const xxbyte_dex_code_item *item, uint32_t handler_off, xxbyte_dex_catch *catches,
                                            size_t capacity, size_t *count);

/*
 * Access flags as smali writes them, space-separated in bit order: for
 * XXBYTE_DEX_FLAGS_CLASS public private protected static final interface
 * abstract synthetic annotation enum; for XXBYTE_DEX_FLAGS_METHOD public
 * private protected static final synchronized bridge varargs native abstract
 * strictfp synthetic constructor declared-synchronized; for
 * XXBYTE_DEX_FLAGS_FIELD public private protected static final volatile
 * transient synthetic enum. Other bits are not shown. Returns the complete
 * length; buffer as for xxbyte_dex_string.
 */
#define XXBYTE_DEX_FLAGS_CLASS 0u
#define XXBYTE_DEX_FLAGS_METHOD 1u
#define XXBYTE_DEX_FLAGS_FIELD 2u
XXBYTE_API size_t xxbyte_dex_access_flags_text(uint32_t access_flags, uint32_t which, char *buffer, size_t buffer_size);

/* Extra reference kinds of xxbyte_dex_format_ref. */
#define XXBYTE_DEX_REF_STRING_TEXT (XXBYTE_INDEX_FAMILY_FIRST + 0)  /* string, unquoted */
#define XXBYTE_DEX_REF_FIELD_SHORT (XXBYTE_INDEX_FAMILY_FIRST + 1)  /* f:I */
#define XXBYTE_DEX_REF_METHOD_SHORT (XXBYTE_INDEX_FAMILY_FIRST + 2) /* m(I)V */

/*
 * A reference as smali (dexlib2's DexFormatter) writes it. kind is one of
 *
 *   XXBYTE_INDEX_STRING         "a\n"   (XXBYTE_DEX_TEXT_QUOTED)
 *   XXBYTE_INDEX_TYPE           Ljava/lang/String;   [I
 *   XXBYTE_INDEX_FIELD          Lcom/a/B;->f:I
 *   XXBYTE_INDEX_METHOD         Lcom/a/B;->m(ILjava/lang/String;)V
 *   XXBYTE_INDEX_PROTO          (ILjava/lang/String;)V
 *   XXBYTE_INDEX_METHOD_HANDLE  invoke-static@Lcom/a/B;->m()V
 *                               (static-put, static-get, instance-put,
 *                               instance-get, invoke-static,
 *                               invoke-instance, invoke-constructor,
 *                               invoke-direct, invoke-interface)
 *   XXBYTE_INDEX_CALL_SITE      call_site_0("run", ()Ljava/lang/Runnable;,
 *                               ()V, invoke-static@Lcom/a/E;->l()V, ()V)@
 *                               Ljava/lang/invoke/LambdaMetafactory;->
 *                               metafactory(...)Ljava/lang/invoke/CallSite;
 *                               (one line): the name and method type, then
 *                               the extra arguments, then the bootstrap
 *                               method handle's member
 *   XXBYTE_DEX_REF_STRING_TEXT  the string as XXBYTE_DEX_TEXT_DEFAULT text
 *   XXBYTE_DEX_REF_FIELD_SHORT  f:I
 *   XXBYTE_DEX_REF_METHOD_SHORT m(ILjava/lang/String;)V
 *
 * Names and descriptors are XXBYTE_DEX_TEXT_DEFAULT text. Call-site
 * arguments print as dexlib2 does: byte, short, char, int and long as
 * unsigned hex of their width (0xff, 0xfffffffb), float and double as Java's
 * Float/Double.toString (1.5, 1.0E10, NaN), true, false, null, strings
 * quoted, types, fields, methods, enums (Lcom/a/En;->A:Lcom/a/En;), method
 * types, method handles, Array[...] and Annotation[Ltype;, name=value]. A
 * single reference is cut after 64 KiB of text, ending in "..." (a string
 * is then not required to end within the image).
 *
 * Returns XXBYTE_STATUS_INVALID_INSTRUCTION for an index out of range, an
 * unknown kind or method handle type, or a malformed call site, and
 * XXBYTE_STATUS_TRUNCATED when data runs past the image. *length and buffer
 * as for xxbyte_dex_string.
 */
XXBYTE_API xxbyte_status xxbyte_dex_format_ref(const xxbyte_dex_file *dex, uint32_t kind, uint32_t index, char *buffer, size_t buffer_size, size_t *length);

/*
 * A ready-made xxbyte_resolver: context is a const xxbyte_dex_file *.
 * Resolves INDEX operands of the kinds string, type, field, method, proto,
 * call_site and method_handle through xxbyte_dex_format_ref, for example
 *
 *   const-string v0, "hi"
 *   invoke-virtual {v1, v0}, Ljava/io/PrintStream;->println(Ljava/lang/String;)V
 *   iget v0, v1, Lcom/a/B;->f:I
 *   invoke-polymorphic {v0, v1}, Ljava/lang/invoke/MethodHandle;->invokeExact([Ljava/lang/Object;)Ljava/lang/Object;, (I)V
 *
 * Indexes out of range, and the odex inline, vtable and field_offset
 * indexes, branches and payload addresses keep their numeric form. The core
 * formatter keeps at most 511 characters of a resolved name; the
 * xxbyte_disasm_file listing (below) is not limited.
 */
XXBYTE_API size_t xxbyte_dex_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/*
 * xxbyte_disasm_file(XXBYTE_FAMILY_DEX, ...) lists a single .dex image (not
 * an APK, odex, or multi-dex container beyond its first dex). With
 * XXBYTE_MODE_AUTO the mode is XXBYTE_DEX_MODE(version of the file); an
 * explicit mode (for example XXBYTE_DEX_MODE_ODEX | XXBYTE_DEX_MODE(35))
 * overrides it. For every class_def, in file order:
 *
 *   .class public Lcom/a/B;
 *
 * and for each of its direct, then virtual methods:
 *
 *   .method public static main([Ljava/lang/String;)V
 *   # Lcom/a/B;->main([Ljava/lang/String;)V, code_item 0xa40, registers 8, ins 1, outs 6, insns 101, tries 1
 *   .catch Ljava/lang/RuntimeException; {0x0020 .. 0x0046} 0x0082
 *   .catchall {0x0020 .. 0x0046} 0x0086
 *   00000000  1A 00 45 00              const-string v0, "hi"
 *   ...
 *   0000008C  00 01 03 00 FF FF FF..   packed-switch-payload -0x1 {+0xc, +0x12, +0xc}
 *                                       case -0x1: 0x007a
 *   .end method
 *
 * followed by an empty line after each class. Methods without code print
 * "# <method>, no code". Try ranges are byte addresses, the end exclusive;
 * each handler of a try_item has its own .catch line. A switch payload
 * referenced by a packed-switch or sparse-switch of the method is followed
 * by one "case <key>: <target>" line per entry, with the absolute target of
 * the first switch that references it, indented to the text column. Names
 * are resolved as by xxbyte_dex_resolve, without the length limit. A method
 * whose code_item is malformed prints "# <method>, code_item 0x..: <status>"
 * and the listing goes on; the first such status is returned at the end.
 * A listing that grows past 64 MiB plus 64 times the image size (only
 * crafted images, for example one huge code_item shared by many methods)
 * ends with "# listing stopped: output limit" and returns
 * XXBYTE_STATUS_OUT_OF_MEMORY.
 */

#ifdef __cplusplus
}
#endif

#endif
