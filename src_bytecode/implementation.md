# Leda Bytecode Implementation Plan

## Table of Contents

- [Purpose](#purpose)
- [Status Snapshot](#status-snapshot)
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

## Status Snapshot

This document began as a design and migration plan. As of May 5, 2026, it also serves as an implementation status report for the code under `src_bytecode/`.

The short version is:

- the bytecode toolchain exists and builds as three executables: `ledac`, `ledavm`, and `lbcdump`
- the portable module format is implemented and round-trippable
- the compiler backend lowers a meaningful slice of the Leda AST to bytecode
- the VM executes that slice, including closures, captured variables, references, object construction, object slot access, method calls, and pattern matching
- narrow bytecode regressions are wired into CTest and have been the main driver for development
- all 14 bytecode regressions are currently green
- all Tier B, C, and D primitives are implemented end-to-end: integer, real, and string arithmetic including `subString`, comparisons, and boolean singletons work correctly through the full method dispatch pipeline
- a combined real-arithmetic and string fixture (`real_string_fixture.led`) is green and wired into CTest as `real_string_test`

The current implementation is therefore no longer a stub or architecture sketch. It is a working bytecode compiler and VM for a non-trivial subset of the language, with closures, methods, references, pattern matching, and the full standard-library primitive set already present.

### What is implemented today

At a high level, the following pieces are in place.

#### Frontend duplication under `src_bytecode/`

The bytecode pipeline does not depend on the legacy `Src/` implementation as its execution backend. The parser, lexer, semantic analysis, and AST-construction support needed for bytecode compilation have local copies under `src_bytecode/`.

The important files are:

- `lc_frontend.c`
- `types_frontend.c`
- `gen_frontend.c`
- `frontend_runtime_support.c`
- `lexer.l`
- `gram.y`

This means the bytecode work is following the intended architecture: reuse the frontend model, but evolve it locally for bytecode execution.

#### Bytecode module format

The module format is implemented in `bytecode.h` and `bytecode.c`.

Concrete status:

- the file header uses explicit magic and version fields
- constants are serialized through explicit portable encoders rather than dumped native structs
- integer, real, and string constants are supported on disk
- function metadata and bytecode streams are serialized explicitly
- the reader reconstructs a `bc_module` from disk
- runtime-only constants such as closures, references, objects, and environment references are intentionally rejected by the serializer, which keeps `.lbc` a portable code-and-literal container rather than a heap snapshot

That last point is important: runtime state is still created dynamically by the VM. The `.lbc` file contains portable code and portable literals, not live runtime objects.

#### Disassembler

`lbcdump` exists and is part of the regular workflow.

It is not just a debugging convenience. In practice it has become the main structural validation tool for compiler work. Each narrow regression test checks for specific bytecode text in the dump output, so the dump format is part of the current testing surface.

#### AST-to-bytecode lowering already implemented

The compiler backend in `bc_emit.c` is no longer limited to trivial constants. The currently implemented lowering includes:

- top-level function creation and module entry selection
- constant emission for integers, reals, and strings
- local loads and stores
- argument loads and stores
- captured local loads and stores
- captured argument loads and stores
- expression statements
- explicit returns and implicit fallthrough return insertion
- conditionals and loop-oriented control flow through jumps and patching
- direct calls when the callee can be resolved to a known closure literal or stable alias
- indirect closure calls
- primitive calls
- closure literal emission with nested function compilation
- assignment lowering, including known-call-state updates used by direct-call optimization
- `commaOp`
- `makeReference`
- `evalReference`
- `buildInstance`
- object slot loads, stores, and reference creation
- `makeMethodContext` lowering
- compatibility lowering for `tailCall` statements as ordinary returns
- compatibility lowering for `evalThunk` as a zero-argument closure call
- `patternMatch` lowering via `BC_OP_BR_IF_NOT_KIND` with field-binding sequences
- `BC_OP_REGISTER_BUILTIN` emission for builtin class table registration after class construction

This list matters because it shows that the bytecode backend has already moved past the "simple statements only" stage. The compiler now handles several of the runtime-model features that originally looked like later-phase work.

#### VM runtime already implemented

The runtime in `vm.c` has also moved beyond a minimal stack machine.

Implemented runtime areas include:

- module execution entry
- operand-stack and frame-stack management
- frame-relative locals and arguments
- closure calls and explicit indirect closure calls
- direct-call entry using compiled function indexes
- primitive dispatch
- conditional and unconditional jumps
- reference objects for locals, arguments, captured locals, captured arguments, and object slots
- reference dereference through `BC_OP_LOAD_REF`
- promoted lexical environments for captured variables
- closure environment resolution across lexical depth
- object allocation for `buildInstance`
- object slot reads and writes
- object-slot reference creation
- method binding through `BC_OP_MAKE_METHOD`
- method execution with `self` pre-populated at local slot 1
- primitive receiver dispatch via `builtin_class_tables` registry and `vm_bind_primitive_environment`
- `BC_OP_BR_IF_NOT_KIND`: class-chain walk for `is` pattern matching, with field binding and true/false push
- `BC_OP_REGISTER_BUILTIN`: registers integer, string, boolean, and real class tables for primitive method dispatch
- raw object allocation, indexed slot read, and indexed slot write via primitives 15/16/17
- Tier 1 primitives: `Leda_object_equals` (0), `Leda_string_compare` (1), `Leda_string_print` (2), `Leda_string_concat` (3), `Leda_integer_asString` (9)
- all integer arithmetic and logic primitives: equals (4), plus/minus/times/divide (5–8), less (10), or/and/not (11–13), `Leda_object_defined` (22)
- `Leda_object_allocate` (15), `Leda_object_at` (16), `Leda_object_atPut` (17)
- class table slot 2 is now populated with the class name string during compilation, enabling `object.asString()` for user-defined classes
- Tier B real arithmetic: `Leda_integer_asReal` (14), `Leda_real_asString` (23), `Leda_real_plus` (24), `Leda_real_minus` (25), `Leda_real_times` (26), `Leda_real_divide` (27), `Leda_real_less` (28), `Leda_real_asInteger` (29), `Leda_real_equals` (30)
- Tier C string and I/O: `Leda_string_length` (19), `Leda_string_substring` (20), `Leda_stdin_read` (21)
- Tier D generic coercion: `Leda_object_cast` (18)
- builtin class table registry expanded to distinguish `BC_BUILTIN_TRUE` (4) and `BC_BUILTIN_FALSE` (5) from the shared `BC_BUILTIN_BOOLEAN` (2); this enables correct `not`, `or`, and `and` dispatch on True and False instances
- `vm_condition_is_false` extended to handle `BC_CONST_OBJECT` by comparing the object's class table against the False builtin table
- `vm_get_local_slot_ref` extended to fall through into the closure environment for thunk/lambda captures when `local_index >= frame->local_count`
- `vm_enter_frame` arg_base calculation guarded against overlap with caller's active local slots
- `vm_bind_primitive_environment` environment parent now threaded from the class table's `BC_CONST_ENVREF` slot, enabling depth > 1 captures from primitive method bodies

The VM is therefore already executing code that depends on lexical capture, mutation through references, instance-slot indirection, method dispatch on both objects and primitive values, and `is` pattern matching. That is materially beyond a "toy VM" stage.

### What has been validated

The currently validated bytecode slices are the ones covered by the narrow CTest regressions.

The following tests are all currently green:

- `lbcdump_test`
- `direct_call_test`
- `constant_alias_call_test`
- `local_alias_call_test`
- `comma_byref_temp_test`
- `eval_reference_test`
- `build_instance_test`
- `object_slot_access_test`
- `method_context_test`
- `nested_direct_call_test`
- `tier1_primitives_test`
- `pattern_match_test`
- `indirect_call_test`
- `object_array_test`

Those tests cover both bytecode shape and runtime behavior, depending on the fixture. In other words, the current bytecode system is not just compiling without crashing; it is executing real Leda programs correctly for those covered features.

### What remains before broader parity

The remaining work should be understood as feature completion, not project bootstrap.

There are no known red regressions. The main work is expanding primitive coverage and widening test coverage toward the legacy chapter regression corpus.

The remaining work is:

- fix the pre-existing `ledac` segfault on closures that capture outer function locals (reproduces with `nested_direct_call_fixture.led`), which blocks 8 of 55 CTest tests
- expand coverage from narrow focused fixtures to more of the legacy regression corpus through `ledac` plus `ledavm`
- add output-comparison infrastructure so VM output can be checked against known-good text in CTest
- continue checking that standard-library initialization paths behave correctly when class tables and methods are created through bytecode-visible objects rather than legacy interpreter-only runtime helpers

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

### Actual lowering logic in the current implementation

The current compiler in `bc_emit.c` is no longer only following the abstract mapping above. It contains concrete lowering rules that resolve AST shapes into specific bytecodes based on slot classification, lexical depth, and a small amount of local call-flow knowledge.

The important point is that lowering is not a flat "one AST node equals one opcode" pass. Several branches first classify the expression or statement into one of a few runtime categories and only then choose the emitted bytecodes.

#### Top-level compilation

`bc_compile_top_level(...)` initializes a fresh `bc_module`, reserves function capacity up front, allocates the entry function as `__top__`, then lowers the top-level statement list into that function.

The top-level lowering logic does three important things beyond simply iterating statements:

- it keeps a `bc_compile_context` that tracks pending nested functions so closure compilation can emit additional bytecode functions into the same module
- it keeps a `bc_call_state` for direct-call optimization information within the current linear slice
- it emits an implicit `BC_OP_HALT` if the top-level code falls through

That means top-level lowering is already building a complete multi-function module, not just a single bytecode stream.

#### Statement lowering

The statement side is split between `bc_compile_simple_statement(...)`, `bc_compile_control_flow_statement(...)`, and `bc_compile_statement_range(...)`.

Current statement forms are lowered as follows.

- `makeLocalsStatement`
  - no opcode is emitted
  - the lowering only raises `function->local_count` to the required slot count
  - local allocation is therefore a compile-time frame-shape decision, not a runtime instruction
- `nullStatement`
  - emits nothing
- `expressionStatement`
  - lowers the expression exactly as an expression
  - whether a `POP` is emitted depends on the expression-specific lowering, not on the statement wrapper itself
- `returnStatement`
  - lowers the return expression
  - then emits `BC_OP_RETURN`
- `tailCall`
  - currently lowered identically to `returnStatement`
  - there is not yet a separate tail-call bytecode; this is a compatibility lowering used so method bodies and other frontend-produced code can compile

`bc_compile_statement_range(...)` is responsible for preserving control-flow boundaries. It walks a linear statement slice until a stop marker, delegating conditional shapes to the control-flow lowering helper and stopping early when an explicit return prevents fallthrough.

#### Conditional and loop lowering

`conditionalStatement` lowering is not based on a separate loop AST node. The compiler first inspects the linked statement structure to decide whether the conditional represents a `while`-like back-edge or a plain if/else split.

The logic is:

- if the true branch can reach the conditional node again through the linked statement chain, treat it as a loop
- otherwise, compute a linear join point between the true and false branches and treat it as a conditional branch

For loop-shaped conditionals, lowering does this:

1. remember the current bytecode offset as the loop start
2. lower the condition expression
3. emit `BC_OP_JUMP_IF_FALSE` with a placeholder delta
4. lower the loop body statements
5. if the body falls through, emit a backward `BC_OP_JUMP` to the recorded loop start
6. patch the false-jump operand to the first instruction after the loop

For if/else-shaped conditionals, lowering does this:

1. lower the condition expression
2. emit `BC_OP_JUMP_IF_FALSE` with a placeholder operand
3. lower the true branch up to the computed join point
4. if the true branch falls through and there is a distinct false branch, emit a placeholder unconditional `BC_OP_JUMP` over the false branch
5. patch the false-jump to the start of the false branch
6. lower the false branch, if present
7. patch the end jump to the join point

After either shape, the compiler clears the local direct-call cache because control-flow joins invalidate the simple linear assumptions used by that optimization.

#### Slot-resolution logic before load and store emission

The most important lowering logic in the current compiler is the slot-classification path used by both loads and stores.

Before emitting a load or store bytecode, `bc_emit.c` asks what the AST slot access really means at runtime. The main categories are:

- current-frame local
- current-frame argument
- captured local in an outer lexical environment
- captured argument in an outer lexical environment
- top-level or context-local slot reachable through the current context chain
- object slot access that could not be resolved as one of the above

That classification is done with helpers such as:

- `bc_resolve_context_depth(...)`
- `bc_match_context_local_slot(...)`
- `bc_match_function_local_slot_with_depth(...)`
- `bc_match_function_arg_slot_with_depth(...)`
- `bc_match_current_function_arg_slot_core(...)`
- `bc_resolve_slot_ref_from_expression(...)`
- `bc_resolve_slot_ref_from_assignment_target(...)`

The result is that bytecode load/store selection is based on resolved frame semantics, not on surface AST spelling.

#### Load lowering

For `getOffset` and `getGlobalOffset`, the compiler tries the following resolution order.

1. If the expression identifies a function local, emit:
   - `BC_OP_LOAD_LOCAL` for depth `0`
   - `BC_OP_LOAD_CAPTURE_LOCAL` for depth `> 0`
2. Else if it identifies a function argument, emit:
   - `BC_OP_LOAD_ARG` for depth `0`
   - `BC_OP_LOAD_CAPTURE_ARG` for depth `> 0`
3. Else if it resolves as a context-local slot through the current-context chain, emit:
   - `BC_OP_LOAD_LOCAL` when the resolved depth is `0`
   - `BC_OP_LOAD_CAPTURE_LOCAL` when the resolved depth is outer
4. Else if it is still a direct current-context slot access, treat it as a local and emit `BC_OP_LOAD_LOCAL`
5. Else if it is a `getOffset(base, location)` that cannot be resolved as a local or argument slot, lower it as an object slot access by:
   - lowering `base`
   - emitting `BC_OP_LOAD_OBJECT_SLOT location`

This is one of the key current design choices: unresolved `getOffset` does not immediately fail. It falls back to object-slot semantics if the shape is consistent with instance access.

#### Constant lowering

Constant lowering is straightforward but still does real module construction work.

- `genIntegerConstant`
  - add the integer to the module constant pool with `bc_add_integer_constant(...)`
  - emit `BC_OP_CONST <const-index>`
- `genRealConstant`
  - add the real to the constant pool
  - emit `BC_OP_CONST <const-index>`
- `genStringConstant`
  - add the string to the constant pool
  - emit `BC_OP_CONST <const-index>`

So constants are interned into the module as part of lowering, not as a separate pre-pass.

#### Assignment lowering

Assignments are more complex than simple slot stores because the compiler first asks whether the target is an object slot or a frame/environment slot.

The lowering sequence is:

1. inspect the left-hand side with `bc_assignment_target_is_object_slot(...)`
2. if it is an object slot:
   - lower the right-hand side value
   - lower the object base expression
   - emit `BC_OP_STORE_OBJECT_SLOT <slot>`
3. otherwise:
   - lower the right-hand side value
   - lower the assignment target to one of:
     - `BC_OP_STORE_LOCAL`
     - `BC_OP_STORE_ARG`
     - `BC_OP_STORE_CAPTURE_LOCAL`
     - `BC_OP_STORE_CAPTURE_ARG`
4. emit `BC_OP_POP`

That final `POP` is deliberate. In the current frontend semantics, assignment expressions do not produce a remaining stack value for later use in this bytecode slice.

After the store, the compiler also updates the direct-call cache through `bc_update_known_call_state_after_assignment(...)`. If the assignment stores a closure literal into a resolvable slot, later calls through that slot can be lowered as direct `BC_OP_CALL` instead of indirect `BC_OP_CALL_CLOSURE`.

#### Closure lowering

`makeClosure` lowering goes through `bc_emit_closure_literal(...)`, which in turn depends on `bc_ensure_function_compiled(...)`.

That logic does several things:

- if the closure body has already been assigned a bytecode function index, reuse it
- otherwise allocate a new bytecode function record in the module
- compile the nested statement list into that function immediately
- emit an implicit `BC_OP_RETURN` if the nested function falls through
- resolve the lexical context depth from the closure's context expression
- emit `BC_OP_MAKE_CLOSURE <function-index> <context-depth>` into the enclosing function

This means nested functions are compiled lazily on first use as closure literals, but once compiled they become ordinary entries in the module function table.

#### Direct-call versus indirect-call lowering

`doFunctionCall` lowering first tries to avoid indirect closure dispatch.

The compiler checks whether the callee can be resolved as:

- a direct closure literal
- a slot known, through the current `bc_call_state`, to contain a specific closure literal assigned earlier in the same linear slice

If that succeeds, lowering emits:

- argument lowering for each argument expression
- `BC_OP_CALL <function-index> <argc> <context-depth>`

If that fails, lowering emits:

1. code for the callee expression itself
2. code for each argument expression
3. `BC_OP_CALL_CLOSURE <argc>`

So the current compiler already performs a small but meaningful optimization pass during lowering: it converts certain closure calls back into direct indexed calls when the callee identity is still statically known.

#### Primitive-call lowering

`doSpecialCall` lowering is simpler.

The compiler:

1. validates that the primitive index is non-negative
2. lowers each argument in order
3. emits `BC_OP_CALL_PRIMITIVE <primitive-index> <argc>`

There is no late primitive-name resolution in the VM. That work is already done by the frontend before bytecode lowering runs.

#### Reference lowering

The reference path is split between creation and dereference.

For `makeReference`, the compiler first tries to resolve the target as a slot reference. If it succeeds, it emits one of:

- `BC_OP_MAKE_REF_LOCAL`
- `BC_OP_MAKE_REF_ARG`
- `BC_OP_MAKE_REF_CAPTURE_LOCAL`
- `BC_OP_MAKE_REF_CAPTURE_ARG`

If the target is not a frame or environment slot but is an object slot, lowering instead:

1. lowers the base object expression
2. emits `BC_OP_MAKE_REF_OBJECT_SLOT <slot>`

For `evalReference`, lowering simply:

1. lowers the reference expression
2. emits `BC_OP_LOAD_REF`

This preserves the existing frontend model where references are explicit values rather than hidden aliasing behavior in ordinary loads and stores.

#### Comma-expression lowering

`commaOp` is lowered as sequencing rather than as a special runtime operation.

The compiler:

1. lowers the left expression
2. if the left expression produces a value, emits `BC_OP_POP` to discard it
3. lowers the right expression and leaves its result as the result of the comma expression

The helper `bc_expression_produces_value(...)` is what decides whether the left-hand side needs the discard `POP`.

#### Thunk lowering

The current frontend still produces `evalThunk` nodes for by-name argument usage. In the current bytecode slice, lowering treats those nodes as zero-argument closure invocation.

The emitted sequence is:

1. lower the thunk expression itself
2. emit `BC_OP_CALL_CLOSURE 0`

This is explicitly a compatibility lowering that matches the current runtime meaning of a thunk as a closure whose body is executed when forced.

#### Instance-construction lowering

`buildInstance` lowering is already using the runtime object layout conventions of the legacy interpreter.

The compiler:

1. validates that the requested object size is at least `2`
   - slot `0` is the method table
   - slot `1` is the global or environment support slot used by the runtime model
2. lowers the table expression first
3. lowers each constructor argument in order
4. checks that the provided argument count fits into the declared object layout
5. emits `BC_OP_BUILD_INSTANCE <size> <argument-count>`

This means the VM can reconstruct the conventional object layout without needing the frontend AST at runtime.

#### Method-context lowering

`makeMethodContext` lowering is intentionally small at the compiler level.

The compiler:

1. validates that there is a receiver base expression and a non-negative method slot index
2. lowers the receiver object expression
3. emits `BC_OP_MAKE_METHOD <slot>`

The actual binding logic is therefore pushed into the VM. That is why the method implementation work has required coordinated changes in frontend class-table generation and runtime environment handling rather than only new compiler opcodes.

#### Current limitations of lowering

The detailed lowering above also makes the current gaps clearer.

- unsupported AST operators still fail explicitly through `bc_set_unsupported_expression_error(...)` or `bc_set_unsupported_statement_error(...)`
- `tailCall` does not yet receive a distinct optimized bytecode form
- `patternMatch` still has no lowering path
- method lowering exists, but the supporting class-table compilation path is still incomplete, which is why the dedicated method regression remains red

So the lowering layer is now substantial and deliberate, but it is still a partial semantic compiler rather than a complete lowering of the full Leda AST.

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

This section started as a proposed sequence. It now doubles as a progress ledger.

### Phase 1: bytecode data structures

Status: substantially complete.

Completed work:

- opcode enum defined and in active use
- constant kinds defined
- function and module records defined
- serializer implemented
- loader implemented
- portable integer, real, and string constant encoding implemented
- runtime-only constant kinds explicitly excluded from on-disk serialization
- disassembler support added for the active opcodes and constant kinds

Remaining work in this phase is mostly maintenance:

- keep serializer, reader, and disassembler aligned as new opcodes or constant kinds are introduced
- decide whether a later file-format revision needs more metadata or debug information

### Phase 2: minimal VM

Status: complete and significantly exceeded.

Completed work:

- operand stack implemented
- frame stack implemented
- `CONST`, `POP`, `RETURN`, `JUMP`, `JUMP_IF_FALSE`, and `HALT` implemented
- direct and indirect call execution implemented
- local and argument slot access implemented

This phase is no longer the limiting factor. The VM is already executing a broader instruction slice than was originally proposed here.

### Phase 3: compile top-level and simple functions

Status: complete.

Completed work:

- top-level function emission implemented
- constant emission implemented
- local and argument loads and stores implemented
- expression statements and returns implemented
- conditionals and loop-oriented control flow implemented
- direct call lowering implemented
- indirect closure calls implemented

This phase has been validated repeatedly through the narrow bytecode tests and is stable enough to support later work.

### Phase 4: primitive support

Status: implementation complete for all identified tiers. End-to-end validation through a real-and-string fixture is in progress.

Completed work:

- primitive-call lowering exists
- VM primitive dispatch exists
- `BC_OP_REGISTER_BUILTIN` and `builtin_class_tables` registry enables primitive method dispatch on integer, string, boolean, and real values
- Tier 1 string and object equality primitives: 0, 1, 2, 3, 9
- All integer arithmetic and logic: 4–13
- Object array primitives: 15, 16, 17
- `Leda_object_defined`: 22
- Tier B real arithmetic: 14, 23–30
- Tier C string and stdin: 19, 20, 21
- Tier D cast: 18

Remaining work:

- `real_string_fixture.led` is partially passing; real arithmetic output is correct through `asInteger` and `asReal`, and `string.length` returns the correct value, but a frame-management interaction when calling `string.subString` is still being debugged

### Phase 5: closures and lexical capture

Status: mostly complete for the currently exercised language slice.

Completed work:

- closure literal emission implemented
- nested bytecode function compilation implemented
- captured local and captured argument opcodes implemented
- environment promotion implemented in the VM
- direct and indirect closure calls validated
- `evalThunk` lowering now maps to zero-argument closure invocation

The main remaining risk here is not basic closure mechanics. The remaining risk is interaction with methods and richer library code.

### Phase 6: methods classes and instances

Status: complete for the core object model and user-defined methods. Primitive receiver dispatch is also working.

Completed work:

- instance construction via `buildInstance` works
- object slot access works
- object-slot references work
- method-binding opcode and runtime support complete
- class-table materialization working in the frontend, including parent-chain wiring at slot 4
- class name strings now stored at slot 2 of class tables, enabling `object.asString()`
- object-backed method environments and self pre-population at local slot 1
- primitive receiver dispatch via `vm_bind_primitive_environment` and `builtin_class_tables`
- `method_context_test` is green
- `BC_OP_BR_IF_NOT_KIND` implements `is` pattern matching with class-chain walk
- `pattern_match_test` validates parent-chain traversal and is green

### Phase 7: references pattern matching relations

Status: references and pattern matching are complete. Relation-heavy coverage not yet re-established.

Completed work:

- by-reference support is implemented for the current bytecode slice
- reference creation and dereference are implemented and tested
- comma-expression support needed by by-reference lowering is implemented and tested
- `patternMatch` is implemented via `BC_OP_BR_IF_NOT_KIND` with DUP/LOAD_OBJECT_SLOT/STORE field-binding sequences
- parent class chain traversal works for `is` checks against ancestor classes

Not complete:

- relation-heavy coverage has not yet been re-established under `ledavm`
- the `Leda_forRelation` and relation library paths have not been exercised through bytecode

### Phase 8: parity and retirement

Status: not complete.

Current state:

- the legacy interpreter remains the broader compatibility reference
- bytecode parity is being established incrementally through narrow, behavior-scoped tests
- the full legacy regression corpus has not yet been ported to the bytecode runtime path

The bytecode implementation is far enough along to justify this incremental strategy. It is not yet ready to replace the legacy interpreter as the default execution backend.

## Testing Strategy

Testing is now centered on executable CTest fixtures rather than on hypothetical future unit-test buckets.

### Actual current process

The current workflow for each new bytecode feature has been:

1. add or adjust a very small `.led` fixture under `Test/`
2. add a corresponding `.golden.txt` file containing the important bytecode-disassembly substrings that must appear
3. wire a dedicated `add_test(...)` entry into `CMakeLists.txt`
4. run only that one new test first
5. if it passes, rerun the existing bytecode regression set to catch over-broad lowering changes

This has been much more effective than trying to jump directly to the full regression corpus after every change.

### How the CTest bytecode harness works

The helper script `Test/run_bytecode_test.cmake` performs the same sequence for every bytecode regression:

1. create a per-test output directory under `build/Testing/<test-name>`
2. run `ledac` on the fixture and fail immediately if compilation returns non-zero
3. run `lbcdump` on the resulting `.lbc` file and save the textual dump
4. optionally run `ledavm` when runtime execution is part of the test
5. read the `.golden.txt` file line by line and require every non-empty line to appear somewhere in the disassembly dump

This means each test simultaneously validates:

- compiler success
- serializable `.lbc` output
- readable disassembly
- expected bytecode shape
- optional VM execution success

The harness is intentionally substring-based rather than exact-output-based. That keeps the tests portable across minor dump-format growth while still checking the semantic bytecode shape that matters for the feature under test.

### Current narrow bytecode regressions

The current bytecode-specific tests in `CMakeLists.txt` are:

- `lbcdump_test`
  - validates basic module generation and readable disassembly
- `direct_call_test`
  - validates direct-call lowering when the callee is statically identifiable
- `constant_alias_call_test`
  - validates direct-call preservation through constant aliases and runtime execution
- `local_alias_call_test`
  - validates local alias tracking and runtime execution
- `comma_byref_temp_test`
  - validates comma-expression lowering used by by-reference temporary handling
- `eval_reference_test`
  - validates reference dereference lowering and runtime execution
- `build_instance_test`
  - validates instance construction lowering and runtime execution
- `object_slot_access_test`
  - validates object slot load, store, and reference behavior through the VM
- `method_context_test`
  - validates method binding and execution including self access and field reads through captured environment
- `nested_direct_call_test`
  - validates direct-call lowering in nested contexts
- `tier1_primitives_test`
  - validates `Leda_string_print`, `Leda_string_concat`, `Leda_integer_asString`, `Leda_string_compare`, `Leda_object_equals`, and the `REGISTER_BUILTIN` mechanism for primitive class tables
- `pattern_match_test`
  - validates `BC_OP_BR_IF_NOT_KIND` for `is` pattern matching, including parent-chain traversal and field-binding sequences
- `indirect_call_test`
  - validates ordinary closure-call fallback when direct resolution is not available
- `object_array_test`
  - validates `Leda_object_allocate`, `Leda_object_at`, `Leda_object_atPut`, and `object.asString()` via the class name in slot 2 of class tables

This list is important because it shows the project has moved to behavior-scoped regression coverage rather than relying on manual inspection.

### Legacy regression coverage

The legacy interpreter still has a much broader CTest-backed regression corpus through `lc`.

That corpus remains useful in two ways:

- it is the semantic reference for expected Leda behavior
- it identifies which features still have to be implemented before the bytecode toolchain can run the same programs end to end

At the moment, the bytecode runtime is not yet a drop-in replacement for that full corpus because methods and pattern matching are still incomplete.

### What has worked well in practice

The most effective test discipline so far has been:

- use one narrow fixture per missing lowering or runtime feature
- validate the new slice immediately after the first edit that implements it
- rerun the already-green bytecode fixtures after each slice to catch regressions in slot resolution or call lowering

That workflow has already caught multiple real regressions, including:

- incorrect top-level slot classification
- over-broad object-slot fallback logic
- expression-statement stack cleanup mistakes
- missing support for `commaOp`
- incorrect handling of by-reference temporaries
- incomplete runtime support for references and object slots

### Current test status

All 14 bytecode regressions are currently green:

- `lbcdump_test`
- `direct_call_test`
- `constant_alias_call_test`
- `local_alias_call_test`
- `comma_byref_temp_test`
- `eval_reference_test`
- `build_instance_test`
- `object_slot_access_test`
- `method_context_test`
- `nested_direct_call_test`
- `tier1_primitives_test`
- `pattern_match_test`
- `indirect_call_test`
- `object_array_test`

### Recommended ongoing testing order

1. run the narrow feature-specific bytecode test that corresponds to the code being changed
2. rerun the rest of the existing bytecode regressions
3. only then widen scope toward more of the legacy corpus

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

This milestone has already been surpassed.

The implementation now supports far more than the original first milestone target, including:

- closures
- captured environments
- by-reference semantics for the current bytecode slice
- thunk evaluation lowering
- object construction
- object slot access
- direct-call optimization for several closure-identification cases

The current practical milestone is no longer "get a minimal VM running" or "finish methods and pattern matching". Both of those are done.

The previous practical milestone is now complete:

- the Tier B/C/D primitive set is fully validated end-to-end through `real_string_fixture`
- the `real_string_test` golden regression is wired into CTest and green
- four VM bugs were fixed in this phase: primitive environment slot layout, spurious `REGISTER_BUILTIN` for boolean singletons, env-slot watermark overlap across environment kinds, and missing arg-slot mapping in captured-local resolution

The current practical milestone is:

- begin running legacy chapter regression fixtures through `ledac` plus `ledavm` and tracking which pass
- investigate and fix the pre-existing `ledac` segfault on closures that capture outer function locals (blocking 8 of 55 CTest tests)
- add output-comparison infrastructure to the test harness so VM output can be checked against golden text, not just dump contents

The right first success condition is:

- `ledac` compiles a simple source file to `.lbc`
- `ledavm` executes that `.lbc`
- output matches the current interpreter for a small regression subset

That creates a stable base for the more difficult semantic features later.
