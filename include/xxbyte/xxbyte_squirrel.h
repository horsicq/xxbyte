#ifndef XXBYTE_XXBYTE_SQUIRREL_H
#define XXBYTE_XXBYTE_SQUIRREL_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_SQUIRREL
#error "the Squirrel bytecode decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Squirrel (squirrel-lang.org) compiled scripts: the instructions of a
 * function prototype, as SQFunctionProto keeps them and sq_writeclosure
 * saves them in a .cnut file.
 *
 * Decode mode: the release series whose sqopcodes.h and sqvm.cpp apply,
 *   - XXBYTE_SQUIRREL_MODE(2, 2): Squirrel 2.2 (2.2.5 stable);
 *   - XXBYTE_SQUIRREL_MODE(3, 0): Squirrel 3.0 (3.0.x stable, verified
 *     against 3.0.7);
 *   - XXBYTE_SQUIRREL_MODE(3, 1): Squirrel 3.1;
 *   - XXBYTE_SQUIRREL_MODE(3, 2): Squirrel 3.2 and the later sources of
 *     github.com/albertodemichelis/squirrel (the default);
 * optionally combined with
 *   - XXBYTE_SQUIRREL_MODE_BIG_ENDIAN: the instruction's 32-bit _arg1 is
 *     big-endian (a big-endian host's .cnut; the default is little-endian);
 *   - XXBYTE_SQUIRREL_MODE_INT64: a 64-bit SQInteger build (_SQ64, which
 *     every LP64 and Win64 build of 3.x defines). Only 3.0 differs: it
 *     zero-extends the _arg1 of LOADINT and of an integer APPENDARRAY to
 *     64 bits, where 2.2 and 3.1+ sign-extend it;
 *   - XXBYTE_SQUIRREL_MODE_DOUBLE: a SQUSEDOUBLE build (8-byte SQFloat).
 *     The compiler then never emits LOADFLOAT or a float APPENDARRAY; the VM
 *     would read the whole 8-byte instruction as the double, and so does
 *     xxbyte (a FLOAT operand of size 8).
 * Any other mode or bit is XXBYTE_STATUS_UNSUPPORTED_MODE. 3.0, 3.1 and
 * 3.2 share one opcode table (61 opcodes); they differ in what the VM makes
 * of a few fields: 3.2 CLOSURE binds the environment in _arg2, and 3.0
 * LOADINT zero-extends (above). 2.2 has its own table (61 opcodes).
 *
 * Every instruction is one 8-byte SQInstruction, the same in every build
 * (32- and 64-bit, char and wchar_t):
 *   bytes 0-3  _arg1  SQInt32 (in the byte order of the build)
 *   byte  4    op     opcode
 *   byte  5    _arg0
 *   byte  6    _arg2
 *   byte  7    _arg3
 * so xxbyte_unit_size is 8. An opcode beyond the series' table, a 3.x
 * NEWOBJ whose _arg3 is not table (0), array (1) or class (2), and a 3.x
 * APPENDARRAY whose _arg2 is not stack (0), literal (1), int (2), float (3)
 * or bool (4) are XXBYTE_STATUS_INVALID_INSTRUCTION (the VM asserts on
 * them); fewer than 8 bytes are XXBYTE_STATUS_TRUNCATED. Fields an opcode
 * does not use are ignored, as the VM ignores them.
 *
 * Addresses: pass the byte offset of the instruction in the function's
 * instruction array (pc * 8). BRANCH operands hold absolute byte targets,
 * as the VM computes them from the signed _arg1 after it has advanced past
 * the instruction: address + 8 * (1 + _arg1) for JMP, JZ, JNZ, JCMP, AND,
 * OR, FOREACH (the loop exit) and PUSHTRAP (the handler), and
 * address + 8 * _arg1 for POSTFOREACH.
 *
 * There is no reference text form (the compiler's _DEBUG_DUMP prints the
 * raw fields); xxbyte prints the opcode name of sqopcodes.h without its
 * "_OP_" prefix and the operands the VM reads, comma-separated, in the order
 * "destination, sources", binary operators with their left operand first
 * (ADD r0, r2, r1 is r0 = r2 + r1: the VM's TARGET = STK(arg2) + STK(arg1)),
 * and an operator, when the instruction has one, last before the branch:
 *   rN       a stack register (local or temporary)
 *   -        no register (0xFF: a result that is dropped, a value-less
 *            RETURN or YIELD, no base class or attributes)
 *   kN       literal N of the function (sq _literals)
 *   oN       outer value N of the closure
 *   fN       nested function N of the prototype (CLOSURE)
 *   N        an integer: line, count, increment, flags, LOADINT value
 *   F        a float, in xxbyte_fmt_float/xxbyte_fmt_double form
 *   0xN      a branch target
 *   true, false, table, array, class
 *            LOADBOOL and bool APPENDARRAY values, the NEWOBJ kind
 *   & | ^ << >> >>>        BITW operators (_arg3 0, 2, 3, 4, 5, 6)
 *   > >= < <= <=>          CMP and JCMP operators (_arg3 0, 2, 3, 4, 5;
 *                          5 only in 3.x)
 *   + - * / %              ARITH (2.2), COMPARITH and COMPARITHL operators
 *                          (the operator character in _arg3)
 * an operator code outside these lists prints as its decimal value.
 *
 *   00 00 00 00 01 02 00 00            LOAD r2, k0
 *   07 00 00 00 02 01 00 00            LOADINT r1, 7
 *   F9 FF FF FF 1C 00 00 00 (addr 112) JMP 0x40
 *   02 00 00 00 1D 00 01 03 (addr 8)   JCMP r1, r0, <, 0x20
 *   07 00 00 00 06 FF 08 02            CALL -, r7, r8, 2
 *   01 00 00 00 17 01 00 00            RETURN r1
 *   00 00 00 00 17 FF 00 00            RETURN
 *   01 00 00 00 30 04 FF 00 (3.2)      CLOSURE r4, f1
 *
 * Per opcode (3.x unless marked; 2.2 opcodes that share a 3.x name read the
 * same fields unless noted):
 *   LINE line                  LOAD r, k            LOADINT r, int
 *   LOADFLOAT r, float         DLOAD r, k, r, k     MOVE r, r
 *   TAILCALL t, func, base, nargs     CALL t, func, base, nargs
 *       (t is "-" for no result; 2.2 always names a register)
 *   PREPCALL r, obj, keyreg, thisreg  PREPCALLK r, obj, k, thisreg
 *   GETK r, obj, k            GET r, obj, key       DELETE r, obj, key
 *   SET t, obj, key, val      NEWSLOT t, obj, key, val
 *       (2.2: t is a register, written when it differs from val)
 *   NEWSLOTA flags, obj, key, val (flags: 1 attributes, 2 static)
 *   EQ / NE r, left, right    (right is kN when _arg3 != 0, else rN)
 *   ADD SUB MUL DIV MOD r, left, right   BITW r, left, right, op
 *   ARITH (2.2) r, left, right, op       CMP r, left, right, op
 *   EXISTS r, obj, key        INSTANCEOF r, instance, class
 *   NEG NOT BWNOT CLONE TYPEOF r, r      GETBASE r     LOADROOT r
 *   RETURN [r]                LOADNULLS r, count    LOADBOOL r, bool
 *   DMOVE r, r, r, r          JMP target            JZ / JNZ (2.2) r, target
 *   JCMP left, right, op, target         AND / OR r, value, target
 *   GETOUTER r, o             SETOUTER t, o, value  LOADFREEVAR (2.2) r, o
 *   NEWOBJ r, table|array, size          NEWOBJ r, class, base, attributes
 *   NEWTABLE / NEWARRAY (2.2) r, size    CLASS (2.2) r, base, attributes
 *   APPENDARRAY array, value (rN, kN, int, float or bool by _arg2; 2.2: kN
 *       when _arg3 != 0, else rN)
 *   COMPARITH r, obj, key, value, op     COMPARITHL (2.2) r, local, value, op
 *   INC / PINC r, obj, key, step         INCL local, step (2.2: INCL r,
 *       local, step)                     PINCL r, local, step
 *   CLOSURE r, f [, env] (env only in 3.2, when _arg2 != 0xFF)
 *   YIELD value, saved (3.x: saved is the number of registers the
 *       generator keeps), YIELD [value] (2.2)
 *   RESUME r, generator       FOREACH container, first, exit-target
 *       (first: the key register, value and iterator follow it)
 *   POSTFOREACH generator, target        PUSHTRAP r, handler
 *   POPTRAP count             THROW r      CLOSE r (the first register to
 *   close)                    VARGC (2.2) r   GETVARGV (2.2) r, index
 *   GETPARENT (2.2) r, r      DELEGATE (2.2) r, table, delegate
 *
 * Operands, in printed order (an operand.extra holds the raw field value,
 * operand.size the field width in bytes: 4 for _arg1, 2 for a COMPARITH
 * half of it, 1 for the byte fields):
 *   - XXBYTE_OPERAND_REGISTER: registers; "-" is a REGISTER with
 *     XXBYTE_SQUIRREL_OPERAND_FLAG_NONE and value 255 (or -1 for a NEWOBJ or
 *     CLASS base);
 *   - XXBYTE_OPERAND_INDEX: XXBYTE_INDEX_CONSTANT (literals),
 *     XXBYTE_INDEX_UPVALUE (outer values) and XXBYTE_INDEX_FUNCTION (nested
 *     functions); value is the 0-based index;
 *   - XXBYTE_OPERAND_IMMEDIATE: integers (XXBYTE_OPERAND_FLAG_SIGNED when
 *     signed), and the operator, bool and NEWOBJ-kind operands with
 *     XXBYTE_SQUIRREL_OPERAND_FLAG_OPERATOR, _BOOL or _KIND (value: the raw
 *     code, 0/1, the NOT_* kind);
 *   - XXBYTE_OPERAND_FLOAT: LOADFLOAT and float APPENDARRAY values;
 *   - XXBYTE_OPERAND_BRANCH: targets.
 *
 * Resolver: called for every displayed INDEX and BRANCH operand and, unlike
 * most families, for every REGISTER operand that is not "-"; resolved text
 * replaces the operand's numeric form. xxbyte_disasm_file resolves literals
 * (quoted strings, numbers, true, false, null), outer and nested function
 * names, and registers that hold a named local at the instruction (from the
 * local variable infos).
 *
 * Groups: JMP: JUMP|STOP; JZ, JNZ, JCMP, AND, OR, FOREACH, POSTFOREACH:
 * CONDITIONAL; CALL and RESUME: CALL; TAILCALL: CALL|RETURN (a native or
 * non-closure callee continues at the next instruction); RETURN:
 * RETURN|STOP; YIELD: RETURN (the generator resumes after it); THROW:
 * THROW|STOP; everything else NONE (PUSHTRAP's handler is a BRANCH operand
 * of a NONE instruction).
 */
#define XXBYTE_SQUIRREL_MODE(major, minor) ((((uint32_t)(major) & 0xFFu) << 8) | ((uint32_t)(minor) & 0xFFu))
#define XXBYTE_SQUIRREL_MODE_MAJOR(mode) (((uint32_t)(mode) >> 8) & 0xFFu)
#define XXBYTE_SQUIRREL_MODE_MINOR(mode) ((uint32_t)(mode) & 0xFFu)
#define XXBYTE_SQUIRREL_MODE_BIG_ENDIAN 0x10000u
#define XXBYTE_SQUIRREL_MODE_INT64 0x20000u
#define XXBYTE_SQUIRREL_MODE_DOUBLE 0x40000u
#define XXBYTE_SQUIRREL_MODE_DEFAULT XXBYTE_SQUIRREL_MODE(3, 2)

/* Family operand flags. */
#define XXBYTE_SQUIRREL_OPERAND_FLAG_NONE 0x1000u     /* "-": no register */
#define XXBYTE_SQUIRREL_OPERAND_FLAG_OPERATOR 0x2000u /* BITW/CMP code or arith character */
#define XXBYTE_SQUIRREL_OPERAND_FLAG_BOOL 0x4000u     /* true / false */
#define XXBYTE_SQUIRREL_OPERAND_FLAG_KIND 0x8000u     /* NEWOBJ table / array / class */

/* Mnemonic identifiers (xxbyte_instruction.id): the 3.x opcodes in opcode
 * order, then the names only 2.2 has. */
typedef enum xxbyte_squirrel_id {
    XXBYTE_SQUIRREL_ID_INVALID = 0,
    XXBYTE_SQUIRREL_ID_LINE,
    XXBYTE_SQUIRREL_ID_LOAD,
    XXBYTE_SQUIRREL_ID_LOADINT,
    XXBYTE_SQUIRREL_ID_LOADFLOAT,
    XXBYTE_SQUIRREL_ID_DLOAD,
    XXBYTE_SQUIRREL_ID_TAILCALL,
    XXBYTE_SQUIRREL_ID_CALL,
    XXBYTE_SQUIRREL_ID_PREPCALL,
    XXBYTE_SQUIRREL_ID_PREPCALLK,
    XXBYTE_SQUIRREL_ID_GETK,
    XXBYTE_SQUIRREL_ID_MOVE,
    XXBYTE_SQUIRREL_ID_NEWSLOT,
    XXBYTE_SQUIRREL_ID_DELETE,
    XXBYTE_SQUIRREL_ID_SET,
    XXBYTE_SQUIRREL_ID_GET,
    XXBYTE_SQUIRREL_ID_EQ,
    XXBYTE_SQUIRREL_ID_NE,
    XXBYTE_SQUIRREL_ID_ADD,
    XXBYTE_SQUIRREL_ID_SUB,
    XXBYTE_SQUIRREL_ID_MUL,
    XXBYTE_SQUIRREL_ID_DIV,
    XXBYTE_SQUIRREL_ID_MOD,
    XXBYTE_SQUIRREL_ID_BITW,
    XXBYTE_SQUIRREL_ID_RETURN,
    XXBYTE_SQUIRREL_ID_LOADNULLS,
    XXBYTE_SQUIRREL_ID_LOADROOT,
    XXBYTE_SQUIRREL_ID_LOADBOOL,
    XXBYTE_SQUIRREL_ID_DMOVE,
    XXBYTE_SQUIRREL_ID_JMP,
    XXBYTE_SQUIRREL_ID_JCMP,
    XXBYTE_SQUIRREL_ID_JZ,
    XXBYTE_SQUIRREL_ID_SETOUTER,
    XXBYTE_SQUIRREL_ID_GETOUTER,
    XXBYTE_SQUIRREL_ID_NEWOBJ,
    XXBYTE_SQUIRREL_ID_APPENDARRAY,
    XXBYTE_SQUIRREL_ID_COMPARITH,
    XXBYTE_SQUIRREL_ID_INC,
    XXBYTE_SQUIRREL_ID_INCL,
    XXBYTE_SQUIRREL_ID_PINC,
    XXBYTE_SQUIRREL_ID_PINCL,
    XXBYTE_SQUIRREL_ID_CMP,
    XXBYTE_SQUIRREL_ID_EXISTS,
    XXBYTE_SQUIRREL_ID_INSTANCEOF,
    XXBYTE_SQUIRREL_ID_AND,
    XXBYTE_SQUIRREL_ID_OR,
    XXBYTE_SQUIRREL_ID_NEG,
    XXBYTE_SQUIRREL_ID_NOT,
    XXBYTE_SQUIRREL_ID_BWNOT,
    XXBYTE_SQUIRREL_ID_CLOSURE,
    XXBYTE_SQUIRREL_ID_YIELD,
    XXBYTE_SQUIRREL_ID_RESUME,
    XXBYTE_SQUIRREL_ID_FOREACH,
    XXBYTE_SQUIRREL_ID_POSTFOREACH,
    XXBYTE_SQUIRREL_ID_CLONE,
    XXBYTE_SQUIRREL_ID_TYPEOF,
    XXBYTE_SQUIRREL_ID_PUSHTRAP,
    XXBYTE_SQUIRREL_ID_POPTRAP,
    XXBYTE_SQUIRREL_ID_THROW,
    XXBYTE_SQUIRREL_ID_NEWSLOTA,
    XXBYTE_SQUIRREL_ID_GETBASE,
    XXBYTE_SQUIRREL_ID_CLOSE,
    /* 2.2 only */
    XXBYTE_SQUIRREL_ID_ARITH,
    XXBYTE_SQUIRREL_ID_LOADROOTTABLE,
    XXBYTE_SQUIRREL_ID_JNZ,
    XXBYTE_SQUIRREL_ID_LOADFREEVAR,
    XXBYTE_SQUIRREL_ID_VARGC,
    XXBYTE_SQUIRREL_ID_GETVARGV,
    XXBYTE_SQUIRREL_ID_NEWTABLE,
    XXBYTE_SQUIRREL_ID_NEWARRAY,
    XXBYTE_SQUIRREL_ID_GETPARENT,
    XXBYTE_SQUIRREL_ID_COMPARITHL,
    XXBYTE_SQUIRREL_ID_DELEGATE,
    XXBYTE_SQUIRREL_ID_CLASS,
    XXBYTE_SQUIRREL_ID_COUNT
} xxbyte_squirrel_id;

/* The mnemonic of an id, or NULL. */
XXBYTE_API const char *xxbyte_squirrel_mnemonic(uint32_t id);
/* The name of opcode in the table of mode's series (flags are ignored), or
 * NULL when the mode is not a supported series or the opcode is undefined. */
XXBYTE_API const char *xxbyte_squirrel_opname(uint32_t mode, uint32_t opcode);

/*
 * .cnut files (sq_writeclosure, read back by sq_readclosure):
 *   u16 0xFAFA (SQ_BYTECODE_STREAM_TAG), then the closure:
 *   u32 'SQIR' (bytes "RIQS" little-endian, "SQIR" big-endian),
 *   u32 sizeof(SQChar), and in 3.x u32 sizeof(SQInteger) and
 *   u32 sizeof(SQFloat), the main function prototype, u32 'TAIL'.
 * A prototype is a sequence of parts, each led by a u32 'PART' tag:
 *   source name and name (objects); the SQInteger counts of literals,
 *   parameters, outer values, local var infos, line infos, default
 *   parameters, instructions and nested functions; the literals (objects);
 *   the parameter names (objects); the outer values (SQUnsignedInteger
 *   type, source object, name object); the local var infos (name object,
 *   SQUnsignedInteger pos, start_op, end_op); the line infos (SQInteger
 *   line, op); the default parameters (SQInteger stack positions); the
 *   instructions (8 bytes each); the nested prototypes; and, without a tag,
 *   SQInteger stacksize, bool bgenerator (1 byte) and varparams (SQInteger
 *   in 3.x, a 1-byte bool in 2.2).
 * In 2.2 the tags ('SQIR', the character size, 'PART', 'TAIL') are
 * SQIntegers (8 bytes in a 64-bit build) rather than u32s.
 * An object is a u32 SQObjectType followed by an SQInteger length and the
 * characters (string), an SQInteger (integer, and bool in 3.x), an SQFloat
 * (float) or nothing (null). Every field is in the byte order of the
 * writing host.
 */
#define XXBYTE_SQUIRREL_OBJECT_NULL 0x01000001u
#define XXBYTE_SQUIRREL_OBJECT_INTEGER 0x05000002u
#define XXBYTE_SQUIRREL_OBJECT_FLOAT 0x05000004u
#define XXBYTE_SQUIRREL_OBJECT_BOOL 0x01000008u
#define XXBYTE_SQUIRREL_OBJECT_STRING 0x08000010u

#define XXBYTE_SQUIRREL_NO_PARENT 0xFFFFFFFFu
/* Prototypes nested deeper than this are XXBYTE_STATUS_INVALID_INSTRUCTION. */
#define XXBYTE_SQUIRREL_MAX_DEPTH 200u

typedef struct xxbyte_squirrel_header {
    uint32_t mode;            /* the decode mode the file implies (see
                                 xxbyte_squirrel_parse_header) */
    uint32_t major;           /* 2 or 3 */
    uint32_t big_endian;      /* 1 for a big-endian file */
    uint32_t char_size;       /* sizeof(SQChar): 1, 2 or 4 */
    uint32_t int_size;        /* sizeof(SQInteger): 4 or 8 */
    uint32_t float_size;      /* sizeof(SQFloat): 4 or 8 */
    uint64_t function_offset; /* the main prototype's first PART tag */
    uint64_t end_offset;      /* one past the TAIL tag */
} xxbyte_squirrel_header;

/*
 * Parses the stream tag and closure header at data[0] and walks the whole
 * file (as xxbyte_squirrel_find_functions does) to check it. 2.2 files do
 * not record sizeof(SQInteger) and sizeof(SQFloat): the integer size is the
 * width of their tags, and the float size the first of 4 and 8 with which
 * the file parses (a file without float literals reports 4; when neither
 * parses, XXBYTE_STATUS_TRUNCATED if either walk ran past the end). mode
 * receives the series and flags: 2.2 for a 2.2 header; for 3.x the files
 * of 3.0, 3.1 and 3.2 look alike, so mode is 3.2 when a CLOSURE instruction has an _arg2 other than 0 or 1 (3.2 writes
 * 0xFF there when it binds no environment) or when there is no CLOSURE,
 * and 3.1 otherwise (3.0 is never inferred: it decodes as 3.1 except for a
 * negative LOADINT in a 64-bit build); plus XXBYTE_SQUIRREL_MODE_BIG_ENDIAN,
 * _INT64 and _DOUBLE from the header. Returns
 * XXBYTE_STATUS_INVALID_INSTRUCTION when data is not a .cnut (bad tags or
 * sizes), XXBYTE_STATUS_TRUNCATED when it ends early, and the statuses of
 * xxbyte_squirrel_find_functions. Trailing bytes after TAIL are ignored.
 */
XXBYTE_API xxbyte_status xxbyte_squirrel_parse_header(const uint8_t *data, size_t size, xxbyte_squirrel_header *header);

/* One serialized object. */
typedef struct xxbyte_squirrel_object {
    uint32_t type;          /* XXBYTE_SQUIRREL_OBJECT_* */
    uint32_t size;          /* INTEGER, BOOL: int_size; FLOAT: float_size;
                               0 otherwise */
    int64_t value;          /* INTEGER, BOOL: the value (sign-extended);
                               FLOAT: the IEEE-754 bits */
    uint64_t string_offset; /* STRING: first byte */
    uint64_t string_length; /* STRING: length in characters (char_size
                               bytes each) */
    uint64_t offset;        /* the record */
    uint64_t end_offset;    /* one past it */
} xxbyte_squirrel_object;

/* Reads the object record at data[offset]. XXBYTE_STATUS_INVALID_INSTRUCTION
 * for a type that cannot be serialized (or BOOL in 2.2) or a negative
 * string length, XXBYTE_STATUS_TRUNCATED when it runs past size. */
XXBYTE_API xxbyte_status xxbyte_squirrel_read_object(const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, uint64_t offset,
                                                     xxbyte_squirrel_object *object);

/* One function prototype. Offsets are from data[0]. */
typedef struct xxbyte_squirrel_function {
    uint32_t parent;    /* index of the enclosing prototype, or
                           XXBYTE_SQUIRREL_NO_PARENT for the main one */
    uint32_t child;     /* position in the parent's function list (the
                           CLOSURE fN) */
    uint32_t depth;     /* 0 for the main function */
    uint32_t generator; /* bgenerator */
    uint32_t varparams; /* varparams != 0 */
    int64_t stack_size;
    uint64_t offset;        /* the first PART tag */
    uint64_t end_offset;    /* one past varparams */
    uint64_t source_offset; /* the source name object */
    uint64_t name_offset;   /* the name object */
    uint32_t literal_count;
    uint32_t parameter_count;
    uint32_t outer_count;
    uint32_t local_count;
    uint32_t lineinfo_count;
    uint32_t default_count;
    uint32_t instruction_count;
    uint32_t function_count;
    uint64_t literals_offset;     /* first literal object */
    uint64_t parameters_offset;   /* first parameter name object */
    uint64_t outers_offset;       /* first outer value record */
    uint64_t locals_offset;       /* first local var info record */
    uint64_t lineinfos_offset;    /* lineinfo_count (line, op) SQInteger pairs */
    uint64_t defaults_offset;     /* default_count SQIntegers */
    uint64_t instructions_offset; /* instruction_count * 8 bytes */
} xxbyte_squirrel_function;

/*
 * Walks the prototypes of the file whose header is *header and stores the
 * first min(capacity, count) of them, in pre-order (a prototype before its
 * nested ones), in functions (NULL when capacity is 0); *count receives the
 * number of prototypes. Every tag, count and record is checked:
 * XXBYTE_STATUS_TRUNCATED when one runs past size,
 * XXBYTE_STATUS_INVALID_INSTRUCTION for a wrong tag, a negative count, an
 * object that cannot be serialized or nesting deeper than
 * XXBYTE_SQUIRREL_MAX_DEPTH. The work is linear in size.
 */
XXBYTE_API xxbyte_status xxbyte_squirrel_find_functions(const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, xxbyte_squirrel_function *functions,
                                                        size_t capacity, size_t *count);

/* Reads count consecutive objects of a list of *function, starting at
 * object first: list is 0 for the literals and 1 for the parameter names.
 * XXBYTE_STATUS_INVALID_ARGUMENT when the range is outside the list or
 * objects is NULL with a nonzero count. */
XXBYTE_API xxbyte_status xxbyte_squirrel_get_objects(const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, const xxbyte_squirrel_function *function,
                                                     uint32_t list, uint32_t first, xxbyte_squirrel_object *objects, size_t count);

typedef struct xxbyte_squirrel_local {
    xxbyte_squirrel_object name;
    uint64_t pos;      /* register */
    uint64_t start_op; /* first pc (instruction index) where it is live */
    uint64_t end_op;   /* last pc */
} xxbyte_squirrel_local;

/* Reads the local var infos of *function into locals[0..local_count). */
XXBYTE_API xxbyte_status xxbyte_squirrel_get_locals(const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, const xxbyte_squirrel_function *function,
                                                    xxbyte_squirrel_local *locals);

typedef struct xxbyte_squirrel_outer {
    uint64_t type; /* 3.x: 0 local, 1 outer; 2.2: 0 local, 1 symbol, 2 outer */
    xxbyte_squirrel_object source;
    xxbyte_squirrel_object name;
} xxbyte_squirrel_outer;

/* Reads the outer values of *function into outers[0..outer_count). */
XXBYTE_API xxbyte_status xxbyte_squirrel_get_outers(const uint8_t *data, size_t size, const xxbyte_squirrel_header *header, const xxbyte_squirrel_function *function,
                                                    xxbyte_squirrel_outer *outers);

/*
 * xxbyte_disasm_file (list_file) reads .cnut files of every series, byte
 * order, character, integer and float size. Each prototype, in pre-order,
 * gets a header line and a listing:
 *   function NAME <SOURCE> (N instructions at 0xOFFSET)
 *   P[+] params (NAMES), L literals, V locals, O outers, D defaults,
 *   F functions, stack S[, generator]
 * ("+" for varparams; NAME and SOURCE are the name and source objects
 * unquoted, "?" when they are not strings), then the instructions at
 * addresses from 0 with the resolver described above, each prototype after
 * the first preceded by an empty line. With XXBYTE_MODE_AUTO the mode is
 * the header's; another mode must be of the file's major series (2 or 3),
 * else XXBYTE_STATUS_UNSUPPORTED_MODE, and replaces the inferred one
 * (including its flags). Resolved literal strings are cut after 200
 * characters ("..."); \n \r \t \\ and \" print as such, other characters
 * outside printable ASCII as \xNN (control characters, and every byte of a
 * 1-byte SQChar string) or \uNNNN / \UNNNNNNNN (other wchar_t ones).
 *
 * Limitations: the text form is xxbyte's own (Squirrel has no
 * disassembler; the compiler's _DEBUG_DUMP prints raw fields, which xxbyte
 * was checked against field by field). A .cnut does not say which 3.x
 * release wrote it (see xxbyte_squirrel_parse_header). Registers are named
 * only where a local var info covers them; temporaries keep rN. Forks with
 * their own opcode tables (quirrel, Electric Imp and other embedders'
 * modified VMs) are not supported.
 */

#ifdef __cplusplus
}
#endif

#endif
