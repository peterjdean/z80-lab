# Z80/LAB — historical browser development notes (v0.6–v0.9)

For v1.0 public usage see [the user guide](web/help.html); for current release
and deployment steps see [README.md](README.md) and [RELEASE.md](RELEASE.md).
This file preserves the incremental v0.6–v0.9 development history.

The v0.6 baseline added a third-party ASM80 browser assembler, a source editor,
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


## v0.7 source-level debugging (development branch)

After **Assemble & load**, each emitted instruction start address is linked to
ASM80's original single-file source line. The running PC highlights the
matching source line, and clicking an *enabled* source line number toggles a
real Z80 breakpoint. Breakpoints added through the disassembly also appear in
the source gutter. This is assembled-address mapping, not a heuristic parser:
`ORG` regions and relative branches use the assembler's actual addresses.

Only lines that emitted bytes have a clickable source breakpoint marker. For a
`DB`/`DS` line, the row start is considered a potential entry address, but it
is not necessarily an executable instruction. Breakpoints inside instruction
operands do not fire. When PC leaves the map (e.g. a subroutine entered from
external code), the source highlight disappears rather than guessing.

Changing source text, loading another program, or manually editing bytes
overlapping the emitted code invalidates the source map until the next
**Assemble & load**. Reset/Set PC alone keep the map. Source maps do not
currently survive a page reload, and macros that lack a source line in ASM80's
output remain unmapped. No WebAssembly CPU code is changed.

### Source-debugging acceptance checks

1. Assemble the alphabet example; verify source line numbers for `LD B,16`,
   `LD A,'A'`, `OUT`, `INC`, `DJNZ`, and `HALT` are clickable.
2. Click the `INC A` line number; Run should stop with PC at `$0106` before
   executing `INC A`, with that source line highlighted.
3. Press Run again, then Pause; it should pass that instruction and loop,
   eventually returning to the same breakpoint on the next iteration.
4. Clear breakpoints, Single-step from reset; the current source line should
   follow PC and the disassembly highlight.
5. Change source text; source breakpoints should be disabled pending reassembly.
6. Edit a byte within assembled code; the mapping should be invalidated, not
   show potentially stale correspondence.
7. Test in Safari and check browser console before merging the PR.


## v0.8 session persistence (development)

Click **Save session** to pause the emulated CPU and download
`z80-lab-session.z80session`. Click **Open session** to restore the file in
this browser. No backend is involved. The restored CPU always starts paused.
The session includes 64 KiB RAM, every Z80 register and interrupt flag,
T-states/instruction counts, current serial queues/transcript, virtual ports,
reset entry, breakpoints, execution speed, memory/disassembly inspector
positions, assembler source, listing and source mapping.

### Acceptance test on Euler/Safari

1. Assemble the Alphabet loop, break at the `INC A` source line, run to it.
2. Use **Save session** and keep the `.z80session` file.
3. Change source, clear the breakpoints and load a different sample.
4. Open the saved session. The PC, A/B registers, breakpoint, source highlight,
   clock, and displayed serial output should match the paused saved state.
5. Press **Run**; the program should continue from the saved PC rather than
   restarting. A save while running also pauses the virtual CPU.
6. Try opening a non-session JSON file: the app should show an error and leave
   the current working machine untouched.

Tests: `ctest --test-dir build --output-on-failure` runs the native binary
snapshot round-trip tests. `npm run test:assembler` now also runs the portable
session file format validation tests. An actual Emscripten/Safari build must
be verified on Euler.


## v0.9 — source stepping, shortcuts, and assembler error locations

* **Step source** (F11 or Shift+F10) is enabled after successful **Assemble &
  load**. It advances from the current assembly line until a *different*
  mapped line, a breakpoint, HALT, or an unmapped destination is reached.
  Unlike a subroutine "step over", this follows a CALL into its destination.
* Individual source lines that expand into several machine instructions run
  through the expansion before stopping at the next line. A loop that stays on
  the same line is limited to 20,000 executed instructions; the Worker yields
  between batches so Pause can interrupt.
* F5 runs, Shift+F5 pauses, F10 steps one instruction; Command/Ctrl+Enter
  assembles and loads even when the source editor is focused. Safari/macOS
  may require pressing Fn with function keys. The buttons remain available.
* ASM80 error messages containing a valid line number are shown immediately
  under the source editor. The offending line is highlighted and clicking its
  diagnostic moves the caret there. Editing source clears the markers.
* Source stepping is disabled when the source map is invalidated by edits or
  loading different machine code. Sessions remain compatible with v0.8.

### Verify on Euler before committing v0.9

1. From `feature/source-stepping`, build the native tests and WebAssembly site
   using the established `bash scripts/build_euler_web.sh` script; confirm
   all checks succeed.
2. Restart the Python server from `build-wasm/site`, then hard-reload Safari
   (Option-Command-R). The **Step source** button appears beside **Step**.
3. Load **Alphabet loop** and Assemble & load. With PC=$0100, click Step source:
   PC should advance to $0102 (LD A,'A'); click again to reach $0104
   (OUT ($00),A). The highlighted editor line should follow.
4. Set a breakpoint on the INC A line ($0106) and click Step source from
   $0104. Execution must stop at $0106 **before** INC A; A should be $41 and
   the serial terminal should contain `A`. Click Step source again and
   confirm it advances to $0107, with A=$42.
5. Place the editor cursor within the source text and use F11 (or Shift+F10)
   to step. Press Command+Enter to assemble. For macOS keyboards, Fn may be
   needed with the F-keys.
6. Insert `FAKEOP A,B` on a fresh line, assemble, and confirm a readable
   diagnostic with a red source-line marker. Click the diagnostic and confirm
   the editor caret jumps to the error line. Fix the source and confirm the
   marker clears. The rejected assembly must not overwrite CPU memory.
7. Save/Open an existing v0.8 `.z80session` and verify resumed execution,
   source mapping, breakpoints and terminal behaviour still work.

Automated tests: `ctest --test-dir build --output-on-failure`,
`npm run test:assembler`, and (where Playwright/Chromium are installed)
`python3 tests/source_step_ui_smoke.py`. The real Safari test is required
because the local UI smoke test uses a mocked Web Worker and assembler.
