#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

echo "============> run speccpu of gcc native"
time ./out/speccpu cpu2006docs.tar.xz 1 \
    055ce243071129412e9dd0b3b69a21654033a9b723d874b2015c774fac1553d9713be561ca86f74e4f16f22e664fc17a79f30caa5ad2c04fbc447549c2810fae \
    650156 -1 0


echo "============> run speccpu of wasm mem64 nosandbox"
time ./out/speccpu_mem64_nosandbox cpu2006docs.tar.xz 1 \
    055ce243071129412e9dd0b3b69a21654033a9b723d874b2015c774fac1553d9713be561ca86f74e4f16f22e664fc17a79f30caa5ad2c04fbc447549c2810fae \
    650156 -1 0
