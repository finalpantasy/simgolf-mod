"""Theme discovery must scan the first real entry, with an exact build gate."""
import json
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools/translate"
sys.path.insert(0, str(TOOLS))
from required_patches import validate_required_patches


class ThemePatchTests(unittest.TestCase):
    def setUp(self):
        self.config = json.loads((TOOLS / "golf.frontend.json").read_text())
        self.name = "FUN_004724c0"
        self.old, self.new = self.config["patches"][self.name][0]
        self.required = {self.name: self.config["required_patch_counts"][self.name]}
        self.body = {"name": self.name, "c": "void " + self.name + "(void) { " + self.old + " }"}

    def test_starts_at_first_100_byte_slot(self):
        self.assertEqual(self.old, "a0 = &DAT_0080b194;")
        self.assertEqual(self.new, "a0 = (byte *)&DAT_0080b130;")
        self.assertEqual(0x80B194 - 0x80B130, 100)
        validate_required_patches([self.body], self.required, self.config["patches"])

    def test_changed_or_duplicate_anchor_blocks_build(self):
        for code in ["", self.body["c"] + self.old]:
            with self.subTest(code=code), self.assertRaises(ValueError):
                validate_required_patches([dict(self.body, c=code)], self.required, self.config["patches"])

    def test_missing_function_or_replacement_blocks_build(self):
        with self.assertRaises(ValueError):
            validate_required_patches([], self.required, self.config["patches"])
        with self.assertRaises(ValueError):
            validate_required_patches([self.body], self.required, {})


if __name__ == "__main__":
    unittest.main()
