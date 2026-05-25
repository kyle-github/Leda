# Engineering Design Document: Upgrading Leda to a Fast, Static Bytecode VM

**Target Architecture:** Hand-written LL(1)/Pratt Compiler $\rightarrow$ Portable Static Image Format (`.lbc`) $\rightarrow$ Optimized Stack-Based VM (Derived from Wren)

---

## 1. Frontend & Compiler Redesign

The current Flex/Bison AST walker will be replaced by a handwritten, two-pass compiler. Because Leda features a fully static class system, the compiler must assume responsibility for calculating all object memory shapes, field layouts, and virtual method tables (vTables) offline.

### Parsing Strategy

* **High-Level Declarations:** A handwritten top-down Recursive Descent parser will handle static structural blocks (classes, methods, functions, loops).
* **Expressions:** A **Pratt Parser** (Top-Down Operator Precedence) will process expressions. This allows easy management of Leda's multi-paradigm operator hierarchies (logical, functional, and procedural) without grammar inflation.

### The Two-Pass Compilation Flow

To generate a single, unified bytecode image, the compiler operates in two major passes:

#### Pass 1: Semantic Analysis and Layout Fixation

1. **Global Symbol Resolution:** Parse all class structures, global functions, and `std.led` relations to populate a global symbol table.
2. **Field Offset Mapping:** For every class, calculate its contiguous instance layout. Fields are assigned sequential 0-indexed offsets based on declaration order.
3. **vTable Construction:** Assign each unique method signature a global integer index. For subclasses, copy the parent class vTable array and overwrite indices where overriding occurs.
4. **Scope Determination:** Traverse functions to map local variables directly to stack frame slot indices. Identify variables captured by nested blocks to flag them for heap-allocated **Upvalues**.

#### Pass 2: Linear Bytecode Generation

Flatten the AST nodes into sequential bytecode. Because the class structure is immutable at runtime, the compiler avoids high-level symbolic generation and emits highly predictable, index-driven instructions:

* Instead of emitting property lookup names (`"x"`), emit `OP_GET_FIELD <offset_idx>`.
* Instead of emitting method dispatch strings (`"draw"`), emit `OP_INVOKE <vtable_idx>`.
* **Self-Recursion Check:** If a relation or function executes a tail-recursive call to itself, the compiler avoids call-infrastructure entirely and emits a simple `OP_JUMP` targeting instruction `0` of the current block, transforming recursion into an iterative execution loop.

---

## 2. Portable Bytecode Image Format (`.lbc`)

The compiler emits a single, self-contained binary file. The VM loads this file sequentially into memory, establishing pointers instantly without runtime search penalties.

### Binary Layout Specification

1. **Header:**
* `Magic Number` (4 bytes): Identifies the binary as a valid Leda bytecode file.
* `Version Number` (2 bytes): Language/VM specification version matching.
* `Entry Point` (4 bytes): Global method/function table index where execution begins.


2. **Constant Pool:**
* An array of primitive literals (strings, floats, large integers) used throughout the program. Instructions access these payloads safely using a 2-byte index operand (`OP_CONSTANT <idx>`).


3. **Static Class Table:**
* For each class: `Class Name Index` (referencing the Constant Pool), `Parent Class Index` (for type checking/casting), `Instance Size` (total slots to allocate during initialization), and a `vTable Map` (array mapping sequential integers to internal function body indices).


4. **Function/Method Code Section:**
* For each executable block: `Argument Count` (arity), `Local Variable Count` (stack reservation size), and the raw `Bytecode Stream` (array of 1-byte opcodes followed by inline operands).



---

## 3. Wren VM Modifications for Leda

Wren provides an exceptional foundation: it implements a compact **NaN-Tagged** value representation (64-bit slots storing doubles, booleans, or heap pointers with zero boxing overhead) and a precise, incremental **Mark-Sweep Garbage Collector**.

To repurpose Wren into a static engine optimized for Leda, three core modifications are required.

### Modification A: De-Wren the Method Dispatch (Static Devirtualization)

Wren is dynamic and routes method calls through runtime string hash tables (`wrenFindMethod`). This must be stripped.

* **New Object Representation:** Keep Wren’s `ObjClass` and `ObjInstance` definitions, but replace the dynamic method map with a contiguous array of function pointers or bytecode offset integers (`Value* vtable`).
* **The Execution Patch:** Rewrite the method call opcode execution logic inside `wren_vm.c`. When executing method dispatch, extract the target layout instantly:
```c
// High-performance static invocation replacing Wren's dynamic lookup
case OP_INVOKE: {
    uint16_t vtable_idx = READ_SHORT();
    uint8_t arg_count = READ_BYTE();

    // Peek past arguments to locate target object
    Value receiver = vm->stackTop[-arg_count - 1]; 
    ObjClass* klass = AS_INSTANCE(receiver)->klass;

    // Direct array lookup: zero string processing overhead
    ObjClosure* method = klass->vtable[vtable_idx];
    wrenCall(vm, method, arg_count);
    break;
}

```



### Modification B: Implement Tail Call Optimization (TCO)

Crucial for `std.led`, where logical backtracking and relations are driven by continuations, TCO prevents stack overflows during deep relational traversals. Wren lacks TCO natively; you must implement it via a specialized `OP_TAIL_CALL` opcode.

* **The Execution Patch:** Instead of pushing a new `CallFrame` onto the VM call stack, the handler recycles the existing frame:
1. Locate the current active `CallFrame`.
2. Slide the incoming parameters (sitting at the top of the stack) down memory to overwrite the current frame's existing locals and arguments.
3. Reset the Instruction Pointer (`frame->ip`) directly to the start address of the targeted function.
4. Bypass the frame counter increment (`vm->frameCount++`).



### Modification C: First-Class Functors

Leda demands functional programming features. Map functions to object structures naturally by enforcing a unified object interface under the hood.

* **The Structure:** Implement functions as instances of a implicit system class that encapsulates a bytecode pointer along with an array of closed **Upvalues** (leveraging Wren’s native upvalue tracking for lexical closures).
* **Execution:** Treat invoking a function closure identically to a static method call targeting a standardized entry slot (e.g., index `0` of the functional object's virtual table).

### Modification D: Host Primitives (`OP_PRIMITIVE`)

To seamlessly bind to the existing runtime subsystems, introduce an `OP_PRIMITIVE <id>` instruction. This acts as a streamlined foreign function 
interface (FFI), fetching values directly from the VM stack top and handing them off directly to your legacy C implementation layers, ensuring 
compatibility with underlying systems without complicating the bytecode architecture.
