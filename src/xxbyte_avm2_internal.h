#ifndef XXBYTE_AVM2_INTERNAL_H
#define XXBYTE_AVM2_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_AVM2

/* The list_file of the family (src/xxbyte_avm2_file.c). */
xxbyte_status xxbyte_avm2_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

/* Appends the escaped, double-quoted text of bytes to out (the string
 * syntax of xxbyte_avm2_format_string). Returns 0 on allocation failure. */
int xxbyte_avm2_append_quoted(xx_buf_t *out, const uint8_t *bytes, size_t length);

#endif

#endif
