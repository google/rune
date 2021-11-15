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

// Find the integer datatype used by the enum.  All initializers must use the
// same time.  The default, if no initializers are given, is uint32.
deDatatype deFindEnumIntType(deBlock block) {
  deDatatype intType = deDatatypeNull;
  deVariable var;
  deForeachBlockVariable(block, var) {
    deExpression initializer = deVariableGetInitializerExpression(var);
    if (initializer != deExpressionNull) {
      deLine line = deExpressionGetLine(initializer);
      deBigint bigint = deExpressionGetBigint(initializer);
      uint32 width = deBigintGetWidth(bigint);
      deDatatype entryType = deUintDatatypeCreate(width);
      if (intType == deDatatypeNull) {
        intType = entryType;
      } else if (entryType != intType) {
        deError(line, "Enum entry has different integer type than prior entries");
      }
    }
  } deEndBlockVariable;
  if (intType == deDatatypeNull) {
    return deUintDatatypeCreate(32);
  }
  return intType;
}

// Verify all integer assignments in the enum have the same type.  Assign integer
// values, start at 0 for the first variable, and increment for each.  Reset
// counter to assigned value when we hit an entry with an assigned value.
// Verify that assigned values are increasing.
void deAssignEnumEntryConstants(deBlock block) {
  // Just verify the entries all have compatible types.
  deDatatype intType = deFindEnumIntType(block);
  // Set the datatype of the first entry to intType, since deEnumDatatypeCreate
  // uses the datatype of the first entry to get the width.
  deVariableSetDatatype(deBlockGetFirstVariable(block), intType);
  deDatatype datatype = deEnumDatatypeCreate(deBlockGetOwningFunction(block));
  utAssert(deDatatypeGetWidth(datatype) == deDatatypeGetWidth(intType));
  uint32 count = 0;
  deVariable var;
  deForeachBlockVariable(block, var) {
    deExpression initializer = deVariableGetInitializerExpression(var);
    if (initializer != deExpressionNull) {
      deLine line = deExpressionGetLine(initializer);
      uint32 value = deBigintGetUint32(deExpressionGetBigint(initializer), line);
      if (value < count) {
        deError(line, "Non-increasing enum value.");
      }
      count = value;
    }
    deVariableSetEntryValue(var, count);
    deVariableSetDatatype(var, datatype);
    count++;
  } deEndBlockVariable;
}
