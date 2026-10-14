"""Synthetic export tests for the crash-fix build gate (no game data)."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools/translate"
sys.path.insert(0, str(TOOLS))
from required_patches import validate_required_patches


class RequiredPatchTests(unittest.TestCase):
    def setUp(self):
        self.configs = [json.loads((TOOLS / f).read_text()) for f in
                        ["golf.slotguard.json", "golf.femaleheads.json"]]
        self.patches = {"FUN_00462020": sum([c["patches"]["FUN_00462020"] for c in self.configs], [])}
        self.required = {"FUN_00462020": sum([c["required_patch_counts"]["FUN_00462020"] for c in self.configs], [])}
        self.code = "void FUN_00462020(void) {\n" + "\n".join(old + ";" for old, count in self.required["FUN_00462020"] for _ in range(count)) + "\n}"
        self.fn = {"name": "FUN_00462020", "addr": "00462020", "sig": "void FUN_00462020(void)", "c": self.code}

    def test_exact_seven_rules_and_eight_sites(self):
        validate_required_patches([self.fn], self.required, self.patches)

    def test_each_missing_anchor_is_rejected(self):
        for old, _ in self.required["FUN_00462020"]:
            with self.subTest(anchor=old), self.assertRaisesRegex(ValueError, "found 0"):
                validate_required_patches([dict(self.fn, c=self.code.replace(old, "changed"))], self.required, self.patches)

    def test_duplicate_rejected(self):
        old = self.required["FUN_00462020"][0][0]
        with self.assertRaisesRegex(ValueError, "found 3"):
            validate_required_patches([dict(self.fn, c=self.code + old)], self.required, self.patches)

    def test_missing_function_and_decompile_rejected(self):
        for functions in [[], [{"name": "FUN_00462020"}]]:
            with self.assertRaisesRegex(ValueError, "body missing"):
                validate_required_patches(functions, self.required, self.patches)

    def test_required_anchor_needs_actual_replacement(self):
        with self.assertRaisesRegex(ValueError, "no replacement"):
            validate_required_patches([self.fn], self.required, {})

    def test_cli_loads_sidecar_gate_before_writing_c(self):
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as temporary:
            root = Path(temporary)
            export = root / "export"
            export.mkdir()
            (export / "functions.jsonl").write_text(json.dumps(dict(self.fn, c="void FUN_00462020(void) {}")) + "\n")
            (export / "types.h").write_text("")
            cfg = root / "golf.json"
            cfg.write_text("{}")
            for n, config in enumerate(self.configs):
                (root / f"golf.{n}.json").write_text(json.dumps(config))
            result = subprocess.run([sys.executable, str(TOOLS / "translate.py"), "--export", str(export),
                "--module", "golf", "--range", "0x462020-0x462021", "--out", str(root / "output"), "--config", str(cfg)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("required translator patches failed", result.stderr)
            self.assertEqual(list((root / "output").glob("*.c")), [])
            # Now give the real translator all seven anchors and check its output.
            (export / "functions.jsonl").write_text(json.dumps(self.fn) + "\n")
            for bypass in [{"native": {"00462020": "native_portrait"}}, {"skip": ["00462020"]}]:
                with self.subTest(bypass=bypass):
                    cfg.write_text(json.dumps(bypass))
                    result = subprocess.run([sys.executable, str(TOOLS / "translate.py"), "--export", str(export),
                        "--module", "golf", "--range", "0x462020-0x462021", "--out", str(root / "output"), "--config", str(cfg)], capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("required function body missing", result.stderr)
                    self.assertEqual(list((root / "output").glob("*.c")), [])
            cfg.write_text("{}")
            result = subprocess.run([sys.executable, str(TOOLS / "translate.py"), "--export", str(export),
                "--module", "golf", "--range", "0x462020-0x462021", "--out", str(root / "output"), "--config", str(cfg)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            generated = "\n".join(f.read_text() for f in (root / "output").glob("golf_0*.c"))
            for _, replacement in self.patches["FUN_00462020"]:
                self.assertIn(replacement, generated)


if __name__ == "__main__":
    unittest.main()
