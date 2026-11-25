#ifndef XXBYTE_QUICKJS_INTERNAL_H
#define XXBYTE_QUICKJS_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_QUICKJS

#include "xxbyte_quickjs_tables.h"

/* One opcode of a release: mnemonic id, size, format, stack effect, the
 * implicit operand values (-128 when absent) and the group. */
typedef struct qjs_op {
    uint16_t id;
    uint8_t size;
    uint8_t fmt;
    uint8_t n_pop;
    uint8_t n_push;
    int8_t imp0;
    int8_t imp1;
    uint32_t group;
} qjs_op;

/* JS_WriteObject layout differences between releases. */
#define QJS_F_BE_FLAG 0x00000001u         /* version byte | 0x40 = big-endian */
#define QJS_F_CHECKSUM 0x00000002u        /* u32 checksum after the version */
#define QJS_F_ATOMS_TYPED 0x00000004u     /* atom entries start with a type byte */
#define QJS_F_ATOMS_CONST 0x00000008u     /* type & 0x80: const atom (ng 0.17+) */
#define QJS_F_DEBUG_BIT11 0x00000010u     /* has_debug_info is flag bit 11 */
#define QJS_F_DEBUG_ALWAYS 0x00000020u    /* debug info always present */
#define QJS_F_VAR_REF_COUNT 0x00000040u   /* var_ref_count after stack_size */
#define QJS_F_VD_SCOPE_LEVEL 0x00000080u  /* vardef scope_level */
#define QJS_F_VD_REF_FIRST 0x00000100u    /* vardef var_ref_idx before flags */
#define QJS_F_VD_HAS_SCOPE 0x00000200u    /* vardef flags bit 7: has_scope */
#define QJS_F_VD_REF_CAPTURED 0x00000400u /* vardef var_ref_idx if captured */
#define QJS_F_CV_U16 0x00000800u          /* closure var flags: u16 */
#define QJS_F_CV_LEB 0x00001000u          /* closure var flags: leb128 */
#define QJS_F_CPOOL_FIRST 0x00002000u     /* constant pool before bytecode */
#define QJS_F_LINE_NUM 0x00004000u        /* debug line_num */
#define QJS_F_COL_NUM 0x00008000u         /* debug col_num */
#define QJS_F_SOURCE 0x00010000u          /* debug source text */
#define QJS_F_IC_IN_DEBUG 0x00020000u     /* ng 0.2 inline-cache trailer */
#define QJS_F_IC_AFTER_DEBUG 0x00040000u  /* ng 0.3/0.4 inline-cache trailer */
#define QJS_F_MOD_ATTR 0x00080000u        /* module request attributes */
#define QJS_F_MOD_IS_STAR 0x00100000u     /* module import is_star byte */
#define QJS_F_MOD_TLA 0x00200000u         /* module has_tla byte */
#define QJS_F_TAGS_BIGFLOAT 0x00400000u   /* BIG_FLOAT/BIG_DECIMAL tags */
#define QJS_F_TAG_REGEXP 0x00800000u      /* BC_TAG_REGEXP */
#define QJS_F_TAG_MAPSET 0x01000000u      /* BC_TAG_MAP, BC_TAG_SET */
#define QJS_F_TAG_SYMBOL 0x02000000u      /* BC_TAG_SYMBOL */
#define QJS_F_BIGINT_BF 0x04000000u       /* libbf BigInt encoding */
#define QJS_F_AB_MAX 0x08000000u          /* array buffer max_byte_length */

/* Function flag word layouts (names only). */
enum {
    QJS_FNFLAGS_BELLARD21, /* ... has_debug, backtrace_barrier */
    QJS_FNFLAGS_BELLARD24, /* ... has_debug, backtrace_barrier, is_direct_or_indirect_eval */
    QJS_FNFLAGS_BELLARD25, /* ... has_debug, is_direct_or_indirect_eval */
    QJS_FNFLAGS_NG3,       /* ... backtrace_barrier */
    QJS_FNFLAGS_NG5        /* ... backtrace_barrier, has_debug_info */
};

typedef struct qjs_version {
    uint32_t mode;
    const char *name;
    uint32_t bc_version;
    uint32_t flags;
    const uint16_t *ops; /* opcode -> xxbyte_quickjs_ops_desc index */
    uint32_t op_count;
    const uint16_t *atoms; /* predefined atom - 1 -> string */
    uint32_t atom_count;   /* JS_ATOM_END - 1 */
    uint32_t fnflags;
} qjs_version;

typedef struct qjs_mode_alias {
    uint32_t mode;
    uint32_t version;
} qjs_mode_alias;

extern const char *const xxbyte_quickjs_mnemonics[XXBYTE_QUICKJS_ID_COUNT];
extern const qjs_op xxbyte_quickjs_ops_desc[QJS_DESC_COUNT];
extern const qjs_version xxbyte_quickjs_versions[QJS_VERSION_COUNT];
extern const qjs_mode_alias xxbyte_quickjs_mode_aliases[QJS_MODE_ALIAS_COUNT];

/* Predefined atom text, NULL if out of range. */
const char *qjs_builtin_text(const qjs_version *version, uint32_t atom);
/* The release of a mode, NULL if unsupported. */
const qjs_version *qjs_find_version(uint32_t mode);
/* Appends an atom's text as the dump prints it: identifiers bare, anything
 * else quoted with escapes. Bytes are 8-bit (wide = 0) or UTF-16LE code
 * units (wide = 1). Returns 0 on allocation failure. */
int qjs_put_atom_text(xx_buf_t *out, const uint8_t *bytes, size_t length, int wide);
/* Appends a string always quoted and escaped, at most limit units, "..."
 * marking a cut. Returns 0 on allocation failure. */
int qjs_put_quoted(xx_buf_t *out, const uint8_t *bytes, size_t length, int wide, size_t limit);
/* The family's list_file (src/xxbyte_quickjs_file.c). */
xxbyte_status xxbyte_quickjs_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

#endif

#endif
