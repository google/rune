/*
 * Portable foundation for Rune's opaque four-lane f64 SIMD helpers.
 *
 * Handles intentionally point at scalar lane storage rather than carrying a
 * compiler vector type. Generated Rune functions therefore never pass AVX
 * registers across an ABI boundary. On x86, CPU-checked AVX helper functions
 * accelerate the operation bodies without changing the language-facing ABI.
 */

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define RN_F64X4_LANES 4u

typedef struct rn_f64x4 {
  double lane[RN_F64X4_LANES];
} rn_f64x4;

typedef rn_f64x4 *rn_f64x4_t;

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define RN_F64X4_HAS_X86_AVX 1
#define RN_F64X4_AVX_TARGET __attribute__((target("avx")))
#else
#define RN_F64X4_HAS_X86_AVX 0
#define RN_F64X4_AVX_TARGET
#endif

static inline int rn_f64x4_avx_available(void) {
#if RN_F64X4_HAS_X86_AVX
  static int available = -1;
  if (available < 0) {
    __builtin_cpu_init();
    available = __builtin_cpu_supports("avx") ? 1 : 0;
  }
  return available;
#else
  return 0;
#endif
}

#if RN_F64X4_HAS_X86_AVX
static RN_F64X4_AVX_TARGET void rn_f64x4_copy_avx(
    rn_f64x4_t destination, const rn_f64x4_t source) {
  _mm256_storeu_pd(destination->lane, _mm256_loadu_pd(source->lane));
}

static RN_F64X4_AVX_TARGET void rn_f64x4_splat_avx(
    rn_f64x4_t destination, double value) {
  _mm256_storeu_pd(destination->lane, _mm256_set1_pd(value));
}

static RN_F64X4_AVX_TARGET void rn_f64x4_load_avx(
    rn_f64x4_t destination, const double *source) {
  _mm256_storeu_pd(destination->lane, _mm256_loadu_pd(source));
}

static RN_F64X4_AVX_TARGET void rn_f64x4_store_avx(
    const rn_f64x4_t source, double *destination) {
  _mm256_storeu_pd(destination, _mm256_loadu_pd(source->lane));
}

static RN_F64X4_AVX_TARGET void rn_f64x4_add_avx(
    rn_f64x4_t destination, const rn_f64x4_t left,
    const rn_f64x4_t right) {
  _mm256_storeu_pd(destination->lane,
      _mm256_add_pd(_mm256_loadu_pd(left->lane), _mm256_loadu_pd(right->lane)));
}

static RN_F64X4_AVX_TARGET void rn_f64x4_sub_avx(
    rn_f64x4_t destination, const rn_f64x4_t left,
    const rn_f64x4_t right) {
  _mm256_storeu_pd(destination->lane,
      _mm256_sub_pd(_mm256_loadu_pd(left->lane), _mm256_loadu_pd(right->lane)));
}

static RN_F64X4_AVX_TARGET void rn_f64x4_mul_avx(
    rn_f64x4_t destination, const rn_f64x4_t left,
    const rn_f64x4_t right) {
  _mm256_storeu_pd(destination->lane,
      _mm256_mul_pd(_mm256_loadu_pd(left->lane), _mm256_loadu_pd(right->lane)));
}

static RN_F64X4_AVX_TARGET void rn_f64x4_approx_rsqrt_avx(
    rn_f64x4_t destination, const rn_f64x4_t source) {
  const __m256d squared = _mm256_loadu_pd(source->lane);
  __m256d estimate = _mm256_cvtps_pd(_mm_rsqrt_ps(_mm256_cvtpd_ps(squared)));
  const __m256d y = _mm256_mul_pd(squared, _mm256_mul_pd(estimate, estimate));
  const __m256d correction = _mm256_add_pd(
      _mm256_sub_pd(_mm256_mul_pd(_mm256_set1_pd(0.375),
                                  _mm256_mul_pd(y, y)),
                    _mm256_mul_pd(_mm256_set1_pd(1.25), y)),
      _mm256_set1_pd(1.875));
  estimate = _mm256_mul_pd(estimate, correction);
  _mm256_storeu_pd(destination->lane, estimate);
}
#endif

static inline void rn_f64x4_require(rn_f64x4_t value) {
  if (value == NULL) {
    raise("F64x4Null");
  }
}

static inline void rn_f64x4_require_range(const double *values,
                                          uint64_t offset) {
  if (values == NULL) {
    raise("F64x4ArrayNull");
  }
  const array_t *array = ((const array_t *)values) - 1;
  if (array->magic_hdr != ARRAY_MAGIC || array->magic_ftr != ARRAY_MAGIC ||
      array->element_size != sizeof(double) || offset > array->used ||
      array->used - offset < RN_F64X4_LANES) {
    raise("F64x4ArrayRange");
  }
}

static inline rn_f64x4_t f64x4New(void) {
  rn_f64x4_t result = (rn_f64x4_t)malloc(sizeof(rn_f64x4));
  if (result == NULL) {
    raise("OutOfMemory");
  }
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    result->lane[lane] = 0.0;
  }
  return result;
}

static inline void f64x4Set(rn_f64x4_t destination,
                            const rn_f64x4_t source) {
  rn_f64x4_require(destination);
  rn_f64x4_require(source);
#if RN_F64X4_HAS_X86_AVX
  if (rn_f64x4_avx_available()) {
    rn_f64x4_copy_avx(destination, source);
    return;
  }
#endif
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = source->lane[lane];
  }
}

static inline void f64x4Splat(rn_f64x4_t destination, double value) {
  rn_f64x4_require(destination);
#if RN_F64X4_HAS_X86_AVX
  if (rn_f64x4_avx_available()) {
    rn_f64x4_splat_avx(destination, value);
    return;
  }
#endif
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = value;
  }
}

static inline void f64x4Load(rn_f64x4_t destination, const double *source,
                             uint64_t offset) {
  rn_f64x4_require(destination);
  rn_f64x4_require_range(source, offset);
#if RN_F64X4_HAS_X86_AVX
  if (rn_f64x4_avx_available()) {
    rn_f64x4_load_avx(destination, source + offset);
    return;
  }
#endif
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = source[offset + lane];
  }
}

static inline void f64x4Store(const rn_f64x4_t source, double *destination,
                              uint64_t offset) {
  rn_f64x4_require(source);
  rn_f64x4_require_range(destination, offset);
#if RN_F64X4_HAS_X86_AVX
  if (rn_f64x4_avx_available()) {
    rn_f64x4_store_avx(source, destination + offset);
    return;
  }
#endif
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination[offset + lane] = source->lane[lane];
  }
}

static inline void f64x4Add(rn_f64x4_t destination, const rn_f64x4_t left,
                            const rn_f64x4_t right) {
  rn_f64x4_require(destination);
  rn_f64x4_require(left);
  rn_f64x4_require(right);
#if RN_F64X4_HAS_X86_AVX
  if (rn_f64x4_avx_available()) {
    rn_f64x4_add_avx(destination, left, right);
    return;
  }
#endif
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = left->lane[lane] + right->lane[lane];
  }
}

static inline void f64x4Sub(rn_f64x4_t destination, const rn_f64x4_t left,
                            const rn_f64x4_t right) {
  rn_f64x4_require(destination);
  rn_f64x4_require(left);
  rn_f64x4_require(right);
#if RN_F64X4_HAS_X86_AVX
  if (rn_f64x4_avx_available()) {
    rn_f64x4_sub_avx(destination, left, right);
    return;
  }
#endif
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = left->lane[lane] - right->lane[lane];
  }
}

static inline void f64x4Mul(rn_f64x4_t destination, const rn_f64x4_t left,
                            const rn_f64x4_t right) {
  rn_f64x4_require(destination);
  rn_f64x4_require(left);
  rn_f64x4_require(right);
#if RN_F64X4_HAS_X86_AVX
  if (rn_f64x4_avx_available()) {
    rn_f64x4_mul_avx(destination, left, right);
    return;
  }
#endif
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = left->lane[lane] * right->lane[lane];
  }
}

static inline double f64x4HorizontalSum(const rn_f64x4_t value) {
  rn_f64x4_require(value);
  double sum = 0.0;
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    sum += value->lane[lane];
  }
  return sum;
}

/* Explicitly named approximate reciprocal square root. The AVX path converts
 * through float rsqrt and one Goldschmidt refinement; non-x86 keeps the
 * correctly rounded scalar fallback. */
static inline void f64x4ApproxReciprocalSqrt(rn_f64x4_t destination,
                                             const rn_f64x4_t source) {
  rn_f64x4_require(destination);
  rn_f64x4_require(source);
#if RN_F64X4_HAS_X86_AVX
  if (rn_f64x4_avx_available()) {
    rn_f64x4_approx_rsqrt_avx(destination, source);
    return;
  }
#endif
  for (uint64_t lane = 0; lane < RN_F64X4_LANES; lane++) {
    destination->lane[lane] = 1.0 / sqrt(source->lane[lane]);
  }
}

static inline uint8_t simdAvxAvailable(void) {
  return rn_f64x4_avx_available() ? 1 : 0;
}
