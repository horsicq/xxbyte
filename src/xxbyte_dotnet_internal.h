/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_DOTNET_INTERNAL_H
#define XXBYTE_DOTNET_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_DOTNET

#include "xxbyte/xxbyte_dotnet.h"

/* xxbyte_dotnet_names.c: the list_file entry of the ops table. */
xxbyte_status xxbyte_dotnet_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

/* xxbyte_dotnet_meta.c: column value of a row whose bounds were checked. */
uint32_t xxbyte_dotnet_column(const xxbyte_dotnet_image *image, xxbyte_dotnet_table table, uint32_t row, uint32_t column);

/* xxbyte_dotnet_get_method, without the owner lookup when find_owner is 0. */
xxbyte_status xxbyte_dotnet_read_method(const xxbyte_dotnet_image *image, uint32_t row, xxbyte_dotnet_method *method, int find_owner);

#endif

#endif
