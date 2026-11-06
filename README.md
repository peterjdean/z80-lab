# Z80/LAB — v0.8 development

Generic Z80 emulator with 64 KiB RAM, terminal debugger, CPU registers, serial
I/O ports, disassembly, breakpoints, memory viewer/editor, 4 MHz clock option,
and a Safari-first browser build. The browser includes an integrated ASM80 assembler (`@asm80/core` 1.3.0, MIT),
source-level debugging, and portable saved sessions in v0.8 development.

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
The browser assembly and v0.7 debugger were previously verified on Euler
and Safari; v0.8 session restoration still needs Euler/Safari acceptance.

**Source-code semantics:** ASM80 is not Pasmo. Some Pasmo-specific constructs
may need changes; in v0.6 `INCLUDE` and `INCBIN` are intentionally unsupported.

**Licensing:** original project retains its existing `LICENSE`; the embedded
third-party assembler is MIT and its notice is preserved in
`THIRD_PARTY_LICENSES.txt`.


### v0.7 development: source-level debugging

Since the merged v0.7 release, ASM80's 1-based source line metadata
links emitted instruction starts to assembly source. An active source line is
highlighted alongside the disassembly, and clicking its line number toggles
a real breakpoint. The mapping is discarded after changes to source text,
manual writes overlapping assembled code, or loading a different program.
The C++ Z80 core remains unchanged. See `WEB.md` for the Safari acceptance
checklist.


## v0.8 development — portable sessions

The browser can save a `.z80session` JSON file and open it later, entirely
locally. The file includes a versioned native CPU snapshot (64 KiB RAM,
registers including shadow/interrupt state, virtual port state, instruction
and T-state counters, serial RX queue/transcript, reset entry and recent port
writes), plus breakpoints, clock setting, disassembly/memory position, assembly
source, listing, source map and displayed serial terminal. A restored session
starts **paused**, even if it was saved while executing; press Run to continue.
Files are untrusted input and are checked before applying. v0.8 files have
`format: "z80-lab-session", version: 1`; unrelated versions are rejected.
There is no server-side persistence, upload or automatic cloud sync.

For the initial version, the saved source map is restored from the file rather
than regenerated and checked against self-modified RAM; reassemble if you alter
machine code outside the editor. Browser disassembly Back history is not saved.
