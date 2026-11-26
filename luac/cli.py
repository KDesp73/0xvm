"""Command-line interface for the Lua-subset compiler."""

import argparse
import sys
from pathlib import Path

from .codegen import Codegen
from .lexer import CompileError
from .modules import load_program
from .parser import Parser


def compile_source(source: str, source_name: str) -> str:
    """Compile an in-memory source string; imports require ``compile_file``."""
    program = Parser.from_source(source).parse_program()
    return Codegen(program, source_name).generate()


def compile_file(source: str | Path) -> str:
    """Compile a source file, expanding imports relative to that file."""
    path = Path(source).resolve()
    program = load_program(path)
    return Codegen(program, path.name).generate()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog='0xluac', description='Compile a Lua subset to 0xasm source')
    sub = ap.add_subparsers(dest='cmd', required=True)
    compile_cmd = sub.add_parser('compile', help='compile SOURCE to 0xasm')
    compile_cmd.add_argument('source')
    compile_cmd.add_argument('-o', '--output', required=True)
    args = ap.parse_args(argv)

    source_path = Path(args.source)
    try:
        asm = compile_file(source_path)
    except (CompileError, OSError) as err:
        print(f'0xluac: {err}', file=sys.stderr)
        return 1

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(asm)
    return 0
