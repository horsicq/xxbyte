/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#include "xxbyte_installshield_internal.h"

#if XXBYTE_USE_INSTALLSHIELD

/* INX opcode classes. */
enum {
    K_PLAIN = 0, /* u16 argc, arguments */
    K_STRUCT,    /* u16 typedef, one argument */
    K_CALL,      /* u16 prototype, u16 argc, arguments */
    K_FUNCTION,  /* u16 argc, arguments, type-7 i32 offset */
    K_ENDFUNC,   /* u16 argc, arguments, declaration list */
    K_IF,        /* argument 0 is a jump */
    K_GOTO,      /* argument 0 is a jump */
    K_TRY,       /* arguments 0 and 1 are jumps */
    K_BIND       /* argument 1 is a prototype number */
};

#define G_STOP_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_RET (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)

typedef struct inx_entry {
    const char *name;
    uint8_t kind;
    uint16_t group;
} inx_entry;

static const inx_entry inx_table[XXBYTE_INSTALLSHIELD_OP_ADDRSTR + 1u] = {
    [0x01] = {"nop", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x02] = {"abort", K_PLAIN, G_THROW},
    [0x03] = {"exit", K_PLAIN, XXBYTE_GROUP_STOP},
    [0x04] = {"if", K_IF, XXBYTE_GROUP_CONDITIONAL},
    [0x05] = {"goto", K_GOTO, G_STOP_JUMP},
    [0x06] = {"assign", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x07] = {"add", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x08] = {"mod", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x09] = {"lt", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x0A] = {"gt", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x0B] = {"le", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x0C] = {"ge", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x0D] = {"eq", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x0E] = {"ne", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x0F] = {"sub", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x10] = {"mul", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x11] = {"div", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x12] = {"and", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x13] = {"or", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x14] = {"xor", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x15] = {"bnot", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x16] = {"shl", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x17] = {"shr", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x18] = {"lor", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x19] = {"land", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x1A] = {"addr", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x1B] = {"deref", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x1C] = {"struct", K_STRUCT, XXBYTE_GROUP_NONE},
    [0x1D] = {"setbyte", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x1E] = {"getbyte", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x20] = {"calldll", K_CALL, XXBYTE_GROUP_CALL},
    [0x21] = {"call", K_CALL, XXBYTE_GROUP_CALL},
    [0x22] = {"function", K_FUNCTION, XXBYTE_GROUP_NONE},
    [0x23] = {"return", K_PLAIN, G_RET},
    [0x24] = {"return2", K_PLAIN, G_RET},
    [0x25] = {"return3", K_PLAIN, G_RET},
    [0x26] = {"endfunc", K_ENDFUNC, G_RET},
    [0x27] = {"nop2", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x28] = {"strlengthchars", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x29] = {"strsub", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x2A] = {"strfind", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x2B] = {"strcompare", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x2C] = {"strtonum", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x2D] = {"numtostr", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x2E] = {"handler", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x2F] = {"handlerex", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x30] = {"dohandler", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x31] = {"resize", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x32] = {"sizeof", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x33] = {"propput", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x34] = {"propputref", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x35] = {"propget", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x36] = {"try", K_TRY, XXBYTE_GROUP_NONE},
    [0x37] = {"endtry", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x38] = {"endcatch", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x39] = {"usedll", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x3A] = {"unusedll", K_PLAIN, XXBYTE_GROUP_NONE},
    [0x3B] = {"bindvariable", K_BIND, XXBYTE_GROUP_NONE},
    [0x3C] = {"addrstr", K_PLAIN, XXBYTE_GROUP_NONE},
};

/* INS opcodes: argument count in the low nibble (the compiler table's flag
 * bits above it are kept but unused) and mnemonic; NULL is undefined. */
typedef struct ins_entry {
    uint8_t argc;
    const char *name;
} ins_entry;

static const ins_entry ins_table[XXBYTE_INSTALLSHIELD_INS_OP_MAX + 1u] = {
    /* 0x000 */ {0x00u, "block"},
    /* 0x001 */ {0x01u, "enable"},
    /* 0x002 */ {0x01u, "disable"},
    /* 0x003 */ {0x02u, "setcolor"},
    /* 0x004 */ {0x03u, "settitle"},
    /* 0x005 */ {0x01u, "getdiskspace"},
    /* 0x006 */ {0x00u, "getmemfree"},
    /* 0x007 */ {0x00u, "getmode"},
    /* 0x008 */ {0x02u, "setstatuswindow"},
    /* 0x009 */ {0x02u, "setinfowindow"},
    /* 0x00A */ {0x01u, "delay"},
    /* 0x00B */ {0x01u, "existsdisk"},
    /* 0x00C */ {0x01u, "existsdir"},
    /* 0x00D */ {0x43u, "askpath"},
    /* 0x00E */ {0x02u, "getdisk"},
    /* 0x00F */ {0x02u, "getdir"},
    /* 0x010 */ {0x81u, "createdir"},
    /* 0x011 */ {0x83u, "createfile"},
    /* 0x012 */ {0x82u, "writeline"},
    /* 0x013 */ {0x02u, "strcopy"},
    /* 0x014 */ {0x02u, "strconcat"},
    /* 0x015 */ {0x01u, "op_015"},
    /* 0x016 */ {0x02u, "op_016"},
    /* 0x017 */ {0x01u, "createproggroup"},
    /* 0x018 */ {0x04u, "addprogitem"},
    /* 0x019 */ {0x03u, "op_019"},
    /* 0x01A */ {0x02u, "enterdisk"},
    /* 0x01B */ {0xFFu, NULL},
    /* 0x01C */ {0x82u, "copyfile"},
    /* 0x01D */ {0x01u, "deletefile"},
    /* 0x01E */ {0x03u, "asktext"},
    /* 0x01F */ {0x81u, "closefile"},
    /* 0x020 */ {0xC0u, "askoptions"},
    /* 0x021 */ {0x02u, "assign"},
    /* 0x022 */ {0x04u, "if"},
    /* 0x023 */ {0x02u, "strcompare"},
    /* 0x024 */ {0x03u, "openfile"},
    /* 0x025 */ {0x84u, "writeprofstring"},
    /* 0x026 */ {0x04u, "getprofstring"},
    /* 0x027 */ {0x04u, "getprofint"},
    /* 0x028 */ {0x22u, "askyesno"},
    /* 0x029 */ {0x03u, "findfile"},
    /* 0x02A */ {0x02u, "messagebox"},
    /* 0x02B */ {0x00u, "exit"},
    /* 0x02C */ {0x01u, "goto"},
    /* 0x02D */ {0xFFu, NULL},
    /* 0x02E */ {0x02u, "appendtopath"},
    /* 0x02F */ {0x01u, "strlength"},
    /* 0x030 */ {0x04u, "strsub"},
    /* 0x031 */ {0x02u, "strfind"},
    /* 0x032 */ {0x02u, "add2"},
    /* 0x033 */ {0x62u, "launchapp"},
    /* 0x034 */ {0x02u, "executedll"},
    /* 0x035 */ {0x02u, "getextents"},
    /* 0x036 */ {0x05u, "placebitmap"},
    /* 0x037 */ {0x02u, "getline"},
    /* 0x038 */ {0x02u, "exportnum"},
    /* 0x039 */ {0x02u, "exportstr"},
    /* 0x03A */ {0x01u, "messagebeep"},
    /* 0x03B */ {0x02u, "setdialogtitle"},
    /* 0x03C */ {0x03u, "setfont"},
    /* 0x03D */ {0x02u, "seterrormsg"},
    /* 0x03E */ {0x02u, "seterrortitle"},
    /* 0x03F */ {0x02u, "deletedir"},
    /* 0x040 */ {0x04u, "findallfiles"},
    /* 0x041 */ {0x02u, "getenvvar"},
    /* 0x042 */ {0x02u, "suppress"},
    /* 0x043 */ {0x01u, "gosub"},
    /* 0x044 */ {0x00u, "retsub"},
    /* 0x045 */ {0x04u, "ezbatchaddpath"},
    /* 0x046 */ {0x03u, "ezbatchaddstring"},
    /* 0x047 */ {0x01u, "batchgetfilename"},
    /* 0x048 */ {0x01u, "batchfileload"},
    /* 0x049 */ {0x03u, "batchfind"},
    /* 0x04A */ {0x03u, "batchmove"},
    /* 0x04B */ {0x04u, "batchadd"},
    /* 0x04C */ {0x01u, "batchdelete"},
    /* 0x04D */ {0x01u, "batchfilesave"},
    /* 0x04E */ {0x03u, "ezconfigadddriver"},
    /* 0x04F */ {0x03u, "ezconfigaddstring"},
    /* 0x050 */ {0x02u, "ezconfiggetvalue"},
    /* 0x051 */ {0x02u, "ezconfigsetvalue"},
    /* 0x052 */ {0x01u, "configgetfilename"},
    /* 0x053 */ {0x01u, "configfileload"},
    /* 0x054 */ {0x03u, "configfind"},
    /* 0x055 */ {0x03u, "configmove"},
    /* 0x056 */ {0x04u, "configadd"},
    /* 0x057 */ {0x01u, "configdelete"},
    /* 0x058 */ {0x02u, "configgetint"},
    /* 0x059 */ {0x02u, "configsetint"},
    /* 0x05A */ {0x01u, "configfilesave"},
    /* 0x05B */ {0x01u, "pathset"},
    /* 0x05C */ {0x01u, "pathget"},
    /* 0x05D */ {0x04u, "pathadd"},
    /* 0x05E */ {0x05u, "pathmove"},
    /* 0x05F */ {0x04u, "pathfind"},
    /* 0x060 */ {0x02u, "pathdelete"},
    /* 0x061 */ {0x03u, "getsysteminfo"},
    /* 0x062 */ {0x01u, "batchsetfilename"},
    /* 0x063 */ {0x01u, "configsetfilename"},
    /* 0x064 */ {0x02u, "statusupdate"},
    /* 0x065 */ {0x01u, "openfilemode"},
    /* 0x066 */ {0x01u, "deletegroup"},
    /* 0x067 */ {0x02u, "showgroup"},
    /* 0x068 */ {0x04u, "placewindow"},
    /* 0x069 */ {0x03u, "sizewindow"},
    /* 0x06A */ {0x03u, "seekbytes"},
    /* 0x06B */ {0x04u, "readbytes"},
    /* 0x06C */ {0x84u, "writebytes"},
    /* 0x06D */ {0x02u, "strtonum"},
    /* 0x06E */ {0x02u, "numtostr"},
    /* 0x06F */ {0x01u, "system"},
    /* 0x070 */ {0x02u, "op_070"},
    /* 0x071 */ {0x01u, "op_071"},
    /* 0x072 */ {0x02u, "getgroupnames"},
    /* 0x073 */ {0x02u, "findwindow"},
    /* 0x074 */ {0x04u, "sendmessage"},
    /* 0x075 */ {0x02u, "sub2"},
    /* 0x076 */ {0x02u, "mul2"},
    /* 0x077 */ {0x02u, "div2"},
    /* 0x078 */ {0x02u, "renamefile"},
    /* 0x079 */ {0x05u, "copybytes"},
    /* 0x07A */ {0x03u, "getbyte"},
    /* 0x07B */ {0x03u, "setbyte"},
    /* 0x07C */ {0x04u, "getfileinfo"},
    /* 0x07D */ {0x04u, "setfileinfo"},
    /* 0x07E */ {0x01u, "bitnot"},
    /* 0x07F */ {0x02u, "bitand"},
    /* 0x080 */ {0x02u, "bitor"},
    /* 0x081 */ {0x02u, "appcommand"},
    /* 0x082 */ {0x01u, "changedirectory"},
    /* 0x083 */ {0x01u, "ezbatchreplace"},
    /* 0x084 */ {0x02u, "handler"},
    /* 0x085 */ {0x01u, "regdbcreatekey"},
    /* 0x086 */ {0x02u, "regdbcreatekeyvalue"},
    /* 0x087 */ {0x01u, "regdbdeletekey"},
    /* 0x088 */ {0x01u, "regdbkeyexist"},
    /* 0x089 */ {0x02u, "regdbgetkeyvalue"},
    /* 0x08A */ {0x02u, "regdbsetkeyvalue"},
    /* 0x08B */ {0x08u, "definedialog"},
    /* 0x08C */ {0x41u, "waitondialog"},
    /* 0x08D */ {0x01u, "enddialog"},
    /* 0x08E */ {0x01u, "releasedialog"},
    /* 0x08F */ {0x02u, "ctrlgetstate"},
    /* 0x090 */ {0x03u, "ctrlsetstate"},
    /* 0x091 */ {0x03u, "ctrlsettext"},
    /* 0x092 */ {0x03u, "ctrlgettext"},
    /* 0x093 */ {0x02u, "ctrlselecttext"},
    /* 0x094 */ {0x03u, "ctrlgetcursel"},
    /* 0x095 */ {0x03u, "ctrlsetcursel"},
    /* 0x096 */ {0x02u, "ctrlclear"},
    /* 0x097 */ {0x04u, "ctrldir"},
    /* 0x098 */ {0x02u, "ctrlpgroups"},
    /* 0x099 */ {0x03u, "ctrlsetlist"},
    /* 0x09A */ {0x01u, "cmdgethwnddlg"},
    /* 0x09B */ {0x01u, "cmdgetmsg"},
    /* 0x09C */ {0x01u, "cmdgetparam1"},
    /* 0x09D */ {0x01u, "cmdgetparam2"},
    /* 0x09E */ {0x01u, "ctrlgetsubcommand"},
    /* 0x09F */ {0x01u, "listcreate"},
    /* 0x0A0 */ {0x01u, "listdestroy"},
    /* 0x0A1 */ {0x03u, "listaddstring"},
    /* 0x0A2 */ {0x02u, "listfindstring"},
    /* 0x0A3 */ {0x03u, "listadditem"},
    /* 0x0A4 */ {0x02u, "listgetfirststring"},
    /* 0x0A5 */ {0x02u, "listgetnextstring"},
    /* 0x0A6 */ {0x02u, "listgetfirstitem"},
    /* 0x0A7 */ {0x02u, "listgetnextitem"},
    /* 0x0A8 */ {0x01u, "listdeletestring"},
    /* 0x0A9 */ {0x02u, "listsetcurrentstring"},
    /* 0x0AA */ {0x02u, "listsetcurrentitem"},
    /* 0x0AB */ {0x02u, "vergetfileversion"},
    /* 0x0AC */ {0x03u, "verfindfileversion"},
    /* 0x0AD */ {0x03u, "versearchandupdatefile"},
    /* 0x0AE */ {0x03u, "verupdatefile"},
    /* 0x0AF */ {0xFFu, NULL},
    /* 0x0B0 */ {0xFFu, NULL},
    /* 0x0B1 */ {0xFFu, NULL},
    /* 0x0B2 */ {0x01u, "usedll"},
    /* 0x0B3 */ {0x01u, "unusedll"},
    /* 0x0B4 */ {0xF0u, "calldll"},
    /* 0x0B5 */ {0xF0u, "call"},
    /* 0x0B6 */ {0xF0u, "function"},
    /* 0x0B7 */ {0x01u, "return"},
    /* 0x0B8 */ {0x00u, "endfunc"},
    /* 0x0B9 */ {0x01u, "addressnumber"},
    /* 0x0BA */ {0x01u, "addressstring"},
    /* 0x0BB */ {0x01u, "addressstruct"},
    /* 0x0BC */ {0x05u, "structget"},
    /* 0x0BD */ {0x05u, "structgetp"},
    /* 0x0BE */ {0x05u, "structput"},
    /* 0x0BF */ {0x05u, "structputp"},
    /* 0x0C0 */ {0xFFu, NULL},
    /* 0x0C1 */ {0xFFu, NULL},
    /* 0x0C2 */ {0xFFu, NULL},
    /* 0x0C3 */ {0xFFu, NULL},
    /* 0x0C4 */ {0xFFu, NULL},
    /* 0x0C5 */ {0xFFu, NULL},
    /* 0x0C6 */ {0x01u, "getwindowhandle"},
    /* 0x0C7 */ {0x02u, "createproggroupex"},
    /* 0x0C8 */ {0x01u, "getgroupnamelist"},
    /* 0x0C9 */ {0x08u, "addprogitemex"},
    /* 0x0CA */ {0x02u, "getitemnamelist"},
    /* 0x0CB */ {0x03u, "deleteprogitem"},
    /* 0x0CC */ {0x08u, "queryprogitem"},
    /* 0x0CD */ {0x03u, "queryproggroup"},
    /* 0x0CE */ {0x01u, "queryshellmgr"},
    /* 0x0CF */ {0x01u, "reloadproggroup"},
    /* 0x0D0 */ {0x09u, "replaceprogitem"},
    /* 0x0D1 */ {0x01u, "exitprogman"},
    /* 0x0D2 */ {0x41u, "filesetbegindefine"},
    /* 0x0D3 */ {0x01u, "filesetenddefine"},
    /* 0x0D4 */ {0x02u, "filesetreset"},
    /* 0x0D5 */ {0x82u, "filesetperform"},
    /* 0x0D6 */ {0x01u, "filesetendperform"},
    /* 0x0D7 */ {0x04u, "batchmoveex"},
    /* 0x0D8 */ {0x02u, "batchdeleteex"},
    /* 0x0D9 */ {0x03u, "parsepath"},
    /* 0x0DA */ {0x02u, "strtolower"},
    /* 0x0DB */ {0x02u, "strtoupper"},
    /* 0x0DC */ {0x03u, "strgettokens"},
    /* 0x0DD */ {0x03u, "filecompare"},
    /* 0x0DE */ {0x05u, "filegrep"},
    /* 0x0DF */ {0x04u, "fileinsertline"},
    /* 0x0E0 */ {0x03u, "filedeleteline"},
    /* 0x0E1 */ {0x03u, "getvaliddriveslist"},
    /* 0x0E2 */ {0x02u, "is"},
    /* 0x0E3 */ {0x43u, "launchappandwait"},
    /* 0x0E4 */ {0x03u, "doinstall"},
    /* 0x0E5 */ {0x02u, "copyfileex"},
    /* 0x0E6 */ {0x04u, "split"},
    /* 0x0E7 */ {0x05u, "op_0e7"},
    /* 0x0E8 */ {0x03u, "op_0e8"},
    /* 0x0E9 */ {0x02u, "op_0e9"},
    /* 0x0EA */ {0x03u, "statussetline"},
    /* 0x0EB */ {0x05u, "statusgetsize"},
    /* 0x0EC */ {0x02u, "op_0ec"},
    /* 0x0ED */ {0x01u, "op_0ed"},
    /* 0x0EE */ {0x84u, "addprofstring"},
    /* 0x0EF */ {0x85u, "replaceprofstring"},
    /* 0x0F0 */ {0x03u, "ctrlgetmletext"},
    /* 0x0F1 */ {0x03u, "ctrlsetmletext"},
    /* 0x0F2 */ {0x03u, "ctrlgetmultcursel"},
    /* 0x0F3 */ {0x01u, "structsizeof"},
    /* 0x0F4 */ {0x02u, "listcurrentstring"},
    /* 0x0F5 */ {0x02u, "listcurrentitem"},
    /* 0x0F6 */ {0x01u, "listdeleteitem"},
    /* 0x0F7 */ {0x02u, "listfinditem"},
    /* 0x0F8 */ {0x01u, "listcount"},
    /* 0x0F9 */ {0x04u, "ctrlsetmultcursel"},
    /* 0x0FA */ {0x83u, "xcopyfile"},
    /* 0x0FB */ {0x02u, "op_0fb"},
    /* 0x0FC */ {0x02u, "filesetroot"},
    /* 0x0FD */ {0x02u, "op_0fd"},
    /* 0x0FE */ {0x02u, "op_0fe"},
    /* 0x0FF */ {0x01u, "varsave"},
    /* 0x100 */ {0x01u, "varrestore"},
    /* 0x101 */ {0x03u, "findalldirs"},
    /* 0x102 */ {0x03u, "vercompare"},
    /* 0x103 */ {0xF0u, "sprintf"},
    /* 0x104 */ {0xF0u, "sprintfbox"},
    /* 0x105 */ {0x03u, "getfont"},
    /* 0x106 */ {0x03u, "ctrlsetfont"},
    /* 0x107 */ {0x05u, "op_107"},
    /* 0x108 */ {0x01u, "op_108"},
    /* 0x109 */ {0x01u, "op_109"},
    /* 0x10A */ {0x02u, "op_10a"},
    /* 0x10B */ {0x01u, "op_10b"},
    /* 0x10C */ {0x02u, "op_10c"},
    /* 0x10D */ {0x01u, "op_10d"},
    /* 0x10E */ {0x03u, "op_10e"},
    /* 0x10F */ {0x03u, "op_10f"},
    /* 0x110 */ {0x01u, "regdbsetdefaultroot"},
    /* 0x111 */ {0x01u, "progdefgrouptype"},
    /* 0x112 */ {0x03u, "loadstringconst"},
    /* 0x113 */ {0x04u, "ezdefinedialog"},
    /* 0x114 */ {0x01u, "language"},
    /* 0x115 */ {0x02u, "listwritetofile"},
    /* 0x116 */ {0x02u, "listreadfromfile"},
    /* 0x117 */ {0x03u, "listgetfileresource"},
    /* 0x118 */ {0x02u, "languageinfo"},
    /* 0x119 */ {0x03u, "add"},
    /* 0x11A */ {0x03u, "sub"},
    /* 0x11B */ {0x03u, "mul"},
    /* 0x11C */ {0x03u, "div"},
    /* 0x11D */ {0x03u, "and"},
    /* 0x11E */ {0x03u, "or"},
    /* 0x11F */ {0x03u, "xor"},
    /* 0x120 */ {0x03u, "bnot"},
    /* 0x121 */ {0x03u, "shl"},
    /* 0x122 */ {0x03u, "shr"},
    /* 0x123 */ {0x03u, "mod"},
    /* 0x124 */ {0x03u, "strcat"},
    /* 0x125 */ {0x03u, "pathcat"},
    /* 0x126 */ {0x03u, "lor"},
    /* 0x127 */ {0x03u, "land"},
    /* 0x128 */ {0x04u, "compare"},
    /* 0x129 */ {0x00u, "switch"},
    /* 0x12A */ {0x02u, "effects"},
    /* 0x12B */ {0x01u, "varloadfromfile"},
    /* 0x12C */ {0x01u, "varsavetofile"},
    /* 0x12D */ {0x02u, "varsavelisttofile"},
    /* 0x12E */ {0xF0u, "op_12e"},
    /* 0x12F */ {0x00u, "nop"},
    /* 0x130 */ {0x05u, "op_130"},
    /* 0x131 */ {0x05u, "op_131"},
    /* 0x132 */ {0x00u, "op_132"},
    /* 0x133 */ {0x00u, "op_133"},
    /* 0x134 */ {0x00u, "op_134"},
    /* 0x135 */ {0x00u, "op_135"},
    /* 0x136 */ {0x00u, "op_136"},
    /* 0x137 */ {0x44u, "askdestpath"},
    /* 0x138 */ {0x45u, "setuptype"},
    /* 0x139 */ {0x43u, "selectfolder"},
    /* 0x13A */ {0x08u, "addfoldericon"},
    /* 0x13B */ {0x44u, "calldllfx"},
    /* 0x13C */ {0x01u, "createprogramfolder"},
    /* 0x13D */ {0x01u, "deleteprogramfolder"},
    /* 0x13E */ {0x02u, "deletefoldericon"},
    /* 0x13F */ {0xC2u, "filesetperformez"},
    /* 0x140 */ {0x01u, "op_140"},
    /* 0x141 */ {0x04u, "regdbsetappinfo"},
    /* 0x142 */ {0x04u, "regdbgetappinfo"},
    /* 0x143 */ {0x09u, "replacefoldericon"},
    /* 0x144 */ {0x42u, "welcome"},
    /* 0x145 */ {0x04u, "deinstallstart"},
    /* 0x146 */ {0x04u, "installationinfo"},
    /* 0x147 */ {0x02u, "regdbsetitem"},
    /* 0x148 */ {0x02u, "regdbgetitem"},
    /* 0x149 */ {0x44u, "componentdialog"},
    /* 0x14A */ {0x04u, "componentadditem"},
    /* 0x14B */ {0x02u, "componentisitemselected"},
    /* 0x14C */ {0x03u, "componentselectitem"},
    /* 0x14D */ {0x43u, "rebootdialog"},
    /* 0x14E */ {0x02u, "showprogramfolder"},
    /* 0x14F */ {0x01u, "commitsharedfiles"},
    /* 0x150 */ {0x02u, "regdbcreatekeyex"},
    /* 0x151 */ {0x05u, "regdbsetkeyvalueex"},
    /* 0x152 */ {0x05u, "regdbgetkeyvalueex"},
    /* 0x153 */ {0x01u, "strremovelastslash"},
    /* 0x154 */ {0x41u, "do"},
    /* 0x155 */ {0x02u, "listsetindex"},
    /* 0x156 */ {0x03u, "componentlistitems"},
    /* 0x157 */ {0x03u, "componentgetitemsize"},
    /* 0x158 */ {0x44u, "selectdir"},
    /* 0x159 */ {0x00u, "abort"},
    /* 0x15A */ {0x02u, "longpathtoquote"},
    /* 0x15B */ {0x01u, "longpathtoshortpath"},
    /* 0x15C */ {0x01u, "longpathfromshortpath"},
    /* 0x15D */ {0x05u, "silentreaddata"},
    /* 0x15E */ {0x05u, "silentwritedata"},
    /* 0x15F */ {0x03u, "dialogsetinfo"},
    /* 0x160 */ {0x04u, "componentsetiteminfo"},
    /* 0x161 */ {0x04u, "componentgetiteminfo"},
    /* 0x162 */ {0x04u, "componenttotalsize"},
    /* 0x163 */ {0x07u, "componentlink"},
    /* 0x164 */ {0x03u, "regdbquerykey"},
    /* 0x165 */ {0x03u, "getfoldernamelist"},
    /* 0x166 */ {0x02u, "regdbdeletevalue"},
    /* 0x167 */ {0x03u, "op_167"},
    /* 0x168 */ {0x03u, "op_168"},
    /* 0x169 */ {0x00u, "op_169"},
    /* 0x16A */ {0x03u, "regdbconnectregistry"},
    /* 0x16B */ {0x01u, "regdbdisconnectregistry"},
    /* 0x16C */ {0x02u, "regdbmergeregfile"},
    /* 0x16D */ {0xFFu, NULL},
    /* 0x16E */ {0x02u, "getscriptfunctionaddress"},
    /* 0x16F */ {0x01u, "setdisplayeffect"},
    /* 0x170 */ {0x04u, "playmmedia"},
    /* 0x171 */ {0x01u, "mmediaisplaying"},
    /* 0x172 */ {0x03u, "componentcomparesizerequired"},
    /* 0x173 */ {0x05u, "componenterror"},
    /* 0x174 */ {0x05u, "componentfileenum"},
    /* 0x175 */ {0x06u, "componentfileinfo"},
    /* 0x176 */ {0x04u, "componentfilter"},
    /* 0x177 */ {0x05u, "componentgetdata"},
    /* 0x178 */ {0x02u, "componentinitialize"},
    /* 0x179 */ {0x83u, "componentmovedata"},
    /* 0x17A */ {0x82u, "componentmovedataez"},
    /* 0x17B */ {0x05u, "componentsetdata"},
    /* 0x17C */ {0x03u, "componenttextsubset"},
    /* 0x17D */ {0x03u, "componentvalidate"},
    /* 0x17E */ {0x01u, "indirectnumber"},
    /* 0x17F */ {0x05u, "structgetaddressex"},
    /* 0x180 */ {0x05u, "structgetpaddressex"},
    /* 0x181 */ {0x05u, "structgetex"},
    /* 0x182 */ {0x05u, "structgetpex"},
    /* 0x183 */ {0x05u, "structputex"},
    /* 0x184 */ {0x05u, "structputpex"},
    /* 0x185 */ {0x01u, "structsizeofex"},
    /* 0x186 */ {0x04u, "componenttextsubquery"},
    /* 0x187 */ {0x02u, "componenttextsubenum"},
    /* 0x188 */ {0x04u, "componenttextsubeval"},
    /* 0x189 */ {0x02u, "componentsetuptypeset"},
    /* 0x18A */ {0x02u, "componentsetuptypeenum"},
    /* 0x18B */ {0x02u, "componentviewcreate"},
    /* 0x18C */ {0x04u, "componentviewsetinfo"},
    /* 0x18D */ {0x04u, "componentviewqueryinfo"},
    /* 0x18E */ {0x02u, "componentviewrefresh"},
    /* 0x18F */ {0x01u, "componentviewdestroy"},
    /* 0x190 */ {0x01u, "componentviewcreatewindow"},
    /* 0x191 */ {0x02u, "componentviewselectall"},
    /* 0x192 */ {0x03u, "componentsettarget"},
    /* 0x193 */ {0x02u, "componentsetcurrent"},
    /* 0x194 */ {0x05u, "componentsetuptypegetdata"},
    /* 0x195 */ {0x01u, "deinstallsetreference"},
    /* 0x196 */ {0x04u, "componentfilteros"},
    /* 0x197 */ {0x03u, "componentfilterlanguage"},
};

static const char block_name[] = "block";

static xxbyte_status installshield_check_mode(uint32_t mode)
{
    return (mode == XXBYTE_INSTALLSHIELD_MODE_DEFAULT || mode == XXBYTE_INSTALLSHIELD_MODE_INX || mode == XXBYTE_INSTALLSHIELD_MODE_INS) ? XXBYTE_STATUS_OK
                                                                                                                                         : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static int is_ins(uint32_t mode)
{
    return mode == XXBYTE_INSTALLSHIELD_MODE_INS;
}

/* Exact block whose address is address; returns 1 and sets *index. */
static int find_block(const xxbyte_installshield_context *ctx, uint64_t address, uint32_t *index)
{
    uint32_t lo = 0;
    uint32_t hi;

    if (ctx == NULL || ctx->block_addresses == NULL || ctx->block_count == 0) {
        return 0;
    }
    hi = ctx->block_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if ((uint64_t)ctx->block_addresses[mid] < address) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo < ctx->block_count && (uint64_t)ctx->block_addresses[lo] == address) {
        *index = lo;
        return 1;
    }
    return 0;
}

/* Last block starting at or before address. */
static int containing_block(const xxbyte_installshield_context *ctx, uint64_t address, uint32_t *index)
{
    uint32_t lo = 0;
    uint32_t hi;

    if (ctx == NULL || ctx->block_addresses == NULL || ctx->block_count == 0) {
        return 0;
    }
    hi = ctx->block_count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if ((uint64_t)ctx->block_addresses[mid] <= address) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    if (lo == 0) {
        return 0;
    }
    *index = lo - 1u;
    return 1;
}

/* Size of the argument at code[pos]: 0 with *status set on failure. */
static size_t arg_size(uint32_t mode, const uint8_t *code, size_t size, size_t pos, xxbyte_status *status)
{
    uint8_t type;
    size_t need;

    if (pos >= size) {
        *status = XXBYTE_STATUS_TRUNCATED;
        return 0;
    }
    type = code[pos];
    if (is_ins(mode)) {
        switch (type & 0xF0u) {
            case 0x30:
            case 0x50: need = 3; break;
            case 0x40: need = (type & 1u) != 0 ? 5u : 3u; break;
            case 0x60:
                if ((type & 1u) == 0) {
                    need = 3;
                    break;
                }
                if (size - pos < 3) {
                    *status = XXBYTE_STATUS_TRUNCATED;
                    return 0;
                }
                need = 3u + xxbyte_u16le(code + pos + 1);
                break;
            default: *status = XXBYTE_STATUS_INVALID_INSTRUCTION; return 0;
        }
    } else {
        switch (type) {
            case 4:
            case 5:
            case 8: need = 3; break;
            case 7: need = 5; break;
            case 6:
                if (size - pos < 3) {
                    *status = XXBYTE_STATUS_TRUNCATED;
                    return 0;
                }
                need = 3u + xxbyte_u16le(code + pos + 1);
                break;
            default: *status = XXBYTE_STATUS_INVALID_INSTRUCTION; return 0;
        }
    }
    if (size - pos < need) {
        *status = XXBYTE_STATUS_TRUNCATED;
        return 0;
    }
    return need;
}

/* Variable bank of an argument type byte, 0 for literals. */
static uint16_t var_flag(uint32_t mode, uint8_t type)
{
    if (is_ins(mode)) {
        switch (type & 0xF0u) {
            case 0x30: return XXBYTE_INSTALLSHIELD_OPERAND_FLAG_NUMBER;
            case 0x40: return (type & 1u) != 0 ? 0 : XXBYTE_INSTALLSHIELD_OPERAND_FLAG_NUMBER;
            case 0x50: return XXBYTE_INSTALLSHIELD_OPERAND_FLAG_STRING;
            case 0x60: return (type & 1u) != 0 ? 0 : XXBYTE_INSTALLSHIELD_OPERAND_FLAG_STRING;
            default: return 0;
        }
    }
    switch (type) {
        case 4: return XXBYTE_INSTALLSHIELD_OPERAND_FLAG_STRING;
        case 5: return XXBYTE_INSTALLSHIELD_OPERAND_FLAG_NUMBER;
        case 8: return XXBYTE_INSTALLSHIELD_OPERAND_FLAG_VARIANT;
        default: return 0;
    }
}

static int is_number_literal(uint32_t mode, uint8_t type)
{
    return is_ins(mode) ? ((type & 0xF1u) == 0x41u) : type == 7;
}

/* The plain operand of an argument known to be well-formed at code[pos];
 * offsets are relative to code (the instruction's first byte). */
static void arg_operand(uint32_t mode, const uint8_t *code, size_t pos, xxbyte_operand *operand)
{
    uint8_t type = code[pos];
    uint16_t flag = var_flag(mode, type);

    memset(operand, 0, sizeof(*operand));
    if (flag != 0) {
        uint16_t raw = xxbyte_u16le(code + pos + 1);
        int64_t address = (int16_t)raw;
        operand->type = XXBYTE_OPERAND_INDEX;
        operand->size = 2;
        operand->flags = flag;
        operand->extra = raw;
        if (address <= -100) {
            operand->kind = XXBYTE_INDEX_LOCAL;
            operand->value = -(address + 100);
        } else {
            operand->kind = XXBYTE_INDEX_GLOBAL;
            operand->value = address;
            if (address < 0) {
                operand->flags |= XXBYTE_OPERAND_FLAG_SIGNED;
            }
        }
        return;
    }
    if (is_number_literal(mode, type)) {
        operand->type = XXBYTE_OPERAND_IMMEDIATE;
        operand->size = 4;
        operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
        operand->value = (int32_t)xxbyte_u32le(code + pos + 1);
        return;
    }
    /* string literal */
    operand->type = XXBYTE_OPERAND_BYTES;
    operand->flags = XXBYTE_INSTALLSHIELD_OPERAND_FLAG_STRING;
    operand->size = xxbyte_u16le(code + pos + 1);
    operand->value = (int64_t)(pos + 3u);
}

/* Operands before the arguments, by mode and opcode. */
static uint32_t fixed_operands(uint32_t mode, uint32_t opcode)
{
    if (is_ins(mode)) {
        switch (opcode) {
            case XXBYTE_INSTALLSHIELD_INS_OP_GOTO:
            case XXBYTE_INSTALLSHIELD_INS_OP_CALLDLL: return 1;
            case XXBYTE_INSTALLSHIELD_INS_OP_IF:
            case XXBYTE_INSTALLSHIELD_INS_OP_CALL: return 2;
            default: return 0;
        }
    }
    switch (opcode) {
        case XXBYTE_INSTALLSHIELD_OP_STRUCT:
        case XXBYTE_INSTALLSHIELD_OP_CALLDLL:
        case XXBYTE_INSTALLSHIELD_OP_CALL: return 1;
        default: return 0;
    }
}

/* An INX jump argument: BRANCH when the block table resolves it. */
static void jump_operand(const xxbyte_installshield_context *ctx, uint64_t address, xxbyte_operand *operand)
{
    int64_t delta = operand->value;
    uint32_t block;

    operand->type = XXBYTE_OPERAND_INDEX;
    operand->kind = XXBYTE_INSTALLSHIELD_INDEX_BLOCK_DELTA;
    operand->flags = XXBYTE_OPERAND_FLAG_SIGNED;
    operand->extra = 0;
    if (containing_block(ctx, address, &block)) {
        int64_t target = (int64_t)block + delta;
        if (target >= 0 && target < (int64_t)ctx->block_count) {
            operand->type = XXBYTE_OPERAND_BRANCH;
            operand->kind = 0;
            operand->flags = 0;
            operand->value = (int64_t)ctx->block_addresses[target];
            operand->extra = (uint64_t)target;
        }
    }
}

/* An INS label number: BRANCH when the block table has it. */
static void label_operand(const xxbyte_installshield_context *ctx, uint32_t label, xxbyte_operand *operand)
{
    memset(operand, 0, sizeof(*operand));
    operand->size = 2;
    if (ctx != NULL && ctx->block_addresses != NULL && label < ctx->block_count) {
        operand->type = XXBYTE_OPERAND_BRANCH;
        operand->value = (int64_t)ctx->block_addresses[label];
        operand->extra = label;
    } else {
        operand->type = XXBYTE_OPERAND_INDEX;
        operand->kind = XXBYTE_INSTALLSHIELD_INDEX_BLOCK;
        operand->value = label;
    }
}

/* Applies the opcode's special meaning to argument index of an INX
 * instruction. */
static void special_argument(uint32_t mode, uint32_t opcode, uint32_t index, uint8_t type, const xxbyte_installshield_context *ctx, uint64_t address,
                             xxbyte_operand *operand)
{
    uint8_t kind;

    if (is_ins(mode) || opcode > XXBYTE_INSTALLSHIELD_OP_ADDRSTR || type != 7) {
        return;
    }
    kind = inx_table[opcode].kind;
    if ((kind == K_IF && index == 0) || (kind == K_GOTO && index == 0) || (kind == K_TRY && index < 2)) {
        jump_operand(ctx, address, operand);
    } else if (kind == K_BIND && index == 1) {
        operand->type = XXBYTE_OPERAND_INDEX;
        operand->kind = XXBYTE_INDEX_FUNCTION;
        operand->flags = operand->value < 0 ? XXBYTE_OPERAND_FLAG_SIGNED : 0;
    }
}

static xxbyte_operand *add(xxbyte_instruction *instruction, xxbyte_operand_type type, uint32_t size, int64_t value)
{
    return xxbyte_add_operand(instruction, type, size, value);
}

/* Reads argc arguments from code[*pos]; stores those that fit. */
static xxbyte_status read_arguments(uint32_t mode, const xxbyte_installshield_context *ctx, const uint8_t *code, size_t size, size_t *pos, uint32_t argc,
                                    uint64_t address, xxbyte_instruction *instruction)
{
    uint32_t i;

    instruction->table_count = argc;
    instruction->table_offset = argc != 0 ? (uint32_t)*pos : 0;
    for (i = 0; i < argc; ++i) {
        xxbyte_status status = XXBYTE_STATUS_OK;
        size_t n = arg_size(mode, code, size, *pos, &status);
        if (n == 0) {
            return status;
        }
        if (instruction->operand_count < XXBYTE_MAX_OPERANDS) {
            xxbyte_operand *operand = &instruction->operands[instruction->operand_count++];
            arg_operand(mode, code, *pos, operand);
            special_argument(mode, instruction->opcode, i, code[*pos], ctx, address, operand);
        }
        *pos += n;
        if (*pos > UINT32_MAX) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    return XXBYTE_STATUS_OK;
}

size_t xxbyte_is_decl_size(const uint8_t *data, size_t size, size_t pos)
{
    size_t start = pos;
    size_t count;

    if (pos > size || size - pos < 4) {
        return 0;
    }
    count = xxbyte_u16le(data + pos + 2);
    pos += 4;
    if (size - pos < count * 4u + 4u) {
        return 0;
    }
    pos += count * 4u;
    count = xxbyte_u16le(data + pos + 2);
    pos += 4;
    if (size - pos < count * 4u) {
        return 0;
    }
    pos += count * 4u;
    return pos - start;
}

static xxbyte_status block_header(const xxbyte_installshield_context *ctx, const uint8_t *code, size_t code_size, uint64_t address, uint32_t opcode, size_t pos,
                                  xxbyte_instruction *instruction)
{
    uint32_t block;

    if (code_size < pos + 2u) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    instruction->opcode = opcode;
    instruction->id = XXBYTE_INSTALLSHIELD_ID_BLOCK;
    instruction->mnemonic = block_name;
    instruction->group = XXBYTE_GROUP_PAYLOAD;
    if (find_block(ctx, address, &block)) {
        xxbyte_add_index(instruction, (xxbyte_index_kind)XXBYTE_INSTALLSHIELD_INDEX_BLOCK, 0, block);
    }
    add(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, xxbyte_u16le(code + pos));
    instruction->size = (uint32_t)(pos + 2u);
    return XXBYTE_STATUS_OK;
}

static xxbyte_status decode_inx(const xxbyte_installshield_context *ctx, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    uint32_t block;
    uint32_t opcode;
    uint32_t argc;
    const inx_entry *entry;
    size_t pos = 2;
    xxbyte_status status;

    if (find_block(ctx, address, &block)) {
        return block_header(ctx, code, code_size, address, XXBYTE_INSTALLSHIELD_BLOCK_OPCODE, 0, instruction);
    }
    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    opcode = xxbyte_u16le(code);
    if (opcode > XXBYTE_INSTALLSHIELD_OP_ADDRSTR || inx_table[opcode].name == NULL) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    entry = &inx_table[opcode];
    instruction->opcode = opcode;
    instruction->id = opcode;
    instruction->mnemonic = entry->name;
    instruction->group = entry->group;
    if (code_size < 4) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (entry->kind == K_STRUCT) {
        xxbyte_add_index(instruction, XXBYTE_INDEX_TYPE, 2, xxbyte_u16le(code + 2));
        pos = 4;
        argc = 1;
    } else if (entry->kind == K_CALL) {
        if (code_size < 6) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        xxbyte_add_index(instruction, XXBYTE_INDEX_FUNCTION, 2, xxbyte_u16le(code + 2));
        argc = xxbyte_u16le(code + 4);
        pos = 6;
    } else {
        argc = xxbyte_u16le(code + 2);
        pos = 4;
    }
    status = read_arguments(XXBYTE_INSTALLSHIELD_MODE_INX, ctx, code, code_size, &pos, argc, address, instruction);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (entry->kind == K_FUNCTION) {
        uint32_t offset;
        if (pos >= code_size) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (code[pos] != 7) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
        if (code_size - pos < 5) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        offset = xxbyte_u32le(code + pos + 1);
        if (instruction->operand_count < XXBYTE_MAX_OPERANDS) {
            add(instruction, XXBYTE_OPERAND_DATA, 4, (int64_t)(address + pos + 1u + offset));
        }
        pos += 5;
    } else if (entry->kind == K_ENDFUNC) {
        size_t n;
        if (code_size - pos < 4 || code_size - pos < 8u + 4u * (size_t)xxbyte_u16le(code + pos + 2)) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        n = xxbyte_is_decl_size(code, code_size, pos);
        if (n == 0) {
            return XXBYTE_STATUS_TRUNCATED;
        }
        if (instruction->operand_count + 3u <= XXBYTE_MAX_OPERANDS) {
            add(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, xxbyte_u16le(code + pos));
            add(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, xxbyte_u16le(code + pos + 4u + 4u * (size_t)xxbyte_u16le(code + pos + 2)));
            add(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, xxbyte_u16le(code + pos + 2));
        }
        pos += n;
    }
    if (pos > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->size = (uint32_t)pos;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status decode_ins(const xxbyte_installshield_context *ctx, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    uint32_t opcode;
    uint32_t argc;
    size_t pos = 2;
    const ins_entry *entry;
    xxbyte_status status;
    xxbyte_operand *operand;

    if (code_size < 2) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    opcode = xxbyte_u16le(code);
    if (opcode == XXBYTE_INSTALLSHIELD_INS_OP_LABEL) {
        return block_header(ctx, code, code_size, address, 0, 2, instruction);
    }
    if (opcode > XXBYTE_INSTALLSHIELD_INS_OP_MAX || ins_table[opcode].name == NULL) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    entry = &ins_table[opcode];
    instruction->opcode = opcode;
    instruction->id = XXBYTE_INSTALLSHIELD_ID_INS_BASE + opcode;
    instruction->mnemonic = entry->name;
    argc = entry->argc & 0x0Fu;
    switch (opcode) {
        case XXBYTE_INSTALLSHIELD_INS_OP_IF:
            if (code_size < 6) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            operand = &instruction->operands[instruction->operand_count++];
            label_operand(ctx, xxbyte_u16le(code + 3), operand);
            add(instruction, XXBYTE_OPERAND_IMMEDIATE, 1, code[5]);
            instruction->group = XXBYTE_GROUP_CONDITIONAL;
            pos = 6;
            argc = 2;
            break;
        case XXBYTE_INSTALLSHIELD_INS_OP_GOSUB:
            instruction->group = XXBYTE_GROUP_CALL;
            if (code_size >= 3 && code[2] == 0x70u) {
                if (code_size < 5) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                operand = &instruction->operands[instruction->operand_count++];
                label_operand(ctx, xxbyte_u16le(code + 3), operand);
                instruction->size = 5;
                return XXBYTE_STATUS_OK;
            }
            break;
        case XXBYTE_INSTALLSHIELD_INS_OP_RETSUB: instruction->group = G_RET; break;
        case XXBYTE_INSTALLSHIELD_INS_OP_GOTO:
            if (code_size < 5) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            operand = &instruction->operands[instruction->operand_count++];
            label_operand(ctx, xxbyte_u16le(code + 3), operand);
            instruction->group = G_STOP_JUMP;
            instruction->size = 5;
            return XXBYTE_STATUS_OK;
        case XXBYTE_INSTALLSHIELD_INS_OP_CALL:
        case XXBYTE_INSTALLSHIELD_INS_OP_CALLDLL: {
            uint32_t proto;
            if (code_size < 5) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            proto = xxbyte_u16le(code + 3);
            xxbyte_add_index(instruction, XXBYTE_INDEX_FUNCTION, 2, proto);
            pos = 5;
            if (opcode == XXBYTE_INSTALLSHIELD_INS_OP_CALL) {
                if (code_size < 8) {
                    return XXBYTE_STATUS_TRUNCATED;
                }
                operand = &instruction->operands[instruction->operand_count++];
                label_operand(ctx, xxbyte_u16le(code + 6), operand);
                pos = 8;
            }
            if (ctx == NULL || ctx->prototype_arguments == NULL || proto >= ctx->prototype_count) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            argc = ctx->prototype_arguments[proto];
            instruction->group = XXBYTE_GROUP_CALL;
            break;
        }
        case XXBYTE_INSTALLSHIELD_INS_OP_FUNCTION: {
            size_t strings;
            if (code_size < 6) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            strings = xxbyte_u16le(code + 4);
            pos = 6u + strings * 2u;
            if (code_size < pos + 6u) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            add(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, (int64_t)strings);
            add(instruction, XXBYTE_OPERAND_IMMEDIATE, 2, xxbyte_u16le(code + pos + 2));
            instruction->size = (uint32_t)(pos + 6u);
            return XXBYTE_STATUS_OK;
        }
        case XXBYTE_INSTALLSHIELD_INS_OP_SPRINTF:
        case XXBYTE_INSTALLSHIELD_INS_OP_SPRINTFBOX:
        case XXBYTE_INSTALLSHIELD_INS_OP_ASKOPTIONS:
            if (code_size < 5) {
                return XXBYTE_STATUS_TRUNCATED;
            }
            argc = xxbyte_u16le(code + 3);
            if (opcode == XXBYTE_INSTALLSHIELD_INS_OP_ASKOPTIONS) {
                argc = 2u + 2u * argc;
            }
            pos = 5;
            break;
        case XXBYTE_INSTALLSHIELD_INS_OP_EXIT: instruction->group = XXBYTE_GROUP_STOP; break;
        case XXBYTE_INSTALLSHIELD_INS_OP_RETURN:
        case XXBYTE_INSTALLSHIELD_INS_OP_ENDFUNC: instruction->group = G_RET; break;
        case XXBYTE_INSTALLSHIELD_INS_OP_ABORT: instruction->group = G_THROW; break;
        default: break;
    }
    status = read_arguments(XXBYTE_INSTALLSHIELD_MODE_INS, ctx, code, code_size, &pos, argc, address, instruction);
    if (status != XXBYTE_STATUS_OK) {
        return status;
    }
    if (pos > UINT32_MAX) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    instruction->size = (uint32_t)pos;
    return XXBYTE_STATUS_OK;
}

static xxbyte_status installshield_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    const xxbyte_installshield_context *ctx = (const xxbyte_installshield_context *)context;

    return is_ins(mode) ? decode_ins(ctx, code, code_size, address, instruction) : decode_inx(ctx, code, code_size, address, instruction);
}

xxbyte_status xxbyte_installshield_argument(const xxbyte_instruction *instruction, uint32_t index, xxbyte_operand *operand)
{
    uint32_t fixed;
    uint32_t i;
    size_t pos;
    size_t limit;

    if (instruction == NULL || operand == NULL || instruction->code == NULL || instruction->family != XXBYTE_FAMILY_INSTALLSHIELD || index >= instruction->table_count ||
        instruction->id == XXBYTE_INSTALLSHIELD_ID_BLOCK) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    fixed = fixed_operands(instruction->mode, instruction->opcode);
    if (fixed + index < instruction->operand_count) {
        *operand = instruction->operands[fixed + index];
        return XXBYTE_STATUS_OK;
    }
    pos = instruction->table_offset;
    limit = instruction->size;
    for (i = 0; i < index; ++i) {
        xxbyte_status status = XXBYTE_STATUS_OK;
        size_t n = arg_size(instruction->mode, instruction->code, limit, pos, &status);
        if (n == 0) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
        pos += n;
    }
    {
        xxbyte_status status = XXBYTE_STATUS_OK;
        if (arg_size(instruction->mode, instruction->code, limit, pos, &status) == 0) {
            return XXBYTE_STATUS_INVALID_ARGUMENT;
        }
    }
    arg_operand(instruction->mode, instruction->code, pos, operand);
    special_argument(instruction->mode, instruction->opcode, index, instruction->code[pos], NULL, instruction->address, operand);
    return XXBYTE_STATUS_OK;
}

/* Formatting. */

static int fmt_string(xxbyte_fmt *fmt, const uint8_t *s, size_t n)
{
    size_t i;

    if (!xxbyte_fmt_char(fmt, '"')) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        uint8_t c = s[i];
        int ok;
        switch (c) {
            case '\n': ok = xxbyte_fmt_str(fmt, "\\n"); break;
            case '\r': ok = xxbyte_fmt_str(fmt, "\\r"); break;
            case '\t': ok = xxbyte_fmt_str(fmt, "\\t"); break;
            case '\\': ok = xxbyte_fmt_str(fmt, "\\\\"); break;
            case '"': ok = xxbyte_fmt_str(fmt, "\\\""); break;
            default: ok = (c < 0x20 || c > 0x7E) ? xxbyte_fmt_printf(fmt, "\\%03o", (unsigned)c) : xxbyte_fmt_char(fmt, (char)c); break;
        }
        if (!ok) {
            return 0;
        }
    }
    return xxbyte_fmt_char(fmt, '"');
}

/* Formats operand (operand index in instruction, or an argument entry
 * outside operands[] when entry is nonzero). */
static int fmt_operand(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, const xxbyte_operand *operand, uint32_t index, int entry)
{
    if (operand->type == XXBYTE_OPERAND_INDEX || operand->type == XXBYTE_OPERAND_BRANCH || operand->type == XXBYTE_OPERAND_DATA) {
        int r = entry ? xxbyte_fmt_resolve_entry(fmt, instruction, operand) : xxbyte_fmt_resolve(fmt, instruction, index);
        if (r != 0) {
            return r > 0;
        }
    }
    switch (operand->type) {
        case XXBYTE_OPERAND_INDEX:
            switch (operand->kind) {
                case XXBYTE_INDEX_GLOBAL:
                case XXBYTE_INDEX_LOCAL: {
                    char bank = (operand->flags & XXBYTE_INSTALLSHIELD_OPERAND_FLAG_STRING) != 0   ? 's'
                                : (operand->flags & XXBYTE_INSTALLSHIELD_OPERAND_FLAG_NUMBER) != 0 ? 'n'
                                                                                                   : 'v';
                    return xxbyte_fmt_printf(fmt, "%s%c%lld", operand->kind == XXBYTE_INDEX_LOCAL ? "l" : "", bank, (long long)operand->value);
                }
                case XXBYTE_INDEX_FUNCTION: return xxbyte_fmt_printf(fmt, "func_%lld", (long long)operand->value);
                case XXBYTE_INDEX_TYPE: return xxbyte_fmt_printf(fmt, "typedef_%lld", (long long)operand->value);
                case XXBYTE_INSTALLSHIELD_INDEX_BLOCK_DELTA: return xxbyte_fmt_printf(fmt, operand->value >= 0 ? "+%lld" : "%lld", (long long)operand->value);
                default: return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
            }
        case XXBYTE_OPERAND_BRANCH:
        case XXBYTE_OPERAND_DATA: return xxbyte_fmt_printf(fmt, "0x%llX", (unsigned long long)operand->value);
        case XXBYTE_OPERAND_BYTES: return fmt_string(fmt, instruction->code + operand->value, operand->size);
        default: return xxbyte_fmt_printf(fmt, "%lld", (long long)operand->value);
    }
}

static int fmt_sep(xxbyte_fmt *fmt, int *first)
{
    int ok = xxbyte_fmt_str(fmt, *first ? " " : ", ");
    *first = 0;
    return ok;
}

/* Position of the INX trailer (after the last argument). */
static size_t trailer_position(const xxbyte_instruction *instruction)
{
    size_t pos = instruction->table_count != 0 ? instruction->table_offset : 4u;
    uint32_t i;

    for (i = 0; i < instruction->table_count; ++i) {
        xxbyte_status status = XXBYTE_STATUS_OK;
        size_t n = arg_size(instruction->mode, instruction->code, instruction->size, pos, &status);
        if (n == 0) {
            break;
        }
        pos += n;
    }
    return pos;
}

static int installshield_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    uint32_t fixed;
    uint32_t i;
    int first = 1;

    if (instruction->id == XXBYTE_INSTALLSHIELD_ID_BLOCK) {
        const xxbyte_operand *count = &instruction->operands[instruction->operand_count - 1u];
        if (instruction->operand_count == 2) {
            if (!xxbyte_fmt_char(fmt, ' ') || !fmt_operand(fmt, instruction, &instruction->operands[0], 0, 0)) {
                return 0;
            }
        }
        return xxbyte_fmt_printf(fmt, " (%lld)", (long long)count->value);
    }
    if (is_ins(instruction->mode) && instruction->opcode == XXBYTE_INSTALLSHIELD_INS_OP_FUNCTION) {
        return xxbyte_fmt_printf(fmt, " strings %lld, numbers %lld", (long long)instruction->operands[0].value, (long long)instruction->operands[1].value);
    }
    if (is_ins(instruction->mode) && instruction->opcode == XXBYTE_INSTALLSHIELD_INS_OP_IF && instruction->operand_count >= 4) {
        static const char *const compare[7] = {NULL, "<", ">", "<=", ">=", "=", "!="};
        uint32_t code = (uint32_t)instruction->operands[1].value & 0x0Fu;
        if (!xxbyte_fmt_char(fmt, ' ') || !fmt_operand(fmt, instruction, &instruction->operands[0], 0, 0) || !xxbyte_fmt_str(fmt, ", ") ||
            !fmt_operand(fmt, instruction, &instruction->operands[2], 2, 0)) {
            return 0;
        }
        if (code >= 1 && code <= 6 ? !xxbyte_fmt_printf(fmt, " %s ", compare[code]) : !xxbyte_fmt_printf(fmt, " ?%u ", (unsigned)instruction->operands[1].value)) {
            return 0;
        }
        return fmt_operand(fmt, instruction, &instruction->operands[3], 3, 0);
    }
    fixed = fixed_operands(instruction->mode, instruction->opcode);
    if (is_ins(instruction->mode) && instruction->opcode == XXBYTE_INSTALLSHIELD_INS_OP_GOSUB && instruction->table_count == 0) {
        fixed = 1;
    }
    for (i = 0; i < fixed && i < instruction->operand_count; ++i) {
        if (!fmt_sep(fmt, &first) || !fmt_operand(fmt, instruction, &instruction->operands[i], i, 0)) {
            return 0;
        }
    }
    for (i = 0; i < instruction->table_count; ++i) {
        xxbyte_operand operand;
        int entry = fixed + i >= instruction->operand_count;
        if (xxbyte_installshield_argument(instruction, i, &operand) != XXBYTE_STATUS_OK) {
            return 0;
        }
        if (!fmt_sep(fmt, &first) || !fmt_operand(fmt, instruction, &operand, fixed + i, entry)) {
            return 0;
        }
    }
    if (!is_ins(instruction->mode) && instruction->opcode == XXBYTE_INSTALLSHIELD_OP_FUNCTION) {
        size_t pos = trailer_position(instruction);
        uint32_t index = fixed + instruction->table_count;
        xxbyte_operand data;
        if (index < instruction->operand_count) {
            return fmt_sep(fmt, &first) && fmt_operand(fmt, instruction, &instruction->operands[index], index, 0);
        }
        memset(&data, 0, sizeof(data));
        data.type = XXBYTE_OPERAND_DATA;
        data.size = 4;
        data.value = (int64_t)(instruction->address + pos + 1u + xxbyte_u32le(instruction->code + pos + 1));
        return fmt_sep(fmt, &first) && fmt_operand(fmt, instruction, &data, 0, 1);
    }
    if (!is_ins(instruction->mode) && instruction->opcode == XXBYTE_INSTALLSHIELD_OP_ENDFUNC) {
        const uint8_t *d = instruction->code + trailer_position(instruction);
        unsigned objects = xxbyte_u16le(d + 2);
        unsigned strings = xxbyte_u16le(d + 4u + 4u * objects);
        return fmt_sep(fmt, &first) && xxbyte_fmt_printf(fmt, "numbers %u, strings %u, objects %u", (unsigned)xxbyte_u16le(d), strings, objects);
    }
    return 1;
}

static const char *const type_names[] = {
    "STRING", "CHAR", "SHORT", "INT", "LONG", "POINTER", "BOOL", "HWND", "VOID", "STRUCT", "OBJECT", "BINARY", "WSTRING", "WPOINTER", "int32",
};

const char *xxbyte_installshield_type_name(uint32_t type)
{
    return type < sizeof(type_names) / sizeof(type_names[0]) ? type_names[type] : NULL;
}

const xxbyte_family_ops xxbyte_installshield_ops = {
    XXBYTE_FAMILY_INSTALLSHIELD, XXBYTE_INSTALLSHIELD_MODE_DEFAULT, installshield_check_mode, installshield_decode, NULL, installshield_format_operands, NULL,
    xxbyte_is_list_file,
};

#else
typedef int xxbyte_installshield_disabled;
#endif
