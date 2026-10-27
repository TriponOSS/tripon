# Tripon (.tp)

![ci](https://github.com/TriponOSS/tripon/actions/workflows/ci.yml/badge.svg)
![license MIT](https://img.shields.io/badge/license-MIT-green)
![spec v0](https://img.shields.io/badge/spec-v0.0-blue)
![phase](https://img.shields.io/badge/phase-P2%20shipped-orange)
![suite](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/wiki/TriponOSS/tripon/suite.json)

Robust, simple compiled language for low-resource systems — and the #1
AI-agent-friendly programming language.

```tripon
use tier core;

extern "C" { fn puts(p: str) -> i32; }

fn fib(n: i32) -> i32 {
    let a: i32 = 0;
    let b: i32 = 1;
    let i = 0;
    while i < n {
        let t = a + b;
        a = b;
        b = t;
        i = i + 1;
    }
    return a;
}

fn main() -> i32 {
    print_i32(fib(20));
    return 0;
}
```

## Why

C gives you UB, JavaScript gives you `{}`, Rust gives you a PhD. Tripon keeps
the safe core and the brace-shaped skin an LLM already knows, then optimizes
the loop where an agent writes/debugs code:

| Agent-need | Tripon answer |
|---|---|
| "what exactly is wrong" | `ralt check --json` → `{code,name,file,line,col,len,msg,hint}`. Codes frozen forever (`E0202` is never renumbered), full table in [`spec/errors.csv`](spec/errors.csv). |
| "what does this program mean" | one grammar file: [`spec/grammar.ebnf`](spec/grammar.ebnf), LL-ish, no macros, no overloading. |
| "same input, same output" | deterministic: same source → byte-identical C, tested every commit. |
| "fix it now, not after a build" | spans + hints direct in the JSON, `ralt explain E0202` expands any code. |
| "compile it all instantly" | `ralt` is one dep-free C99 binary; default build is optimizer-free (ms-brisk) |

## Language pillars

- **Type-safe JS-style syntax** — braces with braces, `let`/`const`, `fn`, mandatory semicolons, `==` only within the same type.
- **No null, no undefined** — `opt<T>` and `result<T, E>` as explicit value types; `?` propagates. Forgetting cases = compile error (E0205), not runtime.
- **No UB** — integer overflow traps E9001 by default (`-fast` = wrap), `/` and `%` by `0` traps E9002, out-of-bounds traps E9003, uninitialized values are `0`.
- **No implicit conversions, forever** — `i32` + `f64` is E0202, not a shrug; keep `cast<T>()`.
- **Explicit tiering** — `use tier kernel|core|std|full` per file: `kernel` is bare-metal-ready (no alloc, no runtime); unsupported features produce hard errors (E0501), never silent degradation.
- **Errors as values** — no exceptions, no unwinding.
- **Fast builds** — single translation-unit C99 out; `-O fast` adds opt passes (niche packing for `result<T,E>`: zero runtime overhead).

## ralt — the one tool

```
ralt build <file.tp> [-o out]  [-O fast] [-fast]
ralt run   <file.tp> [args...]
ralt check <file.tp> [--json]
ralt emit  --c file.tp -o out.c
ralt explain <code>            e.g. E0202
ralt init                      scaffold ./ralt.yaml
ralt install <name|url|dir>    dependency into ~/.ralt/deps
ralt deps                      list collected dep sources
ralt fmt <file.tp>             canonical, agent-diffable layout
ralt doc <file.tp> [--json]
ralt lsp                       editor/agent protocol (over stdio)
```

**Install:** `git clone https://github.com/TriponOSS/tripon && (cd tripon && make)`.
Requires any C99 toolchain; nothing else — `ralt` is self-hosted even for itself.

## Deps

```yaml
name: myapp
version: 0.1.0
deps:
  tinser: https://github.com/TriponOSS/tinser   # git url
  localmath: ./vendor/localmath                 # or local dir
```

`ralt install localmath` → cache and fold together. Deps compile into the same
translation unit, so first-order users pay zero link overhead.

## Status (see [PLAN.md](PLAN.md))

- [x] P0 — spec: grammar EBNF, language contract, error code table
- [x] P1.0 — bootstrap compiler in C99: lex/parse/typecheck/C99-codegen, drivers, deps, no-UB traps
- [x] P2 — agent tooling: fmt, doc, lsp, deps; suite 18/18, determinism-green
- [ ] P3 — self-host: Tripon lexer/parser/checker written in Tripon ("Tripon made in Tripon")
- [ ] P4 — native x86-64 backend, drop C dependence, package cache

## Repo layout

```
spec/        grammar.ebnf, tripon.md, errors.csv — the language contract
ralt/src/    the bootstrap compiler (C99, zero deps)
stdlib/      tier-managed standard library (grows with P1.2)
examples/    runnable .tp samples
tests/       frozen contract suite + determinism + bench
```

Contrib: `make` builds, `make test` runs the frozen suite (must be green under
`gcc` and `clang` together), error codes append-only per `spec/errors.csv`.

MIT. Project view: PRs to [`TriponOSS/tripon`](https://github.com/TriponOSS/tripon).
