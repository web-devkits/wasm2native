#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if [[ -z ${WASM2NATIVE_DIR} ]]; then
    WASM2NATIVE_DIR=$PWD/../../..
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

if [[ $1 != "--no-simd" ]];then
    NATIVE_SIMD_FLAGS="-msse2 -msse3 -msse4"
    WASM_SIMD_FLAGS="-msimd128"
else
    NATIVE_SIMD_FLAGS=""
    WASM_SIMD_FLAGS=""
fi

echo "Compile dhrystone src to dhrystone native with gcc .."
gcc -O3 ${NATIVE_SIMD_FLAGS} -o out/dhrystone src/dhry_1.c src/dhry_2.c -I include

echo "Compile dhrystone src to dhrystone_mem32.wasm with wasi-sdk .."
/opt/wasi-sdk/bin/clang -O3 ${WASM_SIMD_FLAGS} \
    -o out/dhrystone_mem32.wasm src/dhry_1.c src/dhry_2.c -I include \
    -nostdlib -Wl,--no-entry -Wl,--export=__main_argc_argv \
    -Wl,--export=__heap_base -Wl,--export=__data_end \
    -Wl,--allow-undefined

echo "Compile dhrystone src to dhrystone_mem64.wasm with Android clang toolchain .."
${ANDROID_CLANG_TOOLCHAIN}/bin/clang \
        -O3 ${WASM_SIMD_FLAGS} \
        --target=wasm64-unknown-unknown \
        -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
        -Wl,--export=__main_argc_argv -Wl,--allow-undefined \
        -fvisibility=default -Wno-implicit-function-declaration \
        --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
        -I${WASM_NDK_ROOT}/include/wasm64 \
        -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
        -o out/dhrystone_mem64.wasm src/dhry_1.c src/dhry_2.c -I include

${WABT_HOME}/bin/wasm2wat \
        --enable-memory64 -o out/dhrystone_mem64.wast out/dhrystone_mem64.wasm

${WABT_HOME}/bin/wasm-objdump \
        -s -x -r -d out/dhrystone_mem64.wasm > out/dhrystone_mem64.dump

cd ${WORK_DIR}/out

echo "Compile dhrystone_mem32.wasm into dhrystone_mem32.o file"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o dhrystone_mem32.o dhrystone_mem32.wasm

echo "Compile dhrystone_mem64.wasm into dhrystone_mem64.o file"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o dhrystone_mem64.o dhrystone_mem64.wasm

echo "Compile dhrystone_mem64.wasm into dhrystone_mem64_nosandbox.o file"
${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o dhrystone_mem64_nosandbox.o dhrystone_mem64.wasm

echo "Compile dhrystone_mem64.wasm into dhrystone_mem64_nosandbox.ll file"
${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o dhrystone_mem64_nosandbox.ll dhrystone_mem64.wasm

echo "Generate dhrystone_mem32 binary"
gcc -O3 -o dhrystone_mem32 dhrystone_mem32.o -L ../build -lvmlib

echo "Generate dhrystone_mem64 binary"
gcc -O3 -o dhrystone_mem64 dhrystone_mem64.o -L ../build -lvmlib

echo "Generate dhrystone_mem64_nosandbox binary"
gcc -O3 -o dhrystone_mem64_nosandbox dhrystone_mem64_nosandbox.o -L ../build -lnosandbox -lm

echo "Done"
