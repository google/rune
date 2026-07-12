#!/usr/bin/env bash
set -euo pipefail

readonly repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly compiler="$repo/bootstrap/rune"
readonly source="$repo/bootstrap/research/string_header_sanitizer.rn"
readonly cc="${CC:-clang}"
work="$(mktemp -d "${TMPDIR:-/tmp}/rune-string-header-sanitizer.XXXXXX")"
trap 'rm -rf "$work"' EXIT

generated="$work/string_header_sanitizer.c"
executable="$work/string_header_sanitizer"

command -v "$cc" >/dev/null 2>&1
nice -n 15 ionice -c 3 taskset -c 0 \
  "$compiler" -q -n --oc "$generated" "$source" >/dev/null

nice -n 15 ionice -c 3 taskset -c 0 \
  "$cc" -O1 -g -fno-omit-frame-pointer \
    -fsanitize=address,undefined -Wno-main-return-type -fPIC \
    -o "$executable" "$generated" -lm -lpcre2-8 -lgmp

# btest.sh limits ordinary children to 8 GiB of virtual address space. ASan
# needs a much larger sparse shadow mapping even for this tiny program, so
# only the sanitized child restores the inherited hard limit.
printf 'A\0B\n' | (
  ulimit -S -v unlimited
  exec env \
    -u RUNE_STRING_HEADER_CANARY_MISSING_7E0A01D8 \
    RUNE_STRING_HEADER_CANARY=headed \
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    nice -n 15 ionice -c 3 taskset -c 0 "$executable"
) >/dev/null

printf 'headed string sanitizer canary: PASS\n'
