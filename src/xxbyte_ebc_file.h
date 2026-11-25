#ifndef XXBYTE_EBC_FILE_H
#define XXBYTE_EBC_FILE_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_EBC

/* The family's list_file (src/xxbyte_ebc_file.c). */
xxbyte_status xxbyte_ebc_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
