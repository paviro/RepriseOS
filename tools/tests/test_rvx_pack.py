# SPDX-License-Identifier: GPL-3.0-only

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "rvx-sdk"))
import manifest
import rvx

MANIFEST = """id = "probe"
name = "Probe"
description = "d"
version = "1"
author = "a"
license = "GPL-3.0-only"
category = "other"
api = "{api}"

[[module]]
name = "main"
trigger = "always"
"""


class PackTests(unittest.TestCase):
    def pack(self, api):
        with tempfile.TemporaryDirectory() as directory:
            toml = Path(directory) / "probe" / "manifest.toml"
            toml.parent.mkdir()
            toml.write_text(MANIFEST.format(api=api))
            rvx.pack(
                "", Path(directory) / "probe.rvx", toml, [], manifest.LAYOUT_API, None
            )

    def test_refuses_a_manifest_below_the_layout_floor(self):
        with self.assertRaisesRegex(ValueError, 'set api = "1.92"'):
            self.pack("1.89")

    def test_accepts_a_manifest_at_the_layout_floor(self):
        # Past the API checks the packer wants an ELF per module.
        with self.assertRaisesRegex(
            ValueError, "the manifest's modules are main, in that order"
        ):
            self.pack("1.92")


if __name__ == "__main__":
    unittest.main()
