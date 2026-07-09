#!/usr/bin/env bash
# Benchmark harness: build each program with the self-hosted (bootstrap) Rune
# compiler and its C/C++ reference at -O3, then time both (best of 3 runs).
# Run from the repo root:  bash benchmarks/bench.sh
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
B=benchmarks
OUT="${TMPDIR:-/tmp}/rune-bench"
mkdir -p "$OUT"

CC=${CC:-clang}
CXX=${CXX:-clang++}

timeit() {  # best-of-3 wall ms; args after the first optional "<infile" marker
  local infile="" best=999999999 i s e d
  if [ "$1" = "-i" ]; then infile="$2"; shift 2; fi
  for i in 1 2 3; do
    s=$(date +%s%N)
    if [ -n "$infile" ]; then "$@" <"$infile" >/dev/null 2>&1
    else "$@" >/dev/null 2>&1; fi
    e=$(date +%s%N); d=$(( (e - s) / 1000000 ))
    (( d < best )) && best=$d
  done
  echo $best
}

buildbs() { rm -f "$B/$1" "$B/$1.c"; bootstrap/rune -q "$B/$1.rn" >/dev/null 2>&1 && cp "$B/$1" "$OUT/$1.bs"; }

printf '%-22s %13s %10s %8s\n' "benchmark(arg)" "bootstrap ms" "C -O3 ms" "Rune/C"
printf '%-22s %13s %10s %8s\n' "----------------------" "------------" "--------" "------"
row() { local r="-"; [ "$3" -gt 0 ] && r=$(awk "BEGIN{printf \"%.2f\", $2/$3}"); printf '%-22s %13s %10s %7sx\n' "$1" "$2" "$3" "$r"; }

# C / C++ references
$CC  -O3 -o "$OUT/n_body_ref"            "$B/n_body_ref.c"            -lm 2>/dev/null
$CC  -O3 -o "$OUT/fannkuch_ref"          "$B/fannkuch_redux_ref.c"        2>/dev/null
$CC  -O3 -o "$OUT/spectral_ref"          "$B/spectral_norm_ref.c"    -lm 2>/dev/null
$CC  -O3 -o "$OUT/mandelbrot_ref"        "$B/mandelbrot_ref.c"            2>/dev/null
$CXX -O3 -o "$OUT/binary_trees_ref"      "$B/binary_trees.cc"             2>/dev/null
$CC  -O3 -o "$OUT/fasta_ref"             "$B/fasta_ref.c"                 2>/dev/null
$CC  -O3 -o "$OUT/revcomp_ref"           "$B/reverse_complement_ref.c"    2>/dev/null
$CC  -O3 -o "$OUT/knuc_ref"              "$B/k_nucleotide_ref.c"          2>/dev/null
$CC  -O3 -o "$OUT/regex_redux_ref"       "$B/regex_redux_ref.c"    -lpcre2-8 2>/dev/null

buildbs binary_trees;   row "binary_trees(18)"   "$(timeit "$OUT/binary_trees.bs" 18)"    "$(timeit "$OUT/binary_trees_ref" 18)"
buildbs fannkuch_redux; row "fannkuch(11)"       "$(timeit "$OUT/fannkuch_redux.bs" 11)"  "$(timeit "$OUT/fannkuch_ref" 11)"
buildbs mandelbrot;     row "mandelbrot(4000)"   "$(timeit "$OUT/mandelbrot.bs" 4000)"    "$(timeit "$OUT/mandelbrot_ref" 4000)"
buildbs spectral_norm;  row "spectral_norm(3000)" "$(timeit "$OUT/spectral_norm.bs" 3000)" "$(timeit "$OUT/spectral_ref" 3000)"
buildbs n_body;         row "n_body(5M)"         "$(timeit "$OUT/n_body.bs" 5000000)"     "$(timeit "$OUT/n_body_ref" 5000000)"
buildbs fasta;          row "fasta(2.5M)"        "$(timeit "$OUT/fasta.bs" 2500000)"      "$(timeit "$OUT/fasta_ref" 2500000)"

"$OUT/fasta.bs" 5000000 > "$OUT/rc_in.txt" 2>/dev/null
buildbs reverse_complement; row "revcomp(5M)"    "$(timeit -i "$OUT/rc_in.txt" "$OUT/reverse_complement.bs")" "$(timeit -i "$OUT/rc_in.txt" "$OUT/revcomp_ref")"
"$OUT/fasta.bs" 1000000 > "$OUT/kn_in.txt" 2>/dev/null
buildbs k_nucleotide;   row "knucleotide(1M)"    "$(timeit -i "$OUT/kn_in.txt" "$OUT/k_nucleotide.bs")" "$(timeit -i "$OUT/kn_in.txt" "$OUT/knuc_ref")"
# regex_redux reuses the 5M fasta input generated for reverse-complement.
buildbs regex_redux;    row "regex_redux(5M)"    "$(timeit -i "$OUT/rc_in.txt" "$OUT/regex_redux.bs")" "$(timeit -i "$OUT/rc_in.txt" "$OUT/regex_redux_ref")"
