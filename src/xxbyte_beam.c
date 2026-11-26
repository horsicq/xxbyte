#include "xxbyte_beam_internal.h"

#if XXBYTE_USE_BEAM

/*
 * Erlang/OTP BEAM generic instructions: decoder and formatter. See
 * include/xxbyte/xxbyte_beam.h for the operand model and the text syntax.
 */

#define BEAM_NESTED_LENGTH_DEPTH 4u

static const struct {
    uint8_t release;
    uint8_t max_opcode;
} beam_releases[] = {
    {17, 158}, {18, 158}, {19, 158}, {20, 159}, {21, 163}, {22, 168}, {23, 170}, {24, 176}, {25, 180}, {26, 182}, {27, 183}, {28, 184}, {29, 191},
};

uint32_t xxbyte_beam_release(uint32_t mode)
{
    if (mode == XXBYTE_BEAM_MODE_DEFAULT) {
        return XXBYTE_BEAM_MODE_OTP_MAX;
    }
    if (mode >= XXBYTE_BEAM_MODE_OTP_MIN && mode <= XXBYTE_BEAM_MODE_OTP_MAX) {
        return mode;
    }
    return 0;
}

uint32_t xxbyte_beam_max_opcode(uint32_t mode)
{
    uint32_t release = xxbyte_beam_release(mode);
    size_t i;

    for (i = 0; i < sizeof(beam_releases) / sizeof(beam_releases[0]); ++i) {
        if (beam_releases[i].release == release) {
            return beam_releases[i].max_opcode;
        }
    }
    return 0;
}

uint32_t xxbyte_beam_mode_for_max_opcode(uint32_t max_opcode)
{
    size_t i;

    for (i = 0; i < sizeof(beam_releases) / sizeof(beam_releases[0]); ++i) {
        if (beam_releases[i].max_opcode >= max_opcode) {
            return beam_releases[i].release;
        }
    }
    return XXBYTE_BEAM_MODE_OTP_MAX;
}

const char *xxbyte_beam_mnemonic(uint32_t opcode)
{
    return (opcode >= 1 && opcode <= XXBYTE_BEAM_OPCODE_MAX) ? xxbyte_beam_opinfo_table[opcode].name : NULL;
}

uint32_t xxbyte_beam_arity(uint32_t opcode)
{
    return (opcode >= 1 && opcode <= XXBYTE_BEAM_OPCODE_MAX) ? xxbyte_beam_opinfo_table[opcode].arity : 0u;
}

uint32_t xxbyte_beam_opcode_since(uint32_t opcode)
{
    return (opcode >= 1 && opcode <= XXBYTE_BEAM_OPCODE_MAX) ? xxbyte_beam_opinfo_table[opcode].since : 0u;
}

uint32_t xxbyte_beam_opcode_deprecated(uint32_t opcode)
{
    return (opcode >= 1 && opcode <= XXBYTE_BEAM_OPCODE_MAX) ? xxbyte_beam_opinfo_table[opcode].deprecated : 0u;
}

static xxbyte_status beam_check_mode(uint32_t mode)
{
    return xxbyte_beam_release(mode) != 0 ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

/* ---------------------------------------------------------------------- */
/* Tagged numbers (erts beam_file.c beamreader_read_tagged). */

typedef struct beam_number {
    uint32_t tag;      /* raw tag 0..7 */
    int big;           /* value does not fit: bytes at big_offset */
    uint64_t value;    /* the value (i: int64 bits) */
    size_t big_offset; /* from code[0] */
    size_t big_size;
} beam_number;

/* Number of significant bytes after stripping redundant leading bytes. */
static int beam_fits(const uint8_t *p, size_t count, int is_signed, uint64_t *value)
{
    size_t i = 0;
    uint64_t v = 0;

    if (is_signed) {
        int negative = (p[0] & 0x80) != 0;
        uint8_t fill = negative ? 0xFF : 0x00;
        while (count - i > 8 && p[i] == fill && ((p[i + 1] & 0x80) != 0) == negative) {
            ++i;
        }
        if (count - i > 8) {
            return 0;
        }
        v = negative ? ~UINT64_C(0) : 0;
        for (; i < count; ++i) {
            v = (v << 8) | p[i];
        }
        *value = v;
        return 1;
    }
    while (count - i > 8 && p[i] == 0) {
        ++i;
    }
    if (count - i > 8 || (count - i == 8 && (p[i] & 0x80) != 0)) {
        return 0;
    }
    for (; i < count; ++i) {
        v = (v << 8) | p[i];
    }
    *value = v;
    return 1;
}

static xxbyte_status beam_read_number(const uint8_t *code, size_t size, size_t *offset, beam_number *n, unsigned depth)
{
    size_t off = *offset;
    uint8_t b;

    if (off >= size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    b = code[off++];
    n->tag = b & 7u;
    n->big = 0;
    n->big_offset = 0;
    n->big_size = 0;
    if ((b & 0x08) == 0) {
        n->value = (uint64_t)(b >> 4);
    } else if ((b & 0x10) == 0) {
        if (off >= size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        n->value = ((uint64_t)(b >> 5) << 8) | code[off++];
    } else {
        size_t count;
        uint32_t length_code = (uint32_t)(b >> 5);

        if (length_code < 7) {
            count = length_code + 2u;
        } else {
            beam_number prefix;
            xxbyte_status status;

            if (depth >= BEAM_NESTED_LENGTH_DEPTH) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            status = beam_read_number(code, size, &off, &prefix, depth + 1);
            if (status != XXBYTE_STATUS_OK) {
                return status;
            }
            if (prefix.tag != XXBYTE_BEAM_TAG_U || prefix.big || prefix.value >= UINT64_C(0x7FFFFFFF) - 9u) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            count = (size_t)prefix.value + 9u;
        }
        if (size - off < count) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (!beam_fits(code + off, count, n->tag == XXBYTE_BEAM_TAG_I, &n->value)) {
            n->big = 1;
            n->value = 0;
            n->big_offset = off;
            n->big_size = count;
        }
        off += count;
    }
    *offset = off;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status beam_read_small_u(const uint8_t *code, size_t size, size_t *offset, uint64_t *value)
{
    beam_number n;
    xxbyte_status status = beam_read_number(code, size, offset, &n, 0);

    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (n.tag != XXBYTE_BEAM_TAG_U || n.big) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    *value = n.value;
    return XXBYTE_STATUS_OK;
}

static void beam_set(xxbyte_operand *op, xxbyte_operand_type type, uint32_t tag, int64_t value)
{
    memset(op, 0, sizeof(*op));
    op->type = (uint8_t)type;
    op->value = value;
    op->extra = tag;
}

xxbyte_status xxbyte_beam_term_at(uint32_t mode, const uint8_t *code, size_t code_size, size_t offset, int in_list, beam_term *term)
{
    beam_number n;
    size_t off = offset;
    xxbyte_operand *op = &term->op;
    xxbyte_status status;

    status = beam_read_number(code, code_size, &off, &n, 0);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    term->start = offset;
    if (n.tag != 7u) {
        if (n.big) {
            if (n.tag != XXBYTE_BEAM_TAG_U && n.tag != XXBYTE_BEAM_TAG_I) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            beam_set(op, XXBYTE_OPERAND_BYTES, n.tag, (int64_t)n.big_offset);
            op->size = (uint32_t)n.big_size;
            if (n.tag == XXBYTE_BEAM_TAG_I) {
                op->flags = XXBYTE_OPERAND_FLAG_SIGNED;
            }
            term->end = off;
            return off - offset > UINT32_MAX ? XXBYTE_STATUS_INVALID_INSTRUCTION : XXBYTE_STATUS_OK;
        }
        switch (n.tag) {
            case XXBYTE_BEAM_TAG_U:
            case XXBYTE_BEAM_TAG_H: beam_set(op, XXBYTE_OPERAND_IMMEDIATE, n.tag, (int64_t)n.value); break;
            case XXBYTE_BEAM_TAG_I:
                beam_set(op, XXBYTE_OPERAND_IMMEDIATE, n.tag, (int64_t)n.value);
                op->flags = XXBYTE_OPERAND_FLAG_SIGNED;
                break;
            case XXBYTE_BEAM_TAG_A:
                beam_set(op, XXBYTE_OPERAND_INDEX, n.tag, (int64_t)n.value);
                op->kind = (uint8_t)XXBYTE_BEAM_INDEX_ATOM;
                break;
            case XXBYTE_BEAM_TAG_X:
            case XXBYTE_BEAM_TAG_Y: beam_set(op, XXBYTE_OPERAND_REGISTER, n.tag, (int64_t)n.value); break;
            default: /* F */
                beam_set(op, XXBYTE_OPERAND_INDEX, n.tag, (int64_t)n.value);
                op->kind = (uint8_t)XXBYTE_INDEX_LABEL;
                break;
        }
    } else {
        uint64_t value;

        if (n.big) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        switch (n.value) {
            case 0: /* float */
                if (code_size - off < 8) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                {
                    uint64_t bits = 0;
                    unsigned i;
                    for (i = 0; i < 8; ++i) {
                        bits = (bits << 8) | code[off + i];
                    }
                    off += 8;
                    beam_set(op, XXBYTE_OPERAND_FLOAT, XXBYTE_BEAM_TAG_FLOAT, (int64_t)bits);
                }
                term->end = off;
                op->size = 8;
                return XXBYTE_STATUS_OK;
            case 1: /* extended list */
                if (in_list) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                status = beam_read_small_u(code, code_size, &off, &value);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                if (value > (UINT64_MAX >> 8)) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                beam_set(op, XXBYTE_OPERAND_IMMEDIATE, XXBYTE_BEAM_TAG_LIST, (int64_t)value);
                op->extra |= value << 8;
                break;
            case 2: /* floating-point register */
                status = beam_read_small_u(code, code_size, &off, &value);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                beam_set(op, XXBYTE_OPERAND_REGISTER, XXBYTE_BEAM_TAG_FR, (int64_t)value);
                break;
            case 3: /* allocation list */
            {
                uint64_t count, i, words = 0;

                if (in_list) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                status = beam_read_small_u(code, code_size, &off, &count);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                if (count > 3) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                for (i = 0; i < count; ++i) {
                    uint64_t kind = 0, number = 0;
                    status = beam_read_small_u(code, code_size, &off, &kind);
                    if (status == XXBYTE_STATUS_OK) {
                        status = beam_read_small_u(code, code_size, &off, &number);
                    }
                    if (status != XXBYTE_STATUS_OK) {
                        return status;
                    }
                    if (kind > 2 || number > UINT64_C(0x7FFFFFFF)) {
                        return XXBYTE_STATUS_INVALID_INSTRUCTION;
                    }
                    if (kind == 0) {
                        words += number;
                    }
                }
                beam_set(op, XXBYTE_OPERAND_IMMEDIATE, XXBYTE_BEAM_TAG_ALLOC, (int64_t)words);
                op->extra |= count << 8;
                break;
            }
            case 4: /* literal */
                status = beam_read_small_u(code, code_size, &off, &value);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                beam_set(op, XXBYTE_OPERAND_INDEX, XXBYTE_BEAM_TAG_LITERAL, (int64_t)value);
                op->kind = (uint8_t)XXBYTE_INDEX_CONSTANT;
                break;
            case 5: /* type-tagged register */
            {
                beam_number reg;

                if (xxbyte_beam_release(mode) < XXBYTE_BEAM_MODE_TYPES) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                status = beam_read_number(code, code_size, &off, &reg, 0);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                if (reg.big || (reg.tag != XXBYTE_BEAM_TAG_X && reg.tag != XXBYTE_BEAM_TAG_Y)) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                status = beam_read_small_u(code, code_size, &off, &value);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                if (value > (UINT64_MAX >> 8)) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                beam_set(op, XXBYTE_OPERAND_REGISTER, reg.tag == XXBYTE_BEAM_TAG_X ? XXBYTE_BEAM_TAG_TR_X : XXBYTE_BEAM_TAG_TR_Y, (int64_t)reg.value);
                op->extra |= value << 8;
                break;
            }
            default: return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    if (op->value < 0 && (op->flags & XXBYTE_OPERAND_FLAG_SIGNED) == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (off - offset > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    op->size = (uint32_t)(off - offset);
    term->end = off;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_beam_read_term(uint32_t mode, const uint8_t *code, size_t code_size, size_t *offset, int in_list, xxbyte_operand *operand)
{
    beam_term term;
    xxbyte_status status;

    if (operand != NULL) {
        memset(operand, 0, sizeof(*operand));
    }
    if (offset == NULL || operand == NULL || (code == NULL && code_size != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (beam_check_mode(mode) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    status = xxbyte_beam_term_at(mode, code, code_size, *offset, in_list, &term);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    *operand = term.op;
    *offset = term.end;
    return XXBYTE_STATUS_OK;
}

/* ---------------------------------------------------------------------- */
/* Decoder. */

static const uint32_t beam_group_bits[] = {
    XXBYTE_GROUP_NONE,
    XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP,
    XXBYTE_GROUP_STOP,
    XXBYTE_GROUP_CALL,
    XXBYTE_GROUP_CALL | XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP,
    XXBYTE_GROUP_CALL,
    XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP,
    XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP,
    XXBYTE_GROUP_SWITCH | XXBYTE_GROUP_STOP,
    XXBYTE_GROUP_NONE,
};

/* Applies the opcode's table role to a u operand, and a context label. */
static void beam_specialize(const xxbyte_beam_opinfo *info, uint32_t arg, const xxbyte_beam_context *ctx, xxbyte_operand *op)
{
    uint32_t tag = XXBYTE_BEAM_OPERAND_TAG(op);

    if (tag == XXBYTE_BEAM_TAG_U && op->type == XXBYTE_OPERAND_IMMEDIATE && info->role != BEAM_ROLE_NONE && info->role_arg == arg) {
        static const uint8_t kinds[] = {0, XXBYTE_INDEX_IMPORT, XXBYTE_INDEX_FUNCTION, XXBYTE_INDEX_STRING, XXBYTE_BEAM_INDEX_LINE};
        op->type = XXBYTE_OPERAND_INDEX;
        op->kind = kinds[info->role];
    } else if (tag == XXBYTE_BEAM_TAG_F && op->type == XXBYTE_OPERAND_INDEX && ctx != NULL && ctx->labels != NULL && op->value != 0 &&
               (uint64_t)op->value < ctx->label_count) {
        uint64_t address = ctx->labels[op->value];
        if (address != XXBYTE_BEAM_NO_ADDRESS) {
            op->extra = XXBYTE_BEAM_TAG_F | ((uint64_t)op->value << 8);
            op->type = XXBYTE_OPERAND_BRANCH;
            op->kind = 0;
            op->value = (int64_t)address;
        }
    }
}

static xxbyte_status beam_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const xxbyte_beam_context *ctx = (const xxbyte_beam_context *)context;
    const xxbyte_beam_opinfo *info;
    uint32_t opcode = code[0];
    uint32_t arg;
    size_t off = 1;
    int first_label = -1;

    (void)address;
    if (opcode == 0 || opcode > xxbyte_beam_max_opcode(mode)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    info = &xxbyte_beam_opinfo_table[opcode];
    for (arg = 0; arg < info->arity; ++arg) {
        beam_term term;
        xxbyte_operand *op;
        xxbyte_status status = xxbyte_beam_term_at(mode, code, code_size, off, 0, &term);

        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        off = term.end;
        if (XXBYTE_BEAM_OPERAND_TAG(&term.op) == XXBYTE_BEAM_TAG_LIST) {
            uint64_t count = (uint64_t)term.op.value, i;

            if (arg + 1u != info->arity) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            if (count > code_size - off) {
                /* every element takes at least one byte */
                return XXBYTE_STATUS_TRUNCATED;
            }
            if (off > UINT32_MAX) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            instruction->table_offset = (uint32_t)off;
            instruction->table_count = (uint32_t)count;
            for (i = 0; i < count; ++i) {
                beam_term element;
                status = xxbyte_beam_term_at(mode, code, code_size, off, 1, &element);
                if (status != XXBYTE_STATUS_OK) {
                    return status;
                }
                off = element.end;
            }
            if (off - term.start > UINT32_MAX) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            term.op.size = (uint32_t)(off - term.start);
        }
        beam_specialize(info, arg, ctx, &term.op);
        if (first_label < 0 && XXBYTE_BEAM_OPERAND_TAG(&term.op) == XXBYTE_BEAM_TAG_F) {
            first_label = (term.op.type == XXBYTE_OPERAND_BRANCH || term.op.value != 0) ? 1 : 0;
        }
        op = &instruction->operands[instruction->operand_count++];
        *op = term.op;
    }
    if (off > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->size = (uint32_t)off;
    instruction->opcode = opcode;
    instruction->id = opcode;
    instruction->mnemonic = info->name;
    instruction->group = beam_group_bits[info->group];
    if ((info->group == BEAM_G_COND || info->group == BEAM_G_CALLCOND) && first_label == 1) {
        instruction->group |= XXBYTE_GROUP_CONDITIONAL;
    }
    return XXBYTE_STATUS_OK;
}

/* Position of operand operand_index (or of the list) in the instruction. */
static xxbyte_status beam_operand_term(const xxbyte_instruction *instruction, uint32_t operand_index, beam_term *term)
{
    size_t off = 1;
    uint32_t arg;

    if (instruction == NULL || instruction->code == NULL || instruction->family != XXBYTE_FAMILY_BEAM || operand_index >= instruction->operand_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    for (arg = 0; arg <= operand_index; ++arg) {
        xxbyte_status status = xxbyte_beam_term_at(instruction->mode, instruction->code, instruction->size, off, 0, term);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        off = term->end;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_beam_list_entry(const xxbyte_instruction *instruction, uint32_t index, xxbyte_operand *entry)
{
    size_t off;
    uint32_t i;
    beam_term term;

    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
    }
    if (instruction == NULL || entry == NULL || instruction->code == NULL || instruction->family != XXBYTE_FAMILY_BEAM || index >= instruction->table_count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    off = instruction->table_offset;
    for (i = 0;; ++i) {
        xxbyte_status status = xxbyte_beam_term_at(instruction->mode, instruction->code, instruction->size, off, 1, &term);
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
        if (i == index) {
            break;
        }
        off = term.end;
    }
    if (instruction->opcode == XXBYTE_BEAM_OP_BS_CREATE_BIN && index % 6u == 4u && XXBYTE_BEAM_OPERAND_TAG(&term.op) == XXBYTE_BEAM_TAG_U &&
        term.op.type == XXBYTE_OPERAND_IMMEDIATE) {
        term.op.type = XXBYTE_OPERAND_INDEX;
        term.op.kind = (uint8_t)XXBYTE_INDEX_STRING;
    }
    *entry = term.op;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_beam_alloc_list(const xxbyte_instruction *instruction, uint32_t operand_index, uint64_t *words, uint64_t *floats, uint64_t *funs)
{
    beam_term term;
    xxbyte_status status;
    uint64_t totals[3] = {0, 0, 0};
    uint64_t count = 0, i;
    size_t off;

    if (words != NULL) {
        *words = 0;
    }
    if (floats != NULL) {
        *floats = 0;
    }
    if (funs != NULL) {
        *funs = 0;
    }
    status = beam_operand_term(instruction, operand_index, &term);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (XXBYTE_BEAM_OPERAND_TAG(&term.op) != XXBYTE_BEAM_TAG_ALLOC) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    /* skip the extended tag, then the count */
    off = term.start;
    {
        beam_number n;
        status = beam_read_number(instruction->code, instruction->size, &off, &n, 0);
        if (status == XXBYTE_STATUS_OK) {
            status = beam_read_small_u(instruction->code, instruction->size, &off, &count);
        }
        if (status != XXBYTE_STATUS_OK) {
            return status;
        }
    }
    for (i = 0; i < count; ++i) {
        uint64_t kind = 0, number = 0;
        status = beam_read_small_u(instruction->code, instruction->size, &off, &kind);
        if (status == XXBYTE_STATUS_OK) {
            status = beam_read_small_u(instruction->code, instruction->size, &off, &number);
        }
        if (status != XXBYTE_STATUS_OK || kind > 2) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        totals[kind] += number;
    }
    if (words != NULL) {
        *words = totals[0];
    }
    if (floats != NULL) {
        *floats = totals[1];
    }
    if (funs != NULL) {
        *funs = totals[2];
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_beam_scan_labels(uint32_t mode, const uint8_t *code, size_t code_size, uint64_t address, uint64_t *labels, size_t label_count)
{
    size_t i, off = 0;

    if (beam_check_mode(mode) != XXBYTE_STATUS_OK) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if ((code == NULL && code_size != 0) || (labels == NULL && label_count != 0)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    for (i = 0; i < label_count; ++i) {
        labels[i] = XXBYTE_BEAM_NO_ADDRESS;
    }
    while (off < code_size) {
        xxbyte_instruction insn;
        if (xxbyte_decode(XXBYTE_FAMILY_BEAM, mode, code + off, code_size - off, address + off, &insn) != XXBYTE_STATUS_OK) {
            ++off;
            continue;
        }
        if (insn.opcode == XXBYTE_BEAM_OP_LABEL && insn.operand_count == 1 && insn.operands[0].type == XXBYTE_OPERAND_IMMEDIATE &&
            (uint64_t)insn.operands[0].value < label_count) {
            labels[insn.operands[0].value] = address + off;
        }
        off += insn.size;
    }
    return XXBYTE_STATUS_OK;
}

/* ---------------------------------------------------------------------- */
/* Formatter. */

static int beam_fmt_u64(xxbyte_fmt *fmt, uint64_t v)
{
    return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)v);
}

/* Resolved text: 1 written, 0 numeric form, -1 failure. */
static int beam_resolve(const xxbyte_instruction *instruction, const xxbyte_operand *op, uint32_t operand_index, uint32_t list_index, xxbyte_fmt *fmt)
{
    if (fmt->resolver == NULL) {
        return 0;
    }
    if (fmt->resolver == xxbyte_beam_listing_resolver) {
        return xxbyte_beam_listing_write(fmt->context, instruction, op, operand_index, list_index, fmt);
    }
    if (list_index != UINT32_MAX) {
        return xxbyte_fmt_resolve_entry(fmt, instruction, op);
    }
    return xxbyte_fmt_resolve(fmt, instruction, operand_index);
}

static int beam_wants_resolver(const xxbyte_operand *op)
{
    uint32_t tag = XXBYTE_BEAM_OPERAND_TAG(op);

    if (op->type == XXBYTE_OPERAND_BRANCH) {
        return 1;
    }
    if (op->type == XXBYTE_OPERAND_INDEX) {
        return !(op->kind == XXBYTE_BEAM_INDEX_ATOM && op->value == 0);
    }
    return op->type == XXBYTE_OPERAND_REGISTER && (tag == XXBYTE_BEAM_TAG_TR_X || tag == XXBYTE_BEAM_TAG_TR_Y);
}

static int beam_format_alloc(const xxbyte_instruction *instruction, const beam_term *term, xxbyte_fmt *fmt)
{
    static const char *const names[] = {"words", "floats", "funs"};
    size_t off = term->start;
    beam_number n;
    uint64_t count, i;

    if (beam_read_number(instruction->code, instruction->size, &off, &n, 0) != XXBYTE_STATUS_OK ||
        beam_read_small_u(instruction->code, instruction->size, &off, &count) != XXBYTE_STATUS_OK || !xxbyte_fmt_str(fmt, "{alloc,[")) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        uint64_t kind = 0, number = 0;
        if (beam_read_small_u(instruction->code, instruction->size, &off, &kind) != XXBYTE_STATUS_OK ||
            beam_read_small_u(instruction->code, instruction->size, &off, &number) != XXBYTE_STATUS_OK || kind > 2) {
            return 0;
        }
        if (!xxbyte_fmt_printf(fmt, "%s{%s,%llu}", i == 0 ? "" : ",", names[kind], (unsigned long long)number)) {
            return 0;
        }
    }
    return xxbyte_fmt_str(fmt, "]}");
}

static int beam_format_value(const xxbyte_instruction *instruction, const xxbyte_operand *op, const beam_term *term, uint32_t operand_index, uint32_t list_index,
                             xxbyte_fmt *fmt)
{
    uint32_t tag = XXBYTE_BEAM_OPERAND_TAG(op);

    if (beam_wants_resolver(op)) {
        int r = beam_resolve(instruction, op, operand_index, list_index, fmt);
        if (r != 0) {
            return r > 0;
        }
    }
    switch (op->type) {
        case XXBYTE_OPERAND_IMMEDIATE:
            if (tag == XXBYTE_BEAM_TAG_I) {
                return xxbyte_fmt_printf(fmt, "{integer,%lld}", (long long)op->value);
            }
            if (tag == XXBYTE_BEAM_TAG_H) {
                return xxbyte_fmt_printf(fmt, "{h,%llu}", (unsigned long long)op->value);
            }
            if (tag == XXBYTE_BEAM_TAG_ALLOC) {
                return beam_format_alloc(instruction, term, fmt);
            }
            return beam_fmt_u64(fmt, (uint64_t)op->value);
        case XXBYTE_OPERAND_BYTES:
            if (tag == XXBYTE_BEAM_TAG_I && !xxbyte_fmt_str(fmt, "{integer,")) {
                return 0;
            }
            if (!xxbyte_beam_fmt_bigint(fmt, instruction->code + op->value, op->size, tag == XXBYTE_BEAM_TAG_I)) {
                return 0;
            }
            return tag == XXBYTE_BEAM_TAG_I ? xxbyte_fmt_char(fmt, '}') : 1;
        case XXBYTE_OPERAND_FLOAT: {
            double d;
            uint64_t bits = (uint64_t)op->value;
            memcpy(&d, &bits, sizeof(d));
            return xxbyte_fmt_str(fmt, "{float,") && xxbyte_beam_fmt_erlang_float(fmt, d) && xxbyte_fmt_char(fmt, '}');
        }
        case XXBYTE_OPERAND_REGISTER:
            switch (tag) {
                case XXBYTE_BEAM_TAG_X: return xxbyte_fmt_printf(fmt, "{x,%llu}", (unsigned long long)op->value);
                case XXBYTE_BEAM_TAG_Y: return xxbyte_fmt_printf(fmt, "{y,%llu}", (unsigned long long)op->value);
                case XXBYTE_BEAM_TAG_FR: return xxbyte_fmt_printf(fmt, "{fr,%llu}", (unsigned long long)op->value);
                default:
                    return xxbyte_fmt_printf(fmt, "{tr,{%c,%llu},%llu}", tag == XXBYTE_BEAM_TAG_TR_X ? 'x' : 'y', (unsigned long long)op->value,
                                             (unsigned long long)XXBYTE_BEAM_OPERAND_AUX(op));
            }
        case XXBYTE_OPERAND_BRANCH: return xxbyte_fmt_printf(fmt, "{f,%llu}", (unsigned long long)XXBYTE_BEAM_OPERAND_AUX(op));
        case XXBYTE_OPERAND_INDEX:
            switch (op->kind) {
                case XXBYTE_BEAM_INDEX_ATOM: return op->value == 0 ? xxbyte_fmt_str(fmt, "nil") : xxbyte_fmt_printf(fmt, "{a,%llu}", (unsigned long long)op->value);
                case XXBYTE_INDEX_LABEL: return xxbyte_fmt_printf(fmt, "{f,%llu}", (unsigned long long)op->value);
                case XXBYTE_INDEX_CONSTANT: return xxbyte_fmt_printf(fmt, "{lit,%llu}", (unsigned long long)op->value);
                case XXBYTE_INDEX_IMPORT: return xxbyte_fmt_printf(fmt, "{import,%llu}", (unsigned long long)op->value);
                case XXBYTE_INDEX_FUNCTION: return xxbyte_fmt_printf(fmt, "{fun,%llu}", (unsigned long long)op->value);
                default: return beam_fmt_u64(fmt, (uint64_t)op->value);
            }
        default: return 0;
    }
}

static int beam_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    size_t off = 1;
    uint32_t arg;

    if (instruction->code == NULL) {
        return 0;
    }
    for (arg = 0; arg < instruction->operand_count; ++arg) {
        const xxbyte_operand *op = &instruction->operands[arg];
        beam_term term;

        if (xxbyte_beam_term_at(instruction->mode, instruction->code, instruction->size, off, 0, &term) != XXBYTE_STATUS_OK) {
            return 0;
        }
        off = term.end;
        if (!xxbyte_fmt_str(fmt, arg == 0 ? " " : ", ")) {
            return 0;
        }
        if (XXBYTE_BEAM_OPERAND_TAG(op) == XXBYTE_BEAM_TAG_LIST) {
            uint32_t i;
            if (!xxbyte_fmt_str(fmt, "{list,[")) {
                return 0;
            }
            for (i = 0; i < instruction->table_count; ++i) {
                xxbyte_operand entry;
                beam_term element;
                if (xxbyte_beam_term_at(instruction->mode, instruction->code, instruction->size, off, 1, &element) != XXBYTE_STATUS_OK) {
                    return 0;
                }
                entry = element.op;
                if (instruction->opcode == XXBYTE_BEAM_OP_BS_CREATE_BIN && i % 6u == 4u && XXBYTE_BEAM_OPERAND_TAG(&entry) == XXBYTE_BEAM_TAG_U &&
                    entry.type == XXBYTE_OPERAND_IMMEDIATE) {
                    entry.type = XXBYTE_OPERAND_INDEX;
                    entry.kind = (uint8_t)XXBYTE_INDEX_STRING;
                }
                off = element.end;
                if ((i != 0 && !xxbyte_fmt_char(fmt, ',')) || !beam_format_value(instruction, &entry, &element, arg, i, fmt)) {
                    return 0;
                }
            }
            if (!xxbyte_fmt_str(fmt, "]}")) {
                return 0;
            }
            continue;
        }
        if (!beam_format_value(instruction, op, &term, arg, UINT32_MAX, fmt)) {
            return 0;
        }
    }
    return 1;
}

const xxbyte_family_ops xxbyte_beam_ops = {
    XXBYTE_FAMILY_BEAM, XXBYTE_BEAM_MODE_DEFAULT, beam_check_mode, beam_decode, NULL, beam_format_operands, NULL, xxbyte_beam_list_file,
};

#else
typedef int xxbyte_beam_disabled;
#endif
