#include "xxbyte_ebpf_internal.h"

#if XXBYTE_USE_EBPF

/*
 * eBPF. Encodings and field rules: RFC 9669 (sections 3-5 and the
 * Appendix A registry) and the Linux verifier's reserved-field checks; the
 * later additions (load-acquire, store-release, may_goto, addr_space_cast,
 * gotox, the r11 stack arguments) follow include/uapi/linux/bpf.h and
 * kernel/bpf/verifier.c, callx follows LLVM. The text is that of
 * llvm-objdump 21; an instruction's cpu version is the first LLVM -mcpu
 * whose code generator emits it (BPFSubtarget.cpp and the Predicates of
 * BPFInstrInfo.td, release 21 and main), with the alu32 target feature
 * that clang accepts below v3.
 */

#define EBPF_CLASS_LD 0x00u
#define EBPF_CLASS_LDX 0x01u
#define EBPF_CLASS_ST 0x02u
#define EBPF_CLASS_STX 0x03u
#define EBPF_CLASS_ALU 0x04u
#define EBPF_CLASS_JMP 0x05u
#define EBPF_CLASS_JMP32 0x06u
#define EBPF_CLASS_ALU64 0x07u

#define EBPF_SRC_X 0x08u

#define EBPF_MODE_IMM 0x00u
#define EBPF_MODE_ABS 0x20u
#define EBPF_MODE_IND 0x40u
#define EBPF_MODE_MEM 0x60u
#define EBPF_MODE_MEMSX 0x80u
#define EBPF_MODE_ATOMIC 0xC0u

#define EBPF_SIZE_W 0x00u
#define EBPF_SIZE_DW 0x18u

#define EBPF_REG_LAST 10u   /* r0..r10 */
#define EBPF_REG_PARAMS 11u /* stack-argument base (Linux BPF_REG_PARAMS) */

#define EBPF_ATOMIC_LOAD_ACQ 0x100
#define EBPF_ATOMIC_STORE_REL 0x110

/* The lddw src_reg subtypes (RFC 9669 Table 12). */
#define EBPF_PSEUDO_LAST 6u

#define EBPF_KNOWN_MODE_BITS (XXBYTE_EBPF_MODE_BIG_ENDIAN | XXBYTE_EBPF_MODE_CPU_MASK)

/* Register view widths stored in REGISTER operands' extra. */
#define EBPF_R 64u
#define EBPF_W 32u

enum ebpf_format {
    EBPF_FMT_ALU,        /* dst op= src|imm */
    EBPF_FMT_NEG,        /* dst = -dst */
    EBPF_FMT_MOVSX,      /* dst = (s8)src */
    EBPF_FMT_ASCAST,     /* dst = addr_space_cast(src, a, b) */
    EBPF_FMT_END,        /* dst = le16 dst */
    EBPF_FMT_JA,         /* goto +off <target> */
    EBPF_FMT_JCC,        /* if dst op src|imm goto +off <target> */
    EBPF_FMT_CALL,       /* call imm */
    EBPF_FMT_CALL_LOCAL, /* call imm <target> */
    EBPF_FMT_CALL_KFUNC, /* call kfunc imm[, off] */
    EBPF_FMT_REG_JUMP,   /* callx r1, gotox r1 */
    EBPF_FMT_EXIT,       /* exit */
    EBPF_FMT_LDDW,       /* dst = imm64 ll */
    EBPF_FMT_LD_MAP,     /* dst = map_by_fd(imm) ll */
    EBPF_FMT_LD_MAP_VAL, /* dst = map_val(map_by_fd(imm)) + next_imm ll */
    EBPF_FMT_LD_VAR,     /* dst = var_addr(imm[, next_imm]) ll */
    EBPF_FMT_LD_CODE,    /* dst = code_addr(+imm <target>) ll */
    EBPF_FMT_LD_PACKET,  /* r0 = *(u32 *)skb[imm] or skb[src + imm] */
    EBPF_FMT_LDX,        /* dst = *(u32 *)(src + off) */
    EBPF_FMT_ST,         /* *(u32 *)(dst + off) = src|imm */
    EBPF_FMT_LOCK,       /* lock *(u32 *)(dst + off) op= src */
    EBPF_FMT_FETCH,      /* src = atomic_fetch_op((u32 *)(dst + off), src) */
    EBPF_FMT_XCHG,       /* src = xchg_64(dst + off, src) */
    EBPF_FMT_CMPXCHG,    /* r0 = cmpxchg_64(dst + off, r0, src) */
    EBPF_FMT_LOAD_ACQ,   /* dst = load_acquire((u32 *)(src + off)) */
    EBPF_FMT_STORE_REL   /* store_release((u32 *)(dst + off), src) */
};

#define EBPF_G_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define EBPF_G_JCC XXBYTE_GROUP_CONDITIONAL
#define EBPF_G_CALL XXBYTE_GROUP_CALL
#define EBPF_G_EXIT (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)

typedef struct ebpf_op {
    const char *mnemonic;
    const char *text; /* operator, condition, keyword or function name */
    uint8_t format;   /* enum ebpf_format */
    uint8_t cpu;      /* first LLVM cpu version */
    uint16_t group;
} ebpf_op;

/* Indexed by xxbyte_ebpf_id - 1. */
static const ebpf_op ebpf_ops[] = {
    /* ALU64 */
    {"add", "+=", EBPF_FMT_ALU, 1, 0},
    {"sub", "-=", EBPF_FMT_ALU, 1, 0},
    {"mul", "*=", EBPF_FMT_ALU, 1, 0},
    {"div", "/=", EBPF_FMT_ALU, 1, 0},
    {"sdiv", "s/=", EBPF_FMT_ALU, 4, 0},
    {"or", "|=", EBPF_FMT_ALU, 1, 0},
    {"and", "&=", EBPF_FMT_ALU, 1, 0},
    {"lsh", "<<=", EBPF_FMT_ALU, 1, 0},
    {"rsh", ">>=", EBPF_FMT_ALU, 1, 0},
    {"neg", "-", EBPF_FMT_NEG, 1, 0},
    {"mod", "%=", EBPF_FMT_ALU, 1, 0},
    {"smod", "s%=", EBPF_FMT_ALU, 4, 0},
    {"xor", "^=", EBPF_FMT_ALU, 1, 0},
    {"mov", "=", EBPF_FMT_ALU, 1, 0},
    {"movsx", "s", EBPF_FMT_MOVSX, 4, 0},
    {"arsh", "s>>=", EBPF_FMT_ALU, 1, 0},
    {"bswap", "bswap", EBPF_FMT_END, 4, 0},
    {"addr_space_cast", "addr_space_cast", EBPF_FMT_ASCAST, 1, 0},
    /* ALU */
    {"add32", "+=", EBPF_FMT_ALU, 1, 0},
    {"sub32", "-=", EBPF_FMT_ALU, 1, 0},
    {"mul32", "*=", EBPF_FMT_ALU, 1, 0},
    {"div32", "/=", EBPF_FMT_ALU, 1, 0},
    {"sdiv32", "s/=", EBPF_FMT_ALU, 4, 0},
    {"or32", "|=", EBPF_FMT_ALU, 1, 0},
    {"and32", "&=", EBPF_FMT_ALU, 1, 0},
    {"lsh32", "<<=", EBPF_FMT_ALU, 1, 0},
    {"rsh32", ">>=", EBPF_FMT_ALU, 1, 0},
    {"neg32", "-", EBPF_FMT_NEG, 1, 0},
    {"mod32", "%=", EBPF_FMT_ALU, 1, 0},
    {"smod32", "s%=", EBPF_FMT_ALU, 4, 0},
    {"xor32", "^=", EBPF_FMT_ALU, 1, 0},
    {"mov32", "=", EBPF_FMT_ALU, 1, 0},
    {"movsx32", "s", EBPF_FMT_MOVSX, 4, 0},
    {"arsh32", "s>>=", EBPF_FMT_ALU, 1, 0},
    {"endle", "le", EBPF_FMT_END, 1, 0},
    {"endbe", "be", EBPF_FMT_END, 1, 0},
    /* JMP */
    {"ja", "goto", EBPF_FMT_JA, 1, EBPF_G_JUMP},
    {"jeq", "==", EBPF_FMT_JCC, 1, EBPF_G_JCC},
    {"jgt", ">", EBPF_FMT_JCC, 1, EBPF_G_JCC},
    {"jge", ">=", EBPF_FMT_JCC, 1, EBPF_G_JCC},
    {"jset", "&", EBPF_FMT_JCC, 1, EBPF_G_JCC},
    {"jne", "!=", EBPF_FMT_JCC, 1, EBPF_G_JCC},
    {"jsgt", "s>", EBPF_FMT_JCC, 1, EBPF_G_JCC},
    {"jsge", "s>=", EBPF_FMT_JCC, 1, EBPF_G_JCC},
    {"call", "call", EBPF_FMT_CALL, 1, EBPF_G_CALL},
    {"call_local", "call", EBPF_FMT_CALL_LOCAL, 1, EBPF_G_CALL},
    {"call_kfunc", "call kfunc", EBPF_FMT_CALL_KFUNC, 1, EBPF_G_CALL},
    {"callx", "callx", EBPF_FMT_REG_JUMP, 1, EBPF_G_CALL},
    {"exit", "exit", EBPF_FMT_EXIT, 1, EBPF_G_EXIT},
    {"jlt", "<", EBPF_FMT_JCC, 2, EBPF_G_JCC},
    {"jle", "<=", EBPF_FMT_JCC, 2, EBPF_G_JCC},
    {"jslt", "s<", EBPF_FMT_JCC, 2, EBPF_G_JCC},
    {"jsle", "s<=", EBPF_FMT_JCC, 2, EBPF_G_JCC},
    {"may_goto", "may_goto", EBPF_FMT_JA, 1, EBPF_G_JCC},
    {"gotox", "gotox", EBPF_FMT_REG_JUMP, 4, EBPF_G_JUMP},
    /* JMP32 */
    {"ja32", "gotol", EBPF_FMT_JA, 4, EBPF_G_JUMP},
    {"jeq32", "==", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    {"jgt32", ">", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    {"jge32", ">=", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    {"jset32", "&", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    {"jne32", "!=", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    {"jsgt32", "s>", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    {"jsge32", "s>=", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    {"jlt32", "<", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    {"jle32", "<=", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    {"jslt32", "s<", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    {"jsle32", "s<=", EBPF_FMT_JCC, 3, EBPF_G_JCC},
    /* LD: lddw by src_reg, then the packet loads by size */
    {"lddw", "", EBPF_FMT_LDDW, 1, 0},
    {"lddw_map_fd", "map_by_fd", EBPF_FMT_LD_MAP, 1, 0},
    {"lddw_map_value", "map_by_fd", EBPF_FMT_LD_MAP_VAL, 1, 0},
    {"lddw_var_addr", "var_addr", EBPF_FMT_LD_VAR, 1, 0},
    {"lddw_code_addr", "code_addr", EBPF_FMT_LD_CODE, 1, 0},
    {"lddw_map_idx", "map_by_idx", EBPF_FMT_LD_MAP, 1, 0},
    {"lddw_map_idx_value", "map_by_idx", EBPF_FMT_LD_MAP_VAL, 1, 0},
    {"ldabsw", "", EBPF_FMT_LD_PACKET, 1, 0},
    {"ldabsh", "", EBPF_FMT_LD_PACKET, 1, 0},
    {"ldabsb", "", EBPF_FMT_LD_PACKET, 1, 0},
    {"ldindw", "", EBPF_FMT_LD_PACKET, 1, 0},
    {"ldindh", "", EBPF_FMT_LD_PACKET, 1, 0},
    {"ldindb", "", EBPF_FMT_LD_PACKET, 1, 0},
    /* LDX */
    {"ldxw", "u", EBPF_FMT_LDX, 1, 0},
    {"ldxh", "u", EBPF_FMT_LDX, 1, 0},
    {"ldxb", "u", EBPF_FMT_LDX, 1, 0},
    {"ldxdw", "u", EBPF_FMT_LDX, 1, 0},
    {"ldxsw", "s", EBPF_FMT_LDX, 4, 0},
    {"ldxsh", "s", EBPF_FMT_LDX, 4, 0},
    {"ldxsb", "s", EBPF_FMT_LDX, 4, 0},
    /* ST, STX */
    {"stw", "", EBPF_FMT_ST, 4, 0},
    {"sth", "", EBPF_FMT_ST, 4, 0},
    {"stb", "", EBPF_FMT_ST, 4, 0},
    {"stdw", "", EBPF_FMT_ST, 4, 0},
    {"stxw", "", EBPF_FMT_ST, 1, 0},
    {"stxh", "", EBPF_FMT_ST, 1, 0},
    {"stxb", "", EBPF_FMT_ST, 1, 0},
    {"stxdw", "", EBPF_FMT_ST, 1, 0},
    /* STX ATOMIC DW (v1); the W forms need alu32 below v3 */
    {"aadd", "+=", EBPF_FMT_LOCK, 1, 0},
    {"aor", "|=", EBPF_FMT_LOCK, 1, 0},
    {"aand", "&=", EBPF_FMT_LOCK, 1, 0},
    {"axor", "^=", EBPF_FMT_LOCK, 1, 0},
    {"afadd", "atomic_fetch_add", EBPF_FMT_FETCH, 1, 0},
    {"afor", "atomic_fetch_or", EBPF_FMT_FETCH, 1, 0},
    {"afand", "atomic_fetch_and", EBPF_FMT_FETCH, 1, 0},
    {"afxor", "atomic_fetch_xor", EBPF_FMT_FETCH, 1, 0},
    {"axchg", "xchg_64", EBPF_FMT_XCHG, 1, 0},
    {"acmp", "cmpxchg_64", EBPF_FMT_CMPXCHG, 1, 0},
    /* STX ATOMIC W */
    {"aadd32", "+=", EBPF_FMT_LOCK, 1, 0},
    {"aor32", "|=", EBPF_FMT_LOCK, 1, 0},
    {"aand32", "&=", EBPF_FMT_LOCK, 1, 0},
    {"axor32", "^=", EBPF_FMT_LOCK, 1, 0},
    {"afadd32", "atomic_fetch_add", EBPF_FMT_FETCH, 1, 0},
    {"afor32", "atomic_fetch_or", EBPF_FMT_FETCH, 1, 0},
    {"afand32", "atomic_fetch_and", EBPF_FMT_FETCH, 1, 0},
    {"afxor32", "atomic_fetch_xor", EBPF_FMT_FETCH, 1, 0},
    {"axchg32", "xchg32_32", EBPF_FMT_XCHG, 1, 0},
    {"acmp32", "cmpxchg32_32", EBPF_FMT_CMPXCHG, 1, 0},
    /* STX ATOMIC load-acquire and store-release, by size */
    {"ldacqw", "load_acquire", EBPF_FMT_LOAD_ACQ, 4, 0},
    {"ldacqh", "load_acquire", EBPF_FMT_LOAD_ACQ, 4, 0},
    {"ldacqb", "load_acquire", EBPF_FMT_LOAD_ACQ, 4, 0},
    {"ldacqdw", "load_acquire", EBPF_FMT_LOAD_ACQ, 4, 0},
    {"strelw", "store_release", EBPF_FMT_STORE_REL, 4, 0},
    {"strelh", "store_release", EBPF_FMT_STORE_REL, 4, 0},
    {"strelb", "store_release", EBPF_FMT_STORE_REL, 4, 0},
    {"streldw", "store_release", EBPF_FMT_STORE_REL, 4, 0},
    /* sBPF only (cpu 0; never decoded in a Linux mode) */
    {"uhmul64", "", EBPF_FMT_ALU, 0, 0},
    {"shmul64", "", EBPF_FMT_ALU, 0, 0},
    {"hor64", "", EBPF_FMT_ALU, 0, 0},
    {"call_unresolved", "", EBPF_FMT_CALL, 0, EBPF_G_CALL},
};

typedef char ebpf_ops_complete[(sizeof(ebpf_ops) / sizeof(ebpf_ops[0]) == XXBYTE_EBPF_ID_COUNT - 1) ? 1 : -1];

/* The 'code' nibble of the ALU classes; NEG and END are handled apart. */
static const uint8_t ebpf_alu64_ids[16] = {
    XXBYTE_EBPF_ID_ADD,
    XXBYTE_EBPF_ID_SUB,
    XXBYTE_EBPF_ID_MUL,
    XXBYTE_EBPF_ID_DIV,
    XXBYTE_EBPF_ID_OR,
    XXBYTE_EBPF_ID_AND,
    XXBYTE_EBPF_ID_LSH,
    XXBYTE_EBPF_ID_RSH,
    0,
    XXBYTE_EBPF_ID_MOD,
    XXBYTE_EBPF_ID_XOR,
    XXBYTE_EBPF_ID_MOV,
    XXBYTE_EBPF_ID_ARSH,
    0,
    0,
    0,
};

static const uint8_t ebpf_alu32_ids[16] = {
    XXBYTE_EBPF_ID_ADD32,
    XXBYTE_EBPF_ID_SUB32,
    XXBYTE_EBPF_ID_MUL32,
    XXBYTE_EBPF_ID_DIV32,
    XXBYTE_EBPF_ID_OR32,
    XXBYTE_EBPF_ID_AND32,
    XXBYTE_EBPF_ID_LSH32,
    XXBYTE_EBPF_ID_RSH32,
    0,
    XXBYTE_EBPF_ID_MOD32,
    XXBYTE_EBPF_ID_XOR32,
    XXBYTE_EBPF_ID_MOV32,
    XXBYTE_EBPF_ID_ARSH32,
    0,
    0,
    0,
};

/* The conditional jumps by 'code' nibble; 0 for JA, CALL, EXIT, JCOND. */
static const uint8_t ebpf_jmp_ids[16] = {
    0,
    XXBYTE_EBPF_ID_JEQ,
    XXBYTE_EBPF_ID_JGT,
    XXBYTE_EBPF_ID_JGE,
    XXBYTE_EBPF_ID_JSET,
    XXBYTE_EBPF_ID_JNE,
    XXBYTE_EBPF_ID_JSGT,
    XXBYTE_EBPF_ID_JSGE,
    0,
    0,
    XXBYTE_EBPF_ID_JLT,
    XXBYTE_EBPF_ID_JLE,
    XXBYTE_EBPF_ID_JSLT,
    XXBYTE_EBPF_ID_JSLE,
    0,
    0,
};

static const uint8_t ebpf_jmp32_ids[16] = {
    0,
    XXBYTE_EBPF_ID_JEQ32,
    XXBYTE_EBPF_ID_JGT32,
    XXBYTE_EBPF_ID_JGE32,
    XXBYTE_EBPF_ID_JSET32,
    XXBYTE_EBPF_ID_JNE32,
    XXBYTE_EBPF_ID_JSGT32,
    XXBYTE_EBPF_ID_JSGE32,
    0,
    0,
    XXBYTE_EBPF_ID_JLT32,
    XXBYTE_EBPF_ID_JLE32,
    XXBYTE_EBPF_ID_JSLT32,
    XXBYTE_EBPF_ID_JSLE32,
    0,
    0,
};

/* Access width in bytes by the size field (W, H, B, DW). */
static const uint8_t ebpf_widths[4] = {4, 2, 1, 8};

/* The atomic operations of the W and DW sizes by imm; the first ten ids
 * of each size are in this order. */
static const int32_t ebpf_atomic_imms[10] = {
    0x00, 0x40, 0x50, 0xA0, 0x01, 0x41, 0x51, 0xA1, 0xE1, 0xF1,
};

/* One slot's fields. */
typedef struct ebpf_fields {
    uint32_t opcode;
    uint32_t dst;
    uint32_t src;
    int32_t off;
    int32_t imm;
} ebpf_fields;

static uint32_t ebpf_level(uint32_t mode)
{
    uint32_t cpu = (mode & XXBYTE_EBPF_MODE_CPU_MASK) >> 4;
    return cpu == 0 ? XXBYTE_EBPF_CPU_NEWEST : cpu;
}

static xxbyte_status ebpf_check_mode(uint32_t mode)
{
    if (XXBYTE_EBPF_MODE_IS_SBPF(mode)) {
        return (mode & ~(uint32_t)XXBYTE_EBPF_MODE_SBPF_MASK) != 0 || XXBYTE_EBPF_MODE_SBPF_VERSION(mode) > XXBYTE_EBPF_SBPF_NEWEST ? XXBYTE_STATUS_UNSUPPORTED_MODE
                                                                                                                                    : XXBYTE_STATUS_OK;
    }
    if ((mode & ~(uint32_t)EBPF_KNOWN_MODE_BITS) != 0 || ((mode & XXBYTE_EBPF_MODE_CPU_MASK) >> 4) > XXBYTE_EBPF_CPU_NEWEST) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    return XXBYTE_STATUS_OK;
}

/* code holds at least 8 bytes. */
static void ebpf_read(uint32_t mode, const uint8_t *code, ebpf_fields *f)
{
    f->opcode = code[0];
    if ((mode & XXBYTE_EBPF_MODE_BIG_ENDIAN) != 0) {
        f->dst = (uint32_t)code[1] >> 4;
        f->src = (uint32_t)code[1] & 0x0Fu;
        f->off = (int32_t)xxbyte_sext(xxbyte_u16be(code + 2), 16);
        f->imm = (int32_t)xxbyte_sext(xxbyte_u32be(code + 4), 32);
    } else {
        f->dst = (uint32_t)code[1] & 0x0Fu;
        f->src = (uint32_t)code[1] >> 4;
        f->off = (int32_t)xxbyte_sext(xxbyte_u16le(code + 2), 16);
        f->imm = (int32_t)xxbyte_sext(xxbyte_u32le(code + 4), 32);
    }
}

/*
 * The lowest cpu version at which some encoding with this opcode byte is
 * valid, or 0 when none is. Decides between TRUNCATED and
 * INVALID_INSTRUCTION for inputs shorter than a slot.
 */
static uint32_t ebpf_opcode_level(uint32_t opcode)
{
    uint32_t code = opcode >> 4;
    uint32_t mode = opcode & 0xE0u;
    uint32_t size = opcode & 0x18u;
    int x = (opcode & EBPF_SRC_X) != 0;

    switch (opcode & 0x07u) {
        case EBPF_CLASS_LD:
            if (mode == EBPF_MODE_IMM) {
                return size == EBPF_SIZE_DW ? 1u : 0u;
            }
            return (mode == EBPF_MODE_ABS || mode == EBPF_MODE_IND) && size != EBPF_SIZE_DW ? 1u : 0u;
        case EBPF_CLASS_LDX:
            if (mode == EBPF_MODE_MEM) {
                return 1;
            }
            return mode == EBPF_MODE_MEMSX && size != EBPF_SIZE_DW ? 4u : 0u;
        case EBPF_CLASS_ST: return mode == EBPF_MODE_MEM ? 4u : 0u;
        case EBPF_CLASS_STX:
            if (mode == EBPF_MODE_MEM) {
                return 1;
            }
            if (mode != EBPF_MODE_ATOMIC) {
                return 0;
            }
            return size == EBPF_SIZE_W || size == EBPF_SIZE_DW ? 1u : 4u;
        case EBPF_CLASS_ALU:
        case EBPF_CLASS_ALU64:
            if (code >= 0xE || (code == 0x8 && x)) {
                return 0;
            }
            if (code == 0xD && (opcode & 0x07u) == EBPF_CLASS_ALU64) {
                return x ? 0u : 4u;
            }
            return 1;
        case EBPF_CLASS_JMP:
            switch (code) {
                case 0x0: return x ? 4u : 1u;
                case 0x8: return 1;
                case 0x9: /* EXIT */
                case 0xE: /* JCOND */ return x ? 0u : 1u;
                case 0xF: return 0;
                default: return code >= 0xA ? 2u : 1u;
            }
        default: /* JMP32 */
            if (code == 0x0) {
                return x ? 0u : 4u;
            }
            return ebpf_jmp32_ids[code] != 0 ? 3u : 0u;
    }
}

static uint32_t ebpf_classify_alu(const ebpf_fields *f)
{
    int is64 = (f->opcode & 0x07u) == EBPF_CLASS_ALU64;
    int x = (f->opcode & EBPF_SRC_X) != 0;
    uint32_t code = f->opcode >> 4;

    if (f->dst > EBPF_REG_LAST) {
        return 0;
    }
    switch (code) {
        case 0x8: /* NEG */
            if (x || f->src != 0 || f->off != 0 || f->imm != 0) {
                return 0;
            }
            return is64 ? XXBYTE_EBPF_ID_NEG : XXBYTE_EBPF_ID_NEG32;
        case 0xD: /* END */
            if (f->src != 0 || f->off != 0 || (f->imm != 16 && f->imm != 32 && f->imm != 64)) {
                return 0;
            }
            if (is64) {
                return x ? 0u : (uint32_t)XXBYTE_EBPF_ID_BSWAP;
            }
            return x ? XXBYTE_EBPF_ID_ENDBE : XXBYTE_EBPF_ID_ENDLE;
        case 0xB: /* MOV */
            if (!x) {
                break;
            }
            if (f->src > EBPF_REG_LAST) {
                return 0;
            }
            if (is64 && f->off == 1) {
                return f->imm == 1 || f->imm == 0x10000 ? (uint32_t)XXBYTE_EBPF_ID_ADDR_SPACE_CAST : 0u;
            }
            if (f->imm != 0) {
                return 0;
            }
            if (f->off == 0) {
                return is64 ? XXBYTE_EBPF_ID_MOV : XXBYTE_EBPF_ID_MOV32;
            }
            if (f->off == 8 || f->off == 16 || (is64 && f->off == 32)) {
                return is64 ? XXBYTE_EBPF_ID_MOVSX : XXBYTE_EBPF_ID_MOVSX32;
            }
            return 0;
        case 0xE:
        case 0xF: return 0;
        default: break;
    }
    if (x ? (f->imm != 0 || f->src > EBPF_REG_LAST) : f->src != 0) {
        return 0;
    }
    if (f->off == 1 && (code == 0x3 || code == 0x9)) {
        if (code == 0x3) {
            return is64 ? XXBYTE_EBPF_ID_SDIV : XXBYTE_EBPF_ID_SDIV32;
        }
        return is64 ? XXBYTE_EBPF_ID_SMOD : XXBYTE_EBPF_ID_SMOD32;
    }
    if (f->off != 0) {
        return 0;
    }
    return is64 ? ebpf_alu64_ids[code] : ebpf_alu32_ids[code];
}

static uint32_t ebpf_classify_jmp(const ebpf_fields *f)
{
    int j32 = (f->opcode & 0x07u) == EBPF_CLASS_JMP32;
    int x = (f->opcode & EBPF_SRC_X) != 0;
    uint32_t code = f->opcode >> 4;

    switch (code) {
        case 0x0: /* JA */
            if (j32) {
                return x || f->dst != 0 || f->src != 0 || f->off != 0 ? 0u : (uint32_t)XXBYTE_EBPF_ID_JA32;
            }
            if (x) {
                return f->dst > EBPF_REG_LAST || f->src != 0 || f->off != 0 || f->imm != 0 ? 0u : (uint32_t)XXBYTE_EBPF_ID_GOTOX;
            }
            return f->dst != 0 || f->src != 0 || f->imm != 0 ? 0u : (uint32_t)XXBYTE_EBPF_ID_JA;
        case 0x8: /* CALL */
            if (j32) {
                return 0;
            }
            if (x) {
                return f->dst > EBPF_REG_LAST || f->src != 0 || f->off != 0 || f->imm != 0 ? 0u : (uint32_t)XXBYTE_EBPF_ID_CALLX;
            }
            if (f->dst != 0 || (f->src != 2 && f->off != 0)) {
                return 0;
            }
            switch (f->src) {
                case 0: return XXBYTE_EBPF_ID_CALL;
                case 1: return XXBYTE_EBPF_ID_CALL_LOCAL;
                case 2: return XXBYTE_EBPF_ID_CALL_KFUNC;
                default: return 0;
            }
        case 0x9: /* EXIT */ return j32 || x || f->dst != 0 || f->src != 0 || f->off != 0 || f->imm != 0 ? 0u : (uint32_t)XXBYTE_EBPF_ID_EXIT;
        case 0xE: /* JCOND: may_goto */ return j32 || x || f->dst != 0 || f->src != 0 || f->imm != 0 ? 0u : (uint32_t)XXBYTE_EBPF_ID_MAY_GOTO;
        case 0xF: return 0;
        default: break;
    }
    if (f->dst > EBPF_REG_LAST || (x ? (f->imm != 0 || f->src > EBPF_REG_LAST) : f->src != 0)) {
        return 0;
    }
    return j32 ? ebpf_jmp32_ids[code] : ebpf_jmp_ids[code];
}

/* r11 is valid only as the base of the stack-argument forms; they take
 * the cpu version of their instruction (LLVM emits the ldxdw and stxdw
 * forms for every -mcpu and the stdw form, a BPF_ST, from v4). */
static int ebpf_stack_arg(uint32_t size, int32_t off, int load)
{
    return size == EBPF_SIZE_DW && off % 8 == 0 && (load ? off > 0 : off < 0);
}

static uint32_t ebpf_classify_mem(const ebpf_fields *f)
{
    uint32_t mode = f->opcode & 0xE0u;
    uint32_t size = f->opcode & 0x18u;
    uint32_t index = size >> 3;

    switch (f->opcode & 0x07u) {
        case EBPF_CLASS_LD:
            if (mode == EBPF_MODE_IMM) {
                if (size != EBPF_SIZE_DW || f->off != 0 || f->dst > EBPF_REG_LAST || f->src > EBPF_PSEUDO_LAST) {
                    return 0;
                }
                return XXBYTE_EBPF_ID_LDDW + f->src;
            }
            if ((mode != EBPF_MODE_ABS && mode != EBPF_MODE_IND) || size == EBPF_SIZE_DW || f->dst != 0 || f->off != 0) {
                return 0;
            }
            if (mode == EBPF_MODE_ABS) {
                return f->src != 0 ? 0u : XXBYTE_EBPF_ID_LDABSW + index;
            }
            return f->src > EBPF_REG_LAST ? 0u : XXBYTE_EBPF_ID_LDINDW + index;
        case EBPF_CLASS_LDX:
            if (f->imm != 0 || f->dst > EBPF_REG_LAST) {
                return 0;
            }
            if (mode == EBPF_MODE_MEM) {
                if (f->src > EBPF_REG_LAST && (f->src != EBPF_REG_PARAMS || !ebpf_stack_arg(size, f->off, 1))) {
                    return 0;
                }
                return XXBYTE_EBPF_ID_LDXW + index;
            }
            if (mode != EBPF_MODE_MEMSX || size == EBPF_SIZE_DW || f->src > EBPF_REG_LAST) {
                return 0;
            }
            return XXBYTE_EBPF_ID_LDXSW + index;
        case EBPF_CLASS_ST:
            if (mode != EBPF_MODE_MEM || f->src != 0) {
                return 0;
            }
            if (f->dst > EBPF_REG_LAST && (f->dst != EBPF_REG_PARAMS || !ebpf_stack_arg(size, f->off, 0))) {
                return 0;
            }
            return XXBYTE_EBPF_ID_STW + index;
        default: /* STX */
            if (f->src > EBPF_REG_LAST) {
                return 0;
            }
            if (mode == EBPF_MODE_MEM) {
                if (f->imm != 0) {
                    return 0;
                }
                if (f->dst > EBPF_REG_LAST && (f->dst != EBPF_REG_PARAMS || !ebpf_stack_arg(size, f->off, 0))) {
                    return 0;
                }
                return XXBYTE_EBPF_ID_STXW + index;
            }
            if (mode != EBPF_MODE_ATOMIC || f->dst > EBPF_REG_LAST) {
                return 0;
            }
            if (f->imm == EBPF_ATOMIC_LOAD_ACQ) {
                return XXBYTE_EBPF_ID_LDACQW + index;
            }
            if (f->imm == EBPF_ATOMIC_STORE_REL) {
                return XXBYTE_EBPF_ID_STRELW + index;
            }
            if (size == EBPF_SIZE_W || size == EBPF_SIZE_DW) {
                uint32_t i;
                for (i = 0; i < 10; ++i) {
                    if (f->imm == ebpf_atomic_imms[i]) {
                        return (size == EBPF_SIZE_DW ? XXBYTE_EBPF_ID_AADD : XXBYTE_EBPF_ID_AADD32) + i;
                    }
                }
            }
            return 0;
    }
}

static uint32_t ebpf_classify(const ebpf_fields *f)
{
    switch (f->opcode & 0x07u) {
        case EBPF_CLASS_ALU:
        case EBPF_CLASS_ALU64: return ebpf_classify_alu(f);
        case EBPF_CLASS_JMP:
        case EBPF_CLASS_JMP32: return ebpf_classify_jmp(f);
        default: return ebpf_classify_mem(f);
    }
}

static void ebpf_add_reg(xxbyte_instruction *instruction, uint32_t reg, uint32_t bits)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_REGISTER, 1, (int64_t)reg);
    if (operand != NULL) {
        operand->extra = bits;
    }
}

static void ebpf_add_implicit_reg(xxbyte_instruction *instruction, uint32_t reg, uint32_t bits)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_REGISTER, 0, (int64_t)reg);
    if (operand != NULL) {
        operand->flags = XXBYTE_OPERAND_FLAG_IMPLICIT;
        operand->extra = bits;
    }
}

static void ebpf_add_signed(xxbyte_instruction *instruction, uint32_t size, int64_t value)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, size, value);
    if (operand != NULL) {
        operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
    }
}

static void ebpf_add_index(xxbyte_instruction *instruction, uint32_t kind, uint32_t size, int64_t value)
{
    xxbyte_operand *operand = xxbyte_add_index(instruction, (xxbyte_index_kind)kind, size, value);
    if (operand != NULL) {
        operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
    }
}

static void ebpf_add_memory(xxbyte_instruction *instruction, uint32_t base, int32_t off, uint32_t size_field)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_MEMORY, ebpf_widths[size_field >> 3], off);
    if (operand != NULL) {
        operand->kind = (uint8_t)base;
    }
}

/* A target of address + 8 * (rel + 1), modulo 2^64. */
static void ebpf_add_branch(xxbyte_instruction *instruction, uint32_t size, uint64_t address, int64_t rel)
{
    uint64_t target = address + (uint64_t)(8 * (rel + 1));
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, size, (int64_t)target);
    if (operand != NULL) {
        operand->extra = (uint64_t)rel;
    }
}

static void ebpf_add_operands(const ebpf_fields *f, int32_t next_imm, uint32_t level, uint64_t address, xxbyte_instruction *instruction)
{
    const ebpf_op *op = &ebpf_ops[instruction->id - 1];
    uint32_t size = f->opcode & 0x18u;
    uint32_t cls = f->opcode & 0x07u;
    int x = (f->opcode & EBPF_SRC_X) != 0;
    /* The ALU and JMP32 classes print w registers, as do narrow loads,
     * stores and atomics in the alu32 view (cpu v3 and later). */
    uint32_t alu = cls == EBPF_CLASS_ALU || cls == EBPF_CLASS_JMP32 ? EBPF_W : EBPF_R;
    uint32_t narrow = level >= 3 && size != EBPF_SIZE_DW ? EBPF_W : EBPF_R;

    switch (op->format) {
        case EBPF_FMT_ALU:
            ebpf_add_reg(instruction, f->dst, alu);
            if (x) {
                ebpf_add_reg(instruction, f->src, alu);
            } else {
                ebpf_add_signed(instruction, 4, f->imm);
            }
            break;
        case EBPF_FMT_NEG: ebpf_add_reg(instruction, f->dst, alu); break;
        case EBPF_FMT_MOVSX:
            ebpf_add_reg(instruction, f->dst, alu);
            xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, f->off);
            ebpf_add_reg(instruction, f->src, alu);
            break;
        case EBPF_FMT_ASCAST:
            ebpf_add_reg(instruction, f->dst, EBPF_R);
            ebpf_add_reg(instruction, f->src, EBPF_R);
            xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, (int64_t)(((uint32_t)f->imm >> 16) & 0xFFFFu));
            xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, (int64_t)((uint32_t)f->imm & 0xFFFFu));
            break;
        case EBPF_FMT_END:
            ebpf_add_reg(instruction, f->dst, EBPF_R);
            xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, f->imm);
            break;
        case EBPF_FMT_JA:
            if (cls == EBPF_CLASS_JMP32) {
                ebpf_add_branch(instruction, 4, address, f->imm);
            } else {
                ebpf_add_branch(instruction, 2, address, f->off);
            }
            break;
        case EBPF_FMT_JCC:
            ebpf_add_reg(instruction, f->dst, alu);
            if (x) {
                ebpf_add_reg(instruction, f->src, alu);
            } else {
                ebpf_add_signed(instruction, 4, f->imm);
            }
            ebpf_add_branch(instruction, 2, address, f->off);
            break;
        case EBPF_FMT_CALL: ebpf_add_index(instruction, XXBYTE_EBPF_INDEX_HELPER, 4, f->imm); break;
        case EBPF_FMT_CALL_LOCAL:
        case EBPF_FMT_LD_CODE:
            if (op->format == EBPF_FMT_LD_CODE) {
                ebpf_add_reg(instruction, f->dst, EBPF_R);
            }
            ebpf_add_branch(instruction, 4, address, f->imm);
            break;
        case EBPF_FMT_CALL_KFUNC:
            ebpf_add_index(instruction, XXBYTE_EBPF_INDEX_KFUNC, 4, f->imm);
            if (f->off != 0) {
                ebpf_add_index(instruction, XXBYTE_EBPF_INDEX_BTF_OBJECT, 2, f->off);
            }
            break;
        case EBPF_FMT_REG_JUMP: ebpf_add_reg(instruction, f->dst, EBPF_R); break;
        case EBPF_FMT_EXIT: break;
        case EBPF_FMT_LDDW: {
            uint64_t value = ((uint64_t)(uint32_t)next_imm << 32) | (uint32_t)f->imm;
            xxbyte_operand *operand;
            ebpf_add_reg(instruction, f->dst, EBPF_R);
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 8, (int64_t)value);
            if (operand != NULL) {
                operand->flags = XXBYTE_OPERAND_FLAG_SIGNED | XXBYTE_OPERAND_FLAG_WIDE;
            }
            break;
        }
        case EBPF_FMT_LD_MAP:
        case EBPF_FMT_LD_MAP_VAL:
            ebpf_add_reg(instruction, f->dst, EBPF_R);
            ebpf_add_index(instruction, f->src >= 5 ? XXBYTE_EBPF_INDEX_MAP_IDX : XXBYTE_EBPF_INDEX_MAP_FD, 4, f->imm);
            if (op->format == EBPF_FMT_LD_MAP_VAL) {
                ebpf_add_signed(instruction, 4, next_imm);
            }
            break;
        case EBPF_FMT_LD_VAR:
            ebpf_add_reg(instruction, f->dst, EBPF_R);
            ebpf_add_index(instruction, XXBYTE_EBPF_INDEX_VARIABLE, 4, f->imm);
            if (next_imm != 0) {
                ebpf_add_index(instruction, XXBYTE_EBPF_INDEX_BTF_OBJECT, 4, next_imm);
            }
            break;
        case EBPF_FMT_LD_PACKET:
            ebpf_add_implicit_reg(instruction, 0, EBPF_R);
            ebpf_add_implicit_reg(instruction, 6, EBPF_R);
            if ((f->opcode & 0xE0u) == EBPF_MODE_IND) {
                ebpf_add_reg(instruction, f->src, EBPF_R);
            }
            ebpf_add_signed(instruction, 4, f->imm);
            break;
        case EBPF_FMT_LDX:
            ebpf_add_reg(instruction, f->dst, op->text[0] == 'u' ? narrow : EBPF_R);
            ebpf_add_memory(instruction, f->src, f->off, size);
            break;
        case EBPF_FMT_ST:
            ebpf_add_memory(instruction, f->dst, f->off, size);
            if (cls == EBPF_CLASS_ST) {
                ebpf_add_signed(instruction, 4, f->imm);
            } else {
                ebpf_add_reg(instruction, f->src, narrow);
            }
            break;
        case EBPF_FMT_LOCK:
            ebpf_add_memory(instruction, f->dst, f->off, size);
            ebpf_add_reg(instruction, f->src, narrow);
            break;
        case EBPF_FMT_FETCH:
        case EBPF_FMT_XCHG:
            ebpf_add_reg(instruction, f->src, narrow);
            ebpf_add_memory(instruction, f->dst, f->off, size);
            break;
        case EBPF_FMT_CMPXCHG:
            ebpf_add_implicit_reg(instruction, 0, narrow);
            ebpf_add_memory(instruction, f->dst, f->off, size);
            ebpf_add_reg(instruction, f->src, narrow);
            break;
        case EBPF_FMT_LOAD_ACQ:
            ebpf_add_reg(instruction, f->dst, narrow);
            ebpf_add_memory(instruction, f->src, f->off, size);
            break;
        default: /* EBPF_FMT_STORE_REL */
            ebpf_add_memory(instruction, f->dst, f->off, size);
            ebpf_add_reg(instruction, f->src, narrow);
            break;
    }
}

static xxbyte_status ebpf_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    uint32_t level = ebpf_level(mode);
    uint32_t opcode_level = ebpf_opcode_level(code[0]);
    const xxbyte_ebpf_context *ctx = (const xxbyte_ebpf_context *)context;
    ebpf_fields f;
    ebpf_fields next;
    uint32_t id;

    if (XXBYTE_EBPF_MODE_IS_SBPF(mode)) {
        return xxbyte_ebpf_sbpf_decode(mode, ctx, code, code_size, address, instruction);
    }
    if (opcode_level == 0 || opcode_level > level) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < XXBYTE_EBPF_SLOT_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    ebpf_read(mode, code, &f);
    id = ebpf_classify(&f);
    if (id == 0 || ebpf_ops[id - 1].cpu > level) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    next.imm = 0;
    instruction->size = XXBYTE_EBPF_SLOT_SIZE;
    if (id >= XXBYTE_EBPF_ID_LDDW && id <= XXBYTE_EBPF_ID_LDDW_MAP_IDX_VALUE) {
        if (code_size < 2u * XXBYTE_EBPF_SLOT_SIZE) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        ebpf_read(mode, code + XXBYTE_EBPF_SLOT_SIZE, &next);
        /* The second slot is opcode 0 with zero registers and offset; the
         * pseudo loads that take one immediate leave next_imm zero too. */
        if (next.opcode != 0 || next.dst != 0 || next.src != 0 || next.off != 0 ||
            (next.imm != 0 && (id == XXBYTE_EBPF_ID_LDDW_MAP_FD || id == XXBYTE_EBPF_ID_LDDW_CODE_ADDR || id == XXBYTE_EBPF_ID_LDDW_MAP_IDX))) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        instruction->size = 2u * XXBYTE_EBPF_SLOT_SIZE;
    }
    instruction->opcode = code[0];
    instruction->id = id;
    instruction->group = ebpf_ops[id - 1].group;
    instruction->mnemonic = ebpf_ops[id - 1].mnemonic;
    ebpf_add_operands(&f, next.imm, level, address, instruction);
    if (id == XXBYTE_EBPF_ID_LDDW && ctx != NULL && ctx->lddw_relocated != NULL && ctx->lddw_relocated(ctx->user, address) && instruction->operand_count == 2) {
        /* A symbol relocation patches the immediate: an index a resolver
         * can name (the text without one is the same). */
        instruction->operands[1].type = XXBYTE_OPERAND_INDEX;
        instruction->operands[1].kind = (uint8_t)XXBYTE_EBPF_INDEX_SYMBOL;
        instruction->operands[1].flags = XXBYTE_OPERAND_FLAG_SIGNED;
    }
    return XXBYTE_STATUS_OK;
}

/* Formatting. Every letter the family prints goes through ebpf_put, which
 * uppercases it for XXBYTE_FORMAT_UPPERCASE; numbers are printed apart. */

static int ebpf_put(xxbyte_fmt *fmt, const char *text)
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

/* "0x1f" or "-0x1f". */
static int ebpf_hex(xxbyte_fmt *fmt, int64_t value)
{
    uint64_t magnitude = (uint64_t)value;
    const char *sign = "";

    if (value < 0) {
        magnitude = 0u - magnitude;
        sign = "-";
    }
    return xxbyte_fmt_printf(fmt, "%s0x%llx", sign, (unsigned long long)magnitude);
}

static int ebpf_reg(xxbyte_fmt *fmt, const xxbyte_operand *operand)
{
    return ebpf_put(fmt, operand->extra == EBPF_W ? "w" : "r") && xxbyte_fmt_printf(fmt, "%u", (unsigned)operand->value);
}

/* "r1 + 0x4" or "r10 - 0x8". */
static int ebpf_address(xxbyte_fmt *fmt, const xxbyte_operand *operand)
{
    uint64_t magnitude = (uint64_t)operand->value;
    const char *sign = " + ";

    if (operand->value < 0) {
        magnitude = 0u - magnitude;
        sign = " - ";
    }
    return ebpf_put(fmt, "r") && xxbyte_fmt_printf(fmt, "%u%s0x%llx", (unsigned)operand->kind, sign, (unsigned long long)magnitude);
}

/* "u32", "s16" by access width. */
static int ebpf_type(xxbyte_fmt *fmt, const xxbyte_operand *operand, int sign)
{
    return ebpf_put(fmt, sign ? "s" : "u") && xxbyte_fmt_printf(fmt, "%u", 8u * (unsigned)operand->size);
}

/* "*(u32 *)(r1 + 0x4)", or "(u32 *)(r1 + 0x4)" without the dereference. */
static int ebpf_memory(xxbyte_fmt *fmt, const xxbyte_operand *operand, int sign, int deref)
{
    return (!deref || xxbyte_fmt_char(fmt, '*')) && xxbyte_fmt_char(fmt, '(') && ebpf_type(fmt, operand, sign) && xxbyte_fmt_str(fmt, " *)(") &&
           ebpf_address(fmt, operand) && xxbyte_fmt_char(fmt, ')');
}

static int ebpf_index(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t index)
{
    int resolved = xxbyte_fmt_resolve(fmt, instruction, index);
    if (resolved != 0) {
        return resolved > 0;
    }
    return ebpf_hex(fmt, instruction->operands[index].value);
}

/* The encoded offset ("+0x3", or "0x3" without plus), then the resolved or
 * absolute target in angle brackets. */
static int ebpf_target(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t index, int plus)
{
    const xxbyte_operand *operand = &instruction->operands[index];
    int64_t rel = (int64_t)operand->extra;
    int resolved;

    if ((plus && rel >= 0 && !xxbyte_fmt_char(fmt, '+')) || !ebpf_hex(fmt, rel) || !xxbyte_fmt_str(fmt, " <")) {
        return 0;
    }
    resolved = xxbyte_fmt_resolve(fmt, instruction, index);
    if (resolved < 0 || (resolved == 0 && !xxbyte_fmt_printf(fmt, "0x%llx", (unsigned long long)operand->value))) {
        return 0;
    }
    return xxbyte_fmt_char(fmt, '>');
}

/* " + 0x8" or " - 0x8". */
static int ebpf_addend(xxbyte_fmt *fmt, int64_t value)
{
    uint64_t magnitude = (uint64_t)value;
    const char *sign = " + ";

    if (value < 0) {
        magnitude = 0u - magnitude;
        sign = " - ";
    }
    return xxbyte_fmt_printf(fmt, "%s0x%llx", sign, (unsigned long long)magnitude);
}

/* Operands each format needs, in enum ebpf_format order (the minimum for
 * the optional ones). */
static const uint8_t ebpf_format_operands_min[] = {
    2, 1, 3, 4, 2, 1, 3, 1, 1, 1, 1, 0, 2, 2, 3, 2, 2, 3, 2, 2, 2, 2, 2, 3, 2, 2,
};

typedef char ebpf_formats_complete[sizeof(ebpf_format_operands_min) == EBPF_FMT_STORE_REL + 1 ? 1 : -1];

static int ebpf_has(const xxbyte_instruction *instruction, uint32_t index, xxbyte_operand_type type)
{
    return index < instruction->operand_count && instruction->operands[index].type == (uint8_t)type;
}

static int ebpf_operand_text(const xxbyte_instruction *instruction, uint32_t index, xxbyte_fmt *fmt)
{
    const xxbyte_operand *operand = &instruction->operands[index];

    if (operand->type == XXBYTE_OPERAND_REGISTER) {
        return ebpf_reg(fmt, operand);
    }
    return ebpf_hex(fmt, operand->value);
}

static int ebpf_format_text(const xxbyte_instruction *instruction, const ebpf_op *op, xxbyte_fmt *fmt)
{
    const xxbyte_operand *o = instruction->operands;
    uint32_t n = instruction->operand_count;
    int sign = op->text[0] == 's';

    switch (op->format) {
        case EBPF_FMT_ALU:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_char(fmt, ' ') && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, ' ') && ebpf_operand_text(instruction, 1, fmt);
        case EBPF_FMT_NEG: return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = -") && ebpf_reg(fmt, &o[0]);
        case EBPF_FMT_MOVSX:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = (") && ebpf_put(fmt, "s") && xxbyte_fmt_printf(fmt, "%u)", (unsigned)o[1].value) &&
                   ebpf_reg(fmt, &o[2]);
        case EBPF_FMT_ASCAST:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, '(') && ebpf_reg(fmt, &o[1]) &&
                   xxbyte_fmt_str(fmt, ", ") && ebpf_hex(fmt, o[2].value) && xxbyte_fmt_str(fmt, ", ") && ebpf_hex(fmt, o[3].value) && xxbyte_fmt_char(fmt, ')');
        case EBPF_FMT_END:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_put(fmt, op->text) && xxbyte_fmt_printf(fmt, "%u ", (unsigned)o[1].value) &&
                   ebpf_reg(fmt, &o[0]);
        case EBPF_FMT_JA: return ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, ' ') && ebpf_target(fmt, instruction, 0, 1);
        case EBPF_FMT_JCC:
            return ebpf_put(fmt, "if ") && ebpf_reg(fmt, &o[0]) && xxbyte_fmt_char(fmt, ' ') && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, ' ') &&
                   ebpf_operand_text(instruction, 1, fmt) && ebpf_put(fmt, " goto ") && ebpf_target(fmt, instruction, 2, 1);
        case EBPF_FMT_CALL: return ebpf_put(fmt, "call ") && ebpf_index(fmt, instruction, 0);
        case EBPF_FMT_CALL_LOCAL: return ebpf_put(fmt, "call ") && ebpf_target(fmt, instruction, 0, 0);
        case EBPF_FMT_CALL_KFUNC:
            return ebpf_put(fmt, "call kfunc ") && ebpf_index(fmt, instruction, 0) && (n < 2 || (xxbyte_fmt_str(fmt, ", ") && ebpf_index(fmt, instruction, 1)));
        case EBPF_FMT_REG_JUMP: return ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, ' ') && ebpf_reg(fmt, &o[0]);
        case EBPF_FMT_EXIT: return ebpf_put(fmt, "exit");
        case EBPF_FMT_LDDW:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") &&
                   (o[1].type == XXBYTE_OPERAND_INDEX ? ebpf_index(fmt, instruction, 1) : ebpf_hex(fmt, o[1].value)) && ebpf_put(fmt, " ll");
        case EBPF_FMT_LD_MAP:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, '(') && ebpf_index(fmt, instruction, 1) &&
                   xxbyte_fmt_char(fmt, ')') && ebpf_put(fmt, " ll");
        case EBPF_FMT_LD_MAP_VAL:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_put(fmt, "map_val(") && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, '(') &&
                   ebpf_index(fmt, instruction, 1) && xxbyte_fmt_str(fmt, "))") && ebpf_addend(fmt, o[2].value) && ebpf_put(fmt, " ll");
        case EBPF_FMT_LD_VAR:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, '(') && ebpf_index(fmt, instruction, 1) &&
                   (n < 3 || (xxbyte_fmt_str(fmt, ", ") && ebpf_index(fmt, instruction, 2))) && xxbyte_fmt_char(fmt, ')') && ebpf_put(fmt, " ll");
        case EBPF_FMT_LD_CODE:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, '(') && ebpf_target(fmt, instruction, 1, 1) &&
                   xxbyte_fmt_char(fmt, ')') && ebpf_put(fmt, " ll");
        case EBPF_FMT_LD_PACKET: {
            /* r0 and r6 are implicit; the access width comes from the id. */
            static const char *const types[3] = {"u32", "u16", "u8"};
            uint32_t which = (instruction->id - XXBYTE_EBPF_ID_LDABSW) % 3u;
            if (!ebpf_reg(fmt, &o[0]) || !xxbyte_fmt_str(fmt, " = *(") || !ebpf_put(fmt, types[which]) || !xxbyte_fmt_str(fmt, " *)") || !ebpf_put(fmt, "skb") ||
                !xxbyte_fmt_char(fmt, '[')) {
                return 0;
            }
            if (n == 3) {
                return ebpf_hex(fmt, o[2].value) && xxbyte_fmt_char(fmt, ']');
            }
            return ebpf_reg(fmt, &o[2]) && (o[3].value == 0 || ebpf_addend(fmt, o[3].value)) && xxbyte_fmt_char(fmt, ']');
        }
        case EBPF_FMT_LDX: return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_memory(fmt, &o[1], sign, 1);
        case EBPF_FMT_ST: return ebpf_memory(fmt, &o[0], 0, 1) && xxbyte_fmt_str(fmt, " = ") && ebpf_operand_text(instruction, 1, fmt);
        case EBPF_FMT_LOCK:
            return ebpf_put(fmt, "lock ") && ebpf_memory(fmt, &o[0], 0, 1) && xxbyte_fmt_char(fmt, ' ') && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, ' ') &&
                   ebpf_reg(fmt, &o[1]);
        case EBPF_FMT_FETCH:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, '(') && ebpf_memory(fmt, &o[1], 0, 0) &&
                   xxbyte_fmt_str(fmt, ", ") && ebpf_reg(fmt, &o[0]) && xxbyte_fmt_char(fmt, ')');
        case EBPF_FMT_XCHG:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, '(') && ebpf_address(fmt, &o[1]) &&
                   xxbyte_fmt_str(fmt, ", ") && ebpf_reg(fmt, &o[0]) && xxbyte_fmt_char(fmt, ')');
        case EBPF_FMT_CMPXCHG:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, '(') && ebpf_address(fmt, &o[1]) &&
                   xxbyte_fmt_str(fmt, ", ") && ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, ", ") && ebpf_reg(fmt, &o[2]) && xxbyte_fmt_char(fmt, ')');
        case EBPF_FMT_LOAD_ACQ:
            return ebpf_reg(fmt, &o[0]) && xxbyte_fmt_str(fmt, " = ") && ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, '(') && ebpf_memory(fmt, &o[1], 0, 0) &&
                   xxbyte_fmt_char(fmt, ')');
        default: /* EBPF_FMT_STORE_REL */
            return ebpf_put(fmt, op->text) && xxbyte_fmt_char(fmt, '(') && ebpf_memory(fmt, &o[0], 0, 0) && xxbyte_fmt_str(fmt, ", ") && ebpf_reg(fmt, &o[1]) &&
                   xxbyte_fmt_char(fmt, ')');
    }
}

/* The operand types each format reads, so a hand-made instruction cannot
 * make the formatter read the wrong kind of operand. */
static int ebpf_operands_fit(const xxbyte_instruction *instruction, const ebpf_op *op)
{
    uint32_t i;

    if (instruction->operand_count < ebpf_format_operands_min[op->format] || instruction->operand_count > XXBYTE_MAX_OPERANDS) {
        return 0;
    }
    switch (op->format) {
        case EBPF_FMT_JA:
        case EBPF_FMT_CALL_LOCAL: return ebpf_has(instruction, 0, XXBYTE_OPERAND_BRANCH);
        case EBPF_FMT_JCC: return ebpf_has(instruction, 2, XXBYTE_OPERAND_BRANCH);
        case EBPF_FMT_LD_CODE: return ebpf_has(instruction, 1, XXBYTE_OPERAND_BRANCH);
        case EBPF_FMT_LD_PACKET: return instruction->operand_count == 3 || (instruction->operand_count == 4 && ebpf_has(instruction, 2, XXBYTE_OPERAND_REGISTER));
        case EBPF_FMT_LDX:
        case EBPF_FMT_LOAD_ACQ:
        case EBPF_FMT_FETCH:
        case EBPF_FMT_XCHG:
        case EBPF_FMT_CMPXCHG: return ebpf_has(instruction, 1, XXBYTE_OPERAND_MEMORY);
        case EBPF_FMT_ST:
        case EBPF_FMT_LOCK:
        case EBPF_FMT_STORE_REL: return ebpf_has(instruction, 0, XXBYTE_OPERAND_MEMORY);
        default:
            for (i = 0; i < instruction->operand_count; ++i) {
                if (instruction->operands[i].type == XXBYTE_OPERAND_MEMORY || instruction->operands[i].type == XXBYTE_OPERAND_BRANCH) {
                    return 0;
                }
            }
            return 1;
    }
}

/*
 * The text is a pseudo-C statement without a leading mnemonic, so the
 * mnemonic the core has just appended is dropped first (the core prints it
 * immediately before calling format_operands).
 */
static int ebpf_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const ebpf_op *op;
    size_t mark = xxbyte_fmt_mark(fmt);
    size_t length;

    if (XXBYTE_EBPF_MODE_IS_SBPF(instruction->mode)) {
        return xxbyte_ebpf_sbpf_format(instruction, fmt);
    }
    if (instruction->id == 0 || instruction->id >= XXBYTE_EBPF_ID_COUNT || ebpf_ops[instruction->id - 1].cpu == 0) {
        return 0;
    }
    op = &ebpf_ops[instruction->id - 1];
    length = strlen(instruction->mnemonic);
    if (instruction->mnemonic != op->mnemonic || length > mark || !ebpf_operands_fit(instruction, op)) {
        return 0;
    }
    xxbyte_fmt_rewind(fmt, mark - length);
    return ebpf_format_text(instruction, op, fmt);
}

const char *xxbyte_ebpf_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_EBPF_ID_COUNT) {
        return NULL;
    }
    return ebpf_ops[id - 1].mnemonic;
}

uint32_t xxbyte_ebpf_cpu_version(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_EBPF_ID_COUNT) {
        return 0;
    }
    return ebpf_ops[id - 1].cpu;
}

xxbyte_status xxbyte_ebpf_read_slot(uint32_t mode, const uint8_t *code, size_t code_size, xxbyte_ebpf_slot *slot)
{
    ebpf_fields f;

    if (slot == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(slot, 0, sizeof(*slot));
    if (code == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (ebpf_check_mode(mode) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if (code_size < XXBYTE_EBPF_SLOT_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    ebpf_read(mode, code, &f);
    slot->opcode = (uint8_t)f.opcode;
    slot->dst_reg = (uint8_t)f.dst;
    slot->src_reg = (uint8_t)f.src;
    slot->offset = (int16_t)f.off;
    slot->imm = f.imm;
    return XXBYTE_STATUS_OK;
}

static size_t ebpf_unit_size(uint32_t mode)
{
    (void)mode;
    return 8;
}

const xxbyte_family_ops xxbyte_ebpf_ops = {
    XXBYTE_FAMILY_EBPF, XXBYTE_EBPF_MODE_DEFAULT, ebpf_check_mode, ebpf_decode, NULL, ebpf_format_operands, ebpf_unit_size, xxbyte_ebpf_list_file,
};

#else
typedef int xxbyte_ebpf_disabled;
#endif
