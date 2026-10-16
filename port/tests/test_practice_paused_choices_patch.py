"""Paused practice shot choices must retain the original five hitboxes."""
import json
import re
import sys
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools/translate"
sys.path.insert(0, str(TOOLS))
from required_patches import validate_required_patches


class PracticePausedChoicesPatchTests(unittest.TestCase):
    def setUp(self):
        self.config = json.loads((TOOLS / "golf.placement_practice.json").read_text())
        self.name = "FUN_00435f00"
        self.old, self.new = self.config["patches"][self.name][0]
        self.required = {self.name: self.config["required_patch_counts"][self.name]}
        self.body = {
            "name": self.name,
            "c": """void f(void) {
  if (DAT_00567afc == 3) {
    iVar1 = param_2 + -0x1f9;
    if (FUN_00467170(param_1 + -0x19e, iVar1) < 0x14) uVar4 = 4;
    if (FUN_00467170(param_1 + -0x1ed, iVar1) < 0x14) uVar4 = 5;
    if (FUN_00467170(param_1 + -0x23c, iVar1) < 0x14) uVar4 = 6;
    if (FUN_00467170(param_1 + -0x28b, iVar1) < 0x14) uVar4 = 7;
    if (FUN_00467170(param_1 + -0x2da, iVar1) < 0x14) uVar4 = 8;
  }
}""",
        }

    def translated_body(self, body=None, patches=None):
        body = self.body if body is None else body
        patches = self.config["patches"] if patches is None else patches
        validate_required_patches([body], self.required, patches)
        text = body["c"]
        for before, after in patches[self.name]:
            text = text.replace(before, after)
        return text

    def test_paused_mode_is_added_without_changing_hitboxes(self):
        translated = self.translated_body()
        self.assertEqual(translated, self.body["c"].replace(self.old, self.new))
        self.assertEqual(translated.count("FUN_00467170("), 5)
        self.assertEqual(translated.count("< 0x14) uVar4 ="), 5)
        self.assertIn(self.new, translated)

    def test_changed_missing_or_duplicate_guard_blocks_translation(self):
        for code in (
            self.body["c"].replace(self.old, "if (DAT_00567afc == 4) {"),
            self.body["c"].replace(self.old, ""),
            self.body["c"] + "\n" + self.old,
        ):
            with self.subTest(code=code), self.assertRaises(ValueError):
                self.translated_body(dict(self.body, c=code))

    def test_actual_guard_truth_matrix_preserves_original_hud_fallback(self):
        translated = self.translated_body()
        match = re.fullmatch(
            r"if \(\(DAT_00567afc == (?P<mode3>\d+)\) \|\| "
            r"\(\(DAT_00567afc == (?P<mode4>\d+)\) && hud_on\(\)\)\) \{",
            self.new,
        )
        self.assertIsNotNone(match, self.new)
        mode3 = int(match.group("mode3"))
        mode4 = int(match.group("mode4"))
        self.assertIn(self.new, translated)

        cases = (
            ("other page, original HUD", 2, False, False),
            ("other page, Frosted Mint HUD", 2, True, False),
            ("mode-3 pro, original HUD", 3, False, True),
            ("mode-3 pro, Frosted Mint HUD", 3, True, True),
            ("paused mode-4 pro, original HUD", 4, False, False),
            ("paused mode-4 pro, Frosted Mint HUD", 4, True, True),
        )
        for label, mode, hud_enabled, expected in cases:
            actual = mode == mode3 or (mode == mode4 and hud_enabled)
            with self.subTest(label=label):
                self.assertEqual(actual, expected)

    def test_required_anchor_stays_original_mode3_guard_once(self):
        self.assertEqual(self.old, "if (DAT_00567afc == 3) {")
        self.assertEqual(self.config["required_patch_counts"][self.name], [[self.old, 1]])
        self.assertEqual(self.translated_body().count(self.new), 1)

    def test_missing_required_replacement_blocks_translation(self):
        with self.assertRaises(ValueError):
            validate_required_patches([self.body], self.required, {})


if __name__ == "__main__":
    unittest.main()
