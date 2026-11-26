#!/usr/bin/env python3
"""Unit checks for the Lua-subset compiler front end."""

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from luac import nodes  # noqa: E402
from luac.cli import compile_source  # noqa: E402
from luac.lexer import CompileError, tokenize  # noqa: E402
from luac.parser import Parser  # noqa: E402


class LuacTests(unittest.TestCase):
    def test_lexer_skips_comments_and_keeps_lines(self):
        tokens = tokenize('local x = 1 -- hi\nprint(x)\n')
        self.assertEqual([t.kind for t in tokens],
                         ['KEYWORD', 'IDENT', 'OP', 'INT', 'KEYWORD', 'OP',
                          'IDENT', 'OP', 'EOF'])
        self.assertEqual(tokens[-1].line, 3)

    def test_parser_respects_precedence(self):
        program = Parser.from_source('print(1 + 2 * 3)').parse_program()
        expr = program[0].expr
        self.assertIsInstance(expr, nodes.Binary)
        self.assertEqual(expr.op, '+')
        self.assertIsInstance(expr.right, nodes.Binary)
        self.assertEqual(expr.right.op, '*')

    def test_codegen_emits_loops_and_strings(self):
        asm = compile_source(
            'local i = 0\nwhile i < 2 do\n print(i)\n i = i + 1\nend\nprint("ok")',
            'sample.lua',
        )
        self.assertIn('ENTER 1', asm)
        self.assertIn('CMP_LT', asm)
        self.assertIn('PRINT_STR', asm)
        self.assertIn(': STR "ok"', asm)
        self.assertIn('HALT', asm)

    def test_string_assignment_is_rejected(self):
        with self.assertRaises(CompileError):
            compile_source('local s = "no"', 'bad.lua')


if __name__ == '__main__':
    unittest.main()
