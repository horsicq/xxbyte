#include "xxbyte_php_internal.h"

#if XXBYTE_USE_PHP

/* Operand kinds of the op_type byte. */
enum {
    PT_UNUSED,
    PT_CONST,
    PT_TMP,
    PT_VAR,
    PT_CV,
    PT_BAD
};

#define PHP_TYPE_CONST 1u
#define PHP_TYPE_TMP 2u
#define PHP_TYPE_VAR 4u

uint32_t php_version_index(uint32_t mode)
{
    uint32_t major = XXBYTE_PHP_MODE_MAJOR(mode);
    uint32_t minor = XXBYTE_PHP_MODE_MINOR(mode);
    return major == 7u ? minor : 5u + minor;
}

static xxbyte_status php_check_mode(uint32_t mode)
{
    uint32_t major = XXBYTE_PHP_MODE_MAJOR(mode);
    uint32_t minor = XXBYTE_PHP_MODE_MINOR(mode);

    if ((mode & ~0xFFFFu) != 0) {
        return XXBYTE_STATUS_UNSUPPORTED_MODE;
    }
    if ((major == 7u && minor <= 4u) || (major == 8u && minor <= 6u)) {
        return XXBYTE_STATUS_OK;
    }
    return XXBYTE_STATUS_UNSUPPORTED_MODE;
}

void php_read_raw(const uint8_t *p, php_raw_op *op)
{
    op->handler = xxbyte_u64le(p);
    op->op1 = xxbyte_u32le(p + 8);
    op->op2 = xxbyte_u32le(p + 12);
    op->result = xxbyte_u32le(p + 16);
    op->extended_value = xxbyte_u32le(p + 20);
    op->lineno = xxbyte_u32le(p + 24);
    op->opcode = p[28];
    op->op1_type = p[29];
    op->op2_type = p[30];
    op->result_type = p[31];
}

xxbyte_status xxbyte_php_read_op(const uint8_t *code, size_t size, xxbyte_php_op *op)
{
    php_raw_op raw;

    if (code == NULL || op == NULL) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    if (size < XXBYTE_PHP_OP_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    php_read_raw(code, &raw);
    op->handler = raw.handler;
    op->op1 = raw.op1;
    op->op2 = raw.op2;
    op->result = raw.result;
    op->extended_value = raw.extended_value;
    op->lineno = raw.lineno;
    op->opcode = raw.opcode;
    op->op1_type = raw.op1_type;
    op->op2_type = raw.op2_type;
    op->result_type = raw.result_type;
    return XXBYTE_STATUS_OK;
}

const char *xxbyte_php_mnemonic(uint32_t id)
{
    return id != 0u && id < (uint32_t)XXBYTE_PHP_ID_MAX ? php_id_names[id] : NULL;
}

const char *xxbyte_php_opname(uint32_t mode, uint32_t opcode)
{
    const php_opcode_table *table;

    if (php_check_mode(mode) != XXBYTE_STATUS_OK) {
        return NULL;
    }
    table = &php_opcode_tables[php_version_index(mode)];
    return opcode < table->count ? table->ops[opcode].name : NULL;
}

static int php_type_kind(const php_version *v, uint32_t type)
{
    /* 7.0-7.2 compilers leave 0 in operands they never set: unused too. */
    if (type == v->is_unused || type == 0u) {
        return PT_UNUSED;
    }
    if (type == v->is_cv) {
        return PT_CV;
    }
    switch (type) {
        case PHP_TYPE_CONST: return PT_CONST;
        case PHP_TYPE_TMP: return PT_TMP;
        case PHP_TYPE_VAR: return PT_VAR;
        default: return PT_BAD;
    }
}

/* The kind of a result_type: the dumper reads the TMP_VAR/VAR bits, extra
 * bits aside. */
static int php_result_kind(const php_version *v, uint32_t type)
{
    uint32_t base = type & ~(uint32_t)v->result_extra;
    if (base != type && base != PHP_TYPE_TMP && base != PHP_TYPE_VAR && !(v->major == 7 && base == v->is_unused)) {
        return PT_BAD;
    }
    return php_type_kind(v, base);
}

static xxbyte_operand *php_add(xxbyte_instruction *ins, xxbyte_operand_type type, uint32_t kind, uint16_t flags, uint32_t size, int64_t value, uint64_t extra)
{
    xxbyte_operand *op = xxbyte_add_operand(ins, type, size, value);
    if (op != NULL) {
        op->kind = (uint8_t)kind;
        op->flags = flags;
        op->extra = extra;
    }
    return op;
}

/* A TMP/VAR/CV slot offset to its number, or -1. */
static int64_t php_var_number(const php_version *v, uint32_t raw)
{
    if ((raw & 15u) != 0u || (raw >> 4) < v->frame_slot) {
        return -1;
    }
    return (int64_t)((raw >> 4) - v->frame_slot);
}

static int php_branch(xxbyte_instruction *ins, uint64_t address, uint32_t raw, uint16_t field, uint64_t type_bits)
{
    int32_t offset = (int32_t)raw;
    if ((offset & 31) != 0) {
        return 0;
    }
    return php_add(ins, XXBYTE_OPERAND_BRANCH, 0, field, 4, (int64_t)address + offset, raw | type_bits) != NULL;
}

/* op1 or op2. Returns 0 for an invalid encoding. */
static int php_add_op(xxbyte_instruction *ins, const php_version *v, const xxbyte_php_unit *unit, uint64_t address, uint32_t type, uint32_t raw, uint32_t opk,
                      uint16_t field, uint32_t ext)
{
    uint64_t extra = raw | ((uint64_t)type << 32);
    int64_t n;

    switch (php_type_kind(v, type)) {
        case PT_CONST:
            if (raw % v->const_scale != 0u) {
                return 0;
            }
            if (unit != NULL && unit->op_array != NULL && raw / v->const_scale >= unit->op_array->literal_count) {
                return 0;
            }
            return php_add(ins, XXBYTE_OPERAND_INDEX, XXBYTE_INDEX_CONSTANT, field, 4, (int64_t)(raw / v->const_scale), extra) != NULL;
        case PT_TMP:
        case PT_VAR:
        case PT_CV:
            n = php_var_number(v, raw);
            if (n < 0) {
                return 0;
            }
            return php_add(ins, XXBYTE_OPERAND_INDEX,
                           php_type_kind(v, type) == PT_CV ? XXBYTE_INDEX_LOCAL : (php_type_kind(v, type) == PT_TMP ? XXBYTE_PHP_INDEX_TMP : XXBYTE_PHP_INDEX_VAR), field,
                           4, n, extra) != NULL;
        case PT_UNUSED:
            switch (opk) {
                case PHP_OPK_JMP_ADDR:
                    /* 7.1+ CATCH: no jump for the last catch. */
                    if (field == XXBYTE_PHP_OPERAND_FLAG_OP2 && ins->id == XXBYTE_PHP_ID_CATCH && (ext & v->last_catch) != 0u && v->last_catch != 0u) {
                        return 1;
                    }
                    return php_branch(ins, address, raw, field, (uint64_t)type << 32);
                case PHP_OPK_LIVE_RANGE:
                case PHP_OPK_LOOP_END:
                    /* Bit 40: extended_value has ZEND_FREE_ON_RETURN. */
                    if ((ext & v->free_on_return) != 0u) {
                        extra |= UINT64_C(1) << 40;
                    }
                    return php_add(ins, XXBYTE_OPERAND_IMMEDIATE, 0, field, 4, raw, extra) != NULL;
                case PHP_OPK_NUM:
                case PHP_OPK_TRY_CATCH:
                case PHP_OPK_CLASS_FETCH:
                case PHP_OPK_CONST_FETCH: return php_add(ins, XXBYTE_OPERAND_IMMEDIATE, 0, field, 4, raw, extra) != NULL;
                case PHP_OPK_THIS:
                case PHP_OPK_NEXT:
                case PHP_OPK_CONSTRUCTOR: return php_add(ins, XXBYTE_OPERAND_IMMEDIATE, 0, (uint16_t)(field | XXBYTE_OPERAND_FLAG_IMPLICIT), 0, raw, extra) != NULL;
                default: return 1;
            }
        default: return 0;
    }
}

static int php_is_switch(uint32_t id)
{
    return id == XXBYTE_PHP_ID_SWITCH_LONG || id == XXBYTE_PHP_ID_SWITCH_STRING || id == XXBYTE_PHP_ID_MATCH;
}

static int php_is_frameless(uint32_t id)
{
    return id == XXBYTE_PHP_ID_FRAMELESS_ICALL_0 || id == XXBYTE_PHP_ID_FRAMELESS_ICALL_1 || id == XXBYTE_PHP_ID_FRAMELESS_ICALL_2 ||
           id == XXBYTE_PHP_ID_FRAMELESS_ICALL_3;
}

static xxbyte_status php_decode(uint32_t mode, const void *context, const uint8_t *code, size_t code_size, uint64_t address, xxbyte_instruction *ins)
{
    uint32_t vi = php_version_index(mode);
    const php_version *v = &php_versions[vi];
    const php_opcode_table *table = &php_opcode_tables[vi];
    const xxbyte_php_unit *unit = (const xxbyte_php_unit *)context;
    const php_opcode_info *info;
    php_raw_op op;
    uint32_t ek, eb;
    int rk;

    if (code_size < XXBYTE_PHP_OP_SIZE) {
        return XXBYTE_STATUS_TRUNCATED;
    }
    if (unit != NULL && (unit->file == NULL || unit->op_array == NULL)) {
        unit = NULL;
    }
    php_read_raw(code, &op);
    if (op.opcode >= table->count || table->ops[op.opcode].name == NULL) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    info = &table->ops[op.opcode];
    rk = php_result_kind(v, op.result_type);
    if (php_type_kind(v, op.op1_type) == PT_BAD || php_type_kind(v, op.op2_type) == PT_BAD || rk == PT_BAD) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    ins->size = XXBYTE_PHP_OP_SIZE;
    ins->opcode = op.opcode;
    ins->id = info->id;
    ins->mnemonic = info->name;
    ins->group = info->group;

    ek = PHP_FLAGS_EXT(info->flags);
    eb = PHP_FLAGS_EXTB(info->flags);
    if (php_is_frameless(ins->id)) {
        if (php_add(ins, XXBYTE_OPERAND_INDEX, XXBYTE_PHP_INDEX_FRAMELESS, XXBYTE_PHP_OPERAND_FLAG_EXTENDED, 4, op.extended_value, op.extended_value) == NULL) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    } else if ((ek != PHP_EXTK_NONE && ek != PHP_EXTK_LAST_CATCH && ek != PHP_EXTK_JMP_ADDR) || (ek != PHP_EXTK_JMP_ADDR && (eb & PHP_EXTB_PRINTED) != 0u)) {
        if (php_add(ins, XXBYTE_OPERAND_IMMEDIATE, 0, XXBYTE_PHP_OPERAND_FLAG_EXTENDED, 4, op.extended_value, op.extended_value) == NULL) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    if (!php_add_op(ins, v, unit, address, op.op1_type, op.op1, PHP_FLAGS_OP1(info->flags), XXBYTE_PHP_OPERAND_FLAG_OP1, op.extended_value) ||
        !php_add_op(ins, v, unit, address, op.op2_type, op.op2, PHP_FLAGS_OP2(info->flags), XXBYTE_PHP_OPERAND_FLAG_OP2, op.extended_value)) {
        return XXBYTE_STATUS_INVALID_INSTRUCTION;
    }
    if (ek == PHP_EXTK_JMP_ADDR) {
        /* 7.0 to 7.2: the last CATCH has no jump (result.num set). */
        int skip = ins->id == XXBYTE_PHP_ID_CATCH && v->last_catch == 0u && op.result != 0u;
        if (!skip && !php_branch(ins, address, op.extended_value, XXBYTE_PHP_OPERAND_FLAG_EXTENDED, 0)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    /* 7.0 keeps absolute opline numbers: CATCH's next catch in
     * extended_value (unless it is the last, result.num != 0), and the
     * op2 of FAST_CALL and FAST_RET when extended_value is set. */
    if (vi == 0u) {
        int64_t target = -1;
        uint16_t field = 0;
        if (ins->id == XXBYTE_PHP_ID_CATCH && op.result == 0u) {
            target = (int64_t)op.extended_value * XXBYTE_PHP_OP_SIZE;
            field = XXBYTE_PHP_OPERAND_FLAG_EXTENDED;
        } else if ((ins->id == XXBYTE_PHP_ID_FAST_CALL || ins->id == XXBYTE_PHP_ID_FAST_RET) && op.extended_value != 0u && php_type_kind(v, op.op2_type) == PT_UNUSED) {
            target = (int64_t)op.op2 * XXBYTE_PHP_OP_SIZE;
            field = XXBYTE_PHP_OPERAND_FLAG_OP2;
        }
        if (target >= 0 && php_add(ins, XXBYTE_OPERAND_BRANCH, 0, field, 4, target, field == XXBYTE_PHP_OPERAND_FLAG_OP2 ? op.op2 : op.extended_value) == NULL) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    if (ins->id == XXBYTE_PHP_ID_CATCH) {
        uint32_t i, jumps = 0;
        for (i = 0; i < ins->operand_count; ++i) {
            jumps += ins->operands[i].type == XXBYTE_OPERAND_BRANCH;
        }
        if (jumps == 0u) {
            ins->group = XXBYTE_GROUP_NONE;
        }
    }
    if (rk == PT_CONST) {
        if (!php_add_op(ins, v, unit, address, PHP_TYPE_CONST, op.result, PHP_OPK_NONE, XXBYTE_PHP_OPERAND_FLAG_RESULT, op.extended_value)) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    } else if (rk == PT_TMP || rk == PT_VAR || rk == PT_CV) {
        int64_t n = php_var_number(v, op.result);
        uint32_t kind = rk == PT_CV ? XXBYTE_INDEX_LOCAL : (rk == PT_TMP ? XXBYTE_PHP_INDEX_TMP : XXBYTE_PHP_INDEX_VAR);
        if (n < 0 || php_add(ins, XXBYTE_OPERAND_INDEX, kind, XXBYTE_PHP_OPERAND_FLAG_RESULT, 4, n, op.result | ((uint64_t)op.result_type << 32)) == NULL) {
            return XXBYTE_STATUS_INVALID_INSTRUCTION;
        }
    }
    if (php_is_switch(ins->id) && unit != NULL && php_type_kind(v, op.op2_type) == PT_CONST) {
        uint32_t i, count = 0;
        for (i = 0; i < ins->operand_count; ++i) {
            xxbyte_operand *o = &ins->operands[i];
            if ((o->flags & XXBYTE_PHP_OPERAND_FLAG_OP2) != 0u && o->type == XXBYTE_OPERAND_INDEX && php_unit_jump_table(unit, (uint32_t)o->value, &count)) {
                o->extra = (uint64_t)(uintptr_t)unit;
                ins->table_count = count;
                ins->table_offset = 0;
                ins->group |= XXBYTE_GROUP_SWITCH;
            }
        }
    }
    return XXBYTE_STATUS_OK;
}

static const xxbyte_operand *php_find(const xxbyte_instruction *ins, uint16_t field, uint32_t *index)
{
    uint32_t i;
    for (i = 0; i < ins->operand_count; ++i) {
        if ((ins->operands[i].flags & field) != 0u) {
            if (index != NULL) {
                *index = i;
            }
            return &ins->operands[i];
        }
    }
    return NULL;
}

static const xxbyte_php_unit *php_table_unit(const xxbyte_instruction *ins, uint32_t *literal)
{
    uint32_t i;
    if (ins->table_count == 0u || !php_is_switch(ins->id)) {
        return NULL;
    }
    for (i = 0; i < ins->operand_count; ++i) {
        const xxbyte_operand *o = &ins->operands[i];
        if ((o->flags & XXBYTE_PHP_OPERAND_FLAG_OP2) != 0u && o->type == XXBYTE_OPERAND_INDEX && o->kind == XXBYTE_INDEX_CONSTANT) {
            *literal = (uint32_t)o->value;
            return (const xxbyte_php_unit *)(uintptr_t)o->extra;
        }
    }
    return NULL;
}

static xxbyte_status php_switch_entry(const xxbyte_instruction *ins, uint32_t index, int64_t *key, int64_t *target)
{
    uint32_t literal = 0;
    const xxbyte_php_unit *unit = php_table_unit(ins, &literal);
    xxbyte_php_jump_entry entry;

    if (unit == NULL || !php_unit_jump_entry(unit, literal, index, &entry)) {
        return XXBYTE_STATUS_INVALID_ARGUMENT;
    }
    *key = entry.is_string ? (int64_t)index : entry.key;
    *target = (int64_t)ins->address + entry.offset;
    return XXBYTE_STATUS_OK;
}

static size_t php_unit_size(uint32_t mode)
{
    (void)mode;
    return XXBYTE_PHP_OP_SIZE;
}

/* ---- formatting ---- */

static int php_s(xxbyte_fmt *fmt, const char *text)
{
    return xxbyte_fmt_str(fmt, text);
}

static int php_hex(xxbyte_fmt *fmt, int64_t value)
{
    if (value < 0) {
        return xxbyte_fmt_printf(fmt, "-0x%llX", (unsigned long long)(0 - (uint64_t)value));
    }
    return xxbyte_fmt_printf(fmt, "0x%llX", (unsigned long long)value);
}

static int php_class_fetch(xxbyte_fmt *fmt, const php_version *v, uint32_t value)
{
    uint32_t t = value & v->class_mask;
    const char *s = NULL;
    if (t == v->class_self) {
        s = " (self)";
    } else if (t == v->class_parent) {
        s = " (parent)";
    } else if (t == v->class_static) {
        s = " (static)";
    } else if (t == v->class_auto) {
        s = " (auto)";
    } else if (t == v->class_interface) {
        s = " (interface)";
    } else if (t == v->class_trait) {
        s = " (trait)";
    }
    if (s != NULL && !php_s(fmt, s)) {
        return 0;
    }
    if ((value & v->class_no_autoload) != 0u && !php_s(fmt, " (no-autoload)")) {
        return 0;
    }
    if ((value & v->class_silent) != 0u && !php_s(fmt, " (silent)")) {
        return 0;
    }
    if (v->class_exception != 0u && (value & v->class_exception) != 0u && !php_s(fmt, " (exception)")) {
        return 0;
    }
    return 1;
}

static int php_type_name(xxbyte_fmt *fmt, const php_version *v, uint32_t value)
{
    static const char *const basic[10] = {NULL, " (null)", " (false)", " (true)", " (long)", " (double)", " (string)", " (array)", " (object)", " (resource)"};
    if (value >= 1u && value <= 9u) {
        return php_s(fmt, basic[value]);
    }
    if (value == v->t_bool) {
        return php_s(fmt, " (bool)");
    }
    if (value == v->t_callable) {
        return php_s(fmt, " (callable)");
    }
    if (value == v->t_void) {
        return php_s(fmt, " (void)");
    }
    /* The 8.0 dumper does not know IS_NEVER (8.1+) anyway. */
    if (v->t_never != 0xFFu && value == v->t_never) {
        return php_s(fmt, " (never)");
    }
    return php_s(fmt, " (\?\?\?)");
}

/* zend_dump_type_info for the basic type bits TYPE_CHECK masks use. */
static int php_type_info(xxbyte_fmt *fmt, uint32_t info)
{
    static const char *const names[] = {"undef", "null", "false", "true", "long", "double", "string", "array", "object", "resource", "ref"};
    int first = 1;
    uint32_t any = 0x3FEu;
    if (!php_s(fmt, " TYPE [")) {
        return 0;
    }
#define PHP_ITEM(text)                                                    \
    do {                                                                  \
        if ((!first && !php_s(fmt, ", ")) || !php_s(fmt, text)) return 0; \
        first = 0;                                                        \
    } while (0)
    if ((info & 1u) != 0u) {
        PHP_ITEM(names[0]);
    }
    if ((info & (1u << 10)) != 0u) {
        PHP_ITEM(names[10]);
    }
    if ((info & any) == any) {
        PHP_ITEM("any");
    } else {
        uint32_t t;
        if ((info & 2u) != 0u) {
            PHP_ITEM(names[1]);
        }
        if ((info & 12u) == 12u) {
            PHP_ITEM("bool");
        } else if ((info & 4u) != 0u) {
            PHP_ITEM(names[2]);
        } else if ((info & 8u) != 0u) {
            PHP_ITEM(names[3]);
        }
        for (t = 4; t <= 9u; ++t) {
            if ((info & (1u << t)) != 0u) {
                PHP_ITEM(names[t]);
            }
        }
    }
#undef PHP_ITEM
    return php_s(fmt, "]");
}

static int php_format_ext(xxbyte_fmt *fmt, const php_version *v, uint32_t vi, uint32_t flags, uint32_t value)
{
    uint32_t ek = PHP_FLAGS_EXT(flags);
    uint32_t eb = PHP_FLAGS_EXTB(flags);
    int seven = v->major == 7;

    switch (ek) {
        case PHP_EXTK_NUM: return xxbyte_fmt_printf(fmt, " %u", value);
        case PHP_EXTK_OP: {
            const php_opcode_table *t = &php_opcode_tables[vi];
            if (value < t->count && t->ops[value].name != NULL) {
                return xxbyte_fmt_printf(fmt, " (%s)", t->ops[value].name);
            }
            return xxbyte_fmt_printf(fmt, " (OP_%u)", value);
        }
        case PHP_EXTK_DIM_OBJ: {
            const php_opcode_table *t = &php_opcode_tables[vi];
            if (value < t->count && t->ops[value].name != NULL) {
                if (t->ops[value].id == XXBYTE_PHP_ID_ASSIGN_DIM) {
                    return php_s(fmt, " (dim)");
                }
                if (t->ops[value].id == XXBYTE_PHP_ID_ASSIGN_OBJ) {
                    return php_s(fmt, " (obj)");
                }
            }
            return 1;
        }
        case PHP_EXTK_CLASS_FETCH: return php_class_fetch(fmt, v, value);
        case PHP_EXTK_CONST_FETCH:
            if ((value & v->const_unqualified) != 0u && !php_s(fmt, " (unqualified)")) {
                return 0;
            }
            if ((value & v->const_class) != 0u && !php_s(fmt, " (__class__)")) {
                return 0;
            }
            if ((value & v->const_in_namespace) != 0u && !php_s(fmt, " (in-namespace)")) {
                return 0;
            }
            return 1;
        case PHP_EXTK_TYPE: return php_type_name(fmt, v, value);
        case PHP_EXTK_EVAL:
            if (value == v->eval_eval) {
                return php_s(fmt, " (eval)");
            }
            if (value == v->eval_include) {
                return php_s(fmt, " (include)");
            }
            if (value == v->eval_include_once) {
                return php_s(fmt, " (include_once)");
            }
            if (value == v->eval_require) {
                return php_s(fmt, " (require)");
            }
            if (value == v->eval_require_once) {
                return php_s(fmt, " (require_once)");
            }
            return php_s(fmt, " (\?\?\?)");
        case PHP_EXTK_TYPE_MASK:
            switch (value) {
                case 1u << 1: return php_s(fmt, " (null)");
                case 1u << 2: return php_s(fmt, " (false)");
                case 1u << 3: return php_s(fmt, " (true)");
                case 1u << 4: return php_s(fmt, " (long)");
                case 1u << 5: return php_s(fmt, " (double)");
                case 1u << 6: return php_s(fmt, " (string)");
                case 1u << 7: return php_s(fmt, " (array)");
                case 1u << 8: return php_s(fmt, " (object)");
                case 1u << 9: return php_s(fmt, " (resource)");
                case (1u << 2) | (1u << 3): return php_s(fmt, " (bool)");
                default: return seven ? php_s(fmt, " (\?\?\?)") : php_type_info(fmt, value);
            }
        case PHP_EXTK_SRC:
            if (value == v->returns_value) {
                return php_s(fmt, " (value)");
            }
            if ((v->major == 7 && v->minor <= 3) ? value == v->returns_function : (value & v->returns_function) != 0u) {
                return php_s(fmt, " (function)");
            }
            return 1;
        default: break;
    }
    if ((eb & PHP_EXTB_VAR_FETCH) != 0u) {
        const char *s = NULL;
        if (v->fetch_type_mask != 0u && seven && v->minor <= 2) {
            uint32_t t = value & v->fetch_type_mask;
            s = t == v->fetch_global ? " (global)" : (t == v->fetch_local ? " (local)" : (t == v->fetch_global_lock ? " (global+lock)" : NULL));
        } else if ((value & v->fetch_global) != 0u) {
            s = " (global)";
        } else if ((value & v->fetch_local) != 0u) {
            s = " (local)";
        } else if ((value & v->fetch_global_lock) != 0u) {
            s = " (global+lock)";
        }
        if (s != NULL && !php_s(fmt, s)) {
            return 0;
        }
    }
    if ((eb & PHP_EXTB_ISSET) != 0u) {
        if (seven && v->minor <= 2) {
            if (v->quick_set != 0u && (value & v->quick_set) != 0u && !php_s(fmt, " (quick)")) {
                return 0;
            }
            if ((value & v->isset) != 0u) {
                if (!php_s(fmt, " (isset)")) {
                    return 0;
                }
            } else if ((value & v->isempty) != 0u && !php_s(fmt, " (empty)")) {
                return 0;
            }
        } else if (!php_s(fmt, (value & v->isempty) == 0u ? " (isset)" : " (empty)")) {
            return 0;
        }
    }
    if ((eb & PHP_EXTB_ARG_NUM) != 0u && !xxbyte_fmt_printf(fmt, " %u", value & v->fetch_arg_mask)) {
        return 0;
    }
    if ((eb & PHP_EXTB_ARRAY_INIT) != 0u) {
        if (!xxbyte_fmt_printf(fmt, " %u", value >> v->array_size_shift)) {
            return 0;
        }
        if ((value & v->array_not_packed) == 0u && !php_s(fmt, " (packed)")) {
            return 0;
        }
    }
    if ((eb & PHP_EXTB_REF) != 0u && (value & v->array_element_ref) != 0u && !php_s(fmt, " (ref)")) {
        return 0;
    }
    if ((eb & (PHP_EXTB_FETCH_REF | PHP_EXTB_DIM_WRITE)) != 0u && v->fetch_obj_flags != 0u) {
        uint32_t f = value & v->fetch_obj_flags;
        if (f == v->fetch_ref) {
            return php_s(fmt, " (ref)");
        }
        if (f == v->fetch_dim_write) {
            return php_s(fmt, " (dim write)");
        }
        if (v->fetch_obj_write != 0u && f == v->fetch_obj_write) {
            return php_s(fmt, " (obj write)");
        }
    }
    return 1;
}

/* CONST, TMP, VAR and CV operands. */
/* Resolved text: xxbyte_php_resolve itself prints without the core's
 * length limit. 1 printed, 0 declined, -1 failure. */
static int php_resolve(xxbyte_fmt *fmt, const xxbyte_instruction *ins, uint32_t index)
{
    if (fmt->resolver == xxbyte_php_resolve) {
        int declined = 1;
        if (!php_append_resolved((const xxbyte_php_unit *)fmt->context, ins, index, fmt->out, &declined)) {
            return -1;
        }
        return declined ? 0 : 1;
    }
    return xxbyte_fmt_resolve(fmt, ins, index);
}

static int php_format_index(xxbyte_fmt *fmt, const xxbyte_instruction *ins, uint32_t index)
{
    const xxbyte_operand *o = &ins->operands[index];
    int r = php_resolve(fmt, ins, index);
    if (r != 0) {
        return r > 0;
    }
    switch (o->kind) {
        case XXBYTE_INDEX_CONSTANT: return xxbyte_fmt_printf(fmt, "#%llu", (unsigned long long)o->value);
        case XXBYTE_INDEX_LOCAL: return xxbyte_fmt_printf(fmt, "CV%llu", (unsigned long long)o->value);
        case XXBYTE_PHP_INDEX_VAR: return xxbyte_fmt_printf(fmt, "V%llu", (unsigned long long)o->value);
        case XXBYTE_PHP_INDEX_TMP: return xxbyte_fmt_printf(fmt, "T%llu", (unsigned long long)o->value);
        default: return xxbyte_fmt_printf(fmt, "%llu", (unsigned long long)o->value);
    }
}

static int php_format_branch(xxbyte_fmt *fmt, const xxbyte_instruction *ins, uint32_t index)
{
    int r = xxbyte_fmt_resolve(fmt, ins, index);
    if (r != 0) {
        return r > 0;
    }
    return php_hex(fmt, ins->operands[index].value);
}

static int php_format_unused(xxbyte_fmt *fmt, const php_version *v, const xxbyte_instruction *ins, uint32_t index, uint32_t opk)
{
    uint32_t value = (uint32_t)ins->operands[index].value;
    uint32_t freed = (uint32_t)(ins->operands[index].extra >> 40) & 1u;
    switch (opk) {
        case PHP_OPK_NUM: return xxbyte_fmt_printf(fmt, " %u", value);
        case PHP_OPK_TRY_CATCH: return value == 0xFFFFFFFFu ? 1 : xxbyte_fmt_printf(fmt, " try-catch(%u)", value);
        case PHP_OPK_LIVE_RANGE: return freed == 0u ? 1 : xxbyte_fmt_printf(fmt, " live-range(%u)", value);
        case PHP_OPK_LOOP_END: return freed == 0u ? 1 : xxbyte_fmt_printf(fmt, " loop-end(+%u)", value);
        case PHP_OPK_THIS: return php_s(fmt, " THIS");
        case PHP_OPK_NEXT: return php_s(fmt, " NEXT");
        case PHP_OPK_CONSTRUCTOR: return php_s(fmt, " CONSTRUCTOR");
        case PHP_OPK_CLASS_FETCH: return php_class_fetch(fmt, v, value);
        case PHP_OPK_CONST_FETCH:
            if (v->const_unqualified_in_namespace != 0u) {
                return (value & v->const_unqualified_in_namespace) == 0u ? 1 : php_s(fmt, " (unqualified-in-namespace)");
            }
            if ((value & v->const_unqualified) != 0u && !php_s(fmt, " (unqualified)")) {
                return 0;
            }
            return (value & v->const_in_namespace) == 0u ? 1 : php_s(fmt, " (in-namespace)");
        default: return 1;
    }
}

static int php_format_table(xxbyte_fmt *fmt, const xxbyte_instruction *ins)
{
    uint32_t literal = 0, i;
    const xxbyte_php_unit *unit = php_table_unit(ins, &literal);
    xxbyte_operand entry;

    memset(&entry, 0, sizeof(entry));
    entry.type = XXBYTE_OPERAND_BRANCH;
    entry.size = 4;
    for (i = 0; i < ins->table_count; ++i) {
        xxbyte_php_jump_entry e;
        int r;
        if (unit == NULL || !php_unit_jump_entry(unit, literal, i, &e)) {
            return 0;
        }
        if (e.is_string) {
            if (!php_s(fmt, " \"") || !php_fmt_escaped(fmt->out, e.string, e.string_size) || !php_s(fmt, "\":")) {
                return 0;
            }
        } else if (!xxbyte_fmt_printf(fmt, " %lld:", (long long)e.key)) {
            return 0;
        }
        entry.value = (int64_t)ins->address + e.offset;
        if (!php_s(fmt, " ")) {
            return 0;
        }
        r = xxbyte_fmt_resolve_entry(fmt, ins, &entry);
        if (r < 0 || (r == 0 && !php_hex(fmt, entry.value)) || !php_s(fmt, ",")) {
            return 0;
        }
    }
    return 1;
}

static int php_format_operands(const xxbyte_instruction *ins, xxbyte_fmt *fmt)
{
    uint32_t vi = php_version_index(ins->mode);
    const php_version *v = &php_versions[vi];
    const php_opcode_table *table = &php_opcode_tables[vi];
    uint32_t flags = ins->opcode < table->count ? table->ops[ins->opcode].flags : 0u;
    uint32_t i, idx;
    const xxbyte_operand *o;
    int is_switch = php_is_switch(ins->id);

    for (i = 0; i < ins->operand_count; ++i) {
        o = &ins->operands[i];
        if ((o->flags & XXBYTE_PHP_OPERAND_FLAG_EXTENDED) != 0u) {
            if (o->type == XXBYTE_OPERAND_INDEX && o->kind == XXBYTE_PHP_INDEX_FRAMELESS) {
                int r;
                if (!php_s(fmt, "(")) {
                    return 0;
                }
                r = php_resolve(fmt, ins, i);
                if (r < 0 || (r == 0 && !xxbyte_fmt_printf(fmt, "#%llu", (unsigned long long)o->value)) || !php_s(fmt, ")")) {
                    return 0;
                }
            } else if (o->type == XXBYTE_OPERAND_IMMEDIATE && !php_format_ext(fmt, v, vi, flags, (uint32_t)o->value)) {
                return 0;
            }
        }
    }
    for (idx = 0; idx < 2u; ++idx) {
        uint16_t field = idx == 0u ? XXBYTE_PHP_OPERAND_FLAG_OP1 : XXBYTE_PHP_OPERAND_FLAG_OP2;
        uint32_t opk = idx == 0u ? PHP_FLAGS_OP1(flags) : PHP_FLAGS_OP2(flags);
        uint32_t n = 0;
        o = php_find(ins, field, &n);
        if (o == NULL) {
            continue;
        }
        if (o->type == XXBYTE_OPERAND_INDEX) {
            if (idx == 1u && is_switch && o->kind == XXBYTE_INDEX_CONSTANT) {
                if (ins->table_count != 0u) {
                    if (!php_format_table(fmt, ins)) {
                        return 0;
                    }
                } else if (!php_s(fmt, " ") || !php_format_index(fmt, ins, n)) {
                    return 0;
                }
                if (!php_s(fmt, " default:")) {
                    return 0;
                }
                continue;
            }
            if (!php_s(fmt, " ") || !php_format_index(fmt, ins, n)) {
                return 0;
            }
        } else if (o->type == XXBYTE_OPERAND_BRANCH) {
            if (!php_s(fmt, " ") || !php_format_branch(fmt, ins, n)) {
                return 0;
            }
        } else if (!php_format_unused(fmt, v, ins, n, opk)) {
            return 0;
        }
    }
    for (i = 0; i < ins->operand_count; ++i) {
        o = &ins->operands[i];
        if ((o->flags & XXBYTE_PHP_OPERAND_FLAG_EXTENDED) != 0u && o->type == XXBYTE_OPERAND_BRANCH) {
            if (!php_s(fmt, " ") || !php_format_branch(fmt, ins, i)) {
                return 0;
            }
        }
    }
    o = php_find(ins, XXBYTE_PHP_OPERAND_FLAG_RESULT, &idx);
    if (o != NULL) {
        if (o->kind == XXBYTE_INDEX_CONSTANT) {
            return php_s(fmt, " ") && php_format_index(fmt, ins, idx);
        }
        /* 7.0: a result marked EXT_TYPE_UNUSED is computed and dropped;
         * it is not shown (as later releases, which leave it UNUSED). */
        if (v->major == 7 && v->minor == 0 && ((o->extra >> 32) & 0x20u) != 0u) {
            return 1;
        }
        return php_s(fmt, " -> ") && php_format_index(fmt, ins, idx);
    }
    return 1;
}

const xxbyte_family_ops xxbyte_php_ops = {
    XXBYTE_FAMILY_PHP, XXBYTE_PHP_MODE_DEFAULT, php_check_mode, php_decode, php_switch_entry, php_format_operands, php_unit_size, php_list_file,
};

#else
typedef int xxbyte_php_disabled;
#endif
