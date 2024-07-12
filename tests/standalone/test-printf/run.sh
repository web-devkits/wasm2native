#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

OUT_DIR=$PWD/out

cd $OUT_DIR

echo ""
echo "Run test of gcc native .."
time ./test > gcc.log

echo ""
echo "Run test of wasm memory32 .."
time ./test_mem32 > mem32.log

echo ""
echo "Run test of wasm memory64 .."
time ./test_mem64 > mem64.log

echo ""
echo "Run test of wasm memory64 nosandbox .."
time ./test_mem64_nosandbox > mem64_nosandbox.log
