"""Release metadata must be consistent before publishing a site artifact."""
import json
from html.parser import HTMLParser
from pathlib import Path
import unittest

from scripts.package_site import ASSETS, project_version

ROOT = Path(__file__).resolve().parents[1]
RELEASE_VERSION = "1.0.1"


class Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.hrefs = []

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            self.hrefs.extend(value for key, value in attrs if key == "href")


class MetadataTests(unittest.TestCase):
    def test_release_versions_agree(self):
        package = json.loads((ROOT / "package.json").read_text())
        lock = json.loads((ROOT / "package-lock.json").read_text())
        html = (ROOT / "web/index.html").read_text()
        guide = (ROOT / "web/help.html").read_text()
        self.assertEqual(project_version(), RELEASE_VERSION)
        self.assertEqual(package["version"], RELEASE_VERSION)
        self.assertEqual(lock["version"], RELEASE_VERSION)
        self.assertEqual(lock["packages"][""]["version"], RELEASE_VERSION)
        self.assertIn("Z80/LAB v1.0.1", html)
        self.assertIn("Z80/LAB v1.0.1", guide)
        self.assertNotIn("v0.9.0 development", html)

    def test_public_help_is_linked_and_packaged(self):
        self.assertIn("help.html", ASSETS)
        links = Links()
        links.feed((ROOT / "web/index.html").read_text())
        self.assertIn("help.html", links.hrefs)
        for name in ("LICENSE", "THIRD_PARTY_LICENSES.txt", "EMSCRIPTEN_LICENSE.txt"):
            self.assertIn(name, ASSETS)
        guide = (ROOT / "web/help.html").read_text()
        self.assertIn('href="index.html"', guide)
        for name in ("LICENSE", "THIRD_PARTY_LICENSES.txt", "EMSCRIPTEN_LICENSE.txt"):
            self.assertIn(f'href="{name}"', guide)
        footer = (ROOT / "web/index.html").read_text()
        for name in ("LICENSE", "THIRD_PARTY_LICENSES.txt", "EMSCRIPTEN_LICENSE.txt"):
            self.assertIn(f'href="{name}"', footer)

    def test_licence_notices_are_present_and_complete(self):
        own = (ROOT / "LICENSE").read_text()
        third = (ROOT / "THIRD_PARTY_LICENSES.txt").read_text()
        emscripten = (ROOT / "EMSCRIPTEN_LICENSE.txt").read_text()
        self.assertIn("MIT License", own)
        self.assertIn("Copyright (c) 2023 Martin Maly", third)
        self.assertIn("Emscripten 4.0.23", third)
        self.assertIn("University of Illinois/NCSA", emscripten)
        self.assertIn("Emscripten authors", emscripten)
        guide = (ROOT / "web/help.html").read_text()
        self.assertIn('not affiliated with', guide)


if __name__ == "__main__":
    unittest.main()
