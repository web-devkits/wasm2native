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

/**
 * This file declares the APIs exported in the compiled native binary of
 * sandbox mode.
 */

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

/**
 * Create the wasm instance, use wasm_instance_is_created() to
 * check whether it is created successfully. If not, developer
 * can use wasm_get_exception() and wasm_get_exception_msg() to
 * get the exception info.
 */
void
wasm_instance_create(void);

/**
 * Destroy the wasm instance
 */
void
wasm_instance_destroy(void);

/**
 * Check whether the wasm instance is created
 */
bool
wasm_instance_is_created(void);

/**
 * Get the base address of the wasm linear memory
 */
uint8_t *
wasm_get_memory(void);

/**
 * Get the total size of the wasm linear memory
 */
uint64_t
wasm_get_memory_size(void);

/**
 * Get the heap handle of the host-managed heap which resides in the
 * wasm linear memory, it may be created if `--heap-size=n` is specified
 * for wasm2native tool to emit the object file.
 * If it is not NULL, developer can pass it to mem_allocator_malloc()
 * to allocate memory from this heap.
 */
void *
wasm_get_heap_handle(void);

/**
 * Get the exception id, no exception was thrown if it is 0, otherwise
 * it is one of the values in enum WASMExceptionID
 */
int32_t
wasm_get_exception(void);

/**
 * Get the exception message, no exception was thrown if it is NULL
 */
const char *
wasm_get_exception_msg(void);

/**
 * Set the exception id, if it is 0, the exception will be cleared
 */
void
wasm_set_exception(int32_t exception_id);

/**
 * Allocate memory from the memory allocator
 *
 * @param allocator the memory allocator, e.g., the return value of
 *        wasm_get_heap_handle()
 * @param size the size to allocate
 *
 * @return the memory allocated, NULL if failed
 */
void *
mem_allocator_malloc(void *allocator, uint32_t size);

/**
 * Re-allocate memory from the memory allocator
 */
void *
mem_allocator_realloc(void *allocator, void *ptr, uint32_t size);

/**
 * Free the memory allocated from the memory allocator
 */
void
mem_allocator_free(void *allocator, void *ptr);

/**
 * Get the exported API info array of the native binary, these APIs
 * are exported in the related wasm file. Developer can get the API
 * number with wasm_get_export_api_num() and then retrieve the array
 * with function name and function signature to get the function
 * pointer, and then call it.
 */
WASMExportApi *
wasm_get_export_apis(void);

/**
 * Get the exported API number of the native binary
 */
uint32_t
wasm_get_export_api_num(void);

#ifdef __cplusplus
}
#endif

#endif /* end of _W2N_EXPORT_H */
