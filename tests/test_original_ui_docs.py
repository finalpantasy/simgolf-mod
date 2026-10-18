"""Keep the novice original-UI recipe complete and identical in both READMEs."""
import configparser
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


def original_ui_section(path):
    text = path.read_text(encoding="utf-8")
    return text.split("## Use the original UI\n", 1)[1].split("\n## ", 1)[0]


class OriginalUIDocsTests(unittest.TestCase):
    def test_release_recipe_matches_main(self):
        self.assertEqual(original_ui_section(ROOT / "README.md"),
                         original_ui_section(ROOT / "tools/release/public/README.md"))

    def test_recipe_covers_independent_layers_and_defaults(self):
        section = original_ui_section(ROOT / "README.md")
        block = re.search(r"```ini\n(.*?)\n```", section, re.S).group(1)
        config = configparser.ConfigParser(strict=True)
        config.read_string(block)
        original = (
            "font hud golfers faces portraits portrait_preview frontend logo "
            "map_badges map_globe map_icons dialogs reports options loading "
            "paths aim shot_readout ball ball_trail"
        ).split()
        for key in original:
            self.assertEqual(config["ui"][key], "original", key)
        for key in ("hd_legacy_text", "original_rotate_button", "speed_button", "smooth_loading"):
            self.assertEqual(config["ui"][key], "0", key)
        for key in ("hud_show", "original_bottom"):
            self.assertEqual(config["ui"][key], "1", key)
        self.assertEqual(config["art"]["ui"], "original")
        # The native options parser does not trim key/value whitespace.
        for line in block.splitlines():
            if "=" in line:
                key, value = line.split("=", 1)
                self.assertEqual(key, key.strip())
                self.assertEqual(value, value.strip())

    def test_recipe_explains_precedence_and_recovery(self):
        section = original_ui_section(ROOT / "README.md")
        for phrase in ("close SimGolf Remastered completely", "Open File Location",
                       "options.ini.txt", "options-before-original-ui.ini",
                       "read after `mods.ini`", "SIMGOLF_OPTIONS_FILE",
                       "To get the new UI back", "not an unmodified 2002 game"):
            self.assertIn(phrase, section)


if __name__ == "__main__":
    unittest.main()
