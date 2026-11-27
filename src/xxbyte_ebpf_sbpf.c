/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_ebpf_internal.h"

#if XXBYTE_USE_EBPF

/*
 * Solana sBPF, as the official anza-xyz/sbpf crate (solana-sbpf 0.28)
 * defines it: the opcodes of ebpf.rs, the per-version gates of
 * program.rs (SBPFVersion), the per-instruction checks of
 * verifier.rs (RequisiteVerifier) and the text of disassembler.rs.
 *
 *   v0  the original "BPF loader" ISA
 *   v1  SIMD-0166 dynamic stack frames: add64 r10, imm (imm % 64 == 0)
 *   v2  v1 plus SIMD-0174 (PQR class replaces mul/div/mod, neg removed)
 *       and SIMD-0173 (lddw and le removed, hor64, the memory instructions
 *       move into the ALU classes, callx takes src_reg)
 *   v3  the v0 encoding plus SIMD-0178 static syscalls (call src_reg 0 is
 *       a syscall by hash, src_reg 1 a pc-relative call), SIMD-0377 JMP32
 *       and callx taking dst_reg; no dynamic stack frames
 *   v4  v3's instruction set (SIMD-0177 changes memory alignment only)
 */

enum sbpf_format {
    SBPF_FMT_NONE = 0,
    SBPF_FMT_ALU_K, /* add64 r1, 5 */
    SBPF_FMT_ALU_X, /* add64 r1, r2 */
    SBPF_FMT_NEG,   /* neg64 r1 */
    SBPF_FMT_END,   /* le16 r1 */
    SBPF_FMT_LDDW,  /* lddw r1, 0x1122334455667788 */
    SBPF_FMT_LDX,   /* ldxw r1, [r2+0x4] */
    SBPF_FMT_ST,    /* stw [r1+0x4], 5 */
    SBPF_FMT_STX,   /* stxw [r1+0x4], r2 */
    SBPF_FMT_JA,    /* ja lbb_5 */
    SBPF_FMT_JCC_K, /* jeq r1, 5, lbb_5 */
    SBPF_FMT_JCC_X, /* jeq r1, r2, lbb_5 */
    SBPF_FMT_CALL,  /* syscall sol_log_, call lbb_5, call 5 */
    SBPF_FMT_CALLX, /* callx r1 */
    SBPF_FMT_EXIT   /* exit */
};

enum sbpf_check {
    SBPF_CHECK_NONE = 0,
    SBPF_CHECK_NONZERO, /* division by an immediate zero */
    SBPF_CHECK_SHIFT32, /* 0 <= imm < 32 */
    SBPF_CHECK_SHIFT64, /* 0 <= imm < 64 */
    SBPF_CHECK_ENDIAN   /* imm is 16, 32 or 64 */
};

#define SBPF_F_W32 0x01u   /* 32-bit operation (register extra 32) */
#define SBPF_F_STORE 0x02u /* dst_reg is a base register: r10 is valid */
#define SBPF_F_JMP32 0x04u /* the JMP32 class: v3 and later */

typedef struct sbpf_op {
    const char *mnemonic;
    uint8_t id;
    uint8_t format; /* enum sbpf_format; 0 = not an instruction */
    uint8_t check;  /* enum sbpf_check */
    uint8_t flags;  /* SBPF_F_* */
    uint8_t width;  /* memory access width in bytes */
} sbpf_op;

/* v0, v1, v3 and v4 (generated from ebpf.rs and disassembler.rs). */
static const sbpf_op sbpf_legacy_ops[256] = {
    [0x04] = {"add32", XXBYTE_EBPF_ID_ADD32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x05] = {"ja", XXBYTE_EBPF_ID_JA, SBPF_FMT_JA, SBPF_CHECK_NONE, 0, 0},
    [0x07] = {"add64", XXBYTE_EBPF_ID_ADD, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x0C] = {"add32", XXBYTE_EBPF_ID_ADD32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x0F] = {"add64", XXBYTE_EBPF_ID_ADD, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x14] = {"sub32", XXBYTE_EBPF_ID_SUB32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x15] = {"jeq", XXBYTE_EBPF_ID_JEQ, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x16] = {"jeq32", XXBYTE_EBPF_ID_JEQ32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x17] = {"sub64", XXBYTE_EBPF_ID_SUB, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x18] = {"lddw", XXBYTE_EBPF_ID_LDDW, SBPF_FMT_LDDW, SBPF_CHECK_NONE, 0, 0},
    [0x1C] = {"sub32", XXBYTE_EBPF_ID_SUB32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x1D] = {"jeq", XXBYTE_EBPF_ID_JEQ, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x1E] = {"jeq32", XXBYTE_EBPF_ID_JEQ32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x1F] = {"sub64", XXBYTE_EBPF_ID_SUB, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x24] = {"mul32", XXBYTE_EBPF_ID_MUL32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x25] = {"jgt", XXBYTE_EBPF_ID_JGT, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x26] = {"jgt32", XXBYTE_EBPF_ID_JGT32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x27] = {"mul64", XXBYTE_EBPF_ID_MUL, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x2C] = {"mul32", XXBYTE_EBPF_ID_MUL32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x2D] = {"jgt", XXBYTE_EBPF_ID_JGT, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x2E] = {"jgt32", XXBYTE_EBPF_ID_JGT32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x2F] = {"mul64", XXBYTE_EBPF_ID_MUL, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x34] = {"div32", XXBYTE_EBPF_ID_DIV32, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, SBPF_F_W32, 0},
    [0x35] = {"jge", XXBYTE_EBPF_ID_JGE, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x36] = {"jge32", XXBYTE_EBPF_ID_JGE32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x37] = {"div64", XXBYTE_EBPF_ID_DIV, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, 0, 0},
    [0x3C] = {"div32", XXBYTE_EBPF_ID_DIV32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x3D] = {"jge", XXBYTE_EBPF_ID_JGE, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x3E] = {"jge32", XXBYTE_EBPF_ID_JGE32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x3F] = {"div64", XXBYTE_EBPF_ID_DIV, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x44] = {"or32", XXBYTE_EBPF_ID_OR32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x45] = {"jset", XXBYTE_EBPF_ID_JSET, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x46] = {"jset32", XXBYTE_EBPF_ID_JSET32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x47] = {"or64", XXBYTE_EBPF_ID_OR, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x4C] = {"or32", XXBYTE_EBPF_ID_OR32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x4D] = {"jset", XXBYTE_EBPF_ID_JSET, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x4E] = {"jset32", XXBYTE_EBPF_ID_JSET32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x4F] = {"or64", XXBYTE_EBPF_ID_OR, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x54] = {"and32", XXBYTE_EBPF_ID_AND32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x55] = {"jne", XXBYTE_EBPF_ID_JNE, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x56] = {"jne32", XXBYTE_EBPF_ID_JNE32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x57] = {"and64", XXBYTE_EBPF_ID_AND, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x5C] = {"and32", XXBYTE_EBPF_ID_AND32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x5D] = {"jne", XXBYTE_EBPF_ID_JNE, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x5E] = {"jne32", XXBYTE_EBPF_ID_JNE32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x5F] = {"and64", XXBYTE_EBPF_ID_AND, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x61] = {"ldxw", XXBYTE_EBPF_ID_LDXW, SBPF_FMT_LDX, SBPF_CHECK_NONE, 0, 4},
    [0x62] = {"stw", XXBYTE_EBPF_ID_STW, SBPF_FMT_ST, SBPF_CHECK_NONE, SBPF_F_STORE, 4},
    [0x63] = {"stxw", XXBYTE_EBPF_ID_STXW, SBPF_FMT_STX, SBPF_CHECK_NONE, SBPF_F_STORE, 4},
    [0x64] = {"lsh32", XXBYTE_EBPF_ID_LSH32, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT32, SBPF_F_W32, 0},
    [0x65] = {"jsgt", XXBYTE_EBPF_ID_JSGT, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x66] = {"jsgt32", XXBYTE_EBPF_ID_JSGT32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x67] = {"lsh64", XXBYTE_EBPF_ID_LSH, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT64, 0, 0},
    [0x69] = {"ldxh", XXBYTE_EBPF_ID_LDXH, SBPF_FMT_LDX, SBPF_CHECK_NONE, 0, 2},
    [0x6A] = {"sth", XXBYTE_EBPF_ID_STH, SBPF_FMT_ST, SBPF_CHECK_NONE, SBPF_F_STORE, 2},
    [0x6B] = {"stxh", XXBYTE_EBPF_ID_STXH, SBPF_FMT_STX, SBPF_CHECK_NONE, SBPF_F_STORE, 2},
    [0x6C] = {"lsh32", XXBYTE_EBPF_ID_LSH32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x6D] = {"jsgt", XXBYTE_EBPF_ID_JSGT, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x6E] = {"jsgt32", XXBYTE_EBPF_ID_JSGT32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x6F] = {"lsh64", XXBYTE_EBPF_ID_LSH, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x71] = {"ldxb", XXBYTE_EBPF_ID_LDXB, SBPF_FMT_LDX, SBPF_CHECK_NONE, 0, 1},
    [0x72] = {"stb", XXBYTE_EBPF_ID_STB, SBPF_FMT_ST, SBPF_CHECK_NONE, SBPF_F_STORE, 1},
    [0x73] = {"stxb", XXBYTE_EBPF_ID_STXB, SBPF_FMT_STX, SBPF_CHECK_NONE, SBPF_F_STORE, 1},
    [0x74] = {"rsh32", XXBYTE_EBPF_ID_RSH32, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT32, SBPF_F_W32, 0},
    [0x75] = {"jsge", XXBYTE_EBPF_ID_JSGE, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x76] = {"jsge32", XXBYTE_EBPF_ID_JSGE32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x77] = {"rsh64", XXBYTE_EBPF_ID_RSH, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT64, 0, 0},
    [0x79] = {"ldxdw", XXBYTE_EBPF_ID_LDXDW, SBPF_FMT_LDX, SBPF_CHECK_NONE, 0, 8},
    [0x7A] = {"stdw", XXBYTE_EBPF_ID_STDW, SBPF_FMT_ST, SBPF_CHECK_NONE, SBPF_F_STORE, 8},
    [0x7B] = {"stxdw", XXBYTE_EBPF_ID_STXDW, SBPF_FMT_STX, SBPF_CHECK_NONE, SBPF_F_STORE, 8},
    [0x7C] = {"rsh32", XXBYTE_EBPF_ID_RSH32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x7D] = {"jsge", XXBYTE_EBPF_ID_JSGE, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x7E] = {"jsge32", XXBYTE_EBPF_ID_JSGE32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0x7F] = {"rsh64", XXBYTE_EBPF_ID_RSH, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x84] = {"neg32", XXBYTE_EBPF_ID_NEG32, SBPF_FMT_NEG, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x85] = {"syscall", XXBYTE_EBPF_ID_CALL, SBPF_FMT_CALL, SBPF_CHECK_NONE, 0, 0},
    [0x87] = {"neg64", XXBYTE_EBPF_ID_NEG, SBPF_FMT_NEG, SBPF_CHECK_NONE, 0, 0},
    [0x8D] = {"callx", XXBYTE_EBPF_ID_CALLX, SBPF_FMT_CALLX, SBPF_CHECK_NONE, 0, 0},
    [0x94] = {"mod32", XXBYTE_EBPF_ID_MOD32, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, SBPF_F_W32, 0},
    [0x95] = {"exit", XXBYTE_EBPF_ID_EXIT, SBPF_FMT_EXIT, SBPF_CHECK_NONE, 0, 0},
    [0x97] = {"mod64", XXBYTE_EBPF_ID_MOD, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, 0, 0},
    [0x9C] = {"mod32", XXBYTE_EBPF_ID_MOD32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x9F] = {"mod64", XXBYTE_EBPF_ID_MOD, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0xA4] = {"xor32", XXBYTE_EBPF_ID_XOR32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xA5] = {"jlt", XXBYTE_EBPF_ID_JLT, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0xA6] = {"jlt32", XXBYTE_EBPF_ID_JLT32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0xA7] = {"xor64", XXBYTE_EBPF_ID_XOR, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0xAC] = {"xor32", XXBYTE_EBPF_ID_XOR32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xAD] = {"jlt", XXBYTE_EBPF_ID_JLT, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0xAE] = {"jlt32", XXBYTE_EBPF_ID_JLT32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0xAF] = {"xor64", XXBYTE_EBPF_ID_XOR, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0xB4] = {"mov32", XXBYTE_EBPF_ID_MOV32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xB5] = {"jle", XXBYTE_EBPF_ID_JLE, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0xB6] = {"jle32", XXBYTE_EBPF_ID_JLE32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0xB7] = {"mov64", XXBYTE_EBPF_ID_MOV, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0xBC] = {"mov32", XXBYTE_EBPF_ID_MOV32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xBD] = {"jle", XXBYTE_EBPF_ID_JLE, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0xBE] = {"jle32", XXBYTE_EBPF_ID_JLE32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0xBF] = {"mov64", XXBYTE_EBPF_ID_MOV, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0xC4] = {"arsh32", XXBYTE_EBPF_ID_ARSH32, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT32, SBPF_F_W32, 0},
    [0xC5] = {"jslt", XXBYTE_EBPF_ID_JSLT, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0xC6] = {"jslt32", XXBYTE_EBPF_ID_JSLT32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0xC7] = {"arsh64", XXBYTE_EBPF_ID_ARSH, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT64, 0, 0},
    [0xCC] = {"arsh32", XXBYTE_EBPF_ID_ARSH32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xCD] = {"jslt", XXBYTE_EBPF_ID_JSLT, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0xCE] = {"jslt32", XXBYTE_EBPF_ID_JSLT32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0xCF] = {"arsh64", XXBYTE_EBPF_ID_ARSH, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0xD4] = {"le", XXBYTE_EBPF_ID_ENDLE, SBPF_FMT_END, SBPF_CHECK_ENDIAN, 0, 0},
    [0xD5] = {"jsle", XXBYTE_EBPF_ID_JSLE, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0xD6] = {"jsle32", XXBYTE_EBPF_ID_JSLE32, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
    [0xDC] = {"be", XXBYTE_EBPF_ID_ENDBE, SBPF_FMT_END, SBPF_CHECK_ENDIAN, 0, 0},
    [0xDD] = {"jsle", XXBYTE_EBPF_ID_JSLE, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0xDE] = {"jsle32", XXBYTE_EBPF_ID_JSLE32, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, SBPF_F_W32 | SBPF_F_JMP32, 0},
};

/* v2. */
static const sbpf_op sbpf_v2_ops[256] = {
    [0x04] = {"add32", XXBYTE_EBPF_ID_ADD32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x05] = {"ja", XXBYTE_EBPF_ID_JA, SBPF_FMT_JA, SBPF_CHECK_NONE, 0, 0},
    [0x07] = {"add64", XXBYTE_EBPF_ID_ADD, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x0C] = {"add32", XXBYTE_EBPF_ID_ADD32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x0F] = {"add64", XXBYTE_EBPF_ID_ADD, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x14] = {"sub32", XXBYTE_EBPF_ID_SUB32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x15] = {"jeq", XXBYTE_EBPF_ID_JEQ, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x17] = {"sub64", XXBYTE_EBPF_ID_SUB, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x1C] = {"sub32", XXBYTE_EBPF_ID_SUB32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x1D] = {"jeq", XXBYTE_EBPF_ID_JEQ, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x1F] = {"sub64", XXBYTE_EBPF_ID_SUB, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x25] = {"jgt", XXBYTE_EBPF_ID_JGT, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x27] = {"stb", XXBYTE_EBPF_ID_STB, SBPF_FMT_ST, SBPF_CHECK_NONE, SBPF_F_STORE, 1},
    [0x2C] = {"ldxb", XXBYTE_EBPF_ID_LDXB, SBPF_FMT_LDX, SBPF_CHECK_NONE, 0, 1},
    [0x2D] = {"jgt", XXBYTE_EBPF_ID_JGT, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x2F] = {"stxb", XXBYTE_EBPF_ID_STXB, SBPF_FMT_STX, SBPF_CHECK_NONE, SBPF_F_STORE, 1},
    [0x35] = {"jge", XXBYTE_EBPF_ID_JGE, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x36] = {"uhmul64", XXBYTE_EBPF_ID_UHMUL64, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x37] = {"sth", XXBYTE_EBPF_ID_STH, SBPF_FMT_ST, SBPF_CHECK_NONE, SBPF_F_STORE, 2},
    [0x3C] = {"ldxh", XXBYTE_EBPF_ID_LDXH, SBPF_FMT_LDX, SBPF_CHECK_NONE, 0, 2},
    [0x3D] = {"jge", XXBYTE_EBPF_ID_JGE, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x3E] = {"uhmul64", XXBYTE_EBPF_ID_UHMUL64, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x3F] = {"stxh", XXBYTE_EBPF_ID_STXH, SBPF_FMT_STX, SBPF_CHECK_NONE, SBPF_F_STORE, 2},
    [0x44] = {"or32", XXBYTE_EBPF_ID_OR32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x45] = {"jset", XXBYTE_EBPF_ID_JSET, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x46] = {"udiv32", XXBYTE_EBPF_ID_DIV32, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, SBPF_F_W32, 0},
    [0x47] = {"or64", XXBYTE_EBPF_ID_OR, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x4C] = {"or32", XXBYTE_EBPF_ID_OR32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x4D] = {"jset", XXBYTE_EBPF_ID_JSET, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x4E] = {"udiv32", XXBYTE_EBPF_ID_DIV32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x4F] = {"or64", XXBYTE_EBPF_ID_OR, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x54] = {"and32", XXBYTE_EBPF_ID_AND32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x55] = {"jne", XXBYTE_EBPF_ID_JNE, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x56] = {"udiv64", XXBYTE_EBPF_ID_DIV, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, 0, 0},
    [0x57] = {"and64", XXBYTE_EBPF_ID_AND, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x5C] = {"and32", XXBYTE_EBPF_ID_AND32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x5D] = {"jne", XXBYTE_EBPF_ID_JNE, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x5E] = {"udiv64", XXBYTE_EBPF_ID_DIV, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x5F] = {"and64", XXBYTE_EBPF_ID_AND, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x64] = {"lsh32", XXBYTE_EBPF_ID_LSH32, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT32, SBPF_F_W32, 0},
    [0x65] = {"jsgt", XXBYTE_EBPF_ID_JSGT, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x66] = {"urem32", XXBYTE_EBPF_ID_MOD32, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, SBPF_F_W32, 0},
    [0x67] = {"lsh64", XXBYTE_EBPF_ID_LSH, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT64, 0, 0},
    [0x6C] = {"lsh32", XXBYTE_EBPF_ID_LSH32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x6D] = {"jsgt", XXBYTE_EBPF_ID_JSGT, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x6E] = {"urem32", XXBYTE_EBPF_ID_MOD32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x6F] = {"lsh64", XXBYTE_EBPF_ID_LSH, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x74] = {"rsh32", XXBYTE_EBPF_ID_RSH32, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT32, SBPF_F_W32, 0},
    [0x75] = {"jsge", XXBYTE_EBPF_ID_JSGE, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0x76] = {"urem64", XXBYTE_EBPF_ID_MOD, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, 0, 0},
    [0x77] = {"rsh64", XXBYTE_EBPF_ID_RSH, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT64, 0, 0},
    [0x7C] = {"rsh32", XXBYTE_EBPF_ID_RSH32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x7D] = {"jsge", XXBYTE_EBPF_ID_JSGE, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0x7E] = {"urem64", XXBYTE_EBPF_ID_MOD, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x7F] = {"rsh64", XXBYTE_EBPF_ID_RSH, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x85] = {"syscall", XXBYTE_EBPF_ID_CALL, SBPF_FMT_CALL, SBPF_CHECK_NONE, 0, 0},
    [0x86] = {"lmul32", XXBYTE_EBPF_ID_MUL32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x87] = {"stw", XXBYTE_EBPF_ID_STW, SBPF_FMT_ST, SBPF_CHECK_NONE, SBPF_F_STORE, 4},
    [0x8C] = {"ldxw", XXBYTE_EBPF_ID_LDXW, SBPF_FMT_LDX, SBPF_CHECK_NONE, 0, 4},
    [0x8D] = {"callx", XXBYTE_EBPF_ID_CALLX, SBPF_FMT_CALLX, SBPF_CHECK_NONE, 0, 0},
    [0x8E] = {"lmul32", XXBYTE_EBPF_ID_MUL32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0x8F] = {"stxw", XXBYTE_EBPF_ID_STXW, SBPF_FMT_STX, SBPF_CHECK_NONE, SBPF_F_STORE, 4},
    [0x95] = {"exit", XXBYTE_EBPF_ID_EXIT, SBPF_FMT_EXIT, SBPF_CHECK_NONE, 0, 0},
    [0x96] = {"lmul64", XXBYTE_EBPF_ID_MUL, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0x97] = {"stdw", XXBYTE_EBPF_ID_STDW, SBPF_FMT_ST, SBPF_CHECK_NONE, SBPF_F_STORE, 8},
    [0x9C] = {"ldxdw", XXBYTE_EBPF_ID_LDXDW, SBPF_FMT_LDX, SBPF_CHECK_NONE, 0, 8},
    [0x9E] = {"lmul64", XXBYTE_EBPF_ID_MUL, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0x9F] = {"stxdw", XXBYTE_EBPF_ID_STXDW, SBPF_FMT_STX, SBPF_CHECK_NONE, SBPF_F_STORE, 8},
    [0xA4] = {"xor32", XXBYTE_EBPF_ID_XOR32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xA5] = {"jlt", XXBYTE_EBPF_ID_JLT, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0xA7] = {"xor64", XXBYTE_EBPF_ID_XOR, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0xAC] = {"xor32", XXBYTE_EBPF_ID_XOR32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xAD] = {"jlt", XXBYTE_EBPF_ID_JLT, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0xAF] = {"xor64", XXBYTE_EBPF_ID_XOR, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0xB4] = {"mov32", XXBYTE_EBPF_ID_MOV32, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xB5] = {"jle", XXBYTE_EBPF_ID_JLE, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0xB6] = {"shmul64", XXBYTE_EBPF_ID_SHMUL64, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0xB7] = {"mov64", XXBYTE_EBPF_ID_MOV, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0xBC] = {"mov32", XXBYTE_EBPF_ID_MOV32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xBD] = {"jle", XXBYTE_EBPF_ID_JLE, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0xBE] = {"shmul64", XXBYTE_EBPF_ID_SHMUL64, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0xBF] = {"mov64", XXBYTE_EBPF_ID_MOV, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0xC4] = {"arsh32", XXBYTE_EBPF_ID_ARSH32, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT32, SBPF_F_W32, 0},
    [0xC5] = {"jslt", XXBYTE_EBPF_ID_JSLT, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0xC6] = {"sdiv32", XXBYTE_EBPF_ID_SDIV32, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, SBPF_F_W32, 0},
    [0xC7] = {"arsh64", XXBYTE_EBPF_ID_ARSH, SBPF_FMT_ALU_K, SBPF_CHECK_SHIFT64, 0, 0},
    [0xCC] = {"arsh32", XXBYTE_EBPF_ID_ARSH32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xCD] = {"jslt", XXBYTE_EBPF_ID_JSLT, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0xCE] = {"sdiv32", XXBYTE_EBPF_ID_SDIV32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xCF] = {"arsh64", XXBYTE_EBPF_ID_ARSH, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0xD5] = {"jsle", XXBYTE_EBPF_ID_JSLE, SBPF_FMT_JCC_K, SBPF_CHECK_NONE, 0, 0},
    [0xD6] = {"sdiv64", XXBYTE_EBPF_ID_SDIV, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, 0, 0},
    [0xDC] = {"be", XXBYTE_EBPF_ID_ENDBE, SBPF_FMT_END, SBPF_CHECK_ENDIAN, 0, 0},
    [0xDD] = {"jsle", XXBYTE_EBPF_ID_JSLE, SBPF_FMT_JCC_X, SBPF_CHECK_NONE, 0, 0},
    [0xDE] = {"sdiv64", XXBYTE_EBPF_ID_SDIV, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
    [0xE6] = {"srem32", XXBYTE_EBPF_ID_SMOD32, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, SBPF_F_W32, 0},
    [0xEE] = {"srem32", XXBYTE_EBPF_ID_SMOD32, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, SBPF_F_W32, 0},
    [0xF6] = {"srem64", XXBYTE_EBPF_ID_SMOD, SBPF_FMT_ALU_K, SBPF_CHECK_NONZERO, 0, 0},
    [0xF7] = {"hor64", XXBYTE_EBPF_ID_HOR64, SBPF_FMT_ALU_K, SBPF_CHECK_NONE, 0, 0},
    [0xFE] = {"srem64", XXBYTE_EBPF_ID_SMOD, SBPF_FMT_ALU_X, SBPF_CHECK_NONE, 0, 0},
};

/* The official syscall names by hash (Agave syscalls/src/lib.rs,
 * create_program_runtime_environment), sorted by hash. */
static const struct {
    uint32_t hash;
    const char *name;
} sbpf_syscalls[] = {
    {0x080C98B0u, "sol_curve_decompress"},
    {0x11F49D86u, "sol_sha256"},
    {0x13C1B505u, "sol_get_sysvar"},
    {0x174C5122u, "sol_blake3"},
    {0x17E40350u, "sol_secp256k1_recover"},
    {0x188A0031u, "sol_get_last_restart_slot"},
    {0x207559BDu, "sol_log_"},
    {0x23A29A61u, "sol_get_epoch_schedule_sysvar"},
    {0x334FD5EDu, "sol_alt_bn128_compression"},
    {0x3770FB22u, "sol_memset_"},
    {0x3B97B73Cu, "sol_get_fees_sysvar"},
    {0x434371F8u, "sol_memmove_"},
    {0x48504A38u, "sol_try_find_program_address"},
    {0x52BA5096u, "sol_log_compute_units_"},
    {0x5BE92F4Au, "sol_get_epoch_stake"},
    {0x5C2A3178u, "sol_log_64_"},
    {0x5D2245E4u, "sol_get_return_data"},
    {0x5FDCDE31u, "sol_memcmp_"},
    {0x60A40880u, "sol_curve_multiscalar_mul"},
    {0x686093BBu, "sol_panic_"},
    {0x717CC4A3u, "sol_memcpy_"},
    {0x7317B434u, "sol_log_data"},
    {0x780E4C15u, "sol_big_mod_exp"},
    {0x7EF088CAu, "sol_log_pubkey"},
    {0x83F00E8Fu, "sol_alloc_free_"},
    {0x85532D94u, "sol_get_stack_height"},
    {0x9229CDCCu, "sol_sha512"},
    {0x9377323Cu, "sol_create_program_address"},
    {0xA226D3EBu, "sol_set_return_data"},
    {0xA22B9C85u, "sol_invoke_signed_c"},
    {0xA2437F44u, "sol_invoke_signed_v2"},
    {0xAA2607CAu, "sol_curve_validate_point"},
    {0xADB8EFC8u, "sol_get_processed_sibling_instruction"},
    {0xAE0C318Bu, "sol_alt_bn128_group_op"},
    {0xB6FC1A11u, "abort"},
    {0xBF7188F6u, "sol_get_rent_sysvar"},
    {0xC4947C21u, "sol_poseidon"},
    {0xD56B5FE9u, "sol_get_clock_sysvar"},
    {0xD7449092u, "sol_invoke_signed_rust"},
    {0xD7793ABBu, "sol_keccak256"},
    {0xDD1C41A6u, "sol_curve_group_op"},
    {0xEDEF5AEEu, "sol_remaining_compute_units"},
    {0xF111A47Eu, "sol_curve_pairing_map"},
    {0xFDBA2B3Bu, "sol_get_epoch_rewards_sysvar"},
};

static const char *const sbpf_end_names[2][3] = {
    {"le16", "le32", "le64"},
    {"be16", "be32", "be64"},
};

static const char sbpf_call[] = "call";

#define SBPF_REG_FP 10u

static const sbpf_op *sbpf_lookup(uint32_t version, uint32_t opcode)
{
    const sbpf_op *op = version == 2 ? &sbpf_v2_ops[opcode & 0xFFu] : &sbpf_legacy_ops[opcode & 0xFFu];

    if (op->format == SBPF_FMT_NONE || ((op->flags & SBPF_F_JMP32) != 0 && version < 3)) {
        return NULL;
    }
    return op;
}

uint32_t xxbyte_ebpf_sbpf_hash(const void *data, size_t size)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 0;
    uint32_t k;
    size_t i;
    size_t blocks = size / 4u;

    if (p == NULL) {
        size = 0;
        blocks = 0;
    }
    for (i = 0; i < blocks; ++i) {
        k = xxbyte_u32le(p + 4u * i);
        k *= 0xCC9E2D51u;
        k = (k << 15) | (k >> 17);
        k *= 0x1B873593u;
        h ^= k;
        h = (h << 13) | (h >> 19);
        h = h * 5u + 0xE6546B64u;
    }
    k = 0;
    switch (size & 3u) {
        case 3: k ^= (uint32_t)p[4u * blocks + 2u] << 16; /* fall through */
        case 2: k ^= (uint32_t)p[4u * blocks + 1u] << 8;  /* fall through */
        case 1:
            k ^= p[4u * blocks];
            k *= 0xCC9E2D51u;
            k = (k << 15) | (k >> 17);
            k *= 0x1B873593u;
            h ^= k;
            break;
        default: break;
    }
    h ^= (uint32_t)size;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

const char *xxbyte_ebpf_sbpf_syscall_name(uint32_t hash)
{
    size_t low = 0;
    size_t high = sizeof(sbpf_syscalls) / sizeof(sbpf_syscalls[0]);

    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (sbpf_syscalls[middle].hash == hash) {
            return sbpf_syscalls[middle].name;
        }
        if (sbpf_syscalls[middle].hash < hash) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return NULL;
}

static void sbpf_add_reg(xxbyte_instruction *instruction, uint32_t reg, uint32_t bits)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_REGISTER, 1, (int64_t)reg);
    if (operand != NULL) {
        operand->extra = bits;
    }
}

static void sbpf_add_signed(xxbyte_instruction *instruction, uint32_t size, int64_t value, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, size, value);
    if (operand != NULL) {
        operand->flags = flags;
    }
}

static void sbpf_add_memory(xxbyte_instruction *instruction, uint32_t base, int32_t off, uint32_t width)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_MEMORY, width, off);
    if (operand != NULL) {
        operand->kind = (uint8_t)base;
    }
}

/* A target of address + 8 * (rel + 1), modulo 2^64; extra = rel. */
static void sbpf_add_branch(xxbyte_instruction *instruction, uint32_t size, uint64_t address, int64_t rel)
{
    uint64_t target = address + 8u * ((uint64_t)rel + 1u);
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, size, (int64_t)target);
    if (operand != NULL) {
        operand->extra = (uint64_t)rel;
    }
}

/* The per-instruction checks of RequisiteVerifier. */
static int sbpf_valid(uint32_t version, uint32_t opcode, const sbpf_op *op, uint32_t dst, uint32_t src, int32_t imm)
{
    if (src > SBPF_REG_FP || dst > SBPF_REG_FP) {
        return 0;
    }
    if (dst == SBPF_REG_FP && (op->flags & SBPF_F_STORE) == 0) {
        /* SIMD-0166: v1 and v2 bump the frame pointer by multiples of 64. */
        if (opcode != 0x07u || (version != 1 && version != 2) || (imm & 63) != 0) {
            return 0;
        }
    }
    switch (op->check) {
        case SBPF_CHECK_NONZERO: return imm != 0;
        case SBPF_CHECK_SHIFT32: return imm >= 0 && imm < 32;
        case SBPF_CHECK_SHIFT64: return imm >= 0 && imm < 64;
        case SBPF_CHECK_ENDIAN: return imm == 16 || imm == 32 || imm == 64;
        default: break;
    }
    if (op->format == SBPF_FMT_CALLX) {
        int32_t reg = version <= 1 ? imm : (int32_t)(version == 2 ? src : dst);
        return reg >= 0 && reg <= 9;
    }
    return 1;
}

xxbyte_status xxbyte_ebpf_sbpf_decode(uint32_t mode, const xxbyte_ebpf_context *context, const uint8_t *code, size_t code_size, uint64_t address,
                                      xxbyte_instruction *instruction)
{
    uint32_t version = XXBYTE_EBPF_MODE_SBPF_VERSION(mode);
    const sbpf_op *op = sbpf_lookup(version, code[0]);
    uint32_t dst;
    uint32_t src;
    int32_t off;
    int32_t imm;
    uint32_t bits;

    if (op == NULL) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < XXBYTE_EBPF_SLOT_SIZE || (op->format == SBPF_FMT_LDDW && code_size < 2u * XXBYTE_EBPF_SLOT_SIZE)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    dst = (uint32_t)code[1] & 0x0Fu;
    src = (uint32_t)code[1] >> 4;
    off = (int32_t)xxbyte_sext(xxbyte_u16le(code + 2), 16);
    imm = (int32_t)xxbyte_sext(xxbyte_u32le(code + 4), 32);
    if (!sbpf_valid(version, code[0], op, dst, src, imm) || (op->format == SBPF_FMT_LDDW && code[8] != 0)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    bits = (op->flags & SBPF_F_W32) != 0 ? 32u : 64u;
    instruction->size = op->format == SBPF_FMT_LDDW ? 2u * XXBYTE_EBPF_SLOT_SIZE : XXBYTE_EBPF_SLOT_SIZE;
    instruction->opcode = code[0];
    instruction->id = op->id;
    instruction->mnemonic = op->mnemonic;
    switch (op->format) {
        case SBPF_FMT_ALU_K:
            sbpf_add_reg(instruction, dst, bits);
            sbpf_add_signed(instruction, 4, imm, XXBYTE_OPERAND_FLAG_SIGNED);
            break;
        case SBPF_FMT_ALU_X:
            sbpf_add_reg(instruction, dst, bits);
            sbpf_add_reg(instruction, src, bits);
            break;
        case SBPF_FMT_NEG: sbpf_add_reg(instruction, dst, bits); break;
        case SBPF_FMT_END:
            instruction->mnemonic = sbpf_end_names[op->id == XXBYTE_EBPF_ID_ENDBE][imm == 16 ? 0 : imm == 32 ? 1 : 2];
            sbpf_add_reg(instruction, dst, 64);
            sbpf_add_signed(instruction, 4, imm, 0);
            break;
        case SBPF_FMT_LDDW: {
            uint64_t value = (uint64_t)xxbyte_u32le(code + 4) | ((uint64_t)xxbyte_u32le(code + 12) << 32);
            sbpf_add_reg(instruction, dst, 64);
            sbpf_add_signed(instruction, 8, (int64_t)value, XXBYTE_OPERAND_FLAG_SIGNED | XXBYTE_OPERAND_FLAG_WIDE);
            break;
        }
        case SBPF_FMT_LDX:
            sbpf_add_reg(instruction, dst, 64);
            sbpf_add_memory(instruction, src, off, op->width);
            break;
        case SBPF_FMT_ST:
            sbpf_add_memory(instruction, dst, off, op->width);
            sbpf_add_signed(instruction, 4, imm, XXBYTE_OPERAND_FLAG_SIGNED);
            break;
        case SBPF_FMT_STX:
            sbpf_add_memory(instruction, dst, off, op->width);
            sbpf_add_reg(instruction, src, 64);
            break;
        case SBPF_FMT_JA:
            instruction->group = XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP;
            sbpf_add_branch(instruction, 2, address, off);
            break;
        case SBPF_FMT_JCC_K:
        case SBPF_FMT_JCC_X:
            instruction->group = XXBYTE_GROUP_CONDITIONAL;
            sbpf_add_reg(instruction, dst, bits);
            if (op->format == SBPF_FMT_JCC_X) {
                sbpf_add_reg(instruction, src, bits);
            } else {
                sbpf_add_signed(instruction, 4, imm, XXBYTE_OPERAND_FLAG_SIGNED);
            }
            sbpf_add_branch(instruction, 2, address, off);
            break;
        case SBPF_FMT_CALL: {
            uint64_t target = 0;
            instruction->group = XXBYTE_GROUP_CALL;
            if (version >= 3 ? src == 1 : (context != NULL && context->function_address != NULL && context->function_address(context->user, (uint32_t)imm, &target))) {
                instruction->id = XXBYTE_EBPF_ID_CALL_LOCAL;
                instruction->mnemonic = sbpf_call;
                if (version >= 3) {
                    sbpf_add_branch(instruction, 4, address, imm);
                } else {
                    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, 4, (int64_t)target);
                    if (operand != NULL) {
                        operand->extra = (uint64_t)(int64_t)imm;
                    }
                }
            } else if (version >= 3 && src != 0) {
                /* Neither a syscall nor a local call: the verifier accepts
                 * it and it faults when executed. */
                instruction->id = XXBYTE_EBPF_ID_CALL_UNRESOLVED;
                instruction->mnemonic = sbpf_call;
                sbpf_add_signed(instruction, 4, imm, XXBYTE_OPERAND_FLAG_SIGNED);
            } else {
                xxbyte_operand *operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_EBPF_INDEX_SYSCALL, 4, imm);
                if (operand != NULL) {
                    operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
                }
            }
            break;
        }
        case SBPF_FMT_CALLX:
            instruction->group = XXBYTE_GROUP_CALL;
            sbpf_add_reg(instruction, version <= 1 ? (uint32_t)imm : version == 2 ? src : dst, 64);
            break;
        default: /* SBPF_FMT_EXIT */ instruction->group = XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP; break;
    }
    return XXBYTE_STATUS_OK;
}

/* Formatting: letters go through sbpf_put (uppercased for
 * XXBYTE_FORMAT_UPPERCASE); numbers and names are printed apart. */

static int sbpf_put(xxbyte_fmt *fmt, const char *text)
{
    if ((fmt->flags & XXBYTE_FORMAT_UPPERCASE) == 0) {
        return xxbyte_fmt_str(fmt, text);
    }
    for (; *text != '\0'; ++text) {
        char c = *text;
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (!xxbyte_fmt_char(fmt, c)) {
            return 0;
        }
    }
    return 1;
}

static int sbpf_reg(xxbyte_fmt *fmt, const xxbyte_operand *operand)
{
    return sbpf_put(fmt, "r") && xxbyte_fmt_printf(fmt, "%u", (unsigned)operand->value);
}

/* "[r1+0x4]", "[r10-0x8]". */
static int sbpf_memory(xxbyte_fmt *fmt, const xxbyte_operand *operand)
{
    int64_t off = operand->value;
    uint64_t magnitude = off < 0 ? 0u - (uint64_t)off : (uint64_t)off;
    return xxbyte_fmt_char(fmt, '[') && sbpf_put(fmt, "r") &&
           xxbyte_fmt_printf(fmt, "%u%s0x%llx]", (unsigned)operand->kind, off < 0 ? "-" : "+", (unsigned long long)magnitude);
}

/*
 * The resolver's name, else "lbb_" and the slot index of the target. A
 * target below 0 prints as the reference computes it: wrapped to 64 bits
 * for jumps, to 32 bits for the SBPFv3 call.
 */
static int sbpf_label(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t index)
{
    int resolved = xxbyte_fmt_resolve(fmt, instruction, index);
    uint64_t value = (uint64_t)instruction->operands[index].value;
    uint64_t slot = value >> 3;

    if (resolved != 0) {
        return resolved > 0;
    }
    if ((value >> 63) != 0) {
        slot |= UINT64_C(7) << 61;
    }
    if (instruction->id == XXBYTE_EBPF_ID_CALL_LOCAL && XXBYTE_EBPF_MODE_SBPF_VERSION(instruction->mode) >= 3) {
        slot &= 0xFFFFFFFFu;
    }
    return sbpf_put(fmt, "lbb_") && xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)slot);
}

static int sbpf_syscall(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t index)
{
    int resolved = xxbyte_fmt_resolve(fmt, instruction, index);
    const char *name;

    if (resolved != 0) {
        return resolved > 0;
    }
    name = xxbyte_ebpf_sbpf_syscall_name((uint32_t)instruction->operands[index].value);
    if (name != NULL) {
        return xxbyte_fmt_str(fmt, name);
    }
    return xxbyte_fmt_printf(fmt, "%lld", (long long)instruction->operands[index].value);
}

static int sbpf_type(const xxbyte_instruction *instruction, uint32_t index, xxbyte_operand_type type)
{
    return index < instruction->operand_count && instruction->operands[index].type == (uint8_t)type;
}

/* The operand layout of each format, so that a hand-made instruction
 * cannot make the formatter read the wrong operands. */
static int sbpf_fits(const xxbyte_instruction *instruction, const sbpf_op *op)
{
    const uint32_t n = instruction->operand_count;
    const xxbyte_operand_type R = XXBYTE_OPERAND_REGISTER;
    const xxbyte_operand_type I = XXBYTE_OPERAND_IMMEDIATE;

    switch (op->format) {
        case SBPF_FMT_ALU_K:
        case SBPF_FMT_END:
        case SBPF_FMT_LDDW: return n == 2 && sbpf_type(instruction, 0, R) && sbpf_type(instruction, 1, I);
        case SBPF_FMT_ALU_X: return n == 2 && sbpf_type(instruction, 0, R) && sbpf_type(instruction, 1, R);
        case SBPF_FMT_NEG:
        case SBPF_FMT_CALLX: return n == 1 && sbpf_type(instruction, 0, R);
        case SBPF_FMT_LDX: return n == 2 && sbpf_type(instruction, 0, R) && sbpf_type(instruction, 1, XXBYTE_OPERAND_MEMORY);
        case SBPF_FMT_ST: return n == 2 && sbpf_type(instruction, 0, XXBYTE_OPERAND_MEMORY) && sbpf_type(instruction, 1, I);
        case SBPF_FMT_STX: return n == 2 && sbpf_type(instruction, 0, XXBYTE_OPERAND_MEMORY) && sbpf_type(instruction, 1, R);
        case SBPF_FMT_JA: return n == 1 && sbpf_type(instruction, 0, XXBYTE_OPERAND_BRANCH);
        case SBPF_FMT_JCC_K: return n == 3 && sbpf_type(instruction, 0, R) && sbpf_type(instruction, 1, I) && sbpf_type(instruction, 2, XXBYTE_OPERAND_BRANCH);
        case SBPF_FMT_JCC_X: return n == 3 && sbpf_type(instruction, 0, R) && sbpf_type(instruction, 1, R) && sbpf_type(instruction, 2, XXBYTE_OPERAND_BRANCH);
        case SBPF_FMT_CALL:
            if (n != 1) {
                return 0;
            }
            switch (instruction->id) {
                case XXBYTE_EBPF_ID_CALL: return instruction->mnemonic == op->mnemonic && sbpf_type(instruction, 0, XXBYTE_OPERAND_INDEX);
                case XXBYTE_EBPF_ID_CALL_LOCAL: return instruction->mnemonic == sbpf_call && sbpf_type(instruction, 0, XXBYTE_OPERAND_BRANCH);
                case XXBYTE_EBPF_ID_CALL_UNRESOLVED: return instruction->mnemonic == sbpf_call && sbpf_type(instruction, 0, I);
                default: return 0;
            }
        default: return n == 0;
    }
}

int xxbyte_ebpf_sbpf_format(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const xxbyte_operand *o = instruction->operands;
    const sbpf_op *op;

    if (!XXBYTE_EBPF_MODE_IS_SBPF(instruction->mode) || XXBYTE_EBPF_MODE_SBPF_VERSION(instruction->mode) > XXBYTE_EBPF_SBPF_NEWEST || instruction->opcode > 0xFFu) {
        return 0;
    }
    op = sbpf_lookup(XXBYTE_EBPF_MODE_SBPF_VERSION(instruction->mode), instruction->opcode);
    if (op == NULL || (op->format != SBPF_FMT_CALL && (instruction->id != op->id || (op->format != SBPF_FMT_END && instruction->mnemonic != op->mnemonic))) ||
        !sbpf_fits(instruction, op)) {
        return 0;
    }
    if (op->format == SBPF_FMT_END) {
        const char *const *names = sbpf_end_names[op->id == XXBYTE_EBPF_ID_ENDBE];
        if (instruction->mnemonic != names[0] && instruction->mnemonic != names[1] && instruction->mnemonic != names[2]) {
            return 0;
        }
    }
    switch (op->format) {
        case SBPF_FMT_ALU_K: return xxbyte_fmt_char(fmt, ' ') && sbpf_reg(fmt, &o[0]) && xxbyte_fmt_printf(fmt, ", %lld", (long long)o[1].value);
        case SBPF_FMT_ALU_X: return xxbyte_fmt_char(fmt, ' ') && sbpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, ", ") && sbpf_reg(fmt, &o[1]);
        case SBPF_FMT_NEG:
        case SBPF_FMT_END:
        case SBPF_FMT_CALLX: return xxbyte_fmt_char(fmt, ' ') && sbpf_reg(fmt, &o[0]);
        case SBPF_FMT_LDDW: return xxbyte_fmt_char(fmt, ' ') && sbpf_reg(fmt, &o[0]) && xxbyte_fmt_printf(fmt, ", 0x%llx", (unsigned long long)o[1].value);
        case SBPF_FMT_LDX: return xxbyte_fmt_char(fmt, ' ') && sbpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, ", ") && sbpf_memory(fmt, &o[1]);
        case SBPF_FMT_ST: return xxbyte_fmt_char(fmt, ' ') && sbpf_memory(fmt, &o[0]) && xxbyte_fmt_printf(fmt, ", %lld", (long long)o[1].value);
        case SBPF_FMT_STX: return xxbyte_fmt_char(fmt, ' ') && sbpf_memory(fmt, &o[0]) && xxbyte_fmt_str(fmt, ", ") && sbpf_reg(fmt, &o[1]);
        case SBPF_FMT_JA: return xxbyte_fmt_char(fmt, ' ') && sbpf_label(fmt, instruction, 0);
        case SBPF_FMT_JCC_K:
            return xxbyte_fmt_char(fmt, ' ') && sbpf_reg(fmt, &o[0]) && xxbyte_fmt_printf(fmt, ", %lld, ", (long long)o[1].value) && sbpf_label(fmt, instruction, 2);
        case SBPF_FMT_JCC_X:
            return xxbyte_fmt_char(fmt, ' ') && sbpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, ", ") && sbpf_reg(fmt, &o[1]) && xxbyte_fmt_str(fmt, ", ") &&
                   sbpf_label(fmt, instruction, 2);
        case SBPF_FMT_CALL:
            if (!xxbyte_fmt_char(fmt, ' ')) {
                return 0;
            }
            switch (instruction->id) {
                case XXBYTE_EBPF_ID_CALL: return sbpf_syscall(fmt, instruction, 0);
                case XXBYTE_EBPF_ID_CALL_LOCAL: return sbpf_label(fmt, instruction, 0);
                default: return xxbyte_fmt_printf(fmt, "%lld", (long long)o[0].value);
            }
        default: return 1; /* exit */
    }
}

#else
typedef int xxbyte_ebpf_sbpf_disabled;
#endif
