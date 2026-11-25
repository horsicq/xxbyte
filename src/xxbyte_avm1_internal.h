#ifndef XXBYTE_AVM1_INTERNAL_H
#define XXBYTE_AVM1_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_AVM1

/* Appends the quoted, escaped form of text[0..length) (see the family
 * header); returns 0 on allocation failure. */
int xxbyte_avm1_append_string(xx_buf_t *out, const uint8_t *text, size_t length);

/* The family's list_file (src/xxbyte_avm1_swf.c). */
xxbyte_status xxbyte_avm1_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
