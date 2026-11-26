#!/usr/bin/env python3
"""Unit checks for the Lua-subset compiler front end."""

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from luac import nodes  # noqa: E402
from luac.cli import compile_file, compile_source  # noqa: E402
from luac.lexer import CompileError, tokenize  # noqa: E402
from luac.parser import Parser  # noqa: E402


class LuacTests(unittest.TestCase):
    @staticmethod
    def write_source(directory: Path, name: str, text: str) -> Path:
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

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

    def test_string_assignment_uses_descriptor_pair(self):
        asm = compile_source('local s = "ok"\nprint(s)', 'string.lua')
        self.assertIn('ENTER 2', asm)
        self.assertIn(
            '    PUSH __lua_str_0\n    PUSH 2\n'
            '    LOCAL_SET 1\n    LOCAL_SET 0',
            asm,
        )
        self.assertIn('    LOCAL_GET 0\n    LOCAL_GET 1\n    PRINT_STR', asm)
        self.assertIn('__lua_str_0: STR "ok"', asm)

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

    def test_lexer_recognizes_const_and_data_braces(self):
        tokens = tokenize('const table = bytes { 1, 0xFF }')
        self.assertEqual([t.text for t in tokens],
                         ['const', 'table', '=', 'bytes', '{', '1', ',',
                          '0xFF', '}', '<eof>'])

    def test_parser_builds_const_and_data_nodes(self):
        program = Parser.from_source(
            'const N = 2 + 3\nconst row = bytes { 1, N }'
        ).parse_program()
        self.assertIsInstance(program[0], nodes.Const)
        self.assertEqual(program[0].expr.op, '+')
        self.assertIsInstance(program[1].expr, nodes.Data)
        self.assertEqual(program[1].expr.directive, 'DB')
        self.assertEqual(len(program[1].expr.values), 2)

    def test_codegen_emits_equ_and_initialized_data(self):
        asm = compile_source(
            'const N = 0xFF\n'
            'const row = bytes { N, 2 }\n'
            'const vals = words { 0x11223344, -1 }\n'
            'const big = quads { -1 }\n'
            'print(N)\n'
            'print(row[0])\n'
            'print(vals[0])\n'
            'print(big[0])',
            'data.lua',
        )
        self.assertIn('EQU N 255', asm)
        self.assertIn('PUSH 255\n    PRINT', asm)
        self.assertIn('__lua_data_0:\n    DB 255\n    DB 2', asm)
        self.assertIn('DW 4386\n    DW 13124', asm)
        self.assertIn('DW 65535\n    DW 65535', asm)
        self.assertIn('ALIGN 8\n__lua_data_2:\n    DQ -1', asm)
        self.assertIn('LOAD64', asm)

    def test_const_cannot_be_reassigned(self):
        with self.assertRaises(CompileError):
            compile_source('const N = 1\nN = 2', 'bad.lua')

    def test_const_initializer_must_be_constant(self):
        with self.assertRaises(CompileError):
            compile_source('local x = 1\nconst N = x', 'bad.lua')

    def test_const_data_index_bounds_are_checked(self):
        with self.assertRaises(CompileError):
            compile_source('const row = bytes { 1, 2 }\nprint(row[2])',
                           'bad.lua')

    def test_const_in_function_is_rejected(self):
        with self.assertRaises(CompileError):
            compile_source(
                'function f()\n const N = 1\n return N\nend',
                'bad.lua',
            )

    def test_parser_accepts_multi_local_screen_size(self):
        program = Parser.from_source('local w, h = screen_size()').parse_program()
        self.assertIsInstance(program[0], nodes.LocalMulti)
        self.assertEqual(program[0].names, ['w', 'h'])
        self.assertEqual(program[0].expr.name, 'screen_size')

    def test_codegen_emits_device_intrinsics(self):
        asm = compile_source(
            'local w, h = screen_size()\n'
            'set_color(0xFFC040)\n'
            'plot(1, 2)\n'
            'draw()\n'
            'clear()\n'
            'print(read_key())\n'
            'print(quit_requested())\n'
            'print(now_ms())\n'
            'sleep(20)',
            'devices.lua',
        )
        for include in ('screen.asm', 'controller.asm', 'timer.asm'):
            self.assertIn(f'INCLUDE "{include}"', asm)
        self.assertIn('    SCREEN_SIZE\n    LOCAL_SET 1\n    LOCAL_SET 0', asm)
        self.assertIn('    PUSH 16760896\n    SET_COLOR', asm)
        self.assertIn(
            '    SWAP\n    PUSH 16\n    SHL\n    SWAP\n    OR\n    PLOT', asm
        )
        self.assertIn('    DRAW\n    CLEAR\n', asm)
        self.assertIn('    READ_KEY\n    PRINT', asm)
        self.assertIn('    QUIT_REQUESTED\n    PRINT', asm)
        self.assertIn('    NOW_MS\n    PRINT', asm)
        self.assertIn('    PUSH 20\n    DEO 0x41', asm)

    def test_value_intrinsic_statement_discards_result(self):
        asm = compile_source('read_key()', 'devices.lua')
        self.assertIn('    READ_KEY\n    POP', asm)

    def test_screen_size_requires_two_targets(self):
        for source in ('local x = screen_size()', 'screen_size()'):
            with self.subTest(source=source), self.assertRaises(CompileError):
                compile_source(source, 'bad.lua')

    def test_multi_local_only_supports_screen_size(self):
        with self.assertRaises(CompileError):
            compile_source('local a, b = now_ms()', 'bad.lua')

    def test_intrinsic_arity_is_checked(self):
        with self.assertRaises(CompileError):
            compile_source('plot(1)', 'bad.lua')

    def test_void_intrinsic_is_rejected_as_expression(self):
        with self.assertRaises(CompileError):
            compile_source('local x = clear()', 'bad.lua')

    def test_intrinsic_cannot_be_redefined_as_function(self):
        with self.assertRaises(CompileError):
            compile_source(
                'function draw()\n return 1\nend\nprint(draw())',
                'bad.lua',
            )

    def test_codegen_emits_string_bindings(self):
        asm = compile_source(
            'local title = "0XVM"\n'
            'print(title)\n'
            'print(strlen(title))\n'
            'print(title[0])\n'
            'local part = slice(title, 1, 2)\n'
            'print(part)\n'
            'print_str(part)\n'
            'print(strcmp(title, "0XVM"))\n'
            'print(strfind(title, "X"))\n'
            'print(strchr(title, 86))\n'
            'local buf = alloc_bytes(2)\n'
            'strcpy(buf, part)\n'
            'free(buf)',
            'strings.lua',
        )
        self.assertIn('INCLUDE "strings.asm"', asm)
        self.assertIn('    ENTER 5', asm)
        self.assertIn('    STRCMP\n    PRINT', asm)
        self.assertIn('    STRFIND\n    PRINT', asm)
        self.assertIn('    STRCHR\n    PRINT', asm)
        self.assertIn('    STRCPY\n', asm)
        self.assertIn(
            '    LOCAL_GET 0\n    LOCAL_GET 1\n'
            '    PUSH 0\n    SWAP\n    POP\n    ADD\n    LOAD8',
            asm,
        )
        self.assertIn('__lua_str_0: STR "0XVM"', asm)

    def test_slice_clamps_start_and_count(self):
        asm = compile_source(
            'local s = slice("0XVM TYPE", 5, 99)\nprint(strlen(s))',
            'strings.lua',
        )
        for instruction in ('CMP_LT', 'CMP_LE', 'PICK', 'ROT', 'SWAP'):
            self.assertIn(instruction, asm)
        self.assertIn('    PUSH 5\n    SUB', asm)
        self.assertIn('    PUSH 99', asm)

    def test_string_value_cannot_be_used_as_integer(self):
        for source in ('local s = "x"\nprint(s + 1)',
                       'local n = 1\nn = "x"'):
            with self.subTest(source=source), self.assertRaises(CompileError):
                compile_source(source, 'bad.lua')

    def test_string_variable_requires_string_assignment(self):
        with self.assertRaises(CompileError):
            compile_source('local s = "x"\ns = 1', 'bad.lua')

    def test_string_index_is_bounds_checked_and_read_only(self):
        with self.assertRaises(CompileError):
            compile_source('local s = "abc"\nprint(s[3])', 'bad.lua')
        with self.assertRaises(CompileError):
            compile_source('local s = "abc"\ns[0] = 65', 'bad.lua')

    def test_string_binding_arity_is_checked(self):
        with self.assertRaises(CompileError):
            compile_source('print(strlen("a", "b"))', 'bad.lua')

    def test_void_string_binding_is_rejected_as_expression(self):
        for source in ('local n = print_str("a")', 'print(strcpy(1, "a"))'):
            with self.subTest(source=source), self.assertRaises(CompileError):
                compile_source(source, 'bad.lua')

    def test_slice_offsets_must_be_constant_and_nonnegative(self):
        for source in ('local n = 1\nprint(slice("abc", n, 1))',
                       'print(slice("abc", -1, 1))',
                       'print(slice("abc", 0, -1))'):
            with self.subTest(source=source), self.assertRaises(CompileError):
                compile_source(source, 'bad.lua')

    def test_string_return_values_are_rejected(self):
        with self.assertRaises(CompileError):
            compile_source(
                'function f()\n return "x"\nend\nprint(f())',
                'bad.lua',
            )

    def test_lexer_recognizes_import(self):
        tokens = tokenize('import "font"')
        self.assertEqual([t.kind for t in tokens], ['KEYWORD', 'STRING', 'EOF'])
        self.assertEqual(tokens[1].value, 'font')

    def test_parser_builds_import_node(self):
        statement = Parser.from_source('import "layout"').parse_program()[0]
        self.assertIsInstance(statement, nodes.Import)
        self.assertEqual(statement.path, 'layout')
        self.assertEqual(statement.line, 1)

    def test_compile_file_expands_nested_modules(self):
        fixture = Path(__file__).resolve().parent / 'lua' / 'modules.lua'
        asm = compile_file(fixture)
        self.assertEqual(asm.count('    ENTER '), 3)
        self.assertIn('EQU BASE 10', asm)
        self.assertIn('__lua_fn_twice:', asm)
        self.assertIn('__lua_fn_total:', asm)
        self.assertIn('__lua_data_0:\n    DB 2\n    DB 4\n    DB 6', asm)
        self.assertIn('__lua_str_0: STR "MODULES"', asm)
        self.assertIn('__lua_str_1: STR "LAYOUT"', asm)

    def test_import_with_lua_extension_is_resolved(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_source(root, 'helper.lua', 'print("HELPER")')
            main = self.write_source(root, 'main.lua', 'import "helper.lua"')
            asm = compile_file(main)
        self.assertIn('__lua_str_0: STR "HELPER"', asm)

    def test_unresolved_import_is_rejected(self):
        for source in ('import "x"',
                       'function f()\n import "x"\n return 1\nend'):
            with self.subTest(source=source), self.assertRaises(CompileError):
                compile_source(source, 'bad.lua')

    def test_missing_import_names_the_importing_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            main = self.write_source(root, 'main.lua', 'import "nope"')
            with self.assertRaises(CompileError) as caught:
                compile_file(main)
        self.assertIn('line 1', str(caught.exception))
        self.assertIn('main.lua', str(caught.exception))
        self.assertIn('not found', str(caught.exception))

    def test_duplicate_import_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_source(root, 'helper.lua', 'print("A")')
            main = self.write_source(
                root, 'main.lua', 'import "helper"\nimport "helper"'
            )
            with self.assertRaises(CompileError) as caught:
                compile_file(main)
        self.assertIn('line 2', str(caught.exception))
        self.assertIn('duplicate import', str(caught.exception))

    def test_circular_import_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = self.write_source(root, 'a.lua', 'import "b"')
            self.write_source(root, 'b.lua', 'import "a"')
            with self.assertRaises(CompileError) as caught:
                compile_file(first)
        self.assertIn('circular import', str(caught.exception))
        self.assertIn('a.lua -> b.lua -> a.lua', str(caught.exception))

    def test_duplicate_symbol_across_modules_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_source(
                root, 'helper.lua', 'function f()\n return 1\nend'
            )
            main = self.write_source(
                root,
                'main.lua',
                'import "helper"\nfunction f()\n return 2\nend',
            )
            with self.assertRaises(CompileError) as caught:
                compile_file(main)
        self.assertIn('duplicate function', str(caught.exception))

    def test_lexer_recognizes_elseif_break_and_continue(self):
        tokens = tokenize('elseif break continue')
        self.assertEqual([t.text for t in tokens],
                         ['elseif', 'break', 'continue', '<eof>'])
        self.assertTrue(all(t.kind == 'KEYWORD' for t in tokens[:-1]))

    def test_parser_builds_control_flow_nodes_with_lines(self):
        program = Parser.from_source(
            'if a == 1 then\n'
            '    break\n'
            'elseif a == 2 then\n'
            '    continue\n'
            'else\n'
            '    print(a)\n'
            'end'
        ).parse_program()
        outer = program[0]
        self.assertIsInstance(outer, nodes.If)
        self.assertEqual(outer.line, 1)
        self.assertIsInstance(outer.then_body[0], nodes.Break)
        self.assertEqual(outer.then_body[0].line, 2)
        nested = outer.else_body[0]
        self.assertIsInstance(nested, nodes.If)
        self.assertEqual(nested.line, 3)
        self.assertIsInstance(nested.then_body[0], nodes.Continue)
        self.assertEqual(nested.then_body[0].line, 4)

    def test_codegen_emits_break_continue_and_elseif(self):
        asm = compile_source(
            'local i = 0\n'
            'while i < 3 do\n'
            '    i = i + 1\n'
            '    if i == 2 then\n'
            '        continue\n'
            '    elseif i == 3 then\n'
            '        break\n'
            '    end\n'
            'end',
            'control.lua',
        )
        self.assertIn('    JMP __lua_while_1', asm)
        self.assertIn('    JMP __lua_endwhile_2', asm)
        self.assertIn('__lua_else_5:', asm)
        self.assertEqual(asm.count('    JMP_Z '), 3)

    def test_break_and_continue_require_a_loop(self):
        for statement in ('break', 'continue'):
            with self.subTest(statement=statement), self.assertRaises(CompileError):
                compile_source(statement, 'bad.lua')

    def test_continue_skips_to_for_step(self):
        asm = compile_source(
            'for i = 1, 3 do\n'
            '    if i == 2 then\n'
            '        continue\n'
            '    end\n'
            '    print(i)\n'
            'end',
            'control.lua',
        )
        self.assertRegex(asm, r'    JMP __lua_for_step_\d+\n')
        self.assertRegex(asm, r'__lua_for_step_\d+:\n    LOCAL_GET ')

    def test_golden_control_fixture(self):
        fixture = Path(__file__).resolve().parent / 'lua' / 'control.lua'
        asm = compile_file(fixture)
        self.assertIn('    JMP __lua_while_1', asm)
        self.assertIn('    JMP __lua_endwhile_2', asm)
        self.assertRegex(asm, r'    JMP __lua_for_step_\d+\n')
        self.assertIn('    LOCAL_GET 0\n    PRINT', asm)

    def test_compile_file_writes_lua_line_map(self):
        fixture = Path(__file__).resolve().parent / 'lua' / 'control.lua'
        with tempfile.TemporaryDirectory() as directory:
            map_path = Path(directory) / 'control.lumap'
            compile_file(fixture, map_path)
            text = map_path.read_text()
        self.assertIn('# luac source map v1', text)
        self.assertIn('\ttests/lua/control.lua\t9\n', text)
        self.assertIn('\ttests/lua/control.lua\t18\n', text)

    def test_line_map_tracks_imported_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.write_source(root, 'helper.lua', 'print("HELPER")')
            main = self.write_source(root, 'main.lua', 'import "helper"')
            map_path = root / 'main.lumap'
            compile_file(main, map_path)
            text = map_path.read_text()
        self.assertRegex(text, r'\t[^\t]*helper\.lua\t1\n')
        self.assertNotIn('\tbad.lua\t', text)

    def test_typography_example_compiles(self):
        source = Path(__file__).resolve().parents[1] / 'examples' / 'typography.lua'
        asm = compile_file(source)
        self.assertIn('INCLUDE "screen.asm"', asm)
        self.assertIn('INCLUDE "controller.asm"', asm)
        self.assertIn('INCLUDE "timer.asm"', asm)
        self.assertIn('__lua_fn_draw_glyph:', asm)
        self.assertIn('DQ -8608480292191076352', asm)
        self.assertIn('__lua_str_1: STR "TYPOGRAPHY OK"', asm)

    def test_typography_example_has_a_cli_safe_branch(self):
        source = Path(__file__).resolve().parents[1] / 'examples' / 'typography.lua'
        text = source.read_text().replace('const ENABLE = 1',
                                         'const ENABLE = 0', 1)
        asm = compile_source(text, 'typography.lua')
        self.assertIn('EQU ENABLE 0', asm)
        self.assertIn('__lua_str_1: STR "TYPOGRAPHY OK"', asm)

    def test_raycaster_example_compiles(self):
        source = Path(__file__).resolve().parents[1] / 'examples' / 'raycaster.lua'
        asm = compile_file(source)
        self.assertIn('INCLUDE "screen.asm"', asm)
        self.assertIn('INCLUDE "controller.asm"', asm)
        self.assertIn('INCLUDE "timer.asm"', asm)
        self.assertIn('__lua_fn_cast_ray:', asm)
        self.assertIn('__lua_fn_draw_minimap:', asm)
        self.assertIn('__lua_str_1: STR "RAYCASTER OK"', asm)
        self.assertIn('EQU ENABLE 1', asm)
        self.assertIn('    SCREEN_SIZE\n', asm)

    def test_raycaster_example_has_a_cli_safe_branch(self):
        source = Path(__file__).resolve().parents[1] / 'examples' / 'raycaster.lua'
        text = source.read_text().replace('const ENABLE = 1',
                                         'const ENABLE = 0', 1)
        asm = compile_source(text, 'raycaster.lua')
        self.assertIn('EQU ENABLE 0', asm)
        self.assertIn('__lua_str_1: STR "RAYCASTER OK"', asm)


if __name__ == '__main__':
    unittest.main()
