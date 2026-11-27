/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_internal.h"

#if XXBYTE_USE_EVM

#include "xxbyte_evm_internal.h"

#define G_NONE XXBYTE_GROUP_NONE
#define G_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_CALL XXBYTE_GROUP_CALL
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)

/* Operand layouts. */
enum {
    F_NONE,
    F_PUSH0,   /* implicit 0 */
    F_PUSH,    /* 1 to 32 immediate bytes */
    F_DUP,     /* implicit n: duplicates item n (1 = top) */
    F_SWAP,    /* implicit n: swaps the top with item n + 1 */
    F_LOG,     /* implicit topic count */
    F_DUPN,    /* EIP-8024 single-index immediate */
    F_SWAPN,   /* EIP-8024 single-index immediate */
    F_EXCHANGE /* EIP-8024 pair immediate */
};

#define M_FRONTIER XXBYTE_EVM_MODE_FRONTIER
#define M_HOMESTEAD XXBYTE_EVM_MODE_HOMESTEAD
#define M_BYZANTIUM XXBYTE_EVM_MODE_BYZANTIUM
#define M_CONSTANTINOPLE XXBYTE_EVM_MODE_CONSTANTINOPLE
#define M_ISTANBUL XXBYTE_EVM_MODE_ISTANBUL
#define M_LONDON XXBYTE_EVM_MODE_LONDON
#define M_PARIS XXBYTE_EVM_MODE_PARIS
#define M_SHANGHAI XXBYTE_EVM_MODE_SHANGHAI
#define M_CANCUN XXBYTE_EVM_MODE_CANCUN
#define M_OSAKA XXBYTE_EVM_MODE_OSAKA
#define M_AMSTERDAM XXBYTE_EVM_MODE_AMSTERDAM
#define M_LAST XXBYTE_EVM_MODE_NEWEST

#define OP_PUSH1 0x60u
#define OP_DUP1 0x80u
#define OP_SWAP1 0x90u
#define OP_LOG0 0xA0u
#define OP_JUMPDEST 0x5Bu

/* Indexed by id - 1. */
static const char *const mnemonics[XXBYTE_EVM_ID_COUNT - 1] = {
    "STOP",
    "ADD",
    "MUL",
    "SUB",
    "DIV",
    "SDIV",
    "MOD",
    "SMOD",
    "ADDMOD",
    "MULMOD",
    "EXP",
    "SIGNEXTEND",
    "LT",
    "GT",
    "SLT",
    "SGT",
    "EQ",
    "ISZERO",
    "AND",
    "OR",
    "XOR",
    "NOT",
    "BYTE",
    "SHL",
    "SHR",
    "SAR",
    "CLZ",
    "KECCAK256",
    "ADDRESS",
    "BALANCE",
    "ORIGIN",
    "CALLER",
    "CALLVALUE",
    "CALLDATALOAD",
    "CALLDATASIZE",
    "CALLDATACOPY",
    "CODESIZE",
    "CODECOPY",
    "GASPRICE",
    "EXTCODESIZE",
    "EXTCODECOPY",
    "RETURNDATASIZE",
    "RETURNDATACOPY",
    "EXTCODEHASH",
    "BLOCKHASH",
    "COINBASE",
    "TIMESTAMP",
    "NUMBER",
    "DIFFICULTY",
    "PREVRANDAO",
    "GASLIMIT",
    "CHAINID",
    "SELFBALANCE",
    "BASEFEE",
    "BLOBHASH",
    "BLOBBASEFEE",
    "POP",
    "MLOAD",
    "MSTORE",
    "MSTORE8",
    "SLOAD",
    "SSTORE",
    "JUMP",
    "JUMPI",
    "PC",
    "MSIZE",
    "GAS",
    "JUMPDEST",
    "TLOAD",
    "TSTORE",
    "MCOPY",
    "PUSH0",
    "PUSH1",
    "PUSH2",
    "PUSH3",
    "PUSH4",
    "PUSH5",
    "PUSH6",
    "PUSH7",
    "PUSH8",
    "PUSH9",
    "PUSH10",
    "PUSH11",
    "PUSH12",
    "PUSH13",
    "PUSH14",
    "PUSH15",
    "PUSH16",
    "PUSH17",
    "PUSH18",
    "PUSH19",
    "PUSH20",
    "PUSH21",
    "PUSH22",
    "PUSH23",
    "PUSH24",
    "PUSH25",
    "PUSH26",
    "PUSH27",
    "PUSH28",
    "PUSH29",
    "PUSH30",
    "PUSH31",
    "PUSH32",
    "DUP1",
    "DUP2",
    "DUP3",
    "DUP4",
    "DUP5",
    "DUP6",
    "DUP7",
    "DUP8",
    "DUP9",
    "DUP10",
    "DUP11",
    "DUP12",
    "DUP13",
    "DUP14",
    "DUP15",
    "DUP16",
    "SWAP1",
    "SWAP2",
    "SWAP3",
    "SWAP4",
    "SWAP5",
    "SWAP6",
    "SWAP7",
    "SWAP8",
    "SWAP9",
    "SWAP10",
    "SWAP11",
    "SWAP12",
    "SWAP13",
    "SWAP14",
    "SWAP15",
    "SWAP16",
    "LOG0",
    "LOG1",
    "LOG2",
    "LOG3",
    "LOG4",
    "CREATE",
    "CALL",
    "CALLCODE",
    "RETURN",
    "DELEGATECALL",
    "CREATE2",
    "STATICCALL",
    "REVERT",
    "INVALID",
    "SELFDESTRUCT",
    "SLOTNUM",
    "DUPN",
    "SWAPN",
    "EXCHANGE",
};

typedef struct evm_info {
    uint16_t group;  /* XXBYTE_GROUP_* */
    uint8_t form;    /* F_* */
    uint8_t first;   /* the first mode that defines the mnemonic */
    uint8_t last;    /* the last mode that defines it */
    uint8_t inputs;  /* stack items removed (Yellow Paper delta) */
    uint8_t outputs; /* stack items added (alpha) */
} evm_info;

/*
 * Generated from the Ops enums of execution-specs (src/ethereum/forks/
 * <fork>/vm/instructions/__init__.py), plus INVALID (0xFE, EIP-141). Stack
 * effects count the pops and pushes of the execution-specs implementations,
 * cross-checked with evm.codes. Indexed by id - 1.
 */
static const evm_info infos[XXBYTE_EVM_ID_COUNT - 1] = {
    {G_RETURN, F_NONE, M_FRONTIER, M_LAST, 0, 0},     /* STOP */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* ADD */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* MUL */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* SUB */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* DIV */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* SDIV */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* MOD */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* SMOD */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 3, 1},       /* ADDMOD */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 3, 1},       /* MULMOD */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* EXP */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* SIGNEXTEND */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* LT */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* GT */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* SLT */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* SGT */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* EQ */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 1, 1},       /* ISZERO */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* AND */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* OR */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* XOR */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 1, 1},       /* NOT */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* BYTE */
    {G_NONE, F_NONE, M_CONSTANTINOPLE, M_LAST, 2, 1}, /* SHL */
    {G_NONE, F_NONE, M_CONSTANTINOPLE, M_LAST, 2, 1}, /* SHR */
    {G_NONE, F_NONE, M_CONSTANTINOPLE, M_LAST, 2, 1}, /* SAR */
    {G_NONE, F_NONE, M_OSAKA, M_LAST, 1, 1},          /* CLZ */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 1},       /* KECCAK256 */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* ADDRESS */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 1, 1},       /* BALANCE */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* ORIGIN */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* CALLER */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* CALLVALUE */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 1, 1},       /* CALLDATALOAD */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* CALLDATASIZE */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 3, 0},       /* CALLDATACOPY */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* CODESIZE */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 3, 0},       /* CODECOPY */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* GASPRICE */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 1, 1},       /* EXTCODESIZE */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 4, 0},       /* EXTCODECOPY */
    {G_NONE, F_NONE, M_BYZANTIUM, M_LAST, 0, 1},      /* RETURNDATASIZE */
    {G_NONE, F_NONE, M_BYZANTIUM, M_LAST, 3, 0},      /* RETURNDATACOPY */
    {G_NONE, F_NONE, M_CONSTANTINOPLE, M_LAST, 1, 1}, /* EXTCODEHASH */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 1, 1},       /* BLOCKHASH */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* COINBASE */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* TIMESTAMP */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* NUMBER */
    {G_NONE, F_NONE, M_FRONTIER, M_LONDON, 0, 1},     /* DIFFICULTY */
    {G_NONE, F_NONE, M_PARIS, M_LAST, 0, 1},          /* PREVRANDAO */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* GASLIMIT */
    {G_NONE, F_NONE, M_ISTANBUL, M_LAST, 0, 1},       /* CHAINID */
    {G_NONE, F_NONE, M_ISTANBUL, M_LAST, 0, 1},       /* SELFBALANCE */
    {G_NONE, F_NONE, M_LONDON, M_LAST, 0, 1},         /* BASEFEE */
    {G_NONE, F_NONE, M_CANCUN, M_LAST, 1, 1},         /* BLOBHASH */
    {G_NONE, F_NONE, M_CANCUN, M_LAST, 0, 1},         /* BLOBBASEFEE */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 1, 0},       /* POP */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 1, 1},       /* MLOAD */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 0},       /* MSTORE */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 0},       /* MSTORE8 */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 1, 1},       /* SLOAD */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 2, 0},       /* SSTORE */
    {G_JUMP, F_NONE, M_FRONTIER, M_LAST, 1, 0},       /* JUMP */
    {G_COND, F_NONE, M_FRONTIER, M_LAST, 2, 0},       /* JUMPI */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* PC */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* MSIZE */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 1},       /* GAS */
    {G_NONE, F_NONE, M_FRONTIER, M_LAST, 0, 0},       /* JUMPDEST */
    {G_NONE, F_NONE, M_CANCUN, M_LAST, 1, 1},         /* TLOAD */
    {G_NONE, F_NONE, M_CANCUN, M_LAST, 2, 0},         /* TSTORE */
    {G_NONE, F_NONE, M_CANCUN, M_LAST, 3, 0},         /* MCOPY */
    {G_NONE, F_PUSH0, M_SHANGHAI, M_LAST, 0, 1},      /* PUSH0 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH1 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH2 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH3 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH4 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH5 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH6 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH7 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH8 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH9 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH10 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH11 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH12 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH13 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH14 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH15 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH16 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH17 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH18 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH19 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH20 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH21 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH22 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH23 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH24 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH25 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH26 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH27 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH28 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH29 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH30 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH31 */
    {G_NONE, F_PUSH, M_FRONTIER, M_LAST, 0, 1},       /* PUSH32 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 1, 2},        /* DUP1 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 2, 3},        /* DUP2 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 3, 4},        /* DUP3 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 4, 5},        /* DUP4 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 5, 6},        /* DUP5 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 6, 7},        /* DUP6 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 7, 8},        /* DUP7 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 8, 9},        /* DUP8 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 9, 10},       /* DUP9 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 10, 11},      /* DUP10 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 11, 12},      /* DUP11 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 12, 13},      /* DUP12 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 13, 14},      /* DUP13 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 14, 15},      /* DUP14 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 15, 16},      /* DUP15 */
    {G_NONE, F_DUP, M_FRONTIER, M_LAST, 16, 17},      /* DUP16 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 2, 2},       /* SWAP1 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 3, 3},       /* SWAP2 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 4, 4},       /* SWAP3 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 5, 5},       /* SWAP4 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 6, 6},       /* SWAP5 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 7, 7},       /* SWAP6 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 8, 8},       /* SWAP7 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 9, 9},       /* SWAP8 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 10, 10},     /* SWAP9 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 11, 11},     /* SWAP10 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 12, 12},     /* SWAP11 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 13, 13},     /* SWAP12 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 14, 14},     /* SWAP13 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 15, 15},     /* SWAP14 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 16, 16},     /* SWAP15 */
    {G_NONE, F_SWAP, M_FRONTIER, M_LAST, 17, 17},     /* SWAP16 */
    {G_NONE, F_LOG, M_FRONTIER, M_LAST, 2, 0},        /* LOG0 */
    {G_NONE, F_LOG, M_FRONTIER, M_LAST, 3, 0},        /* LOG1 */
    {G_NONE, F_LOG, M_FRONTIER, M_LAST, 4, 0},        /* LOG2 */
    {G_NONE, F_LOG, M_FRONTIER, M_LAST, 5, 0},        /* LOG3 */
    {G_NONE, F_LOG, M_FRONTIER, M_LAST, 6, 0},        /* LOG4 */
    {G_CALL, F_NONE, M_FRONTIER, M_LAST, 3, 1},       /* CREATE */
    {G_CALL, F_NONE, M_FRONTIER, M_LAST, 7, 1},       /* CALL */
    {G_CALL, F_NONE, M_FRONTIER, M_LAST, 7, 1},       /* CALLCODE */
    {G_RETURN, F_NONE, M_FRONTIER, M_LAST, 2, 0},     /* RETURN */
    {G_CALL, F_NONE, M_HOMESTEAD, M_LAST, 6, 1},      /* DELEGATECALL */
    {G_CALL, F_NONE, M_CONSTANTINOPLE, M_LAST, 4, 1}, /* CREATE2 */
    {G_CALL, F_NONE, M_BYZANTIUM, M_LAST, 6, 1},      /* STATICCALL */
    {G_THROW, F_NONE, M_BYZANTIUM, M_LAST, 2, 0},     /* REVERT */
    {G_THROW, F_NONE, M_FRONTIER, M_LAST, 0, 0},      /* INVALID */
    {G_RETURN, F_NONE, M_FRONTIER, M_LAST, 1, 0},     /* SELFDESTRUCT */
    {G_NONE, F_NONE, M_AMSTERDAM, M_LAST, 0, 1},      /* SLOTNUM */
    {G_NONE, F_DUPN, M_AMSTERDAM, M_LAST, 0, 0},      /* DUPN */
    {G_NONE, F_SWAPN, M_AMSTERDAM, M_LAST, 0, 0},     /* SWAPN */
    {G_NONE, F_EXCHANGE, M_AMSTERDAM, M_LAST, 0, 0},  /* EXCHANGE */
};

/* The ids an opcode can have, two where a fork renamed it. */
static const uint8_t opcode_ids[256][2] = {
    [0x00] = {XXBYTE_EVM_ID_STOP},
    [0x01] = {XXBYTE_EVM_ID_ADD},
    [0x02] = {XXBYTE_EVM_ID_MUL},
    [0x03] = {XXBYTE_EVM_ID_SUB},
    [0x04] = {XXBYTE_EVM_ID_DIV},
    [0x05] = {XXBYTE_EVM_ID_SDIV},
    [0x06] = {XXBYTE_EVM_ID_MOD},
    [0x07] = {XXBYTE_EVM_ID_SMOD},
    [0x08] = {XXBYTE_EVM_ID_ADDMOD},
    [0x09] = {XXBYTE_EVM_ID_MULMOD},
    [0x0A] = {XXBYTE_EVM_ID_EXP},
    [0x0B] = {XXBYTE_EVM_ID_SIGNEXTEND},
    [0x10] = {XXBYTE_EVM_ID_LT},
    [0x11] = {XXBYTE_EVM_ID_GT},
    [0x12] = {XXBYTE_EVM_ID_SLT},
    [0x13] = {XXBYTE_EVM_ID_SGT},
    [0x14] = {XXBYTE_EVM_ID_EQ},
    [0x15] = {XXBYTE_EVM_ID_ISZERO},
    [0x16] = {XXBYTE_EVM_ID_AND},
    [0x17] = {XXBYTE_EVM_ID_OR},
    [0x18] = {XXBYTE_EVM_ID_XOR},
    [0x19] = {XXBYTE_EVM_ID_NOT},
    [0x1A] = {XXBYTE_EVM_ID_BYTE},
    [0x1B] = {XXBYTE_EVM_ID_SHL},
    [0x1C] = {XXBYTE_EVM_ID_SHR},
    [0x1D] = {XXBYTE_EVM_ID_SAR},
    [0x1E] = {XXBYTE_EVM_ID_CLZ},
    [0x20] = {XXBYTE_EVM_ID_KECCAK256},
    [0x30] = {XXBYTE_EVM_ID_ADDRESS},
    [0x31] = {XXBYTE_EVM_ID_BALANCE},
    [0x32] = {XXBYTE_EVM_ID_ORIGIN},
    [0x33] = {XXBYTE_EVM_ID_CALLER},
    [0x34] = {XXBYTE_EVM_ID_CALLVALUE},
    [0x35] = {XXBYTE_EVM_ID_CALLDATALOAD},
    [0x36] = {XXBYTE_EVM_ID_CALLDATASIZE},
    [0x37] = {XXBYTE_EVM_ID_CALLDATACOPY},
    [0x38] = {XXBYTE_EVM_ID_CODESIZE},
    [0x39] = {XXBYTE_EVM_ID_CODECOPY},
    [0x3A] = {XXBYTE_EVM_ID_GASPRICE},
    [0x3B] = {XXBYTE_EVM_ID_EXTCODESIZE},
    [0x3C] = {XXBYTE_EVM_ID_EXTCODECOPY},
    [0x3D] = {XXBYTE_EVM_ID_RETURNDATASIZE},
    [0x3E] = {XXBYTE_EVM_ID_RETURNDATACOPY},
    [0x3F] = {XXBYTE_EVM_ID_EXTCODEHASH},
    [0x40] = {XXBYTE_EVM_ID_BLOCKHASH},
    [0x41] = {XXBYTE_EVM_ID_COINBASE},
    [0x42] = {XXBYTE_EVM_ID_TIMESTAMP},
    [0x43] = {XXBYTE_EVM_ID_NUMBER},
    [0x44] = {XXBYTE_EVM_ID_DIFFICULTY, XXBYTE_EVM_ID_PREVRANDAO},
    [0x45] = {XXBYTE_EVM_ID_GASLIMIT},
    [0x46] = {XXBYTE_EVM_ID_CHAINID},
    [0x47] = {XXBYTE_EVM_ID_SELFBALANCE},
    [0x48] = {XXBYTE_EVM_ID_BASEFEE},
    [0x49] = {XXBYTE_EVM_ID_BLOBHASH},
    [0x4A] = {XXBYTE_EVM_ID_BLOBBASEFEE},
    [0x4B] = {XXBYTE_EVM_ID_SLOTNUM},
    [0x50] = {XXBYTE_EVM_ID_POP},
    [0x51] = {XXBYTE_EVM_ID_MLOAD},
    [0x52] = {XXBYTE_EVM_ID_MSTORE},
    [0x53] = {XXBYTE_EVM_ID_MSTORE8},
    [0x54] = {XXBYTE_EVM_ID_SLOAD},
    [0x55] = {XXBYTE_EVM_ID_SSTORE},
    [0x56] = {XXBYTE_EVM_ID_JUMP},
    [0x57] = {XXBYTE_EVM_ID_JUMPI},
    [0x58] = {XXBYTE_EVM_ID_PC},
    [0x59] = {XXBYTE_EVM_ID_MSIZE},
    [0x5A] = {XXBYTE_EVM_ID_GAS},
    [0x5B] = {XXBYTE_EVM_ID_JUMPDEST},
    [0x5C] = {XXBYTE_EVM_ID_TLOAD},
    [0x5D] = {XXBYTE_EVM_ID_TSTORE},
    [0x5E] = {XXBYTE_EVM_ID_MCOPY},
    [0x5F] = {XXBYTE_EVM_ID_PUSH0},
    [0x60] = {XXBYTE_EVM_ID_PUSH1},
    [0x61] = {XXBYTE_EVM_ID_PUSH2},
    [0x62] = {XXBYTE_EVM_ID_PUSH3},
    [0x63] = {XXBYTE_EVM_ID_PUSH4},
    [0x64] = {XXBYTE_EVM_ID_PUSH5},
    [0x65] = {XXBYTE_EVM_ID_PUSH6},
    [0x66] = {XXBYTE_EVM_ID_PUSH7},
    [0x67] = {XXBYTE_EVM_ID_PUSH8},
    [0x68] = {XXBYTE_EVM_ID_PUSH9},
    [0x69] = {XXBYTE_EVM_ID_PUSH10},
    [0x6A] = {XXBYTE_EVM_ID_PUSH11},
    [0x6B] = {XXBYTE_EVM_ID_PUSH12},
    [0x6C] = {XXBYTE_EVM_ID_PUSH13},
    [0x6D] = {XXBYTE_EVM_ID_PUSH14},
    [0x6E] = {XXBYTE_EVM_ID_PUSH15},
    [0x6F] = {XXBYTE_EVM_ID_PUSH16},
    [0x70] = {XXBYTE_EVM_ID_PUSH17},
    [0x71] = {XXBYTE_EVM_ID_PUSH18},
    [0x72] = {XXBYTE_EVM_ID_PUSH19},
    [0x73] = {XXBYTE_EVM_ID_PUSH20},
    [0x74] = {XXBYTE_EVM_ID_PUSH21},
    [0x75] = {XXBYTE_EVM_ID_PUSH22},
    [0x76] = {XXBYTE_EVM_ID_PUSH23},
    [0x77] = {XXBYTE_EVM_ID_PUSH24},
    [0x78] = {XXBYTE_EVM_ID_PUSH25},
    [0x79] = {XXBYTE_EVM_ID_PUSH26},
    [0x7A] = {XXBYTE_EVM_ID_PUSH27},
    [0x7B] = {XXBYTE_EVM_ID_PUSH28},
    [0x7C] = {XXBYTE_EVM_ID_PUSH29},
    [0x7D] = {XXBYTE_EVM_ID_PUSH30},
    [0x7E] = {XXBYTE_EVM_ID_PUSH31},
    [0x7F] = {XXBYTE_EVM_ID_PUSH32},
    [0x80] = {XXBYTE_EVM_ID_DUP1},
    [0x81] = {XXBYTE_EVM_ID_DUP2},
    [0x82] = {XXBYTE_EVM_ID_DUP3},
    [0x83] = {XXBYTE_EVM_ID_DUP4},
    [0x84] = {XXBYTE_EVM_ID_DUP5},
    [0x85] = {XXBYTE_EVM_ID_DUP6},
    [0x86] = {XXBYTE_EVM_ID_DUP7},
    [0x87] = {XXBYTE_EVM_ID_DUP8},
    [0x88] = {XXBYTE_EVM_ID_DUP9},
    [0x89] = {XXBYTE_EVM_ID_DUP10},
    [0x8A] = {XXBYTE_EVM_ID_DUP11},
    [0x8B] = {XXBYTE_EVM_ID_DUP12},
    [0x8C] = {XXBYTE_EVM_ID_DUP13},
    [0x8D] = {XXBYTE_EVM_ID_DUP14},
    [0x8E] = {XXBYTE_EVM_ID_DUP15},
    [0x8F] = {XXBYTE_EVM_ID_DUP16},
    [0x90] = {XXBYTE_EVM_ID_SWAP1},
    [0x91] = {XXBYTE_EVM_ID_SWAP2},
    [0x92] = {XXBYTE_EVM_ID_SWAP3},
    [0x93] = {XXBYTE_EVM_ID_SWAP4},
    [0x94] = {XXBYTE_EVM_ID_SWAP5},
    [0x95] = {XXBYTE_EVM_ID_SWAP6},
    [0x96] = {XXBYTE_EVM_ID_SWAP7},
    [0x97] = {XXBYTE_EVM_ID_SWAP8},
    [0x98] = {XXBYTE_EVM_ID_SWAP9},
    [0x99] = {XXBYTE_EVM_ID_SWAP10},
    [0x9A] = {XXBYTE_EVM_ID_SWAP11},
    [0x9B] = {XXBYTE_EVM_ID_SWAP12},
    [0x9C] = {XXBYTE_EVM_ID_SWAP13},
    [0x9D] = {XXBYTE_EVM_ID_SWAP14},
    [0x9E] = {XXBYTE_EVM_ID_SWAP15},
    [0x9F] = {XXBYTE_EVM_ID_SWAP16},
    [0xA0] = {XXBYTE_EVM_ID_LOG0},
    [0xA1] = {XXBYTE_EVM_ID_LOG1},
    [0xA2] = {XXBYTE_EVM_ID_LOG2},
    [0xA3] = {XXBYTE_EVM_ID_LOG3},
    [0xA4] = {XXBYTE_EVM_ID_LOG4},
    [0xE6] = {XXBYTE_EVM_ID_DUPN},
    [0xE7] = {XXBYTE_EVM_ID_SWAPN},
    [0xE8] = {XXBYTE_EVM_ID_EXCHANGE},
    [0xF0] = {XXBYTE_EVM_ID_CREATE},
    [0xF1] = {XXBYTE_EVM_ID_CALL},
    [0xF2] = {XXBYTE_EVM_ID_CALLCODE},
    [0xF3] = {XXBYTE_EVM_ID_RETURN},
    [0xF4] = {XXBYTE_EVM_ID_DELEGATECALL},
    [0xF5] = {XXBYTE_EVM_ID_CREATE2},
    [0xFA] = {XXBYTE_EVM_ID_STATICCALL},
    [0xFD] = {XXBYTE_EVM_ID_REVERT},
    [0xFE] = {XXBYTE_EVM_ID_INVALID},
    [0xFF] = {XXBYTE_EVM_ID_SELFDESTRUCT},
};

/* Indexed by mode; mode 0 is the newest final fork. */
static const char *const mode_names[XXBYTE_EVM_MODE_NEWEST + 1] = {
    "osaka",  "frontier", "homestead", "tangerine_whistle", "spurious_dragon", "byzantium", "constantinople", "istanbul",
    "berlin", "london",   "paris",     "shanghai",          "cancun",          "prague",    "osaka",          "amsterdam",
};

static const struct {
    const char *name;
    uint8_t mode;
} mode_aliases[] = {
    {"default", XXBYTE_EVM_MODE_DEFAULT},
    {"latest", XXBYTE_EVM_MODE_DEFAULT},
    {"dao_fork", XXBYTE_EVM_MODE_DAO_FORK},
    {"petersburg", XXBYTE_EVM_MODE_PETERSBURG},
    {"muir_glacier", XXBYTE_EVM_MODE_MUIR_GLACIER},
    {"arrow_glacier", XXBYTE_EVM_MODE_ARROW_GLACIER},
    {"gray_glacier", XXBYTE_EVM_MODE_GRAY_GLACIER},
    {"merge", XXBYTE_EVM_MODE_PARIS},
    {"shapella", XXBYTE_EVM_MODE_SHANGHAI},
    {"dencun", XXBYTE_EVM_MODE_CANCUN},
    {"pectra", XXBYTE_EVM_MODE_PRAGUE},
    {"fusaka", XXBYTE_EVM_MODE_OSAKA},
    {"bpo1", XXBYTE_EVM_MODE_BPO1},
    {"bpo2", XXBYTE_EVM_MODE_BPO2},
    {"bpo3", XXBYTE_EVM_MODE_BPO3},
    {"bpo4", XXBYTE_EVM_MODE_BPO4},
    {"bpo5", XXBYTE_EVM_MODE_BPO5},
    {"glamsterdam", XXBYTE_EVM_MODE_AMSTERDAM},
    {"bogota", XXBYTE_EVM_MODE_BOGOTA},
};

static xxbyte_status evm_check_mode(uint32_t mode)
{
    return mode <= XXBYTE_EVM_MODE_NEWEST ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static const evm_info *info_of(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_EVM_ID_COUNT) {
        return NULL;
    }
    return &infos[id - 1];
}

/* The id of opcode in a supported mode, 0 when the mode leaves it
 * undefined. */
static uint32_t lookup(uint32_t mode, uint8_t opcode)
{
    uint32_t fork = mode == XXBYTE_EVM_MODE_DEFAULT ? XXBYTE_EVM_MODE_LATEST_FINAL : mode;
    uint32_t i;

    for (i = 0; i < 2; ++i) {
        uint32_t id = opcode_ids[opcode][i];
        if (id != 0 && infos[id - 1].first <= fork && fork <= infos[id - 1].last) {
            return id;
        }
    }
    return 0;
}

/* EIP-8024 decode_single: 17..235, or 0 for the forbidden 91..127. */
static uint32_t decode_single(uint8_t x)
{
    if (x > 90u && x < 128u) {
        return 0;
    }
    return (x + 145u) & 0xFFu;
}

/* EIP-8024 decode_pair: 1 <= n < m <= 30 - n; 0 for the forbidden
 * 82..127. */
static int decode_pair(uint8_t x, uint32_t *n, uint32_t *m)
{
    uint32_t k;
    uint32_t q;
    uint32_t r;

    if (x > 81u && x < 128u) {
        return 0;
    }
    k = (uint32_t)x ^ 143u;
    q = k >> 4;
    r = k & 15u;
    if (q < r) {
        *n = q + 1u;
        *m = r + 1u;
    } else {
        *n = r + 1u;
        *m = 29u - q;
    }
    return 1;
}

/* An unsigned 64-bit pattern as int64_t without implementation-defined
 * conversion. */
static int64_t as_signed(uint64_t value)
{
    if (value <= (uint64_t)INT64_MAX) {
        return (int64_t)value;
    }
    return -(int64_t)(~value) - 1;
}

static void add_number(xxbyte_instruction *instruction, uint32_t size, int64_t value, uint64_t extra, uint16_t flags)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, size, value);
    if (operand != NULL) {
        operand->extra = extra;
        operand->flags = flags;
    }
}

static xxbyte_status evm_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const uint16_t implicit = XXBYTE_OPERAND_FLAG_IMPLICIT;
    uint32_t id = lookup(mode, code[0]);
    const evm_info *info;
    uint32_t size = 1;
    uint32_t n = 0;
    uint32_t m = 0;

    (void)context;
    (void)address;
    if (id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    info = &infos[id - 1];
    if (info->form == F_PUSH) {
        n = (uint32_t)code[0] - OP_PUSH1 + 1u;
        size = 1u + n;
    } else if (info->form == F_DUPN || info->form == F_SWAPN || info->form == F_EXCHANGE) {
        size = 2;
    }
    if (code_size < size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (info->form == F_DUPN || info->form == F_SWAPN) {
        n = decode_single(code[1]);
        if (n == 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    } else if (info->form == F_EXCHANGE && !decode_pair(code[1], &n, &m)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->size = size;
    instruction->opcode = code[0];
    instruction->id = id;
    instruction->group = info->group;
    instruction->mnemonic = mnemonics[id - 1];

    switch (info->form) {
        case F_PUSH0: add_number(instruction, 0, 0, 0, implicit); break;
        case F_PUSH:
            if (n <= 8u) {
                uint64_t value = 0;
                uint32_t i;
                for (i = 1; i <= n; ++i) {
                    value = (value << 8) | code[i];
                }
                add_number(instruction, n, as_signed(value), 0, 0);
            } else {
                xxbyte_add_operand(instruction, XXBYTE_OPERAND_BYTES, n, 1);
            }
            break;
        case F_DUP: add_number(instruction, 0, (int64_t)code[0] - (int64_t)OP_DUP1 + 1, 0, implicit); break;
        case F_SWAP: add_number(instruction, 0, (int64_t)code[0] - (int64_t)OP_SWAP1 + 1, 0, implicit); break;
        case F_LOG: add_number(instruction, 0, (int64_t)code[0] - (int64_t)OP_LOG0, 0, implicit); break;
        case F_DUPN:
        case F_SWAPN: add_number(instruction, 1, (int64_t)n, code[1], 0); break;
        case F_EXCHANGE:
            add_number(instruction, 1, (int64_t)n, code[1], 0);
            add_number(instruction, 1, (int64_t)m, code[1], 0);
            break;
        default: break;
    }
    return XXBYTE_STATUS_OK;
}

/* "0x" and two lowercase hex digits per immediate byte. */
static int format_push(const xxbyte_instruction *instruction, uint32_t index, xxbyte_fmt *fmt)
{
    static const char digits[] = "0123456789abcdef";
    const xxbyte_operand *operand = &instruction->operands[index];
    char text[2 + 2 * 32 + 1];
    size_t length = 2;
    uint32_t i;
    int resolved = xxbyte_fmt_resolve(fmt, instruction, index);

    if (resolved != 0) {
        return resolved > 0;
    }
    if (operand->size == 0 || operand->size > 32) {
        return 0;
    }
    text[0] = '0';
    text[1] = 'x';
    for (i = 0; i < operand->size; ++i) {
        uint32_t byte;
        if (operand->type == XXBYTE_OPERAND_IMMEDIATE) {
            uint32_t shift = 8u * (operand->size - 1u - i);
            if (shift >= 64u) {
                return 0;
            }
            byte = (uint32_t)(((uint64_t)operand->value >> shift) & 0xFFu);
        } else {
            if (instruction->code == NULL || operand->value < 0 || (uint64_t)operand->value + operand->size > instruction->size) {
                return 0;
            }
            byte = instruction->code[(size_t)operand->value + i];
        }
        text[length++] = digits[byte >> 4];
        text[length++] = digits[byte & 15u];
    }
    text[length] = '\0';
    return xxbyte_fmt_str(fmt, text);
}

static int evm_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const evm_info *info = info_of(instruction->id);
    uint32_t i;

    for (i = 0; i < instruction->operand_count; ++i) {
        const xxbyte_operand *operand = &instruction->operands[i];
        if ((operand->flags & XXBYTE_OPERAND_FLAG_IMPLICIT) != 0) {
            continue;
        }
        if (!xxbyte_fmt_char(fmt, ' ')) {
            return 0;
        }
        if (info != NULL && info->form == F_PUSH && i == 0) {
            if (!format_push(instruction, i, fmt)) {
                return 0;
            }
        } else if (!xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value)) {
            return 0;
        }
    }
    return 1;
}

const xxbyte_family_ops xxbyte_evm_ops = {
    XXBYTE_FAMILY_EVM, XXBYTE_EVM_MODE_DEFAULT, evm_check_mode, evm_decode, NULL, evm_format_operands, NULL, xxbyte_evm_list_file,
};

const char *xxbyte_evm_mnemonic(uint32_t id)
{
    return info_of(id) != NULL ? mnemonics[id - 1] : NULL;
}

const char *xxbyte_evm_opcode_name(uint32_t mode, uint8_t opcode)
{
    uint32_t id;

    if (evm_check_mode(mode) != XXBYTE_STATUS_OK) {
        return NULL;
    }
    id = lookup(mode, opcode);
    return id != 0 ? mnemonics[id - 1] : NULL;
}

xxbyte_status xxbyte_evm_id_forks(uint32_t id, uint32_t *first_mode, uint32_t *last_mode)
{
    const evm_info *info = info_of(id);

    if (info == NULL || first_mode == NULL || last_mode == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *first_mode = info->first;
    *last_mode = info->last;
    return XXBYTE_STATUS_OK;
}

const char *xxbyte_evm_mode_name(uint32_t mode)
{
    return evm_check_mode(mode) == XXBYTE_STATUS_OK ? mode_names[mode] : NULL;
}

/* Case-insensitive, with '-' and ' ' matching '_'. */
static int same_name(const char *a, const char *b)
{
    for (;; ++a, ++b) {
        int x = (unsigned char)*a;
        int y = (unsigned char)*b;
        if (x >= 'A' && x <= 'Z') {
            x += 'a' - 'A';
        }
        if (y >= 'A' && y <= 'Z') {
            y += 'a' - 'A';
        }
        if (x == '-' || x == ' ') {
            x = '_';
        }
        if (y == '-' || y == ' ') {
            y = '_';
        }
        if (x != y) {
            return 0;
        }
        if (x == '\0') {
            return 1;
        }
    }
}

xxbyte_status xxbyte_evm_mode_from_name(const char *name, uint32_t *mode)
{
    uint32_t i;

    if (name == NULL || mode == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    for (i = XXBYTE_EVM_MODE_FRONTIER; i <= XXBYTE_EVM_MODE_NEWEST; ++i) {
        if (same_name(name, mode_names[i])) {
            *mode = i;
            return XXBYTE_STATUS_OK;
        }
    }
    for (i = 0; i < sizeof(mode_aliases) / sizeof(mode_aliases[0]); ++i) {
        if (same_name(name, mode_aliases[i].name)) {
            *mode = mode_aliases[i].mode;
            return XXBYTE_STATUS_OK;
        }
    }
    return XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static const evm_info *decoded_info(const xxbyte_instruction *instruction)
{
    if (instruction == NULL || instruction->family != XXBYTE_FAMILY_EVM || instruction->size == 0) {
        return NULL;
    }
    return info_of(instruction->id);
}

xxbyte_status xxbyte_evm_stack_effect(const xxbyte_instruction *instruction, uint32_t *inputs, uint32_t *outputs)
{
    const evm_info *info = decoded_info(instruction);
    uint32_t operand = 0;
    int64_t depth;

    if (info == NULL || inputs == NULL || outputs == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (info->form == F_DUPN || info->form == F_SWAPN || info->form == F_EXCHANGE) {
        /* The deepest item touched: n for DUPN and SWAPN, m for EXCHANGE. */
        if (info->form == F_EXCHANGE) {
            operand = 1;
        }
        if (instruction->operand_count <= operand) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
        depth = instruction->operands[operand].value;
        if (depth < 1 || depth > 255) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
        *inputs = (uint32_t)depth + (info->form == F_DUPN ? 0u : 1u);
        *outputs = (uint32_t)depth + 1u;
        return XXBYTE_STATUS_OK;
    }
    *inputs = info->inputs;
    *outputs = info->outputs;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_evm_push_value(const xxbyte_instruction *instruction, uint8_t value[32])
{
    const evm_info *info = decoded_info(instruction);
    const xxbyte_operand *operand;
    uint32_t size;
    uint32_t i;

    if (info == NULL || value == NULL || (info->form != F_PUSH0 && info->form != F_PUSH)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->operand_count != 1) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    operand = &instruction->operands[0];
    size = operand->size;
    if (info->form == F_PUSH0) {
        if (operand->type != XXBYTE_OPERAND_IMMEDIATE || operand->value != 0) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
        size = 0;
    } else if (operand->type == XXBYTE_OPERAND_IMMEDIATE) {
        if (size == 0 || size > 8) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
    } else if (operand->type != XXBYTE_OPERAND_BYTES || size == 0 || size > 32 || instruction->code == NULL || operand->value < 0 ||
               (uint64_t)operand->value + size > instruction->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(value, 0, 32);
    for (i = 0; i < size; ++i) {
        uint8_t byte;
        if (operand->type == XXBYTE_OPERAND_IMMEDIATE) {
            uint32_t shift = 8u * (size - 1u - i);
            byte = (uint8_t)((uint64_t)operand->value >> shift);
        } else {
            byte = instruction->code[(size_t)operand->value + i];
        }
        value[32u - size + i] = byte;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_evm_jump_destinations(uint32_t mode, const uint8_t *code, size_t code_size, uint8_t *bitmap, size_t bitmap_size, size_t *count)
{
    size_t pc = 0;
    size_t found = 0;
    size_t needed = code_size / 8u + ((code_size & 7u) != 0 ? 1u : 0u);

    if (count != NULL) {
        *count = 0;
    }
    if (evm_check_mode(mode) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if ((code == NULL && code_size != 0) || (bitmap == NULL && bitmap_size != 0) || (bitmap != NULL && bitmap_size < needed)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (bitmap != NULL) {
        memset(bitmap, 0, needed);
    }
    while (pc < code_size) {
        uint8_t op = code[pc];
        if (op == OP_JUMPDEST) {
            if (bitmap != NULL) {
                bitmap[pc >> 3] = (uint8_t)(bitmap[pc >> 3] | (1u << (pc & 7u)));
            }
            ++found;
        } else if (op >= OP_PUSH1 && op <= OP_PUSH1 + 31u) {
            size_t data = (size_t)op - OP_PUSH1 + 1u;
            if (data >= code_size - pc) {
                break;
            }
            pc += data;
        }
        ++pc;
    }
    if (count != NULL) {
        *count = found;
    }
    return XXBYTE_STATUS_OK;
}

/*
 * A minimal CBOR (RFC 8949) reader for the metadata: definite lengths only,
 * no tags. Every item takes at least one byte and nesting is bounded, so
 * the work is linear in the input.
 */
#define CBOR_MAX_DEPTH 16

typedef struct cbor_item {
    uint32_t major;
    uint64_t argument;
    size_t start; /* the head */
    size_t data;  /* after the head: the bytes of a string, the first
                     element of an array or map */
} cbor_item;

static int cbor_head(const uint8_t *p, size_t end, size_t *pos, cbor_item *item)
{
    uint32_t info;
    uint32_t i;
    uint32_t bytes;

    if (*pos >= end) {
        return 0;
    }
    item->start = *pos;
    item->major = (uint32_t)p[*pos] >> 5;
    info = (uint32_t)p[*pos] & 31u;
    ++*pos;
    item->argument = info;
    if (info >= 24u) {
        if (info > 27u) {
            return 0; /* reserved or indefinite length */
        }
        bytes = 1u << (info - 24u);
        if (end - *pos < bytes) {
            return 0;
        }
        item->argument = 0;
        for (i = 0; i < bytes; ++i) {
            item->argument = (item->argument << 8) | p[*pos + i];
        }
        *pos += bytes;
    }
    item->data = *pos;
    return item->major != 6u; /* tags are not used by the compilers */
}

static int cbor_skip(const uint8_t *p, size_t end, size_t *pos, int depth)
{
    cbor_item item;
    uint64_t count;
    uint64_t i;

    if (depth > CBOR_MAX_DEPTH || !cbor_head(p, end, pos, &item)) {
        return 0;
    }
    switch (item.major) {
        case 2:
        case 3:
            if (item.argument > end - *pos) {
                return 0;
            }
            *pos += (size_t)item.argument;
            return 1;
        case 4:
        case 5:
            count = item.argument;
            if (count > end - *pos || (item.major == 5u && count > (end - *pos) / 2u)) {
                return 0;
            }
            if (item.major == 5u) {
                count *= 2u;
            }
            for (i = 0; i < count; ++i) {
                if (!cbor_skip(p, end, pos, depth + 1)) {
                    return 0;
                }
            }
            return 1;
        default: return 1; /* integers, simple values and floats */
    }
}

static int text_is(const uint8_t *p, const cbor_item *key, const char *text)
{
    size_t length = strlen(text);
    return key->major == 3u && key->argument == length && memcmp(p + key->data, text, length) == 0;
}

/* The keys whose value is a source digest; the first one found is kept. */
static const struct {
    const char *name;
    uint32_t flag;
} digest_keys[] = {
    {"ipfs", XXBYTE_EVM_METADATA_IPFS},
    {"bzzr1", XXBYTE_EVM_METADATA_BZZR1},
    {"bzzr0", XXBYTE_EVM_METADATA_BZZR0},
};

static uint32_t digest_flag(const uint8_t *p, const cbor_item *key)
{
    size_t i;

    for (i = 0; i < sizeof(digest_keys) / sizeof(digest_keys[0]); ++i) {
        if (text_is(p, key, digest_keys[i].name)) {
            return digest_keys[i].flag;
        }
    }
    return 0;
}

/* A 3-element array of small unsigned integers, as major << 16 | minor << 8
 * | patch; 0 otherwise. */
static uint32_t version_triple(const uint8_t *p, size_t end, size_t pos)
{
    cbor_item item;
    uint32_t version = 0;
    uint32_t i;

    if (!cbor_head(p, end, &pos, &item) || item.major != 4u || item.argument != 3u) {
        return 0;
    }
    for (i = 0; i < 3; ++i) {
        if (!cbor_head(p, end, &pos, &item) || item.major != 0u || item.argument > 255u) {
            return 0;
        }
        version = (version << 8) | (uint32_t)item.argument;
    }
    return version;
}

/* A map with text keys and at least one entry, starting at *pos. Records the
 * known keys in metadata. */
static int parse_map(const uint8_t *p, size_t end, size_t *pos, int depth, xxbyte_evm_metadata *metadata)
{
    cbor_item map;
    uint64_t i;

    if (!cbor_head(p, end, pos, &map) || map.major != 5u || map.argument == 0 || map.argument > (end - *pos) / 2u) {
        return 0;
    }
    for (i = 0; i < map.argument; ++i) {
        cbor_item key;
        cbor_item value;
        size_t value_pos;
        uint32_t digest;

        if (!cbor_head(p, end, pos, &key) || key.major != 3u || key.argument > end - *pos) {
            return 0;
        }
        *pos += (size_t)key.argument;
        value_pos = *pos;
        memset(&value, 0, sizeof(value));
        if (!cbor_skip(p, end, pos, depth + 1) || !cbor_head(p, end, &value_pos, &value)) {
            return 0;
        }
        digest = digest_flag(p, &key);
        if (digest != 0) {
            metadata->flags |= digest;
            if (value.major == 2u && metadata->hash_size == 0) {
                metadata->hash_offset = (uint32_t)value.data;
                metadata->hash_size = (uint32_t)value.argument;
            }
        } else if (text_is(p, &key, "solc")) {
            metadata->flags |= XXBYTE_EVM_METADATA_SOLC;
            if (value.major == 2u || value.major == 3u) {
                metadata->version_offset = (uint32_t)value.data;
                metadata->version_size = (uint32_t)value.argument;
            }
            if (value.major == 2u && value.argument == 3u) {
                metadata->compiler_version = ((uint32_t)p[value.data] << 16) | ((uint32_t)p[value.data + 1] << 8) | p[value.data + 2];
            }
        } else if (text_is(p, &key, "experimental")) {
            metadata->flags |= XXBYTE_EVM_METADATA_EXPERIMENTAL;
        } else if (text_is(p, &key, "vyper")) {
            metadata->flags |= XXBYTE_EVM_METADATA_VYPER;
            metadata->compiler_version = version_triple(p, end, value.start);
        }
    }
    return 1;
}

/* Solidity layout: map of `length` bytes, then the 2-byte length. */
static int parse_map_layout(const uint8_t *code, size_t code_size, size_t length, xxbyte_evm_metadata *metadata)
{
    size_t end = code_size - 2u;
    size_t pos;

    if (length == 0 || length > end) {
        return 0;
    }
    pos = end - length;
    metadata->offset = (uint32_t)pos;
    if (!parse_map(code, end, &pos, 0, metadata) || pos != end) {
        return 0;
    }
    metadata->flags |= XXBYTE_EVM_METADATA_MAP;
    return 1;
}

/* Vyper 0.3.10+ layout: array of `length` - 2 bytes, then the length;
 * the last element is a map with a "vyper" key. */
static int parse_array_layout(const uint8_t *code, size_t code_size, size_t length, xxbyte_evm_metadata *metadata)
{
    size_t end = code_size - 2u;
    size_t pos;
    cbor_item array;
    uint64_t i;

    if (length < 3u || length > code_size) {
        return 0;
    }
    pos = code_size - length;
    metadata->offset = (uint32_t)pos;
    if (!cbor_head(code, end, &pos, &array) || array.major != 4u || array.argument == 0 || array.argument > end - pos) {
        return 0;
    }
    for (i = 0; i + 1u < array.argument; ++i) {
        if (!cbor_skip(code, end, &pos, 1)) {
            return 0;
        }
    }
    if (!parse_map(code, end, &pos, 1, metadata) || pos != end || (metadata->flags & XXBYTE_EVM_METADATA_VYPER) == 0) {
        return 0;
    }
    metadata->flags |= XXBYTE_EVM_METADATA_ARRAY;
    return 1;
}

xxbyte_status xxbyte_evm_find_metadata(const uint8_t *code, size_t code_size, xxbyte_evm_metadata *metadata)
{
    size_t length;

    if (metadata == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(metadata, 0, sizeof(*metadata));
    if ((code == NULL && code_size != 0) || code_size > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (code_size < 3) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    length = xxbyte_u16be(code + code_size - 2u);
    if (parse_map_layout(code, code_size, length, metadata)) {
        metadata->size = (uint32_t)(code_size - metadata->offset);
        return XXBYTE_STATUS_OK;
    }
    memset(metadata, 0, sizeof(*metadata));
    if (parse_array_layout(code, code_size, length, metadata)) {
        metadata->size = (uint32_t)(code_size - metadata->offset);
        return XXBYTE_STATUS_OK;
    }
    memset(metadata, 0, sizeof(*metadata));
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

#else
typedef int xxbyte_evm_disabled;
#endif
