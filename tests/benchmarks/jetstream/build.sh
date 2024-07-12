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

if [ ! -d perf-automation ]; then
    git clone https://github.com/mozilla/perf-automation.git
    echo "Patch source files .."
    cd perf-automation
    patch -p1 -N < ../jetstream.patch
fi

cd ${JETSTREAM_DIR}

# Build quicksort

echo "Build quicksort with gcc .."
gcc -O3 ${NATIVE_SIMD_FLAGS} -o ${OUT_DIR}/quicksort quicksort.c

echo "Build quicksort_mem32.wasm with wasi-sdk .."
/opt/wasi-sdk/bin/clang -O3 ${WASM_SIMD_FLAGS} \
    -o ${OUT_DIR}/quicksort_mem32.wasm quicksort.c \
    -nostdlib -Wl,--no-entry -Wl,--export=__main_void \
    -Wl,--export=__heap_base -Wl,--export=__data_end \
    -Wl,--allow-undefined

echo "Build quicksort_mem64.wasm with Android clang toolchain"
${ANDROID_CLANG_TOOLCHAIN}/bin/clang \
    --target=wasm64-unknown-unknown -O3 ${WASM_SIMD_FLAGS} \
    -Wl,--allow-undefined \
    -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
    -Wl,--export=__main_void \
    -fvisibility=default -Wno-implicit-function-declaration \
    --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
    -I${WASM_NDK_ROOT}/include/wasm64 \
    -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
    -o ${OUT_DIR}/quicksort_mem64.wasm quicksort.c

echo "Disassemble quicksort_mem64.wasm to quicksort_mem64.wast with wasm2wat"
${WABT_HOME}/bin/wasm2wat --enable-memory64 \
    -o ${OUT_DIR}/quicksort_mem64.wast ${OUT_DIR}/quicksort_mem64.wasm

${WABT_HOME}/bin/wasm-objdump \
    -s -x -r -d ${OUT_DIR}/quicksort_mem64.wasm > ${OUT_DIR}/quicksort_mem64.dump

echo "Compile quicksort_mem32.wasm into quicksort_mem32.o"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o ${OUT_DIR}/quicksort_mem32.o ${OUT_DIR}/quicksort_mem32.wasm

echo "Compile quicksort_mem64.wasm into quicksort_mem64.o"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o ${OUT_DIR}/quicksort_mem64.o ${OUT_DIR}/quicksort_mem64.wasm

echo "Compile quicksort_mem64.wasm into quicksort_mem64_nosandbox.o"
${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o ${OUT_DIR}/quicksort_mem64_nosandbox.o ${OUT_DIR}/quicksort_mem64.wasm

echo "Compile quicksort_mem64.wasm into quicksort_mem64_nosandbox.ll"
${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o ${OUT_DIR}/quicksort_mem64_nosandbox.ll ${OUT_DIR}/quicksort_mem64.wasm

echo "Generate quicksort_mem32 binary"
gcc -O3 -o ${OUT_DIR}/quicksort_mem32 ${OUT_DIR}/quicksort_mem32.o -L ${WORK_DIR}/build -lvmlib

echo "Generate quicksort_mem64 binary"
gcc -O3 -o ${OUT_DIR}/quicksort_mem64 ${OUT_DIR}/quicksort_mem64.o -L ${WORK_DIR}/build -lvmlib

echo "Generate quicksort_mem64 binary"
gcc -O3 -o ${OUT_DIR}/quicksort_mem64_nosandbox ${OUT_DIR}/quicksort_mem64_nosandbox.o -L ${WORK_DIR}/build -lnosandbox -lm

# Build float-mm

cd ${JETSTREAM_DIR}/../simple

echo "Build float-mm with gcc .."
gcc -O3 ${NATIVE_SIMD_FLAGS} -o ${OUT_DIR}/float-mm float-mm.c

echo "Build float-mm_mem32.wasm with wasi-sdk .."
/opt/wasi-sdk/bin/clang -O3 ${WASM_SIMD_FLAGS} \
    -o ${OUT_DIR}/float-mm_mem32.wasm float-mm.c \
    -nostdlib -Wl,--no-entry -Wl,--export=__main_void \
    -Wl,--export=__heap_base -Wl,--export=__data_end \
    -Wl,--allow-undefined

echo "Build float-mm_mem64.wasm with Android clang toolchain"
${ANDROID_CLANG_TOOLCHAIN}/bin/clang \
    --target=wasm64-unknown-unknown -O3 ${WASM_SIMD_FLAGS} \
    -Wl,--allow-undefined \
    -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
    -Wl,--export=__main_void \
    -fvisibility=default -Wno-implicit-function-declaration \
    --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
    -I${WASM_NDK_ROOT}/include/wasm64 \
    -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
    -o ${OUT_DIR}/float-mm_mem64.wasm float-mm.c

${WABT_HOME}/bin/wasm2wat --enable-memory64 \
    -o ${OUT_DIR}/float-mm_mem64.wast ${OUT_DIR}/float-mm_mem64.wasm

${WABT_HOME}/bin/wasm-objdump \
    -s -x -r -d ${OUT_DIR}/float-mm_mem64.wasm > ${OUT_DIR}/float-mm_mem64.dump

echo "Compile float-mm_mem32.wasm into float-mm_mem32.o"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o ${OUT_DIR}/float-mm_mem32.o ${OUT_DIR}/float-mm_mem32.wasm

echo "Compile float-mm_mem64.wasm into float-mm_mem64.o"
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o ${OUT_DIR}/float-mm_mem64.o ${OUT_DIR}/float-mm_mem64.wasm

echo "Compile float-mm_mem64.wasm into float-mm_mem64_nosandbox.o"
${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o ${OUT_DIR}/float-mm_mem64_nosandbox.o ${OUT_DIR}/float-mm_mem64.wasm

echo "Compile float-mm_mem64.wasm into float-mm_mem64_nosandbox.ll"
${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o ${OUT_DIR}/float-mm_mem64_nosandbox.ll ${OUT_DIR}/float-mm_mem64.wasm

echo "Generate float-mm_mem32 binary"
gcc -O3 -o ${OUT_DIR}/float-mm_mem32 ${OUT_DIR}/float-mm_mem32.o -L ${WORK_DIR}/build -lvmlib

echo "Generate float-mm_mem64 binary"
gcc -O3 -o ${OUT_DIR}/float-mm_mem64 ${OUT_DIR}/float-mm_mem64.o -L ${WORK_DIR}/build -lvmlib

echo "Generate float-mm_mem64_nosandbox binary"
gcc -O3 -o ${OUT_DIR}/float-mm_mem64_nosandbox ${OUT_DIR}/float-mm_mem64_nosandbox.o -L ${WORK_DIR}/build -lnosandbox -lm

# Build HashSet

cd ${JETSTREAM_DIR}/

echo "Build HashSet with g++ .."
g++ -O3 ${NATIVE_SIMD_FLAGS} -o ${OUT_DIR}/HashSet HashSet.cpp

echo "Build HashSet_mem64.wasm with Android clang toolchain"
${ANDROID_CLANG_TOOLCHAIN}/bin/clang++ \
    --target=wasm64-unknown-unknown -O3 ${WASM_SIMD_FLAGS} \
    -Wl,--allow-undefined \
    -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
    -Wl,--export=__main_argc_argv \
    -fvisibility=default -Wno-implicit-function-declaration \
    --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
    -I${WASM_NDK_ROOT}/include/wasm64 \
    -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include/c++/v1 \
    -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
    -D_LIBCPP_HAS_NO_THREADS \
    -o ${OUT_DIR}/HashSet_mem64.wasm HashSet.cpp

${WABT_HOME}/bin/wasm2wat --enable-memory64 \
    -o ${OUT_DIR}/HashSet_mem64.wast ${OUT_DIR}/HashSet_mem64.wasm

${WABT_HOME}/bin/wasm-objdump \
    -s -x -r -d ${OUT_DIR}/HashSet_mem64.wasm > ${OUT_DIR}/HashSet_mem64.dump

echo "Compile HashSet_mem64.wasm into HashSet_mem64_nosandbox.o"
${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o ${OUT_DIR}/HashSet_mem64_nosandbox.o ${OUT_DIR}/HashSet_mem64.wasm

echo "Compile HashSet_mem64.wasm into HashSet_mem64_nosandbox.ll"
${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o ${OUT_DIR}/HashSet_mem64_nosandbox.ll ${OUT_DIR}/HashSet_mem64.wasm

echo "Generate HashSet_mem64_nosandbox binary"
g++ -O3 -o ${OUT_DIR}/HashSet_mem64_nosandbox ${OUT_DIR}/HashSet_mem64_nosandbox.o -L ${WORK_DIR}/build -lnosandbox -lm

# Build tsf

cd ${JETSTREAM_DIR}/TSF

tsf_srcs="tsf_asprintf.c tsf_buffer.c tsf_error.c tsf_reflect.c tsf_st.c \
          tsf_type.c tsf_io.c tsf_native.c tsf_generator.c tsf_st_typetable.c \
          tsf_parser.c tsf_buf_writer.c tsf_buf_reader.c tsf_primitive.c \
          tsf_type_table.c tsf_copier.c tsf_destructor.c tsf_gpc_code_gen.c \
          gpc_code_gen_util.c gpc_threaded.c gpc_intable.c gpc_instruction.c \
          gpc_program.c gpc_proto.c gpc_stack_height.c tsf_serial_in_man.c \
          tsf_serial_out_man.c tsf_type_in_map.c tsf_type_out_map.c \
          tsf_stream_file_input.c tsf_stream_file_output.c tsf_sort.c \
          tsf_version.c tsf_named_type.c tsf_io_utils.c tsf_zip_attr.c \
          tsf_zip_reader.c tsf_zip_writer.c tsf_zip_abstract.c tsf_limits.c \
          tsf_ra_type_man.c tsf_adaptive_reader.c tsf_sha1.c tsf_sha1_writer.c \
          tsf_fsdb.c tsf_fsdb_protocol.c tsf_define_helpers.c tsf_ir.c \
          tsf_ir_different.c tsf_ir_speed.c"

echo "Build tsf with gcc .."
gcc -O3 ${NATIVE_SIMD_FLAGS} -o ${OUT_DIR}/tsf ${tsf_srcs} -lm \
    -DTSF_BUILD_SYSTEM=1 \
    -Wno-deprecated-declarations

echo "Build tsf_mem64.wasm with Android clang toolchain"
${ANDROID_CLANG_TOOLCHAIN}/bin/clang \
    --target=wasm64-unknown-unknown -O3 ${WASM_SIMD_FLAGS} -DTSF_BUILD_SYSTEM=1 \
    -Wno-deprecated-declarations -Wno-deprecated-non-prototype \
    -Wl,--allow-undefined \
    -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
    -Wl,--export=__main_argc_argv \
    -fvisibility=default -Wno-implicit-function-declaration \
    --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
    -I${WASM_NDK_ROOT}/include/wasm64 \
    -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
    -o ${OUT_DIR}/tsf_mem64.wasm ${tsf_srcs}

echo "Dump tsf_mem64.wasm into tsf_mem64.dump"
${WABT_HOME}/bin/wasm-objdump \
    -s -x -r -d ${OUT_DIR}/tsf_mem64.wasm > ${OUT_DIR}/tsf_mem64.dump

echo "Compile tsf_mem64.wasm into tsf_mem64_nosandbox.o"
${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o ${OUT_DIR}/tsf_mem64_nosandbox.o ${OUT_DIR}/tsf_mem64.wasm

echo "Compile tsf_mem64.wasm into tsf_mem64_nosandbox.ll"
${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o ${OUT_DIR}/tsf_mem64_nosandbox.ll ${OUT_DIR}/tsf_mem64.wasm

echo "Generate tsf_mem64_nosandbox binary"
gcc -O3 -o ${OUT_DIR}/tsf_mem64_nosandbox ${OUT_DIR}/tsf_mem64_nosandbox.o -L ${WORK_DIR}/build -lnosandbox -lm
