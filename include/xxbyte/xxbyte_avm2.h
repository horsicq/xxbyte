#ifndef XXBYTE_XXBYTE_AVM2_H
#define XXBYTE_XXBYTE_AVM2_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_AVM2
#error "the AVM2 (ActionScript 3) decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Modes. A mode is an abcFile version, XXBYTE_AVM2_MODE(major, minor), as
 * the avmplus AbcParser accepts it: 46.16 (every Flash Player 9+ and AIR
 * compiler) and 47.12 to 47.20. The float opcodes (lf32x4, sf32x4,
 * pushfloat, pushfloat4, convert_f, unplus, convert_f4) need 47.16 or later
 * and are INVALID_INSTRUCTION in the earlier versions.
 * XXBYTE_AVM2_MODE_DEFAULT (0) decodes the newest set, 47.20. Any other mode
 * returns XXBYTE_STATUS_UNSUPPORTED_MODE.
 *
 * Addresses are byte offsets in the method body's code. Decoding needs no
 * context; xxbyte_decode_ex ignores it.
 */
#define XXBYTE_AVM2_MODE(major, minor) ((((uint32_t)(major) & 0xFFFFu) << 16) | ((uint32_t)(minor) & 0xFFFFu))
#define XXBYTE_AVM2_MODE_MAJOR(mode) (((uint32_t)(mode) >> 16) & 0xFFFFu)
#define XXBYTE_AVM2_MODE_MINOR(mode) ((uint32_t)(mode) & 0xFFFFu)
#define XXBYTE_AVM2_MODE_DEFAULT 0u
/* The first version with the float opcodes and constant pools. */
#define XXBYTE_AVM2_MODE_FLOAT XXBYTE_AVM2_MODE(47, 16)

/*
 * Mnemonic identifiers (xxbyte_instruction.id): every opcode of avmplus
 * core/opcodes.tbl that ABC code may contain, in opcode order, named as the
 * AVM2 Overview names them ("getlocal_0", "convert_s"; opcodes.tbl and the
 * avmplus verifier spell the register shortcuts getlocal0 to setlocal3,
 * every other name is the same). The VM-internal
 * opcodes (lix8, lix16, callsuperid, callinterface, findpropglobalstrict,
 * findpropglobal, restargc, restarg), which the verifier rejects, and the
 * unused values are not decoded. The name is the mnemonic upper-cased with
 * every run of non-alphanumeric characters replaced by '_'. Valid ids are 1
 * to XXBYTE_AVM2_ID_COUNT - 1.
 */
typedef enum xxbyte_avm2_id {
    XXBYTE_AVM2_ID_INVALID = 0,
    XXBYTE_AVM2_ID_BKPT = 1,
    XXBYTE_AVM2_ID_NOP = 2,
    XXBYTE_AVM2_ID_THROW = 3,
    XXBYTE_AVM2_ID_GETSUPER = 4,
    XXBYTE_AVM2_ID_SETSUPER = 5,
    XXBYTE_AVM2_ID_DXNS = 6,
    XXBYTE_AVM2_ID_DXNSLATE = 7,
    XXBYTE_AVM2_ID_KILL = 8,
    XXBYTE_AVM2_ID_LABEL = 9,
    XXBYTE_AVM2_ID_LF32X4 = 10,
    XXBYTE_AVM2_ID_SF32X4 = 11,
    XXBYTE_AVM2_ID_IFNLT = 12,
    XXBYTE_AVM2_ID_IFNLE = 13,
    XXBYTE_AVM2_ID_IFNGT = 14,
    XXBYTE_AVM2_ID_IFNGE = 15,
    XXBYTE_AVM2_ID_JUMP = 16,
    XXBYTE_AVM2_ID_IFTRUE = 17,
    XXBYTE_AVM2_ID_IFFALSE = 18,
    XXBYTE_AVM2_ID_IFEQ = 19,
    XXBYTE_AVM2_ID_IFNE = 20,
    XXBYTE_AVM2_ID_IFLT = 21,
    XXBYTE_AVM2_ID_IFLE = 22,
    XXBYTE_AVM2_ID_IFGT = 23,
    XXBYTE_AVM2_ID_IFGE = 24,
    XXBYTE_AVM2_ID_IFSTRICTEQ = 25,
    XXBYTE_AVM2_ID_IFSTRICTNE = 26,
    XXBYTE_AVM2_ID_LOOKUPSWITCH = 27,
    XXBYTE_AVM2_ID_PUSHWITH = 28,
    XXBYTE_AVM2_ID_POPSCOPE = 29,
    XXBYTE_AVM2_ID_NEXTNAME = 30,
    XXBYTE_AVM2_ID_HASNEXT = 31,
    XXBYTE_AVM2_ID_PUSHNULL = 32,
    XXBYTE_AVM2_ID_PUSHUNDEFINED = 33,
    XXBYTE_AVM2_ID_PUSHFLOAT = 34,
    XXBYTE_AVM2_ID_NEXTVALUE = 35,
    XXBYTE_AVM2_ID_PUSHBYTE = 36,
    XXBYTE_AVM2_ID_PUSHSHORT = 37,
    XXBYTE_AVM2_ID_PUSHTRUE = 38,
    XXBYTE_AVM2_ID_PUSHFALSE = 39,
    XXBYTE_AVM2_ID_PUSHNAN = 40,
    XXBYTE_AVM2_ID_POP = 41,
    XXBYTE_AVM2_ID_DUP = 42,
    XXBYTE_AVM2_ID_SWAP = 43,
    XXBYTE_AVM2_ID_PUSHSTRING = 44,
    XXBYTE_AVM2_ID_PUSHINT = 45,
    XXBYTE_AVM2_ID_PUSHUINT = 46,
    XXBYTE_AVM2_ID_PUSHDOUBLE = 47,
    XXBYTE_AVM2_ID_PUSHSCOPE = 48,
    XXBYTE_AVM2_ID_PUSHNAMESPACE = 49,
    XXBYTE_AVM2_ID_HASNEXT2 = 50,
    XXBYTE_AVM2_ID_LI8 = 51,
    XXBYTE_AVM2_ID_LI16 = 52,
    XXBYTE_AVM2_ID_LI32 = 53,
    XXBYTE_AVM2_ID_LF32 = 54,
    XXBYTE_AVM2_ID_LF64 = 55,
    XXBYTE_AVM2_ID_SI8 = 56,
    XXBYTE_AVM2_ID_SI16 = 57,
    XXBYTE_AVM2_ID_SI32 = 58,
    XXBYTE_AVM2_ID_SF32 = 59,
    XXBYTE_AVM2_ID_SF64 = 60,
    XXBYTE_AVM2_ID_NEWFUNCTION = 61,
    XXBYTE_AVM2_ID_CALL = 62,
    XXBYTE_AVM2_ID_CONSTRUCT = 63,
    XXBYTE_AVM2_ID_CALLMETHOD = 64,
    XXBYTE_AVM2_ID_CALLSTATIC = 65,
    XXBYTE_AVM2_ID_CALLSUPER = 66,
    XXBYTE_AVM2_ID_CALLPROPERTY = 67,
    XXBYTE_AVM2_ID_RETURNVOID = 68,
    XXBYTE_AVM2_ID_RETURNVALUE = 69,
    XXBYTE_AVM2_ID_CONSTRUCTSUPER = 70,
    XXBYTE_AVM2_ID_CONSTRUCTPROP = 71,
    XXBYTE_AVM2_ID_CALLPROPLEX = 72,
    XXBYTE_AVM2_ID_CALLSUPERVOID = 73,
    XXBYTE_AVM2_ID_CALLPROPVOID = 74,
    XXBYTE_AVM2_ID_SXI1 = 75,
    XXBYTE_AVM2_ID_SXI8 = 76,
    XXBYTE_AVM2_ID_SXI16 = 77,
    XXBYTE_AVM2_ID_APPLYTYPE = 78,
    XXBYTE_AVM2_ID_PUSHFLOAT4 = 79,
    XXBYTE_AVM2_ID_NEWOBJECT = 80,
    XXBYTE_AVM2_ID_NEWARRAY = 81,
    XXBYTE_AVM2_ID_NEWACTIVATION = 82,
    XXBYTE_AVM2_ID_NEWCLASS = 83,
    XXBYTE_AVM2_ID_GETDESCENDANTS = 84,
    XXBYTE_AVM2_ID_NEWCATCH = 85,
    XXBYTE_AVM2_ID_FINDPROPSTRICT = 86,
    XXBYTE_AVM2_ID_FINDPROPERTY = 87,
    XXBYTE_AVM2_ID_FINDDEF = 88,
    XXBYTE_AVM2_ID_GETLEX = 89,
    XXBYTE_AVM2_ID_SETPROPERTY = 90,
    XXBYTE_AVM2_ID_GETLOCAL = 91,
    XXBYTE_AVM2_ID_SETLOCAL = 92,
    XXBYTE_AVM2_ID_GETGLOBALSCOPE = 93,
    XXBYTE_AVM2_ID_GETSCOPEOBJECT = 94,
    XXBYTE_AVM2_ID_GETPROPERTY = 95,
    XXBYTE_AVM2_ID_GETOUTERSCOPE = 96,
    XXBYTE_AVM2_ID_INITPROPERTY = 97,
    XXBYTE_AVM2_ID_DELETEPROPERTY = 98,
    XXBYTE_AVM2_ID_GETSLOT = 99,
    XXBYTE_AVM2_ID_SETSLOT = 100,
    XXBYTE_AVM2_ID_GETGLOBALSLOT = 101,
    XXBYTE_AVM2_ID_SETGLOBALSLOT = 102,
    XXBYTE_AVM2_ID_CONVERT_S = 103,
    XXBYTE_AVM2_ID_ESC_XELEM = 104,
    XXBYTE_AVM2_ID_ESC_XATTR = 105,
    XXBYTE_AVM2_ID_CONVERT_I = 106,
    XXBYTE_AVM2_ID_CONVERT_U = 107,
    XXBYTE_AVM2_ID_CONVERT_D = 108,
    XXBYTE_AVM2_ID_CONVERT_B = 109,
    XXBYTE_AVM2_ID_CONVERT_O = 110,
    XXBYTE_AVM2_ID_CHECKFILTER = 111,
    XXBYTE_AVM2_ID_CONVERT_F = 112,
    XXBYTE_AVM2_ID_UNPLUS = 113,
    XXBYTE_AVM2_ID_CONVERT_F4 = 114,
    XXBYTE_AVM2_ID_COERCE = 115,
    XXBYTE_AVM2_ID_COERCE_B = 116,
    XXBYTE_AVM2_ID_COERCE_A = 117,
    XXBYTE_AVM2_ID_COERCE_I = 118,
    XXBYTE_AVM2_ID_COERCE_D = 119,
    XXBYTE_AVM2_ID_COERCE_S = 120,
    XXBYTE_AVM2_ID_ASTYPE = 121,
    XXBYTE_AVM2_ID_ASTYPELATE = 122,
    XXBYTE_AVM2_ID_COERCE_U = 123,
    XXBYTE_AVM2_ID_COERCE_O = 124,
    XXBYTE_AVM2_ID_NEGATE = 125,
    XXBYTE_AVM2_ID_INCREMENT = 126,
    XXBYTE_AVM2_ID_INCLOCAL = 127,
    XXBYTE_AVM2_ID_DECREMENT = 128,
    XXBYTE_AVM2_ID_DECLOCAL = 129,
    XXBYTE_AVM2_ID_TYPEOF = 130,
    XXBYTE_AVM2_ID_NOT = 131,
    XXBYTE_AVM2_ID_BITNOT = 132,
    XXBYTE_AVM2_ID_ADD = 133,
    XXBYTE_AVM2_ID_SUBTRACT = 134,
    XXBYTE_AVM2_ID_MULTIPLY = 135,
    XXBYTE_AVM2_ID_DIVIDE = 136,
    XXBYTE_AVM2_ID_MODULO = 137,
    XXBYTE_AVM2_ID_LSHIFT = 138,
    XXBYTE_AVM2_ID_RSHIFT = 139,
    XXBYTE_AVM2_ID_URSHIFT = 140,
    XXBYTE_AVM2_ID_BITAND = 141,
    XXBYTE_AVM2_ID_BITOR = 142,
    XXBYTE_AVM2_ID_BITXOR = 143,
    XXBYTE_AVM2_ID_EQUALS = 144,
    XXBYTE_AVM2_ID_STRICTEQUALS = 145,
    XXBYTE_AVM2_ID_LESSTHAN = 146,
    XXBYTE_AVM2_ID_LESSEQUALS = 147,
    XXBYTE_AVM2_ID_GREATERTHAN = 148,
    XXBYTE_AVM2_ID_GREATEREQUALS = 149,
    XXBYTE_AVM2_ID_INSTANCEOF = 150,
    XXBYTE_AVM2_ID_ISTYPE = 151,
    XXBYTE_AVM2_ID_ISTYPELATE = 152,
    XXBYTE_AVM2_ID_IN = 153,
    XXBYTE_AVM2_ID_INCREMENT_I = 154,
    XXBYTE_AVM2_ID_DECREMENT_I = 155,
    XXBYTE_AVM2_ID_INCLOCAL_I = 156,
    XXBYTE_AVM2_ID_DECLOCAL_I = 157,
    XXBYTE_AVM2_ID_NEGATE_I = 158,
    XXBYTE_AVM2_ID_ADD_I = 159,
    XXBYTE_AVM2_ID_SUBTRACT_I = 160,
    XXBYTE_AVM2_ID_MULTIPLY_I = 161,
    XXBYTE_AVM2_ID_GETLOCAL_0 = 162,
    XXBYTE_AVM2_ID_GETLOCAL_1 = 163,
    XXBYTE_AVM2_ID_GETLOCAL_2 = 164,
    XXBYTE_AVM2_ID_GETLOCAL_3 = 165,
    XXBYTE_AVM2_ID_SETLOCAL_0 = 166,
    XXBYTE_AVM2_ID_SETLOCAL_1 = 167,
    XXBYTE_AVM2_ID_SETLOCAL_2 = 168,
    XXBYTE_AVM2_ID_SETLOCAL_3 = 169,
    XXBYTE_AVM2_ID_DEBUG = 170,
    XXBYTE_AVM2_ID_DEBUGLINE = 171,
    XXBYTE_AVM2_ID_DEBUGFILE = 172,
    XXBYTE_AVM2_ID_BKPTLINE = 173,
    XXBYTE_AVM2_ID_TIMESTAMP = 174,
    XXBYTE_AVM2_ID_COUNT
} xxbyte_avm2_id;

/*
 * INDEX operand kinds of this family, besides XXBYTE_INDEX_STRING (string
 * pool), XXBYTE_INDEX_METHOD (method_info), XXBYTE_INDEX_CLASS (class_info)
 * and XXBYTE_INDEX_LOCAL (register).
 */
typedef enum xxbyte_avm2_index_kind {
    XXBYTE_AVM2_INDEX_INT = XXBYTE_INDEX_FAMILY_FIRST, /* integer pool */
    XXBYTE_AVM2_INDEX_UINT,                            /* uinteger pool */
    XXBYTE_AVM2_INDEX_DOUBLE,                          /* double pool */
    XXBYTE_AVM2_INDEX_FLOAT,                           /* float pool (ABC 47.16+) */
    XXBYTE_AVM2_INDEX_FLOAT4,                          /* float4 pool (ABC 47.16+) */
    XXBYTE_AVM2_INDEX_NAMESPACE,                       /* namespace pool */
    XXBYTE_AVM2_INDEX_MULTINAME,                       /* multiname pool */
    XXBYTE_AVM2_INDEX_EXCEPTION,                       /* exception_info of the method body */
    XXBYTE_AVM2_INDEX_SLOT,                            /* slot_id (1-based) of a slot trait */
    XXBYTE_AVM2_INDEX_DISP_ID,                         /* dispatch id of callmethod */
    XXBYTE_AVM2_INDEX_SCOPE,                           /* local scope stack index */
    XXBYTE_AVM2_INDEX_OUTER_SCOPE                      /* captured (outer) scope index */
} xxbyte_avm2_index_kind;

/*
 * Operands, in encoding order:
 * - A u30 operand is an INDEX of the kinds above, or an IMMEDIATE for the
 *   argument counts (call*, construct*, applytype, newobject, newarray) and
 *   line numbers (debugline, bkptline); size is its encoded width, 1 to 5.
 * - getlocal_0..3 and setlocal_0..3 carry an IMPLICIT INDEX LOCAL operand.
 * - pushbyte: a SIGNED IMMEDIATE, the u8 sign-extended. pushshort: a SIGNED
 *   IMMEDIATE holding the low 16 bits of its variable-length operand,
 *   sign-extended, which is what the VM pushes; extra holds the operand's
 *   32 bits. The verifier exempts pushshort from the u30 range check, so
 *   the operand is read as an s32 (see xxbyte_avm2_read_s32): "25 ff ff ff
 *   ff 0f" and "25 ff ff ff ff 7f" are both "pushshort -1".
 * - getscopeobject: INDEX SCOPE from a u8, as in the AVM2 Overview and the
 *   interpreter (the avmplus verifier sizes it as a u30; the two agree for
 *   indexes below 128).
 * - debug: IMMEDIATE debug_type (u8), INDEX STRING (u30), INDEX LOCAL
 *   register (u8), IMMEDIATE extra (u30).
 * - jump and if*: a BRANCH to address + 4 + s24.
 * - lookupswitch: a BRANCH default (address + s24) and an IMMEDIATE
 *   case_count; its case_count + 1 s24 offsets, also relative to the
 *   lookupswitch's address, are table_count entries at table_offset, and
 *   xxbyte_switch_entry returns keys 0 to case_count.
 *
 * Groups: jump JUMP|STOP, if* CONDITIONAL, lookupswitch SWITCH|STOP,
 * call* and construct* CALL, returnvoid and returnvalue RETURN|STOP, throw
 * THROW|STOP.
 *
 * Text: the mnemonic, then the operands separated by ", ": decimals, pool
 * and table indexes as int@N, uint@N, double@N, float@N, float4@N,
 * string@N, namespace@N, multiname@N, method@N, class@N and exception@N,
 * registers, slots, dispatch ids and scope indexes as plain decimals, and
 * branch targets as "0x" and at least four lowercase hex digits ("-0x" when
 * negative): "getlocal_0", "getlocal 4", "callproperty multiname@5, 1",
 * "jump 0x0024", "lookupswitch 0x0030, [0x0010, 0x0020]" (the default, then
 * every case; the count is not printed). A resolver is asked about every
 * displayed INDEX and BRANCH operand and about every lookupswitch case
 * target.
 */

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_avm2_mnemonic(uint32_t id);

/*
 * Variable-length integers of the abcFile format (1 to 5 bytes, 7 bits per
 * byte, low bits first; a fifth byte gives bits 28 to 31). Reads at
 * data[*offset] and advances *offset. All three reject a fifth byte with
 * the continuation bit (XXBYTE_STATUS_INVALID_INSTRUCTION) and return
 * XXBYTE_STATUS_TRUNCATED when the encoding runs past size; non-minimal
 * encodings are accepted, as avmplus accepts them.
 * - xxbyte_avm2_read_u30 rejects values of 2^30 and above (a fifth byte
 *   above 0x03) and xxbyte_avm2_read_u32 values of 2^32 and above (a fifth
 *   byte above 0x0F).
 * - xxbyte_avm2_read_s32 reads an integer or uinteger pool entry the way
 *   avmplus does (AbcParser::readS32, for both pools): any fifth byte from
 *   0x00 to 0x7F, of which only the low four bits count, so both the u32
 *   bit pattern (-1 as "ff ff ff ff 0f") and the sign-extended s32 of the
 *   AVM2 Overview ("ff ff ff ff 7f") are accepted. Shorter encodings are not
 *   sign-extended ("7f" is 127), as in avmplus. A uinteger is the value cast
 *   to uint32_t.
 * On failure *value is 0 and *offset is unchanged.
 */
XXBYTE_API xxbyte_status xxbyte_avm2_read_u30(const uint8_t *data, size_t size, size_t *offset, uint32_t *value);
XXBYTE_API xxbyte_status xxbyte_avm2_read_u32(const uint8_t *data, size_t size, size_t *offset, uint32_t *value);
XXBYTE_API xxbyte_status xxbyte_avm2_read_s32(const uint8_t *data, size_t size, size_t *offset, int32_t *value);

/* The tables of an abcFile (AVM2 Overview 4.2), in file order. */
typedef enum xxbyte_avm2_table {
    XXBYTE_AVM2_TABLE_INT = 0,     /* cpool integer: variable-length s32 */
    XXBYTE_AVM2_TABLE_UINT,        /* cpool uinteger: variable-length u32,
                                      read as an s32 like avmplus does */
    XXBYTE_AVM2_TABLE_DOUBLE,      /* cpool double: 8 bytes */
    XXBYTE_AVM2_TABLE_FLOAT,       /* cpool float: 4 bytes (47.16+) */
    XXBYTE_AVM2_TABLE_FLOAT4,      /* cpool float4: 16 bytes (47.16+) */
    XXBYTE_AVM2_TABLE_STRING,      /* string_info */
    XXBYTE_AVM2_TABLE_NAMESPACE,   /* namespace_info */
    XXBYTE_AVM2_TABLE_NS_SET,      /* ns_set_info */
    XXBYTE_AVM2_TABLE_MULTINAME,   /* multiname_info */
    XXBYTE_AVM2_TABLE_METHOD,      /* method_info */
    XXBYTE_AVM2_TABLE_METADATA,    /* metadata_info */
    XXBYTE_AVM2_TABLE_INSTANCE,    /* instance_info (class_count entries) */
    XXBYTE_AVM2_TABLE_CLASS,       /* class_info (class_count entries) */
    XXBYTE_AVM2_TABLE_SCRIPT,      /* script_info */
    XXBYTE_AVM2_TABLE_METHOD_BODY, /* method_body_info */
    XXBYTE_AVM2_TABLE_COUNT
} xxbyte_avm2_table;

/*
 * count is the encoded count. The nine constant pools (INT to MULTINAME)
 * store entries 1 to count - 1 (none when count is 0); entry 0 is implicit.
 * offset is the byte offset of the first stored entry from data[0] and size
 * the byte size of all stored entries. The float pools are absent (all
 * zero) before ABC 47.16.
 */
typedef struct xxbyte_avm2_table_info {
    uint32_t count;
    uint32_t offset;
    uint32_t size;
} xxbyte_avm2_table_info;

typedef struct xxbyte_avm2_abc {
    uint16_t minor_version;
    uint16_t major_version;
    uint32_t mode; /* XXBYTE_AVM2_MODE(major_version, minor_version) */
    uint32_t size; /* bytes of the abcFile from data[0] */
    xxbyte_avm2_table_info tables[XXBYTE_AVM2_TABLE_COUNT];
} xxbyte_avm2_abc;

/*
 * Parses the abcFile at data[0] (for example the payload of a SWF DoABC
 * tag, or of DoABC2 after its flags and name) and walks every table with
 * bounds checks; size may extend past the abcFile. Returns
 * XXBYTE_STATUS_UNSUPPORTED_MODE for a version that is not a mode,
 * XXBYTE_STATUS_TRUNCATED when a table runs past size, and
 * XXBYTE_STATUS_INVALID_INSTRUCTION for a malformed integer or an unknown
 * namespace, multiname or trait kind. Only the first 4 GiB of data are
 * looked at. On failure *abc is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_avm2_parse_abc(const uint8_t *data, size_t size, xxbyte_avm2_abc *abc);

/*
 * Byte offsets of the entries of table, from data[0], of the abcFile parsed
 * into *abc from the same bytes: offsets[i] is entry i, and for the
 * constant pools offsets[0] is 0 (the implicit entry), so a pool index
 * selects its entry directly. *count receives the number of offsets the
 * table has and the first min(capacity, count) are stored; offsets may be
 * NULL when capacity is 0.
 */
XXBYTE_API xxbyte_status xxbyte_avm2_table_offsets(const uint8_t *data, size_t size, const xxbyte_avm2_abc *abc, xxbyte_avm2_table table, uint32_t *offsets,
                                                   size_t capacity, size_t *count);

/* Reads the string_info at data[offset]: *text points into data. */
XXBYTE_API xxbyte_status xxbyte_avm2_read_string(const uint8_t *data, size_t size, size_t offset, const uint8_t **text, uint32_t *length);

/* A method_body_info. Offsets are from data[0]. */
typedef struct xxbyte_avm2_method_body {
    uint32_t method; /* method_info index */
    uint32_t max_stack;
    uint32_t local_count;
    uint32_t init_scope_depth;
    uint32_t max_scope_depth;
    uint32_t code_length;
    uint32_t code_offset;
    uint32_t exception_count;
    uint32_t exception_offset; /* the first exception_info */
    uint32_t trait_count;
    uint32_t trait_offset; /* the first traits_info */
    uint32_t offset;       /* the method_body_info */
    uint32_t size;         /* its byte size */
} xxbyte_avm2_method_body;

/* Parses the method_body_info at data[offset], with the same checks and
 * results as xxbyte_avm2_parse_abc. On failure *body is zeroed. */
XXBYTE_API xxbyte_status xxbyte_avm2_parse_method_body(const uint8_t *data, size_t size, size_t offset, xxbyte_avm2_method_body *body);

/*
 * Reads the method bodies of the abcFile parsed into *abc from the same
 * bytes. *count receives method_body_count and the first
 * min(capacity, count) bodies are stored; bodies may be NULL when capacity
 * is 0.
 */
XXBYTE_API xxbyte_status xxbyte_avm2_parse_method_bodies(const uint8_t *data, size_t size, const xxbyte_avm2_abc *abc, xxbyte_avm2_method_body *bodies, size_t capacity,
                                                         size_t *count);

/* An exception_info. from, to and target are code offsets. */
typedef struct xxbyte_avm2_exception {
    uint32_t from;
    uint32_t to; /* exclusive */
    uint32_t target;
    uint32_t exc_type; /* multiname index, 0 catches everything */
    uint32_t var_name; /* multiname index, 0 for none */
} xxbyte_avm2_exception;

/* Reads the exception table of *body, parsed from the same bytes; capacity
 * and count as for xxbyte_avm2_parse_method_bodies. */
XXBYTE_API xxbyte_status xxbyte_avm2_parse_exceptions(const uint8_t *data, size_t size, const xxbyte_avm2_method_body *body, xxbyte_avm2_exception *exceptions,
                                                      size_t capacity, size_t *count);

/* traits_info kinds (low four bits of the kind byte) and attributes (high
 * four bits). */
#define XXBYTE_AVM2_TRAIT_SLOT 0u
#define XXBYTE_AVM2_TRAIT_METHOD 1u
#define XXBYTE_AVM2_TRAIT_GETTER 2u
#define XXBYTE_AVM2_TRAIT_SETTER 3u
#define XXBYTE_AVM2_TRAIT_CLASS 4u
#define XXBYTE_AVM2_TRAIT_FUNCTION 5u
#define XXBYTE_AVM2_TRAIT_CONST 6u
#define XXBYTE_AVM2_TRAIT_ATTR_FINAL 0x1u
#define XXBYTE_AVM2_TRAIT_ATTR_OVERRIDE 0x2u
#define XXBYTE_AVM2_TRAIT_ATTR_METADATA 0x4u

/* A traits_info. Offsets are from data[0]. */
typedef struct xxbyte_avm2_trait {
    uint32_t name;      /* multiname index */
    uint8_t kind;       /* XXBYTE_AVM2_TRAIT_* */
    uint8_t attributes; /* XXBYTE_AVM2_TRAIT_ATTR_* */
    uint8_t value_kind; /* slot/const vkind when value_index != 0 */
    uint8_t reserved;
    uint32_t id;          /* slot_id, or disp_id of a method trait */
    uint32_t index;       /* slot/const type_name, class_info index, or
                             method_info index */
    uint32_t value_index; /* slot/const vindex, else 0 */
    uint32_t metadata_count;
    uint32_t metadata_offset; /* the first metadata index */
    uint32_t offset;          /* the traits_info */
    uint32_t size;            /* its byte size */
} xxbyte_avm2_trait;

/*
 * Parses the traits_info at data[offset] (a trait of a method body, an
 * instance, a class or a script); the next trait starts at offset + size.
 * Kind 5 (Function) is read with its AVM2 Overview layout. On failure
 * *trait is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_avm2_parse_trait(const uint8_t *data, size_t size, size_t offset, xxbyte_avm2_trait *trait);

/* namespace_info kinds. */
#define XXBYTE_AVM2_NS_PRIVATE 0x05u          /* PrivateNamespace */
#define XXBYTE_AVM2_NS_NAMESPACE 0x08u        /* Namespace */
#define XXBYTE_AVM2_NS_PACKAGE 0x16u          /* PackageNamespace */
#define XXBYTE_AVM2_NS_PACKAGE_INTERNAL 0x17u /* PackageInternalNs */
#define XXBYTE_AVM2_NS_PROTECTED 0x18u        /* ProtectedNamespace */
#define XXBYTE_AVM2_NS_EXPLICIT 0x19u         /* ExplicitNamespace */
#define XXBYTE_AVM2_NS_STATIC_PROTECTED 0x1Au /* StaticProtectedNs */

/* multiname_info kinds. */
#define XXBYTE_AVM2_MN_QNAME 0x07u
#define XXBYTE_AVM2_MN_QNAME_A 0x0Du
#define XXBYTE_AVM2_MN_RTQNAME 0x0Fu
#define XXBYTE_AVM2_MN_RTQNAME_A 0x10u
#define XXBYTE_AVM2_MN_RTQNAME_L 0x11u
#define XXBYTE_AVM2_MN_RTQNAME_LA 0x12u
#define XXBYTE_AVM2_MN_MULTINAME 0x09u
#define XXBYTE_AVM2_MN_MULTINAME_A 0x0Eu
#define XXBYTE_AVM2_MN_MULTINAME_L 0x1Bu
#define XXBYTE_AVM2_MN_MULTINAME_LA 0x1Cu
#define XXBYTE_AVM2_MN_TYPENAME 0x1Du

/* method_info flags. */
#define XXBYTE_AVM2_METHOD_NEED_ARGUMENTS 0x01u
#define XXBYTE_AVM2_METHOD_NEED_ACTIVATION 0x02u
#define XXBYTE_AVM2_METHOD_NEED_REST 0x04u
#define XXBYTE_AVM2_METHOD_HAS_OPTIONAL 0x08u
#define XXBYTE_AVM2_METHOD_IGNORE_REST 0x10u
#define XXBYTE_AVM2_METHOD_NATIVE 0x20u
#define XXBYTE_AVM2_METHOD_SET_DXNS 0x40u
#define XXBYTE_AVM2_METHOD_HAS_PARAM_NAMES 0x80u

/*
 * A method_info. Offsets are from data[0]: the param_count parameter type
 * u30s start at param_types_offset, the option_count option_details (u30
 * value index, u8 kind) at options_offset and the param_count parameter
 * name u30s at param_names_offset (0 without HAS_PARAM_NAMES).
 */
typedef struct xxbyte_avm2_method_info {
    uint32_t param_count;
    uint32_t return_type; /* multiname index, 0 for any */
    uint32_t name;        /* string index of the debug name */
    uint32_t flags;       /* XXBYTE_AVM2_METHOD_* */
    uint32_t option_count;
    uint32_t param_types_offset;
    uint32_t options_offset;
    uint32_t param_names_offset;
    uint32_t offset; /* the method_info */
    uint32_t size;   /* its byte size */
} xxbyte_avm2_method_info;

/* Parses the method_info at data[offset], with the checks and results of
 * xxbyte_avm2_parse_abc. On failure *method is zeroed. */
XXBYTE_API xxbyte_status xxbyte_avm2_parse_method_info(const uint8_t *data, size_t size, size_t offset, xxbyte_avm2_method_info *method);

/*
 * Name resolution. An xxbyte_avm2_names holds the parsed tables of one
 * abcFile and reads its bytes on demand; data must stay alive and unchanged
 * while it is used. xxbyte_avm2_names_create parses the abcFile at data[0]
 * (results as for xxbyte_avm2_parse_abc, plus XXBYTE_STATUS_OUT_OF_MEMORY)
 * and stores a new object in *names (NULL on failure); free it with
 * xxbyte_avm2_names_destroy (NULL is ignored). Its memory comes from
 * xx_mem_alloc and is at most about 16 bytes per byte of the abcFile.
 *
 * Text syntax (close to RABCDAsm; every formatter and the resolver write
 * it):
 * - strings: "text" in double quotes, with \" \\ \n \r \t, \xNN for the
 *   other control characters and for bytes that are not valid UTF-8, and
 *   other UTF-8 kept as it is; string 0 is null.
 * - namespaces: the kind and the quoted name: PackageNamespace(""),
 *   PackageNamespace("flash.utils"), PackageInternalNs("foo"),
 *   Namespace("http://adobe.com/AS3/2006/builtin"), ProtectedNamespace("A"),
 *   ExplicitNamespace("x"), StaticProtectedNs("A"), PrivateNamespace("A").
 *   A private namespace whose name another private namespace of the file
 *   shares gets its pool index as a second argument,
 *   PrivateNamespace("", "#3"), since private namespaces are distinct by
 *   identity. Namespace 0 (any namespace) is *.
 * - namespace sets: [ns, ns, ...].
 * - multinames: QName(PackageNamespace(""), "trace"), QNameA(ns, "x"),
 *   RTQName("x"), RTQNameA("x"), RTQNameL(), RTQNameLA(),
 *   Multiname("x", [ns, ...]), MultinameA("x", [...]),
 *   MultinameL([...]), MultinameLA([...]) and
 *   TypeName(QName(PackageNamespace("__AS3__.vec"), "Vector")<QName(PackageNamespace(""), "int")>)
 *   (Vector.<int>). Multiname 0 and name string 0 (any name) are *.
 *   TypeNames nest at most 8 deep; deeper parameters print as multiname@N.
 * - compact names (method names, signatures and headers): the AS3 form the
 *   avmplus VM uses in stack traces: "trace", "flash.utils::ByteArray",
 *   "Vector.<int>", "*": a QName in a public (Namespace or PackageNamespace)
 *   namespace with an empty name is its name alone, any other QName is
 *   "uri::name" ("@" before the name of an attribute), with trailing API
 *   version marks (U+E000 to U+F8FF) dropped from the uri; other kinds use
 *   the multiname text above.
 * Index references that are out of range, and references to entries that do
 * not parse, keep the numeric form (multiname@N, string@N, ...).
 */
typedef struct xxbyte_avm2_names xxbyte_avm2_names;

XXBYTE_API xxbyte_status xxbyte_avm2_names_create(const uint8_t *data, size_t size, xxbyte_avm2_names **names);
XXBYTE_API void xxbyte_avm2_names_destroy(xxbyte_avm2_names *names);
/* The tables of the abcFile, as xxbyte_avm2_parse_abc returns them. */
XXBYTE_API const xxbyte_avm2_abc *xxbyte_avm2_names_abc(const xxbyte_avm2_names *names);
/* The method bodies (*count receives their number), and the index of the
 * body of method_info index (method_body_count when it has none). */
XXBYTE_API const xxbyte_avm2_method_body *xxbyte_avm2_names_bodies(const xxbyte_avm2_names *names, size_t *count);
XXBYTE_API uint32_t xxbyte_avm2_names_body_of(const xxbyte_avm2_names *names, uint32_t method);
/* Selects the method body whose exception table exception@N operands
 * (newcatch) refer to: a body index, or method_body_count (the default) for
 * none. Returns XXBYTE_STATUS_INVALID_ARGUMENT for other values. */
XXBYTE_API xxbyte_status xxbyte_avm2_names_select_body(xxbyte_avm2_names *names, uint32_t body);

/*
 * Formatters. Each writes NUL-terminated text of at most buffer_size - 1
 * characters and returns its length; text that does not fit ends in "...".
 * They return 0, with an empty buffer when buffer_size is not 0, for an
 * index that is out of range or whose entry does not parse.
 * - xxbyte_avm2_format_string: string index (0 gives null).
 * - xxbyte_avm2_format_namespace, _ns_set, _multiname: pool indexes.
 * - xxbyte_avm2_format_compact: a multiname as a compact name.
 * - xxbyte_avm2_format_value: an option or slot value (value index and
 *   kind byte): 42, "text", true, false, null, undefined, a namespace.
 * - xxbyte_avm2_method_name: the method's name, as the avmplus VM names
 *   methods (MethodInfo::getMethodName), with T the compact name of the
 *   class's instance_info name:
 *     instance initializer (constructor)       T             "Foo"
 *     class (static) initializer               T$cinit       "Foo$cinit"
 *     script initializer                       global$init
 *     instance method, getter, setter trait    T/name, T/get name, T/set name
 *     class (static) trait                     T$/name, T$/get name, ...
 *     script trait                             global/name
 *     function created by newfunction          Function/<debug name>
 *     method nothing refers to                 <debug name>
 *   where name is the trait's compact name ("pkg::name" for a non-public
 *   namespace, as the VM prints them) and <debug name> is the
 *   method_info's name string, or <anonymous> when it is empty. A method
 *   that several places refer to (which avmplus rejects) takes the first
 *   of: instances (initializer, then traits), classes, scripts, newfunction.
 *   A Function trait (kind 5, which avmplus rejects) names its method like
 *   a method trait.
 * - xxbyte_avm2_method_signature: "(int, String = \"a\", ...):void" with
 *   compact type names, * for untyped, the optional values, "..." for a
 *   rest parameter (NEED_REST) and "name:type" when the method has
 *   parameter names.
 */
XXBYTE_API size_t xxbyte_avm2_format_string(const xxbyte_avm2_names *names, uint32_t index, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_avm2_format_namespace(const xxbyte_avm2_names *names, uint32_t index, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_avm2_format_ns_set(const xxbyte_avm2_names *names, uint32_t index, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_avm2_format_multiname(const xxbyte_avm2_names *names, uint32_t index, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_avm2_format_compact(const xxbyte_avm2_names *names, uint32_t index, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_avm2_format_value(const xxbyte_avm2_names *names, uint32_t value_index, uint8_t value_kind, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_avm2_method_name(const xxbyte_avm2_names *names, uint32_t method, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_avm2_method_signature(const xxbyte_avm2_names *names, uint32_t method, char *buffer, size_t buffer_size);

/* What refers to a method_info (see xxbyte_avm2_method_name). */
typedef enum xxbyte_avm2_owner_role {
    XXBYTE_AVM2_OWNER_NONE = 0,       /* nothing */
    XXBYTE_AVM2_OWNER_INSTANCE_INIT,  /* index: class_info */
    XXBYTE_AVM2_OWNER_CLASS_INIT,     /* index: class_info */
    XXBYTE_AVM2_OWNER_SCRIPT_INIT,    /* index: script_info */
    XXBYTE_AVM2_OWNER_INSTANCE_TRAIT, /* index: class_info; trait set */
    XXBYTE_AVM2_OWNER_CLASS_TRAIT,    /* index: class_info; trait set */
    XXBYTE_AVM2_OWNER_SCRIPT_TRAIT,   /* index: script_info; trait set */
    XXBYTE_AVM2_OWNER_CLOSURE         /* index: the method_info whose body
                                         has the first newfunction of it */
} xxbyte_avm2_owner_role;

typedef struct xxbyte_avm2_method_owner {
    uint32_t role;       /* xxbyte_avm2_owner_role */
    uint32_t index;      /* see the roles */
    uint32_t trait_kind; /* XXBYTE_AVM2_TRAIT_METHOD, _GETTER, _SETTER or
                            _FUNCTION for the trait roles, else 0 */
    uint32_t trait_name; /* the trait's multiname index, else 0 */
    uint32_t disp_id;    /* the trait's disp_id, else 0 */
} xxbyte_avm2_method_owner;

/* Fills *owner for method_info index method (XXBYTE_STATUS_INVALID_ARGUMENT
 * when out of range). */
XXBYTE_API xxbyte_status xxbyte_avm2_names_owner(const xxbyte_avm2_names *names, uint32_t method, xxbyte_avm2_method_owner *owner);

/*
 * A resolver (xxbyte_resolver) whose context is an xxbyte_avm2_names. It
 * names every pool and table INDEX operand and keeps the numeric form of
 * registers, slots, dispatch ids, scope indexes and branch targets:
 *   string@N     "text"          (pushstring, dxns, debugfile, debug)
 *   int@N        -5              uint@N  4294967295
 *   double@N     0.5 (shortest round-trip form; double@0 is nan)
 *   float@N      1.5             float4@N  float4(1, 2, 3, 4)
 *   namespace@N  PackageNamespace("flash.utils")
 *   multiname@N  QName(PackageNamespace(""), "trace")
 *   method@N     method(4, "Function/<anonymous>")
 *   class@N      class(0, QName(PackageNamespace(""), "Foo"))
 *   exception@N  exception(0, QName(PackageNamespace(""), "Error")), with the
 *                handler's exc_type from the selected body
 * so that "46 05 01" reads "callproperty QName(PackageNamespace(""),
 * "trace"), 1". Resolved operands are cut to fit the formatter's 511
 * characters.
 */
XXBYTE_API size_t xxbyte_avm2_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/*
 * SWF containers. xxbyte_avm2_swf_open checks a SWF header ("FWS"
 * uncompressed, "CWS" zlib-compressed; "ZWS", LZMA, returns
 * XXBYTE_STATUS_UNSUPPORTED_FORMAT) and returns the whole uncompressed file in
 * *swf (header included, FileLength bytes as the header gives it), allocated
 * with xx_mem_alloc: free it with xx_mem_free. A CWS body is inflated with
 * xxbyte_inflate into at most XXBYTE_AVM2_SWF_LIMIT bytes and at most 1032
 * output bytes per input byte. *swf_size receives the size of *swf, which is
 * shorter than FileLength when the data ends early; *notice receives a
 * nonzero XXBYTE_AVM2_SWF_NOTICE_* mask then (and on a zlib checksum error
 * or a FileLength that disagrees), and the status is still OK.
 * XXBYTE_STATUS_INVALID_INSTRUCTION means data is not a SWF.
 */
#define XXBYTE_AVM2_SWF_LIMIT (256u * 1024u * 1024u)
#define XXBYTE_AVM2_SWF_NOTICE_TRUNCATED 0x1u /* data or zlib stream ends early */
#define XXBYTE_AVM2_SWF_NOTICE_CHECKSUM 0x2u  /* zlib stream corrupt after its last output */
#define XXBYTE_AVM2_SWF_NOTICE_LENGTH 0x4u    /* more data than FileLength */
XXBYTE_API xxbyte_status xxbyte_avm2_swf_open(const uint8_t *data, size_t size, uint8_t **swf, size_t *swf_size, uint32_t *notice);

/* A DoABC (tag 72: the payload is an abcFile) or DoABC2 (tag 82: u32 flags,
 * a NUL-terminated name, then the abcFile) tag, as avmplus's avmshell and
 * abcdump read them. Offsets are from the start of the uncompressed SWF. */
#define XXBYTE_AVM2_SWF_TAG_DO_ABC 72u
#define XXBYTE_AVM2_SWF_TAG_DO_ABC2 82u
#define XXBYTE_AVM2_DO_ABC_LAZY_INITIALIZE 0x1u
typedef struct xxbyte_avm2_swf_abc {
    uint32_t tag_code;    /* 72 or 82 */
    uint32_t tag_offset;  /* the RECORDHEADER */
    uint32_t flags;       /* DoABC2 flags, else 0 */
    uint32_t name_offset; /* DoABC2 name, else 0 */
    uint32_t name_length; /* without the NUL */
    uint32_t abc_offset;
    uint32_t abc_size;
} xxbyte_avm2_swf_abc;

/*
 * Walks the tags of the uncompressed SWF in swf[0..size) (from
 * xxbyte_avm2_swf_open) up to the End tag or the end of the data: *count
 * receives the number of DoABC and DoABC2 tags and the first
 * min(capacity, count) are stored. Returns XXBYTE_STATUS_TRUNCATED, with
 * the tags before it, when a tag runs past size, and
 * XXBYTE_STATUS_INVALID_INSTRUCTION for a DoABC2 tag without a name.
 */
XXBYTE_API xxbyte_status xxbyte_avm2_swf_abc_tags(const uint8_t *swf, size_t size, xxbyte_avm2_swf_abc *tags, size_t capacity, size_t *count);

/*
 * Container listing (xxbyte_disasm_file with XXBYTE_FAMILY_AVM2). The data is
 * a SWF file ("FWS" or "CWS"; "ZWS" prints a notice and returns
 * XXBYTE_STATUS_UNSUPPORTED_MODE) or a bare abcFile; anything else is
 * XXBYTE_STATUS_INVALID_INSTRUCTION. XXBYTE_MODE_AUTO decodes each abcFile in
 * its own version; another mode overrides it. The listing is
 *
 *   swf CWS version 10, file_length 1196, frame_count 1
 *   notice: ...                          (data shorter or longer than
 *                                         FileLength, corrupt zlib stream)
 *
 *   DoABC2 tag at 0x13, flags 0x1 (lazy initialize), name "frame1", abc at 0x24, 1156 bytes
 *   abc 46.16, 1156 bytes, 16 methods, 16 bodies, 1 classes, 1 scripts
 *
 *   method 3 demo::Foo/run(String, Number = 2.5, ...):String; instance method trait
 *       QName(PackageNamespace(""), "run") of class 0, disp_id 1; body 15,
 *       max_stack 10, local_count 8, scope_depth 0..3, code_length 29
 *   00000000  D0  getlocal_0
 *   ...
 *   00000016  2C 05  pushstring "caught"
 *   exception 0: from 0x0002 to 0x0008, target 0x000c, type QName(PackageNamespace(""), "Error"), name QName(PackageNamespace(""), "e")
 *
 * (the method line is one line): the SWF lines only for a SWF, then for
 * every DoABC/DoABC2 tag (or the bare abcFile) a summary line and, for every
 * method body in file order, a blank line, a header line (method index, its
 * xxbyte_avm2_method_name and xxbyte_avm2_method_signature, what refers to
 * it, the body's limits), the xxbyte_disasm_buf listing of its code with
 * xxbyte_avm2_resolve (addresses are code offsets) and one line per
 * exception_info. Methods without a body (native, interface) are not
 * listed. An abcFile that does not parse prints "error: abcFile: <status>"
 * and the listing goes on with the next tag; the first such status, or a
 * tag that runs past the data ("error: tags: ..."), is returned after the
 * whole listing.
 *
 * Limitations: LZMA (ZWS) SWF files and SWC archives are not read; the
 * avmplus VM's runtime names of its own specialised Vector classes
 * (Vector.<int> for builtin.abc's Vector$int) are not applied; float and
 * float4 pool values print as numbers without an ABC 47.16+ reference run.
 */

#ifdef __cplusplus
}
#endif

#endif
