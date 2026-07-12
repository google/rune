/* Stack-nestable, thread-local regions for short-lived Rune class objects.
 *
 * A region owns a linked list of max_align_t-aligned bump blocks. Leaving a
 * region releases every block in O(number of blocks), retaining at most one
 * bounded block in a per-thread cache. The cache is deliberately small and
 * explicit: regions are a lifetime facility, not an unbounded hidden pool.
 *
 * This fragment assumes raise(const char *) has already been emitted.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RN_REGION_MIN_BLOCK_BYTES ((size_t)1024u)
#define RN_REGION_MAX_CACHED_BLOCK_BYTES ((size_t)(256u * 1024u))
#define RN_REGION_OBJECT_ID UINT32_MAX

typedef struct rn_region_block {
  struct rn_region_block *next;
  size_t capacity;
  size_t used;
} rn_region_block;

typedef struct rn_region_scope {
  struct rn_region_scope *previous;
  rn_region_block *blocks;
  size_t initial_capacity;
} rn_region_scope;

static _Thread_local rn_region_scope *rn_region_current = NULL;
static _Thread_local rn_region_block *rn_region_cached = NULL;

_Static_assert(RN_REGION_MIN_BLOCK_BYTES <=
                   RN_REGION_MAX_CACHED_BLOCK_BYTES,
               "Rune region cache bounds are inverted");
_Static_assert((_Alignof(max_align_t) & (_Alignof(max_align_t) - 1u)) == 0u,
               "max_align_t alignment must be a power of two");

static inline void rn_region_size_overflow(void) {
  raise("RegionSizeOverflow");
}

static inline size_t rn_region_checked_add(size_t left, size_t right) {
  if (left > SIZE_MAX - right) {
    rn_region_size_overflow();
  }
  return left + right;
}

static inline size_t rn_region_align_up_to(size_t value, size_t alignment) {
  const size_t mask = alignment - 1u;
  return rn_region_checked_add(value, mask) & ~mask;
}

static inline size_t rn_region_align_up(size_t value) {
  return rn_region_align_up_to(value, _Alignof(max_align_t));
}

static inline size_t rn_region_block_header_size(void) {
  /* malloc returns storage suitable for max_align_t.  The bytes after this
   * rounded header are outside the declared rn_region_block object, so stores
   * through a class pointer establish their own effective type instead of
   * aliasing a typed flexible-array member. */
  return rn_region_align_up(sizeof(rn_region_block));
}

static inline unsigned char *rn_region_block_payload(rn_region_block *block) {
  return (unsigned char *)block + rn_region_block_header_size();
}

static inline size_t rn_region_normalize_capacity(size_t capacity) {
  if (capacity < RN_REGION_MIN_BLOCK_BYTES) {
    capacity = RN_REGION_MIN_BLOCK_BYTES;
  }
  return rn_region_align_up(capacity);
}

static inline rn_region_block *rn_region_new_block(size_t capacity) {
  capacity = rn_region_normalize_capacity(capacity);
  const size_t allocation_size = rn_region_checked_add(
      rn_region_block_header_size(), capacity);
  rn_region_block *block = (rn_region_block *)malloc(allocation_size);
  if (block == NULL) {
    raise("OutOfMemory");
  }
  block->next = NULL;
  block->capacity = capacity;
  block->used = 0u;
  return block;
}

static inline int rn_region_is_active(void) {
  return rn_region_current != NULL;
}

static inline void rn_region_enter(rn_region_scope *scope,
                                   uint64_t initial_bytes) {
  if (scope == NULL || initial_bytes > (uint64_t)SIZE_MAX) {
    rn_region_size_overflow();
  }
  scope->previous = rn_region_current;
  scope->blocks = NULL;
  scope->initial_capacity = rn_region_normalize_capacity((size_t)initial_bytes);
  rn_region_current = scope;
}

static inline size_t rn_region_next_block_capacity(
    const rn_region_scope *scope, size_t required) {
  size_t capacity = scope->initial_capacity;
  if (scope->blocks != NULL) {
    const size_t prior = scope->blocks->capacity;
    /* Doubling is only a growth policy. Near SIZE_MAX, fall back to the exact
     * required capacity so a representable request is not rejected merely
     * because twice the previous capacity is unrepresentable. */
    capacity = prior <= SIZE_MAX / 2u ? prior * 2u : required;
  }
  if (capacity < required) {
    capacity = required;
  }
  return rn_region_normalize_capacity(capacity);
}

static inline rn_region_block *rn_region_add_block(rn_region_scope *scope,
                                                    size_t required) {
  const size_t capacity = rn_region_next_block_capacity(scope, required);
  rn_region_block *block = rn_region_cached;
  if (block != NULL && block->capacity >= capacity) {
    /* Remove the cache entry before linking it into an active scope. An
     * active block and the cache must never alias, including under nesting. */
    rn_region_cached = NULL;
    assert(block->next == NULL);
    block->used = 0u;
  } else {
    block = rn_region_new_block(capacity);
  }
  block->next = scope->blocks;
  scope->blocks = block;
  return block;
}

static inline void *rn_region_alloc_zeroed(size_t requested_size,
                                           size_t requested_alignment) {
  if (rn_region_current == NULL) {
    raise("RegionNotActive");
  }
  if (requested_alignment == 0u ||
      (requested_alignment & (requested_alignment - 1u)) != 0u ||
      requested_alignment > _Alignof(max_align_t)) {
    raise("RegionInvalidAlignment");
  }

  /* C permits zero-byte allocation requests, but callers still need a stable,
   * aligned pointer that cannot alias the next object. */
  const size_t allocation_size = requested_size == 0u ? 1u : requested_size;
  rn_region_scope *scope = rn_region_current;
  rn_region_block *block = scope->blocks;
  size_t offset = 0u;

  if (block != NULL) {
    offset = rn_region_align_up_to(block->used, requested_alignment);
    if (offset > block->capacity ||
        allocation_size > block->capacity - offset) {
      block = NULL;
    }
  }

  if (block == NULL) {
    block = rn_region_add_block(scope, allocation_size);
    offset = 0u;
  }

  block->used = rn_region_checked_add(offset, allocation_size);
  void *result = (void *)(rn_region_block_payload(block) + offset);
  memset(result, 0, allocation_size);
  return result;
}

static inline void rn_region_consider_for_cache(rn_region_block *block) {
  assert(block != NULL);
  assert(block != rn_region_cached);
  block->next = NULL;
  block->used = 0u;

  if (block->capacity > RN_REGION_MAX_CACHED_BLOCK_BYTES) {
    free(block);
    return;
  }

  if (rn_region_cached == NULL) {
    rn_region_cached = block;
    return;
  }
  if (block->capacity > rn_region_cached->capacity) {
    free(rn_region_cached);
    rn_region_cached = block;
    return;
  }
  free(block);
}

static inline void rn_region_leave(rn_region_scope *scope) {
  if (scope == NULL || rn_region_current != scope) {
    raise("RegionScopeMismatch");
  }

  /* Restore the enclosing scope before reclaiming this one. The leaving
   * scope is unreachable from TLS throughout cleanup, so nested state cannot
   * accidentally observe a half-reclaimed active list. */
  rn_region_current = scope->previous;
  scope->previous = NULL;
  rn_region_block *block = scope->blocks;
  scope->blocks = NULL;
  scope->initial_capacity = 0u;

  while (block != NULL) {
    rn_region_block *next = block->next;
    rn_region_consider_for_cache(block);
    block = next;
  }
}

/* Ephemeral worker threads call this before returning, and generated main()
 * drains the main-thread cache before process exit. Between region calls each
 * live thread retains at most one bounded 256 KiB payload. */
static inline void rn_region_thread_cleanup(void) {
  if (rn_region_current != NULL) {
    raise("RegionCleanupWhileActive");
  }
  free(rn_region_cached);
  rn_region_cached = NULL;
}
