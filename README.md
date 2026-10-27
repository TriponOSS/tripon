# Tripon (.tp)

Robust, simple compiled language for low-resource systems — and the #1
AI-agent-friendly programming language.

- **JS-like skin, type-safe core**: braces, `let`/`const`, no null, no implicit
  conversions, errors as values (`result<T, E>` + `?`), exhaustive `match`,
  no UB (overflow traps, bounds checks everywhere).
- **Tiered modularity**: `kernel` (no alloc, no runtime, bare metal) → `core` →
  `std` → `full` (async). One feature belongs to one tier; nothing silently degrades.
- **Agent contract**: `ralt check --json` emits stable error codes with spans and
  fix hints. Grammar in one EBNF file. Same source always compiles the same way.
- **Fast builder**: default build has no optimizer — milliseconds via tcc or cc.
  `-O fast|size` unlocks passes (niche packing, proven bounds-check elimination).
- **One binary**: `ralt` — build, run, check, fmt, test, explain, doc, new.

Status: spec phase. Bootstrap compiler in C99 lands next (PLAN.md P1).
