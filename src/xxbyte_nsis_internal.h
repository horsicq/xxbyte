#ifndef XXBYTE_NSIS_INTERNAL_H
#define XXBYTE_NSIS_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_NSIS

/* Nonzero when mode is a supported NSIS mode. */
int xxbyte_nsis_mode_valid(uint32_t mode);

/* The highest parameter index + 1 that makensis writes for id (6 for the
 * entries that some versions pad with leftovers), 0 for an invalid id. */
uint32_t xxbyte_nsis_param_limit(uint32_t id);

/* Nonzero for the NSIS 3 variants (V300, V305, V306), and for Jim Park's
 * Unicode NSIS variants (PARK1 to PARK3). */
int xxbyte_nsis_variant_is_nsis3(uint32_t variant);
int xxbyte_nsis_variant_is_park(uint32_t variant);

/* The list_file of the family ops table (src/xxbyte_nsis_list.c). */
xxbyte_status xxbyte_nsis_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
