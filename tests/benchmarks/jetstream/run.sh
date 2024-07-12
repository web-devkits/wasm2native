#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

echo "============> run quicksort of gcc native"
time ./out/quicksort > /dev/null 2>&1

echo "============> run quicksort of wasm memory32"
time ./out/quicksort_mem32 > /dev/null 2>&1

echo "============> run quicksort of wasm memory64"
time ./out/quicksort_mem64 > /dev/null 2>&1

echo "============> run quicksort of wasm memory64 nosandbox"
time ./out/quicksort_mem64_nosandbox > /dev/null 2>&1

echo "============> run float-mm of gcc native"
time ./out/float-mm > /dev/null 2>&1

echo "============> run float-mm of wasm memory32"
time ./out/float-mm_mem32 > /dev/null 2>&1

echo "============> run float-mm of wasm memory64"
time ./out/float-mm_mem64 > /dev/null 2>&1

echo "============> run float-mm of wasm memory64 nosandbox"
time ./out/float-mm_mem64_nosandbox > /dev/null 2>&1

echo "============> run HashSet of gcc native"
time ./out/HashSet > /dev/null 2>&1

echo "============> run HashSet of wasm memory64 nosandbox"
time ./out/HashSet_mem64_nosandbox > /dev/null 2>&1

echo "============> run tsf of gcc native"
time ./out/tsf > /dev/null 2>&1

echo "============> run tsf of wasm memory64 nosandbox"
time ./out/tsf_mem64_nosandbox > /dev/null 2>&1
