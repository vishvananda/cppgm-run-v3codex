#!/usr/bin/env python3

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "scripts" / "run_ab_compile_benchmark.py"


FAKE_COMPILER = """\
#!/usr/bin/env python3
import pathlib
import sys

output = pathlib.Path(sys.argv[sys.argv.index("-o") + 1])
mode = pathlib.Path(sys.argv[0]).name
if "vary" in mode:
    payload = output.name.encode("ascii")
elif "candidate" in mode:
    payload = b"candidate"
else:
    payload = b"baseline"
output.write_bytes(payload)
"""


class BenchmarkHarnessTest(unittest.TestCase):
    def make_compiler(self, root, name):
        path = root / name
        path.write_text(textwrap.dedent(FAKE_COMPILER), encoding="utf-8")
        path.chmod(0o755)
        return path

    def run_harness(self, root, compiler_a, compiler_b, output_mode="exact"):
        source = root / "input.cpp"
        source.write_text("int main() {}\n", encoding="utf-8")
        prefix = root / "report"
        proc = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                "--repo-root", str(root),
                "--compiler-a", str(compiler_a),
                "--compiler-b", str(compiler_b),
                "--source", str(source),
                "--abba-blocks", "1",
                "--output-prefix", str(prefix),
                "--output-mode", output_mode,
            ],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        report = json.loads(prefix.with_suffix(".json").read_text(encoding="utf-8"))
        return proc, report

    def test_aa_exact_records_four_runs_and_paired_metrics(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler = self.make_compiler(root, "baseline-compiler")
            proc, report = self.run_harness(root, compiler, compiler)
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual([run["label"] for run in report["runs"]],
                             ["a", "b", "b", "a"])
            self.assertEqual(len(report["runs"]), 4)
            self.assertEqual(report["validation_errors"], [])
            self.assertEqual(
                len(report["summary"]["paired_candidate_over_baseline"]
                    ["wall_seconds"]["ratios"]),
                1,
            )

    def test_exact_mode_rejects_different_candidate_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = self.make_compiler(root, "baseline-compiler")
            candidate = self.make_compiler(root, "candidate-compiler")
            proc, report = self.run_harness(root, baseline, candidate)
            self.assertEqual(proc.returncode, 3)
            self.assertIn("baseline and candidate output bytes differ",
                          report["validation_errors"])

    def test_deterministic_mode_allows_stable_per_compiler_difference(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            baseline = self.make_compiler(root, "baseline-compiler")
            candidate = self.make_compiler(root, "candidate-compiler")
            proc, report = self.run_harness(
                root, baseline, candidate, output_mode="deterministic"
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(report["validation_errors"], [])

    def test_deterministic_mode_rejects_varying_output(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler = self.make_compiler(root, "vary-compiler")
            proc, report = self.run_harness(
                root, compiler, compiler, output_mode="deterministic"
            )
            self.assertEqual(proc.returncode, 3)
            self.assertIn("a output is not deterministic", report["validation_errors"])
            self.assertIn("b output is not deterministic", report["validation_errors"])

    def test_load_screen_failure_writes_partial_report(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            compiler = self.make_compiler(root, "baseline-compiler")
            source = root / "input.cpp"
            source.write_text("int main() {}\n", encoding="utf-8")
            prefix = root / "screened"
            proc = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    "--repo-root", str(root),
                    "--compiler-a", str(compiler),
                    "--compiler-b", str(compiler),
                    "--source", str(source),
                    "--abba-blocks", "1",
                    "--output-prefix", str(prefix),
                    "--max-load1", "-1",
                    "--screen-timeout-sec", "0",
                ],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            self.assertEqual(proc.returncode, 2)
            report = json.loads(
                prefix.with_suffix(".json").read_text(encoding="utf-8")
            )
            self.assertEqual(report["status"], "screen-error")
            self.assertEqual(report["runs"], [])


if __name__ == "__main__":
    unittest.main()
