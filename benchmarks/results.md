# Rune benchmark results

Rune ports of programs from the [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/index.html),
built by the **self-hosted (bootstrap) Rune compiler** — the Rune-in-Rune
compiler that emits C — and compared against hand-written C/C++ references at
`-O3`.

## Methodology

- **Rune compiler:** `bootstrap/rune` (self-hosted, emits C, then `clang -O3`).
  Built each program with `bootstrap/rune -q NAME.rn`.
- **C/C++ references:** `NAME_ref.c` (or `binary_trees.cc`), `clang/clang++ -O3`
  (`-lm` where needed). Each reference is verified to produce **byte-identical
  output** to the Rune version on the timing workload, so the two do the same work.
- **Timing:** best (min) of 3 runs, wall clock, output to `/dev/null`.
- **Machine:** Intel Core Ultra 9 285H, x86_64, clang 22.1.6, Linux.
- Reproduce with `bash bench.sh` (from the repo root).

All eight programs are correct: each Rune build matches its committed `.stdout`
golden and matches the C reference byte-for-byte across the sizes tested.

## Results

| Benchmark (arg)      | bootstrap Rune | C `-O3` | Rune / C |
|----------------------|---------------:|--------:|---------:|
| binary_trees (18)    |       1093 ms  | 1073 ms |  **1.02×** |
| fannkuch_redux (11)  |       4329 ms  | 1087 ms |  3.98× |
| mandelbrot (4000)    |       2333 ms  |  314 ms |  7.43× |
| fasta (2.5M)         |       2411 ms  |  269 ms |  8.96× |
| k_nucleotide (1M)    |        520 ms  |   56 ms |  9.29× |
| spectral_norm (3000) |       4737 ms  |  250 ms | 18.95× |
| n_body (5M)          |       4160 ms  |  180 ms | 23.11× |
| reverse_complement (5M) |    4977 ms  |   89 ms | 55.92× |

(arg is the CLI argument / input scale; reverse_complement and k_nucleotide read
`fasta` output from stdin.)

## Analysis

The spread — from parity to ~56× — tracks *what each benchmark spends its time
on*, and points at three concrete gaps in the current bootstrap C backend.

**Parity when the bottleneck isn't codegen — binary_trees (1.02×).** Dominated
by allocating and freeing millions of tree nodes. Rune's reference-counted
object allocation is competitive with C++ `unique_ptr`, so the generated-code
quality barely matters here. This is the encouraging data point: Rune's runtime
is not the problem.

**No function inlining — spectral_norm (19×).** The hot loop calls `evalA(i,j)`
once per matrix element (~180M calls at N=3000). The C compiler inlines it to a
few arithmetic ops; the bootstrap backend emits a real C function call every
time (confirmed in the generated C), and clang can't inline across the call
because the whole inner loop is a call. This is the single biggest structural
gap and also inflates n_body.

**No hardware `sqrt` — n_body (23×).** Rune exposes no libm intrinsic, so
`math.sqrt` is a software Newton's-method routine. Isolating it (same 5M
workload):

| n_body 5M                     | time   |
|-------------------------------|-------:|
| C, libm `sqrt`                | 181 ms |
| C, **same** Newton `sqrt`     | 1164 ms |
| Rune (Newton `sqrt`)          | 4133 ms |

So the 23× headline factors as **~6.4× (software vs hardware sqrt) × ~3.5×
(pure codegen)**. Running the *same* algorithm, Rune is ~3.5× C — the honest
codegen number. Exposing libm `sqrt` would drop n_body to roughly that.

**Per-byte I/O — reverse_complement (56×).** Reads ~25 MB one byte at a time via
`readByte()`/`writeByte()`, each a runtime function call, versus C's buffered
`getc`/`putc`. Almost entirely call overhead, not codegen. Bulk I/O
(`readBytes`/`writeBytes`) would close most of this gap.
(An earlier version of this benchmark *looked* 2× faster than C — it was a bug:
a fixed 10000-byte sequence buffer silently truncated large inputs so it did
almost no work. Fixed to grow by doubling; the honest number is 56×.)

**The middle band (4–9×) — fannkuch, mandelbrot, fasta, k_nucleotide.** Tight
compute/array loops with no calls and no sqrt. This is the baseline
Rune-vs-C codegen overhead: bounds/idiom differences, less aggressive
vectorization, and Rune's value-semantics array copies. 4–9× is a reasonable
starting point for an unoptimized C-emitting compiler.

## Takeaways / optimization opportunities (in impact order)

1. **Inline small functions** in the C backend (or mark them `static inline`) —
   would help spectral_norm (~19×) and n_body most.
2. **Expose libm math** (`sqrt`, etc.) as intrinsics — turns n_body from 23× into
   ~3.5×. (Attempted via `extern "C"`, but the bootstrap frontend crashes on the
   fp extern and the generic extern path has no libm wiring — tracked as future
   compiler work.)
3. **Buffer byte I/O** or steer these benchmarks to bulk `readBytes`/`writeBytes`
   — fixes reverse_complement's 56×.
4. General inner-loop codegen (the 4–9× band): revisit value-semantics array
   copies and give clang more to work with.

None of these are runtime-model problems (binary_trees proves the allocator is
fine) — they're all C-backend codegen maturity, which is expected for a
compiler that only recently began compiling this many non-trivial programs.

## Bugs found and fixed along the way

Porting these benchmarks to the bootstrap compiler surfaced five real
compiler/codegen bugs, each fixed with the suite held at 205/205:

- `argv[i]` indexed `void*` (returned void) — `rn_argv_array` now returns `char**`.
- `range(lo, hi, step)` ignored the step (hardcoded +1).
- Array assignment `q = p` aliased the buffer instead of copying (value semantics).
- A user function named `main` collided with the C entry point.
- **The C backend dropped parentheses around a same-precedence right operand**:
  `a / (b * c)` emitted as `a / b * c`. A broad correctness bug the test suite
  never happened to exercise; found because n_body's `dt / (d2 * dist)` came out
  as `(dt / d2) * dist`.
