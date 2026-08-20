#!/usr/bin/env python3

from pathlib import Path
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
COMPARE = REPO_ROOT / "pa29" / "scripts" / "compare_results.pl"


class Pa29MirModeTest(unittest.TestCase):
    def write_success_case(self, root, ref_mir, my_mir):
        test = root / "case.t"
        test.write_text("function @main() -> i32 {\n}\n", encoding="utf-8")
        for suffix in ("ref", "my"):
            (root / f"case.{suffix}.impl.exit_status").write_text(
                "0\n", encoding="utf-8"
            )
            (root / f"case.{suffix}.program.exit_status").write_text(
                "0\n", encoding="utf-8"
            )
            (root / f"case.{suffix}.program.stdout").write_text(
                "ok\n", encoding="utf-8"
            )
        (root / "case.ref.mir").write_text(ref_mir, encoding="utf-8")
        (root / "case.my.mir").write_text(my_mir, encoding="utf-8")
        return test

    def compare(self, test):
        return subprocess.run(
            ["perl", str(COMPARE), "ref", "my", str(test)],
            cwd=REPO_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

    def test_behavior_retains_but_does_not_compare_reference_mir(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "behavior"
            root.mkdir()
            test = self.write_success_case(root, "reference mir\n", "student mir\n")
            result = self.compare(test)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_behavior_requires_informational_reference_mir(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "behavior"
            root.mkdir()
            test = self.write_success_case(root, "reference mir\n", "student mir\n")
            (root / "case.ref.mir").unlink()
            result = self.compare(test)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("missing informational reference machine IR", result.stdout)

    def test_strict_still_compares_reference_mir(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "strict"
            root.mkdir()
            test = self.write_success_case(root, "reference mir\n", "student mir\n")
            result = self.compare(test)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("machine IR dumps do not match", result.stdout)


if __name__ == "__main__":
    unittest.main()
