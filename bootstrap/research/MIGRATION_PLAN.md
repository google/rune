# Finishing the Relations → Desugar Migration — Execution Plan

Authoritative execution roadmap to complete the bootstrap binding fix by adopting Lyric's
relation/desugar/monomorphization architecture **while keeping Rune's secret types and
memory-safety guarantee**. Re-baselines the stage structure of `IMPLEMENTATION_PLAN.md`
around the Lyric-confirmed design in `RELATIONS_DESUGAR.md`.

**Folded in the `lyric-relations-extract` workflow (run `wf_35efec1e-261`, 16 agents):
file:line detail in `WORKFLOW_FINDINGS.md`; completeness-critic corrections applied below.**

Companion docs: `WORKFLOW_FINDINGS.md` (full Rune-grounded steps + critic), `RELATIONS_DESUGAR.md`
(design synthesis + Lyric evidence), `BINDING_DESIGN.md` / `BINDING_RESEARCH.md` (research,
direction C, A1–A7), `IMPLEMENTATION_PLAN.md` (failure triage, Stage 0–1 history),
`CODE_REVIEW_CHECKLIST.md` (review loop).

---

## ⚠ REASSESSMENT (2026-07-01) — supersedes Stage B; re-sequences B/C

Full re-read of The Lyric Book (ch08 relations, ch10 Dict, ch14 pipeline/4-phase Check;
`lyric-reassess` workflow, 6 book-mining agents) + empirical re-measurement on a clean rebuild.
This block overrides the Stage-B plan below where they conflict.

**Empirical corrections to the snapshot:**
- Clean baseline (`2663f55`) is **188/205** (17 failing) — confirmed. An earlier reading of
  "187" was a HARNESS BUG, not a regression or dragon drift: the harness ran binaries by bare
  path (`tests/printargv`) so `printargv`'s golden `["./tests/printargv"]` mismatched on argv[0];
  invoking via `./tests/<name>` (fixed in `67e24ca`) restores the true 188. Failing 17:
  `allocfree defaultMethods dictitr dicttest edwards edwards2 escapedCharTest funcptr gf2
  heapqlisttest heapqtest heapsort in integer safe turingTypeConstraints uint2string`.
- **Stage B as scoped (name-only Phase-0 stub + stub-reuse + demandClass in the arrayof arm)
  is a PROVEN DEAD END.** Freshly rebuilt with no dict, it regressed **188→181**, breaking 7
  HashedClass-cluster tests (`classheapsort classtype hashedClassTest hashedtest
  printArrayOfClasses symtest twohash`) and greening ZERO. The earlier "188-neutral" reading
  was an artifact of a stale (dict-loaded) binary. Changes preserved in `stash@{0}`, reverted.
  (Absolute counts in this doc from before `67e24ca` were measured one low; deltas are correct.)

**Why it can't work (root cause, book-confirmed — ch14 §14.3):** Lyric's Check is 4-phase with
a hard barrier — Phase 0 pre-registers NAMES, **Phase 1 fills full TypeInfo/FIELDS declaratively
by reading syntactic + desugar-injected field nodes (executing NO body)**, Phase 1.5 binds
methods, Phase 2 checks bodies. Lyric's Phase-0 is name-only *only because* Phase 1 later fills
fields without running anything. **Rune discovers a class's fields by EXECUTING its constructor
body** — field-discovery and body-checking are fused, so there is no declarative Phase-1
analogue. A name-only stub carries no fields, and `demandClass` (which runs the body) fired
mid-`arrayof` re-enters construction → the −7. *The prerequisite the plan skipped is splitting
field-collection out of constructor execution.*

**Two DISTINCT failure modes were being conflated (disambiguate before coding):**
1. **Forward-ref / field-discovery** — `Entry` resolves null while `Dict` binds (needs Entry's
   fields available). This is the Phase-1 gap above. Blocks the Dict cluster from *binding*.
2. **Template-emission contamination** — merely *loading* `dict` (adding it to the loader,
   which compiles it into every program incl. the self-build) regresses the SAME 7 HashedClass
   users. That is a monomorphization / per-signature *isolation* problem (Stage C), not field
   discovery. `dict.rn` and `heapq.rn` both exist as builtins but are unloaded; **5 of 8
   in-scope failures (`dicttest dictitr in heapqtest heapsort`) are gated on loading one of
   them**, and loading regresses −7.

**Target calibration (ch10 §10.5):** Lyric ITSELF cannot compile `Dict<K,V>` as a *field on a
class* — "TypeVar leak 'V'", identical to Rune's v54/v-111; it's on Lyric's roadmap. But
multi-signature *top-level* Dict (our `dicttest`) works in Lyric via monomorphization. All our
failing Dict/Heapq tests are top-level → within the Lyric frontier; don't chase Dict-as-field.

**Out of book scope entirely:** D-binding (`gf2 integer safe funcptr`) — the book has NO HM
let-generalization (inference is one-directional call-site arg→param unification only). Source
these from `BINDING_RESEARCH.md §3`, not Lyric. Representation (`escapedCharTest uint2string
allocfree defaultMethods`) — length-prefixed strings / object-pool, orthogonal.

**RE-SEQUENCED PLAN (replaces A→B→C→D):**
- **D1 — Diagnostic spike (DONE 2026-07-01).** Found the dict-load blocker has TWO layers, and
  the first was NOT the dragon:
  - **Layer 1 — `[]` lexer bug (FIXED, committed `d068837`).** Loading `dict` crashed the
    compiler *while lexing* `dict.rn` — historically blamed on the "pool-resize dragon," but it
    is **deterministic, not count-sensitive** (every byte-count variant of adjacent `[]` crashes;
    `[ ]` with a space is fine). Root: the lexer adjusted `groupDepth` from the peeked first
    char, so the two-char `[]` index-operator token counted its `[` as opening a group while its
    `]` (inside the token) never closed it → `groupDepth` stuck → the newline-swallow loop ate
    every newline to EOF → run off the buffer. Fixed by counting group depth from the whole
    token. Suite-neutral 188. This unblocks *lexing* `dict.rn`/`operator []`.
  - **Layer 2 — uninstantiated-template emission (STILL OPEN, = Stage C).** With `dict` loaded,
    a program that never uses `Dict` still emits `Dict`'s class methods AND its Hashed-relation
    methods that `hoistNestedFunctions` lifted to module level (`updateHashTableAfterResize_h1`,
    …) carrying the class's free type vars → invalid C (`Dict_v135_v136_t`, `hashValue_v_u45125`).
    Two guard attempts REJECTED: (a) a `hasFreeVars` guard on `genCMethod`/`genCPlainFunc` is too
    coarse — it also skips legitimately-needed free-var `Function`-typed methods that resolve
    under an active binding, regressing `genericFactorial polygroup recursiveDestructor twohash`
    (184/205, no dict); (b) a class-level "skip methods of an uninstantiated generic class" guard
    on `genCConstructor` is suite-neutral (188, no dict) but MISSES the hoisted relation
    functions (they are module-level plain functions, not class children) → PASS=0 with dict.
    **Conclusion:** Layer 2 needs true reachability/per-signature monomorphization — emit only
    specializations reachable from a concrete use, skipping uninstantiated templates INCLUDING
    their hoisted relation functions — plus a `validate_post_mono` gate. `hasFreeVars` alone is
    not the discriminator; "reachable from a concrete instantiation/use" is. This is Stage C.
  - **So:** the dict cluster's TWO gates are now (1) lex `[]` — DONE; (2) don't emit
    uninstantiated templates — Stage C. There is no separate `demandClass`-reentrancy blocker for
    dict loading; the earlier −7 was measured with the (now-abandoned) Stage-B changes.
- **Stage C — Iterative monomorphization FIRST (book-specified, lower risk, post-Check pass):**
  import the book's invariants verbatim — a **fixpoint worklist** over `(fn, concrete-sig)`
  ("converges in 2–3 iterations"), and a **`validate_post_mono` gate** asserting no residual
  type params. Per-signature field-type isolation is the likely fix for the template-emission
  −7, so C may be what actually unblocks *loading* dict/heapq. Targets the non-loading-gated
  `heapqlisttest turingTypeConstraints edwards2` immediately; then `heapqtest heapsort dicttest`
  once loading is safe. Do C0→C1→C3, each suite-gated.
  - **C-ENTRY — ✅ LANDED (`41dd129`, 2026-07-02), but delivered ZERO greens (188/205 held,
    zero drops, self-build clean).** Mechanism: `Sym.exists()` probe (lookup-only, added to
    `std/sym.rn`) checked in `loadBuiltinModules` before any builtin loads — the lexer interns
    every identifier, so after the user's modules parse, sym `Dict`/`Heapq` exists iff a user
    file names it (exact match: `HeapqList` is a different sym; comments/strings never intern).
    **The "expected greens" prediction below was WRONG:** loading-when-used does avoid the
    free-var emission of *unused* templates, but the *used* templates still fail downstream:
    - **Dict cluster (`in dictitr dicttest`):** `typeunifier.rn:647 assert v > ty.tyvar.id`
      in `resolveVar` — unification creates a var→var binding pointing to a HIGHER (newer)
      tyvar id, violating the union-find chain-orientation invariant (older-id representative).
      One signature suffices to trigger it (`in.rn` is a 3-line Dict use).
    - **Heapq cluster (`heapqtest heapsort`):** "TypeGenerator: type variable v-32 is
      unresolved" + null-indirection panic at emission — same family as `heapqlisttest`'s
      v-111. Heapq IS concretely instantiated (`Heapq(string)`), so some reachable
      specialization still emits with a free tyvar.
    Both signatures are C1 territory (per-signature materialization / tyvar id-space), which
    is now the critical path for all six container tests. Original C-ENTRY rationale kept
    below for the record:
    `merge_stdlib` reachability idea (book ch13 §13.4) — load `dict`/`heapq` **only when the top
    program references `Dict`/`Heapq`**, instead of force-loading every builtin into every program
    (`parse/loader.rn:118-128 loadBuiltinModules`). Rationale: the uninstantiated-template
    emission crash (D1 Layer 2) only happens because `dict` is loaded into programs that never use
    it; if `dict` is present *only when used*, it is always instantiated → no free-var emission,
    and no need to solve general uninstantiated-template skipping first. Keep the relation
    TRANSFORMERS (`doublylinked hashed hashedclass` …) always-loaded (the compiler's own relations
    need them); make only the container CLASSES (`dict`, `heapq`) conditional. Cheap reachability:
    scan the top module's tokens/source for the identifiers `Dict`/`Heapq` before loading (coarse
    but sufficient; refine to true reachability later). Expected to unblock single-signature
    `in`/`dictitr`/`heapqtest`; `dicttest` (two `Dict` signatures in one program) additionally
    needs C1 per-signature mono. Gate at 188 zero-drops; verify the transformer-only builtins
    still load and the compiler self-builds. If conditional loading proves leaky, fall back to the
    general uninstantiated-template emission skip (must also cover HOISTED relation functions —
    the gap that made the naive genCConstructor guard fail, see D1 Layer 2).
- **Stage B′ — Declarative field pre-pass (GATED on D1; high risk):** only if D1 shows forward-ref
  is still blocking after C. Split field-name/type collection out of constructor execution into
  a Rune "Phase 1" that reads ctor self-assignments + relation-injected fields WITHOUT running
  the body, so `Dict`↔`Entry` resolves without re-entrancy. Validate against the compiler's OWN
  relations at every step. If infeasible without a broader inference refactor, document the Dict
  cluster as a known limitation at the Rune/Lyric frontier.
- **Stage D — unchanged, out of book scope.** Realistic migration ceiling ≈ **195/205**; the
  remaining ~10 are D-binding + representation, tracked as known limitations.

**Minor doc corrections:** (a) `ref`/`unref` are NOT absent from Lyric — it exposes raw
`ref`/`unref` behind a `trusted` modifier; Lyric drops the *safety guarantee*, not the ops.
(b) Lyric finalizes label-prefixed method names in a LATE `rewrite_impl_renames` pass (after
mono), not at desugar — a possible source of name issues if Rune resolves them early.

---

## Objective / definition of done

1. **Architecture:** transformer/relation expansion happens in a dedicated **desugar pass
   before typecheck**; generated members bind through the normal path; class names are
   pre-registered so forward refs resolve; generics specialize per-signature in an explicit
   monomorphization step.
2. **Tests:** green where binding-architecture is the cause — target ≥ **200/205** (the 4
   representation-only failures, §Stage D-repr, may stay out of scope). Never below the
   current **188/205**, zero drops, at any commit.
3. **Guarantees preserved:** Rune secret-type propagation (`secret(...)`/`reveal(...)`) and
   `ref`/`unref` ownership ops cover generated members **at least as well as today** — the
   desugar must add no bypass.

## Guardrails (must hold at EVERY commit)

- Branch `bootstrap`. Commit-on-green: commit after every clean, regression-free step.
  **Never commit a regression.** (See [[commit-on-green-steps]].)
- **Gate = the runtests stdout-diff suite, floor 188/205 zero drops.** `runtests.sh` builds
  the bootstrap with the legacy `../rune`, then the built `./bootstrap/rune` compiles each
  `tests/*.rn` (rm exe → compile → require `[ -x ]` → diff stdout). **Rune has NO Lyric-style
  stage2/stage3 byte-identical self-compile** — do not import that gate language (the
  workflow mapping did; corrected here). The bootstrap *building itself via `make`* is the
  integration check, because the typechecker dogfoods these very relations for its own
  registries (`typechecker.rn:341–345`).
- Keep secrets + memory safety. Any step touching generated-member binding re-runs the
  secret/`ref`-`unref` guardrail (theme E / §Stage A pre-reqs).
- Rune's transformer SURFACE stays (`transformer` + `prependcode`/`appendcode` + `$label`).
  Port Lyric's PIPELINE SHAPE, not its `interface`/`embed` syntax.

## Verified build/test recipe

```bash
cd /home/ah/src/rune/bootstrap && make && cd ..          # legacy ../rune rebuilds ./bootstrap/rune; run after ANY .rn / builtin edit
rm -f tests/<name> && ./bootstrap/rune tests/<name>.rn \   # bootstrap compiles by default; no -g
  && ./tests/<name> | diff - tests/<name>.stdout && echo PASS
./runtests.sh                                            # full suite; floor 188/205 zero drops
```

---

## Status snapshot (2026-06-30)

- **Stage A lift LANDED** (`f64d23b`): transformer/relation expansion extracted from
  `TypeChecker` into a standalone `Desugar` pass (`types/desugar.rn`) run before typecheck.
  Suite **188/205, zero drops** (structural, 0 new greens as predicted); guardrail fixture
  emits BYTE-IDENTICAL C (sha256 `dffd86..`). Pre-reqs landed first: `expr.copy()` isConst
  fidelity (`3bd03ce`), secret/ref-unref guardrail (`85fbbe8`).
- HEAD on `bootstrap`; suite **188/205**, zero drops. Stage 0 (named Blocker scaffolding,
  inert) + Stage 1 (destroy-SCC) landed; `recursiveDestructor` green.
- **17 remaining failures**, re-bucketed by this plan (note: the Dict cluster spans B **and**
  C — see §correction):
  - **Forward-ref + per-signature (Dict/Heapq), 6:** `dicttest dictitr in heapqtest heapsort
    heapqlisttest` — `in`/`heapqlisttest` likely green at **B**; the typecheck-failing
    `dicttest dictitr heapqtest heapsort` need **C** (per-signature materialization).
  - **Poly-recursion (mono worklist), ~3:** `turingTypeConstraints edwards2` (+`edwards`?
    edwards may be a separate C int→ptr codegen bug — verify) → **Stage C**.
  - **Stage D-binding, 4:** `gf2 integer` (param-merge generalization, `deferVars`/
    `PolymorphicType` ~`typechecker.rn:4892-4924`), `safe funcptr` (positive-var
    generalization, tyvar id-space). **UNMAPPED — needs its own analysis pass (§Stage D).**
  - **Stage D-representation (likely out of scope), 4:** `escapedCharTest uint2string
    allocfree defaultMethods` — length-prefixed strings / object-pool decisions, NOT binding.

---

## Step 0 — already done (this revision)

Workflow `wf_35efec1e-261` ran; findings folded in; critic corrections applied. The
**remaining gap** the critic flagged: **Stage D-binding (gf2/integer/safe/funcptr) has no
file:line mapping** — it was deliberately excluded from the 5 themes. Before executing
Stage D, run a focused analysis (a 6th theme) grounded in `BINDING_RESEARCH.md`'s
`deferVars`/param-merge section. A/B/C are execution-ready.

---

## Stage A — Desugar extraction (FOUNDATION, do next) — FAITHFUL LIFT

**Correction (critic):** this is a *faithful relocation*, NOT a redesign. Rune's AST copies
are **already deep** (`Function/Block/Statement/Expr/Variable.copy`, and `copyCodeBlockInto`
copies the container template BEFORE expanding — `typechecker.rn:3983/3989/4026/4033/4040`),
so there is **no cross-relation contamination to fix** — Stage A must *preserve* it. The
"adopt deep destructor copies" idea (and the claim that it subsumes the `hashed.rn`
clear-bucket lines) was **wrong** and is dropped. Verified: the whole expansion cluster
touches **zero typed state** (no unifier/resolve/Type/ClassInfo) — only db AST, Sym,
TransEnv, two registries — which is exactly why the lift is safe.

Likely greens **0 new tests** — exit criterion is structural.

### Pre-reqs (do first, separate commits)
- **Fix `expr.copy()` fidelity bugs** (`expr.rn:767-781`), since every transformer body is
  deep-copied through it: line 772 `self.isConst = self.isConst` is a no-op (should set
  `newExpr.isConst`); `datatype` is not copied (769-770 TODO); `val` is shared by reference
  (775 — aliases `Bigint.isSecret`). These silently drop const/secret state on copy. (Theme E
  step 2; also a secrets guardrail.)
- **Pin current behavior** with a guardrail fixture: a relation whose generated
  append/remove/destroy bodies contain `secret(...)`, explicit `ref`/`unref`, and a generated
  back-pointer field; snapshot emitted C + typechecker error stream as the byte-baseline.

### Work items (concrete — `WORKFLOW_FINDINGS.md` theme A steps 1–13)
1. New file `types/desugar.rn` with a `Desugar` class; add to `bootstrap/Makefile` SRC +
   `use desugar` in `types/package.rn`. (`types.Desugar()` reachable; no new rune.rn import.)
2. Move verbatim into `Desugar`: `TransVal`/`TransEnv` (`typechecker.rn:271-301`); the
   expansion cluster `registerTransformersIn`/`registerTransformer`/`lookupTransformer`
   (3735-3761), `executeTransformStatement`…`expandTransName` (3783-4192); the pure-AST
   helpers `synthesizeDestroy` (584-610), `hoistNestedClasses`+rewriters (813-886),
   `hoistNestedFunctions`+helpers (892-950); carry fields `transformerNames`/`transformerFns`
   (387-388), `hoistCounter` (372).
3. Promote `findClassFunction`/`In` (3764-3779) to a free `db.findClassFunction` (also used
   by `demandClass:688` and member access `:4704`); leave a one-line `TypeChecker` delegate.
4. Decouple `ArrayExtMethod`: registry STAYS on `TypeChecker` (consumed at `findClassIterator
   :3008`); `Desugar` collects into a public `arrayExtFns` list handed to `tc` before
   `tc.function`.
5. `Desugar` gets its own error sink (`desugarError`→`errors` list); surface in `rune.rn`
   mirroring `rune.rn:155-158`.
6. `Desugar.desugarModule(fn)` reproduces the CURRENT Module-arm order EXACTLY:
   `registerTransformersIn` → depth-first child-module recurse → `hoistNestedClasses` →
   `synthesizeDestroy` loop → `registerTransformer` loop → expansion loop over
   Transform/Relation/Appendcode/Prependcode → `hoistNestedFunctions`. Add a
   `statement.executed` idempotency guard for multi-import reachability.
7. Gut the `TypeChecker` Module arm: delete the 6 expansion sub-steps (`4201`, `4211-4245`);
   **KEEP** child-module binding recursion (4205-4209) and everything `4250+`
   (`registerFunctionDef`, global pre-bind, **operator-signature pre-registration 4279-4323
   which still scans the inert relation statements**, eager body loop, eager-destroy, module
   statements). Neutralize the `FuncType.Transformer` arm (4409-4411) to a no-op.
8. Wire `rune.rn:152`: `desugar = types.Desugar(); desugar.desugarModule(module)`; report+exit
   on `desugar.errors`; `for f in desugar.arrayExtFns { tc.registerArrayExtMethod(f) }`; then
   existing `tc.function(module)`.

### Exit criteria
- Suite still **188/205, zero drops**; bootstrap still builds itself (`make`).
- Typechecker no longer special-cases transformer expansion; generated members are ordinary
  pre-Check AST. Relation/Transform statements stay physically in the AST as inert no-ops
  (the operator-signature pre-registration still scans them — do NOT remove them).
- Guardrail fixture: emitted C for generated destroy bodies, `secret`/`reveal` folds,
  generated-field `noPrint` marking, and `ref`/`unref` placement are **byte-identical**
  pre/post-move.
- **Patch retirement — FINDING (not enabled by the lift):** The lift is byte-identical
  (guardrail proves emitted C unchanged), so it relocated *expansion* without touching
  *binding/emission mechanics*. The Stage-1/3 patches all live in the binding/emission path,
  NOT the moved expansion cluster: `ClassInfo.constructing` (`typechecker.rn:239/3745/3940/4070`
  — destroy-SCC re-entrancy guard, holds up `recursiveDestructor`), `noteDependency`
  (`expr.rn:2200`/`cbuilder.rn:435` — C-emit dep ordering), `deferredRetypecheck`/
  `retypecheckDeferred` (`cbuilder.rn:252/265/292/610/656` — deferral emission). Each is
  exactly as load-bearing as before the lift; removing any would regress the same tests it
  did pre-lift. **Patch retirement is gated on Stage B/C** (which actually change the binding
  path), not on Stage A. Re-evaluate after C.

### Regression surface (critic — the REAL fixtures)
The compiler's OWN relations are the load-bearing test, not just Hashed/Heapq:
`typeclasses.rn:823-834` (`Type` in ~12 simultaneous `OneToOne` relations), `rel.rn:26-27`,
`signature.rn:24-25`, `ident.rn:198`, `expr.rn:2826-2831`. Rune has **8 transformers**
(LinkedList, ArrayList, OneToOne, TailLinked, DoublyLinked, HeapqList, HashedClass, Hashed) —
verify faithful lift for ALL, not just Hashed. Plus `bootstrap/parse/exprTree.rn`
(`transformer ExprTree` on `HirBuilder` — the largest live transformer consumer; if it
mis-desugars the compiler won't self-build). **`make` after every desugar change.**

### Risks
Var-id / generated-identifier ordering perturbation (prior refactors regressed 187→184 from
changed generated names — `$label` expansion + `hoistCounter` must produce byte-identical
identifiers); `ArrayExtMethod` hand-off timing; error-sink swallowing; **do NOT touch
`builtin/hashed.rn:230-247,258`** (runtime mutual-cascade guard, unrelated to desugar).

## Stage B — Check Phase-0 pre-registration (forward refs) — partial Dict greens

Pre-register class NAMES before any constructor body binds, mirroring Lyric Check Phase 0.
**Key divergence (critic):** Rune discovers a class's fields by **executing its constructor
body** (`typechecker.rn:4480-4487`), so a name-only Phase-0 is NOT Lyric's field-bearing
Phase-1 — Rune's existing `demandClass` (on-demand body typecheck) is the substitute.

### Work items (`WORKFLOW_FINDINGS.md` theme B steps 1–5)
1. **Why null today:** Dict's HashedClass-injected `self.…_Table = arrayof(Entry)` is typed in
   the arrayof arm (`typechecker.rn:2560-2576`); `Entry` resolves to a bare `TypeName` via
   `namedType` (1610-1622) which does NOT register/demand; `lookupClass(Entry)` is null
   because the sole insert site `constructorFunction:4438` hasn't run for `Entry` (appended
   after Dict by `hoistNestedClasses:829`). The arrayof arm uniquely does NOT `demandClass`.
2. Add a **Phase-0 loop** before the eager body loop (`:4324`): for each Constructor child,
   if `lookupClass` is null, insert a **stub** `ClassInfo` (strct/selfType null,
   `constructing=false`). Classes are already flat (hoist ran at 4212).
3. Make `constructorFunction` **REUSE the stub** (`:4431-4438`: lookup-or-create, never
   double-insert) — the `ClassInfo`/`TynameDefinition` registries are HashedClass-backed, so
   a second insert prepends a DUPLICATE to the bucket and `classInfos()` double-visits.
   **Mandatory, not optional.**
4. Fix the **`demandClass` guard** (`:684-687`): today `lookupClass-non-null` doubles as the
   done/re-entrancy guard; with stubs everywhere it would block all on-demand typechecking.
   Distinguish three states: in-progress (`info.constructing`, window 4439..4634) → return;
   done (`classFn.typedValue` non-null) → return; stub → proceed. **Load-bearing for
   mutual-cascade re-entrancy** — `constructing` must mean "leave alone."
5. If tracing shows Dict's relation-injected methods need `Entry`'s CONCRETE fields (not just
   nominal `[Entry]`) while Dict binds, add `demandClass(Entry)` in the arrayof arm (~2569),
   no-op when the target is currently constructing.

### Exit
`in` + `heapqlisttest` green (single-signature). Suite ≥ prior, zero drops, self-builds.
The typecheck-failing 4 (`dicttest dictitr heapqtest heapsort`) move to Stage C. Risk:
stub `ClassInfo` has null `selfType`, which several sites read as "still typechecking"
(`classInstanceType:570`, `constraintType:1661`, method demand `:4764`) — confirm each still
demands rather than baking an arity-0 instance for generic `Dict<K,V>`. Coordinate with the
Stage-0 `ClassRegistered` blocker at the arrayof arm (2572) — Phase-0 makes it never fire.

## Stage C — Per-signature + iterative monomorphization (Dict rest + poly-recursion)

Two sub-mechanisms over the EXISTING `PolyInstantiations` + `retypecheckDeferred` hooks (no
rewrite). (`WORKFLOW_FINDINGS.md` theme C + theme D-dict.)

- **Step 0 (de-risk):** extract the **9 duplicated** `if deferredTypecheck &&
  !deferredRetypechecked {…}` blocks (`function.rn:468,890,774,788`; `cbuilder.rn:252,292,
  613,659`) into one `emitDeferredRetypecheck(fn, specKey)`.
- **C1 — Dict per-signature (fixes `dicttest` v54 leak):** replace the **one-shot
  `deferredRetypechecked` boolean** with per-specialization dedup against `definedSignatures`
  (`typechecker.rn:384`), keyed on `genSpecializationName` (`typeclasses.rn:1086`) under
  active bindings — so `Dict(u32,string).remove` retypechecks under its OWN bindings instead
  of reusing `Dict(string,u32)`'s baked body. Implement `materializeInner(Dict,K,V)` via
  `classInstanceType` (562) + `demandClass` (684) + `substituteClassVars` (4832); cache
  `Entry<K,V>` in `definedSignatures` BEFORE binding its body (breaks the Entry↔Dict knot);
  drive from the pre-emission pass (`cbuilder.rn:239-263`), NOT phase-5. **Per-signature
  field-type isolation:** `genCConstructorInstance` persists field types into the SHARED
  `info.strct.fieldtypes` (`function.rn:613`) — re-resolve per emission or key by specName so
  `Dict(string,u32)` and `Dict(u32,string)` keep distinct Entry field types.
- **C3 — poly-recursion (turing/edwards2):** add an explicit `MonoItem{fn,inst,specName,
  depth}` worklist on CBuilder + a named `RECURSION_LIMIT` const (**must trip below the
  `resolveDepth` 64 bound** — there is no occurs check; `occursIn` exists at
  `typeunifier.rn:203-256` but is **unwired** from `unify`). Driver `monomorphize(module)`
  between `resolveNestedInstantiations` (`cbuilder.rn:231`) and `genC` (:316); reuse
  `alreadyEmitted`/`markEmitted` as the visited set. Fix the **no-op root**
  (`typechecker.rn:1193` `if isnull(val)`): in the call-site router (`expr.rn:2620-2733`),
  when a recursive call's resolved arg types differ from the emitting frame, route to a
  FRESHLY-OPENED instantiation (`openPoly:1153`) as a new MonoItem instead of matching the
  already-bound poly. Symmetric `deInstantiate` per item (no frame leakage).

### Exit
`dicttest dictitr heapqtest heapsort` green at C1; `turingTypeConstraints edwards2` at C3.
Each commit suite-gated ≥188 zero drops, self-builds. **Top risk:** iterator-inlining
AST-mutation — `retypecheckDeferred` re-walks `bodySnapshot` (628-635) and now runs N× per
fn; inlining mutates `subBlock` (`InlinedBlock:3560`); confirm idempotence or clear inlined
nodes between signatures (gates C1). Keep `genSpecializationName` string-equality sound as
the dedup key (or switch to structural `TyvarInstantiation` equality).

## Stage D — Leak/policy/representation (remaining ≤8) — D-binding UNMAPPED

- **D-binding (4) — NEEDS A DEDICATED ANALYSIS PASS:** `gf2 integer` (two independent type
  params merge into one bound var — `deferVars`/`PolymorphicType` ~`typechecker.rn:4892-4924`)
  and `safe funcptr` (positive-var generalization, tyvar id-space). The 5 workflow themes
  EXCLUDED these. Run a 6th analysis theme grounded in `BINDING_RESEARCH.md` §3 before coding.
- **D-representation (4, likely out of scope):** `escapedCharTest uint2string allocfree
  defaultMethods` — length-prefixed strings / object-pool decisions, not binding. Decide
  per-test; document any deferral in a Known-Limitations note.

---

## Sequencing & commit discipline

A (pre-reqs → lift → patch-retirement) → B → C (C0 → C1 → C3) → D (analyze D-binding first).
Each step independently committable and suite-gated at the 188 floor. After Stage A lands
green, update this snapshot and re-point `IMPLEMENTATION_PLAN.md` here.

## Open decisions to settle at stage boundaries

- **A/B boundary:** keep faithful per-module desugar order, OR switch to Lyric-style
  whole-program phasing (each sub-pass across all modules) — the latter de-risks Phase-0 but
  changes visitation order under the 188 gate. Recommend faithful for A, decide at B.
- **B:** name-only stub vs `demandClass(Entry)` in the arrayof arm — answered by tracing
  whether Dict's relation methods touch `Entry`'s concrete fields during Dict's own binding.
- **C:** `RECURSION_LIMIT` value + central-constant home; whether `monomorphize(module)` is a
  new pass or replaces the two deferred-retypecheck sweeps (`cbuilder.rn:239-315`).

## Doc map

- This file — execution roadmap (how to finish).
- `WORKFLOW_FINDINGS.md` — full Rune-grounded steps (5 themes) + completeness critic.
- `RELATIONS_DESUGAR.md` — design synthesis + Lyric evidence (why this shape).
- `BINDING_DESIGN.md` / `BINDING_RESEARCH.md` — research, direction C, A1–A7.
- `IMPLEMENTATION_PLAN.md` — failure triage + Stage 0–1 history (stage structure superseded).
- `CODE_REVIEW_CHECKLIST.md` — per-commit review loop.
- `books/` (gitignored) — The Lyric Book + `_extracted/` chapter text the workflow read.
