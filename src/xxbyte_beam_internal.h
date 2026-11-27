/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_BEAM_INTERNAL_H
#define XXBYTE_BEAM_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_BEAM

/* Group classes of the opcode table. */
enum {
    BEAM_G_NONE = 0,
    BEAM_G_THROWSTOP,
    BEAM_G_STOP,
    BEAM_G_CALL,
    BEAM_G_TAIL,
    BEAM_G_CALLCOND,
    BEAM_G_RETURN,
    BEAM_G_JUMP,
    BEAM_G_SWITCH,
    BEAM_G_COND
};

/* The table a u operand indexes. */
enum {
    BEAM_ROLE_NONE = 0,
    BEAM_ROLE_IMPORT,
    BEAM_ROLE_LAMBDA,
    BEAM_ROLE_STRING,
    BEAM_ROLE_LINE
};

typedef struct xxbyte_beam_opinfo {
    const char *name;
    uint8_t arity;
    uint8_t since;      /* OTP release that added the opcode (17 = older) */
    uint8_t deprecated; /* OTP release that deprecated it, 0 = current */
    uint8_t group;      /* BEAM_G_* */
    uint8_t role;       /* BEAM_ROLE_* */
    uint8_t role_arg;   /* operand index of the role */
} xxbyte_beam_opinfo;

extern const xxbyte_beam_opinfo xxbyte_beam_opinfo_table[XXBYTE_BEAM_OPCODE_MAX + 1];

/* Mode check helpers shared by the decoder and the container code. */
uint32_t xxbyte_beam_release(uint32_t mode);

/* Terms: decoded compact terms with their position, for the formatter. */
typedef struct beam_term {
    xxbyte_operand op; /* operand model (labels as INDEX LABEL) */
    size_t start;      /* offset of the term */
    size_t end;        /* offset after the term (list: after the count) */
} beam_term;

xxbyte_status xxbyte_beam_term_at(uint32_t mode, const uint8_t *code, size_t code_size, size_t offset, int in_list, beam_term *term);

/* Appends the decimal text of a big-endian integer (two's complement when
 * is_signed). */
int xxbyte_beam_fmt_bigint(xxbyte_fmt *fmt, const uint8_t *bytes, size_t size, int is_signed);
/* Appends an Erlang float as float_to_list(F, [short]) prints it. */
int xxbyte_beam_fmt_erlang_float(xxbyte_fmt *fmt, double value);
/* Appends an atom name (UTF-8 or Latin-1 bytes) as io_lib:write_atom_as_latin1
 * quotes it. */
int xxbyte_beam_fmt_atom(xxbyte_fmt *fmt, const uint8_t *text, size_t length, int utf8);
/* Appends an external term format term (data at etf, starting with the 131
 * version byte) as io_lib:format("~kp") prints it (no line breaks). *used
 * receives the bytes read. Returns 1, 0 on allocation failure or -1 for a
 * malformed term (the output then holds a partial text). */
int xxbyte_beam_fmt_etf(xxbyte_fmt *fmt, const uint8_t *etf, size_t size, size_t *used);
/* Skips the term at data[*pos] (no version byte): 1, or 0 when malformed
 * or not printable. */
int xxbyte_beam_etf_skip(const uint8_t *data, size_t size, size_t *pos);
/* Appends a Type chunk entry (newest format) as type text. */
int xxbyte_beam_fmt_type(xxbyte_fmt *fmt, uint32_t bits, int has_min, int64_t min, int has_max, int64_t max, int has_unit, uint32_t unit);

/* Lists a .beam file (xxbyte_disasm_file). */
xxbyte_status xxbyte_beam_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

/* The listing's resolver: format_operands prints full-length resolved
 * literals and strings itself when fmt->resolver is this function. */
size_t xxbyte_beam_listing_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);
/* Writes the resolved text of an operand (or list entry) for the listing
 * context directly to fmt. Returns 1 when written, 0 to use the numeric
 * form, -1 on allocation failure. */
int xxbyte_beam_listing_write(void *context, const xxbyte_instruction *instruction, const xxbyte_operand *operand, uint32_t operand_index, uint32_t list_index,
                              xxbyte_fmt *fmt);

#endif

#endif
