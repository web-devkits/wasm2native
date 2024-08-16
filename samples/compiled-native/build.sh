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
rm -fr build && mkdir -p build

echo "Build nomain.wasm with Android clang toolchain .."
${ANDROID_CLANG_TOOLCHAIN}/bin/clang -O3 -I. -msimd128 \
        --target=wasm64-unknown-unknown -Wno-format \
        -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
        -Wl,--export=add -Wl,--allow-undefined \
        -fvisibility=default -Wno-implicit-function-declaration \
        --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
        -I${WASM_NDK_ROOT}/include/wasm64 \
        -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
        -o out/nomain.wasm \
        wasm-app/nomain.c

echo "Build main.wasm with Android clang toolchain .."
${ANDROID_CLANG_TOOLCHAIN}/bin/clang -O3 -I. -msimd128 \
        --target=wasm64-unknown-unknown -Wno-format \
        -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
        -Wl,--export=__main_void -Wl,--allow-undefined \
        -fvisibility=default -Wno-implicit-function-declaration \
        --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
        -I${WASM_NDK_ROOT}/include/wasm64 \
        -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
        -o out/main.wasm \
        wasm-app/main.c

cd ${WORK_DIR}/out

echo "Compile main.wasm into main.o"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o main.o main.wasm

echo "Compile nomain.wasm into nomain_nosandbox.o"
${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o nomain_nosandbox.o nomain.wasm

# static libary example usage
cd ${WORK_DIR}
echo "Build wasm2native-vmlib(-DW2N_BUILD_WASM_APPLICATION=1)" 
cd build
cmake ${WASM2NATIVE_DIR}/wasm2native-vmlib -DW2N_BUILD_WASM_APPLICATION=1
make -j

cd ${WORK_DIR}/out
mkdir extracted_objs
ar x ../build/libvmlib.a --output=extracted_objs

echo "Generate sandbox static library"
ar rcs libstaticsandbox.a main.o $(ls extracted_objs/*.o | grep -vE 'main\.c\.o|wasm_application\.c\.o')

echo "Generate sandbox_with_staticlib use sandbox static library, main.c and wasm_application.c"
gcc -O3 -o sandbox_with_staticlib \
  extracted_objs/main.c.o extracted_objs/wasm_application.c.o \
  -L. -lstaticsandbox

echo "Generate nosandbox static library"
ar x ../build/libnosandbox.a --output=extracted_objs
ar rcs libstaticnosandbox.a nomain_nosandbox.o extracted_objs/libc64_nosandbox_wrapper.c.o

echo "Generate nosandbox_with_staticlib use nosandbox static library and nosandbox_main.c"
gcc -O3 -o nosandbox_with_staticlib ../src/nosandbox_main.c -L. -lstaticnosandbox

# shared library example usage
cd ${WORK_DIR}
echo "Build wasm2native-vmlib(-DW2N_BUILD_WASM_APPLICATION=0)" 
cd build
cmake ${WASM2NATIVE_DIR}/wasm2native-vmlib -DW2N_BUILD_WASM_APPLICATION=0
make -j

cd ${WORK_DIR}/out
echo "Generate sandbox shared library"
gcc -fPIC -shared -o libsharedsandbox.so main.o -L ../build/ -lvmlib

echo "Generate sandbox_with_sharedlib use sandbox shared library, main.c and wasm_application.c"
gcc -o sandbox_with_sharedlib \
  extracted_objs/main.c.o extracted_objs/wasm_application.c.o \
  -L. -lsharedsandbox

echo "Generate nosandbox shared library"
gcc -fPIC -shared -o libsharednosandbox.so nomain_nosandbox.o -L ../build -lnosandbox -lm

echo "Generate nosandbox_with_sharedlib use nosandbox shared library and nosandbox_main.c"
gcc -o nosandbox_with_sharedlib ../src/nosandbox_main.c -L. -lsharednosandbox


echo "Done"
