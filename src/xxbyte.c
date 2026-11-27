/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_internal.h"

#include <xxfclib/memory/xx_memory.h>

static const char *const status_names[] = {
    "ok",        "invalid argument",   "end of input", "truncated input", "invalid instruction", "unsupported family", "unsupported mode", "out of memory",
    "I/O error", "unsupported format",
};

static const char *const family_names[XXBYTE_FAMILY_COUNT] = {
    "unknown", "dotnet", "dex",  "java",   "python",        "vb",     "wasm", "ebpf",  "lua",      "luajit",   "nsis", "ifps",    "evm", "ebc",
    "yarv",    "avm2",   "avm1", "autoit", "installshield", "hermes", "beam", "ocaml", "gdscript", "squirrel", "php",  "quickjs", "v8",
};

static const struct {
    const char *name;
    xxbyte_family family;
} family_aliases[] = {
    {"dotnet", XXBYTE_FAMILY_DOTNET},
    {"net", XXBYTE_FAMILY_DOTNET},
    {"cil", XXBYTE_FAMILY_DOTNET},
    {"msil", XXBYTE_FAMILY_DOTNET},
    {"dex", XXBYTE_FAMILY_DEX},
    {"dalvik", XXBYTE_FAMILY_DEX},
    {"java", XXBYTE_FAMILY_JAVA},
    {"jvm", XXBYTE_FAMILY_JAVA},
    {"python", XXBYTE_FAMILY_PYTHON},
    {"pyc", XXBYTE_FAMILY_PYTHON},
    {"vb", XXBYTE_FAMILY_VB},
    {"pcode", XXBYTE_FAMILY_VB},
    {"p-code", XXBYTE_FAMILY_VB},
    {"wasm", XXBYTE_FAMILY_WASM},
    {"webassembly", XXBYTE_FAMILY_WASM},
    {"ebpf", XXBYTE_FAMILY_EBPF},
    {"bpf", XXBYTE_FAMILY_EBPF},
    {"lua", XXBYTE_FAMILY_LUA},
    {"luajit", XXBYTE_FAMILY_LUAJIT},
    {"nsis", XXBYTE_FAMILY_NSIS},
    {"ifps", XXBYTE_FAMILY_IFPS},
    {"innosetup", XXBYTE_FAMILY_IFPS},
    {"pascalscript", XXBYTE_FAMILY_IFPS},
    {"evm", XXBYTE_FAMILY_EVM},
    {"ethereum", XXBYTE_FAMILY_EVM},
    {"ebc", XXBYTE_FAMILY_EBC},
    {"efi", XXBYTE_FAMILY_EBC},
    {"yarv", XXBYTE_FAMILY_YARV},
    {"ruby", XXBYTE_FAMILY_YARV},
    {"avm2", XXBYTE_FAMILY_AVM2},
    {"abc", XXBYTE_FAMILY_AVM2},
    {"actionscript", XXBYTE_FAMILY_AVM2},
    {"avm1", XXBYTE_FAMILY_AVM1},
    {"swf", XXBYTE_FAMILY_AVM1},
    {"as2", XXBYTE_FAMILY_AVM1},
    {"autoit", XXBYTE_FAMILY_AUTOIT},
    {"a3x", XXBYTE_FAMILY_AUTOIT},
    {"au3", XXBYTE_FAMILY_AUTOIT},
    {"installshield", XXBYTE_FAMILY_INSTALLSHIELD},
    {"inx", XXBYTE_FAMILY_INSTALLSHIELD},
    {"isscript", XXBYTE_FAMILY_INSTALLSHIELD},
    {"hermes", XXBYTE_FAMILY_HERMES},
    {"hbc", XXBYTE_FAMILY_HERMES},
    {"beam", XXBYTE_FAMILY_BEAM},
    {"erlang", XXBYTE_FAMILY_BEAM},
    {"ocaml", XXBYTE_FAMILY_OCAML},
    {"ocamlrun", XXBYTE_FAMILY_OCAML},
    {"gdscript", XXBYTE_FAMILY_GDSCRIPT},
    {"godot", XXBYTE_FAMILY_GDSCRIPT},
    {"squirrel", XXBYTE_FAMILY_SQUIRREL},
    {"cnut", XXBYTE_FAMILY_SQUIRREL},
    {"php", XXBYTE_FAMILY_PHP},
    {"opcache", XXBYTE_FAMILY_PHP},
    {"zend", XXBYTE_FAMILY_PHP},
    {"quickjs", XXBYTE_FAMILY_QUICKJS},
    {"qjs", XXBYTE_FAMILY_QUICKJS},
    {"v8", XXBYTE_FAMILY_V8},
    {"ignition", XXBYTE_FAMILY_V8},
};

const char *xxbyte_version(void)
{
    return XXBYTE_VERSION_STRING;
}

const char *xxbyte_status_string(xxbyte_status status)
{
    if ((unsigned)status < sizeof(status_names) / sizeof(status_names[0])) {
        return status_names[status];
    }
    return "unknown status";
}

const char *xxbyte_family_name(xxbyte_family family)
{
    if ((unsigned)family < XXBYTE_FAMILY_COUNT) {
        return family_names[family];
    }
    return family_names[0];
}

const xxbyte_family_ops *xxbyte_get_ops(xxbyte_family family)
{
    switch (family) {
#if XXBYTE_USE_DOTNET
        case XXBYTE_FAMILY_DOTNET: return &xxbyte_dotnet_ops;
#endif
#if XXBYTE_USE_DEX
        case XXBYTE_FAMILY_DEX: return &xxbyte_dex_ops;
#endif
#if XXBYTE_USE_JAVA
        case XXBYTE_FAMILY_JAVA: return &xxbyte_java_ops;
#endif
#if XXBYTE_USE_PYTHON
        case XXBYTE_FAMILY_PYTHON: return &xxbyte_python_ops;
#endif
#if XXBYTE_USE_VB
        case XXBYTE_FAMILY_VB: return &xxbyte_vb_ops;
#endif
#if XXBYTE_USE_WASM
        case XXBYTE_FAMILY_WASM: return &xxbyte_wasm_ops;
#endif
#if XXBYTE_USE_EBPF
        case XXBYTE_FAMILY_EBPF: return &xxbyte_ebpf_ops;
#endif
#if XXBYTE_USE_LUA
        case XXBYTE_FAMILY_LUA: return &xxbyte_lua_ops;
#endif
#if XXBYTE_USE_LUAJIT
        case XXBYTE_FAMILY_LUAJIT: return &xxbyte_luajit_ops;
#endif
#if XXBYTE_USE_NSIS
        case XXBYTE_FAMILY_NSIS: return &xxbyte_nsis_ops;
#endif
#if XXBYTE_USE_IFPS
        case XXBYTE_FAMILY_IFPS: return &xxbyte_ifps_ops;
#endif
#if XXBYTE_USE_EVM
        case XXBYTE_FAMILY_EVM: return &xxbyte_evm_ops;
#endif
#if XXBYTE_USE_EBC
        case XXBYTE_FAMILY_EBC: return &xxbyte_ebc_ops;
#endif
#if XXBYTE_USE_YARV
        case XXBYTE_FAMILY_YARV: return &xxbyte_yarv_ops;
#endif
#if XXBYTE_USE_AVM2
        case XXBYTE_FAMILY_AVM2: return &xxbyte_avm2_ops;
#endif
#if XXBYTE_USE_AVM1
        case XXBYTE_FAMILY_AVM1: return &xxbyte_avm1_ops;
#endif
#if XXBYTE_USE_AUTOIT
        case XXBYTE_FAMILY_AUTOIT: return &xxbyte_autoit_ops;
#endif
#if XXBYTE_USE_INSTALLSHIELD
        case XXBYTE_FAMILY_INSTALLSHIELD: return &xxbyte_installshield_ops;
#endif
#if XXBYTE_USE_HERMES
        case XXBYTE_FAMILY_HERMES: return &xxbyte_hermes_ops;
#endif
#if XXBYTE_USE_BEAM
        case XXBYTE_FAMILY_BEAM: return &xxbyte_beam_ops;
#endif
#if XXBYTE_USE_OCAML
        case XXBYTE_FAMILY_OCAML: return &xxbyte_ocaml_ops;
#endif
#if XXBYTE_USE_GDSCRIPT
        case XXBYTE_FAMILY_GDSCRIPT: return &xxbyte_gdscript_ops;
#endif
#if XXBYTE_USE_SQUIRREL
        case XXBYTE_FAMILY_SQUIRREL: return &xxbyte_squirrel_ops;
#endif
#if XXBYTE_USE_PHP
        case XXBYTE_FAMILY_PHP: return &xxbyte_php_ops;
#endif
#if XXBYTE_USE_QUICKJS
        case XXBYTE_FAMILY_QUICKJS: return &xxbyte_quickjs_ops;
#endif
#if XXBYTE_USE_V8
        case XXBYTE_FAMILY_V8: return &xxbyte_v8_ops;
#endif
        default: return NULL;
    }
}

int xxbyte_family_enabled(xxbyte_family family)
{
    return xxbyte_get_ops(family) != NULL;
}

static int ascii_lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

xxbyte_family xxbyte_family_from_name(const char *name)
{
    size_t i;

    if (name == NULL) {
        return XXBYTE_FAMILY_UNKNOWN;
    }
    for (i = 0; i < sizeof(family_aliases) / sizeof(family_aliases[0]); ++i) {
        const char *a = family_aliases[i].name;
        const char *b = name;
        while (*a != '\0' && ascii_lower((unsigned char)*b) == *a) {
            ++a;
            ++b;
        }
        if (*a == '\0' && *b == '\0') {
            return family_aliases[i].family;
        }
    }
    return XXBYTE_FAMILY_UNKNOWN;
}

uint32_t xxbyte_default_mode(xxbyte_family family)
{
    const xxbyte_family_ops *ops = xxbyte_get_ops(family);
    return ops != NULL ? ops->default_mode : 0;
}

xxbyte_status xxbyte_check_mode(xxbyte_family family, uint32_t mode)
{
    const xxbyte_family_ops *ops = xxbyte_get_ops(family);

    if (ops == NULL) {
        return XXBYTE_STATUS_UNSUPPORTED_FAMILY;
    }
    return ops->check_mode(mode);
}

size_t xxbyte_unit_size(xxbyte_family family, uint32_t mode)
{
    const xxbyte_family_ops *ops = xxbyte_get_ops(family);

    if (ops == NULL || ops->check_mode(mode) != XXBYTE_STATUS_OK) {
        return 0;
    }
    return ops->unit_size != NULL ? ops->unit_size(mode) : 1u;
}

static void reset_instruction(xxbyte_instruction *instruction, xxbyte_family family, uint32_t mode, uint64_t address)
{
    memset(instruction, 0, sizeof(*instruction));
    instruction->family = (uint32_t)family;
    instruction->mode = mode;
    instruction->address = address;
}

xxbyte_status xxbyte_decode(xxbyte_family family, uint32_t mode, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    return xxbyte_decode_ex(family, mode, NULL, code, code_size, address, instruction);
}

xxbyte_status xxbyte_decode_ex(xxbyte_family family, uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address,
                               xxbyte_instruction *instruction)
{
    const xxbyte_family_ops *ops;
    xxbyte_status status;

    if (instruction == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    reset_instruction(instruction, family, mode, address);
    if (code == NULL && code_size != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    ops = xxbyte_get_ops(family);
    if (ops == NULL) {
        return XXBYTE_STATUS_UNSUPPORTED_FAMILY;
    }
    status = ops->check_mode(mode);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (code_size == 0) {
        return XXBYTE_STATUS_END_OF_INPUT;
    }
    instruction->code = code;
    status = ops->decode(mode, context, code, code_size, address, instruction);
    if (status != XXBYTE_STATUS_OK) {
        reset_instruction(instruction, family, mode, address);
        return status;
    }
    if (instruction->size == 0 || instruction->size > code_size || instruction->mnemonic == NULL) {
        reset_instruction(instruction, family, mode, address);
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_switch_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target)
{
    const xxbyte_family_ops *ops;

    if (instruction == NULL || key == NULL || target == NULL || instruction->code == NULL || index >= instruction->table_count ||
        (instruction->group & XXBYTE_GROUP_SWITCH) == 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    ops = xxbyte_get_ops((xxbyte_family)instruction->family);
    if (ops == NULL) {
        return XXBYTE_STATUS_UNSUPPORTED_FAMILY;
    }
    if (ops->switch_entry == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return ops->switch_entry(instruction, index, key, target);
}

xxbyte_operand *xxbyte_add_operand(xxbyte_instruction *instruction, xxbyte_operand_type type, uint32_t size, int64_t value)
{
    xxbyte_operand *operand;

    if (instruction->operand_count >= XXBYTE_MAX_OPERANDS) {
        return NULL;
    }
    operand = &instruction->operands[instruction->operand_count++];
    memset(operand, 0, sizeof(*operand));
    operand->type = (uint8_t)type;
    operand->size = size;
    operand->value = value;
    return operand;
}

xxbyte_operand *xxbyte_add_index(xxbyte_instruction *instruction, xxbyte_index_kind kind, uint32_t size, int64_t value)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_INDEX, size, value);
    if (operand != NULL) {
        operand->kind = (uint8_t)kind;
    }
    return operand;
}

int xxbyte_fmt_str(xxbyte_fmt *fmt, const char *text)
{
    return xx_buf_append_str(fmt->out, text) ? 1 : 0;
}

int xxbyte_fmt_char(xxbyte_fmt *fmt, char c)
{
    return xx_buf_append_char(fmt->out, c) ? 1 : 0;
}

size_t xxbyte_fmt_mark(const xxbyte_fmt *fmt)
{
    return fmt->out->size;
}

void xxbyte_fmt_rewind(xxbyte_fmt *fmt, size_t mark)
{
    if (mark < fmt->out->size && fmt->out->data != NULL) {
        fmt->out->size = mark;
        fmt->out->data[mark] = '\0';
    }
}

int xxbyte_fmt_resolve_entry(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, const xxbyte_operand *entry)
{
    xxbyte_instruction copy;

    if (fmt->resolver == NULL) {
        return 0;
    }
    copy = *instruction;
    memset(copy.operands, 0, sizeof(copy.operands));
    copy.operands[0] = *entry;
    copy.operand_count = 1;
    return xxbyte_fmt_resolve(fmt, &copy, 0);
}

int xxbyte_fmt_resolve(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t operand_index)
{
    char name[512];
    size_t length;

    if (fmt->resolver == NULL || operand_index >= instruction->operand_count) {
        return 0;
    }
    name[0] = '\0';
    length = fmt->resolver(fmt->context, instruction, operand_index, name, sizeof(name));
    if (length == 0 || name[0] == '\0') {
        return 0;
    }
    if (length >= sizeof(name) && length < XXBYTE_RESOLVER_NAME_LIMIT) {
        /* The resolver reported the full length of a longer name: ask
         * again with room for all of it. */
        char *big = (char *)xx_mem_alloc(length + 1);
        if (big != NULL) {
            size_t again;
            int ok;
            big[0] = '\0';
            again = fmt->resolver(fmt->context, instruction, operand_index, big, length + 1);
            if (again > length) {
                again = length;
            }
            big[again] = '\0';
            ok = xx_buf_append_str(fmt->out, big);
            xx_mem_free(big);
            return ok ? 1 : -1;
        }
    }
    if (length >= sizeof(name)) {
        length = sizeof(name) - 1;
    }
    name[length] = '\0';
    return xx_buf_append_str(fmt->out, name) ? 1 : -1;
}

int xxbyte_format_to_buf(const xxbyte_instruction *instruction, uint32_t flags, xxbyte_resolver resolver, void *context, xx_buf_t *out)
{
    const xxbyte_family_ops *ops;
    xxbyte_fmt fmt;
    const char *m;

    if (instruction == NULL || out == NULL || instruction->mnemonic == NULL || instruction->size == 0 || (flags & ~(uint32_t)XXBYTE_FORMAT_KNOWN_FLAGS) != 0) {
        return 0;
    }
    ops = xxbyte_get_ops((xxbyte_family)instruction->family);
    if (ops == NULL) {
        return 0;
    }
    fmt.out = out;
    fmt.flags = flags;
    fmt.resolver = resolver;
    fmt.context = context;
    for (m = instruction->mnemonic; *m != '\0'; ++m) {
        char c = *m;
        if ((flags & XXBYTE_FORMAT_UPPERCASE) != 0 && c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (!xx_buf_append_char(out, c)) {
            return 0;
        }
    }
    return ops->format_operands(instruction, &fmt) && xx_buf_ok(out);
}

size_t xxbyte_format_ex(const xxbyte_instruction *instruction, uint32_t flags, xxbyte_resolver resolver, void *context, char *buffer, size_t buffer_size)
{
    xx_buf_t text;
    size_t length = 0;

    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if (buffer == NULL && buffer_size != 0) {
        return 0;
    }
    xx_buf_init(&text);
    if (xxbyte_format_to_buf(instruction, flags, resolver, context, &text) && text.size != 0) {
        length = text.size;
        if (buffer_size != 0) {
            size_t copy = length < buffer_size ? length : buffer_size - 1;
            memcpy(buffer, text.data, copy);
            buffer[copy] = '\0';
        }
    }
    xx_buf_free(&text);
    return length;
}

size_t xxbyte_format(const xxbyte_instruction *instruction, uint32_t flags, char *buffer, size_t buffer_size)
{
    return xxbyte_format_ex(instruction, flags, NULL, NULL, buffer, buffer_size);
}
