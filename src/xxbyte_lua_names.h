/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_LUA_NAMES_H
#define XXBYTE_LUA_NAMES_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_LUA

/* Appends the comments luac -l -l prints after instruction's columns,
 * from the xxbyte_lua_names in fmt->context (src/xxbyte_lua_list.c).
 * Returns 0 on allocation failure. */
int xxbyte_lua_luac_comment(const xxbyte_instruction *instruction, xxbyte_fmt *fmt);

/* The family's list_file (see xxbyte_disasm_file). */
xxbyte_status xxbyte_lua_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
