# Bootstrap compiler — known limitations

Status as of suite 197/205 (HEAD `f96ba07` era).  This documents the
remaining red tests whose fixes require REPRESENTATION-MODEL work
rather than typechecking/binding/emission fixes, per the migration
plan's fix-or-document done-condition.  Each entry states the exact
gap, the evidence, and what implementing it would take.

## 1. Binary-safe (length-carrying) strings

**Affected tests: `escapedCharTest`, `uint2string`, `integer`.**

The bootstrap represents strings as NUL-terminated C `char *`
throughout (`cruntime/string_methods.inc` is `strlen`-based; printing
routes through `printf`-family `%s`).  The legacy compiler's strings
carry an explicit length and are binary-safe.

- `escapedCharTest`: the test string ends with an embedded `\0` and
  asserts `s.length() == 10`; `strlen` sees 9 and the assert aborts
  (SIGABRT before any output).
- `uint2string`: `toUintLE`/`toStringLE` are unimplemented, but even
  with them added (string method table + the `toUint` type-arg special
  case in typechecker.rn ~3275 + two small `wide_ints.inc` helpers —
  all mechanical), the golden REQUIRES printing two embedded 0x00
  bytes (`block.toStringLE()` of a u128 yields all 16 bytes); a
  NUL-terminated string cannot carry them to the writer.
- `integer` (moved here from the binding cluster at `f96ba07`): the
  test now typechecks, compiles with zero C errors, and runs; every
  remaining output delta is this gap.  `Integer.data =
  uintValue.toStringLE()` embeds NUL bytes (`1u32` -> `01 00 00 00`),
  so the strlen-based `string_length`/`resize`/`reverse` helpers see
  length 1 and `toHex` prints `0x03` where the golden wants
  `0x00000003`.  No further binding work applies.

**Implementation shape**: migrate the emitted string type to a
(pointer, length) pair (or length-headed buffer like `rn_wide`), and
update: every `cruntime/string_methods.inc` helper, the
GlobalStringWriter (`%s` paths must become length-aware writes),
string literals' emission, string equality/compare/hash, and every
`char *` parameter/field type the C backend names.  A contained but
cross-cutting change; nothing in the typechecker needs to move.

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

## Not limitations (open fix work, tracked in HANDOFF.md)

`funcptr`, `gf2`, `integer` — one constraint-generalization root
(choice/constraint arms freezing onto free template vars; see
HANDOFF.md for the constraint-by-copy direction and per-test state).
