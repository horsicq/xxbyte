/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_QUICKJS_H
#define XXBYTE_XXBYTE_QUICKJS_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_QUICKJS
#error "the QuickJS bytecode decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * QuickJS bytecode: the function bytecode of Fabrice Bellard's QuickJS and of
 * the quickjs-ng fork, as JS_WriteObject(JS_WRITE_OBJ_BYTECODE) serializes it
 * (qjsc -c/-e output arrays, qjsc -b / ng raw .qbc files, bjson.write,
 * std.writeObject and embedded blobs).
 *
 * Modes. Every mode is one release's quickjs-opcode.h (DEF entries only; the
 * temporary def opcodes never reach serialized bytecode), quickjs-atom.h
 * (the predefined atoms below JS_ATOM_END) and JS_WriteObject layout, all
 * tabulated from the official sources:
 *
 *   XXBYTE_QUICKJS_MODE_2026_06_04  Bellard 2026-06-04 (BC_VERSION 5)
 *   XXBYTE_QUICKJS_MODE_2025_09_13  Bellard 2025-09-13 (BC_VERSION 5)
 *   XXBYTE_QUICKJS_MODE_2025_04_26  Bellard 2025-04-26 (BC_VERSION 4)
 *   XXBYTE_QUICKJS_MODE_2024_01_13  Bellard 2024-01-13 (BC_VERSION 2)
 *   XXBYTE_QUICKJS_MODE_2021_03_27  Bellard 2021-03-27 (BC_VERSION 2;
 *                                   2020-11-08 has the same tables)
 *   XXBYTE_QUICKJS_MODE_NG(minor, patch)  quickjs-ng 0.minor.patch:
 *     18.0 (BC_VERSION 29)  17.0 (28)  16.0-16.2 (27)  15.0/15.1 (26)
 *     14.0 (25)  13.0 (24)  12.0/12.1 (23)  11.0 (21)  10.0/10.1 (20)
 *     9.0 (19)  7.0/8.0 (19)  6.1 (16)  6.0 (14)  5.0 (12)
 *     3.0/4.0/4.1 (8)  2.0 (3)  1.0 (2)
 *
 * The Bellard 2021 and 2024 releases have two opcode and atom tables: the
 * default build (CONFIG_BIGNUM=y in the Makefile, BC_VERSION 2) and a build
 * without CONFIG_BIGNUM (BC_VERSION 1), selected by OR-ing
 * XXBYTE_QUICKJS_MODE_NO_BIGNUM into the mode. Any other mode is
 * XXBYTE_STATUS_UNSUPPORTED_MODE. XXBYTE_QUICKJS_MODE_DEFAULT is the newest
 * Bellard release, 2026-06-04.
 *
 * Instructions are one opcode byte (short opcodes included, numbered after
 * nop exactly as in the release's enum) followed by the operands of its
 * format, little-endian and unaligned. Opcodes at or beyond the release's
 * OP_COUNT are XXBYTE_STATUS_INVALID_INSTRUCTION, every other byte decodes,
 * and an instruction running past code_size is XXBYTE_STATUS_TRUNCATED.
 * Pass the address of the instruction as its offset in the function's
 * bytecode (the reference dump's pc): branch targets are absolute in that
 * space.
 *
 * Text follows the dump_byte_code output of a DUMP_BYTECODE build, with
 * ", " between operands:
 *   - immediates (u8, i8, u16, i16, u32, i32, npop, the second operand of
 *     npop_u16, atom_u8, atom_u16 and label_u16) in decimal, signed where
 *     the format is signed;
 *   - the implicit operand of none_int (push_minus1 .. push_7), npopx
 *     (call0 .. call3), none_loc, none_arg and none_var_ref opcodes is shown
 *     as the reference dump shows it ("push_2 2", "get_loc0 0",
 *     "get_loc0_loc1 0, 1");
 *   - locals (loc8, loc, none_loc), arguments (arg, none_arg), closure
 *     variables (var_ref, none_var_ref) and constant-pool entries (const8,
 *     const) as the decimal index followed by ": text" when the resolver
 *     supplies text ("get_loc 3: c", "push_const 0: 2.5", "fclosure8 1:
 *     [function f]"); the resolver is called for implicit operands too;
 *   - atoms (see below) as the resolver's text, else the predefined atom's
 *     name (an identifier as is, anything else quoted and escaped: length,
 *     "Symbol.iterator", "") and file atoms as atom:<index>, integer atoms
 *     in decimal;
 *   - branch targets (label8, label16, label, the label of atom_label_u8 /
 *     with_*) as the absolute address "0x002c".
 *
 *   B5 (2026-06-04)                 push_2 2
 *   BB 2A                           push_i8 42
 *   C3                              get_loc0 0
 *   3D 32 00 00 00                  get_field length
 *   3D 2F 00 00 00                  get_field ""
 *   3D FF 00 00 00                  get_field atom:12 (file atom 12)
 *   EA 05 (at 0x07)                 goto8 0x000d
 *   24 02 00                        call_method 2
 *
 * Operands, one per encoded field in order, plus the implicit ones:
 *   - XXBYTE_OPERAND_IMMEDIATE: integers (SIGNED for i8, i16, i32 and
 *     none_int); none_int and npopx values are IMPLICIT with size 0;
 *   - XXBYTE_OPERAND_INDEX: XXBYTE_INDEX_LOCAL, XXBYTE_INDEX_ARGUMENT,
 *     XXBYTE_INDEX_UPVALUE (closure variable, b->closure_var),
 *     XXBYTE_INDEX_CONSTANT (b->cpool), IMPLICIT with size 0 for the
 *     none_loc, none_arg and none_var_ref forms; and the atoms below;
 *   - XXBYTE_OPERAND_BRANCH: value = absolute target, extra = the signed
 *     relative offset as encoded (SIGNED flag). QuickJS label offsets are
 *     relative to the first operand byte (address + 1), and the label of
 *     atom_label_u8 to address + 5.
 * Atoms are 32-bit values in the serialized bytecode (JS_WriteObject
 * rewrites runtime atoms to file indices); operand.extra holds that raw
 * value and size is 4:
 *   - XXBYTE_QUICKJS_INDEX_BUILTIN_ATOM: a predefined atom of the mode's
 *     quickjs-atom.h, value = the atom number 1 .. JS_ATOM_END - 1 (0 is
 *     JS_ATOM_NULL, printed <null>);
 *   - XXBYTE_QUICKJS_INDEX_ATOM: entry value of the file's atom table
 *     (raw - JS_ATOM_END);
 *   - XXBYTE_QUICKJS_INDEX_INT_ATOM: an integer atom (bit 31 set), value =
 *     the integer.
 * The decoder needs no context: the atom class follows from the raw value
 * and the mode; names come from the resolver (xxbyte_quickjs_resolve).
 *
 * Groups: goto, goto8, goto16, ret: JUMP|STOP; if_true, if_false (and the
 * 8-bit forms), with_get_var, with_put_var, with_delete_var, with_make_ref,
 * with_get_ref, with_get_ref_undef: CONDITIONAL; return, return_undef,
 * return_async: RETURN|STOP; throw, throw_error: THROW|STOP; tail_call,
 * tail_call_method: CALL|RETURN|STOP; call, call0-3, call_method,
 * call_constructor, apply, apply_eval, eval, gosub, iterator_call: CALL.
 * catch (pushes a handler address) is a BRANCH operand without a group.
 *
 * Containers. xxbyte_disasm_file reads one JS_WriteObject output: the
 * version byte (ng 0.13+: and checksum), the atom table, then the object
 * tree: a module (BC_TAG_MODULE: requests, exports, star exports, imports,
 * then its function) or a function (BC_TAG_FUNCTION_BYTECODE: flags,
 * js_mode, name, counts, local and closure-variable definitions, bytecode,
 * debug info (file name, line, pc2line, source) and the constant pool, in
 * the release's order), whose constant pools hold nested functions,
 * numbers, strings, BigInts (and 2021/2024 BigFloat/BigDecimal), template
 * objects, arrays, objects, regexps and the other structured-clone tags.
 * With XXBYTE_MODE_AUTO the release is taken from the version byte; where
 * several releases share it (2021/2024 and ng 0.1.0 at 2; 2025-09-13 and
 * 2026-06-04 at 5; ng 0.7/0.8/0.9 at 19) the newest release whose layout
 * reads the whole file and whose opcodes, atoms, labels and indices are all
 * valid in every function is chosen (xxbyte_quickjs_detect). Every
 * function is listed, in file order (each before its nested functions),
 * after a header with the release, the checksum (ng 0.13+), the atom table
 * and the module records; each function header gives its name, parent and
 * constant slot, counts, flags, kind and debug position, then the
 * arguments, locals (kind, name, scope), closure variables (kind, name,
 * source) and constants:
 *
 *   ; QuickJS 2026-06-04 (BC_VERSION 5), 8 atoms, 2 functions
 *   ; atoms:
 *   ;   0: o
 *   ...
 *   ; function #1 add (parent #0, constant 0): 2 args (2 defined), 1 locals,
 *   ;   0 closure vars, 1 constants, stack 2, var refs 0, 14 bytes,
 *   ;   flags 0x0643, file "small.js"          (one line in the listing)
 *   ;   args: a b
 *   ;   locals:
 *   ;     0: let c [next:-1]
 *   ;   constants:
 *   ;     0: 2.5
 *   00000000  5E 00 00                 set_loc_uninitialized 0: c
 *   00000003  CF                       get_arg0 0: a
 *   0000000A  BD 00                    push_const8 0: 2.5
 *
 * Constants print as numbers (shortest round trip), quoted strings (cut at
 * 200 characters), BigInts in decimal with "n", [bytecode name] for
 * functions, [template "a", "b"] and [1, 2] for templates and arrays, and
 * bracketed summaries for the other tags.
 *
 * Names in the listing come from the file: atoms, locals, arguments,
 * closure variables and constants. A file that a mode cannot read, or an
 * image without bytecode (plain bjson data), returns
 * XXBYTE_STATUS_INVALID_INSTRUCTION; a big-endian file (2021/2024 and
 * ng 0.1.0 with BC_BE_VERSION) returns XXBYTE_STATUS_UNSUPPORTED_FORMAT.
 *
 * Limitations: JS_READ_OBJ_ROM_DATA images (runtime atom numbers) and
 * big-endian files are not read; BigFloat and BigDecimal constants print an
 * approximation; strings print non-ASCII characters as \uXXXX escapes.
 */
#define XXBYTE_QUICKJS_MODE_BELLARD(y, m, d) ((uint32_t)(y) * 10000u + (uint32_t)(m) * 100u + (uint32_t)(d))
#define XXBYTE_QUICKJS_MODE_2021_03_27 XXBYTE_QUICKJS_MODE_BELLARD(2021, 3, 27)
#define XXBYTE_QUICKJS_MODE_2024_01_13 XXBYTE_QUICKJS_MODE_BELLARD(2024, 1, 13)
#define XXBYTE_QUICKJS_MODE_2025_04_26 XXBYTE_QUICKJS_MODE_BELLARD(2025, 4, 26)
#define XXBYTE_QUICKJS_MODE_2025_09_13 XXBYTE_QUICKJS_MODE_BELLARD(2025, 9, 13)
#define XXBYTE_QUICKJS_MODE_2026_06_04 XXBYTE_QUICKJS_MODE_BELLARD(2026, 6, 4)
#define XXBYTE_QUICKJS_MODE_NO_BIGNUM 0x40000000u /* 2021/2024 without CONFIG_BIGNUM */
#define XXBYTE_QUICKJS_MODE_NG(minor, patch) (0x80000000u | (((uint32_t)(minor) & 0xFFu) << 8) | ((uint32_t)(patch) & 0xFFu))
#define XXBYTE_QUICKJS_MODE_NEWEST XXBYTE_QUICKJS_MODE_2026_06_04
#define XXBYTE_QUICKJS_MODE_NG_NEWEST XXBYTE_QUICKJS_MODE_NG(18, 0)
#define XXBYTE_QUICKJS_MODE_DEFAULT XXBYTE_QUICKJS_MODE_NEWEST

/* Family index kinds. */
#define XXBYTE_QUICKJS_INDEX_ATOM (XXBYTE_INDEX_FAMILY_FIRST + 0)         /* file atom table entry */
#define XXBYTE_QUICKJS_INDEX_BUILTIN_ATOM (XXBYTE_INDEX_FAMILY_FIRST + 1) /* predefined atom */
#define XXBYTE_QUICKJS_INDEX_INT_ATOM (XXBYTE_INDEX_FAMILY_FIRST + 2)     /* integer atom */

/* Release name of a mode ("2026-06-04", "quickjs-ng 0.18.0"), NULL if the
 * mode is unsupported. Aliases name the release whose tables they use. */
XXBYTE_API const char *xxbyte_quickjs_mode_name(uint32_t mode);
/* BC_VERSION byte the mode's release writes (without BC_BE_VERSION), 0 if
 * unsupported. */
XXBYTE_API uint32_t xxbyte_quickjs_bc_version(uint32_t mode);
/* OP_COUNT of the mode (first invalid opcode), 0 if unsupported. */
XXBYTE_API uint32_t xxbyte_quickjs_opcode_count(uint32_t mode);
/* Name of opcode in mode, NULL beyond OP_COUNT or for unsupported modes. */
XXBYTE_API const char *xxbyte_quickjs_opname(uint32_t mode, uint32_t opcode);
/* Mnemonic of an instruction id (instruction.id), NULL for 0 or unknown. */
XXBYTE_API const char *xxbyte_quickjs_mnemonic(uint32_t id);
/* JS_ATOM_END of the mode: atoms below it are predefined, file atom table
 * entries start there. 0 if unsupported. */
XXBYTE_API uint32_t xxbyte_quickjs_atom_end(uint32_t mode);
/* Text of predefined atom 1 .. JS_ATOM_END - 1 (unescaped bytes as in
 * quickjs-atom.h, "" for the empty string), NULL otherwise. */
XXBYTE_API const char *xxbyte_quickjs_builtin_atom(uint32_t mode, uint32_t atom);

/*
 * Parsed container. xxbyte_quickjs_open reads a whole JS_WriteObject image
 * (mode XXBYTE_MODE_AUTO detects the release) and keeps pointers into data,
 * which must outlive it. Statuses: XXBYTE_STATUS_INVALID_INSTRUCTION (not
 * readable in that mode), XXBYTE_STATUS_TRUNCATED (ends early),
 * XXBYTE_STATUS_UNSUPPORTED_FORMAT (big-endian), OUT_OF_MEMORY.
 */
typedef struct xxbyte_quickjs_file xxbyte_quickjs_file;

typedef struct xxbyte_quickjs_function {
    uint32_t index;      /* file order */
    uint32_t parent;     /* enclosing function, UINT32_MAX for a root */
    uint32_t cpool_slot; /* this function's index in the parent's pool */
    uint32_t flags;      /* the u16 flag word as serialized */
    uint32_t js_mode;    /* js_mode byte (bit 0: strict) */
    uint32_t func_kind;  /* 0 normal, 1 generator, 2 async, 3 async generator */
    uint32_t has_debug;
    uint32_t name_atom; /* raw atom (see xxbyte_quickjs_atom_text) */
    uint32_t arg_count, var_count, defined_arg_count, stack_size, var_ref_count;
    uint32_t closure_var_count, cpool_count, local_count;
    uint64_t offset;          /* of the BC_TAG_FUNCTION_BYTECODE byte */
    uint64_t bytecode_offset; /* of the bytecode in the file */
    uint32_t bytecode_size;
    uint32_t filename_atom; /* raw atom, 0 without debug info */
    uint32_t line, column;  /* 0 when the release does not store them */
    uint64_t pc2line_offset;
    uint32_t pc2line_size;
    uint64_t source_offset;
    uint32_t source_size;
} xxbyte_quickjs_function;

XXBYTE_API xxbyte_status xxbyte_quickjs_open(uint32_t mode, const uint8_t *data, size_t size, xxbyte_quickjs_file **file);
XXBYTE_API void xxbyte_quickjs_close(xxbyte_quickjs_file *file);
/* The mode the file was read with (the detected one for AUTO). */
XXBYTE_API uint32_t xxbyte_quickjs_file_mode(const xxbyte_quickjs_file *file);
XXBYTE_API uint32_t xxbyte_quickjs_function_count(const xxbyte_quickjs_file *file);
XXBYTE_API const xxbyte_quickjs_function *xxbyte_quickjs_get_function(const xxbyte_quickjs_file *file, uint32_t index);
XXBYTE_API uint32_t xxbyte_quickjs_atom_count(const xxbyte_quickjs_file *file);
/* Text of a raw atom (bytecode operand extra, name_atom, ...) as the
 * listing prints it (identifiers bare, other strings quoted). Returns the
 * full length, writing at most buffer_size - 1 characters; 0 if invalid. */
XXBYTE_API size_t xxbyte_quickjs_atom_text(const xxbyte_quickjs_file *file, uint32_t atom, char *buffer, size_t buffer_size);
/* Detects the release of a JS_WriteObject image (see Containers). */
XXBYTE_API xxbyte_status xxbyte_quickjs_detect(const uint8_t *data, size_t size, uint32_t *mode);

/* Resolver context for function `function` of file: names atoms, locals,
 * arguments, closure variables and constants (xxbyte_quickjs_resolve). */
typedef struct xxbyte_quickjs_names {
    const xxbyte_quickjs_file *file;
    uint32_t function;
} xxbyte_quickjs_names;

XXBYTE_API size_t xxbyte_quickjs_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
