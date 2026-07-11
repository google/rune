#!/usr/bin/env bash
# Structural guards for runtime paths whose observable semantics do not prove
# that the intended optional acceleration was emitted.
set -euo pipefail

readonly ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly REGEX_C="$ROOT/tests/regexBuiltins.c"
readonly PARALLEL_C="$ROOT/tests/parallelMap.c"
readonly PLAIN_C="$ROOT/tests/helloworld.c"

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

require_pattern "$REGEX_C" 'pcre2_jit_match' 'regex generated C'
require_pattern "$REGEX_C" 'PCRE2_INFO_JITSIZE' 'regex generated C'
require_pattern "$REGEX_C" 'PCRE2_ERROR_JIT_STACKLIMIT' 'regex generated C'
require_pattern "$REGEX_C" 'PCRE2_NO_JIT' 'regex generated C'
require_pattern "$REGEX_C" 'jit_ready' 'regex generated C'

require_pattern "$PARALLEL_C" 'pthread_create' 'parallelMap generated C'
require_pattern "$PARALLEL_C" 'atomic_fetch_add_explicit' 'parallelMap generated C'
require_pattern "$PARALLEL_C" 'pthread_join' 'parallelMap generated C'

reject_pattern "$PLAIN_C" 'pcre2_' 'plain generated C'
reject_pattern "$PLAIN_C" 'pthread_' 'plain generated C'
