#include "xxbyte_vb_file.h"
#include "xxbyte_vb_names.h"
#include "xxbyte_vb_pe.h"

#if XXBYTE_USE_VB

#include "xxbyte/xxbyte_vb.h"
#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

/*
 * The container listing of a compiled Visual Basic 5/6 image (see
 * xxbyte_vb.h): PE headers -> VB header (EXEPROJECTINFO, "VB5!") ->
 * ProjectInfo -> object table -> public object descriptors -> ObjectInfo
 * (method table and constant pool) and, for forms and user controls, the
 * optional object info whose control list maps event-sink slots to
 * procedure descriptors.
 */

#define VB_MAGIC 0x21354256u /* "VB5!" */
#define VB_PROJECT_VERSION 0x1F4u
#define VB_HEADER_SIZE 0x68u
#define VB_PUBLIC_OBJECT_SIZE 0x30u
#define VB_OBJECT_INFO_SIZE 0x38u
#define VB_OPTIONAL_INFO_SIZE 0x40u
#define VB_CONTROL_SIZE 0x28u
#define VB_EVENT_TABLE_HEADER 0x18u
#define VB_HAS_OPTIONAL_INFO 0x80u
#define VB_MAX_OBJECTS 0x10000u
#define VB_MAX_PROCS 0x10000u
#define VB_MAX_CONTROLS 0x4000u
#define VB_MAX_NAME 255u
#define VB_MAX_STRING_CHARS 120u
#define VB_GUID_BASE 0x33AD4ED0u

typedef struct vb_proc {
    uint32_t desc_va;
    uint32_t object;
    uint32_t method;
    uint32_t slot;           /* event-sink slot when control != NULL */
    const char *control;     /* control name of an event handler */
    const char *event;       /* its event name, NULL when unknown */
    const char *public_name; /* lpMethodNames entry */
} vb_proc;

typedef struct vb_object {
    uint32_t va; /* public object descriptor */
    uint32_t info_va;
    uint32_t type;
    const char *name;
    uint32_t method_count;
    uint32_t methods_va;
    uint32_t pool_count;
    uint32_t pool_va;
} vb_object;

typedef struct vb_file {
    xxpe_image pe;
    uint32_t mode;
    uint32_t header_va;
    uint32_t object_count;
    vb_object *objects;
    vb_proc *procs;
    uint32_t proc_count;
    uint32_t *hash; /* open addressing: proc index + 1, 0 = empty */
    uint32_t hash_mask;
    int runtime60; /* imports MSVBVM60.DLL */
    int runtime50;
    size_t budget;       /* P-code bytes still to list; hostile images can make
                            many overlapping 64 KB procedures */
    size_t event_budget; /* event-table entries still to read */
} vb_file;

typedef struct vb_resolve_context {
    const vb_file *file;
    const vb_object *object;
} vb_resolve_context;

static uint32_t vb_u32(const vb_file *f, uint32_t va, int *ok)
{
    int here;
    uint32_t value = xxpe_u32_va(&f->pe, va, &here);

    if (!here) {
        *ok = 0;
    }
    return value;
}

static uint16_t vb_u16(const vb_file *f, uint32_t va, int *ok)
{
    int here;
    uint16_t value = xxpe_u16_va(&f->pe, va, &here);

    if (!here) {
        *ok = 0;
    }
    return value;
}

/* A NUL-terminated string at a virtual address, or NULL. */
static const char *vb_str(const vb_file *f, uint32_t va)
{
    int ok;
    uint32_t rva = xxpe_rva_of(&f->pe, va, &ok);

    return ok && va != 0 ? xxpe_str(&f->pe, rva, VB_MAX_NAME) : NULL;
}

/* Nonzero when count records of record_size bytes at va are in the file. */
static int vb_array_ok(const vb_file *f, uint32_t va, uint64_t count, uint32_t record_size)
{
    uint64_t length = count * record_size;

    return length <= 0xFFFFFFFFu && (length == 0 || xxpe_at_va(&f->pe, va, (uint32_t)length) != NULL);
}

/* Validates a VB header at va: magic, ProjectInfo version and an object
 * table in the file. */
static int vb_header_ok(const vb_file *f, uint32_t va)
{
    int ok = 1;
    uint32_t project;
    uint32_t table;

    if (vb_u32(f, va, &ok) != VB_MAGIC || !vb_array_ok(f, va, 1, VB_HEADER_SIZE)) {
        return 0;
    }
    project = vb_u32(f, va + 0x30u, &ok);
    if (!ok || vb_u32(f, project, &ok) != VB_PROJECT_VERSION || !vb_array_ok(f, project, 1, 0x24u)) {
        return 0;
    }
    table = vb_u32(f, project + 4u, &ok);
    return ok && vb_array_ok(f, table, 1, 0x54u);
}

/* The VB header: the "push offset VBHeader; call ThunRTMain" entry point,
 * else the first valid "VB5!" signature in a section (DLLs, OCXs). */
static uint32_t vb_find_header(const vb_file *f)
{
    const xxpe_image *pe = &f->pe;
    const uint8_t *entry = xxpe_at(pe, pe->entry_rva, 10u);
    uint32_t i;

    if (entry != NULL && entry[0] == 0x68u && entry[5] == 0xE8u && vb_header_ok(f, xxbyte_u32le(entry + 1))) {
        return xxbyte_u32le(entry + 1);
    }
    for (i = 0; i < pe->section_count; ++i) {
        const xxpe_section *s = &pe->sections[i];
        uint32_t k;

        for (k = 0; k + 4u <= s->raw_size; ++k) {
            const uint8_t *p = pe->data + s->raw_offset + k;
            uint64_t va = pe->image_base + s->rva + k;

            if (p[0] == 'V' && p[1] == 'B' && p[2] == '5' && p[3] == '!' && va <= 0xFFFFFFFFu && vb_header_ok(f, (uint32_t)va)) {
                return (uint32_t)va;
            }
        }
    }
    return 0;
}

static int vb_name_equal(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        char x = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        char y = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;

        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

/* Calls visit for each import descriptor until it returns nonzero. */
typedef int (*vb_import_visit)(void *context, const char *dll, uint32_t thunk_rva, uint32_t lookup_rva);

static void vb_each_import(const vb_file *f, vb_import_visit visit, void *context)
{
    const xxpe_image *pe = &f->pe;
    uint32_t rva;
    uint32_t n;

    if (pe->dir_count <= XXPE_DIR_IMPORT || pe->dir_rva[XXPE_DIR_IMPORT] == 0) {
        return;
    }
    rva = pe->dir_rva[XXPE_DIR_IMPORT];
    for (n = 0; n < 4096u; ++n, rva += 20u) {
        const uint8_t *p = xxpe_at(pe, rva, 20u);
        const char *dll;
        uint32_t lookup;
        uint32_t thunk;

        if (p == NULL || rva > 0xFFFFFFFFu - 20u) {
            return;
        }
        lookup = xxbyte_u32le(p);
        thunk = xxbyte_u32le(p + 16);
        if (lookup == 0 && thunk == 0 && xxbyte_u32le(p + 12) == 0) {
            return;
        }
        dll = xxpe_str(pe, xxbyte_u32le(p + 12), VB_MAX_NAME);
        if (dll != NULL && visit(context, dll, thunk, lookup != 0 ? lookup : thunk)) {
            return;
        }
    }
}

static int vb_note_runtime(void *context, const char *dll, uint32_t thunk_rva, uint32_t lookup_rva)
{
    vb_file *f = (vb_file *)context;

    (void)thunk_rva;
    (void)lookup_rva;
    if (vb_name_equal(dll, "MSVBVM60.DLL")) {
        f->runtime60 = 1;
    } else if (vb_name_equal(dll, "MSVBVM50.DLL")) {
        f->runtime50 = 1;
    }
    return 0;
}

typedef struct vb_import_query {
    uint32_t rva; /* IAT slot */
    const char *dll;
    uint32_t thunk_rva;
    uint32_t lookup_rva;
} vb_import_query;

static size_t vb_copy(char *buffer, size_t size, size_t at, const char *text)
{
    while (*text != '\0' && at + 1u < size) {
        char c = *text++;
        buffer[at++] = (c > 0x20 && c < 0x7F) ? c : '?';
    }
    if (size != 0) {
        buffer[at < size ? at : size - 1u] = '\0';
    }
    return at;
}

static size_t vb_copy_number(char *buffer, size_t size, size_t at, const char *prefix, uint32_t value)
{
    char text[24];
    char digits[12];
    size_t n = 0;
    size_t d = 0;

    while (*prefix != '\0' && n < 8u) {
        text[n++] = *prefix++;
    }
    do {
        digits[d++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    while (d != 0) {
        text[n++] = digits[--d];
    }
    text[n] = '\0';
    return vb_copy(buffer, size, at, text);
}

/* Picks the import table holding the IAT slot: the one with the closest
 * start at or below it. */
static int vb_pick_import(void *context, const char *dll, uint32_t thunk_rva, uint32_t lookup_rva)
{
    vb_import_query *q = (vb_import_query *)context;

    if (q->rva >= thunk_rva && (q->rva - thunk_rva) % 4u == 0 && (q->dll == NULL || thunk_rva > q->thunk_rva)) {
        q->dll = dll;
        q->thunk_rva = thunk_rva;
        q->lookup_rva = lookup_rva;
    }
    return 0;
}

/* Writes DLL!name (or DLL!#ordinal) for an IAT slot; returns its length,
 * 0 when the slot is not an import. */
static size_t vb_import_name(const vb_file *f, uint32_t slot_rva, char *buffer, size_t size)
{
    const xxpe_image *pe = &f->pe;
    vb_import_query q;
    const uint8_t *p;
    uint32_t index;
    uint32_t entry;
    size_t length;

    memset(&q, 0, sizeof(q));
    q.rva = slot_rva;
    vb_each_import(f, vb_pick_import, &q);
    if (q.dll == NULL) {
        return 0;
    }
    index = (slot_rva - q.thunk_rva) / 4u;
    p = (uint64_t)q.lookup_rva + 4u * (uint64_t)index <= 0xFFFFFFFFu ? xxpe_at(pe, q.lookup_rva + 4u * index, 4u) : NULL;
    entry = p != NULL ? xxbyte_u32le(p) : 0u;
    if (entry == 0) {
        return 0;
    }
    length = vb_copy(buffer, size, 0, q.dll);
    length = vb_copy(buffer, size, length, "!");
    if ((entry & 0x80000000u) != 0) {
        uint32_t ordinal = entry & 0xFFFFu;

        if (vb_name_equal(q.dll, "MSVBVM60.DLL") && ordinal >= XXBYTE_VB_MSVBVM60_ORDINAL_FIRST &&
            ordinal - XXBYTE_VB_MSVBVM60_ORDINAL_FIRST < XXBYTE_VB_MSVBVM60_ORDINAL_COUNT &&
            xxbyte_vb_msvbvm60_ordinals[ordinal - XXBYTE_VB_MSVBVM60_ORDINAL_FIRST] != NULL) {
            return vb_copy(buffer, size, length, xxbyte_vb_msvbvm60_ordinals[ordinal - XXBYTE_VB_MSVBVM60_ORDINAL_FIRST]);
        }
        return vb_copy_number(buffer, size, length, "#", ordinal);
    }
    {
        const char *name = entry <= 0xFFFFFFFFu - 2u ? xxpe_str(pe, entry + 2u, VB_MAX_NAME) : NULL;

        return name != NULL ? vb_copy(buffer, size, length, name) : 0;
    }
}

static uint32_t vb_hash(uint32_t va)
{
    return (va >> 2) * 0x9E3779B1u;
}

static vb_proc *vb_proc_at(const vb_file *f, uint32_t desc_va)
{
    uint32_t h;

    if (f->hash == NULL) {
        return NULL;
    }
    for (h = vb_hash(desc_va) & f->hash_mask; f->hash[h] != 0; h = (h + 1u) & f->hash_mask) {
        if (f->procs[f->hash[h] - 1u].desc_va == desc_va) {
            return &f->procs[f->hash[h] - 1u];
        }
    }
    return NULL;
}

/* Writes "Object.Name" for a procedure: Object.Control_Event for an event
 * handler, Object.Name for a named method, Object.Proc_<method> otherwise. */
static size_t vb_proc_name(const vb_file *f, const vb_proc *p, char *buffer, size_t size)
{
    const vb_object *o = &f->objects[p->object];
    size_t at = vb_copy(buffer, size, 0, o->name != NULL ? o->name : "?");

    at = vb_copy(buffer, size, at, ".");
    if (p->control != NULL) {
        at = vb_copy(buffer, size, at, p->control);
        at = vb_copy(buffer, size, at, "_");
        if (p->event != NULL) {
            return vb_copy(buffer, size, at, p->event);
        }
        return vb_copy_number(buffer, size, at, "Event", p->slot);
    }
    if (p->public_name != NULL && p->public_name[0] != '\0') {
        return vb_copy(buffer, size, at, p->public_name);
    }
    return vb_copy_number(buffer, size, at, "Proc_", p->method);
}

/* Appends a BSTR literal as a quoted, escaped string. */
static size_t vb_quote(const uint8_t *chars, uint32_t count, char *buffer, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t at = 0;
    uint32_t i;

    if (size < 3u) {
        return 0;
    }
    buffer[at++] = '"';
    for (i = 0; i < count; ++i) {
        uint32_t c = xxbyte_u16le(chars + 2u * i);
        char esc[8];
        size_t n = 0;
        size_t k;

        if (i == VB_MAX_STRING_CHARS) {
            esc[n++] = '.';
            esc[n++] = '.';
            esc[n++] = '.';
        } else if (c == '"' || c == '\\') {
            esc[n++] = '\\';
            esc[n++] = (char)c;
        } else if (c >= 0x20u && c < 0x7Fu) {
            esc[n++] = (char)c;
        } else if (c == '\n' || c == '\r' || c == '\t') {
            esc[n++] = '\\';
            esc[n++] = c == '\n' ? 'n' : (c == '\r' ? 'r' : 't');
        } else if (c < 0x100u) {
            esc[n++] = '\\';
            esc[n++] = 'x';
            esc[n++] = hex[c >> 4];
            esc[n++] = hex[c & 15u];
        } else {
            esc[n++] = '\\';
            esc[n++] = 'u';
            esc[n++] = hex[(c >> 12) & 15u];
            esc[n++] = hex[(c >> 8) & 15u];
            esc[n++] = hex[(c >> 4) & 15u];
            esc[n++] = hex[c & 15u];
        }
        if (at + n + 2u > size) {
            return 0;
        }
        for (k = 0; k < n; ++k) {
            buffer[at++] = esc[k];
        }
        if (i == VB_MAX_STRING_CHARS) {
            break;
        }
    }
    buffer[at++] = '"';
    buffer[at] = '\0';
    return at;
}

/* A procedure descriptor referenced by a P-code call stub at code, or 0:
 * "mov edx, RTMI; mov ecx, ProcCallEngine; jmp ecx" (constant-pool entries
 * of procedures) and "xor eax, eax; mov edx, RTMI; push engine; ret"
 * (event-sink entries, after an optional "mov eax, imm32; cmp ax, ..."). */
static uint32_t vb_stub_target(const vb_file *f, uint32_t va)
{
    const uint8_t *p = xxpe_at_va(&f->pe, va, 12u);
    const uint8_t *q;

    if (p != NULL && p[0] == 0xBAu && p[5] == 0xB9u && p[10] == 0xFFu && p[11] == 0xE1u) {
        return xxbyte_u32le(p + 1);
    }
    q = p != NULL ? p : NULL;
    if (q != NULL && q[0] == 0xB8u && q[5] == 0x66u && q[6] == 0x3Du) {
        q = va <= 0xFFFFFFFFu - 7u ? xxpe_at_va(&f->pe, (uint64_t)va + 7u, 13u) : NULL;
    } else {
        q = xxpe_at_va(&f->pe, va, 13u);
    }
    if (q != NULL && q[0] == 0x33u && q[1] == 0xC0u && q[2] == 0xBAu && q[7] == 0x68u && q[12] == 0xC3u) {
        return xxbyte_u32le(q + 3);
    }
    return 0;
}

/* Instructions whose constant-pool operand is a GUID: the interface of a
 * vtable call (VCall*, ThisVCall*) or the class of New and NewIfNull*. */
static int vb_takes_guid(const xxbyte_instruction *instruction)
{
    const char *m = instruction->mnemonic;

    if (m != NULL && m[0] == 'N' && m[1] == 'e' && m[2] == 'w') {
        return 1;
    }
    for (; m != NULL && *m != '\0'; ++m) {
        if (m[0] == 'V' && m[1] == 'C' && m[2] == 'a' && m[3] == 'l' && m[4] == 'l') {
            return 1;
        }
    }
    return 0;
}

/* Instructions whose constant-pool operand is a string: literals (LitStr,
 * LitVarStr, ...) and late-bound member names (LateMem*, VarLateMem*). */
static int vb_takes_string(const xxbyte_instruction *instruction)
{
    const char *m = instruction->mnemonic;

    for (; m != NULL && *m != '\0'; ++m) {
        if ((m[0] == 'S' && m[1] == 't' && m[2] == 'r') || (m[0] == 'L' && m[1] == 'a' && m[2] == 't' && m[3] == 'e')) {
            return 1;
        }
    }
    return 0;
}

static size_t vb_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const vb_resolve_context *rc = (const vb_resolve_context *)context;
    const vb_file *f = rc->file;
    const xxbyte_operand *op = &instruction->operands[operand_index];
    uint32_t index;
    uint32_t entry;
    int ok = 1;
    const uint8_t *p;

    if (op->type != XXBYTE_OPERAND_INDEX || (op->kind != XXBYTE_INDEX_CONSTANT && op->kind != XXBYTE_INDEX_IMPORT) || op->value < 0 ||
        (uint64_t)op->value >= rc->object->pool_count) {
        return 0;
    }
    index = (uint32_t)op->value;
    entry = vb_u32(f, rc->object->pool_va + 4u * index, &ok);
    if (!ok) {
        return 0;
    }
    if (entry == 0) {
        /* A null BSTR, the empty string. */
        return vb_takes_string(instruction) && op->kind == XXBYTE_INDEX_CONSTANT ? vb_copy(buffer, buffer_size, 0, "\"\"") : 0;
    }
    /* jmp dword ptr [IAT slot]: an imported function. */
    p = xxpe_at_va(&f->pe, entry, 6u);
    if (p != NULL && p[0] == 0xFFu && p[1] == 0x25u) {
        uint32_t rva = xxpe_rva_of(&f->pe, xxbyte_u32le(p + 2), &ok);

        return ok ? vb_import_name(f, rva, buffer, buffer_size) : 0;
    }
    /* A Declare stub: mov eax, [cache]; or eax, eax; je +2; jmp eax;
     * push DeclareInfo; mov eax, DllFunctionCall; call eax; jmp eax. */
    p = xxpe_at_va(&f->pe, entry, 25u);
    if (p != NULL && p[0] == 0xA1u && p[5] == 0x0Bu && p[6] == 0xC0u && p[7] == 0x74u && p[8] == 0x02u && p[9] == 0xFFu && p[10] == 0xE0u && p[11] == 0x68u &&
        p[16] == 0xB8u && p[21] == 0xFFu && p[22] == 0xD0u) {
        uint32_t info = xxbyte_u32le(p + 12);
        const char *lib = vb_str(f, vb_u32(f, info, &ok));
        const char *fn = vb_str(f, vb_u32(f, info + 4u, &ok));

        if (ok && lib != NULL && fn != NULL) {
            size_t at = vb_copy(buffer, buffer_size, 0, lib);

            at = vb_copy(buffer, buffer_size, at, "!");
            return vb_copy(buffer, buffer_size, at, fn);
        }
        return 0;
    }
    {
        uint32_t desc = vb_stub_target(f, entry);
        const vb_proc *proc = desc != 0 ? vb_proc_at(f, desc) : NULL;

        if (proc != NULL) {
            return vb_proc_name(f, proc, buffer, buffer_size);
        }
    }
    if (vb_takes_guid(instruction)) {
        /* The interface (VCall*) or class (New*) GUID. */
        const uint8_t *g = xxpe_at_va(&f->pe, entry, 16u);
        static const char hex[] = "0123456789ABCDEF";
        static const int order[16] = {3, 2, 1, 0, -1, 5, 4, -1, 7, 6, -1, 8, 9, -1, 10, 11};
        char text[40];
        size_t n = 0;
        uint32_t i;

        if (g == NULL || buffer_size < 39u) {
            return 0;
        }
        text[n++] = '{';
        for (i = 0; i < 16u; ++i) {
            if (order[i] < 0) {
                text[n++] = '-';
                continue;
            }
            text[n++] = hex[g[order[i]] >> 4];
            text[n++] = hex[g[order[i]] & 15u];
        }
        for (i = 12; i < 16u; ++i) {
            text[n++] = hex[g[i] >> 4];
            text[n++] = hex[g[i] & 15u];
        }
        text[n++] = '}';
        text[n] = '\0';
        return vb_copy(buffer, buffer_size, 0, text);
    }
    if (!vb_takes_string(instruction)) {
        return 0;
    }
    /* A BSTR literal: a 32-bit byte length, the UTF-16 characters (which
     * may include NUL, as Chr$(0) does) and a terminating zero character. */
    if (entry >= 4u) {
        uint32_t length = vb_u32(f, entry - 4u, &ok);

        if (ok && (length & 1u) == 0 && length <= 0x10000u) {
            const uint8_t *chars = xxpe_at_va(&f->pe, entry, length + 2u);

            if (chars != NULL && xxbyte_u16le(chars + length) == 0) {
                return vb_quote(chars, length / 2u, buffer, buffer_size);
            }
        }
    }
    /* A late-bound member name: NUL-terminated UTF-16 without a length. */
    {
        uint32_t n;

        for (n = 0; n <= VB_MAX_NAME; ++n) {
            const uint8_t *c = (uint64_t)entry + 2u * n <= 0xFFFFFFFFu ? xxpe_at_va(&f->pe, (uint64_t)entry + 2u * n, 2u) : NULL;
            uint32_t ch = c != NULL ? xxbyte_u16le(c) : 0xFFFFu;

            if (ch == 0) {
                return n != 0 ? vb_quote(xxpe_at_va(&f->pe, entry, 2u * n), n, buffer, buffer_size) : 0;
            }
            if (ch < 0x20u || ch >= 0x7Fu) {
                return 0;
            }
        }
    }
    return 0;
}

/* Adds a procedure; returns 0 when the table is full, -1 when out of
 * memory. The hash table has twice as many slots as procedures. */
static int vb_add_proc(vb_file *f, const vb_proc *proc)
{
    uint32_t h;
    uint32_t i;

    if (f->proc_count == VB_MAX_PROCS) {
        return 0;
    }
    if (f->procs == NULL || f->proc_count == (f->hash_mask + 1u) / 2u) {
        uint32_t capacity = f->procs == NULL ? 64u : f->proc_count * 2u;
        vb_proc *procs = (vb_proc *)xx_mem_alloc(sizeof(vb_proc) * capacity);
        uint32_t *hash = (uint32_t *)xx_mem_alloc(sizeof(uint32_t) * 2u * capacity);

        if (procs == NULL || hash == NULL) {
            xx_mem_free(procs);
            xx_mem_free(hash);
            return -1;
        }
        if (f->procs != NULL) {
            memcpy(procs, f->procs, sizeof(vb_proc) * f->proc_count);
        }
        xx_mem_free(f->procs);
        xx_mem_free(f->hash);
        f->procs = procs;
        f->hash = hash;
        f->hash_mask = 2u * capacity - 1u;
        memset(hash, 0, sizeof(uint32_t) * 2u * capacity);
        for (i = 0; i < f->proc_count; ++i) {
            for (h = vb_hash(procs[i].desc_va) & f->hash_mask; hash[h] != 0; h = (h + 1u) & f->hash_mask) {
            }
            hash[h] = i + 1u;
        }
    }
    f->procs[f->proc_count++] = *proc;
    for (h = vb_hash(proc->desc_va) & f->hash_mask; f->hash[h] != 0; h = (h + 1u) & f->hash_mask) {
    }
    f->hash[h] = f->proc_count;
    return 1;
}

/* Names the event handlers of a form-like object from its control list. */
static void vb_name_events(vb_file *f, const vb_object *o, uint32_t object)
{
    uint32_t optional = o->info_va + VB_OBJECT_INFO_SIZE;
    uint32_t controls;
    uint32_t list;
    uint32_t k;
    int ok = 1;

    if ((o->type & VB_HAS_OPTIONAL_INFO) == 0 || !vb_array_ok(f, optional, 1, VB_OPTIONAL_INFO_SIZE)) {
        return;
    }
    controls = vb_u32(f, optional + 0x20u, &ok);
    list = vb_u32(f, optional + 0x24u, &ok);
    if (!ok || controls > VB_MAX_CONTROLS || !vb_array_ok(f, list, controls, VB_CONTROL_SIZE)) {
        return;
    }
    for (k = 0; k < controls; ++k) {
        uint32_t c = list + VB_CONTROL_SIZE * k;
        uint32_t events = vb_u16(f, c + 2u, &ok);
        uint32_t table = vb_u32(f, c + 0x18u, &ok);
        const char *control = vb_str(f, vb_u32(f, c + 0x20u, &ok));
        const xxbyte_vb_control_class *cls = NULL;
        const uint8_t *guid = xxpe_at_va(&f->pe, vb_u32(f, c + 8u, &ok), 16u);
        uint32_t e;

        if (!ok || control == NULL || !vb_array_ok(f, table, 1, VB_EVENT_TABLE_HEADER) || !vb_array_ok(f, table + VB_EVENT_TABLE_HEADER, events, 4u)) {
            ok = 1;
            continue;
        }
        if (guid != NULL && memcmp(guid + 4, "\x99\x66\xCF\x11\xB7\x0C\x00\xAA\x00\x60\xD3\x93", 12) == 0) {
            uint32_t d = xxbyte_u32le(guid) - VB_GUID_BASE;

            /* Each class owns eight GUIDs; its coclass is the third. */
            if (d % 8u == 2u && d / 8u < XXBYTE_VB_CONTROL_CLASS_COUNT && xxbyte_vb_control_classes[d / 8u].event_count == events && events != 0) {
                cls = &xxbyte_vb_control_classes[d / 8u];
            }
        }
        if (events > f->event_budget) {
            return;
        }
        f->event_budget -= events;
        for (e = 0; e < events; ++e) {
            uint32_t stub = vb_u32(f, table + VB_EVENT_TABLE_HEADER + 4u * e, &ok);
            uint32_t desc = stub != 0 && ok ? vb_stub_target(f, stub) : 0;
            vb_proc *p = desc != 0 ? vb_proc_at(f, desc) : NULL;

            ok = 1;
            if (p != NULL && p->object == object && p->control == NULL) {
                p->control = control;
                p->slot = e;
                p->event = cls != NULL && cls->events[e] != 0xFFu ? xxbyte_vb_event_names[cls->events[e]] : NULL;
            }
        }
    }
}

/* Reads the object table and collects the P-code procedures. */
static xxbyte_status vb_read_objects(vb_file *f)
{
    int ok = 1;
    uint32_t project = vb_u32(f, f->header_va + 0x30u, &ok);
    uint32_t table = vb_u32(f, project + 4u, &ok);
    uint32_t array = vb_u32(f, table + 0x30u, &ok);
    uint32_t count = vb_u16(f, table + 0x2Au, &ok);
    uint32_t i;

    if (!ok) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (!vb_array_ok(f, array, count, VB_PUBLIC_OBJECT_SIZE)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    f->objects = (vb_object *)xx_mem_alloc(sizeof(vb_object) * (count != 0 ? count : 1u));
    if (f->objects == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    memset(f->objects, 0, sizeof(vb_object) * (count != 0 ? count : 1u));
    f->object_count = count;
    for (i = 0; i < count; ++i) {
        vb_object *o = &f->objects[i];
        uint32_t pod = array + VB_PUBLIC_OBJECT_SIZE * i;
        uint32_t names;
        uint32_t name_count;
        uint32_t j;

        ok = 1;
        o->va = pod;
        o->info_va = vb_u32(f, pod, &ok);
        o->name = vb_str(f, vb_u32(f, pod + 0x18u, &ok));
        name_count = vb_u32(f, pod + 0x1Cu, &ok);
        names = vb_u32(f, pod + 0x20u, &ok);
        o->type = vb_u32(f, pod + 0x28u, &ok);
        if (!vb_array_ok(f, o->info_va, 1, VB_OBJECT_INFO_SIZE)) {
            continue;
        }
        o->method_count = vb_u16(f, o->info_va + 0x20u, &ok);
        o->methods_va = vb_u32(f, o->info_va + 0x24u, &ok);
        o->pool_count = vb_u16(f, o->info_va + 0x28u, &ok);
        o->pool_va = vb_u32(f, o->info_va + 0x34u, &ok);
        if (!vb_array_ok(f, o->pool_va, o->pool_count, 4u)) {
            o->pool_count = 0;
        }
        if (!vb_array_ok(f, o->methods_va, o->method_count, 4u)) {
            o->method_count = 0;
        }
        if (names == 0 || !vb_array_ok(f, names, name_count, 4u)) {
            name_count = 0;
        }
        for (j = 0; j < o->method_count; ++j) {
            uint32_t desc = vb_u32(f, o->methods_va + 4u * j, &ok);
            vb_proc proc;
            int added;

            /* A P-code descriptor points back at its ObjectInfo. */
            if (desc == 0 || vb_u32(f, desc, &ok) != o->info_va || !vb_array_ok(f, desc, 1, XXBYTE_VB_PROC_DESC_SIZE) || vb_proc_at(f, desc) != NULL) {
                ok = 1;
                continue;
            }
            memset(&proc, 0, sizeof(proc));
            proc.desc_va = desc;
            proc.object = i;
            proc.method = j;
            if (j < name_count) {
                proc.public_name = vb_str(f, vb_u32(f, names + 4u * j, &ok));
                ok = 1;
            }
            added = vb_add_proc(f, &proc);
            if (added < 0) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            if (added == 0) {
                break;
            }
        }
        vb_name_events(f, o, i);
    }
    return XXBYTE_STATUS_OK;
}

static int vb_puts(xx_buf_t *out, const char *text)
{
    for (; *text != '\0'; ++text) {
        char c = *text;
        if (!xx_buf_append_char(out, (c >= 0x20 && c < 0x7F) ? c : '?')) {
            return 0;
        }
    }
    return 1;
}

static xxbyte_status vb_list_proc(vb_file *f, const vb_proc *p, uint32_t listing_flags, xx_buf_t *out)
{
    const vb_object *o = &f->objects[p->object];
    const uint8_t *desc = xxpe_at_va(&f->pe, p->desc_va, XXBYTE_VB_PROC_DESC_SIZE);
    xxbyte_vb_proc_desc d;
    const uint8_t *code;
    size_t code_size = 0;
    char name[512];
    vb_resolve_context rc;
    xxbyte_status status;

    if (desc == NULL || xxbyte_vb_parse_proc_desc(desc, XXBYTE_VB_PROC_DESC_SIZE, &d) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    vb_proc_name(f, p, name, sizeof(name));
    if (!xx_buf_append_str(out, "; procedure ") || !vb_puts(out, name)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    code = d.proc_size != 0 && p->desc_va >= d.proc_size ? xxpe_at_va(&f->pe, (uint64_t)p->desc_va - d.proc_size, d.proc_size) : NULL;
    if (code == NULL) {
        return xx_buf_appendf(out, ": RTMI 0x%08X, proc_size 0x%X is not in the file\n\n", (unsigned)p->desc_va, (unsigned)d.proc_size) ? XXBYTE_STATUS_OK
                                                                                                                                        : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_vb_proc_code_size(f->mode, code, d.proc_size, &code_size);
    if (status != XXBYTE_STATUS_OK) {
        code_size = d.proc_size;
    }
    if (!xx_buf_appendf(out, ": RTMI 0x%08X, code 0x%08X, 0x%X bytes", (unsigned)p->desc_va, (unsigned)(p->desc_va - d.proc_size), (unsigned)code_size) ||
        (code_size != d.proc_size && !xx_buf_appendf(out, " + 0x%X filler", (unsigned)(d.proc_size - code_size))) ||
        !xx_buf_appendf(out, ", frame 0x%X\n", (unsigned)d.frame_size)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (code_size > f->budget) {
        return xx_buf_append_str(out, "; not listed: the listing limit for this image is reached\n\n") ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    f->budget -= code_size;
    rc.file = f;
    rc.object = o;
    status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_VB, f->mode, NULL, code, code_size, 0, listing_flags, vb_resolve, &rc, out);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    return xx_buf_append_char(out, '\n') ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

static xxbyte_status vb_list(vb_file *f, uint32_t mode, uint32_t listing_flags, xx_buf_t *out)
{
    int ok = 1;
    uint32_t project = vb_u32(f, f->header_va + 0x30u, &ok);
    uint32_t native = vb_u32(f, project + 0x20u, &ok);
    uint32_t table = vb_u32(f, project + 4u, &ok);
    const char *project_name = vb_str(f, vb_u32(f, table + 0x40u, &ok));
    xxbyte_status status;
    uint32_t i;
    uint32_t next;

    vb_each_import(f, vb_note_runtime, f);
    if (mode == XXBYTE_MODE_AUTO) {
        mode = f->runtime50 && !f->runtime60 ? XXBYTE_VB_MODE_VB5 : XXBYTE_VB_MODE_VB6;
    }
    f->mode = mode;
    status = vb_read_objects(f);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (!xx_buf_appendf(out, "; Visual Basic %s %s project \"", mode == XXBYTE_VB_MODE_VB5 ? "5" : "6", native != 0 ? "native-code" : "P-code") ||
        !vb_puts(out, project_name != NULL ? project_name : "") ||
        !xx_buf_appendf(out, "\": VB header 0x%08X, %u objects\n", (unsigned)f->header_va, (unsigned)f->object_count)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (native != 0) {
        for (i = 0; i < f->object_count; ++i) {
            const vb_object *o = &f->objects[i];

            if (!xx_buf_append_str(out, "; object \"") || !vb_puts(out, o->name != NULL ? o->name : "") ||
                !xx_buf_appendf(out, "\": type 0x%X, ObjectInfo 0x%08X, %u methods\n", (unsigned)o->type, (unsigned)o->info_va, (unsigned)o->method_count)) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
        return xx_buf_append_str(out, "; native code: no P-code procedures\n") ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    /* Procedures were collected in object order. */
    for (i = 0, next = 0; i < f->object_count; ++i) {
        const vb_object *o = &f->objects[i];
        uint32_t k;
        uint32_t end = next;

        while (end < f->proc_count && f->procs[end].object == i) {
            ++end;
        }
        if (!xx_buf_append_str(out, "; object \"") || !vb_puts(out, o->name != NULL ? o->name : "") ||
            !xx_buf_appendf(out, "\": type 0x%X, ObjectInfo 0x%08X, %u methods (%u P-code), %u constants\n\n", (unsigned)o->type, (unsigned)o->info_va,
                            (unsigned)o->method_count, (unsigned)(end - next), (unsigned)o->pool_count)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        for (k = next; k < end; ++k) {
            status = vb_list_proc(f, &f->procs[k], listing_flags, out);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
        }
        next = end;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_vb_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    vb_file *f;
    xxbyte_status status;

    f = (vb_file *)xx_mem_alloc(sizeof(vb_file));
    if (f == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    memset(f, 0, sizeof(*f));
    f->event_budget = size / 4u + 0x10000u;
    f->budget = size < ((size_t)-1 - 0x100000u) / 4u ? 4u * size + 0x100000u : (size_t)-1;
    status = xxpe_parse(&f->pe, data, size);
    if (status == XXBYTE_STATUS_OK && (f->pe.machine != 0x014Cu || f->pe.magic != 0x10Bu)) {
        status = XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (status == XXBYTE_STATUS_OK) {
        f->header_va = vb_find_header(f);
        status = f->header_va != 0 ? vb_list(f, mode, listing_flags, out) : XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    xx_mem_free(f->objects);
    xx_mem_free(f->procs);
    xx_mem_free(f->hash);
    xx_mem_free(f);
    return status;
}

#else
typedef int xxbyte_vb_file_disabled;
#endif
