# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (UTILS_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

include_directories(${UTILS_SHARED_DIR})

file (GLOB source_all ${UTILS_SHARED_DIR}/*.c)

if (EXCLUDE_PTREHAD_SROUCE)
    add_compile_definitions(W2N_ENABLE_PTHREAD=0)
    list(REMOVE_ITEM source_all ${UTILS_SHARED_DIR}/bh_hashmap.c)
endif()

set (UTILS_SHARED_SOURCE ${source_all})
