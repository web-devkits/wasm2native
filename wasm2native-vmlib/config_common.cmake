# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

string (TOUPPER ${W2N_BUILD_TARGET} W2N_BUILD_TARGET)

# Add definitions for the build target
if (W2N_BUILD_TARGET STREQUAL "X86_64")
  add_definitions (-DBUILD_TARGET_X86_64)
elseif (W2N_BUILD_TARGET STREQUAL "AMD_64")
  add_definitions (-DBUILD_TARGET_AMD_64)
elseif (W2N_BUILD_TARGET STREQUAL "X86_32")
  add_definitions (-DBUILD_TARGET_X86_32)
elseif (W2N_BUILD_TARGET MATCHES "ARM.*")
  if (W2N_BUILD_TARGET MATCHES "(ARM.*)_VFP")
    add_definitions (-DBUILD_TARGET_ARM_VFP)
    add_definitions (-DBUILD_TARGET="${CMAKE_MATCH_1}")
  else ()
    add_definitions (-DBUILD_TARGET_ARM)
    add_definitions (-DBUILD_TARGET="${W2N_BUILD_TARGET}")
  endif ()
elseif (W2N_BUILD_TARGET MATCHES "THUMB.*")
  if (W2N_BUILD_TARGET MATCHES "(THUMB.*)_VFP")
    add_definitions (-DBUILD_TARGET_THUMB_VFP)
    add_definitions (-DBUILD_TARGET="${CMAKE_MATCH_1}")
  else ()
    add_definitions (-DBUILD_TARGET_THUMB)
    add_definitions (-DBUILD_TARGET="${W2N_BUILD_TARGET}")
  endif ()
elseif (W2N_BUILD_TARGET MATCHES "AARCH64.*")
  add_definitions (-DBUILD_TARGET_AARCH64)
  add_definitions (-DBUILD_TARGET="${W2N_BUILD_TARGET}")
elseif (W2N_BUILD_TARGET STREQUAL "MIPS")
  add_definitions (-DBUILD_TARGET_MIPS)
elseif (W2N_BUILD_TARGET STREQUAL "XTENSA")
  add_definitions (-DBUILD_TARGET_XTENSA)
elseif (W2N_BUILD_TARGET STREQUAL "RISCV64" OR W2N_BUILD_TARGET STREQUAL "RISCV64_LP64D")
  add_definitions (-DBUILD_TARGET_RISCV64_LP64D)
elseif (W2N_BUILD_TARGET STREQUAL "RISCV64_LP64")
  add_definitions (-DBUILD_TARGET_RISCV64_LP64)
elseif (W2N_BUILD_TARGET STREQUAL "RISCV32" OR W2N_BUILD_TARGET STREQUAL "RISCV32_ILP32D")
  add_definitions (-DBUILD_TARGET_RISCV32_ILP32D)
elseif (W2N_BUILD_TARGET STREQUAL "RISCV32_ILP32")
  add_definitions (-DBUILD_TARGET_RISCV32_ILP32)
elseif (W2N_BUILD_TARGET STREQUAL "ARC")
  add_definitions (-DBUILD_TARGET_ARC)
else ()
  message (FATAL_ERROR "-- W2N build target isn't set")
endif ()

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
  if (W2N_BUILD_TARGET STREQUAL "X86_64" OR W2N_BUILD_TARGET STREQUAL "AMD_64"
      OR W2N_BUILD_TARGET MATCHES "AARCH64.*" OR W2N_BUILD_TARGET MATCHES "RISCV64.*")
  if (NOT W2N_BUILD_PLATFORM STREQUAL "windows")
    # Add -fPIC flag if build as 64-bit
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC")
    set (CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "${CMAKE_SHARED_LIBRARY_LINK_C_FLAGS} -fPIC")
    set (CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "${CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS} -fPIC")
  endif ()
  else ()
    include (CheckCCompilerFlag)
    Check_C_Compiler_Flag (-m32 M32_OK)
    if (M32_OK OR W2N_BUILD_TARGET STREQUAL "X86_32")
      add_definitions (-m32)
      set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
      set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")
    endif ()
  endif ()
endif ()

if (W2N_BUILD_TARGET MATCHES "ARM.*")
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -marm")
elseif (W2N_BUILD_TARGET MATCHES "THUMB.*")
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mthumb")
  set (CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -Wa,-mthumb")
endif ()

########################################

message ("-- Build Configurations:")
message ("     Build as target ${W2N_BUILD_TARGET}")
message ("     CMAKE_BUILD_TYPE " ${CMAKE_BUILD_TYPE})
if (W2N_BUILD_WASM_APPLICATION EQUAL 1)
    message ("     Extra application lib enabled")
endif ()