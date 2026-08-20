#!/usr/bin/env python3
"""Report deterministic ELF code-shape metrics using GNU binutils views."""

import argparse
import collections
import hashlib
import json
from pathlib import Path
import re
import subprocess


SECTION_RE = re.compile(
    r"^\s*\[\s*(\d+)\]\s+(\S+)\s+(\S+)\s+"
    r"[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)\s+"
)
SYMBOL_RE = re.compile(
    r"^\s*\d+:\s+[0-9A-Fa-f]+\s+(0x[0-9A-Fa-f]+|\d+)\s+"
    r"(\S+)\s+(\S+)\s+"
    r"\S+\s+(\S+)\s*(.*)$"
)
INSTRUCTION_RE = re.compile(
    r"^\s*[0-9A-Fa-f]+:\s+(?:[0-9A-Fa-f]{2}\s+)+"
    r"([A-Za-z][A-Za-z0-9_.]*)\s*(.*)$"
)
RELOCATION_RE = re.compile(
    r"^\s*[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+R_[A-Za-z0-9_]+"
)
CALL_TARGET_RE = re.compile(r"<([^>]+)>")
CALL_RELOCATION_RE = re.compile(
    r"^\s*[0-9A-Fa-f]+:\s+R_[A-Za-z0-9_]+\s+(\S+)"
)


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def run_tool(tool, *args):
    result = subprocess.run(
        [tool, *args], text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=False
    )
    if result.returncode != 0:
        raise RuntimeError("%s failed: %s" % (tool, result.stderr.strip()))
    return result.stdout


def parse_sections(text):
    sections = []
    for line in text.splitlines():
        match = SECTION_RE.match(line)
        if match:
            index, name, section_type, size = match.groups()
            sections.append({
                "index": int(index),
                "name": name,
                "type": section_type,
                "size": int(size, 16),
            })
    return sections


def parse_symbols(text):
    functions = []
    for line in text.splitlines():
        match = SYMBOL_RE.match(line)
        if not match:
            continue
        size, symbol_type, binding, section, name = match.groups()
        if symbol_type != "FUNC" or section == "UND":
            continue
        functions.append({
            "name": name.strip(),
            "size": int(size, 0),
            "binding": binding,
            "section": section,
        })
    return functions


def parse_disassembly(text):
    instructions = collections.Counter()
    call_targets = collections.Counter()
    pending_call_target = None
    for line in text.splitlines():
        match = INSTRUCTION_RE.match(line)
        if match:
            if pending_call_target:
                call_targets[pending_call_target] += 1
            mnemonic, operands = match.groups()
            instructions[mnemonic] += 1
            pending_call_target = None
            if mnemonic in ("call", "callq"):
                target = CALL_TARGET_RE.search(operands)
                if target and not target.group(1).startswith("0x"):
                    pending_call_target = target.group(1)
            continue
        relocation = CALL_RELOCATION_RE.match(line)
        if pending_call_target is not None and relocation:
            call_targets[relocation.group(1)] += 1
            pending_call_target = None
        elif line.strip():
            if pending_call_target:
                call_targets[pending_call_target] += 1
            pending_call_target = None
    if pending_call_target:
        call_targets[pending_call_target] += 1
    return instructions, call_targets


def parse_relocation_count(text):
    return sum(bool(RELOCATION_RE.match(line)) for line in text.splitlines())


def sum_sections(sections, predicate):
    return sum(section["size"] for section in sections if predicate(section))


def report_object(path, selected_functions, selected_calls, tools):
    sections = parse_sections(run_tool(tools["readelf"], "-SW", str(path)))
    functions = parse_symbols(
        run_tool(tools["readelf"], "-Ws", "--demangle", str(path))
    )
    relocations = parse_relocation_count(
        run_tool(tools["readelf"], "-Wr", str(path))
    )
    instructions, call_targets = parse_disassembly(
        run_tool(tools["objdump"], "-dr", "--demangle", str(path))
    )
    bindings = collections.Counter(function["binding"] for function in functions)
    function_sizes = collections.defaultdict(int)
    for function in functions:
        function_sizes[function["name"]] += function["size"]
    return {
        "path": str(path.resolve()),
        "bytes": path.stat().st_size,
        "sha256": file_sha256(path),
        "sections": {section["name"]: section["size"] for section in sections},
        "section_totals": {
            "text_bytes": sum_sections(
                sections, lambda section: section["name"].startswith(".text")
            ),
            "base_text_bytes": sum_sections(
                sections, lambda section: section["name"] == ".text"
            ),
            "gcc_except_table_bytes": sum_sections(
                sections,
                lambda section: section["name"].startswith(".gcc_except_table"),
            ),
            "eh_frame_bytes": sum_sections(
                sections, lambda section: section["name"] == ".eh_frame"
            ),
            "relocation_bytes": sum_sections(
                sections, lambda section: section["type"] in ("REL", "RELA")
            ),
            "strtab_bytes": sum_sections(
                sections, lambda section: section["name"] == ".strtab"
            ),
            "shstrtab_bytes": sum_sections(
                sections, lambda section: section["name"] == ".shstrtab"
            ),
        },
        "relocations": relocations,
        "defined_functions": {
            "total": len(functions),
            "by_binding": dict(sorted(bindings.items())),
        },
        "decoded_instructions": sum(instructions.values()),
        "instruction_families": dict(instructions.most_common()),
        "selected_function_sizes": {
            pattern: max(
                (size for name, size in function_sizes.items() if pattern in name),
                default=0,
            )
            for pattern in selected_functions
        },
        "selected_call_targets": {
            pattern: sum(
                count for name, count in call_targets.items() if pattern in name
            )
            for pattern in selected_calls
        },
    }


def print_summary(report):
    totals = report["section_totals"]
    print(report["path"])
    print("  bytes=%d sha256=%s" % (report["bytes"], report["sha256"]))
    print(
        "  text=%d base_text=%d gcc_except_table=%d eh_frame=%d"
        % (
            totals["text_bytes"], totals["base_text_bytes"],
            totals["gcc_except_table_bytes"], totals["eh_frame_bytes"],
        )
    )
    print(
        "  relocations=%d relocation_bytes=%d functions=%d bindings=%s"
        % (
            report["relocations"], totals["relocation_bytes"],
            report["defined_functions"]["total"],
            report["defined_functions"]["by_binding"],
        )
    )
    print("  decoded_instructions=%d" % report["decoded_instructions"])
    if report["selected_function_sizes"]:
        print("  selected_function_sizes=%s" % report["selected_function_sizes"])
    if report["selected_call_targets"]:
        print("  selected_call_targets=%s" % report["selected_call_targets"])


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("object", nargs="+")
    parser.add_argument("--function", action="append", default=[])
    parser.add_argument("--call-target", action="append", default=[])
    parser.add_argument("--json")
    parser.add_argument("--readelf", default="readelf")
    parser.add_argument("--objdump", default="objdump")
    return parser.parse_args()


def main():
    args = parse_args()
    tools = {"readelf": args.readelf, "objdump": args.objdump}
    reports = [
        report_object(Path(value), args.function, args.call_target, tools)
        for value in args.object
    ]
    for report in reports:
        print_summary(report)
    if args.json:
        Path(args.json).write_text(
            json.dumps({"objects": reports}, indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )


if __name__ == "__main__":
    main()
