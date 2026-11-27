/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_JAVA_INTERNAL_H
#define XXBYTE_JAVA_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_JAVA

#include "xxbyte/xxbyte_java.h"

/* Character.isJavaIdentifierStart and isJavaIdentifierPart of JDK 25
 * (src/xxbyte_java_unicode.c). */
int xxbyte_java_identifier_start(uint32_t cp);
int xxbyte_java_identifier_part(uint32_t cp);

/* Text builders behind the public javap-text functions; they append to out
 * and return 0 when there is no text (nothing appended) or on allocation
 * failure (check xx_buf_ok). */
int xxbyte_java_append_constant(xx_buf_t *out, const xxbyte_java_class *java_class, uint32_t index);
/* The javap form of the name of Class constant index ("java/lang/Object",
 * "\"[I\""). */
int xxbyte_java_append_class_name(xx_buf_t *out, const xxbyte_java_class *java_class, uint32_t index);
int xxbyte_java_append_header(xx_buf_t *out, const xxbyte_java_class *java_class, const xxbyte_java_member *method);
/* Float.toString / Double.toString of JDK 19 and later. */
int xxbyte_java_append_double(xx_buf_t *out, double value);
int xxbyte_java_append_float(xx_buf_t *out, float value);

/* The list_file entry of the ops table (src/xxbyte_java_list.c). */
xxbyte_status xxbyte_java_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
