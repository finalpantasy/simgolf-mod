"""Missing or changed queue anchors must stop translation, not drop the fix."""
import json
import sys
import unittest
from pathlib import Path
TOOLS = Path(__file__).resolve().parents[1] / 'tools/translate'
sys.path.insert(0, str(TOOLS))
from required_patches import validate_required_patches

class PathQueuePatchTests(unittest.TestCase):
    def test_required_anchors(self):
        cfg = json.loads((TOOLS / 'golf.path_queue.json').read_text())
        name = 'FUN_0042e7e0'
        anchors = cfg['patches'][name]
        body = '\n'.join(old for old, new in anchors)
        def validate(text):
            validate_required_patches([{'name': name, 'c': text}], cfg['required_patch_counts'], cfg['patches'])
        validate(body)
        for old, new in anchors:
            with self.subTest(anchor=old), self.assertRaises(ValueError):
                validate(body.replace(old, ''))
            with self.subTest(duplicate=old), self.assertRaises(ValueError):
                validate(body + old)
        for old, new in anchors:
            body = body.replace(old, new)
        self.assertNotIn('DAT_005a7404', body)
        self.assertNotIn('DAT_005a8028', body)
        self.assertNotIn('uVar6 != local_34', body)

if __name__ == '__main__':
    unittest.main()
