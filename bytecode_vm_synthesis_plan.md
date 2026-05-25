# Leda Bytecode VM Synthesis Specification

## Purpose

This document synthesizes design input from new_bytecode3.md and vm_ideas.md into one implementation-grade specification.

Conflict resolution rule:

- When the two sources conflict, vm_ideas.md is authoritative.

Primary outcome:

- A greenfield compiler + VM design that can be implemented incrementally, tested, and profiled.

Hard implementation constraint:

- Existing implementation code under src_bytecode is reference-only for missing case discovery and test coverage review.
- It is not the implementation baseline for this design.

## Non-Goals For V1

- No extension words in instruction encoding.
- No dynamic runtime mutation of class shapes.
- No requirement to preserve the execution internals of legacy AST-walker behavior.
- No mandatory NaN-boxing in V1 (tagged union remains acceptable for first bring-up).

## Decision Baseline (Authoritative)

These decisions are locked for V1.

1. Fixed-width instruction encoding: one 64-bit instruction word.
2. Field layout: 8-bit opcode, 4 x 12-bit operands (A/B/C/D), 8 reserved bits.
3. Reserved bits are emitted as zero in V1, ignored by runtime semantics.
4. No instruction extension words in V1.
5. Operand interpretation is opcode-specific; operands may be combined into larger immediates.
6. Closures use shared mutable capture cells.
7. Access to captures uses explicit capture opcodes, not transparent implicit dereference.
8. Methods are closures that carry receiver context in closure environment.
9. Primitive dispatch is table-indexed by primitive id.
10. Bytecode image is standalone and self-contained.
11. V1 language coverage includes closures, classes/methods, and pattern matching.
12. GC direction is compacting/copying, with object table strategy to reduce pointer-fixup cost.
13. Return locations are declared in function metadata; V1 uses single-return only.
14. Branch deltas are measured in instruction words.
15. Debug metadata is required in V1 (line + symbol mapping).
16. Frame debug field uses compact debug-context index to support useful stack traces.

## Synthesis Of Source Inputs

What is preserved from new_bytecode3.md:

- Two-pass compiler concept (layout pass then code emission pass).
- Static class layout and vtable indexing at compile time.
- Strong preference for deterministic index-driven execution.
- Tail-call optimization as a first-class opcode behavior.
- Primitive operation channel as explicit VM opcode.

What is replaced by vm_ideas.md decisions:

- Instruction encoding is not 1-byte opcode stream with ad-hoc operands.
- VM architecture is not "Wren-derived" as a baseline requirement.
- No extension words in V1 instruction format.
- Closure and frame model follows frame-slot + capture-cell design, not host VM object-model inheritance.

## Top-Level Architecture

Pipeline:

1. Frontend parsing and semantic analysis.
2. Symbol/layout pass for globals, classes, methods, functions, captures.
3. Bytecode emission pass producing fixed 64-bit instructions.
4. Image assembly into standalone .lbc format.
5. VM loader + verifier + executor.
6. Runtime services: allocation, GC, primitive dispatch, debug stack tracing.

Execution model:

- Frame-slot VM with explicit frame pointer semantics.
- Function metadata drives slot partitioning and return slot selection.
- Closures and objects live in managed heap.
- Call frames remain stack-like for non-escaping execution speed.

## Frontend And Compiler Specification

### Parsing

Preferred parser strategy:

- Recursive descent for declarations and statements.
- Pratt parser for expressions and operator precedence.

Rationale:

- Works naturally with Leda’s mixed paradigm syntax.
- Keeps precedence logic local and editable.
- Simplifies extension of logical and functional operators.

### Compiler Pass 1: Layout And Binding

Pass 1 responsibilities:

1. Build global symbol table for classes/functions/primitives.
2. Resolve method signatures and assign stable method indexes.
3. Build class hierarchy metadata and parent links.
4. Build concrete instance slot layouts per class.
5. Build vtable arrays per class:
- Start from parent vtable.
- Overwrite entries for overrides.
6. Resolve local variables and assign frame slot indexes.
7. Identify captured variables for each closure literal.
8. Build closure capture plans:
- Capture source scope depth.
- Capture source slot kind and index.
- Capture mutability/reference behavior.
9. Produce function metadata skeletons:
- arity
- local_count
- temp_count
- return_count
- return slot declaration
- debug context index

### Compiler Pass 2: Bytecode Emission

Pass 2 responsibilities:

1. Emit 64-bit instructions per function body.
2. Resolve branch targets to instruction-word deltas.
3. Encode direct-call targets and closure-call forms.
4. Emit capture load/store/make closure instructions based on pass-1 capture plan.
5. Emit method binding and invoke instructions using vtable indexes.
6. Emit pattern-match branching and destructuring helpers.
7. Emit primitive calls by primitive id.
8. Emit debug line mapping table entries keyed by instruction index.

Codegen principles:

- Prefer direct indexed operations over symbolic operations.
- Preserve deterministic instruction ordering for reproducible builds.
- Emit canonical encoding for all composed immediates.

## Instruction Encoding Specification

### Bit Layout

Instruction word (bits high to low):

- 63..56: opcode (8)
- 55..44: A (12)
- 43..32: B (12)
- 31..20: C (12)
- 19..8: D (12)
- 7..0: reserved (8)

V1 reserved bit rule:

- Must be zero in emitted code.
- Runtime does not assign semantic meaning.
- Optional diagnostics may warn on non-zero values.

### Operand Composition

Operands may be interpreted per opcode.

Supported composition patterns:

1. Four-index form:
- A/B/C/D are independent indexes.

2. Three-index plus small immediate:
- A/B/C indexes
- D is 12-bit immediate.

3. Packed immediate form:
- A is index
- B:C:D form imm36
- imm36 = (B << 24) | (C << 12) | D

4. Dual-index with offset:
- A destination index
- B base index
- C index index
- D offset immediate.

Word-based branch deltas:

- Jump offsets are measured in instruction words, not bytes.

## Preliminary Opcode Families

Opcode numbers are not fixed in this section; this is semantic catalog for implementation planning.

### Core Control

- HALT
- NOP
- JUMP (word delta)
- JUMP_IF_FALSE (word delta)
- JUMP_IF_TRUE (word delta)
- RETURN
- TAIL_CALL

### Frame/Call

- CALL_DIRECT
- CALL_CLOSURE
- MAKE_CLOSURE
- MAKE_METHOD_CLOSURE

### Locals/Args/Temps

- LOAD_LOCAL
- STORE_LOCAL
- LOAD_ARG
- STORE_ARG
- LOAD_TEMP
- STORE_TEMP

### Capture Access

- LOAD_CAPTURE
- STORE_CAPTURE
- MAKE_CAPTURE_REF

### References

- MAKE_REF_LOCAL
- MAKE_REF_ARG
- LOAD_REF
- STORE_REF

### Constants

- LOAD_CONST

### Arithmetic And Logical

- ADD, SUB, MUL, DIV, MOD
- NEG, NOT
- AND, OR, XOR
- EQ, NEQ, LT, LE, GT, GE

### Objects/Classes/Methods

- NEW_INSTANCE
- LOAD_FIELD
- STORE_FIELD
- LOAD_INDEXED
- STORE_INDEXED
- METHOD_LOOKUP
- INVOKE_VTABLE

### Pattern Matching Support

- MATCH_KIND
- MATCH_BIND_SLOT
- MATCH_FAIL_JUMP

### Primitive Bridge

- PRIMITIVE_CALL

Note:

- Whether some families collapse into fewer opcodes with mode bits is an optimization decision after baseline correctness.

## Function Metadata Specification

Each function/template metadata record must declare at minimum:

1. function_id
2. name_id (debug/symbol table link)
3. code_start_word_index
4. code_word_count
5. arity
6. local_count
7. temp_count
8. return_count
9. return_slot_base or explicit return slot list
10. constant_pool_id or constant_pool_offset
11. debug_context_id
12. flags (tail-call eligible, closure factory, method body, etc.)

V1 return constraint:

- return_count == 1 for all emitted functions.

Why metadata-based return slots are kept:

- No call-site operand burden.
- Future multi-return can be enabled by lifting return_count > 1.
- Return layout stays callee-owned.

## Frame And Register/Header Model

Per-frame runtime header fields:

1. IP: instruction pointer (word index)
2. FP: frame pointer/base in frame storage
3. CP: current closure/template handle
4. CE: closure environment pointer/handle
5. DP: debug context id
6. Optional derived context:
- CL (class table context) candidate
- CTP (constant table pointer) candidate

Current recommendation:

- Keep IP/FP/CP/CE/DP explicit.
- Derive CL/CTP from CP metadata unless profiling proves benefit of explicit fields.

## Closure And Capture Runtime Model

### Closure Value

Closure runtime value contains:

1. template/function id
2. capture environment handle

### Capture Environment

Capture environment is a heap vector of capture cells.

Capture cell semantics:

- Shared mutable aliasing between parent and closure.
- If parent local is captured, parent slot transitions to indirection cell.
- Reads/writes through parent and closure observe same logical value.

### Creation Path

On MAKE_CLOSURE:

1. Lookup template capture plan from function metadata.
2. Allocate capture vector sized to capture_count.
3. For each capture descriptor:
- Locate source slot in current lexical chain.
- Materialize or reuse capture cell.
- Store cell handle in closure capture vector.
4. Construct closure value with template id + env handle.

### Execution Path

On CALL_CLOSURE:

1. Create callee frame from function metadata.
2. Set CP from closure template id.
3. Set CE from closure capture env handle.
4. Place arguments per metadata.
5. Jump to code_start_word_index.

## Class And Method Model

### Static Class Table

Class metadata record includes:

1. class_id
2. name_id
3. parent_class_id
4. instance_slot_count
5. vtable_offset + vtable_size
6. optional class-constant links

### vTable

- Method indexes are stable integers assigned by compiler.
- Subclass vtable is parent copy plus override replacements.

### Method Invocation

Method call path:

1. Determine receiver class id.
2. Resolve method template through vtable index.
3. Build method closure carrying receiver context.
4. Call via CALL_CLOSURE path.

## Constant Pools And Globals

Constant organization:

- Per-function constant pools are default.
- CP references current function metadata entry.

Index pressure handling:

- 12-bit direct indexes are preferred.
- For larger constant or target spaces, use opcode-level operand composition (imm36 forms).

Global data:

- Globals table stored in image with stable indexes.
- Global slots referenced directly by index-based opcodes.

## Image Format Specification (LBC V1)

Single standalone binary with little-endian encoding.

### Section Order

1. Header
2. Section directory
3. Constant data section
4. Function metadata section
5. Class metadata section
6. vTable section
7. Code section (64-bit words)
8. Global init section (optional executable function id)
9. Debug metadata section
10. Symbol table section

### Header Fields

1. Magic (4 bytes)
2. Format version (2 bytes)
3. Flags (2 bytes)
4. Entry function id (4 bytes)
5. Section directory offset (8 bytes)
6. Section count (4 bytes)
7. Build id/hash (optional fixed bytes)

### Section Directory Entry

1. Section kind id (4 bytes)
2. Offset bytes from file start (8 bytes)
3. Length bytes (8 bytes)
4. Element count (4 bytes)
5. Element size or encoding id (4 bytes)

### Code Section

- Dense array of uint64 instruction words.
- All branch targets are instruction-word indexes.

### Debug Metadata Section

Minimum required:

1. Debug context table
2. File table
3. Function name table link
4. Line table mapping (context_id, ip_word_range, file_id, line, column)
5. Optional local symbol scope table

## Loader And Verifier

### Loader Responsibilities

1. Validate header and version.
2. Load section directory.
3. Bounds-check all sections.
4. Materialize metadata tables in runtime structures.
5. Validate entry function id.

### Verifier Responsibilities

1. Validate opcode values.
2. Validate operand index ranges for known metadata limits.
3. Validate branch target word indexes.
4. Validate function metadata consistency:
- return_count and return slots
- local/temp bounds
- code range in code section
5. Validate class/vtable references.
6. Validate debug metadata cross-references.

## Runtime Memory And GC Plan

### Allocation Domains

1. Frame stack-like storage for active non-escaping call frames.
2. Heap for closures, capture cells, objects, arrays, strings, references.
3. Metadata area for immutable loaded image tables.

### GC Strategy

V1 direction:

- Copying/compacting GC (two-space favored).
- Object table used to reduce global pointer-fixup burden.

Root set includes:

1. Active frames and frame slots.
2. Operand evaluation stack if separate.
3. Global slots.
4. Closure environments and method contexts reachable from roots.
5. Runtime handles in primitive boundary layer.

## Stack Trace And Debugging Model

Frame stack trace record is reconstructed using:

1. Frame linkage (caller relation)
2. IP (instruction word index)
3. DP (debug context id)

Trace formatting lookup flow:

1. DP -> function/debug context
2. IP -> source line/column mapping
3. function context -> symbol name

Required behavior:

- Stack traces must include source file and line for each frame when debug metadata is present.

## Tail Call Optimization

Tail-call opcode semantics:

1. Validate same call contract requirements (arity and metadata constraints).
2. Reuse current frame where legal.
3. Overwrite argument/local region as needed.
4. Replace CP/CE/IP with target callee context.
5. Avoid frame-depth growth.

Compiler responsibility:

- Emit TAIL_CALL only in proven tail position.

## Primitive Interface

Primitive dispatch model:

- PRIMITIVE_CALL id, argc form (exact operand shape opcode-specific).
- Runtime dispatches primitive id to registered C function table.

Primitive contract:

1. Input operands read from known frame/stack slots.
2. Primitive may allocate managed heap objects.
3. Primitive returns value into declared return slot or designated destination slot.
4. Primitive errors map to VM exception/error path with debug context.

## Pattern Matching Support (V1 Required)

Compiler lowering requirements:

1. Lower high-level pattern forms into explicit match/test/branch opcodes.
2. Bind matched fields into deterministic slot indexes.
3. Preserve backtracking/control semantics expected by std.led logic layer.

Runtime requirements:

1. Type/kind checks against class metadata.
2. Field extraction by precomputed offsets.
3. Deterministic branch behavior with word-based deltas.

## Implementation Work Plan

### Phase 0: Spec Freeze

1. Freeze opcode semantic table.
2. Freeze metadata binary structs.
3. Freeze image section IDs and ordering.

Deliverables:

- ISA appendix
- binary format appendix
- verifier rule set

### Phase 1: Frontend Pass 1

1. Symbol table and class layout builder.
2. vtable allocator and inheritance override resolver.
3. closure capture analysis.
4. function metadata skeleton generation.

Deliverables:

- validated metadata graph
- deterministic ids for classes/functions/methods/constants

### Phase 2: Frontend Pass 2 + Assembler

1. instruction emitter for fixed 64-bit words.
2. branch target resolver.
3. operand composition encoder (including imm36).
4. debug mapping emission.

Deliverables:

- code section generation
- debug table generation

### Phase 3: Image Writer + Loader

1. .lbc writer with section directory.
2. loader and integrity checks.
3. standalone verifier executable/tooling.

Deliverables:

- round-trip test fixtures
- corrupted image rejection tests

### Phase 4: VM Core

1. frame engine and dispatch loop.
2. call/return and tail-call paths.
3. closure creation and capture access opcodes.
4. class/method invocation path.
5. primitive dispatch path.

Deliverables:

- execution of core language subset

### Phase 5: GC + Runtime Objects

1. object model and allocation APIs.
2. two-space collector + object table.
3. GC root scanner integration.

Deliverables:

- GC stress tests
- closure/object lifetime correctness

### Phase 6: Pattern Matching + Std Library Bring-up

1. full lowering and runtime match semantics.
2. std.led bootstrap validation.

Deliverables:

- targeted pattern and logic regression suite

### Phase 7: Debug + Tooling

1. stack trace formatter.
2. bytecode disassembler.
3. source mapping diagnostics.

Deliverables:

- line-accurate runtime errors
- deterministic disassembly output

## Test Strategy

### Compiler Tests

1. Symbol and layout golden tests.
2. vtable inheritance/override tests.
3. closure capture plan tests.
4. opcode emission golden tests.

### Image Tests

1. Serialize/deserialize fidelity.
2. verifier negative tests.
3. section bounds/offset corruption tests.

### VM Semantics Tests

1. arithmetic/logical correctness.
2. direct and closure calls.
3. captured mutation aliasing.
4. method dispatch and inheritance.
5. tail-call depth invariance.
6. pattern matching behaviors.

### Runtime/GC Tests

1. allocation churn under closures.
2. object movement correctness with object table.
3. primitive bridge allocations.
4. leak detection and root integrity.

### Debug Tests

1. stack trace accuracy per frame.
2. line mapping correctness.
3. symbol name resolution.

## Risk Register And Mitigations

1. Operand space pressure with 12-bit fields.
- Mitigation: opcode-level operand composition (imm36 and mixed forms).

2. Closure capture aliasing bugs.
- Mitigation: explicit capture descriptors and dedicated capture opcodes.

3. GC complexity with moving objects.
- Mitigation: object table indirection and focused stress tests before optimization.

4. Pattern matching semantic drift.
- Mitigation: golden tests tied to std.led behavior and chapter tests.

5. Debug metadata overhead.
- Mitigation: compact indexed tables and optional symbol granularity levels.

## Acceptance Criteria For V1

1. Compiler emits valid standalone .lbc images for target subset.
2. VM executes closures, classes/methods, and pattern matching correctly.
3. Captured mutable variables preserve aliasing semantics.
4. Stack traces include source lines and symbol names via debug metadata.
5. Tail-call path prevents unbounded frame growth in verified tail-recursive cases.
6. GC safely handles closure environments, objects, and references under stress.
7. Performance baseline and profiling harness are in place for post-V1 optimization.

## Immediate Next Steps

1. Freeze opcode table with concrete numeric assignments.
2. Freeze binary struct field widths and endianness macros.
3. Implement compiler pass-1 metadata pipeline with unit tests.
4. Implement pass-2 emitter for a minimal executable subset.
5. Build verifier before full VM execution loop to catch encoding defects early.
