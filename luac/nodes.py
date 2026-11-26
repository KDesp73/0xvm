"""Small AST nodes for the supported Lua subset."""

from dataclasses import dataclass, field
from typing import Optional


@dataclass
class Int:
    value: int


@dataclass
class Str:
    value: str


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
class Local:
    name: str
    expr: object


@dataclass
class Assign:
    name: str
    expr: object


@dataclass
class Print:
    expr: object


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
    limit_name: Optional[str] = None
