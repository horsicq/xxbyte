#ifndef XXBYTE_INTERNAL_H
#define XXBYTE_INTERNAL_H

#include "xxbyte/xxbyte.h"

#include <xxfclib/buf/xx_buf.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * Text output for family formatters. Use only %s %c %d %u %x %X %lld %llu
 * %llx %llX %zu with the '-' and '0' flags, width and precision: xxfclib's
 * own formatter backs xx_buf_appendf on some targets. Cast 64-bit values to
 * long long or unsigned long long; do not use the PRI macros. Floating-point
 * values go through xxbyte_fmt_double and xxbyte_fmt_float.
 */
typedef struct xxbyte_fmt {
    xx_buf_t *out;
    uint32_t flags;
    xxbyte_resolver resolver;
    void *context;
} xxbyte_fmt;

/*
 * One bytecode family. The core validates arguments, zeroes the instruction
 * and sets family, mode, address and code before calling decode, and it
 * prints the mnemonic before calling format_operands.
 */
typedef struct xxbyte_family_ops {
    xxbyte_family family;
    uint32_t default_mode;
    /* XXBYTE_STATUS_OK or XXBYTE_STATUS_UNSUPPORTED_MODE. */
    xxbyte_status (*check_mode)(uint32_t mode);
    /* code_size > 0 and mode already checked; context is the caller's
     * xxbyte_decode_ex context or NULL. On success set size, opcode, id,
     * group, mnemonic, operands and, for tables, table_count and
     * table_offset. */
    xxbyte_status (*decode)(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction);
    /* NULL when the family has no switch tables. index < table_count. */
    xxbyte_status (*switch_entry)(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target);
    /* Appends everything after the mnemonic, including the separating
     * space. Returns 0 on failure. */
    int (*format_operands)(const xxbyte_instruction *instruction, xxbyte_fmt *fmt);
    /* NULL for byte-aligned families; otherwise the code unit of a checked
     * mode (see xxbyte_unit_size). */
    size_t (*unit_size)(uint32_t mode);
    /* NULL when the family has no container listing. Lists a whole
     * container file (see xxbyte_disasm_file); mode is checked unless it is
     * XXBYTE_MODE_AUTO. */
    xxbyte_status (*list_file)(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);
} xxbyte_family_ops;

#if XXBYTE_USE_DOTNET
extern const xxbyte_family_ops xxbyte_dotnet_ops;
#endif
#if XXBYTE_USE_DEX
extern const xxbyte_family_ops xxbyte_dex_ops;
#endif
#if XXBYTE_USE_JAVA
extern const xxbyte_family_ops xxbyte_java_ops;
#endif
#if XXBYTE_USE_PYTHON
extern const xxbyte_family_ops xxbyte_python_ops;
#endif
#if XXBYTE_USE_VB
extern const xxbyte_family_ops xxbyte_vb_ops;
#endif
#if XXBYTE_USE_WASM
extern const xxbyte_family_ops xxbyte_wasm_ops;
#endif
#if XXBYTE_USE_EBPF
extern const xxbyte_family_ops xxbyte_ebpf_ops;
#endif
#if XXBYTE_USE_LUA
extern const xxbyte_family_ops xxbyte_lua_ops;
#endif
#if XXBYTE_USE_LUAJIT
extern const xxbyte_family_ops xxbyte_luajit_ops;
#endif
#if XXBYTE_USE_NSIS
extern const xxbyte_family_ops xxbyte_nsis_ops;
#endif
#if XXBYTE_USE_IFPS
extern const xxbyte_family_ops xxbyte_ifps_ops;
#endif
#if XXBYTE_USE_EVM
extern const xxbyte_family_ops xxbyte_evm_ops;
#endif
#if XXBYTE_USE_EBC
extern const xxbyte_family_ops xxbyte_ebc_ops;
#endif
#if XXBYTE_USE_YARV
extern const xxbyte_family_ops xxbyte_yarv_ops;
#endif
#if XXBYTE_USE_AVM2
extern const xxbyte_family_ops xxbyte_avm2_ops;
#endif
#if XXBYTE_USE_AVM1
extern const xxbyte_family_ops xxbyte_avm1_ops;
#endif
#if XXBYTE_USE_AUTOIT
extern const xxbyte_family_ops xxbyte_autoit_ops;
#endif
#if XXBYTE_USE_INSTALLSHIELD
extern const xxbyte_family_ops xxbyte_installshield_ops;
#endif
#if XXBYTE_USE_HERMES
extern const xxbyte_family_ops xxbyte_hermes_ops;
#endif
#if XXBYTE_USE_BEAM
extern const xxbyte_family_ops xxbyte_beam_ops;
#endif
#if XXBYTE_USE_OCAML
extern const xxbyte_family_ops xxbyte_ocaml_ops;
#endif
#if XXBYTE_USE_GDSCRIPT
extern const xxbyte_family_ops xxbyte_gdscript_ops;
#endif
#if XXBYTE_USE_SQUIRREL
extern const xxbyte_family_ops xxbyte_squirrel_ops;
#endif
#if XXBYTE_USE_PHP
extern const xxbyte_family_ops xxbyte_php_ops;
#endif
#if XXBYTE_USE_QUICKJS
extern const xxbyte_family_ops xxbyte_quickjs_ops;
#endif
#if XXBYTE_USE_V8
extern const xxbyte_family_ops xxbyte_v8_ops;
#endif

const xxbyte_family_ops *xxbyte_get_ops(xxbyte_family family);

/*
 * Decompresses one DEFLATE stream (RFC 1951) from in[0..in_size), or with
 * zlib nonzero one zlib stream (RFC 1950: header, no preset dictionary,
 * Adler-32 checked). Writes the output to out[0..out_capacity) and sets
 * *out_size to its length and, when in_used is not NULL, *in_used to the
 * number of input bytes consumed (including the zlib trailer).
 * Returns XXBYTE_STATUS_OK, XXBYTE_STATUS_TRUNCATED when the input ends
 * before the final block, XXBYTE_STATUS_OUT_OF_MEMORY when the output does
 * not fit in out_capacity (out then holds exactly the first out_capacity
 * bytes of the real output, so a caller may read a known-length prefix of a
 * longer stream), XXBYTE_STATUS_INVALID_INSTRUCTION for corrupt
 * data (or a bad zlib header or checksum) and XXBYTE_STATUS_INVALID_ARGUMENT
 * for NULL pointers. Never reads or writes out of bounds, on any input.
 */
xxbyte_status xxbyte_inflate(const uint8_t *in, size_t in_size, int zlib, uint8_t *out, size_t out_capacity, size_t *out_size, size_t *in_used);

/* Unaligned readers. Callers check bounds first. */
static inline uint16_t xxbyte_u16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t xxbyte_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t xxbyte_u64le(const uint8_t *p)
{
    return (uint64_t)xxbyte_u32le(p) | ((uint64_t)xxbyte_u32le(p + 4) << 32);
}

static inline uint16_t xxbyte_u16be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline uint32_t xxbyte_u32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Sign-extends the low bits of value. */
static inline int64_t xxbyte_sext(uint64_t value, unsigned bits)
{
    uint64_t sign = UINT64_C(1) << (bits - 1);
    value &= (bits >= 64) ? ~UINT64_C(0) : ((UINT64_C(1) << bits) - 1);
    return (int64_t)((value ^ sign) - sign);
}

/* Appends an operand; returns NULL once XXBYTE_MAX_OPERANDS are in use. */
xxbyte_operand *xxbyte_add_operand(xxbyte_instruction *instruction, xxbyte_operand_type type, uint32_t size, int64_t value);
xxbyte_operand *xxbyte_add_index(xxbyte_instruction *instruction, xxbyte_index_kind kind, uint32_t size, int64_t value);

/* Formatting helpers; all return 0 on allocation failure. */
int xxbyte_fmt_str(xxbyte_fmt *fmt, const char *text);
int xxbyte_fmt_char(xxbyte_fmt *fmt, char c);
#define xxbyte_fmt_printf(fmt, ...) (xx_buf_appendf((fmt)->out, __VA_ARGS__) ? 1 : 0)
/* The shortest text that reads back as the same double, independent of the
 * locale and the C library: positional for decimal exponents -4..15 ("0.1",
 * "100", "-0"), otherwise scientific ("1e+16", "2.5e-05"); NaN and the
 * infinities print as "nan", "inf" and "-inf" (src/xxbyte_float.c). */
int xxbyte_fmt_double(xxbyte_fmt *fmt, double value);
/* The same for a float32 ("0.1", not the widened double's
 * "0.10000000149011612"). */
int xxbyte_fmt_float(xxbyte_fmt *fmt, float value);
/* Appends the resolver's text for operand_index. Returns 1 when the resolver
 * supplied text, 0 when the caller should print the numeric form (nothing is
 * appended), and -1 on allocation failure. */
int xxbyte_fmt_resolve(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t operand_index);
/* Resolves an inline-table entry (switch target, list entry) that is not an
 * operand: the resolver sees entry as operand 0, the only operand, of a copy
 * of instruction. Same return values as xxbyte_fmt_resolve. */
int xxbyte_fmt_resolve_entry(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, const xxbyte_operand *entry);
/* Output position, and a rewind to it that drops text appended since. */
size_t xxbyte_fmt_mark(const xxbyte_fmt *fmt);
void xxbyte_fmt_rewind(xxbyte_fmt *fmt, size_t mark);

/* Appends mnemonic and operands to out; returns 0 on failure. */
int xxbyte_format_to_buf(const xxbyte_instruction *instruction, uint32_t flags, xxbyte_resolver resolver, void *context, xx_buf_t *out);

#endif
