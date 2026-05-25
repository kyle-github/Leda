# VM ISA Ideas

This document explains the new VM ISA for Leda (and possibly other languages).  This is a work in progress, and is not yet complete.

Leda supports:

- procedural programming with nested functions and closures
- First class functions/closures
- Multiple arguments
- Dynamic typing, polymorphism and dynamic dispatch
- various parameter passing modes (by value, by reference, by name, etc.)
- Object oriented programming with classes and methods
- functional programming with higher order functions, map/filter/reduce, etc.
- logical programming with pattern matching, etc. (implemented using higher-order functions and closures in the standard library)

## Overview

The VM ISA is a combination of ideas from the TI 99A, and the AT&T Hobbit/CRISP.  The primary data store is an activation record stack, but it is
accessed as a register file. There are very few "real" registers in the VM.  The primary data accessor register is the Frame Pointer (FP).

The VM uses the frame pointer in a manner similar to the TI 99A.  It points to the current workspace/activation record. The workspace contains the arguments, return value(s), locals, and temporary values.

### Instruction Format

(WIP subject to change)

Each instruction is variable length.  The first byte is the opcode.  The remaining bytes are interpreted according to the opcode.  Keeping the code as a
stream of bytes ensures that there are no endian issues and compresses the code somewhat.

Instructions access locations on the stack using offsets from the frame pointer.  The instructions look similar to RISC instructions, however the operands are not registers, but rather offsets from the frame pointer.  For example, an instruction to add two values on the stack might look like this:

```text
ADD FP+0, FP+4, FP+8 ; FP[0] = FP[4] + FP[8]
```

### Calling Conventions

The frame stack is used for passing arguments, return values, storing locals and temporaries.  In the following, the stack may grow upward or downward but we always refer to the end of the stack as the "top" of the stack regardless of which direction the stack grows.  The frame pointer points to the base of the current frame, which is the start of the workspace for the current function.

Each function's activation record (frame) contains the following sections in order:

| Slot   | Description |
| :----: | :---------- |
| 0 (FP) | Return address (the instruction to return to after the function call) |
| 1      | Previous frame pointer (the frame pointer of the caller)              |
| 2      | Return value #1 (if any)                                              |
| ...    | Additional return values (if any) |
| A      | Argument 1 (if any) |
| A+1    | Argument 2 (if any) |
| ...    | Additional arguments (if any) |
| C      | Captured variable 1 (if any) |
| C+1    | Captured variable 2 (if any) |
| ...    | Additional captured variables (if any) |
| L      | Local variable 1 (if any) |
| L+1    | Local variable 2 (if any) |
| ...    | Additional local variables (if any) |
| T      | Temporary variable 1 (if any) |
| T+1    | Temporary variable 2 (if any) |
| ...    | Additional temporary variables (if any) |

The exact offsets for the arguments, captured variables, return values, locals, and temporaries are determined by the function's needs.  The caller is responsible for setting up the arguments before the call, and the callee is responsible for setting up the return values before returning.

#### Example

If we have a function A that calls a function B, and A has 2 arguments, 1 return value, and 3 local variables, the stack frame for A would look like this:

| Slot   | Description |
| :----: | :---------- |
| 0 (FP) | Return address (the instruction to return to after A finishes) |
| 1      | Previous frame pointer (the frame pointer of the caller of A) |
| 2      | Return value #1 (the return value of A) |
| 3      | Argument 1 (the first argument to A) |
| 4      | Argument 2 (the second argument to A) |
| 5      | Local variable 1 (the first local variable of A) |
| 6      | Local variable 2 (the second local variable of A) |
| 7      | Local variable 3 (the third local variable of A) |
| 8      | Temporary variable 1 (the first temporary variable of A) |
| 9      | Temporary variable 2 (the second temporary variable of A) |
| 10     | Temporary variable 3 (the third temporary variable of A) |

Suppose B has 1 argument and 1 return value.  When A calls B, it needs to set up the arguments for B in the workspace of A.  The stack frame for A would then look like this:

| Slot   | Description |
| :----: | :---------- |
| 0 (FP) | Return address (the instruction to return to after A finishes) |
| 1      | Previous frame pointer (the frame pointer of the caller of A) |
| 2      | Return value #1 (the return value of A) |
| 3      | Argument 1 (the first argument to A) |
| 4      | Argument 2 (the second argument to A) |
| 5      | Local variable 1 (the first local variable of A) |
| 6      | Local variable 2 (the second local variable of A) |
| 7      | Local variable 3 (the third local variable of A) |
| 8      | Temporary variable 1 (the first temporary variable of A) |
| 9      | Temporary variable 2 (the second temporary variable of A) |
| 10     | Temporary variable 3 (the third temporary variable of A) |
| 11     | Slot for the IP of A at the CALL instruction |
| 12     | Slot for the FP of A at the CALL instruction |
| 13     | Return value #1 for B (the return value of B) |
| 14     | Argument 1 for B (the first argument to B) |

The CALL instruction takes the following byte values after it:

| Byte | Description |
| :--: | :---------- |
| 0    | Opcode for CALL |
| 1    | The FP offset, in A's frame, where the frame of B starts (in this case, 11) |
| 2... | The bytecode address of the first instruction of B (encoding TBD, constant?) |

The CALL instruction will do the following:

1. Store the current IP of A's code in slot 0 of B's frame (the return address)
2. Store the current FP of A in slot 1 of B's frame (the previous frame pointer)
3. Set FP to point to A's slot 11 (the start of B's frame)
4. Jump to the bytecode address of B's first instruction.

After the CALL executes the workspace will look like this:

| Slot   | Description |
| :----: | :---------- |
| -11    | Return address (the instruction to return to after A finishes) |
| -10    | Previous frame pointer (the frame pointer of the caller of A) |
| -9     | Return value #1 (the return value of A) |
| -8     | Argument 1 (the first argument to A) |
| -7     | Argument 2 (the second argument to A) |
| -6     | Local variable 1 (the first local variable of A) |
| -5     | Local variable 2 (the second local variable of A) |
| -4      | Local variable 3 (the third local variable of A) |
| -3      | Temporary variable 1 (the first temporary variable of A) |
| -2      | Temporary variable 2 (the second temporary variable of A) |
| -1     | Temporary variable 3 (the third temporary variable of A) |
| 0 (FP)   | Slot for the IP of A at the CALL instruction |
| 1      | Slot for the FP of A |
| 2     | Return value #1 for B (the return value of B) |
| 3     | Argument 1 for B (the first argument to B) |

When B finishes, It stores the result/return value in slot 2 of B's frame (slot 13 of A's frame), and then executes a RET instruction.  The RET instruction will do the following:

1. Set the IP to the return address stored in slot 0 of B's frame
2. Set the FP to the previous frame pointer stored in slot 1 of B's frame.

## Open Questions

### Closures/Methods/Functions

Closures, methods and functions are all first class values in Leda.  They can be created at runtime, passed around as values, and called.  This means that we need to have a way to represent them in the VM and to call them.

It maybe easier to implement all functions as closures.  Note that closures can be created and then executed after the parent frame/functions has returned. So all variables that are captured by the closure need to be stored on the heap.  

A closure as created by the compiler would be a template of sorts.  All the dynamic aspects would need to be created by the VM at runtime.

- we could create a closure environment as an activation record that is in the heap and have another category of slot that is for captured variables similar to the argument, local slots and temporary slots.  Then the existing ideas around value addressing would naturally just work.
- The compiler should able to determine which variables are captured and which are not.  We can have specific instructions to access captured variables.  
- have "transparent" references to captured variables?  The VM would automatically dereference them when seen.  However, this adds a runtime cost to every single access of a variable.
- just clone values and not worry about references?  This will mean that closures cannot modify captured variables.  Does this violate the semantics of Leda?

#### Closure Calls

How do we call a closure?

- creating the closure activation record on the heap?
- how do we populate the closure activation record with the captured variables?
- how do we determine whether an activation record is in the activation stack or on the heap?
- does it make sense to put all activation records on the heap?  That would simplify some things, but would make it more expensive to call functions in that we would need to allocate and copy arguments and captures over into the new record.

#### Function Calls

Static functions are easy.  We can have them in the constant pool.  But Leda can create functions at runtime.  Would those just end up with pointers into the heap and a function template in the constant pool?  

Possible registers?

FP - frame pointer (object + offset?)
CP - function-specific constant pool pointer
GP - global pool pointer
IP - instruction pointer (probably needs to include the object/array and the index into it)
???

If we do this, then normal register + offset instructions can be used for accessing everything.  This is a strong goal.

### Heap and not-Heap Memory

There are many different kinds of memory.  Leda will have several areas that are fully constant.

- Should the execution frame stack be in the GC heap or should it be separate?  We will need heap activation records for closures.  We would need to make sure that the FP is both a pointer into a memory zone and an offset (much like segments in x86).
- Classes, closure templates, bytecode and everything but runtime state is effectively constant data. This will need to be in the bytecode image/file, but it is not clear that we need to keep any of it on the heap since it cannot change at runtime.  Note that _values_ can change though. Just not structure.
- Constants should be broken up by function and stored separately for each function so that they can be accessed with a single byte index.
- What other divisions should we make in the data?

### Constants

Constants probably need to be per function and stored separately from the execution stack and the heap. These constants would include the
addresses of functions that the function could call.  Few functions would need more than 255 constants, so we can use a single byte to index into the constant pool for the function.  Do we need a larger index space? If so, how do we encode that in the instructions?  Do we need a separate instruction for accessing constants with a larger index?

Having a constant pool per function means that we need a constant pool "pointer" like we have for the IP and FP.  If so, it should be stored in the frame after the IP and FP.

The constant pool entry for a functions points to a set of metadata about the function including:

- where the bytecode for the function starts
- the size of the bytecode for the function? (might be explicit in the bytecode array object)
- the number of slots needed for the function activation record (arguments, captures, return values, locals, temporaries)
- the number of constants the function uses and a pointer to the constant pool for the function.
- references/pointers to:
  - constant pool
  - global pool
  - ...
- ...

### Instruction Set

What instructions do we need?

- Arithmetic instructions: ADD, SUB, MUL, DIV, MOD, etc.
- Logical instructions: AND, OR, XOR, NOT, etc.
- Comparison instructions: EQ, NEQ, LT, GT, LE, GE, etc.
- Control flow instructions: JMP, JZ, JNZ, CALL, RET, etc? Do we need these or will OOP method dispatch and function calls cover all of our needs for control flow?
- Memory access instructions: LOAD, STORE, LOAD_INDEXED, STORE_INDEXED, etc.
- Instructions for accessing reference/pointer values.  This would chain down the references until it reaches a value that is not a reference.  This would allow us to have transparent references for captured variables in closures, and for objects/structs. Having this as a separate instruction decreases the average cost of accessing slots in an activation record.
- Should instructions be typed? Separate for integer and float?  How would we handle polymorphism?

### Instruction Encoding

Using single bytes slows down the intepreter but it removes concerns about endianness.  Instructions/opcodes vary in the number of operands they take. We could also just use a single 64-bit word for each instruction. This simplifies fetch and decoding and means that the instruction address are all aligned on 64-bit boundaries, but it also means that the bytecode is larger and has a specific endianness.  

#### 4 operand instructions

- ADDC -- FP[operand1] = FP[operand2] + FP[operand3] + FP[operand4] -- this is needed for implementing multi-precision arithmetic and for implementing the carry flag for addition and subtraction.
- SUBC -- FP[operand1] = FP[operand2] - FP[operand3] - FP[operand4] -- this is needed for implementing multi-precision arithmetic and for implementing the carry flag for addition and subtraction.
- MUL -- FP[operand1] = LOWER_64_BITS(FP[operand3] * FP[operand4]) and FP[operand2] = UPPER_64_BITS(FP[operand2] * FP[operand3]) -- this is needed for implementing multi-precision multiplication.
- DIV -- FP[operand1] = QUOTIENT(FP[operand2] / FP[operand3]) and FP[operand2] = REMAINDER(FP[operand2] / FP[operand3]) -- this is needed for implementing multi-precision division.
- ...

#### 3 operand instructions

- ADD
- SUB
- MUL
- DIV
- AND
- OR
- XOR
- MOD
- LOAD_INDEXED -- FP[operand1] = MEM(FP[operand2] + FP[operand3])
- STORE_INDEXED -- MEM(FP[operand1] + FP[operand2]) = FP[operand3]
- CALL_INDEXED -- CALL the function at FP[operand2] + FP[operand3/immediate?]] and place the return address/link in FP[operand1].  For method calls through a vtable.
- ...

#### 2 operand instructions

- NOT
- NEG
- LOAD_IMMEDIATE -- FP[operand1] = immediate value encoded in the instruction?  Only for small integers?  Or do we just store these in the constant pool?
- ...

#### 1 operand instructions

Are there any?

#### Extended Indexing

Should we have the ability to extend the index space for instructions that take operands that are indexes into the constant pool or the stack?  For example, if we want to have more than 255 constants or more than 255 stack slots, we would need a way to encode larger indexes.  While having a separate instruction to extend an index, which one would it extend if there are more than one in that operation?

We do not really care that much about space, so perhaps we should just use 2 bytes for each "chunk" of the instruction.  This would allow for up to 65536 constants and stack slots, which should be more than enough for any function.  It would also simplify the instruction encoding and decoding since we would not need to worry about variable length instructions.  However, that would mean the interpreter becomes a specific endianness.

Another alternative is to use a fixed encoding length for instructions such as 64-bits.  This would allow for a large number of constants and activation record slots, and would also simplify the instruction encoding and decoding.  However, it would also mean that the bytecode has a specific endianness.  And it would increase the size of the bytecode which may have negative cache impacts.

### Endianness

The original idea of single bytes making up the parts of an instruction sidesteps endian issues.  We can simply declare that the bytecode image is always stored in little endian format and the VM will transform this to the native host endianness when the image is read.  This is a one-time cost when the image is read and is not that expensive compared to the cost of loading the data off disk.  This allows us to use multi-byte values in the instructions without worrying about endianness.

### Memory Cell Type

Initially we will use a tagged union for the memory cells on the stack.  This allows us to store different types of values in the same memory cell, and to easily check the type of a value at runtime.  This uses a lot more space than using NaN boxing or some similar technique. It is simple to implement and is essentially able to represent any new type of value.  This will allow us to get the VM running and then focus on optimizing memory cell representation later.

Is the cost in performance and space worth the simplicity of a tagged union?  If we factor out some macros and static inline functions, we can minimize the code cost of the difference between a tagged union and NaN boxing (for instance).  

NaN boxing is fairly nice, but has a significant drawback in that high end processors now can use 52 bits of address space.  NaN boxing only allows for 48 bits of address space.  If we use NaN boxing we will need to use an object table.  We need to measure how much impact an object table has on performance.  Modern processors have very good branch prediction and caching, so it may not be as bad as we think.

### References vs Values

An activation record for a function contains many cells. We have the issue that when a closure is created, we need to be able to capture the variables and allow both the closer and the parent function to access and modify the variables.  This means that we need to have some way to have references to values on the stack.  We may need a separate value type for references.  The normal instructions would operate as if the reference was transparent.

The issue may be moot if the compiler can determine which variables are captured and which are not.  We can have specific instructions to access captured variables.  Then we can just load the values into normal activation record cells and the instructions can operate on them as normal.  This would be simpler and more efficient than having a separate reference type.  However, it may be more complex to implement in the compiler.

### Impact of GC Algorithm on Performance

If we choose to do a mark-sweep GC, then allocation will be more expensive than a collection that compacts memory.  We cannot use a bump pointer and will need to traverse a free list.  This can be partially migitigated by coding in a manner that minimizes the number of allocations and deallocations, but it will still be more expensive than a compacting GC.

Note that a free list will tend to have the most commonly used chunks in the cache.  A scan across all object to update references in a moving GC will trash the cache.  This may need to be measured.

Use of an object table for any of the copying/compacting GCs will remove the scan-and-update pass of the GC, but it will add an extra indirection for every object access.  This may also need to be measured.

## Locked Design Decisions (May 25, 2026)

This section records decisions that are now fixed for the first full bytecode VM implementation.

Implementation constraint:

- The existing VM/compiler implementation under `src_bytecode/` is not the implementation baseline for this design.
- It may be consulted only to identify missing feature cases and test coverage gaps.
- New VM implementation must be greenfield from this specification.

1. Instruction encoding is fixed-width 64-bit words.
2. Instruction operand fields use 12-bit indexes so that one instruction can carry up to four indexes.
3. Closures use shared mutable capture cells (not copy-only captures).
4. Reference behavior is explicit by opcode (no transparent dereference in all operations).
5. Methods are closures whose environment carries the receiver.
6. Primitive dispatch is by primitive id table lookup.
7. Bytecode image format is standalone and self-contained.
8. v1 feature target includes closures, classes/methods, and pattern matching.
9. GC direction is compacting/copying (two-space is the current preference), with an object table strategy to reduce bulk pointer-fixup costs.
10. v1 return model is single-return value only.
11. Branch deltas are encoded in instruction words, not bytes.
12. Debug metadata is required in v1 images (line table and symbol table).
13. Each function metadata record declares return slot index(es); v1 uses one declared return slot.
14. DP is defined to maximize useful stack traces with minimal runtime work.
15. Reserved instruction bits remain unused in v1 and must be encoded as zero.
16. Opcodes may combine multiple 12-bit operand fields into larger immediates; operand interpretation is opcode-specific.

## 64-bit Instruction Word Layout

Instruction words are serialized in little-endian order in the bytecode image.  Loader code is responsible for adapting to host endianness.

### Fixed field form

- bits 63..56: opcode (8 bits)
- bits 55..44: operand A (12 bits)
- bits 43..32: operand B (12 bits)
- bits 31..20: operand C (12 bits)
- bits 19..8: operand D (12 bits)
- bits 7..4: reserved (4 bits, currently unused)
- bits 3..0: reserved (4 bits, currently unused)

This supports up to 4096 slot indexes, constant indexes, function indexes, primitive indexes, or small immediate fields per operand.

There are no extension words in this design phase.  Every instruction is exactly one 64-bit word.

Reserved bit policy for v1:

1. Compiler/assembler emits reserved bits as zero.
2. VM ignores reserved bits during decode.
3. Non-zero reserved bits are optionally diagnosable under debug builds, but not used for semantics.

### Operand composition rules

Although each instruction has four nominal 12-bit operands, opcodes may reinterpret these fields.

Base rule:

1. A/B/C/D are decoded from the instruction word.
2. Opcode semantics define whether each field is a slot index, constant index, function id, or immediate fragment.

Common composition forms:

1. Single-index form: one 12-bit index in one field (for simple slot or constant access).
2. Dual-index form: two independent 12-bit indexes plus one 12-bit immediate.
3. Packed-immediate form: concatenate B:C:D into a 36-bit immediate while A remains a slot/index operand.

Packing convention for combined immediates:

- imm36 = (B << 24) | (C << 12) | D

This gives a direct immediate range of 0..68,719,476,735 without extension words.

### Examples

CALL_LONG example:

- A = destination/return slot or call-mode selector (opcode-specific)
- B:C:D = packed 36-bit code/function target index

LOAD_INDEXED_IMM example:

- A = base slot (address/object base)
- B:C:D = packed 36-bit immediate offset

LOAD_INDEXED2 example (double-index + offset):

- A = destination slot
- B = base slot index
- C = index slot index
- D = small immediate offset (12-bit)

The ISA can include both LOAD_INDEXED_IMM and LOAD_INDEXED2 so the compiler can choose the cheaper encoding per expression shape.

## Frame and Slot Model

The VM keeps stack-like call frames for ordinary function calls.  Closure captures are heap based.

Each frame has fixed slot regions:

1. Header slots (return IP, previous FP, CP, and VM-managed metadata)
2. Return value slots
3. Argument slots
4. Local slots
5. Temporary slots

The compiler emits metadata per function describing slot counts and region base offsets.  The VM does not infer slot kinds from hardcoded positional conventions.

### Clarification on legacy slot-4 convention

The prior partial implementation had patterns where argument access in certain lowered forms started at an offset equivalent to slot 4.  In the new VM this is not a semantic rule.  Slot addressing is derived from per-function metadata generated by the compiler.

## Closures and Captures

### Representation

- A function template is immutable metadata in the image.
- A closure value is:
  - function/template id
  - pointer to a heap capture environment vector

### Capture environment

- The compiler determines the exact capture set for each closure literal.
- At closure creation time, the VM allocates a capture vector with one cell per captured variable.
- For each captured variable, the VM stores a pointer-like reference cell that aliases mutable storage used by both parent and closure.

### Required behavior

1. If a local is captured, the parent frame slot is replaced with an indirection/reference cell to heap-managed captured storage.
2. Parent code and closure code both observe and update the same logical variable value.
3. Captures survive parent return because underlying storage is heap managed.
4. Compiler emits dedicated captured-load/store opcodes so captured and non-captured access paths are explicit.

This model preserves Leda semantics for mutable captured variables.

## Calling Model

### Direct function call

- Target is a known function/template id.
- CALL creates a new frame in stack-like frame storage.
- Arguments are copied/placed according to callee metadata.

### Closure call

- Target is a closure value.
- CALL_CLOSURE uses closure.template_id and closure.capture_env.
- New frame receives CP/capture-environment pointer from the closure.

### Method call

- Method lookup yields function/template metadata.
- VM builds a method closure whose environment includes the receiver.
- Method invocation is then a normal closure call path.

## Constants and Pools

Constants are per-function.  CP points to the current function's constant pool metadata.

Notes:

- 12-bit index fields give a direct index range of 0..4095.
- Functions that exceed this use extension forms for constant indexes.
- Constants include function/template references, strings, numeric literals, class metadata references, and primitive descriptors.

## Memory and GC Direction

Current direction for first complete VM:

1. Use a compacting/copying collector (two-space style favored initially).
2. Keep normal call frames in stack-like memory for fast call/return.
3. Allocate closure capture environments, objects, arrays, and other escaping runtime structures in heap space.
4. Use an object table strategy so moving objects can be tracked with reduced whole-heap pointer rewrite overhead.

Performance decisions remain measurement-driven.

## Register-Shift Alternative (Recorded, Not Selected)

A register-window style machine was considered, where CALL shifts a logical register window and spilled registers are written to memory.  This remains a viable future experiment but is not selected as the baseline architecture for this phase because it is close in behavior to the frame-slot plan while adding another axis of complexity.

## Implementation Plan for New VM

1. Freeze opcode catalog and exact word-format decode rules.
2. Define function metadata format (slot region sizes, constants pointer/index, code offset, arity, single return).
3. Implement loader for standalone image format with endianness normalization.
4. Implement runtime value model with explicit reference cells.
5. Implement frame engine (CALL, RET, closure call path).
6. Implement closure creation and capture-vector allocation path.
7. Implement captured slot opcodes and verify mutation aliasing behavior.
8. Implement class/method closure binding and primitive dispatch table.
9. Implement pattern-match branch opcodes needed by std.led logic paths.
10. Add benchmark and profiling harness before attempting low-level optimizations.

## Frame Header Registers (Current Draft)

Current draft register/header fields for each executing frame:

1. IP: instruction pointer
2. FP: frame pointer
3. CP: closure pointer (or closure/template handle for current function activation)
4. CE: closure environment pointer
5. DP: debug context handle (index into debug metadata table)
6. CL: class table pointer (candidate)
7. CTP: constant table pointer (candidate)

Naming cleanup is required to reduce the number of C-prefixed fields.

## Open Research: CL and CTP Necessity

The following design question remains open and should be resolved before runtime implementation starts.

If each function/template metadata record already provides code pointer/range and constant-pool pointer, and closures carry function/template identity plus CE, then CL and CTP may be derivable rather than stored as explicit per-frame registers.

Research task:

1. Evaluate decode/runtime cost of deriving class table and constant-pool pointers from function/template metadata each dispatch cycle.
2. Compare against explicit CL/CTP fields in frame headers.
3. Measure impact with representative call-heavy and method-heavy benchmarks.

Current working recommendation (pending measurement):

- Keep IP, FP, CP, CE, DP explicit in the frame header.
- Derive CL and CTP from CP/function-template metadata unless profiling shows a measurable regression.

## Return Slot Metadata

Return locations are declared in function metadata rather than call-site operands.

Required fields per function/template metadata record:

1. return_count
2. return_slot_base (or explicit return_slot_list)

v1 constraints:

1. return_count is 1.
2. Callee writes the result to its declared return slot.
3. RET moves the declared result to caller-visible result position per calling convention.

Why this is selected:

1. It avoids spending operand bits on return routing in common call instructions.
2. It is forward-compatible with multi-return: return_count can be raised later without redefining CALL encoding.
3. It keeps return layout as a callee property, which aligns with compiler-owned frame layout metadata.

## Debug Stack Trace Model

DP is a compact index into a debug metadata table.  This is chosen because it simplifies useful stack traces without requiring per-frame heavyweight structures.

Minimum debug metadata table entries:

1. function/template name id
2. source file id
3. line/column table reference
4. optional lexical symbol table reference

Stack trace construction uses:

1. frame.IP for current instruction position
2. frame.DP to resolve symbolic debug context
3. caller chain via frame linkage (FP/previous frame)
