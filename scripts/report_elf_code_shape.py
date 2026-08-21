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
    r"^\s*([0-9A-Fa-f]+):\s+((?:[0-9A-Fa-f]{2}\s+)+)"
    r"([A-Za-z][A-Za-z0-9_.]*)\s*(.*)$"
)
FUNCTION_HEADER_RE = re.compile(r"^\s*[0-9A-Fa-f]+\s+<(.+)>:\s*$")
RELOCATION_RE = re.compile(
    r"^\s*[0-9A-Fa-f]+\s+[0-9A-Fa-f]+\s+R_[A-Za-z0-9_]+"
)
RELOCATION_SECTION_RE = re.compile(r"^Relocation section '([^']+)' ")
LSDA_RELOCATION_RE = re.compile(
    r"\s(\.gcc_except_table\S*)\s+\+\s+([0-9A-Fa-f]+)\s*$"
)
HEX_SECTION_RE = re.compile(r"^Hex dump of section '([^']+)':$")
HEX_LINE_RE = re.compile(r"^\s*0x[0-9A-Fa-f]+\s+(.*)$")
CALL_TARGET_RE = re.compile(r"<([^>]+)>")
CALL_RELOCATION_RE = re.compile(
    r"^\s*[0-9A-Fa-f]+:\s+R_[A-Za-z0-9_]+\s+(\S+)"
)
INLINE_RELOCATION_RE = re.compile(r"\bR_[A-Za-z0-9_]+\s+(\S+)")


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


def split_operands(operands):
    result = []
    start = 0
    depth = 0
    for index, character in enumerate(operands):
        if character == "(":
            depth += 1
        elif character == ")":
            depth = max(0, depth - 1)
        elif character == "," and depth == 0:
            result.append(operands[start:index].strip())
            start = index + 1
    tail = operands[start:].strip()
    if tail:
        result.append(tail)
    return result


def operand_class(operand):
    operand = operand.strip()
    if operand.startswith("$"):
        return "immediate"
    if operand.startswith("%") and "(" not in operand:
        return "register"
    if "(" in operand or operand.startswith("*"):
        return "memory"
    if operand:
        return "symbol"
    return "none"


def movement_operand_class(mnemonic, operands):
    parts = split_operands(operands)
    if mnemonic.startswith("lea") and len(parts) == 2:
        return "address_to_%s" % operand_class(parts[1])
    if mnemonic.startswith("mov") and len(parts) == 2:
        return "%s_to_%s" % (
            operand_class(parts[0]), operand_class(parts[1])
        )
    if mnemonic.startswith("push") and len(parts) == 1:
        return "%s_to_stack" % operand_class(parts[0])
    if mnemonic.startswith("pop") and len(parts) == 1:
        return "stack_to_%s" % operand_class(parts[0])
    return ""


def normalize_prefixed_instruction(mnemonic, operands):
    # GNU objdump sometimes renders a REX or repeat/lock prefix as the
    # mnemonic and places the semantic opcode at the front of the operand
    # column. Keep one decoded instruction while attributing it to the
    # operation the processor executes.
    if (mnemonic.startswith("rex") or
            mnemonic in ("rep", "repe", "repz", "repne", "repnz", "lock")):
        parts = operands.split(None, 1)
        if parts:
            mnemonic = parts[0]
            operands = parts[1] if len(parts) == 2 else ""
    return mnemonic, operands


def parse_disassembly_details(text):
    instructions = collections.Counter()
    instruction_bytes = collections.Counter()
    operand_classes = collections.Counter()
    operand_class_bytes = collections.Counter()
    call_targets = collections.Counter()
    functions = collections.defaultdict(lambda: {
        "instructions": 0,
        "bytes": 0,
        "instruction_bytes": collections.Counter(),
        "call_targets": collections.Counter(),
    })
    pending_call_target = None
    pending_call_function = ""
    current_function = ""
    for line in text.splitlines():
        header = FUNCTION_HEADER_RE.match(line)
        if header:
            current_function = header.group(1)
            continue
        match = INSTRUCTION_RE.match(line)
        if match:
            if pending_call_target:
                call_targets[pending_call_target] += 1
                if pending_call_function:
                    functions[pending_call_function]["call_targets"][
                        pending_call_target
                    ] += 1
            _address, encoded, mnemonic, operands = match.groups()
            mnemonic, operands = normalize_prefixed_instruction(
                mnemonic, operands
            )
            byte_count = len(encoded.split())
            instructions[mnemonic] += 1
            instruction_bytes[mnemonic] += byte_count
            movement = movement_operand_class(mnemonic, operands)
            if movement:
                key = "%s:%s" % (
                    "lea" if mnemonic.startswith("lea") else
                    "mov" if mnemonic.startswith("mov") else
                    "push" if mnemonic.startswith("push") else "pop",
                    movement,
                )
                operand_classes[key] += 1
                operand_class_bytes[key] += byte_count
            if current_function:
                function = functions[current_function]
                function["instructions"] += 1
                function["bytes"] += byte_count
                function["instruction_bytes"][mnemonic] += byte_count
            pending_call_target = None
            pending_call_function = ""
            if mnemonic in ("call", "callq"):
                relocation = INLINE_RELOCATION_RE.search(line)
                if relocation:
                    call_targets[relocation.group(1)] += 1
                    if current_function:
                        functions[current_function]["call_targets"][
                            relocation.group(1)
                        ] += 1
                    continue
                target = CALL_TARGET_RE.search(operands)
                if target and not target.group(1).startswith("0x"):
                    pending_call_target = target.group(1)
                    pending_call_function = current_function
            continue
        relocation = CALL_RELOCATION_RE.match(line)
        if pending_call_target is not None and relocation:
            call_targets[relocation.group(1)] += 1
            if pending_call_function:
                functions[pending_call_function]["call_targets"][
                    relocation.group(1)
                ] += 1
            pending_call_target = None
            pending_call_function = ""
        elif line.strip():
            if pending_call_target:
                call_targets[pending_call_target] += 1
                if pending_call_function:
                    functions[pending_call_function]["call_targets"][
                        pending_call_target
                    ] += 1
            pending_call_target = None
            pending_call_function = ""
    if pending_call_target:
        call_targets[pending_call_target] += 1
        if pending_call_function:
            functions[pending_call_function]["call_targets"][
                pending_call_target
            ] += 1
    function_metrics = {}
    for name, function in functions.items():
        function_metrics[name] = {
            "instructions": function["instructions"],
            "bytes": function["bytes"],
            "instruction_bytes": dict(
                function["instruction_bytes"].most_common()
            ),
            "call_targets": dict(function["call_targets"].most_common()),
        }
    return {
        "instructions": instructions,
        "instruction_bytes": instruction_bytes,
        "operand_classes": operand_classes,
        "operand_class_bytes": operand_class_bytes,
        "call_targets": call_targets,
        "functions": function_metrics,
    }


def parse_disassembly(text):
    details = parse_disassembly_details(text)
    return details["instructions"], details["call_targets"]


def parse_relocation_count(text):
    return sum(bool(RELOCATION_RE.match(line)) for line in text.splitlines())


def parse_lsda_relocations(text):
    starts = collections.defaultdict(set)
    relocation_section = ""
    for line in text.splitlines():
        header = RELOCATION_SECTION_RE.match(line)
        if header:
            relocation_section = header.group(1)
            continue
        if relocation_section != ".rela.eh_frame":
            continue
        match = LSDA_RELOCATION_RE.search(line)
        if match:
            section, addend = match.groups()
            starts[section].add(int(addend, 16))
    return {
        section: sorted(offsets)
        for section, offsets in starts.items()
    }


def parse_hex_sections(text, section_sizes):
    result = {}
    section = ""
    for line in text.splitlines():
        header = HEX_SECTION_RE.match(line)
        if header:
            section = header.group(1)
            result[section] = bytearray()
            continue
        match = HEX_LINE_RE.match(line)
        if not section or not match:
            continue
        target_size = section_sizes.get(section, 0)
        for token in match.group(1).split():
            if not re.fullmatch(r"[0-9A-Fa-f]{2,8}", token):
                break
            if len(token) % 2:
                break
            result[section].extend(bytes.fromhex(token))
            if target_size and len(result[section]) >= target_size:
                del result[section][target_size:]
                break
    return {name: bytes(data) for name, data in result.items()}


def read_uleb128(data, offset, limit):
    value = 0
    shift = 0
    while offset < limit:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7f) << shift
        if not byte & 0x80:
            return value, offset
        shift += 7
        if shift >= 64:
            break
    raise ValueError("invalid ULEB128 value")


def read_sleb128(data, offset, limit):
    value = 0
    shift = 0
    while offset < limit:
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7f) << shift
        shift += 7
        if not byte & 0x80:
            if shift < 64 and byte & 0x40:
                value |= -(1 << shift)
            return value, offset
        if shift >= 64:
            break
    raise ValueError("invalid SLEB128 value")


def read_encoded_value(data, offset, limit, encoding):
    value_format = encoding & 0x0f
    if value_format == 0x01:
        return read_uleb128(data, offset, limit)
    if value_format == 0x09:
        return read_sleb128(data, offset, limit)
    widths = {
        0x00: 8,
        0x02: 2,
        0x03: 4,
        0x04: 8,
        0x0a: 2,
        0x0b: 4,
        0x0c: 8,
    }
    width = widths.get(value_format)
    if width is None or offset + width > limit:
        raise ValueError("unsupported or truncated DWARF encoding")
    signed = value_format >= 0x09
    value = int.from_bytes(data[offset:offset + width], "little", signed=signed)
    return value, offset + width


def parse_lsda_call_sites(hex_sections, starts_by_section):
    totals = collections.Counter()
    for section, starts in starts_by_section.items():
        data = hex_sections.get(section, b"")
        for index, start in enumerate(starts):
            limit = starts[index + 1] if index + 1 < len(starts) else len(data)
            try:
                if start >= limit:
                    raise ValueError("empty LSDA")
                offset = start
                lpstart_encoding = data[offset]
                offset += 1
                if lpstart_encoding != 0xff:
                    _lpstart, offset = read_encoded_value(
                        data, offset, limit, lpstart_encoding
                    )
                ttype_encoding = data[offset]
                offset += 1
                if ttype_encoding != 0xff:
                    _ttype_offset, offset = read_uleb128(data, offset, limit)
                call_site_encoding = data[offset]
                offset += 1
                table_length, offset = read_uleb128(data, offset, limit)
                table_end = offset + table_length
                if table_end > limit:
                    raise ValueError("truncated LSDA call-site table")
                totals["lsdas"] += 1
                totals["table_bytes"] += table_length
                while offset < table_end:
                    entry_start = offset
                    _start, offset = read_encoded_value(
                        data, offset, table_end, call_site_encoding
                    )
                    _length, offset = read_encoded_value(
                        data, offset, table_end, call_site_encoding
                    )
                    landing_pad, offset = read_encoded_value(
                        data, offset, table_end, call_site_encoding
                    )
                    action, offset = read_uleb128(data, offset, table_end)
                    entry_bytes = offset - entry_start
                    if landing_pad or action:
                        totals["protected_records"] += 1
                        totals["protected_bytes"] += entry_bytes
                    else:
                        totals["unprotected_records"] += 1
                        totals["unprotected_bytes"] += entry_bytes
            except (IndexError, ValueError):
                totals["unparsed_lsdas"] += 1
    return dict(sorted(totals.items()))


def sum_sections(sections, predicate):
    return sum(section["size"] for section in sections if predicate(section))


def report_object(path, selected_functions, selected_calls, top_functions, tools):
    sections = parse_sections(run_tool(tools["readelf"], "-SW", str(path)))
    functions = parse_symbols(
        run_tool(tools["readelf"], "-Ws", "--demangle", str(path))
    )
    relocation_text = run_tool(tools["readelf"], "-Wr", str(path))
    relocations = parse_relocation_count(relocation_text)
    lsda_sections = [
        section for section in sections
        if section["name"].startswith(".gcc_except_table") and
        section["type"] == "PROGBITS"
    ]
    lsda_call_sites = {}
    if lsda_sections:
        hex_arguments = []
        for section in lsda_sections:
            hex_arguments.extend(("-x", str(section["index"])))
        hex_text = run_tool(
            tools["readelf"], *hex_arguments, str(path)
        )
        section_sizes = {
            section["name"]: section["size"] for section in lsda_sections
        }
        lsda_call_sites = parse_lsda_call_sites(
            parse_hex_sections(hex_text, section_sizes),
            parse_lsda_relocations(relocation_text),
        )
    disassembly = parse_disassembly_details(
        # Wide output keeps every encoded instruction on one line. Without
        # -w, objdump may wrap trailing bytes onto a line that resembles an
        # instruction and corrupt both counts and byte attribution.
        run_tool(tools["objdump"], "-drw", "--demangle", str(path))
    )
    instructions = disassembly["instructions"]
    call_targets = disassembly["call_targets"]
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
        "lsda_call_sites": lsda_call_sites,
        "defined_functions": {
            "total": len(functions),
            "by_binding": dict(sorted(bindings.items())),
        },
        "decoded_instructions": sum(instructions.values()),
        "decoded_instruction_bytes": sum(
            disassembly["instruction_bytes"].values()
        ),
        "instruction_families": dict(instructions.most_common()),
        "instruction_family_bytes": dict(
            disassembly["instruction_bytes"].most_common()
        ),
        "movement_operand_classes": {
            key: {
                "instructions": disassembly["operand_classes"][key],
                "bytes": disassembly["operand_class_bytes"][key],
            }
            for key in sorted(disassembly["operand_classes"])
        },
        "largest_function_bodies": [
            {"name": name, **metrics}
            for name, metrics in sorted(
                disassembly["functions"].items(),
                key=lambda item: (-item[1]["bytes"], item[0]),
            )[:top_functions]
        ],
        "selected_function_sizes": {
            pattern: max(
                (size for name, size in function_sizes.items() if pattern in name),
                default=0,
            )
            for pattern in selected_functions
        },
        "selected_function_bodies": {
            pattern: {
                "instructions": sum(
                    metrics["instructions"]
                    for name, metrics in disassembly["functions"].items()
                    if pattern in name
                ),
                "bytes": sum(
                    metrics["bytes"]
                    for name, metrics in disassembly["functions"].items()
                    if pattern in name
                ),
                "instruction_bytes": dict(collections.Counter({
                    mnemonic: total
                    for mnemonic in disassembly["instruction_bytes"]
                    for total in [sum(
                        metrics["instruction_bytes"].get(mnemonic, 0)
                        for name, metrics in disassembly["functions"].items()
                        if pattern in name
                    )]
                    if total
                }).most_common()),
            }
            for pattern in selected_functions
        },
        "selected_call_targets": {
            pattern: sum(
                count for name, count in call_targets.items() if pattern in name
            )
            for pattern in selected_calls
        },
        "selected_callers": {
            pattern: [
                {"name": name, "calls": count}
                for name, count in sorted(
                    (
                        (name, sum(
                            count
                            for target, count in metrics["call_targets"].items()
                            if pattern in target
                        ))
                        for name, metrics in disassembly["functions"].items()
                    ),
                    key=lambda item: (-item[1], item[0]),
                )
                if count
            ]
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
    print(
        "  decoded_instructions=%d decoded_instruction_bytes=%d"
        % (report["decoded_instructions"], report["decoded_instruction_bytes"])
    )
    if report["lsda_call_sites"]:
        print("  lsda_call_sites=%s" % report["lsda_call_sites"])
    movement = report["movement_operand_classes"]
    if movement:
        print(
            "  movement_operand_classes=%s"
            % {
                key: "%d/%dB" % (value["instructions"], value["bytes"])
                for key, value in movement.items()
            }
        )
    if report["selected_function_sizes"]:
        print("  selected_function_sizes=%s" % report["selected_function_sizes"])
        print("  selected_function_bodies=%s" % report["selected_function_bodies"])
    if report["selected_call_targets"]:
        print("  selected_call_targets=%s" % report["selected_call_targets"])


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("object", nargs="+")
    parser.add_argument("--function", action="append", default=[])
    parser.add_argument("--call-target", action="append", default=[])
    parser.add_argument("--top-functions", type=int, default=20)
    parser.add_argument("--json")
    parser.add_argument("--readelf", default="readelf")
    parser.add_argument("--objdump", default="objdump")
    return parser.parse_args()


def main():
    args = parse_args()
    tools = {"readelf": args.readelf, "objdump": args.objdump}
    reports = [
        report_object(
            Path(value), args.function, args.call_target,
            max(0, args.top_functions), tools,
        )
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
