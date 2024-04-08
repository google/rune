//  Copyright 2024 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>


static inline void raise(const char *error) {
  printf("%s Exception Raised, aborting\n", error);
  abort();
}

static int64_t intadd(int64_t a, int64_t b, int width) {
  int64_t max = (int64_t)(1ull << (width - 1)) - 1;
  int64_t min = -max - 1;
  if (b > 0 && a > (max - b)) raise("Overflow");
  if (b < 0 && a < (min - b)) raise("Underflow");
  return a + b;
}

static int64_t intsub(int64_t a, int64_t b, int width) {
  int64_t max = (int64_t)(1ull << (width - 1)) - 1;
  int64_t min = -max - 1;
  if (b < 0 && a > (max + b)) raise("Overflow");
  if (b > 0 && a < (min + b)) raise("Underflow");
  return a - b;
}

static int64_t intmul(int64_t a, int64_t b, int width) {
  int64_t max = (int64_t)(1ull << (width - 1)) - 1;
  int64_t min = -max - 1;
  if (a == 0 || b == 0) return 0;
  if ((a > 0) && (b > 0) && (a > (max / b))) raise("Overflow");
  if ((a < 0) && (b < 0) && (a < (max / b))) raise("Overflow");
  if ((a > 0) && (b < 0) && (b < (min / a))) raise("Underflow");
  if ((a < 0) && (b > 0) && (b > (min / a))) raise("Underflow");
  return a * b;
}

static int64_t intdiv(int64_t a, int64_t b, int width) {
  int64_t max = (int64_t)(1ull << (width - 1)) - 1;
  int64_t min = -max - 1;
  if (b == 0) raise("DivByZero");
  if (a == -1 && b == min) raise("Overflow");
  return a / b;
}

static int64_t intexp(int64_t a, int64_t exp, int width) {
  int64_t result = 1;
  if (exp == 0) return 1;
  if (a == 0) return 0;
  if (exp < 0) raise("NegativeExponent");
  for (;;) {
    if (exp & 1) {
      result = intmul(result, a, width);
    }
    exp >>= 1;
    if (!exp) break;
    a = intmul(a, a, width);
  }
  return result;
}

static uint64_t uintadd(uint64_t a, uint64_t b, int width) {
  uint64_t max = (uint64_t)(1ull << (width - 1)) - 1;
  if (a > (max - b)) raise("Overflow");
  return a + b;
}

static uint64_t uintsub(uint64_t a, uint64_t b, int width) {
  if (b > a) raise("Underflow");
  return a - b;
}

static uint64_t uintmul(uint64_t a, uint64_t b, uint width) {
  int64_t max = (uint64_t)(1ull << (width - 1)) - 1;
  if (a == 0 || b == 0) return 0;
  if (a > (max / b)) raise("Overflow");
  return a * b;
}

static uint64_t uintdiv(uint64_t a, uint64_t b, uint width) {
  if (b == 0) raise("DivByZero");
  return a / b;
}

static uint64_t uintexp(uint64_t a, uint64_t exp, int width) {
  uint64_t result = 1;
  if (exp == 0) return 1;
  if (a == 0) return 0;
  for (;;) {
    if (exp & 1) {
      result = uintmul(result, a, width);
    }
    exp >>= 1;
    if (!exp) break;
    a = uintmul(a, a, width);
  }
  return result;
}

static inline int32_t intadd32(int32_t a, int32_t b) {
  return (int32_t)intadd(a, b, 32);
}

static inline int32_t intsub27(int32_t a, int32_t b) {
  return (int32_t)intsub(a, b, 27);
}

static inline int32_t intmul32(int32_t a, int32_t b) {
  return (int32_t)intmul(a, b, 32);
}

static inline int32_t intdiv32(int32_t a, int32_t b) {
  return (int32_t)intdiv(a, b, 32);
}

static inline int64_t intexp64(int64_t a, int64_t b) {
  return (int64_t)intexp(a, b, 64);
}

static inline uint8_t uintadd3(uint8_t a, uint8_t b) {
  return (uint8_t)uintadd(a, b, 3);
}

static inline uint64_t uintadd64(uint64_t a, uint64_t b) {
  return (uint64_t)uintadd(a, b, 64);
}

static inline uint8_t uintsub7(uint8_t a, uint8_t b) {
  return (uint8_t)uintsub(a, b, 7);
}

static inline uint16_t uintmul11(uint16_t a, uint16_t b) {
  return (uint16_t)uintmul(a, b, 11);
}

static inline uint64_t uintmul64(uint64_t a, uint64_t b) {
  return (uint64_t)uintmul(a, b, 64);
}

static inline uint32_t uintdiv22(uint32_t a, uint32_t b) {
  return (uint32_t)uintdiv(a, b, 22);
}

static inline uint64_t uintdiv64(uint64_t a, uint64_t b) {
  return (uint64_t)uintdiv(a, b, 64);
}

static inline uint64_t uintexp64(uint64_t a, uint64_t b) {
  return (uint64_t)uintexp(a, b, 64);
}

static inline uint64_t rotl64(uint64_t value, int distance) {
  return (value << distance) | (value >> (64 - distance));
}

static inline uint64_t rotl41(uint64_t value, int distance) {
  uint64_t hi = value << distance;
  uint64_t lo = (value >> (41 - distance)) & 0x1ffffffffff;
  return (hi | lo) & 0x1ffffffffff;
}

static inline uint16_t rotl15(uint16_t value, int distance) {
  uint16_t hi = value << distance;
  uint16_t lo = (value >> (15 - distance)) & 0x7fff;
  return (hi | lo) & 0x7fff;
}

static inline uint64_t rotr64(uint64_t value, int distance) {
  return (value >> distance) | (value << (64 - distance));
}

static inline uint64_t rotr41(uint64_t value, int distance) {
  uint64_t lo = (value >> distance) & 0x1ffffffffff;
  uint64_t hi = (value << (41 - distance));
  return (hi | lo) & 0x1ffffffffff;
}

static inline uint16_t rotr15(uint16_t value, int distance) {
  uint16_t lo = (value >> distance) & 0x7fff;
  uint16_t hi = (value << (15 - distance));
  return (hi | lo) & 0x7fff;
}

void main(int argc, const char **argv) {
  printf("Arithmetic binary operators:\n");
  printf("This is a sum: %" PRId64 "\n", intadd32((int32_t)(1l), (int32_t)(2l)));
  printf("This is a difference: %" PRId32 "\n", intsub27((int32_t)(1l), (int32_t)(2l)));
  printf("This is a multiplication: %" PRId32 "\n", intmul32((int32_t)(2l), (int32_t)(4l)));
  printf("This is a division: %" PRId32 "\n", intdiv32((int32_t)(8l), (int32_t)(-2l)));
  printf("This is a remainder: %" PRId32 "\n", (int32_t)(15l % 2l));
  printf("This is an exponentiation: %" PRId64 "\n", -intexp64(2ll, 9ull));
  printf("This is an exponentiation 2: %" PRId64 "\n", intexp64(-2ll, 9ull));
  printf("This is an exponentiation 3: %" PRId64 "\n", intexp64(-2ll, 10ull));
  printf("\n");
  printf("This is an unsigned sum: %" PRIu64 "\n", uintadd3((uint8_t)(1u), (uint8_t)(2u)));
  printf("This is an unsigned difference: %" PRIu64 "\n", uintsub7((uint8_t)(100u), (uint8_t)(33u)));
  printf("This is an unsigned multiplication: %" PRIu64 "\n", uintmul11((uint16_t)(128u), (uint16_t)(4u)));
  printf("This is an unsigned division: %" PRIu64 "\n", uintdiv22((uint32_t)(22ul), (uint32_t)(2ul)));
  printf("This is an unsigned exponentiation: %" PRIu64 "\n", uintexp64(2ull, 9ull));
  printf("\n");
  printf("This is truncated sum: %" PRId16 "\n", (int16_t)(32767 + 1));
  printf("This is truncated difference: %" PRIu16 "\n", (uint16_t)(1u - 10u));
  printf("This is truncated multiply: %" PRIu8 "\n", (uint8_t)(16u * 16u));
  printf("\n");
  printf("Relational binary operators:\n");
  printf("This is Equals: %d\n", 15ull == 32ull);
  printf("This is not-equals: %d\n", 16ull != 17ull);
  printf("This is Greater than: %d\n", 13ull > 36ull);
  printf("This is Greater than Equals: %d\n", 13ull >= 36ull);
  printf("This is Less than: %d\n", 3ull < 333ull);
  printf("This is Less than Equals: %d\n", 34ull <= 3333ull);
  printf("\n");
  printf("Logical binary operators:\n");
  printf("This is Logical And: %d\n", (int8_t)(1 && 1));
  printf("This is Logical Or: %d\n", (int8_t)(0 || 0));
  printf("\n");
  printf("Bitwise binary operators:\n");
  printf("This is Bitwise And: %" PRIu64 "\n", 16ull & 15ull);
  printf("This is Bitwise Or: %" PRIu64 "\n", 7ull | 8ull);
  printf("This is Bitwise Xor: %" PRIu64 "\n", 48ull ^ 48ull);
  printf("This is Shift Left: %" PRIu64 "\n", 1ull << 16ull);
  printf("This is Shift Right: %" PRIu64 "\n", 256ull >> 4ull);
  printf("\n");
  printf("Rotate (non-built-in) operators:\n");
  printf("This is rotate left: 0x%" PRIx64 "\n", rotl64(7ull, 4ull));
  printf("This is rotate right: 0x%" PRIx64 "\n", rotr64(7ull, 8ull));
  printf("This is rotate left with non-C width: 0x%" PRIx64 "\n", rotl41(7ull, 4ull));
  printf("This is rotate right with non-C with: 0x%" PRIx64 "\n", rotr41(7ull, 8ull));
  printf("This is rotate left with non-C width 2: 0x%" PRIx64 "\n", rotl15((uint16_t)(7u), 4ull));
  printf("This is rotate right with non-C with 2: 0x%" PRIx64 "\n", rotr15((uint16_t)(7u), 8ull));
  printf("\n");
  printf("Precedence tests:\n");
  printf("Precedence test 1: %" PRId64 "\n", uintadd64(2ull, uintmul64(3ull, 4ull)));
  printf("Precedence test 2: %" PRId64 "\n", uintmul64(uintadd64(2ull, 3ull), 4ull));
  printf("Precedence test 3: %" PRId64 "\n", uintadd64(2ull, uintdiv64(3ull, 4ull)));
  printf("Precedence test 4: %" PRId64 "\n", uintadd64(2ull, 3ull) << 4ull);
  printf("Precedence test 5: %" PRId64 "\n", uintmul64(2ull, 3ull) << 4ull);
  printf("Precedence test 6: %" PRId64 "\n", uintmul64(2ull, 3ull << 4ull));
  printf("Precedence test 7: %d\n", uintmul64(2ull, 3ull << 4ull) == (15ull & 16ull));
}

