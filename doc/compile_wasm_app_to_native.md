### Pre-requisites

Build the [wasm2native compiler](../wasm2native-compiler/README.md) and [wasm2native vmlib](../wasm2native-vmlib/README.md).

### Example usage

After compiling the C/C++ source code to wasm32/wasm64 file(for more details please follow the steps in [build_wasm_app.md](./build_wasm_app.md)), you can use the `wasm2native` to compile the Wasm file to a native object file in sandbox/no-sandbox mode, and then use a linker like gcc/clang to link that object file with auxiliary library(`libvmlib.a` or `libnosandbox.a`) to generate the final product, it could be an executable file, shared library, or static library.

#### Compile wasm32/wasm64 file to the native object file

You can compile the wasm64 to either sandbox mode or non-sandbox mode, but wasm32 only supports sandbox mode, and wasm64 no-sandbox mode needs to add the compile flag `-Wl,--emit-relocs` when generating the wasm64 file.

```bash
# compile wasm32 to native object file in sandbox mode
./wasm2native --format=object --heap-size=16384 -o test_mem32.o test_mem32.wasm
# compile wasm64 to native object file in sandbox/non-sandbox mode
./wasm2native --format=object --heap-size=16384 -o test_mem64.o test_mem64.wasm
./wasm2native --format=object --no-sandbox-mode -o test_mem64_nosandbox.o test_mem64.wasm
```

#### Link the object file with the auxiliary library to generate the final product

Like a normal object file, you can link it with the auxiliary library to generate the final product. It could be

- [executable file](#binary-executable)
- [shared library](#shared-library)
- [static library](#static-library)

Using the executable file is straightforward, you can execute it like a normal executable file, whether in sandbox mode or non-sandbox mode, for shared library and static library. It's more complex for the shared library or static library. You can find more details on using them in [this section](./embed_compiled_native.md).

You can find the sample demonstrating how you may use the shared library and static library in this [directory](../samples/compiled-embed-native/)

##### Binary executable

There are two auxiliary libraries, `libvmlib.a` and `libnosandbox.a`, used for `sandbox` mode and `no-sandbox` mode object files respectively. You can either link with `libvmlib.a` to generate the executable file in sandbox mode or `libnosandbox.a` to generate the executable file in non-sandbox mode.

```bash
# link object file and vmlib library to generate the executable file in sandbox mode
gcc -O3 -o test_mem32 test_mem32.o -L ../build -lvmlib
gcc -O3 -o test_mem64 test_mem64.o -L ../build -lvmlib
# link object file and nosandbox library to generate the executable file in non-sandbox mode
gcc -O3 -o test_mem64_nosandbox test_mem64_nosandbox.o -L ../build -lnosandbox -lm
```

For more detail about the difference between two modes `sandbox` and `no-sandbox`, please refer to [embed_compiled_native.md](./embed_compiled_native.md).

After linking, you can execute the generated executable file like a normal executable, whether in sandbox mode or non-sandbox mode.

```bash
./test_mem32
./test_mem64_nosandbox
```

Additionally, in **sandbox** mode, you can

- pass some command line arguments to specify a function name of the module to run rather than main. For example, in this case, you can run the function `add` in the module `test_mem32` by

  ```bash
  # the result will be 0x5:i32
  ./test_mem32 --invoke add 2 3
  ```

- start a simple REPL (read-eval-print-loop) mode that runs commands in the form of "FUNC ARG...".

  ```bash
  ./test_mem32 --repl
  # It will show the prompt like "webassembl>", and you can run the command like "add 3 4", it will return the result "0x7:i32"
  webassembly> add 3 4
  0x7:i32
  ```

For more detail on those two usages, you can refer to the help message by running the command with `--help` option.

```bash
# For more detail show the help message
./test_mem32 --help
```

##### Shared library

Like a normal object file, you can link it with the auxiliary library `libvmlib` or `libnosandbox` to generate the shared library.

> PS: when compiling auxiliary libraries, don't add the CMake flag `-DW2N_BUILD_WASM_APPLICATION=1` like in other examples, this option will exclude the main.c and wasm_application.c from the `vmlib` library build source list.

```bash
# link object file and vmlib library to generate the shared library for sandbox mode
gcc -fPIC -shared -o libsharedsandbox.so main.o -L../build/ -lvmlib
# link object file and nosandbox library to generate the shared library for no-sandbox mode
gcc -fPIC -shared -o libsharednosandbox.so nomain_nosandbox.o -L ../build -lnosandbox -lm
```

##### Static library

It's more complex for a static library, you need to extract the existing auxiliary library to object files first, then you can link the object file with them to generate the static library.

> In this step, the vmlib library is built with the main.c and wasm_application.c object files(`-DW2N_BUILD_WASM_APPLICATION=1`), for they will be used when linking the final product library to generate the executable file(details can refer to [embed_compiled_native.md](./embed_compiled_native.md)).

First, extract the object files from the existing static library.

```bash
mkdir extracted_objs
ar x ../build/libvmlib.a --output=extracted_objs
```

Then, create a new static library including your object file, and extracted object files.

For sandbox mode, exclude main.c and wasm_application.c object files.

```bash
ar rcs libstaticsandbox.a main.o $(ls extracted_objs/*.o | grep -vE 'main\.c\.o|wasm_application\.c\.o')
```

For the no-sandbox mode, it's simpler, you can link the object file with the nosandbox object directly

```bash
ar x ../build/libnosandbox.a --output=extracted_objs
ar rcs libstaticnosandbox.a nomain_nosandbox.o extracted_objs/libc64_nosandbox_wrapper.c.o
```
