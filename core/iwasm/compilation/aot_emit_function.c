/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_emit_function.h"
#include "aot_emit_exception.h"
#include "aot_emit_control.h"

#define ADD_BASIC_BLOCK(block, name)                                          \
    do {                                                                      \
        if (!(block = LLVMAppendBasicBlockInContext(comp_ctx->context,        \
                                                    func_ctx->func, name))) { \
            aot_set_last_error("llvm add basic block failed.");               \
            goto fail;                                                        \
        }                                                                     \
    } while (0)

static bool
create_func_return_block(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    LLVMBasicBlockRef block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    AOTFuncType *aot_func_type = func_ctx->aot_func->func_type;

    /* Create function return block if it isn't created */
    if (!func_ctx->func_return_block) {
        if (!(func_ctx->func_return_block = LLVMAppendBasicBlockInContext(
                  comp_ctx->context, func_ctx->func, "func_ret"))) {
            aot_set_last_error("llvm add basic block failed.");
            return false;
        }

        /* Create return IR */
        LLVMPositionBuilderAtEnd(comp_ctx->builder,
                                 func_ctx->func_return_block);
        if (!aot_build_zero_function_ret(comp_ctx, func_ctx, aot_func_type)) {
            return false;
        }
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, block_curr);
    return true;
}

/* Check whether there was exception thrown, if yes, return directly */
static bool
check_exception_thrown(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    LLVMBasicBlockRef check_exce_succ_block;
    LLVMValueRef exce_id_global, exce_id, cmp;

    if (comp_ctx->no_sandbox_mode)
        return true;

    /* Create function return block if it isn't created */
    if (!create_func_return_block(comp_ctx, func_ctx))
        return false;

    if (!(check_exce_succ_block = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_exce_succ"))) {
        aot_set_last_error("llvm add basic block failed.");
        return false;
    }
    LLVMMoveBasicBlockAfter(check_exce_succ_block,
                            LLVMGetInsertBlock(comp_ctx->builder));

    exce_id_global = LLVMGetNamedGlobal(comp_ctx->module, "exception_id");
    bh_assert(exce_id_global);
    if (!(exce_id = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, exce_id_global,
                                   "exce_id"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }

    if (!(cmp = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, exce_id, I32_ZERO,
                              "cmp"))) {
        aot_set_last_error("llvm build cmp failed.");
        return false;
    }
    if (!LLVMBuildCondBr(comp_ctx->builder, cmp, check_exce_succ_block,
                         func_ctx->func_return_block)) {
        aot_set_last_error("llvm build cond br failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_exce_succ_block);
    return true;
}

bool
aot_compile_op_call(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                    uint32 func_idx, bool tail_call, WASMRelocation *relocation)
{
    uint32 import_func_count = comp_ctx->comp_data->import_func_count;
    AOTImportFunc *import_funcs = comp_ctx->comp_data->import_funcs;
    uint32 func_count = comp_ctx->func_ctx_count;
    uint32 ext_ret_cell_num = 0, cell_num = 0;
    AOTFuncContext **func_ctxes = comp_ctx->func_ctxes;
    AOTFuncType *func_type;
    LLVMTypeRef *param_types = NULL, ret_type, array_type;
    LLVMTypeRef ext_ret_ptr_type;
    LLVMValueRef *param_values = NULL, value_ret = NULL, func;
    LLVMValueRef argv_buf;
    LLVMValueRef ext_ret, ext_ret_ptr, ext_ret_idx;
    int32 i, j = 0, param_count, result_count, ext_ret_count;
    uint64 total_size;
    uint8 wasm_ret_type;
    uint8 *ext_ret_types = NULL;
    bool ret = false;
    char buf[32];

    /* Check function index */
    if (func_idx >= import_func_count + func_count) {
        aot_set_last_error("Function index out of range.");
        return false;
    }

    /* Get function type */
    if (func_idx < import_func_count) {
        func_type = import_funcs[func_idx].func_type;
    }
    else {
        func_type =
            func_ctxes[func_idx - import_func_count]->aot_func->func_type;
    }

    /* Allocate memory for parameters.
     * Parameters layout:
     *   - wasm function's parameters
     *   - extra results'(except the first one) addresses
     */
    param_count = (int32)func_type->param_count;
    result_count = (int32)func_type->result_count;
    ext_ret_count = result_count > 1 ? result_count - 1 : 0;
    total_size = sizeof(LLVMValueRef) * (uint64)(param_count + ext_ret_count);
    if (total_size > 0) {
        if (total_size >= UINT32_MAX
            || !(param_values = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed.");
            return false;
        }
    }

    /* Pop parameters from stack */
    for (i = param_count - 1; i >= 0; i--)
        POP(param_values[i + j], func_type->types[i]);

    /* Set parameters for multiple return values, the first return value
       is returned by function return value, and the other return values
       are returned by function parameters with pointer types */
    if (ext_ret_count > 0) {
        ext_ret_types = func_type->types + param_count + 1;
        ext_ret_cell_num = wasm_get_cell_num(ext_ret_types, ext_ret_count);

        if (!(array_type = LLVMArrayType(I32_TYPE, ext_ret_cell_num))) {
            aot_set_last_error("create llvm array type failed");
            goto fail;
        }
        if (!(argv_buf =
                  LLVMBuildAlloca(comp_ctx->builder, array_type, "argv_buf"))) {
            aot_set_last_error("llvm build alloca failed.");
            goto fail;
        }

        cell_num = 0;
        for (i = 0; i < ext_ret_count; i++) {
            if (!(ext_ret_idx = I32_CONST(cell_num))
                || !(ext_ret_ptr_type =
                         LLVMPointerType(TO_LLVM_TYPE(ext_ret_types[i]), 0))) {
                aot_set_last_error("llvm add const or pointer type failed.");
                goto fail;
            }

            snprintf(buf, sizeof(buf), "ext_ret%d_ptr", i);
            if (!(ext_ret_ptr =
                      LLVMBuildInBoundsGEP2(comp_ctx->builder, I32_TYPE,
                                            argv_buf, &ext_ret_idx, 1, buf))) {
                aot_set_last_error("llvm build GEP failed.");
                goto fail;
            }
            snprintf(buf, sizeof(buf), "ext_ret%d_ptr_cast", i);
            if (!(ext_ret_ptr = LLVMBuildBitCast(comp_ctx->builder, ext_ret_ptr,
                                                 ext_ret_ptr_type, buf))) {
                aot_set_last_error("llvm build bit cast failed.");
                goto fail;
            }
            param_values[param_count + i] = ext_ret_ptr;
            cell_num += wasm_value_type_cell_num(ext_ret_types[i]);
        }
    }

    if (func_idx < import_func_count) {
        /* Initialize parameter types of the LLVM function */
        total_size = sizeof(LLVMTypeRef) * (uint64)param_count;
        if (total_size > 0) {
            if (total_size >= UINT32_MAX
                || !(param_types = wasm_runtime_malloc((uint32)total_size))) {
                aot_set_last_error("allocate memory failed.");
                goto fail;
            }
        }

        for (i = 0; i < param_count; i++, j++) {
            param_types[i] = TO_LLVM_TYPE(func_type->types[i]);
        }

        if (func_type->result_count) {
            wasm_ret_type = func_type->types[func_type->param_count];
            ret_type = TO_LLVM_TYPE(wasm_ret_type);
        }
        else {
            wasm_ret_type = VALUE_TYPE_VOID;
            ret_type = VOID_TYPE;
        }

        LLVMTypeRef native_func_type, func_ptr_type;
        LLVMValueRef func_ptr;

        if (!(native_func_type = LLVMFunctionType(ret_type, param_types,
                                                  param_count, false))) {
            aot_set_last_error("llvm add function type failed.");
            goto fail;
        }

        if (!(func_ptr_type = LLVMPointerType(native_func_type, 0))) {
            aot_set_last_error("create LLVM function type failed.");
            goto fail;
        }

        func_ptr = comp_ctx->import_func_ptrs[func_idx];
        bh_assert(func_ptr);

        if (!(func = LLVMBuildBitCast(comp_ctx->builder, func_ptr,
                                      func_ptr_type, "native_func"))) {
            aot_set_last_error("llvm bit cast failed.");
            goto fail;
        }

        LLVMBasicBlockRef check_func_ptr_succ_block;
        LLVMValueRef cmp;

        if (!comp_ctx->no_sandbox_mode) {
            if (!(check_func_ptr_succ_block = LLVMAppendBasicBlockInContext(
                      comp_ctx->context, func_ctx->func,
                      "check_func_ptr_succ"))) {
                aot_set_last_error("llvm add basic block failed.");
                goto fail;
            }
            LLVMMoveBasicBlockAfter(check_func_ptr_succ_block,
                                    LLVMGetInsertBlock(comp_ctx->builder));

            if (!(cmp = LLVMBuildIsNull(comp_ctx->builder, func, "is_null"))) {
                aot_set_last_error("llvm build is null failed.");
                goto fail;
            }

            if (!aot_emit_exception(comp_ctx, func_ctx,
                                    EXCE_CALL_UNLINKED_IMPORT_FUNC, true, cmp,
                                    check_func_ptr_succ_block)) {
                goto fail;
            }

            LLVMPositionBuilderAtEnd(comp_ctx->builder,
                                     check_func_ptr_succ_block);
        }

        /* Call the function */
        if (!(value_ret = LLVMBuildCall2(
                  comp_ctx->builder, native_func_type, func, param_values,
                  (uint32)param_count,
                  (func_type->result_count > 0 ? "call" : "")))) {
            aot_set_last_error("LLVM build call failed.");
            goto fail;
        }
    }
    else {
#if LLVM_VERSION_MAJOR >= 14
        LLVMTypeRef llvm_func_type;
#endif
        if (func_ctxes[func_idx - import_func_count] == func_ctx) {
            /* recursive call */
            func = func_ctx->func;
        }
        else {
            func = func_ctxes[func_idx - import_func_count]->func;
        }

#if LLVM_VERSION_MAJOR >= 14
        llvm_func_type = func_ctxes[func_idx - import_func_count]->func_type;
#endif

        /* Call the function */
        if (!(value_ret = LLVMBuildCall2(
                  comp_ctx->builder, llvm_func_type, func, param_values,
                  (uint32)param_count + ext_ret_count,
                  (func_type->result_count > 0 ? "call" : "")))) {
            aot_set_last_error("LLVM build call failed.");
            goto fail;
        }

        if (tail_call)
            LLVMSetTailCall(value_ret, true);
    }

    if (func_type->result_count > 0) {
        /* Push the first result to stack */
        PUSH(value_ret, func_type->types[func_type->param_count]);
        /* Load extra result from its address and push to stack */
        for (i = 0; i < ext_ret_count; i++) {
            snprintf(buf, sizeof(buf), "func%d_ext_ret%d", func_idx, i);
            if (!(ext_ret = LLVMBuildLoad2(
                      comp_ctx->builder, TO_LLVM_TYPE(ext_ret_types[i]),
                      param_values[1 + param_count + i], buf))) {
                aot_set_last_error("llvm build load failed.");
                goto fail;
            }
            PUSH(ext_ret, ext_ret_types[i]);
        }
    }

    if (!check_exception_thrown(comp_ctx, func_ctx)) {
        goto fail;
    }

    ret = true;
fail:
    if (param_types)
        wasm_runtime_free(param_types);
    if (param_values)
        wasm_runtime_free(param_values);
    return ret;
}

static bool
compile_call_indirect_for_nosandbox(AOTCompContext *comp_ctx,
                                    AOTFuncContext *func_ctx, uint32 type_idx,
                                    uint32 tbl_idx, WASMRelocation *relocation)
{
    AOTFuncType *func_type;
    LLVMValueRef elem_idx;
    LLVMValueRef func;
    LLVMValueRef ext_ret_offset, ext_ret_ptr, ext_ret;
    LLVMValueRef *param_values = NULL, *value_rets = NULL;
    LLVMValueRef value_ret;
    LLVMTypeRef *param_types = NULL, ret_type;
    LLVMTypeRef llvm_func_type, llvm_func_ptr_type;
    LLVMTypeRef ext_ret_ptr_type, array_type;
    LLVMValueRef argv_buf = NULL;
    uint32 total_param_count, func_param_count, func_result_count;
    uint32 ext_cell_num, i, j;
    uint8 wasm_ret_type;
    uint64 total_size;
    char buf[32];
    bool ret = false, is_table64;

    func_type = comp_ctx->comp_data->func_types[type_idx];
    func_param_count = func_type->param_count;
    func_result_count = func_type->result_count;

    is_table64 = comp_ctx->comp_data->tables[tbl_idx].table_flags & TABLE64_FLAG
                     ? true
                     : false;

    if (is_table64)
        POP_I64(elem_idx);
    else
        POP_I32(elem_idx);

    /* Initialize parameter types of the LLVM function */
    total_param_count = func_param_count;

    /* Extra function results' addresses (except the first one) are
       appended to aot function parameters. */
    if (func_result_count > 1)
        total_param_count += func_result_count - 1;

    total_size = sizeof(LLVMTypeRef) * (uint64)total_param_count;
    if (total_size > 0) {
        if (total_size >= UINT32_MAX
            || !(param_types = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed.");
            goto fail;
        }
    }

    /* Prepare param types */
    j = 0;
    for (i = 0; i < func_param_count; i++)
        param_types[j++] = TO_LLVM_TYPE(func_type->types[i]);

    for (i = 1; i < func_result_count; i++, j++) {
        param_types[j] = TO_LLVM_TYPE(func_type->types[func_param_count + i]);
        if (!(param_types[j] = LLVMPointerType(param_types[j], 0))) {
            aot_set_last_error("llvm get pointer type failed.");
            goto fail;
        }
    }

    /* Resolve return type of the LLVM function */
    if (func_result_count) {
        wasm_ret_type = func_type->types[func_param_count];
        ret_type = TO_LLVM_TYPE(wasm_ret_type);
    }
    else {
        wasm_ret_type = VALUE_TYPE_VOID;
        ret_type = VOID_TYPE;
    }

    /* Allocate memory for parameters */
    total_size = sizeof(LLVMValueRef) * (uint64)total_param_count;
    if (total_size > 0) {
        if (total_size >= UINT32_MAX
            || !(param_values = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed.");
            goto fail;
        }
    }

    /* Pop parameters from stack */
    for (i = func_param_count - 1; (int32)i >= 0; i--)
        POP(param_values[i], func_type->types[i]);

    ext_cell_num = 0;
    for (i = 1; i < func_result_count; i++) {
        ext_cell_num +=
            wasm_value_type_cell_num(func_type->types[func_param_count + i]);
    }

    if (ext_cell_num > 0) {
        if (!(array_type = LLVMArrayType(I32_TYPE, ext_cell_num))) {
            aot_set_last_error("create llvm array type failed");
            goto fail;
        }
        if (!(argv_buf =
                  LLVMBuildAlloca(comp_ctx->builder, array_type, "argv_buf"))) {
            aot_set_last_error("llvm build alloca failed.");
            goto fail;
        }
    }

    /* Prepare extra parameters */
    ext_cell_num = 0;
    for (i = 1; i < func_result_count; i++) {
        ext_ret_offset = I32_CONST(ext_cell_num);
        CHECK_LLVM_CONST(ext_ret_offset);

        snprintf(buf, sizeof(buf), "ext_ret%d_ptr", i - 1);
        if (!(ext_ret_ptr =
                  LLVMBuildInBoundsGEP2(comp_ctx->builder, I32_TYPE, argv_buf,
                                        &ext_ret_offset, 1, buf))) {
            aot_set_last_error("llvm build GEP failed.");
            goto fail;
        }

        ext_ret_ptr_type = param_types[func_param_count + i];
        snprintf(buf, sizeof(buf), "ext_ret%d_ptr_cast", i - 1);
        if (!(ext_ret_ptr = LLVMBuildBitCast(comp_ctx->builder, ext_ret_ptr,
                                             ext_ret_ptr_type, buf))) {
            aot_set_last_error("llvm build bit cast failed.");
            goto fail;
        }

        param_values[func_param_count + i] = ext_ret_ptr;
        ext_cell_num +=
            wasm_value_type_cell_num(func_type->types[func_param_count + i]);
    }

    if (!(llvm_func_type =
              LLVMFunctionType(ret_type, param_types, total_param_count, false))
        || !(llvm_func_ptr_type = LLVMPointerType(llvm_func_type, 0))) {
        aot_set_last_error("llvm add function type failed.");
        goto fail;
    }

    if (!(func = LLVMBuildIntToPtr(comp_ctx->builder, elem_idx,
                                   llvm_func_ptr_type, "indirect_func"))) {
        aot_set_last_error("llvm build bit cast failed.");
        goto fail;
    }

    if (!(value_ret = LLVMBuildCall2(comp_ctx->builder, llvm_func_type, func,
                                     param_values, total_param_count,
                                     func_result_count > 0 ? "ret" : ""))) {
        aot_set_last_error("llvm build call failed.");
        goto fail;
    }

    if (func_result_count > 0) {
        /* Push the first result to stack */
        PUSH(value_ret, func_type->types[func_param_count]);

        /* Load extra result from its address and push to stack */
        for (i = 1; i < func_result_count; i++) {
            ret_type = TO_LLVM_TYPE(func_type->types[func_param_count + i]);
            snprintf(buf, sizeof(buf), "ext_ret%d", i - 1);
            if (!(ext_ret = LLVMBuildLoad2(comp_ctx->builder, ret_type,
                                           param_values[func_param_count + i],
                                           buf))) {
                aot_set_last_error("llvm build load failed.");
                goto fail;
            }
            PUSH(ext_ret, func_type->types[func_param_count + i]);
        }
    }

    if (!check_exception_thrown(comp_ctx, func_ctx)) {
        goto fail;
    }

    ret = true;

fail:
    if (param_values)
        wasm_runtime_free(param_values);
    if (param_types)
        wasm_runtime_free(param_types);
    if (value_rets)
        wasm_runtime_free(value_rets);
    return ret;
}

bool
aot_compile_op_call_indirect(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                             uint32 type_idx, uint32 tbl_idx,
                             WASMRelocation *relocation)
{
    AOTFuncType *func_type;
    LLVMValueRef elem_idx, table_elem, func_idx;
    LLVMValueRef func, func_ptr;
    LLVMValueRef ext_ret_offset, ext_ret_ptr, ext_ret;
    LLVMValueRef *param_values = NULL, *value_rets = NULL;
    LLVMValueRef value_ret;
    LLVMTypeRef *param_types = NULL, ret_type;
    LLVMTypeRef llvm_func_type, llvm_func_ptr_type;
    LLVMTypeRef ext_ret_ptr_type, array_type;
    LLVMValueRef argv_buf = NULL;
    uint32 total_param_count, func_param_count, func_result_count;
    uint32 ext_cell_num, i, j;
    uint8 wasm_ret_type;
    uint64 total_size;
    char buf[32];
    bool ret = false, is_table64;

    /* Check function type index */
    if (type_idx >= comp_ctx->comp_data->func_type_count) {
        aot_set_last_error("function type index out of range");
        return false;
    }

    /* Find the equivalent function type whose type index is the smallest:
       the callee function's type index is also converted to the smallest
       one in wasm loader, so we can just check whether the two type indexes
       are equal (the type index of call_indirect opcode and callee func),
       we don't need to check whether the whole function types are equal,
       including param types and result types. */
    type_idx = wasm_get_smallest_type_idx(comp_ctx->comp_data->func_types,
                                          comp_ctx->comp_data->func_type_count,
                                          type_idx);

    if (comp_ctx->no_sandbox_mode)
        return compile_call_indirect_for_nosandbox(comp_ctx, func_ctx, type_idx,
                                                   tbl_idx, relocation);

    func_type = comp_ctx->comp_data->func_types[type_idx];
    func_param_count = func_type->param_count;
    func_result_count = func_type->result_count;

    is_table64 = comp_ctx->comp_data->tables[tbl_idx].table_flags & TABLE64_FLAG
                     ? true
                     : false;

    if (is_table64)
        POP_I64(elem_idx);
    else
        POP_I32(elem_idx);

    LLVMBasicBlockRef check_elem_idx_succ;
    LLVMValueRef table_size_const, cmp_elem_idx;
    AOTTable *aot_table = &comp_ctx->comp_data->tables[0];

    if (is_table64)
        table_size_const = I64_CONST(aot_table->table_init_size);
    else
        table_size_const = I32_CONST(aot_table->table_init_size);
    CHECK_LLVM_CONST(table_size_const);

    /* Check if (uint32)elem index >= table size */
    if (!(cmp_elem_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntUGE, elem_idx,
                                       table_size_const, "cmp_elem_idx"))) {
        aot_set_last_error("llvm build icmp failed.");
        goto fail;
    }

    /* Throw exception if elem index >= table size */
    if (!(check_elem_idx_succ = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_elem_idx_succ"))) {
        aot_set_last_error("llvm add basic block failed.");
        goto fail;
    }

    LLVMMoveBasicBlockAfter(check_elem_idx_succ,
                            LLVMGetInsertBlock(comp_ctx->builder));

    if (!(aot_emit_exception(comp_ctx, func_ctx, EXCE_UNDEFINED_ELEMENT, true,
                             cmp_elem_idx, check_elem_idx_succ))) {
        goto fail;
    }

    LLVMValueRef table_elems_global =
        LLVMGetNamedGlobal(comp_ctx->module, "table_elems");
    bh_assert(table_elems_global);

    if (!(table_elem = LLVMBuildInBoundsGEP2(comp_ctx->builder, I32_TYPE,
                                             table_elems_global, &elem_idx, 1,
                                             "table_elem_addr"))) {
        aot_set_last_error("llvm build add failed.");
        goto fail;
    }

    if (!(func_idx = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, table_elem,
                                    "func_idx"))) {
        aot_set_last_error("llvm build load failed.");
        goto fail;
    }

    LLVMBasicBlockRef check_func_idx_succ;
    LLVMValueRef cmp_func_idx;

    /* Check if func_idx == -1 */
    if (!(cmp_func_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, func_idx,
                                       I32_NEG_ONE, "cmp_func_idx"))) {
        aot_set_last_error("llvm build icmp failed.");
        goto fail;
    }

    /* Throw exception if func_idx == -1 */
    if (!(check_func_idx_succ = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_func_idx_succ"))) {
        aot_set_last_error("llvm add basic block failed.");
        goto fail;
    }

    LLVMMoveBasicBlockAfter(check_func_idx_succ,
                            LLVMGetInsertBlock(comp_ctx->builder));

    if (!(aot_emit_exception(comp_ctx, func_ctx, EXCE_UNINITIALIZED_ELEMENT,
                             true, cmp_func_idx, check_func_idx_succ)))
        goto fail;

    LLVMBasicBlockRef check_ftype_idx_succ;
    LLVMValueRef func_type_indexes_global, ftype_idx_ptr;
    LLVMValueRef ftype_idx_const, ftype_idx, cmp_ftype_idx;

    func_type_indexes_global =
        LLVMGetNamedGlobal(comp_ctx->module, "func_type_indexes");
    bh_assert(func_type_indexes_global);

    /* Load function type index */
    if (!(ftype_idx_ptr = LLVMBuildInBoundsGEP2(
              comp_ctx->builder, I32_TYPE, func_type_indexes_global, &func_idx,
              1, "ftype_idx_ptr"))) {
        aot_set_last_error("llvm build inbounds gep failed.");
        goto fail;
    }

    if (!(ftype_idx = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, ftype_idx_ptr,
                                     "ftype_idx"))) {
        aot_set_last_error("llvm build load failed.");
        goto fail;
    }

    ftype_idx_const = I32_CONST(type_idx);
    CHECK_LLVM_CONST(ftype_idx_const);

    /* Check if function type index not equal */
    if (!(cmp_ftype_idx = LLVMBuildICmp(comp_ctx->builder, LLVMIntNE, ftype_idx,
                                        ftype_idx_const, "cmp_ftype_idx"))) {
        aot_set_last_error("llvm build icmp failed.");
        goto fail;
    }

    /* Throw exception if ftype_idx != ftype_idx_const */
    if (!(check_ftype_idx_succ = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_ftype_idx_succ"))) {
        aot_set_last_error("llvm add basic block failed.");
        goto fail;
    }

    LLVMMoveBasicBlockAfter(check_ftype_idx_succ,
                            LLVMGetInsertBlock(comp_ctx->builder));

    if (!(aot_emit_exception(comp_ctx, func_ctx,
                             EXCE_INVALID_FUNCTION_TYPE_INDEX, true,
                             cmp_ftype_idx, check_ftype_idx_succ)))
        goto fail;

    /* Initialize parameter types of the LLVM function */
    total_param_count = func_param_count;

    /* Extra function results' addresses (except the first one) are
       appended to aot function parameters. */
    if (func_result_count > 1)
        total_param_count += func_result_count - 1;

    total_size = sizeof(LLVMTypeRef) * (uint64)total_param_count;
    if (total_size > 0) {
        if (total_size >= UINT32_MAX
            || !(param_types = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed.");
            goto fail;
        }
    }

    /* Prepare param types */
    j = 0;
    for (i = 0; i < func_param_count; i++)
        param_types[j++] = TO_LLVM_TYPE(func_type->types[i]);

    for (i = 1; i < func_result_count; i++, j++) {
        param_types[j] = TO_LLVM_TYPE(func_type->types[func_param_count + i]);
        if (!(param_types[j] = LLVMPointerType(param_types[j], 0))) {
            aot_set_last_error("llvm get pointer type failed.");
            goto fail;
        }
    }

    /* Resolve return type of the LLVM function */
    if (func_result_count) {
        wasm_ret_type = func_type->types[func_param_count];
        ret_type = TO_LLVM_TYPE(wasm_ret_type);
    }
    else {
        wasm_ret_type = VALUE_TYPE_VOID;
        ret_type = VOID_TYPE;
    }

    /* Allocate memory for parameters */
    total_size = sizeof(LLVMValueRef) * (uint64)total_param_count;
    if (total_size > 0) {
        if (total_size >= UINT32_MAX
            || !(param_values = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed.");
            goto fail;
        }
    }

    /* Pop parameters from stack */
    for (i = func_param_count - 1; (int32)i >= 0; i--)
        POP(param_values[i], func_type->types[i]);

    ext_cell_num = 0;
    for (i = 1; i < func_result_count; i++) {
        ext_cell_num +=
            wasm_value_type_cell_num(func_type->types[func_param_count + i]);
    }

    if (ext_cell_num > 0) {
        if (!(array_type = LLVMArrayType(I32_TYPE, ext_cell_num))) {
            aot_set_last_error("create llvm array type failed");
            goto fail;
        }
        if (!(argv_buf =
                  LLVMBuildAlloca(comp_ctx->builder, array_type, "argv_buf"))) {
            aot_set_last_error("llvm build alloca failed.");
            goto fail;
        }
    }

    /* Prepare extra parameters */
    ext_cell_num = 0;
    for (i = 1; i < func_result_count; i++) {
        ext_ret_offset = I32_CONST(ext_cell_num);
        CHECK_LLVM_CONST(ext_ret_offset);

        snprintf(buf, sizeof(buf), "ext_ret%d_ptr", i - 1);
        if (!(ext_ret_ptr =
                  LLVMBuildInBoundsGEP2(comp_ctx->builder, I32_TYPE, argv_buf,
                                        &ext_ret_offset, 1, buf))) {
            aot_set_last_error("llvm build GEP failed.");
            goto fail;
        }

        ext_ret_ptr_type = param_types[func_param_count + i];
        snprintf(buf, sizeof(buf), "ext_ret%d_ptr_cast", i - 1);
        if (!(ext_ret_ptr = LLVMBuildBitCast(comp_ctx->builder, ext_ret_ptr,
                                             ext_ret_ptr_type, buf))) {
            aot_set_last_error("llvm build bit cast failed.");
            goto fail;
        }

        param_values[func_param_count + i] = ext_ret_ptr;
        ext_cell_num +=
            wasm_value_type_cell_num(func_type->types[func_param_count + i]);
    }

    LLVMValueRef func_ptrs = LLVMGetNamedGlobal(comp_ctx->module, "func_ptrs");
    bh_assert(func_ptrs);

    /* Load function pointer */
    if (!(func_ptr =
              LLVMBuildInBoundsGEP2(comp_ctx->builder, OPQ_PTR_TYPE, func_ptrs,
                                    &func_idx, 1, "func_ptr_tmp"))) {
        aot_set_last_error("llvm build inbounds gep failed.");
        goto fail;
    }

    if (!(func_ptr = LLVMBuildLoad2(comp_ctx->builder, OPQ_PTR_TYPE, func_ptr,
                                    "func_ptr"))) {
        aot_set_last_error("llvm build load failed.");
        goto fail;
    }

    if (!(llvm_func_type =
              LLVMFunctionType(ret_type, param_types, total_param_count, false))
        || !(llvm_func_ptr_type = LLVMPointerType(llvm_func_type, 0))) {
        aot_set_last_error("llvm add function type failed.");
        goto fail;
    }

    if (!(func = LLVMBuildBitCast(comp_ctx->builder, func_ptr,
                                  llvm_func_ptr_type, "indirect_func"))) {
        aot_set_last_error("llvm build bit cast failed.");
        goto fail;
    }

    LLVMBasicBlockRef check_func_ptr_succ_block;
    LLVMValueRef cmp;

    if (!(check_func_ptr_succ_block = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "check_func_ptr_succ"))) {
        aot_set_last_error("llvm add basic block failed.");
        goto fail;
    }
    LLVMMoveBasicBlockAfter(check_func_ptr_succ_block,
                            LLVMGetInsertBlock(comp_ctx->builder));

    if (!(cmp = LLVMBuildIsNull(comp_ctx->builder, func, "is_null"))) {
        aot_set_last_error("llvm build is null failed.");
        goto fail;
    }

    if (!aot_emit_exception(comp_ctx, func_ctx, EXCE_CALL_UNLINKED_IMPORT_FUNC,
                            true, cmp, check_func_ptr_succ_block)) {
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_func_ptr_succ_block);

    if (!(value_ret = LLVMBuildCall2(comp_ctx->builder, llvm_func_type, func,
                                     param_values, total_param_count,
                                     func_result_count > 0 ? "ret" : ""))) {
        aot_set_last_error("llvm build call failed.");
        goto fail;
    }

    if (func_result_count > 0) {
        /* Push the first result to stack */
        PUSH(value_ret, func_type->types[func_param_count]);

        /* Load extra result from its address and push to stack */
        for (i = 1; i < func_result_count; i++) {
            ret_type = TO_LLVM_TYPE(func_type->types[func_param_count + i]);
            snprintf(buf, sizeof(buf), "ext_ret%d", i - 1);
            if (!(ext_ret = LLVMBuildLoad2(comp_ctx->builder, ret_type,
                                           param_values[func_param_count + i],
                                           buf))) {
                aot_set_last_error("llvm build load failed.");
                goto fail;
            }
            PUSH(ext_ret, func_type->types[func_param_count + i]);
        }
    }

    if (!check_exception_thrown(comp_ctx, func_ctx)) {
        goto fail;
    }

    ret = true;

fail:
    if (param_values)
        wasm_runtime_free(param_values);
    if (param_types)
        wasm_runtime_free(param_types);
    if (value_rets)
        wasm_runtime_free(value_rets);
    return ret;
}

bool
aot_compile_op_ref_null(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    PUSH_I32(REF_NULL);

    return true;
fail:
    return false;
}

bool
aot_compile_op_ref_is_null(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    LLVMValueRef lhs, res;

    POP_I32(lhs);

    if (!(res = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, lhs, REF_NULL,
                              "cmp_w_null"))) {
        HANDLE_FAILURE("LLVMBuildICmp");
        goto fail;
    }

    if (!(res = LLVMBuildZExt(comp_ctx->builder, res, I32_TYPE, "r_i"))) {
        HANDLE_FAILURE("LLVMBuildZExt");
        goto fail;
    }

    PUSH_I32(res);

    return true;
fail:
    return false;
}

bool
aot_compile_op_ref_func(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                        uint32 func_idx)
{
    LLVMValueRef ref_idx;

    if (!(ref_idx = I32_CONST(func_idx))) {
        HANDLE_FAILURE("LLVMConstInt");
        goto fail;
    }

    PUSH_I32(ref_idx);

    return true;
fail:
    return false;
}
