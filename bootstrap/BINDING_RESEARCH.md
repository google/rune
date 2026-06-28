# Rune Bootstrap Binding — Final Design Decision Document

Author: lead compiler architect (synthesis of grounding + research areas A1–A7, revised
after adversarial review).
Date: 2026-06-21. Repo state: HEAD `ce4c684`, 187/205 bootstrap tests passing.
All Rune `file:line` cites are verified against the live source (`types/typechecker.rn`,
`cbackend/cbuilder.rn`, `database/function.rn`, `database/expr.rn`, `rune.rn`) and against
`bootstrap/research/ground-bootstrap.md`, `bootstrap/research/ground-legacy.md`, and
`bootstrap/HANDOFF.md`. External cites are in the numbered bibliography (§10), referenced
inline as `[n]`.

---

## WHAT CHANGED AFTER REVIEW

A four-lens adversarial panel found the original draft **unsound on the Dict worked example
and on staging feasibility**, sound-with-fixes on turing, and sound-with-fixes on citations.
Every blocker and major critique was verified directly against the source and (where the
reviewers made empirical claims) by **actually compiling the tests with the bootstrap
binary**. The verification confirmed the reviewers on every checkable point. The substantive
changes:

1. **The Dict fix moved from the EMISSION phase to the TYPECHECK phase, and is honestly
   re-scoped.** Verified by compiling all six cluster-1 tests: **five of six** (`in`,
   `dictitr`, `dicttest`, `heapqtest`, `heapsort`) fail at **typecheck**, where
   `CBuilder.build` — which hosts the proposed `materializeInner` pre-emission pass — **never
   runs** (`rune.rn:154-164` prints type errors and stops before `CBuilder(tc).build`). Only
   `heapqlisttest` reaches codegen. An emission-time fix therefore **cannot** turn those five
   green; it reproduces HANDOFF ITER 24's inert outcome. The Dict mechanism is now specified
   at the **constructor-typing path** (during phase 8 of `function()`), not in the
   pre-emission pass.

2. **The Dict worked-example trace is rebuilt on real mechanisms.** The original cited
   `classInstanceType(:486)` as "ties Entry's params to `<K,V>`" — but that function takes a
   `ClassInfo`, not `[K,V]`, and parameterizes with **fresh** tyvars (verified, lines
   486-492). It cited `demandClass(:583)` as "register Entry under `{key→K,value→V}`" — but
   that function takes only a `Sym` and registers Entry **generically** over its own canonical
   vars (verified, lines 583-595; the exact ITER-24 wall). The trace now **owns** that the
   per-signature concrete inner-class step is **net-new code modeled on legacy
   `markConstructorClassBound` (`bind/bindexpr.c:1219`)**, not a reuse of existing hooks.

3. **The Entry-paramVar / template-arg MISALIGNMENT is now specified.** `Entry(self, dict,
   <key>, <value>)` produces `info.paramVars = [dict, key, value]` (length 3,
   `constructorFunction:4297-4302`), while the relation `Dict<key,value> Dict.Entry<key,value>`
   carries 2 template args. `substituteClassVars:4659-4675` aligns positionally with guard
   `i < instParams.length`, so it would bind `dict←string`, `key←u32`, and **skip `value`**.
   The fix (P-ALIGN) builds the instance arg-list from only the `<key>/<value>` template
   params via an explicit index map.

4. **The interned-Sym OUTER-class merge hazard is now addressed.** Verified: `dicttest`
   line 20 `Dict(u32,string)` errors "could not unify parameters of function `(string,u32)`
   with args `(u32,string)`" — the second signature reuses the first. `materializeInner` only
   ever handled the inner Entry; the OUTER Dict signature merge is a separate, named problem
   (P-OUTER).

5. **Stage order changed: `recursiveDestructor` (was Stage 2) is now the FIRST GREEN.** It is
   verified to be a genuine emission/typecheck-phase failure ("call emitted without a type"
   at `hashed.rn:231` during genC) whose hooks (`retypecheckDeferred:600`, `demandMethod:558`,
   the eager-destroy loop `4188-4230`) are all real and non-generic. Stage 1 (Dict) as
   originally written needs a not-yet-trivial typecheck-phase mechanism and is no longer the
   lead. Dict's honest first-green scope is **`heapqlisttest` + the `b.rn` repro**, not 6 tests.

6. **turing trace fixed with the real driver and BOTTOM-UP RESULT resolution.** The chain is
   discovered by **re-typechecking each frame's body under concrete bindings** (via
   `retypecheckDeferred`, which records the next instantiation through `apply()→open()`), not
   a passive "scan". Each frame's RESULT type is now resolved bottom-up (base case `_0_7`
   first) before its C signature emits — the worked table gains a **resolved-return-type
   column** — otherwise `genCType` deadlocks on an unresolved Var exactly where the current
   compiler does. Termination is restated honestly: finite because of **structural decrease
   on a concrete measure + `instantiationIsConcrete` gating**, with `RECURSION_LIMIT` as a
   backstop that **REJECTS** infinite chains. The two preconditions (concrete seed,
   decreasing measure) are stated explicitly, reconciling the design with the undecidability
   of polymorphic-recursion inference.

7. **The genCPolyInstantiation no-op fix is specified as a RESTRUCTURE, not "routing".**
   `genCPolyInstantiation` binds the shared poly at `function.rn:423` and unbinds at `:479`;
   an inner instance re-enters the same function on the same poly while the outer is bound (the
   no-op wall, `typechecker.rn:1091-1097`). The design now states that this bind/unbind-shared-
   poly structure is **replaced**: worklist items emit at top level, each via its own
   `poly.open()` private frame, and the call site emits only a **name reference**.

8. **Citations fixed and a missing major approach added.** Swift "Compiling Swift Generics"
   re-dated **2024** (was 2025); rustc monomorphic-invariant quote + `check_recursion_limit`
   re-pointed at the **collector rustdoc/source** (which contain them), not the dev-guide
   overview page (which does not); C++ `[temp.inst]/12` pinned to **N4861** (paragraph numbers
   drift in the live draft); the A6 `Generic Go to Go` DOI corrected to **10.1145/3563331**
   in the supporting file. **Dictionary-passing / runtime type-passing** (Swift witness
   tables; Featherweight Go / Generic Go to Go) is added to §2 and the comparison table as the
   standard alternative for `nomono` type-level recursion, with an explicit reason Rune
   rejects it. Bidirectional typing (Dunfield & Krishnaswami) is named as the frame for the
   "check, don't infer" stance.

The minor fixes (the `Blocker.VarBound` id type is **i32** not u32, per `instantiate:1085`;
the `retypecheckDeferred` call takes **one** arg bracketed by `instantiate`/`deInstantiate`)
are folded into §4.

**Errata (2026-06-26).** Three file/line corrections folded in this revision: (1)
`markConstructorClassBound` lives in **`bind/bindexpr.c:1219`** (def) / `:1261` (call), **not**
`class.c` — the `deClass` per-signature machinery is in `database/class.c:98-220`, but the
trigger that marks a constructor's class bound for a concrete signature is in `bindexpr.c`;
(2) Rune's unifier **does** have an occurs check (`typeunifier.rn:287-298`,
`"Cannot construct infinite type"`); the `typechecker.rn:1169` comment denying it is stale (see
§2, A5 code-fact note); (3) the Dict cluster is a **composite** of three distinct equation
defects — P-ALIGN (misaligned params), P-OUTER (collapsed interned-Sym signatures), and the
missing inner-class instantiation — **not** a single root cause, which is why Stage 2's scope is
staged (§8) and no single edit greens the whole cluster.

---

## 1. PROBLEM RESTATEMENT

**Binding** = attaching a concrete datatype to every variable, expression, field, and call
result so the C backend can emit. The legacy C compiler does this with an **event-driven,
breadth-first fixpoint**: a `Binding` (pending work) parks on a `deEvent` when it hits a
missing dependency, the binder moves on, and the event *fires* when the dependency appears,
re-queuing the parked binding — looping to a fixpoint (`bind/bind.c`, three event kinds
`DE_EVENT_SIGNATURE/VARIABLE/UNDEFINED`) [ground-legacy.md]. Plus **per-signature
monomorphization**: `Dict(string,u32)` and `Dict(u32,string)` are two distinct concrete
`deClass`es (`database/class.c:98-220`), triggered by `markConstructorClassBound`
(`bind/bindexpr.c:1219`).

The **bootstrap** instead binds in **one forward pass** (`typechecker.rn function()` :4051)
with strict phases — relations expand (phase 5, :4090) **before** any class is typechecked
(phase 8, :4180) — **no work queue, no park/resume**. Its only fallback is a coarse
**once-deferral**: a function that hits an unresolved type variable sets
`sawStructuralGenericError` (set at **five** sites: :2185/2210/2396/3545/4568), rolls back,
and is re-typechecked **exactly once** later under its first concrete instantiation
(`retypecheckDeferred` :600, driven by `cbuilder.rn:255-306`). Per-call generics carry a
`PolymorphicType` whose uses are recorded as `TyvarInstantiation`s (`typeclasses.rn:789`) and
bound at emission by `instantiate`/`deInstantiate` (:1081/:1111).

**A structural fact that constrains every fix (verified `rune.rn:154-164`):** the driver runs
the *entire* typecheck first, and **if `countTypeErrors() > 0` it prints the errors and
STOPS** — `CBuilder(tc).build(module)` (which contains the pre-emission deferral pass) is
reached **only when typecheck is clean**. Therefore **a fix for a test that fails at typecheck
must run during the typecheck pass; a pre-emission/emission-phase fix can only help tests that
already typecheck cleanly and fail later.** This single fact reshapes the staging below.

**The three failure clusters (one root, but split by failure PHASE):**

1. **Dict / Heapq (6: `in`, `dictitr`, `dicttest`, `heapqtest`, `heapsort`,
   `heapqlisttest`).** `relation Hashed` injects `self.EntriesTable = arrayof(Entry)` into
   `Dict`'s constructor (`builtin/hashed.rn:92`). To type that field the inner `Entry` class
   must already exist tied to Dict's `<key,value>`. But the relation runs at phase 5 when
   `lookupClass(Entry)`/`lookupTyname(Entry)` are **null** (Entry registers only in phase 8).
   The field freezes to a bare `TypeName(Entry)` with no type args (`Arrayof` arm
   :2416-2443, which sets **no** `sawStructuralGenericError`, so the constructor **does not
   even defer**); `find`'s `entry.value` resolves to Entry's canonical var `v-33`. **PHASE
   SPLIT (verified by compiling):** five of six — `in`, `dictitr`, `dicttest`, `heapqtest`,
   `heapsort` — fail at **typecheck** ("Field Selection on v-28" at user code `dict.insert`);
   only `heapqlisttest` reaches codegen ("type variable v-112 is unresolved",
   `ctypegen.rn:342`). A 4-piece hand attempt (HANDOFF ITER 22-24) reached the
   `lookupClass(Entry)=null` wall and reverted clean at 187.

2. **turing / edwards / edwards2 / gf2 / integer (5): per-call polymorphism + type-level
   recursion.** Core: `addParens(a,b){ typeswitch a { ()=>b; default=>
   addParens(decParens(a), incParens(b)) } }`, `decParens(x)=x[0]` (strip a tuple level),
   `incParens(x)=(x,)` (add one). Each recursive call changes the *types*, so `addParens` must
   monomorphize into a finite chain `addParens_3_4 → _2_5 → _1_6 → _0_7`. The checker cannot
   evaluate this (result type is defined by its own recursive call → open var). At emission,
   `addParens_3_4`'s body must emit `addParens_2_5`, but `instantiate` binds a var **only if
   `isnull(val)`** (:1091-1097) and the same poly is already bound from the outer
   instantiation → the inner bindings are skipped (no-op). **PHASE SPLIT (verified):**
   `turing` and `edwards2` reach codegen (`v-65`/`v-93` unresolved → null indirection);
   `edwards` fails at **typecheck** ("Cannot index into type v-87" at user code
   `aliceSharedSecret[0]`, edwards.rn:65). gf2/integer additionally **merge two independent
   type params into one bound var** (a separate generalization bug; also a typecheck leak).

3. **recursiveDestructor (1): mutual recursion + a synthesized destroy.** A mutually-recursive
   cascade relation (`Hashed Foo Bar` + `Hashed Bar Foo`) makes a synthesized `destroy`'s body
   never get typechecked under mutual recursion ("call emitted without a type",
   `hashed.rn:231` during genC, verified). The single-recursion knot `monoResult`
   (:4797-4798) covers only one function. **PHASE: emission** — it typechecks clean and fails
   in `genC`, so it is the one cluster a non-typecheck-phase mechanism reaches cleanly.

**Common root (confirmed):** every remaining failure needs **per-instantiation and/or
fixpoint re-binding of GENERATED or RECURSIVE code**, which the single-pass + once-deferral
model cannot express. But the **failure phase differs**, and that determines where a fix must
live.

---

## 2. HOW OTHERS SOLVE IT

**Lazy / demand-driven completion (A1).** A symbol's type is a *thunk* forced on first use;
forcing is re-entrant, so a per-symbol "completing"/`Touched` bit detects genuine cycles,
while legal mutual recursion is handled by *tying the knot* over a dependency SCC seeded with
placeholders before any body is checked. Scala dotty's `SymDenotation` is a `Completer`; cycle
detection is literally `if is(Touched) then throw CyclicReference` [1]. Swift's
request-evaluator memoizes requests and tracks an **active-request stack** to detect cycles
[2]. rustc queries are memoized key→result functions, cycles found by DFS over active
`QueryJob`s [3]. GHC ties mutually-recursive type groups with `fixM` over SCCs [4].

**Constraint-based two-phase inference (A2).** Split into order-independent *constraint
generation* (one AST walk emitting equations + instance constraints, committing to nothing)
and *constraint solving* (union-find to a fixpoint). A use-before-definition just emits an
instantiation constraint naming the not-yet-known scheme; order-independence is *by
construction*. Pottier & Rémy, "The Essence of ML Type Inference" [5]; binding groups in
Jones, "Typing Haskell in Haskell" (`tiImpls` seeds every mutually-recursive member with a
fresh var before checking any body) [6]; GHC's OutsideIn(X) implication constraints [7].

**Worklist / attribute-grammar fixpoint (A3).** The legacy binder *is* a demand-driven
circular attribute evaluator run by a Kildall worklist. Reference Attributed Grammars make
name binding an on-demand inherited attribute, resolving forward/mutual refs regardless of
source order [8]; Circular RAGs evaluate self-dependent attributes by least-fixed-point
iteration with an `IN_CIRCLE` guard [9]; Kildall's worklist proves the **fixpoint is
independent of pop order** under monotonicity on a finite-height lattice [10].

**Monomorphization with generated members tied to outer args (A4).** C++ instantiates a
nested/derived member of a generic **lazily, at point of use, with the enclosing
specialization's arguments, only after those args are concrete** — `[temp.inst]/3,4,12`
(N4861) instantiates member `List<V>` as `List<int>` inside `Map<const char*,int>` [11].
rustc's collector invariant: **"the mono item we are currently at is always monomorphic, so
we know the concrete type arguments of its used mono items"** (rustc_monomorphize **collector
rustdoc**, [12]). This is exactly what the bootstrap violates by typing `arrayof(Entry)`
generically and early. The legacy does the same per-signature thing in `database/class.c`, triggered by
`markConstructorClassBound` (`bind/bindexpr.c:1219`).

**Polymorphic / type-level recursion (A5).** Inferring polymorphic recursion ≡
semi-unification ≡ **undecidable** (Henglein's FIX-M vs FIX-P; Kfoury–Tiuryn–Urzyczyn)
[13][14].

**Code-fact note (occurs check).** Rune's unifier *does* enforce an occurs check
(`typeunifier.rn` `occursIn` :195-256, applied in `bind` :287-298 →
`TypeError("Cannot construct infinite type")`). A naive unify of the self-call at a
structurally *deeper* type is therefore **actively rejected**, not silently looped — which is
precisely why the design **monomorphizes from the ground seed** rather than trying to unify the
recursion away. (The comment at `typechecker.rn:1169` that *"the unifier has no occurs check"*
is **stale**; `resolveDepth`'s depth bound guards self-referential bindings that bypass `bind`,
and is not evidence that no occurs check exists.)

The standard practical escapes are two, and the design must name both:

- **(i) Monomorphize from concrete uses** (whole-program). MLton caches `(type-args → fresh
  name)` per polymorphic decl and dedups; "due to the absence of polymorphic recursion in SML
  there are only a finite number of instances" [15]. Rust *rejects* genuinely infinite chains
  with a `recursion_limit` depth guard (collector `check_recursion_limit`, error E0275)
  [12][16]. **This is what Rune adopts.**
- **(ii) Dictionary-passing / runtime type-passing.** The literature's primary answer for the
  *non-monomorphizable* (`nomono`) case is **not** a depth guard but passing types/witnesses at
  runtime: Swift's default is **witness tables** [2]; Featherweight Go formalizes the `nomono`
  condition and **Generic Go to Go** gives a dictionary-passing and a hybrid back-end
  [20][22]. This compiles programs whose instantiation chains are genuinely infinite.

  **Why Rune rejects (ii):** Rune is a whole-program, no-runtime-type-representation compiler
  targeting C; it has no witness-table/dictionary runtime, and **every** poly-recursive
  function in the 205-test suite has a concrete ground call-site root with a structurally
  decreasing measure, so its chains are **finite**. Monomorphization + depth-guard compiles
  them with zero runtime cost; dictionary-passing would be a new runtime ABI for a case that
  does not occur. The depth guard exists precisely to **reject** (not silently miscompile) any
  future input that *would* need (ii).

**Derived-member ordering (A6).** Universal discipline: *generate first, bind later*, over the
**combined** program so source order is irrelevant. Rust runs full name resolution **after**
all macro-generated impls are collected [17]; Swift synthesizes conformances lazily, pulled by
demand inside the request-evaluator [2]; Template Haskell type-checks spliced decls "just as
if the programmer had written that program" [18]. Lombok is the cautionary anti-pattern (no
phase boundary → order-dependent, brittle).

**Background — bidirectional typing.** The "check, don't *infer*" stance the design takes for
turing (types are ground at concrete call sites, so the typechecker correctly leaves an open
var and the work moves to instantiation/emission) is the **bidirectional-typing** discipline
(Dunfield & Krishnaswami [23]). Naming it clarifies *why* leaving an open result var is
correct rather than a bug.

### Comparison table

| Approach | Order-independence mechanism | Generated members? | Poly-recursion? | Effort in Rune | Risk |
|---|---|---|---|---|---|
| **A1 Lazy/demand completion** | thunk forced on use + `Touched`/active-stack cycle detect | Partial — supplies *trigger* (`demandClass`) but not the knot-cut | No (would mis-flag finite chain as cycle) | Low–Med (hooks exist) | Low |
| **A2 Constraint two-phase** | union-find solver, any order → same MGU | Yes via deferred constraints | No (HM monomorphizes recursion) | Very High (rewrite `typechecker.rn`) | High |
| **A3 Worklist / RAG fixpoint** | park on missing entity, re-queue on fire, Kildall fixpoint | Yes (re-bind over concrete class) | Partial (schedules; needs keyed memo) | Med (legacy shape, no data model) | Med |
| **A4 Monomorphization (per-sig class)** | instantiate nested member with enclosing args at use, when concrete | **Yes — designed for it** | No (separate worklist) | Med — **net-new per-sig class code**, NOT pure reuse of `classInstanceType`/`demandClass` | Med |
| **A5(i) Use-driven mono worklist + depth guard** | enqueue fresh-keyed instance per concrete call site | n/a | **Yes — designed for it** | Med-High (restructure `genCPolyInstantiation`) | Med |
| **A5(ii) Dictionary / runtime type-passing** | pass type witnesses at runtime; no chain to enumerate | Yes | **Yes — incl. genuinely infinite (`nomono`)** | Very High (new runtime ABI) | High — **rejected**: no runtime type reps; chains here are finite |
| **A6 Generate-first/bind-combined** | bind over combined program, demand-pull synthesis | **Yes** | Partial (this *is* nomono) | Low (reorder + demandClass) | Low |

---

## 3. RECOMMENDATION

**Adopt a COMBINATION, in the bootstrap's own idiom, not a rewrite, and land the genuinely
emission-phase test FIRST:**

> **(C) Per-signature concrete-class monomorphization** for generated members (Dict), run
> **at the constructor-typing path during the typecheck pass** (NOT at emission), **plus a
> minimal named-blocker worklist** (a thin demand-driven park/resume that generalizes the
> existing once-deferral to a fixpoint) for mutual recursion, **plus a use-driven
> monomorphization walk with a recursion-depth guard** for per-call polymorphic recursion
> (turing).

This is **direction (C) as the backbone, (A)'s *shape* (named park/resume to a fixpoint)
without its data model, and (A5)(i)'s depth-guarded mono walk** for the type-recursion piece.
It is the convergent recommendation of A4, A6, A7, A3, and A5 once the approaches the research
explicitly rules out are discounted.

**Honest correction to the original draft:** the per-signature inner-class step (C) is **not**
a reuse of existing hooks. Verified directly: `classInstanceType` (:486) parameterizes with
**fresh** tyvars and takes a `ClassInfo`, not `[K,V]`; `demandClass` (:583) registers Entry
**generically** over its own canonical vars and takes only a `Sym`; `substituteClassVars`
(:4659) **misaligns** Entry's 3 paramVars `[dict,key,value]` against 2 template args. **(C)
requires net-new code** modeled on legacy `markConstructorClassBound` (`bind/bindexpr.c:1219`) — open a fresh
frame, bind Entry's `key`/`value` paramVars to Dict's concrete args (skipping the non-template
`dict` param via an explicit index map), typecheck Entry's body under those bindings, and
register a **distinct concrete `Entry<string,u32>`** tyname/class. The design owns this; it is
the new mechanism, not a hook call.

**Why this and not the alternatives:**

- **Not a full event binder (candidate A).** Highest effort; re-implements a global queue +
  three event factories + wait-lists the bootstrap does not need, because its dependency
  structure is *staged* — every grounded failure resolves in 1–3 monotone rounds, not
  arbitrary interleaving (A7 §4.4). Take its *shape*, not its data model.
- **Not constraint two-phase (A2).** A near-total rewrite of `typechecker.rn`, and it **still
  does not solve turing** (HM monomorphizes recursion). A2 itself recommends *retrofitting*
  three mechanisms onto the existing unifier, not rewriting.
- **Not lazy-completion alone (A1).** It supplies Dict's *trigger* + cycle *diagnosis* but
  "is NOT dissolved by laziness alone" — the Dict↔Entry knot must be **cut by per-signature
  monomorphization** (A1 mapping). And it would wrongly flag turing's productive finite chain
  as an illegal cycle.
- **Not dictionary-passing (A5(ii)).** It is the production answer for `nomono`, but Rune has
  no runtime type representation and the suite's chains are finite (§2); it would be a new ABI
  for a non-occurring case.
- **(C) is the proven mechanism.** It is what the legacy actually does (`database/class.c`)
  and what two independent production compilers do (C++ `[temp.inst]` N4861 [11], rustc
  collector [12]).

**What it unlocks (honest, phase-corrected, verified by compiling each test):**

- **Stage 1 (FIRST GREEN) — `recursiveDestructor` = 1 test.** Genuinely emission-phase ("call
  emitted without a type" at genC); all hooks real and non-generic. The credible self-contained
  first green.
- **Stage 2 (typecheck-phase per-sig concrete Entry) — Dict cluster.** Honest scope:
  **`heapqlisttest` + the `/tmp/zdict/b.rn` repro first**, then `in`/`dictitr`/`dicttest`/
  `heapqtest`/`heapsort` **only once the typecheck-phase materialization AND the outer-signature
  merge (P-OUTER) are both done.** The original "6 tests in one stage" claim is withdrawn; the
  five typecheck-failing tests are gated on real, named typecheck-phase work, not on a
  pre-emission pass.
- **Stage 3 (depth-guarded mono walk) — `turing`, `edwards2` = 2 tests** (the codegen-reaching
  members of cluster 2). **`edwards` is explicitly excluded**: it fails at typecheck ("Cannot
  index into v-87"), so an emission-time walk never runs for it; it belongs to the
  "typecheck leaks a poly result var" family with Dict, not the no-op-instantiation family.

**What it does NOT unlock (be explicit):**

- **`edwards`, `gf2`, `integer` (typecheck-leak family):** a polymorphic *result* leaks an
  unresolved var into user code at typecheck. Same phase blind spot as Dict; needs a
  typecheck-phase result-resolution, not the emission mono-walk. gf2/integer additionally have
  the param-merge generalization bug. **Stage 4 / future.**
- **safe / funcptr (2):** positive-var generalization dead-end (tyvar id-space). Orthogonal;
  **Stage 4.**
- **escapedCharTest, uint2string, allocfree, defaultMethods (4):** length-prefixed strings /
  object-pool **representation decisions**, not binding-architecture problems at all.

**Cost.** Stages 0–3 are each independently committable and gated on the suite staying ≥187,
zero drops. Stage 1 (recursiveDestructor) is medium and self-contained. Stage 2 (Dict) is
**medium-high** — it is the net-new per-signature class code plus the outer-merge fix, at the
typecheck path. Stage 3 is medium-high (restructure `genCPolyInstantiation`). The single
load-bearing conclusion: **these are different problems in different compiler phases solved
with different tools — do not bet the remaining failures on one mechanism, and do not place a
typecheck-phase fix at emission.**

---

## 4. DESIGN IN DETAIL

### 4.1 Data structures (small additions to the existing model)

```rune
// Stage 0: turn the boolean fn.deferredTypecheck into a typed reason.
// Augments sawStructuralGenericError (typechecker.rn:2185/2210/2396/3545/4568) AND
// adds a NEW producer at the Arrayof arm (:2416-2443), which currently signals nothing.
enum Blocker {
  ClassRegistered(name: Sym)       // "class Entry not yet registered"  (legacy DE_EVENT_UNDEFINED)
  SignatureKnown(spec: string)     // "destroy_Bar return type unknown" (DE_EVENT_SIGNATURE)
  VarBound(tyvarId: i32)           // "type variable v-33 still unbound" (DE_EVENT_VARIABLE)
}                                  // NOTE: tyvar ids are i32 (instantiate:1085 `arrayof(i32)`, btv.id)

class Deferred {
  fn:        Function              // the unit of pending work (the resume target)
  blockedOn: Blocker               // WHY it stopped — a named entity, not a flag
  inst:      Instantiation?        // re-run under this recorded instantiation, if any
}

// The worklist (was: a single "retypecheck once" call site in cbuilder.rn:255-306).
worklist:   List(Deferred)
defined:    Set(Sym)               // registered classes/tynames — monotone, grows only
boundVars:  Set(i32)               // bound tyvar ids — monotone within a live frame
```

**Existing hooks (all confirmed present) and what they really do:**

- `fn.deferredTypecheck` / `deferredRetypechecked` flags + `sawStructuralGenericError`
  (producer, five sites). **The `Arrayof` arm is NOT one of them — it sets nothing — so Dict's
  ctor never defers today; Stage 0 must add a producer there.**
- `retypecheckDeferred(fn: db.Function)` (:600): the **resume** primitive — re-opens scope,
  re-binds params, re-walks the **snapshotted** body. **Signature takes ONE arg.** The driver
  brackets it: `instantiate(poly, inst)` → `retypecheckDeferred(fn)` → `deInstantiate(poly)`
  (`cbuilder.rn:242-245`, `function.rn:468-479`). The `Deferred.inst` field is consumed by the
  bracketing, not passed as a second argument.
- `instantiate`/`deInstantiate` (:1081/:1111) + `PolyInstantiations` (the monomorphization
  worklist). `instantiate` binds a var only if `isnull(lookupVar(btv.id))` (:1091-1097) — the
  source of the turing no-op wall.
- the pre-emission pass (`cbuilder.rn:222-306`), already "bind concrete signature, re-check
  under it" — reached **only for clean-typecheck programs** (`rune.rn:159-164`).
- `demandClass(sym: Sym)` (:583) — registers a class **generically** over its own canonical
  vars; `demandMethod` (:558). On-demand but **not** parameterized by caller args.
- `classInstanceType(info: ClassInfo)` (:486) — parameterizes with **fresh** tyvars (poly
  classes) or builds the monomorphic self-type on demand (:494-501). **Does not accept or tie
  caller args.**
- `substituteClassVars(info, instanceType, memberType)` (:4659) — positional projection with
  guard `i < instParams.length`; **misaligns** when paramVars include non-template params.
- the eager-destroy retypecheck loop (`typechecker.rn:4188-4230`, **phase 9, in the typecheck
  pass**) — already synthesizes an instantiation and `retypecheckDeferred`s deferred destroys;
  the natural home for the Stage-1 SCC seed.

### 4.2 Stage 1 — mutual-recursion destroy fixpoint (recursiveDestructor; (A)'s shape)

**Phase: typecheck (eager-destroy loop) + emission. Verified failure: emission-phase.** This
is the first green because every hook is real and non-generic.

The single-recursion knot `monoResult` (:4797-4798) covers one function. For mutual
`destroy_Foo`/`destroy_Bar`, **seed both with a `monoResult`-style in-progress return type
before checking either body** (GHC `fixM`/`tcExtendRecEnv` over the SCC [4]; THIH binding
groups [6]). This is the SCC generalization of the existing single-slot knot.

```rune
func runDeferredToFixpoint() {
  loop {
    progress = false
    next = []
    for d in worklist {
      if blockerSatisfied(d.blockedOn) {            // class now in `defined` / var in `boundVars`
        instantiate(d.fn.poly, d.inst)              // bracket the resume primitive
        retypecheckDeferred(d.fn)                   // EXISTING resume primitive (:600), ONE arg
        deInstantiate(d.fn.poly)
        progress = true
      } else { next.append(d) }
    }
    worklist = next
    if worklist.isEmpty() { break }                 // success
    if !progress { break }                          // stuck → residue becomes errors (legacy bind.c:642-660)
  }
  for d in worklist { reportUnresolved(d.blockedOn) }
}
```

Concretely for recursiveDestructor: in the eager-destroy loop (4188-4230) recognize the
mutual `destroy_Foo`/`destroy_Bar` SCC, seed both in-progress slots, then check each body so
the call to the peer's `destroy` unifies against its in-progress slot (no demand-recursion, no
no-op). Any still-untyped method call (`for x in range(self.Bar_Table.length())`) records
`Deferred{Foo.destroy, SignatureKnown(Bar.destroy)}`; `runDeferredToFixpoint` converges in ≤2
rounds. **Must re-run from `bodySnapshot` (:628-635)** to avoid re-inlining the iterator AST.

### 4.3 Stage 2 — per-signature concrete inner class (Dict; candidate C, TYPECHECK phase)

**Phase: TYPECHECK. Verified: five of six cluster tests fail here; the fix must run during
`tc.function(module)`, NOT in `CBuilder.build`.**

The legacy makes `Dict(string,u32)` a distinct concrete class whose generated members are
**re-bound** with a concrete `Entry<string,u32>` (`class.c:98-220`, triggered by
`markConstructorClassBound`, `bind/bindexpr.c:1219`). The bootstrap analogue must run **when the concrete `Dict(K,V)`
constructor call is typed** — i.e. in the `apply`/`constructorFunction` path during phase 8,
or eagerly per recorded `Dict` instantiation immediately after phase 8 and **before** the
phase-10 module-level user statements (`dict.insert`) are typed. This is net-new code:

```
materializeInner(Dict, concreteArgs = [string, u32]):     // C++ [temp.inst]/12 N4861; rustc collector [12]
  key = (Entry, concreteArgs)
  if defined.has(key): return cached[key]                 // dedup (legacy classMatchesParams)

  // --- net-new: per-signature concrete class (legacy markConstructorClassBound, bind/bindexpr.c:1219) ---
  frame = openFreshFrame()
  // P-ALIGN: align ONLY the <key>,<value> TEMPLATE paramVars, skipping the non-template
  // `dict` param. Entry's info.paramVars = [dict, key, value]; the relation template carries
  // [key, value]. Build an index map templateIdx = [1, 2] (positions of key,value in paramVars)
  // so bindings line up; do NOT feed concreteArgs positionally into substituteClassVars.
  for (ai, pvi) in zip(range(len(concreteArgs)), templateIdx):
    bindVar(Entry.info.paramVars[pvi], concreteArgs[ai])  // key<-string, value<-u32 ; dict left to self-type
  entryTyname = TypeName(Entry, TupleType([Dict-self-of-this-sig, string, u32]))
  registerConcreteTyname(mangledName(Entry, concreteArgs), entryTyname)  // a DISTINCT class per signature
  cache[key] = entryTyname; defined.add(key)              // BEFORE binding body: breaks Entry.dict:Dict back-ref
  typecheckEntryBodyUnder(frame)                           // self.key:string, self.value:u32 become concrete
  closeFrame(frame)
  return entryTyname
```

Then the `Arrayof` arm (:2416-2443) for the generated `arrayof(Entry)` field, instead of
falling to bare `namedType` (:1519), calls `materializeInner(Dict, Dict's-concrete-args)` and
types the field as `arrayof(Entry<string,u32>)`. HANDOFF's P1–P4 (preserve `<key,value>` args
at `exprTree.rn:71`; `TransVal.templateArgs`; append args in `expandTransExprTree`;
parameterized `explicitType`/P4) are the **plumbing**; **the missing pieces ITER 24 hit are
(a) the ORDER — materialize when the concrete `Dict(K,V)` instantiation is typed, by which
point Entry can be built — and (b) the paramVar alignment (P-ALIGN) and (c) the outer-signature
merge (P-OUTER, §4.3.1).** P4 (parameterized `explicitType`) must land so the Arrayof arm can
construct `Entry<string,u32>`; `namedType:1519` currently ignores children.

#### 4.3.1 P-OUTER — two concrete signatures of one class Sym must coexist

Verified: `dicttest` line 20 `Dict(u32,string)` errors "could not unify parameters of function
`(string,u32) -> v-28` with args `(u32,string)`" — the second instantiation reuses the first's
signature. The class `Sym` is interned by name and `instantiate`/`deInstantiate` share one
`poly` object. `materializeInner` only handles the inner Entry. So **even a perfect inner-Entry
materialization does not make `dicttest` pass.** The design must, per recorded outer
instantiation, key a **distinct concrete outer Dict** on its resolved params (legacy makes
`Dict(string,u32)` and `Dict(u32,string)` two `deClass`es, `class.c:98-138`). The existing
`PolymorphicType.instantiations` list records both calls; the fix is to drive
`materializeInner` (outer + inner) **per recorded instantiation** and mangle the resulting
tyname/class on the resolved param tuple, so the two signatures do not collapse onto one
`v-28`. **This is part of Stage 2's scope, not an afterthought.**

### 4.4 Stage 3 — use-driven mono walk with depth guard (turing; A5(i)/rustc)

**Phase: emission, but ONLY for tests that reach codegen — `turing`, `edwards2`. NOT
`edwards`** (typecheck-fails). Make the recorded-instantiation list a real **collector
worklist** (rustc model [12][16]):

```
seed: a CONCRETE call addParens(t3,t4) records Instantiation{a:3-tuple, b:4-tuple} -> specName addParens_3_4
      // PRECONDITION: the seed is ground (instantiationIsConcrete, cbuilder.rn:241-276).
walk (depthByBaseFn: Map[baseFn -> usize]):
  pop specName S; if visited.has(S): continue; visited.add(S)
  if depthByBaseFn[base(S)]++ > RECURSION_LIMIT: error "infinite type recursion at <S>"   // [12][16] REJECT
  emit S as a TOP-LEVEL mono item with its OWN poly.open() frame (see 4.4.1)
  re-typecheck S's body under its CONCRETE bindings (retypecheckDeferred): the recursive call
    apply()->open() RECORDS the next, differently-typed instantiation -> enqueue it
  // only enqueue when the next instantiation is free-var-FREE (instantiationIsConcrete);
  // otherwise the chain is rejected at RECURSION_LIMIT, not silently continued.
resolve RESULT types BOTTOM-UP (4.4.2) before emitting each frame's C signature.
```

#### 4.4.1 The no-op fix is a RESTRUCTURE of genCPolyInstantiation, not "routing"

Verified: `genCPolyInstantiation` binds the **shared** poly at `function.rn:423`
(`builder.instantiate(fntype.poly!, instantiation)`) and unbinds at `:479`
(`builder.deInstantiate(fntype.poly!)`). The call-site path routes a nested call straight back
through `genCPolyInstantiation`/`instantiate` on **that same poly object while the outer is
still bound**, so `instantiate`'s `if isnull(val)` (:1091) skips every binding — the no-op
wall. "Route the call site to a fresh frame" is **insufficient**, because that path re-enters
the same bind/unbind cycle.

The fix **replaces** the shared bind/unbind structure: emit **all** worklist items at top level
from the collector **before** any body references them, each via its **own** `poly.open()`
with a **private var-frame** (decoupled from the outer's `instantiate`/`deInstantiate`). The
call site then emits **only a NAME reference** (`addParens_2_5`) — no `instantiate` on the
outer poly. This is how `countParens` already escapes (its instantiations are recorded and
emitted via the clean matching loop, never synthesized inside the outer's bound poly); the
restructure makes `addParens` behave the same way.

#### 4.4.2 Bottom-up RESULT-type resolution (the second deadlock the original missed)

`addParens_N_M`'s body returns either `b` (the `()` arm) or the recursive call's result (the
`default` arm). At re-typecheck of `_3_4` the recursive call returns a **fresh open result
var** (`apply` returns `ty.function.result` unresolved). So `_3_4`'s result stays open until
`_2_5` resolves, …, until `_0_7`'s base case (`return b`) resolves to a concrete type and
propagates **up**. **Process/emit the worklist in REVERSE dependency order (base case `_0_7`
first), or run a second pass that unifies each frame's result slot with its callee's resolved
result, before emitting that frame's C signature.** Otherwise `genCType` hits
`Type.TypeClass.Var` and prints "type variable v-NN is unresolved" (`ctypegen.rn:342`) — the
identical failure mode of the current compiler. The §6 table tracks this in a dedicated
resolved-return column.

### 4.5 TERMINATION

**Stage 1 (fixpoint loop).** Define `M = ⟨U, B⟩`, lexicographic, bounded below by 0: `U` =
distinct *unsatisfied* blockers referenced by the worklist; `B` = number of deferred items.
(1) **The blocker universe is finite and monotone:** classes/signatures/tyvars are drawn from
the program's finite declarations + recorded `PolyInstantiations`; a class is registered at
most once, a tyvar bound at most once in a live frame, a signature determined at most once —
**once satisfied, stays satisfied** (no event un-registers a class) [10]. So `U` is
non-increasing. (2) Every **non-final** round has `progress=true`, which either consumes a
satisfied blocker (`B`↓, maybe `U`↓) or binds ≥1 var / registers ≥1 class (`U`↓) ⇒ `M`
strictly decreases. A `progress=false` round **breaks immediately**. (3) A strictly-decreasing
sequence of pairs of naturals is finite ⇒ the loop halts, with an empty worklist (success) or
a residue reported as errors (legacy `bind.c:642-660`). This is the dataflow monotone-progress
argument [10], mirroring the lexicographic measure of the worklist-judgment algorithm [19].
**Sound but not complete** on a genuine A-blocks-B-blocks-A cycle — acceptable because the
remaining failures are *staged*, not cyclic; for Swift-grade diagnostics add a per-item
blocked-by back-pointer [2].

**Stage 3 (mono walk) — restated honestly.** The chain is finite for **two structural reasons,
not by fiat of the depth cap**:

1. **Concrete (ground) seed.** The walk is seeded from a concrete call-site instantiation whose
   params are free-var-free (`instantiationIsConcrete`, `cbuilder.rn:241-276`). Without a
   ground root there is nothing to enumerate.
2. **Structural decrease on a concrete measure.** `decParens(a)=a[0]` strips one real tuple
   level when `a` is concrete; each frame's `a` is strictly shorter, reaching the `()` base arm
   in finitely many steps — "due to the absence of [unbounded] polymorphic recursion … only a
   finite number of instances" [15]. Each re-typecheck must yield a free-var-FREE next
   instantiation for the walk to enqueue a usable item.

`RECURSION_LIMIT` is a **backstop that REJECTS** (error E0275-style [16]) — a chain not
reaching a non-recursive `typeswitch` arm within the limit is a genuinely infinite (`nomono`)
expansion and is reported as an error, **not silently accepted and not hung**. `visited`
dedup closes cycles. **Scope of the decidability claim (honest):** monomorphization is
decidable *here* only because every poly-recursive function in the workload satisfies (1) and
(2). A program violating either — e.g. a separately-compiled poly-recursive function with no
concrete root, or one that grows its measure — is correctly **REJECTED** at `RECURSION_LIMIT`,
consistent with the undecidability of polymorphic-recursion *inference* [13][14] (we do not
infer; we monomorphize from a ground seed — the bidirectional "check, don't infer" stance
[23]).

---

## 5. WORKED EXAMPLE 1 — DICT (TYPECHECK-PHASE, every step on a named mechanism)

The real code (`builtin/dict.rn:15-73`, `builtin/hashed.rn:92-93`):

```rune
class Dict(self, <key>, <value>) {
  class Entry(self, dict, <key>, <value>) {     // paramVars = [dict, key, value]  (LENGTH 3)
    self.key = key
    self.value = value
    dict.insertEntry(self)
  }
  func find(self, key) {
    entry = self.findEntry(key)
    if isnull(entry) { raise Status.NotFound, "Key not found" }
    return entry.value
  }
}
relation Hashed Dict<key, value> Dict.Entry<key, value> cascade ("key", "Entries")
// Hashed injects into Dict's ctor:  self.EntriesTable = arrayof(Entry)   ... + findEntry()
```

User code (verified failing): `dict = Dict(string, u32); dict.insert("Bob", 32u32); value =
dict.find("Bob")`; then `dict2 = Dict(u32, string); …`. Expected: `value : u32`. **Today
(verified): "Field Selection on v-28" at `dict.insert` (typecheck), and "could not unify
(string,u32) with (u32,string)" at the `Dict(u32,string)` line.**

| # | Recommended design does | mechanism (file:line) | type | CURRENT single-pass (verified failure) |
|---|---|---|---|---|
| 1 | Phase 5: relation injects `self.EntriesTable = arrayof(Entry)` generically | `executeTransformStatement` :4090; `hashed.rn:92` | element nominal `Entry`, no args | **Same** — injects generically; Entry **not** registered (`lookupClass(Entry)=null`) |
| 2 | **Stage 0 NEW producer** at the `Arrayof` arm: Entry unresolved → set the blocker + defer the ctor (the arm sets **nothing** today, so the ctor never defers) | NEW code at `Arrayof` arm :2416-2443; records `Deferred{Dict.ctor, ClassRegistered(Entry)}` | a *named* blocker | Falls to `explicitType`→`namedType` :1519 → **bare `TypeName(Entry)`, no args**, no error, no defer |
| 3 | Phase 8: `Dict(string,u32)` typed; **per recorded outer instantiation**, `materializeInner` runs at the constructor-typing path (P-OUTER keys a **distinct** outer class on resolved params, so `Dict(u32,string)` does not merge) | NEW `materializeInner` §4.3; keyed off `PolyInstantiations`; legacy `class.c:98-138` | `Dict`'s `<key,value>` = `string,u32` | No per-signature step; `Dict(u32,string)` **reuses** `Dict(string,u32)`'s sig → "could not unify" (verified line 20) |
| 4 | `materializeInner`: open a fresh frame; **P-ALIGN** binds only template params `key←string,value←u32` (skip `dict` via index map `[1,2]`); register a **distinct concrete `Entry<string,u32>`** tyname; cache in `defined` BEFORE binding its body | NEW per-sig class (legacy `markConstructorClassBound`, `bind/bindexpr.c:1219`); **NOT** `classInstanceType:486` (fresh vars) nor `demandClass:583` (generic) | `Entry<string,u32>` registered & concrete | `lookupTyname(Entry)` null → `structInstanceType` :4681 returns null (HANDOFF ITER 24 wall); `demandClass` would register Entry **generically** (its own `v-33`) |
| 5 | `blockerSatisfied(ClassRegistered(Entry))` TRUE → `instantiate`+`retypecheckDeferred(Dict.ctor)`+`deInstantiate` re-runs the table-field assignment; Arrayof arm now builds `arrayof(Entry<string,u32>)` via parameterized `explicitType` (P4) | `runDeferredToFixpoint` §4.2; resume :600 bracketed by :1081/:1111; P4 parameterized `explicitType` :1523 | `EntriesTable : arrayof(Entry<string,u32>)` | No resume driven here at typecheck; field stays bare |
| 6 | `find` re-typechecks: `findEntry` returns `Entry<string,u32>`; `entry.value` projects via `substituteClassVars` **using the aligned 2-arg instParams** (`[string,u32]`, with the index map so `value` is position-matched, NOT skipped) | `substituteClassVars` :4659 **fed an aligned instParams** (the original misalignment fixed by P-ALIGN) | **`u32`** | `entry.value` = Entry's canonical var `v-33`; `substituteClassVars` would mis-bind `dict←string,key←u32,value SKIPPED` |
| 7 | User `dict.insert`/`dict.find` now type against the concrete `Dict(string,u32)` instance | normal `apply`/`classMemberType` :4504 | clean types | "Field Selection on v-28" at `dict.insert` (verified — the unresolved outer instance) |
| 8 | Codegen: `genCType` sees concrete `u32` (only `heapqlisttest` reaches here today) | `ctypegen.rn:337` | clean C | for `heapqlisttest`: `genCType` hits `Var` → "type variable v-112 is unresolved" :342 |

**Key contrast & honest ownership:** the original draft presented steps 3–4 as reuse of
`classInstanceType`/`demandClass` — **wrong**: those return fresh/generic vars and take no
caller args (verified). The real mechanism is the **net-new per-signature concrete class**
(legacy `markConstructorClassBound`, `bind/bindexpr.c:1219`), run at **typecheck** time (because five of six tests fail
at typecheck, `rune.rn:159-164`), with **P-ALIGN** (the `[dict,key,value]` vs `[key,value]`
fix) and **P-OUTER** (the interned-Sym merge fix) both load-bearing. Steps 2 and 5 are net-new
worklist/blocker wiring; `Blocker`/`Deferred`/`runDeferredToFixpoint` do not exist yet.

---

## 6. WORKED EXAMPLE 2 — TURING (polymorphic recursion, with bottom-up result resolution)

The real core (`tests/turingTypeConstraints.rn:36-42`):

```rune
func decParens(x) { return x[0] }          // strip one tuple level  (strictly decreasing measure)
func incParens(x) { return (x,) }          // add one tuple level
func addParens(a, b) {
  typeswitch a {
    () => return b                          // base case: a bottomed out to the empty tuple
    default => return addParens(decParens(a), incParens(b))
  }
}
// call site:  addParens(((( ),),), (((( ),),),))   ->  a is depth-3 (ground), b is depth-4 (ground)
```

Each recursive call decreases `a`'s depth by 1 and increases `b`'s by 1, so the call
monomorphizes into the **finite** chain `addParens_3_4 → _2_5 → _1_6 → _0_7`, ending when `a`
reaches `()`. **Preconditions (§4.5):** the seed is ground (depth-3, depth-4 concrete tuples),
and `decParens` structurally decreases `a` — both hold, so the chain is finite and decidable
here.

| # | Recommended design (Stage 3 mono walk) does | mechanism | next PARAM instance | resolved RETURN type | CURRENT model (verified deadlock) |
|---|---|---|---|---|---|
| 1 | Seed from the **ground** call: record `Instantiation{a:depth-3,b:depth-4}` → `addParens_3_4`; enqueue; `visited={3_4}`; depth[addParens]=1 | `instantiationIsConcrete` `cbuilder.rn:241-276` | — | open (pending) | Records same inst, but ALSO a `v-NN` free-var self-inst that must never be adopted (`expr.rn:2658-2664`) |
| 2 | Re-typecheck `_3_4`'s body under its CONCRETE bindings; `apply()→open()` on the `default` arm RECORDS `addParens(decParens a, incParens b) = _2_5` → enqueue (free-var-free), depth=2 | `retypecheckDeferred` :600; `apply`/`open` :1474-1476 RECORDS the next inst | `_2_5` | open, = result-of(`_2_5`) | Checker can't evaluate result (it IS the recursive call's result) → stays open `monoResult` :4797 |
| 3 | Re-typecheck `_2_5` → records `_1_6`, depth=3 | as row 2 | `_1_6` | open, = result-of(`_1_6`) | Emitting `_3_4`'s body re-enters `instantiate` on the **same bound poly** :423; every var non-null → **bindings skipped (no-op)** :1091-1097 |
| 4 | Re-typecheck `_1_6` → records `_0_7`, depth=4 (< RECURSION_LIMIT [16]) | as row 2 | `_0_7` | open, = result-of(`_0_7`) | — (never reached cleanly) |
| 5 | Re-typecheck `_0_7`: `a` is `()` → the `()` arm `return b` hits, **no recursive call**, leaf; walk ends, `visited={3_4,2_5,1_6,0_7}` | base arm; `b` is depth-`?` ground | (leaf) | **`b`'s type at depth 7** (concrete) — RESOLVED | — |
| 6 | **Bottom-up result pass (§4.4.2):** `_0_7.result = b` (concrete) → `_1_6.result = _0_7.result` → … → `_3_4.result` all concrete | reverse-dependency unify of each result slot with callee's resolved result | — | `_0_7→…→_3_4` all concrete | the no-op skip emits with outer (wrong) bindings, result slot stays a Var |
| 7 | Emit each of the 4 specNames **at top level**, each via its **own `poly.open()` frame** (§4.4.1, restructured `genCPolyInstantiation`); the call site emits only a **name reference** `addParens_2_5` | top-level emission + `poly.open()` per item; **NOT** re-`instantiate` on the bound poly | 4 chained C fns | every C signature has a concrete return | `genCType` hits unresolved Var (`ctypegen.rn:342`) "type variable v-65 is unresolved" (verified) |

**Termination guard:** the per-base-function depth counter [12][16] **rejects** (does not hang)
a chain that fails to reach a non-recursive arm within `RECURSION_LIMIT` — a genuinely
`nomono` function. The finite turing chain reaches `()` at depth 4, far under the limit,
because the seed is ground and `decParens` decreases the measure (§4.5).

**Key contrast:** the current model dead-locks twice — (a) the checker cannot *infer* the
type-level recursion (undecidable [13]; the correct response is to leave an open var, the
bidirectional stance [23]); and (b) at emission the *same* poly is already bound, so the nested
`instantiate` is a no-op (:1091-1097), **and** the result slot is never resolved bottom-up
(`genCType` hits a Var). The recommended design **does not infer** — it **monomorphizes from
the ground seed**, discovers each frame by re-typechecking under concrete bindings (recording
the next instance via `apply()→open()`), resolves results **bottom-up**, and emits each
`addParens_N_M` as a **top-level, freshly-`open()`ed** mono item the call site references **by
name** (MLton/rustc [15][12]).

---

## 7. WORKED EXAMPLE 3 — MUTUAL-RECURSION DESTROY (recursiveDestructor; FIRST GREEN)

**Phase: emission (verified: "call emitted without a type" at `hashed.rn:231` during genC).**
Mutual cascade: `relation Hashed Foo Bar` + `relation Hashed Bar Foo`. Synthesizing
`Foo.destroy` needs `Bar.destroy`'s signature and vice-versa; the single-recursion knot
`monoResult` (:4797-4798) covers only one function, so under mutual recursion the destroy
body's method calls (`for x in range(self.Bar_Table.length())`, `hashed.rn:231`) are never
typechecked → "call emitted without a type" (`expr.rn:2058`, surfacing at genC).

Under the recommended design (Stage 1, in the eager-destroy loop `typechecker.rn:4188-4230`):

1. Synthesize `Foo.destroy` and `Bar.destroy`; recognize them as a mutual-recursion **SCC**
   (each references the other's `destroy`). The loop already iterates these via `demandMethod`
   (:4195) — extend it to detect the SCC.
2. **Seed both** with a `monoResult`-style in-progress return type *before* checking either
   body — the GHC `fixM`/binding-group pattern [4][6], generalizing the existing single-slot
   knot (:4797) to a per-SCC set.
3. Check `Foo.destroy`'s body: the call to `Bar.destroy` unifies against Bar's in-progress
   slot (no demand-recursion, no no-op). Same for `Bar.destroy`. Record
   `Deferred{Foo.destroy, SignatureKnown(Bar.destroy)}` only if a method call is still untyped.
4. `runDeferredToFixpoint` (§4.2): each round, one destroy's signature becomes known via
   `SignatureKnown`; the pair converges in ≤2 rounds (re-running from `bodySnapshot` :628-635 to
   avoid the iterator-inlining AST-mutation hazard, ground-bootstrap.md §2). `length()`/method
   nodes get real types → clean emission.

Same machinery (named blocker + fixpoint + `retypecheckDeferred`), all hooks real and
non-generic (`Foo`/`Bar` are monomorphic, so the table-field-generic problem does **not** bite
here — this is why it is the credible first green).

---

## 8. IMPLEMENTATION PLAN

Each stage independently committable; gate every commit on the suite staying **≥187, zero
drops** (per the commit-on-green rule). `builtin/hashed.rn` ships to both compilers, so run
`make` after editing it.

- **Stage 0 — named blocker + NEW Arrayof producer (groundwork, low risk, ~1 day).** At the
  five `sawStructuralGenericError` sites (`typechecker.rn:2185/2210/2396/3545/4568`) record the
  missing entity into `Deferred.blockedOn`. **Additionally add a NEW producer at the `Arrayof`
  arm (:2416-2443):** when the arrayof element is an unregistered class, signal a blocker and
  arrange for the constructor to defer (it does **not** today — the arm is silent, so Dict's
  ctor never defers). `Blocker.VarBound` uses **i32** ids (`instantiate:1085`). No behaviour
  change for the existing five sites (still fire once); de-risks every later stage and makes
  failures self-describing. **Turns no test green.** Files: `typechecker.rn`.

- **Stage 1 — mutual-recursion destroy fixpoint (FIRST GREEN: `recursiveDestructor` = 1
  test).** In the eager-destroy loop (`typechecker.rn:4188-4230`), detect the mutual
  `destroy_Foo`/`destroy_Bar` SCC, seed both `monoResult`-style slots (§4.2), and replace the
  once-driven `retypecheckDeferred` with `runDeferredToFixpoint` keyed on Stage-0 blockers,
  monotone-progress termination (§4.5). Re-run from `bodySnapshot` (:628-635). Hooks
  (`retypecheckDeferred` :600, `demandMethod` :558, the eager-destroy loop) are all real and
  non-generic. **Files:** `typechecker.rn`, `cbuilder.rn`. Does NOT touch safe/funcptr.
  **→ self-contained, emission-phase verified, the safe first green.**

- **Stage 2 — per-signature concrete Entry, TYPECHECK PHASE (Dict cluster; staged scope).**
  Implement `materializeInner` (§4.3) at the **constructor-typing path** (phase 8) /
  immediately after phase 8 and before phase-10 user statements — **NOT** in `CBuilder.build`
  (which never runs for the five typecheck-failing tests, verified `rune.rn:159-164`). Land
  HANDOFF P1–P4 plumbing **plus P-ALIGN** (`[dict,key,value]` vs `[key,value]` index map,
  §4.3) **plus P-OUTER** (distinct concrete outer Dict per resolved-param signature, §4.3.1).
  Wire the `Arrayof` arm (:2416-2443) to `materializeInner`. **Validation gate, in order:**
  (a) `/tmp/zdict/b.rn` compiles+runs; (b) `heapqlisttest` (the lone codegen-reaching cluster
  member) goes green; (c) **only then** expect `in`/`dictitr`/`dicttest`/`heapqtest`/`heapsort`
  (typecheck) once P-OUTER lands; (d) full suite ≥187. **Honest scope: do NOT claim 6 tests up
  front** — the first green here is `heapqlisttest` + the repro; the five typecheck tests
  depend on the typecheck-phase materialization *and* P-OUTER both working. **Files:**
  `typechecker.rn`, `parse/exprTree.rn`, `cbuilder.rn`.

- **Stage 3 — depth-guarded mono walk, RESTRUCTURE genCPolyInstantiation (GREEN: `turing`,
  `edwards2` = 2).** Make `PolyInstantiations` a collector worklist (§4.4) with a per-base-fn
  recursion-depth counter (REJECTS at `RECURSION_LIMIT`) and `visited` dedup; **replace** the
  shared-poly `instantiate`/`deInstantiate` bind/unbind in `genCPolyInstantiation`
  (`function.rn:423/:479`) with top-level emission of each `addParens_N_M` via its own
  `poly.open()` frame (§4.4.1); add **bottom-up result resolution** (§4.4.2, emit base case
  first); route nested same-poly calls to a **name reference**, not a re-`instantiate`. **Do
  NOT list `edwards`** (typecheck-fails, "Cannot index into v-87"; it is the typecheck-leak
  family, not the no-op family). **Files:** `database/function.rn` (`genCPolyInstantiation`
  :416-479), `database/expr.rn` (:2613-2731), `cbuilder.rn`.

- **Stage 4 — typecheck-leak + generalization-policy bugfixes (separate; `edwards`, `gf2`,
  `integer`, `safe`, `funcptr`).** `edwards`/`gf2`/`integer` leak a polymorphic *result* var
  into user code at typecheck — same phase family as Dict, needs typecheck-phase result
  resolution (not the emission mono-walk). gf2/integer additionally have the param-merge in
  `deferVars`/`PolymorphicType` (:4892-4924). safe/funcptr are the positive-var/tyvar-id-space
  issue. **Explicitly NOT an ordering problem**; keep out of the binding-architecture change;
  gate behind not regressing operator tests.

---

## 9. RISKS & ALTERNATIVES

- **AST-mutation-by-inlining (Stage 1/3, highest hazard).** Re-running a body whose iterator
  inlining already spliced `InlinedBlock` nodes (`typechecker.rn:3560`) re-inlines into an
  already-inlined body. **Mitigation:** always re-run from `bodySnapshot` (:628-635) and keep
  the existing inlining guard; this is *why* `retypecheckDeferred` snapshots and *why* a naive
  second pass is unsafe. RAGs' immutability assumption is the warning [9].
- **Stage 2 is the riskiest and is net-new, not reuse.** The HANDOFF P1–P4 attempt proved each
  piece inert alone and hit the null-`lookupClass` wall; this design adds **(a) the order**
  (materialize at instantiation-typing time, in the typecheck pass), **(b) P-ALIGN** (the
  `[dict,key,value]` misalignment), and **(c) P-OUTER** (the interned-Sym merge). If
  `materializeInner` still can't tie Entry's params, fall back to **eager per-signature class
  creation** at the `Dict(K,V)` recording site (closer to legacy `class.c`), accepting more
  duplication. **Do not place this fix at emission** — the five cluster tests never reach it.
- **Genuine cycles break-and-error (Stage 1).** The fixpoint is sound but not complete on a
  real A↔B blocker cycle; it reports the residue as errors (legacy `bind.c:642-660`). The
  remaining failures are staged, not cyclic, so this suffices; add a Swift-style active-stack
  back-pointer [2] only if a real cycle surfaces.
- **Stage 3 restructure is central code.** `genCPolyInstantiation` (:416-479) and the call-site
  path (`expr.rn:2613-2731`) have compensating logic (name-forcing, instantiation matching,
  fresh-open synthesis); replacing the shared bind/unbind cycle must preserve the cases that
  logic currently handles (notably `countParens`, which already works via the clean recorded
  loop). Bottom-up result resolution (§4.4.2) is the part most likely to surface ordering bugs;
  test on `turing` and `edwards2` only.
- **`nomono` inputs are rejected, not compiled.** Rune deliberately does not implement
  dictionary-passing (A5(ii)); a future program whose poly-recursion is genuinely infinite is
  **rejected** at `RECURSION_LIMIT` with a clear error, not miscompiled. If such a program ever
  needs to compile, that is a separate runtime-ABI project (witness tables / dictionary passing
  [2][20][22]), explicitly out of scope here.
- **Fallback if the primary stalls.** If (C) + worklist proves too entangled, the **full event
  binder (candidate A)** remains the faithful upper bound — strictly more work; the research
  consensus (A7) is that the staged dependency structure does not require it. Stages are
  independent: Stage 1 alone (recursiveDestructor, 187→188) is a worthwhile, low-risk landing.

---

## 10. REFERENCES

1. Scala 3 (dotty) — `SymDenotation` as `Completer`, `Touched` cyclic-reference bit.
   https://github.com/scala/scala3/blob/main/compiler/src/dotty/tools/dotc/core/SymDenotations.scala
2. Swift — *RequestEvaluator* (memoized demand-driven, active-request-stack cycle detection).
   https://github.com/swiftlang/swift/blob/main/docs/RequestEvaluator.md ;
   Pestov, *Compiling Swift Generics*, Nov 16 **2024** (living doc, periodically updated;
   request evaluator, witness tables, Turing-completeness ch.12).
   https://download.swift.org/docs/assets/generics.pdf
3. Rust — query system (memoized key→result, DFS cycle detection).
   https://rustc-dev-guide.rust-lang.org/query.html
4. GHC — tying the knot over mutually-recursive type groups (`fixM`, `tcTyClGroup`).
   https://wiki.haskell.org/Tying_the_Knot ;
   Kiselyov/Rémy, generalization + SCC. https://okmij.org/ftp/ML/generalization.html
5. Pottier & Rémy, *The Essence of ML Type Inference*, ATTAPL ch.10, 2005.
   http://gallium.inria.fr/~fpottier/publis/emlti-final.pdf
6. Jones, *Typing Haskell in Haskell*, 1999 (binding groups, `tiImpls`, `freshInst`).
   https://web.cecs.pdx.edu/~mpj/thih/
7. Vytiniotis, Peyton Jones, Schrijvers, Sulzmann, *OutsideIn(X)*, JFP 2011.
   https://simon.peytonjones.org/outsideinx/
8. Hedin, *Reference Attributed Grammars*, Informatica 24(3), 2000; JastAdd tutorial 2009.
   https://fileadmin.cs.lth.se/sde/publications/papers/2009-Hedin-GTTSE-preprint-tutorial.pdf
9. Magnusson & Hedin, *Circular Reference Attributed Grammars*, Sci. Comput. Program.
   68(1):21–37, 2007.
10. Kildall, *A Unified Approach to Global Program Optimization*, POPL 1973 (worklist,
    order-independent fixpoint).
11. C++ standard — `[temp.inst]/3,4,12`, `[temp.point]/4` (lazy nested-member instantiation
    with enclosing args). **Stable cite: N4861 `[temp.inst]/12`** (contains the
    `Map<const char*,int>`→`List<int>` example; paragraph numbering drifts in the live draft).
    N4861 mirror: https://timsong-cpp.github.io/cppwp/n4861/temp.inst ;
    living draft (pointer only): https://eel.is/c++draft/temp.inst
12. Rust — `rustc_monomorphize` collector. **Monomorphic-item invariant** ("the mono item we
    are currently at is always monomorphic, so we know the concrete type arguments of its used
    mono items") **and `check_recursion_limit` are in the collector rustdoc/source**, not the
    overview page. https://doc.rust-lang.org/nightly/nightly-rustc/rustc_monomorphize/collector/index.html ;
    https://github.com/rust-lang/rust/blob/master/compiler/rustc_monomorphize/src/collector.rs ;
    (overview, secondary) https://rustc-dev-guide.rust-lang.org/backend/monomorph.html
13. Henglein, *Polymorphic Type Inference and Semi-Unification* (diss., FIX-M/FIX-P;
    undecidability of polymorphic-recursion inference). Corroborated by [14] and the GHC manual.
    https://www.cs.tufts.edu/comp/150FP/archive/fritz-henglein/dissertation.pdf
14. Dudenhefner, *Undecidability of Semi-Unification on a Napkin*, FSCD 2020 (KTU attribution).
    https://www.ps.uni-saarland.de/Publications/documents/Dudenhefner_2020_Semi-unification.pdf
15. MLton — *Monomorphise* (cache worklist, finite-instances/drop-unused termination;
    "absence of polymorphic recursion in SML → finite number of instances").
    http://mlton.org/Monomorphise
16. Rust — infinite-chain rejection + `recursion_limit` depth guard, error E0275.
    https://doc.rust-lang.org/error_codes/E0275.html ;
    https://github.com/rust-lang/rust/issues/136528
17. Rust — name resolution runs after macro-generated impls collected (whole-crate, late).
    https://rustc-dev-guide.rust-lang.org/name-resolution.html
18. Sheard & Peyton Jones, *Template Meta-programming for Haskell*, Haskell Workshop 2002.
    https://www.microsoft.com/en-us/research/publication/template-meta-programming-for-haskell/
19. Zhao, Oliveira & Schrijvers, *A Mechanical Formalization of Higher-Ranked Polymorphic Type
    Inference*, ICFP 2019 (single-context worklist, substitute-into-remaining, lexicographic
    termination). https://i.cs.hku.hk/~bruno/papers/icfp2019.pdf
20. Griesemer, Hu, Kokke, Lange, Taylor, Toninho, Wadler, Yoshida, *Featherweight Go* (PACMPL 4
    OOPSLA 149, 2020) — `nomono` condition for divergent monomorphization; dictionary-passing
    as the alternative. https://doi.org/10.1145/3428217 (arXiv:2005.11710)
21. Heeren, Hage & Swierstra, *Constraint based type inferencing in Helium*, 2003.
    https://www.cs.ou.nl/~bastiaan/heeren-cp03.pdf
22. Ellis, Zhu, Yoshida, Song, *Generic Go to Go: Dictionary-Passing, Monomorphisation, and
    Hybrid*, PACMPL 6(OOPSLA2) Art. 168, Oct 2022. **DOI 10.1145/3563331** (arXiv:2208.06810).
    https://doi.org/10.1145/3563331
23. Dunfield & Krishnaswami, *Bidirectional Typing*, ACM Comput. Surv. 54(5), 2021
    (the "check, don't infer" discipline; arXiv:1908.05839). https://doi.org/10.1145/3450952

Internal grounding (this repo): `bootstrap/research/ground-legacy.md`,
`bootstrap/research/ground-bootstrap.md`, `bootstrap/research/A1–A7-*.md`,
`bootstrap/HANDOFF.md`. **Empirical verification for this revision:** all six cluster-1 tests,
`turing`/`edwards`/`edwards2`, and `recursiveDestructor` were compiled with the bootstrap
binary (`./rune -p bootstrap <test>`) on 2026-06-21 to confirm the typecheck-vs-codegen phase
of each failure and the `Dict(u32,string)` signature merge.
```
