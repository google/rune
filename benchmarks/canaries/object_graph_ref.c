/* Generality canary: ordinary heap objects and pointer-rich graph traversal.
 *
 * This is deliberately not an optimization target. It is a straightforward
 * semantic oracle for object_graph.rn, not a place for CLBG binary-trees
 * shaping, SIMD, custom allocators, or benchmark-specific shortcuts.
 * Its explicit per-node allocation and teardown provide ordinary C lifetime
 * hygiene; they do not mirror Rune runtime bookkeeping and must not be used
 * as a performance comparison against Rune.
 */

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum {
    CHAIN_LENGTH = 32,
    MUTATION_ROUNDS = 3,
    SKIP_MULTIPLIER = 17,
    SKIP_OFFSET = 13,
    NULL_SKIP_PERIOD = 97,
    ITERATIVE_PASSES = 4
};

static const uint64_t DEFAULT_NODE_COUNT = UINT64_C(250000);
static const uint64_t MAX_NODE_COUNT = UINT64_C(2000000);
static const uint64_t INITIAL_VALUE_MODULUS = UINT64_C(65521);
static const uint64_t VALUE_MODULUS = UINT64_C(1000003);
static const uint64_t CHECKSUM_MODULUS = UINT64_C(1000000007);

typedef struct GraphNode GraphNode;

struct GraphNode {
    uint64_t id;
    uint64_t value;
    GraphNode *next;
    GraphNode *skip;
};

static uint64_t recursive_checksum(const GraphNode *node,
                                   uint32_t remaining) {
    uint64_t total = node->value + node->id;
    if (remaining > UINT32_C(1) && node->next != NULL) {
        total += recursive_checksum(node->next, remaining - UINT32_C(1));
    }
    return total % CHECKSUM_MODULUS;
}

static void update_node(GraphNode *node, uint64_t round) {
    uint64_t neighbor_value = UINT64_C(0);
    if (node->next != NULL) {
        neighbor_value += node->next->value;
    }
    if (node->skip != NULL) {
        neighbor_value += node->skip->value;
    }
    node->value = (node->value * UINT64_C(5) + neighbor_value + node->id +
                   round) % VALUE_MODULUS;
}

static uint64_t parse_node_count(int argc, char **argv) {
    if (argc <= 1) {
        return DEFAULT_NODE_COUNT;
    }

    char *end = NULL;
    uintmax_t parsed = strtoumax(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || parsed < UINT64_C(1) ||
        parsed > MAX_NODE_COUNT) {
        fprintf(stderr, "object_graph node count must be in 1..2000000\n");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)parsed;
}

int main(int argc, char **argv) {
    uint64_t node_count = parse_node_count(argc, argv);
    GraphNode **nodes = calloc((size_t)node_count, sizeof(*nodes));
    if (nodes == NULL) {
        perror("calloc");
        return EXIT_FAILURE;
    }

    for (uint64_t i = 0; i < node_count; ++i) {
        nodes[i] = calloc(1, sizeof(*nodes[i]));
        if (nodes[i] == NULL) {
            perror("calloc");
            return EXIT_FAILURE;
        }
        nodes[i]->id = i;
        nodes[i]->value = (i * UINT64_C(37) + UINT64_C(11)) %
                          INITIAL_VALUE_MODULUS;
    }

    for (uint64_t i = 0; i < node_count; ++i) {
        if (i + UINT64_C(1) < node_count &&
            (i + UINT64_C(1)) % CHAIN_LENGTH != UINT64_C(0)) {
            nodes[i]->next = nodes[i + UINT64_C(1)];
        }
        if (i % NULL_SKIP_PERIOD != UINT64_C(0)) {
            uint64_t skip_index = (i * SKIP_MULTIPLIER + SKIP_OFFSET) %
                                  node_count;
            nodes[i]->skip = nodes[skip_index];
        }
    }

    for (uint64_t round = 0; round < MUTATION_ROUNDS; ++round) {
        for (uint64_t i = 0; i < node_count; ++i) {
            update_node(nodes[i], round);
        }
    }

    uint64_t checksum = UINT64_C(0);
    for (uint64_t i = 0; i < node_count; i += CHAIN_LENGTH) {
        checksum += recursive_checksum(nodes[i], CHAIN_LENGTH);
        checksum %= CHECKSUM_MODULUS;
    }

    GraphNode *current = nodes[0];
    uint64_t step_count = node_count * ITERATIVE_PASSES;
    for (uint64_t step = 0; step < step_count; ++step) {
        checksum += current->value + current->id;
        checksum %= CHECKSUM_MODULUS;
        if (current->skip != NULL) {
            current = current->skip;
        } else {
            uint64_t fallback_index = (current->id + step + UINT64_C(1)) %
                                      node_count;
            current = nodes[fallback_index];
        }
    }

    printf("%" PRIu64 " %" PRIu64 "\n", node_count, checksum);
    for (uint64_t i = 0; i < node_count; ++i) {
        free(nodes[i]);
    }
    free(nodes);
    return EXIT_SUCCESS;
}
