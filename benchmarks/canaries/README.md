# Rune performance regression canaries

These programs protect ordinary language/runtime paths that are easy to
regress while optimizing CLBG workloads. They are regression canaries, never
optimization targets. Do not specialize Rune, weaken checks, reshape a canary,
or tune its source to improve its score.

Each `NAME.rn` has a straightforward `NAME_ref.c` correctness-only oracle.
The C programs and their compiler flags exist solely to validate output; they
are never performance comparators. The runner builds checked Rune O0 and
host-native O3 binaries plus the C oracle, then requires byte-identical output.
If `NAME.stdout` exists, all three outputs must also match that committed
golden. Without a golden, the freshly built C output is the correctness oracle.

Run correctness only:

```sh
bash benchmarks/canaries/run.sh
```

Collect informational timings after correctness:

```sh
bash benchmarks/canaries/run.sh --measure
```

Measurement is serialized on CPU 0 at nice 15 and idle I/O priority. Rune O0
and O3 each warm once, then run in alternating order for five pairs. Only their
best and mean times are recorded; the C oracle is never timed. There is
initially no timing threshold: results are evidence for detecting and
investigating drift, not a benchmark leaderboard. Timing fails closed when the
one-minute load exceeds 1.0 or another process last scheduled on CPU 0 exceeds
50% CPU. Set `CANARY_SCALE=N`, where
N is an integer from 1 through 1000, as a quick-run override passed equally to
every canary. Defaults remain each canary's authoritative workload. Committed
default-scale goldens are skipped under an override while Rune/reference
equality remains mandatory.

## Known limitation found by checked-matrix

This canary originally placed constructor-field baking, overloaded operators,
and their tuple result behind a second unconstrained outer generic function.
Scheduling that full constructor-field-bake -> operator -> outer-generic chain
remains unsupported and is tracked as a compiler limitation. The canary keeps
the generic `Matrix` class and generic overloaded operators, while its scoring
helper is explicitly typed. This is not an optimization of the canary or a
request to specialize the compiler around its workload.
