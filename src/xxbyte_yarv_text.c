#include "xxbyte_yarv_ibf.h"

#if XXBYTE_USE_YARV

/* Ruby's inspect text of IBF objects, as RubyVM::InstructionSequence#disasm
 * prints them with Encoding.default_external UTF-8. */

void yarv_text_init(yarv_text *text, const yarv_image *image, xx_buf_t *out, size_t limit)
{
    text->image = image;
    text->out = out;
    text->limit = out->size + limit;
    text->nodes = 0;
}

int yarv_text_full(const yarv_text *text)
{
    return text->out->size >= text->limit;
}

int yarv_put_bytes(yarv_text *text, const uint8_t *bytes, size_t length)
{
    size_t room;

    if (text->out->size >= text->limit) {
        return 0;
    }
    room = text->limit - text->out->size;
    if (length > room) {
        if (!xx_buf_append(text->out, bytes, room)) {
            return 0;
        }
        return 0;
    }
    return length == 0 || xx_buf_append(text->out, bytes, length) ? 1 : 0;
}

int yarv_put(yarv_text *text, const char *s)
{
    return yarv_put_bytes(text, (const uint8_t *)s, strlen(s));
}

static int yarv_putc(yarv_text *text, char c)
{
    return yarv_put_bytes(text, (const uint8_t *)&c, 1);
}

static int yarv_putf(yarv_text *text, const char *format, unsigned long long value)
{
    char buffer[32];
    xx_buf_t tmp;
    int ok;

    xx_buf_init(&tmp);
    ok = xx_buf_appendf(&tmp, format, value) ? 1 : 0;
    if (ok && tmp.size < sizeof(buffer)) {
        memcpy(buffer, tmp.data, tmp.size);
        ok = yarv_put_bytes(text, (const uint8_t *)buffer, tmp.size);
    } else {
        ok = 0;
    }
    xx_buf_free(&tmp);
    return ok;
}

static int yarv_put_signed(yarv_text *text, int64_t value)
{
    if (value < 0) {
        return yarv_putc(text, '-') && yarv_putf(text, "%llu", 0ull - (unsigned long long)value);
    }
    return yarv_putf(text, "%llu", (unsigned long long)value);
}

/* Encodings. */

typedef enum yarv_enc_kind {
    ENC_BINARY, /* ASCII-8BIT and unknown single-byte encodings */
    ENC_ASCII,  /* US-ASCII */
    ENC_UTF8,
    ENC_UTF16LE,
    ENC_UTF16BE,
    ENC_UTF32LE,
    ENC_UTF32BE,
    ENC_EUCJP,
    ENC_SJIS,
    ENC_BIG5, /* Big5 and its variants */
    ENC_GBK,  /* GBK, CP936 */
    ENC_GB18030,
    ENC_EUCKR, /* EUC-KR, GB2312, GB12345 (two bytes 0xA1-0xFE) */
    ENC_CP949,
    ENC_EUCTW
} yarv_enc_kind;

typedef struct yarv_enc {
    yarv_enc_kind kind;
    int resenc; /* the encoding is UTF-8, the result encoding */
} yarv_enc;

static int yarv_name_is(const uint8_t *name, uint32_t length, const char *s)
{
    size_t n = strlen(s);
    size_t i;

    if (length != n) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        uint8_t a = name[i];
        uint8_t b = (uint8_t)s[i];
        if (a >= 'a' && a <= 'z') {
            a = (uint8_t)(a - 32);
        }
        if (b >= 'a' && b <= 'z') {
            b = (uint8_t)(b - 32);
        }
        if (a != b) {
            return 0;
        }
    }
    return 1;
}

static void yarv_encoding(const yarv_image *image, uint32_t index, yarv_enc *enc)
{
    enc->resenc = 0;
    switch (index) {
        case YARV_ENC_ASCII_8BIT: enc->kind = ENC_BINARY; return;
        case YARV_ENC_UTF_8:
        case YARV_ENC_UTF8_MAC:
            enc->kind = ENC_UTF8;
            enc->resenc = index == YARV_ENC_UTF_8;
            return;
        case YARV_ENC_US_ASCII: enc->kind = ENC_ASCII; return;
        case YARV_ENC_UTF_16BE:
        case YARV_ENC_UTF_16: enc->kind = ENC_UTF16BE; return;
        case YARV_ENC_UTF_16LE: enc->kind = ENC_UTF16LE; return;
        case YARV_ENC_UTF_32BE:
        case YARV_ENC_UTF_32: enc->kind = ENC_UTF32BE; return;
        case YARV_ENC_UTF_32LE: enc->kind = ENC_UTF32LE; return;
        case YARV_ENC_EUC_JP: enc->kind = ENC_EUCJP; return;
        case YARV_ENC_WINDOWS_31J: enc->kind = ENC_SJIS; return;
        case YARV_ENC_BUILTIN_MAX:
            /* The dumper stores indexes above 12 by name, so 12 itself
             * names the writer's first loaded encoding: Big5 in the
             * encoding database order. */
            enc->kind = ENC_BIG5;
            return;
        default: break;
    }
    enc->kind = ENC_BINARY;
    if (index > YARV_ENC_BUILTIN_MAX) {
        xxbyte_yarv_object name;
        const uint8_t *bytes;
        uint32_t length;
        uint32_t ignored;
        if (yarv_object(image, index - YARV_ENC_BUILTIN_MAX, &name) && yarv_string_bytes(image, &name, &bytes, &length, &ignored)) {
            if (yarv_name_is(bytes, length, "Shift_JIS") || yarv_name_is(bytes, length, "SJIS-DoCoMo") || yarv_name_is(bytes, length, "SJIS-KDDI") ||
                yarv_name_is(bytes, length, "SJIS-SoftBank") || yarv_name_is(bytes, length, "MacJapanese")) {
                enc->kind = ENC_SJIS;
            } else if (yarv_name_is(bytes, length, "eucJP-ms") || yarv_name_is(bytes, length, "CP51932") || yarv_name_is(bytes, length, "EUC-JIS-2004")) {
                enc->kind = ENC_EUCJP;
            } else if (yarv_name_is(bytes, length, "UTF8-DoCoMo") || yarv_name_is(bytes, length, "UTF8-KDDI") || yarv_name_is(bytes, length, "UTF8-SoftBank")) {
                enc->kind = ENC_UTF8;
            } else if (yarv_name_is(bytes, length, "Big5") || yarv_name_is(bytes, length, "Big5-HKSCS") || yarv_name_is(bytes, length, "Big5-UAO") ||
                       yarv_name_is(bytes, length, "CP950") || yarv_name_is(bytes, length, "CP951")) {
                enc->kind = ENC_BIG5;
            } else if (yarv_name_is(bytes, length, "GBK") || yarv_name_is(bytes, length, "CP936")) {
                enc->kind = ENC_GBK;
            } else if (yarv_name_is(bytes, length, "GB18030")) {
                enc->kind = ENC_GB18030;
            } else if (yarv_name_is(bytes, length, "EUC-KR") || yarv_name_is(bytes, length, "GB2312") || yarv_name_is(bytes, length, "GB12345")) {
                enc->kind = ENC_EUCKR;
            } else if (yarv_name_is(bytes, length, "CP949")) {
                enc->kind = ENC_CP949;
            } else if (yarv_name_is(bytes, length, "EUC-TW")) {
                enc->kind = ENC_EUCTW;
            }
        }
    }
}

static int yarv_enc_ascii_compatible(const yarv_enc *enc)
{
    return enc->kind != ENC_UTF16LE && enc->kind != ENC_UTF16BE && enc->kind != ENC_UTF32LE && enc->kind != ENC_UTF32BE;
}

static int yarv_enc_unicode(const yarv_enc *enc)
{
    return enc->kind >= ENC_UTF8 && enc->kind <= ENC_UTF32BE;
}

static unsigned yarv_enc_minlen(const yarv_enc *enc)
{
    switch (enc->kind) {
        case ENC_UTF16LE:
        case ENC_UTF16BE: return 2;
        case ENC_UTF32LE:
        case ENC_UTF32BE: return 4;
        default: return 1;
    }
}

/* One character at p: 1 with its length and code point, 0 when the bytes
 * are not a valid character (rb_enc_precise_mbclen). */
static int yarv_enc_char(const yarv_enc *enc, const uint8_t *p, const uint8_t *end, unsigned *length, uint32_t *code)
{
    size_t left = (size_t)(end - p);
    uint8_t c = p[0];

    switch (enc->kind) {
        case ENC_BINARY:
            *length = 1;
            *code = c;
            return 1;
        case ENC_ASCII:
            *length = 1;
            *code = c;
            return c < 0x80;
        case ENC_UTF8: {
            unsigned n;
            uint32_t cp;
            unsigned i;
            uint8_t lo = 0x80;
            uint8_t hi = 0xBF;
            if (c < 0x80) {
                *length = 1;
                *code = c;
                return 1;
            }
            if (c >= 0xC2 && c <= 0xDF) {
                n = 2;
                cp = c & 0x1Fu;
            } else if (c >= 0xE0 && c <= 0xEF) {
                n = 3;
                cp = c & 0x0Fu;
                if (c == 0xE0) {
                    lo = 0xA0;
                } else if (c == 0xED) {
                    hi = 0x9F;
                }
            } else if (c >= 0xF0 && c <= 0xF4) {
                n = 4;
                cp = c & 0x07u;
                if (c == 0xF0) {
                    lo = 0x90;
                } else if (c == 0xF4) {
                    hi = 0x8F;
                }
            } else {
                return 0;
            }
            if (left < n) {
                return 0;
            }
            for (i = 1; i < n; ++i) {
                uint8_t t = p[i];
                if (t < (i == 1 ? lo : 0x80) || t > (i == 1 ? hi : 0xBF)) {
                    return 0;
                }
                cp = (cp << 6) | (t & 0x3Fu);
            }
            *length = n;
            *code = cp;
            return 1;
        }
        case ENC_UTF16LE:
        case ENC_UTF16BE: {
            int le = enc->kind == ENC_UTF16LE;
            uint32_t u;
            if (left < 2) {
                return 0;
            }
            u = le ? (uint32_t)(p[0] | (p[1] << 8)) : (uint32_t)((p[0] << 8) | p[1]);
            if (u >= 0xD800 && u <= 0xDBFF) {
                uint32_t v;
                if (left < 4) {
                    return 0;
                }
                v = le ? (uint32_t)(p[2] | (p[3] << 8)) : (uint32_t)((p[2] << 8) | p[3]);
                if (v < 0xDC00 || v > 0xDFFF) {
                    return 0;
                }
                *length = 4;
                *code = 0x10000u + ((u - 0xD800u) << 10) + (v - 0xDC00u);
                return 1;
            }
            if (u >= 0xDC00 && u <= 0xDFFF) {
                return 0;
            }
            *length = 2;
            *code = u;
            return 1;
        }
        case ENC_UTF32LE:
        case ENC_UTF32BE: {
            uint32_t u;
            if (left < 4) {
                return 0;
            }
            u = enc->kind == ENC_UTF32LE ? ((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24))
                                         : (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
            if (u > 0x10FFFFu || (u >= 0xD800u && u <= 0xDFFFu)) {
                return 0;
            }
            *length = 4;
            *code = u;
            return 1;
        }
        case ENC_EUCJP:
            if (c < 0x80) {
                *length = 1;
                *code = c;
                return 1;
            }
            if (c == 0x8E) {
                if (left < 2 || p[1] < 0xA1 || p[1] > 0xFE) {
                    return 0;
                }
                *length = 2;
                *code = 0x8E00u | p[1];
                return 1;
            }
            if (c == 0x8F) {
                if (left < 3 || p[1] < 0xA1 || p[1] > 0xFE || p[2] < 0xA1 || p[2] > 0xFE) {
                    return 0;
                }
                *length = 3;
                *code = 0x8F0000u | ((uint32_t)p[1] << 8) | p[2];
                return 1;
            }
            if (c >= 0xA1 && c <= 0xFE) {
                if (left < 2 || p[1] < 0xA1 || p[1] > 0xFE) {
                    return 0;
                }
                *length = 2;
                *code = ((uint32_t)c << 8) | p[1];
                return 1;
            }
            return 0;
        case ENC_BIG5:
        case ENC_GBK:
        case ENC_EUCKR:
        case ENC_CP949: {
            uint8_t t;
            int trail;
            if (c < 0x80) {
                *length = 1;
                *code = c;
                return 1;
            }
            if (c == 0x80 || c == 0xFF || (enc->kind == ENC_EUCKR && c < 0xA1) || left < 2) {
                return 0;
            }
            t = p[1];
            switch (enc->kind) {
                case ENC_BIG5: trail = (t >= 0x40 && t <= 0x7E) || (t >= 0xA1 && t <= 0xFE); break;
                case ENC_GBK: trail = t >= 0x40 && t <= 0xFE && t != 0x7F; break;
                case ENC_EUCKR: trail = t >= 0xA1 && t <= 0xFE; break;
                default: trail = (t >= 0x41 && t <= 0x5A) || (t >= 0x61 && t <= 0x7A) || (t >= 0x81 && t <= 0xFE); break;
            }
            if (!trail) {
                return 0;
            }
            *length = 2;
            *code = ((uint32_t)c << 8) | t;
            return 1;
        }
        case ENC_GB18030:
            if (c < 0x80) {
                *length = 1;
                *code = c;
                return 1;
            }
            if (c == 0x80 || c == 0xFF || left < 2) {
                return 0;
            }
            if (p[1] >= 0x30 && p[1] <= 0x39) {
                if (left < 4 || p[2] < 0x81 || p[2] > 0xFE || p[3] < 0x30 || p[3] > 0x39) {
                    return 0;
                }
                *length = 4;
                *code = ((uint32_t)c << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
                return 1;
            }
            if (p[1] < 0x40 || p[1] == 0x7F || p[1] == 0xFF) {
                return 0;
            }
            *length = 2;
            *code = ((uint32_t)c << 8) | p[1];
            return 1;
        case ENC_EUCTW:
            if (c < 0x80) {
                *length = 1;
                *code = c;
                return 1;
            }
            if (c == 0x8E) {
                if (left < 4 || p[1] < 0xA1 || p[1] > 0xB0 || p[2] < 0xA1 || p[2] > 0xFE || p[3] < 0xA1 || p[3] > 0xFE) {
                    return 0;
                }
                *length = 4;
                *code = ((uint32_t)c << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
                return 1;
            }
            if (c < 0xA1 || c > 0xFE || left < 2 || p[1] < 0xA1 || p[1] > 0xFE) {
                return 0;
            }
            *length = 2;
            *code = ((uint32_t)c << 8) | p[1];
            return 1;
        case ENC_SJIS:
            if (c < 0x80 || (c >= 0xA1 && c <= 0xDF)) {
                *length = 1;
                *code = c;
                return 1;
            }
            if ((c >= 0x81 && c <= 0x9F) || (c >= 0xE0 && c <= 0xFC)) {
                if (left < 2 || p[1] < 0x40 || p[1] == 0x7F || p[1] > 0xFC) {
                    return 0;
                }
                *length = 2;
                *code = ((uint32_t)c << 8) | p[1];
                return 1;
            }
            return 0;
    }
    return 0;
}

static int yarv_ascii_print(uint32_t c)
{
    return c >= 0x20 && c < 0x7F;
}

/* rb_enc_isprint for the encoding. */
static int yarv_enc_print(const yarv_text *text, const yarv_enc *enc, uint32_t c)
{
    if (yarv_enc_unicode(enc)) {
        return yarv_unicode_printable(c, text->image->version);
    }
    if (enc->kind >= ENC_EUCJP) {
        return c < 0x80 ? yarv_ascii_print(c) : 1;
    }
    return yarv_ascii_print(c);
}

static int yarv_escaped_char(yarv_text *text, uint32_t c, int unicode)
{
    if (unicode) {
        if (c < 0x7F && yarv_ascii_print(c)) {
            return yarv_putc(text, (char)c);
        }
        if (c < 0x10000) {
            return yarv_putf(text, "\\u%04llX", c);
        }
        return yarv_put(text, "\\u{") && yarv_putf(text, "%llX", c) && yarv_putc(text, '}');
    }
    if (c < 0x100) {
        return yarv_putf(text, "\\x%02llX", c);
    }
    return yarv_put(text, "\\x{") && yarv_putf(text, "%llX", c) && yarv_putc(text, '}');
}

/* rb_str_inspect. */
static int yarv_inspect_string(yarv_text *text, const uint8_t *bytes, uint32_t length, const yarv_enc *enc)
{
    const uint8_t *p = bytes;
    const uint8_t *end = bytes + length;
    const uint8_t *prev = p;
    int unicode = yarv_enc_unicode(enc);
    int ascii = yarv_enc_ascii_compatible(enc);

    if (!yarv_putc(text, '"')) {
        return 0;
    }
    while (p < end) {
        unsigned n;
        uint32_t c;
        char cc;

        if (!yarv_enc_char(enc, p, end, &n, &c)) {
            unsigned k = yarv_enc_minlen(enc);
            if (p > prev && !yarv_put_bytes(text, prev, (size_t)(p - prev))) {
                return 0;
            }
            if ((size_t)(end - p) < k) {
                k = (unsigned)(end - p);
            }
            while (k-- > 0) {
                if (!yarv_putf(text, "\\x%02llX", *p)) {
                    return 0;
                }
                prev = ++p;
            }
            continue;
        }
        p += n;
        if ((ascii || unicode) && (c == '"' || c == '\\' || c == '#')) {
            int escape = c != '#';
            if (c == '#' && p < end) {
                unsigned n2;
                uint32_t c2;
                if (yarv_enc_char(enc, p, end, &n2, &c2) && (c2 == '$' || c2 == '@' || c2 == '{')) {
                    escape = 1;
                }
            }
            if (escape) {
                if (p - n > prev && !yarv_put_bytes(text, prev, (size_t)(p - n - prev))) {
                    return 0;
                }
                if (!yarv_putc(text, '\\')) {
                    return 0;
                }
                if (ascii || enc->resenc) {
                    prev = p - n;
                    continue;
                }
            }
        }
        switch (c) {
            case '\n': cc = 'n'; break;
            case '\r': cc = 'r'; break;
            case '\t': cc = 't'; break;
            case '\f': cc = 'f'; break;
            case '\013': cc = 'v'; break;
            case '\010': cc = 'b'; break;
            case '\007': cc = 'a'; break;
            case 033: cc = 'e'; break;
            default: cc = 0; break;
        }
        if (cc != 0) {
            char escape[2];
            if (p - n > prev && !yarv_put_bytes(text, prev, (size_t)(p - n - prev))) {
                return 0;
            }
            escape[0] = '\\';
            escape[1] = cc;
            if (!yarv_put_bytes(text, (const uint8_t *)escape, 2)) {
                return 0;
            }
            prev = p;
            continue;
        }
        if ((enc->resenc && yarv_enc_print(text, enc, c) && (c != 0x85 || text->image->version < YARV_MODE(3, 2))) || (ascii && c < 0x80 && yarv_ascii_print(c))) {
            continue;
        }
        if (p - n > prev && !yarv_put_bytes(text, prev, (size_t)(p - n - prev))) {
            return 0;
        }
        if (!yarv_escaped_char(text, c, unicode)) {
            return 0;
        }
        prev = p;
    }
    if (p > prev && !yarv_put_bytes(text, prev, (size_t)(p - prev))) {
        return 0;
    }
    return yarv_putc(text, '"');
}

/* Symbol names (rb_enc_symname_type for ASCII-compatible encodings). */

enum {
    ID_LOCAL,
    ID_INSTANCE,
    ID_GLOBAL,
    ID_ATTRSET,
    ID_CONST,
    ID_CLASS,
    ID_JUNK
};
#define IDSET_SYNTAX ((1u << ID_LOCAL) | (1u << ID_CONST))
#define IDSET_INTERN (((1u << 7) - 1u) & ~(1u << ID_ATTRSET))

static int yarv_identchar(uint8_t c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c >= 0x80;
}

static unsigned yarv_mbclen(const yarv_enc *enc, const uint8_t *p, const uint8_t *end)
{
    unsigned n;
    uint32_t c;

    if (yarv_enc_char(enc, p, end, &n, &c)) {
        return n;
    }
    return 1;
}

static int yarv_global_punct(uint8_t c)
{
    return c != 0 && strchr("~*$?!@/\\;,.=:<>\"&`'+0", c) != NULL;
}

static int yarv_special_global(const uint8_t *m, const uint8_t *e, const yarv_enc *enc)
{
    if (m >= e) {
        return 0;
    }
    if (yarv_global_punct(*m)) {
        ++m;
    } else if (*m == '-') {
        if (++m >= e) {
            return 0;
        }
        if (yarv_identchar(*m)) {
            m += yarv_mbclen(enc, m, e);
        }
    } else {
        if (*m < '0' || *m > '9') {
            return 0;
        }
        do {
            ++m;
        } while (m < e && *m >= '0' && *m <= '9');
    }
    return m == e;
}

static int yarv_symname_type_at(const uint8_t *name, size_t len, const yarv_enc *enc, unsigned allowed, unsigned depth)
{
    const uint8_t *e = name + len;
    const uint8_t *m = name;
    int type;
    size_t nread;
    int more;

#define AT(i) ((size_t)(i) < len ? name[i] : 0)
    if (!yarv_enc_ascii_compatible(enc) || len == 0) {
        return -1;
    }
    more = 0;
    nread = 0;
    type = ID_JUNK;
    switch (name[0]) {
        case '\0': return -1;
        case '$':
            if (yarv_special_global(name + 1, e, enc)) {
                return ID_GLOBAL;
            }
            more = 1;
            type = ID_GLOBAL;
            nread = 1;
            break;
        case '@':
            more = 1;
            if (AT(1) == '@') {
                type = ID_CLASS;
                nread = 2;
            } else {
                type = ID_INSTANCE;
                nread = 1;
            }
            break;
        case '<': nread = AT(1) == '<' ? 2 : AT(1) == '=' ? (AT(2) == '>' ? 3 : 2) : 1; break;
        case '>': nread = AT(1) == '>' || AT(1) == '=' ? 2 : 1; break;
        case '=':
            if (AT(1) == '~') {
                nread = 2;
            } else if (AT(1) == '=') {
                nread = AT(2) == '=' ? 3 : 2;
            } else {
                return -1;
            }
            break;
        case '*': nread = AT(1) == '*' ? 2 : 1; break;
        case '+':
        case '-': nread = AT(1) == '@' ? 2 : 1; break;
        case '|':
        case '^':
        case '&':
        case '/':
        case '%':
        case '~':
        case '`': nread = 1; break;
        case '[':
            if (AT(1) == ']') {
                nread = AT(2) == '=' ? 3 : 2;
            } else {
                more = 1;
                nread = 0;
            }
            break;
        case '!':
            if (AT(1) == '=' || AT(1) == '~') {
                nread = 2;
            } else {
                nread = 1;
                more = (allowed & (1u << ID_JUNK)) != 0;
            }
            break;
        default:
            more = 1;
            type = name[0] >= 'A' && name[0] <= 'Z' ? ID_CONST : ID_LOCAL;
            nread = 0;
            break;
    }
    if (!more) {
        return nread == len ? type : -1;
    }
    m = name + nread;
    if (m >= e || (*m != '_' && !((*m >= 'a' && *m <= 'z') || (*m >= 'A' && *m <= 'Z')) && *m < 0x80)) {
        if (len > 1 && e[-1] == '=') {
            /* Ruby recurses once per trailing '='; past the first level
             * the answer is always -1 (no allowed set holds ATTRSET). */
            int inner = depth == 0 ? yarv_symname_type_at(name, len - 1, enc, allowed, depth + 1) : -1;
            if (inner >= 0 && (allowed & (1u << inner)) != 0) {
                return ID_ATTRSET;
            }
        }
        return -1;
    }
    while (m < e && yarv_identchar(*m)) {
        m += yarv_mbclen(enc, m, e);
    }
    if (m >= e) {
        return m == e ? type : -1;
    }
    switch (*m) {
        case '!':
        case '?':
            if (type == ID_GLOBAL || type == ID_CLASS || type == ID_INSTANCE) {
                return -1;
            }
            type = ID_JUNK;
            ++m;
            if (m + 1 < e || (m < e ? *m : 0) != '=') {
                break;
            }
            /* fall through */
        case '=':
            if ((allowed & (1u << type)) == 0) {
                return -1;
            }
            type = ID_ATTRSET;
            ++m;
            break;
        default: break;
    }
    return m == e ? type : -1;
#undef AT
}

static int yarv_symname_type(const uint8_t *name, size_t len, const yarv_enc *enc, unsigned allowed)
{
    return yarv_symname_type_at(name, len, enc, allowed, 0);
}

/* rb_str_symname_p with result encoding UTF-8. */
static int yarv_symname_p(const yarv_text *text, const uint8_t *name, uint32_t length, const yarv_enc *enc)
{
    const uint8_t *p = name;
    const uint8_t *end = name + length;
    uint32_t i;

    if (!enc->resenc) {
        for (i = 0; i < length; ++i) {
            if (name[i] >= 0x80) {
                return 0;
            }
        }
    }
    if (memchr(name, 0, length) != NULL || yarv_symname_type(name, length, enc, IDSET_SYNTAX) == -1) {
        return 0;
    }
    while (p < end) {
        unsigned n;
        uint32_t c;
        if (!yarv_enc_char(enc, p, end, &n, &c) || !yarv_enc_print(text, enc, c)) {
            return 0;
        }
        p += n;
    }
    return 1;
}

static int yarv_symbol_text(yarv_text *text, const uint8_t *bytes, uint32_t length, uint32_t encoding)
{
    yarv_enc enc;

    yarv_encoding(text->image, encoding, &enc);
    if (!yarv_putc(text, ':')) {
        return 0;
    }
    if (yarv_symname_p(text, bytes, length, &enc)) {
        return yarv_put_bytes(text, bytes, length);
    }
    return yarv_inspect_string(text, bytes, length, &enc);
}

/* Numbers. */

static int yarv_float(yarv_text *text, uint64_t bits)
{
    double value;
    xx_buf_t tmp;
    xxbyte_fmt fmt;
    char digits[40];
    size_t ndigits = 0;
    int decpt = 0;
    int negative;
    int ok;
    size_t i;
    const char *s;
    int seen_point = 0;
    int exponent = 0;
    int have_exponent = 0;
    int lead = 1;

    memcpy(&value, &bits, sizeof(value));
    if (value != value) {
        return yarv_put(text, "NaN");
    }
    negative = (bits >> 63) != 0;
    if ((bits & 0x7FFFFFFFFFFFFFFFu) == 0x7FF0000000000000u) {
        return yarv_put(text, negative ? "-Infinity" : "Infinity");
    }
    xx_buf_init(&tmp);
    fmt.out = &tmp;
    fmt.flags = 0;
    fmt.resolver = NULL;
    fmt.context = NULL;
    if (!xxbyte_fmt_double(&fmt, value) || tmp.data == NULL) {
        xx_buf_free(&tmp);
        return 0;
    }
    /* Shortest digits and the decimal point position from the shortest
     * round-trip text. */
    s = tmp.data;
    if (*s == '-') {
        ++s;
    }
    for (; *s != '\0'; ++s) {
        if (*s == '.') {
            seen_point = 1;
        } else if (*s == 'e') {
            {
                const char *q = s + 1;
                int sign = 1;
                if (*q == '+' || *q == '-') {
                    sign = *q == '-' ? -1 : 1;
                    ++q;
                }
                while (*q >= '0' && *q <= '9') {
                    exponent = exponent * 10 + (*q - '0');
                    ++q;
                }
                exponent *= sign;
            }
            have_exponent = 1;
            break;
        } else if (*s >= '0' && *s <= '9') {
            if (lead && *s == '0') {
                if (seen_point) {
                    --decpt;
                }
                continue;
            }
            lead = 0;
            if (ndigits < sizeof(digits)) {
                digits[ndigits++] = *s;
            }
            if (!seen_point) {
                ++decpt;
            }
        }
    }
    xx_buf_free(&tmp);
    if (have_exponent) {
        /* d.ddde+X: decpt counted the one digit before the point. */
        decpt += exponent;
    }
    while (ndigits > 1 && digits[ndigits - 1] == '0') {
        --ndigits;
    }
    if (ndigits == 0) {
        digits[0] = '0';
        ndigits = 1;
        decpt = 1;
    }
    ok = !negative || yarv_putc(text, '-');
    if (!ok) {
        return 0;
    }
    if (decpt > 0) {
        if ((size_t)decpt < ndigits) {
            return yarv_put_bytes(text, (const uint8_t *)digits, (size_t)decpt) && yarv_putc(text, '.') &&
                   yarv_put_bytes(text, (const uint8_t *)digits + decpt, ndigits - (size_t)decpt);
        }
        if (decpt <= 15) {
            if (!yarv_put_bytes(text, (const uint8_t *)digits, ndigits)) {
                return 0;
            }
            for (i = ndigits; i < (size_t)decpt; ++i) {
                if (!yarv_putc(text, '0')) {
                    return 0;
                }
            }
            return yarv_put(text, ".0");
        }
    } else if (decpt > -4) {
        if (!yarv_put(text, "0.")) {
            return 0;
        }
        for (i = 0; i < (size_t)(-decpt); ++i) {
            if (!yarv_putc(text, '0')) {
                return 0;
            }
        }
        return yarv_put_bytes(text, (const uint8_t *)digits, ndigits);
    }
    if (!yarv_putc(text, digits[0]) || !yarv_putc(text, '.')) {
        return 0;
    }
    if (ndigits > 1) {
        if (!yarv_put_bytes(text, (const uint8_t *)digits + 1, ndigits - 1)) {
            return 0;
        }
    } else if (!yarv_putc(text, '0')) {
        return 0;
    }
    if (!yarv_putc(text, 'e') || !yarv_putc(text, decpt - 1 < 0 ? '-' : '+')) {
        return 0;
    }
    {
        int e = decpt - 1 < 0 ? 1 - decpt : decpt - 1;
        return yarv_putf(text, "%02llu", (unsigned long long)e);
    }
}

static int yarv_bignum(yarv_text *text, const xxbyte_yarv_object *object, int absolute)
{
    uint32_t words[YARV_MAX_BIGNUM_WORDS];
    char decimal[YARV_MAX_BIGNUM_WORDS * 10 + 2];
    size_t count = (size_t)object->length;
    size_t pos = sizeof(decimal);
    size_t i;

    if (count > YARV_MAX_BIGNUM_WORDS) {
        return yarv_put(text, "?");
    }
    for (i = 0; i < count; ++i) {
        uint64_t w;
        if (!yarv_read_word(text->image, (uint64_t)object->data_offset + 4u * i, 4, &w)) {
            return yarv_put(text, "?");
        }
        words[i] = (uint32_t)w;
    }
    while (count > 0 && words[count - 1] == 0) {
        --count;
    }
    if (count == 0) {
        decimal[--pos] = '0';
    }
    while (count > 0) {
        uint64_t rest = 0;
        unsigned k;
        for (i = count; i-- > 0;) {
            uint64_t cur = (rest << 32) | words[i];
            words[i] = (uint32_t)(cur / 1000000000u);
            rest = cur % 1000000000u;
        }
        while (count > 0 && words[count - 1] == 0) {
            --count;
        }
        for (k = 0; k < 9; ++k) {
            decimal[--pos] = (char)('0' + rest % 10u);
            rest /= 10u;
            if (count == 0 && rest == 0) {
                break;
            }
        }
    }
    if (object->flags != 0 && !absolute) {
        decimal[--pos] = '-';
    }
    return yarv_put_bytes(text, (const uint8_t *)decimal + pos, sizeof(decimal) - pos);
}

static int yarv_fixnum_value(uint64_t value, unsigned word, int64_t *result)
{
    if ((value & 1u) == 0) {
        return 0;
    }
    *result = xxbyte_sext(value, 8u * word) >> 1;
    return 1;
}

/* Special constants of the release and word size. */
static int yarv_special(yarv_text *text, uint64_t value, int absolute)
{
    const yarv_image *image = text->image;
    uint64_t nil;
    uint64_t truth;
    int64_t fixnum;

    if (image->word == 8) {
        nil = image->version >= YARV_MODE(3, 2) ? 0x04u : 0x08u;
        truth = 0x14u;
    } else {
        nil = image->version >= YARV_MODE(3, 2) ? 0x02u : 0x04u;
        truth = image->version >= YARV_MODE(3, 2) ? 0x06u : 0x02u;
    }
    if (yarv_fixnum_value(value, image->word, &fixnum)) {
        if (absolute && fixnum < 0) {
            fixnum = -fixnum;
        }
        return yarv_put_signed(text, fixnum);
    }
    if (value == 0) {
        return yarv_put(text, "false");
    }
    if (value == nil) {
        return yarv_put(text, "nil");
    }
    if (value == truth) {
        return yarv_put(text, "true");
    }
    return yarv_put(text, "?");
}

static const char *const yarv_class_names[] = {
    "Object", "Array", "StandardError", "NoMatchingPatternError", "TypeError", "NoMatchingPatternKeyError",
};

static int yarv_class(yarv_text *text, uint64_t index)
{
    size_t count = text->image->version < YARV_MODE(2, 7) ? 3u : sizeof(yarv_class_names) / sizeof(yarv_class_names[0]);

    if (index >= count) {
        return yarv_put(text, "?");
    }
    return yarv_put(text, yarv_class_names[index]);
}

static int yarv_dummy_encoding(const uint8_t *name, uint32_t length)
{
    static const char *const dummies[] = {
        "UTF-16", "UTF-32", "UTF-7", "CP65000", "ISO-2022-JP", "ISO2022-JP", "ISO-2022-JP-2", "ISO2022-JP2", "ISO-2022-JP-KDDI", "CP50220", "CP50221",
    };
    size_t i;

    for (i = 0; i < sizeof(dummies) / sizeof(dummies[0]); ++i) {
        if (yarv_name_is(name, length, dummies[i])) {
            return 1;
        }
    }
    return 0;
}

static int yarv_encoding_object(yarv_text *text, const xxbyte_yarv_object *object)
{
    const uint8_t *name = text->image->data + object->data_offset;
    uint32_t length = 0;

    if (object->value != 0) {
        return yarv_put(text, "?");
    }
    while ((uint64_t)length < object->length && name[length] != 0) {
        ++length;
    }
    if (!yarv_put(text, "#<Encoding:")) {
        return 0;
    }
    if (text->image->version >= YARV_MODE(3, 4) && yarv_name_is(name, length, "ASCII-8BIT")) {
        if (!yarv_put(text, "BINARY (ASCII-8BIT)")) {
            return 0;
        }
    } else if (!yarv_put_bytes(text, name, length)) {
        return 0;
    }
    if (yarv_dummy_encoding(name, length) && !yarv_put(text, " (dummy)")) {
        return 0;
    }
    return yarv_putc(text, '>');
}

/* rb_reg_desc. */
static int yarv_regexp(yarv_text *text, const xxbyte_yarv_object *object)
{
    xxbyte_yarv_object source;
    const uint8_t *s;
    uint32_t len;
    uint32_t encoding;
    yarv_enc enc;
    const uint8_t *p;
    const uint8_t *end;
    int need_escape = 0;

    if (!yarv_object(text->image, object->a, &source) || !yarv_string_bytes(text->image, &source, &s, &len, &encoding) || source.type != XXBYTE_YARV_T_STRING) {
        return yarv_put(text, "?");
    }
    yarv_encoding(text->image, encoding, &enc);
    if (!yarv_putc(text, '/')) {
        return 0;
    }
    p = s;
    end = s + len;
    if (yarv_enc_ascii_compatible(&enc)) {
        while (p < end) {
            unsigned n;
            uint32_t c;
            if (!yarv_enc_char(&enc, p, end, &n, &c)) {
                need_escape = 1;
                break;
            }
            if (c >= 0x80) {
                if (!enc.resenc) {
                    need_escape = 1;
                    break;
                }
            } else if (c == '/' || !yarv_ascii_print(c)) {
                need_escape = 1;
                break;
            }
            p += n;
        }
    } else {
        need_escape = 1;
    }
    if (!need_escape) {
        if (!yarv_put_bytes(text, s, len)) {
            return 0;
        }
    } else {
        int unicode = yarv_enc_unicode(&enc);
        p = s;
        while (p < end) {
            unsigned n = 1;
            uint32_t c;
            int valid = yarv_enc_char(&enc, p, end, &n, &c);
            int ascii = valid && c < 0x80 && (yarv_enc_ascii_compatible(&enc) || n == 1);
            if (ascii && c == '\\' && p + n < end) {
                unsigned m = n + yarv_mbclen(&enc, p + n, end);
                if (p + m > end) {
                    m = (unsigned)(end - p);
                }
                if (!yarv_put_bytes(text, p, m)) {
                    return 0;
                }
                p += m;
                continue;
            }
            if (!ascii) {
                if (!valid) {
                    if (!yarv_putf(text, "\\x%02llX", *p)) {
                        return 0;
                    }
                    p += 1;
                    continue;
                }
                if (!yarv_escaped_char(text, c, unicode)) {
                    return 0;
                }
            } else if (c == '/') {
                if (!yarv_putc(text, '\\') || !yarv_put_bytes(text, p, n)) {
                    return 0;
                }
            } else if (yarv_ascii_print(c) || (c >= 0x09 && c <= 0x0D) || c == ' ') {
                if (!yarv_put_bytes(text, p, n)) {
                    return 0;
                }
            } else if (!yarv_putf(text, "\\x%02llX", c)) {
                return 0;
            }
            p += n;
        }
    }
    if (!yarv_putc(text, '/')) {
        return 0;
    }
    if ((object->flags & 4u) != 0 && !yarv_putc(text, 'm')) {
        return 0;
    }
    if ((object->flags & 1u) != 0 && !yarv_putc(text, 'i')) {
        return 0;
    }
    if ((object->flags & 2u) != 0 && !yarv_putc(text, 'x')) {
        return 0;
    }
    if ((object->flags & 32u) != 0 && !yarv_putc(text, 'n')) {
        return 0;
    }
    return 1;
}

static int yarv_is_nil(const yarv_text *text, uint64_t index)
{
    xxbyte_yarv_object object;

    if (index == 0) {
        return 1;
    }
    if (!yarv_object(text->image, index, &object) || !object.special_const) {
        return 0;
    }
    if (text->image->word == 8) {
        return object.value == (text->image->version >= YARV_MODE(3, 2) ? 0x04u : 0x08u);
    }
    return object.value == (text->image->version >= YARV_MODE(3, 2) ? 0x02u : 0x04u);
}

/* Whether a number object is negative (f_signbit: -0.0 counts, NaN not). */
static int yarv_negative(const yarv_text *text, uint64_t index)
{
    xxbyte_yarv_object object;
    int64_t fixnum;

    if (!yarv_object(text->image, index, &object)) {
        return 0;
    }
    if (object.special_const) {
        return yarv_fixnum_value(object.value, text->image->word, &fixnum) && fixnum < 0;
    }
    switch (object.type) {
        case XXBYTE_YARV_T_FLOAT: {
            double value;
            memcpy(&value, &object.value, sizeof(value));
            return value == value && (object.value >> 63) != 0;
        }
        case XXBYTE_YARV_T_BIGNUM: return object.flags != 0;
        case XXBYTE_YARV_T_RATIONAL: return yarv_negative(text, object.a);
        default: return 0;
    }
}

static int yarv_inspect_value(yarv_text *text, uint64_t index, unsigned depth, int absolute);

static int yarv_inspect_hash(yarv_text *text, const xxbyte_yarv_object *object, unsigned depth)
{
    yarv_elements it;
    uint64_t key;
    uint64_t value;
    uint64_t i;
    int modern = text->image->version >= YARV_MODE(3, 4);

    if (!yarv_putc(text, '{')) {
        return 0;
    }
    yarv_elements_init(&it, text->image, object);
    for (i = 0; i < object->length; ++i) {
        xxbyte_yarv_object k;
        const uint8_t *bytes;
        uint32_t length;
        uint32_t encoding;
        int symbol;

        if (!yarv_elements_next(&it, &key) || !yarv_elements_next(&it, &value)) {
            return yarv_put(text, "?}");
        }
        if (i != 0 && !yarv_put(text, ", ")) {
            return 0;
        }
        symbol =
            yarv_object(text->image, key, &k) && !k.special_const && k.type == XXBYTE_YARV_T_SYMBOL && yarv_string_bytes(text->image, &k, &bytes, &length, &encoding);
        if (modern && symbol) {
            yarv_enc enc;
            int quote;
            yarv_encoding(text->image, encoding, &enc);
            quote = length == 0 || !yarv_symname_p(text, bytes, length, &enc) || bytes[0] == '@' || bytes[0] == '$' || bytes[0] == '!';
            if (!quote) {
                /* The last character must not end an operator; a
                 * multibyte last character never does. */
                const uint8_t *q = bytes;
                const uint8_t *last = bytes;
                while (q < bytes + length) {
                    last = q;
                    q += yarv_mbclen(&enc, q, bytes + length);
                }
                if (last == bytes + length - 1 && strchr("+-*/`%^&|]<=>~@", bytes[length - 1]) != NULL) {
                    quote = 1;
                }
            }
            if (!(quote ? yarv_inspect_string(text, bytes, length, &enc) : yarv_put_bytes(text, bytes, length)) || !yarv_put(text, ": ")) {
                return 0;
            }
        } else {
            if (!yarv_inspect_value(text, key, depth + 1, 0) || !yarv_put(text, modern ? " => " : "=>")) {
                return 0;
            }
        }
        if (!yarv_inspect_value(text, value, depth + 1, 0)) {
            return 0;
        }
    }
    return yarv_putc(text, '}');
}

static int yarv_inspect_value(yarv_text *text, uint64_t index, unsigned depth, int absolute)
{
    xxbyte_yarv_object object;
    const uint8_t *bytes;
    uint32_t length;
    uint32_t encoding;

    if (index == 0) {
        return yarv_put(text, "nil");
    }
    if (depth > YARV_MAX_DEPTH || ++text->nodes > YARV_MAX_NODES) {
        return yarv_put(text, "...");
    }
    if (!yarv_object(text->image, index, &object)) {
        return yarv_put(text, "?");
    }
    if (object.special_const) {
        return yarv_special(text, object.value, absolute);
    }
    switch (object.type) {
        case XXBYTE_YARV_T_CLASS: return yarv_class(text, object.value);
        case XXBYTE_YARV_T_FLOAT: return yarv_float(text, absolute ? object.value & 0x7FFFFFFFFFFFFFFFu : object.value);
        case XXBYTE_YARV_T_STRING: {
            yarv_enc enc;
            yarv_encoding(text->image, object.encoding, &enc);
            return yarv_inspect_string(text, text->image->data + object.data_offset, (uint32_t)object.length, &enc);
        }
        case XXBYTE_YARV_T_SYMBOL:
            if (!yarv_string_bytes(text->image, &object, &bytes, &length, &encoding)) {
                return yarv_put(text, "?");
            }
            return yarv_symbol_text(text, bytes, length, encoding);
        case XXBYTE_YARV_T_REGEXP: return yarv_regexp(text, &object);
        case XXBYTE_YARV_T_ARRAY: {
            yarv_elements it;
            uint64_t element;
            uint64_t i;
            if (!yarv_putc(text, '[')) {
                return 0;
            }
            yarv_elements_init(&it, text->image, &object);
            for (i = 0; i < object.length; ++i) {
                if (!yarv_elements_next(&it, &element)) {
                    return yarv_put(text, "?]");
                }
                if ((i != 0 && !yarv_put(text, ", ")) || !yarv_inspect_value(text, element, depth + 1, 0)) {
                    return 0;
                }
            }
            return yarv_putc(text, ']');
        }
        case XXBYTE_YARV_T_HASH: return yarv_inspect_hash(text, &object, depth);
        case XXBYTE_YARV_T_STRUCT: {
            int begin_nil = yarv_is_nil(text, object.a);
            int end_nil = yarv_is_nil(text, object.b);
            if (text->image->version < YARV_MODE(2, 7) || !begin_nil || end_nil) {
                if (!yarv_inspect_value(text, object.a, depth + 1, 0)) {
                    return 0;
                }
            }
            if (!yarv_put(text, object.flags != 0 ? "..." : "..")) {
                return 0;
            }
            if (text->image->version < YARV_MODE(2, 7) ? !end_nil : (begin_nil || !end_nil)) {
                return yarv_inspect_value(text, object.b, depth + 1, 0);
            }
            return 1;
        }
        case XXBYTE_YARV_T_BIGNUM: return yarv_bignum(text, &object, absolute);
        case XXBYTE_YARV_T_DATA: return yarv_encoding_object(text, &object);
        case XXBYTE_YARV_T_RATIONAL:
            return yarv_putc(text, '(') && yarv_inspect_value(text, object.a, depth + 1, absolute) && yarv_putc(text, '/') &&
                   yarv_inspect_value(text, object.b, depth + 1, 0) && yarv_putc(text, ')');
        case XXBYTE_YARV_T_COMPLEX: {
            size_t mark;
            char last;
            if (!yarv_putc(text, '(') || !yarv_inspect_value(text, object.a, depth + 1, 0) || !yarv_putc(text, yarv_negative(text, object.b) ? '-' : '+')) {
                return 0;
            }
            mark = text->out->size;
            if (!yarv_inspect_value(text, object.b, depth + 1, 1)) {
                return 0;
            }
            last = text->out->size > mark ? text->out->data[text->out->size - 1] : 0;
            if (!(last >= '0' && last <= '9') && !yarv_putc(text, '*')) {
                return 0;
            }
            return yarv_put(text, "i)");
        }
        default: return yarv_put(text, "?");
    }
}

int yarv_inspect(yarv_text *text, uint64_t index, unsigned depth)
{
    return yarv_inspect_value(text, index, depth, 0);
}

int yarv_literal(yarv_text *text, uint64_t index)
{
    text->nodes = 0;
    return yarv_inspect_value(text, index, 0, 0);
}

int yarv_inspect_symbol_name(yarv_text *text, uint64_t index)
{
    xxbyte_yarv_object object;
    const uint8_t *bytes;
    uint32_t length;
    uint32_t encoding;

    if (!yarv_object(text->image, index, &object) || !yarv_string_bytes(text->image, &object, &bytes, &length, &encoding)) {
        return yarv_put(text, "?");
    }
    return yarv_symbol_text(text, bytes, length, encoding);
}

int yarv_put_name(yarv_text *text, uint64_t index)
{
    xxbyte_yarv_object object;
    const uint8_t *bytes;
    uint32_t length;
    uint32_t encoding;

    if (!yarv_object(text->image, index, &object) || !yarv_string_bytes(text->image, &object, &bytes, &length, &encoding)) {
        return yarv_put(text, "?");
    }
    return yarv_put_bytes(text, bytes, length);
}

int yarv_local_name(yarv_text *text, uint64_t index)
{
    xxbyte_yarv_object object;
    const uint8_t *bytes;
    uint32_t length;
    uint32_t encoding;
    yarv_enc enc;
    int plain;

    if (index == 0 || !yarv_object(text->image, index, &object) || !yarv_string_bytes(text->image, &object, &bytes, &length, &encoding)) {
        return yarv_put(text, "?");
    }
    yarv_encoding(text->image, encoding, &enc);
    if (text->image->version >= YARV_MODE(3, 3)) {
        plain = memchr(bytes, 0, length) == NULL && yarv_symname_type(bytes, length, &enc, IDSET_INTERN) == ID_LOCAL;
    } else {
        plain = yarv_symname_p(text, bytes, length, &enc);
    }
    if (plain) {
        return yarv_put_bytes(text, bytes, length);
    }
    return yarv_inspect_string(text, bytes, length, &enc);
}

int yarv_constant_path(yarv_text *text, uint64_t index)
{
    xxbyte_yarv_object object;
    yarv_elements it;
    uint64_t element;
    uint64_t i;

    if (!yarv_object(text->image, index, &object) || object.special_const || object.type != XXBYTE_YARV_T_ARRAY) {
        return yarv_put(text, "?");
    }
    yarv_elements_init(&it, text->image, &object);
    for (i = 0; i < object.length; ++i) {
        if (!yarv_elements_next(&it, &element)) {
            return yarv_put(text, "?");
        }
        if (i != 0 && !yarv_put(text, "::")) {
            return 0;
        }
        if (element != 0 && !yarv_put_name(text, element)) {
            return 0;
        }
    }
    return 1;
}

size_t xxbyte_yarv_inspect_object(const uint8_t *data, size_t size, const xxbyte_yarv_ibf_header *header, uint64_t index, char *buffer, size_t buffer_size)
{
    yarv_image image;
    yarv_text text;
    xx_buf_t out;
    size_t length = 0;

    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if ((buffer == NULL && buffer_size != 0) || !yarv_image_init(&image, data, size, header) || index >= image.header.object_list_size) {
        return 0;
    }
    xx_buf_init(&out);
    yarv_text_init(&text, &image, &out, YARV_MAX_TEXT);
    yarv_inspect(&text, index, 0);
    if (xx_buf_ok(&out) && out.data != NULL) {
        length = out.size;
        if (buffer_size != 0) {
            size_t copy = length < buffer_size ? length : buffer_size - 1;
            memcpy(buffer, out.data, copy);
            buffer[copy] = '\0';
        }
    }
    xx_buf_free(&out);
    return length;
}

#else
typedef int xxbyte_yarv_text_disabled;
#endif
