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

### Example Usage

The sample C/C++ source code and shell scripts to automatically compile/run it are in this [directory](../samples/hello-world/). Here is the step-by-step guide on how to build different kind of wasm file from C/C++ source code:

#### Compile C/C++ source code to wasm32

To compile C/C++ source code to wasm32, use the `wasi-sdk` toolchain, here is an example command:

```bash
/opt/wasi-sdk/bin/clang -O3 -I. -msimd128 \
        -nostdlib -Wl,--no-entry -Wl,--export=__main_void \
        -Wl,--export=add \
        -Wl,--allow-undefined \
        -Wno-format \
        -o test_mem32.wasm \
        main.c
```

> PS: change the export from `__main_void` to `__main_argc_argv` if the main function take command line arguments.

After that, you can use the `wasm2native` compiler to compile the wasm32 file to a native object file in **sandbox mode**(no-sandbox mode is not supported yet for wasm32). Details can refer to [compile wasm applications to native binary](./compile_wasm_app_to_native.md).

#### Compile C/C++ source code to wasm64

To compile C/C++ source code to wasm64, use the `wasm_ndk of AndroidWasm` toolchain, here is an example command:

```bash
${ANDROID_CLANG_TOOLCHAIN}/bin/clang -O3 -I. -msimd128 \
        --target=wasm64-unknown-unknown -Wno-format \
        -nostdlib -Wl,--emit-relocs -Wl,--no-entry -Wl,--export=add \
        -Wl,--export=__main_void -Wl,--allow-undefined \
        -fvisibility=default -Wno-implicit-function-declaration \
        --sysroot=${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot \
        -I${WASM_NDK_ROOT}/include/wasm64 \
        -I${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/usr/include \
        -o test_mem64.wasm \
        main.c
```

> Note: by adding the compile flag `-Wl,--emit-relocs`, the wasm64 file will contain relocation information, which is required for the `wasm2native` compiler to compile the wasm64 file to a native object file in no-sandbox mode(the wasm64 file will also be compatible with sandbox mode). But if you only want to compile the wasm64 file to a native object file in sandbox mode, you can remove this flag.

After that, you can use the `wasm2native` compiler to compile the wasm64 file to a native object file in **both sandbox mode and no-sandbox mode**. Details can refer to [compile wasm applications to native binary](./compile_wasm_app_to_native.md).
