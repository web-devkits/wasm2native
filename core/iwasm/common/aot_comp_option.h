/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#ifndef _AOT_COMP_OPTION_H
#define _AOT_COMP_OPTION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct AOTCompOption {
    char *target_arch;
    char *target_abi;
    char *target_cpu;
    char *cpu_features;
    bool no_sandbox_mode;
    bool enable_simd;
    bool enable_aux_stack_check;
    bool disable_llvm_lto;
    uint32_t opt_level;
    uint32_t size_level;
    uint32_t output_format;
    uint32_t heap_size;
    char **custom_sections;
    uint32_t custom_sections_count;
} AOTCompOption, *aot_comp_option_t;

#ifdef __cplusplus
}
#endif

#endif /* end of _AOT_COMP_OPTION_H */
