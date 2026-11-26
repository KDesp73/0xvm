# SPDX-License-Identifier: MIT
"""Command-line interface for the Lua-subset compiler."""

import argparse
import sys
from pathlib import Path

from .codegen import Codegen
from .lexer import CompileError
from .modules import display_name, load_program
from .parser import Parser


def compile_source(source: str, source_name: str) -> str:
    """Compile an in-memory source string; imports require ``compile_file``."""
    program = Parser.from_source(source).parse_program()
    return Codegen(program, source_name).generate()


def write_line_map(path: str | Path, entries: list[tuple[int, str, int]]) -> None:
    """Write a sidecar map from generated assembly lines to Lua lines."""
    output = Path(path)
    text = ['# luac source map v1', '# asm_line\tlua_file\tlua_line']
    for asm_line, source, line in entries:
        safe_source = source.replace('\t', ' ').replace('\n', ' ')
        text.append(f'{asm_line}\t{safe_source}\t{line}')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(text) + '\n')


def compile_file(source: str | Path, map_path: str | Path | None = None) -> str:
    """Compile a source file, expanding imports relative to that file."""
    path = Path(source).resolve()
    program = load_program(path)
    codegen = Codegen(program, display_name(path))
    asm = codegen.generate()
    if map_path is not None:
        write_line_map(map_path, codegen.line_map)
    return asm


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog='0xluac', description='Compile a Lua subset to 0xasm source')
    sub = ap.add_subparsers(dest='cmd', required=True)
    compile_cmd = sub.add_parser('compile', help='compile SOURCE to 0xasm')
    compile_cmd.add_argument('source')
    compile_cmd.add_argument('-o', '--output', required=True)
    compile_cmd.add_argument('-m', '--map', help='write a Lua source-line map')
    args = ap.parse_args(argv)

    source_path = Path(args.source)
    try:
        asm = compile_file(source_path, args.map)
    except (CompileError, OSError) as err:
        print(f'0xluac: {err}', file=sys.stderr)
        return 1

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(asm)
    return 0
