"""Lexer for Leda source code.

Tokenizes source text into a stream of Token objects with source position tracking.
Supports:
- Keywords (begin, end, function, etc.)
- Identifiers and reserved words
- Integer and floating-point literals
- String literals with escape sequences
- Operators and punctuation
- Comments (/* ... */ style)
"""

from dataclasses import dataclass
from enum import Enum
from typing import Iterable, Optional


class TokenKind(Enum):
    """Token kind enumeration."""
    # Literals
    INTEGER = "INTEGER"
    REAL = "REAL"
    STRING = "STRING"
    IDENTIFIER = "IDENTIFIER"
    
    # Keywords
    BEGIN = "BEGIN"
    END = "END"
    VAR = "VAR"
    CONST = "CONST"
    TYPE = "TYPE"
    FUNCTION = "FUNCTION"
    CLASS = "CLASS"
    IF = "IF"
    THEN = "THEN"
    ELSE = "ELSE"
    WHILE = "WHILE"
    DO = "DO"
    FOR = "FOR"
    TO = "TO"
    RETURN = "RETURN"
    INCLUDE = "INCLUDE"
    BYREF = "BYREF"
    BYNAME = "BYNAME"
    CFUNCTION = "CFUNCTION"
    DEFINED = "DEFINED"
    OF = "OF"
    IS = "IS"
    
    # Operators and punctuation
    ASSIGN = "ASSIGN"           # :=
    ARROW = "ARROW"             # ->
    LPAREN = "LPAREN"           # (
    RPAREN = "RPAREN"           # )
    LBRACKET = "LBRACKET"       # [
    RBRACKET = "RBRACKET"       # ]
    LBRACE = "LBRACE"           # {
    RBRACE = "RBRACE"           # }
    SEMICOLON = "SEMICOLON"     # ;
    COMMA = "COMMA"             # ,
    DOT = "DOT"                 # .
    COLON = "COLON"             # :
    
    # Arithmetic
    PLUS = "PLUS"               # +
    MINUS = "MINUS"             # -
    MULTIPLY = "MULTIPLY"       # *
    DIVIDE = "DIVIDE"           # /
    MODULO = "MODULO"           # %
    
    # Comparison
    EQ = "EQ"                   # =
    NEQ = "NEQ"                 # <>
    LT = "LT"                   # <
    LE = "LE"                   # <=
    GT = "GT"                   # >
    GE = "GE"                   # >=
    SAME_AS = "SAME_AS"         # ==
    NOT_SAME_AS = "NOT_SAME_AS" # ~=
    
    # Logical
    AND = "AND"                 # &
    OR = "OR"                   # |
    NOT = "NOT"                 # ~
    
    # Special
    EOF = "EOF"
    NEWLINE = "NEWLINE"


@dataclass(slots=True)
class Token:
    """Represents a single token in the source code."""
    kind: TokenKind
    text: str
    line: int
    col: int


class Lexer:
    """Tokenizes Leda source code."""
    
    KEYWORDS = {
        'begin': TokenKind.BEGIN,
        'end': TokenKind.END,
        'var': TokenKind.VAR,
        'const': TokenKind.CONST,
        'type': TokenKind.TYPE,
        'function': TokenKind.FUNCTION,
        'class': TokenKind.CLASS,
        'if': TokenKind.IF,
        'then': TokenKind.THEN,
        'else': TokenKind.ELSE,
        'while': TokenKind.WHILE,
        'do': TokenKind.DO,
        'for': TokenKind.FOR,
        'to': TokenKind.TO,
        'return': TokenKind.RETURN,
        'include': TokenKind.INCLUDE,
        'byRef': TokenKind.BYREF,
        'byName': TokenKind.BYNAME,
        'cfunction': TokenKind.CFUNCTION,
        'defined': TokenKind.DEFINED,
        'of': TokenKind.OF,
        'is': TokenKind.IS,
    }
    
    def __init__(self, source: str):
        self.source = source
        self.pos = 0
        self.line = 1
        self.col = 1
        self.tokens: list[Token] = []
    
    def error(self, msg: str) -> Exception:
        """Format a lexer error with position."""
        return SyntaxError(f"Lexer error at line {self.line}, col {self.col}: {msg}")
    
    def peek(self, offset: int = 0) -> Optional[str]:
        """Look ahead at a character without consuming it."""
        pos = self.pos + offset
        if pos < len(self.source):
            return self.source[pos]
        return None
    
    def advance(self) -> Optional[str]:
        """Consume and return the next character."""
        if self.pos < len(self.source):
            ch = self.source[self.pos]
            self.pos += 1
            if ch == '\n':
                self.line += 1
                self.col = 1
            else:
                self.col += 1
            return ch
        return None
    
    def skip_whitespace(self) -> None:
        """Skip whitespace (but not newlines)."""
        while self.peek() and self.peek() in ' \t\r\n':
            self.advance()
    
    def skip_comment(self) -> None:
        """Skip a /* ... */ comment."""
        if self.peek() == '{':
            self.advance()  # consume '{'
            depth = 1
            while depth > 0 and self.peek():
                if self.peek() == '{':
                    depth += 1
                elif self.peek() == '}':
                    depth -= 1
                self.advance()
    
    def read_string(self, quote: str) -> str:
        """Read a string literal (handles escape sequences)."""
        result = []
        self.advance()  # consume opening quote
        
        while True:
            ch = self.peek()
            if ch is None:
                raise self.error("Unterminated string literal")
            if ch == quote:
                self.advance()
                break
            if ch == '\\':
                self.advance()
                next_ch = self.advance()
                if next_ch == 'n':
                    result.append('\n')
                elif next_ch == 't':
                    result.append('\t')
                elif next_ch == 'r':
                    result.append('\r')
                elif next_ch == '\\':
                    result.append('\\')
                elif next_ch == quote:
                    result.append(quote)
                else:
                    result.append(next_ch)
            else:
                result.append(ch)
                self.advance()
        
        return ''.join(result)
    
    def read_number(self) -> Token:
        """Read an integer or floating-point literal."""
        start_line = self.line
        start_col = self.col
        digits = []
        
        while self.peek() and self.peek().isdigit():
            digits.append(self.advance())
        
        # Check for decimal point
        if self.peek() == '.' and self.peek(1) and self.peek(1).isdigit():
            digits.append(self.advance())  # consume '.'
            while self.peek() and self.peek().isdigit():
                digits.append(self.advance())
            return Token(TokenKind.REAL, ''.join(digits), start_line, start_col)
        
        return Token(TokenKind.INTEGER, ''.join(digits), start_line, start_col)
    
    def read_identifier(self) -> Token:
        """Read an identifier or keyword."""
        start_line = self.line
        start_col = self.col
        chars = []
        
        while self.peek() and (self.peek().isalnum() or self.peek() == '_'):
            chars.append(self.advance())
        
        text = ''.join(chars)
        kind = self.KEYWORDS.get(text, TokenKind.IDENTIFIER)
        return Token(kind, text, start_line, start_col)
    
    def tokenize(self) -> list[Token]:
        """Tokenize the entire source."""
        while self.pos < len(self.source):
            self.skip_whitespace()
            
            if self.pos >= len(self.source):
                break
            
            ch = self.peek()
            start_line = self.line
            start_col = self.col
            
            # Comments
            if ch == '{':
                self.skip_comment()
                continue
            
            # String literals
            if ch == '"':
                text = self.read_string('"')
                self.tokens.append(Token(TokenKind.STRING, text, start_line, start_col))
                continue
            
            # Numbers
            if ch.isdigit():
                self.tokens.append(self.read_number())
                continue
            
            # Identifiers and keywords
            if ch.isalpha() or ch == '_':
                self.tokens.append(self.read_identifier())
                continue
            
            # Two-character operators
            two_char = ch + (self.peek(1) or '')
            if two_char == ':=':
                self.advance()
                self.advance()
                self.tokens.append(Token(TokenKind.ASSIGN, ':=', start_line, start_col))
                continue
            elif two_char == '->':
                self.advance()
                self.advance()
                self.tokens.append(Token(TokenKind.ARROW, '->', start_line, start_col))
                continue
            elif two_char == '<=':
                self.advance()
                self.advance()
                self.tokens.append(Token(TokenKind.LE, '<=', start_line, start_col))
                continue
            elif two_char == '>=':
                self.advance()
                self.advance()
                self.tokens.append(Token(TokenKind.GE, '>=', start_line, start_col))
                continue
            elif two_char == '<>':
                self.advance()
                self.advance()
                self.tokens.append(Token(TokenKind.NEQ, '<>', start_line, start_col))
                continue
            elif two_char == '==':
                self.advance()
                self.advance()
                self.tokens.append(Token(TokenKind.SAME_AS, '==', start_line, start_col))
                continue
            elif two_char == '~=':
                self.advance()
                self.advance()
                self.tokens.append(Token(TokenKind.NOT_SAME_AS, '~=', start_line, start_col))
                continue
            
            # Single-character operators
            self.advance()
            if ch == '(':
                self.tokens.append(Token(TokenKind.LPAREN, ch, start_line, start_col))
            elif ch == ')':
                self.tokens.append(Token(TokenKind.RPAREN, ch, start_line, start_col))
            elif ch == '[':
                self.tokens.append(Token(TokenKind.LBRACKET, ch, start_line, start_col))
            elif ch == ']':
                self.tokens.append(Token(TokenKind.RBRACKET, ch, start_line, start_col))
            elif ch == ';':
                self.tokens.append(Token(TokenKind.SEMICOLON, ch, start_line, start_col))
            elif ch == ',':
                self.tokens.append(Token(TokenKind.COMMA, ch, start_line, start_col))
            elif ch == '.':
                self.tokens.append(Token(TokenKind.DOT, ch, start_line, start_col))
            elif ch == ':':
                self.tokens.append(Token(TokenKind.COLON, ch, start_line, start_col))
            elif ch == '+':
                self.tokens.append(Token(TokenKind.PLUS, ch, start_line, start_col))
            elif ch == '-':
                self.tokens.append(Token(TokenKind.MINUS, ch, start_line, start_col))
            elif ch == '*':
                self.tokens.append(Token(TokenKind.MULTIPLY, ch, start_line, start_col))
            elif ch == '/':
                self.tokens.append(Token(TokenKind.DIVIDE, ch, start_line, start_col))
            elif ch == '%':
                self.tokens.append(Token(TokenKind.MODULO, ch, start_line, start_col))
            elif ch == '=':
                self.tokens.append(Token(TokenKind.EQ, ch, start_line, start_col))
            elif ch == '<':
                self.tokens.append(Token(TokenKind.LT, ch, start_line, start_col))
            elif ch == '>':
                self.tokens.append(Token(TokenKind.GT, ch, start_line, start_col))
            elif ch == '&':
                self.tokens.append(Token(TokenKind.AND, ch, start_line, start_col))
            elif ch == '|':
                self.tokens.append(Token(TokenKind.OR, ch, start_line, start_col))
            elif ch == '~':
                self.tokens.append(Token(TokenKind.NOT, ch, start_line, start_col))
            else:
                raise self.error(f"Unexpected character: {repr(ch)}")
        
        self.tokens.append(Token(TokenKind.EOF, '', self.line, self.col))
        return self.tokens


def lex(source: str) -> list[Token]:
    """Tokenize Leda source code."""
    lexer = Lexer(source)
    return lexer.tokenize()
