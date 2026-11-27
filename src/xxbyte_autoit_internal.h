/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_AUTOIT_INTERNAL_H
#define XXBYTE_AUTOIT_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_AUTOIT

extern const char *const xxbyte_autoit_keywords[XXBYTE_AUTOIT_KEYWORD_COUNT];
extern const char *const xxbyte_autoit_functions[XXBYTE_AUTOIT_FUNCTION_COUNT];
extern const char *const xxbyte_autoit_macros[XXBYTE_AUTOIT_MACRO_COUNT];

/* The table entry equal to the n UTF-16 units at p ignoring ASCII case, or
 * NULL. */
const char *xxbyte_autoit_lookup(const char *const *table, size_t count, const uint8_t *p, uint32_t n);

/* Appends a token's text as it reads in source: names with their sigil and
 * canonical case, strings quoted, numbers as literals. 0 on failure. */
int xxbyte_autoit_token_source(const xxbyte_instruction *instruction, xxbyte_fmt *fmt);

xxbyte_status xxbyte_autoit_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
