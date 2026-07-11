# Rune benchmark-optimization loop

This file is the **prompt** for a self-paced optimization loop. Run it with:

```
/loop <paste the "LOOP PROMPT" section below, verbatim>
```

Pass it with **no interval** so it runs in dynamic (self-paced) mode — each
iteration completes a whole target (which involves multi-minute agent runs and a
full 205-test rebuild) before scheduling the next. A fixed 5-minute cron is wrong
for this; cancel any active short-interval loop first (`CronList` → `CronDelete`).

---

## Context the loop needs (read once, then act)

**Goal:** drive the self-hosted Rune compiler's generated code toward — and where
possible past — the fastest solutions on the Computer Language Benchmarks Game
(CLBG). Success is measured as the **Rune/C wall-clock ratio** per benchmark,
against two references built locally: the *naive* single-threaded C already in
`benchmarks/*_ref.c`, and the *fastest published* CLBG entry for that program.

**The headline finding that reframed everything (verified and fixed
2026-07-10):** before Stage 0, the compiler never passed `-O3` to clang —
`bootstrap/rune.rn` set `optimized = false` and the parser had no `-O` case, so
every earlier benchmark number was measured at **clang `-O0`**. Stage 0 wired
`-O`/`--optimize`, repaired the correctness harness, and produced this pinned,
byte-verified best-of-five baseline:

| Benchmark | committed (−O0) | real −O3 vs naive C | status |
|---|---|---|---|
| n_body (5M) | 3.65× | **0.996×** | parity |
| spectral_norm (3000, −U) | ~5× | **1.006×** | parity |
| fannkuch_redux (11, −U) | 3.98× | **1.026×** | parity |
| mandelbrot (4000, −U) | 7.43× | **1.155×** | near |
| k_nucleotide (1M) | invalid oracle | **0.571×** | beats repaired naive oracle |
| binary_trees (18) | 1.02× | **0.516×** | beats naive oracle |
| regex_redux (5M) | 1.04× | **0.994×** | parity (PCRE2-bound) |
| **fasta (2.5M)** | 8.96× | **6.656×** | **real target** |
| **reverse_complement (5M)** | 57.8× | **41.874×** | **real target** |

So the "4–9× codegen band" was mostly an `-O0` artifact. **Two genuine targets
remain, both per-byte I/O:** `writeByte` lowers to `putchar(b)` (the *locked*
stdio call) followed by `fflush(stdout)` — fasta issues 25M+ of them; revcomp
also does per-byte input over ~50.8 MB. Bulk `writeBytes`/`readBytes` builtins
already exist in the runtime; the benchmarks just don't use them.

---

## Measurement methodology (follow exactly — this is the contract)

A speed number is only admissible if it is **correct, pinned, and reproducible.**

1. **Correctness gate (before any timing).** The Rune binary's output must be
   **byte-identical** to (a) its committed `benchmarks/NAME.stdout` golden at the
   golden's small arg, and (b) the reference it is timed against, at the *timing*
   size. Verify with `cmp -s`. A fast-but-wrong build is disqualified — remember
   the earlier revcomp bug where a fixed buffer silently truncated input and
   "beat" C by doing no work. **Never edit a benchmark to do less work.**
2. **Regression gate (after any compiler change).** From the repo root,
   `bash bootstrap/research/btest.sh` must print `PASS=205 FAIL=0`. No exceptions.
3. **CPU pinning.** The governor is `powersave` and P-cores are heterogeneous
   (5400 / 4500 / 2500 MHz). Always `taskset -c 0` (the 5400 MHz core). You cannot
   change the governor in this sandbox — reject throttled runs via min-of-N.
4. **Timing.** Discard one warmup run, then **best-of-5 min** wall-clock
   (`date +%s%N`), output to `/dev/null`. Generate any stdin input **once, ahead
   of time** (e.g. `fasta 5000000 > rc_in.txt`) and time the consumer separately —
   never fold input generation into the measured command.
5. **Two references, built locally.** Stage 0 is the provisional naive-reference
   baseline and leaves published-leader columns explicitly pending. From Stage 2
   onward, do not claim a leader comparison until its local build is validated.
   - *Naive:* the committed `NAME_ref.c` at `clang -O3` (and `-lm`), as `bench.sh`
     already does.
   - *Fastest published:* fetch the top CLBG entry (WebFetch
     `benchmarksgame-team.pages.debian.net/benchmarksgame/program/NAME-*.html`;
     salsa.debian.org is also reachable). Build it with **its** published flags
     (`-fopenmp`, `-march=native`, SIMD, GMP/PCRE2, etc.). Record what features it
     uses that Rune lacks — that list is the language-feature roadmap.
6. **Record O0 and O3 both** for the Rune build, so a codegen regression that only
   shows at one level is visible.
7. **Profiling (no perf/valgrind/hyperfine — perf_event_paranoid=2).** Attribute
   hotspots with gprof: `clang -O3 -pg` the generated C, run, `gprof`. Compare the
   Rune hot function's emitted asm (`clang -S -O3 NAME.c`) against the C
   reference's. Diff the generated C against a hand-written ideal to find the gap.
8. **Scoreboard.** Append every measured result to `benchmarks/results.md` (or a
   sibling `benchmarks/scoreboard.md`) with columns: date, git SHA, benchmark,
   arg, Rune −O0, Rune −O3, naive-C, fastest-published, Rune/naive, Rune/fastest,
   and which flags (`-U`, etc.). Append-only history so regressions are visible.
   Update `HANDOFF.md` with current target + debug state each iteration.

---

## Staged plan (the loop works down this)

- **Stage 0 — unlock (complete 2026-07-10).** Wire the `-O` flag in
  `bootstrap/rune.rn`: parse `-O`/`--optimize`, set `optimized = true`; decide
  whether benchmark builds should default to `-O3`. Rebuild the compiler
  (`cd bootstrap && make rune`), re-run the 205 gate, re-baseline every benchmark
  with the harness above, rewrite `results.md`'s numbers and analysis to the real
  `-O3` figures. The established nine-row O3 geomean is 1.662×, dominated by
  the two I/O outliers; the other seven have a 0.860× geomean.
- **Stage 1 — close the last single-thread gaps vs naive C.** Buffered stdout is
  complete: removing `writeByte`'s per-byte `fflush` moved fasta to 1.057×,
  reverse-complement to 1.996×, and mandelbrot to 0.775×. Internal generated-
  function linkage then improved reverse-complement to 1.824×; length-safe
  getline input brought it to 1.755×; an exact 64 KiB byte-array output buffer
  brings it to 1.347×; a 256-byte complement lookup table reaches 1.254×. The C
  byte-array ABI is now binary- and range-safe, but standalone `readBytes`
  result typing remains a separate bootstrap type-checker repair; an explicit
  `<[u8]>readBytes(n)` cast is now supported and tested. `-U` is valid for the
  verified bounded reverse-complement counters. `readlnInto()` reuses caller
  storage and reaches 0.883× the naive C reference. The locally validated,
  CPU-0-constrained published leader is still 3.794× faster through raw bulk
  I/O, SSE4.1, and threads. `appendBytes()` then moves sequence lines with C
  `memcpy`, reaching 0.571× naive C and leaving a 2.341× leader gap. A Rune-level
  byte-chunk parser regressed and was rejected. `writeReverseTranslated()` then
  adds a portable, arbitrary-table two-byte kernel and reaches 0.403× naive C,
  leaving a 1.772× leader gap. A direct SSSE3 experiment was 27.372 ms, but
  generic-table validation cost 37.330 ms and was rejected. Next, either scope
  an explicit masked-table API or continue building the other published leaders;
  do not silently change arbitrary-table semantics. Keep all work within a safe
  CPU allocation. Target: every benchmark ≤ ~1.15× the naive C reference.
- **Stage 2 — measure the real leaders & find the ceiling.** Build the fastest
  published entry for each program locally; that becomes the true target. For each,
  produce a gap analysis: what it does that Rune can't (threads, SIMD intrinsics,
  mmap, GMP bignum for pidigits) → concrete language/runtime feature proposals.
- **Stage 3 — win where reachable.** For benchmarks whose leader is beatable
  single-threaded or with a modest feature, push Rune to match/beat and commit.
  For the rest, land the gap analysis so the feature work is scoped. Report the
  honest scoreboard vs both references.

### Stage 2/3 update: pidigits (2026-07-10)

Pidigits was the largest validated non-reverse gap: the `i8192` port was about
304x a GMP oracle at 265 digits and invalid above 265 digits. The bootstrap
runtime now has an opt-in opaque GMP-backed `BigInt` with explicit
destination-taking operations, preserving all existing fixed-width integer
semantics. The benchmark was ported to the current C gcc #2 leader algorithm,
then validated byte-for-byte at 27, 265, and the standard 10,000 digits against
both the committed GMP oracle and a locally built, pinned exact leader source.
At 10,000 digits Rune O3 is 351.701 ms versus 350.583 ms for that leader
(1.003x) and 769.219 ms for the committed GMP oracle (0.457x). See
`results.md` for flags, source provenance, and the full contract.

### Stage 2 update: regex-redux semantic alignment (2026-07-10)

The old Rune/C oracle performed obsolete eleven-IUB substitutions, whereas
current CLBG uses five magic substitutions. Both implementations and the golden
were migrated before measurement; Rune O0/O3 and the C oracle match at the
small and full FASTA inputs. The current-workload baseline is Rune O3 7329.149
ms versus 7286.571 ms naive PCRE2 C (1.006x). Do not use historical regex rows
to compare current leaders. The next generic runtime change is PCRE2 JIT with a
JIT-stack-limit fallback; only then build a constrained C gcc #5 leader.

### Stage 2 update: regex-redux JIT and bulk input (2026-07-10)

Generic PCRE2 JIT now has an interpreter retry on `PCRE2_ERROR_JIT_STACKLIMIT`,
and the matched C oracle uses the same JIT request. Exact bulk `readBytes` plus
`appendBytes` replaces the port's per-byte input loop. All revised golden/full
outputs and `PASS=205 FAIL=0` passed. At the 5M workload Rune O3 falls from
7329.149 ms aligned baseline to 1633.824 ms, beating the matched JIT C oracle
(1646.232 ms, 0.992x). The constrained one-CPU C gcc #5 leader is 1556.601 ms
(Rune 1.050x); explore PCRE2 match context/JIT-stack or manual replace paths
before treating OpenMP/Rayon as the next boundary.

The explicit 16 KiB PCRE2 JIT-stack probe was exact but only 0.995x normal JIT
matching (1602.528 versus 1611.087 ms), so it was rejected as noise-level. The
remaining boundary is a general replacement-engine redesign or explicit
parallelism, not a transparent stack toggle.

### Stage 2 update: n-body (2026-07-10)

The locally built current C gcc #9 leader is single-threaded and exact at the
current 5M workload. Rune O3 is 169.476 ms versus its 100.649 ms (1.684x),
while already 0.994x the scalar naive C oracle. Exact-output `-U` and manual
`-march=ivybridge` experiments were rejected (noise-level and 11.9% regression,
respectively). The bounded roadmap item is opt-in SIMD vector support paired
with an explicit approximate reciprocal-square-root primitive; scalar `sqrt`
must keep its exact existing behavior.

### Stage 2 update: FASTA semantic alignment (2026-07-11)

The old FASTA ALU repeat and double cumulative selection were not current CLBG
semantics. Rune and its scalar C oracle now use the 287-byte ALU and exact
`f32` 139,968-entry LCG lookup construction from pinned C gcc #3; dependent
goldens/inputs were regenerated and the full harness passed. At 2.5M, Rune O3
is 67.404 ms versus aligned naive C 74.071 ms, but the exact single-thread
gcc #3 comparator is 46.727 ms (1.443x). Next target: reusable preformatted
byte-output blocks, with lookup/LCG semantics fixed.

### Stage 3 update: FASTA bulk output (2026-07-11)

Reusable, exact byte-output blocks now replace per-byte FASTA writes: a
17,220-base LCM repeat block and a refilled 100-line random block. Focused
boundary checks, the full serialized harness, and a fresh 2.5M direct
comparison to C gcc #3 all passed byte-identically. The harness measures Rune
O3 47.382 ms versus aligned naive C 71.818 ms (0.660x); an immediate matching
leader series is Rune 49.845 ms versus gcc #3 48.240 ms (1.033x). Treat this as
near parity, not a leader win, because powersave-state variation spans those
two O3 samples. The next feature-sized target is n-body's explicit SIMD plus
explicit approximate-rsqrt path.

### N-body foundation update: opaque F64x4 surface (2026-07-11)

The first compiler/runtime step is a deliberately scalar-safe `F64x4` opaque
handle with destination-taking load/store, arithmetic, horizontal sum, and an
explicit `f64x4ApproxReciprocalSqrt` surface. It has a focused smoke test and
the full `PASS=205 FAIL=0` compiler gate. It initially made no performance
claim; the follow-on runtime update adds CPU-checked AVX helper bodies and the
explicit float-rsqrt/Goldschmidt path while retaining a scalar fallback.
Generated smoke-test assembly contains `vrsqrtps`, `vaddpd`, and `vmulpd`.
A direct F64x4 builtin now marks only its enclosing generated function with an
AVX target attribute; a guarded smoke function confirms the helpers inline into
that function while generated `main` remains baseline. N-body is still
unchanged and unscored: the remaining work is local-only escape restrictions
and the leader's padded four-pair layout. Scalar `sqrt` remains unchanged.

### Rejected n-body opaque-handle port (2026-07-11)

An exact, fully unrolled source port of gcc #9's padded 12-pair layout was
implemented and verified byte-identical at N=1000 and N=5,000,000 against both
the scalar oracle and gcc #9. Its generated AVX helper functions contained the
expected instructions, but the design was a performance regression: Rune O0
was 6129.444 ms and O3 321.913 ms, versus 174.029 ms naive C and 103.462 ms
gcc #9 (best-of-five, CPU 0, warmup discarded). The prior scalar Rune path is
about 169.476 ms. Every F64x4 value is an opaque heap pointer, so each apparent
vector operation reloads/stores through memory rather than retaining vector
state in registers. The uncommitted port and its temporary squared-length API
were reverted. Do not repeat this source shape; the next SIMD design must make
F64x4 a register-resident local value within AVX-targeted functions.

---

## LOOP PROMPT (this is what you paste into `/loop`)

> You are the orchestrator of the Rune benchmark-optimization loop. Read
> `benchmarks/OPTIMIZATION_LOOP.md` and `memory/benchmark-optimization-baseline.md`
> for full context, methodology, and the staged plan. **Manage, don't do:**
> delegate research, porting, profiling, and codegen changes to subagents (haiku
> for mechanical fan-out like fetching/building reference solutions and running
> the timing harness; sonnet for benchmark porting, profiling, and routine
> compiler edits; opus for codegen/runtime design, gap analysis, and synthesis).
> Never spawn a fable-tier subagent. Run independent agents in parallel; track the
> roster; verify results; never paste raw agent output — relay conclusions.
>
> Each iteration: (1) pick the single highest-ROI open target from the scoreboard
> in `benchmarks/results.md` (Stage 0 first — wiring `-O3` — if not yet done).
> (2) Drive the change via agents. (3) Enforce the gates: `bash
> bootstrap/research/btest.sh` = `PASS=205 FAIL=0`, and the Rune output
> byte-identical (`cmp -s`) to both its golden and the reference at the timing
> size. (4) Measure with the exact harness in OPTIMIZATION_LOOP.md — `taskset -c
> 0`, warmup discarded, best-of-5 min, /dev/null, input pre-generated, against both
> the naive C ref and the fastest published CLBG entry built locally. (5) On green
> with a real improvement, commit (Conventional Commit; end the message with
> `Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`) and
> append the result to the scoreboard. On red or a regression, revert and record
> why. Never edit a benchmark to do less work; never commit the untracked dotfiles
> or `HANDOFF.md`. (6) Update `HANDOFF.md` with the current target and debug state
> so the next iteration (or a fresh looper) can resume. If a target is blocked on a
> missing language feature, write the gap analysis and move to the next target.
> Then schedule the next iteration (self-paced — a full iteration takes many
> minutes; do not use a short fixed interval).
