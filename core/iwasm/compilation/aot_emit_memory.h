/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _AOT_EMIT_MEMORY_H_
#define _AOT_EMIT_MEMORY_H_

#include "aot_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

bool
aot_compile_op_i32_load(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                        uint32 align, uint64 offset, uint32 bytes, bool sign,
                        bool atomic, WASMRelocation *relocation);

bool
aot_compile_op_i64_load(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                        uint32 align, uint64 offset, uint32 bytes, bool sign,
                        bool atomic, WASMRelocation *relocation);

bool
aot_compile_op_f32_load(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                        uint32 align, uint64 offset,
                        WASMRelocation *relocation);

bool
aot_compile_op_f64_load(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                        uint32 align, uint64 offset,
                        WASMRelocation *relocation);

bool
aot_compile_op_i32_store(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 align, uint64 offset, uint32 bytes, bool atomic,
                         WASMRelocation *relocation);

bool
aot_compile_op_i64_store(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 align, uint64 offset, uint32 bytes, bool atomic,
                         WASMRelocation *relocation);

bool
aot_compile_op_f32_store(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 align, uint64 offset,
                         WASMRelocation *relocation);

bool
aot_compile_op_f64_store(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                         uint32 align, uint64 offset,
                         WASMRelocation *relocation);

LLVMValueRef
aot_check_memory_overflow(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                          uint64 offset, uint32 bytes,
                          WASMRelocation *relocation);

bool
aot_compile_op_memory_size(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx);

bool
aot_compile_op_memory_grow(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx);

bool
aot_compile_op_memory_copy(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx);

bool
aot_compile_op_memory_fill(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx);

bool
aot_compile_op_atomic_rmw(AOTCompContext *comp_ctx, AOTFuncContext *func_ctx,
                          uint8 atomic_op, uint8 op_type, uint32 align,
                          uint64 offset, uint32 bytes);

bool
aot_compile_op_atomic_cmpxchg(AOTCompContext *comp_ctx,
                              AOTFuncContext *func_ctx, uint8 op_type,
                              uint32 align, uint64 offset, uint32 bytes);

bool
aot_compiler_op_atomic_fence(AOTCompContext *comp_ctx,
                             AOTFuncContext *func_ctx);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif /* end of _AOT_EMIT_MEMORY_H_ */
