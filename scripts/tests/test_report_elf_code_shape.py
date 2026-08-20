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


if __name__ == "__main__":
    unittest.main()
