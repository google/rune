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
- **Stage 1 — close the last single-thread gaps vs naive C.** fasta (6.656×) and
  reverse_complement (41.874×): route them through bulk I/O (use the existing
  `writeBytes`/`readBytes`, or make `writeByte`/`readByte` use a manual buffer /
  the `_unlocked` stdio variants). Nudge mandelbrot (1.155×) if cheap. Target:
  every benchmark ≤ ~1.15× the naive C reference.
- **Stage 2 — measure the real leaders & find the ceiling.** Build the fastest
  published entry for each program locally; that becomes the true target. For each,
  produce a gap analysis: what it does that Rune can't (threads, SIMD intrinsics,
  mmap, GMP bignum for pidigits) → concrete language/runtime feature proposals.
- **Stage 3 — win where reachable.** For benchmarks whose leader is beatable
  single-threaded or with a modest feature, push Rune to match/beat and commit.
  For the rest, land the gap analysis so the feature work is scoped. Report the
  honest scoreboard vs both references.

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
