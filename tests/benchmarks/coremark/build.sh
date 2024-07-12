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
if [ ! -d coremark ]; then
    git clone https://github.com/eembc/coremark.git
fi

cd ${WORK_DIR}/coremark

echo "Build coremark.exe with gcc .."
gcc -O3 -Iposix -I. -DFLAGS_STR=\""-O3 -DPERFORMANCE_RUN=1  -lrt"\" \
        -DITERATIONS=400000 -DSEED_METHOD=SEED_VOLATILE -DPERFORMANCE_RUN=1 \
        core_list_join.c core_main.c core_matrix.c core_state.c \
        core_util.c posix/core_portme.c \
        -o ../out/coremark.exe -lrt

echo "Build coremark_mem32.wasm with wasi-sdk .."
/opt/wasi-sdk/bin/clang -O3 -Iposix -I. -DFLAGS_STR=\""-O3 -DPERFORMANCE_RUN=1"\" \
        -nostdlib -Wl,--no-entry -Wl,--export=__main_argc_argv \
        -DITERATIONS=400000 -DSEED_METHOD=SEED_VOLATILE -DPERFORMANCE_RUN=1 \
        -Wl,--allow-undefined \
        core_list_join.c core_main.c core_matrix.c core_state.c \
        core_util.c posix/core_portme.c \
        -o ../out/coremark_mem32.wasm

echo "Build coremark_mem64.wasm with Android clang toolchain .."
${ANDROID_CLANG_TOOLCHAIN}/bin/clang \
        -O3 -Iposix -I. -DFLAGS_STR=\""-O3 -DPERFORMANCE_RUN=1"\" \
        -DITERATIONS=400000 -DSEED_METHOD=SEED_VOLATILE -DPERFORMANCE_RUN=1 \
        --target=wasm64-unknown-unknown -msimd128 \
        -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
        -Wl,--export=__main_argc_argv -Wl,--allow-undefined \
        -Wl,--export=__heap_base -Wl,--export=__data_end \
        -z stack-size=32768 \
        -fvisibility=default -Wno-implicit-function-declaration \
        --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
        -I${WASM_NDK_ROOT}/include/wasm64 \
        -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
        -o ../out/coremark_mem64.wasm \
        core_list_join.c core_main.c core_matrix.c core_state.c \
        core_util.c posix/core_portme.c

${WABT_HOME}/bin/wasm2wat \
        --enable-memory64 -o ../out/coremark_mem64.wast ../out/coremark_mem64.wasm

${WABT_HOME}/bin/wasm-objdump \
        -s -x -r -d ../out/coremark_mem64.wasm > ../out/coremark_mem64.dump

cd ${WORK_DIR}/out

echo "Compile coremark_mem32.wasm into coremark_mem32.o"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o coremark_mem32.o coremark_mem32.wasm

echo "Compile coremark_mem64.wasm into coremark_mem64.o"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o coremark_mem64.o coremark_mem64.wasm

echo "Compile coremark_mem64.wasm into coremark_mem64_nosandbox.o"
${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o coremark_mem64_nosandbox.o coremark_mem64.wasm

echo "Compile coremark_mem64.wasm into coremark_mem64_nosandbox.ll"
${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o coremark_mem64_nosandbox.ll coremark_mem64.wasm

echo "Generate coremark_mem32 binary"
gcc -O3 -o coremark_mem32 coremark_mem32.o -L ../build -lvmlib

echo "Generate coremark_mem64 binary"
gcc -O3 -o coremark_mem64 coremark_mem64.o -L ../build -lvmlib

echo "Generate coremark_mem64_nosandbox binary"
gcc -O3 -o coremark_mem64_nosandbox coremark_mem64_nosandbox.o -L ../build -lnosandbox -lm

echo "Done"
