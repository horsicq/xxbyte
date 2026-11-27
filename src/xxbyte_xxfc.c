/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_internal.h"
#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#define LISTING_BYTES 8u
#define LISTING_BYTES_WIDTH (LISTING_BYTES * 3u - 1u)

int xxbyte_format_buf(const xxbyte_instruction *instruction, uint32_t flags, xxbyte_resolver resolver, void *context, xx_buf_t *out)
{
    return xxbyte_format_to_buf(instruction, flags, resolver, context, out);
}

static int append_prefix(xx_buf_t *out, uint32_t listing_flags, uint64_t address, const uint8_t *bytes, size_t size)
{
    size_t i;
    size_t shown = size < LISTING_BYTES ? size : LISTING_BYTES;
    size_t width = 0;

    if ((listing_flags & XXBYTE_LISTING_NO_ADDRESS) == 0 && !xx_buf_appendf(out, "%08llX  ", (unsigned long long)address)) {
        return 0;
    }
    if ((listing_flags & XXBYTE_LISTING_NO_BYTES) != 0) {
        return 1;
    }
    for (i = 0; i < shown; ++i) {
        if (i + 1 == LISTING_BYTES && size > LISTING_BYTES) {
            if (!xx_buf_append_str(out, "..")) {
                return 0;
            }
            width += 2;
            break;
        }
        if (!xx_buf_appendf(out, i == 0 ? "%02X" : " %02X", bytes[i])) {
            return 0;
        }
        width += i == 0 ? 2 : 3;
    }
    while (width < LISTING_BYTES_WIDTH + 2) {
        if (!xx_buf_append_char(out, ' ')) {
            return 0;
        }
        ++width;
    }
    return 1;
}

xxbyte_status xxbyte_disasm_buf(xxbyte_family family, uint32_t mode, const uint8_t *code, size_t code_size, uint64_t address, uint32_t listing_flags,
                                xxbyte_resolver resolver, void *context, xx_buf_t *out)
{
    return xxbyte_disasm_buf_ex(family, mode, NULL, code, code_size, address, listing_flags, resolver, context, out);
}

xxbyte_status xxbyte_disasm_buf_ex(xxbyte_family family, uint32_t mode, const void *decode_context, const uint8_t *code, size_t code_size, uint64_t address,
                                   uint32_t listing_flags, xxbyte_resolver resolver, void *context, xx_buf_t *out)
{
    size_t offset = 0;
    size_t unit;
    uint32_t format_flags = XXBYTE_FORMAT_DEFAULT;
    xxbyte_status status;

    if (out == NULL || (code == NULL && code_size != 0) || (listing_flags & ~(uint32_t)XXBYTE_LISTING_KNOWN_FLAGS) != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = xxbyte_check_mode(family, mode);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    unit = xxbyte_unit_size(family, mode);
    if (unit == 0) {
        unit = 1;
    }
    if ((listing_flags & XXBYTE_LISTING_UPPERCASE) != 0) {
        format_flags |= XXBYTE_FORMAT_UPPERCASE;
    }
    while (offset < code_size) {
        xxbyte_instruction instruction;
        uint64_t here = address + offset;

        status = xxbyte_decode_ex(family, mode, decode_context, code + offset, code_size - offset, here, &instruction);
        if (status == XXBYTE_STATUS_OK) {
            if (!append_prefix(out, listing_flags, here, code + offset, instruction.size) || !xxbyte_format_to_buf(&instruction, format_flags, resolver, context, out) ||
                !xx_buf_append_char(out, '\n')) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            offset += instruction.size;
        } else {
            size_t count = code_size - offset < unit ? code_size - offset : unit;
            size_t i;

            if (!append_prefix(out, listing_flags, here, code + offset, count) || !xx_buf_append_str(out, "db")) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            for (i = 0; i < count; ++i) {
                if (!xx_buf_appendf(out, i == 0 ? " 0x%02X" : ", 0x%02X", code[offset + i])) {
                    return XXBYTE_STATUS_OUT_OF_MEMORY;
                }
            }
            if (!xx_buf_append_char(out, '\n')) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            offset += count;
        }
    }
    return xx_buf_ok(out) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
}

xxbyte_status xxbyte_disasm_device(xxbyte_family family, uint32_t mode, xx_io_device *device, int64_t offset, int64_t size, uint64_t address, uint32_t listing_flags,
                                   xxbyte_resolver resolver, void *context, xx_buf_t *out)
{
    int64_t total;
    uint8_t *data;
    xxbyte_status status;

    if (device == NULL || out == NULL || offset < 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = xxbyte_check_mode(family, mode);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    total = xx_io_total_size(device);
    if (total < 0) {
        return XXBYTE_STATUS_IO_ERROR;
    }
    if (offset > total) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < 0) {
        size = total - offset;
    }
    if (size > total - offset) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if ((uint64_t)size > XXBYTE_DEVICE_REGION_LIMIT) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size == 0) {
        return XXBYTE_STATUS_OK;
    }
    data = (uint8_t *)xx_mem_alloc((size_t)size);
    if (data == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    if (!xx_io_read_at(device, offset, data, (size_t)size)) {
        xx_mem_free(data);
        return XXBYTE_STATUS_IO_ERROR;
    }
    status = xxbyte_disasm_buf(family, mode, data, (size_t)size, address, listing_flags, resolver, context, out);
    xx_mem_free(data);
    return status;
}

int xxbyte_file_supported(xxbyte_family family)
{
    const xxbyte_family_ops *ops = xxbyte_get_ops(family);

    return ops != NULL && ops->list_file != NULL;
}

xxbyte_status xxbyte_disasm_file(xxbyte_family family, uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    const xxbyte_family_ops *ops = xxbyte_get_ops(family);
    xxbyte_status status;

    if (out == NULL || (data == NULL && size != 0) || (listing_flags & ~(uint32_t)XXBYTE_LISTING_KNOWN_FLAGS) != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (ops == NULL || ops->list_file == NULL) {
        return XXBYTE_STATUS_UNSUPPORTED_FAMILY;
    }
    if (mode != XXBYTE_MODE_AUTO) {
        status = ops->check_mode(mode);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    status = ops->list_file(mode, data, size, listing_flags, out);
    if (status == XXBYTE_STATUS_OK && !xx_buf_ok(out)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return status;
}
