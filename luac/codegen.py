"""Emit 0xasm source for the supported Lua subset."""

from .lexer import CompileError
from . import nodes

COMPARE_OPS = {
    '==': 'CMP_EQ',
    '~=': 'CMP_NE',
    '<': 'CMP_LT',
    '<=': 'CMP_LE',
    '>': 'CMP_GT',
    '>=': 'CMP_GE',
}

ARITH_OPS = {'+': 'ADD', '-': 'SUB', '*': 'MUL', '/': 'DIV', '%': 'MOD'}


def asm_escape(value: str) -> str:
    simple = {'\\': '\\\\', '"': '\\"', '\n': '\\n', '\r': '\\r', '\t': '\\t'}
    out = []
    for ch in value:
        if ch in simple:
            out.append(simple[ch])
        elif 32 <= ord(ch) <= 126:
            out.append(ch)
        else:
            out.append(f'\\x{ord(ch):02X}')
    return ''.join(out)


class Codegen:
    def __init__(self, program: list, source_name: str):
        self.program = program
        self.source_name = source_name
        self.lines: list[str] = []
        self.locals: dict[str, int] = {}
        self.strings: dict[str, tuple[str, int]] = {}
        self.label_count = 0

        for statement in program:
            self.collect_locals(statement)

    def collect_locals(self, statement) -> None:
        if isinstance(statement, nodes.Local):
            self.declare(statement.name)
            return
        if isinstance(statement, nodes.For):
            self.declare(statement.name)
            statement.limit_name = f'__for_limit_{len(self.locals)}'
            self.declare(statement.limit_name)
        for child in self.children(statement):
            if isinstance(child, list):
                for item in child:
                    self.collect_locals(item)
            else:
                self.collect_locals(child)

    @staticmethod
    def children(statement) -> list:
        if isinstance(statement, nodes.If):
            return [statement.then_body, statement.else_body]
        if isinstance(statement, (nodes.While, nodes.For)):
            return [statement.body]
        return []

    def declare(self, name: str) -> int:
        if name not in self.locals:
            self.locals[name] = len(self.locals)
        return self.locals[name]

    def local_index(self, name: str) -> int:
        if name not in self.locals:
            raise CompileError(f'unknown local {name!r}')
        return self.locals[name]

    def label(self, prefix: str) -> str:
        self.label_count += 1
        return f'__lua_{prefix}_{self.label_count}'

    def emit(self, text: str = '') -> None:
        self.lines.append(text)

    def emit_instr(self, text: str) -> None:
        self.lines.append(f'    {text}')

    def intern_string(self, value: str) -> str:
        if value not in self.strings:
            label = f'__lua_str_{len(self.strings)}'
            self.strings[value] = (label, len(value))
        return self.strings[value][0]

    def require_numeric(self, expr) -> None:
        if isinstance(expr, nodes.Str):
            raise CompileError('strings are only supported as print arguments')
        if isinstance(expr, nodes.Unary):
            self.require_numeric(expr.operand)
        elif isinstance(expr, nodes.Binary):
            self.require_numeric(expr.left)
            self.require_numeric(expr.right)

    def emit_num(self, expr) -> None:
        self.require_numeric(expr)
        if isinstance(expr, nodes.Int):
            self.emit_instr(f'PUSH {expr.value}')
        elif isinstance(expr, nodes.Var):
            self.emit_instr(f'LOCAL_GET {self.local_index(expr.name)}')
        elif isinstance(expr, nodes.Unary):
            self.emit_instr('PUSH 0')
            self.emit_num(expr.operand)
            self.emit_instr('SUB')
        elif isinstance(expr, nodes.Binary):
            self.emit_num(expr.left)
            self.emit_num(expr.right)
            op = COMPARE_OPS.get(expr.op) or ARITH_OPS.get(expr.op)
            if op is None:
                raise CompileError(f'unsupported operator {expr.op!r}')
            self.emit_instr(op)
        else:
            raise CompileError(f'unsupported expression {type(expr).__name__}')

    def emit_statement(self, statement) -> None:
        if isinstance(statement, (nodes.Local, nodes.Assign)):
            self.emit_num(statement.expr)
            self.emit_instr(f'LOCAL_SET {self.local_index(statement.name)}')
            return

        if isinstance(statement, nodes.Print):
            if isinstance(statement.expr, nodes.Str):
                label = self.intern_string(statement.expr.value)
                length = self.strings[statement.expr.value][1]
                self.emit_instr(f'PUSH {label}')
                self.emit_instr(f'PUSH {length}')
                self.emit_instr('PRINT_STR')
            else:
                self.emit_num(statement.expr)
                self.emit_instr('PRINT')
            return

        if isinstance(statement, nodes.If):
            false_label = self.label('else')
            end_label = self.label('endif')
            self.emit_num(statement.cond)
            self.emit_instr(f'JMP_Z {false_label}')
            for child in statement.then_body:
                self.emit_statement(child)
            self.emit_instr(f'JMP {end_label}')
            self.emit(false_label + ':')
            for child in statement.else_body:
                self.emit_statement(child)
            self.emit(end_label + ':')
            return

        if isinstance(statement, nodes.While):
            cond_label = self.label('while')
            end_label = self.label('endwhile')
            self.emit(cond_label + ':')
            self.emit_num(statement.cond)
            self.emit_instr(f'JMP_Z {end_label}')
            for child in statement.body:
                self.emit_statement(child)
            self.emit_instr(f'JMP {cond_label}')
            self.emit(end_label + ':')
            return

        if isinstance(statement, nodes.For):
            assert statement.limit_name is not None
            var = self.local_index(statement.name)
            limit = self.local_index(statement.limit_name)
            cond_label = self.label('for')
            end_label = self.label('endfor')
            self.emit_num(statement.start)
            self.emit_instr(f'LOCAL_SET {var}')
            self.emit_num(statement.limit)
            self.emit_instr(f'LOCAL_SET {limit}')
            self.emit(cond_label + ':')
            self.emit_instr(f'LOCAL_GET {var}')
            self.emit_instr(f'LOCAL_GET {limit}')
            self.emit_instr('CMP_LE')
            self.emit_instr(f'JMP_Z {end_label}')
            for child in statement.body:
                self.emit_statement(child)
            self.emit_instr(f'LOCAL_GET {var}')
            self.emit_instr('PUSH 1')
            self.emit_instr('ADD')
            self.emit_instr(f'LOCAL_SET {var}')
            self.emit_instr(f'JMP {cond_label}')
            self.emit(end_label + ':')
            return

        raise CompileError(f'unsupported statement {type(statement).__name__}')

    def generate(self) -> str:
        self.emit(f'; Generated by luac from {self.source_name}')
        self.emit('INCLUDE "console.asm"')
        self.emit()
        self.emit_instr(f'ENTER {len(self.locals)}')
        for statement in self.program:
            self.emit_statement(statement)
        self.emit_instr('HALT')

        if self.strings:
            self.emit()
            for value, (label, _) in self.strings.items():
                self.emit(f'{label}: STR "{asm_escape(value)}"')

        return '\n'.join(self.lines) + '\n'
