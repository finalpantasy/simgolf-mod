"""Both original Hole Stats flushes must use the no-present frame hook."""
import json
from pathlib import Path
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools/translate"
sys.path.insert(0, str(TOOLS))
from required_patches import validate_required_patches


class HoleReportPatchTests(unittest.TestCase):
    def setUp(self):
        self.config = json.loads((TOOLS / "golf.reports.json").read_text())
        self.name = "FUN_00453330"
        self.anchor = "FUN_00480c80(&DAT_00519a60);"
        self.required = {self.name: self.config["required_patch_counts"][self.name]}
        self.body = {
            "name": self.name,
            "c": "void f(void) { " + self.anchor + " " + self.anchor + " }",
        }

    def translated_body(self, body=None, patches=None):
        body = self.body if body is None else body
        patches = self.config["patches"] if patches is None else patches
        validate_required_patches([body], self.required, patches)
        text = body["c"]
        for before, after in patches[self.name]:
            text = text.replace(before, after)
        return text

    def test_both_flushes_use_one_preflush_hook_each(self):
        text = self.translated_body()
        expected = "rp_frame_preflush();\n    " + self.anchor
        self.assertEqual(text.count(expected), 2)
        self.assertEqual(text.count("rp_frame_preflush();"), 2)
        self.assertNotIn("rp_frame();", text)

    def test_native_preflush_skips_only_its_present(self):
        source = (Path(__file__).resolve().parents[1] / "src/port/reports_game.c").read_text()
        self.assertIn("void rp_frame(void) { rp_frame_run(1); }", source)
        self.assertIn("void rp_frame_preflush(void) { rp_frame_run(0); }", source)
        self.assertIn("static void rp_frame_body(int present) {", source)
        self.assertIn(
            "if (present && g_dirty && g_kind != RP_PROFILE && g_kind != RP_SKILLS && g_kind != RP_PHOTO)",
            source,
        )

    def test_changed_or_duplicate_flush_blocks_translation(self):
        for count in (0, 1, 3):
            body = dict(self.body, c="void f(void) { " + self.anchor * count + " }")
            with self.subTest(count=count), self.assertRaises(ValueError):
                self.translated_body(body)

    def test_missing_function_or_required_hook_blocks_translation(self):
        with self.assertRaises(ValueError):
            validate_required_patches([], self.required, self.config["patches"])
        patches = dict(self.config["patches"])
        patches[self.name] = [
            patch for patch in patches[self.name]
            if patch[0] != self.anchor
        ]
        with self.assertRaises(ValueError):
            validate_required_patches([self.body], self.required, patches)


if __name__ == "__main__":
    unittest.main()
