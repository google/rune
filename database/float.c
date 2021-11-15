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

// Dump a float.
void deDumpFloat(deFloat floatVal) {
  printf("%gf%u", deFloatGetValue(floatVal), deFloatGetWidth(floatVal));
}

// Append a float to |string| for debugging.
void deDumpFloatStr(deString string, deFloat floatVal) {
  deStringSprintf(string, "%gf%u", deFloatGetValue(floatVal), deFloatGetWidth(floatVal));
}

// Create a float.
deFloat deFloatCreate(deFloatType type, double value) {
  deFloat floatVal = deFloatAlloc();
  deFloatSetType(floatVal, type);
  uint32 width = 0;
  switch (type) {
    case DE_FLOAT_SINGLE:
      width = 32;
      break;
    case DE_FLOAT_DOUBLE:
      width = 64;
      break;
  }
  deFloatSetValue(floatVal, value);
  deFloatSetWidth(floatVal, width);
  return floatVal;
}

// Return the negation of the float.
deFloat deFloatNegate(deFloat theFloat) {
  return deFloatCreate(deFloatGetType(theFloat), -deFloatGetValue(theFloat));
}

// Make a copy of the float.
deFloat deCopyFloat(deFloat theFloat) {
  return deFloatCreate(deFloatGetType(theFloat), deFloatGetValue(theFloat));
}
