#ifndef XXBYTE_VB_PE_H
#define XXBYTE_VB_PE_H

/*
 * A bounds-checked PE/COFF image reader shared by the Visual Basic
 * (src/xxbyte_vb_file.c) and EFI Byte Code (src/xxbyte_ebc_file.c) container
 * listings. Everything is static inline so that a build with only one of the
 * two families compiles no unused code. Every read is checked against the
 * file; nothing here trusts a header field.
 */

#include "xxbyte_internal.h"

#define XXPE_MAX_SECTIONS 96u
#define XXPE_DIR_EXPORT 0u
#define XXPE_DIR_IMPORT 1u
#define XXPE_DIR_BASERELOC 5u
#define XXPE_SCN_CNT_CODE 0x00000020u
#define XXPE_SCN_CNT_INITIALIZED_DATA 0x00000040u
#define XXPE_SCN_CNT_UNINITIALIZED_DATA 0x00000080u
#define XXPE_SCN_MEM_EXECUTE 0x20000000u

typedef struct xxpe_section {
    char name[9];
    uint32_t rva;
    uint32_t virtual_size;
    uint32_t raw_offset;
    uint32_t raw_size; /* clipped to the file */
    uint32_t characteristics;
} xxpe_section;

typedef struct xxpe_image {
    const uint8_t *data;
    size_t size;
    uint16_t machine;
    uint16_t characteristics;
    uint16_t magic; /* 0x10B PE32, 0x20B PE32+ */
    uint16_t subsystem;
    uint32_t entry_rva;
    uint64_t image_base;
    uint32_t size_of_image;
    uint32_t size_of_headers; /* clipped to the file */
    uint32_t dir_count;
    uint32_t dir_rva[16];
    uint32_t dir_size[16];
    uint32_t section_count;
    xxpe_section sections[XXPE_MAX_SECTIONS];
} xxpe_image;

static inline int xxpe_range(size_t size, uint64_t offset, uint64_t length)
{
    return offset <= size && length <= size - offset;
}

/* Parses the DOS, COFF and optional headers and the section table.
 * Returns XXBYTE_STATUS_INVALID_INSTRUCTION when data is not a PE image and
 * XXBYTE_STATUS_TRUNCATED when its headers run past the end. */
static inline xxbyte_status xxpe_parse(xxpe_image *pe, const uint8_t *data, size_t size)
{
    uint32_t e_lfanew;
    uint32_t opt;
    uint32_t opt_size;
    uint32_t sec;
    uint32_t i;
    uint32_t dirs_at;

    memset(pe, 0, sizeof(*pe));
    pe->data = data;
    pe->size = size;
    if (size < 0x40u) {
        return size >= 2u && (data[0] != 'M' || data[1] != 'Z') ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_TRUNCATED;
    }
    if (data[0] != 'M' || data[1] != 'Z') {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    e_lfanew = xxbyte_u32le(data + 0x3C);
    if (!xxpe_range(size, e_lfanew, 24u)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (memcmp(data + e_lfanew, "PE\0\0", 4) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    pe->machine = xxbyte_u16le(data + e_lfanew + 4u);
    pe->section_count = xxbyte_u16le(data + e_lfanew + 6u);
    opt_size = xxbyte_u16le(data + e_lfanew + 20u);
    pe->characteristics = xxbyte_u16le(data + e_lfanew + 22u);
    opt = e_lfanew + 24u;
    if (pe->section_count > XXPE_MAX_SECTIONS || opt_size < 2u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (!xxpe_range(size, opt, opt_size)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    pe->magic = xxbyte_u16le(data + opt);
    if (pe->magic == 0x10Bu) {
        if (opt_size < 96u) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        pe->image_base = xxbyte_u32le(data + opt + 28u);
        dirs_at = 96u;
    } else if (pe->magic == 0x20Bu) {
        if (opt_size < 112u) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        pe->image_base = xxbyte_u64le(data + opt + 24u);
        dirs_at = 112u;
    } else {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    pe->entry_rva = xxbyte_u32le(data + opt + 16u);
    pe->size_of_image = xxbyte_u32le(data + opt + 56u);
    pe->size_of_headers = xxbyte_u32le(data + opt + 60u);
    if (pe->size_of_headers > size) {
        pe->size_of_headers = (uint32_t)size;
    }
    pe->subsystem = xxbyte_u16le(data + opt + 68u);
    pe->dir_count = xxbyte_u32le(data + opt + dirs_at - 4u);
    if (pe->dir_count > 16u) {
        pe->dir_count = 16u;
    }
    if ((uint64_t)dirs_at + 8u * pe->dir_count > opt_size) {
        pe->dir_count = (opt_size - dirs_at) / 8u;
    }
    for (i = 0; i < pe->dir_count; ++i) {
        pe->dir_rva[i] = xxbyte_u32le(data + opt + dirs_at + 8u * i);
        pe->dir_size[i] = xxbyte_u32le(data + opt + dirs_at + 8u * i + 4u);
    }
    sec = opt + opt_size;
    if (!xxpe_range(size, sec, 40u * (uint64_t)pe->section_count)) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    for (i = 0; i < pe->section_count; ++i) {
        const uint8_t *p = data + sec + 40u * i;
        xxpe_section *s = &pe->sections[i];
        uint32_t j;

        for (j = 0; j < 8u; ++j) {
            char c = (char)p[j];
            s->name[j] = (c >= 0x20 && c < 0x7F) ? c : (c == 0 ? '\0' : '?');
        }
        s->name[8] = '\0';
        s->virtual_size = xxbyte_u32le(p + 8);
        s->rva = xxbyte_u32le(p + 12);
        s->raw_size = xxbyte_u32le(p + 16);
        s->raw_offset = xxbyte_u32le(p + 20);
        s->characteristics = xxbyte_u32le(p + 36);
        if (s->raw_offset >= size) {
            s->raw_size = 0;
        } else if (s->raw_size > size - s->raw_offset) {
            s->raw_size = (uint32_t)(size - s->raw_offset);
        }
    }
    return XXBYTE_STATUS_OK;
}

/* The index of the section whose virtual extent holds rva, or -1. */
static inline int xxpe_section_of(const xxpe_image *pe, uint32_t rva)
{
    uint32_t i;

    for (i = 0; i < pe->section_count; ++i) {
        const xxpe_section *s = &pe->sections[i];
        uint32_t extent = s->virtual_size != 0 ? s->virtual_size : s->raw_size;

        if (rva >= s->rva && rva - s->rva < extent) {
            return (int)i;
        }
    }
    return -1;
}

/*
 * The file offset of length bytes at rva when all of them are stored in the
 * file (in the headers or in one section's raw data); 0 with *ok cleared
 * otherwise.
 */
static inline size_t xxpe_offset(const xxpe_image *pe, uint32_t rva, uint32_t length, int *ok)
{
    int i = xxpe_section_of(pe, rva);

    *ok = 0;
    if (i >= 0) {
        const xxpe_section *s = &pe->sections[i];
        uint32_t delta = rva - s->rva;

        if (delta <= s->raw_size && length <= s->raw_size - delta) {
            *ok = 1;
            return (size_t)s->raw_offset + delta;
        }
        return 0;
    }
    if ((uint64_t)rva + length <= pe->size_of_headers) {
        *ok = 1;
        return rva;
    }
    return 0;
}

/* A pointer to length readable bytes at rva, or NULL. */
static inline const uint8_t *xxpe_at(const xxpe_image *pe, uint32_t rva, uint32_t length)
{
    int ok;
    size_t offset = xxpe_offset(pe, rva, length, &ok);

    return ok ? pe->data + offset : NULL;
}

/* Converts a 32-bit virtual address to an RVA; 0 with *ok cleared when it
 * lies below the image base or past 4 GB of it. */
static inline uint32_t xxpe_rva_of(const xxpe_image *pe, uint64_t va, int *ok)
{
    *ok = va >= pe->image_base && va - pe->image_base <= 0xFFFFFFFFu;
    return *ok ? (uint32_t)(va - pe->image_base) : 0u;
}

static inline const uint8_t *xxpe_at_va(const xxpe_image *pe, uint64_t va, uint32_t length)
{
    int ok;
    uint32_t rva = xxpe_rva_of(pe, va, &ok);

    return ok ? xxpe_at(pe, rva, length) : NULL;
}

/* Reads a u32 at a virtual address; returns 0 and clears *ok when it is not
 * in the file. */
static inline uint32_t xxpe_u32_va(const xxpe_image *pe, uint64_t va, int *ok)
{
    const uint8_t *p = xxpe_at_va(pe, va, 4u);

    *ok = p != NULL;
    return p != NULL ? xxbyte_u32le(p) : 0u;
}

static inline uint16_t xxpe_u16_va(const xxpe_image *pe, uint64_t va, int *ok)
{
    const uint8_t *p = xxpe_at_va(pe, va, 2u);

    *ok = p != NULL;
    return p != NULL ? xxbyte_u16le(p) : 0u;
}

/*
 * The length of the NUL-terminated string at rva, at most max bytes, when
 * its terminator lies in the file before that limit; -1 otherwise.
 */
static inline long xxpe_strlen(const xxpe_image *pe, uint32_t rva, uint32_t max)
{
    int ok;
    size_t offset = xxpe_offset(pe, rva, 1u, &ok);
    int i;
    size_t limit;
    size_t n;

    if (!ok) {
        return -1;
    }
    /* The string may not leave its section either. */
    i = xxpe_section_of(pe, rva);
    if (i >= 0) {
        const xxpe_section *s = &pe->sections[i];
        limit = (size_t)s->raw_offset + s->raw_size;
    } else {
        limit = pe->size_of_headers;
    }
    for (n = 0; offset + n < limit && n <= max; ++n) {
        if (pe->data[offset + n] == 0) {
            return (long)n;
        }
    }
    return -1;
}

/* A NUL-terminated string at rva, or NULL (see xxpe_strlen). */
static inline const char *xxpe_str(const xxpe_image *pe, uint32_t rva, uint32_t max)
{
    int ok;
    size_t offset;

    if (xxpe_strlen(pe, rva, max) < 0) {
        return NULL;
    }
    offset = xxpe_offset(pe, rva, 1u, &ok);
    return ok ? (const char *)pe->data + offset : NULL;
}

#endif
