# Z80/LAB v0.6.0 — Safari-compatible browser development environment

This release adds a third-party ASM80 browser assembler, a source editor,
assembler diagnostics, a machine-code listing, save/open `.asm` and download
Intel HEX, plus a **Set PC** debugger control. It retains the existing C++
CPU core and serial emulator. All emulation and assembly run in the visitor's
browser (no backend and no runtime CDN).

## Third-party assembler and syntax

* `@asm80/core` **1.3.0** (MIT; Martin Maly), bundled via esbuild into a
  self-hosted `assembler.js`. See `THIRD_PARTY_LICENSES.txt`.
* This is **ASM80 syntax**, broadly familiar to Pasmo users, but not a claim
  of full Pasmo source compatibility. `ORG`, `DB`, labels, Zilog mnemonics and
  standard `$` hexadecimal constants are our initial target.
* For v0.6, assembly is single-file. `INCLUDE` / `INCBIN` are deliberately
  disabled. The assembler core upstream is marked *alpha*, so run its real
  integration tests on Euler before relying on unusual directives.
* The source is processed locally; no program text is sent to Euler or a CDN.
  At build time only, npm fetches the pinned dependency.

## Installing on Euler

From your Mac, copy the source ZIP and on Euler extract it:

```sh
cd ~
unzip z80-lab-v0.6.0-source.zip
cd z80-web-v0.6.0
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Activate the Emscripten environment already installed on Euler, and perform
**both** the WebAssembly and independent assembler-bundle builds:

```sh
source ~/emsdk/emsdk_env.sh
bash scripts/build_euler_web.sh
```

This runs `emcmake`, compiles `z80wasm.mjs/.wasm`, installs npm dependencies,
bundles the ASM80 source into static `assembler.js` targeting Safari 15, runs
actual ASM80 integration tests, then checks all the built website assets.
If any command fails, **do not deploy that build**; share the error for repair.

The assembler tests run via `npm run test:assembler` (including an actual
ASM80 run). `ctest` on its own tests native CPU and assembly-result conversion,
but cannot verify the third-party package until it has been installed.

## Local Safari test

On Euler (keep running):

```sh
cd ~/z80-web-v0.6.0/build-wasm/site
python3 -m http.server 8080 --bind 127.0.0.1
```

On the Mac, in a separate Terminal (keep running):

```sh
ssh -N -L 18080:127.0.0.1:8080 pd@euler.lan
```

Open Safari at **http://127.0.0.1:18080/index.html**. If the page was
previously cached, press Option-Command-R.

## Acceptance checks

1. On page load, the CPU displays `PC=0100` and the source editor says
   “Assembler ready · single-file ASM80 syntax”.
2. The default assembly example is a string output program starting at `$0100`.
   Click **Assemble & load**. The interface must show a positive byte count,
   a listing, and PC `$0100`. Click **Run** and the virtual serial terminal
   should print `Hello from Z80/LAB!`.
3. Choose the **Alphabet loop** example, insert and assemble. Run: the serial
   terminal should show `ABCDEFGHIJKLMNOP`.
4. Put an invalid instruction in the source, e.g. `FAKEOP B,C`; the assembler
   must show an error and must **not** replace the running program.
5. Choose **Memory write**, assemble, step or run; memory at `$2000` should
   contain `41 42`. Download the assembled HEX file and load it as a normal
   file using the existing program loader.
6. Set PC manually to `$0103` using the new control, verify the displayed PC;
   Set PC never clears RAM. Check a value outside `$0000–$FFFF` is rejected.
7. Verify Safari Worker execution, breakpoints, manual memory editing, serial
   terminal and native 4 MHz clock still work as they did in v0.5.

## Known boundaries / pending verification

* Native `ctest`: verified in the release build environment.
* The Emscripten/WASM build, real assembler integration and live Safari run
  **must be run on Euler**; these are not claimed to have been executed here.
* The native CPU code (`src/cpu.cpp`) is unchanged from ZEXDOC/ZEXALL 67/67
  validated v0.3.x. Full conformance on the web build remains a future task.
* No source-level breakpoints yet (that will follow once listing/source maps
  are integrated). The source editor is deliberately single-file in v0.6.
