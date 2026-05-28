"""Symbol table and semantic analysis for Leda.

Manages scopes, symbol resolution, and capture analysis.
"""

from dataclasses import dataclass, field
from typing import Optional
from .ast_nodes import *


@dataclass(slots=True)
class Symbol:
    """Represents a declared identifier (variable, function, class, etc.)."""
    name: str
    kind: str  # "var", "const", "function", "class", "type", "param"
    decl_node: Optional[AstNode] = None
    scope: Optional['Scope'] = None
    
    # Analysis results
    is_captured: bool = False  # Whether this variable is captured by nested closure
    is_assigned: bool = False  # Whether the variable is ever assigned after declaration
    is_mutable: bool = False   # Whether it can be mutated (not a constant)
    reg_index: Optional[int] = None  # Assigned by slot planner (R0-R255)
    
    def __hash__(self):
        return hash((id(self),))
    
    def __eq__(self, other):
        return self is other


@dataclass(slots=True)
class Scope:
    """Represents a scope level (global, function, block)."""
    scope_type: str  # "global", "function", "block"
    parent: Optional['Scope'] = None
    symbols: dict[str, Symbol] = field(default_factory=dict)
    
    # For function scopes
    func_decl: Optional[FunctionDecl] = None
    upvalues: list[Symbol] = field(default_factory=list)  # Captured from parent scopes
    nested_scopes: list['Scope'] = field(default_factory=list)


class SemanticAnalyzer:
    """Analyzes AST for semantic correctness and builds symbol tables."""
    
    def __init__(self):
        self.global_scope = Scope(scope_type="global")
        self.current_scope = self.global_scope
        self.errors: list[str] = []
        self.scopes_by_decl: dict[int, Scope] = {}  # Maps decl id to their scopes
    
    def analyze(self, program: Program) -> tuple[Program, list[str]]:
        """Perform semantic analysis on the AST."""
        self.visit(program)
        return program, self.errors
    
    def error(self, msg: str) -> None:
        """Record an error."""
        self.errors.append(msg)
    
    def enter_scope(self, scope_type: str, decl_node: Optional[AstNode] = None) -> Scope:
        """Enter a new scope."""
        scope = Scope(scope_type=scope_type, parent=self.current_scope)
        if scope_type == "function" and isinstance(decl_node, FunctionDecl):
            scope.func_decl = decl_node
        self.current_scope.nested_scopes.append(scope)
        if decl_node:
            self.scopes_by_decl[id(decl_node)] = scope
        old_scope = self.current_scope
        self.current_scope = scope
        return old_scope
    
    def exit_scope(self, old_scope: Scope) -> None:
        """Exit the current scope."""
        self.current_scope = old_scope
    
    def define(self, name: str, kind: str, decl_node: Optional[AstNode] = None) -> Optional[Symbol]:
        """Define a symbol in the current scope."""
        if name in self.current_scope.symbols:
            self.error(f"Identifier '{name}' already defined in this scope")
            return None
        
        sym = Symbol(name=name, kind=kind, decl_node=decl_node, scope=self.current_scope)
        self.current_scope.symbols[name] = sym
        return sym
    
    def resolve(self, name: str) -> Optional[Symbol]:
        """Resolve a name, searching from current scope upward."""
        scope = self.current_scope
        while scope:
            if name in scope.symbols:
                symbol = scope.symbols[name]
                
                # If we crossed a function boundary, mark as captured
                if scope != self.current_scope:
                    if self.current_scope.scope_type == "function" and scope.scope_type != "global":
                        if symbol not in self.current_scope.upvalues:
                            self.current_scope.upvalues.append(symbol)
                        symbol.is_captured = True
                
                return symbol
            scope = scope.parent
        
        return None
    
    # =========================================================================
    # Visitor Methods
    # =========================================================================
    
    def visit(self, node: Optional[AstNode]) -> None:
        """Dispatch visitor based on node type."""
        if node is None:
            return
        
        if isinstance(node, list):
            for item in node:
                self.visit(item)
            return
        
        method_name = f"visit_{type(node).__name__}"
        method = getattr(self, method_name, self.generic_visit)
        method(node)
    
    def generic_visit(self, node: AstNode) -> None:
        """Default visitor - traverse all fields."""
        for field_name, field_value in node.__dict__.items():
            if isinstance(field_value, (AstNode, list)):
                self.visit(field_value)
    
    def visit_Program(self, node: Program) -> None:
        """Visit program node."""
        self.visit(node.declarations)
        self.visit(node.body)
    
    def visit_FunctionDecl(self, node: FunctionDecl) -> None:
        """Visit function declaration."""
        # Define function in current (outer) scope
        self.define(node.name, "function", node)
        
        # Enter function scope
        old_scope = self.enter_scope("function", node)
        
        # Define parameters
        for param in node.params:
            for param_name in param.names:
                sym = self.define(param_name, "param", param)
                if sym:
                    sym.is_mutable = param.storage_form == "byRef"
        
        # Visit declarations and body
        self.visit(node.declarations)
        self.visit(node.body)
        
        # Exit function scope
        self.exit_scope(old_scope)
    
    def visit_ClassDecl(self, node: ClassDecl) -> None:
        """Visit class declaration."""
        self.define(node.name, "class", node)
        
        # Enter class scope
        old_scope = self.enter_scope("block", node)
        self.visit(node.declarations)
        self.exit_scope(old_scope)
    
    def visit_VarDecl(self, node: VarDecl) -> None:
        """Visit variable declaration."""
        for name in node.names:
            sym = self.define(name, "var", node)
            if sym:
                sym.is_mutable = True
    
    def visit_ConstDecl(self, node: ConstDecl) -> None:
        """Visit constant declaration."""
        self.define(node.name, "const", node)
        self.visit(node.value)
    
    def visit_TypeDecl(self, node: TypeDecl) -> None:
        """Visit type declaration."""
        self.define(node.name, "type", node)
        self.visit(node.type_node)
    
    def visit_CompoundStatement(self, node: CompoundStatement) -> None:
        """Visit compound statement (begin...end)."""
        # Create a block scope for local variables
        if self.current_scope.scope_type != "function":
            old_scope = self.enter_scope("block", node)
        else:
            old_scope = None
        
        self.visit(node.statements)
        
        if old_scope:
            self.exit_scope(old_scope)
    
    def visit_IfStmt(self, node: IfStmt) -> None:
        """Visit if statement."""
        self.visit(node.condition)
        self.visit(node.then_stmt)
        self.visit(node.else_stmt)
    
    def visit_WhileStmt(self, node: WhileStmt) -> None:
        """Visit while statement."""
        self.visit(node.condition)
        self.visit(node.body)
    
    def visit_ForStmt(self, node: ForStmt) -> None:
        """Visit for statement."""
        if node.kind == "indexed":
            # For indexed form, define the index variable
            if node.index_var:
                self.define(node.index_var, "var", node)
            self.visit(node.init)
            self.visit(node.end)
        else:
            self.visit(node.relation)
        
        self.visit(node.body)
    
    def visit_AssignmentStmt(self, node: AssignmentStmt) -> None:
        """Visit assignment statement."""
        self.visit(node.target)
        self.visit(node.value)
        
        # Mark target symbol as assigned
        if isinstance(node.target, Identifier):
            sym = self.resolve(node.target.name)
            if sym:
                sym.is_assigned = True
    
    def visit_ReturnStmt(self, node: ReturnStmt) -> None:
        """Visit return statement."""
        self.visit(node.value)
    
    def visit_ProcedureCallStmt(self, node: ProcedureCallStmt) -> None:
        """Visit procedure call statement."""
        self.visit(node.call)
    
    def visit_IncludeStmt(self, node: IncludeStmt) -> None:
        """Visit include statement."""
        # TODO: Load and process included files
        pass
    
    def visit_Identifier(self, node: Identifier) -> None:
        """Visit identifier - resolve the name."""
        symbol = self.resolve(node.name)
        if symbol is None:
            self.error(f"Undefined identifier: '{node.name}'")
    
    def visit_BinaryOp(self, node: BinaryOp) -> None:
        """Visit binary operation."""
        self.visit(node.left)
        self.visit(node.right)
    
    def visit_UnaryOp(self, node: UnaryOp) -> None:
        """Visit unary operation."""
        self.visit(node.operand)
    
    def visit_FunctionCall(self, node: FunctionCall) -> None:
        """Visit function call."""
        self.visit(node.function)
        self.visit(node.arguments)
    
    def visit_MethodCall(self, node: MethodCall) -> None:
        """Visit method call."""
        self.visit(node.object_expr)
        self.visit(node.arguments)
    
    def visit_IndexAccess(self, node: IndexAccess) -> None:
        """Visit array index access."""
        self.visit(node.array_expr)
        self.visit(node.indices)
    
    def visit_ArrayLiteral(self, node: ArrayLiteral) -> None:
        """Visit array literal."""
        self.visit(node.elements)
    
    def visit_TypeTest(self, node: TypeTest) -> None:
        """Visit type test expression."""
        self.visit(node.value)
        # Bind names from pattern match
        for name in node.bind_names:
            self.define(name, "var", node)
    
    def visit_FunctionLiteral(self, node: FunctionLiteral) -> None:
        """Visit function literal (closure)."""
        # Enter function scope for the anonymous function
        old_scope = self.enter_scope("function", node)
        
        # Define parameters
        for param in node.params:
            for param_name in param.names:
                sym = self.define(param_name, "param", param)
                if sym:
                    sym.is_mutable = param.storage_form == "byRef"
        
        # Visit body
        self.visit(node.declarations)
        self.visit(node.body)
        
        # Exit function scope
        self.exit_scope(old_scope)
    
    def visit_CfunctionCall(self, node: CfunctionCall) -> None:
        """Visit cfunction call."""
        self.visit(node.arguments)
    
    # Literal nodes don't need visiting
    def visit_IntegerLiteral(self, node: IntegerLiteral) -> None:
        pass
    
    def visit_RealLiteral(self, node: RealLiteral) -> None:
        pass
    
    def visit_StringLiteral(self, node: StringLiteral) -> None:
        pass
    
    def visit_TypeNode(self, node: TypeNode) -> None:
        """Generic type node visitor."""
        self.generic_visit(node)


def analyze_semantics(program: Program) -> tuple[Program, list[str]]:
    """Perform semantic analysis on an AST."""
    analyzer = SemanticAnalyzer()
    return analyzer.analyze(program)
