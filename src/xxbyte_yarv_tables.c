/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/*
 * Ruby YARV instruction tables, transcribed from the insns_info.inc that
 * each release tarball ships (generated from its insns.def and
 * opt_operand.def): 2.6.10, 2.7.8, 3.0.7, 3.1.7,
 * 3.2.11, 3.3.12, 3.4.11, 4.0.7; the x.y.0 releases have the same tables.
 * Opcode n of a release is entry n of its table; an entry holds the
 * mnemonic id and the index of its operand types, a string of Ruby's
 * TS_* letters. The tables were checked against RubyVM::INSTRUCTION_NAMES
 * of each release.
 */

#include "xxbyte_yarv_tables.h"

#if XXBYTE_USE_YARV

#define G_JUMP XXBYTE_GROUP_JUMP
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_CALL XXBYTE_GROUP_CALL
#define G_RET XXBYTE_GROUP_RETURN
#define G_THROW XXBYTE_GROUP_THROW
#define G_SWITCH XXBYTE_GROUP_SWITCH
#define G_STOP XXBYTE_GROUP_STOP

const yarv_mnemonic xxbyte_yarv_mnemonics[XXBYTE_YARV_ID_COUNT] = {
    {NULL, 0, 0},
    {"nop", XXBYTE_YARV_ID_NOP, 0},
    {"getlocal", XXBYTE_YARV_ID_GETLOCAL, 0},
    {"setlocal", XXBYTE_YARV_ID_SETLOCAL, 0},
    {"getblockparam", XXBYTE_YARV_ID_GETBLOCKPARAM, 0},
    {"setblockparam", XXBYTE_YARV_ID_SETBLOCKPARAM, 0},
    {"getblockparamproxy", XXBYTE_YARV_ID_GETBLOCKPARAMPROXY, 0},
    {"getspecial", XXBYTE_YARV_ID_GETSPECIAL, 0},
    {"setspecial", XXBYTE_YARV_ID_SETSPECIAL, 0},
    {"getinstancevariable", XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 0},
    {"setinstancevariable", XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 0},
    {"getclassvariable", XXBYTE_YARV_ID_GETCLASSVARIABLE, 0},
    {"setclassvariable", XXBYTE_YARV_ID_SETCLASSVARIABLE, 0},
    {"getconstant", XXBYTE_YARV_ID_GETCONSTANT, 0},
    {"setconstant", XXBYTE_YARV_ID_SETCONSTANT, 0},
    {"getglobal", XXBYTE_YARV_ID_GETGLOBAL, 0},
    {"setglobal", XXBYTE_YARV_ID_SETGLOBAL, 0},
    {"putnil", XXBYTE_YARV_ID_PUTNIL, 0},
    {"putself", XXBYTE_YARV_ID_PUTSELF, 0},
    {"putobject", XXBYTE_YARV_ID_PUTOBJECT, 0},
    {"putspecialobject", XXBYTE_YARV_ID_PUTSPECIALOBJECT, 0},
    {"putiseq", XXBYTE_YARV_ID_PUTISEQ, 0},
    {"putstring", XXBYTE_YARV_ID_PUTSTRING, 0},
    {"concatstrings", XXBYTE_YARV_ID_CONCATSTRINGS, 0},
    {"tostring", XXBYTE_YARV_ID_TOSTRING, 0},
    {"freezestring", XXBYTE_YARV_ID_FREEZESTRING, 0},
    {"toregexp", XXBYTE_YARV_ID_TOREGEXP, 0},
    {"intern", XXBYTE_YARV_ID_INTERN, 0},
    {"newarray", XXBYTE_YARV_ID_NEWARRAY, 0},
    {"duparray", XXBYTE_YARV_ID_DUPARRAY, 0},
    {"duphash", XXBYTE_YARV_ID_DUPHASH, 0},
    {"expandarray", XXBYTE_YARV_ID_EXPANDARRAY, 0},
    {"concatarray", XXBYTE_YARV_ID_CONCATARRAY, 0},
    {"splatarray", XXBYTE_YARV_ID_SPLATARRAY, 0},
    {"newhash", XXBYTE_YARV_ID_NEWHASH, 0},
    {"newrange", XXBYTE_YARV_ID_NEWRANGE, 0},
    {"pop", XXBYTE_YARV_ID_POP, 0},
    {"dup", XXBYTE_YARV_ID_DUP, 0},
    {"dupn", XXBYTE_YARV_ID_DUPN, 0},
    {"swap", XXBYTE_YARV_ID_SWAP, 0},
    {"reverse", XXBYTE_YARV_ID_REVERSE, 0},
    {"reput", XXBYTE_YARV_ID_REPUT, 0},
    {"topn", XXBYTE_YARV_ID_TOPN, 0},
    {"setn", XXBYTE_YARV_ID_SETN, 0},
    {"adjuststack", XXBYTE_YARV_ID_ADJUSTSTACK, 0},
    {"defined", XXBYTE_YARV_ID_DEFINED, 0},
    {"checkmatch", XXBYTE_YARV_ID_CHECKMATCH, 0},
    {"checkkeyword", XXBYTE_YARV_ID_CHECKKEYWORD, 0},
    {"checktype", XXBYTE_YARV_ID_CHECKTYPE, 0},
    {"defineclass", XXBYTE_YARV_ID_DEFINECLASS, G_CALL},
    {"send", XXBYTE_YARV_ID_SEND, G_CALL},
    {"opt_send_without_block", XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, G_CALL},
    {"opt_str_freeze", XXBYTE_YARV_ID_OPT_STR_FREEZE, 0},
    {"opt_str_uminus", XXBYTE_YARV_ID_OPT_STR_UMINUS, 0},
    {"opt_newarray_max", XXBYTE_YARV_ID_OPT_NEWARRAY_MAX, 0},
    {"opt_newarray_min", XXBYTE_YARV_ID_OPT_NEWARRAY_MIN, 0},
    {"invokesuper", XXBYTE_YARV_ID_INVOKESUPER, G_CALL},
    {"invokeblock", XXBYTE_YARV_ID_INVOKEBLOCK, G_CALL},
    {"leave", XXBYTE_YARV_ID_LEAVE, G_RET | G_STOP},
    {"throw", XXBYTE_YARV_ID_THROW, G_THROW | G_STOP},
    {"jump", XXBYTE_YARV_ID_JUMP, G_JUMP | G_STOP},
    {"branchif", XXBYTE_YARV_ID_BRANCHIF, G_COND},
    {"branchunless", XXBYTE_YARV_ID_BRANCHUNLESS, G_COND},
    {"branchnil", XXBYTE_YARV_ID_BRANCHNIL, G_COND},
    {"opt_getinlinecache", XXBYTE_YARV_ID_OPT_GETINLINECACHE, G_COND},
    {"opt_setinlinecache", XXBYTE_YARV_ID_OPT_SETINLINECACHE, 0},
    {"once", XXBYTE_YARV_ID_ONCE, G_CALL},
    {"opt_case_dispatch", XXBYTE_YARV_ID_OPT_CASE_DISPATCH, G_SWITCH | G_COND},
    {"opt_plus", XXBYTE_YARV_ID_OPT_PLUS, 0},
    {"opt_minus", XXBYTE_YARV_ID_OPT_MINUS, 0},
    {"opt_mult", XXBYTE_YARV_ID_OPT_MULT, 0},
    {"opt_div", XXBYTE_YARV_ID_OPT_DIV, 0},
    {"opt_mod", XXBYTE_YARV_ID_OPT_MOD, 0},
    {"opt_eq", XXBYTE_YARV_ID_OPT_EQ, 0},
    {"opt_neq", XXBYTE_YARV_ID_OPT_NEQ, 0},
    {"opt_lt", XXBYTE_YARV_ID_OPT_LT, 0},
    {"opt_le", XXBYTE_YARV_ID_OPT_LE, 0},
    {"opt_gt", XXBYTE_YARV_ID_OPT_GT, 0},
    {"opt_ge", XXBYTE_YARV_ID_OPT_GE, 0},
    {"opt_ltlt", XXBYTE_YARV_ID_OPT_LTLT, 0},
    {"opt_and", XXBYTE_YARV_ID_OPT_AND, 0},
    {"opt_or", XXBYTE_YARV_ID_OPT_OR, 0},
    {"opt_aref", XXBYTE_YARV_ID_OPT_AREF, 0},
    {"opt_aset", XXBYTE_YARV_ID_OPT_ASET, 0},
    {"opt_aset_with", XXBYTE_YARV_ID_OPT_ASET_WITH, 0},
    {"opt_aref_with", XXBYTE_YARV_ID_OPT_AREF_WITH, 0},
    {"opt_length", XXBYTE_YARV_ID_OPT_LENGTH, 0},
    {"opt_size", XXBYTE_YARV_ID_OPT_SIZE, 0},
    {"opt_empty_p", XXBYTE_YARV_ID_OPT_EMPTY_P, 0},
    {"opt_succ", XXBYTE_YARV_ID_OPT_SUCC, 0},
    {"opt_not", XXBYTE_YARV_ID_OPT_NOT, 0},
    {"opt_regexpmatch1", XXBYTE_YARV_ID_OPT_REGEXPMATCH1, 0},
    {"opt_regexpmatch2", XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 0},
    {"opt_call_c_function", XXBYTE_YARV_ID_OPT_CALL_C_FUNCTION, G_CALL},
    {"bitblt", XXBYTE_YARV_ID_BITBLT, 0},
    {"answer", XXBYTE_YARV_ID_ANSWER, 0},
    {"getlocal_WC_0", XXBYTE_YARV_ID_GETLOCAL_WC_0, 0},
    {"getlocal_WC_1", XXBYTE_YARV_ID_GETLOCAL_WC_1, 0},
    {"setlocal_WC_0", XXBYTE_YARV_ID_SETLOCAL_WC_0, 0},
    {"setlocal_WC_1", XXBYTE_YARV_ID_SETLOCAL_WC_1, 0},
    {"putobject_INT2FIX_0_", XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_, 0},
    {"putobject_INT2FIX_1_", XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_, 0},
    {"trace_nop", XXBYTE_YARV_ID_NOP, 0},
    {"trace_getlocal", XXBYTE_YARV_ID_GETLOCAL, 0},
    {"trace_setlocal", XXBYTE_YARV_ID_SETLOCAL, 0},
    {"trace_getblockparam", XXBYTE_YARV_ID_GETBLOCKPARAM, 0},
    {"trace_setblockparam", XXBYTE_YARV_ID_SETBLOCKPARAM, 0},
    {"trace_getblockparamproxy", XXBYTE_YARV_ID_GETBLOCKPARAMPROXY, 0},
    {"trace_getspecial", XXBYTE_YARV_ID_GETSPECIAL, 0},
    {"trace_setspecial", XXBYTE_YARV_ID_SETSPECIAL, 0},
    {"trace_getinstancevariable", XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 0},
    {"trace_setinstancevariable", XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 0},
    {"trace_getclassvariable", XXBYTE_YARV_ID_GETCLASSVARIABLE, 0},
    {"trace_setclassvariable", XXBYTE_YARV_ID_SETCLASSVARIABLE, 0},
    {"trace_getconstant", XXBYTE_YARV_ID_GETCONSTANT, 0},
    {"trace_setconstant", XXBYTE_YARV_ID_SETCONSTANT, 0},
    {"trace_getglobal", XXBYTE_YARV_ID_GETGLOBAL, 0},
    {"trace_setglobal", XXBYTE_YARV_ID_SETGLOBAL, 0},
    {"trace_putnil", XXBYTE_YARV_ID_PUTNIL, 0},
    {"trace_putself", XXBYTE_YARV_ID_PUTSELF, 0},
    {"trace_putobject", XXBYTE_YARV_ID_PUTOBJECT, 0},
    {"trace_putspecialobject", XXBYTE_YARV_ID_PUTSPECIALOBJECT, 0},
    {"trace_putiseq", XXBYTE_YARV_ID_PUTISEQ, 0},
    {"trace_putstring", XXBYTE_YARV_ID_PUTSTRING, 0},
    {"trace_concatstrings", XXBYTE_YARV_ID_CONCATSTRINGS, 0},
    {"trace_tostring", XXBYTE_YARV_ID_TOSTRING, 0},
    {"trace_freezestring", XXBYTE_YARV_ID_FREEZESTRING, 0},
    {"trace_toregexp", XXBYTE_YARV_ID_TOREGEXP, 0},
    {"trace_intern", XXBYTE_YARV_ID_INTERN, 0},
    {"trace_newarray", XXBYTE_YARV_ID_NEWARRAY, 0},
    {"trace_duparray", XXBYTE_YARV_ID_DUPARRAY, 0},
    {"trace_duphash", XXBYTE_YARV_ID_DUPHASH, 0},
    {"trace_expandarray", XXBYTE_YARV_ID_EXPANDARRAY, 0},
    {"trace_concatarray", XXBYTE_YARV_ID_CONCATARRAY, 0},
    {"trace_splatarray", XXBYTE_YARV_ID_SPLATARRAY, 0},
    {"trace_newhash", XXBYTE_YARV_ID_NEWHASH, 0},
    {"trace_newrange", XXBYTE_YARV_ID_NEWRANGE, 0},
    {"trace_pop", XXBYTE_YARV_ID_POP, 0},
    {"trace_dup", XXBYTE_YARV_ID_DUP, 0},
    {"trace_dupn", XXBYTE_YARV_ID_DUPN, 0},
    {"trace_swap", XXBYTE_YARV_ID_SWAP, 0},
    {"trace_reverse", XXBYTE_YARV_ID_REVERSE, 0},
    {"trace_reput", XXBYTE_YARV_ID_REPUT, 0},
    {"trace_topn", XXBYTE_YARV_ID_TOPN, 0},
    {"trace_setn", XXBYTE_YARV_ID_SETN, 0},
    {"trace_adjuststack", XXBYTE_YARV_ID_ADJUSTSTACK, 0},
    {"trace_defined", XXBYTE_YARV_ID_DEFINED, 0},
    {"trace_checkmatch", XXBYTE_YARV_ID_CHECKMATCH, 0},
    {"trace_checkkeyword", XXBYTE_YARV_ID_CHECKKEYWORD, 0},
    {"trace_checktype", XXBYTE_YARV_ID_CHECKTYPE, 0},
    {"trace_defineclass", XXBYTE_YARV_ID_DEFINECLASS, G_CALL},
    {"trace_send", XXBYTE_YARV_ID_SEND, G_CALL},
    {"trace_opt_send_without_block", XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, G_CALL},
    {"trace_opt_str_freeze", XXBYTE_YARV_ID_OPT_STR_FREEZE, 0},
    {"trace_opt_str_uminus", XXBYTE_YARV_ID_OPT_STR_UMINUS, 0},
    {"trace_opt_newarray_max", XXBYTE_YARV_ID_OPT_NEWARRAY_MAX, 0},
    {"trace_opt_newarray_min", XXBYTE_YARV_ID_OPT_NEWARRAY_MIN, 0},
    {"trace_invokesuper", XXBYTE_YARV_ID_INVOKESUPER, G_CALL},
    {"trace_invokeblock", XXBYTE_YARV_ID_INVOKEBLOCK, G_CALL},
    {"trace_leave", XXBYTE_YARV_ID_LEAVE, G_RET | G_STOP},
    {"trace_throw", XXBYTE_YARV_ID_THROW, G_THROW | G_STOP},
    {"trace_jump", XXBYTE_YARV_ID_JUMP, G_JUMP | G_STOP},
    {"trace_branchif", XXBYTE_YARV_ID_BRANCHIF, G_COND},
    {"trace_branchunless", XXBYTE_YARV_ID_BRANCHUNLESS, G_COND},
    {"trace_branchnil", XXBYTE_YARV_ID_BRANCHNIL, G_COND},
    {"trace_opt_getinlinecache", XXBYTE_YARV_ID_OPT_GETINLINECACHE, G_COND},
    {"trace_opt_setinlinecache", XXBYTE_YARV_ID_OPT_SETINLINECACHE, 0},
    {"trace_once", XXBYTE_YARV_ID_ONCE, G_CALL},
    {"trace_opt_case_dispatch", XXBYTE_YARV_ID_OPT_CASE_DISPATCH, G_SWITCH | G_COND},
    {"trace_opt_plus", XXBYTE_YARV_ID_OPT_PLUS, 0},
    {"trace_opt_minus", XXBYTE_YARV_ID_OPT_MINUS, 0},
    {"trace_opt_mult", XXBYTE_YARV_ID_OPT_MULT, 0},
    {"trace_opt_div", XXBYTE_YARV_ID_OPT_DIV, 0},
    {"trace_opt_mod", XXBYTE_YARV_ID_OPT_MOD, 0},
    {"trace_opt_eq", XXBYTE_YARV_ID_OPT_EQ, 0},
    {"trace_opt_neq", XXBYTE_YARV_ID_OPT_NEQ, 0},
    {"trace_opt_lt", XXBYTE_YARV_ID_OPT_LT, 0},
    {"trace_opt_le", XXBYTE_YARV_ID_OPT_LE, 0},
    {"trace_opt_gt", XXBYTE_YARV_ID_OPT_GT, 0},
    {"trace_opt_ge", XXBYTE_YARV_ID_OPT_GE, 0},
    {"trace_opt_ltlt", XXBYTE_YARV_ID_OPT_LTLT, 0},
    {"trace_opt_and", XXBYTE_YARV_ID_OPT_AND, 0},
    {"trace_opt_or", XXBYTE_YARV_ID_OPT_OR, 0},
    {"trace_opt_aref", XXBYTE_YARV_ID_OPT_AREF, 0},
    {"trace_opt_aset", XXBYTE_YARV_ID_OPT_ASET, 0},
    {"trace_opt_aset_with", XXBYTE_YARV_ID_OPT_ASET_WITH, 0},
    {"trace_opt_aref_with", XXBYTE_YARV_ID_OPT_AREF_WITH, 0},
    {"trace_opt_length", XXBYTE_YARV_ID_OPT_LENGTH, 0},
    {"trace_opt_size", XXBYTE_YARV_ID_OPT_SIZE, 0},
    {"trace_opt_empty_p", XXBYTE_YARV_ID_OPT_EMPTY_P, 0},
    {"trace_opt_succ", XXBYTE_YARV_ID_OPT_SUCC, 0},
    {"trace_opt_not", XXBYTE_YARV_ID_OPT_NOT, 0},
    {"trace_opt_regexpmatch1", XXBYTE_YARV_ID_OPT_REGEXPMATCH1, 0},
    {"trace_opt_regexpmatch2", XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 0},
    {"trace_opt_call_c_function", XXBYTE_YARV_ID_OPT_CALL_C_FUNCTION, G_CALL},
    {"trace_bitblt", XXBYTE_YARV_ID_BITBLT, 0},
    {"trace_answer", XXBYTE_YARV_ID_ANSWER, 0},
    {"trace_getlocal_WC_0", XXBYTE_YARV_ID_GETLOCAL_WC_0, 0},
    {"trace_getlocal_WC_1", XXBYTE_YARV_ID_GETLOCAL_WC_1, 0},
    {"trace_setlocal_WC_0", XXBYTE_YARV_ID_SETLOCAL_WC_0, 0},
    {"trace_setlocal_WC_1", XXBYTE_YARV_ID_SETLOCAL_WC_1, 0},
    {"trace_putobject_INT2FIX_0_", XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_, 0},
    {"trace_putobject_INT2FIX_1_", XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_, 0},
    {"newarraykwsplat", XXBYTE_YARV_ID_NEWARRAYKWSPLAT, 0},
    {"definemethod", XXBYTE_YARV_ID_DEFINEMETHOD, 0},
    {"definesmethod", XXBYTE_YARV_ID_DEFINESMETHOD, 0},
    {"opt_nil_p", XXBYTE_YARV_ID_OPT_NIL_P, 0},
    {"invokebuiltin", XXBYTE_YARV_ID_INVOKEBUILTIN, G_CALL},
    {"opt_invokebuiltin_delegate", XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE, G_CALL},
    {"opt_invokebuiltin_delegate_leave", XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, G_CALL | G_RET | G_STOP},
    {"trace_newarraykwsplat", XXBYTE_YARV_ID_NEWARRAYKWSPLAT, 0},
    {"trace_definemethod", XXBYTE_YARV_ID_DEFINEMETHOD, 0},
    {"trace_definesmethod", XXBYTE_YARV_ID_DEFINESMETHOD, 0},
    {"trace_opt_nil_p", XXBYTE_YARV_ID_OPT_NIL_P, 0},
    {"trace_invokebuiltin", XXBYTE_YARV_ID_INVOKEBUILTIN, G_CALL},
    {"trace_opt_invokebuiltin_delegate", XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE, G_CALL},
    {"trace_opt_invokebuiltin_delegate_leave", XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, G_CALL | G_RET | G_STOP},
    {"anytostring", XXBYTE_YARV_ID_ANYTOSTRING, 0},
    {"objtostring", XXBYTE_YARV_ID_OBJTOSTRING, 0},
    {"trace_anytostring", XXBYTE_YARV_ID_ANYTOSTRING, 0},
    {"trace_objtostring", XXBYTE_YARV_ID_OBJTOSTRING, 0},
    {"opt_getconstant_path", XXBYTE_YARV_ID_OPT_GETCONSTANT_PATH, 0},
    {"opt_reverse", XXBYTE_YARV_ID_OPT_REVERSE, 0},
    {"trace_opt_getconstant_path", XXBYTE_YARV_ID_OPT_GETCONSTANT_PATH, 0},
    {"trace_opt_reverse", XXBYTE_YARV_ID_OPT_REVERSE, 0},
    {"splatkw", XXBYTE_YARV_ID_SPLATKW, 0},
    {"definedivar", XXBYTE_YARV_ID_DEFINEDIVAR, 0},
    {"opt_newarray_send", XXBYTE_YARV_ID_OPT_NEWARRAY_SEND, 0},
    {"trace_splatkw", XXBYTE_YARV_ID_SPLATKW, 0},
    {"trace_definedivar", XXBYTE_YARV_ID_DEFINEDIVAR, 0},
    {"trace_opt_newarray_send", XXBYTE_YARV_ID_OPT_NEWARRAY_SEND, 0},
    {"putchilledstring", XXBYTE_YARV_ID_PUTCHILLEDSTRING, 0},
    {"pushtoarraykwsplat", XXBYTE_YARV_ID_PUSHTOARRAYKWSPLAT, 0},
    {"concattoarray", XXBYTE_YARV_ID_CONCATTOARRAY, 0},
    {"pushtoarray", XXBYTE_YARV_ID_PUSHTOARRAY, 0},
    {"sendforward", XXBYTE_YARV_ID_SENDFORWARD, G_CALL},
    {"opt_ary_freeze", XXBYTE_YARV_ID_OPT_ARY_FREEZE, 0},
    {"opt_hash_freeze", XXBYTE_YARV_ID_OPT_HASH_FREEZE, 0},
    {"opt_duparray_send", XXBYTE_YARV_ID_OPT_DUPARRAY_SEND, 0},
    {"invokesuperforward", XXBYTE_YARV_ID_INVOKESUPERFORWARD, G_CALL},
    {"trace_putchilledstring", XXBYTE_YARV_ID_PUTCHILLEDSTRING, 0},
    {"trace_pushtoarraykwsplat", XXBYTE_YARV_ID_PUSHTOARRAYKWSPLAT, 0},
    {"trace_concattoarray", XXBYTE_YARV_ID_CONCATTOARRAY, 0},
    {"trace_pushtoarray", XXBYTE_YARV_ID_PUSHTOARRAY, 0},
    {"trace_sendforward", XXBYTE_YARV_ID_SENDFORWARD, G_CALL},
    {"trace_opt_ary_freeze", XXBYTE_YARV_ID_OPT_ARY_FREEZE, 0},
    {"trace_opt_hash_freeze", XXBYTE_YARV_ID_OPT_HASH_FREEZE, 0},
    {"trace_opt_duparray_send", XXBYTE_YARV_ID_OPT_DUPARRAY_SEND, 0},
    {"trace_invokesuperforward", XXBYTE_YARV_ID_INVOKESUPERFORWARD, G_CALL},
    {"opt_new", XXBYTE_YARV_ID_OPT_NEW, G_COND},
    {"trace_opt_new", XXBYTE_YARV_ID_OPT_NEW, G_COND},
    {"zjit_getinstancevariable", XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 0},
    {"zjit_setinstancevariable", XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 0},
    {"zjit_definedivar", XXBYTE_YARV_ID_DEFINEDIVAR, 0},
    {"zjit_send", XXBYTE_YARV_ID_SEND, G_CALL},
    {"zjit_opt_send_without_block", XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, G_CALL},
    {"zjit_objtostring", XXBYTE_YARV_ID_OBJTOSTRING, 0},
    {"zjit_opt_nil_p", XXBYTE_YARV_ID_OPT_NIL_P, 0},
    {"zjit_invokeblock", XXBYTE_YARV_ID_INVOKEBLOCK, G_CALL},
    {"zjit_opt_plus", XXBYTE_YARV_ID_OPT_PLUS, 0},
    {"zjit_opt_minus", XXBYTE_YARV_ID_OPT_MINUS, 0},
    {"zjit_opt_mult", XXBYTE_YARV_ID_OPT_MULT, 0},
    {"zjit_opt_div", XXBYTE_YARV_ID_OPT_DIV, 0},
    {"zjit_opt_mod", XXBYTE_YARV_ID_OPT_MOD, 0},
    {"zjit_opt_eq", XXBYTE_YARV_ID_OPT_EQ, 0},
    {"zjit_opt_neq", XXBYTE_YARV_ID_OPT_NEQ, 0},
    {"zjit_opt_lt", XXBYTE_YARV_ID_OPT_LT, 0},
    {"zjit_opt_le", XXBYTE_YARV_ID_OPT_LE, 0},
    {"zjit_opt_gt", XXBYTE_YARV_ID_OPT_GT, 0},
    {"zjit_opt_ge", XXBYTE_YARV_ID_OPT_GE, 0},
    {"zjit_opt_ltlt", XXBYTE_YARV_ID_OPT_LTLT, 0},
    {"zjit_opt_and", XXBYTE_YARV_ID_OPT_AND, 0},
    {"zjit_opt_or", XXBYTE_YARV_ID_OPT_OR, 0},
    {"zjit_opt_aref", XXBYTE_YARV_ID_OPT_AREF, 0},
    {"zjit_opt_aset", XXBYTE_YARV_ID_OPT_ASET, 0},
    {"zjit_opt_length", XXBYTE_YARV_ID_OPT_LENGTH, 0},
    {"zjit_opt_size", XXBYTE_YARV_ID_OPT_SIZE, 0},
    {"zjit_opt_empty_p", XXBYTE_YARV_ID_OPT_EMPTY_P, 0},
    {"zjit_opt_succ", XXBYTE_YARV_ID_OPT_SUCC, 0},
    {"zjit_opt_not", XXBYTE_YARV_ID_OPT_NOT, 0},
    {"zjit_opt_regexpmatch2", XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 0},
};

const char *const xxbyte_yarv_signatures[YARV_SIGNATURE_COUNT] = {
    "",   "C",  "F",  "G",  "I",  "K",  "L",  "N",  "O",  "R",  "S",  "V",   "CC",  "CE",  "CO",  "CS",  "HO",  "IA",
    "IJ", "IK", "IS", "LL", "LN", "NI", "NN", "OK", "RN", "ST", "VC", "CES", "IAV", "ISN", "NVV", "VCE", "VIN", "CECE",
};

/* Ruby 2.6.10 (202 instructions) */
static const yarv_op yarv_ops_2_6[202] = {
    {XXBYTE_YARV_ID_NOP, 0},                           /*   0 - */
    {XXBYTE_YARV_ID_GETLOCAL, 22},                     /*   1 LN */
    {XXBYTE_YARV_ID_SETLOCAL, 22},                     /*   2 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAM, 22},                /*   3 LN */
    {XXBYTE_YARV_ID_SETBLOCKPARAM, 22},                /*   4 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAMPROXY, 22},           /*   5 LN */
    {XXBYTE_YARV_ID_GETSPECIAL, 24},                   /*   6 NN */
    {XXBYTE_YARV_ID_SETSPECIAL, 7},                    /*   7 N */
    {XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 19},          /*   8 IK */
    {XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 19},          /*   9 IK */
    {XXBYTE_YARV_ID_GETCLASSVARIABLE, 4},              /*  10 I */
    {XXBYTE_YARV_ID_SETCLASSVARIABLE, 4},              /*  11 I */
    {XXBYTE_YARV_ID_GETCONSTANT, 4},                   /*  12 I */
    {XXBYTE_YARV_ID_SETCONSTANT, 4},                   /*  13 I */
    {XXBYTE_YARV_ID_GETGLOBAL, 3},                     /*  14 G */
    {XXBYTE_YARV_ID_SETGLOBAL, 3},                     /*  15 G */
    {XXBYTE_YARV_ID_PUTNIL, 0},                        /*  16 - */
    {XXBYTE_YARV_ID_PUTSELF, 0},                       /*  17 - */
    {XXBYTE_YARV_ID_PUTOBJECT, 11},                    /*  18 V */
    {XXBYTE_YARV_ID_PUTSPECIALOBJECT, 7},              /*  19 N */
    {XXBYTE_YARV_ID_PUTISEQ, 10},                      /*  20 S */
    {XXBYTE_YARV_ID_PUTSTRING, 11},                    /*  21 V */
    {XXBYTE_YARV_ID_CONCATSTRINGS, 7},                 /*  22 N */
    {XXBYTE_YARV_ID_TOSTRING, 0},                      /*  23 - */
    {XXBYTE_YARV_ID_FREEZESTRING, 11},                 /*  24 V */
    {XXBYTE_YARV_ID_TOREGEXP, 24},                     /*  25 NN */
    {XXBYTE_YARV_ID_INTERN, 0},                        /*  26 - */
    {XXBYTE_YARV_ID_NEWARRAY, 7},                      /*  27 N */
    {XXBYTE_YARV_ID_DUPARRAY, 11},                     /*  28 V */
    {XXBYTE_YARV_ID_DUPHASH, 11},                      /*  29 V */
    {XXBYTE_YARV_ID_EXPANDARRAY, 24},                  /*  30 NN */
    {XXBYTE_YARV_ID_CONCATARRAY, 0},                   /*  31 - */
    {XXBYTE_YARV_ID_SPLATARRAY, 11},                   /*  32 V */
    {XXBYTE_YARV_ID_NEWHASH, 7},                       /*  33 N */
    {XXBYTE_YARV_ID_NEWRANGE, 7},                      /*  34 N */
    {XXBYTE_YARV_ID_POP, 0},                           /*  35 - */
    {XXBYTE_YARV_ID_DUP, 0},                           /*  36 - */
    {XXBYTE_YARV_ID_DUPN, 7},                          /*  37 N */
    {XXBYTE_YARV_ID_SWAP, 0},                          /*  38 - */
    {XXBYTE_YARV_ID_REVERSE, 7},                       /*  39 N */
    {XXBYTE_YARV_ID_REPUT, 0},                         /*  40 - */
    {XXBYTE_YARV_ID_TOPN, 7},                          /*  41 N */
    {XXBYTE_YARV_ID_SETN, 7},                          /*  42 N */
    {XXBYTE_YARV_ID_ADJUSTSTACK, 7},                   /*  43 N */
    {XXBYTE_YARV_ID_DEFINED, 32},                      /*  44 NVV */
    {XXBYTE_YARV_ID_CHECKMATCH, 7},                    /*  45 N */
    {XXBYTE_YARV_ID_CHECKKEYWORD, 21},                 /*  46 LL */
    {XXBYTE_YARV_ID_CHECKTYPE, 7},                     /*  47 N */
    {XXBYTE_YARV_ID_DEFINECLASS, 31},                  /*  48 ISN */
    {XXBYTE_YARV_ID_SEND, 29},                         /*  49 CES */
    {XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, 13},       /*  50 CE */
    {XXBYTE_YARV_ID_OPT_STR_FREEZE, 33},               /*  51 VCE */
    {XXBYTE_YARV_ID_OPT_STR_UMINUS, 33},               /*  52 VCE */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_MAX, 7},              /*  53 N */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_MIN, 7},              /*  54 N */
    {XXBYTE_YARV_ID_INVOKESUPER, 29},                  /*  55 CES */
    {XXBYTE_YARV_ID_INVOKEBLOCK, 1},                   /*  56 C */
    {XXBYTE_YARV_ID_LEAVE, 0},                         /*  57 - */
    {XXBYTE_YARV_ID_THROW, 7},                         /*  58 N */
    {XXBYTE_YARV_ID_JUMP, 8},                          /*  59 O */
    {XXBYTE_YARV_ID_BRANCHIF, 8},                      /*  60 O */
    {XXBYTE_YARV_ID_BRANCHUNLESS, 8},                  /*  61 O */
    {XXBYTE_YARV_ID_BRANCHNIL, 8},                     /*  62 O */
    {XXBYTE_YARV_ID_OPT_GETINLINECACHE, 25},           /*  63 OK */
    {XXBYTE_YARV_ID_OPT_SETINLINECACHE, 5},            /*  64 K */
    {XXBYTE_YARV_ID_ONCE, 27},                         /*  65 ST */
    {XXBYTE_YARV_ID_OPT_CASE_DISPATCH, 16},            /*  66 HO */
    {XXBYTE_YARV_ID_OPT_PLUS, 13},                     /*  67 CE */
    {XXBYTE_YARV_ID_OPT_MINUS, 13},                    /*  68 CE */
    {XXBYTE_YARV_ID_OPT_MULT, 13},                     /*  69 CE */
    {XXBYTE_YARV_ID_OPT_DIV, 13},                      /*  70 CE */
    {XXBYTE_YARV_ID_OPT_MOD, 13},                      /*  71 CE */
    {XXBYTE_YARV_ID_OPT_EQ, 13},                       /*  72 CE */
    {XXBYTE_YARV_ID_OPT_NEQ, 35},                      /*  73 CECE */
    {XXBYTE_YARV_ID_OPT_LT, 13},                       /*  74 CE */
    {XXBYTE_YARV_ID_OPT_LE, 13},                       /*  75 CE */
    {XXBYTE_YARV_ID_OPT_GT, 13},                       /*  76 CE */
    {XXBYTE_YARV_ID_OPT_GE, 13},                       /*  77 CE */
    {XXBYTE_YARV_ID_OPT_LTLT, 13},                     /*  78 CE */
    {XXBYTE_YARV_ID_OPT_AND, 13},                      /*  79 CE */
    {XXBYTE_YARV_ID_OPT_OR, 13},                       /*  80 CE */
    {XXBYTE_YARV_ID_OPT_AREF, 13},                     /*  81 CE */
    {XXBYTE_YARV_ID_OPT_ASET, 13},                     /*  82 CE */
    {XXBYTE_YARV_ID_OPT_ASET_WITH, 33},                /*  83 VCE */
    {XXBYTE_YARV_ID_OPT_AREF_WITH, 33},                /*  84 VCE */
    {XXBYTE_YARV_ID_OPT_LENGTH, 13},                   /*  85 CE */
    {XXBYTE_YARV_ID_OPT_SIZE, 13},                     /*  86 CE */
    {XXBYTE_YARV_ID_OPT_EMPTY_P, 13},                  /*  87 CE */
    {XXBYTE_YARV_ID_OPT_SUCC, 13},                     /*  88 CE */
    {XXBYTE_YARV_ID_OPT_NOT, 13},                      /*  89 CE */
    {XXBYTE_YARV_ID_OPT_REGEXPMATCH1, 11},             /*  90 V */
    {XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 13},             /*  91 CE */
    {XXBYTE_YARV_ID_OPT_CALL_C_FUNCTION, 2},           /*  92 F */
    {XXBYTE_YARV_ID_BITBLT, 0},                        /*  93 - */
    {XXBYTE_YARV_ID_ANSWER, 0},                        /*  94 - */
    {XXBYTE_YARV_ID_GETLOCAL_WC_0, 6},                 /*  95 L */
    {XXBYTE_YARV_ID_GETLOCAL_WC_1, 6},                 /*  96 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_0, 6},                 /*  97 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_1, 6},                 /*  98 L */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_, 0},          /*  99 - */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_, 0},          /* 100 - */
    {XXBYTE_YARV_ID_TRACE_NOP, 0},                     /* 101 - */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL, 22},               /* 102 LN */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL, 22},               /* 103 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAM, 22},          /* 104 LN */
    {XXBYTE_YARV_ID_TRACE_SETBLOCKPARAM, 22},          /* 105 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAMPROXY, 22},     /* 106 LN */
    {XXBYTE_YARV_ID_TRACE_GETSPECIAL, 24},             /* 107 NN */
    {XXBYTE_YARV_ID_TRACE_SETSPECIAL, 7},              /* 108 N */
    {XXBYTE_YARV_ID_TRACE_GETINSTANCEVARIABLE, 19},    /* 109 IK */
    {XXBYTE_YARV_ID_TRACE_SETINSTANCEVARIABLE, 19},    /* 110 IK */
    {XXBYTE_YARV_ID_TRACE_GETCLASSVARIABLE, 4},        /* 111 I */
    {XXBYTE_YARV_ID_TRACE_SETCLASSVARIABLE, 4},        /* 112 I */
    {XXBYTE_YARV_ID_TRACE_GETCONSTANT, 4},             /* 113 I */
    {XXBYTE_YARV_ID_TRACE_SETCONSTANT, 4},             /* 114 I */
    {XXBYTE_YARV_ID_TRACE_GETGLOBAL, 3},               /* 115 G */
    {XXBYTE_YARV_ID_TRACE_SETGLOBAL, 3},               /* 116 G */
    {XXBYTE_YARV_ID_TRACE_PUTNIL, 0},                  /* 117 - */
    {XXBYTE_YARV_ID_TRACE_PUTSELF, 0},                 /* 118 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT, 11},              /* 119 V */
    {XXBYTE_YARV_ID_TRACE_PUTSPECIALOBJECT, 7},        /* 120 N */
    {XXBYTE_YARV_ID_TRACE_PUTISEQ, 10},                /* 121 S */
    {XXBYTE_YARV_ID_TRACE_PUTSTRING, 11},              /* 122 V */
    {XXBYTE_YARV_ID_TRACE_CONCATSTRINGS, 7},           /* 123 N */
    {XXBYTE_YARV_ID_TRACE_TOSTRING, 0},                /* 124 - */
    {XXBYTE_YARV_ID_TRACE_FREEZESTRING, 11},           /* 125 V */
    {XXBYTE_YARV_ID_TRACE_TOREGEXP, 24},               /* 126 NN */
    {XXBYTE_YARV_ID_TRACE_INTERN, 0},                  /* 127 - */
    {XXBYTE_YARV_ID_TRACE_NEWARRAY, 7},                /* 128 N */
    {XXBYTE_YARV_ID_TRACE_DUPARRAY, 11},               /* 129 V */
    {XXBYTE_YARV_ID_TRACE_DUPHASH, 11},                /* 130 V */
    {XXBYTE_YARV_ID_TRACE_EXPANDARRAY, 24},            /* 131 NN */
    {XXBYTE_YARV_ID_TRACE_CONCATARRAY, 0},             /* 132 - */
    {XXBYTE_YARV_ID_TRACE_SPLATARRAY, 11},             /* 133 V */
    {XXBYTE_YARV_ID_TRACE_NEWHASH, 7},                 /* 134 N */
    {XXBYTE_YARV_ID_TRACE_NEWRANGE, 7},                /* 135 N */
    {XXBYTE_YARV_ID_TRACE_POP, 0},                     /* 136 - */
    {XXBYTE_YARV_ID_TRACE_DUP, 0},                     /* 137 - */
    {XXBYTE_YARV_ID_TRACE_DUPN, 7},                    /* 138 N */
    {XXBYTE_YARV_ID_TRACE_SWAP, 0},                    /* 139 - */
    {XXBYTE_YARV_ID_TRACE_REVERSE, 7},                 /* 140 N */
    {XXBYTE_YARV_ID_TRACE_REPUT, 0},                   /* 141 - */
    {XXBYTE_YARV_ID_TRACE_TOPN, 7},                    /* 142 N */
    {XXBYTE_YARV_ID_TRACE_SETN, 7},                    /* 143 N */
    {XXBYTE_YARV_ID_TRACE_ADJUSTSTACK, 7},             /* 144 N */
    {XXBYTE_YARV_ID_TRACE_DEFINED, 32},                /* 145 NVV */
    {XXBYTE_YARV_ID_TRACE_CHECKMATCH, 7},              /* 146 N */
    {XXBYTE_YARV_ID_TRACE_CHECKKEYWORD, 21},           /* 147 LL */
    {XXBYTE_YARV_ID_TRACE_CHECKTYPE, 7},               /* 148 N */
    {XXBYTE_YARV_ID_TRACE_DEFINECLASS, 31},            /* 149 ISN */
    {XXBYTE_YARV_ID_TRACE_SEND, 29},                   /* 150 CES */
    {XXBYTE_YARV_ID_TRACE_OPT_SEND_WITHOUT_BLOCK, 13}, /* 151 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_FREEZE, 33},         /* 152 VCE */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_UMINUS, 33},         /* 153 VCE */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MAX, 7},        /* 154 N */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MIN, 7},        /* 155 N */
    {XXBYTE_YARV_ID_TRACE_INVOKESUPER, 29},            /* 156 CES */
    {XXBYTE_YARV_ID_TRACE_INVOKEBLOCK, 1},             /* 157 C */
    {XXBYTE_YARV_ID_TRACE_LEAVE, 0},                   /* 158 - */
    {XXBYTE_YARV_ID_TRACE_THROW, 7},                   /* 159 N */
    {XXBYTE_YARV_ID_TRACE_JUMP, 8},                    /* 160 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHIF, 8},                /* 161 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHUNLESS, 8},            /* 162 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHNIL, 8},               /* 163 O */
    {XXBYTE_YARV_ID_TRACE_OPT_GETINLINECACHE, 25},     /* 164 OK */
    {XXBYTE_YARV_ID_TRACE_OPT_SETINLINECACHE, 5},      /* 165 K */
    {XXBYTE_YARV_ID_TRACE_ONCE, 27},                   /* 166 ST */
    {XXBYTE_YARV_ID_TRACE_OPT_CASE_DISPATCH, 16},      /* 167 HO */
    {XXBYTE_YARV_ID_TRACE_OPT_PLUS, 13},               /* 168 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_MINUS, 13},              /* 169 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_MULT, 13},               /* 170 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_DIV, 13},                /* 171 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_MOD, 13},                /* 172 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_EQ, 13},                 /* 173 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_NEQ, 35},                /* 174 CECE */
    {XXBYTE_YARV_ID_TRACE_OPT_LT, 13},                 /* 175 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_LE, 13},                 /* 176 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_GT, 13},                 /* 177 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_GE, 13},                 /* 178 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_LTLT, 13},               /* 179 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_AND, 13},                /* 180 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_OR, 13},                 /* 181 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF, 13},               /* 182 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET, 13},               /* 183 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET_WITH, 33},          /* 184 VCE */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF_WITH, 33},          /* 185 VCE */
    {XXBYTE_YARV_ID_TRACE_OPT_LENGTH, 13},             /* 186 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_SIZE, 13},               /* 187 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_EMPTY_P, 13},            /* 188 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_SUCC, 13},               /* 189 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_NOT, 13},                /* 190 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH1, 11},       /* 191 V */
    {XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH2, 13},       /* 192 CE */
    {XXBYTE_YARV_ID_TRACE_OPT_CALL_C_FUNCTION, 2},     /* 193 F */
    {XXBYTE_YARV_ID_TRACE_BITBLT, 0},                  /* 194 - */
    {XXBYTE_YARV_ID_TRACE_ANSWER, 0},                  /* 195 - */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_0, 6},           /* 196 L */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_1, 6},           /* 197 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_0, 6},           /* 198 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_1, 6},           /* 199 L */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_0_, 0},    /* 200 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_1_, 0},    /* 201 - */
};

/* Ruby 2.7.8 (206 instructions) */
static const yarv_op yarv_ops_2_7[206] = {
    {XXBYTE_YARV_ID_NOP, 0},                                     /*   0 - */
    {XXBYTE_YARV_ID_GETLOCAL, 22},                               /*   1 LN */
    {XXBYTE_YARV_ID_SETLOCAL, 22},                               /*   2 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAM, 22},                          /*   3 LN */
    {XXBYTE_YARV_ID_SETBLOCKPARAM, 22},                          /*   4 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAMPROXY, 22},                     /*   5 LN */
    {XXBYTE_YARV_ID_GETSPECIAL, 24},                             /*   6 NN */
    {XXBYTE_YARV_ID_SETSPECIAL, 7},                              /*   7 N */
    {XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 17},                    /*   8 IA */
    {XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 17},                    /*   9 IA */
    {XXBYTE_YARV_ID_GETCLASSVARIABLE, 4},                        /*  10 I */
    {XXBYTE_YARV_ID_SETCLASSVARIABLE, 4},                        /*  11 I */
    {XXBYTE_YARV_ID_GETCONSTANT, 4},                             /*  12 I */
    {XXBYTE_YARV_ID_SETCONSTANT, 4},                             /*  13 I */
    {XXBYTE_YARV_ID_GETGLOBAL, 3},                               /*  14 G */
    {XXBYTE_YARV_ID_SETGLOBAL, 3},                               /*  15 G */
    {XXBYTE_YARV_ID_PUTNIL, 0},                                  /*  16 - */
    {XXBYTE_YARV_ID_PUTSELF, 0},                                 /*  17 - */
    {XXBYTE_YARV_ID_PUTOBJECT, 11},                              /*  18 V */
    {XXBYTE_YARV_ID_PUTSPECIALOBJECT, 7},                        /*  19 N */
    {XXBYTE_YARV_ID_PUTSTRING, 11},                              /*  20 V */
    {XXBYTE_YARV_ID_CONCATSTRINGS, 7},                           /*  21 N */
    {XXBYTE_YARV_ID_TOSTRING, 0},                                /*  22 - */
    {XXBYTE_YARV_ID_FREEZESTRING, 11},                           /*  23 V */
    {XXBYTE_YARV_ID_TOREGEXP, 24},                               /*  24 NN */
    {XXBYTE_YARV_ID_INTERN, 0},                                  /*  25 - */
    {XXBYTE_YARV_ID_NEWARRAY, 7},                                /*  26 N */
    {XXBYTE_YARV_ID_NEWARRAYKWSPLAT, 7},                         /*  27 N */
    {XXBYTE_YARV_ID_DUPARRAY, 11},                               /*  28 V */
    {XXBYTE_YARV_ID_DUPHASH, 11},                                /*  29 V */
    {XXBYTE_YARV_ID_EXPANDARRAY, 24},                            /*  30 NN */
    {XXBYTE_YARV_ID_CONCATARRAY, 0},                             /*  31 - */
    {XXBYTE_YARV_ID_SPLATARRAY, 11},                             /*  32 V */
    {XXBYTE_YARV_ID_NEWHASH, 7},                                 /*  33 N */
    {XXBYTE_YARV_ID_NEWRANGE, 7},                                /*  34 N */
    {XXBYTE_YARV_ID_POP, 0},                                     /*  35 - */
    {XXBYTE_YARV_ID_DUP, 0},                                     /*  36 - */
    {XXBYTE_YARV_ID_DUPN, 7},                                    /*  37 N */
    {XXBYTE_YARV_ID_SWAP, 0},                                    /*  38 - */
    {XXBYTE_YARV_ID_REVERSE, 7},                                 /*  39 N */
    {XXBYTE_YARV_ID_TOPN, 7},                                    /*  40 N */
    {XXBYTE_YARV_ID_SETN, 7},                                    /*  41 N */
    {XXBYTE_YARV_ID_ADJUSTSTACK, 7},                             /*  42 N */
    {XXBYTE_YARV_ID_DEFINED, 32},                                /*  43 NVV */
    {XXBYTE_YARV_ID_CHECKMATCH, 7},                              /*  44 N */
    {XXBYTE_YARV_ID_CHECKKEYWORD, 21},                           /*  45 LL */
    {XXBYTE_YARV_ID_CHECKTYPE, 7},                               /*  46 N */
    {XXBYTE_YARV_ID_DEFINECLASS, 31},                            /*  47 ISN */
    {XXBYTE_YARV_ID_DEFINEMETHOD, 20},                           /*  48 IS */
    {XXBYTE_YARV_ID_DEFINESMETHOD, 20},                          /*  49 IS */
    {XXBYTE_YARV_ID_SEND, 15},                                   /*  50 CS */
    {XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, 1},                  /*  51 C */
    {XXBYTE_YARV_ID_OPT_STR_FREEZE, 28},                         /*  52 VC */
    {XXBYTE_YARV_ID_OPT_NIL_P, 1},                               /*  53 C */
    {XXBYTE_YARV_ID_OPT_STR_UMINUS, 28},                         /*  54 VC */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_MAX, 7},                        /*  55 N */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_MIN, 7},                        /*  56 N */
    {XXBYTE_YARV_ID_INVOKESUPER, 15},                            /*  57 CS */
    {XXBYTE_YARV_ID_INVOKEBLOCK, 1},                             /*  58 C */
    {XXBYTE_YARV_ID_LEAVE, 0},                                   /*  59 - */
    {XXBYTE_YARV_ID_THROW, 7},                                   /*  60 N */
    {XXBYTE_YARV_ID_JUMP, 8},                                    /*  61 O */
    {XXBYTE_YARV_ID_BRANCHIF, 8},                                /*  62 O */
    {XXBYTE_YARV_ID_BRANCHUNLESS, 8},                            /*  63 O */
    {XXBYTE_YARV_ID_BRANCHNIL, 8},                               /*  64 O */
    {XXBYTE_YARV_ID_OPT_GETINLINECACHE, 25},                     /*  65 OK */
    {XXBYTE_YARV_ID_OPT_SETINLINECACHE, 5},                      /*  66 K */
    {XXBYTE_YARV_ID_ONCE, 27},                                   /*  67 ST */
    {XXBYTE_YARV_ID_OPT_CASE_DISPATCH, 16},                      /*  68 HO */
    {XXBYTE_YARV_ID_OPT_PLUS, 1},                                /*  69 C */
    {XXBYTE_YARV_ID_OPT_MINUS, 1},                               /*  70 C */
    {XXBYTE_YARV_ID_OPT_MULT, 1},                                /*  71 C */
    {XXBYTE_YARV_ID_OPT_DIV, 1},                                 /*  72 C */
    {XXBYTE_YARV_ID_OPT_MOD, 1},                                 /*  73 C */
    {XXBYTE_YARV_ID_OPT_EQ, 1},                                  /*  74 C */
    {XXBYTE_YARV_ID_OPT_NEQ, 12},                                /*  75 CC */
    {XXBYTE_YARV_ID_OPT_LT, 1},                                  /*  76 C */
    {XXBYTE_YARV_ID_OPT_LE, 1},                                  /*  77 C */
    {XXBYTE_YARV_ID_OPT_GT, 1},                                  /*  78 C */
    {XXBYTE_YARV_ID_OPT_GE, 1},                                  /*  79 C */
    {XXBYTE_YARV_ID_OPT_LTLT, 1},                                /*  80 C */
    {XXBYTE_YARV_ID_OPT_AND, 1},                                 /*  81 C */
    {XXBYTE_YARV_ID_OPT_OR, 1},                                  /*  82 C */
    {XXBYTE_YARV_ID_OPT_AREF, 1},                                /*  83 C */
    {XXBYTE_YARV_ID_OPT_ASET, 1},                                /*  84 C */
    {XXBYTE_YARV_ID_OPT_ASET_WITH, 28},                          /*  85 VC */
    {XXBYTE_YARV_ID_OPT_AREF_WITH, 28},                          /*  86 VC */
    {XXBYTE_YARV_ID_OPT_LENGTH, 1},                              /*  87 C */
    {XXBYTE_YARV_ID_OPT_SIZE, 1},                                /*  88 C */
    {XXBYTE_YARV_ID_OPT_EMPTY_P, 1},                             /*  89 C */
    {XXBYTE_YARV_ID_OPT_SUCC, 1},                                /*  90 C */
    {XXBYTE_YARV_ID_OPT_NOT, 1},                                 /*  91 C */
    {XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 1},                        /*  92 C */
    {XXBYTE_YARV_ID_OPT_CALL_C_FUNCTION, 2},                     /*  93 F */
    {XXBYTE_YARV_ID_INVOKEBUILTIN, 9},                           /*  94 R */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE, 26},             /*  95 RN */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26},       /*  96 RN */
    {XXBYTE_YARV_ID_GETLOCAL_WC_0, 6},                           /*  97 L */
    {XXBYTE_YARV_ID_GETLOCAL_WC_1, 6},                           /*  98 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_0, 6},                           /*  99 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_1, 6},                           /* 100 L */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_, 0},                    /* 101 - */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_, 0},                    /* 102 - */
    {XXBYTE_YARV_ID_TRACE_NOP, 0},                               /* 103 - */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL, 22},                         /* 104 LN */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL, 22},                         /* 105 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAM, 22},                    /* 106 LN */
    {XXBYTE_YARV_ID_TRACE_SETBLOCKPARAM, 22},                    /* 107 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAMPROXY, 22},               /* 108 LN */
    {XXBYTE_YARV_ID_TRACE_GETSPECIAL, 24},                       /* 109 NN */
    {XXBYTE_YARV_ID_TRACE_SETSPECIAL, 7},                        /* 110 N */
    {XXBYTE_YARV_ID_TRACE_GETINSTANCEVARIABLE, 17},              /* 111 IA */
    {XXBYTE_YARV_ID_TRACE_SETINSTANCEVARIABLE, 17},              /* 112 IA */
    {XXBYTE_YARV_ID_TRACE_GETCLASSVARIABLE, 4},                  /* 113 I */
    {XXBYTE_YARV_ID_TRACE_SETCLASSVARIABLE, 4},                  /* 114 I */
    {XXBYTE_YARV_ID_TRACE_GETCONSTANT, 4},                       /* 115 I */
    {XXBYTE_YARV_ID_TRACE_SETCONSTANT, 4},                       /* 116 I */
    {XXBYTE_YARV_ID_TRACE_GETGLOBAL, 3},                         /* 117 G */
    {XXBYTE_YARV_ID_TRACE_SETGLOBAL, 3},                         /* 118 G */
    {XXBYTE_YARV_ID_TRACE_PUTNIL, 0},                            /* 119 - */
    {XXBYTE_YARV_ID_TRACE_PUTSELF, 0},                           /* 120 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT, 11},                        /* 121 V */
    {XXBYTE_YARV_ID_TRACE_PUTSPECIALOBJECT, 7},                  /* 122 N */
    {XXBYTE_YARV_ID_TRACE_PUTSTRING, 11},                        /* 123 V */
    {XXBYTE_YARV_ID_TRACE_CONCATSTRINGS, 7},                     /* 124 N */
    {XXBYTE_YARV_ID_TRACE_TOSTRING, 0},                          /* 125 - */
    {XXBYTE_YARV_ID_TRACE_FREEZESTRING, 11},                     /* 126 V */
    {XXBYTE_YARV_ID_TRACE_TOREGEXP, 24},                         /* 127 NN */
    {XXBYTE_YARV_ID_TRACE_INTERN, 0},                            /* 128 - */
    {XXBYTE_YARV_ID_TRACE_NEWARRAY, 7},                          /* 129 N */
    {XXBYTE_YARV_ID_TRACE_NEWARRAYKWSPLAT, 7},                   /* 130 N */
    {XXBYTE_YARV_ID_TRACE_DUPARRAY, 11},                         /* 131 V */
    {XXBYTE_YARV_ID_TRACE_DUPHASH, 11},                          /* 132 V */
    {XXBYTE_YARV_ID_TRACE_EXPANDARRAY, 24},                      /* 133 NN */
    {XXBYTE_YARV_ID_TRACE_CONCATARRAY, 0},                       /* 134 - */
    {XXBYTE_YARV_ID_TRACE_SPLATARRAY, 11},                       /* 135 V */
    {XXBYTE_YARV_ID_TRACE_NEWHASH, 7},                           /* 136 N */
    {XXBYTE_YARV_ID_TRACE_NEWRANGE, 7},                          /* 137 N */
    {XXBYTE_YARV_ID_TRACE_POP, 0},                               /* 138 - */
    {XXBYTE_YARV_ID_TRACE_DUP, 0},                               /* 139 - */
    {XXBYTE_YARV_ID_TRACE_DUPN, 7},                              /* 140 N */
    {XXBYTE_YARV_ID_TRACE_SWAP, 0},                              /* 141 - */
    {XXBYTE_YARV_ID_TRACE_REVERSE, 7},                           /* 142 N */
    {XXBYTE_YARV_ID_TRACE_TOPN, 7},                              /* 143 N */
    {XXBYTE_YARV_ID_TRACE_SETN, 7},                              /* 144 N */
    {XXBYTE_YARV_ID_TRACE_ADJUSTSTACK, 7},                       /* 145 N */
    {XXBYTE_YARV_ID_TRACE_DEFINED, 32},                          /* 146 NVV */
    {XXBYTE_YARV_ID_TRACE_CHECKMATCH, 7},                        /* 147 N */
    {XXBYTE_YARV_ID_TRACE_CHECKKEYWORD, 21},                     /* 148 LL */
    {XXBYTE_YARV_ID_TRACE_CHECKTYPE, 7},                         /* 149 N */
    {XXBYTE_YARV_ID_TRACE_DEFINECLASS, 31},                      /* 150 ISN */
    {XXBYTE_YARV_ID_TRACE_DEFINEMETHOD, 20},                     /* 151 IS */
    {XXBYTE_YARV_ID_TRACE_DEFINESMETHOD, 20},                    /* 152 IS */
    {XXBYTE_YARV_ID_TRACE_SEND, 15},                             /* 153 CS */
    {XXBYTE_YARV_ID_TRACE_OPT_SEND_WITHOUT_BLOCK, 1},            /* 154 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_FREEZE, 28},                   /* 155 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NIL_P, 1},                         /* 156 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_UMINUS, 28},                   /* 157 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MAX, 7},                  /* 158 N */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MIN, 7},                  /* 159 N */
    {XXBYTE_YARV_ID_TRACE_INVOKESUPER, 15},                      /* 160 CS */
    {XXBYTE_YARV_ID_TRACE_INVOKEBLOCK, 1},                       /* 161 C */
    {XXBYTE_YARV_ID_TRACE_LEAVE, 0},                             /* 162 - */
    {XXBYTE_YARV_ID_TRACE_THROW, 7},                             /* 163 N */
    {XXBYTE_YARV_ID_TRACE_JUMP, 8},                              /* 164 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHIF, 8},                          /* 165 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHUNLESS, 8},                      /* 166 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHNIL, 8},                         /* 167 O */
    {XXBYTE_YARV_ID_TRACE_OPT_GETINLINECACHE, 25},               /* 168 OK */
    {XXBYTE_YARV_ID_TRACE_OPT_SETINLINECACHE, 5},                /* 169 K */
    {XXBYTE_YARV_ID_TRACE_ONCE, 27},                             /* 170 ST */
    {XXBYTE_YARV_ID_TRACE_OPT_CASE_DISPATCH, 16},                /* 171 HO */
    {XXBYTE_YARV_ID_TRACE_OPT_PLUS, 1},                          /* 172 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MINUS, 1},                         /* 173 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MULT, 1},                          /* 174 C */
    {XXBYTE_YARV_ID_TRACE_OPT_DIV, 1},                           /* 175 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MOD, 1},                           /* 176 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EQ, 1},                            /* 177 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NEQ, 12},                          /* 178 CC */
    {XXBYTE_YARV_ID_TRACE_OPT_LT, 1},                            /* 179 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LE, 1},                            /* 180 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GT, 1},                            /* 181 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GE, 1},                            /* 182 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LTLT, 1},                          /* 183 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AND, 1},                           /* 184 C */
    {XXBYTE_YARV_ID_TRACE_OPT_OR, 1},                            /* 185 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF, 1},                          /* 186 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET, 1},                          /* 187 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET_WITH, 28},                    /* 188 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF_WITH, 28},                    /* 189 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_LENGTH, 1},                        /* 190 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SIZE, 1},                          /* 191 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EMPTY_P, 1},                       /* 192 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SUCC, 1},                          /* 193 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NOT, 1},                           /* 194 C */
    {XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH2, 1},                  /* 195 C */
    {XXBYTE_YARV_ID_TRACE_OPT_CALL_C_FUNCTION, 2},               /* 196 F */
    {XXBYTE_YARV_ID_TRACE_INVOKEBUILTIN, 9},                     /* 197 R */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE, 26},       /* 198 RN */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26}, /* 199 RN */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_0, 6},                     /* 200 L */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_1, 6},                     /* 201 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_0, 6},                     /* 202 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_1, 6},                     /* 203 L */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_0_, 0},              /* 204 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_1_, 0},              /* 205 - */
};

/* Ruby 3.0.7 (202 instructions) */
static const yarv_op yarv_ops_3_0[202] = {
    {XXBYTE_YARV_ID_NOP, 0},                                     /*   0 - */
    {XXBYTE_YARV_ID_GETLOCAL, 22},                               /*   1 LN */
    {XXBYTE_YARV_ID_SETLOCAL, 22},                               /*   2 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAM, 22},                          /*   3 LN */
    {XXBYTE_YARV_ID_SETBLOCKPARAM, 22},                          /*   4 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAMPROXY, 22},                     /*   5 LN */
    {XXBYTE_YARV_ID_GETSPECIAL, 24},                             /*   6 NN */
    {XXBYTE_YARV_ID_SETSPECIAL, 7},                              /*   7 N */
    {XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 17},                    /*   8 IA */
    {XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 17},                    /*   9 IA */
    {XXBYTE_YARV_ID_GETCLASSVARIABLE, 4},                        /*  10 I */
    {XXBYTE_YARV_ID_SETCLASSVARIABLE, 4},                        /*  11 I */
    {XXBYTE_YARV_ID_GETCONSTANT, 4},                             /*  12 I */
    {XXBYTE_YARV_ID_SETCONSTANT, 4},                             /*  13 I */
    {XXBYTE_YARV_ID_GETGLOBAL, 4},                               /*  14 I */
    {XXBYTE_YARV_ID_SETGLOBAL, 4},                               /*  15 I */
    {XXBYTE_YARV_ID_PUTNIL, 0},                                  /*  16 - */
    {XXBYTE_YARV_ID_PUTSELF, 0},                                 /*  17 - */
    {XXBYTE_YARV_ID_PUTOBJECT, 11},                              /*  18 V */
    {XXBYTE_YARV_ID_PUTSPECIALOBJECT, 7},                        /*  19 N */
    {XXBYTE_YARV_ID_PUTSTRING, 11},                              /*  20 V */
    {XXBYTE_YARV_ID_CONCATSTRINGS, 7},                           /*  21 N */
    {XXBYTE_YARV_ID_TOSTRING, 0},                                /*  22 - */
    {XXBYTE_YARV_ID_TOREGEXP, 24},                               /*  23 NN */
    {XXBYTE_YARV_ID_INTERN, 0},                                  /*  24 - */
    {XXBYTE_YARV_ID_NEWARRAY, 7},                                /*  25 N */
    {XXBYTE_YARV_ID_NEWARRAYKWSPLAT, 7},                         /*  26 N */
    {XXBYTE_YARV_ID_DUPARRAY, 11},                               /*  27 V */
    {XXBYTE_YARV_ID_DUPHASH, 11},                                /*  28 V */
    {XXBYTE_YARV_ID_EXPANDARRAY, 24},                            /*  29 NN */
    {XXBYTE_YARV_ID_CONCATARRAY, 0},                             /*  30 - */
    {XXBYTE_YARV_ID_SPLATARRAY, 11},                             /*  31 V */
    {XXBYTE_YARV_ID_NEWHASH, 7},                                 /*  32 N */
    {XXBYTE_YARV_ID_NEWRANGE, 7},                                /*  33 N */
    {XXBYTE_YARV_ID_POP, 0},                                     /*  34 - */
    {XXBYTE_YARV_ID_DUP, 0},                                     /*  35 - */
    {XXBYTE_YARV_ID_DUPN, 7},                                    /*  36 N */
    {XXBYTE_YARV_ID_SWAP, 0},                                    /*  37 - */
    {XXBYTE_YARV_ID_REVERSE, 7},                                 /*  38 N */
    {XXBYTE_YARV_ID_TOPN, 7},                                    /*  39 N */
    {XXBYTE_YARV_ID_SETN, 7},                                    /*  40 N */
    {XXBYTE_YARV_ID_ADJUSTSTACK, 7},                             /*  41 N */
    {XXBYTE_YARV_ID_DEFINED, 32},                                /*  42 NVV */
    {XXBYTE_YARV_ID_CHECKMATCH, 7},                              /*  43 N */
    {XXBYTE_YARV_ID_CHECKKEYWORD, 21},                           /*  44 LL */
    {XXBYTE_YARV_ID_CHECKTYPE, 7},                               /*  45 N */
    {XXBYTE_YARV_ID_DEFINECLASS, 31},                            /*  46 ISN */
    {XXBYTE_YARV_ID_DEFINEMETHOD, 20},                           /*  47 IS */
    {XXBYTE_YARV_ID_DEFINESMETHOD, 20},                          /*  48 IS */
    {XXBYTE_YARV_ID_SEND, 15},                                   /*  49 CS */
    {XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, 1},                  /*  50 C */
    {XXBYTE_YARV_ID_OPT_STR_FREEZE, 28},                         /*  51 VC */
    {XXBYTE_YARV_ID_OPT_NIL_P, 1},                               /*  52 C */
    {XXBYTE_YARV_ID_OPT_STR_UMINUS, 28},                         /*  53 VC */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_MAX, 7},                        /*  54 N */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_MIN, 7},                        /*  55 N */
    {XXBYTE_YARV_ID_INVOKESUPER, 15},                            /*  56 CS */
    {XXBYTE_YARV_ID_INVOKEBLOCK, 1},                             /*  57 C */
    {XXBYTE_YARV_ID_LEAVE, 0},                                   /*  58 - */
    {XXBYTE_YARV_ID_THROW, 7},                                   /*  59 N */
    {XXBYTE_YARV_ID_JUMP, 8},                                    /*  60 O */
    {XXBYTE_YARV_ID_BRANCHIF, 8},                                /*  61 O */
    {XXBYTE_YARV_ID_BRANCHUNLESS, 8},                            /*  62 O */
    {XXBYTE_YARV_ID_BRANCHNIL, 8},                               /*  63 O */
    {XXBYTE_YARV_ID_OPT_GETINLINECACHE, 25},                     /*  64 OK */
    {XXBYTE_YARV_ID_OPT_SETINLINECACHE, 5},                      /*  65 K */
    {XXBYTE_YARV_ID_ONCE, 27},                                   /*  66 ST */
    {XXBYTE_YARV_ID_OPT_CASE_DISPATCH, 16},                      /*  67 HO */
    {XXBYTE_YARV_ID_OPT_PLUS, 1},                                /*  68 C */
    {XXBYTE_YARV_ID_OPT_MINUS, 1},                               /*  69 C */
    {XXBYTE_YARV_ID_OPT_MULT, 1},                                /*  70 C */
    {XXBYTE_YARV_ID_OPT_DIV, 1},                                 /*  71 C */
    {XXBYTE_YARV_ID_OPT_MOD, 1},                                 /*  72 C */
    {XXBYTE_YARV_ID_OPT_EQ, 1},                                  /*  73 C */
    {XXBYTE_YARV_ID_OPT_NEQ, 12},                                /*  74 CC */
    {XXBYTE_YARV_ID_OPT_LT, 1},                                  /*  75 C */
    {XXBYTE_YARV_ID_OPT_LE, 1},                                  /*  76 C */
    {XXBYTE_YARV_ID_OPT_GT, 1},                                  /*  77 C */
    {XXBYTE_YARV_ID_OPT_GE, 1},                                  /*  78 C */
    {XXBYTE_YARV_ID_OPT_LTLT, 1},                                /*  79 C */
    {XXBYTE_YARV_ID_OPT_AND, 1},                                 /*  80 C */
    {XXBYTE_YARV_ID_OPT_OR, 1},                                  /*  81 C */
    {XXBYTE_YARV_ID_OPT_AREF, 1},                                /*  82 C */
    {XXBYTE_YARV_ID_OPT_ASET, 1},                                /*  83 C */
    {XXBYTE_YARV_ID_OPT_ASET_WITH, 28},                          /*  84 VC */
    {XXBYTE_YARV_ID_OPT_AREF_WITH, 28},                          /*  85 VC */
    {XXBYTE_YARV_ID_OPT_LENGTH, 1},                              /*  86 C */
    {XXBYTE_YARV_ID_OPT_SIZE, 1},                                /*  87 C */
    {XXBYTE_YARV_ID_OPT_EMPTY_P, 1},                             /*  88 C */
    {XXBYTE_YARV_ID_OPT_SUCC, 1},                                /*  89 C */
    {XXBYTE_YARV_ID_OPT_NOT, 1},                                 /*  90 C */
    {XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 1},                        /*  91 C */
    {XXBYTE_YARV_ID_INVOKEBUILTIN, 9},                           /*  92 R */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE, 26},             /*  93 RN */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26},       /*  94 RN */
    {XXBYTE_YARV_ID_GETLOCAL_WC_0, 6},                           /*  95 L */
    {XXBYTE_YARV_ID_GETLOCAL_WC_1, 6},                           /*  96 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_0, 6},                           /*  97 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_1, 6},                           /*  98 L */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_, 0},                    /*  99 - */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_, 0},                    /* 100 - */
    {XXBYTE_YARV_ID_TRACE_NOP, 0},                               /* 101 - */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL, 22},                         /* 102 LN */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL, 22},                         /* 103 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAM, 22},                    /* 104 LN */
    {XXBYTE_YARV_ID_TRACE_SETBLOCKPARAM, 22},                    /* 105 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAMPROXY, 22},               /* 106 LN */
    {XXBYTE_YARV_ID_TRACE_GETSPECIAL, 24},                       /* 107 NN */
    {XXBYTE_YARV_ID_TRACE_SETSPECIAL, 7},                        /* 108 N */
    {XXBYTE_YARV_ID_TRACE_GETINSTANCEVARIABLE, 17},              /* 109 IA */
    {XXBYTE_YARV_ID_TRACE_SETINSTANCEVARIABLE, 17},              /* 110 IA */
    {XXBYTE_YARV_ID_TRACE_GETCLASSVARIABLE, 4},                  /* 111 I */
    {XXBYTE_YARV_ID_TRACE_SETCLASSVARIABLE, 4},                  /* 112 I */
    {XXBYTE_YARV_ID_TRACE_GETCONSTANT, 4},                       /* 113 I */
    {XXBYTE_YARV_ID_TRACE_SETCONSTANT, 4},                       /* 114 I */
    {XXBYTE_YARV_ID_TRACE_GETGLOBAL, 4},                         /* 115 I */
    {XXBYTE_YARV_ID_TRACE_SETGLOBAL, 4},                         /* 116 I */
    {XXBYTE_YARV_ID_TRACE_PUTNIL, 0},                            /* 117 - */
    {XXBYTE_YARV_ID_TRACE_PUTSELF, 0},                           /* 118 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT, 11},                        /* 119 V */
    {XXBYTE_YARV_ID_TRACE_PUTSPECIALOBJECT, 7},                  /* 120 N */
    {XXBYTE_YARV_ID_TRACE_PUTSTRING, 11},                        /* 121 V */
    {XXBYTE_YARV_ID_TRACE_CONCATSTRINGS, 7},                     /* 122 N */
    {XXBYTE_YARV_ID_TRACE_TOSTRING, 0},                          /* 123 - */
    {XXBYTE_YARV_ID_TRACE_TOREGEXP, 24},                         /* 124 NN */
    {XXBYTE_YARV_ID_TRACE_INTERN, 0},                            /* 125 - */
    {XXBYTE_YARV_ID_TRACE_NEWARRAY, 7},                          /* 126 N */
    {XXBYTE_YARV_ID_TRACE_NEWARRAYKWSPLAT, 7},                   /* 127 N */
    {XXBYTE_YARV_ID_TRACE_DUPARRAY, 11},                         /* 128 V */
    {XXBYTE_YARV_ID_TRACE_DUPHASH, 11},                          /* 129 V */
    {XXBYTE_YARV_ID_TRACE_EXPANDARRAY, 24},                      /* 130 NN */
    {XXBYTE_YARV_ID_TRACE_CONCATARRAY, 0},                       /* 131 - */
    {XXBYTE_YARV_ID_TRACE_SPLATARRAY, 11},                       /* 132 V */
    {XXBYTE_YARV_ID_TRACE_NEWHASH, 7},                           /* 133 N */
    {XXBYTE_YARV_ID_TRACE_NEWRANGE, 7},                          /* 134 N */
    {XXBYTE_YARV_ID_TRACE_POP, 0},                               /* 135 - */
    {XXBYTE_YARV_ID_TRACE_DUP, 0},                               /* 136 - */
    {XXBYTE_YARV_ID_TRACE_DUPN, 7},                              /* 137 N */
    {XXBYTE_YARV_ID_TRACE_SWAP, 0},                              /* 138 - */
    {XXBYTE_YARV_ID_TRACE_REVERSE, 7},                           /* 139 N */
    {XXBYTE_YARV_ID_TRACE_TOPN, 7},                              /* 140 N */
    {XXBYTE_YARV_ID_TRACE_SETN, 7},                              /* 141 N */
    {XXBYTE_YARV_ID_TRACE_ADJUSTSTACK, 7},                       /* 142 N */
    {XXBYTE_YARV_ID_TRACE_DEFINED, 32},                          /* 143 NVV */
    {XXBYTE_YARV_ID_TRACE_CHECKMATCH, 7},                        /* 144 N */
    {XXBYTE_YARV_ID_TRACE_CHECKKEYWORD, 21},                     /* 145 LL */
    {XXBYTE_YARV_ID_TRACE_CHECKTYPE, 7},                         /* 146 N */
    {XXBYTE_YARV_ID_TRACE_DEFINECLASS, 31},                      /* 147 ISN */
    {XXBYTE_YARV_ID_TRACE_DEFINEMETHOD, 20},                     /* 148 IS */
    {XXBYTE_YARV_ID_TRACE_DEFINESMETHOD, 20},                    /* 149 IS */
    {XXBYTE_YARV_ID_TRACE_SEND, 15},                             /* 150 CS */
    {XXBYTE_YARV_ID_TRACE_OPT_SEND_WITHOUT_BLOCK, 1},            /* 151 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_FREEZE, 28},                   /* 152 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NIL_P, 1},                         /* 153 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_UMINUS, 28},                   /* 154 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MAX, 7},                  /* 155 N */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MIN, 7},                  /* 156 N */
    {XXBYTE_YARV_ID_TRACE_INVOKESUPER, 15},                      /* 157 CS */
    {XXBYTE_YARV_ID_TRACE_INVOKEBLOCK, 1},                       /* 158 C */
    {XXBYTE_YARV_ID_TRACE_LEAVE, 0},                             /* 159 - */
    {XXBYTE_YARV_ID_TRACE_THROW, 7},                             /* 160 N */
    {XXBYTE_YARV_ID_TRACE_JUMP, 8},                              /* 161 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHIF, 8},                          /* 162 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHUNLESS, 8},                      /* 163 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHNIL, 8},                         /* 164 O */
    {XXBYTE_YARV_ID_TRACE_OPT_GETINLINECACHE, 25},               /* 165 OK */
    {XXBYTE_YARV_ID_TRACE_OPT_SETINLINECACHE, 5},                /* 166 K */
    {XXBYTE_YARV_ID_TRACE_ONCE, 27},                             /* 167 ST */
    {XXBYTE_YARV_ID_TRACE_OPT_CASE_DISPATCH, 16},                /* 168 HO */
    {XXBYTE_YARV_ID_TRACE_OPT_PLUS, 1},                          /* 169 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MINUS, 1},                         /* 170 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MULT, 1},                          /* 171 C */
    {XXBYTE_YARV_ID_TRACE_OPT_DIV, 1},                           /* 172 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MOD, 1},                           /* 173 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EQ, 1},                            /* 174 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NEQ, 12},                          /* 175 CC */
    {XXBYTE_YARV_ID_TRACE_OPT_LT, 1},                            /* 176 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LE, 1},                            /* 177 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GT, 1},                            /* 178 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GE, 1},                            /* 179 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LTLT, 1},                          /* 180 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AND, 1},                           /* 181 C */
    {XXBYTE_YARV_ID_TRACE_OPT_OR, 1},                            /* 182 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF, 1},                          /* 183 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET, 1},                          /* 184 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET_WITH, 28},                    /* 185 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF_WITH, 28},                    /* 186 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_LENGTH, 1},                        /* 187 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SIZE, 1},                          /* 188 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EMPTY_P, 1},                       /* 189 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SUCC, 1},                          /* 190 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NOT, 1},                           /* 191 C */
    {XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH2, 1},                  /* 192 C */
    {XXBYTE_YARV_ID_TRACE_INVOKEBUILTIN, 9},                     /* 193 R */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE, 26},       /* 194 RN */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26}, /* 195 RN */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_0, 6},                     /* 196 L */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_1, 6},                     /* 197 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_0, 6},                     /* 198 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_1, 6},                     /* 199 L */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_0_, 0},              /* 200 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_1_, 0},              /* 201 - */
};

/* Ruby 3.1.7 (202 instructions) */
static const yarv_op yarv_ops_3_1[202] = {
    {XXBYTE_YARV_ID_NOP, 0},                                     /*   0 - */
    {XXBYTE_YARV_ID_GETLOCAL, 22},                               /*   1 LN */
    {XXBYTE_YARV_ID_SETLOCAL, 22},                               /*   2 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAM, 22},                          /*   3 LN */
    {XXBYTE_YARV_ID_SETBLOCKPARAM, 22},                          /*   4 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAMPROXY, 22},                     /*   5 LN */
    {XXBYTE_YARV_ID_GETSPECIAL, 24},                             /*   6 NN */
    {XXBYTE_YARV_ID_SETSPECIAL, 7},                              /*   7 N */
    {XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 17},                    /*   8 IA */
    {XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 17},                    /*   9 IA */
    {XXBYTE_YARV_ID_GETCLASSVARIABLE, 17},                       /*  10 IA */
    {XXBYTE_YARV_ID_SETCLASSVARIABLE, 17},                       /*  11 IA */
    {XXBYTE_YARV_ID_GETCONSTANT, 4},                             /*  12 I */
    {XXBYTE_YARV_ID_SETCONSTANT, 4},                             /*  13 I */
    {XXBYTE_YARV_ID_GETGLOBAL, 4},                               /*  14 I */
    {XXBYTE_YARV_ID_SETGLOBAL, 4},                               /*  15 I */
    {XXBYTE_YARV_ID_PUTNIL, 0},                                  /*  16 - */
    {XXBYTE_YARV_ID_PUTSELF, 0},                                 /*  17 - */
    {XXBYTE_YARV_ID_PUTOBJECT, 11},                              /*  18 V */
    {XXBYTE_YARV_ID_PUTSPECIALOBJECT, 7},                        /*  19 N */
    {XXBYTE_YARV_ID_PUTSTRING, 11},                              /*  20 V */
    {XXBYTE_YARV_ID_CONCATSTRINGS, 7},                           /*  21 N */
    {XXBYTE_YARV_ID_ANYTOSTRING, 0},                             /*  22 - */
    {XXBYTE_YARV_ID_TOREGEXP, 24},                               /*  23 NN */
    {XXBYTE_YARV_ID_INTERN, 0},                                  /*  24 - */
    {XXBYTE_YARV_ID_NEWARRAY, 7},                                /*  25 N */
    {XXBYTE_YARV_ID_NEWARRAYKWSPLAT, 7},                         /*  26 N */
    {XXBYTE_YARV_ID_DUPARRAY, 11},                               /*  27 V */
    {XXBYTE_YARV_ID_DUPHASH, 11},                                /*  28 V */
    {XXBYTE_YARV_ID_EXPANDARRAY, 24},                            /*  29 NN */
    {XXBYTE_YARV_ID_CONCATARRAY, 0},                             /*  30 - */
    {XXBYTE_YARV_ID_SPLATARRAY, 11},                             /*  31 V */
    {XXBYTE_YARV_ID_NEWHASH, 7},                                 /*  32 N */
    {XXBYTE_YARV_ID_NEWRANGE, 7},                                /*  33 N */
    {XXBYTE_YARV_ID_POP, 0},                                     /*  34 - */
    {XXBYTE_YARV_ID_DUP, 0},                                     /*  35 - */
    {XXBYTE_YARV_ID_DUPN, 7},                                    /*  36 N */
    {XXBYTE_YARV_ID_SWAP, 0},                                    /*  37 - */
    {XXBYTE_YARV_ID_TOPN, 7},                                    /*  38 N */
    {XXBYTE_YARV_ID_SETN, 7},                                    /*  39 N */
    {XXBYTE_YARV_ID_ADJUSTSTACK, 7},                             /*  40 N */
    {XXBYTE_YARV_ID_DEFINED, 32},                                /*  41 NVV */
    {XXBYTE_YARV_ID_CHECKMATCH, 7},                              /*  42 N */
    {XXBYTE_YARV_ID_CHECKKEYWORD, 21},                           /*  43 LL */
    {XXBYTE_YARV_ID_CHECKTYPE, 7},                               /*  44 N */
    {XXBYTE_YARV_ID_DEFINECLASS, 31},                            /*  45 ISN */
    {XXBYTE_YARV_ID_DEFINEMETHOD, 20},                           /*  46 IS */
    {XXBYTE_YARV_ID_DEFINESMETHOD, 20},                          /*  47 IS */
    {XXBYTE_YARV_ID_SEND, 15},                                   /*  48 CS */
    {XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, 1},                  /*  49 C */
    {XXBYTE_YARV_ID_OBJTOSTRING, 1},                             /*  50 C */
    {XXBYTE_YARV_ID_OPT_STR_FREEZE, 28},                         /*  51 VC */
    {XXBYTE_YARV_ID_OPT_NIL_P, 1},                               /*  52 C */
    {XXBYTE_YARV_ID_OPT_STR_UMINUS, 28},                         /*  53 VC */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_MAX, 7},                        /*  54 N */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_MIN, 7},                        /*  55 N */
    {XXBYTE_YARV_ID_INVOKESUPER, 15},                            /*  56 CS */
    {XXBYTE_YARV_ID_INVOKEBLOCK, 1},                             /*  57 C */
    {XXBYTE_YARV_ID_LEAVE, 0},                                   /*  58 - */
    {XXBYTE_YARV_ID_THROW, 7},                                   /*  59 N */
    {XXBYTE_YARV_ID_JUMP, 8},                                    /*  60 O */
    {XXBYTE_YARV_ID_BRANCHIF, 8},                                /*  61 O */
    {XXBYTE_YARV_ID_BRANCHUNLESS, 8},                            /*  62 O */
    {XXBYTE_YARV_ID_BRANCHNIL, 8},                               /*  63 O */
    {XXBYTE_YARV_ID_OPT_GETINLINECACHE, 25},                     /*  64 OK */
    {XXBYTE_YARV_ID_OPT_SETINLINECACHE, 5},                      /*  65 K */
    {XXBYTE_YARV_ID_ONCE, 27},                                   /*  66 ST */
    {XXBYTE_YARV_ID_OPT_CASE_DISPATCH, 16},                      /*  67 HO */
    {XXBYTE_YARV_ID_OPT_PLUS, 1},                                /*  68 C */
    {XXBYTE_YARV_ID_OPT_MINUS, 1},                               /*  69 C */
    {XXBYTE_YARV_ID_OPT_MULT, 1},                                /*  70 C */
    {XXBYTE_YARV_ID_OPT_DIV, 1},                                 /*  71 C */
    {XXBYTE_YARV_ID_OPT_MOD, 1},                                 /*  72 C */
    {XXBYTE_YARV_ID_OPT_EQ, 1},                                  /*  73 C */
    {XXBYTE_YARV_ID_OPT_NEQ, 12},                                /*  74 CC */
    {XXBYTE_YARV_ID_OPT_LT, 1},                                  /*  75 C */
    {XXBYTE_YARV_ID_OPT_LE, 1},                                  /*  76 C */
    {XXBYTE_YARV_ID_OPT_GT, 1},                                  /*  77 C */
    {XXBYTE_YARV_ID_OPT_GE, 1},                                  /*  78 C */
    {XXBYTE_YARV_ID_OPT_LTLT, 1},                                /*  79 C */
    {XXBYTE_YARV_ID_OPT_AND, 1},                                 /*  80 C */
    {XXBYTE_YARV_ID_OPT_OR, 1},                                  /*  81 C */
    {XXBYTE_YARV_ID_OPT_AREF, 1},                                /*  82 C */
    {XXBYTE_YARV_ID_OPT_ASET, 1},                                /*  83 C */
    {XXBYTE_YARV_ID_OPT_ASET_WITH, 28},                          /*  84 VC */
    {XXBYTE_YARV_ID_OPT_AREF_WITH, 28},                          /*  85 VC */
    {XXBYTE_YARV_ID_OPT_LENGTH, 1},                              /*  86 C */
    {XXBYTE_YARV_ID_OPT_SIZE, 1},                                /*  87 C */
    {XXBYTE_YARV_ID_OPT_EMPTY_P, 1},                             /*  88 C */
    {XXBYTE_YARV_ID_OPT_SUCC, 1},                                /*  89 C */
    {XXBYTE_YARV_ID_OPT_NOT, 1},                                 /*  90 C */
    {XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 1},                        /*  91 C */
    {XXBYTE_YARV_ID_INVOKEBUILTIN, 9},                           /*  92 R */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE, 26},             /*  93 RN */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26},       /*  94 RN */
    {XXBYTE_YARV_ID_GETLOCAL_WC_0, 6},                           /*  95 L */
    {XXBYTE_YARV_ID_GETLOCAL_WC_1, 6},                           /*  96 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_0, 6},                           /*  97 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_1, 6},                           /*  98 L */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_, 0},                    /*  99 - */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_, 0},                    /* 100 - */
    {XXBYTE_YARV_ID_TRACE_NOP, 0},                               /* 101 - */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL, 22},                         /* 102 LN */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL, 22},                         /* 103 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAM, 22},                    /* 104 LN */
    {XXBYTE_YARV_ID_TRACE_SETBLOCKPARAM, 22},                    /* 105 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAMPROXY, 22},               /* 106 LN */
    {XXBYTE_YARV_ID_TRACE_GETSPECIAL, 24},                       /* 107 NN */
    {XXBYTE_YARV_ID_TRACE_SETSPECIAL, 7},                        /* 108 N */
    {XXBYTE_YARV_ID_TRACE_GETINSTANCEVARIABLE, 17},              /* 109 IA */
    {XXBYTE_YARV_ID_TRACE_SETINSTANCEVARIABLE, 17},              /* 110 IA */
    {XXBYTE_YARV_ID_TRACE_GETCLASSVARIABLE, 17},                 /* 111 IA */
    {XXBYTE_YARV_ID_TRACE_SETCLASSVARIABLE, 17},                 /* 112 IA */
    {XXBYTE_YARV_ID_TRACE_GETCONSTANT, 4},                       /* 113 I */
    {XXBYTE_YARV_ID_TRACE_SETCONSTANT, 4},                       /* 114 I */
    {XXBYTE_YARV_ID_TRACE_GETGLOBAL, 4},                         /* 115 I */
    {XXBYTE_YARV_ID_TRACE_SETGLOBAL, 4},                         /* 116 I */
    {XXBYTE_YARV_ID_TRACE_PUTNIL, 0},                            /* 117 - */
    {XXBYTE_YARV_ID_TRACE_PUTSELF, 0},                           /* 118 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT, 11},                        /* 119 V */
    {XXBYTE_YARV_ID_TRACE_PUTSPECIALOBJECT, 7},                  /* 120 N */
    {XXBYTE_YARV_ID_TRACE_PUTSTRING, 11},                        /* 121 V */
    {XXBYTE_YARV_ID_TRACE_CONCATSTRINGS, 7},                     /* 122 N */
    {XXBYTE_YARV_ID_TRACE_ANYTOSTRING, 0},                       /* 123 - */
    {XXBYTE_YARV_ID_TRACE_TOREGEXP, 24},                         /* 124 NN */
    {XXBYTE_YARV_ID_TRACE_INTERN, 0},                            /* 125 - */
    {XXBYTE_YARV_ID_TRACE_NEWARRAY, 7},                          /* 126 N */
    {XXBYTE_YARV_ID_TRACE_NEWARRAYKWSPLAT, 7},                   /* 127 N */
    {XXBYTE_YARV_ID_TRACE_DUPARRAY, 11},                         /* 128 V */
    {XXBYTE_YARV_ID_TRACE_DUPHASH, 11},                          /* 129 V */
    {XXBYTE_YARV_ID_TRACE_EXPANDARRAY, 24},                      /* 130 NN */
    {XXBYTE_YARV_ID_TRACE_CONCATARRAY, 0},                       /* 131 - */
    {XXBYTE_YARV_ID_TRACE_SPLATARRAY, 11},                       /* 132 V */
    {XXBYTE_YARV_ID_TRACE_NEWHASH, 7},                           /* 133 N */
    {XXBYTE_YARV_ID_TRACE_NEWRANGE, 7},                          /* 134 N */
    {XXBYTE_YARV_ID_TRACE_POP, 0},                               /* 135 - */
    {XXBYTE_YARV_ID_TRACE_DUP, 0},                               /* 136 - */
    {XXBYTE_YARV_ID_TRACE_DUPN, 7},                              /* 137 N */
    {XXBYTE_YARV_ID_TRACE_SWAP, 0},                              /* 138 - */
    {XXBYTE_YARV_ID_TRACE_TOPN, 7},                              /* 139 N */
    {XXBYTE_YARV_ID_TRACE_SETN, 7},                              /* 140 N */
    {XXBYTE_YARV_ID_TRACE_ADJUSTSTACK, 7},                       /* 141 N */
    {XXBYTE_YARV_ID_TRACE_DEFINED, 32},                          /* 142 NVV */
    {XXBYTE_YARV_ID_TRACE_CHECKMATCH, 7},                        /* 143 N */
    {XXBYTE_YARV_ID_TRACE_CHECKKEYWORD, 21},                     /* 144 LL */
    {XXBYTE_YARV_ID_TRACE_CHECKTYPE, 7},                         /* 145 N */
    {XXBYTE_YARV_ID_TRACE_DEFINECLASS, 31},                      /* 146 ISN */
    {XXBYTE_YARV_ID_TRACE_DEFINEMETHOD, 20},                     /* 147 IS */
    {XXBYTE_YARV_ID_TRACE_DEFINESMETHOD, 20},                    /* 148 IS */
    {XXBYTE_YARV_ID_TRACE_SEND, 15},                             /* 149 CS */
    {XXBYTE_YARV_ID_TRACE_OPT_SEND_WITHOUT_BLOCK, 1},            /* 150 C */
    {XXBYTE_YARV_ID_TRACE_OBJTOSTRING, 1},                       /* 151 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_FREEZE, 28},                   /* 152 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NIL_P, 1},                         /* 153 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_UMINUS, 28},                   /* 154 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MAX, 7},                  /* 155 N */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MIN, 7},                  /* 156 N */
    {XXBYTE_YARV_ID_TRACE_INVOKESUPER, 15},                      /* 157 CS */
    {XXBYTE_YARV_ID_TRACE_INVOKEBLOCK, 1},                       /* 158 C */
    {XXBYTE_YARV_ID_TRACE_LEAVE, 0},                             /* 159 - */
    {XXBYTE_YARV_ID_TRACE_THROW, 7},                             /* 160 N */
    {XXBYTE_YARV_ID_TRACE_JUMP, 8},                              /* 161 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHIF, 8},                          /* 162 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHUNLESS, 8},                      /* 163 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHNIL, 8},                         /* 164 O */
    {XXBYTE_YARV_ID_TRACE_OPT_GETINLINECACHE, 25},               /* 165 OK */
    {XXBYTE_YARV_ID_TRACE_OPT_SETINLINECACHE, 5},                /* 166 K */
    {XXBYTE_YARV_ID_TRACE_ONCE, 27},                             /* 167 ST */
    {XXBYTE_YARV_ID_TRACE_OPT_CASE_DISPATCH, 16},                /* 168 HO */
    {XXBYTE_YARV_ID_TRACE_OPT_PLUS, 1},                          /* 169 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MINUS, 1},                         /* 170 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MULT, 1},                          /* 171 C */
    {XXBYTE_YARV_ID_TRACE_OPT_DIV, 1},                           /* 172 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MOD, 1},                           /* 173 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EQ, 1},                            /* 174 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NEQ, 12},                          /* 175 CC */
    {XXBYTE_YARV_ID_TRACE_OPT_LT, 1},                            /* 176 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LE, 1},                            /* 177 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GT, 1},                            /* 178 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GE, 1},                            /* 179 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LTLT, 1},                          /* 180 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AND, 1},                           /* 181 C */
    {XXBYTE_YARV_ID_TRACE_OPT_OR, 1},                            /* 182 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF, 1},                          /* 183 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET, 1},                          /* 184 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET_WITH, 28},                    /* 185 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF_WITH, 28},                    /* 186 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_LENGTH, 1},                        /* 187 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SIZE, 1},                          /* 188 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EMPTY_P, 1},                       /* 189 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SUCC, 1},                          /* 190 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NOT, 1},                           /* 191 C */
    {XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH2, 1},                  /* 192 C */
    {XXBYTE_YARV_ID_TRACE_INVOKEBUILTIN, 9},                     /* 193 R */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE, 26},       /* 194 RN */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26}, /* 195 RN */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_0, 6},                     /* 196 L */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_1, 6},                     /* 197 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_0, 6},                     /* 198 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_1, 6},                     /* 199 L */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_0_, 0},              /* 200 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_1_, 0},              /* 201 - */
};

/* Ruby 3.2.11 (202 instructions) */
static const yarv_op yarv_ops_3_2[202] = {
    {XXBYTE_YARV_ID_NOP, 0},                                     /*   0 - */
    {XXBYTE_YARV_ID_GETLOCAL, 22},                               /*   1 LN */
    {XXBYTE_YARV_ID_SETLOCAL, 22},                               /*   2 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAM, 22},                          /*   3 LN */
    {XXBYTE_YARV_ID_SETBLOCKPARAM, 22},                          /*   4 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAMPROXY, 22},                     /*   5 LN */
    {XXBYTE_YARV_ID_GETSPECIAL, 24},                             /*   6 NN */
    {XXBYTE_YARV_ID_SETSPECIAL, 7},                              /*   7 N */
    {XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 17},                    /*   8 IA */
    {XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 17},                    /*   9 IA */
    {XXBYTE_YARV_ID_GETCLASSVARIABLE, 18},                       /*  10 IJ */
    {XXBYTE_YARV_ID_SETCLASSVARIABLE, 18},                       /*  11 IJ */
    {XXBYTE_YARV_ID_OPT_GETCONSTANT_PATH, 5},                    /*  12 K */
    {XXBYTE_YARV_ID_GETCONSTANT, 4},                             /*  13 I */
    {XXBYTE_YARV_ID_SETCONSTANT, 4},                             /*  14 I */
    {XXBYTE_YARV_ID_GETGLOBAL, 4},                               /*  15 I */
    {XXBYTE_YARV_ID_SETGLOBAL, 4},                               /*  16 I */
    {XXBYTE_YARV_ID_PUTNIL, 0},                                  /*  17 - */
    {XXBYTE_YARV_ID_PUTSELF, 0},                                 /*  18 - */
    {XXBYTE_YARV_ID_PUTOBJECT, 11},                              /*  19 V */
    {XXBYTE_YARV_ID_PUTSPECIALOBJECT, 7},                        /*  20 N */
    {XXBYTE_YARV_ID_PUTSTRING, 11},                              /*  21 V */
    {XXBYTE_YARV_ID_CONCATSTRINGS, 7},                           /*  22 N */
    {XXBYTE_YARV_ID_ANYTOSTRING, 0},                             /*  23 - */
    {XXBYTE_YARV_ID_TOREGEXP, 24},                               /*  24 NN */
    {XXBYTE_YARV_ID_INTERN, 0},                                  /*  25 - */
    {XXBYTE_YARV_ID_NEWARRAY, 7},                                /*  26 N */
    {XXBYTE_YARV_ID_NEWARRAYKWSPLAT, 7},                         /*  27 N */
    {XXBYTE_YARV_ID_DUPARRAY, 11},                               /*  28 V */
    {XXBYTE_YARV_ID_DUPHASH, 11},                                /*  29 V */
    {XXBYTE_YARV_ID_EXPANDARRAY, 24},                            /*  30 NN */
    {XXBYTE_YARV_ID_CONCATARRAY, 0},                             /*  31 - */
    {XXBYTE_YARV_ID_SPLATARRAY, 11},                             /*  32 V */
    {XXBYTE_YARV_ID_NEWHASH, 7},                                 /*  33 N */
    {XXBYTE_YARV_ID_NEWRANGE, 7},                                /*  34 N */
    {XXBYTE_YARV_ID_POP, 0},                                     /*  35 - */
    {XXBYTE_YARV_ID_DUP, 0},                                     /*  36 - */
    {XXBYTE_YARV_ID_DUPN, 7},                                    /*  37 N */
    {XXBYTE_YARV_ID_SWAP, 0},                                    /*  38 - */
    {XXBYTE_YARV_ID_OPT_REVERSE, 7},                             /*  39 N */
    {XXBYTE_YARV_ID_TOPN, 7},                                    /*  40 N */
    {XXBYTE_YARV_ID_SETN, 7},                                    /*  41 N */
    {XXBYTE_YARV_ID_ADJUSTSTACK, 7},                             /*  42 N */
    {XXBYTE_YARV_ID_DEFINED, 32},                                /*  43 NVV */
    {XXBYTE_YARV_ID_CHECKMATCH, 7},                              /*  44 N */
    {XXBYTE_YARV_ID_CHECKKEYWORD, 21},                           /*  45 LL */
    {XXBYTE_YARV_ID_CHECKTYPE, 7},                               /*  46 N */
    {XXBYTE_YARV_ID_DEFINECLASS, 31},                            /*  47 ISN */
    {XXBYTE_YARV_ID_DEFINEMETHOD, 20},                           /*  48 IS */
    {XXBYTE_YARV_ID_DEFINESMETHOD, 20},                          /*  49 IS */
    {XXBYTE_YARV_ID_SEND, 15},                                   /*  50 CS */
    {XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, 1},                  /*  51 C */
    {XXBYTE_YARV_ID_OBJTOSTRING, 1},                             /*  52 C */
    {XXBYTE_YARV_ID_OPT_STR_FREEZE, 28},                         /*  53 VC */
    {XXBYTE_YARV_ID_OPT_NIL_P, 1},                               /*  54 C */
    {XXBYTE_YARV_ID_OPT_STR_UMINUS, 28},                         /*  55 VC */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_MAX, 7},                        /*  56 N */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_MIN, 7},                        /*  57 N */
    {XXBYTE_YARV_ID_INVOKESUPER, 15},                            /*  58 CS */
    {XXBYTE_YARV_ID_INVOKEBLOCK, 1},                             /*  59 C */
    {XXBYTE_YARV_ID_LEAVE, 0},                                   /*  60 - */
    {XXBYTE_YARV_ID_THROW, 7},                                   /*  61 N */
    {XXBYTE_YARV_ID_JUMP, 8},                                    /*  62 O */
    {XXBYTE_YARV_ID_BRANCHIF, 8},                                /*  63 O */
    {XXBYTE_YARV_ID_BRANCHUNLESS, 8},                            /*  64 O */
    {XXBYTE_YARV_ID_BRANCHNIL, 8},                               /*  65 O */
    {XXBYTE_YARV_ID_ONCE, 27},                                   /*  66 ST */
    {XXBYTE_YARV_ID_OPT_CASE_DISPATCH, 16},                      /*  67 HO */
    {XXBYTE_YARV_ID_OPT_PLUS, 1},                                /*  68 C */
    {XXBYTE_YARV_ID_OPT_MINUS, 1},                               /*  69 C */
    {XXBYTE_YARV_ID_OPT_MULT, 1},                                /*  70 C */
    {XXBYTE_YARV_ID_OPT_DIV, 1},                                 /*  71 C */
    {XXBYTE_YARV_ID_OPT_MOD, 1},                                 /*  72 C */
    {XXBYTE_YARV_ID_OPT_EQ, 1},                                  /*  73 C */
    {XXBYTE_YARV_ID_OPT_NEQ, 12},                                /*  74 CC */
    {XXBYTE_YARV_ID_OPT_LT, 1},                                  /*  75 C */
    {XXBYTE_YARV_ID_OPT_LE, 1},                                  /*  76 C */
    {XXBYTE_YARV_ID_OPT_GT, 1},                                  /*  77 C */
    {XXBYTE_YARV_ID_OPT_GE, 1},                                  /*  78 C */
    {XXBYTE_YARV_ID_OPT_LTLT, 1},                                /*  79 C */
    {XXBYTE_YARV_ID_OPT_AND, 1},                                 /*  80 C */
    {XXBYTE_YARV_ID_OPT_OR, 1},                                  /*  81 C */
    {XXBYTE_YARV_ID_OPT_AREF, 1},                                /*  82 C */
    {XXBYTE_YARV_ID_OPT_ASET, 1},                                /*  83 C */
    {XXBYTE_YARV_ID_OPT_ASET_WITH, 28},                          /*  84 VC */
    {XXBYTE_YARV_ID_OPT_AREF_WITH, 28},                          /*  85 VC */
    {XXBYTE_YARV_ID_OPT_LENGTH, 1},                              /*  86 C */
    {XXBYTE_YARV_ID_OPT_SIZE, 1},                                /*  87 C */
    {XXBYTE_YARV_ID_OPT_EMPTY_P, 1},                             /*  88 C */
    {XXBYTE_YARV_ID_OPT_SUCC, 1},                                /*  89 C */
    {XXBYTE_YARV_ID_OPT_NOT, 1},                                 /*  90 C */
    {XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 1},                        /*  91 C */
    {XXBYTE_YARV_ID_INVOKEBUILTIN, 9},                           /*  92 R */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE, 26},             /*  93 RN */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26},       /*  94 RN */
    {XXBYTE_YARV_ID_GETLOCAL_WC_0, 6},                           /*  95 L */
    {XXBYTE_YARV_ID_GETLOCAL_WC_1, 6},                           /*  96 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_0, 6},                           /*  97 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_1, 6},                           /*  98 L */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_, 0},                    /*  99 - */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_, 0},                    /* 100 - */
    {XXBYTE_YARV_ID_TRACE_NOP, 0},                               /* 101 - */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL, 22},                         /* 102 LN */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL, 22},                         /* 103 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAM, 22},                    /* 104 LN */
    {XXBYTE_YARV_ID_TRACE_SETBLOCKPARAM, 22},                    /* 105 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAMPROXY, 22},               /* 106 LN */
    {XXBYTE_YARV_ID_TRACE_GETSPECIAL, 24},                       /* 107 NN */
    {XXBYTE_YARV_ID_TRACE_SETSPECIAL, 7},                        /* 108 N */
    {XXBYTE_YARV_ID_TRACE_GETINSTANCEVARIABLE, 17},              /* 109 IA */
    {XXBYTE_YARV_ID_TRACE_SETINSTANCEVARIABLE, 17},              /* 110 IA */
    {XXBYTE_YARV_ID_TRACE_GETCLASSVARIABLE, 18},                 /* 111 IJ */
    {XXBYTE_YARV_ID_TRACE_SETCLASSVARIABLE, 18},                 /* 112 IJ */
    {XXBYTE_YARV_ID_TRACE_OPT_GETCONSTANT_PATH, 5},              /* 113 K */
    {XXBYTE_YARV_ID_TRACE_GETCONSTANT, 4},                       /* 114 I */
    {XXBYTE_YARV_ID_TRACE_SETCONSTANT, 4},                       /* 115 I */
    {XXBYTE_YARV_ID_TRACE_GETGLOBAL, 4},                         /* 116 I */
    {XXBYTE_YARV_ID_TRACE_SETGLOBAL, 4},                         /* 117 I */
    {XXBYTE_YARV_ID_TRACE_PUTNIL, 0},                            /* 118 - */
    {XXBYTE_YARV_ID_TRACE_PUTSELF, 0},                           /* 119 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT, 11},                        /* 120 V */
    {XXBYTE_YARV_ID_TRACE_PUTSPECIALOBJECT, 7},                  /* 121 N */
    {XXBYTE_YARV_ID_TRACE_PUTSTRING, 11},                        /* 122 V */
    {XXBYTE_YARV_ID_TRACE_CONCATSTRINGS, 7},                     /* 123 N */
    {XXBYTE_YARV_ID_TRACE_ANYTOSTRING, 0},                       /* 124 - */
    {XXBYTE_YARV_ID_TRACE_TOREGEXP, 24},                         /* 125 NN */
    {XXBYTE_YARV_ID_TRACE_INTERN, 0},                            /* 126 - */
    {XXBYTE_YARV_ID_TRACE_NEWARRAY, 7},                          /* 127 N */
    {XXBYTE_YARV_ID_TRACE_NEWARRAYKWSPLAT, 7},                   /* 128 N */
    {XXBYTE_YARV_ID_TRACE_DUPARRAY, 11},                         /* 129 V */
    {XXBYTE_YARV_ID_TRACE_DUPHASH, 11},                          /* 130 V */
    {XXBYTE_YARV_ID_TRACE_EXPANDARRAY, 24},                      /* 131 NN */
    {XXBYTE_YARV_ID_TRACE_CONCATARRAY, 0},                       /* 132 - */
    {XXBYTE_YARV_ID_TRACE_SPLATARRAY, 11},                       /* 133 V */
    {XXBYTE_YARV_ID_TRACE_NEWHASH, 7},                           /* 134 N */
    {XXBYTE_YARV_ID_TRACE_NEWRANGE, 7},                          /* 135 N */
    {XXBYTE_YARV_ID_TRACE_POP, 0},                               /* 136 - */
    {XXBYTE_YARV_ID_TRACE_DUP, 0},                               /* 137 - */
    {XXBYTE_YARV_ID_TRACE_DUPN, 7},                              /* 138 N */
    {XXBYTE_YARV_ID_TRACE_SWAP, 0},                              /* 139 - */
    {XXBYTE_YARV_ID_TRACE_OPT_REVERSE, 7},                       /* 140 N */
    {XXBYTE_YARV_ID_TRACE_TOPN, 7},                              /* 141 N */
    {XXBYTE_YARV_ID_TRACE_SETN, 7},                              /* 142 N */
    {XXBYTE_YARV_ID_TRACE_ADJUSTSTACK, 7},                       /* 143 N */
    {XXBYTE_YARV_ID_TRACE_DEFINED, 32},                          /* 144 NVV */
    {XXBYTE_YARV_ID_TRACE_CHECKMATCH, 7},                        /* 145 N */
    {XXBYTE_YARV_ID_TRACE_CHECKKEYWORD, 21},                     /* 146 LL */
    {XXBYTE_YARV_ID_TRACE_CHECKTYPE, 7},                         /* 147 N */
    {XXBYTE_YARV_ID_TRACE_DEFINECLASS, 31},                      /* 148 ISN */
    {XXBYTE_YARV_ID_TRACE_DEFINEMETHOD, 20},                     /* 149 IS */
    {XXBYTE_YARV_ID_TRACE_DEFINESMETHOD, 20},                    /* 150 IS */
    {XXBYTE_YARV_ID_TRACE_SEND, 15},                             /* 151 CS */
    {XXBYTE_YARV_ID_TRACE_OPT_SEND_WITHOUT_BLOCK, 1},            /* 152 C */
    {XXBYTE_YARV_ID_TRACE_OBJTOSTRING, 1},                       /* 153 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_FREEZE, 28},                   /* 154 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NIL_P, 1},                         /* 155 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_UMINUS, 28},                   /* 156 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MAX, 7},                  /* 157 N */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_MIN, 7},                  /* 158 N */
    {XXBYTE_YARV_ID_TRACE_INVOKESUPER, 15},                      /* 159 CS */
    {XXBYTE_YARV_ID_TRACE_INVOKEBLOCK, 1},                       /* 160 C */
    {XXBYTE_YARV_ID_TRACE_LEAVE, 0},                             /* 161 - */
    {XXBYTE_YARV_ID_TRACE_THROW, 7},                             /* 162 N */
    {XXBYTE_YARV_ID_TRACE_JUMP, 8},                              /* 163 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHIF, 8},                          /* 164 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHUNLESS, 8},                      /* 165 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHNIL, 8},                         /* 166 O */
    {XXBYTE_YARV_ID_TRACE_ONCE, 27},                             /* 167 ST */
    {XXBYTE_YARV_ID_TRACE_OPT_CASE_DISPATCH, 16},                /* 168 HO */
    {XXBYTE_YARV_ID_TRACE_OPT_PLUS, 1},                          /* 169 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MINUS, 1},                         /* 170 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MULT, 1},                          /* 171 C */
    {XXBYTE_YARV_ID_TRACE_OPT_DIV, 1},                           /* 172 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MOD, 1},                           /* 173 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EQ, 1},                            /* 174 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NEQ, 12},                          /* 175 CC */
    {XXBYTE_YARV_ID_TRACE_OPT_LT, 1},                            /* 176 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LE, 1},                            /* 177 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GT, 1},                            /* 178 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GE, 1},                            /* 179 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LTLT, 1},                          /* 180 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AND, 1},                           /* 181 C */
    {XXBYTE_YARV_ID_TRACE_OPT_OR, 1},                            /* 182 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF, 1},                          /* 183 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET, 1},                          /* 184 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET_WITH, 28},                    /* 185 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF_WITH, 28},                    /* 186 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_LENGTH, 1},                        /* 187 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SIZE, 1},                          /* 188 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EMPTY_P, 1},                       /* 189 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SUCC, 1},                          /* 190 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NOT, 1},                           /* 191 C */
    {XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH2, 1},                  /* 192 C */
    {XXBYTE_YARV_ID_TRACE_INVOKEBUILTIN, 9},                     /* 193 R */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE, 26},       /* 194 RN */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26}, /* 195 RN */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_0, 6},                     /* 196 L */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_1, 6},                     /* 197 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_0, 6},                     /* 198 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_1, 6},                     /* 199 L */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_0_, 0},              /* 200 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_1_, 0},              /* 201 - */
};

/* Ruby 3.3.12 (204 instructions) */
static const yarv_op yarv_ops_3_3[204] = {
    {XXBYTE_YARV_ID_NOP, 0},                                     /*   0 - */
    {XXBYTE_YARV_ID_GETLOCAL, 22},                               /*   1 LN */
    {XXBYTE_YARV_ID_SETLOCAL, 22},                               /*   2 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAM, 22},                          /*   3 LN */
    {XXBYTE_YARV_ID_SETBLOCKPARAM, 22},                          /*   4 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAMPROXY, 22},                     /*   5 LN */
    {XXBYTE_YARV_ID_GETSPECIAL, 24},                             /*   6 NN */
    {XXBYTE_YARV_ID_SETSPECIAL, 7},                              /*   7 N */
    {XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 17},                    /*   8 IA */
    {XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 17},                    /*   9 IA */
    {XXBYTE_YARV_ID_GETCLASSVARIABLE, 18},                       /*  10 IJ */
    {XXBYTE_YARV_ID_SETCLASSVARIABLE, 18},                       /*  11 IJ */
    {XXBYTE_YARV_ID_OPT_GETCONSTANT_PATH, 5},                    /*  12 K */
    {XXBYTE_YARV_ID_GETCONSTANT, 4},                             /*  13 I */
    {XXBYTE_YARV_ID_SETCONSTANT, 4},                             /*  14 I */
    {XXBYTE_YARV_ID_GETGLOBAL, 4},                               /*  15 I */
    {XXBYTE_YARV_ID_SETGLOBAL, 4},                               /*  16 I */
    {XXBYTE_YARV_ID_PUTNIL, 0},                                  /*  17 - */
    {XXBYTE_YARV_ID_PUTSELF, 0},                                 /*  18 - */
    {XXBYTE_YARV_ID_PUTOBJECT, 11},                              /*  19 V */
    {XXBYTE_YARV_ID_PUTSPECIALOBJECT, 7},                        /*  20 N */
    {XXBYTE_YARV_ID_PUTSTRING, 11},                              /*  21 V */
    {XXBYTE_YARV_ID_CONCATSTRINGS, 7},                           /*  22 N */
    {XXBYTE_YARV_ID_ANYTOSTRING, 0},                             /*  23 - */
    {XXBYTE_YARV_ID_TOREGEXP, 24},                               /*  24 NN */
    {XXBYTE_YARV_ID_INTERN, 0},                                  /*  25 - */
    {XXBYTE_YARV_ID_NEWARRAY, 7},                                /*  26 N */
    {XXBYTE_YARV_ID_NEWARRAYKWSPLAT, 7},                         /*  27 N */
    {XXBYTE_YARV_ID_DUPARRAY, 11},                               /*  28 V */
    {XXBYTE_YARV_ID_DUPHASH, 11},                                /*  29 V */
    {XXBYTE_YARV_ID_EXPANDARRAY, 24},                            /*  30 NN */
    {XXBYTE_YARV_ID_CONCATARRAY, 0},                             /*  31 - */
    {XXBYTE_YARV_ID_SPLATARRAY, 11},                             /*  32 V */
    {XXBYTE_YARV_ID_SPLATKW, 0},                                 /*  33 - */
    {XXBYTE_YARV_ID_NEWHASH, 7},                                 /*  34 N */
    {XXBYTE_YARV_ID_NEWRANGE, 7},                                /*  35 N */
    {XXBYTE_YARV_ID_POP, 0},                                     /*  36 - */
    {XXBYTE_YARV_ID_DUP, 0},                                     /*  37 - */
    {XXBYTE_YARV_ID_DUPN, 7},                                    /*  38 N */
    {XXBYTE_YARV_ID_SWAP, 0},                                    /*  39 - */
    {XXBYTE_YARV_ID_OPT_REVERSE, 7},                             /*  40 N */
    {XXBYTE_YARV_ID_TOPN, 7},                                    /*  41 N */
    {XXBYTE_YARV_ID_SETN, 7},                                    /*  42 N */
    {XXBYTE_YARV_ID_ADJUSTSTACK, 7},                             /*  43 N */
    {XXBYTE_YARV_ID_DEFINED, 32},                                /*  44 NVV */
    {XXBYTE_YARV_ID_DEFINEDIVAR, 30},                            /*  45 IAV */
    {XXBYTE_YARV_ID_CHECKMATCH, 7},                              /*  46 N */
    {XXBYTE_YARV_ID_CHECKKEYWORD, 21},                           /*  47 LL */
    {XXBYTE_YARV_ID_CHECKTYPE, 7},                               /*  48 N */
    {XXBYTE_YARV_ID_DEFINECLASS, 31},                            /*  49 ISN */
    {XXBYTE_YARV_ID_DEFINEMETHOD, 20},                           /*  50 IS */
    {XXBYTE_YARV_ID_DEFINESMETHOD, 20},                          /*  51 IS */
    {XXBYTE_YARV_ID_SEND, 15},                                   /*  52 CS */
    {XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, 1},                  /*  53 C */
    {XXBYTE_YARV_ID_OBJTOSTRING, 1},                             /*  54 C */
    {XXBYTE_YARV_ID_OPT_STR_FREEZE, 28},                         /*  55 VC */
    {XXBYTE_YARV_ID_OPT_NIL_P, 1},                               /*  56 C */
    {XXBYTE_YARV_ID_OPT_STR_UMINUS, 28},                         /*  57 VC */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_SEND, 23},                      /*  58 NI */
    {XXBYTE_YARV_ID_INVOKESUPER, 15},                            /*  59 CS */
    {XXBYTE_YARV_ID_INVOKEBLOCK, 1},                             /*  60 C */
    {XXBYTE_YARV_ID_LEAVE, 0},                                   /*  61 - */
    {XXBYTE_YARV_ID_THROW, 7},                                   /*  62 N */
    {XXBYTE_YARV_ID_JUMP, 8},                                    /*  63 O */
    {XXBYTE_YARV_ID_BRANCHIF, 8},                                /*  64 O */
    {XXBYTE_YARV_ID_BRANCHUNLESS, 8},                            /*  65 O */
    {XXBYTE_YARV_ID_BRANCHNIL, 8},                               /*  66 O */
    {XXBYTE_YARV_ID_ONCE, 27},                                   /*  67 ST */
    {XXBYTE_YARV_ID_OPT_CASE_DISPATCH, 16},                      /*  68 HO */
    {XXBYTE_YARV_ID_OPT_PLUS, 1},                                /*  69 C */
    {XXBYTE_YARV_ID_OPT_MINUS, 1},                               /*  70 C */
    {XXBYTE_YARV_ID_OPT_MULT, 1},                                /*  71 C */
    {XXBYTE_YARV_ID_OPT_DIV, 1},                                 /*  72 C */
    {XXBYTE_YARV_ID_OPT_MOD, 1},                                 /*  73 C */
    {XXBYTE_YARV_ID_OPT_EQ, 1},                                  /*  74 C */
    {XXBYTE_YARV_ID_OPT_NEQ, 12},                                /*  75 CC */
    {XXBYTE_YARV_ID_OPT_LT, 1},                                  /*  76 C */
    {XXBYTE_YARV_ID_OPT_LE, 1},                                  /*  77 C */
    {XXBYTE_YARV_ID_OPT_GT, 1},                                  /*  78 C */
    {XXBYTE_YARV_ID_OPT_GE, 1},                                  /*  79 C */
    {XXBYTE_YARV_ID_OPT_LTLT, 1},                                /*  80 C */
    {XXBYTE_YARV_ID_OPT_AND, 1},                                 /*  81 C */
    {XXBYTE_YARV_ID_OPT_OR, 1},                                  /*  82 C */
    {XXBYTE_YARV_ID_OPT_AREF, 1},                                /*  83 C */
    {XXBYTE_YARV_ID_OPT_ASET, 1},                                /*  84 C */
    {XXBYTE_YARV_ID_OPT_ASET_WITH, 28},                          /*  85 VC */
    {XXBYTE_YARV_ID_OPT_AREF_WITH, 28},                          /*  86 VC */
    {XXBYTE_YARV_ID_OPT_LENGTH, 1},                              /*  87 C */
    {XXBYTE_YARV_ID_OPT_SIZE, 1},                                /*  88 C */
    {XXBYTE_YARV_ID_OPT_EMPTY_P, 1},                             /*  89 C */
    {XXBYTE_YARV_ID_OPT_SUCC, 1},                                /*  90 C */
    {XXBYTE_YARV_ID_OPT_NOT, 1},                                 /*  91 C */
    {XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 1},                        /*  92 C */
    {XXBYTE_YARV_ID_INVOKEBUILTIN, 9},                           /*  93 R */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE, 26},             /*  94 RN */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26},       /*  95 RN */
    {XXBYTE_YARV_ID_GETLOCAL_WC_0, 6},                           /*  96 L */
    {XXBYTE_YARV_ID_GETLOCAL_WC_1, 6},                           /*  97 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_0, 6},                           /*  98 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_1, 6},                           /*  99 L */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_, 0},                    /* 100 - */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_, 0},                    /* 101 - */
    {XXBYTE_YARV_ID_TRACE_NOP, 0},                               /* 102 - */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL, 22},                         /* 103 LN */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL, 22},                         /* 104 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAM, 22},                    /* 105 LN */
    {XXBYTE_YARV_ID_TRACE_SETBLOCKPARAM, 22},                    /* 106 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAMPROXY, 22},               /* 107 LN */
    {XXBYTE_YARV_ID_TRACE_GETSPECIAL, 24},                       /* 108 NN */
    {XXBYTE_YARV_ID_TRACE_SETSPECIAL, 7},                        /* 109 N */
    {XXBYTE_YARV_ID_TRACE_GETINSTANCEVARIABLE, 17},              /* 110 IA */
    {XXBYTE_YARV_ID_TRACE_SETINSTANCEVARIABLE, 17},              /* 111 IA */
    {XXBYTE_YARV_ID_TRACE_GETCLASSVARIABLE, 18},                 /* 112 IJ */
    {XXBYTE_YARV_ID_TRACE_SETCLASSVARIABLE, 18},                 /* 113 IJ */
    {XXBYTE_YARV_ID_TRACE_OPT_GETCONSTANT_PATH, 5},              /* 114 K */
    {XXBYTE_YARV_ID_TRACE_GETCONSTANT, 4},                       /* 115 I */
    {XXBYTE_YARV_ID_TRACE_SETCONSTANT, 4},                       /* 116 I */
    {XXBYTE_YARV_ID_TRACE_GETGLOBAL, 4},                         /* 117 I */
    {XXBYTE_YARV_ID_TRACE_SETGLOBAL, 4},                         /* 118 I */
    {XXBYTE_YARV_ID_TRACE_PUTNIL, 0},                            /* 119 - */
    {XXBYTE_YARV_ID_TRACE_PUTSELF, 0},                           /* 120 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT, 11},                        /* 121 V */
    {XXBYTE_YARV_ID_TRACE_PUTSPECIALOBJECT, 7},                  /* 122 N */
    {XXBYTE_YARV_ID_TRACE_PUTSTRING, 11},                        /* 123 V */
    {XXBYTE_YARV_ID_TRACE_CONCATSTRINGS, 7},                     /* 124 N */
    {XXBYTE_YARV_ID_TRACE_ANYTOSTRING, 0},                       /* 125 - */
    {XXBYTE_YARV_ID_TRACE_TOREGEXP, 24},                         /* 126 NN */
    {XXBYTE_YARV_ID_TRACE_INTERN, 0},                            /* 127 - */
    {XXBYTE_YARV_ID_TRACE_NEWARRAY, 7},                          /* 128 N */
    {XXBYTE_YARV_ID_TRACE_NEWARRAYKWSPLAT, 7},                   /* 129 N */
    {XXBYTE_YARV_ID_TRACE_DUPARRAY, 11},                         /* 130 V */
    {XXBYTE_YARV_ID_TRACE_DUPHASH, 11},                          /* 131 V */
    {XXBYTE_YARV_ID_TRACE_EXPANDARRAY, 24},                      /* 132 NN */
    {XXBYTE_YARV_ID_TRACE_CONCATARRAY, 0},                       /* 133 - */
    {XXBYTE_YARV_ID_TRACE_SPLATARRAY, 11},                       /* 134 V */
    {XXBYTE_YARV_ID_TRACE_SPLATKW, 0},                           /* 135 - */
    {XXBYTE_YARV_ID_TRACE_NEWHASH, 7},                           /* 136 N */
    {XXBYTE_YARV_ID_TRACE_NEWRANGE, 7},                          /* 137 N */
    {XXBYTE_YARV_ID_TRACE_POP, 0},                               /* 138 - */
    {XXBYTE_YARV_ID_TRACE_DUP, 0},                               /* 139 - */
    {XXBYTE_YARV_ID_TRACE_DUPN, 7},                              /* 140 N */
    {XXBYTE_YARV_ID_TRACE_SWAP, 0},                              /* 141 - */
    {XXBYTE_YARV_ID_TRACE_OPT_REVERSE, 7},                       /* 142 N */
    {XXBYTE_YARV_ID_TRACE_TOPN, 7},                              /* 143 N */
    {XXBYTE_YARV_ID_TRACE_SETN, 7},                              /* 144 N */
    {XXBYTE_YARV_ID_TRACE_ADJUSTSTACK, 7},                       /* 145 N */
    {XXBYTE_YARV_ID_TRACE_DEFINED, 32},                          /* 146 NVV */
    {XXBYTE_YARV_ID_TRACE_DEFINEDIVAR, 30},                      /* 147 IAV */
    {XXBYTE_YARV_ID_TRACE_CHECKMATCH, 7},                        /* 148 N */
    {XXBYTE_YARV_ID_TRACE_CHECKKEYWORD, 21},                     /* 149 LL */
    {XXBYTE_YARV_ID_TRACE_CHECKTYPE, 7},                         /* 150 N */
    {XXBYTE_YARV_ID_TRACE_DEFINECLASS, 31},                      /* 151 ISN */
    {XXBYTE_YARV_ID_TRACE_DEFINEMETHOD, 20},                     /* 152 IS */
    {XXBYTE_YARV_ID_TRACE_DEFINESMETHOD, 20},                    /* 153 IS */
    {XXBYTE_YARV_ID_TRACE_SEND, 15},                             /* 154 CS */
    {XXBYTE_YARV_ID_TRACE_OPT_SEND_WITHOUT_BLOCK, 1},            /* 155 C */
    {XXBYTE_YARV_ID_TRACE_OBJTOSTRING, 1},                       /* 156 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_FREEZE, 28},                   /* 157 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NIL_P, 1},                         /* 158 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_UMINUS, 28},                   /* 159 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_SEND, 23},                /* 160 NI */
    {XXBYTE_YARV_ID_TRACE_INVOKESUPER, 15},                      /* 161 CS */
    {XXBYTE_YARV_ID_TRACE_INVOKEBLOCK, 1},                       /* 162 C */
    {XXBYTE_YARV_ID_TRACE_LEAVE, 0},                             /* 163 - */
    {XXBYTE_YARV_ID_TRACE_THROW, 7},                             /* 164 N */
    {XXBYTE_YARV_ID_TRACE_JUMP, 8},                              /* 165 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHIF, 8},                          /* 166 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHUNLESS, 8},                      /* 167 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHNIL, 8},                         /* 168 O */
    {XXBYTE_YARV_ID_TRACE_ONCE, 27},                             /* 169 ST */
    {XXBYTE_YARV_ID_TRACE_OPT_CASE_DISPATCH, 16},                /* 170 HO */
    {XXBYTE_YARV_ID_TRACE_OPT_PLUS, 1},                          /* 171 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MINUS, 1},                         /* 172 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MULT, 1},                          /* 173 C */
    {XXBYTE_YARV_ID_TRACE_OPT_DIV, 1},                           /* 174 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MOD, 1},                           /* 175 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EQ, 1},                            /* 176 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NEQ, 12},                          /* 177 CC */
    {XXBYTE_YARV_ID_TRACE_OPT_LT, 1},                            /* 178 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LE, 1},                            /* 179 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GT, 1},                            /* 180 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GE, 1},                            /* 181 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LTLT, 1},                          /* 182 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AND, 1},                           /* 183 C */
    {XXBYTE_YARV_ID_TRACE_OPT_OR, 1},                            /* 184 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF, 1},                          /* 185 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET, 1},                          /* 186 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET_WITH, 28},                    /* 187 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF_WITH, 28},                    /* 188 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_LENGTH, 1},                        /* 189 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SIZE, 1},                          /* 190 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EMPTY_P, 1},                       /* 191 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SUCC, 1},                          /* 192 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NOT, 1},                           /* 193 C */
    {XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH2, 1},                  /* 194 C */
    {XXBYTE_YARV_ID_TRACE_INVOKEBUILTIN, 9},                     /* 195 R */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE, 26},       /* 196 RN */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26}, /* 197 RN */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_0, 6},                     /* 198 L */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_1, 6},                     /* 199 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_0, 6},                     /* 200 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_1, 6},                     /* 201 L */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_0_, 0},              /* 202 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_1_, 0},              /* 203 - */
};

/* Ruby 3.4.11 (220 instructions) */
static const yarv_op yarv_ops_3_4[220] = {
    {XXBYTE_YARV_ID_NOP, 0},                                     /*   0 - */
    {XXBYTE_YARV_ID_GETLOCAL, 22},                               /*   1 LN */
    {XXBYTE_YARV_ID_SETLOCAL, 22},                               /*   2 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAM, 22},                          /*   3 LN */
    {XXBYTE_YARV_ID_SETBLOCKPARAM, 22},                          /*   4 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAMPROXY, 22},                     /*   5 LN */
    {XXBYTE_YARV_ID_GETSPECIAL, 24},                             /*   6 NN */
    {XXBYTE_YARV_ID_SETSPECIAL, 7},                              /*   7 N */
    {XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 17},                    /*   8 IA */
    {XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 17},                    /*   9 IA */
    {XXBYTE_YARV_ID_GETCLASSVARIABLE, 18},                       /*  10 IJ */
    {XXBYTE_YARV_ID_SETCLASSVARIABLE, 18},                       /*  11 IJ */
    {XXBYTE_YARV_ID_OPT_GETCONSTANT_PATH, 5},                    /*  12 K */
    {XXBYTE_YARV_ID_GETCONSTANT, 4},                             /*  13 I */
    {XXBYTE_YARV_ID_SETCONSTANT, 4},                             /*  14 I */
    {XXBYTE_YARV_ID_GETGLOBAL, 4},                               /*  15 I */
    {XXBYTE_YARV_ID_SETGLOBAL, 4},                               /*  16 I */
    {XXBYTE_YARV_ID_PUTNIL, 0},                                  /*  17 - */
    {XXBYTE_YARV_ID_PUTSELF, 0},                                 /*  18 - */
    {XXBYTE_YARV_ID_PUTOBJECT, 11},                              /*  19 V */
    {XXBYTE_YARV_ID_PUTSPECIALOBJECT, 7},                        /*  20 N */
    {XXBYTE_YARV_ID_PUTSTRING, 11},                              /*  21 V */
    {XXBYTE_YARV_ID_PUTCHILLEDSTRING, 11},                       /*  22 V */
    {XXBYTE_YARV_ID_CONCATSTRINGS, 7},                           /*  23 N */
    {XXBYTE_YARV_ID_ANYTOSTRING, 0},                             /*  24 - */
    {XXBYTE_YARV_ID_TOREGEXP, 24},                               /*  25 NN */
    {XXBYTE_YARV_ID_INTERN, 0},                                  /*  26 - */
    {XXBYTE_YARV_ID_NEWARRAY, 7},                                /*  27 N */
    {XXBYTE_YARV_ID_PUSHTOARRAYKWSPLAT, 0},                      /*  28 - */
    {XXBYTE_YARV_ID_DUPARRAY, 11},                               /*  29 V */
    {XXBYTE_YARV_ID_DUPHASH, 11},                                /*  30 V */
    {XXBYTE_YARV_ID_EXPANDARRAY, 24},                            /*  31 NN */
    {XXBYTE_YARV_ID_CONCATARRAY, 0},                             /*  32 - */
    {XXBYTE_YARV_ID_CONCATTOARRAY, 0},                           /*  33 - */
    {XXBYTE_YARV_ID_PUSHTOARRAY, 7},                             /*  34 N */
    {XXBYTE_YARV_ID_SPLATARRAY, 11},                             /*  35 V */
    {XXBYTE_YARV_ID_SPLATKW, 0},                                 /*  36 - */
    {XXBYTE_YARV_ID_NEWHASH, 7},                                 /*  37 N */
    {XXBYTE_YARV_ID_NEWRANGE, 7},                                /*  38 N */
    {XXBYTE_YARV_ID_POP, 0},                                     /*  39 - */
    {XXBYTE_YARV_ID_DUP, 0},                                     /*  40 - */
    {XXBYTE_YARV_ID_DUPN, 7},                                    /*  41 N */
    {XXBYTE_YARV_ID_SWAP, 0},                                    /*  42 - */
    {XXBYTE_YARV_ID_OPT_REVERSE, 7},                             /*  43 N */
    {XXBYTE_YARV_ID_TOPN, 7},                                    /*  44 N */
    {XXBYTE_YARV_ID_SETN, 7},                                    /*  45 N */
    {XXBYTE_YARV_ID_ADJUSTSTACK, 7},                             /*  46 N */
    {XXBYTE_YARV_ID_DEFINED, 32},                                /*  47 NVV */
    {XXBYTE_YARV_ID_DEFINEDIVAR, 30},                            /*  48 IAV */
    {XXBYTE_YARV_ID_CHECKMATCH, 7},                              /*  49 N */
    {XXBYTE_YARV_ID_CHECKKEYWORD, 21},                           /*  50 LL */
    {XXBYTE_YARV_ID_CHECKTYPE, 7},                               /*  51 N */
    {XXBYTE_YARV_ID_DEFINECLASS, 31},                            /*  52 ISN */
    {XXBYTE_YARV_ID_DEFINEMETHOD, 20},                           /*  53 IS */
    {XXBYTE_YARV_ID_DEFINESMETHOD, 20},                          /*  54 IS */
    {XXBYTE_YARV_ID_SEND, 15},                                   /*  55 CS */
    {XXBYTE_YARV_ID_SENDFORWARD, 15},                            /*  56 CS */
    {XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, 1},                  /*  57 C */
    {XXBYTE_YARV_ID_OBJTOSTRING, 1},                             /*  58 C */
    {XXBYTE_YARV_ID_OPT_ARY_FREEZE, 28},                         /*  59 VC */
    {XXBYTE_YARV_ID_OPT_HASH_FREEZE, 28},                        /*  60 VC */
    {XXBYTE_YARV_ID_OPT_STR_FREEZE, 28},                         /*  61 VC */
    {XXBYTE_YARV_ID_OPT_NIL_P, 1},                               /*  62 C */
    {XXBYTE_YARV_ID_OPT_STR_UMINUS, 28},                         /*  63 VC */
    {XXBYTE_YARV_ID_OPT_DUPARRAY_SEND, 34},                      /*  64 VIN */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_SEND, 24},                      /*  65 NN */
    {XXBYTE_YARV_ID_INVOKESUPER, 15},                            /*  66 CS */
    {XXBYTE_YARV_ID_INVOKESUPERFORWARD, 15},                     /*  67 CS */
    {XXBYTE_YARV_ID_INVOKEBLOCK, 1},                             /*  68 C */
    {XXBYTE_YARV_ID_LEAVE, 0},                                   /*  69 - */
    {XXBYTE_YARV_ID_THROW, 7},                                   /*  70 N */
    {XXBYTE_YARV_ID_JUMP, 8},                                    /*  71 O */
    {XXBYTE_YARV_ID_BRANCHIF, 8},                                /*  72 O */
    {XXBYTE_YARV_ID_BRANCHUNLESS, 8},                            /*  73 O */
    {XXBYTE_YARV_ID_BRANCHNIL, 8},                               /*  74 O */
    {XXBYTE_YARV_ID_ONCE, 27},                                   /*  75 ST */
    {XXBYTE_YARV_ID_OPT_CASE_DISPATCH, 16},                      /*  76 HO */
    {XXBYTE_YARV_ID_OPT_PLUS, 1},                                /*  77 C */
    {XXBYTE_YARV_ID_OPT_MINUS, 1},                               /*  78 C */
    {XXBYTE_YARV_ID_OPT_MULT, 1},                                /*  79 C */
    {XXBYTE_YARV_ID_OPT_DIV, 1},                                 /*  80 C */
    {XXBYTE_YARV_ID_OPT_MOD, 1},                                 /*  81 C */
    {XXBYTE_YARV_ID_OPT_EQ, 1},                                  /*  82 C */
    {XXBYTE_YARV_ID_OPT_NEQ, 12},                                /*  83 CC */
    {XXBYTE_YARV_ID_OPT_LT, 1},                                  /*  84 C */
    {XXBYTE_YARV_ID_OPT_LE, 1},                                  /*  85 C */
    {XXBYTE_YARV_ID_OPT_GT, 1},                                  /*  86 C */
    {XXBYTE_YARV_ID_OPT_GE, 1},                                  /*  87 C */
    {XXBYTE_YARV_ID_OPT_LTLT, 1},                                /*  88 C */
    {XXBYTE_YARV_ID_OPT_AND, 1},                                 /*  89 C */
    {XXBYTE_YARV_ID_OPT_OR, 1},                                  /*  90 C */
    {XXBYTE_YARV_ID_OPT_AREF, 1},                                /*  91 C */
    {XXBYTE_YARV_ID_OPT_ASET, 1},                                /*  92 C */
    {XXBYTE_YARV_ID_OPT_ASET_WITH, 28},                          /*  93 VC */
    {XXBYTE_YARV_ID_OPT_AREF_WITH, 28},                          /*  94 VC */
    {XXBYTE_YARV_ID_OPT_LENGTH, 1},                              /*  95 C */
    {XXBYTE_YARV_ID_OPT_SIZE, 1},                                /*  96 C */
    {XXBYTE_YARV_ID_OPT_EMPTY_P, 1},                             /*  97 C */
    {XXBYTE_YARV_ID_OPT_SUCC, 1},                                /*  98 C */
    {XXBYTE_YARV_ID_OPT_NOT, 1},                                 /*  99 C */
    {XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 1},                        /* 100 C */
    {XXBYTE_YARV_ID_INVOKEBUILTIN, 9},                           /* 101 R */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE, 26},             /* 102 RN */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26},       /* 103 RN */
    {XXBYTE_YARV_ID_GETLOCAL_WC_0, 6},                           /* 104 L */
    {XXBYTE_YARV_ID_GETLOCAL_WC_1, 6},                           /* 105 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_0, 6},                           /* 106 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_1, 6},                           /* 107 L */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_, 0},                    /* 108 - */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_, 0},                    /* 109 - */
    {XXBYTE_YARV_ID_TRACE_NOP, 0},                               /* 110 - */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL, 22},                         /* 111 LN */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL, 22},                         /* 112 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAM, 22},                    /* 113 LN */
    {XXBYTE_YARV_ID_TRACE_SETBLOCKPARAM, 22},                    /* 114 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAMPROXY, 22},               /* 115 LN */
    {XXBYTE_YARV_ID_TRACE_GETSPECIAL, 24},                       /* 116 NN */
    {XXBYTE_YARV_ID_TRACE_SETSPECIAL, 7},                        /* 117 N */
    {XXBYTE_YARV_ID_TRACE_GETINSTANCEVARIABLE, 17},              /* 118 IA */
    {XXBYTE_YARV_ID_TRACE_SETINSTANCEVARIABLE, 17},              /* 119 IA */
    {XXBYTE_YARV_ID_TRACE_GETCLASSVARIABLE, 18},                 /* 120 IJ */
    {XXBYTE_YARV_ID_TRACE_SETCLASSVARIABLE, 18},                 /* 121 IJ */
    {XXBYTE_YARV_ID_TRACE_OPT_GETCONSTANT_PATH, 5},              /* 122 K */
    {XXBYTE_YARV_ID_TRACE_GETCONSTANT, 4},                       /* 123 I */
    {XXBYTE_YARV_ID_TRACE_SETCONSTANT, 4},                       /* 124 I */
    {XXBYTE_YARV_ID_TRACE_GETGLOBAL, 4},                         /* 125 I */
    {XXBYTE_YARV_ID_TRACE_SETGLOBAL, 4},                         /* 126 I */
    {XXBYTE_YARV_ID_TRACE_PUTNIL, 0},                            /* 127 - */
    {XXBYTE_YARV_ID_TRACE_PUTSELF, 0},                           /* 128 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT, 11},                        /* 129 V */
    {XXBYTE_YARV_ID_TRACE_PUTSPECIALOBJECT, 7},                  /* 130 N */
    {XXBYTE_YARV_ID_TRACE_PUTSTRING, 11},                        /* 131 V */
    {XXBYTE_YARV_ID_TRACE_PUTCHILLEDSTRING, 11},                 /* 132 V */
    {XXBYTE_YARV_ID_TRACE_CONCATSTRINGS, 7},                     /* 133 N */
    {XXBYTE_YARV_ID_TRACE_ANYTOSTRING, 0},                       /* 134 - */
    {XXBYTE_YARV_ID_TRACE_TOREGEXP, 24},                         /* 135 NN */
    {XXBYTE_YARV_ID_TRACE_INTERN, 0},                            /* 136 - */
    {XXBYTE_YARV_ID_TRACE_NEWARRAY, 7},                          /* 137 N */
    {XXBYTE_YARV_ID_TRACE_PUSHTOARRAYKWSPLAT, 0},                /* 138 - */
    {XXBYTE_YARV_ID_TRACE_DUPARRAY, 11},                         /* 139 V */
    {XXBYTE_YARV_ID_TRACE_DUPHASH, 11},                          /* 140 V */
    {XXBYTE_YARV_ID_TRACE_EXPANDARRAY, 24},                      /* 141 NN */
    {XXBYTE_YARV_ID_TRACE_CONCATARRAY, 0},                       /* 142 - */
    {XXBYTE_YARV_ID_TRACE_CONCATTOARRAY, 0},                     /* 143 - */
    {XXBYTE_YARV_ID_TRACE_PUSHTOARRAY, 7},                       /* 144 N */
    {XXBYTE_YARV_ID_TRACE_SPLATARRAY, 11},                       /* 145 V */
    {XXBYTE_YARV_ID_TRACE_SPLATKW, 0},                           /* 146 - */
    {XXBYTE_YARV_ID_TRACE_NEWHASH, 7},                           /* 147 N */
    {XXBYTE_YARV_ID_TRACE_NEWRANGE, 7},                          /* 148 N */
    {XXBYTE_YARV_ID_TRACE_POP, 0},                               /* 149 - */
    {XXBYTE_YARV_ID_TRACE_DUP, 0},                               /* 150 - */
    {XXBYTE_YARV_ID_TRACE_DUPN, 7},                              /* 151 N */
    {XXBYTE_YARV_ID_TRACE_SWAP, 0},                              /* 152 - */
    {XXBYTE_YARV_ID_TRACE_OPT_REVERSE, 7},                       /* 153 N */
    {XXBYTE_YARV_ID_TRACE_TOPN, 7},                              /* 154 N */
    {XXBYTE_YARV_ID_TRACE_SETN, 7},                              /* 155 N */
    {XXBYTE_YARV_ID_TRACE_ADJUSTSTACK, 7},                       /* 156 N */
    {XXBYTE_YARV_ID_TRACE_DEFINED, 32},                          /* 157 NVV */
    {XXBYTE_YARV_ID_TRACE_DEFINEDIVAR, 30},                      /* 158 IAV */
    {XXBYTE_YARV_ID_TRACE_CHECKMATCH, 7},                        /* 159 N */
    {XXBYTE_YARV_ID_TRACE_CHECKKEYWORD, 21},                     /* 160 LL */
    {XXBYTE_YARV_ID_TRACE_CHECKTYPE, 7},                         /* 161 N */
    {XXBYTE_YARV_ID_TRACE_DEFINECLASS, 31},                      /* 162 ISN */
    {XXBYTE_YARV_ID_TRACE_DEFINEMETHOD, 20},                     /* 163 IS */
    {XXBYTE_YARV_ID_TRACE_DEFINESMETHOD, 20},                    /* 164 IS */
    {XXBYTE_YARV_ID_TRACE_SEND, 15},                             /* 165 CS */
    {XXBYTE_YARV_ID_TRACE_SENDFORWARD, 15},                      /* 166 CS */
    {XXBYTE_YARV_ID_TRACE_OPT_SEND_WITHOUT_BLOCK, 1},            /* 167 C */
    {XXBYTE_YARV_ID_TRACE_OBJTOSTRING, 1},                       /* 168 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ARY_FREEZE, 28},                   /* 169 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_HASH_FREEZE, 28},                  /* 170 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_FREEZE, 28},                   /* 171 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NIL_P, 1},                         /* 172 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_UMINUS, 28},                   /* 173 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_DUPARRAY_SEND, 34},                /* 174 VIN */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_SEND, 24},                /* 175 NN */
    {XXBYTE_YARV_ID_TRACE_INVOKESUPER, 15},                      /* 176 CS */
    {XXBYTE_YARV_ID_TRACE_INVOKESUPERFORWARD, 15},               /* 177 CS */
    {XXBYTE_YARV_ID_TRACE_INVOKEBLOCK, 1},                       /* 178 C */
    {XXBYTE_YARV_ID_TRACE_LEAVE, 0},                             /* 179 - */
    {XXBYTE_YARV_ID_TRACE_THROW, 7},                             /* 180 N */
    {XXBYTE_YARV_ID_TRACE_JUMP, 8},                              /* 181 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHIF, 8},                          /* 182 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHUNLESS, 8},                      /* 183 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHNIL, 8},                         /* 184 O */
    {XXBYTE_YARV_ID_TRACE_ONCE, 27},                             /* 185 ST */
    {XXBYTE_YARV_ID_TRACE_OPT_CASE_DISPATCH, 16},                /* 186 HO */
    {XXBYTE_YARV_ID_TRACE_OPT_PLUS, 1},                          /* 187 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MINUS, 1},                         /* 188 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MULT, 1},                          /* 189 C */
    {XXBYTE_YARV_ID_TRACE_OPT_DIV, 1},                           /* 190 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MOD, 1},                           /* 191 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EQ, 1},                            /* 192 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NEQ, 12},                          /* 193 CC */
    {XXBYTE_YARV_ID_TRACE_OPT_LT, 1},                            /* 194 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LE, 1},                            /* 195 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GT, 1},                            /* 196 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GE, 1},                            /* 197 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LTLT, 1},                          /* 198 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AND, 1},                           /* 199 C */
    {XXBYTE_YARV_ID_TRACE_OPT_OR, 1},                            /* 200 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF, 1},                          /* 201 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET, 1},                          /* 202 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET_WITH, 28},                    /* 203 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF_WITH, 28},                    /* 204 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_LENGTH, 1},                        /* 205 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SIZE, 1},                          /* 206 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EMPTY_P, 1},                       /* 207 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SUCC, 1},                          /* 208 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NOT, 1},                           /* 209 C */
    {XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH2, 1},                  /* 210 C */
    {XXBYTE_YARV_ID_TRACE_INVOKEBUILTIN, 9},                     /* 211 R */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE, 26},       /* 212 RN */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26}, /* 213 RN */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_0, 6},                     /* 214 L */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_1, 6},                     /* 215 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_0, 6},                     /* 216 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_1, 6},                     /* 217 L */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_0_, 0},              /* 218 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_1_, 0},              /* 219 - */
};

/* Ruby 4.0.7 (248 instructions) */
static const yarv_op yarv_ops_4_0[248] = {
    {XXBYTE_YARV_ID_NOP, 0},                                     /*   0 - */
    {XXBYTE_YARV_ID_GETLOCAL, 22},                               /*   1 LN */
    {XXBYTE_YARV_ID_SETLOCAL, 22},                               /*   2 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAM, 22},                          /*   3 LN */
    {XXBYTE_YARV_ID_SETBLOCKPARAM, 22},                          /*   4 LN */
    {XXBYTE_YARV_ID_GETBLOCKPARAMPROXY, 22},                     /*   5 LN */
    {XXBYTE_YARV_ID_GETSPECIAL, 24},                             /*   6 NN */
    {XXBYTE_YARV_ID_SETSPECIAL, 7},                              /*   7 N */
    {XXBYTE_YARV_ID_GETINSTANCEVARIABLE, 17},                    /*   8 IA */
    {XXBYTE_YARV_ID_SETINSTANCEVARIABLE, 17},                    /*   9 IA */
    {XXBYTE_YARV_ID_GETCLASSVARIABLE, 18},                       /*  10 IJ */
    {XXBYTE_YARV_ID_SETCLASSVARIABLE, 18},                       /*  11 IJ */
    {XXBYTE_YARV_ID_OPT_GETCONSTANT_PATH, 5},                    /*  12 K */
    {XXBYTE_YARV_ID_GETCONSTANT, 4},                             /*  13 I */
    {XXBYTE_YARV_ID_SETCONSTANT, 4},                             /*  14 I */
    {XXBYTE_YARV_ID_GETGLOBAL, 4},                               /*  15 I */
    {XXBYTE_YARV_ID_SETGLOBAL, 4},                               /*  16 I */
    {XXBYTE_YARV_ID_PUTNIL, 0},                                  /*  17 - */
    {XXBYTE_YARV_ID_PUTSELF, 0},                                 /*  18 - */
    {XXBYTE_YARV_ID_PUTOBJECT, 11},                              /*  19 V */
    {XXBYTE_YARV_ID_PUTSPECIALOBJECT, 7},                        /*  20 N */
    {XXBYTE_YARV_ID_PUTSTRING, 11},                              /*  21 V */
    {XXBYTE_YARV_ID_PUTCHILLEDSTRING, 11},                       /*  22 V */
    {XXBYTE_YARV_ID_CONCATSTRINGS, 7},                           /*  23 N */
    {XXBYTE_YARV_ID_ANYTOSTRING, 0},                             /*  24 - */
    {XXBYTE_YARV_ID_TOREGEXP, 24},                               /*  25 NN */
    {XXBYTE_YARV_ID_INTERN, 0},                                  /*  26 - */
    {XXBYTE_YARV_ID_NEWARRAY, 7},                                /*  27 N */
    {XXBYTE_YARV_ID_PUSHTOARRAYKWSPLAT, 0},                      /*  28 - */
    {XXBYTE_YARV_ID_DUPARRAY, 11},                               /*  29 V */
    {XXBYTE_YARV_ID_DUPHASH, 11},                                /*  30 V */
    {XXBYTE_YARV_ID_EXPANDARRAY, 24},                            /*  31 NN */
    {XXBYTE_YARV_ID_CONCATARRAY, 0},                             /*  32 - */
    {XXBYTE_YARV_ID_CONCATTOARRAY, 0},                           /*  33 - */
    {XXBYTE_YARV_ID_PUSHTOARRAY, 7},                             /*  34 N */
    {XXBYTE_YARV_ID_SPLATARRAY, 11},                             /*  35 V */
    {XXBYTE_YARV_ID_SPLATKW, 0},                                 /*  36 - */
    {XXBYTE_YARV_ID_NEWHASH, 7},                                 /*  37 N */
    {XXBYTE_YARV_ID_NEWRANGE, 7},                                /*  38 N */
    {XXBYTE_YARV_ID_POP, 0},                                     /*  39 - */
    {XXBYTE_YARV_ID_DUP, 0},                                     /*  40 - */
    {XXBYTE_YARV_ID_DUPN, 7},                                    /*  41 N */
    {XXBYTE_YARV_ID_SWAP, 0},                                    /*  42 - */
    {XXBYTE_YARV_ID_OPT_REVERSE, 7},                             /*  43 N */
    {XXBYTE_YARV_ID_TOPN, 7},                                    /*  44 N */
    {XXBYTE_YARV_ID_SETN, 7},                                    /*  45 N */
    {XXBYTE_YARV_ID_ADJUSTSTACK, 7},                             /*  46 N */
    {XXBYTE_YARV_ID_DEFINED, 32},                                /*  47 NVV */
    {XXBYTE_YARV_ID_DEFINEDIVAR, 30},                            /*  48 IAV */
    {XXBYTE_YARV_ID_CHECKMATCH, 7},                              /*  49 N */
    {XXBYTE_YARV_ID_CHECKKEYWORD, 21},                           /*  50 LL */
    {XXBYTE_YARV_ID_CHECKTYPE, 7},                               /*  51 N */
    {XXBYTE_YARV_ID_DEFINECLASS, 31},                            /*  52 ISN */
    {XXBYTE_YARV_ID_DEFINEMETHOD, 20},                           /*  53 IS */
    {XXBYTE_YARV_ID_DEFINESMETHOD, 20},                          /*  54 IS */
    {XXBYTE_YARV_ID_SEND, 15},                                   /*  55 CS */
    {XXBYTE_YARV_ID_SENDFORWARD, 15},                            /*  56 CS */
    {XXBYTE_YARV_ID_OPT_SEND_WITHOUT_BLOCK, 1},                  /*  57 C */
    {XXBYTE_YARV_ID_OPT_NEW, 14},                                /*  58 CO */
    {XXBYTE_YARV_ID_OBJTOSTRING, 1},                             /*  59 C */
    {XXBYTE_YARV_ID_OPT_ARY_FREEZE, 28},                         /*  60 VC */
    {XXBYTE_YARV_ID_OPT_HASH_FREEZE, 28},                        /*  61 VC */
    {XXBYTE_YARV_ID_OPT_STR_FREEZE, 28},                         /*  62 VC */
    {XXBYTE_YARV_ID_OPT_NIL_P, 1},                               /*  63 C */
    {XXBYTE_YARV_ID_OPT_STR_UMINUS, 28},                         /*  64 VC */
    {XXBYTE_YARV_ID_OPT_DUPARRAY_SEND, 34},                      /*  65 VIN */
    {XXBYTE_YARV_ID_OPT_NEWARRAY_SEND, 24},                      /*  66 NN */
    {XXBYTE_YARV_ID_INVOKESUPER, 15},                            /*  67 CS */
    {XXBYTE_YARV_ID_INVOKESUPERFORWARD, 15},                     /*  68 CS */
    {XXBYTE_YARV_ID_INVOKEBLOCK, 1},                             /*  69 C */
    {XXBYTE_YARV_ID_LEAVE, 0},                                   /*  70 - */
    {XXBYTE_YARV_ID_THROW, 7},                                   /*  71 N */
    {XXBYTE_YARV_ID_JUMP, 8},                                    /*  72 O */
    {XXBYTE_YARV_ID_BRANCHIF, 8},                                /*  73 O */
    {XXBYTE_YARV_ID_BRANCHUNLESS, 8},                            /*  74 O */
    {XXBYTE_YARV_ID_BRANCHNIL, 8},                               /*  75 O */
    {XXBYTE_YARV_ID_ONCE, 27},                                   /*  76 ST */
    {XXBYTE_YARV_ID_OPT_CASE_DISPATCH, 16},                      /*  77 HO */
    {XXBYTE_YARV_ID_OPT_PLUS, 1},                                /*  78 C */
    {XXBYTE_YARV_ID_OPT_MINUS, 1},                               /*  79 C */
    {XXBYTE_YARV_ID_OPT_MULT, 1},                                /*  80 C */
    {XXBYTE_YARV_ID_OPT_DIV, 1},                                 /*  81 C */
    {XXBYTE_YARV_ID_OPT_MOD, 1},                                 /*  82 C */
    {XXBYTE_YARV_ID_OPT_EQ, 1},                                  /*  83 C */
    {XXBYTE_YARV_ID_OPT_NEQ, 12},                                /*  84 CC */
    {XXBYTE_YARV_ID_OPT_LT, 1},                                  /*  85 C */
    {XXBYTE_YARV_ID_OPT_LE, 1},                                  /*  86 C */
    {XXBYTE_YARV_ID_OPT_GT, 1},                                  /*  87 C */
    {XXBYTE_YARV_ID_OPT_GE, 1},                                  /*  88 C */
    {XXBYTE_YARV_ID_OPT_LTLT, 1},                                /*  89 C */
    {XXBYTE_YARV_ID_OPT_AND, 1},                                 /*  90 C */
    {XXBYTE_YARV_ID_OPT_OR, 1},                                  /*  91 C */
    {XXBYTE_YARV_ID_OPT_AREF, 1},                                /*  92 C */
    {XXBYTE_YARV_ID_OPT_ASET, 1},                                /*  93 C */
    {XXBYTE_YARV_ID_OPT_LENGTH, 1},                              /*  94 C */
    {XXBYTE_YARV_ID_OPT_SIZE, 1},                                /*  95 C */
    {XXBYTE_YARV_ID_OPT_EMPTY_P, 1},                             /*  96 C */
    {XXBYTE_YARV_ID_OPT_SUCC, 1},                                /*  97 C */
    {XXBYTE_YARV_ID_OPT_NOT, 1},                                 /*  98 C */
    {XXBYTE_YARV_ID_OPT_REGEXPMATCH2, 1},                        /*  99 C */
    {XXBYTE_YARV_ID_INVOKEBUILTIN, 9},                           /* 100 R */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE, 26},             /* 101 RN */
    {XXBYTE_YARV_ID_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26},       /* 102 RN */
    {XXBYTE_YARV_ID_GETLOCAL_WC_0, 6},                           /* 103 L */
    {XXBYTE_YARV_ID_GETLOCAL_WC_1, 6},                           /* 104 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_0, 6},                           /* 105 L */
    {XXBYTE_YARV_ID_SETLOCAL_WC_1, 6},                           /* 106 L */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_0_, 0},                    /* 107 - */
    {XXBYTE_YARV_ID_PUTOBJECT_INT2FIX_1_, 0},                    /* 108 - */
    {XXBYTE_YARV_ID_TRACE_NOP, 0},                               /* 109 - */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL, 22},                         /* 110 LN */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL, 22},                         /* 111 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAM, 22},                    /* 112 LN */
    {XXBYTE_YARV_ID_TRACE_SETBLOCKPARAM, 22},                    /* 113 LN */
    {XXBYTE_YARV_ID_TRACE_GETBLOCKPARAMPROXY, 22},               /* 114 LN */
    {XXBYTE_YARV_ID_TRACE_GETSPECIAL, 24},                       /* 115 NN */
    {XXBYTE_YARV_ID_TRACE_SETSPECIAL, 7},                        /* 116 N */
    {XXBYTE_YARV_ID_TRACE_GETINSTANCEVARIABLE, 17},              /* 117 IA */
    {XXBYTE_YARV_ID_TRACE_SETINSTANCEVARIABLE, 17},              /* 118 IA */
    {XXBYTE_YARV_ID_TRACE_GETCLASSVARIABLE, 18},                 /* 119 IJ */
    {XXBYTE_YARV_ID_TRACE_SETCLASSVARIABLE, 18},                 /* 120 IJ */
    {XXBYTE_YARV_ID_TRACE_OPT_GETCONSTANT_PATH, 5},              /* 121 K */
    {XXBYTE_YARV_ID_TRACE_GETCONSTANT, 4},                       /* 122 I */
    {XXBYTE_YARV_ID_TRACE_SETCONSTANT, 4},                       /* 123 I */
    {XXBYTE_YARV_ID_TRACE_GETGLOBAL, 4},                         /* 124 I */
    {XXBYTE_YARV_ID_TRACE_SETGLOBAL, 4},                         /* 125 I */
    {XXBYTE_YARV_ID_TRACE_PUTNIL, 0},                            /* 126 - */
    {XXBYTE_YARV_ID_TRACE_PUTSELF, 0},                           /* 127 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT, 11},                        /* 128 V */
    {XXBYTE_YARV_ID_TRACE_PUTSPECIALOBJECT, 7},                  /* 129 N */
    {XXBYTE_YARV_ID_TRACE_PUTSTRING, 11},                        /* 130 V */
    {XXBYTE_YARV_ID_TRACE_PUTCHILLEDSTRING, 11},                 /* 131 V */
    {XXBYTE_YARV_ID_TRACE_CONCATSTRINGS, 7},                     /* 132 N */
    {XXBYTE_YARV_ID_TRACE_ANYTOSTRING, 0},                       /* 133 - */
    {XXBYTE_YARV_ID_TRACE_TOREGEXP, 24},                         /* 134 NN */
    {XXBYTE_YARV_ID_TRACE_INTERN, 0},                            /* 135 - */
    {XXBYTE_YARV_ID_TRACE_NEWARRAY, 7},                          /* 136 N */
    {XXBYTE_YARV_ID_TRACE_PUSHTOARRAYKWSPLAT, 0},                /* 137 - */
    {XXBYTE_YARV_ID_TRACE_DUPARRAY, 11},                         /* 138 V */
    {XXBYTE_YARV_ID_TRACE_DUPHASH, 11},                          /* 139 V */
    {XXBYTE_YARV_ID_TRACE_EXPANDARRAY, 24},                      /* 140 NN */
    {XXBYTE_YARV_ID_TRACE_CONCATARRAY, 0},                       /* 141 - */
    {XXBYTE_YARV_ID_TRACE_CONCATTOARRAY, 0},                     /* 142 - */
    {XXBYTE_YARV_ID_TRACE_PUSHTOARRAY, 7},                       /* 143 N */
    {XXBYTE_YARV_ID_TRACE_SPLATARRAY, 11},                       /* 144 V */
    {XXBYTE_YARV_ID_TRACE_SPLATKW, 0},                           /* 145 - */
    {XXBYTE_YARV_ID_TRACE_NEWHASH, 7},                           /* 146 N */
    {XXBYTE_YARV_ID_TRACE_NEWRANGE, 7},                          /* 147 N */
    {XXBYTE_YARV_ID_TRACE_POP, 0},                               /* 148 - */
    {XXBYTE_YARV_ID_TRACE_DUP, 0},                               /* 149 - */
    {XXBYTE_YARV_ID_TRACE_DUPN, 7},                              /* 150 N */
    {XXBYTE_YARV_ID_TRACE_SWAP, 0},                              /* 151 - */
    {XXBYTE_YARV_ID_TRACE_OPT_REVERSE, 7},                       /* 152 N */
    {XXBYTE_YARV_ID_TRACE_TOPN, 7},                              /* 153 N */
    {XXBYTE_YARV_ID_TRACE_SETN, 7},                              /* 154 N */
    {XXBYTE_YARV_ID_TRACE_ADJUSTSTACK, 7},                       /* 155 N */
    {XXBYTE_YARV_ID_TRACE_DEFINED, 32},                          /* 156 NVV */
    {XXBYTE_YARV_ID_TRACE_DEFINEDIVAR, 30},                      /* 157 IAV */
    {XXBYTE_YARV_ID_TRACE_CHECKMATCH, 7},                        /* 158 N */
    {XXBYTE_YARV_ID_TRACE_CHECKKEYWORD, 21},                     /* 159 LL */
    {XXBYTE_YARV_ID_TRACE_CHECKTYPE, 7},                         /* 160 N */
    {XXBYTE_YARV_ID_TRACE_DEFINECLASS, 31},                      /* 161 ISN */
    {XXBYTE_YARV_ID_TRACE_DEFINEMETHOD, 20},                     /* 162 IS */
    {XXBYTE_YARV_ID_TRACE_DEFINESMETHOD, 20},                    /* 163 IS */
    {XXBYTE_YARV_ID_TRACE_SEND, 15},                             /* 164 CS */
    {XXBYTE_YARV_ID_TRACE_SENDFORWARD, 15},                      /* 165 CS */
    {XXBYTE_YARV_ID_TRACE_OPT_SEND_WITHOUT_BLOCK, 1},            /* 166 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NEW, 14},                          /* 167 CO */
    {XXBYTE_YARV_ID_TRACE_OBJTOSTRING, 1},                       /* 168 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ARY_FREEZE, 28},                   /* 169 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_HASH_FREEZE, 28},                  /* 170 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_FREEZE, 28},                   /* 171 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_NIL_P, 1},                         /* 172 C */
    {XXBYTE_YARV_ID_TRACE_OPT_STR_UMINUS, 28},                   /* 173 VC */
    {XXBYTE_YARV_ID_TRACE_OPT_DUPARRAY_SEND, 34},                /* 174 VIN */
    {XXBYTE_YARV_ID_TRACE_OPT_NEWARRAY_SEND, 24},                /* 175 NN */
    {XXBYTE_YARV_ID_TRACE_INVOKESUPER, 15},                      /* 176 CS */
    {XXBYTE_YARV_ID_TRACE_INVOKESUPERFORWARD, 15},               /* 177 CS */
    {XXBYTE_YARV_ID_TRACE_INVOKEBLOCK, 1},                       /* 178 C */
    {XXBYTE_YARV_ID_TRACE_LEAVE, 0},                             /* 179 - */
    {XXBYTE_YARV_ID_TRACE_THROW, 7},                             /* 180 N */
    {XXBYTE_YARV_ID_TRACE_JUMP, 8},                              /* 181 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHIF, 8},                          /* 182 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHUNLESS, 8},                      /* 183 O */
    {XXBYTE_YARV_ID_TRACE_BRANCHNIL, 8},                         /* 184 O */
    {XXBYTE_YARV_ID_TRACE_ONCE, 27},                             /* 185 ST */
    {XXBYTE_YARV_ID_TRACE_OPT_CASE_DISPATCH, 16},                /* 186 HO */
    {XXBYTE_YARV_ID_TRACE_OPT_PLUS, 1},                          /* 187 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MINUS, 1},                         /* 188 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MULT, 1},                          /* 189 C */
    {XXBYTE_YARV_ID_TRACE_OPT_DIV, 1},                           /* 190 C */
    {XXBYTE_YARV_ID_TRACE_OPT_MOD, 1},                           /* 191 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EQ, 1},                            /* 192 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NEQ, 12},                          /* 193 CC */
    {XXBYTE_YARV_ID_TRACE_OPT_LT, 1},                            /* 194 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LE, 1},                            /* 195 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GT, 1},                            /* 196 C */
    {XXBYTE_YARV_ID_TRACE_OPT_GE, 1},                            /* 197 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LTLT, 1},                          /* 198 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AND, 1},                           /* 199 C */
    {XXBYTE_YARV_ID_TRACE_OPT_OR, 1},                            /* 200 C */
    {XXBYTE_YARV_ID_TRACE_OPT_AREF, 1},                          /* 201 C */
    {XXBYTE_YARV_ID_TRACE_OPT_ASET, 1},                          /* 202 C */
    {XXBYTE_YARV_ID_TRACE_OPT_LENGTH, 1},                        /* 203 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SIZE, 1},                          /* 204 C */
    {XXBYTE_YARV_ID_TRACE_OPT_EMPTY_P, 1},                       /* 205 C */
    {XXBYTE_YARV_ID_TRACE_OPT_SUCC, 1},                          /* 206 C */
    {XXBYTE_YARV_ID_TRACE_OPT_NOT, 1},                           /* 207 C */
    {XXBYTE_YARV_ID_TRACE_OPT_REGEXPMATCH2, 1},                  /* 208 C */
    {XXBYTE_YARV_ID_TRACE_INVOKEBUILTIN, 9},                     /* 209 R */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE, 26},       /* 210 RN */
    {XXBYTE_YARV_ID_TRACE_OPT_INVOKEBUILTIN_DELEGATE_LEAVE, 26}, /* 211 RN */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_0, 6},                     /* 212 L */
    {XXBYTE_YARV_ID_TRACE_GETLOCAL_WC_1, 6},                     /* 213 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_0, 6},                     /* 214 L */
    {XXBYTE_YARV_ID_TRACE_SETLOCAL_WC_1, 6},                     /* 215 L */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_0_, 0},              /* 216 - */
    {XXBYTE_YARV_ID_TRACE_PUTOBJECT_INT2FIX_1_, 0},              /* 217 - */
    {XXBYTE_YARV_ID_ZJIT_GETINSTANCEVARIABLE, 17},               /* 218 IA */
    {XXBYTE_YARV_ID_ZJIT_SETINSTANCEVARIABLE, 17},               /* 219 IA */
    {XXBYTE_YARV_ID_ZJIT_DEFINEDIVAR, 30},                       /* 220 IAV */
    {XXBYTE_YARV_ID_ZJIT_SEND, 15},                              /* 221 CS */
    {XXBYTE_YARV_ID_ZJIT_OPT_SEND_WITHOUT_BLOCK, 1},             /* 222 C */
    {XXBYTE_YARV_ID_ZJIT_OBJTOSTRING, 1},                        /* 223 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_NIL_P, 1},                          /* 224 C */
    {XXBYTE_YARV_ID_ZJIT_INVOKEBLOCK, 1},                        /* 225 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_PLUS, 1},                           /* 226 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_MINUS, 1},                          /* 227 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_MULT, 1},                           /* 228 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_DIV, 1},                            /* 229 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_MOD, 1},                            /* 230 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_EQ, 1},                             /* 231 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_NEQ, 12},                           /* 232 CC */
    {XXBYTE_YARV_ID_ZJIT_OPT_LT, 1},                             /* 233 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_LE, 1},                             /* 234 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_GT, 1},                             /* 235 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_GE, 1},                             /* 236 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_LTLT, 1},                           /* 237 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_AND, 1},                            /* 238 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_OR, 1},                             /* 239 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_AREF, 1},                           /* 240 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_ASET, 1},                           /* 241 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_LENGTH, 1},                         /* 242 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_SIZE, 1},                           /* 243 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_EMPTY_P, 1},                        /* 244 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_SUCC, 1},                           /* 245 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_NOT, 1},                            /* 246 C */
    {XXBYTE_YARV_ID_ZJIT_OPT_REGEXPMATCH2, 1},                   /* 247 C */
};

const yarv_version xxbyte_yarv_versions[YARV_VERSION_COUNT] = {
    {XXBYTE_YARV_MODE(2, 6), 202, yarv_ops_2_6}, {XXBYTE_YARV_MODE(2, 7), 206, yarv_ops_2_7}, {XXBYTE_YARV_MODE(3, 0), 202, yarv_ops_3_0},
    {XXBYTE_YARV_MODE(3, 1), 202, yarv_ops_3_1}, {XXBYTE_YARV_MODE(3, 2), 202, yarv_ops_3_2}, {XXBYTE_YARV_MODE(3, 3), 204, yarv_ops_3_3},
    {XXBYTE_YARV_MODE(3, 4), 220, yarv_ops_3_4}, {XXBYTE_YARV_MODE(4, 0), 248, yarv_ops_4_0},
};

#else
typedef int xxbyte_yarv_tables_disabled;
#endif
