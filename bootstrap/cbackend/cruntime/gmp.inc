/*
 * Optional GMP-backed arbitrary-precision integer runtime.
 *
 * This is deliberately an opaque, destination-taking API. Rune's fixed-width
 * iN/uN values retain their current two's-complement overflow semantics; a
 * future distinct BigInt type owns one rn_bigint_t and lowers here.
 *
 * CRuntime.needGmp() emits string_methods.inc and raise() before this file.
 */

#include <gmp.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct rn_bigint {
  mpz_t value;
} rn_bigint;

typedef rn_bigint *rn_bigint_t;

static inline rn_bigint_t rn_bigint_alloc(void) {
  rn_bigint_t result = (rn_bigint_t)malloc(sizeof(rn_bigint));
  if (result == NULL) {
    raise("OutOfMemory");
  }
  mpz_init(result->value);
  return result;
}

static inline void rn_bigint_set_mpz_u64(mpz_t destination, uint64_t value) {
  /* mpz_import avoids assuming unsigned long is at least 64 bits. */
  mpz_import(destination, 1, 1, sizeof(value), 0, 0, &value);
}

static inline rn_bigint_t rn_bigint_new_u64(uint64_t value) {
  rn_bigint_t result = rn_bigint_alloc();
  rn_bigint_set_mpz_u64(result->value, value);
  return result;
}

static inline void rn_bigint_set(rn_bigint_t destination,
                                  const rn_bigint *source) {
  mpz_set(destination->value, source->value);
}

static inline void rn_bigint_add(rn_bigint_t destination,
                                  const rn_bigint *left,
                                  const rn_bigint *right) {
  mpz_add(destination->value, left->value, right->value);
}

static inline void rn_bigint_sub(rn_bigint_t destination,
                                  const rn_bigint *left,
                                  const rn_bigint *right) {
  mpz_sub(destination->value, left->value, right->value);
}

static inline void rn_bigint_mul(rn_bigint_t destination,
                                  const rn_bigint *left,
                                  const rn_bigint *right) {
  mpz_mul(destination->value, left->value, right->value);
}

static inline void rn_bigint_mul_u64(rn_bigint_t destination,
                                      const rn_bigint *left, uint64_t right) {
#if ULONG_MAX >= UINT64_MAX
  mpz_mul_ui(destination->value, left->value, (unsigned long)right);
#else
  mpz_t right_value;
  mpz_init(right_value);
  rn_bigint_set_mpz_u64(right_value, right);
  mpz_mul(destination->value, left->value, right_value);
  mpz_clear(right_value);
#endif
}

static inline void rn_bigint_add_u64(rn_bigint_t destination,
                                      const rn_bigint *left, uint64_t right) {
#if ULONG_MAX >= UINT64_MAX
  mpz_add_ui(destination->value, left->value, (unsigned long)right);
#else
  mpz_t right_value;
  mpz_init(right_value);
  rn_bigint_set_mpz_u64(right_value, right);
  mpz_add(destination->value, left->value, right_value);
  mpz_clear(right_value);
#endif
}

static inline void rn_bigint_sub_u64(rn_bigint_t destination,
                                      const rn_bigint *left, uint64_t right) {
#if ULONG_MAX >= UINT64_MAX
  mpz_sub_ui(destination->value, left->value, (unsigned long)right);
#else
  mpz_t right_value;
  mpz_init(right_value);
  rn_bigint_set_mpz_u64(right_value, right);
  mpz_sub(destination->value, left->value, right_value);
  mpz_clear(right_value);
#endif
}

static inline void rn_bigint_div_trunc(rn_bigint_t destination,
                                        const rn_bigint *left,
                                        const rn_bigint *right) {
  if (mpz_sgn(right->value) == 0) {
    raise("DivByZero");
  }
  mpz_tdiv_q(destination->value, left->value, right->value);
}

static inline uint8_t rn_bigint_less(const rn_bigint *left,
                                      const rn_bigint *right) {
  return (uint8_t)(mpz_cmp(left->value, right->value) < 0);
}

static inline uint64_t rn_bigint_to_u64(const rn_bigint *value) {
  uint64_t result = 0;
  if (mpz_sgn(value->value) < 0 || mpz_sizeinbase(value->value, 2) > 64) {
    raise("Overflow");
  }
  mpz_export(&result, NULL, 1, sizeof(result), 0, 0, value->value);
  return result;
}

static inline char *rn_bigint_to_string(const rn_bigint *value) {
  size_t digits = mpz_sizeinbase(value->value, 10);
  size_t capacity = digits + (mpz_sgn(value->value) < 0 ? 1 : 0);
  char *result = rn_stralloc(capacity);
  mpz_get_str(result, 10, value->value);
  /* GMP permits mpz_sizeinbase() to overestimate non-power-of-two bases. */
  ((rn_strhdr *)result - 1)->len = strlen(result);
  return result;
}
