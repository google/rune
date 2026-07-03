# Bootstrap compiler — known limitations

Status: suite 205/205 — the relations->desugar migration is COMPLETE
(every test green, no documented-red carve-outs remain).  This file is
now a HISTORY of the representation-model gaps that were closed to get
there; nothing below is an open limitation.  If a future change reopens
one, this is the record of what it was and how it was fixed.

## 1. Binary-safe (length-carrying) strings — RESOLVED (all three GREEN)

`integer`, `escapedCharTest`, and `uint2string` were here; all now
GREEN.  Strings carry an `array_t`-style length header (`rn_strhdr` =
magic + byte length before the char data; `string_t` stays `char*`),
landed in three stages, all ZERO-regression:
 - Stage A (`5db6801`): PRODUCERS allocate headed via `rn_stralloc` and
   size sources via `string_length` (reads header, else `strlen`).
 - Stage C (`d5f85cc`): VALUE LITERALS materialize via `rn_strlit(lit,
   len)` (only CLiteral.Type.String; PrintfString stays bare).
 - Stage B (`b895d38`): length-aware PRINTING — the writer gained
   `write_bytes`/`write_string` (write a value by `string_length`) and
   `flush` (fwrite the accumulated bytes by length, not `printf %s`);
   plus uint2string's `toUintLE`/wide `toStringLE` (wide_ints.inc
   `wide_from_le_bytes`/`wide_tostring_le`).
Not required by any current red but still strlen-based for full
binary-safety: string equality/compare/hash (move to
`string_length`+`memcmp` if a future test needs it).

## 2. Object pools and slot identity — RESOLVED (all four GREEN)

**Affected tests: `safe`, `defaultMethods`, `heapqlisttest`,
`allocfree` — all now GREEN.**

RESOLVED:
- REFERENCE COUNTING (`bf6f458` / `399f69d`): class structs carry a
  `refCount`, the constructor sets it to 1 (the creation reference,
  matching the legacy pool where `allocate()` sets the slot count to 1);
  reassignment unrefs the old value, exit unrefs reassigned vars, the
  DoublyLinked transformer's `ref`/`unref child` inc/dec with
  destroy-on-zero, and destroy() bumps refCount out of reach to avoid
  re-entrant destroy.  destroy() does NOT free (rn_id is the liveness
  flag) — which is why pointer refcounting works.  Fixed `safe`.
- `show()` auto default method (`399f69d`): `Class_show()` prints
  `<RuneName>(<rn_id>) = {nextFree = <refCount>, <fields>}`.  KEY
  insight (from the legacy .ll): `nextFree` IS the refcount (the pool
  stores the count in the slot's free-list field), and `Foo(1)` is just
  `rn_id` — both of which the bootstrap already has.  Fixed
  `defaultMethods` with NO pool rewrite.

The id FREE-LIST (`d30237d`) landed: `rn_id` now comes from a per-class
free-list (`Class_firstFree`/`Class_nextFree[]`/`Class_used`/
`Class_allocated` + `Class_alloc_id()`), reused when destroy() calls
`Class_free_id(rn_id)`.  Fixed `heapqlisttest` (ids now repeat like the
pool).  Keeps pointer memory — only ids appear in output.

`allocfree` — the LAST red, now GREEN — had TWO blockers, both fixed:
1. `appendcode <function>`: `appendcode runAllocFreeTest { ... }` targets
   a plain FUNCTION, not a class.  desugar.rn instantiateCodeBlock only
   handled class targets + the builtin `Array`; a function target hit
   the fall-through "Code block target is not a class".  FIX: an
   Ident-target branch in instantiateCodeBlock that, when the name
   resolves to a top-level plain function, DIRECTLY appends/prepends the
   code block's child functions and statements into the target's
   subBlock (expandTransFunctionTree / expandTransStatementTree on each
   copy) — NOT copyCodeBlockInto, whose cascade/destroy logic is
   class-oriented.  The lookup uses a new typechecker helper
   `findPlainFunction(name)` that walks the AST from `getMainFunc()`
   (descending only into module/package containers so it finds top-level
   functions, not class methods) — NOT `lookupFunctionDef`, because the
   function-def registry is not yet populated during the desugar pass.
2. SLOT-AS-IDENTITY: the test does `a = Foo(); a.destroy(); b = Foo();
   if a != b { "failed" } else { "passed" }` and expects "passed".  In
   the legacy, instances ARE u32 slots, so a's freed slot is reused by b
   and `a == b`.  FIX: a per-class OBJECT-memory pool in
   clanguageclasses.rn — `<Class>_slots[]` (indexed by rn_id) retains the
   malloc'd object even after destroy (which only frees the id), and a
   new `<Class>_alloc_obj()` reuses that pointer (memset to zero) when
   alloc_id hands the id back, else callocs and records it.  The
   constructor now calls `alloc_obj()` (which also sets rn_id and
   refCount = 1) instead of calloc + alloc_id + refCount.  So `b` reuses
   `a`'s freed storage and `a == b`.  Memory reuse changes object
   identity after destroy for every class — verified ZERO regressions
   across the full 205-test suite (correct refcounting means destroy
   only fires when no live reference remains, so aliasing is safe).

## (Resolved) Debug-trace print cosmetics — gf2 is now GREEN

`gf2` was previously documented here for two debug-print gaps; BOTH are
now fixed and gf2 matches the golden exactly (suite 198/205):

- Array element width naming (`8d5a286`): arrays are now named by the
  RUNE element type (u17_array) not the C storage type (uint32_array),
  via CTypeExpr.arrayElemName() routed through all four array-name
  sites, so two Rune widths sharing a C storage type no longer collide
  onto one tostring with the wrong width suffix.
- Nested print in a print argument (`d23514e`): non-wide CALL arguments
  are hoisted into temporaries before the shared writer is reset, so a
  call whose body prints no longer clobbers the enclosing line.

## Not limitations (open fix work, tracked in HANDOFF.md)

All Stage-C/D binding tests are now green or documented above;
`funcptr`, `integer`, and `gf2` compile and run (`gf2` fully green).
See HANDOFF.md if any regression reopens.
