/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_GDSCRIPT_INTERNAL_H
#define XXBYTE_GDSCRIPT_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_GDSCRIPT

typedef struct xxbyte_gdscript_token_info {
    const char *mnemonic;
    const char *source; /* NULL: text from a table, or none */
} xxbyte_gdscript_token_info;

/* Indexed by id. */
extern const xxbyte_gdscript_token_info xxbyte_gdscript_tokens[XXBYTE_GDSCRIPT_ID_COUNT];

/* The id of token type in mode (DEFAULT allowed), 0 when the type is past
 * the mode's table. */
uint32_t xxbyte_gdscript_token_id(uint32_t mode, uint32_t type);
/* The built-in function name of index in a 3.x mode, NULL past the table. */
const char *xxbyte_gdscript_builtin_name(uint32_t mode, uint32_t index);
/* The 3.x source name of Variant::Type index, NULL past VARIANT_MAX. */
const char *xxbyte_gdscript_v3_type_name(uint32_t index);

/*
 * Zstandard (RFC 8878) single-frame decoder: in[0..in_size) must be one
 * frame, without a dictionary, that inflates to exactly out_size bytes.
 * Returns OK, TRUNCATED when the input ends early, INVALID_INSTRUCTION for
 * corrupt data or a size mismatch. Never reads or writes out of bounds.
 */
xxbyte_status xxbyte_gdscript_zstd(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_size);

/* The family's list_file (src/xxbyte_gdscript_file.c). */
xxbyte_status xxbyte_gdscript_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
