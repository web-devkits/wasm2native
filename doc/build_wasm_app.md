### Pre-requisites

Refer to [wasm_ndk of AndroidWasm] to install the toolchains. Note that when building the [Android
Clang/LLVM Toolchain], apply the table64 patch to `llvm-toolchain/toolchain/llvm-project` ([#92042]) before the step `python
toolchain/llvm_android/build.py`.

[wasm_ndk of AndroidWasm]: https://github.com/AndroidWasm/wasm_ndk?tab=readme-ov-file#pre-requisites
[Android Clang/LLVM Toolchain]: https://android.googlesource.com/toolchain/llvm_android/+/master/README.md#android-clang_llvm-toolchain
[#92042]: https://github.com/llvm/llvm-project/pull/92042

Then, ensure that the following environment variables are set:

```bash
export ANDROID_NDK_HOME=<path/to/android-ndk-r26d>
export ANDROID_CLANG_TOOLCHAIN=<path/to/linux-x86/clang-dev>
export WABT_HOME=<path/to/wabt>
export WASM_NDK_ROOT=<path/to/wasm_ndk>
```

Install [wasi-sdk](https://github.com/WebAssembly/wasi-sdk/tags).

### Compile C/C++ source code to wasm32 sandbox mode


### Compile C/C++ source code to wasm64 sandbox mode


### Compile C/C++ source code to wasm64 non-sandbox mode
