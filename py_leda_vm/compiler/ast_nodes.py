"""Abstract Syntax Tree (AST) node definitions for Leda.

These nodes represent the parsed structure of Leda source code.
"""

from abc import ABC
from dataclasses import dataclass, field
from typing import Any, Optional


@dataclass(slots=True)
class SourceSpan:
    """Represents a location in source code."""
    line_start: int
    col_start: int
    line_end: int
    col_end: int


@dataclass(slots=True)
class AstNode(ABC):
    """Base class for all AST nodes."""
    span: SourceSpan = field(default_factory=lambda: SourceSpan(0, 0, 0, 0))


# =============================================================================
# Program Structure
# =============================================================================

@dataclass(slots=True)
class Program(AstNode):
    """Root node representing a complete Leda program."""
    declarations: list['Declaration'] = field(default_factory=list)
    body: Optional['CompoundStatement'] = None


# =============================================================================
# Declarations
# =============================================================================

@dataclass(slots=True)
class Declaration(AstNode):
    """Base class for declarations."""
    pass


@dataclass(slots=True)
class VarDecl(Declaration):
    """Variable declaration: var x, y, z : integer;"""
    names: list[str] = field(default_factory=list)
    type_node: Optional['TypeNode'] = None


@dataclass(slots=True)
class ConstDecl(Declaration):
    """Constant declaration: const PI := 3.14;"""
    name: str = ""
    value: Optional['Expression'] = None


@dataclass(slots=True)
class TypeDecl(Declaration):
    """Type declaration: type IntArray : integer[100];"""
    name: str = ""
    type_node: Optional['TypeNode'] = None


@dataclass(slots=True)
class FunctionDecl(Declaration):
    """Function declaration with signature and body."""
    name: str = ""
    type_params: list[str] = field(default_factory=list)
    params: list['Parameter'] = field(default_factory=list)
    return_type: Optional['TypeNode'] = None
    declarations: list[Declaration] = field(default_factory=list)
    body: Optional['CompoundStatement'] = None


@dataclass(slots=True)
class ClassDecl(Declaration):
    """Class declaration."""
    name: str = ""
    type_params: list[str] = field(default_factory=list)
    parent_type: Optional['TypeNode'] = None
    declarations: list[Declaration] = field(default_factory=list)


@dataclass(slots=True)
class Parameter(AstNode):
    """Function parameter."""
    names: list[str] = field(default_factory=list)
    type_node: Optional['TypeNode'] = None
    storage_form: str = ""  # "" (by-value), "byRef", "byName"


# =============================================================================
# Types
# =============================================================================

@dataclass(slots=True)
class TypeNode(AstNode):
    """Base class for type expressions."""
    pass


@dataclass(slots=True)
class SimpleType(TypeNode):
    """Simple named type: integer, string, MyClass, etc."""
    name: str = ""


@dataclass(slots=True)
class ArrayType(TypeNode):
    """Array type: integer[10], string[], etc."""
    element_type: Optional[TypeNode] = None
    indices: list[Optional['Expression']] = field(default_factory=list)


@dataclass(slots=True)
class FunctionType(TypeNode):
    """Function type: function(integer, byRef string) -> boolean."""
    params: list[Parameter] = field(default_factory=list)
    return_type: Optional[TypeNode] = None


@dataclass(slots=True)
class GenericType(TypeNode):
    """Generic/qualified type: Stack[integer]."""
    base_type: Optional[TypeNode] = None
    type_args: list[TypeNode] = field(default_factory=list)


# =============================================================================
# Statements
# =============================================================================

@dataclass(slots=True)
class Statement(AstNode):
    """Base class for statements."""
    pass


@dataclass(slots=True)
class CompoundStatement(Statement):
    """begin ... end block."""
    statements: list[Statement] = field(default_factory=list)


@dataclass(slots=True)
class AssignmentStmt(Statement):
    """Assignment: x := 10;"""
    target: Optional['Expression'] = None
    value: Optional['Expression'] = None


@dataclass(slots=True)
class ReturnStmt(Statement):
    """Return statement."""
    value: Optional['Expression'] = None


@dataclass(slots=True)
class IfStmt(Statement):
    """If statement: if cond then stmt; else stmt;"""
    condition: Optional['Expression'] = None
    then_stmt: Optional[Statement] = None
    else_stmt: Optional[Statement] = None


@dataclass(slots=True)
class WhileStmt(Statement):
    """While loop: while cond do stmt;"""
    condition: Optional['Expression'] = None
    body: Optional[Statement] = None


@dataclass(slots=True)
class ForStmt(Statement):
    """For loop with multiple forms."""
    kind: str = ""  # "relation", "range", "indexed"
    
    # For relation form
    relation: Optional['Expression'] = None
    
    # For range form
    start: Optional['Expression'] = None
    end: Optional['Expression'] = None
    
    # For indexed form
    index_var: Optional[str] = None
    init: Optional['Expression'] = None
    
    body: Optional[Statement] = None


@dataclass(slots=True)
class ProcedureCallStmt(Statement):
    """Standalone procedure call: print(x);"""
    call: Optional['FunctionCall'] = None


@dataclass(slots=True)
class IncludeStmt(Statement):
    """Include directive: include "std.led";"""
    path: str = ""


# =============================================================================
# Expressions
# =============================================================================

@dataclass(slots=True)
class Expression(AstNode):
    """Base class for expressions."""
    pass


@dataclass(slots=True)
class IntegerLiteral(Expression):
    """Integer constant: 42"""
    value: int = 0


@dataclass(slots=True)
class RealLiteral(Expression):
    """Floating-point constant: 3.14"""
    value: float = 0.0


@dataclass(slots=True)
class StringLiteral(Expression):
    """String constant: "hello" """
    value: str = ""


@dataclass(slots=True)
class Identifier(Expression):
    """Variable reference: x"""
    name: str = ""


@dataclass(slots=True)
class BinaryOp(Expression):
    """Binary operation: a + b"""
    operator: str = ""
    left: Optional[Expression] = None
    right: Optional[Expression] = None


@dataclass(slots=True)
class UnaryOp(Expression):
    """Unary operation: -x, ~cond"""
    operator: str = ""
    operand: Optional[Expression] = None


@dataclass(slots=True)
class FunctionCall(Expression):
    """Function call: add(1, 2)"""
    function: Optional[Expression] = None
    arguments: list[Expression] = field(default_factory=list)


@dataclass(slots=True)
class MethodCall(Expression):
    """Method call: obj.method()"""
    object_expr: Optional[Expression] = None
    method_name: str = ""
    arguments: list[Expression] = field(default_factory=list)


@dataclass(slots=True)
class IndexAccess(Expression):
    """Array indexing: arr[i]"""
    array_expr: Optional[Expression] = None
    indices: list[Expression] = field(default_factory=list)


@dataclass(slots=True)
class ArrayLiteral(Expression):
    """Array literal: [1, 2, 3]"""
    elements: list[Expression] = field(default_factory=list)


@dataclass(slots=True)
class TypeTest(Expression):
    """Type pattern matching: obj is MyClass"""
    value: Optional[Expression] = None
    type_name: str = ""
    bind_names: list[str] = field(default_factory=list)


@dataclass(slots=True)
class FunctionLiteral(Expression):
    """Anonymous function: function(x : integer) -> integer; begin return x + 1; end"""
    params: list[Parameter] = field(default_factory=list)
    return_type: Optional[TypeNode] = None
    declarations: list[Declaration] = field(default_factory=list)
    body: Optional[CompoundStatement] = None


@dataclass(slots=True)
class CfunctionCall(Expression):
    """C function call: cfunction malloc(1024) -> integer"""
    name: str = ""
    arguments: list[Expression] = field(default_factory=list)
    return_type: Optional[TypeNode] = None
