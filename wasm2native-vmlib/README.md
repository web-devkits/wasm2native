### Auxiliary libraries

These auxiliary libraries provide extra functionality for a `wasm2native` binary:
- `libvmlib.a`: designed for sandbox mode, this library provides native API wrappers, a memory
  allocator for a host-managed heap, and APIs to execute the Wasm functions in the native binary.
- `libnosandbox.a`: for no-sandbox mode, this library only provides API wrappers.

### Build

```shell
cd wasm2native-vmlib
mkdir build && cd build
cmake .. -DW2N_BUILD_WASM_APPLICATION=1
# or cmake .., if no need to compile the application folder
# libvmlib.a and libnosandbox.a are generated under current directory
```
