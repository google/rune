# Bootstrap compiler — known limitations

Status as of suite 202/205 (HEAD `bf6f458` era).  This documents the
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

## 2. Object pools and slot identity — refcounts RESOLVED

**Affected tests: `allocfree`, `heapqlisttest`, `defaultMethods`.
(`safe` was here; now GREEN.)**

REFERENCE COUNTING landed (`bf6f458`, `safe` GREEN): class structs
carry a `refCount`; a module class-var assignment unrefs the old value
and refs the new; the DoublyLinked transformer's `ref child`/`unref
child` (previously no-ops) now inc/dec with destroy-on-zero; exit
unref is registered only on reassignment; and destroy() bumps refCount
out of reach to avoid re-entrant destroy.  So an UNSHARED object dies
on overwrite/exit while a SHARED one (held by a relation list)
survives.  destroy() still does NOT free (rn_id is the liveness flag),
which is exactly why pointer-based refcounting works here.

Remaining reds need the POOL / SLOT representation (independent of
refcounts):
- `allocfree`: uses `appendcode <transformer>` (appendcode targeting a
  TRANSFORMER identifier) which the desugar pass rejects ("Transformer
  identifier not found / target is not a class") — a transformer
  feature gap — and its golden then exercises pool alloc/free.
- `heapqlisttest`: compiles and runs; the golden's destroy ORDER and
  object ids reflect legacy pool-slot reuse (`Destroying B 15/10/11/16`
  divergence).
- `defaultMethods`: the auto-generated `show()` is unimplemented, and
  its golden `Foo(1) = {nextFree = 1, value = 123}` prints the SLOT ID
  and the pool header `nextFree` — the golden text is itself a
  pool-representation artifact.

**Implementation shape**: per-class pool allocation (slot arrays + free
list + `nextFree` header + `<u32>self` = slot index) so instances are
`u32` slot indices into per-class arrays rather than malloc'd pointers,
plus the generated `show()` walking fields.  The single largest change
in the migration — struct layout, the `self` param type (u32 vs
pointer), field access, construction, destruction, and every method
call.  Purely C backend + runtime; not the type system.

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
