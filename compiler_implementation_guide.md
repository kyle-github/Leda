# Leda Compiler Frontend: Python Implementation Guide

This document outlines the software architecture and function-level specifications for the Python-based Leda compiler. This compiler translates Leda source code into the `.lbc` binary format for the Leda Workspace Register Machine.

## 1. Project Structure

The compiler is organized into the following Python modules to ensure a clear separation of concerns:

- `lexer.py`: Converts source text into a stream of tokens.
- `ast_nodes.py`: Defines the classes for the Abstract Syntax Tree (AST).
- `parser.py`: Implements the Recursive Descent and Pratt parser.
- `symbols.py`: Manages Scopes, Symbol resolution, and Capture analysis.
- `slot_planner.py`: Assigns R0-R255 registers and plans the Capture Cache.
- `codegen.py`: Emits 32-bit instruction words and assembles the `.lbc` image.
- `compiler.py`: The main entry point that orchestrates the compilation passes.

---

## 2. Module: `lexer.py`
**Responsibility:** Scan raw source code and produce a list of `Token` objects.

- `tokenize(source_code: str) -> List[Token]`: Uses regular expressions to identify keywords (`FUNCTION`, `VAR`, `BEGIN`), identifiers, literals (integers, strings), and operators (`+`, `:=`, `==`).
- `Token` (Class): A data class storing `type`, `value`, `line`, and `column` for error reporting and debug metadata.

---

## 3. Module: `ast_nodes.py`
**Responsibility:** Define the data structures for the intermediate representation of the code.

- `Node`: Base class for all AST nodes containing source position data.
- `Declaration(Node)`: Subclasses include `VarDecl`, `ConstDecl`, `FunctionDecl`, and `ClassDecl`.
- `Statement(Node)`: Subclasses include `Assignment`, `IfStmt`, `WhileStmt`, `ReturnStmt`, and `InvokeStmt`.
- `Expression(Node)`: Subclasses include `BinaryOp`, `UnaryOp`, `Literal`, `Identifier`, and `ClosureLiteral`.

---

## 4. Module: `parser.py`
**Responsibility:** Transform the token stream into an AST. It uses **Recursive Descent** for structural blocks and **Pratt Parsing** for expressions to handle Leda's operator precedence cleanly.

### Class: `LedaParser`
- `expect(token_type: str) -> Token`: Asserts the current token matches the expected type and consumes it. Raises a `SyntaxError` with line/column info on failure.
- `parse_program()`: Entry point. Parses `declarations` until the `BEGIN` keyword, then parses `statements`.
- `parse_declaration()`: Dispatches to `parse_var_decl`, `parse_func_decl`, `parse_class_decl`, or `parse_include`.
- `parse_statement()`: Dispatches to `parse_if`, `parse_while`, `parse_assignment`, or `parse_return`.
- `parse_expression(precedence: int = 0)`: The Pratt parser core. 
  - `get_prefix_handler(token_type)`: Handles literals, grouping, and unary operators.
  - `get_infix_handler(token_type)`: Handles binary operators and function call parentheses.

```python
class LedaParser:
    """
    Handwritten LL(n) Recursive Descent and Pratt Parser for Leda.
    Initialized with a stream of tokens produced by the Lexer.
    """
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def peek(self):
        """Look at the current token without consuming it."""
        if self.pos < len(self.tokens):
            return self.tokens[self.pos]
        return None

    def peek_type(self):
        """Return the type of the next token without consuming it."""
        token = self.peek()
        return token.type if token else None

    def advance(self):
        token = self.peek()
        if token: self.pos += 1
        return token

    def expect(self, token_type):
        """
        Asserts the current token matches the expected type and consumes it.
        If the type does not match, or we are at the end of input, raises a 
        SyntaxError with precise line and column metadata.
        """
        token = self.peek()

        if token and token.type == token_type:
            self.pos += 1
            return token

        # Handle mismatch or End of File (EOF)
        if token:
            error_msg = (
                f"Syntax Error: Expected '{token_type}', "
                f"found '{token.type}' ('{token.value}') "
                f"at line {token.line}, column {token.column}."
            )
        else:
            error_msg = f"Syntax Error: Expected '{token_type}', but reached end of input."

        raise SyntaxError(error_msg)

    def match(self, *token_types):
        """Helper to consume the current token if it matches any of the types."""
        token = self.peek()
        if token and token.type in token_types:
            self.pos += 1
            return token
        return None
```

---

## 5. Module: `symbols.py` (Pass 1)
**Responsibility:** Resolve names and identify "captured" variables (upvalues).

### Class: `SymbolTable`
- `enter_scope(scope_type: str)`: Pushes a new `Scope` (Global, Function, or Block).
- `exit_scope()`: Pops the scope.
- `define(name: str, node: Node)`: Binds a name to an AST node.
- `resolve(name: str) -> Symbol`: Recursively looks up a name. If a variable is found in a parent function's scope, it marks the symbol as `is_captured = True`.

### Class: `SemanticAnalyzer`
- `analyze(ast: Program)`: Walks the AST to build symbols and resolve all identifiers. It flags which variables must eventually be moved to the heap as `CaptureCells`.

```python
class Symbol:
    def __init__(self, name, decl_node, scope):
        self.name = name
        self.decl_node = decl_node
        self.scope = scope
        self.is_captured = False
        self.is_assigned = False # Tracks if the variable is ever the target of an assignment
        self.reg_index = None # Assigned by SlotPlanner

class Scope:
    def __init__(self, parent=None, scope_type='block'):
        self.parent = parent
        self.scope_type = scope_type # 'global', 'function', 'block'
        self.symbols = {}
        self.upvalues = [] # List of Symbols captured from outer scopes

    def define(self, name, node):
        symbol = Symbol(name, node, self)
        self.symbols[name] = symbol
        return symbol

    def resolve(self, name):
        if name in self.symbols:
            return self.symbols[name]
        if self.parent:
            symbol = self.parent.resolve(name)
            # Multi-level Capture Logic:
            # If we cross a function boundary to find a non-global symbol defined elsewhere...
            if symbol and self.scope_type == 'function' and symbol.scope != self:
                if symbol.scope.scope_type != 'global':
                    # 1. Mark the original symbol as captured so it gets a CaptureCell (Pass 2)
                    symbol.is_captured = True
                    # 2. Record this as an upvalue for THIS function scope
                    if symbol not in self.upvalues:
                        self.upvalues.append(symbol)
            return symbol
        return None

class SemanticAnalyzer:
    def __init__(self):
        self.current_scope = Scope(scope_type='global')

    def analyze(self, node):
        self.visit(node)

    def visit(self, node):
        if isinstance(node, list):
            for item in node: self.visit(item)
            return
        method = f'visit_{type(node).__name__}'
        visitor = getattr(self, method, self.generic_visit)
        visitor(node)

    def generic_visit(self, node):
        for value in node.__dict__.values():
            if isinstance(value, (Node, list)):
                self.visit(value)

    def visit_FunctionDecl(self, node):
        # Define the function in the outer scope
        self.current_scope.define(node.name, node)
        # Enter new function scope for parameters and body
        parent_scope = self.current_scope
        self.current_scope = Scope(parent_scope, 'function')
        self.current_scope.decl_node = node
        node.scope = self.current_scope
        self.visit(node.args)
        self.visit(node.body)
        self.current_scope = parent_scope

    def visit_VarDecl(self, node):
        for name in node.names:
            self.current_scope.define(name, node)

    def visit_Assignment(self, node):
        # Resolve the target and mark the underlying symbol as assigned/mutable
        self.visit(node.target)
        if hasattr(node.target, 'symbol') and node.target.symbol:
            node.target.symbol.is_assigned = True
        self.visit(node.value)

    def visit_Identifier(self, node):
        node.symbol = self.current_scope.resolve(node.name)
        if not node.symbol:
            raise NameError(f"Undefined variable '{node.name}' at {node.line}:{node.column}")
```

---

## 6. Module: `slot_planner.py` (Pass 2)
**Responsibility:** Map logical variables to physical stack slots (**R0-R255**) and plan the **Capture Cache**.

### Class: `SlotPlanner`
- `plan(ast: Program)`: Assigns register indices to every variable and temporary.
- `assign_registers(function_node)`:
  - Reserves **R0-R4** for ABI (Prev FP, Ret PC, Closure, Const Pool, Result).
  - Assigns **R5+** to arguments and local variables.
  - Allocates a contiguous block of registers for the **Capture Cache** if the function creates closures.
- `calculate_hwm(function_node) -> int`: Determines the "High Water Mark"—the total number of registers the VM must allocate for this frame.

```python
class SlotPlanner:
    def plan(self, node):
        self.visit(node)

    def visit(self, node):
        if isinstance(node, list):
            for item in node: self.visit(item)
            return
        method = f'visit_{type(node).__name__}'
        visitor = getattr(self, method, self.generic_visit)
        visitor(node)

    def generic_visit(self, node):
        for value in node.__dict__.values():
            if isinstance(value, (Node, list)):
                self.visit(value)

    def visit_FunctionDecl(self, node):
        self.assign_registers(node)
        self.visit(node.body) # Process nested functions

    def assign_registers(self, func):
        # ABI: R0-R4 reserved. Workspace starts at R5.
        next_reg = 5
        
        # 1. Map Arguments to R5, R6...
        for arg in func.args:
            sym = func.scope.resolve(arg.name)
            sym.reg_index = next_reg
            next_reg += 1
            
        # 2. Map Locals to subsequent registers
        # (Note: This simple version assumes flat locals; blocks could reuse slots)
        for sym in func.scope.symbols.values():
            if sym.decl_node.kind == 'VarDecl' and sym.reg_index is None:
                sym.reg_index = next_reg
                next_reg += 1
            
        # 3. Plan Capture Cache
        # The cache stores handles to CaptureCells for variables from THIS scope
        # that are captured by children.
        captured_locals = [s for s in func.scope.symbols.values() if s.is_captured]
        if captured_locals:
            func.capture_cache_offset = next_reg
            func.capture_cache_size = len(captured_locals)
            for i, sym in enumerate(captured_locals):
                sym.capture_cache_idx = i # Slot index within the cache
            next_reg += func.capture_cache_size
        else:
            func.capture_cache_offset = 0
            func.capture_cache_size = 0

        # 4. Map Upvalues to Environment Indices
        # The order of symbols in the upvalues list (from Pass 1) defines the
        # layout of the Environment Vector. We store this mapping for the emitter.
        func.env_index_map = {sym: i for i, sym in enumerate(func.scope.upvalues)}

        # 6. Determine Environment Scannability (GC Optimization)
        # The environment is scannable if it contains any heap pointers (CaptureCells
        # or heap-allocated objects like Strings/Arrays).
        func.env_is_scannable = False
        for sym in func.scope.upvalues:
            # Mutable captures (0x01/0x02) always use heap-allocated CaptureCells
            if sym.is_assigned:
                func.env_is_scannable = True
                break
            # Capture-by-value (0x03) check: Is the value type a heap object?
            if sym.decl_node.type not in ('Integer', 'Real', 'Boolean'):
                func.env_is_scannable = True
                break

        # 5. Generate Capture Plan
        # This plan describes how the parent scope should populate this function's
        # environment vector when it is instantiated as a closure.
        func.capture_plan = []
        parent_scope = func.scope.parent
        
        # Find the nearest enclosing function scope (the scope creating the closure)
        while parent_scope and parent_scope.scope_type != 'function' and parent_scope.scope_type != 'global':
            parent_scope = parent_scope.parent

        for sym in func.scope.upvalues:
            # Optimization: CAPTURE_VAL (Appendix D)
            # If a captured variable is never reassigned, we copy the raw bits 
            # into the environment instead of using a shared CaptureCell.
            if not sym.is_assigned:
                if sym.scope == parent_scope:
                    # Kind 0x03: Capture by Value (from parent's stack)
                    func.capture_plan.append((0x03, sym.reg_index, None))
                else:
                    # Kind 0x02: Capture from Environment (Value is already in parent's Env)
                    parent_func = parent_scope.decl_node
                    parent_env_idx = parent_func.env_index_map[sym]
                    func.capture_plan.append((0x02, parent_env_idx, None))
                continue

            # Is the variable defined in the immediate parent function?
            if sym.scope == parent_scope:
                # Kind 0x01: Capture from Stack (Register Index)
                # sym.capture_cache_idx was assigned during parent's SlotPlanner pass.
                # We include it so the VM can share the CaptureCell handle via the cache.
                func.capture_plan.append((0x01, sym.reg_index, sym.capture_cache_idx))
            else:
                # Kind 0x02: Capture from Environment (Upvalue Index in parent)
                # We navigate to the parent function to find where this upvalue
                # is stored in ITS environment vector.
                parent_func = parent_scope.decl_node
                parent_env_idx = parent_func.env_index_map[sym]
                # Environment captures don't use the cache (they share the existing Cell handle).
                func.capture_plan.append((0x02, parent_env_idx, None))

        func.hwm = next_reg # High Water Mark for the frame
```

---

## 7. Module: `codegen.py` (Pass 3)
**Responsibility:** Generate the 32-bit instruction words and assemble the final `.lbc` image.

### Class: `BytecodeEmitter`

```python
class BytecodeEmitter:
    def __init__(self, constant_pool):
        self.code = []
        self.constants = constant_pool
        self.current_func = None

    def emit(self, opcode, rd=0, s1=0, s2=0):
        """Packs operands into a 32-bit word: [Op(8)|RD(8)|S1(8)|S2(8)]"""
        word = ((opcode & 0xFF) << 24) | ((rd & 0xFF) << 16) | \
               ((s1 & 0xFF) << 8) | (s2 & 0xFF)
        self.code.append(word)
        return len(self.code) - 1

    def patch_jump(self, instr_idx, target_idx):
        """Calculates word delta and updates the S2 field of a jump instruction."""
        delta = target_idx - instr_idx
        # Update S2 (bits 0-7) of the instruction word at instr_idx
        self.code[instr_idx] = (self.code[instr_idx] & 0xFFFFFF00) | (delta & 0xFF)

    def visit(self, node, rd=0):
        method = f'visit_{type(node).__name__}'
        visitor = getattr(self, method, self.generic_visit)
        return visitor(node, rd)

    def visit_Literal(self, node, rd):
        c_idx = self.constants.add(node.value, node.type)
        self.emit(OP_LOAD_CONST, rd, 0, c_idx)
        return rd

    def visit_Assignment(self, node, _):
        # 1. Evaluate RHS into a temporary register
        rhs_reg = 10 
        self.visit(node.value, rhs_reg)
        
        # 2. Store into target
        symbol = node.target.symbol
        if symbol.is_captured:
            # Store in the Environment Vector (captured variable)
            self.emit(OP_STORE_CAPTURE, symbol.capture_env_idx, 0, rhs_reg)
        else:
            # Standard register move
            self.emit(OP_MOVE, symbol.reg_index, rhs_reg, 0)

    def visit_FunctionDecl(self, node, _):
        # 1. Save state and switch context
        prev_func = self.current_func
        self.current_func = node
        
        # 2. Recursively generate code for the function body
        # (Note: In a real compiler, you'd store this in a separate buffer)
        self.visit(node.body)
        
        # 3. Restore context
        self.current_func = prev_func

    def emit_make_closure(self, node, rd):
        """Emits MAKE_CLOSURE and generates the Capture Plan descriptors."""
        # s2 is the index of the function template in the constant pool
        self.emit(OP_MAKE_CLOSURE, rd, 0, node.template_pool_idx)
        
        # The capture plan follows the instruction in some implementations, 
        # but here it's stored in the Function Metadata Record in the .lbc image.
        pass

    def visit_IfStmt(self, node, _):
        cond_reg = 10
        self.visit(node.cond, cond_reg)
        
        # Emit conditional jump with placeholder delta
        jump_idx = self.emit(OP_JMP_IF_FALSE, cond_reg, 0, 0)
        
        self.visit(node.then_part)
        
        if node.else_part:
            exit_jmp = self.emit(OP_JMP, 0, 0, 0)
            self.patch_jump(jump_idx, len(self.code)) # Point IfFalse to Else start
            self.visit(node.else_part)
            self.patch_jump(exit_jmp, len(self.code)) # Point Else exit to End
        else:
            self.patch_jump(jump_idx, len(self.code))
```

### Class: `BinaryWriter`

```python
import struct

class BinaryWriter:
    def __init__(self):
        self.buffer = bytearray()

    def _align(self, n):
        """Appends zero-padding to reach the next n-byte boundary."""
        while len(self.buffer) % n != 0:
            self.buffer.append(0)

    def write_header(self, entry_id, dir_offset, dir_count):
        # uint32 magic, uint16 ver, uint16 res, uint32 entry, uint64 dir_off...
        self.buffer.extend(struct.pack('<IHH IQ I I', 
            0x4C42431A, 1, 0, entry_id, dir_offset, dir_count, 0))

    def write_constant_pool(self, constants):
        """Writes the constant pool with strict 8-byte alignment per entry."""
        start_offset = len(self.buffer)
        for tag, value in constants:
            self._align(8)
            self.buffer.append(tag)
            if tag == 1: # Integer
                self.buffer.extend(struct.pack('<q', value))
            elif tag == 3: # String (uint32 len + UTF8 + \0)
                enc = value.encode('utf-8')
                self.buffer.extend(struct.pack('<I', len(enc)))
                self.buffer.extend(enc)
                self.buffer.append(0) 
        return start_offset, len(self.buffer) - start_offset

    def write_function_metadata(self, metadata_records):
        """Serializes 32-byte Function Metadata Records."""
        start_offset = len(self.buffer)
        for m in metadata_records:
            # uint32 code_start, uint8 arity, local, cache_off, cache_size, hwm, 3x padding, uint32 cp_idx, dbg_idx, plan_off, uint8 plan_count, uint8 flags, 6x padding.
            flags = 1 if m.env_is_scannable else 0
            self.buffer.extend(struct.pack('<I BBBBB 3x I I I B B 6x',
                m.code_start_idx, m.arity, m.local_count, 
                m.capture_cache_offset, m.capture_cache_size, m.hwm,
                m.constant_pool_idx, m.debug_context_idx, m.capture_plan_offset, m.capture_plan_count, flags))
        return start_offset, len(self.buffer) - start_offset

    def write_bytecode(self, code_words):
        """Serializes 32-bit instruction words into the buffer."""
        start_offset = len(self.buffer)
        for word in code_words:
            self.buffer.extend(struct.pack('<I', word))
        return start_offset, len(self.buffer) - start_offset
```

---

## 8. Compiler Orchestration: `compiler.py`

The main compiler driver follows this sequence:

```python
def compile_leda(source_file):
    # 1. Lexing
    tokens = lexer.tokenize(read_file(source_file))

    # 2. Parsing
    parser = LedaParser(tokens)
    ast = parser.parse_program()

    # 3. Semantic Analysis (Pass 1)
    # Resolves symbols and marks captures.
    SemanticAnalyzer().analyze(ast)

    # 4. Slot Planning (Pass 2)
    # Assigns R-registers and HWM.
    SlotPlanner().plan(ast)

    # 5. Code Generation (Pass 3)
    # Emits instructions and builds sections.
    emitter = BytecodeEmitter()
    image_bytes = emitter.generate_image(ast)

    # 6. Save Binary
    output_path = source_file.replace(".led", ".lbc")
    with open(output_path, "wb") as f:
        f.write(image_bytes)
```

## 9. Appendix Reference

For exact binary layouts and opcode values, refer to:
- `vm_ideas.md`: Section **Binary Image Format (.lbc)** and **Instruction Set**.
- `leda_vm_appendices.md`: **Appendix F: Compiler Front-End Design**.
```
