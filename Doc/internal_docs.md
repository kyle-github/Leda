# Leda Implementation Notes

## Table of Contents

- [Overview](#overview)
- [How The Interpreter Works](#how-the-interpreter-works)
    - [Runtime values](#runtime-values)
    - [Runtime object layout table](#runtime-object-layout-table)
    - [Parse-time code generation](#parse-time-code-generation)
    - [Execution model](#execution-model)
    - [Closures, methods, and contexts](#closures-methods-and-contexts)
    - [Control flow](#control-flow)
    - [Pattern matching](#pattern-matching)
    - [Memory management](#memory-management)
- [How Relations Are Implemented](#how-relations-are-implemented)
- [How Functions Are Implemented](#how-functions-are-implemented)
- [How Closures Are Implemented](#how-closures-are-implemented)
- [How Primitive Calls Work](#how-primitive-calls-work)
- [Source Files And Functions](#source-files-and-functions)
    - [Src/lexer.l](#srclexerl)
    - [Src/gram.y](#srcgramy)
    - [Src/lc.c](#srclcc)
    - [Src/types.c](#srctypesc)
    - [Src/gen.c](#srcgenc)
    - [Src/interp.c](#srcinterpc)
    - [Src/memory.c](#srcmemoryc)
    - [Src/test.c](#srctestc)
- [Grammar Reference](#grammar-reference)
    - [Terminals](#terminals)
    - [Non-terminals](#non-terminals)

## Overview

This document describes the C, yacc, and lex sources that make up the Leda implementation.

The important correction up front is that the runtime is not a bytecode interpreter. Leda parses source text into syntax tree objects and statement lists, then evaluates those trees directly. The parser in Src/gram.y calls the constructors in Src/gen.c while reductions happen. Those constructors build expressionRecord and statementRecord nodes declared in Src/interp.h. When parsing finishes, beginInterpreter in Src/interp.c creates the initial runtime environment and evaluateStatement walks the generated statement list.

At a high level the pipeline is:

1. Src/lexer.l tokenizes the input stream and manages nested include files.
2. Src/gram.y parses declarations, statements, and expressions.
3. Grammar actions call functions in Src/lc.c, Src/types.c, and Src/gen.c to build symbol tables, type records, expressions, and statements.
4. beginInterpreter in Src/interp.c creates class objects, global bindings, and common constants.
5. evaluateStatement and evaluateExpression execute the program directly against runtime ledaValue objects.
6. Src/memory.c manages object allocation and copying garbage collection.

## How The Interpreter Works

### Runtime values

Runtime values are stored as struct ledaValue objects from Src/memory.h. Each object has:

- a size field whose low bits encode GC state and the binary-object flag
- a flexible array of union slots, where each slot can hold an integer, floating-point value, C string pointer, or object pointer

The interpreter uses a convention for object layout:

- slot 0 is usually the class or method table pointer
- slot 1 is usually the surrounding context
- later slots hold fields, arguments, locals, constants, or binary payload data

Binary values such as integers, reals, strings, and references are still wrapped as ledaValue objects, but their payload lives in union members such as ival, fval, or sval.

### Runtime object layout table

The interpreter does not use one universal object schema. Instead, different expression kinds and runtime helpers allocate different shapes and interpret the slots by convention.

| Runtime object kind | Allocated by | Binary flag | Slot layout |
| --- | --- | --- | --- |
| Global context | beginInterpreter via staticAllocate(syms->size) | no | slot n = global variable, class object, function code pointer, or constant slot |
| Locals block | evaluateStatement for makeLocalsStatement | no | slot n = local variable or local constant storage |
| Function activation record | evaluateExpression/doFunctionCall and evaluateStatement/tailCall | no | slot 1 = lexical parent context, slot 2 = caller context, slot 3 = locals block, slot 4+ = actual arguments |
| Closure | makeClosure evaluation | no | slot 1 = lexical context, slot 2 = statementRecord pointer for function body |
| Method context helper | makeMethodContext evaluation | no | slot 1 = receiver object, slot 2 = statementRecord pointer for method body |
| Ordinary instance | buildInstance evaluation | no | slot 0 = class table, slot 1 = globalContext, slot 2+ = instance fields |
| Class table / class object | buildClassTable then fixClassTable | no | slot 0 = metaclass Class, slot 1 = globalContext, slot 2 = class name string object, slot 3 = method-table size integer object, slot 4 = parent class table, method code slots at method locations |
| Integer object | newIntegerConstant via binaryValue | yes | slot 0 = integerClass, slot 1 = globalContext, slot 2.ival = integer payload |
| Real object | newRealConstant | yes | slot 0 = realClass, slot 1 = globalContext, slot 2.fval = real payload |
| String object | newStringConstant | yes | slot 0 = stringClass, slot 1 = globalContext, slot 2.sval = raw C string pointer |
| Reference object | makeReference evaluation via binaryValue | yes | slot 0 = base object being referenced, slot 2.ival = referenced slot index |

Notes on the size field:

- `size >> 2` is the logical payload size used by the collector.
- bit `01` is used by the collector as the forwarding flag.
- bit `02` marks a binary object, which changes how `gc_move` traverses and patches it.

Two practical consequences follow from this layout:

- slot 0 does not always mean “class pointer”; for explicit references it is the referenced base object, and for temporary GC forwarding it may briefly hold collector state
- slot 1 is often a context pointer, but not for every object shape; the meaning comes from the opcode or allocator that created the object

### Parse-time code generation

The parser does not build a generic parse tree and lower it later. Instead, grammar actions immediately build executable nodes:

- expressions become expressionRecord nodes with operators such as getOffset, doFunctionCall, buildInstance, and patternMatch
- statements become linked statementRecord nodes such as returnStatement, conditionalStatement, and makeLocalsStatement
- symbol tables and type records are populated as declarations are recognized

That means much of Src/gen.c is really semantic analysis plus AST construction.

### Execution model

Execution starts in beginInterpreter.

- It allocates the global context object.
- It installs built-in globals such as NIL, true, and false.
- It stores class tables and top-level function entry points into the global context.
- It pre-allocates small integers.
- It patches class tables so each class object points at Class and at its parent class table.
- It then sets currentContext to globalContext and calls evaluateStatement on the top-level body.

Statements are executed sequentially through the next pointer in statementRecord. Expressions are evaluated recursively by evaluateExpression.

### Closures, methods, and contexts

A function value is represented as a closure object containing:

- the lexical context in slot 1
- the statement list pointer in slot 2

A method lookup produces a makeMethodContext expression. At runtime that expression packages the receiver object with the method body taken from the receiver's class table.

A function call allocates a fresh activation record. The activation record stores:

- slot 1: the lexical parent context for the callee
- slot 2: the caller context to restore on return
- slot 3: the locals block allocated by makeLocalsStatement
- slot 4 and up: actual arguments

By-name arguments are converted into thunks. By-reference arguments are converted into explicit reference objects. That behavior is implemented in generateFunctionCall and consumed at runtime by evalThunk and evalReference.

### Control flow

Control flow is represented with linked statements, not jump offsets.

- if and while become conditionalStatement nodes whose next and falsePart pointers route control flow
- return returns immediately from evaluateStatement
- tail-call-eligible returns are rewritten to tailCall so evaluateStatement can reuse the call loop instead of nesting another C return path

### Pattern matching

The IS syntax is compiled to a patternMatch expression. At runtime, the interpreter:

- evaluates the candidate object
- evaluates the class object being matched against
- walks up the candidate's parent chain through class tables
- if the class matches, optionally writes extracted fields into local variables supplied in the pattern

### Memory management

Dynamic objects are allocated from one semispace. When space runs out, gcollect flips to the other semispace and gc_move copies reachable objects there. Reachability starts from:

- currentContext
- globalContext and the values it contains
- rootStack entries pushed temporarily around evaluation points that might allocate

Static objects, such as some initialization-time structures and class tables, are allocated in a non-collected area with staticAllocate.

## How Relations Are Implemented

Relations are Leda's logic-programming layer. The important implementation point is that relations are not a separate runtime engine in C. There is no Prolog-style unifier, trail stack, or resolution machine in the interpreter. Instead, relations are encoded using ordinary Leda function values, closures, by-reference arguments, and a small set of library combinators defined in Test/std.led.

The implementation is split across three layers:

1. the compiler and type system define what a relation is and where relation syntax appears
2. the interpreter provides closures, by-name thunks, by-reference references, and function calls
3. the standard library defines the actual search, backtracking, and binding behavior in ordinary Leda code

### The core representation

In initialCreation in Src/lc.c, the built-in type `relation` is created as a function type:

- it has one argument named `future`
- that argument itself has type `relation`
- the return type is `boolean`

So, conceptually:

```text
relation == function(future : relation) -> boolean
```

This is a continuation-passing representation. A relation does not directly “return bindings.” Instead, it is called with another relation representing “what to do next if this step succeeds.”

The continuation name `future` is not accidental. It is the rest of the logical computation.

Operationally, a relation answers the question:

“Given a continuation relation representing the remaining goals, can I produce bindings that allow that continuation to succeed?”

That means a relation is a search procedure encoded as a closure.

### Why this works with the existing interpreter

The interpreter already supports the features needed to implement logic-style search:

- closures capture lexical state
- by-reference arguments let a relation temporarily bind variables
- by-name arguments delay evaluation and let the library control when the next goal is explored
- booleans communicate success or failure
- `<-` and `unify` can tentatively assign values and roll them back when later goals fail

Because these features already exist, the language can implement relations mostly as library code instead of special interpreter opcodes.

### Relation evaluation model

At runtime, a relation is just a closure object like any other function value. The difference is in the calling convention established by the library.

When a relation `r` is executed, the library expects code shaped like:

```text
r(future)
```

where `future` is another relation. The current relation may:

- fail immediately by returning `false`
- succeed and invoke `future(trueRelation)` or `future(...)`
- try multiple bindings by invoking its continuation under different temporary assignments

This is continuation-passing search.

### The base continuation: trueRelation

The simplest relation is `trueRelation` in Test/std.led:

```leda
function trueRelation (f : relation)->boolean;
begin
    return true;
end;
```

It ignores its continuation and simply succeeds. This acts as the terminal continuation for “there are no more goals.”

Calling:

```text
someRelation(trueRelation)
```

means “run `someRelation` and see whether it can produce at least one successful binding sequence.”

### Bridging booleans and relations

The compiler contains explicit bridge functions because control-flow expressions such as `if` and `while` require booleans, while relational expressions produce relations.

#### relationAsBoolean

Defined in Test/std.led as:

```leda
function relationAsBoolean (future : relation)->boolean;
begin
    return future(trueRelation);
end;
```

This converts a relation into a boolean by asking whether it has at least one solution. It does not enumerate all solutions. It only asks for the first success.

Compiler integration:

- booleanCheck in Src/gen.c wraps relation-valued expressions with a call to `relationAsBoolean`
- `if`, `while`, and similar boolean contexts therefore treat a relation as “true if it can succeed”

#### booleanAsRelation

Defined in Test/std.led as:

```leda
function booleanAsRelation (byName x : boolean)->relation;
begin
    return function(future : relation)->boolean;
    begin
        return x & future(trueRelation);
    end;
end;
```

This lifts a boolean into a relation. If the boolean succeeds, the continuation is run. If the boolean fails, the relation fails.

Compiler integration:

- relationCheck in Src/gen.c wraps boolean-valued expressions with `booleanAsRelation`
- this is used when a function is declared to return `relation` but its body computes a boolean

The argument is `byName`, so the boolean is not evaluated until the resulting relation is actually run.

### Backtracking by temporary assignment: `<-`

The special relational assignment syntax:

```leda
x <- y
```

is compiled by generateLeftArrow in Src/gen.c into a call to the library function `Leda_arrow`. The left-hand side must be assignable, because the compiler turns it into an explicit reference-like argument.

`Leda_arrow` in Test/std.led is the core backtracking primitive:

```leda
function Leda_arrow (byRef left : object, right : object)->relation;
begin
    return function(future : relation)->boolean;
        var save : object;
    begin
        save := left;
        left := right;
        if future(trueRelation) then
            return true;
        left := save;
        return false;
    end;
end;
```

This is the key mechanism for logic-style variable binding.

How it works:

1. save the old value of the logic variable
2. assign the new candidate value
3. run the continuation
4. if the continuation succeeds, report success and keep the binding
5. if the continuation fails, restore the old value and report failure

That is how Leda gets reversible bindings without a special trail stack in the C runtime.

### Unification

The generic `unify` relation in Test/std.led is defined as:

```leda
function unify [T : equality] (byRef left : T, right : T)->relation;
begin
    if defined(left) then
        return left = right
    else
        return left <- right;
end;
```

This is not a full structural Prolog unifier. It is a simple, pragmatic relation:

- if the left side is already bound, check equality
- if the left side is unbound, bind it using `<-`

So “unification” in Leda is implemented through ordinary equality plus reversible assignment.

### Conjunction and disjunction

The logical connectives are ordinary overloaded functions in Test/std.led.

#### Disjunction: `or`

For two relations:

```leda
function or (left : relation, byName right : relation)->relation;
begin
    return function(future : relation)->boolean;
    begin
        if left(future) then return true;
        return right(future);
    end;
end;
```

This means:

- try the left relation first
- if it finds a successful continuation, stop with success
- otherwise evaluate and try the right relation

The right operand is `byName`, so it is only evaluated if the left branch fails. That gives the library lazy search behavior.

#### Conjunction: `and`

For two relations:

```leda
function and (left : relation, byName right : relation)->relation;
begin
    return function(future : relation)->boolean;
    begin
        return left(function(f : relation)->boolean;
            begin
                return right(future);
            end);
    end;
end;
```

This means:

- run the left relation first
- if it succeeds, continue with the right relation
- the right relation is passed the original future continuation

This is where the CPS representation becomes visible: conjunction is not “evaluate both and combine.” It is “run the left search step with a continuation that performs the right search step.”

The library also supplies mixed overloads between booleans and relations, implemented through `booleanAsRelation`.

### Negation

Negation is deliberately not relation-valued in the standard library:

```leda
function not (arg : relation)->boolean;
begin
    return ~ relationAsBoolean(arg);
end;
```

So negating a relation means “convert it to the existence of at least one solution, then negate that boolean.” That is negation-as-failure, not generation of a complementary relation.

### Relation syntax in the grammar

The parser exposes relations through ordinary expression syntax plus a few specific hooks.

#### Relational operators

The grammar's `relationalExpression` rule compiles operators like `<`, `=`, `<>`, and `==` through generateBinaryOperator. Those operators can resolve either to boolean-returning methods/functions or relation-returning ones, depending on overloads visible in scope.

#### Left arrow

The special rule:

```text
reference LEFTARROW binaryExpression
```

compiles into `generateLeftArrow`, which produces a relation-valued call to `Leda_arrow`.

#### Relation-returning functions

A function declared as:

```leda
function p(byRef x : integer)->relation;
```

is just a normal function whose return type is the built-in `relation` function type. The parser and type checker treat it like any other function type.

### Relation use in `if` and `while`

When a relation appears in a boolean context, the compiler inserts `relationAsBoolean` through booleanCheck. That gives the relation one chance to find a successful path.

For example:

```leda
if pythagoras(5, 12, c) then ...
```

does not iterate over all solutions. It asks whether `pythagoras(5, 12, c)` can find a successful binding for `c`.

This is why relations can be used naturally inside imperative control flow.

### Relational `for`

The grammar has two relation-oriented loop forms:

- `for relationExpr do statement`
- `for relationExpr to stopExpr do statement`

generateForRelation in Src/gen.c compiles these loops into a call to the standard-library helper `Leda_forRelation`.

The compiler constructs a thunk that behaves like:

```leda
function()->boolean;
begin
    statement;
    return stopCondition;
end;
```

and then passes that thunk as a by-name boolean argument to `Leda_forRelation`.

`Leda_forRelation` is defined as:

```leda
function Leda_forRelation (rel : relation, byName stop : boolean);
begin
    if rel(function(f : relation)->boolean;
        begin
            return stop;
        end)
    then ;
end;
```

So the loop works by asking the relation to repeatedly drive execution of the body thunk. Each successful binding runs the body once and then checks the stop condition.

This is how the example in Test/chap20d.led can enumerate all Pythagorean triples under 20:

```leda
for pythagoras(a, b, c) do begin
    ...
end;
```

The relation itself generates candidate bindings; the `for` loop consumes them.

### Generators as relations

`integerRange` in Test/std.led shows how an ordinary search generator is written:

```leda
function integerRange(low, high, step : integer, byRef ident : integer)->relation;
begin
    return ident <- low 
        | (low <> high) & integerRange(low + step, high, step, ident);
end;
```

This is a relation-valued generator expressed entirely with:

- reversible binding via `<-`
- disjunction via `|`
- conjunction via `&`
- recursion

It first offers `ident = low`. If that path fails or later goals backtrack, the disjunction tries the recursive case and offers the next value.

### Example: search with constraints

The Pythagorean example in Test/chap20d.led:

```leda
function pythagoras(byRef x, y, z : integer)->relation;
begin
    return runify(x) & runify(y) & runify(z) &
        x * x + y * y = z * z;
end;
```

shows the intended logic-programming style:

- `runify` turns unbound variables into generators over a finite range
- each `&` threads the continuation through another constraint
- the arithmetic equality is the final boolean guard, automatically lifted into a relation when needed

When used in an `if`, the program asks for one solution. When used in a relational `for`, the program consumes all solutions generated through recursive backtracking.

### Example: database-style relations

The family-tree example in Test/chap19c.led uses the same machinery:

```leda
function parentOf(byRef par, kid:string)->relation;
begin
    return fatherOf(par, kid) | motherOf(par, kid);
end;
```

and:

```leda
function sibling(byRef left, right: string)->relation;
var par: string;
begin
    return parentOf(par, left) & parentOf(par, right) & (left <> right);
end;
```

This reads like logic programming because the library functions have given ordinary function composition the meaning of search, continuation, and reversible binding.

### What the C runtime does and does not do

The C implementation supports relations indirectly. It provides the necessary substrate, but not the search semantics itself.

What the C code does provide:

- a built-in `relation` type descriptor in initialCreation
- insertion of `relationAsBoolean` and `booleanAsRelation` at type boundaries
- compilation of `<-` into `Leda_arrow`
- compilation of relational `for` into `Leda_forRelation`
- closures, lexical environments, by-name thunks, by-reference arguments, and normal function calls

What the C code does not provide:

- a native unification engine
- a separate relation VM
- a dedicated backtracking stack
- implicit search over multiple clauses at the interpreter level

Instead, search is encoded in the library using closures and reversible assignment.

### Summary

Leda relations are implemented as continuation-passing functions of type:

```text
function(relation) -> boolean
```

Backtracking is expressed by ordinary closure execution plus explicit save/restore in `Leda_arrow`. Conjunction, disjunction, boolean conversion, and relational iteration are all defined in the library on top of that representation. The result is a logic-programming style embedded inside the ordinary function and closure machinery of the language.

## How Functions Are Implemented

Functions in Leda are implemented in two stages:

1. the parser and semantic actions construct a nested symbol table, function type, and statement body
2. the interpreter turns function values into closure objects and executes them with activation records

There is no separate code-generation pass producing bytecode. A function body is stored directly as a linked `statementRecord` list.

### Named function declarations

The main grammar path is in Src/gram.y:

- `functionname` calls `addFunctionSymbol`
- `functionHead` calls `addFunctionArguments`
- `functiondeclaration` completes the body by attaching `genBody(syms, body)` to the placeholder statement created earlier

The sequence is important:

1. `addFunctionSymbol` creates a nested `functionTable` scope.
2. It creates a `functionSymbol` in the enclosing scope.
3. It allocates a placeholder `nullStatement` body so references can point to the function before the body is fully parsed.
4. `addFunctionArguments` fills in the function type and installs formal parameters as `argumentSymbol` entries in the nested function scope.
5. The parser then processes nested declarations and the body.
6. `genBody` prepends constant initialization and local-frame allocation to the parsed statement list.
7. The resulting statement chain is stored in `functionSymbol->u.f.code`.

So a named function is stored in the compiler data structures as:

- a `functionSymbol` with a type and storage or method-table location
- a pointer to the first `statementRecord` of the function body
- a nested symbol table describing parameters, locals, constants, and nested declarations

### Function types

Leda function types are built with `newFunctionType` and stored in `typeRecord` objects.

The type contains:

- the formal argument list as a list of `argumentSymbol`-shaped records
- the return type, or `0` for procedure-like functions

Storage modes are part of the formal parameter metadata:

- `byValue`
- `byName`
- `byReference`

Those modes do not just affect type checking. They directly change the AST generated for actual arguments and later change runtime behavior.

### Methods versus ordinary functions

The same machinery is reused for both.

- top-level or nested functions are stored in a scope's regular symbol list and get a normal storage location
- methods are stored in a class symbol table's `methodTable` and get a method-table slot index instead

When a method is overridden, `addFunctionSymbol` detects inherited methods and reuses the inherited entry rather than inserting a brand new unrelated symbol.

At runtime that means:

- ordinary function calls fetch code from a closure or a global/local function value
- method calls fetch code from the receiver's class table using the method-table slot number

### Function expressions

Anonymous functions use `functionExpressionHead` in the grammar, which calls `generateFunctionExpression`.

That helper creates:

- a fresh nested `functionTable`
- a function type based on the parsed parameters and return type

After the body is parsed, the grammar wraps it as a `makeClosure` expression whose:

- `context` field is `getCurrentContext`
- `code` field is the statement list returned by `genBody`
- `resultType` is the defining function type

So anonymous functions are not lowered differently from named ones. They are just expressions that produce closures directly.

### Calling a function

All normal calls become `doFunctionCall` expressions built by `generateFunctionCall`.

That helper does several jobs:

1. verify that the callee has a function type or constructor-like class-definition type
2. verify that actual arguments conform to formal parameter types and passing modes
3. transform actual arguments according to `byValue`, `byName`, or `byReference`
4. build the final `doFunctionCall` AST node

At runtime, `evaluateExpression` handles `doFunctionCall` by:

1. resolving the callee into a code pointer plus lexical context
2. allocating a new activation record of size `length(args) + 4`
3. storing the lexical parent in slot 1 and caller context in slot 2
4. evaluating and storing actual arguments in slots 4 and up
5. switching `currentContext` to the new frame
6. calling `evaluateStatement` on the callee's stored statement list
7. restoring the previous caller context on return

### Constructors as function calls

`generateFunctionCall` also recognizes class-definition values. If the callee's type is `classDefType`, the call becomes a `buildInstance` expression instead of a normal `doFunctionCall`.

So constructor syntax is implemented by reusing the function-call surface syntax while generating a different AST opcode.

### Tail calls

`genReturnStatement` can rewrite a return expression into `tailCall` if `canMakeIntoTailCall` recognizes a simple self-tail-recursive form.

At runtime, `evaluateStatement` handles `tailCall` by constructing a replacement activation record and continuing execution at the callee code pointer instead of returning to the caller first.

This is not a general tail-call optimizer. It is a narrow source-pattern optimization for a specific recursive shape.

## How Closures Are Implemented

Closures are the runtime objects that make lexical scoping, first-class functions, by-name parameters, and relations work.

### Closure creation at compile time

The AST opcode for a closure is `makeClosure`.

`makeClosure` nodes are produced in several places:

- `genFromSymbol` when a function symbol is referenced as a value
- the grammar action for anonymous function expressions
- `genThunk` when by-name arguments need delayed evaluation
- relation helpers built in the standard library, because relation values are ordinary function values

Each `makeClosure` expression stores:

- an expression describing the lexical context to capture
- a `statementRecord *` pointer for the body code
- optionally a source-level function name for diagnostics

### Closure creation at runtime

When `evaluateExpression` sees `makeClosure`, it allocates a collectable object with `gcalloc(2)` and fills:

- slot 1 with the captured lexical context
- slot 2 with the function body pointer cast as `struct ledaValue *`

This is why closure objects appear in the runtime layout table as two-slot objects with context plus code pointer.

No separate executable code block is created. The closure simply points at the already-built linked statement list.

### Lexical capture

The meaning of closure capture is determined by the `context` expression embedded in the `makeClosure` node.

Common cases are:

- `getCurrentContext` for ordinary nested functions and anonymous functions
- a more complex context expression for methods converted into callable values
- a synthetic environment when relation combinators or by-name helpers build nested closures in library code

When the closure is later called, its slot 1 value becomes the new frame's lexical parent in slot 1 of the activation record.

That is how nested functions can see names from surrounding scopes.

### Closures for methods

Methods are slightly different because a method body must run relative to a receiver object.

The compiler uses `makeMethodContext` for field access that resolves to a method. At runtime, `makeMethodContext` allocates a helper object containing:

- slot 1 = receiver object
- slot 2 = method body pointer from the receiver's class table

Then `doFunctionCall` treats that helper as a callee source and builds a normal activation record from it.

So method invocation is closure-like, but the receiver is discovered dynamically through the class table rather than by directly evaluating a stored `makeClosure` node.

### Thunks are closures

By-name parameters are implemented by turning the actual argument expression into a thunk closure with `genThunk`.

That thunk is just a zero-argument closure whose body is:

- a synthetic `returnStatement`
- returning the original expression

At runtime, `evalThunk`:

1. evaluates the thunk expression to get the closure object
2. saves the current context
3. switches `currentContext` to the thunk's captured context
4. executes the thunk body with `evaluateStatement`
5. restores the old context

So Leda's by-name semantics are implemented directly in terms of closures.

### Why closures matter to the language design

Several apparently separate features all depend on the same closure representation:

- nested functions
- anonymous functions
- methods as values
- by-name arguments
- relations and continuation-passing search

The language gets all of those from one runtime mechanism: storing a lexical context plus a statement-list pointer.

## How Primitive Calls Work

Primitive calls are Leda's controlled escape hatch into built-in operations implemented directly in C.

### Source syntax

The grammar recognizes primitives through `CFUNCTIONkw`:

- procedure form: `cfunction Name(args)`
- value form: `cfunction Name(args) -> ReturnType`

The parser routes both forms to `generateCFunctionCall`.

Examples:

```leda
cfunction Leda_string_print(msg)
cfunction Leda_object_at(data, index) -> T
cfunction Leda_stdin_read() -> string
```

### Compile-time representation

`generateCFunctionCall` does not emit an external call instruction. Instead, it:

1. creates a `doSpecialCall` expression node
2. maps the primitive name string to an integer index in `specialFunctionNames`
3. reverses the actual-argument list into source order
4. stores the declared result type on the AST node

If the primitive name is not in the whitelist, compilation fails immediately.

This means primitive dispatch is closed over a fixed table compiled into `interp.c`.

### Runtime dispatch

When `evaluateExpression` sees `doSpecialCall`, it calls `evaluateSpecial(index, args)`.

`evaluateSpecial` is a hand-written dispatcher over the integer index. Each case:

- evaluates argument expressions explicitly
- protects live values on `rootStack` if a later allocation might trigger GC
- performs the primitive operation in C
- wraps the result back into ordinary Leda runtime objects when needed

There is no FFI in the general sense. Primitive calls can only target the names compiled into `specialFunctionNames`.

### Calling convention for primitives

Primitive arguments are still AST expressions. They are not pre-evaluated by a generic call mechanism first. Each primitive case decides:

- how many arguments to evaluate
- in what order to evaluate them
- which intermediate values must be pushed to `rootStack`
- whether the result is a value object or no value at all

This matters because many primitives allocate new runtime objects. Without the temporary `rootStack` pushes, intermediate values could be lost across GC.

### Categories of primitives

The built-ins currently fall into these groups:

- object operations: pointer equality, object allocation, raw slot read/write, defined checks, identity cast
- string operations: compare, print, concat, length, substring, stdin read, string conversion
- integer operations: equality, arithmetic, bitwise operations, comparison, conversion to string and real
- real operations: equality, arithmetic, comparison, conversion to string and integer

The detailed table in the `evaluateSpecial` section lists every index and exact behavior.

### Return values

Primitives return the same kinds of runtime objects that ordinary interpreted code does:

- integer objects from `newIntegerConstant`
- real objects from `newRealConstant`
- string objects from `newStringConstant`
- boolean objects via `trueObject` and `falseObject`
- raw object pointers for indexing and allocation helpers

Procedure-like primitives return `0`. That is why the grammar separates procedure context from value context and why `genExpressionStatement` insists that procedure-style expressions have no result type.

### Primitive calls versus ordinary function calls

Primitive calls look similar at the source level, but they are implemented differently from ordinary calls.

Ordinary function call path:

- compiled to `doFunctionCall`
- callee is a closure, method context, or function value
- runtime allocates an activation record and executes a statement list

Primitive call path:

- compiled to `doSpecialCall`
- callee is an integer index into a C dispatch table
- runtime executes one hand-written C case directly with no interpreted statement body

So primitive calls bypass the normal closure-and-activation-record function path.

### Why primitives exist

Primitives are used for operations that would be awkward, impossible, or too slow to express in ordinary Leda code alone, including:

- raw array/object slot access
- low-level allocation support
- direct console input/output
- efficient string and numeric conversions

They are intentionally narrow. Most of the language semantics, including relations, control flow, and higher-order functions, still run through ordinary interpreted closures rather than primitives.

## Source Files And Functions

## Src/lexer.l

This file is the lexer. It turns characters into parser tokens and also handles include-file stacking.

### openInputFile(char *name)

How it works: Allocates an inputSources record, saves the current buffer, line number, and file name, opens the requested file, creates a flex buffer for it, and switches the lexer to that buffer.

How it is called: Called from main for the initial source file and from doInclude in the parser when an include declaration is reduced.

Major actions: push current source state, open file, reset line numbering for the new file, and switch flex input.

### yywrap(void)

How it works: Cleans up the current flex buffer and pops back to the previous input source when the current file reaches EOF.

How it is called: Flex calls it automatically at end of input.

Major actions: delete current buffer, restore previous file name and line number, free the saved inputSources node, and decide whether lexing is finished.

### newString(char *c)

How it works: Interns strings in a fixed-size table so repeated identifiers and string fragments reuse the same C string pointer.

How it is called: Used by identifier and string-literal token actions and by helper routines that want canonical string names.

Major actions: linear search in the string table, allocate a fresh copy when absent, and return the shared pointer.

### newTextString(char *c)

How it works: Removes the surrounding quotes from a token text buffer and interns the resulting string.

How it is called: Available as a helper for quoted text processing.

Major actions: skip the opening quote, replace the closing quote with a null terminator, and pass the content to newString.

### readLiteralString(void)

How it works: Reads a quoted string literal character by character from the lexer input stream until a closing quote is found.

How it is called: Triggered by the double-quote token rule.

Major actions: collect characters into a buffer, handle escapes such as \n and \t, detect unterminated literals, and return an interned string.

### skipComment(void)

How it works: Consumes characters until a closing brace comment terminator is found.

How it is called: Triggered by the { rule in the lexer.

Major actions: advance through input, count newlines, reject nested comments, and report EOF inside comments.

### Token rules

The lexer recognizes:

- keywords such as begin, class, const, function, if, while, for, include, defined
- identifiers as ID
- integer constants as ICONSTANT
- real constants as RCONSTANT
- string literals as SCONSTANT
- punctuation such as :, ;, ., :=, ,, ->, <-, brackets, and parentheses
- relational operators mapped onto method/function names like less, equals, sameAs
- arithmetic and logical operators mapped onto names like plus, minus, times, and, or

Examples:

- begin ... end produces BEGINkw and ENDkw
- a := b + 1 produces ID ASSIGN ID PLUSop ICONSTANT
- x <- y produces ID LEFTARROW ID
- defined(expr) produces DEFINEDkw LEFTPAREN ... RIGHTPAREN

## Src/gram.y

This file is the parser and the top-level driver. Its grammar actions do semantic work while parsing: symbol insertion, type construction, AST construction, and include handling.

### yyserror(char *pattern, char *name)

How it works: Formats a parser error message with file, line, and current token text, then exits.

How it is called: Called by semantic checks in parser actions and helper routines when they want printf-style formatting.

Major actions: print location, print formatted message, terminate.

### yyerror(char *s)

How it works: Prints a plain error message with file, line, and current token text, then exits.

How it is called: Used throughout lexer, parser, semantic analysis, and runtime code.

Major actions: print location, print message, terminate.

### testInclude(char *name)

How it works: Attempts to open a candidate include path and, on success, hands it to openInputFile.

How it is called: Used only by doInclude.

Major actions: probe a file path, open it if present, and signal success or failure.

### doInclude(char *name)

How it works: Tries to open an include file first as given, then relative to each -I directory supplied on the command line.

How it is called: Triggered by the grammar rule for include "file";.

Major actions: test the raw name, search include directories, and report failure if no path works.

### main(int argc, char **argv)

How it works: Processes command-line options, opens the root source file, initializes the memory system and initial global symbol table, then runs yyparse.

How it is called: Process entry point.

Major actions: handle debugging flags, parse include and memory-size options, create initialCreation globals, and start parsing.

Supported options:

- -df: show function-call tracing
- -ds: show function and statement tracing
- -do: show function, statement, and operator tracing
- -v: print the version string
- -p: parse only, do not execute
- -I dir: add an include directory
- -m n: set dynamic heap size
- -s n: set static heap size

### Grammar structure

The parser's semantic values include:

- strings and operator names
- expressionRecord pointers
- statementRecord pointers and statement-list pairs
- typeRecord pointers
- symbolRecord pointers
- argument/type lists

The start symbol is program.

## Src/lc.c

This file implements list utilities, symbol-table construction, class/function symbol creation, and the bootstrapping of the initial global environment.

### newList(char *v, struct list *ol)

How it works: Allocates a new list cell and prepends it to an existing list.

How it is called: Used everywhere lists are accumulated during parsing and semantic analysis.

Major actions: allocate node, store value, chain next pointer.

### length(struct list *p)

How it works: Counts nodes in a list.

How it is called: Used in type checking, argument checking, and runtime activation-record sizing.

Major actions: iterate and count.

### reverse2(struct list *todo, struct list *done)

How it works: Tail-recursive helper that builds a reversed copy of a list.

How it is called: Used only by reverse.

Major actions: recursively move one value at a time from todo to done.

### reverse(struct list *a)

How it works: Returns a reversed copy of a list.

How it is called: Used when grammar rules naturally build lists backward.

Major actions: delegate to reverse2.

### newSymbolTable(enum tableTypes tt, struct symbolTableRecord *ctx)

How it works: Allocates a symbol table and initializes bookkeeping according to whether it is global, function, or class scope.

How it is called: Used when entering functions, classes, and the initial global scope.

Major actions: set parent scope, initialize symbol storage, initialize function argument offsets or class method-table metadata.

### lookupLocal(struct symbolTableRecord *syms, char *name)

How it works: Searches only the current symbol table, and for class tables also searches the method table.

How it is called: Used by uniqueness checks and by addFunctionSymbol.

Major actions: linear search through local declarations and class methods.

### uniqueName(struct symbolTableRecord *syms, char *name)

How it works: Rejects redefinition of a name in the current scope.

How it is called: Called before inserting variables, constants, and type names.

Major actions: call lookupLocal and raise an error on collision.

### lookupSymbol(struct symbolTableRecord *syms, char *name)

How it works: Walks outward through nested scopes until a symbol is found.

How it is called: Used by parser actions when a type or identifier name must already exist.

Major actions: repeated lookupLocal calls over surroundingContext.

### addNewSymbol(struct symbolTableRecord *syms, struct symbolRecord *s)

How it works: Prepends a symbol to the table's firstSymbol list.

How it is called: Shared insertion helper.

Major actions: wrap the symbol in a list node and link it into the table.

### newSymbolRecord(char *n, enum symbolTypes st)

How it works: Allocates a symbolRecord with a name and symbol kind.

How it is called: Used by all symbol creation routines.

Major actions: allocate the record and set the discriminant.

### addConstant(struct symbolTableRecord *syms, char *name, struct expressionRecord *value)

How it works: Creates a constant symbol whose value expression will later be compiled into initialization assignments by genBody.

How it is called: Called by the constdefinition grammar rule.

Major actions: reject class-scope constants, assign a storage location, wrap the type as constantType, and record the source line.

### addVariable(struct symbolTableRecord *syms, char *name, struct typeRecord *typ)

How it works: Creates a mutable variable symbol with a storage location in the current context block.

How it is called: Used by variable declarations and temporary/reference helpers.

Major actions: enforce uniqueness, assign location, and return the inserted symbol.

### addTypeDeclaration(struct symbolTableRecord *syms, char *name, struct typeRecord *typ)

How it works: Inserts a named type or alias into the current symbol table.

How it is called: Called by typedefinition reductions.

Major actions: enforce uniqueness and insert a typeSymbol.

### newArgument(char *n, struct typeRecord *t, enum forms f)

How it works: Packages a parsed argument declaration before it is turned into a symbol table entry.

How it is called: Used indirectly by buildArgumentList.

Major actions: allocate an argument descriptor with name, type, and passing form.

### buildArgumentList(struct list *id, enum forms af, struct typeRecord *typ, struct list *soFar)

How it works: Converts a parsed identifier list and common type/form into a list of argumentRecord objects.

How it is called: Used by function parameter grammar rules and generic type-argument grammar rules.

Major actions: recurse to preserve left-to-right order, then create one argumentRecord per identifier.

### newClassSymbol(struct symbolTableRecord *syms, struct symbolTableRecord *gsyms, char *name)

How it works: Creates or completes a class definition symbol and creates the nested class symbol table attached to its class type.

How it is called: Triggered by classStart in the grammar.

Major actions: support forward references, allocate the class type if needed, detect duplicate full definitions, and install a fresh class symbol table.

### fillInParent(struct typeRecord *theClass, struct typeRecord *theParent, struct list *typeArgs)

How it works: Connects a class to its parent and copies inherited fields and methods into the new class scope.

How it is called: Used by classheading grammar actions after OF clauses or default object inheritance.

Major actions: resolve qualified parent types, set parent links, copy field layout size, copy inherited methods, and adapt inherited method types through fixResolvedType when generics are involved.

### addFunctionSymbol(struct symbolTableRecord *syms, char *name, struct list *ta)

How it works: Creates a new function symbol and nested function scope, or overrides an inherited class method.

How it is called: Triggered by functionname in the grammar.

Major actions: assign storage or method-table location, create an empty placeholder body, create the function type, install generic type parameters, and insert implicit self for methods.

### enterFunctionArguments(struct symbolTableRecord *syms, struct list *args)

How it works: Turns argumentRecord entries into argumentSymbol records inside the current function scope.

How it is called: Used by addFunctionArguments and generateFunctionExpression.

Major actions: assign argument locations starting at slot 4 and return the corresponding symbol list in source order.

### addFunctionArguments(struct symbolTableRecord *syms, struct list *args, struct typeRecord *rt)

How it works: Completes the defining function type with its return type and formal parameters.

How it is called: Called by functionHead reductions.

Major actions: set return type and call enterFunctionArguments.

### makeInitialClass(struct symbolTableRecord *syms, char *name, struct typeRecord *p)

How it works: Creates one of the built-in root classes and inserts its symbol.

How it is called: Used only by initialCreation.

Major actions: allocate a classDefSymbol, allocate a classType, set its parent, and assign a global slot.

### initialCreation(void)

How it works: Builds the initial global symbol table containing built-in values, classes, and the relation type.

How it is called: Called from main before yyparse.

Major actions: create NIL/true/false variables, create object/Class/boolean/integer/real/string/True/False/Leda_undefined classes, fill their symbol types, and create the function-valued relation type.

## Src/types.c

This file handles semantic checks and type transformations.

### newConstantType(struct typeRecord *b)

How it works: Wraps a base type to mark it constant.

How it is called: Used for constants and for implicit self in methods.

Major actions: allocate a constantType whose baseType is b.

### checkType(struct symbolRecord *s)

How it works: Verifies that a symbol is usable as a type name.

How it is called: Used in type grammar actions.

Major actions: accept typeSymbol and classDefSymbol, reject everything else.

### checkClass(struct typeRecord *t)

How it works: Peels qualified wrappers and returns the underlying class type, or null if the type is not a class.

How it is called: Used during inheritance, field lookup, and interpreter class-table setup.

Major actions: recurse through qualifiedType and classify classType.

### checkQualifications(struct typeRecord *qt, struct list *args)

How it works: Applies actual type parameters to a generic type.

How it is called: Used when parsing T[A, B]-style type applications and array element qualification.

Major actions: verify the base type is qualified, verify arity, verify conformance of each actual argument, and build a resolvedType pairing patterns with replacements.

### fixResolvedType(struct typeRecord *t, struct typeRecord *rt)

How it works: Rewrites a type by replacing unresolved generic parameters with the concrete arguments from a resolvedType.

How it is called: Used when inherited methods or field types pass through a generic instantiation.

Major actions: substitute direct pattern matches or wrap nested results in another resolvedType.

### checkFunction(struct typeRecord *t)

How it works: Returns the underlying functionType, optionally through a resolvedType wrapper.

How it is called: Used before building calls and validating operators.

Major actions: normalize function types.

### argumentNumber(struct typeRecord *t, int n)

How it works: Fetches the nth formal parameter description from a function type, adapting it through generics if necessary.

How it is called: Used by call checking and operator resolution.

Major actions: index into the formal argument list and substitute resolved generic types.

### newTypeRecord(enum typeForms tt)

How it works: Allocates a typeRecord and initializes fields appropriate to its kind.

How it is called: Shared constructor for all type-building code.

Major actions: allocate record, set discriminant, initialize relevant union fields.

### newFunctionType(struct list *args, struct typeRecord *result)

How it works: Builds a functionType record.

How it is called: Used for declared functions, relation type creation, and function expressions.

Major actions: store formal argument list and return type.

### functionTypeConformable(struct typeRecord *a, struct typeRecord *b)

How it works: Checks whether two function types are compatible.

How it is called: Used internally by typeConformable.

Major actions: compare return types, compare argument counts, and compare each argument's passing form and type.

### typeConformable(struct typeRecord *a, struct typeRecord *b)

How it works: Implements the main type-compatibility relation used across parsing and code generation.

How it is called: Used for assignment, call checking, return checking, operator checking, inheritance-related substitutions, and array literal validation.

Major actions: treat NIL as polymorphic, unwrap constant/unresolved/resolved types, handle function compatibility, and walk up class inheritance for class compatibility.

### newQualifiedType(struct symbolTableRecord *syms, struct list *qualifiers, struct typeRecord *t)

How it works: Creates a generic type schema and installs its formal type parameters into the current scope as unresolved type names.

How it is called: Used when parsing class or function declarations with type parameters.

Major actions: validate by-value-only type parameters, create unresolvedType placeholders, insert them into the symbol table, and remember the qualifier list.

### newTypelist(struct typeRecord *t, enum forms stform, struct list *old)

How it works: Packs a type and storage form into a symbol-like list element used for parameter/type argument lists.

How it is called: Used by typelist grammar rules.

Major actions: create an argumentSymbol-shaped record and prepend it.

## Src/gen.c

This file builds executable expression and statement nodes while the grammar reduces.

### newStatement(enum statements st)

How it works: Allocates a statementRecord and stamps it with the current file and line.

How it is called: Used by all statement-generation helpers.

Major actions: allocate, record source location, initialize next.

### genExpressionStatement(struct expressionRecord *e)

How it works: Wraps a void-valued expression as a statement.

How it is called: Used for procedure calls and for loop helpers that compile into void runtime calls.

Major actions: verify that the expression has no result type and build an expressionStatement.

### genAssignmentStatement(struct expressionRecord *left, struct expressionRecord *right)

How it works: Type-checks an assignment and converts it into an expressionStatement containing an assignment expression.

How it is called: Used by assignment grammar rules and several loop/function helpers.

Major actions: reject assignment to constants, check type conformance, and delegate to genAssignment.

### canMakeIntoTailCall(struct expressionRecord *e, struct typeRecord *t)

How it works: Recognizes a narrow form of self tail recursion.

How it is called: Used by genReturnStatement.

Major actions: verify the expression is a function call with exactly one argument, and that the argument is the current function's incoming parameter.

### genReturnStatement(struct symbolTableRecord *syms, struct expressionRecord *e)

How it works: Type-checks and builds a return statement.

How it is called: Used by RETURN grammar rules and by thunk/function-expression construction.

Major actions: verify function context, coerce between relation and boolean when needed, check return-type conformance, and rewrite to tailCall when canMakeIntoTailCall succeeds.

### genConditionalStatement(...)

How it works: Builds the control-flow links for an if statement.

How it is called: Used by if grammar rules.

Major actions: create one conditionalStatement, route true execution through next, route false execution through falsePart, and join both branches at a supplied null statement.

### genWhileStatement(...)

How it works: Builds a while loop by reusing genConditionalStatement and wiring the loop body back to the test.

How it is called: Used by while grammar rules.

Major actions: create a conditional, make the body fall back to the test, and exit through the supplied null statement.

### genBody(struct symbolTableRecord *syms, struct statementRecord *code)

How it works: Prepends constant initialization statements and a locals-allocation statement to a statement list.

How it is called: Used for top-level execution, named functions, and function expressions.

Major actions: choose the base context for constant slots, synthesize assignments for each constSymbol, and insert a makeLocalsStatement sized to the scope.

### newExpression(enum instructions opcode)

How it works: Allocates an expressionRecord, with a special singleton reuse for getCurrentContext.

How it is called: Used by every expression constructor.

Major actions: allocate node, set opcode, optionally cache the current-context node.

### integerConstant(int v)

How it works: Builds an integer literal expression.

How it is called: Used by ICONSTANT grammar reductions and internal helpers.

Major actions: set genIntegerConstant opcode and integer result type.

### stringConstant(char *s)

How it works: Builds a string literal expression.

How it is called: Used by SCONSTANT grammar reductions.

Major actions: set genStringConstant opcode and string result type.

### realConstant(double v)

How it works: Builds a real literal expression.

How it is called: Used by RCONSTANT grammar reductions.

Major actions: set genRealConstant opcode and real result type.

### genOffset(struct expressionRecord *base, int i, struct symbolRecord *s, struct typeRecord *t)

How it works: Creates an expression that reads slot i from a base context or object.

How it is called: Core helper used by identifier resolution and field lookup.

Major actions: store base, slot index, optional symbol name for diagnostics, and result type.

### genFromSymbol(...)

How it works: Converts a symbol table entry into an expression suited to its meaning in the current scope.

How it is called: Used by lookupAddress, lookupField, and operator resolution.

Major actions: map variables/constants to offsets, methods/functions to closures, arguments to offsets or thunk/reference evaluators, and classes to class-definition expressions.

### generateTemporary(struct symbolTableRecord *syms, struct typeRecord *t)

How it works: Creates a hidden local temporary variable expression.

How it is called: Used when a by-reference argument needs a stable assignable location.

Major actions: synthesize a unique name, add a variable, and return its access expression.

### lookupFunction(struct symbolTableRecord *syms, char *name)

How it works: Resolves a name and verifies that it denotes a function.

How it is called: Used by helpers that call known runtime functions such as Leda_arrow or relation conversions.

Major actions: lookupIdentifier and type-check the result.

### makeMethodIntoFunction(struct typeRecord *ct, char *fieldName)

How it works: Turns a class-definition method into a first-class function suitable for constructor/class-object use.

How it is called: Used by lookupField when a field is accessed through a class definition rather than an instance.

Major actions: find the method, build a wrapper closure that adds the receiver argument, and synthesize the corresponding function type.

### lookupField(struct expressionRecord *base, struct typeRecord *t, char *fieldName)

How it works: Resolves field and method access for objects, generic instances, constants, unresolved wrappers, and class definitions.

How it is called: Used by the PERIOD grammar rule and by operator lookup.

Major actions: unwrap type wrappers, search instance fields, search method table, build makeMethodContext for methods, or delegate to makeMethodIntoFunction for class definitions.

### lookupAddress(struct symbolTableRecord *syms, char *name, struct expressionRecord *base)

How it works: Resolves an identifier through nested scopes by threading the correct context-base expression.

How it is called: Used only by lookupIdentifier.

Major actions: search the current scope kind, descend into surrounding scopes through slot 1 context links, and use lookupField for class scopes.

### lookupIdentifier(struct symbolTableRecord *syms, char *name)

How it works: Public identifier resolver.

How it is called: Used by the ID grammar rule and many semantic helpers.

Major actions: call lookupAddress starting from getCurrentContext and report unknown identifiers.

### argumentsCanMatch(struct typeRecord *t, struct list *args)

How it works: Checks whether actual arguments are acceptable for a callable value.

How it is called: Used before generating regular calls and operator calls.

Major actions: normalize to a function type, compare argument count, and enforce by-value, by-name, and by-reference rules.

### genThunk(struct expressionRecord *e)

How it works: Wraps an expression in a closure that returns it later.

How it is called: Used to implement by-name argument passing and boolean-to-relation conversion.

Major actions: build a one-statement closure returning e.

### generateFunctionCall(struct symbolTableRecord *syms, struct expressionRecord *base, struct list *args, int isFun)

How it works: Builds a function-call expression or constructor expression after checking types and adapting arguments.

How it is called: Used for normal function-call syntax, procedure-call syntax, constructor calls, and operator calls.

Major actions: treat class-definition values as constructors, validate arguments, wrap by-name arguments in thunks, convert by-reference arguments into references or temporaries, and produce a doFunctionCall or buildInstance node.

### generateCFunctionCall(char *name, struct list *args, struct typeRecord *rt)

How it works: Maps a whitelisted cfunction name onto an integer special-call index.

How it is called: Used by CFUNCTION grammar rules.

Major actions: look up the name in specialFunctionNames, reverse the argument list, and build a doSpecialCall expression.

### checkBinarySymbol(...)

How it works: Tests whether a symbol name and type can satisfy an operator lookup.

How it is called: Internal helper for lookupBinaryOperator.

Major actions: name match, convert symbol to expression, and verify call compatibility.

### lookupBinaryOperator(...)

How it works: Searches nested scopes for a globally defined function implementing an operator.

How it is called: Used by generateBinaryOperator and generateUnaryOperator when method lookup fails.

Major actions: search local scope, then surrounding scopes via lexical-context base adjustment.

### generateBinaryOperator(struct symbolTableRecord *syms, char *name, struct expressionRecord *left, struct expressionRecord *right)

How it works: Resolves a binary operator either as a method on the left operand or as a free function in scope.

How it is called: Used by expression grammar rules for arithmetic, logic, relations, and generic binary operators.

Major actions: try method dispatch first, then scoped free-function dispatch, then produce a call node.

### generateUnaryOperator(struct symbolTableRecord *syms, char *name, struct expressionRecord *arg)

How it works: Resolves a unary operator as a method or free function.

How it is called: Used for NOT and unary minus.

Major actions: follow the same resolution pattern as generateBinaryOperator, but with zero method arguments or one free-function argument.

### genAssignment(struct expressionRecord *left, struct expressionRecord *right)

How it works: Converts an assignable expression into an assignment AST node.

How it is called: Used by genAssignmentStatement and by by-reference temporary generation.

Major actions: normalize global offsets, turn direct slot references into makeReference nodes, allow assignment through existing evalReference nodes, and reject non-assignable expressions.

### generateLeftArrow(struct symbolTableRecord *syms, struct expressionRecord *left, struct expressionRecord *right)

How it works: Compiles the special <- relation syntax into a call to Leda_arrow.

How it is called: Used by the relationalExpression grammar rule.

Major actions: ensure the left side is reference-like, wrap it as a reference object, and build a call returning relationType.

### generateForRelation(...)

How it works: Compiles a relation-driven for loop into a runtime call to Leda_forRelation.

How it is called: Used by FOR relation grammar rules.

Major actions: validate relation and stop-condition types, wrap the loop body and stop condition in a thunk/closure, and build the helper call.

### booleanCheck(struct symbolTableRecord *syms, struct expressionRecord *e)

How it works: Converts a relation-valued expression into a boolean when a boolean is required.

How it is called: Used by if and while grammar rules.

Major actions: call relationAsBoolean when needed.

### relationCheck(struct symbolTableRecord *syms, struct expressionRecord *e)

How it works: Converts a boolean-valued expression into a relation when a relation is required.

How it is called: Used in return-type adaptation.

Major actions: thunk the boolean expression and call booleanAsRelation.

### generateFunctionExpression(struct symbolTableRecord *syms, struct list *va, struct typeRecord *rt)

How it works: Creates the nested scope and function type for an anonymous function expression.

How it is called: Used by functionExpressionHead in the grammar.

Major actions: allocate a nested function table and install its formal parameters.

### generateArithmeticForStatement(...)

How it works: Compiles a numeric for loop into explicit initialization plus a while loop.

How it is called: Used by FOR x := start TO limit DO ... grammar rules.

Major actions: create a temporary for the upper bound, assign start to the loop variable, build a lessEqual test, build the increment, and wire the while loop around the body.

### generateArrayLiteral(struct symbolTableRecord *syms, struct list *exps)

How it works: Compiles an array literal into a constructor call for the built-in array class.

How it is called: Used by the [expr, expr, ...] basicExpression rule.

Major actions: verify a non-empty homogeneous element list, qualify the array type with the element type, build a helper allocation expression, and build the array instance initializer.

### genPatternMatch(struct symbolTableRecord *syms, struct expressionRecord *base, struct expressionRecord *theclass, struct list *args)

How it works: Builds a patternMatch expression that optionally binds matched fields into local variables.

How it is called: Used by reference IS Class(...) and reference IS Class grammar rules.

Major actions: convert named capture variables into makeReference expressions and mark the result as booleanType.

## Src/interp.c

This file is the evaluator.

### buildClassTable(struct symbolRecord *sym)

How it works: Allocates the runtime method table object for a class and stores method code pointers into the correct slots.

How it is called: Called at the end of each class declaration by the parser.

Major actions: validate that the symbol names a class, allocate a static table, and fill function slots from the class symbol table's methodTable.

### undefCheck(int x, struct ledaValue *arg, char *s)

How it works: Aborts execution if a runtime value is unexpectedly null.

How it is called: Used around field access, calls, indexing, method dispatch, and reference evaluation.

Major actions: print a numbered failure site plus file/line context and exit.

### binaryValue(int i)

How it works: Allocates a small binary ledaValue payload wrapper, used for integers and references.

How it is called: Used by newIntegerConstant and makeReference evaluation.

Major actions: allocate two data slots, mark the value as binary, and store the integer payload.

### newIntegerConstant(int i)

How it works: Returns a runtime integer object, reusing cached small integers 0 through 19 when possible.

How it is called: Used during expression evaluation and interpreter initialization.

Major actions: look in integerTable, build a binary value if needed, and fill class/context slots.

### newRealConstant(float r)

How it works: Allocates a runtime real object.

How it is called: Used by literal evaluation and numeric special functions.

Major actions: allocate a binary object, set realClass and globalContext, and store the floating-point payload.

### realValue(struct ledaValue *d)

How it works: Extracts the floating-point payload from a real object.

How it is called: Used by real arithmetic and comparison special functions.

Major actions: read the fval union member.

### newStringConstant(char *p)

How it works: Wraps a C string pointer in a runtime string object.

How it is called: Used by string literal evaluation and string-producing special functions.

Major actions: allocate object, set stringClass and globalContext, and store the C string pointer.

### evaluateSpecial(int index, struct list *args)

How it works: Implements the built-in cfunction table listed in specialFunctionNames.

How it is called: Called by evaluateExpression when it sees doSpecialCall.

Major actions: evaluate operands, protect temporary roots across allocations, perform primitive operations, and return runtime objects or null for void primitives.

Implemented primitives include:

- object equality and defined checks
- string compare, print, concat, length, substring, stdin read
- integer equality, arithmetic, bitwise operations, comparisons, conversions
- real arithmetic, equality, comparison, conversions
- raw object allocation and indexed access/update

### Primitive summary

The `cfunction` mechanism exposes a fixed whitelist of built-ins. `generateCFunctionCall` maps the source-level name to an index in `specialFunctionNames`, and `evaluateSpecial` dispatches on that index.

| Index | Primitive name | Arguments | Result | Runtime behavior |
| --- | --- | --- | --- | --- |
| 0 | `Leda_object_equals` | any, any | boolean object | Evaluates both arguments and returns `trueObject` if the two runtime pointers are identical, else `falseObject`. |
| 1 | `Leda_string_compare` | string, string | integer object | Calls `strcmp` on `slot 2.sval` and returns the C comparison result as an integer object. |
| 2 | `Leda_string_print` | string | void | Prints the raw string with `printf("%s", ...)` and returns `0`. |
| 3 | `Leda_string_concat` | string, string | string object | Allocates a new C buffer, copies both strings, and wraps the concatenated result as a runtime string. |
| 4 | `Leda_integer_equals` | integer, integer | boolean object | Compares `slot 2.ival` from both operands for equality. |
| 5 | `Leda_integer_plus` | integer, integer | integer object | Adds the integer payloads and wraps the sum. |
| 6 | `Leda_integer_minus` | integer, integer | integer object | Subtracts the second integer payload from the first. |
| 7 | `Leda_integer_times` | integer, integer | integer object | Multiplies the integer payloads. |
| 8 | `Leda_integer_divide` | integer, integer | integer object | Divides the first integer payload by the second using C integer division. |
| 9 | `Leda_integer_asString` | integer | string object | Formats the integer payload with `sprintf("%d", ...)` and returns a new runtime string. |
| 10 | `Leda_integer_less` | integer, integer | boolean object | Returns `trueObject` if the first integer is smaller. |
| 11 | `Leda_integer_or` | integer, integer | integer object | Applies bitwise `|` to the integer payloads. |
| 12 | `Leda_integer_and` | integer, integer | integer object | Applies bitwise `&` to the integer payloads. |
| 13 | `Leda_integer_not` | integer | integer object | Applies bitwise complement `~` to the integer payload. |
| 14 | `Leda_integer_asReal` | integer | real object | Casts the integer payload to float and wraps it as a real. |
| 15 | `Leda_object_allocate` | size, init0, init1, ... | object | Allocates a raw collectable object with `gcalloc(size)` and stores each remaining evaluated argument into successive slots starting at 0. |
| 16 | `Leda_object_at` | object, index | object | Checks both operands for undefined, then returns `base->data[index].oval`. |
| 17 | `Leda_object_atPut` | object, index, value | void | Checks base and index, then writes `value` into `base->data[index].oval` and returns `0`. |
| 18 | `Leda_object_cast` | value | same as input | Performs no conversion; it simply evaluates and returns its argument. |
| 19 | `Leda_string_length` | string | integer object | Returns `strlen(slot 2.sval)`. |
| 20 | `Leda_string_substring` | string, start, length | string object | Copies `length` characters starting at `start` from the source string into a new C buffer and returns a new runtime string. |
| 21 | `Leda_stdin_read` | none | string object or null | Reads one line with `fgets`; returns `0` on EOF, otherwise wraps the line as a string object. |
| 22 | `Leda_object_defined` | any | boolean object | Returns `trueObject` if the evaluated argument is non-null, otherwise `falseObject`. |
| 23 | `Leda_real_asString` | real | string object | Formats the real payload with `sprintf("%g", ...)` and returns a new runtime string. |
| 24 | `Leda_real_plus` | real, real | real object | Adds the two real payloads. |
| 25 | `Leda_real_minus` | real, real | real object | Subtracts the second real payload from the first. |
| 26 | `Leda_real_times` | real, real | real object | Multiplies the real payloads. |
| 27 | `Leda_real_divide` | real, real | real object | Divides the first real payload by the second. |
| 28 | `Leda_real_less` | real, real | boolean object | Returns `trueObject` if the first real payload is smaller. |
| 29 | `Leda_real_asInteger` | real | integer object | Casts the real payload to `int` and wraps it. |
| 30 | `Leda_real_equals` | real, real | boolean object | Uses direct `==` comparison on the real payloads. |

### How primitives work in practice

The implementation style is consistent across most primitives:

- Arguments are still normal AST expressions. `evaluateSpecial` explicitly evaluates each one.
- When a later allocation might trigger GC, the code temporarily pushes live intermediate values onto `rootStack` before evaluating the next argument.
- Primitive results are ordinary runtime objects such as integer, real, string, or boolean objects. There is no separate immediate-value representation.
- “Void” primitives signal no value by returning `0`, which is why `genExpressionStatement` requires a zero result type for procedure-style calls.
- The object indexing primitives operate on raw data slots; they do not impose extra bounds checks or type checks beyond the undefined-value tests visible in `undefCheck`.

Examples:

- `cfunction Leda_integer_plus(a, b) -> integer` evaluates `a` and `b`, extracts `slot 2.ival` from both, adds them, and returns a new integer object.
- `cfunction Leda_string_print(msg)` evaluates `msg`, prints `msg->data[2].sval`, and returns no value.
- `defined(x)` is compiled to special index 22 and simply checks whether the evaluated runtime pointer is non-null.

### evaluateExpression(struct expressionRecord *e)

How it works: Recursively interprets one expression node according to its opcode.

How it is called: Called from evaluateStatement and recursively from itself.

Major actions by opcode:

- getCurrentContext: return the current activation record
- getOffset and getGlobalOffset: read a slot from the current or global context
- makeReference: create an explicit reference object storing base and slot index
- assignment: write through either a direct reference or an explicit reference object
- makeMethodContext: pair a receiver with a method body from its class table
- makeClosure: build a first-class function value from lexical context plus code pointer
- doFunctionCall: resolve code/context, allocate a new activation record, evaluate arguments, and execute the callee body
- evalThunk: switch currentContext to the thunk's lexical context and run its body
- evalReference: dereference a reference object to fetch the target slot
- genIntegerConstant, genStringConstant, genRealConstant: build literal runtime objects
- doSpecialCall: dispatch to evaluateSpecial
- buildInstance: allocate an object instance and initialize fields
- commaOp: evaluate left for side effects, then right for value
- patternMatch: test class ancestry and optionally bind fields into local references

### evaluateStatement(struct statementRecord *st)

How it works: Executes a linked list of statementRecord nodes iteratively.

How it is called: Used for top-level code, function bodies, thunks, and tail calls.

Major actions by statement kind:

- makeLocalsStatement: allocate the locals block and store it in slot 3 of currentContext
- expressionStatement: evaluate a void expression for side effects
- returnStatement: evaluate and return a value immediately
- tailCall: construct a replacement activation record and continue execution at the callee code without returning to the current frame first
- conditionalStatement: evaluate the condition and continue at next or falsePart
- nullStatement: advance without side effects

### fixClassTable(struct symbolRecord *sym, struct ledaValue *classClass)

How it works: Finalizes the runtime layout of a class object after all globals exist.

How it is called: Called from beginInterpreter for every class definition.

Major actions: write the metaclass, enclosing context, class name, method-table size, and parent class table into fixed slots.

### beginInterpreter(struct symbolTableRecord *syms, struct statementRecord *firstStatement)

How it works: Bootstraps runtime state and runs the top-level program.

How it is called: Called by the program grammar rule after successful parsing unless -p was supplied.

Major actions: allocate globalContext, initialize built-in global values, record special built-in classes, cache small integers, finalize all class tables, switch out of initialization mode, set currentContext, and call evaluateStatement on the top-level statement list.

## Src/memory.c

This file implements a semispace copying collector plus a separate static area.

### gcinit(int staticsz, int dynamicsz)

How it works: Allocates the static area and the two dynamic semispaces, then initializes allocation pointers.

How it is called: Called from main before any parsing or execution.

Major actions: allocate heaps, set staticPointer to the top of static memory, set memoryBase and memoryPointer for the active semispace, and record which space is active.

### gc_move(struct ledaValue *ptr)

How it works: Copies one reachable object graph from old space to new space while updating forwarding information.

How it is called: Used only by gcollect.

Major actions: descend through object pointers, allocate a copy in new space, record forwarding pointers in the old object, and then backtrack to patch references.

### gcollect(int sz)

How it works: Performs garbage collection and then reserves space for the requested allocation.

How it is called: Called when gcalloc would underflow the current semispace.

Major actions: swap semispaces, clear the new one, move currentContext, move all globalContext entries, move rootStack entries, and finally reserve the requested object.

### staticAllocate(int sz)

How it works: Allocates from the non-collected static area.

How it is called: Used during initialization for class tables and some bootstrap objects.

Major actions: decrement staticPointer, bounds-check, and stamp the size field.

### gcalloc(int sz)

How it works: Allocates from the current semispace, or triggers gcollect if there is not enough room.

How it is called: Used by the runtime whenever a collectable object is created.

Major actions: reserve sz + 2 slots and initialize the size field.

## Src/test.c

This file is only a small diagnostic program.

### main(void)

How it works: Prints the sizes of a ledaValue pointer, int, and double.

How it is called: Separate test executable source, not part of the main interpreter path.

Major actions: report platform type sizes, historically useful when the runtime depended on equal-sized pointer and payload assumptions.

## Grammar Reference

## Terminals

The lexer returns these terminal categories.

### Keywords

- INCLUDEkw: include declaration. Example: include "std.led";
- DEFINEDkw: defined test. Example: defined(x)
- CONSTkw: starts constant declarations. Example: const pi := 3.14;
- VARkw: starts variable declarations. Example: var x : integer;
- TYPEkw: starts type declarations. Example: type Pair : object;
- CLASSkw: starts a class declaration. Example: class Point;
- FUNCTIONkw: starts a function declaration or anonymous function expression. Example: function add(x : integer) -> integer;
- OFkw: introduces a parent class. Example: class Child of Parent;
- BYNAME and BYREF: parameter-passing modes. Example: function f(byName x : integer);
- BEGINkw and ENDkw: delimit bodies and blocks. Example: begin ... end
- RETURNkw: return from a function. Example: return x + 1
- IFkw, THENkw, ELSEkw: conditional syntax. Example: if cond then s else t
- WHILEkw, DOkw: loop syntax. Example: while cond do stmt
- ISkw: pattern-match operator. Example: value is Node(left, right)
- FORkw and TOkw: for-loop syntax. Example: for i := 1 to 10 do stmt
- CFUNCTIONkw: explicit call to a built-in primitive. Example: cfunction Leda_string_print(msg)

### Operator and literal tokens

- BINARYOP: generic named binary operators returned by the lexer when configured that way
- PLUSop, MINUSop, TIMESop: arithmetic operator categories, carrying method/function names such as plus, minus, times, divide, remainder
- ANDop and ORop: logical operators carrying names and / or
- NOT: unary logical negation
- RELATIONALOP: relational operators carrying names such as less, greaterEqual, equals, sameAs
- LEFTARROW: the <- relation-building operator
- ID: identifier token. Example: total
- ICONSTANT: integer literal. Example: 42
- RCONSTANT: real literal. Example: 3.14
- SCONSTANT: string literal. Example: "hello"

### Punctuation tokens

- COLON: :
- SEMI: ;
- ASSIGN: :=
- COMMA: ,
- ARROW: ->
- PERIOD: .
- LEFTPAREN and RIGHTPAREN: ( and )
- LEFTBRACK and RIGHTBRACK: [ and ]

## Non-terminals

### program

Shape: declarations begin statements end ;

Meaning: whole compilation unit. The semantic action starts execution by calling beginInterpreter on the generated top-level body unless parse-only mode is active.

Example:

```leda
var x : integer;
begin
    x := 1;
end;
```

### declarations and declaration

Meaning: zero or more top-level declarations. A declaration may be const, var, type, function, class, or include.

Example:

```leda
const answer := 42;
var count : integer;
include "std.led";
```

### constdeclarations, constdefinitions, constdefinition

Meaning: introduce named constant expressions. Constants receive storage locations and are initialized by genBody before executable statements run.

Example:

```leda
const
    one := 1;
    two := one + 1;
```

### vardeclarations, vardefinitions, vardefinition, idlist

Meaning: introduce mutable storage locations with explicit types.

Example:

```leda
var x, y : integer;
```

### typedeclarations, typedefinitions, typedefinition

Meaning: bind names to types.

Example:

```leda
type Predicate : function(integer) -> boolean;
```

### type, opttypelist, typelist, storageForm

Meaning: describe plain named types, qualified generic types, and function types. typelist elements also carry byValue, byName, or byReference storage forms.

Examples:

```leda
integer
array[integer]
function(byRef x : integer) -> boolean
```

### functiondeclaration, functionHead, functionname, valueArguments, argumentList, optReturnType, returnType

Meaning: declare a named function and its nested declarations/body.

Example:

```leda
function add(x : integer, y : integer) -> integer;
begin
    return x + y;
end;
```

### classdeclaration, classheading, classQualifications, classStart, typeArguments

Meaning: declare a class, optional type parameters, and optional inheritance.

Examples:

```leda
class Point;
    var x : integer;
end;

class Child of Parent;
end;

class Box[T] of object;
end;
```

### body, statements, nonReturnStatements, statement, nonReturnStatement

Meaning: build linked statement lists. The nonReturn forms exist so relation-driven loops and similar constructs can exclude direct return statements where the runtime model expects continuation.

Covered statement forms:

- assignment
- bare return or return expression
- nested begin/end block
- if/then[/else]
- while/do
- relation-style for loops
- arithmetic for loops
- procedure calls
- empty statement

Examples:

```leda
x := 3;
return;
return x;
if x < 10 then y := y + 1 else y := 0;
while x < 10 do x := x + 1;
for i := 1 to 10 do sum := sum + i;
```

### optexpressionList and expressionList

Meaning: zero or more comma-separated actual arguments.

Example:

```leda
f(x, y + 1, "hi")
```

### expression, andExpression, notExpression, relationalExpression, binaryExpression, plusExpression, timesExpression

Meaning: precedence ladder for expressions.

Operator precedence from low to high in this grammar is:

1. or
2. and
3. not and pattern-match is
4. relational operators and <-
5. generic binary operators
6. + and -
7. * / % and unary minus
8. function application and primary forms

Examples:

```leda
x or y and z
not done
x < y
a <- b
x + y * z
-negatedValue
```

### procedureCall

Meaning: a call used for side effects only. The generated expression must have no result type.

Examples:

```leda
print(msg)
cfunction Leda_string_print(msg)
```

### functionCall

Meaning: a value-producing call, a defined test, or a chained call on a primary expression.

Examples:

```leda
f(x)
defined(obj)
cfunction Leda_integer_plus(a, b) -> integer
makeAdder(1)(2)
```

### basicExpression

Meaning: primary expressions.

Forms covered:

- references
- integer, string, and real literals
- parenthesized expressions
- anonymous function expressions
- qualified type application on a value
- array literals

Examples:

```leda
x
123
"hello"
(1 + 2)
function(x : integer) -> integer; begin return x; end
arrayValue[integer]
[1, 2, 3]
```

### reference

Meaning: assignable or field-access expressions.

Forms covered:

- identifier lookup
- inline local declaration with type annotation
- field access through .

Examples:

```leda
x
item : integer
point.x
node.next.value
```

### functionExpressionHead

Meaning: the header for an anonymous function expression. It creates a temporary nested symbol table before the function body's declarations and statements are parsed.

Example:

```leda
function(x : integer) -> integer;
begin
    return x + 1;
end
```

## Notes And Caveats

- The runtime evaluates AST nodes directly. There is no bytecode stream, no instruction pointer, and no dispatch loop over encoded opcodes stored in a program buffer.
- The names in enum instructions are AST node kinds, not bytecode opcodes.
- Many runtime primitives are implemented through cfunction names mapped to evaluateSpecial indexes.
- The parser performs substantial semantic work while parsing; it is not only syntax recognition.
