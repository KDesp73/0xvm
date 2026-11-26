"""Tokenizer for the supported Lua subset."""

from dataclasses import dataclass


class CompileError(Exception):
    """Raised for invalid source in the supported subset."""


@dataclass(frozen=True)
class Token:
    kind: str
    text: str
    line: int
    value: object = None


KEYWORDS = {
    'local', 'print', 'if', 'then', 'else', 'end', 'while', 'do', 'for',
    'function', 'return',
}

TWO_CHAR_OPS = {'==', '~=', '<=', '>=', '<<', '>>'}
ONE_CHAR_OPS = set('=+-*/%(),<>[]&|~')


def _unescape(text: str, line: int) -> str:
    out = []
    i = 0
    simple = {'n': '\n', 'r': '\r', 't': '\t', '\\': '\\', '"': '"', "'": "'"}
    while i < len(text):
        ch = text[i]
        if ch != '\\':
            out.append(ch)
            i += 1
            continue
        i += 1
        if i >= len(text):
            raise CompileError(f'line {line}: unterminated escape in string')
        ch = text[i]
        if ch == 'x':
            try:
                out.append(chr(int(text[i + 1:i + 3], 16)))
            except (ValueError, IndexError):
                raise CompileError(f'line {line}: invalid \\x escape in string')
            i += 3
        elif ch in simple:
            out.append(simple[ch])
            i += 1
        else:
            raise CompileError(f'line {line}: unknown escape \\{ch}')
    return ''.join(out)


def tokenize(source: str) -> list[Token]:
    tokens = []
    i = 0
    line = 1
    n = len(source)

    while i < n:
        ch = source[i]

        if ch == '\n':
            line += 1
            i += 1
            continue
        if ch.isspace():
            i += 1
            continue
        if source.startswith('--', i):
            while i < n and source[i] != '\n':
                i += 1
            continue

        if ch.isalpha() or ch == '_':
            start = i
            while i < n and (source[i].isalnum() or source[i] == '_'):
                i += 1
            word = source[start:i]
            kind = 'KEYWORD' if word in KEYWORDS else 'IDENT'
            tokens.append(Token(kind, word, line))
            continue

        if ch.isdigit():
            start = i
            if ch == '0' and i + 1 < n and source[i + 1] in 'xX':
                i += 2
                digits = i
                while i < n and source[i] in '0123456789abcdefABCDEF':
                    i += 1
                if i == digits:
                    raise CompileError(f'line {line}: invalid hex literal')
                text = source[start:i]
                tokens.append(Token('INT', text, line, int(text[2:], 16)))
                continue
            while i < n and source[i].isdigit():
                i += 1
            tokens.append(Token('INT', source[start:i], line,
                                int(source[start:i], 10)))
            continue

        if ch in ('"', "'"):
            quote = ch
            start_line = line
            i += 1
            buf = []
            while i < n and source[i] != quote:
                if source[i] == '\n':
                    line += 1
                buf.append(source[i])
                i += 1
            if i >= n:
                raise CompileError(f'line {start_line}: unterminated string')
            i += 1
            tokens.append(Token('STRING', 'string', start_line,
                                _unescape(''.join(buf), start_line)))
            continue

        pair = source[i:i + 2]
        if pair in TWO_CHAR_OPS:
            tokens.append(Token('OP', pair, line))
            i += 2
            continue
        if ch in ONE_CHAR_OPS:
            tokens.append(Token('OP', ch, line))
            i += 1
            continue

        raise CompileError(f'line {line}: unexpected character {ch!r}')

    tokens.append(Token('EOF', '<eof>', line))
    return tokens
