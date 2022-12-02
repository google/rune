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
#include <stdarg.h>

// Create a string.  These are uniquified so there is only one copy of any given string.
deString deStringCreate(char *text, uint32 len) {
  deString string = deRootFindString(deTheRoot, text, len);
  if (string != deStringNull) {
    return string;
  }
  string = deStringAlloc();
  deStringSetText(string, text, len);
  deStringSetUsed(string, len);
  deRootInsertString(deTheRoot, string);
  return string;
}

// Create a string from a C-style zero-terminated string.  Do not include the trailing '\0'.
// These are uniquified so there is only one copy of any give string.
deString deCStringCreate(char *text) {
  size_t len = strlen(text);
  if (len > UINT32_MAX) {
    utExit("String too long");
  }
  return deStringCreate(text, len);
}

// Create a mutable string.  These are not uniquified, and should be destroyed when done.
deString deMutableStringCreate(void) {
  deString string = deStringAlloc();
  return string;
}

// Create a string from a C-style zero-terminated string.  Do not include the trailing '\0'.
// These are not uniquified.
deString deMutableCStringCreate(char *text) {
  size_t len = strlen(text);
  if (len > UINT32_MAX) {
    utExit("String too long");
  }
  deString string = deMutableStringCreate();
  // Needed to ensure deStringSetText does not realloc string texts.
  deStringResizeTexts(string, len);
  deStringSetText(string, text, len);
  deStringSetUsed(string, len);
  return string;
}

// Make a copy of the string.  This is only used for non-uniquified strings.
deString deCopyString(deString string) {
  utAssert(deStringGetRoot(string) == deRootNull);
  deString newString = deStringAlloc();
  // Needed to ensure deStringSetText does not realloc string texts.
  uint32 len = deStringGetUsed(string);
  deStringResizeTexts(newString, len);
  deStringSetText(newString, deStringGetText(string), len);
  deStringSetUsed(newString, len);
  return newString;
}

// Create a uniquified string from a non-uniquified string.
deString deUniquifyString(deString string) {
  utAssert(deStringGetRoot(string) == deRootNull);
  deString existingString = deRootFindString(deTheRoot, deStringGetText(string),
      deStringGetUsed(string));
  if (existingString != deStringNull) {
    return existingString;
  }
  deString newString = deStringAlloc();
  // Needed to ensure deStringSetText does not realloc string texts.
  deStringResizeTexts(newString, deStringGetUsed(string));
  uint32 len =  deStringGetUsed(string);
  deStringSetText(newString, deStringGetText(string), len);
  deStringSetUsed(string, len);
  deRootInsertString(deTheRoot, newString);
  return newString;
}

// Compare two non-uniquified strings for equality.
bool deStringsEqual(deString string1, deString string2) {
  if (string1 == deStringNull || string2 == deStringNull) {
    return string1 == string2;
  }
  if (deStringGetUsed(string1) != deStringGetUsed(string2)) {
    return false;
  }
  return !memcmp(deStringGetText(string1), deStringGetText(string2), deStringGetUsed(string1));
}

// Convert a hex digit to a 4-but uint8_t.
static uint8 toHex(uint8 c) {
  return c <= 9? '0' + c : 'a' + c - 10;
}

// Escape a string so it can be used as a C string.  Return a temp buffer.
char *deEscapeString(deString string) {
  uint32 len = deStringGetUsed(string);
  char *buf = utMakeString(4 * len + 1);
  char *p = deStringGetText(string);
  char *q = buf;
  char c = *p++;
  for (uint32 i = 0; i < len; i++) {
    if (c >= ' ' && c <= '~') {
      *q++ = c;
    } else {
      if (c == '\n') {
        *q++ = '\\';
        *q++ = 'n';
      } else if (c == '\t') {
      *q++ = '\\';
        *q++ = 't';
      } else if (c == '\0') {
      *q++ = '\\';
        *q++ = '0';
      } else {
        *q++ = '\\';
        *q++ = 'x';
        *q++ = toHex(c >> 4);
        *q++ = toHex(c & 0xf);
      }
    }
    c = *p++;
  }
  *q++ = '\0';
  return buf;
}

// Determine if the string contains any 0 chars.
static bool stringContainsZero(deString string) {
  char *p = deStringGetText(string);
  uint32 len = deStringGetUsed(string);
  while (len-- != 0) {
    if (*p++ == '\0') {
      return true;
    }
  }
  return false;
}

// Return a zero-terminated C string.  This exits if the string contains any 0's.
// Returns a temp buffer.
char *deStringGetCstr(deString string) {
  if (stringContainsZero(string)) {
    utExit("String containing '\\0' converted to C string");
  }
  uint32 len = deStringGetUsed(string);
  char *buf = utMakeString(len + 1);
  memcpy(buf, deStringGetText(string), len);
  buf[len] = '\0';
  return buf;
}

// Create a string using a printf-like format.  For now, only %s is supported,
// which must match a deString.
deString deStrinCreateFormatted(char *format, ...) {
  va_list ap;
  va_start(ap, format);
  uint32 bufLen = 42;
  uint32 pos = 0;
  char *buf = utMakeString(bufLen);
  char *p = format;
  while (*p != '\0') {
    char c = *p++;
    if (c == '%') {
      c = *p++;
      if (c == 's') {
        deString string = va_arg(ap, deString);
        uint32 len = deStringGetUsed(string);
        buf = deResizeBufferIfNeeded(buf, &bufLen, pos, len);
        memcpy(buf + pos, deStringGetText(string), len);
        pos += len;
      } else {
        utExit("Unknown escape sequence char %c", c);
      }
    } else {
      buf = deResizeBufferIfNeeded(buf, &bufLen, pos, 1);
      buf[pos++] = c;
    }
  }
  va_end(ap);
  return deStringCreate(buf, pos);
}
