#!/usr/bin/env python3
"""Static smoke-check for the browser entrypoints and optional built site."""
from html.parser import HTMLParser
from pathlib import Path
import argparse
import re
import sys

ROOT = Path(__file__).resolve().parents[1]

class Ids(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = set()
    def handle_starttag(self, tag, attrs):
        for key, val in attrs:
            if key == "id" and val:
                if val in self.ids:
                    raise ValueError(f"Duplicate HTML id: {val}")
                self.ids.add(val)


def check(root, site=False):
    base = Path(root)
    expected = ["index.html", "app.js", "worker.js", "style.css", "assembler-core.mjs", "assembler-source.js", "session-format.mjs"]
    if site:
        expected += ["z80wasm.mjs", "z80wasm.wasm", "assembler.js", "THIRD_PARTY_LICENSES.txt",
                     "examples/hello.hex", "examples/console_hello.hex",
                     "examples/console_echo.hex", "examples/monitor.hex"]
    for name in expected:
        path = base / name
        if not path.is_file() or path.stat().st_size == 0:
            raise ValueError(f"Missing or empty web asset: {path}")
    html = (base / "index.html").read_text()
    js = (base / "app.js").read_text()
    worker = (base / "worker.js").read_text()
    ids = Ids()
    ids.feed(html)
    for match in re.finditer(r"\$\('([a-zA-Z0-9-]+)'\)", js):
        if match.group(1) not in ids.ids:
            raise ValueError(f"App references unknown HTML id: {match.group(1)}")
    for key in ("z80wasm.mjs", "_z80_run_slice", "_z80_set_breakpoint",
                "_z80_write_byte", "_z80_set_pc", "_z80_snapshot_save", "_z80_snapshot_restore", "_z80_state_json", "_z80_load",
                "disasm-follow", "breakpoint-toggle", "write-memory"):
        if key not in worker:
            raise ValueError(f"Worker missing required code: {key}")
    for required in ("memory-edit-form", "disasm-at", "breakpoint-list", "memory-next",
                     "asm-source", "asm-assemble", "pc-set-value", "pc-set", "asm-listing",
                     "session-file", "session-save"):
        if required not in ids.ids:
            raise ValueError(f"Missing debugger element: {required}")
    if site and not (base / "assembler.js").read_text().strip():
        raise ValueError("Empty bundled ASM80 assembler")
    print(f"PASS: {'built site' if site else 'source web'}: {len(ids.ids)} unique HTML ids, entrypoints and assets")

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--site', help='Validate an Emscripten-built site directory')
    args = parser.parse_args()
    try:
        check(args.site or ROOT / 'web', bool(args.site))
    except ValueError as error:
        print(f'FAIL: {error}', file=sys.stderr)
        sys.exit(1)
