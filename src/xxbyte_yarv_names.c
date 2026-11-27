/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_yarv_ibf.h"
#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#if XXBYTE_USE_YARV

/* Naming context, resolver and whole-image listing. */

#define YARV_NONE 0xFFFFFFFFu
#define YARV_ENV_DATA_SIZE 3

/* VM_CALL_* names in the order #disasm prints them, with their bits. */
typedef struct yarv_call_flag {
    const char *name;
    uint8_t bit;
} yarv_call_flag;

static const yarv_call_flag yarv_flags_2_6[] = {
    {"ARGS_SPLAT", 0}, {"ARGS_BLOCKARG", 1}, {"FCALL", 2},   {"VCALL", 3}, {"ARGS_SIMPLE", 4}, {"BLOCKISEQ", 5},
    {"TAILCALL", 8},   {"SUPER", 9},         {"ZSUPER", 10}, {"KWARG", 6}, {"KW_SPLAT", 7},    {"OPT_SEND", 11},
};
static const yarv_call_flag yarv_flags_3_0[] = {
    {"ARGS_SPLAT", 0}, {"ARGS_BLOCKARG", 1}, {"FCALL", 2}, {"VCALL", 3},    {"ARGS_SIMPLE", 4},   {"BLOCKISEQ", 5}, {"TAILCALL", 8},
    {"SUPER", 9},      {"ZSUPER", 10},       {"KWARG", 6}, {"KW_SPLAT", 7}, {"KW_SPLAT_MUT", 12}, {"OPT_SEND", 11},
};
static const yarv_call_flag yarv_flags_3_3[] = {
    {"ARGS_SPLAT", 0}, {"ARGS_BLOCKARG", 1}, {"FCALL", 2}, {"VCALL", 3},    {"ARGS_SIMPLE", 4},   {"TAILCALL", 7},
    {"SUPER", 8},      {"ZSUPER", 9},        {"KWARG", 5}, {"KW_SPLAT", 6}, {"KW_SPLAT_MUT", 11}, {"OPT_SEND", 10},
};
static const yarv_call_flag yarv_flags_3_4[] = {
    {"ARGS_SPLAT", 0}, {"ARGS_SPLAT_MUT", 12}, {"ARGS_BLOCKARG", 1}, {"FCALL", 2},    {"VCALL", 3},         {"ARGS_SIMPLE", 4}, {"TAILCALL", 7},
    {"SUPER", 8},      {"ZSUPER", 9},          {"KWARG", 5},         {"KW_SPLAT", 6}, {"KW_SPLAT_MUT", 11}, {"FORWARDING", 13}, {"OPT_SEND", 10},
};
static const yarv_call_flag yarv_flags_4_0[] = {
    {"ARGS_SPLAT", 0}, {"ARGS_SPLAT_MUT", 12}, {"ARGS_BLOCKARG", 1}, {"FCALL", 2},         {"VCALL", 3},       {"ARGS_SIMPLE", 4},         {"TAILCALL", 7},  {"SUPER", 8},
    {"ZSUPER", 9},     {"KWARG", 5},           {"KW_SPLAT", 6},      {"KW_SPLAT_MUT", 11}, {"FORWARDING", 13}, {"IGNORE_REFINEMENTS", 14}, {"OPT_SEND", 10},
};

static const yarv_call_flag *yarv_call_flags(uint32_t version, size_t *count, unsigned *kwarg)
{
    if (version >= YARV_MODE(4, 0)) {
        *count = sizeof(yarv_flags_4_0) / sizeof(yarv_flags_4_0[0]);
        *kwarg = 5;
        return yarv_flags_4_0;
    }
    if (version >= YARV_MODE(3, 4)) {
        *count = sizeof(yarv_flags_3_4) / sizeof(yarv_flags_3_4[0]);
        *kwarg = 5;
        return yarv_flags_3_4;
    }
    if (version >= YARV_MODE(3, 3)) {
        *count = sizeof(yarv_flags_3_3) / sizeof(yarv_flags_3_3[0]);
        *kwarg = 5;
        return yarv_flags_3_3;
    }
    *kwarg = 6;
    if (version >= YARV_MODE(3, 0)) {
        *count = sizeof(yarv_flags_3_0) / sizeof(yarv_flags_3_0[0]);
        return yarv_flags_3_0;
    }
    *count = sizeof(yarv_flags_2_6) / sizeof(yarv_flags_2_6[0]);
    return yarv_flags_2_6;
}

static int yarv_names_image(const xxbyte_yarv_names *names, yarv_image *image)
{
    return yarv_image_init(image, names->data, names->size, &names->header);
}

void xxbyte_yarv_names_free(xxbyte_yarv_names *names)
{
    if (names == NULL) {
        return;
    }
    xx_mem_free(names->instructions);
    xx_mem_free(names->calls);
    xx_mem_free(names->caches);
    xx_mem_free(names->slots);
    xx_mem_free(names->slot_offsets);
    xx_mem_free(names->call_infos);
    names->call_infos = NULL;
    names->call_info_count = 0;
    names->instructions = NULL;
    names->calls = NULL;
    names->caches = NULL;
    names->slots = NULL;
    names->slot_offsets = NULL;
    names->instruction_count = 0;
    names->decode.offsets = NULL;
    names->decode.slot_count = 0;
}

static int yarv_raw(uint32_t mode)
{
    return YARV_VERSION(mode) < YARV_MODE(2, 7);
}

/* Walks the code; with names->instructions allocated it records every
 * instruction, else it only counts them. */
static xxbyte_status yarv_walk(xxbyte_yarv_names *names, const uint8_t *code, size_t code_size, size_t *count)
{
    uint32_t mode = names->header.mode;
    int raw = yarv_raw(mode);
    unsigned word = (mode & XXBYTE_YARV_MODE_WORD32) != 0 ? 4u : 8u;
    size_t pos = 0;
    size_t n = 0;
    uint32_t slot = 0;
    uint32_t plain = 0;
    uint32_t keyword = 0;
    uint32_t cache = 0;
    xxbyte_status status = XXBYTE_STATUS_OK;

    while (pos < code_size) {
        xxbyte_instruction insn;
        uint32_t k;

        status = xxbyte_decode_ex(XXBYTE_FAMILY_YARV, mode, raw ? NULL : &names->decode, code + pos, code_size - pos, names->base + pos, &insn);
        if (status != XXBYTE_STATUS_OK) {
            break;
        }
        if (names->instructions != NULL) {
            unsigned call = 0;
            names->instructions[n] = (uint32_t)pos;
            names->slots[n] = raw ? (uint32_t)(pos / word) : slot;
            names->calls[2 * n] = YARV_NONE;
            names->calls[2 * n + 1] = YARV_NONE;
            names->caches[n] = YARV_NONE;
            for (k = 0; k < insn.operand_count; ++k) {
                const xxbyte_operand *op = &insn.operands[k];
                if (op->type != XXBYTE_OPERAND_INDEX) {
                    continue;
                }
                if (op->kind == XXBYTE_YARV_INDEX_CALLDATA) {
                    int kw = op->extra != 0 && YARV_VERSION(mode) < YARV_MODE(3, 0);
                    uint32_t ci = kw ? names->info.ci_size + keyword++ : plain++;
                    if (call < 2) {
                        names->calls[2 * n + call++] = ci;
                    }
                } else if (op->kind == XXBYTE_YARV_INDEX_IC) {
                    names->caches[n] = cache++;
                }
            }
        }
        slot += xxbyte_yarv_insn_len(mode, insn.opcode);
        pos += insn.size;
        ++n;
    }
    *count = n;
    names->slot_count = raw ? (uint32_t)(pos / word) : slot;
    return status;
}

xxbyte_status xxbyte_yarv_names_init(xxbyte_yarv_names *names, const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint32_t index, uint64_t base)
{
    yarv_image image;
    xxbyte_status status;
    const uint8_t *code;
    size_t code_size;
    size_t count = 0;

    if (names == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(names, 0, sizeof(*names));
    if (!yarv_image_init(&image, data, size, header)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    names->data = data;
    names->size = size;
    names->header = *header;
    names->iseq_index = index;
    names->base = base;
    if (!yarv_iseq_info(&image, index, &names->info, &status)) {
        return status;
    }
    code = data + names->info.code.code_offset;
    code_size = names->info.code.code_size;
    names->decode.base = base;
    names->decode.code_size = code_size;
    if (!yarv_raw(header->mode)) {
        size_t slots = 0;
        /* A partial map (code that stops decoding) still names the
         * instructions before the failure. */
        xxbyte_yarv_build_slot_map(header->mode, code, code_size, NULL, 0, &slots);
        if (slots != 0) {
            names->slot_offsets = (uint32_t *)xx_mem_alloc(slots * sizeof(uint32_t));
            if (names->slot_offsets == NULL) {
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            xxbyte_yarv_build_slot_map(header->mode, code, code_size, names->slot_offsets, slots, &slots);
        }
        names->decode.offsets = names->slot_offsets;
        names->decode.slot_count = slots;
    }
    yarv_walk(names, code, code_size, &count);
    if (count != 0) {
        names->instructions = (uint32_t *)xx_mem_alloc(count * sizeof(uint32_t));
        names->calls = (uint32_t *)xx_mem_alloc(count * 2 * sizeof(uint32_t));
        names->caches = (uint32_t *)xx_mem_alloc(count * sizeof(uint32_t));
        names->slots = (uint32_t *)xx_mem_alloc(count * sizeof(uint32_t));
        if (names->instructions == NULL || names->calls == NULL || names->caches == NULL || names->slots == NULL) {
            xxbyte_yarv_names_free(names);
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
    }
    status = yarv_walk(names, code, code_size, &count);
    names->instruction_count = count;
    {
        /* Every call info takes at least one byte (a word in 2.6), so a
         * count beyond the image is malformed and reads nothing. */
        size_t total = (size_t)names->info.ci_size + names->info.ci_kw_size;
        size_t read = 0;
        if (total != 0 && total <= image.size) {
            names->call_infos = (xxbyte_yarv_call_info *)xx_mem_alloc(total * sizeof(xxbyte_yarv_call_info));
            if (names->call_infos == NULL) {
                xxbyte_yarv_names_free(names);
                return XXBYTE_STATUS_OUT_OF_MEMORY;
            }
            if (xxbyte_yarv_read_call_info(data, size, header, &names->info, names->call_infos, total, &read) == XXBYTE_STATUS_OK) {
                names->call_info_count = read;
            }
        }
    }
    return status;
}

/* The ordinal of the instruction at address, or YARV_NONE. */
static size_t yarv_ordinal(const xxbyte_yarv_names *names, uint64_t address)
{
    uint64_t offset = address - names->base;
    size_t low = 0;
    size_t high = names->instruction_count;

    if (offset > UINT32_MAX) {
        return YARV_NONE;
    }
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (names->instructions[middle] < offset) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    if (low < names->instruction_count && names->instructions[low] == offset) {
        return low;
    }
    return YARV_NONE;
}

/* The address of slot pc (the end of the code for iseq_size and past). */
static uint64_t yarv_slot_address(const xxbyte_yarv_names *names, uint64_t pc)
{
    if (yarv_raw(names->header.mode)) {
        unsigned word = (names->header.mode & XXBYTE_YARV_MODE_WORD32) != 0 ? 4u : 8u;
        return names->base + pc * word;
    }
    if (pc < names->decode.slot_count) {
        return names->base + names->slot_offsets[pc];
    }
    return names->base + names->info.code.code_size + (pc - names->decode.slot_count);
}

static int yarv_put_address(yarv_text *text, uint64_t address)
{
    char buffer[24];
    xx_buf_t tmp;
    int ok;

    xx_buf_init(&tmp);
    ok = xx_buf_appendf(&tmp, "0x%04llX", (unsigned long long)address) && tmp.size < sizeof(buffer);
    if (ok) {
        memcpy(buffer, tmp.data, tmp.size);
        ok = yarv_put_bytes(text, (const uint8_t *)buffer, tmp.size);
    }
    xx_buf_free(&tmp);
    return ok;
}

static int yarv_put_number(yarv_text *text, long long value)
{
    char buffer[24];
    xx_buf_t tmp;
    int ok;

    xx_buf_init(&tmp);
    ok = xx_buf_appendf(&tmp, "%lld", value) && tmp.size < sizeof(buffer);
    if (ok) {
        memcpy(buffer, tmp.data, tmp.size);
        ok = yarv_put_bytes(text, (const uint8_t *)buffer, tmp.size);
    }
    xx_buf_free(&tmp);
    return ok;
}

/* local_var_name: the name of local op (an environment index) level
 * frames up, then "@index". */
static int yarv_local(yarv_text *text, const xxbyte_yarv_names *names, uint64_t level, int64_t op)
{
    const yarv_image *image = text->image;
    xxbyte_yarv_iseq_info info = names->info;
    xxbyte_status status;
    uint64_t i;
    int64_t idx;
    uint64_t id;
    uint64_t object;

    if (level > image->header.iseq_list_size) {
        return -1;
    }
    for (i = 0; i < level; ++i) {
        if (info.parent_iseq < 0 || !yarv_iseq_info(image, (uint32_t)info.parent_iseq, &info, &status)) {
            return -1;
        }
    }
    if (op < YARV_ENV_DATA_SIZE || op - YARV_ENV_DATA_SIZE >= (int64_t)info.local_table_size) {
        return -1;
    }
    idx = (int64_t)info.local_table_size - (op - YARV_ENV_DATA_SIZE) - 1;
    if (idx < 0 || idx >= (int64_t)info.local_table_size) {
        return -1;
    }
    if (!yarv_read_word(image, (uint64_t)info.local_table_offset + (uint64_t)idx * image->word, image->word, &id) || !yarv_id_object(image, id, &object)) {
        return -1;
    }
    return yarv_local_name(text, object) && yarv_put(text, "@") && yarv_put_number(text, idx);
}

static int yarv_call_data(yarv_text *text, const xxbyte_yarv_names *names, uint32_t ci_index)
{
    const yarv_image *image = text->image;
    xxbyte_yarv_call_info ci;
    const yarv_call_flag *flags;
    size_t flag_count;
    unsigned kwarg;
    size_t i;
    int parts = 0;

    if (ci_index == YARV_NONE) {
        return -1;
    }
    if (ci_index >= names->call_info_count) {
        return -1;
    }
    ci = names->call_infos[ci_index];
    flags = yarv_call_flags(image->version, &flag_count, &kwarg);
    if (!yarv_put(text, image->version < YARV_MODE(2, 7) ? "<callinfo!" : "<calldata!")) {
        return 0;
    }
    if (ci.mid != 0) {
        uint64_t object;
        if (!yarv_id_object(image, ci.mid, &object) || !yarv_put(text, "mid:") || !yarv_put_name(text, object)) {
            return 0;
        }
        parts = 1;
    }
    if ((parts && !yarv_put(text, ", ")) || !yarv_put(text, "argc:") || !yarv_put_number(text, (int32_t)ci.argc)) {
        return 0;
    }
    if ((ci.flag & (1u << kwarg)) != 0) {
        if (!yarv_put(text, ", kw:[")) {
            return 0;
        }
        for (i = 0; i < ci.keyword_count; ++i) {
            uint64_t keyword;
            if (xxbyte_yarv_call_info_keyword(names->data, names->size, &names->header, &ci, (uint32_t)i, &keyword) != XXBYTE_STATUS_OK) {
                return -1;
            }
            if ((i != 0 && !yarv_put(text, ",")) || !yarv_put_name(text, keyword)) {
                return 0;
            }
        }
        if (!yarv_put(text, "]")) {
            return 0;
        }
    }
    if (ci.flag != 0) {
        int first = 1;
        if (!yarv_put(text, ", ")) {
            return 0;
        }
        for (i = 0; i < flag_count; ++i) {
            if ((ci.flag & (1u << flags[i].bit)) != 0) {
                if ((!first && !yarv_put(text, "|")) || !yarv_put(text, flags[i].name)) {
                    return 0;
                }
                first = 0;
            }
        }
    }
    return yarv_put(text, ">");
}

/* The type letter of operand index (TS_* of xxbyte_yarv_operand_types). */
static char yarv_operand_type(const xxbyte_instruction *insn, uint32_t operand_index)
{
    const char *types = xxbyte_yarv_operand_types(insn->mode, insn->opcode);
    uint32_t i;

    if (types == NULL) {
        return 0;
    }
    for (i = 0; i < operand_index && types[i] != '\0'; ++i) {
    }
    return types[i];
}

/* 1 when text was written, -1 to keep the numeric form, 0 when the text
 * ran out of room. */
static int yarv_resolve_text(yarv_text *text, const xxbyte_yarv_names *names, const xxbyte_instruction *insn, uint32_t operand_index)
{
    const yarv_image *image = text->image;
    const xxbyte_operand *op = &insn->operands[operand_index];
    uint32_t version = image->version;
    uint32_t base_id = insn->id < XXBYTE_YARV_ID_COUNT ? insn->id : 0;
    size_t ordinal;
    char type;

    {
        const char *name = xxbyte_yarv_mnemonic(base_id);
        /* The trace_ and zjit_ forms name their base instruction. */
        if (name != NULL && strncmp(name, "trace_", 6) == 0) {
            name += 6;
        } else if (name != NULL && strncmp(name, "zjit_", 5) == 0) {
            name += 5;
        }
        if (name != NULL && strcmp(name, "checkkeyword") == 0 && op->type == XXBYTE_OPERAND_INDEX && op->kind == XXBYTE_INDEX_LOCAL) {
            return -1;
        }
        if (name != NULL && strcmp(name, "defined") == 0 && operand_index == 1 && op->type == XXBYTE_OPERAND_INDEX && op->kind == XXBYTE_YARV_INDEX_OBJECT) {
            xxbyte_yarv_object object;
            if (yarv_object(image, (uint64_t)op->value, &object) && object.special_const && (object.value & 1u) != 0) {
                int64_t n = xxbyte_sext(object.value, 8u * image->word) >> 1;
                if (n != 0) {
                    char buffer[2];
                    if (!yarv_put(text, ":$")) {
                        return 0;
                    }
                    if ((n & 1) != 0) {
                        buffer[0] = (char)(n >> 1);
                        buffer[1] = 0;
                        return yarv_put_bytes(text, (const uint8_t *)buffer, 1);
                    }
                    return yarv_put_number(text, n >> 1);
                }
            }
        }
    }
    if (op->type == XXBYTE_OPERAND_BRANCH) {
        return yarv_put_address(text, (uint64_t)op->value);
    }
    if (op->type != XXBYTE_OPERAND_INDEX) {
        return -1;
    }
    ordinal = yarv_ordinal(names, insn->address);
    switch (op->kind) {
        case XXBYTE_INDEX_LOCAL: return yarv_local(text, names, op->extra, op->value);
        case XXBYTE_INDEX_FUNCTION: {
            xxbyte_yarv_iseq_info child;
            xxbyte_status status;
            if (op->value == -1) {
                return yarv_put(text, "nil");
            }
            if (op->value < 0 || op->value >= (int64_t)image->header.iseq_list_size || !yarv_iseq_info(image, (uint32_t)op->value, &child, &status)) {
                return -1;
            }
            return yarv_put_name(text, child.label);
        }
        case XXBYTE_YARV_INDEX_OBJECT:
            if ((uint64_t)op->value >= image->header.object_list_size) {
                return -1;
            }
            return yarv_literal(text, (uint64_t)op->value);
        case XXBYTE_YARV_INDEX_ID: {
            uint64_t object;
            type = yarv_operand_type(insn, operand_index);
            if (op->value == 0 || !yarv_id_object(image, (uint64_t)op->value, &object) || object == 0 || object >= image->header.object_list_size) {
                return -1;
            }
            if (type == 'G') {
                return yarv_put_name(text, object);
            }
            return yarv_inspect_symbol_name(text, object);
        }
        case XXBYTE_YARV_INDEX_CDHASH: return yarv_put(text, "<cdhash>");
        case XXBYTE_YARV_INDEX_CALLCACHE: return yarv_put(text, "<callcache>");
        case XXBYTE_YARV_INDEX_IC:
            if (version < YARV_MODE(3, 2)) {
                return yarv_put(text, "<is:") && yarv_put_number(text, op->value) && yarv_put(text, ">");
            }
            if (ordinal == YARV_NONE || names->caches[ordinal] == YARV_NONE) {
                return -1;
            }
            return yarv_put(text, "<ic:") &&
                   yarv_put_number(text, (long long)names->info.ivc_size + names->info.icvarc_size + names->info.ise_size + names->caches[ordinal]) &&
                   yarv_put(text, " ") && yarv_constant_path(text, (uint64_t)op->value) && yarv_put(text, ">");
        case XXBYTE_YARV_INDEX_IVC:
        case XXBYTE_YARV_INDEX_ICVARC:
        case XXBYTE_YARV_INDEX_ISE: {
            long long start = 0;
            if (version >= YARV_MODE(3, 2)) {
                if (op->kind == XXBYTE_YARV_INDEX_ICVARC) {
                    start = names->info.ivc_size;
                } else if (op->kind == XXBYTE_YARV_INDEX_ISE) {
                    start = (long long)names->info.ivc_size + names->info.icvarc_size;
                }
            }
            return yarv_put(text, "<is:") && yarv_put_number(text, (long long)((unsigned long long)start + (unsigned long long)op->value)) && yarv_put(text, ">");
        }
        case XXBYTE_YARV_INDEX_CALLDATA: {
            uint32_t k;
            unsigned which = 0;
            if (ordinal == YARV_NONE) {
                return -1;
            }
            for (k = 0; k < operand_index; ++k) {
                if (insn->operands[k].type == XXBYTE_OPERAND_INDEX && insn->operands[k].kind == XXBYTE_YARV_INDEX_CALLDATA) {
                    ++which;
                }
            }
            if (which > 1) {
                return -1;
            }
            return yarv_call_data(text, names, names->calls[2 * ordinal + which]);
        }
        case XXBYTE_YARV_INDEX_BUILTIN: {
            const xxbyte_operand *name = operand_index + 1 < insn->operand_count ? &insn->operands[operand_index + 1] : NULL;
            if (name == NULL || name->type != XXBYTE_OPERAND_BYTES || insn->code == NULL || (uint64_t)name->value > insn->size ||
                name->size > insn->size - (uint64_t)name->value) {
                return -1;
            }
            return yarv_put(text, "<builtin!") && yarv_put_bytes(text, insn->code + name->value, name->size) && yarv_put(text, "/?>");
        }
        default: return -1;
    }
}

size_t xxbyte_yarv_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_yarv_names *names = (const xxbyte_yarv_names *)context;
    yarv_image image;
    yarv_text text;
    xx_buf_t out;
    size_t length = 0;
    int result;

    if (names == NULL || instruction == NULL || buffer == NULL || buffer_size < 8 || operand_index >= instruction->operand_count ||
        instruction->family != XXBYTE_FAMILY_YARV || !yarv_names_image(names, &image)) {
        return 0;
    }
    xx_buf_init(&out);
    yarv_text_init(&text, &image, &out, XXBYTE_RESOLVER_NAME_LIMIT - 1);
    result = yarv_resolve_text(&text, names, instruction, operand_index);
    if (result != -1 && xx_buf_ok(&out) && out.data != NULL && out.size != 0 && (result == 1 || yarv_text_full(&text))) {
        size_t copy;
        length = out.size;
        if (result == 0) {
            /* Longer than the core accepts: cut at a UTF-8 character
             * boundary and end in "...". */
            size_t keep = XXBYTE_RESOLVER_NAME_LIMIT - 4;
            while (keep > 0 && ((uint8_t)out.data[keep] & 0xC0u) == 0x80u) {
                --keep;
            }
            memcpy(out.data + keep, "...", 3);
            length = keep + 3;
        }
        /* snprintf style: the formatter asks again with a buffer that
         * holds the whole name when length >= buffer_size. */
        copy = length < buffer_size ? length : buffer_size - 1;
        memcpy(buffer, out.data, copy);
        buffer[copy] = '\0';
    }
    xx_buf_free(&out);
    return length;
}

/* Listing. */

static const char *yarv_catch_name(uint32_t type)
{
    switch (type) {
        case XXBYTE_YARV_CATCH_RESCUE: return "rescue";
        case XXBYTE_YARV_CATCH_ENSURE: return "ensure";
        case XXBYTE_YARV_CATCH_RETRY: return "retry";
        case XXBYTE_YARV_CATCH_BREAK: return "break";
        case XXBYTE_YARV_CATCH_REDO: return "redo";
        case XXBYTE_YARV_CATCH_NEXT: return "next";
        default: return "?";
    }
}

typedef struct yarv_lister {
    const uint8_t *data;
    size_t size;
    xxbyte_yarv_ibf_header header;
    yarv_image image;
    uint32_t flags;
    xx_buf_t *out;
    uint8_t *visited;
    uint32_t *stack;
    size_t stack_size;
    size_t stack_capacity;
    int first;
} yarv_lister;

static int yarv_push(yarv_lister *lister, uint32_t index)
{
    if (lister->stack_size == lister->stack_capacity) {
        size_t capacity = lister->stack_capacity == 0 ? 64 : lister->stack_capacity * 2;
        uint32_t *stack = (uint32_t *)xx_mem_realloc(lister->stack, capacity * sizeof(uint32_t));
        if (stack == NULL) {
            return 0;
        }
        lister->stack = stack;
        lister->stack_capacity = capacity;
    }
    lister->stack[lister->stack_size++] = index;
    return 1;
}

static int yarv_appendf_u(xx_buf_t *out, const char *format, unsigned long long value)
{
    return xx_buf_appendf(out, format, value) ? 1 : 0;
}

static int yarv_put_name_out(yarv_lister *lister, uint64_t index)
{
    yarv_text text;

    yarv_text_init(&text, &lister->image, lister->out, (size_t)1 << 20);
    yarv_put_name(&text, index);
    return xx_buf_ok(lister->out) ? 1 : 0;
}

static int yarv_header_line(yarv_lister *lister, const xxbyte_yarv_iseq_info *info)
{
    xx_buf_t *out = lister->out;
    uint32_t version = lister->image.version;
    uint64_t path = info->pathobj;
    xxbyte_yarv_object object;

    if (!xx_buf_append_str(out, "== disasm: #<ISeq:") || !yarv_put_name_out(lister, info->label) || !xx_buf_append_char(out, '@')) {
        return 0;
    }
    if (yarv_object(&lister->image, path, &object) && !object.special_const && object.type == XXBYTE_YARV_T_ARRAY) {
        yarv_elements it;
        yarv_elements_init(&it, &lister->image, &object);
        if (!yarv_elements_next(&it, &path)) {
            path = 0;
        }
    }
    if (!yarv_put_name_out(lister, path) ||
        !xx_buf_appendf(out, ":%d (%d,%d)-(%d,%d)>", info->beg_lineno, info->beg_lineno, info->beg_column, info->end_lineno, info->end_column)) {
        return 0;
    }
    if (version <= YARV_MODE(3, 2)) {
        const char *value = info->catch_except_p ? (version == YARV_MODE(3, 2) ? "true" : "TRUE") : (version == YARV_MODE(3, 2) ? "false" : "FALSE");
        if (!xx_buf_appendf(out, " (catch: %s)", value)) {
            return 0;
        }
    }
    if (version >= YARV_MODE(3, 4)) {
        static const char *const attrs[] = {" LEAF", " SINGLE_NOARG_LEAF", " INLINE_BLOCK", " C_TRACE"};
        unsigned i;
        for (i = 0; i < 4; ++i) {
            if ((info->builtin_attrs & (1u << i)) != 0 && !xx_buf_append_str(out, attrs[i])) {
                return 0;
            }
        }
    }
    return xx_buf_append_char(out, '\n') ? 1 : 0;
}

static void yarv_right_strip(xx_buf_t *out, size_t line_start)
{
    while (out->size > line_start && out->data[out->size - 1] == ' ') {
        --out->size;
    }
    if (out->data != NULL) {
        out->data[out->size] = '\0';
    }
}

static int yarv_local_table(yarv_lister *lister, const xxbyte_yarv_names *names)
{
    const xxbyte_yarv_iseq_info *info = &names->info;
    xx_buf_t *out = lister->out;
    uint32_t version = lister->image.version;
    uint32_t flags = info->param_flags;
    size_t line_start;
    uint32_t size = info->local_table_size;
    uint32_t i;
    int has_kw = (flags & XXBYTE_YARV_PARAM_KW) != 0;
    int has_kwrest = (flags & XXBYTE_YARV_PARAM_KWREST) != 0;

    if (size == 0) {
        return 1;
    }
    if (!xx_buf_appendf(out, "local table (size: %u, argc: %d [opts: %d, rest: %d, post: %d, block: %d, kw: %d@%d, kwrest: %d])\n", size, info->lead_num, info->opt_num,
                        (flags & XXBYTE_YARV_PARAM_REST) != 0 ? info->rest_start : -1, info->post_num, (flags & XXBYTE_YARV_PARAM_BLOCK) != 0 ? info->block_start : -1,
                        has_kw ? info->keyword_num : -1, has_kw ? info->keyword_required_num : -1, has_kwrest ? info->keyword_rest_start : -1)) {
        return 0;
    }
    if ((uint64_t)info->local_table_offset + (uint64_t)size * lister->image.word > lister->image.size) {
        /* The loader would read past the image. */
        return xx_buf_append_str(out, "(local table past the end of the image)\n") ? 1 : 0;
    }
    line_start = out->size;
    for (i = size; i > 0; --i) {
        int li = (int)(size - i);
        uint64_t id = 0;
        uint64_t object = 0;
        size_t width;
        yarv_text text;
        int modern = version >= YARV_MODE(3, 4);

        if (!xx_buf_appendf(out, "[%2u] ", i)) {
            return 0;
        }
        width = out->size + 11;
        yarv_read_word(&lister->image, (uint64_t)info->local_table_offset + (uint64_t)li * lister->image.word, lister->image.word, &id);
        if (!yarv_id_object(&lister->image, id, &object)) {
            object = 0;
        }
        yarv_text_init(&text, &lister->image, out, (size_t)1 << 20);
        yarv_local_name(&text, object);
        if (!xx_buf_ok(out) || !xx_buf_appendf(out, "@%d", li)) {
            return 0;
        }
        {
            int any = 0;
            const char *parts[6];
            int count = 0;
            int opt = (flags & XXBYTE_YARV_PARAM_OPT) != 0 && li >= info->lead_num && li < info->lead_num + info->opt_num;
            if (info->lead_num > li) {
                parts[count++] = modern && (flags & XXBYTE_YARV_PARAM_AMBIGUOUS_PARAM0) != 0 ? "AmbiguousArg" : "Arg";
            }
            if ((flags & XXBYTE_YARV_PARAM_REST) != 0 && info->rest_start == li) {
                parts[count++] = modern && (flags & XXBYTE_YARV_PARAM_ANON_REST) != 0 ? "AnonRest" : "Rest";
            }
            if ((flags & XXBYTE_YARV_PARAM_POST) != 0 && info->post_start <= li && li < info->post_start + info->post_num) {
                parts[count++] = "Post";
            }
            if (has_kwrest && info->keyword_rest_start == li) {
                parts[count++] = modern && (flags & XXBYTE_YARV_PARAM_ANON_KWREST) != 0 ? "AnonKwrest" : "Kwrest";
            }
            if ((flags & XXBYTE_YARV_PARAM_BLOCK) != 0 && info->block_start == li) {
                parts[count++] = "Block";
            }
            any = count != 0 || opt;
            if (any) {
                int k = 0;
                if (!xx_buf_append_char(out, '<')) {
                    return 0;
                }
                if (count > 0 && info->lead_num > li) {
                    if (!xx_buf_append_str(out, parts[k++])) {
                        return 0;
                    }
                }
                if (opt) {
                    uint64_t pc = 0;
                    yarv_read_word(&lister->image, (uint64_t)info->opt_table_offset + (uint64_t)(li - info->lead_num) * lister->image.word, lister->image.word, &pc);
                    if (!yarv_appendf_u(out, "Opt=0x%04llX", (unsigned long long)yarv_slot_address(names, pc))) {
                        return 0;
                    }
                }
                for (; k < count; ++k) {
                    if (!xx_buf_append_str(out, parts[k])) {
                        return 0;
                    }
                }
                if (!xx_buf_append_char(out, '>')) {
                    return 0;
                }
            }
        }
        while (out->size < width) {
            if (!xx_buf_append_char(out, ' ')) {
                return 0;
            }
        }
    }
    yarv_right_strip(out, line_start);
    return xx_buf_append_char(out, '\n') ? 1 : 0;
}

/* The line table record of slot pc: the last one starting at or before
 * it. */
static const xxbyte_yarv_insn_info *yarv_line_entry(const xxbyte_yarv_insn_info *entries, size_t count, uint32_t pc)
{
    size_t low = 0;
    size_t high = count;

    if (count == 0) {
        return NULL;
    }
    if (count == 1) {
        return &entries[0];
    }
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (entries[middle].position <= pc) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low == 0 ? NULL : &entries[low - 1];
}

static int yarv_line_suffix(yarv_lister *lister, size_t text_start, const xxbyte_yarv_insn_info *entries, size_t count, uint32_t pc)
{
    xx_buf_t *out = lister->out;
    const xxbyte_yarv_insn_info *entry = yarv_line_entry(entries, count, pc);
    const xxbyte_yarv_insn_info *prev = pc == 0 ? NULL : yarv_line_entry(entries, count, pc - 1);
    int32_t line = entry != NULL ? entry->line_no : 0;
    int32_t prev_line = prev != NULL ? prev->line_no : 0;
    uint32_t events = entry != NULL ? entry->events : 0;

    if (line != 0 && line != prev_line) {
        do {
            if (!xx_buf_append_char(out, ' ')) {
                return 0;
            }
        } while (out->size < text_start + 50);
        if (!xx_buf_appendf(out, "(%4d)", line)) {
            return 0;
        }
    }
    if (events != 0) {
        static const struct {
            uint32_t bit;
            const char *name;
        } names[] = {
            {0x0001, "Li"}, {0x0002, "Cl"}, {0x0004, "En"}, {0x0008, "Ca"}, {0x0010, "Re"},   {0x0020, "Cc"},
            {0x0040, "Cr"}, {0x0100, "Bc"}, {0x0200, "Br"}, {0x4000, "Rs"}, {0x10000, "Cli"}, {0x20000, "Cbr"},
        };
        size_t i;
        if (!xx_buf_append_char(out, '[')) {
            return 0;
        }
        for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
            if (names[i].bit == 0x4000 && lister->image.version < YARV_MODE(3, 3)) {
                continue;
            }
            if ((events & names[i].bit) != 0 && !xx_buf_append_str(out, names[i].name)) {
                return 0;
            }
        }
        if (!xx_buf_append_char(out, ']')) {
            return 0;
        }
    }
    return 1;
}

/* Lists iseq index; pushes the iseqs it reaches. */
static xxbyte_status yarv_list_iseq(yarv_lister *lister, uint32_t index)
{
    xx_buf_t *out = lister->out;
    xxbyte_yarv_names names;
    xxbyte_status status;
    xxbyte_yarv_catch_entry *catches = NULL;
    xxbyte_yarv_insn_info *lines = NULL;
    size_t catch_count = 0;
    size_t line_count = 0;
    const uint8_t *code;
    size_t code_size;
    size_t prefix = 0;
    size_t i;
    uint32_t *children = NULL;
    size_t child_count = 0;
    xxbyte_status result = XXBYTE_STATUS_OK;
    uint32_t format_flags = lister->flags;

    if (!lister->first && !xx_buf_append_char(out, '\n')) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    lister->first = 0;
    {
        xxbyte_yarv_iseq_info info;
        if (!yarv_iseq_info(&lister->image, index, &info, &status)) {
            return xx_buf_appendf(out, "== iseq %u: %s\n", index, xxbyte_status_string(status)) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        /* Addresses are file offsets. */
        status = xxbyte_yarv_names_init(&names, lister->data, lister->size, &lister->header, index, info.code.code_offset);
        if (status == XXBYTE_STATUS_OUT_OF_MEMORY) {
            xxbyte_yarv_names_free(&names);
            return status;
        }
    }
    if (!yarv_header_line(lister, &names.info)) {
        result = XXBYTE_STATUS_OUT_OF_MEMORY;
        goto done;
    }
    code = lister->data + names.info.code.code_offset;
    code_size = names.info.code.code_size;

    /* Catch table. */
    if (names.info.catch_table_size != 0) {
        if (xxbyte_yarv_read_catch_table(lister->data, lister->size, &lister->header, &names.info, NULL, 0, &catch_count) == XXBYTE_STATUS_OK && catch_count != 0) {
            catches = (xxbyte_yarv_catch_entry *)xx_mem_alloc(catch_count * sizeof(xxbyte_yarv_catch_entry));
            if (catches == NULL) {
                result = XXBYTE_STATUS_OUT_OF_MEMORY;
                goto done;
            }
            xxbyte_yarv_read_catch_table(lister->data, lister->size, &lister->header, &names.info, catches, catch_count, &catch_count);
        } else {
            catch_count = 0;
        }
        if (!xx_buf_append_str(out, "== catch table\n")) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            goto done;
        }
        for (i = 0; i < catch_count; ++i) {
            const xxbyte_yarv_catch_entry *e = &catches[i];
            if (!xx_buf_appendf(out, "| catch type: %-6s st: 0x%04llX ed: 0x%04llX sp: %04u cont: 0x%04llX", yarv_catch_name(e->type),
                                (unsigned long long)yarv_slot_address(&names, e->start), (unsigned long long)yarv_slot_address(&names, e->end), e->sp,
                                (unsigned long long)yarv_slot_address(&names, e->cont))) {
                result = XXBYTE_STATUS_OUT_OF_MEMORY;
                goto done;
            }
            if (e->iseq >= 0 && (uint32_t)e->iseq < lister->header.iseq_list_size) {
                xxbyte_yarv_iseq_info handler;
                xxbyte_status handler_status;
                if (!xx_buf_append_str(out, ", ")) {
                    result = XXBYTE_STATUS_OUT_OF_MEMORY;
                    goto done;
                }
                if (yarv_iseq_info(&lister->image, (uint32_t)e->iseq, &handler, &handler_status)) {
                    if (!yarv_put_name_out(lister, handler.label)) {
                        result = XXBYTE_STATUS_OUT_OF_MEMORY;
                        goto done;
                    }
                } else if (!xx_buf_appendf(out, "iseq@%d", e->iseq)) {
                    result = XXBYTE_STATUS_OUT_OF_MEMORY;
                    goto done;
                }
            }
            if (!xx_buf_append_char(out, '\n')) {
                result = XXBYTE_STATUS_OUT_OF_MEMORY;
                goto done;
            }
        }
        if (!xx_buf_append_str(out, "|------------------------------------------------------------------------\n")) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            goto done;
        }
    }
    if (!yarv_local_table(lister, &names)) {
        result = XXBYTE_STATUS_OUT_OF_MEMORY;
        goto done;
    }

    /* Line table. */
    if (names.info.insns_info_size != 0 &&
        xxbyte_yarv_read_insns_info(lister->data, lister->size, &lister->header, &names.info, NULL, 0, &line_count) == XXBYTE_STATUS_OK && line_count != 0) {
        lines = (xxbyte_yarv_insn_info *)xx_mem_alloc(line_count * sizeof(xxbyte_yarv_insn_info));
        if (lines == NULL) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            goto done;
        }
        xxbyte_yarv_read_insns_info(lister->data, lister->size, &lister->header, &names.info, lines, line_count, &line_count);
    } else {
        line_count = 0;
    }

    /* Instructions. */
    if ((format_flags & XXBYTE_LISTING_NO_ADDRESS) == 0) {
        prefix += 10;
    }
    if ((format_flags & XXBYTE_LISTING_NO_BYTES) == 0) {
        prefix += 25;
    }
    if (names.instruction_count != 0) {
        children = (uint32_t *)xx_mem_alloc(names.instruction_count * sizeof(uint32_t) * 2);
        if (children == NULL) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            goto done;
        }
    }
    for (i = 0; i < names.instruction_count; ++i) {
        uint32_t offset = names.instructions[i];
        uint32_t next = i + 1 < names.instruction_count ? names.instructions[i + 1] : 0;
        size_t line_start = out->size;
        xxbyte_instruction insn;
        uint32_t k;

        if (xxbyte_decode_ex(XXBYTE_FAMILY_YARV, lister->header.mode, &names.decode, code + offset, code_size - offset, names.base + offset, &insn) != XXBYTE_STATUS_OK) {
            break;
        }
        next = offset + insn.size;
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_YARV, lister->header.mode, &names.decode, code + offset, next - offset, names.base + offset, format_flags,
                                      xxbyte_yarv_resolver, &names, out);
        if (status != XXBYTE_STATUS_OK) {
            result = status;
            goto done;
        }
        if (out->size > line_start && out->data[out->size - 1] == '\n') {
            --out->size;
            out->data[out->size] = '\0';
        }
        if (!yarv_line_suffix(lister, line_start + prefix, lines, line_count, names.slots[i])) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            goto done;
        }
        yarv_right_strip(out, line_start);
        if (!xx_buf_append_char(out, '\n')) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            goto done;
        }
        for (k = 0; k < insn.operand_count; ++k) {
            const xxbyte_operand *op = &insn.operands[k];
            if (op->type == XXBYTE_OPERAND_INDEX && op->kind == XXBYTE_INDEX_FUNCTION && op->value >= 0 && op->value < (int64_t)lister->header.iseq_list_size &&
                child_count < names.instruction_count * 2) {
                children[child_count++] = (uint32_t)op->value;
            }
        }
    }
    {
        size_t done_bytes = names.instruction_count != 0 ? names.instructions[names.instruction_count - 1] : 0;
        if (names.instruction_count != 0) {
            xxbyte_instruction last;
            if (xxbyte_decode_ex(XXBYTE_FAMILY_YARV, lister->header.mode, &names.decode, code + done_bytes, code_size - done_bytes, names.base + done_bytes, &last) ==
                XXBYTE_STATUS_OK) {
                done_bytes += last.size;
            }
        }
        if (done_bytes < code_size) {
            status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_YARV, lister->header.mode, &names.decode, code + done_bytes, code_size - done_bytes, names.base + done_bytes,
                                          format_flags, xxbyte_yarv_resolver, &names, out);
            if (status != XXBYTE_STATUS_OK) {
                result = status;
                goto done;
            }
        }
    }
    /* Successors: children in reverse code order, then catch handlers in
     * reverse table order, so the handlers come first. */
    for (i = child_count; i-- > 0;) {
        if (!lister->visited[children[i]] && !yarv_push(lister, children[i])) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            goto done;
        }
    }
    for (i = catch_count; i-- > 0;) {
        int32_t handler = catches[i].iseq;
        if (handler >= 0 && (uint32_t)handler < lister->header.iseq_list_size && !lister->visited[handler] && !yarv_push(lister, (uint32_t)handler)) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            goto done;
        }
    }
done:
    xx_mem_free(children);
    xx_mem_free(lines);
    xx_mem_free(catches);
    xxbyte_yarv_names_free(&names);
    return result;
}

xxbyte_status yarv_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    yarv_lister lister;
    xxbyte_status status;
    uint32_t i;
    uint32_t format_flags = listing_flags;

    memset(&lister, 0, sizeof(lister));
    status = xxbyte_yarv_parse_ibf_header(data, size, &lister.header);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (mode != XXBYTE_MODE_AUTO) {
        if (YARV_VERSION(mode) != YARV_VERSION(lister.header.mode)) {
            return XXBYTE_STATUS_UNSUPPORTED_MODE;
        }
        lister.header.mode = mode;
    }
    if (!yarv_image_init(&lister.image, data, size, &lister.header)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    lister.data = data;
    lister.size = size;
    lister.flags = format_flags;
    lister.out = out;
    lister.first = 1;
    if (lister.header.iseq_list_size == 0) {
        return XXBYTE_STATUS_OK;
    }
    lister.visited = (uint8_t *)xx_mem_calloc(lister.header.iseq_list_size, 1);
    if (lister.visited == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    status = XXBYTE_STATUS_OK;
    for (i = 0; i < lister.header.iseq_list_size && status == XXBYTE_STATUS_OK; ++i) {
        if (lister.visited[i]) {
            continue;
        }
        if (!yarv_push(&lister, i)) {
            status = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        while (lister.stack_size != 0 && status == XXBYTE_STATUS_OK) {
            uint32_t index = lister.stack[--lister.stack_size];
            if (lister.visited[index]) {
                continue;
            }
            lister.visited[index] = 1;
            status = yarv_list_iseq(&lister, index);
        }
    }
    xx_mem_free(lister.stack);
    xx_mem_free(lister.visited);
    if (status == XXBYTE_STATUS_OK && !xx_buf_ok(out)) {
        status = XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    return status;
}

#else
typedef int xxbyte_yarv_names_disabled;
#endif
