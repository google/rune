# Stage-1 structured `parallelMap`

The bootstrap compiler provides an intentionally narrow first concurrency
primitive:

```rune
func addOffset(item: u64, offset: u64) -> u64 {
  return item + offset
}

items = [1u64, 2u64, 3u64]
results = parallelMap(
    items, 10u64, &addOffset(items[0], 10u64), 4u64)
// results is [11u64, 12u64, 13u64]
```

`parallelMap(items, context, callback, maxWorkers)` is synchronous and eager.
It returns only after every item has been processed, allocates the complete
result array up front, and preserves input order regardless of worker
completion order. Each item is processed exactly once.

The callback must be a direct named function with signature
`(Item, Context) -> Result`. The expressions inside `&callback(sampleItem,
sampleContext)` select a concrete callback specialization; they are never
evaluated. Consequently, `items[0]` is safe as a sample even when `items` is
empty.

## Stage-1 safety boundary

`Item`, `Context`, and `Result` must currently be by-value scalars: fixed-width
integers, floating-point values, booleans, or enums. Callback bodies and every
transitively called helper are also restricted to scalar computation. The
compiler rejects arrays, strings, tuples, structs, classes, opaque runtime
handles, SIMD values, and function values, including worker-local
intermediates. Inference `ChoiceType` constraints are also rejected at the
task boundary because they have no runtime C representation; ordinary generic
calls specialize them to a concrete scalar before reaching this check.

Callbacks may not access module globals, perform input or output, use random
state, explicitly raise or catch exceptions, invoke another `parallelMap`, or
call a function whose effects cannot be checked transitively. These
restrictions are compile-time errors. A user-defined function that happens to
be named `parallelMap` is an ordinary function and is not subject to this
builtin contract.

Checked arithmetic can still fail implicitly inside a worker. Exception frame
state is thread-local, so a worker can never jump through a handler on another
thread. The calling thread temporarily masks any enclosing `try` while it
participates and until every child is joined, so it cannot jump past live
worker state either. Because worker `try` blocks are forbidden, an uncaught
implicit error aborts the process, matching an uncaught error on the main
thread.

## Worker count and implementation

`maxWorkers` is explicit. Values `0` and `1` select the deterministic serial
path. Larger values are clamped to the item count and to the named runtime cap
`RN_PARALLEL_MAP_MAX_WORKERS`, currently 4. A partial thread-creation failure
falls back to the threads that were successfully created plus the calling
thread; all started threads are joined before return.

Stage 1 creates and joins a bounded set of pthreads for each call. It does not
yet provide a persistent pool, frozen shared inputs, uniquely owned/moved
buffer results, streaming backpressure, or an ordered fold. In particular,
this API is not yet suitable for FASTA-style byte-block generation: byte-array
items, context, and results remain deliberately rejected until Rune has a
sound ownership-transfer model.
