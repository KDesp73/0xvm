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

    def test_lexer_recognizes_function_keywords(self):
        tokens = tokenize('function f(x)\n return x\nend')
        self.assertEqual([t.text for t in tokens],
                         ['function', 'f', '(', 'x', ')', 'return', 'x',
                          'end', '<eof>'])

    def test_parser_accepts_functions_and_calls(self):
        program = Parser.from_source(
            'function f(a)\n return a\nend\nprint(f(1))'
        ).parse_program()
        function = program[0]
        self.assertIsInstance(function, nodes.Function)
        self.assertEqual(function.params, ['a'])
        self.assertIsInstance(function.body[0], nodes.Return)
        call = program[1].expr
        self.assertIsInstance(call, nodes.Call)
        self.assertEqual(call.name, 'f')

    def test_codegen_emits_function_abi(self):
        asm = compile_source(
            'function add(a, b)\n return a + b\nend\nprint(add(1, 2))',
            'fn.lua',
        )
        self.assertIn('__lua_fn_add:', asm)
        self.assertIn('ENTER 2', asm)
        self.assertIn('PUSH 3\n    PICK\n    LOCAL_SET 0', asm)
        self.assertIn(
            '    CALL __lua_fn_add\n'
            '    SWAP\n    POP\n'
            '    SWAP\n    POP\n'
            '    PRINT',
            asm,
        )
        self.assertIn('    RET\n', asm)

    def test_statement_call_discards_result_and_arguments(self):
        asm = compile_source(
            'function f(a, b)\n return a\nend\nf(1, 2)',
            'fn.lua',
        )
        self.assertIn(
            '    CALL __lua_fn_f\n'
            '    POP\n    POP\n    POP\n'
            '    HALT',
            asm,
        )

    def test_call_arity_is_checked(self):
        with self.assertRaises(CompileError):
            compile_source(
                'function f(a)\n return a\nend\nprint(f(1, 2))',
                'bad.lua',
            )

    def test_unknown_function_is_rejected(self):
        with self.assertRaises(CompileError):
            compile_source('print(nope(1))', 'bad.lua')

    def test_return_outside_function_is_rejected(self):
        with self.assertRaises(CompileError):
            compile_source('return 1', 'bad.lua')

    def test_nested_function_is_rejected(self):
        with self.assertRaises(CompileError):
            compile_source(
                'function outer()\n function inner()\n  return 1\n end\n return 2\nend',
                'bad.lua',
            )


if __name__ == '__main__':
    unittest.main()
