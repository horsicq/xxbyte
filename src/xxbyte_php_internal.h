/* Copyright (c) 2026 hors<horsicq@gmail.com> */

#ifndef XXBYTE_PHP_INTERNAL_H
#define XXBYTE_PHP_INTERNAL_H

#include "xxbyte_internal.h"

#if XXBYTE_USE_PHP

#include "xxbyte/xxbyte_php.h"

/* Supported releases, oldest first: 7.0 to 7.4, 8.0 to 8.6. */
#define PHP_VERSION_COUNT 12u

/* Normalized operand specifications of an opcode (from the release's
 * ZEND_VM_OP_* and ZEND_VM_EXT_* flags). */
enum {
    PHP_OPK_NONE = 0,
    PHP_OPK_NUM = 1,
    PHP_OPK_JMP_ADDR = 2,
    PHP_OPK_TRY_CATCH = 3,
    PHP_OPK_LIVE_RANGE = 4,
    PHP_OPK_LOOP_END = 5,
    PHP_OPK_THIS = 6,
    PHP_OPK_NEXT = 7,
    PHP_OPK_CLASS_FETCH = 8,
    PHP_OPK_CONSTRUCTOR = 9,
    PHP_OPK_CONST_FETCH = 10,
    PHP_OPK_CACHE_SLOT = 11
};

enum {
    PHP_EXTK_NONE = 0,
    PHP_EXTK_NUM = 1,
    PHP_EXTK_LAST_CATCH = 2,
    PHP_EXTK_JMP_ADDR = 3,
    PHP_EXTK_OP = 4,
    PHP_EXTK_DIM_OBJ = 5,
    PHP_EXTK_CLASS_FETCH = 6,
    PHP_EXTK_CONST_FETCH = 7,
    PHP_EXTK_TYPE = 8,
    PHP_EXTK_EVAL = 9,
    PHP_EXTK_TYPE_MASK = 10,
    PHP_EXTK_SRC = 11
};

#define PHP_EXTB_VAR_FETCH 0x01u
#define PHP_EXTB_ISSET 0x02u
#define PHP_EXTB_ARG_NUM 0x04u
#define PHP_EXTB_CACHE_SLOT 0x08u
#define PHP_EXTB_ARRAY_INIT 0x10u
#define PHP_EXTB_REF 0x20u
#define PHP_EXTB_FETCH_REF 0x40u
#define PHP_EXTB_DIM_WRITE 0x80u
/* Bits that make the formatter print extended_value. */
#define PHP_EXTB_PRINTED (PHP_EXTB_VAR_FETCH | PHP_EXTB_ISSET | PHP_EXTB_ARG_NUM | PHP_EXTB_ARRAY_INIT | PHP_EXTB_REF | PHP_EXTB_FETCH_REF | PHP_EXTB_DIM_WRITE)

#define PHP_FLAGS_OP1(f) ((f) & 0xFu)
#define PHP_FLAGS_OP2(f) (((f) >> 4) & 0xFu)
#define PHP_FLAGS_EXT(f) (((f) >> 8) & 0xFu)
#define PHP_FLAGS_EXTB(f) (((f) >> 12) & 0xFFu)

typedef struct php_opcode_info {
    const char *name; /* without the ZEND_ prefix; NULL = undefined */
    uint32_t flags;   /* PHP_FLAGS_* */
    uint16_t group;   /* XXBYTE_GROUP_* */
    uint16_t id;      /* xxbyte_php_id */
} php_opcode_info;

typedef struct php_opcode_table {
    const php_opcode_info *ops;
    uint32_t count;
} php_opcode_table;

extern const php_opcode_table php_opcode_tables[PHP_VERSION_COUNT];
extern const char *const php_id_names[XXBYTE_PHP_ID_MAX];

/* Structure layouts and constants of one release (64-bit builds), from
 * the release's headers. Offsets are in bytes. */
typedef struct php_version {
    uint8_t major;
    uint8_t minor;
    uint8_t frame_slot;       /* ZEND_CALL_FRAME_SLOT */
    uint8_t const_scale;      /* CONST operands hold index * const_scale */
    uint8_t is_unused;        /* IS_UNUSED */
    uint8_t is_cv;            /* IS_CV */
    uint8_t result_extra;     /* extra result_type bits (EXT_TYPE_UNUSED,
                                 IS_SMART_BRANCH_*) */
    uint8_t hash_packed;      /* HASH_FLAG_PACKED */
    uint8_t hash_uninit;      /* HASH_FLAG_UNINITIALIZED (7.3+), else 0 */
    uint8_t hash_initialized; /* HASH_FLAG_INITIALIZED (7.0-7.2), else 0 */
    uint8_t packed_zvals;     /* packed arrays store zvals (8.2+) */
    /* zval type codes */
    uint8_t t_constant; /* IS_CONSTANT (7.0-7.2), else 0xFF */
    uint8_t t_constant_ast;
    uint8_t t_indirect;
    uint8_t t_ptr;
    uint8_t t_alias_ptr; /* 0xFF when absent */
    uint8_t t_bool;      /* _IS_BOOL */
    uint8_t t_callable;
    uint8_t t_void;     /* 0xFF when absent */
    uint8_t t_never;    /* 0xFF when absent */
    uint8_t t_iterable; /* 0xFF when absent */
    /* dump constants */
    uint32_t fetch_global, fetch_local, fetch_global_lock, fetch_type_mask;
    uint32_t isset, isempty, quick_set, fetch_arg_mask;
    uint32_t array_size_shift, array_not_packed, array_element_ref;
    uint32_t fetch_obj_flags, fetch_ref, fetch_dim_write, fetch_obj_write;
    uint32_t last_catch, free_on_return;
    uint32_t const_unqualified, const_in_namespace, const_class, const_unqualified_in_namespace;
    uint32_t returns_value, returns_function;
    uint32_t eval_eval, eval_include, eval_include_once, eval_require, eval_require_once;
    uint32_t class_self, class_parent, class_static, class_auto, class_interface, class_trait, class_mask;
    uint32_t class_no_autoload, class_silent, class_exception;
    uint32_t live_tmpvar, live_loop, live_silence, live_rope, live_new, live_mask;
    uint32_t acc_abstract, acc_trait_clone;
    /* layouts */
    uint32_t script_filename, script_main, script_functions, script_classes, script_size;
    uint32_t oa_size, oa_type, oa_fn_flags, oa_function_name, oa_scope, oa_num_args, oa_required_num_args;
    uint32_t oa_last, oa_opcodes, oa_last_var, oa_T, oa_vars, oa_last_try_catch, oa_try_catch;
    uint32_t oa_filename, oa_line_start, oa_line_end, oa_last_literal, oa_literals;
    uint32_t oa_last_live_range, oa_live_range; /* 7.0: last_brk_cont (no live ranges): 0 */
    uint32_t oa_num_dynamic, oa_dynamic;        /* 8.1+, else 0 */
    uint32_t live_range_size;
    uint32_t ce_name, ce_function_table, ce_properties_info;
    uint32_t pi_name, pi_ce, pi_hooks; /* pi_hooks: 8.4+, else 0 */
    uint32_t hook_count;
} php_version;

extern const php_version php_versions[PHP_VERSION_COUNT];

/* VM handler numbering of one release (7.1+). */
#define PHP_HRULE_OP1 1u
#define PHP_HRULE_OP2 2u
#define PHP_HRULE_OP_DATA 4u
#define PHP_HRULE_RETVAL 8u
#define PHP_HRULE_QUICK_ARG 16u
#define PHP_HRULE_SMART_BRANCH 32u
#define PHP_HRULE_DIM_OBJ 64u
#define PHP_HRULE_ISSET 128u
#define PHP_HRULE_OBSERVER 256u

typedef struct php_handler_table {
    const uint16_t *base;  /* first handler of each opcode */
    const uint16_t *rules; /* PHP_HRULE_* */
    uint32_t count;        /* opcodes */
    uint32_t labels;       /* handlers */
    uint32_t special;      /* first type-specialized handler */
} php_handler_table;

extern const php_handler_table php_handler_tables[PHP_VERSION_COUNT];

/* The type-specialized handlers zend_vm_set_opcode_handler_ex picks. */
typedef struct php_special_handler {
    uint8_t opcode;
    uint16_t base;
    uint16_t rules; /* PHP_HRULE_* */
} php_special_handler;

typedef struct php_special_table {
    const php_special_handler *entries; /* by opcode */
    uint32_t count;
} php_special_table;

extern const php_special_table php_special_tables[PHP_VERSION_COUNT];

/* The version index of a checked mode. */
uint32_t php_version_index(uint32_t mode);

/* Raw zend_op fields. */
typedef struct php_raw_op {
    uint64_t handler;
    uint32_t op1, op2, result, extended_value, lineno;
    uint8_t opcode, op1_type, op2_type, result_type;
} php_raw_op;

void php_read_raw(const uint8_t *p, php_raw_op *op);

/* The file-side of a decode context: implemented in xxbyte_php_file.c. */
int php_unit_jump_table(const xxbyte_php_unit *unit, uint32_t literal, uint32_t *count);
int php_unit_jump_entry(const xxbyte_php_unit *unit, uint32_t literal, uint32_t index, xxbyte_php_jump_entry *entry);

/* list_file. */
xxbyte_status php_list_file(uint32_t mode, const uint8_t *data, size_t size, uint32_t listing_flags, xx_buf_t *out);

/* xxbyte_php_resolve's text for an operand, without its length limit:
 * *declined is set when it gives none. Returns 0 on allocation failure. */
int php_append_resolved(const xxbyte_php_unit *unit, const xxbyte_instruction *ins, uint32_t operand_index, xx_buf_t *out, int *declined);

/* Appends text with the escaping of the PHP 8.5+ opcode dumper (\n, \r,
 * \t, \f, \v, \e, \\, \" and \xHH for other bytes outside 0x20-0x7E). */
int php_fmt_escaped(xx_buf_t *out, const uint8_t *text, size_t size);

#endif

#endif
