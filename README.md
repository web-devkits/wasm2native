<div align="center">
  <h1><code>wasm2native</code></h1>
  <p>
    <strong>Toolchain for compiling WebAssembly to native</strong>
  </p>
</div>

### Overview
wasm2native allows developer to compile the WebAssembly file into a native object file, and then
link it with an auxiliary library into a native binary, e.g., an executable file, shared library or
static library. It removes the Wasm runtime dependency and provides two modes:
- sandbox mode: the Wasm sandbox is kept, and the Wasm address space and native address space are 
  kept separate
- [no-sandbox
  mode](https://github.com/AndroidWasm/wabt/tree/main/wasm2c#no-sandbox-mode-experimental), the Wasm
  sandbox is discarded; this means it allows sharing pointers (e.g., memory pointers, function
  pointers) between Wasm and native and the address space is the same in Wasm and native.

<img src="./doc/images/compilation_pipeline.svg" width="1100" height="220" />

### Getting started
- [Build wasm2native compiler](./wasm2native-compiler/README.md)
- [Build auxiliary lib](./wasm2native-vmlib/README.md)
- [Build wasm applications](./doc/build_wasm_app.md)
- [Compile wasm applications to native binary](./doc/compile_wasm_app_to_native.md)
- [Embed compiled native binary in C/C++](./doc/embed_compiled_native.md)
- [Register native APIs for Wasm applications](./doc/register_native_api.md)
- [Samples](./samples) and [Benchmarks](./tests/benchmarks)

License
=======
wasm2native uses the same license as LLVM: the `Apache 2.0 license` with the LLVM exception. See the
LICENSE file for details. This license allows you to freely use, modify, distribute and sell your
own products based on wasm2native. Any contributions you make will be under the same license.
