/* Shared runtime substrate for compiler-specialized parallelMap helpers. */

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

// Deliberately conservative: callers must request parallelism explicitly,
// and every specialized helper clamps that request to this per-call cap.
#define RN_PARALLEL_MAP_MAX_WORKERS 4u

/*
 * Stage 1 deliberately creates a bounded set of pthreads per call. A
 * persistent pool, uniquely-owned transferable buffers, frozen shared input,
 * and an ordered fold/backpressure API are separate follow-up stages.
 */
