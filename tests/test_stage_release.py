"""Check release verification, safe promotion and recovery without Apache/root."""
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

from scripts.package_site import ASSETS, create_package
from scripts.stage_release import check_archive, install, rollback, status


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.site = self.base / "site"
        self.site.mkdir()
        for name in ASSETS:
            path = self.site / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"\x00asm\x01\x00\x00\x00" if name == "z80wasm.wasm" else b"test data")
        self.root = self.base / "deploy"

    def package(self, sha, path):
        return create_package(self.site, self.base / path, revision=sha, version="1.0.0")

    def test_verified_install_repeat_and_atomic_rollback(self):
        first, first_sha = self.package("a" * 40, "first.zip")
        m, data = check_archive(first, first_sha)
        self.assertEqual(m["revision"], "a" * 40)
        self.assertEqual(set(data), set(ASSETS) | {"site-manifest.json"})
        install(self.root, first, first_sha)
        self.assertEqual(status(self.root), ("releases/rev-" + "a"*12, None))
        install(self.root, first, first_sha)  # idempotent
        second, second_sha = self.package("b" * 40, "second.zip")
        install(self.root, second, second_sha)
        self.assertEqual(status(self.root), ("releases/rev-" + "b"*12, "releases/rev-"+"a"*12))
        self.assertEqual((self.root / "current" / "app.js").read_bytes(), b"test data")
        self.assertEqual(rollback(self.root), "releases/rev-"+"a"*12)
        self.assertEqual(status(self.root), ("releases/rev-"+"a"*12, "releases/rev-"+"b"*12))

    def test_corrupt_checksum_does_not_change_current(self):
        first, first_sha = self.package("a" * 40, "first.zip")
        install(self.root, first, first_sha)
        second, second_sha = self.package("b" * 40, "second.zip")
        second_sha.write_text("0"*64+"  second.zip\n")
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            install(self.root, second, second_sha)
        self.assertEqual(status(self.root)[0], "releases/rev-"+"a"*12)

    def test_manifest_integrity_and_unapproved_files_rejected(self):
        first, first_sha = self.package("a" * 40, "first.zip")
        # Repack a ZIP with an extra file and matching top-level checksum.
        extra = self.base / "extra.zip"
        with zipfile.ZipFile(first) as src, zipfile.ZipFile(extra, "w") as dst:
            for item in src.infolist():
                dst.writestr(item, src.read(item.filename))
            dst.writestr("private.txt", "not an approved runtime file")
        import hashlib
        checksum = self.base / "extra.sha256"
        checksum.write_text(hashlib.sha256(extra.read_bytes()).hexdigest()+"  extra.zip\n")
        with self.assertRaisesRegex(ValueError, "unapproved"):
            check_archive(extra, checksum)
        self.assertFalse(self.root.exists())

    def test_real_directory_is_not_replaced_by_current_symlink(self):
        archive, checksum = self.package("a" * 40, "first.zip")
        self.root.mkdir()
        (self.root / "current").mkdir()
        with self.assertRaisesRegex(ValueError, "not a managed symlink"):
            install(self.root, archive, checksum)


if __name__ == "__main__":
    unittest.main()
