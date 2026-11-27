/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_autoit_internal.h"

#if XXBYTE_USE_AUTOIT

/* Keyword indexes with a group. */
#define KW_EXITLOOP 18u
#define KW_CONTINUELOOP 19u
#define KW_CONTINUECASE 25u
#define KW_RETURN 34u
#define KW_EXIT 35u

static const char *const mnemonics[XXBYTE_AUTOIT_ID_COUNT] = {
    "keyword_index",
    "function_index",
    "int32",
    "int64",
    "double",
    "keyword",
    "function",
    "macro",
    "variable",
    "identifier",
    "property",
    "string",
    "directive",
    ",",
    "=",
    ">",
    "<",
    "<>",
    ">=",
    "<=",
    "(",
    ")",
    "+",
    "-",
    "/",
    "*",
    "&",
    "[",
    "]",
    "==",
    "^",
    "+=",
    "-=",
    "/=",
    "*=",
    "&=",
    "?",
    ":",
    "eol",
};

/* Token byte of the string tokens 0x30-0x37, in id order. */
#define STRING_TOKEN_FIRST 0x30u
#define STRING_TOKEN_LAST 0x37u
#define OPERATOR_FIRST 0x40u
#define OPERATOR_LAST 0x58u

const char *xxbyte_autoit_keyword_name(uint32_t index)
{
    return index < XXBYTE_AUTOIT_KEYWORD_COUNT ? xxbyte_autoit_keywords[index] : NULL;
}

const char *xxbyte_autoit_function_name(uint32_t index)
{
    return index < XXBYTE_AUTOIT_FUNCTION_COUNT ? xxbyte_autoit_functions[index] : NULL;
}

const char *xxbyte_autoit_macro_name(uint32_t index)
{
    return index < XXBYTE_AUTOIT_MACRO_COUNT ? xxbyte_autoit_macros[index] : NULL;
}

const char *xxbyte_autoit_mnemonic(uint32_t id)
{
    return id >= 1 && id <= XXBYTE_AUTOIT_ID_COUNT ? mnemonics[id - 1] : NULL;
}

static xxbyte_status autoit_check_mode(uint32_t mode)
{
    return mode == XXBYTE_AUTOIT_MODE_V3 ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static void set_id(xxbyte_instruction *instruction, uint32_t id, uint32_t size)
{
    instruction->id = id;
    instruction->mnemonic = mnemonics[id - 1];
    instruction->size = size;
}

static uint32_t keyword_group(uint32_t index)
{
    switch (index) {
        case KW_RETURN: return XXBYTE_GROUP_RETURN;
        case KW_EXIT: return XXBYTE_GROUP_STOP;
        case KW_EXITLOOP:
        case KW_CONTINUELOOP:
        case KW_CONTINUECASE: return XXBYTE_GROUP_JUMP;
        default: return XXBYTE_GROUP_NONE;
    }
}

/* The keyword-table index of a name the table holds. */
static size_t name_index(const char *name)
{
    size_t i;

    for (i = 0; i < XXBYTE_AUTOIT_KEYWORD_COUNT; ++i) {
        if (xxbyte_autoit_keywords[i] == name) {
            return i;
        }
    }
    return 0;
}

static xxbyte_status autoit_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    uint32_t token = code[0];
    xxbyte_operand *operand;

    (void)mode;
    (void)context;
    (void)address;
    instruction->opcode = token;
    if (token == 0x00 || token == 0x01) {
        uint32_t index;
        if (code_size < 5) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        index = xxbyte_u32le(code + 1);
        set_id(instruction, token == 0 ? XXBYTE_AUTOIT_ID_KEYWORD_INDEX : XXBYTE_AUTOIT_ID_FUNCTION_INDEX, 5);
        xxbyte_add_index(instruction, token == 0 ? (xxbyte_index_kind)XXBYTE_AUTOIT_INDEX_KEYWORD : (xxbyte_index_kind)XXBYTE_AUTOIT_INDEX_FUNCTION, 4, (int64_t)index);
        instruction->group = token == 0x01 ? XXBYTE_GROUP_CALL : keyword_group(index);
        return XXBYTE_STATUS_OK;
    }
    if (token == 0x05) {
        if (code_size < 5) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        set_id(instruction, XXBYTE_AUTOIT_ID_INT32, 5);
        operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, xxbyte_sext(xxbyte_u32le(code + 1), 32));
        operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
        return XXBYTE_STATUS_OK;
    }
    if (token == 0x10 || token == 0x20) {
        if (code_size < 9) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (token == 0x10) {
            set_id(instruction, XXBYTE_AUTOIT_ID_INT64, 9);
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 8, (int64_t)xxbyte_u64le(code + 1));
            operand->flags = XXBYTE_OPERAND_FLAG_SIGNED | XXBYTE_OPERAND_FLAG_WIDE;
        } else {
            set_id(instruction, XXBYTE_AUTOIT_ID_DOUBLE, 9);
            xxbyte_add_operand(instruction, XXBYTE_OPERAND_FLOAT, 8, (int64_t)xxbyte_u64le(code + 1));
        }
        return XXBYTE_STATUS_OK;
    }
    if (token >= STRING_TOKEN_FIRST && token <= STRING_TOKEN_LAST) {
        uint32_t count;
        uint64_t total;
        if (code_size < 5) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        count = xxbyte_u32le(code + 1);
        total = 5u + 2u * (uint64_t)count;
        if (total > code_size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        set_id(instruction, XXBYTE_AUTOIT_ID_KEYWORD + (token - STRING_TOKEN_FIRST), (uint32_t)total);
        operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BYTES, 2u * count, 5);
        operand->extra = count;
        if (token == STRING_TOKEN_FIRST + 1) {
            instruction->group = XXBYTE_GROUP_CALL;
        } else if (token == STRING_TOKEN_FIRST) {
            const char *name = xxbyte_autoit_lookup(xxbyte_autoit_keywords, XXBYTE_AUTOIT_KEYWORD_COUNT, code + 5, count);
            instruction->group = name != NULL ? keyword_group((uint32_t)(name_index(name))) : XXBYTE_GROUP_NONE;
        }
        return XXBYTE_STATUS_OK;
    }
    if (token >= OPERATOR_FIRST && token <= OPERATOR_LAST) {
        set_id(instruction, XXBYTE_AUTOIT_ID_COMMA + (token - OPERATOR_FIRST), 1);
        return XXBYTE_STATUS_OK;
    }
    if (token == XXBYTE_AUTOIT_TOKEN_EOL) {
        set_id(instruction, XXBYTE_AUTOIT_ID_EOL, 1);
        return XXBYTE_STATUS_OK;
    }
    return XXBYTE_STATUS_INVALID_INSTRUCTION;
}

/* ---- text of tokens ---- */

/* Unit i of a string token's text. */
static uint32_t unit_at(const uint8_t *p, uint32_t count, uint32_t i)
{
    return (uint32_t)(xxbyte_u16le(p + 2u * i) ^ (uint16_t)count);
}

static int put_utf8(xxbyte_fmt *fmt, uint32_t cp)
{
    char b[4];
    size_t n;

    if (cp < 0x80) {
        b[0] = (char)cp;
        n = 1;
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        n = 2;
    } else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        n = 3;
    } else {
        b[0] = (char)(0xF0 | (cp >> 18));
        b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[3] = (char)(0x80 | (cp & 0x3F));
        n = 4;
    }
    return xx_buf_append(fmt->out, b, n) ? 1 : 0;
}

/* Reads the code point at *i (advancing it); returns 0 for an unpaired
 * surrogate (its unit in *cp). */
static int next_code_point(const uint8_t *p, uint32_t count, uint32_t *i, uint32_t *cp)
{
    uint32_t u = unit_at(p, count, *i);

    *i += 1;
    *cp = u;
    if (u >= 0xD800 && u <= 0xDBFF && *i < count) {
        uint32_t v = unit_at(p, count, *i);
        if (v >= 0xDC00 && v <= 0xDFFF) {
            *i += 1;
            *cp = 0x10000u + ((u - 0xD800u) << 10) + (v - 0xDC00u);
            return 1;
        }
    }
    return !(u >= 0xD800 && u <= 0xDFFF);
}

static int put_name(xxbyte_fmt *fmt, const uint8_t *p, uint32_t count)
{
    uint32_t i = 0;

    while (i < count) {
        uint32_t cp;
        if (!next_code_point(p, count, &i, &cp) || cp < 0x20 || cp == 0x7F) {
            cp = 0xFFFD;
        }
        if (!put_utf8(fmt, cp)) {
            return 0;
        }
    }
    return 1;
}

static int put_string(xxbyte_fmt *fmt, const uint8_t *p, uint32_t count)
{
    uint32_t i = 0;

    if (!xxbyte_fmt_char(fmt, '"')) {
        return 0;
    }
    while (i < count) {
        uint32_t cp;
        int ok;
        if (!next_code_point(p, count, &i, &cp)) {
            ok = xxbyte_fmt_printf(fmt, "\" & ChrW(%u) & \"", (unsigned)cp);
        } else if ((cp < 0x20 && cp != '\t') || cp == 0x7F) {
            ok = xxbyte_fmt_printf(fmt, "\" & Chr(%u) & \"", (unsigned)cp);
        } else if (cp == '"') {
            ok = xxbyte_fmt_str(fmt, "\"\"");
        } else {
            ok = put_utf8(fmt, cp);
        }
        if (!ok) {
            return 0;
        }
    }
    return xxbyte_fmt_char(fmt, '"');
}

static int ascii_upper(uint32_t c)
{
    return (c >= 'a' && c <= 'z') ? (int)(c - 'a' + 'A') : (int)c;
}

const char *xxbyte_autoit_lookup(const char *const *table, size_t count, const uint8_t *p, uint32_t n)
{
    size_t k;

    for (k = 0; k < count; ++k) {
        const char *name = table[k];
        uint32_t i;
        for (i = 0; i < n && name[i] != '\0'; ++i) {
            uint32_t u = unit_at(p, n, i);
            if (u >= 0x80 || ascii_upper(u) != ascii_upper((unsigned char)name[i])) {
                break;
            }
        }
        if (i == n && name[i] == '\0') {
            return name;
        }
    }
    return NULL;
}

static int put_double(xxbyte_fmt *fmt, uint64_t bits)
{
    double value;
    size_t mark = xxbyte_fmt_mark(fmt);
    size_t i;

    memcpy(&value, &bits, sizeof(value));
    if (!xxbyte_fmt_double(fmt, value)) {
        return 0;
    }
    for (i = mark; i < fmt->out->size; ++i) {
        char c = fmt->out->data[i];
        if (c == '.' || c == 'e' || c == 'n' || c == 'i') {
            return 1;
        }
    }
    return xxbyte_fmt_str(fmt, ".0");
}

static int put_index(const xxbyte_instruction *instruction, xxbyte_fmt *fmt, int resolve)
{
    const xxbyte_operand *operand = &instruction->operands[0];
    const char *name;
    int r = resolve ? xxbyte_fmt_resolve(fmt, instruction, 0) : 0;

    if (r != 0) {
        return r > 0;
    }
    name =
        instruction->id == XXBYTE_AUTOIT_ID_KEYWORD_INDEX ? xxbyte_autoit_keyword_name((uint32_t)operand->value) : xxbyte_autoit_function_name((uint32_t)operand->value);
    if (name != NULL) {
        return xxbyte_fmt_str(fmt, name);
    }
    return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)operand->value);
}

static int token_text(const xxbyte_instruction *instruction, xxbyte_fmt *fmt, int resolve)
{
    const xxbyte_operand *operand = &instruction->operands[0];
    const uint8_t *p;
    uint32_t n;
    const char *canon = NULL;

    switch (instruction->id) {
        case XXBYTE_AUTOIT_ID_KEYWORD_INDEX:
        case XXBYTE_AUTOIT_ID_FUNCTION_INDEX: return put_index(instruction, fmt, resolve);
        case XXBYTE_AUTOIT_ID_INT32:
        case XXBYTE_AUTOIT_ID_INT64: return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
        case XXBYTE_AUTOIT_ID_DOUBLE: return put_double(fmt, (uint64_t)operand->value);
        default: break;
    }
    if (instruction->id < XXBYTE_AUTOIT_ID_KEYWORD || instruction->id > XXBYTE_AUTOIT_ID_DIRECTIVE) {
        return xxbyte_fmt_str(fmt, instruction->mnemonic);
    }
    p = instruction->code + operand->value;
    n = (uint32_t)operand->extra;
    switch (instruction->id) {
        case XXBYTE_AUTOIT_ID_KEYWORD: canon = xxbyte_autoit_lookup(xxbyte_autoit_keywords, XXBYTE_AUTOIT_KEYWORD_COUNT, p, n); break;
        case XXBYTE_AUTOIT_ID_FUNCTION: canon = xxbyte_autoit_lookup(xxbyte_autoit_functions, XXBYTE_AUTOIT_FUNCTION_COUNT, p, n); break;
        case XXBYTE_AUTOIT_ID_MACRO:
            if (!xxbyte_fmt_char(fmt, '@')) {
                return 0;
            }
            canon = xxbyte_autoit_lookup(xxbyte_autoit_macros, XXBYTE_AUTOIT_MACRO_COUNT, p, n);
            break;
        case XXBYTE_AUTOIT_ID_VARIABLE:
            if (!xxbyte_fmt_char(fmt, '$')) {
                return 0;
            }
            break;
        case XXBYTE_AUTOIT_ID_PROPERTY:
            if (!xxbyte_fmt_char(fmt, '.')) {
                return 0;
            }
            break;
        case XXBYTE_AUTOIT_ID_STRING: return put_string(fmt, p, n);
        default: break;
    }
    if (canon != NULL) {
        return xxbyte_fmt_str(fmt, canon);
    }
    return put_name(fmt, p, n);
}

int xxbyte_autoit_token_source(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    return token_text(instruction, fmt, 0);
}

static int autoit_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    if (instruction->operand_count == 0) {
        return 1;
    }
    return xxbyte_fmt_char(fmt, ' ') && token_text(instruction, fmt, 1);
}

const xxbyte_family_ops xxbyte_autoit_ops = {
    XXBYTE_FAMILY_AUTOIT, XXBYTE_AUTOIT_MODE_DEFAULT, autoit_check_mode, autoit_decode, NULL, autoit_format_operands, NULL, xxbyte_autoit_list_file,
};

#else
typedef int xxbyte_autoit_disabled;
#endif
