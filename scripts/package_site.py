#!/usr/bin/env python3
"""Build a checked, standalone static-site ZIP from the Emscripten output.

Only explicitly approved runtime assets are shipped. No node_modules, source
trees, private files, or incidental files from the build directory are included.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
ASSETS = (
    "index.html",
    "help.html",
    "app.js",
    "worker.js",
    "style.css",
    "assembler.js",
    "assembler-core.mjs",
    "assembler-source.js",
    "session-format.mjs",
    "source-step.mjs",
    "z80wasm.mjs",
    "z80wasm.wasm",
    "THIRD_PARTY_LICENSES.txt",
    "EMSCRIPTEN_LICENSE.txt",
    "LICENSE",
    "examples/hello.hex",
    "examples/monitor.hex",
    "examples/console_hello.hex",
    "examples/console_echo.hex",
)
MAX_ASSET_BYTES = 25 * 1024 * 1024
ZIP_DATE = (2020, 1, 1, 0, 0, 0)


def project_version():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(z80emu VERSION (\d+\.\d+\.\d+)", cmake)
    if not match:
        raise ValueError("Cannot determine Z80/LAB version from CMake")
    return match.group(1)


def zip_entry(name, data):
    info = zipfile.ZipInfo(name, ZIP_DATE)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    return info, data


def create_package(site, output, revision="local", version=None):
    site = Path(site)
    output = Path(output)
    if not site.is_dir():
        raise ValueError(f"Site directory does not exist: {site}")
    if not (revision == "local" or re.fullmatch(r"[0-9a-fA-F]{40}", revision)):
        raise ValueError("Revision must be 'local' or a 40-digit Git commit SHA")

    payloads = {}
    file_info = {}
    for name in sorted(ASSETS):
        path = site / name
        if path.is_symlink() or path.parent.is_symlink() or not path.is_file():
            raise ValueError(f"Missing, invalid, or symlinked site asset: {name}")
        size = path.stat().st_size
        if size == 0 or size > MAX_ASSET_BYTES:
            raise ValueError(f"Empty or oversized site asset: {name}")
        data = path.read_bytes()
        if name == "z80wasm.wasm" and not data.startswith(b"\x00asm\x01\x00\x00\x00"):
            raise ValueError("WebAssembly binary has an invalid magic/version header")
        payloads[name] = data
        file_info[name] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}

    manifest = {
        "format": "z80-lab-static-site",
        "version": version or project_version(),
        "revision": revision,
        "files": file_info,
    }
    payloads["site-manifest.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")

    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, mode="w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name in sorted(payloads):
            info, data = zip_entry(name, payloads[name])
            archive.writestr(info, data, compresslevel=9)

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    checksum = output.with_name(output.name + ".sha256")
    checksum.write_text(f"{digest}  {output.name}\n", encoding="ascii")
    print(f"Z80/LAB static release candidate: {output} ({output.stat().st_size} bytes)")
    print(f"SHA256: {digest}")
    return output, checksum


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--site", type=Path, default=Path("build-wasm/site"))
    parser.add_argument("--output", type=Path, default=Path("dist/z80-lab-site.zip"))
    parser.add_argument("--revision", default="local", help="40-character Git SHA or 'local'")
    args = parser.parse_args()
    try:
        create_package(args.site, args.output, args.revision)
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        parser.exit(1, f"FAIL: {error}\n")
