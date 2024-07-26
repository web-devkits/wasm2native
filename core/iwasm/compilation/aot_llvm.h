/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _AOT_LLVM_H_
#define _AOT_LLVM_H_

#include "aot.h"
#include "aot_comp_option.h"
#include "../common/wasm_runtime.h"

#include "llvm/Config/llvm-config.h"
#include "llvm-c/Types.h"
#include "llvm-c/Target.h"
#include "llvm-c/Core.h"
#include "llvm-c/Object.h"
#include "llvm-c/ExecutionEngine.h"
#include "llvm-c/Analysis.h"
#include "llvm-c/BitWriter.h"
#if LLVM_VERSION_MAJOR < 17
#include "llvm-c/Transforms/Utils.h"
#include "llvm-c/Transforms/Scalar.h"
#include "llvm-c/Transforms/Vectorize.h"
#include "llvm-c/Transforms/PassManagerBuilder.h"
#include "llvm-c/Initialization.h"
#endif

#include "llvm-c/Error.h"
#include "llvm-c/Support.h"

#include "llvm-c/TargetMachine.h"

#ifdef __cplusplus
extern "C" {
#endif

#if LLVM_VERSION_MAJOR < 14
#define LLVMBuildLoad2(builder, type, value, name) \
    LLVMBuildLoad(builder, value, name)

#define LLVMBuildCall2(builder, type, func, args, num_args, name) \
    LLVMBuildCall(builder, func, args, num_args, name)

#define LLVMBuildInBoundsGEP2(builder, type, ptr, indices, num_indices, name) \
    LLVMBuildInBoundsGEP(builder, ptr, indices, num_indices, name)
#else
/* Opaque pointer type */
#define OPQ_PTR_TYPE INT8_PTR_TYPE
#endif

/**
 * Value in the WASM operation stack, each stack element
 * is an LLVM value
 */
typedef struct AOTValue {
    struct AOTValue *next;
    struct AOTValue *prev;
    LLVMValueRef value;
    /* VALUE_TYPE_I32/I64/F32/F64/VOID */
    uint8 type;
    bool is_local;
    uint32 local_idx;
} AOTValue;

/**
 * Value stack, represents stack elements in a WASM block
 */
typedef struct AOTValueStack {
    AOTValue *value_list_head;
    AOTValue *value_list_end;
} AOTValueStack;

typedef struct AOTBlock {
    struct AOTBlock *next;
    struct AOTBlock *prev;

    /* Block index */
    uint32 block_index;
    /* LABEL_TYPE_BLOCK/LOOP/IF/FUNCTION */
    uint32 label_type;
    /* Whether it is reachable */
    bool is_reachable;
    /* Whether skip translation of wasm else branch */
    bool skip_wasm_code_else;

    /* code of else opcode of this block, if it is a IF block  */
    uint8 *wasm_code_else;
    /* code end of this block */
    uint8 *wasm_code_end;

    /* LLVM label points to code begin */
    LLVMBasicBlockRef llvm_entry_block;
    /* LLVM label points to code else */
    LLVMBasicBlockRef llvm_else_block;
    /* LLVM label points to code end */
    LLVMBasicBlockRef llvm_end_block;

    /* WASM operation stack */
    AOTValueStack value_stack;

    /* Param count/types/PHIs of this block */
    uint32 param_count;
    uint8 *param_types;
    LLVMValueRef *param_phis;
    LLVMValueRef *else_param_phis;

    /* Result count/types/PHIs of this block */
    uint32 result_count;
    uint8 *result_types;
    LLVMValueRef *result_phis;
} AOTBlock;

/**
 * Block stack, represents WASM block stack elements
 */
typedef struct AOTBlockStack {
    AOTBlock *block_list_head;
    AOTBlock *block_list_end;
    /* Current block index of each block type */
    uint32 block_index[3];
} AOTBlockStack;

typedef struct AOTCheckedAddr {
    struct AOTCheckedAddr *next;
    uint32 local_idx;
    uint32 offset;
    uint32 bytes;
} AOTCheckedAddr, *AOTCheckedAddrList;

typedef struct AOTFuncContext {
    AOTFunc *aot_func;
    LLVMValueRef func;
    LLVMTypeRef func_type;
    LLVMModuleRef module;
    AOTBlockStack block_stack;

    LLVMValueRef memory_data;

    LLVMBasicBlockRef got_exception_block;
    LLVMBasicBlockRef func_return_block;
    LLVMValueRef exception_id_phi;

    LLVMValueRef locals[1];
} AOTFuncContext;

typedef struct AOTLLVMTypes {
    LLVMTypeRef int1_type;
    LLVMTypeRef int8_type;
    LLVMTypeRef int16_type;
    LLVMTypeRef int32_type;
    LLVMTypeRef int64_type;
    LLVMTypeRef float32_type;
    LLVMTypeRef float64_type;
    LLVMTypeRef void_type;

    LLVMTypeRef int8_ptr_type;
    LLVMTypeRef int8_pptr_type;
    LLVMTypeRef int16_ptr_type;
    LLVMTypeRef int32_ptr_type;
    LLVMTypeRef int64_ptr_type;
    LLVMTypeRef float32_ptr_type;
    LLVMTypeRef float64_ptr_type;

    LLVMTypeRef v128_type;
    LLVMTypeRef v128_ptr_type;
    LLVMTypeRef i8x16_vec_type;
    LLVMTypeRef i16x8_vec_type;
    LLVMTypeRef i32x4_vec_type;
    LLVMTypeRef i64x2_vec_type;
    LLVMTypeRef f32x4_vec_type;
    LLVMTypeRef f64x2_vec_type;

    LLVMTypeRef i1x2_vec_type;

    LLVMTypeRef meta_data_type;
} AOTLLVMTypes;

typedef struct AOTLLVMConsts {
    LLVMValueRef i1_zero;
    LLVMValueRef i1_one;
    LLVMValueRef i8_zero;
    LLVMValueRef i8_one;
    LLVMValueRef i32_zero;
    LLVMValueRef i64_zero;
    LLVMValueRef f32_zero;
    LLVMValueRef f64_zero;
    LLVMValueRef i32_one;
    LLVMValueRef i32_two;
    LLVMValueRef i32_three;
    LLVMValueRef i32_four;
    LLVMValueRef i32_five;
    LLVMValueRef i32_six;
    LLVMValueRef i32_seven;
    LLVMValueRef i32_eight;
    LLVMValueRef i32_nine;
    LLVMValueRef i32_ten;
    LLVMValueRef i32_eleven;
    LLVMValueRef i32_twelve;
    LLVMValueRef i32_thirteen;
    LLVMValueRef i32_fourteen;
    LLVMValueRef i32_fifteen;
    LLVMValueRef i32_neg_one;
    LLVMValueRef i64_neg_one;
    LLVMValueRef i32_min;
    LLVMValueRef i64_min;
    LLVMValueRef i32_31;
    LLVMValueRef i32_32;
    LLVMValueRef i64_63;
    LLVMValueRef i64_64;
    LLVMValueRef i8x16_vec_zero;
    LLVMValueRef i16x8_vec_zero;
    LLVMValueRef i32x4_vec_zero;
    LLVMValueRef i64x2_vec_zero;
    LLVMValueRef f32x4_vec_zero;
    LLVMValueRef f64x2_vec_zero;
    LLVMValueRef i8x16_undef;
    LLVMValueRef i16x8_undef;
    LLVMValueRef i32x4_undef;
    LLVMValueRef i64x2_undef;
    LLVMValueRef f32x4_undef;
    LLVMValueRef f64x2_undef;
    LLVMValueRef i32x16_zero;
    LLVMValueRef i32x8_zero;
    LLVMValueRef i32x4_zero;
    LLVMValueRef i32x2_zero;
} AOTLLVMConsts;

/**
 * Compiler context
 */
typedef struct AOTCompContext {
    const AOTCompData *comp_data;

    /* LLVM variables required to emit LLVM IR */
    LLVMContextRef context;
    LLVMBuilderRef builder;
    LLVMTargetMachineRef target_machine;
    char *target_cpu;
    char target_arch[16];
    unsigned pointer_size;

    LLVMModuleRef module;

    /* Whether to enable the no-sandbox mode */
    bool no_sandbox_mode;

    /* 128-bit SIMD */
    bool enable_simd;

    /* Auxiliary stack overflow/underflow check */
    bool enable_aux_stack_check;

    /* Disable LLVM link time optimization */
    bool disable_llvm_lto;

    /* Whether optimize the machine code */
    bool optimize;

    uint32 opt_level;
    uint32 size_level;

    /* LLVM floating-point rounding mode metadata */
    LLVMValueRef fp_rounding_mode;

    /* LLVM floating-point exception behavior metadata */
    LLVMValueRef fp_exception_behavior;

    /* LLVM data types */
    AOTLLVMTypes basic_types;

    /* LLVM const values */
    AOTLLVMConsts llvm_consts;

    uint32 import_func_count;
    LLVMValueRef *import_func_ptrs;

    /* Function contexts */
    AOTFuncContext **func_ctxes;
    uint32 func_ctx_count;
    char **custom_sections_wp;
    uint32 custom_sections_count;
} AOTCompContext;

bool
aot_compiler_init(void);

void
aot_compiler_destroy(void);

AOTCompContext *
aot_create_comp_context(AOTCompData *comp_data, aot_comp_option_t option);

void
aot_destroy_comp_context(AOTCompContext *comp_ctx);

int32
aot_get_native_symbol_index(AOTCompContext *comp_ctx, const char *symbol);

bool
aot_compile_wasm(AOTCompContext *comp_ctx);

uint8 *
aot_emit_elf_file(AOTCompContext *comp_ctx, uint32 *p_elf_file_size);

void
aot_destroy_elf_file(uint8 *elf_file);

void
aot_value_stack_push(AOTValueStack *stack, AOTValue *value);

AOTValue *
aot_value_stack_pop(AOTValueStack *stack);

void
aot_value_stack_destroy(AOTValueStack *stack);

void
aot_block_stack_push(AOTBlockStack *stack, AOTBlock *block);

AOTBlock *
aot_block_stack_pop(AOTBlockStack *stack);

void
aot_block_stack_destroy(AOTBlockStack *stack);

void
aot_block_destroy(AOTBlock *block);

LLVMTypeRef
wasm_type_to_llvm_type(const AOTLLVMTypes *llvm_types, uint8 wasm_type);

bool
aot_build_zero_function_ret(const AOTCompContext *comp_ctx,
                            AOTFuncContext *func_ctx, AOTFuncType *func_type);

LLVMValueRef
aot_call_llvm_intrinsic(const AOTCompContext *comp_ctx,
                        const AOTFuncContext *func_ctx, const char *intrinsic,
                        LLVMTypeRef ret_type, LLVMTypeRef *param_types,
                        int param_count, ...);

LLVMValueRef
aot_call_llvm_intrinsic_v(const AOTCompContext *comp_ctx,
                          const AOTFuncContext *func_ctx, const char *intrinsic,
                          LLVMTypeRef ret_type, LLVMTypeRef *param_types,
                          int param_count, va_list param_value_list);

bool
aot_check_simd_compatibility(const char *arch_c_str, const char *cpu_c_str);

void
aot_apply_llvm_new_pass_manager(AOTCompContext *comp_ctx, LLVMModuleRef module);

/* This allow APIs to pass LLVMModule argument to CreateGlobalStringPtr */
LLVMValueRef
LLVMBuildGlobalStringPtr_v2(LLVMBuilderRef B, const char *Str, const char *Name,
                            LLVMModuleRef module);

void
aot_handle_llvm_errmsg(const char *string, LLVMErrorRef err);

LLVMValueRef
aot_get_memory_base_addr(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* end of _AOT_LLVM_H_ */
