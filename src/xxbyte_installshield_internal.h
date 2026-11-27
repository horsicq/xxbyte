/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_INSTALLSHIELD_INTERNAL_H
#define XXBYTE_INSTALLSHIELD_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_INSTALLSHIELD

#include "xxbyte/xxbyte_installshield.h"

/* The family's list_file (src/xxbyte_installshield_file.c). */
xxbyte_status xxbyte_is_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

/* Encoded size of the INX declaration list at data[pos..size), or 0 when it
 * runs past size. */
size_t xxbyte_is_decl_size(const uint8_t *data, size_t size, size_t pos);

#endif

#endif
