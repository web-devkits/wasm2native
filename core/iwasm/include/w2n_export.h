/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _W2N_EXPORT_H
#define _W2N_EXPORT_H

#include <stdint.h>
#include <stdbool.h>

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

typedef struct WASMExportApi {
    const char *func_name;
    const char *signature;
    const void *func_ptr;
} WASMExportApi;

void
wasm_instance_create(void);

void
wasm_instance_destroy(void);

bool
wasm_instance_is_created(void);

uint8_t *
wasm_get_memory(void);

uint64_t
wasm_get_memory_size(void);

void *
wasm_get_heap_handle(void);

int32_t
wasm_get_exception(void);

const char *
wasm_get_exception_msg(void);

void
wasm_set_exception(int32_t exception_id);

void *
mem_allocator_malloc(void *allocator, uint32_t size);

void *
mem_allocator_realloc(void *allocator, void *ptr, uint32_t size);

void
mem_allocator_free(void *allocator, void *ptr);

WASMExportApi *
wasm_get_export_apis(void);

uint32_t
wasm_get_export_api_num(void);

#ifdef __cplusplus
}
#endif

#endif /* end of _W2N_EXPORT_H */
