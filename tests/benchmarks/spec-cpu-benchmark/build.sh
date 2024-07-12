#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if [[ -z ${WASM2NATIVE_DIR} ]]; then
    WASM2NATIVE_DIR=$PWD/../../..
fi

WORK_DIR=$PWD
OUT_DIR=$PWD/out
SPECCPU_DIR=$PWD/src
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

rm -fr src && tar -zxf src.tar.gz

speccpu_srcs="spec.c spec_xz.c pxz.c common/tuklib_physmem.c liblzma/common/common.c \
              liblzma/common/block_util.c liblzma/common/easy_preset.c \
              liblzma/common/filter_common.c liblzma/common/hardware_physmem.c \
              liblzma/common/index.c liblzma/common/stream_flags_common.c \
              liblzma/common/vli_size.c liblzma/common/alone_encoder.c \
              liblzma/common/block_buffer_encoder.c liblzma/common/block_encoder.c \
              liblzma/common/block_header_encoder.c liblzma/common/easy_buffer_encoder.c \
              liblzma/common/easy_encoder.c liblzma/common/easy_encoder_memusage.c \
              liblzma/common/filter_buffer_encoder.c liblzma/common/filter_encoder.c \
              liblzma/common/filter_flags_encoder.c liblzma/common/index_encoder.c \
              liblzma/common/stream_buffer_encoder.c liblzma/common/stream_encoder.c \
              liblzma/common/stream_flags_encoder.c liblzma/common/vli_encoder.c \
              liblzma/common/alone_decoder.c liblzma/common/auto_decoder.c \
              liblzma/common/block_buffer_decoder.c liblzma/common/block_decoder.c \
              liblzma/common/block_header_decoder.c liblzma/common/easy_decoder_memusage.c \
              liblzma/common/filter_buffer_decoder.c liblzma/common/filter_decoder.c \
              liblzma/common/filter_flags_decoder.c liblzma/common/index_decoder.c \
              liblzma/common/index_hash.c liblzma/common/stream_buffer_decoder.c \
              liblzma/common/stream_decoder.c liblzma/common/stream_flags_decoder.c \
              liblzma/common/vli_decoder.c liblzma/check/check.c liblzma/check/crc32_table.c \
              liblzma/check/crc32_fast.c liblzma/check/crc64_table.c liblzma/check/crc64_fast.c \
              liblzma/check/sha256.c liblzma/lz/lz_encoder.c liblzma/lz/lz_encoder_mf.c \
              liblzma/lz/lz_decoder.c liblzma/lzma/lzma_encoder.c \
              liblzma/lzma/lzma_encoder_presets.c liblzma/lzma/lzma_encoder_optimum_fast.c \
              liblzma/lzma/lzma_encoder_optimum_normal.c liblzma/lzma/fastpos_table.c \
              liblzma/lzma/lzma_decoder.c liblzma/lzma/lzma2_encoder.c \
              liblzma/lzma/lzma2_decoder.c liblzma/rangecoder/price_table.c \
              liblzma/delta/delta_common.c liblzma/delta/delta_encoder.c \
              liblzma/delta/delta_decoder.c liblzma/simple/simple_coder.c \
              liblzma/simple/simple_encoder.c liblzma/simple/simple_decoder.c \
              liblzma/simple/x86.c liblzma/simple/powerpc.c liblzma/simple/ia64.c \
              liblzma/simple/arm.c liblzma/simple/armthumb.c liblzma/simple/sparc.c xz/args.c \
              xz/coder.c xz/file_io.c xz/hardware.c xz/list.c xz/main.c xz/message.c \
              xz/options.c xz/signals.c xz/util.c common/tuklib_open_stdxxx.c \
              common/tuklib_progname.c common/tuklib_exit.c common/tuklib_cpucores.c \
              common/tuklib_mbstr_width.c common/tuklib_mbstr_fw.c spec_mem_io/spec_mem_io.c \
              sha-2/sha512.c"

C_FLAGS="-std=c99 -DSPEC -DSPEC_CPU -DNDEBUG -DSPEC_AUTO_BYTEORDER=0x12345678 \
    -DHAVE_CONFIG_H=1 -DSPEC_MEM_IO -DSPEC_XZ -DSPEC_AUTO_SUPPRESS_OPENMP -I. \
    -Ispec_mem_io -Isha-2 -Icommon -Iliblzma/api -Iliblzma/lzma -Iliblzma/common \
    -Iliblzma/check -Iliblzma/simple -Iliblzma/delta -Iliblzma/lz -Iliblzma/rangecoder \
    -fno-strict-aliasing -DSPEC_LP64 -fno-strict-aliasing"

cd ${SPECCPU_DIR}

echo "Build speccpu with gcc .."
gcc -O3 ${C_FLAGS} -z muldefs -o ${OUT_DIR}/speccpu ${speccpu_srcs}

echo "Build speccpu_mem64.wasm with Android clang toolchain"
${ANDROID_CLANG_TOOLCHAIN}/bin/clang \
    --target=wasm64-unknown-unknown -O3 ${C_FLAGS} \
    -Wl,--allow-undefined \
    -nostdlib -Wl,--emit-relocs -Wl,--no-entry \
    -Wl,--export=__main_argc_argv \
    -z stack-size=32768 -Wl,--export=__data_end -Wl,--export=__heap_base \
    -fvisibility=default -Wno-implicit-function-declaration \
    --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
    -I${WASM_NDK_ROOT}/include/wasm64 \
    -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
    -o ${OUT_DIR}/speccpu_mem64.wasm ${speccpu_srcs}

echo "Disassemble speccpu_mem64.wasm to speccpu_mem64.wast with wasm2wat"
${WABT_HOME}/bin/wasm2wat --enable-memory64 \
    -o ${OUT_DIR}/speccpu_mem64.wast ${OUT_DIR}/speccpu_mem64.wasm

echo "Dump speccpu_mem64.wasm to speccpu_mem64.dump"
${WABT_HOME}/bin/wasm-objdump \
    -s -x -r -d ${OUT_DIR}/speccpu_mem64.wasm > ${OUT_DIR}/speccpu_mem64.dump

echo "Compile speccpu_mem64.wasm into speccpu_mem64_nosandbox.o"
${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o ${OUT_DIR}/speccpu_mem64_nosandbox.o ${OUT_DIR}/speccpu_mem64.wasm

echo "Compile speccpu_mem64.wasm into speccpu_mem64_nosandbox.ll"
${WASM2NATIVE_CMD} --format=llvmir-unopt --no-sandbox-mode -o ${OUT_DIR}/speccpu_mem64_nosandbox.ll ${OUT_DIR}/speccpu_mem64.wasm

echo "Generate speccpu_mem64 binary"
gcc -O3 -o ${OUT_DIR}/speccpu_mem64_nosandbox ${OUT_DIR}/speccpu_mem64_nosandbox.o -L ${WORK_DIR}/build -lnosandbox -lm

