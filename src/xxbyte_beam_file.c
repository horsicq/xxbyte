#include "xxbyte_beam_internal.h"

#if XXBYTE_USE_BEAM

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

/*
 * .beam containers: the IFF form, its tables, and the whole-file listing
 * (xxbyte_disasm_file). The function split and the resolved operand forms
 * follow lib/compiler/src/beam_disasm.erl.
 */

#define BEAM_INFLATE_LIMIT (256u * 1024u * 1024u)

static int beam_id(const uint8_t *p, const char *id)
{
    return memcmp(p, id, 4) == 0;
}

static xxbyte_status beam_check_count(const xxbyte_beam_chunk *chunk, uint32_t header, uint32_t entry, uint32_t *count, const uint8_t *data)
{
    uint32_t n;

    *count = 0;
    if (chunk->offset == 0) {
        return XXBYTE_STATUS_OK;
    }
    if (chunk->size < header) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    n = xxbyte_u32be(data + chunk->offset);
    if ((uint64_t)n * entry > chunk->size - header) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *count = n;
    return XXBYTE_STATUS_OK;
}

/* Reads a compact u term (the long-atom length). */
static int beam_read_u(const uint8_t *p, size_t size, size_t *off, uint64_t *value)
{
    xxbyte_operand op;

    if (xxbyte_beam_read_term(XXBYTE_BEAM_MODE_DEFAULT, p, size, off, 1, &op) != XXBYTE_STATUS_OK || XXBYTE_BEAM_OPERAND_TAG(&op) != XXBYTE_BEAM_TAG_U ||
        op.type != XXBYTE_OPERAND_IMMEDIATE) {
        return 0;
    }
    *value = (uint64_t)op.value;
    return 1;
}

/* Walks the atom table: calls back (if not NULL) for each atom and checks
 * the table. */
typedef int (*beam_atom_cb)(void *ctx, uint32_t index, uint32_t offset, uint32_t length);

static xxbyte_status beam_walk_atoms(const uint8_t *data, const xxbyte_beam_file *file, beam_atom_cb cb, void *ctx)
{
    const uint8_t *p = data + file->chunks[XXBYTE_BEAM_CHUNK_ATOMS].offset;
    size_t size = file->chunks[XXBYTE_BEAM_CHUNK_ATOMS].size;
    size_t off = 4;
    uint32_t i;

    for (i = 1; i <= file->atom_count; ++i) {
        uint64_t length;
        if (file->atoms_long) {
            if (!beam_read_u(p, size, &off, &length)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
        } else {
            if (off >= size) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            length = p[off++];
        }
        if (length > size - off) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (cb != NULL && !cb(ctx, i, (uint32_t)(file->chunks[XXBYTE_BEAM_CHUNK_ATOMS].offset + off), (uint32_t)length)) {
            return XXBYTE_STATUS_OK;
        }
        off += (size_t)length;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_beam_parse_file(const uint8_t *data, size_t size, xxbyte_beam_file *file)
{
    xxbyte_beam_file f;
    size_t form_end, off;
    xxbyte_status status;

    if (file != NULL) {
        memset(file, 0, sizeof(*file));
    }
    if (file == NULL || (data == NULL && size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size > UINT32_MAX) {
        size = UINT32_MAX;
    }
    memset(&f, 0, sizeof(f));
    if (size < 12) {
        return (size >= 4 && !beam_id(data, "FOR1")) ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_TRUNCATED;
    }
    if (!beam_id(data, "FOR1") || !beam_id(data + 8, "BEAM")) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if ((uint64_t)xxbyte_u32be(data + 4) + 8u > size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    form_end = (size_t)xxbyte_u32be(data + 4) + 8u;
    if (form_end < 12) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    f.size = (uint32_t)form_end;
    off = 12;
    while (off < form_end) {
        uint32_t length;
        int slot = -1;
        const uint8_t *id;

        if (form_end - off < 8) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        id = data + off;
        length = xxbyte_u32be(data + off + 4);
        if (length > form_end - off - 8) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (beam_id(id, "AtU8")) {
            slot = XXBYTE_BEAM_CHUNK_ATOMS;
            f.atoms_utf8 = 1;
        } else if (beam_id(id, "Atom")) {
            if (f.chunks[XXBYTE_BEAM_CHUNK_ATOMS].offset == 0) {
                slot = XXBYTE_BEAM_CHUNK_ATOMS;
            }
        } else if (beam_id(id, "Code")) {
            slot = XXBYTE_BEAM_CHUNK_CODE;
        } else if (beam_id(id, "StrT")) {
            slot = XXBYTE_BEAM_CHUNK_STRT;
        } else if (beam_id(id, "ImpT")) {
            slot = XXBYTE_BEAM_CHUNK_IMPT;
        } else if (beam_id(id, "ExpT")) {
            slot = XXBYTE_BEAM_CHUNK_EXPT;
        } else if (beam_id(id, "LocT")) {
            slot = XXBYTE_BEAM_CHUNK_LOCT;
        } else if (beam_id(id, "FunT")) {
            slot = XXBYTE_BEAM_CHUNK_FUNT;
        } else if (beam_id(id, "LitT")) {
            slot = XXBYTE_BEAM_CHUNK_LITT;
        } else if (beam_id(id, "Line")) {
            slot = XXBYTE_BEAM_CHUNK_LINE;
        } else if (beam_id(id, "Type")) {
            slot = XXBYTE_BEAM_CHUNK_TYPE;
        } else if (beam_id(id, "CInf")) {
            slot = XXBYTE_BEAM_CHUNK_CINF;
        } else if (beam_id(id, "Attr")) {
            slot = XXBYTE_BEAM_CHUNK_ATTR;
        }
        if (slot >= 0 && (f.chunks[slot].offset == 0 || slot == XXBYTE_BEAM_CHUNK_ATOMS)) {
            if (slot == XXBYTE_BEAM_CHUNK_ATOMS && f.chunks[slot].offset != 0 && !beam_id(id, "AtU8")) {
                /* keep the first */
            } else {
                f.chunks[slot].offset = (uint32_t)(off + 8);
                f.chunks[slot].size = length;
            }
        }
        ++f.chunk_count;
        off += 8u + (size_t)length;
        off = (off + 3u) & ~(size_t)3u;
    }
    if (f.chunks[XXBYTE_BEAM_CHUNK_CODE].offset == 0 || f.chunks[XXBYTE_BEAM_CHUNK_ATOMS].offset == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    /* atoms */
    {
        const xxbyte_beam_chunk *c = &f.chunks[XXBYTE_BEAM_CHUNK_ATOMS];
        int32_t count;
        if (c->size < 4) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        count = (int32_t)xxbyte_u32be(data + c->offset);
        if (count < 0) {
            if (!f.atoms_utf8 || count == INT32_MIN) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            f.atoms_long = 1;
            count = -count;
        }
        if ((uint32_t)count > c->size - 4) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        f.atom_count = (uint32_t)count;
        status = beam_walk_atoms(data, &f, NULL, NULL);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    /* code */
    {
        const xxbyte_beam_chunk *c = &f.chunks[XXBYTE_BEAM_CHUNK_CODE];
        uint32_t sub;
        if (c->size < 20) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        sub = xxbyte_u32be(data + c->offset);
        if (sub < 16 || sub > c->size - 4) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        f.instruction_set = xxbyte_u32be(data + c->offset + 4);
        f.max_opcode = xxbyte_u32be(data + c->offset + 8);
        f.label_count = xxbyte_u32be(data + c->offset + 12);
        f.function_count = xxbyte_u32be(data + c->offset + 16);
        if (f.instruction_set != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        f.code_offset = c->offset + 4 + sub;
        f.code_size = c->size - 4 - sub;
    }
    status = beam_check_count(&f.chunks[XXBYTE_BEAM_CHUNK_IMPT], 4, 12, &f.import_count, data);
    if (status == XXBYTE_STATUS_OK) {
        status = beam_check_count(&f.chunks[XXBYTE_BEAM_CHUNK_EXPT], 4, 12, &f.export_count, data);
    }
    if (status == XXBYTE_STATUS_OK) {
        status = beam_check_count(&f.chunks[XXBYTE_BEAM_CHUNK_LOCT], 4, 12, &f.local_count, data);
    }
    if (status == XXBYTE_STATUS_OK) {
        status = beam_check_count(&f.chunks[XXBYTE_BEAM_CHUNK_FUNT], 4, 24, &f.lambda_count, data);
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (f.chunks[XXBYTE_BEAM_CHUNK_TYPE].offset != 0) {
        const xxbyte_beam_chunk *c = &f.chunks[XXBYTE_BEAM_CHUNK_TYPE];
        if (c->size < 8) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        f.type_version = xxbyte_u32be(data + c->offset);
        f.type_count = xxbyte_u32be(data + c->offset + 4);
    }
    if (f.chunks[XXBYTE_BEAM_CHUNK_LITT].offset != 0 && f.chunks[XXBYTE_BEAM_CHUNK_LITT].size < 4) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    f.mode = xxbyte_beam_mode_for_max_opcode(f.max_opcode);
    if (f.type_version != 0 && f.mode < XXBYTE_BEAM_MODE_TYPES) {
        f.mode = XXBYTE_BEAM_MODE_TYPES;
    }
    *file = f;
    return XXBYTE_STATUS_OK;
}

static int beam_file_ok(const uint8_t *data, size_t size, const xxbyte_beam_file *file)
{
    return file != NULL && data != NULL && file->size != 0 && file->size <= size;
}

typedef struct beam_atom_find {
    uint32_t want;
    uint32_t offset;
    uint32_t length;
    int found;
} beam_atom_find;

static int beam_atom_find_cb(void *ctx, uint32_t index, uint32_t offset, uint32_t length)
{
    beam_atom_find *f = (beam_atom_find *)ctx;
    if (index == f->want) {
        f->offset = offset;
        f->length = length;
        f->found = 1;
        return 0;
    }
    return 1;
}

xxbyte_status xxbyte_beam_atom(const uint8_t *data, size_t size, const xxbyte_beam_file *file, uint32_t index, const uint8_t **text, uint32_t *length)
{
    beam_atom_find find;
    xxbyte_status status;

    if (text != NULL) {
        *text = NULL;
    }
    if (length != NULL) {
        *length = 0;
    }
    if (!beam_file_ok(data, size, file) || text == NULL || length == NULL || index == 0 || index > file->atom_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(&find, 0, sizeof(find));
    find.want = index;
    status = beam_walk_atoms(data, file, beam_atom_find_cb, &find);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (!find.found) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *text = data + find.offset;
    *length = find.length;
    return XXBYTE_STATUS_OK;
}

static const uint8_t *beam_entry(const uint8_t *data, const xxbyte_beam_file *file, xxbyte_beam_chunk_id chunk, uint32_t count, uint32_t entry, uint32_t index)
{
    if (index >= count) {
        return NULL;
    }
    return data + file->chunks[chunk].offset + 4u + (size_t)index * entry;
}

xxbyte_status xxbyte_beam_import(const uint8_t *data, size_t size, const xxbyte_beam_file *file, uint32_t index, uint32_t *module, uint32_t *function, uint32_t *arity)
{
    const uint8_t *p;

    if (!beam_file_ok(data, size, file) || module == NULL || function == NULL || arity == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    p = beam_entry(data, file, XXBYTE_BEAM_CHUNK_IMPT, file->import_count, 12, index);
    if (p == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *module = xxbyte_u32be(p);
    *function = xxbyte_u32be(p + 4);
    *arity = xxbyte_u32be(p + 8);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_beam_export(const uint8_t *data, size_t size, const xxbyte_beam_file *file, int local, uint32_t index, uint32_t *function, uint32_t *arity,
                                 uint32_t *label)
{
    const uint8_t *p;

    if (!beam_file_ok(data, size, file) || function == NULL || arity == NULL || label == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    p = local ? beam_entry(data, file, XXBYTE_BEAM_CHUNK_LOCT, file->local_count, 12, index)
              : beam_entry(data, file, XXBYTE_BEAM_CHUNK_EXPT, file->export_count, 12, index);
    if (p == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *function = xxbyte_u32be(p);
    *arity = xxbyte_u32be(p + 4);
    *label = xxbyte_u32be(p + 8);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_beam_lambda_entry(const uint8_t *data, size_t size, const xxbyte_beam_file *file, uint32_t index, xxbyte_beam_lambda *lambda)
{
    const uint8_t *p;

    if (lambda != NULL) {
        memset(lambda, 0, sizeof(*lambda));
    }
    if (!beam_file_ok(data, size, file) || lambda == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    p = beam_entry(data, file, XXBYTE_BEAM_CHUNK_FUNT, file->lambda_count, 24, index);
    if (p == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    lambda->function = xxbyte_u32be(p);
    lambda->arity = xxbyte_u32be(p + 4);
    lambda->label = xxbyte_u32be(p + 8);
    lambda->index = xxbyte_u32be(p + 12);
    lambda->free_count = xxbyte_u32be(p + 16);
    lambda->old_uniq = xxbyte_u32be(p + 20);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_beam_literal_table(const uint8_t *data, size_t size, const xxbyte_beam_file *file, uint8_t *out, size_t capacity, size_t *table_size)
{
    const xxbyte_beam_chunk *c;
    uint32_t original;

    if (table_size != NULL) {
        *table_size = 0;
    }
    if (!beam_file_ok(data, size, file) || table_size == NULL || (out == NULL && capacity != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    c = &file->chunks[XXBYTE_BEAM_CHUNK_LITT];
    if (c->offset == 0) {
        return XXBYTE_STATUS_OK;
    }
    original = xxbyte_u32be(data + c->offset);
    if (original == 0) {
        *table_size = c->size - 4u;
        if (capacity < *table_size) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        memcpy(out, data + c->offset + 4, *table_size);
        return XXBYTE_STATUS_OK;
    }
    if (original > BEAM_INFLATE_LIMIT) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *table_size = original;
    if (capacity < original) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    {
        size_t got = 0;
        xxbyte_status status = xxbyte_inflate(data + c->offset + 4, c->size - 4u, 1, out, original, &got, NULL);
        if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
            status = XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (status != XXBYTE_STATUS_OK) {
            *table_size = 0;
            return status;
        }
        if (got != original) {
            *table_size = 0;
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_beam_literal(const uint8_t *table, size_t table_size, uint32_t index, const uint8_t **term, size_t *term_size)
{
    size_t off = 4;
    uint32_t i, count;

    if (term != NULL) {
        *term = NULL;
    }
    if (term_size != NULL) {
        *term_size = 0;
    }
    if (table == NULL || term == NULL || term_size == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (table_size < 4) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    count = xxbyte_u32be(table);
    if (index >= count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0;; ++i) {
        uint32_t n;
        if (table_size - off < 4) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        n = xxbyte_u32be(table + off);
        off += 4;
        if (n > table_size - off) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (i == index) {
            *term = table + off;
            *term_size = n;
            return XXBYTE_STATUS_OK;
        }
        off += n;
    }
}

/* Copies the text of a temporary buffer to a caller's buffer. */
static size_t beam_copy_text(xx_buf_t *text, int ok, char *buffer, size_t buffer_size)
{
    size_t length = 0;

    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if (ok && xx_buf_ok(text) && text->size != 0) {
        length = text->size;
        if (buffer != NULL && buffer_size != 0) {
            size_t copy = length < buffer_size ? length : buffer_size - 1;
            memcpy(buffer, text->data, copy);
            buffer[copy] = '\0';
        }
    }
    xx_buf_free(text);
    return length;
}

size_t xxbyte_beam_term_text(const uint8_t *term, size_t term_size, char *buffer, size_t buffer_size)
{
    xx_buf_t text;
    xxbyte_fmt fmt;
    size_t used = 0;
    int r;

    if ((buffer == NULL && buffer_size != 0) || (term == NULL && term_size != 0)) {
        return 0;
    }
    xx_buf_init(&text);
    fmt.out = &text;
    fmt.flags = 0;
    fmt.resolver = NULL;
    fmt.context = NULL;
    r = term == NULL ? -1 : xxbyte_beam_fmt_etf(&fmt, term, term_size, &used);
    return beam_copy_text(&text, r > 0, buffer, buffer_size);
}

/* ---------------------------------------------------------------------- */
/* Type table. */

typedef struct beam_type {
    uint16_t bits; /* newest format, without the metadata flags */
    uint8_t has_min, has_max, has_unit;
    uint8_t unit;
    int64_t min, max;
} beam_type;

static int64_t beam_s64be(const uint8_t *p)
{
    uint64_t v = ((uint64_t)xxbyte_u32be(p) << 32) | xxbyte_u32be(p + 4);
    return (int64_t)v;
}

/* Reads the entry at chunk offset *off (after the 8-byte header). */
static int beam_read_type(const uint8_t *p, size_t size, size_t *off, uint32_t version, beam_type *t)
{
    uint32_t raw, bits;
    size_t o = *off;

    memset(t, 0, sizeof(*t));
    if (size - o < 2) {
        return 0;
    }
    raw = xxbyte_u16be(p + o);
    o += 2;
    if (version == 1) {
        int64_t min, max;
        if (size - o < 16) {
            return 0;
        }
        min = beam_s64be(p + o);
        max = beam_s64be(p + o + 8);
        o += 16;
        bits = (raw & 3u) | ((raw & 0x1FFCu) >> 1);
        t->bits = (uint16_t)(bits & 0x0FFFu);
        if (min <= max) {
            t->has_min = t->has_max = 1;
            t->min = min;
            t->max = max;
        }
        *off = o;
        return 1;
    }
    if (version == 2) {
        raw = (raw & 3u) | ((raw & ~3u & 0xFFFFu) >> 1);
        version = 3;
    }
    if (version == 3) {
        raw = (raw & 0x0FFFu) | ((raw << 1) & 0xE000u);
    } else if (version != 4) {
        return 0;
    }
    t->bits = (uint16_t)(raw & 0x1FFFu);
    if ((raw & (1u << 13)) != 0) {
        if (size - o < 8) {
            return 0;
        }
        t->has_min = 1;
        t->min = beam_s64be(p + o);
        o += 8;
    }
    if ((raw & (1u << 14)) != 0) {
        if (size - o < 8) {
            return 0;
        }
        t->has_max = 1;
        t->max = beam_s64be(p + o);
        o += 8;
    }
    if ((raw & (1u << 15)) != 0) {
        if (size - o < 1) {
            return 0;
        }
        t->has_unit = 1;
        t->unit = p[o];
        o += 1;
    }
    *off = o;
    return 1;
}

static int beam_fmt_type_entry(xxbyte_fmt *fmt, const beam_type *t)
{
    return xxbyte_beam_fmt_type(fmt, t->bits, t->has_min, t->min, t->has_max, t->max, t->has_unit, (uint32_t)t->unit + 1u);
}

size_t xxbyte_beam_type_text(const uint8_t *data, size_t size, const xxbyte_beam_file *file, uint32_t index, char *buffer, size_t buffer_size)
{
    const xxbyte_beam_chunk *c;
    size_t off = 8;
    uint32_t i;
    beam_type t;
    xx_buf_t text;
    xxbyte_fmt fmt;
    int ok;

    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if (!beam_file_ok(data, size, file) || (buffer == NULL && buffer_size != 0)) {
        return 0;
    }
    c = &file->chunks[XXBYTE_BEAM_CHUNK_TYPE];
    if (c->offset == 0 || c->size < 8) {
        return 0;
    }
    for (i = 0;; ++i) {
        if (off >= c->size || !beam_read_type(data + c->offset, c->size, &off, file->type_version, &t)) {
            return 0;
        }
        if (i == index) {
            break;
        }
    }
    xx_buf_init(&text);
    fmt.out = &text;
    fmt.flags = 0;
    fmt.resolver = NULL;
    fmt.context = NULL;
    ok = beam_fmt_type_entry(&fmt, &t);
    return beam_copy_text(&text, ok, buffer, buffer_size);
}

/* ---------------------------------------------------------------------- */
/* Listing. */

typedef struct beam_line {
    uint32_t file; /* index into the file names: 0 = the source */
    uint64_t line;
    int big; /* the line number did not fit (printed as 0) */
} beam_line;

typedef struct beam_listing {
    const uint8_t *data;
    size_t size;
    xxbyte_beam_file file;
    uint32_t mode;
    uint32_t *atom_offsets; /* [atom_count + 1] */
    uint32_t *atom_lengths;
    uint8_t *literals; /* decompressed table */
    size_t literal_size;
    uint32_t literal_count;
    size_t *literal_offsets; /* term offsets in literals */
    size_t *literal_sizes;
    beam_type *types;
    uint32_t type_count;
    beam_line *lines;
    uint32_t line_count;
    uint32_t fname_count;    /* names in the Line chunk */
    uint32_t *fname_offsets; /* from data[0] */
    uint32_t *fname_lengths;
    uint32_t *source_chars;
    size_t source_length;
    uint32_t *entry_function; /* label -> function index + 1 (0: none) */
    size_t entry_count;
    uint32_t (*functions)[3]; /* module atom, function atom, arity */
    size_t function_count;
} beam_listing;

static int beam_atoms_cb(void *ctx, uint32_t index, uint32_t offset, uint32_t length)
{
    beam_listing *l = (beam_listing *)ctx;
    l->atom_offsets[index] = offset;
    l->atom_lengths[index] = length;
    return 1;
}

static int beam_fmt_atom_index(xxbyte_fmt *fmt, const beam_listing *l, uint64_t index)
{
    if (index == 0 || index > l->file.atom_count) {
        return -1;
    }
    return xxbyte_beam_fmt_atom(fmt, l->data + l->atom_offsets[index], l->atom_lengths[index], l->file.atoms_utf8);
}

/* A code-point list as ~p prints it. */
static int beam_fmt_char_list(xxbyte_fmt *fmt, const uint32_t *chars, size_t count)
{
    size_t i;
    int printable = count > 0;

    for (i = 0; printable && i < count; ++i) {
        uint32_t c = chars[i];
        printable = (c >= 32 && c <= 126) || (c >= 160 && c <= 255) || c == '\n' || c == '\r' || c == '\t' || c == '\v' || c == '\b' || c == '\f' || c == 27;
    }
    if (count == 0) {
        return xxbyte_fmt_str(fmt, "[]");
    }
    if (!printable) {
        if (!xxbyte_fmt_char(fmt, '[')) {
            return 0;
        }
        for (i = 0; i < count; ++i) {
            if (!xxbyte_fmt_printf(fmt, i == 0 ? "%u" : ",%u", (unsigned)chars[i])) {
                return 0;
            }
        }
        return xxbyte_fmt_char(fmt, ']');
    }
    {
        /* build a Latin-1 STRING_EXT-like body and reuse the ETF printer */
        uint8_t *etf = (uint8_t *)xx_mem_alloc(count * 2 + 8);
        size_t used = 0, n = 0;
        int r;
        if (etf == NULL) {
            return 0;
        }
        etf[n++] = 131;
        etf[n++] = 108; /* LIST */
        etf[n++] = (uint8_t)(count >> 24);
        etf[n++] = (uint8_t)(count >> 16);
        etf[n++] = (uint8_t)(count >> 8);
        etf[n++] = (uint8_t)count;
        for (i = 0; i < count; ++i) {
            etf[n++] = 97;
            etf[n++] = (uint8_t)chars[i];
        }
        etf[n++] = 106;
        r = xxbyte_beam_fmt_etf(fmt, etf, n, &used);
        xx_mem_free(etf);
        return r > 0;
    }
}

/* Decodes UTF-8 bytes to code points (malformed bytes stand for
 * themselves). */
static uint32_t *beam_utf8_chars(const uint8_t *text, size_t length, size_t *count)
{
    uint32_t *chars = (uint32_t *)xx_mem_alloc((length + 1) * sizeof(uint32_t));
    size_t i = 0, n = 0;

    if (chars == NULL) {
        return NULL;
    }
    while (i < length) {
        uint8_t b = text[i];
        uint32_t c = b;
        size_t need = 0, k;
        if (b >= 0xC2 && b <= 0xDF) {
            need = 1;
            c = b & 0x1Fu;
        } else if (b >= 0xE0 && b <= 0xEF) {
            need = 2;
            c = b & 0x0Fu;
        } else if (b >= 0xF0 && b <= 0xF4) {
            need = 3;
            c = b & 0x07u;
        }
        if (need != 0 && length - i - 1 >= need) {
            for (k = 0; k < need; ++k) {
                if ((text[i + 1 + k] & 0xC0) != 0x80) {
                    break;
                }
                c = (c << 6) | (text[i + 1 + k] & 0x3Fu);
            }
            if (k == need) {
                chars[n++] = c;
                i += 1 + need;
                continue;
            }
        }
        chars[n++] = b;
        ++i;
    }
    *count = n;
    return chars;
}

/* Code points of an ETF string (STRING, LIST of integers or NIL) at pos. */
static uint32_t *beam_etf_chars(const uint8_t *p, size_t size, size_t pos, size_t *count)
{
    uint32_t *chars = NULL;
    size_t k, n = 0;

    *count = 0;
    if (pos >= size) {
        return NULL;
    }
    if (p[pos] == 106) {
        return (uint32_t *)xx_mem_alloc(sizeof(uint32_t));
    }
    if (p[pos] == 107 && size - pos >= 3) {
        size_t len = xxbyte_u16be(p + pos + 1);
        if (len > size - pos - 3) {
            return NULL;
        }
        chars = (uint32_t *)xx_mem_alloc((len + 1) * sizeof(uint32_t));
        if (chars == NULL) {
            return NULL;
        }
        for (k = 0; k < len; ++k) {
            chars[k] = p[pos + 3 + k];
        }
        *count = len;
        return chars;
    }
    if (p[pos] == 108 && size - pos >= 5) {
        uint32_t len = xxbyte_u32be(p + pos + 1);
        size_t q = pos + 5;
        if ((uint64_t)len * 2 > size - q) {
            return NULL;
        }
        chars = (uint32_t *)xx_mem_alloc(((size_t)len + 1) * sizeof(uint32_t));
        if (chars == NULL) {
            return NULL;
        }
        for (k = 0; k < len; ++k) {
            if (q < size && p[q] == 97 && size - q >= 2) {
                chars[n++] = p[q + 1];
                q += 2;
            } else if (q < size && p[q] == 98 && size - q >= 5) {
                chars[n++] = xxbyte_u32be(p + q + 1);
                q += 5;
            } else {
                xx_mem_free(chars);
                return NULL;
            }
        }
        if (q >= size || p[q] != 106) {
            xx_mem_free(chars);
            return NULL;
        }
        *count = n;
        return chars;
    }
    return NULL;
}

/* Finds {source, Chars} in the CInf term (a proplist) and keeps the
 * basename of Chars, as beam_disasm does. */
static void beam_load_source(beam_listing *l)
{
    const xxbyte_beam_chunk *c = &l->file.chunks[XXBYTE_BEAM_CHUNK_CINF];
    const uint8_t *p;
    size_t size, off;
    uint32_t n, i;

    if (c->offset == 0 || c->size < 6) {
        return;
    }
    p = l->data + c->offset;
    size = c->size;
    if (p[0] != 131 || p[1] != 108) {
        return;
    }
    n = xxbyte_u32be(p + 2);
    off = 6;
    for (i = 0; i < n; ++i) {
        size_t name_at = 0, value_at = 0;
        if (size - off >= 4 && p[off] == 104 && p[off + 1] == 2) {
            uint8_t tag = p[off + 2];
            if ((tag == 115 || tag == 119) && size - off >= 10 && p[off + 3] == 6) {
                name_at = off + 4;
                value_at = off + 10;
            } else if ((tag == 100 || tag == 118) && size - off >= 11 && p[off + 3] == 0 && p[off + 4] == 6) {
                name_at = off + 5;
                value_at = off + 11;
            }
        }
        if (name_at != 0 && memcmp(p + name_at, "source", 6) == 0) {
            size_t count = 0, k, base = 0;
            uint32_t *chars = beam_etf_chars(p, size, value_at, &count);
            if (chars != NULL) {
                for (k = 0; k < count; ++k) {
                    if (chars[k] == '/') {
                        base = k + 1;
                    }
                }
                memmove(chars, chars + base, (count - base) * sizeof(uint32_t));
                l->source_chars = chars;
                l->source_length = count - base;
            }
            return;
        }
        if (!xxbyte_beam_etf_skip(p, size, &off)) {
            return;
        }
    }
}

static void beam_load_lines(beam_listing *l)
{
    const xxbyte_beam_chunk *c = &l->file.chunks[XXBYTE_BEAM_CHUNK_LINE];
    const uint8_t *p;
    size_t size, off;
    uint32_t count, i, fnames, current = 0;

    if (c->offset == 0 || c->size < 20) {
        return;
    }
    p = l->data + c->offset;
    size = c->size;
    if (xxbyte_u32be(p) != 0) {
        return;
    }
    count = xxbyte_u32be(p + 12);
    fnames = xxbyte_u32be(p + 16);
    if (count > size) {
        return;
    }
    l->lines = (beam_line *)xx_mem_alloc(((size_t)count + 1) * sizeof(beam_line));
    if (l->lines == NULL) {
        return;
    }
    off = 20;
    for (i = 0; i < count; ++i) {
        xxbyte_operand op;
        if (xxbyte_beam_read_term(XXBYTE_BEAM_MODE_DEFAULT, p, size, &off, 1, &op) != XXBYTE_STATUS_OK) {
            break;
        }
        if (XXBYTE_BEAM_OPERAND_TAG(&op) == XXBYTE_BEAM_TAG_A) {
            current = (uint32_t)op.value;
            if (op.value > UINT32_MAX || xxbyte_beam_read_term(XXBYTE_BEAM_MODE_DEFAULT, p, size, &off, 1, &op) != XXBYTE_STATUS_OK) {
                break;
            }
        }
        if (XXBYTE_BEAM_OPERAND_TAG(&op) != XXBYTE_BEAM_TAG_I) {
            break;
        }
        l->lines[i].file = current;
        l->lines[i].big = op.type != XXBYTE_OPERAND_IMMEDIATE;
        l->lines[i].line = op.type == XXBYTE_OPERAND_IMMEDIATE ? (uint64_t)op.value : 0;
    }
    l->line_count = i;
    if (i != count) {
        return;
    }
    if (fnames > size) {
        fnames = (uint32_t)size;
    }
    l->fname_offsets = (uint32_t *)xx_mem_alloc(((size_t)fnames + 1) * sizeof(uint32_t));
    l->fname_lengths = (uint32_t *)xx_mem_alloc(((size_t)fnames + 1) * sizeof(uint32_t));
    if (l->fname_offsets == NULL || l->fname_lengths == NULL) {
        return;
    }
    for (i = 0; i < fnames && size - off >= 2; ++i) {
        uint32_t len = xxbyte_u16be(p + off);
        if (len > size - off - 2) {
            break;
        }
        l->fname_offsets[i] = (uint32_t)(c->offset + off + 2);
        l->fname_lengths[i] = len;
        off += 2u + len;
    }
    l->fname_count = i;
}

static void beam_load_types(beam_listing *l)
{
    const xxbyte_beam_chunk *c = &l->file.chunks[XXBYTE_BEAM_CHUNK_TYPE];
    size_t off = 8, cap;
    uint32_t n = 0;

    if (c->offset == 0 || c->size < 8) {
        return;
    }
    cap = (c->size - 8) / 2 + 1;
    l->types = (beam_type *)xx_mem_alloc(cap * sizeof(beam_type));
    if (l->types == NULL) {
        return;
    }
    while (off < c->size && n < cap && beam_read_type(l->data + c->offset, c->size, &off, l->file.type_version, &l->types[n])) {
        ++n;
    }
    l->type_count = n;
}

static void beam_load_literals(beam_listing *l)
{
    size_t table_size = 0, off = 4;
    uint32_t count, i;

    if (xxbyte_beam_literal_table(l->data, l->size, &l->file, NULL, 0, &table_size) != XXBYTE_STATUS_OUT_OF_MEMORY || table_size < 4) {
        return;
    }
    l->literals = (uint8_t *)xx_mem_alloc(table_size);
    if (l->literals == NULL) {
        return;
    }
    if (xxbyte_beam_literal_table(l->data, l->size, &l->file, l->literals, table_size, &l->literal_size) != XXBYTE_STATUS_OK) {
        l->literal_size = 0;
        return;
    }
    count = xxbyte_u32be(l->literals);
    if (count > table_size / 4) {
        count = (uint32_t)(table_size / 4);
    }
    l->literal_offsets = (size_t *)xx_mem_alloc(((size_t)count + 1) * sizeof(size_t));
    l->literal_sizes = (size_t *)xx_mem_alloc(((size_t)count + 1) * sizeof(size_t));
    if (l->literal_offsets == NULL || l->literal_sizes == NULL) {
        return;
    }
    for (i = 0; i < count; ++i) {
        uint32_t n;
        if (l->literal_size - off < 4) {
            break;
        }
        n = xxbyte_u32be(l->literals + off);
        off += 4;
        if (n > l->literal_size - off) {
            break;
        }
        l->literal_offsets[i] = off;
        l->literal_sizes[i] = n;
        off += n;
    }
    l->literal_count = i;
}

static void beam_listing_free(beam_listing *l)
{
    xx_mem_free(l->atom_offsets);
    xx_mem_free(l->atom_lengths);
    xx_mem_free(l->literals);
    xx_mem_free(l->literal_offsets);
    xx_mem_free(l->literal_sizes);
    xx_mem_free(l->types);
    xx_mem_free(l->lines);
    xx_mem_free(l->fname_offsets);
    xx_mem_free(l->fname_lengths);
    xx_mem_free(l->source_chars);
    xx_mem_free(l->entry_function);
    xx_mem_free(l->functions);
}

/* The location text of line item index (1-based). */
static int beam_fmt_location(xxbyte_fmt *fmt, const beam_listing *l, uint64_t index)
{
    const beam_line *line;
    int ok;

    if (index == 0 || l->file.chunks[XXBYTE_BEAM_CHUNK_LINE].offset == 0) {
        return xxbyte_fmt_str(fmt, "[]");
    }
    if (index > l->line_count) {
        return -1;
    }
    line = &l->lines[index - 1];
    if (line->file != 0 && line->file > l->fname_count) {
        return -1;
    }
    if (!xxbyte_fmt_str(fmt, "[{location,")) {
        return 0;
    }
    if (line->file == 0) {
        ok = beam_fmt_char_list(fmt, l->source_chars, l->source_length);
    } else {
        size_t count = 0;
        uint32_t *chars = beam_utf8_chars(l->data + l->fname_offsets[line->file - 1], l->fname_lengths[line->file - 1], &count);
        if (chars == NULL) {
            return 0;
        }
        ok = beam_fmt_char_list(fmt, chars, count);
        xx_mem_free(chars);
    }
    return ok && xxbyte_fmt_printf(fmt, ",%llu}]", (unsigned long long)line->line);
}

static int beam_fmt_literal(xxbyte_fmt *fmt, const beam_listing *l, uint64_t index)
{
    size_t used = 0, mark;
    const uint8_t *t;
    int r;

    if (index >= l->literal_count) {
        return -1;
    }
    t = l->literals + l->literal_offsets[index];
    mark = xxbyte_fmt_mark(fmt);
    if (l->literal_sizes[index] >= 10 && t[0] == 131 && t[1] == 70) {
        if (!xxbyte_fmt_str(fmt, "{float,")) {
            return 0;
        }
    } else if (!xxbyte_fmt_str(fmt, "{literal,")) {
        return 0;
    }
    r = xxbyte_beam_fmt_etf(fmt, t, l->literal_sizes[index], &used);
    if (r <= 0) {
        xxbyte_fmt_rewind(fmt, mark);
        return r == 0 ? 0 : -1;
    }
    return xxbyte_fmt_char(fmt, '}');
}

/* The StrT bytes as a list ("abc") or binary (<<"abc">>). */
static int beam_fmt_strt(xxbyte_fmt *fmt, const beam_listing *l, uint64_t offset, uint64_t length, int binary)
{
    const xxbyte_beam_chunk *c = &l->file.chunks[XXBYTE_BEAM_CHUNK_STRT];
    uint8_t *etf;
    size_t n = 0, used = 0, i;
    int r;

    if (length == 0) {
        return xxbyte_fmt_str(fmt, binary ? "{string,<<>>}" : "{string,[]}");
    }
    if (c->offset == 0 || offset > c->size || length > c->size - offset) {
        return -1;
    }
    etf = (uint8_t *)xx_mem_alloc((size_t)length * 2 + 8);
    if (etf == NULL) {
        return 0;
    }
    etf[n++] = 131;
    if (binary) {
        etf[n++] = 109;
        etf[n++] = (uint8_t)(length >> 24);
        etf[n++] = (uint8_t)(length >> 16);
        etf[n++] = (uint8_t)(length >> 8);
        etf[n++] = (uint8_t)length;
        memcpy(etf + n, l->data + c->offset + offset, (size_t)length);
        n += (size_t)length;
    } else {
        etf[n++] = 108;
        etf[n++] = (uint8_t)(length >> 24);
        etf[n++] = (uint8_t)(length >> 16);
        etf[n++] = (uint8_t)(length >> 8);
        etf[n++] = (uint8_t)length;
        for (i = 0; i < length; ++i) {
            etf[n++] = 97;
            etf[n++] = l->data[c->offset + offset + i];
        }
        etf[n++] = 106;
    }
    if (!xxbyte_fmt_str(fmt, "{string,")) {
        xx_mem_free(etf);
        return 0;
    }
    r = xxbyte_beam_fmt_etf(fmt, etf, n, &used);
    xx_mem_free(etf);
    return r > 0 ? xxbyte_fmt_char(fmt, '}') : r;
}

static int beam_operand_u(const xxbyte_instruction *instruction, uint32_t index, uint64_t *value)
{
    const xxbyte_operand *op;

    if (index >= instruction->operand_count) {
        return 0;
    }
    op = &instruction->operands[index];
    if (op->type != XXBYTE_OPERAND_IMMEDIATE || XXBYTE_BEAM_OPERAND_TAG(op) != XXBYTE_BEAM_TAG_U) {
        return 0;
    }
    *value = (uint64_t)op->value;
    return 1;
}

int xxbyte_beam_listing_write(void *context, const xxbyte_instruction *instruction, const xxbyte_operand *op, uint32_t operand_index, uint32_t list_index,
                              xxbyte_fmt *fmt)
{
    const beam_listing *l = (const beam_listing *)context;
    uint32_t tag = XXBYTE_BEAM_OPERAND_TAG(op);
    size_t mark = xxbyte_fmt_mark(fmt);
    int r = -1;

    (void)operand_index;
    if (l == NULL) {
        return 0;
    }
    if (op->type == XXBYTE_OPERAND_BRANCH || (op->type == XXBYTE_OPERAND_INDEX && op->kind == XXBYTE_INDEX_LABEL)) {
        uint64_t label = op->type == XXBYTE_OPERAND_BRANCH ? XXBYTE_BEAM_OPERAND_AUX(op) : (uint64_t)op->value;
        uint32_t opcode = instruction->opcode;
        if (list_index == UINT32_MAX && (opcode == XXBYTE_BEAM_OP_CALL || opcode == XXBYTE_BEAM_OP_CALL_LAST || opcode == XXBYTE_BEAM_OP_CALL_ONLY) &&
            label < l->entry_count && l->entry_function[label] != 0) {
            const uint32_t *f = l->functions[l->entry_function[label] - 1];
            r = xxbyte_fmt_char(fmt, '{') && beam_fmt_atom_index(fmt, l, f[0]) > 0 && xxbyte_fmt_char(fmt, ',') && beam_fmt_atom_index(fmt, l, f[1]) > 0 &&
                xxbyte_fmt_printf(fmt, ",%u}", (unsigned)f[2]);
        } else {
            r = xxbyte_fmt_printf(fmt, "{f,%llu}", (unsigned long long)label);
        }
    } else if (op->type == XXBYTE_OPERAND_REGISTER) {
        uint64_t t = XXBYTE_BEAM_OPERAND_AUX(op);
        if (t < l->type_count) {
            r = xxbyte_fmt_printf(fmt, "{tr,{%c,%llu},", tag == XXBYTE_BEAM_TAG_TR_X ? 'x' : 'y', (unsigned long long)op->value) &&
                beam_fmt_type_entry(fmt, &l->types[t]) && xxbyte_fmt_char(fmt, '}');
        }
    } else if (op->type == XXBYTE_OPERAND_INDEX) {
        uint64_t v = (uint64_t)op->value;
        switch (op->kind) {
            case XXBYTE_BEAM_INDEX_ATOM:
                if (v != 0 && v <= l->file.atom_count) {
                    r = xxbyte_fmt_str(fmt, "{atom,") && beam_fmt_atom_index(fmt, l, v) > 0 && xxbyte_fmt_char(fmt, '}');
                }
                break;
            case XXBYTE_INDEX_CONSTANT: r = beam_fmt_literal(fmt, l, v); break;
            case XXBYTE_INDEX_IMPORT: {
                uint32_t m, f, a;
                if (xxbyte_beam_import(l->data, l->size, &l->file, (uint32_t)(v > UINT32_MAX ? UINT32_MAX : v), &m, &f, &a) == XXBYTE_STATUS_OK && m != 0 &&
                    m <= l->file.atom_count && f != 0 && f <= l->file.atom_count) {
                    r = xxbyte_fmt_str(fmt, "{extfunc,") && beam_fmt_atom_index(fmt, l, m) > 0 && xxbyte_fmt_char(fmt, ',') && beam_fmt_atom_index(fmt, l, f) > 0 &&
                        xxbyte_fmt_printf(fmt, ",%u}", (unsigned)a);
                }
                break;
            }
            case XXBYTE_INDEX_FUNCTION: {
                xxbyte_beam_lambda lambda;
                if (xxbyte_beam_lambda_entry(l->data, l->size, &l->file, (uint32_t)(v > UINT32_MAX ? UINT32_MAX : v), &lambda) == XXBYTE_STATUS_OK) {
                    if (instruction->opcode == XXBYTE_BEAM_OP_CALL_FUN2) {
                        r = xxbyte_fmt_printf(fmt, "{f,%u}", (unsigned)lambda.label);
                    } else if (lambda.function != 0 && lambda.function <= l->file.atom_count && l->file.atom_count >= 1) {
                        r = xxbyte_fmt_char(fmt, '{') && beam_fmt_atom_index(fmt, l, 1) > 0 && xxbyte_fmt_char(fmt, ',') &&
                            beam_fmt_atom_index(fmt, l, lambda.function) > 0 && xxbyte_fmt_printf(fmt, ",%u}", (unsigned)lambda.arity);
                    }
                }
                break;
            }
            case XXBYTE_INDEX_STRING: {
                uint64_t n;
                if (instruction->opcode == XXBYTE_BEAM_OP_BS_MATCH_STRING) {
                    if (beam_operand_u(instruction, 2, &n)) {
                        r = beam_fmt_strt(fmt, l, v, (n + 7) / 8, 1);
                    }
                } else if (instruction->opcode == XXBYTE_BEAM_OP_BS_CREATE_BIN) {
                    xxbyte_operand size_op;
                    if (list_index != UINT32_MAX && xxbyte_beam_list_entry(instruction, list_index + 1, &size_op) == XXBYTE_STATUS_OK &&
                        XXBYTE_BEAM_OPERAND_TAG(&size_op) == XXBYTE_BEAM_TAG_I && size_op.type == XXBYTE_OPERAND_IMMEDIATE && size_op.value >= 0) {
                        r = beam_fmt_strt(fmt, l, v, (uint64_t)size_op.value, 1);
                    }
                } else if (beam_operand_u(instruction, 0, &n)) {
                    r = beam_fmt_strt(fmt, l, v, n, 0);
                }
                break;
            }
            case XXBYTE_BEAM_INDEX_LINE: r = beam_fmt_location(fmt, l, v); break;
            default: break;
        }
    }
    if (r < 0) {
        xxbyte_fmt_rewind(fmt, mark);
        return 0;
    }
    return r > 0 ? 1 : -1;
}

size_t xxbyte_beam_listing_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    xx_buf_t text;
    xxbyte_fmt fmt;
    int r;

    if (instruction == NULL || operand_index >= instruction->operand_count) {
        return 0;
    }
    xx_buf_init(&text);
    fmt.out = &text;
    fmt.flags = 0;
    fmt.resolver = NULL;
    fmt.context = NULL;
    r = xxbyte_beam_listing_write(context, instruction, &instruction->operands[operand_index], operand_index, UINT32_MAX, &fmt);
    return beam_copy_text(&text, r > 0, buffer, buffer_size);
}

/* One swept instruction of the code. */
typedef struct beam_item {
    uint32_t offset;
    uint32_t size; /* 0: one undecodable byte */
    uint8_t opcode;
} beam_item;

static xxbyte_status beam_list(beam_listing *l, uint32_t listing_flags, xx_buf_t *out)
{
    const uint8_t *code = l->data + l->file.code_offset;
    size_t code_size = l->file.code_size, off = 0, n = 0, cap, i, label_cap;
    beam_item *items;
    uint64_t *labels;
    xxbyte_beam_context ctx;
    xxbyte_status status = XXBYTE_STATUS_OK;
    size_t *starts;
    size_t start_count = 0, end_index;
    uint32_t release = xxbyte_beam_release(l->mode);

    cap = code_size + 1;
    items = (beam_item *)xx_mem_alloc(cap * sizeof(beam_item));
    starts = (size_t *)xx_mem_alloc(cap * sizeof(size_t));
    label_cap = l->file.label_count < code_size + 1 ? l->file.label_count : code_size + 1;
    labels = (uint64_t *)xx_mem_alloc((label_cap + 1) * sizeof(uint64_t));
    if (items == NULL || starts == NULL || labels == NULL) {
        xx_mem_free(items);
        xx_mem_free(starts);
        xx_mem_free(labels);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    /* sweep */
    while (off < code_size) {
        xxbyte_instruction insn;
        if (xxbyte_decode(XXBYTE_FAMILY_BEAM, l->mode, code + off, code_size - off, off, &insn) == XXBYTE_STATUS_OK) {
            items[n].offset = (uint32_t)off;
            items[n].size = insn.size;
            items[n].opcode = (uint8_t)insn.opcode;
            off += insn.size;
        } else {
            items[n].offset = (uint32_t)off;
            items[n].size = 0;
            items[n].opcode = 0;
            ++off;
        }
        ++n;
    }
    (void)xxbyte_beam_scan_labels(l->mode, code, code_size, 0, labels, label_cap);
    ctx.labels = labels;
    ctx.label_count = label_cap;
    /* function starts: the label/line run before each func_info */
    end_index = n;
    for (i = 0; i < n; ++i) {
        if (items[i].opcode == XXBYTE_BEAM_OP_INT_CODE_END && items[i].size != 0) {
            end_index = i;
            break;
        }
    }
    for (i = 0; i < end_index; ++i) {
        if (items[i].opcode == XXBYTE_BEAM_OP_FUNC_INFO && items[i].size != 0) {
            size_t s = i;
            while (s > 0 && items[s - 1].size != 0 && (items[s - 1].opcode == XXBYTE_BEAM_OP_LABEL || items[s - 1].opcode == XXBYTE_BEAM_OP_LINE)) {
                --s;
            }
            if (start_count == 0 || s > starts[start_count - 1]) {
                starts[start_count++] = s;
            }
        }
    }
    /* function names and entry labels */
    l->functions = (uint32_t (*)[3])xx_mem_alloc((start_count + 1) * sizeof(*l->functions));
    l->entry_function = (uint32_t *)xx_mem_alloc((label_cap + 1) * sizeof(uint32_t));
    if (l->functions == NULL || l->entry_function == NULL) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    memset(l->entry_function, 0, (label_cap + 1) * sizeof(uint32_t));
    l->entry_count = label_cap;
    for (i = 0; i < start_count; ++i) {
        size_t k = starts[i], stop = i + 1 < start_count ? starts[i + 1] : end_index;
        xxbyte_instruction insn;
        memset(l->functions[i], 0, sizeof(l->functions[i]));
        while (k < stop && items[k].opcode != XXBYTE_BEAM_OP_FUNC_INFO) {
            ++k;
        }
        if (k < stop && xxbyte_decode(XXBYTE_FAMILY_BEAM, l->mode, code + items[k].offset, code_size - items[k].offset, items[k].offset, &insn) == XXBYTE_STATUS_OK &&
            insn.operands[0].type == XXBYTE_OPERAND_INDEX && insn.operands[0].kind == XXBYTE_BEAM_INDEX_ATOM && insn.operands[1].type == XXBYTE_OPERAND_INDEX &&
            insn.operands[1].kind == XXBYTE_BEAM_INDEX_ATOM && insn.operands[2].type == XXBYTE_OPERAND_IMMEDIATE &&
            XXBYTE_BEAM_OPERAND_TAG(&insn.operands[2]) == XXBYTE_BEAM_TAG_U && insn.operands[2].value <= UINT32_MAX) {
            l->functions[i][0] = (uint32_t)(insn.operands[0].value > UINT32_MAX ? 0 : insn.operands[0].value);
            l->functions[i][1] = (uint32_t)(insn.operands[1].value > UINT32_MAX ? 0 : insn.operands[1].value);
            l->functions[i][2] = (uint32_t)insn.operands[2].value;
            for (++k; k < stop && items[k].opcode == XXBYTE_BEAM_OP_LABEL && items[k].size != 0; ++k) {
                if (xxbyte_decode(XXBYTE_FAMILY_BEAM, l->mode, code + items[k].offset, code_size - items[k].offset, items[k].offset, &insn) == XXBYTE_STATUS_OK &&
                    (uint64_t)insn.operands[0].value < label_cap) {
                    l->entry_function[insn.operands[0].value] = (uint32_t)(i + 1);
                }
            }
        }
    }
    l->function_count = start_count;
    /* summary */
    xx_buf_append_str(out, "; BEAM module ");
    {
        xxbyte_fmt fmt;
        fmt.out = out;
        fmt.flags = 0;
        fmt.resolver = NULL;
        fmt.context = NULL;
        if (l->file.atom_count >= 1) {
            (void)beam_fmt_atom_index(&fmt, l, 1);
        }
    }
    xx_buf_appendf(out, ": OTP %u instruction set (file max opcode %u), %u labels, %u functions, %u atoms, %u imports, %u exports, %u funs, %u literals, %u types\n",
                   (unsigned)release, (unsigned)l->file.max_opcode, (unsigned)l->file.label_count, (unsigned)l->file.function_count, (unsigned)l->file.atom_count,
                   (unsigned)l->file.import_count, (unsigned)l->file.export_count, (unsigned)l->file.lambda_count, (unsigned)l->literal_count, (unsigned)l->type_count);
    /* bodies */
    {
        size_t from = 0;
        for (i = 0; i <= start_count; ++i) {
            size_t to = i < start_count ? starts[i] : end_index;
            if (to > from) {
                size_t a = items[from].offset;
                size_t b = to < n ? items[to].offset : code_size;
                status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_BEAM, l->mode, &ctx, code + a, b - a, a, listing_flags, xxbyte_beam_listing_resolver, l, out);
                if (status != XXBYTE_STATUS_OK) {
                    goto done;
                }
            }
            if (i == start_count) {
                break;
            }
            xx_buf_append_str(out, "; function ");
            {
                xxbyte_fmt fmt;
                const uint32_t *f = l->functions[i];
                size_t k;
                fmt.out = out;
                fmt.flags = 0;
                fmt.resolver = NULL;
                fmt.context = NULL;
                if (beam_fmt_atom_index(&fmt, l, f[1]) <= 0) {
                    xx_buf_appendf(out, "{a,%u}", (unsigned)f[1]);
                }
                xx_buf_appendf(out, "/%u", (unsigned)f[2]);
                for (k = starts[i]; k < end_index && items[k].opcode != XXBYTE_BEAM_OP_FUNC_INFO; ++k) {
                }
                if (k + 1 < end_index && items[k + 1].opcode == XXBYTE_BEAM_OP_LABEL && items[k + 1].size != 0) {
                    xxbyte_instruction insn;
                    if (xxbyte_decode(XXBYTE_FAMILY_BEAM, l->mode, code + items[k + 1].offset, code_size - items[k + 1].offset, items[k + 1].offset, &insn) ==
                        XXBYTE_STATUS_OK) {
                        xx_buf_appendf(out, " (entry {f,%llu})", (unsigned long long)insn.operands[0].value);
                    }
                }
                xx_buf_append_char(out, '\n');
            }
            from = to;
        }
        if (end_index < n) {
            size_t a = items[end_index].offset;
            xx_buf_append_str(out, "; end of code\n");
            status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_BEAM, l->mode, &ctx, code + a, code_size - a, a, listing_flags, xxbyte_beam_listing_resolver, l, out);
        }
    }
done:
    xx_mem_free(items);
    xx_mem_free(starts);
    xx_mem_free(labels);
    if (status == XXBYTE_STATUS_OK && !xx_buf_ok(out)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return status;
}

/* gzip (RFC 1952) member: returns the decompressed form or NULL. */
static uint8_t *beam_gunzip(const uint8_t *data, size_t size, size_t *out_size, xxbyte_status *status)
{
    size_t off = 10;
    uint8_t flags;
    uint32_t isize;
    uint8_t *out;
    size_t got = 0;

    *status = XXBYTE_STATUS_INVALID_INSTRUCTION;
    if (size < 18 || data[2] != 8) {
        *status = size < 18 ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
        return NULL;
    }
    flags = data[3];
    if ((flags & 0xE0) != 0) {
        return NULL;
    }
    if ((flags & 4) != 0) { /* FEXTRA */
        size_t xlen;
        if (size - off < 2) {
            *status = XXBYTE_STATUS_TRUNCATED;
            return NULL;
        }
        xlen = xxbyte_u16le(data + off);
        off += 2;
        if (size - off < xlen) {
            *status = XXBYTE_STATUS_TRUNCATED;
            return NULL;
        }
        off += xlen;
    }
    if ((flags & 8) != 0) { /* FNAME */
        while (off < size && data[off] != 0) {
            ++off;
        }
        ++off;
    }
    if ((flags & 16) != 0) { /* FCOMMENT */
        while (off < size && data[off] != 0) {
            ++off;
        }
        ++off;
    }
    if ((flags & 2) != 0) { /* FHCRC */
        off += 2;
    }
    if (off >= size || size - off < 8) {
        *status = XXBYTE_STATUS_TRUNCATED;
        return NULL;
    }
    isize = xxbyte_u32le(data + size - 4);
    if (isize == 0 || isize > BEAM_INFLATE_LIMIT) {
        return NULL;
    }
    out = (uint8_t *)xx_mem_alloc(isize);
    if (out == NULL) {
        *status = XXBYTE_STATUS_OUT_OF_MEMORY;
        return NULL;
    }
    *status = xxbyte_inflate(data + off, size - off, 0, out, isize, &got, NULL);
    if (*status == XXBYTE_STATUS_OUT_OF_MEMORY) {
        *status = XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (*status != XXBYTE_STATUS_OK || got != isize) {
        if (*status == XXBYTE_STATUS_OK) {
            *status = XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        xx_mem_free(out);
        return NULL;
    }
    *out_size = got;
    return out;
}

xxbyte_status xxbyte_beam_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    beam_listing l;
    xxbyte_status status;
    uint8_t *inflated = NULL;

    memset(&l, 0, sizeof(l));
    if (size >= 2 && data[0] == 0x1F && data[1] == 0x8B) {
        size_t inflated_size = 0;
        inflated = beam_gunzip(data, size, &inflated_size, &status);
        if (inflated == NULL) {
            return status;
        }
        data = inflated;
        size = inflated_size;
    }
    status = xxbyte_beam_parse_file(data, size, &l.file);
    if (status != XXBYTE_STATUS_OK) {
        xx_mem_free(inflated);
        return status;
    }
    l.data = data;
    l.size = size;
    l.mode = mode == XXBYTE_MODE_AUTO ? l.file.mode : mode;
    l.atom_offsets = (uint32_t *)xx_mem_alloc(((size_t)l.file.atom_count + 1) * sizeof(uint32_t));
    l.atom_lengths = (uint32_t *)xx_mem_alloc(((size_t)l.file.atom_count + 1) * sizeof(uint32_t));
    if (l.atom_offsets == NULL || l.atom_lengths == NULL) {
        beam_listing_free(&l);
        xx_mem_free(inflated);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    (void)beam_walk_atoms(data, &l.file, beam_atoms_cb, &l);
    beam_load_literals(&l);
    beam_load_types(&l);
    beam_load_lines(&l);
    beam_load_source(&l);
    status = beam_list(&l, listing_flags, out);
    beam_listing_free(&l);
    xx_mem_free(inflated);
    return status;
}

#else
typedef int xxbyte_beam_file_disabled;
#endif
