/*
 * Portable sixteen-lane byte values with an explicitly opt-in x86 fast path.
 *
 * The x86 implementation requires both SSSE3 (pshufb) and SSE4.1 (blendv).
 * Callers must dispatch through simdU8x16Available() before selecting a path
 * that depends on those instructions. Other targets retain the same API and
 * exact byte semantics through the scalar representation below.
 */

#include <stdint.h>

#define RN_U8X16_LANES 16u

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#define RN_U8X16_HAS_X86_SIMD 1
#define RN_U8X16_TARGET __attribute__((target("ssse3,sse4.1")))
#else
#define RN_U8X16_HAS_X86_SIMD 0
#define RN_U8X16_TARGET
#endif

/* Both SIMD fragments define this identically and guard it so either fragment
 * may be emitted first. Mixed-target functions need one explicit union of all
 * required ISA features rather than compiler-dependent stacked attributes. */
#ifndef RN_SIMD_COMBINED_TARGET
#if defined(__x86_64__) || defined(__i386__)
#define RN_SIMD_COMBINED_TARGET \
  __attribute__((target("avx,ssse3,sse4.1")))
#else
#define RN_SIMD_COMBINED_TARGET
#endif
#endif

#if RN_U8X16_HAS_X86_SIMD
typedef __m128i rn_u8x16_t;
#else
typedef struct rn_u8x16_value {
  uint8_t lane[RN_U8X16_LANES];
} rn_u8x16_t;
#endif

#if defined(__clang__) || defined(__GNUC__)
#define RN_U8X16_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define RN_U8X16_ALWAYS_INLINE inline
#endif

#define RN_U8X16_VALUE_FN static RN_U8X16_TARGET RN_U8X16_ALWAYS_INLINE

/* Validate the complete headed Rune u8-array contract before a vector access.
 * Subtract only after the null check; offset arithmetic is written without an
 * overflowing offset + lane-count expression. */
static RN_U8X16_ALWAYS_INLINE void
rn_u8x16_require_range(const uint8_t *values, uint64_t offset) {
  if (values == NULL) {
    raise("U8x16ArrayNull");
  }
  const array_t *array = ((const array_t *)values) - 1;
  if (array->magic_hdr != ARRAY_MAGIC || array->magic_ftr != ARRAY_MAGIC) {
    raise("U8x16ArrayHeader");
  }
  if (array->element_size != sizeof(uint8_t)) {
    raise("U8x16ArrayElement");
  }
  if (offset > array->used || array->used - offset < RN_U8X16_LANES) {
    raise("U8x16ArrayRange");
  }
}

RN_U8X16_VALUE_FN rn_u8x16_t u8x16Load(const uint8_t *source,
                                        uint64_t offset) {
  rn_u8x16_require_range(source, offset);
  rn_u8x16_t result;
#if RN_U8X16_HAS_X86_SIMD
  result = _mm_loadu_si128((const __m128i *)(source + offset));
#else
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    result.lane[lane] = source[offset + lane];
  }
#endif
  return result;
}

RN_U8X16_VALUE_FN void u8x16Store(rn_u8x16_t source, uint8_t *destination,
                                   uint64_t offset) {
  rn_u8x16_require_range(destination, offset);
#if RN_U8X16_HAS_X86_SIMD
  _mm_storeu_si128((__m128i *)(destination + offset), source);
#else
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    destination[offset + lane] = source.lane[lane];
  }
#endif
}

RN_U8X16_VALUE_FN rn_u8x16_t u8x16Splat(uint8_t value) {
  rn_u8x16_t result;
#if RN_U8X16_HAS_X86_SIMD
  result = _mm_set1_epi8((char)value);
#else
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    result.lane[lane] = value;
  }
#endif
  return result;
}

/* Byte arithmetic is deliberately modulo 256 on every lane. */
RN_U8X16_VALUE_FN rn_u8x16_t u8x16Add(rn_u8x16_t left,
                                       rn_u8x16_t right) {
  rn_u8x16_t result;
#if RN_U8X16_HAS_X86_SIMD
  result = _mm_add_epi8(left, right);
#else
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    result.lane[lane] = (uint8_t)(left.lane[lane] + right.lane[lane]);
  }
#endif
  return result;
}

RN_U8X16_VALUE_FN rn_u8x16_t u8x16Sub(rn_u8x16_t left,
                                       rn_u8x16_t right) {
  rn_u8x16_t result;
#if RN_U8X16_HAS_X86_SIMD
  result = _mm_sub_epi8(left, right);
#else
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    result.lane[lane] = (uint8_t)(left.lane[lane] - right.lane[lane]);
  }
#endif
  return result;
}

/* Match _mm_andnot_si128: result = (~left) & right. */
RN_U8X16_VALUE_FN rn_u8x16_t u8x16AndNot(rn_u8x16_t left,
                                          rn_u8x16_t right) {
  rn_u8x16_t result;
#if RN_U8X16_HAS_X86_SIMD
  result = _mm_andnot_si128(left, right);
#else
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    result.lane[lane] = (uint8_t)(~left.lane[lane] & right.lane[lane]);
  }
#endif
  return result;
}

/* Comparison results are canonical byte masks: 0xff for true, 0x00 false. */
RN_U8X16_VALUE_FN rn_u8x16_t u8x16Equal(rn_u8x16_t left,
                                         rn_u8x16_t right) {
  rn_u8x16_t result;
#if RN_U8X16_HAS_X86_SIMD
  result = _mm_cmpeq_epi8(left, right);
#else
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    result.lane[lane] = left.lane[lane] == right.lane[lane] ? 0xffu : 0u;
  }
#endif
  return result;
}

/* Interpret each input byte as a two's-complement signed int8 value. */
RN_U8X16_VALUE_FN rn_u8x16_t u8x16GreaterThanSigned(rn_u8x16_t left,
                                                     rn_u8x16_t right) {
  rn_u8x16_t result;
#if RN_U8X16_HAS_X86_SIMD
  result = _mm_cmpgt_epi8(left, right);
#else
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    const int16_t signed_left = left.lane[lane] < 0x80u ?
        (int16_t)left.lane[lane] : (int16_t)left.lane[lane] - 0x100;
    const int16_t signed_right = right.lane[lane] < 0x80u ?
        (int16_t)right.lane[lane] : (int16_t)right.lane[lane] - 0x100;
    result.lane[lane] = signed_left > signed_right ? 0xffu : 0u;
  }
#endif
  return result;
}

/* Match pshufb exactly: bit 7 clears a lane; otherwise bits 0..3 select the
 * source lane and bits 4..6 are ignored. */
RN_U8X16_VALUE_FN rn_u8x16_t u8x16Shuffle(rn_u8x16_t source,
                                           rn_u8x16_t indices) {
  rn_u8x16_t result;
#if RN_U8X16_HAS_X86_SIMD
  result = _mm_shuffle_epi8(source, indices);
#else
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    const uint8_t index = indices.lane[lane];
    result.lane[lane] = (index & 0x80u) != 0 ?
        0u : source.lane[index & 0x0fu];
  }
#endif
  return result;
}

/* Select right where the mask byte's high bit is set, otherwise left. */
RN_U8X16_VALUE_FN rn_u8x16_t
u8x16BlendByHighBit(rn_u8x16_t left, rn_u8x16_t right, rn_u8x16_t mask) {
  rn_u8x16_t result;
#if RN_U8X16_HAS_X86_SIMD
  result = _mm_blendv_epi8(left, right, mask);
#else
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    result.lane[lane] = (mask.lane[lane] & 0x80u) != 0 ?
        right.lane[lane] : left.lane[lane];
  }
#endif
  return result;
}

/* Shift lanes toward higher indices and insert low: [a..p], x -> [x,a..o]. */
RN_U8X16_VALUE_FN rn_u8x16_t
u8x16ShiftRightInsertLow(rn_u8x16_t source, uint8_t low) {
  rn_u8x16_t result;
#if RN_U8X16_HAS_X86_SIMD
  const __m128i shifted = _mm_slli_si128(source, 1);
  result = _mm_or_si128(shifted, _mm_cvtsi32_si128((int)low));
#else
  result.lane[0] = low;
  for (uint64_t lane = 1; lane < RN_U8X16_LANES; lane++) {
    result.lane[lane] = source.lane[lane - 1];
  }
#endif
  return result;
}

/* Return lane high bits with lane 0 in bit 0 and lane 15 in bit 15. */
RN_U8X16_VALUE_FN uint16_t u8x16MoveMask(rn_u8x16_t source) {
#if RN_U8X16_HAS_X86_SIMD
  return (uint16_t)_mm_movemask_epi8(source);
#else
  uint16_t result = 0;
  for (uint64_t lane = 0; lane < RN_U8X16_LANES; lane++) {
    result |= (uint16_t)((source.lane[lane] >> 7) << lane);
  }
  return result;
#endif
}

/* Pack lanes 0..3 into a little-endian u32, independent of host endianness. */
RN_U8X16_VALUE_FN uint32_t u8x16LowU32(rn_u8x16_t source) {
#if RN_U8X16_HAS_X86_SIMD
  return (uint32_t)_mm_cvtsi128_si32(source);
#else
  return (uint32_t)source.lane[0] |
      ((uint32_t)source.lane[1] << 8) |
      ((uint32_t)source.lane[2] << 16) |
      ((uint32_t)source.lane[3] << 24);
#endif
}

/* Count low zero bits in a u16; define the otherwise-undefined zero case. */
RN_U8X16_VALUE_FN uint32_t u16TrailingZeros(uint16_t value) {
  return value == 0 ? 16u : (uint32_t)__builtin_ctz((unsigned)value);
}

static RN_U8X16_ALWAYS_INLINE uint8_t simdU8x16Available(void) {
#if RN_U8X16_HAS_X86_SIMD
  return __builtin_cpu_supports("ssse3") &&
      __builtin_cpu_supports("sse4.1") ? 1u : 0u;
#else
  return 0u;
#endif
}
