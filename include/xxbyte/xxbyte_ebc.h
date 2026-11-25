#ifndef XXBYTE_XXBYTE_EBC_H
#define XXBYTE_XXBYTE_EBC_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_EBC
#error "the EFI Byte Code decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * EFI Byte Code (UEFI 2.10 chapter 22) has a single encoding, decoded in
 * mode 0. Addresses are byte addresses in the caller's space, normally the
 * RVA or the offset in the code section; BRANCH and DATA operands hold
 * absolute addresses computed from them.
 */
#define XXBYTE_EBC_MODE_DEFAULT 0u

/*
 * Mnemonic identifiers (xxbyte_instruction.id), one per distinct mnemonic:
 * the UEFI specification's assembler names with every width, condition,
 * EX, and absolute suffix spelled out (ADD32, CMP64ulte, CMPI32weq, MOVqw,
 * MOVIqd, JMP32cs, CALL32EXa). The name is the mnemonic upper-cased with
 * every run of non-alphanumeric characters replaced by '_'. Valid ids are 1
 * to XXBYTE_EBC_ID_COUNT - 1.
 *
 * A trailing 'a' marks an absolute JMP or CALL (operand bit 4 clear); the
 * specification spells it for CALL, and JMP uses the same letter. CALL64
 * is always absolute, so only CALL64a and CALL64EXa exist.
 */
typedef enum xxbyte_ebc_id {
    XXBYTE_EBC_ID_INVALID = 0,
    XXBYTE_EBC_ID_BREAK = 1,
    XXBYTE_EBC_ID_JMP32 = 2,
    XXBYTE_EBC_ID_JMP32A = 3,
    XXBYTE_EBC_ID_JMP32CC = 4,
    XXBYTE_EBC_ID_JMP32CCA = 5,
    XXBYTE_EBC_ID_JMP32CS = 6,
    XXBYTE_EBC_ID_JMP32CSA = 7,
    XXBYTE_EBC_ID_JMP64 = 8,
    XXBYTE_EBC_ID_JMP64A = 9,
    XXBYTE_EBC_ID_JMP64CC = 10,
    XXBYTE_EBC_ID_JMP64CCA = 11,
    XXBYTE_EBC_ID_JMP64CS = 12,
    XXBYTE_EBC_ID_JMP64CSA = 13,
    XXBYTE_EBC_ID_JMP8 = 14,
    XXBYTE_EBC_ID_JMP8CC = 15,
    XXBYTE_EBC_ID_JMP8CS = 16,
    XXBYTE_EBC_ID_CALL32 = 17,
    XXBYTE_EBC_ID_CALL32A = 18,
    XXBYTE_EBC_ID_CALL32EX = 19,
    XXBYTE_EBC_ID_CALL32EXA = 20,
    XXBYTE_EBC_ID_CALL64A = 21,
    XXBYTE_EBC_ID_CALL64EXA = 22,
    XXBYTE_EBC_ID_RET = 23,
    XXBYTE_EBC_ID_CMP32EQ = 24,
    XXBYTE_EBC_ID_CMP32LTE = 25,
    XXBYTE_EBC_ID_CMP32GTE = 26,
    XXBYTE_EBC_ID_CMP32ULTE = 27,
    XXBYTE_EBC_ID_CMP32UGTE = 28,
    XXBYTE_EBC_ID_CMP64EQ = 29,
    XXBYTE_EBC_ID_CMP64LTE = 30,
    XXBYTE_EBC_ID_CMP64GTE = 31,
    XXBYTE_EBC_ID_CMP64ULTE = 32,
    XXBYTE_EBC_ID_CMP64UGTE = 33,
    XXBYTE_EBC_ID_NOT32 = 34,
    XXBYTE_EBC_ID_NOT64 = 35,
    XXBYTE_EBC_ID_NEG32 = 36,
    XXBYTE_EBC_ID_NEG64 = 37,
    XXBYTE_EBC_ID_ADD32 = 38,
    XXBYTE_EBC_ID_ADD64 = 39,
    XXBYTE_EBC_ID_SUB32 = 40,
    XXBYTE_EBC_ID_SUB64 = 41,
    XXBYTE_EBC_ID_MUL32 = 42,
    XXBYTE_EBC_ID_MUL64 = 43,
    XXBYTE_EBC_ID_MULU32 = 44,
    XXBYTE_EBC_ID_MULU64 = 45,
    XXBYTE_EBC_ID_DIV32 = 46,
    XXBYTE_EBC_ID_DIV64 = 47,
    XXBYTE_EBC_ID_DIVU32 = 48,
    XXBYTE_EBC_ID_DIVU64 = 49,
    XXBYTE_EBC_ID_MOD32 = 50,
    XXBYTE_EBC_ID_MOD64 = 51,
    XXBYTE_EBC_ID_MODU32 = 52,
    XXBYTE_EBC_ID_MODU64 = 53,
    XXBYTE_EBC_ID_AND32 = 54,
    XXBYTE_EBC_ID_AND64 = 55,
    XXBYTE_EBC_ID_OR32 = 56,
    XXBYTE_EBC_ID_OR64 = 57,
    XXBYTE_EBC_ID_XOR32 = 58,
    XXBYTE_EBC_ID_XOR64 = 59,
    XXBYTE_EBC_ID_SHL32 = 60,
    XXBYTE_EBC_ID_SHL64 = 61,
    XXBYTE_EBC_ID_SHR32 = 62,
    XXBYTE_EBC_ID_SHR64 = 63,
    XXBYTE_EBC_ID_ASHR32 = 64,
    XXBYTE_EBC_ID_ASHR64 = 65,
    XXBYTE_EBC_ID_EXTNDB32 = 66,
    XXBYTE_EBC_ID_EXTNDB64 = 67,
    XXBYTE_EBC_ID_EXTNDW32 = 68,
    XXBYTE_EBC_ID_EXTNDW64 = 69,
    XXBYTE_EBC_ID_EXTNDD32 = 70,
    XXBYTE_EBC_ID_EXTNDD64 = 71,
    XXBYTE_EBC_ID_MOVBW = 72,
    XXBYTE_EBC_ID_MOVWW = 73,
    XXBYTE_EBC_ID_MOVDW = 74,
    XXBYTE_EBC_ID_MOVQW = 75,
    XXBYTE_EBC_ID_MOVBD = 76,
    XXBYTE_EBC_ID_MOVWD = 77,
    XXBYTE_EBC_ID_MOVDD = 78,
    XXBYTE_EBC_ID_MOVQD = 79,
    XXBYTE_EBC_ID_MOVQQ = 80,
    XXBYTE_EBC_ID_MOVSNW = 81,
    XXBYTE_EBC_ID_MOVSND = 82,
    XXBYTE_EBC_ID_LOADSP = 83,
    XXBYTE_EBC_ID_STORESP = 84,
    XXBYTE_EBC_ID_PUSH32 = 85,
    XXBYTE_EBC_ID_PUSH64 = 86,
    XXBYTE_EBC_ID_POP32 = 87,
    XXBYTE_EBC_ID_POP64 = 88,
    XXBYTE_EBC_ID_CMPI32WEQ = 89,
    XXBYTE_EBC_ID_CMPI32WLTE = 90,
    XXBYTE_EBC_ID_CMPI32WGTE = 91,
    XXBYTE_EBC_ID_CMPI32WULTE = 92,
    XXBYTE_EBC_ID_CMPI32WUGTE = 93,
    XXBYTE_EBC_ID_CMPI32DEQ = 94,
    XXBYTE_EBC_ID_CMPI32DLTE = 95,
    XXBYTE_EBC_ID_CMPI32DGTE = 96,
    XXBYTE_EBC_ID_CMPI32DULTE = 97,
    XXBYTE_EBC_ID_CMPI32DUGTE = 98,
    XXBYTE_EBC_ID_CMPI64WEQ = 99,
    XXBYTE_EBC_ID_CMPI64WLTE = 100,
    XXBYTE_EBC_ID_CMPI64WGTE = 101,
    XXBYTE_EBC_ID_CMPI64WULTE = 102,
    XXBYTE_EBC_ID_CMPI64WUGTE = 103,
    XXBYTE_EBC_ID_CMPI64DEQ = 104,
    XXBYTE_EBC_ID_CMPI64DLTE = 105,
    XXBYTE_EBC_ID_CMPI64DGTE = 106,
    XXBYTE_EBC_ID_CMPI64DULTE = 107,
    XXBYTE_EBC_ID_CMPI64DUGTE = 108,
    XXBYTE_EBC_ID_MOVNW = 109,
    XXBYTE_EBC_ID_MOVND = 110,
    XXBYTE_EBC_ID_PUSHN = 111,
    XXBYTE_EBC_ID_POPN = 112,
    XXBYTE_EBC_ID_MOVIBW = 113,
    XXBYTE_EBC_ID_MOVIBD = 114,
    XXBYTE_EBC_ID_MOVIBQ = 115,
    XXBYTE_EBC_ID_MOVIWW = 116,
    XXBYTE_EBC_ID_MOVIWD = 117,
    XXBYTE_EBC_ID_MOVIWQ = 118,
    XXBYTE_EBC_ID_MOVIDW = 119,
    XXBYTE_EBC_ID_MOVIDD = 120,
    XXBYTE_EBC_ID_MOVIDQ = 121,
    XXBYTE_EBC_ID_MOVIQW = 122,
    XXBYTE_EBC_ID_MOVIQD = 123,
    XXBYTE_EBC_ID_MOVIQQ = 124,
    XXBYTE_EBC_ID_MOVINW = 125,
    XXBYTE_EBC_ID_MOVIND = 126,
    XXBYTE_EBC_ID_MOVINQ = 127,
    XXBYTE_EBC_ID_MOVRELW = 128,
    XXBYTE_EBC_ID_MOVRELD = 129,
    XXBYTE_EBC_ID_MOVRELQ = 130,
    XXBYTE_EBC_ID_COUNT
} xxbyte_ebc_id;

/*
 * Operands, in text order.
 *
 * REGISTER: value = register number; kind = XXBYTE_EBC_REGISTER_GENERAL
 * (R0-R7) or XXBYTE_EBC_REGISTER_DEDICATED (value XXBYTE_EBC_DEDICATED_FLAGS
 * or XXBYTE_EBC_DEDICATED_IP, the LOADSP/STORESP operands); size 1.
 *
 * MEMORY: an indirect operand @Rn. kind = base register, value = constant
 * byte offset, extra = natural units (int64_t stored in the uint64_t), both
 * already signed by the index's sign bit, so the effective address is
 * Rn + (int64_t)extra * sizeof (VOID *) + value (xxbyte_ebc_operand_offset).
 * Both are 0 when the instruction has no index for the operand. flags has
 * XXBYTE_EBC_OPERAND_FLAG_INDEX when the instruction encodes an index for it
 * (printed "(+n,+c)", so @R1 and @R1(+0,+0) differ), plus
 * XXBYTE_EBC_OPERAND_FLAG_NEGATIVE when the index's sign bit is set (so
 * "(-0,-0)" survives a zero value and extra). size is the
 * access width in bytes, or 0 for a natural-width (sizeof (VOID *)) access;
 * an EXTNDB/EXTNDW/EXTNDD source has its own width, 1, 2 or 4 (EDK II's
 * interpreter reads 4 or 8 bytes and truncates). JMP32 and CALL32 read R0
 * as 0; POP and POPn address @R0 after adjusting R0.
 *
 * IMMEDIATE: size = encoded width; SIGNED unless it is a BREAK code. An
 * IMMEDIATE right after a direct REGISTER prints with it ("R2(8)"):
 * - operand 2 of the arithmetic, CMP, EXTNDx and MOVsn forms, operand 1 of
 *   PUSH/PUSHn, JMP32 and CALL32: it is added to the register's value, and
 *   the sum is the operand (R + immediate);
 * - operand 1 of POP/POPn: it is added to the value popped from the stack,
 *   and the sum is stored in the register (R1 = popped + Immed16); the
 *   register's old value is not used.
 * Every Immed16 is sign-extended, as EDK II's interpreter reads it
 * (VmReadImmed16), DIVU and MODU included: UEFI 2.10 sections 22.8.9 and
 * 22.8.17 call their direct immediate unsigned (R2 + 0..65535), but
 * "DIVU32 R1, R2(-1)" divides by R2 - 1 in EDK II. A consumer following the
 * specification's reading uses (uint16_t)value instead.
 * CMPI's immediate (Immed16 or Immed32) is sign-extended too, but EDK II's
 * CMPI64wulte, CMPI64wugte, CMPI64dulte and CMPI64dugte compare the 64-bit
 * operand 1 against the zero-extended low 32 bits of it,
 * (uint64_t)(uint32_t)value (EbcExecute.c ExecuteCMPI): with R1 =
 * 0x100000000, "CMPI64wulte R1, -1" clears the condition flag. The other
 * CMPI64 conditions and every CMPI32 condition use value as decoded.
 * For MOV operand 2 ("R2(+1,+8)") and MOVIn operand 2 it is a natural
 * index: value = constant units, extra = natural units, flags SIGNED |
 * XXBYTE_EBC_OPERAND_FLAG_INDEX (| XXBYTE_EBC_OPERAND_FLAG_NEGATIVE), as for
 * MEMORY.
 *
 * BRANCH: the absolute target of JMP8, JMP64, CALL64 and of the JMP32 and
 * CALL32 forms whose operand is a direct R0 (the register is ignored):
 * address + size + offset when relative, the immediate when absolute. size
 * is the immediate's width (0 for a JMP32/CALL32 without one).
 *
 * DATA: MOVREL's address, address + size + offset; size = offset width.
 *
 * The formatter renders from these operands and instruction->opcode only;
 * it never reads instruction->code, so an instruction can be formatted
 * after its buffer is gone, and a copy formats like the original.
 */
#define XXBYTE_EBC_REGISTER_GENERAL 0u
#define XXBYTE_EBC_REGISTER_DEDICATED 1u
#define XXBYTE_EBC_DEDICATED_FLAGS 0u
#define XXBYTE_EBC_DEDICATED_IP 1u

/*
 * Family operand flags, in the top two bits of xxbyte_operand.flags. Unlike
 * index kinds (XXBYTE_INDEX_FAMILY_FIRST), xxbyte_common.h reserves no
 * family range for operand flags yet; a core change request asks for
 * XXBYTE_OPERAND_FLAG_FAMILY_FIRST (bits from 0x1000u up family-defined).
 * Until then src/xxbyte_ebc.c refuses to compile when one of these bits is
 * also a core XXBYTE_OPERAND_FLAG_* it knows of, or lies below
 * XXBYTE_OPERAND_FLAG_FAMILY_FIRST once the core defines it. A generic
 * consumer tests only the core flags; XXBYTE_EBC_OPERAND_FLAG_MASK is the set
 * of bits that belong to this family.
 */
#define XXBYTE_EBC_OPERAND_FLAG_INDEX 0x4000u    /* a natural index is encoded */
#define XXBYTE_EBC_OPERAND_FLAG_NEGATIVE 0x8000u /* its sign bit is set */
#define XXBYTE_EBC_OPERAND_FLAG_MASK (XXBYTE_EBC_OPERAND_FLAG_INDEX | XXBYTE_EBC_OPERAND_FLAG_NEGATIVE)

/* The fields of a natural index (UEFI 2.10 section 22.4). */
typedef struct xxbyte_ebc_index {
    uint32_t negative; /* 1 when the sign bit is set */
    uint64_t natural;  /* natural units n */
    uint64_t constant; /* constant units c */
} xxbyte_ebc_index;

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_ebc_mnemonic(uint32_t id);

/*
 * Splits a raw natural index of width 2, 4 or 8 bytes (the little-endian
 * value in the instruction; bits above the width are ignored) into its
 * fields. Returns XXBYTE_STATUS_INVALID_ARGUMENT for another width or a
 * NULL index, and XXBYTE_STATUS_INVALID_INSTRUCTION when the natural-units
 * field is wider than the index (16-bit indexes whose width field is 7).
 * *index is zeroed on failure.
 *
 * The decoder rejects every instruction carrying such a 16-bit index
 * (INVALID_INSTRUCTION). The specification gives the natural-units field
 * 2 * 7 = 14 of the 12 bits available, and EDK II's debugger
 * (EdbGetNaturalIndex16) shifts by a negative count for it. EDK II's
 * interpreter does execute these bytes (EbcExecute.c VmReadIndex16): it
 * takes natural units = raw & 0x3FFF (bits 12-13 of the width field
 * included), constant units = 0, truncates natural * sizeof (UINTN) to
 * INT16 and negates it when the sign bit is set. That overflow artifact has
 * no faithful (sign, n, c) form, so a consumer that wants to step over such
 * an instruction the interpreter's way computes the offset from the raw
 * index itself; the instruction's length is the one it would have with any
 * other index of the same width.
 */
XXBYTE_API xxbyte_status xxbyte_ebc_decode_index(uint64_t raw, uint32_t width, xxbyte_ebc_index *index);

/* (constant + natural * pointer_size), negated when negative; 64-bit
 * wrap-around arithmetic. */
XXBYTE_API int64_t xxbyte_ebc_index_offset(const xxbyte_ebc_index *index, uint32_t pointer_size);

/* value + (int64_t)extra * pointer_size for a MEMORY operand or a natural
 * IMMEDIATE (see above); value for other operands, 0 for NULL. */
XXBYTE_API int64_t xxbyte_ebc_operand_offset(const xxbyte_operand *operand, uint32_t pointer_size);

/*
 * Container listing (xxbyte_disasm_file, "xxbyte_cli -f ebc -F"): an EFI
 * Byte Code PE/COFF image (PE32 or PE32+, Machine 0x0EBC, normally a .efi
 * driver or application). The listing prints the headers, the export table
 * (forwarders included) and a summary of the base relocations by type,
 *   ; EFI Byte Code image: PE32+, image base 0x10000000, entry RVA 0x1000, subsystem 11 (EFI boot service driver), 4 sections
 *   ; exports of "Test.efi": 2 functions, 1 names, ordinal base 1
 *   ;   ordinal 1 "Exported" RVA 0x1028
 *   ; base relocations: 4 entries in 2 blocks, ABSOLUTE 2, DIR64 2
 * then a line per section; data sections are "data: not listed", and each
 * code section (IMAGE_SCN_CNT_CODE or IMAGE_SCN_MEM_EXECUTE) is swept
 * linearly as xxbyte_disasm_buf does, over its raw data up to its virtual
 * size, at virtual addresses (image base + RVA). Label lines ("entry:",
 * "Exported:", "ordinal_2:", "sub_10001022:") precede the instructions at
 * the entry point, at each export and at each direct CALL target inside the
 * image (relative CALL32/CALL64 with a direct R0 operand and absolute
 * CALL64a, the BRANCH operands); the resolver prints those names for every
 * BRANCH and DATA operand that hits a label ("CALL32 Exported",
 * "JMP8cs sub_10001022"), as EDK II's debugger prints symbols for CALL
 * targets. A label inside an instruction of the sweep gets no line, only
 * the operand name. Calls through registers or memory (CALLEX to native
 * code included) cannot be resolved statically; relocations are summarized,
 * not applied.
 */

#ifdef __cplusplus
}
#endif

#endif
