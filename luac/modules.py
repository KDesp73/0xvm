"""Resolve and expand ``import`` statements for the Lua subset."""

from pathlib import Path

from . import nodes
from .lexer import CompileError
from .parser import Parser


def resolve_import(path_text: str, base_dir: Path) -> Path:
    """Resolve an import path relative to the importing file."""
    path = Path(path_text)
    if path.suffix.lower() != '.lua':
        path = path.with_suffix('.lua')
    if not path.is_absolute():
        path = base_dir / path
    return path.resolve()


def load_program(path: Path | str, *, seen: set[Path] | None = None,
                 stack: tuple[Path, ...] = ()) -> list:
    """Parse ``path`` and splice imported modules into its statement list."""
    path = Path(path).resolve()
    if seen is None:
        seen = set()

    if not path.is_file():
        context = f'import from {stack[-1].name}' if stack else 'source'
        raise CompileError(f'{context} not found: {path}')
    if path in seen:
        raise CompileError(f'duplicate import: {path.name}')

    seen.add(path)
    try:
        source = path.read_text()
        program = Parser.from_source(source).parse_program()
    except OSError as err:
        raise CompileError(f'cannot read {path}: {err}') from err
    except CompileError as err:
        raise CompileError(f'{path.name}: {err}') from err

    expanded: list = []
    for statement in program:
        if not isinstance(statement, nodes.Import):
            expanded.append(statement)
            continue

        child = resolve_import(statement.path, path.parent)
        if not child.is_file():
            raise CompileError(
                f'line {statement.line}: import {statement.path!r} not found '
                f'in {path.name} ({child})'
            )
        if child in stack or child == path:
            chain = list((*stack, path, child))
            if child in stack:
                chain = chain[chain.index(child):] + [child]
            names = ' -> '.join(item.name for item in chain)
            raise CompileError(
                f'line {statement.line}: circular import: {names}'
            )
        if child in seen:
            raise CompileError(
                f'line {statement.line}: duplicate import '
                f'{statement.path!r} in {path.name}'
            )

        expanded.extend(load_program(child, seen=seen, stack=(*stack, path)))

    return expanded
