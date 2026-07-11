# Rune benchmark results

Rune ports of programs from the [Computer Language Benchmarks Game](https://benchmarksgame-team.pages.debian.net/benchmarksgame/index.html),
built by the self-hosted bootstrap compiler (Rune-in-Rune, emitting C). This
scoreboard compares Rune at clang `-O0` and `-O3` with the committed naive C/C++
references built locally at `-O3`.

The naive references are correctness oracles, not the fastest published CLBG
programs. Stage 2 has validated reverse-complement's leader under a deliberately
constrained one-core setup; the remaining published-leader columns are pending.

## Reproducibility contract

- **Measurement date/source:** 2026-07-10; Stage 0 commit based on parent
  `21f3534`.
- **Rune builds:** `bootstrap/rune -q NAME.rn` for O0 and
  `bootstrap/rune -q -O NAME.rn` for O3. `-O` now selects clang `-O3`;
  `--optimize` is an equivalent long spelling. The compiler default remains O0.
- **Unsafe mode:** fannkuch-redux, k-nucleotide, mandelbrot, spectral-norm, and
  reverse-complement use `-U` at both optimization levels. It removes fixed-width
  `+`, `-`, and `*` overflow checks; bounds and division checks remain.
  K-nucleotide's rolling indices and counts are bounded by the generated input
  workload, so they cannot overflow at the measured size. All other rows are
  checked builds.
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

## Stage 1 incremental update: line-based reverse-complement input

On 2026-07-10, source `f9f8069+readln-revcomp`, bootstrap `readln()` was made
length-safe by returning a headed Rune string. Reverse-complement now consumes
complete FASTA lines through bootstrap `getline` rather than issuing one
`getchar` per input byte. The compiler was rebuilt and the 205-test gate passed;
both O0 and O3 match the committed golden and the full 50.8 MB naive-reference
output byte-for-byte.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | O0 / naive | O3 / naive | fastest published |
|---|---:|---:|---:|---:|---:|---:|---:|
| reverse-complement (fasta 5M) | checked | 1049.213 | 135.825 | 77.415 | 13.553x | **1.755x** | pending |

The CLBG FASTA input has no blank lines, so its empty `readln()` result is an
unambiguous EOF marker. Record capacity remains dynamically doubled; no input
size or amount of work was reduced.

## Stage 1 incremental update: exact buffered reverse-complement output

On 2026-07-10, source `ae3b6cb+bulk-byte-writer`, the bootstrap C runtime's
byte-array writer was made binary-safe and range-aware. It now writes from the
Rune array header's used length rather than `strlen`, and its one-, two-, and
three-argument Rune calls lower to a fixed `(bytes, count, offset)` C ABI. A
zero count means the remaining bytes after the offset. The corresponding reader
now returns a headed byte array with its actual `fread` length, including on a
short read. The low-level C ABI is correct, but inferring the result type of a
standalone `readBytes` call is a separate bootstrap type-checker repair. An
explicit `<[u8]>readBytes(n)` cast now emits its required C typedef and is
covered by the 205-test suite; this benchmark only uses the fully typed writer
path.

Reverse-complement retains a complete dynamically grown sequence per record
(required for reversal), but emits each result through one named 64 KiB byte
buffer. The writer's binary NUL and optional-range forms passed focused checks;
both Rune modes also remain byte-identical to the committed golden and the
50.8 MB naive-reference workload. The compiler was rebuilt and the full gate
passed `PASS=205 FAIL=0`.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | O0 / naive | O3 / naive | fastest published |
|---|---:|---:|---:|---:|---:|---:|---:|
| reverse-complement (fasta 5M) | checked | 1041.577 | 106.308 | 78.916 | 13.199x | **1.347x** | pending |

## Stage 1 incremental update: reverse-complement lookup table

On 2026-07-10, source `4589e9e+complement-table`, the 16-pair IUB complement
branch chain was replaced with a 256-byte table. It is initialized to identity
for all byte values, then overwrites all uppercase and lowercase IUB mappings;
the benchmark therefore preserves the prior behavior for both supported and
unmapped bytes. This is a benchmark-only source change: no compiler/runtime
code changed.

Both Rune modes remain byte-identical to the committed golden and the 50.8 MB
naive-reference workload. Although clang had already compressed the branch
chain substantially, the direct lookup removes enough work to improve O3 from
1.347x to 1.254x the naive reference.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | O0 / naive | O3 / naive | fastest published |
|---|---:|---:|---:|---:|---:|---:|---:|
| reverse-complement (fasta 5M) | checked | 456.207 | 96.860 | 77.226 | 5.907x | **1.254x** | pending |

## Stage 2 initial leader comparison: reverse-complement

The current official CLBG leader is C gcc #7 (Jeremy Zerfas). Its canonical
source was read from the benchmarksgame Salsa tree at `40296663ed35`, then built
locally with its published flags, `gcc -pipe -Wall -O3 -fomit-frame-pointer
-march=ivybridge -pthread` (local GCC 16.1.1). All leader, Rune, and naive-C
outputs matched the committed golden and the same pre-generated 50.8 MB input
byte-for-byte.

The leader obtains its speed from 64 KiB POSIX `read`, raw `write`, a two-byte
lookup table plus SSE4.1 16-byte transform, cache-sized chunks, and pthreads.
It sizes its thread pool with `_SC_NPROCESSORS_ONLN`, which does not honor
affinity. For a safe development-machine comparison, the entire process was
pinned and lowered to CPU 0/nice 15; every spawned leader thread inherited that
one CPU. This is a reproducible **constrained-leader** result, not a claim to
reproduce the published multicore wall time.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | constrained leader | O3 / naive | O3 / constrained leader |
|---|---:|---:|---:|---:|---:|---:|---:|
| reverse-complement (fasta 5M) | checked | 461.360 | 98.935 | 77.837 | 17.806 | 1.271x | **5.556x** |

The published table reports gcc #7 at 0.44 seconds for the larger 100,000,001
input workload, so its published timing is not directly comparable to the
50.8 MB local input. The feature gap, however, is direct: Rune needs a typed
bulk input/reuse primitive, then explicit SIMD and bounded parallelism to pursue
this leader rather than merely its naive C oracle.

## Stage 1 incremental update: reverse-complement unsafe arithmetic

The remaining sequence and output counters are all bounded far below `u64` on
the verified workload, so reverse-complement now opts into `-U`, like the other
integer-heavy ports. It removes only fixed-width arithmetic overflow checks;
array bounds checks and all I/O range validation remain. Both O0 and O3 outputs
matched the committed golden and the complete 50.8 MB naive-reference output
before timing.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | constrained leader | O3 / naive | O3 / constrained leader |
|---|---:|---:|---:|---:|---:|---:|---:|
| reverse-complement (fasta 5M) | `-U` | 221.892 | 91.697 | 77.794 | 17.167 | **1.179x** | **5.342x** |

## Stage 1 incremental update: reusable reverse-complement line storage

On 2026-07-10, `readlnInto(buffer)` was added to the bootstrap C backend. It
uses one reusable `getline` buffer and copies each newline-stripped line into a
caller-owned Rune byte array, growing that array only when necessary and
returning its possibly reallocated data pointer. This preserves `readln()`'s
safe value semantics while eliminating its allocation and `free` on every FASTA
line. The reverse-complement port reuses one such `[u8]` line buffer and writes
headers with exact byte-array output.

The focused reusable-buffer check, compiler gate (`PASS=205 FAIL=0`), committed
golden, and full 50.8 MB reference comparison all passed. This is the first
reverse-complement result faster than the naive C oracle; its row also updates
the same-session constrained-leader comparison.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | constrained leader | O3 / naive | O3 / constrained leader |
|---|---:|---:|---:|---:|---:|---:|---:|
| reverse-complement (fasta 5M) | `-U` | 199.193 | 68.623 | 77.754 | 18.086 | **0.883x** | **3.794x** |

## Stage 1 incremental update: bulk reverse-complement sequence append

On 2026-07-10, `appendBytes(destination, source)` was added to the bootstrap C
backend. It appends the exact used portion of one Rune byte array to another
with `memcpy`, retains destination capacity across appends, grows only when
needed, rejects size overflow, and is binary-safe. Reverse-complement now keeps
the sequence's logical length in its array header rather than copying every
60-byte input line one byte at a time through checked Rune assignments.

An embedded-NUL append round-trip passed, as did the compiler gate
(`PASS=205 FAIL=0`), committed golden, and full 50.8 MB reference comparison.
The same-session result is 0.571x the naive C oracle and narrows the
constrained-leader gap to 2.341x.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | constrained leader | O3 / naive | O3 / constrained leader |
|---|---:|---:|---:|---:|---:|---:|---:|
| reverse-complement (fasta 5M) | `-U` | 110.911 | 44.177 | 77.406 | 18.870 | **0.571x** | **2.341x** |

## Stage 1 incremental update: generic reverse translation writer

On 2026-07-10, `writeReverseTranslated(bytes, table, wrap, terminator)` was
added to the bootstrap C backend. It accepts an arbitrary exact 256-byte
translation table, reverses the input, translates it, and emits buffered,
wrapped output. Its portable two-byte lookup table handles every byte mapping
correctly, including NUL and identity entries; block-level wrapping keeps the
wrap check out of the hot pair loop. `wrap == 0` emits an unwrapped translation.

The embedded-NUL/wrapping check, compiler gate (`PASS=205 FAIL=0`), committed
golden, and full 50.8 MB reference comparison all passed. The direct
same-session result reaches 0.403x naive C and is within 1.772x of the
constrained leader, still without benchmark-specific DNA logic, threads, or
ISA-specific code.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | constrained leader | O3 / naive | O3 / constrained leader |
|---|---:|---:|---:|---:|---:|---:|---:|
| reverse-complement (fasta 5M) | `-U` | 65.824 | 30.897 | 76.618 | 17.434 | **0.403x** | **1.772x** |

## Stage 3: explicit masked-IUB reverse translation

The generic 256-byte `writeReverseTranslated` API remains unchanged. A new
opt-in `writeReverseMasked5Translated(bytes, table, wrap, terminator)` instead
defines its mapping as `table[input & 31]`, requires exactly 32 table bytes,
and preserves arbitrary output bytes including NUL. Reverse-complement uses it
under the explicit CLBG IUB-alphabet precondition; uppercase and lowercase ASCII
bases share the same low five bits. The runtime selects a 16-byte SSSE3
`pshufb` kernel only when the CPU supports it and retains the scalar fallback.

The focused binary-NUL/wrapping test, compiler gate (`PASS=205 FAIL=0`),
committed golden, full 50.8 MB scalar oracle, and freshly revalidated gcc #7
output all match byte-for-byte. The table below is a same-session pinned
warmup-plus-best-of-five series on the regenerated FASTA input.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | constrained gcc #7 | O3 / naive | O3 / constrained leader |
|---|---:|---:|---:|---:|---:|---:|---:|
| reverse-complement (fasta 5M) | `-U` | 57.165 | 30.170 | 78.786 | 19.063 | **0.383x** | **1.583x** |

The pre-change current-input comparison was Rune 33.834 ms versus gcc #7
20.052 ms, so the explicit masked kernel improves Rune by 10.8%. The remaining
gap is no longer generic translation semantics; gcc #7 combines raw chunked
I/O, vector translation, and a threaded pipeline. Any next step must expose one
of those capabilities explicitly rather than weakening the arbitrary-table API.

A follow-up whole-record SSSE3 experiment hoisted dispatch, table loads, and
shuffle constants out of the per-line helper. It passed the binary test, gate,
golden, and full scalar/leader comparisons, but a direct paired O3 series
measured the committed per-line form at 30.185 ms and the hoisted form at
30.486 ms. The experiment was reverted; this call/table-loading hypothesis is
not the remaining optimized-build bottleneck.

The next measured experiment targeted the common 12-byte remainder left after
three 16-byte vectors in every 60-base line. The SSSE3 helper now translates
that remainder with one bounded `pshufb` and exact 8+4-byte stores; short or
other-sized tails retain the scalar fallback, so arbitrary wrapping and binary
table output remain unchanged. Golden and full 50.8 MB outputs match at O0/O3,
and the compiler gate is `PASS=205 FAIL=0`. In a 15-pair alternating series the
new path won 14 pairs: best times were 29.994 ms versus 30.589 ms (0.981x), and
means were 30.421 ms versus 31.088 ms. A separate standard best-of-five series
measured Rune O0 64.126 ms, Rune O3 30.054 ms, naive C 79.360 ms, and gcc #7
19.019 ms. This is a real local improvement, but input remains the dominant
gap. Increasing libc's stdin buffer with `stdbuf` to 64 KiB, 1 MiB, or 8 MiB
was also measured and rejected; all three were slower than the default.

A general read-all experiment added native byte-subsequence search and native
range compaction, then parsed only the three FASTA record boundaries in Rune.
It was fully correct but regressed to 46.077 ms versus the committed 29.669 ms:
materializing the whole input and then a second dense sequence outweighed the
removed line calls. Exact regular-file pre-sizing plus one-pass exclusion cut
the experiment to 32.615 ms versus 29.431 ms, still 1.108x slower. Reserving
the full input capacity through ordinary array resize made it worse at 35.298
ms versus 29.289 ms. The entire experiment was reverted. A future raw-input
path must translate wrapped bytes directly without constructing a dense second
copy; do not repeat read-all plus record compaction.

A CPU-checked SSE4.1 helper using `pblendvb` in place of the SSSE3 boolean
selection was also exact, but a 30-pair alternating series did not separate it
from the committed path: best times were 29.497 and 29.504 ms, with 16/30 wins
and a 0.119 ms mean paired advantage. It was reverted as noise-level. The
remaining single-thread gap is not the table-selection instruction count.

## Stage 3: explicit buffered reverse-complement reader

The next measured input change adds an explicitly owned `ByteReader` backed by
a 64 KiB `fread` buffer and native `memchr` line scanning. The legacy
`readlnInto` API and its behavior remain unchanged; reverse-complement opts into
the new reader and owns its lifetime. A temporary diagnostic covering a line
longer than the buffer (70,000 bytes), a chunk-spanning delimiter, and a final
unterminated line passed at O0 and O3. The existing test count and output remain
unchanged, and the compiler gate is `PASS=205 FAIL=0`.

The committed golden and full workload are byte-exact at O0 and O3. For the
50,833,411-byte input, all Rune and reference variants produced the same output
(SHA-256 beginning `e92b329f`). The fresh pre-change same-session baseline was
Rune O0 61.468 ms, Rune O3 28.049 ms, naive C 73.829 ms, and constrained gcc #7
17.684 ms, putting Rune at 1.586x the leader. With `ByteReader`, the pinned
warmup-plus-best-of-five result is:

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | naive O3 | constrained gcc #7 | O3 / naive | O3 / constrained leader |
|---|---:|---:|---:|---:|---:|---:|---:|
| reverse-complement (fasta 5M) | `-U` | 60.420 | 21.451 | 76.552 | 19.699 | **0.280x** | **1.089x** |

In 15 alternating O3 pairs the new reader won 15/15: best times were 21.299 ms
versus 27.987 ms (0.761x), and means were 21.887 ms versus 28.663 ms. This
removes `getline` as the dominant single-thread gap. If reverse-complement is
again the largest measured target, the remaining hypotheses are raw record
layout or an output-side probe; otherwise prioritization returns to the largest
validated global gap.

## Current analysis

### The optimization unlock

The bootstrap driver already had code to choose clang `-O3`, but no command-line
branch could set its `optimized` variable. Earlier benchmark reports therefore
timed generated C at O0 while describing it as optimized. Wiring `-O` removes
that artifact: n-body is 0.996x the naive C reference, spectral-norm is 1.006x,
fannkuch-redux is 1.026x, and regex-redux is 0.994x. The former claims of a
general 4-9x code-generation band and missing cross-function inlining do not
survive a real O3 build.

Binary-trees and the historical k-nucleotide port beat their naive references
at 0.516x and 0.571x, respectively. These are comparisons with the committed
straightforward oracles, not claims of beating the published CLBG leaders.
Binary-trees benefits from Rune's reusable object pool. The historical
k-nucleotide shortcut used indexed base-4 counters; it is superseded below by
the current specification-compliant all-hash-table workload.

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
complement's hot helper calls: it improves from 1.996x to 1.824x. Replacing its
per-byte input loop with the length-safe bootstrap line reader further improves
it to 1.755x. Its exact-length 64 KiB output buffer then reaches 1.347x. This
is still not a fixed input cap: each record buffer grows by doubling and the
timing output remains byte-identical.

The direct 256-byte complement table then reaches 1.254x, confirming that the
remaining transform cost was still material despite clang's branch-chain
lowering. The byte-array C ABI now honors actual array lengths and embedded
NULs; source-level inference for a standalone `readBytes` result remains an
independent type-checker gap. Reusable line storage removes the dominant
per-line allocation churn, and `appendBytes` removes the scalar sequence-copy
loop. The generic pair translation writer then reaches 0.403x the naive C oracle
and 1.772x the published leader source. The remaining leader features are now
explicitly SSSE3/SSE4.1 16-byte translation and parallel chunks. A prior
Rune-level byte-chunk parser was correct but slower, so pursue ISA dispatch only
behind a measured, correct generic fallback.

Recompiling the same generated C with `-march=native` did not help (32.046 ms
versus 31.622 ms). A direct SSSE3 experiment reached 27.372 ms, but its masked
five-bit lookup is not equivalent to an arbitrary 256-byte table. Checking every
input byte before taking that path restored generic semantics but measured
37.330 ms, slower than the portable pair implementation. That transparent
dispatch was rejected. An explicit masked-table primitive would be a separate
language-surface decision, not an invisible substitute for arbitrary byte
translation.
The historical pidigits row demonstrates why a true bignum facility was needed;
the subsequent GMP-backed `BigInt` work below replaces that fixed-width path.

## Stage 2/3: pidigits GMP BigInt and published-leader parity

On 2026-07-10, a fresh pinned check confirmed that the prior `i8192` port was
303.622x slower than its GMP oracle at 265 digits (390.583 ms versus 1.286 ms),
and it overflowed at 266 digits. This was structural: every fixed-width result
allocated 128 limbs, multiplication was full 128-by-128 schoolbook arithmetic,
and division scanned all 8192 bits. `-U` cannot remove those wide-runtime costs.

The bootstrap backend now provides an opt-in opaque `BigInt` runtime backed by
GMP. It deliberately does **not** alter `iN`/`uN`: their fixed-width overflow,
cast, and bitwise semantics remain intact. `bigIntNew` creates a value and the
explicit destination-taking `bigIntSet`, `bigIntAdd`, `bigIntSub`, `bigIntMul`,
`bigIntMulU64`, `bigIntAddU64`, `bigIntSubU64`, and `bigIntDivTrunc` operations
reuse GMP destination capacity. `bigIntLess`, `bigIntToU64`, and
`bigIntToString` provide the small conversion surface needed by ordinary Rune
programs. Programs that do not call a `bigInt*` builtin do not link GMP.

Pidigits now uses the current official C gcc #2 algorithm with this generic
runtime surface, rather than its former fixed-width Gibbons implementation. The
official source was obtained from the pinned
[Benchmark Game Salsa page](https://salsa.debian.org/benchmarksgame-team/benchmarksgame/-/raw/40296663ed350d5fe4a6ab5e367bab61cb77c219/public/program/pidigits-gcc-2.html)
and built locally with its published `gcc -pipe -Wall -O3
-fomit-frame-pointer -march=ivybridge -lgmp` flags. Rune O0/O3, the committed
GMP oracle, and that exact leader source all matched the 27-digit golden and
the full 265- and 10,000-digit outputs byte-for-byte. The compiler gate also
passed `PASS=205 FAIL=0`.

The harness workload is now the CLBG-standard 10,000 digits. Times below are
milliseconds on pinned CPU 0, one warmup discarded, best of five, at nice 15
and idle I/O priority. The first three values are from the final full harness;
the constrained leader is a separate same-method local build because the
harness intentionally builds only committed oracle sources.

| Benchmark (workload) | Rune flags | Rune O0 | Rune O3 | committed GMP oracle | published C gcc #2 | O3 / oracle | O3 / leader |
|---|---:|---:|---:|---:|---:|---:|---:|
| pidigits (10000) | checked | 352.979 | 351.701 | 769.219 | 350.583 | **0.457x** | **1.003x** |

The remaining 0.3% is beneath the powersave/minimum-sample noise visible
between individual same-method series; it is not a defensible claim that Rune
beats the leader. It does prove the emitted C and reusable GMP API add no
material overhead on this single-threaded benchmark. This closes pidigits as a
language/runtime blocker and moves its direct leader gap from unbounded
fixed-width failure to practical parity.

## Stage 2: n-body leader gap and SIMD boundary

The current official C gcc #9 n-body leader is single-threaded, so unlike
threaded entries it can be compared directly under CPU-0 affinity. Its source
was taken from the pinned
[Salsa page](https://salsa.debian.org/benchmarksgame-team/benchmarksgame/-/raw/40296663ed350d5fe4a6ab5e367bab61cb77c219/public/program/nbody-gcc-9.html)
and built with its published `gcc -pipe -Wall -O3 -fomit-frame-pointer
-march=ivybridge` flags. It, Rune O0/O3, and the committed naive C reference
all matched the golden and the current 5,000,000-step timing output exactly.

| Benchmark (workload) | Rune O0 | Rune O3 | naive C O3 | published C gcc #9 | O3 / naive | O3 / leader |
|---|---:|---:|---:|---:|---:|---:|
| n-body (5M) | 640.322 | 169.476 | 170.426 | 100.649 | **0.994x** | **1.684x** |

Rune’s emitted hot loop is the same scalar `sqrtsd`/`divsd` shape as the naive
oracle. The leader instead batches padded body pairs in AVX lanes and uses a
float reciprocal-square-root estimate followed by Goldschmidt refinement; it
remains accurate at the required nine decimal output places. Two smaller paths
were measured and rejected before feature work: `-U` preserved exact output but
was 170.447 ms versus 170.246 ms checked O3, and manually recompiling Rune’s
identical generated C with `-march=ivybridge` preserved output but regressed to
189.548 ms versus 169.447 ms portable O3. The concrete remaining feature is
therefore explicit, opt-in SIMD vectors plus an explicitly named approximate
reciprocal-square-root primitive and target dispatch. It must not silently
change the semantics of scalar `sqrt`.

## Rejected n-body opaque-handle vector port

The first AVX experiment was intentionally kept out of the committed benchmark
after it regressed. It exactly matched the golden and both scalar-C/gcc #9
outputs at N=1000 and 5,000,000, but represented every four-lane value as a
heap pointer. Even with AVX-targeted helper bodies, generated code reloaded and
stored those handles on every operation rather than keeping a vector in a
register. The pinned CPU-0 best-of-five series was 6129.444 ms O0 and 321.913
ms O3, compared with 174.029 ms naive C and 103.462 ms gcc #9. The temporary
source port was reverted. Future SIMD work needs a local register-resident
value representation; it must not disguise this memory-traffic regression as
a benchmark optimization.

## Stage 3: register-resident n-body SIMD port

The replacement `F64x4` representation is a portable 32-byte value struct.
Its explicitly AVX-targeted operations are always inlined, and the typechecker
confines values to function locals rather than allowing them in user-function
ABIs or stored aggregates. The n-body port keeps all positions, velocities,
pair deltas, three padded reciprocal-square-root batches, both energy
evaluations, and the complete advance loop in one guarded monomorphic
`runAvx(n: u64)` function. Machines without AVX retain the prior scalar path.
The follow-up `f64x4SquaredLengths4` primitive computes four squared lengths
with the gcc #9 leader's exact hadd/permute/blend/add order, and has a portable
pairwise fallback. The advance loop now feeds its ten pair deltas through
three of these batched reductions before reciprocal-square-root refinement.

Before timing, Rune O0/O3, the rebuilt naive C oracle, and the pinned published
gcc #9 binary were byte-identical at both the committed N=1000 golden and the
full N=5,000,000 workload. The runtime's pairwise lane reduction and
Goldschmidt refinement were also corrected to match the leader's exact
floating-point parenthesization. Optimized assembly contains inline
`vrsqrtps`, `vaddpd`, `vsubpd`, and `vmulpd` with no F64x4 helper calls or heap
allocation. The batched-reduction version likewise has no helper call and
reduces total spill/reload annotations from 118 to 115. The committed N=1000
and full N=5,000,000 results remain exact; the latter output's SHA-256 begins
`a209`.

| n-body (5M) | Rune O0 | Rune O3 | naive C O3 | published C gcc #9 | O3 / naive | O3 / leader |
|---|---:|---:|---:|---:|---:|---:|
| batched F64x4 reductions | 12712.866 | 109.111 | 169.940 | 100.349 | **0.642x** | **1.087x** |

Each series was pinned to CPU 0 at nice 15/idle I/O priority, discarded one
warmup, and used the best of five. O0 is intentionally poor because value
struct copies are only scalar-replaced by optimization; the benchmark result
is the checked O3 row. In 20 alternating-order O3 pairs the batched kernel won
20/20: old/new best times were 115.185/109.120 ms (0.947x), and means were
118.411/110.080 ms (about 7.0% faster). Compared with the validated
169.476/100.649 ms scalar session, the direct leader gap has fallen from
1.684x to 1.087x.

Two exact experiments were not retained. A phased-source rewrite produced a
byte-identical binary and assembly, so it could not change runtime. Compiling
n-body with `-U` won only 10/15 alternating pairs: checked/unsafe means were
123.052/122.508 ms and best times were 122.373/121.118 ms. That marginal,
noisy result is not part of the normal benchmark harness.

## Stage 2: Mandelbrot official-output alignment and leader gap

The former Rune benchmark and naive C oracle emitted two newlines after the PBM
dimensions because Rune used `println` around a format string that already
ended in `\n`. The official CLBG N=200 output has one newline and is 5,011
bytes, not the former 5,012. Rune now uses `print`, the oracle matches it, and
the committed golden is the current official download (SHA-256
`97610473750700638fc63d13cfa49d339b67c18e7f26b3f9c9acb61e746472d5`).

The current wall-time leader is
[C++ g++ #4](https://benchmarksgame-team.pages.debian.net/benchmarksgame/program/mandelbrot-gpp-4.html),
built from pinned Salsa commit `40296663ed350d5fe4a6ab5e367bab61cb77c219`
with its published `-O3 -fomit-frame-pointer -march=ivybridge -std=c++17
-mno-fma` flags and `-pthread`. It uses eight-pixel AVX vectors, five-iteration
escape-check batches, previous-byte pruning, interlaced threads, and one bulk
bitmap write. Since its `hardware_concurrency()` row rounding changes the N=200
height on this machine, a source-identical comparator with only the thread
count fixed to one supplied the golden check. At N=4000, both that comparator
and the unmodified published binary preserve dimensions and are byte-identical
to Rune O0/O3 and the rebuilt naive C oracle.

| Mandelbrot (4000) | Rune flags | Rune O0 | Rune O3 | naive C O3 | g++ #4 one thread | g++ #4 default, CPU0 | O3 / naive | O3 / one-thread leader |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| official PBM output | `-U` | 1400.716 | 236.665 | 303.393 | 79.820 | 81.114 | **0.780x** | **2.965x** |

All timing series were pinned to CPU 0 at nice 15/idle I/O priority, discarded
one warmup, and used the best of five. The default leader's threads cannot run
in parallel under that affinity and are slightly slower than its one-thread
form, so the immediate 2.965x gap is SIMD/kernel structure rather than thread
allocation. This supersedes reverse-complement as the largest validated
single-core leader gap. The concrete Rune roadmap is an explicit packed-byte
SIMD kernel (building on register-resident vector values), five-iteration
escape batching, and buffered bitmap output; bounded tasks come afterward.

As the first isolated step, Rune now stores the exact bitmap in one byte array
and issues one `writeBytes` call instead of two million locked `writeByte`
calls at N=4000. O0/O3 remain exact at N=200 and N=4000. In a ten-pair
alternating O3 comparison the bulk form improved best time from 237.044 to
234.904 ms (0.991x) and mean time from 238.521 to 235.776 ms. A separate
standard series measured Rune O0 1463.007 ms, Rune O3 245.288 ms, naive C
305.612 ms, and g++ #4 one-thread 81.879 ms; powersave state shifted the whole
session, so the paired series is the attribution evidence. The remaining gap
is overwhelmingly the scalar kernel.

The next step adds one general SIMD primitive,
`f64x4LessEqualMask(F64x4,F64x4) -> u8`, with ordered AVX comparison and a
portable lane-bit fallback. Its compiler/runtime foundation passed
`PASS=205 FAIL=0`. Mandelbrot then renders each byte as two reversed-lane
F64x4 streams inside one guarded monomorphic function, preserves the leader's
separate multiply/subtract/add order, batches checks every five iterations,
uses the previous byte to avoid unnecessary checks, and retains the exact
scalar fallback. Optimized assembly has inline AVX comparisons/arithmetic, no
F64x4 helper calls or FMA, no repeated vector spills, and one scalar spill.

Rune O0/O3 are byte-identical to the official N=200 golden and N=4000 oracle;
Rune O3 is additionally byte-identical to naive C and both g++ #4 variants at
the official N=16000 workload. At N=4000, the SIMD O3 path measures 93.357 ms
versus 303.235 ms naive C and 79.833 ms g++ #4 one-thread: **0.308x naive and
1.169x the leader**. The prior bulk scalar path was 233.441 ms in the paired
session, so SIMD improves Rune by 2.50x. O0 is 25018.278 ms because clang O0
cannot scalar-replace the by-value vector structs; it is recorded for the
contract, not as a production configuration.

| official Mandelbrot (16000) | Rune O3 | naive C O3 | g++ #4 one thread | g++ #4 default, CPU0 | O3 / naive | O3 / one-thread leader |
|---|---:|---:|---:|---:|---:|---:|
| exact full workload | 1439.085 | 4724.742 | 1193.206 | 1197.778 | **0.305x** | **1.206x** |

The official-size series also discarded one warmup and used the best of five
serialized CPU0 runs. The remaining 17–21% gap is now a bounded kernel/codegen
question rather than missing vectorization: compare coordinate precompute,
loop unrolling, previous-byte control flow, and the leader's native union
layout before adding threads. Reverse-complement's roughly 1.5x gap again
becomes the largest validated direct target. K-nucleotide's required hash-table
migration is measured immediately below.

The coordinate-precompute follow-up removes almost all of that bounded gap.
Rune now computes every horizontal coordinate once, preserving the exact
`scale * x - 1.5` operation order, and stores coordinates in reversed groups of
four. The hot loop can therefore load each F64x4 directly without lane-building
shuffles or range checks. Optimized assembly confirms that the hot path has no
coordinate shuffles or bounds failures. Rune remains byte-identical to the
official N=200 golden, the N=4000 oracle at O0, and all N=16000 references at
O3; the full-output SHA-256 is unchanged and begins `609262`.

The fresh exact N=16000 pre-change baseline was Rune 1438.576 ms, naive C
4727.020 ms, and the constrained one-thread leader 1190.631 ms, putting Rune
at 1.208x the leader before prepacking.

Ten alternating-order O3 pairs favored prepacking 10/10. Old/new best times
were 1438.625/1212.404 ms (0.843x), and means were 1441.237/1216.831 ms, a
roughly 15.7% improvement. Two exact benchmark-only scheduling experiments
were rejected and fully reverted: splitting the multiply streams lost 0/10
pairs (best 1436.348/1464.641 ms, means 1438.624/1467.071 ms), while
short-circuiting the vector comparison also lost 0/10 (best
1437.604/1463.373 ms, means 1441.255/1466.256 ms).

| official Mandelbrot (16000) | Rune O3 | naive C O3 | g++ #4 one thread | O3 / naive | O3 / one-thread leader |
|---|---:|---:|---:|---:|---:|
| prepacked coordinates | 1213.308 | 4728.927 | 1195.156 | **0.257x** | **1.015x** |

The standard O0 N=4000 run measured 24910.286 ms, consistent with the known
unoptimized by-value vector cost. At official size, Rune is now at constrained
single-thread leader parity, with a measured 1.5% gap; this is not a leader
claim.

## Stage 2: k-nucleotide hash-table compliance

The current CLBG specification requires a built-in or library hash table for
all seven k values and explicitly forbids optimizing away that work. The former
Rune port used dense direct counters for k=1/2 and direct substring scans for
the five requested sequences, so its fast row was not eligible for a leader
claim even though its output was correct.

The compliant port encodes bases once as two-bit digits and builds a full Rune
`Dict(u64,u64)` for each of k=1,2,3,4,6,12,18 using rolling packed keys. It
updates the relation-generated entry value in place after one lookup, sorts
only the small k=1/2 outputs deterministically by count then numeric key, and
queries the five large tables only after completing their full workload. The
official N=1000 golden and the full local `fasta 1000000` output are
byte-identical at O0/O3 to the naive C oracle and current published leader.

The current leader is
[C++ g++ #2](https://benchmarksgame-team.pages.debian.net/benchmarksgame/program/knucleotide-gpp-2.html),
from pinned Salsa commit `40296663ed350d5fe4a6ab5e367bab61cb77c219`, built
with its published `-O3 -fomit-frame-pointer -march=ivybridge -std=c++17` and
`-lpthread` flags. It uses four threads, two-bit rolling keys, and GNU PBDS
chained hash tables. The locally extracted source SHA-256 is
`4c4b112d384d589eaf10c38fb6a879289d646a8412fdec4cfebcdac009794657`.

| k-nucleotide (fasta 1M) | Rune O0 | Rune O3 | naive C O3 | constrained g++ #2 | O3 / naive | O3 / leader |
|---|---:|---:|---:|---:|---:|---:|
| all seven k values through Dict | 701.556 | 173.220 | 180.937 | 148.233 | **0.957x** | **1.169x** |

Every series was serialized on CPU 0 at nice 15/idle I/O priority, with one
warmup discarded and best of five. The leader's four threads therefore share
one CPU. The former noncompliant shortcut measured 95.868 ms in the same
session, so honoring the mandated workload costs Rune 1.81x, but the compliant
port still beats the naive oracle and lands within 16.9% of the optimized
leader. The official 25M scaling run remains to be recorded before any
published-size claim.

The official `fasta 25000000` input was then generated once outside timing
(254,166,745 bytes, SHA-256
`3fcf4f78104c8a65ef210fe1d469f4e473456c791225f2f1f9114f4986aa09fa`).
Rune O0/O3, naive C, and g++ #2 again produced byte-identical output. Pinned
warmup-plus-best-of-five times were Rune O0 16850.124 ms, Rune O3 3759.007 ms,
naive C 4360.669 ms, and constrained g++ #2 2038.751 ms: Rune remains faster
than the oracle at **0.862x**, but scales to **1.844x the leader**. This is now
the largest valid direct gap.

Gprof assigns 92.35% of the compliant Rune run to the seven `countKmers`
calls. Generated C showed an apparent duplicate miss lookup (`findEntry` then
safe `insert`), but two measured library API experiments disproved it as the
dominant cause. A generic `findOrInsert` wrapper regressed best time by 2.2%
because it did not inline and added a call on every update. A miss-only
constructor method was exact but noise-level at 3755.006 versus 3758.370 ms.
Both were reverted. Do not repeat mutable-entry API churn without an inlining
mechanism.

The measured follow-up adds a reusable, value-storing open-addressed `U64Map`
runtime collection. It begins at the small library default and grows normally,
so it preserves the specification's required hash-table workload rather than
reserving a benchmark-specific final capacity. The k-nucleotide port uses
identity hashing for k <= 6, where packed keys distribute cleanly, and the
runtime's SplitMix64 avalanche mixer for k=12/18. This hybrid is load-bearing:
identity hashing averaged 16.0076 probes at k=12 and 19.6821 at k=18, while an
initial rotate/multiply mixer was catastrophically clustered and its run was
aborted. The SplitMix hybrid keeps the k<=6 cases near one probe while avoiding
the long-key clustering.

At the official 25M size, Rune O0/O3, the naive C oracle, and constrained g++
#2 again produced byte-identical output. Serialized CPU-0 warmup-plus-best-of-
five times were Rune O0 10084.689 ms, Rune O3 2630.359 ms, naive C 4369.015 ms,
and g++ #2 2045.277 ms. The open-addressed map improves O0 by 40.1% and O3 by
30.0% over the compliant `Dict` port; Rune is now **0.602x the naive oracle**
and **1.286x the leader**. Reducing redundant opaque-handle checks accounted
for the final measured improvement from 2852.219 to 2630.359 ms, with the full
compiler gate still at `PASS=205 FAIL=0`.

The measured layout follow-up keeps control bytes in their own compact array,
but combines each key and value into one contiguous slot array. The official
golden and full 25M outputs remain byte-identical at O0/O3 to both references,
and the compiler gate remains `PASS=205 FAIL=0`.

Ten alternating-order O3 pairs validate a modest improvement: the combined
slot layout won 8/10 pairs, old/new best times were 2563.275/2549.648 ms
(0.995x), and old/new means were 2627.394/2577.278 ms (about 1.9% faster by
the mean). A fresh standard warmup-plus-best-of-five series measured Rune O0
9926.711 ms, Rune O3 2575.632 ms, naive C 4358.133 ms, and constrained g++ #2
2041.814 ms. Rune is therefore **0.591x the naive oracle** and **1.261x the
leader**. This is a real but small cache-layout win, not a leader result.

The next measured hash-kernel change replaces the two-multiply SplitMix64
path with capacity-aware Fibonacci multiplicative hashing. It selects the high
product bits using a shift derived from the table capacity, requiring one
multiply and one count-leading-zeros operation per hash. Identity hashing for
k <= 6 is unchanged; using low-bit masking for the longer packed keys remains
pathologically clustered. The official golden and full 25M outputs remain
byte-identical at O0/O3 to both references, and the compiler gate remains
`PASS=205 FAIL=0`.

In ten alternating-order O3 pairs, Fibonacci hashing won 10/10. Old/new best
times were 2548.041/2371.032 ms (0.931x), and means were
2572.147/2398.348 ms (about 6.8% faster). A fresh standard
warmup-plus-best-of-five series measured Rune O0 9683.787 ms, Rune O3
2377.900 ms, naive C 4344.362 ms, and constrained g++ #2 2036.207 ms. Rune is
therefore **0.547x the naive oracle** and **1.168x the leader**. This is a
validated hash-kernel improvement, not a leader result.

The next hash-kernel checkpoint caches the capacity-derived Fibonacci shift in
each map and updates it only when the table grows, removing a count-leading-
zeros operation from every mixed-map lookup. The official golden and full 25M
outputs remain byte-identical at O0/O3 to both references, and the compiler
gate remains `PASS=205 FAIL=0`.

In eight alternating-order checked O3 pairs, cached shifting won 8/8. Old/new
best times were 2402.738/2379.994 ms (0.991x), and means were
2411.267/2388.651 ms (about 0.94% faster). A fresh standard
warmup-plus-best-of-five series measured Rune O0 9710.216 ms, Rune O3
2354.425 ms, naive C 4405.715 ms, and constrained g++ #2 2039.866 ms. Rune is
therefore **0.534x the naive oracle** and **1.154x the leader**. This modest
checkpoint is exact and repeatable, but is not a leader result.

An exact eight-pair probe found that compiling k-nucleotide with `-U` won 8/8
against checked mode: checked/unsafe best times were 2372.789/2326.186 ms
(0.980x), and means were 2377.817/2333.890 ms (about 1.85% faster). This is a
sound unsafe-mode use because the rolling indices and accumulated counts are
bounded by the generated input workload. The normal benchmark harness now
applies `-U` to both k-nucleotide builds; `bash -n benchmarks/bench.sh` passes.

The committed golden and official 25M outputs remain byte-identical at O0/O3
to the naive oracle and constrained leader. A fresh unsafe-mode standard
warmup-plus-best-of-five series measured Rune O0 7771.462 ms, Rune O3
2318.900 ms, naive C 4346.744 ms, and constrained g++ #2 2044.810 ms. Rune is
therefore **0.533x the naive oracle** and **1.134x the leader**.

The latest hash-update checkpoint adds an explicit
`u64MapIncrementUnchecked` operation for callers that have already established
the map lifetime and that incrementing the stored count cannot overflow. The
existing checked `u64MapAdd` API and its validation remain unchanged.
K-nucleotide opts into this narrow operation under `-U`: all seven maps stay
live for every update, and each finite input position contributes at most one
increment to a table, so no count can approach `u64` overflow on the verified
workloads.

The committed golden and official 25M outputs are byte-identical at O0/O3 to
the naive oracle and constrained leader, and the compiler gate is
`PASS=205 FAIL=0`. In ten alternating-order O3 pairs, the unchecked increment
won 10/10. Cached-unsafe/unchecked best times were 2321.117/2220.807 ms
(0.957x), and means were 2354.525/2239.153 ms (about 4.9% faster). A fresh
standard warmup-plus-best-of-five series measured Rune O0 7194.054 ms, Rune O3
2209.640 ms, naive C 4353.842 ms, and constrained g++ #2 2039.065 ms. Rune is
therefore **0.508x the naive oracle** and **1.084x the leader**.

The largest current validated gap is now reverse-complement at 1.089x,
followed by n-body at 1.087x, k-nucleotide at 1.084x, and Mandelbrot at 1.015x.

## Stage 2: regex-redux current-workload alignment

The former Rune and C-oracle regex-redux sources used an obsolete eleven-IUB
substitution workload. It was therefore invalid to compare their output or
timing to current CLBG entries, which use five substitutions:
`tHa[Nt] -> <4>`, `aND|caN|Ha[DS]|WaS -> <3>`, `a[NSt]|BY -> <2>`,
`<[^>]*> -> |`, and `\|[^|][^|]*\| -> -`. The nine variant count patterns,
cleanup phase, raw-input length, and output order were already equivalent.

Rune, its updated local PCRE2 oracle, and the revised golden now agree
byte-for-byte on `fasta 1000` (final length `5262`) and the full 5M FASTA input.
The aligned baseline below is measured before JIT work, pinned to CPU 0 with
one warmup discarded and best-of-five at nice 15. It replaces the old
semantic-mismatch regex row as the relevant local comparison.

| Benchmark (workload) | Rune O0 | Rune O3 | naive PCRE2 C O3 | O3 / naive |
|---|---:|---:|---:|---:|
| regex-redux (fasta 5M, current CLBG substitutions) | 7514.058 | 7329.149 | 7286.571 | **1.006x** |

Current fastest-published Rust #7 cannot yet be built locally because its
published build depends on missing prebuilt Rayon and PCRE2 FFI artifacts. The
current C gcc #5 source is the practical constrained reference after this
alignment; it uses PCRE2 JIT plus OpenMP, so its one-CPU result must be labelled
constrained. The next generic runtime experiment is safe PCRE2 JIT enablement
with an explicit `PCRE2_ERROR_JIT_STACKLIMIT` fallback to `PCRE2_NO_JIT`; this
must apply equally to the local C oracle before comparing timings.

## Stage 2: regex-redux PCRE2 JIT and bulk input

PCRE2 JIT is now requested by the generic Rune regex runtime and by the local
C oracle after every successful pattern compile. The runtime still calls normal
`pcre2_match`/`pcre2_substitute`, so unsupported JIT patterns use PCRE2's normal
fallback. `PCRE2_ERROR_JIT_STACKLIMIT` is handled explicitly by retrying the
same operation with `PCRE2_NO_JIT`; this avoids treating a JIT-specific error as
an absent match. Rune O0/O3 and the C oracle remain byte-identical at the
revised golden and full 5M workload, and the compiler gate passed `PASS=205
FAIL=0`.

After JIT, the regex-redux port replaces its byte-at-a-time stdin loop with the
existing binary-safe `readBytes` and `appendBytes` primitives. It reads until
an empty exact-length byte array signals EOF, then makes only bulk `memcpy`
appends; no FASTA/regex work is skipped. The matched JIT C oracle retains its
own implementation, so it is a fair local code-generation/runtime comparison.

| Benchmark (fasta 5M, current CLBG substitutions) | Rune O0 | Rune O3 | matched JIT PCRE2 C | O3 / C |
|---|---:|---:|---:|---:|
| aligned baseline (no JIT, byte input) | 7514.058 | 7329.149 | 7286.571 | 1.006x |
| JIT, byte input | 1875.484 | 1713.221 | 1655.060 | 1.035x |
| JIT, bulk input | 1655.488 | 1633.824 | 1646.232 | **0.992x** |

The locally built current C gcc #5 leader source also matches both outputs.
With `OMP_NUM_THREADS=1` and CPU-0 affinity it measures 1556.601 ms, so Rune
O3 is 1.050x behind this **constrained** leader. This is not a reproduction of
its published OpenMP result; the fastest Rust #7 source remains unavailable
locally because its published Rayon/PCRE2 FFI artifacts are absent. The remaining
single-thread attribution is PCRE2 match-context/JIT-stack and replacement-path
engineering, not input or general Rune code generation.

An explicit reusable 16 KiB PCRE2 JIT stack was also tested in the matched C
path before adding persistent runtime state: it was exact but measured 1602.528
ms versus 1611.087 ms for normal JIT matching (0.995x). That sub-percent delta
is below useful session variance and does not explain the 5% constrained-leader
gap, so the context experiment was rejected. Closing that gap would require a
careful general replacement-engine redesign or explicit parallel work, not a
transparent semantic change to Rune regex calls.

## Stage 2: FASTA current-workload alignment

The old FASTA source was not the current CLBG workload: its ALU repeat contained
one extra final byte, and its double cumulative-probability search did not match
the current generator's `f32` 139,968-entry LCG lookup boundaries. Rune and the
naive scalar C oracle now match the pinned single-thread C gcc #3 source exactly:
the ALU literal is 287 bytes; each random record builds the same byte lookup
table with the leader's `r >= sum` transition; every output base advances the
same LCG once and indexes that table. Headers, record sizes, and 60-column
formatting are unchanged.

The revised FASTA golden and all derived reverse-complement, k-nucleotide, and
regex-redux golden/timing paths were regenerated from verified references. The
full serial harness passed every golden and timing-workload comparison after it
regenerated its 1M/5M FASTA inputs. The scalar C gcc #3 entry is not the tied
published #1 (which is a two-worker C++/Rust implementation), but is a
dependency-free single-thread program only about 1% behind it in the official
table, and it is exact at the local workload.

| FASTA (2.5M) | Rune O0 | Rune O3 | aligned naive C | single-thread C gcc #3 | O3 / naive | O3 / gcc #3 |
|---|---:|---:|---:|---:|---:|---:|
| current CLBG generator | 212.574 | 67.404 | 74.071 | 46.727 | **0.910x** | **1.443x** |

The remaining safe single-thread gap is output construction: gcc #3 preformats
100 complete 60-byte lines per bulk write, whereas Rune still invokes the
buffered `writeByte` helper for every emitted byte. The next source-level work
is an exact reusable byte-output block; do not alter the lookup/LCG semantics.

## Stage 3: FASTA reusable bulk output blocks

FASTA now constructs output into reusable byte arrays and writes each completed
array with the existing binary-safe `writeBytes` runtime builtin. The ALU block
contains `lcm(60, 287) = 17,220` bases and 287 newlines, so it can be reused
without changing either the 60-column boundary or the repeat phase. Random
records refill a 100-line (6,000-base / 6,100-byte) buffer; this still advances
the LCG exactly once for each base and uses the already validated lookup table.
The final partial block is written with its exact used length.

Focused comparisons at N=1, 1000, 2000 (random-block boundary), and 8610
(ALU-block boundary) were byte-identical to the aligned C reference. The full
serialized harness then passed every golden and timing-workload comparison. A
fresh 2.5M Rune output was also byte-identical to the locally built gcc #3
comparator before its timing series.

| FASTA (2.5M) | Rune O0 | Rune O3 | aligned naive C | gcc #3 | O3 / naive | O3 / gcc #3 |
|---|---:|---:|---:|---:|---:|---:|
| full harness, scalar oracle comparison | 160.423 | 47.382 | 71.818 | — | **0.660x** | — |
| immediate dedicated leader series | — | 49.845 | — | 48.240 | — | **1.033x** |

Both series discarded one warmup and used the minimum of five CPU-0,
nice-15/idle-I/O runs. The difference between the harness and the immediate
series is normal powersave-state variation; it is not evidence that Rune beats
the comparator. The reliable result is the large improvement from 67.404 ms to
roughly 47–50 ms and practical single-thread parity with the dependency-free
near-leader. N-body's later register-resident SIMD port reduces its raw leader
gap from 1.684x to 1.152x; reverse-complement's validated 1.583x gap is now the
largest direct current target.

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
