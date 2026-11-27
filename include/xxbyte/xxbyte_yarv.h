/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_YARV_H
#define XXBYTE_XXBYTE_YARV_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_YARV
#error "the Ruby YARV decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ruby YARV decode mode: the Ruby release whose instruction table and
 * binary layout apply, XXBYTE_YARV_MODE(major, minor) for 2.6, 2.7, 3.0,
 * 3.1, 3.2, 3.3, 3.4 and 4.0, optionally combined with the flags below.
 * Any other mode is XXBYTE_STATUS_UNSUPPORTED_MODE.
 *
 * The decoder reads the bytecode of one instruction sequence as
 * RubyVM::InstructionSequence#to_binary stores it (the IBF format; see
 * xxbyte_yarv_parse_ibf_header and xxbyte_yarv_locate_iseq):
 *   - Ruby 2.6 stores the code as raw VALUE words in the byte order of the
 *     platform that wrote it: the instruction number, then one word per
 *     operand. An instruction is insn_len words.
 *   - From Ruby 2.7 every instruction number and operand is a "small value"
 *     (1 to 9 bytes: the count of trailing zero bits of the first byte plus
 *     one gives the length, 0x00 means 9, and the value follows big-endian),
 *     except that Ruby 2.7 writes one raw kw-flag byte for a call data and
 *     3.0+ writes nothing for it, and that a builtin function is its index,
 *     the length of its name and the name bytes.
 * An instruction occupies insn_len VALUE slots in the loaded iseq whatever
 * its encoded size; xxbyte_yarv_insn_len gives that length.
 *
 * Addresses. Branch operands (TS_OFFSET) count VALUE slots from the end of
 * the instruction.
 *   - Raw words (2.6): addresses are byte offsets, slot = word, and the
 *     BRANCH value is the absolute byte address of the target.
 *   - Compact code (2.7+) decoded without a context (xxbyte_decode, or a
 *     NULL context): the address of an instruction is its slot index, the
 *     "pc" that RubyVM::InstructionSequence#disasm prints, and the BRANCH
 *     value is the target pc. A caller walking the code advances the
 *     address by xxbyte_yarv_insn_len and the byte position by size.
 *     xxbyte_disasm_buf and xxbyte_disasm_device cannot do that: they
 *     advance the address by the byte size, so their listings of compact
 *     code show wrong branch targets (an instruction with a call data is
 *     one byte but two slots). List compact code with xxbyte_disasm_buf_ex
 *     and a context instead:
 *         n = 0; xxbyte_yarv_build_slot_map(mode, code, size, NULL, 0, &n);
 *         offsets = (uint32_t *)malloc(n * sizeof(uint32_t));
 *         xxbyte_yarv_build_slot_map(mode, code, size, offsets, n, &n);
 *         xxbyte_yarv_context context = {address, offsets, n, size};
 *         xxbyte_disasm_buf_ex(XXBYTE_FAMILY_YARV, mode, &context, code,
 *                              size, address, ...);
 *   - Compact code decoded with an xxbyte_yarv_context (xxbyte_decode_ex,
 *     xxbyte_disasm_buf_ex): addresses are byte addresses, base + the byte
 *     offset of the instruction in the bytecode, and the BRANCH value is the
 *     byte address of the target instruction. An instruction whose address
 *     is not an instruction start of the context's slot map is
 *     XXBYTE_STATUS_INVALID_INSTRUCTION. A branch target slot outside the
 *     map (Ruby 3.3's prism compiler emits jumps past the end of an iseq,
 *     which the loader accepts) maps to base + code_size + (slot -
 *     slot_count) when it lies past the end and to base + slot (an address
 *     below base) when it lies before the start, so it never names an
 *     instruction of the iseq; extra keeps the raw offset. That needs the
 *     context's code_size (above the last instruction's offset); without it
 *     such a branch is XXBYTE_STATUS_INVALID_INSTRUCTION. A target slot
 *     inside an instruction (an operand slot) is always
 *     XXBYTE_STATUS_INVALID_INSTRUCTION: no compiler emits one and the VM
 *     would run an operand as an instruction.
 *
 * Validity follows the IBF loader of the release: an instruction number at
 * or above the release's instruction count, and every instruction with a
 * TS_FUNCPTR operand (opt_call_c_function, which the IBF dumper and loader
 * reject), are XXBYTE_STATUS_INVALID_INSTRUCTION; a builtin name length
 * above INT32_MAX is invalid. Overlong small values are accepted, as the
 * loader accepts them.
 */
#define XXBYTE_YARV_MODE(major, minor) ((((uint32_t)(major) & 0xFFu) << 8) | ((uint32_t)(minor) & 0xFFu))
#define XXBYTE_YARV_MODE_MAJOR(mode) (((uint32_t)(mode) >> 8) & 0xFFu)
#define XXBYTE_YARV_MODE_MINOR(mode) ((uint32_t)(mode) & 0xFFu)
#define XXBYTE_YARV_MODE_VERSION_MASK 0xFFFFu
/* VALUE is 32 bits wide (i386, ARM32 and other ILP32 builds): raw words are
 * 4 bytes, small values are taken modulo 2**32 and offsets are 32-bit. */
#define XXBYTE_YARV_MODE_WORD32 0x10000u
/* The image was written on a big-endian machine; this changes the raw
 * words of Ruby 2.6 code only. */
#define XXBYTE_YARV_MODE_BIG_ENDIAN 0x20000u
#define XXBYTE_YARV_MODE_FLAGS (XXBYTE_YARV_MODE_WORD32 | XXBYTE_YARV_MODE_BIG_ENDIAN)
#define XXBYTE_YARV_MODE_DEFAULT XXBYTE_YARV_MODE(4, 0)

/*
 * Operands, one per encoded operand in instruction order, plus implicit
 * ones. operand.size is the number of encoded bytes (0 for implicit
 * operands). The extra of an INDEX operand is its raw encoded value,
 * except for XXBYTE_INDEX_LOCAL (the level) and the implicit call data
 * (0).
 *   - TS_OFFSET: XXBYTE_OPERAND_BRANCH, value the absolute target (see
 *     "Addresses"), extra the raw slot offset.
 *   - TS_NUM: XXBYTE_OPERAND_IMMEDIATE, unsigned (no SIGNED flag).
 *   - TS_LINDEX: INDEX XXBYTE_INDEX_LOCAL, value the raw local index (the
 *     environment offset, as #to_a shows it: a signed VALUE, sign-extended
 *     from 32 bits in WORD32 modes and printed signed; Ruby 3.3's prism
 *     compiler emits negative ones), extra the level. The level is
 *     the following TS_NUM operand of getlocal, setlocal, getblockparam,
 *     setblockparam and getblockparamproxy, or an IMPLICIT IMMEDIATE of
 *     getlocal_WC_0/1 and setlocal_WC_0/1, so a local access always has
 *     the operands (index, level).
 *   - checkkeyword's keyword_index (a TS_LINDEX that is a keyword list
 *     position): an IMMEDIATE with the SIGNED flag, signed like TS_LINDEX.
 *   - putobject_INT2FIX_0_/_1_: an IMPLICIT IMMEDIATE 0 or 1.
 *   - TS_VALUE: INDEX XXBYTE_YARV_INDEX_OBJECT, an index into the global
 *     object list (2.6: the object list).
 *   - TS_ID and TS_GENTRY: INDEX XXBYTE_YARV_INDEX_ID, the global object
 *     list index of the name's Symbol (2.7+) or an index into the ID list
 *     (2.6); 0 stands for no name.
 *   - TS_CDHASH: INDEX XXBYTE_YARV_INDEX_CDHASH, the global object list
 *     index of the case/when Hash.
 *   - TS_ISEQ: INDEX XXBYTE_INDEX_FUNCTION, the iseq list index, -1 for
 *     none (the loader casts the encoded value to int; extra is raw).
 *   - TS_IC: INDEX XXBYTE_YARV_INDEX_IC, the inline storage index (up to
 *     3.1) or the global object list index of the constant path, an Array
 *     of Symbols (3.2+, whose cache slot is implicit).
 *   - TS_IVC, TS_ICVARC, TS_ISE: INDEX XXBYTE_YARV_INDEX_IVC/_ICVARC/_ISE,
 *     the inline storage index (from 3.2 counted from the start of the
 *     storage kind, as #to_a shows it; #disasm counts from the start of
 *     all inline storage).
 *   - TS_CALLDATA (2.7+) and TS_CALLINFO (2.6): INDEX
 *     XXBYTE_YARV_INDEX_CALLDATA with value -1. Call data are numbered in
 *     code order, not encoded: extra is the raw kw-flag byte (2.7) or word
 *     (2.6, Qtrue or Qfalse) that selects the keyword call list when
 *     nonzero, and from 3.0 the operand is IMPLICIT with size 0.
 *   - TS_CALLCACHE (2.6): INDEX XXBYTE_YARV_INDEX_CALLCACHE, value -1,
 *     extra the raw word (ignored by the loader).
 *   - TS_BUILTIN: INDEX XXBYTE_YARV_INDEX_BUILTIN with the builtin function
 *     index (cast to int as the loader does), then an XXBYTE_OPERAND_BYTES
 *     operand for the function name. The small value between them, the
 *     name length, belongs to neither operand's size.
 *
 * Text follows RubyVM::InstructionSequence#disasm: the instruction name,
 * then the operands separated by ", ". Branch targets, numbers and local
 * indexes are decimal ("branchunless 12", "getlocal 3, 1"), the first
 * operand of defined and checktype prints as #disasm names it ("defined
 * method, obj@4, obj@1", "checktype T_STRING") when it is a known value,
 * and references print as obj@N, id@N, cdhash@N, iseq@N or "nil", ic@N,
 * ivc@N, icvarc@N, ise@N, builtin@N(name) (name bytes outside 0x21-0x7E
 * and "\()" as \xHH), "<calldata>" ("<calldata!kw>" with a nonzero 2.7
 * flag), "<callinfo>" ("<callinfo!kw>") and "<callcache>". Implicit
 * operands print nothing, except the call data of 3.0+. A resolver is
 * called for every BRANCH and INDEX operand that prints, including the
 * implicit call data, and its text replaces the operand's whole text (for a
 * builtin, the name too). The formatter reads the code bytes only for
 * builtin names. xxbyte_yarv_resolver (see "Names" below) supplies the
 * names #disasm prints from an IBF image.
 */

/* Family index kinds (xxbyte_operand.kind of XXBYTE_OPERAND_INDEX). */
#define XXBYTE_YARV_INDEX_OBJECT (XXBYTE_INDEX_FAMILY_FIRST + 0)
#define XXBYTE_YARV_INDEX_ID (XXBYTE_INDEX_FAMILY_FIRST + 1)
#define XXBYTE_YARV_INDEX_CDHASH (XXBYTE_INDEX_FAMILY_FIRST + 2)
#define XXBYTE_YARV_INDEX_IC (XXBYTE_INDEX_FAMILY_FIRST + 3)
#define XXBYTE_YARV_INDEX_IVC (XXBYTE_INDEX_FAMILY_FIRST + 4)
#define XXBYTE_YARV_INDEX_ICVARC (XXBYTE_INDEX_FAMILY_FIRST + 5)
#define XXBYTE_YARV_INDEX_ISE (XXBYTE_INDEX_FAMILY_FIRST + 6)
#define XXBYTE_YARV_INDEX_CALLDATA (XXBYTE_INDEX_FAMILY_FIRST + 7)
#define XXBYTE_YARV_INDEX_CALLCACHE (XXBYTE_INDEX_FAMILY_FIRST + 8)
#define XXBYTE_YARV_INDEX_BUILTIN (XXBYTE_INDEX_FAMILY_FIRST + 9)

/*
 * Mnemonic identifiers (xxbyte_instruction.id), one per instruction name of
 * all supported releases in order of first appearance (2.6 first), so a name
 * keeps its id across releases. They include the operand-unified forms
 * (getlocal_WC_0, putobject_INT2FIX_1_, ...), the trace_ variants of every
 * instruction and the zjit_ variants of 4.0. The enum name is the
 * instruction name upper-cased with each run of characters other than A-Z
 * and 0-9 replaced by '_'. xxbyte_instruction.opcode is the release's
 * instruction number.
 */
typedef enum xxbyte_yarv_id {
    XXBYTE_YARV_ID_INVALID = 0,
    XXBYTE_YARV_ID_NOP = 1,
    XXBYTE_YARV_ID_GETLOCAL = 2,
    XXBYTE_YARV_ID_SETLOCAL = 3,
    XXBYTE_YARV_ID_GETBLOCKPARAM = 4,
    XXBYTE_YARV_ID_SETBLOCKPARAM = 5,
    XXBYTE_YARV_ID_GETBLOCKPARAMPROXY = 6,
    XXBYTE_YARV_ID_GETSPECIAL = 7,
    XXBYTE_YARV_ID_SETSPECIAL = 8,
    XXBYTE_YARV_ID_GETINSTANCEVARIABLE = 9,
    XXBYTE_YARV_ID_SETINSTANCEVARIABLE = 10,
    XXBYTE_YARV_ID_GETCLASSVARIABLE = 11,
    XXBYTE_YARV_ID_SETCLASSVARIABLE = 12,
    XXBYTE_YARV_ID_GETCONSTANT = 13,
    XXBYTE_YARV_ID_SETCONSTANT = 14,
    XXBYTE_YARV_ID_GETGLOBAL = 15,
    XXBYTE_YARV_ID_SETGLOBAL = 16,
    XXBYTE_YARV_ID_PUTNIL = 17,
    XXBYTE_YARV_ID_PUTSELF = 18,
    XXBYTE_YARV_ID_PUTOBJECT = 19,
    XXBYTE_YARV_ID_PUTSPECIALOBJECT = 20,
    XXBYTE_YARV_ID_PUTISEQ = 21,
    XXBYTE_YARV_ID_PUTSTRING = 22,
    XXBYTE_YARV_ID_CONCATSTRINGS = 23,
    XXBYTE_YARV_ID_TOSTRING = 24,
    XXBYTE_YARV_ID_FREEZESTRING = 25,
    XXBYTE_YARV_ID_TOREGEXP = 26,
    XXBYTE_YARV_ID_INTERN = 27,
    XXBYTE_YARV_ID_NEWARRAY = 28,
    XXBYTE_YARV_ID_DUPARRAY = 29,
    XXBYTE_YARV_ID_DUPHASH = 30,
    XXBYTE_YARV_ID_EXPANDARRAY = 31,
    XXBYTE_YARV_ID_CONCATARRAY = 32,
    XXBYTE_YARV_ID_SPLATARRAY = 33,
    XXBYTE_YARV_ID_NEWHASH = 34,
    XXBYTE_YARV_ID_NEWRANGE = 35,
    XXBYTE_YARV_ID_POP = 36,
    XXBYTE_YARV_ID_DUP = 37,
    XXBYTE_YARV_ID_DUPN = 38,
    XXBYTE_YARV_ID_SWAP = 39,
    XXBYTE_YARV_ID_REVERSE = 40,
    XXBYTE_YARV_ID_REPUT = 41,
    XXBYTE_YARV_ID_TOPN = 42,
    XXBYTE_YARV_ID_SETN = 43,
    XXBYTE_YARV_ID_ADJUSTSTACK = 44,
    XXBYTE_YARV_ID_DEFINED = 45,
    XXBYTE_YARV_ID_CHECKMATCH = 46,
    XXBYTE_YARV_ID_CHECKKEYWORD = 47,
    XXBYTE_YARV_ID_CHECKTYPE = 48,
    XXBYTE_YARV_ID_DEFINECLASS = 49,
    XXBYTE_YARV_ID_SEND = 50,
    XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK = 51,
    XXBYTE_YARV_ID_OPT_STR_FREEZE = 52,
    XXBYTE_YARV_ID_OPT_STR_UMINUS = 53,
    XXBYTE_YARV_ID_OPT_NEWARRAY_MAX = 54,
    XXBYTE_YARV_ID_OPT_NEWARRAY_MIN = 55,
    XXBYTE_YARV_ID_INVOKESUPER = 56,
    XXBYTE_YARV_ID_INVOKEBLOCK = 57,
    XXBYTE_YARV_ID_LEAVE = 58,
    XXBYTE_YARV_ID_THROW = 59,
    XXBYTE_YARV_ID_JUMP = 60,
    XXBYTE_YARV_ID_BRANCHIF = 61,
    XXBYTE_YARV_ID_BRANCHUNLESS = 62,
    XXBYTE_YARV_ID_BRANCHNIL = 63,
    XXBYTE_YARV_ID_OPT_GETINLINECACHE = 64,
    XXBYTE_YARV_ID_OPT_SETINLINECACHE = 65,
    XXBYTE_YARV_ID_ONCE = 66,
    XXBYTE_YARV_ID_OPT_CASE_DISPATCH = 67,
    XXBYTE_YARV_ID_OPT_PLUS = 68,
    XXBYTE_YARV_ID_OPT_MINUS = 69,
    XXBYTE_YARV_ID_OPT_MULT = 70,
    XXBYTE_YARV_ID_OPT_DIV = 71,
    XXBYTE_YARV_ID_OPT_MOD = 72,
    XXBYTE_YARV_ID_OPT_EQ = 73,
    XXBYTE_YARV_ID_OPT_NEQ = 74,
    XXBYTE_YARV_ID_OPT_LT = 75,
    XXBYTE_YARV_ID_OPT_LE = 76,
    XXBYTE_YARV_ID_OPT_GT = 77,
    XXBYTE_YARV_ID_OPT_GE = 78,
    XXBYTE_YARV_ID_OPT_LTLT = 79,
    XXBYTE_YARV_ID_OPT_AND = 80,
    XXBYTE_YARV_ID_OPT_OR = 81,
    XXBYTE_YARV_ID_OPT_AREF = 82,
    XXBYTE_YARV_ID_OPT_ASET = 83,
    XXBYTE_YARV_ID_OPT_ASET_WITH = 84,
    XXBYTE_YARV_ID_OPT_AREF_WITH = 85,
    XXBYTE_YARV_ID_OPT_LENGTH = 86,
    XXBYTE_YARV_ID_OPT_SIZE = 87,
    XXBYTE_YARV_ID_OPT_EMPTY_P = 88,
    XXBYTE_YARV_ID_OPT_SUCC = 89,
    XXBYTE_YARV_ID_OPT_NOT = 90,
    XXBYTE_YARV_ID_OPT_REGEXPMATCH1 = 91,
    XXBYTE_YARV_ID_OPT_REGEXPMATCH2 = 92,
    XXBYTE_YARV_ID_OPT_CALL_C_FUNCTION = 93,
    XXBYTE_YARV_ID_BITBLT = 94,
    XXBYTE_YARV_ID_ANSWER = 95,
    XXBYTE_YARV_ID_GETLOCAL_WC_0 = 96,
    XXBYTE_YARV_ID_GETLOCAL_WC_1 = 97,
    XXBYTE_YARV_ID_SETLOCAL_WC_0 = 98,
    XXBYTE_YARV_ID_SETLOCAL_WC_1 = 99,
    XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_ = 100,
    XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_ = 101,
    XXBYTE_YARV_ID_TRACE_NOP = 102,
    XXBYTE_YARV_ID_TRACE_GETLOCAL = 103,
    XXBYTE_YARV_ID_TRACE_SETLOCAL = 104,
    XXBYTE_YARV_ID_TRACE_GETBLOCKPARAM = 105,
    XXBYTE_YARV_ID_TRACE_SETBLOCKPARAM = 106,
    XXBYTE_YARV_ID_TRACE_GETBLOCKPARAMPROXY = 107,
    XXBYTE_YARV_ID_TRACE_GETSPECIAL = 108,
    XXBYTE_YARV_ID_TRACE_SETSPECIAL = 109,
    XXBYTE_YARV_ID_TRACE_GETINSTANCEVARIABLE = 110,
    XXBYTE_YARV_ID_TRACE_SETINSTANCEVARIABLE = 111,
    XXBYTE_YARV_ID_TRACE_GETCLASSVARIABLE = 112,
    XXBYTE_YARV_ID_TRACE_SETCLASSVARIABLE = 113,
    XXBYTE_YARV_ID_TRACE_GETCONSTANT = 114,
    XXBYTE_YARV_ID_TRACE_SETCONSTANT = 115,
    XXBYTE_YARV_ID_TRACE_GETGLOBAL = 116,
    XXBYTE_YARV_ID_TRACE_SETGLOBAL = 117,
    XXBYTE_YARV_ID_TRACE_PUTNIL = 118,
    XXBYTE_YARV_ID_TRACE_PUTSELF = 119,
    XXBYTE_YARV_ID_TRACE_PUTOBJECT = 120,
    XXBYTE_YARV_ID_TRACE_PUTSPECIALOBJECT = 121,
    XXBYTE_YARV_ID_TRACE_PUTISEQ = 122,
    XXBYTE_YARV_ID_TRACE_PUTSTRING = 123,
    XXBYTE_YARV_ID_TRACE_CONCATSTRINGS = 124,
    XXBYTE_YARV_ID_TRACE_TOSTRING = 125,
    XXBYTE_YARV_ID_TRACE_FREEZESTRING = 126,
    XXBYTE_YARV_ID_TRACE_TOREGEXP = 127,
    XXBYTE_YARV_ID_TRACE_INTERN = 128,
    XXBYTE_YARV_ID_TRACE_NEWARRAY = 129,
    XXBYTE_YARV_ID_TRACE_DUPARRAY = 130,
    XXBYTE_YARV_ID_TRACE_DUPHASH = 131,
    XXBYTE_YARV_ID_TRACE_EXPANDARRAY = 132,
    XXBYTE_YARV_ID_TRACE_CONCATARRAY = 133,
    XXBYTE_YARV_ID_TRACE_SPLATARRAY = 134,
    XXBYTE_YARV_ID_TRACE_NEWHASH = 135,
    XXBYTE_YARV_ID_TRACE_NEWRANGE = 136,
    XXBYTE_YARV_ID_TRACE_POP = 137,
    XXBYTE_YARV_ID_TRACE_DUP = 138,
    XXBYTE_YARV_ID_TRACE_DUPN = 139,
    XXBYTE_YARV_ID_TRACE_SWAP = 140,
    XXBYTE_YARV_ID_TRACE_REVERSE = 141,
    XXBYTE_YARV_ID_TRACE_REPUT = 142,
    XXBYTE_YARV_ID_TRACE_TOPN = 143,
    XXBYTE_YARV_ID_TRACE_SETN = 144,
    XXBYTE_YARV_ID_TRACE_ADJUSTSTACK = 145,
    XXBYTE_YARV_ID_TRACE_DEFINED = 146,
    XXBYTE_YARV_ID_TRACE_CHECKMATCH = 147,
    XXBYTE_YARV_ID_TRACE_CHECKKEYWORD = 148,
    XXBYTE_YARV_ID_TRACE_CHECKTYPE = 149,
    XXBYTE_YARV_ID_TRACE_DEFINECLASS = 150,
    XXBYTE_YARV_ID_TRACE_SEND = 151,
    XXBYTE_YARV_ID_TRACE_OPT_SEND_WITHOUT_BLOCK = 152,
    XXBYTE_YARV_ID_TRACE_OPT_STR_FREEZE = 153,
    XXBYTE_YARV_ID_TRACE_OPT_STR_UMINUS = 154,
    XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MAX = 155,
    XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MIN = 156,
    XXBYTE_YARV_ID_TRACE_INVOKESUPER = 157,
    XXBYTE_YARV_ID_TRACE_INVOKEBLOCK = 158,
    XXBYTE_YARV_ID_TRACE_LEAVE = 159,
    XXBYTE_YARV_ID_TRACE_THROW = 160,
    XXBYTE_YARV_ID_TRACE_JUMP = 161,
    XXBYTE_YARV_ID_TRACE_BRANCHIF = 162,
    XXBYTE_YARV_ID_TRACE_BRANCHUNLESS = 163,
    XXBYTE_YARV_ID_TRACE_BRANCHNIL = 164,
    XXBYTE_YARV_ID_TRACE_OPT_GETINLINECACHE = 165,
    XXBYTE_YARV_ID_TRACE_OPT_SETINLINECACHE = 166,
    XXBYTE_YARV_ID_TRACE_ONCE = 167,
    XXBYTE_YARV_ID_TRACE_OPT_CASE_DISPATCH = 168,
    XXBYTE_YARV_ID_TRACE_OPT_PLUS = 169,
    XXBYTE_YARV_ID_TRACE_OPT_MINUS = 170,
    XXBYTE_YARV_ID_TRACE_OPT_MULT = 171,
    XXBYTE_YARV_ID_TRACE_OPT_DIV = 172,
    XXBYTE_YARV_ID_TRACE_OPT_MOD = 173,
    XXBYTE_YARV_ID_TRACE_OPT_EQ = 174,
    XXBYTE_YARV_ID_TRACE_OPT_NEQ = 175,
    XXBYTE_YARV_ID_TRACE_OPT_LT = 176,
    XXBYTE_YARV_ID_TRACE_OPT_LE = 177,
    XXBYTE_YARV_ID_TRACE_OPT_GT = 178,
    XXBYTE_YARV_ID_TRACE_OPT_GE = 179,
    XXBYTE_YARV_ID_TRACE_OPT_LTLT = 180,
    XXBYTE_YARV_ID_TRACE_OPT_AND = 181,
    XXBYTE_YARV_ID_TRACE_OPT_OR = 182,
    XXBYTE_YARV_ID_TRACE_OPT_AREF = 183,
    XXBYTE_YARV_ID_TRACE_OPT_ASET = 184,
    XXBYTE_YARV_ID_TRACE_OPT_ASET_WITH = 185,
    XXBYTE_YARV_ID_TRACE_OPT_AREF_WITH = 186,
    XXBYTE_YARV_ID_TRACE_OPT_LENGTH = 187,
    XXBYTE_YARV_ID_TRACE_OPT_SIZE = 188,
    XXBYTE_YARV_ID_TRACE_OPT_EMPTY_P = 189,
    XXBYTE_YARV_ID_TRACE_OPT_SUCC = 190,
    XXBYTE_YARV_ID_TRACE_OPT_NOT = 191,
    XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH1 = 192,
    XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH2 = 193,
    XXBYTE_YARV_ID_TRACE_OPT_CALL_C_FUNCTION = 194,
    XXBYTE_YARV_ID_TRACE_BITBLT = 195,
    XXBYTE_YARV_ID_TRACE_ANSWER = 196,
    XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_0 = 197,
    XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_1 = 198,
    XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_0 = 199,
    XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_1 = 200,
    XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_0_ = 201,
    XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_1_ = 202,
    XXBYTE_YARV_ID_NEWARRAYKWSPLAT = 203,
    XXBYTE_YARV_ID_DEFINEMETHOD = 204,
    XXBYTE_YARV_ID_DEFINESMETHOD = 205,
    XXBYTE_YARV_ID_OPT_NIL_P = 206,
    XXBYTE_YARV_ID_INVOKEBUILTIN = 207,
    XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE = 208,
    XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE_LEAVE = 209,
    XXBYTE_YARV_ID_TRACE_NEWARRAYKWSPLAT = 210,
    XXBYTE_YARV_ID_TRACE_DEFINEMETHOD = 211,
    XXBYTE_YARV_ID_TRACE_DEFINESMETHOD = 212,
    XXBYTE_YARV_ID_TRACE_OPT_NIL_P = 213,
    XXBYTE_YARV_ID_TRACE_INVOKEBUILTIN = 214,
    XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE = 215,
    XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE_LEAVE = 216,
    XXBYTE_YARV_ID_ANYTOSTRING = 217,
    XXBYTE_YARV_ID_OBJTOSTRING = 218,
    XXBYTE_YARV_ID_TRACE_ANYTOSTRING = 219,
    XXBYTE_YARV_ID_TRACE_OBJTOSTRING = 220,
    XXBYTE_YARV_ID_OPT_GETCONSTANT_PATH = 221,
    XXBYTE_YARV_ID_OPT_REVERSE = 222,
    XXBYTE_YARV_ID_TRACE_OPT_GETCONSTANT_PATH = 223,
    XXBYTE_YARV_ID_TRACE_OPT_REVERSE = 224,
    XXBYTE_YARV_ID_SPLATKW = 225,
    XXBYTE_YARV_ID_DEFINEDIVAR = 226,
    XXBYTE_YARV_ID_OPT_NEWARRAY_SEND = 227,
    XXBYTE_YARV_ID_TRACE_SPLATKW = 228,
    XXBYTE_YARV_ID_TRACE_DEFINEDIVAR = 229,
    XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_SEND = 230,
    XXBYTE_YARV_ID_PUTCHILLEDSTRING = 231,
    XXBYTE_YARV_ID_PUSHTOARRAYKWSPLAT = 232,
    XXBYTE_YARV_ID_CONCATTOARRAY = 233,
    XXBYTE_YARV_ID_PUSHTOARRAY = 234,
    XXBYTE_YARV_ID_SENDFORWARD = 235,
    XXBYTE_YARV_ID_OPT_ARY_FREEZE = 236,
    XXBYTE_YARV_ID_OPT_HASH_FREEZE = 237,
    XXBYTE_YARV_ID_OPT_DUPARRAY_SEND = 238,
    XXBYTE_YARV_ID_INVOKESUPERFORWARD = 239,
    XXBYTE_YARV_ID_TRACE_PUTCHILLEDSTRING = 240,
    XXBYTE_YARV_ID_TRACE_PUSHTOARRAYKWSPLAT = 241,
    XXBYTE_YARV_ID_TRACE_CONCATTOARRAY = 242,
    XXBYTE_YARV_ID_TRACE_PUSHTOARRAY = 243,
    XXBYTE_YARV_ID_TRACE_SENDFORWARD = 244,
    XXBYTE_YARV_ID_TRACE_OPT_ARY_FREEZE = 245,
    XXBYTE_YARV_ID_TRACE_OPT_HASH_FREEZE = 246,
    XXBYTE_YARV_ID_TRACE_OPT_DUPARRAY_SEND = 247,
    XXBYTE_YARV_ID_TRACE_INVOKESUPERFORWARD = 248,
    XXBYTE_YARV_ID_OPT_NEW = 249,
    XXBYTE_YARV_ID_TRACE_OPT_NEW = 250,
    XXBYTE_YARV_ID_ZJIT_GETINSTANCEVARIABLE = 251,
    XXBYTE_YARV_ID_ZJIT_SETINSTANCEVARIABLE = 252,
    XXBYTE_YARV_ID_ZJIT_DEFINEDIVAR = 253,
    XXBYTE_YARV_ID_ZJIT_SEND = 254,
    XXBYTE_YARV_ID_ZJIT_OPT_SEND_WITHOUT_BLOCK = 255,
    XXBYTE_YARV_ID_ZJIT_OBJTOSTRING = 256,
    XXBYTE_YARV_ID_ZJIT_OPT_NIL_P = 257,
    XXBYTE_YARV_ID_ZJIT_INVOKEBLOCK = 258,
    XXBYTE_YARV_ID_ZJIT_OPT_PLUS = 259,
    XXBYTE_YARV_ID_ZJIT_OPT_MINUS = 260,
    XXBYTE_YARV_ID_ZJIT_OPT_MULT = 261,
    XXBYTE_YARV_ID_ZJIT_OPT_DIV = 262,
    XXBYTE_YARV_ID_ZJIT_OPT_MOD = 263,
    XXBYTE_YARV_ID_ZJIT_OPT_EQ = 264,
    XXBYTE_YARV_ID_ZJIT_OPT_NEQ = 265,
    XXBYTE_YARV_ID_ZJIT_OPT_LT = 266,
    XXBYTE_YARV_ID_ZJIT_OPT_LE = 267,
    XXBYTE_YARV_ID_ZJIT_OPT_GT = 268,
    XXBYTE_YARV_ID_ZJIT_OPT_GE = 269,
    XXBYTE_YARV_ID_ZJIT_OPT_LTLT = 270,
    XXBYTE_YARV_ID_ZJIT_OPT_AND = 271,
    XXBYTE_YARV_ID_ZJIT_OPT_OR = 272,
    XXBYTE_YARV_ID_ZJIT_OPT_AREF = 273,
    XXBYTE_YARV_ID_ZJIT_OPT_ASET = 274,
    XXBYTE_YARV_ID_ZJIT_OPT_LENGTH = 275,
    XXBYTE_YARV_ID_ZJIT_OPT_SIZE = 276,
    XXBYTE_YARV_ID_ZJIT_OPT_EMPTY_P = 277,
    XXBYTE_YARV_ID_ZJIT_OPT_SUCC = 278,
    XXBYTE_YARV_ID_ZJIT_OPT_NOT = 279,
    XXBYTE_YARV_ID_ZJIT_OPT_REGEXPMATCH2 = 280,
    XXBYTE_YARV_ID_COUNT = 281 /* one past the last id */
} xxbyte_yarv_id;

/* The instruction name for id, or NULL. */
XXBYTE_API const char *xxbyte_yarv_mnemonic(uint32_t id);

/* The name of instruction number opcode in mode, or NULL when it is not
 * defined there or mode is unsupported. */
XXBYTE_API const char *xxbyte_yarv_insn_name(uint32_t mode, uint32_t opcode);

/* The instruction's length in VALUE slots (1 + its operand count), or 0
 * when opcode is not defined in mode. */
XXBYTE_API uint32_t xxbyte_yarv_insn_len(uint32_t mode, uint32_t opcode);

/*
 * The operand types of opcode in mode as Ruby's TS_* letters, or NULL:
 * 'V' VALUE, 'I' ID, 'G' GENTRY, 'S' ISEQ, 'L' LINDEX, 'N' NUM, 'O' OFFSET,
 * 'H' CDHASH, 'K' IC, 'A' IVC, 'J' ICVARC, 'T' ISE, 'C' CALLDATA (2.7+) or
 * CALLINFO (2.6), 'E' CALLCACHE, 'F' FUNCPTR, 'R' BUILTIN.
 */
XXBYTE_API const char *xxbyte_yarv_operand_types(uint32_t mode, uint32_t opcode);

/*
 * Decode context for compact code (see "Addresses"): a slot map built by
 * xxbyte_yarv_build_slot_map from the iseq's bytecode, and base, the
 * address of the bytecode's first byte. Pass it to xxbyte_decode_ex or
 * xxbyte_disasm_buf_ex with addresses base + byte offset. Raw-word (2.6)
 * modes ignore it.
 */
typedef struct xxbyte_yarv_context {
    uint64_t base;
    const uint32_t *offsets; /* offsets[slot]: byte offset of the
                                instruction that occupies the slot */
    size_t slot_count;       /* the iseq_size in slots */
    size_t code_size;        /* the bytecode size in bytes, for branch
                                targets outside the map; 0 if unknown */
} xxbyte_yarv_context;

/*
 * Decodes code[0..code_size) linearly and stores, for every VALUE slot, the
 * byte offset of the instruction that occupies it: offsets[0..*slot_count).
 * Entries past capacity are not written, so capacity 0 measures. Returns
 * XXBYTE_STATUS_OK when the code ends at an instruction boundary, otherwise
 * the status of the instruction that failed (*slot_count then counts the
 * slots before it). code_size must not exceed UINT32_MAX.
 */
XXBYTE_API xxbyte_status xxbyte_yarv_build_slot_map(uint32_t mode, const uint8_t *code, size_t code_size, uint32_t *offsets, size_t capacity, size_t *slot_count);

/* IBF iseq types (xxbyte_yarv_iseq.type). */
#define XXBYTE_YARV_ISEQ_TYPE_TOP 0u
#define XXBYTE_YARV_ISEQ_TYPE_METHOD 1u
#define XXBYTE_YARV_ISEQ_TYPE_BLOCK 2u
#define XXBYTE_YARV_ISEQ_TYPE_CLASS 3u
#define XXBYTE_YARV_ISEQ_TYPE_RESCUE 4u
#define XXBYTE_YARV_ISEQ_TYPE_ENSURE 5u
#define XXBYTE_YARV_ISEQ_TYPE_EVAL 6u
#define XXBYTE_YARV_ISEQ_TYPE_MAIN 7u
#define XXBYTE_YARV_ISEQ_TYPE_PLAIN 8u

/* A parsed IBF ("YARB") header. Offsets are from the start of the image. */
typedef struct xxbyte_yarv_ibf_header {
    uint32_t major_version;      /* Ruby major version */
    uint32_t minor_version;      /* as stored: the minor version, or
                                    minor * 10000 + n for a RUBY_DEVEL
                                    build */
    uint32_t size;               /* image size without the extra data */
    uint32_t extra_size;         /* extra data after the image */
    uint32_t iseq_list_size;     /* iseqs; iseq 0 is the top level */
    uint32_t iseq_list_offset;   /* uint32 offset per iseq */
    uint32_t object_list_size;   /* (global) object list entries */
    uint32_t object_list_offset; /* uint32 offset per object */
    uint32_t id_list_size;       /* 2.6 only: ID list entries */
    uint32_t id_list_offset;     /* 2.6 only: a C long per ID */
    uint32_t header_size;        /* 44 (2.6), 36 (2.7 to 3.2), 40 (3.3+) */
    uint32_t platform_offset;    /* RUBY_PLATFORM, NUL-terminated, after the
                                    header (2.6 to 3.2); 0 from 3.3 */
    uint32_t platform_size;      /* its length without the NUL */
    uint8_t endian;              /* 'l' or 'b' */
    uint8_t word_size;           /* sizeof(VALUE): 4 or 8 */
    uint8_t development;         /* 1 for a RUBY_DEVEL build */
    uint8_t reserved;
    uint32_t mode; /* decode mode for the image's code */
} xxbyte_yarv_ibf_header;

/*
 * Parses the IBF header at data[0]. Checks the "YARB" magic, the version,
 * that size fits in the input, that the iseq and object lists (and the
 * 2.6 ID list) are 4-byte aligned and lie within size, and, up to 3.2,
 * that the platform string is NUL-terminated within size.
 *
 * The byte order is taken from the header (3.3+) or from the version
 * fields; the word size from the header (3.3+) or else from the platform
 * string, a heuristic: 4 when the OS part ends in "x32" (an x32 ABI such
 * as x86_64-linux-gnux32), else 8 when the CPU part (up to the first '-')
 * contains "64", starts with "universal", "alpha" or "e2k", or is s390x or
 * sparcv9, otherwise 4. A caller that knows better sets or clears
 * XXBYTE_YARV_MODE_WORD32 in mode (and in its own copy of the header's
 * mode before xxbyte_yarv_locate_iseq). mode combines the release with
 * XXBYTE_YARV_MODE_WORD32 and XXBYTE_YARV_MODE_BIG_ENDIAN as applicable;
 * a development build maps to its release (its format may differ).
 *
 * Returns XXBYTE_STATUS_TRUNCATED when data is too short for the header or
 * for size, XXBYTE_STATUS_INVALID_INSTRUCTION for a bad magic or layout,
 * and XXBYTE_STATUS_UNSUPPORTED_MODE for another Ruby version (then only
 * major_version, minor_version and endian are set). On any other failure
 * *header is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_yarv_parse_ibf_header(const uint8_t *data, size_t size, xxbyte_yarv_ibf_header *header);

/* Where an iseq's bytecode lies in the image. */
typedef struct xxbyte_yarv_iseq {
    uint32_t type;        /* XXBYTE_YARV_ISEQ_TYPE_* as stored */
    uint32_t iseq_size;   /* code length in VALUE slots */
    uint32_t code_offset; /* first bytecode byte, from the image start */
    uint32_t code_size;   /* bytecode length in bytes */
    uint32_t body_offset; /* the iseq's body record (2.6: the dumped
                             rb_iseq_constant_body) */
} xxbyte_yarv_iseq;

/*
 * Locates the bytecode of iseq index (< header->iseq_list_size) of the
 * image whose header xxbyte_yarv_parse_ibf_header parsed from the same
 * data and size. Decode code_size bytes at code_offset in header->mode.
 * Returns XXBYTE_STATUS_INVALID_ARGUMENT for a bad index or header,
 * XXBYTE_STATUS_TRUNCATED when the body record or the code runs past
 * header->size (or size), and XXBYTE_STATUS_INVALID_INSTRUCTION when the
 * record is malformed (a field that does not fit 32 bits, a code offset
 * before the image start). *iseq is zeroed on failure.
 */
XXBYTE_API xxbyte_status xxbyte_yarv_locate_iseq(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint32_t index, xxbyte_yarv_iseq *iseq);

/*
 * IBF containers
 * --------------
 * The helpers below read the rest of an image whose header
 * xxbyte_yarv_parse_ibf_header parsed from the same data and size (pass
 * that header, or a copy whose mode has WORD32 set or cleared). Every read
 * is bounds-checked against the image (header->size, clipped to size):
 * XXBYTE_STATUS_INVALID_ARGUMENT for NULL pointers or a bad header or
 * index, XXBYTE_STATUS_TRUNCATED when a record runs past the image, and
 * XXBYTE_STATUS_INVALID_INSTRUCTION for a malformed record. Native-layout
 * parts (Ruby 2.6 records, raw VALUE, ID, long and int fields of later
 * versions) are read with the image's word size and byte order; a C long
 * is 4 bytes for a 2.6 to 3.2 image whose platform names mingw or mswin.
 *
 * Objects. The (global) object list holds every literal an image uses;
 * entry 0 is nil and is never stored. An object is a header (type,
 * special_const, frozen, internal; a byte from 2.7, a 4-byte bit-field
 * word in 2.6) and a body by type:
 *   - special_const: value is the raw VALUE (a Fixnum 2n+1, nil, true,
 *     false; their encodings changed in 3.2, see the inspect rules).
 *   - T_CLASS: value is the class index (Object, Array, StandardError,
 *     NoMatchingPatternError, TypeError, NoMatchingPatternKeyError).
 *   - T_FLOAT: value is the IEEE-754 bit pattern.
 *   - T_STRING: encoding (an encoding index; above 12 it is 12 plus the
 *     object index of the encoding's name), length bytes at data_offset.
 *   - T_SYMBOL: from 3.1 like T_STRING; up to 3.0 a is the String object.
 *   - T_REGEXP: a is the source String object, flags the options byte.
 *   - T_ARRAY: length element object indexes (xxbyte_yarv_object_elements).
 *   - T_HASH: length key/value pairs (2 * length elements).
 *   - T_STRUCT (a Range): a is begin, b end, flags 1 for an exclusive end.
 *   - T_BIGNUM: length 32-bit digits at data_offset, least significant
 *     first, flags 1 when negative.
 *   - T_DATA (an Encoding): value is the data type (0), the NUL-terminated
 *     name of length bytes (with the NUL) is at data_offset.
 *   - T_COMPLEX, T_RATIONAL: a and b are the real and imaginary part or
 *     numerator and denominator.
 */
#define XXBYTE_YARV_T_OBJECT 0x01u
#define XXBYTE_YARV_T_CLASS 0x02u
#define XXBYTE_YARV_T_FLOAT 0x04u
#define XXBYTE_YARV_T_STRING 0x05u
#define XXBYTE_YARV_T_REGEXP 0x06u
#define XXBYTE_YARV_T_ARRAY 0x07u
#define XXBYTE_YARV_T_HASH 0x08u
#define XXBYTE_YARV_T_STRUCT 0x09u
#define XXBYTE_YARV_T_BIGNUM 0x0Au
#define XXBYTE_YARV_T_DATA 0x0Cu
#define XXBYTE_YARV_T_COMPLEX 0x0Eu
#define XXBYTE_YARV_T_RATIONAL 0x0Fu
#define XXBYTE_YARV_T_NIL 0x11u
#define XXBYTE_YARV_T_TRUE 0x12u
#define XXBYTE_YARV_T_FALSE 0x13u
#define XXBYTE_YARV_T_SYMBOL 0x14u
#define XXBYTE_YARV_T_FIXNUM 0x15u

typedef struct xxbyte_yarv_object {
    uint32_t offset; /* the object header, from the image start */
    uint8_t type;    /* XXBYTE_YARV_T_* (5 bits) */
    uint8_t special_const;
    uint8_t frozen;
    uint8_t internal;
    uint32_t body_offset; /* the type-specific body */
    uint32_t data_offset; /* string bytes, first element, digits, name */
    uint32_t flags;       /* Regexp options, Range exclude_end, Bignum
                             sign (1 = negative) */
    uint32_t encoding;    /* String and Symbol encoding index */
    uint64_t length;      /* bytes, elements, pairs, digits (see above) */
    uint64_t value;       /* raw VALUE, float bits, class index, data
                             type */
    uint64_t a;           /* referenced object indexes (see above) */
    uint64_t b;
} xxbyte_yarv_object;

/* Reads object index of the object list. Index 0 (nil) is
 * XXBYTE_STATUS_INVALID_ARGUMENT like any index past the list; an unknown
 * type is XXBYTE_STATUS_INVALID_INSTRUCTION. On failure *object is zeroed. */
XXBYTE_API xxbyte_status xxbyte_yarv_read_object(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint64_t index, xxbyte_yarv_object *object);

/* The element object indexes of an Array, or the alternating key and value
 * indexes of a Hash, read into indexes[0..min(capacity, *count)); *count
 * receives the number of elements. indexes may be NULL when capacity is 0. */
XXBYTE_API xxbyte_status xxbyte_yarv_object_elements(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_object *object,
                                                     uint64_t *indexes, size_t capacity, size_t *count);

/* The object holding the name of an ID operand (TS_ID, TS_GENTRY, a call
 * info mid, a local table entry): from 2.7 the operand is itself the index
 * of a Symbol (or, for a hidden local from 3.3, of an Integer); in 2.6 it
 * indexes the ID list, whose entry is the index of a String. 0 means no
 * name and gives *object_index 0. */
XXBYTE_API xxbyte_status xxbyte_yarv_id_object(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint64_t id, uint64_t *object_index);

/*
 * The text RubyVM::InstructionSequence#disasm prints for object index (Ruby's
 * inspect of the loaded object): nil, true, false, Integers, Floats
 * ("2.5", "1.0e+100", "-0.0", "Infinity", "NaN"), Strings ("\"a\\n\"", see
 * below), Symbols (":a", ":\"a b\"", ":$1", ":[]="), Regexps ("/a\\/b/mix",
 * "n" for no encoding), Arrays ("[1, :a]"), Hashes ("{:a=>1}" up to 3.3,
 * "{a: 1, \"k\" => 2}" from 3.4), Ranges ("1..2", "1...", "..2"),
 * Rationals ("(3/1)"), Complexes ("(0+2i)", "(1.5-Infinity*i)"), Bignums
 * in decimal, Encodings ("#<Encoding:UTF-8>"; "BINARY (ASCII-8BIT)" from
 * 3.4) and the classes by name ("StandardError"). Strings print as
 * rb_str_inspect does with Encoding.default_external UTF-8: escapes \n \t
 * \r \f \v \b \a \e, \" \\ and \# before $ @ {, UTF-8 characters
 * verbatim when Onigmo's Unicode Print class of the release's Unicode
 * version (12.1 up to Ruby 3.0, 13.0 in 3.1, 15.0 in 3.2 to 3.4, 17.0
 * in 4.0) holds them (U+0085 too before 3.2), else \uXXXX or \u{XXXXX},
 * invalid bytes as \xHH, \uXXXX for UTF-16/32 characters, \x{XXXX}
 * for the multibyte characters of EUC-JP, Shift_JIS/Windows-31J, Big5,
 * GBK, GB18030, EUC-KR/GB2312, CP949 and EUC-TW (by the encoding's name;
 * a stored index 12, the writer's first loaded encoding, is taken as
 * Big5), and \xHH for non-ASCII bytes of other encodings (correct for
 * single-byte ones). Nesting
 * stops at 32 levels and 2^20 objects ("..." then stands in); a reference
 * past the list prints "?".
 * Writes at most buffer_size - 1 bytes and a NUL and returns the full
 * length (capped at 8192), 0 for a bad argument or index; buffer_size 0
 * measures.
 */
XXBYTE_API size_t xxbyte_yarv_inspect_object(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint64_t index, char *buffer, size_t buffer_size);

/* Parameter flags (xxbyte_yarv_iseq_info.param_flags), in the bit order
 * of the 2.7+ dump; 2.6 images are converted. */
#define XXBYTE_YARV_PARAM_LEAD 0x0001u
#define XXBYTE_YARV_PARAM_OPT 0x0002u
#define XXBYTE_YARV_PARAM_REST 0x0004u
#define XXBYTE_YARV_PARAM_POST 0x0008u
#define XXBYTE_YARV_PARAM_KW 0x0010u
#define XXBYTE_YARV_PARAM_KWREST 0x0020u
#define XXBYTE_YARV_PARAM_BLOCK 0x0040u
#define XXBYTE_YARV_PARAM_AMBIGUOUS_PARAM0 0x0080u
#define XXBYTE_YARV_PARAM_ACCEPTS_NO_KWARG 0x0100u /* 2.7+ */
#define XXBYTE_YARV_PARAM_RUBY2_KEYWORDS 0x0200u   /* 2.7+ */
#define XXBYTE_YARV_PARAM_ANON_REST 0x0400u        /* 3.3+ */
#define XXBYTE_YARV_PARAM_ANON_KWREST 0x0800u      /* 3.3+ */
#define XXBYTE_YARV_PARAM_USE_BLOCK 0x1000u        /* 3.4+ */
#define XXBYTE_YARV_PARAM_FORWARDABLE 0x2000u      /* 3.4+ */

/*
 * The body record of an iseq: the fields the naming and listing need.
 * Offsets are from the image start (0 when absent), indexes into the
 * object list (pathobj, labels) or the iseq list (-1 for none). Fields a
 * version lacks are 0, -1 for iseq indexes.
 */
typedef struct xxbyte_yarv_iseq_info {
    xxbyte_yarv_iseq code; /* as xxbyte_yarv_locate_iseq */
    uint32_t param_flags;  /* XXBYTE_YARV_PARAM_* */
    uint32_t param_size;
    int32_t lead_num;
    int32_t opt_num;
    int32_t rest_start;
    int32_t post_start;
    int32_t post_num;
    int32_t block_start;
    uint32_t opt_table_offset; /* opt_num + 1 VALUE words: slot pcs */
    uint32_t keyword_offset;   /* struct rb_iseq_param_keyword, or 0 */
    int32_t keyword_num;       /* its num, required_num, bits_start and */
    int32_t keyword_required_num;
    int32_t keyword_bits_start;
    int32_t keyword_rest_start; /* rest_start (0 without a keyword record) */
    uint64_t pathobj;           /* a String, or an Array [path, realpath] */
    uint64_t base_label;
    uint64_t label;
    int64_t first_lineno;
    int32_t node_id;
    int32_t beg_lineno;
    int32_t beg_column;
    int32_t end_lineno;
    int32_t end_column;
    uint32_t insns_info_offset;      /* line, (node id), events records */
    uint32_t insns_positions_offset; /* their slot positions */
    uint32_t insns_info_size;
    uint32_t local_table_offset; /* local_table_size ID words */
    uint32_t local_table_size;
    uint32_t catch_table_offset;
    uint32_t catch_table_size;
    int32_t parent_iseq;
    int32_t local_iseq;
    int32_t mandatory_only_iseq; /* 3.1+ */
    uint32_t ci_offset;          /* call infos */
    uint32_t ci_size;
    uint32_t ci_kw_size;             /* 2.6 and 2.7: keyword call infos,
                                        stored after the ci_size others */
    uint32_t outer_variables_offset; /* 3.0+ */
    uint32_t is_size;                /* up to 3.1: inline storage entries */
    uint32_t ivc_size;               /* 3.2+ */
    uint32_t icvarc_size;            /* 3.2+ */
    uint32_t ise_size;               /* 3.2+ */
    uint32_t ic_size;                /* 3.2+ */
    uint32_t stack_max;
    uint32_t catch_except_p; /* up to 3.2 */
    uint32_t builtin_attrs;  /* 3.3+ (3.0 to 3.2: builtin_inline_p) */
    uint32_t prism;          /* 3.4+ */
    int64_t flip_count;
} xxbyte_yarv_iseq_info;

/* Parses the body record of iseq index (see xxbyte_yarv_locate_iseq, which
 * this includes). On failure *info is zeroed. */
XXBYTE_API xxbyte_status xxbyte_yarv_parse_iseq(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint32_t index, xxbyte_yarv_iseq_info *info);

/* Local table entry i (0 is the first local, the highest "[ N]" of
 * #disasm): the ID operand of its name (see xxbyte_yarv_id_object). */
XXBYTE_API xxbyte_status xxbyte_yarv_local_id(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_iseq_info *info, uint32_t i,
                                              uint64_t *id);

/* Catch table types (xxbyte_yarv_catch_entry.type). */
#define XXBYTE_YARV_CATCH_RESCUE 1u
#define XXBYTE_YARV_CATCH_ENSURE 2u
#define XXBYTE_YARV_CATCH_RETRY 3u
#define XXBYTE_YARV_CATCH_BREAK 4u
#define XXBYTE_YARV_CATCH_REDO 5u
#define XXBYTE_YARV_CATCH_NEXT 6u

/* A catch table row; start, end and cont are slot pcs. */
typedef struct xxbyte_yarv_catch_entry {
    uint32_t type; /* XXBYTE_YARV_CATCH_* (stored as INT2FIX(type)) */
    int32_t iseq;  /* handler iseq index, -1 for none */
    uint32_t start;
    uint32_t end;
    uint32_t cont;
    uint32_t sp;
} xxbyte_yarv_catch_entry;

/* The catch table of *info: info->catch_table_size rows; capacity and
 * count as for xxbyte_yarv_object_elements. */
XXBYTE_API xxbyte_status xxbyte_yarv_read_catch_table(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_iseq_info *info,
                                                      xxbyte_yarv_catch_entry *entries, size_t capacity, size_t *count);

/* A call info (2.6, 2.7: ci_size plain ones, then ci_kw_size keyword ones;
 * 3.0+: ci_size in code order, one per call data operand). */
typedef struct xxbyte_yarv_call_info {
    uint64_t mid;  /* ID operand of the method name, 0 for none */
    uint32_t flag; /* VM_CALL_* bits of the release */
    uint32_t argc;
    uint32_t keyword_count;
    uint32_t keyword_offset; /* keyword_count Symbol indexes: small values
                                (2.7+) or VALUE words (2.6) */
    uint32_t present;        /* 0 for a dumped NULL call info (3.0+) */
} xxbyte_yarv_call_info;

/* The call infos of *info: ci_size + ci_kw_size entries. */
XXBYTE_API xxbyte_status xxbyte_yarv_read_call_info(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_iseq_info *info,
                                                    xxbyte_yarv_call_info *infos, size_t capacity, size_t *count);

/* Keyword i (< keyword_count) of a call info: a Symbol object index. */
XXBYTE_API xxbyte_status xxbyte_yarv_call_info_keyword(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_call_info *ci,
                                                       uint32_t i, uint64_t *object_index);

/* A line table record: the slot pc it starts at, the line, the node id
 * (3.1+, else 0) and the RUBY_EVENT_* bits. */
typedef struct xxbyte_yarv_insn_info {
    uint32_t position;
    int32_t line_no;
    int32_t node_id;
    uint32_t events;
} xxbyte_yarv_insn_info;

/* The line table of *info: insns_info_size records. */
XXBYTE_API xxbyte_status xxbyte_yarv_read_insns_info(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, const xxbyte_yarv_iseq_info *info,
                                                     xxbyte_yarv_insn_info *entries, size_t capacity, size_t *count);

/*
 * Names. xxbyte_yarv_names_init prepares the naming context of one iseq:
 * its body record, a slot map (compact code) and the call info and inline
 * cache of every instruction in code order. Pass &names->decode as the
 * decode context and xxbyte_yarv_resolver with names as the resolver
 * context to xxbyte_disasm_buf_ex / xxbyte_format_ex; code is
 * data + names->info.code.code_offset, names->info.code.code_size bytes,
 * at address base (a file offset when base is code_offset). Ruby 2.6
 * code ignores the decode context; its addresses are base + byte offset
 * too. The context borrows data; free it with xxbyte_yarv_names_free
 * (also after a failed init).
 *
 * The resolver prints operands as #disasm does, except that branch
 * targets are byte addresses:
 *   - TS_OFFSET: "0x" and at least four uppercase hex digits of the target
 *     address ("branchunless 0x01A4").
 *   - TS_LINDEX: the local's name and index in its table, "x@0", "\"$!\"@0"
 *     (3.3+ inspects names that are not local identifiers, earlier
 *     releases names that are not symbol names), "?@5" for a hidden
 *     local; the level selects the table of a parent iseq. checkkeyword's
 *     bits index stays numeric.
 *   - TS_ID: ":name" (Symbol#inspect); TS_GENTRY (2.6, 2.7): the bare name.
 *   - TS_VALUE: the object's inspect text (xxbyte_yarv_inspect_object); the
 *     second operand of defined, a Fixnum n, prints ":$c" or ":$N".
 *   - TS_ISEQ: the child's label, "nil" for none.
 *   - TS_CDHASH: "<cdhash>"; TS_CALLCACHE (2.6): "<callcache>".
 *   - TS_IC: "<is:N>" up to 3.1, "<ic:N A::B>" from 3.2 (N counts all
 *     inline storage: IVC, ICVARC, ISE, then IC entries in code order).
 *   - TS_IVC, TS_ICVARC, TS_ISE: "<is:N>".
 *   - TS_CALLDATA / TS_CALLINFO: "<calldata!mid:puts, argc:1,
 *     FCALL|ARGS_SIMPLE>" ("<callinfo!...>" in 2.6), with "kw:[a,b]" for
 *     keyword calls and the release's flag names.
 *   - TS_BUILTIN: "<builtin!name/?>": the IBF stores no argc.
 * Names longer than the buffer are reported snprintf style (truncated
 * text, full length returned), so the formatter retries with a buffer that
 * holds them; only text past XXBYTE_RESOLVER_NAME_LIMIT is cut and ends in
 * "...". Operands that cannot be resolved keep the numeric form.
 */
typedef struct xxbyte_yarv_names {
    xxbyte_yarv_context decode; /* the decode context */
    const uint8_t *data;
    size_t size;
    xxbyte_yarv_ibf_header header;
    uint32_t iseq_index;
    xxbyte_yarv_iseq_info info;
    uint64_t base;
    size_t instruction_count;
    uint32_t *instructions;            /* byte offset of each instruction */
    uint32_t *calls;                   /* call infos of each instruction (two per
                                          instruction, UINT32_MAX for none) */
    uint32_t *caches;                  /* IC number per instruction, or UINT32_MAX */
    uint32_t *slots;                   /* slot pc per instruction */
    uint32_t *slot_offsets;            /* the slot map (compact code) */
    uint32_t slot_count;               /* the iseq_size the walk found */
    xxbyte_yarv_call_info *call_infos; /* the iseq's call infos */
    size_t call_info_count;
} xxbyte_yarv_names;

/* Returns XXBYTE_STATUS_OK, or the status of the body record or of the
 * first instruction that does not decode (names then stay usable for the
 * instructions before it), or XXBYTE_STATUS_OUT_OF_MEMORY. */
XXBYTE_API xxbyte_status xxbyte_yarv_names_init(xxbyte_yarv_names *names, const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint32_t index,
                                                uint64_t base);
XXBYTE_API void xxbyte_yarv_names_free(xxbyte_yarv_names *names);
/* An xxbyte_resolver; context is an xxbyte_yarv_names. */
XXBYTE_API size_t xxbyte_yarv_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/*
 * Whole images: xxbyte_disasm_file(XXBYTE_FAMILY_YARV, ...) lists the
 * output of RubyVM::InstructionSequence#to_binary (a "YARB" image, Ruby
 * 2.6 to 4.0; extra data after the image is ignored). XXBYTE_MODE_AUTO
 * takes the release, word size and byte order from the header; any other
 * mode must name the image's release and sets the word size and byte
 * order. Every iseq is listed once, depth first from iseq 0 the way
 * #disasm recurses (catch table handlers, then the children in code
 * order), then iseqs no instruction or catch table reaches (a
 * mandatory_only_iseq, ...). Each one is
 *     == disasm: #<ISeq:<main>@t.rb:1 (1,0)-(3,3)> (catch: false)
 *     == catch table
 *     | catch type: rescue st: 0x0040 ed: 0x0052 sp: 0000 cont: 0x0053, rescue in foo
 *     |------------------------------------------------------------------------
 *     local table (size: 2, argc: 1 [opts: 0, rest: -1, post: 0, block: -1, kw: -1@-1, kwrest: -1])
 *     [ 2] x@0<Arg>   [ 1] y@1
 *     000000E1  12 01                    getlocal_WC_0 x@0                    (   2)[LiCa]
 * as #disasm prints it (the header suffix " (catch: ...)" up to 3.2 and the
 * builtin attributes from 3.4, the local table only when the iseq has
 * locals), with byte addresses in place of slot pcs: instruction
 * addresses are file offsets, and catch table st/ed/cont, Opt= labels and
 * branch targets are the addresses of the instructions at those pcs (the
 * end of the code for iseq_size). Instruction lines are the normal
 * listing (address, bytes and text as the listing flags say) with names
 * resolved by xxbyte_yarv_resolver, then the line number where it
 * changes and the events. A blank line separates iseqs. An iseq whose
 * body record does not parse is reported in a "== iseq N: ..." line; one
 * whose code stops decoding is listed up to that point and continues as
 * "db" lines, and a local table that runs past the image is reported in
 * place of its entries. Strings print as xxbyte_yarv_inspect_object
 * describes, in full. Returns
 * XXBYTE_STATUS_INVALID_INSTRUCTION for data that is not a YARB image,
 * XXBYTE_STATUS_UNSUPPORTED_MODE for a mode of another release, the
 * header's status otherwise.
 *
 * Verified against #disasm of RubyVM::InstructionSequence.load_from_binary
 * in Ruby 2.6.10, 2.7.8, 3.0.7, 3.1.7, 3.2.11, 3.3.8, 3.4.13 and 4.0.10
 * (64-bit little-endian builds; 32-bit and big-endian layouts follow the
 * release sources but have no reference run).
 */

#ifdef __cplusplus
}
#endif

#endif
