#ifndef XXBYTE_VB_FILE_H
#define XXBYTE_VB_FILE_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_VB

/* The family's list_file (src/xxbyte_vb_file.c). */
xxbyte_status xxbyte_vb_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
