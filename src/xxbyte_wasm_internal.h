#ifndef XXBYTE_WASM_INTERNAL_H
#define XXBYTE_WASM_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_WASM

/* Reads a value type at data[*pos..end) as the decoder does (an operand as
 * described in xxbyte_wasm.h) and advances *pos past it on success. */
xxbyte_status xxbyte_wasm_read_value_type_at(uint32_t mode, const uint8_t *data, size_t end, size_t *pos, xxbyte_operand *type);

/* The family's list_file (src/xxbyte_wasm_module.c). */
xxbyte_status xxbyte_wasm_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
