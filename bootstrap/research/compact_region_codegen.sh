#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
RUNE="$ROOT/bootstrap/rune"
SOURCE="$ROOT/tests/withRegion.rn"
GOLDEN="$ROOT/tests/withRegion.stdout"
FLAG_OFF_C="$ROOT/tests/withRegion.c"
readonly FLAG_OFF_C_SHA256="a7ebcc6520dfa8f294d77464f56f3f466e70aa62e68fe6e93f7818448843910d"
ZERO_SOURCE="$ROOT/bootstrap/research/compact_region_zero_field.rn"
ZERO_GOLDEN="$ROOT/bootstrap/research/compact_region_zero_field.stdout"
DEFERRED_SOURCE="$ROOT/bootstrap/research/compact_region_deferred_discovery.rn"
DEFERRED_GOLDEN="$ROOT/bootstrap/research/compact_region_deferred_discovery.stdout"
LATE_SOURCE="$ROOT/bootstrap/research/compact_region_late_discovery.rn"
LATE_GOLDEN="$ROOT/bootstrap/research/compact_region_late_discovery.stdout"
CROSS_SOURCE="$ROOT/bootstrap/research/compact_region_cross_class.rn"
CROSS_GOLDEN="$ROOT/bootstrap/research/compact_region_cross_class.stdout"
TMP="$(mktemp -d /tmp/rune-compact-region.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

fail() {
  printf 'compact-region codegen: FAIL: %s\n' "$1" >&2
  exit 1
}

require_fixed() {
  local needle="$1"
  local file="$2"
  rg -Fq -- "$needle" "$file" || fail "missing '$needle'"
}

run_low() {
  nice -n 15 ionice -c 3 taskset -c 15 "$@"
}

# The opt-in must not perturb ordinary generated C at all. Pin the pre-feature
# codegen independently; comparing only with btest's freshly generated copy
# would let two outputs from the same changed compiler agree accidentally.
run_low "$RUNE" -q -n --oc "$TMP/off.c" "$SOURCE"
printf '%s  %s\n' "$FLAG_OFF_C_SHA256" "$TMP/off.c" |
  sha256sum --check --status - || fail "flag-off C baseline changed"
cmp -s "$TMP/off.c" "$FLAG_OFF_C" || fail "flag-off C changed"

# Generate twice to make the no-counter compact naming contract executable.
run_low "$RUNE" -q --compact-regions -O -N --oc "$TMP/compact.c" "$SOURCE"
run_low "$RUNE" -q --compact-regions -n --oc "$TMP/compact-repeat.c" "$SOURCE"
cmp -s "$TMP/compact.c" "$TMP/compact-repeat.c" ||
  fail "compact generated C is not reproducible"
run_low "$TMP/compact" > "$TMP/compact.out"
cmp -s "$TMP/compact.out" "$GOLDEN" || fail "Clang output mismatch"

COMPACT_C="$TMP/compact.c"

# One source class has both representations in the same translation unit.
# The ordinary allocator is pool-only under the flag; only the compact clone
# may call the region allocator.
require_fixed 'struct RegionNode_u8_t {' "$COMPACT_C"
require_fixed 'uint32_t rn_id;' "$COMPACT_C"
require_fixed 'uint32_t refCount;' "$COMPACT_C"
require_fixed 'struct rn_compact_RegionNode_u8_t {' "$COMPACT_C"
require_fixed 'rn_compact_RegionNode_u8_t * left;' "$COMPACT_C"
require_fixed 'rn_compact_RegionNode_u8_t * right;' "$COMPACT_C"
require_fixed 'return (rn_compact_RegionNode_u8_t *)rn_region_alloc_zeroed' "$COMPACT_C"

ordinary_allocator="$({
  sed -n '/^static RegionNode_u8_t \*RegionNode_u8_alloc_obj(void)/,/^void tostring_RegionNode_u8_t/p' \
    "$COMPACT_C"
} || true)"
[[ -n "$ordinary_allocator" ]] || fail "ordinary allocator block missing"
if rg -q 'rn_region_is_active|rn_region_alloc|RN_REGION_OBJECT_ID' \
    <<<"$ordinary_allocator"; then
  fail "ordinary allocator retained a region path"
fi

compact_struct="$(sed -n \
  '/^struct rn_compact_RegionNode_u8_t {/,/^};/p' "$COMPACT_C")"
[[ -n "$compact_struct" ]] || fail "compact struct block missing"
if rg -q 'rn_id|refCount' <<<"$compact_struct"; then
  fail "compact struct retained an object header"
fi
if rg -q 'tostring_rn_compact|rn_compact_.*_(show|destroy)' "$COMPACT_C"; then
  fail "debug or destruction helper was cloned for a compact class"
fi

# Recursive constructor/function/method calls, nesting, and the parallel
# worker must remain entirely in the compact family.
require_fixed 'node->left = rn_compact_makeRegionTree(childDepth);' "$COMPACT_C"
require_fixed 'total = total + rn_compact_RegionNode_u8_check(leftNode);' "$COMPACT_C"
require_fixed 'rn_compact_RegionNode_u8_check(rn_compact_makeRegionTree(depth))' "$COMPACT_C"
require_fixed 'rn_compact_nestedRegionCheck' "$COMPACT_C"
require_fixed 'rn_compact_regionTreeCheck, 1536ul' "$COMPACT_C"
require_fixed 'parallelRegionCheck' "$COMPACT_C"
require_fixed 'rn_compact_regionTreeCheck, 0ul' "$COMPACT_C"

# The same source class is still usable normally across every region scope.
require_fixed 'static RegionNode_u8_t * ordinaryNode;' "$COMPACT_C"
require_fixed 'static RegionNode_u8_t * makeRegionTree(uint32_t depth)' "$COMPACT_C"

# GCC and both address/undefined sanitizers exercise strict-aliasing and
# representation boundaries independently of Clang's optimized build.
printf '#include "%s"\n' "$COMPACT_C" > "$TMP/layout.c"
printf '%s\n' \
  '_Static_assert(sizeof(RegionNode_u8_t) == 24, "ordinary RegionNode size");' \
  '_Static_assert(sizeof(rn_compact_RegionNode_u8_t) == 16, "compact RegionNode size");' \
  >> "$TMP/layout.c"
run_low gcc -std=c11 -pedantic-errors -O3 -fomit-frame-pointer \
  -o "$TMP/gcc" "$TMP/layout.c" -lm -pthread
run_low "$TMP/gcc" > "$TMP/gcc.out"
cmp -s "$TMP/gcc.out" "$GOLDEN" || fail "GCC output mismatch"

run_low clang -std=c11 -pedantic-errors -O3 -fomit-frame-pointer \
  -Wno-main-return-type -o "$TMP/clang-pedantic" "$TMP/layout.c" -lm -pthread
run_low "$TMP/clang-pedantic" > "$TMP/clang-pedantic.out"
cmp -s "$TMP/clang-pedantic.out" "$GOLDEN" ||
  fail "pedantic Clang output mismatch"

(
  ulimit -S -v unlimited
  run_low clang -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Wno-main-return-type -fPIC -o "$TMP/sanitize" "$COMPACT_C" -lm -pthread
  ASAN_OPTIONS=detect_leaks=0 run_low "$TMP/sanitize" > "$TMP/sanitize.out"
)
cmp -s "$TMP/sanitize.out" "$GOLDEN" || fail "sanitizer output mismatch"

# The optimization flag must not weaken the validator's existing boundary.
for rejected in generic_class show cast_escape; do
  if run_low "$RUNE" -q --compact-regions -n \
      --oc "$TMP/rejected-$rejected.c" \
      "$ROOT/bootstrap/research/with_region_negative/$rejected.rn" \
      >"$TMP/rejected-$rejected.log" 2>&1; then
    fail "negative canary '$rejected' compiled"
  fi
done

# A fieldless class receives one compiler-owned byte, while its ordinary form
# retains the normal two-word header. Both compilers reject empty-struct
# extensions under -pedantic-errors, so this is also a standards canary.
run_low "$RUNE" -q --compact-regions -O -N \
  --oc "$TMP/zero.c" "$ZERO_SOURCE"
run_low "$TMP/zero" > "$TMP/zero.out"
cmp -s "$TMP/zero.out" "$ZERO_GOLDEN" || fail "zero-field output mismatch"
require_fixed 'struct rn_compact_EmptyRegionValue_u8_t {' "$TMP/zero.c"
require_fixed 'uint8_t rn_compact_nonempty;' "$TMP/zero.c"
if rg -q 'rn_compact_EmptyRegionValue_u8_ordinaryAnnouncement' "$TMP/zero.c"; then
  fail "ordinary-only I/O method was cloned"
fi
printf '#include "%s"\n' "$TMP/zero.c" > "$TMP/zero-size.c"
printf '%s\n' \
  '_Static_assert(sizeof(EmptyRegionValue_u8_t) == 8, "ordinary header size");' \
  '_Static_assert(sizeof(rn_compact_EmptyRegionValue_u8_t) == 1, "compact dummy size");' \
  >> "$TMP/zero-size.c"
run_low clang -std=c11 -pedantic-errors -O3 -Wno-main-return-type \
  -o "$TMP/zero-clang" "$TMP/zero-size.c" -lm
run_low gcc -std=c11 -pedantic-errors -O3 \
  -o "$TMP/zero-gcc" "$TMP/zero-size.c" -lm

# Multiple compact classes must keep distinct pointer types across fields,
# helpers, and methods that accept and return region-local references. This
# catches prototype ordering or an accidental ordinary/compact ABI mix that a
# single recursive class cannot expose.
run_low "$RUNE" -q --compact-regions -O -N \
  --oc "$TMP/cross.c" "$CROSS_SOURCE"
run_low "$TMP/cross" > "$TMP/cross.out"
cmp -s "$TMP/cross.out" "$CROSS_GOLDEN" ||
  fail "cross-class output mismatch"
require_fixed 'struct rn_compact_CompactLeaf_u8_t {' "$TMP/cross.c"
require_fixed 'struct rn_compact_CompactHolder_u8_t {' "$TMP/cross.c"
require_fixed 'rn_compact_CompactLeaf_u8_t * leaf;' "$TMP/cross.c"
require_fixed 'rn_compact_CompactHolder_u8_replace_rn_compact_CompactLeaf_u8' \
  "$TMP/cross.c"
require_fixed 'rn_compact_CompactLeaf_u8_t * rn_compact_passCompactLeaf' \
  "$TMP/cross.c"
require_fixed 'return rn_compact_CompactLeaf_u8_read(other);' "$TMP/cross.c"
run_low gcc -std=c11 -pedantic-errors -O3 \
  -o "$TMP/cross-gcc" "$TMP/cross.c" -lm
run_low "$TMP/cross-gcc" > "$TMP/cross-gcc.out"
cmp -s "$TMP/cross-gcc.out" "$CROSS_GOLDEN" ||
  fail "cross-class GCC output mismatch"

# Compiler-owned compact names may not alias user declarations. The same
# sources remain legal without the opt-in flag.
for collision in function class parameter local; do
  collision_source="$ROOT/bootstrap/research/compact_region_collision_$collision.rn"
  run_low "$RUNE" -q -n --oc "$TMP/collision-$collision-off.c" \
    "$collision_source"
  if run_low "$RUNE" -q --compact-regions -n \
      --oc "$TMP/collision-$collision-on.c" "$collision_source" \
      >"$TMP/collision-$collision.log" 2>&1; then
    fail "compact namespace collision '$collision' compiled"
  fi
  rg -Fq -- "reserves the generated C prefix 'rn_compact_'" \
    "$TMP/collision-$collision.log" ||
    fail "compact namespace collision '$collision' lacked its diagnostic"
done

# A withRegion call can first become concrete while an ordinary generic body
# is materialized during C emission. Clone only after that discovery pass has
# reached a fixed point; eager one-shot emission misses this callback graph.
run_low "$RUNE" -q --compact-regions -O -N \
  --oc "$TMP/deferred.c" "$DEFERRED_SOURCE"
run_low "$TMP/deferred" > "$TMP/deferred.out"
cmp -s "$TMP/deferred.out" "$DEFERRED_GOLDEN" ||
  fail "deferred-discovery output mismatch"
require_fixed 'rn_compact_regionWorker' "$TMP/deferred.c"

# An early ordinary call must not seal the compact graph before a later nested
# deferred specialization discovers a second callback. Both declarations must
# exist before C compilation, and the fully linked executable must be correct.
run_low "$RUNE" -q --compact-regions -O -N \
  --oc "$TMP/late.c" "$LATE_SOURCE"
run_low "$TMP/late" > "$TMP/late.out"
cmp -s "$TMP/late.out" "$LATE_GOLDEN" ||
  fail "late-discovery output mismatch"
require_fixed 'rn_compact_earlyWorker' "$TMP/late.c"
require_fixed 'rn_compact_lateWorker' "$TMP/late.c"

printf 'compact-region codegen: PASS\n'
