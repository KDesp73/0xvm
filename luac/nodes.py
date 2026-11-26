"""Small AST nodes for the supported Lua subset."""

from dataclasses import dataclass, field


@dataclass
class Int:
    value: int


@dataclass
class Str:
    value: str


@dataclass
class Data:
    directive: str
    values: list


@dataclass
class Const:
    name: str
    expr: object


@dataclass
class Import:
    path: str
    line: int = 0


@dataclass
class Var:
    name: str


@dataclass
class Unary:
    op: str
    operand: object


@dataclass
class Binary:
    op: str
    left: object
    right: object


@dataclass
class Call:
    name: str
    args: list


@dataclass
class Index:
    base: object
    index: object


@dataclass
class Local:
    name: str
    expr: object


@dataclass
class LocalMulti:
    names: list[str]
    expr: object


@dataclass
class Assign:
    name: str
    expr: object


@dataclass
class AssignIndex:
    target: Index
    expr: object


@dataclass
class Print:
    expr: object


@dataclass
class Return:
    expr: object | None = None


@dataclass
class If:
    cond: object
    then_body: list
    else_body: list = field(default_factory=list)


@dataclass
class While:
    cond: object
    body: list


@dataclass
class For:
    name: str
    start: object
    limit: object
    body: list


@dataclass
class Function:
    name: str
    params: list
    body: list
