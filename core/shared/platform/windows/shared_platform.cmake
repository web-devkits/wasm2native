# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

set (PLATFORM_SHARED_DIR ${CMAKE_CURRENT_LIST_DIR})

add_definitions(-DBH_PLATFORM_WINDOWS)
add_definitions(-DHAVE_STRUCT_TIMESPEC)
add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)

include_directories(${PLATFORM_SHARED_DIR})
include_directories(${PLATFORM_SHARED_DIR}/../include)

file (GLOB_RECURSE source_all ${PLATFORM_SHARED_DIR}/*.c
			      ${PLATFORM_SHARED_DIR}/*.cpp)

set (PLATFORM_SHARED_SOURCE ${source_all})
