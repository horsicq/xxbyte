#ifndef XXBYTE_PYTHON_MARSHAL_H
#define XXBYTE_PYTHON_MARSHAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_PYTHON

/*
 * The marshal tree of a .pyc file. Every object read from the stream is one
 * node; references (FLAG_REF/TYPE_REF from marshal version 3, interned
 * strings and TYPE_STRINGREF in 2.x) point at the node they name, so the
 * tree is a DAG (a tuple, list, dict or set may even contain itself). Node
 * payloads (string bytes, long digits, float text) stay in the caller's
 * buffer and are addressed by offset.
 */

#define PYM_NONE_INDEX 0xFFFFFFFFu

enum pym_kind {
    PYM_NULL = 0,
    PYM_NONE,
    PYM_TRUE,
    PYM_FALSE,
    PYM_ELLIPSIS,
    PYM_STOPITER,
    PYM_INT,          /* v.i */
    PYM_LONG,         /* digits at data, size 15-bit digits, flags NEG */
    PYM_FLOAT,        /* v.d[0] */
    PYM_FLOAT_TEXT,   /* text at data, size bytes */
    PYM_COMPLEX,      /* v.d[0], v.d[1] */
    PYM_COMPLEX_TEXT, /* real text at data/size, imaginary at first/extra */
    PYM_BYTES,        /* data, size; Python 2 str, Python 3 bytes */
    PYM_STR,          /* data, size; flags LATIN1, PY2_UNICODE */
    PYM_TUPLE,        /* kids[first .. first + size) */
    PYM_LIST,
    PYM_DICT, /* size pairs: kids[first + 2i] key, [first + 2i + 1] value */
    PYM_SET,
    PYM_FROZENSET,
    PYM_CODE, /* codes[first] */
    PYM_SLICE /* kids[first .. first + 3) */
};

#define PYM_FLAG_NEG 0x01u         /* PYM_LONG: negative */
#define PYM_FLAG_LATIN1 0x02u      /* PYM_STR: one byte per code point */
#define PYM_FLAG_PY2_UNICODE 0x04u /* PYM_STR: a Python 2 unicode object */
#define PYM_FLAG_INT64 0x08u       /* PYM_INT: read from TYPE_INT64 */
#define PYM_FLAG_DQUOTE 0x10u      /* PYM_STR, PYM_BYTES: repr uses " (has ' and no ") */

typedef struct pym_node {
    uint8_t kind;
    uint8_t flags;
    uint8_t type; /* marshal type code without FLAG_REF */
    uint8_t reserved;
    uint32_t offset; /* of the type byte */
    uint32_t data;
    uint32_t size;
    uint32_t first;
    uint32_t extra;
    union {
        int64_t i;
        double d[2];
    } v;
} pym_node;

/* One code object. Object fields are node indices, PYM_NONE_INDEX when the
 * layout has no such field. From 3.11 varnames holds co_localsplusnames
 * and kinds co_localspluskinds. */
typedef struct pym_code {
    uint32_t node;
    int32_t argcount;
    int32_t posonlyargcount;
    int32_t kwonlyargcount;
    int32_t nlocals;
    int32_t stacksize;
    int32_t firstlineno;
    uint32_t flags;
    uint32_t code;
    uint32_t consts;
    uint32_t names;
    uint32_t varnames;
    uint32_t freevars;
    uint32_t cellvars;
    uint32_t kinds;
    uint32_t filename;
    uint32_t name;
    uint32_t qualname;
    uint32_t linetable;
    uint32_t exceptiontable;
} pym_code;

/* One entry of the code-object walk, in the order dis prints them. */
typedef struct pym_walk {
    uint32_t code; /* index into codes */
    uint32_t parent;
    uint32_t depth;
} pym_walk;

typedef struct pym_tree {
    const uint8_t *data;
    size_t size;
    uint32_t mode;
    uint32_t magic;
    uint32_t root; /* node index */
    uint32_t end;  /* offset just past the root object */
    pym_node *nodes;
    size_t node_count;
    size_t node_capacity;
    uint32_t *kids;
    size_t kid_count;
    size_t kid_capacity;
    pym_code *codes;
    size_t code_count;
    size_t code_capacity;
    pym_walk *walk;
    size_t walk_count;
    int walk_truncated;
} pym_tree;

/* Parses the marshal stream at data[offset] with the reader of mode and
 * the code-object layout of magic. On failure the tree is released. */
xxbyte_status xxbyte_pym_parse(pym_tree *tree, uint32_t mode, uint32_t magic, const uint8_t *data, size_t size, size_t offset);
void xxbyte_pym_free(pym_tree *tree);

/* Item i of a tuple or list node (Python negative indexing once), or
 * PYM_NONE_INDEX. count receives the item count when not NULL. */
uint32_t xxbyte_pym_item(const pym_tree *tree, uint32_t node, int64_t index);
uint32_t xxbyte_pym_length(const pym_tree *tree, uint32_t node);

/* The last magic number of mode (its release's marshal layout), 0 if none. */
uint32_t xxbyte_python_release_magic(uint32_t mode);

/* Nonzero when operand 0 of instruction names an existing empty string
 * for xxbyte_python_resolver with this file. */
int xxbyte_python_empty_name(const void *file, const xxbyte_instruction *instruction);

/* The family's list_file. */
xxbyte_status xxbyte_python_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

/* Nonzero when the code point is printable for the repr of Python 3.minor. */
int xxbyte_python_printable(uint32_t cp, uint32_t minor);

#endif

#endif
