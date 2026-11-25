#ifndef XXBYTE_XXBYTE_LUAJIT_H
#define XXBYTE_XXBYTE_LUAJIT_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_LUAJIT
#error "the LuaJIT decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * LuaJIT decode mode: an opcode set in the low byte, optionally combined
 * with XXBYTE_LUAJIT_MODE_BE. Any other value is
 * XXBYTE_STATUS_UNSUPPORTED_MODE.
 *
 *   XXBYTE_LUAJIT_MODE_2_1 (0, the default): the LuaJIT 2.1 branch since
 *     2026-07-20, which added the bit operators BNOT..BSAR (opcodes
 *     89-95) and moved the function headers FUNCF..FUNCCW to 96-103.
 *   XXBYTE_LUAJIT_MODE_2_1_NOBITOP: LuaJIT 2.1 before that (2.1.0-beta1
 *     and the rolling releases up to then): the same opcodes without
 *     BNOT..BSAR, FUNCF..FUNCCW at 89-96.
 *   XXBYTE_LUAJIT_MODE_2_0: LuaJIT 2.0.x, without ISTYPE, ISNUM, TGETR
 *     and TSETR (93 opcodes).
 *
 * The two 2.1 sets differ only in opcodes 89-103. Dumps written by LuaJIT
 * hold neither the function-header opcodes (the header instruction is not
 * stored) nor the I and J variants the JIT patches in (lj_bcwrite restores
 * the plain opcodes), so XXBYTE_LUAJIT_MODE_2_1 decodes every 2.1 dump,
 * with or without the BITOP flag; XXBYTE_LUAJIT_MODE_2_1_NOBITOP is for
 * in-memory bytecode of older 2.1 builds. LJ_FR2 (GC64) builds use the same
 * opcodes; the dump flag FR2 only records the frame layout the code was
 * compiled for.
 *
 * XXBYTE_LUAJIT_MODE_BE reads big-endian instruction words, as a dump
 * with the BE flag (written on a big-endian host) stores them; without it
 * words are little-endian.
 */
#define XXBYTE_LUAJIT_MODE_2_1 0x00u
#define XXBYTE_LUAJIT_MODE_2_0 0x01u
#define XXBYTE_LUAJIT_MODE_2_1_NOBITOP 0x02u
#define XXBYTE_LUAJIT_MODE_SET_MASK 0xFFu
#define XXBYTE_LUAJIT_MODE_BE 0x100u
#define XXBYTE_LUAJIT_MODE_DEFAULT XXBYTE_LUAJIT_MODE_2_1

/*
 * Every instruction is one 32-bit word: OP in bits 0-7, A in 8-15, and
 * either C in 16-23 and B in 24-31 or D in 16-31. instruction.opcode is
 * OP and size is 4.
 *
 * Operands follow luajit -bl: A when the opcode has an A operand, then B
 * and C for ABC opcodes or D for AD opcodes. Each operand's extra is its
 * LuaJIT operand mode (xxbyte_luajit_operand_mode) and size is 1 for A, B
 * and C, 2 for D:
 *   - dst, base, var, rbase: XXBYTE_OPERAND_REGISTER, the stack slot.
 *   - uv: XXBYTE_OPERAND_INDEX of kind XXBYTE_INDEX_UPVALUE.
 *   - str, num, tab, func, cdata: XXBYTE_OPERAND_INDEX of the
 *     XXBYTE_LUAJIT_INDEX_* kind, value as encoded (see below).
 *   - lit, pri: unsigned XXBYTE_OPERAND_IMMEDIATE. pri is 0 nil, 1 false,
 *     2 true.
 *   - lits (KSHORT): XXBYTE_OPERAND_IMMEDIATE, sign-extended from 16 bits,
 *     with the SIGNED flag.
 *   - jump: XXBYTE_OPERAND_BRANCH, value = address + 4 * (D - 0x7FFF),
 *     the absolute byte address of the target.
 *
 * Addresses are byte offsets. luajit -bl numbers instructions by pc, where
 * pc 0 is the function header that dumps leave out, so the first stored
 * instruction of a dumped prototype is pc 1: decode instruction i (from 0)
 * of xxbyte_luajit_proto at address 4 * (i + 1) to get -bl's numbering.
 *
 * Text is luajit -bl's line without the pc, the jump-target marker and the
 * "; constant" comment, with runs of spaces collapsed: "KSHORT 0 1",
 * "TGETS 2 0 3", "IST 5" (no A operand), "KSHORT 1 -5", "JMP 2 => 0008".
 * A jump prints "=> " and the target pc as printf "%04lld" does: the
 * target address divided by 4, rounded toward negative infinity. A
 * resolver is called for each INDEX and BRANCH operand; its text replaces
 * the number (for a jump, the digits after "=> "), except for the
 * ready-made xxbyte_luajit_resolve (below), which keeps the numbers and
 * adds -bl's "; constant" comment. The mnemonics are
 * LuaJIT's upper-case names, so XXBYTE_FORMAT_UPPERCASE changes nothing.
 *
 * Groups: the comparisons and tests ISLT..ISF are CONDITIONAL (they either
 * skip the JMP that follows them or take its target), as are the FOR and
 * ITERL loop instructions; JMP, UCLO and ISNEXT are JUMP|STOP (LOOP and
 * ILOOP only carry the loop exit for the JIT and fall through); CALL,
 * CALLM, ITERC and ITERN (the specialized next() call) are CALL; CALLT and
 * CALLMT are CALL|RETURN|STOP; RET, RETM, RET0 and RET1 are RETURN|STOP.
 */

/*
 * Mnemonic identifiers (xxbyte_instruction.id): one per opcode name, in the
 * opcode order of XXBYTE_LUAJIT_MODE_2_1, so id = opcode + 1 there and a
 * name keeps its id in every mode. Valid ids are 1 to
 * XXBYTE_LUAJIT_ID_COUNT - 1.
 */
typedef enum xxbyte_luajit_id {
    XXBYTE_LUAJIT_ID_INVALID = 0,
    XXBYTE_LUAJIT_ID_ISLT = 1,
    XXBYTE_LUAJIT_ID_ISGE = 2,
    XXBYTE_LUAJIT_ID_ISLE = 3,
    XXBYTE_LUAJIT_ID_ISGT = 4,
    XXBYTE_LUAJIT_ID_ISEQV = 5,
    XXBYTE_LUAJIT_ID_ISNEV = 6,
    XXBYTE_LUAJIT_ID_ISEQS = 7,
    XXBYTE_LUAJIT_ID_ISNES = 8,
    XXBYTE_LUAJIT_ID_ISEQN = 9,
    XXBYTE_LUAJIT_ID_ISNEN = 10,
    XXBYTE_LUAJIT_ID_ISEQP = 11,
    XXBYTE_LUAJIT_ID_ISNEP = 12,
    XXBYTE_LUAJIT_ID_ISTC = 13,
    XXBYTE_LUAJIT_ID_ISFC = 14,
    XXBYTE_LUAJIT_ID_IST = 15,
    XXBYTE_LUAJIT_ID_ISF = 16,
    XXBYTE_LUAJIT_ID_ISTYPE = 17,
    XXBYTE_LUAJIT_ID_ISNUM = 18,
    XXBYTE_LUAJIT_ID_MOV = 19,
    XXBYTE_LUAJIT_ID_NOT = 20,
    XXBYTE_LUAJIT_ID_UNM = 21,
    XXBYTE_LUAJIT_ID_LEN = 22,
    XXBYTE_LUAJIT_ID_ADDVN = 23,
    XXBYTE_LUAJIT_ID_SUBVN = 24,
    XXBYTE_LUAJIT_ID_MULVN = 25,
    XXBYTE_LUAJIT_ID_DIVVN = 26,
    XXBYTE_LUAJIT_ID_MODVN = 27,
    XXBYTE_LUAJIT_ID_ADDNV = 28,
    XXBYTE_LUAJIT_ID_SUBNV = 29,
    XXBYTE_LUAJIT_ID_MULNV = 30,
    XXBYTE_LUAJIT_ID_DIVNV = 31,
    XXBYTE_LUAJIT_ID_MODNV = 32,
    XXBYTE_LUAJIT_ID_ADDVV = 33,
    XXBYTE_LUAJIT_ID_SUBVV = 34,
    XXBYTE_LUAJIT_ID_MULVV = 35,
    XXBYTE_LUAJIT_ID_DIVVV = 36,
    XXBYTE_LUAJIT_ID_MODVV = 37,
    XXBYTE_LUAJIT_ID_POW = 38,
    XXBYTE_LUAJIT_ID_CAT = 39,
    XXBYTE_LUAJIT_ID_KSTR = 40,
    XXBYTE_LUAJIT_ID_KCDATA = 41,
    XXBYTE_LUAJIT_ID_KSHORT = 42,
    XXBYTE_LUAJIT_ID_KNUM = 43,
    XXBYTE_LUAJIT_ID_KPRI = 44,
    XXBYTE_LUAJIT_ID_KNIL = 45,
    XXBYTE_LUAJIT_ID_UGET = 46,
    XXBYTE_LUAJIT_ID_USETV = 47,
    XXBYTE_LUAJIT_ID_USETS = 48,
    XXBYTE_LUAJIT_ID_USETN = 49,
    XXBYTE_LUAJIT_ID_USETP = 50,
    XXBYTE_LUAJIT_ID_UCLO = 51,
    XXBYTE_LUAJIT_ID_FNEW = 52,
    XXBYTE_LUAJIT_ID_TNEW = 53,
    XXBYTE_LUAJIT_ID_TDUP = 54,
    XXBYTE_LUAJIT_ID_GGET = 55,
    XXBYTE_LUAJIT_ID_GSET = 56,
    XXBYTE_LUAJIT_ID_TGETV = 57,
    XXBYTE_LUAJIT_ID_TGETS = 58,
    XXBYTE_LUAJIT_ID_TGETB = 59,
    XXBYTE_LUAJIT_ID_TGETR = 60,
    XXBYTE_LUAJIT_ID_TSETV = 61,
    XXBYTE_LUAJIT_ID_TSETS = 62,
    XXBYTE_LUAJIT_ID_TSETB = 63,
    XXBYTE_LUAJIT_ID_TSETM = 64,
    XXBYTE_LUAJIT_ID_TSETR = 65,
    XXBYTE_LUAJIT_ID_CALLM = 66,
    XXBYTE_LUAJIT_ID_CALL = 67,
    XXBYTE_LUAJIT_ID_CALLMT = 68,
    XXBYTE_LUAJIT_ID_CALLT = 69,
    XXBYTE_LUAJIT_ID_ITERC = 70,
    XXBYTE_LUAJIT_ID_ITERN = 71,
    XXBYTE_LUAJIT_ID_VARG = 72,
    XXBYTE_LUAJIT_ID_ISNEXT = 73,
    XXBYTE_LUAJIT_ID_RETM = 74,
    XXBYTE_LUAJIT_ID_RET = 75,
    XXBYTE_LUAJIT_ID_RET0 = 76,
    XXBYTE_LUAJIT_ID_RET1 = 77,
    XXBYTE_LUAJIT_ID_FORI = 78,
    XXBYTE_LUAJIT_ID_JFORI = 79,
    XXBYTE_LUAJIT_ID_FORL = 80,
    XXBYTE_LUAJIT_ID_IFORL = 81,
    XXBYTE_LUAJIT_ID_JFORL = 82,
    XXBYTE_LUAJIT_ID_ITERL = 83,
    XXBYTE_LUAJIT_ID_IITERL = 84,
    XXBYTE_LUAJIT_ID_JITERL = 85,
    XXBYTE_LUAJIT_ID_LOOP = 86,
    XXBYTE_LUAJIT_ID_ILOOP = 87,
    XXBYTE_LUAJIT_ID_JLOOP = 88,
    XXBYTE_LUAJIT_ID_JMP = 89,
    XXBYTE_LUAJIT_ID_BNOT = 90,
    XXBYTE_LUAJIT_ID_BAND = 91,
    XXBYTE_LUAJIT_ID_BOR = 92,
    XXBYTE_LUAJIT_ID_BXOR = 93,
    XXBYTE_LUAJIT_ID_BSHL = 94,
    XXBYTE_LUAJIT_ID_BSHR = 95,
    XXBYTE_LUAJIT_ID_BSAR = 96,
    XXBYTE_LUAJIT_ID_FUNCF = 97,
    XXBYTE_LUAJIT_ID_IFUNCF = 98,
    XXBYTE_LUAJIT_ID_JFUNCF = 99,
    XXBYTE_LUAJIT_ID_FUNCV = 100,
    XXBYTE_LUAJIT_ID_IFUNCV = 101,
    XXBYTE_LUAJIT_ID_JFUNCV = 102,
    XXBYTE_LUAJIT_ID_FUNCC = 103,
    XXBYTE_LUAJIT_ID_FUNCCW = 104,
    XXBYTE_LUAJIT_ID_COUNT = 105 /* one past the last id */
} xxbyte_luajit_id;

/*
 * Index kinds of the constant operands. str, tab, func and cdata operands
 * index the prototype's GC constants from the end: value v is kgc[-1 - v]
 * in LuaJIT's terms, which is entry sizekgc - 1 - v in dump order. num
 * operands index the number constants from the start (entry v).
 */
typedef enum xxbyte_luajit_index_kind {
    /* string */
    XXBYTE_LUAJIT_INDEX_STR = XXBYTE_INDEX_FAMILY_FIRST,
    /* number */
    XXBYTE_LUAJIT_INDEX_NUM = XXBYTE_INDEX_FAMILY_FIRST + 1,
    /* template table */
    XXBYTE_LUAJIT_INDEX_TAB = XXBYTE_INDEX_FAMILY_FIRST + 2,
    /* child prototype */
    XXBYTE_LUAJIT_INDEX_FUNC = XXBYTE_INDEX_FAMILY_FIRST + 3,
    /* FFI cdata (int64_t, uint64_t or complex) */
    XXBYTE_LUAJIT_INDEX_CDATA = XXBYTE_INDEX_FAMILY_FIRST + 4
} xxbyte_luajit_index_kind;

/* LuaJIT operand modes (BCMode), stored in each operand's extra. */
typedef enum xxbyte_luajit_operand_mode {
    XXBYTE_LUAJIT_OPERAND_NONE = 0,
    XXBYTE_LUAJIT_OPERAND_DST = 1,   /* slot written */
    XXBYTE_LUAJIT_OPERAND_BASE = 2,  /* first slot of a range */
    XXBYTE_LUAJIT_OPERAND_VAR = 3,   /* slot read */
    XXBYTE_LUAJIT_OPERAND_RBASE = 4, /* first slot, read */
    XXBYTE_LUAJIT_OPERAND_UV = 5,    /* upvalue */
    XXBYTE_LUAJIT_OPERAND_LIT = 6,   /* unsigned literal */
    XXBYTE_LUAJIT_OPERAND_LITS = 7,  /* signed literal */
    XXBYTE_LUAJIT_OPERAND_PRI = 8,   /* primitive: nil, false, true */
    XXBYTE_LUAJIT_OPERAND_NUM = 9,   /* number constant */
    XXBYTE_LUAJIT_OPERAND_STR = 10,  /* string constant */
    XXBYTE_LUAJIT_OPERAND_TAB = 11,  /* template table constant */
    XXBYTE_LUAJIT_OPERAND_FUNC = 12, /* child prototype constant */
    XXBYTE_LUAJIT_OPERAND_JUMP = 13, /* branch offset, biased by 0x8000 */
    XXBYTE_LUAJIT_OPERAND_CDATA = 14 /* cdata constant */
} xxbyte_luajit_operand_mode;

/* Fields of xxbyte_luajit_operand_modes(), packed like LuaJIT's
 * lj_bc_mode[] without the metamethod. */
#define XXBYTE_LUAJIT_MODES_A(modes) ((uint32_t)(modes) & 7u)
#define XXBYTE_LUAJIT_MODES_B(modes) (((uint32_t)(modes) >> 3) & 15u)
#define XXBYTE_LUAJIT_MODES_CD(modes) (((uint32_t)(modes) >> 7) & 15u)

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_luajit_mnemonic(uint32_t id);

/* The id of opcode in mode, or 0 when the opcode is undefined there or
 * the mode is not supported. */
XXBYTE_API uint32_t xxbyte_luajit_opcode_id(uint32_t mode, uint32_t opcode);

/* The operand modes of id (A | B << 3 | C or D << 7, see
 * XXBYTE_LUAJIT_MODES_*), or 0 when id is not valid. A nonzero B mode
 * means the ABC format. */
XXBYTE_API uint32_t xxbyte_luajit_operand_modes(uint32_t id);

/* Bytecode dump flags (BCDUMP_F_*). */
#define XXBYTE_LUAJIT_FLAG_BE 0x01u    /* big-endian words and refs */
#define XXBYTE_LUAJIT_FLAG_STRIP 0x02u /* no chunk name, no debug info */
#define XXBYTE_LUAJIT_FLAG_FFI 0x04u   /* uses FFI cdata constants */
#define XXBYTE_LUAJIT_FLAG_FR2 0x08u   /* 2.1: two-slot frames (GC64) */
#define XXBYTE_LUAJIT_FLAG_BITOP 0x10u /* 2.1 since 2026-07: bit operators */

/* A parsed bytecode dump header. */
typedef struct xxbyte_luajit_header {
    uint32_t version;     /* 1 (LuaJIT 2.0) or 2 (LuaJIT 2.1) */
    uint32_t flags;       /* XXBYTE_LUAJIT_FLAG_* */
    uint32_t mode;        /* the decode mode for the dump's bytecode */
    uint32_t name_offset; /* chunk name from data[0], 0 when stripped */
    uint32_t name_size;   /* chunk name length, 0 when stripped */
    uint32_t size;        /* header size: offset of the first prototype */
} xxbyte_luajit_header;

/*
 * Parses the dump header at data[0]: ESC 'L' 'J', the version byte, the
 * ULEB128 flags and, without STRIP, the ULEB128 length and bytes of the
 * chunk name. mode is XXBYTE_LUAJIT_MODE_2_0 for version 1 and
 * XXBYTE_LUAJIT_MODE_2_1 for version 2, with XXBYTE_LUAJIT_MODE_BE when
 * the BE flag is set. Returns XXBYTE_STATUS_TRUNCATED when size is too
 * small, XXBYTE_STATUS_UNSUPPORTED_MODE for an unknown version (LuaJIT
 * reserves 0x80 and up for private formats) and
 * XXBYTE_STATUS_INVALID_INSTRUCTION for a bad signature, flags the version
 * does not define or a ULEB128 longer than 5 bytes or 32 bits. On failure
 * *header is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_luajit_parse_header(const uint8_t *data, size_t size, xxbyte_luajit_header *header);

/* Prototype flags (PROTO_*). */
#define XXBYTE_LUAJIT_PROTO_CHILD 0x01u  /* has child prototypes */
#define XXBYTE_LUAJIT_PROTO_VARARG 0x02u /* vararg function */
#define XXBYTE_LUAJIT_PROTO_FFI 0x04u    /* uses KCDATA */
#define XXBYTE_LUAJIT_PROTO_BITOP 0x80u  /* 2.1 since 2026-07: bit operators */

/* One prototype of a dump. Offsets are from data[0]. */
typedef struct xxbyte_luajit_proto {
    uint32_t offset; /* the prototype's ULEB128 length */
    uint32_t size;   /* bytes from offset to the next prototype */
    uint32_t flags;  /* XXBYTE_LUAJIT_PROTO_* */
    uint32_t numparams;
    uint32_t framesize;
    uint32_t sizeuv;         /* upvalues */
    uint32_t sizekgc;        /* GC constants */
    uint32_t sizekn;         /* number constants */
    uint32_t sizebc;         /* stored instructions (pc 1 to sizebc) */
    uint32_t sizedbg;        /* debug info bytes, 0 without debug info */
    uint32_t firstline;      /* linedefined */
    uint32_t numline;        /* lastlinedefined - linedefined */
    uint32_t bc_offset;      /* sizebc 32-bit words in the dump's byte order */
    uint32_t uv_offset;      /* sizeuv 16-bit upvalue refs, same byte order */
    uint32_t kgc_offset;     /* GC constants, first to last */
    uint32_t kn_offset;      /* number constants */
    uint32_t debug_offset;   /* line info, then upvalue and variable names */
    uint32_t lineinfo_width; /* bytes per instruction in the line info (1,
                                2 or 4), 0 without line info */
    uint32_t children;       /* GC constants that are child prototypes */
} xxbyte_luajit_proto;

/*
 * Parses the prototype at data[offset] of a dump whose header was parsed
 * into *header; the first one is at header->size and each next one at
 * offset + proto->size. Prototypes come children first, the main chunk
 * last, and LuaJIT pairs every child constant with the latest unpaired
 * prototype before it (not checked here).
 *
 * Returns XXBYTE_STATUS_END_OF_INPUT at the zero length that ends the dump,
 * normally a single 0 byte (*proto then holds only offset and size, the
 * length field's size), XXBYTE_STATUS_TRUNCATED when offset is size or the
 * prototype or its length field runs past size, and
 * XXBYTE_STATUS_INVALID_INSTRUCTION when its contents (header, bytecode,
 * upvalues, constants and debug info) do not fill its length exactly or
 * hold a ULEB128 longer than 5 bytes (or a size wider than 32 bits). The
 * work is linear in the prototype's length. On failure *proto is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_luajit_parse_proto(const uint8_t *data, size_t size, size_t offset, const xxbyte_luajit_header *header, xxbyte_luajit_proto *proto);

#define XXBYTE_LUAJIT_NO_PARENT 0xFFFFFFFFu

/* One prototype of a dump with its place in the prototype tree. */
typedef struct xxbyte_luajit_function {
    xxbyte_luajit_proto proto;
    uint32_t index;     /* position in the dump, 0 = first */
    uint32_t parent;    /* index of the prototype whose child constant it
                           is, or XXBYTE_LUAJIT_NO_PARENT for the main
                           chunk (the last) */
    uint32_t kgc_index; /* that constant's position in the parent's GC
                           constants, in dump order */
    uint32_t operand;   /* the FNEW D that creates it: the parent's sizekgc
                           - 1 - kgc_index */
} xxbyte_luajit_function;

/*
 * Walks every prototype of the dump at data[0], whose header was parsed
 * into *header, and stores the first min(capacity, count) of them, in dump
 * order, in functions (which may be NULL when capacity is 0); *count
 * receives the number of prototypes. Children are paired with their
 * parents as lj_bcread does: each child constant takes the latest
 * prototype not taken yet. Returns the statuses of
 * xxbyte_luajit_parse_proto (END_OF_INPUT is not an error here: the walk
 * stops at the terminating 0, and data that ends before it is
 * XXBYTE_STATUS_TRUNCATED), and
 * XXBYTE_STATUS_INVALID_INSTRUCTION when a child constant finds no
 * prototype, or when more than one prototype is left unpaired or none at
 * all (LuaJIT's "bad bytecode"). Trailing bytes after the terminating 0
 * are ignored. The work is linear in size.
 */
XXBYTE_API xxbyte_status xxbyte_luajit_find_functions(const uint8_t *data, size_t size, const xxbyte_luajit_header *header, xxbyte_luajit_function *functions,
                                                      size_t capacity, size_t *count);

/* GC constant types (BCDUMP_KGC_*). */
#define XXBYTE_LUAJIT_KGC_CHILD 0u
#define XXBYTE_LUAJIT_KGC_TAB 1u
#define XXBYTE_LUAJIT_KGC_I64 2u
#define XXBYTE_LUAJIT_KGC_U64 3u
#define XXBYTE_LUAJIT_KGC_COMPLEX 4u
#define XXBYTE_LUAJIT_KGC_STR 5u

/* One GC constant. */
typedef struct xxbyte_luajit_kgc {
    uint32_t type;   /* XXBYTE_LUAJIT_KGC_* */
    uint32_t offset; /* STR: first byte; TAB: the narray ULEB128 (the
                        template table record); from data[0] */
    uint32_t length; /* STR: byte count; TAB: record size in bytes */
    uint32_t reserved;
    uint64_t value; /* I64, U64: the 64-bit value; COMPLEX: the real
                       part's double bits */
    uint64_t imag;  /* COMPLEX: the imaginary part's double bits */
} xxbyte_luajit_kgc;

/*
 * Reads GC constants first to first + count - 1 of *proto (sizekgc in
 * all, dump order: the str, tab, func and cdata operand v names entry
 * sizekgc - 1 - v) into kgc[0..count) in one pass. Returns
 * XXBYTE_STATUS_INVALID_ARGUMENT for a bad range (kgc NULL with a nonzero
 * count) and XXBYTE_STATUS_INVALID_INSTRUCTION when the records do not
 * parse within the prototype.
 */
XXBYTE_API xxbyte_status xxbyte_luajit_get_kgc(const uint8_t *data, size_t size, const xxbyte_luajit_proto *proto, uint32_t first, xxbyte_luajit_kgc *kgc, size_t count);

/* One number constant. */
typedef struct xxbyte_luajit_knum {
    uint64_t bits;      /* the value as double bits (an integer constant
                           converted, as LuaJIT loads it) */
    int32_t integer;    /* the int32 value of an integer constant */
    uint8_t is_integer; /* stored as an integer (low bit 0) */
    uint8_t reserved[3];
} xxbyte_luajit_knum;

/* Reads number constants first to first + count - 1 of *proto (sizekn in
 * all) into knum[0..count); same argument rules and statuses as
 * xxbyte_luajit_get_kgc. */
XXBYTE_API xxbyte_status xxbyte_luajit_get_knum(const uint8_t *data, size_t size, const xxbyte_luajit_proto *proto, uint32_t first, xxbyte_luajit_knum *knum,
                                                size_t count);

/* A name in the dump: data[offset .. offset + length). */
typedef struct xxbyte_luajit_string {
    uint32_t offset;
    uint32_t length;
} xxbyte_luajit_string;

/*
 * Reads the debug names of upvalues 0 to count - 1 of *proto (count at
 * most sizeuv) into names[0..count). Returns XXBYTE_STATUS_INVALID_ARGUMENT
 * for a bad range or when the prototype has no debug info (sizedbg 0:
 * stripped dumps, where luajit -bl shows empty names), and
 * XXBYTE_STATUS_INVALID_INSTRUCTION when a name is not NUL-terminated
 * within the debug info.
 */
XXBYTE_API xxbyte_status xxbyte_luajit_get_uv_names(const uint8_t *data, size_t size, const xxbyte_luajit_proto *proto, xxbyte_luajit_string *names, size_t count);

/* Upvalue reference flags (PROTO_UV_*): the rest is the slot or the
 * enclosing upvalue. */
#define XXBYTE_LUAJIT_UV_LOCAL 0x8000u     /* a slot of the parent */
#define XXBYTE_LUAJIT_UV_IMMUTABLE 0x4000u /* never written */

/* Reads the 16-bit upvalue references 0 to count - 1 of *proto (count at
 * most sizeuv) in the dump's byte order. Returns
 * XXBYTE_STATUS_INVALID_ARGUMENT for a bad range. */
XXBYTE_API xxbyte_status xxbyte_luajit_get_uv_refs(const uint8_t *data, size_t size, const xxbyte_luajit_header *header, const xxbyte_luajit_proto *proto, uint16_t *refs,
                                                   size_t count);

/*
 * The source line of stored instructions 0 to count - 1 (luajit -bl pcs 1
 * to count) of *proto (count at most sizebc) into lines[0..count):
 * firstline plus the lineinfo entry, as lj_debug_line computes it. Lines
 * are 0 without line info. Returns XXBYTE_STATUS_INVALID_ARGUMENT for a
 * bad range.
 */
XXBYTE_API xxbyte_status xxbyte_luajit_get_lines(const uint8_t *data, size_t size, const xxbyte_luajit_header *header, const xxbyte_luajit_proto *proto, uint32_t *lines,
                                                 size_t count);

/* One local variable record of the debug info (varinfo). */
typedef struct xxbyte_luajit_var {
    uint32_t name_offset; /* the name's bytes (0 for a builtin name) */
    uint32_t name_length;
    uint32_t builtin;  /* 1 to 6 for the internal for-loop variables
                          (xxbyte_luajit_builtin_name), else 0 */
    uint32_t start_pc; /* first pc where the variable is live, numbered
                          as luajit -bl numbers them (0 is the header) */
    uint32_t end_pc;   /* first pc where it is dead */
} xxbyte_luajit_var;

/*
 * Reads the local variable records of *proto into vars[0 ..
 * min(capacity, count)) (vars may be NULL when capacity is 0); *count
 * receives the number of records up to the end marker. Returns
 * XXBYTE_STATUS_INVALID_ARGUMENT when the prototype has no debug info and
 * XXBYTE_STATUS_INVALID_INSTRUCTION when the records run past it.
 */
XXBYTE_API xxbyte_status xxbyte_luajit_get_vars(const uint8_t *data, size_t size, const xxbyte_luajit_proto *proto, xxbyte_luajit_var *vars, size_t capacity,
                                                size_t *count);

/* The variable in slot at pc as debug_varname finds it in vars[0..count),
 * or XXBYTE_LUAJIT_NO_VAR. */
#define XXBYTE_LUAJIT_NO_VAR 0xFFFFFFFFu
XXBYTE_API uint32_t xxbyte_luajit_var_slot(const xxbyte_luajit_var *vars, size_t count, uint32_t slot, uint32_t pc);

/* "(for index)", "(for limit)", "(for step)", "(for generator)", "(for
 * state)" or "(for control)" for builtin 1 to 6, else NULL. */
XXBYTE_API const char *xxbyte_luajit_builtin_name(uint32_t builtin);

/*
 * Whole dumps (xxbyte_disasm_file, xxbyte_cli -F): a bytecode dump as
 * luajit -b writes it (2.0 and 2.1, with or without debug info, either
 * byte order), listed as luajit -bl lists it: every prototype in -bl's
 * order (the children, last constant first, before their parent, the main
 * chunk last) under its header line
 *
 *   -- BYTECODE -- small_lj.lua:4-8
 *
 * (the location of pc 0 as lj_debug_pushloc gives it, and the last line),
 * then its code as xxbyte_disasm_buf_ex lists it from address 4 (pc 1)
 * with xxbyte_luajit_resolve, then a blank line. mode XXBYTE_MODE_AUTO
 * takes the mode from the header (XXBYTE_LUAJIT_MODE_2_0 or _2_1, with
 * _BE for the BE flag); any other valid mode overrides it. Header and
 * walker failures return the statuses of xxbyte_luajit_parse_header and
 * xxbyte_luajit_find_functions.
 *
 * Differences from -bl's text: no pc column and no "=>" jump-target
 * marker (branch operands still print "=> NNNN"); a stripped dump has no
 * chunk name, so its location prints "?" where -bl prints the name the
 * loader was given; a chunk name longer than 40 bytes, which -bl prints as
 * the prototype's address (%p), prints as "0x" and the prototype's file
 * offset.
 */
/*
 * Names for formatting a dump's code: the context of xxbyte_luajit_resolve.
 * Fill it with xxbyte_luajit_names_init, then set function (and optionally
 * the caches) before formatting each prototype's code, decoded at address
 * 4 * pc as the notes above say.
 */
typedef struct xxbyte_luajit_names {
    const uint8_t *data;
    size_t size;
    const xxbyte_luajit_header *header;
    const xxbyte_luajit_function *functions; /* xxbyte_luajit_find_functions */
    size_t function_count;
    uint32_t function; /* index of the prototype being formatted */
    uint32_t reserved;
    /* Optional caches for that prototype; NULL reads from data. */
    const xxbyte_luajit_kgc *kgc;         /* sizekgc entries, dump order */
    const xxbyte_luajit_knum *knum;       /* sizekn entries */
    const xxbyte_luajit_string *uv_names; /* sizeuv entries */
    const uint32_t *children;             /* sizekgc entries: the function
                                             index of each child
                                             constant, else
                                             XXBYTE_LUAJIT_NO_PARENT */
} xxbyte_luajit_names;

/* Fills *names with the arguments, function 0 and no caches. */
XXBYTE_API void xxbyte_luajit_names_init(xxbyte_luajit_names *names, const uint8_t *data, size_t size, const xxbyte_luajit_header *header,
                                         const xxbyte_luajit_function *functions, size_t function_count);

/*
 * A ready-made xxbyte_resolver; context is an xxbyte_luajit_names. Gives
 * luajit -bl's text for the INDEX operands of the prototype's code:
 *   - str: the string quoted, control characters as \n, \r, \t or \ddd,
 *     and longer than 40 bytes cut to its first 40 escaped characters
 *     with "~" after the closing quote ("hi\n", "0123...789"~);
 *   - num: the number as tostring prints it ("%.14g": 1.5, 1e+100);
 *     for TSETM the value minus 2^52, the first array index;
 *   - func: the child's location, chunk name and first line (test.lua:4;
 *     "?" stands for the name of a stripped dump, which only the loader
 *     knows);
 *   - uv: the upvalue's debug name (empty in stripped dumps).
 * It declines tab and cdata operands, as -bl shows no value for them,
 * branches, and anything outside the prototype.
 *
 * Passed itself (not wrapped) to xxbyte_format_ex, xxbyte_format_buf or
 * xxbyte_disasm_buf_ex, it makes the formatter keep the numbers and add
 * -bl's comment instead: "GGET 0 0 ; "print"", "USETS 0 1 ; x ; "s"",
 * "FNEW 2 2 ; t.lua:4", "ADDVN 5 0 0 ; 1.5".
 */
XXBYTE_API size_t xxbyte_luajit_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
