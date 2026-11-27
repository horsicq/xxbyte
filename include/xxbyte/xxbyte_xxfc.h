/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_XXFC_H
#define XXBYTE_XXBYTE_XXFC_H

/* xxfclib interoperability: text buffers and I/O devices. */

#include "xxbyte_common.h"

#include <xxfclib/buf/xx_buf.h>
#include <xxfclib/io/xx_io.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Listing flags. Bits not defined here must be zero. */
#define XXBYTE_LISTING_DEFAULT 0x0000u
#define XXBYTE_LISTING_NO_ADDRESS 0x0001u /* omit the address column */
#define XXBYTE_LISTING_NO_BYTES 0x0002u   /* omit the instruction-bytes column */
#define XXBYTE_LISTING_UPPERCASE 0x0004u  /* XXBYTE_FORMAT_UPPERCASE */
#define XXBYTE_LISTING_KNOWN_FLAGS 0x0007u

/* The largest region xxbyte_disasm_device reads into memory. */
#define XXBYTE_DEVICE_REGION_LIMIT (64u * 1024u * 1024u)

/* Appends the formatted instruction to out. Returns false on failure. */
XXBYTE_API int xxbyte_format_buf(const xxbyte_instruction *instruction, uint32_t flags, xxbyte_resolver resolver, void *context, xx_buf_t *out);

/*
 * Appends a linear-sweep listing of code[0..code_size) to out, one line per
 * instruction: address, bytes and text, separated by two spaces. Where
 * nothing decodes, one code unit (xxbyte_unit_size: one byte for most
 * families, 8 for eBPF, 28 for NSIS, ...) or the rest of the code if that is
 * shorter is listed as a "db 0xNN, 0xNN, ..." line, and the sweep continues
 * at the next unit. Returns XXBYTE_STATUS_OK unless the arguments are
 * invalid, the family or mode is unsupported, or out runs out of memory.
 */
XXBYTE_API xxbyte_status xxbyte_disasm_buf(xxbyte_family family, uint32_t mode, const uint8_t *code, size_t code_size, uint64_t address, uint32_t listing_flags,
                                           xxbyte_resolver resolver, void *context, xx_buf_t *out);

/* xxbyte_disasm_buf with a decode context for xxbyte_decode_ex. */
XXBYTE_API xxbyte_status xxbyte_disasm_buf_ex(xxbyte_family family, uint32_t mode, const void *decode_context, const uint8_t *code, size_t code_size, uint64_t address,
                                              uint32_t listing_flags, xxbyte_resolver resolver, void *context, xx_buf_t *out);

/*
 * Reads size bytes at offset from device and appends their listing as with
 * xxbyte_disasm_buf. A negative size reads to the end of the device. Returns
 * XXBYTE_STATUS_TRUNCATED when offset + size runs past the end of the device,
 * XXBYTE_STATUS_INVALID_ARGUMENT when offset is past the end or the region is
 * larger than XXBYTE_DEVICE_REGION_LIMIT (it is not clipped), and
 * XXBYTE_STATUS_IO_ERROR when the device size is unknown or a read fails.
 */
XXBYTE_API xxbyte_status xxbyte_disasm_device(xxbyte_family family, uint32_t mode, xx_io_device *device, int64_t offset, int64_t size, uint64_t address,
                                              uint32_t listing_flags, xxbyte_resolver resolver, void *context, xx_buf_t *out);

/* The mode argument of xxbyte_disasm_file that asks the family to take the
 * mode from the file (a .pyc magic, a Lua chunk header, a DEX version, ...). */
#define XXBYTE_MODE_AUTO 0xFFFFFFFFu

/* Nonzero when family is compiled in and can list whole container files. */
XXBYTE_API int xxbyte_file_supported(xxbyte_family family);

/*
 * Appends a listing of a whole container file in data[0..size) to out: every
 * code body the family's container holds (methods of a .class, .dex or .NET
 * assembly, functions of a .pyc, Lua chunk or Wasm module, and so on), each
 * under a header line naming it, with names resolved from the container
 * where it has them. The family header documents which containers it reads.
 * mode is XXBYTE_MODE_AUTO or a mode of the family that overrides what the
 * file says. Returns XXBYTE_STATUS_UNSUPPORTED_FAMILY when the family has no
 * container listing, XXBYTE_STATUS_INVALID_INSTRUCTION when data is not a
 * container the family reads, XXBYTE_STATUS_UNSUPPORTED_FORMAT when it is
 * a variant xxbyte cannot read (for example LZMA-compressed), and
 * XXBYTE_STATUS_TRUNCATED when it ends early,
 * and the statuses of xxbyte_disasm_buf otherwise. out may hold a partial
 * listing after a failure.
 */
XXBYTE_API xxbyte_status xxbyte_disasm_file(xxbyte_family family, uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#ifdef __cplusplus
}
#endif

#endif
