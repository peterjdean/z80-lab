# Z80/LAB — licensing and attribution

Z80/LAB is an independent, unofficial, non-commercial Z80-compatible
educational/development emulator. The project is **not** affiliated with,
sponsored by or endorsed by Zilog, Inc. The word **Z80** is used to
describe compatibility with the historical Zilog Z80 instruction set.
Z80 is a trademark of Zilog, Inc.; use of that name does not imply
ownership of the mark or any endorsement.

## Software distribution

- **Original Z80/LAB code and documentation:** [MIT licence](LICENSE),
  copyright (c) 2026 z80emu contributors. The MIT grant permits free
  sharing, modification and redistribution, provided the notice is
  retained. This licence covers our implementation, not another party's
  independently copyrighted works or marks.
- **Browser assembler:** `@asm80/core` 1.3.0 by Martin Maly, MIT.
  Its full copyright and licence notice is in
  [THIRD_PARTY_LICENSES.txt](THIRD_PARTY_LICENSES.txt). Source:
  https://github.com/asm80/asm80-core.
- **Emscripten-generated runtime support:** compiled with
  Emscripten **4.0.23**, under permissive MIT and University of
  Illinois/NCSA terms. The pinned upstream licence text is included in
  [EMSCRIPTEN_LICENSE.txt](EMSCRIPTEN_LICENSE.txt). Source:
  https://github.com/emscripten-core/emscripten/tree/4.0.23.
- **Build-only JavaScript tool:** esbuild 0.25.12 (MIT). It produces
  the bundled assembler JavaScript; esbuild itself, npm dependencies
  and `node_modules` are **not shipped** in the static site.
- **Conformance tests:** Frank Cringle's external ZEXDOC/ZEXALL
  executables are not distributed with Z80/LAB; the repo includes
  only a downloader and test runner. Their separate licence is not
  a licence imposed on our compiled emulator.

The GitHub source repository includes these notices. The standalone
static-site ZIP explicitly packages `LICENSE`,
`THIRD_PARTY_LICENSES.txt` and `EMSCRIPTEN_LICENSE.txt`, and all
three are available directly from the public site.

## Scope and limitations

The project emulates instruction-set behaviour without shipping
proprietary Zilog firmware or a copied original computer ROM. No
statement here grants rights to Zilog's trademarks or guarantees
the provenance of unrelated third-party software. This is a
good-faith notice and inventory, not a formal legal clearance.

The project can be viewed and used without charge. Non-commercial
intent does not change the MIT licence terms or eliminate the
usual copyright and trademark obligations.
