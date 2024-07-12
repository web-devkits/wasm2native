#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

echo "Run coremark of gcc native .."
./out/coremark.exe

echo "Run coremark of wasm memory32 .."
./out/coremark_mem32

echo "Run coremark of wasm memory64 .."
./out/coremark_mem64

echo "Run coremark of wasm memory64 nosandbox .."
./out/coremark_mem64_nosandbox
