wasm2native Attributions
========================

The `wasm2native` project reuses some components from other open source project:
- **llvm**: for the AOT compilation
- **wasm-micro-runtime**: for the AOT compilation and the auxiliary library
- **Dhrystone**: for the test benchmark dhrystone
- **stream**: for the test standalone case stream
- **tiny-AES-c**: for the test standalone case test-aes

|  third party components | version number | latest release | vendor pages | CVE details |
| --- | --- | --- | --- | --- |
| llvm | 18.1.8 | 18.1.8 | https://llvm.org | https://www.cvedetails.com/vendor/13260/Llvm.html |
| wasm-micro-runtime | 2.1.1 | 2.1.1 | https://github.com/bytecodealliance/wasm-micro-runtime | |
| Dhrystone | 2.1 | 2.1 | https://fossies.org/linux/privat/old/ | |
| stream | 5.10 | 5.10 | | |
| tiny-AES-c | 1.0.0 | 1.0.0 | https://github.com/kokke/tiny-AES-c | |

## Licenses

### llvm

[LICENSE](./LICENCE.txt)

### wasm-micro-runtime

[LICENSE](./LICENCE.txt)

### Dhrystone

[LICENSE](./tests/benchmarks/dhrystone/LICENSE)

### stream

[LICENSE](./tests/standalone/stream/stream.c)

### tiny-AES-c

[LICENSE](./tests/standalone/test-aes/unlicense.txt)
