#ifndef XXBYTE_XXBYTE_JAVA_H
#define XXBYTE_XXBYTE_JAVA_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_JAVA
#error "the JVM decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* JVM bytecode has a single encoding. Addresses must be offsets from the
 * start of the method's code array, because tableswitch and lookupswitch
 * padding depends on them. */
#define XXBYTE_JAVA_MODE_DEFAULT 0u

/*
 * Mnemonic identifiers (xxbyte_instruction.id), JVMS SE 25 chapter 6: the
 * opcodes 0x00 to 0xC9 in opcode order without the wide prefix (0xC4), then
 * the reserved breakpoint, impdep1 and impdep2, then the twelve wide forms
 * in the order of the opcode they modify. A wide instruction is decoded
 * together with the opcode it modifies and named like javap names it
 * ("iload_w", "iinc_w"). The name is the mnemonic with every run of
 * non-alphanumeric characters replaced by '_'. Valid ids are 1 to
 * XXBYTE_JAVA_ID_COUNT - 1.
 */
typedef enum xxbyte_java_id {
    XXBYTE_JAVA_ID_INVALID = 0,
    XXBYTE_JAVA_ID_NOP = 1,
    XXBYTE_JAVA_ID_ACONST_NULL = 2,
    XXBYTE_JAVA_ID_ICONST_M1 = 3,
    XXBYTE_JAVA_ID_ICONST_0 = 4,
    XXBYTE_JAVA_ID_ICONST_1 = 5,
    XXBYTE_JAVA_ID_ICONST_2 = 6,
    XXBYTE_JAVA_ID_ICONST_3 = 7,
    XXBYTE_JAVA_ID_ICONST_4 = 8,
    XXBYTE_JAVA_ID_ICONST_5 = 9,
    XXBYTE_JAVA_ID_LCONST_0 = 10,
    XXBYTE_JAVA_ID_LCONST_1 = 11,
    XXBYTE_JAVA_ID_FCONST_0 = 12,
    XXBYTE_JAVA_ID_FCONST_1 = 13,
    XXBYTE_JAVA_ID_FCONST_2 = 14,
    XXBYTE_JAVA_ID_DCONST_0 = 15,
    XXBYTE_JAVA_ID_DCONST_1 = 16,
    XXBYTE_JAVA_ID_BIPUSH = 17,
    XXBYTE_JAVA_ID_SIPUSH = 18,
    XXBYTE_JAVA_ID_LDC = 19,
    XXBYTE_JAVA_ID_LDC_W = 20,
    XXBYTE_JAVA_ID_LDC2_W = 21,
    XXBYTE_JAVA_ID_ILOAD = 22,
    XXBYTE_JAVA_ID_LLOAD = 23,
    XXBYTE_JAVA_ID_FLOAD = 24,
    XXBYTE_JAVA_ID_DLOAD = 25,
    XXBYTE_JAVA_ID_ALOAD = 26,
    XXBYTE_JAVA_ID_ILOAD_0 = 27,
    XXBYTE_JAVA_ID_ILOAD_1 = 28,
    XXBYTE_JAVA_ID_ILOAD_2 = 29,
    XXBYTE_JAVA_ID_ILOAD_3 = 30,
    XXBYTE_JAVA_ID_LLOAD_0 = 31,
    XXBYTE_JAVA_ID_LLOAD_1 = 32,
    XXBYTE_JAVA_ID_LLOAD_2 = 33,
    XXBYTE_JAVA_ID_LLOAD_3 = 34,
    XXBYTE_JAVA_ID_FLOAD_0 = 35,
    XXBYTE_JAVA_ID_FLOAD_1 = 36,
    XXBYTE_JAVA_ID_FLOAD_2 = 37,
    XXBYTE_JAVA_ID_FLOAD_3 = 38,
    XXBYTE_JAVA_ID_DLOAD_0 = 39,
    XXBYTE_JAVA_ID_DLOAD_1 = 40,
    XXBYTE_JAVA_ID_DLOAD_2 = 41,
    XXBYTE_JAVA_ID_DLOAD_3 = 42,
    XXBYTE_JAVA_ID_ALOAD_0 = 43,
    XXBYTE_JAVA_ID_ALOAD_1 = 44,
    XXBYTE_JAVA_ID_ALOAD_2 = 45,
    XXBYTE_JAVA_ID_ALOAD_3 = 46,
    XXBYTE_JAVA_ID_IALOAD = 47,
    XXBYTE_JAVA_ID_LALOAD = 48,
    XXBYTE_JAVA_ID_FALOAD = 49,
    XXBYTE_JAVA_ID_DALOAD = 50,
    XXBYTE_JAVA_ID_AALOAD = 51,
    XXBYTE_JAVA_ID_BALOAD = 52,
    XXBYTE_JAVA_ID_CALOAD = 53,
    XXBYTE_JAVA_ID_SALOAD = 54,
    XXBYTE_JAVA_ID_ISTORE = 55,
    XXBYTE_JAVA_ID_LSTORE = 56,
    XXBYTE_JAVA_ID_FSTORE = 57,
    XXBYTE_JAVA_ID_DSTORE = 58,
    XXBYTE_JAVA_ID_ASTORE = 59,
    XXBYTE_JAVA_ID_ISTORE_0 = 60,
    XXBYTE_JAVA_ID_ISTORE_1 = 61,
    XXBYTE_JAVA_ID_ISTORE_2 = 62,
    XXBYTE_JAVA_ID_ISTORE_3 = 63,
    XXBYTE_JAVA_ID_LSTORE_0 = 64,
    XXBYTE_JAVA_ID_LSTORE_1 = 65,
    XXBYTE_JAVA_ID_LSTORE_2 = 66,
    XXBYTE_JAVA_ID_LSTORE_3 = 67,
    XXBYTE_JAVA_ID_FSTORE_0 = 68,
    XXBYTE_JAVA_ID_FSTORE_1 = 69,
    XXBYTE_JAVA_ID_FSTORE_2 = 70,
    XXBYTE_JAVA_ID_FSTORE_3 = 71,
    XXBYTE_JAVA_ID_DSTORE_0 = 72,
    XXBYTE_JAVA_ID_DSTORE_1 = 73,
    XXBYTE_JAVA_ID_DSTORE_2 = 74,
    XXBYTE_JAVA_ID_DSTORE_3 = 75,
    XXBYTE_JAVA_ID_ASTORE_0 = 76,
    XXBYTE_JAVA_ID_ASTORE_1 = 77,
    XXBYTE_JAVA_ID_ASTORE_2 = 78,
    XXBYTE_JAVA_ID_ASTORE_3 = 79,
    XXBYTE_JAVA_ID_IASTORE = 80,
    XXBYTE_JAVA_ID_LASTORE = 81,
    XXBYTE_JAVA_ID_FASTORE = 82,
    XXBYTE_JAVA_ID_DASTORE = 83,
    XXBYTE_JAVA_ID_AASTORE = 84,
    XXBYTE_JAVA_ID_BASTORE = 85,
    XXBYTE_JAVA_ID_CASTORE = 86,
    XXBYTE_JAVA_ID_SASTORE = 87,
    XXBYTE_JAVA_ID_POP = 88,
    XXBYTE_JAVA_ID_POP2 = 89,
    XXBYTE_JAVA_ID_DUP = 90,
    XXBYTE_JAVA_ID_DUP_X1 = 91,
    XXBYTE_JAVA_ID_DUP_X2 = 92,
    XXBYTE_JAVA_ID_DUP2 = 93,
    XXBYTE_JAVA_ID_DUP2_X1 = 94,
    XXBYTE_JAVA_ID_DUP2_X2 = 95,
    XXBYTE_JAVA_ID_SWAP = 96,
    XXBYTE_JAVA_ID_IADD = 97,
    XXBYTE_JAVA_ID_LADD = 98,
    XXBYTE_JAVA_ID_FADD = 99,
    XXBYTE_JAVA_ID_DADD = 100,
    XXBYTE_JAVA_ID_ISUB = 101,
    XXBYTE_JAVA_ID_LSUB = 102,
    XXBYTE_JAVA_ID_FSUB = 103,
    XXBYTE_JAVA_ID_DSUB = 104,
    XXBYTE_JAVA_ID_IMUL = 105,
    XXBYTE_JAVA_ID_LMUL = 106,
    XXBYTE_JAVA_ID_FMUL = 107,
    XXBYTE_JAVA_ID_DMUL = 108,
    XXBYTE_JAVA_ID_IDIV = 109,
    XXBYTE_JAVA_ID_LDIV = 110,
    XXBYTE_JAVA_ID_FDIV = 111,
    XXBYTE_JAVA_ID_DDIV = 112,
    XXBYTE_JAVA_ID_IREM = 113,
    XXBYTE_JAVA_ID_LREM = 114,
    XXBYTE_JAVA_ID_FREM = 115,
    XXBYTE_JAVA_ID_DREM = 116,
    XXBYTE_JAVA_ID_INEG = 117,
    XXBYTE_JAVA_ID_LNEG = 118,
    XXBYTE_JAVA_ID_FNEG = 119,
    XXBYTE_JAVA_ID_DNEG = 120,
    XXBYTE_JAVA_ID_ISHL = 121,
    XXBYTE_JAVA_ID_LSHL = 122,
    XXBYTE_JAVA_ID_ISHR = 123,
    XXBYTE_JAVA_ID_LSHR = 124,
    XXBYTE_JAVA_ID_IUSHR = 125,
    XXBYTE_JAVA_ID_LUSHR = 126,
    XXBYTE_JAVA_ID_IAND = 127,
    XXBYTE_JAVA_ID_LAND = 128,
    XXBYTE_JAVA_ID_IOR = 129,
    XXBYTE_JAVA_ID_LOR = 130,
    XXBYTE_JAVA_ID_IXOR = 131,
    XXBYTE_JAVA_ID_LXOR = 132,
    XXBYTE_JAVA_ID_IINC = 133,
    XXBYTE_JAVA_ID_I2L = 134,
    XXBYTE_JAVA_ID_I2F = 135,
    XXBYTE_JAVA_ID_I2D = 136,
    XXBYTE_JAVA_ID_L2I = 137,
    XXBYTE_JAVA_ID_L2F = 138,
    XXBYTE_JAVA_ID_L2D = 139,
    XXBYTE_JAVA_ID_F2I = 140,
    XXBYTE_JAVA_ID_F2L = 141,
    XXBYTE_JAVA_ID_F2D = 142,
    XXBYTE_JAVA_ID_D2I = 143,
    XXBYTE_JAVA_ID_D2L = 144,
    XXBYTE_JAVA_ID_D2F = 145,
    XXBYTE_JAVA_ID_I2B = 146,
    XXBYTE_JAVA_ID_I2C = 147,
    XXBYTE_JAVA_ID_I2S = 148,
    XXBYTE_JAVA_ID_LCMP = 149,
    XXBYTE_JAVA_ID_FCMPL = 150,
    XXBYTE_JAVA_ID_FCMPG = 151,
    XXBYTE_JAVA_ID_DCMPL = 152,
    XXBYTE_JAVA_ID_DCMPG = 153,
    XXBYTE_JAVA_ID_IFEQ = 154,
    XXBYTE_JAVA_ID_IFNE = 155,
    XXBYTE_JAVA_ID_IFLT = 156,
    XXBYTE_JAVA_ID_IFGE = 157,
    XXBYTE_JAVA_ID_IFGT = 158,
    XXBYTE_JAVA_ID_IFLE = 159,
    XXBYTE_JAVA_ID_IF_ICMPEQ = 160,
    XXBYTE_JAVA_ID_IF_ICMPNE = 161,
    XXBYTE_JAVA_ID_IF_ICMPLT = 162,
    XXBYTE_JAVA_ID_IF_ICMPGE = 163,
    XXBYTE_JAVA_ID_IF_ICMPGT = 164,
    XXBYTE_JAVA_ID_IF_ICMPLE = 165,
    XXBYTE_JAVA_ID_IF_ACMPEQ = 166,
    XXBYTE_JAVA_ID_IF_ACMPNE = 167,
    XXBYTE_JAVA_ID_GOTO = 168,
    XXBYTE_JAVA_ID_JSR = 169,
    XXBYTE_JAVA_ID_RET = 170,
    XXBYTE_JAVA_ID_TABLESWITCH = 171,
    XXBYTE_JAVA_ID_LOOKUPSWITCH = 172,
    XXBYTE_JAVA_ID_IRETURN = 173,
    XXBYTE_JAVA_ID_LRETURN = 174,
    XXBYTE_JAVA_ID_FRETURN = 175,
    XXBYTE_JAVA_ID_DRETURN = 176,
    XXBYTE_JAVA_ID_ARETURN = 177,
    XXBYTE_JAVA_ID_RETURN = 178,
    XXBYTE_JAVA_ID_GETSTATIC = 179,
    XXBYTE_JAVA_ID_PUTSTATIC = 180,
    XXBYTE_JAVA_ID_GETFIELD = 181,
    XXBYTE_JAVA_ID_PUTFIELD = 182,
    XXBYTE_JAVA_ID_INVOKEVIRTUAL = 183,
    XXBYTE_JAVA_ID_INVOKESPECIAL = 184,
    XXBYTE_JAVA_ID_INVOKESTATIC = 185,
    XXBYTE_JAVA_ID_INVOKEINTERFACE = 186,
    XXBYTE_JAVA_ID_INVOKEDYNAMIC = 187,
    XXBYTE_JAVA_ID_NEW = 188,
    XXBYTE_JAVA_ID_NEWARRAY = 189,
    XXBYTE_JAVA_ID_ANEWARRAY = 190,
    XXBYTE_JAVA_ID_ARRAYLENGTH = 191,
    XXBYTE_JAVA_ID_ATHROW = 192,
    XXBYTE_JAVA_ID_CHECKCAST = 193,
    XXBYTE_JAVA_ID_INSTANCEOF = 194,
    XXBYTE_JAVA_ID_MONITORENTER = 195,
    XXBYTE_JAVA_ID_MONITOREXIT = 196,
    XXBYTE_JAVA_ID_MULTIANEWARRAY = 197,
    XXBYTE_JAVA_ID_IFNULL = 198,
    XXBYTE_JAVA_ID_IFNONNULL = 199,
    XXBYTE_JAVA_ID_GOTO_W = 200,
    XXBYTE_JAVA_ID_JSR_W = 201,
    XXBYTE_JAVA_ID_BREAKPOINT = 202,
    XXBYTE_JAVA_ID_IMPDEP1 = 203,
    XXBYTE_JAVA_ID_IMPDEP2 = 204,
    XXBYTE_JAVA_ID_ILOAD_W = 205,
    XXBYTE_JAVA_ID_LLOAD_W = 206,
    XXBYTE_JAVA_ID_FLOAD_W = 207,
    XXBYTE_JAVA_ID_DLOAD_W = 208,
    XXBYTE_JAVA_ID_ALOAD_W = 209,
    XXBYTE_JAVA_ID_ISTORE_W = 210,
    XXBYTE_JAVA_ID_LSTORE_W = 211,
    XXBYTE_JAVA_ID_FSTORE_W = 212,
    XXBYTE_JAVA_ID_DSTORE_W = 213,
    XXBYTE_JAVA_ID_ASTORE_W = 214,
    XXBYTE_JAVA_ID_IINC_W = 215,
    XXBYTE_JAVA_ID_RET_W = 216,
    XXBYTE_JAVA_ID_COUNT
} xxbyte_java_id;

/* newarray atype codes (JVMS 6.5, Table 6.5.newarray-A). */
#define XXBYTE_JAVA_T_BOOLEAN 4u
#define XXBYTE_JAVA_T_CHAR 5u
#define XXBYTE_JAVA_T_FLOAT 6u
#define XXBYTE_JAVA_T_DOUBLE 7u
#define XXBYTE_JAVA_T_BYTE 8u
#define XXBYTE_JAVA_T_SHORT 9u
#define XXBYTE_JAVA_T_INT 10u
#define XXBYTE_JAVA_T_LONG 11u

/* The mnemonic of id, or NULL when id is not valid. */
XXBYTE_API const char *xxbyte_java_mnemonic(uint32_t id);

/* The element type name of a newarray atype ("int"), or NULL when atype is
 * not one of XXBYTE_JAVA_T_*. */
XXBYTE_API const char *xxbyte_java_array_type_name(uint32_t atype);

/* A parsed Code attribute (JVMS 4.7.3). */
typedef struct xxbyte_java_code_attribute {
    uint16_t max_stack;
    uint16_t max_locals;
    uint32_t code_length;
    uint32_t code_offset; /* byte offset of code from data[0] */
    uint16_t exception_table_length;
    uint32_t exception_table_offset; /* byte offset from data[0] */
    uint16_t attributes_count;
    uint32_t attributes_offset; /* byte offset from data[0] */
} xxbyte_java_code_attribute;

/*
 * data points at the attribute's info, after attribute_name_index and
 * attribute_length; size may extend past the attribute. All fields are
 * big-endian. Checks that the code, the exception table and every nested
 * attribute header and body fit in size (XXBYTE_STATUS_TRUNCATED otherwise)
 * and that code_length is between 1 and 65535
 * (XXBYTE_STATUS_INVALID_INSTRUCTION otherwise). On failure *attribute is
 * zeroed.
 */
XXBYTE_API xxbyte_status xxbyte_java_parse_code_attribute(const uint8_t *data, size_t size, xxbyte_java_code_attribute *attribute);

/* One exception_table entry of a Code attribute. The pcs are offsets into
 * the code array. */
typedef struct xxbyte_java_exception_handler {
    uint16_t start_pc;
    uint16_t end_pc; /* exclusive */
    uint16_t handler_pc;
    uint16_t catch_type; /* constant-pool class index, 0 catches everything */
} xxbyte_java_exception_handler;

/*
 * Reads the exception table of the Code attribute at data[0], which was
 * parsed into *attribute from the same bytes. *count receives
 * exception_table_length and the first min(capacity, count) entries are
 * stored in handlers, which may be NULL when capacity is 0. Returns
 * XXBYTE_STATUS_TRUNCATED when the table runs past size.
 */
XXBYTE_API xxbyte_status xxbyte_java_parse_exception_table(const uint8_t *data, size_t size, const xxbyte_java_code_attribute *attribute,
                                                           xxbyte_java_exception_handler *handlers, size_t capacity, size_t *count);

/*
 * Class files (JVMS SE 25 chapter 4)
 *
 * xxbyte_java_class_open parses a class file held in memory: the magic,
 * versions 45.0 to 69.x (Java 1.0.2 to Java 25; minor versions are not
 * checked), every constant-pool tag (Long and Double take two slots), the
 * access flags, this_class, super_class, the interfaces, every field and
 * method with its attributes, and the class attributes. Every count, length
 * and offset is bounds-checked: a file that ends early gives
 * XXBYTE_STATUS_TRUNCATED, and a bad magic, an unsupported version, an
 * unknown constant-pool tag, an attribute whose name is not a Utf8 constant
 * or a malformed Code attribute give XXBYTE_STATUS_INVALID_INSTRUCTION.
 * References between constants are checked when they are used, not at open:
 * an entry that refers to a missing or mistyped constant has no text.
 *
 * Utf8 constants are decoded from Modified UTF-8 into standard UTF-8 once,
 * at open: the two-byte NUL (C0 80) becomes a 0x00 byte, a surrogate pair
 * becomes one four-byte sequence, an unpaired surrogate stays a three-byte
 * sequence (as in WTF-8) and every byte that is not valid Modified UTF-8
 * becomes U+FFFD. Strings returned by the class are NUL-terminated and
 * owned by it; xxbyte_java_class_utf8 also returns the length, which counts
 * embedded 0x00 bytes.
 *
 * The data passed to xxbyte_java_class_open is borrowed: keep it alive and
 * unchanged until xxbyte_java_class_close. Every offset below is a byte
 * offset from data[0].
 */

/* Constant-pool tags (JVMS 4.4, Table 4.4-B). */
#define XXBYTE_JAVA_CONSTANT_UTF8 1u
#define XXBYTE_JAVA_CONSTANT_INTEGER 3u
#define XXBYTE_JAVA_CONSTANT_FLOAT 4u
#define XXBYTE_JAVA_CONSTANT_LONG 5u
#define XXBYTE_JAVA_CONSTANT_DOUBLE 6u
#define XXBYTE_JAVA_CONSTANT_CLASS 7u
#define XXBYTE_JAVA_CONSTANT_STRING 8u
#define XXBYTE_JAVA_CONSTANT_FIELDREF 9u
#define XXBYTE_JAVA_CONSTANT_METHODREF 10u
#define XXBYTE_JAVA_CONSTANT_INTERFACE_METHODREF 11u
#define XXBYTE_JAVA_CONSTANT_NAME_AND_TYPE 12u
#define XXBYTE_JAVA_CONSTANT_METHOD_HANDLE 15u
#define XXBYTE_JAVA_CONSTANT_METHOD_TYPE 16u
#define XXBYTE_JAVA_CONSTANT_DYNAMIC 17u
#define XXBYTE_JAVA_CONSTANT_INVOKE_DYNAMIC 18u
#define XXBYTE_JAVA_CONSTANT_MODULE 19u
#define XXBYTE_JAVA_CONSTANT_PACKAGE 20u

/* MethodHandle reference kinds (JVMS 5.4.3.5). */
#define XXBYTE_JAVA_REF_GET_FIELD 1u
#define XXBYTE_JAVA_REF_GET_STATIC 2u
#define XXBYTE_JAVA_REF_PUT_FIELD 3u
#define XXBYTE_JAVA_REF_PUT_STATIC 4u
#define XXBYTE_JAVA_REF_INVOKE_VIRTUAL 5u
#define XXBYTE_JAVA_REF_INVOKE_STATIC 6u
#define XXBYTE_JAVA_REF_INVOKE_SPECIAL 7u
#define XXBYTE_JAVA_REF_NEW_INVOKE_SPECIAL 8u
#define XXBYTE_JAVA_REF_INVOKE_INTERFACE 9u

/* Access flags (JVMS Tables 4.1-B, 4.5-A and 4.6-A). Some bits mean
 * different things for classes, fields and methods. */
#define XXBYTE_JAVA_ACC_PUBLIC 0x0001u
#define XXBYTE_JAVA_ACC_PRIVATE 0x0002u
#define XXBYTE_JAVA_ACC_PROTECTED 0x0004u
#define XXBYTE_JAVA_ACC_STATIC 0x0008u
#define XXBYTE_JAVA_ACC_FINAL 0x0010u
#define XXBYTE_JAVA_ACC_SUPER 0x0020u        /* class */
#define XXBYTE_JAVA_ACC_SYNCHRONIZED 0x0020u /* method */
#define XXBYTE_JAVA_ACC_VOLATILE 0x0040u     /* field */
#define XXBYTE_JAVA_ACC_BRIDGE 0x0040u       /* method */
#define XXBYTE_JAVA_ACC_TRANSIENT 0x0080u    /* field */
#define XXBYTE_JAVA_ACC_VARARGS 0x0080u      /* method */
#define XXBYTE_JAVA_ACC_NATIVE 0x0100u
#define XXBYTE_JAVA_ACC_INTERFACE 0x0200u
#define XXBYTE_JAVA_ACC_ABSTRACT 0x0400u
#define XXBYTE_JAVA_ACC_STRICT 0x0800u
#define XXBYTE_JAVA_ACC_SYNTHETIC 0x1000u
#define XXBYTE_JAVA_ACC_ANNOTATION 0x2000u
#define XXBYTE_JAVA_ACC_ENUM 0x4000u
#define XXBYTE_JAVA_ACC_MODULE 0x8000u

/* The class-file versions xxbyte_java_class_open accepts. */
#define XXBYTE_JAVA_MIN_MAJOR_VERSION 45u
#define XXBYTE_JAVA_MAX_MAJOR_VERSION 69u

/* A parsed class file (opaque). */
typedef struct xxbyte_java_class xxbyte_java_class;

/* One constant-pool entry. Index 0, the unusable slot after a Long or a
 * Double, and indexes past the pool have tag 0. */
typedef struct xxbyte_java_constant {
    uint8_t tag;            /* XXBYTE_JAVA_CONSTANT_*, 0 for no entry */
    uint8_t reference_kind; /* MethodHandle: XXBYTE_JAVA_REF_* */
    /* Class, Module, Package: name_index. String: string_index.
     * Field/Method/InterfaceMethodref: class_index. NameAndType:
     * name_index. MethodHandle: reference_index. MethodType:
     * descriptor_index. Dynamic, InvokeDynamic:
     * bootstrap_method_attr_index. */
    uint16_t index1;
    /* Field/Method/InterfaceMethodref, Dynamic, InvokeDynamic:
     * name_and_type_index. NameAndType: descriptor_index. */
    uint16_t index2;
    uint32_t offset; /* offset of the tag byte */
    uint32_t length; /* Utf8: the encoded byte length */
    /* Integer and Float: the 32-bit value or bit pattern; Long and Double:
     * the 64-bit value or bit pattern. */
    uint64_t value;
} xxbyte_java_constant;

typedef struct xxbyte_java_class_info {
    uint16_t minor_version;
    uint16_t major_version;
    uint16_t constant_pool_count; /* the file's count: entries 1 to count-1 */
    uint16_t access_flags;
    uint16_t this_class;  /* constant-pool index */
    uint16_t super_class; /* constant-pool index, 0 for java/lang/Object */
    uint16_t interfaces_count;
    uint16_t fields_count;
    uint16_t methods_count;
    uint16_t attributes_count;
    uint16_t bootstrap_methods_count; /* of the BootstrapMethods attribute */
    uint32_t interfaces_offset;       /* offset of the u2 interface indexes */
    /* Internal (slash-separated) names, or NULL when the index does not
     * name a Class constant; the SourceFile attribute, or NULL. */
    const char *this_name;
    const char *super_name;
    const char *source_file;
} xxbyte_java_class_info;

/* A field or a method. */
typedef struct xxbyte_java_member {
    uint16_t access_flags;
    uint16_t name_index;
    uint16_t descriptor_index;
    uint16_t attributes_count;
    uint32_t offset;        /* offset of the field_info or method_info */
    const char *name;       /* never NULL ("" when the index is not Utf8) */
    const char *descriptor; /* never NULL */
    const char *signature;  /* the Signature attribute, or NULL */
    /* Exceptions attribute (methods): exceptions_count u2 class indexes at
     * exceptions_offset; both 0 when there is none. */
    uint16_t exceptions_count;
    uint32_t exceptions_offset;
    /* Code attribute (methods), the first one when there are several.
     * has_code is 0 for abstract and native methods; code holds the
     * xxbyte_java_parse_code_attribute result with every offset rebased
     * from the attribute's info to data[0]. code_attribute_offset is the
     * offset of its info, code_attribute_length its attribute_length. */
    int has_code;
    xxbyte_java_code_attribute code;
    uint32_t code_attribute_offset;
    uint32_t code_attribute_length;
    /* The first LineNumberTable nested in Code: line_number_table_length
     * entries of u2 start_pc and u2 line_number at line_number_table_offset;
     * both 0 when there is none. */
    uint16_t line_number_table_length;
    uint32_t line_number_table_offset;
} xxbyte_java_member;

/* One entry of the BootstrapMethods attribute (JVMS 4.7.23). */
typedef struct xxbyte_java_bootstrap_method {
    uint16_t method_ref;     /* MethodHandle constant index */
    uint16_t argument_count; /* u2 constant indexes at arguments_offset */
    uint32_t arguments_offset;
} xxbyte_java_bootstrap_method;

/* Parses data[0..size). On success *java_class receives the class (free it
 * with xxbyte_java_class_close); on failure it is set to NULL. */
XXBYTE_API xxbyte_status xxbyte_java_class_open(const uint8_t *data, size_t size, xxbyte_java_class **java_class);
XXBYTE_API void xxbyte_java_class_close(xxbyte_java_class *java_class);

XXBYTE_API xxbyte_status xxbyte_java_class_get_info(const xxbyte_java_class *java_class, xxbyte_java_class_info *info);
/* Entry index (see xxbyte_java_constant). Returns
 * XXBYTE_STATUS_INVALID_ARGUMENT, with *constant zeroed, when index has no
 * entry. */
XXBYTE_API xxbyte_status xxbyte_java_class_constant(const xxbyte_java_class *java_class, uint32_t index, xxbyte_java_constant *constant);
/* The decoded text of Utf8 constant index, or NULL when index is not a Utf8
 * constant. length, when not NULL, receives its byte length. */
XXBYTE_API const char *xxbyte_java_class_utf8(const xxbyte_java_class *java_class, uint32_t index, size_t *length);
/* The class-name text of Class constant index ("java/lang/Object",
 * "[I"), or NULL. */
XXBYTE_API const char *xxbyte_java_class_name(const xxbyte_java_class *java_class, uint32_t index);
/* Interface i (0 to interfaces_count - 1) as a constant-pool index, or 0. */
XXBYTE_API uint16_t xxbyte_java_class_interface(const xxbyte_java_class *java_class, uint32_t i);
/* Method or field i in file order. XXBYTE_STATUS_INVALID_ARGUMENT, with
 * *member zeroed, when i is out of range. */
XXBYTE_API xxbyte_status xxbyte_java_class_method(const xxbyte_java_class *java_class, uint32_t i, xxbyte_java_member *member);
XXBYTE_API xxbyte_status xxbyte_java_class_field(const xxbyte_java_class *java_class, uint32_t i, xxbyte_java_member *member);
XXBYTE_API xxbyte_status xxbyte_java_class_bootstrap_method(const xxbyte_java_class *java_class, uint32_t i, xxbyte_java_bootstrap_method *method);
/* The class data and size passed to xxbyte_java_class_open. */
XXBYTE_API const uint8_t *xxbyte_java_class_data(const xxbyte_java_class *java_class, size_t *size);

/*
 * javap text
 *
 * The functions below write text the way JDK 25 javap -c -p prints it, with
 * snprintf semantics: they return the complete length excluding the NUL
 * (0 when there is no text), write at most buffer_size - 1 characters and
 * a NUL when buffer_size is not 0, and only measure when it is 0.
 *
 * xxbyte_java_constant_text is the comment javap prints after an
 * instruction's constant-pool operand:
 *   Field java/lang/System.out:Ljava/io/PrintStream;
 *   Method java/io/PrintStream.println:(Ljava/lang/String;)V
 *   InterfaceMethod java/util/List.add:(Ljava/lang/Object;)Z
 *   Method "[I".clone:()Ljava/lang/Object;
 *   Field cnt:I                       (the class is this class)
 *   class java/lang/Object            class "[I"
 *   String hello                      String a\"b\n\u0001
 *   int 100000                        long 1234567890123l
 *   float 1.5f                        double 1.0E-300d (also NaNd, -Infinityf)
 *   MethodType (Ljava/lang/String;)I
 *   MethodHandle REF_invokeStatic java/lang/Integer.parseInt:(Ljava/lang/String;)I
 *   Dynamic #0:_:I
 *   InvokeDynamic #0:makeConcatWithConstants:(I)Ljava/lang/String;
 * - Field, Method and InterfaceMethod leave out the class when it is this
 *   class; MethodHandle never does.
 * - Class and member names that are not Java identifiers separated by '/'
 *   (by Character.isJavaIdentifierStart/Part of Unicode 16, as JDK 25) are
 *   quoted, with '"' and '\' escaped: "<init>", "[I", "a//b".
 * - Strings escape \b \t \n \f \r \" \' \\ and other ISO control
 *   characters as \u00XX; unpaired surrogates print as '?', as javap's
 *   UTF-8 output does.
 * - float and double are printed as Float.toString and Double.toString of
 *   JDK 19 and later print them, followed by 'f' or 'd'; long by 'l'.
 * - Module and Package constants print as "Module name" and "Package name"
 *   and NameAndType as "name:descriptor" (instructions never refer to
 *   them). Utf8 constants and broken references have no text.
 */
XXBYTE_API size_t xxbyte_java_constant_text(const xxbyte_java_class *java_class, uint32_t index, char *buffer, size_t buffer_size);

/*
 * The javap declaration line of a method, without indentation:
 *   public static void main(java.lang.String[]) throws java.lang.Exception;
 *   static <T extends java.lang.Comparable<T>> java.util.List<? super T> e(java.util.Map<java.lang.String, int[]>, T);
 *   public Hello();    static {};    public default void q();
 * The generic Signature attribute is used when the method has one and it
 * parses, the descriptor otherwise (a descriptor that does not parse is
 * printed as it is). Modifiers come in the order public, private,
 * protected, static, final, synchronized, native, abstract, strictfp (only
 * for versions 46 to 60, where ACC_STRICT is defined), then default for
 * non-abstract, non-static, non-private methods of interfaces. ACC_VARARGS
 * prints the last array parameter as "...". Throws come from the
 * signature's throws clause, or from the Exceptions attribute.
 */
XXBYTE_API size_t xxbyte_java_method_header(const xxbyte_java_class *java_class, const xxbyte_java_member *method, char *buffer, size_t buffer_size);

/*
 * Resolver for xxbyte_format_ex and the listing functions. context points
 * at an xxbyte_java_resolver_context. Every displayed constant-pool INDEX
 * operand gets the javap form, "#7 // Method
 * java/io/PrintStream.println:(Ljava/lang/String;)V" by default, or only the
 * comment text ("Method java/io/...") with XXBYTE_JAVA_RESOLVE_TEXT_ONLY.
 * Local slots, branch targets and constants without text keep their numeric
 * form. The core formatter limits resolver text to 511 characters; for
 * invokeinterface, invokedynamic and multianewarray the count or dimension
 * operand follows the resolved text. xxbyte_disasm_file prints the comment
 * at the end of the line, after every operand, and never truncates it.
 */
#define XXBYTE_JAVA_RESOLVE_DEFAULT 0x0000u
#define XXBYTE_JAVA_RESOLVE_TEXT_ONLY 0x0001u

typedef struct xxbyte_java_resolver_context {
    const xxbyte_java_class *java_class;
    uint32_t flags; /* XXBYTE_JAVA_RESOLVE_* */
} xxbyte_java_resolver_context;

XXBYTE_API size_t xxbyte_java_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

/*
 * Container listing (xxbyte_disasm_file; the mode is always
 * XXBYTE_JAVA_MODE_DEFAULT, and XXBYTE_MODE_AUTO means the same)
 *
 * A class file (magic CAFEBABE) lists as one "// class NAME, version
 * MAJOR.MINOR" line (NAME dotted, "?" when this_class is not a Class), then
 * for every method in file order: an empty line, the
 * xxbyte_java_method_header line, and, when the method has code, a
 * "  // stack=S, locals=L, code_length=N" line, the xxbyte_disasm_buf
 * listing of the code (addresses are pcs) with the xxbyte_java_constant_text
 * of every constant-pool operand appended as "  // text" at the end of its
 * line, and javap's exception table when it is not empty:
 *   00000003  12 09                    ldc #9  // int 123456789
 *     Exception table:
 *        from    to  target type
 *          57    80    91   Class java/lang/RuntimeException
 *          57    80   112   any
 * As javap does, spaces at the end of a comment line are dropped and names
 * are printed as they are, control characters included. Instruction text
 * is javap's with runs of spaces collapsed and switches on one line;
 * javap 25 prints jsr, jsr_w and ret without their operand, xxbyte with it.
 *
 * A JAR, ZIP or JMOD file (an end-of-central-directory record within the
 * last 65557 bytes; leading bytes such as the JMOD "JM\1\0" header or a
 * self-extractor stub are allowed) lists every entry whose name ends in
 * ".class", in central-directory order, each under a "// entry NAME" line
 * and an empty line, with an empty line between entries (control bytes in
 * NAME print as '?'). Stored (method 0) and deflated (method 8, through
 * xxbyte_inflate) entries are read, their sizes and CRC-32 checked.
 * Entries that are encrypted, ZIP64, use another compression method, are
 * larger than 64 MiB, fail their checks or are not valid class files are
 * listed as one "// entry NAME: skipped (reason)" line, for example
 * "skipped (encrypted)", "skipped (ZIP64)", "skipped (compression method
 * 12)", "skipped (bad CRC-32)". A ZIP64 or multi-disk archive gives only a
 * "// archive skipped (ZIP64)" or "(multi-disk)" line. Other entries are
 * not listed.
 *
 * Returns XXBYTE_STATUS_INVALID_INSTRUCTION when data is neither or its
 * central directory is broken, and the xxbyte_java_class_open status for a
 * class file that does not parse.
 */

#ifdef __cplusplus
}
#endif

#endif
