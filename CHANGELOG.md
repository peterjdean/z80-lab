# Z80/LAB release notes

## v1.0.0 — first public release

Z80/LAB is a self-contained browser-based Zilog Z80 development environment,
available at https://z80.peterjdean.com/ .

### Features

- Tested native C++ Z80 core compiled to WebAssembly, 64 KiB RAM, CPU registers,
  flags, instruction/T-state counters, serial terminal and memory tools.
- Integrated ASM80 browser assembler (1.3.0, MIT); source editing, sample
  programs, error diagnostics, machine-code listing and Intel HEX export.
- Source-aware debugger with breakpoints, instruction and source stepping,
  disassembly, configurable virtual clock and manual RAM editing.
- Portable local `.z80session` files preserving CPU/RAM state, source,
  debugger settings, breakpoints and terminal transcript. New downloads use
  timestamped filenames and older saved files remain readable.
- Long `DB` declarations displayed in aligned eight-byte rows.
- In-browser Help & guide plus a verified static-site distribution ZIP.

### Hosting and privacy

- HTTPS with a dedicated certificate and Apache vhost on Popper.
- No server-side account, database, cloud session storage or runtime CDN.
- Per-asset SHA256 manifest, separate staging and production installations,
  atomic release switching and rollback, conservative caching and CSP.

### Compatibility and limitations

- Safari 16+ is the public release target due to CSP; other modern browsers
  should work but have not all been independently verified.
- ASM80 single-file assembly is not fully Pasmo compatible; `INCLUDE` and
  `INCBIN` are unsupported.
- Source stepping enters calls; subroutine step-over is not implemented.
- Complete external Z80 conformance is not established in the browser, although
  native regression tests and historical ZEXDOC/ZEXALL checks passed.

Previous development milestones: v0.6 assembler; v0.7 source debugging;
v0.8 portable sessions; v0.9 source stepping and inline diagnostics.
