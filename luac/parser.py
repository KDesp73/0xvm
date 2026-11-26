"""Recursive-descent parser for the supported Lua subset."""

from .lexer import CompileError, Token, tokenize
from . import nodes


class Parser:
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.pos = 0

    @classmethod
    def from_source(cls, source: str) -> 'Parser':
        return cls(tokenize(source))

    def peek(self) -> Token:
        return self.tokens[self.pos]

    def advance(self) -> Token:
        token = self.tokens[self.pos]
        self.pos += 1
        return token

    def at(self, kind: str, text: str | None = None) -> bool:
        token = self.peek()
        return token.kind == kind and (text is None or token.text == text)

    def accept(self, kind: str, text: str | None = None) -> Token | None:
        if self.at(kind, text):
            return self.advance()
        return None

    def expect(self, kind: str, text: str | None = None) -> Token:
        token = self.peek()
        if not self.at(kind, text):
            want = text if text is not None else kind
            raise CompileError(f'line {token.line}: expected {want}, got {token.text}')
        return self.advance()

    def parse_program(self) -> list:
        statements = []
        while not self.at('EOF'):
            statements.append(self.parse_statement())
        return statements

    def parse_block(self, *terminators: str) -> list:
        statements = []
        while not any(self.at('KEYWORD', word) for word in terminators):
            if self.at('EOF'):
                token = self.peek()
                raise CompileError(f'line {token.line}: missing {" or ".join(terminators)}')
            statements.append(self.parse_statement())
        return statements

    def parse_statement(self):
        if self.accept('KEYWORD', 'local'):
            name = self.expect('IDENT').text
            self.expect('OP', '=')
            return nodes.Local(name, self.parse_expr())

        if self.at('KEYWORD', 'print'):
            self.advance()
            self.expect('OP', '(')
            expr = self.parse_expr()
            self.expect('OP', ')')
            return nodes.Print(expr)

        if self.at('KEYWORD', 'if'):
            self.advance()
            cond = self.parse_expr()
            self.expect('KEYWORD', 'then')
            then_body = self.parse_block('else', 'end')
            else_body = []
            if self.accept('KEYWORD', 'else'):
                else_body = self.parse_block('end')
            self.expect('KEYWORD', 'end')
            return nodes.If(cond, then_body, else_body)

        if self.at('KEYWORD', 'while'):
            self.advance()
            cond = self.parse_expr()
            self.expect('KEYWORD', 'do')
            body = self.parse_block('end')
            self.expect('KEYWORD', 'end')
            return nodes.While(cond, body)

        if self.at('KEYWORD', 'for'):
            self.advance()
            name = self.expect('IDENT').text
            self.expect('OP', '=')
            start = self.parse_expr()
            self.expect('OP', ',')
            limit = self.parse_expr()
            self.expect('KEYWORD', 'do')
            body = self.parse_block('end')
            self.expect('KEYWORD', 'end')
            return nodes.For(name, start, limit, body)

        if self.at('IDENT'):
            name = self.advance().text
            self.expect('OP', '=')
            return nodes.Assign(name, self.parse_expr())

        token = self.peek()
        raise CompileError(f'line {token.line}: unexpected statement starting with {token.text}')

    def parse_expr(self):
        return self.parse_comparison()

    def parse_comparison(self):
        node = self.parse_additive()
        while self.at('OP') and self.peek().text in ('==', '~=', '<', '<=', '>', '>='):
            op = self.advance().text
            node = nodes.Binary(op, node, self.parse_additive())
        return node

    def parse_additive(self):
        node = self.parse_multiplicative()
        while self.at('OP') and self.peek().text in ('+', '-'):
            op = self.advance().text
            node = nodes.Binary(op, node, self.parse_multiplicative())
        return node

    def parse_multiplicative(self):
        node = self.parse_unary()
        while self.at('OP') and self.peek().text in ('*', '/', '%'):
            op = self.advance().text
            node = nodes.Binary(op, node, self.parse_unary())
        return node

    def parse_unary(self):
        if self.at('OP', '-'):
            self.advance()
            return nodes.Unary('-', self.parse_unary())
        return self.parse_primary()

    def parse_primary(self):
        token = self.peek()
        if self.accept('INT'):
            return nodes.Int(token.value)
        if self.accept('STRING'):
            return nodes.Str(token.value)
        if self.accept('IDENT'):
            return nodes.Var(token.text)
        if self.accept('OP', '('):
            node = self.parse_expr()
            self.expect('OP', ')')
            return node
        raise CompileError(f'line {token.line}: expected expression, got {token.text}')
