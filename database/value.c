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

// Values are used during execution of generators.  They are bound to variables
// and computed from expressions.
#include "de.h"

// Dump a value to the end of |string| for debugging.
void deDumpValueStr(deString string, deValue value) {
  switch (deValueGetType(value)) {
    case DE_TYPE_BOOL:
      deStringSprintf(string, "%s", deValueBoolVal(value)? "true" : "false");
      break;
    case DE_TYPE_STRING:
      deStringSprintf(string, "%s", deEscapeString(deValueGetStringVal(value)));
      break;
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
      deDumpBigint(deValueGetBigintVal(value));
      break;
    case DE_TYPE_TCLASS:
      deStringSprintf(string, "<tclass %s>", deTclassGetName(deValueGetTclassVal(value)));
      break;
    case DE_TYPE_CLASS:
      deStringSprintf(string, "<class of %s>", deTclassGetName(deClassGetTclass(deValueGetClassVal(value))));
      break;
    case DE_TYPE_FUNCTION:
      deStringSprintf(string, "<function %s>", deFunctionGetName(deValueGetFuncVal(value)));
      break;
    default:
      utExit("Unexpected value type");
  }
}

// Dump a value to stdout.
void deDumpValue(deValue value) {
  deString string = deMutableStringCreate();
  deDumpValueStr(string, value);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Create a new value object.
static deValue createValue(deDatatypeType type) {
  deValue value = deValueAlloc();
  deValueSetType(value, type);
  return value;
}

// Create an integer value.
deValue deIntegerValueCreate(deBigint bigint) {
  deValue value;
  if (deBigintSigned(bigint)) {
    value = createValue(DE_TYPE_INT);
  } else {
    value = createValue(DE_TYPE_UINT);
  }
  deValueSetBigintVal(value, deCopyBigint(bigint));
  return value;
}

// Create a float value.
deValue deFloatValueCreate(deFloat theFloat) {
  deValue value = createValue(DE_TYPE_FLOAT);
  deValueSetFloatVal(value, deCopyFloat(theFloat));
  return value;
}

// Create a Boolean value.
deValue deBoolValueCreate(bool value) {
  deValue val = createValue(DE_TYPE_BOOL);
  deValueSetBoolVal(val, value);
  return val;
}

// Create a string value.  The value takes ownership of the string.
deValue deStringValueCreate(deString string) {
  deValue value = createValue(DE_TYPE_STRING);
  deValueSetStringVal(value, string);
  return value;
}

// Create a tclass value.
deValue deTclassValueCreate(deTclass tclass) {
  deValue value = createValue(DE_TYPE_TCLASS);
  deValueSetTclassVal(value, tclass);
  return value;
}

// Create a class value.
deValue deClassValueCreate(deClass theClass) {
  deValue value = createValue(DE_TYPE_CLASS);
  deValueSetClassVal(value, theClass);
  return value;
}

// Create a function value.
deValue deFunctionValueCreate(deFunction function) {
  deValue value = createValue(DE_TYPE_FUNCTION);
  deValueSetFuncVal(value, function);
  return value;
}

// Return a utSym representing the name of the value, or the string if this
// value is a string.
utSym deValueGetName(deValue value) {
  switch (deValueGetType(value)) {
    case DE_TYPE_BOOL:
      return deValueBoolVal(value)? utSymCreate("true") : utSymCreate("false");
    case DE_TYPE_STRING:
      return utSymCreate(deEscapeString(deValueGetStringVal(value)));
      break;
    case DE_TYPE_TCLASS:
      return deTclassGetSym(deValueGetTclassVal(value));
    case DE_TYPE_CLASS:
      return deTclassGetSym(deClassGetTclass(deValueGetClassVal(value)));
    case DE_TYPE_FUNCTION:
      return deFunctionGetSym(deValueGetFuncVal(value));
    default:
      return utSymNull;
  }
  return utSymNull;  // Dummy return.
}

// Return true if the values are equal.
bool deValuesEqual(deValue a, deValue b) {
  if (deValueGetType(a) != deValueGetType(b)) {
    utExit("Comparing values of different types");
  }
  switch (deValueGetType(a)) {
    case DE_TYPE_BOOL:
      return deValueBoolVal(a) == deValueBoolVal(b);
    case DE_TYPE_STRING:
      return deStringsEqual(deValueGetStringVal(a), deValueGetStringVal(b));
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
      return deBigintsEqual(deValueGetBigintVal(a), deValueGetBigintVal(b));
    case DE_TYPE_TCLASS:
      return deValueGetTclassVal(a) == deValueGetTclassVal(b);
    case DE_TYPE_CLASS:
      return deValueGetClassVal(a) == deValueGetClassVal(b);
    case DE_TYPE_FUNCTION:
      return deValueGetFuncVal(a) == deValueGetFuncVal(b);
    default:
      utExit("Unknown value type");
  }
  return false;  // Dummy return.
}
