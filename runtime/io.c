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

// I/O functions for the Rune runtime, which read/write to stdin and stdout.
// This is meant to port easily to a microcontroller environment where we might
// have a uart for stdin/stdout.

#include "runtime.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>  // For access to stdin and stdout.
#include <stdlib.h>  // For exit.
#include <unistd.h>  // For getcwd.

// Used in Linux for testing purposes.
#define runtime_setJmp() (runtime_jmpBufSet = true, setjmp(runtime_jmpBuf))
jmp_buf runtime_jmpBuf;
bool runtime_jmpBufSet = false;

// This will exit if runtime_setLongJmp() has not been called.  Otherwise, it will
// long-jump to runtime_jmpBuf.
static void exitOrLongjmp() {
  if (runtime_jmpBufSet) {
    runtime_jmpBufSet = false;
    longjmp(runtime_jmpBuf, 1);
  }
  exit(1);
}

// Get the current working directory.
void io_getcwd(runtime_array *array) {
  char *path = getcwd(NULL, 0);
  size_t len = strlen(path);
  runtime_allocArray(array, len, sizeof(uint8_t), false);
  memcpy(array->data, path, len);
  free(path);
}

// Return one byte from stdin.
uint8_t readByte() {
  return getchar();
}

// Write one character to stdout.
void writeByte(uint8_t c) {
  putchar(c);
}

// Read |numBytes| bytes from stdin.  Block until all are read.
void readBytes(runtime_array *array, uint64_t numBytes) {
  if (array->numElements != 0) {
    runtime_freeArray(array);
  }
  runtime_allocArray(array, numBytes, sizeof(uint8_t), false);
  size_t totalRead = fread(array->data, sizeof(uint8_t), numBytes, stdin);
  while (totalRead < numBytes) {
    totalRead += fread((uint8_t*)array->data + totalRead, sizeof(uint8_t),
        numBytes - totalRead, stdin);
  }
}
// Write |numBytes| bytes from the array to stdout, starting at |offset| in the
// array.  Block until all bytes are written.
void writeBytes(const runtime_array *array, uint64_t numBytes, uint64_t offset) {
  if (numBytes == 0) {
    numBytes = array->numElements;
  }
  uint8_t *p = (uint8_t*)array->data + offset;
  size_t bytesWritten = fwrite(p, sizeof(uint8_t), numBytes, stdout);
  size_t totalWritten = bytesWritten;
  while (totalWritten < numBytes) {
    p += bytesWritten;
    bytesWritten = fwrite(p, sizeof(uint8_t), numBytes - totalWritten, stdout);
    totalWritten += bytesWritten;
  }
}

// Read a line of text from stdin.  Only return up to |maxBytes|.  Do not
// include the '\n' in the returned string.
void readln(runtime_array *array, uint64_t maxBytes) {
  if (array->numElements != 0) {
    runtime_freeArray(array);
  }
  // Allocate array large enough to maybe fit a line, but not too big for a
  // microcontroller.
  uint32_t allocated = 16;
  if (maxBytes == 0) {
    maxBytes = UINT64_MAX;
  }
  if (allocated > maxBytes) {
    allocated = maxBytes;
  }
  runtime_allocArray(array, allocated, sizeof(uint8_t), false);
  uint32_t pos = 0;
  // Avoid using getline, since it calls malloc.
  int c = getchar();
  uint8_t *p = (uint8_t*)array->data;
  while (c != EOF && c != '\n') {
    if (pos == allocated) {
      if (pos == maxBytes) {
        array->numElements = pos;
        return;
      }
      allocated <<= 1;
      if (allocated > maxBytes) {
        allocated = maxBytes;
      }
      runtime_resizeArray(array, allocated, sizeof(uint8_t), false);
      p = (uint8_t*)array->data + pos;
    }
    p[pos++] = c;
    c = getchar();
  }
  array->numElements = pos;
}

// Print a string to stdout, without the \n that puts writes.
void runtime_puts(const runtime_array *string) {
  uint64_t len = string->numElements;
  const char *p = (const char*)string->data;
  while (len-- != 0) {
    // fputc can write \0 and is buffered on Linux, unlike write.
    fputc(*p, stdout);
    p++;
  }
}

// Print a C string string to stdout, without the \n that puts writes.
void runtime_putsCstr(const char *string) {
  const char *p = string;
  char c;
  while ((c = *p++) != '\0') {
    // fputc can write \0 and is buffered on Linux, unlike write.
    fputc(c, stdout);
  }
}

// Throw an exception.  For now, just print the message and exit.
void runtime_throwException(const runtime_array *format, ...) {
  if (runtime_jmpBufSet) {
    printf("Expected ");
  }
  runtime_putsCstr("******************** Exception: ");
  va_list ap;
  va_start(ap, format);
  runtime_array buf = runtime_makeEmptyArray();
  runtime_vsprintf(&buf, format, ap);
  va_end(ap);
  runtime_putsCstr("Exception: ");
  runtime_puts(&buf);
  runtime_putsCstr("\n");
  runtime_freeArray(&buf);
  exitOrLongjmp();
}

// Throw an exception from C.  For now, just print the message and exit.
void runtime_throwExceptionCstr(const char *format, ...) {
  if (runtime_jmpBufSet) {
    printf("Expected ");
  }
  va_list ap;
  va_start(ap, format);
  char buf[RN_MAX_CSTRING];
  vsnprintf(buf, RN_MAX_CSTRING, format, ap);
  va_end(ap);
  runtime_putsCstr("Exception: ");
  runtime_putsCstr(buf);
  runtime_putsCstr("\n");
  exitOrLongjmp();
}

// Throw an overflow exception.
void runtime_throwOverflow(void) {
  runtime_putsCstr("Exception: overflow\n");
  exitOrLongjmp();
}

// Print an error message and exit.
void runtime_panic(const runtime_array *format, ...) {
  va_list ap;
  va_start(ap, format);
  runtime_array buf = runtime_makeEmptyArray();
  runtime_vsprintf(&buf, format, ap);
  va_end(ap);
  runtime_putsCstr("Fatal error: ");
  runtime_puts(&buf);
  runtime_putsCstr("\n");
  fflush(stdout);
  runtime_freeArray(&buf);
#ifdef RN_DEBUG
  // Generate a core file.
  uint8_t *p = NULL;
  *p = 1;
#endif
  exitOrLongjmp();
}

// Print an error message and exit.
void runtime_panicCstr(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  printf("Fatal error: ");
  vprintf(format, ap);
  printf("\n");
  va_end(ap);
  fflush(stdout);
#ifdef RN_DEBUG
  // Generate a core file.
  uint8_t *p = NULL;
  *p = 1;
#endif
  exitOrLongjmp();
}

// Convert a hex digit to a 4-but uint8_t.  Needs to be constant time!
static inline uint8_t fromHex(uint8_t c) {
  return ((uint8_t)9 & -(c >> 6)) + (c & (uint8_t)0xf);
}

// Convert a 4-bit value to a hex digit.  Needs to be constant time!
static inline uint8_t toHex(uint8_t value) {
  uint8_t bit1 = value >> 1;
  uint8_t bit2 = value >> 2;
  uint8_t bit3 = value >> 3;
  uint8_t delta = 'a' - '0' - 10;
  return ((-((bit1 | bit2) & bit3)) & delta) + "0"[0] + value;
}

// Read a uint32 from the string.  Update the string pointer to point to first
// non-digit.  Given an error if the value does not fit in a uint32.
static uint32_t readUint32(const uint8_t **p) {
  const uint8_t *q = *p;
  uint64_t value = 0;
  uint8_t c = *q;
  if (!isdigit(c)) {
    runtime_panicCstr("Width must follow %i or %u specifier");
  }
  do {
    value = value * 10 + c - '0';
    if (value > UINT32_MAX) {
      runtime_panicCstr("Integer width cannot exceed 2^16 - 1");
    }
    c = *++q;
  } while (isdigit(c));
  *p = q;
  return value;
}

// Append a C string to a string array.
static inline void appendArrayCstr(runtime_array *array, const char *string) {
  uint64_t len = strlen(string);
  uint8_t *p = (uint8_t*)string;
  for (uint64_t i = 0; i < len; i++) {
    runtime_appendArrayElement(array, p, sizeof(uint8_t), false, false);
    p++;
  }
}

typedef enum {
  kDoubleNormal,
  kDoubleZero,
  kDoubleInf,
  kDoubleNan
} runtime_doubleType;

// Explode a double into its three parts: a negative flag, exponent and fraction.
static runtime_doubleType explodeDouble(double val, bool *negative, int32_t *exponent, uint64_t *fraction) {
  uint64_t v = *(uint64_t*)&val;
  *fraction = v & (((uint64_t)1 << 52) - 1);
  v >>= 52;
  uint32_t biasedExponent = v & (((uint64_t)1 << 11) - 1);
  *negative = v >> 11;
  *exponent = 0;
  if (biasedExponent == 0) {
    if (*fraction == 0) {
      return kDoubleZero;
    }
    // Subnormals case.
  } else if (biasedExponent == 0x7ff) {
    if (fraction == 0) {
      return kDoubleInf;
    }
    return kDoubleNan;
  } else {
    // This makes fraction a 53-bit value.
    *fraction |= (uint64_t)1 << 52;
  }
  *exponent = biasedExponent - 1023 + 1;
  return kDoubleNormal;
}

// Multiply or divide by powers of 10 until val >= 1.0 and < 10.0.  Return the
// base 10 exponent needed to do this.  |val| should already be positive.
static double normalizeBase10(double val, int32_t *base10Exponent) {
  // TODO: Speed this up.
  int32_t exp = 0;
  while (val < 1.0) {
    val *= 10.0;
    exp--;
  }
  while (val >= 10.0) {
    val /= 10.0;
    exp++;
  }
  *base10Exponent = exp;
  return val;
}

// Write the double value in ASCII to the buffer.  Write in scientific notation.
// See IEEE-754 for double precision encoding.
static void doubleToString(runtime_array *array, double val) {
  runtime_freeArray(array);
  bool negative;
  int32_t exponent;  // Base 2 exponent.
  uint64_t fraction;  // 53 bit fraction.  Value is 0.<53 binary fraction bits>
  runtime_doubleType type = explodeDouble(val, &negative, &exponent, &fraction);
  switch(type) {
    case kDoubleZero:
      appendArrayCstr(array, "0.0");
      return;
    case kDoubleInf:
      appendArrayCstr(array, "Inf");
      return;
    case kDoubleNan:
      appendArrayCstr(array, "NaN");
      return;
    case kDoubleNormal:
      break;
  }
  if (negative) {
    val = -val;
  }
  if (negative) {
    appendArrayCstr(array, "-");
  }
  int32_t base10Exponent;
  val = normalizeBase10(val, &base10Exponent);
  // Round based on the number of digits printed.
  char fractionDigits[] = "000000";
  uint64_t pow10 = 1;
  for (uint32_t i = 0; i < sizeof(fractionDigits) - 1; i++) {
    pow10 *= 10;
  }
  val += 0.5/pow10;
  if (val >= 10.0) {
    val /= 10.0;
    base10Exponent += 1;
  }
  // Print the leading digit before the decimal point.
  char digit[] = "d.";
  digit[0] = '0' + (uint32_t)val;
  appendArrayCstr(array, digit);
  val -= (uint32_t)val;  // Now only fractional digits remain.
  explodeDouble(val, &negative, &exponent, &fraction);
  // Adjust fraction to be fixed-point with the fixed point at position 53.
  fraction >>= -exponent;
  uint64_t mask = ((uint64_t)1 << 53) - 1;
  for (uint32_t i = 0; i < sizeof(fractionDigits) - 1; i++) {
    fraction *= 10;
    fractionDigits[i] = '0' + (fraction >> 53);
    fraction &= mask;
  }
  // Strip trailing 0's.
  uint32_t lastDigitIndex = sizeof(fractionDigits) - 2;
  while (lastDigitIndex != 0 && fractionDigits[lastDigitIndex] == '0') {
    fractionDigits[lastDigitIndex--] = '\0';
  }
  appendArrayCstr(array, fractionDigits);
  if (base10Exponent != 0) {
    appendArrayCstr(array, "e");
    runtime_array buf = runtime_makeEmptyArray();
    runtime_nativeIntToString(&buf, base10Exponent, 10, true);
    runtime_concatArrays(array, &buf, sizeof(uint8_t), false);
    runtime_freeArray(&buf);
  }
}

// Forward declaration for recursion.
static const uint8_t *appendFormattedElement(runtime_array *array, bool topLevel,
    const uint8_t *p, va_list ap);

// Varargs wrapper for appendFormattedElement.
static const uint8_t *appendFormattedArg(runtime_array *array, bool topLevel, const uint8_t *p, ...) {
  va_list ap;
  va_start(ap, p);
  const uint8_t *result = appendFormattedElement(array, topLevel, p, ap);
  va_end(ap);
  return result;
}

// Skip to the matching ] in the spec.  For example, given "[u32]] %d", return
// pointer to " %d".  The opening [ has already been skipped.
static const uint8_t *skipArrayElementSpec(const uint8_t *p) {
  uint32_t depth = 0;
  uint8_t c = *p++;
  while (c != ']' || depth != 0) {
    if (c == '[') {
      depth++;
    } else if (c == ']') {
      depth--;
    }
    c = *p++;
  }
  return p;
}

// Forward declaration for recursion.
static const uint8_t *findEndOfSpec(const uint8_t *p, uint32_t *elementSize, uint32_t *width,
    bool *deref);

// Update the tuple size to take into account the new element.
static uint32_t alignToElement(uint32_t elementPos, uint32_t subElementSize) {
  uint32_t alignment = sizeof(uint64_t);
  if (subElementSize  == 1) {
    alignment = 1;
  } else if (subElementSize == 2) {
    alignment = 2;
  } else if (subElementSize <= 4) {
    alignment = 4;
  }
  if (elementPos & (alignment - 1)) {
    // Move elementPos to the next aligned position.
    elementPos &= ~(alignment - 1);
    elementPos += alignment;
  }
  return elementPos;
}

// Skip to the matching ) in the spec.  For example, given "((u32,b)s) %d",
// return pointer to " %d".  The opening [ has already been skipped.  Compute
// the element size as we go.  Tuples are packed such that each element is
// aligned to the nearest power of 2, up to sizeof(uint64_t).
static const uint8_t *skipTupleElementSpec(const uint8_t *p, uint32_t *tupleSize) {
  uint32_t elementPos = 0;
  uint8_t c;
  do {
    uint32_t elementSize, width;
    bool deref;
    p = findEndOfSpec(p, &elementSize, &width, &deref);
    elementPos = alignToElement(elementPos, elementSize) + elementSize;
    c = *p++;
    if (c != ',' && c != ')') {
      runtime_panicCstr("Unexpected character in tuple spec: %c", c);
    }
  } while (c != ')');
  *tupleSize = elementPos;
  return p;
}

// Find the end of the format element spec, and set |elementSize| to the element
// size in memory.
static const uint8_t *findEndOfSpec(const uint8_t *p, uint32_t *elementSize, uint32_t *width,
    bool *deref) {
  uint8_t c = *p++;
  *deref = false;
  *width = 0;
  if (c == 's') {
    *elementSize = sizeof(runtime_array);
  } else if (c == 'i' || c == 'u' || c == 'x') {
    *width = readUint32(&p);
    if (*width > sizeof(uint64_t) * 8) {
      *elementSize = sizeof(runtime_array);
      return p;
    } else {
      *deref = true;
    }
    // Integers in arrays are rounded up to a power of 2 size.
    if (*width <= 8) {
      *elementSize = 1;
    } else if (*width <= 16) {
      *elementSize = 2;
    } else if (*width <= 32) {
      *elementSize = 4;
    } else if (*width <= 64) {
      *elementSize = 8;
    }
  } else if (c == 'f') {
    uint32_t width = readUint32(&p);
    if (width != 32 && width != 64) {
      runtime_panicCstr("Illegal floating point width %u", width);
    }
    *elementSize = width >> 3;
    *deref = true;
  } else if (c == 'b') {
    *elementSize = 1;
    *deref = true;
  } else if (c == '[') {
    *elementSize = sizeof(runtime_array);
    p = skipArrayElementSpec(p);
  } else if (c == '(') {
    p = skipTupleElementSpec(p, elementSize);
  } else if (c == ')') {
    // This is an empty tuple.
    *elementSize = 0;
    p--;
  } else {
    runtime_panicCstr("Unsupported format specifier: %c", c);
  }
  return p;
}

// Dereference a pointer to the element, and append it to |dest|, formatted
// according to the spec pointed to by |p|.
static void derefAndAppendFormattedArg(runtime_array *dest, bool topLevel, const uint8_t *p,
    const uint8_t *elementPtr, uint32_t elementSize, uint32_t width) {
  // Assumes compiler aligns stack elements on 32-bit boundaries or courser.
  switch (elementSize) {
    case 1: {
      uint8_t value = *(uint8_t *)elementPtr;
      appendFormattedArg(dest, topLevel, p, value);
      break;
    }
    case 2: {
      uint16_t value = *(uint16_t *)elementPtr;
      appendFormattedArg(dest, topLevel, p, value);
      break;
    }
    case 4: {
      uint32_t value = *(uint32_t *)elementPtr;
      appendFormattedArg(dest, topLevel, p, value);
      break;
    }
    case 8: {
      uint64_t value = *(uint64_t *)elementPtr;
      appendFormattedArg(dest, topLevel, p, value);
      break;
    }
    default:
      runtime_panicCstr("Unexpected element width");
  }
}

// Print an array to a string.  |p| points the element type specifier.  Return
// a pointer to the character just past the end of the array format specifier.
static const uint8_t *printArray(runtime_array *dest, const uint8_t *p, const runtime_array *source) {
  appendArrayCstr(dest, "[");
  uint32_t elementSize, width;
  bool deref;
  const uint8_t *elementSpecEnd = findEndOfSpec(p, &elementSize, &width, &deref);
  if (*elementSpecEnd != ']') {
    runtime_panicCstr("Expected ] at end of array format specifier");
  }
  elementSpecEnd++;
  // Don't compute elementPtr here, since the heap can be resized or compacted
  // in the loop.
  uint64_t elementIndex = 0;
  bool firstTime = true;
  for (uint64_t i = 0; i < source->numElements; i++) {
    if (!firstTime) {
      appendArrayCstr(dest, ", ");
    }
    firstTime = false;
    const uint8_t *elementPtr = (const uint8_t*)(source->data) + elementIndex;
    if (!deref) {
      appendFormattedArg(dest, false, p, elementPtr);
    } else {
      derefAndAppendFormattedArg(dest, false, p, elementPtr, elementSize, width);
    }
    elementIndex += elementSize;
  }
  appendArrayCstr(dest, "]");
  return elementSpecEnd;
}

// Print a tuple to a string.  |p| points the element type specifier.  Return a
// pointer to the character just past the end of the tuple format specifier.
static const uint8_t *printTuple(runtime_array *dest, const uint8_t *p, const uint8_t *tuple) {
  appendArrayCstr(dest, "(");
  uint32_t elementPos = 0;
  uint8_t c = *p;
  bool firstTime = true;
  while (c != ')') {
    if (!firstTime) {
      appendArrayCstr(dest, ", ");
    }
    firstTime = false;
    uint32_t elementSize, width;
    bool deref;
    const uint8_t *specEnd = findEndOfSpec(p, &elementSize, &width, &deref);
    elementPos = alignToElement(elementPos, elementSize);
    if (!deref) {
      appendFormattedArg(dest, false, p, tuple + elementPos);
    } else {
      derefAndAppendFormattedArg(dest, false, p, tuple + elementPos, elementSize, width);
    }
    elementPos += elementSize;
    c = *specEnd;
    if (c == ',') {
      c = *++specEnd;
    } else if (c != ')') {
      runtime_panicCstr("Unexpected character in tuple spec: %c", c);
    }
    p = specEnd;
  }
  appendArrayCstr(dest, ")");
  return p + 1;
}

// The LLVM compiler sometimes passes garbage in the upper bits for an integer
// that is not a normal size of 8, 16, 32, or 64 bits.  Either zero-extend
// or sign extend based on whether the value is signed.
static uint64_t extendToUpperBits(uint64_t value, bool isSigned, uint32_t width) {
  if (width == 64) {
    return value;
  }
  if (!isSigned || ((value >> (width - 1) & 1) == 0)) {
    return value & (((uint64_t)1 << width) - 1);
  }
  return value | ~(((uint64_t)1 << width) - 1);
}

// Append a formatted element to the string array.  The first character is the
// format specifier, e.g. s for string.  Consume the entire format specifier and
// return a pointer to the character after the specifier.
static const uint8_t *appendFormattedElement(runtime_array *array, bool topLevel,
    const uint8_t *p, va_list ap) {
  uint8_t c = *p++;
  if (c == 's') {
    uint8_t quote = '"';
    runtime_array *string = va_arg(ap, runtime_array *);
    if (!topLevel) {
      runtime_appendArrayElement(array, &quote, sizeof(uint8_t), false, false);
    }
    runtime_concatArrays(array, string, sizeof(uint8_t), false);
    if (!topLevel) {
      runtime_appendArrayElement(array, &quote, sizeof(uint8_t), false, false);
    }
  } else if (c == 'i' || c == 'u' || c == 'x') {
    uint8_t *typeStart = (uint8_t*)(p - 1);
    uint32_t width = readUint32(&p);
    // We can't print secrets, so this is a bigint based on size.
    if (width > sizeof(uint64_t) * 8) {
      runtime_array buf = runtime_makeEmptyArray();
      runtime_array *bigint = va_arg(ap, runtime_array *);
      runtime_bigintToString(&buf, bigint, c == 'x' ? 16 : 10);
      runtime_concatArrays(array, &buf, sizeof(uint8_t), false);
      runtime_freeArray(&buf);
    } else {
      bool isSigned = c == 'i';
      runtime_array buf = runtime_makeEmptyArray();
      uint64_t value;
      if (width > sizeof(uint32_t) * 8) {
        value = va_arg(ap, uint64_t);
      } else {
        value = va_arg(ap, uint32_t);
      }
      value = extendToUpperBits(value, c == 'i', width);
      if (width < sizeof(uint64_t) * 8 && isSigned &&
          ((value & ((uint64_t)1 << (width - 1))) != 0)) {
        // Sign extend.
        value |= ((uint64_t)-1) << width;
      }
      runtime_nativeIntToString(&buf, value, c == 'x' ? 16 : 10, isSigned);
      runtime_concatArrays(array, &buf, sizeof(uint8_t), false);
      runtime_freeArray(&buf);
    }
    if (!topLevel) {
      while (typeStart < p) {
        runtime_appendArrayElement(array, typeStart++, sizeof(uint8_t), false, false);
      }
    }
  } else if (c == 'f') {
    uint8_t *typeStart = (uint8_t*)(p - 1);
    uint32_t width = readUint32(&p);
    double value = 0.0;
    if (width == 32) {
      if (topLevel) {
        // Note that clang's warning here is wrong for Rune, but right for C/C++.
        // Suppress the warning.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvarargs"
        value = va_arg(ap, float);
#pragma clang diagnostic pop
      } else {
        // We pass floats as uint32_t when called from appendFormattedArg.
        uint32_t floatVal = va_arg(ap, uint32_t);
        value = *(float*)&floatVal;
      }
    } else if (width == 64) {
      if (topLevel) {
        value = va_arg(ap, double);
      } else {
        // We pass doubles as uint64_t when called from appendFormattedArg.
        uint64_t floatVal = va_arg(ap, uint64_t);
        value = *(double*)&floatVal;
      }
    } else {
      runtime_throwExceptionCstr("Unsupported floating point width: %u", width);
    }
    runtime_array buf = runtime_makeEmptyArray();
    doubleToString(&buf, value);
    runtime_concatArrays(array, &buf, sizeof(uint8_t), false);
    runtime_freeArray(&buf);
    if (!topLevel) {
      while (typeStart < p) {
        runtime_appendArrayElement(array, typeStart++, sizeof(uint8_t), false, false);
      }
    }
  } else if (c == 'b') {
    bool boolVal = va_arg(ap, int);
    char *boolText = boolVal ? "true" : "false";
    uint64_t numElements = boolVal ? 4 : 5;
    runtime_array string = {(uint64_t *)boolText, numElements};
    runtime_concatArrays(array, &string, sizeof(uint8_t), false);
  } else if (c == '[') {
    runtime_array buf = runtime_makeEmptyArray();
    runtime_array *arrayArg = va_arg(ap, runtime_array *);
    p = printArray(&buf, p, arrayArg);
    runtime_concatArrays(array, &buf, sizeof(uint8_t), false);
    runtime_freeArray(&buf);
  } else if (c == '(') {
    runtime_array buf = runtime_makeEmptyArray();
    uint8_t *tuplePtr = va_arg(ap, uint8_t *);
    p = printTuple(&buf, p, tuplePtr);
    runtime_concatArrays(array, &buf, sizeof(uint8_t), false);
    runtime_freeArray(&buf);
  } else {
    runtime_panicCstr("Unsupported format specifier: %c", c);
  }
  return p;
}

// Like sprintf, but use format specifiers specific to Rune types.
// Currently, we support:
//
//   %b        - Match an bool value: prints true or false
//   %i<width> - Match an Int value
//   %u<width> - Match a Uint value
//   %f<width> - Match a float or double.
//   %s        - Match a string value
//   %x<width> - Match an Int or Uint value, print in lower-case-hex.
//   %[<spec>] - Match a list of the spec type, eg %[u32]
//   %(<spec>, ...) - Match a tuple of the spec type, eg %(s, u32)
//
// Escapes can be \" \\ \n, \t, or \xx, where xx is a hex encoding of the byte.
//
// TODO: Add support for format modifiers, e.g. %12s, %-12s, %$1d, %8d, %08u...
void runtime_vsprintf(runtime_array *array, const runtime_array *format, va_list ap) {
  if (array->numElements != 0) {
    runtime_freeArray(array);
  }
  const uint8_t *p = (const uint8_t*)format->data;
  const uint8_t *end = p + format->numElements;
  while (p != end) {
    uint8_t c = *p++;
    if (c == '\\') {
      c = *p++;
      if (c == 'x') {
        uint8_t upper = *p++;
        uint8_t lower = *p++;
        if (!isxdigit(upper) || !isxdigit(lower)) {
          runtime_panicCstr("Invalid hex escape: should have 2 hex digits");
        }
        c = (fromHex(upper) << 4) | fromHex(lower);
      } else if (c == 'n') {
        c = '\n';
      } else if (c == 't') {
        c = '\t';
      } else if (c == 'a') {
        c = 7;
      } else if (c == 'b') {
        c = 8;
      } else if (c == 'e') {
        c = 0x1b;
      } else if (c == 'f') {
        c = 0xc;
      } else if (c == 'r') {
        c = 0xd;
      } else if (c == 'v') {
        c = 0xb;
      }
      runtime_appendArrayElement(array, &c, sizeof(uint8_t), false, false);
    } else if (c == '%') {
      p = appendFormattedElement(array, true, p, ap);
    } else {
      runtime_appendArrayElement(array, &c, sizeof(uint8_t), false, false);
    }
  }
}

// Like sprintf, but with Rune's data types.
void runtime_sprintf(runtime_array *array, const runtime_array *format, ...) {
  va_list ap;
  va_start(ap, format);
  runtime_vsprintf(array, format, ap);
  va_end(ap);
}

// Like sprintf, but with Rune's data types.
void runtime_printf(const char *format, ...) {
  runtime_array array = runtime_makeEmptyArray();
  runtime_array formatArray = runtime_makeEmptyArray();
  // Build it like a constant array, outside of the heap.
  formatArray.data = (uint64_t*)format;
  formatArray.numElements = strlen(format);
  va_list ap;
  va_start(ap, format);
  runtime_vsprintf(&array, &formatArray, ap);
  va_end(ap);
  runtime_puts(&array);
  fflush(stdout);
  runtime_freeArray(&array);
  runtime_freeArray(&formatArray);
}

// Convert an integer to a string.
void runtime_nativeIntToString(runtime_array *string, uint64_t value, uint32_t base, bool isSigned) {
  runtime_freeArray(string);
  bool negated = false;
  if (isSigned && (int64_t)value < 0) {
    negated = true;
    value = -value;
  }
  if (value == 0) {
    uint8_t c = '0';
    runtime_appendArrayElement(string, &c, sizeof(uint8_t), false, false);
    return;
  }
  while (value != 0) {
    uint64_t digit = value % base;
    value /= base;
    uint8_t c;
    if (digit > 9) {
      c = 'a' + digit - 10;
    } else {
      c = '0' + digit;
    }
    runtime_appendArrayElement(string, &c, sizeof(uint8_t), false, false);
  }
  if (negated) {
    uint8_t c = '-';
    runtime_appendArrayElement(string, &c, sizeof(uint8_t), false, false);
  }
  runtime_reverseArray(string, sizeof(uint8_t), false);
}

// Convert a bigint to ASCII, using the base.
//
// def toDecimal(value, base):
//   negative = value < 0
//   while value != 0:
//     q = value / base
//     r = value % base
//     if negative and r > 0:
//       r -= base
//       q += 1
//     print r,
//     value = q
//   print
// 
void runtime_bigintToString(runtime_array *string, runtime_array *bigint, uint32_t base) {
  runtime_freeArray(string);
  if (runtime_rnBoolToBool(runtime_bigintZero(bigint))) {
    uint8_t c = '0';
    runtime_appendArrayElement(string, &c, sizeof(uint8_t), false, false);
    return;
  }
  bool negative = false;
  if (runtime_rnBoolToBool(runtime_bigintNegative(bigint))) {
    negative = true;
  }
  runtime_array q = runtime_makeEmptyArray();
  runtime_array r = runtime_makeEmptyArray();
  runtime_array b = runtime_makeEmptyArray();
  runtime_copyBigint(&q, bigint);
  runtime_integerToBigint(&b, base, runtime_bigintWidth(bigint), runtime_bigintSigned(bigint), false);
  while (!runtime_rnBoolToBool(runtime_bigintZero(&q))) {
    runtime_bigintDivRem(&q, &r, &q, &b);
    if (negative) {
      runtime_bigintNegate(&r, &r);
      if (runtime_rnBoolToBool(runtime_bigintNegative(&r))) {
        runtime_throwExceptionCstr("Expected negative remainder");
      }
    }
    uint32_t digit = runtime_bigintToU32(&r);
    uint8_t c;
    if (digit > 9) {
      c = 'a' + digit - 10;
    } else {
      c = '0' + digit;
    }
    runtime_appendArrayElement(string, &c, sizeof(uint8_t), false, false);
  }
  if (negative) {
    uint8_t c = '-';
    runtime_appendArrayElement(string, &c, sizeof(uint8_t), false, false);
  }
  runtime_freeArray(&b);
  runtime_freeArray(&r);
  runtime_freeArray(&q);
  runtime_reverseArray(string, sizeof(uint8_t), false);
}

// Return a char* pointer to the string data.
static inline char *getStringData(const runtime_array *string) {
  return (char*)(string->data);
}

// For debugging.  Do not use in secure code!  Will print secrets.
void runtime_printBigint(runtime_array *val) {
  runtime_array string = runtime_makeEmptyArray();
  runtime_bigintToString(&string, val, 10);
  printf("%s", getStringData(&string));
  runtime_freeArray(&string);
  fflush(stdout);
}

// For debugging.
void runtime_printHexBigint(runtime_array *val) {
  runtime_array string = runtime_makeEmptyArray();
  runtime_bigintToString(&string, val, 16);
  printf("%s", getStringData(&string));
  runtime_freeArray(&string);
  fflush(stdout);
}

// Convert a binary string to a hexadecimal string.
void runtime_stringToHex(runtime_array *destHexString, const runtime_array *sourceBinString) {
  uint64_t numElements = sourceBinString->numElements;
  runtime_resizeArray(destHexString, numElements << 1, sizeof(uint8_t), false);
  uint8_t *p = (uint8_t*)(destHexString->data);
  uint8_t *q = (uint8_t*)(sourceBinString->data);
  for (uint64_t i = 0; i < numElements; i++) {
    uint8_t c = *q++;
    *p++ = toHex(c >> 4);
    *p++ = toHex(c & 0xf);
  }
}

// Convert a hex string to a binary string.  It is an error for there to be an
// odd number of digits.
void runtime_hexToString(runtime_array *destBinString, const runtime_array *sourceHexString) {
  uint64_t numElements = sourceHexString->numElements;
  if (numElements & 1) {
    runtime_freeArray(destBinString);
    runtime_throwExceptionCstr("Invalid hex string: should have even number of hex digits");
  }
  runtime_resizeArray(destBinString, numElements >> 1, sizeof(uint8_t), false);
  uint8_t *p = (uint8_t*)(destBinString->data);
  uint8_t *q = (uint8_t*)(sourceHexString->data);
  for (uint64_t i = 0; i < numElements >> 1; i++) {
    uint8_t upper = *q++;
    if (!isxdigit(upper)) {
      runtime_panicCstr("Invalid hex digit: %c", upper);
    }
    uint8_t lower = *q++;
    if (!isxdigit(lower)) {
      runtime_throwExceptionCstr("Invalid hex digit: %c", lower);
    }
    if (!isxdigit(upper) || !isxdigit(lower)) {
      runtime_throwExceptionCstr("Invalid hex digit: ");
    }
    *p++ = (fromHex(upper) << 4) | fromHex(lower);
  }
}

// Find a sub-string in a string, starting at the offset.  Return the length of
// the string if |needle| is not found in |haystack|.
uint64_t runtime_stringFind(const runtime_array *haystack, const runtime_array *needle, uint64_t offset) {
  uint64_t length = haystack->numElements;
  uint64_t needleLength = needle->numElements;
  if (needleLength > length || offset > length - needleLength) {
    return length;
  }
  const char *p = (const char*)haystack->data + offset;
  const char *q = (const char*)needle->data;
  for (uint64_t i = offset; i < length; i++) {
    if (*p++ == *q) {
      const char *r = p;
      const char *s = q + 1;
      uint64_t j;
      for (j = 1; j < needleLength && *r++ == *s++; j++);
      if (j == needleLength) {
        return i;
      }
    }
  }
  return length;
}

// Reverse-find a sub-string in a string, starting at the offset.  Return the
// length of the string if |needle| is not found in |haystack|.
uint64_t runtime_stringRfind(const runtime_array *haystack, const runtime_array *needle, uint64_t offset) {
  uint64_t length = haystack->numElements;
  uint64_t needleLength = needle->numElements;
  if (needleLength > length || offset > length - needleLength) {
    return length;
  }
  const char *p = (const char*)haystack->data + length - needleLength;
  const char *q = (const char*)needle->data;
  for (uint64_t i = offset; i < length; i++) {
    if (*p-- == *q) {
      const char *r = p + 2;
      const char *s = q + 1;
      uint64_t j;
      for (j = 1; j < needleLength && *r++ == *s++; j++);
      if (j == needleLength) {
        return length - needleLength - (i - offset);
      }
    }
  }
  return length;
}
