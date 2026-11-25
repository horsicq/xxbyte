#ifndef XXBYTE_XXBYTE_LUA_H
#define XXBYTE_XXBYTE_LUA_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_LUA
#error "the Lua 5.x decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PUC-Rio Lua decode mode: the release series whose lopcodes.h applies,
 * XXBYTE_LUA_MODE(5, 0) to XXBYTE_LUA_MODE(5, 5), optionally combined
 * with
 *   - XXBYTE_LUA_MODE_BIG_ENDIAN: instruction words are big-endian (the
 *     byte order of the chunk; the default is little-endian);
 *   - XXBYTE_LUA_MODE_WIDE: 8-byte instructions, only with 5.0, whose
 *     Instruction is an unsigned long and so 64 bits wide on LP64 builds
 *     (the upper 32 bits must be zero).
 * Any other mode or bit is XXBYTE_STATUS_UNSUPPORTED_MODE. One opcode
 * table per series covers all of its releases (5.0.3, 5.1.5, 5.2.4,
 * 5.3.6, 5.4.0 to 5.4.9, 5.5.0 and 5.5.1 share their series' encoding).
 * The text follows the newest printer of each series; in 5.4 that is the
 * luac of 5.4.4 to 5.4.9. luac 5.4.0 to 5.4.3 printed FORPREP as
 * "; to N" with N the pc of its FORLOOP (xxbyte: "; exit to N", the pc
 * after the FORLOOP, as 5.4.4+) and RETURN and TAILCALL without the "k"
 * suffix; the decoded fields, targets and groups are the same.
 *
 * An instruction is one 4-byte word (8 with XXBYTE_LUA_MODE_WIDE), except
 * a 5.1 SETLIST with C == 0, which takes the following word as its block
 * number (luac shows it as "; N" and skips it) and is 8 bytes.
 * EXTRAARG, which supplies the extra argument of the preceding LOADKX,
 * SETLIST or NEWTABLE, decodes as an instruction of its own (group
 * PAYLOAD), as the VM fetches it. luac lists it as such, with one
 * exception: for a 5.2 or 5.3 SETLIST with C == 0, luac prints the
 * following EXTRAARG word as a raw int in a "; N" comment on the SETLIST
 * line and skips its line, while xxbyte prints SETLIST without the
 * comment (4 bytes) and lists that word as EXTRAARG (-1 - Ax). The text
 * of an instruction depends only on its own bytes. Undefined opcodes are
 * XXBYTE_STATUS_INVALID_INSTRUCTION; fields an opcode does not use are
 * ignored, as luac and the VM ignore them.
 *
 * Addresses: pass the byte offset of the instruction within the
 * function's code array (pc * 4, or pc * 8 for WIDE). BRANCH operands hold
 * the absolute byte target, for example address + 4 * (1 + sBx) for JMP
 * and 5.4 FORLOOP address + 4 * (1 - Bx).
 *
 * Text follows luac -l: the opcode name and its numeric columns, separated
 * by single spaces, exactly as that series' luac prints them:
 *   - 5.0 prints every field of the format (A B C, A Bx or A sBx), RK
 *     operands raw (constant x - 250 shows as x);
 *   - 5.1 to 5.3 print the fields the opcode uses; constants show as
 *     -1 - index (LOADK 0 -1 loads constant 0), and any B or C field with
 *     bit 8 set shows as -1 - (field & 255) even when it is not an RK
 *     operand (a 5.1 SETLIST 0 50 300 prints as SETLIST 0 50 -45);
 *     EXTRAARG shows -1 - Ax;
 *   - 5.4 and 5.5 print the columns of luac's PrintCode, with the k flag
 *     as a column of its own (EQ 0 1 1) or as a "k" suffix (SETFIELD 0 1
 *     2k), signed fields (sBx, sJ, sB, sC) as signed numbers.
 * Branches add luac's comment " ; to N" (" ; exit to N" for 5.4+
 * FORPREP), N being luac's 1-based pc of the target, target / 4 + 1. A
 * 5.1 SETLIST with C == 0 adds " ; N" with its block number. Other luac
 * comments (constant values, upvalue names, "1 in 2 out", metamethod
 * names) are left out, since they need the function's constants. With a
 * resolver, the resolved names of the instruction's INDEX operands follow
 * as a comment, separated by spaces in operand order (" ; _ENV "print""
 * when the resolver supplies _ENV and "print"; operands it declines are
 * left out), and a resolved BRANCH operand replaces N after "to". The
 * ready-made resolver xxbyte_lua_resolve (below) instead gives every
 * comment luac -l -l prints, from the chunk's constants and names.
 *
 *   00 00 80 00 (5.1)               MOVE 0 1
 *   01 40 00 00 (5.1)               LOADK 0 -2      (constant 1)
 *   0B 00 00 01 (5.4)               GETTABUP 0 0 1  (upvalue 0, constant 1)
 *   B8 00 00 80 (5.4, address 8)    JMP 2 ; to 6    (target 20, pc 6)
 *   81 00 FF 7F (5.4)               LOADI 1 -1
 *   12 80 01 02 (5.4)               SETFIELD 0 1 2k (k: C is constant 2)
 *
 * Operands, one per printed column in column order:
 *   - XXBYTE_OPERAND_REGISTER: register fields, and RK operands that name
 *     a register;
 *   - XXBYTE_OPERAND_INDEX: XXBYTE_INDEX_CONSTANT (LOADK Bx, GETGLOBAL,
 *     5.4+ K fields, RK operands that name a constant: value is the
 *     0-based constant index), XXBYTE_INDEX_UPVALUE (upvalue indexes,
 *     including the A of SETTABUP) and XXBYTE_INDEX_FUNCTION (CLOSURE Bx,
 *     the index into the function's prototype list);
 *   - XXBYTE_OPERAND_IMMEDIATE: counts, sizes, flags and the k bit;
 *     signed fields (5.4+ LOADI, LOADF, ADDI, SHRI, SHLI, MMBINI, EQI ...)
 *     carry XXBYTE_OPERAND_FLAG_SIGNED. 5.0 prints fields its opcode does
 *     not use; they are IMMEDIATE operands too;
 *   - XXBYTE_OPERAND_BRANCH: jump targets.
 * operand.extra is the raw field value as encoded (for example the RK
 * field with its constant bit, or the unsigned excess-K sBx) and
 * operand.size the field width in bits rounded up to bytes. A few fields
 * that luac does not print are operands at the end of the list but
 * print nothing: the k bit of 5.4 NEWTABLE and SETLIST and the C (float
 * operand) of 5.4+ EQI, LTI, LEI, GTI and GEI.
 *
 * Instructions that skip the next instruction (the comparisons and tests,
 * 5.0/5.1 TFORLOOP, LOADBOOL with C != 0, 5.4+ LFALSESKIP) also carry an
 * IMPLICIT BRANCH operand to address + 8 (+ 16 for WIDE), which prints
 * nothing and is not passed to a resolver.
 *
 * Groups: JMP and the unconditional loop jumps (5.0 TFORPREP, 5.1-5.3
 * FORPREP, 5.4+ TFORPREP), LOADBOOL with C != 0 and LFALSESKIP:
 * JUMP|STOP; comparisons, tests, FORLOOP, TFORLOOP (5.2+) and 5.4+
 * FORPREP: CONDITIONAL; 5.0/5.1 TFORLOOP: CALL|CONDITIONAL; CALL and
 * TFORCALL: CALL; TAILCALL: CALL|RETURN before 5.4 (a C callee continues
 * at the RETURN that follows), CALL|RETURN|STOP from 5.4; RETURN,
 * RETURN0 and RETURN1: RETURN|STOP; EXTRAARG: PAYLOAD; 5.5 ERRNNIL:
 * THROW (it raises an error only when R[A] is not nil).
 */
#define XXBYTE_LUA_MODE(major, minor) ((((uint32_t)(major) & 0xFFu) << 8) | ((uint32_t)(minor) & 0xFFu))
#define XXBYTE_LUA_MODE_MAJOR(mode) (((uint32_t)(mode) >> 8) & 0xFFu)
#define XXBYTE_LUA_MODE_MINOR(mode) ((uint32_t)(mode) & 0xFFu)
#define XXBYTE_LUA_MODE_BIG_ENDIAN 0x10000u
#define XXBYTE_LUA_MODE_WIDE 0x20000u
#define XXBYTE_LUA_MODE_DEFAULT XXBYTE_LUA_MODE(5, 5)

/*
 * Mnemonic identifiers (xxbyte_instruction.id), one per opcode name of all
 * supported versions in order of first appearance (5.0, then the names
 * 5.1 to 5.5 added), so an opcode keeps its id across versions.
 */
typedef enum xxbyte_lua_id {
    XXBYTE_LUA_ID_INVALID = 0,
    XXBYTE_LUA_ID_MOVE = 1,
    XXBYTE_LUA_ID_LOADK = 2,
    XXBYTE_LUA_ID_LOADBOOL = 3,
    XXBYTE_LUA_ID_LOADNIL = 4,
    XXBYTE_LUA_ID_GETUPVAL = 5,
    XXBYTE_LUA_ID_GETGLOBAL = 6,
    XXBYTE_LUA_ID_GETTABLE = 7,
    XXBYTE_LUA_ID_SETGLOBAL = 8,
    XXBYTE_LUA_ID_SETUPVAL = 9,
    XXBYTE_LUA_ID_SETTABLE = 10,
    XXBYTE_LUA_ID_NEWTABLE = 11,
    XXBYTE_LUA_ID_SELF = 12,
    XXBYTE_LUA_ID_ADD = 13,
    XXBYTE_LUA_ID_SUB = 14,
    XXBYTE_LUA_ID_MUL = 15,
    XXBYTE_LUA_ID_DIV = 16,
    XXBYTE_LUA_ID_POW = 17,
    XXBYTE_LUA_ID_UNM = 18,
    XXBYTE_LUA_ID_NOT = 19,
    XXBYTE_LUA_ID_CONCAT = 20,
    XXBYTE_LUA_ID_JMP = 21,
    XXBYTE_LUA_ID_EQ = 22,
    XXBYTE_LUA_ID_LT = 23,
    XXBYTE_LUA_ID_LE = 24,
    XXBYTE_LUA_ID_TEST = 25,
    XXBYTE_LUA_ID_CALL = 26,
    XXBYTE_LUA_ID_TAILCALL = 27,
    XXBYTE_LUA_ID_RETURN = 28,
    XXBYTE_LUA_ID_FORLOOP = 29,
    XXBYTE_LUA_ID_TFORLOOP = 30,
    XXBYTE_LUA_ID_TFORPREP = 31,
    XXBYTE_LUA_ID_SETLIST = 32,
    XXBYTE_LUA_ID_SETLISTO = 33,
    XXBYTE_LUA_ID_CLOSE = 34,
    XXBYTE_LUA_ID_CLOSURE = 35,
    XXBYTE_LUA_ID_MOD = 36,
    XXBYTE_LUA_ID_LEN = 37,
    XXBYTE_LUA_ID_TESTSET = 38,
    XXBYTE_LUA_ID_FORPREP = 39,
    XXBYTE_LUA_ID_VARARG = 40,
    XXBYTE_LUA_ID_LOADKX = 41,
    XXBYTE_LUA_ID_GETTABUP = 42,
    XXBYTE_LUA_ID_SETTABUP = 43,
    XXBYTE_LUA_ID_TFORCALL = 44,
    XXBYTE_LUA_ID_EXTRAARG = 45,
    XXBYTE_LUA_ID_IDIV = 46,
    XXBYTE_LUA_ID_BAND = 47,
    XXBYTE_LUA_ID_BOR = 48,
    XXBYTE_LUA_ID_BXOR = 49,
    XXBYTE_LUA_ID_SHL = 50,
    XXBYTE_LUA_ID_SHR = 51,
    XXBYTE_LUA_ID_BNOT = 52,
    XXBYTE_LUA_ID_LOADI = 53,
    XXBYTE_LUA_ID_LOADF = 54,
    XXBYTE_LUA_ID_LOADFALSE = 55,
    XXBYTE_LUA_ID_LFALSESKIP = 56,
    XXBYTE_LUA_ID_LOADTRUE = 57,
    XXBYTE_LUA_ID_GETI = 58,
    XXBYTE_LUA_ID_GETFIELD = 59,
    XXBYTE_LUA_ID_SETI = 60,
    XXBYTE_LUA_ID_SETFIELD = 61,
    XXBYTE_LUA_ID_ADDI = 62,
    XXBYTE_LUA_ID_ADDK = 63,
    XXBYTE_LUA_ID_SUBK = 64,
    XXBYTE_LUA_ID_MULK = 65,
    XXBYTE_LUA_ID_MODK = 66,
    XXBYTE_LUA_ID_POWK = 67,
    XXBYTE_LUA_ID_DIVK = 68,
    XXBYTE_LUA_ID_IDIVK = 69,
    XXBYTE_LUA_ID_BANDK = 70,
    XXBYTE_LUA_ID_BORK = 71,
    XXBYTE_LUA_ID_BXORK = 72,
    XXBYTE_LUA_ID_SHRI = 73,
    XXBYTE_LUA_ID_SHLI = 74,
    XXBYTE_LUA_ID_MMBIN = 75,
    XXBYTE_LUA_ID_MMBINI = 76,
    XXBYTE_LUA_ID_MMBINK = 77,
    XXBYTE_LUA_ID_TBC = 78,
    XXBYTE_LUA_ID_EQK = 79,
    XXBYTE_LUA_ID_EQI = 80,
    XXBYTE_LUA_ID_LTI = 81,
    XXBYTE_LUA_ID_LEI = 82,
    XXBYTE_LUA_ID_GTI = 83,
    XXBYTE_LUA_ID_GEI = 84,
    XXBYTE_LUA_ID_RETURN0 = 85,
    XXBYTE_LUA_ID_RETURN1 = 86,
    XXBYTE_LUA_ID_VARARGPREP = 87,
    XXBYTE_LUA_ID_GETVARG = 88,
    XXBYTE_LUA_ID_ERRNNIL = 89,
    XXBYTE_LUA_ID_COUNT = 90 /* one past the last id */
} xxbyte_lua_id;

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_lua_mnemonic(uint32_t id);

/* The name of opcode in mode (flags ignored), or NULL when the opcode is
 * undefined there or the mode is unsupported. */
XXBYTE_API const char *xxbyte_lua_opname(uint32_t mode, uint32_t opcode);

/* A parsed precompiled-chunk header. */
typedef struct xxbyte_lua_header {
    uint32_t mode;            /* decode mode for the chunk's instructions,
                                 with BIG_ENDIAN and WIDE as needed, or 0
                                 when xxbyte cannot decode them (another
                                 instruction size, or 5.0 field widths
                                 other than 6/8/9/9) */
    uint32_t header_size;     /* offset of the main function from data[0]
                                 (after the 5.3+ upvalue count byte) */
    uint8_t version;          /* version byte, 0x50 to 0x55 */
    uint8_t format;           /* format byte (5.1+), 0 = official */
    uint8_t big_endian;       /* nonzero for a big-endian chunk */
    uint8_t int_size;         /* sizeof(int); 0 for 5.4 */
    uint8_t size_t_size;      /* sizeof(size_t); 0 for 5.4 and 5.5 */
    uint8_t instruction_size; /* sizeof(Instruction) */
    uint8_t integer_size;     /* sizeof(lua_Integer), 5.3+; else 0 */
    uint8_t number_size;      /* sizeof(lua_Number) */
    uint8_t number_integral;  /* lua_Number is an integer type (5.0 to 5.2;
                                 5.0 tells it from the test number) */
    uint8_t main_upvalues;    /* 5.3+: upvalues of the main closure */
    uint8_t op_bits;          /* 5.0 only: SIZE_OP, SIZE_A, SIZE_B and */
    uint8_t a_bits;           /* SIZE_C from the header; 0 otherwise */
    uint8_t b_bits;
    uint8_t c_bits;
} xxbyte_lua_header;

/*
 * Parses the header of the precompiled chunk at data[0] ("\x1bLua", the
 * version byte, then the series' format, size and test-value fields).
 * Returns XXBYTE_STATUS_TRUNCATED when size is too small,
 * XXBYTE_STATUS_INVALID_INSTRUCTION for a bad signature, LUAC_DATA tail,
 * endianness byte, type size or test value (sizes must be 2, 4 or 8 for
 * int and size_t, 4 or 8 for Instruction, lua_Integer and lua_Number),
 * and XXBYTE_STATUS_UNSUPPORTED_MODE for a version byte other than 0x50
 * to 0x55 or a nonzero (unofficial) format byte. The byte order comes
 * from the endianness byte (5.0 to 5.2) or the test integer (5.3+). On
 * failure *header is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_lua_parse_header(const uint8_t *data, size_t size, xxbyte_lua_header *header);

#define XXBYTE_LUA_NO_PARENT 0xFFFFFFFFu
/* Deepest prototype nesting xxbyte_lua_find_functions accepts (the main
 * function is depth 0). */
#define XXBYTE_LUA_MAX_DEPTH 250u

/* One function prototype of a chunk. Offsets are from data[0]. */
typedef struct xxbyte_lua_function {
    uint32_t index;  /* pre-order position, 0 = main function */
    uint32_t parent; /* index of the enclosing function, or
                        XXBYTE_LUA_NO_PARENT */
    uint32_t child;  /* position in the parent's prototype list
                        (the Bx of the CLOSURE creating it) */
    uint32_t depth;  /* nesting depth, 0 for the main function */
    uint32_t line_defined;
    uint32_t last_line_defined; /* 0 for 5.0, which does not record it */
    uint8_t num_params;
    uint8_t is_vararg; /* the vararg byte (5.5: flag byte) */
    uint8_t max_stack_size;
    uint8_t reserved;
    uint32_t code_count;  /* instructions */
    uint64_t code_offset; /* first instruction */
    uint32_t constant_count;
    uint64_t constants_offset;     /* first constant record */
    uint32_t upvalue_count;        /* 5.0/5.1: nups; 5.2+: upvalue records */
    uint32_t upvalue_name_count;   /* upvalue names in the debug data */
    uint64_t upvalue_names_offset; /* first upvalue name record */
    uint32_t prototype_count;
    uint64_t offset;     /* start of the function record */
    uint64_t end_offset; /* one past the function record */
    /* Debug and upvalue records; counts are 0 in stripped chunks. */
    uint64_t source_offset;      /* the function's source string record
                                    (xxbyte_lua_get_source) */
    uint32_t lineinfo_count;     /* lineinfo entries */
    uint64_t lineinfo_offset;    /* first entry: absolute lines as ints
                                    (5.0 to 5.3) or signed byte deltas
                                    (5.4+) */
    uint32_t abslineinfo_count;  /* 5.4+: absolute (pc, line) pairs */
    uint64_t abslineinfo_offset; /* first pair (5.5: after the padding) */
    uint32_t local_count;        /* local variable records */
    uint64_t locals_offset;      /* first local variable record */
    uint64_t upvalues_offset;    /* 5.2+: first upvalue descriptor; 0
                                    for 5.0 and 5.1 */
} xxbyte_lua_function;

/*
 * Walks the function prototypes of the chunk at data[0], whose header was
 * parsed into *header from the same bytes, and stores the first
 * min(capacity, count) of them, in pre-order, in functions (which may be
 * NULL when capacity is 0). *count receives the number of prototypes.
 * data must start at the chunk's first byte (5.5 aligns the code to it).
 * Every string, constant, upvalue, prototype and debug record is checked
 * against size: XXBYTE_STATUS_TRUNCATED when one runs past it,
 * XXBYTE_STATUS_INVALID_INSTRUCTION for a negative or oversized count, an
 * unknown constant type, a missing constant string, a 5.0 upvalue name
 * count other than 0 or nups, a 5.5 reference to a string not yet saved,
 * or prototypes nested deeper than XXBYTE_LUA_MAX_DEPTH. The work is
 * linear in size. Trailing bytes after the main function are ignored
 * (the function records' end_offset tells where it ends).
 */
XXBYTE_API xxbyte_status xxbyte_lua_find_functions(const uint8_t *data, size_t size, const xxbyte_lua_header *header, xxbyte_lua_function *functions, size_t capacity,
                                                   size_t *count);

#define XXBYTE_LUA_CONSTANT_NIL 0u
#define XXBYTE_LUA_CONSTANT_BOOLEAN 1u
#define XXBYTE_LUA_CONSTANT_NUMBER 2u  /* lua_Number */
#define XXBYTE_LUA_CONSTANT_INTEGER 3u /* lua_Integer (5.3+) */
#define XXBYTE_LUA_CONSTANT_STRING 4u

/* One constant of a function. */
typedef struct xxbyte_lua_constant {
    uint32_t type;          /* XXBYTE_LUA_CONSTANT_* */
    uint32_t size;          /* NUMBER and INTEGER: header number_size or
                               integer_size; 0 otherwise */
    int64_t value;          /* BOOLEAN: 0 or 1; INTEGER: the value;
                               NUMBER: the IEEE-754 bits (size 4 or 8), or
                               the sign-extended value when the chunk's
                               numbers are integral */
    uint64_t string_offset; /* STRING: first byte, from data[0] */
    uint64_t string_length; /* STRING: length without the NUL */
} xxbyte_lua_constant;

/* A string's bytes in the chunk: data[offset .. offset + length). */
typedef struct xxbyte_lua_string {
    uint64_t offset;
    uint64_t length; /* without the NUL */
} xxbyte_lua_string;

/*
 * 5.5 dumps each distinct string once; a later use of it is a reference
 * to its ordinal (1 = first string saved). This walks the chunk once, as
 * xxbyte_lua_find_functions does (same checks and statuses), and stores
 * the first min(capacity, count) saved strings in strings[0..) (strings
 * may be NULL when capacity is 0); *count receives the number of saved
 * strings, 0 for 5.0 to 5.4, which have no references. Pass the table to
 * the _ex getters below so that each reference resolves in O(1).
 */
XXBYTE_API xxbyte_status xxbyte_lua_get_saved_strings(const uint8_t *data, size_t size, const xxbyte_lua_header *header, xxbyte_lua_string *strings, size_t capacity,
                                                      size_t *count);

/*
 * Reads constant index (0-based, as in the INDEX CONSTANT operands) of
 * *function, which xxbyte_lua_find_functions returned for the same data.
 * Constant records vary in size, so this reads the records before index
 * too; use xxbyte_lua_get_constants to read many in one pass. A 5.5
 * string constant that reuses a string saved earlier in the chunk is
 * resolved to that string by walking the chunk again from the start, so
 * resolving every constant of a 5.5 chunk this way costs O(references x
 * chunk size); use xxbyte_lua_get_constants_ex with the table from
 * xxbyte_lua_get_saved_strings for linear total work. Returns
 * XXBYTE_STATUS_INVALID_ARGUMENT when index >= constant_count; on failure
 * *constant is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_lua_get_constant(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t index,
                                                 xxbyte_lua_constant *constant);

/*
 * Reads constants first to first + count - 1 of *function into
 * constants[0..count) in one pass over the records (5.5 reused strings
 * are resolved as by xxbyte_lua_get_constant, one chunk walk each).
 * Returns XXBYTE_STATUS_INVALID_ARGUMENT, without writing to constants,
 * when the range is not within constant_count or constants is NULL with a
 * nonzero count; on any other failure the count constants are zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_lua_get_constants(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t first,
                                                  xxbyte_lua_constant *constants, size_t count);

/*
 * As xxbyte_lua_get_constants, but 5.5 references resolve through
 * saved[0..saved_count), the complete table that
 * xxbyte_lua_get_saved_strings returned for the same data (saved_count
 * is its *count, and the array must hold all of them), so the cost is
 * linear in the records read. A reference beyond saved_count gives
 * XXBYTE_STATUS_INVALID_INSTRUCTION, and a table entry outside size
 * XXBYTE_STATUS_INVALID_ARGUMENT. saved NULL with saved_count 0 behaves
 * as xxbyte_lua_get_constants; saved NULL with a nonzero saved_count is
 * XXBYTE_STATUS_INVALID_ARGUMENT.
 */
XXBYTE_API xxbyte_status xxbyte_lua_get_constants_ex(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function,
                                                     uint32_t first, xxbyte_lua_constant *constants, size_t count, const xxbyte_lua_string *saved, size_t saved_count);

/*
 * Reads the debug name of upvalue index of *function: *offset and *length
 * receive the position and length of its bytes, or 0 and 0 when the name
 * is missing. Name records vary in size, so this reads the names before
 * index too (O(index) per call); use xxbyte_lua_get_upvalue_names to read
 * many in one pass. 5.5 names that reuse a saved string resolve as for
 * xxbyte_lua_get_constant (one chunk walk). Returns
 * XXBYTE_STATUS_INVALID_ARGUMENT when index >= upvalue_name_count
 * (stripped chunks have no names).
 */
XXBYTE_API xxbyte_status xxbyte_lua_get_upvalue_name(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function,
                                                     uint32_t index, uint64_t *offset, uint64_t *length);

/* As xxbyte_lua_get_upvalue_name, resolving 5.5 references through the
 * saved-string table as xxbyte_lua_get_constants_ex does. */
XXBYTE_API xxbyte_status xxbyte_lua_get_upvalue_name_ex(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function,
                                                        uint32_t index, uint64_t *offset, uint64_t *length, const xxbyte_lua_string *saved, size_t saved_count);

/*
 * Reads the debug names of upvalues first to first + count - 1 of
 * *function into names[0..count) in one pass over the name records; a
 * missing name is {0, 0}. 5.5 reused strings resolve as by
 * xxbyte_lua_get_upvalue_name (one chunk walk each). Returns
 * XXBYTE_STATUS_INVALID_ARGUMENT, without writing to names, when the
 * range is not within upvalue_name_count or names is NULL with a nonzero
 * count; on any other failure the count names are zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_lua_get_upvalue_names(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function,
                                                      uint32_t first, xxbyte_lua_string *names, size_t count);

/* As xxbyte_lua_get_upvalue_names, resolving 5.5 references through the
 * saved-string table as xxbyte_lua_get_constants_ex does (linear total
 * work); same argument rules. */
XXBYTE_API xxbyte_status xxbyte_lua_get_upvalue_names_ex(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function,
                                                         uint32_t first, xxbyte_lua_string *names, size_t count, const xxbyte_lua_string *saved, size_t saved_count);

/*
 * Reads the source name record of *function: *present is 1 and *source
 * its bytes when the record holds a name, 0 otherwise (stripped chunks,
 * and in 5.0, 5.1, 5.3 and 5.4 every function whose source is its
 * parent's: luac then shows the parent's, and "?" for a main function
 * without one; 5.2 and 5.5 record it per function, and luac shows "?"
 * when it is missing). 5.5 references resolve through saved[0 ..
 * saved_count) when saved is not NULL, else by walking the chunk. The
 * statuses are those of xxbyte_lua_get_constants_ex.
 */
XXBYTE_API xxbyte_status xxbyte_lua_get_source(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function,
                                               const xxbyte_lua_string *saved, size_t saved_count, xxbyte_lua_string *source, int *present);

/* One local variable record (LocVar) of a function's debug data. */
typedef struct xxbyte_lua_local {
    uint64_t name_offset; /* name bytes, from data[0] */
    uint64_t name_length;
    uint32_t start_pc; /* first pc (0-based) where it is active */
    uint32_t end_pc;   /* first pc where it is dead */
    uint8_t present;   /* the record holds a name */
    uint8_t reserved[7];
} xxbyte_lua_local;

#define XXBYTE_LUA_NO_LOCAL 0xFFFFFFFFu

/*
 * Reads local variable records first to first + count - 1 of *function
 * (local_count in all, 0 when stripped) into locals[0..count) in one pass;
 * 5.5 names resolve as for xxbyte_lua_get_constants_ex (saved may be NULL
 * with saved_count 0). Same argument rules and statuses as
 * xxbyte_lua_get_constants_ex.
 */
XXBYTE_API xxbyte_status xxbyte_lua_get_locals(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t first,
                                               xxbyte_lua_local *locals, size_t count, const xxbyte_lua_string *saved, size_t saved_count);

/*
 * The local variable that holds register reg at pc (0-based), as
 * luaF_getlocalname finds it in all of a function's records locals[0 ..
 * count): the reg + 1-th record active at pc among those starting at or
 * before it. Returns its index in locals, or XXBYTE_LUA_NO_LOCAL when the
 * register holds no named local there (a temporary, or a stripped chunk).
 */
XXBYTE_API uint32_t xxbyte_lua_local_register(const xxbyte_lua_local *locals, size_t count, uint32_t reg, uint32_t pc);

/* One 5.2+ upvalue descriptor. */
typedef struct xxbyte_lua_upvalue {
    uint8_t in_stack; /* 1: a register of the enclosing function */
    uint8_t index;    /* that register, or the enclosing upvalue */
    uint8_t kind;     /* 5.4+: VDKREG, RDKCONST, RDKTOCLOSE or RDKCTC */
    uint8_t reserved;
} xxbyte_lua_upvalue;

/*
 * Reads upvalue descriptors first to first + count - 1 of *function into
 * upvalues[0..count). Returns XXBYTE_STATUS_INVALID_ARGUMENT for 5.0 and
 * 5.1, which have none, or a range outside upvalue_count, and
 * XXBYTE_STATUS_TRUNCATED when the records run past size.
 */
XXBYTE_API xxbyte_status xxbyte_lua_get_upvalues(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, uint32_t first,
                                                 xxbyte_lua_upvalue *upvalues, size_t count);

/*
 * The source lines of instructions 0 to count - 1 of *function (count at
 * most code_count) into lines[0..count), as luac's [line] column shows
 * them: 5.0 to 5.3 store them as ints, 5.4+ as byte deltas from
 * line_defined with absolute (pc, line) entries (luaG_getfuncline). A
 * line of 0 or less means none (luac's "[-]"): stripped chunks, and pcs
 * past the lineinfo. Returns XXBYTE_STATUS_INVALID_ARGUMENT for a bad
 * range and XXBYTE_STATUS_TRUNCATED when the records run past size.
 */
XXBYTE_API xxbyte_status xxbyte_lua_get_lines(const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *function, int64_t *lines,
                                              size_t count);

/*
 * Whole chunks (xxbyte_disasm_file, xxbyte_cli -F): a precompiled chunk as
 * luac -o writes it (5.0 to 5.5, any byte order and int, size_t,
 * lua_Integer and lua_Number sizes the header allows, stripped or not),
 * listed as luac -l -l lists it (luac -l for 5.0, whose luac has no
 * tables). Every function, in luac's order (pre-order), gets luac's two
 * header lines:
 *
 *   main <small.lua:0,0> (20 instructions at 0x20)
 *   0+ params, 8 slots, 1 upvalue, 3 locals, 7 constants, 1 function
 *
 * (5.0 "main <small.lua:0> (5 instructions, 20 bytes at 0x...)" and "...
 * stacks ...", 5.1 "... (9 instructions, 36 bytes at 0x...)"), then its
 * code as xxbyte_disasm_buf_ex lists it from address 0 with
 * xxbyte_lua_resolve (luac's columns and all of its comments), then, from
 * 5.1, luac's "constants (N) for ...", "locals (N) for ..." and
 * "upvalues (N) for ..." tables, tab-separated as luac prints them. A
 * blank line separates functions. luac's %p addresses (of a Proto, in the
 * header, the tables and CLOSURE comments) are replaced by the file
 * offset of the function's record, "0x" and lowercase hex. The source
 * name is luac's: the text after '@' or '=', "(bstring)" or "(string)",
 * the parent's for functions that do not record one (5.0, 5.1, 5.3,
 * 5.4), "?" when there is none. mode XXBYTE_MODE_AUTO takes the decode
 * mode from the header; another mode must be of the chunk's series (its
 * byte-order flag may differ), else XXBYTE_STATUS_UNSUPPORTED_MODE, which
 * is also the status for chunks whose instructions xxbyte cannot decode
 * (8-byte Instructions after 5.0, 5.0 field widths other than 6/8/9/9).
 * Header and walker failures return the statuses of
 * xxbyte_lua_parse_header and xxbyte_lua_find_functions.
 *
 * Differences from luac's text: the pc and [line] columns are not printed
 * (xxbyte_lua_get_lines has the lines); a 5.2/5.3 SETLIST with C == 0
 * shows its EXTRAARG word as a line of its own (luac folds it into the
 * SETLIST comment, which xxbyte prints too); luac 5.4.0 to 5.4.3 printed
 * FORPREP, RETURN and TAILCALL as the decode-mode notes say; control
 * characters in names (not in quoted strings, which follow luac) print as
 * \ddd to keep one line per entry; luac reads past its tables for
 * malformed indexes, where xxbyte prints "?".
 */

/*
 * Names for formatting a chunk's code: the context of xxbyte_lua_resolve.
 * Set data to caches, or use xxbyte_lua_names_init and set function (and
 * optionally the caches) before formatting each function's code, decoded
 * at addresses from 0 (pc * 4) as the decode-mode notes say.
 */
typedef struct xxbyte_lua_names {
    const uint8_t *data;
    size_t size;
    const xxbyte_lua_header *header;
    const xxbyte_lua_function *functions; /* all of them, from
                                             xxbyte_lua_find_functions */
    size_t function_count;
    const xxbyte_lua_string *saved; /* 5.5: xxbyte_lua_get_saved_strings,
                                       or NULL */
    size_t saved_count;
    uint32_t function; /* index of the function being formatted */
    uint32_t reserved;
    /* Optional caches for that function; NULL reads from data. */
    const xxbyte_lua_constant *constants;   /* constant_count entries */
    const xxbyte_lua_string *upvalue_names; /* upvalue_name_count */
    const uint32_t *children;               /* prototype_count function indexes: the
                                               function that CLOSURE Bx creates */
} xxbyte_lua_names;

/* Fills *names with the arguments, function 0 and no caches. */
XXBYTE_API void xxbyte_lua_names_init(xxbyte_lua_names *names, const uint8_t *data, size_t size, const xxbyte_lua_header *header, const xxbyte_lua_function *functions,
                                      size_t function_count, const xxbyte_lua_string *saved, size_t saved_count);

/*
 * A ready-made xxbyte_resolver; context is an xxbyte_lua_names. For the
 * INDEX operands of instructions of function names->function it gives:
 *   - XXBYTE_INDEX_CONSTANT: the constant as luac prints it: strings
 *     quoted and escaped as that series' PrintString does ("hi\n",
 *     "\000"), numbers as LUA_NUMBER_FMT "%.14g" ("0.1", "1e+100", 5.3+
 *     floats that look integral with ".0"), 5.3+ integers in decimal,
 *     true, false and nil; 5.0 and 5.1 GETGLOBAL and SETGLOBAL show the
 *     bare name, as luac does;
 *   - XXBYTE_INDEX_UPVALUE: the upvalue's debug name, "-" when the chunk
 *     has none;
 *   - XXBYTE_INDEX_FUNCTION: "0x" and the hex file offset of the child
 *     function's record (the "at 0x..." of its xxbyte_disasm_file
 *     header), standing in for luac's %p.
 * It declines other operands (BRANCH targets keep luac's "to N"), indexes
 * outside the function's tables, and names that do not fit (the core's
 * resolver buffer holds 511 characters).
 *
 * Passed to xxbyte_format_ex, xxbyte_format_buf or xxbyte_disasm_buf_ex
 * itself (not wrapped), it makes the Lua formatter print every comment
 * luac -l -l prints for the instruction, in that series' layout, from the
 * same names and without the length limit:
 *   GETTABUP 0 0 0 ; _ENV "print"      LOADK 1 -2 ; "hello"  (5.1-5.3)
 *   CALL 0 2 1 ; 1 in 0 out            MMBINK 0 2 6 0 ; __add 1.5
 *   SETFIELD 0 1 2k ; "x" 5            CLOSURE 0 0 ; 0x3a
 *   ADD 0 0 -1 ; - 1 (5.1)             NEWTABLE 0 0 3 ; 3 (5.4)
 * including those that use the next instruction (5.4+ LOADKX, NEWTABLE
 * and SETLIST read their EXTRAARG; a 5.2/5.3 SETLIST with C == 0 shows the
 * following word, and that EXTRAARG gets no comment). Missing constants,
 * names and children print as "?".
 */
XXBYTE_API size_t xxbyte_lua_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
