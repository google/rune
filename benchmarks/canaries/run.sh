#!/usr/bin/env bash
set -euo pipefail
shopt -s inherit_errexit nullglob

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
readonly ROOT
readonly SRC="$ROOT/benchmarks/canaries"
readonly OUT="${TMPDIR:-/tmp}/rune-canaries"
readonly CPU=0
readonly WARMUPS=1
readonly PAIRS=5
readonly CC="${CC:-clang}"
measure=false

quiet_for_measurement() {
  local load1 busy
  read -r load1 _ < /proc/loadavg
  awk -v load_value="$load1" 'BEGIN { exit !(load_value <= 1.0) }' || {
    printf 'Refusing canary timing: one-minute load is %s (> 1.0)\n' "$load1" >&2
    return 1
  }
  busy=$(
    probe_pid=$BASHPID
    ps -eo pid=,ppid=,psr=,pcpu=,comm= |
      awk -v self="$$" -v probe="$probe_pid" \
        '$1 != self && $2 != self && $1 != probe && $2 != probe &&
         $3 == 0 && $4 > 50.0 { print; exit }'
  )
  if [[ -n $busy ]]; then
    printf 'Refusing canary timing: CPU 0 has a >50%% process: %s\n' "$busy" >&2
    return 1
  fi
}

case ${1:-} in
  '') ;;
  --measure) measure=true ;;
  *) printf 'usage: %s [--measure]\n' "$0" >&2; exit 2 ;;
esac

command -v flock >/dev/null 2>&1 || {
  printf 'flock is required for the canary runner\n' >&2
  exit 1
}
# Lock outside OUT so no fixed-output artifact is touched before exclusivity.
exec 9>"${OUT}.lock"
flock -n 9 || {
  printf 'Another canary run owns %s\n' "${OUT}.lock" >&2
  exit 1
}
mkdir -p "$OUT"
taskset -pc "$CPU" "$$" >/dev/null
renice -n 15 -p "$$" >/dev/null
if command -v ionice >/dev/null 2>&1; then
  ionice -c 3 -p "$$" >/dev/null 2>&1 || true
fi

args=()
if [[ -n ${CANARY_SCALE:-} ]]; then
  if [[ ! $CANARY_SCALE =~ ^[0-9]+$ || ${#CANARY_SCALE} -gt 4 ]]; then
    printf 'CANARY_SCALE must be an integer in 1..1000\n' >&2
    exit 2
  fi
  scale_override=$((10#$CANARY_SCALE))
  if ((scale_override < 1 || scale_override > 1000)); then
    printf 'CANARY_SCALE must be an integer in 1..1000\n' >&2
    exit 2
  fi
  args=("$scale_override")
fi

run_to_file() {
  local executable=$1 output=$2
  "$executable" "${args[@]}" > "$output"
}

require_same() {
  local actual=$1 expected=$2 label=$3
  cmp -s "$actual" "$expected" || {
    printf 'FAIL: %s (%s != %s)\n' "$label" "$actual" "$expected" >&2
    return 1
  }
}

elapsed_ns() {
  local executable=$1 start end
  start=$(date +%s%N)
  "$executable" "${args[@]}" >/dev/null
  end=$(date +%s%N)
  ELAPSED_NS=$((end - start))
}

measure_rune_pair() {
  local rune_o0=$1 rune_o3=$2 pair
  local o0_best=999999999999999999 o3_best=999999999999999999
  local o0_sum=0 o3_sum=0 o0_ns o3_ns
  for ((pair = 0; pair < WARMUPS; pair++)); do
    "$rune_o0" "${args[@]}" >/dev/null
    "$rune_o3" "${args[@]}" >/dev/null
  done
  for ((pair = 1; pair <= PAIRS; pair++)); do
    if ((pair % 2 == 1)); then
      elapsed_ns "$rune_o0"; o0_ns=$ELAPSED_NS
      elapsed_ns "$rune_o3"; o3_ns=$ELAPSED_NS
    else
      elapsed_ns "$rune_o3"; o3_ns=$ELAPSED_NS
      elapsed_ns "$rune_o0"; o0_ns=$ELAPSED_NS
    fi
    ((o0_ns < o0_best)) && o0_best=$o0_ns
    ((o3_ns < o3_best)) && o3_best=$o3_ns
    o0_sum=$((o0_sum + o0_ns))
    o3_sum=$((o3_sum + o3_ns))
  done
  O0_BEST_NS=$o0_best
  O3_BEST_NS=$o3_best
  O0_MEAN_NS=$((o0_sum / PAIRS))
  O3_MEAN_NS=$((o3_sum / PAIRS))
}

refs=("$SRC"/*_ref.c)
((${#refs[@]} > 0)) || { printf 'No canary references found\n' >&2; exit 1; }

results_tmp="$OUT/.results.tsv.$$"
trap 'rm -f "$results_tmp"' EXIT INT TERM
if $measure; then
  rm -f "$OUT/results.tsv"
  quiet_for_measurement
  printf 'canary\trune_o0_best_ms\trune_o3_best_ms\trune_o0_mean_ms\trune_o3_mean_ms\n' \
    > "$results_tmp"
fi

for ref_src in "${refs[@]}"; do
  name="$(basename "${ref_src%_ref.c}")"
  rune_src="$SRC/$name.rn"
  golden="$SRC/$name.stdout"
  rune_o0="$OUT/$name.o0"
  rune_o3="$OUT/$name.o3"
  ref="$OUT/$name.ref"
  [[ -f $rune_src ]] || { printf 'Missing Rune source for %s\n' "$name" >&2; exit 1; }

  rm -f "$rune_o0" "$rune_o3" "$ref" \
        "$OUT/$name.o0.c" "$OUT/$name.o3.c"
  "$ROOT/bootstrap/rune" -q --oc "$OUT/$name.o0.c" "$rune_src"
  "$ROOT/bootstrap/rune" -q -O -N --oc "$OUT/$name.o3.c" "$rune_src"
  "$CC" -O3 -ffp-contract=off ${CANARY_CFLAGS:-} \
    -o "$ref" "$ref_src" ${CANARY_LDLIBS:-}

  run_to_file "$ref" "$OUT/$name.ref.out"
  run_to_file "$rune_o0" "$OUT/$name.o0.out"
  run_to_file "$rune_o3" "$OUT/$name.o3.out"
  require_same "$OUT/$name.o0.out" "$OUT/$name.ref.out" "$name Rune O0/reference"
  require_same "$OUT/$name.o3.out" "$OUT/$name.ref.out" "$name Rune O3/reference"
  if [[ -z ${CANARY_SCALE:-} && -f $golden ]]; then
    require_same "$OUT/$name.ref.out" "$golden" "$name reference/golden"
    require_same "$OUT/$name.o0.out" "$golden" "$name Rune O0/golden"
    require_same "$OUT/$name.o3.out" "$golden" "$name Rune O3/golden"
  fi
  printf '%-28s PASS\n' "$name"

  if $measure; then
    quiet_for_measurement
    measure_rune_pair "$rune_o0" "$rune_o3"
    awk -v name="$name" -v o0b="$O0_BEST_NS" -v o3b="$O3_BEST_NS" \
        -v o0m="$O0_MEAN_NS" -v o3m="$O3_MEAN_NS" \
      'BEGIN { printf "%s\t%.3f\t%.3f\t%.3f\t%.3f\n", name, o0b/1e6, o3b/1e6, o0m/1e6, o3m/1e6 }' \
      >> "$results_tmp"
  fi
done

if $measure; then
  mv -f "$results_tmp" "$OUT/results.tsv"
  trap - EXIT INT TERM
  cat "$OUT/results.tsv"
fi
