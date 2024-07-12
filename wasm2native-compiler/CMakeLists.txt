# Copyright (C) 2019 Intel Corporation. All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required (VERSION 3.14)

include(CheckPIESupported)

if (NOT DEFINED W2N_BUILD_PLATFORM)
  if (CMAKE_SYSTEM_NAME)
    string(TOLOWER ${CMAKE_SYSTEM_NAME} W2N_BUILD_PLATFORM)
  else()
    string(TOLOWER ${CMAKE_HOST_SYSTEM_NAME} W2N_BUILD_PLATFORM)
  endif()
endif()

if (NOT W2N_BUILD_PLATFORM STREQUAL "windows")
  project (aot-compiler)
else()
  project (aot-compiler C ASM CXX)
  enable_language (ASM_MASM)
  add_definitions(-DCOMPILING_WASM_RUNTIME_API=1)
endif()

set (CMAKE_CXX_STANDARD 17)

if (NOT DEFINED W2N_BUILD_PLATFORM)
  set (W2N_BUILD_PLATFORM "linux")
endif()

# Reset default linker flags
set (CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "")
set (CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS "")

add_definitions(-DWASM_ENABLE_CUSTOM_NAME_SECTION=1)
add_definitions(-DWASM_ENABLE_LOAD_CUSTOM_SECTION=1)

if (W2N_BUILD_LLVM_LEGACY_PM EQUAL 1)
  add_definitions(-DWASM_ENABLE_LLVM_LEGACY_PM=1)
endif ()

if (NOT W2N_BUILD_TARGET)
  string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" HOST_SYSTEM_PROCESSOR)
  if (${HOST_SYSTEM_PROCESSOR} STREQUAL "x86_64")
    set (W2N_BUILD_TARGET "X86_64")
  elseif (${HOST_SYSTEM_PROCESSOR} STREQUAL "i686")
    set (W2N_BUILD_TARGET "X86_32")
  elseif (${HOST_SYSTEM_PROCESSOR} STREQUAL "amd64")
    set (W2N_BUILD_TARGET "AMD_64")
  elseif (${HOST_SYSTEM_PROCESSOR} STREQUAL "aarch64"
          OR ${HOST_SYSTEM_PROCESSOR} STREQUAL "arm64")
    set (W2N_BUILD_TARGET "AARCH64")
  elseif (${HOST_SYSTEM_PROCESSOR} MATCHES "arm.*")
    message(STATUS "Assuming ${CMAKE_HOST_SYSTEM_PROCESSOR} as ARM_32")
    set (W2N_BUILD_TARGET "ARM_32")
  elseif (${HOST_SYSTEM_PROCESSOR} STREQUAL "mips")
    set (W2N_BUILD_TARGET "MIPS_32")
  elseif (${HOST_SYSTEM_PROCESSOR} STREQUAL "xtensa")
    set (W2N_BUILD_TARGET "XTENSA_32")
  elseif (${HOST_SYSTEM_PROCESSOR} STREQUAL "riscv64")
    set (W2N_BUILD_TARGET "RISCV64")
  elseif (${HOST_SYSTEM_PROCESSOR} STREQUAL "riscv")
    message(STATUS "Assuming ${CMAKE_HOST_SYSTEM_PROCESSOR} as RISCV32")
    set (W2N_BUILD_TARGET "RISCV32")
  else ()
    message (FATAL_ERROR "Unsupported CMAKE_HOST_SYSTEM_PROCESSOR "
	                 "${CMAKE_HOST_SYSTEM_PROCESSOR}")
  endif()

  if (W2N_BUILD_PLATFORM STREQUAL "windows")
    if (("${CMAKE_GENERATOR_PLATFORM}" STREQUAL "Win32"))
      set (W2N_BUILD_TARGET "X86_32")
    endif()
  endif ()
endif ()

string(TOUPPER ${W2N_BUILD_TARGET} W2N_BUILD_TARGET)

# Add definitions for the build target
if (W2N_BUILD_TARGET STREQUAL "X86_64")
  add_definitions(-DBUILD_TARGET_X86_64)
elseif (W2N_BUILD_TARGET STREQUAL "AMD_64")
  add_definitions(-DBUILD_TARGET_AMD_64)
elseif (W2N_BUILD_TARGET STREQUAL "X86_32")
  add_definitions(-DBUILD_TARGET_X86_32)
elseif (W2N_BUILD_TARGET MATCHES "AARCH64.*")
  add_definitions(-DBUILD_TARGET_AARCH64)
  add_definitions(-DBUILD_TARGET="${W2N_BUILD_TARGET}")
elseif (W2N_BUILD_TARGET MATCHES "ARM.*")
  add_definitions(-DBUILD_TARGET_ARM)
  add_definitions(-DBUILD_TARGET="${W2N_BUILD_TARGET}")
elseif (W2N_BUILD_TARGET STREQUAL "RISCV64" OR W2N_BUILD_TARGET STREQUAL "RISCV64_LP64D")
  add_definitions(-DBUILD_TARGET_RISCV64_LP64D)
elseif (W2N_BUILD_TARGET STREQUAL "RISCV64_LP64")
  add_definitions(-DBUILD_TARGET_RISCV64_LP64)
elseif (W2N_BUILD_TARGET STREQUAL "RISCV32" OR W2N_BUILD_TARGET STREQUAL "RISCV32_ILP32D")
  add_definitions(-DBUILD_TARGET_RISCV32_ILP32D)
elseif (W2N_BUILD_TARGET STREQUAL "RISCV32_ILP32")
  add_definitions(-DBUILD_TARGET_RISCV32_ILP32)
else ()
  message (FATAL_ERROR "-- Build target isn't set")
endif ()

message ("-- Build as target ${W2N_BUILD_TARGET}")

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
    add_definitions (-m32)
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -m32")
    set (CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")
  endif ()
endif ()

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif (NOT CMAKE_BUILD_TYPE)
message ("-- CMAKE_BUILD_TYPE = " ${CMAKE_BUILD_TYPE})

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_definitions(-DBH_DEBUG=1)
endif ()

# Enable LLVM
if (NOT W2N_BUILD_WITH_CUSTOM_LLVM)
  set (LLVM_SRC_ROOT "${PROJECT_SOURCE_DIR}/../core/deps/llvm")
  if (W2N_BUILD_PLATFORM STREQUAL "windows")
    if (NOT EXISTS "${LLVM_SRC_ROOT}/win32build")
      message (FATAL_ERROR "Cannot find LLVM dir: ${LLVM_SRC_ROOT}/win32build")
    endif ()
    set (CMAKE_PREFIX_PATH "${LLVM_SRC_ROOT}/win32build;${CMAKE_PREFIX_PATH}")
  else()
    if (NOT EXISTS "${LLVM_SRC_ROOT}/build")
      message (FATAL_ERROR "Cannot find LLVM dir: ${LLVM_SRC_ROOT}/build")
    endif ()
    set (CMAKE_PREFIX_PATH "${LLVM_SRC_ROOT}/build;${CMAKE_PREFIX_PATH}")
  endif ()
endif ()

find_package(LLVM REQUIRED CONFIG)
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if ("$ENV{COLLECT_CODE_COVERAGE}" STREQUAL "1" OR COLLECT_CODE_COVERAGE EQUAL 1)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fprofile-arcs -ftest-coverage")
  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fprofile-arcs -ftest-coverage")
  message ("-- Collect code coverage enabled")
endif ()

if (NOT (CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang"))
  if(NOT MSVC)
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")
  else()
    set (CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /SAFESEH:NO")
  endif()
endif()

if (NOT MSVC)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wformat -Wformat-security \
                                       -ffunction-sections -fdata-sections \
                                       -Wno-unused-parameter -Wno-pedantic")
  # Remove the extra spaces for better make log
  string (REGEX REPLACE "  *" " " CMAKE_C_FLAGS ${CMAKE_C_FLAGS})
endif()

set (SHARED_DIR ../core/shared)
set (IWASM_DIR ../core/iwasm)

include_directories (${SHARED_DIR}/include ${IWASM_DIR}/include)

enable_language (ASM)

include (${SHARED_DIR}/platform/${W2N_BUILD_PLATFORM}/shared_platform.cmake)
include (${SHARED_DIR}/utils/shared_utils.cmake)
include (${SHARED_DIR}/utils/uncommon/shared_uncommon.cmake)
include (${IWASM_DIR}/common/iwasm_common.cmake)
include (${IWASM_DIR}/compilation/iwasm_compl.cmake)

# set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wconversion -Wsign-conversion")
if (W2N_BUILD_TARGET MATCHES "X86_.*" OR W2N_BUILD_TARGET STREQUAL "AMD_64")
  if (NOT (CMAKE_C_COMPILER MATCHES ".*clang.*" OR CMAKE_C_COMPILER_ID MATCHES ".*Clang" OR MSVC))
    set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mindirect-branch-register")
    # UNDEFINED BEHAVIOR, refer to https://en.cppreference.com/w/cpp/language/ub
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined \
                                          -fno-sanitize=bounds,bounds-strict,alignment \
                                          -fno-sanitize-recover")
      set(lib_ubsan -fsanitize=undefined)
    endif()
  else ()
    # UNDEFINED BEHAVIOR, refer to https://en.cppreference.com/w/cpp/language/ub
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined \
                                          -fno-sanitize=bounds,alignment \
                                          -fno-sanitize-recover")
      set(lib_ubsan -fsanitize=undefined)
    endif()
  endif()
endif ()

if (NOT MSVC)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fstack-protector-strong --param ssp-buffer-size=4")
endif()

if (NOT MSVC)
  add_definitions(-D_FORTIFY_SOURCE=2)
  set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ftrapv")
endif()

if (WIN32)
  add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

# message ("-- CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")

add_library (vmlib
             ${PLATFORM_SHARED_SOURCE}
             ${UTILS_SHARED_SOURCE}
             ${UNCOMMON_SHARED_SOURCE}
             ${IWASM_COMMON_SOURCE}
             ${IWASM_INTERP_SOURCE}
             ${IWASM_AOT_SOURCE})

add_library (aotclib ${IWASM_COMPL_SOURCE})

add_executable (wasm2native main.c)
check_pie_supported()
set_target_properties (wasm2native PROPERTIES POSITION_INDEPENDENT_CODE ON)

if (LLVM_LINK_LLVM_DYLIB)
  set(W2NC_LINK_LLVM_LIBS LLVM)
else()
  set(W2NC_LINK_LLVM_LIBS ${LLVM_AVAILABLE_LIBS})
endif()

if (NOT MSVC)
  target_link_libraries (wasm2native aotclib vmlib ${W2NC_LINK_LLVM_LIBS} ${lib_ubsan}
                         -lm -lpthread ${lib_lldb} ${UV_A_LIBS})
  if (MINGW)
      target_link_libraries (wasm2native ssp.a ws2_32)
  else()
      target_link_libraries (wasm2native -ldl)
  endif()
else()
  target_link_libraries (wasm2native aotclib vmlib  ${lib_lldb} ${W2NC_LINK_LLVM_LIBS} ${lib_ubsan}
                         ${UV_A_LIBS})
endif()

install (TARGETS wasm2native DESTINATION bin)
