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

**RESOLVED 2026-06-28 — destroy-SCC mechanic landed (commit `85e6569`), but recursiveDestructor
is NOT green: it is a DUAL-cluster test.** The pre-seed designs above were all abandoned (they
either regressed generic/container classes via seed leaks + selfType capture, or only fixed the
cross-CALL while leaving cross-class FIELD ACCESS broken). The approach that worked, far simpler
and zero-regression:
- **Per-class `ClassInfo.constructing` flag** (true while its `constructorFunction` is on the
  stack). In `methodCallType`, a `destroy` cross-called while `info.constructing` is true (the
  exact mutual-cascade trigger: destroy_Foo demanded from inside Bar's class body, or vice-versa)
  returns a provisional `self -> none` arrow instead of being walked in the incomplete context.
  The eager-destroy pass walks the real body once every class is built. Naturally scoped to the
  true bug — one-way cascades (classheapsort Root/Element) and non-cascade classes (symtest) are
  untouched because their destroys are never demanded mid-construction.
- **`ClassInfo.expectedParamArity`**, recorded before the param loop fills `paramVars`, used by
  `null(ThisClass)` so a re-entrant null during nested construction mints the right instance
  arity (fixes the `Foo()` vs `Foo(Bar?,u64)` unify failures). DO NOT try to fix this by
  pre-reserving paramVars in the param loop (perturbs var-id order → breaks generic-class
  generalization, e.g. Symtab_findSym_*) or by counting `fn.variables()` at the null() site
  (over-counts for classes where ctor params != instance arity → `Root(tuple1())` too-many-args).
- These two carry recursiveDestructor PAST the whole destroy-SCC failure. It now dies at a
  **SEPARATE cluster = Stage 3 (generic relation-method instantiation)**: `remove(self, child)` /
  `find(self, key)` / `insert` have UNTYPED params, so their generic typecheck records a spurious
  `hashValue_none` instantiation (unconstrained key defaults to none). At emission that concrete-
  looking instantiation hits `hashValue`'s poisoned `default` arm (builtin/hashed.rn:70) ->
  "typeswitch case selected at 70 did not typecheck". Verified via probe: `emittingName=
  [hashValue_none] scrutinee=none`. So **recursiveDestructor needs BOTH Stage 1 (done) AND
  Stage 3**; the plan's premise that it was a pure Stage-1 first-green was wrong. Candidate
  first-greens that are pure single-cluster should be re-evaluated.

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

**Discovered 2026-06-28 (attempt #1 — full instrumented trace; reverted, suite went 187→185):**
The naive pre-seed is necessary but FAR from sufficient. Instrumenting `plainFunction`
(enter/DEFER/DONE + depth), the constructor methods-loop, and the emitter (`expr.rn:2058`,
`function.rn` genCMethodInstance) revealed the actual sequence for `recursiveDestructor`:

1. `constructorFunction(Foo)` runs first. **Inside Foo's ctor BODY** (`bar.insertFoo(self)`),
   `constructorFunction(Bar)` is demanded (nested). Bar records its 5 fields, then Bar's
   methods-loop walks `Bar.destroy` (depth d1), which cross-calls `Foo.destroy` (demanded at
   depth d2). **At d2 Foo's own fields are only partially recorded** (Foo's ctor body is
   suspended mid-way at the `insertFoo` call), so `Foo.destroy`'s body cannot fully type:
   it finishes with `untypedCalls=2` — the WHOLE for-loop header `range(self.$Bar_Table.length())`
   is untyped (iterator inlining never ran in that broken context). Its function-level type is
   still set (`Foo() -> none`), so every later pass EARLY-RETURNS and never revisits it.
2. The methods-loop's generated-method guard (typechecker.rn ~4549, `isGeneratedMethod &&
   errsAdded`) NULLs+`clearExprTypes` the destroy that *adds errors during its own iteration*.
   Because Foo's errors are raised during **Bar's** iteration, **Bar** gets cleared (and
   re-typed later) while **Foo** keeps its half-typed body. Asymmetric: the inner-demanded
   destroy is the one that rots.
3. At emission, `genCMethodInstance` walks `Foo.destroy`'s body and hits the untyped
   `self.$Bar_Table.length` Dot → `expr.rn:2058 "call emitted without a type"`. (`currentEmittingFn`
   is empty there because the destroy-emission path never sets it — NOT diagnostic.)

**Two coupled root causes, not one:**
- (a) **cross-CALL** to an unseeded peer types null — fixed by pre-seeding an in-progress arrow.
- (b) **cross-class FIELD ACCESS** (`entry.nextHashed<...>`, `entry.hash`) on a peer whose
  constructor hasn't finished recording fields — pre-seed does NOT fix this; the body must be
  walked only AFTER every class is fully built (i.e. defer the destroy walk to the eager pass).

**Why attempt #1 regressed (key gotchas for attempt #2):**
- Pre-seeding ALL destroys + **skipping them in the methods-loop** (so the eager pass walks a
  pristine, never-inlined body) is the right shape for (b), BUT it **leaks seeds**: any class
  whose destroy the eager pass doesn't reach (nested classes, instantiation-only emission via
  `function.rn` genCMethod) is emitted with the placeholder arrow `FN(tyvar,tyvar)` → **segfault**
  (`classheapsort`, `symtest` newly broke; 2 core-dumps during compile).
- The seed's **self-parameter must be the real `selfType`**, not a bare tyvar. With a bare tyvar,
  `methodCallType`'s `unify(selfParam, instance)` + `null(self)` produced a malformed `Foo()`
  (empty-param) instance that then failed later unifications (`Foo()?` vs `Foo(Bar?,u64)?`).
  → seed LAZILY inside `constructorFunction` once `selfType` exists, not in a pre-pass.
- Re-walking an already-inlined destroy body **re-inlines and corrupts** it (HANDOFF hazard);
  this is why the eager-pass walk must be the FIRST and ONLY walk (hence the methods-loop skip),
  or must restore a pristine pre-inline body snapshot.

**Attempt #2 shape (proposed, NOT yet validated — run by user first):** seed each destroy lazily
in `constructorFunction` with the real `selfType`; skip seeded destroys in the methods-loop;
make the eager pass the single walk site; and **leak-proof emission** — in `function.rn`
`genCMethod`/`genCMethodInstance`, if `seededInProgress` still set, demand-walk before emitting
(covers nested/instantiation-only classes the eager pass misses). Gate on full suite ≥187 zero
drops AND no new core-dumps.

**Files:** `types/typechecker.rn`, `database/function.rn` (seededInProgress flag + emission
leak-proof), `cbackend/cbuilder.rn` (does NOT touch safe/funcptr).
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

**✅ recursiveDestructor is GREEN (2026-06-28, commit `5356723`, suite 188/205 zero drops).**
It needed Stage 1 (destroy-SCC, `85e6569`) PLUS the Stage-3 relation-method-emission pieces that
turned out to be the bulk of the work.  The chain of bugs, each fixed in order (a useful map of
how a cascade flows through the whole pipeline):
1. duck-typed ambiguous field `child.hash` (both Foo and Bar have hash) → resolve to the common
   concrete field type u64 (`commonMemberFieldType`), not a disconnected fresh var.
2. deferred void relation method (`remove`/`insert`) generalized a free RESULT var → tie
   monoResult to the body's return type (none) in the deferral path, so its instantiation is
   concrete and actually emitted.
3. deferred method body emitted with leaked vars → `genCMethodInstance` must set emitting fn/name
   and `retypecheckDeferred` under the concrete instantiation (parity with genCPolyInstantiation).
4. mutual-recursive destroys called before defined → method-call emission records the callee as a
   dependency (`noteDependency`) so the C emitter emits a prototype on the cycle.
5. mutual cascade infinite-recursed at RUNTIME (stack overflow) → the cascade clears its hash
   bucket BEFORE recursing (builtin/hashed.rn), so a re-entrant destructor finds nothing to
   recurse into.  `rn_id` stays the object identity (read by `<u32>self`), reset last — do NOT
   zero it before the body (breaks one-way cascades' identity prints: graph/onetoone/arraylist).
Remaining Stage 3 work below (turing/edwards2 — the polymorphic-recursion mono walk) is separate.

**recursiveDestructor's remaining blocker lives here (probed 2026-06-28, commit `62840e9`):**
After the destroy-SCC fix (Stage 1) + the ambiguous-duck-typed-field fresh-var fix (this stage,
committed) the test now compiles past typecheck and the `hashValue_none` poison, but FAILS at C
link: the deferred relation-method specializations the destroy calls — `removeBar_Bar_Foo_u64`,
`insertBar_Bar_Foo_u64` — are CALLED but NEVER DEFINED, and `removeBar`'s body emits with a leaked
type variable (`hashValue_v_u45129`). Root: a deferred class method (relation `remove(self,
child)` / `find(self, key)` with an unconstrained param) is emitted via `genCMethodInstance`
(`database/function.rn` ~:863), which — UNLIKE `genCPolyInstantiation` (~:460-473) — sets neither
`currentEmittingFn`/`currentEmittingName` NOR calls `retypecheckDeferred`. Adding both to
`genCMethodInstance` is NON-regressing (187) but INSUFFICIENT alone: the concrete instantiation
`removeBar_Bar_Foo_u64` the destroy references is never recorded/emitted at all (genCMethod finds
no concrete instantiation — only a non-concrete one). So the fix needs (a) the deferred-method
retypecheck in `genCMethodInstance` (pattern parity with plain functions), AND (b) the concrete
relation-method instantiation to actually be recorded + emitted on demand from the destroy's call
site (emission-ordering / late-instantiation). This is squarely the §4.4 collector-worklist /
restructure work below. **So recursiveDestructor needs Stage 1 (done) + this Stage 3 emission
restructure.** Likely-needed first step here: give `genCMethodInstance` the same
deferred-retypecheck + emitting-name handling that `genCPolyInstantiation` already has.


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
