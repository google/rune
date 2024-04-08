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

#include "runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <float.h>
#include <string.h>

#define MAX_FLOAT_STRING_SIZE (64)

#if defined(DBL_DECIMAL_DIG)
#  define DBL_SIG_DIGITS (DBL_DECIMAL_DIG)
#  define FLT_SIG_DIGITS (FLT_DECIMAL_DIG)
#elif defined(DECIMAL_DIG)
#  define DBL_SIG_DIGITS (DECIMAL_DIG)
#  define FLT_SIG_DIGITS (DECIMAL_DIG)
#else
#  define DBL_SIG_DIGITS (DBL_DIG + 3)
#  define FLT_SIG_DIGITS (FLT_DIG + 3)
#endif


void runtime_f32tostring(runtime_array *dest, float value) {
  char tmp[MAX_FLOAT_STRING_SIZE];
  memset(tmp, 0, sizeof(tmp));
  int len = snprintf(tmp, sizeof(tmp), "%.*g", FLT_SIG_DIGITS, value);

  runtime_freeArray(dest);
  runtime_allocArray(dest, len, sizeof(uint8_t), false);
  memcpy((char*)dest->data, tmp, len);
}

void runtime_f64tostring(runtime_array *dest, double value) {
  char tmp[MAX_FLOAT_STRING_SIZE];
  memset(tmp, 0, sizeof(tmp));
  int len = snprintf(tmp, sizeof(tmp), "%.*g", DBL_SIG_DIGITS, value);

  runtime_freeArray(dest);
  runtime_allocArray(dest, len, sizeof(uint8_t), false);
  memcpy((char*)dest->data, tmp, len);
}
