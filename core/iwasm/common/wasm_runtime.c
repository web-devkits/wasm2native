/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "wasm_runtime.h"
#include "wasm_loader.h"
#include "../compilation/aot_llvm.h"
#include "../../version.h"

static void
set_error_buf(char *error_buf, uint32 error_buf_size, const char *string)
{
    if (error_buf != NULL)
        snprintf(error_buf, error_buf_size, "%s", string);
}

bool
wasm_runtime_init()
{
    if (bh_platform_init() != 0)
        return false;

    if (!aot_compiler_init()) {
        bh_platform_destroy();
        return false;
    }

    return true;
}

void
wasm_runtime_destroy()
{
    aot_compiler_destroy();

    bh_platform_destroy();
}

void
wasm_runtime_set_log_level(log_level_t level)
{
    bh_log_set_verbose_level(level);
}

WASMModule *
wasm_runtime_load(uint8 *buf, uint32 size, char *error_buf,
                  uint32 error_buf_size)
{
    if (size < 4) { /* length of MAGIC NUMBER */
        set_error_buf(error_buf, error_buf_size,
                      "WASM module load failed: unexpected end");
        return NULL;
    }
    else if (!(buf[0] == '\0' && buf[1] == 'a' && buf[2] == 's'
               && buf[3] == 'm')) {
        set_error_buf(error_buf, error_buf_size,
                      "WASM module load failed: magic header not detected");
        return NULL;
    }
    else {
        WASMModule *module = NULL;

        module = wasm_loader_load(buf, size, error_buf, error_buf_size);
        return module;
    }
}

void
wasm_runtime_unload(WASMModule *module)
{
    wasm_loader_unload(module);
}

static union {
    int a;
    char b;
} __ue = { .a = 1 };

#define is_little_endian() (__ue.b == 1) /* NOLINT */

static void
exchange_uint32(uint8 *p_data)
{
    uint8 value = *p_data;
    *p_data = *(p_data + 3);
    *(p_data + 3) = value;

    value = *(p_data + 1);
    *(p_data + 1) = *(p_data + 2);
    *(p_data + 2) = value;
}

static void
exchange_uint64(uint8 *p_data)
{
    uint32 value;

    value = *(uint32 *)p_data;
    *(uint32 *)p_data = *(uint32 *)(p_data + 4);
    *(uint32 *)(p_data + 4) = value;
    exchange_uint32(p_data);
    exchange_uint32(p_data + 4);
}

void
wasm_runtime_read_v128(const uint8 *bytes, uint64 *ret1, uint64 *ret2)
{
    uint64 u1, u2;

    bh_memcpy_s(&u1, 8, bytes, 8);
    bh_memcpy_s(&u2, 8, bytes + 8, 8);

    if (!is_little_endian()) {
        exchange_uint64((uint8 *)&u1);
        exchange_uint64((uint8 *)&u2);
        *ret1 = u2;
        *ret2 = u1;
    }
    else {
        *ret1 = u1;
        *ret2 = u2;
    }
}

void
wasm_runtime_get_version(uint32 *p_major, uint32 *p_minor, uint32 *p_patch)
{
    *p_major = W2N_VERSION_MAJOR;
    *p_minor = W2N_VERSION_MINOR;
    *p_patch = W2N_VERSION_PATCH;
}
