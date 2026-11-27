/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_OCAML_H
#define XXBYTE_XXBYTE_OCAML_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_OCAML
#error "the OCaml bytecode decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OCaml bytecode, as ocamlc emits it and ocamlrun interprets it (the ZINC
 * machine of runtime/interp.c). Code is a sequence of 32-bit little-endian
 * words: an opcode word (runtime/caml/instruct.h) followed by its operand
 * words. xxbyte_unit_size is 4: every instruction starts at a multiple of 4
 * bytes from the start of the code.
 *
 * Modes. The opcode numbering is the same in every version except for the
 * renames and additions below:
 *
 *   XXBYTE_OCAML_MODE_4_02  OCaml 4.02 to 4.06, 148 opcodes (0-147). 82 and
 *                           83 are GETSTRINGCHAR and SETSTRINGCHAR; 45, 47,
 *                           49 and 51 are OFFSETCLOSUREM2, OFFSETCLOSURE2,
 *                           PUSHOFFSETCLOSUREM2 and PUSHOFFSETCLOSURE2.
 *   XXBYTE_OCAML_MODE_4_07  OCaml 4.07 to 4.11, 149 opcodes: 82 and 83
 *                           become GETBYTESCHAR and SETBYTESCHAR (safe
 *                           strings), and 148 is a new GETSTRINGCHAR.
 *   XXBYTE_OCAML_MODE_4_12  OCaml 4.12 to 4.14, 149 opcodes: closures carry
 *                           an arity/env word, so the four closure offsets
 *                           become OFFSETCLOSUREM3, OFFSETCLOSURE3,
 *                           PUSHOFFSETCLOSUREM3 and PUSHOFFSETCLOSURE3.
 *   XXBYTE_OCAML_MODE_5_00  OCaml 5.0 to 5.5 (and trunk, November 2026),
 *                           153 opcodes: the effect handlers PERFORM (149),
 *                           RESUME, RESUMETERM and REPERFORMTERM (152).
 *
 * XXBYTE_OCAML_MODE_DEFAULT is the newest, XXBYTE_OCAML_MODE_5_00. An
 * opcode word at or above the mode's count is INVALID_INSTRUCTION. Any
 * other mode returns XXBYTE_STATUS_UNSUPPORTED_MODE. Decoding needs no
 * context; xxbyte_decode_ex ignores it.
 */
#define XXBYTE_OCAML_MODE_4_02 0u
#define XXBYTE_OCAML_MODE_4_07 1u
#define XXBYTE_OCAML_MODE_4_12 2u
#define XXBYTE_OCAML_MODE_5_00 3u
#define XXBYTE_OCAML_MODE_NEWEST XXBYTE_OCAML_MODE_5_00
#define XXBYTE_OCAML_MODE_DEFAULT XXBYTE_OCAML_MODE_NEWEST

/*
 * Syntax. The mnemonic is the instruct.h name; operands follow as
 * tools/dumpobj prints them, except that code addresses are absolute byte
 * addresses in hexadecimal (dumpobj prints word indexes in decimal) and
 * every encoded word is shown:
 *
 *   ACC0                         no operand
 *   ACC 5                        unsigned word (stack slots, sizes, tags,
 *                                argument counts)
 *   CONSTINT -7                  signed word (CONSTINT, PUSHCONSTINT,
 *                                OFFSETINT, OFFSETREF, OFFSETCLOSURE,
 *                                PUSHOFFSETCLOSURE)
 *   APPTERM 2, 5                 two unsigned words (APPTERM nargs, slots;
 *                                MAKEBLOCK size, tag)
 *   BRANCH 0x1c                  code offset: the word holds a signed word
 *                                displacement from the address of that
 *                                word (PUSH_RETADDR, BRANCH, BRANCHIF,
 *                                BRANCHIFNOT, PUSHTRAP)
 *   CLOSURE 1, 0x40              nvars, then the function's code
 *   BEQ -1, 0x24                 signed constant, then the target
 *                                (BEQ BNEQ BLTINT BLEINT BGTINT BGEINT)
 *   BULTINT 3, 0x30              unsigned constant, then the target
 *   GETGLOBAL 12                 global slot (also PUSHGETGLOBAL,
 *                                SETGLOBAL)
 *   GETGLOBALFIELD 12, 3         global slot, field
 *   C_CALL2 57                   primitive number (C_CALL1 to C_CALL5)
 *   C_CALLN 7, 102               argument count, primitive number
 *   GETPUBMET -1053241331, 0     method tag (signed hash), cache word
 *   SWITCH {int 0: 0x54, tag 0: 0x60, tag 1: 0x6c}
 *                                the int cases, then the tag cases
 *   CLOSUREREC 2, 0, [0x8, 0x48] nfuncs, nvars, then the code of every
 *                                function
 *
 * Operands (xxbyte_instruction.operands), in encoding order, each with size
 * 4 and extra = the byte offset of its word from the opcode word:
 *   - unsigned and signed words: XXBYTE_OPERAND_IMMEDIATE (signed ones with
 *     XXBYTE_OPERAND_FLAG_SIGNED);
 *   - code offsets: XXBYTE_OPERAND_BRANCH with the absolute target address,
 *     operand word address + 4 * displacement;
 *   - global slots: XXBYTE_OPERAND_INDEX, kind XXBYTE_INDEX_GLOBAL;
 *   - primitive numbers: XXBYTE_OPERAND_INDEX, kind
 *     XXBYTE_OCAML_INDEX_PRIMITIVE (an index in the executable's PRIM
 *     section, the C primitive table);
 *   - SWITCH: two IMMEDIATE operands, the int case count (the low 16 bits of
 *     its size word, size 2, extra 4) and the tag case count (the high 16
 *     bits, size 2, extra 6). table_count is their sum, table_offset 8, and
 *     xxbyte_switch_entry gives entry i: for an int case, key = the matched
 *     integer (i); for a tag case, key = XXBYTE_OCAML_SWITCH_TAG | the
 *     matched block tag; the target is the address of the first table word
 *     + 4 * the entry's displacement (ocamlrun: pc += pc[index]);
 *   - CLOSUREREC: two IMMEDIATE operands, nfuncs and nvars. table_count is
 *     nfuncs, table_offset 12; xxbyte_ocaml_closure_entry gives the code
 *     address of function i, the address of the first offset word + 4 *
 *     its displacement. It is not a switch (xxbyte_switch_entry refuses it).
 * The resolver is called for every BRANCH and INDEX operand and for every
 * SWITCH and CLOSUREREC table entry (as a BRANCH operand 0 of a copy of the
 * instruction, xxbyte_fmt_resolve_entry).
 *
 * Groups: BRANCH JUMP|STOP; BRANCHIF, BRANCHIFNOT and the compare-and-
 * branch instructions CONDITIONAL; SWITCH SWITCH|STOP; APPLY*, C_CALL*,
 * PERFORM and RESUME CALL; APPTERM*, RESUMETERM and REPERFORMTERM (tail
 * calls) CALL|RETURN|STOP; RETURN and STOP RETURN|STOP; RAISE, RERAISE and
 * RAISE_NOTRACE THROW|STOP; everything else NONE (PUSHTRAP and PUSH_RETADDR
 * only push a code address; GRAB may return a partial application but
 * otherwise falls through).
 *
 * Limitations: EVENT and BREAK are decoded although only the debugger
 * writes them into code. GETPUBMET's cache word is zero in files and
 * rewritten by ocamlrun at run time. The table of an instruction may make
 * it very large (a SWITCH up to 2 * 65535 entries); a CLOSUREREC whose size
 * would not fit in 32 bits is INVALID_INSTRUCTION, a table that runs past
 * the end of the code TRUNCATED.
 */
#define XXBYTE_OCAML_INDEX_PRIMITIVE ((xxbyte_index_kind)XXBYTE_INDEX_FAMILY_FIRST)

/* The key bit of a SWITCH tag case (xxbyte_switch_entry). */
#define XXBYTE_OCAML_SWITCH_TAG (INT64_C(1) << 32)

/*
 * Mnemonic identifiers (xxbyte_instruction.id): the 5.x instruction set in
 * opcode order (id = 5.x opcode + 1), then the names only older versions
 * use. Valid ids are 1 to XXBYTE_OCAML_ID_COUNT - 1.
 */
typedef enum xxbyte_ocaml_id {
    XXBYTE_OCAML_ID_INVALID = 0,
    XXBYTE_OCAML_ID_ACC0 = 1,
    XXBYTE_OCAML_ID_ACC1 = 2,
    XXBYTE_OCAML_ID_ACC2 = 3,
    XXBYTE_OCAML_ID_ACC3 = 4,
    XXBYTE_OCAML_ID_ACC4 = 5,
    XXBYTE_OCAML_ID_ACC5 = 6,
    XXBYTE_OCAML_ID_ACC6 = 7,
    XXBYTE_OCAML_ID_ACC7 = 8,
    XXBYTE_OCAML_ID_ACC = 9,
    XXBYTE_OCAML_ID_PUSH = 10,
    XXBYTE_OCAML_ID_PUSHACC0 = 11,
    XXBYTE_OCAML_ID_PUSHACC1 = 12,
    XXBYTE_OCAML_ID_PUSHACC2 = 13,
    XXBYTE_OCAML_ID_PUSHACC3 = 14,
    XXBYTE_OCAML_ID_PUSHACC4 = 15,
    XXBYTE_OCAML_ID_PUSHACC5 = 16,
    XXBYTE_OCAML_ID_PUSHACC6 = 17,
    XXBYTE_OCAML_ID_PUSHACC7 = 18,
    XXBYTE_OCAML_ID_PUSHACC = 19,
    XXBYTE_OCAML_ID_POP = 20,
    XXBYTE_OCAML_ID_ASSIGN = 21,
    XXBYTE_OCAML_ID_ENVACC1 = 22,
    XXBYTE_OCAML_ID_ENVACC2 = 23,
    XXBYTE_OCAML_ID_ENVACC3 = 24,
    XXBYTE_OCAML_ID_ENVACC4 = 25,
    XXBYTE_OCAML_ID_ENVACC = 26,
    XXBYTE_OCAML_ID_PUSHENVACC1 = 27,
    XXBYTE_OCAML_ID_PUSHENVACC2 = 28,
    XXBYTE_OCAML_ID_PUSHENVACC3 = 29,
    XXBYTE_OCAML_ID_PUSHENVACC4 = 30,
    XXBYTE_OCAML_ID_PUSHENVACC = 31,
    XXBYTE_OCAML_ID_PUSH_RETADDR = 32,
    XXBYTE_OCAML_ID_APPLY = 33,
    XXBYTE_OCAML_ID_APPLY1 = 34,
    XXBYTE_OCAML_ID_APPLY2 = 35,
    XXBYTE_OCAML_ID_APPLY3 = 36,
    XXBYTE_OCAML_ID_APPTERM = 37,
    XXBYTE_OCAML_ID_APPTERM1 = 38,
    XXBYTE_OCAML_ID_APPTERM2 = 39,
    XXBYTE_OCAML_ID_APPTERM3 = 40,
    XXBYTE_OCAML_ID_RETURN = 41,
    XXBYTE_OCAML_ID_RESTART = 42,
    XXBYTE_OCAML_ID_GRAB = 43,
    XXBYTE_OCAML_ID_CLOSURE = 44,
    XXBYTE_OCAML_ID_CLOSUREREC = 45,
    XXBYTE_OCAML_ID_OFFSETCLOSUREM3 = 46,
    XXBYTE_OCAML_ID_OFFSETCLOSURE0 = 47,
    XXBYTE_OCAML_ID_OFFSETCLOSURE3 = 48,
    XXBYTE_OCAML_ID_OFFSETCLOSURE = 49,
    XXBYTE_OCAML_ID_PUSHOFFSETCLOSUREM3 = 50,
    XXBYTE_OCAML_ID_PUSHOFFSETCLOSURE0 = 51,
    XXBYTE_OCAML_ID_PUSHOFFSETCLOSURE3 = 52,
    XXBYTE_OCAML_ID_PUSHOFFSETCLOSURE = 53,
    XXBYTE_OCAML_ID_GETGLOBAL = 54,
    XXBYTE_OCAML_ID_PUSHGETGLOBAL = 55,
    XXBYTE_OCAML_ID_GETGLOBALFIELD = 56,
    XXBYTE_OCAML_ID_PUSHGETGLOBALFIELD = 57,
    XXBYTE_OCAML_ID_SETGLOBAL = 58,
    XXBYTE_OCAML_ID_ATOM0 = 59,
    XXBYTE_OCAML_ID_ATOM = 60,
    XXBYTE_OCAML_ID_PUSHATOM0 = 61,
    XXBYTE_OCAML_ID_PUSHATOM = 62,
    XXBYTE_OCAML_ID_MAKEBLOCK = 63,
    XXBYTE_OCAML_ID_MAKEBLOCK1 = 64,
    XXBYTE_OCAML_ID_MAKEBLOCK2 = 65,
    XXBYTE_OCAML_ID_MAKEBLOCK3 = 66,
    XXBYTE_OCAML_ID_MAKEFLOATBLOCK = 67,
    XXBYTE_OCAML_ID_GETFIELD0 = 68,
    XXBYTE_OCAML_ID_GETFIELD1 = 69,
    XXBYTE_OCAML_ID_GETFIELD2 = 70,
    XXBYTE_OCAML_ID_GETFIELD3 = 71,
    XXBYTE_OCAML_ID_GETFIELD = 72,
    XXBYTE_OCAML_ID_GETFLOATFIELD = 73,
    XXBYTE_OCAML_ID_SETFIELD0 = 74,
    XXBYTE_OCAML_ID_SETFIELD1 = 75,
    XXBYTE_OCAML_ID_SETFIELD2 = 76,
    XXBYTE_OCAML_ID_SETFIELD3 = 77,
    XXBYTE_OCAML_ID_SETFIELD = 78,
    XXBYTE_OCAML_ID_SETFLOATFIELD = 79,
    XXBYTE_OCAML_ID_VECTLENGTH = 80,
    XXBYTE_OCAML_ID_GETVECTITEM = 81,
    XXBYTE_OCAML_ID_SETVECTITEM = 82,
    XXBYTE_OCAML_ID_GETBYTESCHAR = 83,
    XXBYTE_OCAML_ID_SETBYTESCHAR = 84,
    XXBYTE_OCAML_ID_BRANCH = 85,
    XXBYTE_OCAML_ID_BRANCHIF = 86,
    XXBYTE_OCAML_ID_BRANCHIFNOT = 87,
    XXBYTE_OCAML_ID_SWITCH = 88,
    XXBYTE_OCAML_ID_BOOLNOT = 89,
    XXBYTE_OCAML_ID_PUSHTRAP = 90,
    XXBYTE_OCAML_ID_POPTRAP = 91,
    XXBYTE_OCAML_ID_RAISE = 92,
    XXBYTE_OCAML_ID_CHECK_SIGNALS = 93,
    XXBYTE_OCAML_ID_C_CALL1 = 94,
    XXBYTE_OCAML_ID_C_CALL2 = 95,
    XXBYTE_OCAML_ID_C_CALL3 = 96,
    XXBYTE_OCAML_ID_C_CALL4 = 97,
    XXBYTE_OCAML_ID_C_CALL5 = 98,
    XXBYTE_OCAML_ID_C_CALLN = 99,
    XXBYTE_OCAML_ID_CONST0 = 100,
    XXBYTE_OCAML_ID_CONST1 = 101,
    XXBYTE_OCAML_ID_CONST2 = 102,
    XXBYTE_OCAML_ID_CONST3 = 103,
    XXBYTE_OCAML_ID_CONSTINT = 104,
    XXBYTE_OCAML_ID_PUSHCONST0 = 105,
    XXBYTE_OCAML_ID_PUSHCONST1 = 106,
    XXBYTE_OCAML_ID_PUSHCONST2 = 107,
    XXBYTE_OCAML_ID_PUSHCONST3 = 108,
    XXBYTE_OCAML_ID_PUSHCONSTINT = 109,
    XXBYTE_OCAML_ID_NEGINT = 110,
    XXBYTE_OCAML_ID_ADDINT = 111,
    XXBYTE_OCAML_ID_SUBINT = 112,
    XXBYTE_OCAML_ID_MULINT = 113,
    XXBYTE_OCAML_ID_DIVINT = 114,
    XXBYTE_OCAML_ID_MODINT = 115,
    XXBYTE_OCAML_ID_ANDINT = 116,
    XXBYTE_OCAML_ID_ORINT = 117,
    XXBYTE_OCAML_ID_XORINT = 118,
    XXBYTE_OCAML_ID_LSLINT = 119,
    XXBYTE_OCAML_ID_LSRINT = 120,
    XXBYTE_OCAML_ID_ASRINT = 121,
    XXBYTE_OCAML_ID_EQ = 122,
    XXBYTE_OCAML_ID_NEQ = 123,
    XXBYTE_OCAML_ID_LTINT = 124,
    XXBYTE_OCAML_ID_LEINT = 125,
    XXBYTE_OCAML_ID_GTINT = 126,
    XXBYTE_OCAML_ID_GEINT = 127,
    XXBYTE_OCAML_ID_OFFSETINT = 128,
    XXBYTE_OCAML_ID_OFFSETREF = 129,
    XXBYTE_OCAML_ID_ISINT = 130,
    XXBYTE_OCAML_ID_GETMETHOD = 131,
    XXBYTE_OCAML_ID_BEQ = 132,
    XXBYTE_OCAML_ID_BNEQ = 133,
    XXBYTE_OCAML_ID_BLTINT = 134,
    XXBYTE_OCAML_ID_BLEINT = 135,
    XXBYTE_OCAML_ID_BGTINT = 136,
    XXBYTE_OCAML_ID_BGEINT = 137,
    XXBYTE_OCAML_ID_ULTINT = 138,
    XXBYTE_OCAML_ID_UGEINT = 139,
    XXBYTE_OCAML_ID_BULTINT = 140,
    XXBYTE_OCAML_ID_BUGEINT = 141,
    XXBYTE_OCAML_ID_GETPUBMET = 142,
    XXBYTE_OCAML_ID_GETDYNMET = 143,
    XXBYTE_OCAML_ID_STOP = 144,
    XXBYTE_OCAML_ID_EVENT = 145,
    XXBYTE_OCAML_ID_BREAK = 146,
    XXBYTE_OCAML_ID_RERAISE = 147,
    XXBYTE_OCAML_ID_RAISE_NOTRACE = 148,
    XXBYTE_OCAML_ID_GETSTRINGCHAR = 149,
    XXBYTE_OCAML_ID_PERFORM = 150,
    XXBYTE_OCAML_ID_RESUME = 151,
    XXBYTE_OCAML_ID_RESUMETERM = 152,
    XXBYTE_OCAML_ID_REPERFORMTERM = 153,
    XXBYTE_OCAML_ID_OFFSETCLOSUREM2 = 154,
    XXBYTE_OCAML_ID_OFFSETCLOSURE2 = 155,
    XXBYTE_OCAML_ID_PUSHOFFSETCLOSUREM2 = 156,
    XXBYTE_OCAML_ID_PUSHOFFSETCLOSURE2 = 157,
    XXBYTE_OCAML_ID_SETSTRINGCHAR = 158,
    XXBYTE_OCAML_ID_COUNT = 159
} xxbyte_ocaml_id;

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_ocaml_mnemonic(uint32_t id);

/* The number of opcodes of mode (148, 149 or 153), 0 if the mode is not
 * supported. */
XXBYTE_API uint32_t xxbyte_ocaml_opcode_count(uint32_t mode);

/*
 * The code address of function index of a decoded CLOSUREREC (index <
 * table_count). Returns XXBYTE_STATUS_INVALID_ARGUMENT for anything else.
 */
XXBYTE_API xxbyte_status xxbyte_ocaml_closure_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *target);

/*
 * The mode of a "Caml1999<k>0NN" magic number (12 bytes; k = X for
 * executables, O for .cmo, A for .cma, the other kinds as well): NN <= 22
 * gives 4.02 (4.02 to 4.06; older files are decoded as 4.02 as well),
 * 23 to 28 gives 4.07, 29 to 31 gives 4.12, 32 and up gives 5.00. Returns
 * XXBYTE_STATUS_INVALID_INSTRUCTION when magic is NULL or not such a
 * number, or mode is NULL.
 */
XXBYTE_API xxbyte_status xxbyte_ocaml_mode_from_magic(const uint8_t *magic, size_t size, uint32_t *mode);

/*
 * Containers (xxbyte_disasm_file; XXBYTE_MODE_AUTO takes the mode from the
 * magic number, any other mode overrides it):
 *   - bytecode executables (ocamlc output, with a #! line or a launcher,
 *     or a -custom runtime in front): the trailer
 *     "Caml1999X0NN" and the section table before it, at the end of the
 *     file. The CODE section is listed; C_CALL primitives are named from
 *     PRIM; global slots from SYMB (compilation units and predefined
 *     exceptions: "SETGLOBAL Stdlib__List") and otherwise from DATA (the
 *     constant: "GETGLOBAL \"hi\\n\"", "3.5", "42L", "<0>(1, <0>(2, 0))",
 *     as tools/dumpobj prints it, except that floats are the shortest
 *     round-trip text and long values end in "...").
 *   - .cmo objects ("Caml1999O0NN"): the compilation unit's code. Its
 *     relocations name what the global and primitive words will be
 *     patched with: compilation units, predefined exceptions, literals
 *     (Lambda structured constants up to 5.0, whose floats keep their
 *     source text; values from 5.1) and primitives.
 *   - .cma libraries ("Caml1999A0NN"): every compilation unit in turn.
 * DATA, SYMB and the unit and library descriptors are output_value data,
 * read with an own reader of the Marshal format (small and big headers;
 * the int32, int64 and nativeint custom blocks). Compressed values (OCaml
 * 5.1+ with zstd) are not read; the names they would give stay numeric.
 * Debug information (DBUG, cu_debug) is not used. Programs linked with
 * -output-complete-exe or -output-obj keep their code in C data and are
 * not containers (tools/dumpobj does not read them either).
 *
 * Each code body gets a header line, then the listing of its code at
 * address 0. Before an instruction that some BRANCH operand, SWITCH entry
 * or CLOSUREREC entry targets, a label line "L_XXXX:" (the address in
 * upper-case hex, at least 4 digits) is printed, preceded by
 * "; function L_XXXX" when it is the code of a closure; those operands are
 * printed as the label:
 *
 *   ; CODE section at 0x1a, 588 bytes, 479 primitives, 18 globals (Caml1999X037)
 *   00000000  54 00 00 00 50 00 00 00  BRANCH L_0144
 *   ; function L_0008
 *   L_0008:
 *   00000008  00 00 00 00              ACC0
 *
 *   ; compilation unit T at 0x10, 584 bytes, 10 relocations (Caml1999O031)
 *
 * Limitations: a primitive with an empty name is printed as its number.
 */

#ifdef __cplusplus
}
#endif

#endif
