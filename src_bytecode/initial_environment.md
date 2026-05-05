# Leda Bytecode: Initial Environment Setup

This document describes in detail how the Leda bytecode compiler and virtual machine
establish the initial execution environment — classes, built-in type registrations, and
singleton objects such as `true` and `false` — before any user-written code runs.

---

## Overview

There is no separate bootstrap phase in the VM. Initialization is not special-cased.
Everything — class table construction, method registration, singleton creation, and
built-in type registration — happens through ordinary bytecode execution inside the
top-level function `__top__`, which is the first function entered when the VM starts.

The work is divided across two tools:

- **`ledac`** (compiler): runs the frontend, resolves symbols, generates the AST, and
  lowers everything — including standard-library initialization — to bytecode in `__top__`
- **`ledavm`** (VM): loads the `.lbc` file, zero-initializes its runtime state, enters
  `__top__`, and executes until `BC_OP_HALT`

---

## Phase 1 — Compiler Frontend Initialization

When `ledac` starts, before any source file is parsed, it calls `lc_initSymbolTable()`
(`lc_frontend.c:384`). This function builds the global symbol table and creates the
compiler-level type records that represent the built-in types.

### Global symbol slots

Three global variable records are allocated at fixed slot positions:

| Slot | Name    | Initial type         |
|------|---------|----------------------|
| 0    | `NIL`   | `undefinedType`      |
| 1    | `true`  | `trueType`           |
| 2    | `false` | `falseType`          |

These slots are pre-allocated so that later code can refer to `true`, `false`, and `NIL`
as ordinary variables.

### Built-in type records

`lc_initSymbolTable` creates a `struct typeRecord *` for each built-in type:

```c
objectType   = makeInitialClass(gs, "object",          0);
ClassType    = makeInitialClass(gs, "Class",           objectType);
booleanType  = makeInitialClass(gs, "boolean",         objectType);
integerType  = makeInitialClass(gs, "integer",         0);          // no parent
realType     = makeInitialClass(gs, "real",            0);          // no parent
stringType   = makeInitialClass(gs, "string",          objectType);
trueType     = makeInitialClass(gs, "True",            booleanType);
falseType    = makeInitialClass(gs, "False",           booleanType);
undefinedType = makeInitialClass(gs, "Leda_undefined", objectType);
```

These type records are the compiler's representation of the type hierarchy. They do not
yet exist as runtime objects. They exist only as C structs in the compiler's memory.
The corresponding runtime class table objects are created later by the bytecode that
`ledac` emits.

The `bc_builtin_class_tag` function (`bc_emit.c:790`) maps these type record pointers to
integer tags used by `BC_OP_REGISTER_BUILTIN`:

| Tag | Constant             | Type record    |
|-----|----------------------|----------------|
| 0   | `BC_BUILTIN_INTEGER` | `integerType`  |
| 1   | `BC_BUILTIN_STRING`  | `stringType`   |
| 2   | `BC_BUILTIN_BOOLEAN` | `booleanType`  |
| 3   | `BC_BUILTIN_REAL`    | `realType`     |
| 4   | `BC_BUILTIN_TRUE`    | `trueType`     |
| 5   | `BC_BUILTIN_FALSE`   | `falseType`    |

---

## Phase 2 — Bytecode Compilation

After the frontend initializes, `ledac` parses the source files (typically `std.led`
followed by user code) and lowers everything to bytecode via `bc_compile_top_level`
(`bc_emit.c`). All initialization bytecode ends up in the function named `__top__`,
which is always function index 0 in the module.

### Class table object layout

A class table is a runtime object (`BC_CONST_OBJECT`) produced by `BC_OP_BUILD_INSTANCE`.
Every class table has the following fixed slot layout:

| Slot | Content                                              |
|------|------------------------------------------------------|
| 0    | Integer 0 (bootstrap placeholder; ideally the Class  |
|      | class table pointer, but set to 0 at construction)   |
| 1    | `BC_CONST_ENVREF` — index into `vm->environments[]` |
|      | capturing the lexical context where the class was    |
|      | defined                                              |
| 2    | Class name string (e.g., `"boolean"`, `"True"`)      |
| 3    | Integer 0 (size field, initialized to 0)             |
| 4    | Parent class table (wired in by a separate assignment |
|      | after the class table is stored)                     |
| 5+   | Method closures (`BC_CONST_FUNCTION`), one per method|
|      | at the slot index assigned by the frontend           |

Slots 0 and 1 are always set by `BC_OP_BUILD_INSTANCE` itself. Slots 2 through
`methodTableSize - 1` are supplied as constructor arguments on the operand stack.

This layout corresponds directly to the `Class` class definition in `std.led`:

```leda
class Class of ordered[Class];
var
    name   : string;    { slot 2 }
    size   : integer;   { slot 3 }
    parent : Class;     { slot 4 }
```

The method closures stored at slots 5 and above are the indexed entries that
`BC_OP_MAKE_METHOD` looks up when a method call is dispatched.

### Class table generation — `genClassTableLiteral`

For each class declaration, the frontend calls `genClassTableLiteral` (`gen_frontend.c:380`),
which produces a `buildInstance` AST node encoding:

1. A zero integer for slot 0 (the class pointer bootstrap value).
2. The class name string constant for slot 2.
3. Zero integers for all remaining slots (slots 3+), as defaults.
4. Method closure expressions (`makeClosure` nodes) at the slot positions assigned to
   each method by the frontend symbol table (`method_symbol->u.f.location`).

The compiler then lowers this AST node via the `buildInstance` case in
`bc_compile_expression` (`bc_emit.c:1164`).

---

## Phase 3 — VM Initialization

When `ledavm` loads the `.lbc` file and calls `bc_vm_init` (`vm.c:1133`), it performs
the following steps before any bytecode instruction executes:

```c
int bc_vm_init(struct bc_vm *vm, struct bc_module *module) {
    vm->module = module;
    vm->stack_size = 0;
    memset(vm->builtin_class_tables, 0, sizeof(vm->builtin_class_tables));
    memset(vm->frame_slots,          0, sizeof(vm->frame_slots));
    memset(vm->env_slots,            0, sizeof(vm->env_slots));
    memset(vm->runtime_constants,    0, sizeof(vm->runtime_constants));
    vm->runtime_constant_count = 0;
    memset(vm->environments,         0, sizeof(vm->environments));
    vm->environment_count = 0;
    vm->env_slot_count    = 0;
    memset(vm->frames,               0, sizeof(vm->frames));
    vm->frame_count            = 0;
    vm->current_frame_index    = 0;

    return vm_enter_frame(vm, &module->functions[module->entry_function],
                          0, 0, SIZE_MAX, NULL, 0);
}
```

The `builtin_class_tables[BC_BUILTIN_COUNT]` array is all null at this point. No
primitive receiver dispatch can work until `BC_OP_REGISTER_BUILTIN` is executed
during the run of `__top__`.

`vm_enter_frame` is called with:

| Parameter            | Value     | Meaning                          |
|----------------------|-----------|----------------------------------|
| `function`           | `__top__` | The entry bytecode function      |
| `arg_count`          | 0         | No arguments                     |
| `caller_frame_index` | 0         | No caller (entry point)          |
| `closure_env_index`  | `SIZE_MAX`| No enclosing lexical environment |

This sets `frame->promoted_env_index = SIZE_MAX` and zeroes all local slots.
The first call to `BC_OP_BUILD_INSTANCE` inside `__top__` will trigger
`vm_promote_frame_environment` to create environment 0, which becomes the
construction context stored in slot 1 of every class table built at the top level.

---

## Phase 4 — Execution of `__top__`

`bc_vm_run` (`vm.c:1171`) begins the main dispatch loop. The first thing `__top__`
executes is the compiled standard library. The following subsections describe the
bytecode sequences involved.

### 4.1 Building a class table

For each class defined in `std.led`, the bytecode in `__top__` follows this pattern.
Using `class boolean of equality[boolean]` as a representative example:

```
; Push slot 0: integer 0 (class pointer placeholder)
BC_OP_CONST    <idx: integer 0>

; Push slot 2: class name string
BC_OP_CONST    <idx: string "boolean">

; Push slot 3: integer 0 (size field default)
BC_OP_CONST    <idx: integer 0>

; Push slot 4 placeholder: integer 0
; (slot 4 is set to the parent class later via a separate assignment)
BC_OP_CONST    <idx: integer 0>

; Push slot 5: closure for method 'not'
BC_OP_MAKE_CLOSURE  <fn_idx_for_boolean_not>  <context_depth>

; Push slot 6: closure for method 'or'
BC_OP_MAKE_CLOSURE  <fn_idx_for_boolean_or>   <context_depth>

; Push slot 7: closure for method 'and'
BC_OP_MAKE_CLOSURE  <fn_idx_for_boolean_and>  <context_depth>

; Construct the class table object
; slot_count = methodTableSize (e.g., 8), arg_count = slot_count - 2 = 6
BC_OP_BUILD_INSTANCE  <slot_count>  <arg_count>
```

`BC_OP_BUILD_INSTANCE` executes as follows (`vm.c:1420`):

1. Allocates a new object with `slot_count` slots via `vm_alloc_runtime_object`.
2. Calls `vm_promote_frame_environment` (if not yet promoted) to create an environment
   record for the current `__top__` frame, then calls `vm_alloc_runtime_envref` to
   create a `BC_CONST_ENVREF` constant pointing to that environment.
3. Sets:
   - `slots[0]` = the class-pointer value from the stack (integer 0, the bootstrap value)
   - `slots[1]` = the `BC_CONST_ENVREF` constant
   - `slots[2..2+arg_count-1]` = the remaining values from the stack in order
4. Pops the class-pointer value and all arg values from the stack.
5. Pushes the new object onto the stack.

After `BUILD_INSTANCE`, the class table object is on top of the stack. The compiler
then emits a store to place it in the appropriate local slot (the class variable in the
surrounding context):

```
; Store the class table into local slot <n> (the 'boolean' class variable)
BC_OP_STORE_LOCAL  <n>
```

The assignment code in `bc_compile_expression` (`bc_emit.c:1074`) checks whether the
right-hand side is a `buildInstance` expression with `size > 2` and a recognized
built-in result type. If so, it emits `BC_OP_REGISTER_BUILTIN` immediately after the
store, while the class table object is still on top of the stack:

```
; Register this class table as the canonical table for primitive 'boolean' dispatch
BC_OP_REGISTER_BUILTIN  2     ; tag BC_BUILTIN_BOOLEAN
BC_OP_POP                     ; discard the class table from the operand stack
```

`BC_OP_REGISTER_BUILTIN` (`vm.c:1639`):

```c
case BC_OP_REGISTER_BUILTIN: {
    uint64_t tag;
    vm_read_u64le(vm, &tag);
    vm->builtin_class_tables[tag] = vm->stack[vm->stack_size - 1];
    break;
}
```

This stores the class table pointer into `vm->builtin_class_tables[tag]`. From this
point forward, `BC_OP_MAKE_METHOD` on a primitive value of that type can resolve its
method table.

### 4.2 Wiring the parent class table (slot 4)

After the class table is stored, the frontend generates a second assignment that wires
the parent class table into slot 4 (`gen_frontend.c:193`). For `boolean`, whose parent
is `object`, the emitted bytecode is:

```
; Load the already-stored class table for 'boolean'
BC_OP_LOAD_LOCAL   <n_boolean>

; Load the already-stored class table for 'object'
BC_OP_LOAD_LOCAL   <n_object>

; Write it into slot 4 of the boolean class table
BC_OP_STORE_OBJECT_SLOT  4
BC_OP_POP
```

After this sequence, `boolean_class_table->slots[4]` points to `object_class_table`.
This parent-chain link is what `BC_OP_BR_IF_NOT_KIND` traverses when checking whether
an object is an instance of an ancestor class.

### 4.3 Creation order for built-in class tables

Classes must be constructed in dependency order: a class must be built before it can be
referenced as a parent. The order the frontend emits them matches the class hierarchy:

1. `object`        — root; parent slot 4 left null
2. `Class`         — parent: `object`
3. `boolean`       — parent: `object`
4. `integer`       — parent: none (null parent)
5. `real`          — parent: none
6. `string`        — parent: `object`
7. `True`          — parent: `boolean`
8. `False`         — parent: `boolean`
9. `Leda_undefined`— parent: `object`

`BC_OP_REGISTER_BUILTIN` is emitted only for types that have a compiler-known builtin
tag (tags 0–5). `object`, `Class`, and `Leda_undefined` do not get a `REGISTER_BUILTIN`
instruction because they are not in the primitive dispatch registry.

### 4.4 Creating the `true` and `false` singletons

After the `True` and `False` class tables are registered, the compiler emits code to
create the singleton instances that the Leda runtime binds to the global variables
`true` and `false`.

The singleton construction uses `buildInstance` with `size = 2` and zero arguments.
Because `size == 2` (not `> 2`), the `REGISTER_BUILTIN` trigger does not fire — only
the class-table construction (size > 2) fires it.

For `true`:

```
; Push the True class table (the class pointer for the new singleton)
BC_OP_LOAD_LOCAL   <n_True_class_table>

; Construct an object with 2 slots (classPtr + envref), no extra args
BC_OP_BUILD_INSTANCE  2  0

; Store into global slot 1 (the 'true' variable)
BC_OP_STORE_LOCAL   1     ; or STORE_CAPTURE_LOCAL if 'true' is captured
BC_OP_POP
```

`BC_OP_BUILD_INSTANCE 2 0` creates:
- `slots[0]` = the True class table (the class pointer — the singleton "is a True")
- `slots[1]` = `BC_CONST_ENVREF` for the current environment

For `false`:

```
BC_OP_LOAD_LOCAL   <n_False_class_table>
BC_OP_BUILD_INSTANCE  2  0
BC_OP_STORE_LOCAL   2
BC_OP_POP
```

After this, global slot 1 holds the `true` singleton and global slot 2 holds the
`false` singleton.

### 4.5 How `true` and `false` are distinguished at runtime

The VM distinguishes the two boolean singletons by class table pointer identity.

`vm_condition_is_false` (`vm.c:1101`) is called by `BC_OP_JUMP_IF_FALSE`. For an
object value (a `BC_CONST_OBJECT`), it checks:

```c
struct bc_constant *class_table = condition->value.object.slots[0];
*is_false = (class_table != NULL
             && class_table == vm->builtin_class_tables[BC_BUILTIN_FALSE]);
```

An object is "false" if and only if its class table pointer is the registered False
class table. The `true` singleton is "truthy" because its class table pointer is the
True class table, not the False class table.

For `BC_OP_MAKE_METHOD` on a `BC_CONST_BOOLEAN` (raw boolean, not an object), the VM
selects between `BC_BUILTIN_TRUE` and `BC_BUILTIN_FALSE` based on the boolean value:

```c
case BC_CONST_BOOLEAN:
    builtin_idx = receiver->value.integer ? BC_BUILTIN_TRUE : BC_BUILTIN_FALSE;
    break;
```

---

## Phase 5 — Primitive Method Dispatch Setup

Once the built-in class tables are registered, method calls on primitive values can be
resolved. `BC_OP_MAKE_METHOD` (`vm.c:1571`) is the instruction that binds a method to
its receiver.

### For user-defined object receivers

```c
// Get the class table from slot 0 of the receiver
vm_get_object_slot_ref(receiver, 0, &method_ref, ...);
// Get the method closure from the class table at the given slot
vm_get_object_slot_ref(*method_ref, method_index, &method_ref, ...);
```

### For primitive receivers (integer, string, real, boolean)

```c
class_table = vm->builtin_class_tables[builtin_idx];
// Build a primitive environment wrapper
vm_bind_primitive_environment(vm, receiver, class_table, &method_env_index, ...);
// Get the method closure from the class table at the given slot
vm_get_object_slot_ref(class_table, method_index, &method_ref, ...);
```

`vm_bind_primitive_environment` (`vm.c:499`) creates a 2-slot wrapper object:
- `wrapper->slots[0]` = the class table (for capture resolution inside the method)
- `wrapper->slots[1]` = the primitive receiver value itself

It also inherits the construction context from the class table's slot 1 (`BC_CONST_ENVREF`),
so that method bodies compiled with captured outer variables can resolve depth > 1
captures correctly.

The resulting `bc_environment` record has `self` set to the primitive receiver, which
`vm_enter_frame` then pre-populates into local slot 1 of the method's frame:

```c
if(effective_local_count >= 2 && closure_env_index != SIZE_MAX ...) {
    struct bc_constant *self_val = vm->environments[closure_env_index].self;
    if(self_val != NULL) { vm->frame_slots[local_base + 1] = self_val; }
}
```

---

## Phase 6 — Pattern Matching Support (`is` checks)

`BC_OP_BR_IF_NOT_KIND` (`vm.c:1528`) implements the `is` type test used in Leda
pattern matching. It pops a class object and an instance from the stack, then walks
the class chain of the instance:

```c
current_class = base->value.object.slots[0];   // get instance's class table
while (current_class != NULL && ...) {
    if (current_class == class_obj) { matched = true; break; }
    if (current_class->value.object.slot_count <= 4) { break; }
    struct bc_constant *parent = current_class->value.object.slots[4];  // slot 4
    if (parent == NULL || parent == current_class) { break; }
    current_class = parent;
}
```

This traversal is possible because slot 4 of every class table was wired to the parent
class table during the initialization sequence described in §4.2.

---

## Summary: Initialization Sequence at Runtime

| Step | Instruction(s) | Effect |
|------|----------------|--------|
| 1 | `bc_vm_init` | Zero-initializes all VM state; `builtin_class_tables` all null; enters `__top__` |
| 2 | `BUILD_INSTANCE` (first call) | Triggers environment 0 creation for `__top__`; creates `object` class table |
| 3 | `STORE_LOCAL` | Stores `object` class table to its local slot |
| 4 | `BUILD_INSTANCE` × N | Creates each built-in class table in dependency order |
| 5 | `STORE_OBJECT_SLOT 4` × N | Wires parent class table into slot 4 of each class table |
| 6 | `REGISTER_BUILTIN tag` × 6 | Populates `builtin_class_tables[0..5]` (integer, string, boolean, real, True, False) |
| 7 | `BUILD_INSTANCE 2 0` × 2 | Creates the `true` singleton (slots: True class table, envref) |
| 8 | `BUILD_INSTANCE 2 0` × 1 | Creates the `false` singleton (slots: False class table, envref) |
| 9 | Remaining `__top__` | User code and library functions execute |

After step 8, the Leda environment is fully initialized:
- all class tables exist as runtime objects
- all methods are stored as closure constants in their class table slots
- the parent chain (slot 4) is populated for every class, enabling `is` checks
- `vm->builtin_class_tables` contains registered tables for all primitive types
- the `true` and `false` globals hold their singleton object instances
- `NIL` remains null (uninitialized, checked by `Leda_object_defined`)

---

## Key Data Structures

### `struct bc_vm` (vm.h)

```c
struct bc_vm {
    struct bc_module    *module;
    struct bc_constant  *builtin_class_tables[6]; // indexed by BC_BUILTIN_* tags
    struct bc_constant  *stack[256];
    size_t               stack_size;
    struct bc_constant  *frame_slots[512];
    struct bc_constant  *env_slots[1024];
    struct bc_constant   runtime_constants[65536];
    size_t               runtime_constant_count;
    struct bc_environment environments[128];
    size_t               environment_count;
    size_t               env_slot_count;
    struct bc_frame      frames[64];
    size_t               frame_count;
    size_t               current_frame_index;
};
```

### `struct bc_frame` (vm.h)

```c
struct bc_frame {
    struct bc_function *function;       // active bytecode function
    size_t              ip;             // instruction pointer
    size_t              caller_frame_index;
    size_t              closure_env_index;  // SIZE_MAX if no closure env
    size_t              promoted_env_index; // SIZE_MAX until first capture
    size_t              stack_base;
    size_t              arg_base;
    size_t              local_base;
    uint32_t            arg_count;
    uint32_t            local_count;
};
```

### `struct bc_environment` (vm.h)

```c
struct bc_environment {
    size_t              parent_env_index;
    size_t              arg_base;
    size_t              local_base;
    uint32_t            arg_count;
    uint32_t            local_count;
    struct bc_constant *object;   // non-null for object/primitive method envs
    struct bc_constant *self;     // the receiver value for method frames
};
```

### `struct bc_constant` (bytecode.h)

```c
struct bc_constant {
    enum bc_constant_kind kind;  // INTEGER, STRING, REAL, BOOLEAN, FUNCTION,
                                 // REFERENCE, OBJECT, ENVREF
    union {
        int64_t      integer;
        double       real;
        char        *string;
        struct { uint64_t function_index; uint64_t parent_env_index; } closure;
        struct bc_constant **slot_ref;
        struct { uint64_t slot_count; struct bc_constant **slots; } object;
        uint64_t     env_index;
    } value;
};
```

---

## Relevant Source Locations

| File                 | Symbol / Line           | Role                                          |
|----------------------|-------------------------|-----------------------------------------------|
| `lc_frontend.c:384`  | `lc_initSymbolTable`    | Creates global symbols and type records       |
| `gen_frontend.c:380` | `genClassTableLiteral`  | Builds the `buildInstance` AST for each class |
| `gen_frontend.c:193` | parent-chain fixup      | Emits slot-4 assignment for parent wiring     |
| `bc_emit.c:790`      | `bc_builtin_class_tag`  | Maps type records to REGISTER_BUILTIN tags    |
| `bc_emit.c:1074`     | REGISTER_BUILTIN emit   | Decides when to emit REGISTER_BUILTIN         |
| `bc_emit.c:1164`     | `buildInstance` case    | Lowers buildInstance AST to bytecode          |
| `vm.c:1133`          | `bc_vm_init`            | VM zero-initialization and frame entry        |
| `vm.c:945`           | `vm_enter_frame`        | Frame setup, self pre-population              |
| `vm.c:1420`          | `BC_OP_BUILD_INSTANCE`  | Object allocation and slot assignment         |
| `vm.c:1639`          | `BC_OP_REGISTER_BUILTIN`| Writes to `builtin_class_tables[]`            |
| `vm.c:1101`          | `vm_condition_is_false` | False singleton detection via class table ptr |
| `vm.c:1528`          | `BC_OP_BR_IF_NOT_KIND`  | Class chain walk for `is` pattern matching    |
| `vm.c:1571`          | `BC_OP_MAKE_METHOD`     | Method binding for objects and primitives     |
| `vm.c:499`           | `vm_bind_primitive_environment` | Primitive receiver wrapper creation  |
