#!/usr/bin/env bash
# Structural guards for runtime paths whose observable semantics do not prove
# that the intended optional acceleration was emitted.
set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly REGEX_C="$ROOT/tests/regexBuiltins.c"
readonly PARALLEL_C="$ROOT/tests/parallelMap.c"
readonly PLAIN_C="$ROOT/tests/helloworld.c"
readonly REGEX_EXE="$ROOT/tests/regexBuiltins"
readonly PLAIN_EXE="$ROOT/tests/helloworld"
readonly ESCAPES_C="$ROOT/tests/escapes.c"
readonly PRINTARGV_C="$ROOT/tests/printargv.c"
readonly GETENV_C="$ROOT/tests/getenv_test.c"
readonly CWD_C="$ROOT/tests/cwdtest.c"
readonly FLOAT_C="$ROOT/tests/float2string.c"
readonly TUPLE_STRING_C="$ROOT/tests/tupletostring.c"
readonly INT_STRING_C="$ROOT/tests/inttostring.c"
readonly BIGNUM_C="$ROOT/tests/bignumarray.c"
readonly TRY_C="$ROOT/tests/basicTryCatch.c"
readonly STATIC_LITERAL_C="$ROOT/tests/staticStringLiterals.c"
readonly BTEST="$ROOT/bootstrap/research/btest.sh"
readonly ARRAY_SIZE_INC="$ROOT/bootstrap/cbackend/cruntime/arrays/size.inc"
readonly ARRAY_RESIZE_INC="$ROOT/bootstrap/cbackend/cruntime/arrays/resize.inc"
readonly ARRAY_CONCAT_INC="$ROOT/bootstrap/cbackend/cruntime/arrays/concat.inc"

command -v rg >/dev/null 2>&1 || {
  printf 'runtime codegen canaries require rg\n' >&2
  exit 1
}
command -v readelf >/dev/null 2>&1 || {
  printf 'runtime codegen canaries require readelf\n' >&2
  exit 1
}

require_pattern() {
  local file=$1 pattern=$2 label=$3
  [[ -r $file ]] || { printf 'missing %s: %s\n' "$label" "$file" >&2; return 1; }
  rg -q --fixed-strings "$pattern" "$file" || {
    printf '%s missing %s\n' "$label" "$pattern" >&2
    return 1
  }
}

reject_pattern() {
  local file=$1 pattern=$2 label=$3
  [[ -r $file ]] || { printf 'missing %s: %s\n' "$label" "$file" >&2; return 1; }
  if rg -q --fixed-strings "$pattern" "$file"; then
    printf '%s unexpectedly contains %s\n' "$label" "$pattern" >&2
    return 1
  fi
}

require_count() {
  local file=$1 pattern=$2 expected=$3 label=$4
  local actual
  [[ -r $file ]] || { printf 'missing %s: %s\n' "$label" "$file" >&2; return 1; }
  actual="$({ rg -o --fixed-strings "$pattern" "$file" || true; } | wc -l)"
  actual="${actual//[[:space:]]/}"
  if [[ $actual != "$expected" ]]; then
    printf '%s expected %s occurrences of %s, found %s\n' \
      "$label" "$expected" "$pattern" "$actual" >&2
    return 1
  fi
}

require_needed() {
  local executable=$1 pattern=$2 label=$3
  [[ -x $executable ]] || {
    printf 'missing %s executable: %s\n' "$label" "$executable" >&2
    return 1
  }
  readelf -d "$executable" | rg -q --fixed-strings "$pattern" || {
    printf '%s does not link %s\n' "$label" "$pattern" >&2
    return 1
  }
}

reject_needed() {
  local executable=$1 pattern=$2 label=$3
  [[ -x $executable ]] || {
    printf 'missing %s executable: %s\n' "$label" "$executable" >&2
    return 1
  }
  if readelf -d "$executable" | rg -q --fixed-strings "$pattern"; then
    printf '%s unexpectedly links %s\n' "$label" "$pattern" >&2
    return 1
  fi
}

require_pattern "$REGEX_C" 'pcre2_jit_compile' 'regex generated C'
require_pattern "$REGEX_C" 'int rc = pcre2_match(' 'regex generated C'
require_pattern "$REGEX_C" 'PCRE2_ERROR_JIT_STACKLIMIT' 'regex generated C'
require_pattern "$REGEX_C" 'PCRE2_NO_JIT' 'regex generated C'
reject_pattern "$REGEX_C" 'pcre2_jit_match(' 'regex generated C'

require_pattern "$PARALLEL_C" 'pthread_create' 'parallelMap generated C'
require_pattern "$PARALLEL_C" 'atomic_fetch_add_explicit' 'parallelMap generated C'
require_pattern "$PARALLEL_C" 'pthread_join' 'parallelMap generated C'
require_pattern "$PARALLEL_C" '#define ARRAY_MAGIC UINT64_C(0xA99A73A656658C59)' \
  'parallelMap generated C'
require_pattern "$PARALLEL_C" 'uint64_t magic;' 'parallelMap generated C'
require_pattern "$PARALLEL_C" 'sizeof(array_t) == 32u' 'parallelMap generated C'
reject_pattern "$PARALLEL_C" 'magic_hdr' 'parallelMap generated C'
reject_pattern "$PARALLEL_C" 'magic_ftr' 'parallelMap generated C'
require_pattern "$PARALLEL_C" '#define ARRAY_MIN_CAPACITY ((size_t)16u)' \
  'parallelMap generated C'
require_pattern "$PARALLEL_C" 'static inline size_t array_checked_add(' \
  'parallelMap generated C'
require_pattern "$PARALLEL_C" 'array_initial_capacity(element_size, num_initializers)' \
  'parallelMap generated C'
require_pattern "$ARRAY_RESIZE_INC" 'resized->capacity = new_capacity;' \
  'array resize runtime'
reject_pattern "$ARRAY_RESIZE_INC" 'a->capacity = new_capacity;' \
  'array resize runtime'
require_pattern "$ARRAY_CONCAT_INC" 'const int self_concat = dest == source;' \
  'array concat runtime'
require_pattern "$ARRAY_CONCAT_INC" 'source = dest;' 'array concat runtime'
require_pattern "$ESCAPES_C" 'string_from_u8_array(' 'escapes generated C'
require_pattern "$ESCAPES_C" 'u8_array_from_string(' 'escapes generated C'
reject_pattern "$ESCAPES_C" '(string_t)(escapeStrings)' 'escapes generated C'
require_pattern "$ESCAPES_C" 'return string_compare(a, b) == 0;' \
  'escapes generated C'
require_pattern "$ESCAPES_C" 'string_compare(orderedLow, orderedHigh)' \
  'escapes generated C'

# Every Rune-visible string is headed. Raw C pointers are copied at the ABI
# boundary, and no runtime operation guesses by reading before such a pointer.
require_pattern "$PLAIN_C" 'static inline char *rn_strfromc(const char *s)' \
  'plain generated C'
require_pattern "$PLAIN_C" '#define RN_STR_MAGIC UINT64_C(0x726E537472486472)' \
  'plain generated C'
require_pattern "$PLAIN_C" \
  '#define RN_STATIC_STR_MAGIC UINT64_C(0x726E537472537461)' \
  'plain generated C'
require_pattern "$PLAIN_C" 'uint64_t magic;' 'plain generated C'
require_pattern "$PLAIN_C" 'sizeof(rn_strhdr) == 16u' 'plain generated C'
reject_pattern "$PLAIN_C" 'size_t magic;' 'plain generated C'
require_pattern "$PLAIN_C" 'assert(h->magic == RN_STR_MAGIC);' 'plain generated C'
require_pattern "$PLAIN_C" \
  'assert(h->magic == RN_STR_MAGIC || h->magic == RN_STATIC_STR_MAGIC);' \
  'plain generated C'
require_pattern "$PLAIN_C" 'return (uint64_t)rn_strheader(s)->len;' \
  'plain generated C'
reject_pattern "$PLAIN_C" 'return (uint64_t)strlen(s);' 'plain generated C'
require_pattern "$PRINTARGV_C" 'arr[i] = rn_strfromc(argv[i]);' \
  'printargv generated C'
reject_pattern "$PRINTARGV_C" 'arr[i] = (char *)argv[i];' 'printargv generated C'
require_pattern "$GETENV_C" 'rn_strfromc(rn_getenv(' 'getenv generated C'
require_pattern "$CWD_C" 'static _Thread_local char buf[RN_GETCWD_BUFFER_SIZE];' \
  'getcwd generated C'
reject_pattern "$CWD_C" 'calloc(1, 4096)' 'getcwd generated C'
require_pattern "$FLOAT_C" 'return rn_strfromc(tmp);' 'float generated C'
require_pattern "$TUPLE_STRING_C" 'memcpy(r, GlobalStringWriter_string(), len);' \
  'tuple tostring generated C'
require_pattern "$INT_STRING_C" 'char *out = rn_stralloc(outlen);' \
  'integer tostring generated C'
require_pattern "$INT_STRING_C" 'rn_strfree(s);' 'integer tostring generated C'
# The explicit BigInt builtin already returns a headed Rune string.  It must
# bypass the conventional raw extern-string ingress copy (and its strlen).
require_pattern "$BIGNUM_C" 'rn_bigint_to_string(result);' \
  'bigint tostring generated C'
reject_pattern "$BIGNUM_C" 'rn_strfromc(rn_bigint_to_string' \
  'bigint tostring generated C'
require_pattern "$TRY_C" 'rn_raised_message_headed' 'try generated C'
require_pattern "$TRY_C" 'GlobalStringWriter_write_string(' \
  'try generated C'
require_pattern "$TRY_C" 'rn_dup_writer()' 'try generated C'
reject_pattern "$TRY_C" 'rn_strfromc(GlobalStringWriter_string())' \
  'try generated C'
require_pattern "$TRY_C" '? rn_raised_message : rn_strfromc(rn_raised_message)' \
  'try generated C'
reject_pattern "$TRY_C" 'string_dup(rn_raised_message)' 'try generated C'

# Rune string value literals are immutable headed objects, deduplicated by
# exact bytes. The source has three unique values and five literal sites
# ("hot" occurs twice inside a 100,000-iteration loop; the binary value is
# also returned by a function). The runtime has one string_dup definition and
# one static-only COW call; no literal evaluation may call the retired
# per-evaluation rn_strlit allocator.
require_count "$STATIC_LITERAL_C" 'static const rn_string_literal_' 3 \
  'static literal generated C'
require_count "$STATIC_LITERAL_C" '(char *)rn_string_literal_' 5 \
  'static literal generated C'
require_count "$STATIC_LITERAL_C" '  "hot"' 1 'static literal generated C'
require_count "$STATIC_LITERAL_C" '  "A\000B"' 1 'static literal generated C'
require_count "$STATIC_LITERAL_C" '  char data[4];' 2 'static literal generated C'
require_count "$STATIC_LITERAL_C" '  char data[1];' 1 'static literal generated C'
require_count "$STATIC_LITERAL_C" '  {RN_STATIC_STR_MAGIC, 3},' 2 \
  'static literal generated C'
require_count "$STATIC_LITERAL_C" '  {RN_STATIC_STR_MAGIC, 0},' 1 \
  'static literal generated C'
require_count "$STATIC_LITERAL_C" 'string_dup(' 2 'static literal generated C'
reject_pattern "$STATIC_LITERAL_C" 'rn_strlit' 'static literal generated C'
require_pattern "$STATIC_LITERAL_C" \
  '_Static_assert(offsetof(rn_string_literal_' 'static literal generated C'
require_pattern "$STATIC_LITERAL_C" 'string_set_index(&returnedFirst' \
  'static literal generated C'
require_pattern "$STATIC_LITERAL_C" 'string_set_index_value(returnedLiteral()' \
  'static literal generated C'
require_pattern "$STATIC_LITERAL_C" 'string_set_index_value(returnedStruct().value' \
  'static literal generated C'

# A positive program must satisfy both its exact output and process status.
# Keep both stdin and no-stdin branches fail-closed.
require_pattern "$BTEST" 'if ! "./$base" < "$base.stdin" > "$base.result"' \
  'bootstrap regression gate'
require_pattern "$BTEST" 'if ! "./$base" > "$base.result"' \
  'bootstrap regression gate'

reject_pattern "$PLAIN_C" 'pcre2_' 'plain generated C'
reject_pattern "$PLAIN_C" 'pthread_' 'plain generated C'
require_needed "$REGEX_EXE" 'libpcre2-8.so' 'regex executable'
reject_needed "$PLAIN_EXE" 'libpcre2-8.so' 'plain executable'
