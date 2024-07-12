/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_emit_const.h"

bool
aot_compile_op_i32_const(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         int32 i32_const)
{
    LLVMValueRef value;

    value = I32_CONST((uint32)i32_const);
    CHECK_LLVM_CONST(value);

    PUSH_I32(value);
    return true;
fail:
    return false;
}

bool
aot_compile_op_i64_const(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         int64 i64_const, WASMRelocation *relocation)
{
    LLVMValueRef value, mem_data, mem_addr;
    WASMSymbol *symbol;
    WASMSymbolData *sym_data;

    value = I64_CONST((uint64)i64_const);
    CHECK_LLVM_CONST(value);

    if (comp_ctx->no_sandbox_mode && relocation) {
        if (relocation->type == R_WASM_MEMORY_ADDR_SLEB64) {
            bh_assert(relocation->index < comp_ctx->comp_data->symbol_count);

            symbol = &comp_ctx->comp_data->symbols[relocation->index];
            bh_assert(symbol->type == WASM_SYMBOL_TYPE_DATA);
            sym_data = &symbol->u.sym_data;
            LOG_VERBOSE(
                "relocation of i64.const, base offset: %lu, data_offset: "
                "%lu, i64 const: %lu\n",
                comp_ctx->comp_data->data_segments[sym_data->seg_index]
                    ->base_offset.u.u64,
                sym_data->data_offset, i64_const);

            if (!(mem_data = aot_get_memory_base_addr(comp_ctx, func_ctx)))
                return false;

            if (!(mem_addr =
                      LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE,
                                            mem_data, &value, 1, "mem_addr"))) {
                aot_set_last_error("llvm build inbound gep failed");
                return false;
            }

            if (!(value = LLVMBuildPtrToInt(comp_ctx->builder, mem_addr,
                                            I64_TYPE, "mem_addr_i64"))) {
                aot_set_last_error("llvm build ptr to int failed");
                return false;
            }
        }
        else if (relocation->type == R_WASM_TABLE_INDEX_SLEB64) {
            bh_assert(comp_ctx->comp_data->symbols[relocation->index].type
                      == WASM_SYMBOL_TYPE_FUNCTION);
            WASMSymbol *symbol =
                &comp_ctx->comp_data->symbols[relocation->index];
            LLVMValueRef func_ptr;

            bh_assert(symbol->type == WASM_SYMBOL_TYPE_FUNCTION);

            if (!symbol->is_defined) {
                func_ptr = comp_ctx->import_func_ptrs[symbol->u.import->u
                                                          .function.func_idx];
            }
            else {
                func_ptr = comp_ctx
                               ->func_ctxes[symbol->u.function->func_idx
                                            - comp_ctx->import_func_count]
                               ->func;
            }

            if (!(value = LLVMBuildPtrToInt(comp_ctx->builder, func_ptr,
                                            I64_TYPE, "func_ptr"))) {
                aot_set_last_error("llvm build ptr to int failed");
                return false;
            }
        }
        else {
            aot_set_last_error_v("unsupported relocation type %u for i64.const",
                                 relocation->type);
            return false;
        }
    }

    PUSH_I64(value);
    return true;
fail:
    return false;
}

bool
aot_compile_op_f32_const(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         float32 f32_const)
{
    LLVMValueRef alloca, value;

    if (!isnan(f32_const)) {
        value = F32_CONST(f32_const);
        CHECK_LLVM_CONST(value);
        PUSH_F32(value);
    }
    else {
        int32 i32_const;
        memcpy(&i32_const, &f32_const, sizeof(int32));
        if (!(alloca =
                  LLVMBuildAlloca(comp_ctx->builder, I32_TYPE, "i32_ptr"))) {
            aot_set_last_error("llvm build alloca failed.");
            return false;
        }
        if (!LLVMBuildStore(comp_ctx->builder, I32_CONST((uint32)i32_const),
                            alloca)) {
            aot_set_last_error("llvm build store failed.");
            return false;
        }
        if (!(alloca = LLVMBuildBitCast(comp_ctx->builder, alloca, F32_PTR_TYPE,
                                        "f32_ptr"))) {
            aot_set_last_error("llvm build bitcast failed.");
            return false;
        }
        if (!(value =
                  LLVMBuildLoad2(comp_ctx->builder, F32_TYPE, alloca, ""))) {
            aot_set_last_error("llvm build load failed.");
            return false;
        }
        PUSH_F32(value);
    }

    return true;
fail:
    return false;
}

bool
aot_compile_op_f64_const(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         float64 f64_const)
{
    LLVMValueRef alloca, value;

    if (!isnan(f64_const)) {
        value = F64_CONST(f64_const);
        CHECK_LLVM_CONST(value);
        PUSH_F64(value);
    }
    else {
        int64 i64_const;
        memcpy(&i64_const, &f64_const, sizeof(int64));
        if (!(alloca =
                  LLVMBuildAlloca(comp_ctx->builder, I64_TYPE, "i64_ptr"))) {
            aot_set_last_error("llvm build alloca failed.");
            return false;
        }
        value = I64_CONST((uint64)i64_const);
        CHECK_LLVM_CONST(value);
        if (!LLVMBuildStore(comp_ctx->builder, value, alloca)) {
            aot_set_last_error("llvm build store failed.");
            return false;
        }
        if (!(alloca = LLVMBuildBitCast(comp_ctx->builder, alloca, F64_PTR_TYPE,
                                        "f64_ptr"))) {
            aot_set_last_error("llvm build bitcast failed.");
            return false;
        }
        if (!(value =
                  LLVMBuildLoad2(comp_ctx->builder, F64_TYPE, alloca, ""))) {
            aot_set_last_error("llvm build load failed.");
            return false;
        }
        PUSH_F64(value);
    }

    return true;
fail:
    return false;
}
