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
| pidigits            | `pidigits.rn`           | `pidigits_ref.c` (GMP)     | — |
| regex-redux         | `regex_redux.rn`        | `regex_redux_ref.c` (PCRE2)| fasta output |

Each `NAME.stdout` is a golden for a fixed small argument. Every C reference is
verified byte-identical to its Rune counterpart.

**pidigits** uses Rune's opt-in, GMP-backed opaque `BigInt` runtime and a
destination-taking API, so its mutable spigot state is unbounded while reusing
GMP allocations. Its timing workload is the CLBG standard N=10000 digits.
Fixed-width `iN`/`uN` integers remain unchanged; they retain their existing
width, overflow, cast, and bitwise semantics. Building a program that calls a
`bigInt*` builtin links `-lgmp`, so **GMP must be installed** (`libgmp-dev` /
`gmp`); programs that do not use it gain no GMP dependency.

**regex-redux** uses two regex builtins added to the bootstrap compiler —
`regexCount(pattern, text) -> u64` and `regexReplace(pattern, repl, text) ->
string` — backed by a thin PCRE2 shim (`../bootstrap/cbackend/cruntime/regex.inc`).
Building a program that calls them links `-lpcre2-8`, so **PCRE2 must be
installed** (`libpcre2-dev` / `pcre2`); programs that don't use regex gain no
such dependency. The C reference (`regex_redux_ref.c`) also uses PCRE2 and is
byte-identical to the Rune version on the `fasta`-generated golden.

## Build & run one program

```sh
# from the repo root
bootstrap/rune -q -O benchmarks/n_body.rn   # -> benchmarks/n_body (via C + clang -O3)
./benchmarks/n_body 1000
```

Omit `-O` for the clang O0 build; `--optimize` is the equivalent long spelling.

The legacy LLVM compiler (`./rune -U -O benchmarks/NAME.rn`) also builds most of
these and is handy as a second oracle, though it has its own quirks (rejects
underscore identifiers; miscompiles n_body's step loop).

## Timing

```sh
bash benchmarks/bench.sh
```

The harness builds Rune at O0 and O3 plus each naive reference at O3, pins all
serialized work to CPU 0 at reduced priority, verifies the committed goldens and
full timing outputs byte-for-byte, discards one warmup, and records the best of
five measured runs. Stdin workloads are generated once before timing.

See [results.md](results.md) for the current numbers and analysis.
