#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if [[ -z ${WASM2NATIVE_DIR} ]]; then
    WASM2NATIVE_DIR=$PWD/../..
fi

WORK_DIR=$PWD
WASM2NATIVE_CMD=${WASM2NATIVE_DIR}/wasm2native-compiler/build/wasm2native

if [ -z "${ANDROID_NDK_HOME}" ]; then
  echo "ANDROID_NDK_HOME is not set"
  exit 1
fi
if [ -z "${ANDROID_CLANG_TOOLCHAIN}" ]; then
  echo "ANDROID_CLANG_TOOLCHAIN is not set"
  exit 1
fi
if [ -z "${WABT_HOME}" ]; then
  echo "WABT_HOME is not set"
  exit 1
fi
if [ -z "${WASM_NDK_ROOT}" ]; then
  echo "WASM_NDK_ROOT is not set"
  exit 1
fi

rm -fr out && mkdir -p out

echo "Build wasm2native-vmlib .."
rm -fr build && mkdir -p build
cd build
cmake ${WASM2NATIVE_DIR}/wasm2native-vmlib -DW2N_BUILD_WASM_APPLICATION=1
make -j

cd ${WORK_DIR}

echo "Build test with gcc .."
gcc -O3 -I. -o out/test main.c -Wno-format

echo "Build test_mem32.wasm with wasi-sdk .."
/opt/wasi-sdk/bin/clang -O3 -I. -msimd128 \
        -nostdlib -Wl,--no-entry -Wl,--export=__main_void \
        -Wl,--allow-undefined \
        -Wno-format \
        -o out/test_mem32.wasm \
        main.c

echo "Build test_mem64.wasm with Android clang toolchain .."
${ANDROID_CLANG_TOOLCHAIN}/bin/clang -O3 -I. -msimd128 \
        --target=wasm64-unknown-unknown -Wno-format \
        -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
        -Wl,--export=__main_void -Wl,--allow-undefined \
        -fvisibility=default -Wno-implicit-function-declaration \
        --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
        -I${WASM_NDK_ROOT}/include/wasm64 \
        -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
        -o out/test_mem64.wasm \
        main.c

${WABT_HOME}/bin/wasm2wat \
        --enable-memory64 -o out/test_mem64.wast out/test_mem64.wasm

${WABT_HOME}/bin/wasm-objdump \
        -s -x -r -d out/test_mem64.wasm > out/test_mem64.dump

cd ${WORK_DIR}/out

echo "Compile test_mem32.wasm into test_mem32.o"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o test_mem32.o test_mem32.wasm

echo "Compile test_mem64.wasm into test_mem64.o"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o test_mem64.o test_mem64.wasm

echo "Compile test_mem64.wasm into test_mem64_nosandbox.o"
${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o test_mem64_nosandbox.o test_mem64.wasm

echo "Compile test_mem64.wasm into test_mem64_nosandbox.ll"
${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o test_mem64_nosandbox.ll test_mem64.wasm

echo "Generate test_mem32 binary"
gcc -O3 -o test_mem32 test_mem32.o -L ../build -lvmlib

echo "Generate test_mem64 binary"
gcc -O3 -o test_mem64 test_mem64.o -L ../build -lvmlib

echo "Generate test_mem64_nosandbox binary"
gcc -O3 -o test_mem64_nosandbox test_mem64_nosandbox.o -L ../build -lnosandbox -lm

echo "Done"
