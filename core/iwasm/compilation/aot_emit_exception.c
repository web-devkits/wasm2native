/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_emit_exception.h"
#include "../common/wasm_runtime.h"

bool
aot_emit_exception(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                   int32 exception_id, bool is_cond_br, LLVMValueRef cond_br_if,
                   LLVMBasicBlockRef cond_br_else_block)
{
    LLVMBasicBlockRef block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    LLVMValueRef exce_id;

    if (comp_ctx->no_sandbox_mode) {
        /* Create return IR */
        AOTFuncType *aot_func_type = func_ctx->aot_func->func_type;

        bh_assert(!is_cond_br);

        if (!aot_build_zero_function_ret(comp_ctx, func_ctx, aot_func_type)) {
            return false;
        }
        return true;
    }

    bh_assert(exception_id >= EXCE_ID_MIN && exception_id <= EXCE_ID_MAX);

    exce_id = I32_CONST((uint32)exception_id);
    CHECK_LLVM_CONST(exce_id);

    /* Create got_exception block if needed */
    if (!func_ctx->got_exception_block) {
        if (!(func_ctx->got_exception_block = LLVMAppendBasicBlockInContext(
                  comp_ctx->context, func_ctx->func, "got_exception"))) {
            aot_set_last_error("add LLVM basic block failed.");
            return false;
        }

        LLVMPositionBuilderAtEnd(comp_ctx->builder,
                                 func_ctx->got_exception_block);

        /* Create exection id phi */
        if (!(func_ctx->exception_id_phi = LLVMBuildPhi(
                  comp_ctx->builder, I32_TYPE, "exception_id_phi"))) {
            aot_set_last_error("llvm build phi failed.");
            return false;
        }

        LLVMValueRef exce_id_global;

        exce_id_global = LLVMGetNamedGlobal(comp_ctx->module, "exception_id");
        bh_assert(exce_id_global);
        if (!LLVMBuildStore(comp_ctx->builder, func_ctx->exception_id_phi,
                            exce_id_global)) {
            aot_set_last_error("llvm build store failed.");
            return false;
        }

        /* Create return IR */
        AOTFuncType *aot_func_type = func_ctx->aot_func->func_type;
        if (!aot_build_zero_function_ret(comp_ctx, func_ctx, aot_func_type)) {
            return false;
        }

        /* Resume the builder position */
        LLVMPositionBuilderAtEnd(comp_ctx->builder, block_curr);
    }

    /* Add phi incoming value to got_exception block */
    LLVMAddIncoming(func_ctx->exception_id_phi, &exce_id, &block_curr, 1);

    if (!is_cond_br) {
        /* not condition br, create br IR */
        if (!LLVMBuildBr(comp_ctx->builder, func_ctx->got_exception_block)) {
            aot_set_last_error("llvm build br failed.");
            return false;
        }
    }
    else {
        /* Create condition br */
        if (!LLVMBuildCondBr(comp_ctx->builder, cond_br_if,
                             func_ctx->got_exception_block,
                             cond_br_else_block)) {
            aot_set_last_error("llvm build cond br failed.");
            return false;
        }
        /* Start to translate the else block */
        LLVMPositionBuilderAtEnd(comp_ctx->builder, cond_br_else_block);
    }

    return true;
fail:
    return false;
}
