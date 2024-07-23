#!/bin/sh

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

export PATH=/opt/coverity/bin:$PATH

cov-configure --gcc
cov-configure --template --compiler cc --comptype gcc
cov-configure --template --compiler c++ --comptype g++

WASM2NATIVE_DIR=${PWD}
COV_DIR=${PWD}/cov-out

rm -rf ${COV_DIR}
mkdir -p ${COV_DIR}

# wasm2native-compiler with default features
cd ${WASM2NATIVE_DIR}/wasm2native-compiler
rm -fr build && mkdir build && cd build
cov-build --dir ${COV_DIR} cmake .. -DCMAKE_BUILD_TYPE=Debug
cov-build --dir ${COV_DIR} make -j ${nproc}

# wasm2native-vmlib with default features
cd ${WASM2NATIVE_DIR}/wasm2native-vmlib
rm -fr build && mkdir build && cd build
cov-build --dir ${COV_DIR} cmake .. -DCMAKE_BUILD_TYPE=Debug
cov-build --dir ${COV_DIR} make -j ${nproc}

# wasm2native-vmlib with wasm application enabled
cd ${WASM2NATIVE_DIR}/wasm2native-vmlib
rm -fr build && mkdir build && cd build
cov-build --dir ${COV_DIR} cmake .. -DW2N_BUILD_WASM_APPLICATION=1 -DCMAKE_BUILD_TYPE=Debug
cov-build --dir ${COV_DIR} make -j ${nproc}

cov-analyze --dir ${COV_DIR} --concurrency --security --rule \
            --enable-constraint-fpp --enable-fnptr --enable-virtual

# Commit the analyze results to server manually
# export PATH=/opt/coverity/bin:$PATH
# cov-commit-defects --dir cov-out -stream Wasm2native \
#            --url https://coverityent.devtools.intel.com/prod2/ --user xxx
