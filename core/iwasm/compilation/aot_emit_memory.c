/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "aot_emit_memory.h"
#include "aot_compiler.h"
#include "aot_emit_exception.h"
#include "aot_emit_control.h"

#define BUILD_ICMP(op, left, right, res, name)                                \
    do {                                                                      \
        if (!(res =                                                           \
                  LLVMBuildICmp(comp_ctx->builder, op, left, right, name))) { \
            aot_set_last_error("llvm build icmp failed.");                    \
            goto fail;                                                        \
        }                                                                     \
    } while (0)

#define BUILD_OP(Op, left, right, res, name)                                \
    do {                                                                    \
        if (!(res = LLVMBuild##Op(comp_ctx->builder, left, right, name))) { \
            aot_set_last_error("llvm build " #Op " fail.");                 \
            goto fail;                                                      \
        }                                                                   \
    } while (0)

#define ADD_BASIC_BLOCK(block, name)                                          \
    do {                                                                      \
        if (!(block = LLVMAppendBasicBlockInContext(comp_ctx->context,        \
                                                    func_ctx->func, name))) { \
            aot_set_last_error("llvm add basic block failed.");               \
            goto fail;                                                        \
        }                                                                     \
    } while (0)

#define SET_BUILD_POS(block) LLVMPositionBuilderAtEnd(comp_ctx->builder, block)

#define MOVE_BLOCK_AFTER(llvm_block, llvm_block_after) \
    LLVMMoveBasicBlockAfter(llvm_block, llvm_block_after)

#define BUILD_COND_BR(value_if, block_then, block_else)               \
    do {                                                              \
        if (!LLVMBuildCondBr(comp_ctx->builder, value_if, block_then, \
                             block_else)) {                           \
            aot_set_last_error("llvm build cond br failed.");         \
            goto fail;                                                \
        }                                                             \
    } while (0)

static LLVMValueRef
get_memory_curr_page_count(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx);

static bool
zero_extend_u64(AOTCompContext *comp_ctx, LLVMValueRef *value, const char *name)
{
    if (comp_ctx->pointer_size == sizeof(uint64)) {
        /* zero extend to uint64 if the target is 64-bit */
        *value = LLVMBuildZExt(comp_ctx->builder, *value, I64_TYPE, name);
        if (!*value) {
            aot_set_last_error("llvm build zero extend failed.");
            return false;
        }
    }
    return true;
}

static LLVMValueRef
get_memory_check_bound(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                       uint32 bytes)
{
    LLVMValueRef mem_check_bound = NULL;
    switch (bytes) {
        case 1:
            mem_check_bound =
                LLVMGetNamedGlobal(comp_ctx->module, "mem_bound_check_1byte");
            break;
        case 2:
            mem_check_bound =
                LLVMGetNamedGlobal(comp_ctx->module, "mem_bound_check_2bytes");
            break;
        case 4:
            mem_check_bound =
                LLVMGetNamedGlobal(comp_ctx->module, "mem_bound_check_4bytes");
            break;
        case 8:
            mem_check_bound =
                LLVMGetNamedGlobal(comp_ctx->module, "mem_bound_check_8bytes");
            break;
        case 16:
            mem_check_bound =
                LLVMGetNamedGlobal(comp_ctx->module, "mem_bound_check_16bytes");
            break;
        default:
            bh_assert(0);
            return NULL;
    }

    if (!(mem_check_bound =
              LLVMBuildLoad2(comp_ctx->builder, I64_TYPE, mem_check_bound,
                             "mem_check_bound"))) {
        aot_set_last_error("llvm build load failed.");
        return NULL;
    }
    return mem_check_bound;
}

#define MEMORY_DATA_SIZE_FIXED(aot_memory)                             \
    (aot_memory->mem_init_page_count == aot_memory->mem_max_page_count \
         ? true                                                        \
         : false)

bool
aot_print_i32(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
              LLVMValueRef i32_value)
{
    LLVMTypeRef param_types[1], func_type;
    LLVMValueRef func;
    char func_name[32];

    param_types[0] = I32_TYPE;
    if (!(func_type = LLVMFunctionType(VOID_TYPE, param_types, 1, false))) {
        aot_set_last_error("add LLVM function type failed.");
        return false;
    }
    snprintf(func_name, sizeof(func_name), "%s", "print_i32");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    if (!LLVMBuildCall2(comp_ctx->builder, func_type, func, &i32_value, 1,
                        "")) {
        aot_set_last_error("llvm build call failed.");
        return false;
    }
    return true;
}

bool
aot_print_i64(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
              LLVMValueRef i64_value)
{
    LLVMTypeRef param_types[1], func_type;
    LLVMValueRef func;
    char func_name[32];

    param_types[0] = I64_TYPE;
    if (!(func_type = LLVMFunctionType(VOID_TYPE, param_types, 1, false))) {
        aot_set_last_error("add LLVM function type failed.");
        return false;
    }
    snprintf(func_name, sizeof(func_name), "%s", "print_i64");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        return false;
    }

    if (!LLVMBuildCall2(comp_ctx->builder, func_type, func, &i64_value, 1,
                        "")) {
        aot_set_last_error("llvm build call failed.");
        return false;
    }
    return true;
}

LLVMValueRef
aot_check_memory_overflow(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                          uint64 offset, uint32 bytes,
                          WASMRelocation *relocation)
{
    LLVMValueRef offset_const =
        MEMORY64_COND_VALUE(I64_CONST(offset), I32_CONST((uint32)offset));
    LLVMValueRef addr, maddr, offset1, cmp, mem_check_bound;
    LLVMValueRef mem_base_addr, cmp1, cmp2;
    LLVMBasicBlockRef check_succ, block_curr;
    AOTMemory *aot_memory = &comp_ctx->comp_data->memories[0];
    bool is_target_64bit =
        (comp_ctx->pointer_size == sizeof(uint64)) ? true : false;

    CHECK_LLVM_CONST(offset_const);

    if (!(mem_base_addr = aot_get_memory_base_addr(comp_ctx, func_ctx)))
        return false;

    POP_MEM_OFFSET(addr);

    if (is_target_64bit) {
        if (!(offset_const = LLVMBuildZExt(comp_ctx->builder, offset_const,
                                           I64_TYPE, "offset_i64"))
            || !(addr = LLVMBuildZExt(comp_ctx->builder, addr, I64_TYPE,
                                      "addr_i64"))) {
            aot_set_last_error("llvm build zero extend failed.");
            goto fail;
        }
    }

    if (offset)
        /* offset1 = offset + addr; */
        BUILD_OP(Add, offset_const, addr, offset1, "offset1");
    else
        offset1 = addr;

    block_curr = LLVMGetInsertBlock(comp_ctx->builder);

    if (!comp_ctx->no_sandbox_mode && aot_memory->mem_init_page_count == 0) {
        LLVMValueRef mem_size;

        if (!(mem_size = get_memory_curr_page_count(comp_ctx, func_ctx))) {
            goto fail;
        }
        BUILD_ICMP(LLVMIntEQ, mem_size, MEMORY64_COND_VALUE(I64_ZERO, I32_ZERO),
                   cmp, "is_zero");
        ADD_BASIC_BLOCK(check_succ, "check_mem_size_succ");
        LLVMMoveBasicBlockAfter(check_succ, block_curr);
        if (!aot_emit_exception(comp_ctx, func_ctx,
                                EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS, true, cmp,
                                check_succ)) {
            goto fail;
        }

        SET_BUILD_POS(check_succ);
        block_curr = check_succ;
    }

    if (!comp_ctx->no_sandbox_mode) {
        if (!(mem_check_bound =
                  get_memory_check_bound(comp_ctx, func_ctx, bytes))) {
            goto fail;
        }

        if (comp_ctx->pointer_size == sizeof(uint64))
            BUILD_ICMP(LLVMIntUGT, offset1, mem_check_bound, cmp, "cmp");
        else {
            /* Check integer overflow */
            BUILD_ICMP(LLVMIntULT, offset1, addr, cmp1, "cmp1");
            if (!(mem_check_bound =
                      LLVMBuildTrunc(comp_ctx->builder, mem_check_bound,
                                     I32_TYPE, "mem_check_bound_i32"))) {
                aot_set_last_error("llvm build trunc failed.");
                goto fail;
            }
            BUILD_ICMP(LLVMIntUGT, offset1, mem_check_bound, cmp2, "cmp2");
            BUILD_OP(Or, cmp1, cmp2, cmp, "cmp");
        }

        /* Add basic blocks */
        ADD_BASIC_BLOCK(check_succ, "check_mem_bound_succ");
        LLVMMoveBasicBlockAfter(check_succ, block_curr);

        if (!aot_emit_exception(comp_ctx, func_ctx,
                                EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS, true, cmp,
                                check_succ)) {
            goto fail;
        }

        SET_BUILD_POS(check_succ);
    }

    if (!comp_ctx->no_sandbox_mode
        || (comp_ctx->no_sandbox_mode && relocation)) {
        /* maddr = mem_base_addr + offset1 */
        if (!(maddr =
                  LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE,
                                        mem_base_addr, &offset1, 1, "maddr"))) {
            aot_set_last_error("llvm build add failed.");
            goto fail;
        }
    }
    else {
        if (!(addr = LLVMBuildIntToPtr(comp_ctx->builder, addr, INT8_PTR_TYPE,
                                       "base_addr"))) {
            aot_set_last_error("llvm build int to ptr failed.");
            goto fail;
        }
        if (offset) {
            if (!(maddr =
                      LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE, addr,
                                            &offset_const, 1, "maddr"))) {
                aot_set_last_error("llvm build in bounds gep failed.");
                goto fail;
            }
        }
        else {
            maddr = addr;
        }
    }

    return maddr;
fail:
    return NULL;
}

#define BUILD_PTR_CAST(ptr_type)                                           \
    do {                                                                   \
        if (!(maddr = LLVMBuildBitCast(comp_ctx->builder, maddr, ptr_type, \
                                       "data_ptr"))) {                     \
            aot_set_last_error("llvm build bit cast failed.");             \
            goto fail;                                                     \
        }                                                                  \
    } while (0)

#define BUILD_LOAD(data_type)                                             \
    do {                                                                  \
        if (!(value = LLVMBuildLoad2(comp_ctx->builder, data_type, maddr, \
                                     "data"))) {                          \
            aot_set_last_error("llvm build load failed.");                \
            goto fail;                                                    \
        }                                                                 \
        LLVMSetAlignment(value, 1);                                       \
    } while (0)

#define BUILD_TRUNC(value, data_type)                                     \
    do {                                                                  \
        if (!(value = LLVMBuildTrunc(comp_ctx->builder, value, data_type, \
                                     "val_trunc"))) {                     \
            aot_set_last_error("llvm build trunc failed.");               \
            goto fail;                                                    \
        }                                                                 \
    } while (0)

#define BUILD_STORE()                                                   \
    do {                                                                \
        LLVMValueRef res;                                               \
        if (!(res = LLVMBuildStore(comp_ctx->builder, value, maddr))) { \
            aot_set_last_error("llvm build store failed.");             \
            goto fail;                                                  \
        }                                                               \
        LLVMSetAlignment(res, 1);                                       \
    } while (0)

#define BUILD_SIGN_EXT(dst_type)                                        \
    do {                                                                \
        if (!(value = LLVMBuildSExt(comp_ctx->builder, value, dst_type, \
                                    "data_s_ext"))) {                   \
            aot_set_last_error("llvm build sign ext failed.");          \
            goto fail;                                                  \
        }                                                               \
    } while (0)

#define BUILD_ZERO_EXT(dst_type)                                        \
    do {                                                                \
        if (!(value = LLVMBuildZExt(comp_ctx->builder, value, dst_type, \
                                    "data_z_ext"))) {                   \
            aot_set_last_error("llvm build zero ext failed.");          \
            goto fail;                                                  \
        }                                                               \
    } while (0)

bool
check_memory_alignment(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                       LLVMValueRef addr, uint32 align)
{
    LLVMBasicBlockRef block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    LLVMBasicBlockRef check_align_succ;
    LLVMValueRef align_mask = I32_CONST(((uint32)1 << align) - 1);
    LLVMValueRef res;

    if (comp_ctx->no_sandbox_mode)
        return true;

    CHECK_LLVM_CONST(align_mask);

    /* Convert pointer to int */
    if (!(addr = LLVMBuildPtrToInt(comp_ctx->builder, addr, I32_TYPE,
                                   "address"))) {
        aot_set_last_error("llvm build ptr to int failed.");
        goto fail;
    }

    /* The memory address should be aligned */
    BUILD_OP(And, addr, align_mask, res, "and");
    BUILD_ICMP(LLVMIntNE, res, I32_ZERO, res, "cmp");

    /* Add basic blocks */
    ADD_BASIC_BLOCK(check_align_succ, "check_align_succ");
    LLVMMoveBasicBlockAfter(check_align_succ, block_curr);

    if (!aot_emit_exception(comp_ctx, func_ctx, EXCE_UNALIGNED_ATOMIC, true,
                            res, check_align_succ)) {
        goto fail;
    }

    SET_BUILD_POS(check_align_succ);

    return true;
fail:
    return false;
}

#define BUILD_ATOMIC_LOAD(align, data_type)                                \
    do {                                                                   \
        if (!(check_memory_alignment(comp_ctx, func_ctx, maddr, align))) { \
            goto fail;                                                     \
        }                                                                  \
        if (!(value = LLVMBuildLoad2(comp_ctx->builder, data_type, maddr,  \
                                     "data"))) {                           \
            aot_set_last_error("llvm build load failed.");                 \
            goto fail;                                                     \
        }                                                                  \
        LLVMSetAlignment(value, 1 << align);                               \
        LLVMSetVolatile(value, true);                                      \
        LLVMSetOrdering(value, LLVMAtomicOrderingSequentiallyConsistent);  \
    } while (0)

#define BUILD_ATOMIC_STORE(align)                                          \
    do {                                                                   \
        LLVMValueRef res;                                                  \
        if (!(check_memory_alignment(comp_ctx, func_ctx, maddr, align))) { \
            goto fail;                                                     \
        }                                                                  \
        if (!(res = LLVMBuildStore(comp_ctx->builder, value, maddr))) {    \
            aot_set_last_error("llvm build store failed.");                \
            goto fail;                                                     \
        }                                                                  \
        LLVMSetAlignment(res, 1 << align);                                 \
        LLVMSetVolatile(res, true);                                        \
        LLVMSetOrdering(res, LLVMAtomicOrderingSequentiallyConsistent);    \
    } while (0)

bool
aot_compile_op_i32_load(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                        uint32 align, uint64 offset, uint32 bytes, bool sign,
                        bool atomic, WASMRelocation *relocation)
{
    LLVMValueRef maddr, value = NULL;
    LLVMTypeRef data_type;

    if (!(maddr = aot_check_memory_overflow(comp_ctx, func_ctx, offset, bytes,
                                            relocation)))
        return false;

    switch (bytes) {
        case 4:
            BUILD_PTR_CAST(INT32_PTR_TYPE);
            if (atomic)
                BUILD_ATOMIC_LOAD(align, I32_TYPE);
            else
                BUILD_LOAD(I32_TYPE);
            break;
        case 2:
        case 1:
            if (bytes == 2) {
                BUILD_PTR_CAST(INT16_PTR_TYPE);
                data_type = INT16_TYPE;
            }
            else {
                BUILD_PTR_CAST(INT8_PTR_TYPE);
                data_type = INT8_TYPE;
            }

            if (atomic) {
                BUILD_ATOMIC_LOAD(align, data_type);
                BUILD_ZERO_EXT(I32_TYPE);
            }
            else {
                BUILD_LOAD(data_type);
                if (sign)
                    BUILD_SIGN_EXT(I32_TYPE);
                else
                    BUILD_ZERO_EXT(I32_TYPE);
            }
            break;
        default:
            bh_assert(0);
            break;
    }

    PUSH_I32(value);
    (void)data_type;
    return true;
fail:
    return false;
}

bool
aot_compile_op_i64_load(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                        uint32 align, uint64 offset, uint32 bytes, bool sign,
                        bool atomic, WASMRelocation *relocation)
{
    LLVMValueRef maddr, value = NULL;
    LLVMTypeRef data_type;

    if (!(maddr = aot_check_memory_overflow(comp_ctx, func_ctx, offset, bytes,
                                            relocation)))
        return false;

    switch (bytes) {
        case 8:
            BUILD_PTR_CAST(INT64_PTR_TYPE);
            if (atomic)
                BUILD_ATOMIC_LOAD(align, I64_TYPE);
            else
                BUILD_LOAD(I64_TYPE);
            break;
        case 4:
        case 2:
        case 1:
            if (bytes == 4) {
                BUILD_PTR_CAST(INT32_PTR_TYPE);
                data_type = I32_TYPE;
            }
            else if (bytes == 2) {
                BUILD_PTR_CAST(INT16_PTR_TYPE);
                data_type = INT16_TYPE;
            }
            else {
                BUILD_PTR_CAST(INT8_PTR_TYPE);
                data_type = INT8_TYPE;
            }

            if (atomic) {
                BUILD_ATOMIC_LOAD(align, data_type);
                BUILD_ZERO_EXT(I64_TYPE);
            }
            else {
                BUILD_LOAD(data_type);
                if (sign)
                    BUILD_SIGN_EXT(I64_TYPE);
                else
                    BUILD_ZERO_EXT(I64_TYPE);
            }
            break;
        default:
            bh_assert(0);
            break;
    }

    PUSH_I64(value);
    (void)data_type;
    return true;
fail:
    return false;
}

bool
aot_compile_op_f32_load(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                        uint32 align, uint64 offset, WASMRelocation *relocation)
{
    LLVMValueRef maddr, value;

    if (!(maddr = aot_check_memory_overflow(comp_ctx, func_ctx, offset, 4,
                                            relocation)))
        return false;

    BUILD_PTR_CAST(F32_PTR_TYPE);
    BUILD_LOAD(F32_TYPE);

    PUSH_F32(value);
    return true;
fail:
    return false;
}

bool
aot_compile_op_f64_load(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                        uint32 align, uint64 offset, WASMRelocation *relocation)
{
    LLVMValueRef maddr, value;

    if (!(maddr = aot_check_memory_overflow(comp_ctx, func_ctx, offset, 8,
                                            relocation)))
        return false;

    BUILD_PTR_CAST(F64_PTR_TYPE);
    BUILD_LOAD(F64_TYPE);

    PUSH_F64(value);
    return true;
fail:
    return false;
}

bool
aot_compile_op_i32_store(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 align, uint64 offset, uint32 bytes, bool atomic,
                         WASMRelocation *relocation)
{
    LLVMValueRef maddr, value;

    POP_I32(value);

    if (!(maddr = aot_check_memory_overflow(comp_ctx, func_ctx, offset, bytes,
                                            relocation)))
        return false;

    switch (bytes) {
        case 4:
            BUILD_PTR_CAST(INT32_PTR_TYPE);
            break;
        case 2:
            BUILD_PTR_CAST(INT16_PTR_TYPE);
            BUILD_TRUNC(value, INT16_TYPE);
            break;
        case 1:
            BUILD_PTR_CAST(INT8_PTR_TYPE);
            BUILD_TRUNC(value, INT8_TYPE);
            break;
        default:
            bh_assert(0);
            break;
    }

    if (atomic)
        BUILD_ATOMIC_STORE(align);
    else
        BUILD_STORE();
    return true;
fail:
    return false;
}

bool
aot_compile_op_i64_store(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 align, uint64 offset, uint32 bytes, bool atomic,
                         WASMRelocation *relocation)
{
    LLVMValueRef maddr, value;

    POP_I64(value);

    if (!(maddr = aot_check_memory_overflow(comp_ctx, func_ctx, offset, bytes,
                                            relocation)))
        return false;

    switch (bytes) {
        case 8:
            BUILD_PTR_CAST(INT64_PTR_TYPE);
            break;
        case 4:
            BUILD_PTR_CAST(INT32_PTR_TYPE);
            BUILD_TRUNC(value, I32_TYPE);
            break;
        case 2:
            BUILD_PTR_CAST(INT16_PTR_TYPE);
            BUILD_TRUNC(value, INT16_TYPE);
            break;
        case 1:
            BUILD_PTR_CAST(INT8_PTR_TYPE);
            BUILD_TRUNC(value, INT8_TYPE);
            break;
        default:
            bh_assert(0);
            break;
    }

    if (atomic)
        BUILD_ATOMIC_STORE(align);
    else
        BUILD_STORE();
    return true;
fail:
    return false;
}

bool
aot_compile_op_f32_store(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 align, uint64 offset,
                         WASMRelocation *relocation)
{
    LLVMValueRef maddr, value;

    POP_F32(value);

    if (!(maddr = aot_check_memory_overflow(comp_ctx, func_ctx, offset, 4,
                                            relocation)))
        return false;

    BUILD_PTR_CAST(F32_PTR_TYPE);
    BUILD_STORE();
    return true;
fail:
    return false;
}

bool
aot_compile_op_f64_store(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 align, uint64 offset,
                         WASMRelocation *relocation)
{
    LLVMValueRef maddr, value;

    POP_F64(value);

    if (!(maddr = aot_check_memory_overflow(comp_ctx, func_ctx, offset, 8,
                                            relocation)))
        return false;

    BUILD_PTR_CAST(F64_PTR_TYPE);
    BUILD_STORE();
    return true;
fail:
    return false;
}

static LLVMValueRef
get_memory_curr_page_count(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    LLVMValueRef cur_page_count_global, cur_page_count;

    cur_page_count_global =
        LLVMGetNamedGlobal(comp_ctx->module, "cur_page_count");
    bh_assert(cur_page_count_global);

    if (!(cur_page_count =
              LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, cur_page_count_global,
                             "cur_page_count"))) {
        aot_set_last_error("llvm build load failed.");
        goto fail;
    }

    return cur_page_count;
fail:
    return NULL;
}

bool
aot_compile_op_memory_size(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    LLVMValueRef mem_size = get_memory_curr_page_count(comp_ctx, func_ctx);

    if (mem_size) {
        if (IS_MEMORY64) {
            mem_size = LLVMBuildIntCast(comp_ctx->builder, mem_size, I64_TYPE,
                                        "mem_size_i64");
            if (!mem_size) {
                aot_set_last_error("llvm build int cast failed.");
                return false;
            }
            PUSH_I64(mem_size);
        }
        else
            PUSH_I32(mem_size);
    }
    return mem_size ? true : false;
fail:
    return false;
}

bool
aot_compile_op_memory_grow(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    LLVMValueRef memory_data_global, memory_data_size_global;
    LLVMValueRef num_bytes_per_page_global;
    LLVMValueRef cur_page_count_global, max_page_count_global;
    LLVMValueRef mem_bound_check_1byte_global, mem_bound_check_2bytes_global;
    LLVMValueRef mem_bound_check_4bytes_global, mem_bound_check_8bytes_global;
    LLVMValueRef mem_bound_check_16bytes_global;
    LLVMValueRef memory_data, memory_data_new;
    LLVMValueRef memory_data_size, memory_data_size_new;
    LLVMValueRef num_bytes_per_page;
    LLVMValueRef cur_page_count, max_page_count, max_left_page_count;
    LLVMValueRef inc_page_count, new_page_count;
    LLVMValueRef mem_bound_check_1byte, mem_bound_check_2bytes;
    LLVMValueRef mem_bound_check_4bytes, mem_bound_check_8bytes;
    LLVMValueRef mem_bound_check_16bytes, cmp, phi, bytes_const;
    LLVMValueRef num_bytes_per_page_u64, new_page_count_u64;
    LLVMValueRef memory_data_zeroed, memory_data_size_zeroed;
    LLVMValueRef memory_data_size_new_i32 = NULL;
    LLVMValueRef const_4G = I64_CONST(4 * (uint64)BH_GB);
    LLVMBasicBlockRef inc_page_count_non_zero;
    LLVMBasicBlockRef check_inc_page_count_succ;
    LLVMBasicBlockRef check_mem_data_size_new_succ = NULL;
    LLVMBasicBlockRef check_realloc_succ;
    LLVMBasicBlockRef cur_block;
    LLVMBasicBlockRef memory_grow_ret;
    LLVMTypeRef param_types[2], func_type;
    LLVMValueRef param_values[2], func;
    char func_name[32];

    memory_data_global = LLVMGetNamedGlobal(comp_ctx->module, "memory_data");
    bh_assert(memory_data_global);
    memory_data_size_global =
        LLVMGetNamedGlobal(comp_ctx->module, "memory_data_size");
    bh_assert(memory_data_size_global);
    num_bytes_per_page_global =
        LLVMGetNamedGlobal(comp_ctx->module, "num_bytes_per_page");
    bh_assert(num_bytes_per_page_global);
    cur_page_count_global =
        LLVMGetNamedGlobal(comp_ctx->module, "cur_page_count");
    bh_assert(cur_page_count_global);
    max_page_count_global =
        LLVMGetNamedGlobal(comp_ctx->module, "max_page_count");
    bh_assert(max_page_count_global);
    mem_bound_check_1byte_global =
        LLVMGetNamedGlobal(comp_ctx->module, "mem_bound_check_1byte");
    bh_assert(mem_bound_check_1byte_global);
    mem_bound_check_2bytes_global =
        LLVMGetNamedGlobal(comp_ctx->module, "mem_bound_check_2bytes");
    bh_assert(mem_bound_check_2bytes_global);
    mem_bound_check_4bytes_global =
        LLVMGetNamedGlobal(comp_ctx->module, "mem_bound_check_4bytes");
    bh_assert(mem_bound_check_4bytes_global);
    mem_bound_check_8bytes_global =
        LLVMGetNamedGlobal(comp_ctx->module, "mem_bound_check_8bytes");
    bh_assert(mem_bound_check_8bytes_global);
    mem_bound_check_16bytes_global =
        LLVMGetNamedGlobal(comp_ctx->module, "mem_bound_check_16bytes");
    bh_assert(mem_bound_check_16bytes_global);

    memory_data = LLVMBuildLoad2(comp_ctx->builder, INT8_PTR_TYPE,
                                 memory_data_global, "memory_data");
    memory_data_size =
        LLVMBuildLoad2(comp_ctx->builder, I64_TYPE, memory_data_size_global,
                       "memory_data_size");
    num_bytes_per_page =
        LLVMBuildLoad2(comp_ctx->builder, I32_TYPE, num_bytes_per_page_global,
                       "num_bytes_per_page");
    cur_page_count = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE,
                                    cur_page_count_global, "cur_page_count");
    max_page_count = LLVMBuildLoad2(comp_ctx->builder, I32_TYPE,
                                    max_page_count_global, "max_page_count");
    if (!memory_data || !memory_data_size || !num_bytes_per_page
        || !cur_page_count || !max_page_count) {
        aot_set_last_error("llvm build load failed.");
        goto fail;
    }

    cur_block = LLVMGetInsertBlock(comp_ctx->builder);

    ADD_BASIC_BLOCK(inc_page_count_non_zero, "inc_page_count_non_zero");
    MOVE_BLOCK_AFTER(inc_page_count_non_zero, cur_block);

    ADD_BASIC_BLOCK(check_inc_page_count_succ, "check_inc_page_count_succ");
    MOVE_BLOCK_AFTER(check_inc_page_count_succ, inc_page_count_non_zero);

    if (comp_ctx->pointer_size == sizeof(uint32)) {
        ADD_BASIC_BLOCK(check_mem_data_size_new_succ,
                        "check_mem_data_size_new_succ");
        MOVE_BLOCK_AFTER(check_mem_data_size_new_succ,
                         check_inc_page_count_succ);
        ADD_BASIC_BLOCK(check_realloc_succ, "check_realloc_succ");
        MOVE_BLOCK_AFTER(check_realloc_succ, check_mem_data_size_new_succ);
    }
    else {
        ADD_BASIC_BLOCK(check_realloc_succ, "check_realloc_succ");
        MOVE_BLOCK_AFTER(check_realloc_succ, check_inc_page_count_succ);
    }

    ADD_BASIC_BLOCK(memory_grow_ret, "memory_grow_ret");
    MOVE_BLOCK_AFTER(memory_grow_ret, check_realloc_succ);

    SET_BUILD_POS(memory_grow_ret);
    if (!(phi = LLVMBuildPhi(comp_ctx->builder, I32_TYPE, "phi"))) {
        aot_set_last_error("llvm build phi failed.");
        goto fail;
    }
    SET_BUILD_POS(cur_block);

    POP_I32(inc_page_count);
    BUILD_ICMP(LLVMIntEQ, inc_page_count, I32_ZERO, cmp, "is_zero");
    BUILD_COND_BR(cmp, memory_grow_ret, inc_page_count_non_zero);
    LLVMAddIncoming(phi, &cur_page_count, &cur_block, 1);

    SET_BUILD_POS(inc_page_count_non_zero);
    if (!(max_left_page_count =
              LLVMBuildSub(comp_ctx->builder, max_page_count, cur_page_count,
                           "max_left_page_count"))) {
        aot_set_last_error("llvm build sub failed.");
        goto fail;
    }
    BUILD_ICMP(LLVMIntUGT, inc_page_count, max_left_page_count, cmp, "cmp");
    BUILD_COND_BR(cmp, memory_grow_ret, check_inc_page_count_succ);
    LLVMAddIncoming(phi, &I32_NEG_ONE, &inc_page_count_non_zero, 1);

    SET_BUILD_POS(check_inc_page_count_succ);
    if (!(new_page_count = LLVMBuildAdd(comp_ctx->builder, cur_page_count,
                                        inc_page_count, "new_page_count"))) {
        aot_set_last_error("llvm build add failed.");
        goto fail;
    }
    if (!(num_bytes_per_page_u64 =
              LLVMBuildZExt(comp_ctx->builder, num_bytes_per_page, I64_TYPE,
                            "num_bytes_per_page_u64"))) {
        aot_set_last_error("llvm build zext failed.");
        goto fail;
    }
    if (!(new_page_count_u64 = LLVMBuildZExt(comp_ctx->builder, new_page_count,
                                             I64_TYPE, "new_page_count_u64"))) {
        aot_set_last_error("llvm build zext failed.");
        goto fail;
    }
    if (!(memory_data_size_new =
              LLVMBuildMul(comp_ctx->builder, num_bytes_per_page_u64,
                           new_page_count_u64, "memory_data_size_new"))) {
        aot_set_last_error("llvm build mul failed.");
        goto fail;
    }

    if (comp_ctx->pointer_size == sizeof(uint32)) {
        BUILD_ICMP(LLVMIntNE, memory_data_size_new, const_4G, cmp,
                   "memory_is_4GB");
        BUILD_COND_BR(cmp, check_mem_data_size_new_succ, memory_grow_ret);
        LLVMAddIncoming(phi, &I32_NEG_ONE, &check_inc_page_count_succ, 1);
        SET_BUILD_POS(check_mem_data_size_new_succ);
        if (!(memory_data_size_new_i32 =
                  LLVMBuildTrunc(comp_ctx->builder, memory_data_size_new,
                                 I32_TYPE, "memory_data_size_new_i32"))) {
            aot_set_last_error("llvm build trunc failed.");
            goto fail;
        }
    }

    param_types[0] = INT8_PTR_TYPE;
    param_types[1] =
        comp_ctx->pointer_size == sizeof(uint64) ? I64_TYPE : I32_TYPE;
    if (!(func_type = LLVMFunctionType(INT8_PTR_TYPE, param_types, 2, false))) {
        aot_set_last_error("create LLVM function type failed.");
        goto fail;
    }

    /* Call realloc function to re-allocate memory for wasm linear memory */
    snprintf(func_name, sizeof(func_name), "%s", "realloc");
    if (!(func = LLVMGetNamedFunction(comp_ctx->module, func_name))
        && !(func = LLVMAddFunction(comp_ctx->module, func_name, func_type))) {
        aot_set_last_error("add LLVM function failed.");
        goto fail;
    }

    param_values[0] = memory_data;
    param_values[1] = comp_ctx->pointer_size == sizeof(uint64)
                          ? memory_data_size_new
                          : memory_data_size_new_i32;
    if (!(memory_data_new =
              LLVMBuildCall2(comp_ctx->builder, func_type, func, param_values,
                             2, "memory_data_new"))) {
        aot_set_last_error("llvm build call failed.");
        goto fail;
    }

    /* Check the return value of realloc */
    if (!(cmp = LLVMBuildIsNotNull(comp_ctx->builder, memory_data_new,
                                   "not_null"))) {
        aot_set_last_error("llvm build is null failed.");
        goto fail;
    }

    BUILD_COND_BR(cmp, check_realloc_succ, memory_grow_ret);
    if (comp_ctx->pointer_size == sizeof(uint64))
        LLVMAddIncoming(phi, &I32_NEG_ONE, &check_inc_page_count_succ, 1);
    else
        LLVMAddIncoming(phi, &I32_NEG_ONE, &check_mem_data_size_new_succ, 1);

    SET_BUILD_POS(check_realloc_succ);
    if (!(memory_data_zeroed = LLVMBuildInBoundsGEP2(
              comp_ctx->builder, INT8_TYPE, memory_data_new, &memory_data_size,
              1, "memory_data_zeroed"))) {
        aot_set_last_error("llvm build in bound gep failed.");
        goto fail;
    }
    if (!(memory_data_size_zeroed =
              LLVMBuildSub(comp_ctx->builder, memory_data_size_new,
                           memory_data_size, "memory_data_size_zeroed"))) {
        aot_set_last_error("llvm build sub failed.");
        goto fail;
    }
    if (comp_ctx->pointer_size == sizeof(uint32)) {
        if (!(memory_data_size_zeroed =
                  LLVMBuildTrunc(comp_ctx->builder, memory_data_size_zeroed,
                                 I32_TYPE, "memory_data_size_zeroed_u32"))) {
            aot_set_last_error("llvm build trunc failed.");
            goto fail;
        }
    }
    if (!LLVMBuildMemSet(comp_ctx->builder, memory_data_zeroed, I8_ZERO,
                         memory_data_size_zeroed, 8)) {
        aot_set_last_error("llvm build memset failed.");
        goto fail;
    }

    if (!LLVMBuildStore(comp_ctx->builder, memory_data_new, memory_data_global)
        || !LLVMBuildStore(comp_ctx->builder, memory_data_size_new,
                           memory_data_size_global)
        || !LLVMBuildStore(comp_ctx->builder, new_page_count,
                           cur_page_count_global)) {
        aot_set_last_error("llvm build store failed.");
        goto fail;
    }

    bytes_const = I64_CONST(1);
    CHECK_LLVM_CONST(bytes_const);
    if (!(mem_bound_check_1byte =
              LLVMBuildSub(comp_ctx->builder, memory_data_size_new, bytes_const,
                           "mem_bound_check_1byte"))) {
        aot_set_last_error("llvm build sub failed.");
        goto fail;
    }
    if (!LLVMBuildStore(comp_ctx->builder, mem_bound_check_1byte,
                        mem_bound_check_1byte_global)) {
        aot_set_last_error("llvm build store failed.");
        goto fail;
    }

    bytes_const = I64_CONST(2);
    CHECK_LLVM_CONST(bytes_const);
    if (!(mem_bound_check_2bytes =
              LLVMBuildSub(comp_ctx->builder, memory_data_size_new, bytes_const,
                           "mem_bound_check_2bytes"))) {
        aot_set_last_error("llvm build sub failed.");
        goto fail;
    }
    if (!LLVMBuildStore(comp_ctx->builder, mem_bound_check_2bytes,
                        mem_bound_check_2bytes_global)) {
        aot_set_last_error("llvm build store failed.");
        goto fail;
    }

    bytes_const = I64_CONST(4);
    CHECK_LLVM_CONST(bytes_const);
    if (!(mem_bound_check_4bytes =
              LLVMBuildSub(comp_ctx->builder, memory_data_size_new, bytes_const,
                           "mem_bound_check_4bytes"))) {
        aot_set_last_error("llvm build sub failed.");
        goto fail;
    }
    if (!LLVMBuildStore(comp_ctx->builder, mem_bound_check_4bytes,
                        mem_bound_check_4bytes_global)) {
        aot_set_last_error("llvm build store failed.");
        goto fail;
    }

    bytes_const = I64_CONST(8);
    CHECK_LLVM_CONST(bytes_const);
    if (!(mem_bound_check_8bytes =
              LLVMBuildSub(comp_ctx->builder, memory_data_size_new, bytes_const,
                           "mem_bound_check_8bytes"))) {
        aot_set_last_error("llvm build sub failed.");
        goto fail;
    }
    if (!LLVMBuildStore(comp_ctx->builder, mem_bound_check_8bytes,
                        mem_bound_check_8bytes_global)) {
        aot_set_last_error("llvm build store failed.");
        goto fail;
    }

    bytes_const = I64_CONST(16);
    CHECK_LLVM_CONST(bytes_const);
    if (!(mem_bound_check_16bytes =
              LLVMBuildSub(comp_ctx->builder, memory_data_size_new, bytes_const,
                           "mem_bound_check_16bytes"))) {
        aot_set_last_error("llvm build sub failed.");
        goto fail;
    }
    if (!LLVMBuildStore(comp_ctx->builder, mem_bound_check_16bytes,
                        mem_bound_check_16bytes_global)) {
        aot_set_last_error("llvm build store failed.");
        goto fail;
    }

    LLVMAddIncoming(phi, &cur_page_count, &check_realloc_succ, 1);
    LLVMBuildBr(comp_ctx->builder, memory_grow_ret);

    SET_BUILD_POS(memory_grow_ret);

    PUSH_I32(phi);
    return true;
fail:
    return false;
}

static LLVMValueRef
check_bulk_memory_overflow(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                           LLVMValueRef offset, LLVMValueRef bytes)
{
    LLVMValueRef maddr, max_addr, mem_base_addr, cmp;
    LLVMValueRef mem_data_size, mem_data_size_global;
    LLVMBasicBlockRef block_curr = LLVMGetInsertBlock(comp_ctx->builder);
    LLVMBasicBlockRef check_succ;

    if (!(mem_base_addr = aot_get_memory_base_addr(comp_ctx, func_ctx)))
        return false;

    mem_data_size_global =
        LLVMGetNamedGlobal(comp_ctx->module, "memory_data_size");
    bh_assert(mem_data_size_global);

    if (!(mem_data_size =
              LLVMBuildLoad2(comp_ctx->builder, I64_TYPE, mem_data_size_global,
                             "mem_data_size"))) {
        aot_set_last_error("llvm build load failed.");
        goto fail;
    }

    offset =
        LLVMBuildZExt(comp_ctx->builder, offset, I64_TYPE, "extend_offset");
    bytes = LLVMBuildZExt(comp_ctx->builder, bytes, I64_TYPE, "extend_len");

    BUILD_OP(Add, offset, bytes, max_addr, "max_addr");

    if (!comp_ctx->no_sandbox_mode) {
        BUILD_ICMP(LLVMIntUGT, max_addr, mem_data_size, cmp,
                   "cmp_max_mem_addr");
        ADD_BASIC_BLOCK(check_succ, "check_succ");
        LLVMMoveBasicBlockAfter(check_succ, block_curr);
        if (!aot_emit_exception(comp_ctx, func_ctx,
                                EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS, true, cmp,
                                check_succ)) {
            goto fail;
        }
    }

    /* maddr = mem_base_addr + offset */
    if (!(maddr = LLVMBuildInBoundsGEP2(comp_ctx->builder, INT8_TYPE,
                                        mem_base_addr, &offset, 1, "maddr"))) {
        aot_set_last_error("llvm build add failed.");
        goto fail;
    }
    return maddr;
fail:
    return NULL;
}

bool
aot_compile_op_memory_copy(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    LLVMValueRef src, dst, src_addr, dst_addr, len, res;

    POP_MEM_OFFSET(len);
    POP_MEM_OFFSET(src);
    POP_MEM_OFFSET(dst);

    if (!(src_addr = check_bulk_memory_overflow(comp_ctx, func_ctx, src, len)))
        return false;

    if (!(dst_addr = check_bulk_memory_overflow(comp_ctx, func_ctx, dst, len)))
        return false;

    if (!zero_extend_u64(comp_ctx, &len, "len64")) {
        return false;
    }

    if (!(res = LLVMBuildMemMove(comp_ctx->builder, dst_addr, 1, src_addr, 1,
                                 len))) {
        aot_set_last_error("llvm build memmove failed.");
        return false;
    }

    return true;
fail:
    return false;
}

bool
aot_compile_op_memory_fill(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    LLVMValueRef val, dst, dst_addr, len, res;
    LLVMTypeRef param_types[3], ret_type, func_type, func_ptr_type;
    LLVMValueRef func, params[3];

    POP_MEM_OFFSET(len);
    POP_I32(val);
    POP_MEM_OFFSET(dst);

    if (!(dst_addr = check_bulk_memory_overflow(comp_ctx, func_ctx, dst, len)))
        return false;

    /* TODO */
    param_types[0] = INT8_PTR_TYPE;
    param_types[1] = I32_TYPE;
    param_types[2] = I32_TYPE;
    ret_type = INT8_PTR_TYPE;

    if (!(func_type = LLVMFunctionType(ret_type, param_types, 3, false))) {
        aot_set_last_error("create LLVM function type failed.");
        return false;
    }

    if (!(func_ptr_type = LLVMPointerType(func_type, 0))) {
        aot_set_last_error("create LLVM function pointer type failed.");
        return false;
    }

    if (!(func = LLVMGetNamedFunction(func_ctx->module, "memset"))
        && !(func = LLVMAddFunction(func_ctx->module, "memset", func_type))) {
        aot_set_last_error("llvm add function failed.");
        return false;
    }

    params[0] = dst_addr;
    params[1] = val;
    params[2] = len;
    if (!(res = LLVMBuildCall2(comp_ctx->builder, func_type, func, params, 3,
                               "call_memset"))) {
        aot_set_last_error("llvm build memset failed.");
        return false;
    }

    return true;
fail:
    return false;
}

bool
aot_compile_op_atomic_rmw(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                          uint8 atomic_op, uint8 op_type, uint32 align,
                          uint64 offset, uint32 bytes)
{
    LLVMValueRef maddr, value, result;

    if (op_type == VALUE_TYPE_I32)
        POP_I32(value);
    else
        POP_I64(value);

    if (!(maddr = aot_check_memory_overflow(comp_ctx, func_ctx, offset, bytes,
                                            NULL)))
        return false;

    if (!check_memory_alignment(comp_ctx, func_ctx, maddr, align))
        return false;

    switch (bytes) {
        case 8:
            BUILD_PTR_CAST(INT64_PTR_TYPE);
            break;
        case 4:
            BUILD_PTR_CAST(INT32_PTR_TYPE);
            if (op_type == VALUE_TYPE_I64)
                BUILD_TRUNC(value, I32_TYPE);
            break;
        case 2:
            BUILD_PTR_CAST(INT16_PTR_TYPE);
            BUILD_TRUNC(value, INT16_TYPE);
            break;
        case 1:
            BUILD_PTR_CAST(INT8_PTR_TYPE);
            BUILD_TRUNC(value, INT8_TYPE);
            break;
        default:
            bh_assert(0);
            break;
    }

    if (!(result = LLVMBuildAtomicRMW(
              comp_ctx->builder, atomic_op, maddr, value,
              LLVMAtomicOrderingSequentiallyConsistent, false))) {
        goto fail;
    }

    LLVMSetVolatile(result, true);

    if (op_type == VALUE_TYPE_I32) {
        if (!(result = LLVMBuildZExt(comp_ctx->builder, result, I32_TYPE,
                                     "result_i32"))) {
            goto fail;
        }
        PUSH_I32(result);
    }
    else {
        if (!(result = LLVMBuildZExt(comp_ctx->builder, result, I64_TYPE,
                                     "result_i64"))) {
            goto fail;
        }
        PUSH_I64(result);
    }

    return true;
fail:
    return false;
}

bool
aot_compile_op_atomic_cmpxchg(AOTCompContext *comp_ctx,
                              AOTFuncContext *func_ctx, uint8 op_type,
                              uint32 align, uint64 offset, uint32 bytes)
{
    LLVMValueRef maddr, value, expect, result;

    if (op_type == VALUE_TYPE_I32) {
        POP_I32(value);
        POP_I32(expect);
    }
    else {
        POP_I64(value);
        POP_I64(expect);
    }

    if (!(maddr = aot_check_memory_overflow(comp_ctx, func_ctx, offset, bytes,
                                            NULL)))
        return false;

    if (!check_memory_alignment(comp_ctx, func_ctx, maddr, align))
        return false;

    switch (bytes) {
        case 8:
            BUILD_PTR_CAST(INT64_PTR_TYPE);
            break;
        case 4:
            BUILD_PTR_CAST(INT32_PTR_TYPE);
            if (op_type == VALUE_TYPE_I64) {
                BUILD_TRUNC(value, I32_TYPE);
                BUILD_TRUNC(expect, I32_TYPE);
            }
            break;
        case 2:
            BUILD_PTR_CAST(INT16_PTR_TYPE);
            BUILD_TRUNC(value, INT16_TYPE);
            BUILD_TRUNC(expect, INT16_TYPE);
            break;
        case 1:
            BUILD_PTR_CAST(INT8_PTR_TYPE);
            BUILD_TRUNC(value, INT8_TYPE);
            BUILD_TRUNC(expect, INT8_TYPE);
            break;
        default:
            bh_assert(0);
            break;
    }

    if (!(result = LLVMBuildAtomicCmpXchg(
              comp_ctx->builder, maddr, expect, value,
              LLVMAtomicOrderingSequentiallyConsistent,
              LLVMAtomicOrderingSequentiallyConsistent, false))) {
        goto fail;
    }

    LLVMSetVolatile(result, true);

    /* CmpXchg return {i32, i1} structure,
       we need to extrack the previous_value from the structure */
    if (!(result = LLVMBuildExtractValue(comp_ctx->builder, result, 0,
                                         "previous_value"))) {
        goto fail;
    }

    if (op_type == VALUE_TYPE_I32) {
        if (LLVMTypeOf(result) != I32_TYPE) {
            if (!(result = LLVMBuildZExt(comp_ctx->builder, result, I32_TYPE,
                                         "result_i32"))) {
                goto fail;
            }
        }
        PUSH_I32(result);
    }
    else {
        if (LLVMTypeOf(result) != I64_TYPE) {
            if (!(result = LLVMBuildZExt(comp_ctx->builder, result, I64_TYPE,
                                         "result_i64"))) {
                goto fail;
            }
        }
        PUSH_I64(result);
    }

    return true;
fail:
    return false;
}

bool
aot_compiler_op_atomic_fence(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx)
{
    return LLVMBuildFence(comp_ctx->builder,
                          LLVMAtomicOrderingSequentiallyConsistent, false, "")
               ? true
               : false;
}
