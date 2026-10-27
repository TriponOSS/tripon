# Tripon (.tp) — Plan

Status: planning → awaiting go on Phase 1

## Goal
A robust, simple compiled language for low-resource systems, designed so AI agents
can write and fix it at #1 efficiency. Self-hosted end state ("Tripon made with Tripon").

## Core identity (what "agent friendly" actually means, no vibes)
1. **Machine-readable everything**: `ralt check --json` emits diagnostics with
   `{code, span(file,line,col,len), message, hint}`. Stable numeric error codes, never renumbered.
2. **Simple orthogonal grammar** (LL-ish, no operator-overloading chaos, no macro hell):
   an agent can predict the language. Grammar published as a single EBNF + data file.
3. **Explicit over implicit**: no hidden lifetimes, no implicit conversion table,
   no preprocessor. Same text always compiles the same way (deterministic, hermetic).
4. **Errors as values**: no exceptions, no null. `?T`/`Result` style, forced handling.
5. **Tools in one binary**: `ralt` = compiler + fmt + test + doc + check in one static binary.
6. **Fast builder**: incremental compile, content-addressed module cache, C-codegen
   backend first (tcc/LTO = milliseconds), native x86-64 backend later.

## Tier system (modularity = kernel + additive layers)
- T0 `kernel`: ints/bools/structs/enums/fn/fixed arrays/extern C/consts. No alloc, no runtime.
- T1 `core`: + arenas, slices, Result/?, match, modules. 
- T2 `std`: + ref-owned pointers, String/Vec/Map, closures, iterators, generics.
- T3 `full`: + coroutines/async, self-hosted compiler runtime.
- One `ralt` binary; tiers are compiler-internal feature flags.
- Tier ceiling declared in source (mechanism TBD - question for user).
- Nothing degrades across tiers: unsupported = hard JSON error, never "kinda works".
- `ralt check --json` reports which tier each usage requires; `ralt tier lint` unused-slot tooling.

## Optimization model
- Builder speed: default = no optimizer (ms builds), -O fast|size opt-in, per-pass
  timing via ralt build -stats`, content-addressed module cache, tcc-consumable
  output as hard gate.
- Runtime speed: single-translation-unit C output (static internals) so cc does IPA
  free; arena = no free calls. Own passes only where types beat C optimizers:
  niche packing for opt/result (zero overhead), proven bounds-check elimination,
  const fold/prop + small-inline, escape analysis to stack slots.
- No UB = optimizer asset: all transforms provable, `-O fast` preserves semantics.
- P4 native backend: linear-scan regalloc, no heavy SSA; kernel tier is reference.
- `bench/` from day one: binary size, startup, build ms (cc + tcc), numeric bench.

## Language draft (v0 sketch)
- statically typed, local inference, sum types + struct enums, no null, no exceptions
- modules with explicit `use`, `pub` visibility
- manual-ish memory: arenas for hot paths + ref-count fallback; no stop-the-world GC
- unsafe/extern islands clearly fenced (`extern "C"` blocks for the C backend)
- stdlib: no libc dependency for pure targets (freestanding-capable)

## Approach / phases
- **P0 — Spec (this repo, markdown + data)**
  - `spec/tripon.md` — language spec v0
  - `spec/grammar.ebnf` — one grammar file, the source of truth
  - `spec/errors.csv` — error code table (code, severity, message, hint)
  - acceptance: spec round-trips 10 sample .tp files
- **P1 — Bootstrap compiler in C99** (`src/` - zero deps, builds with cc + tcc)
  - lexer → parser → resolve/typecheck → C99 codegen → emits .c/.h per module
  - ralt build`, `ralt run`, `ralt check --json`
  - acceptance: hello world + linked list + sum-type match program compile+run
- **P2 — Agent tooling**
  - `ralt fmt`, `ralt doc`, `ralt explain <code>`, LSP (server in C, reuse compiler internals)
  - acceptance: one-shot agent edit loop works (error JSON → fix → green)
- **P3 — Self-host** ("Tripon made with Tripon")
  - port lexer, then parser, then checker to .tp; bootstrap-compiler keeps compiling it
  - flag day when tp-compiler.tp compiles itself via the bootstrap
- **P4 — Native backend + stdlib+** (x86-64 ELF codegen, drop C deps, package cache)

## Files touched
P1: `src/*.c`, `src/*.h`, `Makefile`, `spec/*`, `examples/*.tp`

## Verification
- every phase: build with `cc` AND `tcc`, run acceptance programs, compare JSON output
- self-host: bootstrap output binary == bootstrapped output binary on golden tests
