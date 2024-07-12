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
POLYBENCH_CASES="datamining linear-algebra medley stencils"

if [[ $1 != "--no-simd" ]];then
    NATIVE_SIMD_FLAGS="-msse2 -msse3 -msse4"
    WASM_SIMD_FLAGS="-msimd128 -msse2 -msse3 -msse4"
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

if [ ! -d PolyBenchC-4.2.1 ]; then
    git clone https://github.com/MatthiasJReisinger/PolyBenchC-4.2.1.git
fi

cd PolyBenchC-4.2.1

for case in $POLYBENCH_CASES
do
    files=`find ${case} -name "*.c"`
    for file in ${files}
    do
        file_name=${file##*/}
        if [[ ${file_name} == "Nussinov.orig.c" ]]; then
            continue
        fi

        echo "Build ${file_name%.*} native"
        gcc -O3 -I utilities -I ${file%/*} utilities/polybench.c ${file} \
                -DPOLYBENCH_TIME -lm -o ${OUT_DIR}/${file_name%.*} \
                ${NATIVE_SIMD_FLAGS}

        echo "Build ${file_name%.*}_mem32.wasm with wasi-sdk"
        /opt/wasi-sdk/bin/clang -O3 -I utilities -I ${file%/*}      \
                utilities/polybench.c ${file} -nostdlib             \
                -Wl,--export=__main_argc_argv -Wl,--no-entry        \
                -Wl,--export=__heap_base -Wl,--export=__data_end    \
                -DPOLYBENCH_TIME -o ${OUT_DIR}/${file_name%.*}_mem32.wasm \
                -D_WASI_EMULATED_PROCESS_CLOCKS \
                -Wl,--allow-undefined \
                ${WASM_SIMD_FLAGS}

        echo "Build ${file_name%.*}_mem64.wasm with Android clang toolchain"
        ${ANDROID_CLANG_TOOLCHAIN}/bin/clang \
                --target=wasm64-unknown-unknown -O3 \
                -Wl,--allow-undefined \
                -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
                -Wl,--export=__main_argc_argv \
                -DPOLYBENCH_TIME -D_WASI_EMULATED_PROCESS_CLOCKS \
                -fvisibility=default -Wno-implicit-function-declaration \
                --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
                -I utilities -I ${file%/*} \
                -I${WASM_NDK_ROOT}/include/wasm64 \
                -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
                -o ${OUT_DIR}/${file_name%.*}_mem64.wasm \
                utilities/polybench.c ${file}

        ${WABT_HOME}/bin/wasm2wat --enable-memory64 \
                -o ${OUT_DIR}/${file_name%.*}_mem64.wast ${OUT_DIR}/${file_name%.*}_mem64.wasm

        ${WABT_HOME}/bin/wasm-objdump \
                -s -x -r -d ${OUT_DIR}/${file_name%.*}_mem64.wasm > ${OUT_DIR}/${file_name%.*}_mem64.dump

        echo "Compile ${file_name%.*}_mem32.wasm into ${file_name%.*}_mem32.o"
        ${WASM2NATIVE_CMD} --format=object --heap-size=16384 \
            -o ${OUT_DIR}/${file_name%.*}_mem32.o ${OUT_DIR}/${file_name%.*}_mem32.wasm

        echo "Compile ${file_name%.*}_mem64.wasm into ${file_name%.*}_mem64.o"
        ${WASM2NATIVE_CMD} --format=object --heap-size=16384 \
            -o ${OUT_DIR}/${file_name%.*}_mem64.o ${OUT_DIR}/${file_name%.*}_mem64.wasm

        echo "Compile ${file_name%.*}_mem64.wasm into ${file_name%.*}_mem64_nosandbox.o"
        ${WASM2NATIVE_CMD} --format=object --no-sandbox-mode \
            -o ${OUT_DIR}/${file_name%.*}_mem64_nosandbox.o ${OUT_DIR}/${file_name%.*}_mem64.wasm

        echo "Compile ${file_name%.*}_mem64.wasm into ${file_name%.*}_mem64_nosandbox.ll"
        ${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode \
            -o ${OUT_DIR}/${file_name%.*}_mem64_nosandbox.ll ${OUT_DIR}/${file_name%.*}_mem64.wasm

        echo "Generate ${file_name%.*}_mem32 binary"
        gcc -O3 -o ${OUT_DIR}/${file_name%.*}_mem32 ${OUT_DIR}/${file_name%.*}_mem32.o \
            -L ${WORK_DIR}/build -lvmlib

        echo "Generate ${file_name%.*}_mem64 binary"
        gcc -O3 -o ${OUT_DIR}/${file_name%.*}_mem64 ${OUT_DIR}/${file_name%.*}_mem64.o \
            -L ${WORK_DIR}/build -lvmlib

        echo "Generate ${file_name%.*}_mem64_nosandbox binary"
        gcc -O3 -o ${OUT_DIR}/${file_name%.*}_mem64_nosandbox ${OUT_DIR}/${file_name%.*}_mem64_nosandbox.o \
            -L ${WORK_DIR}/build -lnosandbox -lm
    done
done

cd ..

echo "Done"
