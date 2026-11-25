#include "xxbyte_vb_file.h"
#include "xxbyte_vb_tables.h"

#if XXBYTE_USE_VB

#include <string.h>

#define VB_LEAD_FIRST 0xFBu
#define VB_FADF_HAVEVARTYPE 0x80u

static xxbyte_status vb_check_mode(uint32_t mode)
{
    if (mode == XXBYTE_VB_MODE_DEFAULT || mode == XXBYTE_VB_MODE_VB5 || mode == XXBYTE_VB_MODE_VB6) {
        return XXBYTE_STATUS_OK;
    }
    return XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static uint32_t vb_field_size(char c)
{
    switch (c) {
        case 'b':
        case 'B':
        case 'x': return 1;
        case 'd':
        case 'D':
        case 'f':
        case 'Y': return 4;
        case 'F':
        case 'y': return 8;
        default: return 2;
    }
}

/* OnErrorGoto FFFE disables the handler (On Error GoTo 0), FFFF selects
 * Resume Next, and 0 clears the active error without changing the handler;
 * Resume FFFE retries the failed statement and FFFF resumes after it. */
static int vb_special_target(char c, uint32_t value)
{
    if (value == 0xFFFFu || value == 0xFFFEu) {
        return 1;
    }
    return c == 'e' && value == 0;
}

/* Fills *op with the field of kind c encoded at p. */
static void vb_read_field(char c, const uint8_t *p, xxbyte_operand *op)
{
    memset(op, 0, sizeof(*op));
    op->type = XXBYTE_OPERAND_IMMEDIATE;
    op->size = vb_field_size(c);
    switch (c) {
        case 'a':
            op->type = XXBYTE_OPERAND_INDEX;
            op->kind = XXBYTE_INDEX_FRAME;
            op->flags = XXBYTE_OPERAND_FLAG_SIGNED;
            op->value = xxbyte_sext(xxbyte_u16le(p), 16);
            break;
        case 'c':
        case 't':
            op->type = XXBYTE_OPERAND_INDEX;
            op->kind = XXBYTE_INDEX_CONSTANT;
            op->value = xxbyte_u16le(p);
            break;
        case 'i':
            op->type = XXBYTE_OPERAND_INDEX;
            op->kind = XXBYTE_INDEX_IMPORT;
            op->value = xxbyte_u16le(p);
            break;
        case 'v':
            op->type = XXBYTE_OPERAND_INDEX;
            op->kind = XXBYTE_INDEX_VTABLE;
            op->value = xxbyte_u16le(p);
            break;
        case 'l':
            op->type = XXBYTE_OPERAND_BRANCH;
            op->value = xxbyte_u16le(p);
            break;
        case 'e':
        case 'r':
            op->value = xxbyte_u16le(p);
            if (!vb_special_target(c, (uint32_t)op->value)) {
                op->type = XXBYTE_OPERAND_BRANCH;
            }
            break;
        case 'M':
        case 'w':
            op->flags = XXBYTE_OPERAND_FLAG_SIGNED;
            op->value = xxbyte_sext(xxbyte_u16le(p), 16);
            break;
        case 'b':
        case 'u':
        case 'x': op->value = p[0]; break;
        case 'B':
            op->flags = XXBYTE_OPERAND_FLAG_SIGNED;
            op->value = xxbyte_sext(p[0], 8);
            break;
        case 'd':
        case 'Y':
            op->flags = XXBYTE_OPERAND_FLAG_SIGNED;
            op->value = xxbyte_sext(xxbyte_u32le(p), 32);
            break;
        case 'D': op->value = (int64_t)xxbyte_u32le(p); break;
        case 'f':
            op->type = XXBYTE_OPERAND_FLOAT;
            op->value = (int64_t)xxbyte_u32le(p);
            break;
        case 'F':
            op->type = XXBYTE_OPERAND_FLOAT;
            op->flags = XXBYTE_OPERAND_FLAG_WIDE;
            op->value = (int64_t)xxbyte_u64le(p);
            break;
        case 'y':
            op->flags = XXBYTE_OPERAND_FLAG_SIGNED | XXBYTE_OPERAND_FLAG_WIDE;
            op->value = (int64_t)xxbyte_u64le(p);
            break;
        default: /* m, n */ op->value = xxbyte_u16le(p); break;
    }
}

/*
 * The Redim element type at p, followed by the element size and the FADF
 * flags. MSVBVM60 passes the word itself, a VARTYPE, when the flags have
 * FADF_HAVEVARTYPE; both runtimes pass 0 for FFFF and otherwise load the
 * constant-pool entry (an IRecordInfo or IID).
 */
static void vb_element_type(uint32_t mode, const uint8_t *p, xxbyte_operand *op)
{
    if (op->value == 0xFFFF || (mode != XXBYTE_VB_MODE_VB5 && (xxbyte_u16le(p + 4) & VB_FADF_HAVEVARTYPE) != 0)) {
        op->type = XXBYTE_OPERAND_IMMEDIATE;
        op->kind = XXBYTE_INDEX_NONE;
    }
}

static int vb_add_fields(xxbyte_instruction *instruction, uint32_t mode, const char *fields, const char *end, const uint8_t *p)
{
    for (; fields < end; ++fields) {
        xxbyte_operand *op;

        if (instruction->operand_count >= XXBYTE_MAX_OPERANDS) {
            return 0;
        }
        op = &instruction->operands[instruction->operand_count++];
        vb_read_field(*fields, p, op);
        if (*fields == 't') {
            vb_element_type(mode, p, op);
        }
        p += vb_field_size(*fields);
    }
    return 1;
}

/* The fixed fields and the list kind of a variable-length layout. */
static void vb_split_layout(const char *layout, const char **fields_end, char *entry, uint32_t *fixed)
{
    const char *p;

    *fixed = 0;
    for (p = layout + 1; *p != ':'; ++p) {
        *fixed += vb_field_size(*p);
    }
    *fields_end = p;
    *entry = p[1];
}

static xxbyte_status vb_decode_variable(uint32_t mode, const char *layout, const uint8_t *code, size_t code_size, uint32_t base, xxbyte_instruction *instruction)
{
    const char *fields_end;
    char entry;
    uint32_t fixed;
    uint32_t entry_size;
    uint32_t count;
    uint64_t total;

    vb_split_layout(layout, &fields_end, &entry, &fixed);
    entry_size = vb_field_size(entry);
    if (code_size < (size_t)base + 2u) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    count = xxbyte_u16le(code + base);
    switch (entry) {
        case 'a':
            /* FFree*: a signed byte count; the handler frees at least one. */
            if (count < 2u || (count & 1u) != 0 || count >= 0x8000u) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            break;
        case 'l':
            /* OnGoto, OnGosub: a signed byte count of 16-bit targets. */
            if ((count & 1u) != 0 || count >= 0x8000u) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            break;
        case 'x': break;
        default:
            /* Named arguments: the fixed fields, then whole entries. */
            if (count < fixed || (count - fixed) % entry_size != 0) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            break;
    }
    total = (uint64_t)base + 2u + count;
    if (total > code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (!vb_add_fields(instruction, mode, layout + 1, fields_end, code + base + 2u)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->table_offset = base + 2u + fixed;
    instruction->table_count = (count - fixed) / entry_size;
    instruction->size = (uint32_t)total;
    return XXBYTE_STATUS_OK;
}

/* The table entry of a decoded opcode (lead << 8 | byte for FB..FF). */
static const vb_op *vb_op_of(uint32_t opcode)
{
    if (opcode < VB_LEAD_FIRST) {
        return &xxbyte_vb_ops_primary[opcode];
    }
    if (opcode >= (VB_LEAD_FIRST << 8) && opcode <= 0xFFFFu) {
        return &xxbyte_vb_ops_lead[(opcode >> 8) - VB_LEAD_FIRST][opcode & 0xFFu];
    }
    return NULL;
}

/* The operand layout of op in mode; see VB_OP_VB5_NO_VTABLE. */
static const char *vb_op_layout(const vb_op *op, uint32_t mode)
{
    const char *layout = xxbyte_vb_mnemonics[op->id].layout;

    if (mode == XXBYTE_VB_MODE_VB5 && (op->flags & VB_OP_VB5_NO_VTABLE) != 0) {
        return layout + 1;
    }
    return layout;
}

static xxbyte_status vb_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const vb_op *op;
    const vb_mnemonic *mnemonic;
    const char *layout;
    uint32_t base = 1;
    uint32_t size;
    xxbyte_status status;

    (void)context;
    (void)address;
    if (code[0] >= VB_LEAD_FIRST) {
        if (code_size < 2) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        op = &xxbyte_vb_ops_lead[code[0] - VB_LEAD_FIRST][code[1]];
        instruction->opcode = ((uint32_t)code[0] << 8) | code[1];
        base = 2;
    } else {
        op = &xxbyte_vb_ops_primary[code[0]];
        instruction->opcode = code[0];
    }
    if (op->id == 0 || (mode == XXBYTE_VB_MODE_VB5 && (op->flags & VB_OP_VB6) != 0)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    mnemonic = &xxbyte_vb_mnemonics[op->id];
    layout = vb_op_layout(op, mode);
    if (layout[0] == '*') {
        status = vb_decode_variable(mode, layout, code, code_size, base, instruction);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    } else {
        size = op->size;
        if (layout != mnemonic->layout) {
            size -= vb_field_size(mnemonic->layout[0]);
        }
        if (code_size < size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (!vb_add_fields(instruction, mode, layout, layout + strlen(layout), code + base)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        instruction->size = size;
    }
    instruction->id = op->id;
    instruction->group = mnemonic->group;
    instruction->mnemonic = mnemonic->name;
    return XXBYTE_STATUS_OK;
}

/* The operand layout a VB instruction was decoded with, or NULL. */
static const char *vb_layout_of(const xxbyte_instruction *instruction)
{
    const vb_op *op;

    if (instruction->family != (uint32_t)XXBYTE_FAMILY_VB || instruction->id == 0 || instruction->id >= XXBYTE_VB_ID_COUNT || instruction->mnemonic == NULL) {
        return NULL;
    }
    op = vb_op_of(instruction->opcode);
    if (op != NULL && op->id == instruction->id) {
        return vb_op_layout(op, instruction->mode);
    }
    return xxbyte_vb_mnemonics[instruction->id].layout;
}

static xxbyte_status vb_list_operand(const xxbyte_instruction *instruction, uint32_t index, xxbyte_operand *entry)
{
    const char *layout = vb_layout_of(instruction);
    const char *fields_end;
    char kind;
    uint32_t fixed;
    uint64_t offset;

    if (layout == NULL || layout[0] != '*' || instruction->code == NULL || index >= instruction->table_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    vb_split_layout(layout, &fields_end, &kind, &fixed);
    offset = (uint64_t)instruction->table_offset + (uint64_t)index * vb_field_size(kind);
    if (offset + vb_field_size(kind) > instruction->size) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    vb_read_field(kind, instruction->code + offset, entry);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status vb_switch_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target)
{
    xxbyte_operand entry;
    xxbyte_status status = vb_list_operand(instruction, index, &entry);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (entry.type != XXBYTE_OPERAND_BRANCH) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *key = (int64_t)index + 1;
    *target = entry.value;
    return XXBYTE_STATUS_OK;
}

static unsigned long long vb_magnitude(int64_t value)
{
    return value < 0 ? 0ull - (unsigned long long)value : (unsigned long long)value;
}

/* Currency: the raw value in units of 1/10000, as an exact decimal. */
static int vb_fmt_currency(xxbyte_fmt *fmt, int64_t raw)
{
    unsigned long long magnitude = vb_magnitude(raw);
    unsigned fraction = (unsigned)(magnitude % 10000u);
    char digits[5];
    int length = 4;

    if (!xxbyte_fmt_printf(fmt, "%s%llu", raw < 0 ? "-" : "", magnitude / 10000u)) {
        return 0;
    }
    if (fraction == 0) {
        return 1;
    }
    digits[0] = (char)('0' + fraction / 1000u);
    digits[1] = (char)('0' + fraction / 100u % 10u);
    digits[2] = (char)('0' + fraction / 10u % 10u);
    digits[3] = (char)('0' + fraction % 10u);
    while (digits[length - 1] == '0') {
        --length;
    }
    digits[length] = '\0';
    return xxbyte_fmt_printf(fmt, ".%s", digits);
}

static int vb_fmt_value(xxbyte_fmt *fmt, char c, const xxbyte_operand *op)
{
    switch (c) {
        case 'a':
            if (op->value < 0) {
                return xxbyte_fmt_printf(fmt, "var_%llX", vb_magnitude(op->value));
            }
            return xxbyte_fmt_printf(fmt, "arg_%llX", (unsigned long long)op->value);
        case 'c': return xxbyte_fmt_printf(fmt, "const_%04llX", (unsigned long long)op->value);
        case 't':
            if (op->type == XXBYTE_OPERAND_INDEX) {
                return xxbyte_fmt_printf(fmt, "const_%04llX", (unsigned long long)op->value);
            }
            return xxbyte_fmt_printf(fmt, "0x%llX", (unsigned long long)op->value);
        case 'i': return xxbyte_fmt_printf(fmt, "imp_%04llX", (unsigned long long)op->value);
        case 'v': return xxbyte_fmt_printf(fmt, "vt_%04llX", (unsigned long long)op->value);
        case 'l':
        case 'e':
        case 'r':
            if (op->type == XXBYTE_OPERAND_BRANCH) {
                return xxbyte_fmt_printf(fmt, "loc_%04llX", (unsigned long long)op->value);
            }
            return xxbyte_fmt_printf(fmt, "0x%llX", (unsigned long long)op->value);
        case 'M':
            if (op->value < 0) {
                return xxbyte_fmt_printf(fmt, "-0x%llX", vb_magnitude(op->value));
            }
            return xxbyte_fmt_printf(fmt, "0x%llX", (unsigned long long)op->value);
        case 'B':
        case 'w':
        case 'd': return xxbyte_fmt_printf(fmt, "%lld", (long long)op->value);
        case 'u': return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)op->value);
        case 'x': return xxbyte_fmt_printf(fmt, "0x%02llX", (unsigned long long)op->value);
        case 'f': {
            float value;
            uint32_t bits = (uint32_t)op->value;
            memcpy(&value, &bits, sizeof(value));
            return xxbyte_fmt_float(fmt, value);
        }
        case 'F': {
            double value;
            uint64_t bits = (uint64_t)op->value;
            memcpy(&value, &bits, sizeof(value));
            return xxbyte_fmt_double(fmt, value);
        }
        case 'y':
        case 'Y': return vb_fmt_currency(fmt, op->value);
        default: /* b, m, n, D */ return xxbyte_fmt_printf(fmt, "0x%llX", (unsigned long long)op->value);
    }
}

static int vb_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const char *fields = vb_layout_of(instruction);
    const char *fields_end;
    char entry = '\0';
    uint32_t fixed;
    uint32_t i;

    if (fields == NULL) {
        return 0;
    }
    if (fields[0] == '*') {
        vb_split_layout(fields, &fields_end, &entry, &fixed);
        ++fields;
    } else {
        fields_end = fields + strlen(fields);
    }
    if (instruction->operand_count != (uint32_t)(fields_end - fields)) {
        return 0;
    }
    for (i = 0; i < instruction->operand_count; ++i) {
        const xxbyte_operand *op = &instruction->operands[i];
        int resolved = 0;

        if (!xxbyte_fmt_str(fmt, i == 0 ? " " : ", ")) {
            return 0;
        }
        if (op->type == XXBYTE_OPERAND_INDEX || op->type == XXBYTE_OPERAND_BRANCH) {
            resolved = xxbyte_fmt_resolve(fmt, instruction, i);
            if (resolved < 0) {
                return 0;
            }
        }
        if (resolved == 0 && !vb_fmt_value(fmt, fields[i], op)) {
            return 0;
        }
    }
    if (entry == '\0' || instruction->table_count == 0) {
        return 1;
    }
    /* List entries are not operands; the resolver sees each INDEX or BRANCH
     * entry through xxbyte_fmt_resolve_entry. */
    for (i = 0; i < instruction->table_count; ++i) {
        xxbyte_operand op;
        const char *separator = ", ";
        int resolved = 0;

        if (vb_list_operand(instruction, i, &op) != XXBYTE_STATUS_OK) {
            return 0;
        }
        if (i == 0) {
            separator = instruction->operand_count != 0 ? ", {" : " ";
        }
        if (!xxbyte_fmt_str(fmt, separator)) {
            return 0;
        }
        if (op.type == XXBYTE_OPERAND_INDEX || op.type == XXBYTE_OPERAND_BRANCH) {
            resolved = xxbyte_fmt_resolve_entry(fmt, instruction, &op);
            if (resolved < 0) {
                return 0;
            }
        }
        if (resolved == 0 && !vb_fmt_value(fmt, entry, &op)) {
            return 0;
        }
    }
    return instruction->operand_count == 0 || xxbyte_fmt_char(fmt, '}');
}

xxbyte_status xxbyte_vb_list_entry(const xxbyte_instruction *instruction, uint32_t index, xxbyte_operand *entry)
{
    if (entry == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(entry, 0, sizeof(*entry));
    if (instruction == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return vb_list_operand(instruction, index, entry);
}

xxbyte_status xxbyte_vb_parse_proc_desc(const uint8_t *data, size_t size, xxbyte_vb_proc_desc *desc)
{
    if (desc == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(desc, 0, sizeof(*desc));
    if (data == NULL && size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < XXBYTE_VB_PROC_DESC_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    desc->object_info = xxbyte_u32le(data);
    desc->field_4 = xxbyte_u16le(data + 4);
    desc->frame_size = xxbyte_u16le(data + 6);
    desc->proc_size = xxbyte_u16le(data + 8);
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_vb_locate_proc(const uint8_t *data, size_t size, size_t desc_offset, xxbyte_vb_proc_desc *desc, size_t *code_offset)
{
    xxbyte_status status;

    if (code_offset == NULL || desc == NULL) {
        if (desc != NULL) {
            memset(desc, 0, sizeof(*desc));
        }
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *code_offset = 0;
    if (data == NULL) {
        memset(desc, 0, sizeof(*desc));
        return size != 0 ? XXBYTE_STATUS_INVALID_ARGUMENT : XXBYTE_STATUS_TRUNCATED;
    }
    if (desc_offset > size) {
        memset(desc, 0, sizeof(*desc));
        return XXBYTE_STATUS_TRUNCATED;
    }
    status = xxbyte_vb_parse_proc_desc(data + desc_offset, size - desc_offset, desc);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (desc->proc_size == 0 || desc->proc_size > desc_offset) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *code_offset = desc_offset - desc->proc_size;
    return XXBYTE_STATUS_OK;
}

/* Raises *max_target to the target of op when it branches into
 * [0, limit). */
static void vb_note_target(const xxbyte_operand *op, size_t limit, size_t *max_target)
{
    if (op->type == XXBYTE_OPERAND_BRANCH && op->value >= 0 && (uint64_t)op->value < (uint64_t)limit && (size_t)op->value > *max_target) {
        *max_target = (size_t)op->value;
    }
}

xxbyte_status xxbyte_vb_proc_code_size(uint32_t mode, const uint8_t *code, size_t proc_size, size_t *code_size)
{
    xxbyte_status status;
    size_t offset = 0;
    size_t max_target = 0;

    if (code_size == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *code_size = 0;
    if (code == NULL && proc_size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    status = vb_check_mode(mode);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    while (offset < proc_size) {
        xxbyte_instruction instruction;
        uint32_t i;

        status = xxbyte_decode(XXBYTE_FAMILY_VB, mode, code + offset, proc_size - offset, offset, &instruction);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        for (i = 0; i < instruction.operand_count; ++i) {
            vb_note_target(&instruction.operands[i], proc_size, &max_target);
        }
        for (i = 0; i < instruction.table_count; ++i) {
            xxbyte_operand entry;

            if (vb_list_operand(&instruction, i, &entry) == XXBYTE_STATUS_OK) {
                vb_note_target(&entry, proc_size, &max_target);
            }
        }
        offset += instruction.size;
        /* Code continues past a STOP only where a branch lands. */
        if ((instruction.group & XXBYTE_GROUP_STOP) != 0 && proc_size - offset <= 3u && max_target < offset) {
            *code_size = offset;
            return XXBYTE_STATUS_OK;
        }
    }
    return XXBYTE_STATUS_END_OF_INPUT;
}

const xxbyte_family_ops xxbyte_vb_ops = {
    XXBYTE_FAMILY_VB, XXBYTE_VB_MODE_DEFAULT, vb_check_mode, vb_decode, vb_switch_entry, vb_format_operands, NULL, xxbyte_vb_list_file,
};

#else
typedef int xxbyte_vb_disabled;
#endif
