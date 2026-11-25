#include "xxbyte_internal.h"

#if XXBYTE_USE_AVM1

#include "xxbyte_avm1_internal.h"
#include "xxbyte/xxbyte_xxfc.h"

#include <xxfclib/memory/xx_memory.h>

/*
 * AVM1 action records (SWF19 chapter "Actions"; Ruffle swf/src/avm1/read.rs
 * for the details the specification leaves open or gets wrong).
 */

#define G_JUMP (XXBYTE_GROUP_JUMP | XXBYTE_GROUP_STOP)
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_CALL XXBYTE_GROUP_CALL
#define G_RET (XXBYTE_GROUP_RETURN | XXBYTE_GROUP_STOP)
#define G_THROW (XXBYTE_GROUP_THROW | XXBYTE_GROUP_STOP)

typedef struct avm1_info {
    uint8_t opcode;
    uint8_t version; /* SWF19: the SWF version that introduced it */
    uint16_t group;
    const char *name;
} avm1_info;

static const avm1_info avm1_infos[XXBYTE_AVM1_ID_COUNT] = {
    {0x00, 0, 0, NULL},
    {0x00, 3, G_RET, "end"},
    {0x04, 3, 0, "nextframe"},
    {0x05, 3, 0, "previousframe"},
    {0x06, 3, 0, "play"},
    {0x07, 3, 0, "stop"},
    {0x08, 3, 0, "togglequality"},
    {0x09, 3, 0, "stopsounds"},
    {0x0A, 4, 0, "add"},
    {0x0B, 4, 0, "subtract"},
    {0x0C, 4, 0, "multiply"},
    {0x0D, 4, 0, "divide"},
    {0x0E, 4, 0, "equals"},
    {0x0F, 4, 0, "less"},
    {0x10, 4, 0, "and"},
    {0x11, 4, 0, "or"},
    {0x12, 4, 0, "not"},
    {0x13, 4, 0, "stringequals"},
    {0x14, 4, 0, "stringlength"},
    {0x15, 4, 0, "stringextract"},
    {0x17, 4, 0, "pop"},
    {0x18, 4, 0, "tointeger"},
    {0x1C, 4, 0, "getvariable"},
    {0x1D, 4, 0, "setvariable"},
    {0x20, 4, 0, "settarget2"},
    {0x21, 4, 0, "stringadd"},
    {0x22, 4, 0, "getproperty"},
    {0x23, 4, 0, "setproperty"},
    {0x24, 4, 0, "clonesprite"},
    {0x25, 4, 0, "removesprite"},
    {0x26, 4, 0, "trace"},
    {0x27, 4, 0, "startdrag"},
    {0x28, 4, 0, "enddrag"},
    {0x29, 4, 0, "stringless"},
    {0x2A, 7, G_THROW, "throw"},
    {0x2B, 7, 0, "castop"},
    {0x2C, 7, 0, "implementsop"},
    {0x2D, 0, 0, "fscommand2"},
    {0x30, 4, 0, "randomnumber"},
    {0x31, 4, 0, "mbstringlength"},
    {0x32, 4, 0, "chartoascii"},
    {0x33, 4, 0, "asciitochar"},
    {0x34, 4, 0, "gettime"},
    {0x35, 4, 0, "mbstringextract"},
    {0x36, 4, 0, "mbchartoascii"},
    {0x37, 4, 0, "mbasciitochar"},
    {0x3A, 5, 0, "delete"},
    {0x3B, 5, 0, "delete2"},
    {0x3C, 5, 0, "definelocal"},
    {0x3D, 5, G_CALL, "callfunction"},
    {0x3E, 5, G_RET, "return"},
    {0x3F, 5, 0, "modulo"},
    {0x40, 5, G_CALL, "newobject"},
    {0x41, 5, 0, "definelocal2"},
    {0x42, 5, 0, "initarray"},
    {0x43, 5, 0, "initobject"},
    {0x44, 5, 0, "typeof"},
    {0x45, 5, 0, "targetpath"},
    {0x46, 5, 0, "enumerate"},
    {0x47, 5, 0, "add2"},
    {0x48, 5, 0, "less2"},
    {0x49, 5, 0, "equals2"},
    {0x4A, 5, 0, "tonumber"},
    {0x4B, 5, 0, "tostring"},
    {0x4C, 5, 0, "pushduplicate"},
    {0x4D, 5, 0, "stackswap"},
    {0x4E, 5, 0, "getmember"},
    {0x4F, 5, 0, "setmember"},
    {0x50, 5, 0, "increment"},
    {0x51, 5, 0, "decrement"},
    {0x52, 5, G_CALL, "callmethod"},
    {0x53, 5, G_CALL, "newmethod"},
    {0x54, 6, 0, "instanceof"},
    {0x55, 6, 0, "enumerate2"},
    {0x60, 5, 0, "bitand"},
    {0x61, 5, 0, "bitor"},
    {0x62, 5, 0, "bitxor"},
    {0x63, 5, 0, "bitlshift"},
    {0x64, 5, 0, "bitrshift"},
    {0x65, 5, 0, "biturshift"},
    {0x66, 6, 0, "strictequals"},
    {0x67, 6, 0, "greater"},
    {0x68, 6, 0, "stringgreater"},
    {0x69, 7, 0, "extends"},
    {0x81, 3, 0, "gotoframe"},
    {0x83, 3, 0, "geturl"},
    {0x87, 5, 0, "storeregister"},
    {0x88, 5, 0, "constantpool"},
    {0x89, 0, 0, "strictmode"},
    {0x8A, 3, G_COND, "waitforframe"},
    {0x8B, 3, 0, "settarget"},
    {0x8C, 3, 0, "gotolabel"},
    {0x8D, 4, G_COND, "waitforframe2"},
    {0x8E, 7, 0, "definefunction2"},
    {0x8F, 7, 0, "try"},
    {0x94, 5, 0, "with"},
    {0x96, 4, 0, "push"},
    {0x99, 4, G_JUMP, "jump"},
    {0x9A, 4, 0, "geturl2"},
    {0x9B, 5, 0, "definefunction"},
    {0x9D, 4, G_COND, "if"},
    {0x9E, 4, G_CALL, "call"},
    {0x9F, 4, 0, "gotoframe2"},
    {0x00, 0, 0, "unknown"},
};

/* opcode -> id; 0 means undefined (XXBYTE_AVM1_ID_UNKNOWN). Filled from
 * avm1_infos on first use would need a lock, so it is spelled out. */
static const uint8_t avm1_ids[256] = {
    [0x00] = XXBYTE_AVM1_ID_END,
    [0x04] = XXBYTE_AVM1_ID_NEXTFRAME,
    [0x05] = XXBYTE_AVM1_ID_PREVIOUSFRAME,
    [0x06] = XXBYTE_AVM1_ID_PLAY,
    [0x07] = XXBYTE_AVM1_ID_STOP,
    [0x08] = XXBYTE_AVM1_ID_TOGGLEQUALITY,
    [0x09] = XXBYTE_AVM1_ID_STOPSOUNDS,
    [0x0A] = XXBYTE_AVM1_ID_ADD,
    [0x0B] = XXBYTE_AVM1_ID_SUBTRACT,
    [0x0C] = XXBYTE_AVM1_ID_MULTIPLY,
    [0x0D] = XXBYTE_AVM1_ID_DIVIDE,
    [0x0E] = XXBYTE_AVM1_ID_EQUALS,
    [0x0F] = XXBYTE_AVM1_ID_LESS,
    [0x10] = XXBYTE_AVM1_ID_AND,
    [0x11] = XXBYTE_AVM1_ID_OR,
    [0x12] = XXBYTE_AVM1_ID_NOT,
    [0x13] = XXBYTE_AVM1_ID_STRINGEQUALS,
    [0x14] = XXBYTE_AVM1_ID_STRINGLENGTH,
    [0x15] = XXBYTE_AVM1_ID_STRINGEXTRACT,
    [0x17] = XXBYTE_AVM1_ID_POP,
    [0x18] = XXBYTE_AVM1_ID_TOINTEGER,
    [0x1C] = XXBYTE_AVM1_ID_GETVARIABLE,
    [0x1D] = XXBYTE_AVM1_ID_SETVARIABLE,
    [0x20] = XXBYTE_AVM1_ID_SETTARGET2,
    [0x21] = XXBYTE_AVM1_ID_STRINGADD,
    [0x22] = XXBYTE_AVM1_ID_GETPROPERTY,
    [0x23] = XXBYTE_AVM1_ID_SETPROPERTY,
    [0x24] = XXBYTE_AVM1_ID_CLONESPRITE,
    [0x25] = XXBYTE_AVM1_ID_REMOVESPRITE,
    [0x26] = XXBYTE_AVM1_ID_TRACE,
    [0x27] = XXBYTE_AVM1_ID_STARTDRAG,
    [0x28] = XXBYTE_AVM1_ID_ENDDRAG,
    [0x29] = XXBYTE_AVM1_ID_STRINGLESS,
    [0x2A] = XXBYTE_AVM1_ID_THROW,
    [0x2B] = XXBYTE_AVM1_ID_CASTOP,
    [0x2C] = XXBYTE_AVM1_ID_IMPLEMENTSOP,
    [0x2D] = XXBYTE_AVM1_ID_FSCOMMAND2,
    [0x30] = XXBYTE_AVM1_ID_RANDOMNUMBER,
    [0x31] = XXBYTE_AVM1_ID_MBSTRINGLENGTH,
    [0x32] = XXBYTE_AVM1_ID_CHARTOASCII,
    [0x33] = XXBYTE_AVM1_ID_ASCIITOCHAR,
    [0x34] = XXBYTE_AVM1_ID_GETTIME,
    [0x35] = XXBYTE_AVM1_ID_MBSTRINGEXTRACT,
    [0x36] = XXBYTE_AVM1_ID_MBCHARTOASCII,
    [0x37] = XXBYTE_AVM1_ID_MBASCIITOCHAR,
    [0x3A] = XXBYTE_AVM1_ID_DELETE,
    [0x3B] = XXBYTE_AVM1_ID_DELETE2,
    [0x3C] = XXBYTE_AVM1_ID_DEFINELOCAL,
    [0x3D] = XXBYTE_AVM1_ID_CALLFUNCTION,
    [0x3E] = XXBYTE_AVM1_ID_RETURN,
    [0x3F] = XXBYTE_AVM1_ID_MODULO,
    [0x40] = XXBYTE_AVM1_ID_NEWOBJECT,
    [0x41] = XXBYTE_AVM1_ID_DEFINELOCAL2,
    [0x42] = XXBYTE_AVM1_ID_INITARRAY,
    [0x43] = XXBYTE_AVM1_ID_INITOBJECT,
    [0x44] = XXBYTE_AVM1_ID_TYPEOF,
    [0x45] = XXBYTE_AVM1_ID_TARGETPATH,
    [0x46] = XXBYTE_AVM1_ID_ENUMERATE,
    [0x47] = XXBYTE_AVM1_ID_ADD2,
    [0x48] = XXBYTE_AVM1_ID_LESS2,
    [0x49] = XXBYTE_AVM1_ID_EQUALS2,
    [0x4A] = XXBYTE_AVM1_ID_TONUMBER,
    [0x4B] = XXBYTE_AVM1_ID_TOSTRING,
    [0x4C] = XXBYTE_AVM1_ID_PUSHDUPLICATE,
    [0x4D] = XXBYTE_AVM1_ID_STACKSWAP,
    [0x4E] = XXBYTE_AVM1_ID_GETMEMBER,
    [0x4F] = XXBYTE_AVM1_ID_SETMEMBER,
    [0x50] = XXBYTE_AVM1_ID_INCREMENT,
    [0x51] = XXBYTE_AVM1_ID_DECREMENT,
    [0x52] = XXBYTE_AVM1_ID_CALLMETHOD,
    [0x53] = XXBYTE_AVM1_ID_NEWMETHOD,
    [0x54] = XXBYTE_AVM1_ID_INSTANCEOF,
    [0x55] = XXBYTE_AVM1_ID_ENUMERATE2,
    [0x60] = XXBYTE_AVM1_ID_BITAND,
    [0x61] = XXBYTE_AVM1_ID_BITOR,
    [0x62] = XXBYTE_AVM1_ID_BITXOR,
    [0x63] = XXBYTE_AVM1_ID_BITLSHIFT,
    [0x64] = XXBYTE_AVM1_ID_BITRSHIFT,
    [0x65] = XXBYTE_AVM1_ID_BITURSHIFT,
    [0x66] = XXBYTE_AVM1_ID_STRICTEQUALS,
    [0x67] = XXBYTE_AVM1_ID_GREATER,
    [0x68] = XXBYTE_AVM1_ID_STRINGGREATER,
    [0x69] = XXBYTE_AVM1_ID_EXTENDS,
    [0x81] = XXBYTE_AVM1_ID_GOTOFRAME,
    [0x83] = XXBYTE_AVM1_ID_GETURL,
    [0x87] = XXBYTE_AVM1_ID_STOREREGISTER,
    [0x88] = XXBYTE_AVM1_ID_CONSTANTPOOL,
    [0x89] = XXBYTE_AVM1_ID_STRICTMODE,
    [0x8A] = XXBYTE_AVM1_ID_WAITFORFRAME,
    [0x8B] = XXBYTE_AVM1_ID_SETTARGET,
    [0x8C] = XXBYTE_AVM1_ID_GOTOLABEL,
    [0x8D] = XXBYTE_AVM1_ID_WAITFORFRAME2,
    [0x8E] = XXBYTE_AVM1_ID_DEFINEFUNCTION2,
    [0x8F] = XXBYTE_AVM1_ID_TRY,
    [0x94] = XXBYTE_AVM1_ID_WITH,
    [0x96] = XXBYTE_AVM1_ID_PUSH,
    [0x99] = XXBYTE_AVM1_ID_JUMP,
    [0x9A] = XXBYTE_AVM1_ID_GETURL2,
    [0x9B] = XXBYTE_AVM1_ID_DEFINEFUNCTION,
    [0x9D] = XXBYTE_AVM1_ID_IF,
    [0x9E] = XXBYTE_AVM1_ID_CALL,
    [0x9F] = XXBYTE_AVM1_ID_GOTOFRAME2,
};

const char *xxbyte_avm1_mnemonic(uint32_t id)
{
    return id != 0 && id < XXBYTE_AVM1_ID_COUNT ? avm1_infos[id].name : NULL;
}

uint32_t xxbyte_avm1_opcode_id(uint8_t opcode)
{
    return avm1_ids[opcode] != 0 ? avm1_ids[opcode] : (uint32_t)XXBYTE_AVM1_ID_UNKNOWN;
}

uint32_t xxbyte_avm1_action_version(uint8_t opcode)
{
    return avm1_infos[xxbyte_avm1_opcode_id(opcode)].version;
}

/* ---- payload reader ---------------------------------------------------- */

typedef struct avm1_reader {
    const uint8_t *code;
    uint32_t pos;
    uint32_t end;
} avm1_reader;

static int rd_u8(avm1_reader *r, uint32_t *value)
{
    if (r->end - r->pos < 1) {
        return 0;
    }
    *value = r->code[r->pos];
    r->pos += 1;
    return 1;
}

static int rd_u16(avm1_reader *r, uint32_t *value)
{
    if (r->end - r->pos < 2) {
        return 0;
    }
    *value = xxbyte_u16le(r->code + r->pos);
    r->pos += 2;
    return 1;
}

static int rd_u32(avm1_reader *r, uint32_t *value)
{
    if (r->end - r->pos < 4) {
        return 0;
    }
    *value = xxbyte_u32le(r->code + r->pos);
    r->pos += 4;
    return 1;
}

/* A NUL-terminated string: *offset is its first byte, *length excludes the
 * NUL. */
static int rd_str(avm1_reader *r, uint32_t *offset, uint32_t *length)
{
    const uint8_t *start = r->code + r->pos;
    const uint8_t *nul = (const uint8_t *)memchr(start, 0, r->end - r->pos);

    if (nul == NULL) {
        return 0;
    }
    *offset = r->pos;
    *length = (uint32_t)(nul - start);
    r->pos += *length + 1u;
    return 1;
}

static void set_operand(xxbyte_operand *operand, xxbyte_operand_type type, uint32_t size, int64_t value)
{
    memset(operand, 0, sizeof(*operand));
    operand->type = (uint8_t)type;
    operand->size = size;
    operand->value = value;
}

static void set_string(xxbyte_operand *operand, uint32_t offset, uint32_t length)
{
    set_operand(operand, XXBYTE_OPERAND_BYTES, length, (int64_t)offset);
    operand->flags = XXBYTE_AVM1_OPERAND_FLAG_STRING;
}

/*
 * Reads one table entry of an instruction of id at *pos (offsets from code),
 * not past end. Returns 1 and advances *pos, or 0 when the entry does not
 * fit.
 */
static int read_entry(const uint8_t *code, uint32_t id, uint32_t pos, uint32_t end, uint32_t index, xxbyte_avm1_entry *entry)
{
    avm1_reader r;
    uint32_t value;
    uint32_t offset;
    uint32_t length;

    r.code = code;
    r.pos = pos;
    r.end = end;
    memset(entry, 0, sizeof(*entry));
    entry->index = index;
    entry->offset = pos;
    entry->type = XXBYTE_AVM1_PUSH_STRING;
    switch (id) {
        case XXBYTE_AVM1_ID_PUSH:
            if (!rd_u8(&r, &entry->type)) {
                return 0;
            }
            switch (entry->type) {
                case XXBYTE_AVM1_PUSH_STRING:
                    if (!rd_str(&r, &offset, &length)) {
                        return 0;
                    }
                    set_string(&entry->value, offset, length);
                    break;
                case XXBYTE_AVM1_PUSH_FLOAT:
                    if (!rd_u32(&r, &value)) {
                        return 0;
                    }
                    set_operand(&entry->value, XXBYTE_OPERAND_FLOAT, 4, (int64_t)value);
                    break;
                case XXBYTE_AVM1_PUSH_REGISTER:
                    if (!rd_u8(&r, &value)) {
                        return 0;
                    }
                    set_operand(&entry->value, XXBYTE_OPERAND_REGISTER, 1, (int64_t)value);
                    break;
                case XXBYTE_AVM1_PUSH_BOOLEAN:
                    if (!rd_u8(&r, &value)) {
                        return 0;
                    }
                    set_operand(&entry->value, XXBYTE_OPERAND_IMMEDIATE, 1, (int64_t)value);
                    break;
                case XXBYTE_AVM1_PUSH_DOUBLE: {
                    uint32_t high;
                    uint32_t low;
                    if (!rd_u32(&r, &high) || !rd_u32(&r, &low)) {
                        return 0;
                    }
                    set_operand(&entry->value, XXBYTE_OPERAND_FLOAT, 8, (int64_t)(((uint64_t)high << 32) | low));
                    break;
                }
                case XXBYTE_AVM1_PUSH_INTEGER:
                    if (!rd_u32(&r, &value)) {
                        return 0;
                    }
                    set_operand(&entry->value, XXBYTE_OPERAND_IMMEDIATE, 4, (int64_t)(int32_t)value);
                    entry->value.flags = XXBYTE_OPERAND_FLAG_SIGNED;
                    break;
                case XXBYTE_AVM1_PUSH_CONSTANT8:
                case XXBYTE_AVM1_PUSH_CONSTANT16:
                    if (entry->type == XXBYTE_AVM1_PUSH_CONSTANT8 ? !rd_u8(&r, &value) : !rd_u16(&r, &value)) {
                        return 0;
                    }
                    set_operand(&entry->value, XXBYTE_OPERAND_INDEX, entry->type == XXBYTE_AVM1_PUSH_CONSTANT8 ? 1u : 2u, (int64_t)value);
                    entry->value.kind = (uint8_t)XXBYTE_INDEX_CONSTANT;
                    break;
                case XXBYTE_AVM1_PUSH_NULL:
                case XXBYTE_AVM1_PUSH_UNDEFINED: set_operand(&entry->value, XXBYTE_OPERAND_IMMEDIATE, 0, 0); break;
                default: set_operand(&entry->value, XXBYTE_OPERAND_IMMEDIATE, 0, (int64_t)entry->type); break;
            }
            entry->value.extra = entry->type;
            break;
        case XXBYTE_AVM1_ID_DEFINEFUNCTION2:
            if (!rd_u8(&r, &value)) {
                return 0;
            }
            set_operand(&entry->reg, XXBYTE_OPERAND_REGISTER, 1, (int64_t)value);
            /* fall through */
        case XXBYTE_AVM1_ID_CONSTANTPOOL:
        case XXBYTE_AVM1_ID_DEFINEFUNCTION:
            if (!rd_str(&r, &offset, &length)) {
                return 0;
            }
            set_string(&entry->value, offset, length);
            break;
        default: return 0;
    }
    entry->size = r.pos - pos;
    return 1;
}

static int has_table(uint32_t id)
{
    return id == XXBYTE_AVM1_ID_PUSH || id == XXBYTE_AVM1_ID_CONSTANTPOOL || id == XXBYTE_AVM1_ID_DEFINEFUNCTION || id == XXBYTE_AVM1_ID_DEFINEFUNCTION2;
}

/* ---- decoder ----------------------------------------------------------- */

static xxbyte_status avm1_check_mode(uint32_t mode)
{
    return mode == XXBYTE_AVM1_MODE_DEFAULT ? XXBYTE_STATUS_OK : XXBYTE_STATUS_UNSUPPORTED_MODE;
}

static xxbyte_operand *add_imm(xxbyte_instruction *instruction, uint32_t size, uint32_t value)
{
    return xxbyte_add_operand(instruction, XXBYTE_OPERAND_IMMEDIATE, size, (int64_t)value);
}

static void add_str(xxbyte_instruction *instruction, uint32_t offset, uint32_t length)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BYTES, length, (int64_t)offset);
    if (operand != NULL) {
        operand->flags = XXBYTE_AVM1_OPERAND_FLAG_STRING;
    }
}

/* A block end: next + size. */
static void add_end(xxbyte_instruction *instruction, uint64_t next, uint32_t size)
{
    xxbyte_operand *operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, 2, (int64_t)(next + size));
    if (operand != NULL) {
        operand->flags = XXBYTE_AVM1_OPERAND_FLAG_BLOCK_END;
        operand->extra = size;
    }
}

/* The parameters of definefunction(2), the strings of constantpool. */
static int read_table(xxbyte_instruction *instruction, avm1_reader *r, uint32_t count, int lenient)
{
    xxbyte_avm1_entry entry;
    uint32_t n = 0;

    instruction->table_offset = r->pos;
    while (n < count) {
        if (!read_entry(r->code, instruction->id, r->pos, r->end, n, &entry)) {
            if (lenient) {
                break;
            }
            return 0;
        }
        r->pos += entry.size;
        ++n;
    }
    instruction->table_count = n;
    return 1;
}

static xxbyte_status avm1_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *instruction)
{
    uint8_t opcode = code[0];
    uint32_t id = xxbyte_avm1_opcode_id(opcode);
    uint32_t length;
    uint32_t total;
    uint64_t next;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    avm1_reader r;

    (void)mode;
    (void)context;
    instruction->opcode = opcode;
    instruction->id = id;
    instruction->mnemonic = avm1_infos[id].name;
    instruction->group = avm1_infos[id].group;
    if (opcode < 0x80) {
        instruction->size = 1;
        if (id == XXBYTE_AVM1_ID_UNKNOWN) {
            add_imm(instruction, 1, opcode);
        }
        return XXBYTE_STATUS_OK;
    }
    if (code_size < 3) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    length = xxbyte_u16le(code + 1);
    total = 3u + length;
    if (total > code_size) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    instruction->size = total;
    next = address + total;
    r.code = code;
    r.pos = 3;
    r.end = total;
    switch (id) {
        case XXBYTE_AVM1_ID_GOTOFRAME:
            if (!rd_u16(&r, &a)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_imm(instruction, 2, a);
            break;
        case XXBYTE_AVM1_ID_GETURL:
            if (!rd_str(&r, &a, &b) || !rd_str(&r, &c, &d)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_str(instruction, a, b);
            add_str(instruction, c, d);
            break;
        case XXBYTE_AVM1_ID_STOREREGISTER:
            if (!rd_u8(&r, &a)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            xxbyte_add_operand(instruction, XXBYTE_OPERAND_REGISTER, 1, (int64_t)a);
            break;
        case XXBYTE_AVM1_ID_CONSTANTPOOL:
            if (!rd_u16(&r, &a)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_imm(instruction, 2, a);
            read_table(instruction, &r, a, 1);
            break;
        case XXBYTE_AVM1_ID_STRICTMODE:
        case XXBYTE_AVM1_ID_WAITFORFRAME2:
        case XXBYTE_AVM1_ID_GETURL2:
            if (!rd_u8(&r, &a)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_imm(instruction, 1, a);
            break;
        case XXBYTE_AVM1_ID_WAITFORFRAME:
            if (!rd_u16(&r, &a) || !rd_u8(&r, &b)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_imm(instruction, 2, a);
            add_imm(instruction, 1, b);
            break;
        case XXBYTE_AVM1_ID_SETTARGET:
        case XXBYTE_AVM1_ID_GOTOLABEL:
            if (!rd_str(&r, &a, &b)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_str(instruction, a, b);
            break;
        case XXBYTE_AVM1_ID_DEFINEFUNCTION:
            if (!rd_str(&r, &a, &b) || !rd_u16(&r, &c)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_str(instruction, a, b);
            add_imm(instruction, 2, c);
            if (!read_table(instruction, &r, c, 0) || !rd_u16(&r, &d)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_end(instruction, next, d);
            break;
        case XXBYTE_AVM1_ID_DEFINEFUNCTION2: {
            uint32_t registers;
            uint32_t flags;
            if (!rd_str(&r, &a, &b) || !rd_u16(&r, &c) || !rd_u8(&r, &registers) || !rd_u16(&r, &flags)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_str(instruction, a, b);
            add_imm(instruction, 2, c);
            add_imm(instruction, 1, registers);
            add_imm(instruction, 2, flags);
            if (!read_table(instruction, &r, c, 0) || !rd_u16(&r, &d)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_end(instruction, next, d);
            break;
        }
        case XXBYTE_AVM1_ID_TRY: {
            uint32_t flags;
            if (length < 7) {
                break; /* Ruffle: a bogus try with no blocks */
            }
            rd_u8(&r, &flags);
            rd_u16(&r, &a);
            rd_u16(&r, &b);
            rd_u16(&r, &c);
            add_imm(instruction, 1, flags);
            add_end(instruction, next, a);
            add_end(instruction, next + a, b);
            add_end(instruction, next + a + b, c);
            if ((flags & XXBYTE_AVM1_TRY_CATCH_IN_REGISTER) != 0) {
                if (!rd_u8(&r, &d)) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                xxbyte_add_operand(instruction, XXBYTE_OPERAND_REGISTER, 1, (int64_t)d);
            } else {
                if (!rd_str(&r, &a, &b)) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                add_str(instruction, a, b);
            }
            break;
        }
        case XXBYTE_AVM1_ID_WITH:
            if (!rd_u16(&r, &a)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_end(instruction, next, a);
            break;
        case XXBYTE_AVM1_ID_PUSH: {
            xxbyte_avm1_entry entry;
            uint32_t n = 0;
            instruction->table_offset = 3;
            while (r.pos < r.end) {
                if (!read_entry(code, id, r.pos, r.end, n, &entry)) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                r.pos += entry.size;
                ++n;
            }
            instruction->table_count = n;
            break;
        }
        case XXBYTE_AVM1_ID_JUMP:
        case XXBYTE_AVM1_ID_IF: {
            xxbyte_operand *operand;
            if (!rd_u16(&r, &a)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            operand = xxbyte_add_operand(instruction, XXBYTE_OPERAND_BRANCH, 2, (int64_t)(next + (uint64_t)xxbyte_sext(a, 16)));
            if (operand != NULL) {
                operand->extra = a;
            }
            break;
        }
        case XXBYTE_AVM1_ID_GOTOFRAME2:
            if (!rd_u8(&r, &a)) {
                return XXBYTE_STATUS_INVALID_INSTRUCTION;
            }
            add_imm(instruction, 1, a);
            if ((a & XXBYTE_AVM1_GOTOFRAME2_SCENE_BIAS) != 0) {
                if (!rd_u16(&r, &b)) {
                    return XXBYTE_STATUS_INVALID_INSTRUCTION;
                }
                add_imm(instruction, 2, b);
            }
            break;
        case XXBYTE_AVM1_ID_UNKNOWN:
            add_imm(instruction, 1, opcode);
            xxbyte_add_operand(instruction, XXBYTE_OPERAND_BYTES, length, 3);
            break;
        default: break;
    }
    return XXBYTE_STATUS_OK;
}

/* ---- tables ------------------------------------------------------------ */

static int is_table_instruction(const xxbyte_instruction *instruction)
{
    return instruction != NULL && instruction->family == XXBYTE_FAMILY_AVM1 && instruction->code != NULL && instruction->size != 0 && has_table(instruction->id);
}

xxbyte_status xxbyte_avm1_first_entry(const xxbyte_instruction *instruction, xxbyte_avm1_entry *entry)
{
    if (entry == NULL || !is_table_instruction(instruction)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (instruction->table_count == 0) {
        return XXBYTE_STATUS_END_OF_INPUT;
    }
    if (instruction->table_offset > instruction->size || !read_entry(instruction->code, instruction->id, instruction->table_offset, instruction->size, 0, entry)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm1_next_entry(const xxbyte_instruction *instruction, xxbyte_avm1_entry *entry)
{
    uint32_t pos;

    if (entry == NULL || !is_table_instruction(instruction)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (entry->index + 1u >= instruction->table_count) {
        return XXBYTE_STATUS_END_OF_INPUT;
    }
    pos = entry->offset + entry->size;
    if (pos < entry->offset || pos > instruction->size || !read_entry(instruction->code, instruction->id, pos, instruction->size, entry->index + 1u, entry)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm1_entry_at(const xxbyte_instruction *instruction, uint32_t index, xxbyte_avm1_entry *entry)
{
    xxbyte_status status;

    if (entry == NULL || !is_table_instruction(instruction)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (index >= instruction->table_count) {
        return XXBYTE_STATUS_END_OF_INPUT;
    }
    status = xxbyte_avm1_first_entry(instruction, entry);
    while (status == XXBYTE_STATUS_OK && entry->index < index) {
        status = xxbyte_avm1_next_entry(instruction, entry);
    }
    return status;
}

/* ---- text -------------------------------------------------------------- */

/* The escaped form of c, NUL-terminated; returns its length. */
static size_t escape_byte(uint8_t c, char text[5])
{
    static const char hex[] = "0123456789ABCDEF";

    switch (c) {
        case '"':
        case '\\':
            text[0] = '\\';
            text[1] = (char)c;
            text[2] = '\0';
            return 2;
        case '\n': memcpy(text, "\\n", 3); return 2;
        case '\r': memcpy(text, "\\r", 3); return 2;
        case '\t': memcpy(text, "\\t", 3); return 2;
        default: break;
    }
    if (c >= 0x20 && c <= 0x7E) {
        text[0] = (char)c;
        text[1] = '\0';
        return 1;
    }
    text[0] = '\\';
    text[1] = 'x';
    text[2] = hex[c >> 4];
    text[3] = hex[c & 0x0F];
    text[4] = '\0';
    return 4;
}

int xxbyte_avm1_append_string(xx_buf_t *out, const uint8_t *text, size_t length)
{
    size_t i;
    char escaped[5];

    if (!xx_buf_append_char(out, '"')) {
        return 0;
    }
    for (i = 0; i < length; ++i) {
        escape_byte(text[i], escaped);
        if (!xx_buf_append_str(out, escaped)) {
            return 0;
        }
    }
    return xx_buf_append_char(out, '"') ? 1 : 0;
}

static int fmt_string(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, const xxbyte_operand *operand)
{
    uint64_t offset = (uint64_t)operand->value;

    if (offset > instruction->size || operand->size > instruction->size - offset) {
        return 0;
    }
    return xxbyte_avm1_append_string(fmt->out, instruction->code + offset, operand->size);
}

static int fmt_target(xxbyte_fmt *fmt, int64_t target)
{
    if (target < 0) {
        return xxbyte_fmt_printf(fmt, "-0x%04llx", (unsigned long long)(0u - (uint64_t)target));
    }
    return xxbyte_fmt_printf(fmt, "0x%04llx", (unsigned long long)target);
}

static int fmt_branch(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, uint32_t index)
{
    int resolved = xxbyte_fmt_resolve(fmt, instruction, index);

    if (resolved != 0) {
        return resolved > 0;
    }
    return fmt_target(fmt, instruction->operands[index].value);
}

/* Adds ".0" when the number appended since mark has only digits and '-'. */
static int fmt_point(xxbyte_fmt *fmt, size_t mark)
{
    size_t i;

    for (i = mark; i < fmt->out->size; ++i) {
        char c = fmt->out->data[i];
        if (c != '-' && (c < '0' || c > '9')) {
            return 1;
        }
    }
    return xxbyte_fmt_str(fmt, ".0");
}

static int fmt_value(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, const xxbyte_avm1_entry *entry)
{
    const xxbyte_operand *value = &entry->value;
    size_t mark = xxbyte_fmt_mark(fmt);

    switch (entry->type) {
        case XXBYTE_AVM1_PUSH_STRING: return fmt_string(fmt, instruction, value);
        case XXBYTE_AVM1_PUSH_FLOAT: {
            uint32_t bits = (uint32_t)value->value;
            float number;
            memcpy(&number, &bits, sizeof(number));
            return xxbyte_fmt_float(fmt, number) && fmt_point(fmt, mark) && xxbyte_fmt_char(fmt, 'f');
        }
        case XXBYTE_AVM1_PUSH_NULL: return xxbyte_fmt_str(fmt, "null");
        case XXBYTE_AVM1_PUSH_UNDEFINED: return xxbyte_fmt_str(fmt, "undefined");
        case XXBYTE_AVM1_PUSH_REGISTER: return xxbyte_fmt_printf(fmt, "r%u", (unsigned)value->value);
        case XXBYTE_AVM1_PUSH_BOOLEAN: return xxbyte_fmt_str(fmt, value->value != 0 ? "true" : "false");
        case XXBYTE_AVM1_PUSH_DOUBLE: {
            uint64_t bits = (uint64_t)value->value;
            double number;
            memcpy(&number, &bits, sizeof(number));
            return xxbyte_fmt_double(fmt, number) && fmt_point(fmt, mark);
        }
        case XXBYTE_AVM1_PUSH_INTEGER: return xxbyte_fmt_printf(fmt, "%lld", (long long)value->value);
        case XXBYTE_AVM1_PUSH_CONSTANT8:
        case XXBYTE_AVM1_PUSH_CONSTANT16: {
            int resolved = xxbyte_fmt_resolve_entry(fmt, instruction, value);
            if (resolved != 0) {
                return resolved > 0;
            }
            return xxbyte_fmt_printf(fmt, entry->type == XXBYTE_AVM1_PUSH_CONSTANT8 ? "constant@%u" : "constant16@%u", (unsigned)value->value);
        }
        default: return xxbyte_fmt_printf(fmt, "invalid %u", (unsigned)entry->type);
    }
}

static int fmt_register(xxbyte_fmt *fmt, const xxbyte_operand *operand)
{
    return xxbyte_fmt_printf(fmt, "r%u", (unsigned)operand->value);
}

/* Flag names in bit order, '|'-separated, the remaining bits in hex. */
static int fmt_flags(xxbyte_fmt *fmt, uint32_t flags, const char *const *names, unsigned count, const char *rest_format)
{
    unsigned bit;
    int first = 1;

    for (bit = 0; bit < count; ++bit) {
        if ((flags & (1u << bit)) != 0 && names[bit] != NULL) {
            if ((!first && !xxbyte_fmt_char(fmt, '|')) || !xxbyte_fmt_str(fmt, names[bit])) {
                return 0;
            }
            flags &= ~(1u << bit);
            first = 0;
        }
    }
    if (flags != 0) {
        if ((!first && !xxbyte_fmt_char(fmt, '|')) || !xxbyte_fmt_printf(fmt, rest_format, flags)) {
            return 0;
        }
        first = 0;
    }
    return first ? xxbyte_fmt_char(fmt, '0') : 1;
}

static const char *const function_flag_names[] = {
    "preload_this", "suppress_this", "preload_arguments", "suppress_arguments", "preload_super", "suppress_super", "preload_root", "preload_parent", "preload_global",
};

static const char *const geturl2_flag_names[] = {NULL, NULL, NULL, NULL, NULL, NULL, "load_target", "load_variables"};

static int fmt_geturl2(xxbyte_fmt *fmt, uint32_t flags)
{
    static const char *const methods[] = {NULL, "get", "post", "method3"};
    uint32_t method = flags & XXBYTE_AVM1_GETURL2_METHOD_MASK;

    if (method != 0) {
        if (!xxbyte_fmt_str(fmt, methods[method])) {
            return 0;
        }
        flags &= ~XXBYTE_AVM1_GETURL2_METHOD_MASK;
        if (flags == 0) {
            return 1;
        }
        if (!xxbyte_fmt_char(fmt, '|')) {
            return 0;
        }
    }
    return fmt_flags(fmt, flags, geturl2_flag_names, 8, "0x%02x");
}

static int fmt_table(xxbyte_fmt *fmt, const xxbyte_instruction *instruction, int brackets)
{
    xxbyte_avm1_entry entry;
    xxbyte_status status = xxbyte_avm1_first_entry(instruction, &entry);

    if (brackets && !xxbyte_fmt_char(fmt, '[')) {
        return 0;
    }
    while (status == XXBYTE_STATUS_OK) {
        if (entry.index != 0 && !xxbyte_fmt_str(fmt, ", ")) {
            return 0;
        }
        if (instruction->id == XXBYTE_AVM1_ID_PUSH) {
            if (!fmt_value(fmt, instruction, &entry)) {
                return 0;
            }
        } else {
            if (entry.reg.type == XXBYTE_OPERAND_REGISTER && entry.reg.value != 0 && (!fmt_register(fmt, &entry.reg) || !xxbyte_fmt_char(fmt, ' '))) {
                return 0;
            }
            if (!fmt_string(fmt, instruction, &entry.value)) {
                return 0;
            }
        }
        status = xxbyte_avm1_next_entry(instruction, &entry);
    }
    if (status != XXBYTE_STATUS_END_OF_INPUT) {
        return 0;
    }
    return !brackets || xxbyte_fmt_char(fmt, ']');
}

static int fmt_try(xxbyte_fmt *fmt, const xxbyte_instruction *instruction)
{
    const xxbyte_operand *ops = instruction->operands;
    uint32_t flags;

    if (instruction->operand_count < 5) {
        return 1;
    }
    flags = (uint32_t)ops[0].value;
    if (!xxbyte_fmt_char(fmt, ' ') || !fmt_branch(fmt, instruction, 1)) {
        return 0;
    }
    if ((flags & XXBYTE_AVM1_TRY_CATCH) != 0) {
        if (!xxbyte_fmt_str(fmt, ", catch ")) {
            return 0;
        }
        if (ops[4].type == XXBYTE_OPERAND_REGISTER ? !fmt_register(fmt, &ops[4]) : !fmt_string(fmt, instruction, &ops[4])) {
            return 0;
        }
        if (!xxbyte_fmt_char(fmt, ' ') || !fmt_branch(fmt, instruction, 2)) {
            return 0;
        }
    }
    if ((flags & XXBYTE_AVM1_TRY_FINALLY) != 0 && (!xxbyte_fmt_str(fmt, ", finally ") || !fmt_branch(fmt, instruction, 3))) {
        return 0;
    }
    if ((flags & 0xF8u) != 0 && !xxbyte_fmt_printf(fmt, ", flags 0x%02x", flags)) {
        return 0;
    }
    return 1;
}

static int avm1_format_operands(const xxbyte_instruction *instruction, xxbyte_fmt *fmt)
{
    const xxbyte_operand *ops = instruction->operands;

    switch (instruction->id) {
        case XXBYTE_AVM1_ID_GOTOFRAME:
        case XXBYTE_AVM1_ID_STRICTMODE:
        case XXBYTE_AVM1_ID_WAITFORFRAME2: return xxbyte_fmt_printf(fmt, " %u", (unsigned)ops[0].value);
        case XXBYTE_AVM1_ID_WAITFORFRAME: return xxbyte_fmt_printf(fmt, " %u, %u", (unsigned)ops[0].value, (unsigned)ops[1].value);
        case XXBYTE_AVM1_ID_GETURL:
            return xxbyte_fmt_char(fmt, ' ') && fmt_string(fmt, instruction, &ops[0]) && xxbyte_fmt_str(fmt, ", ") && fmt_string(fmt, instruction, &ops[1]);
        case XXBYTE_AVM1_ID_SETTARGET:
        case XXBYTE_AVM1_ID_GOTOLABEL: return xxbyte_fmt_char(fmt, ' ') && fmt_string(fmt, instruction, &ops[0]);
        case XXBYTE_AVM1_ID_STOREREGISTER: return xxbyte_fmt_char(fmt, ' ') && fmt_register(fmt, &ops[0]);
        case XXBYTE_AVM1_ID_CONSTANTPOOL:
        case XXBYTE_AVM1_ID_PUSH:
            if (instruction->table_count == 0) {
                return 1;
            }
            return xxbyte_fmt_char(fmt, ' ') && fmt_table(fmt, instruction, 0);
        case XXBYTE_AVM1_ID_DEFINEFUNCTION:
            return xxbyte_fmt_char(fmt, ' ') && fmt_string(fmt, instruction, &ops[0]) && xxbyte_fmt_str(fmt, ", ") && fmt_table(fmt, instruction, 1) &&
                   xxbyte_fmt_str(fmt, ", ") && fmt_branch(fmt, instruction, 2);
        case XXBYTE_AVM1_ID_DEFINEFUNCTION2:
            return xxbyte_fmt_char(fmt, ' ') && fmt_string(fmt, instruction, &ops[0]) && xxbyte_fmt_printf(fmt, ", %u, ", (unsigned)ops[2].value) &&
                   fmt_flags(fmt, (uint32_t)ops[3].value, function_flag_names, 9, "0x%04x") && xxbyte_fmt_str(fmt, ", ") && fmt_table(fmt, instruction, 1) &&
                   xxbyte_fmt_str(fmt, ", ") && fmt_branch(fmt, instruction, 4);
        case XXBYTE_AVM1_ID_TRY: return fmt_try(fmt, instruction);
        case XXBYTE_AVM1_ID_WITH:
        case XXBYTE_AVM1_ID_JUMP:
        case XXBYTE_AVM1_ID_IF: return xxbyte_fmt_char(fmt, ' ') && fmt_branch(fmt, instruction, 0);
        case XXBYTE_AVM1_ID_GETURL2: return xxbyte_fmt_char(fmt, ' ') && fmt_geturl2(fmt, (uint32_t)ops[0].value);
        case XXBYTE_AVM1_ID_GOTOFRAME2: {
            uint32_t flags = (uint32_t)ops[0].value;
            if (!xxbyte_fmt_str(fmt, (flags & XXBYTE_AVM1_GOTOFRAME2_PLAY) != 0 ? " play" : " stop")) {
                return 0;
            }
            if ((flags & 0xFCu) != 0 && !xxbyte_fmt_printf(fmt, "|0x%02x", flags & 0xFCu)) {
                return 0;
            }
            return instruction->operand_count < 2 || xxbyte_fmt_printf(fmt, ", %u", (unsigned)ops[1].value);
        }
        case XXBYTE_AVM1_ID_UNKNOWN:
            if (!xxbyte_fmt_printf(fmt, " 0x%02x", (unsigned)ops[0].value)) {
                return 0;
            }
            return instruction->operand_count < 2 || xxbyte_fmt_printf(fmt, ", length %u", (unsigned)ops[1].size);
        default: return 1;
    }
}

/* ---- constant pool and resolver ---------------------------------------- */

void xxbyte_avm1_pool_init(xxbyte_avm1_pool *pool)
{
    if (pool != NULL) {
        memset(pool, 0, sizeof(*pool));
    }
}

void xxbyte_avm1_pool_clear(xxbyte_avm1_pool *pool)
{
    if (pool != NULL) {
        pool->code = NULL;
        pool->count = 0;
    }
}

void xxbyte_avm1_pool_free(xxbyte_avm1_pool *pool)
{
    if (pool != NULL) {
        xx_mem_free(pool->offsets);
        xx_mem_free(pool->lengths);
        memset(pool, 0, sizeof(*pool));
    }
}

xxbyte_status xxbyte_avm1_pool_set(xxbyte_avm1_pool *pool, const xxbyte_instruction *constantpool)
{
    xxbyte_avm1_entry entry;
    xxbyte_status status;
    uint32_t count;

    if (pool == NULL || constantpool == NULL || constantpool->id != XXBYTE_AVM1_ID_CONSTANTPOOL || !is_table_instruction(constantpool)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    count = constantpool->table_count;
    if (count > pool->capacity) {
        uint32_t *offsets = (uint32_t *)xx_mem_alloc((size_t)count * sizeof(uint32_t));
        uint32_t *lengths = (uint32_t *)xx_mem_alloc((size_t)count * sizeof(uint32_t));
        if (offsets == NULL || lengths == NULL) {
            xx_mem_free(offsets);
            xx_mem_free(lengths);
            xxbyte_avm1_pool_clear(pool);
            return XXBYTE_STATUS_OUT_OF_MEMORY;
        }
        xx_mem_free(pool->offsets);
        xx_mem_free(pool->lengths);
        pool->offsets = offsets;
        pool->lengths = lengths;
        pool->capacity = count;
    }
    xxbyte_avm1_pool_clear(pool);
    status = xxbyte_avm1_first_entry(constantpool, &entry);
    while (status == XXBYTE_STATUS_OK) {
        pool->offsets[entry.index] = (uint32_t)entry.value.value;
        pool->lengths[entry.index] = entry.value.size;
        status = xxbyte_avm1_next_entry(constantpool, &entry);
    }
    if (status != XXBYTE_STATUS_END_OF_INPUT) {
        return status;
    }
    pool->code = constantpool->code;
    pool->count = count;
    return XXBYTE_STATUS_OK;
}

xxbyte_status xxbyte_avm1_pool_string(const xxbyte_avm1_pool *pool, uint32_t index, const uint8_t **text, uint32_t *length)
{
    if (text != NULL) {
        *text = NULL;
    }
    if (length != NULL) {
        *length = 0;
    }
    if (pool == NULL || text == NULL || length == NULL || pool->code == NULL || index >= pool->count) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *text = pool->code + pool->offsets[index];
    *length = pool->lengths[index];
    return XXBYTE_STATUS_OK;
}

size_t xxbyte_avm1_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size)
{
    const xxbyte_avm1_pool *pool = (const xxbyte_avm1_pool *)context;
    const xxbyte_operand *operand;
    const uint8_t *text;
    uint32_t length;
    uint32_t i;
    size_t n = 0;
    char escaped[5];

    if (instruction == NULL || buffer == NULL || buffer_size < 8 || operand_index >= instruction->operand_count) {
        return 0;
    }
    operand = &instruction->operands[operand_index];
    if (operand->type != XXBYTE_OPERAND_INDEX || operand->kind != XXBYTE_INDEX_CONSTANT || operand->value < 0 || operand->value > UINT32_MAX ||
        xxbyte_avm1_pool_string(pool, (uint32_t)operand->value, &text, &length) != XXBYTE_STATUS_OK) {
        return 0;
    }
    buffer[n++] = '"';
    for (i = 0; i < length; ++i) {
        size_t width = escape_byte(text[i], escaped);
        /* Room for this byte and the closing quote, or for '...' and it. */
        size_t need = i + 1u < length ? width + 5u : width + 2u;
        if (n + need > buffer_size) {
            memcpy(buffer + n, "...", 3);
            n += 3;
            break;
        }
        memcpy(buffer + n, escaped, width);
        n += width;
    }
    buffer[n++] = '"';
    buffer[n] = '\0';
    return n;
}

xxbyte_status xxbyte_avm1_disasm_actions(const uint8_t *code, size_t code_size, uint64_t address, uint32_t listing_flags, struct xx_buf_s *out)
{
    xxbyte_avm1_pool pool;
    size_t offset = 0;
    xxbyte_status status = XXBYTE_STATUS_OK;

    if (out == NULL || (code == NULL && code_size != 0) || (listing_flags & ~(uint32_t)XXBYTE_LISTING_KNOWN_FLAGS) != 0) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    xxbyte_avm1_pool_init(&pool);
    while (offset < code_size) {
        xxbyte_instruction instruction;
        size_t size = 1;

        if (xxbyte_decode(XXBYTE_FAMILY_AVM1, XXBYTE_AVM1_MODE_DEFAULT, code + offset, code_size - offset, address + offset, &instruction) == XXBYTE_STATUS_OK) {
            size = instruction.size;
            if (instruction.id == XXBYTE_AVM1_ID_CONSTANTPOOL) {
                status = xxbyte_avm1_pool_set(&pool, &instruction);
                if (status != XXBYTE_STATUS_OK) {
                    break;
                }
            }
        }
        status = xxbyte_disasm_buf_ex(XXBYTE_FAMILY_AVM1, XXBYTE_AVM1_MODE_DEFAULT, NULL, code + offset, size, address + offset, listing_flags, xxbyte_avm1_resolve,
                                      &pool, out);
        if (status != XXBYTE_STATUS_OK) {
            break;
        }
        offset += size;
    }
    xxbyte_avm1_pool_free(&pool);
    return status;
}

const xxbyte_family_ops xxbyte_avm1_ops = {
    XXBYTE_FAMILY_AVM1, XXBYTE_AVM1_MODE_DEFAULT, avm1_check_mode, avm1_decode, NULL, avm1_format_operands, NULL, xxbyte_avm1_list_file,
};

#else
typedef int xxbyte_avm1_disabled;
#endif
