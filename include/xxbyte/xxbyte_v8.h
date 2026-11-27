/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_XXBYTE_V8_H
#define XXBYTE_XXBYTE_V8_H

#include "xxbyte_common.h"

#if !XXBYTE_USE_V8
#error "the V8 Ignition bytecode decoder is disabled in this xxbyte build"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * V8 Ignition bytecode: the BytecodeArray contents of V8 (Node.js, Chrome,
 * Electron, Deno) as node --print-bytecode / d8 --print-bytecode show them.
 *
 * Modes. A mode is a V8 major.minor version, XXBYTE_V8_MODE(major, minor)
 * = major * 100 + minor; the supported ones are below, any other mode is
 * XXBYTE_STATUS_UNSUPPORTED_MODE. The bytecode list and operand types
 * (src/interpreter/bytecodes.h, bytecode-operands.h), the runtime function
 * list (src/runtime/runtime.h), the intrinsics (interpreter-intrinsics.h),
 * the native context fields (src/objects/contexts.h), the abort reasons
 * (src/codegen/bailout-reason.h), the embedded feedback type names
 * (src/common/globals.h) and the interpreter frame layout
 * (src/execution/frame-constants.h) are tabulated by a generator from the
 * official sources of:
 *
 *   1002  V8 10.2.154.26   Node.js 18 LTS (v18.20.8 deps/v8)
 *   1103  V8 11.3.244.8    Node.js 20 LTS (v20.20.2 deps/v8)
 *   1204  V8 12.4.254.21   Node.js 22 LTS (v22.23.5 deps/v8)
 *   1306  V8 13.6.233.17   Node.js 24 LTS (v24.22.3 deps/v8)
 *   1406  V8 14.6.202.34   Node.js 26 LTS (v26.13.1 deps/v8)
 *   1508  V8 15.8.62.61    Chrome 158 (the v8/v8 tag)
 *
 * XXBYTE_V8_MODE_DEFAULT is the newest, 1508. Node.js floats patches on
 * V8: the Node.js modes use the node deps/v8 copy (Node 18 has one more
 * runtime function, GetWeakCollectionSize, than V8 10.2; Node 26 two
 * fewer than V8 14.6), the build configuration of Node.js (Intl and
 * WebAssembly on, not an official build; 13.6 with leap tiering), and for
 * 1508 that of an official x64/arm64 Chrome (Temporal, Sparkplug-plus).
 * Bytecode numbering only depends on the version; runtime ids and native
 * context indexes can differ in other embedders' configurations.
 *
 * Encoding. A bytecode is one byte followed by its operands, little-endian
 * and unaligned. Scalable operands (registers, Imm, UImm, Idx, RegCount
 * and from 14.6 ConstantPoolIndex, FeedbackSlot, ContextSlot, CoverageSlot)
 * are 1 byte, 2 bytes after the Wide prefix (0x00) or 4 bytes after
 * ExtraWide (0x01); the prefix and the bytecode decode as one instruction
 * (mnemonic "LdaSmi.Wide", "LdaSmi.ExtraWide"; opcode =
 * XXBYTE_V8_OPCODE_PREFIXED | prefix << 8 | bytecode, so 0x1000D is
 * Wide LdaSmi; an unprefixed opcode is the bytecode byte). The
 * DebugBreakWide / DebugBreakExtraWide prefixes (0x02, 0x03, patched in
 * by the debugger) scale the same way and print like Wide / ExtraWide, as
 * V8's BytecodeDecoder does. Fixed operands
 * (Flag8, IntrinsicId, NativeContextIndex, AbortReason: 1 byte; Flag16,
 * RuntimeId: 2 bytes; EmbeddedFeedback: 2 bytes in 14.6, 1 byte in 15.8)
 * do not scale. A byte beyond the version's bytecode count, a prefix
 * before a bytecode without scalable operands (or before another prefix)
 * is XXBYTE_STATUS_INVALID_INSTRUCTION; an instruction that runs past
 * code_size, or a prefix at the end, is XXBYTE_STATUS_TRUNCATED. Every
 * operand value decodes. DebugBreak0 to DebugBreak6 replace a bytecode of
 * the same size; their operand bytes belong to the replaced bytecode, so
 * they have no operands.
 *
 * Text follows BytecodeDecoder::Decode (--print-bytecode without the hex
 * bytes), one space after the mnemonic and ", " between operands:
 *   - registers as V8's Register::ToString: r0, r1, ... (locals), <this>,
 *     a0, a1, ... (parameters), <context>, <closure>, <accumulator>;
 *   - register lists (RegList, RegOutList with their RegCount), pairs and
 *     triples as first-last, "r1-r3" (a zero-count list prints first-r0
 *     as V8 does, "r2-r0");
 *   - Idx, UImm, Imm, ContextSlot, CoverageSlot as [n] (Imm signed),
 *     FeedbackSlot (14.6+) as FBV[n], ConstantPoolIndex (14.6+) as [n];
 *   - Flag8, Flag16 as #n, in hex from 14.6 ("#1a") as V8 prints them;
 *   - RuntimeId as [Name], IntrinsicId as [_Name] (the inline runtime
 *     function), NativeContextIndex as [name], AbortReason (14.6+) as
 *     [message]; an id beyond the version's table prints as [n];
 *   - EmbeddedFeedback as EmbeddedFeedback[0x1] (14.6) or with the
 *     BinaryOperationFeedback / CompareOperationFeedback type name,
 *     EmbeddedFeedback[SignedSmall] (15.8; an index beyond the list prints
 *     "Unknown" as V8 does);
 *   - jump targets as the absolute address, "0x0026" ("-0x0004" when
 *     negative). --print-bytecode prints the relative operand and the
 *     target, "JumpIfFalse [29] (0x... @ 38)": the target is the address
 *     of the bytecode after its prefix plus the offset (minus for
 *     JumpLoop).
 * V8 14.6+ prints a constant pool operand with its value, [3:"name"]; the
 * decoder cannot see the pool, xxbyte_v8_resolve prints that form from a
 * xxbyte_v8_context.
 *
 *   0B 03                   (12.4) Ldar a0
 *   0D 05                   (12.4) LdaSmi [5]
 *   00 0D E8 03             (12.4) LdaSmi.Wide [1000]
 *   9E 1D        (12.4, at 0x09) JumpIfFalse 0x0026
 *   8E 1E 00 06  (12.4, at 0x22) JumpLoop 0x0004, [0], [6]
 *   71 F8 00                (12.4) TestLessThan r1, [0]
 *   68 E4 01 F9 00          (12.4) CallRuntime [IsBeingInterpreted], r0-r0
 *   6B 0A F8 02             (12.4) InvokeIntrinsic [_CreateJSGeneratorObject], r1-r2
 *   C9                      (12.4) Star0
 *
 * Registers. A register operand is a signed frame-slot offset;
 * Register::FromOperand gives the register index = kRegisterFileStartOffset
 * - operand, where kRegisterFileStartOffset is -6 slots up to 11.3 and -7
 * from 12.4 (the frame gained a feedback-vector slot). Index >= 0 is the
 * local register r<index>; <context>, <closure> and <accumulator> are
 * fixed operands -1, -2 and 1; any other negative index is the parameter
 * p = operand - 2 (0 = <this>, p >= 1 = a<p-1>), so operand 2 is <this>
 * and 3 is a0 in every version. These are the layouts of 64-bit and
 * 32-bit targets without an embedded constant pool (all but PPC).
 *
 * Operands, one per V8 operand except that a RegList / RegOutList and the
 * RegCount after it are one operand:
 *   - XXBYTE_OPERAND_REGISTER: Reg, RegOut, RegInOut; value = the V8
 *     register index (signed, r<value> when >= 0), extra = the encoded
 *     operand, sign-extended; flags SIGNED and the family flags below;
 *   - XXBYTE_OPERAND_REGISTER_RANGE: RegList / RegOutList (extra = the
 *     RegCount), RegPair / RegOutPair (extra = 2), RegOutTriple (3); value =
 *     the first register's index; size = the width of one register operand;
 *   - XXBYTE_OPERAND_IMMEDIATE: Imm (SIGNED), UImm, Flag8, Flag16 and
 *     EmbeddedFeedback (xxbyte_v8_operand_type tells them apart);
 *   - XXBYTE_OPERAND_BRANCH: the offset of Jump..., JumpLoop and, with a
 *     context whose constant is a Smi, of Jump...Constant; value = the
 *     absolute target, extra = the encoded offset (the constant index for
 *     a constant jump, flag XXBYTE_V8_OPERAND_FLAG_CONSTANT);
 *   - XXBYTE_OPERAND_INDEX: Idx, ConstantPoolIndex, FeedbackSlot,
 *     ContextSlot, CoverageSlot, RuntimeId, IntrinsicId,
 *     NativeContextIndex, AbortReason; kind XXBYTE_INDEX_CONSTANT for the
 *     constant pool, else an XXBYTE_V8_INDEX_* below. An Idx of 10.2 to
 *     13.6 gets the kind of the operand's 14.6 type (GetNamedProperty r0,
 *     [constant], [feedback]); the text stays [n] as V8 prints it.
 * operand.size is the encoded width (2 for RuntimeId, scaled for the
 * scalable ones).
 *
 * Groups: Jump, JumpConstant, JumpLoop: JUMP|STOP; JumpIf...: CONDITIONAL;
 * SwitchOnSmiNoFeedback, SwitchOnGeneratorState: SWITCH (they fall through
 * when no case matches); Return, SuspendGenerator: RETURN|STOP; Throw,
 * ReThrow: THROW|STOP; the conditional Throw...If...: THROW; Abort, Illegal: STOP; Call..., Construct..., InvokeIntrinsic,
 * FindNonDefaultConstructorOrConstruct: CALL.
 *
 * Switches. SwitchOnSmiNoFeedback [table_start], [table_length],
 * [case_value_base] and SwitchOnGeneratorState reg, [table_start],
 * [table_length] (case values from 0) take their targets from the Smi
 * constants table_start .. table_start + table_length - 1 of the constant
 * pool, offsets from the address after the prefix. The pool is not part
 * of the bytecode, so table_count is 0 and xxbyte_switch_entry does not
 * apply; xxbyte_v8_switch_entry reads the entries from a decode context.
 * --print-bytecode appends them as " { 0: @39, 1: @36 }".
 *
 * Container files. Node.js code caches (v8.Script cachedData, bytenode
 * .jsc) and V8 snapshots are serialized heaps whose encoding (the
 * deserializer's bytecodes, the read-only root list, object layouts)
 * changes with every V8 version and build configuration; there is no
 * container listing (xxbyte_disasm_file returns
 * XXBYTE_STATUS_UNSUPPORTED_FAMILY). Decode the bytes of a BytecodeArray
 * with a context instead.
 */
#define XXBYTE_V8_MODE(major, minor) ((uint32_t)(major) * 100u + (uint32_t)(minor))
#define XXBYTE_V8_MODE_10_2 1002u
#define XXBYTE_V8_MODE_11_3 1103u
#define XXBYTE_V8_MODE_12_4 1204u
#define XXBYTE_V8_MODE_13_6 1306u
#define XXBYTE_V8_MODE_14_6 1406u
#define XXBYTE_V8_MODE_15_8 1508u
#define XXBYTE_V8_MODE_NODE18 XXBYTE_V8_MODE_10_2
#define XXBYTE_V8_MODE_NODE20 XXBYTE_V8_MODE_11_3
#define XXBYTE_V8_MODE_NODE22 XXBYTE_V8_MODE_12_4
#define XXBYTE_V8_MODE_NODE24 XXBYTE_V8_MODE_13_6
#define XXBYTE_V8_MODE_NODE26 XXBYTE_V8_MODE_14_6
#define XXBYTE_V8_MODE_OLDEST XXBYTE_V8_MODE_10_2
#define XXBYTE_V8_MODE_NEWEST XXBYTE_V8_MODE_15_8
#define XXBYTE_V8_MODE_DEFAULT XXBYTE_V8_MODE_NEWEST

/* xxbyte_instruction.opcode bit of a prefixed bytecode (the Wide prefix
 * is byte 0x00). */
#define XXBYTE_V8_OPCODE_PREFIXED 0x10000u

/* Family index kinds. */
#define XXBYTE_V8_INDEX_FEEDBACK (XXBYTE_INDEX_FAMILY_FIRST + 0)       /* feedback vector slot */
#define XXBYTE_V8_INDEX_CONTEXT_SLOT (XXBYTE_INDEX_FAMILY_FIRST + 1)   /* context slot */
#define XXBYTE_V8_INDEX_COVERAGE (XXBYTE_INDEX_FAMILY_FIRST + 2)       /* block coverage slot */
#define XXBYTE_V8_INDEX_RUNTIME (XXBYTE_INDEX_FAMILY_FIRST + 3)        /* Runtime::FunctionId */
#define XXBYTE_V8_INDEX_INTRINSIC (XXBYTE_INDEX_FAMILY_FIRST + 4)      /* IntrinsicsHelper::IntrinsicId */
#define XXBYTE_V8_INDEX_NATIVE_CONTEXT (XXBYTE_INDEX_FAMILY_FIRST + 5) /* native context field */
#define XXBYTE_V8_INDEX_ABORT_REASON (XXBYTE_INDEX_FAMILY_FIRST + 6)   /* AbortReason */
#define XXBYTE_V8_INDEX_IDX (XXBYTE_INDEX_FAMILY_FIRST + 7)            /* reserved: an Idx of unknown meaning (every tabulated Idx has a kind) */

/* Family operand flags. */
#define XXBYTE_V8_OPERAND_FLAG_PARAMETER 0x1000u /* register is <this> or a parameter */
#define XXBYTE_V8_OPERAND_FLAG_SPECIAL 0x2000u   /* <context>, <closure> or <accumulator> */
#define XXBYTE_V8_OPERAND_FLAG_OUTPUT 0x4000u    /* RegOut..., RegInOut: written */
#define XXBYTE_V8_OPERAND_FLAG_CONSTANT 0x8000u  /* branch offset from the constant pool */

/* V8 operand types (xxbyte_v8_operand_type). */
typedef enum xxbyte_v8_operand_kind {
    XXBYTE_V8_OPERAND_NONE = 0,
    XXBYTE_V8_OPERAND_REG = 1,
    XXBYTE_V8_OPERAND_REG_LIST = 2,
    XXBYTE_V8_OPERAND_REG_PAIR = 3,
    XXBYTE_V8_OPERAND_REG_OUT = 4,
    XXBYTE_V8_OPERAND_REG_OUT_LIST = 5,
    XXBYTE_V8_OPERAND_REG_OUT_PAIR = 6,
    XXBYTE_V8_OPERAND_REG_OUT_TRIPLE = 7,
    XXBYTE_V8_OPERAND_REG_IN_OUT = 8,
    XXBYTE_V8_OPERAND_REG_COUNT = 9,
    XXBYTE_V8_OPERAND_IMM = 10,
    XXBYTE_V8_OPERAND_UIMM = 11,
    XXBYTE_V8_OPERAND_IDX = 12,
    XXBYTE_V8_OPERAND_CONSTANT_POOL_INDEX = 13,
    XXBYTE_V8_OPERAND_FEEDBACK_SLOT = 14,
    XXBYTE_V8_OPERAND_CONTEXT_SLOT = 15,
    XXBYTE_V8_OPERAND_COVERAGE_SLOT = 16,
    XXBYTE_V8_OPERAND_FLAG8 = 17,
    XXBYTE_V8_OPERAND_FLAG16 = 18,
    XXBYTE_V8_OPERAND_INTRINSIC_ID = 19,
    XXBYTE_V8_OPERAND_RUNTIME_ID = 20,
    XXBYTE_V8_OPERAND_NATIVE_CONTEXT_INDEX = 21,
    XXBYTE_V8_OPERAND_ABORT_REASON = 22,
    XXBYTE_V8_OPERAND_EMBEDDED_FEEDBACK = 23,
    XXBYTE_V8_OPERAND_TYPE_COUNT
} xxbyte_v8_operand_kind;

/*
 * Decode context for xxbyte_decode_ex / xxbyte_disasm_buf_ex, and resolver
 * context for xxbyte_v8_resolve: what --print-bytecode prints around the
 * bytecodes. constants[i] is the display text of constant pool entry i
 * (NULL entries and a NULL array are allowed); a Smi entry is its decimal
 * text ("20", "-3"), as --print-bytecode lists it. With a context the
 * decoder turns Jump...Constant operands whose constant is a Smi into
 * branches. parameter_count (including the receiver) and register_count
 * are informational: register names do not depend on them.
 */
typedef struct xxbyte_v8_context {
    uint32_t parameter_count;
    uint32_t register_count;
    uint32_t constant_count;
    const char *const *constants;
} xxbyte_v8_context;

/*
 * Mnemonic identifiers (xxbyte_instruction.id): every bytecode name of the
 * supported versions in order of first appearance (10.2, then the names
 * later versions added), so a bytecode keeps its id when its number
 * changes. The Wide and ExtraWide forms share the id. The name is the
 * bytecode name in upper snake case.
 */
typedef enum xxbyte_v8_id {
    XXBYTE_V8_ID_INVALID = 0,
    XXBYTE_V8_ID_WIDE = 1,
    XXBYTE_V8_ID_EXTRA_WIDE = 2,
    XXBYTE_V8_ID_DEBUG_BREAK_WIDE = 3,
    XXBYTE_V8_ID_DEBUG_BREAK_EXTRA_WIDE = 4,
    XXBYTE_V8_ID_DEBUG_BREAK0 = 5,
    XXBYTE_V8_ID_DEBUG_BREAK1 = 6,
    XXBYTE_V8_ID_DEBUG_BREAK2 = 7,
    XXBYTE_V8_ID_DEBUG_BREAK3 = 8,
    XXBYTE_V8_ID_DEBUG_BREAK4 = 9,
    XXBYTE_V8_ID_DEBUG_BREAK5 = 10,
    XXBYTE_V8_ID_DEBUG_BREAK6 = 11,
    XXBYTE_V8_ID_LDAR = 12,
    XXBYTE_V8_ID_LDA_ZERO = 13,
    XXBYTE_V8_ID_LDA_SMI = 14,
    XXBYTE_V8_ID_LDA_UNDEFINED = 15,
    XXBYTE_V8_ID_LDA_NULL = 16,
    XXBYTE_V8_ID_LDA_THE_HOLE = 17,
    XXBYTE_V8_ID_LDA_TRUE = 18,
    XXBYTE_V8_ID_LDA_FALSE = 19,
    XXBYTE_V8_ID_LDA_CONSTANT = 20,
    XXBYTE_V8_ID_LDA_CONTEXT_SLOT = 21,
    XXBYTE_V8_ID_LDA_IMMUTABLE_CONTEXT_SLOT = 22,
    XXBYTE_V8_ID_LDA_CURRENT_CONTEXT_SLOT = 23,
    XXBYTE_V8_ID_LDA_IMMUTABLE_CURRENT_CONTEXT_SLOT = 24,
    XXBYTE_V8_ID_STAR = 25,
    XXBYTE_V8_ID_MOV = 26,
    XXBYTE_V8_ID_PUSH_CONTEXT = 27,
    XXBYTE_V8_ID_POP_CONTEXT = 28,
    XXBYTE_V8_ID_TEST_REFERENCE_EQUAL = 29,
    XXBYTE_V8_ID_TEST_UNDETECTABLE = 30,
    XXBYTE_V8_ID_TEST_NULL = 31,
    XXBYTE_V8_ID_TEST_UNDEFINED = 32,
    XXBYTE_V8_ID_TEST_TYPE_OF = 33,
    XXBYTE_V8_ID_LDA_GLOBAL = 34,
    XXBYTE_V8_ID_LDA_GLOBAL_INSIDE_TYPEOF = 35,
    XXBYTE_V8_ID_STA_GLOBAL = 36,
    XXBYTE_V8_ID_STA_CONTEXT_SLOT = 37,
    XXBYTE_V8_ID_STA_CURRENT_CONTEXT_SLOT = 38,
    XXBYTE_V8_ID_LDA_LOOKUP_SLOT = 39,
    XXBYTE_V8_ID_LDA_LOOKUP_CONTEXT_SLOT = 40,
    XXBYTE_V8_ID_LDA_LOOKUP_GLOBAL_SLOT = 41,
    XXBYTE_V8_ID_LDA_LOOKUP_SLOT_INSIDE_TYPEOF = 42,
    XXBYTE_V8_ID_LDA_LOOKUP_CONTEXT_SLOT_INSIDE_TYPEOF = 43,
    XXBYTE_V8_ID_LDA_LOOKUP_GLOBAL_SLOT_INSIDE_TYPEOF = 44,
    XXBYTE_V8_ID_STA_LOOKUP_SLOT = 45,
    XXBYTE_V8_ID_GET_NAMED_PROPERTY = 46,
    XXBYTE_V8_ID_GET_NAMED_PROPERTY_FROM_SUPER = 47,
    XXBYTE_V8_ID_GET_KEYED_PROPERTY = 48,
    XXBYTE_V8_ID_LDA_MODULE_VARIABLE = 49,
    XXBYTE_V8_ID_STA_MODULE_VARIABLE = 50,
    XXBYTE_V8_ID_SET_NAMED_PROPERTY = 51,
    XXBYTE_V8_ID_DEFINE_NAMED_OWN_PROPERTY = 52,
    XXBYTE_V8_ID_SET_KEYED_PROPERTY = 53,
    XXBYTE_V8_ID_DEFINE_KEYED_OWN_PROPERTY = 54,
    XXBYTE_V8_ID_STA_IN_ARRAY_LITERAL = 55,
    XXBYTE_V8_ID_DEFINE_KEYED_OWN_PROPERTY_IN_LITERAL = 56,
    XXBYTE_V8_ID_COLLECT_TYPE_PROFILE = 57,
    XXBYTE_V8_ID_ADD = 58,
    XXBYTE_V8_ID_SUB = 59,
    XXBYTE_V8_ID_MUL = 60,
    XXBYTE_V8_ID_DIV = 61,
    XXBYTE_V8_ID_MOD = 62,
    XXBYTE_V8_ID_EXP = 63,
    XXBYTE_V8_ID_BITWISE_OR = 64,
    XXBYTE_V8_ID_BITWISE_XOR = 65,
    XXBYTE_V8_ID_BITWISE_AND = 66,
    XXBYTE_V8_ID_SHIFT_LEFT = 67,
    XXBYTE_V8_ID_SHIFT_RIGHT = 68,
    XXBYTE_V8_ID_SHIFT_RIGHT_LOGICAL = 69,
    XXBYTE_V8_ID_ADD_SMI = 70,
    XXBYTE_V8_ID_SUB_SMI = 71,
    XXBYTE_V8_ID_MUL_SMI = 72,
    XXBYTE_V8_ID_DIV_SMI = 73,
    XXBYTE_V8_ID_MOD_SMI = 74,
    XXBYTE_V8_ID_EXP_SMI = 75,
    XXBYTE_V8_ID_BITWISE_OR_SMI = 76,
    XXBYTE_V8_ID_BITWISE_XOR_SMI = 77,
    XXBYTE_V8_ID_BITWISE_AND_SMI = 78,
    XXBYTE_V8_ID_SHIFT_LEFT_SMI = 79,
    XXBYTE_V8_ID_SHIFT_RIGHT_SMI = 80,
    XXBYTE_V8_ID_SHIFT_RIGHT_LOGICAL_SMI = 81,
    XXBYTE_V8_ID_INC = 82,
    XXBYTE_V8_ID_DEC = 83,
    XXBYTE_V8_ID_NEGATE = 84,
    XXBYTE_V8_ID_BITWISE_NOT = 85,
    XXBYTE_V8_ID_TO_BOOLEAN_LOGICAL_NOT = 86,
    XXBYTE_V8_ID_LOGICAL_NOT = 87,
    XXBYTE_V8_ID_TYPE_OF = 88,
    XXBYTE_V8_ID_DELETE_PROPERTY_STRICT = 89,
    XXBYTE_V8_ID_DELETE_PROPERTY_SLOPPY = 90,
    XXBYTE_V8_ID_GET_SUPER_CONSTRUCTOR = 91,
    XXBYTE_V8_ID_CALL_ANY_RECEIVER = 92,
    XXBYTE_V8_ID_CALL_PROPERTY = 93,
    XXBYTE_V8_ID_CALL_PROPERTY0 = 94,
    XXBYTE_V8_ID_CALL_PROPERTY1 = 95,
    XXBYTE_V8_ID_CALL_PROPERTY2 = 96,
    XXBYTE_V8_ID_CALL_UNDEFINED_RECEIVER = 97,
    XXBYTE_V8_ID_CALL_UNDEFINED_RECEIVER0 = 98,
    XXBYTE_V8_ID_CALL_UNDEFINED_RECEIVER1 = 99,
    XXBYTE_V8_ID_CALL_UNDEFINED_RECEIVER2 = 100,
    XXBYTE_V8_ID_CALL_WITH_SPREAD = 101,
    XXBYTE_V8_ID_CALL_RUNTIME = 102,
    XXBYTE_V8_ID_CALL_RUNTIME_FOR_PAIR = 103,
    XXBYTE_V8_ID_CALL_JS_RUNTIME = 104,
    XXBYTE_V8_ID_INVOKE_INTRINSIC = 105,
    XXBYTE_V8_ID_CONSTRUCT = 106,
    XXBYTE_V8_ID_CONSTRUCT_WITH_SPREAD = 107,
    XXBYTE_V8_ID_TEST_EQUAL = 108,
    XXBYTE_V8_ID_TEST_EQUAL_STRICT = 109,
    XXBYTE_V8_ID_TEST_LESS_THAN = 110,
    XXBYTE_V8_ID_TEST_GREATER_THAN = 111,
    XXBYTE_V8_ID_TEST_LESS_THAN_OR_EQUAL = 112,
    XXBYTE_V8_ID_TEST_GREATER_THAN_OR_EQUAL = 113,
    XXBYTE_V8_ID_TEST_INSTANCE_OF = 114,
    XXBYTE_V8_ID_TEST_IN = 115,
    XXBYTE_V8_ID_TO_NAME = 116,
    XXBYTE_V8_ID_TO_NUMBER = 117,
    XXBYTE_V8_ID_TO_NUMERIC = 118,
    XXBYTE_V8_ID_TO_OBJECT = 119,
    XXBYTE_V8_ID_TO_STRING = 120,
    XXBYTE_V8_ID_CREATE_REG_EXP_LITERAL = 121,
    XXBYTE_V8_ID_CREATE_ARRAY_LITERAL = 122,
    XXBYTE_V8_ID_CREATE_ARRAY_FROM_ITERABLE = 123,
    XXBYTE_V8_ID_CREATE_EMPTY_ARRAY_LITERAL = 124,
    XXBYTE_V8_ID_CREATE_OBJECT_LITERAL = 125,
    XXBYTE_V8_ID_CREATE_EMPTY_OBJECT_LITERAL = 126,
    XXBYTE_V8_ID_CLONE_OBJECT = 127,
    XXBYTE_V8_ID_GET_TEMPLATE_OBJECT = 128,
    XXBYTE_V8_ID_CREATE_CLOSURE = 129,
    XXBYTE_V8_ID_CREATE_BLOCK_CONTEXT = 130,
    XXBYTE_V8_ID_CREATE_CATCH_CONTEXT = 131,
    XXBYTE_V8_ID_CREATE_FUNCTION_CONTEXT = 132,
    XXBYTE_V8_ID_CREATE_EVAL_CONTEXT = 133,
    XXBYTE_V8_ID_CREATE_WITH_CONTEXT = 134,
    XXBYTE_V8_ID_CREATE_MAPPED_ARGUMENTS = 135,
    XXBYTE_V8_ID_CREATE_UNMAPPED_ARGUMENTS = 136,
    XXBYTE_V8_ID_CREATE_REST_PARAMETER = 137,
    XXBYTE_V8_ID_JUMP_LOOP = 138,
    XXBYTE_V8_ID_JUMP = 139,
    XXBYTE_V8_ID_JUMP_CONSTANT = 140,
    XXBYTE_V8_ID_JUMP_IF_NULL_CONSTANT = 141,
    XXBYTE_V8_ID_JUMP_IF_NOT_NULL_CONSTANT = 142,
    XXBYTE_V8_ID_JUMP_IF_UNDEFINED_CONSTANT = 143,
    XXBYTE_V8_ID_JUMP_IF_NOT_UNDEFINED_CONSTANT = 144,
    XXBYTE_V8_ID_JUMP_IF_UNDEFINED_OR_NULL_CONSTANT = 145,
    XXBYTE_V8_ID_JUMP_IF_TRUE_CONSTANT = 146,
    XXBYTE_V8_ID_JUMP_IF_FALSE_CONSTANT = 147,
    XXBYTE_V8_ID_JUMP_IF_JS_RECEIVER_CONSTANT = 148,
    XXBYTE_V8_ID_JUMP_IF_TO_BOOLEAN_TRUE_CONSTANT = 149,
    XXBYTE_V8_ID_JUMP_IF_TO_BOOLEAN_FALSE_CONSTANT = 150,
    XXBYTE_V8_ID_JUMP_IF_TO_BOOLEAN_TRUE = 151,
    XXBYTE_V8_ID_JUMP_IF_TO_BOOLEAN_FALSE = 152,
    XXBYTE_V8_ID_JUMP_IF_TRUE = 153,
    XXBYTE_V8_ID_JUMP_IF_FALSE = 154,
    XXBYTE_V8_ID_JUMP_IF_NULL = 155,
    XXBYTE_V8_ID_JUMP_IF_NOT_NULL = 156,
    XXBYTE_V8_ID_JUMP_IF_UNDEFINED = 157,
    XXBYTE_V8_ID_JUMP_IF_NOT_UNDEFINED = 158,
    XXBYTE_V8_ID_JUMP_IF_UNDEFINED_OR_NULL = 159,
    XXBYTE_V8_ID_JUMP_IF_JS_RECEIVER = 160,
    XXBYTE_V8_ID_SWITCH_ON_SMI_NO_FEEDBACK = 161,
    XXBYTE_V8_ID_FOR_IN_ENUMERATE = 162,
    XXBYTE_V8_ID_FOR_IN_PREPARE = 163,
    XXBYTE_V8_ID_FOR_IN_CONTINUE = 164,
    XXBYTE_V8_ID_FOR_IN_NEXT = 165,
    XXBYTE_V8_ID_FOR_IN_STEP = 166,
    XXBYTE_V8_ID_SET_PENDING_MESSAGE = 167,
    XXBYTE_V8_ID_THROW = 168,
    XXBYTE_V8_ID_RE_THROW = 169,
    XXBYTE_V8_ID_RETURN = 170,
    XXBYTE_V8_ID_THROW_REFERENCE_ERROR_IF_HOLE = 171,
    XXBYTE_V8_ID_THROW_SUPER_NOT_CALLED_IF_HOLE = 172,
    XXBYTE_V8_ID_THROW_SUPER_ALREADY_CALLED_IF_NOT_HOLE = 173,
    XXBYTE_V8_ID_THROW_IF_NOT_SUPER_CONSTRUCTOR = 174,
    XXBYTE_V8_ID_SWITCH_ON_GENERATOR_STATE = 175,
    XXBYTE_V8_ID_SUSPEND_GENERATOR = 176,
    XXBYTE_V8_ID_RESUME_GENERATOR = 177,
    XXBYTE_V8_ID_GET_ITERATOR = 178,
    XXBYTE_V8_ID_DEBUGGER = 179,
    XXBYTE_V8_ID_INC_BLOCK_COUNTER = 180,
    XXBYTE_V8_ID_ABORT = 181,
    XXBYTE_V8_ID_STAR15 = 182,
    XXBYTE_V8_ID_STAR14 = 183,
    XXBYTE_V8_ID_STAR13 = 184,
    XXBYTE_V8_ID_STAR12 = 185,
    XXBYTE_V8_ID_STAR11 = 186,
    XXBYTE_V8_ID_STAR10 = 187,
    XXBYTE_V8_ID_STAR9 = 188,
    XXBYTE_V8_ID_STAR8 = 189,
    XXBYTE_V8_ID_STAR7 = 190,
    XXBYTE_V8_ID_STAR6 = 191,
    XXBYTE_V8_ID_STAR5 = 192,
    XXBYTE_V8_ID_STAR4 = 193,
    XXBYTE_V8_ID_STAR3 = 194,
    XXBYTE_V8_ID_STAR2 = 195,
    XXBYTE_V8_ID_STAR1 = 196,
    XXBYTE_V8_ID_STAR0 = 197,
    XXBYTE_V8_ID_ILLEGAL = 198,
    XXBYTE_V8_ID_FIND_NON_DEFAULT_CONSTRUCTOR_OR_CONSTRUCT = 199,
    XXBYTE_V8_ID_STA_SCRIPT_CONTEXT_SLOT = 200,
    XXBYTE_V8_ID_STA_CURRENT_SCRIPT_CONTEXT_SLOT = 201,
    XXBYTE_V8_ID_GET_ENUMERATED_KEYED_PROPERTY = 202,
    XXBYTE_V8_ID_CONSTRUCT_FORWARD_ALL_ARGS = 203,
    XXBYTE_V8_ID_TO_BOOLEAN = 204,
    XXBYTE_V8_ID_LDA_SCRIPT_CONTEXT_SLOT = 205,
    XXBYTE_V8_ID_LDA_CURRENT_SCRIPT_CONTEXT_SLOT = 206,
    XXBYTE_V8_ID_LDA_LOOKUP_SCRIPT_CONTEXT_SLOT = 207,
    XXBYTE_V8_ID_LDA_LOOKUP_SCRIPT_CONTEXT_SLOT_INSIDE_TYPEOF = 208,
    XXBYTE_V8_ID_JUMP_IF_FOR_IN_DONE_CONSTANT = 209,
    XXBYTE_V8_ID_JUMP_IF_FOR_IN_DONE = 210,
    XXBYTE_V8_ID_LDA_CONTEXT_SLOT_NO_CELL = 211,
    XXBYTE_V8_ID_LDA_CURRENT_CONTEXT_SLOT_NO_CELL = 212,
    XXBYTE_V8_ID_STA_CONTEXT_SLOT_NO_CELL = 213,
    XXBYTE_V8_ID_STA_CURRENT_CONTEXT_SLOT_NO_CELL = 214,
    XXBYTE_V8_ID_LDA_LOOKUP_CONTEXT_SLOT_NO_CELL = 215,
    XXBYTE_V8_ID_LDA_LOOKUP_CONTEXT_SLOT_NO_CELL_INSIDE_TYPEOF = 216,
    XXBYTE_V8_ID_SET_PROTOTYPE_PROPERTIES = 217,
    XXBYTE_V8_ID_ADD_STRING_CONSTANT_INTERNALIZE = 218,
    XXBYTE_V8_ID_CREATE_FUNCTION_CONTEXT_WITH_CELLS = 219,
    XXBYTE_V8_ID_FOR_OF_NEXT = 220,
    XXBYTE_V8_ID_LDA_TDZ_HOLE = 221,
    XXBYTE_V8_ID_GET_PRIVATE_FIELD = 222,
    XXBYTE_V8_ID_DEFINE_NAMED_OWN_PROPERTY_IN_LITERAL = 223,
    XXBYTE_V8_ID_SET_PRIVATE_FIELD = 224,
    XXBYTE_V8_ID_THROW_REFERENCE_ERROR_IF_TDZ_HOLE = 225,
    XXBYTE_V8_ID_THROW_SUPER_NOT_CALLED_IF_TDZ_HOLE = 226,
    XXBYTE_V8_ID_THROW_SUPER_ALREADY_CALLED_IF_NOT_TDZ_HOLE = 227,
    XXBYTE_V8_ID_ARRAY_DESTRUCTURE = 228,
    XXBYTE_V8_ID_COUNT = 229
} xxbyte_v8_id;

/* The bytecode name of id (without a .Wide suffix), or NULL. */
XXBYTE_API const char *xxbyte_v8_mnemonic(uint32_t id);

/* The name of bytecode byte opcode in mode, or NULL when it is undefined
 * there or the mode is unsupported. */
XXBYTE_API const char *xxbyte_v8_opname(uint32_t mode, uint32_t opcode);

/* The number of bytecodes of mode (0 when unsupported). */
XXBYTE_API uint32_t xxbyte_v8_opcode_count(uint32_t mode);

/* The XXBYTE_V8_OPERAND_* type of operand operand_index of a decoded V8
 * instruction (a fused register list reports REG_LIST / REG_OUT_LIST), or
 * XXBYTE_V8_OPERAND_NONE. */
XXBYTE_API uint32_t xxbyte_v8_operand_type(const xxbyte_instruction *instruction, uint32_t operand_index);

/* Table names of mode, or NULL when the id is beyond the table: runtime
 * functions ("DefineClass", inline ones "_CreateIterResultObject"),
 * intrinsics (as their inline runtime function, "_GeneratorClose"),
 * native context fields by context index ("array_push"), abort reasons
 * (the message). */
XXBYTE_API const char *xxbyte_v8_runtime_name(uint32_t mode, uint32_t id);
XXBYTE_API const char *xxbyte_v8_intrinsic_name(uint32_t mode, uint32_t id);
XXBYTE_API const char *xxbyte_v8_native_context_name(uint32_t mode, uint32_t index);
XXBYTE_API const char *xxbyte_v8_abort_reason(uint32_t mode, uint32_t reason);
/* The number of runtime functions of mode (0 when unsupported). */
XXBYTE_API uint32_t xxbyte_v8_runtime_count(uint32_t mode);

/* Writes the --print-bytecode name of register index (an operand value)
 * in mode, "r3", "a0", "<this>", "<context>". Returns the length (0 when
 * the mode is unsupported); buffer as for xxbyte_format. */
XXBYTE_API size_t xxbyte_v8_register_name(uint32_t mode, int64_t index, char *buffer, size_t buffer_size);

/* The number of jump table entries of a SwitchOnSmiNoFeedback /
 * SwitchOnGeneratorState instruction (its table_length operand), else 0. */
XXBYTE_API uint32_t xxbyte_v8_switch_count(const xxbyte_instruction *instruction);

/* Reads jump table entry index of a switch instruction from the constant
 * pool of context: key = case_value_base + index, target = the absolute
 * address. XXBYTE_STATUS_INVALID_ARGUMENT for a non-switch, a NULL
 * argument or index >= xxbyte_v8_switch_count; XXBYTE_STATUS_INVALID_INSTRUCTION
 * when the entry is outside the pool or not a Smi (V8 skips such entries). */
XXBYTE_API xxbyte_status xxbyte_v8_switch_entry(const xxbyte_v8_context *context, const xxbyte_instruction *instruction, uint32_t index, int64_t *key, int64_t *target);

/* An xxbyte_resolver whose context is a const xxbyte_v8_context *: a
 * constant pool INDEX operand with a non-NULL entry prints as
 * [index:text] (the 14.6+ --print-bytecode form); everything else keeps
 * the default text. */
XXBYTE_API size_t xxbyte_v8_resolve(void *context, const xxbyte_instruction *instruction, uint32_t operand_index, char *buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif

#endif
