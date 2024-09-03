# Embed compiled native object file guideline

From the previous documentation on how to [compile Wasm to native](./compile_wasm_app_to_native.md), we have seen how to compile the wasm app to a native object file, then linking it with the auxiliary library, both in `sandbox` mode and `no-sandbox` mode, to produce the final product(executable file, shared library, or static library).

In this section, we will explore the difference between `sandbox` and `no-sandbox` modes in more details, and how you may embed the static library and shared library linked from the native object file and auxiliary library within your application.

The complete sample code can be found in this [directory](../samples/embed-compiled-native/).

## Wasm app is compiled to native in sandbox mode

The sandbox mode is the default mode when compiling the wasm app to native. As its name suggests, it preserves the Wasm sandbox mechanism, and the Wasm address space and native address space are kept separate.

```bash
# Example of compiling wasm app to native object file in sandbox mode
./wasm2native --format=object --heap-size=16384 -o main.o main.wasm
```

In the sandbox mode, the native binary object file will export the API defined in `w2n_export.h`, you can use them in the host native binary to interact with the wasm app, such as `wasm_instance_create`, `wasm_get_export_apis` can be used to lookup the wasm function and then call it. You can also get exceptions, the base address and size of linear memory, host-managed heap information, etc.

The native binary object needs to be further linked with `libc-builtin.c` to provide C standard library APIs. After that, it can mainly used in two ways, link with source files like `wasm_application.c` and `main.c` to generate an executable file as the final product, or link without them to generate a library as the final product.

### Choice one: Link with source files like `wasm_application.c` and `main.c` to generate an executable file as the final product

As we can see in [compile wasm applications to native binary](./compile_wasm_app_to_native.md), the `wasm2native` compiler can generate an executable file by linking with auxiliary library `libvmlib.a` which includes source files `wasm_application.c` and `main.c`.

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

### Choice two: If `wasm_application.c` and `main.c` are **not** linked, then normally a library(either static library or dynamic library) is generated as final product

When using this library, normally in the other source code that the native object file is linked with, use export APIs defined in `w2n_export.h` to interact with the wasm app.

#### Shared library

Now let's take a look at how to use the static library, for the sandbox shared library, after linking, you can use the shared library in the other source code that the native object file linked with, that other source needs to use export APIs defined in `w2n_export.h` to properly handle the wasm instance, lookup the wasm function, and call it. One example is shown below, use `wasm_application.c` and `main.c` in the directory `wasm2native-vmlib/application`.

```bash
gcc -O3 -o sandbox_with_sharedlib \
  extracted_objs/main.c.o extracted_objs/wasm_application.c.o \
  -L. -lsharedsandbox
# Example usage of executable(call main function of wasm app)
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
# Example usage of executable(call main function of wasm app)
./sandbox_with_staticlib 
```

## Wasm app is compiled to native in no-sandbox mode

In the no-sandbox mode, the Wasm sandbox is discarded, and the Wasm address space and native address space are the same. This allows sharing pointers (e.g., memory pointers, function pointers) between Wasm and native.

```bash
# Example of compiling wasm app to native object file in no-sandbox mode
./wasm2native --format=object --no-sandbox-mode -o nomain_nosandbox.o nomain.wasm
```

> PS: Normally the wasm object file shouldn't have a main function since the main function normally is provided by the host native binary.

Normally, it will link with `libc64_nosandbox_wrapper.c`, either generate an executable file(it will link with the `libnosandbox.a` which includes this source code) or a library(it will link with the object file of this source code), but either way, the usage is more or less the same, it's **similar to the one compiled directly by gcc**. You can use the native binary generated by gcc the same way as here.

### Example usage of an executable file

For example, hello world [sample code](../samples/hello-world/), you can use the executable file like a normal executable file.

```bash
./test_mem64_nosandbox
```

### Example usage of a shared/static library

For the no-sandbox library, after linking, you can use the library in the other source code that the native object file is linked with, that other source can directly use the functions in the Wasm object file.

#### No-sandbox shared library

```bash
gcc -O3 -o nosandbox_with_sharedlib ../src/nosandbox_main.c -L. -lsharednosandbox
# Example usage of executable, in the nosandbox_main.c it calls the add function in the wasm object file
LD_LIBRARY_PATH=$PWD ./nosandbox_with_sharedlib
```

#### No-sandbox static library

```bash
gcc -O3 -o nosandbox_with_staticlib ../src/nosandbox_main.c -L. -lstaticnosandbox
# Example usage of executable, in the nosandbox_main.c it calls the add function in the wasm object file
./nosandbox_with_staticlib
```

## General workflow of embedding wasm object file generated library

In the previous two sections, we have seen how to use the final product of the wasm app compiled to native in sandbox mode and no-sandbox mode. Here we will explore more details of the general workflow of embedding the wasm final product library in the host native binary.

For the **no-sandbox mode** library, use the library as you would use a normal library, for example, in your wasm application you will make a library as the final product, and you can have such function:

```C
// wasm app C source
int
add(int a, int b)
{
    return a + b;
}
```

Then in your host native binary, you can call this function directly.

```C
#include <stdio.h>

extern int
add(int a, int b);

int
main()
{
    printf("Nosandbox main: add 3 + 4 = %d\n", add(3, 4));
}
```

So in this section, we will focus on the sandbox mode library.

Here is the more detailed process of how to use the export APIs defined in `w2n_export.h` to interact with the wasm app(in the form of a library) in the host native binary:

### Embed the wasm app library in the host native binary

First, the wasm instance needs to be created using `wasm_instance_create()`, and check whether it creates successfully using `wasm_instance_is_created()`. If it fails or at the end of the host native binary, we need to destroy the wasm instance.

```C
    wasm_instance_create();
    if (!wasm_instance_is_created()) {
        os_printf("Create wasm instance failed.\n");
        goto fail;
    }

    ...

fail:
    wasm_instance_destroy();
    return ret;
```

Then you can use `wasm_get_export_apis()` and `wasm_get_export_api_num()` to get and call the wasm function by name, following similar procedures as below(a full example process can be found in `execute_func` of [wasm_application.c](wasm2native-vmlib/application/wasm_application.c)):

```C
    WASMExportApi *export_apis = wasm_get_export_apis();
    WASMExportApi *export_func = NULL;
    void (*invoke_native)(const void *, uint32 *, uint32 *);
    uint32 export_api_num = wasm_get_export_api_num();

    /* Lookup exported function */
    for (i = 0; i < export_api_num; i++) {
        if (!strcmp(export_apis[i].func_name, name)) {
            export_func = &export_apis[i];
            break;
        }
    }

    /* from the export_func->signature, parse and prepare the arguments array(argv1) */
    ...

    /* lookup the quick aot entry, basically it will reinterpret the function ptr type that will match export_func->signature */
    invoke_native = lookup_quick_aot_entry(export_func->signature);

    /* call the wasm function, use the argv1 as both parameter and return array */
    invoke_native(export_func->func_ptr, argv1, argv1);

    /* parse and process the return value from the return array(argv1) */
    ...
```

PS: during the wasm instance creation or calling the wasm function, you can use `wasm_get_exception()` and `wasm_get_exception_msg()` to check the exception and utilize the exception message.

```C
static void
check_and_print_exception()
{
    int32 exce_id = wasm_get_exception();
    const char*exce_msg;
    if (exce_id) {
        exce_msg = wasm_get_exception_msg();
        os_printf("Exception: %s\n", exce_msg);
    }
    else {
        os_printf("Exception: unknown error\n");
    }
}

// check whether create wasm instance successfully
int
main(int argc, char *argv[])
{
    ...

    if (!wasm_instance_is_created()) {
        os_printf("Create wasm instance failed.\n");
        check_and_print_exception();
        goto fail;
    }

    ...
    
    if (!app_instance_func(func_name))
        ret = 1;

    if (ret) {
        check_and_print_exception();
    }

    ...

}
```
