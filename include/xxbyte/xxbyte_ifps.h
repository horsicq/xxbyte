#ifndef XXBYTE_XXBYTE_IFPS_H
#define XXBYTE_XXBYTE_IFPS_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_IFPS
#error "the PascalScript (IFPS) decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * RemObjects PascalScript ("IFPS", Innerfuse Pascal Script 3) compiled
 * bytecode, as run by uPSRuntime.pas and by Inno Setup's [Code] section.
 *
 * Every IFPS build the runtime loads (PSLowBuildSupport 12 to
 * PSCurrentBuildNo 23) uses the same instruction set and operand rules:
 * build 22 (RemObjects sources of 2006; ANSI Inno Setup, seen in 4.1.4 to
 * 5.5.7) and build 23 (2007 on; Unicode Inno Setup, seen in 5.3.8 to
 * 6.4.2) differ only in the container (see xxbyte_ifps_parse_script), and
 * ANSI and Unicode string types are distinct base types in the script's
 * type table.
 * The one encoding difference is the width of Extended immediates, which
 * is SizeOf(Extended) of the compiler that built the runtime the script
 * was compiled for. Nothing in the script records it:
 *
 *   XXBYTE_IFPS_MODE_32BIT  10-byte (x87) Extended: every 32-bit x86
 *                           runtime (Delphi Win32, FPC i386), so every
 *                           Inno Setup release up to 6.x and the default
 *                           32-bit installers of Inno Setup 7, and FPC
 *                           x86_64 on non-Windows targets.
 *   XXBYTE_IFPS_MODE_64BIT  8-byte Extended (an alias of Double): Delphi
 *                           Win64, so Inno Setup 7 64-bit installers
 *                           (SetupArchitecture x64), FPC Win64, and ARM
 *                           and other non-x86 targets.
 *
 * Runtimes with a 16-byte Extended (Delphi Linux64 and Intel macOS64) are
 * not supported: either mode mis-sizes their Extended immediates and
 * attribute values.
 *
 * XXBYTE_IFPS_MODE_DEFAULT is XXBYTE_IFPS_MODE_32BIT. Other values return
 * XXBYTE_STATUS_UNSUPPORTED_MODE.
 */
#define XXBYTE_IFPS_MODE_DEFAULT 0u
#define XXBYTE_IFPS_MODE_32BIT 32u
#define XXBYTE_IFPS_MODE_64BIT 64u

/*
 * Base types (uPSUtils.pas bt*). The type table stores one per type, with
 * bit 7 set when the type has an export name; xxbyte_ifps_type.base_type
 * has that bit cleared.
 */
typedef enum xxbyte_ifps_base_type {
    XXBYTE_IFPS_BT_RETURN_ADDRESS = 0,
    XXBYTE_IFPS_BT_U8 = 1,
    XXBYTE_IFPS_BT_S8 = 2,
    XXBYTE_IFPS_BT_U16 = 3,
    XXBYTE_IFPS_BT_S16 = 4,
    XXBYTE_IFPS_BT_U32 = 5,
    XXBYTE_IFPS_BT_S32 = 6,
    XXBYTE_IFPS_BT_SINGLE = 7,
    XXBYTE_IFPS_BT_DOUBLE = 8,
    XXBYTE_IFPS_BT_EXTENDED = 9,
    XXBYTE_IFPS_BT_STRING = 10, /* AnsiString */
    XXBYTE_IFPS_BT_RECORD = 11,
    XXBYTE_IFPS_BT_ARRAY = 12,
    XXBYTE_IFPS_BT_POINTER = 13,
    XXBYTE_IFPS_BT_PCHAR = 14,
    XXBYTE_IFPS_BT_RESOURCE_POINTER = 15,
    XXBYTE_IFPS_BT_VARIANT = 16,
    XXBYTE_IFPS_BT_S64 = 17,
    XXBYTE_IFPS_BT_CHAR = 18, /* AnsiChar */
    XXBYTE_IFPS_BT_WIDE_STRING = 19,
    XXBYTE_IFPS_BT_WIDE_CHAR = 20,
    XXBYTE_IFPS_BT_PROC_PTR = 21,
    XXBYTE_IFPS_BT_STATIC_ARRAY = 22,
    XXBYTE_IFPS_BT_SET = 23,
    XXBYTE_IFPS_BT_CURRENCY = 24,
    XXBYTE_IFPS_BT_CLASS = 25,
    XXBYTE_IFPS_BT_INTERFACE = 26,
    XXBYTE_IFPS_BT_NOTIFICATION_VARIANT = 27,
    XXBYTE_IFPS_BT_UNICODE_STRING = 28,
    XXBYTE_IFPS_BT_U64 = 29
} xxbyte_ifps_base_type;

/* Opcodes (uPSUtils.pas cm_*). */
#define XXBYTE_IFPS_OP_ASSIGN 0x00u         /* CM_A */
#define XXBYTE_IFPS_OP_CALC 0x01u           /* CM_CA */
#define XXBYTE_IFPS_OP_PUSH 0x02u           /* CM_P */
#define XXBYTE_IFPS_OP_PUSHVAR 0x03u        /* CM_PV */
#define XXBYTE_IFPS_OP_POP 0x04u            /* CM_PO */
#define XXBYTE_IFPS_OP_CALL 0x05u           /* Cm_C */
#define XXBYTE_IFPS_OP_GOTO 0x06u           /* Cm_G */
#define XXBYTE_IFPS_OP_COND_GOTO 0x07u      /* Cm_CG */
#define XXBYTE_IFPS_OP_COND_NOT_GOTO 0x08u  /* Cm_CNG */
#define XXBYTE_IFPS_OP_RET 0x09u            /* Cm_R */
#define XXBYTE_IFPS_OP_SETSTACKTYPE 0x0Au   /* Cm_ST */
#define XXBYTE_IFPS_OP_PUSHTYPE 0x0Bu       /* Cm_Pt */
#define XXBYTE_IFPS_OP_COMPARE 0x0Cu        /* CM_CO */
#define XXBYTE_IFPS_OP_CALLVAR 0x0Du        /* Cm_cv */
#define XXBYTE_IFPS_OP_SETPOINTER 0x0Eu     /* cm_sp */
#define XXBYTE_IFPS_OP_BNOT 0x0Fu           /* cm_bn */
#define XXBYTE_IFPS_OP_MINUS 0x10u          /* cm_vm */
#define XXBYTE_IFPS_OP_SETFLAG 0x11u        /* cm_sf */
#define XXBYTE_IFPS_OP_FLAGGOTO 0x12u       /* cm_fg */
#define XXBYTE_IFPS_OP_PUSHEXCEPTION 0x13u  /* cm_puexh */
#define XXBYTE_IFPS_OP_POPEXCEPTION 0x14u   /* cm_poexh */
#define XXBYTE_IFPS_OP_INOT 0x15u           /* cm_in */
#define XXBYTE_IFPS_OP_SETCOPYPOINTER 0x16u /* cm_spc */
#define XXBYTE_IFPS_OP_INC 0x17u            /* cm_inc */
#define XXBYTE_IFPS_OP_DEC 0x18u            /* cm_dec */
#define XXBYTE_IFPS_OP_POP_GOTO 0x19u       /* Cm_PG */
#define XXBYTE_IFPS_OP_POP2_GOTO 0x1Au      /* Cm_P2G */
#define XXBYTE_IFPS_OP_NOP 0xFFu            /* cm_nop */

/* calc operators (operand 0 of calc). */
#define XXBYTE_IFPS_CALC_ADD 0u
#define XXBYTE_IFPS_CALC_SUB 1u
#define XXBYTE_IFPS_CALC_MUL 2u
#define XXBYTE_IFPS_CALC_DIV 3u
#define XXBYTE_IFPS_CALC_MOD 4u
#define XXBYTE_IFPS_CALC_SHL 5u
#define XXBYTE_IFPS_CALC_SHR 6u
#define XXBYTE_IFPS_CALC_AND 7u
#define XXBYTE_IFPS_CALC_OR 8u
#define XXBYTE_IFPS_CALC_XOR 9u
#define XXBYTE_IFPS_CALC_AS 10u

/* compare operators (operand 0 of compare). */
#define XXBYTE_IFPS_COMPARE_GE 0u
#define XXBYTE_IFPS_COMPARE_LE 1u
#define XXBYTE_IFPS_COMPARE_GT 2u
#define XXBYTE_IFPS_COMPARE_LT 3u
#define XXBYTE_IFPS_COMPARE_NE 4u
#define XXBYTE_IFPS_COMPARE_EQ 5u
#define XXBYTE_IFPS_COMPARE_IN 6u
#define XXBYTE_IFPS_COMPARE_IS 7u

/* popexception positions; the runtime ignores other values. */
#define XXBYTE_IFPS_POPEXCEPTION_TRY 0u      /* end of the try block */
#define XXBYTE_IFPS_POPEXCEPTION_FINALLY 1u  /* end of the first finally */
#define XXBYTE_IFPS_POPEXCEPTION_EXCEPT 2u   /* end of the except block */
#define XXBYTE_IFPS_POPEXCEPTION_FINALLY2 3u /* end of the second finally */

/* Variable addresses: below XXBYTE_IFPS_ADDR_STACK_LIMIT a global variable
 * number, otherwise XXBYTE_IFPS_ADDR_STACK_BASE + n for stack slot
 * Base[n] (32-bit wraparound), where Base[0] holds the return address,
 * Base[1] up are locals and Base[-1] down are the result and parameters. */
#define XXBYTE_IFPS_ADDR_STACK_LIMIT 0x40000000u /* PSAddrNegativeStackStart */
#define XXBYTE_IFPS_ADDR_STACK_BASE 0x60000000u  /* PSAddrStackStart */

/* "None": no main proc, no exception-handler offset (InvalidVal). */
#define XXBYTE_IFPS_NONE 0xFFFFFFFFu

/*
 * Instruction operands.
 *
 * Most instructions take "variables", each encoded as a form byte and a
 * 32-bit address or type number (uPSRuntime.pas ReadVariable). A variable
 * becomes one operand, or two for an element reference:
 *   form 0  variable       INDEX GLOBAL (value = global number, < 2^30),
 *                          INDEX LOCAL (value = n >= 0 of Base[n]) or
 *                          INDEX ARGUMENT (value = n < 0, SIGNED);
 *                          text "GlobalVar[3]", "Base[1]", "Base[-2]"
 *   form 1  immediate      type number and data sized by the type's base
 *                          type (needs a decode context, see below); the
 *                          text is in brackets as uPSDisassembly prints it:
 *                            U8 U16 U32 U64      IMMEDIATE "[200]"
 *                            S8 S16 S32 S64      IMMEDIATE, SIGNED "[-5]"
 *                            AnsiChar WideChar   IMMEDIATE "[#65]"
 *                            Currency            IMMEDIATE, SIGNED: the
 *                                                raw value, 10000 times
 *                                                the number; "[1.5]"
 *                            Single Double       FLOAT (IEEE bits, size 4
 *                                                or 8) "[0.25]"
 *                            Extended            FLOAT of size 8: the
 *                                                10-byte x87 value as the
 *                                                x87 stores it to a double
 *                                                (round half even); an
 *                                                unsupported encoding (an
 *                                                unnormal, pseudo-zero,
 *                                                pseudo-infinity or
 *                                                pseudo-NaN) becomes the
 *                                                default NaN, bits
 *                                                0xFFF8000000000000. In
 *                                                64-bit mode the 8-byte
 *                                                value as is.
 *                            AnsiString PChar    BYTES (the characters
 *                                                after the 32-bit length)
 *                                                "['Setup'#13#10]"
 *                            WideString
 *                            UnicodeString       BYTES of UTF-16LE code
 *                                                units, WIDE "['A'#8364]"
 *                            Set                 BYTES, (bits + 7) / 8
 *                                                bytes, least significant
 *                                                bit first; text lists the
 *                                                members "[[0, 3..5]]"
 *                            ProcPtr             INDEX FUNCTION (the proc
 *                                                number) "[PROC: 12]"
 *                          64-bit integers have the WIDE flag. extra holds
 *                          the type number and base type of the immediate
 *                          and XXBYTE_IFPS_EXTRA_HAS_TYPE.
 *   form 2  constant       the variable as in form 0, followed by INDEX
 *           element        XXBYTE_IFPS_INDEX_ELEMENT (value = record field
 *                          or array element number) "Base[1].[2]"
 *   form 3  variable       the variable as in form 0, followed by INDEX
 *           element        XXBYTE_IFPS_INDEX_ELEMENT_GLOBAL, _LOCAL or
 *                          _ARGUMENT naming the variable that holds the
 *                          element number "Base[1].GlobalVar[0]"
 * An XXBYTE_IFPS_INDEX_ELEMENT* operand always belongs to the operand
 * before it. Strings print as Pascal literals: printable ASCII quoted (a
 * quote doubled), every other byte or code unit as #n, '' when empty.
 *
 * Instructions, in operand order (V = variable, T = absolute BRANCH):
 *   assign V, V            setpointer V: V          setcopypointer V: V
 *   calc V op V            op: IMMEDIATE operand 0, printed between the
 *                          variables: + - * / mod shl shr and or xor as
 *   compare into V: V op V op: IMMEDIATE operand 0: >= <= > < <> = in is;
 *                          the first variable receives the result
 *   push V, pushvar V, callvar V, bnot V, minus V, inot V, inc V, dec V
 *   setflag V / setflag not V
 *                          the negate byte is an IMMEDIATE after the
 *                          variable; any nonzero value negates
 *   call P                 INDEX FUNCTION (proc number)
 *   pushtype t(BaseType)   INDEX TYPE; "(U8)" (xxbyte_ifps_base_type_name,
 *                          or "Unknown n") only when the decode context has
 *                          type t
 *   setstacktype Base[n] t INDEX LOCAL and INDEX TYPE, as uPSDisassembly
 *                          reads them; no runtime executes this opcode
 *   goto T, flaggoto T, pop/goto T, pop2/goto T
 *   cond_goto T, V / cond_not_goto T, V
 *   pushexception F E F2 End
 *                          handler offsets of a try block: finally,
 *                          except, second finally and end of block; BRANCH
 *                          or, when absent (XXBYTE_IFPS_NONE), IMMEDIATE
 *                          0xFFFFFFFF printed "none"
 *   popexception n         IMMEDIATE (XXBYTE_IFPS_POPEXCEPTION_*)
 *   pop, ret, nop
 * An INDEX TYPE operand (pushtype, setstacktype) has extra = type number,
 * base type and XXBYTE_IFPS_EXTRA_HAS_TYPE when the decode context has the
 * type, else 0. Branch offsets are relative to the end of the instruction
 * (after the variable of cond_goto); the BRANCH value is the absolute
 * target. Pass each instruction's offset within its proc as the address so
 * addresses and targets share one space.
 *
 * The text follows IFPS3DataToText (uPSDisassembly.pas) with lower-case
 * names and keywords ("mod", "not") and these differences: branch targets
 * are absolute, without the "currpos + n" form; pushexception prints
 * absolute targets and "none" instead of the raw offsets; floats print as
 * the shortest text that reads back (see the README); strings escape every
 * non-ASCII byte as #n and an empty one prints ''; a Set lists its members
 * instead of its raw bytes as a string; Currency, U64 and UnicodeString
 * immediates print their values (the official disassembler has no case
 * for them and loses its place); pushtype names only the base type
 * ("(Record)", not "(Record(U8,S32))"); there are no stack-depth comments.
 *
 * Groups: call and callvar CALL; goto, pop/goto and pop2/goto JUMP|STOP;
 * cond_goto, cond_not_goto and flaggoto CONDITIONAL; ret RETURN|STOP;
 * popexception 0 to 3 JUMP|STOP without a BRANCH operand: it continues at a
 * handler offset of the matching pushexception (a finally block or the end
 * of the block; at the end of a try/except, the end of the block, past the
 * except handler), at the exit point of an earlier ret, or re-raises; the
 * runtime skips other popexception values, which are NONE. Everything else
 * is NONE; pushexception falls through and its BRANCH operands are the
 * handler entry points.
 *
 * Resolver. The formatter offers every INDEX operand (variables, element
 * selectors, proc numbers including ProcPtr immediates, type numbers) and
 * every BRANCH to the resolver. Its text replaces the numeric form
 * ("GlobalVar[3]", "Base[-1]", "12"); the ".[" "]" around a constant
 * element, the "[PROC: " "]" of a ProcPtr immediate and the "(BaseType)"
 * of pushtype stay.
 *
 * Decode context. The size of an immediate depends on the script's type
 * table, so xxbyte_decode_ex takes a const xxbyte_ifps_context *. Without
 * one (xxbyte_decode, or a NULL context) every instruction with an
 * immediate operand returns XXBYTE_STATUS_INVALID_INSTRUCTION once its
 * type number has been read (XXBYTE_STATUS_TRUNCATED before that); all
 * other instructions decode normally, pushtype without its "(BaseType)".
 * With a context, an immediate whose type number is not below type_count,
 * whose base type cannot be an immediate (Record, Array, Pointer, Variant,
 * Class, ...) or whose Set has more than 256 bits is INVALID_INSTRUCTION.
 * A destination given as an immediate decodes (the runtime rejects it when
 * it runs). Variable forms above 3, calc operators above 10, compare
 * operators above 7 and unknown opcodes are INVALID_INSTRUCTION.
 */

/* Family index kinds: the element selector of a variable (see above). */
typedef enum xxbyte_ifps_index_kind {
    /* constant record field or array element number (form 2) */
    XXBYTE_IFPS_INDEX_ELEMENT = XXBYTE_INDEX_FAMILY_FIRST,
    /* element number held by a global variable (form 3) */
    XXBYTE_IFPS_INDEX_ELEMENT_GLOBAL = XXBYTE_INDEX_FAMILY_FIRST + 1,
    /* element number held by Base[n], n >= 0 (form 3) */
    XXBYTE_IFPS_INDEX_ELEMENT_LOCAL = XXBYTE_INDEX_FAMILY_FIRST + 2,
    /* element number held by Base[n], n < 0 (form 3; value SIGNED) */
    XXBYTE_IFPS_INDEX_ELEMENT_ARGUMENT = XXBYTE_INDEX_FAMILY_FIRST + 3
} xxbyte_ifps_index_kind;

/* xxbyte_operand.extra of an immediate, and of an INDEX TYPE operand whose
 * type the decode context has: the type number, its base type and
 * XXBYTE_IFPS_EXTRA_HAS_TYPE. */
#define XXBYTE_IFPS_EXTRA_TYPE(extra) ((uint32_t)((extra) & 0xFFFFFFFFu))
#define XXBYTE_IFPS_EXTRA_BASE_TYPE(extra) ((uint32_t)(((extra) >> 32) & 0xFFu))
#define XXBYTE_IFPS_EXTRA_HAS_TYPE ((uint64_t)1 << 40)

/*
 * Mnemonic identifiers (xxbyte_instruction.id), in opcode order. The name
 * is the mnemonic upper-cased with every run of non-alphanumeric characters
 * replaced by '_' ("pop/goto" is XXBYTE_IFPS_ID_POP_GOTO). Valid ids are 1
 * to XXBYTE_IFPS_ID_COUNT - 1.
 */
typedef enum xxbyte_ifps_id {
    XXBYTE_IFPS_ID_INVALID = 0,
    XXBYTE_IFPS_ID_ASSIGN = 1,
    XXBYTE_IFPS_ID_CALC = 2,
    XXBYTE_IFPS_ID_PUSH = 3,
    XXBYTE_IFPS_ID_PUSHVAR = 4,
    XXBYTE_IFPS_ID_POP = 5,
    XXBYTE_IFPS_ID_CALL = 6,
    XXBYTE_IFPS_ID_GOTO = 7,
    XXBYTE_IFPS_ID_COND_GOTO = 8,
    XXBYTE_IFPS_ID_COND_NOT_GOTO = 9,
    XXBYTE_IFPS_ID_RET = 10,
    XXBYTE_IFPS_ID_SETSTACKTYPE = 11,
    XXBYTE_IFPS_ID_PUSHTYPE = 12,
    XXBYTE_IFPS_ID_COMPARE = 13,
    XXBYTE_IFPS_ID_CALLVAR = 14,
    XXBYTE_IFPS_ID_SETPOINTER = 15,
    XXBYTE_IFPS_ID_BNOT = 16,
    XXBYTE_IFPS_ID_MINUS = 17,
    XXBYTE_IFPS_ID_SETFLAG = 18,
    XXBYTE_IFPS_ID_FLAGGOTO = 19,
    XXBYTE_IFPS_ID_PUSHEXCEPTION = 20,
    XXBYTE_IFPS_ID_POPEXCEPTION = 21,
    XXBYTE_IFPS_ID_INOT = 22,
    XXBYTE_IFPS_ID_SETCOPYPOINTER = 23,
    XXBYTE_IFPS_ID_INC = 24,
    XXBYTE_IFPS_ID_DEC = 25,
    XXBYTE_IFPS_ID_POP_GOTO = 26,
    XXBYTE_IFPS_ID_POP2_GOTO = 27,
    XXBYTE_IFPS_ID_NOP = 28,
    XXBYTE_IFPS_ID_COUNT = 29
} xxbyte_ifps_id;

/* xxbyte_ifps_type.flags */
#define XXBYTE_IFPS_TYPE_EXPORTED 0x01u /* has an export name */

/*
 * One entry of the type table. Offsets are from the start of the script;
 * a field that does not apply is 0. To build a decode context by hand,
 * zero the entries and set base_type and, for sets, set_bits.
 */
typedef struct xxbyte_ifps_type {
    uint8_t base_type; /* XXBYTE_IFPS_BT_* */
    uint8_t flags;     /* XXBYTE_IFPS_TYPE_* */
    uint16_t reserved;
    uint32_t set_bits;     /* Set: element count, at most 256 */
    uint32_t element_type; /* Array, StaticArray: element type number */
    uint32_t count;        /* StaticArray: element count; Record:
                              field count */
    int32_t start_index;   /* StaticArray: low bound (build 23+) */
    uint32_t data_offset;  /* Class: class name; ProcPtr: parameter
                              info; Interface: GUID; Record: field
                              type numbers (count 32-bit values) */
    uint32_t data_size;
    uint32_t name_offset; /* export name */
    uint32_t name_size;
    uint32_t attributes_offset; /* attribute count (build 21+) */
    uint32_t attribute_count;
    uint32_t offset; /* the type record */
} xxbyte_ifps_type;

/* xxbyte_ifps_proc.flags (the stored flag byte) */
#define XXBYTE_IFPS_PROC_EXTERNAL 0x01u /* imported */
#define XXBYTE_IFPS_PROC_EXPORTED                                         \
    0x02u                                 /* export name and declaration; \
                                             for an external proc, a      \
                                             declaration */
#define XXBYTE_IFPS_PROC_ATTRIBUTES 0x04u /* attributes follow */

typedef struct xxbyte_ifps_proc {
    uint8_t flags; /* XXBYTE_IFPS_PROC_* */
    uint8_t reserved[3];
    uint32_t code_offset; /* internal: bytecode */
    uint32_t code_size;   /* internal: bytecode length, at least 1 */
    uint32_t name_offset; /* external: import name (up to 255
                             bytes); exported internal: export name */
    uint32_t name_size;
    uint32_t decl_offset; /* declaration, when EXPORTED */
    uint32_t decl_size;
    uint32_t attributes_offset; /* attribute count, when ATTRIBUTES */
    uint32_t attribute_count;
    uint32_t offset; /* the proc record */
} xxbyte_ifps_proc;

/* xxbyte_ifps_var.flags */
#define XXBYTE_IFPS_VAR_EXPORTED 0x01u

typedef struct xxbyte_ifps_var {
    uint32_t type; /* type number */
    uint8_t flags; /* XXBYTE_IFPS_VAR_* */
    uint8_t reserved[3];
    uint32_t name_offset; /* export name */
    uint32_t name_size;
    uint32_t offset; /* the var record */
} xxbyte_ifps_var;

/* The 28-byte header ("IFPS", build number, table sizes) and where the
 * tables lie. */
typedef struct xxbyte_ifps_script {
    uint32_t build; /* PSBuildNo, 12 to 23 */
    uint32_t type_count;
    uint32_t proc_count;
    uint32_t var_count;
    uint32_t main_proc;         /* XXBYTE_IFPS_NONE when there is none */
    uint32_t import_table_size; /* stored, unused by the runtime */
    uint32_t types_offset;      /* first type record (28) */
    uint32_t procs_offset;      /* first proc record */
    uint32_t vars_offset;       /* first var record */
    uint32_t end_offset;        /* just past the var table */
} xxbyte_ifps_script;

#define XXBYTE_IFPS_HEADER_SIZE 28u
#define XXBYTE_IFPS_SIGNATURE 0x53504649u /* "IFPS" read little-endian */
#define XXBYTE_IFPS_BUILD_MIN 12u
#define XXBYTE_IFPS_BUILD_MAX 23u

/* The decode context for xxbyte_decode_ex and xxbyte_disasm_buf_ex: a
 * borrowed type table. */
typedef struct xxbyte_ifps_context {
    const xxbyte_ifps_type *types;
    uint32_t type_count;
} xxbyte_ifps_context;

/* The mnemonic for id, or NULL. */
XXBYTE_API const char *xxbyte_ifps_mnemonic(uint32_t id);

/* The base type's name as BT2S of uPSDisassembly.pas names it in Inno
 * Setup's build (PS_PANSICHAR): "U8", "S32", "AnsiString", "AnsiChar",
 * "UnicodeString", ...; for the base types BT2S does not name, the
 * uPSUtils.pas constant without "bt": "ReturnAddress",
 * "NotificationVariant", "U64". NULL for an unknown base type. */
XXBYTE_API const char *xxbyte_ifps_base_type_name(uint32_t base_type);

/* "+" .. "as" for XXBYTE_IFPS_CALC_*, ">=" .. "is" for
 * XXBYTE_IFPS_COMPARE_*, or NULL. */
XXBYTE_API const char *xxbyte_ifps_calc_operator(uint32_t calc);
XXBYTE_API const char *xxbyte_ifps_compare_operator(uint32_t compare);

/*
 * Reads the 28-byte header at data[0] into *script (the *_offset fields
 * past types_offset stay 0). Returns XXBYTE_STATUS_TRUNCATED when size is
 * below 28, XXBYTE_STATUS_INVALID_INSTRUCTION when the signature is not
 * "IFPS" and XXBYTE_STATUS_UNSUPPORTED_MODE for a build outside 12 to 23.
 */
XXBYTE_API xxbyte_status xxbyte_ifps_parse_header(const uint8_t *data, size_t size, xxbyte_ifps_script *script);

/*
 * Parses a whole compiled script (Inno Setup's CompiledCodeText) as
 * TPSExec.LoadData does: the header, the type table (with export names,
 * attributes from build 21 and the static-array low bound from build 23:
 * the change that added it also raised the build to 23, and today's
 * LoadData, which reads it for every build, misreads older scripts),
 * the proc table and the var table. mode sizes Extended attribute values.
 * The first type_capacity types, proc_capacity procs and var_capacity
 * vars are stored (an array may be NULL when its capacity is 0); the
 * counts are in *script. Internal procs' code lies at code_offset; decode
 * it with addresses from 0. Offsets are 32-bit: only the first 4 GiB - 1
 * bytes of data are read.
 *
 * Attribute values (types and procs may carry attributes; Inno Setup 6
 * event procedures do) are sized by their field types, which are taken
 * from the stored types. So size the arrays from xxbyte_ifps_parse_header
 * and pass type_capacity >= type_count: a field whose type was not stored
 * fails with XXBYTE_STATUS_INVALID_ARGUMENT.
 *
 * Returns XXBYTE_STATUS_OK when every table is well formed,
 * XXBYTE_STATUS_TRUNCATED when a record or a proc's code runs past size,
 * and XXBYTE_STATUS_INVALID_INSTRUCTION when a record is malformed: an
 * unknown or non-storable base type (ReturnAddress, ResourcePointer,
 * NotificationVariant), a type number that is not below the types defined
 * so far, a set of more than 256 bits, a class name or parameter info
 * over 255 bytes, an empty record, a static array of more than 2^29 - 1
 * elements, a name or string longer than the runtime accepts, a proc with
 * empty code or a code offset or length of 2^31 or more, a var of an
 * unknown type, or a main proc that is not a proc.
 * XXBYTE_STATUS_UNSUPPORTED_MODE reports a bad mode or build.
 * Not modelled: the runtime's checks of the host (ImportProc must know
 * every external proc; registered attribute handlers may reject an
 * attribute), and counts of 2^31 or more, which the runtime's Longint
 * loops treat as compiler-dependent; here they are read as unsigned
 * counts (the script then ends early). On failure
 * *script holds the header fields when they could be read, and the table
 * offsets reached so far.
 */
XXBYTE_API xxbyte_status xxbyte_ifps_parse_script(const uint8_t *data, size_t size, uint32_t mode, xxbyte_ifps_script *script, xxbyte_ifps_type *types,
                                                  size_t type_capacity, xxbyte_ifps_proc *procs, size_t proc_capacity, xxbyte_ifps_var *vars, size_t var_capacity);

/* Points *context at types[0..count) (count is clamped to 2^32 - 1). */
XXBYTE_API void xxbyte_ifps_init_context(xxbyte_ifps_context *context, const xxbyte_ifps_type *types, size_t count);

/*
 * Names. A script names its entries through export names and import
 * declarations; xxbyte_ifps_resolve shows them in place of numbers.
 * xxbyte_ifps_names borrows the tables xxbyte_ifps_parse_script filled and
 * the script bytes their offsets point into (entries whose text lies
 * outside data have no name).
 *
 * Name text: the stored bytes with printable ASCII as is, '\' doubled and
 * every other byte as \XX (two upper-case hex digits), as Debug2Str of
 * uPSDisassembly.pas escapes declarations (it leaves bytes 127 and 128
 * alone). A resolved name longer than the resolver's buffer is cut.
 *
 *   proc       an internal proc with an export name: that name
 *              ("DONATEIMAGEONCLICK", "!MAIN"); an external proc with an
 *              import name: that name ("EXPANDCONSTANT"); an external proc
 *              without one, by its declaration:
 *                "class:C|M|..."      C.M (a property setter keeps the
 *                                     '@' the compiler appends: C.M@)
 *                "class:-", "class:+" the declaration as is (the nil and
 *                                     cast helpers)
 *                "dll:L\0F\0..."      F@L, or F when L is empty
 *              anything else (an unexported internal proc, "intf:"
 *              methods, a malformed declaration) has no name
 *   var        a global variable with an export name: that name
 *              ("WIZARDFORM")
 *   type       a type with an export name: that name ("BOOLEAN")
 */
typedef struct xxbyte_ifps_names {
    const uint8_t *data; /* the script */
    size_t size;
    const xxbyte_ifps_type *types;
    uint32_t type_count;
    const xxbyte_ifps_proc *procs;
    uint32_t proc_count;
    const xxbyte_ifps_var *vars;
    uint32_t var_count;
} xxbyte_ifps_names;

/* Fills *names (counts are clamped to 2^32 - 1; a NULL array counts 0). */
XXBYTE_API void xxbyte_ifps_init_names(xxbyte_ifps_names *names, const uint8_t *data, size_t size, const xxbyte_ifps_type *types, size_t type_count,
                                       const xxbyte_ifps_proc *procs, size_t proc_count, const xxbyte_ifps_var *vars, size_t var_count);

/*
 * The name of proc, global variable var or type type (see above): writes
 * it NUL-terminated to buffer, cut to buffer_size - 1 characters, and
 * returns its full length; 0 when the entry has no name (or names is
 * NULL), with buffer[0] = '\0' when buffer_size > 0.
 */
XXBYTE_API size_t xxbyte_ifps_proc_name(const xxbyte_ifps_names *names, uint32_t proc, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_ifps_var_name(const xxbyte_ifps_names *names, uint32_t var, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_ifps_type_name(const xxbyte_ifps_names *names, uint32_t type, char *buffer, size_t buffer_size);

/*
 * An xxbyte_resolver; context is a const xxbyte_ifps_names *. It names
 * INDEX FUNCTION operands (call, ProcPtr immediates) by proc, INDEX GLOBAL
 * and XXBYTE_IFPS_INDEX_ELEMENT_GLOBAL operands by var and INDEX TYPE
 * operands (pushtype, setstacktype) by type, and leaves everything else
 * (stack slots, constant elements, branches, other families) numeric:
 *   call 4                     call EXPANDCONSTANT
 *   call 7                     call TCONTROL.CREATE
 *   assign Base[4], GlobalVar[0]   assign Base[4], WIZARDFORM
 *   pushtype 28(U8)            pushtype BOOLEAN(U8)
 *   push [PROC: 3]             push [PROC: DONATEIMAGEINITIALIZEWIZARD]
 * The result is cut to buffer_size - 1 characters.
 */
XXBYTE_API size_t xxbyte_ifps_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/*
 * Containers (xxbyte_disasm_file): a whole compiled script, magic "IFPS"
 * (Inno Setup's CompiledCodeText, the [Code] section of setup.0 /
 * the setup data, after decompression). The mode decides the width of
 * Extended values; XXBYTE_MODE_AUTO and XXBYTE_IFPS_MODE_DEFAULT mean
 * XXBYTE_IFPS_MODE_32BIT for every build, since the build number (12 to
 * 23) does not record the runtime's platform: pass XXBYTE_IFPS_MODE_64BIT
 * for scripts of Inno Setup 7 64-bit installers (and other Win64, ARM or
 * other non-x86 runtimes).
 *
 * The script is parsed with xxbyte_ifps_parse_script, then listed as
 * IFPS3DataToText (uPSDisassembly.pas) lays it out, with names:
 *
 *   ; IFPS build 23: 45 types, 22 procs, 3 vars, main proc 0
 *   [TYPES]
 *   Type [28]: U8 Export: BOOLEAN
 *   Type [30]: Class ClassName: TWIZARDFORM Export: TWIZARDFORM
 *   Type [40]: Set Bits: 4 Export: TANCHORS
 *   [VARS]
 *   Var [0]: 30 Class TWIZARDFORM Export: WIZARDFORM
 *   [PROCS]
 *   Proc [0]: Export: !MAIN -1 (main)
 *   00000000  09                       ret
 *   Proc [3]: Export: DONATEIMAGEINITIALIZEWIZARD -1 Attributes:
 *     EVENT('InitializeWizard')                         (one line)
 *   00000000  0B 12 00 00 00           pushtype 18(UnicodeString)
 *   ...
 *   00000049  05 04 00 00 00           call EXPANDCONSTANT
 *   Proc [4]: External Decl: \01\00 EXPANDCONSTANT
 *   Proc [7]: External Decl: class:TCONTROL|CREATE|\00\01\00
 *
 * The first line gives the header ("main proc none" without one). Type
 * lines: "Type [n]: " BT2S (base type names of xxbyte_ifps_base_type_name;
 * records, arrays and static arrays spelled out as "Record(U8,S32)",
 * "Array of U8", "StaticArray[4] of U8", cut after 256 characters with
 * "..."), then " ClassName: " (Class), " Params: " (ProcPtr parameter
 * info), " GUID: {...}" (Interface), " Bits: n" (Set) or " Start: n"
 * (StaticArray with a nonzero low bound), " Export: " and the export name,
 * and " Attributes: " with the attributes. Var lines: "Var [n]: ", the
 * type number, BT2S of the type, the type's export name if any (as
 * IFPS3DataToText prints it) and " Export: " with the variable's own
 * export name. Proc lines: "Proc [n]: External: NAME" (import name),
 * "Proc [n]: External Decl: DECL NAME" (with a declaration; NAME only
 * when not empty), "Proc [n]: Export: NAME DECL" (exported internal) or
 * "Proc [n]" (internal), then " Attributes: ..." and " (main)" for the
 * main proc; an internal proc's line is followed by the listing of its
 * code (xxbyte_disasm_buf_ex with its offsets from 0, the script's type
 * table as decode context, xxbyte_ifps_resolve and the listing flags).
 * Names and declarations are escaped as above. Attributes print as
 * NAME or NAME(v1, v2), values as the immediates of instructions print
 * them without brackets ('text', 5, #65, [0, 3..5], 1.5); a ProcPtr value
 * prints its proc number.
 *
 * Returns the statuses of xxbyte_ifps_parse_script (TRUNCATED, also for
 * data under 28 bytes; INVALID_INSTRUCTION, also without the "IFPS"
 * magic; UNSUPPORTED_MODE for a build outside 12 to 23), with nothing
 * listed, or OUT_OF_MEMORY.
 */

#ifdef __cplusplus
}
#endif

#endif
