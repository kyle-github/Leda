---
author:
- |
  Timothy A. Budd\
  Oregon State University\
  Corvallis, Oregon\
  USA
title: Leda Language Reference Manual
---

## Table of Contents

- [Leda Grammar](#leda-grammar)
  - [Overall Program Structure](#overall-program-structure)
  - [Declarations](#declarations)
  - [Types](#types)
  - [Function Declarations](#function-declarations)
  - [Class Declarations](#class-declarations)
  - [Statements](#statements)
  - [Expressions](#expressions)
  - [Basic Expression](#basic-expression)
  - [Comments and Whitespace](#comments-and-whitespace)
  - [Identifiers and Keywords](#identifiers-and-keywords)
  - [Constants](#constants)
  - [The classes `equality` and `ordered`](#the-classes-equality-and-ordered)
  - [The class `Class` and the `typeTest` function](#the-class-class-and-the-typetest-function)
  - [The classes `boolean, True and False`](#the-classes-boolean-true-and-false)
  - [The class `real`](#the-class-real)
  - [The class `integer`](#the-class-integer)
  - [The class `string`](#the-class-string)
  - [The class `array`](#the-class-array)
  - [Testing for Definition](#testing-for-definition)
  - [Input and Output](#input-and-output)
  - [Relations](#relations)
- [Unresolved Language Issues in Leda](#unresolved-language-issues-in-leda)
  - [Semantics of Assignment and Parameter Passing](#semantics-of-assignment-and-parameter-passing)
  - [Modularization Facilities](#modularization-facilities)
  - [Restricted Scoping](#restricted-scoping)
  - [Shared and Overridden Data Fields](#shared-and-overridden-data-fields)
  - [The Subscript operator](#the-subscript-operator)
  - [Nested Classes](#nested-classes)
  - [Forward References for Classes or Functions](#forward-references-for-classes-or-functions)
  - [Copies or Clones](#copies-or-clones)
  - [Enumerated Datatypes](#enumerated-datatypes)
  - [User Defined Constructors](#user-defined-constructors)
  - [Metaclasses](#metaclasses)
  - [A Type inferencing system](#a-type-inferencing-system)
- [Restrictions in the Current Implementation](#restrictions-in-the-current-implementation)
  - [Bibliography](#bibliography)

# Leda Grammar

In this chapter we describe the grammar of the language Leda as it is
currently defined. Note that the language has evolved over time, and
this grammar differs slightly from the language used in some of the
earlier papers on Leda, especially Budd 91a and Budd 92.

The grammar will be defined using a combination of BNF and commentary.
Keywords are given in **bold**. Non-keyword lexical items are written in
*italic*. Nonterminals symbols are written in Roman font. The symbol
`ε` is used to represent “nothing”, that is, an empty sequence
of characters.

## Overall Program Structure

|              |          |                              |
|:-------------|---------:|:-----------------------------|
| program      |      ::= | declarations body **;**      |
|              |          |                              |
| declarations |      ::= | `ε`                 |
|              | `|` | declarations declaration     |
|              |          |                              |
| body         |      ::= | **begin** statements **end** |
|              |          |                              |

The overall structure of a program is a sequence consisting of zero or
more declarations, followed by a single compound statement. The compound
statement given in the body is the statement initially executed when the
program is invoked.

Definitions: declaration:
Section [1.2](#declarations),
statements:
Section [1.6](#statements).

## Declarations

|                     |          |                                        |
|:--------------------|---------:|:---------------------------------------|
| declaration         |      ::= | **const** constantDefinitions          |
|                     | `|` | **var** variableDefinitions            |
|                     | `|` | **type** typeDefinitions               |
|                     | `|` | functionDeclaration                    |
|                     | `|` | classDeclaration                       |
|                     |          |                                        |
| constantDefinitions |      ::= | constantDefinition                     |
|                     | `|` | constantDefinitions constantDefinition |
|                     |          |                                        |
| constantDefinition  |      ::= | *identifier* **:=** expression **;**   |
|                     |          |                                        |
| variableDefinitions |      ::= | variableDefinition                     |
|                     | `|` | variableDefinitions variableDefinition |
|                     |          |                                        |
| variableDefinition  |      ::= | identifierList **:** type **;**        |
|                     |          |                                        |
| identifierList      |      ::= | *identifier*                           |
|                     | `|` | identifierList **,** *identifier*      |
|                     |          |                                        |
| typeDefinitions     |      ::= | typeDefinition                         |
|                     | `|` | typeDefinitions typeDefinition         |
|                     |          |                                        |
| typeDefinition      |      ::= | *identifier* **:** type **;**          |
|                     |          |                                        |

Unlike Pascal, in Leda the different forms of declaration can be listed
in any order, and the same type of declaration section may appear more
than once in a given scope (although individual identifiers cannot be
declared more than once, see below).

The expression assigned to a constant identifier need not be resolvable
at compile time. Such values will be calculated in the order they are
given prior to the execution of any statement in the context in which
they are defined.

All declared identifiers (constant, type, variable, function and class
names) must be unique within the scope in which they declared.

Although type declarations are permitted in Leda they tend to be used
far less frequently than in Pascal, since the vast majority of type
declarations are replaced by classes.

Identifiers declared within a function can only be accessed by
statements defined in the function scope.

Identifiers created within a class definition can be accessed outside
the class scope only if qualified by an expression of the appropriate
class type (see
Section [1.8](#basic-expression)).

Definitions: type:
Section [1.3](#types),
functionDeclaration:
Section [1.4](#function-declarations),
classDeclaration:
Section [1.5](#class-declarations),
expression:
Section [1.7](#expressions).

## Types

|  |  |  |
|:---|---:|:---|
| type | ::= | *identifier* |
|  | `|` | *identifier* **[** typeList **]** |
|  | `|` | **function** **(** optionalArguments **)** optionalReturnType |
|  |  |  |
| typeList | ::= | type |
|  | `|` | typeList **,** type |
|  |  |  |
| optionalArguments | ::= | `ε` |
|  | `|` | formalList |
|  |  |  |
| formalList | ::= | storageForm type |
|  | `|` | formalList **,** storageForm type |
|  |  |  |
| storageForm | ::= | `ε` |
|  | `|` | **byRef** |
|  | `|` | **byName** |
|  |  |  |
| optionalReturnType | ::= | `ε` |
|  | `|` | **`->`** type |
|  |  |  |

Types can be divided into class types (which are types generated
automatically by a class definition), resolved types (produced by
binding qualified type parameters on a generic function or class), or
function types.

A type-list used to qualify a generic function or class type must match
in number and compatibility the type arguments given in the declaration
of the underlying type.

The data-type `relation`, used in the back-tracking mechanism, is the
only type predefined by the Leda system (as opposed to being defined in
Leda itself in the standard library). Although a relation can be thought
of as a form of boolean, in fact a relation is more accurately described
as equivalent to the following definition:

```leda
type relation : function (relation)->boolean;

```

That is, a relation is a function which takes as argument another
relation and returns a boolean value. Functions that manipulate
relations are written in Leda and are defined as part of the standard
library. A number of other types are also defined in the standard
library.

## Function Declarations

|  |  |  |
|:---|---:|:---|
| functionDeclaration | ::= | **function** *identifier* typeArguments |
|  |  | valueArguments optionalReturnType **;** |
|  |  | declarations body **;** |
|  |  |  |
| typeArguments | ::= | `ε` |
|  | `|` | **[** argumentList **]** |
|  |  |  |
| valueArguments | ::= | **(** `;` **)** |
|  | `|` | **(** argumentList **)** |
|  |  |  |
| argumentList | ::= | storageForm identifierList **:** type |
|  | `|` | argumentList **,** storageForm identifierList **:** type |
|  |  |  |

A function declaration provides both the function signature (argument
types and return type) and the function body.

The keyword **function** is used to define both functions that return
values and functions that do not return any value (the latter are
sometimes referred to as procedures).

Functions can be made generic by defining type parameters. If type
parameters are omitted then the square brackets are omitted as well.
However, the parenthesis that surround value parameters must be written,
even if no such parameters are used.

Identifiers declared within a function definition can be accessed only
by statements that originate within the scope of the function.

The meaning of operator symbols can be defined by providing a function
which uses the textual-name for the operator symbol. See
Section [1.11](#textual-names-for-operator-symbols)
for a list of the textual-names for operators.

Function names cannot be overloaded, with the exception of functions
that are defining the meaning of operator symbols, which can be
overloaded only at the global scope.

Definitions: declarations, body:
Section [1.1](#overall-program-structure),
identifierList:
Section [1.2](#declarations),
storageForm, type, optionalReturnType:
Section [1.3](#types).

## Class Declarations

|  |  |  |
|:---|---:|:---|
| classDeclaration | ::= | classHeading declarations **end** **;** |
|  |  |  |
| classHeading | ::= | className **;** |
|  | `|` | className **of** *identifier* **;** |
|  | `|` | className **of** *identifier* **[** typeList **]** **;** |
|  |  |  |
| className | ::= | **class** *identifier* typeArguments |
|  |  |  |

Class definitions define a new identifier scope, in a similar manner to
function definitions. In addition, class declarations define the class
name as a new type.

The optional identifier following the keyword `of` must denote a class
name, which is the parent class from which the new class will inherit.
If the parent class is generic (defined using type-parameters) then
type-parameters must be provided in the bracket-surrounded type argument
list in order to fully resolve the generic class parameters.
Furthermore, such parameters must match in type and in number of type
arguments given in the underlying class declaration. Such a type-list
cannot be used on a non-generic class. If no parent class is specified
the class *object* is assumed.

Definitions: typeList:
Section [1.3](#types),
typeArguments:
Section [1.4](#function-declarations).

## Statements

|  |  |  |
|:---|---:|:---|
| statements | ::= | `ε` |
|  | `|` | statements statement **;** |
|  |  |  |
| statement | ::= | reference **:=** expression |
|  | `|` | **return** |
|  | `|` | **return** expression |
|  | `|` | **begin** statements **end** |
|  | `|` | **if** expression **then** statement |
|  | `|` | **if** expression **then** statement **else** statement |
|  | `|` | **while** expression **do** statement |
|  | `|` | **for** expression **do** statement |
|  | `|` | **for** expression **to** expression **do** statement |
|  | `|` | **for** reference **:=** expression **to** expression **do** statement |
|  | `|` | procedureCall |
|  | `|` | `ε` |
|  |  |  |
| procedureCall | ::= | functionCall **(** optionalExpressionList **)** |
|  | `|` | **cfunction** *identifier* **(** optionalExpressionList **)** |
|  |  |  |
| optionalExpressionList | ::= | `ε` |
|  | `|` | expressionList |
|  |  |  |
| expressionList | ::= | expression |
|  | `|` | expressionList **,** expression |
|  |  |  |

The semicolon is used as a statement terminator, not a statement
separator.

In an assignment statement, the expression to the right of the
assignment symbol must have a type that is legally assignment-compatible
with the reference described to the left of the assignment symbol.
Assignment is performed using *pointer semantics*. Following the
assignment statement the value of the reference on the left of the
assignment arrow is exactly the same as the value on the right of the
assignment arrow. If the right side is a simple reference, then changes
made to one variable will be reflected in changes made to the other, and
vice-versa. This situation will remain until one or the other reference
is subsequently reassigned, or until one or the other goes out of scope.

A value is assignment-compatible with a variable if

1.  The type of the value is the same as the type of the variable,

2.  The type of the value is an instance of a subclass for the class
    associated with the variable,

3.  The value is the polymorphic constant `NIL`,

4.  Both the variable and value are function types, and

    1.  The number of arguments and their storage form match

    2.  The type associated with each by-value or by-name parameter in
        the value is type-assignment with the equivalent types in the
        target variable,

    3.  The type associated with each by-reference parameter is the same
        in both cases (a test which can be implemented by determining
        that the values are type-assignable in both directions), and

    4.  The type associated with the returned value is type-assignable
        to the type of the target.

No automatic conversions are done by Leda,[^1] not even the conversion
of integer to real. All such changes must be explicitly specified by the
user. (Mixed mode arithmetic operations are implemented by defining
several forms of the arithmetic operators at the global level).

Parameters which are passed by-name cannot be used as the target of an
assignment, nor can identifiers which are declared as referring to
constant values. Parameters which are passed by-value are treated the
same as local variables which have been initialized with the value of
the actual argument. Note that this is a form of assignment, and thus
the comments relative to the semantics of assignment (above) are true
for by-value parameters as well. This is occasionally a source of
confusion, when an operation that changes the state of a formal
parameter value will be observed to have also changed the state of the
actual parameter value used in a function call.

Return statements are not permitted in the body defined at the global
level. A return statement with a value can only be used in an function
which returns a value of a type to which the expression can be assigned.
Conversely, if a return statement is used within a function that does
not produce a value, then no expression can be used with the return
statement.

Return statements are not permitted within the statement associated with
the relational version of the `for` statement.

The expression used to control execution in an `if` statement or a
`while` loop must be either boolean type, or relational type (which will
be converted into boolean type).

An `else` clause is matched with the closest surrounding `if` statement.

The first expression used in the first and second form of the `for`
statement must be of type relation. In the second form of the `for`
statement the second expression must be type boolean. In the form of the
`for` statement using assignment (the third form of `for` statement),
the reference and all expressions must be type integer.

Parenthesis must be used in a procedure call, even if no actual
arguments are being passed.

The keyword **cfunction** introduces a run-time system call; an
invocation of an underlying operation that is normally not type-checked.
It is the responsibility of the programmer to ensure that no type errors
can be introduced through the use of this mechanism. Normally calls on
**cfunctions** are hidden within other functions. Implementations may
make further restrictions on the use of this mechanism. (For example, in
the Leda Interpreter there is a fixed set of cfunctions, which can only
be modified by recompiling the interpreter).

In a procedure call the actual arguments being passed must match in
number and compatibility the arguments given in the declaration of the
target function.

Definitions: expression, functionCall :
Section [1.7](#expressions),
reference:
Section [1.8](#basic-expression).

## Expressions

|  |  |  |
|:---|---:|:---|
| expression | ::= | andExpression |
|  | `|` | expression *orSymbol* andExpression |
|  |  |  |
| andExpression | ::= | notExpression |
|  | `|` | andExpression *andSymbol* notExpression |
|  |  |  |
| notExpression | ::= | relationalExpression |
|  | `|` | `~` notExpression |
|  | `|` | reference **is** type |
|  | `|` | reference **is** type **(** identifierList **)** |
|  |  |  |
| relationalExpression | ::= | plusExpression |
|  | `|` | plusExpression *relationalOperator* plusExpression |
|  |  |  |
| plusExpression | ::= | timesExpression |
|  | `|` | plusExpression *plusOperator* timesExpression |
|  |  |  |
| timesExpression | ::= | functionCall |
|  | `|` | timesExpression *timesOperator* functionCall |
|  | `|` | *plusOperator* functionCall |
|  |  |  |
| functionCall | ::= | basicExpression |
|  | `|` | functionCall **(** optionalExpressionList **)** |
|  | `|` | **cfunction** *identifier* **(** optionalExpressionList **)** `->` type |
|  |  |  |

The *or* symbol is `|`. The *and* symbol is `&`. The six relational
operators are `<`, `<=`, `=`, `<>`, `>=` and `>`, The two
plus operators are `+` and `-`. The three times operators are `*`,
`/` and `%`, the latter denoting remainder. The **cfunction** keyword
is described in
Section [1.6](#statements).

The **is** keyword introduces a type pattern matching operation. The
expression returns a boolean true value if the expression to the left of
the **is** is an instance or subclass of the type represented by the
identifier to the right of the keyword. The identifiers in the optional
list following the test must match the data fields defined for the type.
If the identifier list is present and the test is successful, the data
fields are copied out of the value into the variables, in effect undoing
the actions of the constructor for the class.

In a function call the actual arguments being passed must match in
number and compatibility the arguments given in the declaration of the
target function. Arguments passed to by-reference parameters need not be
references; if non-reference a temporary variable will be generated, the
value of the parameter will be assigned to the temporary, and the
reference passed to the function will be that of the temporary.

Definitions: type:
Section [1.3](#types),
optionalExpressionList:
Section [1.6](#statements),
basicExpression:
Section [1.8](#basic-expression).

## Basic Expression

|  |  |  |
|:---|---:|:---|
| basicExpression | ::= | reference |
|  | `|` | *constant* |
|  | `|` | **(** expression **)** |
|  | `|` | basicExpression **[** typeList **]** |
|  | `|` | **[** expressionList **]** |
|  | `|` | **function** valueArguments optionalReturnType **;** declarations body |
|  |  |  |
| reference | ::= | *identifier* |
|  | `|` | functionCall **.** *identifier* |
|  |  |  |

Constants can be either string constants, integer constants, or real
(floating-point) constants (see
Section [1.12](#constants)).

A reference must denote a declared identifier (which can be either
constant, variable, type or function). In the first form the identifier
must be accessible in the scope containing the reference, while in the
second form the identifier must be accessible in the scope denoted by
the function call, which must be a class type.

A square bracket surrounding a type-list following a basic expression is
used to describe the resolution of qualified types in a function
expression.

Square brackets surrounding an expression list is used to define an
array literal. All the expressions appearing in the list must have the
same type.

Definitions: declarations, body:
Section [1.1](#overall-program-structure),
typeList:
Section [1.3](#types),
valueArguments, optionalReturnType:
Section [1.4](#function-declarations),
expression, expressionList, functionCall:
Section [1.7](#expressions).

## Comments and Whitespace

Any text between matching curly braces is treated as a comment and will
be ignored by the parser. This is the only legal use for curly brace
characters.

Outside of literal strings, space, tab and newline characters have no
meaning.

## Identifiers and Keywords

Identifiers must begin with a letter or underscore, and consist of an
arbitrary number of letters, digits, or underscore characters.

In addition to the textual names for operator symbols (see
Section [1.11](#textual-names-for-operator-symbols)),
the following tokens are reserved as keywords, and cannot be used to
define identifiers:

<div class="center">

|        |          |       |           |       |
|:-------|:---------|:------|:----------|:------|
| begin  | byName   | byRef | cfunction | class |
| const  | defined  | do    | else      | end   |
| for    | function | if    | include   | of    |
| return | then     | to    | type      | var   |
| while  | is       |       |           |       |

```

## Textual names for Operator Symbols

The meaning of unary and binary operators in any given context is
provided by the implementation of a function with the *textual name* for
the operator symbol. The following table gives the various operators and
their associated textual name.

<div class="center">

<table>
<thead>
<tr>
<th colspan="2" style="text-align: center;"><em>binary
operators</em></th>
</tr>
</thead>
<tbody>
<tr>
<td style="text-align: left;">symbol</td>
<td style="text-align: left;">name</td>
</tr>
<tr>
<td style="text-align: left;"><code>+</code></td>
<td style="text-align: left;">plus</td>
</tr>
<tr>
<td style="text-align: left;"><code>-</code></td>
<td style="text-align: left;">minus</td>
</tr>
<tr>
<td style="text-align: left;"><code>*</code></td>
<td style="text-align: left;">times</td>
</tr>
<tr>
<td style="text-align: left;"><code>/</code></td>
<td style="text-align: left;">divide</td>
</tr>
<tr>
<td style="text-align: left;"><code>%</code> &amp; remainder</td>
<td style="text-align: left;">remainder</td>
</tr>
<tr>
<td style="text-align: left;"><code>&amp;</code></td>
<td style="text-align: left;">and</td>
</tr>
<tr>
<td style="text-align: left;"><code>|</code></td>
<td style="text-align: left;">or</td>
</tr>
<tr>
<td style="text-align: left;"><code>&lt;</code></td>
<td style="text-align: left;">less</td>
</tr>
<tr>
<td style="text-align: left;"><code>&lt;=</code></td>
<td style="text-align: left;">lessEqual</td>
</tr>
<tr>
<td style="text-align: left;"><code>&gt;</code></td>
<td style="text-align: left;">greater</td>
</tr>
<tr>
<td style="text-align: left;"><code>&gt;=</code></td>
<td style="text-align: left;">greaterEqual</td>
</tr>
<tr>
<td style="text-align: left;"><code>==</code></td>
<td style="text-align: left;">sameAs</td>
</tr>
<tr>
<td style="text-align: left;"><code>~=</code></td>
<td style="text-align: left;">notSameAs</td>
</tr>
<tr>
<td style="text-align: left;"><code>=</code></td>
<td style="text-align: left;">equals</td>
</tr>
<tr>
<td style="text-align: left;"><code>&lt;&gt;</code></td>
<td style="text-align: left;">notEquals</td>
</tr>
</tbody>
</table>

<table>
<thead>
<tr>
<th colspan="2" style="text-align: center;"><em>unary
operators</em></th>
</tr>
</thead>
<tbody>
<tr>
<td style="text-align: left;">symbol</td>
<td style="text-align: left;">name</td>
</tr>
<tr>
<td style="text-align: left;"><code>~</code></td>
<td style="text-align: left;">not</td>
</tr>
<tr>
<td style="text-align: left;"><code>-</code></td>
<td style="text-align: left;">negation</td>
</tr>
</tbody>
</table>

```

Functions that define operator symbols are the only function names that
can be overloaded (be associated with more than one function body
defined in the same scope). Such overloading can only take place at the
global scope.

## Constants

There are three types of constants in Leda. These are string constants,
integer constants, and real (or floating-point) constants.

String constants are surrounded by a matching pair of double-quote
marks. String constants cannot span multiple input lines, however the
following escape conventions can be used to represent special
characters:

<div class="center">

| *sequence* | *meaning*    |
|:-----------|:-------------|
| `\n`       | *newline*    |
| `\t`       | *tab*        |
| `\b`       | *backspace*  |
| `\\`       | *backslash*  |
| `\"`       | *quote mark* |

```

There is no separate type for character constants. Strings of length 1
can be used in place of character values.

Integer constants consist of a sequence of digit characters. The
underlying architecture may impose restrictions on the size of integer
constants that can be recognized.

A real (or floating-point) constant consists of a non-empty sequence of
digit characters, followed by a fractional part and/or an exponent part.
A fractional part consists of a decimal point (period) followed by a
non-empty sequence of digit characters. An exponent part consists of the
literal character **E** followed by an optional sign and a non-empty
sequence of digit characters.

# The Standard Library

In this appendix we describe the functions found in the standard Leda
run-time library. All of the functionality provided by this library is
written in Leda itself, and is thus available for modification by the
programmer (although care must be exercised when manipulating some of
the more basic classes, such as `object` or `Class`).

## The class `object`

The class `object` is the root class of all Leda entities, including
functions. It defines two data fields, one of type `Class`, and the
second of type `object`. While used internally, the second field is
currently not used by any user-accessible functions.

Three operations are implemented in class `object`. The first is the
function `asString`, used to convert a value into a string. This is
used, for example, by the `print` function
(Section [2.10](#input-and-output)).
By default objects print simply by printing their class name. This
function is overridden in a number of classes, such as integers, string,
real, and so on.

The second and third functions define the meaning for the operators `==`
and `~=`. The `==` operator is implemented so as to test object
identity between the two argument values; that is, the value returned is
true if the two arguments are exactly the same entity. This operation is
occasionally overridden in subclasses (for example, it is overridden by
integers). Testing for same identity should not be confused with testing
for equality (next section). The `~=` operator is defined to be
simply the inverse of the `==` operation. Thus, `~=` will seldom
need to be redefined even if the operation `==` is overridden.

```leda
class object; var classPtr : Class; pointer describing object context :
object; pointer to context – not used

function asString ()->string; begin return name of our class return
cfunction Leda_object_at(classPtr, 2)->string; end;

function sameAs (arg : object)->boolean; begin true if self and arg are
same object return cfunction Leda_object_equals(self, arg)->boolean;
end;

function notSameAs (arg : object)->boolean; begin inverse of sameAs if
self == arg then return false; return true; end; end;

```

## The classes `equality` and `ordered`

The classes `equality` and `ordered` define protocol for objects which
know how to compare against similar objects for equality, and comparison
using relational operators. Both classes are parameterized by a type
value which is the type associated with the right argument in boolean
operators. When subclassing from these classes the class type for the
subclass is normally used as the type argument as well, so that both
left and right arguments in boolean operations have the same type.

The class `equality` simply provides the meaning for the two operators
`=` and `<>`. The former is normally redefined in subclasses, although
in certain situations the default meaning, which is identity of the two
arguments, is sufficient. The latter operation is defined in terms of
the former, so it will seldom need to be redefined in subclasses.

```leda
class equality [T : equality];

function equals (arg : T)->boolean; begin default is simply pointer
equality return self == arg; end;

function notEquals (arg : T)->boolean; begin use = which can be
redefined if self = arg then return false; return true; end; end;

```

The class `ordered` provides meaning for the remaining four relational
operators (`<`, `<=`, `>=`, and `>`). These are defined in terms
of each other, so a subclass need only redefine the meaning of the
less-than operator to gain access to the remaining three. An additional
function `between` is defined which can be used to determine if a value
is between two limits.

```leda
class ordered [T : ordered] of equality[T];

function less (arg : T)->boolean; begin return false; end;

function lessEqual (arg : T)->boolean; begin return self < arg | self
= arg; end;

function greater (arg : T)->boolean; begin return   (self <= arg);
end;

function greaterEqual (arg : T)->boolean; begin return self > arg |
self = arg; end;

function between (low, high : T)->boolean; begin return low <= self &
self <= high; end; end;

```

## The class `Class` and the `typeTest` function

The class `Class` (the initial upper case letter being used to avoid
conflict with the keyword) is used to define protocol for the object
which is the representative of each class. Classes maintain data fields
containing the class name, size, and parent class.

Class `Class` is a subclass of `ordered`, the relational operators being
used to describe the class/subclass relationship. That is, the value of
`x < y` is true if `x` and `y` are class values and `x` is a
(proper) subclass of `y`. The value of `x <= y` is true if either
`x` is the same class as `y` or `x < y`.

There are three functions implemented in this class. The first simply
overrides the printing function inherited from class `object`, so that
class values respond with their name. The second provides implementation
for the relational less-than operator. Since the default meaning
(namely, identity) of the equality testing operator is used, this then
implicitly defines all six relational operators. The third function
takes as argument a value, and returns true if the the value is an
instance of the class, or an instance of a subclass of the class.

```leda
class Class of ordered[Class]; var name : string; size : integer;
parent : Class;

function asString ()->string; begin return name; end;

function less (arg : Class)->boolean; begin if self == arg then equal,
not less return false; if parent == arg then return true; return parent
<> self & parent < arg; end;

function isInstance (val : object)->boolean; begin return val.classPtr
<= self; end; end;

```

The latter operation is used in the implementation of the function
`typeTest`, which can be used to not only determine if a value is or is
not an instance of a specific class, but to conver the value into a form
so that it can be assigned to an instance of a class. (Normally the
class in question is a subclass of the declared type for a variable).
The `typeTest` function either returns the undefined value `NIL`, which
can be assigned to any variable, or it returns the first argument value.
The former is used to indicate the value is not an instance of the class
in question, while the latter indicates it is. The condition of the
result can then be tested using the predicate `defined`.

```leda
function typeTest [T : object] (val : object, aClass : Class)->T;
begin if aClass.isInstance(val) then return cfunction
Leda_object_cast(val)->T; return NIL; end;

```

An example of the use of `typeTest` is found in the function which
overrides the meaning of the `==` operator in the class `integer`. The
argument is declared as simply being an instance of class `object`,
whereas the function wishes to define an alternative meaning if the
actual value is an instance of class `integer`. See
section [2.6](#the-class-integer).

## The classes `boolean, True and False`

The class `boolean` defines the meaning of the unary operator `~` and
the binary operators `|` and `&`. In the binary operators the argument
is passed by-name, and thus evaluation of the right argument value will
be delayed until it is actually used in the body of the function.

```leda
class boolean of equality[boolean];

function not ()->boolean; begin if self then return false; return true;
end;

function or (byName arg : boolean)->boolean; begin if either are true,
return true if self then return true; if arg then return true; return
false; end;

function and (byName arg : boolean)->boolean; begin if both are true,
return true if self then if arg then return true; return false; end;
end;

```

In actual practice the implementations defined in class `boolean` will
never be executed, as they are all overridden in the pair of classes
`True` and `False`, which are used to create the global constants `true`
and `false`, respectively. Each class redefines the conversion to string
function (inherited from class `object`), and the operations defined in
class `boolean`. The effect of these changes is to avoid the evaluation
of the right-hand argument to binary operators when the result can be
determined solely by the left argument. Such an evaluation scheme is
sometimes referred to as *short circuit evaluation*.

```leda
class True of boolean;

function asString ()->string; begin return "true"; end;

function not ()->boolean; begin return false; end;

function or (byName arg : boolean)->boolean; begin return true; end;

function and (byName arg : boolean)->boolean; begin return arg; end;
end;

class False of boolean;

function asString ()->string; begin return "false"; end;

function not ()->boolean; begin return true; end;

function or (byName arg : boolean)->boolean; begin return arg; end;

function and (byName arg : boolean)->boolean; begin return false; end;
end;

```

## The class `real`

The class `real` defines the protocol for real numbers. Reals are a
subclass of class `ordered`, and thus can be used with relational
operators. The inherited functions `asString`, `equals` and `less` are
all overridden with appropriate definitions. Reals define the arithmetic
operators `+`, `-`, `*` and `/`, but not the remainder operator
`%`. The remaining two functions implement unary negation and the
conversion of real values to integer via truncation.

```leda
class real of ordered[real];

function asString ()->string; begin return cfunction
Leda_real_asString(self)->string; end;

function equals (arg : real)->boolean; begin return cfunction
Leda_real_equals(self, arg)->boolean; end;

function less (arg : real)->boolean; begin return cfunction
Leda_real_less(self, arg)->boolean; end;

function plus (arg : real)->real; begin return cfunction
Leda_real_plus(self, arg)->real; end;

function minus (arg : real)->real; begin return cfunction
Leda_real_minus(self, arg)->real; end;

function times (arg : real)->real; begin return cfunction
Leda_real_times(self, arg)->real; end;

function divide (arg : real)->real; begin return cfunction
Leda_real_divide(self, arg)->real; end;

function negation ()->real; begin return 0.0 - self; end;

function asInteger()->integer; begin return cfunction
Leda_real_asInteger(self)->integer; end; end;

```

There are (almost) no automatic coercions or conversions performed by
Leda.[^2] Mixed mode arithmetic is implemented by overloaded versions of
the arithmetic operators, defined in the global scope:

```leda
function plus (left : integer, right : real)->real; begin return
left.asReal() + right; end;

function plus (left : real, right : integer)->real; begin return left +
right.asReal(); end;

```

## The class `integer`

The class `integer` defines protocol for the manipulation of integer
objects. The underlying computer architecture may place limitations on
the size of integers that can be held in an instance of this class. (The
creation of infinite precision integers is, however, an interesting and
useful programming exercise. Since the standard library is written in
Leda itself such values can even be easily integrated into the rest of
the system).

The class `integer` inherits from class `ordered`, and redefines the
relational operators, as well as the conversion to string function
inherited from class `object`. The identity testing operator `==` is
redefined in class `integer` so that two integer values with the same
magnitude will test equal. Doing so requires testing the right argument,
which is declared simply as an instance of class `object`.

The binary operators `|` and `&` are defined so as to mean bit-wise
boolean operations, as in the unary operator `~`, which produces a
result by inverting all bits in the argument.

The arithmetic operators `+`, `-`, `*`, `/` and `%` are given
their expected meaning, the latter being defined as the remainder which
remains after the left argument is divided by the right.

The final two operations implemented in the class perform unary negation
and the conversion of integer values into real numbers.

```leda
class integer of ordered[integer];

function asString ()->string; begin return cfunction
Leda_integer_asString(self)->string; end;

function sameAs (arg : object)->boolean; var argInt : integer; begin if
arg is an integer, use int compare argInt := typeTest[integer](arg,
integer); if defined(argInt) then return self = argInt; not equal to
anything but integer return false; end;

function equals (arg : integer)->boolean; begin return cfunction
Leda_integer_equals(self, arg)->boolean; end;

function less (arg : integer)->boolean; begin return cfunction
Leda_integer_less(self, arg)->boolean; end;

function or (arg : integer)->integer; begin return cfunction
Leda_integer_or(self, arg)->integer; end;

function and (arg : integer)->integer; begin return cfunction
Leda_integer_and(self, arg)->integer; end;

function not ()->integer; begin return cfunction
Leda_integer_not(self)->integer; end;

function plus (arg : integer)->integer; begin return cfunction
Leda_integer_plus(self, arg)->integer; end;

function minus (arg : integer)->integer; begin return cfunction
Leda_integer_minus(self, arg)->integer; end;

function times (arg : integer)->integer; begin return cfunction
Leda_integer_times(self, arg)->integer; end;

function divide (arg : integer)->integer; begin return cfunction
Leda_integer_divide(self, arg)->integer; end;

function remainder (arg : integer)->integer; var result : integer;
begin result := self - (self / arg) \* arg; if result < 0 then result
:= result + arg; return result; end;

function asReal ()->real; begin return cfunction
Leda_integer_asReal(self)->real; end;

function negation ()->integer; begin return 0 - self; end; end;

```

## The class `string`

The class `string` implements operations used in the manipulation of
text values. Strings are a subclass of ordered, and define the ordering
operations as lexicographic (dictionary-style) sequencing. The plus
operation is used to concatenate two string values together – the second
argument is converted into a string using the function `asString` if it
is not already a string. The function `length` is used to determine the
number of characters in a string. The function `subString` is used to
extract a subportion of a string; the two arguments represent the
starting location and length of the subtext. If the first argument is
negative it is interpreted as an index from the right side. The function
`index` takes a second string as argument and returns the position in
the receiver at which the argument string first appears, returning `NIL`
if the argument does not appear in the string at all.

```leda
class string of ordered[string];

function asString ()->string; begin return self; end;

function equals (arg : string)->boolean; begin return 0 = cfunction
Leda_string_compare(self, arg)->integer; end;

function less (arg : string)->boolean; begin return 0 > cfunction
Leda_string_compare(self, arg)->integer; end;

function plus (arg : object)->string; begin return cfunction
Leda_string_concat(self, arg.asString())->string; end;

function print (); begin cfunction Leda_string_print(self); end;

function length ()->integer; begin return cfunction
Leda_string_length(self)->integer; end;

function subString (start, len : integer)->string; const selfLength :=
length(); begin start := start if len < 0 then len := 0; if start + len
> selfLength then len := selfLength - start; return cfunction
Leda_string_substring(self, start, len)->string; end;

function index (pattern : string)->integer; const patternLength :=
pattern.length(); var position : integer; begin for position := 0 to
length() do if pattern = subString(position, patternLength) then return
position; return NIL; end;

end;

```

## The class `array`

The `array` data structure is the only collection data type defined in
the standard library. An array is a fixed-length indexed collection of
elements having the same type. The function `size` returns the number of
elements held in the array. Elements are placed into an array using the
function `atPut`, and extracted from the array using the function `at`.
The relation `items` is used to iterate over the values held in an
array.

New arrays can be generated using the function `newArray`. The user must
supply both the lower and upper index values for the array. A literal
array is generated by surrounding a list of similarly-typed values with
square brackets. In literal arrays index values always begin at zero.

```leda
class array [T : object] of equality[array]; var lowerBound :
integer; higherBound : integer; data : object;

function size ()->integer; begin return number of elements in
collection return 1 + (higherBound - lowerBound); end;

function at (index : integer)->T; begin if index.between(lowerBound,
higherBound) then return cfunction Leda_object_at(data, index -
lowerBound)->T else return NIL; end;

function atPut (index : integer, newVal : T); begin if
index.between(lowerBound, higherBound) then cfunction
Leda_object_atPut(data, index - lowerBound, newVal); end;

function items (byRef val : T)->relation; var index : integer; begin
return (lowerBound <= higherBound) & integerRange(lowerBound,
higherBound, 1, index) & val <- at(index); end;

function asString ()->string; var result : string; v : T; begin result
:= "["; for items(v) do result := result + v + " "; return result +
"]"; end; end;

function newArray [T : object] (lb, hb : integer)->array[T]; begin
if (hb > lb) then return array[T](lb, hb, cfunction
Leda_object_allocate((hb-lb)+1)->object) else return NIL; end;

```

## Testing for Definition

The function `defined` can be used to test whether a value is defined.
It does this by simply comparing against the value `NIL`.

```leda
function defined (arg : object)->boolean; begin return   cfunction
Leda_object_equals(NIL, arg)->boolean; end;

```

For efficiency reasons some implementations may elect to implement this
function as a built-in operation rather than as Leda code in the
standard library. The only noticable difference (other than efficiency)
would be that `defined` then becomes a keyword, and can not be used in
any other context.

## Input and Output

Output to the standard output device is performed using the function
`print`. The argument to this function is declared as object, and thus
can be any value. It is converted into a string using the function
`asString` (see
Section [2.1](#the-class-object)),
and the resulting string is then displayed. If the argument is undefined
a literal value is displayed.

```leda
function print (arg : object); begin if defined(arg) then convert to
string, then print arg.asString().print() else "(undefined)".print();
end;

```

Input is performed line by line. The function `readLine` reads a single
line from the standard input, assigning it to a by-reference parameter.
A boolean true value is returned if input is successful, while a boolean
false value is returned on end of input.

```leda
function readLine (byRef line : string)->boolean; begin line :=
cfunction Leda_stdin_read()->string; return defined(line); end;

```

## Relations

From the programmers point of view, relations are defined using the
relational assignment operator (`<-`), conjunctions and disjunctions,
and boolean functions. The relational assignment operator is implemented
using the following function:

```leda
function Leda_arrow (byRef left : object, right : object)->relation;
begin return function(future : relation)->boolean; var save : object;
begin save the current value of the left argument save := left; then
change it to the right left := right; try the task to be done if
future(trueRelation) then worked, report success return true; did not
work, restore the old value left := save; return false; end; end;

```

Conjunction of two relations is implemented by the following:

```leda
function and (left : relation, byName right : relation)->relation;
begin conjunction of two relations – do one after the other return
function(future : relation)->boolean; begin return left(function(f :
relation)->boolean; begin return right(future); end); end; end;

```

The backtracking mechanism is found in the implementation of the
disjunction operator:

```leda
function or (left : relation, byName right : relation)->relation; begin
disjunction of two relations – do backtracking return function(future :
relation)->boolean; begin if left(future) then return true; return
right(future); end; end;

```

Conversions between relations and booleans are performed using a pair of
functions:

```leda
function booleanAsRelation (byName x : boolean)->relation; begin
convert boolean into a relation return function(future :
relation)->boolean; begin return x & future(trueRelation); end; end;

function relationAsBoolean (future : relation)->boolean; begin convert
relation into a boolean return future(trueRelation); end;

```

The function `unify` is used as a simple approximation to Prolog-style
unification (more complex unification techniques can also be written in
Leda, but are generally not necessary). It attempts to make the left
argument the same as the right, either because they are both defined and
equal, or by assigning one the value of the other.

```leda
function unify [T : equality] (byRef left : T, right : T)->relation;
begin if defined(left) then return left = right else return left <-
right; end;

```

Finally, the function `integerRange` defines a simple relation which
will generate values from a range of integers. The starting and ending
values and the step amount must be specified. The ending value must be
reached exactly, or an infinite regression will result.

```leda
function integerRange (low, high, step : integer, byRef ident :
integer)->relation; begin return ident <- low | (low <> high) &
integerRange(low + step, high, step, ident); end;

```

# Unresolved Language Issues in Leda

Although the Leda language as described in this manual is now reasonably
mature, there remain a few issues that are frequently sources of error
or frustration, and are likely targets for future changes in the
language (or in the next generation multiparadigm language). In this
chapter we will describe the most critical of these issues. (In
addition, the current Leda implementation makes a number of limiting
assumptions, thus supporting only a subset of the language defined in
Chapter [1](#leda-grammar).
This, however, is simply an issue of implementation, and not language
design.)

In reading the following, keep in mind that an overriding design
objective has been to keep the language as simple as possible, but no
simpler. Many issues could be addressed by introducing new *mechanisms*,
and one must always weight the cost (both conceptual and practical)
against the benefits.

## Semantics of Assignment and Parameter Passing

In several places we have described the particular semantics of
assignment and parameter passing in Leda. There seems little chance that
the semantics of assignment will be changed, but there is some
possibility that the technique used in parameter passing can be
modified, particularly if a mechanism for cloning values is introduced
(see
Section [3.9](#copies-or-clones)).
The principle problem with current parameter passing mechanism is that
it does not prevent pass-by-value parameters from being modified from
within a called procedure. There have been various proposals made to
address this difficulty:

- Make a clone or copy of a value before passing as a by-value
  parameter. (Requires a general cloning facility).

- Make by-value parameters constant, and forbid operations on constant
  values. (Currently, operations on constant values are not forbidden,
  only assignment to constants). This seems too restrictive, as many
  existing programs would then cease to work.

- Somehow characterize which functions are constant preserving and which
  are not, and only allow constant preserving functions to be performed
  on by-value parameters. It is not at all clear, however, how to
  discover which functions preserve constant values, and which do not.

## Modularization Facilities

Currently a Leda program is simply one large file. There are “include”
facilities, of course, so that data structures can be moved from one
application to another, but no mechanism for separate compilation.

## Restricted Scoping

Currently all variables declared within a class scope are accessible
using the dot operator. Good style dictates that such values should
never be modified outside the class functions, but the language does not
enforce this restriction. A proposed change would be to include C++
style modifiers for declarations which would separate those values which
were purely local to a class from those which are permitted to be
modified outside the class.

## Shared and Overridden Data Fields

In an earlier version of Leda it was possible to declare data fields
which were shared by all instances of a class. It was also possible to
override data fields declared in parent classes, in much the same way
that methods are overridden. Both features were little used, and due to
difficulties in syntax or implementation, both have been dropped from
the current version of the language.

## The Subscript operator

It is conventional in programming languages, going back to the days of
Fortran and ALGOL-60, to use braces of some sort to indicate array
subscripts. Should Leda follow this tradition?

- Arrays are currently only weakly part of the “language”, if by this
  one means the syntax recognized by the parser. Instead, arrays are
  simply a data structure that happens to be implemented in the standard
  library. (This is not entirely true, since array literals require a
  special syntax, and thus the parser does need to know about the
  “Array” data type in order to give a type to these literals).

- Is it right to complicate the syntax of the language (by adding array
  expressions) for just a single data structure? Of course, one could
  say that this is not really just for arrays – onE could add
  productions like:

  ```leda
  basicExpression: basicExpression LEFTBRACKET expression-list
  RIGHTBRACKET

  </div>

  and say this is simply a shorthand for the function call

  ```leda
  expression.subscript(arguments);

  </div>

  where the number of expressions must match the number of arguments
  provided for whatever the subscript operator is found in the class of
  the expression. (This would permit users to define their own
  array-like things, such as matrices or dictionaries).

- Presuming one did do something like the previous, what should the
  return type be for the subscript operator? Note that the semantics of
  subscripts are different depending upon whether they appear as the
  target of an assignment or as an expression. There seems to be two
  choices – either have separate operators for these two operations
  (similar to the “at” and “atPut” functions now in use in Leda,
  admittedly copied from Smalltalk), or introduce some mechanism for
  explicitly dealing with references as values (as in C++). Neither of
  these seems particularly appealing.

## Overloaded operators and/or functions

Currently there is only a minimal amount of overloading of function
names permitted in Leda. Multiple copies of operator definitions (plus,
times, “and” and “or” and the like) are permitted at the global scope,
and nowhere else. Types of overloading that are *not* permitted include:

- overloading of other function names at the global scope

- overloading of operators in local scopes (either within classes or
  within functions)

- overloading of other function names in local scopes

The argument for outlawing each of these is the same. That is, that
currently if `bar` is a function in scope `foo`, the expression
`foo.bar` has a single well defined and unambiguous meaning. This is a
value of type function (technically, a closure), and can be assigned to
a variable, passed as an argument, and so on. In fact, all procedure
calls, such as:

```leda
foo.bar(x, y, z)

```

are actually treated in two steps. First, the expression foo.bar is
evaluated, resulting in a closure value. Then this closure is invoked as
a function. Introducing any of the overloadings that are currently not
permitted would destroy this property.

(It is sometimes argued that at least operators should be permitted to
be overloaded in class scopes, since operators are never invoked without
arguments, but this is not true; several places in the book I refer to
expression such as “integer.plus” and expect it to mean the binary
function named plus defined in the class integer. Allowing overloading
would make such an expression ambiguous.)

## Nested Classes

This is probably less an open point, and more simply a bug in the
current language implementation. A glance at the current Leda grammar
will indicate that I had originally intended for classes and functions
to be permitted to be nested arbitrarily – functions within functions,
functions within classes, classes within functions, or classes within
classes. While pondering the question of forward references (see next
section) the following awkward example was discovered:

```leda
class A;

function foo(); begin ... end;

class B of A; ... end;

function bar(); begin ... end; end;

```

That is, within class A a new class B is defined, which subclasses from
A. Now, currently the Leda language has been carefully crafted so that
it can be parsed and compiled in one pass. This being the case, do
instances of B inherit the function foo from class A? what about the
function bar? If an instance of B is assigned to a variable declared as
maintaining an A, can we perform the bar function on the result? There
are a number of technical issues that complicate this (meaning,
subclasses are basically an extension of the parent class, and one can’t
extend something that hasn’t been completely constructed yet). Probably
the most logical thing is to rule out completely the type of construct
shown here.

## Forward References for Classes or Functions

In order versions of the Leda language I separated class definition from
method definition. In the latest revision I have used the BETA idea (in
fact this actually goes all the way back to SIMULA) of just putting
everything together into one big structure. While this greatly
simplified the language description, it did have the disadvantage that
it makes it difficult to write mutually recursive classes.

Consider first the analogy to mutually recursive functions. By mutually
recursive functions I mean that a function A can call function B, which
is some cases can call function A again. Mutually recursive functions
can sometimes (but not always) be handed by nesting, as in

```leda
function A();

function B(); begin ... A(); end;

begin ... B(); ... end;

```

It was the consideration of the analogy to classes that led me to
discover the problem with nested classes described in the previous
section.

Another common way to get around the problem of mutually recursive
functions, when nesting doesn’t work, is to create a variable of type
function, which is later defined:

```leda
var A : function();

function B(); begin ... A(); ... end;

function C(); begin ... B(); ... end;

begin A := C; end;

```

This is a somewhat primitive facility that nevertheless permits
functions to be forward referenced (that is, named before they are
defined). A more general forward reference mechanism for functions could
probably be defined and easily implemented, but I don’t know if the
payoff is worth the effort, because the situation is not that common,
either nesting or the variable trick is usually sufficient, and in any
case neither gives us any hint as to how to handle forward referencing
of mutually recursive classes.

The equivalent to the mutually recursive functions in classes is a class
A which in one of its methods creates and manipulates an instance of
class B, and a class B in which in one of its methods creates and
manipulates an instance of class A. In the old Leda (and in C++, Object
Pascal, and other languages which separate the class definition from the
method definitions) the solution is clear. Give the class definition for
A, followed by the class definition for B, followed by the methods for
A, followed by the methods for B. By the time the methods are seen both
class definitions have been read, so both are known.

## Copies or Clones

A common operation is to make a copy, or clone, of a value. Of course,
it is easy to define a clone method in any *particular* class to do this
operation. What is harder is to try to develop a system-wide function or
method to generalize this.

The most obvious approach would be to have a function defined in class
object, which would thus be inherited by all values. Classes which
wanted to change the meaning of this (such as integers or arrays) could
then easily override the method. But the difficulty with this is the
question – what should be the return type associated with this function?

A similar situation occurs with regards to the problem of “reverse
polymorphism”. That is, taking a value known to be of some superclass,
and trying to discover whether in fact it is an element of a subclass.
In the current Leda this is accomplished in a somewhat clumsy fashion,
through a parameterized function named `typeTest` which is defined as
follows:

```leda
function typeTest [T : object] (val : object, aClass : Class)->T;
begin if aClass.isInstance(val) then return cfunction
Leda_object_cast(val)->T; return NIL; end;

```

What is clumsy about this is that the class name must be both passed as
a type-parameter (which is used by the parser, not during execution) and
as a value parameter (used by the execution of the procedure). But this
seems to be the cost for having the mechanism defined entirely in Leda,
and not handed as a special case by the parser.

But this also means that subclasses can not define their own version of
“typeTest”, as would seemingly be necessary to make an equivalent copy
procedure.

## Enumerated Datatypes

Older versions of the Leda language had enumerated datatypes – differing
from their PASCAL or C cousins only in that they printed using their
symbolic form and could be iterated over using a relation. But the
amount of work needed by the parser and the changes to the grammar and
the run-time system necessitated by the introduction of these features
seems all out of proportion to their utility.

It also seemed to be the case that enumerated datatypes in previous
versions of the language were primitive, and not easily extensible (by
adding new functionality, for example).

In the latest Leda I have found a literal array of strings to be almost
as easy to use, and does not require any new language features.

(A half-way measure might be to introduce symbols, as in Lisp or
Smalltalk. The equivalent to an enumerated datatype would then be a
literal array of symbols, rather than strings).

## User Defined Constructors

In my previous extensive work in Smalltalk and C++ I found that the
large majority of time (perhaps as high as 75%) that constructors were
used, they simply initialized the data fields in the newly generated
object. This is why in Leda this is the default behavior, and doesn’t
require any additional effort on the part of the programmer.
Nevertheless, in the remaining 25% of the time it would be nice to have
more general constructor facilities that could perform actions other
than simply initialization.

I have toyed with a couple of idea. The BETA-like solution would be to
make classes more like functions, as in

```leda
class A(a, b, c : x) of B(a, b); ... begin do initialization here end;

```

This makes the class heading somewhat complicated to look at, (and even
harder to parse – for example the parent field is now an expression, not
a type) but is a nice general mechanism. Unfortunately, it limits one to
a single constructor for every class.

Another possible solution is to permit some sort of special “functions”
to appear within a class, such as is done in C++, where a function with
the same name as the class is treated differently from other functions.
This allows multiple constructors for a single class, but seems in other
respects somewhat less elegant.

## Metaclasses

Are classes objects or not? If the answer is Yes, then what is their
type? This is commonly called metaclass programming, and is somewhat
popular at present. I’ve always had a slight suspicion of metaclass
programming, thinking that the mechanism is far too complex for the
benefits involved. Leda does have class values – see the description of
class object and class Class in the standard library. Every object has a
field of type Class, which is holding the name of the class, its size,
the pointer to its parent class, and methods. (The latter don’t actually
appear in the class description).

Currently only two things are done with this information. The first is
the display of the class name when printing the value – (see “asString”
in class object, which gets the 3rd field in the class object, namely
the name of the class. Note that functions from class Class can’t be
invoked in class object because of the forward referencing problems
alluded to earlier). The second is the loop used in the `typeTest`
function described earlier.

Traditional tasks performed by metaclasses include making new instances
and cloning. The latter is not currently done in Leda, and the former is
done as a built-in operation, not a metaclass task.

## A Type inferencing system

It is somewhat of a pain to be constantly filling in the type arguments
in parameterized functions or classes. It would be nice if the system
could automatically infer this information from a function call, as in
ML or similar languages.

# Restrictions in the Current Implementation

The current implementation is simplified by restricting the language in
a number of ways:

- Classes can only be defined at the global level, not within functions.

- Classes cannot be nested.

- Constants can only be defined in functions, not in classes.

## Bibliography

Knuth73 Timothy A. Budd, “Blending Imperative and Relational
Programming,” *IEEE Software*, 8(1):58–65, January 1991.

Timothy A. Budd, *An Introduction to Object-Oriented Programming*,
Addison-Wesley, Reading, Mass., 1991.

Timothy A. Budd, “Multiparadigm Data Structures in Leda,” *Proceedings
of the 1992 International Conference on Computer Languages*, IEEE
Computer Society Press, Oakland, California, pp 165–173, April 1992.

[^1]: There is one small exception to the rule that Leda performs no
    automatic conversions. Relational values will be automatically
    converted into a boolean value when they are used in the test
    expression in an `if` or `while` statement.

[^2]: The one exception being the conversion of relation values into
    booleans in the expression portion of an `if` or `while` statement.
