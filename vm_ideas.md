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
- logical programming with pattern matching, etc. (implemented using higher-order functions and closures in the standard library via CPS)

## Overview

The VM ISA is a "Workspace Register Machine," combining ideas from the TI-9900 and the AT&T Hobbit/CRISP. The primary data store is an activation record stack. Registers (R0-R255) are mapped directly into the current stack frame (activation record); there is no distinction between a "register" and a stack slot.

The VM uses a Frame Pointer (FP) to point to the current set of registers. A function call "slides" the frame pointer up the stack, allowing for zero-copy argument passing where the end of the caller's window overlaps with the start of the callee's window.

### Instruction Format

Instructions are fixed-width 32-bit words (4 bytes) to ensure fast fetching and decoding. For instructions that produce two results (e.g., quotient and remainder, or 128-bit math results), the instruction specifies a single destination register $R[x]$, and the second part of the result is implicitly placed in the sequential register $R[x+1]$.

| Bits | Description |
| :--- | :--- |
| 31..24 | Opcode (8-bit) |
| 23..16 | Destination Register (R_Dest, 8-bit) |
| 15..08 | Source Register 1 (R_Src1, 8-bit) |
| 07..00 | Source Register 2 or Constant Index (R_Src2/Index, 8-bit) |

### Object Header Layout

All heap-allocated objects begin with a contiguous 64-bit header word. This packed metadata supports efficient garbage collection, dynamic dispatch, and stable identity tracking.

| Bits | Name | Description |
| :--- | :--- | :--- |
| 00..31 | `object_index` | 32-bit reverse reference to the Object Table index. Enables $O(1)$ pointer fix-ups during GC moves and provides a stable hash code. |
| 32..43 | `object_size` | 12-bit size field in 8-byte cells. If the value is `0xFFF`, the object's actual size is stored as a 32-bit integer in the first payload slot. |
| 44..59 | `class_index`  | 16-bit index into the global class metadata array, allowing for O(1) method lookup and type checking. |
| 60..63 | `flags`        | 4-bit field for GC state and object characteristics (e.g., whether the object contains boxed Values or raw byte data). |
|- 60 | `is_scannable` | 1-bit flag. True if the object's payload contains Leda `Value`s that might reference other heap objects (e.g., Closure, EnvironmentVector, Array of Values). False if the payload is raw data (e.g., String, Array of Bytes). |
Total: 64 bits (8 bytes).

> **Note on Pointer Resolution:** In the C++ VM, a `Value` does not hold a raw memory address. To resolve a pointer, the VM extracts the 32-bit `object_index` from the `Value` and uses it to look up the current address in the global `ObjectTable`. This indirection allows the Garbage Collector to move objects in memory by updating only the table entry, without needing to scan and patch every register or stack slot.

### Calling Conventions

Each function activation record uses a fixed ABI for its first few registers:

| Register | Name | Purpose |
| :--- | :--- | :--- |
| **R0** | `PREV_FP` | Caller's frame pointer for return linkage. |
| **R1** | `RET_PC` | Return address (Program Counter) in caller. |
| **R2** | `CLOSURE` | Pointer to the current `Closure` object (for Upvalues). |
| **R3** | `CONST_POOL` | Pointer to the function's metadata/constant pool. |
| **R4** | `RESULT` | Return value slot. |
| **R5-R255** | `ARGS/LOCALS` | Arguments followed by locals and temporaries. |

#### Example Call Sequence

Suppose function `A` (2 arguments, 3 locals) calls function `B` (1 argument). 

**A's Frame (before call):**

| Reg | Description |
| :---: | :--- |
| R0-R4 | ABI Linkage (Caller's state) |
| R5-R6 | Arguments to A |
| R7-R9 | Locals of A |
| R10-R14 | Space reserved for B's ABI Linkage (overlapping A's temporaries) |
| R15 | Argument 1 for B |

**Execution Steps:**

1. **Preparation**: `A` calculates the argument for `B` and places it in its window at `R15`.
2. **Invocation**: `A` executes a `CALL` instruction, specifying function `B` and a window offset of 10.
3. **VM Transition**:
    - The VM stores `A`'s current frame pointer into `A[R10]` (which becomes `B[R0]`).
    - The VM stores the return address into `A[R11]` (which becomes `B[R1]`).
    - The VM sets `A[R12]` and `A[R13]` to `B`'s closure and constant pool metadata.
    - The VM updates the active frame pointer: `FP = FP + 10`.
    - Execution jumps to `B`'s entry point.

**B's Frame (at entry):**

| Reg | Description |
| :---: | :--- |
| R0 | `PREV_FP` (pointing back to A's frame pointer base) |
| R1 | `RET_PC` (return address in A's code) |
| R2-R3 | `CLOSURE` and `CONST_POOL` for B |
| R4 | `RESULT` slot (overlaps `A[R14]`) |
| R5 | Argument 1 for B (overlaps `A[R15]`) |

4. **Return**: When `B` executes `RET`, the VM restores the frame pointer from `R0` and `PC` from `R1. `A` resumes execution and finds `B`'s return value in its `R14`.

#### Tail Call Optimization (TCO)

TCO is critical for Leda's logic programming implementation. When a tail call is detected:
1. The compiler emits code to evaluate new arguments into temporary registers.
2. These values are moved into R5, R6, etc., overwriting the current function's arguments.
3. Locals and temporaries are naturally "reset" as the new function bytecode will simply overwrite the remaining registers in the current window.
4. The VM updates R2 (if it's a closure call), R3 (constant pool), and jumps to the target PC without sliding the frame pointer.

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
IP - instruction pointer
???

All addressing is relative to the frame pointer (`FP`). Register `R_n` is simply memory at `FP + n`.
All addressing is relative to the frame pointer (`FP`). Register `R_n` is simply the stack slot at `stack[FP + n]`.

### Heap and not-Heap Memory

There are many different kinds of memory.  Leda will have several areas that are fully constant.

- Should the execution frame stack be in the GC heap or should it be separate?  We will need heap activation records for closures.  We would need to make sure that the FP is both a pointer into a memory zone and an offset (much like segments in x86).
- Classes, closure templates, bytecode and everything but runtime state is effectively constant data. This will need to be in the bytecode image/file.
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
- **Capture Cache Offset**: The register index where the handles to open capture cells are stored.
- **Capture Cache Size**: The number of registers reserved for the capture cache.
- **Captured Register Indices**: A precalculated list of indices (R5-R255) in this function's frame that are lexically captured by nested closures. 
- references/pointers to:
  - constant pool
  - global pool
  - ...
- ...

## Instruction Set

| Opcode (Binary) | Mnemonic | Operands (Dest, Src1, Src2/Idx/Imm) | Pseudocode | Description |
| :--- | :--- | :--- | :--- | :--- |
| 00000001 | **ADD** | R_Dest, R_S1, R_S2 | FP[R_Dest] = FP[R_S1] + FP[R_S2] | Internal addition of two registers. |
| 00000010 | **ADDI** | R_Dest, R_S1, Imm | FP[R_Dest] = FP[R_S1] + Imm | Internal addition of register and immediate. |
| 00000011 | **AND** | R_Dest, R_S1, R_S2 | FP[R_Dest] = FP[R_S1] & FP[R_S2] | Bitwise AND of two registers. |
| 00000100 | **CALL** | R_Target, _, Offset | NewFP=FP+Offset; NewFP[0]=FP; NewFP[1]=PC; FP=NewFP; PC=FP[R_Target].PC | Workspace slide call to callable in R_Target. |
| 00000101 | **DIV** | R_Dest, R_S1, R_S2 | FP[R_Dest]=S1/S2; FP[R_Dest+1]=S1%S2 | Integer division. Quotient in R_Dest, Remainder in R_Dest+1. |
| 00000110 | **EQ** | R_Dest, R_S1, R_S2 | FP[R_Dest] = (FP[R_S1] == FP[R_S2]) | Comparison: Equality. |
| 00000111 | **GE** | R_Dest, R_S1, R_S2 | FP[R_Dest] = (FP[R_S1] >= FP[R_S2]) | Comparison: Greater than or equal. |
| 00001000 | **GET_ATTR** | R_Dest, R_Obj, C_Idx | FP[R_Dest] = FP[R_Obj].attr[ConstPool[C_Idx]] | Load object attribute by constant index. |
| 00001001 | **GT** | R_Dest, R_S1, R_S2 | FP[R_Dest] = (FP[R_S1] > FP[R_S2]) | Comparison: Greater than. |
| 00001010 | **INVOKE_STATIC** | Offset, _, C_Idx | NewFP=FP+Offset; NewFP[0]=FP; NewFP[1]=PC; FP=NewFP; PC=ConstPool[C_Idx].PC | Slide frame call to top-level function in constant pool. |
| 00001011 | **JMP** | _, _, Delta | PC += Delta | Unconditional relative Program Counter jump. |
| 00001100 | **JMP_IF_FALSE** | R_Src, _, Delta | if(!FP[R_Src]) PC += Delta | Relative Program Counter jump if R_Src is falsy. |
| 00001101 | **JMP_IF_TRUE** | R_Src, _, Delta | if(FP[R_Src]) PC += Delta | Relative Program Counter jump if R_Src is truthy. |
| 00001110 | **LE** | R_Dest, R_S1, R_S2 | FP[R_Dest] = (FP[R_S1] <= FP[R_S2]) | Comparison: Less than or equal. |
| 00001111 | **LOAD_CAPTURE** | R_Dest, _, Idx | FP[R_Dest] = R2.Env[Idx] | Load captured variable from current closure environment. |
| 00010000 | **LOAD_CONST** | R_Dest, _, C_Idx | FP[R_Dest] = ConstPool[C_Idx] | Load constant from current function's pool into register. |
| 00010001 | **LOAD_INDEXED** | R_Dest, R_Base, R_Idx | FP[R_Dest] = FP[R_Base][FP[R_Idx]] | Load value from array-like base at register index. |
| 00010010 | **LT** | R_Dest, R_S1, R_S2 | FP[R_Dest] = (FP[R_S1] < FP[R_S2]) | Comparison: Less than. |
| 00010011 | **MAKE_CLOSURE** | R_Dest, _, C_FIdx | FP[R_Dest] = New Closure(ConstPool[C_FIdx], CurrentEnv) | Create closure from template in constant pool. |
| 00010100 | **METHOD_LOOKUP** | R_Dest, R_Obj, C_NIdx | FP[R_Dest] = vTable(FP[R_Obj], ConstPool[C_NIdx]) | Look up method in object's vTable and store callable. |
| 00010101 | **MOD** | R_Dest, R_S1, R_S2 | FP[R_Dest] = FP[R_S1] % FP[R_S2] | Internal modulo calculation. |
| 00010110 | **MOVE** | R_Dest, R_Src, _ | FP[R_Dest] = FP[R_Src] | Copy value from R_Src to R_Dest. |
| 00010111 | **MUL** | R_Dest, R_S1, R_S2 | FP[R_Dest]=Low64(S1*S2); FP[R_Dest+1]=High64(S1*S2) | Multiplication. Low bits in R_Dest, High bits in R_Dest+1. |
| 00011000 | **NEG** | R_Dest, R_Src, _ | FP[R_Dest] = -FP[R_Src] | Arithmetic negation of register. |
| 00011001 | **NEQ** | R_Dest, R_S1, R_S2 | FP[R_Dest] = (FP[R_S1] != FP[R_S2]) | Comparison: Inequality. |
| 00011010 | **NEW_ARRAY** | R_Dest, R_Size, _ | FP[R_Dest] = Allocate Array(FP[R_Size]) | Allocate a new array of the specified size. |
| 00011011 | **NEW_OBJ** | R_Dest, _, C_CIdx | FP[R_Dest] = Allocate Instance(ConstPool[C_CIdx]) | Allocate a new instance of the class in constant pool. |
| 00011100 | **NOT** | R_Dest, R_Src, _ | FP[R_Dest] = !FP[R_Src] | Logical NOT of register. |
| 00011101 | **OR** | R_Dest, R_S1, R_S2 | FP[R_Dest] = FP[R_S1] \| FP[R_S2] | Bitwise OR of two registers. |
| 00011110 | **PRIMITIVE_CALL** | P_Id, R_Base, _ | FP[R_Base] = Primitives[P_Id](FP[R_Base...]) | Call host C++ primitive; used for Leda math and I/O. |
| 00011111 | **RET** | _, _, _ | PC = FP[1]; NewFP = FP[0]; FP = NewFP | Restore previous FP and PC. |
| 00100000 | **SET_ATTR** | R_Obj, C_Idx, R_Src | FP[R_Obj].attr[ConstPool[C_Idx]] = FP[R_Src] | Store value from R_Src to object attribute. |
| 00100001 | **SHL** | R_Dest, R_S1, R_S2 | FP[R_Dest] = FP[R_S1] << FP[R_S2] | Bitwise shift left. |
| 00100010 | **SHR** | R_Dest, R_S1, R_S2 | FP[R_Dest] = FP[R_S1] >> FP[R_S2] | Bitwise shift right. |
| 00100011 | **STORE_CAPTURE** | Idx, _, R_Src | R2.Env[Idx] = FP[R_Src] | Store value from register to current closure environment. |
| 00100100 | **STORE_INDEXED** | R_Base, R_Idx, R_Src | FP[R_Base][FP[R_Idx]] = FP[R_Src] | Store value from R_Src into array base at register index. |
| 00100101 | **SUB** | R_Dest, R_S1, R_S2 | FP[R_Dest] = FP[R_S1] - FP[R_S2] | Internal subtraction of two registers. |
| 00100110 | **SUBI** | R_Dest, R_S1, Imm | FP[R_Dest] = FP[R_S1] - Imm | Internal subtraction of immediate from register. |
| 00100111 | **TAIL_CALL** | R_Target, _, _ | R2=FP[R_Target].Clos; R3=FP[R_Target].Pool; PC=FP[R_Target].PC | Overwrite current args and jump (TCO). |
| 00100111 | **TAIL_CALL** | R_Target, _, _ | CloseUpvals(); R2=FP[R_Target].Clos; R3=FP[R_Target].Pool; PC=FP[R_Target].PC | Close current upvals, overwrite current args, and jump (TCO). |
| 00101000 | **XOR** | R_Dest, R_S1, R_S2 | FP[R_Dest] = FP[R_S1] ^ FP[R_S2] | Bitwise XOR of two registers. |

### Endianness
## Binary Image Format (.lbc)

The `.lbc` file is a standalone binary image containing all static data, code, and metadata. It is serialized in **little-endian** format. 

### 1. Header (32 bytes)
- **Magic**: `uint32` = `0x4C42431A`.
- **Format Version**: `uint16`.
- **Reserved**: `uint16` (Padding).
- **Entry Function ID**: `uint32` - Global index into the Function Metadata section.
- **Section Directory Offset**: `uint64` - Absolute file offset to the directory.
- **Section Directory Count**: `uint32` - Number of entries in the directory.
- **Checksum**: `uint32` - Reserved for file integrity (e.g., CRC32).

### 2. Section Directory Entry (32 bytes)
- **Section Kind**: `uint32` (1: Constant Pool, 2: Function Metadata, 3: Class Metadata, 4: Bytecode, 5: Debug Info).
- **Offset**: `uint64` - Absolute file offset to section start.
- **Size**: `uint64` - Length of section in bytes.
- **Count**: `uint32` - Number of elements/records in the section.
- **Reserved**: `uint32` - Reserved/Padding.

### 3. Constant Pool Section
The constant pool is a sequence of tagged records. **Alignment**: Every record in the Constant Pool must begin at a file offset that is a multiple of 8. The compiler/writer is responsible for inserting zero-byte padding between entries.

| Tag (`uint8`) | Name | Data Format |
| :--- | :--- | :--- |
| `0x01` | **Integer** | `int64` |
| `0x02` | **Real** | `float64` (IEEE 754) |
| `0x03` | **String** | `uint32` length, N bytes (UTF-8), and a null-terminator (`\0`). |
| `0x04` | **Symbol** | `uint32` length followed by N bytes. |

### 4. Function Metadata Record (32 bytes)
- **Code Start Index**: `uint32` - Start index into the Bytecode section.
- **Arity**: `uint8` - Number of required arguments.
- **Local Count**: `uint8` - Number of local variables.
- **Capture Cache Offset**: `uint8` - Register index where the capture cache begins.
- **Capture Cache Size**: `uint8` - Number of slots in the capture cache.
- **High Water Mark**: `uint8` - Total registers needed for this frame (R0-R255).
- **Reserved**: `uint24` - Padding to align subsequent 32-bit fields.
- **Constant Pool Index**: `uint32` - Index into the Constant Pool section.
- **Debug Context Index**: `uint32` - Index into the Debug Info section.
- **Capture Plan Offset**: `uint32` - Absolute file offset to the sequence of descriptors.
- **Capture Plan Count**: `uint8`.
- **Flags**: `uint8` - Function characteristics. Bit 0: `is_scannable` (true if the environment contains heap pointers).
- **Reserved**: `uint48` - Padding to maintain 32-byte record size and 8-byte alignment for the next record.

### 5. Capture Descriptors (2 bytes)
- **Kind**: `uint8` (`0x01`: Stack, `0x02`: Env, `0x03`: Value).
- **Index**: `uint8` (Source Register or Environment slot index).

### 6. Class Metadata Record (32 bytes)
- **Name Index**: `uint32` - Constant Pool index.
- **Parent Index**: `uint32` - Index of the parent Class Metadata Record (0xFFFFFFFF for none).
- **Instance Slot Count**: `uint16` - Number of data slots in class instances.
- **vTable Offset**: `uint32` - Absolute file offset to the vTable array.
- **vTable Count**: `uint16` - Number of methods in the vTable.
- **Reserved**: `uint64` - Padding.

### 7. Bytecode Section
A contiguous sequence of `uint32` instruction words as defined in the **Instruction Format** section.

### 8. vTable Data
A contiguous array of `uint32` values. Each value is a global index into the **Function Metadata** section.

## Endianness

The original idea of single bytes making up the parts of an instruction sidesteps endian issues.  We can simply declare that the bytecode image is always stored in little endian format and the VM will transform this to the native host endianness when the image is read.  This is a one-time cost when the image is read and is not that expensive compared to the cost of loading the data off disk.  This allows us to use multi-byte values in the instructions without worrying about endianness.

### Memory Cell Type

We use NaN boxing encapsulated in a C++ class `Value`. Operator overloading allows for natural arithmetic while the class handles bit-masking and double-indirection via the Object Table.

### References vs Values

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

1. Instruction encoding is fixed-width 32-bit words.
2. Instruction operand fields use 8-bit indexes (R_Dest, R_Src1, R_Src2/Index).
3. Closures use shared mutable capture cells (not copy-only captures).
4. Reference behavior is explicit by opcode (no transparent dereference in all operations).
5. Methods are closures whose environment carries the receiver.
6. Primitive dispatch is by primitive id table lookup.
7. Bytecode image format is standalone and self-contained.
8. v1 feature target includes closures, classes/methods, and pattern matching.
9. GC direction is compacting/copying (two-space is the current preference), with an object table strategy to reduce bulk pointer-fixup costs.
10. ABI uses R4 for single-return value.
11. TCO is mandatory and uses argument overwriting.
12. Debug metadata is required in v1 images (line table and symbol table).
13. Each function metadata record declares return slot index(es); v1 uses one declared return slot.
14. DP is defined to maximize useful stack traces with minimal runtime work.
15. Reserved instruction bits remain unused in v1 and must be encoded as zero.
16. Opcodes may combine multiple operand fields into larger immediates; operand interpretation is opcode-specific.

## 32-bit Instruction Word Layout

Instruction words are serialized in little-endian order in the bytecode image.  Loader code is responsible for adapting to host endianness.

### Fixed field form

- bits 31..24: opcode (8 bits)
- bits 23..16: R_Dest (8 bits)
- bits 15..08: R_Src1 (8 bits)
- bits 07..00: R_Src2 or Index (8 bits)

Reserved bit policy for v1:

1. Compiler/assembler emits reserved bits as zero.
2. VM ignores reserved bits during decode.
3. Non-zero reserved bits are optionally diagnosable under debug builds, but not used for semantics.

### Examples

LOAD_CONST example:
`LOAD_CONST R10, C5`

ADD example:
`ADD R10, R11, R12`

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

- 8-bit index fields give a direct index range of 0..255.
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

## Implementation Phases

### Phase 1: Python Prototype
An "executable specification" to validate the ISA and Leda's logic programming patterns.

- **Scope**: Lexer, Parser, Symbol Table, Bytecode Generator, and the VM Interpreter loop.
- **Effort**: ~2-3 weeks.
- **Line Count**: ~1,700 - 2,600 LoC.
- **Key Focus**: Proving that the sliding window, TCO, and CPS logic work correctly before committing to C++ memory management.

### Phase 2: Systems Implementation (C++)
The production-ready VM focusing on performance and memory safety.

- **Scope**:
    - **Compiler**: Uses C++ STL for AST and Symbol Table management (~2,500 LoC).
    - **VM**: Implements the "Better C" approach. Fixed 32-bit instruction decoding, NaN Boxing for values, and the Object Table for GC safety.
    - **GC**: Two-space copying collector integrated with the Object Table (~800 LoC).
- **Effort**: ~2 months.
- **Line Count**: ~5,200 - 8,300 LoC.
- **Key Focus**: Transparent NaN boxing via C++ operator overloading and efficient heap management.

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

The `MAKE_CLOSURE` instruction is responsible for instantiating a closure and establishing the links to captured variables (Upvalues). This process ensures that multiple closures can share access to the same mutable variables across different lexical scopes.

### 1. Template Lookup and Allocation
When `MAKE_CLOSURE R_Dest, _, C_FIdx` is executed:
- The VM retrieves the **Function Template** from the constant pool at index `C_FIdx`. This template contains a **Capture Plan**, which is a precomputed list of descriptors created by the compiler.
- A new **Closure Object** and an **Environment Vector** are allocated on the heap.

### 2. Resolving Captures (Per-Frame Cache)
To ensure that multiple closures created within the same frame share the same `Capture Cell`, the VM uses a **Capture Cache** located within the frame's register space (at the offset defined in the metadata).

The VM iterates through each descriptor in the Capture Plan:

- **Capture from Stack**: If the descriptor indicates a variable located in the *current* stack frame:
    - **Sharing (Low Cost)**: The VM checks the specific slot in the frame's **Capture Cache** assigned to this register. 
    - **Creation**: If the cache slot is `NULL`, the VM allocates a new `Capture Cell`, initializes it as **Open** (pointing to the stack slot), and stores the handle in the cache slot.
    - The handle to this cell is stored in the new closure's Environment Vector.

- **Capture from Current Environment**: If the descriptor indicates a variable already captured by the executing function:
    - The VM retrieves the cell handle from the **current closure's environment** (accessible via the `CLOSURE` register, **R2**).
    - This handle is copied directly into the new closure's Environment Vector.

### 3. Opcode Interaction (LOAD/STORE_CAPTURE)
The VM must handle the Cell state transparently:
- **LOAD_CAPTURE**: If the cell is **Open**, it dereferences the stored stack address. If **Closed**, it returns the value from the cell's internal heap storage.
- **STORE_CAPTURE**: If the cell is **Open**, it writes directly to the stack address. If **Closed**, it writes to the internal heap storage.

### 4. Closing the Cells (Compiler-Assisted)
`Capture Cells` remain in the **Open** state as long as the stack frame owning the captured variable is active. 
- **RET**: Before restoring the FP, the VM iterates through the **Capture Cache** in the current frame.
- **TAIL_CALL**: Before overwriting arguments (R5+), the VM iterates through the **Capture Cache** and closes any cells pointing to the locals or temporaries that are being discarded.
- **Closing Logic**: For every non-null handle in the cache, the VM copies the current `Value` from the stack into the cell's internal storage and marks the cell as **Closed**.


### 5. Implementation Note on Shared State
This "Open Upvalue" model ensures that the owner of a variable (the parent function) can continue using standard register instructions (e.g., `ADD R7, R8, R9`) without knowing a variable has been captured. This is significantly more efficient than the "Promotion" model mentioned in Section 6.1, as it eliminates indirection for the primary owner.

---

## Appendix D: Optimization - Capture by Value

While shared mutable state is required for some Leda patterns, many captured variables are never modified after the closure is created. The compiler can optimize these cases to eliminate the overhead of `Capture Cells` and the `Capture Cache`.

### 1. Immutability Analysis
During the "Pass 1" semantic analysis, the compiler tracks reassignments to all variables. If a variable is captured by a nested closure but is **never the target of an assignment** within its own scope or any capturing scope, it is flagged as `CAPTURE_VAL`.

### 2. Impact on MAKE_CLOSURE
When the VM processes a `CAPTURE_VAL` descriptor in the Capture Plan:
- It does **not** check the `Capture Cache`.
- It does **not** allocate a `Capture Cell`.
- It simply copies the 64-bit `Value` directly from the stack register into the closure's **Environment Vector**.

### 3. Impact on Access (LOAD_CAPTURE)
For `CAPTURE_VAL` entries, the `Environment Vector` holds the actual data bits rather than an object handle to a `Capture Cell`.
- The compiler emits a standard `LOAD_CAPTURE` instruction.
- The VM performs a direct load from the environment. Since the variable is proven immutable, there is no need for a "dereference if open" check or any shared mutable logic.

### 4. Summary of Benefits
- **Memory**: Significant reduction in heap pressure as most captures in functional-style Leda code avoid `Capture Cell` allocations.
- **Speed**: Closure creation is faster (simple copy), and variable access is faster (no indirection).
- **Complexity**: The `Capture Cache` and "Close" logic only trigger for variables that actually require shared mutation, keeping the hot path of the VM focused on high-performance data movement.

---

## Appendix E: Implementation Details for Phases

This appendix provides more concrete implementation guidance for the Python prototype and the C++ systems implementation.

### Phase 1: Python Prototype (Detailed)

The Python prototype serves as an executable specification. It should prioritize clarity and direct mapping to the VM's conceptual model over raw performance.

#### 1. Value Representation
In Python, `Value` objects can be directly represented by Python's native types (int, float, bool, str) or custom Python classes for Leda objects, closures, and capture cells. NaN boxing is not necessary in Python; the type system handles it.

```python
class LedaObject:
    # Base class for all heap-allocated Leda objects
    pass

class LedaClosure(LedaObject):
    def __init__(self, template_id, env_handle):
        self.template_id = template_id # Index to function metadata
        self.env_handle = env_handle   # Handle to EnvironmentVector

class LedaEnvironmentVector(LedaObject):
    def __init__(self, count):
        self.cells = [None] * count # List of CaptureCell handles

class LedaCaptureCell(LedaObject):
    def __init__(self, value=None, is_open=False, stack_address=None):
        self.value = value
        self.is_open = is_open
        self.stack_address = stack_address # (frame_index, register_index) tuple if open

# Example: A Leda string
class LedaString(LedaObject):
    def __init__(self, value):
        self.value = value

# Example: A Leda integer (can be native Python int, or wrapped for consistency)
class LedaInteger(LedaObject):
    def __init__(self, value):
        self.value = value
```

#### 2. Frame Structure
A Python class representing a single activation record.

```python
class LedaFrame:
    def __init__(self, prev_fp, ret_pc, closure_obj, const_pool_obj, total_registers, capture_cache_offset, capture_cache_size):
        self.registers = [None] * total_registers # R0-R255
        self.registers[0] = prev_fp
        self.registers[1] = ret_pc
        self.registers[2] = closure_obj
        self.registers[3] = const_pool_obj
        # R4 is for RESULT, R5+ for ARGS/LOCALS/TEMPS

        self.capture_cache_offset = capture_cache_offset
        self.capture_cache_size = capture_cache_size

    def get_capture_cache(self):
        # Returns a slice of the registers representing the capture cache
        return self.registers[self.capture_cache_offset : self.capture_cache_offset + self.capture_cache_size]
```

#### 3. VM Loop Sketch
The core interpreter loop in Python.

```python
class LedaVM:
    def __init__(self, bytecode_image):
        self.bytecode_image = bytecode_image
        self.stack = [None] * 1024 # Contiguous stack: R0-R255 per frame
        self.frames = [] # Stack of LedaFrameMetadata (linkage info)
        self.fp = 0      # Current Frame Pointer (absolute stack index)
        self.frames = [] # List of LedaFrame objects
        self.fp = -1     # Index to current frame in self.frames
        self.pc = 0      # Program Counter (instruction index)
        self.heap = {}   # Simple dict for heap objects (Python's GC handles actual cleanup)
        self.next_heap_id = 1

    def _decode_instruction(self, instruction_word):
        # (op, r_dest, r_src1, r_src2) = decode(instruction_word)
        # ... (as per Instruction Format)
        pass

    def _get_current_frame(self):
        return self.frames[self.fp]

    def run(self):
        # Initial setup: push main function's frame
        # ...

        while self.pc < len(self.bytecode_image.code_section):
            current_frame = self._get_current_frame()
            instruction_word = self.bytecode_image.code_section[self.pc]
            op, rd, s1, s2 = self._decode_instruction(instruction_word)
            self.pc += 1

            if op == OP_ADD:
                current_frame.registers[rd] = current_frame.registers[s1] + current_frame.registers[s2]
            elif op == OP_CALL:
                # s2 is the offset for the new frame's FP
                target_func_obj = current_frame.registers[rd] # R_Target holds callable
                
                # Create new frame, set up ABI registers (R0-R4)
                # Push new frame onto self.frames, update self.fp
                # Set self.pc to target_func_obj.entry_point
                pass
            elif op == OP_RET:
                # Close upvalues in current frame
                self._close_upvalues(current_frame)
                
                # Pop current frame, restore self.fp and self.pc from R0/R1 of current_frame
                pass
            elif op == OP_TAIL_CALL:
                # Close upvalues in current frame (for locals/temps that will be overwritten)
                self._close_upvalues_for_tail_call(current_frame)
                
                # Overwrite R5+ with new arguments
                # Update R2, R3 for new function
                # Set self.pc to target_func_obj.entry_point (no frame push/pop)
                pass
            elif op == OP_MAKE_CLOSURE:
                # rd: Destination Register, s2: Function Template Index (C_FIdx)
                template_idx = s2
                template_meta = self.bytecode_image.function_metadata[template_idx]
                
                # 1. Allocate the Environment Vector
                env = LedaEnvironmentVector(template_meta.capture_plan_count)
                env_handle = self._heap_alloc(env)
                
                # 2. Resolve each capture according to the plan
                for i in range(template_meta.capture_plan_count):
                    # Descriptor: (kind, src_idx, cache_idx)
                    kind, src_idx, cache_idx = self.bytecode_image.get_capture_descriptor(template_idx, i)
                    
                    if kind == 0x01: # CAPTURE_STACK
                        # Check sharing via the per-frame Capture Cache
                        cache_slot = current_frame.reg_idx(current_frame.meta.capture_cache_offset + cache_idx)
                        cell_handle = self.stack[cache_slot]
                        
                        if cell_handle is None:
                            # First time capturing this local in this frame activation
                            abs_addr = current_frame.reg_idx(src_idx)
                            cell = LedaCaptureCell(is_open=True, stack_address=abs_addr)
                            cell_handle = self._heap_alloc(cell)
                            self.stack[cache_slot] = cell_handle
                        
                        env.cells[i] = cell_handle
                        
                    elif kind == 0x02: # CAPTURE_ENV
                        # Nested capture: pull from current closure's environment (R2)
                        curr_closure = self.heap[self.stack[current_frame.reg_idx(2)]]
                        curr_env = self.heap[curr_closure.env_handle]
                        env.cells[i] = curr_env.cells[src_idx]
            # ... other opcodes

                    elif kind == 0x03: # CAPTURE_VAL (Appendix D)
                        # Optimization: copy by value, no Cell or Cache used
                        env.cells[i] = self.stack[current_frame.reg_idx(src_idx)]

                # 3. Create Closure object and store in destination
                closure = LedaClosure(template_id=template_idx, env_handle=env_handle)
                self.stack[current_frame.reg_idx(rd)] = self._heap_alloc(closure)

            elif op == OP_LOAD_CAPTURE:
                # rd: R_Dest, s2: Index in Environment
                closure_handle = self.stack[current_frame.reg_idx(2)] # R2 holds current closure
                closure = self.heap[closure_handle]
                env = self.heap[closure.env_handle]
                cell_or_val = env.cells[s2]

                if isinstance(cell_or_val, LedaCaptureCell):
                    val = self.stack[cell_or_val.stack_address] if cell_or_val.is_open else cell_or_val.value
                else:
                    val = cell_or_val # Optimized capture-by-value

                self.stack[current_frame.reg_idx(rd)] = val

            elif op == OP_STORE_CAPTURE:
                # rd: Index in Environment, s2: R_Src
                val = self.stack[current_frame.reg_idx(s2)]
                closure_handle = self.stack[current_frame.reg_idx(2)] # R2
                closure = self.heap[closure_handle]
                env = self.heap[closure.env_handle]
                cell = env.cells[rd]

                if cell.is_open:
                    self.stack[cell.stack_address] = val
                else:
                    cell.value = val

    def _close_upvalues(self, frame):
        # The capture cache is a contiguous region of registers
        cache_start = frame.reg_idx(frame.meta.capture_cache_offset)
        for i in range(frame.meta.capture_cache_size):
            slot_idx = cache_start + i
            cell_handle = self.stack[slot_idx]
            
            if cell_handle is not None:
                cell = self.heap[cell_handle]
                if cell.is_open:
                    # Move value from the stack slot into the heap-allocated cell
                    cell.value = self.stack[cell.stack_address]
                    cell.is_open = False
                    cell.stack_address = None
                
                # Clear the cache slot in the stack
                self.stack[slot_idx] = None
        # Iterate through frame.get_capture_cache()
        # For each non-None handle, get the CaptureCell from self.heap
        # Copy value from frame.registers[cell.stack_address[1]] to cell.value
        # Set cell.is_open = False, cell.stack_address = None
        # Clear the cache slot
        pass

    def _close_upvalues_for_tail_call(self, frame):
        # In the Workspace Register Machine, a Tail Call ends the lexical scope
        # of the current function. All captured locals and arguments must be closed.
        self._close_upvalues(frame)
        # Similar to _close_upvalues, but only for locals/temps that are NOT arguments
        # (i.e., those that will be overwritten by the new function's arguments)
        pass
```

#### 4. Compiler Flow (High-Level Python)

```python
class LedaCompiler:
    def __init__(self):
        self.ast = None
        self.symbol_table = {}
        self.function_metadata = {}
        self.constant_pool = []

    def compile(self, source_code):
        tokens = self._lex(source_code)
        self.ast = self._parse(tokens)
        self._semantic_analysis(self.ast) # Populates symbol_table, does capture analysis
        self._slot_planning(self.ast)     # Assigns register indices, calculates HWM, capture cache offsets
        bytecode = self._generate_bytecode(self.ast)
        return self._assemble_image(bytecode)

    def _lex(self, source):
        # Returns list of tokens
        pass

    def _parse(self, tokens):
        # Returns AST (tree of Python objects)
        pass

    def _semantic_analysis(self, ast):
        # Builds symbol table, resolves names, identifies captures
        pass

    def _slot_planning(self, ast):
        # Assigns R0-R255 slots for args, locals, temps, capture cache
        # Calculates capture_cache_offset, capture_cache_size for each function
        pass

    def _generate_bytecode(self, ast):
        # Traverses AST, emits 32-bit instruction words
        # Handles branch targets, MAKE_CLOSURE, LOAD_CAPTURE, STORE_CAPTURE
        pass

    def _assemble_image(self, bytecode):
        # Creates a LedaBytecodeImage object with constant pool, function metadata, code section
        pass
```

### Phase 2: Systems Implementation (C++)

The C++ implementation will leverage C++ features for type safety and encapsulation while maintaining C-like performance for the VM's hot path.

#### 1. Value Representation (NaN Boxing)
A C++ `Value` class that uses NaN boxing to represent various types.

```cpp
#include <cstdint>
#include <cstring> // For memcpy

// Forward declarations for heap objects
struct LedaObjectHeader;
template<typename T> class Handle;

class Value {
private:
    uint64_t bits;

    // NaN boxing tags (example, actual values would be carefully chosen)
    static constexpr uint64_t QNAN = 0x7ffc000000000000ULL;
    static constexpr uint64_t TAG_INT = 0x0001000000000000ULL; // Small integer tag
    static constexpr uint64_t TAG_BOOL = 0x0002000000000000ULL; // Boolean tag
    static constexpr uint64_t TAG_OBJ_HANDLE = 0x0003000000000000ULL; // Object handle tag
    // ... other tags for nil, etc.

public:
    // Constructors for native types
    Value(double d) { memcpy(&bits, &d, sizeof(double)); }
    Value(int32_t i) : bits(QNAN | TAG_INT | (static_cast<uint64_t>(i) & 0xFFFFFFFFULL)) {}
    Value(bool b) : bits(QNAN | TAG_BOOL | (b ? 1ULL : 0ULL)) {}

    // Constructor for object handles (stores the object ID)
    Value(uint32_t object_id) : bits(QNAN | TAG_OBJ_HANDLE | object_id) {}

    // Type checks
    bool is_double() const { return (bits & QNAN) != QNAN; }
    bool is_int() const { return (bits & (QNAN | TAG_INT)) == (QNAN | TAG_INT); }
    bool is_bool() const { return (bits & (QNAN | TAG_BOOL)) == (QNAN | TAG_BOOL); }
    bool is_obj_handle() const { return (bits & (QNAN | TAG_OBJ_HANDLE)) == (QNAN | TAG_OBJ_HANDLE); }

    // Accessors
    double as_double() const { double d; memcpy(&d, &bits, sizeof(double)); return d; }
    int32_t as_int() const { return static_cast<int32_t>(bits & 0xFFFFFFFFULL); }
    bool as_bool() const { return (bits & 1ULL) == 1ULL; }
    uint32_t as_obj_id() const { return static_cast<uint32_t>(bits & 0xFFFFFFFFULL); }

    // Pointer Resolution: Resolve the handle into a raw pointer via the Object Table.
    // This is the standard way to "dereference" a Leda Value in C++.
    template<typename T>
    T* as_object(const ObjectTable& table) const {
        if (!is_obj_handle()) return nullptr;
        // Table lookup is a simple array access: O(1)
        return static_cast<T*>(table.get_object(as_obj_id()));
    }

    // Basic arithmetic (will need to handle type checking/conversion)
    Value operator+(const Value& other) const {
        if (is_int() && other.is_int()) return Value(as_int() + other.as_int());
        if (is_double() && other.is_double()) return Value(as_double() + other.as_double());
        // ... handle mixed types or error
        return Value(0.0); // Placeholder for error
    }
    // ... other operators
};
```

#### 2. Object Table and Handles
The `ObjectTable` manages raw pointers to heap objects, and `Handle<T>` provides type-safe access via object IDs.

```cpp
#include <vector>
#include <cstdint>

// Base header for all heap-allocated Leda objects.
// Packed into 64 bits for atomic access and memory efficiency.
struct LedaObjectHeader {
    uint64_t object_index : 32;    // Reverse reference to Object Table index.
    uint64_t object_size  : 12;    // Size in 8-byte cells. 0xFFF indicates size is in the next slot.
    uint64_t class_index  : 16;    // Index into the global class metadata array.
    uint64_t is_scannable : 1;     // True if payload contains Leda Values (GC must scan).
    uint64_t gc_mark_bit  : 1;     // GC mark bit (for mark-sweep or tricolor).
    uint64_t gc_reserved  : 2;     // Remaining GC flags (e.g., generational age, pinned).
};

class ObjectTable {
private:
    std::vector<LedaObjectHeader*> objects; // Stores raw pointers to heap objects
    std::vector<uint32_t> free_ids;         // For reusing freed object IDs

public:
    ObjectTable() {
        // Reserve ID 0 for NULL/invalid handle
        objects.push_back(nullptr); 
    }

    // Allocates a new ID and returns it. The actual memory is allocated by GC.
    uint32_t allocate_id(LedaObjectHeader* obj_ptr) {
        if (!free_ids.empty()) {
            uint32_t id = free_ids.back();
            free_ids.pop_back();
            objects[id] = obj_ptr;
            return id;
        }
        objects.push_back(obj_ptr);
        return objects.size() - 1;
    }

    // Returns the raw pointer for a given ID.
    LedaObjectHeader* get_object(uint32_t id) const {
        if (id == 0 || id >= objects.size()) return nullptr; // Handle invalid IDs
        return objects[id];
    }

    // Updates the pointer for an object ID (used by GC after moving an object).
    void update_object_ptr(uint32_t id, LedaObjectHeader* new_ptr) {
        if (id > 0 && id < objects.size()) {
            objects[id] = new_ptr;
        }
    }

    // Marks an ID as free (used by GC after an object is collected).
    void free_id(uint32_t id) {
        if (id > 0 && id < objects.size()) {
            objects[id] = nullptr; // Clear the pointer
            free_ids.push_back(id);
        }
    }

    // GC root scanning: iterate through all objects in the table
    const std::vector<LedaObjectHeader*>& get_all_objects() const {
        return objects;
    }
};

// Handle class for type-safe access to objects via ObjectTable
template<typename T>
class Handle {
private:
    uint32_t id;
    ObjectTable* obj_table; // Pointer to the global ObjectTable

public:
    Handle() : id(0), obj_table(nullptr) {}
    Handle(uint32_t obj_id, ObjectTable* table) : id(obj_id), obj_table(table) {}

    T* operator->() const {
        return static_cast<T*>(obj_table->get_object(id));
    }

    T& operator*() const {
        return *static_cast<T*>(obj_table->get_object(id));
    }

    uint32_t get_id() const { return id; }
    bool is_null() const { return id == 0; }

    // Conversion to Value (for NaN boxing)
    operator Value() const { return Value(id); }
};
```

#### 3. C++ Closure and Capture Structures
These structs are allocated in the GC heap and managed via `ObjectTable` and `Handle`.

```cpp
// Forward declaration for Handle
template<typename T> class Handle;

// Base for all Leda heap objects
struct LedaObject {
    LedaObjectHeader header;
    // ... common fields for all objects
};

struct LedaCaptureCell : LedaObject {
    Value value;
    bool is_open;
    // If open, this would be an absolute stack address (Value* or index)
    // For C++, this might be a raw pointer to the stack slot,
    // which must be handled carefully during GC safepoints.
    Value* stack_slot_ptr; 
};

struct LedaEnvironmentVector : LedaObject {
    Handle<LedaCaptureCell> cells[1]; // Flexible array member for N cells
};

struct LedaClosure : LedaObject {
    Value template_id; // NaN boxed LedaInteger
    Value env_handle;  // NaN boxed object handle
};
```

#### 4. C++ VM Loop Sketch
The core interpreter loop in C++.

```cpp
#include <vector>
#include <cstdint>
// ... include Value, ObjectTable, Handle, LedaFrame, etc.

struct LedaFunctionMetadata {
    uint32_t code_start_pc;
    uint32_t total_register_count;
    uint32_t capture_cache_offset;
    uint32_t capture_cache_size;
    // ... other metadata
};

struct LedaFrame {
    Value registers[256]; // The 256 registers for this frame
    uint32_t prev_fp_offset; // Offset to the previous frame's base
    uint32_t ret_pc;         // Return PC
    Handle<LedaClosure> closure_handle; // R2
    uint32_t const_pool_id;  // R3 (ID to constant pool object)
    // ... other ABI registers

    // Compiler-provided metadata for this frame
    const LedaFunctionMetadata* metadata;
};

class LedaVM {
private:
    std::vector<LedaFrame> call_stack; // VM-managed stack of frames
    uint32_t fp_offset;                // Current frame's base offset in call_stack
    uint32_t pc;                       // Program Counter
    ObjectTable obj_table;             // Global object table
    // ... other VM state (bytecode, global constants, etc.)

    // Instruction decoding function
    struct DecodedInstruction {
        uint8_t opcode;
        uint8_t r_dest;
        uint8_t r_src1;
        uint8_t r_src2_imm;
    };
    DecodedInstruction decode_instruction(uint32_t instruction_word) {
        // ... bit shifting as per Instruction Format
        return {}; // Placeholder
    }

    void close_upvalues(LedaFrame* frame) {
        // Iterate through frame->registers[frame->metadata->capture_cache_offset ... ]
        // For each non-null Handle<LedaCaptureCell>
        //   Get CaptureCell* from obj_table
        //   Copy frame->registers[cell->stack_slot_ptr_index] to cell->value
        //   Set cell->is_open = false, cell->stack_slot_ptr = nullptr
        //   Clear the cache slot in frame->registers
    }

public:
    void run() {
        // Initial setup: push main function's frame
        // ...

        while (pc < bytecode_size) { // Assume bytecode_size is known
            // Safepoint check (for GC)
            // if (gc_manager.should_collect()) { gc_manager.collect_garbage(); }

            LedaFrame* current_frame = &call_stack[fp_offset];
            uint32_t instruction_word = get_instruction_at_pc(pc); // Fetch from bytecode
            DecodedInstruction instr = decode_instruction(instruction_word);
            pc++;

            switch (instr.opcode) {
                case OP_ADD:
                    current_frame->registers[instr.r_dest] = 
                        current_frame->registers[instr.r_src1] + current_frame->registers[instr.r_src2_imm];
                    break;
                case OP_CALL: {
                    // instr.r_dest holds the target callable (Handle<LedaClosure> or function ID)
                    // instr.r_src2_imm is the offset to slide FP
                    
                    // 1. Save caller's linkage in new frame's R0, R1
                    // 2. Setup new frame's R2, R3
                    // 3. Update fp_offset = fp_offset + instr.r_src2_imm
                    // 4. Set pc to target function's entry point
                    break;
                }
                case OP_RET: {
                    close_upvalues(current_frame); // Close upvalues in current frame
                    // Restore pc from current_frame->registers[1]
                    // Restore fp_offset from current_frame->registers[0]
                    // Pop frame (conceptually, by adjusting fp_offset)
                    break;
                }
                case OP_TAIL_CALL: {
                    // Close upvalues for locals/temps that will be overwritten
                    close_upvalues(current_frame); 
                    
                    // Overwrite R5+ with new arguments (from current_frame->registers)
                    // Update R2, R3 for new function
                    // Set pc to target function's entry point (no frame push/pop)
                    break;
                }
                // ... other opcodes
            }
        }
    }
};
```

### Phase 3: Optimization
Post-v1 optimization based on profiling.

- **Scope**: Move from Object Table to Direct Pointers if indirection is a bottleneck. Refine the JIT-readiness of the register machine.

| Field | Size | Description |
| :--- | :--- | :--- |
| **Header** | 8 bytes | GC metadata, type tag (CLOSURE), and object size. |
| **Template ID** | 4 bytes | Index into the global function metadata table. |
| **Env Pointer** | 8 bytes | Pointer/Handle to the Environment Vector object. |

### 2. Environment Vector (Heap)
A specialized array containing pointers to shared capture cells.

| Field | Size | Description |
| :--- | :--- | :--- |
| **Header** | 8 bytes | GC metadata, type tag (ENV), and object size. |
| **Count** | 4 bytes | Number of captured variables in this environment. |
| **Cells[]** | N * 8 bytes | Array of pointers/handles to individual Capture Cells. |

### 3. Capture Cell (Heap)
A mutable box that allows multiple closures to share and update the same logical variable.

| Field | Size | Description |
| :--- | :--- | :--- |
| **Header** | 8 bytes | GC metadata, type tag (CELL). |
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

The `MAKE_CLOSURE` instruction is responsible for instantiating a closure and establishing the links to captured variables (Upvalues). This process ensures that multiple closures can share access to the same mutable variables across different lexical scopes.

### 1. Template Lookup and Allocation
When `MAKE_CLOSURE R_Dest, _, C_FIdx` is executed:
- The VM retrieves the **Function Template** from the constant pool at index `C_FIdx`. This template contains a **Capture Plan**, which is a precomputed list of descriptors created by the compiler.
- A new **Closure Object** and an **Environment Vector** are allocated on the heap.

### 2. Resolving Captures (Per-Frame Cache)
To ensure that multiple closures created within the same frame share the same `Capture Cell`, the VM uses a **Capture Cache** located within the frame's register space (at the offset defined in the metadata).

The VM iterates through each descriptor in the Capture Plan:

- **Capture from Stack**: If the descriptor indicates a variable located in the *current* stack frame:
    - **Sharing (Low Cost)**: The VM checks the specific slot in the frame's **Capture Cache** assigned to this register. 
    - **Creation**: If the cache slot is `NULL`, the VM allocates a new `Capture Cell`, initializes it as **Open** (pointing to the stack slot), and stores the handle in the cache slot.
    - The handle to this cell is stored in the new closure's Environment Vector.

- **Capture from Current Environment**: If the descriptor indicates a variable already captured by the executing function:
    - The VM retrieves the cell handle from the **current closure's environment** (accessible via the `CLOSURE` register, **R2**).
    - This handle is copied directly into the new closure's Environment Vector.

### 3. Opcode Interaction (LOAD/STORE_CAPTURE)
The VM must handle the Cell state transparently:
- **LOAD_CAPTURE**: If the cell is **Open**, it dereferences the stored stack address. If **Closed**, it returns the value from the cell's internal heap storage.
- **STORE_CAPTURE**: If the cell is **Open**, it writes directly to the stack address. If **Closed**, it writes to the internal heap storage.

### 4. Closing the Cells (Compiler-Assisted)
`Capture Cells` remain in the **Open** state as long as the stack frame owning the captured variable is active. 
- **RET**: Before restoring the FP, the VM iterates through the **Capture Cache** in the current frame.
- **TAIL_CALL**: Before overwriting arguments (R5+), the VM iterates through the **Capture Cache** and closes any cells pointing to the locals or temporaries that are being discarded.
- **Closing Logic**: For every non-null handle in the cache, the VM copies the current `Value` from the stack into the cell's internal storage and marks the cell as **Closed**.


### 5. Implementation Note on Shared State
This "Open Upvalue" model ensures that the owner of a variable (the parent function) can continue using standard register instructions (e.g., `ADD R7, R8, R9`) without knowing a variable has been captured. This is significantly more efficient than the "Promotion" model mentioned in Section 6.1, as it eliminates indirection for the primary owner.

---

## Appendix D: Optimization - Capture by Value

While shared mutable state is required for some Leda patterns, many captured variables are never modified after the closure is created. The compiler can optimize these cases to eliminate the overhead of `Capture Cells` and the `Capture Cache`.

### 1. Immutability Analysis
During the "Pass 1" semantic analysis, the compiler tracks reassignments to all variables. If a variable is captured by a nested closure but is **never the target of an assignment** within its own scope or any capturing scope, it is flagged as `CAPTURE_VAL`.

### 2. Impact on MAKE_CLOSURE
When the VM processes a `CAPTURE_VAL` descriptor in the Capture Plan:
- It does **not** check the `Capture Cache`.
- It does **not** allocate a `Capture Cell`.
- It simply copies the 64-bit `Value` directly from the stack register into the closure's **Environment Vector**.

### 3. Impact on Access (LOAD_CAPTURE)
For `CAPTURE_VAL` entries, the `Environment Vector` holds the actual data bits rather than an object handle to a `Capture Cell`.
- The compiler emits a standard `LOAD_CAPTURE` instruction.
- The VM performs a direct load from the environment. Since the variable is proven immutable, there is no need for a "dereference if open" check or any shared mutable logic.

### 4. Summary of Benefits
- **Memory**: Significant reduction in heap pressure as most captures in functional-style Leda code avoid `Capture Cell` allocations.
- **Speed**: Closure creation is faster (simple copy), and variable access is faster (no indirection).
- **Complexity**: The `Capture Cache` and "Close" logic only trigger for variables that actually require shared mutation, keeping the hot path of the VM focused on high-performance data movement.

```

---

## Appendix F: Compiler Front-End Design

The Leda compiler is a multi-pass Python application. The front end consists of a Lexer and an LL(n) Recursive Descent Parser that produces an Abstract Syntax Tree (AST).

### 1. Parser Structure (Recursive Descent)

Based on the grammar in `gram.y`, the parser will implement the following primary methods:

- `parse_program()`: Entry point. Expects `declarations`, `BEGIN`, `statements`, `END`, `;`.
- `parse_declaration()`: Branches into `CONST`, `VAR`, `TYPE`, `FUNCTION`, `CLASS`, or `INCLUDE`.
- `parse_type()`: Handles base types, qualified types (e.g., `List[Integer]`), and function types.
- `parse_statement()`: Handles assignments, control flow (`IF`, `WHILE`, `FOR`), and `RETURN`.
- `parse_expression()`: Implements operator precedence (Logic > Relational > Additive > Multiplicative).
- `parse_reference()`: Handles identifiers and member access (e.g., `obj.field`).

### 2. High-Level AST Definitions

The AST is a tree of Python objects. Each node corresponds to a Leda language construct.

#### Base Nodes
- `Node`: Abstract base class with line/column metadata.
- `Declaration(Node)`: Base for `VarDecl`, `ConstDecl`, `FunctionDecl`, `ClassDecl`.
- `Statement(Node)`: Base for `IfStmt`, `WhileStmt`, `Assignment`, etc.
- `Expression(Node)`: Base for nodes that return a `Value`.

#### Specific AST Nodes

| Category | Node Type | Fields |
| :--- | :--- | :--- |
| **Top Level** | `Program` | `decls: List[Declaration]`, `body: List[Statement]` |
| **Decls** | `VarDecl` | `names: List[str]`, `type: TypeNode` |
| | `FunctionDecl` | `name: str`, `args: List[Param]`, `ret_type: TypeNode`, `body: List[Statement]` |
| | `ClassDecl` | `name: str`, `parent: str`, `args: List[Param]`, `members: List[Declaration]` |
| **Statements**| `Assignment` | `target: Reference`, `value: Expression` |
| | `IfStmt` | `cond: Expression`, `then_part: Statement`, `else_part: Optional[Statement]` |
| | `WhileStmt` | `cond: Expression`, `body: Statement` |
| | `ForStmt` | `init: Node`, `limit: Expression`, `body: Statement` (handles relation and numeric loops) |
| | `ReturnStmt` | `value: Optional[Expression]` |
| **Expressions**| `BinaryOp` | `left: Expression`, `op: str`, `right: Expression` |
| | `UnaryOp` | `op: str`, `operand: Expression` |
| | `Literal` | `value: Any`, `type: str` (Integer, Real, String) |
| | `Identifier` | `name: str` |
| | `FunctionCall`| `target: Expression`, `args: List[Expression]` |
| | `Closure` | `params: List[Param]`, `ret_type: TypeNode`, `body: List[Statement]` |
| | `MemberAccess`| `receiver: Expression`, `member: str` |

### 3. Implementation Steps for the Junior Developer

1.  **Lexer**: Use Python's `re` module or `ply.lex` to generate tokens: `ID`, `ICONSTANT`, `SCONSTANT`, `ASSIGN`, `BEGINkw`, etc.
2.  **Parser**:
    - Implement the `expect(token_type)` helper to consume tokens or raise syntax errors.
    - Use the AST classes to build the tree bottom-up during recursive calls.
    - **Example**: `parse_if()` should call `parse_expression()` for the condition and `parse_statement()` for the branches.
3.  **Symbol Table (Pass 1)**:
    - Walk the AST.
    - Create `Scope` objects to track variable declarations.
    - Identify which variables are "Captured" (lexically used in nested `FunctionDecl` or `Closure` nodes).
4.  **Slot Planner (Pass 2)**:
    - Calculate the **High Water Mark** for each function.
    - Assign `FP` relative offsets to all variables and capture caches as defined in the **HWM Calculation** section.
5.  **Code Generator (Pass 3)**:
    - Traverse the AST and emit the 32-bit instruction words found in the **Opcode Table**.
```

---
