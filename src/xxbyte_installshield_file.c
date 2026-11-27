/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_installshield_internal.h"

#if XXBYTE_USE_INSTALLSHIELD

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#define ALUZ_HEADER_SIZE 124u
#define OBS_HEADER_SIZE 0x100u
#define DBG_HEADER_SIZE 0x74u
#define NONE32 0xFFFFFFFFu

/* Variable banks of names and externs. */
enum {
    BANK_NUMBER = 0,
    BANK_STRING = 1,
    BANK_VARIANT = 2
};

typedef struct sref {
    size_t off;
    size_t len;
} sref;

typedef struct rd {
    const uint8_t *d;
    size_t size;
    size_t pos;
    int bad;
} rd;

typedef struct names {
    sref *v[3];
    uint32_t n[3];
} names;

typedef struct decl {
    uint32_t numbers;
    uint32_t strings;
    uint32_t objects;
    size_t objects_off; /* object entries (4 bytes each) */
    uint32_t sized;
    size_t sized_off;
} decl;

typedef struct proto {
    uint8_t flags;
    uint32_t ret;
    sref dll;
    sref name;
    uint32_t block;
    uint32_t argc;
    size_t args_off;
    int wide_args; /* INS: u16 type, u16 mode per argument */
    uint32_t func;
    uint32_t dbg;
} proto;

typedef struct func {
    uint32_t proto;
    uint32_t first_block;
    uint32_t last_block;
    uint32_t act_start;
    uint32_t act_end;
    int has_decl;
    decl locals;
    uint32_t dbg;
} func;

typedef struct ext {
    int bank;
    int64_t address;
    sref name;
} ext;

typedef struct dbgfn {
    sref name;
    names vars;
    uint32_t lines;
    size_t lines_off;
} dbgfn;

typedef struct script {
    const uint8_t *d;
    size_t size;
    int kind;
    uint32_t mode;
    sref copyright;
    uint32_t version_magic;
    decl globals;
    uint32_t ntypedefs;
    size_t typedefs_off;
    int named_typedefs;
    sref *typedef_names;
    uint32_t nprotos;
    proto *protos;
    uint16_t *argcounts;
    uint32_t nblocks;
    uint32_t *blocks;
    uint32_t nexterns;
    ext *externs;
    uint32_t nfuncs;
    func *funcs;
    size_t code_end;
    int dbg_kind; /* 0 none, 1 external path, 2 embedded, 3 unreadable */
    sref dbg_path;
    names dbg_globals;
    uint32_t ndbg;
    dbgfn *dbgfns;
    int dbg_attached;
    uint32_t cur_func;
} script;

/* Bounds-checked reads. */

static uint32_t r8(rd *r)
{
    if (r->bad || r->pos >= r->size) {
        r->bad = 1;
        return 0;
    }
    return r->d[r->pos++];
}

static uint32_t r16(rd *r)
{
    uint32_t v;
    if (r->bad || r->pos > r->size || r->size - r->pos < 2) {
        r->bad = 1;
        return 0;
    }
    v = xxbyte_u16le(r->d + r->pos);
    r->pos += 2;
    return v;
}

static uint32_t r32(rd *r)
{
    uint32_t v;
    if (r->bad || r->pos > r->size || r->size - r->pos < 4) {
        r->bad = 1;
        return 0;
    }
    v = xxbyte_u32le(r->d + r->pos);
    r->pos += 4;
    return v;
}

static void rskip(rd *r, size_t n)
{
    if (r->bad || r->pos > r->size || r->size - r->pos < n) {
        r->bad = 1;
        return;
    }
    r->pos += n;
}

/* u16 length and that many bytes. */
static sref rstr(rd *r)
{
    sref s;
    s.len = r16(r);
    s.off = r->pos;
    rskip(r, s.len);
    if (r->bad) {
        s.len = 0;
        s.off = 0;
    }
    return s;
}

static rd reader(const script *sc, size_t pos)
{
    rd r;
    r.d = sc->d;
    r.size = sc->size;
    r.pos = pos;
    r.bad = pos > sc->size;
    return r;
}

static void *alloc_array(size_t count, size_t size)
{
    return xx_mem_calloc(count != 0 ? count : 1u, size);
}

/* Text helpers. */

/* Appends the bytes of s with every byte outside printable ASCII as
 * \ooo. */
static int put_ref(xx_buf_t *out, const script *sc, sref s)
{
    size_t i;
    for (i = 0; i < s.len; ++i) {
        uint8_t c = sc->d[s.off + i];
        if (c < 0x20 || c > 0x7E) {
            if (!xx_buf_appendf(out, "\\%03o", (unsigned)c)) {
                return 0;
            }
        } else if (!xx_buf_append_char(out, (char)c)) {
            return 0;
        }
    }
    return 1;
}

/* Copies s into buffer as put_ref would; returns the length or 0. */
static size_t copy_ref(char *buffer, size_t buffer_size, const script *sc, sref s)
{
    xx_buf_t b;
    size_t n = 0;

    if (s.len == 0) {
        return 0;
    }
    xx_buf_init(&b);
    if (put_ref(&b, sc, s) && xx_buf_ok(&b) && b.size < buffer_size) {
        memcpy(buffer, b.data, b.size);
        buffer[b.size] = '\0';
        n = b.size;
    }
    xx_buf_free(&b);
    return n;
}

static size_t copy_text(char *buffer, size_t buffer_size, const char *text)
{
    size_t n = strlen(text);
    if (n == 0 || n >= buffer_size) {
        return 0;
    }
    memcpy(buffer, text, n + 1u);
    return n;
}

static int ref_ieq(const script *sc, sref a, sref b)
{
    size_t i;
    if (a.len != b.len) {
        return 0;
    }
    for (i = 0; i < a.len; ++i) {
        uint8_t x = sc->d[a.off + i];
        uint8_t y = sc->d[b.off + i];
        if (x >= 'A' && x <= 'Z') {
            x = (uint8_t)(x - 'A' + 'a');
        }
        if (y >= 'A' && y <= 'Z') {
            y = (uint8_t)(y - 'A' + 'a');
        }
        if (x != y) {
            return 0;
        }
    }
    return 1;
}

/* Detection and decryption. */

void xxbyte_installshield_decrypt(uint8_t *buffer, size_t size, uint64_t position)
{
    size_t i;

    if (buffer == NULL) {
        return;
    }
    for (i = 0; i < size; ++i) {
        uint8_t c = (uint8_t)(buffer[i] ^ 0xF1u);
        c = (uint8_t)((c >> 2) | (c << 6));
        buffer[i] = (uint8_t)(c - (uint8_t)((position + i) % 71u));
    }
}

static xxbyte_installshield_file_kind magic_kind(const uint8_t *m)
{
    if (m[0] == 0x48 && m[1] == 0x4F && m[2] == 0xF3 && m[3] == 0xC9) {
        return XXBYTE_INSTALLSHIELD_FILE_OBS;
    }
    if (memcmp(m, "pOdA", 4) == 0) {
        return XXBYTE_INSTALLSHIELD_FILE_OBL;
    }
    if (memcmp(m, "aLuZ", 4) == 0) {
        return XXBYTE_INSTALLSHIELD_FILE_INX;
    }
    if (m[0] == 0xB8 && m[1] == 0xC9 && m[2] == 0x0C && m[3] == 0x00) {
        return XXBYTE_INSTALLSHIELD_FILE_INS;
    }
    if (memcmp(m, "kUtZ", 4) == 0) {
        return XXBYTE_INSTALLSHIELD_FILE_DBG;
    }
    return XXBYTE_INSTALLSHIELD_FILE_UNKNOWN;
}

xxbyte_installshield_file_kind xxbyte_installshield_detect(const uint8_t *data, size_t size)
{
    uint8_t m[4];
    xxbyte_installshield_file_kind kind;

    if (data == NULL || size < 4) {
        return XXBYTE_INSTALLSHIELD_FILE_UNKNOWN;
    }
    kind = magic_kind(data);
    if (kind != XXBYTE_INSTALLSHIELD_FILE_UNKNOWN) {
        return kind;
    }
    memcpy(m, data, 4);
    xxbyte_installshield_decrypt(m, 4, 0);
    return memcmp(m, "aLuZ", 4) == 0 ? XXBYTE_INSTALLSHIELD_FILE_INX_ENCRYPTED : XXBYTE_INSTALLSHIELD_FILE_UNKNOWN;
}

/* Script tables. */

static void free_names(names *n)
{
    int i;
    for (i = 0; i < 3; ++i) {
        xx_mem_free(n->v[i]);
        n->v[i] = NULL;
        n->n[i] = 0;
    }
}

static void free_script(script *sc)
{
    uint32_t i;
    xx_mem_free(sc->typedef_names);
    xx_mem_free(sc->protos);
    xx_mem_free(sc->argcounts);
    xx_mem_free(sc->blocks);
    xx_mem_free(sc->externs);
    xx_mem_free(sc->funcs);
    free_names(&sc->dbg_globals);
    if (sc->dbgfns != NULL) {
        for (i = 0; i < sc->ndbg; ++i) {
            free_names(&sc->dbgfns[i].vars);
        }
    }
    xx_mem_free(sc->dbgfns);
}

static xxbyte_status read_decl(rd *r, decl *d)
{
    d->numbers = r16(r);
    d->objects = r16(r);
    d->objects_off = r->pos;
    rskip(r, (size_t)d->objects * 4u);
    d->strings = r16(r);
    d->sized = r16(r);
    d->sized_off = r->pos;
    rskip(r, (size_t)d->sized * 4u);
    return r->bad ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OK;
}

static xxbyte_status read_typedefs(script *sc, size_t off, int named)
{
    rd r = reader(sc, off);
    uint32_t i;
    uint32_t j;

    sc->ntypedefs = r16(&r);
    sc->typedefs_off = r.pos;
    sc->named_typedefs = named;
    if (r.bad) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    sc->typedef_names = (sref *)alloc_array(sc->ntypedefs, sizeof(sref));
    if (sc->typedef_names == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < sc->ntypedefs && !r.bad; ++i) {
        uint32_t members;
        if (named) {
            sc->typedef_names[i] = rstr(&r);
        }
        members = r16(&r);
        for (j = 0; j < members && !r.bad; ++j) {
            r8(&r);
            r16(&r);
            rstr(&r);
        }
    }
    return r.bad ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OK;
}

static xxbyte_status read_inx_prototypes(script *sc, size_t off)
{
    rd r = reader(sc, off);
    uint32_t i;

    sc->nprotos = r16(&r);
    if (r.bad) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    sc->protos = (proto *)alloc_array(sc->nprotos, sizeof(proto));
    if (sc->protos == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < sc->nprotos && !r.bad; ++i) {
        proto *p = &sc->protos[i];
        p->flags = (uint8_t)r8(&r);
        p->block = 0xFFFFu;
        p->func = NONE32;
        p->dbg = NONE32;
        if ((p->flags & 4u) != 0) {
            continue; /* predefined: nothing follows */
        }
        if ((p->flags & 1u) != 0) {
            p->ret = r8(&r);
            p->dll = rstr(&r);
            p->name = rstr(&r);
        } else if ((p->flags & 2u) != 0) {
            p->ret = r8(&r);
            r16(&r);
            p->name = rstr(&r);
        } else {
            return r.bad ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        p->block = r16(&r);
        p->argc = r16(&r);
        p->args_off = r.pos;
        rskip(&r, (size_t)p->argc * 2u);
    }
    return r.bad ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OK;
}

static xxbyte_status read_names(rd *r, names *n)
{
    int b;
    for (b = 0; b < 3; ++b) {
        uint32_t i;
        uint32_t count = r16(r);
        if (r->bad) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        n->v[b] = (sref *)alloc_array(count, sizeof(sref));
        if (n->v[b] == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        n->n[b] = count;
        for (i = 0; i < count && !r->bad; ++i) {
            n->v[b][i] = rstr(r);
        }
    }
    return r->bad ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OK;
}

/* Embedded debug symbols (Compile.exe /GI); failures only mark them
 * unreadable. */
static xxbyte_status read_debug(script *sc, size_t off)
{
    rd r = reader(sc, off);
    uint32_t kind = r8(&r);
    uint32_t i;
    uint32_t files;
    xxbyte_status status;

    if (r.bad) {
        return XXBYTE_STATUS_OK;
    }
    if (kind == 0) {
        sc->dbg_path = rstr(&r);
        sc->dbg_kind = (!r.bad && sc->dbg_path.len != 0) ? 1 : 0;
        return XXBYTE_STATUS_OK;
    }
    if (kind != 1) {
        return XXBYTE_STATUS_OK;
    }
    sc->dbg_kind = 3;
    {
        uint32_t length = r32(&r);
        if (r.bad || length > r.size - r.pos) {
            return XXBYTE_STATUS_OK;
        }
        r.size = r.pos + length;
    }
    if (r16(&r) != 1 || r.bad) {
        return XXBYTE_STATUS_OK;
    }
    files = r16(&r);
    for (i = 0; i < files && !r.bad; ++i) {
        rstr(&r);
    }
    status = read_names(&r, &sc->dbg_globals);
    if (status != XXBYTE_STATUS_OK) {
        return status == XXBYTE_STATUS_OUT_OF_MEMORY ? status : XXBYTE_STATUS_OK;
    }
    sc->ndbg = r16(&r);
    if (r16(&r) != 0 || r.bad) {
        sc->ndbg = 0;
        return XXBYTE_STATUS_OK;
    }
    sc->dbgfns = (dbgfn *)alloc_array(sc->ndbg, sizeof(dbgfn));
    if (sc->dbgfns == NULL) {
        sc->ndbg = 0;
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < sc->ndbg; ++i) {
        dbgfn *f = &sc->dbgfns[i];
        f->name = rstr(&r);
        status = read_names(&r, &f->vars);
        if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
            return status;
        }
        f->lines = r16(&r);
        f->lines_off = r.pos;
        rskip(&r, (size_t)f->lines * 8u);
        if (r.bad || status != XXBYTE_STATUS_OK) {
            return XXBYTE_STATUS_OK;
        }
    }
    sc->dbg_kind = 2;
    return XXBYTE_STATUS_OK;
}

static uint32_t dbg_line_action(const script *sc, const dbgfn *f, uint32_t i)
{
    return xxbyte_u32le(sc->d + f->lines_off + (size_t)i * 8u);
}

/* jte's rule: the first line entry is the function's first statement (after
 * the prolog), the last its closing return or end, all lie inside, there
 * are no more names than variables, and an exported function keeps its
 * name. */
static int dbg_matches(const script *sc, const dbgfn *df, const func *f)
{
    uint32_t i;
    uint32_t first;
    uint32_t last;

    if (df->lines == 0 || f->act_end == 0) {
        return 0;
    }
    first = dbg_line_action(sc, df, 0);
    last = dbg_line_action(sc, df, df->lines - 1u);
    if (first < f->act_start || first > f->act_start + 1u || last >= f->act_end || last + 2u < f->act_end) {
        return 0;
    }
    for (i = 0; i < df->lines; ++i) {
        uint32_t a = dbg_line_action(sc, df, i);
        if (a < f->act_start || a >= f->act_end) {
            return 0;
        }
    }
    if (df->vars.n[BANK_NUMBER] > f->locals.numbers || df->vars.n[BANK_STRING] > f->locals.strings || df->vars.n[BANK_VARIANT] > f->locals.objects) {
        return 0;
    }
    if (f->proto != NONE32 && (sc->protos[f->proto].flags & 8u) != 0 && !ref_ieq(sc, sc->protos[f->proto].name, df->name)) {
        return 0;
    }
    return 1;
}

static void attach_debug(script *sc)
{
    uint32_t i;
    uint32_t j;

    if (sc->dbg_kind != 2) {
        return;
    }
    for (i = 0; i < sc->nfuncs; ++i) {
        sc->funcs[i].dbg = NONE32;
    }
    for (i = 0; i < sc->ndbg; ++i) {
        for (j = 0; j < sc->nfuncs; ++j) {
            if (sc->funcs[j].dbg == NONE32 && dbg_matches(sc, &sc->dbgfns[i], &sc->funcs[j])) {
                sc->funcs[j].dbg = i;
                break;
            }
        }
        if (j == sc->nfuncs) {
            for (j = 0; j < sc->nfuncs; ++j) {
                sc->funcs[j].dbg = NONE32;
            }
            return;
        }
    }
    sc->dbg_attached = 1;
}

/* Builds the decode context. */
static void make_context(const script *sc, xxbyte_installshield_context *ctx)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->block_addresses = sc->blocks;
    ctx->block_count = sc->nblocks;
    ctx->prototype_arguments = sc->argcounts;
    ctx->prototype_count = sc->argcounts != NULL ? sc->nprotos : 0;
}

/* End of block k: decodes its count actions; *ok is 0 when one fails. */
static size_t block_end(const script *sc, const xxbyte_installshield_context *ctx, uint32_t k, uint32_t *count, int *ok)
{
    size_t pos = sc->blocks[k];
    size_t header = sc->mode == XXBYTE_INSTALLSHIELD_MODE_INS ? 4u : 2u;
    uint32_t n;
    uint32_t i;
    xxbyte_instruction inst;

    *ok = 0;
    *count = 0;
    if (pos >= sc->size || sc->size - pos < header) {
        return pos < sc->size ? sc->size : pos;
    }
    n = xxbyte_u16le(sc->d + pos + header - 2u);
    *count = n;
    pos += header;
    for (i = 0; i < n; ++i) {
        if (pos >= sc->size || xxbyte_decode_ex(XXBYTE_FAMILY_INSTALLSHIELD, sc->mode, ctx, sc->d + pos, sc->size - pos, pos, &inst) != XXBYTE_STATUS_OK ||
            inst.id == XXBYTE_INSTALLSHIELD_ID_BLOCK) {
            return pos;
        }
        pos += inst.size;
    }
    *ok = 1;
    return pos;
}

/* The decoded action at pos when it is the given opcode. */
static int action_at(const script *sc, const xxbyte_installshield_context *ctx, size_t pos, uint32_t opcode, xxbyte_instruction *inst)
{
    return pos < sc->size && xxbyte_decode_ex(XXBYTE_FAMILY_INSTALLSHIELD, sc->mode, ctx, sc->d + pos, sc->size - pos, pos, inst) == XXBYTE_STATUS_OK &&
           inst->opcode == opcode;
}

/* Splits the INX blocks into functions (as the runtime does: a function
 * starts at the block its prototype names, the program block included). */
static xxbyte_status build_inx_functions(script *sc)
{
    xxbyte_installshield_context ctx;
    uint32_t k;
    uint32_t actions = 0;
    uint32_t *starts;

    sc->funcs = (func *)alloc_array((size_t)sc->nblocks + 1u, sizeof(func));
    starts = (uint32_t *)alloc_array(sc->nblocks, sizeof(uint32_t));
    if (sc->funcs == NULL || starts == NULL) {
        xx_mem_free(starts);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (k = 0; k < sc->nblocks; ++k) {
        starts[k] = NONE32;
    }
    for (k = sc->nprotos; k-- > 0;) {
        if ((sc->protos[k].flags & 4u) == 0 && sc->protos[k].block < sc->nblocks) {
            starts[sc->protos[k].block] = k;
        }
    }
    make_context(sc, &ctx);
    for (k = 0; k < sc->nblocks; ++k) {
        uint32_t p = starts[k];
        uint32_t count;
        int ok;
        size_t end = block_end(sc, &ctx, k, &count, &ok);
        func *f;
        if (p != NONE32 || sc->nfuncs == 0) {
            f = &sc->funcs[sc->nfuncs++];
            memset(f, 0, sizeof(*f));
            f->proto = p;
            f->first_block = k;
            f->act_start = actions;
            f->dbg = NONE32;
            if (p != NONE32) {
                sc->protos[p].func = sc->nfuncs - 1u;
            }
        }
        f = &sc->funcs[sc->nfuncs - 1u];
        f->last_block = k;
        actions += count;
        f->act_end = actions;
        if (!f->has_decl && sc->blocks[k] < sc->size && sc->size - sc->blocks[k] >= 2) {
            xxbyte_instruction inst;
            if (action_at(sc, &ctx, (size_t)sc->blocks[k] + 2u, XXBYTE_INSTALLSHIELD_OP_FUNCTION, &inst)) {
                xxbyte_operand data;
                memset(&data, 0, sizeof(data));
                if (inst.table_count == 0 && inst.operand_count == 1) {
                    data = inst.operands[0];
                }
                if (data.type == XXBYTE_OPERAND_DATA && (uint64_t)data.value < sc->size) {
                    rd r = reader(sc, (size_t)data.value);
                    if (read_decl(&r, &f->locals) == XXBYTE_STATUS_OK) {
                        f->has_decl = 1;
                    }
                }
            }
        }
        if (end > sc->code_end) {
            sc->code_end = end;
        }
    }
    xx_mem_free(starts);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status read_block_table(script *sc, size_t off, uint32_t count)
{
    rd r = reader(sc, off);
    uint32_t i;

    sc->nblocks = count;
    sc->blocks = (uint32_t *)alloc_array(count, sizeof(uint32_t));
    if (sc->blocks == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < count && !r.bad; ++i) {
        sc->blocks[i] = r32(&r);
    }
    return r.bad ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OK;
}

static xxbyte_status parse_inx(script *sc)
{
    rd r;
    uint32_t off_globals;
    uint32_t off_protos;
    uint32_t off_typedefs;
    uint32_t off_blocks;
    uint32_t off_debug;
    xxbyte_status status;

    if (sc->size < ALUZ_HEADER_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    sc->copyright.off = 6;
    while (sc->copyright.off + sc->copyright.len < 0x68u && sc->d[sc->copyright.off + sc->copyright.len] != 0) {
        ++sc->copyright.len;
    }
    off_globals = xxbyte_u32le(sc->d + 0x68);
    off_protos = xxbyte_u32le(sc->d + 0x6C);
    off_typedefs = xxbyte_u32le(sc->d + 0x70);
    off_blocks = xxbyte_u32le(sc->d + 0x74);
    off_debug = xxbyte_u32le(sc->d + 0x78);
    status = read_typedefs(sc, off_typedefs, 0);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    r = reader(sc, off_globals);
    status = read_decl(&r, &sc->globals);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    status = read_inx_prototypes(sc, off_protos);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    r = reader(sc, off_blocks);
    {
        uint32_t count = r16(&r);
        if (r.bad) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        status = read_block_table(sc, r.pos, count);
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (off_debug >= ALUZ_HEADER_SIZE && off_debug < sc->size) {
        status = read_debug(sc, off_debug);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    status = build_inx_functions(sc);
    if (status == XXBYTE_STATUS_OK) {
        attach_debug(sc);
    }
    return status;
}

static xxbyte_status parse_obs(script *sc)
{
    rd r;
    uint32_t count;
    uint32_t i;
    xxbyte_status status;

    if (sc->size < OBS_HEADER_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    /* externs */
    r = reader(sc, xxbyte_u32le(sc->d + 0x84));
    count = r16(&r);
    if (r.bad) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    sc->externs = (ext *)alloc_array(count, sizeof(ext));
    if (sc->externs == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < count && !r.bad; ++i) {
        uint32_t code = r8(&r);
        int64_t address = (int16_t)r16(&r);
        sref name = rstr(&r);
        if (code >= 1 && code <= 3) {
            ext *e = &sc->externs[sc->nexterns++];
            e->bank = code == 1 ? BANK_VARIANT : code == 2 ? BANK_STRING : BANK_NUMBER;
            e->address = address;
            e->name = name;
        }
    }
    if (r.bad) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    /* globals: numbers from the header, objects and strings from tables */
    sc->globals.numbers = xxbyte_u16le(sc->d + 0x74);
    r = reader(sc, xxbyte_u32le(sc->d + 0x80));
    sc->globals.objects = r16(&r);
    sc->globals.objects_off = r.pos;
    rskip(&r, (size_t)sc->globals.objects * 4u);
    if (r.bad) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    r = reader(sc, xxbyte_u32le(sc->d + 0x7C));
    sc->globals.sized = r16(&r);
    r16(&r);
    sc->globals.sized_off = r.pos;
    rskip(&r, (size_t)sc->globals.sized * 4u);
    sc->globals.strings = sc->globals.sized;
    if (r.bad) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    status = read_typedefs(sc, xxbyte_u32le(sc->d + 0x8C), 1);
    if (status == XXBYTE_STATUS_OK) {
        status = read_inx_prototypes(sc, xxbyte_u32le(sc->d + 0x88));
    }
    if (status == XXBYTE_STATUS_OK) {
        status = read_block_table(sc, xxbyte_u32le(sc->d + 0xD8), xxbyte_u16le(sc->d + 0x62));
    }
    if (status == XXBYTE_STATUS_OK) {
        status = build_inx_functions(sc);
    }
    return status;
}

/* INS (InstallShield 5). */

static xxbyte_status read_ins_externs(script *sc, rd *r, int bank)
{
    uint32_t count = r16(r);
    uint32_t i;
    ext *grown;

    if (r->bad) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    grown = (ext *)xx_mem_realloc(sc->externs, ((size_t)sc->nexterns + count + 1u) * sizeof(ext));
    if (grown == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    sc->externs = grown;
    for (i = 0; i < count && !r->bad; ++i) {
        ext *e = &sc->externs[sc->nexterns];
        e->bank = bank;
        e->address = r16(r);
        e->name = rstr(r);
        if (!r->bad) {
            ++sc->nexterns;
        }
    }
    return r->bad ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OK;
}

static xxbyte_status parse_ins(script *sc)
{
    rd r = reader(sc, 4);
    uint32_t events;
    uint32_t count;
    uint32_t i;
    uint32_t j;
    xxbyte_status status;
    xxbyte_installshield_context ctx;
    size_t pos;
    uint32_t *proto_at;

    sc->version_magic = r32(&r);
    r.pos = 0x0D;
    sc->copyright = rstr(&r);
    events = r16(&r);
    count = r16(&r);
    rskip(&r, (size_t)count * 2u);
    sc->globals.strings = count;
    status = read_ins_externs(sc, &r, BANK_STRING);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    sc->globals.numbers = r16(&r);
    status = read_ins_externs(sc, &r, BANK_NUMBER);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    /* structs */
    sc->ntypedefs = r16(&r);
    sc->typedefs_off = r.pos;
    sc->named_typedefs = 1;
    for (i = 0; i < sc->ntypedefs && !r.bad; ++i) {
        uint32_t fields;
        r16(&r);
        rstr(&r);
        fields = r16(&r);
        for (j = 0; j < fields && !r.bad; ++j) {
            r16(&r);
            r16(&r);
            rstr(&r);
        }
    }
    /* prototypes */
    sc->nprotos = r16(&r);
    if (r.bad) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    sc->protos = (proto *)alloc_array(sc->nprotos, sizeof(proto));
    sc->argcounts = (uint16_t *)alloc_array(sc->nprotos, sizeof(uint16_t));
    if (sc->protos == NULL || sc->argcounts == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < sc->nprotos && !r.bad; ++i) {
        proto *p = &sc->protos[i];
        uint32_t kind = r8(&r);
        r8(&r);
        p->func = NONE32;
        p->dbg = NONE32;
        p->block = 0xFFFFu;
        p->ret = 4;
        p->wide_args = 1;
        if (kind == 2) {
            p->flags = 2;
            p->dll = rstr(&r);
            p->name = rstr(&r);
            r16(&r);
        } else if (kind == 1) {
            p->flags = 1;
            p->ret = r16(&r);
            p->dll = rstr(&r);
            p->name = rstr(&r);
        }
        p->argc = r16(&r);
        p->args_off = r.pos;
        rskip(&r, (size_t)p->argc * 4u);
        sc->argcounts[i] = (uint16_t)p->argc;
    }
    if (r.bad) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    /* labels: first pass without the block table */
    sc->blocks = (uint32_t *)alloc_array(events, sizeof(uint32_t));
    sc->funcs = (func *)alloc_array((size_t)events + 1u, sizeof(func));
    proto_at = (uint32_t *)alloc_array(events, sizeof(uint32_t));
    if (sc->blocks == NULL || sc->funcs == NULL || proto_at == NULL) {
        xx_mem_free(proto_at);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < events; ++i) {
        proto_at[i] = NONE32;
    }
    make_context(sc, &ctx);
    ctx.block_addresses = NULL;
    ctx.block_count = 0;
    pos = r.pos;
    sc->funcs[0].proto = NONE32;
    sc->funcs[0].dbg = NONE32;
    sc->nfuncs = 1;
    status = XXBYTE_STATUS_OK;
    for (i = 0; i < events; ++i) {
        xxbyte_instruction inst;
        uint32_t n;
        if (pos > UINT32_MAX || !action_at(sc, &ctx, pos, XXBYTE_INSTALLSHIELD_INS_OP_LABEL, &inst) || inst.id != XXBYTE_INSTALLSHIELD_ID_BLOCK) {
            status = pos >= sc->size ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
            break;
        }
        sc->blocks[sc->nblocks++] = (uint32_t)pos;
        sc->funcs[sc->nfuncs - 1u].last_block = i;
        n = (uint32_t)inst.operands[inst.operand_count - 1u].value;
        pos += inst.size;
        for (j = 0; j < n; ++j) {
            if (pos >= sc->size || xxbyte_decode_ex(XXBYTE_FAMILY_INSTALLSHIELD, sc->mode, &ctx, sc->d + pos, sc->size - pos, pos, &inst) != XXBYTE_STATUS_OK ||
                inst.id == XXBYTE_INSTALLSHIELD_ID_BLOCK) {
                status = pos >= sc->size ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
                break;
            }
            if (inst.opcode == XXBYTE_INSTALLSHIELD_INS_OP_FUNCTION && (sc->nfuncs == 1 || sc->funcs[sc->nfuncs - 1u].first_block != i)) {
                func *f;
                if (i != 0) {
                    sc->funcs[sc->nfuncs - 1u].last_block = i - 1u;
                }
                f = &sc->funcs[sc->nfuncs++];
                memset(f, 0, sizeof(*f));
                f->proto = NONE32;
                f->dbg = NONE32;
                f->first_block = i;
                f->last_block = i;
                f->has_decl = 1;
                f->locals.strings = (uint32_t)inst.operands[0].value;
                f->locals.numbers = (uint32_t)inst.operands[1].value;
                if (i == 0) {
                    /* the code before the first prolog is empty */
                    sc->funcs[0] = *f;
                    --sc->nfuncs;
                }
            }
            if (inst.opcode == XXBYTE_INSTALLSHIELD_INS_OP_CALL && inst.operands[1].type == XXBYTE_OPERAND_INDEX && inst.operands[1].value < (int64_t)events &&
                proto_at[inst.operands[1].value] == NONE32) {
                proto_at[inst.operands[1].value] = (uint32_t)inst.operands[0].value;
            }
            pos += inst.size;
        }
        if (status != XXBYTE_STATUS_OK) {
            break;
        }
    }
    sc->code_end = pos;
    /* name the bodies after the calls that reach them */
    for (i = 1; i < sc->nfuncs; ++i) {
        uint32_t p = proto_at[sc->funcs[i].first_block];
        if (p != NONE32 && p < sc->nprotos && sc->protos[p].func == NONE32 && sc->protos[p].flags == 2) {
            sc->funcs[i].proto = p;
            sc->protos[p].func = i;
            sc->protos[p].block = sc->funcs[i].first_block;
        }
    }
    xx_mem_free(proto_at);
    if (status != XXBYTE_STATUS_OK && sc->nblocks == 0) {
        return status;
    }
    return XXBYTE_STATUS_OK;
}

/* Resolver. */

static size_t prototype_name(const script *sc, uint32_t index, char *buffer, size_t buffer_size)
{
    const proto *p;
    xx_buf_t b;
    size_t n = 0;

    if (index >= sc->nprotos) {
        return 0;
    }
    p = &sc->protos[index];
    if (p->func != NONE32 && sc->funcs[p->func].dbg != NONE32) {
        return copy_ref(buffer, buffer_size, sc, sc->dbgfns[sc->funcs[p->func].dbg].name);
    }
    if (p->name.len == 0) {
        return 0;
    }
    if ((p->flags & 1u) == 0 || p->dll.len == 0) {
        return copy_ref(buffer, buffer_size, sc, p->name);
    }
    xx_buf_init(&b);
    if (put_ref(&b, sc, p->dll) && xx_buf_append_char(&b, '.') && put_ref(&b, sc, p->name) && xx_buf_ok(&b) && b.size < buffer_size) {
        memcpy(buffer, b.data, b.size + 1u);
        n = b.size;
    }
    xx_buf_free(&b);
    return n;
}

static int bank_of(uint16_t flags)
{
    return (flags & XXBYTE_INSTALLSHIELD_OPERAND_FLAG_STRING) != 0 ? BANK_STRING : (flags & XXBYTE_INSTALLSHIELD_OPERAND_FLAG_NUMBER) != 0 ? BANK_NUMBER : BANK_VARIANT;
}

static size_t resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const script *sc = (const script *)context;
    const xxbyte_operand *o = &instruction->operands[operand_index];
    uint32_t i;

    switch (o->type) {
        case XXBYTE_OPERAND_INDEX: break;
        default: return 0;
    }
    switch (o->kind) {
        case XXBYTE_INDEX_FUNCTION: return o->value >= 0 && o->value <= UINT32_MAX ? prototype_name(sc, (uint32_t)o->value, buffer, buffer_size) : 0;
        case XXBYTE_INDEX_TYPE:
            if (sc->named_typedefs && o->value >= 0 && o->value < (int64_t)sc->ntypedefs) {
                return copy_ref(buffer, buffer_size, sc, sc->typedef_names[o->value]);
            }
            return 0;
        case XXBYTE_INDEX_GLOBAL: {
            int bank = bank_of(o->flags);
            if (sc->kind != XXBYTE_INSTALLSHIELD_FILE_INS && bank == BANK_VARIANT && o->value >= 0 && o->value <= 2) {
                static const char *const reserved[3] = {"LAST_RESULT", "NOTHING", "Err"};
                return copy_text(buffer, buffer_size, reserved[o->value]);
            }
            for (i = 0; i < sc->nexterns; ++i) {
                if (sc->externs[i].bank == bank && sc->externs[i].address == o->value && sc->externs[i].name.len != 0) {
                    return copy_ref(buffer, buffer_size, sc, sc->externs[i].name);
                }
            }
            if (sc->dbg_attached && o->value >= 0 && o->value < (int64_t)sc->dbg_globals.n[bank]) {
                return copy_ref(buffer, buffer_size, sc, sc->dbg_globals.v[bank][o->value]);
            }
            return 0;
        }
        case XXBYTE_INDEX_LOCAL: {
            int bank = bank_of(o->flags);
            const func *f = sc->cur_func < sc->nfuncs ? &sc->funcs[sc->cur_func] : NULL;
            if (f != NULL && f->dbg != NONE32 && o->value >= 1 && o->value <= (int64_t)sc->dbgfns[f->dbg].vars.n[bank]) {
                return copy_ref(buffer, buffer_size, sc, sc->dbgfns[f->dbg].vars.v[bank][o->value - 1]);
            }
            return 0;
        }
        default: return 0;
    }
}

static size_t resolve_all(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_operand *o = &instruction->operands[operand_index];

    if (o->type == XXBYTE_OPERAND_BRANCH) {
        xx_buf_t b;
        size_t n = 0;
        xx_buf_init(&b);
        if (xx_buf_appendf(&b, "block_%llu", (unsigned long long)o->extra) && b.size < buffer_size) {
            memcpy(buffer, b.data, b.size + 1u);
            n = b.size;
        }
        xx_buf_free(&b);
        return n;
    }
    return resolve(context, instruction, operand_index, buffer, buffer_size);
}

/* Listing. */

static int put_type(xx_buf_t *out, uint32_t type)
{
    const char *name = xxbyte_installshield_type_name(type);
    return name != NULL ? xx_buf_append_str(out, name) : xx_buf_appendf(out, "TYPE_%u", type);
}

static int put_proto_name(xx_buf_t *out, const script *sc, uint32_t index)
{
    char name[512];
    size_t n = prototype_name(sc, index, name, sizeof(name));
    return n != 0 ? xx_buf_append_str(out, name) : xx_buf_appendf(out, "func_%u", index);
}

static int put_prototype(xx_buf_t *out, const script *sc, uint32_t index)
{
    const proto *p = &sc->protos[index];
    uint32_t i;

    if ((p->flags & 4u) != 0) {
        return xx_buf_appendf(out, "predefined func_%u", index);
    }
    if ((p->flags & 8u) != 0 && !xx_buf_append_str(out, "export ")) {
        return 0;
    }
    if (!xx_buf_append_str(out, "prototype ")) {
        return 0;
    }
    if ((p->flags & 0x10u) != 0 && !xx_buf_append_str(out, "cdecl ")) {
        return 0;
    }
    if (!put_type(out, p->ret) || !xx_buf_append_char(out, ' ') || !put_proto_name(out, sc, index) || !xx_buf_append_char(out, '(')) {
        return 0;
    }
    for (i = 0; i < p->argc; ++i) {
        uint32_t type;
        uint32_t how;
        if (p->wide_args) {
            type = xxbyte_u16le(sc->d + p->args_off + (size_t)i * 4u);
            how = xxbyte_u16le(sc->d + p->args_off + (size_t)i * 4u + 2u);
        } else {
            type = sc->d[p->args_off + (size_t)i * 2u];
            how = sc->d[p->args_off + (size_t)i * 2u + 1u];
        }
        if ((i != 0 && !xx_buf_append_str(out, ", ")) || ((p->wide_args ? (how == 2 || how == 3) : (how >= 2 && how <= 4)) && !xx_buf_append_str(out, "BYREF ")) ||
            !put_type(out, type)) {
            return 0;
        }
    }
    if ((p->flags & 0x80u) != 0 && !xx_buf_append_str(out, p->argc != 0 ? ", ..." : "...")) {
        return 0;
    }
    return xx_buf_append_char(out, ')');
}

static int put_decl(xx_buf_t *out, const decl *d)
{
    return xx_buf_appendf(out, "%u numbers, %u strings, %u objects", d->numbers, d->strings, d->objects);
}

static int list_tables(const script *sc, xx_buf_t *out)
{
    uint32_t i;
    uint32_t j;
    rd r;

    if (!xx_buf_append_str(out, "; globals: ") || !put_decl(out, &sc->globals) || !xx_buf_append_char(out, '\n')) {
        return 0;
    }
    for (i = 0; i < sc->globals.sized; ++i) {
        const uint8_t *e = sc->d + sc->globals.sized_off + (size_t)i * 4u;
        if (!xx_buf_appendf(out, ";   STRING s%u[%u]\n", (unsigned)xxbyte_u16le(e), (unsigned)xxbyte_u16le(e + 2))) {
            return 0;
        }
    }
    if (sc->kind != XXBYTE_INSTALLSHIELD_FILE_INS) {
        for (i = 0; i < sc->globals.objects; ++i) {
            const uint8_t *e = sc->d + sc->globals.objects_off + (size_t)i * 4u;
            if (!xx_buf_appendf(out, ";   v%u: flags 0x%X, %s %d\n", i, (unsigned)xxbyte_u16le(e), (xxbyte_u16le(e) & 0x2000u) != 0 ? "count" : "typedef",
                                (xxbyte_u16le(e) & 0x2000u) != 0 ? (int)xxbyte_u16le(e + 2) : (int)(int16_t)xxbyte_u16le(e + 2))) {
                return 0;
            }
        }
    }
    for (i = 0; i < sc->nexterns; ++i) {
        const ext *e = &sc->externs[i];
        static const char banks[3] = {'n', 's', 'v'};
        if (!xx_buf_appendf(out, "; %s %c%lld: ", sc->kind == XXBYTE_INSTALLSHIELD_FILE_INS ? "system variable" : "extern", banks[e->bank], (long long)e->address) ||
            !put_ref(out, sc, e->name) || !xx_buf_append_char(out, '\n')) {
            return 0;
        }
    }
    r = reader(sc, sc->typedefs_off);
    for (i = 0; i < sc->ntypedefs && !r.bad; ++i) {
        uint32_t members;
        if (!xx_buf_appendf(out, "; typedef_%u", i)) {
            return 0;
        }
        if (sc->kind == XXBYTE_INSTALLSHIELD_FILE_INS) {
            r16(&r);
        }
        if (sc->named_typedefs) {
            sref name = rstr(&r);
            if (!xx_buf_append_char(out, ' ') || !put_ref(out, sc, name)) {
                return 0;
            }
        }
        if (!xx_buf_append_char(out, ':')) {
            return 0;
        }
        members = r16(&r);
        for (j = 0; j < members && !r.bad; ++j) {
            uint32_t type;
            uint32_t rep;
            sref name;
            if (sc->kind == XXBYTE_INSTALLSHIELD_FILE_INS) {
                type = r16(&r);
                rep = r16(&r);
            } else {
                type = r8(&r);
                rep = r16(&r);
            }
            name = rstr(&r);
            if (r.bad) {
                break;
            }
            if (!xx_buf_append_char(out, ' ')) {
                return 0;
            }
            if (type == 9 && sc->kind != XXBYTE_INSTALLSHIELD_FILE_INS) {
                if (!xx_buf_appendf(out, "typedef_%u ", rep) || !put_ref(out, sc, name)) {
                    return 0;
                }
            } else if (!put_type(out, type) || !xx_buf_append_char(out, ' ') || !put_ref(out, sc, name) ||
                       (sc->kind != XXBYTE_INSTALLSHIELD_FILE_INS && rep > 1 && !xx_buf_appendf(out, (type == 0 || type == 12) ? "[%u]" : "(%u)", rep))) {
                return 0;
            }
            if (!xx_buf_append_char(out, ';')) {
                return 0;
            }
        }
        if (!xx_buf_append_char(out, '\n')) {
            return 0;
        }
    }
    for (i = 0; i < sc->nprotos; ++i) {
        if (!xx_buf_appendf(out, "; func_%u: ", i) || !put_prototype(out, sc, i) || !xx_buf_append_char(out, '\n')) {
            return 0;
        }
    }
    return 1;
}

static xxbyte_status list_region(const script *sc, const xxbyte_installshield_context *ctx, size_t start, size_t end, uint32_t listing_flags, xx_buf_t *out)
{
    if (end <= start) {
        return XXBYTE_STATUS_OK;
    }
    return xxbyte_disasm_buf_ex(XXBYTE_FAMILY_INSTALLSHIELD, sc->mode, ctx, sc->d + start, end - start, start, listing_flags, resolve_all, (void *)sc, out);
}

static int put_function_header(xx_buf_t *out, const script *sc, uint32_t index)
{
    const func *f = &sc->funcs[index];

    if (!xx_buf_append_str(out, "\n; ")) {
        return 0;
    }
    if (f->proto != NONE32) {
        if (!xx_buf_appendf(out, "function func_%u: ", f->proto) || !put_prototype(out, sc, f->proto)) {
            return 0;
        }
    } else if (index == 0 && (sc->kind == XXBYTE_INSTALLSHIELD_FILE_INS || f->first_block == 0)) {
        if (!xx_buf_append_str(out, sc->kind == XXBYTE_INSTALLSHIELD_FILE_INS ? "program" : "code outside any function")) {
            return 0;
        }
    } else if (!xx_buf_appendf(out, "function (no prototype)")) {
        return 0;
    }
    if (f->dbg != NONE32 && f->proto == NONE32 && (!xx_buf_append_str(out, " name ") || !put_ref(out, sc, sc->dbgfns[f->dbg].name))) {
        return 0;
    }
    if (!xx_buf_appendf(out, " ; %s %u-%u", sc->kind == XXBYTE_INSTALLSHIELD_FILE_INS ? "labels" : "blocks", f->first_block, f->last_block)) {
        return 0;
    }
    if (f->has_decl && (!xx_buf_append_str(out, ", locals: ") || !put_decl(out, &f->locals))) {
        return 0;
    }
    return xx_buf_append_char(out, '\n');
}

static xxbyte_status list_functions(script *sc, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_installshield_context ctx;
    uint32_t i;
    uint32_t k;
    xxbyte_status status;

    make_context(sc, &ctx);
    for (i = 0; i < sc->nfuncs; ++i) {
        const func *f = &sc->funcs[i];
        sc->cur_func = i;
        if (!put_function_header(out, sc, i)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        for (k = f->first_block; k <= f->last_block && k < sc->nblocks; ++k) {
            uint32_t count;
            int ok;
            size_t start = sc->blocks[k];
            size_t end;
            if (start >= sc->size) {
                if (!xx_buf_appendf(out, "; block %u at 0x%llX is outside the file\n", k, (unsigned long long)start)) {
                    return XXBYTE_STATUS_OUT_OF_MEMORY;
                }
                continue;
            }
            end = block_end(sc, &ctx, k, &count, &ok);
            if (!ok) {
                /* list the undecodable rest up to the next block */
                size_t limit = sc->size;
                if (k + 1u < sc->nblocks && sc->blocks[k + 1u] > end && sc->blocks[k + 1u] < limit) {
                    limit = sc->blocks[k + 1u];
                } else if (limit - end > 16u) {
                    limit = end + 16u;
                }
                end = end < limit ? limit : end;
            }
            status = list_region(sc, &ctx, start, end, listing_flags, out);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
        }
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status list_script(script *sc, uint32_t listing_flags, xx_buf_t *out)
{
    static const char *const kinds[] = {"", "setup.inx", "setup.inx (encrypted)", "script object (.obs)", "", "InstallShield 5 script (setup.ins)"};

    if (!xx_buf_appendf(out, "; InstallShield compiled script: %s\n", kinds[sc->kind])) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (sc->kind == XXBYTE_INSTALLSHIELD_FILE_INS) {
        const char *version = sc->version_magic == 0x1234001Cu   ? "2.20.905"
                              : sc->version_magic == 0x12340016u ? "3.00.065"
                              : sc->version_magic == 0x00010050u ? "5.00.000"
                                                                 : NULL;
        if ((version != NULL ? !xx_buf_appendf(out, "; file version: %s\n", version) : !xx_buf_appendf(out, "; file version magic: 0x%08X\n", sc->version_magic))) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    if (sc->copyright.len != 0 && (!xx_buf_append_str(out, "; ") || !put_ref(out, sc, sc->copyright) || !xx_buf_append_char(out, '\n'))) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (sc->dbg_kind == 1 && (!xx_buf_append_str(out, "; debug symbols: ") || !put_ref(out, sc, sc->dbg_path) || !xx_buf_append_char(out, '\n'))) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (sc->dbg_kind >= 2 && !xx_buf_append_str(out, sc->dbg_kind == 3  ? "; debug symbols: embedded, unreadable\n"
                                                     : sc->dbg_attached ? "; debug symbols: embedded\n"
                                                                        : "; debug symbols: embedded, do not match the code\n")) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (!xx_buf_appendf(out, "; %u %s, %u prototypes, %u typedefs\n", sc->nblocks, sc->kind == XXBYTE_INSTALLSHIELD_FILE_INS ? "labels" : "blocks", sc->nprotos,
                        sc->ntypedefs) ||
        !list_tables(sc, out)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    {
        xxbyte_status status = list_functions(sc, listing_flags, out);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    if (sc->kind == XXBYTE_INSTALLSHIELD_FILE_INS && sc->code_end < sc->size) {
        size_t rest = sc->size - sc->code_end;
        uint8_t last = sc->d[sc->size - 1u];
        if (!xx_buf_appendf(out, "\n; %llu bytes after the code\n", (unsigned long long)rest)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        if (last != 0 && (size_t)last + 1u <= rest) {
            sref v;
            v.off = sc->size - 1u - last;
            v.len = last;
            if (!xx_buf_append_str(out, "; compiler: ") || !put_ref(out, sc, v) || !xx_buf_append_char(out, '\n')) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status list_one(uint32_t mode, xxbyte_installshield_file_kind kind, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    script sc;
    xxbyte_status status;

    memset(&sc, 0, sizeof(sc));
    sc.d = data;
    sc.size = size;
    sc.kind = (int)kind;
    sc.cur_func = NONE32;
    sc.mode = kind == XXBYTE_INSTALLSHIELD_FILE_INS ? XXBYTE_INSTALLSHIELD_MODE_INS : XXBYTE_INSTALLSHIELD_MODE_INX;
    (void)mode;
    switch (kind) {
        case XXBYTE_INSTALLSHIELD_FILE_INX:
        case XXBYTE_INSTALLSHIELD_FILE_INX_ENCRYPTED: status = parse_inx(&sc); break;
        case XXBYTE_INSTALLSHIELD_FILE_OBS: status = parse_obs(&sc); break;
        default: status = parse_ins(&sc); break;
    }
    if (status == XXBYTE_STATUS_OK) {
        status = list_script(&sc, listing_flags, out);
    }
    free_script(&sc);
    return status;
}

static xxbyte_status list_library(const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    script sc;
    rd r;
    uint32_t count;
    uint32_t i;

    memset(&sc, 0, sizeof(sc));
    sc.d = data;
    sc.size = size;
    r = reader(&sc, 4);
    if (r32(&r) != 1 || r.bad) {
        return r.bad ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    count = r32(&r);
    if (r.bad) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (!xx_buf_appendf(out, "; InstallShield script library (.obl): %u members\n", count)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < count; ++i) {
        sref name = rstr(&r);
        uint32_t offset = r32(&r);
        uint32_t length = r32(&r);
        xxbyte_status status;
        if (r.bad) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (!xx_buf_append_str(out, "\n; ===== member ") || !put_ref(out, &sc, name) || !xx_buf_append_str(out, " =====\n")) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        if (offset > size || length > size - offset) {
            if (!xx_buf_append_str(out, "; member runs past the end of the library\n")) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            continue;
        }
        if (length < 4 || magic_kind(data + offset) != XXBYTE_INSTALLSHIELD_FILE_OBS) {
            if (!xx_buf_append_str(out, "; not a script object\n")) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            continue;
        }
        status = list_one(XXBYTE_INSTALLSHIELD_MODE_INX, XXBYTE_INSTALLSHIELD_FILE_OBS, data + offset, length, listing_flags, out);
        if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
            return status;
        }
        if (status != XXBYTE_STATUS_OK && !xx_buf_appendf(out, "; could not list the member: %s\n", xxbyte_status_string(status))) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_is_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_installshield_file_kind kind = xxbyte_installshield_detect(data, size);
    xxbyte_status status;

    if (kind == XXBYTE_INSTALLSHIELD_FILE_UNKNOWN || kind == XXBYTE_INSTALLSHIELD_FILE_DBG) {
        return size < 4 && size != 0 ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (mode != XXBYTE_MODE_AUTO && (mode == XXBYTE_INSTALLSHIELD_MODE_INS) != (kind == XXBYTE_INSTALLSHIELD_FILE_INS)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (kind == XXBYTE_INSTALLSHIELD_FILE_OBL) {
        return list_library(data, size, listing_flags, out);
    }
    if (kind == XXBYTE_INSTALLSHIELD_FILE_INX_ENCRYPTED) {
        uint8_t *copy = (uint8_t *)xx_mem_alloc(size);
        if (copy == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        memcpy(copy, data, size);
        xxbyte_installshield_decrypt(copy, size, 0);
        status = list_one(mode, kind, copy, size, listing_flags, out);
        xx_mem_free(copy);
        return status;
    }
    return list_one(mode, kind, data, size, listing_flags, out);
}

#else
typedef int xxbyte_installshield_file_disabled;
#endif
