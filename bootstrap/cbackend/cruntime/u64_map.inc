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
#define RN_U64MAP_MIX_GAMMA UINT64_C(0x9e3779b97f4a7c15)
#define RN_U64MAP_MIX_MULTIPLIER_1 UINT64_C(0xbf58476d1ce4e5b9)
#define RN_U64MAP_MIX_MULTIPLIER_2 UINT64_C(0x94d049bb133111eb)
#define RN_U64MAP_MIX_SHIFT_1 30u
#define RN_U64MAP_MIX_SHIFT_2 27u
#define RN_U64MAP_MIX_SHIFT_3 31u

#if defined(__GNUC__) || defined(__clang__)
#define RN_U64MAP_COLD __attribute__((cold, noinline))
#define RN_U64MAP_HOT inline __attribute__((always_inline))
#else
#define RN_U64MAP_COLD
#define RN_U64MAP_HOT inline
#endif

typedef struct rn_u64map_slot {
  uint64_t key;
  uint64_t value;
} rn_u64map_slot;

typedef struct rn_u64map {
  uint64_t magic;
  uint8_t *control;
  rn_u64map_slot *slots;
  size_t capacity;
  size_t count;
  uint8_t identity_hash;
} rn_u64map;

typedef rn_u64map *rn_u64map_t;

static RN_U64MAP_HOT void rn_u64map_require(const rn_u64map *map) {
  // The representation is opaque to Rune code, so a live handle's internal
  // arrays and power-of-two capacity can only be established by this runtime.
  if (map == NULL || map->magic != RN_U64MAP_MAGIC) {
    raise("U64MapInvalid");
  }
}

static RN_U64MAP_HOT uint64_t rn_u64map_hash(const rn_u64map *map, uint64_t key) {
  if (map->identity_hash) {
    return key;
  }
  uint64_t value = key + RN_U64MAP_MIX_GAMMA;
  value = (value ^ (value >> RN_U64MAP_MIX_SHIFT_1)) *
      RN_U64MAP_MIX_MULTIPLIER_1;
  value = (value ^ (value >> RN_U64MAP_MIX_SHIFT_2)) *
      RN_U64MAP_MIX_MULTIPLIER_2;
  return value ^ (value >> RN_U64MAP_MIX_SHIFT_3);
}

static RN_U64MAP_COLD void rn_u64map_allocate_arrays(
    size_t capacity, uint8_t **control_out, rn_u64map_slot **slots_out) {
  if (capacity > SIZE_MAX / sizeof(rn_u64map_slot)) {
    raise("U64MapCapacityOverflow");
  }
  uint8_t *control = (uint8_t *)calloc(capacity, sizeof(uint8_t));
  rn_u64map_slot *slots =
      (rn_u64map_slot *)malloc(capacity * sizeof(rn_u64map_slot));
  if (control == NULL || slots == NULL) {
    free(control);
    free(slots);
    raise("OutOfMemory");
  }
  *control_out = control;
  *slots_out = slots;
}

static RN_U64MAP_HOT size_t rn_u64map_find_slot(const rn_u64map *map,
                                                uint64_t key, uint8_t *found) {
  size_t mask = map->capacity - 1;
  size_t slot = (size_t)rn_u64map_hash(map, key) & mask;
  while (map->control[slot] == RN_U64MAP_OCCUPIED) {
    if (map->slots[slot].key == key) {
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
  rn_u64map_slot *new_slots;
  rn_u64map_allocate_arrays(new_capacity, &new_control, &new_slots);

  uint8_t *old_control = map->control;
  rn_u64map_slot *old_slots = map->slots;
  size_t old_capacity = map->capacity;
  map->control = new_control;
  map->slots = new_slots;
  map->capacity = new_capacity;

  for (size_t old_slot = 0; old_slot < old_capacity; ++old_slot) {
    if (old_control[old_slot] == RN_U64MAP_OCCUPIED) {
      uint8_t found;
      size_t new_slot =
          rn_u64map_find_slot(map, old_slots[old_slot].key, &found);
      map->control[new_slot] = RN_U64MAP_OCCUPIED;
      map->slots[new_slot] = old_slots[old_slot];
    }
  }
  free(old_control);
  free(old_slots);
}

static inline rn_u64map_t rn_u64map_new(uint8_t identity_hash) {
  uint8_t *control;
  rn_u64map_slot *slots;
  rn_u64map_allocate_arrays(RN_U64MAP_INITIAL_CAPACITY, &control, &slots);
  rn_u64map *map = (rn_u64map *)malloc(sizeof(rn_u64map));
  if (map == NULL) {
    free(control);
    free(slots);
    raise("OutOfMemory");
  }
  memset(map, 0, sizeof(*map));
  map->control = control;
  map->slots = slots;
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
    if (UINT64_MAX - map->slots[slot].value < delta) {
      raise("Overflow");
    }
    map->slots[slot].value += delta;
    return map->slots[slot].value;
  }

  /* capacity - capacity/4 is exactly 75% for power-of-two capacities. */
  size_t load_limit = map->capacity -
      map->capacity / RN_U64MAP_LOAD_DENOMINATOR;
  if (map->count >= load_limit) {
    rn_u64map_grow(map);
    slot = rn_u64map_find_slot(map, key, &found);
  }
  map->control[slot] = RN_U64MAP_OCCUPIED;
  map->slots[slot].key = key;
  map->slots[slot].value = delta;
  ++map->count;
  return delta;
}

static RN_U64MAP_HOT uint64_t rn_u64map_get_or_zero(const rn_u64map_t map,
                                                    uint64_t key) {
  rn_u64map_require(map);
  uint8_t found;
  size_t slot = rn_u64map_find_slot(map, key, &found);
  return found ? map->slots[slot].value : UINT64_C(0);
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
      result[result_index++] = map->slots[slot].key;
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
  free(map->slots);
  map->control = NULL;
  map->slots = NULL;
  map->capacity = 0;
  map->count = 0;
  free(map);
}

#undef RN_U64MAP_COLD
#undef RN_U64MAP_HOT
#undef RN_U64MAP_MIX_GAMMA
#undef RN_U64MAP_MIX_MULTIPLIER_1
#undef RN_U64MAP_MIX_MULTIPLIER_2
#undef RN_U64MAP_MIX_SHIFT_1
#undef RN_U64MAP_MIX_SHIFT_2
#undef RN_U64MAP_MIX_SHIFT_3
