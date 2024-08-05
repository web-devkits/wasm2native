### Compiler

The `wasm2native` compiler exists to compile Wasm modules to a native object file.

### Build

To build the `wasm2native` compiler:

##### Linux (e.g., Ubuntu 20.04)

Install necessary dependencies:

```shell
sudo apt-get install git build-essential cmake g++-multilib libgcc-9-dev lib32gcc-9-dev ccache
```

Then, build the compiler:

```shell
cd wasm2native-compiler
./build_llvm.sh (or "./build_llvm_xtensa.sh" to support xtensa target)
mkdir build && cd build
cmake .. (or "cmake .. -DW2N_BUILD_PLATFORM=darwin" for MacOS)
make
# wasm2native is generated under current directory
```

##### Windows

```shell
cd wasm2native-compiler
python build_llvm.py
mkdir build && cd build
cmake ..
cmake --build . --config Release
# wasm2native.exe is generated under .\Release directory
```
