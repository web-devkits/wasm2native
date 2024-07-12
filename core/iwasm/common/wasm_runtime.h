/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _WASM_RUNTIME_H
#define _WASM_RUNTIME_H

#include "wasm.h"
#include "../include/wasm_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum WASMExceptionID {
    EXCE_UNREACHABLE = -100,
    EXCE_OUT_OF_BOUNDS_MEMORY_ACCESS,
    EXCE_OUT_OF_BOUNDS_TABLE_ACCESS,
    EXCE_INTEGER_OVERFLOW,
    EXCE_INTEGER_DIVIDE_BY_ZERO,
    EXCE_INVALID_CONVERSION_TO_INTEGER,
    EXCE_INVALID_FUNCTION_TYPE_INDEX,
    EXCE_UNDEFINED_ELEMENT,
    EXCE_UNINITIALIZED_ELEMENT,
    EXCE_CALL_UNLINKED_IMPORT_FUNC,
    EXCE_NATIVE_STACK_OVERFLOW,
    EXCE_UNALIGNED_ATOMIC,
    EXCE_AUX_STACK_OVERFLOW,
    EXCE_AUX_STACK_UNDERFLOW,
    EXCE_ALLOCATE_MEMORY_FAILED,
    EXCE_LOOKUP_ENTRY_SYMBOL_FAILED,
    EXCE_LOOKUP_FUNCTION_FAILED,
    EXCE_INVALID_INPUT_ARGUMENT_COUNT,
    EXCE_INVALID_INPUT_ARGUMENT,
    EXCE_HOST_MANAGED_HEAP_NOT_FOUND,
    EXCE_QUICK_CALL_ENTRY_NOT_FOUND,
    EXCE_UNKNOWN_ERROR,

    EXCE_ID_MIN = EXCE_UNREACHABLE,
    EXCE_ID_MAX = EXCE_UNKNOWN_ERROR,
} WASMExceptionID;

typedef wasm_section_t WASMSection;

bool
wasm_runtime_init(void);

void
wasm_runtime_destroy(void);

WASMModule *
wasm_runtime_load(uint8 *buf, uint32 size, char *error_buf,
                  uint32 error_buf_size);

void
wasm_runtime_unload(WASMModule *module);

void
wasm_runtime_read_v128(const uint8 *bytes, uint64 *ret1, uint64 *ret2);

void
wasm_runtime_destroy_custom_sections(WASMCustomSection *section_list);

#ifdef __cplusplus
}
#endif

#endif /* end of _WASM_RUNTIME_H */
