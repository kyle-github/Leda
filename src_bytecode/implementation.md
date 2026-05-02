# Leda Bytecode Implementation Plan

## Table of Contents

- [Purpose](#purpose)
- [Current Implementation Baseline](#current-implementation-baseline)
- [Target Architecture](#target-architecture)
- [Major Design Decisions](#major-design-decisions)
- [Portability Requirements](#portability-requirements)
- [Repository Layout](#repository-layout)
- [Execution Model](#execution-model)
- [Bytecode File Format](#bytecode-file-format)
    - [Lessons from Little Smalltalk images](#lessons-from-little-smalltalk-images)
- [Instruction Set](#instruction-set)
- [Compiler Design](#compiler-design)
- [Virtual Machine Design](#virtual-machine-design)
- [Performance Strategy](#performance-strategy)
- [Primitives](#primitives)
- [Closures Methods and Contexts](#closures-methods-and-contexts)
- [Variables References and Thunks](#variables-references-and-thunks)
- [Classes Objects and Pattern Matching](#classes-objects-and-pattern-matching)
- [Relations](#relations)
- [Memory Management Integration](#memory-management-integration)
- [Compiler Executable ledac](#compiler-executable-ledac)
- [VM Executable ledavm](#vm-executable-ledavm)
- [Migration Strategy](#migration-strategy)
- [Implementation Phases](#implementation-phases)
- [Testing Strategy](#testing-strategy)
- [Compatibility Risks](#compatibility-risks)
- [Recommended First Milestone](#recommended-first-milestone)

## Purpose

This document proposes how to change the Leda implementation from a direct AST-walking interpreter into a bytecode compiler plus a simple switch-based virtual machine.

The goal is not to redesign the whole language implementation at once. The goal is to preserve the existing front-end semantics, type system behavior, runtime object model, and garbage collector behavior as much as possible while replacing the execution backend.

The bytecode implementation should do that by copying the needed frontend and runtime support code into `src_bytecode/` and evolving it there. It should not link the new tools against the legacy `Src/` implementation files as a permanent architecture.

The resulting toolchain should consist of:

- `ledac`: compile `.led` or `.leda` source files into `.lbc` bytecode files
- `ledavm`: load `.lbc` bytecode files and execute them with a stack-based virtual machine

## Current Implementation Baseline

Today the system works like this:

1. `Src/lexer.l` tokenizes source files.
2. `Src/gram.y` parses the token stream.
3. `Src/lc.c`, `Src/types.c`, and `Src/gen.c` perform symbol handling, type handling, and AST construction.
4. `Src/interp.c` executes the resulting `expressionRecord` and `statementRecord` trees by recursive AST walking.
5. `Src/memory.c` provides runtime allocation and copying garbage collection.

The most important existing facts are:

- The implementation is not bytecode-based today.
- The parser already constructs semantically meaningful AST nodes.
- Function calls, closures, method contexts, references, thunks, and pattern matching are already represented explicitly in the AST and runtime.
- The runtime object model is already rich enough to support a VM without redesigning the language itself.

That means the lowest-risk migration is:

- copy the lexer, parser, symbol-table support, type checker support, and AST construction support into `src_bytecode/`
- keep `struct ledaValue` and the GC model semantically compatible while moving the needed code under `src_bytecode/`
- add a compiler backend from AST to bytecode
- add a VM backend for bytecode execution
- remove the AST interpreter only after bytecode execution reaches semantic parity

## Target Architecture

The new architecture should be:

1. Front end
   - lexer
   - parser
   - name resolution
   - type checking
   - AST construction
2. Compiler backend
   - AST to bytecode lowering
   - constant pool creation
   - code object construction
   - `.lbc` serialization
3. VM runtime
   - `.lbc` loading
   - bytecode dispatch loop
   - frame management
   - primitive dispatch
   - integration with `memory.c`

This splits the current monolithic interpreter into two explicit products:

- compiler product: source to bytecode
- runtime product: bytecode to execution

## Major Design Decisions

### Use a stack-based VM

The first VM should be stack-based, not register-based.

Reasons:

- Leda's current AST is expression-oriented, and a stack VM maps naturally from recursive expression trees.
- Stack bytecode avoids building a register allocator in the first implementation.
- A simple switch interpreter is easier to debug with stack semantics.
- Closures, references, thunks, and primitive calls can all be represented as ordinary stack operands.

A register VM may be worth considering later, but only after a correct stack VM exists and performance data shows that instruction dispatch and stack traffic are the main bottlenecks.

### Keep primitives as VM-dispatched built-ins

Primitives should not be dynamic host calls resolved by name at runtime. Instead:

- the compiler resolves each primitive name to a primitive id
- bytecode contains `CALL_PRIMITIVE primitive_id argc`
- the VM dispatches through a fixed C table

This preserves the current implementation model where `cfunction` targets come from a known fixed set rather than general-purpose foreign-function linkage.

### Preserve runtime object representation initially

The first bytecode implementation should continue using the current `struct ledaValue` runtime representation and the current collector design.

That does not require linking `ledac` or `ledavm` against the legacy interpreter objects. The needed runtime code should be copied into `src_bytecode/` and kept layout-compatible while the bytecode backend comes up.

This avoids coupling the bytecode transition with a second risky redesign in memory management.

## Portability Requirements

The `.lbc` format must be portable across:

- 32-bit hosts
- 64-bit hosts
- little-endian hosts
- big-endian hosts

That requirement has several concrete consequences.

### Never serialize native C structs

The bytecode file must never be produced by dumping in-memory C structs directly.

In particular, the file must not depend on:

- `sizeof(int)`
- `sizeof(long)`
- `sizeof(void *)`
- host struct padding
- host enum layout
- host endianness

Every field in the `.lbc` file must be written and read through explicit encoding routines.

### Never serialize raw pointers

The bytecode file must not contain:

- function pointers
- object pointers
- AST pointers
- host addresses of any kind

All cross-references in the file should be represented as:

- function indexes
- constant pool indexes
- string table indexes
- primitive ids
- relative code offsets

### Use canonical integer encodings

All serialized integers should use a machine-independent encoding.

The simplest good options are:

- fixed-width integers in a declared byte order
- fixed 64-bit little-endian scalar encodings

For this project, fixed 64-bit little-endian scalar fields are the best fit because they are:

- endian-independent
- compact
- easy to decode in a portable C implementation

Recommended use:

- opcodes: one byte each
- indexes, counts, arities, local counts, lengths: unsigned 64-bit little-endian
- signed integer constants and signed branch deltas: signed 64-bit little-endian

### Define one canonical floating-point encoding

Reals need an explicit portable representation.

There are two viable choices:

1. serialize all reals as IEEE-754 binary64 in a declared byte order
2. serialize all reals as decimal text strings in the constant pool

For Leda, the recommended choice is IEEE-754 binary64 with an explicitly defined byte order in the file format, because it is much smaller and faster.

That means:

- the file format defines real constants as 8-byte binary64 values
- the bytes are written in one canonical order regardless of host endianness
- the loader swaps bytes when necessary

If the implementation ever needs to run on non-IEEE floating-point hardware, the fallback should be to reject the file format version or introduce a separate portable-decimal constant encoding in a later version.

### Define one canonical byte order where fixed-width fields are used

If any fixed-width multi-byte fields remain in the format, the format must define their byte order explicitly.

The easiest rule is:

- all fixed-width fields in `.lbc` are little-endian

That rule is independent of the host architecture. A big-endian VM must swap as needed. A little-endian VM can read directly.

Using fixed-width 64-bit little-endian fields keeps decoding simple and makes patching operands straightforward.

### Define opcode widths independently of host word size

Instruction encoding must not depend on machine word size.

That means:

- opcodes are always 8-bit
- immediate operands are encoded with explicit byte widths or with LEB128
- jump offsets are measured in bytecode stream bytes, not host instruction units

### Keep runtime width and file-format width separate

Inside the VM, it is fine to use native machine word sizes for:

- stack indexes
- local array indexes
- instruction pointer variables

But at the serialization boundary, all values must be converted to and from the canonical on-disk representation.

The VM should treat file decoding as a translation step from portable `.lbc` representation into native in-memory structures.

## Repository Layout

The expected layout for the bytecode implementation can be:

```text
src_bytecode/
  implementation.md
    lexer.l
    gram.y
    frontend.h
    ast.h
    lc_frontend.c
    types_frontend.c
    gen_frontend.c
  bytecode.h
  bytecode.c
  bc_emit.h
  bc_emit.c
  bc_loader.h
  bc_loader.c
  vm.h
  vm.c
  primitives_vm.h
  primitives_vm.c
  ledac.c
  ledavm.c
```

Suggested responsibilities:

- `lexer.l`, `gram.y`: copied frontend grammar and scanner for the bytecode toolchain
- `frontend.h`: copied shared frontend structures needed by the bytecode compiler
- `ast.h`: copied AST layout definitions consumed by the bytecode emitter
- `lc_frontend.c`, `types_frontend.c`, `gen_frontend.c`: copied semantic-analysis and AST-construction support to be evolved locally under `src_bytecode`
- `bytecode.h/.c`: instruction encoding, module structures, helpers
- `bc_emit.h/.c`: AST to bytecode compiler
- `bc_loader.h/.c`: `.lbc` loader and serializer
- `vm.h/.c`: virtual machine loop and frames
- `primitives_vm.h/.c`: primitive dispatch table
- `ledac.c`: compiler command-line tool
- `ledavm.c`: bytecode runner command-line tool

## Execution Model

The VM should execute bytecode using:

- one operand stack
- one call-frame stack
- instruction pointers into bytecode arrays
- heap objects allocated through existing memory allocation routines

Each call frame should minimally contain:

- current function or code object pointer
- instruction pointer
- lexical environment pointer
- caller frame pointer or caller index
- locals array
- argument array
- stack base for this frame

This is the bytecode analogue of the current interpreter state, which presently spreads control through:

- `currentContext`
- AST recursion
- linked `statementRecord` control flow
- context objects containing arguments and locals

## Bytecode File Format

The `.lbc` format should be simple and explicit.

### Lessons from Little Smalltalk images

The Little Smalltalk VM in `~/Projects/littlesmalltalk/src/vm/image.c` is a useful reference because it shows two different serialization styles. It should be used for ideas and tradeoff comparisons, not as a final design template for Leda.

#### Version 2: near-direct image dump with pointer swizzling

Little Smalltalk image version 2 writes a memory image that is very close to a raw heap dump.

The key characteristics are:

- it writes saved base addresses and image bounds
- it writes core root pointers
- it writes the raw object memory region
- on load, it computes the delta between the saved base address and the current base address
- it fixes pointers by adding that delta to each serialized pointer value

Conceptually, this is a relocation-based memory image.

That design is attractive because:

- save and load can be fast
- heap objects keep their in-memory layout
- loader logic is simple once object traversal is available

But it also has hard limits.

It is not suitable as the main `.lbc` format for Leda because it depends on:

- host pointer size
- host object layout
- host alignment assumptions
- native endianness unless every field is explicitly normalized
- the exact heap representation of runtime objects

Even with pointer deltas, a raw or near-raw image still couples the file format to the runtime layout too tightly for the portability target of:

- 32-bit and 64-bit hosts
- big-endian and little-endian hosts

So the Little Smalltalk version 2 idea is useful only as background. It should not be the model for Leda's portable bytecode format.

#### Version 3: tagged object serialization

Little Smalltalk image version 3 moves away from raw heap dumping and instead writes a tagged object stream.

The important ideas are:

- each serialized entity starts with a compact tag
- tags encode object kind and payload size
- repeated objects are emitted once and then referred to indirectly
- integers and small values use compact encodings
- binary payloads are separated from pointer-bearing objects
- the file uses a defined byte ordering for multi-byte tag payloads

This is much closer to the kind of portable serialization Leda is likely to want for `.lbc`.

But it is still only a source of ideas. Leda should adopt only the parts that fit its own runtime, object model, and toolchain.

The specific ideas worth reusing are:

- explicit versioned file header
- tagged records instead of dumped native structs
- backreference or indirection ids for shared objects
- a canonical byte order for fixed-width fields
- explicit root object serialization rather than implicit process state dumps

#### How this applies to Leda

For Leda, `.lbc` should remain a fully architecture-neutral bytecode container.

That means:

- no raw heap dump
- no serialized pointers
- no dependence on `struct ledaValue` layout

Leda should not adopt a raw or near-raw version 2 style image format as the baseline.

The only version 2 idea worth carrying forward directly is this:

- it is useful to represent references by position relative to a serialized heap region and then rebuild native pointers at load time

Even there, the file should store offsets or ids, not literal machine pointers.

The important refinement is that Leda should not write literal C pointers in `.lbc`. Instead, it should write either:

- offsets from the start of the serialized heap image
- object indexes
- or relocation entries

That preserves the main benefit of pointer-delta loading while avoiding host-pointer-size coupling.

#### Recommended rule for Leda

Use Little Smalltalk as a design reference, not as a specification.

Concretely:

- for `.lbc`, prefer the same general direction as version 3: tagged, versioned, explicit, portable serialization
- do not treat either Little Smalltalk version as a final answer for Leda
- keep only the parts that match Leda's own runtime constraints and implementation goals

The `.lbc` format should be portable and semantic.

### File header

The header should contain:


#### Concrete frame definition

The first VM implementation should make that layout explicit with a native frame structure such as:

```c
struct bc_frame {
        struct bc_function *function;
        uint8_t *ip;
        struct bc_env *env;
        uint32_t caller_frame_index;
        uint32_t stack_base;
        uint32_t arg_base;
        uint32_t local_base;
        uint16_t arg_count;
        uint16_t local_count;
};
```

The important points are:

- `function` identifies the active bytecode body
- `ip` is the current instruction pointer within that body
- `env` names the lexical parent environment captured by the current closure call, or is null when the function has no captured outer scope
- a live frame may also be promoted into a heap environment when an escaping closure captures its locals or arguments
- `caller_frame_index` links back to the caller without storing a raw C stack pointer
- `stack_base` marks the start of this frame's operand-stack slice
- `arg_base` marks the first argument slot for frame-pointer-style argument access
- `local_base` marks the first temporary or local slot for frame-pointer-style local access
- `arg_count` and `local_count` are bounds for slot validation and tracing

This is intentionally frame-pointer-like even if the VM never materializes a separate frame-pointer register. The effective frame base is the tuple:

- operand stack array
- `arg_base`
- `local_base`
- current `bc_frame`

So `OP_LOAD_ARG 0` means "load from the current frame's argument area at `arg_base + 0`", and `OP_LOAD_LOCAL 0` means "load from the current frame's local area at `local_base + 0`".
- entry function index



The header should also carry format-capability bits that make portability rules explicit, such as:

### Concrete slot-access definitions

The slot-oriented bytecodes should be defined as frame-relative or environment-relative operations, not as symbolic lookups.

#### Locals and temporaries

- `OP_LOAD_LOCAL slot`
    - read `frame.locals[slot]`
    - equivalently, read operand-stack storage at `frame.local_base + slot`
- `OP_STORE_LOCAL slot`
    - write the top-of-stack value to `frame.locals[slot]`
    - leave the stored value on the operand stack unless a later `OP_POP` removes it

These opcodes cover ordinary locals and compiler-created temporaries. The compiler can place both in the same indexed local area.

#### Arguments

- `OP_LOAD_ARG slot`
    - read `frame.args[slot]`
    - equivalently, read operand-stack storage at `frame.arg_base + slot`
- `OP_STORE_ARG slot`
    - write the top-of-stack value to `frame.args[slot]`

Even if arguments are immutable by language convention in some cases, it is still useful to define `OP_STORE_ARG` because by-reference or lowered helper code may need a uniform writable slot model.

#### Captures

The current bytecode slice uses separate opcodes for captured locals and captured arguments, each with an explicit lexical depth:

- `OP_LOAD_CAPTURE_LOCAL depth slot`
    - walk `depth` lexical environments outward from the current frame
    - read the captured local slot from that promoted environment
- `OP_STORE_CAPTURE_LOCAL depth slot`
    - walk `depth` lexical environments outward from the current frame
    - write the top-of-stack value into the captured local slot
- `OP_LOAD_CAPTURE_ARG depth slot`
    - walk `depth` lexical environments outward from the current frame
    - read the captured argument slot from that promoted environment
- `OP_STORE_CAPTURE_ARG depth slot`
    - walk `depth` lexical environments outward from the current frame
    - write the top-of-stack value into the captured argument slot

These are never plain frame-stack accesses. They resolve through persistent promoted environments so a closure can outlive the function activation that created it.

#### Why this split matters

This separation keeps the common case fast:

- locals and temporaries use frame-relative indexed access
- arguments use frame-relative indexed access
- only captured variables pay the extra indirection through `env`

That is the bytecode equivalent of frame-pointer-based addressing with explicit closure lifting.

- real encoding kind
- debug info present or absent
- reserved feature flags for future format revisions

#### Concrete environment definition

The current implementation uses a regular promoted-environment record per captured activation. Conceptually it is:

```c
struct bc_env {
    struct bc_env *parent;
    uint32_t arg_count;
    uint32_t local_count;
    struct ledaValue **args;
    struct ledaValue **locals;
};
```

Where:

- `parent` links to the next outer lexical environment when nested closures capture outer scopes
- `arg_count` and `local_count` preserve the original frame layout boundaries
- `args[i]` stores captured argument slots for the activation
- `locals[i]` stores captured local or temporary slots for the activation

The VM promotes a live frame into one shared environment object the first time an escaping closure captures it. After that, both the still-running frame and any closures created from it read and write the same promoted slots. That gives the required shared-mutation semantics even after the creator later returns.

If boxed cells are needed later, each entry in `args` or `locals` can itself point to a heap cell object rather than directly to the value.

For example:

```c
struct bc_cell {
        struct ledaValue *value;
};
```

So the current model is:

- uncaptured local: stored directly in the current frame local area
- captured local or argument in a non-escaping activation: still accessible through the live frame until promotion happens
- captured local or argument in an escaping activation: moved into a shared promoted environment and accessed there thereafter

The closure object itself should then contain:

- callee function index or pointer
- pointer to the `bc_env`

It should not contain a pointer to the whole stack activation record.

### Constant pool

The constant pool should store:

- integers
- reals
- strings
- optional symbol names for diagnostics

Constants should not embed runtime object pointers. The loader should construct runtime values from serialized constants when needed.

Constant payloads should be encoded as follows:

- integers: signed 64-bit little-endian
- string lengths: unsigned 64-bit little-endian followed by raw bytes
- strings: raw bytes plus length, not null-terminated C layout
- reals: canonical IEEE-754 binary64 in the file's declared byte order

The constant pool should not serialize host-specific representations such as:

- native `double` byte dumps without declared endianness
- pointer-backed string objects
- object headers from `struct ledaValue`

### Function table

Each function record should contain:

- function name or debug name
- arity
- local count
- max stack depth
- flag bits
- bytecode length
- bytecode bytes
- optional source line table

Each of these fields should be serialized using canonical indexes and lengths. In particular:

- function names should be string-pool indexes or inline length-prefixed strings
- bytecode length should be a byte count, not a host instruction count
- source locations should use file indexes and line numbers encoded canonically

Useful flag bits include:

- top-level entry
- method body
- thunk body
- variadic if ever added later

Function records must not contain runtime pointers. Closures created at runtime should refer to functions by loaded function-table index or by pointer into native in-memory function descriptors built by the loader.

### Debug information

The initial implementation should carry enough information to produce reasonable runtime errors:

- source file id
- source line table by instruction offset
- function name

This is enough for stack traces and primitive failure messages.

## Instruction Set

The first instruction set should be small, regular, and easy to interpret.

### Core control instructions

- `OP_HALT`
- `OP_POP`
- `OP_DUP`
- `OP_JUMP offset`
- `OP_JUMP_IF_FALSE offset`
- `OP_RETURN`
- `OP_TAILCALL argc`

### Constant and variable access instructions

- `OP_CONST const_index`
- `OP_LOAD_GLOBAL slot`
- `OP_STORE_GLOBAL slot`
- `OP_LOAD_LOCAL slot`
- `OP_STORE_LOCAL slot`
- `OP_LOAD_ARG slot`
- `OP_STORE_ARG slot`

### Object and slot instructions

- `OP_LOAD_SLOT slot`
- `OP_STORE_SLOT slot`
- `OP_NEW_INSTANCE field_count`
- `OP_NEW_ARRAY`

### Function and closure instructions

- `OP_CALL argc`
- `OP_CALL_METHOD method_slot argc`
- `OP_CALL_PRIMITIVE primitive_id argc`
- `OP_MAKE_CLOSURE function_index context_depth`
- `OP_MAKE_THUNK function_index capture_count`

### Reference and thunk instructions

- `OP_MAKE_REF slot_kind slot_index`
- `OP_DEREF`
- `OP_STORE_REF`
- `OP_FORCE_THUNK`

### Type and match instructions

- `OP_IS_INSTANCE`
- `OP_PATTERN_MATCH field_count`

### Optional convenience instructions

These are not required initially, but may be useful:

- `OP_SWAP`
- `OP_ROT`
- `OP_LOAD_CAPTURE_LOCAL depth slot`
- `OP_STORE_CAPTURE_LOCAL depth slot`
- `OP_LOAD_CAPTURE_ARG depth slot`
- `OP_STORE_CAPTURE_ARG depth slot`

The design should prefer a few explicit instructions over many specialized ones.

## Compiler Design

The compiler should initially lower from the current AST rather than changing yacc actions to emit bytecode directly.

This means adding functions such as:

- `compileStatement(struct statementRecord *)`
- `compileExpression(struct expressionRecord *)`
- `compileFunctionBody(struct statementRecord *)`
- `compileTopLevel(struct symbolTableRecord *, struct statementRecord *)`

This approach has several advantages:

- no parser rewrite is needed
- existing semantic analysis remains intact
- the AST interpreter can stay available for comparison during migration
- the lowering pass becomes the only new semantic layer

However, the compiler should not stop at a naive one-to-one translation from AST nodes to bytecodes. If it does, the VM will still spend most of its time repeating the same kinds of indirections, dispatches, and metadata lookups that make the AST walker slow.

The AST is the right input to the compiler, but it is not the right execution model.

### AST to bytecode mapping

Representative mappings:

- integer/string/real constants -> `OP_CONST`
- current context or symbol lookup -> `OP_LOAD_LOCAL`, `OP_LOAD_ARG`, `OP_LOAD_GLOBAL`, or captured environment load
- assignment -> address formation plus store instruction
- `doFunctionCall` -> push callee, push args, `OP_CALL`
- `doSpecialCall` -> push args, `OP_CALL_PRIMITIVE`
- `makeClosure` -> `OP_MAKE_CLOSURE`
- `makeMethodContext` -> method lookup plus either a closure or `OP_CALL_METHOD`
- `buildInstance` -> `OP_NEW_INSTANCE`
- linked conditionals -> `OP_JUMP_IF_FALSE` and `OP_JUMP`
- return statements -> `OP_RETURN` or `OP_TAILCALL`

This mapping is only the starting point. The compiler should then normalize and simplify the result before final bytecode emission.

### Lowering should remove AST-level structure, not preserve it

The bytecode compiler should try to remove costs that the AST walker pays repeatedly:

- recursive descent through tree nodes
- tag checks on many tiny AST node kinds
- repeated symbol and slot interpretation at runtime
- method-call setup rebuilt from generic node structure every time
- control flow expressed indirectly through linked statement nodes

To do that, the compiler should lower into a flatter execution form where:

- names are already resolved to numeric indexes
- jump targets are already computed
- field offsets are already known where possible
- closure capture layouts are already decided
- primitive names are already resolved to primitive ids

### Add a normalization pass before final bytecode emission

Between AST lowering and final bytecode emission, the compiler should run a normalization pass over a simple internal instruction list or basic-block form.

That pass should do inexpensive but high-value transformations such as:

- constant folding
- dead temporary elimination
- removal of redundant `LOAD` followed immediately by `STORE` patterns
- elimination of useless `DUP` and `POP` pairs
- jump threading for `if` and `while` lowering
- combining simple address formation plus load/store into a direct opcode when legal
- conversion of generic calls into specialized call forms when arity is known

The goal is not to build a sophisticated optimizer. The goal is to prevent the bytecode from becoming a serialized copy of the AST.

### Control-flow lowering

The current AST uses linked `statementRecord` nodes for sequencing and conditional routing. The bytecode compiler must turn that into explicit jump offsets.

That requires:

- block labels
- backpatching jumps
- tracking fallthrough boundaries

The compiler should include a small assembler-like layer that emits instructions and later patches unresolved jump targets.

## Virtual Machine Design

The VM should use a simple `switch` loop.

### Main loop shape

The main loop should be conceptually:

```c
for (;;) {
    opcode = *ip++;
    switch (opcode) {
        case OP_CONST:
            ...
            break;
        case OP_CALL:
            ...
            break;
        case OP_RETURN:
            ...
            break;
        case OP_HALT:
            return;
    }
}
```

No threaded dispatch, computed goto, or JIT work should be attempted initially.

### Operand stack

The operand stack should hold `struct ledaValue *` values.

That is simpler than an unboxed tagged stack and is consistent with the current runtime model.

### Call frames

Each frame should contain:

- pointer to bytecode function descriptor
- instruction pointer into the function code
- pointer to lexical environment
- locals array of `struct ledaValue *`
- argument array of `struct ledaValue *`
- stack base index

This design keeps frame state separate from heap contexts while still allowing lexical captures to be boxed into heap objects.

Leda should use a normal VM activation stack for ordinary non-escaping calls. That is the default and preferred design.

In other words:

- ordinary activations live in a contiguous VM stack
- ordinary locals and arguments are indexed directly out of the current frame
- function return normally just pops the frame

This is both simpler and faster than allocating a heap object for every activation.

What prevents an all-stack design is not ordinary calling. It is closure capture.

If a nested function or thunk can outlive the call that created it, then any captured variables it refers to cannot remain only in a transient stack frame. Those values must be moved to, or represented by, a heap-resident environment.

### Tail calls

The current interpreter already distinguishes some tail-call cases. The VM should keep this by supporting `OP_TAILCALL`.

The first implementation may conservatively emit `OP_CALL` everywhere and add `OP_TAILCALL` later, but if tail-call behavior is important to current workloads it should be included earlier.

### Activation strategy

The recommended activation strategy is therefore hybrid:

1. use a normal VM stack for ordinary frames
2. use heap environments only for captured state that may escape

This is the usual high-performance design for languages with closures.

It gives most of the performance benefit of a traditional stack machine without giving up lexical closures.

## Performance Strategy

Bytecode will only outperform the AST walker if the implementation uses bytecode to remove interpretation overhead, not merely to rename AST node kinds.

The performance plan should therefore focus on four areas:

1. better lowering
2. better opcode design
3. cheaper dispatch and frames
4. faster common-case operations

### 1. Better lowering

The most important improvement is to compile away runtime decisions that the AST walker currently repeats.

#### Resolve everything possible at compile time

The compiler should resolve these to numeric metadata wherever semantics allow:

- global variable slots
- local variable slots
- argument slots
- captured-variable slots
- field offsets
- method table indexes
- primitive ids
- literal pool indexes

The VM should not repeatedly search symbol tables or reinterpret AST node structure.

#### Emit direct operations for common cases

Instead of always emitting general forms such as:

- make address
- wrap as reference
- dereference
- call generic helper

the compiler should emit direct bytecodes when the target is known to be:

- a local slot
- a global slot
- an argument slot
- an object field at a fixed offset

For example, a local assignment should become a direct local store, not a generic reference-building sequence, unless the source language semantics specifically require an explicit reference object at that point.

#### Compile calls into specialized shapes

If argument count is known, use specialized call opcodes such as:

- `OP_CALL_0`
- `OP_CALL_1`
- `OP_CALL_2`
- `OP_CALL_N`

Likewise for primitives and method sends when arity is small and common.

This reduces operand decoding, shortens bytecode streams, and simplifies hot call paths.

### 2. Better opcode design

The instruction set should eventually include fast-path opcodes for the operations that dominate real programs.

#### Add small, high-value specialized opcodes

Examples that are likely worth adding early:

- `OP_PUSH_NIL`
- `OP_PUSH_TRUE`
- `OP_PUSH_FALSE`
- `OP_PUSH_SMALLINT imm`
- `OP_LOAD_LOCAL_0` through `OP_LOAD_LOCAL_k`
- `OP_STORE_LOCAL_0` through `OP_STORE_LOCAL_k`
- `OP_LOAD_ARG_0` through `OP_LOAD_ARG_k`
- `OP_LOAD_CAPTURE_0` through `OP_LOAD_CAPTURE_k`
- `OP_CALL_PRIMITIVE_1` and `OP_CALL_PRIMITIVE_2` for common primitive arities

These are ordinary VM optimizations, not semantic changes.

#### Use superinstructions for common opcode pairs

Once the basic VM works, add superinstructions for frequent patterns such as:

- local load plus slot load
- constant load plus primitive call
- compare plus conditional branch
- method lookup plus call

Examples:

- `OP_LOAD_LOCAL_SLOT`
- `OP_CONST_CALL_PRIMITIVE`
- `OP_BRANCH_IF_FALSE_LOCAL`

These reduce dispatch count, which is one of the main reasons bytecode VMs outperform tree walkers.

The compiler or a post-pass can generate these from ordinary instruction sequences.

### 3. Cheaper dispatch and frames

The initial VM should use a simple `switch`, but the data path around that loop still matters.

#### Keep frame state contiguous and indexable

Frames should store locals, args, and stack base in arrays with direct integer indexing.

This is much cheaper than reconstructing activation structure through heap objects for every ordinary access.

Use heap environment objects only where semantics require long-lived captured state.

#### Avoid heap allocation for non-escaping activation data

The AST interpreter currently allocates context-shaped runtime objects frequently. The bytecode VM should avoid that for ordinary calls.

Preferred rule:

- ordinary frame locals and arguments live in VM frame storage
- escaping captures are copied or linked into heap environment objects only when needed

That keeps the common non-closure case much cheaper.

This should be treated as a core design rule, not a later micro-optimization.

If the VM allocates every activation as a heap object up front, much of the performance benefit over the current interpreter will be lost.

#### Keep the operand stack unboxed at the control level

It is acceptable for the operand stack to hold `struct ledaValue *`, but the stack machinery itself should be plain C arrays and indexes, not linked heap structures.

The control plane should be native and compact even if the language values remain boxed.

### 4. Faster common-case operations

#### Inline cache method dispatch

Method sends are likely to be a major cost. Add a monomorphic inline cache per call site after correctness is established.

Each method-call bytecode site can cache:

- last receiver class
- resolved method entry

Then the VM can do:

- compare receiver class
- jump directly to cached method when it matches
- fall back to full lookup otherwise

This is often a much bigger win than changing stack VM to register VM.

#### Add primitive fast paths for arithmetic and comparisons

If small integers are frequent, the VM should include fast paths for:

- integer addition
- integer subtraction
- integer multiplication
- integer comparison
- boolean tests

These can still preserve language semantics by falling back to the general primitive path when operands are not the expected type.

#### Specialize common load/store paths

The VM should make local/global/field access cheaper than generic reference access. If every operation goes through a fully generic object/reference pathway, the bytecode version will leave too much performance on the table.

### Practical guidance

The right performance sequence is:

1. get correctness with a clean stack VM
2. add normalization so bytecode is not AST-shaped
3. add specialized load/store/call opcodes
4. add superinstructions
5. add inline caches for method dispatch
6. add primitive fast paths for integers and booleans

This order matters. A bytecode VM with a good lowering pass and a few targeted fast paths will usually outperform an AST walker by a wide margin even before more advanced VM techniques are added.

### What not to do first

Do not start by:

- replacing the stack VM with a register VM
- adding a JIT
- introducing a complex optimizer
- making the file format more complicated for performance reasons

Those are later-stage options.

The first performance win should come from compiling away AST overhead and making the common bytecode operations direct and cheap.

## Primitives

Primitive calls in Leda currently come through `cfunction` expressions resolved to a fixed runtime set.

The bytecode design should preserve that as follows:

1. At compile time, map primitive names to numeric primitive ids.
2. Emit `OP_CALL_PRIMITIVE primitive_id argc`.
3. At runtime, dispatch through a fixed table of C handlers.

### Primitive handler signature

A good primitive interface is:

```c
typedef struct ledaValue *(*PrimitiveHandler)(
    struct VM *vm,
    int argc,
    struct ledaValue **argv
);
```

This gives primitives access to:

- the VM state
- argument count
- already-evaluated arguments

It also keeps primitive execution explicit and easy to trace.

### Primitive table requirements

The primitive table should:

- be fixed at build time initially
- provide name to id mapping for the compiler
- provide id to handler mapping for the VM
- provide optional debug name strings

### What not to do initially

Do not introduce:

- dynamic host symbol lookup
- user-defined foreign linkage
- bytecode-level direct C function pointers

That would complicate serialization, compatibility, and safety.

## Closures Methods and Contexts

Leda currently represents closures as objects containing:

- a lexical context pointer
- a code pointer stored as a `statementRecord *`

The bytecode equivalent should be a closure object containing:

- a lexical environment object pointer
- a bytecode function index or function descriptor pointer

### Lexical environments

Captured variables should live in heap-allocated environment objects so they remain valid after the creating frame returns.

That environment object can closely resemble today's context objects.

This does not mean every frame should be a heap object.

The better model is:

- stack frame first
- heap environment only when variables are captured by a closure that may outlive the frame

There are several implementation choices here.

### Closure implementation options

#### Option 1: copy captured values into a flat closure

The closure object can contain:

- function pointer or function index
- an inline array of captured values

This is simple and fast when:

- captured variables are immutable
- or later mutation of the original variable is not required to be shared

This is usually not sufficient for Leda if captured locals can be mutated and the mutation must be observed by multiple closures or by the still-running outer function.

#### Option 2: boxed cells for captured locals

When a local is captured, replace that local slot with a pointer to a heap cell. Both the stack frame and any closures then access the same cell.

This is a strong option for Leda because it preserves shared mutable variables while letting uncaptured locals remain ordinary stack slots.

This model is similar in spirit to upvalue boxing used in several bytecode VMs.

#### Option 3: heap environment object per escaping scope

When the compiler determines that a scope has captured variables, allocate one heap environment object containing those captured slots. Closures reference that environment plus a function index.

This is also a good fit for Leda because it resembles the current context-based implementation and can preserve lexical sharing across multiple nested closures.

#### Recommended closure strategy

For Leda, the best starting design is:

- keep uncaptured locals in normal stack frames
- identify captured locals at compile time
- store captured locals in heap cells or a heap environment object
- let closures reference only the captured subset, not the full activation record

This gives a good balance of:

- runtime speed
- implementation simplicity
- semantic correctness for mutable captures

The design should avoid heap-allocating the full activation unless the whole frame truly escapes, because that collapses back toward the current interpreter's cost structure.

### Open and closed captures

If the implementation uses boxed cells or upvalues, it is useful to distinguish:

- open captures: currently referring to a live stack slot
- closed captures: moved or finalized into heap storage after the defining frame returns

That gives another efficient implementation route:

- while the outer frame is active, closures can reference stack-backed capture descriptors
- when the frame is about to return, the VM closes them by copying the final values into heap cells

This is more complex than always allocating a heap environment immediately, but can be faster.

The initial Leda VM does not need this optimization first. It is enough to choose a design where only captured variables escape to the heap.

### Methods

Methods currently rely on class tables and method contexts. The new VM should preserve the same method lookup model:

- classes still carry method tables
- method lookup still resolves through class metadata
- invocation still binds a receiver context

The implementation may compile method calls in one of two ways:

1. explicit lookup plus ordinary call
2. `OP_CALL_METHOD`

For the first VM, `OP_CALL_METHOD` is reasonable because method dispatch is a distinct existing behavior.

## Variables References and Thunks

These are some of the most important semantics to preserve.

### Locals and globals

Globals should remain indexed slots in a global object or global array.

Locals should initially be frame slots indexed by integer offset.

### By-reference arguments

Today by-reference parameters are represented using explicit reference objects. That model should be preserved initially.

The VM should support:

- making a reference to a local, argument, object field, or global
- dereferencing it
- storing through it

This keeps language semantics stable and avoids hidden aliasing rules in the VM.

### By-name arguments

By-name arguments should continue to be compiled as thunks.

That means:

- the compiler generates a hidden thunk function body
- the caller packages the thunk as a closure with captures
- the callee forces it when needed

This is semantically closest to the current implementation.

## Classes Objects and Pattern Matching

### Classes and instances

Class table construction can remain close to current behavior:

- class objects still exist as runtime values
- method tables still live in class-related objects
- instance layout still uses object slots by convention

The compiler and VM should not redesign object layout at the same time as introducing bytecode.

### Pattern matching

The `is` pattern operation should be implemented either by:

- a dedicated `OP_PATTERN_MATCH` instruction
- or a sequence of `OP_IS_INSTANCE`, slot loads, and stores to references

A dedicated instruction is acceptable if it keeps the compiler simple. Internally it can still use the current class and field layout conventions.

## Relations

Relations should not receive a separate logic engine in the first bytecode implementation.

Leda relations are already implemented as ordinary closures and library-level control patterns. The VM should therefore treat them as ordinary function values.

This means:

- no separate relation stack
- no separate unification machine
- no Prolog-like engine

Relations should continue to work through:

- closures
- references
- thunks
- ordinary calls
- library code in standard Leda sources

## Memory Management Integration

The bytecode VM must integrate cleanly with the current copying collector.

### Root requirements

Today GC roots include global state, current contexts, and temporary root stacks. The VM will need equivalent root visibility for:

- operand stack values
- frame locals
- frame arguments
- current lexical environments
- global module references

There are two plausible approaches:

1. expose VM stacks to the collector through a root enumeration API
2. box more VM state into heap objects and keep using the current root stack model

The first approach is cleaner for a VM.

### Recommended collector integration

Add a root-enumeration hook used by `gcollect` so the VM can present all live frame and stack references. This avoids forcing the VM to allocate artificial context objects for every internal VM structure.

If collector support is reused from the old runtime, copy that collector code into `src_bytecode/` and adapt it there. Do not make the bytecode runtime depend on linking the old interpreter executable support objects.

## Compiler Executable ledac

`ledac` should:

1. parse command-line arguments
2. read one source file
3. run the copied bytecode frontend from `src_bytecode/`
4. build bytecode module structures
5. serialize `.lbc`

Recommended CLI shape:

```text
ledac input.led -o output.lbc
```

Useful later options:

- `--dump-ast`
- `--dump-bytecode`
- `--check-only`
- `--trace-compiler`

## VM Executable ledavm

`ledavm` should:

1. read `.lbc`
2. construct runtime constant values and global structures
3. initialize primitives and base classes
4. enter the module's top-level function
5. run until `OP_HALT` or uncaught error

Recommended CLI shape:

```text
ledavm program.lbc
```

Useful later options:

- `--trace-vm`
- `--trace-primitives`
- `--dump-module`
- `--stack-limit`

## Migration Strategy

The migration should be incremental.

### Keep the AST interpreter during transition

Do not delete `evaluateExpression` and `evaluateStatement` immediately.

Instead:

- keep the current interpreter as a reference implementation
- add bytecode compilation and VM execution beside it
- keep the copied bytecode frontend behavior-matched with the legacy frontend while avoiding direct linkage to `Src/`

### Compare behaviors on the same inputs

For a transition period, it should be possible to:

1. parse once
2. execute via AST interpreter
3. execute via bytecode VM
4. compare results and test outputs

This will reduce semantic drift and make debugging much easier.

In practice that comparison can still use the legacy interpreter as the oracle, but the bytecode compiler path should build from the copied frontend sources under `src_bytecode/`.

### Delay parser changes

Do not change yacc actions to emit bytecode directly until the bytecode backend is stable.

The AST gives a working semantic checkpoint. Eliminating it too early would remove the best correctness reference.

## Implementation Phases

### Phase 1: bytecode data structures

- define opcode enum
- define constant pool
- define function record
- define module record
- define serializer and loader

Deliverable:

- an `.lbc` file can be written and read back

### Phase 2: minimal VM

- implement operand stack
- implement frames
- implement `OP_CONST`, `OP_POP`, `OP_CALL`, `OP_RETURN`, `OP_JUMP`, `OP_JUMP_IF_FALSE`, `OP_HALT`
- implement basic constant loading

Deliverable:

- a hand-written bytecode program can run

### Phase 3: compile top-level and simple functions

- compile constants
- compile variable loads and stores
- compile sequencing and returns
- compile conditionals and loops
- compile direct function calls

Deliverable:

- simple arithmetic and control-flow programs run through `ledac` and `ledavm`

### Phase 4: primitive support

- implement primitive id mapping
- implement primitive table
- compile `cfunction`
- connect VM dispatch to runtime primitive handlers

Deliverable:

- built-in arithmetic, conversion, printing, allocation, and slot access work in the VM

### Phase 5: closures and lexical capture

- add closure objects with bytecode code pointers
- add captured environments
- compile nested functions and thunks

Deliverable:

- nested functions and by-name arguments work

### Phase 6: methods classes and instances

- compile class-generated methods
- implement method dispatch
- preserve class table conventions

Deliverable:

- object-oriented parts of the standard library work

### Phase 7: references pattern matching relations

- implement by-reference argument behavior
- implement `is` pattern handling
- validate relation library behavior

Deliverable:

- the current test suite exercises advanced language features through the VM

### Phase 8: parity and retirement

- compare AST and VM outputs on regression corpus
- fix semantic mismatches
- retire AST interpreter if desired

Deliverable:

- bytecode VM becomes default execution engine

## Testing Strategy

Testing should happen at multiple levels.

### Bytecode unit tests

Test:

- opcode encoding and decoding
- constant pool serialization
- jump patching
- line table correctness

### VM unit tests

Test:

- stack operations
- call and return behavior
- tail calls
- primitive dispatch
- environment capture

### Compiler tests

For selected AST forms, check emitted bytecode structure for:

- arithmetic expressions
- assignment statements
- conditionals
- loops
- function calls
- primitive calls

### End-to-end tests

Use existing Leda test programs from `Test/` to compare:

- current interpreter output
- bytecode VM output

### Differential testing

The most valuable migration test is differential execution between:

- current AST interpreter
- new bytecode VM

If both use the same front end, then mismatches are almost certainly in bytecode lowering or VM runtime behavior.

## Compatibility Risks

The main risks are semantic, not structural.

### Closures and environments

If lexical capture changes, nested functions and methods will silently misbehave.

### Reference semantics

By-reference parameters are central to relations and several library idioms. If references are not preserved exactly, relation behavior will break.

### Thunk semantics

By-name arguments must preserve delayed evaluation and environment capture.

### Primitive behavior

The current implementation relies heavily on built-in primitives for objects, arrays, integers, reals, strings, printing, and casting. Primitive mismatches will cascade into most of the language runtime.

### GC root visibility

If VM stacks and frames are not correctly visible to the collector, failures will be intermittent and difficult to debug.

## Recommended First Milestone

The first milestone should be intentionally limited.

Support only:

- integer constants
- string constants
- global variables
- local variables
- assignment
- straight-line execution
- `if`
- `while`
- ordinary function calls
- returns
- a minimal primitive set for integer ops and printing

Do not begin with:

- methods
- closures
- by-name arguments
- by-reference arguments
- relations
- pattern matching

Those features should come after the compiler, module format, VM loop, frame model, and primitive mechanism are all known to work.

The right first success condition is:

- `ledac` compiles a simple source file to `.lbc`
- `ledavm` executes that `.lbc`
- output matches the current interpreter for a small regression subset

That creates a stable base for the more difficult semantic features later.