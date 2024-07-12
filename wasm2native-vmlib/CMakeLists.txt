# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required (VERSION 3.0)

project (vmlib)

set (CMAKE_VERBOSE_MAKEFILE OFF)

if (NOT DEFINED W2N_BUILD_PLATFORM)
  string (TOLOWER ${CMAKE_HOST_SYSTEM_NAME} W2N_BUILD_PLATFORM)
endif ()

# Reset default linker flags
set (CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set (CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

set (CMAKE_C_STANDARD 99)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif ()

set (W2N_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/..)

include (${CMAKE_CURRENT_LIST_DIR}/runtime_lib.cmake)

set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wformat -Wformat-security -Wshadow -Wno-unused-parameter")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wformat -Wformat-security -Wno-unused")

if (W2N_BUILD_TARGET MATCHES "X86_.*" OR W2N_BUILD_TARGET STREQUAL "AMD_64")
  if (NOT (CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"))
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mindirect-branch-register")
  endif ()
endif ()

if (NOT W2N_BUILD_WASM_APPLICATION EQUAL 1)
  add_library(vmlib_static STATIC ${W2N_RUNTIME_LIB_SOURCE})
  set_target_properties(vmlib_static PROPERTIES OUTPUT_NAME vmlib)
else ()
  add_library(vmlib_static STATIC ${W2N_RUNTIME_LIB_SOURCE}
              application/wasm_application.c application/main.c)
  set_target_properties(vmlib_static PROPERTIES OUTPUT_NAME vmlib)
endif ()

include (${IWASM_DIR}/libraries/libc-nosandbox/libc_nosandbox.cmake)

add_library(nosandbox_static ${LIBC_NOSANDBOX_SOURCE})
set_target_properties(nosandbox_static PROPERTIES OUTPUT_NAME nosandbox)
