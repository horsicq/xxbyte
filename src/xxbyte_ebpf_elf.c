/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_ebpf_internal.h"

#if XXBYTE_USE_EBPF

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#include <stdlib.h>

/*
 * ELF containers of eBPF code: Linux BPF objects (clang -target bpf) and
 * Solana programs (sBPF shared objects). Every read is bounds-checked
 * against the file; malformed parts are skipped.
 */

#define ELF_EM_BPF 247u
#define ELF_EM_SBPF 263u

#define ELF_ET_REL 1u
#define ELF_ET_DYN 3u

#define ELF_SHT_PROGBITS 1u
#define ELF_SHT_SYMTAB 2u
#define ELF_SHT_STRTAB 3u
#define ELF_SHT_RELA 4u
#define ELF_SHT_DYNAMIC 6u
#define ELF_SHT_NOBITS 8u
#define ELF_SHT_REL 9u
#define ELF_SHT_DYNSYM 11u

#define ELF_SHF_EXECINSTR 0x4u

#define ELF_PT_LOAD 1u
#define ELF_PT_DYNAMIC 2u
#define ELF_PF_R 4u

#define ELF_STT_FUNC 2u
#define ELF_STT_SECTION 3u
#define ELF_STT_FILE 4u

#define ELF_DT_SYMTAB 6
#define ELF_DT_REL 17
#define ELF_DT_RELSZ 18
#define ELF_DT_RELENT 19

#define ELF_SHN_XINDEX 0xFFFFu

/* BPF relocation types (the sBPF loader reads the same numbers). */
#define R_BPF_64_64 1u
#define R_BPF_64_RELATIVE 8u
#define R_BPF_64_32 10u

#define SLOT XXBYTE_EBPF_SLOT_SIZE

typedef struct elf_file {
    const uint8_t *data;
    size_t size;
    int is64;
    int be;
    uint32_t type;
    uint32_t machine;
    uint32_t flags;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t phentsize;
    uint32_t phnum;
    uint32_t shentsize;
    uint32_t shnum;
    uint32_t shstrndx;
} elf_file;

typedef struct elf_shdr {
    uint32_t name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
} elf_shdr;

typedef struct elf_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t filesz;
    uint64_t memsz;
} elf_phdr;

typedef struct elf_sym {
    uint32_t name;
    uint32_t info;
    uint32_t shndx;
    uint64_t value;
} elf_sym;

/* A relocation of the listed code: offset in the code, symbol index. */
typedef struct elf_reloc {
    uint64_t offset;
    uint32_t type;
    uint32_t sym;
    int64_t addend;
} elf_reloc;

/* Reading. */

static int elf_has(const elf_file *e, uint64_t offset, uint64_t length)
{
    return offset <= e->size && length <= e->size - offset;
}

static uint64_t elf_get(const elf_file *e, uint64_t offset, unsigned bytes)
{
    const uint8_t *p = e->data + offset;
    uint64_t value = 0;
    unsigned i;

    for (i = 0; i < bytes; ++i) {
        unsigned shift = e->be ? 8u * (bytes - 1u - i) : 8u * i;
        value |= (uint64_t)p[i] << shift;
    }
    return value;
}

/* Reads a word of the file's class: 4 bytes for ELF32, 8 for ELF64. */
static uint64_t elf_word(const elf_file *e, uint64_t offset)
{
    return elf_get(e, offset, e->is64 ? 8u : 4u);
}

static xxbyte_status elf_open(const uint8_t *data, size_t size, elf_file *e)
{
    memset(e, 0, sizeof(*e));
    e->data = data;
    e->size = size;
    if (size < 4 || data[0] != 0x7F || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') {
        return size < 4 && (size == 0 || memcmp(data,
                                                "\x7F"
                                                "ELF",
                                                size) == 0)
                   ? XXBYTE_STATUS_TRUNCATED
                   : XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (size < 6) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if ((data[4] != 1 && data[4] != 2) || (data[5] != 1 && data[5] != 2)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    e->is64 = data[4] == 2;
    e->be = data[5] == 2;
    if (size < (e->is64 ? 64u : 52u)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    e->type = (uint32_t)elf_get(e, 16, 2);
    e->machine = (uint32_t)elf_get(e, 18, 2);
    if (e->machine != ELF_EM_BPF && e->machine != ELF_EM_SBPF) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (e->is64) {
        e->entry = elf_get(e, 24, 8);
        e->phoff = elf_get(e, 32, 8);
        e->shoff = elf_get(e, 40, 8);
        e->flags = (uint32_t)elf_get(e, 48, 4);
        e->phentsize = (uint32_t)elf_get(e, 54, 2);
        e->phnum = (uint32_t)elf_get(e, 56, 2);
        e->shentsize = (uint32_t)elf_get(e, 58, 2);
        e->shnum = (uint32_t)elf_get(e, 60, 2);
        e->shstrndx = (uint32_t)elf_get(e, 62, 2);
    } else {
        e->entry = elf_get(e, 24, 4);
        e->phoff = elf_get(e, 28, 4);
        e->shoff = elf_get(e, 32, 4);
        e->flags = (uint32_t)elf_get(e, 36, 4);
        e->phentsize = (uint32_t)elf_get(e, 42, 2);
        e->phnum = (uint32_t)elf_get(e, 44, 2);
        e->shentsize = (uint32_t)elf_get(e, 46, 2);
        e->shnum = (uint32_t)elf_get(e, 48, 2);
        e->shstrndx = (uint32_t)elf_get(e, 50, 2);
    }
    if (e->shentsize < (e->is64 ? 64u : 40u)) {
        e->shnum = 0;
    }
    if (e->phentsize < (e->is64 ? 56u : 32u)) {
        e->phnum = 0;
    }
    /* Extended numbering: section 0 holds the real counts. */
    if (e->shoff != 0 && e->shentsize != 0 && elf_has(e, e->shoff, e->shentsize)) {
        if (e->shnum == 0) {
            e->shnum = (uint32_t)(elf_word(e, e->shoff + (e->is64 ? 32u : 20u)) & 0xFFFFFFFFu);
        }
        if (e->shstrndx == ELF_SHN_XINDEX) {
            e->shstrndx = (uint32_t)elf_get(e, e->shoff + (e->is64 ? 40u : 24u), 4);
        }
    }
    return XXBYTE_STATUS_OK;
}

static int elf_section(const elf_file *e, uint32_t index, elf_shdr *s)
{
    uint64_t at;

    memset(s, 0, sizeof(*s));
    if (index >= e->shnum || e->shoff > e->size || (uint64_t)index > (e->size - e->shoff) / e->shentsize) {
        return 0;
    }
    at = e->shoff + (uint64_t)index * e->shentsize;
    if (!elf_has(e, at, e->is64 ? 64u : 40u)) {
        return 0;
    }
    s->name = (uint32_t)elf_get(e, at, 4);
    s->type = (uint32_t)elf_get(e, at + 4, 4);
    if (e->is64) {
        s->flags = elf_get(e, at + 8, 8);
        s->addr = elf_get(e, at + 16, 8);
        s->offset = elf_get(e, at + 24, 8);
        s->size = elf_get(e, at + 32, 8);
        s->link = (uint32_t)elf_get(e, at + 40, 4);
        s->info = (uint32_t)elf_get(e, at + 44, 4);
    } else {
        s->flags = elf_get(e, at + 8, 4);
        s->addr = elf_get(e, at + 12, 4);
        s->offset = elf_get(e, at + 16, 4);
        s->size = elf_get(e, at + 20, 4);
        s->link = (uint32_t)elf_get(e, at + 24, 4);
        s->info = (uint32_t)elf_get(e, at + 28, 4);
    }
    return 1;
}

/* The section's bytes, or NULL when it has none in the file. */
static const uint8_t *elf_section_data(const elf_file *e, const elf_shdr *s)
{
    if (s->type == ELF_SHT_NOBITS || !elf_has(e, s->offset, s->size)) {
        return NULL;
    }
    return e->data + s->offset;
}

static int elf_segment(const elf_file *e, uint32_t index, elf_phdr *p)
{
    uint64_t at;

    memset(p, 0, sizeof(*p));
    if (index >= e->phnum || e->phoff > e->size || (uint64_t)index > (e->size - e->phoff) / e->phentsize) {
        return 0;
    }
    at = e->phoff + (uint64_t)index * e->phentsize;
    if (!elf_has(e, at, e->is64 ? 56u : 32u)) {
        return 0;
    }
    p->type = (uint32_t)elf_get(e, at, 4);
    if (e->is64) {
        p->flags = (uint32_t)elf_get(e, at + 4, 4);
        p->offset = elf_get(e, at + 8, 8);
        p->vaddr = elf_get(e, at + 16, 8);
        p->filesz = elf_get(e, at + 32, 8);
        p->memsz = elf_get(e, at + 40, 8);
    } else {
        p->offset = elf_get(e, at + 4, 4);
        p->vaddr = elf_get(e, at + 8, 4);
        p->filesz = elf_get(e, at + 16, 4);
        p->memsz = elf_get(e, at + 20, 4);
        p->flags = (uint32_t)elf_get(e, at + 24, 4);
    }
    return 1;
}

/* The NUL-terminated string at offset in a string table section, or NULL. */
static const char *elf_string(const elf_file *e, uint32_t strtab, uint64_t offset)
{
    elf_shdr s;
    const uint8_t *data;

    if (!elf_section(e, strtab, &s) || s.type != ELF_SHT_STRTAB || (data = elf_section_data(e, &s)) == NULL || offset >= s.size) {
        return NULL;
    }
    if (memchr(data + offset, 0, (size_t)(s.size - offset)) == NULL) {
        return NULL;
    }
    return (const char *)data + offset;
}

/* A symbol-table name as the sBPF loader reads its debug labels: the NUL
 * must come within 255 bytes (DEBUG_SYMBOL_NAME_LENGTH_MAXIMUM). */
static const char *elf_label_string(const elf_file *e, uint32_t strtab, uint64_t offset)
{
    const char *name = elf_string(e, strtab, offset);
    return name != NULL && strlen(name) < 255u ? name : NULL;
}

static const char *elf_section_name(const elf_file *e, const elf_shdr *s)
{
    return elf_string(e, e->shstrndx, s->name);
}

static uint32_t elf_find_section(const elf_file *e, const char *name, elf_shdr *s)
{
    uint32_t i;

    for (i = 1; i < e->shnum; ++i) {
        const char *n;
        if (elf_section(e, i, s) && (n = elf_section_name(e, s)) != NULL && strcmp(n, name) == 0) {
            return i;
        }
    }
    memset(s, 0, sizeof(*s));
    return 0;
}

static uint64_t elf_sym_size(const elf_file *e)
{
    return e->is64 ? 24u : 16u;
}

static uint64_t elf_sym_count(const elf_file *e, const elf_shdr *symtab)
{
    if ((symtab->type != ELF_SHT_SYMTAB && symtab->type != ELF_SHT_DYNSYM) || elf_section_data(e, symtab) == NULL) {
        return 0;
    }
    return symtab->size / elf_sym_size(e);
}

static int elf_symbol(const elf_file *e, const elf_shdr *symtab, uint64_t index, elf_sym *sym)
{
    uint64_t at;

    memset(sym, 0, sizeof(*sym));
    if (index >= elf_sym_count(e, symtab)) {
        return 0;
    }
    at = symtab->offset + index * elf_sym_size(e);
    sym->name = (uint32_t)elf_get(e, at, 4);
    if (e->is64) {
        sym->info = (uint32_t)elf_get(e, at + 4, 1);
        sym->shndx = (uint32_t)elf_get(e, at + 6, 2);
        sym->value = elf_get(e, at + 8, 8);
    } else {
        sym->value = elf_get(e, at + 4, 4);
        sym->info = (uint32_t)elf_get(e, at + 12, 1);
        sym->shndx = (uint32_t)elf_get(e, at + 14, 2);
    }
    return 1;
}

xxbyte_status xxbyte_ebpf_elf_mode(const uint8_t *data, size_t size, uint32_t *mode)
{
    elf_file e;
    xxbyte_status status;

    if (mode == NULL || (data == NULL && size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *mode = XXBYTE_EBPF_MODE_DEFAULT;
    status = elf_open(data, size, &e);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (e.is64 && !e.be && (e.machine == ELF_EM_SBPF || e.type == ELF_ET_DYN || e.flags != 0)) {
        if (e.flags > XXBYTE_EBPF_SBPF_NEWEST) {
            return XXBYTE_STATUS_UNSUPPORTED_MODE;
        }
        *mode = XXBYTE_EBPF_MODE_SBPF(e.flags);
        return XXBYTE_STATUS_OK;
    }
    *mode = e.be ? XXBYTE_EBPF_MODE_BIG_ENDIAN : XXBYTE_EBPF_MODE_LITTLE_ENDIAN;
    return XXBYTE_STATUS_OK;
}

/* Resolver text into the core's buffer. */

typedef struct name_out {
    char *buffer;
    size_t size;
    size_t length;
} name_out;

static void name_str(name_out *o, const char *text)
{
    for (; *text != '\0'; ++text) {
        if (o->length + 1u < o->size) {
            o->buffer[o->length] = *text;
        }
        ++o->length;
    }
}

static void name_hex(name_out *o, uint64_t value)
{
    char digits[20];
    size_t n = 0;

    name_str(o, "0x");
    do {
        digits[n++] = "0123456789abcdef"[value & 0xFu];
        value >>= 4;
    } while (value != 0);
    while (n > 0) {
        char one[2];
        one[0] = digits[--n];
        one[1] = '\0';
        name_str(o, one);
    }
}

static void name_dec(name_out *o, uint64_t value)
{
    char digits[24];
    size_t n = 0;

    do {
        digits[n++] = (char)('0' + (char)(value % 10u));
        value /= 10u;
    } while (value != 0);
    while (n > 0) {
        char one[2];
        one[0] = digits[--n];
        one[1] = '\0';
        name_str(o, one);
    }
}

/* "+0x10" or "-0x10"; nothing for 0. */
static void name_addend(name_out *o, int64_t addend)
{
    if (addend > 0) {
        name_str(o, "+");
        name_hex(o, (uint64_t)addend);
    } else if (addend < 0) {
        name_str(o, "-");
        name_hex(o, 0u - (uint64_t)addend);
    }
}

static size_t name_done(name_out *o)
{
    if (o->size == 0) {
        return 0;
    }
    o->buffer[o->length < o->size ? o->length : o->size - 1u] = '\0';
    return o->length;
}

/* Listing helpers. */

static int list_header(xx_buf_t *out, const char *name, const char *section, uint64_t start, uint64_t end)
{
    return xx_buf_appendf(out, "\n; %s (%s 0x%llx-0x%llx)\n", name, section, (unsigned long long)start, (unsigned long long)end) ? 1 : 0;
}

static const char *elf_class_text(const elf_file *e)
{
    return e->is64 ? (e->be ? "ELF64 big-endian" : "ELF64 little-endian") : (e->be ? "ELF32 big-endian" : "ELF32 little-endian");
}

static int list_file_header(xx_buf_t *out, const elf_file *e, uint32_t mode)
{
    if (XXBYTE_EBPF_MODE_IS_SBPF(mode)) {
        return xx_buf_appendf(out, "; %s, e_machine %u, e_type %u, e_flags 0x%x: Solana sBPF v%u\n", elf_class_text(e), (unsigned)e->machine, (unsigned)e->type,
                              (unsigned)e->flags, (unsigned)XXBYTE_EBPF_MODE_SBPF_VERSION(mode))
                   ? 1
                   : 0;
    }
    return xx_buf_appendf(out, "; %s, e_machine %u, e_type %u, e_flags 0x%x: Linux eBPF%s\n", elf_class_text(e), (unsigned)e->machine, (unsigned)e->type,
                          (unsigned)e->flags, (mode & XXBYTE_EBPF_MODE_BIG_ENDIAN) != 0 ? ", big-endian" : "")
               ? 1
               : 0;
}

static const char *bpf_reloc_name(uint32_t type)
{
    switch (type) {
        case 0: return "R_BPF_NONE";
        case 1: return "R_BPF_64_64";
        case 2: return "R_BPF_64_ABS64";
        case 3: return "R_BPF_64_ABS32";
        case 4: return "R_BPF_64_NODYLD32";
        case 8: return "R_BPF_64_RELATIVE";
        case 10: return "R_BPF_64_32";
        default: return NULL;
    }
}

/* ------------------------------------------------------------------ */
/* Objects: executable sections, the symbol table and REL/RELA sections. */

typedef struct obj_label {
    uint64_t offset;
    uint32_t sym;
    uint32_t func;
} obj_label;

typedef struct obj_ctx {
    const elf_file *e;
    uint32_t mode;
    elf_shdr symtab;
    uint32_t section; /* the listed section */
    elf_shdr shdr;
    uint64_t base; /* subtracted from symbol values and r_offset */
    elf_reloc *relocs;
    size_t reloc_count;
    obj_label *labels; /* the section's named symbols by offset */
    size_t label_count;
    xxbyte_ebpf_context context;
} obj_ctx;

static int obj_label_compare(const void *a, const void *b)
{
    const obj_label *x = (const obj_label *)a;
    const obj_label *y = (const obj_label *)b;

    if (x->offset != y->offset) {
        return x->offset < y->offset ? -1 : 1;
    }
    if (x->func != y->func) {
        return x->func ? -1 : 1;
    }
    return x->sym < y->sym ? -1 : x->sym > y->sym ? 1 : 0;
}

static int reloc_compare(const void *a, const void *b)
{
    const elf_reloc *x = (const elf_reloc *)a;
    const elf_reloc *y = (const elf_reloc *)b;

    if (x->offset != y->offset) {
        return x->offset < y->offset ? -1 : 1;
    }
    return x->type < y->type ? -1 : x->type > y->type ? 1 : 0;
}

/* The first relocation at offset, or NULL. */
static const elf_reloc *reloc_at(const elf_reloc *relocs, size_t count, uint64_t offset)
{
    size_t low = 0;
    size_t high = count;

    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (relocs[middle].offset < offset) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return low < count && relocs[low].offset == offset ? &relocs[low] : NULL;
}

/* A symbol's name: the section name for section symbols. */
static const char *obj_symbol_name(const obj_ctx *c, uint32_t index, elf_sym *sym)
{
    const char *name;

    if (!elf_symbol(c->e, &c->symtab, index, sym)) {
        return NULL;
    }
    if ((sym->info & 0xFu) == ELF_STT_SECTION) {
        elf_shdr s;
        return elf_section(c->e, sym->shndx, &s) ? elf_section_name(c->e, &s) : NULL;
    }
    name = elf_string(c->e, c->symtab.link, sym->name);
    return name != NULL && name[0] != '\0' ? name : NULL;
}

/* The label at or before offset in the listed section: "name" or
 * "name+0x10". */
static int obj_label_text(const obj_ctx *c, uint64_t offset, name_out *o)
{
    size_t low = 0;
    size_t high = c->label_count;
    elf_sym sym;
    const char *name;

    if (offset >= c->shdr.size) {
        return 0;
    }
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (c->labels[middle].offset <= offset) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    if (low == 0) {
        return 0;
    }
    /* The preferred symbol of the last offset not above the target. */
    high = low - 1u;
    while (high > 0 && c->labels[high - 1u].offset == c->labels[low - 1u].offset) {
        --high;
    }
    name = obj_symbol_name(c, c->labels[high].sym, &sym);
    if (name == NULL) {
        return 0;
    }
    name_str(o, name);
    name_addend(o, (int64_t)(offset - c->labels[high].offset));
    return 1;
}

/* The label at or before offset in another section, by a scan of the
 * symbol table (functions win over other symbols at one offset). */
static int obj_label_elsewhere(const obj_ctx *c, uint32_t section, uint64_t offset, name_out *o)
{
    const elf_file *e = c->e;
    uint64_t n = elf_sym_count(e, &c->symtab);
    uint64_t k;
    uint64_t best = 0;
    uint64_t best_offset = 0;
    int best_func = 0;
    elf_shdr s;
    elf_sym sym;
    const char *name;

    if (!elf_section(e, section, &s) || offset >= s.size) {
        return 0;
    }
    for (k = 1; k < n; ++k) {
        uint32_t type;
        uint64_t at;
        if (!elf_symbol(e, &c->symtab, k, &sym) || sym.shndx != section) {
            continue;
        }
        type = sym.info & 0xFu;
        at = sym.value - (e->type == ELF_ET_REL ? 0u : s.addr);
        name = elf_string(e, c->symtab.link, sym.name);
        if (type == ELF_STT_SECTION || type == ELF_STT_FILE || name == NULL || name[0] == '\0' || at > offset) {
            continue;
        }
        if (best == 0 || at > best_offset || (at == best_offset && type == ELF_STT_FUNC && !best_func)) {
            best = k;
            best_offset = at;
            best_func = type == ELF_STT_FUNC;
        }
    }
    if (best == 0 || (name = obj_symbol_name(c, (uint32_t)best, &sym)) == NULL) {
        return 0;
    }
    name_str(o, name);
    name_addend(o, (int64_t)(offset - best_offset));
    return 1;
}

static size_t obj_resolver(void *context, const xxbyte_instruction *instruction, uint32_t index, char *buffer, size_t buffer_size)
{
    const obj_ctx *c = (const obj_ctx *)context;
    const xxbyte_operand *operand = &instruction->operands[index];
    const elf_reloc *r = reloc_at(c->relocs, c->reloc_count, instruction->address);
    name_out o;
    elf_sym sym;
    const char *name;

    o.buffer = buffer;
    o.size = buffer_size;
    o.length = 0;
    if (operand->type == XXBYTE_OPERAND_BRANCH) {
        if (instruction->id == XXBYTE_EBPF_ID_CALL_LOCAL && r != NULL && r->type == R_BPF_64_32 && (name = obj_symbol_name(c, r->sym, &sym)) != NULL) {
            if ((sym.info & 0xFu) == ELF_STT_SECTION) {
                /* A call into a section: the immediate is the slot offset. */
                elf_shdr target_section;
                uint64_t target = SLOT * ((uint64_t)(int64_t)operand->extra + 1u);
                if (elf_section(c->e, sym.shndx, &target_section)) {
                    target += sym.value - (c->e->type == ELF_ET_REL ? 0u : target_section.addr);
                    if (sym.shndx == c->section ? obj_label_text(c, target, &o) : obj_label_elsewhere(c, sym.shndx, target, &o)) {
                        return name_done(&o);
                    }
                }
            }
            name_str(&o, name);
            return name_done(&o);
        }
        return obj_label_text(c, (uint64_t)operand->value, &o) ? name_done(&o) : 0;
    }
    if (operand->type == XXBYTE_OPERAND_INDEX && r != NULL && (r->type == R_BPF_64_64 || r->type == R_BPF_64_32) && (name = obj_symbol_name(c, r->sym, &sym)) != NULL) {
        name_str(&o, name);
        if (operand->kind == XXBYTE_EBPF_INDEX_SYMBOL) {
            name_addend(&o, (int64_t)((uint64_t)operand->value + (uint64_t)r->addend));
        }
        return name_done(&o);
    }
    return 0;
}

static int obj_lddw_relocated(void *user, uint64_t address)
{
    const obj_ctx *c = (const obj_ctx *)user;
    const elf_reloc *r = reloc_at(c->relocs, c->reloc_count, address);

    return r != NULL && r->type == R_BPF_64_64;
}

/* Collects the relocations of section index (offsets made section-relative)
 * from every REL and RELA section that applies to it. */
static int obj_collect_relocs(const elf_file *e, uint32_t index, uint64_t base, elf_reloc **out, size_t *count)
{
    uint32_t i;
    size_t capacity = 0;

    *out = NULL;
    *count = 0;
    for (i = 1; i < e->shnum; ++i) {
        elf_shdr s;
        uint64_t entry;
        uint64_t n;
        uint64_t k;

        if (!elf_section(e, i, &s) || (s.type != ELF_SHT_REL && s.type != ELF_SHT_RELA) || s.info != index || elf_section_data(e, &s) == NULL) {
            continue;
        }
        entry = (e->is64 ? 16u : 8u) + (s.type == ELF_SHT_RELA ? (e->is64 ? 8u : 4u) : 0u);
        n = s.size / entry;
        for (k = 0; k < n; ++k) {
            uint64_t at = s.offset + k * entry;
            uint64_t offset = elf_word(e, at);
            uint64_t info = elf_word(e, at + (e->is64 ? 8u : 4u));
            elf_reloc *r;

            if (*count == capacity) {
                size_t grown = capacity == 0 ? 64u : capacity * 2u;
                elf_reloc *bigger = (elf_reloc *)xx_mem_realloc(*out, grown * sizeof(elf_reloc));
                if (bigger == NULL) {
                    return 0;
                }
                *out = bigger;
                capacity = grown;
            }
            r = &(*out)[(*count)++];
            r->offset = offset - base;
            r->type = (uint32_t)(e->is64 ? info & 0xFFFFFFFFu : info & 0xFFu);
            r->sym = (uint32_t)(e->is64 ? info >> 32 : info >> 8);
            r->addend = 0;
            if (s.type == ELF_SHT_RELA) {
                r->addend = e->is64 ? (int64_t)elf_get(e, at + 16, 8) : (int64_t)xxbyte_sext(elf_get(e, at + 8, 4), 32);
            }
        }
    }
    if (*count > 1) {
        qsort(*out, *count, sizeof(elf_reloc), reloc_compare);
    }
    return 1;
}

/* The relocations of one listed instruction that no resolver shows, as
 * comment lines. */
static int obj_reloc_lines(const obj_ctx *c, const xxbyte_instruction *instruction, uint64_t address, size_t size, xx_buf_t *out)
{
    const elf_reloc *r;
    size_t i;
    int named = 0;

    for (r = c->relocs, i = 0; i < c->reloc_count; ++i, ++r) {
        elf_sym sym;
        const char *name;
        const char *type;

        if (r->offset < address || r->offset - address >= size) {
            continue;
        }
        if (instruction != NULL && r->offset == address &&
            ((r->type == R_BPF_64_64 && instruction->operand_count == 2 && instruction->operands[1].type == XXBYTE_OPERAND_INDEX) ||
             (r->type == R_BPF_64_32 &&
              (instruction->id == XXBYTE_EBPF_ID_CALL_LOCAL || instruction->id == XXBYTE_EBPF_ID_CALL_KFUNC || instruction->id == XXBYTE_EBPF_ID_CALL)))) {
            if (!named) {
                named = 1;
                continue;
            }
        }
        name = obj_symbol_name(c, r->sym, &sym);
        type = bpf_reloc_name(r->type);
        if ((type != NULL ? !xx_buf_appendf(out, "          ; %s", type) : !xx_buf_appendf(out, "          ; R_BPF_%u", (unsigned)r->type)) ||
            !xx_buf_appendf(out, " %s", name != NULL ? name : "?")) {
            return 0;
        }
        if (r->addend != 0 &&
            !xx_buf_appendf(out, "%s0x%llx", r->addend < 0 ? "-" : "+", (unsigned long long)(r->addend < 0 ? 0u - (uint64_t)r->addend : (uint64_t)r->addend))) {
            return 0;
        }
        if (!xx_buf_appendf(out, " at 0x%llx\n", (unsigned long long)r->offset)) {
            return 0;
        }
    }
    return 1;
}

/* Lists code[start..end) one instruction at a time, each followed by the
 * relocations it shows no name for. */
static xxbyte_status obj_list_body(obj_ctx *c, const uint8_t *code, uint64_t start, uint64_t end, uint32_t listing_flags, xx_buf_t *out)
{
    uint64_t at = start;

    while (at < end) {
        xxbyte_instruction instruction;
        size_t size;
        xxbyte_status decoded = xxbyte_decode_ex(XXBYTE_FAMILY_EBPF, c->mode, &c->context, code + at, (size_t)(end - at), at, &instruction);
        xxbyte_status status;

        size = decoded == XXBYTE_STATUS_OK ? instruction.size : (size_t)(end - at < SLOT ? end - at : SLOT);
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_EBPF, c->mode, &c->context, code + at, size, at, listing_flags, obj_resolver, c, out);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (c->reloc_count != 0 && !obj_reloc_lines(c, decoded == XXBYTE_STATUS_OK ? &instruction : NULL, at, size, out)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        at += size;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status obj_list_section(obj_ctx *c, uint32_t listing_flags, xx_buf_t *out)
{
    const elf_file *e = c->e;
    const uint8_t *code = elf_section_data(e, &c->shdr);
    const char *section = elf_section_name(e, &c->shdr);
    uint64_t n = elf_sym_count(e, &c->symtab);
    uint64_t k;
    size_t capacity = 0;
    size_t i;
    size_t functions = 0;
    size_t f = 0;
    obj_label *starts = NULL;
    uint64_t at = 0;
    xxbyte_status status = XXBYTE_STATUS_OK;

    if (section == NULL) {
        section = "?";
    }
    c->base = e->type == ELF_ET_REL ? 0u : c->shdr.addr;
    if (!obj_collect_relocs(e, c->section, c->base, &c->relocs, &c->reloc_count)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (k = 1; k < n; ++k) {
        elf_sym sym;
        uint32_t type;
        const char *name;

        if (!elf_symbol(e, &c->symtab, k, &sym) || sym.shndx != c->section) {
            continue;
        }
        type = sym.info & 0xFu;
        name = elf_string(e, c->symtab.link, sym.name);
        if (type == ELF_STT_SECTION || type == ELF_STT_FILE || name == NULL || name[0] == '\0' || sym.value - c->base >= c->shdr.size) {
            continue;
        }
        if (c->label_count == capacity) {
            size_t grown = capacity == 0 ? 32u : capacity * 2u;
            obj_label *bigger = (obj_label *)xx_mem_realloc(c->labels, grown * sizeof(obj_label));
            if (bigger == NULL) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            c->labels = bigger;
            capacity = grown;
        }
        c->labels[c->label_count].offset = sym.value - c->base;
        c->labels[c->label_count].sym = (uint32_t)k;
        c->labels[c->label_count].func = type == ELF_STT_FUNC;
        ++c->label_count;
    }
    if (c->label_count > 1) {
        qsort(c->labels, c->label_count, sizeof(obj_label), obj_label_compare);
    }
    /* Bodies run from one function symbol to the next; the preferred
     * symbol of an offset sorts first. */
    if (c->label_count != 0) {
        starts = (obj_label *)xx_mem_alloc(c->label_count * sizeof(obj_label));
        if (starts == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    for (i = 0; i < c->label_count; ++i) {
        if (c->labels[i].func && (functions == 0 || starts[functions - 1u].offset != c->labels[i].offset)) {
            starts[functions++] = c->labels[i];
        }
    }
    while (status == XXBYTE_STATUS_OK && at < c->shdr.size) {
        uint64_t next;
        const char *name = section;
        elf_sym sym;

        if (f < functions && starts[f].offset == at) {
            const char *fn = obj_symbol_name(c, starts[f].sym, &sym);
            name = fn != NULL ? fn : section;
            ++f;
        }
        next = f < functions ? starts[f].offset : c->shdr.size;
        if (!list_header(out, name, section, at, next)) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        status = obj_list_body(c, code, at, next, listing_flags, out);
        at = next;
    }
    xx_mem_free(starts);
    return status;
}

static xxbyte_status obj_list(const elf_file *e, uint32_t mode, uint32_t listing_flags, xx_buf_t *out)
{
    uint32_t i;
    elf_shdr symtab;
    xxbyte_status status = XXBYTE_STATUS_OK;
    int truncated = 0;

    memset(&symtab, 0, sizeof(symtab));
    for (i = 1; i < e->shnum; ++i) {
        if (elf_section(e, i, &symtab) && symtab.type == ELF_SHT_SYMTAB) {
            break;
        }
        memset(&symtab, 0, sizeof(symtab));
    }
    for (i = 1; i < e->shnum && status == XXBYTE_STATUS_OK; ++i) {
        obj_ctx c;

        memset(&c, 0, sizeof(c));
        if (!elf_section(e, i, &c.shdr)) {
            truncated = 1;
            continue;
        }
        if (c.shdr.type != ELF_SHT_PROGBITS || (c.shdr.flags & ELF_SHF_EXECINSTR) == 0 || c.shdr.size == 0) {
            continue;
        }
        if (elf_section_data(e, &c.shdr) == NULL) {
            truncated = 1;
            continue;
        }
        c.e = e;
        c.mode = mode;
        c.symtab = symtab;
        c.section = i;
        c.context.user = &c;
        c.context.lddw_relocated = obj_lddw_relocated;
        status = obj_list_section(&c, listing_flags, out);
        xx_mem_free(c.relocs);
        xx_mem_free(c.labels);
    }
    if (status == XXBYTE_STATUS_OK && (truncated || (e->shnum != 0 && !elf_has(e, e->shoff, (uint64_t)e->shnum * e->shentsize)))) {
        status = XXBYTE_STATUS_TRUNCATED;
    }
    return status;
}

/* ------------------------------------------------------------------ */
/* Solana programs: the sBPF loader's view (solana-sbpf elf.rs). */

typedef struct sol_function {
    uint32_t key;
    uint32_t used;
    uint64_t pc;
    const char *name; /* NULL: "function_<pc>" */
} sol_function;

typedef struct sol_syscall {
    uint64_t offset; /* in the code */
    const char *name;
} sol_syscall;

typedef struct sol_ctx {
    uint32_t version;
    uint64_t slots;
    uint64_t super_root; /* the static analysis' extra block: the slot count */
    sol_function *table; /* open addressing by key */
    size_t capacity;
    size_t count;
    sol_function *by_pc; /* one per pc, sorted */
    size_t pc_count;
    uint8_t *starts;       /* per slot: bit 0 set at an instruction start */
    sol_syscall *syscalls; /* sorted by offset */
    size_t syscall_count;
    size_t syscall_capacity;
    xxbyte_ebpf_context context;
} sol_ctx;

static uint32_t sol_pc_key(uint64_t pc)
{
    uint8_t bytes[8];
    unsigned i;

    for (i = 0; i < 8; ++i) {
        bytes[i] = (uint8_t)(pc >> (8u * i));
    }
    return xxbyte_ebpf_sbpf_hash(bytes, sizeof(bytes));
}

static sol_function *sol_find(const sol_ctx *c, uint32_t key)
{
    size_t i;

    if (c->capacity == 0) {
        return NULL;
    }
    for (i = (key * 2654435761u) & (c->capacity - 1u);; i = (i + 1u) & (c->capacity - 1u)) {
        if (!c->table[i].used) {
            return NULL;
        }
        if (c->table[i].key == key) {
            return &c->table[i];
        }
    }
}

static int sol_grow(sol_ctx *c)
{
    size_t capacity = c->capacity == 0 ? 64u : c->capacity * 2u;
    sol_function *table = (sol_function *)xx_mem_calloc(capacity, sizeof(sol_function));
    size_t i;

    if (table == NULL) {
        return 0;
    }
    for (i = 0; i < c->capacity; ++i) {
        if (c->table[i].used) {
            size_t j = (c->table[i].key * 2654435761u) & (capacity - 1u);
            while (table[j].used) {
                j = (j + 1u) & (capacity - 1u);
            }
            table[j] = c->table[i];
        }
    }
    xx_mem_free(c->table);
    c->table = table;
    c->capacity = capacity;
    return 1;
}

/* FunctionRegistry::register_function: the first name of a key stays; a
 * key already naming another pc is a collision the loader rejects. */
static int sol_register(sol_ctx *c, uint32_t key, const char *name, uint64_t pc)
{
    size_t i;

    if (sol_find(c, key) != NULL) {
        return 1;
    }
    if ((c->count + 1u) * 2u > c->capacity && !sol_grow(c)) {
        return 0;
    }
    for (i = (key * 2654435761u) & (c->capacity - 1u); c->table[i].used; i = (i + 1u) & (c->capacity - 1u)) {
    }
    c->table[i].used = 1;
    c->table[i].key = key;
    c->table[i].pc = pc;
    c->table[i].name = name;
    ++c->count;
    return 1;
}

/* register_function_hashed_legacy: the key of "entrypoint" is its name's
 * hash, every other function's the hash of its pc. */
static int sol_register_legacy(sol_ctx *c, const char *name, uint64_t pc)
{
    uint32_t key = name != NULL && strcmp(name, "entrypoint") == 0 ? xxbyte_ebpf_sbpf_hash("entrypoint", 10) : sol_pc_key(pc);
    return sol_register(c, key, name, pc);
}

static void sol_unregister(sol_ctx *c, uint32_t key)
{
    sol_function *f = sol_find(c, key);
    size_t i;
    size_t j;

    if (f == NULL) {
        return;
    }
    /* Backward-shift deletion keeps the probe sequences intact. */
    i = (size_t)(f - c->table);
    c->table[i].used = 0;
    --c->count;
    for (j = (i + 1u) & (c->capacity - 1u); c->table[j].used; j = (j + 1u) & (c->capacity - 1u)) {
        sol_function moved = c->table[j];
        c->table[j].used = 0;
        --c->count;
        sol_register(c, moved.key, moved.name, moved.pc);
    }
}

static int sol_add_syscall(sol_ctx *c, uint64_t offset, const char *name)
{
    if (c->syscall_count == c->syscall_capacity) {
        size_t grown = c->syscall_capacity == 0 ? 32u : c->syscall_capacity * 2u;
        sol_syscall *bigger = (sol_syscall *)xx_mem_realloc(c->syscalls, grown * sizeof(sol_syscall));
        if (bigger == NULL) {
            return 0;
        }
        c->syscalls = bigger;
        c->syscall_capacity = grown;
    }
    c->syscalls[c->syscall_count].offset = offset;
    c->syscalls[c->syscall_count].name = name;
    ++c->syscall_count;
    return 1;
}

static int sol_syscall_compare(const void *a, const void *b)
{
    const sol_syscall *x = (const sol_syscall *)a;
    const sol_syscall *y = (const sol_syscall *)b;

    return x->offset < y->offset ? -1 : x->offset > y->offset ? 1 : 0;
}

/* One entry per pc, preferring the name of the pc's own key over that of
 * "entrypoint". */
static int sol_function_compare(const void *a, const void *b)
{
    const sol_function *x = (const sol_function *)a;
    const sol_function *y = (const sol_function *)b;

    if (x->pc != y->pc) {
        return x->pc < y->pc ? -1 : 1;
    }
    return x->key < y->key ? -1 : x->key > y->key ? 1 : 0;
}

static int sol_index_functions(sol_ctx *c)
{
    size_t i;
    size_t n = 0;
    uint32_t entry_key = xxbyte_ebpf_sbpf_hash("entrypoint", 10);

    if (c->count == 0) {
        return 1;
    }
    c->by_pc = (sol_function *)xx_mem_alloc(c->count * sizeof(sol_function));
    if (c->by_pc == NULL) {
        return 0;
    }
    for (i = 0; i < c->capacity; ++i) {
        if (c->table[i].used && c->table[i].pc < c->slots) {
            c->by_pc[n++] = c->table[i];
        }
    }
    if (n > 1) {
        qsort(c->by_pc, n, sizeof(sol_function), sol_function_compare);
    }
    c->pc_count = 0;
    for (i = 0; i < n; ++i) {
        if (c->pc_count != 0 && c->by_pc[c->pc_count - 1u].pc == c->by_pc[i].pc) {
            sol_function *kept = &c->by_pc[c->pc_count - 1u];
            int kept_natural = c->version >= 3 ? kept->key == (uint32_t)kept->pc : kept->key != entry_key;
            if (!kept_natural) {
                *kept = c->by_pc[i];
            }
            continue;
        }
        c->by_pc[c->pc_count++] = c->by_pc[i];
    }
    return 1;
}

static const sol_function *sol_at_pc(const sol_ctx *c, uint64_t pc)
{
    size_t low = 0;
    size_t high = c->pc_count;

    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (c->by_pc[middle].pc == pc) {
            return &c->by_pc[middle];
        }
        if (c->by_pc[middle].pc < pc) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }
    return NULL;
}

static void sol_function_name(const sol_function *f, name_out *o)
{
    if (f->name != NULL) {
        name_str(o, f->name);
    } else {
        name_str(o, "function_");
        name_dec(o, f->pc);
    }
}

/*
 * Rust legacy symbol demangling as rustc-demangle's Display shows it (the
 * hash element kept): "_ZN13relative_call12function_sum17h0123456789abcdefE"
 * is "relative_call::function_sum::h0123456789abcdef". Returns 0, writing
 * nothing, when name is not such a symbol.
 */
static int sol_demangle(const char *name, name_out *o)
{
    static const struct {
        const char *escape;
        char c;
    } escapes[] = {
        {"$SP$", '@'}, {"$BP$", '*'}, {"$RF$", '&'}, {"$LT$", '<'}, {"$GT$", '>'}, {"$LP$", '('}, {"$RP$", ')'}, {"$C$", ','},
    };
    const char *p = name;
    const char *end;
    size_t length = strlen(name);
    name_out check;
    int pass;

    if (strncmp(p, "__ZN", 4) == 0) {
        p += 4;
    } else if (strncmp(p, "_ZN", 3) == 0) {
        p += 3;
    } else if (strncmp(p, "ZN", 2) == 0) {
        p += 2;
    } else {
        return 0;
    }
    end = name + length;
    if (end == p || end[-1] != 'E') {
        return 0;
    }
    --end;
    /* The first pass validates, the second writes. */
    for (pass = 0; pass < 2; ++pass) {
        const char *q = p;
        name_out *out = o;
        char dummy[1];
        int first = 1;
        if (pass == 0) {
            check.buffer = dummy;
            check.size = 0;
            check.length = 0;
            out = &check;
        }
        while (q < end) {
            size_t n = 0;
            const char *ident;
            const char *ident_end;
            if (*q < '0' || *q > '9') {
                return 0;
            }
            while (q < end && *q >= '0' && *q <= '9') {
                n = n * 10u + (size_t)(*q - '0');
                if (n > length) {
                    return 0;
                }
                ++q;
            }
            if (n == 0 || n > (size_t)(end - q)) {
                return 0;
            }
            ident = q;
            ident_end = q + n;
            q = ident_end;
            if (!first) {
                name_str(out, "::");
            }
            first = 0;
            if (ident_end - ident >= 2 && ident[0] == '_' && ident[1] == '$') {
                ++ident;
            }
            while (ident < ident_end) {
                char one[2] = {0, 0};
                if (*ident == '.') {
                    if (ident + 1 < ident_end && ident[1] == '.') {
                        name_str(out, "::");
                        ident += 2;
                    } else {
                        name_str(out, ".");
                        ++ident;
                    }
                    continue;
                }
                if (*ident == '$') {
                    size_t k;
                    int matched = 0;
                    for (k = 0; k < sizeof(escapes) / sizeof(escapes[0]); ++k) {
                        size_t e = strlen(escapes[k].escape);
                        if ((size_t)(ident_end - ident) >= e && strncmp(ident, escapes[k].escape, e) == 0) {
                            one[0] = escapes[k].c;
                            name_str(out, one);
                            ident += e;
                            matched = 1;
                            break;
                        }
                    }
                    if (!matched && ident_end - ident >= 5 && ident[1] == 'u') {
                        const char *close = ident + 2;
                        unsigned value = 0;
                        while (close < ident_end && *close != '$' && close - ident < 10) {
                            char h = *close;
                            unsigned digit = h >= '0' && h <= '9' ? (unsigned)(h - '0') : h >= 'a' && h <= 'f' ? (unsigned)(h - 'a' + 10) : 16u;
                            if (digit == 16u) {
                                return 0;
                            }
                            value = value * 16u + digit;
                            ++close;
                        }
                        if (close < ident_end && *close == '$' && value >= 0x20 && value < 0x7F) {
                            one[0] = (char)value;
                            name_str(out, one);
                            ident = close + 1;
                            matched = 1;
                        }
                    }
                    if (!matched) {
                        return 0;
                    }
                    continue;
                }
                one[0] = *ident++;
                name_str(out, one);
            }
        }
    }
    return 1;
}

/* A block label: a demangled function name. */
static void sol_label_name(const sol_function *f, name_out *o)
{
    if (f->name == NULL || !sol_demangle(f->name, o)) {
        sol_function_name(f, o);
    }
}

static int sol_function_address(void *user, uint32_t key, uint64_t *address)
{
    const sol_function *f = sol_find((const sol_ctx *)user, key);

    if (f == NULL) {
        return 0;
    }
    *address = f->pc * SLOT;
    return 1;
}

static size_t sol_resolver(void *context, const xxbyte_instruction *instruction, uint32_t index, char *buffer, size_t buffer_size)
{
    const sol_ctx *c = (const sol_ctx *)context;
    const xxbyte_operand *operand = &instruction->operands[index];
    const sol_function *f = NULL;
    name_out o;

    o.buffer = buffer;
    o.size = buffer_size;
    o.length = 0;
    if (operand->type == XXBYTE_OPERAND_BRANCH) {
        uint64_t target = (uint64_t)operand->value;
        uint64_t slot = target / SLOT;
        int start;
        if (instruction->id == XXBYTE_EBPF_ID_CALL_LOCAL && c->version <= 2) {
            /* The registry name of the call's key. */
            f = sol_find(c, (uint32_t)operand->extra);
            if (f == NULL) {
                return 0;
            }
            sol_function_name(f, &o);
            return name_done(&o);
        }
        if (instruction->id == XXBYTE_EBPF_ID_CALL_LOCAL) {
            /* SBPFv3: the registry by pc, else the block label, else the
             * immediate; the pc wraps to 32 bits. */
            slot &= 0xFFFFFFFFu;
            f = sol_find(c, (uint32_t)slot);
            if (f != NULL) {
                sol_function_name(f, &o);
                return name_done(&o);
            }
        } else if (target % SLOT != 0) {
            slot = c->super_root + 1u;
        }
        start = slot < c->slots && c->starts != NULL && (c->starts[slot] & 1u) != 0;
        if (slot == c->super_root) {
            name_str(&o, "super_root");
        } else if (start) {
            f = sol_at_pc(c, slot);
            if (f == NULL) {
                return 0;
            }
            sol_label_name(f, &o);
        } else if (instruction->id == XXBYTE_EBPF_ID_CALL_LOCAL) {
            int64_t imm = (int64_t)operand->extra;
            if (imm < 0) {
                name_str(&o, "-");
            }
            name_dec(&o, imm < 0 ? 0u - (uint64_t)imm : (uint64_t)imm);
        } else {
            /* The reference's label of a target without a block. */
            name_str(&o, "[invalid]");
        }
        return name_done(&o);
    }
    if (operand->type == XXBYTE_OPERAND_INDEX && operand->kind == XXBYTE_EBPF_INDEX_SYSCALL && c->syscall_count != 0) {
        size_t low = 0;
        size_t high = c->syscall_count;
        while (low < high) {
            size_t middle = low + (high - low) / 2u;
            if (c->syscalls[middle].offset < instruction->address) {
                low = middle + 1u;
            } else {
                high = middle;
            }
        }
        if (low < c->syscall_count && c->syscalls[low].offset == instruction->address) {
            name_str(&o, c->syscalls[low].name);
            return name_done(&o);
        }
    }
    return 0;
}

static void sol_put32(uint8_t *code, uint64_t code_size, uint64_t offset, uint32_t value)
{
    unsigned i;

    if (offset > code_size || code_size - offset < 4) {
        return;
    }
    for (i = 0; i < 4; ++i) {
        code[offset + i] = (uint8_t)(value >> (8u * i));
    }
}

/* The dynamic table entry tag, or 0. */
static uint64_t sol_dynamic(const elf_file *e, uint64_t table_offset, uint64_t table_size, int64_t tag)
{
    uint64_t entry = e->is64 ? 16u : 8u;
    uint64_t k;

    for (k = 0; k + entry <= table_size; k += entry) {
        int64_t t = e->is64 ? (int64_t)elf_get(e, table_offset + k, 8) : (int64_t)xxbyte_sext(elf_get(e, table_offset + k, 4), 32);
        if (t == 0) {
            break;
        }
        if (t == tag) {
            return elf_word(e, table_offset + k + (e->is64 ? 8u : 4u));
        }
    }
    return 0;
}

/* Applies the loader's relocations (Executable::relocate) to the text copy
 * and fills the function registry: SBPFv0 to v2. */
static int sol_relocate(sol_ctx *c, const elf_file *e, const elf_shdr *text, uint8_t *code)
{
    uint64_t i;
    uint64_t dyn_offset = 0;
    uint64_t dyn_size = 0;
    uint32_t k;
    elf_shdr s;
    elf_phdr p;

    /* Program-counter-relative calls. */
    for (i = 0; i < c->slots; ++i) {
        int32_t imm = (int32_t)xxbyte_u32le(code + i * SLOT + 4);
        if (code[i * SLOT] == 0x85 && imm != -1) {
            int64_t target = (int64_t)i + 1 + imm;
            if (target >= 0 && (uint64_t)target < c->slots) {
                if (!sol_register(c, sol_pc_key((uint64_t)target), NULL, (uint64_t)target)) {
                    return 0;
                }
                sol_put32(code, c->slots * SLOT, i * SLOT + 4, sol_pc_key((uint64_t)target));
            }
        }
    }

    /* The dynamic relocations: DT_REL of PT_DYNAMIC, else SHT_DYNAMIC. */
    for (k = 0; k < e->phnum; ++k) {
        if (elf_segment(e, k, &p) && p.type == ELF_PT_DYNAMIC && elf_has(e, p.offset, p.filesz)) {
            dyn_offset = p.offset;
            dyn_size = p.filesz;
            break;
        }
    }
    if (dyn_size == 0) {
        for (k = 1; k < e->shnum; ++k) {
            if (elf_section(e, k, &s) && s.type == ELF_SHT_DYNAMIC && elf_section_data(e, &s) != NULL) {
                dyn_offset = s.offset;
                dyn_size = s.size;
                break;
            }
        }
    }
    if (dyn_size != 0) {
        uint64_t rel = sol_dynamic(e, dyn_offset, dyn_size, ELF_DT_REL);
        uint64_t relsz = sol_dynamic(e, dyn_offset, dyn_size, ELF_DT_RELSZ);
        uint64_t relent = sol_dynamic(e, dyn_offset, dyn_size, ELF_DT_RELENT);
        uint64_t symtab_vaddr = sol_dynamic(e, dyn_offset, dyn_size, ELF_DT_SYMTAB);
        uint64_t rel_offset = 0;
        int have_rel = 0;
        elf_shdr dynsym;
        elf_shdr dynstr;
        uint32_t dynstr_index = elf_find_section(e, ".dynstr", &dynstr);

        memset(&dynsym, 0, sizeof(dynsym));
        if (symtab_vaddr != 0) {
            for (k = 1; k < e->shnum; ++k) {
                if (elf_section(e, k, &s) && s.addr == symtab_vaddr) {
                    dynsym = s;
                    break;
                }
            }
        }
        if (rel != 0 && relent == (e->is64 ? 16u : 8u) && relsz != 0) {
            for (k = 0; k < e->phnum; ++k) {
                if (elf_segment(e, k, &p) && rel >= p.vaddr && rel - p.vaddr < p.memsz) {
                    rel_offset = rel - p.vaddr + p.offset;
                    have_rel = 1;
                    break;
                }
            }
            for (k = 1; !have_rel && k < e->shnum; ++k) {
                if (elf_section(e, k, &s) && s.addr == rel) {
                    rel_offset = s.offset;
                    have_rel = 1;
                }
            }
        }
        if (have_rel && elf_has(e, rel_offset, relsz)) {
            uint64_t n = relsz / relent;
            uint64_t text_end = text->offset + text->size;
            for (i = 0; i < n; ++i) {
                uint64_t at = rel_offset + i * relent;
                uint64_t r_offset = elf_word(e, at);
                uint64_t info = elf_word(e, at + (e->is64 ? 8u : 4u));
                uint32_t type = (uint32_t)(e->is64 ? info & 0xFFFFFFFFu : info & 0xFFu);
                uint32_t symbol = (uint32_t)(e->is64 ? info >> 32 : info >> 8);
                uint64_t imm = r_offset + 4u;
                elf_sym sym;
                int in_text = r_offset >= text->offset && r_offset < text_end;

                if (type == R_BPF_64_64 && in_text && elf_has(e, imm, 4) && elf_symbol(e, &dynsym, symbol, &sym)) {
                    uint64_t address = sym.value + (imm + 4u <= text_end ? (uint64_t)xxbyte_u32le(code + (imm - text->offset)) : elf_get(e, imm, 4));
                    if (address < (UINT64_C(1) << 32)) {
                        address += UINT64_C(1) << 32;
                    }
                    sol_put32(code, c->slots * SLOT, imm - text->offset, (uint32_t)address);
                    sol_put32(code, c->slots * SLOT, imm + SLOT - text->offset, (uint32_t)(address >> 32));
                } else if (type == R_BPF_64_RELATIVE && in_text && r_offset - text->offset + 16u <= c->slots * SLOT) {
                    uint64_t low = xxbyte_u32le(code + (imm - text->offset));
                    uint64_t high = xxbyte_u32le(code + (imm + SLOT - text->offset));
                    uint64_t address = (high << 32) | low;
                    if (address != 0) {
                        if (address < (UINT64_C(1) << 32)) {
                            address += UINT64_C(1) << 32;
                        }
                        sol_put32(code, c->slots * SLOT, imm - text->offset, (uint32_t)address);
                        sol_put32(code, c->slots * SLOT, imm + SLOT - text->offset, (uint32_t)(address >> 32));
                    }
                } else if (type == R_BPF_64_32 && elf_symbol(e, &dynsym, symbol, &sym)) {
                    const char *name = dynstr_index != 0 ? elf_string(e, dynstr_index, sym.name) : NULL;
                    uint32_t key;
                    if (name == NULL) {
                        continue;
                    }
                    if ((sym.info & 0xFu) == ELF_STT_FUNC && sym.value != 0) {
                        uint64_t pc;
                        if (sym.value < text->addr || sym.value - text->addr >= text->size) {
                            continue;
                        }
                        pc = (sym.value - text->addr) / SLOT;
                        if (!sol_register_legacy(c, name, pc)) {
                            return 0;
                        }
                        key = strcmp(name, "entrypoint") == 0 ? xxbyte_ebpf_sbpf_hash("entrypoint", 10) : sol_pc_key(pc);
                    } else {
                        key = xxbyte_ebpf_sbpf_hash(name, strlen(name));
                        if (in_text && !sol_add_syscall(c, r_offset - text->offset, name)) {
                            return 0;
                        }
                    }
                    if (in_text) {
                        sol_put32(code, c->slots * SLOT, imm - text->offset, key);
                    }
                }
            }
        }
    }

    /* The function symbols of the symbol table. */
    {
        elf_shdr symtab;
        elf_shdr strtab;
        uint32_t strtab_index = elf_find_section(e, ".strtab", &strtab);
        uint64_t n;

        if (elf_find_section(e, ".symtab", &symtab) != 0 && strtab_index != 0) {
            n = elf_sym_count(e, &symtab);
            for (i = 0; i < n; ++i) {
                elf_sym sym;
                const char *name;
                if (!elf_symbol(e, &symtab, i, &sym) || (sym.info & 0xEFu) != 0x02u || sym.value < text->addr || sym.value - text->addr >= text->size) {
                    continue;
                }
                name = elf_label_string(e, strtab_index, sym.name);
                if (name != NULL && !sol_register_legacy(c, name, (sym.value - text->addr) / SLOT)) {
                    return 0;
                }
            }
        }
    }

    /* The entrypoint. */
    if (e->entry >= text->addr && (e->entry - text->addr) % SLOT == 0) {
        uint32_t key = xxbyte_ebpf_sbpf_hash("entrypoint", 10);
        sol_unregister(c, key);
        if (!sol_register(c, key, "entrypoint", (e->entry - text->addr) / SLOT)) {
            return 0;
        }
    }
    if (c->syscall_count > 1) {
        qsort(c->syscalls, c->syscall_count, sizeof(sol_syscall), sol_syscall_compare);
    }
    return 1;
}

/* SBPFv3 and later: function labels from the symbol table, keyed by pc
 * (Executable::parse_debug_labels). */
static int sol_debug_labels(sol_ctx *c, const elf_file *e, uint64_t vaddr)
{
    elf_shdr symtab;
    elf_shdr strtab;
    uint32_t strtab_index = elf_find_section(e, ".strtab", &strtab);
    uint64_t n;
    uint64_t i;

    if (elf_find_section(e, ".symtab", &symtab) == 0 || strtab_index == 0) {
        return 1;
    }
    n = elf_sym_count(e, &symtab);
    for (i = 0; i < n; ++i) {
        elf_sym sym;
        const char *name;
        uint64_t pc;
        if (!elf_symbol(e, &symtab, i, &sym) || (sym.info & ELF_STT_FUNC) == 0) {
            continue;
        }
        name = elf_label_string(e, strtab_index, sym.name);
        pc = (sym.value > vaddr ? sym.value - vaddr : 0) / SLOT;
        if (name != NULL && !sol_register(c, (uint32_t)pc, name, pc)) {
            return 0;
        }
    }
    return 1;
}

/*
 * The function starts the reference's static analysis finds
 * (Analysis::split_into_basic_blocks and control_flow_graph_tarjan in
 * static_analysis.rs): the basic blocks of the program, then every
 * strongly connected component of the control-flow graph without an
 * outside predecessor names its first block "function_<pc>" unless a
 * function starts there. The function list becomes those of the registry
 * that start a block, plus these.
 */
typedef struct sol_cfg_node {
    uint64_t pc;
    uint32_t dest[2];
    uint32_t dest_count;
    uint32_t discovery;
    uint32_t lowlink;
    uint32_t scc;
    uint32_t on_stack;
    uint32_t external;
} sol_cfg_node;

#define SOL_NONE 0xFFFFFFFFu

static int sol_analyze(sol_ctx *c, const uint8_t *code)
{
    uint64_t slots = c->slots;
    uint8_t *flags = NULL;      /* per slot: 1 instruction, 2 block start, 4 function */
    uint32_t *index = NULL;     /* per slot: node index */
    uint64_t *edge_dest = NULL; /* per slot: two destinations */
    uint8_t *edge_count = NULL; /* per slot: 0xFF no edge, else 0 to 2 */
    sol_cfg_node *nodes = NULL;
    uint32_t *stack = NULL; /* recursion stack: node, edge */
    uint32_t *scc_stack = NULL;
    uint32_t *order = NULL;
    sol_function *merged = NULL;
    size_t node_count = 0;
    size_t i;
    uint64_t p;
    uint64_t last = 0;
    int ok = 0;

    if (slots == 0) {
        return 1;
    }
    if (slots > SIZE_MAX / 16u) {
        return 0;
    }
    flags = (uint8_t *)xx_mem_calloc((size_t)slots, 1);
    index = (uint32_t *)xx_mem_alloc((size_t)slots * sizeof(uint32_t));
    edge_dest = (uint64_t *)xx_mem_alloc((size_t)slots * 2u * sizeof(uint64_t));
    edge_count = (uint8_t *)xx_mem_alloc((size_t)slots);
    if (flags == NULL || index == NULL || edge_dest == NULL || edge_count == NULL) {
        goto done;
    }
    memset(edge_count, 0xFF, (size_t)slots);
    /* The instructions: lddw takes two slots in every version. */
    for (p = 0; p < slots; ++p) {
        if (code[p * SLOT] == 0x18) {
            if (p + 1u >= slots) {
                break;
            }
            flags[p] |= 1u;
            last = p;
            ++p;
            continue;
        }
        flags[p] |= 1u;
        last = p;
    }
    flags[0] |= 2u;
    for (i = 0; i < c->pc_count; ++i) {
        flags[c->by_pc[i].pc] |= 6u;
    }
    for (p = 0; p < slots; ++p) {
        uint32_t opcode;
        int32_t off;
        int32_t imm;
        uint64_t target;
        uint64_t next = p + 1u;

        if ((flags[p] & 1u) == 0) {
            continue;
        }
        opcode = code[p * SLOT];
        off = (int32_t)xxbyte_sext(xxbyte_u16le(code + p * SLOT + 2), 16);
        imm = (int32_t)xxbyte_u32le(code + p * SLOT + 4);
        target = p + 1u + (uint64_t)(int64_t)off;
#define SOL_MARK(pc)           \
    do {                       \
        if ((pc) < slots) {    \
            flags[(pc)] |= 2u; \
        }                      \
    } while (0)
        if (opcode == 0x85) {
            uint64_t callee = 0;
            int known = 0;
            if (c->version >= 3) {
                callee = (uint32_t)((uint64_t)p + (uint64_t)(int64_t)imm + 1u);
                known = (code[p * SLOT + 1] >> 4) == 1;
            } else {
                const sol_function *f = sol_find(c, (uint32_t)imm);
                if (f != NULL) {
                    callee = f->pc;
                    known = 1;
                }
            }
            if (known) {
                SOL_MARK(next);
                SOL_MARK(callee);
                edge_count[p] = 1;
                edge_dest[2u * p] = next;
            }
        } else if (opcode == 0x8D) {
            SOL_MARK(next);
            edge_count[p] = 1;
            edge_dest[2u * p] = next;
        } else if (opcode == 0x95) {
            SOL_MARK(next);
            edge_count[p] = 0;
        } else if (opcode == 0x05) {
            SOL_MARK(next);
            SOL_MARK(target);
            edge_count[p] = 1;
            edge_dest[2u * p] = target;
        } else if (((opcode & 7u) == 5u || ((opcode & 7u) == 6u && c->version >= 3)) && (opcode >> 4) != 0 && (opcode >> 4) != 8 && (opcode >> 4) != 9 &&
                   (opcode >> 4) <= 0xD && (opcode >> 4) != 0xE) {
            SOL_MARK(next);
            SOL_MARK(target);
            edge_count[p] = 2;
            edge_dest[2u * p] = next;
            edge_dest[2u * p + 1u] = target;
        }
#undef SOL_MARK
    }
    /* The blocks: starts that are instructions. */
    for (p = 0; p < slots; ++p) {
        if ((flags[p] & 3u) == 3u) {
            ++node_count;
        }
    }
    nodes = (sol_cfg_node *)xx_mem_calloc(node_count + 1u, sizeof(sol_cfg_node));
    stack = (uint32_t *)xx_mem_alloc(node_count * 2u * 3u * sizeof(uint32_t) + 8u);
    scc_stack = (uint32_t *)xx_mem_alloc(node_count * sizeof(uint32_t) + 4u);
    order = (uint32_t *)xx_mem_alloc(node_count * sizeof(uint32_t) + 4u);
    if (nodes == NULL || stack == NULL || scc_stack == NULL || order == NULL) {
        goto done;
    }
    node_count = 0;
    for (p = 0; p < slots; ++p) {
        index[p] = SOL_NONE;
        if ((flags[p] & 3u) == 3u) {
            index[p] = (uint32_t)node_count;
            nodes[node_count].pc = p;
            ++node_count;
        }
    }
    /* Destinations: the block's edge, else the fall-through unless the
     * block starts a function. */
    {
        uint64_t edge = 0;
        for (i = 0; i < node_count; ++i) {
            uint64_t end = i + 1u < node_count ? nodes[i + 1u].pc - 1u : last;
            while (edge < slots && edge_count[edge] == 0xFF) {
                ++edge;
            }
            if (edge < slots && edge <= end) {
                uint32_t k;
                for (k = 0; k < edge_count[edge]; ++k) {
                    uint64_t d = edge_dest[2u * edge + k];
                    if (d < slots && index[d] != SOL_NONE) {
                        nodes[i].dest[nodes[i].dest_count++] = index[d];
                    }
                }
                ++edge;
                continue;
            }
            if (i + 1u < node_count && (flags[nodes[i].pc] & 4u) == 0) {
                nodes[i].dest[nodes[i].dest_count++] = (uint32_t)(i + 1u);
            }
        }
    }
    /* Tarjan, iteratively and in the reference's visiting order. */
    if (node_count != 0) {
        size_t depth = 0;
        uint32_t discovered = 0;
        uint32_t scc_id = 0;
        size_t scc_depth = 0;
        size_t next_v = 1;

        for (i = 0; i < node_count; ++i) {
            nodes[i].discovery = SOL_NONE;
            nodes[i].lowlink = SOL_NONE;
            nodes[i].scc = SOL_NONE;
        }
        stack[depth++] = 0;
        stack[depth++] = 0;
        while (depth != 0) {
            uint32_t edge_index = stack[--depth];
            uint32_t v = stack[--depth];
            sol_cfg_node *node = &nodes[v];
            uint32_t j;
            int descended = 0;

            if (edge_index == 0) {
                node->discovery = discovered;
                node->lowlink = discovered;
                node->on_stack = 1;
                scc_stack[scc_depth++] = v;
                ++discovered;
            }
            for (j = edge_index; j < node->dest_count; ++j) {
                uint32_t w = node->dest[j];
                if (nodes[w].discovery == SOL_NONE) {
                    stack[depth++] = v;
                    stack[depth++] = j + 1u;
                    stack[depth++] = w;
                    stack[depth++] = 0;
                    descended = 1;
                    break;
                }
                if (nodes[w].on_stack && nodes[w].discovery < node->lowlink) {
                    node->lowlink = nodes[w].discovery;
                }
            }
            if (descended) {
                continue;
            }
            if (node->discovery == node->lowlink) {
                uint32_t in_scc = 0;
                while (scc_depth != 0) {
                    uint32_t w = scc_stack[--scc_depth];
                    nodes[w].on_stack = 0;
                    nodes[w].scc = scc_id;
                    nodes[w].discovery = in_scc++;
                    if (w == v) {
                        break;
                    }
                }
                ++scc_id;
            }
            if (depth != 0) {
                uint32_t parent = stack[depth - 2u];
                if (node->lowlink < nodes[parent].lowlink) {
                    nodes[parent].lowlink = node->lowlink;
                }
            } else {
                while (next_v < node_count && nodes[next_v].discovery != SOL_NONE) {
                    ++next_v;
                }
                if (next_v < node_count) {
                    stack[depth++] = (uint32_t)next_v;
                    stack[depth++] = 0;
                    ++next_v;
                }
            }
        }
    }
    /* Outside predecessors per component. */
    for (i = 0; i < node_count; ++i) {
        uint32_t k;
        for (k = 0; k < nodes[i].dest_count; ++k) {
            uint32_t w = nodes[i].dest[k];
            if (nodes[w].scc != nodes[i].scc) {
                nodes[w].external = 1;
            }
        }
    }
    /* A component's first block in the topological order is its root, the
     * block with the highest discovery index; components without outside
     * predecessors start functions. */
    {
        size_t count = 0;
        uint32_t scc;
        uint32_t scc_count = 0;
        for (i = 0; i < node_count; ++i) {
            if (nodes[i].scc + 1u > scc_count) {
                scc_count = nodes[i].scc + 1u;
            }
        }
        /* order[scc] = root, then whether any member has an outside source. */
        for (scc = 0; scc < scc_count; ++scc) {
            order[scc] = SOL_NONE;
        }
        for (i = 0; i < node_count; ++i) {
            uint32_t *root = &order[nodes[i].scc];
            if (*root == SOL_NONE || nodes[i].discovery > nodes[*root].discovery) {
                *root = (uint32_t)i;
            }
        }
        for (i = 0; i < node_count; ++i) {
            if (nodes[i].external) {
                nodes[order[nodes[i].scc]].on_stack = 2; /* component has an outside source */
            }
        }
        merged = (sol_function *)xx_mem_alloc((c->pc_count + scc_count + 1u) * sizeof(sol_function));
        if (merged == NULL) {
            goto done;
        }
        for (i = 0; i < c->pc_count; ++i) {
            uint64_t pc = c->by_pc[i].pc;
            if (index[pc] != SOL_NONE) {
                merged[count++] = c->by_pc[i];
            } else {
                flags[pc] &= (uint8_t)~4u;
            }
        }
        for (scc = 0; scc < scc_count; ++scc) {
            uint32_t root = order[scc];
            if (root != SOL_NONE && nodes[root].on_stack != 2 && (flags[nodes[root].pc] & 4u) == 0) {
                memset(&merged[count], 0, sizeof(merged[count]));
                merged[count].pc = nodes[root].pc;
                merged[count].key = SOL_NONE;
                merged[count].used = 1;
                merged[count].name = NULL;
                flags[nodes[root].pc] |= 4u;
                ++count;
            }
        }
        if (count > 1) {
            qsort(merged, count, sizeof(sol_function), sol_function_compare);
        }
        xx_mem_free(c->by_pc);
        c->by_pc = merged;
        c->pc_count = count;
        merged = NULL;
    }
    ok = 1;
done:
    if (ok) {
        c->starts = flags;
        flags = NULL;
    }
    xx_mem_free(flags);
    xx_mem_free(index);
    xx_mem_free(edge_dest);
    xx_mem_free(edge_count);
    xx_mem_free(nodes);
    xx_mem_free(stack);
    xx_mem_free(scc_stack);
    xx_mem_free(order);
    xx_mem_free(merged);
    return ok;
}

static void sol_free(sol_ctx *c)
{
    xx_mem_free(c->table);
    xx_mem_free(c->by_pc);
    xx_mem_free(c->syscalls);
    xx_mem_free(c->starts);
}

static xxbyte_status sol_list(const elf_file *e, uint32_t mode, uint32_t listing_flags, xx_buf_t *out)
{
    sol_ctx c;
    elf_shdr text;
    uint64_t text_offset = 0;
    uint64_t text_size = 0;
    const char *section = ".text";
    uint8_t *code = NULL;
    xxbyte_status status = XXBYTE_STATUS_OK;
    uint64_t at = 0;
    size_t i;
    int ok;

    memset(&c, 0, sizeof(c));
    memset(&text, 0, sizeof(text));
    c.version = XXBYTE_EBPF_MODE_SBPF_VERSION(mode);
    if (c.version >= 3) {
        /* The bytecode segment: the first PT_LOAD unless that one is the
         * read-only data (PF_R), then the second. */
        elf_phdr p;
        uint32_t which = elf_segment(e, 0, &p) && p.flags == ELF_PF_R ? 1u : 0u;
        if (elf_segment(e, which, &p) && p.type == ELF_PT_LOAD && elf_has(e, p.offset, p.filesz)) {
            text_offset = p.offset;
            text_size = p.filesz;
            text.addr = p.vaddr;
            section = "bytecode";
        } else if (elf_find_section(e, ".text", &text) == 0 || elf_section_data(e, &text) == NULL) {
            return obj_list(e, mode, listing_flags, out);
        } else {
            text_offset = text.offset;
            text_size = text.size;
        }
    } else {
        if (elf_find_section(e, ".text", &text) == 0 || elf_section_data(e, &text) == NULL) {
            return obj_list(e, mode, listing_flags, out);
        }
        text_offset = text.offset;
        text_size = text.size;
    }
    c.slots = text_size / SLOT;
    c.super_root = text_size / SLOT + (text_size % SLOT != 0);
    if (text_size != 0) {
        code = (uint8_t *)xx_mem_alloc((size_t)text_size);
        if (code == NULL) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        memcpy(code, e->data + text_offset, (size_t)text_size);
    }
    text.offset = text_offset;
    text.size = text_size;
    ok = c.version >= 3 ? sol_debug_labels(&c, e, text.addr) : sol_relocate(&c, e, &text, code);
    if (!ok || !sol_index_functions(&c) || !sol_analyze(&c, code)) {
        xx_mem_free(code);
        sol_free(&c);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (c.version <= 2) {
        c.context.user = &c;
        c.context.function_address = sol_function_address;
    }
    if (!xx_buf_appendf(out, "; %s at file offset 0x%llx, size 0x%llx, vaddr 0x%llx, entrypoint 0x%llx\n", section, (unsigned long long)text_offset,
                        (unsigned long long)text_size, (unsigned long long)text.addr, (unsigned long long)e->entry)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    i = 0;
    while (status == XXBYTE_STATUS_OK && at < text_size) {
        uint64_t next;
        char name[512];
        const char *shown = section;

        if (i < c.pc_count && c.by_pc[i].pc * SLOT == at) {
            /* The block label, as the crate prints it. */
            name_out o;
            o.buffer = name;
            o.size = sizeof(name);
            o.length = 0;
            sol_label_name(&c.by_pc[i], &o);
            name_done(&o);
            shown = name;
            ++i;
        }
        next = i < c.pc_count ? c.by_pc[i].pc * SLOT : text_size;
        if (!list_header(out, shown, section, at, next)) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_EBPF, mode, &c.context, code + at, (size_t)(next - at), at, listing_flags, sol_resolver, &c, out);
        at = next;
    }
    xx_mem_free(code);
    sol_free(&c);
    return status;
}

xxbyte_status xxbyte_ebpf_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    elf_file e;
    xxbyte_status status = elf_open(data, size, &e);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (mode == XXBYTE_MODE_AUTO) {
        status = xxbyte_ebpf_elf_mode(data, size, &mode);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    if (!list_file_header(out, &e, mode)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (XXBYTE_EBPF_MODE_IS_SBPF(mode) && e.type != ELF_ET_REL) {
        return sol_list(&e, mode, listing_flags, out);
    }
    return obj_list(&e, mode, listing_flags, out);
}

#else
typedef int xxbyte_ebpf_elf_disabled;
#endif
