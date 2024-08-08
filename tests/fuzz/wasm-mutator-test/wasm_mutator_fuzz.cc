/*
 * Copyright (C) 2019 Intel Corporation.  All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <vector>

#include "bh_platform.h"
#include "bh_read_file.h"
#include "wasm_export.h"

using namespace std;

/* When print help info for target/cpu/target-abi/cpu-features, load this dummy
 * wasm file content rather than from an input file, the dummy wasm file content
 * is: magic header + version number */
static unsigned char dummy_wasm_file[8] = { 0x00, 0x61, 0x73, 0x6D,
                                            0x01, 0x00, 0x00, 0x00 };

static int
fuzz_test(uint8 *wasm_file, uint32 wasm_file_size)
{
    const char *out_file_name = "./test.o";
    wasm_module_t wasm_module = NULL;
    aot_comp_data_t comp_data = NULL;
    aot_comp_context_t comp_ctx = NULL;
    AOTCompOption option = { 0 };
    char error_buf[128];
    int log_verbose_level = 2;
    bool size_level_set = false, use_dummy_wasm = false;
    int exit_status = EXIT_FAILURE;

    option.opt_level = 3;
    option.size_level = 3;
    option.output_format = AOT_OBJECT_FILE;
    option.enable_simd = true;
    option.enable_aux_stack_check = true;

    /* initialize runtime environment */
    if (!wasm_runtime_init()) {
        printf("Init runtime environment failed.\n");
        return -1;
    }

    bh_log_set_verbose_level(log_verbose_level);

    bh_print_time("Begin to load wasm file");

    if (!wasm_file) {
        printf("Invalid wasm file: magic header not detected\n");
        goto fail1;
    }

    if (wasm_file_size >= 4 /* length of MAGIC NUMBER */
        && !(wasm_file[0] == '\0' && wasm_file[1] == 'a' && wasm_file[2] == 's'
             && wasm_file[3] == 'm')) {
        printf("Invalid wasm file: magic header not detected\n");
        goto fail1;
    }

    /* load WASM module */
    if (!(wasm_module = wasm_runtime_load(wasm_file, wasm_file_size, error_buf,
                                          sizeof(error_buf)))) {
        printf("%s\n", error_buf);
        goto fail1;
    }

    if (!(comp_data = aot_create_comp_data(wasm_module, &option))) {
        printf("%s\n", aot_get_last_error());
        goto fail2;
    }

    bh_print_time("Begin to create compile context");

    if (!(comp_ctx = aot_create_comp_context(comp_data, &option))) {
        printf("%s\n", aot_get_last_error());
        goto fail3;
    }

    bh_print_time("Begin to compile");

    if (!aot_compile_wasm(comp_ctx)) {
        printf("%s\n", aot_get_last_error());
        goto fail4;
    }

    switch (option.output_format) {
        case AOT_LLVMIR_UNOPT_FILE:
        case AOT_LLVMIR_OPT_FILE:
            if (!aot_emit_llvm_file(comp_ctx, out_file_name)) {
                printf("%s\n", aot_get_last_error());
                goto fail4;
            }
            break;
        case AOT_OBJECT_FILE:
            if (!aot_emit_object_file(comp_ctx, out_file_name)) {
                printf("%s\n", aot_get_last_error());
                goto fail4;
            }
            break;
        default:
            break;
    }

    bh_print_time("Compile end");

    printf("Compile success, file %s was generated.\n", out_file_name);
    exit_status = EXIT_SUCCESS;

fail4:
    /* Destroy compiler context */
    aot_destroy_comp_context(comp_ctx);

fail3:
    /* Destroy compile data */
    aot_destroy_comp_data(comp_data);

fail2:
    /* Unload WASM module */
    wasm_runtime_unload(wasm_module);

fail1:
    /* Destroy runtime environment */
    wasm_runtime_destroy();

    bh_print_time("wasm2native exit");

    return exit_status;
}

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size)
{
    /* libfuzzer don't allow us to modify the given Data,
       so we copy the data here */
    std::vector<uint8_t> myData(Data, Data + Size);

    fuzz_test((uint8 *)myData.data(), (uint32)Size);

    /* Values other than 0 and -1 are reserved for future use */
    return 0;
}
