#!/usr/bin/env bash
set -euo pipefail

readonly repo="$(cd "$(dirname "$0")/../.." && pwd)"
readonly compiler="$repo/bootstrap/rune"
readonly fixtures="$repo/bootstrap/research/with_region_negative"
readonly cpu=15

check_rejected() {
  local name=$1 diagnostic=$2 base log status summary_count diagnostic_count
  base="$fixtures/$name"
  rm -f "$base" "$base.c"
  log=$(mktemp)
  trap 'rm -f "$log"' RETURN
  status=0
  nice -n 15 ionice -c 3 taskset -c "$cpu" \
    "$compiler" -q "$base.rn" >"$log" 2>&1 || status=$?
  test "$status" -ne 0
  summary_count=$(grep -Fxc "Found 1 type error." "$log" || true)
  diagnostic_count=$(grep -Fc ": $diagnostic" "$log" || true)
  test "$summary_count" -eq 1
  test "$diagnostic_count" -eq 1
  test ! -e "$base"
  test ! -e "$base.c"
  rm -f "$log"
  trap - RETURN
}

check_rejected context_string \
  "withRegion stage 1 permits only scalar context and result types"
check_rejected result_class \
  "withRegion stage 1 permits only scalar context and result types"
check_rejected indirect_callback \
  "withRegion requires a direct named callback"
check_rejected global \
  "withRegion callback cannot access nonlocal 'regionOffset'"
check_rejected transitive_io \
  "withRegion callback cannot use 'println'"
check_rejected local_array \
  "withRegion callback values must be scalar or region-local classes"
check_rejected cast_escape \
  "withRegion callbacks cannot use casts"
check_rejected destroy \
  "withRegion callback cannot call 'destroy'"
check_rejected final_class \
  "withRegion cannot allocate class 'FinalNode' with final behavior"
check_rejected refwidth_class \
  "withRegion cannot allocate class 'NarrowNode' with explicit reference width"
check_rejected relation_class \
  "withRegion cannot allocate relational class 'RelatedNode'"
check_rejected class_field_string \
  "withRegion class 'StringNode' fields must be scalar or region-local classes"
check_rejected extern_call \
  "withRegion callback cannot call extern or bodyless function 'regionExternal'"
check_rejected raise \
  "withRegion callback cannot use 'raise'"
check_rejected user_operator \
  "withRegion cannot allocate class 'OperatorNode' with user operators"
check_rejected nested_parallel \
  "withRegion callback cannot call 'parallelMap'"
check_rejected try "withRegion callback cannot use 'try'"
check_rejected assert "withRegion callback cannot use 'assert'"
check_rejected random \
  "withRegion callback cannot use process-global random state"
check_rejected generic_class \
  "withRegion requires monomorphic concrete classes"
check_rejected generic_callback \
  "withRegion callback graphs must be monomorphic"
check_rejected destroy_behavior \
  "withRegion cannot allocate class 'ManagedRegionNode' with destroy behavior"
check_rejected wide_boundary \
  "withRegion stage 1 permits only scalar context and result types"
check_rejected wide_field \
  "withRegion class 'WideRegionNode' fields must be scalar or region-local classes"
check_rejected show "withRegion callback cannot call 'show'"

# Shadowing the builtin must remain ordinary Rune code and may accept an
# indirect callback. Remove stale outputs so success cannot be inherited.
shadow_base="$fixtures/shadow"
shadow_log=$(mktemp)
trap 'rm -f "$shadow_log"' EXIT
rm -f "$shadow_base" "$shadow_base.c"
nice -n 15 ionice -c 3 taskset -c "$cpu" \
  "$compiler" -q "$shadow_base.rn" >"$shadow_log" 2>&1
test -x "$shadow_base"
test "$(nice -n 15 ionice -c 3 taskset -c "$cpu" "$shadow_base")" = \
  "8"
rm -f "$shadow_log" "$shadow_base" "$shadow_base.c"
trap - EXIT

# The wrapper must mask an enclosing try frame until after region cleanup.
# Checked callback failure therefore aborts instead of longjmping over live
# arena blocks and reaching either marker below.
overflow_base="$fixtures/overflow_under_try"
overflow_compile_log=$(mktemp)
overflow_run_log=$(mktemp)
trap 'rm -f "$overflow_compile_log" "$overflow_run_log"' EXIT
rm -f "$overflow_base" "$overflow_base.c"
nice -n 15 ionice -c 3 taskset -c "$cpu" \
  "$compiler" -q "$overflow_base.rn" >"$overflow_compile_log" 2>&1
test -x "$overflow_base"
set +e
{ timeout 5s nice -n 15 ionice -c 3 taskset -c "$cpu" \
    "$overflow_base" >"$overflow_run_log" 2>&1; } 2>/dev/null
overflow_status=$?
set -e
test "$overflow_status" -ne 0
test "$overflow_status" -ne 124
! grep -F "caught" "$overflow_run_log" >/dev/null
! grep -F "returned" "$overflow_run_log" >/dev/null
rm -f "$overflow_compile_log" "$overflow_run_log" \
  "$overflow_base" "$overflow_base.c"
trap - EXIT

printf 'withRegion negative diagnostics: PASS\n'
