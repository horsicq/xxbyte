#include "xxbyte_ebc_file.h"
#include "xxbyte_vb_pe.h"

#if XXBYTE_USE_EBC

#include "xxbyte/xxbyte_ebc.h"
#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

/*
 * The container listing of an EFI Byte Code PE/COFF image (Machine 0x0EBC,
 * see xxbyte_ebc.h): headers, sections, exports and base relocations, then
 * a linear sweep of each code section with labels at the entry point, the
 * exports and every direct CALL target, which the resolver also prints in
 * place of BRANCH and DATA addresses.
 */

#define EBC_MACHINE 0x0EBCu
#define EBC_MAX_EXPORTS 0x10000u
#define EBC_MAX_RELOC_ENTRIES 0x100000u
#define EBC_MAX_NAME 255u

enum {
    EBC_LABEL_CALL = 0,
    EBC_LABEL_EXPORT = 1,
    EBC_LABEL_ENTRY = 2
};

typedef struct ebc_label {
    uint64_t va;
    const char *name; /* an export's name, NULL for generated names */
    uint32_t kind;
    uint32_t ordinal; /* export ordinal */
} ebc_label;

typedef struct ebc_file {
    xxpe_image pe;
    ebc_label *labels;
    size_t label_count;
    size_t label_capacity;
} ebc_file;

static int ebc_add_label(ebc_file *f, uint64_t va, const char *name, uint32_t kind, uint32_t ordinal)
{
    if (f->label_count == f->label_capacity) {
        size_t grown = f->label_capacity == 0 ? 64u : f->label_capacity * 2u;
        ebc_label *labels = (ebc_label *)xx_mem_alloc(sizeof(ebc_label) * grown);

        if (labels == NULL) {
            return 0;
        }
        if (f->labels != NULL) {
            memcpy(labels, f->labels, sizeof(ebc_label) * f->label_count);
            xx_mem_free(f->labels);
        }
        f->labels = labels;
        f->label_capacity = grown;
    }
    f->labels[f->label_count].va = va;
    f->labels[f->label_count].name = name;
    f->labels[f->label_count].kind = kind;
    f->labels[f->label_count].ordinal = ordinal;
    ++f->label_count;
    return 1;
}

/* Sorts by address, the strongest kind first, and keeps one label per
 * address. Heap sort: the count is unbounded on hostile input. */
static int ebc_label_less(const ebc_label *a, const ebc_label *b)
{
    if (a->va != b->va) {
        return a->va < b->va;
    }
    if (a->kind != b->kind) {
        return a->kind > b->kind;
    }
    return a->ordinal < b->ordinal;
}

static void ebc_sift(ebc_label *v, size_t root, size_t n)
{
    for (;;) {
        size_t child = 2u * root + 1u;
        ebc_label t;

        if (child >= n) {
            return;
        }
        if (child + 1u < n && ebc_label_less(&v[child], &v[child + 1u])) {
            ++child;
        }
        if (!ebc_label_less(&v[root], &v[child])) {
            return;
        }
        t = v[root];
        v[root] = v[child];
        v[child] = t;
        root = child;
    }
}

static void ebc_sort_labels(ebc_file *f)
{
    ebc_label *v = f->labels;
    size_t n = f->label_count;
    size_t i;
    size_t out = 0;

    for (i = n / 2u; i-- > 0;) {
        ebc_sift(v, i, n);
    }
    for (i = n; i > 1u; --i) {
        ebc_label t = v[0];
        v[0] = v[i - 1u];
        v[i - 1u] = t;
        ebc_sift(v, 0, i - 1u);
    }
    for (i = 0; i < n; ++i) {
        if (out == 0 || v[out - 1u].va != v[i].va) {
            v[out++] = v[i];
        }
    }
    f->label_count = out;
}

static const ebc_label *ebc_label_at(const ebc_file *f, uint64_t va)
{
    size_t lo = 0;
    size_t hi = f->label_count;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;

        if (f->labels[mid].va < va) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return lo < f->label_count && f->labels[lo].va == va ? &f->labels[lo] : NULL;
}

/* Appends a name with every byte outside printable ASCII shown as '?'. */
static int ebc_put_name(xx_buf_t *out, const char *name)
{
    for (; *name != '\0'; ++name) {
        char c = *name;

        if (!xx_buf_append_char(out, (c > 0x20 && c < 0x7F) ? c : '?')) {
            return 0;
        }
    }
    return 1;
}

static int ebc_put_label(xx_buf_t *out, const ebc_label *label)
{
    if (label->name != NULL) {
        return ebc_put_name(out, label->name);
    }
    switch (label->kind) {
        case EBC_LABEL_ENTRY: return xx_buf_append_str(out, "entry");
        case EBC_LABEL_EXPORT: return xx_buf_appendf(out, "ordinal_%u", (unsigned)label->ordinal);
        default: return xx_buf_appendf(out, "sub_%llX", (unsigned long long)label->va);
    }
}

static size_t ebc_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const ebc_file *f = (const ebc_file *)context;
    const xxbyte_operand *op = &instruction->operands[operand_index];
    const ebc_label *label;
    xx_buf_t text;
    size_t length = 0;

    if (op->type != XXBYTE_OPERAND_BRANCH && op->type != XXBYTE_OPERAND_DATA) {
        return 0;
    }
    label = ebc_label_at(f, (uint64_t)op->value);
    if (label == NULL || buffer_size == 0) {
        return 0;
    }
    xx_buf_init(&text);
    if (ebc_put_label(&text, label) && xx_buf_ok(&text) && text.data != NULL) {
        length = text.size < buffer_size - 1u ? text.size : buffer_size - 1u;
        memcpy(buffer, text.data, length);
        buffer[length] = '\0';
    }
    xx_buf_free(&text);
    return length;
}

/* The listed extent of a code section: its raw data, without the file
 * alignment padding past the virtual size. */
static uint32_t ebc_code_size(const xxpe_section *s)
{
    return s->virtual_size != 0 && s->virtual_size < s->raw_size ? s->virtual_size : s->raw_size;
}

static int ebc_is_code(const xxpe_section *s)
{
    return (s->characteristics & (XXPE_SCN_CNT_CODE | XXPE_SCN_MEM_EXECUTE)) != 0;
}

/* How many of count entries of width bytes at rva are in the file, at most
 * EBC_MAX_EXPORTS: the largest prefix xxpe_at can read in one piece. */
static uint32_t ebc_entries(const xxpe_image *pe, uint32_t rva, uint32_t width, uint32_t count)
{
    uint32_t lo = 0;
    uint32_t hi = count > EBC_MAX_EXPORTS ? EBC_MAX_EXPORTS : count;

    while (lo < hi) {
        uint32_t mid = hi - (hi - lo) / 2u;

        if (xxpe_at(pe, rva, mid * width) != NULL) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    return lo;
}

static xxbyte_status ebc_read_exports(ebc_file *f, xx_buf_t *out)
{
    const xxpe_image *pe = &f->pe;
    const uint8_t *dir;
    uint32_t base;
    uint32_t functions;
    uint32_t names;
    uint32_t address_rva;
    uint32_t names_rva;
    uint32_t ordinals_rva;
    const char *module;
    uint32_t *name_of;
    const uint8_t *address_table;
    const uint8_t *name_table;
    const uint8_t *ordinal_table;
    uint32_t i;

    if (pe->dir_count <= XXPE_DIR_EXPORT || pe->dir_rva[XXPE_DIR_EXPORT] == 0) {
        return XXBYTE_STATUS_OK;
    }
    dir = xxpe_at(pe, pe->dir_rva[XXPE_DIR_EXPORT], 40u);
    if (dir == NULL) {
        return xx_buf_append_str(out, "; export directory is not in the file\n") ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    module = xxpe_str(pe, xxbyte_u32le(dir + 12), EBC_MAX_NAME);
    base = xxbyte_u32le(dir + 16);
    functions = xxbyte_u32le(dir + 20);
    names = xxbyte_u32le(dir + 24);
    address_rva = xxbyte_u32le(dir + 28);
    names_rva = xxbyte_u32le(dir + 32);
    ordinals_rva = xxbyte_u32le(dir + 36);
    /* Tables that run past their section are clipped to the entries in
     * the file. */
    functions = ebc_entries(pe, address_rva, 4u, functions);
    names = ebc_entries(pe, names_rva, 4u, names);
    names = ebc_entries(pe, ordinals_rva, 2u, names);
    /* Each table is read through one pointer: a later entry can lie in
     * another section when sections overlap. */
    address_table = xxpe_at(pe, address_rva, 4u * functions);
    name_table = xxpe_at(pe, names_rva, 4u * names);
    ordinal_table = xxpe_at(pe, ordinals_rva, 2u * names);
    if (address_table == NULL) {
        functions = 0;
    }
    if (name_table == NULL || ordinal_table == NULL) {
        names = 0;
    }
    if (!xx_buf_append_str(out, "; exports of \"") || !ebc_put_name(out, module != NULL ? module : "") ||
        !xx_buf_appendf(out, "\": %u functions, %u names, ordinal base %u\n", (unsigned)functions, (unsigned)names, (unsigned)base)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    name_of = (uint32_t *)xx_mem_alloc(sizeof(uint32_t) * (functions != 0 ? functions : 1u));
    if (name_of == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    memset(name_of, 0, sizeof(uint32_t) * (functions != 0 ? functions : 1u));
    /* The first name of each function: name index + 1. */
    for (i = names; i-- > 0;) {
        uint32_t ordinal = xxbyte_u16le(ordinal_table + 2u * i);

        if (ordinal < functions) {
            name_of[ordinal] = i + 1u;
        }
    }
    for (i = 0; i < functions; ++i) {
        uint32_t rva = xxbyte_u32le(address_table + 4u * i);
        const char *name = NULL;

        if (rva == 0) {
            continue;
        }
        if (name_of[i] != 0) {
            name = xxpe_str(pe, xxbyte_u32le(name_table + 4u * (name_of[i] - 1u)), EBC_MAX_NAME);
        }
        if (!xx_buf_appendf(out, ";   ordinal %u ", (unsigned)(base + i)) ||
            (name != NULL && (!xx_buf_append_char(out, '"') || !ebc_put_name(out, name) || !xx_buf_append_str(out, "\" ")))) {
            xx_mem_free(name_of);
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        /* An address inside the export directory is a forwarder string. */
        if (rva - pe->dir_rva[XXPE_DIR_EXPORT] < pe->dir_size[XXPE_DIR_EXPORT]) {
            const char *forward = xxpe_str(pe, rva, EBC_MAX_NAME);

            if (!xx_buf_append_str(out, "forwarded to \"") || !ebc_put_name(out, forward != NULL ? forward : "") || !xx_buf_append_str(out, "\"\n")) {
                xx_mem_free(name_of);
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            continue;
        }
        if (!xx_buf_appendf(out, "RVA 0x%X\n", (unsigned)rva) || !ebc_add_label(f, pe->image_base + rva, name, EBC_LABEL_EXPORT, base + i)) {
            xx_mem_free(name_of);
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    xx_mem_free(name_of);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status ebc_read_relocations(const ebc_file *f, xx_buf_t *out)
{
    const xxpe_image *pe = &f->pe;
    uint32_t rva;
    uint32_t end;
    uint32_t blocks = 0;
    uint32_t counts[16];
    uint32_t total = 0;
    uint32_t i;
    static const char *const names[16] = {"ABSOLUTE", "HIGH", "LOW", "HIGHLOW", "HIGHADJ", "5", "6", "7", "8", "9", "DIR64", "11", "12", "13", "14", "15"};

    if (pe->dir_count <= XXPE_DIR_BASERELOC || pe->dir_rva[XXPE_DIR_BASERELOC] == 0 || pe->dir_size[XXPE_DIR_BASERELOC] == 0) {
        return XXBYTE_STATUS_OK;
    }
    memset(counts, 0, sizeof(counts));
    rva = pe->dir_rva[XXPE_DIR_BASERELOC];
    end = pe->dir_size[XXPE_DIR_BASERELOC] > 0xFFFFFFFFu - rva ? 0xFFFFFFFFu : rva + pe->dir_size[XXPE_DIR_BASERELOC];
    while (end - rva >= 8u && total < EBC_MAX_RELOC_ENTRIES) {
        const uint8_t *block = xxpe_at(pe, rva, 8u);
        uint32_t size;
        const uint8_t *entries;

        if (block == NULL) {
            break;
        }
        size = xxbyte_u32le(block + 4);
        if (size < 8u || size > end - rva || (entries = xxpe_at(pe, rva, size)) == NULL) {
            break;
        }
        ++blocks;
        for (i = 8; i + 2u <= size && total < EBC_MAX_RELOC_ENTRIES; i += 2u) {
            ++counts[xxbyte_u16le(entries + i) >> 12];
            ++total;
        }
        rva += size;
    }
    if (!xx_buf_appendf(out, "; base relocations: %u entries in %u blocks", (unsigned)total, (unsigned)blocks)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < 16u; ++i) {
        if (counts[i] != 0 && !xx_buf_appendf(out, ", %s %u", names[i], (unsigned)counts[i])) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    return xx_buf_append_char(out, '\n') ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

/* Walks a code section the way xxbyte_disasm_buf does and records each
 * instruction start in starts (one bit per byte) and each direct CALL
 * target inside the image as a label. */
static xxbyte_status ebc_scan(ebc_file *f, const uint8_t *code, uint32_t size, uint64_t va, uint8_t *starts)
{
    uint32_t offset = 0;

    while (offset < size) {
        xxbyte_instruction insn;

        starts[offset >> 3] = (uint8_t)(starts[offset >> 3] | (1u << (offset & 7u)));
        if (xxbyte_decode(XXBYTE_FAMILY_EBC, XXBYTE_EBC_MODE_DEFAULT, code + offset, size - offset, va + offset, &insn) != XXBYTE_STATUS_OK) {
            ++offset;
            continue;
        }
        if ((insn.group & XXBYTE_GROUP_CALL) != 0 && insn.operand_count != 0 && insn.operands[0].type == XXBYTE_OPERAND_BRANCH) {
            uint64_t target = (uint64_t)insn.operands[0].value;

            if (target >= f->pe.image_base && target - f->pe.image_base < f->pe.size_of_image && !ebc_add_label(f, target, NULL, EBC_LABEL_CALL, 0)) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
        offset += insn.size;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status ebc_list_section(const ebc_file *f, const xxpe_section *s, const uint8_t *starts, uint32_t listing_flags, xx_buf_t *out)
{
    const uint8_t *code = f->pe.data + s->raw_offset;
    uint32_t size = ebc_code_size(s);
    uint64_t va = f->pe.image_base + s->rva;
    uint32_t from = 0;
    size_t li = 0;

    /* The first label at or after the section start. */
    {
        size_t lo = 0;
        size_t hi = f->label_count;

        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2u;

            if (f->labels[mid].va < va) {
                lo = mid + 1u;
            } else {
                hi = mid;
            }
        }
        li = lo;
    }
    while (from < size) {
        uint32_t to = size;
        xxbyte_status status;

        /* Labels at instruction starts split the sweep; one inside an
         * instruction is only named by the resolver. */
        while (li < f->label_count && f->labels[li].va - va < size) {
            uint32_t at = (uint32_t)(f->labels[li].va - va);

            if (at < from || (starts[at >> 3] & (1u << (at & 7u))) == 0) {
                ++li;
                continue;
            }
            if (at > from) {
                to = at;
                break;
            }
            if (!ebc_put_label(out, &f->labels[li]) || !xx_buf_append_str(out, ":\n")) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            ++li;
        }
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_EBC, XXBYTE_EBC_MODE_DEFAULT, NULL, code + from, to - from, va + from, listing_flags, ebc_resolve, (void *)f, out);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        from = to;
    }
    return XXBYTE_STATUS_OK;
}

static const char *ebc_subsystem(uint16_t subsystem)
{
    switch (subsystem) {
        case 10: return "EFI application";
        case 11: return "EFI boot service driver";
        case 12: return "EFI runtime driver";
        case 13: return "EFI ROM";
        default: return "not an EFI subsystem";
    }
}

static xxbyte_status ebc_list(ebc_file *f, uint32_t listing_flags, xx_buf_t *out)
{
    const xxpe_image *pe = &f->pe;
    xxbyte_status status;
    uint8_t *starts[XXPE_MAX_SECTIONS];
    uint32_t i;

    if (!xx_buf_appendf(out, "; EFI Byte Code image: %s, image base 0x%llX, entry RVA 0x%X, subsystem %u (%s), %u sections\n", pe->magic == 0x20Bu ? "PE32+" : "PE32",
                        (unsigned long long)pe->image_base, (unsigned)pe->entry_rva, (unsigned)pe->subsystem, ebc_subsystem(pe->subsystem),
                        (unsigned)pe->section_count)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (pe->entry_rva != 0 && !ebc_add_label(f, pe->image_base + pe->entry_rva, NULL, EBC_LABEL_ENTRY, 0)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = ebc_read_exports(f, out);
    if (status == XXBYTE_STATUS_OK) {
        status = ebc_read_relocations(f, out);
    }
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    memset(starts, 0, sizeof(starts));
    for (i = 0; i < pe->section_count && status == XXBYTE_STATUS_OK; ++i) {
        const xxpe_section *s = &pe->sections[i];
        uint32_t size = ebc_code_size(s);

        if (!ebc_is_code(s) || size == 0) {
            continue;
        }
        starts[i] = (uint8_t *)xx_mem_alloc(size / 8u + 1u);
        if (starts[i] == NULL) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        memset(starts[i], 0, size / 8u + 1u);
        status = ebc_scan(f, pe->data + s->raw_offset, size, pe->image_base + s->rva, starts[i]);
    }
    if (status == XXBYTE_STATUS_OK) {
        ebc_sort_labels(f);
    }
    for (i = 0; i < pe->section_count && status == XXBYTE_STATUS_OK; ++i) {
        const xxpe_section *s = &pe->sections[i];
        int code = ebc_is_code(s);

        if (!xx_buf_append_str(out, "\n; section \"") || !ebc_put_name(out, s->name) ||
            !xx_buf_appendf(out, "\": RVA 0x%X, virtual size 0x%X, file 0x%X+0x%X, characteristics 0x%08X%s\n", (unsigned)s->rva, (unsigned)s->virtual_size,
                            (unsigned)s->raw_offset, (unsigned)s->raw_size, (unsigned)s->characteristics,
                            !code ? ", data: not listed" : (starts[i] == NULL ? ", code: no bytes in the file" : ", code"))) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        if (code && starts[i] != NULL) {
            status = ebc_list_section(f, s, starts[i], listing_flags, out);
        }
    }
    for (i = 0; i < XXPE_MAX_SECTIONS; ++i) {
        xx_mem_free(starts[i]);
    }
    return status;
}

xxbyte_status xxbyte_ebc_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    ebc_file *f;
    xxbyte_status status;

    (void)mode; /* one encoding */
    f = (ebc_file *)xx_mem_alloc(sizeof(ebc_file));
    if (f == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    memset(f, 0, sizeof(*f));
    status = xxpe_parse(&f->pe, data, size);
    if (status == XXBYTE_STATUS_OK && f->pe.machine != EBC_MACHINE) {
        status = XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (status == XXBYTE_STATUS_OK) {
        status = ebc_list(f, listing_flags, out);
    }
    xx_mem_free(f->labels);
    xx_mem_free(f);
    return status;
}

#else
typedef int xxbyte_ebc_file_disabled;
#endif
