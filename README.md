<div align="center">
  <h1><code>wasm2native</code></h1>
  <p>
    <strong>Toolchain for compiling WebAssembly to native</strong>
  </p>
</div>

### Overview
wasm2native allows to compile the wasm file into a native object file, which can be linked with an auxiliary library into a native binary, e.g. executable, shared library or static library. It removes the AOT runtime dependency and provides two modes: one is sandbox mode, in which the wasm sandbox is kept and the wasm address space and native address space are different. The other is [no-sandbox mode](](https://github.com/AndroidWasm/wabt/tree/main/wasm2c#no-sandbox-mode-experimental)), the wasm sandbox is discarded but it allows sharing pointers (both memory pointers and function pointers) between wasm and native, the address space is the same in wasm and native.

### Pre-requisites

Refer to [wasm_ndk of AnroidWasm](https://github.com/AndroidWasm/wasm_ndk?tab=readme-ov-file#pre-requisites) to install the toolchainls. Note that when building the [Android Clang/LLVM Toolchain](https://android.googlesource.com/toolchain/llvm_android/+/master/README.md#android-clang_llvm-toolchain), apply the table64 patch to `llvm-toolchain/toolchain/llvm-project`: [[WebAssembly] Use 64-bit table when targeting wasm64](https://github.com/llvm/llvm-project/pull/92042) before the step `python toolchain/llvm_android/build.py`.

Finally ensure that the below environment variables are set:
```bash
export ANDROID_NDK_HOME=<path/to/android-ndk-r26d>
export ANDROID_CLANG_TOOLCHAIN=<path/to/linux-x86/clang-dev>
export WABT_HOME=<path/to/wabt>
export WASM_NDK_ROOT=<path/to/wasm_ndk>
```

And install [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/tags).

### Build wasm2native compiler

Refer to [wasm2compiler/README.md](./wasm2native-compiler/README.md) for how to build the wasm2native compiler.

### Run benchmarks

Change to each folder under [./tests/benchmarks](tests/benchmarks), then run `build.sh` to build the benchmarks and `run.sh` to run the benchmarks.
