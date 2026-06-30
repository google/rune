# Finishing the Relations → Desugar Migration — Execution Plan

Authoritative execution roadmap to complete the bootstrap binding fix by adopting Lyric's
relation/desugar/monomorphization architecture **while keeping Rune's secret types and
memory-safety guarantee**. Re-baselines the stage structure of `IMPLEMENTATION_PLAN.md`
around the Lyric-confirmed design in `RELATIONS_DESUGAR.md`.

Companion docs: `RELATIONS_DESUGAR.md` (design synthesis + evidence), `BINDING_DESIGN.md`
(research, the A1–A7 areas + direction C), `IMPLEMENTATION_PLAN.md` (failure triage, Stage
0–1 history), `CODE_REVIEW_CHECKLIST.md` (review loop).

---

## Objective / definition of done

1. **Architecture:** transformer/relation expansion happens in a dedicated **desugar pass
   before typecheck**; generated members bind through the normal path; no special-case
   late-binding scaffolding. Forward refs resolve via name pre-registration; generics
   specialize in an iterative monomorphization pass.
2. **Tests:** full bootstrap suite green where binding-architecture is the cause —
   target ≥ **200/205** (the 4 representation-only failures, §Stage D, may stay out of
   scope). Never below the current **188/205**, zero drops, at any commit.
3. **Guarantees preserved:** Rune secret-type propagation and `ref`/`unref` memory safety
   cover generated members **at least as well as today** (the desugar must not open a hole).

## Guardrails (must hold at EVERY commit)

- Branch `bootstrap`. Commit-on-green: commit after every clean, regression-free step.
  **Never commit a regression.** (See [[commit-on-green-steps]].)
- Gate every step on the full suite: `rm -f` each exe, compile, require `[ -x ]`, diff
  stdout. Baseline floor = **188/205, zero drops**.
- Keep secrets + memory safety. Any step that touches generated-member binding re-runs the
  secret/safety guardrail check (workflow theme E).
- Rune's transformer SURFACE stays (`transformer` + `prependcode`/`appendcode` + `$label`).
  We port Lyric's PIPELINE SHAPE, not its `interface`/`embed` syntax.

## Verified build/test recipe

```bash
cd /home/ah/src/rune/bootstrap && make && cd ..          # rebuild after ANY .rn / hashed.rn edit
rm -f tests/<name> && ./bootstrap/rune tests/<name>.rn \   # bootstrap compiles by default; no -g
  && ./tests/<name> | diff - tests/<name>.stdout && echo PASS
# full suite harness: rm exe → compile → require [ -x ] → diff stdout (bootstrap exits 0 on type errors)
```

---

## Status snapshot (2026-06-30)

- HEAD on `bootstrap`; suite **188/205**, zero drops. Stage 0 (named Blocker scaffolding)
  + Stage 1 (destroy-SCC) landed; `recursiveDestructor` green.
- **17 remaining failures**, bucketed by this plan's stages:
  - **Stage B (Dict/forward-ref), 6:** `dicttest dictitr in heapqtest heapsort heapqlisttest`
  - **Stage C (poly-recursion mono), 3:** `turingTypeConstraints edwards edwards2`
  - **Stage D-binding, 4:** `gf2 integer` (param-merge generalization), `safe funcptr`
    (positive-var generalization)
  - **Stage D-representation (may stay out of scope), 4:** `escapedCharTest uint2string
    allocfree defaultMethods`
- Lyric-extraction workflow `wwsd6ixgr` (run `wf_35efec1e-261`) is producing the file:line
  implementation detail + a completeness-critic gap list — **fold in as Step 0 below.**

---

## Step 0 — Fold in the workflow findings (do FIRST, after compaction)

Before cutting code: read the completed workflow result (`extracted` book corpus,
`mapped` 5 Rune sub-plans, `critic` gaps). Then:
1. Append the critic's gaps / unverified-claims / additional-reading to this file.
2. Drop each theme's `concreteSteps` + `filesAndFunctions` into the matching Stage below.
3. Resolve any contradiction between a workflow finding and a Stage premise BEFORE starting
   that Stage. If the critic flags an under-covered region, re-read it.

---

## Stage A — Desugar extraction (FOUNDATION, do next)

**Why first:** removes the special-case late-binding that the Stage-1/3 patches work around,
and is the substrate B and C build on. Likely greens **0 new tests** by itself — its exit
criterion is *structural*, not a new pass.

**Work items** (refine with workflow theme A):
- Move `executeTransformStatement` / `interpretTransBlock` / `instantiateCodeBlock` /
  `copyCodeBlockInto` / `evalTransExpr` out of `Typechecker` (`typechecker.rn:3735–3970`,
  driver at `:4234`) into a standalone **desugar pass run after `hir.rn` build, before
  `function()` typecheck.**
- Decouple from typechecker state: `typeError` (keep as diagnostics), `findClassFunction`
  (walks db AST by name — movable), `registerArrayExtMethod` (becomes AST injection — the
  `appendcode Array` path gets *simpler*).
- Preserve ordering deps: `hoistNestedClasses` + `synthesizeDestroy` run before/within
  desugar so `A.destroy` targets and `Outer.Inner` names resolve. Mirror Lyric's fixed
  sub-pass order where it maps.
- Adopt **deep destructor copies** (Lyric Ch 14 §14.3: "destructor copies must be deep to
  prevent cross-relation contamination when method names are renamed"). Verify this
  subsumes the `builtin/hashed.rn` clear-bucket-before-recurse workaround; if so, revert it.

**Exit criteria:**
- Suite still **188/205, zero drops**.
- Typechecker no longer special-cases transformer expansion; generated members are ordinary
  pre-Check AST.
- **Retire the Stage-1/3 patches one at a time, re-running the suite after each removal:**
  `methodCallType` destroy provisional, `ClassInfo.constructing`, `noteDependency`,
  `genCMethodInstance` retypecheck dance. (A patch that can't be removed without a drop is a
  finding — record why it's still load-bearing.)
- Secret/safety guardrail check passes (theme E).

**Risks:** perturbing var-id ordering (broke generic classes in prior pre-seed attempts —
see `IMPLEMENTATION_PLAN.md`); the `appendcode Array` registration path; self-hosting
transformers (`bootstrap/parse/exprTree.rn` runs `transformer ExprTree` on `HirBuilder`) —
the desugar must run over the bootstrap's OWN source too.

## Stage B — Check Phase-0 pre-registration (Dict forward refs → 6 tests)

Pre-register all class/type names (incl. a generated inner `Entry`) before binding any body,
mirroring Lyric Check Phase 0 ("each phase completes across all blocks before the next").
Dissolves "`lookupClass(Entry)` is null at the table-field typecheck" (`typechecker.rn`
Arrayof arm ~2416). Targets `dicttest dictitr in heapqtest heapsort heapqlisttest`.
Refine with workflow themes B + D. Exit: those 6 green, suite ≥ prior, zero drops.

## Stage C — Iterative monomorphization pass (poly recursion → 3 tests)

Dedicated post-Check specialization to a fixpoint with a recursion-depth guard; fresh frame
per `addParens_N_M`. Mirrors Lyric Monomorphize (converges in 2–3 iters). Maps onto existing
`PolyInstantiations`/deferral/`retypecheckDeferred` hooks. Targets `turingTypeConstraints
edwards edwards2`. Refine with workflow theme C + `BINDING_DESIGN.md` A4/A5. Exit: those 3
green, zero drops.

## Stage D — Leak / policy / representation (remaining ≤8)

Orthogonal to pipeline shape; tackle after A–C:
- **Binding-generalization bugs (4):** `gf2 integer` (two independent type params merge into
  one bound var — `deferVars`/`PolymorphicType`); `safe funcptr` (positive-var
  generalization, tyvar id-space). 
- **Representation (4, may stay out of scope):** `escapedCharTest uint2string allocfree
  defaultMethods` — length-prefixed strings / object-pool decisions, NOT binding. Decide
  per-test whether in scope for this migration; document any deferral in Known-Limitations.

---

## Sequencing & commit discipline

A → B → C → D. Each Stage is independently committable and gated on the suite floor. Within
a Stage, commit each clean sub-step. After Stage A lands green, update this file's status
snapshot and re-point `IMPLEMENTATION_PLAN.md` here for the stage structure.

## Risk register

- **Var-id ordering perturbation** → generic-class emission breakage. Mitigation: change
  expansion *timing* without changing var allocation order; diff-test generic tests
  (classheapsort, symtest) every step.
- **Self-hosting transformers** (`exprTree.rn`) must desugar correctly or the compiler won't
  build itself. Mitigation: build bootstrap (`make`) after every desugar change — it's the
  largest transformer consumer.
- **Patch removal regressions** in Stage A: remove one patch per commit, suite-gated.
- **Secret/safety coverage**: re-run guardrail check whenever generated-member binding moves.

## Doc map

- This file — execution roadmap (how to finish).
- `RELATIONS_DESUGAR.md` — design synthesis + Lyric evidence (why this shape).
- `BINDING_DESIGN.md` / `BINDING_RESEARCH.md` — research, direction C, A1–A7.
- `IMPLEMENTATION_PLAN.md` — failure triage + Stage 0–1 history (stage structure superseded
  here).
- `CODE_REVIEW_CHECKLIST.md` — per-commit review loop.
- `books/` (gitignored) — The Lyric Book + `_extracted/` chapter text the workflow read.
