#ifndef XXBYTE_YARV_IBF_H
#define XXBYTE_YARV_IBF_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_YARV

#define YARV_MODE(major, minor) XXBYTE_YARV_MODE(major, minor)
#define YARV_VERSION(mode) ((uint32_t)(mode) & XXBYTE_YARV_MODE_VERSION_MASK)

/* Ruby's builtin encoding indexes (encindex.h, 2.6 to 4.0). */
#define YARV_ENC_ASCII_8BIT 0u
#define YARV_ENC_UTF_8 1u
#define YARV_ENC_US_ASCII 2u
#define YARV_ENC_UTF_16BE 3u
#define YARV_ENC_UTF_16LE 4u
#define YARV_ENC_UTF_32BE 5u
#define YARV_ENC_UTF_32LE 6u
#define YARV_ENC_UTF_16 7u
#define YARV_ENC_UTF_32 8u
#define YARV_ENC_UTF8_MAC 9u
#define YARV_ENC_EUC_JP 10u
#define YARV_ENC_WINDOWS_31J 11u
#define YARV_ENC_BUILTIN_MAX 12u

/* Upper bounds that keep hostile images cheap. */
#define YARV_MAX_DEPTH 32u             /* nested objects in one inspect */
#define YARV_MAX_NODES (1024u * 1024u) /* objects visited by one inspect */
#define YARV_MAX_BIGNUM_WORDS 1024u
#define YARV_MAX_TEXT 8192u /* bytes one inspect produces */

/* An image whose header xxbyte_yarv_parse_ibf_header accepted. Every read
 * is checked against size, the image size clipped to the input. */
typedef struct yarv_image {
    const uint8_t *data;
    uint32_t size;
    xxbyte_yarv_ibf_header header;
    uint32_t version; /* YARV_MODE(major, minor) */
    unsigned word;    /* sizeof(VALUE) */
    unsigned lng;     /* sizeof(long): 4 on 64-bit Windows (2.6 to 3.2) */
    int big;
} yarv_image;

int yarv_image_init(yarv_image *image, const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header);

/* Readers; all return 0 when the value does not lie within the image. */
int yarv_read_small(const yarv_image *image, uint32_t *pos, uint64_t *value);
int yarv_read_byte(const yarv_image *image, uint32_t *pos, uint8_t *value);
int yarv_read_word(const yarv_image *image, uint64_t offset, unsigned width, uint64_t *value);
int yarv_read_i32(const yarv_image *image, uint64_t offset, int32_t *value);

/* Object list access. */
int yarv_object(const yarv_image *image, uint64_t index, xxbyte_yarv_object *object);
/* The bytes of a String or Symbol object (up to 3.0 a Symbol goes
 * through its String). */
int yarv_string_bytes(const yarv_image *image, const xxbyte_yarv_object *object, const uint8_t **bytes, uint32_t *length, uint32_t *encoding);
/* Element i of an Array (a Hash stores key, value, key, value, ...). */
typedef struct yarv_elements {
    const yarv_image *image;
    const xxbyte_yarv_object *object;
    uint64_t next;
    uint32_t pos;
} yarv_elements;
void yarv_elements_init(yarv_elements *it, const yarv_image *image, const xxbyte_yarv_object *object);
int yarv_elements_next(yarv_elements *it, uint64_t *index);
/* The name object of an ID operand (a Symbol, or a String in 2.6); 0 for
 * no name. */
int yarv_id_object(const yarv_image *image, uint64_t id, uint64_t *object_index);

int yarv_iseq_info(const yarv_image *image, uint32_t index, xxbyte_yarv_iseq_info *info, xxbyte_status *status);

/* Text (src/xxbyte_yarv_text.c). Every function appends to out and returns
 * 0 only when out ran out of memory or the text budget ran out. */
typedef struct yarv_text {
    const yarv_image *image;
    xx_buf_t *out;
    size_t limit; /* stop appending beyond this many bytes of out */
    uint32_t nodes;
} yarv_text;

void yarv_text_init(yarv_text *text, const yarv_image *image, xx_buf_t *out, size_t limit);
int yarv_text_full(const yarv_text *text);
int yarv_put(yarv_text *text, const char *s);
int yarv_put_bytes(yarv_text *text, const uint8_t *bytes, size_t length);
int yarv_inspect(yarv_text *text, uint64_t index, unsigned depth);
/* rb_dump_literal of an operand object (classes by name). */
int yarv_literal(yarv_text *text, uint64_t index);
/* Symbol#inspect of a name object. */
int yarv_inspect_symbol_name(yarv_text *text, uint64_t index);
/* The raw bytes of a String or Symbol object; "?" when it is not one. */
int yarv_put_name(yarv_text *text, uint64_t index);
/* A local variable name as local_var_name prints it, without "@N". */
int yarv_local_name(yarv_text *text, uint64_t index);
/* The constant path of opt_getconstant_path ("A::B"). */
int yarv_constant_path(yarv_text *text, uint64_t index);

/* Onigmo's Unicode Print class in the Unicode version of the release
 * (src/xxbyte_yarv_unicode.c). */
int yarv_unicode_printable(uint32_t c, uint32_t version);

/* Listing (src/xxbyte_yarv_names.c). */
xxbyte_status yarv_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
