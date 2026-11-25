#ifndef XXBYTE_EVM_INTERNAL_H
#define XXBYTE_EVM_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_EVM

/* The family's list_file (src/xxbyte_evm_file.c). */
xxbyte_status xxbyte_evm_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
