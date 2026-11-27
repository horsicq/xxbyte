/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_DEX_FILE_H
#define XXBYTE_DEX_FILE_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_DEX

/* The list_file entry of xxbyte_dex_ops (src/xxbyte_dex_file.c). */
xxbyte_status xxbyte_dex_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
