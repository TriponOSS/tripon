# Tripon Language Specification v0

One binary `ralt`. Compiles .tp to C99 (v0.x), native x86-64 later.
Semicolons and braces mandatory. No preprocessor. No macros. No null. No exceptions.
Deterministic: the same source always compiles the same way, byte for byte.

## 1. Identity

| | |
|---|---|
| Extension | `.tp` |
| Entry | `fn main() -> i32` (or `-> ()` for void) |
| Comment | `//` line, `/* */` block |
| Shebang-friendly | leading `#!` line ignored |

## 2. Tier system (modularity = kernel + additive layers)

| Tier | Name | Gets | Target |
|---|---|---|---|
| 0 | `kernel` | ints/bools/byte/structs/enums/fn/fixed arrays/extern C/const | firmware, bootloaders, bare metal |
| 1 | `core` | + arena allocator, slices, Result/?/opt, match, use modules | microcontrollers, tiny CLIs |
| 2 | `std` | + ref-owned pointers, String/Vec/Map, closures/arrow fns, generics | normal apps, default |
| 3 | `full` | + coroutines/async, self-host runtime | everything else |

Rules:
- Tier ceiling declared at top of file: `use tier std;` (mandatory except for
  single-file scripts, where the default is `core`).
- Stdlib modules declare their own tier; `use` raises your requirement.
- Nothing degrades across tiers. Unsupported on your tier = hard error E0301.
- Higher-tier features are never inferred into lower-tier code.

## 3. Type-safety contract (non-negotiable)

T1. **Fully static typing.** Locals infer; signatures are explicit. No dynamic typing.

T2. **No implicit conversions, ever.** Even widening is explicit:
    `cast<i64>(x)`. Only exact-type or safe-tagged casts compile. E0207 otherwise.
    (`cast` = the one conversion function; lossy ops require it.)

T3. **No null.** Missing-value type is `opt<T>` with two variants `some(v)` and
    `none`. Unwrap via `?` (T1) or `match`. Flow-narrowing: after a truthiness
    check, the binding is statically known to hold a value. E0204 otherwise.

T4. **Errors are values.** `result<T, E> = ok(v) | err(e)`. Ignoring a result is
    a compile error (E0205). Propagate with `?`; handle with `match`.

T5. **Exhaustive match.** `match` over an `enum` must cover every variant (use
    `_` deliberately). Non-exhaustive = E0206.

T6. **No UB. Defined behavior for everything:**
    - integer overflow: trap (panic) in all tiers by default; `-fast` build flips
      arithmetic ops to two's-complement wrap, explicitly per-buildflag.
    - array/slice indexing is bounds-checked; out-of-bounds = defined trap E9002.

T7. **Raw pointers only inside fenced islands:**
    ```
    extern "C" {
        fn puts(p: *u8) -> i32;
    }
    ```
    Everything outside an `extern` block is pointer-free and memory-safe.

T8. **People not shared.** `const` bindings cannot reassign (E0203). Mutable
    aliasing of owned data is blocked by construction in v0 (single-owner moves +
    borrows `&T` read-only). Full borrow checking lands in self-host (P4) with
    `&mut T` exclusivity enforced statically.

T9. **Type width is explicit.** `i8 i16 i32 i64 u8 u16 u32 u64 isize usize f32 f64 byte bool ()`.
    Default integer literal: `i32`. Ambiguous float: `f64`.

## 4. v0 syntax quick reference

```tp
use tier core;

fn main() -> i32 {
    let x = 5;                  // i32, reassignable (let = let JS feel)
    const LIMIT = 10;           // immutable
    let total = 0;
    let idx = 0;
    while idx < LIMIT {
        total = total + x;
        idx += 1;
    }

    let s = "hello";            // str = utf8 slice, heap-free literal
    if s.len() > 0 { puts(s); }

    return 0;
}
```

Sum types + match + ?:

```tp
enum Shape {
    Circle(f64),
    Rect(f64, f64),
}

result<T, E> is stdlib, tier 1: varianted `ok(v) | err(e)`, spread with `?`.

fn area(sh: Shape) -> f64 {
    return match sh {
        Circle(r)      => 3.14 * r * r,
        Rect(w, h)     => w * h,
        // _ not needed: exhaustive over enum defaults
    };
}
```

## 5. Error table

Machine table: `spec/errors.csv`. `ralt explain E0205` prints code + hint.
Codes are never renumbered; new codes are appended.

## 6. Agent contract

- `ralt check --json` → `[{ "code": "E0202", "file": "...", "line": n, "col": n,
  "len": n, "msg": "...", "hint": "..." }]`
- One fix span per diagnostic. No batching that hides the first real error.
- Grammar lives in one file: `spec/grammar.ebnf` (plain EBNF).
- The tier table (section 2) is the permission table: given a tier, the legal
  feature set is known exactly.

## 7. Open for P4 (not decided yet)

- Closures capture rules, async slot, borrow checker fullness, generics ABI.
