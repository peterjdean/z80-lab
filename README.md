# Z80/LAB v0.6.0

Generic Z80 emulator with 64 KiB RAM, terminal debugger, CPU registers, serial
I/O ports, disassembly, breakpoints, memory viewer/editor, 4 MHz clock option,
and a Safari-first browser build. This release introduces a client-side
third-party Z80 assembler using ASM80 (`@asm80/core` 1.3.0, MIT).

**Build on Ubuntu/Euler:** see [WEB.md](WEB.md) for complete instructions,
including the separate assembler bundle build and Safari acceptance test.

**Native tests:**

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

**Web build on Euler:**

```sh
source ~/emsdk/emsdk_env.sh
bash scripts/build_euler_web.sh
```

The browser site is produced in `build-wasm/site/`. For a public deployment,
serve the entire directory (including `assembler.js`, `z80wasm.mjs`,
`z80wasm.wasm`, sample programs and `THIRD_PARTY_LICENSES.txt`) over HTTPS.

**Compatibility:** Safari 15+ is a *design target*. The compiler targets
standard 32-bit WebAssembly, with no pthreads, memory64 or SIMD requirement.
The actual v0.6 assembler bundle and Safari run still need verification on
Euler and the Mac.

**Source-code semantics:** ASM80 is not Pasmo. Some Pasmo-specific constructs
may need changes; in v0.6 `INCLUDE` and `INCBIN` are intentionally unsupported.

**Licensing:** original project retains its existing `LICENSE`; the embedded
third-party assembler is MIT and its notice is preserved in
`THIRD_PARTY_LICENSES.txt`.
