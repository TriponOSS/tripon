# Tripon (.tp) — Plan

Status: P0 spec done (pushed 4e7c27f). P1.0 tests frozen, compiler in build.

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

## P1 detailed plan (ralt in C99, zero deps)

Repos/ownership: `TriponOSS/tripon` monorepo (spec/, ralt/, stdlib/, examples/, tests/, bench/).
`docs` repo = site only. `.tp` = file extension, `ralt` = the one binary/driver.

### P1.0 milestone (this build slice)
Files: `ralt/src/{common.h,util.c,diag.c,lex.c,parse.c,typecheck.c,codegen.c,main.c}`, `Makefile`.

Passes, in order — each independent, each testable:
1. **lex** — full v0 lexical surface: idents, kw, dec/hex/oct/bin/floats, strings
   with escapes, all punctuation, line+block comments. No unicode idents (E0002).
2. **parse** — subset: tier decl, extern blocks, fns, params, let/const,
   if/else, while, return, expr stmts, assign ops, calls, C-precedence exprs.
   Struct/enum/match arrive P1.1; parsed-but-error with clear msg until then.
3. **typecheck** — builtin types i8..usize/f32/f64/bool/str/void. Local inference
   = literal type. NO implicit conversion (E0202 on all mixes, since cast lands
   P1.1). Scope stack, shadowing warn E0603, unused-bind warn E0601, const
   reassign E0203, return checks, call arg checks, while/if cond bool.
   Pointer-free: no borrow types exist yet (T8 shortform).
4. **codegen** — direct C99 emit, static fns, tp_-prefix for all Tripon-defined
   names (main maps to `main`), C-keyword collision mangling.
   No-UB contract wired in codegen TODAY:
   - uninit locals -> zero-init emit
   - / and % by zero -> E9003 trap helper
   - i32 +/-/* overflow -> trap via compiler builtins (default); `-fast` -> wrap
   - str = const char* literal; puts via extern decl as-is
5. **driver** — `ralt build|run|check|emit|explain`.
   - build: emit .c to build/<stem>.c (temp then rename), cc -O0 default,
     `-O fast` passes -O2
   - run: build then exec; stdin/stdout/stderr passthrough
   - check --json: the frozen diagnostic contract (below)
   - explain <code>: from table
   - exit codes: 0 clean/warn-only, 1 errors, 2 usage
6. **stdlib prelude v0** (hardcoded builtins, tier core, no import needed):
   `print_i32(i32)`, `print_f64(f64)` -> C printf backends. Extends P1.2.

### Diagnostic contract (frozen by tests/expected/*.json)
`ralt check --json <file.tp>` -> one JSON array, one object per diagnostic, in
lex->parse->typecheck emission order:
`{"code":"E####","kind":"error"|"warn","name":"...","file":"<as-given-argv>",
"line":n,"col":n,"len":n,"msg":"...","hint":"<errors.csv hint>"}`

### Test matrix (tests/, POSIX sh, zero deps)
| case | contract |
|---|---|
| hello | build+run stdout exact, incl while+if+const |
| arith | precedence, unary neg, %, compound assigns |
| floats | f64 default, exp literals, comparisons only (no mixing) |
| calls | fn params/returns/nested calls, str param via extern puts |
| print | builtin print_i32/print_f64 |
| strings | escapes \\n \\t \\\\ \\" |
| trap_divzero/trap_modzero | exit 101 + E9003 on stderr |
| trap_overflow | exit 101 + E9001 on stderr |
| check_typemismatch | E0202 span+hint exact JSON |
| check_constreassign | E0203 span+hint exact JSON |
| check_unknownident | E0201 span+hint exact JSON |
| check_parse | E0101 span+hint exact JSON |
| check_clean | [] exit 0 |

Traps: exit 101, stderr line `tripon trap E<####> <name> <file>:<line>`.

Extra gates (every P1 acceptance run):
- tests/determinism.sh — `ralt emit --c` twice, byte-identical or fail
- bench/build_bench.sh — build ms / check ms / emit ms / binary bytes, baseline printed
- full suite passes under `gcc` AND `clang` before any commit says done

### P1.1 (next slice after hello green)
- cast<T>(x) builtin + E0207 exact infra; struct+enum+match parse/type/codegen;
  result<T,E> + `?` spread + E0205 enforcement; extern varargs decl syntax.
### P1.2
- slices `[ ]T` + bounds checks (E9002) + BCE only when provable; str ops len;
  `use` modules + symbol visibility pub.
### P1.3
- fmt (subset), unused analysis maturity, build cache + -stats timings,
  tcc-consumability gate.

## Verification
- every phase: full test matrix + determinism + cc/clang, diff-check of JSON goldens
