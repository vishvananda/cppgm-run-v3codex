#!/usr/bin/env python3
"""Run reproducible three-way LowIR/LLVM comparison artifacts."""

import argparse
import fnmatch
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys


SCHEMA = "cppgm-lowir-llvm-sweep-v1"
SUCCESS = "EXIT_SUCCESS"

KNOWN_INSTRUCTIONS = {
    "add", "addrspacecast", "alloca", "and", "ashr", "atomicrmw",
    "bitcast", "br", "call", "callbr", "catchpad", "catchret",
    "catchswitch", "cleanupret", "cleanuppad", "cmpxchg", "extractelement",
    "extractvalue", "fadd", "fcmp", "fdiv", "fence", "fmul", "fneg",
    "fpext", "fptosi", "fptoui", "fptrunc", "freeze", "frem", "fsub",
    "getelementptr", "icmp", "indirectbr", "insertelement", "insertvalue",
    "inttoptr", "invoke", "landingpad", "load", "lshr", "mul", "or",
    "phi", "ptrtoint", "resume", "ret", "sdiv", "select", "sext",
    "shl", "shufflevector", "sitofp", "srem", "store", "sub", "switch",
    "trunc", "udiv", "uitofp", "unreachable", "urem", "va_arg", "xor",
    "zext",
}

ATTRIBUTE_WORDS = {
    "align", "alignstack", "allocsize", "alwaysinline", "builtin", "byref",
    "byval", "captures", "cold", "convergent", "dereferenceable",
    "dereferenceable_or_null", "disable_sanitizer_instrumentation", "elementtype",
    "fn_ret_thunk_extern", "hot", "immarg", "inalloca", "inreg",
    "jumptable", "memory", "minsize", "mustprogress", "naked", "nest",
    "noalias", "nobuiltin", "nocallback", "nocf_check", "noduplicate",
    "nofree", "noinline", "nomerge", "nonlazybind", "nonnull", "noprofile",
    "norecurse", "noredzone", "noreturn", "nosync", "nounwind", "noundef",
    "null_pointer_is_valid", "optforfuzzing", "optnone", "optsize",
    "preallocated", "readnone", "readonly", "returned", "returns_twice",
    "safestack", "sanitize_address", "sanitize_hwaddress", "sanitize_memory",
    "sanitize_thread", "shadowcallstack", "signext", "speculatable", "sret",
    "ssp", "sspreq", "sspstrong", "strictfp", "swiftasync", "swifterror",
    "swiftself", "uwtable", "vscale_range", "willreturn", "writeonly",
    "zeroext",
}


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha256_file(path):
    return sha256_bytes(path.read_bytes())


def write_json(path, value):
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def run(command, cwd, stderr_path):
    process = subprocess.run(
        command,
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if process.stdout:
        stderr_path.with_suffix(stderr_path.suffix + ".stdout").write_bytes(
            process.stdout
        )
    stderr_path.write_bytes(process.stderr)
    return process.returncode


def artifact(path, relative_to):
    if not path.exists():
        return None
    data = path.read_bytes()
    return {
        "path": str(path.relative_to(relative_to)),
        "bytes": len(data),
        "sha256": sha256_bytes(data),
    }


def diagnostic_category(path):
    if not path.exists():
        return None
    text = path.read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        line = re.sub(r"^(ERROR|error):\s*", "", line)
        line = re.sub(r"[/A-Za-z0-9_.+-]+:\d+(?::\d+)?:\s*", "", line)
        line = re.sub(r"\b\d+\b", "#", line)
        return line[:240]
    return None


def normalized_command(command, repo, case_dir):
    result = []
    for item in command:
        item = str(item)
        if item.startswith(str(case_dir)):
            item = "<case>" + item[len(str(case_dir)):]
        elif item.startswith(str(repo)):
            item = "<repo>" + item[len(str(repo)):]
        result.append(item)
    return result


def command_record(command, status, stderr_path, repo, case_dir):
    return {
        "argv": normalized_command(command, repo, case_dir),
        "status": status,
        "diagnostic_category": diagnostic_category(stderr_path),
    }


def increment(table, key, amount=1):
    table[key] = table.get(key, 0) + amount


def llvm_inventory(path):
    inventory = {
        "instructions": {},
        "unknown_instructions": {},
        "intrinsics": {},
        "attributes": {},
        "named_metadata": {},
        "metadata_attachments": {},
    }
    if not path.exists():
        return inventory
    inside_function = False
    inside_switch_table = False
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.split(";", 1)[0].strip()
        if not line:
            continue
        if inside_switch_table:
            if line == "]":
                inside_switch_table = False
            continue
        definition_line = line.startswith("define ") and line.endswith("{")
        if definition_line:
            inside_function = True
        elif inside_function and line == "}":
            inside_function = False
            continue
        for intrinsic in re.findall(r"@llvm\.([A-Za-z0-9_.]+)", line):
            parts = intrinsic.split(".")
            family = ".".join(parts[:2])
            increment(inventory["intrinsics"], family)
        for attachment in re.findall(r",\s*!([A-Za-z0-9_.-]+)\s+!\d+", line):
            increment(inventory["metadata_attachments"], attachment)
        named = re.match(r"!([A-Za-z][A-Za-z0-9_.-]*)\s*=", line)
        if named:
            increment(inventory["named_metadata"], named.group(1))
        words = set(re.findall(r"\b[A-Za-z_][A-Za-z0-9_.]*\b", line))
        for attribute_name in sorted(words & ATTRIBUTE_WORDS):
            increment(inventory["attributes"], attribute_name)
        if definition_line or not inside_function or line.endswith(":"):
            continue
        instruction_text = re.sub(
            r'^%(?:[-A-Za-z$._0-9]+|"(?:[^"\\]|\\.)+")\s*=\s*',
            "",
            line,
        )
        tokens = instruction_text.split()
        if not tokens:
            continue
        opcode = tokens[0]
        if opcode in {"cleanup", "catch", "filter", "to", "unwind"}:
            continue
        if opcode in {"tail", "musttail", "notail"} and len(tokens) > 1:
            opcode = tokens[1]
        increment(inventory["instructions"], opcode)
        if opcode not in KNOWN_INSTRUCTIONS:
            increment(inventory["unknown_instructions"], opcode)
        if opcode == "switch" and line.endswith("["):
            inside_switch_table = True
    return inventory


def merge_inventory(target, source):
    for family, counts in source.items():
        for name, count in counts.items():
            increment(target[family], name, count)


def expected_status(source):
    sidecar = source.with_suffix(".ref.exit_status")
    if sidecar.is_file():
        return sidecar.read_text(encoding="utf-8").strip()
    descriptor_source = re.match(r"^(.*)\.t\.[0-9]+$", str(source))
    if descriptor_source:
        sidecar = Path(descriptor_source.group(1) + ".ref.impl.exit_status")
        if sidecar.is_file():
            value = sidecar.read_text(encoding="utf-8").strip()
            return SUCCESS if value == "0" else "EXIT_FAILURE"
    return None


def stable_case_id(relative):
    stem = re.sub(r"[^A-Za-z0-9]+", "_", str(relative)).strip("_")
    suffix = hashlib.sha256(str(relative).encode("utf-8")).hexdigest()[:10]
    return stem[:100] + "__" + suffix


def discover(repo, pa_from, pa_through, case_glob):
    cases = []
    for pa in range(pa_from, pa_through + 1):
        roots = [repo / ("pa{}".format(pa)) / "tests",
                 repo / "cppgm.tests" / "course" / ("pa{}".format(pa))]
        for root in roots:
            if not root.is_dir():
                continue
            for source in root.rglob("*.t"):
                relative = source.relative_to(repo)
                if case_glob and not fnmatch.fnmatch(str(relative), case_glob):
                    continue
                cases.append((pa, source, relative))
    return sorted(cases, key=lambda item: str(item[2]))


def discover_list(repo, source_list, case_glob):
    cases = []
    for line_number, raw in enumerate(
            source_list.read_text(encoding="utf-8").splitlines(), 1):
        value = raw.split("#", 1)[0].strip()
        if not value:
            continue
        source = (repo / value).resolve()
        try:
            relative = source.relative_to(repo)
        except ValueError:
            raise RuntimeError(
                "source list path escapes repository at line {}: {}".format(
                    line_number, value
                )
            )
        if not source.is_file():
            raise RuntimeError(
                "source list path does not exist at line {}: {}".format(
                    line_number, value
                )
            )
        match = re.match(r"pa([0-9]+)/", str(relative))
        if not match:
            match = re.match(r"cppgm\.tests/course/pa([0-9]+)/", str(relative))
        if not match:
            raise RuntimeError(
                "source list path has no assignment owner at line {}: {}".format(
                    line_number, value
                )
            )
        if case_glob and not fnmatch.fnmatch(str(relative), case_glob):
            continue
        cases.append((int(match.group(1)), source, relative))
    return sorted(cases, key=lambda item: str(item[2]))


def checked_tool(value, repo):
    candidate = Path(value)
    if not candidate.is_absolute() and (repo / candidate).is_file():
        candidate = repo / candidate
    elif not candidate.is_file():
        found = shutil.which(value)
        if not found:
            raise RuntimeError("tool not found: " + value)
        candidate = Path(found)
    return str(candidate.resolve())


def prepare_output(output, repo):
    output = output.resolve()
    protected = [repo, repo / "dev", repo / "doc", repo / "cppgm.tests"]
    protected.extend(path for path in repo.glob("pa[0-9]*") if path.is_dir())
    for path in protected:
        try:
            output.relative_to(path.resolve())
            raise RuntimeError("scratch output is inside protected path: " + str(path))
        except ValueError:
            pass
    if output.exists() and any(output.iterdir()):
        raise RuntimeError("scratch output is not empty: " + str(output))
    output.mkdir(parents=True, exist_ok=True)
    write_json(output / "RUN-MARKER.json", {"schema": SCHEMA})
    (output / "cases").mkdir()
    return output


def run_case(pa, source, relative, repo, output, cppgm, clang, clangxx):
    case_id = stable_case_id(relative)
    case_dir = output / "cases" / case_id
    case_dir.mkdir()
    lowir = case_dir / "subject.lowir"
    ours_ir = case_dir / "ours.ll"
    clang_ir = case_dir / "clang.ll"
    ours_object = case_dir / "ours.o"
    clang_object = case_dir / "clang.o"
    commands = {}

    lowir_command = [cppgm, "--emit-lowir", "-O0", str(source), "-o", str(lowir)]
    lowir_stderr = case_dir / "subject.stderr"
    lowir_status = run(lowir_command, repo, lowir_stderr)
    commands["subject_lowir"] = command_record(
        lowir_command, lowir_status, lowir_stderr, repo, case_dir
    )

    ours_command = [cppgm, "--emit-llvm-ir", "-O0", str(source), "-o", str(ours_ir)]
    ours_stderr = case_dir / "ours.stderr"
    ours_status = run(ours_command, repo, ours_stderr)
    commands["ours_ir"] = command_record(
        ours_command, ours_status, ours_stderr, repo, case_dir
    )

    clang_command = [
        clangxx, "-std=gnu++11", "-stdlib=libstdc++", "-O0", "-g0",
        "-S", "-emit-llvm", "-Xclang", "-disable-llvm-passes",
        "-x", "c++", str(source), "-o", str(clang_ir),
    ]
    clang_stderr = case_dir / "clang.stderr"
    clang_status = run(clang_command, repo, clang_stderr)
    commands["clang_ir"] = command_record(
        clang_command, clang_status, clang_stderr, repo, case_dir
    )

    ours_verify_status = None
    if ours_status == 0:
        verify = [clang, "-x", "ir", "-c", str(ours_ir), "-o", str(ours_object)]
        verify_stderr = case_dir / "ours.verify.stderr"
        ours_verify_status = run(verify, repo, verify_stderr)
        commands["ours_verify"] = command_record(
            verify, ours_verify_status, verify_stderr, repo, case_dir
        )

    clang_verify_status = None
    if clang_status == 0:
        verify = [clang, "-x", "ir", "-c", str(clang_ir), "-o", str(clang_object)]
        verify_stderr = case_dir / "clang.verify.stderr"
        clang_verify_status = run(verify, repo, verify_stderr)
        commands["clang_verify"] = command_record(
            verify, clang_verify_status, verify_stderr, repo, case_dir
        )

    oracle = expected_status(source)
    if oracle != SUCCESS:
        state = "expected-rejection"
    elif lowir_status != 0:
        state = "subject-failure"
    elif ours_status == 0 and ours_verify_status != 0:
        state = "llvm-invalid"
    elif clang_status == 0 and clang_verify_status != 0:
        state = "clang-ir-invalid"
    elif ours_status != 0:
        state = "exporter-limitation"
    elif clang_status != 0:
        state = "clang-noncomparable"
    else:
        state = "complete"

    artifact_paths = [
        lowir, lowir_stderr, ours_ir, ours_stderr,
        case_dir / "ours.verify.stderr", ours_object,
        clang_ir, clang_stderr, case_dir / "clang.verify.stderr", clang_object,
    ]
    record = {
        "schema": SCHEMA,
        "id": case_id,
        "pa": pa,
        "source": str(relative),
        "source_sha256": sha256_file(source),
        "oracle_status": oracle,
        "state": state,
        "commands": commands,
        "artifacts": [item for item in
                      (artifact(path, output) for path in artifact_paths)
                      if item is not None],
        "ours_inventory": llvm_inventory(ours_ir)
            if ours_status == 0 and ours_verify_status == 0
            else llvm_inventory(Path("/does/not/exist")),
        "clang_inventory": llvm_inventory(clang_ir)
            if clang_status == 0 and clang_verify_status == 0
            else llvm_inventory(Path("/does/not/exist")),
    }
    write_json(case_dir / "case.json", record)
    return record


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--output", required=True)
    parser.add_argument("--cppgm", default="dev/cppgm++")
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--clangxx", default="clang++")
    parser.add_argument("--pa-from", type=int, default=15)
    parser.add_argument("--pa-through", type=int, default=15)
    parser.add_argument("--case-glob")
    parser.add_argument(
        "--source-list",
        help="newline-delimited repository-relative source paths; # starts comments",
    )
    parser.add_argument("--limit", type=int)
    parser.add_argument("--quiet", action="store_true")
    arguments = parser.parse_args()

    repo = Path(arguments.repo_root).resolve()
    if arguments.pa_from > arguments.pa_through:
        raise RuntimeError("--pa-from exceeds --pa-through")
    output = prepare_output(Path(arguments.output), repo)
    cppgm = checked_tool(arguments.cppgm, repo)
    clang = checked_tool(arguments.clang, repo)
    clangxx = checked_tool(arguments.clangxx, repo)
    if arguments.source_list:
        source_list = Path(arguments.source_list)
        if not source_list.is_absolute():
            source_list = repo / source_list
        if not source_list.is_file():
            raise RuntimeError("source list not found: " + str(source_list))
        cases = discover_list(repo, source_list, arguments.case_glob)
    else:
        cases = discover(
            repo, arguments.pa_from, arguments.pa_through, arguments.case_glob
        )
    if arguments.limit is not None:
        cases = cases[:arguments.limit]

    corpus = []
    summary = {
        "schema": SCHEMA,
        "case_count": len(cases),
        "successful_denominator": 0,
        "states": {},
        "by_pa": {},
        "ours_inventory": llvm_inventory(Path("/does/not/exist")),
        "clang_inventory": llvm_inventory(Path("/does/not/exist")),
    }
    for index, (pa, source, relative) in enumerate(cases, 1):
        if not arguments.quiet:
            sys.stderr.write("[{}/{}] {}\n".format(index, len(cases), relative))
        record = run_case(
            pa, source, relative, repo, output, cppgm, clang, clangxx
        )
        corpus.append({
            "id": record["id"],
            "pa": pa,
            "source": str(relative),
            "oracle_status": record["oracle_status"],
            "state": record["state"],
        })
        if record["oracle_status"] == SUCCESS:
            summary["successful_denominator"] += 1
        increment(summary["states"], record["state"])
        pa_key = "pa{}".format(pa)
        if pa_key not in summary["by_pa"]:
            summary["by_pa"][pa_key] = {"cases": 0, "states": {}}
        summary["by_pa"][pa_key]["cases"] += 1
        increment(summary["by_pa"][pa_key]["states"], record["state"])
        merge_inventory(summary["ours_inventory"], record["ours_inventory"])
        merge_inventory(summary["clang_inventory"], record["clang_inventory"])
    write_json(output / "corpus.json", {"schema": SCHEMA, "cases": corpus})
    write_json(output / "summary.json", summary)
    sys.stdout.write(json.dumps(summary, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as error:
        sys.stderr.write("run_lowir_llvm_sweep.py: {}\n".format(error))
        sys.exit(1)
