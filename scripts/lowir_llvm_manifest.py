#!/usr/bin/env python3
"""Capture the reproducible toolchain profile for the LowIR/LLVM study."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys


LANGREF_VERSION = "21.1"
LANGREF_URL = "https://releases.llvm.org/21.1.0/docs/LangRef.html"
LLVM_RELEASE_NOTES_URL = (
    "https://releases.llvm.org/21.1.0/docs/ReleaseNotes.html"
)


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def run(command, cwd, input_text="", environment=None):
    process = subprocess.run(
        command,
        cwd=str(cwd),
        env=environment,
        input=input_text,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    return {
        "command": command,
        "status": process.returncode,
        "stdout": process.stdout,
        "stderr": process.stderr,
    }


def require_success(result, label):
    if result["status"] != 0:
        raise RuntimeError(
            "{} failed with status {}:\n{}".format(
                label, result["status"], result["stderr"]
            )
        )
    return result["stdout"]


def first_lines(text, count=3):
    return text.rstrip("\n").splitlines()[:count]


def command_record(command, cwd, input_text=""):
    result = run(command, cwd, input_text)
    return {
        "command": command,
        "status": result["status"],
        "stdout_lines": first_lines(result["stdout"]),
        "stdout_sha256": digest(result["stdout"]),
        "stderr_lines": first_lines(result["stderr"]),
        "stderr_sha256": digest(result["stderr"]),
    }


def parse_include_paths(text):
    paths = []
    collecting = False
    for raw_line in text.splitlines():
        if "#include <...> search starts here:" in raw_line:
            collecting = True
            continue
        if collecting and "End of search list." in raw_line:
            break
        if not collecting:
            continue
        path = raw_line.strip()
        if not path or "(framework directory)" in path:
            continue
        paths.append(os.path.realpath(path))
    return paths


def extract_assignment(raw_header, name):
    ordinary = re.search(
        r"static const char {}\[\] = \"((?:[^\"\\]|\\.)*)\";".format(
            re.escape(name)
        ),
        raw_header,
    )
    if ordinary:
        return bytes(ordinary.group(1), "utf-8").decode("unicode_escape")
    raw = re.search(
        r"static const char {}\[\] = R\"([^()]*)\((.*?)\)\1\";".format(
            re.escape(name)
        ),
        raw_header,
        re.DOTALL,
    )
    if raw:
        return raw.group(2)
    raise RuntimeError("generated host config has no {} assignment".format(name))


def parse_generated_host_config(text):
    include_block = re.search(
        r"kStandardIncludePaths\[\]\s*=\s*\{(.*?)\};", text, re.DOTALL
    )
    if not include_block:
        raise RuntimeError("generated host config has no include-path block")
    include_paths = []
    for escaped in re.findall(r'"((?:[^"\\]|\\.)*)"', include_block.group(1)):
        include_paths.append(
            os.path.realpath(bytes(escaped, "utf-8").decode("unicode_escape"))
        )
    macros = extract_assignment(text, "kHostPredefinedMacros")
    return {
        "host_cxx": extract_assignment(text, "kHostCxx"),
        "stdlib_flags": extract_assignment(text, "kStdlibFlags"),
        "target": extract_assignment(text, "kTarget").strip(),
        "version": extract_assignment(text, "kVersion").strip(),
        "search_dirs_sha256": digest(extract_assignment(text, "kSearchDirs")),
        "predefined_macros_sha256": digest(macros),
        "predefined_macro_lines": len(macros.splitlines()),
        "standard_include_paths": include_paths,
        "generated_header_sha256": digest(text),
    }


def probe_host_config(repo, host_command, stdlib_flags):
    environment = os.environ.copy()
    environment["CPPGM_BUILD_HOST_CXX"] = " ".join(
        shlex.quote(item) for item in host_command
    )
    environment["CPPGM_STDLIB_FLAGS"] = " ".join(stdlib_flags)
    result = run(
        ["perl", "dev/gen_builtin_host_config.pl"],
        repo,
        environment=environment,
    )
    return parse_generated_host_config(require_success(result, "host config probe"))


def find_tool(name):
    path = shutil.which(name)
    return os.path.realpath(path) if path else None


def probe_optional_llvm_tools(repo):
    result = {}
    for name in ("llvm-as", "opt", "llvm-dis", "llvm-link", "llc"):
        path = find_tool(name)
        record = {"path": path}
        if path:
            record["version"] = command_record([path, "--version"], repo)
        result[name] = record
    return result


def probe_clang(repo, clang_command, stdlib_flags):
    common = clang_command + ["-std=gnu++11"] + stdlib_flags
    version = run(clang_command + ["--version"], repo)
    target = run(clang_command + ["-dumpmachine"], repo)
    compiler_version = run(clang_command + ["-dumpversion"], repo)
    resources = run(clang_command + ["-print-resource-dir"], repo)
    includes = run(common + ["-E", "-x", "c++", "-", "-v"], repo)
    macros = run(common + ["-dM", "-E", "-x", "c++", "-"], repo)
    minimal_source = "int cppgm_llvm_probe(int x) { return x + 1; }\n"
    pristine_ir = run(
        common
        + [
            "-O0",
            "-g0",
            "-S",
            "-emit-llvm",
            "-Xclang",
            "-disable-llvm-passes",
            "-x",
            "c++",
            "-",
            "-o",
            "-",
        ],
        repo,
        minimal_source,
    )
    ir_text = require_success(pristine_ir, "Clang pristine LLVM IR probe")
    data_layout_match = re.search(r'^target datalayout = "(.*)"$', ir_text, re.M)
    triple_match = re.search(r'^target triple = "(.*)"$', ir_text, re.M)
    if not data_layout_match or not triple_match:
        raise RuntimeError("Clang LLVM output has no target layout/triple")
    macro_text = require_success(macros, "Clang predefined macro probe")
    include_text = includes["stdout"] + includes["stderr"]
    return {
        "command": clang_command,
        "path": os.path.realpath(clang_command[0]),
        "version_lines": first_lines(require_success(version, "Clang version")),
        "target": require_success(target, "Clang target").strip(),
        "compiler_version": require_success(
            compiler_version, "Clang compiler version"
        ).strip(),
        "resource_directory": os.path.realpath(
            require_success(resources, "Clang resource directory").strip()
        ),
        "standard_include_paths": parse_include_paths(include_text),
        "predefined_macros_sha256": digest(macro_text),
        "predefined_macro_lines": len(macro_text.splitlines()),
        "pristine_ir_sha256": digest(ir_text),
        "target_triple": triple_match.group(1),
        "data_layout": data_layout_match.group(1),
        "pristine_flags": common[1:]
        + ["-O0", "-g0", "-S", "-emit-llvm", "-Xclang", "-disable-llvm-passes"],
    }


def probe_cppgm(repo, cppgm_command):
    def query(flag):
        return require_success(run(cppgm_command + [flag], repo), "cppgm++ " + flag)

    return {
        "command": cppgm_command,
        "path": os.path.realpath(cppgm_command[0]),
        "version_lines": first_lines(query("--version")),
        "target": query("-dumpmachine").strip(),
        "compiler_version": query("-dumpversion").strip(),
        "search_dirs_sha256": digest(query("-print-search-dirs")),
    }


def probe_repository(repo):
    revision = require_success(
        run(["git", "rev-parse", "HEAD"], repo), "repository revision"
    ).strip()
    status = require_success(
        run(["git", "status", "--short", "--untracked-files=all"], repo),
        "repository status",
    )
    return {
        "root": str(repo),
        "revision": revision,
        "working_tree_status": status.splitlines(),
        "working_tree_status_sha256": digest(status),
    }


def parse_command(value):
    result = shlex.split(value)
    if not result:
        raise argparse.ArgumentTypeError("compiler command must not be empty")
    path = find_tool(result[0])
    if not path:
        raise argparse.ArgumentTypeError("unable to find command: " + result[0])
    result[0] = path
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", default=str(Path(__file__).resolve().parents[1]))
    parser.add_argument("--cppgm", default="dev/cppgm++")
    parser.add_argument("--clang", default="clang++")
    parser.add_argument("--host-cxx", default=os.environ.get("CPPGM_BUILD_HOST_CXX", "g++"))
    parser.add_argument("--stdlib-flag", action="append", default=[])
    parser.add_argument("--output", default="-")
    arguments = parser.parse_args()

    repo = Path(arguments.repo_root).resolve()
    cppgm_path = Path(arguments.cppgm)
    if not cppgm_path.is_absolute():
        cppgm_path = repo / cppgm_path
    if not cppgm_path.is_file():
        raise RuntimeError("cppgm++ does not exist: " + str(cppgm_path))
    cppgm_command = [str(cppgm_path.resolve())]
    clang_command = parse_command(arguments.clang)
    host_command = parse_command(arguments.host_cxx)
    stdlib_flags = list(arguments.stdlib_flag)
    if not stdlib_flags:
        stdlib_flags = shlex.split(os.environ.get("CPPGM_STDLIB_FLAGS", ""))
    clang_stdlib_flags = list(stdlib_flags)
    if not any(flag.startswith("-stdlib") for flag in clang_stdlib_flags):
        clang_stdlib_flags.append("-stdlib=libstdc++")

    manifest = {
        "schema": "cppgm-lowir-llvm-toolchain-manifest-v1",
        "llvm_specification": {
            "language_reference_version": LANGREF_VERSION,
            "language_reference_url": LANGREF_URL,
            "release_notes_url": LLVM_RELEASE_NOTES_URL,
        },
        "repository": probe_repository(repo),
        "cppgm": probe_cppgm(repo, cppgm_command),
        "generated_host_configuration": probe_host_config(
            repo, host_command, stdlib_flags
        ),
        "clang": probe_clang(repo, clang_command, clang_stdlib_flags),
        "llvm_tools": probe_optional_llvm_tools(repo),
        "comparison_profile": {
            "language": "gnu++11",
            "optimization": "O0",
            "debug_information": "disabled",
            "llvm_passes": "disabled for pristine frontend lane",
            "stdlib_flags": clang_stdlib_flags,
        },
    }
    rendered = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    if arguments.output == "-":
        sys.stdout.write(rendered)
    else:
        output = Path(arguments.output)
        output.write_text(rendered, encoding="utf-8")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError) as error:
        sys.stderr.write("lowir_llvm_manifest.py: {}\n".format(error))
        sys.exit(1)
