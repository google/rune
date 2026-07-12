#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
compiler="$repo/bootstrap/rune"
fixtures="$repo/bootstrap/research/parallel_map_negative"

check_rejected() {
  local name=$1
  local diagnostic=$2
  local log base status
  base="$fixtures/$name"
  rm -f "$base" "$base.c"
  log=$(mktemp)
  trap 'rm -f "$log"' RETURN
  status=0
  nice -n 15 ionice -c 3 taskset -c 0 \
    "$compiler" -q "$base.rn" >"$log" 2>&1 || status=$?
  test "$status" -ne 0
  grep -Fx "Found 1 type error." "$log" >/dev/null
  grep -F "$diagnostic" "$log" >/dev/null
  test ! -e "$base"
  test ! -e "$base.c"
  rm -f "$log"
  trap - RETURN
}

check_rejected global "cannot access nonlocal 'offset'"
check_rejected transitive_io "cannot use 'println'"
check_rejected raise "cannot use 'raise'"
check_rejected unknown_call "unknown-effect function 'sqrt'"
check_rejected array_transfer "permits only scalar item, context, and result types"
check_rejected string_transfer "permits only scalar item, context, and result types"
check_rejected tuple_transfer "permits only scalar item, context, and result types"
check_rejected class_transfer "permits only scalar item, context, and result types"
check_rejected function_transfer "permits only scalar item, context, and result types"
check_rejected indirect_callback "requires a direct named callback"
check_rejected nested "cannot call 'parallelMap'"
check_rejected random "cannot use process-global random state"
check_rejected local_string_format "callback expressions must be scalar"
check_rejected local_array "callback expressions must be scalar"
check_rejected array_item "permits only scalar item, context, and result types"
check_rejected array_result "permits only scalar item, context, and result types"
check_rejected transitive_global "cannot access nonlocal 'offset'"
check_rejected extern "cannot call extern or bodyless function 'scalarExtern'"
check_rejected argv "cannot access nonlocal 'argv'"
check_rejected cast "callbacks cannot use casts"
check_rejected user_operator "registered user operator"

# Rune ChoiceType is an inference constraint and cannot currently be
# materialized as an array element/runtime argument, so no source fixture can
# reach this boundary without failing earlier. Keep the fail-closed predicate
# under a source assertion until a runtime choice representation exists.
choice_guard=$(grep -A5 \
  "func isParallelMapTransferScalar" "$repo/bootstrap/types/typechecker.rn")
grep -F "Type.TypeClass.Choice" <<<"$choice_guard" >/dev/null
grep -F "return false" <<<"$choice_guard" >/dev/null

# Shadowing the builtin must be accepted and lowered as an ordinary user
# function. This also exercises an indirect callback, which the builtin would
# reject. Remove stale outputs so success cannot be inherited from an older run.
shadow_base="$fixtures/shadow"
rm -f "$shadow_base" "$shadow_base.c"
shadow_log=$(mktemp)
trap 'rm -f "$shadow_log"' EXIT
nice -n 15 ionice -c 3 taskset -c 0 \
  "$compiler" -q "$shadow_base.rn" >"$shadow_log" 2>&1
test -x "$shadow_base"
test "$(nice -n 15 ionice -c 3 taskset -c 0 "$shadow_base")" = "8"
rm -f "$shadow_log" "$shadow_base" "$shadow_base.c"
trap - EXIT

# A main-thread try combined with parallelMap must emit thread-local exception
# frames. Worker callbacks still cannot declare try/raise themselves.
tls_base="$fixtures/tls_try"
rm -f "$tls_base" "$tls_base.c"
tls_log=$(mktemp)
trap 'rm -f "$tls_log"' EXIT
nice -n 15 ionice -c 3 taskset -c 0 \
  "$compiler" -q "$tls_base.rn" >"$tls_log" 2>&1
test -x "$tls_base"
test "$(nice -n 15 ionice -c 3 taskset -c 0 "$tls_base")" = \
  "[4u64, 5u64]"
grep -F "static _Thread_local jmp_buf rn_try_stack" "$tls_base.c" >/dev/null
grep -F "rn_parallel_map_try_guard_enter" "$tls_base.c" >/dev/null
rm -f "$tls_log" "$tls_base" "$tls_base.c"
trap - EXIT

# The caller participates in work. Mask its enclosing try until every join so
# checked callback failure aborts rather than longjmping past live workers.
overflow_base="$fixtures/overflow_under_try"
rm -f "$overflow_base" "$overflow_base.c"
overflow_compile_log=$(mktemp)
overflow_run_log=$(mktemp)
trap 'rm -f "$overflow_compile_log" "$overflow_run_log"' EXIT
nice -n 15 ionice -c 3 taskset -c 0 \
  "$compiler" -q "$overflow_base.rn" >"$overflow_compile_log" 2>&1
test -x "$overflow_base"
grep -F "uint32_t savedTryDepth = rn_parallel_map_try_guard_enter();" \
  "$overflow_base.c" >/dev/null
grep -F "rn_parallel_map_try_guard_leave(savedTryDepth);" \
  "$overflow_base.c" >/dev/null
set +e
timeout 5s nice -n 15 ionice -c 3 taskset -c 0 \
  "$overflow_base" >"$overflow_run_log" 2>&1
overflow_status=$?
set -e
test "$overflow_status" -ne 0
test "$overflow_status" -ne 124
! grep -F "caught" "$overflow_run_log" >/dev/null
! grep -F "returned" "$overflow_run_log" >/dev/null
rm -f "$overflow_compile_log" "$overflow_run_log" \
  "$overflow_base" "$overflow_base.c"
trap - EXIT

echo "parallelMap negative diagnostics: PASS"
