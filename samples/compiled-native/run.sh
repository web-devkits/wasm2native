#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

OUT_DIR=$PWD/out

cd $OUT_DIR

echo ""
echo "Run test of gcc native .."
./test

echo ""
echo "Run sandbox main with sandbox static library(wasm memory64 inside)" 
./sandbox_with_staticlib 

echo ""
echo "Run nosandbox main with nosandbox static library(wasm memory64 inside)" 
./nosandbox_with_staticlib

echo ""
echo "Run sandbox main with sandbox shared library(wasm memory64 inside)" 
LD_LIBRARY_PATH=$PWD ./sandbox_with_sharedlib

echo ""
echo "Run nosandbox main with nosandbox shared library(wasm memory64 inside)" 
LD_LIBRARY_PATH=$PWD ./nosandbox_with_sharedlib




