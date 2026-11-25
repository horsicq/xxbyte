#ifndef XXBYTE_XXBYTE_NSIS_H
#define XXBYTE_XXBYTE_NSIS_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_NSIS
#error "the NSIS decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct xx_buf_s; /* xx_buf_t of xxfclib (xxbyte_xxfc.h) */

/*
 * NSIS installer script entries.
 *
 * makensis compiles an installer script into the entries block of the
 * installer header: one 28-byte entry per instruction, a little-endian
 * int32 "which" (the EW_* opcode of Source/exehead/fileform.h) followed by
 * six int32 parameters. One decoded instruction is one entry, so size is
 * always XXBYTE_NSIS_ENTRY_SIZE and opcode is the raw "which" value.
 *
 * The opcode numbering depends on the NSIS release and on how exehead was
 * configured, so the mode is a variant in the low byte plus flags:
 *
 *   XXBYTE_NSIS_V306  NSIS 3.06 and later (default): EW_LOADANDSETIMAGE,
 *                     EW_GETOSINFO
 *   XXBYTE_NSIS_V305  NSIS 3.05: EW_LOADANDSETIMAGE, no EW_GETOSINFO
 *   XXBYTE_NSIS_V300  NSIS 3.00 to 3.04: EW_SETBRANDINGIMAGE
 *   XXBYTE_NSIS_V226  NSIS 2.26 to 2.51
 *   XXBYTE_NSIS_V204  NSIS 2.04 to 2.25 (no $EXEPATH, $EXEFILE)
 *   XXBYTE_NSIS_V202  NSIS 2.02 to 2.03 (no $_OUTDIR either)
 *   XXBYTE_NSIS_V201  NSIS 2.01 (no reboot_called, errlvl exec flags)
 *   XXBYTE_NSIS_V200  NSIS 2.0 and its release candidates 2.0rc1 to
 *                     2.0rc4: as 2.01, but EW_DELREG has the root key in
 *                     parameter 0, and the header has 7 blocks (no
 *                     BGFONT: see xxbyte_nsis_parse_header_ex)
 *   XXBYTE_NSIS_PARK1 Jim Park's Unicode NSIS before 2.46.2
 *   XXBYTE_NSIS_PARK2 Unicode NSIS 2.46.2: EW_GETFONTVERSION
 *   XXBYTE_NSIS_PARK3 Unicode NSIS 2.46.3 and later: EW_GETFONTNAME
 *
 *   XXBYTE_NSIS_MODE_ANSI an ANSI installer. Without it the installer is a
 *                     Unicode one: NSIS 3 adds EW_FPUTWS and EW_FGETWS
 *                     after EW_LOCKWINDOW, Park's builds insert them before
 *                     EW_FSEEK. The 2.x releases are ANSI only, so their
 *                     variants require this flag.
 *   XXBYTE_NSIS_MODE_LOG an exehead built with NSIS_CONFIG_LOG (the
 *                     "logging" special builds, and Park's 2.46.3+ builds):
 *                     EW_LOG sits before EW_SECTIONSET.
 *
 * Any other variant or flag bit is XXBYTE_STATUS_UNSUPPORTED_MODE. Builds
 * that switch other NSIS_SUPPORT_* features off renumber the opcodes too
 * and are not supported. NSIS 1.x (24-byte entries with five parameters,
 * another opcode set) and the 2.0 alphas and betas are not supported
 * either: 2.0b4 numbers the opcodes as 2.0 does, but a dozen of them
 * (EW_GETFULLPATHNAME, EW_FOPEN, EW_SECTIONSET, ...) order their parameters
 * differently. The opcode numbering is the same for the x86 and amd64
 * targets. xxbyte_nsis_guess_mode suggests a mode for a parsed header (NSIS
 * 2 as XXBYTE_NSIS_V226).
 *
 * Addresses. Jump parameters hold an entry number + 1, so a BRANCH target
 * is entries_address + (parameter - 1) * 28, where entries_address is the
 * address of entry 0: the entries_address of an xxbyte_nsis_context passed
 * to xxbyte_decode_ex (or xxbyte_disasm_buf_ex), and 0 without one. So
 * without a context, pass entry index * 28 as the address of each entry.
 *
 * Operands are the parameters that the exehead of at least one release of
 * the variant reads (from exec.c of every release), in parameter order;
 * operand.extra is the parameter's index (0 to 5) and operand.size is 4.
 * Where the releases of a variant differ, the operand list is their union:
 * an operand can be a parameter that the older releases of the variant
 * ignore (makensis writes 0 there); the notes below name the release that
 * reads it. EW_CALL's parameter 1 is the one operand no exehead reads:
 * makensis writes 1 there for "Call :label". Parameter kinds:
 *
 *   s  string: a string-table offset (in characters for Unicode installers,
 *      in bytes for ANSI ones) as XXBYTE_INDEX_STRING, or for a negative
 *      parameter p the language string -(p + 1) as
 *      XXBYTE_NSIS_INDEX_LANGSTRING. Numbers the script computes, such as
 *      Sleep's delay or IntOp's inputs, are strings too.
 *   v  variable: XXBYTE_INDEX_VARIABLE, or a signed IMMEDIATE when the
 *      parameter is negative (-1 marks an absent optional output).
 *   j  jump: XXBYTE_OPERAND_BRANCH for a positive parameter, IMMEDIATE 0
 *      for 0 (no jump: continue with the next entry), and for a negative
 *      parameter p the variable -(p + 1), whose value holds the entry
 *      number + 1, as XXBYTE_NSIS_INDEX_JUMP_VARIABLE.
 *   i  signed integer: IMMEDIATE with XXBYTE_OPERAND_FLAG_SIGNED.
 *   x  flags, masks and other unsigned values: IMMEDIATE, zero-extended.
 *   d  offset into the data block: XXBYTE_NSIS_INDEX_DATA.
 *   c  byte offset into the ctlcolors block: XXBYTE_NSIS_INDEX_CTLCOLORS.
 *   f  exec_flags field index: XXBYTE_NSIS_INDEX_FLAG.
 *
 *   EW_RET               -
 *   EW_NOP               j
 *   EW_ABORT             s
 *   EW_QUIT              -
 *   EW_CALL              j i       (the i is 1 for "Call :label")
 *   EW_UPDATETEXT        s         (V226: s i i, SetDetailsPrint's mode
 *                        in 1 and "lastused" in 2, read by 2.26 to 2.30;
 *                        V200 to V204: s i; Park: s)
 *   EW_SLEEP             s
 *   EW_BRINGTOFRONT      -
 *   EW_CHDETAILSVIEW     i i
 *   EW_SETFILEATTRIBUTES s x
 *   EW_CREATEDIR         s i i     (V226: s i i, the i read by 2.51 only;
 *                        V200 to V204 and Park: s i)
 *   EW_IFFILEEXISTS      s j j
 *   EW_SETFLAG           f s i i   (the last i: 3.10; V300, V305, V226 and
 *                        Park: f s i, the i from 2.31; V200 to V204: f s)
 *   EW_IFFLAG            j j f x
 *   EW_GETFLAG           v f
 *   EW_RENAME            s s i s   (the s: 2.29; V200 to V204: s s i)
 *   EW_GETFULLPATHNAME   s v i
 *   EW_SEARCHPATH        v s
 *   EW_GETTEMPFILENAME   v s
 *   EW_EXTRACTFILE       x s d x x s
 *   EW_DELETEFILE        s x
 *   EW_MESSAGEBOX        x s i j i j
 *   EW_RMDIR             s x
 *   EW_STRLEN            v s
 *   EW_ASSIGNVAR         v s s s
 *   EW_STRCMP            s s j j i (the i: 2.13; V200 to V202: s s j j)
 *   EW_READENVSTR        v s i
 *   EW_INTCMP            s s j j j x
 *   EW_INTOP             v s s i
 *   EW_INTFMT            v s s i   (the i: 3.03; 2.x and Park: v s s)
 *   EW_PUSHPOP           s i i     (v i i when parameter 1 is set: Pop)
 *   EW_FINDWINDOW        v s s s s x
 *   EW_SENDMESSAGE       v s s s s x
 *   EW_ISWINDOW          s j j
 *   EW_GETDLGITEM        v s s
 *   EW_SETCTLCOLORS      s c
 *   EW_SETBRANDINGIMAGE  s i i
 *   EW_LOADANDSETIMAGE   v M N x   (3.05: M N x, the flags in parameter 2)
 *                        M is a string when the flags have 0x10000, N
 *                        when they have 0x100, else resource and control
 *                        ids (i)
 *   EW_CREATEFONT        v s s s x
 *   EW_SHOWWINDOW        s s i i
 *   EW_SHELLEXEC         s s s i x s  (the x: 3.02; V226 and Park:
 *                        s s s i - s, the last s from 2.29; V200 to V204:
 *                        s s s i)
 *   EW_EXECUTE           s v i
 *   EW_GETFILETIME       v v s
 *   EW_GETDLLVERSION     v v s i   (the i: 3.08; other variants: v v s)
 *   EW_REGISTERDLL       s s s i i (the last i: 2.03; V200, V201: s s s i)
 *   EW_CREATESHORTCUT    s s s s x s
 *   EW_COPYFILES         s s x s   (the s: 2.29; V200 to V204: s s x)
 *   EW_REBOOT            x
 *   EW_WRITEINI          s s s s i
 *   EW_READINISTR        v s s s
 *   EW_DELREG            - x s s x (V200: x s s x, the root key first)
 *   EW_WRITEREG          x s s W i i  (W: d when parameter 4 is 3,
 *                        REG_BINARY, else s)
 *   EW_READREGSTR        v x s s i
 *   EW_REGENUM           v x s s i
 *   EW_FCLOSE            v
 *   EW_FOPEN             v x x s
 *   EW_FPUTS             v s i
 *   EW_FGETS             v v s i
 *   EW_FPUTWS            v s i i   (Park: v s i)
 *   EW_FGETWS            v v s i
 *   EW_FSEEK             v v s i
 *   EW_FINDCLOSE         v
 *   EW_FINDNEXT          v v
 *   EW_FINDFIRST         v v s
 *   EW_WRITEUNINSTALLER  s d i s   (the s: 2.29; V200 to V204: s d i)
 *   EW_LOG               i L       (L: i, LogSet's state, when parameter
 *                        0 is nonzero, else LogText's string s)
 *   EW_SECTIONSET        s E i i s (E: the output variable v when
 *                        parameter 2 >= 0, a get, else the new value s;
 *                        the last i and s: 2.05; V200 to V202: s E i)
 *   EW_INSTTYPESET       s T i i   (T: s when parameter 2 is nonzero, a
 *                        set, else v)
 *   EW_GETOSINFO         - v s i s (the last s: 3.08)
 *   EW_LOCKWINDOW        i
 *   EW_GETFONTVERSION    v s       (Park's own opcodes: 7-Zip, the only
 *   EW_GETFONTNAME       v s        description of them, knows two
 *   EW_FINDPROC          v s        parameters; these roles follow the
 *                                   output-first convention of the rest)
 *
 * ("-" is a parameter the exehead does not read; it has no operand.)
 *
 * Groups: EW_RET is RETURN; EW_NOP with a target (Goto) is JUMP; EW_ABORT
 * is THROW (the error propagates out of every Call); all three and EW_QUIT
 * are STOP. EW_CALL and EW_REGISTERDLL (RegDLL, CallInstDLL and plug-in
 * calls) are CALL. EW_IFFILEEXISTS, EW_IFFLAG, EW_STRCMP, EW_INTCMP,
 * EW_ISWINDOW and EW_MESSAGEBOX are CONDITIONAL when one of their jumps is
 * nonzero; all but EW_MESSAGEBOX (which can return other buttons) are also
 * STOP when every one of their jumps is nonzero.
 *
 * Opcode 0 (EW_INVALID_OPCODE), the compiler-only EW_GETLABELADDR and
 * EW_GETFUNCTIONADDR slots, EW_RESERVEDOPCODE and opcodes past the last one
 * of the mode are XXBYTE_STATUS_INVALID_INSTRUCTION, as soon as the four
 * "which" bytes are present; any other entry shorter than 28 bytes is
 * XXBYTE_STATUS_TRUNCATED.
 *
 * Text: the mnemonic, then the operands separated by ", ":
 *   string      string@0x1F           language string  lang@41
 *   variable    $0 $R9 $INSTDIR $_3_  (user variables are numbered from 0
 *               as $_N_, after the variant's predefined variables)
 *   jump        0x1C0 (the absolute target), 0, or the variable ($R0)
 *   integer     -1                    flags            0x80000002
 *   data        data@0x4C2            ctlcolors        ctlcolors@0x18
 *   exec flag   exec_error, or flag@N past the variant's fields
 * for example (makensis 3.12; the script command in brackets)
 *   EW_CREATEDIR string@0x60, 1, 0              [SetOutPath $INSTDIR]
 *   EW_IFFILEEXISTS string@0xB0, 0x3F0, 0x40C   [IfFileExists ... a b]
 *   EW_NOP $0                                   [Goto $0]
 *   EW_SETFLAG exec_error, string@0x95, 0, 0    [ClearErrors]
 *   EW_ASSIGNVAR $_0_, string@0x117, string@0x0, string@0x0  [StrCpy]
 *   EW_EXTRACTFILE 0x5000090, string@0x6A, data@0x0, 0x8C304C00,
 *       0x1DD56F3, lang@36                      [File data.txt]
 * A resolver is called for every string, language string, variable, jump
 * variable, data, ctlcolors, exec flag and branch operand. The decoder
 * never reads the string table; xxbyte_nsis_resolver, with an
 * xxbyte_nsis_script as its context, prints strings decoded and branch
 * targets as labels:
 *   EW_CREATEDIR "$INSTDIR", 1, 0
 *   EW_IFFILEEXISTS "$INSTDIR\file.txt", label_3, label_5
 *   EW_CALL func_8, 0
 *   EW_MESSAGEBOX 0x0, "Found it in $(LSTR_2)", 0, 0, 0, 0
 * XXBYTE_FORMAT_UPPERCASE changes nothing: the mnemonics are upper case.
 *
 * Containers. xxbyte_disasm_file (list_file) reads
 *   - installer and uninstaller images (.exe): the firstheader is searched
 *     at every 512-byte boundary as exehead does, the header decompressed
 *     with xxbyte_nsis_decompress_header (stored and zlib headers, solid or
 *     not; bzip2 and LZMA ones print the archive summary and a "not
 *     listed" notice and return XXBYTE_STATUS_UNSUPPORTED_FORMAT), then
 *     listed as by xxbyte_nsis_list_header; for an installer whose first
 *     EW_WRITEUNINSTALLER data item is reachable (the data block follows
 *     the header: non-solid items, or the same zlib solid stream) the
 *     uninstaller it writes is listed after it, under a "; uninstaller"
 *     line, with un.* callback names;
 *   - a raw decompressed header block (no firstheader found): for headers
 *     that xxbyte cannot decompress, decompress them yourself and pass
 *     them to xxbyte_disasm_file or xxbyte_nsis_list_header.
 * XXBYTE_MODE_AUTO takes the mode from xxbyte_nsis_guess_mode. Limits:
 * functions have no names in the header (only callbacks do), so other
 * functions are func_N and user variables $_N_; GetFunctionAddress and
 * GetLabelAddress targets (numbers in strings) are not labels; an NSIS 2.0
 * header (7 blocks) is read only with an explicit XXBYTE_NSIS_V200 mode;
 * only the main language table's strings are printed (1033, else the
 * first); the plug-in DLLs and files of the data block are not listed.
 */
#define XXBYTE_NSIS_V306 0u
#define XXBYTE_NSIS_V305 1u
#define XXBYTE_NSIS_V300 2u
#define XXBYTE_NSIS_V226 3u
#define XXBYTE_NSIS_V204 4u
#define XXBYTE_NSIS_V202 5u
#define XXBYTE_NSIS_V200 6u
#define XXBYTE_NSIS_PARK1 7u
#define XXBYTE_NSIS_PARK2 8u
#define XXBYTE_NSIS_PARK3 9u
#define XXBYTE_NSIS_V201 10u
#define XXBYTE_NSIS_VARIANT_COUNT 11u

#define XXBYTE_NSIS_MODE_ANSI 0x100u
#define XXBYTE_NSIS_MODE_LOG 0x200u

#define XXBYTE_NSIS_MODE(variant, flags) (((uint32_t)(variant) & 0xFFu) | ((uint32_t)(flags) & 0x300u))
#define XXBYTE_NSIS_MODE_VARIANT(mode) ((uint32_t)(mode) & 0xFFu)
#define XXBYTE_NSIS_MODE_DEFAULT XXBYTE_NSIS_MODE(XXBYTE_NSIS_V306, 0)

#define XXBYTE_NSIS_ENTRY_SIZE 28u

/* Family-specific index kinds (XXBYTE_OPERAND_INDEX operand.kind). */
#define XXBYTE_NSIS_INDEX_LANGSTRING (XXBYTE_INDEX_FAMILY_FIRST + 0)
#define XXBYTE_NSIS_INDEX_JUMP_VARIABLE (XXBYTE_INDEX_FAMILY_FIRST + 1)
#define XXBYTE_NSIS_INDEX_DATA (XXBYTE_INDEX_FAMILY_FIRST + 2)
#define XXBYTE_NSIS_INDEX_CTLCOLORS (XXBYTE_INDEX_FAMILY_FIRST + 3)
#define XXBYTE_NSIS_INDEX_FLAG (XXBYTE_INDEX_FAMILY_FIRST + 4)

/* The decode context for xxbyte_decode_ex and xxbyte_disasm_buf_ex. */
typedef struct xxbyte_nsis_context {
    uint64_t entries_address; /* address of entry 0 */
} xxbyte_nsis_context;

/*
 * Mnemonic identifiers (xxbyte_instruction.id): one per EW_* name of all
 * variants, so an opcode keeps its id across modes whatever its number.
 * The enum name is the mnemonic with every run of non-alphanumeric
 * characters replaced by '_'. Valid ids are 1 to XXBYTE_NSIS_ID_COUNT - 1.
 */
typedef enum xxbyte_nsis_id {
    XXBYTE_NSIS_ID_INVALID = 0,
    XXBYTE_NSIS_ID_EW_RET = 1,
    XXBYTE_NSIS_ID_EW_NOP = 2,
    XXBYTE_NSIS_ID_EW_ABORT = 3,
    XXBYTE_NSIS_ID_EW_QUIT = 4,
    XXBYTE_NSIS_ID_EW_CALL = 5,
    XXBYTE_NSIS_ID_EW_UPDATETEXT = 6,
    XXBYTE_NSIS_ID_EW_SLEEP = 7,
    XXBYTE_NSIS_ID_EW_BRINGTOFRONT = 8,
    XXBYTE_NSIS_ID_EW_CHDETAILSVIEW = 9,
    XXBYTE_NSIS_ID_EW_SETFILEATTRIBUTES = 10,
    XXBYTE_NSIS_ID_EW_CREATEDIR = 11,
    XXBYTE_NSIS_ID_EW_IFFILEEXISTS = 12,
    XXBYTE_NSIS_ID_EW_SETFLAG = 13,
    XXBYTE_NSIS_ID_EW_IFFLAG = 14,
    XXBYTE_NSIS_ID_EW_GETFLAG = 15,
    XXBYTE_NSIS_ID_EW_RENAME = 16,
    XXBYTE_NSIS_ID_EW_GETFULLPATHNAME = 17,
    XXBYTE_NSIS_ID_EW_SEARCHPATH = 18,
    XXBYTE_NSIS_ID_EW_GETTEMPFILENAME = 19,
    XXBYTE_NSIS_ID_EW_EXTRACTFILE = 20,
    XXBYTE_NSIS_ID_EW_DELETEFILE = 21,
    XXBYTE_NSIS_ID_EW_MESSAGEBOX = 22,
    XXBYTE_NSIS_ID_EW_RMDIR = 23,
    XXBYTE_NSIS_ID_EW_STRLEN = 24,
    XXBYTE_NSIS_ID_EW_ASSIGNVAR = 25,
    XXBYTE_NSIS_ID_EW_STRCMP = 26,
    XXBYTE_NSIS_ID_EW_READENVSTR = 27,
    XXBYTE_NSIS_ID_EW_INTCMP = 28,
    XXBYTE_NSIS_ID_EW_INTOP = 29,
    XXBYTE_NSIS_ID_EW_INTFMT = 30,
    XXBYTE_NSIS_ID_EW_PUSHPOP = 31,
    XXBYTE_NSIS_ID_EW_FINDWINDOW = 32,
    XXBYTE_NSIS_ID_EW_SENDMESSAGE = 33,
    XXBYTE_NSIS_ID_EW_ISWINDOW = 34,
    XXBYTE_NSIS_ID_EW_GETDLGITEM = 35,
    XXBYTE_NSIS_ID_EW_SETCTLCOLORS = 36,
    XXBYTE_NSIS_ID_EW_LOADANDSETIMAGE = 37,
    XXBYTE_NSIS_ID_EW_CREATEFONT = 38,
    XXBYTE_NSIS_ID_EW_SHOWWINDOW = 39,
    XXBYTE_NSIS_ID_EW_SHELLEXEC = 40,
    XXBYTE_NSIS_ID_EW_EXECUTE = 41,
    XXBYTE_NSIS_ID_EW_GETFILETIME = 42,
    XXBYTE_NSIS_ID_EW_GETDLLVERSION = 43,
    XXBYTE_NSIS_ID_EW_REGISTERDLL = 44,
    XXBYTE_NSIS_ID_EW_CREATESHORTCUT = 45,
    XXBYTE_NSIS_ID_EW_COPYFILES = 46,
    XXBYTE_NSIS_ID_EW_REBOOT = 47,
    XXBYTE_NSIS_ID_EW_WRITEINI = 48,
    XXBYTE_NSIS_ID_EW_READINISTR = 49,
    XXBYTE_NSIS_ID_EW_DELREG = 50,
    XXBYTE_NSIS_ID_EW_WRITEREG = 51,
    XXBYTE_NSIS_ID_EW_READREGSTR = 52,
    XXBYTE_NSIS_ID_EW_REGENUM = 53,
    XXBYTE_NSIS_ID_EW_FCLOSE = 54,
    XXBYTE_NSIS_ID_EW_FOPEN = 55,
    XXBYTE_NSIS_ID_EW_FPUTS = 56,
    XXBYTE_NSIS_ID_EW_FGETS = 57,
    XXBYTE_NSIS_ID_EW_FSEEK = 58,
    XXBYTE_NSIS_ID_EW_FINDCLOSE = 59,
    XXBYTE_NSIS_ID_EW_FINDNEXT = 60,
    XXBYTE_NSIS_ID_EW_FINDFIRST = 61,
    XXBYTE_NSIS_ID_EW_WRITEUNINSTALLER = 62,
    XXBYTE_NSIS_ID_EW_LOG = 63,
    XXBYTE_NSIS_ID_EW_SECTIONSET = 64,
    XXBYTE_NSIS_ID_EW_INSTTYPESET = 65,
    XXBYTE_NSIS_ID_EW_GETOSINFO = 66,
    XXBYTE_NSIS_ID_EW_LOCKWINDOW = 67,
    XXBYTE_NSIS_ID_EW_FPUTWS = 68,
    XXBYTE_NSIS_ID_EW_FGETWS = 69,
    XXBYTE_NSIS_ID_EW_SETBRANDINGIMAGE = 70,
    XXBYTE_NSIS_ID_EW_GETFONTVERSION = 71,
    XXBYTE_NSIS_ID_EW_GETFONTNAME = 72,
    XXBYTE_NSIS_ID_EW_FINDPROC = 73,
    XXBYTE_NSIS_ID_COUNT = 74
} xxbyte_nsis_id;

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_nsis_mnemonic(uint32_t id);

/* The id that opcode decodes to in mode; 0 when it is invalid there or
 * mode is unsupported. */
XXBYTE_API uint32_t xxbyte_nsis_opcode_id(uint32_t mode, uint32_t opcode);

/* The opcode of id in mode; 0 when mode has no such opcode. */
XXBYTE_API uint32_t xxbyte_nsis_id_opcode(uint32_t mode, uint32_t id);

/*
 * Writes the name of variable index in mode ("$0", "$R5", "$INSTDIR",
 * "$_0_" for the first user variable) to buffer, NUL-terminated and
 * truncated to buffer_size - 1 characters. Returns the full length, or 0
 * when mode is unsupported (buffer is then empty when buffer_size != 0).
 */
XXBYTE_API size_t xxbyte_nsis_variable_name(uint32_t mode, uint32_t index, char *buffer, size_t buffer_size);

/* The number of predefined variables in mode ($0 to $R9 and the named
 * ones): user variables start at this index. 0 for an unsupported mode. */
XXBYTE_API uint32_t xxbyte_nsis_first_user_variable(uint32_t mode);

/*
 * The exec_flags field name of index in mode ("exec_error"), or NULL past
 * the variant's fields or for an unsupported mode. V226 uses the names of
 * 2.42 and later: field 7 is plugin_api_version (insttype_changed, unused,
 * in 2.26 to 2.41) and field 13, status_update, exists from 2.31 on.
 */
XXBYTE_API const char *xxbyte_nsis_flag_name(uint32_t mode, uint32_t index);

/*
 * The script command that produced a decoded NSIS entry, as far as the
 * opcode and its integer parameters tell: "SetOutPath" or "CreateDirectory"
 * for EW_CREATEDIR, "Goto" or "Nop" for EW_NOP, "WriteRegExpandStr" for an
 * EW_WRITEREG whose value type is REG_EXPAND_SZ, "SectionGetFlags" for an
 * EW_SECTIONSET reading field 2. Where only string contents tell commands
 * apart both are named: "ClearErrors/SetErrors", "RegDLL/UnRegDLL" and
 * "ReadMemory/GetWinVer" (GetWinVer reads the exehead's OS-info block
 * through EW_GETOSINFO's memory read, address string "0"); a
 * plug-in call is "CallInstDLL" and InitPluginsDir is "Call". Helper
 * entries that makensis emits for other commands keep their own name
 * (GetFunctionAddress and GetDLLVersionLocal are "StrCpy"). Returns NULL
 * when no command produces the entry (an EW_SETFLAG of an exec flag no
 * command sets, for example) and for instructions of other families.
 */
XXBYTE_API const char *xxbyte_nsis_command_name(const xxbyte_instruction *instruction);

/* firstheader flags. */
#define XXBYTE_NSIS_FH_UNINSTALL 0x1u
#define XXBYTE_NSIS_FH_SILENT 0x2u
#define XXBYTE_NSIS_FH_NO_CRC 0x4u
#define XXBYTE_NSIS_FH_FORCE_CRC 0x8u

#define XXBYTE_NSIS_FIRST_HEADER_SIZE 28u

/* The firstheader that follows the exehead stub (fileform.h). */
typedef struct xxbyte_nsis_first_header {
    uint32_t flags;        /* XXBYTE_NSIS_FH_* */
    uint32_t header_size;  /* length_of_header: the decompressed header */
    uint32_t archive_size; /* length_of_all_following_data: from the first
                              header to the end, CRC included */
} xxbyte_nsis_first_header;

/*
 * Parses the firstheader at data[0]: flags (only bits 0 to 3 may be set),
 * the 0xDEADBEEF signature and "NullsoftInst". Returns
 * XXBYTE_STATUS_TRUNCATED for fewer than 28 bytes and
 * XXBYTE_STATUS_INVALID_INSTRUCTION when it is not a firstheader; on
 * failure *header is zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_parse_first_header(const uint8_t *data, size_t size, xxbyte_nsis_first_header *header);

/*
 * Finds the firstheader in an installer image as exehead does: at the
 * first offset that is a multiple of 512 and holds a valid firstheader.
 * Returns XXBYTE_STATUS_INVALID_INSTRUCTION, with *offset 0, when there is
 * none.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_find_first_header(const uint8_t *data, size_t size, uint64_t *offset);

/* How the header follows the firstheader. */
#define XXBYTE_NSIS_STORED 0u     /* uncompressed: header_size bytes */
#define XXBYTE_NSIS_COMPRESSED 1u /* one compressed block of data_size bytes */
#define XXBYTE_NSIS_SOLID 2u      /* the solid stream of the whole archive */

/* Compression methods, from the stream's first bytes. */
#define XXBYTE_NSIS_METHOD_COPY 0u
#define XXBYTE_NSIS_METHOD_DEFLATE 1u /* raw deflate, no zlib header */
#define XXBYTE_NSIS_METHOD_BZIP2 2u   /* NSIS bzip2 without the "BZh" header */
#define XXBYTE_NSIS_METHOD_LZMA 3u    /* 5-byte LZMA properties, no size */

typedef struct xxbyte_nsis_header_location {
    uint32_t storage;     /* XXBYTE_NSIS_STORED, _COMPRESSED or _SOLID */
    uint32_t method;      /* XXBYTE_NSIS_METHOD_* */
    uint32_t filter;      /* LZMA only: 1 when a filter flag byte (0 or 1,
                             1 = x86 BCJ) precedes the properties */
    uint64_t data_offset; /* from the firstheader: where the stored header,
                             the compressed block or the solid stream
                             (with its filter byte) starts */
    uint32_t data_size;   /* stored: header_size; compressed: the block's
                             size; solid: 0 (unknown) */
} xxbyte_nsis_header_location;

/*
 * Tells how the header is stored after the firstheader at data[0] (data
 * covers the firstheader and what follows it), with the heuristics
 * 7-Zip uses: a stored header is preceded by its size, a non-solid
 * compressed one by its size with bit 31 set; anything else is a solid
 * stream, whose decompressed output is the header's size (4 bytes) and
 * then the header. xxbyte_nsis_decompress_header decompresses stored and
 * zlib headers. Returns XXBYTE_STATUS_TRUNCATED when the bytes to inspect
 * or a stored or compressed header run past size, and
 * XXBYTE_STATUS_INVALID_INSTRUCTION for an invalid firstheader.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_locate_header(const uint8_t *data, size_t size, xxbyte_nsis_header_location *location);

/* Header blocks, in their order in the header. */
#define XXBYTE_NSIS_BLOCK_PAGES 0u
#define XXBYTE_NSIS_BLOCK_SECTIONS 1u
#define XXBYTE_NSIS_BLOCK_ENTRIES 2u
#define XXBYTE_NSIS_BLOCK_STRINGS 3u
#define XXBYTE_NSIS_BLOCK_LANGTABLES 4u
#define XXBYTE_NSIS_BLOCK_CTLCOLORS 5u
#define XXBYTE_NSIS_BLOCK_BGFONT 6u
#define XXBYTE_NSIS_BLOCK_DATA 7u
#define XXBYTE_NSIS_BLOCK_COUNT 8u

typedef struct xxbyte_nsis_block {
    uint64_t offset; /* from the header's first byte; for BGFONT 0 when
                        there is no background font; DATA is filled in at
                        run time and usually 0 */
    uint32_t count;  /* items (bytes for STRINGS: see strings_size) */
} xxbyte_nsis_block;

/* A parsed (decompressed) header. */
typedef struct xxbyte_nsis_header {
    uint32_t flags;        /* common_header.flags (CH_FLAGS_*) */
    uint32_t pointer_size; /* 4, or 8 in amd64 installers, whose block
                              offsets are 64-bit */
    xxbyte_nsis_block blocks[XXBYTE_NSIS_BLOCK_COUNT];
    uint32_t entry_count;    /* blocks[ENTRIES].count */
    uint32_t entries_offset; /* blocks[ENTRIES].offset */
    uint32_t strings_offset; /* blocks[STRINGS].offset */
    uint32_t strings_size;   /* bytes, up to the language tables */
    uint32_t unicode;        /* 1 when strings are UTF-16LE */
} xxbyte_nsis_header;

/*
 * Parses the decompressed header at data[0] (size bytes, normally the
 * firstheader's header_size) with the 8-block table of NSIS 2.01 and later.
 * Tries 32-bit block offsets, then 64-bit ones, and accepts a layout when
 * the blocks from PAGES to CTLCOLORS lie in order inside size, the entries
 * fit before the strings and the string table ends with a NUL character.
 * Unicode is detected as 7-Zip does: the table starts with an empty UTF-16
 * string. Returns XXBYTE_STATUS_TRUNCATED when size cannot hold the block
 * table, XXBYTE_STATUS_INVALID_INSTRUCTION when no layout fits; *header is
 * zeroed on failure. The header of NSIS 2.0 (XXBYTE_NSIS_V200) has no
 * BGFONT block: this function may accept it, with its DATA block in
 * blocks[BGFONT] and the fields that follow the table in blocks[DATA];
 * parse such a header with xxbyte_nsis_parse_header_ex.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_parse_header(const uint8_t *data, size_t size, xxbyte_nsis_header *header);

/* xxbyte_nsis_parse_header_ex options. */
#define XXBYTE_NSIS_HEADER_7_BLOCKS 0x1u /* NSIS 2.0: no BGFONT record */

/*
 * xxbyte_nsis_parse_header with options: XXBYTE_NSIS_HEADER_7_BLOCKS reads
 * the 7-record block table of NSIS 2.0 and its release candidates (PAGES to
 * CTLCOLORS, then DATA), leaving blocks[BGFONT] zero. Other option bits are
 * XXBYTE_STATUS_INVALID_ARGUMENT.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_parse_header_ex(const uint8_t *data, size_t size, uint32_t options, xxbyte_nsis_header *header);

/*
 * Locates the string at string-table offset in a header parsed into
 * *header from the same data: *string_offset receives its byte offset from
 * data[0] and *length its length in characters (bytes for ANSI, UTF-16
 * units for Unicode) up to the terminating NUL. The text is raw: NSIS
 * stores variables, language strings and shell folders as escape codes
 * (NSIS 3 and Unicode: 1 to 4, NSIS 2 ANSI: 252 to 255, Park: 0xE000 to
 * 0xE003, each followed by a two-character number). Returns
 * XXBYTE_STATUS_INVALID_ARGUMENT for an offset past the table and
 * XXBYTE_STATUS_TRUNCATED when the string is not terminated inside it.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_string_at(const uint8_t *data, size_t size, const xxbyte_nsis_header *header, uint32_t offset, uint64_t *string_offset,
                                               uint32_t *length);

/*
 * Suggests a decode mode for a header parsed into *header from data: the
 * string escape codes tell NSIS 3 (and its Unicode or ANSI build), NSIS 2
 * ANSI and Park's Unicode NSIS apart; an EW_GETOSINFO entry or an
 * EW_LOADANDSETIMAGE-style entry 37 picks between the NSIS 3 variants, and
 * entries whose parameters do not fit their opcode under one numbering but
 * do under another select the log variant and Park's 2.46.2 and 2.46.3
 * layouts. NSIS 2 is reported as XXBYTE_NSIS_V226 and NSIS 3 without other
 * evidence as XXBYTE_NSIS_V306. This is a heuristic; check the result
 * against what else is known about the installer.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_guess_mode(const uint8_t *data, size_t size, const xxbyte_nsis_header *header, uint32_t *mode);

/*
 * Decompresses the header that follows the firstheader at data[0] (data
 * covers the firstheader and what follows it, as for
 * xxbyte_nsis_locate_header) into out[0..out_capacity) and sets *out_size
 * to its length, the firstheader's header_size. xxbyte decompresses what it
 * can itself:
 *   - stored headers (SetCompress off, or a block makensis left
 *     uncompressed because compression did not shrink it);
 *   - zlib, both non-solid (one raw-deflate block: makensis's "zlib"
 *     compressor writes RFC 1951 streams without the zlib header) and solid
 *     (the archive's single raw-deflate stream, whose output starts with
 *     the header's size and then the header).
 * bzip2 and LZMA headers return XXBYTE_STATUS_UNSUPPORTED_FORMAT: xxbyte has
 * no decoder for them. Decompress such a header yourself, as *location
 * tells (a non-solid block of data_size bytes at data_offset; a solid
 * stream from data_offset, after the filter byte when location->filter is
 * 1, whose output is the 4-byte size and then the header), and pass the
 * result to xxbyte_nsis_list_header or xxbyte_nsis_script_init.
 * location may be NULL; when it is not it receives what
 * xxbyte_nsis_locate_header reports, also on failure past that step.
 * Returns XXBYTE_STATUS_OUT_OF_MEMORY, with *out_size set to the size
 * needed, when out_capacity is smaller than header_size (out may be NULL
 * then) or a solid stream's work buffer cannot be allocated,
 * XXBYTE_STATUS_TRUNCATED when the compressed data ends early,
 * XXBYTE_STATUS_INVALID_INSTRUCTION for an invalid firstheader or corrupt
 * data (a deflate error, a block that decompresses to another size, a solid
 * stream whose size prefix differs from header_size), and the statuses of
 * xxbyte_nsis_locate_header.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_decompress_header(const uint8_t *data, size_t size, uint8_t *out, size_t out_capacity, size_t *out_size,
                                                       xxbyte_nsis_header_location *location);

/* Section flags (section.flags, SF_*). */
#define XXBYTE_NSIS_SF_SELECTED 0x1u
#define XXBYTE_NSIS_SF_SECGRP 0x2u    /* SectionGroup record (no code) */
#define XXBYTE_NSIS_SF_SECGRPEND 0x4u /* SectionGroupEnd record */
#define XXBYTE_NSIS_SF_BOLD 0x8u
#define XXBYTE_NSIS_SF_RO 0x10u
#define XXBYTE_NSIS_SF_EXPAND 0x20u

/* A section record (the start of fileform.h's section). */
typedef struct xxbyte_nsis_section {
    int32_t name;           /* string parameter: 0 ("") for a hidden section */
    uint32_t install_types; /* InstType bits */
    uint32_t flags;         /* XXBYTE_NSIS_SF_* */
    int32_t code;           /* first entry */
    int32_t code_size;      /* entries */
    int32_t size_kb;
} xxbyte_nsis_section;

/* Page window procedures (page.wndproc_id). */
#define XXBYTE_NSIS_PAGE_LICENSE 0u
#define XXBYTE_NSIS_PAGE_COMPONENTS 1u
#define XXBYTE_NSIS_PAGE_DIRECTORY 2u
#define XXBYTE_NSIS_PAGE_INSTFILES 3u
#define XXBYTE_NSIS_PAGE_UNINSTCONFIRM 4u
#define XXBYTE_NSIS_PAGE_COMPLETED 5u
#define XXBYTE_NSIS_PAGE_CUSTOM 6u

/* A page record. The functions are entry numbers, -1 for none. */
typedef struct xxbyte_nsis_page {
    int32_t dialog_id;
    int32_t wndproc_id; /* XXBYTE_NSIS_PAGE_* */
    int32_t pre_function;
    int32_t show_function;
    int32_t leave_function;
    uint32_t flags;  /* PF_* */
    int32_t caption; /* string parameters */
    int32_t back;
    int32_t next;
    int32_t click_next;
    int32_t cancel;
    int32_t params[5];
} xxbyte_nsis_page;

/* Callback functions of the header (header.code_on*), in their order. */
#define XXBYTE_NSIS_CB_ONINIT 0u
#define XXBYTE_NSIS_CB_ONINSTSUCCESS 1u
#define XXBYTE_NSIS_CB_ONINSTFAILED 2u
#define XXBYTE_NSIS_CB_ONUSERABORT 3u
#define XXBYTE_NSIS_CB_ONGUIINIT 4u
#define XXBYTE_NSIS_CB_ONGUIEND 5u
#define XXBYTE_NSIS_CB_ONMOUSEOVERSECTION 6u
#define XXBYTE_NSIS_CB_ONVERIFYINSTDIR 7u
#define XXBYTE_NSIS_CB_ONSELCHANGE 8u
#define XXBYTE_NSIS_CB_ONREBOOTFAILED 9u
#define XXBYTE_NSIS_CB_COUNT 10u

/* Entry marks (xxbyte_nsis_script.marks). */
#define XXBYTE_NSIS_MARK_LABEL 0x1u    /* a jump target */
#define XXBYTE_NSIS_MARK_FUNCTION 0x2u /* a function's first entry */
#define XXBYTE_NSIS_MARK_SECTION 0x4u  /* a section's first entry */

/* xxbyte_nsis_script_init options. */
#define XXBYTE_NSIS_SCRIPT_UNINSTALLER 0x1u /* callbacks are un.onInit, ... */

/* String codes (xxbyte_nsis_script.string_codes). */
#define XXBYTE_NSIS_CODES_NSIS3 0u /* 1 lang, 2 shell, 3 var, 4 skip */
#define XXBYTE_NSIS_CODES_NSIS2 1u /* 255 lang, 254 shell, 253 var, 252 skip */
#define XXBYTE_NSIS_CODES_PARK                   \
    2u /* 0xE003 lang, 0xE002 shell, 0xE001 var, \
          0xE000 skip (Park's Unicode builds) */

/*
 * A parsed installer header: the model behind xxbyte_nsis_resolver and the
 * listings. Fill it with xxbyte_nsis_script_init and release it with
 * xxbyte_nsis_script_free. data is borrowed and must outlive the script.
 */
typedef struct xxbyte_nsis_script {
    const uint8_t *data; /* the decompressed header */
    size_t size;
    xxbyte_nsis_header header;
    uint32_t mode;         /* the decode mode (given, or guessed) */
    uint32_t options;      /* XXBYTE_NSIS_SCRIPT_* */
    uint32_t string_codes; /* XXBYTE_NSIS_CODES_*, from mode */
    uint32_t page_count;
    uint32_t page_size; /* bytes per record, from the block layout */
    uint32_t section_count;
    uint32_t section_size;
    uint32_t langtable_count;
    uint32_t langtable_size;                 /* header.langtable_size, 0 when unusable */
    uint32_t lang_string_count;              /* (langtable_size - 10) / 4 */
    uint32_t main_langtable;                 /* the table of language 1033, else 0 */
    int32_t callbacks[XXBYTE_NSIS_CB_COUNT]; /* entry numbers, -1 for none */
    int32_t install_directory;               /* string parameters */
    int32_t install_reg_key;
    int32_t install_reg_value;
    uint32_t install_reg_root;
    xxbyte_nsis_context context; /* entries_address: 0 by default */
    uint8_t *marks;              /* XXBYTE_NSIS_MARK_* per entry; owned */
} xxbyte_nsis_script;

/*
 * Parses header[0..size) (a decompressed header) into *script. mode is a
 * decode mode or XXBYTE_MODE_AUTO (xxbyte_nsis_guess_mode); with
 * XXBYTE_NSIS_V200 the header is read with the 7-block table. options are
 * XXBYTE_NSIS_SCRIPT_* bits. Decodes every entry to mark jump targets,
 * function starts (EW_CALL targets, the callbacks and the page functions)
 * and section starts; marks needs one byte per entry, allocated with
 * xx_mem_alloc. Returns the statuses of xxbyte_nsis_parse_header_ex,
 * XXBYTE_STATUS_UNSUPPORTED_MODE for an invalid mode,
 * XXBYTE_STATUS_INVALID_INSTRUCTION when the entries block does not fit in
 * size, XXBYTE_STATUS_OUT_OF_MEMORY, and XXBYTE_STATUS_INVALID_ARGUMENT for
 * other option bits. On failure *script is zeroed and owns nothing.
 * Records of the pages and sections blocks, the language tables and the
 * header fields after the block table are read only where they fit.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_script_init(xxbyte_nsis_script *script, const uint8_t *header, size_t size, uint32_t mode, uint32_t options);

/* Frees what xxbyte_nsis_script_init allocated and zeroes *script. */
XXBYTE_API void xxbyte_nsis_script_free(xxbyte_nsis_script *script);

/* Reads section index (XXBYTE_STATUS_INVALID_ARGUMENT past the count). */
XXBYTE_API xxbyte_status xxbyte_nsis_script_section(const xxbyte_nsis_script *script, uint32_t index, xxbyte_nsis_section *section);

/* Reads page index (XXBYTE_STATUS_INVALID_ARGUMENT past the count). */
XXBYTE_API xxbyte_status xxbyte_nsis_script_page(const xxbyte_nsis_script *script, uint32_t index, xxbyte_nsis_page *page);

/*
 * The string parameter of language string id in language table table: a
 * string-table offset (0 when the table leaves it empty). Returns
 * XXBYTE_STATUS_INVALID_ARGUMENT for a table or id past the counts.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_script_lang_string(const xxbyte_nsis_script *script, uint32_t table, uint32_t id, int32_t *string);

/* The callback name of index (".onInit", "un.onInit" for uninstallers),
 * NULL past XXBYTE_NSIS_CB_COUNT. */
XXBYTE_API const char *xxbyte_nsis_callback_name(uint32_t index, int uninstaller);

/* xxbyte_nsis_script_string flags. */
#define XXBYTE_NSIS_STRING_QUOTED 0x1u /* "...", with $\" $\r $\n $\t $$ */

/*
 * Decodes string parameter (a string-table offset, or a negative language
 * string reference as in entry parameters) to UTF-8 text in the script
 * syntax of makensis:
 *   variables      $0 $R9 $INSTDIR $_0_ (xxbyte_nsis_variable_name)
 *   shell folders  $PROGRAMFILES $SMPROGRAMS $APPDATA $QUICKLAUNCH ...
 *                  by their (current user, all users) CSIDL pair as
 *                  makensis of the mode's generation encodes it; the
 *                  registry-backed ones as $PROGRAMFILES, $COMMONFILES and
 *                  their 32/64 forms; an unknown pair as
 *                  $_SHELL_0xNN_0xNN_
 *   language       $(LSTR_n) (a negative parameter is just that)
 *   skip code      the escaped character itself
 * Unicode tables are converted from UTF-16LE (unpaired surrogates as
 * ${U+XXXX}); ANSI bytes 0x80-0xFF are taken as Latin-1 (the code page is
 * not recorded). With XXBYTE_NSIS_STRING_QUOTED the text is quoted, a
 * literal $ is $$, quote, CR, LF and TAB are $\" $\r $\n $\t, other control
 * characters ${U+XX}; without it they are written as they are. Writes at
 * most buffer_size - 1 bytes (whole characters and codes) and a NUL, and
 * sets *length to the full length. Returns XXBYTE_STATUS_INVALID_ARGUMENT
 * for an offset past the string table, XXBYTE_STATUS_TRUNCATED when the
 * string is not terminated inside it (the text up to the table end is
 * still written), XXBYTE_STATUS_OK otherwise.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_script_string(const xxbyte_nsis_script *script, int32_t parameter, uint32_t flags, char *buffer, size_t buffer_size, size_t *length);

/*
 * The label of entry number entry: the callback name for a callback
 * (".onInit"), "func_N" for another function start, "label_N" otherwise
 * (N the entry number). Same buffer rules as xxbyte_nsis_variable_name;
 * returns the full length.
 */
XXBYTE_API size_t xxbyte_nsis_script_label(const xxbyte_nsis_script *script, uint32_t entry, char *buffer, size_t buffer_size);

/*
 * A ready-made xxbyte_resolver whose context is a const
 * xxbyte_nsis_script *. Pass &script->context as the decode context so
 * that branch targets map back to entries. It prints
 *   strings            the decoded, quoted string ("$INSTDIR\file.txt";
 *                      cut with ..." at the formatter's 511 characters)
 *   language strings   $(LSTR_n)
 *   branch targets     the label of the target entry (.onInit, func_8,
 *                      label_12)
 * and keeps the numeric form of everything else (variables, data,
 * ctlcolors and exec flags already print by name).
 */
XXBYTE_API size_t xxbyte_nsis_resolver(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/*
 * Lists header[0..size), a decompressed header, as xxbyte_disasm_file does
 * for an installer: a summary (mode, blocks, install directory, pages,
 * the language strings of the main language table), then the entries,
 * one code body at a time under a header line, in entry order:
 *   Function .onInit         (callbacks, by name)
 *   Function func_8          (other functions: EW_CALL targets and page
 *                             functions, by entry number)
 *   Section "Main"           (sections, with their flags and index;
 *                             SectionGroup "..." and SectionGroupEnd for
 *                             group records)
 * with "label_N:" lines before jump targets, and names resolved by
 * xxbyte_nsis_resolver. Entries no body covers are listed under
 * "; entries N-M". mode is a decode mode or XXBYTE_MODE_AUTO; options are
 * XXBYTE_NSIS_SCRIPT_* bits. The address column is the byte offset of the
 * entry in the entries block (entry number * 28). Returns the statuses of
 * xxbyte_nsis_script_init and xxbyte_disasm_buf_ex.
 */
XXBYTE_API xxbyte_status xxbyte_nsis_list_header(const uint8_t *header, size_t size, uint32_t mode, uint32_t options, uint32_t listing_flags, struct xx_buf_s *out);

#ifdef __cplusplus
}
#endif

#endif
