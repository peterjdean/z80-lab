#!/usr/bin/env python3
"""Fetch pinned ZEXDOC/ZEXALL binaries for local conformance testing.

The exercisers are GPL-2.0 licensed and are NOT part of this MIT source archive.
See https://github.com/agn453/ZEXALL for source, provenance and license.
"""
from hashlib import sha1
from pathlib import Path
from urllib.request import Request, urlopen
import sys

EXPECTED = {
    "zexdoc.com": "5cfd53161cd70d5a907b4e6fae3ed870dcfd103f",
    "zexall.com": "fa0e6ff0854197656e31de9af06c76a7877978b8",
}
BASE = "https://raw.githubusercontent.com/agn453/ZEXALL/main/"
DEST = Path(__file__).resolve().parents[1] / "tests" / "external"


def git_blob_hash(data: bytes) -> str:
    return sha1(f"blob {len(data)}\0".encode("ascii") + data).hexdigest()


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    for filename, expected in EXPECTED.items():
        path = DEST / filename
        if path.is_file() and git_blob_hash(path.read_bytes()) == expected:
            print(f"Verified existing {path} ({path.stat().st_size} bytes)")
            continue
        print(f"Fetching {filename} from agn453/ZEXALL (GPL-2.0)...", flush=True)
        req = Request(BASE + filename, headers={"User-Agent": "z80emu-conformance/0.3.2"})
        with urlopen(req, timeout=30) as response:
            data = response.read()
        actual = git_blob_hash(data)
        if actual != expected:
            raise RuntimeError(f"Unexpected content for {filename}: blob SHA1 {actual}, expected {expected}")
        path.write_bytes(data)
        print(f"Verified {path} ({len(data)} bytes, Git blob SHA1 {actual})")
    print("Ready: run ./build/z80zex tests/external/zexdoc.com")

if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        sys.exit(f"Could not retrieve and verify test files: {error}")
