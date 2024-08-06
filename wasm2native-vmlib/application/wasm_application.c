/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include "../common/wasm_runtime.h"
#include "w2n_export.h"

#define PUT_I64_TO_ADDR(addr, value)         \
    do {                                     \
        uint32 *addr_u32 = (uint32 *)(addr); \
        union {                              \
            int64 val;                       \
            uint32 parts[2];                 \
        } u;                                 \
        u.val = (int64)(value);              \
        addr_u32[0] = u.parts[0];            \
        addr_u32[1] = u.parts[1];            \
    } while (0)

static inline int64
GET_I64_FROM_ADDR(uint32 *addr)
{
    union {
        int64 val;
        uint32 parts[2];
    } u;
    u.parts[0] = addr[0];
    u.parts[1] = addr[1];
    return u.val;
}

static void
invoke_no_args_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(void) = func_ptr;
    native_code();
}
static void
invoke_no_args_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(void) = func_ptr;
    argv_ret[0] = native_code();
}
static void
invoke_no_args_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(void) = func_ptr;
    int64 ret = native_code();
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_i_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32) = func_ptr;
    native_code(argv[0]);
}
static void
invoke_i_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32) = func_ptr;
    argv_ret[0] = native_code(argv[0]);
}
static void
invoke_i_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32) = func_ptr;
    int64 ret = native_code(argv[0]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_I_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv));
}
static void
invoke_I_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv));
}
static void
invoke_I_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_ii_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int32) = func_ptr;
    native_code(argv[0], argv[1]);
}
static void
invoke_ii_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int32) = func_ptr;
    argv_ret[0] = native_code(argv[0], argv[1]);
}
static void
invoke_ii_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int32) = func_ptr;
    int64 ret = native_code(argv[0], argv[1]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iI_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int64) = func_ptr;
    native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1));
}
static void
invoke_iI_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int64) = func_ptr;
    argv_ret[0] = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1));
}
static void
invoke_iI_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int64) = func_ptr;
    int64 ret = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_Ii_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int32) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2]);
}
static void
invoke_Ii_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int32) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2]);
}
static void
invoke_Ii_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int32) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_II_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int64) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                GET_I64_FROM_ADDR((uint32 *)argv + 2));
}
static void
invoke_II_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int64) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                              GET_I64_FROM_ADDR((uint32 *)argv + 2));
}
static void
invoke_II_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int64) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                            GET_I64_FROM_ADDR((uint32 *)argv + 2));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iii_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int32, int32) = func_ptr;
    native_code(argv[0], argv[1], argv[2]);
}
static void
invoke_iii_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int32, int32) = func_ptr;
    argv_ret[0] = native_code(argv[0], argv[1], argv[2]);
}
static void
invoke_iii_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int32, int32) = func_ptr;
    int64 ret = native_code(argv[0], argv[1], argv[2]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iiI_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int32, int64) = func_ptr;
    native_code(argv[0], argv[1], GET_I64_FROM_ADDR((uint32 *)argv + 2));
}
static void
invoke_iiI_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int32, int64) = func_ptr;
    argv_ret[0] =
        native_code(argv[0], argv[1], GET_I64_FROM_ADDR((uint32 *)argv + 2));
}
static void
invoke_iiI_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int32, int64) = func_ptr;
    int64 ret =
        native_code(argv[0], argv[1], GET_I64_FROM_ADDR((uint32 *)argv + 2));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iIi_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int64, int32) = func_ptr;
    native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1), argv[3]);
}
static void
invoke_iIi_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int64, int32) = func_ptr;
    argv_ret[0] =
        native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1), argv[3]);
}
static void
invoke_iIi_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int64, int32) = func_ptr;
    int64 ret =
        native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1), argv[3]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iII_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int64, int64) = func_ptr;
    native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                GET_I64_FROM_ADDR((uint32 *)argv + 3));
}
static void
invoke_iII_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int64, int64) = func_ptr;
    argv_ret[0] = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                              GET_I64_FROM_ADDR((uint32 *)argv + 3));
}
static void
invoke_iII_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int64, int64) = func_ptr;
    int64 ret = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                            GET_I64_FROM_ADDR((uint32 *)argv + 3));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_Iii_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int32, int32) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2], argv[3]);
}
static void
invoke_Iii_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int32, int32) = func_ptr;
    argv_ret[0] =
        native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2], argv[3]);
}
static void
invoke_Iii_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int32, int32) = func_ptr;
    int64 ret =
        native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2], argv[3]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_IiI_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int32, int64) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                GET_I64_FROM_ADDR((uint32 *)argv + 3));
}
static void
invoke_IiI_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int32, int64) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                              GET_I64_FROM_ADDR((uint32 *)argv + 3));
}
static void
invoke_IiI_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int32, int64) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                            GET_I64_FROM_ADDR((uint32 *)argv + 3));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_IIi_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int64, int32) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4]);
}
static void
invoke_IIi_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int64, int32) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                              GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4]);
}
static void
invoke_IIi_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int64, int32) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                            GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_III_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int64, int64) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                GET_I64_FROM_ADDR((uint32 *)argv + 2),
                GET_I64_FROM_ADDR((uint32 *)argv + 4));
}
static void
invoke_III_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int64, int64) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                              GET_I64_FROM_ADDR((uint32 *)argv + 2),
                              GET_I64_FROM_ADDR((uint32 *)argv + 4));
}
static void
invoke_III_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int64, int64) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                            GET_I64_FROM_ADDR((uint32 *)argv + 2),
                            GET_I64_FROM_ADDR((uint32 *)argv + 4));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iiii_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int32, int32, int32) = func_ptr;
    native_code(argv[0], argv[1], argv[2], argv[3]);
}
static void
invoke_iiii_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int32, int32, int32) = func_ptr;
    argv_ret[0] = native_code(argv[0], argv[1], argv[2], argv[3]);
}
static void
invoke_iiii_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int32, int32, int32) = func_ptr;
    int64 ret = native_code(argv[0], argv[1], argv[2], argv[3]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iiiI_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int32, int32, int64) = func_ptr;
    native_code(argv[0], argv[1], argv[2],
                GET_I64_FROM_ADDR((uint32 *)argv + 3));
}
static void
invoke_iiiI_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int32, int32, int64) = func_ptr;
    argv_ret[0] = native_code(argv[0], argv[1], argv[2],
                              GET_I64_FROM_ADDR((uint32 *)argv + 3));
}
static void
invoke_iiiI_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int32, int32, int64) = func_ptr;
    int64 ret = native_code(argv[0], argv[1], argv[2],
                            GET_I64_FROM_ADDR((uint32 *)argv + 3));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iiIi_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int32, int64, int32) = func_ptr;
    native_code(argv[0], argv[1], GET_I64_FROM_ADDR((uint32 *)argv + 2),
                argv[4]);
}
static void
invoke_iiIi_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int32, int64, int32) = func_ptr;
    argv_ret[0] = native_code(argv[0], argv[1],
                              GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4]);
}
static void
invoke_iiIi_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int32, int64, int32) = func_ptr;
    int64 ret = native_code(argv[0], argv[1],
                            GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iiII_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int32, int64, int64) = func_ptr;
    native_code(argv[0], argv[1], GET_I64_FROM_ADDR((uint32 *)argv + 2),
                GET_I64_FROM_ADDR((uint32 *)argv + 4));
}
static void
invoke_iiII_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int32, int64, int64) = func_ptr;
    argv_ret[0] =
        native_code(argv[0], argv[1], GET_I64_FROM_ADDR((uint32 *)argv + 2),
                    GET_I64_FROM_ADDR((uint32 *)argv + 4));
}
static void
invoke_iiII_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int32, int64, int64) = func_ptr;
    int64 ret =
        native_code(argv[0], argv[1], GET_I64_FROM_ADDR((uint32 *)argv + 2),
                    GET_I64_FROM_ADDR((uint32 *)argv + 4));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iIii_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int64, int32, int32) = func_ptr;
    native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1), argv[3],
                argv[4]);
}
static void
invoke_iIii_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int64, int32, int32) = func_ptr;
    argv_ret[0] = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                              argv[3], argv[4]);
}
static void
invoke_iIii_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int64, int32, int32) = func_ptr;
    int64 ret = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                            argv[3], argv[4]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iIiI_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int64, int32, int64) = func_ptr;
    native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1), argv[3],
                GET_I64_FROM_ADDR((uint32 *)argv + 4));
}
static void
invoke_iIiI_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int64, int32, int64) = func_ptr;
    argv_ret[0] = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                              argv[3], GET_I64_FROM_ADDR((uint32 *)argv + 4));
}
static void
invoke_iIiI_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int64, int32, int64) = func_ptr;
    int64 ret = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                            argv[3], GET_I64_FROM_ADDR((uint32 *)argv + 4));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iIIi_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int64, int64, int32) = func_ptr;
    native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                GET_I64_FROM_ADDR((uint32 *)argv + 3), argv[5]);
}
static void
invoke_iIIi_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int64, int64, int32) = func_ptr;
    argv_ret[0] = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                              GET_I64_FROM_ADDR((uint32 *)argv + 3), argv[5]);
}
static void
invoke_iIIi_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int64, int64, int32) = func_ptr;
    int64 ret = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                            GET_I64_FROM_ADDR((uint32 *)argv + 3), argv[5]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iIII_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int64, int64, int64) = func_ptr;
    native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                GET_I64_FROM_ADDR((uint32 *)argv + 3),
                GET_I64_FROM_ADDR((uint32 *)argv + 5));
}
static void
invoke_iIII_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int64, int64, int64) = func_ptr;
    argv_ret[0] = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                              GET_I64_FROM_ADDR((uint32 *)argv + 3),
                              GET_I64_FROM_ADDR((uint32 *)argv + 5));
}
static void
invoke_iIII_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int64, int64, int64) = func_ptr;
    int64 ret = native_code(argv[0], GET_I64_FROM_ADDR((uint32 *)argv + 1),
                            GET_I64_FROM_ADDR((uint32 *)argv + 3),
                            GET_I64_FROM_ADDR((uint32 *)argv + 5));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_Iiii_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int32, int32, int32) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2], argv[3], argv[4]);
}
static void
invoke_Iiii_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int32, int32, int32) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                              argv[3], argv[4]);
}
static void
invoke_Iiii_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int32, int32, int32) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2], argv[3],
                            argv[4]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_IiiI_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int32, int32, int64) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2], argv[3],
                GET_I64_FROM_ADDR((uint32 *)argv + 4));
}

static void
invoke_IiiI_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int32, int32, int64) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                              argv[3], GET_I64_FROM_ADDR((uint32 *)argv + 4));
}

static void
invoke_IiiI_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int32, int32, int64) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2], argv[3],
                            GET_I64_FROM_ADDR((uint32 *)argv + 4));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_IiIi_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int32, int64, int32) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                GET_I64_FROM_ADDR((uint32 *)argv + 3), argv[5]);
}
static void
invoke_IiIi_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int32, int64, int32) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                              GET_I64_FROM_ADDR((uint32 *)argv + 3), argv[5]);
}
static void
invoke_IiIi_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int32, int64, int32) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                            GET_I64_FROM_ADDR((uint32 *)argv + 3), argv[5]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_IiII_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int32, int64, int64) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                GET_I64_FROM_ADDR((uint32 *)argv + 3),
                GET_I64_FROM_ADDR((uint32 *)argv + 5));
}
static void
invoke_IiII_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int32, int64, int64) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                              GET_I64_FROM_ADDR((uint32 *)argv + 3),
                              GET_I64_FROM_ADDR((uint32 *)argv + 5));
}
static void
invoke_IiII_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int32, int64, int64) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv), argv[2],
                            GET_I64_FROM_ADDR((uint32 *)argv + 3),
                            GET_I64_FROM_ADDR((uint32 *)argv + 5));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_IIii_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int64, int32, int32) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4], argv[5]);
}
static void
invoke_IIii_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int64, int32, int32) = func_ptr;
    argv_ret[0] =
        native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                    GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4], argv[5]);
}
static void
invoke_IIii_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int64, int32, int32) = func_ptr;
    int64 ret =
        native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                    GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4], argv[5]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_IIiI_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int64, int32, int64) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4],
                GET_I64_FROM_ADDR((uint32 *)argv + 5));
}
static void
invoke_IIiI_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int64, int32, int64) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                              GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4],
                              GET_I64_FROM_ADDR((uint32 *)argv + 5));
}
static void
invoke_IIiI_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int64, int32, int64) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                            GET_I64_FROM_ADDR((uint32 *)argv + 2), argv[4],
                            GET_I64_FROM_ADDR((uint32 *)argv + 5));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_IIIi_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int64, int64, int32) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                GET_I64_FROM_ADDR((uint32 *)argv + 2),
                GET_I64_FROM_ADDR((uint32 *)argv + 4), argv[6]);
}
static void
invoke_IIIi_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int64, int64, int32) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                              GET_I64_FROM_ADDR((uint32 *)argv + 2),
                              GET_I64_FROM_ADDR((uint32 *)argv + 4), argv[6]);
}
static void
invoke_IIIi_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int64, int64, int32) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                            GET_I64_FROM_ADDR((uint32 *)argv + 2),
                            GET_I64_FROM_ADDR((uint32 *)argv + 4), argv[6]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_IIII_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, int64, int64, int64) = func_ptr;
    native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                GET_I64_FROM_ADDR((uint32 *)argv + 2),
                GET_I64_FROM_ADDR((uint32 *)argv + 4),
                GET_I64_FROM_ADDR((uint32 *)argv + 6));
}
static void
invoke_IIII_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64, int64, int64, int64) = func_ptr;
    argv_ret[0] = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                              GET_I64_FROM_ADDR((uint32 *)argv + 2),
                              GET_I64_FROM_ADDR((uint32 *)argv + 4),
                              GET_I64_FROM_ADDR((uint32 *)argv + 6));
}
static void
invoke_IIII_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int64, int64, int64) = func_ptr;
    int64 ret = native_code(GET_I64_FROM_ADDR((uint32 *)argv),
                            GET_I64_FROM_ADDR((uint32 *)argv + 2),
                            GET_I64_FROM_ADDR((uint32 *)argv + 4),
                            GET_I64_FROM_ADDR((uint32 *)argv + 6));
    PUT_I64_TO_ADDR(argv_ret, ret);
}

static void
invoke_iiiii_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, int32, int32, int32, int32) = func_ptr;
    native_code(argv[0], argv[1], argv[2], argv[3], argv[4]);
}
static void
invoke_iiiii_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int32, int32, int32, int32) = func_ptr;
    argv_ret[0] = native_code(argv[0], argv[1], argv[2], argv[3], argv[4]);
}
static void
invoke_iiiii_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32, int32, int32, int32, int32) = func_ptr;
    int64 ret = native_code(argv[0], argv[1], argv[2], argv[3], argv[4]);
    PUT_I64_TO_ADDR(argv_ret, ret);
}

/* For spec cases */

static void
invoke_no_args_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)() = func_ptr;
    *(float32 *)argv_ret = native_code();
}

static void
invoke_no_args_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)() = func_ptr;
    *(float64 *)argv_ret = native_code();
}

#if W2N_ENABLE_SPEC_TEST != 0
static void
invoke_no_args_ii(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32 *) = func_ptr;
    argv_ret[0] = native_code((int32 *)(argv_ret + 1));
}

static void
invoke_no_args_iI(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int64 *) = func_ptr;
    argv_ret[0] = native_code((int64 *)(argv_ret + 1));
}

static void
invoke_no_args_iF(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(float64 *) = func_ptr;
    argv_ret[0] = native_code((float64 *)(argv_ret + 1));
}

static void
invoke_no_args_Ii(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int32 *) = func_ptr;
    *(int64 *)argv_ret = native_code((int32 *)(argv_ret + 2));
}

static void
invoke_no_args_fF(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(float64 *) = func_ptr;
    *(float32 *)argv_ret = native_code((float64 *)(argv_ret + 1));
}

static void
invoke_no_args_Fi(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(int32 *) = func_ptr;
    *(float64 *)argv_ret = native_code((int32 *)(argv_ret + 2));
}

static void
invoke_no_args_Ff(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(float32 *) = func_ptr;
    *(float64 *)argv_ret = native_code((float32 *)(argv_ret + 2));
}

static void
invoke_no_args_FF(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(float64 *) = func_ptr;
    *(float64 *)argv_ret = native_code((float64 *)(argv_ret + 2));
}

static void
invoke_no_args_iii(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32 *, int32 *) = func_ptr;
    argv_ret[0] = native_code((int32 *)(argv_ret + 1), (int32 *)(argv_ret + 2));
}

static void
invoke_no_args_iiI(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32 *, int64 *) = func_ptr;
    argv_ret[0] = native_code((int32 *)(argv_ret + 1), (int64 *)(argv_ret + 2));
}

static void
invoke_i_ii(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int32 *) = func_ptr;
    argv_ret[0] = native_code(argv[0], (int32 *)(argv_ret + 1));
}

static void
invoke_i_iI(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int64 *) = func_ptr;
    argv_ret[0] = native_code(argv[0], (int64 *)(argv_ret + 1));
}

static void
invoke_i_iiI(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, int32 *, int64 *) = func_ptr;
    argv_ret[0] =
        native_code(argv[0], (int32 *)(argv_ret + 1), (int64 *)(argv_ret + 2));
}

static void
invoke_IIi_Ii(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, int64, int32, int32 *) = func_ptr;
    *(int64 *)argv_ret =
        native_code(*(int64 *)argv, *(int64 *)(argv + 2), *(int64 *)(argv + 4),
                    (int32 *)(argv_ret + 2));
}
#endif /* end of W2N_ENABLE_SPEC_TEST != 0 */

static void
invoke_i_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(int32) = func_ptr;
    *(float32 *)argv_ret = native_code(argv[0]);
}

static void
invoke_i_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(int32) = func_ptr;
    *(float64 *)argv_ret = native_code(argv[0]);
}

static void
invoke_I_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(int64) = func_ptr;
    *(float32 *)argv_ret = native_code(*(int64 *)argv);
}

static void
invoke_I_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(int64) = func_ptr;
    *(float64 *)argv_ret = native_code(*(int64 *)argv);
}

static void
invoke_f_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(float32) = func_ptr;
    native_code(*(float32 *)argv);
}

static void
invoke_f_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(float32) = func_ptr;
    argv_ret[0] = native_code(*(float32 *)argv);
}

static void
invoke_f_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(float32) = func_ptr;
    *(int64 *)argv_ret = native_code(*(float32 *)argv);
}

static void
invoke_f_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(float32) = func_ptr;
    *(float32 *)argv_ret = native_code(*(float32 *)argv);
}

static void
invoke_f_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(float32) = func_ptr;
    *(float64 *)argv_ret = native_code(*(float32 *)argv);
}

static void
invoke_F_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(float64) = func_ptr;
    native_code(*(float64 *)argv);
}

static void
invoke_F_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(float64) = func_ptr;
    argv_ret[0] = native_code(*(float64 *)argv);
}

static void
invoke_F_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(float64) = func_ptr;
    *(int64 *)argv_ret = native_code(*(float64 *)argv);
}

static void
invoke_F_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(float64) = func_ptr;
    *(float32 *)argv_ret = native_code(*(float64 *)argv);
}

static void
invoke_F_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(float64) = func_ptr;
    *(float64 *)argv_ret = native_code(*(float64 *)argv);
}

static void
invoke_if_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, float32) = func_ptr;
    native_code(*(int32 *)argv, *(float32 *)(argv + 1));
}

static void
invoke_iF_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int32, float64) = func_ptr;
    native_code(*(int32 *)argv, *(float64 *)(argv + 1));
}

static void
invoke_ii_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(int32, int32) = func_ptr;
    *(float32 *)argv_ret = native_code(*(int32 *)argv, *(int32 *)(argv + 1));
}

static void
invoke_ii_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(int32, int32) = func_ptr;
    *(float64 *)argv_ret = native_code(*(int32 *)argv, *(int32 *)(argv + 1));
}
static void
invoke_ff_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(float32, float32) = func_ptr;
    *(float32 *)argv_ret =
        native_code(*(float32 *)argv, *(float32 *)(argv + 1));
}

static void
invoke_ff_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(float32, float32) = func_ptr;
    argv_ret[0] = native_code(*(float32 *)argv, *(float32 *)(argv + 1));
}

static void
invoke_fF_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(float32, float64) = func_ptr;
    *(float32 *)argv_ret =
        native_code(*(float32 *)argv, *(float64 *)(argv + 1));
}

static void
invoke_Ff_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(float64, float32) = func_ptr;
    *(float32 *)argv_ret =
        native_code(*(float64 *)argv, *(float32 *)(argv + 2));
}

static void
invoke_FF_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(float64, float64) = func_ptr;
    *(float64 *)argv_ret =
        native_code(*(float64 *)argv, *(float64 *)(argv + 2));
}

static void
invoke_FF_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(float64, float64) = func_ptr;
    argv_ret[0] = native_code(*(float64 *)argv, *(float64 *)(argv + 2));
}

static void
invoke_iFi_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(int32, float64, int32) = func_ptr;
    *(int32 *)argv_ret = native_code(*(int32 *)argv, *(float64 *)(argv + 1),
                                     *(int32 *)(argv + 3));
}

static void
invoke_ffi_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(float32, float32, int32) = func_ptr;
    *(float32 *)argv_ret = native_code(*(float32 *)argv, *(float32 *)(argv + 1),
                                       *(int32 *)(argv + 2));
}

static void
invoke_fff_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(float32, float32, float32) = func_ptr;
    *(int32 *)argv_ret = native_code(*(float32 *)argv, *(float32 *)(argv + 1),
                                     *(float32 *)(argv + 2));
}

static void
invoke_fff_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(float32, float32, float32) = func_ptr;
    *(float32 *)argv_ret = native_code(*(float32 *)argv, *(float32 *)(argv + 1),
                                       *(float32 *)(argv + 2));
}

static void
invoke_FFi_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(float64, float64, int32) = func_ptr;
    *(float64 *)argv_ret = native_code(*(float64 *)argv, *(float64 *)(argv + 2),
                                       *(int32 *)(argv + 4));
}

static void
invoke_FFF_i(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int32 (*native_code)(float64, float64, float64) = func_ptr;
    *(int32 *)argv_ret = native_code(*(float64 *)argv, *(float64 *)(argv + 2),
                                     *(float64 *)(argv + 4));
}

static void
invoke_FFF_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(float64, float64, float64) = func_ptr;
    *(float64 *)argv_ret = native_code(*(float64 *)argv, *(float64 *)(argv + 2),
                                       *(float64 *)(argv + 4));
}

static void
invoke_ffff_f(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float32 (*native_code)(float32, float32, float32, float32) = func_ptr;
    *(float32 *)argv_ret =
        native_code(*(float32 *)argv, *(float32 *)(argv + 1),
                    *(float32 *)(argv + 2), *(float32 *)(argv + 3));
}

static void
invoke_FFFF_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(float64, float64, float64, float64) = func_ptr;
    *(float64 *)argv_ret =
        native_code(*(float64 *)argv, *(float64 *)(argv + 2),
                    *(float64 *)(argv + 4), *(float64 *)(argv + 6));
}

static void
invoke_IfFii_v(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    void (*native_code)(int64, float32, float64, int32, int32) = func_ptr;
    native_code(*(int64 *)argv, *(float32 *)(argv + 2), *(float64 *)(argv + 3),
                *(int32 *)(argv + 5), *(int32 *)(argv + 6));
}

static void
invoke_IfFii_I(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    int64 (*native_code)(int64, float32, float64, int32, int32) = func_ptr;
    *(int64 *)argv_ret = native_code(
        *(int64 *)argv, *(float32 *)(argv + 2), *(float64 *)(argv + 3),
        *(int32 *)(argv + 5), *(int32 *)(argv + 6));
}

static void
invoke_IfFii_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(int64, float32, float64, int32, int32) = func_ptr;
    *(float64 *)argv_ret = native_code(
        *(int64 *)argv, *(float32 *)(argv + 2), *(float64 *)(argv + 3),
        *(int32 *)(argv + 5), *(int32 *)(argv + 6));
}

static void
invoke_fiIiFi_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(float32, int32, int64, int32, float64, int32) =
        func_ptr;
    *(float64 *)argv_ret = native_code(
        *(float32 *)argv, *(int32 *)(argv + 1), *(int64 *)(argv + 2),
        *(int32 *)(argv + 4), *(float64 *)(argv + 5), *(int32 *)(argv + 7));
}

static void
invoke_FFFFFFFF_F(void *func_ptr, int32 *argv, int32 *argv_ret)
{
    float64 (*native_code)(float64, float64, float64, float64, float64, float64,
                           float64, float64) = func_ptr;
    *(float64 *)argv_ret = native_code(
        *(float64 *)argv, *(float64 *)(argv + 2), *(float64 *)(argv + 4),
        *(float64 *)(argv + 6), *(float64 *)(argv + 8), *(float64 *)(argv + 10),
        *(float64 *)(argv + 12), *(float64 *)(argv + 14));
}

typedef struct QuickAOTEntry {
    const char *signature;
    void *func_ptr;
} QuickAOTEntry;

/* clang-format off */
static QuickAOTEntry quick_aot_entries[] = {
    { "()", invoke_no_args_v },
    { "()i", invoke_no_args_i },
    { "()I", invoke_no_args_I },

    { "(i)", invoke_i_v }, { "(i)i", invoke_i_i }, { "(i)I", invoke_i_I },
    { "(I)", invoke_I_v }, { "(I)i", invoke_I_i }, { "(I)I", invoke_I_I },

    { "(ii)", invoke_ii_v }, { "(ii)i", invoke_ii_i }, { "(ii)I", invoke_ii_I },
    { "(iI)", invoke_iI_v }, { "(iI)i", invoke_iI_i }, { "(iI)I", invoke_iI_I },
    { "(Ii)", invoke_Ii_v }, { "(Ii)i", invoke_Ii_i }, { "(Ii)I", invoke_Ii_I },
    { "(II)", invoke_II_v }, { "(II)i", invoke_II_i }, { "(II)I", invoke_II_I },

    { "(iii)", invoke_iii_v }, { "(iii)i", invoke_iii_i }, { "(iii)I", invoke_iii_I },
    { "(iiI)", invoke_iiI_v }, { "(iiI)i", invoke_iiI_i }, { "(iiI)I", invoke_iiI_I },
    { "(iIi)", invoke_iIi_v }, { "(iIi)i", invoke_iIi_i }, { "(iIi)I", invoke_iIi_I },
    { "(iII)", invoke_iII_v }, { "(iII)i", invoke_iII_i }, { "(iII)I", invoke_iII_I },
    { "(Iii)", invoke_Iii_v }, { "(Iii)i", invoke_Iii_i }, { "(Iii)I", invoke_Iii_I },
    { "(IiI)", invoke_IiI_v }, { "(IiI)i", invoke_IiI_i }, { "(IiI)I", invoke_IiI_I },
    { "(IIi)", invoke_IIi_v }, { "(IIi)i", invoke_IIi_i }, { "(IIi)I", invoke_IIi_I },
    { "(III)", invoke_III_v }, { "(III)i", invoke_III_i }, { "(III)I", invoke_III_I },

    { "(iiii)", invoke_iiii_v }, { "(iiii)i", invoke_iiii_i }, { "(iiii)I", invoke_iiii_I },
    { "(iiiI)", invoke_iiiI_v }, { "(iiiI)i", invoke_iiiI_i }, { "(iiiI)I", invoke_iiiI_I },
    { "(iiIi)", invoke_iiIi_v }, { "(iiIi)i", invoke_iiIi_i }, { "(iiIi)I", invoke_iiIi_I },
    { "(iiII)", invoke_iiII_v }, { "(iiII)i", invoke_iiII_i }, { "(iiII)I", invoke_iiII_I },
    { "(iIii)", invoke_iIii_v }, { "(iIii)i", invoke_iIii_i }, { "(iIii)I", invoke_iIii_I },
    { "(iIiI)", invoke_iIiI_v }, { "(iIiI)i", invoke_iIiI_i }, { "(iIiI)I", invoke_iIiI_I },
    { "(iIIi)", invoke_iIIi_v }, { "(iIIi)i", invoke_iIIi_i }, { "(iIIi)I", invoke_iIIi_I },
    { "(iIII)", invoke_iIII_v }, { "(iIII)i", invoke_iIII_i }, { "(iIII)I", invoke_iIII_I },
    { "(Iiii)", invoke_Iiii_v }, { "(Iiii)i", invoke_Iiii_i }, { "(Iiii)I", invoke_Iiii_I },
    { "(IiiI)", invoke_IiiI_v }, { "(IiiI)i", invoke_IiiI_i }, { "(IiiI)I", invoke_IiiI_I },
    { "(IiIi)", invoke_IiIi_v }, { "(IiIi)i", invoke_IiIi_i }, { "(IiIi)I", invoke_IiIi_I },
    { "(IiII)", invoke_IiII_v }, { "(IiII)i", invoke_IiII_i }, { "(IiII)I", invoke_IiII_I },
    { "(IIii)", invoke_IIii_v }, { "(IIii)i", invoke_IIii_i }, { "(IIii)I", invoke_IIii_I },
    { "(IIiI)", invoke_IIiI_v }, { "(IIiI)i", invoke_IIiI_i }, { "(IIiI)I", invoke_IIiI_I },
    { "(IIIi)", invoke_IIIi_v }, { "(IIIi)i", invoke_IIIi_i }, { "(IIIi)I", invoke_IIIi_I },
    { "(IIII)", invoke_IIII_v }, { "(IIII)i", invoke_IIII_i }, { "(IIII)I", invoke_IIII_I },

    { "(iiiii)", invoke_iiiii_v }, { "(iiiii)i", invoke_iiiii_i }, { "(iiiii)I", invoke_iiiii_I },

#if W2N_ENABLE_SPEC_TEST != 0
    { "()ii", invoke_no_args_ii },
    { "()iI", invoke_no_args_iI },
    { "()iF", invoke_no_args_iF },
    { "()Ii", invoke_no_args_Ii },
    { "()fF", invoke_no_args_fF },
    { "()Fi", invoke_no_args_Fi },
    { "()Ff", invoke_no_args_Ff },
    { "()FF", invoke_no_args_FF },
    { "()iii", invoke_no_args_iii },
    { "()iiI", invoke_no_args_iiI },
    { "(i)ii", invoke_i_ii },
    { "(i)iI", invoke_i_iI },
    { "(i)iiI", invoke_i_iiI },
    { "(i)iiI", invoke_i_iiI },
    { "(IIi)Ii", invoke_IIi_Ii },
    /* TODO */
#if 0
    { "(iIffiFfiiifFFFiif)FfiiiIfiifFFifiF", invoke_iIffiFfiiifFFFiif_FfiiiIfiifFFifiF },
#endif
#endif

    { "()f", invoke_no_args_f },
    { "()F", invoke_no_args_F },
    { "(i)f", invoke_i_f },
    { "(i)F", invoke_i_F },
    { "(I)f", invoke_I_f },
    { "(I)F", invoke_I_F },
    { "(f)", invoke_f_v },
    { "(f)i", invoke_f_i },
    { "(f)I", invoke_f_I },
    { "(f)f", invoke_f_f },
    { "(f)F", invoke_f_F },
    { "(F)", invoke_F_v },
    { "(F)i", invoke_F_i },
    { "(F)I", invoke_F_I },
    { "(F)f", invoke_F_f },
    { "(F)F", invoke_F_F },
    { "(if)", invoke_if_v },
    { "(iF)", invoke_iF_v },
    { "(ii)f", invoke_ii_f },
    { "(ii)F", invoke_ii_F },
    { "(ff)f", invoke_ff_f },
    { "(ff)i", invoke_ff_i },
    { "(fF)f", invoke_fF_f },
    { "(Ff)f", invoke_Ff_f },
    { "(FF)F", invoke_FF_F },
    { "(FF)i", invoke_FF_i },
    { "(iFi)i", invoke_iFi_i },
    { "(ffi)f", invoke_ffi_f },
    { "(fff)i", invoke_fff_i },
    { "(fff)f", invoke_fff_f },
    { "(FFi)F", invoke_FFi_F },
    { "(FFF)i", invoke_FFF_i },
    { "(FFF)F", invoke_FFF_F },
    { "(ffff)f", invoke_ffff_f },
    { "(FFFF)F", invoke_FFFF_F },
    { "(IfFii)", invoke_IfFii_v },
    { "(IfFii)I", invoke_IfFii_I },
    { "(IfFii)F", invoke_IfFii_F },
    { "(fiIiFi)F", invoke_fiIiFi_F },
    { "(FFFFFFFF)F", invoke_FFFFFFFF_F },
};
/* clang-format on */

static bool quick_aot_entries_sorted = false;

static int
quick_aot_entry_cmp(const void *quick_aot_entry1, const void *quick_aot_entry2)
{
    return strcmp(((const QuickAOTEntry *)quick_aot_entry1)->signature,
                  ((const QuickAOTEntry *)quick_aot_entry2)->signature);
}

static void
sort_quick_aot_entries()
{
    qsort(quick_aot_entries, sizeof(quick_aot_entries) / sizeof(QuickAOTEntry),
          sizeof(QuickAOTEntry), quick_aot_entry_cmp);
}

static void *
lookup_quick_aot_entry(const char *signature)
{
    QuickAOTEntry *quick_aot_entry, key = { 0 };

    if (!quick_aot_entries_sorted) {
        sort_quick_aot_entries();
        quick_aot_entries_sorted = true;
    }

    key.signature = signature;
    if ((quick_aot_entry =
             bsearch(&key, quick_aot_entries,
                     sizeof(quick_aot_entries) / sizeof(QuickAOTEntry),
                     sizeof(QuickAOTEntry), quick_aot_entry_cmp))) {
        return quick_aot_entry->func_ptr;
    }

    return NULL;
}

static bool
execute_main(int32 argc, char *argv[])
{
    WASMExportApi *export_apis = wasm_get_export_apis();
    WASMExportApi *export_main = NULL;
    uint32 export_api_num = wasm_get_export_api_num();
    uint32 total_argv_size = 0, *argv_offsets, i;
    uint64 total_size, argv_buf_offset = 0;
    int32 argv1[3] = { 0 };
    char *argv_buf = NULL, *p, *p_end;
    void *wasm_memory = wasm_get_memory();
    void *heap_handle = wasm_get_heap_handle();
    bool is_memory64 = false;
    bool ret;

    /* Lookup exported main function */
    for (i = 0; i < export_api_num; i++) {
        if (!strcmp(export_apis[i].func_name, "__main_argc_argv")
            || !strcmp(export_apis[i].func_name, "main")) {
            if (!strcmp(export_apis[i].signature, "(ii)i")) {
                export_main = &export_apis[i];
                break;
            }
            else if (!strcmp(export_apis[i].signature, "(iI)i")) {
                export_main = &export_apis[i];
                is_memory64 = true;
                break;
            }
        }
        else if ((!strcmp(export_apis[i].func_name, "__main_void")
                  || !strcmp(export_apis[i].func_name, "main"))
                 && !strcmp(export_apis[i].signature, "()i")) {
            export_main = &export_apis[i];
            break;
        }
    }

    if (!export_main) {
        LOG_ERROR("lookup the entry symbol (like __main_argc_argv, "
                  " __main_void, main) failed");
        wasm_set_exception(EXCE_LOOKUP_ENTRY_SYMBOL_FAILED);
        return false;
    }

    if (!strcmp(export_main->signature, "(ii)i")
        || !strcmp(export_main->signature, "(iI)i")) {
        if (!heap_handle) {
            LOG_ERROR("prepare arguments for main function failed: "
                      "host managed heap not found");
            wasm_set_exception(EXCE_HOST_MANAGED_HEAP_NOT_FOUND);
            return false;
        }

        for (i = 0; i < (uint32)argc; i++)
            total_argv_size += (uint32)(strlen(argv[i]) + 1);
        /* `char **argv` is 64-bit array in memory64 */
        total_argv_size = align_uint(total_argv_size, is_memory64 ? 8 : 4);

        total_size =
            (uint64)total_argv_size
            /* `char **argv` is 64-bit array in memory64 */
            + (is_memory64 ? sizeof(uint64) : sizeof(uint32)) * (uint64)argc;
        if (total_size >= UINT32_MAX
            || !(argv_buf =
                     mem_allocator_malloc(heap_handle, (uint32)total_size))) {
            LOG_ERROR("allocate memory failed");
            wasm_set_exception(EXCE_ALLOCATE_MEMORY_FAILED);
            return false;
        }
        argv_buf_offset = (uint32)((uint8 *)argv_buf - (uint8 *)wasm_memory);

        p = argv_buf;
        argv_offsets = (uint32 *)(p + total_argv_size);
        p_end = p + total_size;

        for (i = 0; i < (uint32)argc; i++) {
            bh_memcpy_s(p, (uint32)(p_end - p), argv[i],
                        (uint32)(strlen(argv[i]) + 1));
            if (!is_memory64)
                argv_offsets[i] =
                    (uint32)argv_buf_offset + (uint32)(p - argv_buf);
            else
                /* `char **argv` is 64-bit array in memory64 */
                ((uint64 *)argv_offsets)[i] =
                    (uint32)argv_buf_offset + (uint32)(p - argv_buf);
            p += strlen(argv[i]) + 1;
        }

        argv1[0] = argc;
        if (!is_memory64)
            argv1[1] = (int32)((uint8 *)argv_offsets - (uint8 *)wasm_memory);
        else
            PUT_I64_TO_ADDR(&argv1[1], (int32)((uint8 *)argv_offsets
                                               - (uint8 *)wasm_memory));
    }

    void (*invoke_native)(const void *func_ptr, int32 *argv, int32 *argv_ret) =
        lookup_quick_aot_entry(export_main->signature);
    bh_assert(invoke_native);

    invoke_native(export_main->func_ptr, argv1, argv1);

    if (argv_buf)
        mem_allocator_free(heap_handle, argv_buf);

    ret = !wasm_get_exception() ? true : false;

    if (ret)
        /* copy the return value */
        *(int32 *)argv = (int32)argv1[0];

    return ret;
}

bool
wasm_application_execute_main(int32 argc, char *argv[])
{
    return execute_main(argc, argv);
}

union ieee754_float {
    float f;

    /* This is the IEEE 754 single-precision format.  */
    union {
        struct {
            unsigned int negative : 1;
            unsigned int exponent : 8;
            unsigned int mantissa : 23;
        } ieee_big_endian;
        struct {
            unsigned int mantissa : 23;
            unsigned int exponent : 8;
            unsigned int negative : 1;
        } ieee_little_endian;
    } ieee;
};

union ieee754_double {
    double d;

    /* This is the IEEE 754 double-precision format.  */
    union {
        struct {
            unsigned int negative : 1;
            unsigned int exponent : 11;
            /* Together these comprise the mantissa.  */
            unsigned int mantissa0 : 20;
            unsigned int mantissa1 : 32;
        } ieee_big_endian;
        struct {
            /* Together these comprise the mantissa.  */
            unsigned int mantissa1 : 32;
            unsigned int mantissa0 : 20;
            unsigned int exponent : 11;
            unsigned int negative : 1;
        } ieee_little_endian;
    } ieee;
};

static union {
    int a;
    char b;
} __ue = { .a = 1 };

#define is_little_endian() (__ue.b == 1)

static bool
execute_func(const char *name, int32 argc, char *argv[])
{
    WASMExportApi *export_apis = wasm_get_export_apis();
    WASMExportApi *export_func = NULL;
    void (*invoke_native)(const void *, uint32 *, uint32 *);
    const char *s1, *s2, *s;
    uint32 param_cell_num = 0, ret_cell_num = 0, cell_num = 0;
    uint32 export_api_num = wasm_get_export_api_num();
    uint32 argc1, *argv1 = NULL;
    uint32 i, j, k = 0, p, result_count = 0;
    uint64 total_size;

    bh_assert(argc >= 0);

    /* Lookup exported function */
    for (i = 0; i < export_api_num; i++) {
        if (!strcmp(export_apis[i].func_name, name)) {
            export_func = &export_apis[i];
            break;
        }
    }

    if (!export_func) {
        LOG_ERROR("lookup function %s failed", name);
        wasm_set_exception(EXCE_LOOKUP_FUNCTION_FAILED);
        return false;
    }

    bh_assert(export_func->signature && export_func->func_ptr);

    s1 = strchr(export_func->signature, '(');
    bh_assert(s1);
    s2 = strchr(export_func->signature, ')');
    bh_assert(s1);

    if ((int32)(uintptr_t)(s2 - s1 - 1) != argc) {
        LOG_ERROR("invalid input argument count");
        wasm_set_exception(EXCE_INVALID_INPUT_ARGUMENT_COUNT);
        return false;
    }

    s = s1 + 1;
    while (s != s2) {
        switch (*s) {
            case 'i':
            case 'f':
                param_cell_num++;
                break;
            case 'I':
            case 'F':
                param_cell_num += 2;
                break;
            case 'V':
                param_cell_num += 4;
                break;
            default:
                bh_assert(0);
                break;
        }
        s++;
    }

    s = s2 + 1;
    while (*s != '\0') {
        switch (*s) {
            case 'i':
            case 'f':
                ret_cell_num++;
                break;
            case 'I':
            case 'F':
                ret_cell_num += 2;
                break;
            case 'V':
                ret_cell_num += 4;
                break;
            default:
                bh_assert(0);
                break;
        }
        result_count++;
        s++;
    }

    argc1 = param_cell_num;
    cell_num = (argc1 > ret_cell_num) ? argc1 : ret_cell_num;

    total_size = sizeof(uint32) * (uint64)(cell_num > 2 ? cell_num : 2);
    if ((!(argv1 = malloc((uint32)total_size)))) {
        LOG_ERROR("allocate memory failed");
        wasm_set_exception(EXCE_ALLOCATE_MEMORY_FAILED);
        return false;
    }

    /* Parse arguments */
    for (i = 0, p = 0; i < (uint32)argc; i++) {
        char *endptr = NULL;
        bh_assert(argv[i] != NULL);
        if (argv[i][0] == '\0') {
            LOG_ERROR("invalid input argument %" PRId32, i);
            wasm_set_exception(EXCE_INVALID_INPUT_ARGUMENT);
            goto fail;
        }
        switch (s1[i + 1]) {
            case 'i':
                argv1[p++] = (uint32)strtoul(argv[i], &endptr, 0);
                break;
            case 'I':
            {
                union {
                    uint64 val;
                    uint32 parts[2];
                } u;
                u.val = strtoull(argv[i], &endptr, 0);
                argv1[p++] = u.parts[0];
                argv1[p++] = u.parts[1];
                break;
            }
            case 'f':
            {
                float32 f32 = strtof(argv[i], &endptr);
                if (isnan(f32)) {
#ifdef _MSC_VER
                    /*
                     * Spec tests require the binary representation of NaN to be
                     * 0x7fc00000 for float and 0x7ff8000000000000 for float;
                     * however, in MSVC compiler, strtof doesn't return this
                     * exact value, causing some of the spec test failures. We
                     * use the value returned by nan/nanf as it is the one
                     * expected by spec tests.
                     *
                     */
                    f32 = nanf("");
#endif
                    if (argv[i][0] == '-') {
                        union ieee754_float u;
                        u.f = f32;
                        if (is_little_endian())
                            u.ieee.ieee_little_endian.negative = 1;
                        else
                            u.ieee.ieee_big_endian.negative = 1;
                        bh_memcpy_s(&f32, sizeof(float), &u.f, sizeof(float));
                    }
                    if (endptr[0] == ':') {
                        uint32 sig;
                        union ieee754_float u;
                        sig = (uint32)strtoul(endptr + 1, &endptr, 0);
                        u.f = f32;
                        if (is_little_endian())
                            u.ieee.ieee_little_endian.mantissa = sig;
                        else
                            u.ieee.ieee_big_endian.mantissa = sig;
                        bh_memcpy_s(&f32, sizeof(float), &u.f, sizeof(float));
                    }
                }
                bh_memcpy_s(&argv1[p], (uint32)total_size - p, &f32,
                            (uint32)sizeof(float));
                p++;
                break;
            }
            case 'F':
            {
                union {
                    float64 val;
                    uint32 parts[2];
                } u;
                u.val = strtod(argv[i], &endptr);
                if (isnan(u.val)) {
#ifdef _MSC_VER
                    u.val = nan("");
#endif
                    if (argv[i][0] == '-') {
                        union ieee754_double ud;
                        ud.d = u.val;
                        if (is_little_endian())
                            ud.ieee.ieee_little_endian.negative = 1;
                        else
                            ud.ieee.ieee_big_endian.negative = 1;
                        bh_memcpy_s(&u.val, sizeof(double), &ud.d,
                                    sizeof(double));
                    }
                    if (endptr[0] == ':') {
                        uint64 sig;
                        union ieee754_double ud;
                        sig = strtoull(endptr + 1, &endptr, 0);
                        ud.d = u.val;
                        if (is_little_endian()) {
                            ud.ieee.ieee_little_endian.mantissa0 = sig >> 32;
                            ud.ieee.ieee_little_endian.mantissa1 = (uint32)sig;
                        }
                        else {
                            ud.ieee.ieee_big_endian.mantissa0 = sig >> 32;
                            ud.ieee.ieee_big_endian.mantissa1 = (uint32)sig;
                        }
                        bh_memcpy_s(&u.val, sizeof(double), &ud.d,
                                    sizeof(double));
                    }
                }
                argv1[p++] = u.parts[0];
                argv1[p++] = u.parts[1];
                break;
            }
            case 'V':
            {
                /* The v128 string is like "0x123\\0x234" or "123\\234",
                   retrieve the first i64 */
                *(uint64 *)(argv1 + p) = strtoull(argv[i], &endptr, 0);
                /* skip '\\' */
                endptr++;
                /* Retrieve the second i64 */
                *(uint64 *)(argv1 + p + 2) = strtoull(endptr, &endptr, 0);
                p += 4;
                break;
            }
            default:
            {
                bh_assert(0);
                break;
            }
        }
        if (endptr && *endptr != '\0' && *endptr != '_') {
            LOG_ERROR("invalid input argument %" PRId32 ": %s", i, argv[i]);
            wasm_set_exception(EXCE_INVALID_INPUT_ARGUMENT);
            goto fail;
        }
    }
    bh_assert(p == argc1);

    /* clear the exception */
    wasm_set_exception(0);

    invoke_native = lookup_quick_aot_entry(export_func->signature);

    if (!invoke_native) {
        LOG_ERROR("quick call entry for %s not found", export_func->signature);
        wasm_set_exception(EXCE_QUICK_CALL_ENTRY_NOT_FOUND);
        goto fail;
    }

    invoke_native(export_func->func_ptr, argv1, argv1);
    if (wasm_get_exception())
        goto fail;

    /* print return value */
    for (j = 0; j < result_count; j++) {
        switch (s2[j + 1]) {
            case 'i':
            {
                os_printf("0x%" PRIx32 ":i32", argv1[k]);
                k++;
                break;
            }
            case 'I':
            {
                union {
                    uint64 val;
                    uint32 parts[2];
                } u;
                u.parts[0] = argv1[k];
                u.parts[1] = argv1[k + 1];
                k += 2;
                os_printf("0x%" PRIx64 ":i64", u.val);
                break;
            }
            case 'f':
            {
                os_printf("%.7g:f32", *(float32 *)(argv1 + k));
                k++;
                break;
            }
            case 'F':
            {
                union {
                    float64 val;
                    uint32 parts[2];
                } u;
                u.parts[0] = argv1[k];
                u.parts[1] = argv1[k + 1];
                k += 2;
                os_printf("%.7g:f64", u.val);
                break;
            }
            case 'v':
            {
                uint64 *v = (uint64 *)(argv1 + k);
                os_printf("<0x%016" PRIx64 " 0x%016" PRIx64 ">:v128", *v,
                          *(v + 1));
                k += 4;
                break;
            }
            default:
            {
                bh_assert(0);
                break;
            }
        }
        if (j < (uint32)(result_count - 1))
            os_printf(",");
    }
    os_printf("\n");

    if (argv1)
        free(argv1);
    return true;
fail:
    if (argv1)
        free(argv1);
    return false;
}

bool
wasm_application_execute_func(const char *name, int32 argc, char *argv[])
{
    return execute_func(name, argc, argv);
}
