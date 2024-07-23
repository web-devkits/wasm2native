### Overview of the auxiliary lib

The auxiliary lib provides extra functionalities for native binary:
- For sandbox mode, it provides native API wrappers, memory allocator for host managed heap, and APIs to execute the wasm functions in the native binary. The built out lib is libvmlib.a.
- For no-sandbox mode, it only provides API wrappers. The built out lib is libnosandbox.a.

### Build the auxiliary lib

```shell
cd wasm2native-vmlib
mkdir build && cd build
cmake .. -DW2N_BUILD_WASM_APPLICATION=1
# or cmake .., if no need to compile the application folder
# libvmlib.a and libnosandbox.a are generated under current directory
```
