#include "xxbyte_internal.h"

#if XXBYTE_USE_WASM

#include "xxbyte_wasm_internal.h"
#include "xxbyte_wasm_tables.h"

#define PREFIX_GC 0xFBu
#define PREFIX_MISC 0xFCu
#define PREFIX_SIMD 0xFDu
#define PREFIX_THREADS 0xFEu

#define OP_BR_TABLE 0x0Eu
#define OP_SELECT_T 0x1Cu
#define OP_TRY_TABLE 0x1Fu

/* Byte-exact reader over the instruction bytes code[0..size). */
typedef struct wasm_reader {
    const uint8_t *code;
    size_t size;
    size_t pos;
} wasm_reader;

/*
 * LEB128 of at most bits (32, 33 or 64) bits: at most ceil(bits / 7) bytes,
 * and the unused bits of the last byte zero, or copies of the sign bit for
 * a signed value.
 */
static xxbyte_status read_leb(wasm_reader *r, unsigned bits, int is_signed, uint64_t *value, uint32_t *length)
{
    unsigned max_bytes = (bits + 6u) / 7u;
    uint64_t result = 0;
    unsigned i;

    for (i = 0; i < max_bytes; ++i) {
        uint8_t byte;
        if (r->pos + i >= r->size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        byte = r->code[r->pos + i];
        if (i + 1u == max_bytes) {
            unsigned used = bits - 7u * i;
            if ((byte & 0x80u) != 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (used < 7u) {
                unsigned low = is_signed ? used - 1u : used;
                unsigned rest = (unsigned)byte >> low;
                if (rest != 0 && !(is_signed && rest == (0x7Fu >> low))) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
            }
        }
        result |= (uint64_t)(byte & 0x7Fu) << (7u * i);
        if ((byte & 0x80u) == 0) {
            unsigned shift = 7u * (i + 1u);
            if (is_signed && shift < 64u && (byte & 0x40u) != 0) {
                result |= ~UINT64_C(0) << shift;
            }
            *value = result;
            *length = i + 1u;
            r->pos += i + 1u;
            return XXBYTE_STATUS_OK;
        }
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

static xxbyte_status read_u32(wasm_reader *r, uint64_t *value, uint32_t *length)
{
    return read_leb(r, 32, 0, value, length);
}

static xxbyte_status read_byte(wasm_reader *r, uint8_t *value)
{
    if (r->pos >= r->size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    *value = r->code[r->pos++];
    return XXBYTE_STATUS_OK;
}

static void set_index(xxbyte_operand *operand, unsigned kind, uint32_t size, int64_t value, uint64_t extra)
{
    memset(operand, 0, sizeof(*operand));
    operand->type = (uint8_t)XXBYTE_OPERAND_INDEX;
    operand->kind = (uint8_t)kind;
    operand->size = size;
    operand->value = value;
    operand->extra = extra;
}

static xxbyte_operand *add_operand(xxbyte_instruction *instruction)
{
    return xxbyte_add_operand(instruction, XXBYTE_OPERAND_NONE, 0, 0);
}

/* Reads a u32 index into a new INDEX operand of kind. */
static xxbyte_status add_index(wasm_reader *r, xxbyte_instruction *instruction, unsigned kind)
{
    uint64_t value;
    uint32_t length;
    xxbyte_operand *operand;
    xxbyte_status status = read_u32(r, &value, &length);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    operand = add_operand(instruction);
    if (operand != NULL) {
        set_index(operand, kind, length, (int64_t)value, 0);
    }
    return XXBYTE_STATUS_OK;
}

/*
 * A memory or table index that is the reserved byte 0x00 while the feature
 * that turned it into a u32 index (multi-memory, reference types) is off.
 */
static xxbyte_status add_reserved_index(wasm_reader *r, xxbyte_instruction *instruction, unsigned kind, int reserved)
{
    uint8_t byte;
    xxbyte_operand *operand;
    xxbyte_status status;

    if (!reserved) {
        return add_index(r, instruction, kind);
    }
    status = read_byte(r, &byte);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (byte != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    operand = add_operand(instruction);
    if (operand != NULL) {
        set_index(operand, kind, 1, 0, 0);
    }
    return XXBYTE_STATUS_OK;
}

/* The features needed by a type code, or -1 when byte is no type code.
 * *heap is set for abstract heap types. */
static int type_features(uint8_t byte, int *heap)
{
    *heap = 0;
    switch (byte) {
        case XXBYTE_WASM_TYPE_I32:
        case XXBYTE_WASM_TYPE_I64:
        case XXBYTE_WASM_TYPE_F32:
        case XXBYTE_WASM_TYPE_F64: return 0;
        case XXBYTE_WASM_TYPE_V128: return WNO_SIMD;
        case XXBYTE_WASM_TYPE_FUNC:
        case XXBYTE_WASM_TYPE_EXTERN: *heap = 1; return WNO_REFERENCE_TYPES;
        case XXBYTE_WASM_TYPE_EXN:
        case XXBYTE_WASM_TYPE_NOEXN: *heap = 1; return WNO_EXCEPTIONS;
        case XXBYTE_WASM_TYPE_ANY:
        case XXBYTE_WASM_TYPE_EQ:
        case XXBYTE_WASM_TYPE_I31:
        case XXBYTE_WASM_TYPE_STRUCT:
        case XXBYTE_WASM_TYPE_ARRAY:
        case XXBYTE_WASM_TYPE_NONE:
        case XXBYTE_WASM_TYPE_NOEXTERN:
        case XXBYTE_WASM_TYPE_NOFUNC: *heap = 1; return WNO_GC;
        default: return -1;
    }
}

/*
 * heaptype: an abstract heap type byte or a non-negative s33 type index.
 * form is the reference form recorded in extra (0 for a bare heap type).
 */
static xxbyte_status read_heap_type(wasm_reader *r, uint32_t mode, uint64_t form, xxbyte_operand *operand)
{
    uint8_t byte;
    uint64_t value;
    uint32_t length;
    xxbyte_status status;

    if (r->pos >= r->size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    byte = r->code[r->pos];
    if (byte >= 0x40u && byte < 0x80u) {
        int heap;
        int features = type_features(byte, &heap);
        if (features < 0 || !heap || (mode & (uint32_t)features) != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        ++r->pos;
        set_index(operand, XXBYTE_WASM_INDEX_HEAPTYPE, 1, byte, form);
        return XXBYTE_STATUS_OK;
    }
    status = read_leb(r, 33, 1, &value, &length);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if ((int64_t)value < 0 || (mode & WNO_FUNCTION_REFERENCES) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    set_index(operand, XXBYTE_INDEX_TYPE, length, (int64_t)value, form);
    return XXBYTE_STATUS_OK;
}

/* valtype: a number or vector type, a reference shorthand, or 0x63/0x64
 * followed by a heap type. */
static xxbyte_status read_value_type(wasm_reader *r, uint32_t mode, xxbyte_operand *operand)
{
    uint8_t byte;
    int heap;
    int features;

    if (r->pos >= r->size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    byte = r->code[r->pos];
    if (byte == XXBYTE_WASM_TYPE_REF || byte == XXBYTE_WASM_TYPE_REF_NULL) {
        xxbyte_status status;
        if ((mode & WNO_FUNCTION_REFERENCES) != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        ++r->pos;
        status = read_heap_type(r, mode, byte, operand);
        if (status == XXBYTE_STATUS_OK) {
            operand->size += 1u;
        }
        return status;
    }
    features = type_features(byte, &heap);
    if (features < 0 || (mode & (uint32_t)features) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    ++r->pos;
    if (heap) {
        set_index(operand, XXBYTE_WASM_INDEX_HEAPTYPE, 1, byte, XXBYTE_WASM_TYPE_REF_NULL);
    } else {
        set_index(operand, XXBYTE_WASM_INDEX_VALTYPE, 1, byte, 0);
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_wasm_read_value_type_at(uint32_t mode, const uint8_t *data, size_t end, size_t *pos, xxbyte_operand *type)
{
    wasm_reader r;
    xxbyte_status status;

    r.code = data;
    r.size = end;
    r.pos = *pos;
    status = read_value_type(&r, mode, type);
    if (status == XXBYTE_STATUS_OK) {
        *pos = r.pos;
    }
    return status;
}

/* blocktype: 0x40, a value type, or a non-negative s33 type index. */
static xxbyte_status read_block_type(wasm_reader *r, uint32_t mode, xxbyte_operand *operand)
{
    uint8_t byte;
    uint64_t value;
    uint32_t length;
    xxbyte_status status;

    if (r->pos >= r->size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    byte = r->code[r->pos];
    if (byte == XXBYTE_WASM_TYPE_EMPTY) {
        ++r->pos;
        set_index(operand, XXBYTE_WASM_INDEX_VALTYPE, 1, byte, 0);
        return XXBYTE_STATUS_OK;
    }
    if (byte >= 0x40u && byte < 0x80u) {
        return read_value_type(r, mode, operand);
    }
    status = read_leb(r, 33, 1, &value, &length);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if ((int64_t)value < 0 || (mode & WNO_MULTI_VALUE) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    set_index(operand, XXBYTE_INDEX_TYPE, length, (int64_t)value, 0);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status add_typed(wasm_reader *r, xxbyte_instruction *instruction, xxbyte_status (*read)(wasm_reader *, uint32_t, xxbyte_operand *), uint32_t mode)
{
    xxbyte_operand type;
    xxbyte_operand *operand;
    xxbyte_status status = read(r, mode, &type);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    operand = add_operand(instruction);
    if (operand != NULL) {
        *operand = type;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status add_heap_type(wasm_reader *r, xxbyte_instruction *instruction, uint32_t mode, uint64_t form)
{
    xxbyte_operand type;
    xxbyte_operand *operand;
    xxbyte_status status = read_heap_type(r, mode, form, &type);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    operand = add_operand(instruction);
    if (operand != NULL) {
        *operand = type;
    }
    return XXBYTE_STATUS_OK;
}

/*
 * memarg: the alignment field (bit 6 announces a memory index, values from
 * 128 are malformed), the memory index, and the offset (u64 with memory64).
 */
static xxbyte_status add_memarg(wasm_reader *r, xxbyte_instruction *instruction, uint32_t mode, unsigned width)
{
    uint64_t flags;
    uint64_t memory = 0;
    uint64_t offset;
    uint32_t length;
    uint32_t memory_length = 0;
    xxbyte_operand *operand;
    xxbyte_status status = read_u32(r, &flags, &length);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (flags >= 128u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if ((flags & 64u) != 0) {
        if ((mode & WNO_MULTI_MEMORY) != 0) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        status = read_u32(r, &memory, &memory_length);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    status = read_leb(r, (mode & WNO_MEMORY64) != 0 ? 32u : 64u, 0, &offset, &length);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    operand = add_operand(instruction);
    if (operand != NULL) {
        set_index(operand, XXBYTE_WASM_INDEX_MEMORY, memory_length, (int64_t)memory, 0);
        if (memory_length == 0) {
            operand->flags = XXBYTE_OPERAND_FLAG_IMPLICIT;
        }
    }
    operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_MEMORY, 1u << width, (int64_t)offset);
    if (operand != NULL) {
        operand->extra = flags & 63u;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status add_immediate(wasm_reader *r, xxbyte_instruction *instruction, unsigned bits, int is_signed)
{
    uint64_t value;
    uint32_t length;
    xxbyte_operand *operand;
    xxbyte_status status = read_leb(r, bits, is_signed, &value, &length);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, length, (int64_t)value);
    if (operand != NULL && is_signed) {
        operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
    }
    return XXBYTE_STATUS_OK;
}

static xxbyte_status add_byte(wasm_reader *r, xxbyte_instruction *instruction, uint8_t *value)
{
    xxbyte_status status = read_byte(r, value);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, *value);
    return XXBYTE_STATUS_OK;
}

/* Inline bytes (constants, shuffle lanes) after the opcode. */
static xxbyte_status add_bytes(wasm_reader *r, xxbyte_instruction *instruction, xxbyte_operand_type type, uint32_t count)
{
    xxbyte_operand *operand;

    if (r->size - r->pos < count) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (type == XXBYTE_OPERAND_FLOAT) {
        const uint8_t *p = r->code + r->pos;
        int64_t bits = count == 4 ? (int64_t)xxbyte_u32le(p) : (int64_t)xxbyte_u64le(p);
        operand = xxbyte_add_operand(instruction, type, count, bits);
        if (operand != NULL && count == 8) {
            operand->flags = XXBYTE_OPERAND_FLAG_WIDE;
        }
    } else {
        xxbyte_add_operand(instruction, type, count, (int64_t)r->pos);
    }
    r->pos += count;
    return XXBYTE_STATUS_OK;
}

/* vec(labelidx) labelidx: table_count labels at table_offset, then the
 * default as operand 0. Every label takes at least one byte, so the loop
 * ends within the input. */
static xxbyte_status decode_br_table(wasm_reader *r, xxbyte_instruction *instruction)
{
    uint64_t count;
    uint64_t i;
    uint32_t length;
    xxbyte_status status = read_u32(r, &count, &length);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    instruction->table_offset = (uint32_t)r->pos;
    for (i = 0; i < count; ++i) {
        uint64_t label;
        status = read_u32(r, &label, &length);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    instruction->table_count = (uint32_t)count;
    return add_index(r, instruction, XXBYTE_INDEX_LABEL);
}

static xxbyte_status decode_select_t(wasm_reader *r, xxbyte_instruction *instruction, uint32_t mode)
{
    uint64_t count;
    uint64_t i;
    uint32_t length;
    xxbyte_status status = read_u32(r, &count, &length);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    instruction->table_offset = (uint32_t)r->pos;
    for (i = 0; i < count; ++i) {
        xxbyte_operand type;
        status = read_value_type(r, mode, &type);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    instruction->table_count = (uint32_t)count;
    return XXBYTE_STATUS_OK;
}

/* One try_table catch clause: kind, the tag for catch and catch_ref, and
 * the label. */
static xxbyte_status read_catch(wasm_reader *r, xxbyte_wasm_catch *clause)
{
    uint8_t kind;
    uint64_t value;
    uint32_t length;
    xxbyte_status status = read_byte(r, &kind);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (kind > XXBYTE_WASM_CATCH_ALL_REF) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    clause->kind = kind;
    clause->tag = 0;
    if (kind == XXBYTE_WASM_CATCH || kind == XXBYTE_WASM_CATCH_REF) {
        status = read_u32(r, &value, &length);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        clause->tag = (uint32_t)value;
    }
    status = read_u32(r, &value, &length);
    clause->label = (uint32_t)value;
    return status;
}

static xxbyte_status decode_try_table(wasm_reader *r, xxbyte_instruction *instruction, uint32_t mode)
{
    uint64_t count;
    uint64_t i;
    uint32_t length;
    xxbyte_status status = add_typed(r, instruction, read_block_type, mode);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    status = read_u32(r, &count, &length);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    instruction->table_offset = (uint32_t)r->pos;
    for (i = 0; i < count; ++i) {
        xxbyte_wasm_catch clause;
        status = read_catch(r, &clause);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    instruction->table_count = (uint32_t)count;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status decode_br_on_cast(wasm_reader *r, xxbyte_instruction *instruction, uint32_t mode)
{
    uint8_t flags;
    xxbyte_status status = add_byte(r, instruction, &flags);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (flags > 3u) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    status = add_index(r, instruction, XXBYTE_INDEX_LABEL);
    if (status == XXBYTE_STATUS_OK) {
        status = add_heap_type(r, instruction, mode, (flags & 1u) != 0 ? XXBYTE_WASM_TYPE_REF_NULL : XXBYTE_WASM_TYPE_REF);
    }
    if (status == XXBYTE_STATUS_OK) {
        status = add_heap_type(r, instruction, mode, (flags & 2u) != 0 ? XXBYTE_WASM_TYPE_REF_NULL : XXBYTE_WASM_TYPE_REF);
    }
    return status;
}

static xxbyte_status decode_immediates(const xxbyte_wasm_op *op, uint32_t mode, wasm_reader *r, xxbyte_instruction *instruction)
{
    int no_multi_memory = (mode & WNO_MULTI_MEMORY) != 0;
    int no_reference_types = (mode & WNO_REFERENCE_TYPES) != 0;
    xxbyte_status status = XXBYTE_STATUS_OK;
    uint8_t byte;

    switch (op->form) {
        case WF_NONE: return XXBYTE_STATUS_OK;
        case WF_BLOCK: return add_typed(r, instruction, read_block_type, mode);
        case WF_TRY_TABLE: return decode_try_table(r, instruction, mode);
        case WF_LABEL: return add_index(r, instruction, XXBYTE_INDEX_LABEL);
        case WF_BR_TABLE: return decode_br_table(r, instruction);
        case WF_FUNC: return add_index(r, instruction, XXBYTE_INDEX_FUNCTION);
        case WF_CALL_INDIRECT:
            status = add_index(r, instruction, XXBYTE_INDEX_TYPE);
            if (status == XXBYTE_STATUS_OK) {
                status = add_reserved_index(r, instruction, XXBYTE_INDEX_TABLE, no_reference_types);
            }
            return status;
        case WF_TYPE: return add_index(r, instruction, XXBYTE_INDEX_TYPE);
        case WF_TAG: return add_index(r, instruction, XXBYTE_WASM_INDEX_TAG);
        case WF_SELECT_T: return decode_select_t(r, instruction, mode);
        case WF_LOCAL: return add_index(r, instruction, XXBYTE_INDEX_LOCAL);
        case WF_GLOBAL: return add_index(r, instruction, XXBYTE_INDEX_GLOBAL);
        case WF_TABLE: return add_index(r, instruction, XXBYTE_INDEX_TABLE);
        case WF_MEMARG: return add_memarg(r, instruction, mode, op->width);
        case WF_MEMARG_LANE:
            status = add_memarg(r, instruction, mode, op->width);
            if (status == XXBYTE_STATUS_OK) {
                status = add_byte(r, instruction, &byte);
            }
            return status;
        case WF_MEMORY: return add_reserved_index(r, instruction, XXBYTE_WASM_INDEX_MEMORY, no_multi_memory);
        case WF_MEMORY_COPY:
            status = add_reserved_index(r, instruction, XXBYTE_WASM_INDEX_MEMORY, no_multi_memory);
            if (status == XXBYTE_STATUS_OK) {
                status = add_reserved_index(r, instruction, XXBYTE_WASM_INDEX_MEMORY, no_multi_memory);
            }
            return status;
        case WF_MEMORY_INIT:
            status = add_index(r, instruction, XXBYTE_WASM_INDEX_DATA);
            if (status == XXBYTE_STATUS_OK) {
                status = add_reserved_index(r, instruction, XXBYTE_WASM_INDEX_MEMORY, no_multi_memory);
            }
            return status;
        case WF_DATA: return add_index(r, instruction, XXBYTE_WASM_INDEX_DATA);
        case WF_ELEM: return add_index(r, instruction, XXBYTE_WASM_INDEX_ELEM);
        case WF_TABLE_INIT:
            status = add_index(r, instruction, XXBYTE_WASM_INDEX_ELEM);
            if (status == XXBYTE_STATUS_OK) {
                status = add_reserved_index(r, instruction, XXBYTE_INDEX_TABLE, no_reference_types);
            }
            return status;
        case WF_TABLE_COPY:
            status = add_reserved_index(r, instruction, XXBYTE_INDEX_TABLE, no_reference_types);
            if (status == XXBYTE_STATUS_OK) {
                status = add_reserved_index(r, instruction, XXBYTE_INDEX_TABLE, no_reference_types);
            }
            return status;
        case WF_I32: return add_immediate(r, instruction, 32, 1);
        case WF_I64: return add_immediate(r, instruction, 64, 1);
        case WF_F32: return add_bytes(r, instruction, XXBYTE_OPERAND_FLOAT, 4);
        case WF_F64: return add_bytes(r, instruction, XXBYTE_OPERAND_FLOAT, 8);
        case WF_V128:
        case WF_SHUFFLE: return add_bytes(r, instruction, XXBYTE_OPERAND_BYTES, 16);
        case WF_LANE: return add_byte(r, instruction, &byte);
        case WF_HEAPTYPE: return add_heap_type(r, instruction, mode, 0);
        case WF_FENCE:
            status = add_byte(r, instruction, &byte);
            if (status == XXBYTE_STATUS_OK && byte != 0) {
                status = XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            return status;
        case WF_TYPE_FIELD:
        case WF_TYPE_COUNT:
        case WF_TYPE_DATA:
        case WF_TYPE_ELEM:
        case WF_TYPE_TYPE:
            status = add_index(r, instruction, XXBYTE_INDEX_TYPE);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            switch (op->form) {
                case WF_TYPE_FIELD: return add_index(r, instruction, XXBYTE_INDEX_FIELD);
                case WF_TYPE_COUNT: return add_immediate(r, instruction, 32, 0);
                case WF_TYPE_DATA: return add_index(r, instruction, XXBYTE_WASM_INDEX_DATA);
                case WF_TYPE_ELEM: return add_index(r, instruction, XXBYTE_WASM_INDEX_ELEM);
                default: return add_index(r, instruction, XXBYTE_INDEX_TYPE);
            }
        case WF_REF: return add_heap_type(r, instruction, mode, XXBYTE_WASM_TYPE_REF);
        case WF_REF_NULL: return add_heap_type(r, instruction, mode, XXBYTE_WASM_TYPE_REF_NULL);
        case WF_BR_ON_CAST: return decode_br_on_cast(r, instruction, mode);
        default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
}

/* The table entry of a prefixed opcode, or NULL when out of range. */
static const xxbyte_wasm_op *prefixed_op(uint32_t prefix, uint64_t sub)
{
    switch (prefix) {
        case PREFIX_GC: return sub < XXBYTE_WASM_FB_COUNT ? &xxbyte_wasm_ops_fb[sub] : NULL;
        case PREFIX_MISC: return sub < XXBYTE_WASM_FC_COUNT ? &xxbyte_wasm_ops_fc[sub] : NULL;
        case PREFIX_SIMD: return sub < XXBYTE_WASM_FD_COUNT ? &xxbyte_wasm_ops_fd[sub] : NULL;
        case PREFIX_THREADS: return sub < XXBYTE_WASM_FE_COUNT ? &xxbyte_wasm_ops_fe[sub] : NULL;
        default: return NULL;
    }
}

static const xxbyte_wasm_op *op_of(uint32_t opcode)
{
    if (opcode <= 0xFFu) {
        return &xxbyte_wasm_ops_single[opcode];
    }
    return prefixed_op(opcode >> 16, opcode & 0xFFFFu);
}

static int enabled(const xxbyte_wasm_op *op, uint32_t mode)
{
    uint32_t missing = mode & op->features;

    if ((op->flags & OPF_ANY) != 0) {
        return missing != op->features;
    }
    return missing == 0;
}

static xxbyte_status wasm_check_mode(uint32_t mode)
{
    return (mode & ~(uint32_t)XXBYTE_WASM_MODE_KNOWN_BITS) == 0 ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static xxbyte_status wasm_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    wasm_reader r;
    const xxbyte_wasm_op *op;
    uint32_t opcode = code[0];
    xxbyte_status status;

    (void)context;
    (void)address;
    r.code = code;
    r.size = code_size;
    r.pos = 1;
    if (opcode >= PREFIX_GC && opcode <= PREFIX_THREADS) {
        uint64_t sub;
        uint32_t length;
        status = read_u32(&r, &sub, &length);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        op = prefixed_op(opcode, sub);
        if (op == NULL) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        opcode = (opcode << 16) | (uint32_t)sub;
    } else {
        op = &xxbyte_wasm_ops_single[opcode];
    }
    if (op->id == 0 || !enabled(op, mode)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    status = decode_immediates(op, mode, &r, instruction);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (r.pos > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->size = (uint32_t)r.pos;
    instruction->opcode = opcode;
    instruction->id = op->id;
    instruction->group = op->group;
    instruction->mnemonic = xxbyte_wasm_mnemonics[op->id - 1];
    return XXBYTE_STATUS_OK;
}

/* ---- inline tables and locals vectors */

/*
 * A reader over the inline table of instruction, a decoded br_table,
 * select t or try_table (opcode): from table_offset to the end of the
 * instruction, for br_table to the default label. 0 for anything else.
 */
static int table_reader(const xxbyte_instruction *instruction, uint32_t opcode, wasm_reader *r)
{
    uint32_t end;

    if (instruction == NULL || instruction->code == NULL || instruction->family != (uint32_t)XXBYTE_FAMILY_WASM || instruction->opcode != opcode ||
        instruction->table_offset > instruction->size) {
        return 0;
    }
    end = instruction->size;
    if (opcode == OP_BR_TABLE) {
        if (instruction->operand_count == 0 || instruction->operands[0].size > end - instruction->table_offset) {
            return 0;
        }
        end -= instruction->operands[0].size;
    }
    r->code = instruction->code;
    r->size = end;
    r->pos = instruction->table_offset;
    return 1;
}

/* Reads one table entry into *entry. */
typedef xxbyte_status (*entry_reader)(wasm_reader *r, uint32_t mode, void *entry);

typedef struct local_entry {
    uint32_t count;
    xxbyte_operand type;
} local_entry;

static xxbyte_status read_label_entry(wasm_reader *r, uint32_t mode, void *entry)
{
    uint64_t value;
    uint32_t length;
    xxbyte_status status = read_u32(r, &value, &length);

    (void)mode;
    if (status == XXBYTE_STATUS_OK) {
        *(uint32_t *)entry = (uint32_t)value;
    }
    return status;
}

static xxbyte_status read_type_entry(wasm_reader *r, uint32_t mode, void *entry)
{
    return read_value_type(r, mode, (xxbyte_operand *)entry);
}

static xxbyte_status read_catch_entry(wasm_reader *r, uint32_t mode, void *entry)
{
    (void)mode;
    return read_catch(r, (xxbyte_wasm_catch *)entry);
}

static xxbyte_status read_local_entry(wasm_reader *r, uint32_t mode, void *entry)
{
    local_entry *local = (local_entry *)entry;
    uint64_t value;
    uint32_t length;
    xxbyte_status status = read_u32(r, &value, &length);

    if (status == XXBYTE_STATUS_OK) {
        status = read_value_type(r, mode, &local->type);
    }
    if (status == XXBYTE_STATUS_OK) {
        local->count = (uint32_t)value;
    }
    return status;
}

/*
 * Reads entry index of the count entries in r->code[r->pos..r->size), each
 * at least min_size bytes long. When they span exactly count * min_size
 * bytes they all have that size and entry index is read directly; otherwise
 * the index entries before it are skipped, O(index). Enumerating a table is
 * linear with next_entry.
 */
static xxbyte_status read_entry(wasm_reader *r, uint32_t count, uint32_t min_size, entry_reader read, uint32_t mode, uint32_t index, void *entry)
{
    uint32_t i;

    if (index >= count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if ((uint64_t)(r->size - r->pos) == (uint64_t)count * min_size) {
        r->pos += (size_t)index * min_size;
        r->size = r->pos + min_size;
    } else {
        for (i = 0; i < index; ++i) {
            if (read(r, mode, entry) != XXBYTE_STATUS_OK) {
                return XXBYTE_STATUS_INVALID_ARGUMENT;
            }
        }
    }
    return read(r, mode, entry) == XXBYTE_STATUS_OK ? XXBYTE_STATUS_OK : XXBYTE_STATUS_INVALID_ARGUMENT;
}

/*
 * Cursor step: reads the entry at *offset of the entries in
 * r->code[r->pos..r->size) and advances *offset past it.
 * XXBYTE_STATUS_END_OF_INPUT at the end, XXBYTE_STATUS_INVALID_ARGUMENT for
 * an offset outside the entries or an entry that does not fit; *offset only
 * changes on success.
 */
static xxbyte_status next_entry(wasm_reader *r, uint32_t *offset, entry_reader read, uint32_t mode, void *entry)
{
    if (*offset < r->pos || *offset > r->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (*offset == r->size) {
        return XXBYTE_STATUS_END_OF_INPUT;
    }
    r->pos = *offset;
    if (read(r, mode, entry) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *offset = (uint32_t)r->pos;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status wasm_switch_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target)
{
    wasm_reader r;
    uint32_t label;

    if (!table_reader(instruction, OP_BR_TABLE, &r) || read_entry(&r, instruction->table_count, 1, read_label_entry, 0, index, &label) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *key = index;
    *target = label;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_wasm_br_table_next(const xxbyte_instruction *instruction, uint32_t *offset, uint32_t *label)
{
    wasm_reader r;

    if (label != NULL) {
        *label = 0;
    }
    if (offset == NULL || label == NULL || !table_reader(instruction, OP_BR_TABLE, &r)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    /* A label ends with a byte below 0x80, so an offset after any other
     * byte is inside a label. */
    if (*offset > r.pos && *offset <= r.size && (r.code[*offset - 1u] & 0x80u) != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return next_entry(&r, offset, read_label_entry, 0, label);
}

xxbyte_status xxbyte_wasm_select_type(const xxbyte_instruction *instruction, uint32_t index, xxbyte_operand *type)
{
    wasm_reader r;

    if (type == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (!table_reader(instruction, OP_SELECT_T, &r) || read_entry(&r, instruction->table_count, 1, read_type_entry, instruction->mode, index, type) != XXBYTE_STATUS_OK) {
        memset(type, 0, sizeof(*type));
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_wasm_select_type_next(const xxbyte_instruction *instruction, uint32_t *offset, xxbyte_operand *type)
{
    wasm_reader r;
    xxbyte_status status;

    if (type == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(type, 0, sizeof(*type));
    if (offset == NULL || !table_reader(instruction, OP_SELECT_T, &r)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = next_entry(&r, offset, read_type_entry, instruction->mode, type);
    if (status != XXBYTE_STATUS_OK) {
        memset(type, 0, sizeof(*type));
    }
    return status;
}

xxbyte_status xxbyte_wasm_catch_clause(const xxbyte_instruction *instruction, uint32_t index, xxbyte_wasm_catch *clause)
{
    wasm_reader r;

    if (clause == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (!table_reader(instruction, OP_TRY_TABLE, &r) || read_entry(&r, instruction->table_count, 2, read_catch_entry, 0, index, clause) != XXBYTE_STATUS_OK) {
        memset(clause, 0, sizeof(*clause));
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_wasm_catch_clause_next(const xxbyte_instruction *instruction, uint32_t *offset, xxbyte_wasm_catch *clause)
{
    wasm_reader r;
    xxbyte_status status;

    if (clause == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(clause, 0, sizeof(*clause));
    if (offset == NULL || !table_reader(instruction, OP_TRY_TABLE, &r)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = next_entry(&r, offset, read_catch_entry, 0, clause);
    if (status != XXBYTE_STATUS_OK) {
        memset(clause, 0, sizeof(*clause));
    }
    return status;
}

/* ---- formatting */

static const struct {
    uint8_t code;
    const char *name;      /* number or vector type, or heap type */
    const char *shorthand; /* nullable reference shorthand */
} type_names[] = {
    {XXBYTE_WASM_TYPE_I32, "i32", NULL},
    {XXBYTE_WASM_TYPE_I64, "i64", NULL},
    {XXBYTE_WASM_TYPE_F32, "f32", NULL},
    {XXBYTE_WASM_TYPE_F64, "f64", NULL},
    {XXBYTE_WASM_TYPE_V128, "v128", NULL},
    {XXBYTE_WASM_TYPE_NOEXN, "noexn", "nullexnref"},
    {XXBYTE_WASM_TYPE_NOFUNC, "nofunc", "nullfuncref"},
    {XXBYTE_WASM_TYPE_NOEXTERN, "noextern", "nullexternref"},
    {XXBYTE_WASM_TYPE_NONE, "none", "nullref"},
    {XXBYTE_WASM_TYPE_FUNC, "func", "funcref"},
    {XXBYTE_WASM_TYPE_EXTERN, "extern", "externref"},
    {XXBYTE_WASM_TYPE_ANY, "any", "anyref"},
    {XXBYTE_WASM_TYPE_EQ, "eq", "eqref"},
    {XXBYTE_WASM_TYPE_I31, "i31", "i31ref"},
    {XXBYTE_WASM_TYPE_STRUCT, "struct", "structref"},
    {XXBYTE_WASM_TYPE_ARRAY, "array", "arrayref"},
    {XXBYTE_WASM_TYPE_EXN, "exn", "exnref"},
};

static const char *const non_null_names[] = {
    "(ref noexn)", "(ref nofunc)", "(ref noextern)", "(ref none)",   "(ref func)",  "(ref extern)",
    "(ref any)",   "(ref eq)",     "(ref i31)",      "(ref struct)", "(ref array)", "(ref exn)",
};

const char *xxbyte_wasm_type_name(uint32_t code, uint32_t ref_form)
{
    size_t i;

    for (i = 0; i < sizeof(type_names) / sizeof(type_names[0]); ++i) {
        if (type_names[i].code != code) {
            continue;
        }
        if (ref_form == 0) {
            return type_names[i].name;
        }
        if (type_names[i].shorthand == NULL) {
            return NULL;
        }
        if (ref_form == XXBYTE_WASM_TYPE_REF_NULL) {
            return type_names[i].shorthand;
        }
        if (ref_form == XXBYTE_WASM_TYPE_REF) {
            return non_null_names[i - 5];
        }
        return NULL;
    }
    return NULL;
}

/* A displayed index operand, or an inline-table entry when entry is set. */
static int put_index(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t index, const xxbyte_operand *entry)
{
    const xxbyte_operand *operand = entry != NULL ? entry : &instruction->operands[index];
    int resolved = entry != NULL ? xxbyte_fmt_resolve_entry(fmt, instruction, entry) : xxbyte_fmt_resolve(fmt, instruction, index);

    if (resolved != 0) {
        return resolved > 0;
    }
    return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
}

static int put_space_index(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t index)
{
    return xxbyte_fmt_char(fmt, ' ') && put_index(fmt, instruction, index, NULL);
}

/* A value, reference or heap type operand. */
static int put_type(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t index, const xxbyte_operand *entry)
{
    const xxbyte_operand *operand = entry != NULL ? entry : &instruction->operands[index];

    if (operand->kind == XXBYTE_INDEX_TYPE) {
        const char *open = operand->extra == XXBYTE_WASM_TYPE_REF_NULL ? "(ref null " : (operand->extra == XXBYTE_WASM_TYPE_REF ? "(ref " : NULL);
        if (open == NULL) {
            return put_index(fmt, instruction, index, entry);
        }
        return xxbyte_fmt_str(fmt, open) && put_index(fmt, instruction, index, entry) && xxbyte_fmt_char(fmt, ')');
    }
    {
        const char *name = xxbyte_wasm_type_name((uint32_t)operand->value, operand->kind == XXBYTE_WASM_INDEX_HEAPTYPE ? (uint32_t)operand->extra : 0u);
        return name != NULL && xxbyte_fmt_str(fmt, name);
    }
}

static int put_block_type(xxbyte_fmt *fmt, const xxbyte_instruction *instruction)
{
    const xxbyte_operand *type = &instruction->operands[0];

    if (instruction->operand_count == 0 || (type->kind == XXBYTE_WASM_INDEX_VALTYPE && type->value == XXBYTE_WASM_TYPE_EMPTY)) {
        return 1;
    }
    if (type->kind == XXBYTE_INDEX_TYPE && type->extra == 0) {
        return xxbyte_fmt_str(fmt, " (type ") && put_index(fmt, instruction, 0, NULL) && xxbyte_fmt_char(fmt, ')');
    }
    return xxbyte_fmt_str(fmt, " (result ") && put_type(fmt, instruction, 0, NULL) && xxbyte_fmt_char(fmt, ')');
}

static int put_f32(xxbyte_fmt *fmt, uint32_t bits)
{
    uint32_t payload = bits & 0x7FFFFFu;
    float value;

    if ((bits & 0x7F800000u) == 0x7F800000u && payload != 0) {
        if (!xxbyte_fmt_str(fmt, (bits >> 31) != 0 ? "-nan" : "nan")) {
            return 0;
        }
        return payload == 0x400000u || xxbyte_fmt_printf(fmt, ":0x%x", (unsigned)payload);
    }
    memcpy(&value, &bits, sizeof(value));
    return xxbyte_fmt_float(fmt, value);
}

static int put_f64(xxbyte_fmt *fmt, uint64_t bits)
{
    uint64_t payload = bits & ((UINT64_C(1) << 52) - 1u);
    double value;

    if ((bits & UINT64_C(0x7FF0000000000000)) == UINT64_C(0x7FF0000000000000) && payload != 0) {
        if (!xxbyte_fmt_str(fmt, (bits >> 63) != 0 ? "-nan" : "nan")) {
            return 0;
        }
        return payload == (UINT64_C(1) << 51) || xxbyte_fmt_printf(fmt, ":0x%llx", (unsigned long long)payload);
    }
    memcpy(&value, &bits, sizeof(value));
    return xxbyte_fmt_double(fmt, value);
}

/* [memory] [offset=N] [align=N] as wasm2wat prints a memarg. */
static int put_memarg(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, unsigned width)
{
    const xxbyte_operand *memory = &instruction->operands[0];
    const xxbyte_operand *address = &instruction->operands[1];

    if (instruction->operand_count < 2) {
        return 0;
    }
    if (memory->value != 0 && !put_space_index(fmt, instruction, 0)) {
        return 0;
    }
    if (address->value != 0 && !xxbyte_fmt_printf(fmt, " offset=%llu", (unsigned long long)address->value)) {
        return 0;
    }
    if (address->extra != width) {
        unsigned long long align = 1ull << (address->extra & 63u);
        if (!xxbyte_fmt_printf(fmt, " align=%llu", align)) {
            return 0;
        }
    }
    return 1;
}

static int put_v128(xxbyte_fmt *fmt, const xxbyte_instruction *instruction)
{
    const xxbyte_operand *bytes = &instruction->operands[0];
    const uint8_t *p;
    int i;

    if (instruction->operand_count == 0 || bytes->value + 16 > (int64_t)instruction->size) {
        return 0;
    }
    p = instruction->code + bytes->value;
    if (!xxbyte_fmt_str(fmt, " i32x4")) {
        return 0;
    }
    for (i = 0; i < 4; ++i) {
        if (!xxbyte_fmt_printf(fmt, " 0x%08x", (unsigned)xxbyte_u32le(p))) {
            return 0;
        }
        p += 4;
    }
    return 1;
}

static int put_shuffle(xxbyte_fmt *fmt, const xxbyte_instruction *instruction)
{
    const xxbyte_operand *bytes = &instruction->operands[0];
    const uint8_t *p;
    int i;

    if (instruction->operand_count == 0 || bytes->value + 16 > (int64_t)instruction->size) {
        return 0;
    }
    p = instruction->code + bytes->value;
    for (i = 0; i < 16; ++i) {
        if (!xxbyte_fmt_printf(fmt, " %u", (unsigned)p[i])) {
            return 0;
        }
    }
    return 1;
}

/* The labels are read in one pass, so formatting stays linear. */
static int put_br_table(xxbyte_fmt *fmt, const xxbyte_instruction *instruction)
{
    wasm_reader r;
    xxbyte_operand entry;
    uint32_t i;

    if (!table_reader(instruction, OP_BR_TABLE, &r) || instruction->operand_count == 0) {
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    entry.type = XXBYTE_OPERAND_INDEX;
    entry.kind = XXBYTE_INDEX_LABEL;
    for (i = 0; i < instruction->table_count; ++i) {
        uint64_t label;
        if (read_u32(&r, &label, &entry.size) != XXBYTE_STATUS_OK) {
            return 0;
        }
        entry.value = (int64_t)label;
        if (!xxbyte_fmt_char(fmt, ' ') || !put_index(fmt, instruction, 0, &entry)) {
            return 0;
        }
    }
    return put_space_index(fmt, instruction, 0);
}

static int put_select_types(xxbyte_fmt *fmt, const xxbyte_instruction *instruction)
{
    wasm_reader r;
    uint32_t i;

    if (instruction->table_count == 0) {
        return 1;
    }
    if (!table_reader(instruction, OP_SELECT_T, &r) || !xxbyte_fmt_str(fmt, " (result")) {
        return 0;
    }
    for (i = 0; i < instruction->table_count; ++i) {
        xxbyte_operand type;
        if (read_value_type(&r, instruction->mode, &type) != XXBYTE_STATUS_OK || !xxbyte_fmt_char(fmt, ' ') || !put_type(fmt, instruction, 0, &type)) {
            return 0;
        }
    }
    return xxbyte_fmt_char(fmt, ')');
}

static int put_catches(xxbyte_fmt *fmt, const xxbyte_instruction *instruction)
{
    static const char *const names[] = {"catch", "catch_ref", "catch_all", "catch_all_ref"};
    wasm_reader r;
    xxbyte_operand entry;
    uint32_t i;

    if (instruction->table_count == 0) {
        return 1;
    }
    if (!table_reader(instruction, OP_TRY_TABLE, &r)) {
        return 0;
    }
    memset(&entry, 0, sizeof(entry));
    entry.type = XXBYTE_OPERAND_INDEX;
    for (i = 0; i < instruction->table_count; ++i) {
        xxbyte_wasm_catch clause;
        if (read_catch(&r, &clause) != XXBYTE_STATUS_OK || !xxbyte_fmt_printf(fmt, " (%s ", names[clause.kind])) {
            return 0;
        }
        if (clause.kind == XXBYTE_WASM_CATCH || clause.kind == XXBYTE_WASM_CATCH_REF) {
            entry.kind = (uint8_t)XXBYTE_WASM_INDEX_TAG;
            entry.value = clause.tag;
            if (!put_index(fmt, instruction, 0, &entry) || !xxbyte_fmt_char(fmt, ' ')) {
                return 0;
            }
        }
        entry.kind = XXBYTE_INDEX_LABEL;
        entry.value = clause.label;
        if (!put_index(fmt, instruction, 0, &entry) || !xxbyte_fmt_char(fmt, ')')) {
            return 0;
        }
    }
    return 1;
}

/* Prints operands first..operand_count - 1 separated by spaces. */
static int put_indexes(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t first)
{
    uint32_t i;

    for (i = first; i < instruction->operand_count; ++i) {
        const xxbyte_operand *operand = &instruction->operands[i];
        if (operand->type == XXBYTE_OPERAND_IMMEDIATE) {
            if (!xxbyte_fmt_printf(fmt, " %llu", (unsigned long long)operand->value)) {
                return 0;
            }
        } else if (!put_space_index(fmt, instruction, i)) {
            return 0;
        }
    }
    return 1;
}

static int wasm_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const xxbyte_wasm_op *op = op_of(instruction->opcode);
    const xxbyte_operand *operands = instruction->operands;
    uint32_t count = instruction->operand_count;

    if (op == NULL || op->id == 0) {
        return 0;
    }
    switch (op->form) {
        case WF_NONE:
        case WF_FENCE: return 1;
        case WF_BLOCK: return put_block_type(fmt, instruction);
        case WF_TRY_TABLE: return put_block_type(fmt, instruction) && put_catches(fmt, instruction);
        case WF_BR_TABLE: return put_br_table(fmt, instruction);
        case WF_SELECT_T: return put_select_types(fmt, instruction);
        case WF_CALL_INDIRECT:
            if (count < 2) {
                return 0;
            }
            if (operands[1].value != 0 && !put_space_index(fmt, instruction, 1)) {
                return 0;
            }
            return xxbyte_fmt_str(fmt, " (type ") && put_index(fmt, instruction, 0, NULL) && xxbyte_fmt_char(fmt, ')');
        case WF_MEMARG: return put_memarg(fmt, instruction, op->width);
        case WF_MEMARG_LANE: return count == 3 && put_memarg(fmt, instruction, op->width) && xxbyte_fmt_printf(fmt, " %llu", (unsigned long long)operands[2].value);
        case WF_MEMORY: return count == 1 && (operands[0].value == 0 || put_space_index(fmt, instruction, 0));
        case WF_MEMORY_COPY:
        case WF_TABLE_COPY:
            if (count != 2) {
                return 0;
            }
            if (operands[0].value == 0 && operands[1].value == 0) {
                return 1;
            }
            return put_indexes(fmt, instruction, 0);
        case WF_MEMORY_INIT:
        case WF_TABLE_INIT:
            if (count != 2) {
                return 0;
            }
            if (operands[1].value != 0 && !put_space_index(fmt, instruction, 1)) {
                return 0;
            }
            return put_space_index(fmt, instruction, 0);
        case WF_I32:
        case WF_I64: return count == 1 && xxbyte_fmt_printf(fmt, " %lld", (long long)operands[0].value);
        case WF_F32: return count == 1 && xxbyte_fmt_char(fmt, ' ') && put_f32(fmt, (uint32_t)operands[0].value);
        case WF_F64: return count == 1 && xxbyte_fmt_char(fmt, ' ') && put_f64(fmt, (uint64_t)operands[0].value);
        case WF_V128: return put_v128(fmt, instruction);
        case WF_SHUFFLE: return put_shuffle(fmt, instruction);
        case WF_HEAPTYPE:
        case WF_REF:
        case WF_REF_NULL: return count == 1 && xxbyte_fmt_char(fmt, ' ') && put_type(fmt, instruction, 0, NULL);
        case WF_BR_ON_CAST:
            return count == 4 && put_space_index(fmt, instruction, 1) && xxbyte_fmt_char(fmt, ' ') && put_type(fmt, instruction, 2, NULL) && xxbyte_fmt_char(fmt, ' ') &&
                   put_type(fmt, instruction, 3, NULL);
        default: return put_indexes(fmt, instruction, 0);
    }
}

const xxbyte_family_ops xxbyte_wasm_ops = {
    XXBYTE_FAMILY_WASM, XXBYTE_WASM_MODE_DEFAULT, wasm_check_mode, wasm_decode, wasm_switch_entry, wasm_format_operands, NULL, xxbyte_wasm_list_file,
};

const char *xxbyte_wasm_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_WASM_ID_COUNT) {
        return NULL;
    }
    return xxbyte_wasm_mnemonics[id - 1];
}

/* ---- function bodies and modules */

xxbyte_status xxbyte_wasm_parse_function_body_ex(uint32_t mode, const uint8_t *data, size_t size, xxbyte_wasm_function_body *body)
{
    xxbyte_wasm_function_body parsed;
    wasm_reader r;
    uint64_t groups;
    uint64_t total = 0;
    uint64_t i;
    uint32_t length;
    xxbyte_status status;

    if (body == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(body, 0, sizeof(*body));
    if ((data == NULL && size != 0) || size > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (wasm_check_mode(mode) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    memset(&parsed, 0, sizeof(parsed));
    r.code = data;
    r.size = size;
    r.pos = 0;
    status = read_u32(&r, &groups, &length);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    parsed.locals_offset = (uint32_t)r.pos;
    /* Every entry takes at least two bytes, so the loop ends within size. */
    for (i = 0; i < groups; ++i) {
        uint64_t count;
        xxbyte_operand type;
        status = read_u32(&r, &count, &length);
        if (status == XXBYTE_STATUS_OK) {
            status = read_value_type(&r, mode, &type);
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        total += count;
        if (total > UINT32_MAX) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    parsed.local_group_count = (uint32_t)groups;
    parsed.local_count = (uint32_t)total;
    parsed.code_offset = (uint32_t)r.pos;
    parsed.code_size = (uint32_t)(size - r.pos);
    *body = parsed;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_wasm_parse_function_body(const uint8_t *data, size_t size, xxbyte_wasm_function_body *body)
{
    return xxbyte_wasm_parse_function_body_ex(XXBYTE_WASM_MODE_DEFAULT, data, size, body);
}

/* A reader over the locals vector of *body, or 0 when *body does not fit
 * data[0..size). */
static int locals_reader(const uint8_t *data, size_t size, const xxbyte_wasm_function_body *body, wasm_reader *r)
{
    if (data == NULL || body == NULL || body->code_offset > size || body->locals_offset > body->code_offset) {
        return 0;
    }
    r->code = data;
    r->size = body->code_offset;
    r->pos = body->locals_offset;
    return 1;
}

/* The parse validated the value types in its mode; they are read back with
 * every feature, which accepts a superset and yields the same operands. */
xxbyte_status xxbyte_wasm_local_group(const uint8_t *data, size_t size, const xxbyte_wasm_function_body *body, uint32_t index, uint32_t *count, xxbyte_operand *type)
{
    wasm_reader r;
    local_entry entry;

    if (count == NULL || type == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    memset(type, 0, sizeof(*type));
    if (!locals_reader(data, size, body, &r) ||
        read_entry(&r, body->local_group_count, 2, read_local_entry, XXBYTE_WASM_MODE_DEFAULT, index, &entry) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = entry.count;
    *type = entry.type;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_wasm_local_group_next(const uint8_t *data, size_t size, const xxbyte_wasm_function_body *body, uint32_t *offset, uint32_t *count,
                                           xxbyte_operand *type)
{
    wasm_reader r;
    local_entry entry;
    xxbyte_status status;

    if (count == NULL || type == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *count = 0;
    memset(type, 0, sizeof(*type));
    if (offset == NULL || !locals_reader(data, size, body, &r)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = next_entry(&r, offset, read_local_entry, XXBYTE_WASM_MODE_DEFAULT, &entry);
    if (status == XXBYTE_STATUS_OK) {
        *count = entry.count;
        *type = entry.type;
    }
    return status;
}

xxbyte_status xxbyte_wasm_check_header(const uint8_t *data, size_t size)
{
    static const uint8_t header[8] = {0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00};
    size_t n = size < sizeof(header) ? size : sizeof(header);

    if (data == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (memcmp(data, header, n) != 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return n < sizeof(header) ? XXBYTE_STATUS_TRUNCATED : XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_wasm_next_section(const uint8_t *data, size_t size, size_t *offset, xxbyte_wasm_section *section)
{
    xxbyte_wasm_section parsed;
    wasm_reader r;
    uint64_t value;
    uint32_t length;
    xxbyte_status status;

    if (section != NULL) {
        memset(section, 0, sizeof(*section));
    }
    if (data == NULL || offset == NULL || section == NULL || *offset > size || size > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (*offset == size) {
        return XXBYTE_STATUS_END_OF_INPUT;
    }
    memset(&parsed, 0, sizeof(parsed));
    parsed.offset = (uint32_t)*offset;
    parsed.id = data[*offset];
    if (parsed.id > XXBYTE_WASM_SECTION_TAG) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    r.code = data;
    r.size = size;
    r.pos = *offset + 1u;
    status = read_u32(&r, &value, &length);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (value > size - r.pos) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    parsed.content_offset = (uint32_t)r.pos;
    parsed.content_size = (uint32_t)value;
    if (parsed.id == XXBYTE_WASM_SECTION_CUSTOM) {
        r.size = r.pos + (size_t)value;
        status = read_u32(&r, &value, &length);
        if (status != XXBYTE_STATUS_OK || value > r.size - r.pos) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        parsed.name_offset = (uint32_t)r.pos;
        parsed.name_size = (uint32_t)value;
    }
    *offset = (size_t)parsed.content_offset + parsed.content_size;
    *section = parsed;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_wasm_code_begin(uint32_t mode, const uint8_t *data, size_t size, const xxbyte_wasm_section *section, xxbyte_wasm_code_reader *reader)
{
    wasm_reader r;
    uint64_t count;
    uint32_t length;
    xxbyte_status status;

    if (reader != NULL) {
        memset(reader, 0, sizeof(*reader));
    }
    if (data == NULL || section == NULL || reader == NULL || section->id != XXBYTE_WASM_SECTION_CODE ||
        (uint64_t)section->content_offset + section->content_size > size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (wasm_check_mode(mode) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    r.code = data;
    r.size = (size_t)section->content_offset + section->content_size;
    r.pos = section->content_offset;
    status = read_u32(&r, &count, &length);
    if (status != XXBYTE_STATUS_OK) {
        return status == XXBYTE_STATUS_TRUNCATED ? XXBYTE_STATUS_INVALID_INSTRUCTION : status;
    }
    reader->mode = mode;
    reader->count = (uint32_t)count;
    reader->offset = (uint32_t)r.pos;
    reader->end = (uint32_t)r.size;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_wasm_code_next(const uint8_t *data, size_t size, xxbyte_wasm_code_reader *reader, xxbyte_wasm_function *function)
{
    xxbyte_wasm_function parsed;
    wasm_reader r;
    uint64_t body_size;
    uint32_t length;
    xxbyte_status status;

    if (function != NULL) {
        memset(function, 0, sizeof(*function));
    }
    if (data == NULL || reader == NULL || function == NULL || reader->end > size || reader->offset > reader->end) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (reader->next >= reader->count) {
        return reader->offset == reader->end ? XXBYTE_STATUS_END_OF_INPUT : XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    r.code = data;
    r.size = reader->end;
    r.pos = reader->offset;
    status = read_u32(&r, &body_size, &length);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (body_size > r.size - r.pos) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    memset(&parsed, 0, sizeof(parsed));
    parsed.index = reader->next;
    parsed.body_offset = (uint32_t)r.pos;
    parsed.body_size = (uint32_t)body_size;
    status = xxbyte_wasm_parse_function_body_ex(reader->mode, data + r.pos, (size_t)body_size, &parsed.body);
    if (status != XXBYTE_STATUS_OK) {
        return status == XXBYTE_STATUS_TRUNCATED ? XXBYTE_STATUS_INVALID_INSTRUCTION : status;
    }
    parsed.code_offset = parsed.body_offset + parsed.body.code_offset;
    parsed.code_size = parsed.body.code_size;
    reader->offset = parsed.body_offset + parsed.body_size;
    reader->next++;
    *function = parsed;
    return XXBYTE_STATUS_OK;
}

#else
typedef int xxbyte_wasm_disabled;
#endif
