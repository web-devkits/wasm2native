/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_llvm.h"
#include "aot_llvm_extra2.h"
#include "aot_compiler.h"
#include "aot_emit_exception.h"

LLVMTypeRef
wasm_type_to_llvm_type(const AOTLLVMTypes *llvm_types, uint8 wasm_type)
{
    switch (wasm_type) {
        case VALUE_TYPE_I32:
            return llvm_types->int32_type;
        case VALUE_TYPE_I64:
            return llvm_types->int64_type;
        case VALUE_TYPE_F32:
            return llvm_types->float32_type;
        case VALUE_TYPE_F64:
            return llvm_types->float64_type;
        case VALUE_TYPE_V128:
            return llvm_types->i64x2_vec_type;
        case VALUE_TYPE_VOID:
            return llvm_types->void_type;
        default:
            break;
    }
    return NULL;
}

static LLVMValueRef
create_wasm_global(AOTCompContext *comp_ctx, LLVMTypeRef type, const char *name,
                   LLVMValueRef initializer, bool is_const)
{
    LLVMValueRef global = LLVMAddGlobal(comp_ctx->module, type, name);

    if (!global) {
        aot_set_last_error("add LLVM global failed");
        return NULL;
    }

    LLVMSetSection(global, ".wasm_globals");
    LLVMSetLinkage(global, LLVMInternalLinkage);
    LLVMSetGlobalConstant(global, is_const);
    LLVMSetInitializer(global, initializer);

    return global;
}

/* clang-format off */
static const char *exception_msgs[] = {
    "unreachable",                   /* EXCE_UNREACHABLE */
    "out of bounds memory access",   /* EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS */
    "out of bounds table access",    /* EXCE_OUT_OF_BOUNDS_TABLE_ACCESS */
    "integer overflow",              /* EXCE_INTEGER_OVERFLOW */
    "integer divide by zero",        /* EXCE_INTEGER_DIVIDE_BY_ZERO */
    "invalid conversion to integer", /* EXCE_INVALID_CONVERSION_TO_INTEGER */
    "indirect call type mismatch",   /* EXCE_INVALID_FUNCTION_TYPE_INDEX */
    "undefined element",             /* EXCE_UNDEFINED_ELEMENT */
    "uninitialized element",         /* EXCE_UNINITIALIZED_ELEMENT */
    "failed to call unlinked import function", /* EXCE_CALL_UNLINKED_IMPORT_FUNC */
    "native stack overflow",         /* EXCE_NATIVE_STACK_OVERFLOW */
    "unaligned atomic",              /* EXCE_UNALIGNED_ATOMIC */
    "wasm auxiliary stack overflow", /* EXCE_AUX_STACK_OVERFLOW */
    "wasm auxiliary stack underflow",/* EXCE_AUX_STACK_UNDERFLOW */
    "allocate memory failed",        /* EXCE_ALLOCATE_MEMORY_FAILED */
    "lookup entry symbol failed",    /* EXCE_LOOKUP_ENTRY_SYMBOL_FAILED */
    "lookup function failed",        /* EXCE_LOOKUP_FUNCTION_FAILED */
    "invalid input argument count",  /* EXCE_INVALID_INPUT_ARGUMENT_COUNT */
    "invalid input argument",        /* EXCE_INVALID_INPUT_ARGUMENT */
    "host managed heap not found",   /* EXCE_HOST_MANAGED_HEAP_NOT_FOUND */
    "quick call entry not found",    /* EXCE_QUICK_CALL_ENTRY_NOT_FOUND */
    "unknown error",                 /* EXCE_UNKNOWN_ERROR */
};
/* clang-format on */

#define MEMORY_DATA_SIZE_FIXED(aot_memory)                             \
    (aot_memory->mem_init_page_count == aot_memory->mem_max_page_count \
         ? true                                                        \
         : false)

static bool
create_wasm_globals(const AOTCompData *comp_data, AOTCompContext *comp_ctx)
{
    AOTMemory *aot_memory = &comp_data->memories[0];
    LLVMValueRef initializer, *values = NULL, int8_null_ptr;
    LLVMTypeRef global_type;
    uint64 memory_data_size = (uint64)aot_memory->num_bytes_per_page
                              * aot_memory->mem_init_page_count;
    bool memory_data_size_fixed = MEMORY_DATA_SIZE_FIXED(aot_memory);
    uint64 total_size;
    uint32 i, j, k;
    char buf[32];

    int8_null_ptr = LLVMConstPointerNull(INT8_PTR_TYPE);
    CHECK_LLVM_CONST(int8_null_ptr);

    /* Create memory_data global */
    initializer = int8_null_ptr;
    if (!create_wasm_global(comp_ctx, INT8_PTR_TYPE, "memory_data", initializer,
                            false)) {
        return false;
    }

    /* Create memory_data_size global */
    initializer = I64_CONST(memory_data_size);
    CHECK_LLVM_CONST(initializer);
    if (!create_wasm_global(comp_ctx, I64_TYPE, "memory_data_size", initializer,
                            memory_data_size_fixed)) {
        return false;
    }

    if (!comp_ctx->no_sandbox_mode) {
        /* Create num_bytes_per_page global */
        initializer = I32_CONST(aot_memory->num_bytes_per_page);
        CHECK_LLVM_CONST(initializer);
        if (!create_wasm_global(comp_ctx, I32_TYPE, "num_bytes_per_page",
                                initializer, true)) {
            return false;
        }

        /* Create mem_init_page_count global */
        initializer = I32_CONST(aot_memory->mem_init_page_count);
        CHECK_LLVM_CONST(initializer);
        if (!create_wasm_global(comp_ctx, I32_TYPE, "cur_page_count",
                                initializer, memory_data_size_fixed)) {
            return false;
        }

        /* Create mem_max_page_count global */
        initializer = I32_CONST(aot_memory->mem_max_page_count);
        CHECK_LLVM_CONST(initializer);
        if (!create_wasm_global(comp_ctx, I32_TYPE, "max_page_count",
                                initializer, true)) {
            return false;
        }

        /* Create host_managed_heap_handle global */
        initializer = int8_null_ptr;
        if (!create_wasm_global(comp_ctx, INT8_PTR_TYPE,
                                "host_managed_heap_handle", initializer,
                                false)) {
            return false;
        }

        /* Create mem_bound_check_1byte global */
        initializer = memory_data_size == 0 ? I64_CONST(0)
                                            : I64_CONST(memory_data_size - 1);
        CHECK_LLVM_CONST(initializer);
        if (!create_wasm_global(comp_ctx, I64_TYPE, "mem_bound_check_1byte",
                                initializer, memory_data_size_fixed)) {
            return false;
        }

        /* Create mem_bound_check_2bytes global */
        initializer = memory_data_size == 0 ? I64_CONST(0)
                                            : I64_CONST(memory_data_size - 2);
        CHECK_LLVM_CONST(initializer);
        if (!create_wasm_global(comp_ctx, I64_TYPE, "mem_bound_check_2bytes",
                                initializer, memory_data_size_fixed)) {
            return false;
        }

        /* Create mem_bound_check_4bytes global */
        initializer = memory_data_size == 0 ? I64_CONST(0)
                                            : I64_CONST(memory_data_size - 4);
        CHECK_LLVM_CONST(initializer);
        if (!create_wasm_global(comp_ctx, I64_TYPE, "mem_bound_check_4bytes",
                                initializer, memory_data_size_fixed)) {
            return false;
        }

        /* Create mem_bound_check_8bytes global */
        initializer = memory_data_size == 0 ? I64_CONST(0)
                                            : I64_CONST(memory_data_size - 8);
        CHECK_LLVM_CONST(initializer);
        if (!create_wasm_global(comp_ctx, I64_TYPE, "mem_bound_check_8bytes",
                                initializer, memory_data_size_fixed)) {
            return false;
        }

        /* Create mem_bound_check_16bytes global */
        initializer = memory_data_size == 0 ? I64_CONST(0)
                                            : I64_CONST(memory_data_size - 16);
        CHECK_LLVM_CONST(initializer);
        if (!create_wasm_global(comp_ctx, I64_TYPE, "mem_bound_check_16bytes",
                                initializer, memory_data_size_fixed)) {
            return false;
        }
    }

    if (comp_data->data_seg_count > 0) {
        /* Create data_seg#N globals */
        for (i = 0; i < comp_data->data_seg_count; i++) {
            WASMDataSeg *data_seg = comp_data->data_segments[i];

            /* Check for memory OOB, only for non-passive segment */
            if (!data_seg->is_passive) {
                uint64 data_seg_offset;
                if (data_seg->base_offset.init_expr_type
                    == INIT_EXPR_TYPE_I64_CONST)
                    data_seg_offset = data_seg->base_offset.u.u64;
                else
                    data_seg_offset = data_seg->base_offset.u.u32;

                if (data_seg_offset > memory_data_size) {
                    LOG_DEBUG("base_offset(%d) > memory_data_size(%d)",
                              data_seg_offset, memory_data_size);
                    aot_set_last_error(
                        "out of bounds memory access from data segment");
                    return false;
                }

                if (data_seg->data_length
                    > memory_data_size - data_seg_offset) {
                    LOG_DEBUG(
                        "base_offset(%d) + length(%d)> memory_data_size(%d)",
                        data_seg_offset, data_seg->data_length,
                        memory_data_size);
                    aot_set_last_error(
                        "out of bounds memory access from data segment");
                    return false;
                }
            }

            total_size = (uint64)sizeof(LLVMValueRef) * data_seg->data_length;
            if (total_size > 0
                && !(values = wasm_runtime_malloc((uint32)total_size))) {
                aot_set_last_error("allocate memory failed");
                return false;
            }

            for (j = 0; j < data_seg->data_length; j++) {
                values[j] = I8_CONST(data_seg->data[j]);
                if (!values[j]) {
                    aot_set_last_error("llvm build const failed");
                    wasm_runtime_free(values);
                    return false;
                }
            }

            initializer =
                LLVMConstArray(INT8_TYPE, values, data_seg->data_length);
            if (values) {
                wasm_runtime_free(values);
                values = NULL;
            }
            if (!initializer) {
                aot_set_last_error("llvm build const failed");
                return false;
            }

            snprintf(buf, sizeof(buf), "%s%d", "data_seg#", i);
            if (!(global_type =
                      LLVMArrayType(INT8_TYPE, data_seg->data_length))) {
                aot_set_last_error("create llvm array type failed");
                return false;
            }

            if (!create_wasm_global(comp_ctx, global_type, buf, initializer,
                                    true)) {
                return false;
            }
        }

        /* Create data_segs globals */
        total_size = (uint64)sizeof(LLVMValueRef) * comp_data->data_seg_count;
        if (total_size > 0
            && !(values = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed");
            return false;
        }

        for (i = 0; i < comp_data->data_seg_count; i++) {
            snprintf(buf, sizeof(buf), "%s%d", "data_seg#", i);
            values[i] = LLVMGetNamedGlobal(comp_ctx->module, buf);
            bh_assert(values[i]);
        }

        initializer =
            LLVMConstArray(INT8_PTR_TYPE, values, comp_data->data_seg_count);
        if (values) {
            wasm_runtime_free(values);
            values = NULL;
        }
        if (!initializer) {
            aot_set_last_error("llvm build const failed");
            return false;
        }

        global_type = LLVMArrayType(INT8_PTR_TYPE, comp_data->data_seg_count);
        if (!global_type) {
            aot_set_last_error("create llvm array type failed");
            return false;
        }
        if (!create_wasm_global(comp_ctx, global_type, "data_segs", initializer,
                                true)) {
            return false;
        }

        /* Create data_seg_lengths_passive globals */
        total_size = (uint64)sizeof(LLVMValueRef) * comp_data->data_seg_count;
        if (total_size > 0
            && !(values = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed");
            return false;
        }

        for (i = 0; i < comp_data->data_seg_count; i++) {
            WASMDataSeg *data_seg = comp_data->data_segments[i];

            if (data_seg->is_passive) {
                snprintf(buf, sizeof(buf), "%s%d", "data_seg#", i);
                values[i] = I32_CONST(data_seg->data_length);
                if (!values[i]) {
                    aot_set_last_error("llvm build const failed");
                    wasm_runtime_free(values);
                    return false;
                }
            }
            else {
                values[i] = I32_ZERO;
            }
        }

        initializer =
            LLVMConstArray(I32_TYPE, values, comp_data->data_seg_count);
        if (values) {
            wasm_runtime_free(values);
            values = NULL;
        }
        if (!initializer) {
            aot_set_last_error("llvm build const failed");
            return false;
        }

        global_type = LLVMArrayType(I32_TYPE, comp_data->data_seg_count);
        if (!global_type) {
            aot_set_last_error("create llvm array type failed");
            return false;
        }
        if (!create_wasm_global(comp_ctx, global_type,
                                "data_seg_lengths_passive", initializer,
                                false)) {
            return false;
        }
    }

    /* Create table_elems global */
    if (comp_data->table_count > 0 && !comp_ctx->no_sandbox_mode) {
        AOTTable *table = &comp_data->tables[0];

        total_size = (uint64)sizeof(LLVMValueRef) * table->table_init_size;
        if (total_size > 0
            && !(values = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed");
            return false;
        }

        for (i = 0; i < table->table_init_size; i++) {
            values[i] = I32_NEG_ONE;
        }

        for (j = 0; j < comp_data->table_init_data_count; j++) {
            AOTTableInitData *table_init_data =
                comp_data->table_init_data_list[j];
            uint32 table_idx = table_init_data->table_index, length;
            bool is_table64 = false;

            /* multi-talbe isn't allowed and was already checked in loader */
            if (table_idx < comp_data->import_table_count) {
                aot_set_last_error("import table is not supported");
                return false;
            }

            is_table64 = comp_data->tables[table_idx].table_flags & TABLE64_FLAG
                             ? true
                             : false;

            /* TODO: The offset should be i32? table size >= 0x1_0000_0000 is
             * still valid in table64 spec test */
            bh_assert(table_init_data->mode == 0
                      && table_init_data->table_index == 0
                      && table_init_data->offset.init_expr_type
                             == (is_table64 ? INIT_EXPR_TYPE_I64_CONST
                                            : INIT_EXPR_TYPE_I32_CONST));
            (void)is_table64;

            /* Table.grow is in ref-types proposal, so check for init size */
            if ((uint32)table_init_data->offset.u.i32
                > table->table_init_size) {
                LOG_DEBUG("base_offset(%d) > table->init_size(%d)",
                          table_init_data->offset.u.i32,
                          table->table_init_size);
                aot_set_last_error(
                    "out of bounds table access from elem segment");
                return false;
            }

            length = table_init_data->func_index_count;
            if ((uint32)table_init_data->offset.u.i32 + length
                > table->table_init_size) {
                LOG_DEBUG("base_offset(%d) + length(%d)> table->cur_size(%d)",
                          table_init_data->offset.u.i32, length,
                          table->table_init_size);
                aot_set_last_error(
                    "out of bounds table access from elem segment");
                return false;
            }

            for (k = 0; k < table_init_data->func_index_count; k++) {
                uint32 table_elem_idx = table_init_data->offset.u.i32 + k;
                uint32 func_idx = table_init_data->func_indexes[k];
                if (table_elem_idx < table->table_init_size) {
                    values[table_elem_idx] = I32_CONST(func_idx);
                    if (!values[table_elem_idx]) {
                        aot_set_last_error("llvm build const failed");
                        wasm_runtime_free(values);
                        return false;
                    }
                }
            }
        }

        initializer = LLVMConstArray(I32_TYPE, values, table->table_init_size);
        if (values) {
            wasm_runtime_free(values);
            values = NULL;
        }
        if (!initializer) {
            aot_set_last_error("llvm build const failed");
            return false;
        }

        global_type = LLVMArrayType(I32_TYPE, table->table_init_size);
        if (!global_type) {
            aot_set_last_error("create llvm array type failed");
            return false;
        }
        if (!create_wasm_global(comp_ctx, global_type, "table_elems",
                                initializer, true)) {
            return false;
        }
    }

    if (comp_data->import_func_count + comp_data->func_count > 0
        && !comp_ctx->no_sandbox_mode) {
        /* Create func_ptrs global */
        total_size = (uint64)sizeof(LLVMValueRef)
                     * (comp_data->import_func_count + comp_data->func_count);
        if (total_size > 0
            && !(values = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed");
            return false;
        }

        for (i = 0; i < comp_data->import_func_count; i++) {
            values[i] = int8_null_ptr;
        }
        for (i = 0; i < comp_data->func_count; i++) {
            values[comp_data->import_func_count + i] =
                comp_ctx->func_ctxes[i]->func;
        }

        initializer = LLVMConstArray(INT8_PTR_TYPE, values,
                                     comp_data->import_func_count
                                         + comp_data->func_count);
        if (values) {
            wasm_runtime_free(values);
            values = NULL;
        }
        if (!initializer) {
            aot_set_last_error("llvm build const failed");
            return false;
        }

        global_type = LLVMArrayType(INT8_PTR_TYPE, comp_data->import_func_count
                                                       + comp_data->func_count);
        if (!global_type) {
            aot_set_last_error("create llvm array type failed");
            return false;
        }
        if (!create_wasm_global(comp_ctx, global_type, "func_ptrs", initializer,
                                false)) {
            return false;
        }

        /* Create func_type_indexes global */
        total_size = (uint64)sizeof(LLVMValueRef)
                     * (comp_data->import_func_count + comp_data->func_count);
        if (total_size > 0
            && !(values = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed");
            return false;
        }

        for (i = 0; i < comp_data->import_func_count; i++) {
            uint32 func_type_idx = comp_data->import_funcs[i].func_type_index;
            uint32 smallest_func_type_idx = wasm_get_smallest_type_idx(
                comp_data->func_types, comp_data->func_type_count,
                func_type_idx);
            values[i] = I32_CONST(smallest_func_type_idx);
            if (!values[i]) {
                aot_set_last_error("llvm build const failed");
                wasm_runtime_free(values);
                return false;
            }
        }

        for (i = 0; i < comp_data->func_count; i++) {
            uint32 func_type_idx = comp_data->funcs[i]->func_type_index;
            uint32 smallest_func_type_idx = wasm_get_smallest_type_idx(
                comp_data->func_types, comp_data->func_type_count,
                func_type_idx);
            values[comp_data->import_func_count + i] =
                I32_CONST(smallest_func_type_idx);
            if (!values[comp_data->import_func_count + i]) {
                aot_set_last_error("llvm build const failed");
                wasm_runtime_free(values);
                return false;
            }
        }

        initializer = LLVMConstArray(I32_TYPE, values,
                                     comp_data->import_func_count
                                         + comp_data->func_count);
        if (values) {
            wasm_runtime_free(values);
            values = NULL;
        }
        if (!initializer) {
            aot_set_last_error("llvm build const failed");
            return false;
        }

        global_type = LLVMArrayType(I32_TYPE, comp_data->import_func_count
                                                  + comp_data->func_count);
        if (!global_type) {
            aot_set_last_error("create llvm array type failed");
            return false;
        }
        if (!create_wasm_global(comp_ctx, global_type, "func_type_indexes",
                                initializer, true)) {
            return false;
        }
    }

    /* Create wasm_import_global#N globals */
    for (i = 0; i < comp_data->import_global_count; i++) {
        AOTImportGlobal *aot_import_global = comp_data->import_globals + i;

        initializer = NULL;
        switch (aot_import_global->type) {
            case VALUE_TYPE_I32:
                global_type = I32_TYPE;
                initializer =
                    I32_CONST(aot_import_global->global_data_linked.i32);
                break;
            case VALUE_TYPE_I64:
                global_type = I64_TYPE;
                initializer =
                    I64_CONST(aot_import_global->global_data_linked.i64);
                break;
            case VALUE_TYPE_F32:
                global_type = F32_TYPE;
                initializer =
                    F32_CONST(aot_import_global->global_data_linked.f32);
                break;
            case VALUE_TYPE_F64:
                global_type = F64_TYPE;
                initializer =
                    F64_CONST(aot_import_global->global_data_linked.f64);
                break;
            default:
                bh_assert(0);
                break;
        }
        CHECK_LLVM_CONST(initializer);

        snprintf(buf, sizeof(buf), "%s%d", "wasm_import_global#", i);
        if (!create_wasm_global(comp_ctx, global_type, buf, initializer,
                                !aot_import_global->is_mutable)) {
            return false;
        }
    }

    /* Create wasm_global#N globals */
    for (i = 0; i < comp_data->global_count; i++) {
        AOTGlobal *aot_global = comp_data->globals + i;

        initializer = NULL;
        switch (aot_global->type) {
            case VALUE_TYPE_I32:
                global_type = I32_TYPE;
                bh_assert(aot_global->init_expr.init_expr_type
                              == INIT_EXPR_TYPE_I32_CONST
                          || aot_global->init_expr.init_expr_type
                                 == INIT_EXPR_TYPE_GET_GLOBAL);
                if (aot_global->init_expr.init_expr_type
                    == INIT_EXPR_TYPE_I32_CONST)
                    initializer = I32_CONST(aot_global->init_expr.u.i32);
                else {
                    AOTImportGlobal *aot_import_global =
                        comp_data->import_globals + aot_global->init_expr.u.u32;
                    bh_assert(aot_global->init_expr.u.u32
                                  < comp_data->import_global_count
                              && aot_import_global->type == VALUE_TYPE_I32);
                    initializer =
                        I32_CONST(aot_import_global->global_data_linked.i32);
                }
                break;
            case VALUE_TYPE_I64:
                global_type = I64_TYPE;
                bh_assert(aot_global->init_expr.init_expr_type
                              == INIT_EXPR_TYPE_I64_CONST
                          || aot_global->init_expr.init_expr_type
                                 == INIT_EXPR_TYPE_GET_GLOBAL);
                if (aot_global->init_expr.init_expr_type
                    == INIT_EXPR_TYPE_I64_CONST)
                    initializer = I64_CONST(aot_global->init_expr.u.i64);
                else {
                    AOTImportGlobal *aot_import_global =
                        comp_data->import_globals + aot_global->init_expr.u.u32;
                    bh_assert(aot_global->init_expr.u.u32
                                  < comp_data->import_global_count
                              && aot_import_global->type == VALUE_TYPE_I64);
                    initializer =
                        I64_CONST(aot_import_global->global_data_linked.i64);
                }
                break;
            case VALUE_TYPE_F32:
                global_type = F32_TYPE;
                bh_assert(aot_global->init_expr.init_expr_type
                              == INIT_EXPR_TYPE_F32_CONST
                          || aot_global->init_expr.init_expr_type
                                 == INIT_EXPR_TYPE_GET_GLOBAL);
                if (aot_global->init_expr.init_expr_type
                    == INIT_EXPR_TYPE_F32_CONST)
                    initializer = F32_CONST(aot_global->init_expr.u.f32);
                else {
                    AOTImportGlobal *aot_import_global =
                        comp_data->import_globals + aot_global->init_expr.u.u32;
                    bh_assert(aot_global->init_expr.u.u32
                                  < comp_data->import_global_count
                              && aot_import_global->type == VALUE_TYPE_F32);
                    initializer =
                        F32_CONST(aot_import_global->global_data_linked.f32);
                }
                break;
            case VALUE_TYPE_F64:
                global_type = F64_TYPE;
                bh_assert(aot_global->init_expr.init_expr_type
                              == INIT_EXPR_TYPE_F64_CONST
                          || aot_global->init_expr.init_expr_type
                                 == INIT_EXPR_TYPE_GET_GLOBAL);
                if (aot_global->init_expr.init_expr_type
                    == INIT_EXPR_TYPE_F64_CONST)
                    initializer = F64_CONST(aot_global->init_expr.u.f64);
                else {
                    AOTImportGlobal *aot_import_global =
                        comp_data->import_globals + aot_global->init_expr.u.u32;
                    bh_assert(aot_global->init_expr.u.u32
                                  < comp_data->import_global_count
                              && aot_import_global->type == VALUE_TYPE_F64);
                    initializer =
                        F64_CONST(aot_import_global->global_data_linked.f64);
                }
                break;
            default:
                bh_assert(0);
                break;
        }
        CHECK_LLVM_CONST(initializer);

        snprintf(buf, sizeof(buf), "%s%d", "wasm_global#", i);
        if (!create_wasm_global(comp_ctx, global_type, buf, initializer,
                                !aot_global->is_mutable)) {
            return false;
        }
    }

    if (!comp_ctx->no_sandbox_mode) {
        /* Create exception_msgs global */
        uint32 exce_msg_count = sizeof(exception_msgs) / sizeof(const char *);
        bh_assert(exce_msg_count == EXCE_ID_MAX - EXCE_ID_MIN + 1);

        total_size = (uint64)sizeof(LLVMValueRef) * exce_msg_count;
        if (!(values = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed");
            return false;
        }

        for (i = 0; i < exce_msg_count; i++) {
            snprintf(buf, sizeof(buf), "%s%u", "exception_msg#", i);
            /* pass module M */
            values[i] = LLVMBuildGlobalStringPtr_v2(
                comp_ctx->builder, exception_msgs[i], buf, comp_ctx->module);
            if (!values[i]) {
                aot_set_last_error("llvm build const failed");
                wasm_runtime_free(values);
                return false;
            }
        }

        initializer = LLVMConstArray(INT8_PTR_TYPE, values, exce_msg_count);
        wasm_runtime_free(values);
        if (!initializer) {
            aot_set_last_error("llvm build const failed");
            return false;
        }

        global_type = LLVMArrayType(INT8_PTR_TYPE, exce_msg_count);
        if (!global_type) {
            aot_set_last_error("create llvm array type failed");
            return false;
        }
        if (!create_wasm_global(comp_ctx, global_type, "exception_msgs",
                                initializer, true)) {
            return false;
        }

        /* Create exception_id global */
        if (!create_wasm_global(comp_ctx, I32_TYPE, "exception_id", I32_ZERO,
                                false)) {
            return false;
        }
    }

    return true;
fail:
    return false;
}

typedef struct NativeSymbol {
    const char *module_name;
    const char *symbol_name;
    const char *signature;
} NativeSymbol;

static int
native_symbol_cmp(const void *native_symbol1, const void *native_symbol2)
{
    return strcmp(((const NativeSymbol *)native_symbol1)->symbol_name,
                  ((const NativeSymbol *)native_symbol2)->symbol_name);
}

static NativeSymbol native_symbols_spectest[] = {
    { "spectest", "print", "()" },
    { "spectest", "print_i32", "(i)" },
    { "spectest", "print_i32_f32", "(if)" },
    { "spectest", "print_f64_f64", "(FF)" },
    { "spectest", "print_f32", "(f)" },
    { "spectest", "print_f64", "(F)" },
};

/* clang-format off */
static NativeSymbol native_symbols_libc_builtin[] = {
    { "env", "printf", "(ii)i" },
    { "env", "sprintf", "(iii)i" },
    { "env", "snprintf", "(iiii)i" },
    { "env", "vprintf", "(ii)i" },
    { "env", "vsprintf", "(iii)i" },
    { "env", "vsnprintf", "(iiii)i" },
    { "env", "puts", "(i)i" },
    { "env", "putchar", "(i)i" },
    { "env", "memcmp", "(iii)i" },
    { "env", "memcpy", "(iii)i" },
    { "env", "memmove", "(iii)i" },
    { "env", "memset", "(iii)i" },
    { "env", "strchr", "(ii)i" },
    { "env", "strcmp", "(ii)i" },
    { "env", "strcpy", "(ii)i" },
    { "env", "strlen", "(i)i" },
    { "env", "strncmp", "(iii)i" },
    { "env", "strncpy", "(iii)i" },
    { "env", "malloc", "(i)i" },
    { "env", "realloc", "(ii)i" },
    { "env", "calloc", "(ii)i" },
    { "env", "strdup", "(i)i" },
    { "env", "free", "(i)" },
    { "env", "atoi", "(i)i" },
    { "env", "exit", "(i)" },
    { "env", "strtol", "(iii)i" },
    { "env", "strtoul", "(iii)i" },
    { "env", "memchr", "(iii)i" },
    { "env", "strncasecmp", "(iii)i" },
    { "env", "strspn", "(ii)i" },
    { "env", "strcspn", "(ii)i" },
    { "env", "strstr", "(ii)i" },
    { "env", "isupper", "(i)i" },
    { "env", "isalpha", "(i)i" },
    { "env", "isspace", "(i)i" },
    { "env", "isgraph", "(i)i" },
    { "env", "isprint", "(i)i" },
    { "env", "isdigit", "(i)i" },
    { "env", "isxdigit", "(i)i" },
    { "env", "tolower", "(i)i" },
    { "env", "toupper", "(i)i" },
    { "env", "isalnum", "(i)i" },
    { "env", "abort", "(i)" },
    { "env", "fwrite", "(iiii)i" },
    { "env", "clock_gettime", "(ii)i" },
    { "env", "clock", "()I" },
};

static NativeSymbol native_symbols_libc64_builtin[] = {
    { "env", "printf64", "(II)i" },
    { "env", "sprintf64", "(III)i" },
    { "env", "snprintf64", "(IIII)i" },
    { "env", "vprintf64", "(II)i" },
    { "env", "vsprintf64", "(III)i" },
    { "env", "vsnprintf64", "(IIII)i" },
    { "env", "puts64", "(I)i" },
    { "env", "putchar64", "(i)i" },
    { "env", "memcmp64", "(III)i" },
    { "env", "memcpy64", "(III)I" },
    { "env", "memmove64", "(III)I" },
    { "env", "memset64", "(IiI)I" },
    { "env", "strchr64", "(Ii)I" },
    { "env", "strcmp64", "(II)i" },
    { "env", "strcpy64", "(II)I" },
    { "env", "strlen64", "(I)I" },
    { "env", "strncmp64", "(III)i" },
    { "env", "strncpy64", "(III)I" },
    { "env", "malloc64", "(I)I" },
    { "env", "realloc64", "(II)I" },
    { "env", "calloc64", "(II)I" },
    { "env", "strdup64", "(I)I" },
    { "env", "free64", "(I)" },
    { "env", "atoi64", "(I)i" },
    { "env", "exit64", "(i)" },
    { "env", "strtol64", "(IIi)I" },
    { "env", "strtoul64", "(IIi)I" },
    { "env", "memchr64", "(IiI)I" },
    { "env", "strncasecmp64", "(III)i" },
    { "env", "strspn64", "(II)I" },
    { "env", "strcspn64", "(II)I" },
    { "env", "strstr64", "(II)I" },
    { "env", "isupper64", "(i)i" },
    { "env", "isalpha64", "(i)i" },
    { "env", "isspace64", "(i)i" },
    { "env", "isgraph64", "(i)i" },
    { "env", "isprint64", "(i)i" },
    { "env", "isdigit64", "(i)i" },
    { "env", "isxdigit64", "(i)i" },
    { "env", "tolower64", "(i)i" },
    { "env", "toupper64", "(i)i" },
    { "env", "isalnum64", "(i)i" },
    { "env", "abort64", "(i)" },
    { "env", "fwrite64", "(IIII)I" },
    { "env", "clock_gettime64", "(iI)i" },
    { "env", "clock64", "()I" },
};

static NativeSymbol native_symbols_libc64_nosandbox[] = {
    { "env", "printf", "(II)i" },
    { "env", "sprintf", "(III)i" },
    { "env", "snprintf", "(IIII)i" },
    { "env", "vprintf", "(II)i" },
    { "env", "vsprintf", "(III)i" },
    { "env", "vsnprintf", "(IIII)i" },
    { "env", "vasprintf", "(III)i" },
    { "env", "fwrite", "(IIII)I" },
    { "env", "fprintf", "(III)i" },
    { "env", "fclose", "(I)i" },
    { "env", "fputc", "(iI)i" },
    { "env", "fputs", "(II)i" },
    { "env", "sscanf", "(III)i"},
    { "env", "__assert2", "(IiII)" },
    { "env", "__errno", "()I" },
    { "env", "log", "(F)F" },
};
/* clang-format on */

static bool
create_wasm_instance_create_func(const AOTCompData *comp_data,
                                 AOTCompContext *comp_ctx)
{
    LLVMTypeRef func_type, param_types[2] = { 0 };
    LLVMValueRef func, param_values[2], memory_data, cmp;
    LLVMValueRef exce_id_phi = NULL, exce_id, exce_id_global;
    LLVMValueRef memory_data_global;
    LLVMValueRef is_instance_inited_global, is_instance_inited;
    LLVMBasicBlockRef entry_block, end_block, fail_block = NULL;
    LLVMBasicBlockRef alloc_succ_block, inst_not_inited_block;
    LLVMBasicBlockRef post_instantiate_funcs_succ_block = NULL;
    AOTMemory *aot_memory = &comp_data->memories[0];
    uint64 memory_data_size = (uint64)aot_memory->num_bytes_per_page
                              * aot_memory->mem_init_page_count;
    bool memory_data_size_fixed = MEMORY_DATA_SIZE_FIXED(aot_memory),
         post_instantiate_funcs_exists = false;
    uint64 total_size;
    uint32 i, j, n_native_symbols;
    char func_name[48], buf[128];

    if (!(is_instance_inited_global = create_wasm_global(
              comp_ctx, INT8_TYPE, "is_instance_inited", I8_ZERO, false))) {
        return false;
    }

    if (!(func_type = LLVMFunctionType(VOID_TYPE, NULL, 0, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* Add `bool wasm_instance_create()` function */
    snprintf(func_name, sizeof(func_name), "%s", "wasm_instance_create");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    /**
     * Make wasm_instance_create an initializer function, refer to
     * https://llvm.org/docs/LangRef.html#the-llvm-global-ctors-global-variable
     */
    if (comp_ctx->no_sandbox_mode) {
        LLVMTypeRef struct_type, struct_elem_types[3], array_type;
        LLVMValueRef struct_value, struct_elems[3], initializer, ctors_global;

        struct_elem_types[0] = I32_TYPE;
        struct_elem_types[1] = INT8_PTR_TYPE;
        struct_elem_types[2] = INT8_PTR_TYPE;

        if (!(struct_type = LLVMStructType(struct_elem_types, 3, false))) {
            aot_set_last_error("llvm add struct type failed");
            return false;
        }

        struct_elems[0] = I32_CONST(65535);
        CHECK_LLVM_CONST(struct_elems[0]);
        struct_elems[1] = func;
        struct_elems[2] = LLVMConstPointerNull(INT8_PTR_TYPE);
        CHECK_LLVM_CONST(struct_elems[2]);

        if (!(array_type = LLVMArrayType(struct_type, 1))) {
            aot_set_last_error("llvm add array type failed");
            return false;
        }

        if (!(struct_value = LLVMConstStruct(struct_elems, 3, false))) {
            aot_set_last_error("llvm add const struct failed");
            return false;
        }

        initializer = LLVMConstArray(struct_type, &struct_value, 1);
        CHECK_LLVM_CONST(initializer);

        if (!(ctors_global = LLVMAddGlobal(comp_ctx->module, array_type,
                                           "llvm.global_ctors"))) {
            aot_set_last_error("llvm add global failed");
            return false;
        }

        LLVMSetGlobalConstant(ctors_global, true);
        LLVMSetInitializer(ctors_global, initializer);
        LLVMSetLinkage(ctors_global, LLVMAppendingLinkage);
    }

    /* Add function entry block and end block */
    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))
        || !(end_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                       "func_end"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }
    if (!comp_ctx->no_sandbox_mode
        && !(fail_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                        "fail"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }

    if (!comp_ctx->no_sandbox_mode) {
        /* Build fail block */
        LLVMPositionBuilderAtEnd(comp_ctx->builder, fail_block);

        if (!(exce_id_phi = LLVMBuildPhi(comp_ctx->builder, I32_TYPE,
                                         "exception_id_phi"))) {
            aot_set_last_error("llvm build phi failed.");
            return false;
        }

        exce_id_global = LLVMGetNamedGlobal(comp_ctx->module, "exception_id");
        bh_assert(exce_id_global);
        if (!LLVMBuildStore(comp_ctx->builder, exce_id_phi, exce_id_global)) {
            aot_set_last_error("llvm build store failed.");
            return false;
        }

        if (!LLVMBuildRetVoid(comp_ctx->builder)) {
            aot_set_last_error("llvm build ret failed.");
            return false;
        }
    }

    /* Build entry block */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);

    if (!(inst_not_inited_block = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func, "instance_not_inited"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }
    LLVMMoveBasicBlockAfter(inst_not_inited_block, entry_block);

    if (!(is_instance_inited = LLVMBuildLoad2(comp_ctx->builder, INT8_TYPE,
                                              is_instance_inited_global,
                                              "is_instance_inited"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }
    if (!(cmp = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, is_instance_inited,
                              I8_ZERO, "cmp"))) {
        aot_set_last_error("llvm build icmp failed.");
        return false;
    }
    if (!LLVMBuildCondBr(comp_ctx->builder, cmp, inst_not_inited_block,
                         end_block)) {
        aot_set_last_error("llvm build condbr failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, inst_not_inited_block);

    n_native_symbols = sizeof(native_symbols_spectest) / sizeof(NativeSymbol);
    qsort(native_symbols_spectest, n_native_symbols, sizeof(NativeSymbol),
          native_symbol_cmp);

    n_native_symbols =
        sizeof(native_symbols_libc_builtin) / sizeof(NativeSymbol);
    qsort(native_symbols_libc_builtin, n_native_symbols, sizeof(NativeSymbol),
          native_symbol_cmp);

    n_native_symbols =
        sizeof(native_symbols_libc64_builtin) / sizeof(NativeSymbol);
    qsort(native_symbols_libc64_builtin, n_native_symbols, sizeof(NativeSymbol),
          native_symbol_cmp);

    n_native_symbols =
        sizeof(native_symbols_libc64_nosandbox) / sizeof(NativeSymbol);
    qsort(native_symbols_libc64_nosandbox, n_native_symbols,
          sizeof(NativeSymbol), native_symbol_cmp);

    /* Register native functions */
    for (i = 0; i < comp_data->import_func_count; i++) {
        LLVMTypeRef native_func_type, *native_param_types, native_ret_type;
        LLVMValueRef func_ptrs_global, func_idx_const, p_func_ptr;
        WASMType *wasm_func_type = comp_data->import_funcs[i].func_type;
        NativeSymbol *native_symbol, key = { 0 };
        char signature[32] = { 0 }, *p = signature, symbol_name[128] = { 0 };
        bool native_symbol_found = false;

        bh_assert(wasm_func_type->result_count <= 1);

        *p++ = '(';
        for (j = 0;
             j < wasm_func_type->param_count + wasm_func_type->result_count;
             j++) {
            if (j == wasm_func_type->param_count)
                *p++ = ')';
            switch (wasm_func_type->types[j]) {
                case VALUE_TYPE_I32:
                    *p++ = 'i';
                    break;
                case VALUE_TYPE_I64:
                    *p++ = 'I';
                    break;
                case VALUE_TYPE_F32:
                    *p++ = 'f';
                    break;
                case VALUE_TYPE_F64:
                    *p++ = 'F';
                    break;
                default:
                    bh_assert(0);
                    break;
            }
        }
        if (!wasm_func_type->result_count)
            *p++ = ')';

        key.symbol_name = comp_data->import_funcs[i].func_name;

        if (!strcmp(comp_data->import_funcs[i].module_name, "env")) {
            NativeSymbol *native_symbols = native_symbols_libc_builtin;
            n_native_symbols =
                sizeof(native_symbols_libc_builtin) / sizeof(NativeSymbol);
            if (comp_ctx->no_sandbox_mode) {
                native_symbols = native_symbols_libc64_nosandbox;
                n_native_symbols = sizeof(native_symbols_libc64_nosandbox)
                                   / sizeof(NativeSymbol);
            }
            else if (IS_MEMORY64) {
                snprintf(symbol_name, sizeof(symbol_name), "%s%s",
                         comp_data->import_funcs[i].func_name, "64");
                key.symbol_name = symbol_name;
                native_symbols = native_symbols_libc64_builtin;
                n_native_symbols = sizeof(native_symbols_libc64_builtin)
                                   / sizeof(NativeSymbol);
            }
            if ((native_symbol =
                     bsearch(&key, native_symbols, n_native_symbols,
                             sizeof(NativeSymbol), native_symbol_cmp))
                && !strcmp(native_symbol->signature, signature)) {
                native_symbol_found = true;
            }
        }
        else if (!strcmp(comp_data->import_funcs[i].module_name, "spectest")
                 && (native_symbol = bsearch(
                         &key, native_symbols_spectest,
                         sizeof(native_symbols_spectest) / sizeof(NativeSymbol),
                         sizeof(NativeSymbol), native_symbol_cmp))
                 && !strcmp(native_symbol->signature, signature)) {
            native_symbol_found = true;
        }

        if (!native_symbol_found && !comp_ctx->no_sandbox_mode) {
            snprintf(buf, sizeof(buf),
                     "warning: failed to link import function (%s, %s)",
                     comp_data->import_funcs[i].module_name,
                     comp_data->import_funcs[i].func_name);
            os_printf("%s\n", buf);

            if (!(func_type =
                      LLVMFunctionType(I32_TYPE, &INT8_PTR_TYPE, 1, false))) {
                aot_set_last_error("create LLVM function type failed.");
                return false;
            }
            if (!(func = LLVMGetNamedFunction(comp_ctx->module, "puts"))
                && !(func = LLVMAddFunction(comp_ctx->module, "puts",
                                            func_type))) {
                aot_set_last_error("add LLVM function failed.");
                return false;
            }

            param_values[0] =
                LLVMBuildGlobalStringPtr(comp_ctx->builder, buf, "msg");
            CHECK_LLVM_CONST(param_values[0]);

            if (!LLVMBuildCall2(comp_ctx->builder, func_type, func,
                                param_values, 1, "puts_res")) {
                aot_set_last_error("llvm build call failed.");
                return false;
            }

            comp_ctx->import_func_ptrs[i] = LLVMConstPointerNull(INT8_PTR_TYPE);
            CHECK_LLVM_CONST(comp_ctx->import_func_ptrs[i]);

            continue;
        }

        native_param_types = NULL;
        total_size = (uint64)sizeof(LLVMTypeRef) * wasm_func_type->param_count;
        if (total_size > 0
            && !(native_param_types =
                     wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed");
            return false;
        }
        for (j = 0; j < wasm_func_type->param_count; j++) {
            switch (wasm_func_type->types[j]) {
                case VALUE_TYPE_I32:
                    native_param_types[j] = I32_TYPE;
                    break;
                case VALUE_TYPE_I64:
                    native_param_types[j] = I64_TYPE;
                    break;
                case VALUE_TYPE_F32:
                    native_param_types[j] = F32_TYPE;
                    break;
                case VALUE_TYPE_F64:
                    native_param_types[j] = F64_TYPE;
                    break;
                default:
                    bh_assert(0);
                    break;
            }
        }
        native_ret_type = VOID_TYPE;
        if (wasm_func_type->result_count > 0) {
            switch (wasm_func_type->types[wasm_func_type->param_count]) {
                case VALUE_TYPE_I32:
                    native_ret_type = I32_TYPE;
                    break;
                case VALUE_TYPE_I64:
                    native_ret_type = I64_TYPE;
                    break;
                case VALUE_TYPE_F32:
                    native_ret_type = F32_TYPE;
                    break;
                case VALUE_TYPE_F64:
                    native_ret_type = F64_TYPE;
                    break;
                default:
                    bh_assert(0);
                    break;
            }
        }

        native_func_type = LLVMFunctionType(native_ret_type, native_param_types,
                                            wasm_func_type->param_count, false);
        if (native_param_types)
            wasm_runtime_free(native_param_types);
        if (!native_func_type) {
            aot_set_last_error("create LLVM function type failed.");
            return false;
        }

        if (native_symbol_found)
            snprintf(buf, sizeof(buf), "%s%s", native_symbol->symbol_name,
                     "_wrapper");
        else
            snprintf(buf, sizeof(buf), "%s",
                     comp_data->import_funcs[i].func_name);
        if (!(func = LLVMGetNamedFunction(comp_ctx->module, buf))
            && !(func = LLVMAddFunction(comp_ctx->module, buf,
                                        native_func_type))) {
            aot_set_last_error("add LLVM function failed.");
            return false;
        }
        comp_ctx->import_func_ptrs[i] = func;

        if (!comp_ctx->no_sandbox_mode) {
            func_ptrs_global =
                LLVMGetNamedGlobal(comp_ctx->module, "func_ptrs");
            bh_assert(func_ptrs_global);
            func_idx_const = I32_CONST(i);
            CHECK_LLVM_CONST(func_idx_const);
            p_func_ptr = LLVMBuildInBoundsGEP2(
                comp_ctx->builder, INT8_PTR_TYPE, func_ptrs_global,
                &func_idx_const, 1, "p_func_ptr");
            if (!p_func_ptr) {
                aot_set_last_error("llvm build inbound gep failed.");
                return false;
            }

            if (!LLVMBuildStore(comp_ctx->builder, func, p_func_ptr)) {
                aot_set_last_error("llvm build store failed.");
                return false;
            }
        }
    }

    param_types[0] =
        comp_ctx->pointer_size == sizeof(uint64) ? I64_TYPE : I32_TYPE;
    if (!(func_type = LLVMFunctionType(INT8_PTR_TYPE, param_types, 1, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* Call malloc function to allocate memory for wasm linear memory */
    snprintf(func_name, sizeof(func_name), "%s", "malloc");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    param_values[0] = comp_ctx->pointer_size == sizeof(uint64)
                          ? I64_CONST(memory_data_size)
                          : I32_CONST((uint32)memory_data_size);
    CHECK_LLVM_CONST(param_values[0]);
    if (!(memory_data =
              LLVMBuildCall2(comp_ctx->builder, func_type, func, param_values,
                             1, "memory_data_allocated"))) {
        aot_set_last_error("llvm build call failed.");
        return false;
    }

    /* Check the return value of malloc */
    if (!(cmp =
              LLVMBuildIsNotNull(comp_ctx->builder, memory_data, "not_null"))) {
        aot_set_last_error("llvm build is null failed.");
        return false;
    }

    if (!(alloc_succ_block = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func, "allocate_success"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }
    LLVMMoveBasicBlockAfter(alloc_succ_block,
                            LLVMGetInsertBlock(comp_ctx->builder));

    if (!comp_ctx->no_sandbox_mode) {
        if (!LLVMBuildCondBr(comp_ctx->builder, cmp, alloc_succ_block,
                             fail_block)) {
            aot_set_last_error("llvm build cond br failed.");
            return false;
        }
        exce_id = I32_CONST(EXCE_ALLOCATE_MEMORY_FAILED);
        CHECK_LLVM_CONST(exce_id);
        LLVMAddIncoming(exce_id_phi, &exce_id, &inst_not_inited_block, 1);
    }
    else {
        if (!LLVMBuildCondBr(comp_ctx->builder, cmp, alloc_succ_block,
                             end_block)) {
            aot_set_last_error("llvm build cond br failed.");
            return false;
        }
    }

    /* memset(memory_data, 0, memory_data_size) */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, alloc_succ_block);

    if (!LLVMBuildMemSet(comp_ctx->builder, memory_data, I8_ZERO,
                         param_values[0], 8)) {
        aot_set_last_error("llvm build memset failed.");
        return false;
    }

    /* Initialize the wasm linear memory */
    for (i = 0; i < comp_data->data_seg_count; i++) {
        WASMDataSeg *data_seg = comp_data->data_segments[i];
        LLVMValueRef data_seg_global, offset_value, length_value;
        LLVMValueRef dst_value, src_value;
        uint64 data_seg_offset;
        uint32 data_seg_length;

        if (!data_seg->is_passive) {
            bh_assert(data_seg->base_offset.init_expr_type == IS_MEMORY64
                          ? INIT_EXPR_TYPE_I64_CONST
                          : INIT_EXPR_TYPE_I32_CONST);
            if (data_seg->base_offset.init_expr_type
                == INIT_EXPR_TYPE_I64_CONST)
                data_seg_offset = data_seg->base_offset.u.u64;
            else
                data_seg_offset = data_seg->base_offset.u.u32;
            data_seg_length = data_seg->data_length;

            for (j = 0; j < data_seg_length; j++) {
                if (data_seg->data[j] != 0)
                    break;
            }
            if (j == data_seg_length)
                /* Ignore copying if all bytes are zero */
                continue;

            if (data_seg_length > 0) {
                offset_value = comp_ctx->pointer_size == sizeof(uint64)
                                   ? I64_CONST(data_seg_offset)
                                   : I32_CONST(data_seg_offset);
                CHECK_LLVM_CONST(offset_value);
                if (!(dst_value = LLVMBuildInBoundsGEP2(
                          comp_ctx->builder, INT8_TYPE, memory_data,
                          &offset_value, 1, "dst"))) {
                    aot_set_last_error("llvm build inbound gep failed.");
                    return false;
                }

                snprintf(buf, sizeof(buf), "%s%d", "data_seg#", i);
                data_seg_global = LLVMGetNamedGlobal(comp_ctx->module, buf);
                bh_assert(data_seg_global);
                src_value = data_seg_global;

                length_value = comp_ctx->pointer_size == sizeof(uint64)
                                   ? I64_CONST(data_seg_length)
                                   : I32_CONST(data_seg_length);
                CHECK_LLVM_CONST(length_value);

                if (!LLVMBuildMemCpy(
                        comp_ctx->builder, dst_value,
                        data_seg_offset % 8 == 0 ? 8 : data_seg_offset % 8,
                        src_value, 8, length_value)) {
                    aot_set_last_error("llvm build memcpy failed");
                    return false;
                }
            }
        }
    }

    /* Apply relocations for memory data */
    if (comp_ctx->no_sandbox_mode) {
        const uint8 *data_section_body =
            comp_data->wasm_module->data_section_body;
        WASMRelocation *reloc = comp_data->data_relocs;
        WASMSymbol *symbol;
        WASMSymbolData *sym_data;

        for (i = 0; i < comp_data->data_reloc_count; i++, reloc++) {
            if (reloc->type == R_WASM_MEMORY_ADDR_I64) {
                if (comp_ctx->pointer_size != sizeof(uint64)) {
                    aot_set_last_error(
                        "unsupported reloc type R_WASM_MEMORY_ADDR_I64 "
                        "in non 64-bit target");
                    return false;
                }

                /* reloc index and symbol type have been checked in
                   wasm loader */
                bh_assert(reloc->index < comp_data->symbol_count
                          && comp_data->symbols[reloc->index].type
                                 == WASM_SYMBOL_TYPE_DATA);
                symbol = &comp_data->symbols[reloc->index];
                sym_data = &symbol->u.sym_data;

                LLVMValueRef reloc_offset_const, target_addr;
                LLVMValueRef symbol_offset_const, symbol_addr;
                WASMDataSeg *data_seg;
                uint8 *data_seg_bytes;
                uint64 reloc_offset = reloc->offset;
                uint64 reloc_addend = reloc->addend;
                uint64 data_seg_offset, data_seg_data_length;
                uint64 init_addend = 0, offset_to_data_section;
                uint64 symbol_offset = sym_data->data_offset;
                bool found = false;

                /* Get init_addend and adjust reloc_offset */
                for (j = 0; j < comp_data->data_seg_count; j++) {
                    WASMDataSeg *data_seg = comp_data->data_segments[j];

                    data_seg_bytes = data_seg->data;
                    data_seg_data_length = data_seg->data_length;
                    data_seg_offset = data_seg->base_offset.u.u64;
                    offset_to_data_section =
                        (uint64)(uintptr_t)(data_seg_bytes - data_section_body);

                    bh_assert(data_seg->base_offset.init_expr_type
                              == INIT_EXPR_TYPE_I64_CONST);

                    if (reloc_offset >= offset_to_data_section
                        && reloc_offset < offset_to_data_section
                                              + data_seg_data_length) {
                        init_addend =
                            *(uint64 *)(data_section_body + reloc_offset);
                        /* adjust reloc offset to the offset to linear memory */
                        reloc_offset =
                            (uint64)(uintptr_t)(data_section_body + reloc_offset
                                                - data_seg_bytes
                                                + data_seg_offset);
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    aot_set_last_error(
                        "cannot find a valid data segment for reloc offset");
                    return false;
                }

                reloc_offset_const = I64_CONST(reloc_offset);
                CHECK_LLVM_CONST(reloc_offset_const);
                snprintf(buf, sizeof(buf), "%s%d", "target_addr#", i);
                if (!(target_addr = LLVMBuildInBoundsGEP2(
                          comp_ctx->builder, INT8_TYPE, memory_data,
                          &reloc_offset_const, 1, buf))) {
                    aot_set_last_error("llvm build inbound gep failed.");
                    return false;
                }

                data_seg = comp_data->data_segments[sym_data->seg_index];
                bh_assert(data_seg->base_offset.init_expr_type
                          == INIT_EXPR_TYPE_I64_CONST);
                data_seg_offset = data_seg->base_offset.u.u64;

                bh_assert(data_seg_offset + symbol_offset == init_addend);
                (void)init_addend;

                symbol_offset = data_seg_offset + symbol_offset + reloc_addend;

                symbol_offset_const = I64_CONST(symbol_offset);
                CHECK_LLVM_CONST(symbol_offset_const);

                if (!(symbol_addr = LLVMBuildInBoundsGEP2(
                          comp_ctx->builder, INT8_TYPE, memory_data,
                          &symbol_offset_const, 1, buf))) {
                    aot_set_last_error("llvm build inbound gep failed.");
                    return false;
                }

                if (!(target_addr =
                          LLVMBuildBitCast(comp_ctx->builder, target_addr,
                                           INT8_PPTR_TYPE, "p_target_addr"))) {
                    aot_set_last_error("llvm build bit cast failed");
                    return false;
                }

                if (!LLVMBuildStore(comp_ctx->builder, symbol_addr,
                                    target_addr)) {
                    aot_set_last_error("llvm build store failed");
                    return false;
                }
            }
            else if (reloc->type == R_WASM_TABLE_INDEX_I64) {
                if (comp_ctx->pointer_size != sizeof(uint64)) {
                    aot_set_last_error(
                        "unsupported reloc type R_WASM_TABLE_INDEX_I64 "
                        "in non 64-bit target");
                    return false;
                }

                LLVMValueRef reloc_offset_const, target_addr;
                uint8 *data_seg_bytes;
                uint64 reloc_offset = reloc->offset;
                uint64 data_seg_offset, data_seg_data_length;
                uint64 init_addend = 0, offset_to_data_section;
                bool found = false;

                /* Get init_addend and adjust reloc_offset */
                for (j = 0; j < comp_data->data_seg_count; j++) {
                    WASMDataSeg *data_seg = comp_data->data_segments[j];

                    data_seg_bytes = data_seg->data;
                    data_seg_data_length = data_seg->data_length;
                    data_seg_offset = data_seg->base_offset.u.u64;
                    offset_to_data_section =
                        (uint64)(uintptr_t)(data_seg_bytes - data_section_body);

                    bh_assert(data_seg->base_offset.init_expr_type
                              == INIT_EXPR_TYPE_I64_CONST);

                    if (reloc_offset >= offset_to_data_section
                        && reloc_offset < offset_to_data_section
                                              + data_seg_data_length) {
                        init_addend =
                            *(uint64 *)(data_section_body + reloc_offset);
                        /* adjust reloc offset to the offset to linear memory */
                        reloc_offset =
                            (uint64)(uintptr_t)(data_section_body + reloc_offset
                                                - data_seg_bytes
                                                + data_seg_offset);
                        found = true;
                        break;
                    }
                }

                if (!found) {
                    aot_set_last_error(
                        "cannot find a valid data segment for reloc offset");
                    return false;
                }

                LLVMValueRef func_ptr = NULL;

                found = false;
                for (j = 0; j < comp_data->table_init_data_count; j++) {
                    AOTTableInitData *table_init_data =
                        comp_data->table_init_data_list[j];

                    uint32 base_offset = table_init_data->offset.u.i32;
                    if (init_addend >= base_offset
                        && init_addend
                               < base_offset
                                     + table_init_data->func_index_count) {
                        uint32 func_idx =
                            table_init_data
                                ->func_indexes[init_addend - base_offset];
                        WASMSymbol *symbol = &comp_data->symbols[reloc->index];
                        WASMFunctionImport *func_import;
                        WASMFunction *func;

                        if (!symbol->is_defined) {
                            func_import = &symbol->u.import->u.function;
                            bh_assert(func_import->func_idx == func_idx);
                            (void)func_import;
                            func_ptr = comp_ctx->import_func_ptrs[func_idx];
                        }
                        else {
                            func = symbol->u.function;
                            bh_assert(func->func_idx == func_idx);
                            (void)func;
                            func_ptr =
                                comp_ctx
                                    ->func_ctxes[func_idx
                                                 - comp_ctx->import_func_count]
                                    ->func;
                        }

                        found = true;
                        break;
                    }
                }

                if (!found) {
                    aot_set_last_error(
                        "cannot find a valid table segment for reloc offset");
                    return false;
                }

                reloc_offset_const = I64_CONST(reloc_offset);
                CHECK_LLVM_CONST(reloc_offset_const);
                snprintf(buf, sizeof(buf), "%s%d", "target_addr#", i);
                if (!(target_addr = LLVMBuildInBoundsGEP2(
                          comp_ctx->builder, INT8_TYPE, memory_data,
                          &reloc_offset_const, 1, buf))) {
                    aot_set_last_error("llvm build inbound gep failed.");
                    return false;
                }

                if (!(target_addr =
                          LLVMBuildBitCast(comp_ctx->builder, target_addr,
                                           INT8_PPTR_TYPE, "p_target_addr"))) {
                    aot_set_last_error("llvm build bit cast failed");
                    return false;
                }

                if (!(func_ptr = LLVMBuildBitCast(comp_ctx->builder, func_ptr,
                                                  INT8_PTR_TYPE, "func_ptr"))) {
                    aot_set_last_error("llvm build bit cast failed");
                    return false;
                }

                if (!LLVMBuildStore(comp_ctx->builder, func_ptr, target_addr)) {
                    aot_set_last_error("llvm build store failed");
                    return false;
                }
            }
            else {
                aot_set_last_error_v(
                    "unsupported reloc type %u in reloc.DATA section",
                    reloc->type);
                return false;
            }
        }
    }

    memory_data_global = LLVMGetNamedGlobal(comp_ctx->module, "memory_data");
    bh_assert(memory_data_global);

    /* memory_data_global = memory_data */
    if (!LLVMBuildStore(comp_ctx->builder, memory_data, memory_data_global)) {
        aot_set_last_error("llvm build store failed.");
        return false;
    }

    if (memory_data_size_fixed) {
        /* Create host managed heap */
        if (aot_memory->host_managed_heap_offset > 0
            && !comp_ctx->no_sandbox_mode) {
            param_types[0] = INT8_PTR_TYPE;
            param_types[1] = I32_TYPE;

            if (!(func_type =
                      LLVMFunctionType(INT8_PTR_TYPE, param_types, 2, false))) {
                aot_set_last_error("create LLVM function type failed.");
                return false;
            }

            /* Add `void *mem_allocator_create(void *mem, uint32 size)` function
             */
            snprintf(func_name, sizeof(func_name), "%s",
                     "mem_allocator_create");
            if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
                && !(func = LLVMAddFunction(comp_ctx->module, func_name,
                                            func_type))) {
                aot_set_last_error("add LLVM function failed.");
                return false;
            }

            LLVMValueRef heap_mem, heap_size, heap_handle, heap_handle_global;

            heap_mem = I32_CONST(aot_memory->host_managed_heap_offset);
            CHECK_LLVM_CONST(heap_mem);
            heap_size = I32_CONST(memory_data_size
                                  - aot_memory->host_managed_heap_offset);
            CHECK_LLVM_CONST(heap_size);

            param_values[0] =
                LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, memory_data,
                                      &heap_mem, 1, "app_heap_mem");
            param_values[1] = heap_size;

            if (!param_values[0]) {
                aot_set_last_error("llvm build inbounds gep failed.");
                return false;
            }

            if (!(heap_handle =
                      LLVMBuildCall2(comp_ctx->builder, func_type, func,
                                     param_values, 2, "heap_handle"))) {
                aot_set_last_error("llvm build call failed.");
                return false;
            }

            LLVMBasicBlockRef create_heap_succ_block;

            create_heap_succ_block = LLVMAppendBasicBlockInContext(
                comp_ctx->context, func, "create_heap_succ");
            if (!create_heap_succ_block) {
                aot_set_last_error("llvm create basic block failed.");
                return false;
            }

            LLVMMoveBasicBlockAfter(create_heap_succ_block,
                                    LLVMGetInsertBlock(comp_ctx->builder));

            cmp =
                LLVMBuildIsNotNull(comp_ctx->builder, heap_handle, "not_null");
            if (!cmp) {
                aot_set_last_error("llvm build is not null failed.");
                return false;
            }
            if (!LLVMBuildCondBr(comp_ctx->builder, cmp, create_heap_succ_block,
                                 fail_block)) {
                aot_set_last_error("llvm build condbr failed.");
                return false;
            }
            exce_id = I32_CONST(EXCE_ALLOCATE_MEMORY_FAILED);
            CHECK_LLVM_CONST(exce_id);
            LLVMAddIncoming(exce_id_phi, &exce_id, &alloc_succ_block, 1);

            LLVMPositionBuilderAtEnd(comp_ctx->builder, create_heap_succ_block);
            heap_handle_global = LLVMGetNamedGlobal(comp_ctx->module,
                                                    "host_managed_heap_handle");
            bh_assert(heap_handle_global);
            if (!LLVMBuildStore(comp_ctx->builder, heap_handle,
                                heap_handle_global)) {
                aot_set_last_error("llvm build store failed.");
                return false;
            }
        }
    }

    WASMModule *wasm_module = comp_data->wasm_module;
    AOTExport *aot_exports = wasm_module->exports;
    AOTFuncType *aot_func_type;
    uint32 export_count = wasm_module->export_count;
    uint32 func_idx;

    /* Call wasm start function if found */
    if (wasm_module->start_function != (uint32)-1) {
        post_instantiate_funcs_exists = true;
        /* TODO: fix start function can be import function issue, seems haven't
         * init in this step */
        bh_assert(wasm_module->start_function
                  >= wasm_module->import_function_count);
        func_idx =
            wasm_module->start_function - wasm_module->import_function_count;
        func = comp_ctx->func_ctxes[func_idx]->func;
        func_type = comp_ctx->func_ctxes[func_idx]->func_type;
        if (!LLVMBuildCall2(comp_ctx->builder, func_type, func, NULL, 0, "")) {
            aot_set_last_error("llvm build call failed.");
            return false;
        }
    }

    /* Call exported "__wasm_call_ctors" function if found */
    for (i = 0; i < export_count; i++) {
        if (aot_exports[i].kind == EXPORT_KIND_FUNC
            && !strcmp(aot_exports[i].name, "__wasm_call_ctors")) {
            post_instantiate_funcs_exists = true;
            bh_assert(aot_exports[i].index >= comp_data->import_func_count);
            func_idx = aot_exports[i].index - comp_data->import_func_count;

            aot_func_type = wasm_module->functions[func_idx]->func_type;
            if (aot_func_type->param_count == 0
                && aot_func_type->result_count == 0) {
                func = comp_ctx->func_ctxes[func_idx]->func;
                func_type = comp_ctx->func_ctxes[func_idx]->func_type;
                if (!LLVMBuildCall2(comp_ctx->builder, func_type, func, NULL, 0,
                                    "")) {
                    aot_set_last_error("llvm build call failed.");
                    return false;
                }
                break;
            }
        }
    }

    /* Check the exception from post instantiate function calls */
    if (post_instantiate_funcs_exists) {
        if (!(post_instantiate_funcs_succ_block = LLVMAppendBasicBlockInContext(
                  comp_ctx->context, func, "post_instantiate_funcs_succ"))) {
            aot_set_last_error("add LLVM basic block failed.");
            return false;
        }
        LLVMMoveBasicBlockAfter(post_instantiate_funcs_succ_block,
                                LLVMGetInsertBlock(comp_ctx->builder));

        exce_id_global = LLVMGetNamedGlobal(comp_ctx->module, "exception_id");
        bh_assert(exce_id_global);
        if (!(exce_id = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE,
                                       exce_id_global, "exce_id"))) {
            aot_set_last_error("llvm build load failed.");
            return false;
        }

        cmp = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, exce_id, I32_ZERO,
                            "cmp");
        if (!LLVMBuildCondBr(comp_ctx->builder, cmp,
                             post_instantiate_funcs_succ_block, end_block)) {
            aot_set_last_error("llvm build conditional branch failed.");
            return false;
        }

        LLVMPositionBuilderAtEnd(comp_ctx->builder,
                                 post_instantiate_funcs_succ_block);
    }

    if (!LLVMBuildStore(comp_ctx->builder, I8_ONE, is_instance_inited_global)) {
        aot_set_last_error("llvm build store failed.");
        return false;
    }

    if (!LLVMBuildBr(comp_ctx->builder, end_block)) {
        aot_set_last_error("llvm build br failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, end_block);

    if (!LLVMBuildRetVoid(comp_ctx->builder)) {
        aot_set_last_error("llvm build ret failed.");
        return false;
    }

    return true;
fail:
    return false;
}

static bool
create_wasm_instance_destroy_func(const AOTCompData *comp_data,
                                  AOTCompContext *comp_ctx)
{
    LLVMTypeRef func_type, param_types[2];
    LLVMValueRef func, memory_data, cmp, param_values[2];
    LLVMValueRef memory_data_global;
    LLVMValueRef is_instance_inited_global, is_instance_inited;
    LLVMBasicBlockRef entry_block, check_succ_block, end_block;
    char func_name[32];

    is_instance_inited_global =
        LLVMGetNamedGlobal(comp_ctx->module, "is_instance_inited");
    bh_assert(is_instance_inited_global);

    if (!(func_type = LLVMFunctionType(VOID_TYPE, NULL, 0, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* Add `void wasm_instance_destroy()` function */
    snprintf(func_name, sizeof(func_name), "%s", "wasm_instance_destroy");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    /**
     * Make wasm_instance_create a destructor function, refer to
     * https://llvm.org/docs/LangRef.html#the-llvm-global-dtors-global-variable
     */
    if (comp_ctx->no_sandbox_mode) {
        LLVMTypeRef struct_type, struct_elem_types[3], array_type;
        LLVMValueRef struct_value, struct_elems[3], initializer, dtors_global;

        struct_elem_types[0] = I32_TYPE;
        struct_elem_types[1] = INT8_PTR_TYPE;
        struct_elem_types[2] = INT8_PTR_TYPE;

        if (!(struct_type = LLVMStructType(struct_elem_types, 3, false))) {
            aot_set_last_error("llvm add struct type failed");
            return false;
        }

        struct_elems[0] = I32_CONST(65535);
        CHECK_LLVM_CONST(struct_elems[0]);
        struct_elems[1] = func;
        struct_elems[2] = LLVMConstPointerNull(INT8_PTR_TYPE);
        CHECK_LLVM_CONST(struct_elems[2]);

        if (!(array_type = LLVMArrayType(struct_type, 1))) {
            aot_set_last_error("llvm add array type failed");
            return false;
        }

        if (!(struct_value = LLVMConstStruct(struct_elems, 3, false))) {
            aot_set_last_error("llvm add const struct failed");
            return false;
        }

        initializer = LLVMConstArray(struct_type, &struct_value, 1);
        CHECK_LLVM_CONST(initializer);

        if (!(dtors_global = LLVMAddGlobal(comp_ctx->module, array_type,
                                           "llvm.global_dtors"))) {
            aot_set_last_error("llvm add global failed");
            return false;
        }

        LLVMSetGlobalConstant(dtors_global, true);
        LLVMSetInitializer(dtors_global, initializer);
        LLVMSetLinkage(dtors_global, LLVMAppendingLinkage);
    }

    /* Add function entry block and end block */
    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))
        || !(end_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                       "func_end"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, end_block);
    if (!LLVMBuildRetVoid(comp_ctx->builder)) {
        aot_set_last_error("llvm build ret void failed.");
        return false;
    }

    /* Build entry block */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);

    LLVMBasicBlockRef inst_not_destroyed_block;

    if (!(inst_not_destroyed_block = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func, "inst_not_destroyed"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }
    LLVMMoveBasicBlockAfter(inst_not_destroyed_block, entry_block);

    if (!(is_instance_inited = LLVMBuildLoad2(comp_ctx->builder, INT8_TYPE,
                                              is_instance_inited_global,
                                              "is_instance_inited"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }
    if (!(cmp = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, is_instance_inited,
                              I8_ONE, "cmp"))) {
        aot_set_last_error("llvm build icmp failed.");
        return false;
    }
    if (!LLVMBuildCondBr(comp_ctx->builder, cmp, inst_not_destroyed_block,
                         end_block)) {
        aot_set_last_error("llvm build condbr failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, inst_not_destroyed_block);

    if (!(check_succ_block = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func, "check_succ"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }
    LLVMMoveBasicBlockAfter(check_succ_block, entry_block);

    memory_data_global = LLVMGetNamedGlobal(comp_ctx->module, "memory_data");
    bh_assert(memory_data_global);

    if (!(memory_data = LLVMBuildLoad2(comp_ctx->builder, INT8_PTR_TYPE,
                                       memory_data_global, "memory_data"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }

    /* Check whether memory_data is allocated */
    if (!(cmp =
              LLVMBuildIsNotNull(comp_ctx->builder, memory_data, "not_null"))) {
        aot_set_last_error("llvm build is null failed.");
        return false;
    }

    if (!LLVMBuildCondBr(comp_ctx->builder, cmp, check_succ_block, end_block)) {
        aot_set_last_error("llvm build cond br failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_succ_block);

    param_types[0] = INT8_PTR_TYPE;
    if (!(func_type = LLVMFunctionType(VOID_TYPE, param_types, 1, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* Call free function */
    snprintf(func_name, sizeof(func_name), "%s", "free");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    param_values[0] = memory_data;
    if (!LLVMBuildCall2(comp_ctx->builder, func_type, func, param_values, 1,
                        "")) {
        aot_set_last_error("llvm build call failed.");
        return false;
    }

    if (!LLVMBuildStore(comp_ctx->builder, I8_ZERO,
                        is_instance_inited_global)) {
        aot_set_last_error("llvm build store failed.");
        return false;
    }

    if (!LLVMBuildBr(comp_ctx->builder, end_block)) {
        aot_set_last_error("llvm build condbr failed.");
        return false;
    }

    return true;
fail:
    return false;
}

static bool
create_wasm_instance_is_created_func(const AOTCompData *comp_data,
                                     AOTCompContext *comp_ctx)
{
    LLVMTypeRef func_type;
    LLVMValueRef func;
    LLVMValueRef is_instance_inited_global, is_instance_inited;
    LLVMBasicBlockRef entry_block;
    char func_name[32];

    is_instance_inited_global =
        LLVMGetNamedGlobal(comp_ctx->module, "is_instance_inited");
    bh_assert(is_instance_inited_global);

    if (!(func_type = LLVMFunctionType(INT8_TYPE, NULL, 0, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* Add `bool wasm_instance_is_created()` function */
    snprintf(func_name, sizeof(func_name), "%s", "wasm_instance_is_created");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    /* Add function entry block */
    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);

    if (!(is_instance_inited = LLVMBuildLoad2(comp_ctx->builder, INT8_TYPE,
                                              is_instance_inited_global,
                                              "is_instance_inited"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }

    if (!LLVMBuildRet(comp_ctx->builder, is_instance_inited)) {
        aot_set_last_error("llvm build ret void failed.");
        return false;
    }

    return true;
}

static bool
create_main_func(AOTCompData *comp_data, AOTCompContext *comp_ctx)
{
    LLVMTypeRef func_type, param_types[2] = { 0 };
    LLVMValueRef func, param_values[2] = { 0 }, ret_value = NULL;
    LLVMValueRef argc_value = NULL, argv_value = NULL;
    LLVMBasicBlockRef entry_block;
    AOTExport *aot_exports = comp_data->wasm_module->exports;
    uint32 export_count = comp_data->wasm_module->export_count;
    uint32 func_idx, i;
    AOTFuncType *aot_func_type;
    bool has_main_argc_argv = false, has_main_void = false;
    char func_name[32];

    /* Find export function "__main_argc_argv" or "__main_void" */
    for (i = 0; i < export_count; i++) {
        if (aot_exports[i].kind != EXPORT_KIND_FUNC)
            continue;

        func_idx = aot_exports[i].index - comp_data->import_func_count;
        aot_func_type = comp_data->funcs[func_idx]->func_type;

        if (!strcmp(aot_exports[i].name, "__main_argc_argv")
            && aot_func_type->param_count == 2
            && aot_func_type->result_count == 1
            && aot_func_type->types[0] == VALUE_TYPE_I32
            && aot_func_type->types[1] == VALUE_TYPE_I64
            && aot_func_type->types[2] == VALUE_TYPE_I32) {
            has_main_argc_argv = true;
            break;
        }

        if (!strcmp(aot_exports[i].name, "__main_void")
            && aot_func_type->param_count == 0
            && aot_func_type->result_count == 1
            && aot_func_type->types[0] == VALUE_TYPE_I32) {
            has_main_void = true;
            break;
        }
    }

    if (!has_main_argc_argv && !has_main_void)
        return true;

    if (has_main_argc_argv) {
        /* Add `int32 main(int, char **)` function */
        param_types[0] = I32_TYPE;
        param_types[1] = I64_TYPE;
        if (!(func_type = LLVMFunctionType(I32_TYPE, param_types, 2, false))) {
            aot_set_last_error("create LLVM function type failed.");
            return false;
        }

        snprintf(func_name, sizeof(func_name), "%s", "main");
        if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
            && !(func =
                     LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
            aot_set_last_error("add LLVM function failed.");
            return false;
        }

        argc_value = LLVMGetParam(func, 0);
        argv_value = LLVMGetParam(func, 1);
        LLVMSetValueName(argc_value, "argc");
        LLVMSetValueName(argv_value, "argv");
    }
    else {
        if (!(func_type = LLVMFunctionType(I32_TYPE, NULL, 0, false))) {
            aot_set_last_error("create LLVM function type failed.");
            return false;
        }

        snprintf(func_name, sizeof(func_name), "%s", "main");
        if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
            && !(func =
                     LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
            aot_set_last_error("add LLVM function failed.");
            return false;
        }
    }

    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }
    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);

    /* Call "wasm_instance_create" function */
    func_type = LLVMFunctionType(INT8_TYPE, NULL, 0, false);
    bh_assert(func_type);
    func = LLVMGetNamedFunction(comp_ctx->module, "wasm_instance_create");
    bh_assert(func);

    if (!LLVMBuildCall2(comp_ctx->builder, func_type, func, NULL, 0, "ret")) {
        aot_set_last_error("llvm build call failed.");
        return false;
    }

    /* Call "__main_argc_argv" or "__main_void" function */
    if (has_main_argc_argv) {
        func = LLVMGetNamedFunction(comp_ctx->module, "__main_argc_argv");
        bh_assert(func);

        param_types[0] = I32_TYPE;
        param_types[1] = I64_TYPE;
        func_type = LLVMFunctionType(I32_TYPE, param_types, 2, false);
        bh_assert(func_type);

        param_values[0] = argc_value;
        param_values[1] = argv_value;
        if (!(ret_value = LLVMBuildCall2(comp_ctx->builder, func_type, func,
                                         param_values, 2, "ret_value"))) {
            aot_set_last_error("llvm build call failed.");
            return false;
        }
    }
    else {
        func = LLVMGetNamedFunction(comp_ctx->module, "__main_void");
        bh_assert(func);

        func_type = LLVMFunctionType(I32_TYPE, NULL, 0, false);
        bh_assert(func_type);

        if (!(ret_value = LLVMBuildCall2(comp_ctx->builder, func_type, func,
                                         NULL, 0, "ret_value"))) {
            aot_set_last_error("llvm build call failed.");
            return false;
        }
    }

    /* Call "wasm_instance_destroy" function */
    func_type = LLVMFunctionType(VOID_TYPE, NULL, 0, false);
    bh_assert(func_type);
    func = LLVMGetNamedFunction(comp_ctx->module, "wasm_instance_destroy");
    bh_assert(func);

    if (!LLVMBuildCall2(comp_ctx->builder, func_type, func, NULL, 0, "")) {
        aot_set_last_error("llvm build call failed.");
        return false;
    }

    /* Return ret_value */
    if (!LLVMBuildRet(comp_ctx->builder, ret_value)) {
        aot_set_last_error("llvm build ret void failed.");
        return false;
    }

    return true;
}

static bool
create_wasm_get_exception_func(AOTCompContext *comp_ctx)
{
    LLVMTypeRef func_type;
    LLVMValueRef func, exce_id_global, exce_id_min, exce_id;
    LLVMValueRef exce_msg_idx, exce_msgs_global, exce_msg;
    LLVMBasicBlockRef entry_block;
    char func_name[32];

    if (comp_ctx->no_sandbox_mode)
        return true;

    /* Add `int32 wasm_get_exception()` function */

    if (!(func_type = LLVMFunctionType(I32_TYPE, NULL, 0, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    snprintf(func_name, sizeof(func_name), "%s", "wasm_get_exception");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);

    exce_id_global = LLVMGetNamedGlobal(comp_ctx->module, "exception_id");
    bh_assert(exce_id_global);
    if (!(exce_id = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, exce_id_global,
                                   "exce_id"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }

    if (!LLVMBuildRet(comp_ctx->builder, exce_id)) {
        aot_set_last_error("llvm build ret void failed.");
        return false;
    }

    /* Add `const char *wasm_get_exception_msg()` function */

    if (!(func_type = LLVMFunctionType(INT8_PTR_TYPE, NULL, 0, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    snprintf(func_name, sizeof(func_name), "%s", "wasm_get_exception_msg");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);

    exce_id_global = LLVMGetNamedGlobal(comp_ctx->module, "exception_id");
    bh_assert(exce_id_global);
    if (!(exce_id = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, exce_id_global,
                                   "exce_id"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }

    exce_id_min = I32_CONST(EXCE_ID_MIN);
    CHECK_LLVM_CONST(exce_id_min);
    if (!(exce_msg_idx = LLVMBuildSub(comp_ctx->builder, exce_id, exce_id_min,
                                      "exce_msg_idx"))) {
        aot_set_last_error("llvm build add failed.");
        return false;
    }

    exce_msgs_global = LLVMGetNamedGlobal(comp_ctx->module, "exception_msgs");
    bh_assert(exce_msgs_global);
    if (!(exce_msg = LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_PTR_TYPE,
                                           exce_msgs_global, &exce_msg_idx, 1,
                                           "exce_msg_addr"))
        || !(exce_msg = LLVMBuildLoad2(comp_ctx->builder, INT8_PTR_TYPE,
                                       exce_msg, "exce_msg"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }

    if (!LLVMBuildRet(comp_ctx->builder, exce_msg)) {
        aot_set_last_error("llvm build store failed.");
        return false;
    }

    return true;
fail:
    return false;
}

static bool
create_wasm_set_exception_func(AOTCompContext *comp_ctx)
{
    LLVMTypeRef func_type, param_types[2];
    LLVMValueRef func, func_arg0, exce_id_global, cmp, i32_const;
    LLVMBasicBlockRef entry_block, check_zero_succ, check_zero_fail;
    LLVMBasicBlockRef check_min_succ, check_max_succ, check_fail;
    char func_name[32];

    if (comp_ctx->no_sandbox_mode)
        return true;

    param_types[0] = I32_TYPE;
    if (!(func_type = LLVMFunctionType(VOID_TYPE, param_types, 1, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* Add `void wasm_set_exception(int32 exception_id)` function */
    snprintf(func_name, sizeof(func_name), "%s", "wasm_set_exception");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }
    func_arg0 = LLVMGetParam(func, 0);
    LLVMSetValueName(func_arg0, "exception_id");

    /* Add function entry block and end block */
    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))
        || !(check_zero_succ = LLVMAppendBasicBlockInContext(
                 comp_ctx->context, func, "check_zero_succ"))
        || !(check_zero_fail = LLVMAppendBasicBlockInContext(
                 comp_ctx->context, func, "check_zero_fail"))
        || !(check_min_succ = LLVMAppendBasicBlockInContext(
                 comp_ctx->context, func, "check_min_succ"))
        || !(check_max_succ = LLVMAppendBasicBlockInContext(
                 comp_ctx->context, func, "check_max_succ"))
        || !(check_fail = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                        "check_fail"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }
    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);

    if (!(cmp = LLVMBuildICmp(comp_ctx->builder, LLVMIntEQ, func_arg0, I32_ZERO,
                              "cmp"))) {
        aot_set_last_error("llvm build icmp failed.");
        return false;
    }
    if (!LLVMBuildCondBr(comp_ctx->builder, cmp, check_zero_succ,
                         check_zero_fail)) {
        aot_set_last_error("llvm build condbr failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_zero_succ);

    exce_id_global = LLVMGetNamedGlobal(comp_ctx->module, "exception_id");
    bh_assert(exce_id_global);
    if (!LLVMBuildStore(comp_ctx->builder, func_arg0, exce_id_global)) {
        aot_set_last_error("llvm build store failed.");
        return false;
    }

    if (!LLVMBuildRetVoid(comp_ctx->builder)) {
        aot_set_last_error("llvm build ret void failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_zero_fail);

    i32_const = I32_CONST(EXCE_ID_MIN);
    CHECK_LLVM_CONST(i32_const);
    if (!(cmp = LLVMBuildICmp(comp_ctx->builder, LLVMIntUGE, func_arg0,
                              i32_const, "cmp"))) {
        aot_set_last_error("llvm build icmp failed.");
        return false;
    }
    if (!LLVMBuildCondBr(comp_ctx->builder, cmp, check_min_succ, check_fail)) {
        aot_set_last_error("llvm build condbr failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_min_succ);

    i32_const = I32_CONST(EXCE_ID_MAX);
    CHECK_LLVM_CONST(i32_const);
    if (!(cmp = LLVMBuildICmp(comp_ctx->builder, LLVMIntULE, func_arg0,
                              i32_const, "cmp"))) {
        aot_set_last_error("llvm build icmp failed.");
        return false;
    }
    if (!LLVMBuildCondBr(comp_ctx->builder, cmp, check_max_succ, check_fail)) {
        aot_set_last_error("llvm build condbr failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_max_succ);

    exce_id_global = LLVMGetNamedGlobal(comp_ctx->module, "exception_id");
    bh_assert(exce_id_global);
    if (!LLVMBuildStore(comp_ctx->builder, func_arg0, exce_id_global)) {
        aot_set_last_error("llvm build store failed.");
        return false;
    }

    if (!LLVMBuildRetVoid(comp_ctx->builder)) {
        aot_set_last_error("llvm build ret void failed.");
        return false;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, check_fail);

    exce_id_global = LLVMGetNamedGlobal(comp_ctx->module, "exception_id");
    bh_assert(exce_id_global);

    i32_const = I32_CONST(EXCE_UNKNOWN_ERROR);
    CHECK_LLVM_CONST(i32_const);
    if (!LLVMBuildStore(comp_ctx->builder, i32_const, exce_id_global)) {
        aot_set_last_error("llvm build store failed.");
        return false;
    }

    if (!LLVMBuildRetVoid(comp_ctx->builder)) {
        aot_set_last_error("llvm build ret void failed.");
        return false;
    }

    return true;
fail:
    return false;
}

static bool
create_wasm_get_memory_func(const AOTCompData *comp_data,
                            AOTCompContext *comp_ctx)
{
    LLVMTypeRef func_type;
    LLVMValueRef func, memory_data, memory_data_global;
    LLVMBasicBlockRef entry_block;
    char func_name[32];

    if (comp_ctx->no_sandbox_mode)
        return true;

    if (!(func_type = LLVMFunctionType(INT8_PTR_TYPE, NULL, 0, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* Add `int8 *wasm_get_memory()` function */
    snprintf(func_name, sizeof(func_name), "%s", "wasm_get_memory");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    /* Add function entry block */
    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }

    /* Build entry block */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);
    memory_data_global = LLVMGetNamedGlobal(comp_ctx->module, "memory_data");
    bh_assert(memory_data_global);

    if (!(memory_data = LLVMBuildLoad2(comp_ctx->builder, INT8_PTR_TYPE,
                                       memory_data_global, "memory_data"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }

    if (!LLVMBuildRet(comp_ctx->builder, memory_data)) {
        aot_set_last_error("llvm build ret failed.");
        return false;
    }

    return true;
}

static bool
create_wasm_get_memory_size_func(const AOTCompData *comp_data,
                                 AOTCompContext *comp_ctx)
{
    LLVMTypeRef func_type;
    LLVMValueRef func, memory_data_size, memory_data_size_global;
    LLVMBasicBlockRef entry_block;
    char func_name[32];

    if (comp_ctx->no_sandbox_mode)
        return true;

    if (!(func_type = LLVMFunctionType(I64_TYPE, NULL, 0, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* Add `uint64 wasm_get_memory_size()` function */
    snprintf(func_name, sizeof(func_name), "%s", "wasm_get_memory_size");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    /* Add function entry block */
    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }

    /* Build entry block */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);
    memory_data_size_global =
        LLVMGetNamedGlobal(comp_ctx->module, "memory_data_size");
    bh_assert(memory_data_size_global);

    if (!(memory_data_size =
              LLVMBuildLoad2(comp_ctx->builder, I64_TYPE,
                             memory_data_size_global, "memory_size"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }

    if (!LLVMBuildRet(comp_ctx->builder, memory_data_size)) {
        aot_set_last_error("llvm build ret failed.");
        return false;
    }

    return true;
}

static bool
create_wasm_get_heap_handle_func(const AOTCompData *comp_data,
                                 AOTCompContext *comp_ctx)
{
    LLVMTypeRef func_type;
    LLVMValueRef func, heap_handle, heap_handle_global;
    LLVMBasicBlockRef entry_block;
    char func_name[48];

    if (comp_ctx->no_sandbox_mode)
        return true;

    if (!(func_type = LLVMFunctionType(INT8_PTR_TYPE, NULL, 0, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    /* Add `int8 *wasm_get_heap_handle()` function */
    snprintf(func_name, sizeof(func_name), "%s", "wasm_get_heap_handle");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    /* Add function entry block */
    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))) {
        aot_set_last_error("add LLVM basic block failed.");
        return false;
    }

    /* Build entry block */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);
    heap_handle_global =
        LLVMGetNamedGlobal(comp_ctx->module, "host_managed_heap_handle");
    bh_assert(heap_handle_global);

    if (!(heap_handle = LLVMBuildLoad2(comp_ctx->builder, INT8_PTR_TYPE,
                                       heap_handle_global, "heap_handle"))) {
        aot_set_last_error("llvm build load failed.");
        return false;
    }

    if (!LLVMBuildRet(comp_ctx->builder, heap_handle)) {
        aot_set_last_error("llvm build ret failed.");
        return false;
    }

    return true;
}

static bool
create_wasm_get_export_apis_func(const AOTCompData *comp_data,
                                 AOTCompContext *comp_ctx)
{
    LLVMTypeRef elem_types[3], struct_type, array_type, llvm_func_type;
    LLVMValueRef *values = NULL, fields[3], struct_value, ret_value;
    LLVMValueRef func, initializer, exported_apis_global;
    LLVMBasicBlockRef entry_block;
    AOTExport *aot_exports = comp_data->wasm_module->exports;
    uint32 export_count = comp_data->wasm_module->export_count;
    uint32 export_func_count = 0, func_idx, i, j, k;
    uint64 total_size;
    AOTFuncType *func_type;
    char func_name[256], signature[128], buf[32], *p;

    if (comp_ctx->no_sandbox_mode)
        return true;

    for (i = 0; i < export_count; i++) {
        if (aot_exports[i].kind == EXPORT_KIND_FUNC)
            export_func_count++;
    }

    if (export_func_count > 0) {
        elem_types[0] = INT8_PTR_TYPE; /* func_name */
        elem_types[1] = INT8_PTR_TYPE; /* signature */
        elem_types[2] = INT8_PTR_TYPE; /* func_ptr */

        if (!(struct_type = LLVMStructType(elem_types, 3, false))) {
            aot_set_last_error("llvm add struct type failed.");
            return false;
        }
        if (!(array_type = LLVMArrayType(struct_type, export_func_count))) {
            aot_set_last_error("llvm add array type failed.");
            return false;
        }

        total_size = (uint64)sizeof(LLVMValueRef) * export_func_count;
        if (!(values = wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory failed");
            return false;
        }
        memset(values, 0, (uint32)total_size);

        j = 0;
        for (i = 0; i < export_count; i++) {
            if (aot_exports[i].kind == EXPORT_KIND_FUNC) {
                snprintf(buf, sizeof(buf), "%s%u", "export_func#", j);
                if (!(fields[0] = LLVMBuildGlobalStringPtr(
                          comp_ctx->builder, aot_exports[i].name, buf))) {
                    aot_set_last_error("llvm create global string failed.");
                    goto fail;
                }

                bh_assert(aot_exports[i].index >= comp_data->import_func_count);
                func_idx = aot_exports[i].index - comp_data->import_func_count;

                func_type = comp_data->funcs[func_idx]->func_type;
                p = signature;
                *p++ = '(';
                for (k = 0;
                     k < func_type->param_count + func_type->result_count;
                     k++) {
                    if (k == func_type->param_count)
                        *p++ = ')';
                    switch (func_type->types[k]) {
                        case VALUE_TYPE_I32:
                            *p++ = 'i';
                            break;
                        case VALUE_TYPE_I64:
                            *p++ = 'I';
                            break;
                        case VALUE_TYPE_F32:
                            *p++ = 'f';
                            break;
                        case VALUE_TYPE_F64:
                            *p++ = 'F';
                            break;
                        default:
                            bh_assert(0);
                            break;
                    }
                }
                if (!func_type->result_count)
                    *p++ = ')';
                *p++ = '\0';

                snprintf(buf, sizeof(buf), "%s%u", "signature#", j);
                if (!(fields[1] = LLVMBuildGlobalStringPtr(comp_ctx->builder,
                                                           signature, buf))) {
                    aot_set_last_error("llvm create global string failed.");
                    goto fail;
                }
                fields[2] = comp_ctx->func_ctxes[func_idx]->func;

                if (!(struct_value = LLVMConstStruct(fields, 3, false))) {
                    aot_set_last_error("llvm create struct failed.");
                    goto fail;
                }

                values[j] = struct_value;
                j++;
            }
        }
        bh_assert(j == export_func_count);
        /* TODO: sort the array with name */

        if (!(initializer =
                  LLVMConstArray(struct_type, values, export_func_count))) {
            aot_set_last_error("llvm create array failed.");
            goto fail;
        }
        if (!create_wasm_global(comp_ctx, array_type, "exported_apis",
                                initializer, true)) {
            goto fail;
        }

        wasm_runtime_free(values);
        values = NULL;
    }

    if (!(llvm_func_type = LLVMFunctionType(INT8_PTR_TYPE, NULL, 0, false))) {
        aot_set_last_error("create LLVM function type failed.");
        goto fail;
    }

    /* Add `void *wasm_get_export_apis()` function */
    snprintf(func_name, sizeof(func_name), "%s", "wasm_get_export_apis");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name,
                                    llvm_func_type))) {
        aot_set_last_error("add LLVM function failed.");
        goto fail;
    }

    /* Add function entry block */
    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))) {
        aot_set_last_error("add LLVM basic block failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);

    if (export_func_count > 0) {
        exported_apis_global =
            LLVMGetNamedGlobal(comp_ctx->module, "exported_apis");
        bh_assert(exported_apis_global);

        if (!(ret_value =
                  LLVMBuildBitCast(comp_ctx->builder, exported_apis_global,
                                   INT8_PTR_TYPE, "exported_apis_ret"))) {
            aot_set_last_error("llvm build bit cast failed");
            goto fail;
        }
        if (!LLVMBuildRet(comp_ctx->builder, ret_value)) {
            aot_set_last_error("llvm build ret failed");
            goto fail;
        }
    }
    else {
        ret_value = LLVMConstNull(INT8_PTR_TYPE);
        CHECK_LLVM_CONST(ret_value);
        if (!LLVMBuildRet(comp_ctx->builder, ret_value)) {
            aot_set_last_error("llvm build ret failed");
            goto fail;
        }
    }

    if (!(llvm_func_type = LLVMFunctionType(I32_TYPE, NULL, 0, false))) {
        aot_set_last_error("create LLVM function type failed.");
        goto fail;
    }

    /* Add `void *wasm_get_export_api_num()` function */
    snprintf(func_name, sizeof(func_name), "%s", "wasm_get_export_api_num");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name,
                                    llvm_func_type))) {
        aot_set_last_error("add LLVM function failed.");
        goto fail;
    }

    /* Add function entry block */
    if (!(entry_block = LLVMAppendBasicBlockInContext(comp_ctx->context, func,
                                                      "func_begin"))) {
        aot_set_last_error("add LLVM basic block failed.");
        goto fail;
    }

    LLVMPositionBuilderAtEnd(comp_ctx->builder, entry_block);

    ret_value = I32_CONST(export_func_count);
    CHECK_LLVM_CONST(ret_value);

    if (!LLVMBuildRet(comp_ctx->builder, ret_value)) {
        aot_set_last_error("llvm build ret failed");
        goto fail;
    }

    return true;
fail:
    if (values)
        wasm_runtime_free(values);
    return false;
}

static LLVMValueRef
add_llvm_func(const AOTCompContext *comp_ctx, LLVMModuleRef module,
              uint32 func_index, uint32 param_count, LLVMTypeRef func_type,
              const char *prefix)
{
    WASMModule *wasm_module = comp_ctx->comp_data->wasm_module;
    WASMFunction *wasm_func = wasm_module->functions[func_index];
    char buf[48];
    char *func_name = NULL;
    LLVMValueRef func;
    uint32 i;

    /* Lookup func's export name first */
    for (i = 0; i < wasm_module->export_count; i++) {
        WASMExport *export = wasm_module->exports + i;
        if (export->kind == EXPORT_KIND_FUNC
            && export->index
                   == wasm_module->import_function_count + func_index) {
            func_name = export->name;
            break;
        }
    }

    /* If not found, try to use name from custom name section */
    if (!func_name && wasm_func->name && strlen(wasm_func->name))
        func_name = wasm_func->name;

    if (func_name) {
        /* Avoid calling to these memcpy/memset functions from the
           code generated by LLVMBuildMemCpy and LLVMBuildMemSet */
        if (!strcmp(func_name, "memcpy"))
            func_name = "__aot_memcpy";
        else if (!strcmp(func_name, "memset"))
            func_name = "__aot_memset";
        /* Avoid calling these malloc/free functions from the
           wasm_instance_create/wasm_instance_destroy functions */
        if (!strcmp(func_name, "malloc"))
            func_name = "__aot_malloc";
        else if (!strcmp(func_name, "free"))
            func_name = "__aot_free";
    }

    if (!func_name) {
        snprintf(buf, sizeof(buf), "%s%d", prefix, func_index);
        func_name = buf;
    }

    /* Add LLVM function */
    if (!(func = LLVMAddFunction(module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return NULL;
    }

    return func;
}

/**
 * Add LLVM function
 */
static LLVMValueRef
aot_add_llvm_func(AOTCompContext *comp_ctx, LLVMModuleRef module,
                  const AOTFuncType *aot_func_type, uint32 func_index,
                  LLVMTypeRef *p_func_type)
{
    LLVMValueRef func = NULL;
    LLVMTypeRef *param_types = NULL, ret_type, func_type;
    const char *prefix = AOT_FUNC_PREFIX;
    uint32 i, j = 0, param_count = (uint64)aot_func_type->param_count;
    uint64 size;

    /* Extra wasm function results(except the first one)'s address are
     * appended to aot function parameters. */
    if (aot_func_type->result_count > 1)
        param_count += aot_func_type->result_count - 1;

    if (param_count) {
        /* Initialize parameter types of the LLVM function */
        size = sizeof(LLVMTypeRef) * ((uint64)param_count);
        if (size >= UINT32_MAX
            || !(param_types = wasm_runtime_malloc((uint32)size))) {
            aot_set_last_error("allocate memory failed.");
            return NULL;
        }

        for (i = 0; i < aot_func_type->param_count; i++)
            param_types[j++] = TO_LLVM_TYPE(aot_func_type->types[i]);
        /* Extra results' address */
        for (i = 1; i < aot_func_type->result_count; i++, j++) {
            param_types[j] = TO_LLVM_TYPE(
                aot_func_type->types[aot_func_type->param_count + i]);
            if (!(param_types[j] = LLVMPointerType(param_types[j], 0))) {
                aot_set_last_error("llvm get pointer type failed.");
                goto fail;
            }
        }
    }

    /* Resolve return type of the LLVM function */
    if (aot_func_type->result_count)
        ret_type =
            TO_LLVM_TYPE(aot_func_type->types[aot_func_type->param_count]);
    else
        ret_type = VOID_TYPE;

    /* Resolve function prototype */
    if (!(func_type =
              LLVMFunctionType(ret_type, param_types, param_count, false))) {
        aot_set_last_error("create LLVM function type failed.");
        goto fail;
    }

    bh_assert(func_index < comp_ctx->func_ctx_count);
    bh_assert(LLVMGetReturnType(func_type) == ret_type);
    if (!(func = add_llvm_func(comp_ctx, module, func_index,
                               aot_func_type->param_count, func_type, prefix)))
        goto fail;

    if (p_func_type)
        *p_func_type = func_type;

fail:
    if (param_types)
        wasm_runtime_free(param_types);
    return func;
}

static void
free_block_memory(AOTBlock *block)
{
    if (block->param_types)
        wasm_runtime_free(block->param_types);
    if (block->result_types)
        wasm_runtime_free(block->result_types);
    wasm_runtime_free(block);
}

/**
 * Create first AOTBlock, or function block for the function
 */
static AOTBlock *
aot_create_func_block(const AOTCompContext *comp_ctx,
                      const AOTFuncContext *func_ctx, const AOTFunc *func,
                      const AOTFuncType *aot_func_type)
{
    AOTBlock *aot_block;
    uint32 param_count = aot_func_type->param_count,
           result_count = aot_func_type->result_count;

    /* Allocate memory */
    if (!(aot_block = wasm_runtime_malloc(sizeof(AOTBlock)))) {
        aot_set_last_error("allocate memory failed.");
        return NULL;
    }
    memset(aot_block, 0, sizeof(AOTBlock));
    if (param_count
        && !(aot_block->param_types = wasm_runtime_malloc(param_count))) {
        aot_set_last_error("allocate memory failed.");
        goto fail;
    }
    if (result_count) {
        if (!(aot_block->result_types = wasm_runtime_malloc(result_count))) {
            aot_set_last_error("allocate memory failed.");
            goto fail;
        }
    }

    /* Set block data */
    aot_block->label_type = LABEL_TYPE_FUNCTION;
    aot_block->param_count = param_count;
    if (param_count) {
        bh_memcpy_s(aot_block->param_types, param_count, aot_func_type->types,
                    param_count);
    }
    aot_block->result_count = result_count;
    if (result_count) {
        bh_memcpy_s(aot_block->result_types, result_count,
                    aot_func_type->types + param_count, result_count);
    }
    aot_block->wasm_code_end = func->code + func->code_size;

    /* Add function entry block */
    if (!(aot_block->llvm_entry_block = LLVMAppendBasicBlockInContext(
              comp_ctx->context, func_ctx->func, "func_begin"))) {
        aot_set_last_error("add LLVM basic block failed.");
        goto fail;
    }

    return aot_block;

fail:
    free_block_memory(aot_block);
    return NULL;
}

static bool
create_local_variables(const AOTCompData *comp_data,
                       const AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                       const AOTFunc *func)
{
    AOTFuncType *aot_func_type = comp_data->func_types[func->func_type_index];
    char local_name[32];
    uint32 i;

    for (i = 0; i < aot_func_type->param_count; i++) {
        snprintf(local_name, sizeof(local_name), "l%d", i);
        func_ctx->locals[i] =
            LLVMBuildAlloca(comp_ctx->builder,
                            TO_LLVM_TYPE(aot_func_type->types[i]), local_name);
        if (!func_ctx->locals[i]) {
            aot_set_last_error("llvm build alloca failed.");
            return false;
        }
        if (!LLVMBuildStore(comp_ctx->builder, LLVMGetParam(func_ctx->func, i),
                            func_ctx->locals[i])) {
            aot_set_last_error("llvm build store failed.");
            return false;
        }
    }

    for (i = 0; i < func->local_count; i++) {
        LLVMTypeRef local_type;
        LLVMValueRef local_value = NULL;
        snprintf(local_name, sizeof(local_name), "l%d",
                 aot_func_type->param_count + i);
        local_type = TO_LLVM_TYPE(func->local_types[i]);
        func_ctx->locals[aot_func_type->param_count + i] =
            LLVMBuildAlloca(comp_ctx->builder, local_type, local_name);
        if (!func_ctx->locals[aot_func_type->param_count + i]) {
            aot_set_last_error("llvm build alloca failed.");
            return false;
        }
        switch (func->local_types[i]) {
            case VALUE_TYPE_I32:
                local_value = I32_ZERO;
                break;
            case VALUE_TYPE_I64:
                local_value = I64_ZERO;
                break;
            case VALUE_TYPE_F32:
                local_value = F32_ZERO;
                break;
            case VALUE_TYPE_F64:
                local_value = F64_ZERO;
                break;
            case VALUE_TYPE_V128:
                local_value = V128_i64x2_ZERO;
                break;
            default:
                bh_assert(0);
                break;
        }
        if (!LLVMBuildStore(comp_ctx->builder, local_value,
                            func_ctx->locals[aot_func_type->param_count + i])) {
            aot_set_last_error("llvm build store failed.");
            return false;
        }
    }

    return true;
}

/**
 * Create function compiler context
 */
static AOTFuncContext *
aot_create_func_context(const AOTCompData *comp_data, AOTCompContext *comp_ctx,
                        AOTFunc *func, uint32 func_index)
{
    AOTFuncContext *func_ctx;
    AOTFuncType *aot_func_type = comp_data->func_types[func->func_type_index];
    AOTBlock *aot_block;
    uint64 size;

    /* Allocate memory for the function context */
    size = offsetof(AOTFuncContext, locals)
           + sizeof(LLVMValueRef)
                 * ((uint64)aot_func_type->param_count + func->local_count);
    if (size >= UINT32_MAX || !(func_ctx = wasm_runtime_malloc((uint32)size))) {
        aot_set_last_error("allocate memory failed.");
        return NULL;
    }

    memset(func_ctx, 0, (uint32)size);
    func_ctx->aot_func = func;

    func_ctx->module = comp_ctx->module;

    /* Add LLVM function */
    if (!(func_ctx->func =
              aot_add_llvm_func(comp_ctx, func_ctx->module, aot_func_type,
                                func_index, &func_ctx->func_type))) {
        goto fail;
    }

    /* Create function's first AOTBlock */
    if (!(aot_block =
              aot_create_func_block(comp_ctx, func_ctx, func, aot_func_type))) {
        goto fail;
    }

    aot_block_stack_push(&func_ctx->block_stack, aot_block);

    /* Add local variables */
    LLVMPositionBuilderAtEnd(comp_ctx->builder, aot_block->llvm_entry_block);

    /* Create local variables */
    if (!create_local_variables(comp_data, comp_ctx, func_ctx, func)) {
        goto fail;
    }

    return func_ctx;

fail:
    aot_block_stack_destroy(&func_ctx->block_stack);
    wasm_runtime_free(func_ctx);
    return NULL;
}

static void
aot_destroy_func_contexts(AOTFuncContext **func_ctxes, uint32 count)
{
    uint32 i;

    for (i = 0; i < count; i++) {
        if (func_ctxes[i]) {
            aot_block_stack_destroy(&func_ctxes[i]->block_stack);
            wasm_runtime_free(func_ctxes[i]);
        }
    }
    wasm_runtime_free(func_ctxes);
}

/**
 * Create function compiler contexts
 */
static AOTFuncContext **
aot_create_func_contexts(const AOTCompData *comp_data, AOTCompContext *comp_ctx)
{
    AOTFuncContext **func_ctxes;
    uint64 size;
    uint32 i;

    /* Allocate memory */
    size = sizeof(AOTFuncContext *) * (uint64)comp_data->func_count;
    if (size >= UINT32_MAX
        || !(func_ctxes = wasm_runtime_malloc((uint32)size))) {
        aot_set_last_error("allocate memory failed.");
        return NULL;
    }

    memset(func_ctxes, 0, size);

    /* Create each function context */
    for (i = 0; i < comp_data->func_count; i++) {
        AOTFunc *func = comp_data->funcs[i];
        if (!(func_ctxes[i] =
                  aot_create_func_context(comp_data, comp_ctx, func, i))) {
            aot_destroy_func_contexts(func_ctxes, comp_data->func_count);
            return NULL;
        }
    }

    return func_ctxes;
}

static bool
aot_set_llvm_basic_types(AOTLLVMTypes *basic_types, LLVMContextRef context)
{
    basic_types->int1_type = LLVMInt1TypeInContext(context);
    basic_types->int8_type = LLVMInt8TypeInContext(context);
    basic_types->int16_type = LLVMInt16TypeInContext(context);
    basic_types->int32_type = LLVMInt32TypeInContext(context);
    basic_types->int64_type = LLVMInt64TypeInContext(context);
    basic_types->float32_type = LLVMFloatTypeInContext(context);
    basic_types->float64_type = LLVMDoubleTypeInContext(context);
    basic_types->void_type = LLVMVoidTypeInContext(context);

    basic_types->meta_data_type = LLVMMetadataTypeInContext(context);

    basic_types->int8_ptr_type = LLVMPointerType(basic_types->int8_type, 0);

    if (basic_types->int8_ptr_type) {
        basic_types->int8_pptr_type =
            LLVMPointerType(basic_types->int8_ptr_type, 0);
    }

    basic_types->int16_ptr_type = LLVMPointerType(basic_types->int16_type, 0);
    basic_types->int32_ptr_type = LLVMPointerType(basic_types->int32_type, 0);
    basic_types->int64_ptr_type = LLVMPointerType(basic_types->int64_type, 0);
    basic_types->float32_ptr_type =
        LLVMPointerType(basic_types->float32_type, 0);
    basic_types->float64_ptr_type =
        LLVMPointerType(basic_types->float64_type, 0);

    basic_types->i8x16_vec_type = LLVMVectorType(basic_types->int8_type, 16);
    basic_types->i16x8_vec_type = LLVMVectorType(basic_types->int16_type, 8);
    basic_types->i32x4_vec_type = LLVMVectorType(basic_types->int32_type, 4);
    basic_types->i64x2_vec_type = LLVMVectorType(basic_types->int64_type, 2);
    basic_types->f32x4_vec_type = LLVMVectorType(basic_types->float32_type, 4);
    basic_types->f64x2_vec_type = LLVMVectorType(basic_types->float64_type, 2);

    basic_types->v128_type = basic_types->i64x2_vec_type;
    basic_types->v128_ptr_type = LLVMPointerType(basic_types->v128_type, 0);

    basic_types->i1x2_vec_type = LLVMVectorType(basic_types->int1_type, 2);

    return (basic_types->int8_ptr_type && basic_types->int8_pptr_type
            && basic_types->int16_ptr_type && basic_types->int32_ptr_type
            && basic_types->int64_ptr_type && basic_types->float32_ptr_type
            && basic_types->float64_ptr_type && basic_types->i8x16_vec_type
            && basic_types->i16x8_vec_type && basic_types->i32x4_vec_type
            && basic_types->i64x2_vec_type && basic_types->f32x4_vec_type
            && basic_types->f64x2_vec_type && basic_types->i1x2_vec_type
            && basic_types->meta_data_type)
               ? true
               : false;
}

static bool
aot_create_llvm_consts(AOTLLVMConsts *consts, AOTCompContext *comp_ctx)
{
#define CREATE_I1_CONST(name, value)                                       \
    if (!(consts->i1_##name =                                              \
              LLVMConstInt(comp_ctx->basic_types.int1_type, value, true))) \
        return false;

    CREATE_I1_CONST(zero, 0)
    CREATE_I1_CONST(one, 1)
#undef CREATE_I1_CONST

    if (!(consts->i8_zero = I8_CONST(0)))
        return false;

    if (!(consts->i8_one = I8_CONST(1)))
        return false;

    if (!(consts->f32_zero = F32_CONST(0)))
        return false;

    if (!(consts->f64_zero = F64_CONST(0)))
        return false;

#define CREATE_I32_CONST(name, value)                                \
    if (!(consts->i32_##name = LLVMConstInt(I32_TYPE, value, true))) \
        return false;

    CREATE_I32_CONST(min, (uint32)INT32_MIN)
    CREATE_I32_CONST(neg_one, (uint32)-1)
    CREATE_I32_CONST(zero, 0)
    CREATE_I32_CONST(one, 1)
    CREATE_I32_CONST(two, 2)
    CREATE_I32_CONST(three, 3)
    CREATE_I32_CONST(four, 4)
    CREATE_I32_CONST(five, 5)
    CREATE_I32_CONST(six, 6)
    CREATE_I32_CONST(seven, 7)
    CREATE_I32_CONST(eight, 8)
    CREATE_I32_CONST(nine, 9)
    CREATE_I32_CONST(ten, 10)
    CREATE_I32_CONST(eleven, 11)
    CREATE_I32_CONST(twelve, 12)
    CREATE_I32_CONST(thirteen, 13)
    CREATE_I32_CONST(fourteen, 14)
    CREATE_I32_CONST(fifteen, 15)
    CREATE_I32_CONST(31, 31)
    CREATE_I32_CONST(32, 32)
#undef CREATE_I32_CONST

#define CREATE_I64_CONST(name, value)                                \
    if (!(consts->i64_##name = LLVMConstInt(I64_TYPE, value, true))) \
        return false;

    CREATE_I64_CONST(min, (uint64)INT64_MIN)
    CREATE_I64_CONST(neg_one, (uint64)-1)
    CREATE_I64_CONST(zero, 0)
    CREATE_I64_CONST(63, 63)
    CREATE_I64_CONST(64, 64)
#undef CREATE_I64_CONST

#define CREATE_V128_CONST(name, type)                     \
    if (!(consts->name##_vec_zero = LLVMConstNull(type))) \
        return false;                                     \
    if (!(consts->name##_undef = LLVMGetUndef(type)))     \
        return false;

    CREATE_V128_CONST(i8x16, V128_i8x16_TYPE)
    CREATE_V128_CONST(i16x8, V128_i16x8_TYPE)
    CREATE_V128_CONST(i32x4, V128_i32x4_TYPE)
    CREATE_V128_CONST(i64x2, V128_i64x2_TYPE)
    CREATE_V128_CONST(f32x4, V128_f32x4_TYPE)
    CREATE_V128_CONST(f64x2, V128_f64x2_TYPE)
#undef CREATE_V128_CONST

#define CREATE_VEC_ZERO_MASK(slot)                                       \
    {                                                                    \
        LLVMTypeRef type = LLVMVectorType(I32_TYPE, slot);               \
        if (!type || !(consts->i32x##slot##_zero = LLVMConstNull(type))) \
            return false;                                                \
    }

    CREATE_VEC_ZERO_MASK(16)
    CREATE_VEC_ZERO_MASK(8)
    CREATE_VEC_ZERO_MASK(4)
    CREATE_VEC_ZERO_MASK(2)
#undef CREATE_VEC_ZERO_MASK

    return true;
}

typedef struct ArchItem {
    char *arch;
    bool support_eb;
} ArchItem;

/* clang-format off */
static ArchItem valid_archs[] = {
    { "x86_64", false },
    { "i386", false },
    { "xtensa", false },
    { "mips", true },
    { "mipsel", false },
    { "aarch64v8", false },
    { "aarch64v8.1", false },
    { "aarch64v8.2", false },
    { "aarch64v8.3", false },
    { "aarch64v8.4", false },
    { "aarch64v8.5", false },
    { "aarch64_bev8", false }, /* big endian */
    { "aarch64_bev8.1", false },
    { "aarch64_bev8.2", false },
    { "aarch64_bev8.3", false },
    { "aarch64_bev8.4", false },
    { "aarch64_bev8.5", false },
    { "armv4", true },
    { "armv4t", true },
    { "armv5t", true },
    { "armv5te", true },
    { "armv5tej", true },
    { "armv6", true },
    { "armv6kz", true },
    { "armv6t2", true },
    { "armv6k", true },
    { "armv7", true },
    { "armv6m", true },
    { "armv6sm", true },
    { "armv7em", true },
    { "armv8a", true },
    { "armv8r", true },
    { "armv8m.base", true },
    { "armv8m.main", true },
    { "armv8.1m.main", true },
    { "thumbv4", true },
    { "thumbv4t", true },
    { "thumbv5t", true },
    { "thumbv5te", true },
    { "thumbv5tej", true },
    { "thumbv6", true },
    { "thumbv6kz", true },
    { "thumbv6t2", true },
    { "thumbv6k", true },
    { "thumbv7", true },
    { "thumbv6m", true },
    { "thumbv6sm", true },
    { "thumbv7em", true },
    { "thumbv8a", true },
    { "thumbv8r", true },
    { "thumbv8m.base", true },
    { "thumbv8m.main", true },
    { "thumbv8.1m.main", true },
    { "riscv32", true },
    { "riscv64", true },
};

static const char *valid_abis[] = {
    "gnu",
    "eabi",
    "eabihf",
    "gnueabihf",
    "msvc",
    "ilp32",
    "ilp32f",
    "ilp32d",
    "lp64",
    "lp64f",
    "lp64d"
};
/* clang-format on */

static void
print_supported_targets()
{
    uint32 i;
    const char *target_name;

    os_printf("Supported targets:\n");
    /* over the list of all available targets */
    for (LLVMTargetRef target = LLVMGetFirstTarget(); target != NULL;
         target = LLVMGetNextTarget(target)) {
        target_name = LLVMGetTargetName(target);
        /* Skip mipsel, aarch64_be since prefix mips, aarch64 will cover them */
        if (strcmp(target_name, "mipsel") == 0)
            continue;
        else if (strcmp(target_name, "aarch64_be") == 0)
            continue;

        if (strcmp(target_name, "x86-64") == 0)
            os_printf("  x86_64\n");
        else if (strcmp(target_name, "x86") == 0)
            os_printf("  i386\n");
        else {
            for (i = 0; i < sizeof(valid_archs) / sizeof(ArchItem); i++) {
                /* If target_name is prefix for valid_archs[i].arch */
                if ((strncmp(target_name, valid_archs[i].arch,
                             strlen(target_name))
                     == 0))
                    os_printf("  %s\n", valid_archs[i].arch);
            }
        }
    }
}

static void
print_supported_abis()
{
    uint32 i;
    os_printf("Supported ABI: ");
    for (i = 0; i < sizeof(valid_abis) / sizeof(const char *); i++)
        os_printf("%s ", valid_abis[i]);
    os_printf("\n");
}

static bool
check_target_arch(const char *target_arch)
{
    uint32 i;
    char *arch;
    bool support_eb;

    for (i = 0; i < sizeof(valid_archs) / sizeof(ArchItem); i++) {
        arch = valid_archs[i].arch;
        support_eb = valid_archs[i].support_eb;

        if (!strncmp(target_arch, arch, strlen(arch))
            && ((support_eb
                 && (!strcmp(target_arch + strlen(arch), "eb")
                     || !strcmp(target_arch + strlen(arch), "")))
                || (!support_eb && !strcmp(target_arch + strlen(arch), "")))) {
            return true;
        }
    }
    return false;
}

static bool
check_target_abi(const char *target_abi)
{
    uint32 i;
    for (i = 0; i < sizeof(valid_abis) / sizeof(char *); i++) {
        if (!strcmp(target_abi, valid_abis[i]))
            return true;
    }
    return false;
}

static void
get_target_arch_from_triple(const char *triple, char *arch_buf, uint32 buf_size)
{
    uint32 i = 0;
    while (*triple != '-' && *triple != '\0' && i < buf_size - 1)
        arch_buf[i++] = *triple++;
    /* Make sure buffer is long enough */
    bh_assert(*triple == '-' || *triple == '\0');
}

static bool
is_baremetal_target(const char *target, const char *cpu, const char *abi)
{
    /* TODO: support more baremetal targets */
    if (target) {
        /* If target is thumbxxx, then it is baremetal target */
        if (!strncmp(target, "thumb", strlen("thumb")))
            return true;
    }
    return false;
}

void
aot_handle_llvm_errmsg(const char *string, LLVMErrorRef err)
{
    char *err_msg = LLVMGetErrorMessage(err);
    aot_set_last_error_v("%s: %s", string, err_msg);
    LLVMDisposeErrorMessage(err_msg);
}

bool
aot_compiler_init(void)
{
    /* Initialize LLVM environment */
#if LLVM_VERSION_MAJOR < 17
    LLVMInitializeCore(LLVMGetGlobalPassRegistry());
#endif

    /* Init environment of all targets for AOT compiler */
    LLVMInitializeAllTargetInfos();
    LLVMInitializeAllTargets();
    LLVMInitializeAllTargetMCs();
    LLVMInitializeAllAsmPrinters();

    return true;
}

void
aot_compiler_destroy(void)
{
    LLVMShutdown();
}

AOTCompContext *
aot_create_comp_context(AOTCompData *comp_data, aot_comp_option_t option)
{
    AOTCompContext *comp_ctx, *ret = NULL;
    LLVMTargetRef target;
    char *triple = NULL, *triple_norm, *arch, *abi;
    char *cpu = NULL, *features, buf[128];
    char *triple_norm_new = NULL, *cpu_new = NULL;
    char *err = NULL, *fp_round = "round.tonearest",
         *fp_exce = "fpexcept.strict";
    char triple_buf[128] = { 0 }, features_buf[128] = { 0 };
    uint32 opt_level, size_level, i;
    uint64 total_size;
    LLVMCodeModel code_model;
    LLVMTargetDataRef target_data_ref;

    /* Allocate memory */
    if (!(comp_ctx = wasm_runtime_malloc(sizeof(AOTCompContext)))) {
        aot_set_last_error("allocate memory failed.");
        return NULL;
    }

    memset(comp_ctx, 0, sizeof(AOTCompContext));
    comp_ctx->comp_data = comp_data;

    /* Create LLVM context, module and builder */
    if (!(comp_ctx->context = LLVMContextCreate())) {
        aot_set_last_error("create LLVM context failed.");
        goto fail;
    }

    if (!(comp_ctx->builder = LLVMCreateBuilderInContext(comp_ctx->context))) {
        aot_set_last_error("create LLVM builder failed.");
        goto fail;
    }

    /* Create LLVM module for each aot/jit function */
    if (!(comp_ctx->module = LLVMModuleCreateWithNameInContext(
              "WASM Module", comp_ctx->context))) {
        aot_set_last_error("create LLVM module failed.");
        goto fail;
    }

    if (option->no_sandbox_mode) {
        AOTMemory *aot_memory = &comp_data->memories[0];

        comp_ctx->no_sandbox_mode = true;
        option->heap_size = 0;

        bh_assert(MEMORY_DATA_SIZE_FIXED(aot_memory));
        (void)aot_memory;
    }

    if (option->enable_aux_stack_check)
        comp_ctx->enable_aux_stack_check = true;

    if (option->disable_llvm_lto)
        comp_ctx->disable_llvm_lto = true;

    comp_ctx->opt_level = option->opt_level;
    comp_ctx->size_level = option->size_level;

    /* Check heap size */
    if (option->heap_size > 0) {
        AOTMemory *aot_memory = &comp_data->memories[0];
        uint64 memory_data_size = (uint64)aot_memory->num_bytes_per_page
                                  * aot_memory->mem_init_page_count;
        bool memory_data_size_fixed = MEMORY_DATA_SIZE_FIXED(aot_memory);

        if (!memory_data_size_fixed) {
            aot_set_last_error("cannot append host managed heap when "
                               "wasm memory size isn't fixed.");
            goto fail;
        }
        if (option->heap_size < 512) {
            aot_set_last_error("host managed heap size too small.");
            goto fail;
        }
        if (memory_data_size + option->heap_size >= 4 * (uint64)BH_GB) {
            aot_set_last_error("failed append host managed heap.");
            goto fail;
        }

        aot_memory->host_managed_heap_offset = memory_data_size;
        aot_memory->num_bytes_per_page =
            (uint32)(memory_data_size + option->heap_size);
        aot_memory->mem_init_page_count = aot_memory->mem_max_page_count = 1;
    }

    comp_ctx->custom_sections_wp = option->custom_sections;
    comp_ctx->custom_sections_count = option->custom_sections_count;

    /* Create LLVM target machine */
    arch = option->target_arch;
    abi = option->target_abi;
    cpu = option->target_cpu;
    features = option->cpu_features;
    opt_level = option->opt_level;
    size_level = option->size_level;

    if (arch) {
        /* Add default sub-arch if not specified */
        if (!strcmp(arch, "arm"))
            arch = "armv4";
        else if (!strcmp(arch, "armeb"))
            arch = "armv4eb";
        else if (!strcmp(arch, "thumb"))
            arch = "thumbv4t";
        else if (!strcmp(arch, "thumbeb"))
            arch = "thumbv4teb";
        else if (!strcmp(arch, "aarch64"))
            arch = "aarch64v8";
        else if (!strcmp(arch, "aarch64_be"))
            arch = "aarch64_bev8";
    }

    /* Check target arch */
    if (arch && !check_target_arch(arch)) {
        if (!strcmp(arch, "help"))
            print_supported_targets();
        else
            aot_set_last_error(
                "Invalid target. "
                "Use --target=help to list all supported targets");
        goto fail;
    }

    /* Check target ABI */
    if (abi && !check_target_abi(abi)) {
        if (!strcmp(abi, "help"))
            print_supported_abis();
        else
            aot_set_last_error(
                "Invalid target ABI. "
                "Use --target-abi=help to list all supported ABI");
        goto fail;
    }

    /* Set default abi for riscv target */
    if (arch && !strncmp(arch, "riscv", 5) && !abi) {
        if (!strcmp(arch, "riscv64"))
            abi = "lp64d";
        else
            abi = "ilp32d";
    }

#if defined(__APPLE__) || defined(__MACH__)
    if (!abi) {
        /* On MacOS platform, set abi to "gnu" to avoid generating
           object file of Mach-O binary format which is unsupported */
        abi = "gnu";
        if (!arch && !cpu && !features) {
            /* Get CPU name of the host machine to avoid checking
               SIMD capability failed */
            if (!(cpu = cpu_new = LLVMGetHostCPUName())) {
                aot_set_last_error("llvm get host cpu name failed.");
                goto fail;
            }
        }
    }
#endif

    if (abi) {
        /* Construct target triple: <arch>-<vendor>-<sys>-<abi> */
        const char *vendor_sys;
        char *arch1 = arch, default_arch[32] = { 0 };

        if (!arch1) {
            char *default_triple = LLVMGetDefaultTargetTriple();

            if (!default_triple) {
                aot_set_last_error("llvm get default target triple failed.");
                goto fail;
            }

            vendor_sys = strstr(default_triple, "-");
            bh_assert(vendor_sys);
            bh_memcpy_s(default_arch, sizeof(default_arch), default_triple,
                        (uint32)(vendor_sys - default_triple));
            arch1 = default_arch;

            LLVMDisposeMessage(default_triple);
        }

        /**
         * Set <vendor>-<sys> according to abi to generate the object file
         * with the correct file format which might be different from the
         * default object file format of the host, e.g., generating AOT file
         * for Windows/MacOS under Linux host, or generating AOT file for
         * Linux/MacOS under Windows host.
         */

        if (!strcmp(abi, "msvc")) {
            if (!strcmp(arch1, "i386"))
                vendor_sys = "-pc-win32-";
            else
                vendor_sys = "-pc-windows-";
        }
        else {
            if (is_baremetal_target(arch, cpu, abi))
                vendor_sys = "-unknown-none-";
            else
                vendor_sys = "-pc-linux-";
        }

        bh_assert(strlen(arch1) + strlen(vendor_sys) + strlen(abi)
                  < sizeof(triple_buf));
        bh_memcpy_s(triple_buf, (uint32)sizeof(triple_buf), arch1,
                    (uint32)strlen(arch1));
        bh_memcpy_s(triple_buf + strlen(arch1),
                    (uint32)(sizeof(triple_buf) - strlen(arch1)), vendor_sys,
                    (uint32)strlen(vendor_sys));
        bh_memcpy_s(
            triple_buf + strlen(arch1) + strlen(vendor_sys),
            (uint32)(sizeof(triple_buf) - strlen(arch1) - strlen(vendor_sys)),
            abi, (uint32)strlen(abi));
        triple = triple_buf;
    }
    else if (arch) {
        /* Construct target triple: <arch>-<vendor>-<sys>-<abi> */
        const char *vendor_sys;
        char *default_triple = LLVMGetDefaultTargetTriple();

        if (!default_triple) {
            aot_set_last_error("llvm get default target triple failed.");
            goto fail;
        }

        if (strstr(default_triple, "windows")) {
            vendor_sys = "-pc-windows-";
            if (!abi)
                abi = "msvc";
        }
        else if (strstr(default_triple, "win32")) {
            vendor_sys = "-pc-win32-";
            if (!abi)
                abi = "msvc";
        }
        else if (is_baremetal_target(arch, cpu, abi)) {
            vendor_sys = "-unknown-none-";
            if (!abi)
                abi = "gnu";
        }
        else {
            vendor_sys = "-pc-linux-";
            if (!abi)
                abi = "gnu";
        }

        LLVMDisposeMessage(default_triple);

        bh_assert(strlen(arch) + strlen(vendor_sys) + strlen(abi)
                  < sizeof(triple_buf));
        bh_memcpy_s(triple_buf, (uint32)sizeof(triple_buf), arch,
                    (uint32)strlen(arch));
        bh_memcpy_s(triple_buf + strlen(arch),
                    (uint32)(sizeof(triple_buf) - strlen(arch)), vendor_sys,
                    (uint32)strlen(vendor_sys));
        bh_memcpy_s(
            triple_buf + strlen(arch) + strlen(vendor_sys),
            (uint32)(sizeof(triple_buf) - strlen(arch) - strlen(vendor_sys)),
            abi, (uint32)strlen(abi));
        triple = triple_buf;
    }

    if (!cpu && features) {
        aot_set_last_error("cpu isn't specified for cpu features.");
        goto fail;
    }

    if (!triple && !cpu) {
        /* Get a triple for the host machine */
        if (!(triple_norm = triple_norm_new = LLVMGetDefaultTargetTriple())) {
            aot_set_last_error("llvm get default target triple failed.");
            goto fail;
        }
        /* Get CPU name of the host machine */
        if (!(cpu = cpu_new = LLVMGetHostCPUName())) {
            aot_set_last_error("llvm get host cpu name failed.");
            goto fail;
        }
    }
    else if (triple) {
        /* Normalize a target triple */
        if (!(triple_norm = triple_norm_new =
                  LLVMNormalizeTargetTriple(triple))) {
            snprintf(buf, sizeof(buf),
                     "llvm normlalize target triple (%s) failed.", triple);
            aot_set_last_error(buf);
            goto fail;
        }
        if (!cpu)
            cpu = "";
    }
    else {
        /* triple is NULL, cpu isn't NULL */
        snprintf(buf, sizeof(buf), "target isn't specified for cpu %s.", cpu);
        aot_set_last_error(buf);
        goto fail;
    }

    /* Add module flag and cpu feature for riscv target */
    if (arch && !strncmp(arch, "riscv", 5)) {
        LLVMMetadataRef meta_target_abi;

        if (!(meta_target_abi = LLVMMDStringInContext2(comp_ctx->context, abi,
                                                       strlen(abi)))) {
            aot_set_last_error("create metadata string failed.");
            goto fail;
        }
        LLVMAddModuleFlag(comp_ctx->module, LLVMModuleFlagBehaviorError,
                          "target-abi", strlen("target-abi"), meta_target_abi);

        if (!strcmp(abi, "lp64d") || !strcmp(abi, "ilp32d")) {
            if (features && !strstr(features, "+d")) {
                snprintf(features_buf, sizeof(features_buf), "%s%s", features,
                         ",+d");
                features = features_buf;
            }
            else if (!features) {
                features = "+d";
            }
        }
    }

    if (!features)
        features = "";

    /* Get target with triple, note that LLVMGetTargetFromTriple()
       return 0 when success, but not true. */
    if (LLVMGetTargetFromTriple(triple_norm, &target, &err) != 0) {
        if (err) {
            LLVMDisposeMessage(err);
            err = NULL;
        }
        snprintf(buf, sizeof(buf), "llvm get target from triple (%s) failed",
                 triple_norm);
        aot_set_last_error(buf);
        goto fail;
    }

    /* Save target arch */
    get_target_arch_from_triple(triple_norm, comp_ctx->target_arch,
                                sizeof(comp_ctx->target_arch));

    os_printf("Create AoT compiler with:\n");
    os_printf("  target:        %s\n", comp_ctx->target_arch);
    os_printf("  target cpu:    %s\n", cpu);
    os_printf("  target triple: %s\n", triple_norm);
    os_printf("  cpu features:  %s\n", features);
    os_printf("  opt level:     %d\n", opt_level);
    os_printf("  size level:    %d\n", size_level);
    switch (option->output_format) {
        case AOT_LLVMIR_UNOPT_FILE:
            os_printf("  output format: unoptimized LLVM IR\n");
            break;
        case AOT_LLVMIR_OPT_FILE:
            os_printf("  output format: optimized LLVM IR\n");
            break;
        case AOT_OBJECT_FILE:
            os_printf("  output format: native object file\n");
            break;
    }

    LLVMSetTarget(comp_ctx->module, triple_norm);

    if (!LLVMTargetHasTargetMachine(target)) {
        snprintf(buf, sizeof(buf), "no target machine for this target (%s).",
                 triple_norm);
        aot_set_last_error(buf);
        goto fail;
    }

    /* Report error if target hasn't asm backend. */
    if (!LLVMTargetHasAsmBackend(target)) {
        snprintf(buf, sizeof(buf), "no asm backend for this target (%s).",
                 LLVMGetTargetName(target));
        aot_set_last_error(buf);
        goto fail;
    }

    /* Set code model */
    if (size_level == 0)
        code_model = LLVMCodeModelLarge;
    else if (size_level == 1)
        code_model = LLVMCodeModelMedium;
    else if (size_level == 2)
        code_model = LLVMCodeModelKernel;
    else
        code_model = LLVMCodeModelSmall;

    /* Create the target machine */
    if (!(comp_ctx->target_machine = LLVMCreateTargetMachineWithOpts(
              target, triple_norm, cpu, features, opt_level, LLVMRelocPIC,
              code_model))) {
        aot_set_last_error("create LLVM target machine failed.");
        goto fail;
    }

    /* If only to create target machine for querying information, early stop
     */
    if ((arch && !strcmp(arch, "help")) || (abi && !strcmp(abi, "help"))
        || (cpu && !strcmp(cpu, "help"))
        || (features && !strcmp(features, "+help"))) {
        LOG_DEBUG("create LLVM target machine only for printing help info.");
        goto fail;
    }

    if (option->enable_simd && strcmp(comp_ctx->target_arch, "x86_64") != 0
        && strncmp(comp_ctx->target_arch, "aarch64", 7) != 0) {
        /* Disable simd if it isn't supported by target arch */
        option->enable_simd = false;
    }

    if (option->enable_simd) {
        char *tmp;
        bool check_simd_ret;

        comp_ctx->enable_simd = true;

        if (!(tmp = LLVMGetTargetMachineCPU(comp_ctx->target_machine))) {
            aot_set_last_error("get CPU from Target Machine fail");
            goto fail;
        }

        check_simd_ret =
            aot_check_simd_compatibility(comp_ctx->target_arch, tmp);
        LLVMDisposeMessage(tmp);
        if (!check_simd_ret) {
            aot_set_last_error("SIMD compatibility check failed, "
                               "try adding --cpu=<cpu> to specify a cpu "
                               "or adding --disable-simd to disable SIMD");
            goto fail;
        }
    }

    if (!(target_data_ref =
              LLVMCreateTargetDataLayout(comp_ctx->target_machine))) {
        aot_set_last_error("create LLVM target data layout failed.");
        goto fail;
    }
    LLVMSetModuleDataLayout(comp_ctx->module, target_data_ref);
    comp_ctx->pointer_size = LLVMPointerSize(target_data_ref);
    LLVMDisposeTargetData(target_data_ref);

    comp_ctx->optimize = true;
    if (option->output_format == AOT_LLVMIR_UNOPT_FILE)
        comp_ctx->optimize = false;

    /* Create metadata for llvm float experimental constrained intrinsics */
    if (!(comp_ctx->fp_rounding_mode = LLVMMDStringInContext(
              comp_ctx->context, fp_round, (uint32)strlen(fp_round)))
        || !(comp_ctx->fp_exception_behavior = LLVMMDStringInContext(
                 comp_ctx->context, fp_exce, (uint32)strlen(fp_exce)))) {
        aot_set_last_error("create float llvm metadata failed.");
        goto fail;
    }

    if (!aot_set_llvm_basic_types(&comp_ctx->basic_types, comp_ctx->context)) {
        aot_set_last_error("create LLVM basic types failed.");
        goto fail;
    }

    if (!aot_create_llvm_consts(&comp_ctx->llvm_consts, comp_ctx)) {
        aot_set_last_error("create LLVM const values failed.");
        goto fail;
    }

    comp_ctx->import_func_count = comp_data->import_func_count;
    /* Create param values */
    total_size = sizeof(LLVMValueRef) * (uint64)comp_data->import_func_count;
    if (total_size > 0) {
        if (total_size >= UINT32_MAX
            || !(comp_ctx->import_func_ptrs =
                     wasm_runtime_malloc((uint32)total_size))) {
            aot_set_last_error("allocate memory for param values failed.");
            goto fail;
        }
        memset(comp_ctx->import_func_ptrs, 0, (uint32)total_size);
    }

    /* Create function context for each function */
    comp_ctx->func_ctx_count = comp_data->func_count;
    if (comp_data->func_count > 0
        && !(comp_ctx->func_ctxes =
                 aot_create_func_contexts(comp_data, comp_ctx)))
        goto fail;

    if (!create_wasm_globals(comp_data, comp_ctx)
        || !create_wasm_instance_create_func(comp_data, comp_ctx)
        || !create_wasm_instance_destroy_func(comp_data, comp_ctx)
        || !create_wasm_instance_is_created_func(comp_data, comp_ctx)
        || !create_wasm_set_exception_func(comp_ctx)
        || !create_wasm_get_exception_func(comp_ctx)
        || !create_wasm_get_memory_func(comp_data, comp_ctx)
        || !create_wasm_get_memory_size_func(comp_data, comp_ctx)
        || !create_wasm_get_heap_handle_func(comp_data, comp_ctx)
        || !create_wasm_get_export_apis_func(comp_data, comp_ctx))
        goto fail;

    if (comp_ctx->no_sandbox_mode && !create_main_func(comp_data, comp_ctx))
        goto fail;

    for (i = 0; i < comp_ctx->func_ctx_count; i++) {
        AOTFuncContext *func_ctx = comp_ctx->func_ctxes[i];

        LLVMPositionBuilderAtEnd(
            comp_ctx->builder,
            func_ctx->block_stack.block_list_head->llvm_entry_block);

        LLVMValueRef memory_data_global =
            LLVMGetNamedGlobal(comp_ctx->module, "memory_data");
        bh_assert(memory_data_global);

        AOTMemory *aot_memory = &comp_data->memories[0];
        bool memory_data_size_fixed = MEMORY_DATA_SIZE_FIXED(aot_memory);

        if (memory_data_size_fixed) {
            if (!(func_ctx->memory_data =
                      LLVMBuildLoad2(comp_ctx->builder, INT8_PTR_TYPE,
                                     memory_data_global, "memory_data"))) {
                aot_set_last_error("llvm build load failed");
                goto fail;
            }
        }
    }

    if (cpu) {
        uint32 len = (uint32)strlen(cpu) + 1;
        if (!(comp_ctx->target_cpu = wasm_runtime_malloc(len))) {
            aot_set_last_error("allocate memory failed");
            goto fail;
        }
        bh_memcpy_s(comp_ctx->target_cpu, len, cpu, len);
    }

    ret = comp_ctx;

fail:
    if (triple_norm_new)
        LLVMDisposeMessage(triple_norm_new);

    if (cpu_new)
        LLVMDisposeMessage(cpu_new);

    if (!ret)
        aot_destroy_comp_context(comp_ctx);

    (void)i;
    return ret;
}

void
aot_destroy_comp_context(AOTCompContext *comp_ctx)
{
    if (!comp_ctx)
        return;

    if (comp_ctx->target_machine)
        LLVMDisposeTargetMachine(comp_ctx->target_machine);

    if (comp_ctx->builder)
        LLVMDisposeBuilder(comp_ctx->builder);

    if (comp_ctx->module)
        LLVMDisposeModule(comp_ctx->module);

    if (comp_ctx->context)
        LLVMContextDispose(comp_ctx->context);

    if (comp_ctx->func_ctxes)
        aot_destroy_func_contexts(comp_ctx->func_ctxes,
                                  comp_ctx->func_ctx_count);

    if (comp_ctx->target_cpu)
        wasm_runtime_free(comp_ctx->target_cpu);

    if (comp_ctx->import_func_ptrs)
        wasm_runtime_free(comp_ctx->import_func_ptrs);

    wasm_runtime_free(comp_ctx);
}

void
aot_value_stack_push(AOTValueStack *stack, AOTValue *value)
{
    if (!stack->value_list_head)
        stack->value_list_head = stack->value_list_end = value;
    else {
        stack->value_list_end->next = value;
        value->prev = stack->value_list_end;
        stack->value_list_end = value;
    }
}

AOTValue *
aot_value_stack_pop(AOTValueStack *stack)
{
    AOTValue *value = stack->value_list_end;

    bh_assert(stack->value_list_end);

    if (stack->value_list_head == stack->value_list_end)
        stack->value_list_head = stack->value_list_end = NULL;
    else {
        stack->value_list_end = stack->value_list_end->prev;
        stack->value_list_end->next = NULL;
        value->prev = NULL;
    }

    return value;
}

void
aot_value_stack_destroy(AOTValueStack *stack)
{
    AOTValue *value = stack->value_list_head, *p;

    while (value) {
        p = value->next;
        wasm_runtime_free(value);
        value = p;
    }

    stack->value_list_head = NULL;
    stack->value_list_end = NULL;
}

void
aot_block_stack_push(AOTBlockStack *stack, AOTBlock *block)
{
    if (!stack->block_list_head)
        stack->block_list_head = stack->block_list_end = block;
    else {
        stack->block_list_end->next = block;
        block->prev = stack->block_list_end;
        stack->block_list_end = block;
    }
}

AOTBlock *
aot_block_stack_pop(AOTBlockStack *stack)
{
    AOTBlock *block = stack->block_list_end;

    bh_assert(stack->block_list_end);

    if (stack->block_list_head == stack->block_list_end)
        stack->block_list_head = stack->block_list_end = NULL;
    else {
        stack->block_list_end = stack->block_list_end->prev;
        stack->block_list_end->next = NULL;
        block->prev = NULL;
    }

    return block;
}

void
aot_block_stack_destroy(AOTBlockStack *stack)
{
    AOTBlock *block = stack->block_list_head, *p;

    while (block) {
        p = block->next;
        aot_value_stack_destroy(&block->value_stack);
        aot_block_destroy(block);
        block = p;
    }

    stack->block_list_head = NULL;
    stack->block_list_end = NULL;
}

void
aot_block_destroy(AOTBlock *block)
{
    aot_value_stack_destroy(&block->value_stack);
    if (block->param_types)
        wasm_runtime_free(block->param_types);
    if (block->param_phis)
        wasm_runtime_free(block->param_phis);
    if (block->else_param_phis)
        wasm_runtime_free(block->else_param_phis);
    if (block->result_types)
        wasm_runtime_free(block->result_types);
    if (block->result_phis)
        wasm_runtime_free(block->result_phis);
    wasm_runtime_free(block);
}

bool
aot_build_zero_function_ret(const AOTCompContext *comp_ctx,
                            AOTFuncContext *func_ctx, AOTFuncType *func_type)
{
    LLVMValueRef ret = NULL;

    if (func_type->result_count) {
        switch (func_type->types[func_type->param_count]) {
            case VALUE_TYPE_I32:
                ret = LLVMBuildRet(comp_ctx->builder, I32_ZERO);
                break;
            case VALUE_TYPE_I64:
                ret = LLVMBuildRet(comp_ctx->builder, I64_ZERO);
                break;
            case VALUE_TYPE_F32:
                ret = LLVMBuildRet(comp_ctx->builder, F32_ZERO);
                break;
            case VALUE_TYPE_F64:
                ret = LLVMBuildRet(comp_ctx->builder, F64_ZERO);
                break;
            case VALUE_TYPE_V128:
                ret =
                    LLVMBuildRet(comp_ctx->builder, LLVM_CONST(i64x2_vec_zero));
                break;
            default:
                bh_assert(0);
        }
    }
    else {
        ret = LLVMBuildRetVoid(comp_ctx->builder);
    }

    if (!ret) {
        aot_set_last_error("llvm build ret failed.");
        return false;
    }
    return true;
}

static LLVMValueRef
__call_llvm_intrinsic(const AOTCompContext *comp_ctx,
                      const AOTFuncContext *func_ctx, const char *name,
                      LLVMTypeRef ret_type, LLVMTypeRef *param_types,
                      int param_count, LLVMValueRef *param_values)
{
    LLVMValueRef func, ret;
    LLVMTypeRef func_type;

    /* Declare llvm intrinsic function if necessary */
    if (!(func = LLVMGetNamedFunction(func_ctx->module, name))) {
        if (!(func_type = LLVMFunctionType(ret_type, param_types,
                                           (uint32)param_count, false))) {
            aot_set_last_error("create LLVM intrinsic function type failed.");
            return NULL;
        }

        if (!(func = LLVMAddFunction(func_ctx->module, name, func_type))) {
            aot_set_last_error("add LLVM intrinsic function failed.");
            return NULL;
        }
    }

#if LLVM_VERSION_MAJOR >= 14
    func_type =
        LLVMFunctionType(ret_type, param_types, (uint32)param_count, false);
#endif

    /* Call the LLVM intrinsic function */
    if (!(ret = LLVMBuildCall2(comp_ctx->builder, func_type, func, param_values,
                               (uint32)param_count, "call"))) {
        aot_set_last_error("llvm build intrinsic call failed.");
        return NULL;
    }

    return ret;
}

LLVMValueRef
aot_call_llvm_intrinsic(const AOTCompContext *comp_ctx,
                        const AOTFuncContext *func_ctx, const char *intrinsic,
                        LLVMTypeRef ret_type, LLVMTypeRef *param_types,
                        int param_count, ...)
{
    LLVMValueRef *param_values, ret;
    va_list argptr;
    uint64 total_size;
    int i = 0;

    /* Create param values */
    total_size = sizeof(LLVMValueRef) * (uint64)param_count;
    if (total_size >= UINT32_MAX
        || !(param_values = wasm_runtime_malloc((uint32)total_size))) {
        aot_set_last_error("allocate memory for param values failed.");
        return false;
    }

    /* Load each param value */
    va_start(argptr, param_count);
    while (i < param_count)
        param_values[i++] = va_arg(argptr, LLVMValueRef);
    va_end(argptr);

    ret = __call_llvm_intrinsic(comp_ctx, func_ctx, intrinsic, ret_type,
                                param_types, param_count, param_values);

    wasm_runtime_free(param_values);

    return ret;
}

LLVMValueRef
aot_call_llvm_intrinsic_v(const AOTCompContext *comp_ctx,
                          const AOTFuncContext *func_ctx, const char *intrinsic,
                          LLVMTypeRef ret_type, LLVMTypeRef *param_types,
                          int param_count, va_list param_value_list)
{
    LLVMValueRef *param_values, ret;
    uint64 total_size;
    int i = 0;

    /* Create param values */
    total_size = sizeof(LLVMValueRef) * (uint64)param_count;
    if (total_size >= UINT32_MAX
        || !(param_values = wasm_runtime_malloc((uint32)total_size))) {
        aot_set_last_error("allocate memory for param values failed.");
        return false;
    }

    /* Load each param value */
    while (i < param_count)
        param_values[i++] = va_arg(param_value_list, LLVMValueRef);

    ret = __call_llvm_intrinsic(comp_ctx, func_ctx, intrinsic, ret_type,
                                param_types, param_count, param_values);

    wasm_runtime_free(param_values);

    return ret;
}

LLVMValueRef
aot_get_memory_base_addr(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    LLVMValueRef memory_data_global, memory_data;

    memory_data_global = LLVMGetNamedGlobal(comp_ctx->module, "memory_data");
    bh_assert(memory_data_global);

    if (!(memory_data = LLVMBuildLoad2(comp_ctx->builder, INT8_PTR_TYPE,
                                       memory_data_global, "memory_data"))) {
        aot_set_last_error("llvm build load failed");
        return NULL;
    }

    return memory_data;
}
