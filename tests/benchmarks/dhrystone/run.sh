#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

echo "============> run dhrystone of gcc native"
./out/dhrystone

echo "============> run dhrystone of wasm memory32"
./out/dhrystone_mem32

echo "============> run dhrystone of wasm memory64"
./out/dhrystone_mem64

echo "============> run dhrystone of wasm memory64 nosandbox"
./out/dhrystone_mem64_nosandbox
