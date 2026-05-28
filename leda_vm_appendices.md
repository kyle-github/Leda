# Leda VM Appendices

## Appendix A: Memory Layout for Closures

Closures in Leda are heap-allocated objects that pair a function template with a captured environment. This allows functions to access variables from their defining scope even after that scope has exited.

### 1. Closure Object (Heap)
The primary value passed around as a "function" or "closure."

| Field | Size | Description |
| :--- | :--- | :--- |
| **Header** | 8 bytes | 64-bit packed metadata (see Object Header Layout). |
| **Template ID** | 8 bytes | LedaValue (NaN boxed LedaInteger) representing the index into the function template/metadata table. |
| **Env Pointer** | 8 bytes | LedaValue (NaN boxed object handle) pointing to the Environment Vector object. |

### 2. Environment Vector (Heap)
A specialized array containing pointers to shared capture cells.

| Field | Size | Description |
| :--- | :--- | :--- |
| **Header** | 8 bytes | 64-bit packed metadata (see Object Header Layout). |
| **Cells[]** | N * 8 bytes | Array of pointers/handles to individual Capture Cells. The count is derived from the `object_size` in the header. |

### 3. Capture Cell (Heap)
A mutable box that allows multiple closures to share and update the same logical variable.

| Field | Size | Description |
| :--- | :--- | :--- |
| **Header** | 8 bytes | 64-bit packed metadata (see Object Header Layout). |
| **Value** | 8 bytes | The current value (NaN boxed). |

---

## Appendix B: Bytecode Examples for Control Structures

These examples demonstrate how standard high-level logic is emitted for the Workspace Register Machine.

### 1. Simple If/Else
**Source:**
```leda
if (x < 10) {
    y = 1;
} else {
    y = 2;
}
```
**Bytecode:**
*(Assume R5=x, R6=y, and C0=10, C1=1, C2=2)*
```text
0x00: LT R10, R5, C0         ; Compare x < 10, result in temp R10
0x04: JMP_IF_FALSE R10, 0x0C  ; Jump to Else block if false (delta +12 bytes)
0x08: LOAD_CONST R6, C1      ; y = 1
0x0C: JMP 0x08               ; Jump to End (delta +8 bytes)
0x10: LOAD_CONST R6, C2      ; y = 2
0x14: ...                    ; End of if/else
```

### 2. Simple While Loop
**Source:**
```leda
while (i < 10) {
    i = i + 1;
}
```
**Bytecode:**
*(Assume R5=i, C0=10, C1=1)*
```text
; loop_start:
0x00: LT R10, R5, C0         ; i < 10
0x04: JMP_IF_FALSE R10, 0x0C  ; Exit loop if false
0x08: ADDI R5, R5, 1         ; i = i + 1
0x0C: JMP -0x10              ; Jump back to loop_start (delta -16 bytes)
; loop_end:
0x10: ...
```

### 3. Simple For Loop
**Source:**
```leda
for (i = 0; i < 10; i = i + 1) {
    // body
}
```
**Bytecode:**
*(Assume R5=i, C0=0, C1=10, C2=1)*
```text
0x00: LOAD_CONST R5, C0      ; i = 0 (Init)
; loop_start:
0x04: LT R10, R5, C1         ; i < 10 (Condition)
0x08: JMP_IF_FALSE R10, 0x14  ; Exit loop if false

; [Body bytecode here]

0x0C: ADDI R5, R5, 1         ; i = i + 1 (Increment)
0x10: JMP -0x10              ; Jump back to loop_start
; loop_end:
0x14: ...
```

---

## Appendix C: Runtime Behavior of MAKE_CLOSURE

### 1. Template Lookup and Allocation
When `MAKE_CLOSURE R_Dest, _, C_FIdx` is executed:
- The VM retrieves the **Function Template** from the constant pool at index `C_FIdx`. This template contains a **Capture Plan**.
- A new **Closure Object** and an **Environment Vector** are allocated on the heap.

### 2. Resolving Captures (Per-Frame Cache)
To ensure that multiple closures created within the same frame share the same `Capture Cell`, the VM uses a **Capture Cache** located within the frame's register space.

- **Capture from Stack**: The VM checks the frame's **Capture Cache**. If the cache slot is `NULL`, it allocates a new `Capture Cell`, initializes it as **Open** (pointing to the stack slot), and stores the handle in the cache.
- **Capture from Current Environment**: The VM retrieves the cell handle from the **current closure's environment** (R2) and copies it to the new environment.

### 3. Opcode Interaction (LOAD/STORE_CAPTURE)
- **LOAD_CAPTURE**: If the cell is **Open**, it dereferences the stack address. If **Closed**, it reads from internal heap storage.
- **STORE_CAPTURE**: If the cell is **Open**, it writes to the stack address. If **Closed**, it writes to internal heap storage.

### 4. Closing the Cells (Compiler-Assisted)
- **RET**: Before restoring the FP, the VM iterates through the **Capture Cache** and closes all non-null handles.
- **TAIL_CALL**: Before overwriting arguments (R5+), the VM closes all cache entries pointing to locals/temporaries being discarded.
- **Closing Logic**: Copy the `Value` from the stack into the cell's internal storage and mark it **Closed**.

---

## Appendix D: Optimization - Capture by Value

### 1. Immutability Analysis
If a variable is captured but never reassigned, the compiler flags it as `CAPTURE_VAL`.

### 2. Impact on MAKE_CLOSURE
The VM does **not** check the `Capture Cache` or allocate a `Capture Cell`. It copies the 64-bit `Value` directly from the stack register into the closure's **Environment Vector**.

### 3. Impact on Access (LOAD_CAPTURE)
The `Environment Vector` holds the actual data bits. `LOAD_CAPTURE` performs a direct load from the environment without indirection.

---

## Appendix E: Implementation Details for Phases

### Phase 1: Python Prototype
```python
class LedaCaptureCell(LedaObject):
    def __init__(self, value=None, is_open=False, stack_address=None):
        self.value = value
        self.is_open = is_open
        self.stack_address = stack_address # Absolute stack index if open

class LedaFrameMetadata:
    def __init__(self, fp, metadata):
        self.fp = fp
        self.meta = metadata
    def reg_idx(self, n): return self.fp + n
```

### Phase 2: Systems Implementation (C++)
```cpp
struct LedaObjectHeader {
    uint64_t object_index : 32;    // Reverse reference to Object Table index.
    uint64_t object_size  : 12;    // Size in 8-byte cells. 0xFFF indicates overflow.
    uint64_t class_index  : 16;    // Index into global class metadata array.
    uint64_t is_scannable : 1;     // True if payload contains Leda Values.
    uint64_t gc_mark_bit  : 1;     // GC mark bit.
    uint64_t gc_reserved  : 2;
};

struct LedaCaptureCell : LedaObject {
    Value value;
    bool is_open;
    Value* stack_slot_ptr; 
};

struct LedaClosure : LedaObject {
    Value template_id; // NaN boxed LedaInteger
    Value env_handle;  // NaN boxed object handle
};

// Runtime Materialization Helpers
namespace lbc {
    /**
     * Materializes a string from the constant pool. 
     * Primitives use this to handle constants before performing operations.
     */
    Handle<LedaString> materialize_string(uint32_t pool_idx, ConstantPool* cp, ObjectTable* ot) {
        const uint8_t* entry = cp->get_entry_ptr(pool_idx);
        
        // Entry is 8-byte aligned. Tag is at [0].
        // uint32 length is at [1-4] (since Tag is 1 byte, we might have 3 bytes padding)
        // or simply follow the 8-byte rule: Tag at [0], Length at [4].
        uint32_t len = *reinterpret_cast<const uint32_t*>(entry + 4);
        const char* utf8_str = reinterpret_cast<const char*>(entry + 8);

        // Allocate on Leda heap
        return ot->allocate_string(utf8_str, len);
    }
}
```

---

## Appendix F: Compiler Front-End Design

### 1. Parser Structure (Recursive Descent)
- `parse_program()`: Entry point.
- `parse_declaration()`: `CONST`, `VAR`, `TYPE`, `FUNCTION`, `CLASS`, `INCLUDE`.
- `parse_type()`: Base types, qualified types, function types.
- `parse_statement()`: Assignments, `IF`, `WHILE`, `FOR`, `RETURN`.
- `parse_expression()`: Operator precedence (Pratt).
- `parse_reference()`: Identifiers and member access.

### 2. High-Level AST Definitions

| Category | Node Type | Fields |
| :--- | :--- | :--- |
| **Top Level** | `Program` | `decls: List[Declaration]`, `body: List[Statement]` |
| **Decls** | `VarDecl` | `names: List[str]`, `type: TypeNode` |
| | `FunctionDecl` | `name: str`, `args: List[Param]`, `ret_type: TypeNode`, `body: List[Statement]` |
| | `ClassDecl` | `name: str`, `parent: str`, `args: List[Param]`, `members: List[Declaration]` |
| **Statements**| `Assignment` | `target: Reference`, `value: Expression` |
| | `IfStmt` | `cond: Expression`, `then_part: Statement`, `else_part: Optional[Statement]` |
| | `WhileStmt` | `cond: Expression`, `body: Statement` |
| | `ForStmt` | `init: Node`, `limit: Expression`, `body: Statement` |
| | `ReturnStmt` | `value: Optional[Expression]` |
| **Expressions**| `BinaryOp` | `left: Expression`, `op: str`, `right: Expression` |
| | `UnaryOp` | `op: str`, `operand: Expression` |
| | `Literal` | `value: Any`, `type: str` |
| | `Identifier` | `name: str` |
| | `FunctionCall`| `target: Expression`, `args: List[Expression]` |
| | `Closure` | `params: List[Param]`, `ret_type: TypeNode`, `body: List[Statement]` |
| | `MemberAccess`| `receiver: Expression`, `member: str` |

### 3. Implementation Steps
1. **Lexer**: Token generation (`ID`, `ICONSTANT`, etc.) with position tracking.
2. **Parser**: LL(n) bottom-up tree construction using AST classes.
3. **Symbol Table (Pass 1)**: Scope resolution and capture identification.
4. **Slot Planner (Pass 2)**: HWM calculation and FP relative offset assignment.
5. **Code Generator (Pass 3)**: Emission of 32-bit instruction words.