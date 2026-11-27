/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_internal.h"

#if XXBYTE_USE_EVM

#include "xxbyte_evm_internal.h"
#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#define OP_JUMP 0x56u
#define OP_JUMPI 0x57u
#define OP_JUMPDEST 0x5Bu

/* The metadata is listed this many bytes per line, all of them shown in
 * the bytes column. */
#define DATA_LINE 8u
#define BYTES_COLUMN 25u

static int is_space(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

static int hex_value(uint8_t c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

xxbyte_status xxbyte_evm_parse_hex(const uint8_t *text, size_t size, uint8_t *code, size_t capacity, size_t *code_size)
{
    size_t i = 0;
    size_t digits = 0;
    size_t n = 0;
    int high = -1;

    if (code_size != NULL) {
        *code_size = 0;
    }
    if ((text == NULL && size != 0) || (code == NULL && capacity != 0) || code_size == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    while (i < size && is_space(text[i])) {
        ++i;
    }
    if (size - i >= 2 && text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
        i += 2;
    }
    for (; i < size; ++i) {
        int v;
        if (is_space(text[i])) {
            continue;
        }
        v = hex_value(text[i]);
        if (v < 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        ++digits;
        if (high < 0) {
            high = v;
            continue;
        }
        if (code != NULL) {
            if (n >= capacity) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            code[n] = (uint8_t)((high << 4) | v);
        }
        ++n;
        high = -1;
    }
    if (digits == 0 || high >= 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *code_size = n;
    return XXBYTE_STATUS_OK;
}

/* The listing columns of xxbyte_disasm_buf for a data line. */
static int put_prefix(xx_buf_t *out, uint32_t listing_flags, uint64_t address, const uint8_t *bytes, size_t count)
{
    size_t i;
    size_t width = 0;

    if ((listing_flags & XXBYTE_LISTING_NO_ADDRESS) == 0 && !xx_buf_appendf(out, "%08llX  ", (unsigned long long)address)) {
        return 0;
    }
    if ((listing_flags & XXBYTE_LISTING_NO_BYTES) != 0) {
        return 1;
    }
    for (i = 0; i < count; ++i) {
        if (!xx_buf_appendf(out, i == 0 ? "%02X" : " %02X", bytes[i])) {
            return 0;
        }
        width += i == 0 ? 2u : 3u;
    }
    while (width < BYTES_COLUMN) {
        if (!xx_buf_append_char(out, ' ')) {
            return 0;
        }
        ++width;
    }
    return 1;
}

static int put_hex_value(xx_buf_t *out, const uint8_t value[32])
{
    static const char digits[] = "0123456789abcdef";
    size_t i = 0;

    while (i < 31 && value[i] == 0) {
        ++i;
    }
    if (!xx_buf_append_str(out, "0x")) {
        return 0;
    }
    if (value[i] < 0x10u) {
        if (!xx_buf_append_char(out, digits[value[i]])) {
            return 0;
        }
        ++i;
    }
    for (; i < 32; ++i) {
        if (!xx_buf_append_char(out, digits[value[i] >> 4]) || !xx_buf_append_char(out, digits[value[i] & 15u])) {
            return 0;
        }
    }
    return 1;
}

static int is_destination(const uint8_t *bitmap, size_t code_size, const uint8_t value[32])
{
    uint64_t target = 0;
    size_t i;

    for (i = 0; i < 24; ++i) {
        if (value[i] != 0) {
            return 0;
        }
    }
    for (i = 24; i < 32; ++i) {
        target = (target << 8) | value[i];
    }
    return target < code_size && (bitmap[target >> 3] & (1u << (target & 7u))) != 0;
}

static int put_metadata_summary(xx_buf_t *out, const uint8_t *code, const xxbyte_evm_metadata *metadata)
{
    static const struct {
        uint32_t flag;
        const char *name;
    } keys[] = {
        {XXBYTE_EVM_METADATA_IPFS, "ipfs"},
        {XXBYTE_EVM_METADATA_BZZR0, "bzzr0"},
        {XXBYTE_EVM_METADATA_BZZR1, "bzzr1"},
        {XXBYTE_EVM_METADATA_EXPERIMENTAL, "experimental"},
    };
    size_t i;

    if (!xx_buf_appendf(out, "; metadata at 0x%x: %u bytes, CBOR %s", (unsigned)metadata->offset, (unsigned)metadata->size,
                        (metadata->flags & XXBYTE_EVM_METADATA_ARRAY) != 0 ? "array" : "map")) {
        return 0;
    }
    if ((metadata->flags & (XXBYTE_EVM_METADATA_SOLC | XXBYTE_EVM_METADATA_VYPER)) != 0) {
        const char *compiler = (metadata->flags & XXBYTE_EVM_METADATA_VYPER) != 0 ? "vyper" : "solc";
        if (metadata->compiler_version != 0) {
            if (!xx_buf_appendf(out, ", %s %u.%u.%u", compiler, (unsigned)(metadata->compiler_version >> 16), (unsigned)((metadata->compiler_version >> 8) & 0xFFu),
                                (unsigned)(metadata->compiler_version & 0xFFu))) {
                return 0;
            }
        } else if (metadata->version_size != 0) {
            uint32_t k;
            if (!xx_buf_appendf(out, ", %s ", compiler)) {
                return 0;
            }
            for (k = 0; k < metadata->version_size; ++k) {
                uint8_t c = code[metadata->version_offset + k];
                if (!xx_buf_append_char(out, c >= 0x20u && c < 0x7Fu ? (char)c : '?')) {
                    return 0;
                }
            }
        } else if (!xx_buf_appendf(out, ", %s", compiler)) {
            return 0;
        }
    }
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        if ((metadata->flags & keys[i].flag) != 0 && !xx_buf_appendf(out, ", %s", keys[i].name)) {
            return 0;
        }
    }
    return xx_buf_append_char(out, '\n');
}

static xxbyte_status list_code(uint32_t mode, const uint8_t *code, size_t code_size, int hex, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_evm_metadata metadata;
    uint8_t *bitmap;
    size_t end = code_size;
    size_t pc = 0;
    int has_metadata;
    xxbyte_instruction previous;
    xxbyte_status status = XXBYTE_STATUS_OK;

    has_metadata = xxbyte_evm_find_metadata(code, code_size, &metadata) == XXBYTE_STATUS_OK;
    if (has_metadata) {
        end = metadata.offset;
    }
    bitmap = (uint8_t *)xx_mem_calloc(code_size / 8u + 1u, 1);
    if (bitmap == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (xxbyte_evm_jump_destinations(mode, code, code_size, bitmap, code_size / 8u + 1u, NULL) != XXBYTE_STATUS_OK) {
        xx_mem_free(bitmap);
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if (!xx_buf_appendf(out, "; EVM code, %llu bytes%s, %s\n", (unsigned long long)code_size, hex ? " from hex text" : "", xxbyte_evm_mode_name(mode)) ||
        (has_metadata && !put_metadata_summary(out, code, &metadata))) {
        xx_mem_free(bitmap);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    memset(&previous, 0, sizeof(previous));
    while (pc < end) {
        xxbyte_instruction insn;
        xxbyte_status decoded = xxbyte_decode(XXBYTE_FAMILY_EVM, mode, code + pc, end - pc, pc, &insn);
        size_t length = decoded == XXBYTE_STATUS_OK ? insn.size : 1u;

        if (decoded == XXBYTE_STATUS_OK && insn.opcode == OP_JUMPDEST && (bitmap[pc >> 3] & (1u << (pc & 7u))) != 0 &&
            !xx_buf_appendf(out, "loc_0x%llx:\n", (unsigned long long)pc)) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_EVM, mode, NULL, code + pc, length, pc, listing_flags, NULL, NULL, out);
        if (status != XXBYTE_STATUS_OK) {
            break;
        }
        if (decoded == XXBYTE_STATUS_OK && (insn.opcode == OP_JUMP || insn.opcode == OP_JUMPI) && previous.size != 0) {
            uint8_t value[32];
            if (xxbyte_evm_push_value(&previous, value) == XXBYTE_STATUS_OK) {
                if (out->data != NULL && out->size != 0 && out->data[out->size - 1u] == '\n') {
                    out->data[--out->size] = '\0';
                }
                if (!xx_buf_append_str(out, "  ; -> ") || !put_hex_value(out, value) ||
                    (!is_destination(bitmap, code_size, value) && !xx_buf_append_str(out, " (not a JUMPDEST)")) || !xx_buf_append_char(out, '\n')) {
                    status = XXBYTE_STATUS_OUT_OF_MEMORY;
                    break;
                }
            }
        }
        if (decoded == XXBYTE_STATUS_OK) {
            previous = insn;
        } else {
            memset(&previous, 0, sizeof(previous));
        }
        pc += length;
    }
    if (status == XXBYTE_STATUS_OK && has_metadata) {
        if (!xx_buf_append_str(out, "; metadata\n")) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        for (pc = end; status == XXBYTE_STATUS_OK && pc < code_size; pc += DATA_LINE) {
            size_t count = code_size - pc < DATA_LINE ? code_size - pc : DATA_LINE;
            size_t i;
            if (!put_prefix(out, listing_flags, pc, code + pc, count) || !xx_buf_append_str(out, "db")) {
                status = XXBYTE_STATUS_OUT_OF_MEMORY;
                break;
            }
            for (i = 0; i < count; ++i) {
                if (!xx_buf_appendf(out, i == 0 ? " 0x%02X" : ", 0x%02X", code[pc + i])) {
                    status = XXBYTE_STATUS_OUT_OF_MEMORY;
                    break;
                }
            }
            if (status == XXBYTE_STATUS_OK && !xx_buf_append_char(out, '\n')) {
                status = XXBYTE_STATUS_OUT_OF_MEMORY;
            }
        }
    }
    xx_mem_free(bitmap);
    return status;
}

/* Hex text: ASCII hex digits, whitespace and an optional leading 0x. */
static int looks_hex(const uint8_t *data, size_t size)
{
    size_t i = 0;
    size_t digits = 0;

    while (i < size && is_space(data[i])) {
        ++i;
    }
    if (size - i >= 2 && data[i] == '0' && (data[i + 1] == 'x' || data[i + 1] == 'X')) {
        i += 2;
    }
    for (; i < size; ++i) {
        if (hex_value(data[i]) >= 0) {
            ++digits;
        } else if (!is_space(data[i])) {
            return 0;
        }
    }
    return digits != 0;
}

xxbyte_status xxbyte_evm_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    uint8_t *code;
    size_t code_size = 0;
    xxbyte_status status;

    if (mode == XXBYTE_MODE_AUTO) {
        mode = XXBYTE_EVM_MODE_DEFAULT;
    }
    if (size > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size == 0 || !looks_hex(data, size)) {
        return list_code(mode, data, size, 0, listing_flags, out);
    }
    code = (uint8_t *)xx_mem_alloc(size / 2u + 1u);
    if (code == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = xxbyte_evm_parse_hex(data, size, code, size / 2u + 1u, &code_size);
    if (status == XXBYTE_STATUS_OK) {
        status = list_code(mode, code, code_size, 1, listing_flags, out);
    }
    xx_mem_free(code);
    return status;
}

#else
typedef int xxbyte_evm_file_disabled;
#endif
