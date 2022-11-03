//  Copyright 2021 Google LLC.
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

// Constant time bigint runtime functions, based on CTTK.  Be careful with
// pointers into the bigint data, such as those returned by getConstBigintData.
// They become invalid after a heap compaction or resize, so be sure to reload
// them after any operation that effects the heap.
#include "runtime.h"
#include "../../CTTK/cttk.h"
#include <sys/types.h>

// TODO: disable this.
//#define RN_DEBUG

// The CTTK Not-a-Number bit in the CTTK header.
#define RN_NAN_BIT 0x80000000u

// Return the absolute value of a.
static inline int64_t smallNumAbs(int64_t a) {
  return a >= 0? a : -a;
}

// Return the number of words in the bigint.
static inline uint32_t findBigintNumWords(uint32_t width) {
  // TODO: We can save a word by integrating the sign bit into the CTTK header.
  return 2 + (width + 30)/31;
}

const runtime_bool runtime_false = CTTK_INIT(0);
const runtime_bool runtime_true = CTTK_INIT(1);

// Return a uint32_t *pointer to the bigint data.
static inline uint32_t *getBigintData(const runtime_array *bigint) {
  return (uint32_t*)(bigint->data);
}

// Return a const uint32_t *pointer to the bigint data.
static inline const uint32_t *getConstBigintData(const runtime_array *bigint) {
  return (const uint32_t*)(bigint->data);
}

// Return the width in bits of the bigint.  Under the hood, unsigned integers
// are represented with 1 extra bit so they are never negative.  This extra bit
// is not included in the returned width.
static inline uint32_t getBigintWidth(const uint32_t *data) {
  uint32_t header = data[1];
#ifdef RN_DEBUG
  if (header & RN_NAN_BIT) {
    runtime_panicCstr("NaN set");
  }
#endif
  return header - (header >> 5);
}

// Return true if the bigint is a signed integer.
bool runtime_bigintSigned(const runtime_array *bigint) {
  const uint32_t *data = getConstBigintData(bigint);
  if (data == NULL) {
    return false;
  }
  return (*data & RN_SIGNED_BIT) != 0;
}

// Return the width in bits of the bigint.  Under the hood, unsigned integers
// are represented with 1 exta bit so they are never negative.  This extra bit
// is not included in the returned width.
uint32_t runtime_bigintWidth(const runtime_array *bigint) {
  const uint32_t *data = getConstBigintData(bigint);
  if (data == NULL) {
    return 0;
  }
  uint32_t width = getBigintWidth(data);
  if (runtime_bigintSigned(bigint)) {
#ifdef RN_DEBUG
    if (width == 0) {
      runtime_panicCstr("Zero width");
    }
#endif
    return width;
  }
#ifdef RN_DEBUG
    if (width <= 1) {
      runtime_panicCstr("Zero width");
    }
#endif
  return width - 1;
}

// Return true if the bigint is secret.
bool runtime_bigintSecret(const runtime_array *bigint) {
  const uint32_t *data = getConstBigintData(bigint);
  if (data == NULL) {
    return false;
  }
  return (*data & RN_SECRET_BIT) != 0;
}

// Return true if the bigint is secret.
void runtime_bigintSetSecret(runtime_array *bigint, bool value) {
  uint32_t *data = getBigintData(bigint);
  if (data == NULL) {
    runtime_panicCstr("Tried to set secret bit of empty bigint");
  }
  if (value) {
    *data |= RN_SECRET_BIT;
  } else {
    *data &= ~RN_SECRET_BIT;
  }
}

// Return true if |bigint| is set to Not-A-Number.
static inline bool isNAN(const runtime_array *bigint) {
  const uint32_t *data = getConstBigintData(bigint);
  return (data[1] & RN_NAN_BIT) != 0;
}

// Restore the header, overriding the const qualifier on data.
static inline void checkForNAN(const runtime_array *bigint) {
  if  (isNAN(bigint)) {
    runtime_throwExceptionCstr("Bigint was set to NaN");
  }
}

// Return the sign bit of the most significant word.  This is the same as
// top_index in CTTK.
static inline uint32_t getSignBitPosition(const uint32_t *data) {
  uint32_t header = data[1];
  header = (header & 0x1f) - 1;
  return header + (31 & (header >> 5));
}

// We have to represent unsigned values with a hidden extra bit when using CTTK,
// and manually check for underflow, where the result of an operation is
// negative.  For wrapping opcodes, we just want to set the sign bits to 0.
static void fixUnderflow(runtime_array *bigint) {
  if (!runtime_bigintSigned(bigint)) {
    uint32_t *data = getBigintData(bigint);
    uint32_t signPos = getSignBitPosition(data);
    data[bigint->numElements - 1] &= (1 << signPos) - 1;
  }
}

// We have to represent unsigned values with a hidden extra bit when using CTTK,
// and manually check for underflow, where the result of an operation is
// negative.
static inline void checkForUnderflow(runtime_array *bigint) {
  if (!runtime_bigintSigned(bigint)) {
    uint32_t *data = getBigintData(bigint);
    uint32_t highWord = data[bigint->numElements - 1];
    if ((highWord & 0x40000000u) != 0) {
      runtime_throwExceptionCstr("Unsigned integer underflow");
    }
  }
}

// Return true if the bigint is zero.  Return an runtime_bool to help avoid leaking the
// result through a timing side-channel.
runtime_bool runtime_bigintZero(const runtime_array *a) {
  uint64_t result = 0;
  const uint32_t *data = getConstBigintData(a);
  for (uint64_t i = 2; i < a->numElements; i++) {
    result |= data[i];
  }
  return runtime_boolToRnBool(result == 0);
}

// Return true of the bigint is < 0.
runtime_bool runtime_bigintNegative(const runtime_array *a) {
  const uint32_t *data =  getConstBigintData(a);
  return runtime_boolToRnBool((data[a->numElements - 1] >> 30) != 0);
}

// Resize a bigint in place.
static inline void resizeBigint(runtime_array *bigint, uint32_t width, bool isSigned) {
  if (!isSigned) {
    width++;
  }
  uint32_t numWords = findBigintNumWords(width);
  if (bigint->numElements != numWords) {
    runtime_resizeArray(bigint, numWords, sizeof(uint32_t), false);
  }
  uint32_t *data = getBigintData(bigint);
  cti_init(data + 1, width);
}

// Set the signed bit to match isSigned.
static inline void setSigned(uint32_t *data, bool isSigned) {
  if (isSigned) {
    *data |= RN_SIGNED_BIT;
  } else {
    *data &= ~RN_SIGNED_BIT;
  }
}

// Set the signed bit to match isSigned.
static inline void setSecret(uint32_t *data, bool secret) {
  if (secret) {
    *data |= RN_SECRET_BIT;
  } else {
    *data &= ~RN_SECRET_BIT;
  }
}

// Initialize a bigint in an array.  Bigints are stored in arrays on the array heap.
static void initBigint(runtime_array *bigint, uint32_t width, bool isSigned, bool secret) {
  if (runtime_bigintWidth(bigint) != width) {
    resizeBigint(bigint, width, isSigned);
  }
  uint32_t *data = getBigintData(bigint);
  setSigned(data, isSigned);
  setSecret(data, secret);
  // Clear the NaN bit.
  data[1] &= ~RN_NAN_BIT;
}

// Cast a bigint.  If truncate is true, don't throw an exception if we lose bits.
// and truncate is false.
void runtime_bigintCast(runtime_array *dest, runtime_array *source, uint32_t newWidth,
    bool isSigned, bool secret, bool truncate) {
  resizeBigint(dest, newWidth, isSigned);
  const uint32_t *sourceData = getConstBigintData(source);
  uint32_t *destData = getBigintData(dest);
  if (isSigned) {
    *destData = RN_SIGNED_BIT;
  }
  if (secret) {
    *destData |= RN_SECRET_BIT;
  }
  if (truncate) {
    cti_set_trunc(destData + 1, sourceData + 1);
    fixUnderflow(dest);
  } else {
    cti_set(destData + 1, sourceData + 1);
    checkForUnderflow(dest);
  }
  checkForNAN(dest);
}

// Initialize an array to hold |value| as a bigint.  The caller must sign-extend
// value to uint64_t width before calling this.
void runtime_integerToBigint(runtime_array *dest, uint64_t value, uint32_t width, bool isSigned, bool secret) {
  initBigint(dest, width, isSigned, secret);
  uint32_t *data = getBigintData(dest);
  uint32_t numWords = dest->numElements;
  for (uint32_t i = 2; i < numWords; i++) {
    data[i] = value & 0x7fffffff;
    if (isSigned) {
      value = ((int64_t)value) >> 31;
    } else {
      value >>= 31;
    }
  }
}

// Convert a bigint to an integer.  Throw an exception if it does not fit.
uint64_t runtime_bigintToInteger(const runtime_array *source) {
  const volatile uint32_t *data = getConstBigintData(source);
  uint32_t numWords = source->numElements;
  uint64_t init = -(uint64_t)(data[numWords-1] >> 30);
  uint64_t result = init;
  bool isBad = false;
  for (uint32_t i = numWords - 1; i >= 2; i--) {
    uint64_t shiftOut = ((int64_t)result) >> (sizeof(uint64_t)*8 - 31);
    isBad |= shiftOut ^ init;
    result = (result << 31) | data[i];
  }
  if (isBad) {
    runtime_throwExceptionCstr("Bigint does not fit into uint64_t");
  }
  return result;
}

// Like runtime_bigintToInteger, but does not give any error if we had to truncate
// |source| to fit into a uint64_t.
uint64_t runtime_bigintToIntegerTrunc(const runtime_array *source) {
  const volatile uint32_t *data = getConstBigintData(source);
  uint32_t numWords = source->numElements;
  uint64_t init = -(uint64_t)(data[numWords-1] >> 30);
  uint64_t result = init;
  for (uint32_t i = numWords - 1; i >= 2; i--) {
    result = (result << 31) | data[i];
  }
  return result;
}

// Convert a string (u8 array) to a bigint, little-endian.
void runtime_bigintDecodeLittleEndian(runtime_array *dest, runtime_array *byteArray,
    uint32_t width, bool isSigned, bool secret) {
  initBigint(dest, width, isSigned, secret);
  uint64_t len = byteArray->numElements;
  const void *bytes = (const void*)byteArray->data;
  uint32_t *data = getBigintData(dest);
  if (isSigned) {
    cti_decle_signed(data + 1, bytes, len);
  } else {
    cti_decle_unsigned(data + 1, bytes, len);
  }
  if (isNAN(dest)) {
    runtime_panicCstr("In toUintLE: Number too large to fit in %u bits", width);
  }
  setSigned(data, isSigned);
  setSecret(data, secret);
}

// Convert a string (u8 array) to a bigint, big-endian.
void runtime_bigintDecodeBigEndian(runtime_array *dest, runtime_array *byteArray,
    uint32_t width, bool isSigned, bool secret) {
  initBigint(dest, width, isSigned, secret);
  uint64_t len = byteArray->numElements;
  const void *bytes = (const void*)byteArray->data;
  uint32_t *data = getBigintData(dest);
  if (isSigned) {
    cti_decbe_signed(data + 1, bytes, len);
  } else {
    cti_decbe_unsigned(data + 1, bytes, len);
  }
  if (isNAN(dest)) {
    runtime_panicCstr("In toUintBE: Number too large to fit in %u bits", width);
  }
  setSigned(data, isSigned);
  setSecret(data, secret);
}

// Convert number of bits to number of bytes.
static inline uint32_t bitsToBytes(uint32_t bits) {
  return (bits + 7)/8;
}

// Encode the bigint into a byte array, little-endian.
void runtime_bigintEncodeLittleEndian(runtime_array *byteArray, runtime_array *source) {
  runtime_freeArray(byteArray);
  uint32_t numBytes = bitsToBytes(runtime_bigintWidth(source));
  runtime_allocArray(byteArray, numBytes, sizeof(uint8_t), false);
  void *data = byteArray->data;
  cti_encle(data, numBytes, getConstBigintData(source) + 1);
}

// Encode the bigint into a byte array, big-endian.
void runtime_bigintEncodeBigEndian(runtime_array *byteArray, runtime_array *source) {
  runtime_freeArray(byteArray);
  uint32_t numBytes = bitsToBytes(runtime_bigintWidth(source));
  runtime_allocArray(byteArray, numBytes, sizeof(uint8_t), false);
  void *data = byteArray->data;
  cti_encbe(data, numBytes, getConstBigintData(source) + 1);
}

// Convert a bigint to a uint32.  Throw an exception if the bigint does not fit.
uint32_t runtime_bigintToU32(const runtime_array *a) {
  uint64_t result = runtime_bigintToInteger(a);
  if ((uint32_t)result != result) {
    runtime_throwExceptionCstr("Bigint too large to fit into a u32");
  }
  return (uint32_t)result;
}

// Throw an exception if |a| and |b| have different size or if one is signed and
// the other is not.
static void checkBigintsHaveSameType(const runtime_array *a, const runtime_array *b) {
  if (runtime_bigintWidth(a) != runtime_bigintWidth(b) || runtime_bigintSigned(a) != runtime_bigintSigned(b)) {
    runtime_throwExceptionCstr("Different bigint types in binary operation");
  }
}

// Compare two bigints in constant time.
bool runtime_compareBigints(runtime_comparisonType compareType, const runtime_array *a, const runtime_array *b) {
  if (runtime_bigintWidth(a) != runtime_bigintWidth(b) || runtime_bigintSigned(a) != runtime_bigintSigned(b)) {
    return false;
  }
  const uint32_t *aData = getConstBigintData(a);
  const uint32_t *bData = getConstBigintData(b);
  checkBigintsHaveSameType(a, b);
  runtime_bool result;
  switch (compareType) {
    case RN_LT:
      result = cti_lt(aData + 1, bData + 1);
      break;
    case RN_LE:
      result = cti_leq(aData + 1, bData + 1);
      break;
    case RN_GT:
      result = cti_gt(aData + 1, bData + 1);
      break;
    case RN_GE:
      result = cti_geq(aData + 1, bData + 1);
      break;
    case RN_EQUAL:
      result = cti_eq(aData + 1, bData + 1);
      break;
    case RN_NOTEQUAL:
      result = cti_neq(aData + 1, bData + 1);
      break;
    default:
      runtime_panicCstr("Unexpected comparison type");
  }
  return runtime_rnBoolToBool(result);
}

typedef void (*runtime_binaryBigintFunc)(cti_elt *d, const cti_elt *a, const cti_elt *b);

// Perform a binary bigint operation, calling the CTTK function pointer.
static void binaryOperation(runtime_binaryBigintFunc func, runtime_array *dest, runtime_array *a, runtime_array *b) {
  if (a->data == NULL || b->data == NULL) {
    runtime_panicCstr("Null array passed to binaryOperration");
  }
  checkBigintsHaveSameType(a, b);
  bool secret = runtime_bigintSecret(a) || runtime_bigintSecret(b);
  initBigint(dest, runtime_bigintWidth(a), runtime_bigintSigned(a), secret);
  const uint32_t *aData = getConstBigintData(a);
  const uint32_t *bData = getConstBigintData(b);
  uint32_t *destData = getBigintData(dest);
  func(destData + 1, aData + 1, bData + 1);
  checkForNAN(dest);
}

// Add two bigints.  Throw an exception on overflow/underflow.
void runtime_bigintAdd(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_add, dest, a, b);
  checkForUnderflow(dest);
}

// Add two bigints.  Truncate the result if it is too big.
void runtime_bigintAddTrunc(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_add_trunc, dest, a, b);
  fixUnderflow(dest);
}

// Subtract two bigints.  Throw an exception on overflow/underflow.
void runtime_bigintSub(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_sub, dest, a, b);
  checkForUnderflow(dest);
}

// Subtract two bigints.  Truncate the result if it is too big.
void runtime_bigintSubTrunc(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_sub_trunc, dest, a, b);
  fixUnderflow(dest);
}

// Multiply two bigints.  Throw an exception on overflow/underflow.
void runtime_bigintMul(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_mul, dest, a, b);
}

// Subtract two bigints.  Truncate the result if it is too big.
void runtime_bigintMulTrunc(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_mul_trunc, dest, a, b);
  fixUnderflow(dest);
}

// Divide two bigints.  Throw an exception if |b| is 0.
void runtime_bigintDiv(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_div, dest, a, b);
}

// Compute the remainder of two bigints.  Throw an exception if |b| is 0.
void runtime_bigintMod(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_mod, dest, a, b);
}

// Variable time non-modular exponentiation.  The base may be secret, but not
// the exponent, which must be unsigned.  |dest| and |base| can be the same.
void runtime_bigintExp(runtime_array *dest, runtime_array *base, uint32_t exponent) {
  uint32_t width = runtime_bigintWidth(base);
  bool isSigned = runtime_bigintSigned(base);
  bool secret = runtime_bigintSecret(base);
  runtime_array t = runtime_makeEmptyArray();
  runtime_copyBigint(&t, base);
  // Set dest to 1 after initializing t in case dest == base.
  runtime_integerToBigint(dest, 1, width, isSigned, secret);
  while (exponent != 0) {
    if (exponent & 1) {
      runtime_bigintMul(dest, dest, &t);
    }
    exponent >>= 1;
    if (exponent != 0) {
      // Be careful not to overflow t with an extra squaring.
      runtime_bigintMul(&t, &t, &t);
    }
  }
  runtime_freeArray(&t);
}

typedef void (*runtime_unaryBigintFunc)(cti_elt *d, const cti_elt *a);

// Perform a binary bigint operation, calling the CTTK function pointer.
static void unaryOperation(runtime_unaryBigintFunc func, runtime_array *dest, runtime_array *a) {
  initBigint(dest, runtime_bigintWidth(a), runtime_bigintSigned(a), runtime_bigintSecret(a));
  uint32_t *destData = getBigintData(dest);
  const uint32_t *aData = getConstBigintData(a);
  func(destData + 1, aData + 1);
  checkForNAN(dest);
}

// Negate a bigint.  Throw an error if it is not signed, or if its value is the
// max negative value.
void runtime_bigintNegate(runtime_array *dest, runtime_array *a) {
  if (!runtime_bigintSigned(a)) {
    runtime_throwExceptionCstr("Negating an unsigned value");
  }
  unaryOperation(cti_neg, dest, a);
}

// Negate a bigint.  Don't check for the signed bit or being max negative number.
void runtime_bigintNegateTrunc(runtime_array *dest, runtime_array *a) {
  if (!runtime_bigintSigned(a)) {
    runtime_throwExceptionCstr("Negating an unsigned value");
  }
  unaryOperation(cti_neg_trunc, dest, a);
}

// Complement a bigint.  Throw an exception if it is signed.
void runtime_bigintComplement(runtime_array *dest, runtime_array *a) {
  unaryOperation(cti_not, dest, a);
  fixUnderflow(dest);
}

// Execute a bigint rotation.  |source| must be unsigned.
// included, since unsigned integers have 1 extra 0 bit.
static void rotateBigint(runtime_array *dest, runtime_array *source, uint32_t dist, bool rotateLeft) {
  uint32_t width = runtime_bigintWidth(source);
  if (dist > width) {
    runtime_throwExceptionCstr("Rotation by more than the bit width");
  }
  if (runtime_bigintSigned(source)) {
    runtime_throwExceptionCstr("Cannot rotate signed integers");
  }
  if (!rotateLeft) {
    dist = width - dist;
  }
  runtime_array tmp = runtime_makeEmptyArray();
  initBigint(&tmp, width, false, runtime_bigintSecret(source));
  runtime_bigintShl(&tmp, source, dist);
  runtime_bigintShr(dest, source, (width - dist));
  runtime_bigintBitwiseOr(dest, dest, &tmp);
  fixUnderflow(dest);
  runtime_freeArray(&tmp);
}

// Rotate a bigint left by |dist| bits.
void runtime_bigintRotl(runtime_array *dest, runtime_array *source, uint32_t dist) {
  rotateBigint(dest, source, dist, true);
}

// Rotate a bigint left by |dist| bits.
void runtime_bigintRotr(runtime_array *dest, runtime_array *source, uint32_t dist) {
  rotateBigint(dest, source, dist, false);
}


typedef void (*runtime_shiftFunc)(uint32_t *destData, const uint32_t *sourceData, uint32_t dist);

// Shift a bigint left or right by |dist|.
static void shiftBigint(runtime_array *dest, runtime_array *source, uint32_t dist, bool shiftLeft) {
  if (dist > runtime_bigintWidth(source)) {
    runtime_throwExceptionCstr("Tried to shift by the integer width or more");
  }
  initBigint(dest, runtime_bigintWidth(source), runtime_bigintSigned(source),
      runtime_bigintSecret(source));
  const uint32_t *sourceData = getConstBigintData(source);
  uint32_t *destData = getBigintData(dest);
  if (shiftLeft) {
    cti_lsh_trunc(destData + 1, sourceData + 1, dist);
  } else {
    cti_rsh(destData + 1, sourceData + 1, dist);
  }
  fixUnderflow(dest);
}

// Shift a bigint left.
void runtime_bigintShl(runtime_array *dest, runtime_array *source, uint32_t dist) {
  shiftBigint(dest, source, dist, true);
}

// Shift a bigint right.  Unsigned bigints always have a 0 MSB, so they shift
// logically, while signed bigints shift arihmetacally.
void runtime_bigintShr(runtime_array *dest, runtime_array *source, uint32_t dist) {
  shiftBigint(dest, source, dist, false);
}

// Bitwise AND two bigints.
void runtime_bigintBitwiseAnd(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_and, dest, a, b);
}

// Bitwise OR two bigints.
void runtime_bigintBitwiseOr(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_or, dest, a, b);
}

// Bitwise OR two bigints.
void runtime_bigintBitwiseXor(runtime_array *dest, runtime_array *a, runtime_array *b) {
  binaryOperation(cti_xor, dest, a, b);
}

// Constant-time subtract modulus from X if X > modulus.  This is all that is
// required after a modular addition to make the result less than modulus.  This
// takes advantage of the bigint being unsigned and thus having an extra bit.
// After addition, we must subtract the modulus if either value > modulus, or
// value < 0.
static void subtractModulusIfNeeded(runtime_array *value, runtime_array *modulus) {
  uint32_t width = runtime_bigintWidth(modulus);
  runtime_array tmp = runtime_makeEmptyArray();
  initBigint(&tmp, width, false, runtime_bigintSecret(value));
  binaryOperation(cti_sub_trunc, &tmp, value, modulus);
  runtime_bool ctl = runtime_boolToRnBool(runtime_compareBigints(RN_GE, value, modulus));
  ctl = runtime_boolOr(ctl, runtime_bigintNegative(value));
  runtime_bigintCondCopy(ctl, value, &tmp);
  runtime_freeArray(&tmp);
}

// Constant-time add modulus to X if X < 0.
static void addModulusIfNeeded(runtime_array *value, runtime_array *modulus) {
  uint32_t width = runtime_bigintWidth(modulus);
  runtime_array tmp = runtime_makeEmptyArray();
  initBigint(&tmp, width, false, runtime_bigintSecret(value));
  binaryOperation(cti_add_trunc, &tmp, value, modulus);
  runtime_bigintCondCopy(runtime_bigintNegative(value), value, &tmp);
  runtime_freeArray(&tmp);
}

// Constant time modular addition.
void runtime_bigintModularAdd(runtime_array *dest, runtime_array *a, runtime_array *b, runtime_array *modulus) {
  binaryOperation(cti_add_trunc, dest, a, b);
  subtractModulusIfNeeded(dest, modulus);
}

// Constant time modular subtraction.
void runtime_bigintModularSub(runtime_array *dest, runtime_array *a, runtime_array *b, runtime_array *modulus) {
  binaryOperation(cti_sub_trunc, dest, a, b);
  addModulusIfNeeded(dest, modulus);
}

// Do the binary operation with bigints to preserve secrecy.
static uint64_t secretSmallnumBinaryOp(
    void (*func)(runtime_array *dest, runtime_array *a, runtime_array *b), uint64_t a,
    uint64_t b, bool aIsSigned, bool bIsSigned) {
  runtime_array bigA = runtime_makeEmptyArray();
  runtime_integerToBigint(&bigA, a, sizeof(uint64_t)*8, aIsSigned, true);
  runtime_array bigB = runtime_makeEmptyArray();
  runtime_integerToBigint(&bigB, b, sizeof(uint64_t)*8, false, false);
  runtime_array bigResult = runtime_makeEmptyArray();
  initBigint(&bigResult, sizeof(uint64_t)*8, false, true);
  func(&bigResult, &bigA, &bigB);
  uint64_t result = runtime_bigintToInteger(&bigResult);
  runtime_freeArray(&bigA);
  runtime_freeArray(&bigB);
  runtime_freeArray(&bigResult);
  return result;
}

// Perform modular reduction on the small num.  The modulus is unsigned.
uint64_t runtime_smallnumModReduce(uint64_t value, uint64_t modulus, bool isSigned, bool secret) {
  if (secret) {
    return secretSmallnumBinaryOp(runtime_bigintMod, value, modulus, isSigned, false);
  }
  if (!isSigned || (int64_t)value >= 0) {
    return value % modulus;
  }
  // We can't just negate it because it might be SSIZE_MIN.
  value += modulus;
  if (isSigned && (int64_t)value >= 0) {
    return value;
  }
  // Now we can safely negate it.
  value = -value;
  value %= modulus;
  return modulus - value;
}

// Negate the modular bigint.  This is just |modulus| - |a|.
void runtime_bigintModularNegate(runtime_array *dest, runtime_array *a, runtime_array *modulus) {
  runtime_bigintSub(dest, modulus, a);
}

// Negate the modular smallnum.  This is just |modulus| - |a|.
uint64_t runtime_smallnumModularNegate(uint64_t a, uint64_t modulus, bool secret) {
  return modulus - a;
}

// Constant time modular multiplication.
void runtime_bigintModularMul(runtime_array *dest, runtime_array *a, runtime_array *b, runtime_array *modulus) {
  if (runtime_bigintSecret(modulus)) {
    runtime_throwExceptionCstr("Modulus cannot be secret");
  }
  if (runtime_bigintSigned(modulus)) {
    runtime_throwExceptionCstr("Modulus must be unsigned");
  }
  runtime_array bigA = runtime_makeEmptyArray();
  runtime_array bigB = runtime_makeEmptyArray();
  runtime_array result = runtime_makeEmptyArray();
  uint32_t width = runtime_bigintWidth(a);
  uint32_t width2x = width << 1;
  bool isSigned = runtime_bigintSigned(a);
  bool secret = runtime_bigintSecret(a) || runtime_bigintSecret(b);
  initBigint(&bigA, width2x, isSigned, secret);
  initBigint(&bigB, width2x, isSigned, secret);
  initBigint(&result, width2x, isSigned, secret);
  initBigint(dest, width, isSigned, secret);
  cti_set(getBigintData(&bigA) + 1, getBigintData(a) + 1);
  cti_set(getBigintData(&bigB) + 1, getBigintData(b) + 1);
  runtime_bigintMul(&result, &bigA, &bigB);
  cti_set(getBigintData(&bigA) + 1, getBigintData(modulus) + 1);
  runtime_bigintMod(&result, &result, &bigA);
  cti_set(getBigintData(dest) + 1, getBigintData(&result) + 1);
  runtime_freeArray(&bigA);
  runtime_freeArray(&bigB);
  runtime_freeArray(&result);
}

// WARNING: Not constant constant time!
// TODO: Make this constant time.
bool runtime_bigintModularInverse(runtime_array *dest, runtime_array *source, runtime_array *modulus) {
  if (runtime_bigintSecret(modulus)) {
    runtime_throwExceptionCstr("Modulus cannot be secret");
  }
  if (runtime_bigintSigned(modulus) || runtime_bigintSigned(source)) {
    runtime_throwExceptionCstr("Modular values must be unsigned");
  }
  runtime_array signedModulus = runtime_makeEmptyArray();
  runtime_array a = runtime_makeEmptyArray();
  runtime_array b = runtime_makeEmptyArray();
  uint32_t width = runtime_bigintWidth(modulus);
  bool secret = runtime_bigintSecret(source);
  // We need 1 more bit for the sign bit.
  runtime_bigintCast(&signedModulus, modulus, width + 1, true, false, false);
  runtime_bigintCast(&a, source, width + 1, true, false, false);
  runtime_copyBigint(&b, &signedModulus);
  runtime_array x = runtime_makeEmptyArray();
  runtime_array y = runtime_makeEmptyArray();
  runtime_array u = runtime_makeEmptyArray();
  runtime_array v = runtime_makeEmptyArray();
  runtime_array q = runtime_makeEmptyArray();
  runtime_array r = runtime_makeEmptyArray();
  runtime_array n = runtime_makeEmptyArray();
  runtime_array m = runtime_makeEmptyArray();
  runtime_array t = runtime_makeEmptyArray();
  runtime_integerToBigint(&x, 0, width + 1, true, false);
  runtime_integerToBigint(&y, 1, width + 1, true, false);
  runtime_integerToBigint(&u, 1, width + 1, true, false);
  runtime_integerToBigint(&v, 0, width + 1, true, false);
  while (!runtime_rnBoolToBool(runtime_bigintZero(&a))) {
    runtime_bigintDivRem(&q, &r, &b, &a);
    // m = x - u*q
    runtime_bigintMul(&t, &u, &q);
    runtime_bigintSub(&m, &x, &t);
    // n = y - v*q
    runtime_bigintMul(&t, &v, &q);
    runtime_bigintSub(&n, &y, &t);
    runtime_copyBigint(&t, &b);
    runtime_copyBigint(&b, &a);
    runtime_copyBigint(&a, &r);
    runtime_copyBigint(&r, &t);
    runtime_copyBigint(&t, &x);
    runtime_copyBigint(&x, &u);
    runtime_copyBigint(&u, &m);
    runtime_copyBigint(&m, &t);
    runtime_copyBigint(&t, &y);
    runtime_copyBigint(&y, &v);
    runtime_copyBigint(&v, &n);
    runtime_copyBigint(&n, &t);
  }
  if (runtime_rnBoolToBool(runtime_bigintNegative(&x))) {
    runtime_bigintAdd(&x, &x, &signedModulus);
  }
  runtime_bigintCast(dest, &x, width, false, secret, false);
  // If GCD(a, m) != 1, there is no inverse.
  runtime_integerToBigint(&t, 1, width, false, false);
  bool inverseExists = runtime_compareBigints(RN_EQUAL, &b, &t);
  runtime_freeArray(&signedModulus);
  runtime_freeArray(&a);
  runtime_freeArray(&b);
  runtime_freeArray(&x);
  runtime_freeArray(&y);
  runtime_freeArray(&u);
  runtime_freeArray(&v);
  runtime_freeArray(&q);
  runtime_freeArray(&r);
  runtime_freeArray(&n);
  runtime_freeArray(&m);
  runtime_freeArray(&t);
  return inverseExists;
}

// WARNING: Not yet constant time!
// TODO: Make this constant time.
void runtime_bigintModularDiv(runtime_array *dest, runtime_array *a, runtime_array *b, runtime_array *modulus) {
  runtime_array bInverse = runtime_makeEmptyArray();
  initBigint(&bInverse, runtime_bigintWidth(modulus), false, false);
  runtime_bigintModularInverse(&bInverse, b, modulus);
  runtime_bigintModularMul(dest, a, &bInverse, modulus);
  runtime_freeArray(&bInverse);
}

// Convert an integer width in bits to number of CTTK words.
static uint32_t widthToCTTKWords(uint32_t width) {
  return 1 + (width + 30) / 31;
}

// Modular exponentiation.  TODO: speed this up.
void runtime_bigintModularExp(runtime_array *dest, runtime_array *base, runtime_array *exponent, runtime_array *modulus) {
  if (runtime_rnBoolToBool(runtime_bigintNegative(exponent))) {
    runtime_throwExceptionCstr("Tried to exponentiate with negative exponent");
  }
  uint32_t baseWidth = getBigintWidth(getBigintData(modulus));
  uint32_t expWidth = getBigintWidth(getBigintData(exponent));
  uint32_t width2x = baseWidth << 1;
  bool isSecret = runtime_bigintSecret(base) || runtime_bigintSecret(exponent);
  runtime_array modBuf = runtime_makeEmptyArray();
  runtime_array resBuf = runtime_makeEmptyArray();
  runtime_array tOr1 = runtime_makeEmptyArray();
  runtime_array t = runtime_makeEmptyArray();
  initBigint(&modBuf, width2x, false, false);
  initBigint(&resBuf, width2x, false, false);
  initBigint(&tOr1, width2x, false, false);
  initBigint(&t, width2x, false, false);
  // Subtract 1 from baseWidth for sign bit.
  initBigint(dest, baseWidth - 1, false, isSecret);
  uint32_t *baseData = getBigintData(base) + 1;
  uint32_t *expData = getBigintData(exponent) + 1;
  uint32_t *modulusData = getBigintData(modulus) + 1;
  uint32_t *resBufData = getBigintData(&resBuf) + 1;
  uint32_t *modBufData = getBigintData(&modBuf) + 1;
  cti_set(modBufData, modulusData);
  uint32_t bit_pos = 0;
  uint32_t word_index = 1;
  uint32_t word = expData[word_index];
  uint32_t baseNumWords = widthToCTTKWords(baseWidth);
  cti_set_u32(resBufData, 1);
  uint32_t *tOr1Data = getBigintData(&tOr1) + 1;
  uint32_t *tData = getBigintData(&t) + 1;
  cti_set(tData, baseData);
  cti_set_u32(tOr1Data, 0);
  for (uint32_t i = 0; i < expWidth - 1; i++) {
    uint32_t mask = -(word & 1u);
    for (uint32_t j = 1; j < baseNumWords; j++) {
      tOr1Data[j] = tData[j] & mask;
    }
    tOr1Data[1] |= (~mask) & 1;
    cti_mul(resBufData, resBufData, tOr1Data);
    cti_mod(resBufData, resBufData, modBufData);
    cti_mul(tData, tData, tData);
    cti_mod(tData, tData, modBufData);
    word >>= 1;
    bit_pos++;
    if (bit_pos == 31) {
      bit_pos = 0;
      word = expData[++word_index];
    }
  }
  uint32_t *destData = getBigintData(dest) + 1;
  cti_set(destData, resBufData);
  checkForNAN(dest);
  runtime_freeArray(&modBuf);
  runtime_freeArray(&resBuf);
  runtime_freeArray(&tOr1);
  runtime_freeArray(&t);
}

// Perform a smallnum multiplication.
uint64_t runtime_smallnumMul(uint64_t a, uint64_t b, bool isSigned, bool secret) {
  if (secret) {
    if (isSigned) {
      if (runtime_maxNativeIntWidth == 32) {
          return cttk_muls32(a, b);
      }
      return cttk_muls64(a, b);
    }
    if (runtime_maxNativeIntWidth == 32) {
      return cttk_mulu32(a, b);
    }
    return cttk_mulu64(a, b);
  }
  if (isSigned) {
    return (uint64_t)((int64_t)a * (int64_t)b);
  }
  return a*b;
}

// Perform a smallnum division.
uint64_t runtime_smallnumDiv(uint64_t a, uint64_t b, bool isSigned, bool secret) {
  if (secret) {
    return secretSmallnumBinaryOp(runtime_bigintDiv, a, b, isSigned, false);
  }
  if (isSigned) {
    return (uint64_t)((int64_t)a / (int64_t)b);
  }
  return a/b;
}

// Perform a smallnum mod operation.
uint64_t runtime_smallnumMod(uint64_t a, uint64_t b, bool isSigned, bool secret) {
  if (secret) {
    return secretSmallnumBinaryOp(runtime_bigintMod, a, b, isSigned, false);
  }
  if (isSigned) {
    // Always return a non-negative value from 0 to abs(b).
    return (uint64_t)smallNumAbs(a) % (uint64_t)smallNumAbs(b);
  }
  return a/b;
}

// Perform a smallnum exponentiation operation.
uint64_t runtime_smallnumExp(uint64_t base, uint32_t exponent, bool isSigned, bool secret) {
  if (secret) {
    runtime_array bigBase = runtime_makeEmptyArray();
    runtime_integerToBigint(&bigBase, base, sizeof(uint64_t) * 8, isSigned, true);
    runtime_array bigResult = runtime_makeEmptyArray();
    initBigint(&bigResult, sizeof(uint64_t) * 8, false, true);
    runtime_bigintExp(&bigResult, &bigBase, exponent);
    uint64_t result = runtime_bigintToInteger(&bigResult);
    runtime_freeArray(&bigBase);
    runtime_freeArray(&bigResult);
    return result;
  }
  uint64_t result = 1;
  uint64_t t = base;
  while (exponent != 0) {
    if (exponent & 1) {
      result = runtime_smallnumMul(result, t, isSigned, false);
    }
    exponent >>= 1;
    if (exponent != 0) {
      // Be careful not to overflow t with an extra squaring.
      t = runtime_smallnumMul(t, t, isSigned, false);
    }
  }
  return result;
}

// Smallnum modular addition.
uint64_t runtime_smallnumModularAdd(uint64_t a, uint64_t b, uint64_t modulus, bool secret) {
  uint64_t result = a + b;
  if (secret) {
    cttk_bool ctl;
    if (runtime_maxNativeIntWidth == 32) {
      ctl = cttk_or(cttk_s32_lt(result, a), cttk_s32_lt(modulus, result));
    } else {
      ctl = cttk_or(cttk_s64_lt(result, a), cttk_s64_lt(modulus, result));
    }
    return result - (modulus & (int64_t)-cttk_bool_to_int(ctl));
  }
  if (result < a || result > modulus) {
    result -= modulus;
  }
  return result;
}

// Smallnum modular subtraction.
uint64_t runtime_smallnumModularSub(uint64_t a, uint64_t b, uint64_t modulus, bool secret) {
  uint64_t result = a - b;
  if (secret) {
    cttk_bool ctl;
    if (runtime_maxNativeIntWidth == 32) {
      ctl = cttk_s32_lt0(result);
    } else {
      ctl = cttk_s64_lt0(result);
    }
    return result + (modulus & (int64_t)-cttk_bool_to_int(ctl));
  }
  if ((int64_t)result < 0) {
    result += modulus;
  }
  return result;
}

// Do the binary operation with bigints to preserve secrecy.
static uint64_t secretSmallnumModularBinaryOp(
    void (*func)(runtime_array *dest, runtime_array *a, runtime_array *b, runtime_array *modulus),
    uint64_t a, uint64_t b, uint64_t modulus) {
  runtime_array bigA = runtime_makeEmptyArray();
  runtime_integerToBigint(&bigA, a, sizeof(uint64_t)*8, false, true);
  runtime_array bigB = runtime_makeEmptyArray();
  runtime_integerToBigint(&bigB, b, sizeof(uint64_t)*8, false, false);
  runtime_array bigModulus = runtime_makeEmptyArray();
  runtime_integerToBigint(&bigModulus, modulus, sizeof(uint64_t)*8, false, false);
  runtime_array bigResult = runtime_makeEmptyArray();
  initBigint(&bigResult, sizeof(uint64_t)*8, false, true);
  func(&bigResult, &bigA, &bigB, &bigModulus);
  uint64_t result = runtime_bigintToInteger(&bigResult);
  runtime_freeArray(&bigA);
  runtime_freeArray(&bigB);
  runtime_freeArray(&bigModulus);
  runtime_freeArray(&bigResult);
  return result;
}

// TODO: Have the code generator generate this directly when not secret.
uint64_t runtime_smallnumModularMul(uint64_t a, uint64_t b, uint64_t modulus, bool secret) {
  return secretSmallnumModularBinaryOp(runtime_bigintModularMul, a, b, modulus);
}

// TODO: Speed this up for small integers.
uint64_t runtime_smallnumModularDiv(uint64_t a, uint64_t b, uint64_t modulus, bool secret) {
  return secretSmallnumModularBinaryOp(runtime_bigintModularDiv, a, b, modulus);
}

// TODO: Speed this up for small integers.
uint64_t runtime_smallnumModularExp(uint64_t base, uint64_t exponent, uint64_t modulus, bool secret) {
  return secretSmallnumModularBinaryOp(runtime_bigintModularExp, base, exponent, modulus);
}

// Compute the logical AND of two runtime_bools in constant time.
runtime_bool runtime_boolAnd(runtime_bool a, runtime_bool b) {
  return cttk_and(a, b);
}

// Convert a bool to an runtime_bool.
runtime_bool runtime_boolToRnBool(bool a) {
  runtime_bool result;
  result.v = a;
  return result;
}

// Convert an runtime_bool to bool.
bool runtime_rnBoolToBool(runtime_bool a) {
  return a.v != 0;
}

// Compute the logical OR of two runtime_bools in constant time.
runtime_bool runtime_boolOr(runtime_bool a, runtime_bool b) {
  return cttk_or(a, b);
}

// Compute the logical NOT of an runtime_bool in constant time.
runtime_bool runtime_boolNot(runtime_bool a) {
  return cttk_not(a);
}

// Select one of two uint32s in constant time.
uint64_t runtime_selectUint32(runtime_bool select, uint64_t data1, uint64_t data0) {
  return cttk_u32_mux(select, data1, data0);
  return data0 ^ (-select.v & (data1 ^ data0));
}

// Conditionally copy a bigint in constant time.
void runtime_bigintCondCopy(runtime_bool doCopy, runtime_array *dest, const runtime_array *source) {
  if (runtime_bigintWidth(dest) != runtime_bigintWidth(source)) {
    runtime_throwExceptionCstr("Tried to cond-copy to different size bigint");
  }
  uint64_t len = source->numElements*sizeof(uint32_t);
  cttk_cond_copy(doCopy, dest->data, source->data, len);
}

// Compute the quotient and remainder in constant time.
void runtime_bigintDivRem(runtime_array *q, runtime_array *r, runtime_array *a, runtime_array *b) {
  uint32_t width = runtime_bigintWidth(a);
  bool isSigned = runtime_bigintSigned(a);
  bool secret = runtime_bigintSecret(a) || runtime_bigintSecret(b);
  initBigint(q, width, isSigned, secret);
  initBigint(r, width, isSigned, secret);
  uint32_t *qData = getBigintData(q);
  uint32_t *rData = getBigintData(r);
  checkBigintsHaveSameType(a, b);
  const uint32_t *aData = getConstBigintData(a);
  const uint32_t *bData = getConstBigintData(b);
  cti_divrem(qData + 1, rData + 1, aData + 1, bData + 1);
  checkForNAN(q);
  checkForNAN(r);
}

// Generate a true random unsigned bigint.
void runtime_generateTrueRandomBigint(runtime_array *dest, uint32_t width) {
  resizeBigint(dest, width, false);
  uint32_t *data = getBigintData(dest);
  // Fill the digit portion randomly.
  runtime_generateTrueRandomBytes((uint8_t*)(data + 2), (dest->numElements - 2) * sizeof(uint32_t));
  for (uint32_t i = 1; i < dest->numElements - 1; i++) {
    data[i] &= ~RN_NAN_BIT;  // Clear CTTK's high bit.
  }
  // Fixing underflow will clear bits in the high word from the sign bit higher.
  fixUnderflow(dest);
}
