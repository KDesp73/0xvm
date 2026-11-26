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

    def test_lexer_accepts_index_brackets(self):
        tokens = tokenize('a[0] = 1')
        self.assertEqual([t.text for t in tokens],
                         ['a', '[', '0', ']', '=', '1', '<eof>'])

    def test_parser_builds_index_nodes(self):
        program = Parser.from_source(
            'local a = byte_array(2)\na[1] = 7\nprint(a[0])'
        ).parse_program()
        self.assertIsInstance(program[0].expr, nodes.Call)
        self.assertIsInstance(program[1], nodes.AssignIndex)
        self.assertIsInstance(program[1].target, nodes.Index)
        self.assertIsInstance(program[2].expr, nodes.Index)

    def test_codegen_emits_array_data_and_accesses(self):
        asm = compile_source(
            'local b = byte_array(2)\n'
            'b[0] = 1\n'
            'print(b[0])\n'
            'local w = word_array(2)\n'
            'w[1] = -1\n'
            'print(w[1])',
            'arr.lua',
        )
        self.assertIn('PUSH __lua_arr_0', asm)
        self.assertIn('STORE8', asm)
        self.assertIn('LOAD8', asm)
        self.assertIn('__lua_arr_0:\n    DB 0\n    DB 0', asm)
        self.assertIn('ALIGN 4\n__lua_arr_1:', asm)
        self.assertIn('PUSH 4\n    MUL', asm)
        self.assertIn('STORE32', asm)
        self.assertIn('LOAD32', asm)

    def test_static_array_bounds_are_checked(self):
        with self.assertRaises(CompileError):
            compile_source('local a = byte_array(2)\nprint(a[2])', 'bad.lua')

    def test_indexing_non_array_is_rejected(self):
        with self.assertRaises(CompileError):
            compile_source('local x = 1\nprint(x[0])', 'bad.lua')

    def test_codegen_emits_heap_allocation_and_free(self):
        asm = compile_source(
            'local p = alloc_bytes(2)\n'
            'p[0] = 7\n'
            'print(p[0])\n'
            'free(p)',
            'heap.lua',
        )
        self.assertIn('    PUSH 2\n    ALLOC', asm)
        self.assertIn('STORE8', asm)
        self.assertIn('LOAD8', asm)
        self.assertIn('    FREE\n', asm)
        self.assertNotIn('__lua_arr_0:', asm)

    def test_word_heap_allocation_scales_size_and_index(self):
        asm = compile_source('local w = alloc_words(2)\nprint(w[1])', 'heap.lua')
        self.assertIn('    PUSH 8\n    ALLOC', asm)
        self.assertIn('    PUSH 4\n    MUL', asm)
        self.assertIn('LOAD32', asm)

    def test_runtime_heap_size_is_allowed(self):
        asm = compile_source(
            'local n = 2\nlocal p = alloc_bytes(n)\nfree(p)',
            'heap.lua',
        )
        self.assertIn('    ALLOC', asm)

    def test_pointer_offset_is_bounds_checked(self):
        with self.assertRaises(CompileError):
            compile_source(
                'local p = alloc_bytes(2)\nlocal q = p + 1\nprint(q[2])',
                'bad.lua',
            )

    def test_free_requires_pointer_variable(self):
        with self.assertRaises(CompileError):
            compile_source('local p = alloc_bytes(2)\nfree(p + 1)', 'bad.lua')

    def test_freed_pointer_cannot_be_indexed(self):
        with self.assertRaises(CompileError):
            compile_source(
                'local p = alloc_bytes(2)\nfree(p)\nprint(p[0])',
                'bad.lua',
            )

    def test_lexer_parses_hex_and_bitwise_operators(self):
        tokens = tokenize('0xFF 10 0X2a a & b | c ~ d << 2 >> 1 ~= e')
        values = [t.value for t in tokens if t.kind == 'INT'][:3]
        self.assertEqual(values, [255, 10, 42])
        self.assertEqual([t.text for t in tokens],
                         ['0xFF', '10', '0X2a', 'a', '&', 'b', '|', 'c',
                          '~', 'd', '<<', '2', '>>', '1', '~=', 'e', '<eof>'])

    def test_invalid_hex_literal_is_rejected(self):
        with self.assertRaises(CompileError):
            tokenize('0x')

    def test_parser_uses_bitwise_precedence(self):
        and_expr = Parser.from_source('print(1 | 2 & 3)').parse_program()[0].expr
        self.assertEqual(and_expr.op, '|')
        self.assertIsInstance(and_expr.right, nodes.Binary)
        self.assertEqual(and_expr.right.op, '&')

        shift_expr = Parser.from_source('print(1 << 2 + 3)').parse_program()[0].expr
        self.assertEqual(shift_expr.op, '<<')
        self.assertEqual(shift_expr.right.op, '+')

        compare_expr = Parser.from_source('print(1 & 2 == 3)').parse_program()[0].expr
        self.assertEqual(compare_expr.op, '==')
        self.assertEqual(compare_expr.left.op, '&')

    def test_codegen_emits_bitwise_instructions(self):
        asm = compile_source(
            'print(0xFF)\n'
            'print(0xF0 & 0x3C)\n'
            'print(0xF0 | 0x0F)\n'
            'print(0xF0 ~ 0x0F)\n'
            'print(~0)\n'
            'print(1 << 4)\n'
            'print(16 >> 2)',
            'bits.lua',
        )
        for instruction in ('PUSH 255', 'AND', 'OR', 'XOR', 'NOT', 'SHL',
                            'SHR'):
            self.assertIn(instruction, asm)


if __name__ == '__main__':
    unittest.main()
