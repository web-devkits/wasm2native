# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (IWASM_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR})

include_directories (${IWASM_COMMON_DIR})

add_definitions(-DBH_MALLOC=wasm_runtime_malloc)
add_definitions(-DBH_FREE=wasm_runtime_free)

file (GLOB source_all ${IWASM_COMMON_DIR}/*.c)

set (IWASM_COMMON_SOURCE ${source_all})
