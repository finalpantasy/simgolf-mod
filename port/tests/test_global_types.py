"""Run the actual translator on synthetic conflicting global declarations."""
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TRANSLATOR = Path(__file__).resolve().parents[1] / 'tools/translate/translate.py'


class GlobalTypesTests(unittest.TestCase):
    def translate(self, base, extras):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            export = root / 'export'
            export.mkdir()
            funcs = []
            for i, ptr in enumerate(['char (*)', 'undefined1 (*)', 'int (*)']):
                name = f'FUN_0040100{i}'
                funcs.append({'addr': f'0040100{i}', 'name': name,
                              'sig': f'int {name}(void)',
                              'c': f'int {name}(void) {{ return DAT_00834170; }}',
                              'globals': [{'name': 'DAT_00834170', 'addr': '00834170',
                                           'size': 1 if i < 2 else 4, 'ptr': ptr}]})
            (export / 'functions.jsonl').write_text(''.join(json.dumps(f) + '\n' for f in funcs))
            (export / 'types.h').write_text('')
            (root / 'golf.json').write_text(json.dumps(base))
            for name, config in extras.items():
                (root / ('golf.' + name + '.json')).write_text(json.dumps(config))
            result = subprocess.run([sys.executable, str(TRANSLATOR), '--export', str(export),
                                     '--module', 'golf', '--range', '0x401000-0x401003',
                                     '--out', str(root / 'gen'), '--config', str(root / 'golf.json')],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            return (root / 'gen/golf_00.c').read_text()

    def test_no_override_preserves_function_specific_types(self):
        text = self.translate({}, {})
        for ptr in ('char (*)', 'undefined1 (*)', 'int (*)'):
            self.assertIn('#define DAT_00834170 (*(' + ptr + ')0x834170)', text)

    def test_base_override_applies_to_every_function(self):
        text = self.translate({'global_types': {'00834170': 'int (*)', '_note': 'ignored'}}, {})
        self.assertEqual(text.count('#define DAT_00834170 (*(int (*)' + ')0x834170)'), 3)

    def test_sorted_topic_override_beats_base_and_earlier_topic(self):
        text = self.translate({'global_types': {'00834170': 'short (*)'}}, {
            'a': {'global_types': {'00834170': 'char (*)'}},
            'z': {'global_types': {'00834170': 'int (*)'}}})
        self.assertEqual(text.count('#define DAT_00834170 (*(int (*)' + ')0x834170)'), 3)


if __name__ == '__main__':
    unittest.main()
