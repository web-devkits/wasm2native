#!/bin/bash

# Copyright (C) 2019 Intel Corporation.  All rights reserved.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

OUT_DIR=$PWD/out

cd $OUT_DIR

echo ""
echo "Run binary-trees of gcc native .."
time ./binary-trees 18

echo ""
echo "Run binary-trees of wasm memory32 .."
time ./binary-trees_mem32 18

echo ""
echo "Run binary-trees of wasm memory64 .."
time ./binary-trees_mem64 18

echo ""
echo "Run binary-trees of wasm memory64 nosandbox .."
time ./binary-trees_mem64_nosandbox 18

echo ""
echo "Run fannkuch-redux of gcc native .."
time ./fannkuch-redux 11

echo ""
echo "Run fannkuch-redux of wasm memory32 .."
time ./fannkuch-redux_mem32 11

echo ""
echo "Run fannkuch-redux of wasm memory64 .."
time ./fannkuch-redux_mem64 11

echo ""
echo "Run fannkuch-redux of wasm memory64 nosandbox .."
time ./fannkuch-redux_mem64_nosandbox 11

echo ""
echo "Run fasta of gcc native .."
time ./fasta 20000000 > image.ppm 2>&1

echo ""
echo "Run fasta of wasm memory32 .."
time ./fasta_mem32 20000000 > image.ppm 2>&1

echo ""
echo "Run fasta of wasm memory64 .."
time ./fasta_mem64 20000000 > image.ppm 2>&1

echo ""
echo "Run fasta of wasm memory64 nosandbox .."
time ./fasta_mem64_nosandbox 20000000 > image.ppm 2>&1

echo ""
echo "Run mandelbrot of gcc native .."
time ./mandelbrot 10000 > image.ppm 2>&1

echo ""
echo "Run mandelbrot of wasm memory32 .."
time ./mandelbrot_mem32 10000 > image.ppm 2>&1

echo ""
echo "Run mandelbrot of wasm memory64 .."
time ./mandelbrot_mem64 10000 > image.ppm 2>&1

echo ""
echo "Run mandelbrot of wasm memory64 nosandbox .."
time ./mandelbrot_mem64_nosandbox 10000 > image.ppm 2>&1

echo ""
echo "Run mandelbrot-simd of gcc native .."
time ./mandelbrot-simd 10000 > image.ppm 2>&1

echo ""
echo "Run mandelbrot-simd of wasm memory32 .."
time ./mandelbrot-simd_mem32 10000 > image.ppm 2>&1

echo ""
echo "Run mandelbrot-simd of wasm memory64 .."
time ./mandelbrot-simd_mem64 10000 > image.ppm 2>&1

echo ""
echo "Run mandelbrot-simd of wasm memory64 nosandbox .."
time ./mandelbrot-simd_mem64_nosandbox 10000 > image.ppm 2>&1

echo ""
echo "Run nbody of gcc native .."
time ./nbody 30000000

echo ""
echo "Run nbody of wasm memory32 .."
time ./nbody_mem32 30000000

echo ""
echo "Run nbody of wasm memory64 .."
time ./nbody_mem64 30000000

echo ""
echo "Run nbody of wasm memory64 nosandbox .."
time ./nbody_mem64_nosandbox 30000000
