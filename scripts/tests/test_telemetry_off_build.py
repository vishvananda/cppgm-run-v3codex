#!/usr/bin/env python3

import pathlib
import subprocess
import sys
import tempfile


SOURCE = r"""
template<class T> struct base { constexpr int value() const { return 7; } };
template<class T> struct derived : base<T> {
  int read() const { return this->value(); }
};
int main() { return derived<int>().read() == 7 ? 0 : 1; }
"""


def run(command):
    return subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False
    )


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: test_telemetry_off_build.py NORMAL TELEMETRY_OFF")

    normal = pathlib.Path(sys.argv[1]).resolve()
    telemetry_off = pathlib.Path(sys.argv[2]).resolve()

    with tempfile.TemporaryDirectory(prefix="cppgm-telemetry-off-") as temporary:
        work = pathlib.Path(temporary)
        source = work / "input.cpp"
        source.write_text(SOURCE)
        outputs = []
        for index, compiler in enumerate((normal, telemetry_off)):
            output = work / ("output-%d.o" % index)
            result = run([str(compiler), "-O1", "-c", str(source), "-o", str(output)])
            if result.returncode != 0:
                sys.stderr.buffer.write(result.stderr)
                raise SystemExit("compiler failed: %s" % compiler)
            outputs.append(output.read_bytes())
        if outputs[0] != outputs[1]:
            raise SystemExit("telemetry-off compiler changed object output")

        for option in ("--stats", "--stats-functions"):
            result = run(
                [str(telemetry_off), option, "-O1", "-c", str(source),
                 "-o", str(work / "rejected.o")]
            )
            if result.returncode == 0:
                raise SystemExit("telemetry-off compiler accepted %s" % option)
            if b"statistics are unavailable" not in result.stderr:
                raise SystemExit("telemetry-off rejection was not explicit")

    print("telemetry-off build contract: passed")


if __name__ == "__main__":
    main()
