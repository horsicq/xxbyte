#ifndef XXBYTE_IFPS_INTERNAL_H
#define XXBYTE_IFPS_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_IFPS

#include "xxbyte/xxbyte_ifps.h"

/* XXBYTE_STATUS_OK for the modes of xxbyte_ifps.h. */
xxbyte_status xxbyte_ifps_check_mode(uint32_t mode);

/* SizeOf(Extended) of a checked mode: 10, or 8 in 64-bit mode. */
uint32_t xxbyte_ifps_extended_size(uint32_t mode);

/* The 10 bytes of an x87 Extended at p as the x87 stores them to a
 * double (see xxbyte_ifps.h). */
uint64_t xxbyte_ifps_extended_bits(const uint8_t *p);

/* Immediate text without brackets: a Pascal string literal (wide: UTF-16LE
 * code units), the members of a set, a Currency value. 0 on failure. */
int xxbyte_ifps_format_string(xxbyte_fmt *fmt, const uint8_t *data, uint32_t size, int wide);
int xxbyte_ifps_format_set(xxbyte_fmt *fmt, const uint8_t *data, uint32_t size);
int xxbyte_ifps_format_currency(xxbyte_fmt *fmt, int64_t value);

/* The list_file of the ops table (src/xxbyte_ifps_file.c). */
xxbyte_status xxbyte_ifps_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
