"""Regressions for ADR jump-table classification; no game files or Python extras needed."""
import ast
from pathlib import Path
from types import SimpleNamespace
import unittest


def classifier(path):
    node = next(n for n in ast.parse(Path(path).read_text()).body
                if isinstance(n, ast.FunctionDef) and n.name == 'adr_feeds_br')
    scope = {}
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(path), 'exec'), scope)
    return scope['adr_feeds_br']


class JumpTables(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.classify = staticmethod(classifier(Path(__file__).resolve().parents[1] / 'tools/gen.py'))

    def scan(self, words, rd=10):
        return self.classify(SimpleNamespace(words=words), 0, rd)

    def test_scheduled_branch_seven_instructions_after_adr(self):
        # ADR x10; LDRH w11,[x9,x8,lsl #1]; ADD x10,x10,x11,lsl #2;
        # four independent MOVs; BR x10. The old six-instruction window missed it.
        words = [0x1000010a, 0x7868792b, 0x8b0b094a,
                 0x2a0203f6, 0x2a0103f5, 0xaa0003f4, 0x2a0103f9, 0xd61f0140]
        self.assertTrue(self.scan(words))

    def test_fp_register_does_not_overwrite_integer_register(self):
        # FMOV s10,w0 writes v10, leaving x10 intact; the branch is beyond 16 slots.
        self.assertTrue(self.scan([0x1000010a] + [0x1e27000a] * 17 + [0xd61f0140]))

    def test_overwritten_address_is_not_a_table(self):
        self.assertFalse(self.scan([0x1000010a, 0xd280000a, 0xd61f0140]))

    def test_register_copy_through_add(self):
        self.assertTrue(self.scan([0x1000010a, 0x8b0b094c, 0xd61f0180]))

    def test_control_transfer_ends_scan(self):
        for branch in [0x14000004, 0x94000004, 0xd63f0000, 0xd65f03c0]:
            with self.subTest(branch=hex(branch)):
                self.assertFalse(self.scan([0x1000010a, branch, 0xd61f0140]))

    def test_branch_to_other_register_is_not_a_table(self):
        self.assertFalse(self.scan([0x1000010a, 0xd61f0160]))

    def test_bounded_window(self):
        self.assertTrue(self.scan([0x1000010a] + [0xd503201f] * 63 + [0xd61f0140]))
        self.assertFalse(self.scan([0x1000010a] + [0xd503201f] * 64 + [0xd61f0140]))


if __name__ == '__main__':
    unittest.main()
