/*
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <stdlib.h>
#include "bh_platform.h"
#include "bh_read_file.h"
#include "wasm_export.h"

/* clang-format off */
static void
print_help()
{
    printf("Usage: wasm2native [options] -o output_file wasm_file\n");
    printf("  --target=<arch-name>      Set the target arch, which has the general format: <arch><sub>\n");
    printf("                            <arch> = x86_64, i386, aarch64, arm, thumb, xtensa, mips,\n");
    printf("                                     riscv64, riscv32.\n");
    printf("                              Default is host arch, e.g. x86_64\n");
    printf("                            <sub> = for ex. on arm or thumb: v5, v6m, v7a, v7m, etc.\n");
    printf("                            Use --target=help to list supported targets\n");
    printf("  --target-abi=<abi>        Set the target ABI, e.g. gnu, eabi, gnueabihf, msvc, etc.\n");
    printf("                              Default is gnu if target isn't riscv64 or riscv32\n");
    printf("                              For target riscv64 and riscv32, default is lp64d and ilp32d\n");
    printf("                            Use --target-abi=help to list all the ABI supported\n");
    printf("  --cpu=<cpu>               Set the target CPU (default: host CPU, e.g. skylake)\n");
    printf("                            Use --cpu=help to list all the CPU supported\n");
    printf("  --cpu-features=<features> Enable or disable the CPU features\n");
    printf("                            Use +feature to enable a feature, or -feature to disable it\n");
    printf("                            For example, --cpu-features=+feature1,-feature2\n");
    printf("                            Use --cpu-features=+help to list all the features supported\n");
    printf("  --opt-level=n             Set the optimization level (0 to 3, default is 3)\n");
    printf("  --size-level=n            Set the code size level (0 to 3, default is 3)\n");
    printf("  --format=<format>         Specifies the format of the output file\n");
    printf("                            The format supported:\n");
    printf("                              object         Native object file\n");
    printf("                              llvmir-unopt   Unoptimized LLVM IR\n");
    printf("                              llvmir-opt     Optimized LLVM IR\n");
    printf("  --no-sandbox-mode         Enable the no-sandbox mode, which turns wasm loads and stores into\n");
    printf("                            native host loads and stores without any bounds checking, and allows\n");
    printf("                            pointers to be shared between wasm and the host\n");
    printf("  --heap-size=n             Set host managed heap size in bytes, only supported when no-sandbox\n");
    printf("                            mode is disabled, default is 0 KB\n");
    printf("  --disable-simd            Disable the post-MVP 128-bit SIMD feature:\n");
    printf("                              currently 128-bit SIMD is supported for x86-64 and aarch64 targets,\n");
    printf("                              and by default it is enabled in them and disabled in other targets\n");
    printf("  --disable-llvm-lto        Disable the LLVM link time optimization\n");
    printf("  -v=n                      Set log verbose level (0 to 5, default is 2), larger with more log\n");
    printf("  --version                 Show version information\n");
    printf("Examples: wasm2native -o test.aot test.wasm\n");
    printf("          wasm2native --target=i386 --format=object -o test.o test.wasm\n");
    printf("          wasm2native --target-abi=help\n");
    printf("          wasm2native --target=x86_64 --cpu=help\n");
}
/* clang-format on */

#define PRINT_HELP_AND_EXIT() \
    do {                      \
        print_help();         \
        goto fail1;           \
    } while (0)

/* When print help info for target/cpu/target-abi/cpu-features, load this dummy
 * wasm file content rather than from an input file, the dummy wasm file content
 * is: magic header + version number */
static unsigned char dummy_wasm_file[8] = { 0x00, 0x61, 0x73, 0x6D,
                                            0x01, 0x00, 0x00, 0x00 };

int
main(int argc, char *argv[])
{
    char *wasm_file_name = NULL, *out_file_name = NULL;
    uint8 *wasm_file = NULL;
    uint32 wasm_file_size;
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

    /* Process options */
    for (argc--, argv++; argc > 0 && argv[0][0] == '-'; argc--, argv++) {
        if (!strcmp(argv[0], "-o")) {
            argc--, argv++;
            if (argc < 2)
                PRINT_HELP_AND_EXIT();
            out_file_name = argv[0];
        }
        else if (!strncmp(argv[0], "--target=", 9)) {
            if (argv[0][9] == '\0')
                PRINT_HELP_AND_EXIT();
            option.target_arch = argv[0] + 9;
            if (!strcmp(option.target_arch, "help")) {
                use_dummy_wasm = true;
            }
        }
        else if (!strncmp(argv[0], "--target-abi=", 13)) {
            if (argv[0][13] == '\0')
                PRINT_HELP_AND_EXIT();
            option.target_abi = argv[0] + 13;
            if (!strcmp(option.target_abi, "help")) {
                use_dummy_wasm = true;
            }
        }
        else if (!strncmp(argv[0], "--cpu=", 6)) {
            if (argv[0][6] == '\0')
                PRINT_HELP_AND_EXIT();
            option.target_cpu = argv[0] + 6;
            if (!strcmp(option.target_cpu, "help")) {
                use_dummy_wasm = true;
            }
        }
        else if (!strncmp(argv[0], "--cpu-features=", 15)) {
            if (argv[0][15] == '\0')
                PRINT_HELP_AND_EXIT();
            option.cpu_features = argv[0] + 15;
            if (!strcmp(option.cpu_features, "+help")) {
                use_dummy_wasm = true;
            }
        }
        else if (!strncmp(argv[0], "--opt-level=", 12)) {
            if (argv[0][12] == '\0')
                PRINT_HELP_AND_EXIT();
            option.opt_level = (uint32)atoi(argv[0] + 12);
            if (option.opt_level > 3)
                option.opt_level = 3;
        }
        else if (!strncmp(argv[0], "--size-level=", 13)) {
            if (argv[0][13] == '\0')
                PRINT_HELP_AND_EXIT();
            option.size_level = (uint32)atoi(argv[0] + 13);
            if (option.size_level > 3)
                option.size_level = 3;
            size_level_set = true;
        }
        else if (!strncmp(argv[0], "--format=", 9)) {
            if (argv[0][9] == '\0')
                PRINT_HELP_AND_EXIT();
            else if (!strcmp(argv[0] + 9, "object"))
                option.output_format = AOT_OBJECT_FILE;
            else if (!strcmp(argv[0] + 9, "llvmir-unopt"))
                option.output_format = AOT_LLVMIR_UNOPT_FILE;
            else if (!strcmp(argv[0] + 9, "llvmir-opt"))
                option.output_format = AOT_LLVMIR_OPT_FILE;
            else {
                printf("Invalid format %s.\n", argv[0] + 9);
                PRINT_HELP_AND_EXIT();
            }
        }
        else if (!strncmp(argv[0], "-v=", 3)) {
            log_verbose_level = atoi(argv[0] + 3);
            if (log_verbose_level < 0 || log_verbose_level > 5)
                PRINT_HELP_AND_EXIT();
        }
        else if (!strcmp(argv[0], "--no-sandbox-mode")) {
            option.no_sandbox_mode = true;
        }
        else if (!strncmp(argv[0], "--heap-size=", 12)) {
            if (argv[0][12] == '\0')
                PRINT_HELP_AND_EXIT();
            option.heap_size = atoi(argv[0] + 12);
        }
        else if (!strcmp(argv[0], "--disable-simd")) {
            option.enable_simd = false;
        }
        else if (!strcmp(argv[0], "--disable-llvm-lto")) {
            option.disable_llvm_lto = true;
        }
        else if (!strcmp(argv[0], "--version")) {
            uint32 major, minor, patch;
            wasm_runtime_get_version(&major, &minor, &patch);
            printf("wasm2native %u.%u.%u\n", major, minor, patch);
            return 0;
        }
        else
            PRINT_HELP_AND_EXIT();
    }

    if (!use_dummy_wasm && (argc == 0 || !out_file_name))
        PRINT_HELP_AND_EXIT();

    if (!size_level_set) {
        /**
         * Set opt level to 1 by default for Windows and MacOS as
         * they can not memory map out 0-2GB memory and might not
         * be able to meet the requirements of some AOT relocation
         * operations.
         */
        if (option.target_abi && !strcmp(option.target_abi, "msvc")) {
            LOG_VERBOSE("Set size level to 1 for Windows AOT file");
            option.size_level = 1;
        }
#if defined(_WIN32) || defined(_WIN32_) || defined(__APPLE__) \
    || defined(__MACH__)
        if (!option.target_abi) {
            LOG_VERBOSE("Set size level to 1 for Windows or MacOS AOT file");
            option.size_level = 1;
        }
#endif
    }

    if (!use_dummy_wasm) {
        wasm_file_name = argv[0];

        if (!strcmp(wasm_file_name, out_file_name)) {
            printf("Error: input file and output file are the same");
            return -1;
        }
    }

    /* initialize runtime environment */
    if (!wasm_runtime_init()) {
        printf("Init runtime environment failed.\n");
        return -1;
    }

    bh_log_set_verbose_level(log_verbose_level);

    bh_print_time("Begin to load wasm file");

    if (use_dummy_wasm) {
        /* load WASM byte buffer from dummy buffer */
        wasm_file_size = sizeof(dummy_wasm_file);
        wasm_file = dummy_wasm_file;
    }
    else {
        /* load WASM byte buffer from WASM bin file */
        if (!(wasm_file = (uint8 *)bh_read_file_to_buffer(wasm_file_name,
                                                          &wasm_file_size)))
            goto fail2;
    }

    if (wasm_file_size >= 4 /* length of MAGIC NUMBER */
        && !(wasm_file[0] == '\0' && wasm_file[1] == 'a' && wasm_file[2] == 's'
             && wasm_file[3] == 'm')) {
        printf("Invalid wasm file: magic header not detected\n");
        goto fail2;
    }

    /* load WASM module */
    if (!(wasm_module = wasm_runtime_load(wasm_file, wasm_file_size, error_buf,
                                          sizeof(error_buf)))) {
        printf("%s\n", error_buf);
        goto fail3;
    }

    if (!(comp_data = aot_create_comp_data(wasm_module, &option))) {
        printf("%s\n", aot_get_last_error());
        goto fail4;
    }

    bh_print_time("Begin to create compile context");

    if (!(comp_ctx = aot_create_comp_context(comp_data, &option))) {
        printf("%s\n", aot_get_last_error());
        goto fail5;
    }

    bh_print_time("Begin to compile");

    if (!aot_compile_wasm(comp_ctx)) {
        printf("%s\n", aot_get_last_error());
        goto fail6;
    }

    switch (option.output_format) {
        case AOT_LLVMIR_UNOPT_FILE:
        case AOT_LLVMIR_OPT_FILE:
            if (!aot_emit_llvm_file(comp_ctx, out_file_name)) {
                printf("%s\n", aot_get_last_error());
                goto fail6;
            }
            break;
        case AOT_OBJECT_FILE:
            if (!aot_emit_object_file(comp_ctx, out_file_name)) {
                printf("%s\n", aot_get_last_error());
                goto fail6;
            }
            break;
        default:
            break;
    }

    bh_print_time("Compile end");

    printf("Compile success, file %s was generated.\n", out_file_name);
    exit_status = EXIT_SUCCESS;

fail6:
    /* Destroy compiler context */
    aot_destroy_comp_context(comp_ctx);

fail5:
    /* Destroy compile data */
    aot_destroy_comp_data(comp_data);

fail4:
    /* Unload WASM module */
    wasm_runtime_unload(wasm_module);

fail3:
    /* free the file buffer */
    if (!use_dummy_wasm) {
        wasm_runtime_free(wasm_file);
    }

fail2:
    /* Destroy runtime environment */
    wasm_runtime_destroy();

fail1:
    bh_print_time("wasm2native exit");

    return exit_status;
}
