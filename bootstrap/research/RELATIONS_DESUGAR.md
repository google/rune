# Relations as a Desugar Pass — Adopting Lyric's Architecture

Status: design synthesis (2026-06-30). Confirms the maintainer's "treat relations as
syntactic sugar, expand right after parsing" direction against the authoritative source:
**The Lyric Book** (Bill Cox & CodeRhapsody), downloaded to `bootstrap/research/books/`
(gitignored). Companion to `BINDING_DESIGN.md` and `IMPLEMENTATION_PLAN.md`.

**Constraint (non-negotiable):** Rune KEEPS its secret types and its memory-safety
guarantee. Lyric dropped both (secrets = "research goal, not design tool"; memory safety
= deferred to an unbuilt checker, with an admitted use-after-free gap, Lyric Book Ch 11
§11.4). We adopt only Lyric's **relation/pipeline architecture**, never its omissions.

---

## 1. What Lyric actually does (verified, Lyric Book Ch 8 + Ch 14)

**Pipeline (Ch 14 §14.1, `compile_pipeline` in `src/main/main.ly`):**
Parse → Merge files → Resolve imports → Merge stdlib → **(5) Desugar** → **(6) Check** →
Lower → Optimize → **(9) Monomorphize** → validate-post-mono → rewrite-impl-renames →
slab-rewrite → Emit C.

- **Desugar runs BEFORE Check.** The typechecker never sees a `relation`; it only sees the
  ordinary generated members. Ch 8: *"When `relation ArrayList Team:roster owns
  [Player:team]` binds Team as P and Player as C, **the desugar pass copies this method
  onto Team** with the parent label as prefix — so `Team.append` becomes
  `Team.roster_append`."*
- **Desugar is 6 sub-passes in a fixed, load-bearing order** (Ch 14 §14.3, 1,534 lines):
  `InterfaceEmbeds → InterfaceFields → FieldAccess → Relations → Destructors →
  DefaultImpls`. Each pass emits AST that later passes depend on.
- **The load-bearing insight:** *"destructor copies must be deep to prevent cross-relation
  contamination when method names are renamed."* (This is precisely the bug class we've
  been fighting in the `$label`-substituted cascade in `builtin/hashed.rn`.)
- **Check is 4-phase, each phase completes across ALL blocks before the next begins**
  (Ch 14 §14.3, 4,919 lines): Phase 0 pre-registers all type names (forward + cross-file
  refs) → Phase 1 fills full TypeInfo → Phase 1.5 binds interface/impl methods onto
  concrete classes (label-prefixed names) → Phase 2 checks bodies. *"This is what makes
  forward references and cross-file references work."*
- **Monomorphize is a separate, iterative pass** (Ch 14 §14.3, 3,939 lines): per-signature
  specialization (`identity<i32>` → `identity_i32`); *"specializing a function may reveal
  new generic calls in its body. In practice, it converges in two or three iterations."*
- Relations are **not compiler builtins** — the four container types (ArrayList,
  OwningList, RefList, HashedList) are stdlib `interface`s. *"The `relation` keyword and
  the field/destructor/embed machinery are the builtins."* (Ch 8 §8.4.)

## 2. Why this is exactly our research

Lyric independently arrived at the three-tools-for-three-problems decomposition that
`BINDING_DESIGN.md` recommended:

| Lyric pass | BINDING_DESIGN piece | Our failure cluster |
|---|---|---|
| Desugar-before-Check | A6 generate-first / bind-combined | recursiveDestructor / mutual-recursion (cluster 3) |
| Check Phase 0 pre-registers all names across all blocks | order-independent forward refs | Dict↔Entry (cluster 1) |
| Separate iterative Monomorphize pass | direction-C per-sig mono + A5 poly-recursion | Dict + turing/edwards2 (clusters 1 & 2) |
| rewrite-impl-renames | label-prefixed method resolution | (the `$label` machinery) |

The desugar is the FRONT half; Phase-0 pre-registration and the mono pass are the back
half. Lyric having all three confirms: do not bet the remaining failures on one mechanism.

## 3. Where Rune is today (the gap)

- Rune expands transformers **inside** the typechecker: `executeTransformStatement`
  (`typechecker.rn:4234`) is driven from the per-module `function()` (`:4195`), in one
  forward source-order pass, interleaved with binding. There is **no** Phase-0
  pre-registration of generated members and **no** separate monomorphization pass.
- Surface differs and **stays**: Rune = `transformer` + `prependcode`/`appendcode` +
  `$label` string-template substitution (`builtin/hashed.rn`); Lyric = `interface` +
  `embed` + `field`/`destructor` generics. We adopt the **pipeline shape**, not Lyric's
  relation surface syntax. (A surface migration is a separate, much larger language change.)

## 4. Secrets + memory safety: desugar is PRO-safety

Moving generated members to ordinary AST *strengthens* Rune's guarantees rather than
eroding them: today the generated destroy/relation methods are bound **specially and late**
(the Stage-1/3 patches), which is exactly where secret-propagation or safety checks risk
inconsistent coverage. As ordinary AST emitted before Check, Rune's existing secret-type
propagation and `ref`/`unref` memory-safety machinery cover them **uniformly**, like
hand-written code. The generated cascade code already uses `ref`/`unref`; preserve it
verbatim. No change to Rune's secret or safety semantics is implied or required.

## 5. Proposed staged adoption (incremental, commit-on-green, protect 188)

**Stage A — Desugar extraction (the foundation).**
Lift `executeTransformStatement` / `interpretTransBlock` / `instantiateCodeBlock` /
`copyCodeBlockInto` / `evalTransExpr` out of `Typechecker` into a standalone desugar pass
run **after** `hir.rn` build and **before** `function()` typecheck. Feasible because the
expansion already consumes only names + string/bool params — never a typed result.
- Preserve current ordering deps: `hoistNestedClasses` + `synthesizeDestroy` must run
  before/within desugar so `A.destroy` targets and `Outer.Inner` names resolve.
- Adopt Lyric's **deep destructor copies** to kill cross-relation contamination
  structurally (likely subsumes the `builtin/hashed.rn` clear-bucket workaround).
- The `appendcode Array` path that side-registers ext-methods (`registerArrayExtMethod`)
  becomes *simpler*: inject as AST, let normal registration pick it up.
- Gate: suite ≥188 zero drops. **Enables** retiring the Stage-1/3 patches (`constructing`
  flag, `methodCallType` provisional, `noteDependency`, `genCMethodInstance` retypecheck)
  as generated members now bind through the normal SCC + emission path — verify and remove
  each individually, re-running the suite per removal.

**Stage B — Check Phase-0 pre-registration (Dict forward refs).**
Pre-register all class/type names (including generated `Entry`) before binding any bodies,
so `lookupClass(Entry)` is non-null at the table-field typecheck. Mirrors Lyric Check
Phase 0. (Was old Stage 2.)

**Stage C — Iterative monomorphization pass (Dict per-sig + turing poly-recursion).**
Dedicated post-Check specialization to a fixpoint with a recursion-depth guard; fresh
frame per `addParens_N_M`. Mirrors Lyric Monomorphize. (Was old Stage 3.)

**Stage D — leak/policy.** gf2/integer generalization bug; safe/funcptr positive-var
generalization. Orthogonal to pipeline shape. (Was old Stage 4.)

## 6. Open questions before cutting Stage A

- How tightly is the desugar logic coupled to `Typechecker` state it can't move
  (`typeError` diagnostics — fine; `findClassFunction` — walks db AST by name, movable;
  `registerArrayExtMethod` — becomes AST injection)?
- Does any current test rely on a transformer body observing a typechecked result? (Survey
  says no — expansion is names + literals only — but confirm before relying on it.)
- Sequencing vs the committed Stage plan: Stage A is the NEW foundation that de-risks B/C;
  B/C correspond to old Stage 2/3. Confirm we re-baseline `IMPLEMENTATION_PLAN.md` around
  this once Stage A lands green.
