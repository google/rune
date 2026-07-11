/* Correctness-only oracle for semantic_edges.rn. */
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static const uint64_t DEFAULT_SCALE = 64;
static const uint64_t MAX_SCALE = 100000;

static uint32_t add_value(uint32_t value, uint32_t salt) {
    return value + salt;
}

static uint32_t xor_value(uint32_t value, uint32_t salt) {
    return value ^ salt;
}

static uint32_t dispatch(uint32_t value, bool use_add) {
    uint32_t (*operation)(uint32_t, uint32_t) =
        use_add ? add_value : xor_value;
    return operation(value, 7);
}

static bool checked_u8_add(uint8_t left, uint8_t right, uint8_t *result) {
    if ((uint16_t)left + (uint16_t)right > UINT8_MAX) return false;
    *result = (uint8_t)(left + right);
    return true;
}

static bool checked_u32_div(uint32_t numerator, uint32_t denominator,
                            uint32_t *result) {
    if (denominator == 0) return false;
    *result = numerator / denominator;
    return true;
}

int main(int argc, char **argv) {
    uint64_t scale = DEFAULT_SCALE;
    if (argc > 2) return 2;
    if (argc > 1) {
        char *end = NULL;
        errno = 0;
        unsigned long long parsed = strtoull(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0' ||
            parsed < 1 || parsed > MAX_SCALE) {
            fprintf(stderr, "scale must be an integer in 1..100000\n");
            return 2;
        }
        scale = (uint64_t)parsed;
    }

    uint64_t overflow_count = 0;
    uint64_t division_count = 0;
    uint64_t inner_count = 0;
    uint64_t outer_count = 0;
    uint64_t unexpected_count = 0;
    uint64_t safe_sum = 0;
    uint64_t dispatch_sum = 0;

    for (uint64_t iteration = 0; iteration < scale; iteration++) {
        uint8_t byte_result;
        if (!checked_u8_add(UINT8_MAX, 1, &byte_result)) overflow_count++;
        else unexpected_count += byte_result;

        uint32_t quotient;
        if (!checked_u32_div((uint32_t)(iteration + 1), 0, &quotient))
            division_count++;
        else unexpected_count += quotient;

        /* These counters model the two explicit, nested Rune catches. */
        inner_count++;
        outer_count++;

        safe_sum += 200 + iteration % 50;
        dispatch_sum += dispatch((uint32_t)(iteration % 100),
                                 iteration % 2 == 0);
    }

    printf("%" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
           " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
           scale, overflow_count, division_count, inner_count, outer_count,
           unexpected_count, safe_sum, dispatch_sum);
    return 0;
}
