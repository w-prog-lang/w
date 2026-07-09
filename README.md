# W (wlang)

**W** is a small, statically-typed systems programming language that compiles to
readable C. `wlangc`, the compiler in this repository, is written from scratch in
C11 with no external dependencies — a classic four-stage pipeline (lexer → parser →
semantic analyzer → C code generator) held together by a bump-pointer arena
allocator.

The language is deliberately minimal and opinionated. A few decisions set its
character:

- **Bindings are constant by default.** `x := 42` is immutable; you opt into
  mutation with `var x := 42`.
- **One looping keyword.** There is no `while` and no `for` — a single `loop`
  keyword covers infinite, condition-only, and three-clause loops.
- **Sized integers, always.** The numeric types are `int8`, `int16`, `int32`,
  `int64`, and `int128`; a literal is inferred to the *smallest signed type that
  fits it*.
- **A distinct function arrow.** Functions are written
  `fn name: ReturnType <- (params) { ... }`.
- **Source-level imports.** `#import <lib.wlang>` merges another W file into the
  program; `#import <lib.h>` passes a C header through to the generated C, so
  W code can call into the C world.

## A taste

```w
struct Point {
    x: int32,
    y: int32
}

fn make_point: Point <- (x: int32, y: int32) {
    var p: Point;
    p.x = x;
    p.y = y;
    return p;
}

fn main: int32 <- () {
    var p := make_point(3, 4);
    var sum := 0;
    loop (var i := 0; i < 10; i += 1) {
        sum += i;
    }
    print(sum);          // 45
    return p.x + p.y;    // process exits with 7
}
```

Compiling this emits a small, human-readable C translation unit that any C
compiler can turn into a native binary.

## Building

The compiler builds with a single `make`. The `Makefile` defaults to `clang`, but
any C11 compiler works — override `CC` if you don't have clang:

```sh
make                 # builds ./build/wlangc with clang
make CC=gcc          # ...or with gcc
```

There are no third-party dependencies.

## Using the compiler

```sh
./build/wlangc <input.wlang> [output.c]
```

`wlangc` reads a `.wlang` source file, then:

1. resolves its `#import`s (W libraries are parsed and merged in),
2. prints the parsed AST to stdout (a debugging dump),
3. runs semantic analysis and prints whether it passed, and
4. emits the generated C — to `output.c` if a second argument is given, otherwise
   to stdout.

To go all the way to a runnable program, hand the generated C to any C compiler:

```sh
./build/wlangc examples/hello.wlang hello.c
cc hello.c -o hello
./hello
```

## Testing

```sh
make test
```

This runs `test/run_tests.sh` against every case in `test/cases/`. Each case pairs
a `.wlang` source with a `.expect` file describing the expected outcome. The runner
understands five expectation formats:

| Format                       | Meaning                                                     |
| ---------------------------- | ----------------------------------------------------------- |
| `exit N`                     | generated C compiles, runs, and exits with status `N`       |
| `stdout`                     | program output must match the sibling `.stdout` file        |
| `codegen_contains SUBSTRING` | generated C must contain `SUBSTRING`                        |
| `parse_fail`                 | compilation must fail during parsing                        |
| `import_fail`                | compilation must fail during import resolution              |
| `sema_fail`                  | compilation must fail during semantic analysis              |

## Project layout

```
src/
  lexer.{h,c}     tokenizer — pointer+length tokens, zero allocation
  ast.{h,c}       tagged-union AST node definitions
  parser.{h,c}    recursive-descent parser
  import.{h,c}    #import resolution — parses and merges W libraries
  sema.{h,c}      scope chains, type inference, const/narrowing checks
  codegen.{h,c}   C transpiler
  util.{h,c}      arena allocator + PtrList dynamic array
  main.c          CLI driver and AST pretty-printer
examples/         sample .wlang programs
test/
  cases/          .wlang sources paired with .expect (and .stdout) files
  run_tests.sh    end-to-end test runner
LANGUAGE.md       the full language guide
```

## Documentation

The complete language reference — types, declarations, control flow, the
semantic rules, the C translation model, and a full grammar — lives in
[`LANGUAGE.md`](LANGUAGE.md).

## Status

The pipeline compiles a working subset of the language end to end: functions,
structs, fixed-size arrays, strings, integer arithmetic, the full set of
comparison, logical, and bitwise operators, `if`/`else if`/`else`, all three
`loop` forms, `break`/`continue`, compound assignment, a variadic `print`
builtin, a C-like `printf` builtin whose format string is checked at compile
time, and `#import` of both W libraries and C headers. All 58 test cases pass.

Known gaps, described in the guide's *Current limitations* section, include the
absence of a floating-point type and the lack of a return-type compatibility
check. Contributions are grounded in the source first: read the relevant stage
before changing it.

## License

See [LICENSE](LICENSE).
