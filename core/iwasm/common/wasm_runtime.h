/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _WASM_RUNTIME_H
#define _WASM_RUNTIME_H

#include "wasm.h"
#include "wasm_export.h"
#include "../include/w2n_export.h"

#ifdef __cplusplus
extern "C" {
#endif

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

#ifdef __cplusplus
}
#endif

#endif /* end of _WASM_RUNTIME_H */
