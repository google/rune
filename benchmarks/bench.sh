#!/usr/bin/env bash
# Correctness-checked benchmark harness for the self-hosted Rune compiler.
# Builds Rune at generic clang -O0 and host-targeted C -O3 -march=native. The
# native O3 build matches published CLBG host targeting while disabling FP
# contraction for reproducible output; selected benchmarks may use a documented
# alternate C compiler, while O0 remains generic clang. Builds naive C/C++
# references at -O3, verifies golden and timing-workload output, then measures
# one process at a time on a pinned CPU (one discarded warmup plus best-of-five).
set -euo pipefail
shopt -s inherit_errexit

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

readonly B=benchmarks
readonly OUT="${TMPDIR:-/tmp}/rune-bench"
readonly BENCH_CPU=0
readonly BENCH_NICE="${BENCH_NICE:-15}"
readonly WARMUP_RUNS=1
readonly MEASURED_RUNS=5
readonly FASTA_REVERSE_SIZE=5000000
readonly FASTA_KNUCLEOTIDE_SIZE=1000000
readonly -a OPTIMIZED_RUNE_FLAGS=(-O -N)

CC=${CC:-clang}
CXX=${CXX:-clang++}

mkdir -p "$OUT"
if [[ ! $OUT =~ ^[A-Za-z0-9_./-]+$ ]]; then
  printf 'Unsafe benchmark output path: %s\n' "$OUT" >&2
  exit 1
fi
rm -f "$OUT"/*.golden.out "$OUT"/*.timing.out "$OUT/results.tsv"

# Pin this shell once so every child inherits the same affinity without adding
# taskset startup overhead to each timed process. Lower priority keeps the
# serialized run polite on a development machine.
taskset -pc "$BENCH_CPU" "$$" >/dev/null
renice -n "$BENCH_NICE" -p "$$" >/dev/null
if command -v ionice >/dev/null 2>&1; then
  ionice -c 3 -p "$$" >/dev/null 2>&1 || true
fi

readonly -a BENCHMARKS=(
  binary_trees
  fannkuch_redux
  mandelbrot
  spectral_norm
  n_body
  fasta
  reverse_complement
  k_nucleotide
  regex_redux
  pidigits
)

declare -Ar GOLDEN_ARG=(
  [binary_trees]=10
  [fannkuch_redux]=10
  [mandelbrot]=200
  [spectral_norm]=100
  [n_body]=1000
  [fasta]=1000
  [pidigits]=27
)

declare -Ar TIMING_ARG=(
  [binary_trees]=18
  [fannkuch_redux]=11
  [mandelbrot]=4000
  [spectral_norm]=3000
  [n_body]=5000000
  [fasta]=2500000
  [pidigits]=10000
)

declare -Ar RUNE_FLAGS=(
  [binary_trees]=""
  [fannkuch_redux]="-U"
  [mandelbrot]="-U"
  [spectral_norm]="-U"
  [n_body]=""
  [fasta]=""
  [reverse_complement]="-U"
  # Rolling indices and counts are bounded by the benchmark workload.
  [k_nucleotide]="-U"
  [regex_redux]=""
  [pidigits]=""
)

declare -Ar OPTIMIZED_ONLY_RUNE_FLAGS=(
  [mandelbrot]="--gcc"
)

build_rune() {
  local name=$1 mode=$2
  local -a flags=() optimized_only_flags=()
  rm -f "$OUT/$name.$mode" "$OUT/$name.$mode.c"
  if [[ -n ${RUNE_FLAGS[$name]} ]]; then
    read -r -a flags <<< "${RUNE_FLAGS[$name]}"
  fi
  if [[ $mode == o3 ]]; then
    if [[ -n ${OPTIMIZED_ONLY_RUNE_FLAGS[$name]:-} ]]; then
      read -r -a optimized_only_flags <<< "${OPTIMIZED_ONLY_RUNE_FLAGS[$name]}"
    fi
    bootstrap/rune -q "${OPTIMIZED_RUNE_FLAGS[@]}" \
      "${optimized_only_flags[@]}" "${flags[@]}" \
      --oc "$OUT/$name.o3.c" "$B/$name.rn"
  else
    bootstrap/rune -q "${flags[@]}" --oc "$OUT/$name.o0.c" "$B/$name.rn"
  fi
  if [[ ! -x $OUT/$name.$mode ]]; then
    printf 'Rune build did not produce %s\n' "$OUT/$name.$mode" >&2
    return 1
  fi
}

build_reference() {
  local name=$1
  rm -f "$OUT/$name.ref"
  case "$name" in
    binary_trees)
      "$CXX" -O3 -o "$OUT/$name.ref" "$B/binary_trees.cc"
      ;;
    n_body)
      "$CC" -O3 -o "$OUT/$name.ref" "$B/n_body_ref.c" -lm
      ;;
    spectral_norm)
      "$CC" -O3 -o "$OUT/$name.ref" "$B/spectral_norm_ref.c" -lm
      ;;
    regex_redux)
      "$CC" -O3 -o "$OUT/$name.ref" "$B/regex_redux_ref.c" -lpcre2-8
      ;;
    pidigits)
      "$CC" -O3 -o "$OUT/$name.ref" "$B/pidigits_ref.c" -lgmp
      ;;
    *)
      "$CC" -O3 -o "$OUT/$name.ref" "$B/${name}_ref.c"
      ;;
  esac
  if [[ ! -x $OUT/$name.ref ]]; then
    printf 'Reference build did not produce %s\n' "$OUT/$name.ref" >&2
    return 1
  fi
}

run_to_file() {
  local input=$1 output=$2
  shift 2
  if [[ -n $input ]]; then
    "$@" < "$input" > "$output"
  else
    "$@" > "$output"
  fi
}

require_same() {
  local actual=$1 expected=$2 description=$3
  if ! cmp -s "$actual" "$expected"; then
    printf 'FAIL: %s (%s != %s)\n' "$description" "$actual" "$expected" >&2
    return 1
  fi
}

golden_check() {
  local name=$1 input=""
  local golden="$B/$name.stdout"
  local -a args=()
  if [[ -v "GOLDEN_ARG[$name]" ]]; then
    args=("${GOLDEN_ARG[$name]}")
  else
    input="$B/fasta.stdout"
  fi

  run_to_file "$input" "$OUT/$name.o0.golden.out" "$OUT/$name.o0" "${args[@]}"
  run_to_file "$input" "$OUT/$name.o3.golden.out" "$OUT/$name.o3" "${args[@]}"
  run_to_file "$input" "$OUT/$name.ref.golden.out" "$OUT/$name.ref" "${args[@]}"
  require_same "$OUT/$name.o0.golden.out" "$golden" "$name Rune O0 golden"
  require_same "$OUT/$name.o3.golden.out" "$golden" "$name Rune O3 golden"
  require_same "$OUT/$name.ref.golden.out" "$golden" "$name reference golden"
}

timing_input() {
  case "$1" in
    reverse_complement|regex_redux) printf '%s' "$OUT/fasta-5m.in" ;;
    k_nucleotide) printf '%s' "$OUT/fasta-1m.in" ;;
  esac
  return 0
}

timing_workload() {
  local name=$1
  if [[ -v "TIMING_ARG[$name]" ]]; then
    printf '%s' "${TIMING_ARG[$name]}"
  else
    case "$name" in
      reverse_complement|regex_redux) printf 'fasta-%s' "$FASTA_REVERSE_SIZE" ;;
      k_nucleotide) printf 'fasta-%s' "$FASTA_KNUCLEOTIDE_SIZE" ;;
    esac
  fi
  return 0
}

timing_correctness_check() {
  local name=$1 input
  local -a args=()
  input=$(timing_input "$name")
  if [[ -v "TIMING_ARG[$name]" ]]; then
    args=("${TIMING_ARG[$name]}")
  fi

  run_to_file "$input" "$OUT/$name.o0.timing.out" "$OUT/$name.o0" "${args[@]}"
  run_to_file "$input" "$OUT/$name.o3.timing.out" "$OUT/$name.o3" "${args[@]}"
  run_to_file "$input" "$OUT/$name.ref.timing.out" "$OUT/$name.ref" "${args[@]}"
  require_same "$OUT/$name.o0.timing.out" "$OUT/$name.ref.timing.out" \
    "$name Rune O0 timing workload"
  require_same "$OUT/$name.o3.timing.out" "$OUT/$name.ref.timing.out" \
    "$name Rune O3 timing workload"
}

time_one() {
  local input=$1
  shift
  local best=999999999999999999 elapsed start end run

  for ((run = 0; run < WARMUP_RUNS; run++)); do
    if [[ -n $input ]]; then
      "$@" < "$input" >/dev/null || return 1
    else
      "$@" >/dev/null || return 1
    fi
  done
  for ((run = 0; run < MEASURED_RUNS; run++)); do
    start=$(date +%s%N)
    if [[ -n $input ]]; then
      "$@" < "$input" >/dev/null || return 1
    else
      "$@" >/dev/null || return 1
    fi
    end=$(date +%s%N)
    elapsed=$((end - start))
    if ((elapsed < best)); then
      best=$elapsed
    fi
  done
  printf '%s' "$best"
}

printf 'Building Rune O0/O3 and naive references serially on CPU %s...\n' "$BENCH_CPU"
for name in "${BENCHMARKS[@]}"; do
  build_rune "$name" o0
  build_rune "$name" o3
  require_same "$OUT/$name.o0.c" "$OUT/$name.o3.c" "$name generated C"
  build_reference "$name"
done

# Generate each stdin workload once, outside every consumer timing.
"$OUT/fasta.ref" "$FASTA_REVERSE_SIZE" > "$OUT/fasta-5m.in"
"$OUT/fasta.ref" "$FASTA_KNUCLEOTIDE_SIZE" > "$OUT/fasta-1m.in"

printf 'Running golden and timing-workload correctness checks...\n'
for name in "${BENCHMARKS[@]}"; do
  golden_check "$name"
  timing_correctness_check "$name"
  printf '  %-22s PASS\n' "$name"
done
rm -f "$OUT"/*.golden.out "$OUT"/*.timing.out

measurement_date=$(date +%F)
source_revision=$(git rev-parse --short=12 HEAD)
if ! git diff --quiet; then
  source_revision="${source_revision}+dirty"
fi
printf 'date\trevision\tcpu\tbenchmark\tworkload\tflags\trune_o0_ms\trune_o3_ms\tnaive_o3_ms\tfastest_published_ms\to0/naive\to3/naive\to3/fastest\n' \
  | tee "$OUT/results.tsv"
for name in "${BENCHMARKS[@]}"; do
  input=$(timing_input "$name")
  workload=$(timing_workload "$name")
  reported_flags="${OPTIMIZED_RUNE_FLAGS[*]}"
  if [[ -n ${OPTIMIZED_ONLY_RUNE_FLAGS[$name]:-} ]]; then
    reported_flags+=" ${OPTIMIZED_ONLY_RUNE_FLAGS[$name]}"
  fi
  if [[ -n ${RUNE_FLAGS[$name]} ]]; then
    reported_flags+=" ${RUNE_FLAGS[$name]}"
  fi
  args=()
  if [[ -v "TIMING_ARG[$name]" ]]; then
    args=("${TIMING_ARG[$name]}")
  fi

  o0_ns=$(time_one "$input" "$OUT/$name.o0" "${args[@]}")
  o3_ns=$(time_one "$input" "$OUT/$name.o3" "${args[@]}")
  ref_ns=$(time_one "$input" "$OUT/$name.ref" "${args[@]}")
  awk -v date="$measurement_date" -v revision="$source_revision" \
      -v cpu="$BENCH_CPU" -v name="$name" -v workload="$workload" \
      -v flags="$reported_flags" \
      -v o0="$o0_ns" -v o3="$o3_ns" -v ref="$ref_ns" \
      'BEGIN {
        printf "%s\t%s\t%s\t%s\t%s\t%s\t%.6f\t%.6f\t%.6f\tpending\t%.6f\t%.6f\tpending\n",
               date, revision, cpu, name, workload, flags, o0 / 1000000,
               o3 / 1000000, ref / 1000000, o0 / ref, o3 / ref
      }' | tee -a "$OUT/results.tsv"
done

printf 'Correctness: all golden and timing-reference comparisons PASS\n'
printf 'Raw results: %s/results.tsv\n' "$OUT"
