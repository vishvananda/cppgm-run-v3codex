#!/usr/bin/env python3
"""Run a behavioral triangle for curated single- and multi-TU C++ cases."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


SCHEMA = "cppgm-lowir-llvm-behavior-v1"


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def digest(path):
    if not path.is_file():
        return None
    data = path.read_bytes()
    return {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def tool(value, repo):
    candidate = Path(value)
    if not candidate.is_absolute() and (repo / candidate).is_file():
        return str((repo / candidate).resolve())
    found = shutil.which(value)
    if not found:
        raise RuntimeError("tool not found: " + value)
    return str(Path(found).resolve())


def source_groups(repo, list_path):
    result = []
    for number, raw in enumerate(list_path.read_text(encoding="utf-8").splitlines(), 1):
        value = raw.split("#", 1)[0].strip()
        if not value:
            continue
        paths = []
        relatives = []
        for source_value in value.split():
            path = (repo / source_value).resolve()
            try:
                relative = path.relative_to(repo)
            except ValueError:
                raise RuntimeError("source escapes repository at line {}".format(number))
            if not path.is_file():
                raise RuntimeError(
                    "source not found at line {}: {}".format(number, source_value)
                )
            paths.append(path)
            relatives.append(relative)
        result.append((paths, relatives))
    return result


def run_build(command, repo, stdout_path, stderr_path):
    process = subprocess.run(command, cwd=str(repo), stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, check=False)
    stdout_path.write_bytes(process.stdout)
    stderr_path.write_bytes(process.stderr)
    return process.returncode


def run_program(path, stdout_path, stderr_path, timeout):
    try:
        process = subprocess.run([str(path)], stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, check=False,
                                 timeout=timeout)
        stdout_path.write_bytes(process.stdout)
        stderr_path.write_bytes(process.stderr)
        return {"status": process.returncode, "timed_out": False,
                "stdout_sha256": hashlib.sha256(process.stdout).hexdigest(),
                "stderr_sha256": hashlib.sha256(process.stderr).hexdigest()}
    except subprocess.TimeoutExpired as error:
        stdout = error.stdout or b""
        stderr = error.stderr or b""
        stdout_path.write_bytes(stdout)
        stderr_path.write_bytes(stderr)
        return {"status": None, "timed_out": True,
                "stdout_sha256": hashlib.sha256(stdout).hexdigest(),
                "stderr_sha256": hashlib.sha256(stderr).hexdigest()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--source-list", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--cppgm", default="dev/cppgm++")
    parser.add_argument("--clangxx", default="clang++")
    parser.add_argument("--timeout", type=int, default=10)
    arguments = parser.parse_args()

    repo = Path(arguments.repo_root).resolve()
    list_path = Path(arguments.source_list)
    if not list_path.is_absolute():
        list_path = repo / list_path
    output = Path(arguments.output).resolve()
    try:
        output.relative_to(repo)
        raise RuntimeError("behavior output must be outside the repository")
    except ValueError:
        pass
    if output.exists() and any(output.iterdir()):
        raise RuntimeError("behavior output is not empty: " + str(output))
    output.mkdir(parents=True, exist_ok=True)
    cppgm = tool(arguments.cppgm, repo)
    clangxx = tool(arguments.clangxx, repo)

    records = []
    for sources, relatives in source_groups(repo, list_path):
        source_key = "+".join(str(item) for item in relatives)
        case_id = re.sub(r"[^A-Za-z0-9]+", "_", source_key).strip("_")[:120]
        case_id += "__" + hashlib.sha256(source_key.encode("utf-8")).hexdigest()[:10]
        case_dir = output / "cases" / case_id
        case_dir.mkdir(parents=True)
        subject_exe = case_dir / "subject.exe"
        ours_ir = [case_dir / "ours.{}.ll".format(index)
                   for index in range(len(sources))]
        ours_exe = case_dir / "ours.exe"
        clang_exe = case_dir / "clang.exe"
        commands = {
            "subject_build": [cppgm] + [str(item) for item in sources] +
                             ["-o", str(subject_exe)],
            "ours_link": [clangxx, "-x", "ir"] + [str(item) for item in ours_ir] +
                         ["-o", str(ours_exe)],
            "clang_build": [clangxx, "-std=gnu++11", "-stdlib=libstdc++", "-O0",
                            "-g0", "-x", "c++"] + [str(item) for item in sources] +
                           ["-o", str(clang_exe)],
        }
        build_status = {}
        build_status["subject_build"] = run_build(
            commands["subject_build"], repo, case_dir / "subject_build.stdout",
            case_dir / "subject_build.stderr")
        emit_ok = True
        for index, (source, ir_path) in enumerate(zip(sources, ours_ir)):
            name = "ours_emit_{}".format(index)
            commands[name] = [cppgm, "--emit-llvm-ir", "-O0", str(source),
                              "-o", str(ir_path)]
            build_status[name] = run_build(commands[name], repo,
                                           case_dir / (name + ".stdout"),
                                           case_dir / (name + ".stderr"))
            emit_ok = emit_ok and build_status[name] == 0
        if emit_ok:
            build_status["ours_link"] = run_build(commands["ours_link"], repo,
                                                   case_dir / "ours_link.stdout",
                                                   case_dir / "ours_link.stderr")
        else:
            build_status["ours_link"] = None
        build_status["clang_build"] = run_build(
            commands["clang_build"], repo, case_dir / "clang_build.stdout",
            case_dir / "clang_build.stderr")

        runs = {}
        for lane, executable in (("subject", subject_exe), ("ours", ours_exe),
                                 ("clang", clang_exe)):
            if executable.is_file():
                runs[lane] = run_program(executable, case_dir / (lane + ".stdout"),
                                         case_dir / (lane + ".stderr"), arguments.timeout)
        comparable = len(runs) == 3 and not any(v["timed_out"] for v in runs.values())
        behavior_match = comparable and len({
            (value["status"], value["stdout_sha256"], value["stderr_sha256"])
            for value in runs.values()
        }) == 1
        record = {
            "schema": SCHEMA,
            "source": source_key,
            "sources": [
                {"path": str(relative),
                 "sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
                for source, relative in zip(sources, relatives)
            ],
            "build_status": build_status,
            "runs": runs,
            "comparable": comparable,
            "behavior_match": behavior_match,
            "artifacts": {
                "ours_ir": [digest(item) for item in ours_ir],
                "subject_exe": digest(subject_exe),
                "ours_exe": digest(ours_exe),
                "clang_exe": digest(clang_exe),
            },
        }
        write_json(case_dir / "case.json", record)
        records.append(record)

    summary = {
        "schema": SCHEMA,
        "case_count": len(records),
        "comparable": sum(1 for item in records if item["comparable"]),
        "behavior_matches": sum(1 for item in records if item["behavior_match"]),
        "mismatches": [item["source"] for item in records
                       if item["comparable"] and not item["behavior_match"]],
        "incomplete": [item["source"] for item in records if not item["comparable"]],
    }
    write_json(output / "summary.json", summary)
    sys.stdout.write(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    return 0 if summary["behavior_matches"] == summary["case_count"] else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError) as error:
        sys.stderr.write("run_lowir_llvm_behavior.py: {}\n".format(error))
        sys.exit(1)
