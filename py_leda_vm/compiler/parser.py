"""Parser for Leda source code.

Uses recursive descent parsing for declarations and statements,
and Pratt parsing for expressions to handle operator precedence.
"""

from typing import Optional, Callable
from .lexer import Token, TokenKind, lex
from .ast_nodes import *


class Parser:
    """Recursive descent + Pratt parser for Leda."""
    
    # Operator precedence for Pratt parsing
    PRECEDENCE = {
        TokenKind.OR: 1,
        TokenKind.AND: 2,
        TokenKind.NOT: 3,
        TokenKind.EQ: 4,
        TokenKind.NEQ: 4,
        TokenKind.LT: 4,
        TokenKind.LE: 4,
        TokenKind.GT: 4,
        TokenKind.GE: 4,
        TokenKind.SAME_AS: 4,
        TokenKind.NOT_SAME_AS: 4,
        TokenKind.PLUS: 5,
        TokenKind.MINUS: 5,
        TokenKind.MULTIPLY: 6,
        TokenKind.DIVIDE: 6,
        TokenKind.MODULO: 6,
    }
    
    def __init__(self, tokens: list[Token]):
        self.tokens = tokens
        self.pos = 0
        self.errors: list[str] = []
    
    # =========================================================================
    # Navigation and Utilities
    # =========================================================================
    
    def peek(self, offset: int = 0) -> Optional[Token]:
        """Look ahead at a token without consuming."""
        idx = self.pos + offset
        if idx < len(self.tokens):
            return self.tokens[idx]
        return None
    
    def peek_kind(self) -> Optional[TokenKind]:
        """Get the kind of the next token."""
        token = self.peek()
        return token.kind if token else None
    
    def advance(self) -> Optional[Token]:
        """Consume and return the next token."""
        token = self.peek()
        if token:
            self.pos += 1
        return token
    
    def expect(self, kind: TokenKind) -> Optional[Token]:
        """Consume a token of the expected kind or report error."""
        token = self.peek()
        if token and token.kind == kind:
            return self.advance()
        self.error(f"Expected {kind.name}, got {self.peek_kind()}")
        return None
    
    def match(self, *kinds: TokenKind) -> bool:
        """Check if next token matches any of the given kinds."""
        return self.peek_kind() in kinds
    
    def consume_if(self, *kinds: TokenKind) -> Optional[Token]:
        """Consume if next token matches any of the given kinds."""
        if self.match(*kinds):
            return self.advance()
        return None
    
    def error(self, msg: str) -> None:
        """Record a parse error with location."""
        token = self.peek()
        if token:
            location = f"L{token.line}:C{token.col}"
        else:
            location = "EOF"
        self.errors.append(f"{location}: {msg}")
    
    def span_from_tokens(self, start_token: Token, end_token: Optional[Token] = None) -> SourceSpan:
        """Create a SourceSpan from tokens."""
        if end_token is None:
            end_token = start_token
        return SourceSpan(start_token.line, start_token.col, end_token.line, end_token.col)
    
    # =========================================================================
    # Top-level Parsing
    # =========================================================================
    
    def parse(self) -> Program:
        """Parse a complete Leda program."""
        declarations = []
        
        while not self.match(TokenKind.BEGIN) and not self.match(TokenKind.EOF):
            decl = self.parse_declaration()
            if decl:
                declarations.append(decl)
            else:
                # Skip to next declaration keyword to recover from error
                while not self.match(TokenKind.VAR, TokenKind.CONST, TokenKind.FUNCTION,
                                    TokenKind.CLASS, TokenKind.TYPE, TokenKind.BEGIN, TokenKind.EOF):
                    self.advance()
        
        body = None
        if self.match(TokenKind.BEGIN):
            body = self.parse_compound_statement()
        
        if self.expect(TokenKind.SEMICOLON):
            if self.peek_kind() != TokenKind.EOF:
                self.error("Unexpected tokens after end of program")
        
        program = Program(declarations=declarations, body=body)
        return program
    
    # =========================================================================
    # Declaration Parsing
    # =========================================================================
    
    def parse_declaration(self) -> Optional[Declaration]:
        """Parse a top-level declaration."""
        if self.match(TokenKind.VAR):
            return self.parse_var_decl()
        elif self.match(TokenKind.CONST):
            return self.parse_const_decl()
        elif self.match(TokenKind.TYPE):
            return self.parse_type_decl()
        elif self.match(TokenKind.FUNCTION):
            return self.parse_function_decl()
        elif self.match(TokenKind.CLASS):
            return self.parse_class_decl()
        elif self.match(TokenKind.INCLUDE):
            return self.parse_include()
        else:
            self.error(f"Expected declaration, got {self.peek_kind()}")
            return None
    
    def parse_var_decl(self) -> Optional[VarDecl]:
        """Parse: var x, y, z : integer;"""
        start = self.advance()
        if not start:
            return None
        
        names = []
        if self.match(TokenKind.IDENTIFIER):
            names.append(self.advance().text)
            while self.consume_if(TokenKind.COMMA):
                if self.match(TokenKind.IDENTIFIER):
                    names.append(self.advance().text)
        
        type_node = None
        if self.expect(TokenKind.COLON):
            type_node = self.parse_type()
        
        self.expect(TokenKind.SEMICOLON)
        
        decl = VarDecl(names=names, type_node=type_node)
        decl.span = self.span_from_tokens(start)
        return decl
    
    def parse_const_decl(self) -> Optional[ConstDecl]:
        """Parse: const PI := 3.14;"""
        start = self.advance()
        if not start:
            return None
        
        name = ""
        if self.match(TokenKind.IDENTIFIER):
            name = self.advance().text
        
        value = None
        if self.expect(TokenKind.ASSIGN):
            value = self.parse_expression()
        
        self.expect(TokenKind.SEMICOLON)
        
        decl = ConstDecl(name=name, value=value)
        decl.span = self.span_from_tokens(start)
        return decl
    
    def parse_type_decl(self) -> Optional[TypeDecl]:
        """Parse: type MyInt : integer;"""
        start = self.advance()
        if not start:
            return None
        
        name = ""
        if self.match(TokenKind.IDENTIFIER):
            name = self.advance().text
        
        type_node = None
        if self.expect(TokenKind.COLON):
            type_node = self.parse_type()
        
        self.expect(TokenKind.SEMICOLON)
        
        decl = TypeDecl(name=name, type_node=type_node)
        decl.span = self.span_from_tokens(start)
        return decl
    
    def parse_function_decl(self) -> Optional[FunctionDecl]:
        """Parse: function add(a : integer, b : integer) -> integer; ... end;"""
        start = self.advance()
        if not start:
            return None
        
        name = ""
        if self.match(TokenKind.IDENTIFIER):
            name = self.advance().text
        
        type_params = self.parse_type_parameters()
        
        params = []
        if self.expect(TokenKind.LPAREN):
            if not self.match(TokenKind.RPAREN):
                params = self.parse_parameter_list()
            self.expect(TokenKind.RPAREN)
        
        return_type = None
        if self.consume_if(TokenKind.ARROW):
            return_type = self.parse_type()
        
        self.expect(TokenKind.SEMICOLON)
        
        declarations = []
        while self.match(TokenKind.VAR, TokenKind.CONST, TokenKind.TYPE, TokenKind.FUNCTION, TokenKind.CLASS):
            decl = self.parse_declaration()
            if decl:
                declarations.append(decl)
        
        body = None
        if self.match(TokenKind.BEGIN):
            body = self.parse_compound_statement()
        
        self.expect(TokenKind.SEMICOLON)
        
        func = FunctionDecl(name=name, type_params=type_params, params=params,
                          return_type=return_type, declarations=declarations, body=body)
        func.span = self.span_from_tokens(start)
        return func
    
    def parse_class_decl(self) -> Optional[ClassDecl]:
        """Parse: class MyClass of MyParent; ... end;"""
        start = self.advance()
        if not start:
            return None
        
        name = ""
        if self.match(TokenKind.IDENTIFIER):
            name = self.advance().text
        
        type_params = self.parse_type_parameters()
        
        parent_type = None
        if self.consume_if(TokenKind.OF):
            parent_type = self.parse_type()
        
        self.expect(TokenKind.SEMICOLON)
        
        declarations = []
        while not self.match(TokenKind.END) and not self.match(TokenKind.EOF):
            decl = self.parse_declaration()
            if decl:
                declarations.append(decl)
        
        self.expect(TokenKind.END)
        self.expect(TokenKind.SEMICOLON)
        
        cls = ClassDecl(name=name, type_params=type_params, parent_type=parent_type,
                       declarations=declarations)
        cls.span = self.span_from_tokens(start)
        return cls
    
    def parse_include(self) -> Optional[IncludeStmt]:
        """Parse: include "std.led";"""
        start = self.advance()
        if not start:
            return None
        
        path = ""
        if self.match(TokenKind.STRING):
            path = self.advance().text
        
        self.expect(TokenKind.SEMICOLON)
        
        stmt = IncludeStmt(path=path)
        stmt.span = self.span_from_tokens(start)
        return stmt
    
    def parse_type_parameters(self) -> list[str]:
        """Parse optional [T1, T2, ...] type parameters."""
        params = []
        if self.consume_if(TokenKind.LBRACKET):
            if self.match(TokenKind.IDENTIFIER):
                params.append(self.advance().text)
                while self.consume_if(TokenKind.COMMA):
                    if self.match(TokenKind.IDENTIFIER):
                        params.append(self.advance().text)
            self.expect(TokenKind.RBRACKET)
        return params
    
    def parse_parameter_list(self) -> list[Parameter]:
        """Parse (a : integer, byRef b : string, ...)."""
        params = []
        
        while True:
            param = self.parse_parameter()
            if param:
                params.append(param)
            if not self.match(TokenKind.COMMA):
                break
            self.advance()
        
        return params
    
    def parse_parameter(self) -> Optional[Parameter]:
        """Parse a single parameter with optional storage form."""
        storage_form = ""
        if self.consume_if(TokenKind.BYREF):
            storage_form = "byRef"
        elif self.consume_if(TokenKind.BYNAME):
            storage_form = "byName"
        
        names = []
        if self.match(TokenKind.IDENTIFIER):
            names.append(self.advance().text)
            while self.consume_if(TokenKind.COMMA) and self.match(TokenKind.IDENTIFIER):
                names.append(self.advance().text)
        
        type_node = None
        if self.expect(TokenKind.COLON):
            type_node = self.parse_type()
        
        param = Parameter(names=names, type_node=type_node, storage_form=storage_form)
        return param
    
    # =========================================================================
    # Type Parsing
    # =========================================================================
    
    def parse_type(self) -> Optional[TypeNode]:
        """Parse a type expression."""
        if not self.match(TokenKind.IDENTIFIER, TokenKind.FUNCTION):
            self.error(f"Expected type, got {self.peek_kind()}")
            return None
        
        if self.match(TokenKind.FUNCTION):
            return self.parse_function_type()
        
        # Simple or generic type
        base = SimpleType(name=self.advance().text)
        
        # Check for array indices [...]
        while self.consume_if(TokenKind.LBRACKET):
            idx = None
            if not self.match(TokenKind.RBRACKET):
                idx = self.parse_expression()
            self.expect(TokenKind.RBRACKET)
            arr = ArrayType(element_type=base, indices=[idx])
            base = arr
        
        # Check for generic type parameters [T1, T2, ...]
        if isinstance(base, SimpleType) and self.match(TokenKind.LBRACKET):
            self.advance()
            type_args = []
            if not self.match(TokenKind.RBRACKET):
                type_args.append(self.parse_type())
                while self.consume_if(TokenKind.COMMA):
                    t = self.parse_type()
                    if t:
                        type_args.append(t)
            self.expect(TokenKind.RBRACKET)
            base = GenericType(base_type=base, type_args=type_args)
        
        return base
    
    def parse_function_type(self) -> Optional[FunctionType]:
        """Parse: function(int, byRef string) -> boolean"""
        self.expect(TokenKind.FUNCTION)
        
        params = []
        if self.expect(TokenKind.LPAREN):
            if not self.match(TokenKind.RPAREN):
                params = self.parse_parameter_list()
            self.expect(TokenKind.RPAREN)
        
        return_type = None
        if self.consume_if(TokenKind.ARROW):
            return_type = self.parse_type()
        
        return FunctionType(params=params, return_type=return_type)
    
    # =========================================================================
    # Statement Parsing
    # =========================================================================
    
    def parse_compound_statement(self) -> Optional[CompoundStatement]:
        """Parse: begin ... end"""
        start = self.expect(TokenKind.BEGIN)
        if not start:
            return None
        
        statements = []
        while not self.match(TokenKind.END) and not self.match(TokenKind.EOF):
            stmt = self.parse_statement()
            if stmt:
                statements.append(stmt)
            if self.expect(TokenKind.SEMICOLON) is None:
                break
        
        self.expect(TokenKind.END)
        
        compound = CompoundStatement(statements=statements)
        return compound
    
    def parse_statement(self) -> Optional[Statement]:
        """Parse a single statement."""
        if self.match(TokenKind.BEGIN):
            return self.parse_compound_statement()
        elif self.match(TokenKind.RETURN):
            return self.parse_return_stmt()
        elif self.match(TokenKind.IF):
            return self.parse_if_stmt()
        elif self.match(TokenKind.WHILE):
            return self.parse_while_stmt()
        elif self.match(TokenKind.FOR):
            return self.parse_for_stmt()
        else:
            # Try to parse as assignment or procedure call
            expr = self.parse_expression()
            if self.match(TokenKind.ASSIGN):
                self.advance()
                value = self.parse_expression()
                stmt = AssignmentStmt(target=expr, value=value)
                return stmt
            elif isinstance(expr, FunctionCall):
                stmt = ProcedureCallStmt(call=expr)
                return stmt
            elif expr:
                # Standalone expression statement
                stmt = ProcedureCallStmt(call=expr)
                return stmt
        
        return None
    
    def parse_return_stmt(self) -> Optional[ReturnStmt]:
        """Parse: return; or return expr;"""
        start = self.expect(TokenKind.RETURN)
        if not start:
            return None
        
        value = None
        if not self.match(TokenKind.SEMICOLON):
            value = self.parse_expression()
        
        stmt = ReturnStmt(value=value)
        stmt.span = self.span_from_tokens(start)
        return stmt
    
    def parse_if_stmt(self) -> Optional[IfStmt]:
        """Parse: if cond then stmt; else stmt;"""
        start = self.expect(TokenKind.IF)
        if not start:
            return None
        
        condition = self.parse_expression()
        self.expect(TokenKind.THEN)
        then_stmt = self.parse_statement()
        
        else_stmt = None
        if self.consume_if(TokenKind.ELSE):
            else_stmt = self.parse_statement()
        
        stmt = IfStmt(condition=condition, then_stmt=then_stmt, else_stmt=else_stmt)
        stmt.span = self.span_from_tokens(start)
        return stmt
    
    def parse_while_stmt(self) -> Optional[WhileStmt]:
        """Parse: while cond do stmt;"""
        start = self.expect(TokenKind.WHILE)
        if not start:
            return None
        
        condition = self.parse_expression()
        self.expect(TokenKind.DO)
        body = self.parse_statement()
        
        stmt = WhileStmt(condition=condition, body=body)
        stmt.span = self.span_from_tokens(start)
        return stmt
    
    def parse_for_stmt(self) -> Optional[ForStmt]:
        """Parse for loops: for expr do stmt; or for i := 0 to 10 do stmt;"""
        start = self.expect(TokenKind.FOR)
        if not start:
            return None
        
        # Try to parse as indexed for loop
        expr = self.parse_expression()
        
        if self.match(TokenKind.ASSIGN):
            self.advance()
            init = self.parse_expression()
            self.expect(TokenKind.TO)
            end = self.parse_expression()
            self.expect(TokenKind.DO)
            body = self.parse_statement()
            
            stmt = ForStmt(kind="indexed", index_var=expr.name if isinstance(expr, Identifier) else None,
                         init=init, end=end, body=body)
            stmt.span = self.span_from_tokens(start)
            return stmt
        else:
            self.expect(TokenKind.DO)
            body = self.parse_statement()
            
            stmt = ForStmt(kind="relation", relation=expr, body=body)
            stmt.span = self.span_from_tokens(start)
            return stmt
    
    # =========================================================================
    # Expression Parsing (Pratt)
    # =========================================================================
    
    def parse_expression(self, min_prec: int = 0) -> Optional[Expression]:
        """Parse an expression using Pratt parsing."""
        left = self.parse_primary()
        if not left:
            return None
        
        return self.parse_infix(left, min_prec)
    
    def parse_infix(self, left: Expression, min_prec: int = 0) -> Optional[Expression]:
        """Parse infix operators and function calls."""
        while True:
            if self.match(TokenKind.LPAREN) and isinstance(left, (Identifier, MethodCall, FunctionCall)):
                # Function call
                self.advance()
                args = self.parse_expression_list()
                self.expect(TokenKind.RPAREN)
                left = FunctionCall(function=left, arguments=args)
            elif self.match(TokenKind.LBRACKET):
                # Array index
                self.advance()
                indices = [self.parse_expression()]
                while self.consume_if(TokenKind.COMMA):
                    indices.append(self.parse_expression())
                self.expect(TokenKind.RBRACKET)
                left = IndexAccess(array_expr=left, indices=indices)
            elif self.match(TokenKind.DOT):
                # Method/field access
                self.advance()
                if self.match(TokenKind.IDENTIFIER):
                    method_name = self.advance().text
                    if self.match(TokenKind.LPAREN):
                        self.advance()
                        args = self.parse_expression_list()
                        self.expect(TokenKind.RPAREN)
                        left = MethodCall(object_expr=left, method_name=method_name, arguments=args)
                    else:
                        left = MethodCall(object_expr=left, method_name=method_name)
            elif self.is_binary_op() and self.get_precedence() >= min_prec:
                op_token = self.advance()
                right_prec = self.get_precedence() + 1
                right = self.parse_expression(right_prec)
                if not right:
                    self.error("Expected right operand")
                    return left
                left = BinaryOp(operator=op_token.text, left=left, right=right)
            else:
                break
        
        return left
    
    def parse_primary(self) -> Optional[Expression]:
        """Parse primary expressions."""
        token = self.peek()
        if not token:
            return None
        
        # Literals
        if token.kind == TokenKind.INTEGER:
            self.advance()
            lit = IntegerLiteral(value=int(token.text))
            lit.span = self.span_from_tokens(token)
            return lit
        
        elif token.kind == TokenKind.REAL:
            self.advance()
            lit = RealLiteral(value=float(token.text))
            lit.span = self.span_from_tokens(token)
            return lit
        
        elif token.kind == TokenKind.STRING:
            self.advance()
            lit = StringLiteral(value=token.text)
            lit.span = self.span_from_tokens(token)
            return lit
        
        elif token.kind == TokenKind.IDENTIFIER:
            name_token = self.advance()
            ident = Identifier(name=name_token.text)
            ident.span = self.span_from_tokens(name_token)
            return ident
        
        elif token.kind == TokenKind.LPAREN:
            self.advance()
            expr = self.parse_expression()
            self.expect(TokenKind.RPAREN)
            return expr
        
        elif token.kind == TokenKind.LBRACKET:
            self.advance()
            elements = self.parse_expression_list()
            self.expect(TokenKind.RBRACKET)
            lit = ArrayLiteral(elements=elements)
            return lit
        
        elif token.kind == TokenKind.NOT:
            op_token = self.advance()
            operand = self.parse_expression(self.PRECEDENCE.get(TokenKind.NOT, 10))
            unary = UnaryOp(operator=op_token.text, operand=operand)
            unary.span = self.span_from_tokens(op_token)
            return unary
        
        elif token.kind == TokenKind.MINUS:
            op_token = self.advance()
            operand = self.parse_expression(self.PRECEDENCE.get(TokenKind.MINUS, 10))
            unary = UnaryOp(operator=op_token.text, operand=operand)
            unary.span = self.span_from_tokens(op_token)
            return unary
        
        elif token.kind == TokenKind.PLUS:
            op_token = self.advance()
            operand = self.parse_expression(self.PRECEDENCE.get(TokenKind.PLUS, 10))
            unary = UnaryOp(operator=op_token.text, operand=operand)
            unary.span = self.span_from_tokens(op_token)
            return unary
        
        elif token.kind == TokenKind.FUNCTION:
            return self.parse_function_literal()
        
        elif token.kind == TokenKind.CFUNCTION:
            return self.parse_cfunction_call()
        
        else:
            self.error(f"Unexpected token: {token.kind}")
            return None
    
    def parse_function_literal(self) -> Optional[FunctionLiteral]:
        """Parse: function(x : integer) -> integer; begin ... end"""
        start = self.expect(TokenKind.FUNCTION)
        if not start:
            return None
        
        params = []
        if self.expect(TokenKind.LPAREN):
            if not self.match(TokenKind.RPAREN):
                params = self.parse_parameter_list()
            self.expect(TokenKind.RPAREN)
        
        return_type = None
        if self.consume_if(TokenKind.ARROW):
            return_type = self.parse_type()
        
        self.expect(TokenKind.SEMICOLON)
        
        declarations = []
        while self.match(TokenKind.VAR, TokenKind.CONST, TokenKind.TYPE):
            decl = self.parse_declaration()
            if decl:
                declarations.append(decl)
        
        body = None
        if self.match(TokenKind.BEGIN):
            body = self.parse_compound_statement()
        
        lit = FunctionLiteral(params=params, return_type=return_type,
                            declarations=declarations, body=body)
        lit.span = self.span_from_tokens(start)
        return lit
    
    def parse_cfunction_call(self) -> Optional[CfunctionCall]:
        """Parse: cfunction malloc(1024) -> integer"""
        start = self.expect(TokenKind.CFUNCTION)
        if not start:
            return None
        
        name = ""
        if self.match(TokenKind.IDENTIFIER):
            name = self.advance().text
        
        args = []
        if self.expect(TokenKind.LPAREN):
            if not self.match(TokenKind.RPAREN):
                args = self.parse_expression_list()
            self.expect(TokenKind.RPAREN)
        
        return_type = None
        if self.consume_if(TokenKind.ARROW):
            return_type = self.parse_type()
        
        call = CfunctionCall(name=name, arguments=args, return_type=return_type)
        call.span = self.span_from_tokens(start)
        return call
    
    def parse_expression_list(self) -> list[Expression]:
        """Parse: expr, expr, ..."""
        exprs = []
        expr = self.parse_expression()
        if expr:
            exprs.append(expr)
            while self.consume_if(TokenKind.COMMA):
                expr = self.parse_expression()
                if expr:
                    exprs.append(expr)
        return exprs
    
    def is_binary_op(self) -> bool:
        """Check if current token is a binary operator."""
        return self.peek_kind() in self.PRECEDENCE
    
    def get_precedence(self) -> int:
        """Get precedence of current operator."""
        return self.PRECEDENCE.get(self.peek_kind(), 0)


def parse_source(source: str) -> tuple[Optional[Program], list[str]]:
    """Parse Leda source code and return AST and error list."""
    tokens = lex(source)
    parser = Parser(tokens)
    ast = parser.parse()
    return ast, parser.errors
