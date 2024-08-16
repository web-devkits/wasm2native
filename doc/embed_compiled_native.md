# Embed compiled native object file in C/C++

From the previous documentation on how to [compile Wasm to native](./compile_wasm_app_to_native.md), we have seen how to compile the wasm app to a native object file and link it with the auxiliary library to the native binary, both `sandbox` mode and `no-sandbox` mode, to produce the final product(executable file, shared library, or static library). In this section, we will explore the difference between `sandbox` and `no-sandbox` modes in more detail.

You can find the sample C/C++ source code and shell scripts to automatically compile/run it in this [directory](../samples/compiled-embed-native/).

## Wasm app is compiled to native in sandbox mode

The sandbox mode is the default mode when compiling the wasm app to native. As its name suggests, it preserves the Wasm sandbox mechanism, and the Wasm address space and native address space are kept separate.

```bash
# Example of compiling wasm app to native object file in sandbox mode
${WASM2NATIVE_CMD} --format=object --heap-size=16384 -o main.o main.wasm
```

In the sandbox mode, the native binary object file will export the API defined in `w2n_export.h`, you can use them in the host native binary to interact with the wasm app, such as `wasm_instance_create`, `wasm_get_export_apis` can be used to lookup the wasm function and then call it. You can also get exceptions, the base address and size of linear memory, host-managed heap information, etc.

And the native binary object needs to be further linked with `libc-builtin.c` to provide C standard library APIs. After that, it can mainly used in two ways:

### 1. Link with source files like `wasm_application.c` and `main.c` to generate an executable file as final product

As we can see in [compile wasm applications to native binary](./compile_wasm_app_to_native.md), the `wasm2native` compiler can generate an executable file by linking with auxiliary library `libvmlib.a` which includes source files `wasm_application.c` and `main.c` .

When linking with source files like `wasm_application.c` and `main.c`, an **executable file** is generated, and the `--invoke func` and `--repl` command line options are supported to call a function. If these two options are not used, the default is to execute the main function of the wasm app, if it exists.

For example, for this hello world [sample code](../samples/hello-world/), you can use it like:

```bash
# run main wasm function
./test_mem32
# the result will be 0x5:i32
./test_mem32 --invoke add 2 3
# it will show the prompt like "webassembl>", and you can run the command like "add 3 4", it will return the result "0x7:i32"
./test_mem32 --repl
webassembly> add 3 4
0x7:i32
```

For more details, you can refer to the [this section](./compile_wasm_app_to_native.md#binary-executable).

### 2. If `wasm_application.c` and `main.c` are **not** linked, then normally a library(either static library or dynamic library) is generated as final product

And when using this library, normally in the other source code that the native object file linked with, use export APIs defined in `w2n_export.h` to interact with the wasm app.

#### Shared library

Now let's take a look at how to use the static library, for sandbox shared library, after linking, you can use the shared library in the other source code that the native object file linked with, that other source needs to use export APIs defined in `w2n_export.h` to properly handle the wasm instance, lookup the wasm function, and call it. One example is shown below, use `wasm_application.c` and `main.c` in the directory `wasm2native-vmlib/application`.

```bash
gcc -O3 -o sandbox_with_sharedlib \
  extracted_objs/main.c.o extracted_objs/wasm_application.c.o \
  -L. -lsharedsandbox
# Example usage of executable(call main fucion of wasm app)
LD_LIBRARY_PATH=$PWD ./sandbox_with_sharedlib
# Example usage of executable(print help info)
LD_LIBRARY_PATH=$PWD ./sandbox_with_sharedlib --help
```

#### Static library

Similar to the shared library, you can use the static library in the other source code that the native object file linked with, that other source needs to use export APIs defined in `w2n_export.h` to properly handle the wasm instance, lookup the wasm function, and call it.

```bash
gcc -O3 -o sandbox_with_staticlib \
  extracted_objs/main.c.o extracted_objs/wasm_application.c.o \
  -L. -lstaticsandbox
# Example usage of executable(call main fucion of wasm app)
./sandbox_with_staticlib 
```

## Wasm app is compiled to native in no-sandbox mode

In the no-sandbox mode, the Wasm sandbox is discarded, and the Wasm address space and native address space are the same. This allows sharing pointers (e.g., memory pointers, function pointers) between Wasm and native.

```bash
# Example of compiling wasm app to native object file in nosandbox mode
${WASM2NATIVE_CMD} --format=object --no-sandbox-mode -o nomain_nosandbox.o nomain.wasm
```

> PS: Normally the wasm object file shouldn't have a main function since the main function normally is provided by the host native binary.

Normally, it will link with `libc64_nosandbox_wrapper.c`, either generate a executable file(it will link with the `libnosandbox.a` which include this source code) or library(it will link with the object file of this source code), but either way, the usage is more or less the same, it's **similar to the one compiled directly by gcc**. You can use the native binary generated by gcc the same way as here.

### Example usage of a executable file

Example hello world [sample code](../samples/hello-world/), you can use executable file like normal executable file.

```bash
./test_mem64_nosandbox
```

### Example usage a shared/static library

For no-sandbox library, after linking, you can use the library in the other source code that the native object file linked with, that other source can directly use the functions in the Wasm object file.

#### Nosandbox shared library

```bash
gcc -O3 -o nosandbox_with_sharedlib ../src/nosandbox_main.c -L. -lsharednosandbox
# Example usage of executable, in the nosandbox_main.c it call the add function in the wasm object file
LD_LIBRARY_PATH=$PWD ./nosandbox_with_sharedlib
```

#### Nosandbox static library

```bash
gcc -O3 -o nosandbox_with_staticlib ../src/nosandbox_main.c -L. -lstaticnosandbox
# Example usage of executable, in the nosandbox_main.c it call the add function in the wasm object file
./nosandbox_with_staticlib
```
