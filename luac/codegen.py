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

BITWISE_OPS = {
    '&': 'AND',
    '|': 'OR',
    '~': 'XOR',
    '<<': 'SHL',
    '>>': 'SHR',
}


MASK64 = (1 << 64) - 1
INT64_MIN = -(1 << 63)

DATA_WIDTHS = {'DB': 1, 'DW': 4, 'DQ': 8}
LOAD_OPS = {1: 'LOAD8', 4: 'LOAD32', 8: 'LOAD64'}
STORE_OPS = {1: 'STORE8', 4: 'STORE32', 8: 'STORE64'}

ARRAY_CONSTRUCTORS = {
    'byte_array': ('DB', 1),
    'word_array': ('DW', 4),
}

HEAP_CONSTRUCTORS = {
    'alloc_bytes': ('DB', 1),
    'alloc_words': ('DW', 4),
}

INTRINSIC_ARITY = {
    'screen_size': 0,
    'set_color': 1,
    'plot': 2,
    'draw': 0,
    'clear': 0,
    'read_key': 0,
    'quit_requested': 0,
    'now_ms': 0,
    'sleep': 1,
}

INTRINSIC_INCLUDE = {
    'screen_size': 'screen.asm',
    'set_color': 'screen.asm',
    'plot': 'screen.asm',
    'draw': 'screen.asm',
    'clear': 'screen.asm',
    'read_key': 'controller.asm',
    'quit_requested': 'controller.asm',
    'now_ms': 'timer.asm',
}

VALUE_INTRINSICS = {'read_key', 'quit_requested', 'now_ms'}


def wrap64(value: int) -> int:
    """Match the VM's signed 64-bit arithmetic."""
    value &= MASK64
    return value if value < (1 << 63) else value - (1 << 64)


def trunc_div(left: int, right: int) -> int:
    """C-style integer division with the VM's INT64_MIN guard."""
    if right == -1:
        return INT64_MIN
    quotient = abs(left) // abs(right)
    return -quotient if (left < 0) != (right < 0) else quotient


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
        self.arrays: dict[str, tuple[str, int, int | None, int | None]] = {}
        self.hidden_count = 0
        self.for_limits: dict[int, str] = {}

    def declare(
        self,
        name: str,
        array: tuple[str, int, int | None, int | None] | None = None,
    ) -> int:
        if name not in self.names:
            self.names[name] = len(self.names)
        if array is not None:
            self.arrays[name] = array
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
        self.arrays: list[tuple[str, str, int]] = []
        self.constants: dict[str, int] = {}
        self.static_data: dict[str, tuple[str, str, int]] = {}
        self.data_items: list[tuple[str, str, list[int]]] = []
        self.includes: set[str] = set()
        self.label_count = 0
        self.scope = Scope()
        self.in_function = False
        self.functions: dict[str, int] = {}

        for statement in program:
            if isinstance(statement, nodes.Function):
                if statement.name in INTRINSIC_ARITY:
                    raise CompileError(
                        f'cannot redefine intrinsic {statement.name!r}'
                    )
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

    def scan_expr(self, expr) -> None:
        """Register stdlib includes needed by intrinsics in an expression."""
        if isinstance(expr, nodes.Call):
            include = INTRINSIC_INCLUDE.get(expr.name)
            if include:
                self.includes.add(include)
            for arg in expr.args:
                self.scan_expr(arg)
        elif isinstance(expr, nodes.Unary):
            self.scan_expr(expr.operand)
        elif isinstance(expr, nodes.Binary):
            self.scan_expr(expr.left)
            self.scan_expr(expr.right)
        elif isinstance(expr, nodes.Index):
            self.scan_expr(expr.base)
            self.scan_expr(expr.index)
        elif isinstance(expr, nodes.Data):
            for value in expr.values:
                self.scan_expr(value)

    def scan_statements(self, statements: list) -> None:
        for statement in statements:
            if isinstance(statement, (nodes.Local, nodes.LocalMulti,
                                     nodes.Assign, nodes.Print, nodes.Return,
                                     nodes.Const)):
                if statement.expr is not None:
                    self.scan_expr(statement.expr)
            elif isinstance(statement, nodes.AssignIndex):
                self.scan_expr(statement.target.base)
                self.scan_expr(statement.target.index)
                self.scan_expr(statement.expr)
            elif isinstance(statement, nodes.Call):
                self.scan_expr(statement)
            elif isinstance(statement, nodes.If):
                self.scan_expr(statement.cond)
                self.scan_statements(statement.then_body)
                self.scan_statements(statement.else_body)
            elif isinstance(statement, nodes.While):
                self.scan_expr(statement.cond)
                self.scan_statements(statement.body)
            elif isinstance(statement, nodes.For):
                self.scan_expr(statement.start)
                self.scan_expr(statement.limit)
                self.scan_statements(statement.body)
            elif isinstance(statement, nodes.Function):
                self.scan_statements(statement.body)

    def check_intrinsic(self, call: nodes.Call) -> None:
        expected = INTRINSIC_ARITY.get(call.name)
        if expected is None:
            raise CompileError(f'not an intrinsic: {call.name!r}')
        if len(call.args) != expected:
            raise CompileError(
                f'intrinsic {call.name!r} expects {expected} argument(s), '
                f'got {len(call.args)}'
            )

    def emit_intrinsic_expr(self, call: nodes.Call) -> None:
        self.check_intrinsic(call)
        name = call.name
        if name == 'read_key':
            self.emit_instr('READ_KEY')
        elif name == 'quit_requested':
            self.emit_instr('QUIT_REQUESTED')
        elif name == 'now_ms':
            self.emit_instr('NOW_MS')
        elif name == 'screen_size':
            raise CompileError('screen_size requires two local targets')
        else:
            raise CompileError(f'{name} does not produce a value')

    def emit_intrinsic_statement(self, call: nodes.Call) -> None:
        self.check_intrinsic(call)
        name = call.name
        if name == 'set_color':
            self.emit_num(call.args[0])
            self.emit_instr('SET_COLOR')
        elif name == 'plot':
            self.emit_num(call.args[0])
            self.emit_num(call.args[1])
            self.emit_instr('SWAP')
            self.emit_instr('PUSH 16')
            self.emit_instr('SHL')
            self.emit_instr('SWAP')
            self.emit_instr('OR')
            self.emit_instr('PLOT')
        elif name == 'draw':
            self.emit_instr('DRAW')
        elif name == 'clear':
            self.emit_instr('CLEAR')
        elif name == 'sleep':
            self.emit_num(call.args[0])
            self.emit_instr('DEO 0x41')
        elif name in VALUE_INTRINSICS:
            self.emit_num(call)
            self.emit_instr('POP')
        else:
            raise CompileError('screen_size requires two local targets')

    def emit_screen_size(self, statement: nodes.LocalMulti) -> None:
        expr = statement.expr
        if not isinstance(expr, nodes.Call) or expr.name != 'screen_size':
            raise CompileError(
                'multiple local targets are only supported for screen_size()'
            )
        self.check_intrinsic(expr)
        self.emit_instr('SCREEN_SIZE')
        for name in reversed(statement.names):
            self.emit_instr(f'LOCAL_SET {self.scope.index(name)}')

    def intern_string(self, value: str) -> str:
        if value not in self.strings:
            label = f'__lua_str_{len(self.strings)}'
            self.strings[value] = (label, len(value))
        return self.strings[value][0]

    def const_value(self, expr) -> int | None:
        """Fold an expression to an integer, or return None if it is runtime."""
        if isinstance(expr, nodes.Int):
            return expr.value
        if isinstance(expr, nodes.Var) and expr.name in self.constants:
            return self.constants[expr.name]
        if isinstance(expr, nodes.Unary):
            operand = self.const_value(expr.operand)
            if operand is None:
                return None
            if expr.op == '-':
                return wrap64(-operand)
            if expr.op == '~':
                return wrap64(~operand)
            return None
        if isinstance(expr, nodes.Binary):
            left = self.const_value(expr.left)
            right = self.const_value(expr.right)
            if left is None or right is None:
                return None
            if expr.op == '+':
                return wrap64(left + right)
            if expr.op == '-':
                return wrap64(left - right)
            if expr.op == '*':
                return wrap64(left * right)
            if expr.op in ('/', '%'):
                if right == 0:
                    raise CompileError('division by zero in constant expression')
                quotient = trunc_div(left, right)
                return wrap64(quotient if expr.op == '/' else left - quotient * right)
            if expr.op == '&':
                return wrap64(left & right)
            if expr.op == '|':
                return wrap64(left | right)
            if expr.op == '~':
                return wrap64(left ^ right)
            if expr.op == '<<':
                return wrap64((left & MASK64) << (right & 63))
            if expr.op == '>>':
                return wrap64((left & MASK64) >> (right & 63))
            comparisons = {
                '==': left == right,
                '~=': left != right,
                '<': left < right,
                '<=': left <= right,
                '>': left > right,
                '>=': left >= right,
            }
            if expr.op in comparisons:
                return int(comparisons[expr.op])
        return None

    def require_const(self, expr, context: str) -> int:
        value = self.const_value(expr)
        if value is None:
            raise CompileError(f'{context} requires a compile-time integer')
        return value

    def prepare_declarations(self, statements: list) -> None:
        for statement in statements:
            name = statement.name
            if (name in self.constants or name in self.static_data or
                    name in self.functions):
                raise CompileError(f'duplicate constant {name!r}')
            context = f'const {name!r}'
            if isinstance(statement.expr, nodes.Data):
                if not statement.expr.values:
                    raise CompileError(f'{context} needs at least one data value')
                values = [
                    self.require_const(value, context)
                    for value in statement.expr.values
                ]
                label = f'__lua_data_{len(self.data_items)}'
                self.data_items.append((label, statement.expr.directive, values))
                self.static_data[name] = (
                    label, statement.expr.directive, len(values),
                )
            else:
                self.constants[name] = self.require_const(statement.expr, context)

    def ensure_mutable(self, name: str) -> None:
        if name in self.constants or name in self.static_data:
            raise CompileError(f'cannot assign to constant {name!r}')

    def emit_data_value(self, directive: str, value: int) -> None:
        if directive == 'DB':
            if not -128 <= value <= 255:
                raise CompileError(f'byte data value {value} out of range')
            self.emit_instr(f'DB {value}')
        elif directive == 'DW':
            if not -(1 << 31) <= value <= (1 << 32) - 1:
                raise CompileError(f'word data value {value} out of range')
            word = value & 0xFFFFFFFF
            self.emit_instr(f'DW {(word >> 16) & 0xFFFF}')
            self.emit_instr(f'DW {word & 0xFFFF}')
        else:
            self.emit_instr(f'DQ {wrap64(value)}')

    def emit_array_alloc(
        self, call: nodes.Call
    ) -> tuple[str, int, int, int | None]:
        directive, width = ARRAY_CONSTRUCTORS[call.name]
        if len(call.args) != 1 or not isinstance(call.args[0], nodes.Int):
            raise CompileError(f'{call.name} requires a literal element count')
        size = call.args[0].value
        if size <= 0:
            raise CompileError(f'{call.name} size must be positive')
        label = f'__lua_arr_{len(self.arrays)}'
        self.arrays.append((label, directive, size))
        self.emit_instr(f'PUSH {label}')
        return directive, width, size, 0

    def emit_heap_alloc(
        self, call: nodes.Call
    ) -> tuple[str, int, int | None, int | None]:
        directive, width = HEAP_CONSTRUCTORS[call.name]
        if len(call.args) != 1:
            raise CompileError(f'{call.name} expects one size argument')
        count = self.const_value(call.args[0])
        if count is not None and count <= 0:
            raise CompileError(f'{call.name} size must be positive')
        if count is None:
            self.emit_num(call.args[0])
            if width == 4:
                self.emit_instr('PUSH 4')
                self.emit_instr('MUL')
        else:
            self.emit_instr(f'PUSH {count * width}')
        self.emit_instr('ALLOC')
        return directive, width, count, 0

    @staticmethod
    def shift_pointer(
        ref: tuple[str, int, int | None, int | None],
        delta: int | None,
    ) -> tuple[str, int, int | None, int | None]:
        directive, width, count, offset = ref
        if offset is None or delta is None:
            offset = None
        else:
            offset += delta
        return directive, width, count, offset

    def pointer_descriptor(
        self, expr
    ) -> tuple[str, int, int | None, int | None] | None:
        if isinstance(expr, nodes.Var):
            if expr.name in self.scope.arrays:
                return self.scope.arrays[expr.name]
            if expr.name in self.static_data:
                _, directive, count = self.static_data[expr.name]
                return (directive, DATA_WIDTHS[directive], count, 0)
        if isinstance(expr, nodes.Binary) and expr.op in ('+', '-'):
            left = self.pointer_descriptor(expr.left)
            right = self.pointer_descriptor(expr.right)
            if expr.op == '+':
                if left is not None and right is None:
                    return self.shift_pointer(left, self.const_value(expr.right))
                if right is not None and left is None:
                    return self.shift_pointer(right, self.const_value(expr.left))
            if expr.op == '-' and left is not None and right is None:
                delta = self.const_value(expr.right)
                return self.shift_pointer(left, None if delta is None else -delta)
        return None

    def array_descriptor(
        self, base
    ) -> tuple[str, int, int | None, int | None]:
        ref = self.pointer_descriptor(base)
        if ref is None:
            raise CompileError('indexing requires an array or allocated pointer')
        return ref

    def check_index(self, index, ref) -> None:
        _, width, count, offset = ref
        index_value = self.const_value(index)
        if count is None or offset is None or index_value is None:
            return
        byte_offset = offset + index_value * width
        if not 0 <= byte_offset < count * width:
            raise CompileError(
                f'array index {index_value} out of range for pointer offset {offset}'
            )

    def emit_index_address(self, index: nodes.Index) -> tuple[str, int]:
        ref = self.array_descriptor(index.base)
        self.check_index(index.index, ref)
        directive, width, _, _ = ref
        self.emit_num(index.base)
        self.emit_num(index.index)
        if width > 1:
            self.emit_instr(f'PUSH {width}')
            self.emit_instr('MUL')
        self.emit_instr('ADD')
        return directive, width

    def collect(self, statements: list, scope: Scope) -> None:
        for statement in statements:
            if isinstance(statement, nodes.Local):
                scope.declare(statement.name)
            elif isinstance(statement, nodes.LocalMulti):
                for name in statement.names:
                    scope.declare(name)
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
            name = expr.name
            if name in self.constants:
                self.emit_instr(f'PUSH {self.constants[name]}')
            elif name in self.static_data:
                self.emit_instr(f'PUSH {self.static_data[name][0]}')
            else:
                self.emit_instr(f'LOCAL_GET {self.scope.index(name)}')
        elif isinstance(expr, nodes.Call):
            if expr.name in ARRAY_CONSTRUCTORS:
                self.emit_array_alloc(expr)
            elif expr.name in HEAP_CONSTRUCTORS:
                self.emit_heap_alloc(expr)
            elif expr.name == 'free':
                raise CompileError('free is a statement, not an expression')
            elif expr.name in INTRINSIC_ARITY:
                self.emit_intrinsic_expr(expr)
            else:
                self.emit_call(expr, keep_result=True)
        elif isinstance(expr, nodes.Index):
            _, width = self.emit_index_address(expr)
            self.emit_instr(LOAD_OPS[width])
        elif isinstance(expr, nodes.Unary):
            if expr.op == '-':
                self.emit_instr('PUSH 0')
                self.emit_num(expr.operand)
                self.emit_instr('SUB')
            elif expr.op == '~':
                self.emit_num(expr.operand)
                self.emit_instr('NOT')
            else:
                raise CompileError(f'unsupported unary operator {expr.op!r}')
        elif isinstance(expr, nodes.Binary):
            self.emit_num(expr.left)
            self.emit_num(expr.right)
            op = (COMPARE_OPS.get(expr.op) or ARITH_OPS.get(expr.op) or
                  BITWISE_OPS.get(expr.op))
            if op is None:
                raise CompileError(f'unsupported operator {expr.op!r}')
            self.emit_instr(op)
        else:
            raise CompileError(f'unsupported expression {type(expr).__name__}')

    def emit_statement(self, statement) -> None:
        if isinstance(statement, nodes.Function):
            raise CompileError('nested functions are not supported')
        if isinstance(statement, nodes.Const):
            raise CompileError('const declarations are only supported at top level')

        if isinstance(statement, nodes.Return):
            if not self.in_function:
                raise CompileError('return outside function')
            if statement.expr is None:
                self.emit_instr('PUSH 0')
            else:
                self.emit_num(statement.expr)
            self.emit_instr('RET')
            return

        if isinstance(statement, nodes.LocalMulti):
            for name in statement.names:
                self.ensure_mutable(name)
            self.emit_screen_size(statement)
            return

        if isinstance(statement, nodes.Local):
            self.ensure_mutable(statement.name)
            array = None
            if (isinstance(statement.expr, nodes.Call) and
                    statement.expr.name in ARRAY_CONSTRUCTORS):
                array = self.emit_array_alloc(statement.expr)
            elif (isinstance(statement.expr, nodes.Call) and
                  statement.expr.name in HEAP_CONSTRUCTORS):
                array = self.emit_heap_alloc(statement.expr)
            else:
                self.emit_num(statement.expr)
                array = self.pointer_descriptor(statement.expr)
            self.scope.declare(statement.name, array)
            self.emit_instr(f'LOCAL_SET {self.scope.index(statement.name)}')
            return

        if isinstance(statement, nodes.Assign):
            self.ensure_mutable(statement.name)
            self.emit_num(statement.expr)
            self.emit_instr(f'LOCAL_SET {self.scope.index(statement.name)}')
            array = self.pointer_descriptor(statement.expr)
            if array is None:
                self.scope.arrays.pop(statement.name, None)
            else:
                self.scope.arrays[statement.name] = array
            return

        if isinstance(statement, nodes.AssignIndex):
            self.emit_num(statement.expr)
            _, width = self.emit_index_address(statement.target)
            self.emit_instr(STORE_OPS[width])
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
            if statement.name == 'free':
                if len(statement.args) != 1:
                    raise CompileError('free expects one pointer argument')
                pointer = statement.args[0]
                if (not isinstance(pointer, nodes.Var) or
                        pointer.name not in self.scope.arrays):
                    raise CompileError('free expects an allocated pointer variable')
                self.emit_num(pointer)
                self.emit_instr('FREE')
                del self.scope.arrays[pointer.name]
                return
            if statement.name in INTRINSIC_ARITY:
                self.emit_intrinsic_statement(statement)
                return
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
        self.scan_statements(self.program)
        self.prepare_declarations([
            statement for statement in self.program
            if isinstance(statement, nodes.Const)
        ])

        self.emit(f'; Generated by luac from {self.source_name}')
        self.emit('INCLUDE "console.asm"')
        for include in ('screen.asm', 'controller.asm', 'timer.asm'):
            if include in self.includes:
                self.emit(f'INCLUDE "{include}"')
        if self.constants:
            self.emit()
            for name, value in self.constants.items():
                self.emit(f'EQU {name} {value}')
        self.emit()

        top_statements = [
            statement for statement in self.program
            if not isinstance(statement, (nodes.Function, nodes.Const))
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

        if self.arrays or self.data_items:
            self.emit()
            for label, directive, size in self.arrays:
                if directive == 'DW':
                    self.emit('ALIGN 4')
                self.emit(f'{label}:')
                for _ in range(size):
                    self.emit_data_value(directive, 0)
            for label, directive, values in self.data_items:
                if directive == 'DW':
                    self.emit('ALIGN 4')
                elif directive == 'DQ':
                    self.emit('ALIGN 8')
                self.emit(f'{label}:')
                for value in values:
                    self.emit_data_value(directive, value)

        if self.strings:
            self.emit()
            for value, (label, _) in self.strings.items():
                self.emit(f'{label}: STR "{asm_escape(value)}"')

        return '\n'.join(self.lines) + '\n'
