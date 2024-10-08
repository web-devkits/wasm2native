# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception


set (MEM_ALLOC_DIR ${CMAKE_CURRENT_LIST_DIR})

include_directories(${MEM_ALLOC_DIR})

if (W2N_BUILD_GC_VERIFY EQUAL 1)
    add_definitions (-DBH_ENABLE_GC_VERIFY=1)
endif ()

file (GLOB_RECURSE source_all
      ${MEM_ALLOC_DIR}/ems/*.c
      ${MEM_ALLOC_DIR}/tlsf/*.c
      ${MEM_ALLOC_DIR}/mem_alloc.c)

set (MEM_ALLOC_SHARED_SOURCE ${source_all})

