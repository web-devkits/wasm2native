/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_emit_variable.h"
#include "aot_emit_exception.h"

#define CHECK_LOCAL(idx)                                      \
    do {                                                      \
        if (idx >= func_ctx->aot_func->func_type->param_count \
                       + func_ctx->aot_func->local_count) {   \
            aot_set_last_error("local index out of range");   \
            return false;                                     \
        }                                                     \
    } while (0)

static uint8
get_local_type(AOTFuncContext *func_ctx, uint32 local_idx)
{
    AOTFunc *aot_func = func_ctx->aot_func;
    uint32 param_count = aot_func->func_type->param_count;
    return local_idx < param_count
               ? aot_func->func_type->types[local_idx]
               : aot_func->local_types[local_idx - param_count];
}

bool
aot_compile_op_get_local(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 local_idx)
{
    char name[32];
    LLVMValueRef value;
    AOTValue *aot_value_top;
    uint8 local_type;

    CHECK_LOCAL(local_idx);

    local_type = get_local_type(func_ctx, local_idx);

    snprintf(name, sizeof(name), "%s%d%s", "local", local_idx, "#");
    if (!(value = LLVMBuildLoad2(comp_ctx->builder, TO_LLVM_TYPE(local_type),
                                 func_ctx->locals[local_idx], name))) {
        aot_set_last_error("llvm build load fail");
        return false;
    }

    PUSH(value, local_type);

    aot_value_top =
        func_ctx->block_stack.block_list_end->value_stack.value_list_end;
    aot_value_top->is_local = true;
    aot_value_top->local_idx = local_idx;
    return true;

fail:
    return false;
}

bool
aot_compile_op_set_local(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 local_idx)
{
    LLVMValueRef value;

    CHECK_LOCAL(local_idx);

    POP(value, get_local_type(func_ctx, local_idx));

    if (!LLVMBuildStore(comp_ctx->builder, value,
                        func_ctx->locals[local_idx])) {
        aot_set_last_error("llvm build store fail");
        return false;
    }

    return true;

fail:
    return false;
}

bool
aot_compile_op_tee_local(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 local_idx)
{
    LLVMValueRef value;
    uint8 type;

    CHECK_LOCAL(local_idx);

    type = get_local_type(func_ctx, local_idx);

    POP(value, type);

    if (!LLVMBuildStore(comp_ctx->builder, value,
                        func_ctx->locals[local_idx])) {
        aot_set_last_error("llvm build store fail");
        return false;
    }

    PUSH(value, type);
    return true;

fail:
    return false;
}

static bool
compile_global(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
               uint32 global_idx, bool is_set, bool is_aux_stack,
               WASMRelocation *relocation)
{
    const AOTCompData *comp_data = comp_ctx->comp_data;
    uint32 import_global_count = comp_data->import_global_count;
    uint8 global_type;
    LLVMValueRef global_ptr, global, res, mem_data, mem_addr;
    char buf[48];

    if (!(mem_data = aot_get_memory_base_addr(comp_ctx, func_ctx)))
        return false;

    bh_assert(global_idx < import_global_count + comp_data->global_count);

    if (global_idx < import_global_count) {
        global_type = comp_data->import_globals[global_idx].type;
        snprintf(buf, sizeof(buf), "%s%u", "wasm_import_global#", global_idx);
    }
    else {
        global_type = comp_data->globals[global_idx - import_global_count].type;
        snprintf(buf, sizeof(buf), "%s%u", "wasm_global#",
                 global_idx - import_global_count);
    }

    global_ptr = LLVMGetNamedGlobal(comp_ctx->module, buf);
    bh_assert(global_ptr);

    if (!is_set) {
        if (!(global =
                  LLVMBuildLoad2(comp_ctx->builder, TO_LLVM_TYPE(global_type),
                                 global_ptr, "global"))) {
            aot_set_last_error("llvm build load failed.");
            return false;
        }

        if (comp_ctx->no_sandbox_mode && relocation) {
            bh_assert(global_type == VALUE_TYPE_I64
                      || global_type == VALUE_TYPE_I32);

            if (!(mem_addr = LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE,
                                                   mem_data, &global, 1,
                                                   "mem_addr"))) {
                aot_set_last_error("llvm build in bounds gep failed.");
                return false;
            }
            if (!(global = LLVMBuildPtrToInt(comp_ctx->builder, mem_addr,
                                             TO_LLVM_TYPE(global_type),
                                             "stack_pointer"))) {
                aot_set_last_error("llvm build ptr to int failed.");
                return false;
            }
        }

        PUSH(global, global_type);
    }
    else {
        POP(global, global_type);

        if (comp_ctx->no_sandbox_mode && relocation) {
            bh_assert(global_type == VALUE_TYPE_I64
                      || global_type == VALUE_TYPE_I32);

            if (!(mem_addr = LLVMBuildPtrToInt(comp_ctx->builder, mem_data,
                                               I64_TYPE, "mem_addr_i64"))) {
                aot_set_last_error("llvm build ptr to int failed.");
                return false;
            }
            if (!(global = LLVMBuildSub(comp_ctx->builder, global, mem_addr,
                                        "stack_pointer"))) {
                aot_set_last_error("llvm build sub failed.");
                return false;
            }
        }

        if (!(res = LLVMBuildStore(comp_ctx->builder, global, global_ptr))) {
            aot_set_last_error("llvm build store failed.");
            return false;
        }
    }

    return true;
fail:
    return false;
}

bool
aot_compile_op_get_global(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                          uint32 global_idx, WASMRelocation *relocation)
{
    return compile_global(comp_ctx, func_ctx, global_idx, false, false,
                          relocation);
}

bool
aot_compile_op_set_global(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                          uint32 global_idx, bool is_aux_stack,
                          WASMRelocation *relocation)
{
    return compile_global(comp_ctx, func_ctx, global_idx, true, is_aux_stack,
                          relocation);
}
