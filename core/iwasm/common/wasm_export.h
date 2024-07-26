/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _WASM_EXPORT_H
#define _WASM_EXPORT_H

#include <stdint.h>
#include <stdbool.h>

#include "aot_comp_option.h"

#ifdef __cplusplus
extern "C" {
#endif

/* WASM module loaded from WASM binary file */
struct WASMModule;
typedef struct WASMModule *wasm_module_t;

/* WASM section */
typedef struct wasm_section_t {
    struct wasm_section_t *next;
    /* section type */
    int section_type;
    /* section index in wasm file */
    uint32_t section_index;
    /* section body, not include type and size */
    uint8_t *section_body;
    /* section body size */
    uint32_t section_body_size;
} wasm_section_t, *wasm_section_list_t;

typedef enum {
    WASM_LOG_LEVEL_FATAL = 0,
    WASM_LOG_LEVEL_ERROR = 1,
    WASM_LOG_LEVEL_WARNING = 2,
    WASM_LOG_LEVEL_DEBUG = 3,
    WASM_LOG_LEVEL_VERBOSE = 4
} log_level_t;

struct AOTCompData;
typedef struct AOTCompData *aot_comp_data_t;

struct AOTCompContext;
typedef struct AOTCompContext *aot_comp_context_t;

typedef enum {
    AOT_OBJECT_FILE,
    AOT_LLVMIR_UNOPT_FILE,
    AOT_LLVMIR_OPT_FILE,
} aot_file_format_t;

/**
 * Initialize the WASM runtime environment, and also initialize
 * the memory allocator with system allocator, which calls os_malloc
 * to allocate memory
 *
 * @return true if success, false otherwise
 */
bool
wasm_runtime_init(void);

/**
 * Set the log level. To be called after the runtime is initialized.
 *
 * @param level the log level to set
 */
void
wasm_runtime_set_log_level(log_level_t level);

/**
 * Destroy the WASM runtime environment.
 */
void
wasm_runtime_destroy(void);

/**
 * Allocate memory from runtime memory environment.
 *
 * @param size bytes need to allocate
 *
 * @return the pointer to memory allocated
 */
void *
wasm_runtime_malloc(unsigned int size);

/**
 * Reallocate memory from runtime memory environment
 *
 * @param ptr the original memory
 * @param size bytes need to reallocate
 *
 * @return the pointer to memory reallocated
 */
void *
wasm_runtime_realloc(void *ptr, unsigned int size);

/*
 * Free memory to runtime memory environment.
 */
void
wasm_runtime_free(void *ptr);

/**
 * Load a WASM module from a specified byte buffer of WASM binary data.
 *
 * Note: the runtime can make modifications to the buffer for its internal
 * purposes. Thus, in general, it isn't safe to create multiple modules
 * from a single buffer.
 *
 * @param buf the byte buffer which contains the WASM/AOT binary data,
 *        note that the byte buffer must be writable since runtime may
 *        change its content for footprint and performance purpose, and
 *        it must be referencable until wasm_runtime_unload is called
 * @param size the size of the buffer
 * @param error_buf output of the exception info
 * @param error_buf_size the size of the exception string
 *
 * @return return WASM module loaded, NULL if failed
 */
wasm_module_t
wasm_runtime_load(uint8_t *buf, uint32_t size, char *error_buf,
                  uint32_t error_buf_size);

/**
 * Unload a WASM module.
 *
 * @param module the module to be unloaded
 */
void
wasm_runtime_unload(wasm_module_t module);

/**
 * Get wasm2native semantic version
 */
void
wasm_runtime_get_version(uint32_t *major, uint32_t *minor, uint32_t *patch);

/**
 * Create the compiler data from a wasm module
 */
aot_comp_data_t
aot_create_comp_data(wasm_module_t wasm_module, aot_comp_option_t option);

/**
 * Destroy the compiler data
 */
void
aot_destroy_comp_data(aot_comp_data_t comp_data);

bool
aot_compiler_init(void);

void
aot_compiler_destroy(void);

aot_comp_context_t
aot_create_comp_context(aot_comp_data_t comp_data, aot_comp_option_t option);

void
aot_destroy_comp_context(aot_comp_context_t comp_ctx);

bool
aot_compile_wasm(aot_comp_context_t comp_ctx);

bool
aot_emit_llvm_file(aot_comp_context_t comp_ctx, const char *file_name);

bool
aot_emit_object_file(aot_comp_context_t comp_ctx, const char *file_name);

const char *
aot_get_last_error();

#ifdef __cplusplus
}
#endif

#endif /* end of _WASM_EXPORT_H */
