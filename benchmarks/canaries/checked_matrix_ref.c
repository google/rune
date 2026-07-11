/* Correctness-only scalar oracle for checked_matrix.rn.
 * It is deliberately not a performance comparator. */
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { ROWS = 17, INNER = 23, COLS = 19 };
static const uint64_t DEFAULT_SCALE = 200;

typedef struct Matrix {
    size_t rows;
    size_t cols;
    double **values;
} Matrix;

static Matrix matrix_new(size_t rows, size_t cols) {
    Matrix matrix = { rows, cols, calloc(rows, sizeof(*matrix.values)) };
    if (!matrix.values) { perror("calloc"); exit(1); }
    for (size_t row = 0; row < rows; row++) {
        matrix.values[row] = calloc(cols, sizeof(*matrix.values[row]));
        if (!matrix.values[row]) { perror("calloc"); exit(1); }
    }
    return matrix;
}

static void matrix_free(Matrix *matrix) {
    for (size_t row = 0; row < matrix->rows; row++) free(matrix->values[row]);
    free(matrix->values);
}

static Matrix matrix_make(size_t rows, size_t cols, int64_t seed) {
    Matrix matrix = matrix_new(rows, cols);
    for (size_t row = 0; row < rows; row++) {
        for (size_t col = 0; col < cols; col++) {
            int64_t integer_value = seed + (int64_t)(row * 3 + col * 2 + 1);
            matrix.values[row][col] = (double)integer_value * 0.25;
        }
    }
    return matrix;
}

static Matrix matrix_add(const Matrix *left, const Matrix *right) {
    if (left->rows != right->rows || left->cols != right->cols) abort();
    Matrix result = matrix_new(left->rows, left->cols);
    for (size_t row = 0; row < left->rows; row++) {
        for (size_t col = 0; col < left->cols; col++) {
            result.values[row][col] =
                left->values[row][col] + right->values[row][col];
        }
    }
    return result;
}

static Matrix matrix_multiply(const Matrix *left, const Matrix *right) {
    if (left->cols != right->rows) abort();
    Matrix result = matrix_new(left->rows, right->cols);
    for (size_t row = 0; row < left->rows; row++) {
        for (size_t col = 0; col < right->cols; col++) {
            double value = 0.0;
            for (size_t inner = 0; inner < left->cols; inner++) {
                value += left->values[row][inner] * right->values[inner][col];
            }
            result.values[row][col] = value;
        }
    }
    return result;
}

static void score_values(const Matrix *result, int64_t *integer_score,
                         double *float_score) {
    int64_t integer_result = 0;
    double float_result = 0.0;
    for (size_t row = 0; row < result->rows; row++) {
        for (size_t col = 0; col < result->cols; col++) {
            double value = result->values[row][col];
            int64_t weight = (int64_t)((row + 1) * (col + 2));
            integer_result += (int64_t)(value * 16.0) * weight;
            float_result += value * (double)weight;
        }
    }
    *integer_score = integer_result;
    *float_score = float_result;
}

int main(int argc, char **argv) {
    uint64_t scale = DEFAULT_SCALE;
    if (argc > 1) {
        char *end = NULL;
        errno = 0;
        unsigned long long parsed = strtoull(argv[1], &end, 10);
        if (errno != 0 || end == argv[1] || *end != '\0' ||
            parsed < 1 || parsed > 100000) {
            fprintf(stderr, "scale must be an integer in 1..100000\n");
            return 2;
        }
        scale = (uint64_t)parsed;
    }

    Matrix left = matrix_make(ROWS, INNER, 1);
    Matrix right = matrix_make(INNER, COLS, -2);
    Matrix bias = matrix_make(ROWS, COLS, 3);
    int64_t integer_total = 0;
    double float_total = 0.0;
    for (uint64_t iteration = 0; iteration < scale; iteration++) {
        int64_t integer_score;
        double float_score;
        Matrix product = matrix_multiply(&left, &right);
        Matrix result = matrix_add(&product, &bias);
        score_values(&result, &integer_score, &float_score);
        matrix_free(&result);
        matrix_free(&product);
        integer_total += integer_score + (int64_t)(iteration % 17);
        float_total += float_score + (double)(iteration % 13) * 0.5;
    }
    printf("%" PRId64 " %" PRId64 "\n",
           integer_total, (int64_t)(float_total * 16.0));

    matrix_free(&bias);
    matrix_free(&right);
    matrix_free(&left);
    return 0;
}
