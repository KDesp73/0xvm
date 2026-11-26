# SPDX-License-Identifier: MIT
"""Small AST nodes for the supported Lua subset."""

from dataclasses import dataclass, field


@dataclass(kw_only=True)
class Node:
    """Base node carrying source metadata for debug line maps."""

    line: int = 0
    file: str | None = None


@dataclass
class Int(Node):
    value: int


@dataclass
class Str(Node):
    value: str


@dataclass
class Data(Node):
    directive: str
    values: list


@dataclass
class Const(Node):
    name: str
    expr: object


@dataclass
class Import(Node):
    path: str


@dataclass
class Var(Node):
    name: str


@dataclass
class Unary(Node):
    op: str
    operand: object


@dataclass
class Binary(Node):
    op: str
    left: object
    right: object


@dataclass
class Call(Node):
    name: str
    args: list


@dataclass
class Index(Node):
    base: object
    index: object


@dataclass
class Local(Node):
    name: str
    expr: object


@dataclass
class LocalMulti(Node):
    names: list[str]
    expr: object


@dataclass
class Assign(Node):
    name: str
    expr: object


@dataclass
class AssignIndex(Node):
    target: Index
    expr: object


@dataclass
class Print(Node):
    expr: object


@dataclass
class Return(Node):
    expr: object | None = None


@dataclass
class Break(Node):
    pass


@dataclass
class Continue(Node):
    pass


@dataclass
class If(Node):
    cond: object
    then_body: list
    else_body: list = field(default_factory=list)


@dataclass
class While(Node):
    cond: object
    body: list


@dataclass
class For(Node):
    name: str
    start: object
    limit: object
    body: list


@dataclass
class Function(Node):
    name: str
    params: list
    body: list
