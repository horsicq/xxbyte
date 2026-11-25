#ifndef XXBYTE_XXBYTE_EBPF_H
#define XXBYTE_XXBYTE_EBPF_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_EBPF
#error "the eBPF decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * eBPF (RFC 9669, "BPF Instruction Set Architecture"): every conformance
 * group (base32, base64, atomic32, atomic64, divmul32, divmul64, packet)
 * plus the later Linux and LLVM additions: load-acquire and store-release,
 * may_goto, addr_space_cast, gotox (indirect jump), the stack-argument
 * register r11 and LLVM's callx.
 *
 * A program is an array of 8-byte slots; the 64-bit immediate load (lddw)
 * takes two. address is a byte address (normally the slot index times 8,
 * for example the offset in an ELF section), and branch targets are
 * absolute byte addresses: address + 8 * (offset + 1). Relocations are not
 * applied, so a call or lddw that the loader patches shows its raw
 * immediate.
 *
 * Decoding from a slot boundary keeps slot alignment, since every
 * instruction is 8 or 16 bytes. xxbyte_unit_size is 8, so after a slot that
 * does not decode xxbyte_disasm_buf emits one 8-byte "db" line and resumes
 * at the next slot, as llvm-objdump does for "<unknown>".
 *
 * Mode: XXBYTE_EBPF_MODE_BIG_ENDIAN selects the big-endian encoding
 * (register nibbles swapped, multi-byte fields big-endian); the default is
 * little-endian. XXBYTE_EBPF_MODE_CPU(n) restricts decoding to what clang
 * -mcpu=vN can emit, including with the alu32 target feature that clang
 * accepts below v3 ("-Xclang -target-feature -Xclang +alu32"); 0 (the
 * default) accepts everything. The version of an instruction is the first
 * whose LLVM code generator emits it (BPFSubtarget.cpp and the Predicates
 * of BPFInstrInfo.td; the r11 forms from LLVM main), which matches clang's
 * __BPF_FEATURE_* macros:
 *
 *   v1  ALU and ALU64 arithmetic, le/be, addr_space_cast, JMP-class ja,
 *       jeq, jgt, jge, jset, jne, jsgt, jsge, call (all forms), callx,
 *       exit, may_goto, lddw (all forms), packet loads, ldx, stx, the
 *       atomics of both sizes (lock add/or/and/xor, atomic_fetch_*, xchg,
 *       cmpxchg) and the r11 stack-argument loads and stores of registers
 *   v2  JMP-class jlt, jle, jslt, jsle (__BPF_FEATURE_JMP_EXT)
 *   v3  the JMP32 class except ja32 (__BPF_FEATURE_JMP32)
 *   v4  sdiv, smod, movsx, sign-extending loads, bswap, ja32 (gotol), the
 *       store of an immediate (st, __BPF_FEATURE_ST; also its r11 form),
 *       load-acquire, store-release and gotox
 *
 * Without alu32 clang -mcpu=v1 and v2 emit no ALU-class arithmetic and no
 * 32-bit atomic but lock add; RFC 9669 has st in its base32 group, but
 * LLVM emits it only from v4. Other mode bits return
 * XXBYTE_STATUS_UNSUPPORTED_MODE.
 */
#define XXBYTE_EBPF_MODE_LITTLE_ENDIAN 0x0000u
#define XXBYTE_EBPF_MODE_BIG_ENDIAN 0x0001u
#define XXBYTE_EBPF_MODE_CPU_MASK 0x00F0u
#define XXBYTE_EBPF_MODE_CPU(version) (((uint32_t)(version) & 0xFu) << 4)
#define XXBYTE_EBPF_MODE_DEFAULT 0u

/*
 * Solana sBPF (see "Solana sBPF" below): XXBYTE_EBPF_MODE_SBPF(v) selects
 * SBPFv0 to SBPFv4 (the version an ELF program carries in e_flags). The
 * field is 0 for the Linux modes; an sBPF mode has no other bits (sBPF is
 * little-endian only).
 */
#define XXBYTE_EBPF_MODE_SBPF_MASK 0xF000u
#define XXBYTE_EBPF_MODE_SBPF(version) ((((uint32_t)(version) + 1u) & 0xFu) << 12)
#define XXBYTE_EBPF_MODE_SBPF_V0 XXBYTE_EBPF_MODE_SBPF(0)
#define XXBYTE_EBPF_MODE_SBPF_V1 XXBYTE_EBPF_MODE_SBPF(1)
#define XXBYTE_EBPF_MODE_SBPF_V2 XXBYTE_EBPF_MODE_SBPF(2)
#define XXBYTE_EBPF_MODE_SBPF_V3 XXBYTE_EBPF_MODE_SBPF(3)
#define XXBYTE_EBPF_MODE_SBPF_V4 XXBYTE_EBPF_MODE_SBPF(4)
/* The newest sBPF version. */
#define XXBYTE_EBPF_SBPF_NEWEST 4u
/* Nonzero for an sBPF mode; its version is XXBYTE_EBPF_MODE_SBPF_VERSION. */
#define XXBYTE_EBPF_MODE_IS_SBPF(mode) (((mode) & XXBYTE_EBPF_MODE_SBPF_MASK) != 0)
#define XXBYTE_EBPF_MODE_SBPF_VERSION(mode) ((((mode) & XXBYTE_EBPF_MODE_SBPF_MASK) >> 12) - 1u)

/* The newest LLVM cpu version; a cpu field of 0 means this one. */
#define XXBYTE_EBPF_CPU_NEWEST 4u

/* Slot size; lddw is two slots. */
#define XXBYTE_EBPF_SLOT_SIZE 8u

/*
 * Text: llvm-objdump's pseudo-C (llvm-objdump 21 with --mcpu=vN, default
 * --print-imm-hex). Immediates and offsets are signed hexadecimal
 * ("0x10", "-0x1"):
 *
 *   r1 += 0x1                    w1 s/= w2
 *   r0 = *(u32 *)(r1 + 0x4)      *(u64 *)(r10 - 0x8) = r1
 *   if r1 > 0x5 goto +0x3 <0x28> call 0x6
 *   r0 = 0x123456789 ll          lock *(u64 *)(r1 + 0x8) += r2
 *   r2 = atomic_fetch_add((u64 *)(r1 + 0x0), r2)
 *
 * The text has no leading mnemonic: xxbyte_instruction.mnemonic holds the
 * classic name ("add", "ldxw", "jgt32"; see the ids below) and the text is
 * the pseudo-C statement alone.
 *
 * Narrow loads and stores print the 32-bit subregister (w0 = *(u32 *)...,
 * *(u8 *)(...) = w2, the 32-bit atomics, load_acquire and store_release
 * below 64 bits) unless the mode restricts decoding to v1 or v2, where
 * they print r registers like llvm-objdump --mcpu=v1. The ALU and JMP32
 * classes always print w registers. llvm-objdump --mcpu=v1 and v2 do not
 * decode the 32-bit atomics other than lock add ("<unknown>", and
 * atomic_fetch_add reads as lock add); there the text is that of
 * --mattr=+alu32 with r registers ("r1 = xchg32_32(r2 + 0x0, r1)").
 *
 * Where llvm-objdump shows no target or drops a field, the text adds it:
 *
 *   - Branches print the encoded offset and the absolute target:
 *     "goto -0x1 <0x8>", "gotol +0x1 <0x18>", "may_goto +0x0 <0x8>".
 *   - call by static id prints "call 0x6", the program-local call
 *     "call 0x10 <0x88>" and the call by BTF id "call kfunc 0x10", or
 *     "call kfunc 0x10, 0x3" when the offset field names a BTF object.
 *   - lddw with src_reg 1 to 6, which llvm-objdump prints as "ld_pseudo",
 *     uses the pseudocode of RFC 9669 Table 12:
 *       r1 = map_by_fd(0x5) ll
 *       r1 = map_val(map_by_fd(0x5)) + 0x8 ll
 *       r1 = var_addr(0x5) ll        (var_addr(0x5, 0x3) with a BTF object)
 *       r1 = code_addr(+0x2 <0x20>) ll
 *       r1 = map_by_idx(0x5) ll
 *       r1 = map_val(map_by_idx(0x5)) + 0x8 ll
 *     code_addr's offset counts slots from the second slot, like a call.
 *   - A packet load with a register and a nonzero immediate prints both:
 *     "r0 = *(u32 *)skb[r2 + 0x10]".
 *   - gotox, which llvm-objdump 21 does not know, prints "gotox r1".
 *
 * XXBYTE_FORMAT_UPPERCASE uppercases every letter of the text except those
 * of numbers and of resolver names ("R0 = *(U32 *)(R1 + 0x4)").
 *
 * Decoding is strict: fields an instruction does not use must be zero
 * (RFC 9669 section 3.1, the Linux verifier's "uses reserved fields"
 * checks), registers above r10 are invalid except r11 as the base of the
 * stack-argument forms "*(u64 *)(r11 - 8n) = r1|imm" and
 * "r1 = *(u64 *)(r11 + 8n)", and the second slot of lddw must have a zero
 * opcode, registers and offset. Undefined encodings return
 * XXBYTE_STATUS_INVALID_INSTRUCTION; llvm-objdump decodes some of them
 * anyway by ignoring the field.
 */

/*
 * Operands, in the order of the text, each listed once:
 *
 *   REGISTER   value = register number, size 1; extra = 32 when the text
 *              shows the subregister wN, 64 for rN. The implicit r0 of
 *              cmpxchg and the packet loads, and the packet loads' r6
 *              (printed as "skb"), have XXBYTE_OPERAND_FLAG_IMPLICIT and
 *              size 0.
 *   MEMORY     kind = base register, value = signed offset, size = access
 *              width in bytes (1, 2, 4 or 8).
 *   IMMEDIATE  signed (XXBYTE_OPERAND_FLAG_SIGNED) values of the imm field
 *              (size 4), of lddw (size 8, also XXBYTE_OPERAND_FLAG_WIDE)
 *              and of the second lddw immediate (size 4); the widths of
 *              le, be and bswap (imm, size 4) and of movsx (offset, size
 *              2); the address spaces of addr_space_cast (size 2 each).
 *   BRANCH     value = absolute target; size = 2 for the offset field, 4
 *              for the imm field (ja32, the local call, code_addr); extra
 *              = the encoded offset in slots, sign-extended.
 *   INDEX      the XXBYTE_EBPF_INDEX_* kinds below, signed, size 4 (2 for
 *              the BTF object of a call by BTF id).
 *
 * Groups: ja, ja32 and gotox are XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP;
 * the conditional jumps and may_goto XXBYTE_GROUP_CONDITIONAL; every call
 * XXBYTE_GROUP_CALL; exit XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP.
 *
 * The resolver sees the displayed INDEX operands and every BRANCH operand;
 * for a branch it replaces the absolute target inside "<...>" and keeps
 * the encoded offset ("goto +0x3 <LBB0_2>").
 */
/*
 * Solana sBPF (XXBYTE_EBPF_MODE_SBPF(v)): the instruction set of Solana
 * programs as the official anza-xyz/sbpf crate (solana-sbpf 0.28) defines
 * it: opcodes from ebpf.rs, the version gates of SBPFVersion (program.rs),
 * validity from the per-instruction checks of its verifier (verifier.rs,
 * RequisiteVerifier) and the text of its disassembler (disassembler.rs).
 *
 *   v0  the original "BPF loader" ISA: 64- and 32-bit ALU (add64, mul32,
 *       neg64, le16, be64, ...), lddw, ldx/st/stx, the JMP class, call by
 *       hash, callx with the register in imm, exit
 *   v1  SIMD-0166: also "add64 r10, imm" with imm a multiple of 64
 *   v2  v1 with SIMD-0174 and SIMD-0173: the PQR class (lmul32/64,
 *       uhmul64, shmul64, udiv32/64, urem32/64, sdiv32/64, srem32/64)
 *       replaces mul, div and mod; neg, le and lddw are gone; hor64 (imm
 *       into the upper half) appears; the loads move to the ALU32 class
 *       and the stores to the ALU64 class (ldxb 0x2C, ldxh 0x3C, ldxw 0x8C,
 *       ldxdw 0x9C, stb 0x27, ...); callx takes src_reg
 *   v3  the v0 encoding (no add64 r10) with SIMD-0178 static syscalls
 *       (call src_reg 0 = syscall by hash, src_reg 1 = pc-relative call),
 *       SIMD-0377 JMP32 (jeq32 ... jsle32) and callx taking dst_reg
 *   v4  v3's instruction set (SIMD-0177 changes memory alignment only)
 *
 * The drafts in earlier crate releases (a v2 "syscall" opcode, "return",
 * v3 with PQR) are not part of any version here.
 *
 * Validity follows RequisiteVerifier: src_reg at most r10; dst_reg at most
 * r9 except r10 as the base of a store and in v1/v2 "add64 r10, imm"; no
 * division or remainder by an immediate 0; shift immediates 0 to 31 or 63;
 * le/be widths 16, 32 or 64; callx registers r0 to r9; the second slot of
 * lddw has opcode 0 (its other fields are free). Fields an instruction
 * does not use are not checked, as in the verifier. Program-level checks
 * (jump targets inside the program) are not made.
 *
 * Text (the crate's disassembler; immediates are signed decimal, offsets
 * signed hex, lddw unsigned hex):
 *
 *   add64 r1, -5        mov32 r2, r3        neg64 r1       le16 r1
 *   lddw r1, 0x100000020                    hor64 r1, 16
 *   ldxw r1, [r2+0x4]   stdw [r10-0x8], 5   stxb [r1+0x0], r2
 *   jeq r1, 5, lbb_7    ja lbb_3            jsle32 r1, r2, lbb_9
 *   syscall sol_log_    call lbb_12         callx r3         exit
 *
 * A branch target prints as "lbb_<slot>", the target address divided by
 * 8 (the crate's block labels when address is 8 times the slot index). A
 * call by hash prints "syscall" with the official syscall name
 * (xxbyte_ebpf_sbpf_syscall_name) or the signed immediate; in v0 to v2 a
 * call whose key the decode context's function_address knows is "call"
 * with a BRANCH. In v3 and v4 a call with src_reg 1 is "call" with a
 * target of address + 8 * (imm + 1); another src_reg 2 to 10 is
 * XXBYTE_EBPF_ID_CALL_UNRESOLVED "call <imm>", which the verifier accepts
 * and execution rejects. XXBYTE_FORMAT_UPPERCASE uppercases the mnemonic,
 * registers and "lbb_", not numbers or names.
 *
 * Operands: REGISTER (size 1, extra = 32 for the 32-bit operations and
 * JMP32, else 64), IMMEDIATE (the imm field, size 4, signed; lddw's value,
 * size 8, signed and wide; the width of le/be, size 4), MEMORY (kind = base
 * register, value = offset, size = access width), BRANCH (value = absolute
 * target, size 2 for the offset field, 4 for a call; extra = the encoded
 * offset or immediate) and INDEX XXBYTE_EBPF_INDEX_SYSCALL (the hash,
 * signed, size 4). The resolver sees the BRANCH and INDEX operands; it
 * replaces the whole label or syscall name.
 *
 * Ids: an sBPF instruction takes the id of the eBPF operation it
 * performs, so for example add64 is XXBYTE_EBPF_ID_ADD, lmul64
 * XXBYTE_EBPF_ID_MUL, udiv32 XXBYTE_EBPF_ID_DIV32, srem64
 * XXBYTE_EBPF_ID_SMOD, le16 XXBYTE_EBPF_ID_ENDLE, syscall
 * XXBYTE_EBPF_ID_CALL and a local call XXBYTE_EBPF_ID_CALL_LOCAL;
 * uhmul64, shmul64 and hor64 have ids of their own. The mnemonic is the
 * sBPF one ("add64", "lmul64", "le16", "syscall"); the semantics of an id
 * can differ by version (v2 sub64 with an immediate computes imm - dst,
 * v2 32-bit results are sign-extended).
 *
 * Containers (xxbyte_disasm_file): ELF objects and shared objects, ELF32
 * or ELF64, either byte order; XXBYTE_MODE_AUTO picks the mode by
 * xxbyte_ebpf_elf_mode. The listing starts with a line describing the
 * file, then one header line "; <function> (<section> 0x<start>-0x<end>)"
 * per code body, each followed by its listing; addresses are offsets in
 * the section.
 *
 *   Linux objects (and any ET_REL file): every executable section, split
 *   at its function symbols (code before the first one is named after the
 *   section). Branch targets and local calls name the symbol at or before
 *   the target ("goto +0x7 <prog+0x50>", "call 0x2 <helper>"), as
 *   llvm-objdump does for jumps. R_BPF_64_64 relocations name the lddw
 *   immediate through XXBYTE_EBPF_INDEX_SYMBOL ("r1 = my_map ll",
 *   "r1 = .rodata+0x10 ll"), R_BPF_64_32 relocations name the call target
 *   ("call -0x1 <global_fn>"); other relocations of the code follow their
 *   instruction as "          ; R_BPF_64_ABS64 sym at 0x18" lines. BTF
 *   and BTF.ext are ignored, so CO-RE relocations do not show.
 *
 *   Solana programs (sBPF mode, ET_DYN or ET_EXEC): what the sBPF loader
 *   and the crate's static analysis show. For v0 to v2 the .text section
 *   with the loader's relocations applied (Executable::relocate: R_BPF_64_64
 *   and R_BPF_64_RELATIVE lddw addresses moved to 0x100000000, calls
 *   patched to function and syscall hashes) and the function registry of
 *   pc-relative calls ("function_<slot>"), dynamic symbols, .symtab and
 *   "entrypoint"; for v3 and v4 the bytecode segment (the executable
 *   PT_LOAD) with .symtab names. Functions without a name that the
 *   control-flow analysis finds (Analysis::control_flow_graph_tarjan) are
 *   "function_<slot>", targets that start no instruction "[invalid]". A
 *   syscall named by a relocation keeps the relocation's symbol name even
 *   when it is not an official one (the crate prints its hash).
 *
 * Limitations: Rust symbol names are not demangled (the crate demangles
 * block labels); Linux helper ids and kfunc BTF ids are not named; the
 * loader's ELF validation is not repeated, so files it rejects are still
 * listed.
 */

typedef enum xxbyte_ebpf_index_kind {
    /* Helper function by static id (call, src_reg 0). */
    XXBYTE_EBPF_INDEX_HELPER = XXBYTE_INDEX_FAMILY_FIRST,
    /* Helper function by BTF id, a Linux kfunc (call, src_reg 2). */
    XXBYTE_EBPF_INDEX_KFUNC = XXBYTE_INDEX_FAMILY_FIRST + 1,
    /* Map file descriptor (lddw src_reg 1 and 2). */
    XXBYTE_EBPF_INDEX_MAP_FD = XXBYTE_INDEX_FAMILY_FIRST + 2,
    /* Map index in the program's map set (lddw src_reg 5 and 6). */
    XXBYTE_EBPF_INDEX_MAP_IDX = XXBYTE_INDEX_FAMILY_FIRST + 3,
    /* Platform variable id (lddw src_reg 3; on Linux the BTF id of a
     * kernel variable). */
    XXBYTE_EBPF_INDEX_VARIABLE = XXBYTE_INDEX_FAMILY_FIRST + 4,
    /* Linux BTF object of a kfunc or kernel variable defined in a module:
     * the call's offset field (an fd_array index) or the second lddw
     * immediate (a BTF object fd). Present only when nonzero. */
    XXBYTE_EBPF_INDEX_BTF_OBJECT = XXBYTE_INDEX_FAMILY_FIRST + 5,
    /* sBPF syscall by murmur3-32 hash of its name (syscall). */
    XXBYTE_EBPF_INDEX_SYSCALL = XXBYTE_INDEX_FAMILY_FIRST + 6,
    /* Linux modes with a decode context only: the 64-bit immediate of an
     * lddw that a symbol relocation patches (value = the encoded addend,
     * size 8); see xxbyte_ebpf_context. */
    XXBYTE_EBPF_INDEX_SYMBOL = XXBYTE_INDEX_FAMILY_FIRST + 7
} xxbyte_ebpf_index_kind;

/*
 * Instruction identifiers (xxbyte_instruction.id): the mnemonic upper-cased
 * with every run of other characters replaced by '_'. The K (immediate)
 * and X (register) forms of an operation share an id; the operand types
 * tell them apart. Classic names: a "32" suffix marks the ALU and JMP32
 * classes and the 32-bit atomics, ldx/st/stx/ldacq/strel end in the access
 * size (w, h, b, dw), ldxs* sign-extend, a* are the atomics (af* fetch).
 * Valid ids are 1 to XXBYTE_EBPF_ID_COUNT - 1.
 */
typedef enum xxbyte_ebpf_id {
    XXBYTE_EBPF_ID_INVALID = 0,
    XXBYTE_EBPF_ID_ADD = 1,
    XXBYTE_EBPF_ID_SUB = 2,
    XXBYTE_EBPF_ID_MUL = 3,
    XXBYTE_EBPF_ID_DIV = 4,
    XXBYTE_EBPF_ID_SDIV = 5,
    XXBYTE_EBPF_ID_OR = 6,
    XXBYTE_EBPF_ID_AND = 7,
    XXBYTE_EBPF_ID_LSH = 8,
    XXBYTE_EBPF_ID_RSH = 9,
    XXBYTE_EBPF_ID_NEG = 10,
    XXBYTE_EBPF_ID_MOD = 11,
    XXBYTE_EBPF_ID_SMOD = 12,
    XXBYTE_EBPF_ID_XOR = 13,
    XXBYTE_EBPF_ID_MOV = 14,
    XXBYTE_EBPF_ID_MOVSX = 15,
    XXBYTE_EBPF_ID_ARSH = 16,
    XXBYTE_EBPF_ID_BSWAP = 17,
    XXBYTE_EBPF_ID_ADDR_SPACE_CAST = 18,
    XXBYTE_EBPF_ID_ADD32 = 19,
    XXBYTE_EBPF_ID_SUB32 = 20,
    XXBYTE_EBPF_ID_MUL32 = 21,
    XXBYTE_EBPF_ID_DIV32 = 22,
    XXBYTE_EBPF_ID_SDIV32 = 23,
    XXBYTE_EBPF_ID_OR32 = 24,
    XXBYTE_EBPF_ID_AND32 = 25,
    XXBYTE_EBPF_ID_LSH32 = 26,
    XXBYTE_EBPF_ID_RSH32 = 27,
    XXBYTE_EBPF_ID_NEG32 = 28,
    XXBYTE_EBPF_ID_MOD32 = 29,
    XXBYTE_EBPF_ID_SMOD32 = 30,
    XXBYTE_EBPF_ID_XOR32 = 31,
    XXBYTE_EBPF_ID_MOV32 = 32,
    XXBYTE_EBPF_ID_MOVSX32 = 33,
    XXBYTE_EBPF_ID_ARSH32 = 34,
    XXBYTE_EBPF_ID_ENDLE = 35,
    XXBYTE_EBPF_ID_ENDBE = 36,
    XXBYTE_EBPF_ID_JA = 37,
    XXBYTE_EBPF_ID_JEQ = 38,
    XXBYTE_EBPF_ID_JGT = 39,
    XXBYTE_EBPF_ID_JGE = 40,
    XXBYTE_EBPF_ID_JSET = 41,
    XXBYTE_EBPF_ID_JNE = 42,
    XXBYTE_EBPF_ID_JSGT = 43,
    XXBYTE_EBPF_ID_JSGE = 44,
    XXBYTE_EBPF_ID_CALL = 45,
    XXBYTE_EBPF_ID_CALL_LOCAL = 46,
    XXBYTE_EBPF_ID_CALL_KFUNC = 47,
    XXBYTE_EBPF_ID_CALLX = 48,
    XXBYTE_EBPF_ID_EXIT = 49,
    XXBYTE_EBPF_ID_JLT = 50,
    XXBYTE_EBPF_ID_JLE = 51,
    XXBYTE_EBPF_ID_JSLT = 52,
    XXBYTE_EBPF_ID_JSLE = 53,
    XXBYTE_EBPF_ID_MAY_GOTO = 54,
    XXBYTE_EBPF_ID_GOTOX = 55,
    XXBYTE_EBPF_ID_JA32 = 56,
    XXBYTE_EBPF_ID_JEQ32 = 57,
    XXBYTE_EBPF_ID_JGT32 = 58,
    XXBYTE_EBPF_ID_JGE32 = 59,
    XXBYTE_EBPF_ID_JSET32 = 60,
    XXBYTE_EBPF_ID_JNE32 = 61,
    XXBYTE_EBPF_ID_JSGT32 = 62,
    XXBYTE_EBPF_ID_JSGE32 = 63,
    XXBYTE_EBPF_ID_JLT32 = 64,
    XXBYTE_EBPF_ID_JLE32 = 65,
    XXBYTE_EBPF_ID_JSLT32 = 66,
    XXBYTE_EBPF_ID_JSLE32 = 67,
    XXBYTE_EBPF_ID_LDDW = 68,
    XXBYTE_EBPF_ID_LDDW_MAP_FD = 69,
    XXBYTE_EBPF_ID_LDDW_MAP_VALUE = 70,
    XXBYTE_EBPF_ID_LDDW_VAR_ADDR = 71,
    XXBYTE_EBPF_ID_LDDW_CODE_ADDR = 72,
    XXBYTE_EBPF_ID_LDDW_MAP_IDX = 73,
    XXBYTE_EBPF_ID_LDDW_MAP_IDX_VALUE = 74,
    XXBYTE_EBPF_ID_LDABSW = 75,
    XXBYTE_EBPF_ID_LDABSH = 76,
    XXBYTE_EBPF_ID_LDABSB = 77,
    XXBYTE_EBPF_ID_LDINDW = 78,
    XXBYTE_EBPF_ID_LDINDH = 79,
    XXBYTE_EBPF_ID_LDINDB = 80,
    XXBYTE_EBPF_ID_LDXW = 81,
    XXBYTE_EBPF_ID_LDXH = 82,
    XXBYTE_EBPF_ID_LDXB = 83,
    XXBYTE_EBPF_ID_LDXDW = 84,
    XXBYTE_EBPF_ID_LDXSW = 85,
    XXBYTE_EBPF_ID_LDXSH = 86,
    XXBYTE_EBPF_ID_LDXSB = 87,
    XXBYTE_EBPF_ID_STW = 88,
    XXBYTE_EBPF_ID_STH = 89,
    XXBYTE_EBPF_ID_STB = 90,
    XXBYTE_EBPF_ID_STDW = 91,
    XXBYTE_EBPF_ID_STXW = 92,
    XXBYTE_EBPF_ID_STXH = 93,
    XXBYTE_EBPF_ID_STXB = 94,
    XXBYTE_EBPF_ID_STXDW = 95,
    XXBYTE_EBPF_ID_AADD = 96,
    XXBYTE_EBPF_ID_AOR = 97,
    XXBYTE_EBPF_ID_AAND = 98,
    XXBYTE_EBPF_ID_AXOR = 99,
    XXBYTE_EBPF_ID_AFADD = 100,
    XXBYTE_EBPF_ID_AFOR = 101,
    XXBYTE_EBPF_ID_AFAND = 102,
    XXBYTE_EBPF_ID_AFXOR = 103,
    XXBYTE_EBPF_ID_AXCHG = 104,
    XXBYTE_EBPF_ID_ACMP = 105,
    XXBYTE_EBPF_ID_AADD32 = 106,
    XXBYTE_EBPF_ID_AOR32 = 107,
    XXBYTE_EBPF_ID_AAND32 = 108,
    XXBYTE_EBPF_ID_AXOR32 = 109,
    XXBYTE_EBPF_ID_AFADD32 = 110,
    XXBYTE_EBPF_ID_AFOR32 = 111,
    XXBYTE_EBPF_ID_AFAND32 = 112,
    XXBYTE_EBPF_ID_AFXOR32 = 113,
    XXBYTE_EBPF_ID_AXCHG32 = 114,
    XXBYTE_EBPF_ID_ACMP32 = 115,
    XXBYTE_EBPF_ID_LDACQW = 116,
    XXBYTE_EBPF_ID_LDACQH = 117,
    XXBYTE_EBPF_ID_LDACQB = 118,
    XXBYTE_EBPF_ID_LDACQDW = 119,
    XXBYTE_EBPF_ID_STRELW = 120,
    XXBYTE_EBPF_ID_STRELH = 121,
    XXBYTE_EBPF_ID_STRELB = 122,
    XXBYTE_EBPF_ID_STRELDW = 123,
    /* sBPF only (xxbyte_ebpf_cpu_version is 0). */
    XXBYTE_EBPF_ID_UHMUL64 = 124,
    XXBYTE_EBPF_ID_SHMUL64 = 125,
    XXBYTE_EBPF_ID_HOR64 = 126,
    XXBYTE_EBPF_ID_CALL_UNRESOLVED = 127,
    XXBYTE_EBPF_ID_COUNT = 128
} xxbyte_ebpf_id;

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_ebpf_mnemonic(uint32_t id);

/* The first LLVM cpu version (1 to 4) with id, or 0 when id is not
 * valid or exists in sBPF only. */
XXBYTE_API uint32_t xxbyte_ebpf_cpu_version(uint32_t id);

/* The raw fields of one 8-byte slot. */
typedef struct xxbyte_ebpf_slot {
    uint8_t opcode;
    uint8_t dst_reg; /* 0 to 15 */
    uint8_t src_reg; /* 0 to 15 */
    int16_t offset;
    int32_t imm;
} xxbyte_ebpf_slot;

/*
 * Splits the slot at code[0] into its fields with the byte order of mode,
 * without validating it (for example to apply relocations, which patch
 * imm). Returns XXBYTE_STATUS_TRUNCATED when code_size is below 8,
 * XXBYTE_STATUS_INVALID_ARGUMENT for NULL pointers and
 * XXBYTE_STATUS_UNSUPPORTED_MODE for a bad mode; *slot is zeroed on
 * failure.
 */
XXBYTE_API xxbyte_status xxbyte_ebpf_read_slot(uint32_t mode, const uint8_t *code, size_t code_size, xxbyte_ebpf_slot *slot);

/*
 * Decode context for xxbyte_decode_ex and xxbyte_disasm_buf_ex (optional;
 * NULL, or NULL callbacks, decode as without one). The context only adds
 * information the instruction bytes do not carry:
 *
 *   function_address  sBPF v0 to v2: a call immediate is a key that the
 *                     loader's function registry maps to an internal
 *                     function (the murmur3-32 hash of the target's slot
 *                     index as 8 little-endian bytes, or of "entrypoint").
 *                     Return nonzero and set *address (the function's byte
 *                     address) when key is one; the call then decodes as
 *                     XXBYTE_EBPF_ID_CALL_LOCAL "call" with a BRANCH
 *                     operand instead of a "syscall".
 *   lddw_relocated    Linux modes: return nonzero when a symbol relocation
 *                     patches the lddw (src_reg 0) at address; its 64-bit
 *                     immediate then is an XXBYTE_EBPF_INDEX_SYMBOL operand
 *                     instead of an IMMEDIATE, so a resolver can name it.
 *                     The text without a resolver is unchanged.
 */
typedef struct xxbyte_ebpf_context {
    void *user;
    int (*function_address)(void *user, uint32_t key, uint64_t *address);
    int (*lddw_relocated)(void *user, uint64_t address);
} xxbyte_ebpf_context;

/* MurmurHash3 x86_32 with seed 0 of data[0..size), the hash the sBPF loader
 * gives syscall and function names. */
XXBYTE_API uint32_t xxbyte_ebpf_sbpf_hash(const void *data, size_t size);

/* The official name of the syscall with this hash (the syscalls Agave's
 * loader registers, for example 0x207559BD "sol_log_"), or NULL. */
XXBYTE_API const char *xxbyte_ebpf_sbpf_syscall_name(uint32_t hash);

/*
 * The mode xxbyte_disasm_file uses for XXBYTE_MODE_AUTO on an ELF file:
 *
 *   - e_machine must be EM_BPF (247) or EM_SBPF (263);
 *   - an ELF64 little-endian file is Solana sBPF when e_machine is EM_SBPF,
 *     or EM_BPF with e_type ET_DYN or a nonzero e_flags (the Solana
 *     toolchain writes SBPFv3 programs as EM_BPF with e_flags 3, the Linux
 *     toolchain always leaves e_flags 0 and never links shared objects);
 *     the sBPF version is e_flags as for the loader (0 to 4; another value
 *     returns XXBYTE_STATUS_UNSUPPORTED_MODE);
 *   - everything else is Linux eBPF, XXBYTE_EBPF_MODE_BIG_ENDIAN for
 *     ELFDATA2MSB, with every cpu version (cpu 0).
 *
 * Returns XXBYTE_STATUS_INVALID_INSTRUCTION when data is not such an ELF
 * file and XXBYTE_STATUS_TRUNCATED when it ends inside the ELF header.
 */
XXBYTE_API xxbyte_status xxbyte_ebpf_elf_mode(const uint8_t *data, size_t size, uint32_t *mode);

#ifdef __cplusplus
}
#endif

#endif
