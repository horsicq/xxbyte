/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_AUTOIT_H
#define XXBYTE_XXBYTE_AUTOIT_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_AUTOIT
#error "the AutoIt v3 compiled script (A3X) decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct xx_buf_s; /* xx_buf_t of xxfclib (xxbyte_xxfc.h) */

/*
 * AutoIt v3 compiled scripts.
 *
 * Aut2Exe stores a script as a ">>>AUTOIT SCRIPT<<<" entry of an "AU3!"
 * archive (below). Since AutoIt 3.2.6 the entry holds the script as a
 * token stream: a little-endian uint32 line count, then the tokens of every
 * line, each line ended by token 0x7F. AutoIt reads the stream back into
 * its parser; there is no lower-level bytecode. One decoded instruction is
 * one token, opcode is the token byte, and the instruction text is the
 * token class followed by the token as it reads in source:
 *
 *   30 02 00 00 00 4B 00 44 00 keyword If        (units XOR the count 2)
 *   31 06 00 00 00 4B 00 55 00 ... function MsgBox
 *   05 2A 00 00 00             int32 42
 *   10 00 00 00 00 01 00 00 00 int64 4294967296
 *   20 00 00 00 00 00 00 F8 3F double 1.5
 *   33 02 00 00 00 43 00 43 00 variable $AA
 *   36 02 00 00 00 6A 00 6B 00 string "hi"
 *   41                         =
 *   7F                         eol
 *
 * Tokens:
 *
 *   0x00 keyword_index  uint32 index into the keyword table (If, Then,
 *                       Func, ...; xxbyte_autoit_keyword_name)
 *   0x01 function_index uint32 index into the built-in function table
 *                       (xxbyte_autoit_function_name)
 *   0x05 int32          int32 literal
 *   0x10 int64          int64 literal
 *   0x20 double         IEEE-754 double literal, printed so that it reads
 *                       back as a double ("1.5", "100.0", "1e+16")
 *   0x30 keyword        keyword by name (canonical case when known)
 *   0x31 function       built-in function by name (canonical case when
 *                       known)
 *   0x32 macro          "@" name (canonical case when known)
 *   0x33 variable       "$" name
 *   0x34 identifier     user function name
 *   0x35 property       "." name (object member)
 *   0x36 string         quoted literal, "" for a quote; characters below
 *                       0x20 except tab, and 0x7F, print as " & Chr(n) & ",
 *                       unpaired surrogates as " & ChrW(n) & ", so the text
 *                       stays one line and reads back as the same value
 *   0x37 directive      kept verbatim (#NoTrayIcon, #pragma ...)
 *   0x40 ,   0x41 =   0x42 >   0x43 <   0x44 <>  0x45 >=  0x46 <=
 *   0x47 (   0x48 )   0x49 +   0x4A -   0x4B /   0x4C *   0x4D &
 *   0x4E [   0x4F ]   0x50 ==  0x51 ^   0x52 +=  0x53 -=  0x54 /=
 *   0x55 *=  0x56 &=  0x57 ?   0x58 :   (operators; the mnemonic is the
 *                                        operator, no operands)
 *   0x7F eol            end of a source line
 *
 * Tokens 0x30-0x37 carry a uint32 count n and n UTF-16LE units, each XORed
 * with the low 16 bits of n. Names print as UTF-8; an unpaired surrogate or
 * a control character in a name prints as U+FFFD. Every other token byte is
 * invalid. Aut2Exe 3.3.x writes keywords and built-in functions by name
 * (0x30, 0x31): in the 25 compiled scripts the decoder was checked on, the
 * index forms 0x00 and 0x01 never occur.
 *
 * Operands: keyword_index and function_index have one XXBYTE_OPERAND_INDEX
 * (size 4, kind XXBYTE_AUTOIT_INDEX_KEYWORD or XXBYTE_AUTOIT_INDEX_FUNCTION),
 * passed to the resolver; without resolver text it prints as the table
 * name, or as the decimal index when the index is past the table. int32 and
 * int64 have one signed IMMEDIATE (size 4, or 8 with FLAG_WIDE), double one
 * FLOAT (size 8). Tokens 0x30-0x37 have one XXBYTE_OPERAND_BYTES operand:
 * value 5 (the first unit's offset), size 2n, extra n. Literals print
 * signed, as AutoIt evaluates them: Aut2Exe stores 0x80000000 as the int32
 * -2147483648 and -1 as "+" and the int32 -1 (AutoIt-Ripper prints these
 * as 2147483648 and "+ 4294967295").
 *
 * Groups are statement hints only, the stream has no branch targets: the
 * keyword Return (either form) is XXBYTE_GROUP_RETURN, Exit
 * XXBYTE_GROUP_STOP, ExitLoop, ContinueLoop and ContinueCase
 * XXBYTE_GROUP_JUMP; function and function_index are XXBYTE_GROUP_CALL.
 *
 * Tables: the keyword (45), function (406) and macro (104) names are
 * AutoIt 3.3.16's, as AutoIt-Ripper 1.2.0 tabulates them. Keyword indexes
 * only grow (Null, Volatile and Enum were appended), but the built-in
 * function table is sorted by name, so the function_index names of other
 * releases can be off. Names are only used for 0x00/0x01 and to restore
 * the case of 0x30-0x32 names. There is one mode.
 *
 * Instruction model: a token is the smallest unit AutoIt itself reads, and
 * keeping one per instruction keeps every byte addressable; whole lines
 * come from xxbyte_autoit_source and from the container listing, which
 * prints each reconstructed line above its tokens. Names print as stored:
 * Aut2Exe stores names in upper case ($aList lists as $ALIST); keywords,
 * built-in functions and known macros get their canonical case back from
 * the tables.
 *
 * Archives of AutoIt 3.0 to 3.2.5 ("EA05") hold source text, not tokens,
 * so the token set has no EA05 variant.
 *
 * Ground truth: AutoIt-Ripper 1.2.0 (MIT) on 25 compiled AutoIt
 * executables (EA06, 3.3.x) and on inputs written by an independent
 * encoder. Limitations: Aut2Exe itself was not run; JB01 (AutoHotkey
 * Huffman) data is not decoded; the 16 key bytes after the archive version
 * only enter the EA05 data seed (their byte sum, as in AutoIt-Ripper); no
 * real EA05 archive was available.
 */
#define XXBYTE_AUTOIT_MODE_V3 0u
#define XXBYTE_AUTOIT_MODE_DEFAULT XXBYTE_AUTOIT_MODE_V3

/* Mnemonic identifiers (instruction id). */
enum {
    XXBYTE_AUTOIT_ID_KEYWORD_INDEX = 1,
    XXBYTE_AUTOIT_ID_FUNCTION_INDEX,
    XXBYTE_AUTOIT_ID_INT32,
    XXBYTE_AUTOIT_ID_INT64,
    XXBYTE_AUTOIT_ID_DOUBLE,
    XXBYTE_AUTOIT_ID_KEYWORD,
    XXBYTE_AUTOIT_ID_FUNCTION,
    XXBYTE_AUTOIT_ID_MACRO,
    XXBYTE_AUTOIT_ID_VARIABLE,
    XXBYTE_AUTOIT_ID_IDENTIFIER,
    XXBYTE_AUTOIT_ID_PROPERTY,
    XXBYTE_AUTOIT_ID_STRING,
    XXBYTE_AUTOIT_ID_DIRECTIVE,
    XXBYTE_AUTOIT_ID_COMMA,           /* 0x40 , */
    XXBYTE_AUTOIT_ID_ASSIGN,          /* 0x41 = */
    XXBYTE_AUTOIT_ID_GREATER,         /* 0x42 > */
    XXBYTE_AUTOIT_ID_LESS,            /* 0x43 < */
    XXBYTE_AUTOIT_ID_NOT_EQUAL,       /* 0x44 <> */
    XXBYTE_AUTOIT_ID_GREATER_EQUAL,   /* 0x45 >= */
    XXBYTE_AUTOIT_ID_LESS_EQUAL,      /* 0x46 <= */
    XXBYTE_AUTOIT_ID_LEFT_PAREN,      /* 0x47 ( */
    XXBYTE_AUTOIT_ID_RIGHT_PAREN,     /* 0x48 ) */
    XXBYTE_AUTOIT_ID_ADD,             /* 0x49 + */
    XXBYTE_AUTOIT_ID_SUBTRACT,        /* 0x4A - */
    XXBYTE_AUTOIT_ID_DIVIDE,          /* 0x4B / */
    XXBYTE_AUTOIT_ID_MULTIPLY,        /* 0x4C * */
    XXBYTE_AUTOIT_ID_CONCAT,          /* 0x4D & */
    XXBYTE_AUTOIT_ID_LEFT_BRACKET,    /* 0x4E [ */
    XXBYTE_AUTOIT_ID_RIGHT_BRACKET,   /* 0x4F ] */
    XXBYTE_AUTOIT_ID_EQUAL,           /* 0x50 == */
    XXBYTE_AUTOIT_ID_POWER,           /* 0x51 ^ */
    XXBYTE_AUTOIT_ID_ADD_ASSIGN,      /* 0x52 += */
    XXBYTE_AUTOIT_ID_SUBTRACT_ASSIGN, /* 0x53 -= */
    XXBYTE_AUTOIT_ID_DIVIDE_ASSIGN,   /* 0x54 /= */
    XXBYTE_AUTOIT_ID_MULTIPLY_ASSIGN, /* 0x55 *= */
    XXBYTE_AUTOIT_ID_CONCAT_ASSIGN,   /* 0x56 &= */
    XXBYTE_AUTOIT_ID_QUESTION,        /* 0x57 ? */
    XXBYTE_AUTOIT_ID_COLON,           /* 0x58 : */
    XXBYTE_AUTOIT_ID_EOL,             /* 0x7F */
    XXBYTE_AUTOIT_ID_COUNT = XXBYTE_AUTOIT_ID_EOL
};

#define XXBYTE_AUTOIT_TOKEN_EOL 0x7Fu

/* Index kinds of the keyword_index and function_index operands. */
#define XXBYTE_AUTOIT_INDEX_KEYWORD (XXBYTE_INDEX_FAMILY_FIRST + 0)
#define XXBYTE_AUTOIT_INDEX_FUNCTION (XXBYTE_INDEX_FAMILY_FIRST + 1)

#define XXBYTE_AUTOIT_KEYWORD_COUNT 45u
#define XXBYTE_AUTOIT_FUNCTION_COUNT 406u
#define XXBYTE_AUTOIT_MACRO_COUNT 104u

/* Table names, NULL past the end. Index 0 of the keyword table is the
 * unused "<Dummy>". Macro names have no "@". */
XXBYTE_API const char *xxbyte_autoit_keyword_name(uint32_t index);
XXBYTE_API const char *xxbyte_autoit_function_name(uint32_t index);
XXBYTE_API const char *xxbyte_autoit_macro_name(uint32_t index);
/* The mnemonic of an id, NULL for an unknown id. */
XXBYTE_API const char *xxbyte_autoit_mnemonic(uint32_t id);

/*
 * Source reconstruction.
 *
 * xxbyte_autoit_source reads a token stream (the uint32 line count, then
 * tokens) and appends that many lines of source to out, each ended by
 * "\r\n" as Aut2Exe's input had them. Tokens print as in the instruction
 * text after the mnemonic. By default tokens are separated by one space
 * except around "(", ")", "[", "]", ",", "." members and unary minus, and
 * lines are indented with tabs by the block keywords (the rules of
 * AutoIt-Ripper's formatter). XXBYTE_AUTOIT_SOURCE_SPACED separates every
 * token by one space (AutoIt-Ripper's layout), XXBYTE_AUTOIT_SOURCE_NO_INDENT
 * drops the indentation. *line_count (optional) receives the declared
 * count. Returns XXBYTE_STATUS_TRUNCATED when the stream ends before the
 * last line, XXBYTE_STATUS_INVALID_INSTRUCTION at an invalid token (out
 * then holds the lines before it), XXBYTE_STATUS_OUT_OF_MEMORY.
 */
#define XXBYTE_AUTOIT_SOURCE_DEFAULT 0x0000u
#define XXBYTE_AUTOIT_SOURCE_SPACED 0x0001u
#define XXBYTE_AUTOIT_SOURCE_NO_INDENT 0x0002u
XXBYTE_API xxbyte_status xxbyte_autoit_source(const uint8_t *tokens, size_t size, uint32_t flags, struct xx_buf_s *out, uint32_t *line_count);

/*
 * Ciphers. Entries of "EA05" archives (AutoIt 3.0 to 3.2.5) are encrypted
 * with a Mersenne Twister (MT19937, init_genrand(seed), one key byte per
 * 32-bit output: (tempered >> 1) & 0xFF); entries of "EA06" archives
 * (AutoIt 3.2.6 and later) with the "LAME" generator, a RanRot-style lagged
 * rotate-add generator of 17 words (seed s: s = 1 - s * 0x53A9B4FB per word,
 * 9 warm-up steps, then two steps per key byte, the byte being the top 8
 * bits of the second). xxbyte_autoit_crypt XORs data[0..size) with the key
 * stream for seed, in place; encryption and decryption are the same.
 */
#define XXBYTE_AUTOIT_CIPHER_MT 0u
#define XXBYTE_AUTOIT_CIPHER_LAME 1u
XXBYTE_API xxbyte_status xxbyte_autoit_crypt(uint32_t cipher, uint32_t seed, uint8_t *data, size_t size);

/* Adler-32 of data, the checksum an entry stores for its (decrypted,
 * still compressed) data. */
XXBYTE_API uint32_t xxbyte_autoit_adler32(const uint8_t *data, size_t size);

/*
 * Compression. A compressed entry starts with a 4-byte signature, a
 * big-endian uint32 unpacked size and an MSB-first bit stream of LZSS
 * items: a flag bit (1 = literal for "EA06", 0 = literal for "EA05" and
 * "JB00") and 8 literal bits, or a match. EA05 and EA06 matches are a
 * 15-bit distance and a length coded as 2 bits (+3), 3 bits (+6), 5 bits
 * (+13), 8 bits (+44), 8 bits (+299) and then 8 more bits for as long as the
 * previous field was all ones (each adding 255); JB00 matches are a 13-bit
 * distance (+3) and a 4-bit length (+3). A match copies byte by byte from
 * the distance back, so it may overlap itself. A match past the unpacked
 * size is cut at it. "JB01" (Huffman, AutoHotkey) is not supported.
 *
 * xxbyte_autoit_unpacked_size reads the signature and size: method
 * receives XXBYTE_AUTOIT_PACK_* and size the unpacked size (both
 * optional). xxbyte_autoit_decompress writes the unpacked data to
 * out[0..out_capacity) and sets *out_size. Statuses: TRUNCATED (header or
 * bits run out), INVALID_INSTRUCTION (unknown signature, a distance of 0
 * or before the start), UNSUPPORTED_FORMAT (JB01), OUT_OF_MEMORY (the size
 * exceeds out_capacity). Both never read or write out of bounds and run in
 * time linear in the input and output sizes.
 */
#define XXBYTE_AUTOIT_PACK_EA05 0u
#define XXBYTE_AUTOIT_PACK_EA06 1u
#define XXBYTE_AUTOIT_PACK_JB00 2u
#define XXBYTE_AUTOIT_PACK_JB01 3u
XXBYTE_API xxbyte_status xxbyte_autoit_unpacked_size(const uint8_t *in, size_t in_size, uint32_t *method, uint32_t *size);
XXBYTE_API xxbyte_status xxbyte_autoit_decompress(const uint8_t *in, size_t in_size, uint8_t *out, size_t out_capacity, size_t *out_size);

/*
 * Archives. An "AU3!" archive is the 16 bytes A3 48 4B BE 98 6C 4A A9 99 4C
 * 53 0A 86 D6 48 7D, "AU3!", the version "EA05" or "EA06", 16 bytes whose
 * byte sum keys EA05 entry data, and the entries. Aut2Exe puts the archive
 * in the RT_RCDATA resource "SCRIPT" (3.2.6 and later), at the start of the
 * overlay (older releases), or writes it as an .a3x file; it is found by
 * searching for the signature. Each entry is, with the EA06 / EA05
 * constants:
 *
 *   "FILE"          4 bytes, encrypted, seed 0x18EE / 0x16FA
 *   type length n   uint32 XOR 0xADBC / 0x29BC
 *   type            n UTF-16 units / n bytes, seed n + 0xB33F / n + 0xA25E
 *   name length m   uint32 XOR 0xF820 / 0x29AC
 *   name            m units / bytes, seed m + 0xF479 / m + 0xF25E
 *   compressed      1 byte (1 = compressed)
 *   packed size     uint32 XOR 0x87BC / 0x45AA
 *   size            uint32 XOR 0x87BC / 0x45AA
 *   adler32         uint32 XOR 0xA685 / 0xC3D2, of the decrypted data
 *   created         FILETIME, high uint32 first
 *   modified        FILETIME, high uint32 first
 *   data            packed size bytes, seed 0x2477 / checksum + 0x22AF
 *
 * The type is ">>>AUTOIT SCRIPT<<<" for the script (a token stream after
 * decompression in EA06 archives), ">AUTOIT SCRIPT<" and ">AUTOIT UNICODE
 * SCRIPT<" for source text (ANSI or UTF-16LE), ">>>AUTOIT NO
 * CMDEXECUTE<<<" for a marker; any other type is the source path of a
 * FileInstall file, whose name is the path the script gave. The entries end
 * where "FILE" does not decrypt. EA05 archive strings are byte strings.
 */
#define XXBYTE_AUTOIT_EA05 5u
#define XXBYTE_AUTOIT_EA06 6u

typedef struct xxbyte_autoit_archive {
    uint64_t offset;         /* of the 16-byte signature */
    uint64_t entries_offset; /* of the first entry (offset + 40) */
    uint32_t version;        /* XXBYTE_AUTOIT_EA05 or XXBYTE_AUTOIT_EA06 */
    uint32_t checksum;       /* byte sum of the 16 key bytes */
} xxbyte_autoit_archive;

/* Finds the first archive signature at or after start whose version is
 * EA05 or EA06 and that is followed by at least one entry. Returns
 * INVALID_INSTRUCTION when there is none. */
XXBYTE_API xxbyte_status xxbyte_autoit_find_archive(const uint8_t *data, size_t size, uint64_t start, xxbyte_autoit_archive *archive);

#define XXBYTE_AUTOIT_ENTRY_FILE 0u         /* a FileInstall file */
#define XXBYTE_AUTOIT_ENTRY_SCRIPT 1u       /* >>>AUTOIT SCRIPT<<< */
#define XXBYTE_AUTOIT_ENTRY_TEXT 2u         /* >AUTOIT SCRIPT< */
#define XXBYTE_AUTOIT_ENTRY_UNICODE_TEXT 3u /* >AUTOIT UNICODE SCRIPT< */
#define XXBYTE_AUTOIT_ENTRY_NO_EXECUTE 4u   /* >>>AUTOIT NO CMDEXECUTE<<< */

#define XXBYTE_AUTOIT_NAME_SIZE 1024u

typedef struct xxbyte_autoit_entry {
    uint64_t offset;      /* of the entry */
    uint64_t data_offset; /* of the encrypted data */
    uint64_t next;        /* of the next entry */
    uint32_t kind;        /* XXBYTE_AUTOIT_ENTRY_* */
    uint32_t compressed;  /* the compressed byte */
    uint32_t packed_size;
    uint32_t size;
    uint32_t adler32;     /* stored */
    uint32_t seed;        /* data seed */
    uint32_t cipher;      /* XXBYTE_AUTOIT_CIPHER_* */
    uint32_t type_length; /* in characters */
    uint32_t name_length;
    uint64_t created;  /* FILETIME */
    uint64_t modified; /* FILETIME */
    /* UTF-8 (EA06) or the bytes with \xNN for bytes outside printable
     * ASCII (EA05), NUL-terminated, cut to fit. */
    char type[XXBYTE_AUTOIT_NAME_SIZE];
    char name[XXBYTE_AUTOIT_NAME_SIZE];
} xxbyte_autoit_entry;

/* Parses the entry at offset of archive. Returns INVALID_INSTRUCTION when
 * "FILE" does not decrypt there or fewer than 4 bytes are left (the end of
 * the entries) and TRUNCATED when the entry, including its data, runs past
 * size. */
XXBYTE_API xxbyte_status xxbyte_autoit_read_entry(const uint8_t *data, size_t size, const xxbyte_autoit_archive *archive, uint64_t offset, xxbyte_autoit_entry *entry);

/* The largest unpacked entry xxbyte_autoit_entry_data and the listing
 * accept. */
#define XXBYTE_AUTOIT_DATA_LIMIT (64u * 1024u * 1024u)

/*
 * Decrypts and, when compressed, decompresses an entry's data into a new
 * allocation, released with xxbyte_autoit_free. *adler32_ok (optional) is
 * set to whether the stored checksum matched. A size above
 * XXBYTE_AUTOIT_DATA_LIMIT, or more than 256 output bytes per input byte,
 * is INVALID_INSTRUCTION.
 */
XXBYTE_API xxbyte_status xxbyte_autoit_entry_data(const uint8_t *data, size_t size, const xxbyte_autoit_entry *entry, uint8_t **out, size_t *out_size, int *adler32_ok);
XXBYTE_API void xxbyte_autoit_free(void *pointer);

/*
 * Containers (xxbyte_disasm_file). The mode is XXBYTE_MODE_AUTO or
 * XXBYTE_AUTOIT_MODE_V3; the archive version comes from the file. Read:
 *
 *  - any file holding an "AU3!" archive: a compiled .exe (the SCRIPT
 *    resource or the overlay) or an .a3x file;
 *  - a compressed entry ("EA05", "EA06" or "JB00" signature);
 *  - a raw token stream (line count and tokens).
 *
 * Archive listing: a header line for the archive, then one per entry:
 *
 *   ; AutoIt EA06 archive at 0x0002A400
 *   ; entry 0 at 0x0002A428: >>>AUTOIT SCRIPT<<< "C:\a\test.au3",
 *     compressed 312 -> 540 bytes, adler32 ok, created 2026-11-21 10:00:00,
 *     modified 2026-11-21 10:00:00                            (one line)
 *   ; script: 3 lines
 *   ; 1: Local $A = 1
 *   00000004  00 1C 00 00 00           keyword Local
 *   ...
 *   ; entry 1 at 0x0002A6A1: FileInstall "C:\in\a.txt" name "C:\in\a.txt",
 *     compressed 18 -> 17 bytes, adler32 ok, created ...         (one line)
 *
 * A token script is listed line by line: "; n: " and the reconstructed
 * line, then the line's tokens (xxbyte_disasm_buf_ex, addresses are
 * offsets in the unpacked stream). Tokens after the last line follow under
 * "; trailing tokens". From a line with an invalid or cut token on, the rest
 * of the stream is listed (undecodable bytes as db) after a "; n: partial
 * text (invalid token)" or "(truncated)" line. An empty line prints as
 * "; n:". Source text entries print "; n: text" lines, FileInstall and marker entries only their header; an
 * entry whose data fails to decrypt or unpack says why on its header line.
 * A compressed entry or raw stream given alone prints the same script part
 * without the archive lines. Returns INVALID_INSTRUCTION for anything else
 * and TRUNCATED when the first entry is cut.
 */

#ifdef __cplusplus
}
#endif

#endif
