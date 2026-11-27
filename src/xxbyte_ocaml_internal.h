/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_OCAML_INTERNAL_H
#define XXBYTE_OCAML_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_OCAML

/* The list_file of the OCaml family (src/xxbyte_ocaml_file.c). */
xxbyte_status xxbyte_ocaml_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
