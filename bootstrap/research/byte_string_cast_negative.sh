#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
compiler="$repo/bootstrap/rune"
fixtures="$repo/bootstrap/research/byte_string_cast_negative"

check_rejected() {
  local name=$1 diagnostic=$2 base log summary_count diagnostic_count status
  base="$fixtures/$name"
  rm -f "$base" "$base.c"
  log=$(mktemp)
  trap 'rm -f "$log"' RETURN
  status=0
  nice -n 15 ionice -c 3 taskset -c 0 \
    "$compiler" -q "$base.rn" >"$log" 2>&1 || status=$?
  test "$status" -ne 0
  summary_count=$(grep -Fxc "Found 1 type error." "$log" || true)
  # A normal source diagnostic is emitted as path:line: message. Requiring
  # that form also rejects an internal assertion/raise that merely echoes the
  # expected words.
  diagnostic_count=$(grep -Fc ": $diagnostic" "$log" || true)
  test "$summary_count" -eq 1
  test "$diagnostic_count" -eq 1
  test ! -e "$base"
  test ! -e "$base.c"
  rm -f "$log"
  trap - RETURN
}

check_rejected array_to_string "Only [u8] arrays can be cast to string"
check_rejected string_to_array "Strings can only be cast to [u8]"
check_rejected polymorphic_array_to_string "Only [u8] arrays can be cast to string"
check_rejected polymorphic_string_to_array "Strings can only be cast to [u8]"
check_rejected mixed_array_to_string_valid_first \
  "Only [u8] arrays can be cast to string"
check_rejected mixed_array_to_string_invalid_first \
  "Only [u8] arrays can be cast to string"
check_rejected mixed_array_literal_to_string_valid_first \
  "Only [u8] arrays can be cast to string"
check_rejected mixed_array_literal_to_string_invalid_first \
  "Only [u8] arrays can be cast to string"
check_rejected mixed_string_to_array_valid_first \
  "Strings can only be cast to [u8]"
check_rejected mixed_string_to_array_invalid_first \
  "Strings can only be cast to [u8]"

echo "byte/string cast negative diagnostics: PASS"
