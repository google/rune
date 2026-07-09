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
| spectral_norm (3000) |       4953 ms  |  276 ms | 17.95× |
| n_body (5M)          |        694 ms  |  190 ms |  3.65× |
| reverse_complement (5M) |    5260 ms  |   91 ms | 57.80× |

(arg is the CLI argument / input scale; reverse_complement and k_nucleotide read
`fasta` output from stdin.)

Two of the gaps below were subsequently fixed (see "Fixes applied"):
- **n_body was 23×**; exposing libm `sqrt` (a bare `sqrt(x)` now lowers to the
  hardware instruction) dropped it to **3.65×**.
- **spectral_norm is 17.95× in the default checked build**; compiling with the
  new `-U` unsafe flag (which omits fixed-width integer overflow checks) drops it
  to **~5×** (1389 ms), output identical.

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

## Fixes applied

Investigating the slow benchmarks turned up the real bottlenecks — a couple of
which were not what they looked like — and two were fixed:

1. **Hardware `sqrt` (DONE).** A bare `sqrt(x)` call now lowers to libm's `sqrt`
   instruction instead of the software Newton routine. **n_body: 23× → 3.65×.**
   (The clean path types the bare call `f64->f64` on demand at the call site; a
   global builtin symbol or a `misc.rn` function both trip a latent
   tyvar-resolution fragility that corrupts unrelated programs.)
2. **`-U` overflow-elision mode (DONE).** Fixed-width integer add/sub/mul emit
   plain C arithmetic instead of overflow-checked helpers (whose guard costs an
   integer divide per multiply). **spectral_norm: 17.95× → ~5× with `-U`.** Off
   by default; correctness-relevant checks (division, bounds) are kept.
3. **"Inline small functions" — a red herring.** Clang -O3 already inlines across
   the single-`.c` output; adding `static`/`inline` measured 0×. spectral_norm's
   real cost was the overflow checks (#2), not call overhead.
4. **Wide-int compiler crash (DONE, in the dependency).** Rendering a wide-int
   literal >~10571 bits crashed the compiler; root cause was a stack-buffer
   overflow in CTTK (`lib/libcttk.a`), not Rune — see
   [`../patches/`](../patches/README.md). With the patch, pidigits scales from
   265 to ~400 digits.

Still open: **per-byte I/O** (reverse_complement's 57× is `readByte`/`writeByte`
call overhead — bulk `readBytes`/`writeBytes` would close it), and the general
**4–9× codegen band** (value-semantics array copies, vectorization). None are
runtime-model problems — binary_trees (1.05×) proves the allocator is fine; the
rest is C-backend codegen maturity.

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
