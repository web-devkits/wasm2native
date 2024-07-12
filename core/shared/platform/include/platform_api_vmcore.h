/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _PLATFORM_API_VMCORE_H
#define _PLATFORM_API_VMCORE_H

#include "platform_common.h"
#include "platform_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the platform internal resources if needed
 *
 * @return 0 if success
 */
int
bh_platform_init(void);

/**
 * Destroy the platform internal resources if needed,
 * this function is called by wasm_runtime_destroy()
 */
void
bh_platform_destroy(void);

/**
 ******** memory allocator APIs **********
 */

void *
os_malloc(unsigned size);

void *
os_realloc(void *ptr, unsigned size);

void
os_free(void *ptr);

int
os_printf(const char *format, ...);

int
os_vprintf(const char *format, va_list ap);

/**
 * Get microseconds after boot.
 */
uint64
os_time_get_boot_us(void);

/**
 * Get current thread id.
 * Implementation optional: Used by runtime for logging only.
 */
korp_tid
os_self_thread(void);

/**
 ************** mutext APIs ***********
 *  vmcore:  Not required until pthread is supported by runtime
 *  app-mgr: Must be implemented
 */

int
os_mutex_init(korp_mutex *mutex);

int
os_mutex_destroy(korp_mutex *mutex);

int
os_mutex_lock(korp_mutex *mutex);

int
os_mutex_unlock(korp_mutex *mutex);

#ifdef __cplusplus
}
#endif

#endif /* #ifndef _PLATFORM_API_VMCORE_H */
