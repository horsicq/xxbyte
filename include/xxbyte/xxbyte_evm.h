/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_EVM_H
#define XXBYTE_XXBYTE_EVM_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_EVM
#error "the Ethereum EVM decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * EVM decode mode: the hard fork whose instruction set applies, as defined
 * by the execution-specs (github.com/ethereum/execution-specs) Ops enums.
 * Mode 0 is the newest final mainnet fork (Osaka); the other values name a
 * fork explicitly and keep their meaning. A fork that changed no opcode
 * decodes like its predecessor, but has its own mode where the specs list it
 * as a milestone. Any other value is XXBYTE_STATUS_UNSUPPORTED_MODE.
 *
 * XXBYTE_EVM_MODE_AMSTERDAM is a draft: the fork is not scheduled yet, and
 * its opcodes (SLOTNUM, EIP-7843; DUPN, SWAPN and EXCHANGE, EIP-8024) follow
 * the execution-specs and EIP texts as of October 2026, when both EIPs were
 * in Last Call. They may still change.
 *
 * EOF (EVM Object Format, EIP-7692 and its RJUMP, CALLF, DATALOAD, EXTCALL
 * and related opcodes) is not supported: it was removed from Prague and
 * Osaka and its meta-EIP is stagnant.
 */
#define XXBYTE_EVM_MODE_DEFAULT 0u
#define XXBYTE_EVM_MODE_FRONTIER 1u
#define XXBYTE_EVM_MODE_HOMESTEAD 2u /* DELEGATECALL */
#define XXBYTE_EVM_MODE_TANGERINE_WHISTLE 3u
#define XXBYTE_EVM_MODE_SPURIOUS_DRAGON 4u
#define XXBYTE_EVM_MODE_BYZANTIUM         \
    5u /* RETURNDATASIZE, RETURNDATACOPY, \
          STATICCALL, REVERT */
#define XXBYTE_EVM_MODE_CONSTANTINOPLE                             \
    6u                              /* SHL, SHR, SAR, EXTCODEHASH, \
                                       CREATE2 */
#define XXBYTE_EVM_MODE_ISTANBUL 7u /* CHAINID, SELFBALANCE */
#define XXBYTE_EVM_MODE_BERLIN 8u
#define XXBYTE_EVM_MODE_LONDON 9u    /* BASEFEE */
#define XXBYTE_EVM_MODE_PARIS 10u    /* 0x44 is PREVRANDAO */
#define XXBYTE_EVM_MODE_SHANGHAI 11u /* PUSH0 */
#define XXBYTE_EVM_MODE_CANCUN             \
    12u /* TLOAD, TSTORE, MCOPY, BLOBHASH, \
           BLOBBASEFEE */
#define XXBYTE_EVM_MODE_PRAGUE 13u
#define XXBYTE_EVM_MODE_OSAKA 14u /* CLZ */
#define XXBYTE_EVM_MODE_AMSTERDAM       \
    15u /* draft: SLOTNUM, DUPN, SWAPN, \
           EXCHANGE */

/*
 * Forks with the instruction set of an earlier mode: every other fork of
 * execution-specs (src/ethereum/forks) maps to the newest fork with a mode
 * activated before it. BPO3 to BPO5 (blob-parameter-only forks) and BOGOTA
 * are drafts like AMSTERDAM: the specs list them as Unscheduled, BPO3 to
 * BPO5 between Osaka and Amsterdam and Bogota after Amsterdam, with the
 * Ops enums of Osaka and Amsterdam. Should a draft fork change opcodes, it
 * gets a mode of its own and its macro changes.
 */
#define XXBYTE_EVM_MODE_DAO_FORK XXBYTE_EVM_MODE_HOMESTEAD
#define XXBYTE_EVM_MODE_PETERSBURG XXBYTE_EVM_MODE_CONSTANTINOPLE
#define XXBYTE_EVM_MODE_MUIR_GLACIER XXBYTE_EVM_MODE_ISTANBUL
#define XXBYTE_EVM_MODE_ARROW_GLACIER XXBYTE_EVM_MODE_LONDON
#define XXBYTE_EVM_MODE_GRAY_GLACIER XXBYTE_EVM_MODE_LONDON
#define XXBYTE_EVM_MODE_BPO1 XXBYTE_EVM_MODE_OSAKA
#define XXBYTE_EVM_MODE_BPO2 XXBYTE_EVM_MODE_OSAKA
#define XXBYTE_EVM_MODE_BPO3 XXBYTE_EVM_MODE_OSAKA       /* draft */
#define XXBYTE_EVM_MODE_BPO4 XXBYTE_EVM_MODE_OSAKA       /* draft */
#define XXBYTE_EVM_MODE_BPO5 XXBYTE_EVM_MODE_OSAKA       /* draft */
#define XXBYTE_EVM_MODE_BOGOTA XXBYTE_EVM_MODE_AMSTERDAM /* draft */

/* The fork mode 0 stands for, and the newest mode (a draft). */
#define XXBYTE_EVM_MODE_LATEST_FINAL XXBYTE_EVM_MODE_OSAKA
#define XXBYTE_EVM_MODE_NEWEST XXBYTE_EVM_MODE_AMSTERDAM

/*
 * Decoding. Every opcode is one byte; instruction.opcode is that byte.
 *   - PUSH1..PUSH32 (0x60-0x7F) carry 1 to 32 immediate bytes. One operand:
 *     PUSH1..PUSH8 an XXBYTE_OPERAND_IMMEDIATE whose value holds the
 *     big-endian immediate as an unsigned number (cast it to uint64_t;
 *     PUSH8 0xffffffffffffffff is stored as -1), size n; PUSH9..PUSH32 an
 *     XXBYTE_OPERAND_BYTES with value 1 (the offset of the data from the
 *     opcode) and size n, read through instruction.code.
 *     xxbyte_evm_push_value gives the 256-bit value of any PUSH. A PUSH whose
 *     data runs past code_size is XXBYTE_STATUS_TRUNCATED, although the EVM
 *     itself reads missing code bytes as zero (a contract may end inside a
 *     PUSH, typically in its metadata).
 *   - PUSH0 has an IMPLICIT IMMEDIATE 0. DUP1..DUP16 have an IMPLICIT
 *     IMMEDIATE holding n, the stack item duplicated (1 = top). SWAP1..SWAP16
 *     have an IMPLICIT IMMEDIATE holding n, the top being swapped with item
 *     n + 1 (1 = top, so SWAP1 exchanges the two top items and SWAP16 reaches
 *     item 17). LOG0..LOG4 have an IMPLICIT IMMEDIATE holding the topic
 *     count. Implicit operands have size 0 and print nothing.
 *   - DUPN and SWAPN (draft, Amsterdam) take one immediate byte x and have
 *     one IMMEDIATE holding n = decode_single(x) (17..235), DUPN duplicating
 *     the n'th item and SWAPN swapping the top with item n + 1 (the meaning
 *     of DUPn and SWAPn). EXCHANGE has two IMMEDIATE operands n and m
 *     (decode_pair(x): 1 <= n < m <= 30 - n), swapping items n + 1 and m + 1.
 *     Their size is 1 and extra holds the raw byte x. An immediate in the
 *     range EIP-8024 forbids (DUPN and SWAPN 91..127, EXCHANGE 82..127) makes
 *     the opcode XXBYTE_STATUS_INVALID_INSTRUCTION, so a listing shows the
 *     opcode as "db" and decodes the next byte as an instruction, as the EIP
 *     requires of disassemblers (e7 5b: INVALID_SWAPN, JUMPDEST). At the end
 *     of the input they are TRUNCATED like a PUSH.
 *   - 0xFE is INVALID, the designated invalid instruction (EIP-141), in
 *     every mode. Opcodes the mode does not define are
 *     XXBYTE_STATUS_INVALID_INSTRUCTION.
 *
 * No operand is a branch: JUMP and JUMPI take their target from the stack.
 * Addresses are byte offsets into the code (the program counter).
 *
 * Groups: JUMP is JUMP|STOP and JUMPI CONDITIONAL; CALL, CALLCODE,
 * DELEGATECALL, STATICCALL, CREATE and CREATE2 are CALL; STOP, RETURN and
 * SELFDESTRUCT are RETURN|STOP; REVERT and INVALID are THROW|STOP. JUMPDEST
 * only marks a valid jump target and has no group.
 *
 * Text: the uppercase mnemonic, then for PUSH1..PUSH32 the immediate as
 * "0x" and two lowercase hex digits per encoded byte ("PUSH2 0x0080",
 * "PUSH0"), and for DUPN, SWAPN and EXCHANGE the decoded numbers in decimal,
 * separated by spaces as in EIP-8024 ("DUPN 17", "EXCHANGE 2 3"). 0x20 is
 * shown as KECCAK256, as evm.codes, solc and geth name it (execution-specs:
 * KECCAK, the Yellow Paper: SHA3), and 0x44 as DIFFICULTY before Paris and
 * PREVRANDAO from Paris on.
 *
 * Resolver: xxbyte_format_ex passes the immediate of PUSH1..PUSH32 (the
 * IMMEDIATE or BYTES operand 0) to the resolver, so a caller can print a
 * label for a jump target, a function-selector name or an address book
 * entry; returning 0 keeps the hex. Nothing else is resolved.
 */

/*
 * Mnemonic identifiers (xxbyte_instruction.id), one per mnemonic of all
 * modes. The final forks' mnemonics come first in opcode order (DIFFICULTY
 * just before PREVRANDAO, both 0x44), then the draft Amsterdam ones. The
 * enum name is the mnemonic with every run of characters other than A-Z
 * and 0-9 replaced by '_'. The ids of PUSH0..PUSH32, DUP1..DUP16,
 * SWAP1..SWAP16 and LOG0..LOG4 are consecutive. Valid ids are 1 to
 * XXBYTE_EVM_ID_COUNT - 1; XXBYTE_EVM_ID_INVALID is the INVALID (0xFE)
 * instruction, XXBYTE_EVM_ID_NONE (0) no instruction.
 */
typedef enum xxbyte_evm_id {
    XXBYTE_EVM_ID_NONE = 0,
    XXBYTE_EVM_ID_STOP = 1,
    XXBYTE_EVM_ID_ADD = 2,
    XXBYTE_EVM_ID_MUL = 3,
    XXBYTE_EVM_ID_SUB = 4,
    XXBYTE_EVM_ID_DIV = 5,
    XXBYTE_EVM_ID_SDIV = 6,
    XXBYTE_EVM_ID_MOD = 7,
    XXBYTE_EVM_ID_SMOD = 8,
    XXBYTE_EVM_ID_ADDMOD = 9,
    XXBYTE_EVM_ID_MULMOD = 10,
    XXBYTE_EVM_ID_EXP = 11,
    XXBYTE_EVM_ID_SIGNEXTEND = 12,
    XXBYTE_EVM_ID_LT = 13,
    XXBYTE_EVM_ID_GT = 14,
    XXBYTE_EVM_ID_SLT = 15,
    XXBYTE_EVM_ID_SGT = 16,
    XXBYTE_EVM_ID_EQ = 17,
    XXBYTE_EVM_ID_ISZERO = 18,
    XXBYTE_EVM_ID_AND = 19,
    XXBYTE_EVM_ID_OR = 20,
    XXBYTE_EVM_ID_XOR = 21,
    XXBYTE_EVM_ID_NOT = 22,
    XXBYTE_EVM_ID_BYTE = 23,
    XXBYTE_EVM_ID_SHL = 24,
    XXBYTE_EVM_ID_SHR = 25,
    XXBYTE_EVM_ID_SAR = 26,
    XXBYTE_EVM_ID_CLZ = 27,
    XXBYTE_EVM_ID_KECCAK256 = 28,
    XXBYTE_EVM_ID_ADDRESS = 29,
    XXBYTE_EVM_ID_BALANCE = 30,
    XXBYTE_EVM_ID_ORIGIN = 31,
    XXBYTE_EVM_ID_CALLER = 32,
    XXBYTE_EVM_ID_CALLVALUE = 33,
    XXBYTE_EVM_ID_CALLDATALOAD = 34,
    XXBYTE_EVM_ID_CALLDATASIZE = 35,
    XXBYTE_EVM_ID_CALLDATACOPY = 36,
    XXBYTE_EVM_ID_CODESIZE = 37,
    XXBYTE_EVM_ID_CODECOPY = 38,
    XXBYTE_EVM_ID_GASPRICE = 39,
    XXBYTE_EVM_ID_EXTCODESIZE = 40,
    XXBYTE_EVM_ID_EXTCODECOPY = 41,
    XXBYTE_EVM_ID_RETURNDATASIZE = 42,
    XXBYTE_EVM_ID_RETURNDATACOPY = 43,
    XXBYTE_EVM_ID_EXTCODEHASH = 44,
    XXBYTE_EVM_ID_BLOCKHASH = 45,
    XXBYTE_EVM_ID_COINBASE = 46,
    XXBYTE_EVM_ID_TIMESTAMP = 47,
    XXBYTE_EVM_ID_NUMBER = 48,
    XXBYTE_EVM_ID_DIFFICULTY = 49,
    XXBYTE_EVM_ID_PREVRANDAO = 50,
    XXBYTE_EVM_ID_GASLIMIT = 51,
    XXBYTE_EVM_ID_CHAINID = 52,
    XXBYTE_EVM_ID_SELFBALANCE = 53,
    XXBYTE_EVM_ID_BASEFEE = 54,
    XXBYTE_EVM_ID_BLOBHASH = 55,
    XXBYTE_EVM_ID_BLOBBASEFEE = 56,
    XXBYTE_EVM_ID_POP = 57,
    XXBYTE_EVM_ID_MLOAD = 58,
    XXBYTE_EVM_ID_MSTORE = 59,
    XXBYTE_EVM_ID_MSTORE8 = 60,
    XXBYTE_EVM_ID_SLOAD = 61,
    XXBYTE_EVM_ID_SSTORE = 62,
    XXBYTE_EVM_ID_JUMP = 63,
    XXBYTE_EVM_ID_JUMPI = 64,
    XXBYTE_EVM_ID_PC = 65,
    XXBYTE_EVM_ID_MSIZE = 66,
    XXBYTE_EVM_ID_GAS = 67,
    XXBYTE_EVM_ID_JUMPDEST = 68,
    XXBYTE_EVM_ID_TLOAD = 69,
    XXBYTE_EVM_ID_TSTORE = 70,
    XXBYTE_EVM_ID_MCOPY = 71,
    XXBYTE_EVM_ID_PUSH0 = 72,
    XXBYTE_EVM_ID_PUSH1 = 73,
    XXBYTE_EVM_ID_PUSH2 = 74,
    XXBYTE_EVM_ID_PUSH3 = 75,
    XXBYTE_EVM_ID_PUSH4 = 76,
    XXBYTE_EVM_ID_PUSH5 = 77,
    XXBYTE_EVM_ID_PUSH6 = 78,
    XXBYTE_EVM_ID_PUSH7 = 79,
    XXBYTE_EVM_ID_PUSH8 = 80,
    XXBYTE_EVM_ID_PUSH9 = 81,
    XXBYTE_EVM_ID_PUSH10 = 82,
    XXBYTE_EVM_ID_PUSH11 = 83,
    XXBYTE_EVM_ID_PUSH12 = 84,
    XXBYTE_EVM_ID_PUSH13 = 85,
    XXBYTE_EVM_ID_PUSH14 = 86,
    XXBYTE_EVM_ID_PUSH15 = 87,
    XXBYTE_EVM_ID_PUSH16 = 88,
    XXBYTE_EVM_ID_PUSH17 = 89,
    XXBYTE_EVM_ID_PUSH18 = 90,
    XXBYTE_EVM_ID_PUSH19 = 91,
    XXBYTE_EVM_ID_PUSH20 = 92,
    XXBYTE_EVM_ID_PUSH21 = 93,
    XXBYTE_EVM_ID_PUSH22 = 94,
    XXBYTE_EVM_ID_PUSH23 = 95,
    XXBYTE_EVM_ID_PUSH24 = 96,
    XXBYTE_EVM_ID_PUSH25 = 97,
    XXBYTE_EVM_ID_PUSH26 = 98,
    XXBYTE_EVM_ID_PUSH27 = 99,
    XXBYTE_EVM_ID_PUSH28 = 100,
    XXBYTE_EVM_ID_PUSH29 = 101,
    XXBYTE_EVM_ID_PUSH30 = 102,
    XXBYTE_EVM_ID_PUSH31 = 103,
    XXBYTE_EVM_ID_PUSH32 = 104,
    XXBYTE_EVM_ID_DUP1 = 105,
    XXBYTE_EVM_ID_DUP2 = 106,
    XXBYTE_EVM_ID_DUP3 = 107,
    XXBYTE_EVM_ID_DUP4 = 108,
    XXBYTE_EVM_ID_DUP5 = 109,
    XXBYTE_EVM_ID_DUP6 = 110,
    XXBYTE_EVM_ID_DUP7 = 111,
    XXBYTE_EVM_ID_DUP8 = 112,
    XXBYTE_EVM_ID_DUP9 = 113,
    XXBYTE_EVM_ID_DUP10 = 114,
    XXBYTE_EVM_ID_DUP11 = 115,
    XXBYTE_EVM_ID_DUP12 = 116,
    XXBYTE_EVM_ID_DUP13 = 117,
    XXBYTE_EVM_ID_DUP14 = 118,
    XXBYTE_EVM_ID_DUP15 = 119,
    XXBYTE_EVM_ID_DUP16 = 120,
    XXBYTE_EVM_ID_SWAP1 = 121,
    XXBYTE_EVM_ID_SWAP2 = 122,
    XXBYTE_EVM_ID_SWAP3 = 123,
    XXBYTE_EVM_ID_SWAP4 = 124,
    XXBYTE_EVM_ID_SWAP5 = 125,
    XXBYTE_EVM_ID_SWAP6 = 126,
    XXBYTE_EVM_ID_SWAP7 = 127,
    XXBYTE_EVM_ID_SWAP8 = 128,
    XXBYTE_EVM_ID_SWAP9 = 129,
    XXBYTE_EVM_ID_SWAP10 = 130,
    XXBYTE_EVM_ID_SWAP11 = 131,
    XXBYTE_EVM_ID_SWAP12 = 132,
    XXBYTE_EVM_ID_SWAP13 = 133,
    XXBYTE_EVM_ID_SWAP14 = 134,
    XXBYTE_EVM_ID_SWAP15 = 135,
    XXBYTE_EVM_ID_SWAP16 = 136,
    XXBYTE_EVM_ID_LOG0 = 137,
    XXBYTE_EVM_ID_LOG1 = 138,
    XXBYTE_EVM_ID_LOG2 = 139,
    XXBYTE_EVM_ID_LOG3 = 140,
    XXBYTE_EVM_ID_LOG4 = 141,
    XXBYTE_EVM_ID_CREATE = 142,
    XXBYTE_EVM_ID_CALL = 143,
    XXBYTE_EVM_ID_CALLCODE = 144,
    XXBYTE_EVM_ID_RETURN = 145,
    XXBYTE_EVM_ID_DELEGATECALL = 146,
    XXBYTE_EVM_ID_CREATE2 = 147,
    XXBYTE_EVM_ID_STATICCALL = 148,
    XXBYTE_EVM_ID_REVERT = 149,
    XXBYTE_EVM_ID_INVALID = 150,
    XXBYTE_EVM_ID_SELFDESTRUCT = 151,
    XXBYTE_EVM_ID_SLOTNUM = 152,
    XXBYTE_EVM_ID_DUPN = 153,
    XXBYTE_EVM_ID_SWAPN = 154,
    XXBYTE_EVM_ID_EXCHANGE = 155,
    XXBYTE_EVM_ID_COUNT = 156
} xxbyte_evm_id;

/* The mnemonic of id, or NULL for an unknown id. */
XXBYTE_API const char *xxbyte_evm_mnemonic(uint32_t id);

/* The mnemonic of opcode in mode, or NULL when the mode is unsupported or
 * does not define the opcode. */
XXBYTE_API const char *xxbyte_evm_opcode_name(uint32_t mode, uint8_t opcode);

/*
 * The forks in which id is defined: *first_mode receives the mode of the
 * fork that introduced it and *last_mode the newest mode that still defines
 * it (XXBYTE_EVM_MODE_NEWEST unless a later fork renamed the opcode, as
 * Paris did DIFFICULTY). Useful to find the oldest fork that can run some
 * code. XXBYTE_STATUS_INVALID_ARGUMENT for an unknown id or NULL output.
 */
XXBYTE_API xxbyte_status xxbyte_evm_id_forks(uint32_t id, uint32_t *first_mode, uint32_t *last_mode);

/*
 * The execution-specs name of a mode's fork ("frontier", "tangerine_whistle",
 * "osaka"); mode 0 gives the name of XXBYTE_EVM_MODE_LATEST_FINAL. NULL for
 * an unsupported mode.
 */
XXBYTE_API const char *xxbyte_evm_mode_name(uint32_t mode);

/*
 * Parses a fork name into a mode. Accepts the names of xxbyte_evm_mode_name,
 * the execution-specs names of forks without opcode changes ("dao_fork",
 * "muir_glacier", "arrow_glacier", "gray_glacier", "bpo1" to "bpo5" and
 * "bogota"; see the mode macros above), "petersburg", "merge" (Paris), the
 * combined upgrade names "shapella", "dencun", "pectra", "fusaka" and
 * "glamsterdam", and "default" and "latest" (mode 0). Case does not matter
 * and '-' and ' ' count as '_'.
 * XXBYTE_STATUS_UNSUPPORTED_MODE for an unknown name,
 * XXBYTE_STATUS_INVALID_ARGUMENT for NULL arguments.
 */
XXBYTE_API xxbyte_status xxbyte_evm_mode_from_name(const char *name, uint32_t *mode);

/*
 * The stack items a decoded EVM instruction removes (*inputs, the Yellow
 * Paper's delta) and adds (*outputs, alpha): ADD 2 and 1, DUP3 3 and 4,
 * SWAP2 3 and 3, LOG2 4 and 0, CALL 7 and 1; DUPN n gives n and n + 1,
 * SWAPN n gives n + 1 and n + 1 and EXCHANGE n m gives m + 1 and m + 1.
 * XXBYTE_STATUS_INVALID_ARGUMENT unless instruction is a decoded EVM
 * instruction.
 */
XXBYTE_API xxbyte_status xxbyte_evm_stack_effect(const xxbyte_instruction *instruction, uint32_t *inputs, uint32_t *outputs);

/*
 * Writes the 256-bit value a PUSH0..PUSH32 instruction pushes to value[0..31]
 * as big-endian bytes, zero-extended. The data of PUSH9..PUSH32 is read
 * through instruction->code. XXBYTE_STATUS_INVALID_ARGUMENT for any other
 * instruction.
 */
XXBYTE_API xxbyte_status xxbyte_evm_push_value(const xxbyte_instruction *instruction, uint8_t value[32]);

/*
 * Jump-destination analysis of code[0..code_size) as the EVM performs it: a
 * byte is a valid JUMP/JUMPI target when it is 0x5B (JUMPDEST) and not part
 * of the data of a PUSH1..PUSH32. Undefined opcodes and the immediates of
 * DUPN, SWAPN and EXCHANGE are stepped over as single bytes, so the result
 * is the same in every mode. Sets bit (i & 7) of bitmap[i >> 3] for every
 * valid destination i and clears the other bits of the first
 * (code_size + 7) / 8 bytes. bitmap may be NULL when bitmap_size is 0, to
 * count only; otherwise bitmap_size must be at least (code_size + 7) / 8.
 * *count (optional) receives the number of valid destinations, 0 on failure.
 * XXBYTE_STATUS_UNSUPPORTED_MODE for an unsupported mode,
 * XXBYTE_STATUS_INVALID_ARGUMENT for a NULL buffer or a short bitmap.
 */
XXBYTE_API xxbyte_status xxbyte_evm_jump_destinations(uint32_t mode, const uint8_t *code, size_t code_size, uint8_t *bitmap, size_t bitmap_size, size_t *count);

/* Compiler metadata found at the end of EVM code. */
#define XXBYTE_EVM_METADATA_MAP            \
    0x0001u /* CBOR map + 2-byte length of \
               the map (Solidity, Vyper    \
               0.3.4 to 0.3.9) */
#define XXBYTE_EVM_METADATA_ARRAY                                                   \
    0x0002u                                      /* CBOR array + 2-byte length that \
                                                    counts itself (Vyper 0.3.10+) */
#define XXBYTE_EVM_METADATA_IPFS 0x0010u         /* "ipfs" key */
#define XXBYTE_EVM_METADATA_BZZR0 0x0020u        /* "bzzr0" key */
#define XXBYTE_EVM_METADATA_BZZR1 0x0040u        /* "bzzr1" key */
#define XXBYTE_EVM_METADATA_SOLC 0x0080u         /* "solc" key */
#define XXBYTE_EVM_METADATA_EXPERIMENTAL 0x0100u /* "experimental" key */
#define XXBYTE_EVM_METADATA_VYPER 0x0200u        /* "vyper" key */

typedef struct xxbyte_evm_metadata {
    uint32_t offset;         /* first byte of the CBOR item */
    uint32_t size;           /* offset to the end of the code: the CBOR
                                item and the 2-byte length */
    uint32_t flags;          /* XXBYTE_EVM_METADATA_* */
    uint32_t hash_offset;    /* the ipfs, bzzr1 or bzzr0 digest (byte
                                string), 0 if none */
    uint32_t hash_size;      /* its length: 34 for ipfs, 32 for swarm */
    uint32_t version_offset; /* the bytes of a "solc" byte or text string
                                (3 bytes in a release build, text in a
                                prerelease build), 0 if none */
    uint32_t version_size;
    uint32_t compiler_version; /* major << 16 | minor << 8 | patch from a
                                  3-byte "solc" string or a 3-number
                                  "vyper" array, 0 if not available */
} xxbyte_evm_metadata;

/*
 * Finds the CBOR-encoded compiler metadata at the end of EVM code (runtime
 * code from solc, initcode or runtime code from Vyper), so a caller can stop
 * a listing before it. The last two bytes are a big-endian length L. The
 * Solidity layout has a CBOR map of L bytes before them, with text keys and
 * at least one entry; the Vyper 0.3.10+ layout has a CBOR array of L - 2
 * bytes before them whose last element is a map with a "vyper" key. The
 * CBOR item must end exactly at the length field (definite lengths, no tags,
 * nesting depth at most 16). Offsets are from code[0].
 *
 * Returns XXBYTE_STATUS_OK and fills *metadata, or
 * XXBYTE_STATUS_INVALID_INSTRUCTION when the code does not end with such
 * metadata (*metadata zeroed), XXBYTE_STATUS_INVALID_ARGUMENT for NULL
 * arguments or a code_size above UINT32_MAX.
 */
XXBYTE_API xxbyte_status xxbyte_evm_find_metadata(const uint8_t *code, size_t code_size, xxbyte_evm_metadata *metadata);

/*
 * Decodes hex text into code: ASCII hex digits in either case, any ASCII
 * whitespace between them (also inside a byte) and one optional "0x" or
 * "0X" before the first digit, as solc --bin-runtime, eth_getCode and
 * block explorers write bytecode. Writes the bytes to code[0..capacity) and
 * sets *code_size. XXBYTE_STATUS_INVALID_INSTRUCTION when text holds any
 * other character, no digit or an odd number of digits,
 * XXBYTE_STATUS_OUT_OF_MEMORY when the code does not fit in capacity (a
 * capacity of size / 2 always suffices), XXBYTE_STATUS_INVALID_ARGUMENT for
 * NULL pointers. code may be NULL with capacity 0 to count the bytes.
 */
XXBYTE_API xxbyte_status xxbyte_evm_parse_hex(const uint8_t *text, size_t size, uint8_t *code, size_t capacity, size_t *code_size);

/*
 * Container listing. xxbyte_disasm_file(XXBYTE_FAMILY_EVM, ...) lists one
 * contract's code: runtime bytecode (or initcode) as raw bytes, or as hex
 * text when the whole file is hex text in the form xxbyte_evm_parse_hex
 * reads (a raw file made only of the bytes of hex digits and whitespace is
 * therefore read as text; one with an odd number of digits is
 * XXBYTE_STATUS_INVALID_INSTRUCTION). XXBYTE_MODE_AUTO is mode 0, the
 * default fork. The listing is:
 *
 *     ; EVM code, 345 bytes from hex text, osaka
 *     ; metadata at 0x123: 53 bytes, CBOR map, solc 0.8.20, ipfs
 *     00000000  60 80                    PUSH1 0x80
 *     ...
 *     0000000B  57                       JUMPI  ; -> 0x10
 *     loc_0x10:
 *     00000010  5B                       JUMPDEST
 *     ...
 *     ; metadata
 *     00000123  A2 64 69 70 66 73 58 22  db 0xA2, 0x64, 0x69, ...
 *
 * The header line gives the code size, whether it came from hex text and
 * the fork; a second line describes the compiler metadata that
 * xxbyte_evm_find_metadata finds at the end of the code. Decoding stops
 * where the metadata begins, and the metadata is listed as data, eight
 * bytes per "db" line. Every JUMPDEST that is a valid jump destination
 * (xxbyte_evm_jump_destinations over the whole code) is preceded by a label
 * line "loc_0x<pc>:", the program counter in lowercase hex without leading
 * zeros (the solc assembly's tag_N numbers are not recoverable from
 * bytecode). A JUMP or JUMPI that directly follows a PUSH0..PUSH32 gets the
 * pushed target as a comment, "  ; -> 0x1f", with " (not a JUMPDEST)" when
 * it is no valid destination. Targets computed on the stack are not
 * followed. Bytes that do not decode are "db" lines, as in
 * xxbyte_disasm_buf (a PUSH cut by the metadata or the end of the code is
 * one of them, although the EVM pads it with zeros).
 */

#ifdef __cplusplus
}
#endif

#endif
