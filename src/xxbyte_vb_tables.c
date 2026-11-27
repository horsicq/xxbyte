/* Copyright (c) 2026 hors<horsicq@gmail.com> */

/*
 * Visual Basic 5/6 P-code opcode tables; generated, do not edit by hand.
 *
 * Opcode names: the P-code opcode database of MrUnleaded, Moogman and Napalm
 * as published by Semi VB Decompiler (opcodes.txt) and vb-decompiler.org
 * (https://www.vb-decompiler.org/vb_pcode_table.htm), checked against
 * Semi VB Decompiler's modPCode.bas MakeOpcode table
 * (https://github.com/VBGAMER45/Semi-VB-Decompiler) and the WKTVBDE help
 * file tables (https://sandsprite.com/vb-reversing/WKTVBDebugger_help_file/).
 * Where they disagree the majority name is used: 02 LargeBos, 5E
 * ImpAdCallI4, FBB3 MulCy, FC64 LitVar_Null, FD70 MemLdUI1. Slots that
 * share a handler with another opcode ("DOC" entries) take its name.
 *
 * Valid slots, instruction lengths and operand layouts were checked against
 * the dispatch tables and opcode handlers of MSVBVM60.DLL 6.0.97.82 and
 * 6.0.98.2 and MSVBVM50.DLL 5.2.82.44. The runtime consumes a different
 * length than the published tables for FE8C NextEachVar (6), FE9E
 * ExitProcFrameCb (6), FE9F ExitProcFrameCbStack (6) and FF3E
 * VarLateMemCallLdRfVar (8). MSVBVM50 alone differs for FE1D VCallCbFrame
 * (VB_OP_VB5_NO_VTABLE): its handler jumps into the ThisVCallCbFrame body
 * past the vtable-offset read, so it calls through vtable offset 1D (the
 * opcode byte) and consumes 4 bytes, the opcode and the return-flags word.
 * FFree*, OnGoto, OnGosub, GetRecOwner*, PutRecOwner*, LateMemNamed* and
 * LateIdNamed* carry a 16-bit byte count followed by that many bytes.
 * MSVBVM50 rejects the VB_OP_VB6 opcodes; FBAD and FF1D, named by some
 * tables, are undefined in both runtimes.
 * Operand kinds follow the handlers where the tables say nothing: Redim and
 * RedimPreserve start with the dimension count (the number of bound pairs
 * popped), and MSVBVM60 reads their element type as a VARTYPE under
 * FADF_HAVEVARTYPE; ExitProcCbHresult and ExitProcFrameCbHresult address
 * frame slots.
 */

#include "xxbyte_vb_tables.h"

#if XXBYTE_USE_VB

#define G_JUMP XXBYTE_GROUP_JUMP
#define G_COND XXBYTE_GROUP_CONDITIONAL
#define G_CALL XXBYTE_GROUP_CALL
#define G_RET XXBYTE_GROUP_RETURN
#define G_THROW XXBYTE_GROUP_THROW
#define G_SWITCH XXBYTE_GROUP_SWITCH
#define G_STOP XXBYTE_GROUP_STOP

const vb_mnemonic xxbyte_vb_mnemonics[XXBYTE_VB_ID_COUNT] = {
    {NULL, NULL, 0},
    {"LargeBos", "b", 0},
    {"FLdRfVar", "a", 0},
    {"ImpAdLdRf", "i", 0},
    {"MemLdRfVar", "m", 0},
    {"FMemLdRf", "am", 0},
    {"FLdPr", "a", 0},
    {"ImpAdCallHresult", "in", G_CALL},
    {"ImpAdCallFPR4", "in", G_CALL},
    {"ImpAdCallI2", "in", G_CALL},
    {"ImpAdCallCy", "in", G_CALL},
    {"VCallHresult", "vc", G_CALL},
    {"VCallFPR8", "v", G_CALL},
    {"VCallAd", "v", G_CALL},
    {"ThisVCallHresult", "vc", G_CALL},
    {"ThisVCall", "v", G_CALL},
    {"ThisVCallAd", "v", G_CALL},
    {"ExitProcHresult", "", G_RET | G_STOP},
    {"ExitProc", "", G_RET | G_STOP},
    {"ExitProcI2", "", G_RET | G_STOP},
    {"ExitProcR4", "", G_RET | G_STOP},
    {"ExitProcR8", "", G_RET | G_STOP},
    {"ExitProcCy", "", G_RET | G_STOP},
    {"FStAdFunc", "a", 0},
    {"FFree1Ad", "a", 0},
    {"LitStr", "c", 0},
    {"BranchF", "l", G_COND},
    {"BranchT", "l", G_COND},
    {"Branch", "l", G_JUMP | G_STOP},
    {"CRec2Ansi", "c", 0},
    {"CRec2Uni", "c", 0},
    {"FLdPrThis", "", 0},
    {"ImpAdLdPr", "i", 0},
    {"FStStrNoPop", "a", 0},
    {"NewIfNullPr", "c", 0},
    {"PopAdLdVar", "", 0},
    {"AryDescTemp", "a", 0},
    {"LitVar_Missing", "a", 0},
    {"LitVarI2", "aw", 0},
    {"FFreeAd", "*:a", 0},
    {"ConcatStr", "", 0},
    {"PopTmpLdAd2", "a", 0},
    {"LateIdSt", "D", G_CALL},
    {"AryUnlock", "a", 0},
    {"AryLock", "a", 0},
    {"FFree1Str", "a", 0},
    {"PopTmpLdAd8", "a", 0},
    {"FStStr", "a", 0},
    {"FFreeStr", "*:a", 0},
    {"LdFixedStr", "n", 0},
    {"CStr2Ansi", "", 0},
    {"FFree1Var", "a", 0},
    {"FFreeVar", "*:a", 0},
    {"PopFPR4", "", 0},
    {"CopyBytes", "n", 0},
    {"PopFPR8", "", 0},
    {"LitVarStr", "ac", 0},
    {"Ary1StStrCopy", "", 0},
    {"SetLastSystemError", "", 0},
    {"CastAd", "c", 0},
    {"FLdZeroAd", "a", 0},
    {"CVarCy", "a", 0},
    {"Ary1LdRf", "", 0},
    {"Ary1LdPr", "", 0},
    {"CR4Var", "", 0},
    {"FStStrCopy", "a", 0},
    {"CVarI2", "a", 0},
    {"Error", "", G_THROW | G_STOP},
    {"CVarStr", "a", 0},
    {"StFixedStr", "n", 0},
    {"ILdPr", "a", 0},
    {"PopAdLd4", "", 0},
    {"FnLenStr", "", 0},
    {"OnErrorGoto", "e", 0},
    {"FnLBound", "", 0},
    {"CVarRef", "an", 0},
    {"FStVarCopyObj", "a", 0},
    {"MidStr", "n", 0},
    {"CI4Str", "", 0},
    {"Ary1StVar", "", 0},
    {"CBoolCy", "", 0},
    {"FMemStStrCopy", "am", 0},
    {"CI2Var", "", 0},
    {"NewIfNullAd", "c", 0},
    {"LateMemLdVar", "ac", G_CALL},
    {"MemLdPr", "m", 0},
    {"PopTmpLdAdStr", "a", 0},
    {"Erase", "", 0},
    {"FStAdFuncNoPop", "a", 0},
    {"BranchFVar", "l", G_COND},
    {"HardType", "", 0},
    {"ImpAdCallI4", "in", G_CALL},
    {"FMemLdPr", "am", 0},
    {"CStrVarTmp", "", 0},
    {"LateIdLdVar", "aD", G_CALL},
    {"IStDarg", "a", 0},
    {"LitVar_TRUE", "a", 0},
    {"NextI2", "al", G_COND},
    {"NextStepI2", "al", G_COND},
    {"NextI4", "al", G_COND},
    {"NextStepI4", "al", G_COND},
    {"NextStepR4", "al", G_COND},
    {"NextStepR8", "al", G_COND},
    {"NextStepCy", "al", G_COND},
    {"FLdI2", "a", 0},
    {"ILdRf", "a", 0},
    {"FLdR8", "a", 0},
    {"FLdFPR4", "a", 0},
    {"FLdFPR8", "a", 0},
    {"FStI2", "a", 0},
    {"FStR4", "a", 0},
    {"FStR8", "a", 0},
    {"FStFPR4", "a", 0},
    {"FStFPR8", "a", 0},
    {"ImpAdLdI2", "i", 0},
    {"ImpAdLdI4", "i", 0},
    {"ImpAdLdCy", "i", 0},
    {"ImpAdLdFPR4", "i", 0},
    {"ImpAdLdFPR8", "i", 0},
    {"ImpAdStI2", "i", 0},
    {"ImpAdStR4", "i", 0},
    {"ImpAdStCy", "i", 0},
    {"ImpAdStFPR4", "i", 0},
    {"ImpAdStFPR8", "i", 0},
    {"ILdI2", "a", 0},
    {"ILdI4", "a", 0},
    {"ILdR8", "a", 0},
    {"ILdFPR4", "a", 0},
    {"ILdFPR8", "a", 0},
    {"IStI2", "a", 0},
    {"IStI4", "a", 0},
    {"IStR8", "a", 0},
    {"IStFPR4", "a", 0},
    {"IStFPR8", "a", 0},
    {"MemLdI2", "m", 0},
    {"MemLdStr", "m", 0},
    {"MemLdR8", "m", 0},
    {"MemLdFPR4", "m", 0},
    {"MemLdFPR8", "m", 0},
    {"MemStI2", "m", 0},
    {"MemStI4", "m", 0},
    {"MemStR8", "m", 0},
    {"MemStFPR4", "m", 0},
    {"MemStFPR8", "m", 0},
    {"FMemLdI2", "am", 0},
    {"FMemLdR4", "am", 0},
    {"FMemLdCy", "am", 0},
    {"FMemLdFPR4", "am", 0},
    {"FMemLdFPR8", "am", 0},
    {"FMemStI2", "am", 0},
    {"FMemStI4", "am", 0},
    {"FMemStR8", "am", 0},
    {"FMemStFPR4", "am", 0},
    {"FMemStFPR8", "am", 0},
    {"Ary1LdI2", "", 0},
    {"Ary1LdI4", "", 0},
    {"Ary1LdCy", "", 0},
    {"Ary1LdFPR4", "", 0},
    {"Ary1LdFPR8", "", 0},
    {"Ary1StI2", "", 0},
    {"Ary1StI4", "", 0},
    {"Ary1StCy", "", 0},
    {"Ary1StFPR4", "", 0},
    {"Ary1StFPR8", "", 0},
    {"AryLdPr", "n", 0},
    {"AryLdRf", "n", 0},
    {"AddI2", "", 0},
    {"AddI4", "", 0},
    {"AddR8", "", 0},
    {"AddCy", "", 0},
    {"SubI2", "", 0},
    {"SubI4", "", 0},
    {"SubR4", "", 0},
    {"SubCy", "", 0},
    {"MulI2", "", 0},
    {"MulI4", "", 0},
    {"MulR8", "", 0},
    {"MulCy", "", 0},
    {"MulCyI2", "", 0},
    {"DivR8", "", 0},
    {"UMiI2", "", 0},
    {"UMiI4", "", 0},
    {"UMiR8", "", 0},
    {"UMiCy", "", 0},
    {"FnAbsI2", "", 0},
    {"FnAbsI4", "", 0},
    {"FnAbsR4", "", 0},
    {"FnAbsCy", "", 0},
    {"IDvI2", "", 0},
    {"IDvI4", "", 0},
    {"ModI2", "", 0},
    {"ModI4", "", 0},
    {"NotI4", "", 0},
    {"AndI4", "", 0},
    {"OrI4", "", 0},
    {"EqI2", "", 0},
    {"EqI4", "", 0},
    {"EqR4", "", 0},
    {"EqCy", "", 0},
    {"EqCyR8", "", 0},
    {"NeI2", "", 0},
    {"NeI4", "", 0},
    {"NeR8", "", 0},
    {"NeCy", "", 0},
    {"NeCyR8", "", 0},
    {"LtI2", "", 0},
    {"LtI4", "", 0},
    {"LtR8", "", 0},
    {"LtCy", "", 0},
    {"LtCyR8", "", 0},
    {"LeI2", "", 0},
    {"LeI4", "", 0},
    {"LeR8", "", 0},
    {"LeCy", "", 0},
    {"LeCyR8", "", 0},
    {"GtI2", "", 0},
    {"GtI4", "", 0},
    {"GtR4", "", 0},
    {"GtCy", "", 0},
    {"GtCyR8", "", 0},
    {"GeI2", "", 0},
    {"GeI4", "", 0},
    {"GeR8", "", 0},
    {"GeCy", "", 0},
    {"GeCyR8", "", 0},
    {"CI2I4", "", 0},
    {"CI2R8", "", 0},
    {"CI2Cy", "", 0},
    {"CI4UI1", "", 0},
    {"CI4R8", "", 0},
    {"CI4Cy", "", 0},
    {"CR4R4", "", 0},
    {"CR8I2", "", 0},
    {"CR8I4", "", 0},
    {"CR8R8", "", 0},
    {"CR8Cy", "", 0},
    {"CCyI2", "", 0},
    {"CCyI4", "", 0},
    {"CCyR4", "", 0},
    {"CDateR8", "", 0},
    {"LitI2", "w", 0},
    {"LitI2_Byte", "B", 0},
    {"LitI4", "d", 0},
    {"LitCy", "y", 0},
    {"LitCy4", "Y", 0},
    {"LitI2FP", "w", 0},
    {"LitR4FP", "f", 0},
    {"LitDate", "F", 0},
    {"ImpUI1", "", 0},
    {"ImpI4", "", 0},
    {"ImpVar", "a", 0},
    {"EqvUI1", "", 0},
    {"EqvI4", "", 0},
    {"EqvVar", "a", 0},
    {"XorI4", "", 0},
    {"XorVar", "a", 0},
    {"OrI2", "", 0},
    {"OrVar", "a", 0},
    {"AndUI1", "", 0},
    {"AndVar", "a", 0},
    {"EqR8", "", 0},
    {"EqVar", "a", 0},
    {"EqStr", "", 0},
    {"EqTextVar", "a", 0},
    {"EqTextStr", "", 0},
    {"EqVarBool", "", 0},
    {"EqTextVarBool", "", 0},
    {"NeUI1", "", 0},
    {"NeR4", "", 0},
    {"NeVar", "a", 0},
    {"NeStr", "", 0},
    {"NeTextVar", "a", 0},
    {"NeTextStr", "", 0},
    {"NeVarBool", "", 0},
    {"NeTextVarBool", "", 0},
    {"LeUI1", "", 0},
    {"LeR4", "", 0},
    {"LeVar", "a", 0},
    {"LeStr", "", 0},
    {"LeTextVar", "a", 0},
    {"LeTextStr", "", 0},
    {"LeVarBool", "", 0},
    {"LeTextVarBool", "", 0},
    {"GeUI1", "", 0},
    {"GeR4", "", 0},
    {"GeVar", "a", 0},
    {"GeStr", "", 0},
    {"GeTextVar", "a", 0},
    {"GeTextStr", "", 0},
    {"GeVarBool", "", 0},
    {"GeTextVarBool", "", 0},
    {"LtUI1", "", 0},
    {"LtR4", "", 0},
    {"LtVar", "a", 0},
    {"LtStr", "", 0},
    {"LtTextVar", "a", 0},
    {"LtTextStr", "", 0},
    {"LtVarBool", "", 0},
    {"LtTextVarBool", "", 0},
    {"GtUI1", "", 0},
    {"GtVar", "a", 0},
    {"GtStr", "", 0},
    {"GtTextVar", "a", 0},
    {"GtTextStr", "", 0},
    {"GtVarBool", "", 0},
    {"GtTextVarBool", "", 0},
    {"LikeVar", "a", 0},
    {"LikeStr", "", 0},
    {"LikeTextVar", "a", 0},
    {"LikeTextStr", "", 0},
    {"LikeVarBool", "", 0},
    {"LikeTextVarBool", "", 0},
    {"BetweenUI1", "", 0},
    {"BetweenI2", "", 0},
    {"BetweenI4", "", 0},
    {"BetweenR4", "", 0},
    {"BetweenCy", "", 0},
    {"BetweenVar", "", 0},
    {"BetweenStr", "", 0},
    {"BetweenTextVar", "", 0},
    {"BetweenTextStr", "", 0},
    {"AddUI1", "", 0},
    {"AddR4", "", 0},
    {"AddVar", "a", 0},
    {"SubUI1", "", 0},
    {"SubVar", "a", 0},
    {"ModUI1", "", 0},
    {"ModVar", "a", 0},
    {"IDvUI1", "", 0},
    {"IDvVar", "a", 0},
    {"MulUI1", "", 0},
    {"MulR4", "", 0},
    {"MulVar", "a", 0},
    {"DivVar", "a", 0},
    {"NotUI1", "", 0},
    {"NotVar", "a", 0},
    {"UMiR4", "", 0},
    {"UMiVar", "a", 0},
    {"PwrVar", "a", 0},
    {"PwrR8R8", "", 0},
    {"PwrR8I2", "", 0},
    {"Is", "", 0},
    {"FnAbsVar", "a", 0},
    {"FnFixR8", "", 0},
    {"FnFixCy", "", 0},
    {"FnFixVar", "a", 0},
    {"FnIntR8", "", 0},
    {"FnIntCy", "", 0},
    {"FnIntVar", "a", 0},
    {"FnLenVar", "a", 0},
    {"FnLenBVar", "a", 0},
    {"FnLenBStr", "", 0},
    {"ConcatVar", "a", 0},
    {"FnSgnUI1", "", 0},
    {"FnSgnI4", "", 0},
    {"FnSgnR8", "", 0},
    {"FnSgnR4", "", 0},
    {"FnSgnCy", "", 0},
    {"SeekFile", "", 0},
    {"NameFile", "", 0},
    {"CStrI2", "", 0},
    {"CStrUI1", "", 0},
    {"CStrI4", "", 0},
    {"CStrR4", "", 0},
    {"CStrR8", "", 0},
    {"CStrCy", "", 0},
    {"CStrVar", "", 0},
    {"CCyVar", "", 0},
    {"CCyStr", "", 0},
    {"CUI1I2", "", 0},
    {"CUI1I4", "", 0},
    {"CUI1R4", "", 0},
    {"CUI1Cy", "", 0},
    {"CUI1Var", "", 0},
    {"CUI1Str", "", 0},
    {"CI2UI1", "", 0},
    {"CI2Str", "", 0},
    {"CI4Var", "", 0},
    {"FnCSngI2", "", 0},
    {"CR4I4", "", 0},
    {"CR4R8", "", 0},
    {"CR8Var", "", 0},
    {"CR4Str", "", 0},
    {"CR8R4", "", 0},
    {"CR8Str", "", 0},
    {"CAdVar", "", 0},
    {"CRefVarAry", "", 0},
    {"CUI1Bool", "", 0},
    {"FnCDblCy", "", 0},
    {"FnCDblR8", "", 0},
    {"FnCSngI4", "", 0},
    {"CSng", "", 0},
    {"FnCSngCy", "", 0},
    {"FnCSngVar", "", 0},
    {"FnCSngStr", "", 0},
    {"FnCByteVar", "", 0},
    {"FnCIntVar", "", 0},
    {"FnCLngVar", "", 0},
    {"FnCDblVar", "", 0},
    {"FnCCurVar", "", 0},
    {"FnCStrVar", "", 0},
    {"FnCBoolVar", "", 0},
    {"FnCDateVar", "", 0},
    {"CBoolUI1", "", 0},
    {"CBoolI4", "", 0},
    {"CBoolR4", "", 0},
    {"CBoolVar", "", 0},
    {"CBoolStr", "", 0},
    {"CStr2Uni", "", 0},
    {"CStrAry2Uni", "", 0},
    {"CStrAry2Ansi", "", 0},
    {"CRecAnsi2Uni", "c", 0},
    {"CRecUni2Ansi", "c", 0},
    {"CStr2Vec", "a", 0},
    {"CVar2Vec", "a", 0},
    {"CVec2Var", "aa", 0},
    {"GetLastError", "", 0},
    {"LitNothing", "", 0},
    {"LitVar_Null", "a", 0},
    {"LitVar_FALSE", "a", 0},
    {"LitVar_Empty", "a", 0},
    {"CheckTypeVar", "c", 0},
    {"CUnkVar", "", 0},
    {"CVarUnk", "a", 0},
    {"LdPrUnkVar", "", 0},
    {"FLdLateIdUnkVar", "aDa", G_CALL},
    {"GetRec3", "", 0},
    {"GetRec4", "", 0},
    {"PutRec3", "", 0},
    {"PutRec4", "", 0},
    {"GetRecOwner3", "*:x", 0},
    {"GetRecOwner4", "*:x", 0},
    {"PutRecOwner3", "*:x", 0},
    {"PutRecOwner4", "*:x", 0},
    {"Input", "", 0},
    {"InputDone", "", 0},
    {"InputItemUI1", "", 0},
    {"InputItemI2", "", 0},
    {"InputItemI4", "", 0},
    {"InputItemR4", "", 0},
    {"InputItemR8", "", 0},
    {"InputItemCy", "", 0},
    {"InputItemVar", "", 0},
    {"InputItemStr", "", 0},
    {"InputItemBool", "", 0},
    {"InputItemDate", "", 0},
    {"PopAd", "", 0},
    {"ParmAry1St", "an", 0},
    {"Ary1LdUI1", "", 0},
    {"Ary1LdR8", "", 0},
    {"Ary1LdRfVar", "", 0},
    {"Ary1LdVar", "", 0},
    {"Ary1StUI1", "", 0},
    {"Ary1StR4", "", 0},
    {"Ary1StStr", "", 0},
    {"Ary1StAd", "", 0},
    {"Ary1StVarAd", "", 0},
    {"Ary1StVarAdFunc", "", 0},
    {"Ary1StVarUnk", "", 0},
    {"Ary1StVarUnkFunc", "", 0},
    {"Ary1StAdFunc", "", 0},
    {"Ary1StVarCopy", "", 0},
    {"Ary1LdRfVarg", "a", 0},
    {"Ary1LdVarg", "", 0},
    {"Ary1LdRfVargParam", "", 0},
    {"Ary1StVarg", "", 0},
    {"Ary1StVargCopy", "", 0},
    {"Ary1StVargAd", "", 0},
    {"Ary1StVargAdFunc", "", 0},
    {"Ary1StVargUnk", "", 0},
    {"Ary1StVargUnkFunc", "", 0},
    {"MidVar", "", 0},
    {"MidBStr", "n", 0},
    {"MidBVar", "", 0},
    {"MidBStrB", "n", 0},
    {"LineInputVar", "", 0},
    {"LineInputStr", "", 0},
    {"Stop", "", G_RET | G_STOP},
    {"End", "", G_RET | G_STOP},
    {"Return", "", G_RET | G_STOP},
    {"FnUBound", "", 0},
    {"ExitProcUI1", "", G_RET | G_STOP},
    {"ExitProcStr", "", G_RET | G_STOP},
    {"FLdUI1", "a", 0},
    {"FLdR4", "a", 0},
    {"FLdVar", "a", 0},
    {"FStUI1", "a", 0},
    {"FStVar", "a", 0},
    {"FStAd", "a", 0},
    {"FStVarAd", "a", 0},
    {"FStVarAdFunc", "a", 0},
    {"FStVarUnk", "a", 0},
    {"FStVarUnkFunc", "a", 0},
    {"FStVarCopy", "a", 0},
    {"BranchFVarFree", "l", G_COND},
    {"BranchTVar", "l", G_COND},
    {"BranchTVarFree", "l", G_COND},
    {"Gosub", "l", G_CALL},
    {"Resume", "r", G_JUMP | G_STOP},
    {"ILdUI1", "a", 0},
    {"ILdAd", "a", 0},
    {"ILdRfDarg", "aa", 0},
    {"ILdDarg", "a", 0},
    {"IStUI1", "a", 0},
    {"IStStr", "a", 0},
    {"IStAd", "a", 0},
    {"IStDargAd", "a", 0},
    {"IStDargAdFunc", "a", 0},
    {"IStDargUnk", "a", 0},
    {"IStDargUnkFunc", "a", 0},
    {"IStAdFunc", "a", 0},
    {"IStDargCopy", "a", 0},
    {"IStStrCopy", "a", 0},
    {"PrintChan", "", 0},
    {"WriteChan", "", 0},
    {"PrintComma", "", 0},
    {"PrintEos", "", 0},
    {"PrintNL", "", 0},
    {"PrintItemComma", "", 0},
    {"PrintItemSemi", "", 0},
    {"PrintItemNL", "", 0},
    {"PrintObj", "a", 0},
    {"PrintSpc", "", 0},
    {"PrintTab", "", 0},
    {"Close", "", 0},
    {"CloseAll", "", 0},
    {"IWMemLdUI1", "M", 0},
    {"IWMemLdI2", "M", 0},
    {"IWMemLdI4", "M", 0},
    {"IWMemLdCy", "M", 0},
    {"IWMemLdRfDarg", "Ma", 0},
    {"IWMemLdFPR4", "M", 0},
    {"IWMemLdFPR8", "M", 0},
    {"IWMemLdPr", "M", 0},
    {"IWMemLdRf", "M", 0},
    {"IWMemLdDarg", "M", 0},
    {"IWMemStUI1", "M", 0},
    {"IWMemStI2", "M", 0},
    {"IWMemStR4", "M", 0},
    {"IWMemStCy", "M", 0},
    {"IWMemStDarg", "M", 0},
    {"IWMemStStr", "M", 0},
    {"IWMemStAd", "M", 0},
    {"IWMemStFPR4", "M", 0},
    {"IWMemStFPR8", "M", 0},
    {"IWMemStDargAd", "M", 0},
    {"IWMemStDargAdFunc", "M", 0},
    {"IWMemStDargUnk", "M", 0},
    {"IWMemStDargUnkFunc", "M", 0},
    {"IWMemStAdFunc", "M", 0},
    {"IWMemStDargCopy", "M", 0},
    {"IWMemStStrCopy", "M", 0},
    {"FStVarNoPop", "a", 0},
    {"CVarUI1", "a", 0},
    {"CVarI4", "a", 0},
    {"CVarR4", "a", 0},
    {"CVarR8", "a", 0},
    {"CVarAd", "a", 0},
    {"MemLdUI1", "m", 0},
    {"MemLdR4", "m", 0},
    {"MemLdVar", "m", 0},
    {"MemStUI1", "m", 0},
    {"MemStR4", "m", 0},
    {"MemStCy", "m", 0},
    {"MemStVar", "m", 0},
    {"MemStStr", "m", 0},
    {"MemStAd", "m", 0},
    {"MemStVarAd", "m", 0},
    {"MemStVarAdFunc", "m", 0},
    {"MemStVarUnk", "m", 0},
    {"MemStVarUnkFunc", "m", 0},
    {"MemStAdFunc", "m", 0},
    {"MemStVarCopy", "m", 0},
    {"MemStStrCopy", "m", 0},
    {"CDargRef", "n", 0},
    {"ExitProcCb", "n", G_RET | G_STOP},
    {"ExitProcCbStack", "n", G_RET | G_STOP},
    {"FStAdNoPop", "a", 0},
    {"LdPrVar", "", 0},
    {"ImpAdLdUI1", "i", 0},
    {"ImpAdLdStr", "i", 0},
    {"ImpAdLdVar", "i", 0},
    {"ImpAdStUI1", "i", 0},
    {"ImpAdStR8", "i", 0},
    {"ImpAdStVar", "i", 0},
    {"ImpAdStStr", "i", 0},
    {"ImpAdStAd", "i", 0},
    {"ImpAdStVarAd", "i", 0},
    {"ImpAdStVarAdFunc", "i", 0},
    {"ImpAdStVarUnk", "i", 0},
    {"ImpAdStVarUnkFunc", "i", 0},
    {"ImpAdStAdFunc", "i", 0},
    {"ImpAdStVarCopy", "i", 0},
    {"ImpAdStStrCopy", "i", 0},
    {"PopTmpLdAd1", "a", 0},
    {"PopTmpLdAdVar", "a", 0},
    {"PopTmpLdAdFPR4", "a", 0},
    {"PopTmpLdAdFPR8", "a", 0},
    {"ExitForCollObj", "al", G_JUMP | G_STOP},
    {"ExitForAryVar", "al", G_JUMP | G_STOP},
    {"ExitForVar", "al", G_JUMP | G_STOP},
    {"FMemLdUI1", "am", 0},
    {"FMemLdR8", "am", 0},
    {"FMemLdVar", "am", 0},
    {"FMemStUI1", "am", 0},
    {"FMemStR4", "am", 0},
    {"FMemStVar", "am", 0},
    {"FMemStStr", "am", 0},
    {"FMemStAd", "am", 0},
    {"FMemStVarAd", "am", 0},
    {"FMemStVarAdFunc", "am", 0},
    {"FMemStVarUnk", "am", 0},
    {"FMemStVarUnkFunc", "am", 0},
    {"FMemStAdFunc", "am", 0},
    {"FMemStVarCopy", "am", 0},
    {"CastAdVar", "c", 0},
    {"New", "c", 0},
    {"NewIfNullRf", "c", 0},
    {"CVarBoolI2", "a", 0},
    {"CVarDateVar", "a", 0},
    {"CVarErrI4", "a", 0},
    {"CVarDate", "a", 0},
    {"CVarAryVarg", "a", 0},
    {"CStrVarVal", "a", 0},
    {"DestructOFrame", "ac", 0},
    {"ThisVCallUI1", "v", G_CALL},
    {"ThisVCallI2", "v", G_CALL},
    {"ThisVCallR4", "v", G_CALL},
    {"ThisVCallR8", "v", G_CALL},
    {"ThisVCallCy", "v", G_CALL},
    {"ThisVCallHidden", "v", G_CALL},
    {"ThisVCallCbFrame", "vn", G_CALL},
    {"StLsetFixStr", "n", 0},
    {"StFixedStrFree", "n", 0},
    {"VCallUI1", "v", G_CALL},
    {"VCallStr", "v", G_CALL},
    {"VCallR4", "v", G_CALL},
    {"VCallR8", "v", G_CALL},
    {"VCallCy", "v", G_CALL},
    {"VCallCbFrame", "vn", G_CALL},
    {"StFixedStrR", "n", 0},
    {"StFixedStrRFree", "n", 0},
    {"ImpAdCallUI1", "in", G_CALL},
    {"ImpAdCallR4", "in", G_CALL},
    {"ImpAdCallR8", "in", G_CALL},
    {"ImpAdCallCbFrame", "ainn", G_CALL},
    {"LdStkRf", "M", 0},
    {"LdFrameRf", "a", 0},
    {"LitVarUI1", "au", 0},
    {"SetVarVar", "", 0},
    {"SetVarVarFunc", "", 0},
    {"OpenFile", "n", 0},
    {"LockFile", "n", 0},
    {"EraseDestruct", "c", 0},
    {"ForUI1", "al", G_COND},
    {"ForI2", "al", G_COND},
    {"ForI4", "al", G_COND},
    {"ForR4", "al", G_COND},
    {"ForR8", "al", G_COND},
    {"ForCy", "al", G_COND},
    {"ForVar", "al", G_COND},
    {"ForStepUI1", "al", G_COND},
    {"ForStepI2", "al", G_COND},
    {"ForStepI4", "al", G_COND},
    {"ForStepR4", "al", G_COND},
    {"ForStepR8", "al", G_COND},
    {"ForStepCy", "al", G_COND},
    {"ForStepVar", "al", G_COND},
    {"ForEachCollVar", "al", G_COND},
    {"NextEachCollVar", "al", G_COND},
    {"ForEachCollAd", "al", G_COND},
    {"NextEachCollAd", "al", G_COND},
    {"ForEachAryVar", "anl", G_COND},
    {"NextEachAryVar", "anl", G_COND},
    {"NextUI1", "al", G_COND},
    {"NextR8", "al", G_COND},
    {"NextStepVar", "al", G_COND},
    {"NextStepUI1", "al", G_COND},
    {"ForEachCollObj", "acl", G_COND},
    {"ForEachVar", "al", G_COND},
    {"ForEachVarFree", "al", G_COND},
    {"NextEachCollObj", "acl", G_COND},
    {"NextEachVar", "al", G_COND},
    {"CheckType", "c", 0},
    {"Redim", "ntnn", 0},
    {"RedimPreserve", "ntnn", 0},
    {"RedimVar", "nn", 0},
    {"RedimPreserveVar", "nn", 0},
    {"FDupVar", "aa", 0},
    {"FDupStr", "aa", 0},
    {"OnGosub", "*:l", G_CALL | G_SWITCH},
    {"OnGoto", "*:l", G_SWITCH},
    {"AddRef", "", 0},
    {"LateMemCall", "cn", G_CALL},
    {"LateMemCallLdVar", "acn", G_CALL},
    {"LateMemSt", "c", G_CALL},
    {"LateMemCallSt", "cn", G_CALL},
    {"LateMemStAd", "cn", G_CALL},
    {"ExitProcFrameCb", "an", G_RET | G_STOP},
    {"ExitProcFrameCbStack", "an", G_RET | G_STOP},
    {"LateIdCall", "Dn", G_CALL},
    {"LateIdCallLdVar", "aDn", G_CALL},
    {"LateIdCallSt", "Dn", G_CALL},
    {"LateIdStAd", "Dn", G_CALL},
    {"LateMemNamedCall", "*cn:c", G_CALL},
    {"LateMemNamedCallLdVar", "*acn:c", G_CALL},
    {"LateMemNamedCallSt", "*cn:c", G_CALL},
    {"LateMemNamedStAd", "*cn:c", G_CALL},
    {"LateIdNamedCall", "*Dn:D", G_CALL},
    {"LateIdNamedCallLdVar", "*aDn:D", G_CALL},
    {"LateIdNamedCallSt", "*Dn:D", G_CALL},
    {"LateIdNamedStAd", "*Dn:D", G_CALL},
    {"VarIndexLdVar", "an", G_CALL},
    {"VarIndexLdRfVar", "an", G_CALL},
    {"VarIndexSt", "n", G_CALL},
    {"VarIndexStAd", "n", G_CALL},
    {"LitR8", "F", 0},
    {"LitR8FP", "F", 0},
    {"LitVarI4", "ad", 0},
    {"LitVarR4", "af", 0},
    {"LitVarCy", "ay", 0},
    {"LitVarR8", "aF", 0},
    {"LitVarDate", "aF", 0},
    {"CStrBool", "", 0},
    {"CStrDate", "", 0},
    {"CDateStr", "", 0},
    {"FreeStrNoPop", "", 0},
    {"FreeVarNoPop", "", 0},
    {"FreeAdNoPop", "", 0},
    {"EraseNoPop", "", 0},
    {"WMemLdUI1", "M", 0},
    {"WMemLdI2", "M", 0},
    {"WMemLdStr", "M", 0},
    {"WMemLdCy", "M", 0},
    {"WMemLdRfVar", "M", 0},
    {"WMemLdFPR4", "M", 0},
    {"WMemLdFPR8", "M", 0},
    {"WMemLdVar", "M", 0},
    {"WMemStUI1", "M", 0},
    {"WMemStI2", "M", 0},
    {"WMemStR4", "M", 0},
    {"WMemStR8", "M", 0},
    {"WMemStVar", "M", 0},
    {"WMemStStr", "M", 0},
    {"WMemStAd", "M", 0},
    {"WMemStFPR4", "M", 0},
    {"WMemStFPR8", "M", 0},
    {"WMemStVarAd", "M", 0},
    {"WMemStVarAdFunc", "M", 0},
    {"WMemStVarUnk", "M", 0},
    {"WMemStVarUnkFunc", "M", 0},
    {"WMemStAdFunc", "M", 0},
    {"WMemStVarCopy", "M", 0},
    {"WMemStStrCopy", "M", 0},
    {"VarIndexLdRfVarLock", "aan", G_CALL},
    {"AssignRecord", "c", 0},
    {"DestructAnsiOFrame", "ac", 0},
    {"FStVarZero", "a", 0},
    {"VerifyVarObj", "", 0},
    {"VerifyPVarObj", "", 0},
    {"FnInStrB4", "", 0},
    {"FnInStrB4Var", "a", 0},
    {"FnInStr4", "", 0},
    {"FnInStr4Var", "a", 0},
    {"FnStrComp3", "", 0},
    {"FnStrComp3Var", "a", 0},
    {"StAryMove", "", 0},
    {"StAryCopy", "", 0},
    {"StAryRecMove", "c", 0},
    {"StAryRecCopy", "c", 0},
    {"AryInRecLdPr", "cn", 0},
    {"AryInRecLdRf", "cn", 0},
    {"CExtInstUnk", "", 0},
    {"IStVarCopyObj", "a", 0},
    {"ArrayRebase1Var", "", 0},
    {"Assert", "", 0},
    {"RaiseEvent", "Dn", G_CALL},
    {"PrintObject", "cn", 0},
    {"PrintFile", "cn", 0},
    {"WriteFile", "cn", 0},
    {"InputFile", "cn", 0},
    {"GetRecFxStr3", "", 0},
    {"GetRecFxStr4", "", 0},
    {"PutRecFxStr3", "", 0},
    {"PutRecFxStr4", "", 0},
    {"GetRecOwn3", "c", 0},
    {"GetRecOwn4", "c", 0},
    {"PutRecOwn3", "c", 0},
    {"PutRecOwn4", "c", 0},
    {"CBoolVarNull", "", 0},
    {"ImpAdCallNonVirt", "in", G_CALL},
    {"DestructRecord", "c", 0},
    {"ZeroRetVal", "", 0},
    {"ZeroRetValVar", "", 0},
    {"ExitProcCbHresult", "an", G_RET | G_STOP},
    {"ExitProcFrameCbHresult", "aan", G_RET | G_STOP},
    {"EraseDestrKeepData", "c", 0},
    {"CDargRefUdt", "nc", 0},
    {"CVarRefUdt", "ac", 0},
    {"CVarUdt", "ac", 0},
    {"StUdtVar", "c", 0},
    {"StAryVar", "n", 0},
    {"CopyBytesZero", "n", 0},
    {"FLdZeroAry", "aa", 0},
    {"CVarAryUdt", "anc", 0},
    {"RedimVarUdt", "nnc", 0},
    {"RedimPreserveVarUdt", "nnc", 0},
    {"VarLateMemLdRfVar", "ac", G_CALL},
    {"VarLateMemCallLdRfVar", "acn", G_CALL},
    {"VarLateMemLdVar", "ac", G_CALL},
    {"VarLateMemCallLdVar", "acn", G_CALL},
    {"VarLateMemSt", "c", G_CALL},
    {"VarLateMemCallSt", "cn", G_CALL},
    {"VarLateMemStAd", "cn", G_CALL},
};

const vb_op xxbyte_vb_ops_primary[256] = {
    {XXBYTE_VB_ID_LARGEBOS, 2, 0}, /* 00 */
    {0, 0, 0}, /* 01 */
    {XXBYTE_VB_ID_LARGEBOS, 2, 0}, /* 02 */
    {0, 0, 0}, /* 03 */
    {XXBYTE_VB_ID_FLDRFVAR, 3, 0}, /* 04 */
    {XXBYTE_VB_ID_IMPADLDRF, 3, 0}, /* 05 */
    {XXBYTE_VB_ID_MEMLDRFVAR, 3, 0}, /* 06 */
    {XXBYTE_VB_ID_FMEMLDRF, 5, 0}, /* 07 */
    {XXBYTE_VB_ID_FLDPR, 3, 0}, /* 08 */
    {XXBYTE_VB_ID_IMPADCALLHRESULT, 5, 0}, /* 09 */
    {XXBYTE_VB_ID_IMPADCALLFPR4, 5, 0}, /* 0A */
    {XXBYTE_VB_ID_IMPADCALLI2, 5, 0}, /* 0B */
    {XXBYTE_VB_ID_IMPADCALLCY, 5, 0}, /* 0C */
    {XXBYTE_VB_ID_VCALLHRESULT, 5, 0}, /* 0D */
    {XXBYTE_VB_ID_VCALLFPR8, 3, 0}, /* 0E */
    {XXBYTE_VB_ID_VCALLAD, 3, 0}, /* 0F */
    {XXBYTE_VB_ID_THISVCALLHRESULT, 5, 0}, /* 10 */
    {XXBYTE_VB_ID_THISVCALL, 3, 0}, /* 11 */
    {XXBYTE_VB_ID_THISVCALLAD, 3, 0}, /* 12 */
    {XXBYTE_VB_ID_EXITPROCHRESULT, 1, 0}, /* 13 */
    {XXBYTE_VB_ID_EXITPROC, 1, 0}, /* 14 */
    {XXBYTE_VB_ID_EXITPROCI2, 1, 0}, /* 15 */
    {XXBYTE_VB_ID_EXITPROCR4, 1, 0}, /* 16 */
    {XXBYTE_VB_ID_EXITPROCR8, 1, 0}, /* 17 */
    {XXBYTE_VB_ID_EXITPROCCY, 1, 0}, /* 18 */
    {XXBYTE_VB_ID_FSTADFUNC, 3, 0}, /* 19 */
    {XXBYTE_VB_ID_FFREE1AD, 3, 0}, /* 1A */
    {XXBYTE_VB_ID_LITSTR, 3, 0}, /* 1B */
    {XXBYTE_VB_ID_BRANCHF, 3, 0}, /* 1C */
    {XXBYTE_VB_ID_BRANCHT, 3, 0}, /* 1D */
    {XXBYTE_VB_ID_BRANCH, 3, 0}, /* 1E */
    {XXBYTE_VB_ID_CREC2ANSI, 3, 0}, /* 1F */
    {XXBYTE_VB_ID_CREC2UNI, 3, 0}, /* 20 */
    {XXBYTE_VB_ID_FLDPRTHIS, 1, 0}, /* 21 */
    {XXBYTE_VB_ID_IMPADLDPR, 3, 0}, /* 22 */
    {XXBYTE_VB_ID_FSTSTRNOPOP, 3, 0}, /* 23 */
    {XXBYTE_VB_ID_NEWIFNULLPR, 3, 0}, /* 24 */
    {XXBYTE_VB_ID_POPADLDVAR, 1, 0}, /* 25 */
    {XXBYTE_VB_ID_ARYDESCTEMP, 3, 0}, /* 26 */
    {XXBYTE_VB_ID_LITVAR_MISSING, 3, 0}, /* 27 */
    {XXBYTE_VB_ID_LITVARI2, 5, 0}, /* 28 */
    {XXBYTE_VB_ID_FFREEAD, 0, 0}, /* 29 */
    {XXBYTE_VB_ID_CONCATSTR, 1, 0}, /* 2A */
    {XXBYTE_VB_ID_POPTMPLDAD2, 3, 0}, /* 2B */
    {XXBYTE_VB_ID_LATEIDST, 5, 0}, /* 2C */
    {XXBYTE_VB_ID_ARYUNLOCK, 3, 0}, /* 2D */
    {XXBYTE_VB_ID_ARYLOCK, 3, 0}, /* 2E */
    {XXBYTE_VB_ID_FFREE1STR, 3, 0}, /* 2F */
    {XXBYTE_VB_ID_POPTMPLDAD8, 3, 0}, /* 30 */
    {XXBYTE_VB_ID_FSTSTR, 3, 0}, /* 31 */
    {XXBYTE_VB_ID_FFREESTR, 0, 0}, /* 32 */
    {XXBYTE_VB_ID_LDFIXEDSTR, 3, 0}, /* 33 */
    {XXBYTE_VB_ID_CSTR2ANSI, 1, 0}, /* 34 */
    {XXBYTE_VB_ID_FFREE1VAR, 3, 0}, /* 35 */
    {XXBYTE_VB_ID_FFREEVAR, 0, 0}, /* 36 */
    {XXBYTE_VB_ID_POPFPR4, 1, 0}, /* 37 */
    {XXBYTE_VB_ID_COPYBYTES, 3, 0}, /* 38 */
    {XXBYTE_VB_ID_POPFPR8, 1, 0}, /* 39 */
    {XXBYTE_VB_ID_LITVARSTR, 5, 0}, /* 3A */
    {XXBYTE_VB_ID_ARY1STSTRCOPY, 1, 0}, /* 3B */
    {XXBYTE_VB_ID_SETLASTSYSTEMERROR, 1, 0}, /* 3C */
    {XXBYTE_VB_ID_CASTAD, 3, 0}, /* 3D */
    {XXBYTE_VB_ID_FLDZEROAD, 3, 0}, /* 3E */
    {XXBYTE_VB_ID_CVARCY, 3, 0}, /* 3F */
    {XXBYTE_VB_ID_ARY1LDRF, 1, 0}, /* 40 */
    {XXBYTE_VB_ID_ARY1LDPR, 1, 0}, /* 41 */
    {XXBYTE_VB_ID_CR4VAR, 1, 0}, /* 42 */
    {XXBYTE_VB_ID_FSTSTRCOPY, 3, 0}, /* 43 */
    {XXBYTE_VB_ID_CVARI2, 3, 0}, /* 44 */
    {XXBYTE_VB_ID_ERROR, 1, 0}, /* 45 */
    {XXBYTE_VB_ID_CVARSTR, 3, 0}, /* 46 */
    {XXBYTE_VB_ID_STFIXEDSTR, 3, 0}, /* 47 */
    {XXBYTE_VB_ID_ILDPR, 3, 0}, /* 48 */
    {XXBYTE_VB_ID_POPADLD4, 1, 0}, /* 49 */
    {XXBYTE_VB_ID_FNLENSTR, 1, 0}, /* 4A */
    {XXBYTE_VB_ID_ONERRORGOTO, 3, 0}, /* 4B */
    {XXBYTE_VB_ID_FNLBOUND, 1, 0}, /* 4C */
    {XXBYTE_VB_ID_CVARREF, 5, 0}, /* 4D */
    {XXBYTE_VB_ID_FSTVARCOPYOBJ, 3, 0}, /* 4E */
    {XXBYTE_VB_ID_MIDSTR, 3, 0}, /* 4F */
    {XXBYTE_VB_ID_CI4STR, 1, 0}, /* 50 */
    {XXBYTE_VB_ID_FLDZEROAD, 3, 0}, /* 51 */
    {XXBYTE_VB_ID_ARY1STVAR, 1, 0}, /* 52 */
    {XXBYTE_VB_ID_CBOOLCY, 1, 0}, /* 53 */
    {XXBYTE_VB_ID_FMEMSTSTRCOPY, 5, 0}, /* 54 */
    {XXBYTE_VB_ID_CI2VAR, 1, 0}, /* 55 */
    {XXBYTE_VB_ID_NEWIFNULLAD, 3, 0}, /* 56 */
    {XXBYTE_VB_ID_LATEMEMLDVAR, 5, 0}, /* 57 */
    {XXBYTE_VB_ID_MEMLDPR, 3, 0}, /* 58 */
    {XXBYTE_VB_ID_POPTMPLDADSTR, 3, 0}, /* 59 */
    {XXBYTE_VB_ID_ERASE, 1, 0}, /* 5A */
    {XXBYTE_VB_ID_FSTADFUNCNOPOP, 3, 0}, /* 5B */
    {XXBYTE_VB_ID_BRANCHFVAR, 3, 0}, /* 5C */
    {XXBYTE_VB_ID_HARDTYPE, 1, 0}, /* 5D */
    {XXBYTE_VB_ID_IMPADCALLI4, 5, 0}, /* 5E */
    {XXBYTE_VB_ID_FMEMLDPR, 5, 0}, /* 5F */
    {XXBYTE_VB_ID_CSTRVARTMP, 1, 0}, /* 60 */
    {XXBYTE_VB_ID_LATEIDLDVAR, 7, 0}, /* 61 */
    {XXBYTE_VB_ID_ISTDARG, 3, 0}, /* 62 */
    {XXBYTE_VB_ID_LITVAR_TRUE, 3, 0}, /* 63 */
    {XXBYTE_VB_ID_NEXTI2, 5, 0}, /* 64 */
    {XXBYTE_VB_ID_NEXTSTEPI2, 5, 0}, /* 65 */
    {XXBYTE_VB_ID_NEXTI4, 5, 0}, /* 66 */
    {XXBYTE_VB_ID_NEXTSTEPI4, 5, 0}, /* 67 */
    {XXBYTE_VB_ID_NEXTSTEPR4, 5, 0}, /* 68 */
    {XXBYTE_VB_ID_NEXTSTEPR8, 5, 0}, /* 69 */
    {XXBYTE_VB_ID_NEXTSTEPCY, 5, 0}, /* 6A */
    {XXBYTE_VB_ID_FLDI2, 3, 0}, /* 6B */
    {XXBYTE_VB_ID_ILDRF, 3, 0}, /* 6C */
    {XXBYTE_VB_ID_FLDR8, 3, 0}, /* 6D */
    {XXBYTE_VB_ID_FLDFPR4, 3, 0}, /* 6E */
    {XXBYTE_VB_ID_FLDFPR8, 3, 0}, /* 6F */
    {XXBYTE_VB_ID_FSTI2, 3, 0}, /* 70 */
    {XXBYTE_VB_ID_FSTR4, 3, 0}, /* 71 */
    {XXBYTE_VB_ID_FSTR8, 3, 0}, /* 72 */
    {XXBYTE_VB_ID_FSTFPR4, 3, 0}, /* 73 */
    {XXBYTE_VB_ID_FSTFPR8, 3, 0}, /* 74 */
    {XXBYTE_VB_ID_IMPADLDI2, 3, 0}, /* 75 */
    {XXBYTE_VB_ID_IMPADLDI4, 3, 0}, /* 76 */
    {XXBYTE_VB_ID_IMPADLDCY, 3, 0}, /* 77 */
    {XXBYTE_VB_ID_IMPADLDFPR4, 3, 0}, /* 78 */
    {XXBYTE_VB_ID_IMPADLDFPR8, 3, 0}, /* 79 */
    {XXBYTE_VB_ID_IMPADSTI2, 3, 0}, /* 7A */
    {XXBYTE_VB_ID_IMPADSTR4, 3, 0}, /* 7B */
    {XXBYTE_VB_ID_IMPADSTCY, 3, 0}, /* 7C */
    {XXBYTE_VB_ID_IMPADSTFPR4, 3, 0}, /* 7D */
    {XXBYTE_VB_ID_IMPADSTFPR8, 3, 0}, /* 7E */
    {XXBYTE_VB_ID_ILDI2, 3, 0}, /* 7F */
    {XXBYTE_VB_ID_ILDI4, 3, 0}, /* 80 */
    {XXBYTE_VB_ID_ILDR8, 3, 0}, /* 81 */
    {XXBYTE_VB_ID_ILDFPR4, 3, 0}, /* 82 */
    {XXBYTE_VB_ID_ILDFPR8, 3, 0}, /* 83 */
    {XXBYTE_VB_ID_ISTI2, 3, 0}, /* 84 */
    {XXBYTE_VB_ID_ISTI4, 3, 0}, /* 85 */
    {XXBYTE_VB_ID_ISTR8, 3, 0}, /* 86 */
    {XXBYTE_VB_ID_ISTFPR4, 3, 0}, /* 87 */
    {XXBYTE_VB_ID_ISTFPR8, 3, 0}, /* 88 */
    {XXBYTE_VB_ID_MEMLDI2, 3, 0}, /* 89 */
    {XXBYTE_VB_ID_MEMLDSTR, 3, 0}, /* 8A */
    {XXBYTE_VB_ID_MEMLDR8, 3, 0}, /* 8B */
    {XXBYTE_VB_ID_MEMLDFPR4, 3, 0}, /* 8C */
    {XXBYTE_VB_ID_MEMLDFPR8, 3, 0}, /* 8D */
    {XXBYTE_VB_ID_MEMSTI2, 3, 0}, /* 8E */
    {XXBYTE_VB_ID_MEMSTI4, 3, 0}, /* 8F */
    {XXBYTE_VB_ID_MEMSTR8, 3, 0}, /* 90 */
    {XXBYTE_VB_ID_MEMSTFPR4, 3, 0}, /* 91 */
    {XXBYTE_VB_ID_MEMSTFPR8, 3, 0}, /* 92 */
    {XXBYTE_VB_ID_FMEMLDI2, 5, 0}, /* 93 */
    {XXBYTE_VB_ID_FMEMLDR4, 5, 0}, /* 94 */
    {XXBYTE_VB_ID_FMEMLDCY, 5, 0}, /* 95 */
    {XXBYTE_VB_ID_FMEMLDFPR4, 5, 0}, /* 96 */
    {XXBYTE_VB_ID_FMEMLDFPR8, 5, 0}, /* 97 */
    {XXBYTE_VB_ID_FMEMSTI2, 5, 0}, /* 98 */
    {XXBYTE_VB_ID_FMEMSTI4, 5, 0}, /* 99 */
    {XXBYTE_VB_ID_FMEMSTR8, 5, 0}, /* 9A */
    {XXBYTE_VB_ID_FMEMSTFPR4, 5, 0}, /* 9B */
    {XXBYTE_VB_ID_FMEMSTFPR8, 5, 0}, /* 9C */
    {XXBYTE_VB_ID_ARY1LDI2, 1, 0}, /* 9D */
    {XXBYTE_VB_ID_ARY1LDI4, 1, 0}, /* 9E */
    {XXBYTE_VB_ID_ARY1LDCY, 1, 0}, /* 9F */
    {XXBYTE_VB_ID_ARY1LDFPR4, 1, 0}, /* A0 */
    {XXBYTE_VB_ID_ARY1LDFPR8, 1, 0}, /* A1 */
    {XXBYTE_VB_ID_ARY1STI2, 1, 0}, /* A2 */
    {XXBYTE_VB_ID_ARY1STI4, 1, 0}, /* A3 */
    {XXBYTE_VB_ID_ARY1STCY, 1, 0}, /* A4 */
    {XXBYTE_VB_ID_ARY1STFPR4, 1, 0}, /* A5 */
    {XXBYTE_VB_ID_ARY1STFPR8, 1, 0}, /* A6 */
    {XXBYTE_VB_ID_ARYLDPR, 3, 0}, /* A7 */
    {XXBYTE_VB_ID_ARYLDRF, 3, 0}, /* A8 */
    {XXBYTE_VB_ID_ADDI2, 1, 0}, /* A9 */
    {XXBYTE_VB_ID_ADDI4, 1, 0}, /* AA */
    {XXBYTE_VB_ID_ADDR8, 1, 0}, /* AB */
    {XXBYTE_VB_ID_ADDCY, 1, 0}, /* AC */
    {XXBYTE_VB_ID_SUBI2, 1, 0}, /* AD */
    {XXBYTE_VB_ID_SUBI4, 1, 0}, /* AE */
    {XXBYTE_VB_ID_SUBR4, 1, 0}, /* AF */
    {XXBYTE_VB_ID_SUBCY, 1, 0}, /* B0 */
    {XXBYTE_VB_ID_MULI2, 1, 0}, /* B1 */
    {XXBYTE_VB_ID_MULI4, 1, 0}, /* B2 */
    {XXBYTE_VB_ID_MULR8, 1, 0}, /* B3 */
    {XXBYTE_VB_ID_MULCY, 1, 0}, /* B4 */
    {XXBYTE_VB_ID_MULCYI2, 1, 0}, /* B5 */
    {XXBYTE_VB_ID_DIVR8, 1, 0}, /* B6 */
    {XXBYTE_VB_ID_UMII2, 1, 0}, /* B7 */
    {XXBYTE_VB_ID_UMII4, 1, 0}, /* B8 */
    {XXBYTE_VB_ID_UMIR8, 1, 0}, /* B9 */
    {XXBYTE_VB_ID_UMICY, 1, 0}, /* BA */
    {XXBYTE_VB_ID_FNABSI2, 1, 0}, /* BB */
    {XXBYTE_VB_ID_FNABSI4, 1, 0}, /* BC */
    {XXBYTE_VB_ID_FNABSR4, 1, 0}, /* BD */
    {XXBYTE_VB_ID_FNABSCY, 1, 0}, /* BE */
    {XXBYTE_VB_ID_IDVI2, 1, 0}, /* BF */
    {XXBYTE_VB_ID_IDVI4, 1, 0}, /* C0 */
    {XXBYTE_VB_ID_MODI2, 1, 0}, /* C1 */
    {XXBYTE_VB_ID_MODI4, 1, 0}, /* C2 */
    {XXBYTE_VB_ID_NOTI4, 1, 0}, /* C3 */
    {XXBYTE_VB_ID_ANDI4, 1, 0}, /* C4 */
    {XXBYTE_VB_ID_ORI4, 1, 0}, /* C5 */
    {XXBYTE_VB_ID_EQI2, 1, 0}, /* C6 */
    {XXBYTE_VB_ID_EQI4, 1, 0}, /* C7 */
    {XXBYTE_VB_ID_EQR4, 1, 0}, /* C8 */
    {XXBYTE_VB_ID_EQCY, 1, 0}, /* C9 */
    {XXBYTE_VB_ID_EQCYR8, 1, 0}, /* CA */
    {XXBYTE_VB_ID_NEI2, 1, 0}, /* CB */
    {XXBYTE_VB_ID_NEI4, 1, 0}, /* CC */
    {XXBYTE_VB_ID_NER8, 1, 0}, /* CD */
    {XXBYTE_VB_ID_NECY, 1, 0}, /* CE */
    {XXBYTE_VB_ID_NECYR8, 1, 0}, /* CF */
    {XXBYTE_VB_ID_LTI2, 1, 0}, /* D0 */
    {XXBYTE_VB_ID_LTI4, 1, 0}, /* D1 */
    {XXBYTE_VB_ID_LTR8, 1, 0}, /* D2 */
    {XXBYTE_VB_ID_LTCY, 1, 0}, /* D3 */
    {XXBYTE_VB_ID_LTCYR8, 1, 0}, /* D4 */
    {XXBYTE_VB_ID_LEI2, 1, 0}, /* D5 */
    {XXBYTE_VB_ID_LEI4, 1, 0}, /* D6 */
    {XXBYTE_VB_ID_LER8, 1, 0}, /* D7 */
    {XXBYTE_VB_ID_LECY, 1, 0}, /* D8 */
    {XXBYTE_VB_ID_LECYR8, 1, 0}, /* D9 */
    {XXBYTE_VB_ID_GTI2, 1, 0}, /* DA */
    {XXBYTE_VB_ID_GTI4, 1, 0}, /* DB */
    {XXBYTE_VB_ID_GTR4, 1, 0}, /* DC */
    {XXBYTE_VB_ID_GTCY, 1, 0}, /* DD */
    {XXBYTE_VB_ID_GTCYR8, 1, 0}, /* DE */
    {XXBYTE_VB_ID_GEI2, 1, 0}, /* DF */
    {XXBYTE_VB_ID_GEI4, 1, 0}, /* E0 */
    {XXBYTE_VB_ID_GER8, 1, 0}, /* E1 */
    {XXBYTE_VB_ID_GECY, 1, 0}, /* E2 */
    {XXBYTE_VB_ID_GECYR8, 1, 0}, /* E3 */
    {XXBYTE_VB_ID_CI2I4, 1, 0}, /* E4 */
    {XXBYTE_VB_ID_CI2R8, 1, 0}, /* E5 */
    {XXBYTE_VB_ID_CI2CY, 1, 0}, /* E6 */
    {XXBYTE_VB_ID_CI4UI1, 1, 0}, /* E7 */
    {XXBYTE_VB_ID_CI4R8, 1, 0}, /* E8 */
    {XXBYTE_VB_ID_CI4CY, 1, 0}, /* E9 */
    {XXBYTE_VB_ID_CR4R4, 1, 0}, /* EA */
    {XXBYTE_VB_ID_CR8I2, 1, 0}, /* EB */
    {XXBYTE_VB_ID_CR8I4, 1, 0}, /* EC */
    {XXBYTE_VB_ID_CR8R8, 1, 0}, /* ED */
    {XXBYTE_VB_ID_CR8CY, 1, 0}, /* EE */
    {XXBYTE_VB_ID_CCYI2, 1, 0}, /* EF */
    {XXBYTE_VB_ID_CCYI4, 1, 0}, /* F0 */
    {XXBYTE_VB_ID_CCYR4, 1, 0}, /* F1 */
    {XXBYTE_VB_ID_CDATER8, 1, 0}, /* F2 */
    {XXBYTE_VB_ID_LITI2, 3, 0}, /* F3 */
    {XXBYTE_VB_ID_LITI2_BYTE, 2, 0}, /* F4 */
    {XXBYTE_VB_ID_LITI4, 5, 0}, /* F5 */
    {XXBYTE_VB_ID_LITCY, 9, 0}, /* F6 */
    {XXBYTE_VB_ID_LITCY4, 5, 0}, /* F7 */
    {XXBYTE_VB_ID_LITI2FP, 3, 0}, /* F8 */
    {XXBYTE_VB_ID_LITR4FP, 5, 0}, /* F9 */
    {XXBYTE_VB_ID_LITDATE, 9, 0}, /* FA */
    {0, 0, 0}, /* FB */
    {0, 0, 0}, /* FC */
    {0, 0, 0}, /* FD */
    {0, 0, 0}, /* FE */
    {0, 0, 0}, /* FF */
};

const vb_op xxbyte_vb_ops_lead[5][256] = {
    {
        {0, 0, 0}, /* FB00 */
        {XXBYTE_VB_ID_IMPUI1, 2, 0}, /* FB01 */
        {XXBYTE_VB_ID_IMPI4, 2, 0}, /* FB02 */
        {XXBYTE_VB_ID_IMPI4, 2, 0}, /* FB03 */
        {0, 0, 0}, /* FB04 */
        {0, 0, 0}, /* FB05 */
        {0, 0, 0}, /* FB06 */
        {XXBYTE_VB_ID_IMPVAR, 4, 0}, /* FB07 */
        {0, 0, 0}, /* FB08 */
        {XXBYTE_VB_ID_EQVUI1, 2, 0}, /* FB09 */
        {XXBYTE_VB_ID_EQVI4, 2, 0}, /* FB0A */
        {XXBYTE_VB_ID_EQVI4, 2, 0}, /* FB0B */
        {0, 0, 0}, /* FB0C */
        {0, 0, 0}, /* FB0D */
        {0, 0, 0}, /* FB0E */
        {XXBYTE_VB_ID_EQVVAR, 4, 0}, /* FB0F */
        {0, 0, 0}, /* FB10 */
        {XXBYTE_VB_ID_XORI4, 2, 0}, /* FB11 */
        {XXBYTE_VB_ID_XORI4, 2, 0}, /* FB12 */
        {XXBYTE_VB_ID_XORI4, 2, 0}, /* FB13 */
        {0, 0, 0}, /* FB14 */
        {0, 0, 0}, /* FB15 */
        {0, 0, 0}, /* FB16 */
        {XXBYTE_VB_ID_XORVAR, 4, 0}, /* FB17 */
        {0, 0, 0}, /* FB18 */
        {XXBYTE_VB_ID_ORI2, 2, 0}, /* FB19 */
        {XXBYTE_VB_ID_ORI2, 2, 0}, /* FB1A */
        {XXBYTE_VB_ID_ORI2, 2, 0}, /* FB1B */
        {0, 0, 0}, /* FB1C */
        {0, 0, 0}, /* FB1D */
        {0, 0, 0}, /* FB1E */
        {XXBYTE_VB_ID_ORVAR, 4, 0}, /* FB1F */
        {0, 0, 0}, /* FB20 */
        {XXBYTE_VB_ID_ANDUI1, 2, 0}, /* FB21 */
        {XXBYTE_VB_ID_ANDUI1, 2, 0}, /* FB22 */
        {XXBYTE_VB_ID_ANDUI1, 2, 0}, /* FB23 */
        {0, 0, 0}, /* FB24 */
        {0, 0, 0}, /* FB25 */
        {0, 0, 0}, /* FB26 */
        {XXBYTE_VB_ID_ANDVAR, 4, 0}, /* FB27 */
        {0, 0, 0}, /* FB28 */
        {XXBYTE_VB_ID_EQI2, 2, 0}, /* FB29 */
        {XXBYTE_VB_ID_EQI2, 2, 0}, /* FB2A */
        {XXBYTE_VB_ID_EQI4, 2, 0}, /* FB2B */
        {XXBYTE_VB_ID_EQR8, 2, 0}, /* FB2C */
        {XXBYTE_VB_ID_EQR8, 2, 0}, /* FB2D */
        {XXBYTE_VB_ID_EQCY, 2, 0}, /* FB2E */
        {XXBYTE_VB_ID_EQVAR, 4, 0}, /* FB2F */
        {XXBYTE_VB_ID_EQSTR, 2, 0}, /* FB30 */
        {XXBYTE_VB_ID_EQTEXTVAR, 4, 0}, /* FB31 */
        {XXBYTE_VB_ID_EQTEXTSTR, 2, 0}, /* FB32 */
        {XXBYTE_VB_ID_EQVARBOOL, 2, 0}, /* FB33 */
        {XXBYTE_VB_ID_EQTEXTVARBOOL, 2, 0}, /* FB34 */
        {XXBYTE_VB_ID_EQCYR8, 2, 0}, /* FB35 */
        {XXBYTE_VB_ID_NEUI1, 2, 0}, /* FB36 */
        {XXBYTE_VB_ID_NEUI1, 2, 0}, /* FB37 */
        {XXBYTE_VB_ID_NEI4, 2, 0}, /* FB38 */
        {XXBYTE_VB_ID_NER4, 2, 0}, /* FB39 */
        {XXBYTE_VB_ID_NER4, 2, 0}, /* FB3A */
        {XXBYTE_VB_ID_NECY, 2, 0}, /* FB3B */
        {XXBYTE_VB_ID_NEVAR, 4, 0}, /* FB3C */
        {XXBYTE_VB_ID_NESTR, 2, 0}, /* FB3D */
        {XXBYTE_VB_ID_NETEXTVAR, 4, 0}, /* FB3E */
        {XXBYTE_VB_ID_NETEXTSTR, 2, 0}, /* FB3F */
        {XXBYTE_VB_ID_NEVARBOOL, 2, 0}, /* FB40 */
        {XXBYTE_VB_ID_NETEXTVARBOOL, 2, 0}, /* FB41 */
        {XXBYTE_VB_ID_NECYR8, 2, 0}, /* FB42 */
        {XXBYTE_VB_ID_LEUI1, 2, 0}, /* FB43 */
        {XXBYTE_VB_ID_LEI2, 2, 0}, /* FB44 */
        {XXBYTE_VB_ID_LEI4, 2, 0}, /* FB45 */
        {XXBYTE_VB_ID_LER4, 2, 0}, /* FB46 */
        {XXBYTE_VB_ID_LER4, 2, 0}, /* FB47 */
        {XXBYTE_VB_ID_LECY, 2, 0}, /* FB48 */
        {XXBYTE_VB_ID_LEVAR, 4, 0}, /* FB49 */
        {XXBYTE_VB_ID_LESTR, 2, 0}, /* FB4A */
        {XXBYTE_VB_ID_LETEXTVAR, 4, 0}, /* FB4B */
        {XXBYTE_VB_ID_LETEXTSTR, 2, 0}, /* FB4C */
        {XXBYTE_VB_ID_LEVARBOOL, 2, 0}, /* FB4D */
        {XXBYTE_VB_ID_LETEXTVARBOOL, 2, 0}, /* FB4E */
        {XXBYTE_VB_ID_LECYR8, 2, 0}, /* FB4F */
        {XXBYTE_VB_ID_GEUI1, 2, 0}, /* FB50 */
        {XXBYTE_VB_ID_GEI2, 2, 0}, /* FB51 */
        {XXBYTE_VB_ID_GEI4, 2, 0}, /* FB52 */
        {XXBYTE_VB_ID_GER4, 2, 0}, /* FB53 */
        {XXBYTE_VB_ID_GER4, 2, 0}, /* FB54 */
        {XXBYTE_VB_ID_GECY, 2, 0}, /* FB55 */
        {XXBYTE_VB_ID_GEVAR, 4, 0}, /* FB56 */
        {XXBYTE_VB_ID_GESTR, 2, 0}, /* FB57 */
        {XXBYTE_VB_ID_GETEXTVAR, 4, 0}, /* FB58 */
        {XXBYTE_VB_ID_GETEXTSTR, 2, 0}, /* FB59 */
        {XXBYTE_VB_ID_GEVARBOOL, 2, 0}, /* FB5A */
        {XXBYTE_VB_ID_GETEXTVARBOOL, 2, 0}, /* FB5B */
        {XXBYTE_VB_ID_GECYR8, 2, 0}, /* FB5C */
        {XXBYTE_VB_ID_LTUI1, 2, 0}, /* FB5D */
        {XXBYTE_VB_ID_LTI2, 2, 0}, /* FB5E */
        {XXBYTE_VB_ID_LTI4, 2, 0}, /* FB5F */
        {XXBYTE_VB_ID_LTR4, 2, 0}, /* FB60 */
        {XXBYTE_VB_ID_LTR4, 2, 0}, /* FB61 */
        {XXBYTE_VB_ID_LTCY, 2, 0}, /* FB62 */
        {XXBYTE_VB_ID_LTVAR, 4, 0}, /* FB63 */
        {XXBYTE_VB_ID_LTSTR, 2, 0}, /* FB64 */
        {XXBYTE_VB_ID_LTTEXTVAR, 4, 0}, /* FB65 */
        {XXBYTE_VB_ID_LTTEXTSTR, 2, 0}, /* FB66 */
        {XXBYTE_VB_ID_LTVARBOOL, 2, 0}, /* FB67 */
        {XXBYTE_VB_ID_LTTEXTVARBOOL, 2, 0}, /* FB68 */
        {XXBYTE_VB_ID_LTCYR8, 2, 0}, /* FB69 */
        {XXBYTE_VB_ID_GTUI1, 2, 0}, /* FB6A */
        {XXBYTE_VB_ID_GTI2, 2, 0}, /* FB6B */
        {XXBYTE_VB_ID_GTI4, 2, 0}, /* FB6C */
        {XXBYTE_VB_ID_GTR4, 2, 0}, /* FB6D */
        {XXBYTE_VB_ID_GTR4, 2, 0}, /* FB6E */
        {XXBYTE_VB_ID_GTCY, 2, 0}, /* FB6F */
        {XXBYTE_VB_ID_GTVAR, 4, 0}, /* FB70 */
        {XXBYTE_VB_ID_GTSTR, 2, 0}, /* FB71 */
        {XXBYTE_VB_ID_GTTEXTVAR, 4, 0}, /* FB72 */
        {XXBYTE_VB_ID_GTTEXTSTR, 2, 0}, /* FB73 */
        {XXBYTE_VB_ID_GTVARBOOL, 2, 0}, /* FB74 */
        {XXBYTE_VB_ID_GTTEXTVARBOOL, 2, 0}, /* FB75 */
        {XXBYTE_VB_ID_GTCYR8, 2, 0}, /* FB76 */
        {0, 0, 0}, /* FB77 */
        {0, 0, 0}, /* FB78 */
        {0, 0, 0}, /* FB79 */
        {0, 0, 0}, /* FB7A */
        {0, 0, 0}, /* FB7B */
        {0, 0, 0}, /* FB7C */
        {XXBYTE_VB_ID_LIKEVAR, 4, 0}, /* FB7D */
        {XXBYTE_VB_ID_LIKESTR, 2, 0}, /* FB7E */
        {XXBYTE_VB_ID_LIKETEXTVAR, 4, 0}, /* FB7F */
        {XXBYTE_VB_ID_LIKETEXTSTR, 2, 0}, /* FB80 */
        {XXBYTE_VB_ID_LIKEVARBOOL, 2, 0}, /* FB81 */
        {XXBYTE_VB_ID_LIKETEXTVARBOOL, 2, 0}, /* FB82 */
        {0, 0, 0}, /* FB83 */
        {XXBYTE_VB_ID_BETWEENUI1, 2, 0}, /* FB84 */
        {XXBYTE_VB_ID_BETWEENI2, 2, 0}, /* FB85 */
        {XXBYTE_VB_ID_BETWEENI4, 2, 0}, /* FB86 */
        {XXBYTE_VB_ID_BETWEENR4, 2, 0}, /* FB87 */
        {XXBYTE_VB_ID_BETWEENR4, 2, 0}, /* FB88 */
        {XXBYTE_VB_ID_BETWEENCY, 2, 0}, /* FB89 */
        {XXBYTE_VB_ID_BETWEENVAR, 2, 0}, /* FB8A */
        {XXBYTE_VB_ID_BETWEENSTR, 2, 0}, /* FB8B */
        {XXBYTE_VB_ID_BETWEENTEXTVAR, 2, 0}, /* FB8C */
        {XXBYTE_VB_ID_BETWEENTEXTSTR, 2, 0}, /* FB8D */
        {XXBYTE_VB_ID_ADDUI1, 2, 0}, /* FB8E */
        {XXBYTE_VB_ID_ADDI2, 2, 0}, /* FB8F */
        {XXBYTE_VB_ID_ADDI4, 2, 0}, /* FB90 */
        {XXBYTE_VB_ID_ADDR4, 2, 0}, /* FB91 */
        {XXBYTE_VB_ID_ADDR4, 2, 0}, /* FB92 */
        {XXBYTE_VB_ID_ADDCY, 2, 0}, /* FB93 */
        {XXBYTE_VB_ID_ADDVAR, 4, 0}, /* FB94 */
        {0, 0, 0}, /* FB95 */
        {XXBYTE_VB_ID_SUBUI1, 2, 0}, /* FB96 */
        {XXBYTE_VB_ID_SUBI2, 2, 0}, /* FB97 */
        {XXBYTE_VB_ID_SUBI4, 2, 0}, /* FB98 */
        {XXBYTE_VB_ID_SUBR4, 2, 0}, /* FB99 */
        {XXBYTE_VB_ID_SUBR4, 2, 0}, /* FB9A */
        {XXBYTE_VB_ID_SUBCY, 2, 0}, /* FB9B */
        {XXBYTE_VB_ID_SUBVAR, 4, 0}, /* FB9C */
        {0, 0, 0}, /* FB9D */
        {XXBYTE_VB_ID_MODUI1, 2, 0}, /* FB9E */
        {XXBYTE_VB_ID_MODI2, 2, 0}, /* FB9F */
        {XXBYTE_VB_ID_MODI4, 2, 0}, /* FBA0 */
        {0, 0, 0}, /* FBA1 */
        {0, 0, 0}, /* FBA2 */
        {0, 0, 0}, /* FBA3 */
        {XXBYTE_VB_ID_MODVAR, 4, 0}, /* FBA4 */
        {0, 0, 0}, /* FBA5 */
        {XXBYTE_VB_ID_IDVUI1, 2, 0}, /* FBA6 */
        {XXBYTE_VB_ID_IDVI2, 2, 0}, /* FBA7 */
        {XXBYTE_VB_ID_IDVI4, 2, 0}, /* FBA8 */
        {0, 0, 0}, /* FBA9 */
        {0, 0, 0}, /* FBAA */
        {0, 0, 0}, /* FBAB */
        {XXBYTE_VB_ID_IDVVAR, 4, 0}, /* FBAC */
        {0, 0, 0}, /* FBAD */
        {XXBYTE_VB_ID_MULUI1, 2, 0}, /* FBAE */
        {XXBYTE_VB_ID_MULI2, 2, 0}, /* FBAF */
        {XXBYTE_VB_ID_MULI4, 2, 0}, /* FBB0 */
        {XXBYTE_VB_ID_MULR4, 2, 0}, /* FBB1 */
        {XXBYTE_VB_ID_MULR4, 2, 0}, /* FBB2 */
        {XXBYTE_VB_ID_MULCY, 2, 0}, /* FBB3 */
        {XXBYTE_VB_ID_MULVAR, 4, 0}, /* FBB4 */
        {0, 0, 0}, /* FBB5 */
        {0, 0, 0}, /* FBB6 */
        {0, 0, 0}, /* FBB7 */
        {0, 0, 0}, /* FBB8 */
        {XXBYTE_VB_ID_DIVR8, 2, 0}, /* FBB9 */
        {XXBYTE_VB_ID_DIVR8, 2, 0}, /* FBBA */
        {0, 0, 0}, /* FBBB */
        {XXBYTE_VB_ID_DIVVAR, 4, 0}, /* FBBC */
        {0, 0, 0}, /* FBBD */
        {XXBYTE_VB_ID_NOTUI1, 2, 0}, /* FBBE */
        {XXBYTE_VB_ID_NOTI4, 2, 0}, /* FBBF */
        {XXBYTE_VB_ID_NOTI4, 2, 0}, /* FBC0 */
        {0, 0, 0}, /* FBC1 */
        {0, 0, 0}, /* FBC2 */
        {0, 0, 0}, /* FBC3 */
        {XXBYTE_VB_ID_NOTVAR, 4, 0}, /* FBC4 */
        {0, 0, 0}, /* FBC5 */
        {0, 0, 0}, /* FBC6 */
        {XXBYTE_VB_ID_UMII2, 2, 0}, /* FBC7 */
        {XXBYTE_VB_ID_UMII2, 2, 0}, /* FBC8 */
        {XXBYTE_VB_ID_UMIR4, 2, 0}, /* FBC9 */
        {XXBYTE_VB_ID_UMIR4, 2, 0}, /* FBCA */
        {XXBYTE_VB_ID_UMICY, 2, 0}, /* FBCB */
        {XXBYTE_VB_ID_UMIVAR, 4, 0}, /* FBCC */
        {0, 0, 0}, /* FBCD */
        {XXBYTE_VB_ID_PWRVAR, 4, 0}, /* FBCE */
        {XXBYTE_VB_ID_PWRR8R8, 2, 0}, /* FBCF */
        {XXBYTE_VB_ID_PWRR8I2, 2, 0}, /* FBD0 */
        {XXBYTE_VB_ID_MULCYI2, 2, 0}, /* FBD1 */
        {XXBYTE_VB_ID_IS, 2, 0}, /* FBD2 */
        {0, 0, 0}, /* FBD3 */
        {XXBYTE_VB_ID_FNABSI2, 2, 0}, /* FBD4 */
        {XXBYTE_VB_ID_FNABSI4, 2, 0}, /* FBD5 */
        {XXBYTE_VB_ID_FNABSR4, 2, 0}, /* FBD6 */
        {XXBYTE_VB_ID_FNABSR4, 2, 0}, /* FBD7 */
        {XXBYTE_VB_ID_FNABSCY, 2, 0}, /* FBD8 */
        {XXBYTE_VB_ID_FNABSVAR, 4, 0}, /* FBD9 */
        {0, 0, 0}, /* FBDA */
        {0, 0, 0}, /* FBDB */
        {0, 0, 0}, /* FBDC */
        {0, 0, 0}, /* FBDD */
        {XXBYTE_VB_ID_FNFIXR8, 2, 0}, /* FBDE */
        {XXBYTE_VB_ID_FNFIXR8, 2, 0}, /* FBDF */
        {XXBYTE_VB_ID_FNFIXCY, 2, 0}, /* FBE0 */
        {XXBYTE_VB_ID_FNFIXVAR, 4, 0}, /* FBE1 */
        {0, 0, 0}, /* FBE2 */
        {0, 0, 0}, /* FBE3 */
        {0, 0, 0}, /* FBE4 */
        {0, 0, 0}, /* FBE5 */
        {XXBYTE_VB_ID_FNINTR8, 2, 0}, /* FBE6 */
        {XXBYTE_VB_ID_FNINTR8, 2, 0}, /* FBE7 */
        {XXBYTE_VB_ID_FNINTCY, 2, 0}, /* FBE8 */
        {XXBYTE_VB_ID_FNINTVAR, 4, 0}, /* FBE9 */
        {0, 0, 0}, /* FBEA */
        {XXBYTE_VB_ID_FNLENVAR, 4, 0}, /* FBEB */
        {XXBYTE_VB_ID_FNLENSTR, 2, 0}, /* FBEC */
        {XXBYTE_VB_ID_FNLENBVAR, 4, 0}, /* FBED */
        {XXBYTE_VB_ID_FNLENBSTR, 2, 0}, /* FBEE */
        {XXBYTE_VB_ID_CONCATVAR, 4, 0}, /* FBEF */
        {XXBYTE_VB_ID_CONCATSTR, 2, 0}, /* FBF0 */
        {0, 0, 0}, /* FBF1 */
        {XXBYTE_VB_ID_FNSGNUI1, 2, 0}, /* FBF2 */
        {XXBYTE_VB_ID_FNSGNUI1, 2, 0}, /* FBF3 */
        {XXBYTE_VB_ID_FNSGNI4, 2, 0}, /* FBF4 */
        {XXBYTE_VB_ID_FNSGNR8, 2, 0}, /* FBF5 */
        {XXBYTE_VB_ID_FNSGNR4, 2, 0}, /* FBF6 */
        {XXBYTE_VB_ID_FNSGNCY, 2, 0}, /* FBF7 */
        {0, 0, 0}, /* FBF8 */
        {0, 0, 0}, /* FBF9 */
        {XXBYTE_VB_ID_SEEKFILE, 2, 0}, /* FBFA */
        {XXBYTE_VB_ID_NAMEFILE, 2, 0}, /* FBFB */
        {XXBYTE_VB_ID_CSTRI2, 2, 0}, /* FBFC */
        {XXBYTE_VB_ID_CSTRUI1, 2, 0}, /* FBFD */
        {XXBYTE_VB_ID_CSTRI4, 2, 0}, /* FBFE */
        {XXBYTE_VB_ID_CSTRR4, 2, 0}, /* FBFF */
    },
    {
        {XXBYTE_VB_ID_CSTRR8, 2, 0}, /* FC00 */
        {XXBYTE_VB_ID_CSTRCY, 2, 0}, /* FC01 */
        {XXBYTE_VB_ID_CSTRVAR, 2, 0}, /* FC02 */
        {0, 0, 0}, /* FC03 */
        {XXBYTE_VB_ID_CCYI2, 2, 0}, /* FC04 */
        {XXBYTE_VB_ID_CCYI2, 2, 0}, /* FC05 */
        {XXBYTE_VB_ID_CCYI4, 2, 0}, /* FC06 */
        {XXBYTE_VB_ID_CCYR4, 2, 0}, /* FC07 */
        {XXBYTE_VB_ID_CCYR4, 2, 0}, /* FC08 */
        {0, 0, 0}, /* FC09 */
        {XXBYTE_VB_ID_CCYVAR, 2, 0}, /* FC0A */
        {XXBYTE_VB_ID_CCYSTR, 2, 0}, /* FC0B */
        {0, 0, 0}, /* FC0C */
        {XXBYTE_VB_ID_CUI1I2, 2, 0}, /* FC0D */
        {XXBYTE_VB_ID_CUI1I4, 2, 0}, /* FC0E */
        {XXBYTE_VB_ID_CUI1R4, 2, 0}, /* FC0F */
        {XXBYTE_VB_ID_CUI1R4, 2, 0}, /* FC10 */
        {XXBYTE_VB_ID_CUI1CY, 2, 0}, /* FC11 */
        {XXBYTE_VB_ID_CUI1VAR, 2, 0}, /* FC12 */
        {XXBYTE_VB_ID_CUI1STR, 2, 0}, /* FC13 */
        {XXBYTE_VB_ID_CI2UI1, 2, 0}, /* FC14 */
        {0, 0, 0}, /* FC15 */
        {XXBYTE_VB_ID_CI2I4, 2, 0}, /* FC16 */
        {XXBYTE_VB_ID_CI2R8, 2, 0}, /* FC17 */
        {XXBYTE_VB_ID_CI2R8, 2, 0}, /* FC18 */
        {XXBYTE_VB_ID_CI2CY, 2, 0}, /* FC19 */
        {XXBYTE_VB_ID_CI2VAR, 2, 0}, /* FC1A */
        {XXBYTE_VB_ID_CI2STR, 2, 0}, /* FC1B */
        {XXBYTE_VB_ID_CI4UI1, 2, 0}, /* FC1C */
        {XXBYTE_VB_ID_CI4UI1, 2, 0}, /* FC1D */
        {0, 0, 0}, /* FC1E */
        {XXBYTE_VB_ID_CI4R8, 2, 0}, /* FC1F */
        {XXBYTE_VB_ID_CI4R8, 2, 0}, /* FC20 */
        {XXBYTE_VB_ID_CI4CY, 2, 0}, /* FC21 */
        {XXBYTE_VB_ID_CI4VAR, 2, 0}, /* FC22 */
        {XXBYTE_VB_ID_CI4STR, 2, 0}, /* FC23 */
        {XXBYTE_VB_ID_FNCSNGI2, 2, 0}, /* FC24 */
        {XXBYTE_VB_ID_FNCSNGI2, 2, 0}, /* FC25 */
        {XXBYTE_VB_ID_CR4I4, 2, 0}, /* FC26 */
        {XXBYTE_VB_ID_CR4R8, 2, 0}, /* FC27 */
        {XXBYTE_VB_ID_CR4R8, 2, 0}, /* FC28 */
        {XXBYTE_VB_ID_CR8CY, 2, 0}, /* FC29 */
        {XXBYTE_VB_ID_CR8VAR, 2, 0}, /* FC2A */
        {XXBYTE_VB_ID_CR4STR, 2, 0}, /* FC2B */
        {XXBYTE_VB_ID_FNCSNGI2, 2, 0}, /* FC2C */
        {XXBYTE_VB_ID_FNCSNGI2, 2, 0}, /* FC2D */
        {XXBYTE_VB_ID_CR4I4, 2, 0}, /* FC2E */
        {XXBYTE_VB_ID_CR8R4, 2, 0}, /* FC2F */
        {XXBYTE_VB_ID_CR8R4, 2, 0}, /* FC30 */
        {XXBYTE_VB_ID_CR8CY, 2, 0}, /* FC31 */
        {XXBYTE_VB_ID_CR8VAR, 2, 0}, /* FC32 */
        {XXBYTE_VB_ID_CR8STR, 2, 0}, /* FC33 */
        {XXBYTE_VB_ID_CADVAR, 2, 0}, /* FC34 */
        {XXBYTE_VB_ID_CREFVARARY, 2, 0}, /* FC35 */
        {0, 0, 0}, /* FC36 */
        {0, 0, 0}, /* FC37 */
        {XXBYTE_VB_ID_CUI1BOOL, 2, 0}, /* FC38 */
        {XXBYTE_VB_ID_FNCDBLCY, 2, 0}, /* FC39 */
        {XXBYTE_VB_ID_FNCDBLR8, 2, 0}, /* FC3A */
        {XXBYTE_VB_ID_FNCDBLR8, 2, 0}, /* FC3B */
        {XXBYTE_VB_ID_FNCSNGI2, 2, 0}, /* FC3C */
        {XXBYTE_VB_ID_FNCSNGI2, 2, 0}, /* FC3D */
        {XXBYTE_VB_ID_FNCSNGI4, 2, 0}, /* FC3E */
        {XXBYTE_VB_ID_CSNG, 2, 0}, /* FC3F */
        {XXBYTE_VB_ID_CSNG, 2, 0}, /* FC40 */
        {XXBYTE_VB_ID_FNCSNGCY, 2, 0}, /* FC41 */
        {XXBYTE_VB_ID_FNCSNGVAR, 2, 0}, /* FC42 */
        {XXBYTE_VB_ID_FNCSNGSTR, 2, 0}, /* FC43 */
        {XXBYTE_VB_ID_FNCBYTEVAR, 2, 0}, /* FC44 */
        {XXBYTE_VB_ID_FNCINTVAR, 2, 0}, /* FC45 */
        {XXBYTE_VB_ID_FNCLNGVAR, 2, 0}, /* FC46 */
        {XXBYTE_VB_ID_CDATER8, 2, 0}, /* FC47 */
        {XXBYTE_VB_ID_FNCDBLVAR, 2, 0}, /* FC48 */
        {XXBYTE_VB_ID_FNCCURVAR, 2, 0}, /* FC49 */
        {0, 0, 0}, /* FC4A */
        {XXBYTE_VB_ID_FNCSTRVAR, 2, 0}, /* FC4B */
        {0, 0, 0}, /* FC4C */
        {XXBYTE_VB_ID_FNCBOOLVAR, 2, 0}, /* FC4D */
        {XXBYTE_VB_ID_FNCDATEVAR, 2, 0}, /* FC4E */
        {XXBYTE_VB_ID_FNCDATEVAR, 2, 0}, /* FC4F */
        {XXBYTE_VB_ID_CBOOLUI1, 2, 0}, /* FC50 */
        {XXBYTE_VB_ID_CBOOLUI1, 2, 0}, /* FC51 */
        {XXBYTE_VB_ID_CBOOLI4, 2, 0}, /* FC52 */
        {XXBYTE_VB_ID_CBOOLR4, 2, 0}, /* FC53 */
        {XXBYTE_VB_ID_CBOOLR4, 2, 0}, /* FC54 */
        {XXBYTE_VB_ID_CBOOLCY, 2, 0}, /* FC55 */
        {XXBYTE_VB_ID_CBOOLVAR, 2, 0}, /* FC56 */
        {XXBYTE_VB_ID_CBOOLSTR, 2, 0}, /* FC57 */
        {XXBYTE_VB_ID_CSTR2UNI, 2, 0}, /* FC58 */
        {XXBYTE_VB_ID_CSTRARY2UNI, 2, 0}, /* FC59 */
        {XXBYTE_VB_ID_CSTR2ANSI, 2, 0}, /* FC5A */
        {XXBYTE_VB_ID_CSTRARY2ANSI, 2, 0}, /* FC5B */
        {XXBYTE_VB_ID_POPADLD4, 2, 0}, /* FC5C */
        {XXBYTE_VB_ID_CRECANSI2UNI, 4, 0}, /* FC5D */
        {XXBYTE_VB_ID_CRECUNI2ANSI, 4, 0}, /* FC5E */
        {XXBYTE_VB_ID_CSTR2VEC, 4, 0}, /* FC5F */
        {XXBYTE_VB_ID_CVAR2VEC, 4, 0}, /* FC60 */
        {XXBYTE_VB_ID_CVEC2VAR, 6, 0}, /* FC61 */
        {XXBYTE_VB_ID_GETLASTERROR, 2, 0}, /* FC62 */
        {XXBYTE_VB_ID_LITNOTHING, 2, 0}, /* FC63 */
        {XXBYTE_VB_ID_LITVAR_NULL, 4, 0}, /* FC64 */
        {XXBYTE_VB_ID_LITVAR_TRUE, 4, 0}, /* FC65 */
        {XXBYTE_VB_ID_LITVAR_FALSE, 4, 0}, /* FC66 */
        {XXBYTE_VB_ID_LITVAR_EMPTY, 4, 0}, /* FC67 */
        {XXBYTE_VB_ID_LITVAR_MISSING, 4, 0}, /* FC68 */
        {XXBYTE_VB_ID_VCALLHRESULT, 6, 0}, /* FC69 */
        {XXBYTE_VB_ID_THISVCALLHRESULT, 6, 0}, /* FC6A */
        {0, 0, 0}, /* FC6B */
        {0, 0, 0}, /* FC6C */
        {XXBYTE_VB_ID_EXITPROCHRESULT, 2, 0}, /* FC6D */
        {0, 0, 0}, /* FC6E */
        {XXBYTE_VB_ID_CHECKTYPEVAR, 4, 0}, /* FC6F */
        {0, 0, 0}, /* FC70 */
        {XXBYTE_VB_ID_CUNKVAR, 2, 0}, /* FC71 */
        {XXBYTE_VB_ID_CVARUNK, 4, 0}, /* FC72 */
        {XXBYTE_VB_ID_LDPRUNKVAR, 2, 0}, /* FC73 */
        {XXBYTE_VB_ID_FLDLATEIDUNKVAR, 10, 0}, /* FC74 */
        {XXBYTE_VB_ID_GETREC3, 2, 0}, /* FC75 */
        {XXBYTE_VB_ID_GETREC4, 2, 0}, /* FC76 */
        {XXBYTE_VB_ID_PUTREC3, 2, 0}, /* FC77 */
        {XXBYTE_VB_ID_PUTREC4, 2, 0}, /* FC78 */
        {XXBYTE_VB_ID_GETRECOWNER3, 0, 0}, /* FC79 */
        {XXBYTE_VB_ID_GETRECOWNER4, 0, 0}, /* FC7A */
        {XXBYTE_VB_ID_PUTRECOWNER3, 0, 0}, /* FC7B */
        {XXBYTE_VB_ID_PUTRECOWNER4, 0, 0}, /* FC7C */
        {XXBYTE_VB_ID_INPUT, 2, 0}, /* FC7D */
        {XXBYTE_VB_ID_INPUTDONE, 2, 0}, /* FC7E */
        {XXBYTE_VB_ID_INPUTITEMUI1, 2, 0}, /* FC7F */
        {XXBYTE_VB_ID_INPUTITEMI2, 2, 0}, /* FC80 */
        {XXBYTE_VB_ID_INPUTITEMI4, 2, 0}, /* FC81 */
        {XXBYTE_VB_ID_INPUTITEMR4, 2, 0}, /* FC82 */
        {XXBYTE_VB_ID_INPUTITEMR8, 2, 0}, /* FC83 */
        {XXBYTE_VB_ID_INPUTITEMCY, 2, 0}, /* FC84 */
        {XXBYTE_VB_ID_INPUTITEMVAR, 2, 0}, /* FC85 */
        {XXBYTE_VB_ID_INPUTITEMSTR, 2, 0}, /* FC86 */
        {XXBYTE_VB_ID_INPUTITEMBOOL, 2, 0}, /* FC87 */
        {XXBYTE_VB_ID_INPUTITEMDATE, 2, 0}, /* FC88 */
        {XXBYTE_VB_ID_POPFPR4, 2, 0}, /* FC89 */
        {XXBYTE_VB_ID_POPFPR8, 2, 0}, /* FC8A */
        {XXBYTE_VB_ID_POPAD, 2, 0}, /* FC8B */
        {XXBYTE_VB_ID_POPADLDVAR, 2, 0}, /* FC8C */
        {XXBYTE_VB_ID_ARYLDPR, 4, 0}, /* FC8D */
        {XXBYTE_VB_ID_ARYLDRF, 4, 0}, /* FC8E */
        {XXBYTE_VB_ID_PARMARY1ST, 6, 0}, /* FC8F */
        {XXBYTE_VB_ID_ARY1LDUI1, 2, 0}, /* FC90 */
        {XXBYTE_VB_ID_ARY1LDI2, 2, 0}, /* FC91 */
        {XXBYTE_VB_ID_ARY1LDI4, 2, 0}, /* FC92 */
        {XXBYTE_VB_ID_ARY1LDI4, 2, 0}, /* FC93 */
        {XXBYTE_VB_ID_ARY1LDR8, 2, 0}, /* FC94 */
        {XXBYTE_VB_ID_ARY1LDR8, 2, 0}, /* FC95 */
        {XXBYTE_VB_ID_ARY1LDRFVAR, 2, 0}, /* FC96 */
        {XXBYTE_VB_ID_ARY1LDI4, 2, 0}, /* FC97 */
        {XXBYTE_VB_ID_ARY1LDI4, 2, 0}, /* FC98 */
        {XXBYTE_VB_ID_ARY1LDFPR4, 2, 0}, /* FC99 */
        {XXBYTE_VB_ID_ARY1LDFPR8, 2, 0}, /* FC9A */
        {XXBYTE_VB_ID_ARY1LDPR, 2, 0}, /* FC9B */
        {XXBYTE_VB_ID_ARY1LDRF, 2, 0}, /* FC9C */
        {XXBYTE_VB_ID_ARY1LDVAR, 2, 0}, /* FC9D */
        {0, 0, 0}, /* FC9E */
        {0, 0, 0}, /* FC9F */
        {XXBYTE_VB_ID_ARY1STUI1, 2, 0}, /* FCA0 */
        {XXBYTE_VB_ID_ARY1STI2, 2, 0}, /* FCA1 */
        {XXBYTE_VB_ID_ARY1STR4, 2, 0}, /* FCA2 */
        {XXBYTE_VB_ID_ARY1STR4, 2, 0}, /* FCA3 */
        {XXBYTE_VB_ID_ARY1STCY, 2, 0}, /* FCA4 */
        {XXBYTE_VB_ID_ARY1STCY, 2, 0}, /* FCA5 */
        {XXBYTE_VB_ID_ARY1STVAR, 2, 0}, /* FCA6 */
        {XXBYTE_VB_ID_ARY1STSTR, 2, 0}, /* FCA7 */
        {XXBYTE_VB_ID_ARY1STAD, 2, 0}, /* FCA8 */
        {XXBYTE_VB_ID_ARY1STFPR4, 2, 0}, /* FCA9 */
        {XXBYTE_VB_ID_ARY1STFPR8, 2, 0}, /* FCAA */
        {XXBYTE_VB_ID_ARY1STVARAD, 2, 0}, /* FCAB */
        {XXBYTE_VB_ID_ARY1STVARADFUNC, 2, 0}, /* FCAC */
        {XXBYTE_VB_ID_ARY1STVARUNK, 2, 0}, /* FCAD */
        {XXBYTE_VB_ID_ARY1STVARUNKFUNC, 2, 0}, /* FCAE */
        {XXBYTE_VB_ID_ARY1STADFUNC, 2, 0}, /* FCAF */
        {XXBYTE_VB_ID_ARY1STVARCOPY, 2, 0}, /* FCB0 */
        {XXBYTE_VB_ID_ARY1STSTRCOPY, 2, 0}, /* FCB1 */
        {XXBYTE_VB_ID_ARY1LDRFVARG, 4, 0}, /* FCB2 */
        {XXBYTE_VB_ID_ARY1LDVARG, 2, 0}, /* FCB3 */
        {XXBYTE_VB_ID_ARY1LDRFVARGPARAM, 2, 0}, /* FCB4 */
        {XXBYTE_VB_ID_ARY1STVARG, 2, 0}, /* FCB5 */
        {XXBYTE_VB_ID_ARY1STVARGCOPY, 2, 0}, /* FCB6 */
        {XXBYTE_VB_ID_ARY1STVARGAD, 2, 0}, /* FCB7 */
        {XXBYTE_VB_ID_ARY1STVARGADFUNC, 2, 0}, /* FCB8 */
        {XXBYTE_VB_ID_ARY1STVARGUNK, 2, 0}, /* FCB9 */
        {XXBYTE_VB_ID_ARY1STVARGUNKFUNC, 2, 0}, /* FCBA */
        {XXBYTE_VB_ID_MIDVAR, 2, 0}, /* FCBB */
        {XXBYTE_VB_ID_MIDBSTR, 4, 0}, /* FCBC */
        {XXBYTE_VB_ID_MIDBVAR, 2, 0}, /* FCBD */
        {XXBYTE_VB_ID_MIDBSTRB, 4, 0}, /* FCBE */
        {XXBYTE_VB_ID_LINEINPUTVAR, 2, 0}, /* FCBF */
        {XXBYTE_VB_ID_LINEINPUTSTR, 2, 0}, /* FCC0 */
        {XXBYTE_VB_ID_ERROR, 2, 0}, /* FCC1 */
        {XXBYTE_VB_ID_STOP, 2, 0}, /* FCC2 */
        {XXBYTE_VB_ID_ERASE, 2, 0}, /* FCC3 */
        {XXBYTE_VB_ID_LARGEBOS, 3, 0}, /* FCC4 */
        {0, 0, 0}, /* FCC5 */
        {0, 0, 0}, /* FCC6 */
        {0, 0, 0}, /* FCC7 */
        {XXBYTE_VB_ID_END, 2, 0}, /* FCC8 */
        {XXBYTE_VB_ID_RETURN, 2, 0}, /* FCC9 */
        {XXBYTE_VB_ID_FNLBOUND, 2, 0}, /* FCCA */
        {XXBYTE_VB_ID_FNUBOUND, 2, 0}, /* FCCB */
        {XXBYTE_VB_ID_EXITPROCUI1, 2, 0}, /* FCCC */
        {XXBYTE_VB_ID_EXITPROCI2, 2, 0}, /* FCCD */
        {XXBYTE_VB_ID_EXITPROCSTR, 2, 0}, /* FCCE */
        {XXBYTE_VB_ID_EXITPROCR4, 2, 0}, /* FCCF */
        {XXBYTE_VB_ID_EXITPROCR8, 2, 0}, /* FCD0 */
        {XXBYTE_VB_ID_EXITPROCCY, 2, 0}, /* FCD1 */
        {0, 0, 0}, /* FCD2 */
        {XXBYTE_VB_ID_EXITPROCSTR, 2, 0}, /* FCD3 */
        {XXBYTE_VB_ID_EXITPROCSTR, 2, 0}, /* FCD4 */
        {XXBYTE_VB_ID_EXITPROCSTR, 2, 0}, /* FCD5 */
        {0, 0, 0}, /* FCD6 */
        {0, 0, 0}, /* FCD7 */
        {0, 0, 0}, /* FCD8 */
        {0, 0, 0}, /* FCD9 */
        {0, 0, 0}, /* FCDA */
        {0, 0, 0}, /* FCDB */
        {0, 0, 0}, /* FCDC */
        {0, 0, 0}, /* FCDD */
        {0, 0, 0}, /* FCDE */
        {0, 0, 0}, /* FCDF */
        {XXBYTE_VB_ID_FLDUI1, 4, 0}, /* FCE0 */
        {XXBYTE_VB_ID_FLDI2, 4, 0}, /* FCE1 */
        {XXBYTE_VB_ID_FLDR4, 4, 0}, /* FCE2 */
        {XXBYTE_VB_ID_FLDR4, 4, 0}, /* FCE3 */
        {XXBYTE_VB_ID_FLDR8, 4, 0}, /* FCE4 */
        {XXBYTE_VB_ID_FLDR8, 4, 0}, /* FCE5 */
        {XXBYTE_VB_ID_FLDRFVAR, 4, 0}, /* FCE6 */
        {XXBYTE_VB_ID_FLDR4, 4, 0}, /* FCE7 */
        {XXBYTE_VB_ID_FLDR4, 4, 0}, /* FCE8 */
        {XXBYTE_VB_ID_FLDFPR4, 4, 0}, /* FCE9 */
        {XXBYTE_VB_ID_FLDFPR8, 4, 0}, /* FCEA */
        {XXBYTE_VB_ID_FLDPR, 4, 0}, /* FCEB */
        {XXBYTE_VB_ID_FLDRFVAR, 4, 0}, /* FCEC */
        {XXBYTE_VB_ID_FLDVAR, 4, 0}, /* FCED */
        {0, 0, 0}, /* FCEE */
        {0, 0, 0}, /* FCEF */
        {XXBYTE_VB_ID_FSTUI1, 4, 0}, /* FCF0 */
        {XXBYTE_VB_ID_FSTI2, 4, 0}, /* FCF1 */
        {XXBYTE_VB_ID_FSTR4, 4, 0}, /* FCF2 */
        {XXBYTE_VB_ID_FSTR4, 4, 0}, /* FCF3 */
        {XXBYTE_VB_ID_FSTR8, 4, 0}, /* FCF4 */
        {XXBYTE_VB_ID_FSTR8, 4, 0}, /* FCF5 */
        {XXBYTE_VB_ID_FSTVAR, 4, 0}, /* FCF6 */
        {XXBYTE_VB_ID_FSTSTR, 4, 0}, /* FCF7 */
        {XXBYTE_VB_ID_FSTAD, 4, 0}, /* FCF8 */
        {XXBYTE_VB_ID_FSTFPR4, 4, 0}, /* FCF9 */
        {XXBYTE_VB_ID_FSTFPR8, 4, 0}, /* FCFA */
        {XXBYTE_VB_ID_FSTVARAD, 4, 0}, /* FCFB */
        {XXBYTE_VB_ID_FSTVARADFUNC, 4, 0}, /* FCFC */
        {XXBYTE_VB_ID_FSTVARUNK, 4, 0}, /* FCFD */
        {XXBYTE_VB_ID_FSTVARUNKFUNC, 4, 0}, /* FCFE */
        {XXBYTE_VB_ID_FSTADFUNC, 4, 0}, /* FCFF */
    },
    {
        {XXBYTE_VB_ID_FSTVARCOPY, 4, 0}, /* FD00 */
        {XXBYTE_VB_ID_FSTSTRCOPY, 4, 0}, /* FD01 */
        {XXBYTE_VB_ID_HARDTYPE, 2, 0}, /* FD02 */
        {XXBYTE_VB_ID_BRANCH, 4, 0}, /* FD03 */
        {XXBYTE_VB_ID_BRANCHF, 4, 0}, /* FD04 */
        {XXBYTE_VB_ID_BRANCHFVAR, 4, 0}, /* FD05 */
        {XXBYTE_VB_ID_BRANCHFVARFREE, 4, 0}, /* FD06 */
        {XXBYTE_VB_ID_BRANCHT, 4, 0}, /* FD07 */
        {XXBYTE_VB_ID_BRANCHTVAR, 4, 0}, /* FD08 */
        {XXBYTE_VB_ID_BRANCHTVARFREE, 4, 0}, /* FD09 */
        {XXBYTE_VB_ID_GOSUB, 4, 0}, /* FD0A */
        {XXBYTE_VB_ID_ONERRORGOTO, 4, 0}, /* FD0B */
        {XXBYTE_VB_ID_RESUME, 4, 0}, /* FD0C */
        {XXBYTE_VB_ID_ARYLOCK, 4, 0}, /* FD0D */
        {XXBYTE_VB_ID_ARYUNLOCK, 4, 0}, /* FD0E */
        {XXBYTE_VB_ID_ARYDESCTEMP, 4, 0}, /* FD0F */
        {XXBYTE_VB_ID_ILDUI1, 4, 0}, /* FD10 */
        {XXBYTE_VB_ID_ILDI2, 4, 0}, /* FD11 */
        {XXBYTE_VB_ID_ILDAD, 4, 0}, /* FD12 */
        {XXBYTE_VB_ID_ILDAD, 4, 0}, /* FD13 */
        {XXBYTE_VB_ID_ILDR8, 4, 0}, /* FD14 */
        {XXBYTE_VB_ID_ILDR8, 4, 0}, /* FD15 */
        {XXBYTE_VB_ID_ILDRFDARG, 6, 0}, /* FD16 */
        {XXBYTE_VB_ID_ILDAD, 4, 0}, /* FD17 */
        {XXBYTE_VB_ID_ILDAD, 4, 0}, /* FD18 */
        {XXBYTE_VB_ID_ILDFPR4, 4, 0}, /* FD19 */
        {XXBYTE_VB_ID_ILDFPR8, 4, 0}, /* FD1A */
        {XXBYTE_VB_ID_ILDPR, 4, 0}, /* FD1B */
        {XXBYTE_VB_ID_FLDR4, 4, 0}, /* FD1C */
        {XXBYTE_VB_ID_ILDDARG, 4, 0}, /* FD1D */
        {0, 0, 0}, /* FD1E */
        {0, 0, 0}, /* FD1F */
        {XXBYTE_VB_ID_ISTUI1, 4, 0}, /* FD20 */
        {XXBYTE_VB_ID_ISTI2, 4, 0}, /* FD21 */
        {XXBYTE_VB_ID_ISTI4, 4, 0}, /* FD22 */
        {XXBYTE_VB_ID_ISTI4, 4, 0}, /* FD23 */
        {XXBYTE_VB_ID_ISTR8, 4, 0}, /* FD24 */
        {XXBYTE_VB_ID_ISTR8, 4, 0}, /* FD25 */
        {XXBYTE_VB_ID_ISTDARG, 4, 0}, /* FD26 */
        {XXBYTE_VB_ID_ISTSTR, 4, 0}, /* FD27 */
        {XXBYTE_VB_ID_ISTAD, 4, 0}, /* FD28 */
        {XXBYTE_VB_ID_ISTFPR4, 4, 0}, /* FD29 */
        {XXBYTE_VB_ID_ISTFPR8, 4, 0}, /* FD2A */
        {XXBYTE_VB_ID_ISTDARGAD, 4, 0}, /* FD2B */
        {XXBYTE_VB_ID_ISTDARGADFUNC, 4, 0}, /* FD2C */
        {XXBYTE_VB_ID_ISTDARGUNK, 4, 0}, /* FD2D */
        {XXBYTE_VB_ID_ISTDARGUNKFUNC, 4, 0}, /* FD2E */
        {XXBYTE_VB_ID_ISTADFUNC, 4, 0}, /* FD2F */
        {XXBYTE_VB_ID_ISTDARGCOPY, 4, 0}, /* FD30 */
        {XXBYTE_VB_ID_ISTSTRCOPY, 4, 0}, /* FD31 */
        {XXBYTE_VB_ID_PRINTCHAN, 2, 0}, /* FD32 */
        {XXBYTE_VB_ID_WRITECHAN, 2, 0}, /* FD33 */
        {XXBYTE_VB_ID_PRINTCOMMA, 2, 0}, /* FD34 */
        {XXBYTE_VB_ID_PRINTEOS, 2, 0}, /* FD35 */
        {XXBYTE_VB_ID_PRINTNL, 2, 0}, /* FD36 */
        {XXBYTE_VB_ID_PRINTITEMCOMMA, 2, 0}, /* FD37 */
        {XXBYTE_VB_ID_PRINTITEMSEMI, 2, 0}, /* FD38 */
        {XXBYTE_VB_ID_PRINTITEMNL, 2, 0}, /* FD39 */
        {XXBYTE_VB_ID_PRINTOBJ, 4, 0}, /* FD3A */
        {XXBYTE_VB_ID_PRINTSPC, 2, 0}, /* FD3B */
        {XXBYTE_VB_ID_PRINTTAB, 2, 0}, /* FD3C */
        {XXBYTE_VB_ID_CLOSE, 2, 0}, /* FD3D */
        {XXBYTE_VB_ID_CLOSEALL, 2, 0}, /* FD3E */
        {XXBYTE_VB_ID_FLDZEROAD, 4, 0}, /* FD3F */
        {XXBYTE_VB_ID_IWMEMLDUI1, 4, 0}, /* FD40 */
        {XXBYTE_VB_ID_IWMEMLDI2, 4, 0}, /* FD41 */
        {XXBYTE_VB_ID_IWMEMLDI4, 4, 0}, /* FD42 */
        {XXBYTE_VB_ID_IWMEMLDI4, 4, 0}, /* FD43 */
        {XXBYTE_VB_ID_IWMEMLDCY, 4, 0}, /* FD44 */
        {XXBYTE_VB_ID_IWMEMLDCY, 4, 0}, /* FD45 */
        {XXBYTE_VB_ID_IWMEMLDRFDARG, 6, 0}, /* FD46 */
        {XXBYTE_VB_ID_IWMEMLDI4, 4, 0}, /* FD47 */
        {XXBYTE_VB_ID_IWMEMLDI4, 4, 0}, /* FD48 */
        {XXBYTE_VB_ID_IWMEMLDFPR4, 4, 0}, /* FD49 */
        {XXBYTE_VB_ID_IWMEMLDFPR8, 4, 0}, /* FD4A */
        {XXBYTE_VB_ID_IWMEMLDPR, 4, 0}, /* FD4B */
        {XXBYTE_VB_ID_IWMEMLDRF, 4, 0}, /* FD4C */
        {XXBYTE_VB_ID_IWMEMLDDARG, 4, 0}, /* FD4D */
        {0, 0, 0}, /* FD4E */
        {0, 0, 0}, /* FD4F */
        {XXBYTE_VB_ID_IWMEMSTUI1, 4, 0}, /* FD50 */
        {XXBYTE_VB_ID_IWMEMSTI2, 4, 0}, /* FD51 */
        {XXBYTE_VB_ID_IWMEMSTR4, 4, 0}, /* FD52 */
        {XXBYTE_VB_ID_IWMEMSTR4, 4, 0}, /* FD53 */
        {XXBYTE_VB_ID_IWMEMSTCY, 4, 0}, /* FD54 */
        {XXBYTE_VB_ID_IWMEMSTCY, 4, 0}, /* FD55 */
        {XXBYTE_VB_ID_IWMEMSTDARG, 4, 0}, /* FD56 */
        {XXBYTE_VB_ID_IWMEMSTSTR, 4, 0}, /* FD57 */
        {XXBYTE_VB_ID_IWMEMSTAD, 4, 0}, /* FD58 */
        {XXBYTE_VB_ID_IWMEMSTFPR4, 4, 0}, /* FD59 */
        {XXBYTE_VB_ID_IWMEMSTFPR8, 4, 0}, /* FD5A */
        {XXBYTE_VB_ID_IWMEMSTDARGAD, 4, 0}, /* FD5B */
        {XXBYTE_VB_ID_IWMEMSTDARGADFUNC, 4, 0}, /* FD5C */
        {XXBYTE_VB_ID_IWMEMSTDARGUNK, 4, 0}, /* FD5D */
        {XXBYTE_VB_ID_IWMEMSTDARGUNKFUNC, 4, 0}, /* FD5E */
        {XXBYTE_VB_ID_IWMEMSTADFUNC, 4, 0}, /* FD5F */
        {XXBYTE_VB_ID_IWMEMSTDARGCOPY, 4, 0}, /* FD60 */
        {XXBYTE_VB_ID_IWMEMSTSTRCOPY, 4, 0}, /* FD61 */
        {XXBYTE_VB_ID_FLDZEROAD, 4, 0}, /* FD62 */
        {XXBYTE_VB_ID_FSTVARNOPOP, 4, 0}, /* FD63 */
        {XXBYTE_VB_ID_FSTSTRNOPOP, 4, 0}, /* FD64 */
        {0, 0, 0}, /* FD65 */
        {0, 0, 0}, /* FD66 */
        {XXBYTE_VB_ID_CVARUI1, 4, 0}, /* FD67 */
        {XXBYTE_VB_ID_CVARI2, 4, 0}, /* FD68 */
        {XXBYTE_VB_ID_CVARI4, 4, 0}, /* FD69 */
        {XXBYTE_VB_ID_CVARR4, 4, 0}, /* FD6A */
        {XXBYTE_VB_ID_CVARR8, 4, 0}, /* FD6B */
        {XXBYTE_VB_ID_CVARCY, 4, 0}, /* FD6C */
        {0, 0, 0}, /* FD6D */
        {XXBYTE_VB_ID_CVARSTR, 4, 0}, /* FD6E */
        {XXBYTE_VB_ID_CVARAD, 4, 0}, /* FD6F */
        {XXBYTE_VB_ID_MEMLDUI1, 4, 0}, /* FD70 */
        {XXBYTE_VB_ID_MEMLDI2, 4, 0}, /* FD71 */
        {XXBYTE_VB_ID_MEMLDR4, 4, 0}, /* FD72 */
        {XXBYTE_VB_ID_MEMLDR4, 4, 0}, /* FD73 */
        {XXBYTE_VB_ID_MEMLDR8, 4, 0}, /* FD74 */
        {XXBYTE_VB_ID_MEMLDR8, 4, 0}, /* FD75 */
        {XXBYTE_VB_ID_MEMLDRFVAR, 4, 0}, /* FD76 */
        {XXBYTE_VB_ID_MEMLDR4, 4, 0}, /* FD77 */
        {XXBYTE_VB_ID_MEMLDR4, 4, 0}, /* FD78 */
        {XXBYTE_VB_ID_MEMLDFPR4, 4, 0}, /* FD79 */
        {XXBYTE_VB_ID_MEMLDFPR8, 4, 0}, /* FD7A */
        {XXBYTE_VB_ID_MEMLDPR, 4, 0}, /* FD7B */
        {XXBYTE_VB_ID_MEMLDRFVAR, 4, 0}, /* FD7C */
        {XXBYTE_VB_ID_MEMLDVAR, 4, 0}, /* FD7D */
        {0, 0, 0}, /* FD7E */
        {0, 0, 0}, /* FD7F */
        {XXBYTE_VB_ID_MEMSTUI1, 4, 0}, /* FD80 */
        {XXBYTE_VB_ID_MEMSTI2, 4, 0}, /* FD81 */
        {XXBYTE_VB_ID_MEMSTR4, 4, 0}, /* FD82 */
        {XXBYTE_VB_ID_MEMSTR4, 4, 0}, /* FD83 */
        {XXBYTE_VB_ID_MEMSTCY, 4, 0}, /* FD84 */
        {XXBYTE_VB_ID_MEMSTCY, 4, 0}, /* FD85 */
        {XXBYTE_VB_ID_MEMSTVAR, 4, 0}, /* FD86 */
        {XXBYTE_VB_ID_MEMSTSTR, 4, 0}, /* FD87 */
        {XXBYTE_VB_ID_MEMSTAD, 4, 0}, /* FD88 */
        {XXBYTE_VB_ID_MEMSTFPR4, 4, 0}, /* FD89 */
        {XXBYTE_VB_ID_MEMSTFPR8, 4, 0}, /* FD8A */
        {XXBYTE_VB_ID_MEMSTVARAD, 4, 0}, /* FD8B */
        {XXBYTE_VB_ID_MEMSTVARADFUNC, 4, 0}, /* FD8C */
        {XXBYTE_VB_ID_MEMSTVARUNK, 4, 0}, /* FD8D */
        {XXBYTE_VB_ID_MEMSTVARUNKFUNC, 4, 0}, /* FD8E */
        {XXBYTE_VB_ID_MEMSTADFUNC, 4, 0}, /* FD8F */
        {XXBYTE_VB_ID_MEMSTVARCOPY, 4, 0}, /* FD90 */
        {XXBYTE_VB_ID_MEMSTSTRCOPY, 4, 0}, /* FD91 */
        {0, 0, 0}, /* FD92 */
        {XXBYTE_VB_ID_CDARGREF, 4, 0}, /* FD93 */
        {XXBYTE_VB_ID_CVARREF, 6, 0}, /* FD94 */
        {XXBYTE_VB_ID_EXITPROCCB, 4, 0}, /* FD95 */
        {XXBYTE_VB_ID_EXITPROCCBSTACK, 4, 0}, /* FD96 */
        {0, 0, 0}, /* FD97 */
        {0, 0, 0}, /* FD98 */
        {XXBYTE_VB_ID_FFREE1VAR, 4, 0}, /* FD99 */
        {XXBYTE_VB_ID_FFREE1STR, 4, 0}, /* FD9A */
        {XXBYTE_VB_ID_FFREE1AD, 4, 0}, /* FD9B */
        {XXBYTE_VB_ID_FSTADNOPOP, 4, 0}, /* FD9C */
        {XXBYTE_VB_ID_FSTADFUNCNOPOP, 4, 0}, /* FD9D */
        {XXBYTE_VB_ID_FLDPRTHIS, 2, 0}, /* FD9E */
        {XXBYTE_VB_ID_LDPRVAR, 2, 0}, /* FD9F */
        {XXBYTE_VB_ID_IMPADLDUI1, 4, 0}, /* FDA0 */
        {XXBYTE_VB_ID_IMPADLDI2, 4, 0}, /* FDA1 */
        {XXBYTE_VB_ID_IMPADLDSTR, 4, 0}, /* FDA2 */
        {XXBYTE_VB_ID_IMPADLDSTR, 4, 0}, /* FDA3 */
        {XXBYTE_VB_ID_IMPADLDCY, 4, 0}, /* FDA4 */
        {XXBYTE_VB_ID_IMPADLDCY, 4, 0}, /* FDA5 */
        {XXBYTE_VB_ID_IMPADLDRF, 4, 0}, /* FDA6 */
        {XXBYTE_VB_ID_IMPADLDSTR, 4, 0}, /* FDA7 */
        {XXBYTE_VB_ID_IMPADLDSTR, 4, 0}, /* FDA8 */
        {XXBYTE_VB_ID_IMPADLDFPR4, 4, 0}, /* FDA9 */
        {XXBYTE_VB_ID_IMPADLDFPR8, 4, 0}, /* FDAA */
        {XXBYTE_VB_ID_IMPADLDPR, 4, 0}, /* FDAB */
        {XXBYTE_VB_ID_IMPADLDRF, 4, 0}, /* FDAC */
        {XXBYTE_VB_ID_IMPADLDVAR, 4, 0}, /* FDAD */
        {0, 0, 0}, /* FDAE */
        {0, 0, 0}, /* FDAF */
        {XXBYTE_VB_ID_IMPADSTUI1, 4, 0}, /* FDB0 */
        {XXBYTE_VB_ID_IMPADSTI2, 4, 0}, /* FDB1 */
        {XXBYTE_VB_ID_IMPADSTR4, 4, 0}, /* FDB2 */
        {XXBYTE_VB_ID_IMPADSTR4, 4, 0}, /* FDB3 */
        {XXBYTE_VB_ID_IMPADSTR8, 4, 0}, /* FDB4 */
        {XXBYTE_VB_ID_IMPADSTR8, 4, 0}, /* FDB5 */
        {XXBYTE_VB_ID_IMPADSTVAR, 4, 0}, /* FDB6 */
        {XXBYTE_VB_ID_IMPADSTSTR, 4, 0}, /* FDB7 */
        {XXBYTE_VB_ID_IMPADSTAD, 4, 0}, /* FDB8 */
        {XXBYTE_VB_ID_IMPADSTFPR4, 4, 0}, /* FDB9 */
        {XXBYTE_VB_ID_IMPADSTFPR8, 4, 0}, /* FDBA */
        {XXBYTE_VB_ID_IMPADSTVARAD, 4, 0}, /* FDBB */
        {XXBYTE_VB_ID_IMPADSTVARADFUNC, 4, 0}, /* FDBC */
        {XXBYTE_VB_ID_IMPADSTVARUNK, 4, 0}, /* FDBD */
        {XXBYTE_VB_ID_IMPADSTVARUNKFUNC, 4, 0}, /* FDBE */
        {XXBYTE_VB_ID_IMPADSTADFUNC, 4, 0}, /* FDBF */
        {XXBYTE_VB_ID_IMPADSTVARCOPY, 4, 0}, /* FDC0 */
        {XXBYTE_VB_ID_IMPADSTSTRCOPY, 4, 0}, /* FDC1 */
        {XXBYTE_VB_ID_POPTMPLDAD1, 4, 0}, /* FDC2 */
        {XXBYTE_VB_ID_POPTMPLDAD2, 4, 0}, /* FDC3 */
        {XXBYTE_VB_ID_POPTMPLDADSTR, 4, 0}, /* FDC4 */
        {XXBYTE_VB_ID_POPTMPLDAD8, 4, 0}, /* FDC5 */
        {XXBYTE_VB_ID_POPTMPLDADVAR, 4, 0}, /* FDC6 */
        {XXBYTE_VB_ID_POPTMPLDADSTR, 4, 0}, /* FDC7 */
        {XXBYTE_VB_ID_POPTMPLDADFPR4, 4, 0}, /* FDC8 */
        {XXBYTE_VB_ID_POPTMPLDADFPR8, 4, 0}, /* FDC9 */
        {XXBYTE_VB_ID_COPYBYTES, 4, 0}, /* FDCA */
        {XXBYTE_VB_ID_EXITFORCOLLOBJ, 6, 0}, /* FDCB */
        {XXBYTE_VB_ID_EXITFORCOLLOBJ, 6, 0}, /* FDCC */
        {XXBYTE_VB_ID_EXITFORCOLLOBJ, 6, 0}, /* FDCD */
        {XXBYTE_VB_ID_EXITFORARYVAR, 6, 0}, /* FDCE */
        {XXBYTE_VB_ID_EXITFORVAR, 6, 0}, /* FDCF */
        {XXBYTE_VB_ID_FMEMLDUI1, 6, 0}, /* FDD0 */
        {XXBYTE_VB_ID_FMEMLDI2, 6, 0}, /* FDD1 */
        {XXBYTE_VB_ID_FMEMLDR4, 6, 0}, /* FDD2 */
        {XXBYTE_VB_ID_FMEMLDR4, 6, 0}, /* FDD3 */
        {XXBYTE_VB_ID_FMEMLDR8, 6, 0}, /* FDD4 */
        {XXBYTE_VB_ID_FMEMLDR8, 6, 0}, /* FDD5 */
        {XXBYTE_VB_ID_FMEMLDRF, 6, 0}, /* FDD6 */
        {XXBYTE_VB_ID_FMEMLDR4, 6, 0}, /* FDD7 */
        {XXBYTE_VB_ID_FMEMLDR4, 6, 0}, /* FDD8 */
        {XXBYTE_VB_ID_FMEMLDFPR4, 6, 0}, /* FDD9 */
        {XXBYTE_VB_ID_FMEMLDFPR8, 6, 0}, /* FDDA */
        {XXBYTE_VB_ID_FMEMLDPR, 6, 0}, /* FDDB */
        {XXBYTE_VB_ID_FMEMLDRF, 6, 0}, /* FDDC */
        {XXBYTE_VB_ID_FMEMLDVAR, 6, 0}, /* FDDD */
        {0, 0, 0}, /* FDDE */
        {0, 0, 0}, /* FDDF */
        {XXBYTE_VB_ID_FMEMSTUI1, 6, 0}, /* FDE0 */
        {XXBYTE_VB_ID_FMEMSTI2, 6, 0}, /* FDE1 */
        {XXBYTE_VB_ID_FMEMSTR4, 6, 0}, /* FDE2 */
        {XXBYTE_VB_ID_FMEMSTR4, 6, 0}, /* FDE3 */
        {XXBYTE_VB_ID_FMEMSTR8, 6, 0}, /* FDE4 */
        {XXBYTE_VB_ID_FMEMSTR8, 6, 0}, /* FDE5 */
        {XXBYTE_VB_ID_FMEMSTVAR, 6, 0}, /* FDE6 */
        {XXBYTE_VB_ID_FMEMSTSTR, 6, 0}, /* FDE7 */
        {XXBYTE_VB_ID_FMEMSTAD, 6, 0}, /* FDE8 */
        {XXBYTE_VB_ID_FMEMSTFPR4, 6, 0}, /* FDE9 */
        {XXBYTE_VB_ID_FMEMSTFPR8, 6, 0}, /* FDEA */
        {XXBYTE_VB_ID_FMEMSTVARAD, 6, 0}, /* FDEB */
        {XXBYTE_VB_ID_FMEMSTVARADFUNC, 6, 0}, /* FDEC */
        {XXBYTE_VB_ID_FMEMSTVARUNK, 6, 0}, /* FDED */
        {XXBYTE_VB_ID_FMEMSTVARUNKFUNC, 6, 0}, /* FDEE */
        {XXBYTE_VB_ID_FMEMSTADFUNC, 6, 0}, /* FDEF */
        {XXBYTE_VB_ID_FMEMSTVARCOPY, 6, 0}, /* FDF0 */
        {XXBYTE_VB_ID_FMEMSTSTRCOPY, 6, 0}, /* FDF1 */
        {XXBYTE_VB_ID_CASTAD, 4, 0}, /* FDF2 */
        {XXBYTE_VB_ID_CASTADVAR, 4, 0}, /* FDF3 */
        {XXBYTE_VB_ID_NEW, 4, 0}, /* FDF4 */
        {XXBYTE_VB_ID_NEWIFNULLRF, 4, 0}, /* FDF5 */
        {XXBYTE_VB_ID_NEWIFNULLAD, 4, 0}, /* FDF6 */
        {XXBYTE_VB_ID_NEWIFNULLPR, 4, 0}, /* FDF7 */
        {XXBYTE_VB_ID_CVARBOOLI2, 4, 0}, /* FDF8 */
        {XXBYTE_VB_ID_CVARDATEVAR, 4, 0}, /* FDF9 */
        {XXBYTE_VB_ID_CVARERRI4, 4, 0}, /* FDFA */
        {XXBYTE_VB_ID_CVARDATE, 4, 0}, /* FDFB */
        {XXBYTE_VB_ID_CVARARYVARG, 4, 0}, /* FDFC */
        {XXBYTE_VB_ID_CSTRVARTMP, 2, 0}, /* FDFD */
        {XXBYTE_VB_ID_CSTRVARVAL, 4, 0}, /* FDFE */
        {XXBYTE_VB_ID_DESTRUCTOFRAME, 6, 0}, /* FDFF */
    },
    {
        {XXBYTE_VB_ID_THISVCALLUI1, 4, 0}, /* FE00 */
        {XXBYTE_VB_ID_THISVCALLI2, 4, 0}, /* FE01 */
        {XXBYTE_VB_ID_THISVCALLI2, 4, 0}, /* FE02 */
        {XXBYTE_VB_ID_THISVCALLR4, 4, 0}, /* FE03 */
        {XXBYTE_VB_ID_THISVCALLR8, 4, 0}, /* FE04 */
        {XXBYTE_VB_ID_THISVCALLCY, 4, 0}, /* FE05 */
        {0, 0, 0}, /* FE06 */
        {XXBYTE_VB_ID_THISVCALLI2, 4, 0}, /* FE07 */
        {XXBYTE_VB_ID_THISVCALLI2, 4, 0}, /* FE08 */
        {XXBYTE_VB_ID_THISVCALLHIDDEN, 4, 0}, /* FE09 */
        {XXBYTE_VB_ID_THISVCALLHIDDEN, 4, 0}, /* FE0A */
        {0, 0, 0}, /* FE0B */
        {XXBYTE_VB_ID_THISVCALLHIDDEN, 4, 0}, /* FE0C */
        {XXBYTE_VB_ID_THISVCALLCBFRAME, 6, 0}, /* FE0D */
        {XXBYTE_VB_ID_STLSETFIXSTR, 4, 0}, /* FE0E */
        {XXBYTE_VB_ID_STFIXEDSTRFREE, 4, 0}, /* FE0F */
        {XXBYTE_VB_ID_VCALLUI1, 4, 0}, /* FE10 */
        {XXBYTE_VB_ID_VCALLSTR, 4, 0}, /* FE11 */
        {XXBYTE_VB_ID_VCALLSTR, 4, 0}, /* FE12 */
        {XXBYTE_VB_ID_VCALLR4, 4, 0}, /* FE13 */
        {XXBYTE_VB_ID_VCALLR8, 4, 0}, /* FE14 */
        {XXBYTE_VB_ID_VCALLCY, 4, 0}, /* FE15 */
        {0, 0, 0}, /* FE16 */
        {XXBYTE_VB_ID_VCALLSTR, 4, 0}, /* FE17 */
        {XXBYTE_VB_ID_VCALLSTR, 4, 0}, /* FE18 */
        {XXBYTE_VB_ID_VCALLFPR8, 4, 0}, /* FE19 */
        {XXBYTE_VB_ID_VCALLFPR8, 4, 0}, /* FE1A */
        {0, 0, 0}, /* FE1B */
        {XXBYTE_VB_ID_VCALLFPR8, 4, 0}, /* FE1C */
        {XXBYTE_VB_ID_VCALLCBFRAME, 6, VB_OP_VB5_NO_VTABLE}, /* FE1D */
        {XXBYTE_VB_ID_STFIXEDSTRR, 4, 0}, /* FE1E */
        {XXBYTE_VB_ID_STFIXEDSTRRFREE, 4, 0}, /* FE1F */
        {XXBYTE_VB_ID_IMPADCALLUI1, 6, 0}, /* FE20 */
        {XXBYTE_VB_ID_IMPADCALLI4, 6, 0}, /* FE21 */
        {XXBYTE_VB_ID_IMPADCALLI4, 6, 0}, /* FE22 */
        {XXBYTE_VB_ID_IMPADCALLR4, 6, 0}, /* FE23 */
        {XXBYTE_VB_ID_IMPADCALLR8, 6, 0}, /* FE24 */
        {XXBYTE_VB_ID_IMPADCALLCY, 6, 0}, /* FE25 */
        {0, 0, 0}, /* FE26 */
        {XXBYTE_VB_ID_IMPADCALLI4, 6, 0}, /* FE27 */
        {XXBYTE_VB_ID_IMPADCALLI4, 6, 0}, /* FE28 */
        {XXBYTE_VB_ID_IMPADCALLFPR4, 6, 0}, /* FE29 */
        {XXBYTE_VB_ID_IMPADCALLFPR4, 6, 0}, /* FE2A */
        {0, 0, 0}, /* FE2B */
        {XXBYTE_VB_ID_IMPADCALLFPR4, 6, 0}, /* FE2C */
        {XXBYTE_VB_ID_IMPADCALLCBFRAME, 10, 0}, /* FE2D */
        {XXBYTE_VB_ID_LDSTKRF, 4, 0}, /* FE2E */
        {XXBYTE_VB_ID_LDFRAMERF, 4, 0}, /* FE2F */
        {0, 0, 0}, /* FE30 */
        {0, 0, 0}, /* FE31 */
        {0, 0, 0}, /* FE32 */
        {0, 0, 0}, /* FE33 */
        {0, 0, 0}, /* FE34 */
        {0, 0, 0}, /* FE35 */
        {0, 0, 0}, /* FE36 */
        {0, 0, 0}, /* FE37 */
        {0, 0, 0}, /* FE38 */
        {0, 0, 0}, /* FE39 */
        {0, 0, 0}, /* FE3A */
        {0, 0, 0}, /* FE3B */
        {0, 0, 0}, /* FE3C */
        {XXBYTE_VB_ID_LITVARUI1, 6, 0}, /* FE3D */
        {0, 0, 0}, /* FE3E */
        {0, 0, 0}, /* FE3F */
        {0, 0, 0}, /* FE40 */
        {0, 0, 0}, /* FE41 */
        {0, 0, 0}, /* FE42 */
        {0, 0, 0}, /* FE43 */
        {0, 0, 0}, /* FE44 */
        {0, 0, 0}, /* FE45 */
        {0, 0, 0}, /* FE46 */
        {0, 0, 0}, /* FE47 */
        {0, 0, 0}, /* FE48 */
        {0, 0, 0}, /* FE49 */
        {0, 0, 0}, /* FE4A */
        {0, 0, 0}, /* FE4B */
        {0, 0, 0}, /* FE4C */
        {XXBYTE_VB_ID_SETVARVAR, 2, 0}, /* FE4D */
        {XXBYTE_VB_ID_SETVARVARFUNC, 2, 0}, /* FE4E */
        {XXBYTE_VB_ID_IMPADCALLHRESULT, 6, 0}, /* FE4F */
        {0, 0, 0}, /* FE50 */
        {0, 0, 0}, /* FE51 */
        {0, 0, 0}, /* FE52 */
        {0, 0, 0}, /* FE53 */
        {0, 0, 0}, /* FE54 */
        {0, 0, 0}, /* FE55 */
        {0, 0, 0}, /* FE56 */
        {0, 0, 0}, /* FE57 */
        {0, 0, 0}, /* FE58 */
        {0, 0, 0}, /* FE59 */
        {0, 0, 0}, /* FE5A */
        {0, 0, 0}, /* FE5B */
        {0, 0, 0}, /* FE5C */
        {XXBYTE_VB_ID_OPENFILE, 4, 0}, /* FE5D */
        {XXBYTE_VB_ID_LOCKFILE, 4, 0}, /* FE5E */
        {0, 0, 0}, /* FE5F */
        {XXBYTE_VB_ID_ERASEDESTRUCT, 4, 0}, /* FE60 */
        {XXBYTE_VB_ID_LDFIXEDSTR, 4, 0}, /* FE61 */
        {XXBYTE_VB_ID_FORUI1, 6, 0}, /* FE62 */
        {XXBYTE_VB_ID_FORI2, 6, 0}, /* FE63 */
        {XXBYTE_VB_ID_FORI4, 6, 0}, /* FE64 */
        {XXBYTE_VB_ID_FORR4, 6, 0}, /* FE65 */
        {XXBYTE_VB_ID_FORR8, 6, 0}, /* FE66 */
        {XXBYTE_VB_ID_FORCY, 6, 0}, /* FE67 */
        {XXBYTE_VB_ID_FORVAR, 6, 0}, /* FE68 */
        {0, 0, 0}, /* FE69 */
        {XXBYTE_VB_ID_FORSTEPUI1, 6, 0}, /* FE6A */
        {XXBYTE_VB_ID_FORSTEPI2, 6, 0}, /* FE6B */
        {XXBYTE_VB_ID_FORSTEPI4, 6, 0}, /* FE6C */
        {XXBYTE_VB_ID_FORSTEPR4, 6, 0}, /* FE6D */
        {XXBYTE_VB_ID_FORSTEPR8, 6, 0}, /* FE6E */
        {XXBYTE_VB_ID_FORSTEPCY, 6, 0}, /* FE6F */
        {XXBYTE_VB_ID_FORSTEPVAR, 6, 0}, /* FE70 */
        {0, 0, 0}, /* FE71 */
        {XXBYTE_VB_ID_FOREACHCOLLVAR, 6, 0}, /* FE72 */
        {XXBYTE_VB_ID_NEXTEACHCOLLVAR, 6, 0}, /* FE73 */
        {XXBYTE_VB_ID_FOREACHCOLLAD, 6, 0}, /* FE74 */
        {XXBYTE_VB_ID_NEXTEACHCOLLAD, 6, 0}, /* FE75 */
        {XXBYTE_VB_ID_FOREACHARYVAR, 8, 0}, /* FE76 */
        {XXBYTE_VB_ID_NEXTEACHARYVAR, 8, 0}, /* FE77 */
        {XXBYTE_VB_ID_NEXTUI1, 6, 0}, /* FE78 */
        {XXBYTE_VB_ID_NEXTI2, 6, 0}, /* FE79 */
        {XXBYTE_VB_ID_NEXTI4, 6, 0}, /* FE7A */
        {XXBYTE_VB_ID_NEXTSTEPR4, 6, 0}, /* FE7B */
        {XXBYTE_VB_ID_NEXTR8, 6, 0}, /* FE7C */
        {XXBYTE_VB_ID_NEXTSTEPCY, 6, 0}, /* FE7D */
        {XXBYTE_VB_ID_NEXTSTEPVAR, 6, 0}, /* FE7E */
        {0, 0, 0}, /* FE7F */
        {XXBYTE_VB_ID_NEXTSTEPUI1, 6, 0}, /* FE80 */
        {XXBYTE_VB_ID_NEXTSTEPI2, 6, 0}, /* FE81 */
        {XXBYTE_VB_ID_NEXTSTEPI4, 6, 0}, /* FE82 */
        {XXBYTE_VB_ID_NEXTSTEPR4, 6, 0}, /* FE83 */
        {XXBYTE_VB_ID_NEXTR8, 6, 0}, /* FE84 */
        {XXBYTE_VB_ID_NEXTSTEPCY, 6, 0}, /* FE85 */
        {XXBYTE_VB_ID_NEXTSTEPVAR, 6, 0}, /* FE86 */
        {0, 0, 0}, /* FE87 */
        {XXBYTE_VB_ID_FOREACHCOLLOBJ, 8, 0}, /* FE88 */
        {XXBYTE_VB_ID_FOREACHVAR, 6, 0}, /* FE89 */
        {XXBYTE_VB_ID_FOREACHVARFREE, 6, 0}, /* FE8A */
        {XXBYTE_VB_ID_NEXTEACHCOLLOBJ, 8, 0}, /* FE8B */
        {XXBYTE_VB_ID_NEXTEACHVAR, 6, 0}, /* FE8C */
        {XXBYTE_VB_ID_CHECKTYPE, 4, 0}, /* FE8D */
        {XXBYTE_VB_ID_REDIM, 10, 0}, /* FE8E */
        {XXBYTE_VB_ID_REDIMPRESERVE, 10, 0}, /* FE8F */
        {XXBYTE_VB_ID_REDIMVAR, 6, 0}, /* FE90 */
        {XXBYTE_VB_ID_REDIMPRESERVEVAR, 6, 0}, /* FE91 */
        {XXBYTE_VB_ID_FDUPVAR, 6, 0}, /* FE92 */
        {XXBYTE_VB_ID_FDUPSTR, 6, 0}, /* FE93 */
        {0, 0, 0}, /* FE94 */
        {XXBYTE_VB_ID_ONGOSUB, 0, 0}, /* FE95 */
        {XXBYTE_VB_ID_ONGOTO, 0, 0}, /* FE96 */
        {XXBYTE_VB_ID_ADDREF, 2, 0}, /* FE97 */
        {XXBYTE_VB_ID_LATEMEMCALL, 6, 0}, /* FE98 */
        {XXBYTE_VB_ID_LATEMEMLDVAR, 6, 0}, /* FE99 */
        {XXBYTE_VB_ID_LATEMEMCALLLDVAR, 8, 0}, /* FE9A */
        {XXBYTE_VB_ID_LATEMEMST, 4, 0}, /* FE9B */
        {XXBYTE_VB_ID_LATEMEMCALLST, 6, 0}, /* FE9C */
        {XXBYTE_VB_ID_LATEMEMSTAD, 6, 0}, /* FE9D */
        {XXBYTE_VB_ID_EXITPROCFRAMECB, 6, 0}, /* FE9E */
        {XXBYTE_VB_ID_EXITPROCFRAMECBSTACK, 6, 0}, /* FE9F */
        {XXBYTE_VB_ID_LATEIDCALL, 8, 0}, /* FEA0 */
        {XXBYTE_VB_ID_LATEIDLDVAR, 8, 0}, /* FEA1 */
        {XXBYTE_VB_ID_LATEIDCALLLDVAR, 10, 0}, /* FEA2 */
        {XXBYTE_VB_ID_LATEIDST, 6, 0}, /* FEA3 */
        {XXBYTE_VB_ID_LATEIDCALLST, 8, 0}, /* FEA4 */
        {XXBYTE_VB_ID_LATEIDSTAD, 8, 0}, /* FEA5 */
        {XXBYTE_VB_ID_LATEMEMNAMEDCALL, 0, 0}, /* FEA6 */
        {XXBYTE_VB_ID_LATEMEMNAMEDCALLLDVAR, 0, 0}, /* FEA7 */
        {XXBYTE_VB_ID_LATEMEMNAMEDCALLST, 0, 0}, /* FEA8 */
        {XXBYTE_VB_ID_LATEMEMNAMEDSTAD, 0, 0}, /* FEA9 */
        {XXBYTE_VB_ID_LATEIDNAMEDCALL, 0, 0}, /* FEAA */
        {XXBYTE_VB_ID_LATEIDNAMEDCALLLDVAR, 0, 0}, /* FEAB */
        {XXBYTE_VB_ID_LATEIDNAMEDCALLST, 0, 0}, /* FEAC */
        {XXBYTE_VB_ID_LATEIDNAMEDSTAD, 0, 0}, /* FEAD */
        {XXBYTE_VB_ID_VARINDEXLDVAR, 6, 0}, /* FEAE */
        {XXBYTE_VB_ID_VARINDEXLDRFVAR, 6, 0}, /* FEAF */
        {XXBYTE_VB_ID_VARINDEXST, 4, 0}, /* FEB0 */
        {XXBYTE_VB_ID_VARINDEXSTAD, 4, 0}, /* FEB1 */
        {XXBYTE_VB_ID_FFREEVAR, 0, 0}, /* FEB2 */
        {XXBYTE_VB_ID_FFREESTR, 0, 0}, /* FEB3 */
        {XXBYTE_VB_ID_FFREEAD, 0, 0}, /* FEB4 */
        {XXBYTE_VB_ID_LITI2, 4, 0}, /* FEB5 */
        {XXBYTE_VB_ID_LITI2FP, 4, 0}, /* FEB6 */
        {XXBYTE_VB_ID_LITCY4, 6, 0}, /* FEB7 */
        {XXBYTE_VB_ID_LITI4, 6, 0}, /* FEB8 */
        {XXBYTE_VB_ID_LITI4, 6, 0}, /* FEB9 */
        {XXBYTE_VB_ID_LITR4FP, 6, 0}, /* FEBA */
        {XXBYTE_VB_ID_LITR8, 10, 0}, /* FEBB */
        {XXBYTE_VB_ID_LITR8, 10, 0}, /* FEBC */
        {XXBYTE_VB_ID_LITR8FP, 10, 0}, /* FEBD */
        {XXBYTE_VB_ID_LITR8FP, 10, 0}, /* FEBE */
        {XXBYTE_VB_ID_LITSTR, 4, 0}, /* FEBF */
        {XXBYTE_VB_ID_LITVARI2, 6, 0}, /* FEC0 */
        {XXBYTE_VB_ID_LITVARI4, 8, 0}, /* FEC1 */
        {XXBYTE_VB_ID_LITVARR4, 8, 0}, /* FEC2 */
        {XXBYTE_VB_ID_LITVARCY, 12, 0}, /* FEC3 */
        {XXBYTE_VB_ID_LITVARR8, 12, 0}, /* FEC4 */
        {XXBYTE_VB_ID_LITVARDATE, 12, 0}, /* FEC5 */
        {XXBYTE_VB_ID_LITVARSTR, 6, 0}, /* FEC6 */
        {XXBYTE_VB_ID_CSTRBOOL, 2, 0}, /* FEC7 */
        {XXBYTE_VB_ID_CSTRDATE, 2, 0}, /* FEC8 */
        {XXBYTE_VB_ID_CDATESTR, 2, 0}, /* FEC9 */
        {0, 0, 0}, /* FECA */
        {0, 0, 0}, /* FECB */
        {XXBYTE_VB_ID_FREESTRNOPOP, 2, 0}, /* FECC */
        {XXBYTE_VB_ID_FREEVARNOPOP, 2, 0}, /* FECD */
        {XXBYTE_VB_ID_FREEADNOPOP, 2, 0}, /* FECE */
        {XXBYTE_VB_ID_ERASENOPOP, 2, 0}, /* FECF */
        {XXBYTE_VB_ID_WMEMLDUI1, 4, 0}, /* FED0 */
        {XXBYTE_VB_ID_WMEMLDI2, 4, 0}, /* FED1 */
        {XXBYTE_VB_ID_WMEMLDSTR, 4, 0}, /* FED2 */
        {XXBYTE_VB_ID_WMEMLDSTR, 4, 0}, /* FED3 */
        {XXBYTE_VB_ID_WMEMLDCY, 4, 0}, /* FED4 */
        {XXBYTE_VB_ID_WMEMLDCY, 4, 0}, /* FED5 */
        {XXBYTE_VB_ID_WMEMLDRFVAR, 4, 0}, /* FED6 */
        {XXBYTE_VB_ID_WMEMLDSTR, 4, 0}, /* FED7 */
        {XXBYTE_VB_ID_WMEMLDSTR, 4, 0}, /* FED8 */
        {XXBYTE_VB_ID_WMEMLDFPR4, 4, 0}, /* FED9 */
        {XXBYTE_VB_ID_WMEMLDFPR8, 4, 0}, /* FEDA */
        {XXBYTE_VB_ID_IWMEMLDPR, 4, 0}, /* FEDB */
        {XXBYTE_VB_ID_WMEMLDRFVAR, 4, 0}, /* FEDC */
        {XXBYTE_VB_ID_WMEMLDVAR, 4, 0}, /* FEDD */
        {0, 0, 0}, /* FEDE */
        {0, 0, 0}, /* FEDF */
        {XXBYTE_VB_ID_WMEMSTUI1, 4, 0}, /* FEE0 */
        {XXBYTE_VB_ID_WMEMSTI2, 4, 0}, /* FEE1 */
        {XXBYTE_VB_ID_WMEMSTR4, 4, 0}, /* FEE2 */
        {XXBYTE_VB_ID_WMEMSTR4, 4, 0}, /* FEE3 */
        {XXBYTE_VB_ID_WMEMSTR8, 4, 0}, /* FEE4 */
        {XXBYTE_VB_ID_WMEMSTR8, 4, 0}, /* FEE5 */
        {XXBYTE_VB_ID_WMEMSTVAR, 4, 0}, /* FEE6 */
        {XXBYTE_VB_ID_WMEMSTSTR, 4, 0}, /* FEE7 */
        {XXBYTE_VB_ID_WMEMSTAD, 4, 0}, /* FEE8 */
        {XXBYTE_VB_ID_WMEMSTFPR4, 4, 0}, /* FEE9 */
        {XXBYTE_VB_ID_WMEMSTFPR8, 4, 0}, /* FEEA */
        {XXBYTE_VB_ID_WMEMSTVARAD, 4, 0}, /* FEEB */
        {XXBYTE_VB_ID_WMEMSTVARADFUNC, 4, 0}, /* FEEC */
        {XXBYTE_VB_ID_WMEMSTVARUNK, 4, 0}, /* FEED */
        {XXBYTE_VB_ID_WMEMSTVARUNKFUNC, 4, 0}, /* FEEE */
        {XXBYTE_VB_ID_WMEMSTADFUNC, 4, 0}, /* FEEF */
        {XXBYTE_VB_ID_WMEMSTVARCOPY, 4, 0}, /* FEF0 */
        {XXBYTE_VB_ID_WMEMSTSTRCOPY, 4, 0}, /* FEF1 */
        {XXBYTE_VB_ID_VARINDEXLDRFVARLOCK, 8, 0}, /* FEF2 */
        {0, 0, 0}, /* FEF3 */
        {0, 0, 0}, /* FEF4 */
        {XXBYTE_VB_ID_ASSIGNRECORD, 4, 0}, /* FEF5 */
        {XXBYTE_VB_ID_DESTRUCTANSIOFRAME, 6, 0}, /* FEF6 */
        {XXBYTE_VB_ID_FSTVARZERO, 4, 0}, /* FEF7 */
        {XXBYTE_VB_ID_FSTVARCOPYOBJ, 4, 0}, /* FEF8 */
        {XXBYTE_VB_ID_VERIFYVAROBJ, 2, 0}, /* FEF9 */
        {XXBYTE_VB_ID_VERIFYPVAROBJ, 2, 0}, /* FEFA */
        {XXBYTE_VB_ID_FNINSTRB4, 2, 0}, /* FEFB */
        {XXBYTE_VB_ID_FNINSTRB4VAR, 4, 0}, /* FEFC */
        {XXBYTE_VB_ID_FNINSTR4, 2, 0}, /* FEFD */
        {XXBYTE_VB_ID_FNINSTR4VAR, 4, 0}, /* FEFE */
        {XXBYTE_VB_ID_FNSTRCOMP3, 2, 0}, /* FEFF */
    },
    {
        {XXBYTE_VB_ID_FNSTRCOMP3VAR, 4, 0}, /* FF00 */
        {XXBYTE_VB_ID_STARYMOVE, 2, 0}, /* FF01 */
        {XXBYTE_VB_ID_STARYCOPY, 2, 0}, /* FF02 */
        {XXBYTE_VB_ID_STARYRECMOVE, 4, VB_OP_VB6}, /* FF03 */
        {XXBYTE_VB_ID_STARYRECCOPY, 4, VB_OP_VB6}, /* FF04 */
        {0, 0, 0}, /* FF05 */
        {XXBYTE_VB_ID_ARYINRECLDPR, 6, 0}, /* FF06 */
        {XXBYTE_VB_ID_ARYINRECLDRF, 6, 0}, /* FF07 */
        {XXBYTE_VB_ID_CEXTINSTUNK, 2, 0}, /* FF08 */
        {XXBYTE_VB_ID_ISTVARCOPYOBJ, 4, 0}, /* FF09 */
        {XXBYTE_VB_ID_ARRAYREBASE1VAR, 2, 0}, /* FF0A */
        {XXBYTE_VB_ID_ASSERT, 2, 0}, /* FF0B */
        {XXBYTE_VB_ID_RAISEEVENT, 8, 0}, /* FF0C */
        {XXBYTE_VB_ID_PRINTOBJECT, 6, 0}, /* FF0D */
        {XXBYTE_VB_ID_PRINTFILE, 6, 0}, /* FF0E */
        {XXBYTE_VB_ID_WRITEFILE, 6, 0}, /* FF0F */
        {XXBYTE_VB_ID_INPUTFILE, 6, 0}, /* FF10 */
        {0, 0, 0}, /* FF11 */
        {XXBYTE_VB_ID_GETRECFXSTR3, 2, 0}, /* FF12 */
        {XXBYTE_VB_ID_GETRECFXSTR4, 2, 0}, /* FF13 */
        {XXBYTE_VB_ID_PUTRECFXSTR3, 2, 0}, /* FF14 */
        {XXBYTE_VB_ID_PUTRECFXSTR4, 2, 0}, /* FF15 */
        {XXBYTE_VB_ID_GETRECOWN3, 4, 0}, /* FF16 */
        {XXBYTE_VB_ID_GETRECOWN4, 4, 0}, /* FF17 */
        {XXBYTE_VB_ID_PUTRECOWN3, 4, 0}, /* FF18 */
        {XXBYTE_VB_ID_PUTRECOWN4, 4, 0}, /* FF19 */
        {XXBYTE_VB_ID_LITI2_BYTE, 3, 0}, /* FF1A */
        {XXBYTE_VB_ID_CBOOLVARNULL, 2, 0}, /* FF1B */
        {XXBYTE_VB_ID_LARGEBOS, 3, 0}, /* FF1C */
        {0, 0, 0}, /* FF1D */
        {XXBYTE_VB_ID_IMPADCALLNONVIRT, 6, 0}, /* FF1E */
        {0, 0, 0}, /* FF1F */
        {0, 0, 0}, /* FF20 */
        {0, 0, 0}, /* FF21 */
        {0, 0, 0}, /* FF22 */
        {0, 0, 0}, /* FF23 */
        {0, 0, 0}, /* FF24 */
        {0, 0, 0}, /* FF25 */
        {0, 0, 0}, /* FF26 */
        {0, 0, 0}, /* FF27 */
        {0, 0, 0}, /* FF28 */
        {0, 0, 0}, /* FF29 */
        {XXBYTE_VB_ID_DESTRUCTRECORD, 4, 0}, /* FF2A */
        {XXBYTE_VB_ID_VCALLFPR8, 4, 0}, /* FF2B */
        {XXBYTE_VB_ID_THISVCALLHIDDEN, 4, 0}, /* FF2C */
        {XXBYTE_VB_ID_ZERORETVAL, 2, 0}, /* FF2D */
        {XXBYTE_VB_ID_ZERORETVALVAR, 2, 0}, /* FF2E */
        {XXBYTE_VB_ID_EXITPROCCBHRESULT, 6, 0}, /* FF2F */
        {XXBYTE_VB_ID_EXITPROCFRAMECBHRESULT, 8, 0}, /* FF30 */
        {XXBYTE_VB_ID_ERASEDESTRKEEPDATA, 4, 0}, /* FF31 */
        {XXBYTE_VB_ID_CDARGREFUDT, 6, VB_OP_VB6}, /* FF32 */
        {XXBYTE_VB_ID_CVARREFUDT, 6, VB_OP_VB6}, /* FF33 */
        {XXBYTE_VB_ID_CVARUDT, 6, VB_OP_VB6}, /* FF34 */
        {XXBYTE_VB_ID_STUDTVAR, 4, VB_OP_VB6}, /* FF35 */
        {XXBYTE_VB_ID_STARYVAR, 4, VB_OP_VB6}, /* FF36 */
        {XXBYTE_VB_ID_COPYBYTESZERO, 4, VB_OP_VB6}, /* FF37 */
        {XXBYTE_VB_ID_FLDZEROARY, 6, VB_OP_VB6}, /* FF38 */
        {XXBYTE_VB_ID_FSTVARZERO, 4, VB_OP_VB6}, /* FF39 */
        {XXBYTE_VB_ID_CVARARYUDT, 8, VB_OP_VB6}, /* FF3A */
        {XXBYTE_VB_ID_REDIMVARUDT, 8, VB_OP_VB6}, /* FF3B */
        {XXBYTE_VB_ID_REDIMPRESERVEVARUDT, 8, VB_OP_VB6}, /* FF3C */
        {XXBYTE_VB_ID_VARLATEMEMLDRFVAR, 6, VB_OP_VB6}, /* FF3D */
        {XXBYTE_VB_ID_VARLATEMEMCALLLDRFVAR, 8, VB_OP_VB6}, /* FF3E */
        {0, 0, 0}, /* FF3F */
        {0, 0, 0}, /* FF40 */
        {XXBYTE_VB_ID_VARLATEMEMLDVAR, 6, VB_OP_VB6}, /* FF41 */
        {XXBYTE_VB_ID_VARLATEMEMCALLLDVAR, 8, VB_OP_VB6}, /* FF42 */
        {XXBYTE_VB_ID_VARLATEMEMST, 4, VB_OP_VB6}, /* FF43 */
        {XXBYTE_VB_ID_VARLATEMEMCALLST, 6, VB_OP_VB6}, /* FF44 */
        {XXBYTE_VB_ID_VARLATEMEMSTAD, 6, VB_OP_VB6}, /* FF45 */
        {0, 0, 0}, /* FF46 */
        {0, 0, 0}, /* FF47 */
        {0, 0, 0}, /* FF48 */
        {0, 0, 0}, /* FF49 */
        {0, 0, 0}, /* FF4A */
        {0, 0, 0}, /* FF4B */
        {0, 0, 0}, /* FF4C */
        {0, 0, 0}, /* FF4D */
        {0, 0, 0}, /* FF4E */
        {0, 0, 0}, /* FF4F */
        {0, 0, 0}, /* FF50 */
        {0, 0, 0}, /* FF51 */
        {0, 0, 0}, /* FF52 */
        {0, 0, 0}, /* FF53 */
        {0, 0, 0}, /* FF54 */
        {0, 0, 0}, /* FF55 */
        {0, 0, 0}, /* FF56 */
        {0, 0, 0}, /* FF57 */
        {0, 0, 0}, /* FF58 */
        {0, 0, 0}, /* FF59 */
        {0, 0, 0}, /* FF5A */
        {0, 0, 0}, /* FF5B */
        {0, 0, 0}, /* FF5C */
        {0, 0, 0}, /* FF5D */
        {0, 0, 0}, /* FF5E */
        {0, 0, 0}, /* FF5F */
        {0, 0, 0}, /* FF60 */
        {0, 0, 0}, /* FF61 */
        {0, 0, 0}, /* FF62 */
        {0, 0, 0}, /* FF63 */
        {0, 0, 0}, /* FF64 */
        {0, 0, 0}, /* FF65 */
        {0, 0, 0}, /* FF66 */
        {0, 0, 0}, /* FF67 */
        {0, 0, 0}, /* FF68 */
        {0, 0, 0}, /* FF69 */
        {0, 0, 0}, /* FF6A */
        {0, 0, 0}, /* FF6B */
        {0, 0, 0}, /* FF6C */
        {0, 0, 0}, /* FF6D */
        {0, 0, 0}, /* FF6E */
        {0, 0, 0}, /* FF6F */
        {0, 0, 0}, /* FF70 */
        {0, 0, 0}, /* FF71 */
        {0, 0, 0}, /* FF72 */
        {0, 0, 0}, /* FF73 */
        {0, 0, 0}, /* FF74 */
        {0, 0, 0}, /* FF75 */
        {0, 0, 0}, /* FF76 */
        {0, 0, 0}, /* FF77 */
        {0, 0, 0}, /* FF78 */
        {0, 0, 0}, /* FF79 */
        {0, 0, 0}, /* FF7A */
        {0, 0, 0}, /* FF7B */
        {0, 0, 0}, /* FF7C */
        {0, 0, 0}, /* FF7D */
        {0, 0, 0}, /* FF7E */
        {0, 0, 0}, /* FF7F */
        {0, 0, 0}, /* FF80 */
        {0, 0, 0}, /* FF81 */
        {0, 0, 0}, /* FF82 */
        {0, 0, 0}, /* FF83 */
        {0, 0, 0}, /* FF84 */
        {0, 0, 0}, /* FF85 */
        {0, 0, 0}, /* FF86 */
        {0, 0, 0}, /* FF87 */
        {0, 0, 0}, /* FF88 */
        {0, 0, 0}, /* FF89 */
        {0, 0, 0}, /* FF8A */
        {0, 0, 0}, /* FF8B */
        {0, 0, 0}, /* FF8C */
        {0, 0, 0}, /* FF8D */
        {0, 0, 0}, /* FF8E */
        {0, 0, 0}, /* FF8F */
        {0, 0, 0}, /* FF90 */
        {0, 0, 0}, /* FF91 */
        {0, 0, 0}, /* FF92 */
        {0, 0, 0}, /* FF93 */
        {0, 0, 0}, /* FF94 */
        {0, 0, 0}, /* FF95 */
        {0, 0, 0}, /* FF96 */
        {0, 0, 0}, /* FF97 */
        {0, 0, 0}, /* FF98 */
        {0, 0, 0}, /* FF99 */
        {0, 0, 0}, /* FF9A */
        {0, 0, 0}, /* FF9B */
        {0, 0, 0}, /* FF9C */
        {0, 0, 0}, /* FF9D */
        {0, 0, 0}, /* FF9E */
        {0, 0, 0}, /* FF9F */
        {0, 0, 0}, /* FFA0 */
        {0, 0, 0}, /* FFA1 */
        {0, 0, 0}, /* FFA2 */
        {0, 0, 0}, /* FFA3 */
        {0, 0, 0}, /* FFA4 */
        {0, 0, 0}, /* FFA5 */
        {0, 0, 0}, /* FFA6 */
        {0, 0, 0}, /* FFA7 */
        {0, 0, 0}, /* FFA8 */
        {0, 0, 0}, /* FFA9 */
        {0, 0, 0}, /* FFAA */
        {0, 0, 0}, /* FFAB */
        {0, 0, 0}, /* FFAC */
        {0, 0, 0}, /* FFAD */
        {0, 0, 0}, /* FFAE */
        {0, 0, 0}, /* FFAF */
        {0, 0, 0}, /* FFB0 */
        {0, 0, 0}, /* FFB1 */
        {0, 0, 0}, /* FFB2 */
        {0, 0, 0}, /* FFB3 */
        {0, 0, 0}, /* FFB4 */
        {0, 0, 0}, /* FFB5 */
        {0, 0, 0}, /* FFB6 */
        {0, 0, 0}, /* FFB7 */
        {0, 0, 0}, /* FFB8 */
        {0, 0, 0}, /* FFB9 */
        {0, 0, 0}, /* FFBA */
        {0, 0, 0}, /* FFBB */
        {0, 0, 0}, /* FFBC */
        {0, 0, 0}, /* FFBD */
        {0, 0, 0}, /* FFBE */
        {0, 0, 0}, /* FFBF */
        {0, 0, 0}, /* FFC0 */
        {0, 0, 0}, /* FFC1 */
        {0, 0, 0}, /* FFC2 */
        {0, 0, 0}, /* FFC3 */
        {0, 0, 0}, /* FFC4 */
        {0, 0, 0}, /* FFC5 */
        {0, 0, 0}, /* FFC6 */
        {0, 0, 0}, /* FFC7 */
        {0, 0, 0}, /* FFC8 */
        {0, 0, 0}, /* FFC9 */
        {0, 0, 0}, /* FFCA */
        {0, 0, 0}, /* FFCB */
        {0, 0, 0}, /* FFCC */
        {0, 0, 0}, /* FFCD */
        {0, 0, 0}, /* FFCE */
        {0, 0, 0}, /* FFCF */
        {0, 0, 0}, /* FFD0 */
        {0, 0, 0}, /* FFD1 */
        {0, 0, 0}, /* FFD2 */
        {0, 0, 0}, /* FFD3 */
        {0, 0, 0}, /* FFD4 */
        {0, 0, 0}, /* FFD5 */
        {0, 0, 0}, /* FFD6 */
        {0, 0, 0}, /* FFD7 */
        {0, 0, 0}, /* FFD8 */
        {0, 0, 0}, /* FFD9 */
        {0, 0, 0}, /* FFDA */
        {0, 0, 0}, /* FFDB */
        {0, 0, 0}, /* FFDC */
        {0, 0, 0}, /* FFDD */
        {0, 0, 0}, /* FFDE */
        {0, 0, 0}, /* FFDF */
        {0, 0, 0}, /* FFE0 */
        {0, 0, 0}, /* FFE1 */
        {0, 0, 0}, /* FFE2 */
        {0, 0, 0}, /* FFE3 */
        {0, 0, 0}, /* FFE4 */
        {0, 0, 0}, /* FFE5 */
        {0, 0, 0}, /* FFE6 */
        {0, 0, 0}, /* FFE7 */
        {0, 0, 0}, /* FFE8 */
        {0, 0, 0}, /* FFE9 */
        {0, 0, 0}, /* FFEA */
        {0, 0, 0}, /* FFEB */
        {0, 0, 0}, /* FFEC */
        {0, 0, 0}, /* FFED */
        {0, 0, 0}, /* FFEE */
        {0, 0, 0}, /* FFEF */
        {0, 0, 0}, /* FFF0 */
        {0, 0, 0}, /* FFF1 */
        {0, 0, 0}, /* FFF2 */
        {0, 0, 0}, /* FFF3 */
        {0, 0, 0}, /* FFF4 */
        {0, 0, 0}, /* FFF5 */
        {0, 0, 0}, /* FFF6 */
        {0, 0, 0}, /* FFF7 */
        {0, 0, 0}, /* FFF8 */
        {0, 0, 0}, /* FFF9 */
        {0, 0, 0}, /* FFFA */
        {0, 0, 0}, /* FFFB */
        {0, 0, 0}, /* FFFC */
        {0, 0, 0}, /* FFFD */
        {0, 0, 0}, /* FFFE */
        {0, 0, 0}, /* FFFF */
    },
};

#else
typedef int xxbyte_vb_tables_disabled;
#endif
