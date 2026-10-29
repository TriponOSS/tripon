# stdlib

Tier-managed standard library for Tripon, in pure `.tp` — no FFI, no C.

Layout:

```
src/stdlib/
  kernel/math.tp      integer math helpers (tier kernel, no alloc)
  kernel/bits.tp      byte packing, popcount, parity
  core/optutil.tp     opt<i32> helpers: is_some/is_none/unwrap_or/map_or
  core/resultutil.tp  result<i32,str> helpers: is_ok/is_err/unwrap_or/err_or_empty
  core/strutil.tp     str helpers over len()/indexing
  core/sortutil.tp    sort3 min/max/mid, is_sorted3
  core/floatutil.tp   sqrt_newton, abs/clamp/min/max f64, powi
  core/vec4.tp        fixed-capacity [4]i32 vector in a struct
  core/map8.tp        fixed-capacity i32->i32 map (linear probe)
  core/queue8.tp      fixed-capacity FIFO ring
  core/buf16.tp       fixed-capacity byte buffer + Fletcher-style checksum
  core/strcmp.tp      byte search, lexicographic compare, case-insensitive eq
```

Consumption: `src/stdlib/` is a plain local dep — point any `ralt.yaml` at it:

```yaml
name: myapp
version: 0.1.0
deps:
  std: ./src/stdlib
```

Dep folding merges the stdlib sources into the same translation unit, so
`min_i32`, `unwrap_or_i32`, etc. resolve by plain name. Tier ceilings still
apply per file (`use tier` at the top of each module).

Smoke tests: `tests/cases_std/*.tp` + `tests/expected_std/*.out`, run by
`tests/stdlib_smoke.sh` (wired into `tests/run.sh`).

Constraints (v0): arrays are function-local only (no array params), `opt`
carries scalar/struct only, `?` propagates only inside same-shape fns — the
stdlib is shaped around these, not around them.
