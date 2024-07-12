# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (LIBC_NOSANDBOX_DIR ${CMAKE_CURRENT_LIST_DIR})

include_directories(${LIBC_NOSANDBOX_DIR})

file (GLOB source_all ${LIBC_NOSANDBOX_DIR}/*.c)

set (LIBC_NOSANDBOX_SOURCE ${source_all})
