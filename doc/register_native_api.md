# Register a native API

From the source C/C++ program that would be compiled to the Wasm file, you may have some function declarations (for example, libc functions) that are not implemented, which would be compiled to an import function in the Wasm file. Those import functions need to be implemented in the host native binary, called native APIs.

In this section, we will use `spectest` native APIs as an example, and demonstrate the procedure to register a native API.

## Register the native API declaration

In `aot_llvm.c`, add the declaration of the native API and later in function `create_wasm_instance_create_func`, register native functions. For example, `spectest` native APIs:

```C
static NativeSymbol native_symbols_spectest[] = {
    { "spectest", "print", "()" },
    { "spectest", "print_i32", "(i)" },
    { "spectest", "print_i32_f32", "(if)" },
    { "spectest", "print_f64_f64", "(FF)" },
    { "spectest", "print_f32", "(f)" },
    { "spectest", "print_f64", "(F)" },
};
```

The first is the module name, the second is the function name, and the third is the function signature.

**Function signature**:

The function signature field in **NativeSymbol** structure is a string for describing the function prototype.  It is critical to ensure the function signature is correctly mapping the native function interface.

Each letter in the "()" represents a parameter type, and the one following after ")" represents the return value type. The meaning of each letter:

- '**i**': i32
- '**I**': i64
- '**f**': f32
- '**F**': f64
- '**r**': externref (has to be the value of a `uintptr_t` variable), or all kinds of GC reference types when the GC feature is enabled
- '**\***': the parameter is a buffer address in the WASM application
- '**~**': the parameter is the byte length of the WASM buffer as referred to by the preceding argument "\*". It must follow after '*', otherwise, registration will fail
- '**$**': the parameter is a string in the WASM application

The signature can defined as NULL, and then all function parameters are assumed as i32 data type.

## Provide the implementation in the auxiliary library

The implementation of the native API is provided in the auxiliary library, to be more specific, it is in `libc_builtin_wrapper.c`, you can add the implementation of the native API in this file or another separate source file(but remember to link it in auxiliary library).

In the implementation, the function name needs to be the one that registers previous in `native_symbols_spectest`, for example, `print_i32` plus the suffix `_wrapper`, and the other part of function signature should conform to the previous register one.

For example, the implementation of `spectest` native API `print_i32`:

```C
void
print_i32_wrapper(int32 i32)
{
    os_printf("spectestprint_i32(%d)\n", i32);
}
```
