# Rune benchmarks

Rune ports of programs from the [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/index.html),
used to exercise and measure the **self-hosted (bootstrap) Rune compiler**
(`../bootstrap/rune`, which emits C).

## Programs

| Program | Rune source | C/C++ reference | stdin |
|---|---|---|---|
| binary-trees        | `binary_trees.rn`       | `binary_trees.cc`          | — |
| fannkuch-redux      | `fannkuch_redux.rn`     | `fannkuch_redux_ref.c`     | — |
| mandelbrot          | `mandelbrot.rn`         | `mandelbrot_ref.c`         | — |
| spectral-norm       | `spectral_norm.rn`      | `spectral_norm_ref.c`      | — |
| n-body              | `n_body.rn`             | `n_body_ref.c`             | — |
| fasta               | `fasta.rn`              | `fasta_ref.c`              | — |
| reverse-complement  | `reverse_complement.rn` | `reverse_complement_ref.c` | fasta output |
| k-nucleotide        | `k_nucleotide.rn`       | `k_nucleotide_ref.c`       | fasta output |

Each `NAME.stdout` is a golden for a fixed small argument. Every C reference is
verified byte-identical to its Rune counterpart.

Not yet ported: **pidigits** (needs unbounded bignum) and **regex-redux**
(needs a regex engine Rune does not have).

## Build & run one program

```sh
# from the repo root
bootstrap/rune -q benchmarks/n_body.rn      # -> benchmarks/n_body (via C + clang)
./benchmarks/n_body 1000
```

The legacy LLVM compiler (`../rune -U -O benchmarks/NAME.rn`) also builds most of
these and is handy as a second oracle, though it has its own quirks (rejects
underscore identifiers; miscompiles n_body's step loop).

## Timing

```sh
bash benchmarks/bench.sh     # builds every program + its C ref, times best-of-3
```

See [results.md](results.md) for the current numbers and analysis.
