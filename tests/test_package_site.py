"""Release packaging: strict asset allowlist and integrity/determinism checks."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from scripts.package_site import ASSETS, create_package


class PackageSiteTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.site = self.base / "site"
        self.site.mkdir()
        for name in ASSETS:
            path = self.site / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"\x00asm\x01\x00\x00\x00" if name == "z80wasm.wasm" else b"sample file\n")

    def make(self, name="site.zip"):
        return create_package(self.site, self.base / name, revision="a" * 40, version="1.0.1")

    def test_archive_contains_only_allowlisted_site_files_and_manifest(self):
        (self.site / "secret.txt").write_text("DO NOT SHIP")
        (self.site / "node_modules").mkdir()
        (self.site / "node_modules" / "internal.js").write_text("DO NOT SHIP")
        package, checksum = self.make()
        with zipfile.ZipFile(package) as archive:
            self.assertEqual(set(archive.namelist()), set(ASSETS) | {"site-manifest.json"})
            self.assertIsNone(archive.testzip())
            manifest = json.loads(archive.read("site-manifest.json"))
            self.assertEqual(manifest["version"], "1.0.1")
            self.assertEqual(manifest["revision"], "a" * 40)
            for name in ASSETS:
                data = archive.read(name)
                self.assertEqual(manifest["files"][name]["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(checksum.read_text().split()[0], hashlib.sha256(package.read_bytes()).hexdigest())

    def test_identical_input_produces_identical_archive(self):
        first, _ = self.make("one.zip")
        second, _ = self.make("two.zip")
        self.assertEqual(first.read_bytes(), second.read_bytes())

    def test_missing_or_invalid_wasm_fails(self):
        wasm = self.site / "z80wasm.wasm"
        wasm.unlink()
        with self.assertRaisesRegex(ValueError, "Missing"):
            self.make()
        wasm.write_bytes(b"not a WASM module")
        with self.assertRaisesRegex(ValueError, "WebAssembly binary"):
            self.make()

    def test_invalid_revision_and_symlink_fail_closed(self):
        with self.assertRaisesRegex(ValueError, "Revision"):
            create_package(self.site, self.base / "site.zip", revision="../../bad", version="1.0.1")
        index = self.site / "index.html"
        index.unlink()
        index.symlink_to(self.site / "app.js")
        with self.assertRaisesRegex(ValueError, "symlinked"):
            self.make()


if __name__ == "__main__":
    unittest.main()
