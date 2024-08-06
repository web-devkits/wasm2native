# wasm2native test suites

This folder contains test scripts and cases for wasm2native.

## Help

```
./test_w2n.sh --help
```

## Examples

Test spec cases by default, which will create folder `workspace`, download the `spec` and `wabt`
repo, and build each native binary automatically to test spec cases:
```
./test_w2n.sh -s spec
```

Test spec cases and use the wabt binary release package instead of compiling wabt from the source
code:
```
./test_w2n.sh -s spec -b
```

Test spec cases, use the wabt binary and run the spec test parallelly:
```
./test_w2n.sh -s spec -b -P
```

Test spec cases with SIMD enabled:
```
./test_w2n.sh -s spec -S -b
```

Test spec cases on target x86_32:
```
./test_w2n.sh -s spec -m x86_32 -b
```
