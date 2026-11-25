#ifndef XXBYTE_EBPF_INTERNAL_H
#define XXBYTE_EBPF_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_EBPF

/* Solana sBPF (src/xxbyte_ebpf_sbpf.c). mode is a checked sBPF mode. */
xxbyte_status xxbyte_ebpf_sbpf_decode(uint32_t mode, const xxbyte_ebpf_context *context, const uint8_t *code, size_t code_size, uint64_t address,
                                      xxbyte_instruction *instruction);
int xxbyte_ebpf_sbpf_format(const xxbyte_instruction *instruction, xxbyte_fmt *fmt);

/* The ELF container listing (src/xxbyte_ebpf_elf.c). */
xxbyte_status xxbyte_ebpf_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
