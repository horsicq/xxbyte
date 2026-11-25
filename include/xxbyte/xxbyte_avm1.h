#ifndef XXBYTE_XXBYTE_AVM1_H
#define XXBYTE_XXBYTE_AVM1_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_AVM1
#error "the ActionScript 1/2 (AVM1) decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ActionScript 1/2 (AVM1): the action records of SWF files (DoAction,
 * DoInitAction, button and clip actions), as the SWF File Format
 * Specification (version 19) defines them and Ruffle (the ruffle-rs swf
 * crate, avm1/read.rs) reads them.
 *
 * Mode. XXBYTE_AVM1_MODE_DEFAULT (0) is the only mode: the encoding of an
 * action record does not depend on the SWF version (Ruffle's action reader
 * takes no version, and the players run the actions of any SWF version).
 * The version that introduced each action, as the specification lists it,
 * is available from xxbyte_avm1_action_version. Any other mode returns
 * XXBYTE_STATUS_UNSUPPORTED_MODE.
 *
 * Addresses are byte offsets in the action list, normally from the start of
 * the DoAction (or other) action data. Decoding needs no context;
 * xxbyte_decode_ex ignores it. Constant-pool references are resolved at
 * format time through xxbyte_avm1_resolve and an xxbyte_avm1_pool (below).
 */
#define XXBYTE_AVM1_MODE_DEFAULT 0u

/*
 * Mnemonic identifiers (xxbyte_instruction.id), in opcode order. The
 * mnemonic is the specification's action name without "Action", in lower
 * case ("push", "getvariable", "definefunction2", "previousframe"); the
 * enum name is it upper-cased. Two actions outside SWF19 are decoded too:
 * fscommand2 (0x2D, Flash Lite 1.1, no operand) and strictmode (0x89, one
 * UI8, emitted by the Flash 8+ compilers); Ruffle reads both as unknown
 * actions. Every other opcode is XXBYTE_AVM1_ID_UNKNOWN, mnemonic
 * "unknown": the players skip such an action (one byte, or 3 + length from
 * 0x80 on), so it decodes rather than fails. Valid ids are 1 to
 * XXBYTE_AVM1_ID_COUNT - 1.
 */
typedef enum xxbyte_avm1_id {
    XXBYTE_AVM1_ID_INVALID = 0,
    XXBYTE_AVM1_ID_END = 1,              /* 0x00 */
    XXBYTE_AVM1_ID_NEXTFRAME = 2,        /* 0x04 */
    XXBYTE_AVM1_ID_PREVIOUSFRAME = 3,    /* 0x05 */
    XXBYTE_AVM1_ID_PLAY = 4,             /* 0x06 */
    XXBYTE_AVM1_ID_STOP = 5,             /* 0x07 */
    XXBYTE_AVM1_ID_TOGGLEQUALITY = 6,    /* 0x08 */
    XXBYTE_AVM1_ID_STOPSOUNDS = 7,       /* 0x09 */
    XXBYTE_AVM1_ID_ADD = 8,              /* 0x0A */
    XXBYTE_AVM1_ID_SUBTRACT = 9,         /* 0x0B */
    XXBYTE_AVM1_ID_MULTIPLY = 10,        /* 0x0C */
    XXBYTE_AVM1_ID_DIVIDE = 11,          /* 0x0D */
    XXBYTE_AVM1_ID_EQUALS = 12,          /* 0x0E */
    XXBYTE_AVM1_ID_LESS = 13,            /* 0x0F */
    XXBYTE_AVM1_ID_AND = 14,             /* 0x10 */
    XXBYTE_AVM1_ID_OR = 15,              /* 0x11 */
    XXBYTE_AVM1_ID_NOT = 16,             /* 0x12 */
    XXBYTE_AVM1_ID_STRINGEQUALS = 17,    /* 0x13 */
    XXBYTE_AVM1_ID_STRINGLENGTH = 18,    /* 0x14 */
    XXBYTE_AVM1_ID_STRINGEXTRACT = 19,   /* 0x15 */
    XXBYTE_AVM1_ID_POP = 20,             /* 0x17 */
    XXBYTE_AVM1_ID_TOINTEGER = 21,       /* 0x18 */
    XXBYTE_AVM1_ID_GETVARIABLE = 22,     /* 0x1C */
    XXBYTE_AVM1_ID_SETVARIABLE = 23,     /* 0x1D */
    XXBYTE_AVM1_ID_SETTARGET2 = 24,      /* 0x20 */
    XXBYTE_AVM1_ID_STRINGADD = 25,       /* 0x21 */
    XXBYTE_AVM1_ID_GETPROPERTY = 26,     /* 0x22 */
    XXBYTE_AVM1_ID_SETPROPERTY = 27,     /* 0x23 */
    XXBYTE_AVM1_ID_CLONESPRITE = 28,     /* 0x24 */
    XXBYTE_AVM1_ID_REMOVESPRITE = 29,    /* 0x25 */
    XXBYTE_AVM1_ID_TRACE = 30,           /* 0x26 */
    XXBYTE_AVM1_ID_STARTDRAG = 31,       /* 0x27 */
    XXBYTE_AVM1_ID_ENDDRAG = 32,         /* 0x28 */
    XXBYTE_AVM1_ID_STRINGLESS = 33,      /* 0x29 */
    XXBYTE_AVM1_ID_THROW = 34,           /* 0x2A */
    XXBYTE_AVM1_ID_CASTOP = 35,          /* 0x2B */
    XXBYTE_AVM1_ID_IMPLEMENTSOP = 36,    /* 0x2C */
    XXBYTE_AVM1_ID_FSCOMMAND2 = 37,      /* 0x2D */
    XXBYTE_AVM1_ID_RANDOMNUMBER = 38,    /* 0x30 */
    XXBYTE_AVM1_ID_MBSTRINGLENGTH = 39,  /* 0x31 */
    XXBYTE_AVM1_ID_CHARTOASCII = 40,     /* 0x32 */
    XXBYTE_AVM1_ID_ASCIITOCHAR = 41,     /* 0x33 */
    XXBYTE_AVM1_ID_GETTIME = 42,         /* 0x34 */
    XXBYTE_AVM1_ID_MBSTRINGEXTRACT = 43, /* 0x35 */
    XXBYTE_AVM1_ID_MBCHARTOASCII = 44,   /* 0x36 */
    XXBYTE_AVM1_ID_MBASCIITOCHAR = 45,   /* 0x37 */
    XXBYTE_AVM1_ID_DELETE = 46,          /* 0x3A */
    XXBYTE_AVM1_ID_DELETE2 = 47,         /* 0x3B */
    XXBYTE_AVM1_ID_DEFINELOCAL = 48,     /* 0x3C */
    XXBYTE_AVM1_ID_CALLFUNCTION = 49,    /* 0x3D */
    XXBYTE_AVM1_ID_RETURN = 50,          /* 0x3E */
    XXBYTE_AVM1_ID_MODULO = 51,          /* 0x3F */
    XXBYTE_AVM1_ID_NEWOBJECT = 52,       /* 0x40 */
    XXBYTE_AVM1_ID_DEFINELOCAL2 = 53,    /* 0x41 */
    XXBYTE_AVM1_ID_INITARRAY = 54,       /* 0x42 */
    XXBYTE_AVM1_ID_INITOBJECT = 55,      /* 0x43 */
    XXBYTE_AVM1_ID_TYPEOF = 56,          /* 0x44 */
    XXBYTE_AVM1_ID_TARGETPATH = 57,      /* 0x45 */
    XXBYTE_AVM1_ID_ENUMERATE = 58,       /* 0x46 */
    XXBYTE_AVM1_ID_ADD2 = 59,            /* 0x47 */
    XXBYTE_AVM1_ID_LESS2 = 60,           /* 0x48 */
    XXBYTE_AVM1_ID_EQUALS2 = 61,         /* 0x49 */
    XXBYTE_AVM1_ID_TONUMBER = 62,        /* 0x4A */
    XXBYTE_AVM1_ID_TOSTRING = 63,        /* 0x4B */
    XXBYTE_AVM1_ID_PUSHDUPLICATE = 64,   /* 0x4C */
    XXBYTE_AVM1_ID_STACKSWAP = 65,       /* 0x4D */
    XXBYTE_AVM1_ID_GETMEMBER = 66,       /* 0x4E */
    XXBYTE_AVM1_ID_SETMEMBER = 67,       /* 0x4F */
    XXBYTE_AVM1_ID_INCREMENT = 68,       /* 0x50 */
    XXBYTE_AVM1_ID_DECREMENT = 69,       /* 0x51 */
    XXBYTE_AVM1_ID_CALLMETHOD = 70,      /* 0x52 */
    XXBYTE_AVM1_ID_NEWMETHOD = 71,       /* 0x53 */
    XXBYTE_AVM1_ID_INSTANCEOF = 72,      /* 0x54 */
    XXBYTE_AVM1_ID_ENUMERATE2 = 73,      /* 0x55 */
    XXBYTE_AVM1_ID_BITAND = 74,          /* 0x60 */
    XXBYTE_AVM1_ID_BITOR = 75,           /* 0x61 */
    XXBYTE_AVM1_ID_BITXOR = 76,          /* 0x62 */
    XXBYTE_AVM1_ID_BITLSHIFT = 77,       /* 0x63 */
    XXBYTE_AVM1_ID_BITRSHIFT = 78,       /* 0x64 */
    XXBYTE_AVM1_ID_BITURSHIFT = 79,      /* 0x65 */
    XXBYTE_AVM1_ID_STRICTEQUALS = 80,    /* 0x66 */
    XXBYTE_AVM1_ID_GREATER = 81,         /* 0x67 */
    XXBYTE_AVM1_ID_STRINGGREATER = 82,   /* 0x68 */
    XXBYTE_AVM1_ID_EXTENDS = 83,         /* 0x69 */
    XXBYTE_AVM1_ID_GOTOFRAME = 84,       /* 0x81 */
    XXBYTE_AVM1_ID_GETURL = 85,          /* 0x83 */
    XXBYTE_AVM1_ID_STOREREGISTER = 86,   /* 0x87 */
    XXBYTE_AVM1_ID_CONSTANTPOOL = 87,    /* 0x88 */
    XXBYTE_AVM1_ID_STRICTMODE = 88,      /* 0x89 */
    XXBYTE_AVM1_ID_WAITFORFRAME = 89,    /* 0x8A */
    XXBYTE_AVM1_ID_SETTARGET = 90,       /* 0x8B */
    XXBYTE_AVM1_ID_GOTOLABEL = 91,       /* 0x8C */
    XXBYTE_AVM1_ID_WAITFORFRAME2 = 92,   /* 0x8D */
    XXBYTE_AVM1_ID_DEFINEFUNCTION2 = 93, /* 0x8E */
    XXBYTE_AVM1_ID_TRY = 94,             /* 0x8F */
    XXBYTE_AVM1_ID_WITH = 95,            /* 0x94 */
    XXBYTE_AVM1_ID_PUSH = 96,            /* 0x96 */
    XXBYTE_AVM1_ID_JUMP = 97,            /* 0x99 */
    XXBYTE_AVM1_ID_GETURL2 = 98,         /* 0x9A */
    XXBYTE_AVM1_ID_DEFINEFUNCTION = 99,  /* 0x9B */
    XXBYTE_AVM1_ID_IF = 100,             /* 0x9D */
    XXBYTE_AVM1_ID_CALL = 101,           /* 0x9E */
    XXBYTE_AVM1_ID_GOTOFRAME2 = 102,     /* 0x9F */
    XXBYTE_AVM1_ID_UNKNOWN = 103,        /* any other opcode */
    XXBYTE_AVM1_ID_COUNT
} xxbyte_avm1_id;

/*
 * Encoding. An action is its opcode byte; from 0x80 on a UI16 length
 * follows and then length bytes of payload, so instruction.size is 1 below
 * 0x80 and 3 + length from 0x80 on, exactly as the players and Ruffle step
 * from one action to the next. instruction.opcode is the opcode byte.
 * - A payload shorter than its fields (a string without its NUL, a missing
 *   UI16, a push value cut by the end of the payload) is
 *   XXBYTE_STATUS_INVALID_INSTRUCTION; Ruffle reads such fields from the
 *   bytes after the action and then warns of a length mismatch. Payload
 *   bytes after the fields are ignored, as in Ruffle and the players.
 * - An action whose header or payload runs past code_size is
 *   XXBYTE_STATUS_TRUNCATED.
 * - definefunction, definefunction2, with and try are followed by their
 *   bodies (the function code, the with block, the try, catch and finally
 *   blocks). instruction.size covers the action alone; the bodies decode as
 *   the actions that follow, and their ends are BRANCH operands with
 *   XXBYTE_AVM1_OPERAND_FLAG_BLOCK_END: address + size + the body sizes.
 *   Execution continues there after the action (the players skip the
 *   bodies, which run when the function is called or the block entered).
 *   Ruffle starts a body right after the fields it read; the two agree
 *   unless a payload has bytes after its fields.
 *
 * Operands, in encoding order (sizes are encoded widths):
 * - gotoframe: IMMEDIATE frame (UI16, 0-based).
 * - geturl: STRING url, STRING target.
 * - storeregister: REGISTER (UI8).
 * - constantpool: IMMEDIATE declared count (UI16); the strings are the
 *   table. A constant pool keeps the strings that end within the payload,
 *   up to the declared count, as Ruffle does when the action is all the
 *   input it has (it skips a string it cannot read).
 * - strictmode: IMMEDIATE mode (UI8).
 * - waitforframe: IMMEDIATE frame (UI16), IMMEDIATE skip count (UI8).
 * - settarget, gotolabel: STRING.
 * - waitforframe2: IMMEDIATE skip count (UI8). The skip counts are numbers
 *   of actions, so they are not BRANCH operands.
 * - definefunction2: STRING name, IMMEDIATE parameter count (UI16),
 *   IMMEDIATE register count (UI8), IMMEDIATE flags (UI16,
 *   XXBYTE_AVM1_FUNCTION_*), BRANCH end of the body (the UI16 code size);
 *   the parameters are the table.
 * - try: IMMEDIATE flags (UI8, XXBYTE_AVM1_TRY_*), BRANCH end of the try
 *   block, BRANCH end of the catch block, BRANCH end of the finally block
 *   (each from its UI16 size; a block absent from the flags still occupies
 *   its size), then the catch variable: a REGISTER (UI8) with
 *   XXBYTE_AVM1_TRY_CATCH_IN_REGISTER, else a STRING. A try whose length
 *   is below 7 has no operands and no blocks, as in Ruffle.
 * - with: BRANCH end of the block (UI16 size).
 * - push: the values are the table (no operands).
 * - jump, if: BRANCH address + size + SI16 offset (relative to the next
 *   action); extra holds the raw UI16.
 * - geturl2: IMMEDIATE flags (UI8, XXBYTE_AVM1_GETURL2_*).
 * - definefunction: STRING name, IMMEDIATE parameter count (UI16), BRANCH
 *   end of the body; the parameter names are the table.
 * - gotoframe2: IMMEDIATE flags (UI8, XXBYTE_AVM1_GOTOFRAME2_*), then with
 *   XXBYTE_AVM1_GOTOFRAME2_SCENE_BIAS an IMMEDIATE scene bias (UI16).
 * - unknown: IMMEDIATE opcode (size 1), and from 0x80 on a BYTES operand
 *   with the payload (value 3, size length).
 * - Every other action has no operand; a payload it has (call, or a
 *   one-byte action never has one) is ignored.
 * A STRING operand is XXBYTE_OPERAND_BYTES with
 * XXBYTE_AVM1_OPERAND_FLAG_STRING: value is the offset of its first byte
 * from the instruction's first byte and size its length without the NUL.
 *
 * Tables (table_count entries from table_offset, read with
 * xxbyte_avm1_first_entry): the push values, the constant-pool strings and
 * the function parameters. Their INDEX entries (constant-pool references)
 * are passed to the resolver.
 *
 * Groups: jump JUMP|STOP; if CONDITIONAL; waitforframe and waitforframe2
 * CONDITIONAL (they skip their count of actions when the frame is not
 * loaded; there is no BRANCH operand); call, callfunction, callmethod,
 * newobject and newmethod CALL; return and end RETURN|STOP; throw
 * THROW|STOP.
 *
 * Text: the mnemonic, a space and the operands separated by ", ":
 *   "gotoframe 4", "geturl \"http://x\", \"_blank\"", "storeregister r1",
 *   "constantpool \"a\", \"b\"", "strictmode 1", "waitforframe 10, 2",
 *   "settarget \"/mc\"", "gotolabel \"start\"", "waitforframe2 1",
 *   "definefunction \"f\", [\"a\", \"b\"], 0x0034",
 *   "definefunction2 \"f\", 3, preload_this|suppress_super, [r1 \"a\",
 *   \"b\"], 0x0040" (name, register count, flags, parameters with their
 *   register unless it is 0, body end),
 *   "try 0x0020, catch \"e\" 0x0030, finally 0x0038" (the try end, then
 *   "catch VAR END" with XXBYTE_AVM1_TRY_CATCH and "finally END" with
 *   XXBYTE_AVM1_TRY_FINALLY, then "flags 0xNN" when reserved bits are set),
 *   "with 0x0012", "jump 0x0010", "if -0x0004",
 *   "geturl2 post|load_variables" (get, post or method3, load_target,
 *   load_variables, reserved bits as "0xNN"; "0" when none),
 *   "gotoframe2 play, 2" (play or stop, reserved bits as "|0xNN", then the
 *   scene bias when present), "unknown 0x01", "unknown 0xa5, length 3".
 * - Strings print in double quotes; bytes 0x20-0x7E print as themselves
 *   except '"' and '\\', which print as \" and \\, and the others as \n,
 *   \r, \t or \xNN (two uppercase hex digits). The bytes are SWF strings
 *   (locale-encoded before SWF 6, UTF-8 from SWF 6); they are not decoded.
 * - Push values: "string", 1.5f (float; xxbyte_fmt_float, ".0" added
 *   when the text has only digits, then "f": "2.0f", "nanf", "-inff"),
 *   null, undefined, r3 (register), true and false (any nonzero byte is
 *   true), 2.5 (double; xxbyte_fmt_double, ".0" added when the text has
 *   only digits: "2.0", "-0.0", "1e+16"), 42 (integer), constant@7
 *   (constant8), constant16@300 (constant16), and "invalid 10" for an
 *   unknown type byte, which Ruffle reads as a one-byte invalid value:
 *   "push \"x\", 1.5f, null, r3, true, 2.5, 42, constant@7".
 * - Function flags: preload_this, suppress_this, preload_arguments,
 *   suppress_arguments, preload_super, suppress_super, preload_root,
 *   preload_parent, preload_global in bit order, joined by '|', the other
 *   bits as "0xNNNN"; "0" when none.
 * - BRANCH operands print as "0x" and at least four lowercase hex digits,
 *   "-0x" when negative. The resolver is asked about every displayed
 *   BRANCH operand and every constant-pool INDEX entry.
 */

/* Family operand flags. */
#define XXBYTE_AVM1_OPERAND_FLAG_STRING 0x1000u    /* BYTES: a NUL-terminated string */
#define XXBYTE_AVM1_OPERAND_FLAG_BLOCK_END 0x2000u /* BRANCH: the end of a body or block, not a jump */

/* definefunction2 flags (UI16, Ruffle FunctionFlags). */
#define XXBYTE_AVM1_FUNCTION_PRELOAD_THIS 0x0001u
#define XXBYTE_AVM1_FUNCTION_SUPPRESS_THIS 0x0002u
#define XXBYTE_AVM1_FUNCTION_PRELOAD_ARGUMENTS 0x0004u
#define XXBYTE_AVM1_FUNCTION_SUPPRESS_ARGUMENTS 0x0008u
#define XXBYTE_AVM1_FUNCTION_PRELOAD_SUPER 0x0010u
#define XXBYTE_AVM1_FUNCTION_SUPPRESS_SUPER 0x0020u
#define XXBYTE_AVM1_FUNCTION_PRELOAD_ROOT 0x0040u
#define XXBYTE_AVM1_FUNCTION_PRELOAD_PARENT 0x0080u
#define XXBYTE_AVM1_FUNCTION_PRELOAD_GLOBAL 0x0100u

/* try flags (UI8). */
#define XXBYTE_AVM1_TRY_CATCH 0x01u
#define XXBYTE_AVM1_TRY_FINALLY 0x02u
#define XXBYTE_AVM1_TRY_CATCH_IN_REGISTER 0x04u

/*
 * geturl2 flags (UI8) as Flash Player and Ruffle read them; SWF19 lists the
 * fields in the opposite bit order. A method of 3 is invalid (Ruffle and
 * Flash treat it as none) and prints as "method3".
 */
#define XXBYTE_AVM1_GETURL2_METHOD_MASK 0x03u /* 0 none, 1 GET, 2 POST */
#define XXBYTE_AVM1_GETURL2_LOAD_TARGET 0x40u
#define XXBYTE_AVM1_GETURL2_LOAD_VARIABLES 0x80u

/* gotoframe2 flags (UI8). */
#define XXBYTE_AVM1_GOTOFRAME2_PLAY 0x01u
#define XXBYTE_AVM1_GOTOFRAME2_SCENE_BIAS 0x02u

/* Push value types (the type byte). */
#define XXBYTE_AVM1_PUSH_STRING 0u
#define XXBYTE_AVM1_PUSH_FLOAT 1u
#define XXBYTE_AVM1_PUSH_NULL 2u
#define XXBYTE_AVM1_PUSH_UNDEFINED 3u
#define XXBYTE_AVM1_PUSH_REGISTER 4u
#define XXBYTE_AVM1_PUSH_BOOLEAN 5u
#define XXBYTE_AVM1_PUSH_DOUBLE 6u
#define XXBYTE_AVM1_PUSH_INTEGER 7u
#define XXBYTE_AVM1_PUSH_CONSTANT8 8u
#define XXBYTE_AVM1_PUSH_CONSTANT16 9u

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_avm1_mnemonic(uint32_t id);

/* The id of opcode (XXBYTE_AVM1_ID_UNKNOWN for an undefined one). */
XXBYTE_API uint32_t xxbyte_avm1_opcode_id(uint8_t opcode);

/*
 * The SWF version that introduced opcode according to SWF19 (3 for End and
 * the SWF 3 actions, then 4, 5, 6 or 7), or 0 for fscommand2, strictmode
 * and undefined opcodes.
 */
XXBYTE_API uint32_t xxbyte_avm1_action_version(uint8_t opcode);

/*
 * One table entry of a push, constantpool, definefunction or
 * definefunction2 instruction.
 * - offset is the entry's first byte from the instruction's first byte and
 *   size its encoded size (for a push value, with its type byte).
 * - type is the push type byte (XXBYTE_AVM1_PUSH_*), XXBYTE_AVM1_PUSH_STRING
 *   for the strings of the other tables.
 * - value: a push string, constant-pool string or parameter name is a
 *   STRING (BYTES, offset from the instruction's first byte); float FLOAT
 *   size 4; double FLOAT size 8 (the IEEE bits, after swapping the two
 *   little-endian 32-bit halves the SWF stores high half first); null and
 *   undefined IMMEDIATE 0 size 0; register REGISTER size 1; boolean
 *   IMMEDIATE size 1 holding the raw byte; integer SIGNED IMMEDIATE size 4;
 *   constant8 and constant16 INDEX XXBYTE_INDEX_CONSTANT size 1 or 2; an
 *   invalid type IMMEDIATE size 0 holding the type byte. value.extra is the
 *   type byte.
 * - reg: a definefunction2 parameter's REGISTER (size 1; 0 means the
 *   parameter is not in a register); XXBYTE_OPERAND_NONE otherwise.
 */
typedef struct xxbyte_avm1_entry {
    uint32_t index;
    uint32_t offset;
    uint32_t size;
    uint32_t type;
    xxbyte_operand value;
    xxbyte_operand reg;
} xxbyte_avm1_entry;

/*
 * Reads the first entry of instruction's table, then the one after *entry.
 * Return XXBYTE_STATUS_OK, XXBYTE_STATUS_END_OF_INPUT after the last
 * entry, and XXBYTE_STATUS_INVALID_ARGUMENT for an instruction that is not
 * a decoded AVM1 instruction with a table (its code must still be valid).
 * xxbyte_avm1_entry_at walks to entry index.
 */
XXBYTE_API xxbyte_status xxbyte_avm1_first_entry(const xxbyte_instruction *instruction, xxbyte_avm1_entry *entry);
XXBYTE_API xxbyte_status xxbyte_avm1_next_entry(const xxbyte_instruction *instruction, xxbyte_avm1_entry *entry);
XXBYTE_API xxbyte_status xxbyte_avm1_entry_at(const xxbyte_instruction *instruction, uint32_t index, xxbyte_avm1_entry *entry);

/*
 * Constant pool for formatting. xxbyte_avm1_pool_set records the strings of
 * a decoded constantpool instruction (it borrows instruction->code, which
 * must outlive the pool's use, and allocates an offset table with
 * xx_mem_alloc); a later set replaces them. Initialise with
 * xxbyte_avm1_pool_init and release with xxbyte_avm1_pool_free.
 * xxbyte_avm1_pool_string returns entry index (text points into the
 * constantpool action) or XXBYTE_STATUS_INVALID_ARGUMENT when the pool has
 * no such entry.
 *
 * The players keep the constant pool of the last constantpool action run
 * (a function keeps the one current when it was defined); a constant
 * reference with no such entry pushes undefined.
 */
typedef struct xxbyte_avm1_pool {
    const uint8_t *code; /* the constantpool action */
    uint32_t count;
    uint32_t capacity;
    uint32_t *offsets; /* count string offsets from code */
    uint32_t *lengths; /* count string lengths */
} xxbyte_avm1_pool;

XXBYTE_API void xxbyte_avm1_pool_init(xxbyte_avm1_pool *pool);
XXBYTE_API xxbyte_status xxbyte_avm1_pool_set(xxbyte_avm1_pool *pool, const xxbyte_instruction *constantpool);
XXBYTE_API void xxbyte_avm1_pool_clear(xxbyte_avm1_pool *pool); /* no strings, keeps the storage */
XXBYTE_API void xxbyte_avm1_pool_free(xxbyte_avm1_pool *pool);
XXBYTE_API xxbyte_status xxbyte_avm1_pool_string(const xxbyte_avm1_pool *pool, uint32_t index, const uint8_t **text, uint32_t *length);

/*
 * An xxbyte_resolver whose context is a const xxbyte_avm1_pool * (or NULL):
 * a constant-pool reference the pool has prints as its quoted string
 * (escaped as above; a string too long for the buffer ends in '...' before
 * the closing quote), anything else keeps its numeric form.
 */
XXBYTE_API size_t xxbyte_avm1_resolve(void *pool, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

struct xx_buf_s; /* xx_buf_t of xxfclib (xxbyte_xxfc.h) */

/*
 * Lists an action list like xxbyte_disasm_buf (same columns, "db" lines
 * where nothing decodes) with constant-pool references resolved: the pool
 * starts empty and every constantpool action replaces it as the listing
 * reaches it (in address order, ignoring control flow).
 */
XXBYTE_API xxbyte_status xxbyte_avm1_disasm_actions(const uint8_t *code, size_t code_size, uint64_t address, uint32_t listing_flags, struct xx_buf_s *out);

/*
 * SWF container.
 *
 * xxbyte_avm1_swf_open reads the SWF header: "FWS" (uncompressed) or "CWS"
 * (zlib, SWF 6+; decompressed into memory from xx_mem_alloc, up to the
 * uncompressed length the header gives, keeping what decompresses when the
 * stream is corrupt or short, like Ruffle; at most 512 MiB). "ZWS" (LZMA)
 * returns XXBYTE_STATUS_UNSUPPORTED_FORMAT. The data is limited to the
 * header's file length, as in Flash Player and Ruffle. Version 0 and a
 * file length below 8 are XXBYTE_STATUS_INVALID_INSTRUCTION, a header cut
 * short (signature, length, frame RECT, rate, count) XXBYTE_STATUS_TRUNCATED.
 * swf->data is the uncompressed file (with its 8-byte header, so offsets
 * are the offsets of an uncompressed copy of the file); it borrows the
 * input for FWS. Release with xxbyte_avm1_swf_close.
 */
typedef struct xxbyte_avm1_swf {
    uint8_t signature; /* 'F', 'C' or 'Z' */
    uint8_t version;
    uint16_t frame_rate;  /* 8.8 fixed point */
    uint16_t frame_count; /* as the header gives it */
    uint32_t file_length; /* uncompressed length from the header */
    uint32_t tags_offset; /* the first tag */
    const uint8_t *data;
    size_t size;
    uint8_t *owned; /* allocated data, or NULL */
} xxbyte_avm1_swf;

XXBYTE_API xxbyte_status xxbyte_avm1_swf_open(const uint8_t *data, size_t size, xxbyte_avm1_swf *swf);
XXBYTE_API void xxbyte_avm1_swf_close(xxbyte_avm1_swf *swf);

/* Where an action list comes from. */
typedef enum xxbyte_avm1_block_kind {
    XXBYTE_AVM1_BLOCK_DO_ACTION = 0,      /* DoAction (12) */
    XXBYTE_AVM1_BLOCK_DO_INIT_ACTION = 1, /* DoInitAction (59): id = sprite id */
    XXBYTE_AVM1_BLOCK_BUTTON = 2,         /* DefineButton (7): id = button id, conditions = 0x0008 */
    XXBYTE_AVM1_BLOCK_BUTTON2 = 3,        /* DefineButton2 (34) BUTTONCONDACTION */
    XXBYTE_AVM1_BLOCK_CLIP = 4            /* PlaceObject2/3/4 (26/70/94) CLIPACTIONRECORD */
} xxbyte_avm1_block_kind;

/*
 * One action list. Offsets are in swf->data.
 * - frame: the 1-based frame of the timeline holding the tag (the main
 *   timeline, or the sprite when in_sprite): ShowFrame tags before it + 1.
 * - in_sprite, sprite: the DefineSprite (39) holding the tag, if any.
 * - id: the DoInitAction sprite id, the button id, or the PlaceObject depth.
 * - character, has_character: the PlaceObject character id.
 * - name_offset, name_length: the PlaceObject instance name (offset 0 when
 *   none).
 * - conditions: the button condition flags (UI16; bits 9-15 are the key
 *   code) or the clip event flags (UI16 before SWF 6, UI32 from SWF 6, the
 *   Ruffle ClipEventFlag bits: load 0, enter_frame, unload, mouse_move,
 *   mouse_down, mouse_up, key_down, key_up, data, initialize, press,
 *   release, release_outside, roll_over, roll_out, drag_over, drag_out,
 *   key_press, construct 18).
 * - key_code: the clip KeyPress key code (with key_press), else 0.
 */
typedef struct xxbyte_avm1_block {
    uint32_t kind; /* xxbyte_avm1_block_kind */
    uint32_t tag_code;
    uint32_t tag_offset; /* the tag header */
    uint32_t frame;
    uint32_t in_sprite;
    uint32_t sprite;
    uint32_t id;
    uint32_t has_character;
    uint32_t character;
    uint32_t name_offset;
    uint32_t name_length;
    uint32_t conditions;
    uint32_t key_code;
    uint32_t offset; /* the action data */
    uint32_t size;
} xxbyte_avm1_block;

/* Return XXBYTE_STATUS_OK to continue the walk; anything else stops it and
 * is returned by xxbyte_avm1_swf_walk. */
typedef xxbyte_status (*xxbyte_avm1_block_callback)(void *context, const xxbyte_avm1_swf *swf, const xxbyte_avm1_block *block);

/*
 * Walks the tags of an opened SWF (and of every DefineSprite, nested up to
 * 16 deep) and calls callback for every action list, in file order:
 * DoAction, DoInitAction, the actions of DefineButton (after its button
 * records; condition over_down_to_over_up, as Ruffle gives it) and every
 * BUTTONCONDACTION of DefineButton2 (from its ActionOffset; a CondActionSize
 * of 0 ends the list and takes the rest of the tag), and every
 * CLIPACTIONRECORD of PlaceObject2, PlaceObject3 and PlaceObject4 (read
 * after the place fields, filters and blend mode as Ruffle reads them; the
 * list ends at event flags of zero, counting only the known bits, or at the
 * end of the tag). The tag list ends at an End tag or the end of the data.
 * A malformed tag yields the blocks read before the fault and the walk
 * goes on with the next tag; a tag that runs past the end of the data
 * stops it with XXBYTE_STATUS_TRUNCATED.
 */
XXBYTE_API xxbyte_status xxbyte_avm1_swf_walk(const xxbyte_avm1_swf *swf, xxbyte_avm1_block_callback callback, void *context);

/*
 * xxbyte_disasm_file (XXBYTE_FAMILY_AVM1, mode XXBYTE_MODE_AUTO or
 * XXBYTE_AVM1_MODE_DEFAULT) lists an SWF file: first
 *   "; SWF version 8, FWS, length 1234, 3 frames"
 * then, for every action list of xxbyte_avm1_swf_walk, an empty line, a
 * header line and the xxbyte_avm1_disasm_actions listing of the list at
 * address 0 (its own constant pool, starting empty). Header lines:
 *   "; frame 1: DoAction (0x00000015, 42 bytes)"
 *   "; sprite 3 frame 2: DoAction (...)"
 *   "; frame 1: DoInitAction sprite 3 (...)"
 *   "; frame 1: DefineButton 4 on over_down_to_over_up (...)"
 *   "; frame 1: DefineButton2 4 on over_up_to_over_down|key_press 13 (...)"
 *   "; frame 1: PlaceObject2 depth 1 character 2 name \"mc\" on load|key_press 65 (...)"
 * (the offset and size of the action data in the uncompressed file). The
 * button conditions are idle_to_over_up, over_up_to_idle,
 * over_up_to_over_down, over_down_to_over_up, over_down_to_out_down,
 * out_down_to_over_down, out_down_to_idle, idle_to_over_down,
 * over_down_to_idle in bit order, then "key_press N" for bits 9-15; the
 * clip events are named as in xxbyte_avm1_block, key_press with its key
 * code; unknown bits print as "0x%08x" and no condition as "none". ZWS
 * files print "; SWF version N, ZWS (LZMA) is not supported" and return
 * XXBYTE_STATUS_UNSUPPORTED_FORMAT. DoABC tags (ActionScript 3) are not
 * listed; use XXBYTE_FAMILY_AVM2 for them.
 */

#ifdef __cplusplus
}
#endif

#endif
