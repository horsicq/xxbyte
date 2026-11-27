/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_quickjs_internal.h"

#if XXBYTE_USE_QUICKJS

#define QJS_NO_IMPLICIT (-128)

const qjs_version *qjs_find_version(uint32_t mode)
{
    uint32_t i;

    for (i = 0; i < QJS_MODE_ALIAS_COUNT; ++i) {
        if (xxbyte_quickjs_mode_aliases[i].mode == mode) {
            return &xxbyte_quickjs_versions[xxbyte_quickjs_mode_aliases[i].version];
        }
    }
    return NULL;
}

static xxbyte_status quickjs_check_mode(uint32_t mode)
{
    return qjs_find_version(mode) != NULL ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

const char *xxbyte_quickjs_mode_name(uint32_t mode)
{
    const qjs_version *version = qjs_find_version(mode);
    return version != NULL ? version->name : NULL;
}

uint32_t xxbyte_quickjs_bc_version(uint32_t mode)
{
    const qjs_version *version = qjs_find_version(mode);
    return version != NULL ? version->bc_version : 0u;
}

uint32_t xxbyte_quickjs_opcode_count(uint32_t mode)
{
    const qjs_version *version = qjs_find_version(mode);
    return version != NULL ? version->op_count : 0u;
}

const char *xxbyte_quickjs_mnemonic(uint32_t id)
{
    return id != 0 && id < XXBYTE_QUICKJS_ID_COUNT ? xxbyte_quickjs_mnemonics[id] : NULL;
}

const char *xxbyte_quickjs_opname(uint32_t mode, uint32_t opcode)
{
    const qjs_version *version = qjs_find_version(mode);

    if (version == NULL || opcode >= version->op_count) {
        return NULL;
    }
    return xxbyte_quickjs_mnemonics[xxbyte_quickjs_ops_desc[version->ops[opcode]].id];
}

uint32_t xxbyte_quickjs_atom_end(uint32_t mode)
{
    const qjs_version *version = qjs_find_version(mode);
    return version != NULL ? version->atom_count + 1u : 0u;
}

const char *xxbyte_quickjs_builtin_atom(uint32_t mode, uint32_t atom)
{
    return qjs_builtin_text(qjs_find_version(mode), atom);
}

static uint32_t qjs_unit(const uint8_t *bytes, size_t i, int wide)
{
    return wide ? (uint32_t)xxbyte_u16le(bytes + 2 * i) : (uint32_t)bytes[i];
}

static int qjs_is_ident(const uint8_t *bytes, size_t length, int wide)
{
    size_t i;

    if (length == 0) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        uint32_t c = qjs_unit(bytes, i, wide);
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' || (i > 0 && c >= '0' && c <= '9'))) {
            return 0;
        }
    }
    return 1;
}

int qjs_put_quoted(xx_buf_t *out, const uint8_t *bytes, size_t length, int wide, size_t limit)
{
    size_t n = length < limit ? length : limit;
    size_t i;

    if (!xx_buf_append_char(out, '"')) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        uint32_t c = qjs_unit(bytes, i, wide);
        const char *escape = NULL;
        int ok;

        switch (c) {
            case '\t': escape = "\\t"; break;
            case '\r': escape = "\\r"; break;
            case '\n': escape = "\\n"; break;
            case '\b': escape = "\\b"; break;
            case '\f': escape = "\\f"; break;
            case '\\': escape = "\\\\"; break;
            case '"': escape = "\\\""; break;
            default: break;
        }
        if (escape != NULL) {
            ok = xx_buf_append_str(out, escape);
        } else if (c >= 0x20u && c <= 0x7Eu) {
            ok = xx_buf_append_char(out, (char)c);
        } else {
            ok = xx_buf_appendf(out, "\\u%04x", (unsigned)c);
        }
        if (!ok) {
            return 0;
        }
    }
    if (n < length && !xx_buf_append_str(out, "...")) {
        return 0;
    }
    return xx_buf_append_char(out, '"') ? 1 : 0;
}

int qjs_put_atom_text(xx_buf_t *out, const uint8_t *bytes, size_t length, int wide)
{
    size_t i;

    if (!qjs_is_ident(bytes, length, wide)) {
        return qjs_put_quoted(out, bytes, length, wide, length);
    }
    for (i = 0; i < length; ++i) {
        if (!xx_buf_append_char(out, (char)qjs_unit(bytes, i, wide))) {
            return 0;
        }
    }
    return 1;
}

static void qjs_add(xxbyte_instruction *instruction, xxbyte_operand_type type, uint8_t kind, uint16_t flags, uint32_t size, int64_t value, uint64_t extra)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, type, size, value);
    if (operand != NULL) {
        operand->kind = kind;
        operand->flags = flags;
        operand->extra = extra;
    }
}

static void qjs_add_atom(const qjs_version *version, xxbyte_instruction *instruction, uint32_t raw)
{
    if ((raw & 0x80000000u) != 0) {
        qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_QUICKJS_INDEX_INT_ATOM, 0, 4, (int64_t)(raw & 0x7FFFFFFFu), raw);
    } else if (raw <= version->atom_count) {
        qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_QUICKJS_INDEX_BUILTIN_ATOM, 0, 4, (int64_t)raw, raw);
    } else {
        qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_QUICKJS_INDEX_ATOM, 0, 4, (int64_t)(raw - version->atom_count - 1u), raw);
    }
}

static void qjs_add_branch(xxbyte_instruction *instruction, uint64_t base, int64_t rel, uint32_t size)
{
    qjs_add(instruction, XXBYTE_OPERAND_BRANCH, 0, XXBYTE_OPERAND_FLAG_SIGNED, size, (int64_t)(base + (uint64_t)rel), (uint64_t)rel);
}

static xxbyte_status quickjs_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const qjs_version *version = qjs_find_version(mode);
    const qjs_op *op;
    const uint8_t *p = code + 1;
    uint16_t implicit = XXBYTE_OPERAND_FLAG_IMPLICIT;

    (void)context;
    if (code[0] >= version->op_count) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    op = &xxbyte_quickjs_ops_desc[version->ops[code[0]]];
    if (op->size > code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    instruction->size = op->size;
    instruction->opcode = code[0];
    instruction->id = op->id;
    instruction->group = op->group;
    instruction->mnemonic = xxbyte_quickjs_mnemonics[op->id];
    switch (op->fmt) {
        case QJS_FMT_NONE_INT: qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, (uint16_t)(implicit | XXBYTE_OPERAND_FLAG_SIGNED), 0, op->imp0, 0); break;
        case QJS_FMT_NPOPX: qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, implicit, 0, op->imp0, 0); break;
        case QJS_FMT_NONE_LOC:
            qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_LOCAL, implicit, 0, op->imp0, 0);
            if (op->imp1 != QJS_NO_IMPLICIT) {
                qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_LOCAL, implicit, 0, op->imp1, 0);
            }
            break;
        case QJS_FMT_NONE_ARG: qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_ARGUMENT, implicit, 0, op->imp0, 0); break;
        case QJS_FMT_NONE_VAR_REF: qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_UPVALUE, implicit, 0, op->imp0, 0); break;
        case QJS_FMT_U8: qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 1, p[0], 0); break;
        case QJS_FMT_I8: qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, XXBYTE_OPERAND_FLAG_SIGNED, 1, (int8_t)p[0], 0); break;
        case QJS_FMT_LOC8: qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_LOCAL, 0, 1, p[0], 0); break;
        case QJS_FMT_CONST8: qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_CONSTANT, 0, 1, p[0], 0); break;
        case QJS_FMT_LABEL8: qjs_add_branch(instruction, address + 1u, (int8_t)p[0], 1); break;
        case QJS_FMT_U16:
        case QJS_FMT_NPOP: qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 2, xxbyte_u16le(p), 0); break;
        case QJS_FMT_I16: qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, XXBYTE_OPERAND_FLAG_SIGNED, 2, (int16_t)xxbyte_u16le(p), 0); break;
        case QJS_FMT_LABEL16: qjs_add_branch(instruction, address + 1u, (int16_t)xxbyte_u16le(p), 2); break;
        case QJS_FMT_NPOP_U16:
            qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 2, xxbyte_u16le(p), 0);
            qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 2, xxbyte_u16le(p + 2), 0);
            break;
        case QJS_FMT_LOC: qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_LOCAL, 0, 2, xxbyte_u16le(p), 0); break;
        case QJS_FMT_ARG: qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_ARGUMENT, 0, 2, xxbyte_u16le(p), 0); break;
        case QJS_FMT_VAR_REF: qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_UPVALUE, 0, 2, xxbyte_u16le(p), 0); break;
        case QJS_FMT_U32: qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 4, xxbyte_u32le(p), 0); break;
        case QJS_FMT_U32X2:
            qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 4, xxbyte_u32le(p), 0);
            qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 4, xxbyte_u32le(p + 4), 0);
            break;
        case QJS_FMT_I32: qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, XXBYTE_OPERAND_FLAG_SIGNED, 4, (int32_t)xxbyte_u32le(p), 0); break;
        case QJS_FMT_CONST: qjs_add(instruction, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_CONSTANT, 0, 4, xxbyte_u32le(p), 0); break;
        case QJS_FMT_LABEL: qjs_add_branch(instruction, address + 1u, (int32_t)xxbyte_u32le(p), 4); break;
        case QJS_FMT_ATOM: qjs_add_atom(version, instruction, xxbyte_u32le(p)); break;
        case QJS_FMT_ATOM_U8:
            qjs_add_atom(version, instruction, xxbyte_u32le(p));
            qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 1, p[4], 0);
            break;
        case QJS_FMT_ATOM_U16:
            qjs_add_atom(version, instruction, xxbyte_u32le(p));
            qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 2, xxbyte_u16le(p + 4), 0);
            break;
        case QJS_FMT_ATOM_LABEL_U8:
        case QJS_FMT_ATOM_LABEL_U16:
            qjs_add_atom(version, instruction, xxbyte_u32le(p));
            qjs_add_branch(instruction, address + 5u, (int32_t)xxbyte_u32le(p + 4), 4);
            if (op->fmt == QJS_FMT_ATOM_LABEL_U8) {
                qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 1, p[8], 0);
            } else {
                qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 2, xxbyte_u16le(p + 8), 0);
            }
            break;
        case QJS_FMT_LABEL_U16:
            qjs_add_branch(instruction, address + 1u, (int32_t)xxbyte_u32le(p), 4);
            qjs_add(instruction, XXBYTE_OPERAND_IMMEDIATE, 0, 0, 2, xxbyte_u16le(p + 4), 0);
            break;
        default: break;
    }
    return XXBYTE_STATUS_OK;
}

static int qjs_format_atom(const xxbyte_instruction *instruction, uint32_t index, xxbyte_fmt *fmt)
{
    const xxbyte_operand *operand = &instruction->operands[index];
    const char *text;
    int r = xxbyte_fmt_resolve(fmt, instruction, index);

    if (r != 0) {
        return r > 0;
    }
    switch (operand->kind) {
        case XXBYTE_QUICKJS_INDEX_INT_ATOM: return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
        case XXBYTE_QUICKJS_INDEX_BUILTIN_ATOM:
            if (operand->value == 0) {
                return xxbyte_fmt_str(fmt, "<null>");
            }
            text = qjs_builtin_text(qjs_find_version(instruction->mode), (uint32_t)operand->value);
            if (text != NULL) {
                return qjs_put_atom_text(fmt->out, (const uint8_t *)text, strlen(text), 0);
            }
            return xxbyte_fmt_printf(fmt, "atom:%lld", (long long)operand->value);
        default: return xxbyte_fmt_printf(fmt, "atom:%lld", (long long)operand->value);
    }
}

static int quickjs_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;

    for (i = 0; i < instruction->operand_count; ++i) {
        const xxbyte_operand *operand = &instruction->operands[i];
        int r;

        if (!xxbyte_fmt_str(fmt, i == 0 ? " " : ", ")) {
            return 0;
        }
        switch (operand->type) {
            case XXBYTE_OPERAND_IMMEDIATE:
                if (!xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value)) {
                    return 0;
                }
                break;
            case XXBYTE_OPERAND_BRANCH:
                r = xxbyte_fmt_resolve(fmt, instruction, i);
                if (r < 0 || (r == 0 && !xxbyte_fmt_printf(fmt, "0x%04llx", (unsigned long long)(uint64_t)operand->value))) {
                    return 0;
                }
                break;
            case XXBYTE_OPERAND_INDEX:
                if (operand->kind >= XXBYTE_QUICKJS_INDEX_ATOM) {
                    if (!qjs_format_atom(instruction, i, fmt)) {
                        return 0;
                    }
                } else {
                    size_t mark;
                    if (!xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value)) {
                        return 0;
                    }
                    mark = xxbyte_fmt_mark(fmt);
                    if (!xxbyte_fmt_str(fmt, ": ")) {
                        return 0;
                    }
                    r = xxbyte_fmt_resolve(fmt, instruction, i);
                    if (r < 0) {
                        return 0;
                    }
                    if (r == 0) {
                        xxbyte_fmt_rewind(fmt, mark);
                    }
                }
                break;
            default: break;
        }
    }
    return 1;
}

const xxbyte_family_ops xxbyte_quickjs_ops = {
    XXBYTE_FAMILY_QUICKJS, XXBYTE_QUICKJS_MODE_DEFAULT, quickjs_check_mode, quickjs_decode, NULL, quickjs_format_operands, NULL, xxbyte_quickjs_list_file,
};

#else
typedef int xxbyte_quickjs_disabled;
#endif
