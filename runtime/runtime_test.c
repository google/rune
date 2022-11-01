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

#include "runtime.h"

#include <mcheck.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>

#define RN_HEAP_SIZE (1u << 15)

// Test creating and destroying 1D arrays over and over.  This should not
// cause any heap to be used.
static void testAllocFree(void) {
  for (uint32_t i = 0; i < 128; i++) {
    runtime_array array = runtime_makeEmptyArray();
    runtime_allocArray(&array, 1024, 8, false);
    runtime_freeArray(&array);
  }
  printf("Passed create/destroy loop test\n");
}

// Test creating a new array then freeing the old one.  This forces the heap to
// be compacted.
static void testAllocAllocFree(void) {
  runtime_array array1 = runtime_makeEmptyArray();
  runtime_array array2 = runtime_makeEmptyArray();
  runtime_array arrays[64];
  runtime_allocArray(&array1, 1024, 8, false);
  for (uint32_t i = 0; i < 64; i++) {
    arrays[i] = runtime_makeEmptyArray();
    runtime_allocArray(arrays + i, 17, 5, false);
    runtime_allocArray(&array2, 1024, 8, false);
    runtime_freeArray(&array1);
    runtime_allocArray(&array1, 1024, 8, false);
    runtime_freeArray(&array2);
  }
  runtime_freeArray(&array1);
  for (uint32_t i = 0; i < 64; i++) {
    runtime_freeArray(arrays + i);
  }
}

// Test that moving arrays works.
static void testMoveArray(void) {
  runtime_array a = runtime_makeEmptyArray();
  const char data[] = "password";
  runtime_allocArray(&a, sizeof(data), 1, false);
  memcpy(a.data, data, sizeof(data));
  runtime_array b = runtime_makeEmptyArray();
  runtime_array c = runtime_makeEmptyArray();
  runtime_moveArray(&b, &a);
  assert(a.numElements == 0 && b.numElements == sizeof(data));
  assert(a.data == NULL && b.data != NULL);
  runtime_moveArray(&c, &b);
  assert(b.numElements == 0 && c.numElements == sizeof(data));
  assert(!memcmp(c.data, data, sizeof(data)));
  assert(runtime_getArrayHeader(&c)->backPointer == &c);
  runtime_freeArray(&a);
  runtime_freeArray(&b);
  runtime_freeArray(&c);
}

// Test runtime_reverseArray.
static void testReverseArray(void) {
  runtime_array a = runtime_makeEmptyArray();
  const char data[] = "password";
  runtime_allocArray(&a, strlen(data), 1, false);
  memcpy(a.data, data, strlen(data));
  runtime_reverseArray(&a, sizeof(uint8_t), false);
  assert(!memcmp(a.data, "drowssap", strlen(data)));
  runtime_freeArray(&a);
}

// Test runtime_comparArrays works on simple arrays.
static void testCompareArrays(void) {
  runtime_array a = runtime_makeEmptyArray();
  runtime_array b = runtime_makeEmptyArray();
  const char dataA[] = "a";
  const char dataB[] = "aa";
  runtime_allocArray(&a, strlen(dataA), 1, false);
  runtime_allocArray(&b, strlen(dataB), 1, false);
  memcpy(a.data, dataA, sizeof(dataA));
  memcpy(b.data, dataB, sizeof(dataB));
  assert(runtime_compareArrays(RN_LT, RN_UINT, &a, &b, 1, false, false));
  assert(runtime_compareArrays(RN_LE, RN_UINT, &a, &b, 1, false, false));
  assert(!runtime_compareArrays(RN_EQUAL, RN_UINT, &a, &b, 1, false, false));
  assert(runtime_compareArrays(RN_NOTEQUAL, RN_UINT, &a, &b, 1, false, false));
  runtime_array c = runtime_makeEmptyArray();
  runtime_array d = runtime_makeEmptyArray();
  const char dataC[] = "ab";
  const char dataD[] = "ac";
  runtime_allocArray(&c, strlen(dataC), 1, false);
  runtime_allocArray(&d, strlen(dataD), 1, false);
  memcpy(c.data, dataC, sizeof(dataC));
  memcpy(d.data, dataD, sizeof(dataD));
  assert(runtime_compareArrays(RN_LT, RN_UINT, &c, &d, 1, false, false));
  assert(runtime_compareArrays(RN_LE, RN_UINT, &c, &d, 1, false, false));
  assert(!runtime_compareArrays(RN_EQUAL, RN_UINT, &c, &d, 1, false, false));
  assert(runtime_compareArrays(RN_NOTEQUAL, RN_UINT, &c, &d, 1, false, false));
  runtime_freeArray(&a);
  runtime_freeArray(&b);
  runtime_freeArray(&c);
  runtime_freeArray(&d);
}

// Test converting a uint64_t integer to/from a bigint.
static void testIntegerConversion(void) {
  uint64_t value = 0xbadc0ffee0ddf00dLL;
  runtime_array array = runtime_makeEmptyArray();
  runtime_integerToBigint(&array, value, 65, true, false);
  assert(runtime_bigintToInteger(&array) == value);
  runtime_freeArray(&array);
}

// Test runtime_bigintEncodeLittleEndian and runtime_bigintDecodeLittleEndian.
static void testEncodeDecode(void) {
  runtime_array byteArray = runtime_makeEmptyArray();
  const char message[] = "This is a test.";
  runtime_arrayInitCstr(&byteArray, message);
  runtime_array bigintArray = runtime_makeEmptyArray();
  runtime_bigintDecodeLittleEndian(&bigintArray, &byteArray, byteArray.numElements*8, false, true);
  runtime_array resultArray = runtime_makeEmptyArray();
  runtime_bigintEncodeLittleEndian(&resultArray, &bigintArray);
  assert(runtime_compareArrays(RN_EQUAL, RN_UINT, &byteArray, &resultArray,
      sizeof(uint8_t), false, false));
  runtime_freeArray(&byteArray);
  runtime_freeArray(&bigintArray);
  runtime_freeArray(&resultArray);
}

// Test bigint comparison.
static void testCompareBigints(void) {
  runtime_array a = runtime_makeEmptyArray();
  runtime_array b = runtime_makeEmptyArray();
  runtime_integerToBigint(&a, 0x12345678, 73, false, false);
  runtime_integerToBigint(&b, 0x87654321, 73, false, false);
  assert(runtime_compareBigints(RN_LT, &a, &b));
  assert(runtime_compareBigints(RN_LE, &a, &b));
  assert(!runtime_compareBigints(RN_EQUAL, &a, &b));
  assert(runtime_compareBigints(RN_NOTEQUAL, &a, &b));
  runtime_array c = runtime_makeEmptyArray();
  runtime_array d = runtime_makeEmptyArray();
  runtime_integerToBigint(&c, -0x12345678, 73, true, false);
  runtime_integerToBigint(&d, 0x87654321, 73, true, false);
  assert(runtime_compareBigints(RN_LT, &c, &d));
  assert(runtime_compareBigints(RN_LE, &c, &d));
  assert(!runtime_compareBigints(RN_EQUAL, &c, &d));
  assert(runtime_compareBigints(RN_NOTEQUAL, &c, &d));
  runtime_freeArray(&a);
  runtime_freeArray(&b);
  runtime_freeArray(&c);
  runtime_freeArray(&d);
}

// Test runtime_bigintCast.
static void testBigintCast(void) {
  runtime_array a = runtime_makeEmptyArray();
  runtime_integerToBigint(&a, 0xdeadbeef, 128, false, true);
  runtime_array b = runtime_makeEmptyArray();
  runtime_bigintCast(&b, &a, 32, false, true, false);
  assert(runtime_bigintWidth(&b) == 32);
  assert(runtime_bigintToInteger(&b) == 0xdeadbeef);
  if (!runtime_setJmp()) {
    runtime_bigintCast(&b, &a, 31, false, true, false);
    assert(false);
  }
  runtime_bigintCast(&b, &a, 31, false, true, true);
  assert(runtime_bigintWidth(&b) == 31);
  assert(runtime_bigintToInteger(&b) == 0x5eadbeef);
  runtime_freeArray(&a);
  runtime_freeArray(&b);
}

// Test shifting.
static void testBigintShifts(void) {
  runtime_array a = runtime_makeEmptyArray();
  runtime_integerToBigint(&a, 0xffffffffdeadbeefll, 32, true, false);
  runtime_array b = runtime_makeEmptyArray();
  runtime_bigintShr(&b, &a, 16);
  assert(runtime_bigintWidth(&b) == 32);
  assert(runtime_bigintToInteger(&b) == 0xffffffffffffdeadll);
}

// Test dynamic arrays.
static void testDynamicArrays(void) {
  testAllocFree();
  testAllocAllocFree();
  testMoveArray();
  testReverseArray();
  testCompareArrays();
}

// Test the exponentiate function.
static void testBigintExponentiate(void) {
  runtime_array value = runtime_makeEmptyArray();
  runtime_integerToBigint(&value, 0xdeadbeef, 65, false, false);
  runtime_bigintExp(&value, &value, 2);
  assert(runtime_bigintToInteger(&value) == 0xc1b1cd12216da321LL);
  runtime_freeArray(&value);
}

// Test modular addition.
static void testBigintModularAdd(void) {
  runtime_array modulus = runtime_makeEmptyArray();
  runtime_integerToBigint(&modulus, 7, 3, false, false);
  runtime_array value = runtime_makeEmptyArray();
  runtime_integerToBigint(&value, 6, 3, false, true);
  runtime_bigintModularAdd(&value, &value, &value, &modulus);
  assert(runtime_bigintToInteger(&value) == 5);
  runtime_freeArray(&value);
  runtime_freeArray(&modulus);
}

// Test modular subtraction.
static void testBigintModularSub(void) {
  runtime_array modulus = runtime_makeEmptyArray();
  runtime_integerToBigint(&modulus, 7, 3, false, false);
  runtime_array value1 = runtime_makeEmptyArray();
  runtime_integerToBigint(&value1, 4, 3, false, true);
  runtime_array value2 = runtime_makeEmptyArray();
  runtime_integerToBigint(&value2, 6, 3, false, true);
  runtime_bigintModularSub(&value1, &value1, &value2, &modulus);
  assert(runtime_bigintToInteger(&value1) == 5);
  runtime_freeArray(&value1);
  runtime_freeArray(&value2);
  runtime_freeArray(&modulus);
}

// Test modular multiplication.
static void testBigintModularMul(void) {
  runtime_array modulus = runtime_makeEmptyArray();
  runtime_integerToBigint(&modulus, 13, 4, false, false);
  runtime_array value = runtime_makeEmptyArray();
  runtime_integerToBigint(&value, 5, 4, false, true);
  runtime_bigintModularMul(&value, &value, &value, &modulus);
  // i == 5 mod 13.
  assert(runtime_bigintToInteger(&value) == 12);
  runtime_freeArray(&value);
  runtime_freeArray(&modulus);
}

// Test modular inverse.
static void testBigintModularInverse(void) {
  runtime_array modulus = runtime_makeEmptyArray();
  runtime_integerToBigint(&modulus, 13, 4, false, false);
  runtime_array value = runtime_makeEmptyArray();
  runtime_integerToBigint(&value, 5, 4, false, true);
  runtime_array inverse = runtime_makeEmptyArray();
  runtime_integerToBigint(&inverse, 0, 4, false, true);
  runtime_bigintModularInverse(&inverse, &value, &modulus);
  runtime_bigintModularMul(&value, &value, &inverse, &modulus);
  assert(runtime_bigintToInteger(&value) == 1);
  runtime_freeArray(&value);
  runtime_freeArray(&inverse);
  runtime_freeArray(&modulus);
}

// Test modular multiplication.
static void testBigintModularDiv(void) {
  runtime_array modulus = runtime_makeEmptyArray();
  runtime_array numerator = runtime_makeEmptyArray();
  runtime_array denominator = runtime_makeEmptyArray();
  runtime_array result = runtime_makeEmptyArray();
  runtime_integerToBigint(&modulus, 13, 4, false, false);
  runtime_integerToBigint(&numerator, 12, 4, false, false);
  runtime_integerToBigint(&denominator, 5, 4, false, false);
  runtime_integerToBigint(&result, 0, 4, false, false);
  runtime_bigintModularDiv(&result, &numerator, &denominator, &modulus);
  assert(runtime_bigintToInteger(&result) == 5);
  runtime_freeArray(&modulus);
  runtime_freeArray(&numerator);
  runtime_freeArray(&denominator);
  runtime_freeArray(&result);
}

// Initialize an array to the prime 2^255 - 19.
static void initBigintTo25519(runtime_array *dest) {
  runtime_array big2 = runtime_makeEmptyArray();
  // Compute prime 2^255 - 19.
  runtime_integerToBigint(&big2, 2, 256, false, false);
  runtime_bigintExp(&big2, &big2, 255);
  runtime_array const19 = runtime_makeEmptyArray();
  runtime_integerToBigint(&const19, 19, 256, false, false);
  runtime_bigintSub(&big2, &big2, &const19);
  runtime_bigintCast(dest, &big2, 255, false, false, false);
  runtime_freeArray(&big2);
  runtime_freeArray(&const19);
}

// Test modular exponentiation.
static void testBigintModularExp(void) {
  runtime_array modulus = runtime_makeEmptyArray();
  initBigintTo25519(&modulus);
  runtime_array g = runtime_makeEmptyArray();
  runtime_integerToBigint(&g, 12345, 255, false, false);
  runtime_array res = runtime_makeEmptyArray();
  runtime_integerToBigint(&res, 0, 255, false, false);
  // A number raised to a prime modulus is just itself.
  runtime_bigintModularExp(&res, &g, &modulus, &modulus);
  assert(runtime_compareBigints(RN_EQUAL, &res, &g));
  runtime_freeArray(&modulus);
  runtime_freeArray(&g);
  runtime_freeArray(&res);
}

// Test the Bigint API.
static void testBigints(void) {
  testIntegerConversion();
  testEncodeDecode();
  testCompareBigints();
  testBigintCast();
  testBigintShifts();
  testBigintExponentiate();
  testBigintModularAdd();
  testBigintModularSub();
  testBigintModularMul();
  testBigintModularInverse();
  testBigintModularDiv();
  testBigintModularExp();
}

// Test the Smallnum API.
static void testSmallnums(void) {
  int64_t minVal = (int64_t)1 << ((sizeof(int64_t)*8) - 1);
  assert(runtime_smallnumModReduce(minVal, 13, true, false) == 5);
  assert(runtime_smallnumModularMul(3, 4, 7, true) == 5);
}

struct testTupleStruct {
  uint8_t a;
  uint8_t padding[7];
  uint64_t b;
  runtime_array array;
};

// Test runtime_sprintf.
static void testSprintf(void) {
  runtime_array buf = runtime_makeEmptyArray();
  runtime_array format = runtime_makeEmptyArray();
  runtime_arrayInitCstr(&format, "%u8");
  runtime_sprintf(&buf, &format, (uint8_t)137);
  assert(buf.numElements == 3 && !memcmp(buf.data, "137", 3));
  runtime_arrayInitCstr(&format, "%[u32]\n");
  runtime_array list = runtime_makeEmptyArray();
  for (uint32_t i = 1; i <= 10; i++) {
    runtime_appendArrayElement(&list, (uint8_t*)&i, sizeof(uint32_t), false, false);
  }
  runtime_sprintf(&buf, &format, &list);
  runtime_puts(&buf);
  char expectedArray[] = "[1u32, 2u32, 3u32, 4u32, 5u32, 6u32, 7u32, 8u32, 9u32, 10u32]\n";
  uint64_t len = sizeof(expectedArray) - 1;
  assert(buf.numElements == len && !memcmp(buf.data, expectedArray, len));
  assert(sizeof(struct testTupleStruct) == 16 + sizeof(runtime_array));
  struct testTupleStruct tuple;
  tuple.a = 137;
  tuple.b = 123456789012345678llu;
  tuple.array = runtime_makeEmptyArray();
  runtime_copyArray(&tuple.array, &list, sizeof(uint32_t), false);
  runtime_arrayInitCstr(&format, "%(u8,u64,[u32])\n");
  runtime_sprintf(&buf, &format, &tuple);
  runtime_puts(&buf);
  char expectedArray2[] = "(137u8, 123456789012345678u64, [1u32, 2u32, 3u32, 4u32, 5u32, 6u32, "
      "7u32, 8u32, 9u32, 10u32])\n";
  len = sizeof(expectedArray2) - 1;
  assert(buf.numElements == len && !memcmp(buf.data, expectedArray2, len));
  runtime_freeArray(&tuple.array);
  runtime_freeArray(&buf);
  runtime_freeArray(&format);
  runtime_freeArray(&list);
}

// Test the runtime_initArrayOfStringsFromC function.
static void testInitArrayOfStringFromC(void) {
  char *argvC[] = {"one", "two", "three"};
  runtime_array argv = runtime_makeEmptyArray();
  runtime_initArrayOfStringsFromC(&argv, (const uint8_t**)argvC, 3);
  runtime_array buf = runtime_makeEmptyArray();
  runtime_array format = runtime_makeEmptyArray();
  runtime_arrayInitCstr(&format, "%[s]\n");
  runtime_sprintf(&buf, &format, &argv);
  char expectedArray[] = "[\"one\", \"two\", \"three\"]\n";
  uint64_t len = sizeof(expectedArray) - 1;
  assert(buf.numElements == len && !memcmp(buf.data, expectedArray, len));
  runtime_freeArray(&buf);
  runtime_freeArray(&argv);
  runtime_freeArray(&format);
}

// Text runtime_xorStrings.
static void testXorStrings(void) {
  runtime_array a = runtime_makeEmptyArray();
  runtime_array b = runtime_makeEmptyArray();
  runtime_array c = runtime_makeEmptyArray();
  runtime_arrayInitCstr(&a, "aaa");
  runtime_arrayInitCstr(&b, "bbb");
  runtime_xorStrings(&c, &a, &b);
  assert(!memcmp(c.data, "\x03\x03\x03", 3));
  runtime_freeArray(&a);
  runtime_freeArray(&b);
  runtime_freeArray(&c);
}

int main(int argc, char **argv) {
  mcheck(NULL);
  runtime_arrayStart();
  testDynamicArrays();
  testBigints();
  testSmallnums();
  testSprintf();
  testInitArrayOfStringFromC();
  testXorStrings();
  runtime_arrayStop();
  printf("passed\n");
}
