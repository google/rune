/* Copyright 2026 The Rune Authors.
 *
 * This is a generality canary, not a contest benchmark. Compiler and runtime
 * changes must not recognize this file, its constants, its key prefix, or its
 * operation sequence. Optimize generic dictionary, heap, string, and iterator
 * machinery instead; unrelated workloads must receive the same benefit.
 *
 * The reference intentionally uses an ordinary chained hash table and binary
 * heap. It is meant to state equivalent work clearly, not win a C benchmark.
 */

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { UPDATE_PERIOD = 3 };
static const uint64_t DEFAULT_ITEMS = UINT64_C(30000);
static const uint64_t MIN_ITEMS = UINT64_C(1);
static const uint64_t MAX_ITEMS = UINT64_C(100000);
static const uint64_t VALUE_SALT = UINT64_C(0x6a09e667f3bcc909);

typedef struct Entry {
  char *key;
  uint64_t value;
  struct Entry *next;
} Entry;

typedef struct {
  Entry **buckets;
  size_t bucket_count;
  uint64_t count;
} Dict;

typedef struct {
  char **values;
  size_t length;
  size_t capacity;
} Heap;

static void *checked_malloc(size_t size) {
  void *result = malloc(size);
  if (result == NULL) {
    fputs("out of memory\n", stderr);
    exit(2);
  }
  return result;
}

static void *checked_calloc(size_t count, size_t size) {
  void *result = calloc(count, size);
  if (result == NULL) {
    fputs("out of memory\n", stderr);
    exit(2);
  }
  return result;
}

static void *checked_realloc(void *old, size_t size) {
  void *result = realloc(old, size);
  if (result == NULL) {
    fputs("out of memory\n", stderr);
    exit(2);
  }
  return result;
}

static char *copy_string(const char *source) {
  size_t length = strlen(source) + 1;
  char *result = checked_malloc(length);
  memcpy(result, source, length);
  return result;
}

static uint64_t rotate_left_23(uint64_t value) {
  return (value << 23) | (value >> 41);
}

/* Matches Rune's generic hashValues and hashString definitions. */
static uint64_t hash_values(uint64_t first, uint64_t second) {
  uint64_t mixed = first ^ second ^ UINT64_C(0xa5a5a5a5a5a5a5a5);
  return rotate_left_23(mixed * UINT64_C(0xdeadbeef31415927));
}

static uint64_t hash_string(const char *value) {
  uint64_t hash = 0;
  const unsigned char *cursor = (const unsigned char *)value;
  while (*cursor != '\0') {
    hash = hash_values(hash, *cursor++);
  }
  return hash;
}

static char *key_for(uint64_t index) {
  int length = snprintf(NULL, 0, "key-%" PRIu64, index);
  char *key = checked_malloc((size_t)length + 1);
  snprintf(key, (size_t)length + 1, "key-%" PRIu64, index);
  return key;
}

static void dict_init(Dict *dict, uint64_t expected_items) {
  size_t bucket_count = 16;
  while (bucket_count < expected_items &&
         bucket_count <= SIZE_MAX / 2) {
    bucket_count *= 2;
  }
  dict->buckets = checked_calloc(bucket_count, sizeof(*dict->buckets));
  dict->bucket_count = bucket_count;
  dict->count = 0;
}

static Entry *dict_entry(const Dict *dict, const char *key) {
  size_t bucket = (size_t)(hash_string(key) & (dict->bucket_count - 1));
  for (Entry *entry = dict->buckets[bucket]; entry != NULL;
       entry = entry->next) {
    if (strcmp(entry->key, key) == 0) {
      return entry;
    }
  }
  return NULL;
}

static int dict_contains(const Dict *dict, const char *key) {
  return dict_entry(dict, key) != NULL;
}

static uint64_t dict_find(const Dict *dict, const char *key) {
  Entry *entry = dict_entry(dict, key);
  if (entry == NULL) {
    fputs("dictionary key not found\n", stderr);
    exit(2);
  }
  return entry->value;
}

static void dict_insert(Dict *dict, const char *key, uint64_t value) {
  if (dict_entry(dict, key) != NULL) {
    fputs("duplicate dictionary key\n", stderr);
    exit(2);
  }
  size_t bucket = (size_t)(hash_string(key) & (dict->bucket_count - 1));
  Entry *entry = checked_malloc(sizeof(*entry));
  entry->key = copy_string(key);
  entry->value = value;
  entry->next = dict->buckets[bucket];
  dict->buckets[bucket] = entry;
  dict->count++;
}

static void dict_remove(Dict *dict, const char *key) {
  size_t bucket = (size_t)(hash_string(key) & (dict->bucket_count - 1));
  Entry **link = &dict->buckets[bucket];
  while (*link != NULL && strcmp((*link)->key, key) != 0) {
    link = &(*link)->next;
  }
  if (*link == NULL) {
    fputs("dictionary key not found\n", stderr);
    exit(2);
  }
  Entry *removed = *link;
  *link = removed->next;
  free(removed->key);
  free(removed);
  dict->count--;
}

static void dict_destroy(Dict *dict) {
  for (size_t bucket = 0; bucket < dict->bucket_count; bucket++) {
    Entry *entry = dict->buckets[bucket];
    while (entry != NULL) {
      Entry *next = entry->next;
      free(entry->key);
      free(entry);
      entry = next;
    }
  }
  free(dict->buckets);
}

static void heap_init(Heap *heap) {
  heap->values = NULL;
  heap->length = 0;
  heap->capacity = 0;
}

static void heap_push(Heap *heap, const char *value) {
  if (heap->length == heap->capacity) {
    heap->capacity = heap->capacity == 0 ? 16 : heap->capacity * 2;
    heap->values = checked_realloc(
        heap->values, heap->capacity * sizeof(*heap->values));
  }
  size_t child = heap->length++;
  heap->values[child] = copy_string(value);
  while (child != 0) {
    size_t parent = (child - 1) / 2;
    if (strcmp(heap->values[parent], heap->values[child]) <= 0) {
      break;
    }
    char *temporary = heap->values[parent];
    heap->values[parent] = heap->values[child];
    heap->values[child] = temporary;
    child = parent;
  }
}

static char *heap_pop(Heap *heap) {
  if (heap->length == 0) {
    fputs("heap underflow\n", stderr);
    exit(2);
  }
  char *result = heap->values[0];
  heap->length--;
  if (heap->length == 0) {
    return result;
  }
  heap->values[0] = heap->values[heap->length];
  size_t parent = 0;
  for (;;) {
    size_t left = parent * 2 + 1;
    if (left >= heap->length) {
      break;
    }
    size_t right = left + 1;
    size_t child = left;
    if (right < heap->length &&
        strcmp(heap->values[right], heap->values[left]) < 0) {
      child = right;
    }
    if (strcmp(heap->values[parent], heap->values[child]) <= 0) {
      break;
    }
    char *temporary = heap->values[parent];
    heap->values[parent] = heap->values[child];
    heap->values[child] = temporary;
    parent = child;
  }
  return result;
}

static int parse_items(int argc, char **argv, uint64_t *items) {
  if (argc > 2) {
    return 0;
  }
  if (argc <= 1) {
    *items = DEFAULT_ITEMS;
    return 1;
  }
  if (argv[1][0] == '\0') {
    return 0;
  }
  for (const unsigned char *cursor = (const unsigned char *)argv[1];
       *cursor != '\0'; cursor++) {
    if (*cursor < '0' || *cursor > '9') {
      return 0;
    }
  }
  errno = 0;
  char *end = NULL;
  uint64_t result = strtoull(argv[1], &end, 10);
  if (errno == ERANGE || end == argv[1] || *end != '\0' ||
      result < MIN_ITEMS || result > MAX_ITEMS) {
    return 0;
  }
  *items = result;
  return 1;
}

int main(int argc, char **argv) {
  uint64_t items;
  if (!parse_items(argc, argv, &items)) {
    return 2;
  }
  Dict dict;
  Heap heap;
  dict_init(&dict, items);
  heap_init(&heap);

  for (uint64_t i = 0; i < items; i++) {
    char *key = key_for(i);
    uint64_t value = hash_values(i, i ^ VALUE_SALT);
    dict_insert(&dict, key, value);
    heap_push(&heap, key);
    free(key);
  }

  uint64_t probe_checksum = 0;
  for (uint64_t i = 0; i < items; i++) {
    char *key = key_for(i);
    if (dict_contains(&dict, key)) {
      uint64_t value = dict_find(&dict, key);
      if (i % UPDATE_PERIOD == 0) {
        dict_remove(&dict, key);
        dict_insert(&dict, key, hash_values(value, i));
      }
      probe_checksum = hash_values(
          probe_checksum, hash_values(hash_string(key), dict_find(&dict, key)));
    }
    free(key);
  }

  uint64_t key_checksum = 0;
  uint64_t key_count = 0;
  for (size_t bucket = 0; bucket < dict.bucket_count; bucket++) {
    for (Entry *entry = dict.buckets[bucket]; entry != NULL;
         entry = entry->next) {
      key_checksum += hash_string(entry->key);
      key_count++;
    }
  }

  uint64_t value_checksum = 0;
  for (size_t bucket = 0; bucket < dict.bucket_count; bucket++) {
    for (Entry *entry = dict.buckets[bucket]; entry != NULL;
         entry = entry->next) {
      value_checksum += entry->value;
    }
  }

  uint64_t item_checksum = 0;
  for (size_t bucket = 0; bucket < dict.bucket_count; bucket++) {
    for (Entry *entry = dict.buckets[bucket]; entry != NULL;
         entry = entry->next) {
      item_checksum += hash_values(hash_string(entry->key), entry->value);
    }
  }

  uint64_t heap_checksum = 0;
  while (heap.length != 0) {
    char *key = heap_pop(&heap);
    heap_checksum = hash_values(heap_checksum, hash_string(key));
    free(key);
  }

  uint64_t checksum = hash_values(probe_checksum, key_checksum);
  checksum = hash_values(checksum, value_checksum);
  checksum = hash_values(checksum, item_checksum);
  checksum = hash_values(checksum, heap_checksum);
  checksum = hash_values(checksum, key_count);
  printf("%" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
         items, key_count, checksum);

  free(heap.values);
  dict_destroy(&dict);
  return 0;
}
