#ifndef XXBYTE_LUAJIT_DUMP_H
#define XXBYTE_LUAJIT_DUMP_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_LUAJIT

/* Dump readers shared by src/xxbyte_luajit.c and src/xxbyte_luajit_list.c. */

/* A ULEB128 of at most 5 bytes from data[*pos..end); value may be NULL to
 * skip one, otherwise it must fit 32 bits. */
xxbyte_status xxbyte_luajit_read_uleb(const uint8_t *data, size_t end, size_t *pos, uint32_t *value);

/* Skips one template-table key or value. */
xxbyte_status xxbyte_luajit_skip_ktabk(const uint8_t *data, size_t end, size_t *pos);

/* Appends luajit -bl's "; ..." comment for instruction from the
 * xxbyte_luajit_names in fmt->context. Returns 0 on allocation failure. */
int xxbyte_luajit_bl_comment(const xxbyte_instruction *instruction, xxbyte_fmt *fmt);

/* The family's list_file (see xxbyte_disasm_file). */
xxbyte_status xxbyte_luajit_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
