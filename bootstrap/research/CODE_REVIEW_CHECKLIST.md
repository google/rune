# Stage 0–1 Code-Review Checklist (binding fix → recursiveDestructor green, 188/205)

Loop target. Review each **code** commit below for correctness, edge cases, scope/regression
risk, and accuracy of comments. Docs commits need no code review (listed for completeness).
Mark each `[x]` when reviewed and write the verdict + findings inline. **Do NOT fix bugs
mid-loop** — record them under "Findings to address" at the bottom and batch-fix after the loop.

Branch `bootstrap`, baseline before this effort = `ce4c684` (187/205), HEAD after = `3311a00`
(188/205, zero drops). Full context: `IMPLEMENTATION_PLAN.md` (status, triage, Stage 3 bug chain).

## How to review one commit (the loop body)
1. `git show <hash>` — read the whole diff.
2. **Correctness:** does the code do what the message claims? Null/empty handling, off-by-one,
   ordering, force-unwraps (`!`), unify-error handling. Trace the intended scenario by hand.
3. **Scope / regression risk:** the dangerous ones touch HOT, GENERIC paths — note any commit
   whose change fires for inputs beyond the intended case:
   - `commonMemberFieldType` → every ambiguous duck-typed field access.
   - `noteDependency` → every method-call emission.
   - `methodCallType` provisional + `ClassInfo.constructing` → every destroy cross-call.
   - the `genCMethodInstance` retypecheck/emitting-name → every class-method emission.
   - the `null()` arity change → every `null(Class)`.
   - `builtin/hashed.rn` cascade reorder → every Hashed cascade-delete relation.
4. **Net-at-HEAD check:** some commits were later modified. ESPECIALLY `62840e9` (its fresh-var
   was REPLACED by `commonMemberFieldType` in `5356723`) — review the code AS IT IS AT HEAD, not
   just the historical diff. Use `git show HEAD:<file>` to see the live version.
5. **Comments:** are the inline comments still accurate after later commits?
6. Record verdict: ✅ clean / ⚠️ concern (note it) / 🔴 bug (note it). Append findings below.
7. Check the box, then continue to the next unchecked CODE commit.

## Verification commands (re-derive, don't trust memory)
```bash
cd /home/ah/src/rune
# rebuild after ANY bootstrap-source or builtin/hashed.rn edit:
cd bootstrap && make && cd ..
# one test (bootstrap compiles by default; rm first, gate on [ -x ]):
rm -f tests/recursiveDestructor && ./bootstrap/rune tests/recursiveDestructor.rn \
  && ./tests/recursiveDestructor | diff - tests/recursiveDestructor.stdout && echo PASS
# full suite harness lives in $TMPDIR/btest.sh (rm exe, compile, require [ -x ], diff stdout)
```

---

## CODE commits to review (chronological)

- [x] **`53a16dd`** — Stage 0: named `Blocker`/`BlockerKind`/`Deferred` + deferral worklist (typechecker.rn).
  Additive scaffolding, NOT YET CONSUMED at this commit — confirm it's truly inert (no behavior
  change) and the 5 recording sites + Arrayof producer are side-effect-free.
  Verdict: ✅ clean. Truly inert: the three new fields are only declared/appended, never read at
  this commit; the 5 sites (VarBound×4 @2287/2313/2511/3689, SignatureKnown @4727) + Arrayof
  producer @2571 only allocate + append. `recordVarBlocker`'s extra `resolve()` is total and
  idempotent (Polymorphic arm returns unchanged, no assert; at worst path-compresses), and the lone
  force-unwrap `leftChild.typedValue!` @2287 is already proven non-null by @2278 in the same chain.

- [x] **`85e6569`** — destroy-SCC: `ClassInfo.constructing` flag + `methodCallType` provisional
  `self→none` + `ClassInfo.expectedParamArity` (typechecker.rn).
  Scrutinize: is `constructing` set/cleared on EVERY exit path of `constructorFunction` (incl. the
  early-return and the deferral path)? Does the provisional only fire for destroys mid-construction
  (not other methods)? Does `expectedParamArity` ever over/under-count vs the real instance arity?
  Verdict: ✅ clean. `constructing` correctly paired: set true @4439 (after the already-typechecked
  early-return @4426, before any work), cleared @4634; NO `return` between them and the deferral path
  @4488-4511 falls through (only sets `deferredTypecheck`), so only a fatal raise could skip it.
  Provisional fires only on `isnull(mty) && name=="destroy" && constructing` (non-destroys short-
  circuit; `unify` null-guarded). `expectedParamArity` is computed by the same `variables()`/`!=self`
  loop that fills `paramVars` with no body run between, so it == final `paramVars.length()`; `null()`
  takes `max(len, arity)` (identical for built classes → no `null(Class)` regression) and the `>=0i64`
  guard blocks the -1 sentinel from the u64 cast. Note: provisional relies on the eager-destroy pass
  to later walk every deferred destroy body (by design; suite stable, zero drops).

- [x] **`62840e9`** — fresh var for ambiguous duck-typed field (typechecker.rn).
  **SUPERSEDED:** at HEAD the fresh var is a fallback behind `commonMemberFieldType` (added in
  `5356723`). Review the LIVE branch at HEAD: does the fallback still fire correctly when types
  disagree, and does it still set `sawStructuralGenericError`/defer as intended?
  Verdict: ✅ clean (net-at-HEAD @2509-2520). The fresh-var now sits in the `else` (genuine-ambiguity)
  branch reached exactly when `commonMemberFieldType` returns null — i.e. when types disagree / are
  non-concrete — so it still fires correctly there, and still sets `sawStructuralGenericError = true`
  + `recordVarBlocker(sty)` so the access defers and re-resolves concretely. Yielding a fresh tyvar
  (not none) is strictly more deferrable than the old null; ordering vs `typeError` is irrelevant. No
  regression.

- [x] **`5356723`** — Stage 3: relation-method emission + cascade recursion (188 green).
  Five sub-changes, review each:
  - [x] `commonMemberFieldType` (typechecker.rn) — agreement check via `toString()`; concrete-only
    via `hasFreeVars`. Is `toString()` a sound equality proxy? Any class with a same-named field of
    a DIFFERENT type that this would wrongly unify? Generic classes excluded correctly?
    ⚠️ `toString()` IS sound for concrete types (`IntType.toString` emits `u%/i%`+width, so u64≠i64≠u32;
    differing types bail via the string compare), and `hasFreeVars(rfty)` correctly excludes any
    field with a free var (generic `T`-typed field bails; a generic class's CONCRETE `hash:u64`
    field correctly contributes). BUT scope gap: it consults only `strct` fields, so when ≥2 classes
    share a name as a FIELD in one and a METHOD in another, it returns the field type and pins the
    access (no defer) — wrong if the unconstrained receiver is actually the method-bearing class. See
    Findings. (Not hit by the suite; the prior fresh-var/defer path was safe here.)
  - [x] deferral-path `unify(monoResult, returnType)` (typechecker.rn) — mirrors non-deferred path;
    confirm it can't bind a result that should stay polymorphic.
    ✅ Mirrors the non-deferred `unify(monoResult, returnType!)` @5125. After the unify, `resolve(monoResult)`
    is checked for `Var` and only then added to `deferVars`, so a genuinely polymorphic result stays
    generalized; a concrete `none` is correctly NOT generalized. The `!isnull(returnType)` guard is a
    safe superset of the non-deferred path (which force-unwraps). Can't over-bind.
  - [x] `genCMethodInstance` retypecheck + emitting fn/name save/restore (function.rn) — restore on
    ALL exit paths? once-only `deferredRetypechecked` interaction with the cbuilder pre-emission pass?
    ✅ Mirrors `genCPolyInstantiation` @462-477 exactly. The two early `return`s (@861/864) precede the
    save @886; restore @942-943 is unconditional with no `return` between (only a fatal raise could
    skip it, same as the sibling openScope/tryContext saves). Once-only via the shared
    `deferredTypecheck && !deferredRetypechecked` flag: it participates in the same protocol as the
    pre-emission pass (cbuilder.rn:253-306) — whichever reaches the method first retypechecks+marks,
    the rest skip. (Doesn't manage `currentEmittingResultWide` like the poly path, but the method path
    never did — pre-existing, not introduced here.)
  - [x] `noteDependency` (expr.rn, cbuilder.rn) — `appendIfOpen` guard correct at module level?
    **KNOWN GAP (see triage):** only the METHOD-call path registers deps; plain-function and
    constructor calls do not — confirm and note (this is the `safe` lead).
    ✅+gap. `appendIfOpen`'s `!isnull(lastDependencyList)` guard correctly skips module-level calls
    (no open list; top-level code emitted in main() after decls). The recorded `name` is the MANGLED
    C decl name (`methodCallCName`), so it matches the emitted function; noted unconditionally per call.
    CONFIRMED GAP: the sole caller is expr.rn:2200 (method calls); plain-function and constructor
    calls register no deps — see Findings (the `safe` lead).
  - [x] `builtin/hashed.rn` clear-bucket-before-recurse — does clearing `table[x]` before the while
    loop ever drop entries in the non-mutual case? (chain still walked via local head.)
    ✅ No drop. The line is byte-identical to the old post-loop reset, just MOVED before the while; the
    local head `$labelB$B_Entry` was grabbed the line above, so the chain is still fully walked via the
    local + `next` pointers. Correctly uses `null(entry)` WITHOUT `!` (head may be null on an empty
    bucket; the `!` form stays only on the while-guarded in-loop line). Mutual case now terminates.
  Verdict: ✅ green (with one ⚠️ scope concern + one confirmed known gap, both in Findings). Four
  sub-changes are clean; `commonMemberFieldType` has a narrow field-vs-method unsoundness, and
  `noteDependency` covers only method calls (the tracked `safe` lead). Neither is hit by the suite.

---

## DOCS commits (no code review needed — reference only)
`e76b826`, `f359f9e`, `cab7218`, `42976ec`, `d2badd3`, `704fc60`, `3311a00` — all
`bootstrap/research/*.md` / `BINDING_RESEARCH.md`. Skim only if verifying a claim.

## Prior-session groundwork (OUT OF SCOPE for this loop unless we choose to extend)
`ce4c684`, `28191c5` (builtin/hashed.rn), `bbebe84` (typechecker.rn), `6ce8614`, `379b82e`
(database/expr.rn) — shipped before this effort, already part of the 187 baseline. Review later
only if a Stage-2/3 bug points back at them.

---

## Findings to address (batch-fix AFTER the loop)
**REVIEW COMPLETE — all 4 CODE commits + 5356723's 5 sub-items reviewed; suite-stable. No 🔴 bugs; 2 noted items below (1 ⚠️ scope, 1 known gap), neither hit by the current suite.**

- ⚠️ **`commonMemberFieldType` field-vs-method unsoundness** (typechecker.rn ~530, 5356723). It consults
  only `strct` data fields. When ≥2 classes share a member name as a FIELD in one class and a METHOD in
  another, `findClassByMember` returns null (ambiguous) → `commonMemberFieldType` returns the field's
  concrete type and the access is pinned with NO deferral. If the unconstrained receiver is actually the
  method-bearing class, `child.m` is typed as the field's type instead of the method. Not triggered by the
  suite (188/205), and the pre-5356723 fresh-var/defer path handled it safely. Fix idea: also bail (return
  null) if any class declares the name as a METHOD, not just on field-type disagreement.
- 📌 **`noteDependency` covers only method calls** (expr.rn:2200, 5356723 — the tracked `safe` lead). Plain-
  function and constructor call emissions register no dependency, so the C emitter won't order/forward-
  declare a cycle that runs through a plain function or constructor. Confirmed: the only caller is the
  method-call path. Already noted in IMPLEMENTATION_PLAN.md:43; extend coverage to plain/constructor calls
  when a non-method emission cycle surfaces.
