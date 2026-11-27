/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_INSTALLSHIELD_H
#define XXBYTE_XXBYTE_INSTALLSHIELD_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_INSTALLSHIELD
#error "the InstallShield compiled script (.inx) decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * InstallShield compiled InstallScript: the "actions" of setup.inx, .obs
 * script objects and .obl libraries (InstallShield 6 and later), and of
 * setup.ins (InstallShield 5 and earlier).
 *
 * Modes:
 *
 *   XXBYTE_INSTALLSHIELD_MODE_INX  InstallShield 6 to 2026 (Professional
 *                                  6.x, Developer 7/8, DevStudio 9,
 *                                  InstallShield X to 12, 2008 to 2026):
 *                                  setup.inx (magic "aLuZ", usually
 *                                  stored encrypted), .obs and .obl.
 *                                  One encoding for every version: the
 *                                  files carry no version number, and no
 *                                  encoding difference between versions
 *                                  is known.
 *   XXBYTE_INSTALLSHIELD_MODE_INS  InstallShield 3 and 5 setup.ins (magic
 *                                  B8 C9 0C 00, file versions 3.00.065
 *                                  and 5.00.000); the operand counts are
 *                                  those of the InstallShield 5 compiler.
 *
 * XXBYTE_INSTALLSHIELD_MODE_DEFAULT is XXBYTE_INSTALLSHIELD_MODE_INX. Other
 * values return XXBYTE_STATUS_UNSUPPORTED_MODE.
 */
#define XXBYTE_INSTALLSHIELD_MODE_DEFAULT 0u
#define XXBYTE_INSTALLSHIELD_MODE_INS 5u
#define XXBYTE_INSTALLSHIELD_MODE_INX 6u

/*
 * INX encoding. A code body is a run of basic blocks; each block is a
 * little-endian u16 action count followed by that many actions. An action
 * is
 *
 *   u16 opcode
 *   u16 typedef index                    struct (0x1C) only, then exactly
 *                                        one argument and no count
 *   u16 prototype index                  calldll (0x20) and call (0x21)
 *   u16 argument count, the arguments
 *   trailer                              function (0x22): a type-7 i32,
 *                                        the offset from that i32 to the
 *                                        function's declaration list;
 *                                        endfunc (0x26): the declaration
 *                                        list (see below)
 *
 * and an argument is a type byte and its data:
 *
 *   4  string variable    i16 address
 *   5  number variable    i16 address
 *   6  string literal     u16 length and that many bytes (ANSI code page)
 *   7  number literal     i32
 *   8  variant variable   i16 address (OBJECT, VARIANT, arrays, structs)
 *
 * Addresses above -100 are globals (address = global number); -101 and
 * below are the function's parameters and locals, local n = -(address +
 * 100), numbered from 1 per type, parameters first. The compiler reserves
 * global variants 0 (LAST_RESULT, which receives every call, expression and
 * property result), 1 (NOTHING) and 2 (Err).
 *
 * A declaration list is u16 numbers, u16 object count, object count x
 * {u16 flags, u16 typedef index or array size}, u16 strings, u16 sized
 * string count, sized string count x {u16 string, u16 size}.
 *
 * Jumps are relative to the block table: goto, if and try name the block
 * delta + current block, where the current block is the one holding the
 * action (0 is the start of the current block: the backward jump of a
 * repeat loop).
 *
 * INS encoding. A code body is a run of labels: opcode 0 followed by a u16
 * action count, then that many actions. An action is a u16 opcode and its
 * operands: most have a fixed argument count per opcode (the InstallShield
 * 5 compiler's table); the specials are listed with their instructions
 * below. An argument is a type byte and its data: 0x30-0x3F number
 * variable (i16), 0x40-0x4F number literal (odd low nibble, i32) or number
 * variable, 0x50-0x5F string variable, 0x60-0x6F string literal (odd low
 * nibble, u16 length and bytes) or string variable. Variable addresses are
 * as in INX (system variables first among the globals). Jump targets are
 * absolute label numbers.
 */

/* INX opcodes (the u16 action id). */
#define XXBYTE_INSTALLSHIELD_OP_NOP 0x01u
#define XXBYTE_INSTALLSHIELD_OP_ABORT 0x02u
#define XXBYTE_INSTALLSHIELD_OP_EXIT 0x03u
#define XXBYTE_INSTALLSHIELD_OP_IF 0x04u
#define XXBYTE_INSTALLSHIELD_OP_GOTO 0x05u
#define XXBYTE_INSTALLSHIELD_OP_ASSIGN 0x06u
#define XXBYTE_INSTALLSHIELD_OP_ADD 0x07u
#define XXBYTE_INSTALLSHIELD_OP_MOD 0x08u
#define XXBYTE_INSTALLSHIELD_OP_LT 0x09u
#define XXBYTE_INSTALLSHIELD_OP_GT 0x0Au
#define XXBYTE_INSTALLSHIELD_OP_LE 0x0Bu
#define XXBYTE_INSTALLSHIELD_OP_GE 0x0Cu
#define XXBYTE_INSTALLSHIELD_OP_EQ 0x0Du
#define XXBYTE_INSTALLSHIELD_OP_NE 0x0Eu
#define XXBYTE_INSTALLSHIELD_OP_SUB 0x0Fu
#define XXBYTE_INSTALLSHIELD_OP_MUL 0x10u
#define XXBYTE_INSTALLSHIELD_OP_DIV 0x11u
#define XXBYTE_INSTALLSHIELD_OP_AND 0x12u
#define XXBYTE_INSTALLSHIELD_OP_OR 0x13u
#define XXBYTE_INSTALLSHIELD_OP_XOR 0x14u /* ^: the path operator on strings */
#define XXBYTE_INSTALLSHIELD_OP_BNOT 0x15u
#define XXBYTE_INSTALLSHIELD_OP_SHL 0x16u
#define XXBYTE_INSTALLSHIELD_OP_SHR 0x17u
#define XXBYTE_INSTALLSHIELD_OP_LOR 0x18u
#define XXBYTE_INSTALLSHIELD_OP_LAND 0x19u
#define XXBYTE_INSTALLSHIELD_OP_ADDR 0x1Au
#define XXBYTE_INSTALLSHIELD_OP_DEREF 0x1Bu
#define XXBYTE_INSTALLSHIELD_OP_STRUCT 0x1Cu
#define XXBYTE_INSTALLSHIELD_OP_SETBYTE 0x1Du
#define XXBYTE_INSTALLSHIELD_OP_GETBYTE 0x1Eu
#define XXBYTE_INSTALLSHIELD_OP_CALLDLL 0x20u
#define XXBYTE_INSTALLSHIELD_OP_CALL 0x21u
#define XXBYTE_INSTALLSHIELD_OP_FUNCTION 0x22u
#define XXBYTE_INSTALLSHIELD_OP_RETURN 0x23u
#define XXBYTE_INSTALLSHIELD_OP_RETURN2 0x24u
#define XXBYTE_INSTALLSHIELD_OP_RETURN3 0x25u
#define XXBYTE_INSTALLSHIELD_OP_ENDFUNC 0x26u
#define XXBYTE_INSTALLSHIELD_OP_NOP2 0x27u
#define XXBYTE_INSTALLSHIELD_OP_STRLENGTHCHARS 0x28u
#define XXBYTE_INSTALLSHIELD_OP_STRSUB 0x29u
#define XXBYTE_INSTALLSHIELD_OP_STRFIND 0x2Au
#define XXBYTE_INSTALLSHIELD_OP_STRCOMPARE 0x2Bu
#define XXBYTE_INSTALLSHIELD_OP_STRTONUM 0x2Cu
#define XXBYTE_INSTALLSHIELD_OP_NUMTOSTR 0x2Du
#define XXBYTE_INSTALLSHIELD_OP_HANDLER 0x2Eu
#define XXBYTE_INSTALLSHIELD_OP_HANDLEREX 0x2Fu
#define XXBYTE_INSTALLSHIELD_OP_DOHANDLER 0x30u
#define XXBYTE_INSTALLSHIELD_OP_RESIZE 0x31u
#define XXBYTE_INSTALLSHIELD_OP_SIZEOF 0x32u
#define XXBYTE_INSTALLSHIELD_OP_PROPPUT 0x33u
#define XXBYTE_INSTALLSHIELD_OP_PROPPUTREF 0x34u
#define XXBYTE_INSTALLSHIELD_OP_PROPGET 0x35u
#define XXBYTE_INSTALLSHIELD_OP_TRY 0x36u
#define XXBYTE_INSTALLSHIELD_OP_ENDTRY 0x37u
#define XXBYTE_INSTALLSHIELD_OP_ENDCATCH 0x38u
#define XXBYTE_INSTALLSHIELD_OP_USEDLL 0x39u
#define XXBYTE_INSTALLSHIELD_OP_UNUSEDLL 0x3Au
#define XXBYTE_INSTALLSHIELD_OP_BINDVARIABLE 0x3Bu
#define XXBYTE_INSTALLSHIELD_OP_ADDRSTR 0x3Cu

/* INS opcodes with their own encodings (every other defined opcode is a
 * built-in function with a fixed argument count). */
#define XXBYTE_INSTALLSHIELD_INS_OP_LABEL 0x000u
#define XXBYTE_INSTALLSHIELD_INS_OP_ASKOPTIONS 0x020u
#define XXBYTE_INSTALLSHIELD_INS_OP_IF 0x022u
#define XXBYTE_INSTALLSHIELD_INS_OP_EXIT 0x02Bu
#define XXBYTE_INSTALLSHIELD_INS_OP_GOTO 0x02Cu
#define XXBYTE_INSTALLSHIELD_INS_OP_GOSUB 0x043u
#define XXBYTE_INSTALLSHIELD_INS_OP_RETSUB 0x044u
#define XXBYTE_INSTALLSHIELD_INS_OP_CALLDLL 0x0B4u
#define XXBYTE_INSTALLSHIELD_INS_OP_CALL 0x0B5u
#define XXBYTE_INSTALLSHIELD_INS_OP_FUNCTION 0x0B6u
#define XXBYTE_INSTALLSHIELD_INS_OP_RETURN 0x0B7u
#define XXBYTE_INSTALLSHIELD_INS_OP_ENDFUNC 0x0B8u
#define XXBYTE_INSTALLSHIELD_INS_OP_SPRINTF 0x103u
#define XXBYTE_INSTALLSHIELD_INS_OP_SPRINTFBOX 0x104u
#define XXBYTE_INSTALLSHIELD_INS_OP_COMPARE 0x128u
#define XXBYTE_INSTALLSHIELD_INS_OP_ABORT 0x159u
#define XXBYTE_INSTALLSHIELD_INS_OP_MAX 0x197u

/*
 * Mnemonic identifiers (xxbyte_instruction.id): an INX action has id =
 * opcode (1 to 0x3C), an INS action id = XXBYTE_INSTALLSHIELD_ID_INS_BASE +
 * opcode, and a block header (an INX block count word, an INS label) has
 * XXBYTE_INSTALLSHIELD_ID_BLOCK. xxbyte_instruction.opcode is the raw u16
 * opcode, or XXBYTE_INSTALLSHIELD_BLOCK_OPCODE for an INX block header.
 */
#define XXBYTE_INSTALLSHIELD_ID_INS_BASE 0x100u
#define XXBYTE_INSTALLSHIELD_ID_BLOCK 0x100u
#define XXBYTE_INSTALLSHIELD_BLOCK_OPCODE 0x10000u

/*
 * Instructions and text. Operands are separated by ", "; a variable prints
 * as n5 s5 v5 (global number, string, variant 5) or ln1 ls1 lv1 (local),
 * a number as signed decimal, a string as an InstallScript literal ("a\n",
 * with \n \r \t \\ \" and \ooo octal for every other byte below 0x20 or
 * above 0x7E).
 *
 * INX (operands in encoded order):
 *   block n (c)            block header: INDEX XXBYTE_INSTALLSHIELD_INDEX_
 *                          BLOCK (n, the block number) and IMMEDIATE c (the
 *                          action count); decoded only at an address that
 *                          the context's block table holds
 *   nop, nop2, abort, exit, endtry, endcatch (no arguments)
 *   if T, cond             jumps to T when cond is zero
 *   goto T
 *   try T1, T2             T1 = catch block, T2 = block after endcatch
 *   assign dst, src
 *   add/mod/lt/gt/le/ge/eq/ne/sub/mul/div/and/or/xor/shl/shr/lor/land
 *                          dst, a, b
 *   bnot/addr/deref/addrstr dst, src     (~, &number, *, &string)
 *   struct typedef_k, ptr  the -> operator (typedef index: INDEX TYPE)
 *   setbyte s, i, v / getbyte d, s, i
 *   calldll func_k, args  (0x20) call of a DLL prototype, call (0x21) of a
 *   call func_k, args      script function (as in every file seen); the
 *                          prototype index is an INDEX FUNCTION
 *   function D             DATA D: the address of the declaration list
 *   endfunc numbers N, strings S, objects O
 *                          IMMEDIATE counts of the declaration list that
 *                          follows (its entries are not operands)
 *   return [v], return2, return3
 *   strlengthchars, strsub, strfind, strcompare, strtonum, numtostr,
 *   resize, sizeof, usedll, unusedll, handler, handlerex, dohandler,
 *   propput, propputref, propget  args...  (results go to LAST_RESULT)
 *   bindvariable var, func_k
 *                          the handler is an INDEX FUNCTION when it is a
 *                          number literal
 * A jump target T is a BRANCH to the target block's address (extra = the
 * block number) when the decode context's block table resolves it, else an
 * INDEX XXBYTE_INSTALLSHIELD_INDEX_BLOCK_DELTA (SIGNED, the raw delta)
 * printed "+1" / "-2". A jump operand that is not a number literal stays
 * an argument.
 *
 * INS:
 *   block [n] (c)          opcode 0: IMMEDIATE c (the action count) and,
 *                          when the context's block table holds the
 *                          address, INDEX XXBYTE_INSTALLSHIELD_INDEX_BLOCK
 *                          n before it
 *   if T, a op b           u8 0x70, u16 label T, u8 comparison (low
 *                          nibble 1-6: < > <= >= = !=; printed "?n"
 *                          otherwise), arguments a and b; operands T,
 *                          IMMEDIATE comparison (size 1), a, b; jumps to T
 *                          when the comparison fails
 *   goto T                 u8 0x70, u16 label
 *   gosub T / gosub v      u8 0x70 and u16 label, or one argument; retsub
 *                          returns from it
 *   call func_k, T, args   u8 0x80, u16 prototype, u8 0x70, u16 label of
 *                          the body, then the prototype's declared
 *                          argument count
 *   calldll func_k, args   u8 0x80, u16 prototype, then its argument count
 *   function strings S, numbers N
 *                          u16, u16 S, S x u16, u16, u16 N, u16 (the
 *                          function's string and number slots, parameters
 *                          included)
 *   sprintf/sprintfbox args, askoptions args
 *                          u8 0x20, u16 count (askoptions: count pairs
 *                          after two fixed arguments)
 *   compare dst, a, code, b  code 1-6 as for if
 *   strcopy, assign        dst, src (string and number copies)
 *   other opcodes: the built-in's name (lower case; add2, sub2, mul2 and
 *   div2 for the two-argument Add, Sub, Mul and Divide; "op_NNN" where the
 *   compiler's table has no name) and its arguments; op_12e takes none
 * The u8 tags (0x70 label, 0x80 prototype, 0x20 count) are not checked.
 * Label targets T are BRANCH (to the label's address, extra = label
 * number) with a context whose block table has the label, else INDEX
 * XXBYTE_INSTALLSHIELD_INDEX_BLOCK (absolute label number). call and
 * calldll need the context's prototype argument counts: without them (or
 * for a prototype outside the table) they return
 * XXBYTE_STATUS_INVALID_INSTRUCTION.
 *
 * Operands. A variable is INDEX XXBYTE_INDEX_GLOBAL (value = global
 * number, SIGNED when negative) or XXBYTE_INDEX_LOCAL (value = n >= 1)
 * with one of the flags below, size 2 and extra = the raw address as an
 * unsigned 16-bit value; a number literal is IMMEDIATE (SIGNED, size 4); a
 * string literal is BYTES (value = offset of the first character from the
 * instruction's first byte, size = length) with the STRING flag.
 *
 * Arguments. Every instruction with arguments has table_count = the
 * argument count and table_offset = the offset of the first argument's
 * type byte; the arguments that fit follow the fixed operands in operands[]
 * (XXBYTE_MAX_OPERANDS in all). xxbyte_installshield_argument returns any
 * argument. The INX trailer operands (DATA of function, the counts of
 * endfunc) follow the arguments when they fit.
 *
 * Groups: goto JUMP|STOP; if CONDITIONAL; call, calldll (INS call,
 * calldll, gosub) CALL; return, return2, return3, endfunc (INS return, endfunc,
 * retsub) RETURN|STOP; exit STOP; abort THROW|STOP; block PAYLOAD;
 * everything else (try included: it only registers its handler blocks)
 * NONE.
 *
 * Resolver. Every INDEX, BRANCH and DATA operand and every INDEX argument
 * beyond operands[] (through xxbyte_fmt_resolve_entry) goes to the resolver;
 * its text replaces the numeric form (the "func_" and "typedef_" prefixes
 * included).
 *
 * Unknown opcodes (INX 0, 0x1F and above 0x3C; INS opcodes the compiler's
 * table leaves out, among them the IS3 Compress* functions, and 0x16D,
 * whose encoding is unknown), unknown argument types, an INX function
 * trailer whose type byte is not 7 and an argument list that runs past
 * the instruction's size limit (4 GiB) are INVALID_INSTRUCTION; TRUNCATED
 * when the bytes end first. The INX argument count of nop, abort, exit,
 * endtry and the other argument-less actions is honoured (the compiler
 * writes 0).
 */

/*
 * Limitations. InstallShield's compiler and runtime are closed source and
 * were not run. The INX decoding was checked against the open-source
 * installscript-decompiler on InstallShield 2026 compiler output, a real
 * InstallShield 7-era setup.inx (30277 actions) and generated scripts that
 * use every opcode; the INS decoding against the same tool and isDcc's
 * description on generated scripts only, as no real setup.ins was
 * available. Not verified: INX opcodes no file seen uses (return3, the
 * four-argument binary form), the meaning of nop/nop2 and handler,
 * handlerex, dohandler; the INS if comparison byte and second operand
 * (installscript-decompiler skips them), gosub with a label, op_12e and
 * the InstallShield 3 argument counts that differ from InstallShield 5
 * (CtrlSetMultCurSel, ReplaceFolderIcon, ComponentListItems,
 * ComponentTotalSize, PlayMMedia; the Compress* functions are undefined).
 */

/* Family index kinds. */
typedef enum xxbyte_installshield_index_kind {
    /* block (INX) or label (INS) number */
    XXBYTE_INSTALLSHIELD_INDEX_BLOCK = XXBYTE_INDEX_FAMILY_FIRST,
    /* INX jump target relative to the current block (SIGNED) */
    XXBYTE_INSTALLSHIELD_INDEX_BLOCK_DELTA = XXBYTE_INDEX_FAMILY_FIRST + 1
} xxbyte_installshield_index_kind;

/* Operand flags: the variable bank of a GLOBAL/LOCAL operand, STRING for
 * string literals too. */
#define XXBYTE_INSTALLSHIELD_OPERAND_FLAG_NUMBER 0x1000u
#define XXBYTE_INSTALLSHIELD_OPERAND_FLAG_STRING 0x2000u
#define XXBYTE_INSTALLSHIELD_OPERAND_FLAG_VARIANT 0x4000u

/*
 * Decode context (xxbyte_decode_ex). Both tables are optional; NULL or a
 * zero count means absent.
 *   block_addresses     address of each block (INX: the block table's
 *                       entries, INS: each label) in the address space of
 *                       the decode calls, ascending (every compiler output
 *                       seen is; a table out of order decodes safely but
 *                       may not recognise block headers or resolve jumps)
 *   prototype_arguments INS: declared argument count of each prototype
 */
typedef struct xxbyte_installshield_context {
    const uint32_t *block_addresses;
    uint32_t block_count;
    const uint16_t *prototype_arguments;
    uint32_t prototype_count;
} xxbyte_installshield_context;

/*
 * Argument index (0 to table_count - 1) of a decoded instruction, read from
 * instruction->code. The operand is what operands[] would hold for it.
 * Returns XXBYTE_STATUS_INVALID_ARGUMENT for a bad index.
 */
XXBYTE_API xxbyte_status xxbyte_installshield_argument(const xxbyte_instruction *instruction, uint32_t index, xxbyte_operand *operand);

/*
 * Containers. xxbyte_disasm_file (mode XXBYTE_MODE_AUTO or one of the
 * modes; a mode that does not match the file is INVALID_INSTRUCTION) reads
 *   setup.inx  "aLuZ" header, encrypted or plain
 *   .obs       compiled script object (magic 48 4F F3 C9)
 *   .obl       script library ("pOdA", version 1): every member .obs
 *   setup.ins  InstallShield 5 script (magic B8 C9 0C 00)
 * and lists a summary of the tables (globals, typedefs, prototypes with
 * their DLL imports, externs), then every function: a header line with its
 * prototype and its blocks, and its actions with variables, functions and
 * labels resolved: LAST_RESULT, NOTHING and Err, extern and system
 * variable names, names from debug symbols embedded with Compile.exe /GI
 * (function, global and local names), func_k for unnamed functions,
 * dll.name for DLL prototypes and block_n for jump targets. Code outside
 * any function (INX blocks before the first function) is listed under its
 * own header.
 *
 * Detection of an encrypted setup.inx decrypts its first four bytes; the
 * file is decrypted into a private copy. The InstallShield 2026 compiler
 * writes setup.inx encrypted; when the encryption started is not
 * documented, so plain files are read as well:
 *   plain[i] = ror8(enc[i] ^ 0xF1, 2) - (i mod 71)
 * with i the byte's position in the file. .obs, .obl and .ins files are
 * not encrypted.
 */
typedef enum xxbyte_installshield_file_kind {
    XXBYTE_INSTALLSHIELD_FILE_UNKNOWN = 0,
    XXBYTE_INSTALLSHIELD_FILE_INX = 1,           /* plain setup.inx */
    XXBYTE_INSTALLSHIELD_FILE_INX_ENCRYPTED = 2, /* encrypted setup.inx */
    XXBYTE_INSTALLSHIELD_FILE_OBS = 3,
    XXBYTE_INSTALLSHIELD_FILE_OBL = 4,
    XXBYTE_INSTALLSHIELD_FILE_INS = 5,
    XXBYTE_INSTALLSHIELD_FILE_DBG = 6 /* debug symbols (kUtZ): not code */
} xxbyte_installshield_file_kind;

XXBYTE_API xxbyte_installshield_file_kind xxbyte_installshield_detect(const uint8_t *data, size_t size);

/* Decrypts (encrypted setup.inx) size bytes in place; position is the file
 * offset of buffer[0]. */
XXBYTE_API void xxbyte_installshield_decrypt(uint8_t *buffer, size_t size, uint64_t position);

/* Name of a prototype or struct member type code ("STRING", "LONG", ...),
 * or NULL. */
XXBYTE_API const char *xxbyte_installshield_type_name(uint32_t type);

#ifdef __cplusplus
}
#endif

#endif
