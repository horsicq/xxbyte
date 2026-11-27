/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_COMMON_H
#define XXBYTE_XXBYTE_COMMON_H

#include <stddef.h>
#include <stdint.h>

#include <xxbyte/xxbyte_config.h>

#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(XXBYTE_STATIC)
#define XXBYTE_API
#elif defined(XXBYTE_BUILDING_LIBRARY)
#define XXBYTE_API __declspec(dllexport)
#else
#define XXBYTE_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#if defined(XXBYTE_STATIC)
#define XXBYTE_API
#else
#define XXBYTE_API __attribute__((visibility("default")))
#endif
#else
#define XXBYTE_API
#endif

#define XXBYTE_VERSION_MAJOR 0
#define XXBYTE_VERSION_MINOR 1
#define XXBYTE_VERSION_PATCH 0
#define XXBYTE_VERSION_STRING "0.1.0"

#ifdef __cplusplus
extern "C" {
#endif

/* Bytecode families. A family compiled out of the library keeps its value. */
typedef enum xxbyte_family {
    XXBYTE_FAMILY_UNKNOWN = 0,
    XXBYTE_FAMILY_DOTNET = 1,         /* ECMA-335 CIL (MSIL) method bodies */
    XXBYTE_FAMILY_DEX = 2,            /* Dalvik bytecode from DEX code_items */
    XXBYTE_FAMILY_JAVA = 3,           /* JVM bytecode from class-file Code attributes */
    XXBYTE_FAMILY_PYTHON = 4,         /* CPython bytecode from code objects */
    XXBYTE_FAMILY_VB = 5,             /* Visual Basic 5/6 P-code */
    XXBYTE_FAMILY_WASM = 6,           /* WebAssembly function bodies */
    XXBYTE_FAMILY_EBPF = 7,           /* eBPF and Solana sBPF programs */
    XXBYTE_FAMILY_LUA = 8,            /* PUC-Rio Lua 5.x function prototypes */
    XXBYTE_FAMILY_LUAJIT = 9,         /* LuaJIT 2.x function prototypes */
    XXBYTE_FAMILY_NSIS = 10,          /* NSIS installer script entries */
    XXBYTE_FAMILY_IFPS = 11,          /* RemObjects PascalScript (Inno Setup) */
    XXBYTE_FAMILY_EVM = 12,           /* Ethereum Virtual Machine bytecode */
    XXBYTE_FAMILY_EBC = 13,           /* UEFI EFI Byte Code */
    XXBYTE_FAMILY_YARV = 14,          /* Ruby YARV instruction sequences */
    XXBYTE_FAMILY_AVM2 = 15,          /* ActionScript 3 (AVM2) ABC method bodies */
    XXBYTE_FAMILY_AVM1 = 16,          /* ActionScript 1/2 (AVM1) SWF action records */
    XXBYTE_FAMILY_AUTOIT = 17,        /* AutoIt v3 compiled scripts (AU3! archives, A3X) */
    XXBYTE_FAMILY_INSTALLSHIELD = 18, /* InstallShield compiled script (.inx) */
    XXBYTE_FAMILY_HERMES = 19,        /* Hermes (React Native) bytecode */
    XXBYTE_FAMILY_BEAM = 20,          /* Erlang/OTP BEAM code chunks */
    XXBYTE_FAMILY_OCAML = 21,         /* OCaml bytecode (ocamlrun) */
    XXBYTE_FAMILY_GDSCRIPT = 22,      /* Godot GDScript binary tokens (.gdc) */
    XXBYTE_FAMILY_SQUIRREL = 23,      /* Squirrel compiled scripts (cnut) */
    XXBYTE_FAMILY_PHP = 24,           /* PHP Zend VM opcodes (OPcache) */
    XXBYTE_FAMILY_QUICKJS = 25,       /* QuickJS bytecode */
    XXBYTE_FAMILY_V8 = 26             /* V8 Ignition bytecode */
} xxbyte_family;

#define XXBYTE_FAMILY_COUNT 27u

typedef enum xxbyte_status {
    XXBYTE_STATUS_OK = 0,
    XXBYTE_STATUS_INVALID_ARGUMENT,
    XXBYTE_STATUS_END_OF_INPUT,        /* code_size is zero */
    XXBYTE_STATUS_TRUNCATED,           /* the instruction (or region) runs past
                                          the end of the input */
    XXBYTE_STATUS_INVALID_INSTRUCTION, /* undefined or malformed encoding */
    XXBYTE_STATUS_UNSUPPORTED_FAMILY,  /* unknown or compiled-out family */
    XXBYTE_STATUS_UNSUPPORTED_MODE,    /* mode is not valid for the family */
    XXBYTE_STATUS_OUT_OF_MEMORY,
    XXBYTE_STATUS_IO_ERROR,          /* an xxfclib device read failed */
    XXBYTE_STATUS_UNSUPPORTED_FORMAT /* a known container variant that xxbyte
                                        cannot read (e.g. LZMA-compressed) */
} xxbyte_status;

/* Operand kinds. */
typedef enum xxbyte_operand_type {
    XXBYTE_OPERAND_NONE = 0,
    XXBYTE_OPERAND_REGISTER,       /* value = register number (Dalvik vN) */
    XXBYTE_OPERAND_REGISTER_RANGE, /* value = first register, extra = count */
    XXBYTE_OPERAND_IMMEDIATE,      /* value = integer; SIGNED flag if signed */
    XXBYTE_OPERAND_FLOAT,          /* value = IEEE-754 bits; size = 4 or 8 */
    XXBYTE_OPERAND_BRANCH,         /* value = absolute target address */
    XXBYTE_OPERAND_TOKEN,          /* value = .NET metadata token */
    XXBYTE_OPERAND_INDEX,          /* value = index; kind = xxbyte_index_kind */
    XXBYTE_OPERAND_DATA,           /* value = absolute address of inline data */
    XXBYTE_OPERAND_MEMORY,         /* kind = base register, value = signed
                                      displacement, size = access width in
                                      bytes (0 if none), extra family-defined */
    XXBYTE_OPERAND_BYTES           /* raw bytes too wide for value: value =
                                      offset from the instruction's first
                                      byte, size = byte count (read via code) */
} xxbyte_operand_type;

/* What an XXBYTE_OPERAND_INDEX operand indexes. */
typedef enum xxbyte_index_kind {
    XXBYTE_INDEX_NONE = 0,
    XXBYTE_INDEX_CONSTANT,      /* JVM constant pool, Python co_consts, VB constant pool */
    XXBYTE_INDEX_STRING,        /* Dalvik string_ids (.NET ldstr is a TOKEN) */
    XXBYTE_INDEX_TYPE,          /* Dalvik type_ids */
    XXBYTE_INDEX_FIELD,         /* Dalvik field_ids */
    XXBYTE_INDEX_METHOD,        /* Dalvik method_ids */
    XXBYTE_INDEX_PROTO,         /* Dalvik proto_ids */
    XXBYTE_INDEX_CALL_SITE,     /* Dalvik call_site_ids */
    XXBYTE_INDEX_METHOD_HANDLE, /* Dalvik method_handles */
    XXBYTE_INDEX_LOCAL,         /* local variable slot */
    XXBYTE_INDEX_ARGUMENT,      /* method argument slot */
    XXBYTE_INDEX_NAME,          /* Python co_names */
    XXBYTE_INDEX_FREE,          /* Python cell and free variables */
    XXBYTE_INDEX_FRAME,         /* VB stack-frame offset (signed) */
    XXBYTE_INDEX_IMPORT,        /* VB import table entry */
    XXBYTE_INDEX_VTABLE,        /* VB vtable offset */
    XXBYTE_INDEX_INLINE,        /* Dalvik inline-method index (ODEX) */
    XXBYTE_INDEX_VTABLE_SLOT,   /* Dalvik vtable index (ODEX) */
    XXBYTE_INDEX_FIELD_OFFSET,  /* Dalvik field byte offset (ODEX) */
    XXBYTE_INDEX_FUNCTION,      /* function or child prototype */
    XXBYTE_INDEX_GLOBAL,        /* global variable */
    XXBYTE_INDEX_TABLE,         /* table (WebAssembly) */
    XXBYTE_INDEX_LABEL,         /* relative label depth (WebAssembly br) */
    XXBYTE_INDEX_UPVALUE,       /* closure upvalue (Lua) */
    XXBYTE_INDEX_VARIABLE,      /* script variable (NSIS, PascalScript) */
    XXBYTE_INDEX_CLASS,         /* class (AVM2) */

    /* Kinds from here up are defined by the family headers. */
    XXBYTE_INDEX_FAMILY_FIRST = 128
} xxbyte_index_kind;

/* Per-operand flags. */
#define XXBYTE_OPERAND_FLAG_SIGNED 0x0001u   /* value is a signed quantity */
#define XXBYTE_OPERAND_FLAG_IMPLICIT 0x0002u /* encoded in the opcode itself */
#define XXBYTE_OPERAND_FLAG_LIST 0x0004u     /* member of a braced register list */
#define XXBYTE_OPERAND_FLAG_WIDE 0x0008u     /* register pair or 64-bit value */
#define XXBYTE_OPERAND_FLAG_HIGH16 0x0010u   /* literal is shifted into the high bits */
/* Bits from here up (0x1000-0x8000) are defined by the family headers and
 * mean nothing outside their family. Bits 0x0020-0x0800 are reserved for the
 * core. */
#define XXBYTE_OPERAND_FLAG_FAMILY_FIRST 0x1000u

/* Instruction classification. */
#define XXBYTE_GROUP_NONE 0x0000u
#define XXBYTE_GROUP_JUMP 0x0001u        /* unconditional transfer */
#define XXBYTE_GROUP_CONDITIONAL 0x0002u /* conditional transfer */
#define XXBYTE_GROUP_CALL 0x0004u
#define XXBYTE_GROUP_RETURN 0x0008u
#define XXBYTE_GROUP_THROW 0x0010u
#define XXBYTE_GROUP_SWITCH 0x0020u  /* multi-way transfer (see table_count) */
#define XXBYTE_GROUP_PREFIX 0x0040u  /* modifies the next instruction */
#define XXBYTE_GROUP_PAYLOAD 0x0080u /* inline data, not executable */
#define XXBYTE_GROUP_STOP 0x0100u    /* control never falls through */

#define XXBYTE_MAX_OPERANDS 8u

typedef struct xxbyte_operand {
    uint8_t type;   /* xxbyte_operand_type */
    uint8_t kind;   /* xxbyte_index_kind for XXBYTE_OPERAND_INDEX, base
                       register for XXBYTE_OPERAND_MEMORY */
    uint16_t flags; /* XXBYTE_OPERAND_FLAG_* */
    uint32_t size;  /* encoded width in bytes, 0 when implicit; a FLOAT's
                       size is always its width, 4 or 8 */
    int64_t value;
    uint64_t extra; /* register count for REGISTER_RANGE; family-defined
                       otherwise (the raw CPython oparg), else 0 */
} xxbyte_operand;

/*
 * One decoded instruction.
 *
 * address and every BRANCH target are byte offsets in the caller's address
 * space, normally the offset from the start of the method body. code points
 * at the first byte of the instruction in the buffer passed to the decoder;
 * it is borrowed, and the switch-table helpers and the formatter read it for
 * instructions with table_count != 0, so keep that buffer alive while using
 * the instruction.
 */
typedef struct xxbyte_instruction {
    uint32_t family;       /* xxbyte_family */
    uint32_t mode;         /* family-specific decode mode */
    uint64_t address;      /* address of the first byte */
    uint32_t size;         /* total size in bytes, including inline tables */
    uint32_t opcode;       /* raw opcode with prefix byte(s), e.g. 0xFE01 */
    uint32_t id;           /* family-specific mnemonic identifier, 0 = invalid */
    uint32_t group;        /* XXBYTE_GROUP_* */
    const char *mnemonic;  /* static string */
    const uint8_t *code;   /* borrowed: the instruction's first byte */
    uint32_t table_count;  /* switch/payload/list entries, 0 if none */
    uint32_t table_offset; /* byte offset of the first entry from code */
    uint32_t operand_count;
    xxbyte_operand operands[XXBYTE_MAX_OPERANDS];
} xxbyte_instruction;

/*
 * Optional name resolver for formatting. Called for each displayed (not
 * IMPLICIT) TOKEN, INDEX, BRANCH and DATA operand, except where a family
 * header says otherwise. Write a NUL-terminated replacement of at most
 * buffer_size - 1 characters and return its length, or return 0 to keep the
 * numeric form. A name that does not fit may be reported like snprintf:
 * write the truncated text and return the full length (>= buffer_size);
 * the formatter then calls the resolver again with a buffer that holds the
 * whole name, for names shorter than XXBYTE_RESOLVER_NAME_LIMIT.
 *
 * Inline-table entries of those kinds that are not operands (.NET switch
 * targets, JVM tableswitch/lookupswitch case targets, INDEX and BRANCH
 * entries of VB P-code lists) are passed as operand 0, the only operand, of
 * a copy of the instruction.
 */
#define XXBYTE_RESOLVER_NAME_LIMIT (1024u * 1024u)

typedef size_t (*xxbyte_resolver)(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/* Formatter flags. Bits not defined here must be zero. */
#define XXBYTE_FORMAT_DEFAULT 0x0000u
#define XXBYTE_FORMAT_UPPERCASE 0x0001u /* uppercase the mnemonic */
#define XXBYTE_FORMAT_KNOWN_FLAGS 0x0001u

XXBYTE_API const char *xxbyte_version(void);
XXBYTE_API const char *xxbyte_status_string(xxbyte_status status);
XXBYTE_API const char *xxbyte_family_name(xxbyte_family family);
/* Nonzero when family is compiled into this build. */
XXBYTE_API int xxbyte_family_enabled(xxbyte_family family);
/* Accepts the family names of xxbyte_family_name and the aliases "net",
 * "cil", "msil", "dalvik", "jvm", "pyc", "pcode", "p-code", "webassembly",
 * "bpf", "innosetup", "pascalscript", "ethereum", "efi", "ruby", "abc",
 * "actionscript", "swf", "as2", "a3x", "au3", "inx", "isscript", "hbc",
 * "erlang", "ocamlrun", "godot", "cnut", "opcache", "zend", "qjs" and
 * "ignition"; case-insensitive. Returns UNKNOWN otherwise. */
XXBYTE_API xxbyte_family xxbyte_family_from_name(const char *name);
/* The default mode for family (the newest supported variant). */
XXBYTE_API uint32_t xxbyte_default_mode(xxbyte_family family);
XXBYTE_API xxbyte_status xxbyte_check_mode(xxbyte_family family, uint32_t mode);
/*
 * The code unit of family in mode: every instruction starts at a multiple of
 * it from the start of the code (for example 8 for eBPF, 2 for Dalvik, 28
 * for NSIS entries). It is 1 for byte-aligned families, and 0 when the
 * family is disabled or the mode is unsupported.
 */
XXBYTE_API size_t xxbyte_unit_size(xxbyte_family family, uint32_t mode);

/*
 * Decodes one instruction at code[0]. On success fills *instruction and
 * returns XXBYTE_STATUS_OK. On failure *instruction is zeroed except family,
 * mode and address, and size is 0.
 */
XXBYTE_API xxbyte_status xxbyte_decode(xxbyte_family family, uint32_t mode, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction);

/*
 * xxbyte_decode with a family-specific decode context (see the family
 * header; for example the type table that sizes PascalScript immediates).
 * The context is borrowed for the call only. Families without a context
 * ignore it; xxbyte_decode passes NULL.
 */
XXBYTE_API xxbyte_status xxbyte_decode_ex(xxbyte_family family, uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address,
                                          xxbyte_instruction *instruction);

/*
 * Reads entry index of a switch-like instruction (table_count != 0 and
 * XXBYTE_GROUP_SWITCH set). key receives the matched value and target the
 * absolute branch address. For a Dalvik switch payload, whose base is the
 * referencing switch instruction, target is the signed byte offset relative
 * to that instruction.
 */
XXBYTE_API xxbyte_status xxbyte_switch_entry(const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target);

/*
 * Formats one decoded instruction as text. Returns the complete length
 * excluding the terminating NUL, or 0 on failure. A zero buffer_size only
 * measures. A nonempty buffer is always NUL-terminated, truncating if needed.
 */
XXBYTE_API size_t xxbyte_format(const xxbyte_instruction *instruction, uint32_t flags, char *buffer, size_t buffer_size);
XXBYTE_API size_t xxbyte_format_ex(const xxbyte_instruction *instruction, uint32_t flags, xxbyte_resolver resolver, void *context, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
