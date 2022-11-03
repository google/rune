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

#include "de.h"

#include <ctype.h>
#include <gmp.h>

// Write a uint16 to a byte buffer, little-endian.
static inline void writeUint16LE(uint8* buf, uint16 value) {
  buf[0] = value;
  buf[1] = value >> 8;
}

// Write a uint32 to a byte buffer, little-endian.
static inline void writeUint32LE(uint8* buf, uint32 value) {
  buf[0] = value;
  buf[1] = value >> 8;
  buf[2] = value >> 16;
  buf[3] = value >> 24;
}

// Write a uint64 to a byte buffer, little-endian.
static inline void writeUint64LE(uint8* buf, uint64 value) {
  buf[0] = value;
  buf[1] = value >> 8;
  buf[2] = value >> 16;
  buf[3] = value >> 24;
  buf[4] = value >> 32;
  buf[5] = value >> 40;
  buf[6] = value >> 48;
  buf[7] = value >> 56;
}

// Create a bigint.
static deBigint bigintCreate(bool isSigned, uint32 width) {
  deBigint bigint = deBigintAlloc();
  deBigintSetSigned(bigint, isSigned);
  deBigintSetWidth(bigint, width);
  uint32 len = deBitsToBytes(width);
  deBigintAllocDatas(bigint, len);
  return bigint;
}

// Create a 8-bit unsigned bigint.
deBigint deUint8BigintCreate(uint8 value) {
  deBigint bigint = bigintCreate(false, 8);
  deBigintSetiData(bigint, 0, value);
  return bigint;
}

// Create a 8-bit unsigned bigint.
deBigint deInt8BigintCreate(int8 value) {
  deBigint bigint = deUint8BigintCreate((uint8)value);
  deBigintSetSigned(bigint, true);
  return bigint;
}

// Create a 16-bit unsigned bigint.
deBigint deUint16BigintCreate(uint16 value) {
  deBigint bigint = bigintCreate(false, 16);
  writeUint16LE(deBigintGetDatas(bigint), value);
  return bigint;
}

// Create a 16-bit unsigned bigint.
deBigint deInt16BigintCreate(int16 value) {
  deBigint bigint = deUint16BigintCreate((uint16)value);
  deBigintSetSigned(bigint, true);
  return bigint;
}

// Create a 32-bit unsigned bigint.
deBigint deUint32BigintCreate(uint32 value) {
  deBigint bigint = bigintCreate(false, 32);
  writeUint32LE(deBigintGetDatas(bigint), value);
  return bigint;
}

// Create a 32-bit unsigned bigint.
deBigint deInt32BigintCreate(int32 value) {
  deBigint bigint = deUint32BigintCreate((uint32)value);
  deBigintSetSigned(bigint, true);
  return bigint;
}

// Create a 64-bit unsigned bigint.
deBigint deUint64BigintCreate(uint64 value) {
  deBigint bigint = bigintCreate(false, 64);
  writeUint64LE(deBigintGetDatas(bigint), value);
  return bigint;
}

// Create a 64-bit unsigned bigint.
deBigint deInt64BigintCreate(int64 value) {
  deBigint bigint = deUint64BigintCreate((uint64)value);
  deBigintSetSigned(bigint, true);
  return bigint;
}

// Create a uint64-sized unsigned bigint.
deBigint deNativeUintBigintCreate(uint64 value) {
  deBigint bigint = bigintCreate(false, 64);
  writeUint64LE(deBigintGetDatas(bigint), value);
  return bigint;
}


// Create a 0 valued bigint.
deBigint deZeroBigintCreate(bool isSigned, uint32 width) {
  return bigintCreate(isSigned, width);
}

// Dump the bigint to the file.
void deWriteBigint(FILE* file, deBigint bigint) {
  uint8* data = deBigintGetData(bigint);
  uint32 width = deBigintGetWidth(bigint);
  bool isSigned = deBigintSigned(bigint);
  uint32 len = deBitsToBytes(width);
  int32 i = len - 1;
  // Skip leading 0's
  while (i > 0 && data[i] == 0) {
    --i;
  }
  if (i == 0 && data[i] < 16) {
    fprintf(file, "%u", *data);
  } else {
    fprintf(file, "0x");
    fprintf(file, "%x", data[i]);
    for (--i; i >= 0; --i) {
      fprintf(file, "%02x", data[i]);
    }
  }
  if (!isSigned) {
    fprintf(file, "u%u", width);
  } else {
    fprintf(file, "i%u", width);
  }
}

// Dump the bigint to the file in decimal, with no trailing u32, etc.  A temp
// buffer is returned.
char *deBigintToString(deBigint bigint, uint32 base) {
  uint8* data = deBigintGetData(bigint);
  uint32 width = deBigintGetWidth(bigint);
  uint32 len = deBitsToBytes(width);
  mpz_t val;
  mpz_init(val);
  bool negative = deBigintSigned(bigint) && deBigintNegative(bigint);
  if (negative) {
    deBigint negBigint = deBigintNegate(bigint);
    data = deBigintGetData(negBigint);
    mpz_import(val, len, -1, 1, 0, 0, data);
    mpz_neg(val, val);
    deBigintDestroy(negBigint);
  } else {
    mpz_import(val, len, -1, 1, 0, 0, data);
  }
  uint64 bytes = mpz_sizeinbase(val, base) + 2;
  char *buf = utMakeString(bytes);
  mpz_get_str(buf, base, val);
  mpz_clear(val);
  return buf;
}

// Dump the bigint to stdout for debugging.
void deDumpBigint(deBigint bigint) {
  deWriteBigint(stdout, bigint);
  fflush(stdout);
}

// Parse a uint32.
static uint32 parseUint32(char* p, deLine line) {
  uint32 val = 0;
  uint32 oldVal = 0;
  while (*p != '\0') {
    if (!isdigit(*p)) {
      deError(line, "Failed to parse integer size");
    }
    oldVal = val;
    val *= 10;
    val += *p++ - '0';
    if (val < oldVal) {
      deError(line, "Overflow while reading u32");
    }
  }
  return val;
}

// Parse a big integer.
deBigint deBigintParse(char* text, deLine line) {
  char* buf = utCopyString(text);
  char* p = strchr(buf, 'u');
  bool isSigned = false;
  if (p == NULL) {
    p = strchr(buf, 'i');
    if (p != NULL) {
      isSigned = true;
    }
  }
  uint32 width = 64;
  bool widthUnspecified = true;
  if (p != NULL) {
    *p++ = '\0';
    width = parseUint32(p, line);
    widthUnspecified = false;
  }
  mpz_t val;
  mpz_init(val);
  mpz_set_str(val, buf, 0);
  uint64 bits = mpz_sizeinbase(val, 2);
  if (bits > width) {
    deError(line, "Integer too large to fit in declared width");
  }
  deBigint bigint = bigintCreate(isSigned, width);
  deBigintSetWidthUnspecified(bigint, widthUnspecified);
  uint8* data = deBigintGetData(bigint);
  mpz_export(data, NULL, -1, 1, 0, 0, val);
  mpz_clear(val);
  return bigint;
}

// Compute a 32-bit hash of the bigint.
uint32 deHashBigint(deBigint bigint) {
  uint32 hash = utHashData(deBigintGetData(bigint), deBigintGetNumData(bigint));
  hash = utHashValues(hash, deBigintGetWidth(bigint));
  return utHashValues(hash, deBigintSigned(bigint));
}

// Determine if two bigints are equal.  They are considered not equal if they
// have different types, not just values.
bool deBigintsEqual(deBigint bigint1, deBigint bigint2) {
  if (deBigintGetWidth(bigint1) != deBigintGetWidth(bigint2)) {
    return false;
  }
  if (memcmp(deBigintGetData(bigint1), deBigintGetData(bigint2),
             deBigintGetNumData(bigint1) * sizeof(deBigint))) {
    return false;
  }
  return deBigintSigned(bigint1) == deBigintSigned(bigint2);
}

// Get the value of a bigint as a uint64.  If it is bigger than 64-bits can
// hold, report an error at |line|.  The returned value ca be cast to int64
// if the bigint is signed.
static uint64 bigintGetUint64(deBigint bigint, deLine line) {
  uint8* data = deBigintGetData(bigint);
  uint32 width = deBigintGetWidth(bigint);
  uint32 len = deBitsToBytes(width);
  bool isSigned = deBigintSigned(bigint);
  int32 i = len - 1;
  // Skip leading 1's or 0's
  while (i > 0 && (data[i] == 0 || (isSigned && data[i] == 0xff))) {
    i--;
  }
  if (i >= sizeof(uint64) || (i == sizeof(int64) && isSigned && i < len - 1 &&
                              data[i] >> 7 != (data[i + 1] & 1))) {
    deError(line, "Integer too large");
  }
  uint64 value = 0;
  if (i + 1 < sizeof(uint64) && deBigintNegative(bigint)) {
    value = -1;
  }
  for (; i >= 0; --i) {
    value = value << 8 | data[i];
  }
  return value;
}

// Return the uint32 represented by the bigint constant.  Report an error if the
// integer is too big.
uint32 deBigintGetUint32(deBigint bigint, deLine line) {
  if (deBigintSigned(bigint)) {
    deError(line, "Expected unsigned integer");
  }
  uint64 val = bigintGetUint64(bigint, line);
  if ((uint32) val != val) {
    deError(line, "Integer too large");
  }
  return val;
}

// Return the int32 represented by the bigint constant.  Report an error if the
// integer is too big.
int32 deBigintGetInt32(deBigint bigint, deLine line) {
  if (!deBigintSigned(bigint)) {
    deError(line, "Expected unsigned integer");
  }
  uint64 val = bigintGetUint64(bigint, line);
  if ((int32)val != (int64)val) {
    deError(line, "Integer too large");
  }
  return val;
}

// Return the uint64 represented by the bigint constant.  Report an error if the
// integer is too big.
uint64 deBigintGetUint64(deBigint bigint, deLine line) {
  if (deBigintSigned(bigint)) {
    deError(line, "Expected unsigned integer");
  }
  return bigintGetUint64(bigint, line);
}

// Return the int32 represented by the 'igint constant.  Report an error if the
// integer is too big.
int64 deBigintGetInt64(deBigint bigint, deLine line) {
  if (!deBigintSigned(bigint)) {
    deError(line, "Expected unsigned integer");
  }
  return bigintGetUint64(bigint, line);
}

// Make a copy of a bigint.
deBigint deCopyBigint(deBigint bigint) {
  deBigint newBigint = bigintCreate(deBigintSigned(bigint), deBigintGetWidth(bigint));
  deBigintSetData(newBigint, deBigintGetData(bigint), deBigintGetNumData(bigint));
  return newBigint;
}

// Return true if the bigint is negative.
bool deBigintNegative(deBigint a) {
  if (!deBigintSigned(a)) {
    return false;
  }
  uint8 highByte = deBigintGetiData(a, deBigintGetNumData(a) - 1);
  return highByte >> 7 == 1;
}

// Add two bigints.
deBigint deBigintAdd(deBigint a, deBigint b) {
  bool isSigned = deBigintSigned(a);
  uint32  width = deBigintGetWidth(a);
  if (deBigintGetWidth(b) != width || deBigintSigned(b) != isSigned) {
    deError(0, "Bigint widths not the same");
  }
  deBigint result = bigintCreate(deBigintSigned(a), width);
  uint16 carry = 0;
  for (uint32 i = 0; i < deBigintGetNumData(a); i++) {
    uint16 sum = (uint16)deBigintGetiData(a, i) + (uint16)deBigintGetiData(b, i) + carry;
    carry = sum >> 8;
    deBigintSetiData(result, i, (uint8)sum);
  }
  if (!deBigintNegative(a) && !deBigintNegative(b)) {
    if (carry != 0) {
      deError(0, "Bigint overflow");
    }
  } else if (deBigintNegative(a) && deBigintNegative(b)) {
    if (!deBigintNegative(result)) {
      deError(0, "Bigint overflow");
    }
  }
  return result;
}

// Subtract two bigints.
deBigint deBigintSub(deBigint a, deBigint b) {
  bool isSigned = deBigintSigned(a);
  uint32  width = deBigintGetWidth(a);
  if (deBigintGetWidth(b) != width || deBigintSigned(b) != isSigned) {
    deError(0, "Bigint widths not the same");
  }
  if (deBigintSigned(b) != isSigned) {
    deError(0, "Mixing signed and unsigned bigints");
  }
  deBigint result = bigintCreate(isSigned, width);
  uint16 carry = 1;
  for (uint32 i = 0; i < deBigintGetNumData(a); i++) {
    // The double-cast (uint16)(uint8) is required because ~ extends to uint32
    // first, and winds up setting all the upper bits.  The first (uint8) cast
    // zeros those bits.
    uint16 sum = (uint16)deBigintGetiData(a, i) + (uint16)(uint8)~deBigintGetiData(b, i) + carry;
    carry = sum >> 8;
    deBigintSetiData(result, i, (uint8)sum);
  }
  if (isSigned) {
    bool aPositive = !deBigintNegative(a);
    bool bPositive = !deBigintNegative(b);
    bool resultPositive = !deBigintNegative(result);
    if ((aPositive && bPositive && !resultPositive) ||
        (!aPositive && !bPositive && resultPositive)) {
      deError(0, "Bigint overflow");
    }
  } else {
    if (!carry) {
      deError(0, "Bigint overflow");
    }
  }
  return result;
}

// Negate a bigint.
deBigint deBigintNegate(deBigint a) {
  uint32  width = deBigintGetWidth(a);
  deBigint result = bigintCreate(deBigintSigned(a), width);
  uint16 carry = 1;
  for (uint32 i = 0; i < deBigintGetNumData(a); i++) {
    // The double-cast (uint16)(uint8) is required because ~ extends to uint32
    // first, and winds up setting all the upper bits.  The first (uint8) cast
    // zeros those bits.
    uint16 sum = (uint16)(uint8)~deBigintGetiData(a, i) + carry;
    carry = sum >> 8;
    deBigintSetiData(result, i, (uint8)sum);
  }
  return result;
}

// Reduce the value by the modulus.  This converts value to the range [0,
// modulus).  The result is the type of the modulus.
deBigint deBigintModularReduce(deBigint a, deBigint modulus) {
  utAssert(!deBigintNegative(modulus));
  uint32  modulusWidth = deBigintGetWidth(modulus);
  uint32  aWidth = deBigintGetWidth(a);
  mpz_t aVal, modulusVal, resVal;
  mpz_init(aVal);
  mpz_init(modulusVal);
  mpz_init(resVal);
  mpz_import(aVal, deBitsToBytes(aWidth), -1, 1, 0, 0, deBigintGetData(a));
  mpz_import(modulusVal, deBitsToBytes(modulusWidth), -1, 1, 0, 0, deBigintGetData(modulus));
  mpz_mod(resVal, aVal, modulusVal);
  mpz_clear(aVal);
  mpz_clear(modulusVal);
  deBigint result = bigintCreate(false, modulusWidth);
  mpz_export(deBigintGetData(result), NULL, -1, 1, 0, 0, resVal);
  mpz_clear(resVal);
  return result;
}

// Make a new bigint with the new size.  If truncation changes the value, report an error.
deBigint deBigintResize(deBigint bigint, uint32 width, deLine line) {
  deBigint result = bigintCreate(deBigintSigned(bigint), width);
  uint32 oldWidth = deBigintGetWidth(bigint);
  uint32 oldBytes = deBigintGetNumData(bigint);
  uint32 newBytes = deBigintGetNumData(result);
  if (width >= oldWidth) {
    memcpy(deBigintGetData(result), deBigintGetData(bigint), oldBytes);
    if (deBigintNegative(bigint)) {
      memset(deBigintGetData(result) + oldBytes, 0xff, newBytes - oldBytes);
    }
  } else {
    // Check for truncation.
    uint8 val = deBigintNegative(bigint)? 0xff : 0;
    for (uint32 i = newBytes; i < oldBytes; i++) {
      if (deBigintGetiData(bigint, i) != val) {
        deError(line, "Truncation of integer loses significant bits");
      }
    }
    deBigintSetData(result, deBigintGetData(bigint), newBytes);
  }
  return result;
}
