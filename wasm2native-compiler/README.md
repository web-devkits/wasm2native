### Build wasm2native compiler

The wasm2native compiler is to compile wasm binary file to object file. You can execute following commands to build **wasm2native** compiler:

For **Linux**(Ubuntu 20.04 as an example):

First, make sure necessary dependency are installed:

```shell
sudo apt-get install git build-essential cmake g++-multilib libgcc-9-dev lib32gcc-9-dev ccache 
```

```shell
cd wasm2native-compiler
./build_llvm.sh (or "./build_llvm_xtensa.sh" to support xtensa target)
mkdir build && cd build
cmake .. (or "cmake .. -DW2N_BUILD_PLATFORM=darwin" for MacOS)
make
# wasm2native is generated under current directory
```

For **Windows**：

```shell
cd wasm2native-compiler
python build_llvm.py
mkdir build && cd build
cmake ..
cmake --build . --config Release
# wasm2native.exe is generated under .\Release directory
```
