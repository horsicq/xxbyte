/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_nsis_internal.h"

#if XXBYTE_USE_NSIS

#define ENTRY_SIZE XXBYTE_NSIS_ENTRY_SIZE
#define PARAM_COUNT 6u

#define G_NONE XXBYTE_GROUP_NONE
#define G_RETURN (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)
#define G_STOP XXBYTE_GROUP_STOP
#define G_CALL XXBYTE_GROUP_CALL
/* CONDITIONAL only when one of the jumps is nonzero, and STOP as well when
 * all of them are; EW_NOP is a JUMP when it has a target. */
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_GOTO (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)

/* LoadAndSetImage flags (fileform.h LASIF_*). */
#define LASIF_HWND 0x00000100u
#define LASIF_STRID 0x00010000u
#define REG_BINARY_TYPE 3

/*
 * Operand layouts: one character per parameter (see xxbyte_nsis.h).
 *   s string, v variable, j jump, i signed integer, x unsigned value,
 *   d data offset, c ctlcolors offset, f exec flag, - not read.
 * Kinds chosen by another parameter:
 *   P EW_PUSHPOP p0: v when p1 != 0 (Pop), else s
 *   L EW_LOG p1: i when p0 != 0 (LogSet), else s
 *   E EW_SECTIONSET p1: v when p2 >= 0 (get), else s
 *   T EW_INSTTYPESET p1: s when p2 != 0 (set), else v
 *   W EW_WRITEREG p3: d when p4 is REG_BINARY, else s
 *   M EW_LOADANDSETIMAGE p1: s when p3 has LASIF_STRID, else i
 *   N EW_LOADANDSETIMAGE p2: s when p3 has LASIF_HWND, else i
 *   A 3.05 EW_LOADANDSETIMAGE p0: s when p2 has LASIF_STRID, else i
 *   B 3.05 EW_LOADANDSETIMAGE p1: s when p2 has LASIF_HWND, else i
 */
typedef struct nsis_op {
    const char *name;
    uint16_t group;
    uint8_t limit;     /* parameters makensis may write */
    const char *spec;  /* NSIS 3 (3.06 and later) */
    const char *spec2; /* NSIS 2 and Park when different, else NULL */
} nsis_op;

/* Indexed by id. */
static const nsis_op ops[XXBYTE_NSIS_ID_COUNT] = {
    [XXBYTE_NSIS_ID_EW_RET] = {"EW_RET", G_RETURN, 0, "------", NULL},
    [XXBYTE_NSIS_ID_EW_NOP] = {"EW_NOP", G_NONE, 1, "j-----", NULL},
    [XXBYTE_NSIS_ID_EW_ABORT] = {"EW_ABORT", G_THROW, 1, "s-----", NULL},
    [XXBYTE_NSIS_ID_EW_QUIT] = {"EW_QUIT", G_STOP, 0, "------", NULL},
    [XXBYTE_NSIS_ID_EW_CALL] = {"EW_CALL", G_CALL, 2, "ji----", NULL},
    [XXBYTE_NSIS_ID_EW_UPDATETEXT] = {"EW_UPDATETEXT", G_NONE, 6, "s-----", "si----"},
    [XXBYTE_NSIS_ID_EW_SLEEP] = {"EW_SLEEP", G_NONE, 1, "s-----", NULL},
    [XXBYTE_NSIS_ID_EW_BRINGTOFRONT] = {"EW_BRINGTOFRONT", G_NONE, 0, "------", NULL},
    [XXBYTE_NSIS_ID_EW_CHDETAILSVIEW] = {"EW_CHDETAILSVIEW", G_NONE, 2, "ii----", NULL},
    [XXBYTE_NSIS_ID_EW_SETFILEATTRIBUTES] = {"EW_SETFILEATTRIBUTES", G_NONE, 2, "sx----", NULL},
    [XXBYTE_NSIS_ID_EW_CREATEDIR] = {"EW_CREATEDIR", G_NONE, 3, "sii---", "si----"},
    [XXBYTE_NSIS_ID_EW_IFFILEEXISTS] = {"EW_IFFILEEXISTS", G_COND, 3, "sjj---", NULL},
    [XXBYTE_NSIS_ID_EW_SETFLAG] = {"EW_SETFLAG", G_NONE, 4, "fsii--", "fsi---"},
    [XXBYTE_NSIS_ID_EW_IFFLAG] = {"EW_IFFLAG", G_COND, 4, "jjfx--", NULL},
    [XXBYTE_NSIS_ID_EW_GETFLAG] = {"EW_GETFLAG", G_NONE, 2, "vf----", NULL},
    [XXBYTE_NSIS_ID_EW_RENAME] = {"EW_RENAME", G_NONE, 4, "ssis--", NULL},
    [XXBYTE_NSIS_ID_EW_GETFULLPATHNAME] = {"EW_GETFULLPATHNAME", G_NONE, 3, "svi---", NULL},
    [XXBYTE_NSIS_ID_EW_SEARCHPATH] = {"EW_SEARCHPATH", G_NONE, 2, "vs----", NULL},
    [XXBYTE_NSIS_ID_EW_GETTEMPFILENAME] = {"EW_GETTEMPFILENAME", G_NONE, 2, "vs----", NULL},
    [XXBYTE_NSIS_ID_EW_EXTRACTFILE] = {"EW_EXTRACTFILE", G_NONE, 6, "xsdxxs", NULL},
    [XXBYTE_NSIS_ID_EW_DELETEFILE] = {"EW_DELETEFILE", G_NONE, 2, "sx----", NULL},
    [XXBYTE_NSIS_ID_EW_MESSAGEBOX] = {"EW_MESSAGEBOX", G_COND, 6, "xsijij", NULL},
    [XXBYTE_NSIS_ID_EW_RMDIR] = {"EW_RMDIR", G_NONE, 2, "sx----", NULL},
    [XXBYTE_NSIS_ID_EW_STRLEN] = {"EW_STRLEN", G_NONE, 2, "vs----", NULL},
    [XXBYTE_NSIS_ID_EW_ASSIGNVAR] = {"EW_ASSIGNVAR", G_NONE, 4, "vsss--", NULL},
    [XXBYTE_NSIS_ID_EW_STRCMP] = {"EW_STRCMP", G_COND, 5, "ssjji-", NULL},
    [XXBYTE_NSIS_ID_EW_READENVSTR] = {"EW_READENVSTR", G_NONE, 3, "vsi---", NULL},
    [XXBYTE_NSIS_ID_EW_INTCMP] = {"EW_INTCMP", G_COND, 6, "ssjjjx", NULL},
    [XXBYTE_NSIS_ID_EW_INTOP] = {"EW_INTOP", G_NONE, 4, "vssi--", NULL},
    [XXBYTE_NSIS_ID_EW_INTFMT] = {"EW_INTFMT", G_NONE, 4, "vssi--", "vss---"},
    [XXBYTE_NSIS_ID_EW_PUSHPOP] = {"EW_PUSHPOP", G_NONE, 6, "Pii---", NULL},
    [XXBYTE_NSIS_ID_EW_FINDWINDOW] = {"EW_FINDWINDOW", G_NONE, 6, "vssssx", NULL},
    [XXBYTE_NSIS_ID_EW_SENDMESSAGE] = {"EW_SENDMESSAGE", G_NONE, 6, "vssssx", NULL},
    [XXBYTE_NSIS_ID_EW_ISWINDOW] = {"EW_ISWINDOW", G_COND, 3, "sjj---", NULL},
    [XXBYTE_NSIS_ID_EW_GETDLGITEM] = {"EW_GETDLGITEM", G_NONE, 3, "vss---", NULL},
    [XXBYTE_NSIS_ID_EW_SETCTLCOLORS] = {"EW_SETCTLCOLORS", G_NONE, 2, "sc----", NULL},
    [XXBYTE_NSIS_ID_EW_LOADANDSETIMAGE] = {"EW_LOADANDSETIMAGE", G_NONE, 4, "vMNx--", NULL},
    [XXBYTE_NSIS_ID_EW_CREATEFONT] = {"EW_CREATEFONT", G_NONE, 5, "vsssx-", NULL},
    [XXBYTE_NSIS_ID_EW_SHOWWINDOW] = {"EW_SHOWWINDOW", G_NONE, 4, "ssii--", NULL},
    [XXBYTE_NSIS_ID_EW_SHELLEXEC] = {"EW_SHELLEXEC", G_NONE, 6, "sssixs", "sssi-s"},
    [XXBYTE_NSIS_ID_EW_EXECUTE] = {"EW_EXECUTE", G_NONE, 3, "svi---", NULL},
    [XXBYTE_NSIS_ID_EW_GETFILETIME] = {"EW_GETFILETIME", G_NONE, 3, "vvs---", NULL},
    [XXBYTE_NSIS_ID_EW_GETDLLVERSION] = {"EW_GETDLLVERSION", G_NONE, 4, "vvsi--", "vvs---"},
    [XXBYTE_NSIS_ID_EW_REGISTERDLL] = {"EW_REGISTERDLL", G_CALL, 6, "sssii-", NULL},
    [XXBYTE_NSIS_ID_EW_CREATESHORTCUT] = {"EW_CREATESHORTCUT", G_NONE, 6, "ssssxs", NULL},
    [XXBYTE_NSIS_ID_EW_COPYFILES] = {"EW_COPYFILES", G_NONE, 4, "ssxs--", NULL},
    [XXBYTE_NSIS_ID_EW_REBOOT] = {"EW_REBOOT", G_NONE, 1, "x-----", NULL},
    [XXBYTE_NSIS_ID_EW_WRITEINI] = {"EW_WRITEINI", G_NONE, 5, "ssssi-", NULL},
    [XXBYTE_NSIS_ID_EW_READINISTR] = {"EW_READINISTR", G_NONE, 4, "vsss--", NULL},
    [XXBYTE_NSIS_ID_EW_DELREG] = {"EW_DELREG", G_NONE, 5, "-xssx-", NULL},
    [XXBYTE_NSIS_ID_EW_WRITEREG] = {"EW_WRITEREG", G_NONE, 6, "xssWii", NULL},
    [XXBYTE_NSIS_ID_EW_READREGSTR] = {"EW_READREGSTR", G_NONE, 5, "vxssi-", NULL},
    [XXBYTE_NSIS_ID_EW_REGENUM] = {"EW_REGENUM", G_NONE, 5, "vxssi-", NULL},
    [XXBYTE_NSIS_ID_EW_FCLOSE] = {"EW_FCLOSE", G_NONE, 1, "v-----", NULL},
    [XXBYTE_NSIS_ID_EW_FOPEN] = {"EW_FOPEN", G_NONE, 4, "vxxs--", NULL},
    [XXBYTE_NSIS_ID_EW_FPUTS] = {"EW_FPUTS", G_NONE, 3, "vsi---", NULL},
    [XXBYTE_NSIS_ID_EW_FGETS] = {"EW_FGETS", G_NONE, 4, "vvsi--", NULL},
    [XXBYTE_NSIS_ID_EW_FSEEK] = {"EW_FSEEK", G_NONE, 4, "vvsi--", NULL},
    [XXBYTE_NSIS_ID_EW_FINDCLOSE] = {"EW_FINDCLOSE", G_NONE, 1, "v-----", NULL},
    [XXBYTE_NSIS_ID_EW_FINDNEXT] = {"EW_FINDNEXT", G_NONE, 2, "vv----", NULL},
    [XXBYTE_NSIS_ID_EW_FINDFIRST] = {"EW_FINDFIRST", G_NONE, 3, "vvs---", NULL},
    [XXBYTE_NSIS_ID_EW_WRITEUNINSTALLER] = {"EW_WRITEUNINSTALLER", G_NONE, 4, "sdis--", NULL},
    [XXBYTE_NSIS_ID_EW_LOG] = {"EW_LOG", G_NONE, 2, "iL----", NULL},
    [XXBYTE_NSIS_ID_EW_SECTIONSET] = {"EW_SECTIONSET", G_NONE, 5, "sEiis-", NULL},
    [XXBYTE_NSIS_ID_EW_INSTTYPESET] = {"EW_INSTTYPESET", G_NONE, 4, "sTii--", NULL},
    [XXBYTE_NSIS_ID_EW_GETOSINFO] = {"EW_GETOSINFO", G_NONE, 5, "-vsis-", NULL},
    [XXBYTE_NSIS_ID_EW_LOCKWINDOW] = {"EW_LOCKWINDOW", G_NONE, 1, "i-----", NULL},
    [XXBYTE_NSIS_ID_EW_FPUTWS] = {"EW_FPUTWS", G_NONE, 4, "vsii--", "vsi---"},
    [XXBYTE_NSIS_ID_EW_FGETWS] = {"EW_FGETWS", G_NONE, 4, "vvsi--", NULL},
    [XXBYTE_NSIS_ID_EW_SETBRANDINGIMAGE] = {"EW_SETBRANDINGIMAGE", G_NONE, 3, "sii---", NULL},
    [XXBYTE_NSIS_ID_EW_GETFONTVERSION] = {"EW_GETFONTVERSION", G_NONE, 2, "vs----", NULL},
    [XXBYTE_NSIS_ID_EW_GETFONTNAME] = {"EW_GETFONTNAME", G_NONE, 2, "vs----", NULL},
    [XXBYTE_NSIS_ID_EW_FINDPROC] = {"EW_FINDPROC", G_NONE, 2, "vs----", NULL},
};

#define VBIT(variant) (1u << XXBYTE_NSIS_##variant)
#define V2_OLD (VBIT(V200) | VBIT(V201) | VBIT(V202) | VBIT(V204))
#define V_PARK (VBIT(PARK1) | VBIT(PARK2) | VBIT(PARK3))

/*
 * Variants whose releases read fewer, more or other parameters than the
 * spec (or spec2) of ops[]: an operand is a parameter that the exehead of
 * at least one release of the variant reads (exec.c of every release).
 */
typedef struct nsis_spec_override {
    uint8_t id;
    uint16_t variants; /* VBIT() mask */
    const char *spec;
} nsis_spec_override;

static const nsis_spec_override spec_overrides[] = {
    /* parameter 3: 3.10 */
    {XXBYTE_NSIS_ID_EW_SETFLAG, VBIT(V300) | VBIT(V305), "fsi---"},
    /* parameter 2: 2.31 */
    {XXBYTE_NSIS_ID_EW_SETFLAG, V2_OLD, "fs----"},
    /* parameter 3: 3.08 */
    {XXBYTE_NSIS_ID_EW_GETDLLVERSION, VBIT(V300) | VBIT(V305), "vvs---"},
    /* SetDetailsPrint in parameters 1 and 2: 2.26 to 2.30; Park (2.46)
     * reads parameter 0 only */
    {XXBYTE_NSIS_ID_EW_UPDATETEXT, VBIT(V226), "sii---"},
    {XXBYTE_NSIS_ID_EW_UPDATETEXT, V_PARK, "s-----"},
    /* parameter 2 (restrict the ACL): 2.51 */
    {XXBYTE_NSIS_ID_EW_CREATEDIR, VBIT(V226), "sii---"},
    /* parameter 3 of these four: 2.29 */
    {XXBYTE_NSIS_ID_EW_RENAME, V2_OLD, "ssi---"},
    {XXBYTE_NSIS_ID_EW_SHELLEXEC, V2_OLD, "sssi--"},
    {XXBYTE_NSIS_ID_EW_COPYFILES, V2_OLD, "ssx---"},
    {XXBYTE_NSIS_ID_EW_WRITEUNINSTALLER, V2_OLD, "sdi---"},
    /* parameter 4: StrCmp 2.13, SectionSet 2.05, RegisterDLL 2.03 */
    {XXBYTE_NSIS_ID_EW_STRCMP, VBIT(V200) | VBIT(V201) | VBIT(V202), "ssjj--"},
    {XXBYTE_NSIS_ID_EW_SECTIONSET, VBIT(V200) | VBIT(V201) | VBIT(V202), "sEi---"},
    {XXBYTE_NSIS_ID_EW_REGISTERDLL, VBIT(V200) | VBIT(V201), "sssi--"},
    /* 2.0: root key, key, value name, flags; 2.01 moved them up by one */
    {XXBYTE_NSIS_ID_EW_DELREG, VBIT(V200), "xssx--"},
};

/*
 * The opcode slots of all variants in order: opcode n is the n-th slot
 * present in the mode (opcode 0 is EW_INVALID_OPCODE).
 */
enum {
    S_ALL,          /* every variant */
    S_PARK2,        /* Park 2.46.2 and later: EW_GETFONTVERSION */
    S_PARK3,        /* Park 2.46.3 and later: EW_GETFONTNAME */
    S_PARK_UNICODE, /* Park's Unicode builds: EW_FPUTWS, EW_FGETWS */
    S_LOG,          /* NSIS_CONFIG_LOG builds */
    S_IMAGE,        /* EW_SETBRANDINGIMAGE, EW_LOADANDSETIMAGE from 3.05 */
    S_OSINFO,       /* EW_GETOSINFO from 3.06, EW_GETLABELADDR before */
    S_RESERVED,     /* EW_RESERVEDOPCODE, EW_GETFUNCTIONADDR before 3.06 */
    S_PARK,         /* Park: EW_FINDPROC */
    S_NSIS3_UNICODE /* NSIS 3 Unicode: EW_FPUTWS, EW_FGETWS */
};

typedef struct nsis_slot {
    uint8_t id;
    uint8_t when;
} nsis_slot;

#define SLOT(name) {XXBYTE_NSIS_ID_EW_##name, S_ALL}

static const nsis_slot slots[] = {
    SLOT(RET),
    SLOT(NOP),
    SLOT(ABORT),
    SLOT(QUIT),
    SLOT(CALL),
    SLOT(UPDATETEXT),
    SLOT(SLEEP),
    SLOT(BRINGTOFRONT),
    SLOT(CHDETAILSVIEW),
    SLOT(SETFILEATTRIBUTES),
    SLOT(CREATEDIR),
    SLOT(IFFILEEXISTS),
    SLOT(SETFLAG),
    SLOT(IFFLAG),
    SLOT(GETFLAG),
    SLOT(RENAME),
    SLOT(GETFULLPATHNAME),
    SLOT(SEARCHPATH),
    SLOT(GETTEMPFILENAME),
    SLOT(EXTRACTFILE),
    SLOT(DELETEFILE),
    SLOT(MESSAGEBOX),
    SLOT(RMDIR),
    SLOT(STRLEN),
    SLOT(ASSIGNVAR),
    SLOT(STRCMP),
    SLOT(READENVSTR),
    SLOT(INTCMP),
    SLOT(INTOP),
    SLOT(INTFMT),
    SLOT(PUSHPOP),
    SLOT(FINDWINDOW),
    SLOT(SENDMESSAGE),
    SLOT(ISWINDOW),
    SLOT(GETDLGITEM),
    SLOT(SETCTLCOLORS),
    {0, S_IMAGE},
    SLOT(CREATEFONT),
    SLOT(SHOWWINDOW),
    SLOT(SHELLEXEC),
    SLOT(EXECUTE),
    SLOT(GETFILETIME),
    SLOT(GETDLLVERSION),
    {XXBYTE_NSIS_ID_EW_GETFONTVERSION, S_PARK2},
    {XXBYTE_NSIS_ID_EW_GETFONTNAME, S_PARK3},
    SLOT(REGISTERDLL),
    SLOT(CREATESHORTCUT),
    SLOT(COPYFILES),
    SLOT(REBOOT),
    SLOT(WRITEINI),
    SLOT(READINISTR),
    SLOT(DELREG),
    SLOT(WRITEREG),
    SLOT(READREGSTR),
    SLOT(REGENUM),
    SLOT(FCLOSE),
    SLOT(FOPEN),
    SLOT(FPUTS),
    SLOT(FGETS),
    {XXBYTE_NSIS_ID_EW_FPUTWS, S_PARK_UNICODE},
    {XXBYTE_NSIS_ID_EW_FGETWS, S_PARK_UNICODE},
    SLOT(FSEEK),
    SLOT(FINDCLOSE),
    SLOT(FINDNEXT),
    SLOT(FINDFIRST),
    SLOT(WRITEUNINSTALLER),
    {XXBYTE_NSIS_ID_EW_LOG, S_LOG},
    SLOT(SECTIONSET),
    SLOT(INSTTYPESET),
    {0, S_OSINFO},
    {0, S_RESERVED},
    SLOT(LOCKWINDOW),
    {XXBYTE_NSIS_ID_EW_FINDPROC, S_PARK},
    {XXBYTE_NSIS_ID_EW_FPUTWS, S_NSIS3_UNICODE},
    {XXBYTE_NSIS_ID_EW_FGETWS, S_NSIS3_UNICODE},
};

#define SLOT_COUNT (sizeof(slots) / sizeof(slots[0]))

/* Predefined variables after $0-$9 and $R0-$R9, by release. */
static const char *const vars_226[] = {
    "CMDLINE", "INSTDIR", "OUTDIR", "EXEDIR", "LANGUAGE", "TEMP", "PLUGINSDIR", "EXEPATH", "EXEFILE", "HWNDPARENT", "_CLICK", "_OUTDIR",
};
static const char *const vars_204[] = {
    "CMDLINE", "INSTDIR", "OUTDIR", "EXEDIR", "LANGUAGE", "TEMP", "PLUGINSDIR", "HWNDPARENT", "_CLICK", "_OUTDIR",
};
static const char *const vars_200[] = {
    "CMDLINE", "INSTDIR", "OUTDIR", "EXEDIR", "LANGUAGE", "TEMP", "PLUGINSDIR", "HWNDPARENT", "_CLICK",
};

/* exec_flags fields: 2.42 and later (2.26 to 2.41 name field 7
 * insttype_changed, unused there; status_update is 2.31 and later), 2.02
 * to 2.25, and 2.0 to 2.01. */
static const char *const flags_242[] = {
    "autoclose",          "all_user_var", "exec_error",    "abort", "exec_reboot", "reboot_called",  "cur_insttype",
    "plugin_api_version", "silent",       "instdir_error", "rtl",   "errlvl",      "alter_reg_view", "status_update",
};
static const char *const flags_202[] = {
    "autoclose", "all_user_var", "exec_error", "abort", "exec_reboot", "reboot_called", "cur_insttype", "insttype_changed", "silent", "instdir_error", "rtl", "errlvl",
};
static const char *const flags_200[] = {
    "autoclose", "all_user_var", "exec_error", "abort", "exec_reboot", "cur_insttype", "insttype_changed", "silent", "instdir_error", "rtl",
};

static int is_nsis3(uint32_t variant)
{
    return variant <= XXBYTE_NSIS_V300;
}

static int is_park(uint32_t variant)
{
    return variant >= XXBYTE_NSIS_PARK1 && variant <= XXBYTE_NSIS_PARK3;
}

int xxbyte_nsis_variant_is_nsis3(uint32_t variant)
{
    return is_nsis3(variant);
}

int xxbyte_nsis_variant_is_park(uint32_t variant)
{
    return is_park(variant);
}

int xxbyte_nsis_mode_valid(uint32_t mode)
{
    uint32_t variant = XXBYTE_NSIS_MODE_VARIANT(mode);

    if ((mode & ~(uint32_t)(0xFFu | XXBYTE_NSIS_MODE_ANSI | XXBYTE_NSIS_MODE_LOG)) != 0 || variant >= XXBYTE_NSIS_VARIANT_COUNT) {
        return 0;
    }
    /* The 2.x releases only build ANSI installers. */
    if (!is_nsis3(variant) && !is_park(variant) && (mode & XXBYTE_NSIS_MODE_ANSI) == 0) {
        return 0;
    }
    return 1;
}

uint32_t xxbyte_nsis_param_limit(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_NSIS_ID_COUNT) {
        return 0;
    }
    return ops[id].limit;
}

static int slot_present(const nsis_slot *slot, uint32_t mode)
{
    uint32_t variant = XXBYTE_NSIS_MODE_VARIANT(mode);
    int ansi = (mode & XXBYTE_NSIS_MODE_ANSI) != 0;

    switch (slot->when) {
        case S_PARK2: return variant == XXBYTE_NSIS_PARK2 || variant == XXBYTE_NSIS_PARK3;
        case S_PARK3: return variant == XXBYTE_NSIS_PARK3;
        case S_PARK_UNICODE: return is_park(variant) && !ansi;
        case S_LOG: return (mode & XXBYTE_NSIS_MODE_LOG) != 0;
        case S_PARK: return is_park(variant);
        case S_NSIS3_UNICODE: return is_nsis3(variant) && !ansi;
        default: return 1;
    }
}

static uint32_t slot_id(const nsis_slot *slot, uint32_t mode)
{
    uint32_t variant = XXBYTE_NSIS_MODE_VARIANT(mode);

    switch (slot->when) {
        case S_IMAGE: return variant == XXBYTE_NSIS_V306 || variant == XXBYTE_NSIS_V305 ? XXBYTE_NSIS_ID_EW_LOADANDSETIMAGE : XXBYTE_NSIS_ID_EW_SETBRANDINGIMAGE;
        case S_OSINFO: return variant == XXBYTE_NSIS_V306 ? XXBYTE_NSIS_ID_EW_GETOSINFO : 0;
        case S_RESERVED: return 0;
        default: return slot->id;
    }
}

/* The id of opcode in a valid mode, 0 when it is undefined there. */
static uint32_t lookup_opcode(uint32_t mode, uint32_t opcode)
{
    uint32_t n = 0;
    size_t i;

    if (opcode == 0 || opcode > SLOT_COUNT) {
        return 0;
    }
    for (i = 0; i < SLOT_COUNT; ++i) {
        if (!slot_present(&slots[i], mode)) {
            continue;
        }
        if (++n == opcode) {
            return slot_id(&slots[i], mode);
        }
    }
    return 0;
}

uint32_t xxbyte_nsis_opcode_id(uint32_t mode, uint32_t opcode)
{
    if (!xxbyte_nsis_mode_valid(mode)) {
        return 0;
    }
    return lookup_opcode(mode, opcode);
}

uint32_t xxbyte_nsis_id_opcode(uint32_t mode, uint32_t id)
{
    uint32_t n = 0;
    size_t i;

    if (!xxbyte_nsis_mode_valid(mode) || id == 0 || id >= XXBYTE_NSIS_ID_COUNT) {
        return 0;
    }
    for (i = 0; i < SLOT_COUNT; ++i) {
        if (!slot_present(&slots[i], mode)) {
            continue;
        }
        ++n;
        if (slot_id(&slots[i], mode) == id) {
            return n;
        }
    }
    return 0;
}

const char *xxbyte_nsis_mnemonic(uint32_t id)
{
    if (id == 0 || id >= XXBYTE_NSIS_ID_COUNT) {
        return NULL;
    }
    return ops[id].name;
}

static void named_vars(uint32_t mode, const char *const **names, uint32_t *count)
{
    switch (XXBYTE_NSIS_MODE_VARIANT(mode)) {
        case XXBYTE_NSIS_V200:
        case XXBYTE_NSIS_V201:
        case XXBYTE_NSIS_V202:
            *names = vars_200;
            *count = (uint32_t)(sizeof(vars_200) / sizeof(vars_200[0]));
            break;
        case XXBYTE_NSIS_V204:
            *names = vars_204;
            *count = (uint32_t)(sizeof(vars_204) / sizeof(vars_204[0]));
            break;
        default:
            *names = vars_226;
            *count = (uint32_t)(sizeof(vars_226) / sizeof(vars_226[0]));
            break;
    }
}

uint32_t xxbyte_nsis_first_user_variable(uint32_t mode)
{
    const char *const *names;
    uint32_t count;

    if (!xxbyte_nsis_mode_valid(mode)) {
        return 0;
    }
    named_vars(mode, &names, &count);
    return 20u + count;
}

static size_t put_decimal(char *out, uint32_t value)
{
    char digits[10];
    size_t n = 0;
    size_t i;

    do {
        digits[n++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0);
    for (i = 0; i < n; ++i) {
        out[i] = digits[n - 1 - i];
    }
    return n;
}

/* Writes the variable's name to out (at least 24 bytes), NUL-terminated;
 * returns its length. */
static size_t variable_text(uint32_t mode, uint32_t index, char *out)
{
    const char *const *names;
    uint32_t count;
    size_t length = 1;

    named_vars(mode, &names, &count);
    out[0] = '$';
    if (index < 20u) {
        if (index >= 10u) {
            out[length++] = 'R';
            index -= 10u;
        }
        out[length++] = (char)('0' + index);
    } else if (index < 20u + count) {
        const char *name = names[index - 20u];
        while (*name != '\0') {
            out[length++] = *name++;
        }
    } else {
        out[length++] = '_';
        length += put_decimal(out + length, index - 20u - count);
        out[length++] = '_';
    }
    out[length] = '\0';
    return length;
}

size_t xxbyte_nsis_variable_name(uint32_t mode, uint32_t index, char *buffer, size_t buffer_size)
{
    char text[32];
    size_t length;

    if (buffer != NULL && buffer_size != 0) {
        buffer[0] = '\0';
    }
    if (!xxbyte_nsis_mode_valid(mode) || (buffer == NULL && buffer_size != 0)) {
        return 0;
    }
    length = variable_text(mode, index, text);
    if (buffer_size != 0) {
        size_t copy = length < buffer_size ? length : buffer_size - 1;
        memcpy(buffer, text, copy);
        buffer[copy] = '\0';
    }
    return length;
}

const char *xxbyte_nsis_flag_name(uint32_t mode, uint32_t index)
{
    uint32_t variant = XXBYTE_NSIS_MODE_VARIANT(mode);

    if (!xxbyte_nsis_mode_valid(mode)) {
        return NULL;
    }
    if (variant == XXBYTE_NSIS_V200 || variant == XXBYTE_NSIS_V201) {
        return index < sizeof(flags_200) / sizeof(flags_200[0]) ? flags_200[index] : NULL;
    }
    if (variant == XXBYTE_NSIS_V202 || variant == XXBYTE_NSIS_V204) {
        return index < sizeof(flags_202) / sizeof(flags_202[0]) ? flags_202[index] : NULL;
    }
    return index < sizeof(flags_242) / sizeof(flags_242[0]) ? flags_242[index] : NULL;
}

static const char *op_spec(uint32_t mode, uint32_t id)
{
    const nsis_op *op = &ops[id];
    uint32_t variant = XXBYTE_NSIS_MODE_VARIANT(mode);
    size_t i;

    /* 3.06 added the output variable in front of 3.05's parameters. */
    if (id == XXBYTE_NSIS_ID_EW_LOADANDSETIMAGE && variant == XXBYTE_NSIS_V305) {
        return "ABx---";
    }
    for (i = 0; i < sizeof(spec_overrides) / sizeof(spec_overrides[0]); ++i) {
        if (spec_overrides[i].id == id && (spec_overrides[i].variants & (1u << variant)) != 0) {
            return spec_overrides[i].spec;
        }
    }
    if (op->spec2 != NULL && !is_nsis3(variant)) {
        return op->spec2;
    }
    return op->spec;
}

/* The kind of a parameter whose kind depends on another one. */
static char resolve_kind(char kind, const int32_t *params)
{
    switch (kind) {
        case 'P': return params[1] != 0 ? 'v' : 's';
        case 'L': return params[0] != 0 ? 'i' : 's';
        case 'E': return params[2] >= 0 ? 'v' : 's';
        case 'T': return params[2] != 0 ? 's' : 'v';
        case 'W': return params[4] == REG_BINARY_TYPE ? 'd' : 's';
        case 'M': return ((uint32_t)params[3] & LASIF_STRID) != 0 ? 's' : 'i';
        case 'N': return ((uint32_t)params[3] & LASIF_HWND) != 0 ? 's' : 'i';
        case 'A': return ((uint32_t)params[2] & LASIF_STRID) != 0 ? 's' : 'i';
        case 'B': return ((uint32_t)params[2] & LASIF_HWND) != 0 ? 's' : 'i';
        default: return kind;
    }
}

static void add_param(xxbyte_instruction *instruction, char kind, int32_t value, uint32_t index, uint64_t base)
{
    xxbyte_operand *operand = NULL;
    int64_t v = value;

    switch (kind) {
        case 's':
            if (v >= 0) {
                operand = xxbyte_add_index(instruction, XXBYTE_INDEX_STRING, 4, v);
            } else {
                operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_NSIS_INDEX_LANGSTRING, 4, -(v + 1));
            }
            break;
        case 'v':
            if (v >= 0) {
                operand = xxbyte_add_index(instruction, XXBYTE_INDEX_VARIABLE, 4, v);
            } else {
                operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, v);
                if (operand != NULL) {
                    operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
                }
            }
            break;
        case 'j':
            if (v > 0) {
                operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, 4, (int64_t)(base + (uint64_t)(v - 1) * ENTRY_SIZE));
            } else if (v == 0) {
                operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, 0);
                if (operand != NULL) {
                    operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
                }
            } else {
                operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_NSIS_INDEX_JUMP_VARIABLE, 4, -(v + 1));
            }
            break;
        case 'i':
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, v);
            if (operand != NULL) {
                operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
            }
            break;
        case 'x': operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, 4, (int64_t)(uint32_t)value); break;
        case 'd': operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_NSIS_INDEX_DATA, 4, (int64_t)(uint32_t)value); break;
        case 'c': operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_NSIS_INDEX_CTLCOLORS, 4, (int64_t)(uint32_t)value); break;
        case 'f': operand = xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_NSIS_INDEX_FLAG, 4, v); break;
        default: break;
    }
    if (operand != NULL) {
        operand->extra = index;
    }
}

static xxbyte_status nsis_check_mode(uint32_t mode)
{
    return xxbyte_nsis_mode_valid(mode) ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static xxbyte_status nsis_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const xxbyte_nsis_context *nsis_context = (const xxbyte_nsis_context *)context;
    uint64_t base = nsis_context != NULL ? nsis_context->entries_address : 0;
    int32_t params[PARAM_COUNT];
    const char *spec;
    uint32_t which;
    uint32_t id;
    uint32_t group;
    uint32_t i;
    uint32_t jump_params = 0;
    uint32_t jumps = 0;

    (void)address;
    if (code_size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    which = xxbyte_u32le(code);
    id = lookup_opcode(mode, which);
    if (id == 0) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (code_size < ENTRY_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    for (i = 0; i < PARAM_COUNT; ++i) {
        params[i] = (int32_t)xxbyte_sext(xxbyte_u32le(code + 4 + 4 * i), 32);
    }

    spec = op_spec(mode, id);
    for (i = 0; i < PARAM_COUNT; ++i) {
        char kind = resolve_kind(spec[i], params);
        if (kind == '-') {
            continue;
        }
        if (kind == 'j') {
            ++jump_params;
            jumps += params[i] != 0;
        }
        add_param(instruction, kind, params[i], i, base);
    }

    group = ops[id].group;
    if (group == G_COND) {
        if (jumps == 0) {
            group = G_NONE;
        } else if (jumps == jump_params && id != XXBYTE_NSIS_ID_EW_MESSAGEBOX) {
            /* Every outcome jumps (a MessageBox can return other buttons). */
            group |= XXBYTE_GROUP_STOP;
        }
    }
    if (id == XXBYTE_NSIS_ID_EW_NOP && params[0] != 0) {
        group = G_GOTO;
    }
    instruction->size = ENTRY_SIZE;
    instruction->opcode = which;
    instruction->id = id;
    instruction->group = group;
    instruction->mnemonic = ops[id].name;
    return XXBYTE_STATUS_OK;
}

static int format_variable(xxbyte_fmt *fmt, uint32_t mode, int64_t index)
{
    char text[32];

    if (index < 0 || index > (int64_t)UINT32_MAX) {
        return xxbyte_fmt_printf(fmt, "$%lld", (long long)index);
    }
    variable_text(mode, (uint32_t)index, text);
    return xxbyte_fmt_str(fmt, text);
}

static int format_operand(const xxbyte_instruction *instruction, uint32_t index, xxbyte_fmt *fmt)
{
    const xxbyte_operand *operand = &instruction->operands[index];

    if (operand->type == XXBYTE_OPERAND_INDEX || operand->type == XXBYTE_OPERAND_BRANCH) {
        int resolved = xxbyte_fmt_resolve(fmt, instruction, index);
        if (resolved != 0) {
            return resolved > 0;
        }
    }
    switch (operand->type) {
        case XXBYTE_OPERAND_BRANCH: return xxbyte_fmt_printf(fmt, "0x%llX", (unsigned long long)operand->value);
        case XXBYTE_OPERAND_IMMEDIATE:
            if ((operand->flags & XXBYTE_OPERAND_FLAG_SIGNED) != 0) {
                return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
            }
            return xxbyte_fmt_printf(fmt, "0x%llX", (unsigned long long)operand->value);
        case XXBYTE_OPERAND_INDEX:
            switch (operand->kind) {
                case XXBYTE_INDEX_STRING: return xxbyte_fmt_printf(fmt, "string@0x%llX", (unsigned long long)operand->value);
                case XXBYTE_NSIS_INDEX_LANGSTRING: return xxbyte_fmt_printf(fmt, "lang@%llu", (unsigned long long)operand->value);
                case XXBYTE_INDEX_VARIABLE:
                case XXBYTE_NSIS_INDEX_JUMP_VARIABLE: return format_variable(fmt, instruction->mode, operand->value);
                case XXBYTE_NSIS_INDEX_DATA: return xxbyte_fmt_printf(fmt, "data@0x%llX", (unsigned long long)operand->value);
                case XXBYTE_NSIS_INDEX_CTLCOLORS: return xxbyte_fmt_printf(fmt, "ctlcolors@0x%llX", (unsigned long long)operand->value);
                case XXBYTE_NSIS_INDEX_FLAG: {
                    const char *name = NULL;
                    if (operand->value >= 0 && operand->value <= (int64_t)UINT32_MAX) {
                        name = xxbyte_nsis_flag_name(instruction->mode, (uint32_t)operand->value);
                    }
                    if (name != NULL) {
                        return xxbyte_fmt_str(fmt, name);
                    }
                    return xxbyte_fmt_printf(fmt, "flag@%lld", (long long)operand->value);
                }
                default: break;
            }
            break;
        default: break;
    }
    return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
}

static int nsis_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t i;

    for (i = 0; i < instruction->operand_count; ++i) {
        if (!xxbyte_fmt_str(fmt, i == 0 ? " " : ", ") || !format_operand(instruction, i, fmt)) {
            return 0;
        }
    }
    return 1;
}

static size_t nsis_unit_size(uint32_t mode)
{
    (void)mode;
    return 28;
}

const xxbyte_family_ops xxbyte_nsis_ops = {
    XXBYTE_FAMILY_NSIS, XXBYTE_NSIS_MODE_DEFAULT, nsis_check_mode, nsis_decode, NULL, nsis_format_operands, nsis_unit_size, xxbyte_nsis_list_file,
};

/* The raw parameter index of an entry decoded into instruction. */
static int32_t entry_param(const xxbyte_instruction *instruction, uint32_t index)
{
    return (int32_t)xxbyte_sext(xxbyte_u32le(instruction->code + 4 + 4 * index), 32);
}

static const char *flag_command(uint32_t id, const char *flag)
{
    static const struct {
        uint8_t id;
        const char *flag;
        const char *command;
    } table[] = {
        {XXBYTE_NSIS_ID_EW_SETFLAG, "autoclose", "SetAutoClose"},
        {XXBYTE_NSIS_ID_EW_SETFLAG, "all_user_var", "SetShellVarContext"},
        {XXBYTE_NSIS_ID_EW_SETFLAG, "exec_error", "ClearErrors/SetErrors"},
        {XXBYTE_NSIS_ID_EW_SETFLAG, "exec_reboot", "SetRebootFlag"},
        {XXBYTE_NSIS_ID_EW_SETFLAG, "silent", "SetSilent"},
        {XXBYTE_NSIS_ID_EW_SETFLAG, "errlvl", "SetErrorLevel"},
        {XXBYTE_NSIS_ID_EW_SETFLAG, "alter_reg_view", "SetRegView"},
        {XXBYTE_NSIS_ID_EW_SETFLAG, "status_update", "SetDetailsPrint"},
        {XXBYTE_NSIS_ID_EW_IFFLAG, "abort", "IfAbort"},
        {XXBYTE_NSIS_ID_EW_IFFLAG, "exec_error", "IfErrors"},
        {XXBYTE_NSIS_ID_EW_IFFLAG, "exec_reboot", "IfRebootFlag"},
        {XXBYTE_NSIS_ID_EW_IFFLAG, "silent", "IfSilent"},
        {XXBYTE_NSIS_ID_EW_IFFLAG, "rtl", "IfRtlLanguage"},
        {XXBYTE_NSIS_ID_EW_IFFLAG, "all_user_var", "IfShellVarContextAll"},
        {XXBYTE_NSIS_ID_EW_IFFLAG, "alter_reg_view", "IfAltRegView"},
        {XXBYTE_NSIS_ID_EW_GETFLAG, "errlvl", "GetErrorLevel"},
        {XXBYTE_NSIS_ID_EW_GETFLAG, "instdir_error", "GetInstDirError"},
        {XXBYTE_NSIS_ID_EW_GETFLAG, "all_user_var", "GetShellVarContext"},
        {XXBYTE_NSIS_ID_EW_GETFLAG, "alter_reg_view", "GetRegView"},
    };
    size_t i;

    if (flag == NULL) {
        return NULL;
    }
    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (table[i].id == id && strcmp(table[i].flag, flag) == 0) {
            return table[i].command;
        }
    }
    return NULL;
}

static const char *section_command(int32_t field)
{
    int get = field >= 0;
    int64_t index = get ? field : -(int64_t)field - 1;

    switch (index) {
        case 0: return get ? "SectionGetText" : "SectionSetText";
        case 1: return get ? "SectionGetInstTypes" : "SectionSetInstTypes";
        case 2: return get ? "SectionGetFlags" : "SectionSetFlags";
        case 5: return get ? "SectionGetSize" : "SectionSetSize";
        default: return NULL;
    }
}

static const char *writereg_command(int32_t type, int32_t value_type)
{
    switch (type) {
        case 1: /* REG_SZ */ return value_type == 2 ? "WriteRegExpandStr" : "WriteRegStr";
        case 4: /* REG_DWORD */ return "WriteRegDWORD";
        case REG_BINARY_TYPE:
            if (value_type == 0) {
                return "WriteRegNone";
            }
            return value_type == 7 ? "WriteRegMultiStr" : "WriteRegBin";
        default: return NULL;
    }
}

const char *xxbyte_nsis_command_name(const xxbyte_instruction *instruction)
{
    static const char *const simple[XXBYTE_NSIS_ID_COUNT] = {
        [XXBYTE_NSIS_ID_EW_RET] = "Return",
        [XXBYTE_NSIS_ID_EW_ABORT] = "Abort",
        [XXBYTE_NSIS_ID_EW_QUIT] = "Quit",
        [XXBYTE_NSIS_ID_EW_CALL] = "Call",
        [XXBYTE_NSIS_ID_EW_SLEEP] = "Sleep",
        [XXBYTE_NSIS_ID_EW_BRINGTOFRONT] = "BringToFront",
        [XXBYTE_NSIS_ID_EW_CHDETAILSVIEW] = "SetDetailsView",
        [XXBYTE_NSIS_ID_EW_SETFILEATTRIBUTES] = "SetFileAttributes",
        [XXBYTE_NSIS_ID_EW_IFFILEEXISTS] = "IfFileExists",
        [XXBYTE_NSIS_ID_EW_RENAME] = "Rename",
        [XXBYTE_NSIS_ID_EW_GETFULLPATHNAME] = "GetFullPathName",
        [XXBYTE_NSIS_ID_EW_SEARCHPATH] = "SearchPath",
        [XXBYTE_NSIS_ID_EW_GETTEMPFILENAME] = "GetTempFileName",
        [XXBYTE_NSIS_ID_EW_EXTRACTFILE] = "File",
        [XXBYTE_NSIS_ID_EW_DELETEFILE] = "Delete",
        [XXBYTE_NSIS_ID_EW_MESSAGEBOX] = "MessageBox",
        [XXBYTE_NSIS_ID_EW_RMDIR] = "RMDir",
        [XXBYTE_NSIS_ID_EW_STRLEN] = "StrLen",
        [XXBYTE_NSIS_ID_EW_ASSIGNVAR] = "StrCpy",
        [XXBYTE_NSIS_ID_EW_INTOP] = "IntOp",
        [XXBYTE_NSIS_ID_EW_FINDWINDOW] = "FindWindow",
        [XXBYTE_NSIS_ID_EW_SENDMESSAGE] = "SendMessage",
        [XXBYTE_NSIS_ID_EW_ISWINDOW] = "IsWindow",
        [XXBYTE_NSIS_ID_EW_GETDLGITEM] = "GetDlgItem",
        [XXBYTE_NSIS_ID_EW_SETCTLCOLORS] = "SetCtlColors",
        [XXBYTE_NSIS_ID_EW_CREATEFONT] = "CreateFont",
        [XXBYTE_NSIS_ID_EW_GETFILETIME] = "GetFileTime",
        [XXBYTE_NSIS_ID_EW_GETDLLVERSION] = "GetDLLVersion",
        [XXBYTE_NSIS_ID_EW_CREATESHORTCUT] = "CreateShortCut",
        [XXBYTE_NSIS_ID_EW_COPYFILES] = "CopyFiles",
        [XXBYTE_NSIS_ID_EW_REBOOT] = "Reboot",
        [XXBYTE_NSIS_ID_EW_READINISTR] = "ReadINIStr",
        [XXBYTE_NSIS_ID_EW_FCLOSE] = "FileClose",
        [XXBYTE_NSIS_ID_EW_FOPEN] = "FileOpen",
        [XXBYTE_NSIS_ID_EW_FSEEK] = "FileSeek",
        [XXBYTE_NSIS_ID_EW_FINDCLOSE] = "FindClose",
        [XXBYTE_NSIS_ID_EW_FINDNEXT] = "FindNext",
        [XXBYTE_NSIS_ID_EW_FINDFIRST] = "FindFirst",
        [XXBYTE_NSIS_ID_EW_WRITEUNINSTALLER] = "WriteUninstaller",
        [XXBYTE_NSIS_ID_EW_LOCKWINDOW] = "LockWindow",
        [XXBYTE_NSIS_ID_EW_SETBRANDINGIMAGE] = "SetBrandingImage",
        [XXBYTE_NSIS_ID_EW_GETFONTVERSION] = "GetFontVersion",
        [XXBYTE_NSIS_ID_EW_GETFONTNAME] = "GetFontName",
        [XXBYTE_NSIS_ID_EW_FINDPROC] = "FindProc",
    };
    int32_t p[PARAM_COUNT];
    uint32_t variant;
    uint32_t id;
    uint32_t i;

    if (instruction == NULL || instruction->family != XXBYTE_FAMILY_NSIS || instruction->code == NULL || instruction->size != ENTRY_SIZE ||
        !xxbyte_nsis_mode_valid(instruction->mode)) {
        return NULL;
    }
    id = instruction->id;
    variant = XXBYTE_NSIS_MODE_VARIANT(instruction->mode);
    if (id == 0 || id >= XXBYTE_NSIS_ID_COUNT) {
        return NULL;
    }
    if (simple[id] != NULL) {
        return simple[id];
    }
    for (i = 0; i < PARAM_COUNT; ++i) {
        p[i] = entry_param(instruction, i);
    }
    switch (id) {
        case XXBYTE_NSIS_ID_EW_NOP: return p[0] != 0 ? "Goto" : "Nop";
        case XXBYTE_NSIS_ID_EW_UPDATETEXT:
            /* Before 2.31 SetDetailsPrint sets parameter 1 (or 2 for
             * lastused) instead of the status_update flag. */
            if (!is_nsis3(variant) && !is_park(variant) && (p[1] != 0 || (variant == XXBYTE_NSIS_V226 && p[2] != 0))) {
                return "SetDetailsPrint";
            }
            return "DetailPrint";
        case XXBYTE_NSIS_ID_EW_CREATEDIR: return p[1] != 0 ? "SetOutPath" : "CreateDirectory";
        case XXBYTE_NSIS_ID_EW_SETFLAG: return p[0] < 0 ? NULL : flag_command(id, xxbyte_nsis_flag_name(instruction->mode, (uint32_t)p[0]));
        case XXBYTE_NSIS_ID_EW_IFFLAG: return p[2] < 0 ? NULL : flag_command(id, xxbyte_nsis_flag_name(instruction->mode, (uint32_t)p[2]));
        case XXBYTE_NSIS_ID_EW_GETFLAG: return p[1] < 0 ? NULL : flag_command(id, xxbyte_nsis_flag_name(instruction->mode, (uint32_t)p[1]));
        case XXBYTE_NSIS_ID_EW_STRCMP: return p[4] != 0 ? "StrCmpS" : "StrCmp";
        case XXBYTE_NSIS_ID_EW_READENVSTR: return p[2] != 0 ? "ReadEnvStr" : "ExpandEnvStrings";
        case XXBYTE_NSIS_ID_EW_INTCMP:
            if ((p[5] & 0x8000) != 0) {
                return (p[5] & 1) != 0 ? "Int64CmpU" : "Int64Cmp";
            }
            return (p[5] & 1) != 0 ? "IntCmpU" : "IntCmp";
        case XXBYTE_NSIS_ID_EW_INTFMT: return p[3] != 0 && is_nsis3(variant) ? "Int64Fmt" : "IntFmt";
        case XXBYTE_NSIS_ID_EW_PUSHPOP:
            if (p[2] != 0) {
                return "Exch";
            }
            return p[1] != 0 ? "Pop" : "Push";
        case XXBYTE_NSIS_ID_EW_LOADANDSETIMAGE:
            if (variant == XXBYTE_NSIS_V305) {
                return ((uint32_t)p[2] & LASIF_HWND) != 0 ? "LoadAndSetImage" : "SetBrandingImage";
            }
            return ((uint32_t)p[3] & LASIF_HWND) != 0 ? "LoadAndSetImage" : "SetBrandingImage";
        case XXBYTE_NSIS_ID_EW_SHOWWINDOW:
            if (p[3] != 0) {
                return "EnableWindow";
            }
            return p[2] != 0 ? "HideWindow" : "ShowWindow";
        case XXBYTE_NSIS_ID_EW_SHELLEXEC:
            /* SEE_MASK_NOCLOSEPROCESS */
            return is_nsis3(variant) && (p[4] & 0x40) != 0 ? "ExecShellWait" : "ExecShell";
        case XXBYTE_NSIS_ID_EW_EXECUTE: return p[2] != 0 ? "ExecWait" : "Exec";
        case XXBYTE_NSIS_ID_EW_REGISTERDLL: return p[2] != 0 ? "RegDLL/UnRegDLL" : "CallInstDLL";
        case XXBYTE_NSIS_ID_EW_WRITEINI:
            if (p[4] != 0) {
                return "WriteINIStr";
            }
            if (p[0] == 0) {
                return "FlushINI";
            }
            return p[1] == 0 ? "DeleteINISec" : "DeleteINIStr";
        case XXBYTE_NSIS_ID_EW_DELREG:
            if (variant == XXBYTE_NSIS_V200) {
                return p[3] != 0 ? "DeleteRegKey" : "DeleteRegValue";
            }
            return p[4] != 0 ? "DeleteRegKey" : "DeleteRegValue";
        case XXBYTE_NSIS_ID_EW_WRITEREG: return writereg_command(p[4], p[5]);
        case XXBYTE_NSIS_ID_EW_READREGSTR: return p[4] != 0 ? "ReadRegDWORD" : "ReadRegStr";
        case XXBYTE_NSIS_ID_EW_REGENUM: return p[4] != 0 ? "EnumRegKey" : "EnumRegValue";
        case XXBYTE_NSIS_ID_EW_FPUTS: return p[2] != 0 ? "FileWriteByte" : "FileWrite";
        case XXBYTE_NSIS_ID_EW_FGETS: return p[3] != 0 ? "FileReadByte" : "FileRead";
        case XXBYTE_NSIS_ID_EW_FPUTWS: return p[2] != 0 ? "FileWriteWord" : "FileWriteUTF16LE";
        case XXBYTE_NSIS_ID_EW_FGETWS: return p[3] != 0 ? "FileReadWord" : "FileReadUTF16LE";
        case XXBYTE_NSIS_ID_EW_LOG: return p[0] != 0 ? "LogSet" : "LogText";
        case XXBYTE_NSIS_ID_EW_SECTIONSET: return section_command(p[2]);
        case XXBYTE_NSIS_ID_EW_INSTTYPESET:
            if (p[3] != 0) {
                return p[2] != 0 ? "SetCurInstType" : "GetCurInstType";
            }
            return p[2] != 0 ? "InstTypeSetText" : "InstTypeGetText";
        case XXBYTE_NSIS_ID_EW_GETOSINFO:
            if (p[3] == 0) {
                return "GetKnownFolderPath";
            }
            return p[3] == 1 ? "ReadMemory/GetWinVer" : NULL;
        default: return NULL;
    }
}

#else
typedef int xxbyte_nsis_disabled;
#endif
