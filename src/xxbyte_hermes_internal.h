/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_HERMES_INTERNAL_H
#define XXBYTE_HERMES_INTERNAL_H

#include "xxbyte_hermes_tables.h"

#if XXBYTE_USE_HERMES

/* The family's list_file (src/xxbyte_hermes_file.c). */
xxbyte_status xxbyte_hermes_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
