# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (PLATFORM_COMMON_POSIX_DIR ${CMAKE_CURRENT_LIST_DIR})

file (GLOB_RECURSE source_all ${PLATFORM_COMMON_POSIX_DIR}/*.c)
set (EXCLUDE_FILE ${PLATFORM_COMMON_POSIX_DIR}/posix_thread.c)

if (EXCLUDE_PTREHAD_SROUCE)
    list (FILTER source_all EXCLUDE REGEX ${EXCLUDE_FILE})
    message (STATUS "Excluding file: ${EXCLUDE_FILE}")
endif()

set (PLATFORM_COMMON_POSIX_SOURCE ${source_all} )
