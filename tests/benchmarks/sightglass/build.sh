#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if [[ -z ${WASM2NATIVE_DIR} ]]; then
    WASM2NATIVE_DIR=$PWD/../../..
fi

WORK_DIR=$PWD
OUT_DIR=$PWD/out
JETSTREAM_DIR=$PWD/perf-automation/benchmarks/JetStream2/wasm
WASM2NATIVE_CMD=${WASM2NATIVE_DIR}/wasm2native-compiler/build/wasm2native
SHOOTOUT_CASES="ackermann base64 fib2 gimli heapsort matrix memmove minicsv \
                nestedloop nestedloop2 nestedloop3 random seqhash sieve \
                strchr switch2"

if [[ $1 != "--no-simd" ]];then
    NATIVE_SIMD_FLAGS="-msse2 -msse3 -msse4"
    WASM_SIMD_FLAGS="-msimd128"
else
    NATIVE_SIMD_FLAGS=""
    WASM_SIMD_FLAGS=""
fi

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

if [ ! -d sightglass ]; then
    git clone https://github.com/wasm-micro-runtime/sightglass.git
fi

cd sightglass/benchmarks/shootout

for bench in $SHOOTOUT_CASES
do
    echo "Build ${bench} native"
    gcc -O3 -o ${OUT_DIR}/${bench} -Dblack_box=set_res -Dbench=${bench} \
        -I../../include ${bench}.c main/main_${bench}.c main/my_libc.c \
        ${NATIVE_SIMD_FLAGS}

    echo "Build ${bench}_mem32.wasm with wasi-sdk"
    /opt/wasi-sdk/bin/clang -O3 -nostdlib \
        -Wno-unknown-attributes \
        -Dblack_box=set_res ${WASM_SIMD_FLAGS} \
        -I../../include -DNOSTDLIB_MODE \
        -Wl,--initial-memory=1310720,--allow-undefined \
        -Wl,--strip-all,--no-entry \
        -o ${OUT_DIR}/${bench}_mem32.wasm \
        -Wl,--export=app_main -Wl,--export=__main_argc_argv \
        ${bench}.c main/main_${bench}.c main/my_libc.c

    echo "Build ${bench}_mem64.wasm with Android clang toolchain"
    ${ANDROID_CLANG_TOOLCHAIN}/bin/clang \
        --target=wasm64-unknown-unknown -O3 \
        -Wno-unknown-attributes \
        -Dblack_box=set_res -msimd128 \
        -Wl,--initial-memory=1310720 -Wl,--allow-undefined \
        -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
        -Wl,--export=app_main -Wl,--export=__main_argc_argv \
        -fvisibility=default -Wno-implicit-function-declaration \
        --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
        -I../../include \
        -I${WASM_NDK_ROOT}/include/wasm64 \
        -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
        -o ${OUT_DIR}/${bench}_mem64.wasm \
        ${bench}.c main/main_${bench}.c main/my_libc.c

    ${WABT_HOME}/bin/wasm2wat --enable-memory64 \
        -o ${OUT_DIR}/${bench}_mem64.wast ${OUT_DIR}/${bench}_mem64.wasm

    ${WABT_HOME}/bin/wasm-objdump \
        -s -x -r -d ${OUT_DIR}/${bench}_mem64.wasm > ${OUT_DIR}/${bench}_mem64.dump

    echo "Compile ${bench}_mem32.wasm into ${bench}_mem32.o"
    ${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o ${OUT_DIR}/${bench}_mem32.o ${OUT_DIR}/${bench}_mem32.wasm

    echo "Compile ${bench}_mem64.wasm into ${bench}_mem64.o"
    ${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o ${OUT_DIR}/${bench}_mem64.o ${OUT_DIR}/${bench}_mem64.wasm

    echo "Compile ${bench}_mem64.wasm into ${bench}_mem64_nosandbox.o"
    ${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o ${OUT_DIR}/${bench}_mem64_nosandbox.o ${OUT_DIR}/${bench}_mem64.wasm

    echo "Compile ${bench}_mem64.wasm into ${bench}_mem64_nosandbox.ll"
    ${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o ${OUT_DIR}/${bench}_mem64_nosandbox.ll ${OUT_DIR}/${bench}_mem64.wasm

    echo "Generate ${bench}_mem32 binary"
    gcc -O3 -o ${OUT_DIR}/${bench}_mem32 ${OUT_DIR}/${bench}_mem32.o -L ${WORK_DIR}/build -lvmlib

    echo "Generate ${bench}_mem64 binary"
    gcc -O3 -o ${OUT_DIR}/${bench}_mem64 ${OUT_DIR}/${bench}_mem64.o -L ${WORK_DIR}/build -lvmlib

    echo "Generate ${bench}_mem64_nosandbox binary"
    gcc -O3 -o ${OUT_DIR}/${bench}_mem64_nosandbox ${OUT_DIR}/${bench}_mem64_nosandbox.o -L ${WORK_DIR}/build -lnosandbox -lm
done

echo "Done"
