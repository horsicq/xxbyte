/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_SQUIRREL_INTERNAL_H
#define XXBYTE_SQUIRREL_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_SQUIRREL

#include <xxfclib/buf/xx_buf.h>

/* The resolver xxbyte_disasm_file uses; context is a squirrel_names. */
typedef struct squirrel_names {
    const uint8_t *data;
    size_t size;
    const xxbyte_squirrel_header *header;
    const xxbyte_squirrel_function *function;
    const xxbyte_squirrel_object *literals; /* function->literal_count */
    const xxbyte_squirrel_local *locals;    /* function->local_count, sorted
                                               by (pos, start_op) */
    const xxbyte_squirrel_outer *outers;    /* function->outer_count */
    const uint64_t *children;               /* name object offsets of the
                                               nested functions, or 0 */
} squirrel_names;

size_t xxbyte_squirrel_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

xxbyte_status xxbyte_squirrel_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

/* Checks a decode mode; nonzero when supported. */
int xxbyte_squirrel_mode_ok(uint32_t mode);

#endif

#endif
