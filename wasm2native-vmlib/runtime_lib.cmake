# Copyright (C) 2019 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if (NOT DEFINED W2N_ROOT_DIR)
  set (W2N_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../)
endif ()
if (NOT DEFINED SHARED_DIR)
  set (SHARED_DIR ${W2N_ROOT_DIR}/core/shared)
endif ()
if (NOT DEFINED IWASM_DIR)
  set (IWASM_DIR ${W2N_ROOT_DIR}/core/iwasm)
endif ()

# Set W2N_BUILD_TARGET, currently values supported:
# "X86_64", "AMD_64", "X86_32", "AARCH64[sub]", "ARM[sub]", "THUMB[sub]",
# "MIPS", "XTENSA", "RISCV64[sub]", "RISCV32[sub]"
if (NOT DEFINED W2N_BUILD_TARGET)
  if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
    set (W2N_BUILD_TARGET "AARCH64")
  elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL "riscv64")
    set (W2N_BUILD_TARGET "RISCV64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 8)
    # Build as X86_64 by default in 64-bit platform
    set (W2N_BUILD_TARGET "X86_64")
  elseif (CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Build as X86_32 by default in 32-bit platform
    set (W2N_BUILD_TARGET "X86_32")
  else ()
    message (SEND_ERROR "Unsupported build target platform!")
  endif ()
endif ()

if (NOT MSVC)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99 -ffunction-sections -fdata-sections \
                     -Wall -Wno-unused-parameter -Wno-pedantic")
endif ()

# include the build config template file
include (${CMAKE_CURRENT_LIST_DIR}/config_common.cmake)

include_directories (${IWASM_DIR}/include)
include_directories (${IWASM_DIR}/interpreter)

option(EXCLUDE_PTREHAD_SROUCE "Exclude pthread from sources" ON)
include (${SHARED_DIR}/platform/${W2N_BUILD_PLATFORM}/shared_platform.cmake)
include (${SHARED_DIR}/utils/shared_utils.cmake)
include (${SHARED_DIR}/mem-alloc/mem_alloc.cmake)
include (${IWASM_DIR}/libraries/libc-builtin/libc_builtin.cmake)

set (source_all
  ${PLATFORM_SHARED_SOURCE}
  ${UTILS_SHARED_SOURCE}
  ${MEM_ALLOC_SHARED_SOURCE}
  ${LIBC_BUILTIN_SOURCE}
)

set (W2N_RUNTIME_LIB_SOURCE ${source_all})
