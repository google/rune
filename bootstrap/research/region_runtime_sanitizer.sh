#!/usr/bin/env bash
set -euo pipefail

readonly repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly source="$repo/bootstrap/research/region_runtime_sanitizer.c"
readonly cc="${CC:-clang}"
readonly cpu=15
work="$(mktemp -d "${TMPDIR:-/tmp}/rune-region-sanitizer.XXXXXX")"
trap 'rm -rf "$work"' EXIT
readonly executable="$work/region_runtime_sanitizer"

command -v "$cc" >/dev/null 2>&1
command -v timeout >/dev/null 2>&1

nice -n 15 ionice -c 3 taskset -c "$cpu" \
  "$cc" -std=c11 -O1 -g -fno-omit-frame-pointer \
    -fsanitize=address,undefined -Wall -Wextra -Werror \
    -o "$executable" "$source"

run_sanitized() {
  local mode=${1-}
  (
    # btest.sh limits ordinary children to 8 GiB. ASan needs a much larger
    # sparse virtual shadow mapping, not additional resident memory.
    ulimit -S -v unlimited
    exec env \
      ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1 \
      UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
      nice -n 15 ionice -c 3 taskset -c "$cpu" \
      "$executable" ${mode:+"$mode"}
  )
}

run_sanitized >/dev/null

expect_failure() {
  local mode=$1 status
  set +e
  { timeout 5s bash -c '
      ulimit -S -v unlimited
      exec env \
        ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1 \
        UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
        nice -n 15 ionice -c 3 taskset -c "$1" "$2" "$3"
    ' _ "$cpu" "$executable" "$mode" >/dev/null 2>&1; } 2>/dev/null
  status=$?
  set -e
  test "$status" -ne 0
  test "$status" -ne 124
}

expect_failure overflow-enter
expect_failure overflow-alloc
expect_failure cleanup-active
expect_failure invalid-alignment
expect_failure zero-alignment
expect_failure over-alignment

printf 'region runtime sanitizer: PASS\n'
