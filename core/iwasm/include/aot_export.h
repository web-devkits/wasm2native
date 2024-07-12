/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _AOT_EXPORT_H
#define _AOT_EXPORT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WASMExportApi {
    const char *func_name;
    const char *signature;
    const void *func_ptr;
} WASMExportApi;

bool
wasm_instance_create(void);

void
wasm_instance_destroy(void);

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

#endif /* end of _AOT_EXPORT_H */
