#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if [[ -z ${WASM2NATIVE_DIR} ]]; then
    WASM2NATIVE_DIR=$PWD/../../..
fi

WORK_DIR=$PWD
OUT_DIR=$PWD/out
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

C_CASES="conditionals corrections64 corrections fannkuch ifs memops primes"
CPP_CASES="copy fasta skinning"

rm -fr ${OUT_DIR} && mkdir -p ${OUT_DIR}

echo "Build wasm2native-vmlib .."
rm -fr build && mkdir -p build
cd build
cmake ${WASM2NATIVE_DIR}/wasm2native-vmlib -DW2N_BUILD_WASM_APPLICATION=1
make -j

cd ${WORK_DIR}
if [ ! -d benchmarks ]; then
    git clone https://github.com/AndroidWasm/benchmarks.git
fi

for bench in $C_CASES
do
    cd ${WORK_DIR}/benchmarks/micro-suite

    echo "Build ${bench}.exe with gcc .."
    gcc -O3 ${bench}.c -o ${OUT_DIR}/${bench} -lm

    echo "Build ${bench}_mem32.wasm with wasi-sdk .."
    /opt/wasi-sdk/bin/clang -O3 \
            -nostdlib -Wl,--no-entry -Wl,--export=__main_argc_argv \
            -Wl,--allow-undefined \
            ${bench}.c \
            -o ${OUT_DIR}/${bench}_mem32.wasm

    echo "Build ${bench}_mem64.wasm with Android clang toolchain .."
    ${ANDROID_CLANG_TOOLCHAIN}/bin/clang \
            -O3 \
            --target=wasm64-unknown-unknown -msimd128 \
            -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
            -Wl,--export=__main_argc_argv -Wl,--allow-undefined \
            -fvisibility=default -Wno-implicit-function-declaration \
            --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
            -I${WASM_NDK_ROOT}/include/wasm64 \
            -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
            -o ${OUT_DIR}/${bench}_mem64.wasm \
            ${bench}.c

    ${WABT_HOME}/bin/wasm2wat \
            --enable-memory64 -o ${OUT_DIR}/${bench}_mem64.wast ${OUT_DIR}/${bench}_mem64.wasm

    ${WABT_HOME}/bin/wasm-objdump \
            -s -x -r -d ${OUT_DIR}/${bench}_mem64.wasm > ${OUT_DIR}/${bench}_mem64.dump

    cd ${WORK_DIR}/out

    echo "Compile ${bench}_mem32.wasm into ${bench}_mem32.o"
    ${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o ${bench}_mem32.o ${bench}_mem32.wasm

    echo "Compile ${bench}_mem64.wasm into ${bench}_mem64.o"
    ${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o ${bench}_mem64.o ${bench}_mem64.wasm

    echo "Compile ${bench}_mem64.wasm into ${bench}_mem64_nosandbox.o"
    ${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o ${bench}_mem64_nosandbox.o ${bench}_mem64.wasm

    echo "Compile ${bench}_mem64.wasm into ${bench}_mem64_nosandbox.ll"
    ${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o ${bench}_mem64_nosandbox.ll ${bench}_mem64.wasm

    echo "Generate ${bench}_mem32 binary"
    gcc -O3 -o ${bench}_mem32 ${bench}_mem32.o -L ../build -lvmlib

    echo "Generate ${bench}_mem64 binary"
    gcc -O3 -o ${bench}_mem64 ${bench}_mem64.o -L ../build -lvmlib

    echo "Generate ${bench}_mem64_nosandbox binary"
    gcc -O3 -o ${bench}_mem64_nosandbox ${bench}_mem64_nosandbox.o -L ../build -lnosandbox -lm
done

for bench in $CPP_CASES
do
    cd ${WORK_DIR}/benchmarks/micro-suite

    echo "Build ${bench}.exe with g++ .."
    g++ -O3 ${bench}.cpp -o ${OUT_DIR}/${bench} -lm

    echo "Build ${bench}_mem32.wasm with wasi-sdk .."
    /opt/wasi-sdk/bin/clang++ -O3 \
            -nostdlib -Wl,--no-entry -Wl,--export=__main_argc_argv \
            -Wl,--allow-undefined \
            ${bench}.cpp \
            -o ${OUT_DIR}/${bench}_mem32.wasm

    echo "Build ${bench}_mem64.wasm with Android clang toolchain .."
    ${ANDROID_CLANG_TOOLCHAIN}/bin/clang++ \
            -O3 \
            --target=wasm64-unknown-unknown -msimd128 \
            -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
            -Wl,--export=__main_argc_argv -Wl,--allow-undefined \
            -fvisibility=default -Wno-implicit-function-declaration \
            --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
            -I${WASM_NDK_ROOT}/include/wasm64 \
            -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
            -o ${OUT_DIR}/${bench}_mem64.wasm \
            ${bench}.cpp

    ${WABT_HOME}/bin/wasm2wat \
            --enable-memory64 -o ${OUT_DIR}/${bench}_mem64.wast ${OUT_DIR}/${bench}_mem64.wasm

    ${WABT_HOME}/bin/wasm-objdump \
            -s -x -r -d ${OUT_DIR}/${bench}_mem64.wasm > ${OUT_DIR}/${bench}_mem64.dump

    cd ${WORK_DIR}/out

    echo "Compile ${bench}_mem32.wasm into ${bench}_mem32.o"
    ${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o ${bench}_mem32.o ${bench}_mem32.wasm

    echo "Compile ${bench}_mem64.wasm into ${bench}_mem64.o"
    ${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o ${bench}_mem64.o ${bench}_mem64.wasm

    echo "Compile ${bench}_mem64.wasm into ${bench}_mem64_nosandbox.o"
    ${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o ${bench}_mem64_nosandbox.o ${bench}_mem64.wasm

    echo "Compile ${bench}_mem64.wasm into ${bench}_mem64_nosandbox.ll"
    ${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o ${bench}_mem64_nosandbox.ll ${bench}_mem64.wasm

    echo "Generate ${bench}_mem32 binary"
    g++ -O3 -o ${bench}_mem32 ${bench}_mem32.o -L ../build -lvmlib

    echo "Generate ${bench}_mem64 binary"
    g++ -O3 -o ${bench}_mem64 ${bench}_mem64.o -L ../build -lvmlib

    echo "Generate ${bench}_mem64_nosandbox binary"
    g++ -O3 -o ${bench}_mem64_nosandbox ${bench}_mem64_nosandbox.o -L ../build -lnosandbox -lm
done

echo "Done"
