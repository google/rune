# Desugar-migration guardrail fixture

`grd_relsecret.rn` pins the secret-type + memory-safety codegen of **generated
relation members** so the Stage A desugar lift can be proven a *faithful
relocation* (see `../MIGRATION_PLAN.md` Stage A pre-reqs).

It exercises, in generated (not hand-written) code:
- `OneToOne` back-pointer fields (`self.Bar` on `Foo`, `self.Foo` on `Bar`),
- generated `insertBar` (`ref child`) / `removeBar` (`unref child`),
- the generated `cascadeDelete` destroy cascade + `final()` destructors,
- a `secret(...)` u256 value flowing through a relation-participating class and
  `reveal`-ed, so secret folding over generated members is covered.

## Invariant

Emitting C from this fixture must be **byte-identical before and after** any
desugar-migration change. The compiler is deterministic (verified: two
consecutive emits hash-equal), and the emitted C does **not** embed the source
path, so the hash is content-stable regardless of where the fixture lives.

## Baseline (captured at commit before the Stage A lift, suite 188/205)

- emitted-C sha256:   `dffd86efbf44223245aa217bcf7773a2e3a428d8d5bd04225159a89ea8f2030b`
- runtime-stdout sha256: `446d6c0759f14927845f77cdc720cfa5094a3670cc6bcf1a6e59e045d8cd7c05`
- compiler diag stream: only `Parsing <path>` (no typechecker errors)
- expected stdout:
  ```
  secret ok: true
  secret ok: true
  Destroying Bar 1
  Destroying Foo 1
  Destroying Foo 2
  Destroying Bar 2
  ```

## Verify

```bash
cd /home/ah/src/rune
ulimit -v 8388608
./bootstrap/rune bootstrap/research/guardrail/grd_relsecret.rn >/dev/null 2>&1
sha256sum bootstrap/research/guardrail/grd_relsecret.c   # must equal emitted-C sha256 above
./bootstrap/research/guardrail/grd_relsecret | sha256sum # must equal runtime-stdout sha256 above
```

A mismatch in the emitted-C hash after a desugar change means the lift was
**not** faithful — investigate before committing. (The clang `Foo(tuple1())`
"too many arguments" warnings are a pre-existing, deterministic zero-arg-ctor
quirk, unrelated to the migration.)
