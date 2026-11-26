"""Recursive-descent parser for the supported Lua subset."""

from .lexer import CompileError, Token, tokenize
from . import nodes


DATA_DIRECTIVES = {
    'bytes': 'DB',
    'words': 'DW',
    'quads': 'DQ',
}


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

    @staticmethod
    def located(node, token: Token):
        node.line = token.line
        return node

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

    def parse_args(self) -> list:
        args = []
        if not self.at('OP', ')'):
            args.append(self.parse_expr())
            while self.accept('OP', ','):
                args.append(self.parse_expr())
        self.expect('OP', ')')
        return args

    def parse_if_tail(self, token: Token):
        """Parse an `if`/`elseif` chain; the keyword is already consumed."""
        branches = []
        cond = self.parse_expr()
        self.expect('KEYWORD', 'then')
        branches.append((token, cond, self.parse_block('elseif', 'else', 'end')))

        while self.at('KEYWORD', 'elseif'):
            branch_token = self.advance()
            cond = self.parse_expr()
            self.expect('KEYWORD', 'then')
            branches.append(
                (branch_token, cond, self.parse_block('elseif', 'else', 'end'))
            )

        else_body = []
        if self.accept('KEYWORD', 'else'):
            else_body = self.parse_block('end')
        self.expect('KEYWORD', 'end')

        branch_token, cond, body = branches[-1]
        node = self.located(nodes.If(cond, body, else_body), branch_token)
        for branch_token, cond, body in reversed(branches[:-1]):
            node = self.located(nodes.If(cond, body, [node]), branch_token)
        return node

    def parse_statement(self):
        if self.accept('KEYWORD', 'import'):
            token = self.expect('STRING')
            return self.located(nodes.Import(token.value), token)

        if self.accept('KEYWORD', 'local'):
            token = self.tokens[self.pos - 1]
            names = [self.expect('IDENT').text]
            while self.accept('OP', ','):
                names.append(self.expect('IDENT').text)
            self.expect('OP', '=')
            expr = self.parse_expr()
            if len(names) == 1:
                return self.located(nodes.Local(names[0], expr), token)
            return self.located(nodes.LocalMulti(names, expr), token)

        if self.accept('KEYWORD', 'const'):
            token = self.tokens[self.pos - 1]
            name = self.expect('IDENT').text
            self.expect('OP', '=')
            return self.located(nodes.Const(name, self.parse_expr()), token)

        if self.at('KEYWORD', 'print'):
            token = self.advance()
            self.expect('OP', '(')
            expr = self.parse_expr()
            self.expect('OP', ')')
            return self.located(nodes.Print(expr), token)

        if self.at('KEYWORD', 'if'):
            token = self.advance()
            return self.parse_if_tail(token)

        if self.at('KEYWORD', 'while'):
            token = self.advance()
            cond = self.parse_expr()
            self.expect('KEYWORD', 'do')
            body = self.parse_block('end')
            self.expect('KEYWORD', 'end')
            return self.located(nodes.While(cond, body), token)

        if self.at('KEYWORD', 'for'):
            token = self.advance()
            name = self.expect('IDENT').text
            self.expect('OP', '=')
            start = self.parse_expr()
            self.expect('OP', ',')
            limit = self.parse_expr()
            self.expect('KEYWORD', 'do')
            body = self.parse_block('end')
            self.expect('KEYWORD', 'end')
            return self.located(nodes.For(name, start, limit, body), token)

        if self.accept('KEYWORD', 'function'):
            token = self.tokens[self.pos - 1]
            name = self.expect('IDENT').text
            self.expect('OP', '(')
            params = []
            if not self.at('OP', ')'):
                params.append(self.expect('IDENT').text)
                while self.accept('OP', ','):
                    params.append(self.expect('IDENT').text)
            self.expect('OP', ')')
            body = self.parse_block('end')
            self.expect('KEYWORD', 'end')
            return self.located(nodes.Function(name, params, body), token)

        if self.accept('KEYWORD', 'return'):
            token = self.tokens[self.pos - 1]
            if (self.at('KEYWORD', 'end') or self.at('KEYWORD', 'else') or
                    self.at('KEYWORD', 'elseif') or self.at('EOF')):
                return self.located(nodes.Return(), token)
            return self.located(nodes.Return(self.parse_expr()), token)

        if self.accept('KEYWORD', 'break'):
            token = self.tokens[self.pos - 1]
            return self.located(nodes.Break(), token)

        if self.accept('KEYWORD', 'continue'):
            token = self.tokens[self.pos - 1]
            return self.located(nodes.Continue(), token)

        if self.at('IDENT'):
            token = self.advance()
            name = token.text
            if self.accept('OP', '('):
                return self.located(nodes.Call(name, self.parse_args()), token)
            target = nodes.Var(name)
            while self.accept('OP', '['):
                index = self.parse_expr()
                self.expect('OP', ']')
                target = nodes.Index(target, index)
            self.expect('OP', '=')
            if isinstance(target, nodes.Index):
                return self.located(
                    nodes.AssignIndex(target, self.parse_expr()), token
                )
            return self.located(nodes.Assign(name, self.parse_expr()), token)

        token = self.peek()
        raise CompileError(f'line {token.line}: unexpected statement starting with {token.text}')

    def parse_expr(self):
        return self.parse_comparison()

    def parse_comparison(self):
        node = self.parse_bit_or()
        while self.at('OP') and self.peek().text in ('==', '~=', '<', '<=', '>', '>='):
            op = self.advance().text
            node = nodes.Binary(op, node, self.parse_bit_or())
        return node

    def parse_bit_or(self):
        node = self.parse_bit_xor()
        while self.at('OP', '|'):
            self.advance()
            node = nodes.Binary('|', node, self.parse_bit_xor())
        return node

    def parse_bit_xor(self):
        node = self.parse_bit_and()
        while self.at('OP', '~'):
            self.advance()
            node = nodes.Binary('~', node, self.parse_bit_and())
        return node

    def parse_bit_and(self):
        node = self.parse_shift()
        while self.at('OP', '&'):
            self.advance()
            node = nodes.Binary('&', node, self.parse_shift())
        return node

    def parse_shift(self):
        node = self.parse_additive()
        while self.at('OP') and self.peek().text in ('<<', '>>'):
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
        if self.at('OP', '-') or self.at('OP', '~'):
            op = self.advance().text
            return nodes.Unary(op, self.parse_unary())
        return self.parse_primary()

    def parse_primary(self):
        token = self.peek()
        if self.accept('INT'):
            node = nodes.Int(token.value)
        elif self.accept('STRING'):
            node = nodes.Str(token.value)
        elif self.accept('IDENT'):
            if self.accept('OP', '('):
                node = nodes.Call(token.text, self.parse_args())
            elif token.text in DATA_DIRECTIVES and self.accept('OP', '{'):
                values = []
                if not self.at('OP', '}'):
                    values.append(self.parse_expr())
                    while self.accept('OP', ','):
                        values.append(self.parse_expr())
                self.expect('OP', '}')
                node = nodes.Data(DATA_DIRECTIVES[token.text], values)
            else:
                node = nodes.Var(token.text)
        elif self.accept('OP', '('):
            node = self.parse_expr()
            self.expect('OP', ')')
        else:
            raise CompileError(f'line {token.line}: expected expression, got {token.text}')

        while self.accept('OP', '['):
            index = self.parse_expr()
            self.expect('OP', ']')
            node = nodes.Index(node, index)
        return node
