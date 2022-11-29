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

#include <assert.h>
#include <stdio.h>
#include <sys/types.h>
#include <stdlib.h>  // For calloc, realloc, and free.
#ifdef _WIN32
#include <windows.h>  // To find total RAM available.
#else
#include <sys/sysinfo.h>  // To find total RAM available.
#endif

// These are verified with static_assert in runtime_arrayStart.
#ifdef RN_DEBUG
#ifdef _WIN32
#define RN_HEADER_WORDS 4u
#else
#define RN_HEADER_WORDS 3u
#endif
// Used when initializing array headers to help track down heap bugs.
static size_t runtime_arrayCounter = 0;
#else
#define RN_HEADER_WORDS 2u
#endif
#define RN_ARRAY_WORDS 2u

static size_t runtime_totalRam;

#ifdef RN_DEBUG

// Verify the back pointers in the sub-array, and any sub-arrays.
static void verifySubArray(const runtime_array *array) {
  size_t numElements = array->numElements;
  if (numElements == 0) {
    return;
  }
  runtime_heapHeader *header = runtime_getArrayHeader(array);
  size_t *data = array->data;
  if (header->hasSubArrays) {
    runtime_array *childArray = (runtime_array*)data;
    while (numElements-- != 0) {
      verifySubArray(childArray);
      childArray++;
    }
  }
  if (header->backPointer != array) {
    runtime_panicCstr("Array back-pointer does not point to the array at %lx", (uintptr_t)array);
  }
}

// Verify the back pointers in the array, and any sub-arrays.
static void verifyArray(const runtime_array *array) {
  size_t numElements = array->numElements;
  if (numElements == 0) {
    if (array->data != NULL) {
      runtime_panicCstr("Empty array has non-null data pointer at %lx", (uintptr_t)array);
    }
    return;
  }
  size_t *data = array->data;
  // Constant arrays can exist outside that range.
  runtime_heapHeader *header = runtime_getArrayHeader(array);
  if (header->hasSubArrays) {
    runtime_array *childArray = (runtime_array *)data;
    while (numElements-- != 0) {
      verifySubArray(childArray);
      childArray++;
    }
  }
  if (header->backPointer != array) {
    runtime_panicCstr("Array back-pointer does not point to the array at %lx, runtime_arrayCounter = %lu",
        (uintptr_t)array, header->counter);
  }
}
#endif

// Copy memory by bytes. |src| and |dest| may overlap, as long as |src| < |dest|.
void runtime_memcopy(void *dest, const void *source, size_t len) {
  if ((len & RN_SIZET_MASK) == 0 && ((uintptr_t)dest & RN_SIZET_MASK) == 0) {
    runtime_copyWords(dest, (size_t*)source, len >> RN_SIZET_SHIFT);
    return;
  }
  const uint8_t* aPtr = source;
  uint8_t* bPtr = dest;
  while (len) {
    if (((intptr_t)aPtr & 1) || ((intptr_t)bPtr & 1) || (1 == len)) {
      // Either pointer has an odd address, or there is only 1 byte left:
      // copy 1 byte at a time.
      *bPtr++ = *aPtr++;
      len--;
    } else if (((intptr_t)aPtr & 2) || ((intptr_t)bPtr & 2) || (len < 4)) {
      // We have at least 2 bytes left. Either pointer has a word-aligned
      // address, or we have less than 4 bytes left:
      // copy 2 bytes at a time
      *((uint16_t*)bPtr) = *((uint16_t*)aPtr);
      aPtr += 2;
      bPtr += 2;
      len -= 2;
    } else {
      // We have at least 4 bytes left, and both pointers are 32-bit aligned:
      // copy 4 bytes at a time.
      *((uint32_t *)bPtr) = *((uint32_t *)aPtr);
      aPtr += 4;
      bPtr += 4;
      len -= 4;
    }
  }
}

// Set the array back-pointer to point to the array.
static inline void updateArrayBackPointer(runtime_array *array) {
  size_t *data = array->data;
  if (data == NULL) {
    return;
  }
  runtime_heapHeader *header = runtime_getArrayHeader(array);
  header->backPointer = array;
#ifdef RN_DEBUG
  header->counter = ++runtime_arrayCounter;
#endif
}

// External API to updateArrayBackPointer.
void runtime_updateArrayBackPointer(runtime_array *array) {
  updateArrayBackPointer(array);
}

// Update the back-pointers of the sub-arrays.
static void updateSubArrayBackPointers(runtime_array* array) {
  runtime_array *subArray = (runtime_array *)array->data;
  size_t numElements = array->numElements;
  while (numElements-- != 0) {
    updateArrayBackPointer(subArray);
    subArray++;
  }
}

// Check that the requested words is not out of range.
static inline bool isOutOfRange(size_t numWords) {
  return numWords > runtime_totalRam >> RN_SIZET_SHIFT;
}

// Allocate data on the heap for array elements.
static size_t *allocArrayBuffer(size_t numWords, bool hasSubArrays) {
  if (numWords == 0) {
    return NULL;
  }
  if (isOutOfRange(numWords)) {
    runtime_throwExceptionCstr("Out of memory");
  }
  // We need space for the header.
  // Allocate data with calloc.
  runtime_heapHeader *header = (runtime_heapHeader*)calloc(numWords + RN_HEADER_WORDS, sizeof(size_t));
  size_t *data = ((size_t*)header) + RN_HEADER_WORDS;
  header->allocatedWords = numWords;
  header->hasSubArrays = hasSubArrays;
  return data;
}

// Allocate space for an array, and initialize the array object.  The array
// object must not be directly copied, as the heap has a back-pointer to only
// the one object.  Instead pass the array object by reference.
void runtime_allocArray(runtime_array *array, size_t numElements, size_t elementSize, bool hasSubArrays) {
#ifdef RN_DEBUG
  if (array->numElements != 0 || array->data != NULL) {
    runtime_panicCstr("Allocating over non-empty array");
  }
#endif
  size_t numWords = runtime_bytesToWords(runtime_multCheckForOverflow(numElements, elementSize));
  array->data = allocArrayBuffer(numWords, hasSubArrays);
  array->numElements = numElements;
  updateArrayBackPointer(array);
}

// Mostly for debugging from C.
void runtime_arrayInitCstr(runtime_array *array, const char *text) {
  runtime_freeArray(array);
  size_t len = strlen(text);
  runtime_allocArray(array, len, sizeof(uint8_t), false);
  runtime_memcopy((uint8_t*)array->data, (uint8_t*)text, len);
}

// Free any memory used by the array and set its num_elements to 0.
static void resetArray(runtime_array *array) {
  if (array->data == NULL) {
    // Already reset.
    return;
  }
  runtime_heapHeader *header = (runtime_heapHeader*)(array->data - RN_HEADER_WORDS);
  if (header->hasSubArrays) {
    //  Reset sub-arrays first.
    uint32_t numElements = array->numElements;
    runtime_array *childArray = (runtime_array*)(array->data);
    while (numElements-- != 0) {
      resetArray(childArray);
      childArray++;
    }
  }
  runtime_zeroMemory((size_t*)header, RN_HEADER_WORDS + header->allocatedWords);
  free(header);
  array->data = NULL;
  array->numElements = 0;
}

// Free the array.
void runtime_freeArray(runtime_array *array) {
  if (array->numElements == 0) {
    return;
  }
  resetArray(array);
}

// Index an object in an array, given the array and reference width.
static uint64_t indexArrayObject(runtime_array *array, size_t index, uint32_t refWidth) {
  switch (refWidth) {
    case 8: return ((uint8_t*)(array->data))[index];
    case 16: return ((uint16_t*)(array->data))[index];
    case 32: return ((uint32_t*)(array->data))[index];
    case 64: return ((uint64_t*)(array->data))[index];
    default:
        runtime_panicCstr("Invalid object reference width in indexArrayObject");
  }
  return 0;  // Dummy return.
}

// Index the potentially multi-dimensional array to get the object, if it
// exists.  If it does exist, increment indices to access the next object.
static bool getNextObject(runtime_array *array, size_t indices[], uint32_t depth,
    uint32_t currentDepth, uint64_t *object, uint32_t refWidth) {
  if (indices[currentDepth] == array->numElements) {
    return false;
  }
  runtime_heapHeader *header = runtime_getArrayHeader(array);
  if (!header->hasSubArrays) {
    *object = indexArrayObject(array, indices[currentDepth], refWidth);
    indices[currentDepth]++;
    return true;
  }
  runtime_array *subArray = (runtime_array*)(array->data);
  while (indices[currentDepth] != array->numElements) {
    if (getNextObject(subArray, indices, depth, currentDepth + 1, object, refWidth)) {
      return true;
    } else {
      subArray++;
      indices[currentDepth]++;
      for (uint32_t i = currentDepth + 1; i < depth; i++) {
        indices[i] = 0;
      }
    }
  }
  return false;
}

// Call |callback| for each non-zero object in the array.  If this is a
// multi-dimensional array, call only for the leaf elements.  |array| cannot be
// a constant array: it must have a header.  It must also be on the stack.
void runtime_foreachArrayObject(runtime_array *array, void *callback, uint32_t refWidth, uint32_t depth) {
  if (array->numElements == 0) {
    return;
  }
  // We can't index with pointers because the callback might cause the heap to
  // compact.
  size_t indices[depth];
  for (uint32_t i = 0; i < depth; i++) {
    indices[i] = 0;
  }
  if (refWidth <= 8) {
    refWidth = 8;
  } else if (refWidth <= 16) {
    refWidth = 16;
  } else if (refWidth <= 32) {
    refWidth = 32;
  } else if (refWidth <= 64) {
    refWidth = 64;
  }
  uint64_t object = 0;
  uint64_t nullObject = (uint64_t)-1;
  if (refWidth < 64) {
    nullObject = ((uint64_t)1 << refWidth) - 1;
  }
  while (getNextObject(array, indices, depth, 0, &object, refWidth)) {
    if (object != nullObject) {
      if (refWidth == 8) {
        ((void(*)(uint8_t))callback)(object);
      } else if (refWidth == 16) {
        ((void(*)(uint16_t))callback)(object);
      } else if (refWidth == 32) {
        ((void(*)(uint32_t))callback)(object);
      } else if (refWidth == 64) {
        ((void(*)(uint64_t))callback)(object);
      }
    }
  }
}

// Initialize dynamic array heap memory.
void runtime_arrayStart(void) {
  static_assert(sizeof(runtime_heapHeader) == RN_HEADER_WORDS * sizeof(size_t),
      "Invalid heap header size");
  static_assert(sizeof(runtime_array) == RN_ARRAY_WORDS * sizeof(size_t),
      "Invalid array size");
  static_assert(RN_SIZET_MASK != UINT32_MAX, "Unsupported size_t size");
  static_assert(RN_SIZET_SHIFT != UINT32_MAX, "Unsupported size_t size");
  static_assert(sizeof(char) == 1 && sizeof(uint8_t) == 1,
                "Unsupported char or uint8_t size");
#ifdef _WIN32
  MEMORYSTATUSEX statex;
  statex.dwLength = sizeof (statex);
  GlobalMemoryStatusEx (&statex);
  runtime_totalRam = statex.ullTotalPhys;
#else
  struct sysinfo info;
  sysinfo(&info);
  runtime_totalRam = info.totalram;
#endif
  // Just to ensure there is enough room for a header.  Otherwise, we might get
  // underflow in memory size computations.
  if (runtime_totalRam < sizeof(runtime_heapHeader)) {
    runtime_throwExceptionCstr("Not enough memory to allocate arrays");
  }
  runtime_totalRam -= sizeof(runtime_heapHeader);
}

// Clean up array heap memory.
void runtime_arrayStop(void) {
}

// Resize the array.
static void arrayResize(runtime_array *array, size_t numElements, size_t elementSize,
    bool hasSubArrays, bool allocateExtra) {
  if (numElements == 0) {
    resetArray(array);
    return;
  }
  size_t oldNumElements = array->numElements;
  if (oldNumElements == 0) {
    return runtime_allocArray(array, numElements, elementSize, hasSubArrays);
  }
  runtime_heapHeader *header = runtime_getArrayHeader(array);
  size_t allocatedBytes = runtime_multCheckForOverflow(numElements, elementSize);
  if (allocatedBytes > runtime_totalRam) {
    runtime_throwExceptionCstr("Out of memory");
  }
  size_t oldAllocatedWords = header->allocatedWords;
  size_t allocatedWords = runtime_bytesToWords(allocatedBytes);
  if (allocateExtra) {
    // Make the array 50% larger than the requested size.
    allocatedWords += allocatedWords >> 1;
  }
  header->allocatedWords = allocatedWords;
  // If shrinking the array, zero out the deleted elements.
  if (allocatedWords < oldAllocatedWords) {
    if (!hasSubArrays) {
      // Zero out the deleted elements.
      runtime_zeroMemory(array->data + allocatedWords, oldAllocatedWords - allocatedWords);
    } else {
      // Free the sub-arrays at the end of the array.
      runtime_array *p = (runtime_array*)(array->data + numElements * RN_ARRAY_WORDS);
      for (uint32_t i = numElements; i < oldNumElements; i++) {
        resetArray(p);
        p++;
      }
    }
  }
  header = realloc(header, (allocatedWords + RN_HEADER_WORDS) << RN_SIZET_SHIFT);
  array->data = (size_t*)header + RN_HEADER_WORDS;
  array->numElements = numElements;
  if (allocatedWords > oldAllocatedWords) {
    // Zero out the new elements.
    runtime_zeroMemory(array->data + oldAllocatedWords, allocatedWords - oldAllocatedWords);
  }
  if (hasSubArrays) {
    updateSubArrayBackPointers(array);
  }
}

// Resize the array.  This will resize in-place if there is available room
// allocated on the heap for the array.  Otherwise, it will move the array to
// the end of the heap and resize it there.  If there is not enough space, the
// heap will first be compacted.
void runtime_resizeArray(runtime_array *array, size_t numElements, size_t elementSize, bool hasSubArrays) {
  arrayResize(array, numElements, elementSize, hasSubArrays, false);
}

// Make a copy of the array's data.  |dest| should be empty.  |source| cannot be empty.
static void replicateArrayData(runtime_array *dest, runtime_array *source, size_t numBytes, bool hasSubArrays) {
  size_t numElements = source->numElements;
  size_t numWords = runtime_bytesToWords(numBytes);
  size_t *destData = allocArrayBuffer(numWords, hasSubArrays);
  dest->data = destData;
  dest->numElements = numElements;
  updateArrayBackPointer(dest);
  if (!hasSubArrays) {
    runtime_memcopy(destData, source->data, numBytes);
  } else {
    for (uint32_t i = 0; i < numElements; i++) {
      // Recompute addresses from dest and source each iteration, since the heap
      // may have been compacted.
      runtime_array *subSourceArray = (runtime_array*)source->data + i;
      if (subSourceArray->data != NULL) {
        runtime_heapHeader *subHeader = runtime_getArrayHeader(subSourceArray);
        runtime_array *subDestArray = (runtime_array*)dest->data + i;
        replicateArrayData(subDestArray, subSourceArray,
            subHeader->allocatedWords << RN_SIZET_SHIFT, subHeader->hasSubArrays);
      }
    }
  }
}

// Make a deep copy of the array.  Reset |dest| first.
void runtime_copyArray(runtime_array *dest, runtime_array *source, size_t elementSize, bool hasSubArrays) {
  if (source == dest) {
    // Nothing to do.
    return;
  }
  resetArray(dest);
  if (source->data == NULL) {
    return;
  }
  size_t numBytes = source->numElements * elementSize;
  replicateArrayData(dest, source, numBytes, hasSubArrays);
#ifdef RN_DEBUG
  verifyArray(dest);
#endif
}

// Make a new array with copies of the elements from |lower| to |upper|, not
// including the element at |upper|.  Pass in |elementSize| and |hasSubArrays|
// because constant arrays have no heap header.
void runtime_sliceArray(runtime_array *dest, runtime_array *source, size_t lower, size_t upper,
    size_t elementSize, bool hasSubArrays) {
  resetArray(dest);
  if (upper == lower) {
    return;  // Empty slice.
  }
  if (lower > upper) {
    runtime_throwExceptionCstr("Left index of slice is greater than right index");
  }
  if (upper > source->numElements) {
    runtime_throwExceptionCstr("Attempting to index beyond end of array in slice operation");
  }
  size_t sliceElements = upper - lower;
  size_t numBytes = sliceElements * elementSize;
  size_t numWords = runtime_bytesToWords(numBytes);
  size_t offset = lower * elementSize;
  size_t* destData = allocArrayBuffer(numWords, false);
  dest->data = destData;
  dest->numElements = sliceElements;
  updateArrayBackPointer(dest);
  if (!hasSubArrays) {
    runtime_memcopy(dest->data, (uint8_t*)(source->data) + offset, numBytes);
  } else {
    for (size_t i = lower; i < upper; i++) {
      // Recompute addresses from dest and source each iteration, since the heap
      // may have been compacted.
      runtime_array *subSourceArray = (runtime_array*)source->data + i;
      if (subSourceArray->data != NULL) {
        runtime_heapHeader *subHeader = runtime_getArrayHeader(subSourceArray);
        runtime_array *subDestArray = (runtime_array*)dest->data + i;
        replicateArrayData(subDestArray, subSourceArray,
            subHeader->allocatedWords << RN_SIZET_SHIFT, subHeader->hasSubArrays);
      }
    }
  }
#ifdef RN_DEBUG
  verifyArray(dest);
#endif
}

// Move an array from |source| to |dest|.  |dest| is freed first, and |source|
// is reset afterwards.
void runtime_moveArray(runtime_array *dest, runtime_array *source) {
#ifdef RN_DEBUG
  verifyArray(source);
#endif
  resetArray(dest);
  size_t *sourceData = source->data;
  if (source->numElements != 0) {
    dest->data = sourceData;
    dest->numElements = source->numElements;
    source->data = NULL;
    source->numElements = 0;
    updateArrayBackPointer(dest);
  }
#ifdef RN_DEBUG
  verifyArray(dest);
#endif
}

// Copy an element to the end of |array|.
void runtime_appendArrayElement(runtime_array *array, uint8_t *data, size_t elementSize,
      bool isArray, bool hasSubArrays) {
  size_t numElements = array->numElements;
  arrayResize(array, array->numElements + 1, elementSize, isArray, true);
  uint8_t *dest = ((uint8_t*)array->data) + runtime_multCheckForOverflow(numElements, elementSize);
  if (!isArray) {
    if ((elementSize & RN_SIZET_MASK) != 0) {
      runtime_memcopy(dest, data, elementSize);
    } else {
      runtime_copyWords((size_t*)dest, (size_t*)data, elementSize >> RN_SIZET_SHIFT);
    }
  } else {
    runtime_array *sourceArray = (runtime_array*)data;
    runtime_array *destArray = (runtime_array*)dest;
    size_t numBytes = runtime_multCheckForOverflow(sourceArray->numElements, elementSize);
    replicateArrayData(destArray, sourceArray, numBytes, hasSubArrays);
  }
}

// Copy |source| to the end of |dest|.
void runtime_concatArrays(runtime_array *dest, runtime_array *source, size_t elementSize, bool hasSubArrays) {
  size_t sourceNumElements = source->numElements;
  if (sourceNumElements == 0) {
    return;
  }
  size_t destNumElements = dest->numElements;
  arrayResize(dest, sourceNumElements + destNumElements, elementSize, hasSubArrays, true);
  uint8_t *p = ((uint8_t*)dest->data) + destNumElements * elementSize;
  if (!hasSubArrays) {
    if ((elementSize & RN_SIZET_MASK) != 0) {
      runtime_memcopy(p, (uint8_t*)source->data, sourceNumElements * elementSize);
    } else {
      runtime_copyWords((size_t *)p, (size_t *)(source->data),
          (sourceNumElements * elementSize) >> RN_SIZET_SHIFT);
    }
  } else {
    for (size_t i = 0; i < destNumElements; i++) {
      // Recompute addresses from dest and source each iteration, since the heap
      // may have been compacted.
      runtime_array *subSourceArray = (runtime_array*)source->data + i;
      if (subSourceArray->data != NULL) {
        runtime_heapHeader *subHeader = runtime_getArrayHeader(subSourceArray);
        runtime_array *subDestArray = (runtime_array*)dest->data + sourceNumElements + i;
        replicateArrayData(subDestArray, subSourceArray,
            subHeader->allocatedWords << RN_SIZET_SHIFT, subHeader->hasSubArrays);
      }
    }
  }
}

// Reverse the array by words.
static void reverseWords(size_t *data, size_t numElements, size_t elementWords) {
  size_t *first = data;
  size_t *last = data + (numElements - 1) * elementWords;
  while (first < last) {
    for (size_t i = 0; i < elementWords; i++) {
      size_t tmp = *first;
      *first++ = *last;
      *last++ = tmp;
    }
    last -= elementWords << 1;
  }
}

// Reverse an array byte by byte.
static void reverseBytes(uint8_t *data, size_t numElements, size_t elementSize) {
  uint8_t *first = data;
  uint8_t *last = data + (numElements - 1) * elementSize;
  while (first < last) {
    for (size_t i = 0; i < elementSize; i++) {
      uint8_t tmp = *first;
      *first++ = *last;
      *last++ = tmp;
    }
    last -= elementSize << 1;
  }
}

// Reverse the elements of an array, in-place.
void runtime_reverseArray(runtime_array *array, size_t elementSize, bool hasSubArrays) {
  if (array->numElements <= 1) {
    return;
  }
  if (elementSize & RN_SIZET_MASK) {
    reverseBytes((uint8_t*)(array->data), array->numElements, elementSize);
  } else {
    reverseWords(array->data, array->numElements, elementSize >> RN_SIZET_SHIFT);
  }
  if (hasSubArrays) {
    updateSubArrayBackPointers(array);
  }
}

// Set |aElemPtr| and |bElemPtr| to point to the first elements in the array that
// are different.
static void findArrayFirstDifferentElements(const runtime_array *a, const runtime_array *b,
    size_t elementSize, void **aElemPtr, void **bElemPtr) {
  uint8_t *aPtr = (uint8_t*)(a->data);
  uint8_t *bPtr = (uint8_t*)(b->data);
  size_t numElements = a->numElements <= b->numElements? a->numElements : b->numElements;
  while (numElements > 0 && !memcmp(aPtr, bPtr, elementSize)) {
    numElements--;
    aPtr += elementSize;
    bPtr += elementSize;
  }
  if (numElements == 0) {
    if (a->numElements > b->numElements) {
      *aElemPtr = aPtr;
    } else if (b->numElements > a->numElements) {
      *bElemPtr = bPtr;
    }
    return;
  }
  *aElemPtr = aPtr;
  *bElemPtr = bPtr;
}

// Set |aElemPtr| and |bElemPtr| to point to the first elements in the array that
// are different.
static void findSubArrayFirstDifferentElements(const runtime_array *a, const runtime_array *b,
    size_t elementSize, void **aElemPtr, void **bElemPtr) {
  runtime_array *aPtr = (runtime_array*)a->data;
  runtime_array *bPtr = (runtime_array*)b->data;
  runtime_heapHeader *aHeader = runtime_getArrayHeader(aPtr);
  bool hasSubArrays = aHeader->hasSubArrays;
  size_t numElements = a->numElements <= b->numElements? a->numElements : b->numElements;
  while (numElements-- > 0) {
    if (hasSubArrays) {
      findSubArrayFirstDifferentElements(aPtr, bPtr, elementSize, aElemPtr, bElemPtr);
    } else {
      findArrayFirstDifferentElements(a, b, elementSize, aElemPtr, bElemPtr);
    }
    if (*aElemPtr != NULL || *bElemPtr != NULL) {
      return;
    }
    aPtr++;
    bPtr++;
  }
  if (a->numElements > b->numElements) {
    *aElemPtr = aPtr;
  } else if (b->numElements > numElements) {
    *aElemPtr = aPtr;
  }
}

// Compare two basic types.  Return -1 if a < b, 0 if a == b, and 1 if a > b.
static int32_t compareElements(runtime_type elementType, void *aPtr, void *bPtr,
    size_t elementSize, bool secret) {
  if ((elementType == RN_UINT || elementType == RN_INT) && elementSize == sizeof(runtime_array)) {
    if (runtime_compareBigints(RN_EQUAL, aPtr, bPtr)) {
      return 0;
    }
    if (runtime_compareBigints(RN_LT, aPtr, bPtr)) {
      return -1;
    }
    return 1;
  }
  size_t a = 0;
  size_t b = 0;
  switch (elementSize) {
    case 1:
      a = *(uint8_t *)aPtr;
      b = *(uint8_t *)bPtr;
      break;
    case 2:
      a = *(uint16_t *)aPtr;
      b = *(uint16_t *)bPtr;
      break;
    case 4:
      a = *(uint32_t *)aPtr;
      b = *(uint32_t *)bPtr;
      break;
    case 8:
      a = *(size_t *)aPtr;
      b = *(size_t *)bPtr;
      break;
    default:
      runtime_panicCstr("Unsupported integer width");
  }
  if (a == b) {
    return 0;
  }
  if (!secret) {
    switch (elementType) {
      case RN_UINT:
        return a < b ? -1 : 1;
      case RN_INT:
        return (int64_t)a < (int64_t)b ? -1 : 1;
      default:
        runtime_panicCstr("Unsupported type in array comparison");
    }
  } else {
    uint32_t shift = sizeof(size_t) * 8 - 1;
    if (elementType == RN_INT) {
      // If sign bits are equal, then toggling sign bits does not change the
      // outcome.  If they are different, then toggling the sign big corrects
      // the interpretation of which is larger when viewed as unsigned.
      a ^= (size_t)1 << shift;
      b ^= (size_t)1 << shift;
    } else if (elementType != RN_UINT) {
      runtime_panicCstr("Unsupported type in array comparison");
    }
    size_t aGTb = (b - a) >> shift;
    size_t bGTa = (a - b) >> shift;
    return aGTb - bGTa;
  }
  return 0;  // Dummy return.
}

// Compare two arrays lexically.  Return true or false according to the operator
// selected with lessThan and OrEqual.  If |secret| is true, use constant-time
// comparison.
// TODO: Finish making this constant time for secrets!
bool runtime_compareArrays(runtime_comparisonType compareType, runtime_type elementType, const runtime_array *a,
    const runtime_array *b, size_t elementSize, bool hasSubArrays, bool secret) {
  void *aPtr = NULL;
  void *bPtr = NULL;
  if (hasSubArrays) {
    findSubArrayFirstDifferentElements(a, b, elementSize, &aPtr, &bPtr);
  } else {
    findArrayFirstDifferentElements(a, b, elementSize, &aPtr, &bPtr);
  }
  int32_t result;
  if (aPtr == NULL && bPtr == NULL) {
    result = 0;
  } else if (aPtr == NULL) {
    result = -1;
  } else if (bPtr == NULL) {
    result = 1;
  } else {
    result = compareElements(elementType, aPtr, bPtr, elementSize, secret);
  }
  switch (compareType) {
    case RN_LT:
      return result < 0;
    case RN_LE:
      return result <= 0;
    case RN_GT:
      return result > 0;
    case RN_GE:
      return result >= 0;
    case RN_EQUAL:
      return result == 0;
    case RN_NOTEQUAL:
      return result != 0;
  }
  return false; // Dummy return;
}

// Initialize an array of strings from a C vector of char*.
void runtime_initArrayOfStringsFromC(runtime_array *array, const uint8_t** vector, size_t len) {
  arrayResize(array, len, sizeof(runtime_array), true, false);
  runtime_array *subArray = (runtime_array*)(array->data);
  for (uint32_t i = 0; i < len; i++) {
    uint32_t len = strlen((const char*)vector[i]);
    runtime_allocArray(subArray, len, sizeof(uint8_t), false);
    runtime_memcopy(subArray->data, vector[i], len * sizeof(uint8_t));
    subArray++;
  }
}

// Initialize an array of strings from a C vector of char* with converting to UTF-8 from locale.
void runtime_initArrayOfStringsFromCUTF8(runtime_array *array, const uint8_t** vector, size_t len) {
#ifdef _WIN32
  arrayResize(array, len, sizeof(runtime_array), true, false);
  runtime_array *subArray = (runtime_array*)(array->data);
  for (uint32_t i = 0; i < len; i++) {
    uint32_t len = strlen((const char*)vector[i]);
    int wlen = MultiByteToWideChar(CP_ACP, 0, (const char*)vector[i], len, NULL, 0);
    wchar_t* wbuf = (wchar_t*) malloc(sizeof(wchar_t) * wlen);
    MultiByteToWideChar(CP_ACP, 0, (const char*)vector[i], strlen((const char*)vector[i]), wbuf, wlen);
    int clen = WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, NULL, 0, NULL, FALSE);
    char* cbuf = (char*) malloc(clen);
    WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen, cbuf, clen, NULL, FALSE);

    runtime_allocArray(subArray, clen, sizeof(uint8_t), false);
    runtime_memcopy(subArray->data, cbuf, clen * sizeof(uint8_t));
    free(wbuf);
    free(cbuf);
    subArray++;
  }
#else
  runtime_initArrayOfStringsFromC(array, vector, len);
#endif
}

// XOR two byte-strings together.  Throw an error if their sizes differ.
void runtime_xorStrings(runtime_array *dest, runtime_array *a, runtime_array *b) {
  size_t len = a->numElements;
  if (b->numElements != len) {
    runtime_panicCstr("Called runtime_xorStrings on strings of different length");
  }
  arrayResize(dest, len, sizeof(uint8_t), false, false);
  const size_t *aPtr = a->data;
  const size_t *bPtr = b->data;
  size_t *destPtr = dest->data;
  size_t numWords = runtime_bytesToWords(len);
  for (size_t i = 0; i < numWords; i++) {
    *destPtr++ = *aPtr++ ^ *bPtr++;
  }
}
