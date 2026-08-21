#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "report_elf_code_shape.py"
SPEC = importlib.util.spec_from_file_location("report_elf_code_shape", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ElfCodeShapeParserTest(unittest.TestCase):
    def test_sections_and_relocations(self):
        sections = MODULE.parse_sections(
            "  [ 1] .text PROGBITS 0000000000000000 000040 00002a 00 AX 0 0 16\n"
            "  [ 2] .rela.text RELA 0000000000000000 000070 000030 18 I 4 1 8\n"
        )
        self.assertEqual(42, sections[0]["size"])
        self.assertEqual("RELA", sections[1]["type"])
        self.assertEqual(
            1,
            MODULE.parse_relocation_count(
                "000000000001  000200000004 R_X86_64_PLT32 0 foo - 4\n"
            ),
        )

    def test_symbols_and_instruction_families(self):
        functions = MODULE.parse_symbols(
            "  12: 0000000000000000    37 FUNC    WEAK   DEFAULT    3 thing\n"
            "  14: 0000000000000000  0x2a FUNC    LOCAL  DEFAULT    3 large\n"
            "  13: 0000000000000000     0 FUNC    GLOBAL DEFAULT  UND missing\n"
        )
        self.assertEqual(
            [
                {"name": "thing", "size": 37, "binding": "WEAK", "section": "3"},
                {"name": "large", "size": 42, "binding": "LOCAL", "section": "3"},
            ],
            functions,
        )
        instructions, calls = MODULE.parse_disassembly(
            "  10: 48 89 e5              mov    %rsp,%rbp\n"
            "  13: e8 00 00 00 00        call   18 <target>\n"
            "      14: R_X86_64_PLT32 external-0x4\n"
            "  18: e8 00 00 00 00        call   1d <direct>\n"
        )
        self.assertEqual({"mov": 1, "call": 2}, dict(instructions))
        self.assertEqual({"external-0x4": 1, "direct": 1}, dict(calls))

    def test_instruction_bytes_operand_classes_and_functions(self):
        details = MODULE.parse_disassembly_details(
            "0000000000000000 <sample()>:\n"
            "   0: 48 89 e5              mov    %rsp,%rbp\n"
            "   3: 8b 45 fc              mov    -0x4(%rbp),%eax\n"
            "   6: c7 45 f8 01 00 00 00  movl   $0x1,-0x8(%rbp)\n"
            "   d: 48 8d 45 f0           lea    -0x10(%rbp),%rax\n"
        )
        self.assertEqual(4, sum(details["instructions"].values()))
        self.assertEqual(17, sum(details["instruction_bytes"].values()))
        self.assertEqual(1, details["operand_classes"]["mov:register_to_register"])
        self.assertEqual(1, details["operand_classes"]["mov:memory_to_register"])
        self.assertEqual(1, details["operand_classes"]["mov:immediate_to_memory"])
        self.assertEqual(1, details["operand_classes"]["lea:address_to_register"])
        self.assertEqual(17, details["functions"]["sample()"]["bytes"])

    def test_inline_call_relocation_is_attributed_to_function(self):
        details = MODULE.parse_disassembly_details(
            "0000000000000000 <caller()>:\n"
            "   0: e8 00 00 00 00 call 5 <caller()+0x5> "
            "1: R_X86_64_PLT32 target-0x4\n"
        )
        self.assertEqual(1, details["call_targets"]["target-0x4"])
        self.assertEqual(
            1, details["functions"]["caller()"]["call_targets"]["target-0x4"]
        )

    def test_lsda_call_site_shape(self):
        relocations = (
            "Relocation section '.rela.eh_frame' at offset 0x100:\n"
            "00000019  00000002 R_X86_64_PC32 0 .gcc_except_table + 0\n"
            "00000039  00000002 R_X86_64_PC32 0 .gcc_except_table + c\n"
        )
        hex_dump = (
            "Hex dump of section '.gcc_except_table':\n"
            "  0x00000000 ffff0108 02010000 05010900  .........\n"
            "  0x0000000c ffff0104 03010000           ........\n"
        )
        sections = MODULE.parse_hex_sections(
            hex_dump, {".gcc_except_table": 20}
        )
        self.assertEqual(
            {".gcc_except_table": [0, 12]},
            MODULE.parse_lsda_relocations(relocations),
        )
        self.assertEqual(
            {
                "lsdas": 2,
                "protected_bytes": 4,
                "protected_records": 1,
                "table_bytes": 12,
                "unprotected_bytes": 8,
                "unprotected_records": 2,
            },
            MODULE.parse_lsda_call_sites(
                sections, MODULE.parse_lsda_relocations(relocations)
            ),
        )


if __name__ == "__main__":
    unittest.main()
