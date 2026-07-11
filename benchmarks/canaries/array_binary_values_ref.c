/* Correctness-only oracle for array_binary_values.rn. */
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BOUNDARY_COUNT = 9 };
static const uint64_t DEFAULT_SCALE = UINT64_C(29);
static const uint64_t MAX_SCALE = UINT64_C(1000);
static const uint64_t MIX_MULTIPLIER = UINT64_C(0x9e3779b185ebca87);
static const uint64_t INITIAL_CHECKSUM = UINT64_C(0xcbf29ce484222325);
static const size_t BOUNDARY_LENGTHS[BOUNDARY_COUNT] = {
    15, 16, 17, 31, 32, 33, 63, 64, 65
};

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
} ByteArray;

static void *checked_malloc(size_t size) {
    void *result = malloc(size == 0 ? 1 : size);
    if (result == NULL) {
        fputs("out of memory\n", stderr);
        exit(2);
    }
    return result;
}

static ByteArray array_new(size_t length) {
    ByteArray result = {checked_malloc(length), length, length};
    return result;
}

static ByteArray array_copy(const ByteArray *source) {
    ByteArray result = array_new(source->length);
    memcpy(result.data, source->data, source->length);
    return result;
}

static ByteArray array_slice(const ByteArray *source, size_t start,
                             size_t finish) {
    ByteArray result = array_new(finish - start);
    memcpy(result.data, source->data + start, result.length);
    return result;
}

static void array_resize(ByteArray *array, size_t length) {
    if (length > array->capacity) {
        uint8_t *replacement = realloc(array->data, length);
        if (replacement == NULL) {
            fputs("out of memory\n", stderr);
            exit(2);
        }
        array->data = replacement;
        array->capacity = length;
    }
    array->length = length;
}

static void array_append(ByteArray *array, uint8_t value) {
    if (array->length == array->capacity) {
        size_t capacity = array->capacity == 0 ? 1 : array->capacity * 2;
        uint8_t *replacement = realloc(array->data, capacity);
        if (replacement == NULL) {
            fputs("out of memory\n", stderr);
            exit(2);
        }
        array->data = replacement;
        array->capacity = capacity;
    }
    array->data[array->length++] = value;
}

static void array_free(ByteArray *array) {
    free(array->data);
}

static ByteArray make_bytes(size_t length, uint64_t round) {
    ByteArray bytes = array_new(length);
    for (size_t index = 0; index < length; ++index) {
        uint64_t raw = ((uint64_t)index * UINT64_C(29) +
                        (uint64_t)length * UINT64_C(7) +
                        round * UINT64_C(3)) % UINT64_C(251);
        bytes.data[index] = (uint8_t)raw;
        if (((uint64_t)index + round) % UINT64_C(11) == UINT64_C(4)) {
            bytes.data[index] = 0;
        }
    }
    return bytes;
}

static uint64_t fold_bytes(uint64_t checksum, const ByteArray *bytes,
                           uint64_t tag) {
    for (size_t index = 0; index < bytes->length; ++index) {
        checksum *= MIX_MULTIPLIER;
        checksum ^= (uint64_t)bytes->data[index] + tag + (uint64_t)index;
    }
    return checksum;
}

static uint64_t count_zeros(const ByteArray *bytes) {
    uint64_t count = 0;
    for (size_t index = 0; index < bytes->length; ++index) {
        if (bytes->data[index] == 0) {
            ++count;
        }
    }
    return count;
}

static uint64_t parse_scale(int argc, char **argv) {
    if (argc <= 1) {
        return DEFAULT_SCALE;
    }
    char *end = NULL;
    errno = 0;
    uintmax_t parsed = strtoumax(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' ||
        parsed < UINT64_C(1) || parsed > MAX_SCALE) {
        fputs("scale must be an integer in 1..1000\n", stderr);
        exit(2);
    }
    return (uint64_t)parsed;
}

int main(int argc, char **argv) {
    uint64_t scale = parse_scale(argc, argv);
    uint64_t checksum = INITIAL_CHECKSUM;
    uint64_t zero_count = 0;
    uint64_t independent_copies = 0;
    uint64_t total_length = 0;

    for (uint64_t round = 0; round < scale; ++round) {
        for (size_t boundary = 0; boundary < BOUNDARY_COUNT; ++boundary) {
            size_t length = BOUNDARY_LENGTHS[boundary];
            ByteArray original = make_bytes(length, round);
            ByteArray changed = array_copy(&original);
            size_t mutation_index =
                3 + (size_t)(round % (uint64_t)(length - 6));
            uint8_t source_byte = original.data[mutation_index];
            changed.data[mutation_index] ^= UINT8_C(0xa5);
            if (original.data[mutation_index] == source_byte &&
                changed.data[mutation_index] != source_byte) {
                ++independent_copies;
            }

            size_t start = 2 + (size_t)(round % UINT64_C(3));
            size_t finish = length - (2 + (size_t)(round % UINT64_C(2)));
            ByteArray middle = array_slice(&changed, start, finish);

            array_resize(&changed, length - 3);
            array_append(&changed, 0);
            array_append(&changed, (uint8_t)(round % UINT64_C(251)));
            array_append(&changed, UINT8_C(0xff));
            array_append(&changed, (uint8_t)length);

            checksum = fold_bytes(checksum, &original,
                                  (uint64_t)length + round);
            checksum = fold_bytes(checksum, &middle,
                                  (uint64_t)start + (uint64_t)finish + round);
            checksum = fold_bytes(checksum, &changed,
                                  (uint64_t)length + scale);
            zero_count += count_zeros(&original) + count_zeros(&middle) +
                          count_zeros(&changed);
            total_length += (uint64_t)original.length +
                            (uint64_t)middle.length +
                            (uint64_t)changed.length;

            array_free(&middle);
            array_free(&changed);
            array_free(&original);
        }
    }

    printf("%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
           scale, independent_copies, zero_count, total_length, checksum);
    return 0;
}
