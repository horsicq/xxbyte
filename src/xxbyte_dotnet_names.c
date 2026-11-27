/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/* .NET token names (ildasm operand syntax) and whole-assembly listings. */
#include "xxbyte_dotnet_internal.h"

#if XXBYTE_USE_DOTNET

#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

#define TABLE_TYPEREF XXBYTE_DOTNET_TABLE_TYPEREF
#define TABLE_TYPEDEF XXBYTE_DOTNET_TABLE_TYPEDEF
#define TABLE_FIELDPTR XXBYTE_DOTNET_TABLE_FIELDPTR
#define TABLE_FIELD XXBYTE_DOTNET_TABLE_FIELD
#define TABLE_METHODPTR XXBYTE_DOTNET_TABLE_METHODPTR
#define TABLE_METHODDEF XXBYTE_DOTNET_TABLE_METHODDEF
#define TABLE_MEMBERREF XXBYTE_DOTNET_TABLE_MEMBERREF
#define TABLE_STANDALONESIG XXBYTE_DOTNET_TABLE_STANDALONESIG
#define TABLE_MODULEREF XXBYTE_DOTNET_TABLE_MODULEREF
#define TABLE_TYPESPEC XXBYTE_DOTNET_TABLE_TYPESPEC
#define TABLE_ASSEMBLYREF XXBYTE_DOTNET_TABLE_ASSEMBLYREF
#define TABLE_NESTEDCLASS XXBYTE_DOTNET_TABLE_NESTEDCLASS
#define TABLE_GENERICPARAM XXBYTE_DOTNET_TABLE_GENERICPARAM
#define TABLE_METHODSPEC XXBYTE_DOTNET_TABLE_METHODSPEC

/* Recursion through nested types, TypeSpecs and signature types. */
#define MAX_DEPTH 64
/* Signature elements visited per name: bounds the work on hostile input
 * (a TypeSpec instantiated with itself, nested a few levels, fans out). */
#define MAX_WORK 100000u

/* ---- text builder ---- */

typedef struct text {
    char *buf;
    size_t cap; /* bytes, including the NUL */
    size_t len;
    int full;   /* stopped at cap */
    int failed; /* malformed input */
    int grow;   /* buf is xx_mem memory that may grow to XXBYTE_RESOLVER_NAME_LIMIT */
    unsigned work;
} text;

static void put_char(text *t, char c)
{
    if (t->full) {
        return;
    }
    if (t->len + 1 >= t->cap && t->grow && t->cap < XXBYTE_RESOLVER_NAME_LIMIT) {
        size_t cap = t->cap * 4u < XXBYTE_RESOLVER_NAME_LIMIT ? t->cap * 4u : XXBYTE_RESOLVER_NAME_LIMIT;
        char *p = (char *)xx_mem_realloc(t->buf, cap);
        if (p != NULL) {
            t->buf = p;
            t->cap = cap;
        }
    }
    if (t->len + 1 < t->cap) {
        t->buf[t->len++] = c;
    } else {
        t->full = 1;
    }
}

static void put_mem(text *t, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n && !t->full; ++i) {
        put_char(t, s[i]);
    }
}

static void put_str(text *t, const char *s)
{
    put_mem(t, s, strlen(s));
}

static void put_uint(text *t, uint64_t v)
{
    char digits[24];
    size_t n = 0;
    do {
        digits[n++] = (char)('0' + (int)(v % 10u));
        v /= 10u;
    } while (v != 0);
    while (n > 0 && !t->full) {
        put_char(t, digits[--n]);
    }
}

static void put_int(text *t, int64_t v)
{
    if (v < 0) {
        put_char(t, '-');
        put_uint(t, 0u - (uint64_t)v);
    } else {
        put_uint(t, (uint64_t)v);
    }
}

static void put_hex(text *t, uint64_t v, int width)
{
    static const char hex[] = "0123456789ABCDEF";
    char digits[16];
    int n = 0;
    do {
        digits[n++] = hex[v & 15u];
        v >>= 4;
    } while (v != 0 && n < 16);
    while (n < width) {
        digits[n++] = '0';
    }
    while (n > 0) {
        put_char(t, digits[--n]);
    }
}

/* Returns 0 once the text is full or the input malformed: callers stop. */
static int going(text *t)
{
    if (++t->work > MAX_WORK) {
        t->failed = 1;
    }
    return !t->full && !t->failed;
}

static int fail(text *t)
{
    t->failed = 1;
    return 0;
}

/* ---- identifiers ---- */

/* ILAsm keywords that occur as member or type names in practice, sorted. */
static const char *const keywords[] = {
    "abstract",
    "add",
    "aggressiveinlining",
    "and",
    "ansi",
    "any",
    "arglist",
    "array",
    "as",
    "assembly",
    "assert",
    "at",
    "auto",
    "autochar",
    "beforefieldinit",
    "bestfit",
    "blob",
    "blob_object",
    "bool",
    "box",
    "boxed",
    "break",
    "bstr",
    "bytearray",
    "call",
    "callconv",
    "callmostderived",
    "carray",
    "catch",
    "cdecl",
    "cf",
    "char",
    "charmaplibrary",
    "cil",
    "ckfinite",
    "class",
    "clsid",
    "constrained",
    "cpblk",
    "currency",
    "custom",
    "date",
    "decimal",
    "default",
    "demand",
    "deny",
    "div",
    "dup",
    "error",
    "event",
    "explicit",
    "extern",
    "false",
    "famandassem",
    "family",
    "famorassem",
    "fastcall",
    "fault",
    "field",
    "filetime",
    "filter",
    "final",
    "finally",
    "fixed",
    "flags",
    "float32",
    "float64",
    "forwardref",
    "fromunmanaged",
    "handler",
    "hidebysig",
    "hresult",
    "idispatch",
    "il",
    "illegal",
    "import",
    "inheritcheck",
    "init",
    "initblk",
    "initonly",
    "instance",
    "int",
    "int16",
    "int32",
    "int64",
    "int8",
    "interface",
    "internalcall",
    "is",
    "iunknown",
    "jmp",
    "lasterr",
    "lcid",
    "ldnull",
    "legacy",
    "library",
    "linkcheck",
    "literal",
    "localloc",
    "lpstr",
    "lpstruct",
    "lptstr",
    "lpwstr",
    "managed",
    "mdtoken",
    "method",
    "modopt",
    "modreq",
    "mul",
    "native",
    "neg",
    "nested",
    "no",
    "noinlining",
    "nomangle",
    "noncasdemand",
    "noncasinheritance",
    "noncaslinkdemand",
    "nooptimization",
    "nop",
    "not",
    "notserialized",
    "nullref",
    "object",
    "objectref",
    "off",
    "on",
    "or",
    "out",
    "permitonly",
    "pinned",
    "pinvokeimpl",
    "pop",
    "prejitdeny",
    "prejitgrant",
    "preservesig",
    "private",
    "property",
    "public",
    "readonly",
    "record",
    "refanytype",
    "rem",
    "reqmin",
    "reqopt",
    "reqrefuse",
    "reqsecobj",
    "request",
    "ret",
    "retargetable",
    "rethrow",
    "rtspecialname",
    "runtime",
    "safearray",
    "sealed",
    "sequential",
    "serializable",
    "shl",
    "shr",
    "sizeof",
    "specialname",
    "static",
    "stdcall",
    "storage",
    "stored_object",
    "stream",
    "streamed_object",
    "strict",
    "string",
    "struct",
    "sub",
    "switch",
    "synchronized",
    "sysstring",
    "tail",
    "tbstr",
    "thiscall",
    "throw",
    "to",
    "true",
    "type",
    "typedref",
    "uint",
    "uint16",
    "uint32",
    "uint64",
    "uint8",
    "unaligned",
    "unbox",
    "unicode",
    "unmanaged",
    "unsigned",
    "unused",
    "value",
    "valuetype",
    "vararg",
    "variant",
    "vector",
    "virtual",
    "void",
    "volatile",
    "vtfixup",
    "winapi",
    "with",
    "xor",
};

static int is_keyword(const char *s, size_t n)
{
    size_t lo = 0, hi = sizeof(keywords) / sizeof(keywords[0]);

    /* keywords is sorted (strcmp order): binary search. */
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2u;
        const char *k = keywords[mid];
        int cmp = strncmp(k, s, n);
        if (cmp == 0) {
            cmp = k[n] == '\0' ? 0 : 1;
        }
        if (cmp == 0) {
            return 1;
        }
        if (cmp < 0) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return 0;
}

static int id_start(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c == '$' || c == '@' || c == '?' || c == '`' || c >= 0x80;
}

static int id_char(unsigned char c)
{
    return id_start(c) || (c >= '0' && c <= '9');
}

/* A dotted name of ILAsm identifiers that is not a keyword. */
static int plain_name(const char *s, size_t n)
{
    size_t i, start = 0;

    if (n == 0) {
        return 0;
    }
    if ((n == 5 && memcmp(s, ".ctor", 5) == 0) || (n == 6 && memcmp(s, ".cctor", 6) == 0)) {
        return 1;
    }
    for (i = 0; i <= n; ++i) {
        if (i == n || s[i] == '.') {
            if (i == start || is_keyword(s + start, i - start)) {
                return 0;
            }
            start = i + 1;
        } else if (i == start ? !id_start((unsigned char)s[i]) : !id_char((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static void put_quoted(text *t, const char *s, size_t n)
{
    size_t i;
    put_char(t, '\'');
    for (i = 0; i < n && !t->full; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\'' || c == '\\') {
            put_char(t, '\\');
            put_char(t, (char)c);
        } else if (c < 0x20 || c == 0x7F) {
            put_char(t, '\\');
            put_char(t, (char)('0' + ((c >> 6) & 7)));
            put_char(t, (char)('0' + ((c >> 3) & 7)));
            put_char(t, (char)('0' + (c & 7)));
        } else {
            put_char(t, (char)c);
        }
    }
    put_char(t, '\'');
}

static void put_name(text *t, const char *s, size_t n)
{
    if (plain_name(s, n)) {
        put_mem(t, s, n);
    } else {
        put_quoted(t, s, n);
    }
}

/* "Ns.Name" quoted as one dotted name. */
static void put_full_name(text *t, const char *ns, size_t ns_len, const char *name, size_t name_len)
{
    if (ns_len == 0) {
        put_name(t, name, name_len);
    } else if (plain_name(ns, ns_len) && plain_name(name, name_len)) {
        put_mem(t, ns, ns_len);
        put_char(t, '.');
        put_mem(t, name, name_len);
    } else {
        size_t i;
        put_char(t, '\'');
        for (i = 0; i < ns_len + 1 + name_len && !t->full; ++i) {
            char c = i < ns_len ? ns[i] : i == ns_len ? '.' : name[i - ns_len - 1];
            if (c == '\'' || c == '\\') {
                put_char(t, '\\');
            }
            put_char(t, c);
        }
        put_char(t, '\'');
    }
}

/* ---- metadata access ---- */

typedef struct ctx {
    const xxbyte_dotnet_names *names;
    const xxbyte_dotnet_image *image;
} ctx;

static int valid_row(const xxbyte_dotnet_image *image, uint32_t table, uint32_t row)
{
    return table < XXBYTE_DOTNET_TABLE_KNOWN && row != 0 && row <= image->rows[table] && image->row_size[table] != 0;
}

static uint32_t col(const ctx *c, uint32_t table, uint32_t row, uint32_t column)
{
    return xxbyte_dotnet_column(c->image, (xxbyte_dotnet_table)table, row, column);
}

static int string_at(const ctx *c, uint32_t index, const char **s, size_t *n)
{
    return xxbyte_dotnet_get_string(c->image, index, s, n) == XXBYTE_STATUS_OK;
}

static int put_heap_name(text *t, const ctx *c, uint32_t index)
{
    const char *s;
    size_t n;
    if (!string_at(c, index, &s, &n)) {
        return fail(t);
    }
    put_name(t, s, n);
    return 1;
}

/* A signature cursor. */
typedef struct sig {
    const uint8_t *p;
    size_t n;
    size_t at;
} sig;

static int open_blob(const ctx *c, uint32_t index, sig *s)
{
    size_t n;
    s->at = 0;
    if (xxbyte_dotnet_get_blob(c->image, index, &s->p, &n) != XXBYTE_STATUS_OK) {
        return 0;
    }
    s->n = n;
    return 1;
}

static int sig_byte(text *t, sig *s, uint8_t *b)
{
    if (s->at >= s->n) {
        return fail(t);
    }
    *b = s->p[s->at++];
    return 1;
}

static int sig_peek(sig *s, uint8_t *b)
{
    if (s->at >= s->n) {
        return 0;
    }
    *b = s->p[s->at];
    return 1;
}

static int sig_uint(text *t, sig *s, uint32_t *v)
{
    if (xxbyte_dotnet_read_compressed(s->p, s->n, &s->at, v) != XXBYTE_STATUS_OK) {
        return fail(t);
    }
    return 1;
}

static int sig_int(text *t, sig *s, int32_t *v)
{
    if (xxbyte_dotnet_read_compressed_signed(s->p, s->n, &s->at, v) != XXBYTE_STATUS_OK) {
        return fail(t);
    }
    return 1;
}

/* ---- owners ---- */

static uint32_t search_owner(const ctx *c, uint32_t list_column, uint32_t pos)
{
    uint32_t lo = 1, hi = c->image->rows[TABLE_TYPEDEF], found = 0;
    while (lo <= hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (col(c, TABLE_TYPEDEF, mid, list_column) <= pos) {
            found = mid;
            lo = mid + 1u;
        } else {
            hi = mid - 1u;
        }
    }
    return found;
}

static uint32_t method_owner(const ctx *c, uint32_t row)
{
    if (c->names->method_owner != NULL) {
        return c->names->method_owner[row];
    }
    return c->image->rows[TABLE_METHODPTR] != 0 ? 0 : search_owner(c, 5, row);
}

static uint32_t field_owner(const ctx *c, uint32_t row)
{
    if (c->names->field_owner != NULL) {
        return c->names->field_owner[row];
    }
    return c->image->rows[TABLE_FIELDPTR] != 0 ? 0 : search_owner(c, 4, row);
}

/* ---- types ---- */

static int put_sig_type(text *t, const ctx *c, sig *s, int depth);
static int put_type_token(text *t, const ctx *c, uint32_t token, int depth);

static int put_typedef(text *t, const ctx *c, uint32_t row, int depth)
{
    const char *ns, *name;
    size_t ns_len, name_len;
    uint32_t outer = 0;

    if (depth > MAX_DEPTH || !valid_row(c->image, TABLE_TYPEDEF, row)) {
        return fail(t);
    }
    if (!string_at(c, col(c, TABLE_TYPEDEF, row, 1), &name, &name_len) || !string_at(c, col(c, TABLE_TYPEDEF, row, 2), &ns, &ns_len)) {
        return fail(t);
    }
    if (c->names->enclosing != NULL) {
        outer = c->names->enclosing[row];
    }
    if (outer != 0) {
        if (!put_typedef(t, c, outer, depth + 1)) {
            return 0;
        }
        put_char(t, '/');
    }
    put_full_name(t, ns, ns_len, name, name_len);
    return going(t);
}

static int put_typeref(text *t, const ctx *c, uint32_t row, int depth)
{
    const char *ns, *name;
    size_t ns_len, name_len;
    uint32_t scope;

    if (depth > MAX_DEPTH || !valid_row(c->image, TABLE_TYPEREF, row)) {
        return fail(t);
    }
    scope = col(c, TABLE_TYPEREF, row, 0);
    if (!string_at(c, col(c, TABLE_TYPEREF, row, 1), &name, &name_len) || !string_at(c, col(c, TABLE_TYPEREF, row, 2), &ns, &ns_len)) {
        return fail(t);
    }
    if (XXBYTE_DOTNET_TOKEN_ROW(scope) != 0) {
        uint32_t srow = XXBYTE_DOTNET_TOKEN_ROW(scope);
        switch (XXBYTE_DOTNET_TOKEN_TABLE(scope)) {
            case XXBYTE_DOTNET_TABLE_ASSEMBLYREF:
                if (!valid_row(c->image, TABLE_ASSEMBLYREF, srow)) {
                    return fail(t);
                }
                put_char(t, '[');
                if (!put_heap_name(t, c, col(c, TABLE_ASSEMBLYREF, srow, 6))) {
                    return 0;
                }
                put_char(t, ']');
                break;
            case XXBYTE_DOTNET_TABLE_MODULEREF:
                if (!valid_row(c->image, TABLE_MODULEREF, srow)) {
                    return fail(t);
                }
                put_str(t, "[.module ");
                if (!put_heap_name(t, c, col(c, TABLE_MODULEREF, srow, 0))) {
                    return 0;
                }
                put_char(t, ']');
                break;
            case XXBYTE_DOTNET_TABLE_TYPEREF:
                if (!put_typeref(t, c, srow, depth + 1)) {
                    return 0;
                }
                put_char(t, '/');
                break;
            case XXBYTE_DOTNET_TABLE_MODULE: break;
            default: return fail(t);
        }
    }
    put_full_name(t, ns, ns_len, name, name_len);
    return going(t);
}

static int put_typespec(text *t, const ctx *c, uint32_t row, int depth)
{
    sig s;
    if (depth > MAX_DEPTH || !valid_row(c->image, TABLE_TYPESPEC, row) || !open_blob(c, col(c, TABLE_TYPESPEC, row, 0), &s)) {
        return fail(t);
    }
    return put_sig_type(t, c, &s, depth + 1);
}

/* TypeDef, TypeRef or TypeSpec token. */
static int put_type_token(text *t, const ctx *c, uint32_t token, int depth)
{
    uint32_t row = XXBYTE_DOTNET_TOKEN_ROW(token);
    switch (XXBYTE_DOTNET_TOKEN_TABLE(token)) {
        case XXBYTE_DOTNET_TABLE_TYPEDEF: return put_typedef(t, c, row, depth);
        case XXBYTE_DOTNET_TABLE_TYPEREF: return put_typeref(t, c, row, depth);
        case XXBYTE_DOTNET_TABLE_TYPESPEC: return put_typespec(t, c, row, depth);
        default: return fail(t);
    }
}

/* TypeDefOrRefOrSpecEncoded (II.23.2.8). */
static int put_encoded_type(text *t, const ctx *c, sig *s, int depth)
{
    static const uint8_t tables[3] = {XXBYTE_DOTNET_TABLE_TYPEDEF, XXBYTE_DOTNET_TABLE_TYPEREF, XXBYTE_DOTNET_TABLE_TYPESPEC};
    uint32_t v;
    if (!sig_uint(t, s, &v) || (v & 3u) == 3u) {
        return fail(t);
    }
    return put_type_token(t, c, XXBYTE_DOTNET_TOKEN(tables[v & 3u], v >> 2), depth);
}

static const char *primitive(uint8_t e)
{
    switch (e) {
        case 0x01: return "void";
        case 0x02: return "bool";
        case 0x03: return "char";
        case 0x04: return "int8";
        case 0x05: return "uint8";
        case 0x06: return "int16";
        case 0x07: return "uint16";
        case 0x08: return "int32";
        case 0x09: return "uint32";
        case 0x0A: return "int64";
        case 0x0B: return "uint64";
        case 0x0C: return "float32";
        case 0x0D: return "float64";
        case 0x0E: return "string";
        case 0x16: return "typedref";
        case 0x18: return "native int";
        case 0x19: return "native uint";
        case 0x1C: return "object";
        default: return NULL;
    }
}

static int put_method_sig(text *t, const ctx *c, sig *s, int depth, int fnptr);

static int put_array_shape(text *t, sig *s)
{
    uint32_t rank, nsizes, nlo, i;
    uint32_t sizes[32];
    int32_t lows[32];

    if (!sig_uint(t, s, &rank) || !sig_uint(t, s, &nsizes)) {
        return 0;
    }
    if (nsizes > rank || rank > 1000000u) {
        return fail(t);
    }
    for (i = 0; i < nsizes; ++i) {
        uint32_t v;
        if (!sig_uint(t, s, &v)) {
            return 0;
        }
        if (i < 32) {
            sizes[i] = v;
        }
    }
    if (!sig_uint(t, s, &nlo)) {
        return 0;
    }
    if (nlo > rank) {
        return fail(t);
    }
    for (i = 0; i < nlo; ++i) {
        int32_t v;
        if (!sig_int(t, s, &v)) {
            return 0;
        }
        if (i < 32) {
            lows[i] = v;
        }
    }
    put_char(t, '[');
    for (i = 0; i < rank && going(t); ++i) {
        int has_lo = i < nlo && i < 32, has_size = i < nsizes && i < 32;
        int64_t lo = has_lo ? lows[i] : 0;
        if (i != 0) {
            put_char(t, ',');
        }
        if (has_size) {
            put_int(t, lo);
            put_str(t, "...");
            put_int(t, lo + (int64_t)sizes[i] - 1);
        } else if (has_lo) {
            put_int(t, lo);
            put_str(t, "...");
        }
    }
    put_char(t, ']');
    return going(t);
}

static int put_sig_type(text *t, const ctx *c, sig *s, int depth)
{
    uint8_t e;
    const char *p;

    if (depth > MAX_DEPTH) {
        return fail(t);
    }
    if (!going(t) || !sig_byte(t, s, &e)) {
        return 0;
    }
    p = primitive(e);
    if (p != NULL) {
        put_str(t, p);
        return going(t);
    }
    switch (e) {
        case 0x0F: /* PTR */
            if (!put_sig_type(t, c, s, depth + 1)) {
                return 0;
            }
            put_char(t, '*');
            break;
        case 0x10: /* BYREF */
            if (!put_sig_type(t, c, s, depth + 1)) {
                return 0;
            }
            put_char(t, '&');
            break;
        case 0x11: /* VALUETYPE */
        case 0x12: /* CLASS */
            put_str(t, e == 0x11 ? "valuetype " : "class ");
            if (!put_encoded_type(t, c, s, depth + 1)) {
                return 0;
            }
            break;
        case 0x13: /* VAR */
        case 0x1E: /* MVAR */ {
            uint32_t n;
            if (!sig_uint(t, s, &n)) {
                return 0;
            }
            put_str(t, e == 0x13 ? "!" : "!!");
            put_uint(t, n);
            break;
        }
        case 0x14: /* ARRAY */
            if (!put_sig_type(t, c, s, depth + 1) || !put_array_shape(t, s)) {
                return 0;
            }
            break;
        case 0x15: /* GENERICINST */ {
            uint8_t kind;
            uint32_t n, i;
            if (!sig_byte(t, s, &kind)) {
                return 0;
            }
            if (kind != 0x11 && kind != 0x12) {
                return fail(t);
            }
            put_str(t, kind == 0x11 ? "valuetype " : "class ");
            if (!put_encoded_type(t, c, s, depth + 1) || !sig_uint(t, s, &n)) {
                return 0;
            }
            if (n == 0) {
                return fail(t);
            }
            put_char(t, '<');
            for (i = 0; i < n; ++i) {
                if (i != 0) {
                    put_char(t, ',');
                }
                if (!put_sig_type(t, c, s, depth + 1)) {
                    return 0;
                }
            }
            put_char(t, '>');
            break;
        }
        case 0x1B: /* FNPTR */
            put_str(t, "method ");
            if (!put_method_sig(t, c, s, depth + 1, 1)) {
                return 0;
            }
            break;
        case 0x1D: /* SZARRAY */
            if (!put_sig_type(t, c, s, depth + 1)) {
                return 0;
            }
            put_str(t, "[]");
            break;
        case 0x1F: /* CMOD_REQD */
        case 0x20: /* CMOD_OPT */ {
            /* The modifier precedes the type in the blob and follows it in
             * the text. */
            sig mod = *s;
            uint32_t v;
            if (!sig_uint(t, s, &v) || !put_sig_type(t, c, s, depth + 1)) {
                return 0;
            }
            put_str(t, e == 0x1F ? " modreq(" : " modopt(");
            if (!put_encoded_type(t, c, &mod, depth + 1)) {
                return 0;
            }
            put_char(t, ')');
            break;
        }
        case 0x45: /* PINNED */
            if (!put_sig_type(t, c, s, depth + 1)) {
                return 0;
            }
            put_str(t, " pinned");
            break;
        default: return fail(t);
    }
    return going(t);
}

/* Parses a type without printing it (II.23.2.12). */
static int skip_type(text *t, sig *s, int depth)
{
    uint8_t e;
    uint32_t v, n, i;
    int32_t lo;

    if (depth > MAX_DEPTH || ++t->work > MAX_WORK) {
        return fail(t);
    }
    if (!sig_byte(t, s, &e)) {
        return 0;
    }
    if (primitive(e) != NULL) {
        return 1;
    }
    switch (e) {
        case 0x0F:
        case 0x10:
        case 0x1D:
        case 0x45: return skip_type(t, s, depth + 1);
        case 0x11:
        case 0x12:
        case 0x13:
        case 0x1E: return sig_uint(t, s, &v);
        case 0x1F:
        case 0x20: return sig_uint(t, s, &v) && skip_type(t, s, depth + 1);
        case 0x14:
            if (!skip_type(t, s, depth + 1) || !sig_uint(t, s, &v) || !sig_uint(t, s, &n)) {
                return 0;
            }
            for (i = 0; i < n; ++i) {
                if (!sig_uint(t, s, &v)) {
                    return 0;
                }
            }
            if (!sig_uint(t, s, &n)) {
                return 0;
            }
            for (i = 0; i < n; ++i) {
                if (!sig_int(t, s, &lo)) {
                    return 0;
                }
            }
            return 1;
        case 0x15:
            if (!sig_byte(t, s, &e) || !sig_uint(t, s, &v) || !sig_uint(t, s, &n)) {
                return 0;
            }
            for (i = 0; i < n; ++i) {
                if (!skip_type(t, s, depth + 1)) {
                    return 0;
                }
            }
            return 1;
        case 0x1B: {
            uint8_t conv;
            if (!sig_byte(t, s, &conv) || ((conv & 0x10u) != 0 && !sig_uint(t, s, &v)) || !sig_uint(t, s, &n) || !skip_type(t, s, depth + 1)) {
                return 0;
            }
            for (i = 0; i < n; ++i) {
                uint8_t b;
                if (sig_peek(s, &b) && b == 0x41) {
                    ++s->at;
                }
                if (!skip_type(t, s, depth + 1)) {
                    return 0;
                }
            }
            return 1;
        }
        default: return fail(t);
    }
}

/* ---- method signatures ---- */

typedef struct method_sig {
    uint8_t conv;
    uint32_t generic_count;
    uint32_t param_count;
    size_t ret_at; /* cursor at the return type */
} method_sig;

static int read_method_head(text *t, sig *s, method_sig *m)
{
    if (!sig_byte(t, s, &m->conv)) {
        return 0;
    }
    if ((m->conv & 0x0Fu) > 0x05u && (m->conv & 0x0Fu) != 0x09u) {
        return fail(t); /* field, local, property or instantiation */
    }
    m->generic_count = 0;
    if ((m->conv & 0x10u) != 0 && !sig_uint(t, s, &m->generic_count)) {
        return 0;
    }
    if (!sig_uint(t, s, &m->param_count)) {
        return 0;
    }
    m->ret_at = s->at;
    return 1;
}

static void put_conv(text *t, uint8_t conv)
{
    if (conv & 0x20u) {
        put_str(t, "instance ");
    }
    if (conv & 0x40u) {
        put_str(t, "explicit ");
    }
    switch (conv & 0x0Fu) {
        case 0x01: put_str(t, "unmanaged cdecl "); break;
        case 0x02: put_str(t, "unmanaged stdcall "); break;
        case 0x03: put_str(t, "unmanaged thiscall "); break;
        case 0x04: put_str(t, "unmanaged fastcall "); break;
        case 0x05: put_str(t, "vararg "); break;
        case 0x09: put_str(t, "unmanaged "); break;
        default: break;
    }
}

/* "(params)" at the cursor after the return type. */
static int put_params(text *t, const ctx *c, sig *s, uint32_t count, int depth)
{
    uint32_t i;
    int first = 1;

    put_char(t, '(');
    for (i = 0; i < count; ++i) {
        uint8_t b;
        if (!going(t)) {
            return 0;
        }
        if (!first) {
            put_str(t, ", ");
        }
        first = 0;
        if (sig_peek(s, &b) && b == 0x41) {
            ++s->at;
            put_str(t, "..., ");
        }
        if (!put_sig_type(t, c, s, depth)) {
            return 0;
        }
    }
    put_char(t, ')');
    return going(t);
}

/* A whole method signature without a name: "ret(params)" (calli), or with
 * fnptr "ret *(params)". */
static int put_method_sig(text *t, const ctx *c, sig *s, int depth, int fnptr)
{
    method_sig m;
    if (depth > MAX_DEPTH || !read_method_head(t, s, &m)) {
        return 0;
    }
    put_conv(t, m.conv);
    if (!put_sig_type(t, c, s, depth + 1)) {
        return 0;
    }
    put_str(t, fnptr ? " *" : "");
    return put_params(t, c, s, m.param_count, depth + 1);
}

/* Prints the parent of a member ("Type::"). */
typedef struct member {
    uint32_t parent;    /* TypeDef/TypeRef/TypeSpec/ModuleRef/MethodDef token, 0 for none */
    uint32_t name;      /* #Strings */
    uint32_t signature; /* #Blob */
    uint32_t inst;      /* MethodSpec instantiation #Blob, 0 for none */
    int has_inst;
    int is_ref; /* a MemberRef */
} member;

static int put_parent(text *t, const ctx *c, uint32_t parent, int depth)
{
    uint32_t row = XXBYTE_DOTNET_TOKEN_ROW(parent);

    if (parent == 0) {
        return 1;
    }
    switch (XXBYTE_DOTNET_TOKEN_TABLE(parent)) {
        case XXBYTE_DOTNET_TABLE_MODULEREF:
            if (!valid_row(c->image, TABLE_MODULEREF, row)) {
                return fail(t);
            }
            put_str(t, "[.module ");
            if (!put_heap_name(t, c, col(c, TABLE_MODULEREF, row, 0))) {
                return 0;
            }
            put_char(t, ']');
            break;
        case XXBYTE_DOTNET_TABLE_METHODDEF: {
            uint32_t owner;
            if (!valid_row(c->image, TABLE_METHODDEF, row)) {
                return fail(t);
            }
            owner = method_owner(c, row);
            if (owner <= 1) {
                return 1; /* a global method */
            }
            if (!put_typedef(t, c, owner, depth)) {
                return 0;
            }
            break;
        }
        default:
            if (!put_type_token(t, c, parent, depth)) {
                return 0;
            }
            break;
    }
    put_str(t, "::");
    return going(t);
}

static int put_inst(text *t, const ctx *c, uint32_t blob, int depth)
{
    sig s;
    uint8_t b;
    uint32_t n, i;
    if (!open_blob(c, blob, &s) || !sig_byte(t, &s, &b) || b != 0x0A || !sig_uint(t, &s, &n) || n == 0) {
        return fail(t);
    }
    put_char(t, '<');
    for (i = 0; i < n; ++i) {
        if (i != 0) {
            put_char(t, ',');
        }
        if (!put_sig_type(t, c, &s, depth + 1)) {
            return 0;
        }
    }
    put_char(t, '>');
    return going(t);
}

static int put_member(text *t, const ctx *c, const member *m, int depth)
{
    sig s;
    uint8_t b;

    if (depth > MAX_DEPTH || !open_blob(c, m->signature, &s) || !sig_peek(&s, &b)) {
        return fail(t);
    }
    if (b == 0x06) { /* FIELD */
        ++s.at;
        if (!put_sig_type(t, c, &s, depth + 1)) {
            return 0;
        }
        put_char(t, ' ');
        if (!put_parent(t, c, m->parent, depth + 1) || !put_heap_name(t, c, m->name)) {
            return 0;
        }
        return going(t);
    } else {
        method_sig h;
        if (!read_method_head(t, &s, &h)) {
            return 0;
        }
        put_conv(t, h.conv);
        if (!put_sig_type(t, c, &s, depth + 1)) {
            return 0;
        }
        put_char(t, ' ');
        if (!put_parent(t, c, m->parent, depth + 1) || !put_heap_name(t, c, m->name)) {
            return 0;
        }
        if (m->has_inst) {
            if (!put_inst(t, c, m->inst, depth)) {
                return 0;
            }
        } else if (h.generic_count != 0 && m->is_ref) {
            /* A MemberRef to an uninstantiated generic method: its arity,
             * as monodis prints it ("Contains<[1]>"). */
            put_str(t, "<[");
            put_uint(t, h.generic_count);
            put_str(t, "]>");
        }
        return put_params(t, c, &s, h.param_count, depth + 1);
    }
}

static int load_member(text *t, const ctx *c, uint32_t token, member *m, int depth)
{
    uint32_t row = XXBYTE_DOTNET_TOKEN_ROW(token);

    memset(m, 0, sizeof(*m));
    switch (XXBYTE_DOTNET_TOKEN_TABLE(token)) {
        case XXBYTE_DOTNET_TABLE_FIELD:
            if (!valid_row(c->image, TABLE_FIELD, row)) {
                return fail(t);
            }
            m->name = col(c, TABLE_FIELD, row, 1);
            m->signature = col(c, TABLE_FIELD, row, 2);
            row = field_owner(c, row);
            m->parent = row > 1 ? XXBYTE_DOTNET_TOKEN(TABLE_TYPEDEF, row) : 0;
            return 1;
        case XXBYTE_DOTNET_TABLE_METHODDEF:
            if (!valid_row(c->image, TABLE_METHODDEF, row)) {
                return fail(t);
            }
            m->name = col(c, TABLE_METHODDEF, row, 3);
            m->signature = col(c, TABLE_METHODDEF, row, 4);
            row = method_owner(c, row);
            m->parent = row > 1 ? XXBYTE_DOTNET_TOKEN(TABLE_TYPEDEF, row) : 0;
            return 1;
        case XXBYTE_DOTNET_TABLE_MEMBERREF:
            if (!valid_row(c->image, TABLE_MEMBERREF, row)) {
                return fail(t);
            }
            m->is_ref = 1;
            m->parent = col(c, TABLE_MEMBERREF, row, 0);
            m->name = col(c, TABLE_MEMBERREF, row, 1);
            m->signature = col(c, TABLE_MEMBERREF, row, 2);
            if (XXBYTE_DOTNET_TOKEN_TABLE(m->parent) == XXBYTE_DOTNET_TOKEN_BAD_TAG) {
                return fail(t);
            }
            if (XXBYTE_DOTNET_TOKEN_ROW(m->parent) == 0 || m->parent == XXBYTE_DOTNET_TOKEN(TABLE_TYPEDEF, 1)) {
                m->parent = 0;
            }
            return 1;
        case XXBYTE_DOTNET_TABLE_METHODSPEC: {
            uint32_t method;
            if (depth > 0 || !valid_row(c->image, TABLE_METHODSPEC, row)) {
                return fail(t);
            }
            method = col(c, TABLE_METHODSPEC, row, 0);
            if (XXBYTE_DOTNET_TOKEN_TABLE(method) == XXBYTE_DOTNET_TABLE_METHODSPEC || !load_member(t, c, method, m, depth + 1)) {
                return fail(t);
            }
            m->inst = col(c, TABLE_METHODSPEC, row, 1);
            m->has_inst = 1;
            return 1;
        }
        default: return fail(t);
    }
}

/* Is the member's signature a field signature? */
static int member_is_field(const ctx *c, const member *m)
{
    sig s;
    return open_blob(c, m->signature, &s) && s.n != 0 && s.p[0] == 0x06;
}

static void put_user_string(text *t, const ctx *c, uint32_t index)
{
    const uint8_t *u;
    size_t n, i;

    if (xxbyte_dotnet_get_user_string(c->image, index, &u, &n) != XXBYTE_STATUS_OK) {
        fail(t);
        return;
    }
    put_char(t, '"');
    for (i = 0; i + 1 < n && !t->full; i += 2) {
        unsigned ch = (unsigned)u[i] | ((unsigned)u[i + 1] << 8);
        switch (ch) {
            case '"': put_str(t, "\\\""); break;
            case '\\': put_str(t, "\\\\"); break;
            case 0: put_str(t, "\\0"); break;
            case 7: put_str(t, "\\a"); break;
            case 8: put_str(t, "\\b"); break;
            case 12: put_str(t, "\\f"); break;
            case 10: put_str(t, "\\n"); break;
            case 13: put_str(t, "\\r"); break;
            case 9: put_str(t, "\\t"); break;
            case 11: put_str(t, "\\v"); break;
            default:
                if (ch >= 0x20 && ch <= 0x7E) {
                    put_char(t, (char)ch);
                } else {
                    put_str(t, "\\u");
                    put_hex(t, ch, 4);
                }
                break;
        }
    }
    put_char(t, '"');
}

static int put_standalone(text *t, const ctx *c, uint32_t row)
{
    sig s;
    uint8_t b;

    if (!valid_row(c->image, TABLE_STANDALONESIG, row) || !open_blob(c, col(c, TABLE_STANDALONESIG, row, 0), &s) || !sig_peek(&s, &b)) {
        return fail(t);
    }
    if (b == 0x07) { /* LOCAL_SIG */
        uint32_t n, i;
        ++s.at;
        if (!sig_uint(t, &s, &n)) {
            return 0;
        }
        put_char(t, '(');
        for (i = 0; i < n; ++i) {
            if (i != 0) {
                put_str(t, ", ");
            }
            if (!put_sig_type(t, c, &s, 1)) {
                return 0;
            }
        }
        put_char(t, ')');
        return going(t);
    }
    return put_method_sig(t, c, &s, 0, 0);
}

/* operand: XXBYTE_DOTNET_OPERAND_INLINE_TOK adds "method "/"field " before
 * members; 0 for the plain token text. */
static int put_token(text *t, const ctx *c, uint32_t token, int operand)
{
    uint32_t table = XXBYTE_DOTNET_TOKEN_TABLE(token);
    uint32_t row = XXBYTE_DOTNET_TOKEN_ROW(token);

    switch (table) {
        case XXBYTE_DOTNET_TABLE_TYPEDEF:
        case XXBYTE_DOTNET_TABLE_TYPEREF:
        case XXBYTE_DOTNET_TABLE_TYPESPEC: return put_type_token(t, c, token, 0);
        case XXBYTE_DOTNET_TABLE_FIELD:
        case XXBYTE_DOTNET_TABLE_METHODDEF:
        case XXBYTE_DOTNET_TABLE_MEMBERREF:
        case XXBYTE_DOTNET_TABLE_METHODSPEC: {
            member m;
            if (!load_member(t, c, token, &m, 0)) {
                return 0;
            }
            if (operand == XXBYTE_DOTNET_OPERAND_INLINE_TOK) {
                put_str(t, member_is_field(c, &m) ? "field " : "method ");
            }
            return put_member(t, c, &m, 0);
        }
        case XXBYTE_DOTNET_TABLE_STANDALONESIG: return put_standalone(t, c, row);
        case XXBYTE_DOTNET_TABLE_MODULEREF:
            if (!valid_row(c->image, TABLE_MODULEREF, row)) {
                return fail(t);
            }
            put_str(t, "[.module ");
            if (!put_heap_name(t, c, col(c, TABLE_MODULEREF, row, 0))) {
                return 0;
            }
            put_char(t, ']');
            return going(t);
        case XXBYTE_DOTNET_TABLE_ASSEMBLYREF:
            if (!valid_row(c->image, TABLE_ASSEMBLYREF, row)) {
                return fail(t);
            }
            put_char(t, '[');
            if (!put_heap_name(t, c, col(c, TABLE_ASSEMBLYREF, row, 6))) {
                return 0;
            }
            put_char(t, ']');
            return going(t);
        case XXBYTE_DOTNET_TOKEN_USER_STRING: put_user_string(t, c, row); return going(t);
        default: return fail(t);
    }
}

static size_t finish(text *t)
{
    if (t->failed || t->cap == 0) {
        if (t->cap != 0) {
            t->buf[0] = '\0';
        }
        return 0;
    }
    t->buf[t->len] = '\0';
    return t->len;
}

static void start(text *t, char *buffer, size_t buffer_size)
{
    memset(t, 0, sizeof(*t));
    t->buf = buffer;
    t->cap = buffer_size;
    if (buffer_size == 0) {
        t->full = 1;
    }
}

static int put_title(text *t, const ctx *c, uint32_t row, sig *rest, method_sig *head);

/* What a render call prints. */
typedef struct job {
    int title; /* method title of row, else token text */
    uint32_t value;
    int operand; /* for token text */
} job;

static void run_job(text *t, const ctx *c, const job *j)
{
    if (j->title) {
        sig rest;
        method_sig head;
        put_title(t, c, j->value, &rest, &head);
    } else {
        put_token(t, c, j->value, j->operand);
    }
}

/*
 * Renders into buffer, snprintf style: the text, cut to buffer_size - 1,
 * and its full length. Text that does not fit is rendered again into a
 * growing buffer to learn the full length; past XXBYTE_RESOLVER_NAME_LIMIT
 * it stays cut and the cut length is returned.
 */
static size_t render(const xxbyte_dotnet_names *names, const job *j, char *buffer, size_t buffer_size)
{
    text t;
    ctx c;
    size_t n;

    if (names == NULL || names->image == NULL || buffer == NULL || buffer_size == 0) {
        return 0;
    }
    c.names = names;
    c.image = names->image;
    start(&t, buffer, buffer_size);
    run_job(&t, &c, j);
    if (!t.full || t.failed) {
        return finish(&t);
    }
    n = finish(&t); /* the cut text stays in buffer */
    {
        text g;
        start(&g, (char *)xx_mem_alloc(buffer_size * 4u), buffer_size * 4u);
        if (g.buf == NULL) {
            return n;
        }
        g.grow = 1;
        run_job(&g, &c, j);
        if (!g.failed && !g.full) {
            n = g.len;
        }
        xx_mem_free(g.buf);
    }
    return n;
}

static size_t token_text(const xxbyte_dotnet_names *names, uint32_t token, int operand, char *buffer, size_t buffer_size)
{
    job j;
    j.title = 0;
    j.value = token;
    j.operand = operand;
    return render(names, &j, buffer, buffer_size);
}

size_t xxbyte_dotnet_token_name(const xxbyte_dotnet_names *names, uint32_t token, char *buffer, size_t buffer_size)
{
    return token_text(names, token, 0, buffer, buffer_size);
}

size_t xxbyte_dotnet_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_operand *operand;

    if (context == NULL || instruction == NULL || operand_index >= instruction->operand_count || instruction->family != XXBYTE_FAMILY_DOTNET) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (operand->type != XXBYTE_OPERAND_TOKEN) {
        return 0;
    }
    return token_text((const xxbyte_dotnet_names *)context, (uint32_t)operand->value, (int)xxbyte_dotnet_operand_type_of(instruction->id), buffer, buffer_size);
}

/* ---- method titles ---- */

/* Generic parameter names of a TypeOrMethodDef owner: "<T,U>". GenericParam
 * is sorted by owner (II.22.20), so the first row is found by binary
 * search. */
static void put_generic_names(text *t, const ctx *c, uint32_t owner_token, uint32_t count)
{
    uint32_t rows = c->image->rows[TABLE_GENERICPARAM];
    uint32_t lo = 1, hi = rows, first = 0, i;

    if (count == 0) {
        return;
    }
    while (lo <= hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        uint32_t owner = col(c, TABLE_GENERICPARAM, mid, 2);
        if (owner >= owner_token) {
            if (owner == owner_token) {
                first = mid;
            }
            hi = mid - 1u;
        } else {
            lo = mid + 1u;
        }
    }
    put_char(t, '<');
    for (i = 0; i < count && going(t); ++i) {
        const char *s;
        size_t n;
        uint32_t row = first != 0 ? first + i : 0;
        if (i != 0) {
            put_char(t, ',');
        }
        if (row != 0 && row <= rows && col(c, TABLE_GENERICPARAM, row, 2) == owner_token && col(c, TABLE_GENERICPARAM, row, 0) == i &&
            string_at(c, col(c, TABLE_GENERICPARAM, row, 3), &s, &n) && n != 0) {
            put_name(t, s, n);
        } else {
            put_str(t, "!!");
            put_uint(t, i);
        }
    }
    put_char(t, '>');
}

static int put_title(text *t, const ctx *c, uint32_t row, sig *rest, method_sig *head)
{
    uint32_t owner;
    sig s;

    if (!valid_row(c->image, TABLE_METHODDEF, row)) {
        return fail(t);
    }
    owner = method_owner(c, row);
    if (owner > 1) {
        if (!put_typedef(t, c, owner, 0)) {
            return 0;
        }
        put_str(t, "::");
    }
    if (!put_heap_name(t, c, col(c, TABLE_METHODDEF, row, 3)) || !open_blob(c, col(c, TABLE_METHODDEF, row, 4), &s) || !read_method_head(t, &s, head)) {
        return 0;
    }
    put_generic_names(t, c, XXBYTE_DOTNET_TOKEN(TABLE_METHODDEF, row), head->generic_count);
    if (!skip_type(t, &s, 0)) {
        return 0;
    }
    *rest = s;
    return put_params(t, c, &s, head->param_count, 1);
}

size_t xxbyte_dotnet_method_title(const xxbyte_dotnet_names *names, uint32_t row, char *buffer, size_t buffer_size)
{
    job j;
    j.title = 1;
    j.value = row;
    j.operand = 0;
    return render(names, &j, buffer, buffer_size);
}

/* ---- name tables ---- */

/* Fills owner[row] for the rows of target listed by the TypeDef column
 * list_column, through ptr_table when it has rows. */
static void fill_owners(const ctx *c, uint32_t *owner, uint32_t list_column, uint32_t target, uint32_t ptr_table)
{
    uint32_t types = c->image->rows[TABLE_TYPEDEF];
    uint32_t count = c->image->rows[ptr_table] != 0 ? c->image->rows[ptr_table] : c->image->rows[target];
    uint32_t t, prev_end = 1;

    for (t = 1; t <= types; ++t) {
        uint32_t begin = col(c, TABLE_TYPEDEF, t, list_column);
        uint32_t end = t < types ? col(c, TABLE_TYPEDEF, t + 1, list_column) : count + 1u;
        uint32_t p;

        /* Clamp to an ascending partition so a hostile file stays linear. */
        if (begin < prev_end) {
            begin = prev_end;
        }
        if (end > count + 1u) {
            end = count + 1u;
        }
        for (p = begin; p < end; ++p) {
            uint32_t row = c->image->rows[ptr_table] != 0 ? col(c, ptr_table, p, 0) : p;
            if (row != 0 && row <= c->image->rows[target]) {
                owner[row] = t;
            }
        }
        if (end > prev_end) {
            prev_end = end;
        }
    }
}

xxbyte_status xxbyte_dotnet_names_init(xxbyte_dotnet_names *names, const xxbyte_dotnet_image *image)
{
    size_t types, methods, fields, total;
    uint32_t *mem, i;
    ctx c;

    if (names == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    memset(names, 0, sizeof(*names));
    if (image == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    types = (size_t)image->rows[TABLE_TYPEDEF] + 1u;
    methods = (size_t)image->rows[TABLE_METHODDEF] + 1u;
    fields = (size_t)image->rows[TABLE_FIELD] + 1u;
    total = types + methods + fields;
    mem = (uint32_t *)xx_mem_calloc(total, sizeof(uint32_t));
    if (mem == NULL) {
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    names->image = image;
    names->enclosing = mem;
    names->method_owner = mem + types;
    names->field_owner = mem + types + methods;
    c.names = names;
    c.image = image;
    for (i = 1; i <= image->rows[TABLE_NESTEDCLASS]; ++i) {
        uint32_t nested = col(&c, TABLE_NESTEDCLASS, i, 0);
        uint32_t outer = col(&c, TABLE_NESTEDCLASS, i, 1);
        if (nested != 0 && nested < types && outer != 0 && outer < types && nested != outer) {
            names->enclosing[nested] = outer;
        }
    }
    if (image->rows[TABLE_TYPEDEF] != 0) {
        fill_owners(&c, names->method_owner, 5, TABLE_METHODDEF, TABLE_METHODPTR);
        fill_owners(&c, names->field_owner, 4, TABLE_FIELD, TABLE_FIELDPTR);
    }
    return XXBYTE_STATUS_OK;
}

void xxbyte_dotnet_names_free(xxbyte_dotnet_names *names)
{
    if (names != NULL) {
        xx_mem_free(names->enclosing);
        memset(names, 0, sizeof(*names));
    }
}

/* ---- listing ---- */

#define LINE_SIZE 4096u

/* A growing line for listing headers. */
typedef struct line_buf {
    char *p;
    size_t cap;
} line_buf;

/* Renders a job into the line, growing it to the full text (up to
 * XXBYTE_RESOLVER_NAME_LIMIT). Returns the length, 0 if nothing resolved. */
static size_t fetch(const xxbyte_dotnet_names *names, const job *j, line_buf *line)
{
    size_t n = render(names, j, line->p, line->cap);
    if (n >= line->cap) {
        char *p = (char *)xx_mem_realloc(line->p, n + 1);
        if (p == NULL) {
            return line->cap - 1; /* keep the cut text */
        }
        line->p = p;
        line->cap = n + 1;
        n = render(names, j, line->p, line->cap);
        if (n >= line->cap) {
            n = line->cap - 1;
        }
    }
    return n;
}

static size_t fetch_token(const xxbyte_dotnet_names *names, uint32_t token, line_buf *line)
{
    job j;
    j.title = 0;
    j.value = token;
    j.operand = 0;
    return fetch(names, &j, line);
}

static int append(xx_buf_t *out, const char *s)
{
    return xx_buf_append_str(out, s) ? 1 : 0;
}

static const char *clause_kind(uint32_t flags)
{
    switch (flags) {
        case XXBYTE_DOTNET_CLAUSE_EXCEPTION: return "catch";
        case XXBYTE_DOTNET_CLAUSE_FILTER: return "filter";
        case XXBYTE_DOTNET_CLAUSE_FINALLY: return "finally";
        case XXBYTE_DOTNET_CLAUSE_FAULT: return "fault";
        default: return NULL;
    }
}

static int list_clauses(const ctx *c, const xxbyte_dotnet_method *m, xx_buf_t *out, line_buf *line)
{
    xxbyte_dotnet_exception_clause *clauses;
    size_t count = 0, i;
    xxbyte_status status;
    const uint8_t *body = c->image->data + m->body_offset;

    status = xxbyte_dotnet_parse_exception_clauses(body, m->available, &m->header, NULL, 0, &count);
    if (status != XXBYTE_STATUS_OK) {
        return xx_buf_appendf(out, "// bad exception clauses: %s\n", xxbyte_status_string(status)) ? 1 : 0;
    }
    if (count == 0) {
        return 1;
    }
    clauses = (xxbyte_dotnet_exception_clause *)xx_mem_alloc(count * sizeof(*clauses));
    if (clauses == NULL) {
        return 0;
    }
    xxbyte_dotnet_parse_exception_clauses(body, m->available, &m->header, clauses, count, &count);
    for (i = 0; i < count; ++i) {
        const xxbyte_dotnet_exception_clause *k = &clauses[i];
        const char *kind = clause_kind(k->flags);
        int ok = xx_buf_appendf(out, "// .try IL_%04x to IL_%04x ", k->try_offset, (unsigned)((uint64_t)k->try_offset + k->try_length));
        if (kind == NULL) {
            ok = ok && xx_buf_appendf(out, "flags 0x%x", k->flags);
        } else if (k->flags == XXBYTE_DOTNET_CLAUSE_EXCEPTION) {
            ok = ok && append(out, "catch ");
            if (fetch_token(c->names, k->class_token_or_filter_offset, line) != 0) {
                ok = ok && append(out, line->p);
            } else {
                ok = ok && xx_buf_appendf(out, "0x%08X", k->class_token_or_filter_offset);
            }
        } else if (k->flags == XXBYTE_DOTNET_CLAUSE_FILTER) {
            ok = ok && xx_buf_appendf(out, "filter IL_%04x", k->class_token_or_filter_offset);
        } else {
            ok = ok && append(out, kind);
        }
        ok = ok && xx_buf_appendf(out, " handler IL_%04x to IL_%04x\n", k->handler_offset, (unsigned)((uint64_t)k->handler_offset + k->handler_length));
        if (!ok) {
            xx_mem_free(clauses);
            return 0;
        }
    }
    xx_mem_free(clauses);
    return 1;
}

/* "// token 0x06000001  returns void  code size 7  max stack 8  locals init (int32)" */
static int list_details(const ctx *c, uint32_t row, const xxbyte_dotnet_method *m, xx_buf_t *out, line_buf *line)
{
    text t;
    sig s;
    method_sig head;

    if (!xx_buf_appendf(out, "// token 0x%08X  ", m->token)) {
        return 0;
    }
    start(&t, line->p, line->cap);
    t.grow = 1; /* line->p is xx_mem memory */
    if (open_blob(c, m->signature, &s) && read_method_head(&t, &s, &head)) {
        put_conv(&t, head.conv);
        if (t.len != 0) {
            t.buf[t.len - 1] = ','; /* "instance vararg," */
            put_char(&t, ' ');
        }
        put_str(&t, "returns ");
        put_sig_type(&t, c, &s, 1);
    }
    line->p = t.buf;
    line->cap = t.cap;
    if (finish(&t) != 0 && !(xx_buf_append_str(out, line->p) && xx_buf_append_str(out, "  "))) {
        return 0;
    }
    if (!xx_buf_appendf(out, "%s header  code size %u  max stack %u", m->header.is_fat ? "fat" : "tiny", m->header.code_size, m->header.max_stack)) {
        return 0;
    }
    if (m->header.local_var_sig_token != 0) {
        size_t n = fetch_token(c->names, m->header.local_var_sig_token, line);
        if (!xx_buf_appendf(out, "  locals%s ", (m->header.flags & XXBYTE_DOTNET_METHOD_INIT_LOCALS) ? " init" : "")) {
            return 0;
        }
        if (n != 0 && line->p[0] == '(') {
            if (!append(out, line->p)) {
                return 0;
            }
        } else if (!xx_buf_appendf(out, "0x%08X", m->header.local_var_sig_token)) {
            return 0;
        }
    }
    (void)row;
    return append(out, "\n");
}

xxbyte_status xxbyte_dotnet_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out)
{
    xxbyte_dotnet_image image;
    xxbyte_dotnet_names names;
    xxbyte_status status, result = XXBYTE_STATUS_OK;
    line_buf line;
    uint32_t row;
    uint64_t budget;
    ctx c;

    (void)mode; /* XXBYTE_MODE_AUTO or the single mode */
    status = xxbyte_dotnet_open_image(data, size, &image);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    status = xxbyte_dotnet_names_init(&names, &image);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    line.cap = LINE_SIZE;
    line.p = (char *)xx_mem_alloc(LINE_SIZE);
    if (line.p == NULL) {
        xxbyte_dotnet_names_free(&names);
        return XXBYTE_STATUS_OUT_OF_MEMORY;
    }
    c.names = &names;
    c.image = &image;
    /* Bodies may overlap (many MethodDefs can share one RVA); bound the
     * listed bytes so a hostile file cannot make the listing quadratic. */
    budget = (uint64_t)image.size * 8u + (1u << 20);
    for (row = 1; row <= image.rows[TABLE_METHODDEF] && row <= 0x00FFFFFFu; ++row) {
        xxbyte_dotnet_method m;
        size_t n_title;
        int ok;

        if (xxbyte_dotnet_read_method(&image, row, &m, 0) != XXBYTE_STATUS_OK || m.body_status == XXBYTE_STATUS_END_OF_INPUT) {
            continue;
        }
        {
            job j;
            j.title = 1;
            j.value = row;
            j.operand = 0;
            n_title = fetch(&names, &j, &line);
        }
        if (n_title == 0) {
            text t;
            start(&t, line.p, line.cap);
            put_str(&t, "method 0x");
            put_hex(&t, m.token, 8);
            finish(&t);
        }
        ok = append(out, "// ") && append(out, line.p) && xx_buf_appendf(out, "  RVA 0x%X\n", m.rva);
        if (!ok) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        if (m.body_status != XXBYTE_STATUS_OK) {
            if (!xx_buf_appendf(out, "// token 0x%08X  bad method body: %s\n\n", m.token, xxbyte_status_string(m.body_status))) {
                result = XXBYTE_STATUS_OUT_OF_MEMORY;
                break;
            }
            if (result == XXBYTE_STATUS_OK) {
                result = m.body_status;
            }
            continue;
        }
        if (m.body_size > budget) {
            if (!append(out, "// listing stopped: method bodies overlap too much\n")) {
                result = XXBYTE_STATUS_OUT_OF_MEMORY;
            } else {
                result = XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            break;
        }
        budget -= m.body_size + 64u > budget ? budget : m.body_size + 64u;
        if (!list_details(&c, row, &m, out, &line) || !list_clauses(&c, &m, out, &line)) {
            result = XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_DOTNET, XXBYTE_DOTNET_MODE_DEFAULT, NULL, image.data + m.body_offset + m.header.header_size, m.header.code_size, 0,
                                      listing_flags, xxbyte_dotnet_resolve, &names, out);
        if (status != XXBYTE_STATUS_OK || !append(out, "\n")) {
            result = status != XXBYTE_STATUS_OK ? status : XXBYTE_STATUS_OUT_OF_MEMORY;
            break;
        }
    }
    xx_mem_free(line.p);
    xxbyte_dotnet_names_free(&names);
    return result;
}

#else
typedef int xxbyte_dotnet_names_disabled;
#endif
