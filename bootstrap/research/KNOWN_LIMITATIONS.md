# Bootstrap compiler — known limitations

Status as of suite 203/205 (HEAD `399f69d` era).  This documents the
remaining red tests whose fixes require REPRESENTATION-MODEL work (the
object pool / slot representation) rather than typechecking/binding/
emission fixes, per the migration plan's fix-or-document done-
condition.  Every red test is now either here or green.  Each entry
states the exact gap, the evidence, and what implementing it would take.

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

## 2. Object pools and slot identity — refcounts + show() RESOLVED

**Affected tests: `allocfree`, `heapqlisttest`.  (`safe` and
`defaultMethods` were here; now GREEN.)**

RESOLVED so far:
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

Remaining reds need the POOL / SLOT ID behavior — specifically SLOT
REUSE, which the bootstrap's monotonic `rn_id` counter cannot produce:
- `heapqlisttest`: compiles and runs; the golden's destroy ORDER and
  object ids reflect legacy pool-slot REUSE (`Destroying B 15/10/11/16`
  divergence — ids repeat as slots are freed and reallocated).
- `allocfree`: uses `appendcode <transformer>` (appendcode targeting a
  TRANSFORMER identifier) which the desugar pass rejects ("Transformer
  identifier not found / target is not a class") — a transformer
  feature gap — then exercises pool alloc/free.

**Implementation shape (much smaller than a full struct-of-arrays
rewrite — see HANDOFF §2 "HYBRID")**: replace the monotonic `rn_id`
counter (function.rn:1178) with a per-class FREE-LIST (`Class_firstFree`
+ `Class_nextFree[]` + `Class_used` + `Class_allocated` + a generated
`Class_allocate() -> u32`), assign `rn_id = allocate()`, and free the
slot in destroy() (`nextFree[rn_id] = firstFree; firstFree = rn_id`).
Reused ids then match heapqlisttest.  Keep the pointer memory model —
only ids appear in output, so the struct-of-arrays / u32-`self` rewrite
is NOT needed.  Replicate the legacy allocate/free EXACTLY (see
tests/defaultMethods.ll `_Foo_allocate`).  Purely C backend + runtime.

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
