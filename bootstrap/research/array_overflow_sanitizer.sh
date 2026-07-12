#!/usr/bin/env bash
set -euo pipefail

readonly repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly compiler="$repo/bootstrap/rune"
readonly source="$repo/tests/arrayOverflow.rn"
readonly golden="$repo/tests/arrayOverflow.stdout"
readonly cc="${CC:-clang}"
work="$(mktemp -d "${TMPDIR:-/tmp}/rune-array-overflow.XXXXXX")"
trap 'rm -rf "$work"' EXIT

generated="$work/arrayOverflow.c"
wrapper="$work/array_overflow_canary.c"
executable="$work/array_overflow_canary"
output="$work/output"

nice -n 15 ionice -c 3 taskset -c 15 \
  "$compiler" -q -n --oc "$generated" "$source" >/dev/null

cat >"$wrapper" <<EOF
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int rn_test_fail_realloc;
static void *rn_test_realloc(void *pointer, size_t size) {
  if (rn_test_fail_realloc) {
    return NULL;
  }
  return realloc(pointer, size);
}

#define realloc rn_test_realloc
#define main rn_array_overflow_program_main
#include "$generated"
#undef main
#undef realloc

static void expect_raw_error(const char *expected) {
  assert(rn_try_depth == 0u);
  assert(rn_raised_message_headed == 0u);
  assert(strcmp(rn_raised_message, expected) == 0);
}

int main(void) {
  assert(rn_array_overflow_program_main(0, NULL) == 0);

  if (RN_TRY()) {
    (void)array_alloc(SIZE_MAX, 1u);
    abort();
  } else {
    expect_raw_error(ARRAY_SIZE_OVERFLOW_ERROR);
  }

  if (RN_TRY()) {
    (void)array_checked_add(SIZE_MAX, 1u);
    abort();
  } else {
    expect_raw_error(ARRAY_SIZE_OVERFLOW_ERROR);
  }

  if (RN_TRY()) {
    (void)array_alloc(0u, 0u);
    abort();
  } else {
    expect_raw_error(ARRAY_ZERO_ELEMENT_SIZE_ERROR);
  }

  uint8_t *payload = (uint8_t *)array_alloc(1u, ARRAY_MIN_CAPACITY);
  array_t *header = ((array_t *)payload) - 1;
  const size_t old_capacity = header->capacity;
  rn_test_fail_realloc = 1;
  if (RN_TRY()) {
    (void)array_resize(header, old_capacity + 1u);
    abort();
  } else {
    expect_raw_error(ARRAY_REALLOCATION_ERROR);
  }
  rn_test_fail_realloc = 0;
  assert(header->magic == ARRAY_MAGIC);
  assert(header->capacity == old_capacity);
  assert(header->used == ARRAY_MIN_CAPACITY);
  free(header);
  return 0;
}
EOF

nice -n 15 ionice -c 3 taskset -c 15 \
  "$cc" -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined \
    -Wno-main-return-type -fPIC -o "$executable" "$wrapper" \
    -lm -lpcre2-8 -lgmp -pthread

# btest keeps ordinary children below 8 GiB of virtual address space. ASan's
# sparse shadow mapping needs a larger address-space limit but not comparable
# physical memory, so restore only this sanitized child's soft limit.
(
  ulimit -S -v unlimited
  exec env \
    ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    nice -n 15 ionice -c 3 taskset -c 15 "$executable"
) >"$output"
cmp -s "$output" "$golden"
printf 'array overflow sanitizer canary: PASS\n'
