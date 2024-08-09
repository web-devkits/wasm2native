# wasm2native fuzz test framework

## install wasm-tools

```bash
1.git clone https://github.com/bytecodealliance/wasm-tools
$ cd wasm-tools
2.This project can be installed and compiled from source with this Cargo command:
$ cargo install wasm-tools
3.Installation can be confirmed with:
$ wasm-tools --version
4.Subcommands can be explored with:
$ wasm-tools help
```

## Build

```bash
mkdir build && cd build
cmake ..
make -j
```

## Manually generate wasm file in build

```bash
# wasm-tools smith generate some valid wasm file
# The generated wasm file is in corpus_dir under build
# N - Number of files to be generated
./smith_wasm.sh N
```

# running
``` bash
cd build
./wasm_mutator_fuzz CORPUS_DIR -rss_limit_mb=3072
# increase rss_limit_mb if "libFuzzer: out-of-memory" is reported
# or add `-gnore-ooms=1` flag:
# ./wasm_mutator_fuzz CORPUS_DIR -rss_limit_mb=3072 -ignore_ooms=1
 
```

Refer to [libFuzzer options](https://llvm.org/docs/LibFuzzer.html#options) for more details.
