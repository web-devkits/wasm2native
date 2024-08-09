### Pre-requisites

Build the [wasm2native compiler](../wasm2native-compiler/README.md) and [wasm2native vmlib](../wasm2native-vmlib/README.md).

### Example usage

After compiling the C/C++ source code to wasm32/wasm64 file follow the steps in [build_wasm_app.md](./build_wasm_app.md). You can use the `wasm2native` to compile the Wasm file to a native object file, and then use a linker like gcc/clang to link that object file with `vmlib` to generate the final executable file, here is an example:

#### Compile wasm32/wasm64 code to the native object file

You can compile the wasm32/wasm64 to either sandbox mode or non-sandbox mode.

```bash
# compile wasm32/64 to native object file in sandbox mode
./wasm2native --format=object --heap-size=16384 -o test_mem32.o test_mem32.wasm
./wasm2native --format=object --heap-size=16384 -o test_mem64.o test_mem64.wasm
# compile wasm64 to native object file in non-sandbox mode
./wasm2native --format=object --no-sandbox-mode -o test_mem64_nosandbox.o test_mem64.wasm
```

#### Link the object file with the auxiliary library to generate the final executable file

There are two auxiliary libraries, `vmlib` and `nosandbox`, used for `sandbox` and `no-sandbox` mode object file. You can either link with `vmlib` to generate the executable file in sandbox mode or `nosandbox` to generate the executable file in non-sandbox mode.

```bash
gcc -O3 -o test_mem32 test_mem32.o -L ../build -lvmlib
gcc -O3 -o test_mem64 test_mem64.o -L ../build -lvmlib
gcc -O3 -o test_mem64_nosandbox test_mem64_nosandbox.o -L ../build -lnosandbox -lm
```

For more detail about the difference between `sandbox` and `nosandbox`, it can be found in [embed_compiled_native.md](./embed_compiled_native.md).

#### Execute the generated executable file

After linking, you can execute the generated executable file like normal executable, whether in sandbox mode or non-sandbox mode.

```bash
./test_mem32
./test_mem64_nosandbox
```

Additionally, in **sandbox** mode, you can

- pass some command line to specify a function name of the module to run rather than main

- start a simple REPL (read-eval-print-loop) mode that runs commands in the form of "FUNC ARG...".

The detail can be seen by running the command with `--help` option.

```bash
# For more detail show the help message
./test_mem32 --help
```
