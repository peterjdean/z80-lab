#!/usr/bin/env python3
"""Verify Z80/LAB release artifacts and atomically select a static site release.

Run as the deployment administrator. No Apache or DNS configuration is altered.
Requires package_site.py alongside this script (both live in scripts/).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile
import uuid
import zipfile

try:
    from .package_site import ASSETS, MAX_ASSET_BYTES
except ImportError:
    from package_site import ASSETS, MAX_ASSET_BYTES

FILES = frozenset(ASSETS) | {"site-manifest.json"}
REVISION = re.compile(r"[0-9a-f]{40}")
RELEASE = re.compile(r"releases/rev-[0-9a-f]{12}")
MAX_ARCHIVE_BYTES = 60 * 1024 * 1024


def check_archive(archive_path, checksum_path):
    archive_path = Path(archive_path)
    checksum_path = Path(checksum_path)
    if archive_path.stat().st_size > MAX_ARCHIVE_BYTES:
        raise ValueError("Site archive is oversized")
    checksum_lines = checksum_path.read_text(encoding="ascii").splitlines()
    if len(checksum_lines) != 1:
        raise ValueError("Expected one SHA256 checksum line")
    match = re.fullmatch(r"([0-9a-fA-F]{64})  ([A-Za-z0-9._-]+)", checksum_lines[0])
    if not match or match.group(2) != archive_path.name:
        raise ValueError("Checksum entry does not match the archive filename")
    actual = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    if actual != match.group(1).lower():
        raise ValueError("Archive SHA256 checksum mismatch")

    with zipfile.ZipFile(archive_path) as archive:
        infos = archive.infolist()
        if len(infos) != len(FILES) or {entry.filename for entry in infos} != FILES:
            raise ValueError("Release archive has missing, duplicate or unapproved files")
        payloads = {}
        for entry in infos:
            mode = entry.external_attr >> 16
            if (entry.is_dir() or entry.file_size < 1 or
                    entry.file_size > MAX_ASSET_BYTES or
                    (mode and stat.S_ISLNK(mode))):
                raise ValueError("Release archive has an invalid file entry")
            payloads[entry.filename] = archive.read(entry)
    if not payloads["z80wasm.wasm"].startswith(b"\x00asm\x01\x00\x00\x00"):
        raise ValueError("Invalid WebAssembly module")
    if len(payloads["site-manifest.json"]) > 200_000:
        raise ValueError("Release manifest is oversized")
    manifest = json.loads(payloads["site-manifest.json"])
    if (not isinstance(manifest, dict) or
            manifest.get("format") != "z80-lab-static-site" or
            not REVISION.fullmatch(str(manifest.get("revision", ""))) or
            not re.fullmatch(r"\d+\.\d+\.\d+", str(manifest.get("version", ""))) or
            not isinstance(manifest.get("files"), dict) or
            set(manifest["files"]) != set(ASSETS)):
        raise ValueError("Invalid site manifest")
    for name in ASSETS:
        entry = manifest["files"][name]
        data = payloads[name]
        if (not isinstance(entry, dict) or entry.get("bytes") != len(data) or
                entry.get("sha256") != hashlib.sha256(data).hexdigest()):
            raise ValueError("Manifest integrity check failed for " + name)
    return manifest, payloads


def checked_link(root, name):
    link = root / name
    if not link.is_symlink():
        if link.exists():
            raise ValueError(f"{name} is not a managed symlink")
        return None
    target = os.readlink(link)
    if not RELEASE.fullmatch(target) or not (root / target).is_dir():
        raise ValueError(f"{name} points outside managed releases or is missing")
    return target


def set_link(root, name, target):
    if not RELEASE.fullmatch(target):
        raise ValueError("Invalid release symlink target")
    temporary = root / (".link-" + uuid.uuid4().hex)
    try:
        os.symlink(target, temporary)
        os.replace(temporary, root / name)
    finally:
        if temporary.is_symlink():
            temporary.unlink()


def verify_existing(destination, payloads):
    if destination.is_symlink() or not destination.is_dir():
        raise ValueError("Existing release directory is invalid")
    # Reject even an extra directory symlink: Apache follows symlinks here.
    if any(p.is_symlink() for p in destination.rglob("*")):
        raise ValueError("Existing release contains a symlink")
    existing = {str(p.relative_to(destination)) for p in destination.rglob("*") if p.is_file()}
    if existing != FILES:
        raise ValueError("Existing release contains unexpected files")
    for name, data in payloads.items():
        path = destination / name
        if path.is_symlink() or path.read_bytes() != data:
            raise ValueError("Existing release does not match this archive")


def install(root, archive, checksum):
    manifest, payloads = check_archive(archive, checksum)
    root = Path(root).absolute()
    if root.is_symlink():
        raise ValueError("Deployment root must not be a symlink")
    root.mkdir(parents=True, exist_ok=True)
    releases = root / "releases"
    if releases.is_symlink():
        raise ValueError("Releases directory must not be a symlink")
    releases.mkdir(exist_ok=True)
    release_name = "rev-" + manifest["revision"][:12]
    target = "releases/" + release_name
    destination = releases / release_name

    # Refuse to overwrite previously promoted releases. Identical repeat
    # deployments are safe, differing builds under one revision are rejected.
    if destination.exists() or destination.is_symlink():
        verify_existing(destination, payloads)
    else:
        temporary = Path(tempfile.mkdtemp(prefix=".incoming-", dir=releases))
        try:
            for name, data in payloads.items():
                path = temporary / name
                path.parent.mkdir(parents=True, exist_ok=True, mode=0o755)
                with path.open("xb") as handle:
                    handle.write(data)
                path.chmod(0o644)
            temporary.chmod(0o755)
            os.rename(temporary, destination)
        finally:
            if temporary.exists():
                import shutil
                shutil.rmtree(temporary)

    current = checked_link(root, "current")
    previous = checked_link(root, "previous")
    if current == target:
        print(f"Already active: {target} ({manifest['version']})")
        return target
    if current is not None:
        set_link(root, "previous", current)
    set_link(root, "current", target)
    print(f"Active: {target} ({manifest['version']}); rollback: {current or previous or 'none'}")
    return target


def rollback(root):
    root = Path(root).absolute()
    current = checked_link(root, "current")
    previous = checked_link(root, "previous")
    if not current or not previous or current == previous:
        raise ValueError("No distinct previous release is available")
    set_link(root, "previous", current)
    set_link(root, "current", previous)
    print(f"Rolled back: {previous}; previous now {current}")
    return previous


def status(root):
    root = Path(root).absolute()
    current = checked_link(root, "current")
    previous = checked_link(root, "previous")
    print(f"Current: {current or 'none'}")
    print(f"Previous: {previous or 'none'}")
    return current, previous


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, help="Absolute local deployment root")
    commands = parser.add_subparsers(dest="command", required=True)
    add = commands.add_parser("install")
    add.add_argument("--archive", required=True)
    add.add_argument("--checksum", required=True)
    commands.add_parser("status")
    commands.add_parser("rollback")
    args = parser.parse_args()
    try:
        if args.command == "install":
            install(args.root, args.archive, args.checksum)
        elif args.command == "status":
            status(args.root)
        else:
            rollback(args.root)
    except (OSError, ValueError, json.JSONDecodeError, zipfile.BadZipFile) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
