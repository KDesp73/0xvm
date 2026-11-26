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


class Scope:
    """Local slots for one function frame."""

    def __init__(self):
        self.names: dict[str, int] = {}
        self.hidden_count = 0
        self.for_limits: dict[int, str] = {}

    def declare(self, name: str) -> int:
        if name not in self.names:
            self.names[name] = len(self.names)
        return self.names[name]

    def index(self, name: str) -> int:
        if name not in self.names:
            raise CompileError(f'unknown local {name!r}')
        return self.names[name]

    def hidden(self, prefix: str) -> str:
        self.hidden_count += 1
        name = f'__{prefix}_{self.hidden_count}'
        self.declare(name)
        return name

    @property
    def size(self) -> int:
        return len(self.names)


class Codegen:
    def __init__(self, program: list, source_name: str):
        self.program = program
        self.source_name = source_name
        self.lines: list[str] = []
        self.strings: dict[str, tuple[str, int]] = {}
        self.label_count = 0
        self.scope = Scope()
        self.in_function = False
        self.functions: dict[str, int] = {}

        for statement in program:
            if isinstance(statement, nodes.Function):
                if statement.name in self.functions:
                    raise CompileError(f'duplicate function {statement.name!r}')
                self.functions[statement.name] = len(statement.params)

    @staticmethod
    def function_label(name: str) -> str:
        return f'__lua_fn_{name}'

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

    def collect(self, statements: list, scope: Scope) -> None:
        for statement in statements:
            if isinstance(statement, nodes.Local):
                scope.declare(statement.name)
            elif isinstance(statement, nodes.For):
                scope.declare(statement.name)
                scope.for_limits[id(statement)] = scope.hidden('for_limit')
            elif isinstance(statement, nodes.If):
                self.collect(statement.then_body, scope)
                self.collect(statement.else_body, scope)
            elif isinstance(statement, nodes.While):
                self.collect(statement.body, scope)
            elif isinstance(statement, nodes.Function):
                raise CompileError('nested functions are not supported')

    def require_numeric(self, expr) -> None:
        if isinstance(expr, nodes.Str):
            raise CompileError('strings are only supported as print arguments')
        if isinstance(expr, nodes.Unary):
            self.require_numeric(expr.operand)
        elif isinstance(expr, nodes.Binary):
            self.require_numeric(expr.left)
            self.require_numeric(expr.right)

    def emit_call(self, call: nodes.Call, keep_result: bool) -> None:
        argc = self.functions.get(call.name)
        if argc is None:
            raise CompileError(f'unknown function {call.name!r}')
        if len(call.args) != argc:
            raise CompileError(
                f'function {call.name!r} expects {argc} argument(s), got {len(call.args)}'
            )

        for arg in call.args:
            self.emit_num(arg)
        self.emit_instr(f'CALL {self.function_label(call.name)}')

        if keep_result:
            # After RET the stack holds [args..., result]. Remove the arguments
            # while keeping the result on top.
            for _ in range(argc):
                self.emit_instr('SWAP')
                self.emit_instr('POP')
        else:
            for _ in range(argc + 1):
                self.emit_instr('POP')

    def emit_num(self, expr) -> None:
        self.require_numeric(expr)
        if isinstance(expr, nodes.Int):
            self.emit_instr(f'PUSH {expr.value}')
        elif isinstance(expr, nodes.Var):
            self.emit_instr(f'LOCAL_GET {self.scope.index(expr.name)}')
        elif isinstance(expr, nodes.Call):
            self.emit_call(expr, keep_result=True)
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
        if isinstance(statement, nodes.Function):
            raise CompileError('nested functions are not supported')

        if isinstance(statement, nodes.Return):
            if not self.in_function:
                raise CompileError('return outside function')
            if statement.expr is None:
                self.emit_instr('PUSH 0')
            else:
                self.emit_num(statement.expr)
            self.emit_instr('RET')
            return

        if isinstance(statement, (nodes.Local, nodes.Assign)):
            self.emit_num(statement.expr)
            self.emit_instr(f'LOCAL_SET {self.scope.index(statement.name)}')
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

        if isinstance(statement, nodes.Call):
            self.emit_call(statement, keep_result=False)
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
            limit_name = self.scope.for_limits[id(statement)]
            var = self.scope.index(statement.name)
            limit = self.scope.index(limit_name)
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

    def emit_function(self, function: nodes.Function) -> None:
        scope = Scope()
        for param in function.params:
            scope.declare(param)
        self.collect(function.body, scope)

        self.emit()
        self.emit(f'{self.function_label(function.name)}:')
        self.scope = scope
        self.in_function = True
        self.emit_instr(f'ENTER {scope.size}')
        for index, param in enumerate(function.params):
            self.emit_instr(f'PUSH {scope.size + len(function.params) - index - 1}')
            self.emit_instr('PICK')
            self.emit_instr(f'LOCAL_SET {index}')

        for statement in function.body:
            self.emit_statement(statement)

        self.emit_instr('PUSH 0')
        self.emit_instr('RET')
        self.in_function = False

    def generate(self) -> str:
        self.emit(f'; Generated by luac from {self.source_name}')
        self.emit('INCLUDE "console.asm"')
        self.emit()

        top_statements = [
            statement for statement in self.program
            if not isinstance(statement, nodes.Function)
        ]
        main_scope = Scope()
        self.collect(top_statements, main_scope)
        self.scope = main_scope
        self.in_function = False
        self.emit_instr(f'ENTER {main_scope.size}')
        for statement in top_statements:
            self.emit_statement(statement)
        self.emit_instr('HALT')

        for statement in self.program:
            if isinstance(statement, nodes.Function):
                self.emit_function(statement)

        if self.strings:
            self.emit()
            for value, (label, _) in self.strings.items():
                self.emit(f'{label}: STR "{asm_escape(value)}"')

        return '\n'.join(self.lines) + '\n'
