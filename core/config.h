/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _CONFIG_H_
#define _CONFIG_H_

/* clang-format off */
#if !defined(BUILD_TARGET_X86_64) \
    && !defined(BUILD_TARGET_AMD_64) \
    && !defined(BUILD_TARGET_AARCH64) \
    && !defined(BUILD_TARGET_X86_32) \
    && !defined(BUILD_TARGET_ARM) \
    && !defined(BUILD_TARGET_ARM_VFP) \
    && !defined(BUILD_TARGET_THUMB) \
    && !defined(BUILD_TARGET_THUMB_VFP) \
    && !defined(BUILD_TARGET_MIPS) \
    && !defined(BUILD_TARGET_XTENSA) \
    && !defined(BUILD_TARGET_RISCV64_LP64D) \
    && !defined(BUILD_TARGET_RISCV64_LP64) \
    && !defined(BUILD_TARGET_RISCV32_ILP32D) \
    && !defined(BUILD_TARGET_RISCV32_ILP32) \
    && !defined(BUILD_TARGET_ARC)
/* clang-format on */
#if defined(__x86_64__) || defined(__x86_64)
#define BUILD_TARGET_X86_64
#elif defined(__amd64__) || defined(__amd64)
#define BUILD_TARGET_AMD_64
#elif defined(__aarch64__)
#define BUILD_TARGET_AARCH64
#elif defined(__i386__) || defined(__i386) || defined(i386)
#define BUILD_TARGET_X86_32
#elif defined(__thumb__)
#define BUILD_TARGET_THUMB
#define BUILD_TARGET "THUMBV4T"
#elif defined(__arm__)
#define BUILD_TARGET_ARM
#define BUILD_TARGET "ARMV4T"
#elif defined(__mips__) || defined(__mips) || defined(mips)
#define BUILD_TARGET_MIPS
#elif defined(__XTENSA__)
#define BUILD_TARGET_XTENSA
#elif defined(__riscv) && (__riscv_xlen == 64)
#define BUILD_TARGET_RISCV64_LP64D
#elif defined(__riscv) && (__riscv_xlen == 32)
#define BUILD_TARGET_RISCV32_ILP32D
#elif defined(__arc__)
#define BUILD_TARGET_ARC
#else
#error "Build target isn't set"
#endif
#endif

#ifndef BH_DEBUG
#define BH_DEBUG 0
#endif

#ifndef W2N_ENABLE_CUSTOM_NAME_SECTION
#define W2N_ENABLE_CUSTOM_NAME_SECTION 0
#endif

#ifndef W2N_ENABLE_MEMORY_TRACING
#define W2N_ENABLE_MEMORY_TRACING 0
#endif

#ifndef APP_HEAP_SIZE_MIN
#define APP_HEAP_SIZE_MIN (256)
#endif

/* Default wasm block address cache size and conflict list size */
#ifndef BLOCK_ADDR_CACHE_SIZE
#define BLOCK_ADDR_CACHE_SIZE 64
#endif
#define BLOCK_ADDR_CONFLICT_SIZE 2

/* Default enable pthread */
#ifndef W2N_ENABLE_PTHREAD
#define W2N_ENABLE_PTHREAD 1
#endif

#ifndef W2N_ENABLE_SPEC_TEST
#define W2N_ENABLE_SPEC_TEST 0
#endif

#endif /* end of _CONFIG_H_ */
