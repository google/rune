# Bootstrap compiler — known limitations

Status as of suite 200/205 (HEAD `d5f85cc` era).  This documents the
remaining red tests whose fixes require REPRESENTATION-MODEL work
(binary-safe strings, object pools/refcounts) rather than
typechecking/binding/emission fixes, per the migration plan's
fix-or-document done-condition.  Every red test is now either here or
green.  Each entry states the exact gap, the evidence, and what
implementing it would take.

## 1. Binary-safe (length-carrying) strings — Stages A & C landed

**Affected test: `uint2string`.  (`integer` and `escapedCharTest` were
here; now GREEN.)**

STAGE A (`5db6801`) gave string PRODUCERS a length header: an array_t-
style `rn_strhdr` (magic + byte length) before the char data, allocated
by `rn_stralloc`.  `string_length` reads the header when the magic
matches, else falls back to `strlen`.  Every
`cruntime/string_methods.inc` producer now allocates headed and sizes
its source via `string_length`, so embedded NUL bytes propagate.  Fixed
`integer` (toStringLE -> reverse -> toHex preserve the byte length; the
hex-text output has no NULs).

STAGE C (`d5f85cc`) gave string VALUE LITERALS a header: each is
materialized through `rn_strlit(lit, len)` (byte length known at emit
time, embedded NULs copied) instead of a bare C literal; only
CLiteral.Type.String is wrapped (PrintfString format strings stay
bare).  Fixed `escapedCharTest` (the `"...\0"` literal now reports
length 10, not strlen's 9).

Both landed with ZERO regressions (the whole suite exercises strings).

Remaining: `uint2string` needs STAGE B (length-aware PRINTING) plus its
unimplemented `toUintLE`/`toStringLE` methods.  It currently
COMPILE-FAILs (the methods are unimplemented: string method table + the
`toUint` type-arg special case in typechecker.rn ~3275 + two small
`wide_ints.inc` helpers).  Even with those, the golden prints embedded
0x00 bytes to stdout (`block.toStringLE()` of a u128 yields all 16
bytes), and the print path (GlobalStringWriter + `%s`) stops at NUL.

**Remaining implementation shape (Stage B)**: make the
writer/tostring_string_t path length-aware -- write `string_length(s)`
bytes via a length-carrying write instead of `%s` (now possible: headed
strings carry the length; the writer already has a buffer+length
model).  Then implement uint2string's toStringLE/toUintLE methods.
String equality/compare/hash should also move to
`string_length`+`memcmp` for full binary-safety (not required by the
current reds).  Nothing in the typechecker needs to move for Stage B.

## 2. Object pools, reference counts, and slot identity

**Affected tests: `safe`, `allocfree`, `heapqlisttest`,
`defaultMethods`.**

The legacy compiler allocates class instances from PER-CLASS POOLS
(free-listed slots; `<u32>self` is the slot index; a `nextFree` field
lives in the object header) and REFERENCE-COUNTS instances (`ref` /
`unref` statements emitted by relation transformers).  The bootstrap
mallocs individually, has no refcount field, and its statement
emitter treats `StateType.Ref` as a deliberate no-op
(statement.rn:278); the whole emitted .c contains zero refcount
operations.

- `safe`: `child2 = Child(...)` reassignment destroys the old child
  unconditionally although Mom's/Dad's child lists still reference it
  (golden keeps it alive: `appendChild`'s `ref child` pins list
  members).  All BINDING-side work for safe is done — the generated
  methods emit with correct bodies (`2611bf5`); only the refcount
  semantics is missing.
- `allocfree`: uses `appendcode <transformer>` (appendcode targeting a
  TRANSFORMER identifier) which the desugar pass rejects ("Transformer
  identifier not found / target is not a class") — a transformer
  feature gap — and its golden then exercises pool alloc/free
  behavior.
- `heapqlisttest`: compiles and runs; the golden's destroy ORDER and
  object ids reflect legacy pool-slot reuse (`Destroying B 15/10/11/16`
  divergence).
- `defaultMethods`: the auto-generated `show()` method is unimplemented,
  and its golden output is `Foo(1) = {nextFree = 1, value = 123}` —
  it prints the SLOT ID and the pool header field `nextFree`, i.e. the
  golden text is itself a pool-representation artifact.

**Implementation shape**: per-class pool allocation (slot arrays +
free list + `nextFree` header + `<u32>self` = slot index), a refcount
header field with `ref`/`unref` statement emission and
assignment-overwrite/destroy gating, and the generated `show()`
walking fields.  This is the largest remaining chunk of legacy
fidelity; it purely concerns the C backend + runtime (`cruntime/`),
not the type system.

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
