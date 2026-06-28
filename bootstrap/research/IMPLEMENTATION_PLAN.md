# Bootstrap Binding Fix — Execution Plan

Granular, actionable companion to `bootstrap/BINDING_RESEARCH.md` (the design) and
`bootstrap/research/type-inference-primer.md` (the concepts). This file is the **working
checklist**; the design doc is the **why**. Read both before editing.

---

## Baseline (verified 2026-06-28)

- **HEAD** `ce4c684`, branch **`bootstrap`**, **187/205** bootstrap tests passing.
- **Stage-1 target reproduced live:** `./bootstrap/rune tests/recursiveDestructor.rn` →
  `builtin/hashed.rn:231: internal: call emitted without a type` on
  `for x$labelB$B in range(self.$labelB$B_Table.length())` — exactly the failure §7 predicts
  (the mutual-destroy SCC leaves the table method call untyped → it surfaces at genC).
- Canonical references: design = `BINDING_RESEARCH.md` (§4 mechanism, §7 Stage-1 trace,
  §8 staged plan, §9 risks); concepts/vocabulary = `research/type-inference-primer.md`.

---

## Build & test recipe (verified commands)

All paths relative to repo root `/home/ah/src/rune`. Use `$TMPDIR` for scratch, never `/tmp`.

- **Rebuild the bootstrap compiler** (mandatory after editing ANY bootstrap source —
  `types/*.rn`, `cbackend/*.rn`, `database/*.rn`, `rune.rn` — OR `builtin/hashed.rn`):
  ```bash
  cd bootstrap && make          # legacy ../rune recompiles rune.rn → bootstrap/rune
  cd ..
  ```
  Editing `types/typechecker.rn` changes the **compiler itself**; you must `make` to see any
  effect. Editing `builtin/hashed.rn` ships to BOTH compilers, so `make` confirms the legacy
  rebuild survives too.

- **Run one test with the bootstrap binary** (NOTE: bootstrap rune takes **no `-g`**; it
  compiles by default, `-n` suppresses the C compile; `-p <dir>` overrides the builtin root):
  ```bash
  ./bootstrap/rune tests/recursiveDestructor.rn \
    && ./tests/recursiveDestructor | diff - tests/recursiveDestructor.stdout \
    && echo PASS
  ```

- **Full-suite gate (≥187, zero drops).** Mirror of `runtests.sh` but driven by the bootstrap
  binary (handles per-test `.stdin`/`.args` like the original):
  ```bash
  cd /home/ah/src/rune; pass=0; fail=0; fails=""
  for out in tests/*.stdout; do
    t="${out%.stdout}"; a="-n"; [ -e "$t.args" ] && a="$(cat "$t.args")"
    if ./bootstrap/rune tests/"$(basename "$t")".rn >/dev/null 2>&1; then
      if [ -e "$t.stdin" ]; then "./$t" >"$TMPDIR/o" 2>&1 <"$t.stdin"; else "./$t" >"$TMPDIR/o" 2>&1; fi
      if diff -q "$TMPDIR/o" "$out" >/dev/null; then pass=$((pass+1)); else fail=$((fail+1)); fails="$fails $t"; fi
    else fail=$((fail+1)); fails="$fails $t"; fi
  done
  echo "PASS=$pass FAIL=$fail"; echo "FAILED:$fails"
  ```
  (Compare the failing set against the known 18 baseline failures in `HANDOFF.md`; **zero new
  drops** is the gate, not just the count.)

---

## Commit discipline

Per `commit-on-green` + global CLAUDE.md: **commit after every clean, regression-free step**;
each commit gates on the full suite **≥187 with zero drops**. Stay on branch `bootstrap`.
Conventional, concise messages. End commit messages with the Co-Authored-By trailer.

> ⚠️ All `file:line` anchors below are **from the design doc and drift as code changes**.
> Re-grep to confirm each anchor before editing. Do not trust a line number blind.

---

## Stage 0 — named blocker plumbing (groundwork; turns NO test green)

**Goal:** make deferral *self-describing* and make Dict's constructor actually defer. Pure
de-risking; behaviour-preserving for the existing 5 sites.

1. Add the data types (per §4.1): `enum Blocker { ClassRegistered(name) | SignatureKnown(spec)
   | VarBound(tyvarId: i32) }`, `class Deferred { fn; blockedOn: Blocker; inst }`, a
   `worklist: List(Deferred)`, a `defined: Set`, a `boundVars: Set(i32)`.
   **`VarBound` id is `i32`** (per `instantiate` ~:1085), not u32.
2. At the five `sawStructuralGenericError` sites (`typechecker.rn` ~:2185/2210/2396/3545/4568),
   record the missing entity into `Deferred.blockedOn` (still fire once — no behaviour change).
3. **New producer at the `Arrayof` arm** (~:2416-2443): when the arrayof element is an
   unregistered class, signal a blocker and arrange for the constructor to defer. Today this arm
   is silent, which is *why Dict's ctor never defers* — needed for Stage 2.

**Validation:** rebuild; full suite stays **187, zero drops**. Failures now name what they wait
on. **Commit:** `Bootstrap: named Blocker + Arrayof deferral producer (groundwork).`

---

## Stage 1 — mutual-recursion destroy fixpoint  →  FIRST GREEN: `recursiveDestructor` (188)

This is the tie-the-knot-across-an-SCC mechanic, applied to `destroy_Foo ⇄ destroy_Bar`.
All hooks are real and non-generic (`Foo`/`Bar` are monomorphic), so none of the Dict
generic-table machinery is involved.

1. In the eager-destroy loop (`typechecker.rn` ~:4188-4230, iterating via `demandMethod`
   ~:4195/:558), **detect the mutual-recursion SCC**: `Foo.destroy` references `Bar.destroy`
   and vice-versa.
2. **Seed BOTH** with a `monoResult`-style in-progress return slot *before* checking either
   body — generalize the existing **single-slot** knot (`~:4797-4798`) to a **per-SCC set**
   (the GHC `fixM` / binding-group pattern). This is the "two boxes, both present before any
   body is walked" step.
3. Check each body; the cross-call (`Foo.destroy → Bar.destroy`) **unifies against Bar's
   in-progress slot** — no demand-recursion, no no-op. Park
   `Deferred{Foo.destroy, SignatureKnown(Bar.destroy)}` only if a method call is still untyped.
4. Replace the once-only `retypecheckDeferred` (`~:600`) with **`runDeferredToFixpoint()`**
   (§4.2): each round resolves one `SignatureKnown` blocker; **converges in ≤2 rounds**;
   terminate on the monotone-progress measure (§4.5).
5. **HAZARD (mandatory):** re-run each deferred body **from `bodySnapshot`** (`~:628-635`), NOT
   the live AST — re-typechecking an already-iterator-inlined body re-inlines and corrupts it
   (§9, `ground-bootstrap.md §2`). This is why `retypecheckDeferred` snapshots.

**Discovered 2026-06-28 (refines the above — read before implementing):**
- **Precise cause** (HANDOFF LAYER 4 + code read): under mutual recursion the destroy body's
  **method-call nodes** (`.length()`, `range(...)`) finish with `null typedValue` — only field
  accesses resolve (via field types). They are NOT caught by the once-only retypecheck because
  the destroys don't set `deferredTypecheck` (Foo/Bar are monomorphic); the call to the
  not-yet-seeded peer types to null and nothing revisits it. Non-mutual cascade (`graph.rn`)
  types fine. So pre-seeding the peer BEFORE the body walk is the real fix, not just a fixpoint.
- **Hot-path wrinkle:** `plainFunction` (now ~4839) early-returns at the top when
  `fn.typedValue != null` (~4840) and mints `monoResult` at ~4877-78. A raw pre-seed of
  `fn.typedValue` would make it skip the body. The pre-seed must mark the fn (e.g. a
  TypeChecker-side `seededFns` set), and plainFunction must (a) NOT early-return for a seeded
  fn, and (b) reuse the seeded `monoResult` instead of minting a new one. Watch that the
  seeded arrow's param vars line up with the body's fresh param vars (`self.variable()` at
  ~4865) — unify if needed.
- **Eager-destroy loop** is now ~4267-4309; `retypecheckDeferred` snapshot still at ~629-633.

**Files:** `types/typechecker.rn`, `cbackend/cbuilder.rn` (does NOT touch safe/funcptr).
**Validation:** `./bootstrap/rune tests/recursiveDestructor.rn` compiles; the executable's
output `diff`s clean against `tests/recursiveDestructor.stdout`; full suite **188, zero drops**.
**Commit:** `Bootstrap: tie mutual-recursion destroy SCC to a fixpoint (recursiveDestructor green).`

---

## Stage 2 — per-signature concrete `Entry`, at the TYPECHECK phase (Dict cluster)

**Net-new code; the riskiest stage.** Must run during `tc.function(module)` (the
constructor-typing path / immediately after phase 8, before phase-10 user statements), **NOT**
in `CBuilder.build` — the 5 typecheck-failing cluster tests stop at the gate (`rune.rn`
~:154-164) and never reach emission.

- Implement `materializeInner(Dict, [string,u32])` (§4.3): open a fresh frame, register a
  **distinct concrete `Entry<string,u32>`**, cache in `defined` BEFORE binding its body.
- **P-ALIGN:** align only the template paramVars. `Entry.info.paramVars = [dict,key,value]`
  (len 3) vs template `[key,value]` (len 2); build index map `[1,2]`; do NOT feed args
  positionally into `substituteClassVars` (~:4659, which would bind `dict←string` and skip
  `value`).
- **P-OUTER (§4.3.1):** a distinct concrete OUTER `Dict` class per resolved-param signature, so
  `Dict(string,u32)` and `Dict(u32,string)` don't collapse onto one interned Sym
  ("could not unify (string,u32) with (u32,string)").
- Wire the Stage-0 `Arrayof` arm to `materializeInner`.
- Model: legacy `markConstructorClassBound` (**`bind/bindexpr.c:1219`**) + the `deClass`
  machinery (`database/class.c:98-220`).

**Validation gate, IN ORDER (do not claim 6 tests up front — Dict is a composite of P-ALIGN +
P-OUTER + missing-inner-instantiation, no single edit greens the cluster):**
(a) `/tmp/zdict/b.rn` repro compiles+runs; (b) `heapqlisttest` (the lone codegen-reaching
member) green; (c) only THEN `in`/`dictitr`/`dicttest`/`heapqtest`/`heapsort` once P-OUTER
lands; (d) full suite ≥187, zero drops. **Fallback (§9):** if `materializeInner` can't tie
Entry's params, eager per-signature class creation at the `Dict(K,V)` recording site (closer to
legacy), accepting duplication. Never place at emission.
**Files:** `types/typechecker.rn`, `parse/exprTree.rn`, `cbackend/cbuilder.rn`.

---

## Stage 3 — depth-guarded mono walk, RESTRUCTURE `genCPolyInstantiation`  →  `turing`, `edwards2`

- Make `PolyInstantiations` a real **collector worklist** (§4.4) with a per-base-fn
  recursion-depth counter (REJECTS at `RECURSION_LIMIT`, error not hang) + `visited` dedup.
- **Restructure** (§4.4.1): replace the shared-poly `instantiate`/`deInstantiate` bind/unbind in
  `genCPolyInstantiation` (`database/function.rn` ~:423/:479) with **top-level emission of each
  `addParens_N_M` via its own `poly.open()` frame**; nested same-poly calls emit a **name
  reference**, never a re-`instantiate`.
- **Bottom-up result resolution** (§4.4.2): emit the base case `_0_7` first; unify each frame's
  result slot with its callee's resolved result before emitting its C signature. (Top-down →
  `genCType` `v-65 unresolved`.)
- **Do NOT touch `edwards`** here (it typecheck-fails, "Cannot index into v-87" — that's the
  Stage-4 leak family). Preserve cases the current code already handles (notably `countParens`).
**Files:** `database/function.rn`, `database/expr.rn` (~:2613-2731), `cbackend/cbuilder.rn`.

---

## Stage 4 — typecheck-leak + generalization-policy bugfixes (separate; NOT an ordering problem)

`edwards`/`gf2`/`integer` leak a polymorphic *result* var into user code at typecheck (needs
typecheck-phase result resolution, not the emission mono-walk); `gf2`/`integer` also have the
param-merge in `deferVars`/`PolymorphicType` (~:4892-4924); `safe`/`funcptr` are the
positive-var / tyvar-id-space issue. Keep OUT of the binding-architecture change; gate behind
not regressing operator tests.

---

## Standing hazards (from §9 — keep in view every stage)

- **AST-mutation-by-inlining (Stages 1, 3 — highest):** always re-run bodies from
  `bodySnapshot`; never a naive second pass.
- **Stage 2 is net-new, not reuse** — `classInstanceType`(:486, fresh vars) and
  `demandClass`(:583, generic) do NOT do per-signature concretization. Fallback above.
- **Genuine A↔B blocker cycles:** the fixpoint is sound but not complete — it reports the
  residue as errors (legacy `bind.c:642-660`). Our failures are staged, not cyclic.
- **`nomono` inputs are REJECTED at `RECURSION_LIMIT`,** never silently miscompiled.
- **Fallback if (C)+worklist proves too entangled:** the full event binder (candidate A) is the
  faithful upper bound. Stage 1 alone (187→188) is already a worthwhile, low-risk landing.
