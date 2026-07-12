#!/usr/bin/env bash
# Bootstrap-compiler test harness (the migration's suite gate).
#
# Builds nothing; assumes `bootstrap/rune` is already built (cd bootstrap && make).
# For each tests/<name>.stdout: rm stale exe/.c/.result, compile the .rn with
# bootstrap/rune, require an executable, run it (with optional .stdin), and
# compare stdout to the .stdout golden. Prints `PASS=N FAIL=M` and a sorted
# FAILED list. Floor for the relations->desugar migration: 188/205, zero drops.
#
# NOTE: the top-level ./runtests.sh uses the LEGACY ./rune (259/3) — a DIFFERENT
# metric. This harness currently measures 210 positive programs plus
# fail-closed negative compiler canaries. Do not conflate them.
# Keep ordinary test children below 8 GiB, but do not lower the hard limit:
# the dedicated ASan canary restores its soft limit to reserve sparse shadow
# memory without increasing its physical-memory use.
ulimit -S -v 8388608
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT" || exit 1
RUNE=bootstrap/rune
pass=0
fail=0
failed=""
for so in tests/*.stdout; do
  base="${so%.stdout}"
  rn="$base.rn"
  [ -f "$rn" ] || continue
  name="$(basename "$base")"
  rm -f "$base" "$base.result" "$base.c" 2>/dev/null
  if ! "$RUNE" -q "$rn" >/dev/null 2>&1; then
    fail=$((fail+1)); failed="$failed $name"; continue
  fi
  if [ ! -x "$base" ]; then
    fail=$((fail+1)); failed="$failed $name"; continue
  fi
  # Invoke via "./tests/<name>" so argv[0] matches the goldens (printargv's
  # golden is exactly "./tests/printargv"); a bare relative path fails it.
  if [ -f "$base.stdin" ]; then
    if ! "./$base" < "$base.stdin" > "$base.result" 2>/dev/null; then
      fail=$((fail+1)); failed="$failed $name"
      continue
    fi
  else
    if ! "./$base" > "$base.result" 2>/dev/null; then
      fail=$((fail+1)); failed="$failed $name"
      continue
    fi
  fi
  if cmp -s "$base.result" "$so"; then
    pass=$((pass+1))
  else
    fail=$((fail+1)); failed="$failed $name"
  fi
done
# Negative compiler canaries are fail-closed but do not add to the positive
# PASS count. A missing diagnostic, accidental executable, or shadowing
# regression is one named gate failure.
if ! bash bootstrap/research/parallel_map_negative.sh >/dev/null 2>&1; then
  fail=$((fail+1)); failed="$failed parallel_map_negative"
fi
if ! bash bootstrap/research/runtime_codegen_canaries.sh >/dev/null 2>&1; then
  fail=$((fail+1)); failed="$failed runtime_codegen_canaries"
fi
if ! bash bootstrap/research/byte_string_cast_negative.sh >/dev/null 2>&1; then
  fail=$((fail+1)); failed="$failed byte_string_cast_negative"
fi
if ! bash bootstrap/research/string_header_sanitizer.sh >/dev/null 2>&1; then
  fail=$((fail+1)); failed="$failed string_header_sanitizer"
fi
if ! bash bootstrap/research/array_overflow_sanitizer.sh >/dev/null 2>&1; then
  fail=$((fail+1)); failed="$failed array_overflow_sanitizer"
fi
if ! bash bootstrap/research/with_region_negative.sh >/dev/null 2>&1; then
  fail=$((fail+1)); failed="$failed with_region_negative"
fi
if ! bash bootstrap/research/region_runtime_sanitizer.sh >/dev/null 2>&1; then
  fail=$((fail+1)); failed="$failed region_runtime_sanitizer"
fi
echo "PASS=$pass FAIL=$fail"
printf 'FAILED:'
for f in $(echo $failed | tr ' ' '\n' | sort); do printf ' %s' "$f"; done
echo
if ((fail > 0)); then
  exit 1
fi
