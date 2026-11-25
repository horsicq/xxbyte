#include "xxbyte_java_internal.h"

#if XXBYTE_USE_JAVA

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

/* Container listings: class files and JAR/ZIP/JMOD archives. */

#define ZIP_ENTRY_LIMIT (64u * 1024u * 1024u)

static const uint32_t crc_table[256] = {
    0x00000000u, 0x77073096u, 0xEE0E612Cu, 0x990951BAu, 0x076DC419u, 0x706AF48Fu, 0xE963A535u, 0x9E6495A3u, 0x0EDB8832u, 0x79DCB8A4u, 0xE0D5E91Eu, 0x97D2D988u,
    0x09B64C2Bu, 0x7EB17CBDu, 0xE7B82D07u, 0x90BF1D91u, 0x1DB71064u, 0x6AB020F2u, 0xF3B97148u, 0x84BE41DEu, 0x1ADAD47Du, 0x6DDDE4EBu, 0xF4D4B551u, 0x83D385C7u,
    0x136C9856u, 0x646BA8C0u, 0xFD62F97Au, 0x8A65C9ECu, 0x14015C4Fu, 0x63066CD9u, 0xFA0F3D63u, 0x8D080DF5u, 0x3B6E20C8u, 0x4C69105Eu, 0xD56041E4u, 0xA2677172u,
    0x3C03E4D1u, 0x4B04D447u, 0xD20D85FDu, 0xA50AB56Bu, 0x35B5A8FAu, 0x42B2986Cu, 0xDBBBC9D6u, 0xACBCF940u, 0x32D86CE3u, 0x45DF5C75u, 0xDCD60DCFu, 0xABD13D59u,
    0x26D930ACu, 0x51DE003Au, 0xC8D75180u, 0xBFD06116u, 0x21B4F4B5u, 0x56B3C423u, 0xCFBA9599u, 0xB8BDA50Fu, 0x2802B89Eu, 0x5F058808u, 0xC60CD9B2u, 0xB10BE924u,
    0x2F6F7C87u, 0x58684C11u, 0xC1611DABu, 0xB6662D3Du, 0x76DC4190u, 0x01DB7106u, 0x98D220BCu, 0xEFD5102Au, 0x71B18589u, 0x06B6B51Fu, 0x9FBFE4A5u, 0xE8B8D433u,
    0x7807C9A2u, 0x0F00F934u, 0x9609A88Eu, 0xE10E9818u, 0x7F6A0DBBu, 0x086D3D2Du, 0x91646C97u, 0xE6635C01u, 0x6B6B51F4u, 0x1C6C6162u, 0x856530D8u, 0xF262004Eu,
    0x6C0695EDu, 0x1B01A57Bu, 0x8208F4C1u, 0xF50FC457u, 0x65B0D9C6u, 0x12B7E950u, 0x8BBEB8EAu, 0xFCB9887Cu, 0x62DD1DDFu, 0x15DA2D49u, 0x8CD37CF3u, 0xFBD44C65u,
    0x4DB26158u, 0x3AB551CEu, 0xA3BC0074u, 0xD4BB30E2u, 0x4ADFA541u, 0x3DD895D7u, 0xA4D1C46Du, 0xD3D6F4FBu, 0x4369E96Au, 0x346ED9FCu, 0xAD678846u, 0xDA60B8D0u,
    0x44042D73u, 0x33031DE5u, 0xAA0A4C5Fu, 0xDD0D7CC9u, 0x5005713Cu, 0x270241AAu, 0xBE0B1010u, 0xC90C2086u, 0x5768B525u, 0x206F85B3u, 0xB966D409u, 0xCE61E49Fu,
    0x5EDEF90Eu, 0x29D9C998u, 0xB0D09822u, 0xC7D7A8B4u, 0x59B33D17u, 0x2EB40D81u, 0xB7BD5C3Bu, 0xC0BA6CADu, 0xEDB88320u, 0x9ABFB3B6u, 0x03B6E20Cu, 0x74B1D29Au,
    0xEAD54739u, 0x9DD277AFu, 0x04DB2615u, 0x73DC1683u, 0xE3630B12u, 0x94643B84u, 0x0D6D6A3Eu, 0x7A6A5AA8u, 0xE40ECF0Bu, 0x9309FF9Du, 0x0A00AE27u, 0x7D079EB1u,
    0xF00F9344u, 0x8708A3D2u, 0x1E01F268u, 0x6906C2FEu, 0xF762575Du, 0x806567CBu, 0x196C3671u, 0x6E6B06E7u, 0xFED41B76u, 0x89D32BE0u, 0x10DA7A5Au, 0x67DD4ACCu,
    0xF9B9DF6Fu, 0x8EBEEFF9u, 0x17B7BE43u, 0x60B08ED5u, 0xD6D6A3E8u, 0xA1D1937Eu, 0x38D8C2C4u, 0x4FDFF252u, 0xD1BB67F1u, 0xA6BC5767u, 0x3FB506DDu, 0x48B2364Bu,
    0xD80D2BDAu, 0xAF0A1B4Cu, 0x36034AF6u, 0x41047A60u, 0xDF60EFC3u, 0xA867DF55u, 0x316E8EEFu, 0x4669BE79u, 0xCB61B38Cu, 0xBC66831Au, 0x256FD2A0u, 0x5268E236u,
    0xCC0C7795u, 0xBB0B4703u, 0x220216B9u, 0x5505262Fu, 0xC5BA3BBEu, 0xB2BD0B28u, 0x2BB45A92u, 0x5CB36A04u, 0xC2D7FFA7u, 0xB5D0CF31u, 0x2CD99E8Bu, 0x5BDEAE1Du,
    0x9B64C2B0u, 0xEC63F226u, 0x756AA39Cu, 0x026D930Au, 0x9C0906A9u, 0xEB0E363Fu, 0x72076785u, 0x05005713u, 0x95BF4A82u, 0xE2B87A14u, 0x7BB12BAEu, 0x0CB61B38u,
    0x92D28E9Bu, 0xE5D5BE0Du, 0x7CDCEFB7u, 0x0BDBDF21u, 0x86D3D2D4u, 0xF1D4E242u, 0x68DDB3F8u, 0x1FDA836Eu, 0x81BE16CDu, 0xF6B9265Bu, 0x6FB077E1u, 0x18B74777u,
    0x88085AE6u, 0xFF0F6A70u, 0x66063BCAu, 0x11010B5Cu, 0x8F659EFFu, 0xF862AE69u, 0x616BFFD3u, 0x166CCF45u, 0xA00AE278u, 0xD70DD2EEu, 0x4E048354u, 0x3903B3C2u,
    0xA7672661u, 0xD06016F7u, 0x4969474Du, 0x3E6E77DBu, 0xAED16A4Au, 0xD9D65ADCu, 0x40DF0B66u, 0x37D83BF0u, 0xA9BCAE53u, 0xDEBB9EC5u, 0x47B2CF7Fu, 0x30B5FFE9u,
    0xBDBDF21Cu, 0xCABAC28Au, 0x53B39330u, 0x24B4A3A6u, 0xBAD03605u, 0xCDD70693u, 0x54DE5729u, 0x23D967BFu, 0xB3667A2Eu, 0xC4614AB8u, 0x5D681B02u, 0x2A6F2B94u,
    0xB40BBE37u, 0xC30C8EA1u, 0x5A05DF1Bu, 0x2D02EF8Du,
};

static uint32_t crc32_of(const uint8_t *data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;

    for (i = 0; i < size; ++i) {
        crc = crc_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

static void strip_newline(xx_buf_t *out)
{
    if (out->size != 0 && out->data != NULL && out->data[out->size - 1] == '\n') {
        out->data[--out->size] = '\0';
    }
}

static int has_constant_operand(const xxbyte_instruction *insn, uint32_t *index)
{
    uint32_t i;

    for (i = 0; i < insn->operand_count; ++i) {
        const xxbyte_operand *op = &insn->operands[i];
        if (op->type == XXBYTE_OPERAND_INDEX && op->kind == XXBYTE_INDEX_CONSTANT && (op->flags & XXBYTE_OPERAND_FLAG_IMPLICIT) == 0) {
            *index = (uint32_t)op->value;
            return 1;
        }
    }
    return 0;
}

/* The code of one method, one instruction per line, with the javap comment
 * of the constant-pool operand at the end of the line. */
static xxbyte_status list_code(const xxbyte_java_class *c, const uint8_t *code, uint32_t length, uint32_t listing_flags, xx_buf_t *out)
{
    uint32_t offset = 0;

    while (offset < length) {
        xxbyte_instruction insn;
        uint32_t n = 1;
        uint32_t index = 0;
        int comment = 0;
        xxbyte_status status;

        if (xxbyte_decode(XXBYTE_FAMILY_JAVA, XXBYTE_JAVA_MODE_DEFAULT, code + offset, length - offset, offset, &insn) == XXBYTE_STATUS_OK) {
            n = insn.size;
            comment = has_constant_operand(&insn, &index);
        }
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_JAVA, XXBYTE_JAVA_MODE_DEFAULT, NULL, code + offset, n, offset, listing_flags, NULL, NULL, out);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (comment) {
            size_t mark;
            strip_newline(out);
            mark = out->size;
            if (!xx_buf_append_str(out, "  // ")) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            if (!xxbyte_java_append_constant(out, c, index)) {
                if (!xx_buf_ok(out)) {
                    return XXBYTE_STATUS_OUT_OF_MEMORY;
                }
                out->size = mark;
                out->data[mark] = '\0';
            }
            /* javap drops the spaces at the end of a line ("String a  "
             * prints as "String a"). */
            while (out->size > mark && out->data[out->size - 1] == ' ') {
                out->data[--out->size] = '\0';
            }
            if (!xx_buf_append_char(out, '\n')) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
        offset += n;
    }
    return XXBYTE_STATUS_OK;
}

static int append_safe(xx_buf_t *out, const char *text, size_t length)
{
    size_t i;

    for (i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)text[i];
        if (!xx_buf_append_char(out, ch < 0x20u || ch == 0x7Fu ? '?' : (char)ch)) {
            return 0;
        }
    }
    return 1;
}

static int append_dotted_name(xx_buf_t *out, const char *name)
{
    size_t i;
    size_t length = strlen(name);

    for (i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)name[i];
        if (!xx_buf_append_char(out, ch == '/' ? '.' : (ch < 0x20u || ch == 0x7Fu ? '?' : (char)ch))) {
            return 0;
        }
    }
    return 1;
}

static xxbyte_status list_exception_table(const xxbyte_java_class *c, const xxbyte_java_member *m, xx_buf_t *out)
{
    const uint8_t *p;
    uint32_t i;

    if (m->code.exception_table_length == 0) {
        return XXBYTE_STATUS_OK;
    }
    if (!xx_buf_append_str(out, "  Exception table:\n     from    to  target type\n")) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    p = xxbyte_java_class_data(c, NULL) + m->code.exception_table_offset;
    for (i = 0; i < m->code.exception_table_length; ++i, p += 8) {
        uint16_t type = xxbyte_u16be(p + 6);
        int ok = xx_buf_appendf(out, "    %5u %5u %5u   ", (unsigned)xxbyte_u16be(p), (unsigned)xxbyte_u16be(p + 2), (unsigned)xxbyte_u16be(p + 4));

        if (ok && type == 0) {
            ok = xx_buf_append_str(out, "any");
        } else if (ok) {
            ok = xx_buf_append_str(out, "Class ") && (xxbyte_java_append_class_name(out, c, type) || xx_buf_appendf(out, "#%u", (unsigned)type));
        }
        if (!ok || !xx_buf_append_char(out, '\n')) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status list_class(const xxbyte_java_class *c, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_java_class_info info;
    const uint8_t *data = xxbyte_java_class_data(c, NULL);
    uint32_t i;

    xxbyte_java_class_get_info(c, &info);
    if (!xx_buf_append_str(out, "// class ") || !append_dotted_name(out, info.this_name != NULL ? info.this_name : "?") ||
        !xx_buf_appendf(out, ", version %u.%u\n", (unsigned)info.major_version, (unsigned)info.minor_version)) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    for (i = 0; i < info.methods_count; ++i) {
        xxbyte_java_member m;
        xxbyte_status status;

        xxbyte_java_class_method(c, i, &m);
        if (!xx_buf_append_char(out, '\n') || !xxbyte_java_append_header(out, c, &m) || !xx_buf_append_char(out, '\n')) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        if (!m.has_code) {
            continue;
        }
        if (!xx_buf_appendf(out, "  // stack=%u, locals=%u, code_length=%u\n", (unsigned)m.code.max_stack, (unsigned)m.code.max_locals, (unsigned)m.code.code_length)) {
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        status = list_code(c, data + m.code.code_offset, m.code.code_length, listing_flags, out);
        if (status == XXBYTE_STATUS_OK) {
            status = list_exception_table(c, &m, out);
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    return XXBYTE_STATUS_OK;
}

/* ---- ZIP ---- */

#define ZIP_LOCAL 0x04034B50u
#define ZIP_CENTRAL 0x02014B50u
#define ZIP_END 0x06054B50u
#define ZIP64_LOCATOR 0x07064B50u

static int find_end(const uint8_t *data, size_t size, size_t *end)
{
    size_t pos;
    size_t lowest;

    if (size < 22) {
        return 0;
    }
    lowest = size - 22 > 65535u ? size - 22 - 65535u : 0;
    pos = size - 22;
    for (;;) {
        if (xxbyte_u32le(data + pos) == ZIP_END && pos + 22u + xxbyte_u16le(data + pos + 20) <= size) {
            *end = pos;
            return 1;
        }
        if (pos == lowest) {
            return 0;
        }
        --pos;
    }
}

static int skipped(xx_buf_t *out, const char *name, size_t name_length, const char *reason, uint16_t method)
{
    if (!xx_buf_append_str(out, "// entry ") || !append_safe(out, name, name_length) || !xx_buf_appendf(out, ": skipped (%s", reason)) {
        return 0;
    }
    if (strcmp(reason, "compression method") == 0 && !xx_buf_appendf(out, " %u", (unsigned)method)) {
        return 0;
    }
    return xx_buf_append_str(out, ")\n");
}

/* One central-directory entry whose name ends in ".class". */
static xxbyte_status list_entry(const uint8_t *data, size_t size, size_t base, const uint8_t *cd, uint32_t listing_flags, xx_buf_t *out, int *first)
{
    uint16_t flags = xxbyte_u16le(cd + 8);
    uint16_t method = xxbyte_u16le(cd + 10);
    uint32_t crc = xxbyte_u32le(cd + 16);
    uint32_t csize = xxbyte_u32le(cd + 20);
    uint32_t usize = xxbyte_u32le(cd + 24);
    uint16_t name_length = xxbyte_u16le(cd + 28);
    uint32_t local = xxbyte_u32le(cd + 42);
    const char *name = (const char *)(cd + 46);
    const char *reason = NULL;
    uint8_t *buffer = NULL;
    const uint8_t *content = NULL;
    uint64_t start;
    xxbyte_java_class *c = NULL;
    xxbyte_status status;

    if (!*first && !xx_buf_append_char(out, '\n')) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    *first = 0;
    if ((flags & 1u) != 0) {
        reason = "encrypted";
    } else if (csize == 0xFFFFFFFFu || usize == 0xFFFFFFFFu || local == 0xFFFFFFFFu) {
        reason = "ZIP64";
    } else if (method != 0 && method != 8) {
        reason = "compression method";
    } else if (usize > ZIP_ENTRY_LIMIT) {
        reason = "larger than 64 MiB";
    } else if (method == 0 && csize != usize) {
        reason = "bad stored size";
    }
    if (reason == NULL) {
        start = (uint64_t)base + local;
        if (start + 30u > size || xxbyte_u32le(data + start) != ZIP_LOCAL) {
            reason = "bad local header";
        } else {
            start += 30u + (uint64_t)xxbyte_u16le(data + start + 26) + xxbyte_u16le(data + start + 28);
            if (start + csize > size) {
                reason = "truncated";
            }
        }
    }
    if (reason == NULL) {
        if (method == 0) {
            content = data + start;
        } else {
            size_t got = 0;
            buffer = (uint8_t *)xx_mem_alloc(usize != 0 ? usize : 1u);
            if (buffer == NULL) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            status = xxbyte_inflate(data + start, csize, 0, buffer, usize, &got, NULL);
            if (status == XXBYTE_STATUS_OUT_OF_MEMORY || (status == XXBYTE_STATUS_OK && got != usize)) {
                reason = "bad size";
            } else if (status != XXBYTE_STATUS_OK) {
                reason = "bad deflate data";
            }
            content = buffer;
        }
    }
    if (reason == NULL && crc32_of(content, usize) != crc) {
        reason = "bad CRC-32";
    }
    if (reason == NULL) {
        status = xxbyte_java_class_open(content, usize, &c);
        if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
            xx_mem_free(buffer);
            return status;
        }
        if (status != XXBYTE_STATUS_OK) {
            reason = status == XXBYTE_STATUS_TRUNCATED ? "truncated class file" : "not a valid class file";
        }
    }
    if (reason != NULL) {
        xx_mem_free(buffer);
        return skipped(out, name, name_length, reason, method) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = XXBYTE_STATUS_OK;
    if (!xx_buf_append_str(out, "// entry ") || !append_safe(out, name, name_length) || !xx_buf_append_str(out, "\n\n")) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (status == XXBYTE_STATUS_OK) {
        status = list_class(c, listing_flags, out);
    }
    xxbyte_java_class_close(c);
    xx_mem_free(buffer);
    return status;
}

static int ends_with_class(const uint8_t *name, size_t length)
{
    return length >= 6 && memcmp(name + length - 6, ".class", 6) == 0;
}

static xxbyte_status list_zip(const uint8_t *data, size_t size, size_t end, uint32_t listing_flags, xx_buf_t *out)
{
    uint16_t disk = xxbyte_u16le(data + end + 4);
    uint16_t cd_disk = xxbyte_u16le(data + end + 6);
    uint16_t disk_entries = xxbyte_u16le(data + end + 8);
    uint16_t entries = xxbyte_u16le(data + end + 10);
    uint32_t cd_size = xxbyte_u32le(data + end + 12);
    uint32_t cd_offset = xxbyte_u32le(data + end + 16);
    size_t base;
    size_t pos;
    uint32_t i;
    int first = 1;

    if (entries == 0xFFFFu || disk_entries == 0xFFFFu || cd_size == 0xFFFFFFFFu || cd_offset == 0xFFFFFFFFu ||
        (end >= 20 && xxbyte_u32le(data + end - 20) == ZIP64_LOCATOR)) {
        return xx_buf_append_str(out, "// archive skipped (ZIP64)\n") ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (disk != 0 || cd_disk != 0 || disk_entries != entries) {
        return xx_buf_append_str(out, "// archive skipped (multi-disk)\n") ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if ((uint64_t)cd_offset + cd_size > end) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    /* Bytes in front of the archive (a JMOD header, a stub) shift every
     * offset by the same amount. */
    base = end - ((size_t)cd_offset + cd_size);
    pos = base + cd_offset;
    for (i = 0; i < entries; ++i) {
        uint64_t record;
        uint16_t name_length;

        if (pos + 46u > end || xxbyte_u32le(data + pos) != ZIP_CENTRAL) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        name_length = xxbyte_u16le(data + pos + 28);
        record = 46u + (uint64_t)name_length + xxbyte_u16le(data + pos + 30) + xxbyte_u16le(data + pos + 32);
        if (pos + record > end) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (ends_with_class(data + pos + 46, name_length)) {
            xxbyte_status status = list_entry(data, size, base, data + pos, listing_flags, out, &first);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
        }
        pos += (size_t)record;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_java_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    size_t end;

    (void)mode;
    if (size >= 4 && xxbyte_u32be(data) == 0xCAFEBABEu) {
        xxbyte_java_class *c;
        xxbyte_status status = xxbyte_java_class_open(data, size, &c);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        status = list_class(c, listing_flags, out);
        xxbyte_java_class_close(c);
        return status;
    }
    if (find_end(data, size, &end)) {
        return list_zip(data, size, end, listing_flags, out);
    }
    if (size < 4 && size != 0 && memcmp(data, "\xCA\xFE\xBA\xBE", size) == 0) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

#else
typedef int xxbyte_java_list_disabled;
#endif
