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

- [ ] **`85e6569`** — destroy-SCC: `ClassInfo.constructing` flag + `methodCallType` provisional
  `self→none` + `ClassInfo.expectedParamArity` (typechecker.rn).
  Scrutinize: is `constructing` set/cleared on EVERY exit path of `constructorFunction` (incl. the
  early-return and the deferral path)? Does the provisional only fire for destroys mid-construction
  (not other methods)? Does `expectedParamArity` ever over/under-count vs the real instance arity?
  Verdict: ___

- [ ] **`62840e9`** — fresh var for ambiguous duck-typed field (typechecker.rn).
  **SUPERSEDED:** at HEAD the fresh var is a fallback behind `commonMemberFieldType` (added in
  `5356723`). Review the LIVE branch at HEAD: does the fallback still fire correctly when types
  disagree, and does it still set `sawStructuralGenericError`/defer as intended?
  Verdict: ___

- [ ] **`5356723`** — Stage 3: relation-method emission + cascade recursion (188 green).
  Five sub-changes, review each:
  - [ ] `commonMemberFieldType` (typechecker.rn) — agreement check via `toString()`; concrete-only
    via `hasFreeVars`. Is `toString()` a sound equality proxy? Any class with a same-named field of
    a DIFFERENT type that this would wrongly unify? Generic classes excluded correctly?
  - [ ] deferral-path `unify(monoResult, returnType)` (typechecker.rn) — mirrors non-deferred path;
    confirm it can't bind a result that should stay polymorphic.
  - [ ] `genCMethodInstance` retypecheck + emitting fn/name save/restore (function.rn) — restore on
    ALL exit paths? once-only `deferredRetypechecked` interaction with the cbuilder pre-emission pass?
  - [ ] `noteDependency` (expr.rn, cbuilder.rn) — `appendIfOpen` guard correct at module level?
    **KNOWN GAP (see triage):** only the METHOD-call path registers deps; plain-function and
    constructor calls do not — confirm and note (this is the `safe` lead).
  - [ ] `builtin/hashed.rn` clear-bucket-before-recurse — does clearing `table[x]` before the while
    loop ever drop entries in the non-mutual case? (chain still walked via local head.)
  Verdict: ___

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
- (none yet)
