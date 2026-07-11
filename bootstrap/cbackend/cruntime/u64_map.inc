/* Runtime-backed open-addressed u64-to-u64 map. */

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define RN_U64MAP_MAGIC UINT64_C(0x52554e455536344d)
#define RN_U64MAP_INITIAL_CAPACITY ((size_t)32)
#define RN_U64MAP_LOAD_DENOMINATOR ((size_t)4)
#define RN_U64MAP_EMPTY ((uint8_t)0)
#define RN_U64MAP_OCCUPIED ((uint8_t)1)

#if defined(__GNUC__) || defined(__clang__)
#define RN_U64MAP_COLD __attribute__((cold, noinline))
#define RN_U64MAP_HOT inline __attribute__((always_inline))
#else
#define RN_U64MAP_COLD
#define RN_U64MAP_HOT inline
#endif

typedef struct rn_u64map {
  uint64_t magic;
  uint8_t *control;
  uint64_t *keys;
  uint64_t *values;
  size_t capacity;
  size_t count;
  uint8_t identity_hash;
} rn_u64map;

typedef rn_u64map *rn_u64map_t;

static RN_U64MAP_HOT void rn_u64map_require(const rn_u64map *map) {
  if (map == NULL || map->magic != RN_U64MAP_MAGIC ||
      map->control == NULL || map->keys == NULL || map->values == NULL ||
      map->capacity < RN_U64MAP_INITIAL_CAPACITY ||
      (map->capacity & (map->capacity - 1)) != 0) {
    raise("U64MapInvalid");
  }
}

static RN_U64MAP_HOT uint64_t rn_u64map_rotl64(uint64_t value, unsigned shift) {
  return (value << shift) | (value >> (64u - shift));
}

static RN_U64MAP_HOT uint64_t rn_u64map_hash(const rn_u64map *map, uint64_t key) {
  if (map->identity_hash) {
    return key;
  }
  return rn_u64map_rotl64(
      (key ^ UINT64_C(0xa5a5a5a5a5a5a5a5)) *
          UINT64_C(0xdeadbeef31415927),
      23u);
}

static RN_U64MAP_COLD void rn_u64map_allocate_arrays(
    size_t capacity, uint8_t **control_out, uint64_t **keys_out,
    uint64_t **values_out) {
  if (capacity > SIZE_MAX / sizeof(uint64_t)) {
    raise("U64MapCapacityOverflow");
  }
  uint8_t *control = (uint8_t *)calloc(capacity, sizeof(uint8_t));
  uint64_t *keys = (uint64_t *)malloc(capacity * sizeof(uint64_t));
  uint64_t *values = (uint64_t *)malloc(capacity * sizeof(uint64_t));
  if (control == NULL || keys == NULL || values == NULL) {
    free(control);
    free(keys);
    free(values);
    raise("OutOfMemory");
  }
  *control_out = control;
  *keys_out = keys;
  *values_out = values;
}

static RN_U64MAP_HOT size_t rn_u64map_find_slot(const rn_u64map *map,
                                                uint64_t key, uint8_t *found) {
  size_t mask = map->capacity - 1;
  size_t slot = (size_t)rn_u64map_hash(map, key) & mask;
  while (map->control[slot] == RN_U64MAP_OCCUPIED) {
    if (map->keys[slot] == key) {
      *found = 1;
      return slot;
    }
    slot = (slot + 1) & mask;
  }
  *found = 0;
  return slot;
}

static RN_U64MAP_COLD void rn_u64map_grow(rn_u64map *map) {
  if (map->capacity > SIZE_MAX / 2) {
    raise("U64MapCapacityOverflow");
  }
  size_t new_capacity = map->capacity * 2;
  uint8_t *new_control;
  uint64_t *new_keys;
  uint64_t *new_values;
  rn_u64map_allocate_arrays(new_capacity, &new_control, &new_keys,
                            &new_values);

  uint8_t *old_control = map->control;
  uint64_t *old_keys = map->keys;
  uint64_t *old_values = map->values;
  size_t old_capacity = map->capacity;
  map->control = new_control;
  map->keys = new_keys;
  map->values = new_values;
  map->capacity = new_capacity;

  for (size_t old_slot = 0; old_slot < old_capacity; ++old_slot) {
    if (old_control[old_slot] == RN_U64MAP_OCCUPIED) {
      uint8_t found;
      size_t new_slot = rn_u64map_find_slot(map, old_keys[old_slot], &found);
      map->control[new_slot] = RN_U64MAP_OCCUPIED;
      map->keys[new_slot] = old_keys[old_slot];
      map->values[new_slot] = old_values[old_slot];
    }
  }
  free(old_control);
  free(old_keys);
  free(old_values);
}

static inline rn_u64map_t rn_u64map_new(uint8_t identity_hash) {
  uint8_t *control;
  uint64_t *keys;
  uint64_t *values;
  rn_u64map_allocate_arrays(RN_U64MAP_INITIAL_CAPACITY, &control, &keys,
                            &values);
  rn_u64map *map = (rn_u64map *)malloc(sizeof(rn_u64map));
  if (map == NULL) {
    free(control);
    free(keys);
    free(values);
    raise("OutOfMemory");
  }
  memset(map, 0, sizeof(*map));
  map->control = control;
  map->keys = keys;
  map->values = values;
  map->capacity = RN_U64MAP_INITIAL_CAPACITY;
  map->identity_hash = identity_hash ? 1 : 0;
  map->magic = RN_U64MAP_MAGIC;
  return map;
}

static RN_U64MAP_HOT uint64_t rn_u64map_add(rn_u64map_t map, uint64_t key,
                                            uint64_t delta) {
  rn_u64map_require(map);
  uint8_t found;
  size_t slot = rn_u64map_find_slot(map, key, &found);
  if (found) {
    if (UINT64_MAX - map->values[slot] < delta) {
      raise("Overflow");
    }
    map->values[slot] += delta;
    return map->values[slot];
  }

  /* capacity - capacity/4 is exactly 75% for power-of-two capacities. */
  size_t load_limit = map->capacity -
      map->capacity / RN_U64MAP_LOAD_DENOMINATOR;
  if (map->count >= load_limit) {
    rn_u64map_grow(map);
    slot = rn_u64map_find_slot(map, key, &found);
  }
  map->control[slot] = RN_U64MAP_OCCUPIED;
  map->keys[slot] = key;
  map->values[slot] = delta;
  ++map->count;
  return delta;
}

static RN_U64MAP_HOT uint64_t rn_u64map_get_or_zero(const rn_u64map_t map,
                                                    uint64_t key) {
  rn_u64map_require(map);
  uint8_t found;
  size_t slot = rn_u64map_find_slot(map, key, &found);
  return found ? map->values[slot] : UINT64_C(0);
}

static inline uint64_t *rn_u64map_keys(const rn_u64map_t map) {
  rn_u64map_require(map);
  if (map->count > UINT64_MAX) {
    raise("U64MapCapacityOverflow");
  }
  uint64_t *result =
      (uint64_t *)array_alloc(sizeof(uint64_t), map->count);
  size_t result_index = 0;
  for (size_t slot = 0; slot < map->capacity; ++slot) {
    if (map->control[slot] == RN_U64MAP_OCCUPIED) {
      result[result_index++] = map->keys[slot];
    }
  }
  return result;
}

static inline void rn_u64map_free(rn_u64map_t map) {
  if (map == NULL) {
    return;
  }
  rn_u64map_require(map);
  map->magic = 0;
  free(map->control);
  free(map->keys);
  free(map->values);
  map->control = NULL;
  map->keys = NULL;
  map->values = NULL;
  map->capacity = 0;
  map->count = 0;
  free(map);
}

#undef RN_U64MAP_COLD
#undef RN_U64MAP_HOT
