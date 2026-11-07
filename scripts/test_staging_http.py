#!/usr/bin/env python3
"""Verify vital Z80/LAB private-stage headers and MIME types on loopback."""
from urllib.request import Request, urlopen
import argparse
import sys

FILES = {
    "/index.html": "text/html",
    "/z80wasm.mjs": "javascript",
    "/z80wasm.wasm": "application/wasm",
    "/worker.js": "javascript",
    "/assembler.js": "javascript",
}


def validate(base):
    if not (base.startswith("http://127.0.0.1:") or
            base.startswith("http://localhost:")):
        raise ValueError("Checker supports loopback HTTP staging only")
    for path, mime in FILES.items():
        with urlopen(Request(base.rstrip("/") + path, method="HEAD"), timeout=5) as response:
            headers = response.headers
            if response.status != 200:
                raise ValueError(f"{path}: HTTP {response.status}")
            content_type = headers.get_content_type()
            if mime not in content_type:
                raise ValueError(f"{path}: unexpected MIME type {content_type}")
            if "no-store" not in headers.get("Cache-Control", ""):
                raise ValueError(f"{path}: missing no-store cache policy")
            if headers.get("X-Content-Type-Options") != "nosniff":
                raise ValueError(f"{path}: missing nosniff header")
            if headers.get("X-Frame-Options") != "DENY":
                raise ValueError(f"{path}: missing anti-framing header")
            policy = headers.get("Content-Security-Policy", "")
            for rule in ("script-src 'self' 'wasm-unsafe-eval'",
                         "worker-src 'self'", "object-src 'none'",
                         "frame-ancestors 'none'"):
                if rule not in policy:
                    raise ValueError(f"{path}: missing CSP rule {rule}")
            print(f"PASS {path}: HTTP 200 {content_type}; CSP/no-store verified")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", default="http://127.0.0.1:8082")
    args = parser.parse_args()
    try:
        validate(args.base)
    except (ValueError, OSError) as error:
        sys.exit(f"FAIL: {error}")
