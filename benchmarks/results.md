# Rune benchmark results

Rune ports of programs from the [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/index.html),
built by the self-hosted bootstrap compiler (Rune-in-Rune, emitting C). This
scoreboard compares Rune at clang `-O0` and `-O3` with the committed naive C/C++
references built locally at `-O3`.

The naive references are correctness oracles, not the fastest published CLBG
programs. Building and validating the published leaders is Stage 2; those times
and ratios are deliberately marked pending below.

## Reproducibility contract

- **Measurement date/source:** 2026-07-10; Stage 0 commit based on parent
  `21f3534`.
- **Rune builds:** `bootstrap/rune -q NAME.rn` for O0 and
  `bootstrap/rune -q -O NAME.rn` for O3. `-O` now selects clang `-O3`;
  `--optimize` is an equivalent long spelling. The compiler default remains O0.
- **Unsafe mode:** fannkuch-redux, mandelbrot, and spectral-norm use `-U` at both
  optimization levels. It removes fixed-width `+`, `-`, and `*` overflow checks;
  bounds and division checks remain. All other rows are checked builds.
- **Naive references:** clang/clang++ `-O3`; n-body and spectral-norm add `-lm`,
  regex-redux adds `-lpcre2-8`, and pidigits adds `-lgmp`.
- **Correctness before timing:** every Rune O0/O3 binary and naive reference is
  byte-identical to the committed small `.stdout` golden. On the full timing
  workload, Rune O0 and O3 are also byte-identical to the naive reference.
- **Timing:** CPU 0 only; one discarded warmup, then the minimum wall time of
  five runs; stdout to `/dev/null`. Runs and builds are serialized. The harness
  lowers itself to nice level 15 and idle I/O priority to remain polite on a
  development machine.
- **Stdin:** generated once before consumer timing with the naive fasta
  reference. `fasta 5000000` is reused by reverse-complement and regex-redux;
  `fasta 1000000` feeds k-nucleotide. Input generation is never timed.
- **Machine:** Intel Core Ultra 9 285H, x86_64, clang 22.1.6, Linux. The CPU
  governor is `powersave`, so affinity and min-of-five are required.
- **Reproduce:** `bash benchmarks/bench.sh`. Raw tab-separated output is written
  to `${TMPDIR:-/tmp}/rune-bench/results.tsv`.

## Stage 0 scoreboard

All rows passed both the golden and full timing-workload correctness checks.
Times are milliseconds; ratios use the harness's unrounded nanosecond samples.

| Date | Revision | Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | fastest published | O0 / naive | O3 / naive | O3 / fastest |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2026-07-10 | `21f3534+stage0` | binary-trees (18) | checked | 1092.621 | 559.730 | 1084.867 | pending | 1.007x | **0.516x** | pending |
| 2026-07-10 | `21f3534+stage0` | fannkuch-redux (11) | `-U` | 2340.287 | 1101.191 | 1073.604 | pending | 2.180x | **1.026x** | pending |
| 2026-07-10 | `21f3534+stage0` | mandelbrot (4000) | `-U` | 1586.631 | 360.621 | 312.353 | pending | 5.080x | **1.155x** | pending |
| 2026-07-10 | `21f3534+stage0` | spectral-norm (3000) | `-U` | 1355.426 | 244.308 | 242.964 | pending | 5.579x | **1.006x** | pending |
| 2026-07-10 | `21f3534+stage0` | n-body (5M) | checked | 652.396 | 169.603 | 170.274 | pending | 3.831x | **0.996x** | pending |
| 2026-07-10 | `21f3534+stage0` | fasta (2.5M) | checked | 2264.612 | 1783.652 | 267.966 | pending | 8.451x | **6.656x** | pending |
| 2026-07-10 | `21f3534+stage0` | reverse-complement (fasta 5M) | checked | 4713.012 | 3260.564 | 77.866 | pending | 60.527x | **41.874x** | pending |
| 2026-07-10 | `21f3534+stage0` | k-nucleotide (fasta 1M) | checked | 511.110 | 108.349 | 189.865 | pending | 2.692x | **0.571x** | pending |
| 2026-07-10 | `21f3534+stage0` | regex-redux (fasta 5M) | checked | 6179.480 | 5959.708 | 5993.815 | pending | 1.031x | **0.994x** | pending |
| 2026-07-10 | `21f3534+stage0` | pidigits (265 digits) | checked | 2013.194 | 376.297 | 1.329 | pending | 1515.381x | **283.248x** | pending |

`21f3534+stage0` means the measured Stage 0 worktree based on parent SHA
`21f3534`; using the eventual commit's own SHA inside that same commit would be
self-referential. Future appended runs should use their exact source SHA.

The first nine rows are the established benchmark set. Their O3/naive geometric
mean is 1.662x because the two per-byte I/O outliers dominate it. Excluding fasta
and reverse-complement, the other seven have a 0.860x geometric mean and every
one is at or below 1.155x the naive reference. The same nine-row O0 geometric
mean is 4.115x.

Pidigits is supplementary rather than CLBG-scale: both ports use the Gibbons
spigot algorithm, but Rune uses fixed `i8192` state and is verified only through
265 digits while the C oracle uses GMP. Its result identifies arbitrary-precision
integers as a language/runtime feature gap, not the cost of a different
algorithm, and no published leader has been measured yet.

## Stage 1 incremental update: buffered byte output

On 2026-07-10, source `b6c5748+buffered-stdout`, `writeByte` and `writeBytes`
were aligned with the legacy runtime: they write through libc's normal stdout
buffer instead of calling `fflush(stdout)` after every invocation. The full
compiler gate passed (`PASS=205 FAIL=0`). Each row below again passed its
committed golden and byte-identical full-workload reference comparison before
the pinned warmup-plus-best-of-five timing.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | O0 / naive | O3 / naive |
|---|---:|---:|---:|---:|---:|---:|
| mandelbrot (4000) | `-U` | 1628.294 | 248.932 | 321.340 | 5.067x | **0.775x** |
| fasta (2.5M) | checked | 630.306 | 294.991 | 278.954 | 2.260x | **1.057x** |
| reverse-complement (fasta 5M) | checked | 1190.246 | 156.516 | 78.418 | 15.178x | **1.996x** |
| k-nucleotide (fasta 1M) | checked | 518.859 | 108.642 | 191.675 | 2.707x | **0.567x** |
| pidigits (265 digits) | checked | 2114.417 | 387.289 | 1.316 | 1606.161x | **294.194x** |

The small k-nucleotide and pidigits deltas are ordinary session variation; they
are included because the shared runtime changed and every affected benchmark is
revalidated. Fasta now meets the naive-reference target. Reverse-complement is
the largest remaining established gap at 1.996x, so it remains the next target.

## Stage 1 incremental update: internal generated-function linkage

On 2026-07-10, source `841dcae+internal-linkage`, generated Rune functions
were given C internal linkage. The program is emitted as one translation unit,
so this lets clang inline local helper calls while preserving Rune function and
function-pointer semantics. Class `toString` forward declarations were updated
to the same linkage. The compiler was rebuilt and the regression gate passed
`PASS=205 FAIL=0`; all ten benchmarks then passed their golden and full-workload
reference comparisons before this complete rebaseline.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | O0 / naive | O3 / naive | fastest published |
|---|---:|---:|---:|---:|---:|---:|---:|
| binary-trees (18) | checked | 1113.166 | 566.272 | 1104.992 | 1.007x | **0.512x** | pending |
| fannkuch-redux (11) | `-U` | 2320.175 | 1162.699 | 1114.638 | 2.082x | **1.043x** | pending |
| mandelbrot (4000) | `-U` | 1519.030 | 248.522 | 318.640 | 4.767x | **0.780x** | pending |
| spectral-norm (3000) | `-U` | 1370.413 | 255.700 | 255.500 | 5.364x | **1.001x** | pending |
| n-body (5M) | checked | 674.900 | 179.007 | 179.876 | 3.752x | **0.995x** | pending |
| fasta (2.5M) | checked | 622.904 | 284.398 | 277.120 | 2.248x | **1.026x** | pending |
| reverse-complement (fasta 5M) | checked | 1177.288 | 141.632 | 77.642 | 15.163x | **1.824x** | pending |
| k-nucleotide (fasta 1M) | checked | 514.742 | 100.177 | 189.930 | 2.710x | **0.527x** | pending |
| regex-redux (fasta 5M) | checked | 6251.908 | 6062.384 | 6022.648 | 1.038x | **1.007x** | pending |
| pidigits (265 digits) | checked | 2208.451 | 386.695 | 1.326 | 1665.361x | **291.601x** | pending |

The established nine-row O3 geometric mean is now 0.906x naive C; excluding
reverse-complement it is 0.830x. The small fannkuch and regex shifts are within
the expected powersave/minimum-sample session variation. Reverse-complement is
the only established gap materially above the 1.15x target.

## Current analysis

### The optimization unlock

The bootstrap driver already had code to choose clang `-O3`, but no command-line
branch could set its `optimized` variable. Earlier benchmark reports therefore
timed generated C at O0 while describing it as optimized. Wiring `-O` removes
that artifact: n-body is 0.996x the naive C reference, spectral-norm is 1.006x,
fannkuch-redux is 1.026x, and regex-redux is 0.994x. The former claims of a
general 4-9x code-generation band and missing cross-function inlining do not
survive a real O3 build.

Binary-trees and k-nucleotide beat their naive references at 0.516x and 0.571x,
respectively. These are comparisons with the committed straightforward oracles,
not claims of beating the published CLBG leaders. Binary-trees benefits from
Rune's reusable object pool; k-nucleotide uses indexed base-4 counters while its
naive C reference linearly searches small k-mer tables.

### Correctness bugs exposed by the new contract

The old k-nucleotide timing was invalid. Its C reference stored the 5,000,000-base
`>THREE` record in a fixed 2,000,000-byte array, writing roughly 3 MB out of
bounds. Repairing that oracle then exposed large-input corruption in the Rune
port's empty-array append and freshly-built string-array paths. The Rune port now
uses explicitly grown non-empty byte storage, fixed-size scratch arrays, numeric
k-mer indices, and direct byte emission. It still processes all 5,000,000 bases;
O0 and O3 match the repaired reference byte-for-byte at the full timing size.

### Stage 1: buffered output removes the flush cliff

The bootstrap `writeByte` helper had called `fflush(stdout)` after every byte.
Removing that hidden flush drops fasta from 6.656x to 1.057x, reverse-complement
from 41.874x to 1.996x, and mandelbrot from 1.155x to 0.775x. This is a runtime
semantic alignment with the legacy implementation, which leaves stdout buffered
until normal process/file flushing.

Giving generated functions internal linkage then lets clang inline reverse-
complement's hot helper calls: it improves from 1.996x to 1.824x. This is a
compiler-wide C emission improvement, not a benchmark-specific annotation; the
full rebaseline remains correct with an established-program geometric mean below
the naive references.

Reverse-complement still reads the roughly 50.8 MB `fasta 5000000` input one
byte at a time and transforms it through a branch-heavy complement function.
Those are now the next costs to attribute; the currently advertised bulk-byte
API is not safe for this rewrite because its bootstrap implementation does not
honor its declared array/length contract. Pidigits still needs a true bignum
facility rather than local code-generation tuning.

## Historical fixes retained in the current source

- A bare `sqrt(x)` lowers to hardware/libm sqrt, which is essential to n-body's
  current parity.
- `-U` provides explicit overflow-check elision for integer-heavy programs while
  leaving it opt-in.
- Regex builtins use a thin PCRE2 shim, explaining regex-redux parity once the
  surrounding generated code is optimized.
- Earlier benchmark work fixed argv indexing, stepped ranges, array value-copy
  semantics, a generated-C `main` collision, and missing parentheses around
  same-precedence right operands.

## Retired pre-Stage-0 measurements

These values are preserved only as history. They were best-of-three, unpinned,
used Rune O0 despite being labeled O3, did not record both optimization levels,
and did not satisfy the current correctness contract. The k-nucleotide reference
also overflowed its fixed buffer at the timing workload.

| Benchmark | recorded Rune | recorded naive O3 | recorded ratio |
|---|---:|---:|---:|
| binary-trees (18) | 1093 ms | 1073 ms | 1.02x |
| fannkuch-redux (11) | 4329 ms | 1087 ms | 3.98x |
| mandelbrot (4000) | 2333 ms | 314 ms | 7.43x |
| fasta (2.5M) | 2411 ms | 269 ms | 8.96x |
| k-nucleotide (fasta 1M) | 520 ms | 56 ms | 9.29x |
| spectral-norm (3000) | 4953 ms | 276 ms | 17.95x |
| n-body (5M) | 694 ms | 190 ms | 3.65x |
| reverse-complement (fasta 5M) | 5260 ms | 91 ms | 57.80x |
| regex-redux (fasta 5M) | 6269 ms | 6023 ms | 1.04x |
