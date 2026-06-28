# Type Inference Primer — study notes

Self-contained review notes distilled from the walkthrough/grilling sessions on how
Rune binds (typechecks) programs, and how that connects to the bootstrap compiler's
known failures. Written for someone without a compilers background. Read top-to-bottom
once; after that, the **Glossary** and **Concept map** are the quick-reference parts.

---

## 1. The pipeline, and why bind is a *gate*

```
parse  →  bind (typecheck)  →  emit (codegen → C)  →  clang
```

- **Bind / typecheck** = attach a type to *every expression node* (not just named
  variables: `a.area()`, `p[0]`, `a + b` each get one). It's not a lookup ("this is an
  int") — it's *solving for whether the program is self-consistent under constraints*.
  The **product** is a fully-typed tree (or, for a polymorphic definition, a generalized
  *scheme*).
- **The gate:** if bind reports **≥1 type error, the driver halts** (`rune.rn` ~153–166).
  Emission never runs. No partial C.
- This is what **splits our two failure clusters**:
  - **Dict** fails *at the gate* — `Found N type errors`, zero C emitted.
  - **turing** *passes the gate* and dies *inside emission* (`v-65 unresolved`).
  Same root cause ("committed too early, kept no fixpoint loop"), opposite ends of the pipe.

Source is allowed to be polymorphic; **emitted code is always monomorphic** — one concrete
copy per type actually used (see *monomorphization*).

---

## 2. Glossary

Mechanics first (these build on each other):

- **type variable / box / cell** — a *mutable placeholder*, either empty or filled with a
  type. Crucially, an expression holds a *reference to a box*, not a type directly. (In ML
  implementations it's literally a `ref` cell.)
- **unify** — assert two types are equal. Mechanically: **merge two boxes into one** (copies
  nothing). Because expressions hold *references*, filling the box later is instantly visible
  everywhere it's referenced — this is why within-function inference needs **no second pass**.
- **substitution** — the accumulating record of what each variable has been unified to.
- **zonk** — follow the reference chain to read out a variable's *final concrete type*. Done
  at the end, or whenever a concrete answer is actually needed.
- **occurs-check** — unification refuses to bind a variable to a type that *contains itself*
  (e.g. `Tuple^k = Tuple^(k-1)`), which would be an infinite type. This is what makes
  polymorphic recursion fail the one-box trick.

Polymorphism (HM core):

- **generalize** — **once, at a definition**: close over the still-free variables → a `∀`
  **scheme**.
- **scheme / polymorphic type** — the `∀` "stencil" stored for a definition,
  e.g. `∀a b. (a,b) -> a`. **One** is stored per definition.
- **instantiate / open** — **fresh, at every use**: stamp a private copy of a scheme with
  brand-new boxes, then unify *those* against the actual arguments.
- **monomorphic** — one specific type, *not generalized* (no `∀`). A "monomorphic box" = an
  un-generalized type variable standing for a whole signature.
- **polymorphic** — serves many types (a scheme with `∀`).
- **constraint / bounded polymorphism** — a requirement riding on a *still-generic* variable
  ("must have `area()`", "must be addable"): `∀a where a: HasArea`. Born *structurally from
  use* = **duck typing**.
- **pin vs bound** — a use can **pin** a variable to a concrete type (variable vanishes) or
  **bound** it (variable survives, carrying a constraint). `p[0] + 1` with adaptive literals
  *bounds* the element to numeric; it does not pin it to `int`.
- **AnyInt / adaptive literal** — an untyped integer literal that *adapts* to context rather
  than forcing a fixed width.

The three layers (never conflate them — the bugs live in the gaps):

- **scheme** (1, at the definition) → **instantiations** (N, at the uses; each a
  `TyvarInstantiation` record) → **emitted functions** (N, at codegen).
- **monomorphization** — codegen emits a separate concrete copy per instantiation.
  e.g. geometry → `Circle_f64`, `Rect_u32_u32`, `Rect_i16_i16`, each with its own `area()`.

Ordering & recursion:

- **within-function inference** — *one* forward pass + accumulating substitution + final
  zonk. **Order doesn't matter** (indirection through boxes).
- **across-function inference** — **leaf-first** (process callees before callers, so every
  call has a ready scheme to instantiate). **Order matters.** Done *once* per function; the
  scheme is cached.
- **SCC (strongly connected component)** — a maximal clump of functions that all
  (transitively) call each other. Non-recursive = size 1. Self-recursive = size 1 + self-loop.
  Mutual recursion = size ≥ 2. **A cycle has no valid leaf-first order** → type the whole SCC
  together.
- **tie-the-knot** — how to type a recursive SCC:
  1. **seed** a fresh monomorphic box per function *before* walking any body,
  2. **unify** every self/cross-call against those boxes *during* the bodies,
  3. **generalize** *after* the bodies.
- **fixpoint** — "iterate until nothing changes." For mutual recursion you may need **>1
  round**, because checking one body refines another's box. `recursiveDestructor` converges
  in ≤2 rounds.
- **monomorphic recursion** — self-call at the **same** type (`repeatArea`). One box
  satisfies all levels → tie-knot succeeds → **one** specialization, regardless of runtime
  depth.
- **polymorphic recursion** — self-call at a **different/growing** type (`addParens`). One
  box is *contradictory* (occurs-check) → need **one instantiation per type-level**,
  discovered by a fixpoint from a **ground seed** down to the **base case**. Finite *only* if
  it terminates (structural decrease + ground seed); otherwise `RECURSION_LIMIT` **rejects**.
- **ground seed** — a concrete, variable-free type from a literal/constructor at a call site.
  The thing that makes a monomorphization chain *finite*.
- **bottom-up result resolution** — recursive *result* types form a chain (level k's result
  = level k-1's …); only the base case is concrete. So resolve **base-first and propagate
  up**. Resolving top-down hits an unfilled box → `genCType` crash (`v-65 unresolved`).

---

## 3. Worked examples

### 3.1 The thin chain — generalize/instantiate
```rune
func first(p) { return p[0] }          // inferred: ∀a b. (a, b) -> a   (see note)
// chain: first ← keyOf ← lookup ← handle ← serve  (thin plumbing, p threads straight through)
serve((42, "x"))      // instantiate at (int, string)  → first : (int,string)->int
serve(("k", true))    // instantiate at (string, bool) → first : (string,bool)->string
```
- **Lesson:** ONE scheme stored at the definition; a **fresh instantiation per call site**;
  emission turns each instantiation into a concrete C function (here, ~2).
- **Note (the arity subtlety):** the definition *alone* only knows `p` is "indexable with a
  0th element" — the **arity** (and even tuple-vs-array) is *refined at each use* by unifying
  the abstract `p` against a concrete argument. The `(a,b)` is the call sites leaking in.
- **Why this case is easy:** infinitely many *possible* instantiations, but the *actual* set
  = the call sites, each a **ground type immediately**. No recursion to chase. (Contrast
  turing, §4.2.)

Negative case — pin vs bound:
```rune
func firstPlus(p) { return p[0] + 1 }  // ∀a:Numeric b. (a,b) -> a   (a is BOUND, not free)
firstPlus(("k", true))                 // FAILS at unification: string isn't numeric → type error at the gate
```
- `+` consumed `a`'s freedom. With Rune's **adaptive `AnyInt`** literal it *bounds* `a` to
  numeric rather than *pinning* it to `int` (so `firstPlus((42u8,"x"))` would typecheck).
  (Exact defaulting behavior is a "read the source" detail.)

### 3.2 Geometry — generic classes, duck typing, the numeric tower, monomorphization
```rune
class Circle(self, <r>) { self.r = r; func area(self) { return 3.1415927f64 * <f64>self.r * <f64>self.r } }
class Rect(self, <w>, <h>) { self.w = w; self.h = h; func area(self) { return self.w * self.h } }
func combinedArea(a, b) { return <f64>a.area() + <f64>b.area() }   // duck-typed: a,b BOUND to "has area()"
circle = Circle(2.0f64); bigBox = Rect(3u32, 4u32); tinyBox = Rect(3i16, 4i16)
```
- **Generic class = scheme:** `Rect : ∀w h. (w,h) -> Rect<w,h>` (fresh box per `<field>`).
  **This is the same machine as Dict/Entry.**
- **Duck-typed function = bounded polymorphism:** `combinedArea`'s params carry the constraint
  "must have `area()`" — no inheritance/interface keyword exists in Rune.
- **Numeric tower:** fixed-width types don't silently coerce; casts (`<f64>x`) are mandatory.
- **Monomorphization:** distinct C structs `Circle_f64`, `Rect_u32_u32`, `Rect_i16_i16`, each
  with its own specialized `area()` (different return types per instantiation).

### 3.3 Within-function inference — one pass, order-independent
```rune
func scaleArea(s, factor) {
  base   = s.area()          // base : V1   (and s must have .area())
  bumped = base * factor     // unify: V(factor) = V1 = V(bumped)   — all the SAME box
  return bumped + 0.5f64     // unify: that box = f64
}
```
- **Lesson:** the *last* line pins `factor` to `f64`, even though `factor` last appeared on
  line 2 and the compiler walks the body **once, top-down, never re-reading**. It works
  because `base`/`factor`/`bumped` are three *references to one box*; filling it at the bottom
  is instantly true at the top. The final **zonk** reads it out. `s` ends up bounded to
  `{ area(self) -> f64 }` — structural, = duck typing.

### 3.4 Recursion — monomorphic vs polymorphic
```rune
// MONOMORPHIC: self-call at the SAME type → tie-knot succeeds, ONE specialization
func repeatArea(s, n) {
  if n == 0u32 { return 0.0f64 }
  return <f64>s.area() + repeatArea(s, n - 1u32)
}

// POLYMORPHIC: self-call at a DIFFERENT (growing) type → needs a fixpoint
func incParens(x) { return (x,) }      // +1 tuple layer at the TYPE level
func decParens(x) { return x[0] }      // −1 tuple layer
func addParens(a, b) {
  typeswitch a {
    () => return b                                          // base case: () = type-level 0
    default => return addParens(decParens(a), incParens(b)) // a shrinks, b grows
  }
}
// Concrete seeds make the instantiation set finite:
a = (((),),)        // depth 2
b = ((((),),),)     // depth 3
c = addParens(a, b) // chain: addParens_3_4 → _2_5 → _1_6 → _0_7
```
- **repeatArea:** every self-call is `(s,u32)->f64`, same as the original → one box satisfies
  all levels. Generalize once. Unbounded runtime depth is fine; the *type* never moves.
- **addParens:** each self-call differs by one tuple layer. Forcing one box would assert
  `Tuple^k = Tuple^(k-1)` → **occurs-check failure**. So you enumerate one instantiation per
  type-level, from the **ground seed** down to the **base case**. The count = the seed depth.
- **Bottom-up results:** `_3_4`'s return = `_2_5`'s = … = `_0_7`'s; only `_0_7` (`return b`)
  is concrete. Resolve base-first, propagate up. Top-down → `v-65 unresolved` crash.

---

## 4. The three real targets (where this all points)

| Cluster | Phase it fails | Nature | Fix shape |
|---|---|---|---|
| **recursiveDestructor** (Stage 1, first green) | emission/bind | mutual-recursion SCC (size-2), no leaf-first order | tie-the-knot across the SCC: seed both boxes before either body, run `runDeferredToFixpoint()` (≤2 rounds) |
| **Dict / Entry** (Stage 2) | **typecheck (gate)** | a *missing equation* (Situation B): the inner `Entry` field's instantiation is never created; outer signatures collapse onto one interned class | `materializeInner` at typecheck: build a distinct concrete `Entry<k,v>` per signature, with **P-ALIGN** (align only the template params, skipping the non-template `dict` arg) and **P-OUTER** (a distinct outer class per recorded instantiation) |
| **turing / edwards / gf2 / integer** (Stage 3) | **emission** | a *missing discovery*: polymorphic-recursion chain never enumerated; results never resolved bottom-up | use-driven mono walk (collector worklist + depth guard), restructure `genCPolyInstantiation` to emit each item at top level via its own `poly.open()` frame, plus the bottom-up result pass |

All three are the same disease — **the bootstrap commits too early and keeps no fixpoint
loop to revisit** — unified by one mechanism: a **worklist + named `Blocker` + a
`runDeferredToFixpoint()` loop** (legacy's event-driven park/resume queue, ported minimally).

---

## 5. Concept dependency map (what rests on what)

```
box (mutable type variable) + unify (merge boxes)
   └─ within-function inference: one pass, order-independent, finish with zonk
        └─ generalize (∀ at definition)  +  instantiate (fresh at each use)
             ├─ constraint / bounded polymorphism (duck typing)
             ├─ across-function inference: leaf-first, once each
             │     └─ SCC: a cycle has no leaf-first order → tie-the-knot
             │           ├─ monomorphic recursion → one box, done
             │           └─ polymorphic recursion → fixpoint + ground seed + bottom-up results
             └─ monomorphization: one emitted C copy per instantiation (the gate guarantees
                  every type is concrete before this runs)
```
```

