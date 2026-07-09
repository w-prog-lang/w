# The W Language Guide

This guide describes the W (wlang) language exactly as implemented by the `wlangc`
compiler in this repository. Everything here is grounded in the source — the
lexer, parser, semantic analyzer, and C code generator — rather than in aspiration.
Where the compiler has a rough edge or an unimplemented corner, this guide says so
in [Current limitations](#current-limitations) instead of pretending otherwise.

W is a small, statically-typed language that transpiles to C. A W program is a set
of top-level import, function, struct, and global-variable declarations; the
compiler type-checks them and emits a C translation unit you can compile with any
C compiler.

## Contents

1. [Design philosophy](#design-philosophy)
2. [A complete program](#a-complete-program)
3. [Lexical structure](#lexical-structure)
4. [Types](#types)
5. [Declarations and bindings](#declarations-and-bindings)
6. [The type system](#the-type-system)
7. [Functions](#functions)
8. [Structs](#structs)
9. [Arrays](#arrays)
10. [Strings](#strings)
11. [Expressions and operators](#expressions-and-operators)
12. [Statements and control flow](#statements-and-control-flow)
13. [The `print` and `printf` builtins](#the-print-and-printf-builtins)
14. [Imports](#imports)
15. [The compilation model](#the-compilation-model)
16. [Using the compiler](#using-the-compiler)
17. [Grammar reference](#grammar-reference)
18. [Current limitations](#current-limitations)

---

## Design philosophy

A few principles run through the whole language:

- **Immutable by default.** A binding introduced without the `var` keyword is
  constant. Mutation is something you ask for, not something you get for free.
- **Explicit, sized integers.** There is no bare `int`. Every integer has a known
  width, and literals are inferred to the smallest signed type that holds them.
- **One way to loop.** A single `loop` keyword expresses infinite loops,
  condition loops, and counting loops. There is no `while` or `for` keyword.
- **Transpile to readable C.** The compiler's output is meant to be legible. Each
  W construct maps to a small, predictable piece of C.
- **The programmer owns runtime behavior.** For example, integer overflow at
  runtime is not the compiler's concern — W emits the arithmetic and lets the
  machine do what C would do.

---

## A complete program

```w
// factorials, a struct, and every loop shape in one file

struct Counter {
    value: int32
}

fn factorial: int64 <- (n: int32) {
    var result := 1;
    loop (var i := 2; i <= n; i += 1) {
        result = result * i;
    }
    return result;
}

fn main: int32 <- () {
    var c: Counter;
    c.value = 5;

    var f := factorial(c.value);   // 120
    print(f);

    return c.value;                // process exits with 5
}
```

The `main` function is compiled to C's `main`, so the process's exit status is
whatever `main` returns.

---

## Lexical structure

The lexer scans raw source into tokens. Tokens carry a pointer into the source and
a length — no substrings are copied.

### Comments

Only single-line comments exist, introduced by `//` and running to the end of the
line. There are no block comments.

```w
// this is a comment
var x := 1;  // trailing comments work too
```

### Identifiers

An identifier starts with a letter or underscore and continues with letters,
digits, or underscores: `[A-Za-z_][A-Za-z0-9_]*`.

### Keywords

The reserved words are:

```
fn   var   return   if   else   loop   struct   break   continue   true   false
```

Note that `print` and `printf` are **not** keywords — they are builtin function
names recognized during analysis and code generation (see
[The `print` and `printf` builtins](#the-print-and-printf-builtins)).

### Numeric literals

A numeric literal is a run of digits, optionally followed by a decimal point and
more digits. A literal without a decimal point is an integer; one with a decimal
point is a float (both digits sides are required — write `0.5`, not `.5`).

```w
0        42        255        1000000        3.14        0.5
```

### String literals

A string literal is text enclosed in double quotes:

```w
"hello"
"line1\nline2"
"a quote: \" and a backslash: \\"
```

Six escape sequences are recognized: `\n`, `\t`, `\r`, `\\`, `\"`, and `\0` —
exactly the ones that mean the same thing in a C string literal, because the
literal's text passes through to the generated C verbatim. Any other escape is
a semantic error. An unterminated string is a lexical error.

### Operators and punctuation

| Category    | Tokens                                            |
| ----------- | ------------------------------------------------- |
| Arithmetic  | `+`  `-`  `*`  `/`  `%`                            |
| Bitwise     | `&`  `|`  `^`  `~`  `<<`  `>>`                     |
| Assignment  | `=`  `:=`  `+=`  `-=`  `*=`  `/=`                  |
| Comparison  | `==`  `!=`  `<`  `>`  `<=`  `>=`                   |
| Logical     | `&&`  `||`  `!`                                    |
| Arrow       | `<-`  (function parameter arrow)                  |
| Punctuation | `(` `)` `{` `}` `[` `]` `,` `;` `:` `.`           |

`:=` is the *define* operator (inferred-type declaration). `<-` is a single token
used in function signatures.

### The import directive

The `#` character introduces exactly one construct: the `#import` directive. The
lexer folds the whole directive — `#import`, optional spaces, and an
angle-bracketed path — into a single token whose text is the raw path between
`<` and `>` (no escape processing, scanned to the closing `>` on the same line).
See [Imports](#imports) for its meaning.

```w
#import <mathlib.wlang>
#import <string.h>
```

---

## Types

W has five families of types: `bool`, sized integers, sized floats, the string
type, and user-defined structs. Arrays are formed from any of these with a
fixed length.

### Integer types

| W type   | C type      | Width               |
| -------- | ----------- | ------------------- |
| `int8`   | `int8_t`    | 8-bit signed        |
| `int16`  | `int16_t`   | 16-bit signed       |
| `int32`  | `int32_t`   | 32-bit signed       |
| `int64`  | `int64_t`   | 64-bit signed       |
| `int128` | `__int128`  | 128-bit signed      |

These form an ordered *rank*: `int8 < int16 < int32 < int64 < int128`. The rank
drives inference, widening, and narrowing checks (see [The type system](#the-type-system)).

### Floating-point types

| W type    | C type   | Width               |
| --------- | -------- | ------------------- |
| `float32` | `float`  | 32-bit IEEE binary  |
| `float64` | `double` | 64-bit IEEE binary  |

Floats extend the rank order above every integer:
`… < int128 < float32 < float64`. So any integer widens into either float type
(precision loss at the wide end is the programmer's business, in keeping with
the design philosophy), `float32` widens into `float64`, and no float ever
narrows back into an integer.

A float *literal* is special-cased: it adapts to whichever float type it is
placed into, so `f: float32 = 3.14` and `d: float64 = 3.14` are both legal.
Only when a literal's type must be materialized on its own — an inferred
`x := 3.14` declaration — does it default to `float64`.

`%`, the bitwise operators, and `~` require integer operands; applying them to
a float is a semantic error.

### The bool type

| W type | C type              |
| ------ | ------------------- |
| `bool` | `bool` (`<stdbool.h>`) |

`bool` has two literals, `true` and `false`. Comparison and logical operators
yield `bool`. It sits *below* `int8` in the rank order, so a `bool` widens into
any integer type, but no integer narrows into a `bool` — `b: bool = 5` is an
error. Conditions accept either a `bool` or any integer expression.

### The string type

| W type   | C type        |
| -------- | ------------- |
| `string` | `const char*` |

A `string` maps to a C string pointer. Indexing a string yields a single byte,
typed as `int8` (see [Strings](#strings)).

### Arrays

An array type is written `T[N]` — an element type followed by a fixed length in
square brackets. `N` must be a numeric literal.

```w
int32[5]      // five 32-bit integers
```

Arrays translate to fixed-size C arrays.

### Structs

A struct is a named aggregate of fields, each field being any type (including
another struct or an array). See [Structs](#structs).

---

## Declarations and bindings

Every local binding is either **constant** or **mutable**, and either
**type-inferred** or **explicitly typed**. That gives four spellings.

```w
name := expr;              // constant, type inferred
var name := expr;          // mutable,  type inferred

name: Type = expr;         // constant, explicit type
var name: Type;            // mutable,  explicit type, may be left uninitialized
var name: Type = expr;     // mutable,  explicit type, with initializer
```

The rule is simple: **a binding is constant unless it is introduced with `var`.**
Assigning to a constant is a semantic error:

```w
x := 10;
x = 20;      // error: assignment to const identifier
```

`:=` requires an initializer and infers the type from it. The `: Type` form gives
the type explicitly; with `var` and an explicit type, the initializer is optional
(the variable is declared but unset, just as in C).

Every statement-level declaration ends with a semicolon.

### Global bindings

All four spellings are also legal at the top level of a file, alongside `fn` and
`struct` declarations. A global is visible to every function regardless of
declaration order, and a local declaration of the same name shadows it.

```w
var counter := 0;      // mutable global
limit := 3;            // constant global

fn bump: int32 <- () {
    counter += 1;
    return counter;
}
```

Two restrictions, both inherited from C's file-scope rules:

- A global's initializer must be a **constant expression** — literals combined
  with operators. Naming another variable, calling a function, or indexing
  anything is a semantic error.
- As with locals, redeclaring an existing global name is an error.

Globals declared by an imported W library are merged into the program exactly
like its functions and structs.

---

## The type system

Semantic analysis walks the AST after parsing. It resolves names, infers types,
and enforces the language's few static rules.

### Type inference

When a declaration uses `:=`, the type is inferred from the initializer. For an
integer literal, the inferred type is the **smallest signed integer type that can
hold the value**:

| Literal range              | Inferred type |
| -------------------------- | ------------- |
| −128 … 127                 | `int8`        |
| −32768 … 32767             | `int16`       |
| −2147483648 … 2147483647   | `int32`       |
| up to 9223372036854775807  | `int64`       |

A literal beyond the `int64` maximum is a semantic error: C has no literal
syntax wide enough for `int128`, so `int128` values must be computed (for
example by shifting) rather than written out.

So `x := 42` is an `int8`, while `y := 255` is an `int16` (255 exceeds the signed
`int8` maximum of 127). A call's inferred type is the callee's declared return
type; a binary expression's type is the *wider* of its two operands.

### Widening and narrowing

Assignments and typed declarations permit **widening** (assigning a narrower value
into a wider slot) but reject **narrowing** (the reverse). Concretely, the value's
type rank must be ≤ the target's rank:

```w
var wide: int64 = 5;     // ok: int8 literal widened into int64
var narrow: int8 = 5000; // error: initializer type too wide for declared type
```

The same rule governs plain assignments, array-element assignments, struct-field
assignments, and function-call arguments. `string` is treated as its own category:
mixing a string with an integer in any of these positions is a type mismatch.

### Constants and reassignment

- A binding without `var` is constant; assigning to it is an error.
- Function parameters are constant inside the function body.
- Redeclaring a name already declared in the *same* scope is an error. Shadowing a
  name from an *enclosing* scope is allowed.

### Scopes

Scopes form a chain. Each function body, block, `if` branch, and `loop` opens a new
scope; name lookup walks outward through parent scopes. A loop's header (its init
clause and induction variable) lives in a scope that encloses the loop body, so the
body can see the loop variable while the variable stays invisible to the code
around the loop.

---

## Functions

A function declaration has the shape:

```
fn NAME: RETURN_TYPE <- (PARAMS) { BODY }
```

The return type comes right after the name (separated by `:`), then the `<-` arrow,
then a parenthesized parameter list, then the body block. Each parameter is written
`name: Type`.

```w
fn add: int128 <- (a: int64, b: int64) {
    return a + b;
}

fn greet: int32 <- () {
    print("hi");
    return 0;
}
```

Calls use ordinary parentheses. A call may be an expression or a statement (a
statement call discards the return value):

```w
var s := add(1, 2);   // call in expression position
greet();              // call as a statement
```

At a call site the analyzer checks that the argument count matches and that each
argument's type is not wider than the corresponding parameter's type. All functions
are visible to one another regardless of declaration order, because the analyzer
registers every function before checking any body.

A `return` expression is checked against the declared return type with the same
rule as assignments: a narrower integer widens into a wider return type, but a
too-wide value, a string/integer mix, or a wrong struct type is an error.

```w
fn shrink: int8 <- (x: int64) {
    return x;   // error: returned value type too wide
}
```

---

## Structs

A struct groups named fields. Fields are separated by commas; each is `name: Type`.

```w
struct Point {
    x: int32,
    y: int32
}
```

Declare a struct value with an explicit type, then assign its fields. Field access
uses dot notation, and structs can nest:

```w
struct Line {
    a: Point,
    b: Point
}

fn main: int32 <- () {
    var l: Line;
    l.a.x = 3;
    l.a.y = 4;
    return l.a.x + l.a.y;   // 7
}
```

Field access is type-checked: accessing a field on a non-struct value, or naming a
field that doesn't exist, is a semantic error. Structs are first-class enough to be
returned from functions and assigned whole:

```w
fn make_point: Point <- (x: int32, y: int32) {
    var p: Point;
    p.x = x;
    p.y = y;
    return p;
}
```

Redeclaring a struct name is an error. Struct types may be referenced before they
are declared in the file, since all structs are registered before any function is
checked.

---

## Arrays

Arrays are fixed-length and declared with `T[N]`:

```w
var arr: int32[5];
arr[0] = 10;
arr[1] = 20;
var i := 2;
arr[i] = arr[0] + arr[1];   // index with any integer expression
return arr[0] + arr[1] + arr[2];
```

Indexing a value that is neither an array nor a string is a semantic error.
Array-element assignment obeys the same widening/narrowing rule as any other
assignment. Arrays can be struct fields and can be passed as parameters:

```w
fn sum3: int32 <- (arr: int32[3]) {
    return arr[0] + arr[1] + arr[2];
}
```

Field and index access chain freely, in either order:

```w
struct Pair { vals: int32[2] }

// ...
p.vals[0] = 3;      // index into a field
p.vals[1] = 4;
return p.vals[0] + p.vals[1];
```

---

## Strings

A `string` is an immutable text value backed by a C `const char*`. Indexing a
string returns one byte as an `int8`:

```w
fn main: int32 <- () {
    var s: string = "hi";
    return s[0];   // 104, the byte value of 'h'
}
```

Because `string` is its own type category, it never mixes with integers in
assignments, declarations, or call arguments — doing so is a type mismatch.
String literals support the C-compatible escape sequences `\n`, `\t`, `\r`,
`\\`, `\"`, and `\0` (see [Lexical structure](#lexical-structure)).

---

## Expressions and operators

Expressions are built from literals, identifiers, calls, field/index accesses, and
the operators below. Precedence runs from lowest to highest as follows (each level
is left-associative except unary, which is right-associative):

| Level | Operators                         | Description               |
| ----- | --------------------------------- | ------------------------- |
| 1     | `||`                              | logical or                |
| 2     | `&&`                              | logical and               |
| 3     | `==` `!=` `<` `>` `<=` `>=`        | comparison                |
| 4     | `+` `-` `|` `^`                   | additive / bitwise or, xor |
| 5     | `*` `/` `%` `&` `<<` `>>`         | multiplicative / bitwise and, shifts |
| 6     | `!` `-` `~` (prefix)              | unary not / negation / complement |
| 7     | `.` `[]` and call `()`            | postfix access / call     |

Parentheses group as usual. Comparisons, `&&`/`||`, and `!` produce a `bool`;
a condition may be a `bool` or any integer expression. A binary arithmetic
expression takes the wider of its operand types.

W places the bitwise operators where Go does, not where C does: `&` and the
shifts bind like `*`, while `|` and `^` bind like `+`, and all of them bind
*tighter* than comparisons. So `x & 4 == 4` means `(x & 4) == 4` — the
common intent — rather than C's surprising `x & (4 == 4)`. The generated C is
fully parenthesized, so C's own precedence never leaks through.

```w
var a := (1 + 2) * 3;      // 9
var ok := a > 5 && a < 20; // bool
var neg := -a;             // unary negation
var not := !false;         // logical not -> bool
var msk := a & 7 | 16;     // (a & 7) | 16
var inv := ~a;             // bitwise complement
```

---

## Statements and control flow

### Declarations and assignments

Covered above: `:=` / `: Type` declarations, plain `=` assignment, and the compound
assignments `+=`, `-=`, `*=`, `/=`. A compound assignment is desugared during
parsing — `x += e` becomes `x = x + e` — so there is no separate compound-assignment
node in the AST.

### `if` / `else if` / `else`

```w
if (n < 0) {
    return -1;
} else if (n == 0) {
    return 0;
} else {
    return 1;
}
```

The condition is a parenthesized expression; each branch is a block. `else if`
chains as many times as you like, with an optional final `else` block.

### Loops

There is one loop keyword with three forms.

**Infinite** — no header at all:

```w
loop {
    // runs forever until `break`
    break;
}
```

**Condition-only** (the "while" shape) — a single parenthesized expression:

```w
loop (i < 10) {
    i += 1;
}
```

**Three-clause** (the "for" shape) — init, condition, and step separated by
semicolons:

```w
var sum := 0;
loop (var i := 0; i < 10; i += 1) {
    sum += i;
}
// sum == 45
```

The init clause may declare a fresh variable (with or without `var`) or assign an
existing one; the step clause is an assignment or compound assignment. The parser
distinguishes the three-clause form from the condition-only form by scanning ahead
for a top-level `;` inside the loop header.

### `break` and `continue`

`break` exits the nearest enclosing loop; `continue` skips to its next iteration.
Using either outside any loop is a semantic error. In three-clause loops,
`continue` correctly runs the step clause, because such loops compile to a real C
`for` (see below).

### `return`

`return` optionally takes an expression and ends the current function:

```w
return;          // no value
return a + b;    // with a value
```

---

## The `print` and `printf` builtins

Both are builtins, not user functions. Since W has no `void` type, a call to
either in expression position has a placeholder `int64` type — but they are
almost always used as statements.

### `print`

`print` takes **one or more** arguments, each of which may be any integer type,
a float, a `bool`, or a `string`. It writes the values back to back — no
separators — followed by a single trailing newline. Calling it with zero
arguments is a semantic error.

```w
print(42);                  // prints: 42
print("hello");             // prints: hello
print("x = ", 7, "!");      // prints: x = 7!
print(1.5);                 // prints: 1.5
```

In the generated C, each argument is dispatched through the `w_print_val`
`_Generic` macro so the right formatting is chosen per C type: strings print
as-is, floats through `%g`, and everything else as an integer (a `bool` prints
as `1` or `0`). A final helper emits the newline. The whole call is one comma
expression, so `print` stays usable in expression position.

### `printf`

`printf` is a C-like formatted print whose format string is **checked at compile
time**. The first argument must be a string *literal* (not a variable); the
remaining arguments must line up one-to-one with the format directives:

| Directive | Matches                  | Meaning                    |
| --------- | ------------------------ | -------------------------- |
| `%d`      | any integer or `bool`    | print the integer          |
| `%f`      | `float32` or `float64`   | print the float            |
| `%s`      | `string`                 | print the string           |
| `%%`      | (consumes no argument)   | a literal `%`              |

```w
printf("%s is %d years old", "Ada", 36);   // Ada is 36 years old
printf("pi is about %f", 3.14);            // pi is about 3.140000
printf("100%%");                            // 100%
```

Semantic analysis rejects: a non-literal format string, an unsupported directive
(anything other than `%d`, `%f`, `%s`, `%%`), a directive/argument type mismatch,
and a directive count that disagrees with the argument count. Unlike `print`,
`printf` appends **no** trailing newline.

In the generated C, the call lowers onto C's own `printf`: every `%d` is rewritten
to `%lld` with its argument cast to `long long` (W integers are up to 128 bits
wide), every `%f` argument is cast to `double`, and `%s` passes through unchanged.

---

## Imports

A file pulls in other code with the `#import` directive. The path is written
between angle brackets, and its extension selects one of two behaviors:

```w
#import <mathlib.wlang>  // a W library: parsed and merged into the program
#import <string.h>      // a C header: passed through as #include <string.h>
```

Any other extension is a parse error. Imports are conventionally written at the
top of the file, but the parser accepts them anywhere a top-level declaration is
legal.

### W libraries (`.wlang`)

A W library is just an ordinary `.wlang` file of `fn`, `struct`, and global
declarations (and possibly further imports). Importing it reads, parses, and
merges its declarations into the importing program, as if everything had been
written in one file; the merged declarations are type-checked together with the
rest of the program.

- Paths are resolved **relative to the directory of the importing file**: a
  library in a subdirectory is imported as `#import <libs/mathlib.wlang>`, while a
  library importing a sibling in its own directory names it directly.
- Each file is merged **at most once** per compilation, however many import
  paths lead to it — the diamond pattern is fine, and a circular import is
  simply cut rather than looping.
- All merged declarations share one flat namespace. Two files that declare the
  same function or struct name collide with the ordinary redefinition error.

### C headers (`.h`)

Importing a `.h` file emits a matching `#include <...>` at the top of the
generated C, deduplicated across the whole import graph (a header requested by
several W libraries is included once).

The compiler does not read C headers, so it cannot know what they declare. When
at least one `.h` import is present, a call to a function name that no W
declaration provides is **assumed to come from a C header**: its arguments are
still checked as expressions, but the call itself is emitted as-is for the C
compiler to resolve. Its result has no known type, so it is exempt from the
category and widening checks wherever it is used directly (returned, assigned,
passed as an argument); once stored into an inferred `:=` declaration, the
variable is typed `int64`. Without any `.h` import, calling an undeclared
function remains a semantic error, exactly as before.

```w
#import <string.h>

fn main: int32 <- () {
    return strlen("hello");   // C's strlen; process exits with 5
}
```

---

## The compilation model

`wlangc` is a transpiler: it lowers a W program to a single C translation unit. The
mapping is direct and predictable.

- **Preamble.** Every output begins with `#include <stdbool.h>`,
  `#include <stdint.h>`, and one `#include <...>` per imported C header, then a
  small `print` support block (`#include <stdio.h>` plus three helpers and the
  `w_print_val` `_Generic` macro).
- **Types.** W integer types map to the `<stdint.h>` fixed-width types (`int8` →
  `int8_t`, and so on); `int128` maps to the compiler builtin `__int128`; `bool`
  maps to C's `bool`; `float32`/`float64` map to `float`/`double`; `string` maps
  to `const char*`.
- **Structs** become C `typedef struct { ... } Name;` definitions, emitted before
  functions.
- **Functions** become ordinary C functions. Every function except `main` is
  forward-declared with a prototype before any definition, so W's any-order
  call rule (and code merged from imported libraries) survives the translation
  to C. A zero-parameter function is emitted with a `(void)` parameter list.
  Array parameters keep their `[N]` bounds.
- **Declarations.** Constant bindings gain a `const` qualifier. An inferred
  declaration is emitted with the type the analyzer computed for it.
- **Loops.** A three-clause loop lowers to a C `for` loop, which is what makes
  `continue` run the step clause. The other two forms lower to C `while` loops
  (an infinite loop becomes `while (1)`).
- **Expressions** are emitted fully parenthesized, so W's precedence is preserved
  in the C output without relying on C's own precedence.

For example, the three-clause loop above compiles to:

```c
int32_t main(void)
{
    int8_t sum = 0;
    for (int8_t i = 0; (i < 10); i = (i + 1))
    {
        sum = (sum + i);
    }
    return sum;
}
```

Because `main` becomes C's `main`, its return value is the process exit status.

---

## Using the compiler

Build the compiler (see the README), then run:

```sh
./build/wlangc <input.wlang> [output.c]
```

The driver prints the parsed AST and a semantic-analysis result line to stdout,
then emits the generated C — to `output.c` if you name one, otherwise to stdout. To
produce a native binary, compile the emitted C with any C compiler:

```sh
./build/wlangc examples/hello.wlang hello.c
cc hello.c -o hello
./hello
echo $?     # the exit status is main's return value
```

If parsing, import resolution, or semantic analysis fails, the compiler reports
the error with a line number and exits without emitting C. An unresolvable
import (a missing file, or a library that fails to parse) is reported as an
`import error`.

---

## Grammar reference

The following EBNF mirrors the recursive-descent parser. Uppercase names are
terminals produced by the lexer (`IDENT`, `NUM`, `STRING`); `?` marks optional
parts, `*` repetition, and `|` alternatives.

```ebnf
program        = { import_decl | func_decl | struct_decl | var_decl } ;

import_decl    = "#import" "<" PATH ">" ;
                 (* the lexer folds the whole directive into one token;
                    PATH is raw text up to the closing '>' and must end
                    in ".wlang" or ".h" *)

func_decl      = "fn" IDENT ":" type "<-" "(" [ params ] ")" block ;
params         = param { "," param } ;
param          = IDENT ":" type ;

struct_decl    = "struct" IDENT "{" [ fields ] "}" ;
fields         = field { "," field } ;
field          = IDENT ":" type ;

type           = IDENT [ "[" NUM "]" ] ;

block          = "{" { stmt } "}" ;

stmt           = var_decl
               | assign_or_call
               | if_stmt
               | loop_stmt
               | return_stmt
               | "break" ";"
               | "continue" ";" ;

var_decl       = [ "var" ] IDENT ":=" expr ";"
               | [ "var" ] IDENT ":" type [ "=" expr ] ";" ;

assign_or_call = IDENT "(" [ args ] ")" ";"
               | IDENT { "." IDENT | "[" expr "]" } "=" expr ";"
               | IDENT "=" expr ";"
               | IDENT ( "+=" | "-=" | "*=" | "/=" ) expr ";" ;

if_stmt        = "if" "(" expr ")" block [ "else" ( if_stmt | block ) ] ;

loop_stmt      = "loop" block
               | "loop" "(" expr ")" block
               | "loop" "(" loop_init ";" expr ";" loop_step ")" block ;
loop_init      = [ "var" ] IDENT ":=" expr
               | [ "var" ] IDENT ":" type [ "=" expr ]
               | IDENT "=" expr ;
loop_step      = IDENT ( "=" | "+=" | "-=" | "*=" | "/=" ) expr ;

return_stmt    = "return" [ expr ] ";" ;

expr           = logical_or ;
logical_or     = logical_and { "||" logical_and } ;
logical_and    = comparison { "&&" comparison } ;
comparison     = additive { ( "==" | "!=" | "<" | ">" | "<=" | ">=" ) additive } ;
additive       = term { ( "+" | "-" | "|" | "^" ) term } ;
term           = unary { ( "*" | "/" | "%" | "&" | "<<" | ">>" ) unary } ;
unary          = ( "!" | "-" | "~" ) unary | primary ;
primary        = primary_base { "." IDENT | "[" expr "]" } ;
primary_base   = IDENT
               | IDENT "(" [ args ] ")"
               | NUM
               | "true" | "false"
               | STRING
               | "(" expr ")" ;
args           = expr { "," expr } ;
```

---

## Current limitations

These are honest gaps in the current implementation, drawn from the source itself.
They are the natural places to contribute.

- **Imports share one flat namespace.** `#import <lib.wlang>` merges the library's
  declarations directly into the program — there is no qualification or
  renaming, so a name collision across files is a redefinition error.
  Diagnostics report line numbers only, not file names, so an error inside an
  imported library is attributed to its line in that library without saying
  which file it came from.
- **C imports are unchecked.** Once any `.h` is imported, a call to an
  undeclared function is assumed to be a C function whose result is exempt
  from type checks — a typo'd function name is then caught only by the C
  compiler.
