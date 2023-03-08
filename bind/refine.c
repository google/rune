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

// Set the variable's datatype.  Check that it does not violate the variables
// type constraint, if any.
static void checkVariableDatatype(deBlock scopeBlock, deVariable variable, deLine line) {
  deDatatype datatype = deVariableGetDatatype(variable);
  deExpression typeExpression = deVariableGetTypeExpression(variable);
  if (typeExpression != deExpressionNull &&
      !deDatatypeMatchesTypeExpression(scopeBlock, datatype, typeExpression)) {
    char* message = deVariableGetType(variable) == DE_VAR_PARAMETER?
        "Violation of parameter" : "Violates variable";
    deError(line, "%s %s's type constraint: %s", message,
            deVariableGetName(variable), deDatatypeGetTypeString(datatype));
  }
}

// Set the variable's datatype.  Check that it does not violate the variables
// type constraint, if any.
static void setVariableDatatype(deBlock scopeBlock, deVariable variable,
    deDatatype datatype, deLine line) {
  deDatatype varDatatype = deVariableGetDatatype(variable);
  deDatatype unifiedDatatype = datatype;
  if (varDatatype != deDatatypeNull && varDatatype != datatype) {
    unifiedDatatype = deUnifyDatatypes(deVariableGetDatatype(variable), datatype);
    if (unifiedDatatype == deDatatypeNull) {
      deError(line, "Assigning %s a different type than a prior assignment:%s",
          deVariableGetName(variable),
          deGetOldVsNewDatatypeStrings(varDatatype, datatype));
    }
  }
  deVariableSetDatatype(variable, unifiedDatatype);
  checkVariableDatatype(scopeBlock, variable, line);
}

// Refine NULL types on variables to class types, now that we have a specific class.
void deRefineAccessExpressionDatatype(deBlock scopeBlock, deExpression target,
    deDatatype valueType) {
   deDatatype targetType = deExpressionGetDatatype(target);
   deLine line = deExpressionGetLine(target);
  if (deDatatypeGetType(valueType) == DE_TYPE_NULL) {
    // Don't unrefine to a NULL class if we have already refined.
    deDatatypeType type = deDatatypeGetType(targetType);
    utAssert(type == DE_TYPE_CLASS || type == DE_TYPE_NULL);
    return;
  }
  switch (deExpressionGetType(target)) {
    case DE_EXPR_IDENT: {
      deIdent ident = deFindIdent(scopeBlock, deExpressionGetName(target));
      utAssert(deIdentGetType(ident) == DE_IDENT_VARIABLE);
      deVariable variable = deIdentGetVariable(ident);
      setVariableDatatype(scopeBlock, variable, valueType, line);
      break;
    }
    case DE_EXPR_INDEX: {
      deLine line = deExpressionGetLine(target);
      deExpression nextTarget = deExpressionGetFirstExpression(target);
      deExpression indexExpr = deExpressionGetNextExpression(nextTarget);
      deDatatype nextTargetType = deExpressionGetDatatype(nextTarget);
      deDatatype nextValueType;
      if (deDatatypeGetType(nextTargetType) == DE_TYPE_TUPLE) {
        deDatatypeArray types = deListDatatypes(target);
        uint32 index = deBigintGetUint32(deExpressionGetBigint(indexExpr), line);
        deDatatypeArraySetiDatatype(types, index, valueType);
        nextValueType = deTupleDatatypeCreate(types);
      } else {
        utAssert(deDatatypeGetType(nextTargetType) == DE_TYPE_ARRAY);
        nextValueType = deArrayDatatypeCreate(valueType);
      }
      deRefineAccessExpressionDatatype(scopeBlock, nextTarget, nextValueType);
      break;
    }
    case DE_EXPR_DOT: {
      deExpression left = deExpressionGetFirstExpression(target);
      deExpression right = deExpressionGetNextExpression(left);
      deDatatype leftType = deExpressionGetDatatype(left);
      deBlock subBlock;
      if (deDatatypeGetType(leftType) == DE_TYPE_CLASS) {
        deClass theClass = deDatatypeGetClass(leftType);
        subBlock = deClassGetSubBlock(theClass);
      } else {
        utAssert(deDatatypeGetType(leftType) == DE_TYPE_FUNCTION);
        deFunction function = deDatatypeGetFunction(leftType);
        subBlock = deFunctionGetSubBlock(function);
      }
      utAssert(deExpressionGetType(right) == DE_EXPR_IDENT);
      deIdent ident = deFindIdent(subBlock, deExpressionGetName(right));
      utAssert(deIdentGetType(ident) == DE_IDENT_VARIABLE);
      deVariable variable = deIdentGetVariable(ident);
      setVariableDatatype(scopeBlock, variable, valueType, line);
      break;
    }
    default:
      utExit("Unexpected access expression type");
  }
}
