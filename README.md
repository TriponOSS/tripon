# Tripon

![ci](https://github.com/TriponOSS/tripon/actions/workflows/ci.yml/badge.svg)
![license MIT](https://img.shields.io/badge/license-MIT-green)
![spec v0](https://img.shields.io/badge/spec-v0.0-blue)
![phase](https://img.shields.io/badge/phase-P4%20native%20backend%20in%20progress-orange)
![backend](https://img.shields.io/badge/codegen-C99%20%2B%20x86%2D64-informational)

**A robust compiled language for low-resource systems — and the most
AI-agent-friendly one in existence.**

JS-shaped skin. C-speed output. Rust's safety spine, minus the learning
curve, macros, and null. Everything the toolchain says is machine-readable,
deterministic, and frozen by tests.

---

## Chapters

- [Why does this exist](#why-does-this-exist)
- [Quick start](#quick-start)
- [The language](#the-language)
  - [Hello Tripon](#hello-tripon)
  - [Types and inference](#types-and-inference)
  - [No null, no undefined](#no-null-no-undefined)
  - [Errors are values](#errors-are-values)
  - [No UB, by construction](#no-ub-by-construction)
  - [No implicit conversions. Ever.](#no-implicit-conversions-ever)
  - [Tiers: pick how much runtime you can afford](#tiers-pick-how-much-runtime-you-can-afford)
  - [Determinism](#determinism)
- [The toolchain: one binary `ralt`](#the-toolchain-one-binary-ralt)
- [Dependencies](#dependencies)
- [Benchmark numbers](#benchmark-numbers)
- [The agent interface](#the-agent-interface)
- [Roadmap](#roadmap)
- [Repo layout](#repo-layout)
- [Contributing](#contributing)
- [FAQ](#faq)
- [License](#license)

---

## Why does this exist

Real constraints produce real languages:

1. **Low-resource systems deserve a good language** — firmware, bootloaders,
   embedded CLIs, tiny servers. Rust is heavy, C is unsafe, JS/TS doesn't
   even compile there. Tripon targets that bandwidth: milliseconds builds,
   KB binaries, zero runtime.

2. **Agents write code all day, so design for that loop.** Agents never
   get tired or bored: they write code, get structured feedback, and iterate.
   So `ralt` treats the compiler–agent interface as a first-class product:
   stable fault codes, spans with free fix hints, one deterministic behavior,
   no ambiguity where an LLM could guess differently from the same input.

3. **The language humans know the shape of**: the brace/semicolon/`let`/`fn`
   surface carries zero new-keyword retraining. Type-safety without exotic
   syntax. One grammar file with no macros, no overloading, no feature soup.

Tripon is compiled to C99 by default; since P4.1 the toolchain also emits
native x86-64 assembly directly (`--backend cc` loops a cc pass, no C step) -
same programs, same traps, byte-parity output. Any C toolchain can consume
the portable path, including tiny compilers like tcc.

---

## Quick start

Requirements: git, any C99 toolchain (gcc/clang, otherwise nothing). Browse:

```sh
git clone https://github.com/TriponOSS/tripon
cd tripon
make                    # makes build/ralt — one static binary, zero deps
make test               # 42 checks, gcc- and clang-green (incl. self-host oracle)
```

Run programs:

```sh
./build/ralt run examples/hello.tp
./build/ralt check examples/fib.tp --json   # the full diagnostic contract
ralt init my-app                            # scaffold ralt.yaml + src/main.tp
./build/ralt run my-app/src/main.tp --backend cc   # native x86-64 path
```

---

## The language

Extension `.tp`. Comments are `//` and `/* */`. Semicolons and braces are
mandatory: a whole language where nothing hides.

### Hello Tripon

```tripon
use tier core;              // tier ceiling, section below

extern "C" { fn puts(p: str) -> i32; }

fn fib(n: i32) -> i32 {
    let a: i32 = 0;
    let b: i32 = 1;
    let i = 0;              // inferred i32
    while i < n {
        let t = a + b;
        a = b;
        b = t;
        i = i + 1;
    }
    return a;
}

fn main() -> i32 {
    print_i32(fib(20));     // 6765
    puts("done");
    return 0;
}
```

### Types and inference

`i8` `i16` `i32` `i64` `u8`...`usize` `isize` `f32` `f64` `bool` `str` `void`.
Signatures are explicit, locals infer, everything is checked before your code
reaches runtime. There is no `var` and there is no dynamic anything: what is
declared is what runs.

### Structs, enums, match, cast

Data lives by value, variants carry payloads, `match` is exhaustive — the
compiler refuses to ship a missing arm (E0206):

```tripon
struct Point { x: i32; y: i32; }

enum Res { Good(i32), Bad(str), Empty }

fn show(r: Res) -> i32 {
    match r {
        Res::Good(v) => { print_i32(v); }
        Res::Bad(b)  => { print_str(b); }
        Res::Empty   => { }
    }
    return 0;
}

fn scale(p: Point) -> Point {
    return Point { x: p.x * 2, y: p.y * 2 };
}
```

Lossy conversions are explicit and boring: `cast<i16>(x)` truncates by
definition (T2), never disappoints silently.

Fixed-size arrays are bounds-checked by construction (E9002), `len(arr)`
compile-time, element math `+=` works, whole-array assignment copies:

```tripon
let a: [5]i32;
a[0] = 10;
a[i] += i;
print_i32(len(a));      // 5, known without running
```

### No null, no undefined

Null-pointer bugs and "undefined is not a function" are unrepresentable.
May-be-missing values are values:

```tripon
let maybe = lookup(key);        // opt<i32>, two variants: some(v), none
match maybe {
    lookup::some(v) => { print_i32(v); }
    lookup::none    => { /* explicit */ }
}
```

Passing `null` somewhere = E0204 at compile time:
```
E0204 no-null: null and undefined do not exist; use opt (T) for may-be-missing values
```

### Errors are values

No exceptions, no unwinding, no stack traces that end in tragedy.

```tripon
let res = parse(s);     // result<T, E> = ok(v) | err(e)
let v = res?;           // ? propagates the error to the caller. forced handling
```

Ignoring a result of type result = compile error E0205. By design.

### No UB, by construction

The no-UB contract is not documentation folklore — the compiler wires it:

| the classic | in Tripon | outcome |
|---|---|---|
| int overflow | traps (exit 101, E9001), or wraps with `-fast` | defined, always |
| `/` and `%` by zero | trap E9003 | defined, always |
| OOB index | bounds-checked, trap E9002 | defined, always |
| uninitialized read | locals are zero-initialized | defined, always |

```
tripon trap E9001 integer-overflow-trap main.tp:7
```

One line, code, file, span. Every trap.

### No implicit conversions. Ever.

`i32 + f64` doesn't silently demote or promote; it stops your build:

```
E0202 type-mismatch: add a cast or fix the operand types; cast() documented in tripon.md
```

Conversions are explicit and honest: `cast<i64>(x)` widens, `cast<i32>(y)`
truncates, `cast<f64>(z)` makes a float. T2 in `spec/tripon.md` — the whole
type-safety contract — is non-negotiable.

### Tiers: pick how much runtime you can afford

Firmware ≠ CLI app ≠ web server. Tripon is additive by design:

| tier | gets | target |
|---|---|---|
| `kernel` | ints, bools, structs, enums, fn, fixed arrays, extern C, `const` | bare metal: no alloc, no runtime |
| `core` | + arena, slices, `result`/`opt`/`?`, `match`, modules | microcontrollers, tiny CLIs (default for scripts) |
| `std` | + ref intimacy, String/Vec/Map, closures, generics | normal apps |
| `full` | + coroutines/async, self-host runtime | everything else |

`use tier std;` declares your file's ceiling. A missing feature on your tier is
hard error E0301 — never "kinda works, probably".
The tier ladder is enforced per-file: a stdlib `use` raises requirements via
the manifest of the `use`d module. And nothing silently degrades.

### Determinism

Same source, byte-identical compiled output, every build, tested by CI
(`tests/determinism.sh`). No random ordering. No "works on my machine" drift.
Compilers are personal only to theirs humans; Tripon's is not.

---

## The toolchain: one binary ralt

| command | what it does |
|---|---|
| `ralt build <file.tp>` [-o out] [-O fast] [-fast] | emit C99 → cc pipeline → executable (or `-O fast` optim passes); `--backend cc` emits x86-64 asm directly |
| `ralt run <file.tp> [args...]` | build + execute in one ms |
| `ralt check <file.tp> [--json]` | full typecheck, human or machine format |
| `ralt emit --c <file.tp>` | stop after codegen, emit the .c |
| `ralt explain <code>` | any error code expanded, e.g. `E0202` |
| `ralt init [name]` | scaffold a full project: `ralt.yaml` + `src/main.tp` + `.gitignore` |
| `ralt install <name\|url\|dir>` | resolve/refresh a dependency |
| `ralt deps` | list collected dep sources |
| `ralt fmt <file.tp>` | canonical layout — agents can show diffs instead of guessing |
| `ralt doc <file.tp> [--json]` | signatures of every `fn` (text or JSON) |
| `ralt tokens <file.tp>` | frozen token-stream oracle — pins the self-host lexer byte-for-byte |
| `ralt lsp` | editor/agent server over stdio |

Typical agent loop (real demo):

```sh
$ ralt check main.tp --json
{"code":"E0202","kind":"error","name":"type-mismatch","file":"main.tp",
 "line":7,"col":11,"len":5,"msg":"i32 vs f64","hint":"add a cast or fix the operand types"}

# agent edits main.tp:7, retries:
$ ralt check main.tp --json
[]
```

Exit codes: `0` clean, `1` errors, `2` usage. Grammar source:
[spec/errors.csv](spec/errors.csv) + [spec/grammar.ebnf](spec/grammar.ebnf).

---

## Dependencies

`ralt.yaml` (cargo-like, minimal):

```yaml
name: myapp
version: 0.1.0
deps:
  tinser:   https://github.com/TriponOSS/tinser    # git url, cached in ~/.ralt/deps
  localmath: ./vendor/localmath                     # or a plain local dir
```

`ralt install tinser` clones and pins into the cache; `ralt deps` lists
collected sources. Deps compile into the same translation unit: cross-package
calls inlined, no link ceremony, no dlopen ever.

---

## Benchmark numbers

One binary, C99, cold cache ([bench/build_bench.sh](bench/build_bench.sh)
reproduces on any box):

| pipeline | time |
|---|---|
| full build (emit + cc + link) | ~333 ms |
| `ralt check` (type gates only) | ~4 ms |
| codegen only (emit .c) | ~5 ms |
| hello binary (x86-64, dynamic) | ~17.7 KB |

Startup ≈ instant (static C body), no optimizer by default: `-O fast` opts
into passes (niche packing for `result<T, E>` = zero overhead) once types
prove them.

---

## The agent interface

What actually makes Tripon the #1 agent-facing language:

1. **Stable fault vocabulary**: `E####` codes are append-only. `E0202` means
   the same thing today and in 10 releases. Machine ids never move.
2. **Exact spans**: every diagnostic carries `{file,line,col,len}` so the only
   correct edit can be computed — not "somewhere in this file".
3. **A free fix hint, in JSON**: each diagnostic carries its healer's hint
   (`spec/errors.csv` row) — agents can act without a doc lookup.
4. **Determinism**: same input → same output → incremental agents work.
5. **Everything in one binary** `ralt`: compiler, fmt, doc, LSP, deps, parser
   — no tool discovery tax.
6. **The language is simple enough to simulate**: LL-ish grammar, no macros,
   no overloading, no implicit anything: an agent can decide what the next
   token must be without a model call.

---

## Roadmap

- [x] **P0 — Spec.** grammar EBNF, language contract, frozen error table
- [x] **P1.0 — Bootstrap compiler in C99.** lex/parse/typecheck/C99 codegen,
      drivers, no-UB traps, builtins
- [x] **P1.1 — Data + control.** structs, enums with payloads, exhaustive
      `match`, `cast<T>`, `opt`/`result`/`?`/no-null, fixed arrays + str
      indexing (E9002/E9004), `*i8` extern-island pointers
- [x] **P2 — Agent tooling.** fmt/doc/lsp/deps; `ralt tokens` oracle; suite
      42 checks, determinism-green
- [~] **P3 — Self-host.** P3.0 lexer + P3.1 parser ports done: both replay
      their bootstrap oracles byte-for-byte (`ralt tokens` 47/47, `ralt tree`
      48/48 incl. the port itself; frozen oracle tests in tests/selfhost.sh).
      Next: typechecker port (check --json oracle), then flag day — "Tripon
      made with Tripon" compiles itself and passes the suite
- [~] **P4 — Native x86-64 backend.** `run` with `--backend cc` (or env
      `TP_BACKEND=cc`) loops assembles the .s directly via cc, no C emitted -
      scalars (P4.1) and floats (P4.2) are at byte-parity with the C backend,
      incl. all no-UB traps E9001-E9004 and `-fast` semantics; P4.1 shared TAC
      + linear-scan allocation. Next: P4.3 aggregates (arrays, structs,
      enums/match, opt/result), P4.4 native test slice
- [ ] Ideation: package cache with content addressing, tier linter

Phase detail lives in [PLAN.md](PLAN.md).

---

## Repo layout

```
spec/        grammar.ebnf  tripon.md  errors.csv   ← the language contract (frozen)
ralt/src/    the bootstrap compiler: 1 new C file per pass, zero deps
selfhost/    Tripon-in-Tripon: self-host sources (lexer + parser ports done)
stdlib/      tier-managed standard library (grows with P1.2)
examples/    runnable .tp programs (hello, fib, report, ...)
tests/       frozen contract: cases + goldens + determinism + deps + selfhost oracle
bench/       builder benchmarks → README numbers
```

## Contributing

- Read [spec/tripon.md](spec/tripon.md) + [grammar.ebnf](spec/grammar.ebnf) first;
  they are the source of truth, not the C code.
- Error codes are append-only: never renumber, never reuse; update
  `spec/errors.csv` + matching JSON goldens in `tests/expected/`.
- Every PR must keep `make test` and `sh tests/determinism.sh` green under
  **gcc AND clang**; CI enforces it on the matrix in
  [.github/workflows/ci.yml](.github/workflows/ci.yml).
- Changed spec semantics? Change the spec first, then the code, then the tests.

## FAQ

**Why still compile to C99 by default?** Portability + free optimization
from real C compilers, and lets tcc-class builders take the output today.
The native x86-64 backend (P4) removes the C step when you don't need it;
both paths are held to the same frozen trap + oracle contracts in CI.

**Is Tripon `undefined behavior anywhere?`** No — see [No UB, by
construction](#no-ub-by-construction).

**Why no `null`?** It's the billion-dollar mistake and agents write it from
ten thousands of docs. `opt<T>` + match + `?` is the cheap sound replacement
in all magnitudes of compile-time checks.

**GC?** None. Arenas for hot paths, explicit ownership for long-lived state;
`kernel` tier has no allocator at all, so malloc can't even exist there.

**Tier system vs Zig comptime?** Comptime mixes evaluation into compile
indeterminably; Tripon frowns on "surprise capabilities". Tiers are declared,
layered, and the availability surface is a checked fact (E0301), not vibes.

## License

MIT — [LICENSE](LICENSE). Repository:
[TriponOSS/tripon](https://github.com/TriponOSS/tripon).
