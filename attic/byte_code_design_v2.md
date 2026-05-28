# Leda bytecode system: front end and register-based VM

This is a single, coherent design for a Leda-specific bytecode system, covering both:

- the **front end** (compiler from Leda source to bytecode), and  
- the **back end** (register-based VM with NaN-boxing, stack frames, heap environments, TCO, and compacting GC).

It’s tailored to:

- Leda’s **functional core** (closures, higher-order functions, TCO)  
- Leda’s **logic layer** (implemented in the standard library)  
- **static classes** at runtime  
- a future **JIT** (hence register-based design)

---

## 1. Front end: from Leda source to bytecode

The front end replaces the AST-walking interpreter with a compiler that emits a **module image**:

- a **constant pool**
- **function prototypes** (bytecode + metadata)
- **class descriptors**
- an **entry point** (main function)

### 1.1 Parsing and AST construction

**Goal:** Build a complete AST for the program.

- **Lexing/parsing:**
  - Tokenize Leda source.
  - Parse into AST nodes for:
    - **Expressions:** literals, variables, function calls, lambdas, arithmetic, logic constructs.
    - **Statements:** assignments, conditionals, loops, returns.
    - **Declarations:** functions, classes, fields, methods, globals.

### 1.2 Semantic analysis

**Goal:** Resolve names, scopes, and captured variables.

- **Scope and name resolution:**
  - Build a **scope tree** (nested lexical scopes).
  - Resolve each identifier to:
    - local variable
    - captured variable (upvalue)
    - global
- **Capture analysis:**
  - For each function, determine which locals are **referenced by nested functions**.
  - Mark these locals as **captured**.
- **Environment layout:**
  - For each function, build an **environment layout**:
    - list of captured locals
    - assign each a **slot index** in the environment (0..env_size-1).

This information is used later to generate closure metadata and to drive runtime promotion.

### 1.3 Class and method layout (static classes)

**Goal:** Turn class declarations into static runtime descriptors.

- **Class collection:**
  - For each `class` declaration, build a **class descriptor**:
    - class name (symbol)
    - list of fields (name → field index)
    - list of methods (name → function prototype index)
    - optional parent class
- **Field indexing:**
  - Assign each field a **fixed index** used by `GETFIELD`/`SETFIELD`.
- **Method indexing:**
  - Either:
    - assign each method name a **method index**, or
    - keep a symbol index and use a method table keyed by symbol.
- **Emit to constant pool:**
  - Class descriptors become entries in the **constant pool**.

Classes are **static at runtime**: once compiled, their structure doesn’t change.

### 1.4 Constant pool construction

**Goal:** Centralize all literals and static structures.

The constant pool holds:

- **Literals:**
  - numbers (ints, floats)
  - strings
  - booleans
  - `nil`
- **Symbols:**
  - variable names
  - method names
  - tags for logic programming
- **Class descriptors**
- **Function prototypes** (metadata + bytecode)

The compiler:

- collects all constants,
- deduplicates them,
- assigns each a **constant pool index**.

Bytecode instructions refer to constants only by index, using a single `LOADK` opcode.

### 1.5 Register allocation (per function)

**Goal:** Assign registers for arguments, locals, and temporaries.

For each function:

- **Register layout:**
  - `R0..R(n_args-1)` → arguments
  - `R(n_args)..R(n_locals-1)` → local variables
  - `R(n_locals)..` → temporaries
- **Captured locals:**
  - Captured locals still have registers, but are marked as **capturable**.
  - Their indices are recorded in the function’s environment layout.

A simple linear-scan allocator is sufficient initially.

### 1.6 Bytecode generation

**Goal:** Emit register-based bytecode for each function.

For each function:

- **Control flow:**
  - Build basic blocks and branch targets (optional but helpful).
- **Instruction emission:**
  - Expressions → sequences of:
    - `LOADK`, `MOVE`, arithmetic ops, comparisons, `TAG`, etc.
  - Control flow → `JMP`, `JMPIF`, `JMPIFNOT`.
  - Function calls → `CALL` or `TAILCALL`.
  - Returns → `RET`.
- **Tail-call detection:**
  - If a call is in tail position, emit `TAILCALL` instead of `CALL` + `RET`.
- **Closure creation:**
  - For nested functions, emit `CLOSURE` with:
    - function prototype index
    - **capture list**: which registers correspond to captured locals

Example:

```text
CLOSURE R3, func_idx, [R1, R4]
```

This tells the VM that the closure in `R3` needs to capture the values currently in `R1` and `R4`.

### 1.7 Module image emission

**Goal:** Produce a loadable binary representation.

The module image contains:

- **Header:** version, flags, entry point index.
- **Constant pool:** serialized constants.
- **Function prototypes:**
  - arity
  - register count
  - environment layout (captured locals)
  - bytecode instructions
- **Class descriptors**

The VM loads this image and starts execution at the entry point.

---

## 2. VM: register-based, NaN-boxed, stack frames, heap environments

The VM executes the module image using:

- a **register-based bytecode interpreter**
- **NaN-boxed values**
- a **VM-managed call stack** (not the C stack)
- **heap-allocated environments** for captured variables
- **closures** that can outlive their defining frames
- **proper tail calls**
- a **compacting GC**

### 2.1 Value representation: NaN-boxing

All values are 64-bit words:

- **Non-NaN** → IEEE 754 double (floating-point numbers).
- **NaN with payload** → tagged values:
  - small integers
  - booleans
  - `nil`
  - pointers to heap objects:
    - strings
    - arrays
    - records/objects
    - closures
    - environments
    - class descriptors
    - logic variables, etc.

This gives:

- efficient floating-point support (important for Leda),
- compact tagged representation,
- easy pointer identification for GC.

### 2.2 Stack frames (VM stack, not C stack)

The VM maintains its own **call stack** of frames.

Each frame contains:

- pointer to **function prototype**
- pointer to **environment** (or `null` if none)
- **register array**: `R0..R(n_regs-1)` (NaN-boxed values)
- **return address**: bytecode index or code pointer
- pointer to **previous frame**

Frames are:

- allocated from a **VM-managed stack buffer**,
- grown/shrunk as calls and returns happen,
- **not GC-managed** (they are not moved by GC).

The VM’s current frame pointer and stack are part of the root set for GC (because they contain references to heap objects).

### 2.3 Heap environments (for captured variables)

An **environment** is a heap-allocated object that stores captured variables:

- **Fields:**
  - pointer to **parent environment** (for nested scopes)
  - array of **slots** (NaN-boxed values)

Environments are:

- allocated in the GC heap,
- referenced by closures,
- referenced by frames (once promotion happens),
- movable by the compacting GC.

### 2.4 Promotion of captured variables

This is the key mechanism that allows closures to outlive their defining frames.

**Initial state:**

- When a function begins execution:
  - all locals (including those that may be captured) live in the frame’s registers,
  - the frame’s `env` pointer is `null`.

**When a `CLOSURE` instruction executes:**

1. The VM reads the **capture list** from the instruction (e.g., `[R1, R4]`).
2. If the frame has no environment yet:
   - allocate a new **environment object** with enough slots for all captured locals.
   - for each captured local:
     - copy the value from the corresponding register into the environment slot.
     - replace the register’s value with a **reference to that env slot** (e.g., via a small “cell” object or a tagged env-slot reference).
   - set the frame’s `env` pointer to this environment.
3. Create a **closure object**:
   - store pointer to the function prototype,
   - store pointer to the environment.

**After promotion:**

- Captured variables’ **actual storage** lives in the environment.
- The frame’s registers for those variables now hold **references** to that storage.
- Any closure created later that captures the same variables will share the same environment.
- When the frame returns, closures can still access the environment, so they can outlive the frame.

This matches the behavior you want: **closures can outlive their creating environments**, because the environment is heap-allocated and GC-managed.

### 2.5 Closures

A **closure object** contains:

- pointer to **function prototype** (in the constant pool),
- pointer to **environment** (env object).

When a closure is called:

1. The VM allocates a new frame on the VM stack.
2. Sets:
   - frame’s function prototype pointer,
   - frame’s `env` pointer to the closure’s environment.
3. Copies arguments into the frame’s registers.
4. Starts executing the function’s bytecode.

### 2.6 Constant pool

The constant pool is an array of immutable values:

- numbers, strings, symbols,
- class descriptors,
- function prototypes.

The VM loads constants with a single opcode:

```text
LOADK A, const_index   ; R[A] = const_pool[const_index]
```

The constant pool is treated as a **GC root** (it may contain pointers to heap objects).

### 2.7 Register-based bytecode format

Instructions are fixed-width, 32-bit words:

```text
[ OPCODE (8 bits) | A (8 bits) | B (8 bits) | C (8 bits) ]
```

Where A, B, C are:

- register indices,
- small immediates,
- constant pool indices (for `LOADK`),
- or small offsets (for short jumps).

Extended forms can be used for large constants or long jumps (e.g., `LOADKX` + extra word).

### 2.8 Core instruction set (examples)

**Constants and moves**

- `LOADK  A, k`  
  `R[A] = const_pool[k]`
- `MOVE   A, B`  
  `R[A] = R[B]`

**Arithmetic and logic**

- `ADD    A, B, C`
- `SUB    A, B, C`
- `MUL    A, B, C`
- `DIV    A, B, C`
- `EQ     A, B, C`
- `LT     A, B, C`
- `NOT    A, B`

**Control flow**

- `JMP    offset`
- `JMPIF  A, offset`
- `JMPIFNOT A, offset`

**Functions and closures**

- `CLOSURE A, func_idx, capture_spec`  
  - Promote captured locals if needed, create closure in `R[A]`.
- `CALL   A, n_args, n_ret`  
  - Call function in `R[A]` with args in `R[A+1..A+n_args]`.
  - Results in `R[A..A+n_ret-1]`.
- `TAILCALL A, n_args`  
  - Same as `CALL`, but **reuse current frame** (TCO).
- `RET    A, n_ret`  
  - Return `n_ret` values from `R[A..]`.

**Upvalues (via environments)**

- `GETUPVAL A, idx`  
  - `R[A] = env[idx]`
- `SETUPVAL idx, A`  
  - `env[idx] = R[A]`

**Objects and classes**

- `NEWOBJ  A, class_idx`  
  - `R[A] = new instance of class const_pool[class_idx]`
- `GETFIELD A, B, field_idx`  
  - `R[A] = R[B].fields[field_idx]`
- `SETFIELD B, field_idx, A`  
  - `R[B].fields[field_idx] = R[A]`
- `CALLMETHOD A, recv_reg, method_idx, n_args, n_ret`  
  - Look up method in receiver’s class, then call.

**Data structures / ADTs**

- `NEWTUPLE A, n`
- `NEWARRAY A, n`
- `GETELEM A, B, C`
- `SETELEM B, C, A`
- `TAG    A, tag_id, B`  
  - Create tagged value/variant.
- `ISTAG  A, B, tag_id`  
  - `R[A] = (R[B] has tag_id)`

This set is enough to support Leda’s standard library, including the logic programming layer.

### 2.9 Tail-call optimization (TCO)

TCO is implemented via the `TAILCALL` instruction:

- Instead of pushing a new frame:
  - reuse the current frame,
  - overwrite its registers with the new arguments,
  - change its function prototype pointer,
  - reset the instruction pointer to the callee’s entry.

This ensures:

- no additional stack growth for tail recursion,
- efficient CPS-style logic programming (as in Leda’s standard library).

### 2.10 Garbage collection (compacting)

The GC is either:

- **two-space copying (Baker-style)**, or  
- **mark-sweep-compact**.

In both cases:

- **Roots:**
  - constant pool entries that reference heap objects,
  - global variables,
  - closures reachable from globals,
  - environments reachable from closures,
  - environments referenced by stack frames,
  - heap objects reachable from any of the above.

Stack frames themselves are not moved, but they contain NaN-boxed values that may reference heap objects; the GC updates those references as needed.

Because:

- environments are heap objects,
- closures are heap objects,
- objects/arrays/strings are heap objects,

…the collector can compact memory and update all pointers, keeping memory usage under control.

---

## 3. Why this design fits Leda

This front end + VM design matches Leda’s needs:

- **Functional core:**
  - first-class functions
  - closures
  - lexical scoping
  - proper tail calls

- **Logic layer:**
  - implemented entirely in Leda using CPS and closures
  - no special VM support beyond closures + TCO + dynamic typing

- **Static classes:**
  - compiled into constant pool descriptors
  - simple object model with fixed field/method layouts

- **Performance and implementation:**
  - NaN-boxing supports floats efficiently
  - register-based bytecode is JIT-friendly
  - stack frames are simple and fast
  - heap environments allow closures to outlive frames
  - compacting GC keeps memory usage reasonable

## Object Table Impacts

Here’s a focused pass at what changes if you introduce an **object table** (handles) instead of storing raw heap pointers in NaN-boxed values.

---

### 1. What “object table” means here

- Every heap object (string, array, closure, env, object, etc.) gets an **object ID** (index).
- The VM maintains a global **object table**: an array of pointers to actual heap objects.
- NaN-boxed values store **object IDs**, not raw pointers.
- To use an object, the VM/JIT does:
  - `ptr = object_table[id]`
  - then dereferences `ptr`.

So you get an extra level of indirection between values and actual heap memory.

---

### 2. Impact on the VM

#### 2.1 Value representation

- Instead of “tag + raw pointer”, you now have:
  - “tag + object ID” (small integer index).
- NaN-boxing still works:
  - floats are real doubles,
  - non-floats encode tags + small ints (including object IDs).

#### 2.2 Accessing heap objects

Every time the VM needs to touch a heap object:

- **Before:**
  - decode tag → raw pointer → use object.
- **With object table:**
  - decode tag → object ID → `object_table[id]` → pointer → use object.

So every heap access adds one array lookup.

#### 2.3 Environments, closures, objects

- Environments:
  - env references in registers/env slots are object IDs.
  - to access env slots: `env_ptr = object_table[env_id]`.
- Closures:
  - closure references are object IDs.
  - to call: `closure_ptr = object_table[closure_id]`.
- Objects:
  - instance references are object IDs.
  - field access: `obj_ptr = object_table[obj_id]`.

The **logical model** (frames, envs, closures) doesn’t change—only how you reach the underlying memory.

---

### 3. Impact on GC

This is where an object table really changes the story.

#### 3.1 Without object table

- NaN-boxed values contain **raw pointers**.
- A compacting GC must:
  - move objects,
  - update all pointers:
    - in stack frames,
    - in environments,
    - in closures,
    - in other heap objects,
    - in the constant pool.

That means scanning all roots and all heap objects to rewrite pointers.

#### 3.2 With object table

- NaN-boxed values contain **object IDs**, not pointers.
- The object table holds the **only real pointers** to heap objects.
- A compacting GC can:
  - move objects in memory,
  - update only the **object table entries** (IDs → new pointers),
  - leave all NaN-boxed values untouched.

So:

- **Roots**: stack frames, envs, closures, constant pool now hold IDs, not pointers.
- **GC work**:
  - mark live objects (by ID),
  - move them,
  - update `object_table[id]` to point to the new location.

You no longer need to rewrite every reference in the stack/heap—just the table.

#### 3.3 Pros for GC

- Compaction becomes much simpler:
  - no pointer rewriting in user-visible data structures.
- You can:
  - move objects freely,
  - even change their layout,
  - as long as `object_table[id]` is updated.
- You can implement:
  - moving generations,
  - object relocation,
  - heap resizing,
  with minimal impact on the rest of the VM.

#### 3.4 Cons for GC

- You now have to manage:
  - the object table itself (grow/shrink, free slots).
- You need a strategy for:
  - reusing freed IDs,
  - avoiding fragmentation in the table.

But these are manageable bookkeeping problems.

---

### 4. Impact on JIT

This is the interesting tradeoff.

#### 4.1 Extra indirection cost

Every heap access in JIT code becomes:

- **Before:**
  - `ptr = value_as_pointer(v)`
- **With object table:**
  - `id = value_as_id(v)`
  - `ptr = object_table[id]`

So you pay an extra load for:

- env access,
- closure access,
- object field access,
- array access, etc.

#### 4.2 JIT optimizations to hide the cost

A JIT can mitigate this:

- **Hoist table lookups:**
  - If you use the same object multiple times in a block, load `ptr` once into a machine register and reuse it.
- **Inline caches:**
  - For method calls, you can cache:
    - `(class_id → method_ptr)` mappings,
    - bypassing repeated table lookups.
- **Specialization:**
  - For hot paths, JIT can:
    - assume certain IDs are stable,
    - keep `ptr` in a register across multiple operations.

So the raw cost is one extra memory load, but a decent JIT can often hide it.

#### 4.3 GC safety and JIT

With an object table:

- JIT code can keep **raw pointers** in machine registers between GC safepoints, because:
  - the GC only updates `object_table[id]`,
  - but if objects move, those raw pointers become stale.
- To stay safe:
  - either:
    - don’t allow GC while raw pointers are live (only at safepoints where you flush back to IDs), or
    - treat raw pointers as **non-GC-safe** and recompute them from IDs after GC.

Simplest approach:

- At GC safepoints:
  - ensure all live values are in NaN-boxed form (IDs) in registers/envs.
  - JIT discards raw pointers and recomputes them after GC.

This is similar to the non-object-table design, but the object table makes compaction easier.

---

### 5. Overall tradeoffs

#### VM

**Pros:**

- Simpler object movement and compaction.
- Clear separation between:
  - value representation (IDs),
  - actual heap layout (pointers).

**Cons:**

- Extra indirection on every heap access.

#### GC

**Pros:**

- Compaction is much simpler:
  - update object table entries, not all references.
- Easier to implement:
  - moving generations,
  - heap resizing,
  - different allocation arenas.

**Cons:**

- Need to manage the object table:
  - free list of IDs,
  - potential table fragmentation.

#### JIT

**Pros:**

- GC becomes less intrusive:
  - JIT doesn’t need to rewrite pointers in its own data structures.
- JIT can:
  - hoist object table lookups,
  - keep raw pointers in registers between safepoints.

**Cons:**

- Baseline cost: one extra load per heap access.
- Must be careful about:
  - not using stale raw pointers after GC,
  - flushing back to IDs at safepoints.

---

### 6. When an object table makes sense for Leda

It’s most attractive if:

- you really want **aggressive compaction** and **moving GC** with minimal complexity, and/or
- you expect a lot of **heap object movement** (e.g., generational GC, copying young generation).

It’s less attractive if:

- you’re extremely sensitive to per-access overhead, and
- your GC is simple enough that pointer rewriting isn’t a big burden.

Given Leda’s design:

- lots of closures and environments,
- logic programming layer allocating many small objects,
- desire for compacting GC,

…an object table is a reasonable simplification on the GC side, at the cost of a small, JIT-mitigable indirection on the VM side.

If you want, we can next sketch:

- how the **NaN-boxed ID format** would look, or  
- how a **baseline JIT** would generate code with object-table-aware loads/stores.

You’re absolutely right: if **object IDs are 32‑bit**, then the env‑slot reference format should reflect that, and the slot index does **not** need anywhere near 24 bits. With a 32‑bit environment ID and a 12‑bit slot index, we get a clean, compact, GC‑friendly, JIT‑friendly encoding that still fits comfortably inside a NaN‑boxed payload.

Below is the refined, corrected bit layout.

---

## Revised NaN‑boxed env‑slot reference format  
**Environment ID = 32 bits**  
**Slot index = 12 bits**  
**TAG = 7 bits (more than enough)**  
**Fits inside the 51‑bit NaN payload**

```
  63        52 51     45 44                     13 12           0
+-------------+----------+------------------------+---------------+
|  NaN tag    |  TAG     |   environment ID (32)  | slot index(12)|
+-------------+----------+------------------------+---------------+
```

### Field sizes

| Field | Bits | Purpose |
|-------|------|---------|
| NaN tag | 12 | Identifies this as a NaN‑boxed non‑float |
| TAG | 7 | Distinguishes env‑slot refs from other tagged values |
| environment ID | 32 | Index into the object table |
| slot index | 12 | Index into the environment’s slot array |

Total payload bits = 7 + 32 + 12 = **51 bits**, exactly the mantissa size.

---

# 🧩 Why this layout is ideal

### ✔ 32‑bit environment IDs  
Matches your requirement and the object‑table design:

- up to **4 billion** heap objects  
- GC only updates `object_table[env_id]`  
- registers never need pointer rewriting  

### ✔ 12‑bit slot index  
Supports up to **4096 captured variables** per environment.

Realistically:

- most environments have < 10 slots  
- even deeply nested closures rarely exceed 50  
- 4096 is enormous headroom

### ✔ 7‑bit TAG  
Allows up to **128 distinct non‑float types**, including:

- object ID  
- closure  
- string  
- array  
- logic variable  
- environment reference  
- class descriptor  
- method reference  
- etc.

This is more than enough for a dynamic language VM.

---

##How the VM decodes an env‑slot reference

Given a 64‑bit value `v`:

1. Check exponent = 0x7FF → NaN  
2. Extract TAG → is it `TAG_ENV_REF`?  
3. Extract fields:

```
env_id    = (v >> 12) & 0xFFFFFFFF
slot_idx  = v & 0xFFF
```

4. Resolve environment pointer:

```
env_ptr = object_table[env_id]
```

5. Access slot:

```
value = env_ptr->slots[slot_idx]
```

6. For writes:

```
env_ptr->slots[slot_idx] = new_value
```

---

## GC interaction

GC only updates:

```
object_table[env_id]
```

Registers containing env‑slot references **never change**, because:

- `env_id` is stable  
- `slot_idx` is stable  

This is extremely GC‑friendly.

---

# 🧱 JIT interaction

The JIT can inline env access as:

```
env_ptr = object_table[env_id]
value   = env_ptr->slots[slot_idx]
```

Or hoist:

- `env_ptr` into a machine register  
- `slot_ptr` into another register  

This makes closure variable access extremely fast.

---

# 🎯 Final Summary

Your corrected constraints give us a clean, efficient env‑slot reference:

- **32‑bit environment ID**  
- **12‑bit slot index**  
- **7‑bit TAG**  
- **Fits perfectly in the 51‑bit NaN payload**  
- **GC‑friendly**  
- **JIT‑friendly**  
- **No extra heap allocations**  
- **Fast closure access**  

If you want, I can now sketch:

- **the full NaN‑box layout for all value types**  
- **the bytecode instructions for LOAD_ENV / STORE_ENV**  
- **how promotion rewrites registers into env‑slot references**