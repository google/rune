/* Direct sanitizer coverage for the production region runtime fragment.
 * This translation unit intentionally includes region.inc rather than a test
 * copy, so alignment/cache/arithmetic regressions exercise shipped code. */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void raise(const char *message) {
  fprintf(stderr, "%s\n", message);
  abort();
}

#include "../cbackend/cruntime/region.inc"

static void require_zeroed(const void *memory, size_t size) {
  const unsigned char *bytes = (const unsigned char *)memory;
  for (size_t index = 0u; index < size; ++index) {
    assert(bytes[index] == 0u);
  }
}

static void require_aligned(const void *memory) {
  assert(((uintptr_t)memory % _Alignof(max_align_t)) == 0u);
}

static int block_is_active(const rn_region_block *needle) {
  for (const rn_region_scope *scope = rn_region_current; scope != NULL;
       scope = scope->previous) {
    for (const rn_region_block *block = scope->blocks; block != NULL;
         block = block->next) {
      if (block == needle) {
        return 1;
      }
    }
  }
  return 0;
}

static void require_cache_disjoint(void) {
  assert(rn_region_cached == NULL || !block_is_active(rn_region_cached));
  if (rn_region_cached != NULL) {
    assert(rn_region_cached->next == NULL);
    assert(rn_region_cached->used == 0u);
    assert(rn_region_cached->capacity <=
           RN_REGION_MAX_CACHED_BLOCK_BYTES);
  }
}

typedef struct {
  uint64_t words[3];
} region_test_node24;

_Static_assert(sizeof(region_test_node24) == 24u,
               "region stride canary requires a 24-byte object");

static void run_normal_matrix(void) {
  assert(!rn_region_is_active());
  assert(rn_region_cached == NULL);

  rn_region_scope outer;
  rn_region_enter(&outer, 0u);
  assert(rn_region_is_active());
  assert(rn_region_current == &outer);
  assert(outer.previous == NULL);
  assert(outer.blocks == NULL); /* entry is lazy */

  unsigned char *first = (unsigned char *)rn_region_alloc_zeroed(
      37u, _Alignof(max_align_t));
  require_aligned(first);
  require_zeroed(first, 37u);
  memset(first, 0xa5, 37u);
  rn_region_block *outer_first_block = outer.blocks;
  assert(outer_first_block != NULL);
  assert(rn_region_cached == NULL);
  require_cache_disjoint();

  max_align_t *aligned =
      (max_align_t *)rn_region_alloc_zeroed(
          sizeof(max_align_t), _Alignof(max_align_t));
  require_aligned(aligned);
  require_zeroed(aligned, sizeof(*aligned));
  memset(aligned, 0x5a, sizeof(*aligned));

  rn_region_scope inner;
  rn_region_enter(&inner, 2048u);
  assert(rn_region_current == &inner);
  assert(inner.previous == &outer);
  unsigned char *inner_bytes =
      (unsigned char *)rn_region_alloc_zeroed(
          96u, _Alignof(max_align_t));
  require_aligned(inner_bytes);
  require_zeroed(inner_bytes, 96u);
  memset(inner_bytes, 0x3c, 96u);
  assert(inner.blocks != outer_first_block);
  require_cache_disjoint();

  rn_region_leave(&inner);
  assert(rn_region_current == &outer);
  assert(rn_region_cached != NULL);
  assert(!block_is_active(rn_region_cached));
  require_cache_disjoint();

  /* The outer allocation forces another block and may adopt the just-freed
   * inner block. Existing outer data must remain untouched. */
  unsigned char *outer_large =
      (unsigned char *)rn_region_alloc_zeroed(
          1500u, _Alignof(max_align_t));
  require_aligned(outer_large);
  require_zeroed(outer_large, 1500u);
  for (size_t index = 0u; index < 37u; ++index) {
    assert(first[index] == 0xa5u);
  }
  assert(rn_region_cached == NULL);
  require_cache_disjoint();
  memset(outer_large, 0xc3, 1500u);

  rn_region_leave(&outer);
  assert(!rn_region_is_active());
  assert(rn_region_cached != NULL);
  require_cache_disjoint();
  rn_region_block *reusable = rn_region_cached;

  /* Cached storage contains dirty bytes, but every new allocation must be
   * zeroed before it becomes observable. */
  rn_region_scope reuse;
  rn_region_enter(&reuse, 0u);
  unsigned char *reused =
      (unsigned char *)rn_region_alloc_zeroed(
          1500u, _Alignof(max_align_t));
  require_aligned(reused);
  require_zeroed(reused, 1500u);
  assert(reuse.blocks == reusable);
  assert(rn_region_cached == NULL);
  memset(reused, 0x7e, 1500u);
  rn_region_leave(&reuse);
  require_cache_disjoint();

  /* Per-object alignment must not silently round every 8-aligned 24-byte
   * node to a 32-byte max_align_t stride. */
  rn_region_scope packed;
  rn_region_enter(&packed, 0u);
  region_test_node24 *packed_first =
      (region_test_node24 *)rn_region_alloc_zeroed(
          sizeof(region_test_node24), _Alignof(region_test_node24));
  region_test_node24 *packed_second =
      (region_test_node24 *)rn_region_alloc_zeroed(
          sizeof(region_test_node24), _Alignof(region_test_node24));
  assert((unsigned char *)packed_second - (unsigned char *)packed_first ==
         (ptrdiff_t)sizeof(region_test_node24));
  require_zeroed(packed_first, sizeof(*packed_first));
  require_zeroed(packed_second, sizeof(*packed_second));
  rn_region_leave(&packed);
  require_cache_disjoint();

  /* Force multiple blocks; leaving retains the largest eligible payload. */
  rn_region_scope growth;
  rn_region_enter(&growth, 1024u);
  (void)rn_region_alloc_zeroed(800u, _Alignof(max_align_t));
  (void)rn_region_alloc_zeroed(800u, _Alignof(max_align_t));
  (void)rn_region_alloc_zeroed(3000u, _Alignof(max_align_t));
  size_t largest_eligible = 0u;
  for (rn_region_block *block = growth.blocks; block != NULL;
       block = block->next) {
    if (block->capacity <= RN_REGION_MAX_CACHED_BLOCK_BYTES &&
        block->capacity > largest_eligible) {
      largest_eligible = block->capacity;
    }
  }
  rn_region_leave(&growth);
  assert(rn_region_cached != NULL);
  assert(rn_region_cached->capacity == largest_eligible);
  require_cache_disjoint();

  /* An oversized valid block is never cached and must not evict the bounded
   * cache retained from earlier scopes. */
  rn_region_block *bounded_cache = rn_region_cached;
  rn_region_scope oversized;
  rn_region_enter(&oversized,
                  (uint64_t)RN_REGION_MAX_CACHED_BLOCK_BYTES + 4096u);
  (void)rn_region_alloc_zeroed(1u, _Alignof(max_align_t));
  assert(oversized.blocks->capacity > RN_REGION_MAX_CACHED_BLOCK_BYTES);
  assert(rn_region_cached == bounded_cache);
  require_cache_disjoint();
  rn_region_leave(&oversized);
  assert(rn_region_cached == bounded_cache);
  require_cache_disjoint();

  rn_region_thread_cleanup();
  assert(rn_region_cached == NULL);
  assert(!rn_region_is_active());
}

int main(int argc, char **argv) {
  if (argc == 1) {
    run_normal_matrix();
    return 0;
  }

  if (strcmp(argv[1], "overflow-enter") == 0) {
    rn_region_scope scope;
    rn_region_enter(&scope, UINT64_MAX);
    return 1;
  }
  if (strcmp(argv[1], "overflow-alloc") == 0) {
    rn_region_scope scope;
    rn_region_enter(&scope, 0u);
    (void)rn_region_alloc_zeroed(SIZE_MAX, _Alignof(max_align_t));
    return 1;
  }
  if (strcmp(argv[1], "cleanup-active") == 0) {
    rn_region_scope scope;
    rn_region_enter(&scope, 0u);
    rn_region_thread_cleanup();
    return 1;
  }
  if (strcmp(argv[1], "invalid-alignment") == 0) {
    rn_region_scope scope;
    rn_region_enter(&scope, 0u);
    (void)rn_region_alloc_zeroed(8u, 3u);
    return 1;
  }
  if (strcmp(argv[1], "zero-alignment") == 0) {
    rn_region_scope scope;
    rn_region_enter(&scope, 0u);
    (void)rn_region_alloc_zeroed(8u, 0u);
    return 1;
  }
  if (strcmp(argv[1], "over-alignment") == 0) {
    rn_region_scope scope;
    rn_region_enter(&scope, 0u);
    (void)rn_region_alloc_zeroed(8u, _Alignof(max_align_t) * 2u);
    return 1;
  }

  fprintf(stderr, "unknown mode: %s\n", argv[1]);
  return 2;
}
