# Python Compiler + VM Detailed Plan

## Objective

Build a greenfield Python-based compiler and VM for Leda that:

1. Uses vm_ideas.md decisions as architecture authority.
2. Produces deterministic bytecode images.
3. Executes programs with behavior matching the legacy interpreter.
4. Uses chapter programs in Test as primary acceptance gates.

## Scope

In scope:

1. Python frontend (lexer/parser/semantic passes/lowering/emission).
2. Python VM runtime (loader, dispatch, closures, classes, methods, pattern matching, primitives, debug stack traces).
3. Differential test harness comparing legacy interpreter outputs vs Python compiler+VM outputs.
4. Incremental chapter-based validation using Test/chapXY.led.

Out of scope for first pass:

1. Production performance parity with C runtime.
2. Native code generation.
3. Whole-program optimization pipeline.

## Hard Constraints

1. Existing src_bytecode implementation is reference-only for gap discovery, not implementation reuse.
2. Instruction encoding is fixed 64-bit words as specified in vm_ideas.md.
3. No extension instruction words in V1.
4. Debug metadata is mandatory in V1.

## Proposed Repository Layout (New Python Workspace)

Create a new top-level directory, for example py_leda_vm/:

1. py_leda_vm/compiler/
2. py_leda_vm/vm/
3. py_leda_vm/image/
4. py_leda_vm/runtime/
5. py_leda_vm/tools/
6. py_leda_vm/tests/
7. py_leda_vm/tests/golden/
8. py_leda_vm/tests/fixtures/

Suggested module breakdown:

1. compiler/lexer.py
2. compiler/parser.py
3. compiler/ast_nodes.py
4. compiler/symbols.py
5. compiler/binder.py
6. compiler/hir_nodes.py
7. compiler/ast_to_hir.py
8. compiler/closure_analysis.py
9. compiler/slot_planner.py
10. compiler/cfg_lowering.py
11. compiler/bytecode_ir.py
12. compiler/encode64.py
13. image/lbc_format.py
14. image/lbc_writer.py
15. image/lbc_reader.py
16. image/verifier.py
17. vm/dispatch.py
18. vm/frames.py
19. vm/closures.py
20. vm/objects.py
21. vm/patterns.py
22. vm/primitives.py
23. vm/debug_trace.py
24. runtime/memory.py
25. runtime/gc.py
26. tools/compile.py
27. tools/run_vm.py
28. tools/disasm.py
29. tools/generate_goldens.py
30. tools/diff_run.py

## Execution Strategy

Use a two-track implementation flow with tight feedback:

1. Feature implementation track (compiler + VM).
2. Differential validation track (legacy vs Python VM outputs).

Every feature change must include:

1. Unit tests for new pass/opcode behavior.
2. At least one chapter or focused fixture execution comparison.

## Test Corpus Baseline

Chapter test files detected in Test:

1. chap2a to chap2f
2. chap3
3. chap4a to chap4c
4. chap5
5. chap6a to chap6f
6. chap7a to chap7d
7. chap8a, chap8b, chap8c
8. chap9
9. chap11
10. chap14
11. chap15a to chap15d
12. chap16
13. chap17
14. chap19a to chap19c
15. chap20a to chap20d
16. chap21

Special invocation handling required by current regression script:

1. chap8c uses stdin from Test/concordanceInput.
2. chap17 uses stdin from Test/chap17input.
3. chap20d is run with legacy flag -m 500000.

## Golden Output Method

### Golden generation sources

1. Primary oracle: legacy interpreter executable (lc) from Src build.
2. Secondary oracle: existing per-test golden files where present.

### Golden generation rules

1. Capture stdout and stderr separately.
2. Capture exit code.
3. Normalize line endings to \n.
4. Trim only trailing whitespace at end-of-output to avoid platform noise.
5. Persist command metadata (stdin fixture, flags).

### Differential comparison contract

For each test program:

1. Run legacy interpreter and capture result tuple:
- exit_code
- stdout
- stderr
2. Run Python compiler + Python VM and capture same tuple.
3. Compare tuple fields and fail on mismatch.
4. Record mismatch artifacts for triage:
- emitted bytecode disassembly
- stack trace
- failing instruction pointer

## Milestone Plan

The timeline assumes one experienced engineer full time.

### Phase 0: Bootstrap And Harness (Week 1-2)

Deliverables:

1. Python project scaffolding and packaging.
2. CLI tools compile.py, run_vm.py, disasm.py (stubs acceptable).
3. Golden harness tools generate_goldens.py and diff_run.py.
4. Baseline golden generation for all chapter tests.

Exit criteria:

1. All chapter programs can be executed by legacy interpreter through harness.
2. Golden artifacts generated for each chapter case, including stdin-driven cases.

### Phase 1: Parser + HIR Foundations (Week 3-5)

Deliverables:

1. Lexer and parser for declarations/statements/expressions.
2. Parse AST to HIR conversion.
3. Symbol resolution and scope graph.
4. Basic diagnostics with source spans.

Exit criteria:

1. All chapter programs parse successfully to HIR.
2. Symbol tables emitted for all units without crashes.

### Phase 2: Frame Model + Slot Planning (Week 6-7)

Deliverables:

1. Function metadata generation (arity, locals, temps, return slot declaration).
2. Deterministic slot planner.
3. Return slot policy enforced (return_count=1 in V1).

Exit criteria:

1. Metadata verifier passes on chapter corpus.
2. Slot assignment stability test passes (same input -> same slot map).

### Phase 3: Bytecode Emission Core (Week 8-10)

Deliverables:

1. 64-bit instruction encoder.
2. Control flow lowering with word-based branch deltas.
3. Operand composition support (including B:C:D imm36 forms).
4. LBC image writer and verifier.

Exit criteria:

1. Bytecode generated for chapter files without verifier errors.
2. Disassembler can round-trip and decode all emitted instructions.

### Phase 4: VM Core Execution (Week 11-13)

Deliverables:

1. Loader and dispatch loop.
2. Frame call/return semantics.
3. Basic arithmetic/logical operations.
4. Globals/locals/temps operations.

Validation wave:

1. chap2a to chap2f
2. chap3
3. chap4a to chap4c

Exit criteria:

1. Differential pass for validation wave above.

### Phase 5: Closures, Methods, Objects (Week 14-17)

Deliverables:

1. Closure creation and capture environment model.
2. Shared mutable capture cells.
3. Class metadata loading and instance field access.
4. Method closure binding with receiver context.
5. Primitive dispatch table path.

Validation wave:

1. chap5
2. chap6a to chap6f
3. chap7a to chap7d
4. chap8a, chap8b, chap8c
5. chap9

Exit criteria:

1. Differential pass for full wave.
2. chap8c stdin behavior matches golden output.

### Phase 6: Pattern Matching + Advanced Control (Week 18-20)

Deliverables:

1. Pattern matching lowering and runtime opcodes.
2. Match bind semantics and branch behavior.
3. Debug metadata emission and runtime stack traces.

Validation wave:

1. chap11
2. chap14
3. chap15a to chap15d
4. chap16
5. chap17

Exit criteria:

1. Differential pass for wave.
2. chap17 stdin behavior matches golden output.
3. Runtime stack traces include source file and line.

### Phase 7: Late Chapters + Runtime Hardening (Week 21-24)

Deliverables:

1. Tail-call path and recursion stability checks.
2. Memory subsystem hardening.
3. GC prototype integration (copying + object table direction).
4. Performance profiling instrumentation.

Validation wave:

1. chap19a to chap19c
2. chap20a to chap20d
3. chap21

Exit criteria:

1. Differential pass for final wave.
2. chap20d behavior accepted with documented compatibility approach for -m legacy flag.

## Effort Estimates

### One engineer

1. MVP with meaningful chapter coverage: 12-16 weeks.
2. Full chapter differential parity target: 20-24 weeks.
3. Hardened V1 with debug and GC prototype: 24-30 weeks.

### Two engineers

1. MVP: 8-12 weeks.
2. Full chapter parity: 14-18 weeks.
3. Hardened V1: 18-24 weeks.

Parallel split recommendation for two engineers:

1. Engineer A: parser/binder/lowering/image.
2. Engineer B: VM runtime/objects/primitives/debug.

## Weekly Cadence And Governance

Every week:

1. Land at least one end-to-end runnable slice.
2. Update differential status dashboard by chapter test.
3. Record new semantic mismatches and classify:
- frontend bug
- lowering bug
- VM execution bug
- primitive semantics mismatch

Every two weeks:

1. Freeze opcode changes unless justified by failing tests.
2. Run full chapter suite and publish pass/fail trend.

## Deliverables Checklist

### Compiler deliverables

1. Parse AST and HIR definitions.
2. Symbol binder and closure analysis.
3. Slot planner and metadata emitter.
4. 64-bit bytecode emitter with operand composition.
5. LBC writer and verifier.

### VM deliverables

1. Loader and dispatch engine.
2. Frame/call/return implementation.
3. Closure capture implementation.
4. Class/method/object implementation.
5. Pattern matching and primitive interface.
6. Debug stack trace engine.

### Tooling deliverables

1. Golden generator.
2. Differential runner.
3. Disassembler.
4. Failure triage artifacts (trace + disasm + metadata dumps).

## Risk Register

1. Grammar parity risk.
- Mitigation: parser snapshot tests using all chapter files.

2. Closure aliasing correctness risk.
- Mitigation: dedicated capture mutation tests and per-opcode invariants.

3. Pattern matching semantic drift risk.
- Mitigation: targeted fixtures plus chapter wave gates.

4. Primitive semantic mismatch risk.
- Mitigation: isolate primitive layer and add contract tests per primitive.

5. Schedule slip risk due to broad language surface.
- Mitigation: strict wave gating and defer non-essential optimization.

## Acceptance Criteria

Project is considered successful for this phase when:

1. All selected chapter tests execute through Python compiler + Python VM.
2. Differential comparison passes for stdout/stderr/exit behavior against legacy interpreter.
3. Required stdin-driven chapter cases match expected behavior.
4. Debug stack traces are present and usable.
5. Bytecode verifier catches malformed images before execution.

## Immediate Next Actions

1. Create py_leda_vm scaffold and Python package config.
2. Implement generate_goldens.py for all chapter programs.
3. Implement diff_run.py harness with tuple-based comparison.
4. Start parser implementation with chapter corpus as parse-only CI gate.

## Implementation Playbook (Step By Step)

This section is intentionally procedural. Follow it in order.

### Step 1: Create a runnable Python skeleton

Tasks:

1. Create package folders listed in the repository layout section.
2. Add pyproject.toml with one command-line entry for each tool:
- compile
- run-vm
- disasm
- generate-goldens
- diff-run
3. Add one smoke test that imports all top-level modules.

Definition of done:

1. A clean checkout can run the smoke test.
2. Each CLI prints a help message and exits code 0.

### Step 2: Build the golden harness first

Tasks:

1. Implement tools/generate_goldens.py.
2. Implement tools/diff_run.py.
3. Add manifest file tests/golden/manifest.json.

Manifest entry fields:

1. test_name
2. source_path
3. stdin_path (optional)
4. legacy_args (optional)
5. expected_stdout_path
6. expected_stderr_path
7. expected_exit_code

Definition of done:

1. Every chapter case has a manifest entry.
2. Goldens are generated by running legacy interpreter only.
3. Diff tool can compare stub Python output to goldens and report mismatches.

### Step 3: Implement parse-only pipeline

Tasks:

1. Implement lexer and parser that can parse all chapter files.
2. Build Parse AST nodes with source spans.
3. Add parser snapshots to tests.

Parse AST node minimum fields:

1. kind
2. children (typed fields)
3. span_start_line
4. span_start_col
5. span_end_line
6. span_end_col

Definition of done:

1. Parse-only CI gate passes on all chapter files.
2. Parser errors include file, line, column, and nearest token context.

### Step 4: Implement semantic binder and HIR

Tasks:

1. Create symbol table model for module/class/function/block scopes.
2. Convert Parse AST to HIR with resolved symbol ids.
3. Encode explicit call forms:
- direct call
- closure call
- method call

HIR node minimum fields:

1. kind
2. result_type_id (if known)
3. span
4. resolved symbol references

Definition of done:

1. HIR can be generated for all chapter files.
2. Binder catches undefined symbol and duplicate definition errors.

### Step 5: Implement closure analysis and slot planning

Tasks:

1. Compute free variable sets for nested functions.
2. Create capture descriptors per closure:
- source_scope_depth
- source_slot_kind
- source_slot_index
- capture_cell_index
3. Generate deterministic frame layout metadata.

Function metadata minimum fields:

1. function_id
2. name
3. arity
4. local_count
5. temp_count
6. return_count
7. return_slot_base
8. debug_context_id
9. capture_descriptor_list

Definition of done:

1. Identical input emits identical metadata ordering.
2. A dedicated capture mutation unit test passes.

### Step 6: Implement bytecode IR and 64-bit encoder

Tasks:

1. Define opcode enum and semantic operand shapes.
2. Implement 64-bit packing and unpacking helpers.
3. Implement branch label resolution in instruction words.
4. Implement operand composition forms including imm36.

Encoding formulas:

1. word = (opcode << 56) | (A << 44) | (B << 32) | (C << 20) | (D << 8)
2. imm36 = (B << 24) | (C << 12) | D

Definition of done:

1. Encoder-decoder roundtrip tests pass for all opcode forms.
2. Verifier rejects reserved bits when strict mode is enabled.

### Step 7: Implement image writer, reader, and verifier

Tasks:

1. Implement sectioned LBC writer.
2. Implement reader with bounds checks.
3. Implement verifier with explicit error codes.

Verifier checks required:

1. Valid section bounds.
2. Valid function metadata references.
3. Valid opcode and operand ranges per opcode shape.
4. Valid branch targets by word index.
5. Valid class and vtable references.

Definition of done:

1. Corrupted image fixtures fail with deterministic error codes.
2. Valid chapter images load and verify.

### Step 8: Implement VM core dispatch and frame engine

Tasks:

1. Implement dispatch loop with instruction-word IP.
2. Implement frame push/pop.
3. Implement call/return path based on metadata return slot.
4. Implement direct branch opcodes.

Frame structure minimum fields:

1. ip
2. fp
3. cp
4. ce
5. dp
6. caller_frame_index

Definition of done:

1. Core arithmetic and control-flow fixture suite passes.
2. Differential wave for early chapters passes.

### Step 9: Implement objects, methods, closures, captures

Tasks:

1. Implement closure value and capture environment objects.
2. Implement shared mutable capture cell semantics.
3. Implement class metadata and vtable dispatch.
4. Implement method closure binding with receiver context.

Definition of done:

1. Closure mutation fixtures pass.
2. Method dispatch fixtures pass.
3. Differential wave for chapters 5-9 passes.

### Step 10: Implement pattern matching and debug stack traces

Tasks:

1. Lower pattern forms to explicit match opcodes.
2. Implement runtime match semantics and binding.
3. Implement debug metadata lookup via dp field.
4. Implement stack trace formatting.

Definition of done:

1. Differential wave for chapters 11-17 passes.
2. Stack traces include file and line for runtime errors.

### Step 11: Implement GC prototype and hardening

Tasks:

1. Implement moving collector prototype.
2. Implement object table indirection layer.
3. Implement root scanning for frames, globals, closures.

Definition of done:

1. Stress allocation tests pass.
2. Differential wave for chapters 19-21 passes.

## File-by-File Responsibilities

compiler/lexer.py:

1. Token definitions.
2. Source position tracking.
3. Lexer error reporting.

compiler/parser.py:

1. Declaration parsing.
2. Statement parsing.
3. Pratt expression parsing.

compiler/symbols.py:

1. Symbol kinds.
2. Scope tree model.
3. Id allocators.

compiler/binder.py:

1. Name resolution.
2. Scope entry/exit rules.
3. Semantic diagnostics.

compiler/closure_analysis.py:

1. Free-variable discovery.
2. Capture descriptor emission.

compiler/slot_planner.py:

1. Frame layout assignment.
2. Return slot declaration.

compiler/cfg_lowering.py:

1. Basic block builder.
2. Branch target labeling.

compiler/encode64.py:

1. Instruction packing.
2. Instruction unpacking.
3. Operand composition helpers.

image/lbc_writer.py:

1. Section serialization.
2. Offsets and directory writing.

image/lbc_reader.py:

1. Section loading.
2. Cross-reference reconstruction.

image/verifier.py:

1. Structural validation.
2. Semantic opcode validation.

vm/dispatch.py:

1. Main loop and opcode dispatch.
2. Error propagation with debug context.

vm/frames.py:

1. Frame lifecycle.
2. Call and return semantics.

vm/closures.py:

1. Closure creation.
2. Capture env access.
3. Capture aliasing behavior.

vm/objects.py:

1. Class table integration.
2. Instance field operations.
3. Method lookup and invocation.

vm/patterns.py:

1. Match operations.
2. Bind and fail branching.

vm/primitives.py:

1. Primitive registry.
2. Primitive argument and return conventions.

vm/debug_trace.py:

1. DP lookup.
2. Stack trace rendering.

runtime/gc.py:

1. Heap spaces.
2. Collection trigger and copy.
3. Object table remap.

tools/generate_goldens.py:

1. Legacy runner wrapper.
2. Golden artifact writer.

tools/diff_run.py:

1. End-to-end differential runner.
2. Failure artifact output.

## Daily Workflow For A Junior Developer

1. Pull latest main branch.
2. Run parse-only gate on chapter corpus.
3. Implement one small ticket.
4. Add unit tests for changed module.
5. Run differential subset for related chapter wave.
6. If mismatch, save artifacts and open mismatch note.
7. Commit only when parse gate, unit tests, and subset diff are green.

## Ticket Template (Use For Every Change)

Ticket fields:

1. Title.
2. Phase.
3. Files to modify.
4. Inputs and outputs.
5. Error conditions.
6. Unit tests to add.
7. Differential tests to run.
8. Definition of done.

Example ticket:

Title: Implement word-based JUMP_IF_FALSE lowering

1. Phase: 3.
2. Files: compiler/cfg_lowering.py, compiler/bytecode_ir.py, compiler/encode64.py.
3. Input: HIR conditional node and label map.
4. Output: JUMP_IF_FALSE with word-delta target.
5. Error conditions: unresolved label, delta overflow.
6. Unit tests: single-branch, nested-branch, backward-jump.
7. Differential tests: chap2a, chap3.
8. Done: all tests green and disasm shows expected deltas.

## Detailed Differential Harness Design

Execution record structure for each run:

1. test_name
2. source_path
3. stdin_path
4. args
5. exit_code
6. stdout
7. stderr
8. runtime_ms

Comparison rules:

1. exit_code must match exactly.
2. stdout must match after newline normalization.
3. stderr must match after newline normalization.

Failure artifact bundle per test:

1. actual.stdout
2. actual.stderr
3. expected.stdout
4. expected.stderr
5. compile.log
6. vm.log
7. disasm.txt
8. metadata.json

## CI Gates (Minimum)

Gate A: Parser gate

1. Parse all chapter files.
2. No parser crashes.

Gate B: Compiler gate

1. Emit and verify LBC for all chapter files.

Gate C: VM smoke gate

1. Run early chapter wave end-to-end.

Gate D: Differential wave gate

1. Current wave must match legacy outputs.

Gate E: Full regression gate

1. All completed waves pass before milestone closure.

## Troubleshooting Guide

If parser fails:

1. Re-run one file with token dump enabled.
2. Inspect nearest unexpected token and span.
3. Add minimal failing fixture.

If verifier fails:

1. Dump section directory and check offsets.
2. Check branch target against code word count.
3. Validate operand shape for failing opcode.

If VM output mismatches legacy:

1. Compare disassembly around failing IP.
2. Compare stack frame snapshots.
3. Compare closure env snapshots if closure-related.
4. Reduce to smallest reproducer and add fixture.

If closure mutation is wrong:

1. Check capture descriptor mapping.
2. Check whether parent slot was replaced by indirection cell.
3. Check store path writes into cell, not copied value.

## Milestone Exit Checklists

Phase 0 exit checklist:

1. Goldens generated for all chapter tests.
2. Diff harness runs and reports mismatches correctly.

Phase 1 exit checklist:

1. All chapter files parse.
2. HIR generation works for all files.

Phase 2 exit checklist:

1. Deterministic slot and metadata output confirmed.
2. Capture descriptors generated for closure cases.

Phase 3 exit checklist:

1. Encoder roundtrip tests pass.
2. Verifier passes on emitted images.

Phase 4 exit checklist:

1. Early wave differential pass complete.

Phase 5 exit checklist:

1. Closure and method wave differential pass complete.

Phase 6 exit checklist:

1. Pattern and debug wave differential pass complete.

Phase 7 exit checklist:

1. Late chapter wave differential pass complete.
2. GC stress suite complete.

## Suggested First 10 Tickets

1. Create pyproject and CLI entry points.
2. Implement chapter test manifest generator.
3. Implement legacy golden recorder.
4. Implement differential comparator.
5. Implement token model and lexer spans.
6. Implement Pratt expression parser core.
7. Implement statement parser core.
8. Implement symbol table and binder skeleton.
9. Implement Parse AST to HIR transformer skeleton.
10. Implement parse-only CI command for chapter corpus.
