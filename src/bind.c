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

// Type variables may never be directly instantiated.  As a post-process to type
// binding of a scope-level block, all variables need to be checked.  If they
// are assigned to a type expression, either as a parameter in a function call,
// or in a variable assignment within the block, and instantiated, then they
// cannot be type variables.  Most expressions instantiate their sub-expressions,
// so use this global to indicate that the current expression is instantiating.
// For expressions such as type-cast, before recursing into the type, clear this
// flag, and restore it when done binding it.
bool deInstantiating;
// The currently binding theClass.
static deClass deCurrentClass;
// We inline iterators only during the second call to deBindBlock.
static bool deInlining;
// Set when binding an access expression.
bool deBindingAssignmentTarget;
// The current statement being bound.
deStatement deCurrentStatement;

// Forward declarations for recursion.
static void bindBlock(deBlock scopeBlock, deBlock block, deSignature signature);
static void bindExpression(deBlock scopeBlock, deExpression expression);

// Manually set the datatype of argv.
static void bindArgvVariable(deBlock rootBlock) {
  utSym argvSym = utSymCreate("argv");
  deIdent ident = deBlockFindIdent(rootBlock, argvSym);
  utAssert(deIdentGetType(ident) == DE_IDENT_VARIABLE);
  deVariable argv = deIdentGetVariable(ident);
  deDatatype elementType = deStringDatatypeCreate();
  deDatatype argvType = deArrayDatatypeCreate(elementType);
  deVariableSetDatatype(argv, argvType);
  deVariableSetInstantiated(argv, true);
}

// Get the datatype of the expression, and require that it be set.
static inline deDatatype getDatatype(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (datatype == deDatatypeNull) {
    deError(deExpressionGetLine(expression),
        "Could not determine expression type.  Make sure to use fully qualified types");
  }
  return datatype;
}

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

// Set the signature's return type.  If it is a function signature, check that
// the return type does not violate it's type constraint, if any.
static void setSignatureReturnType(deBlock scopeBlock, deSignature signature,
      deDatatype returnType, deLine line) {
  deSignatureSetReturnType(signature, returnType);
  deFunction function = deSignatureGetFunction(signature);
  if (function == deFunctionNull) {
    return;
  }
  deExpression typeExpression = deFunctionGetTypeExpression(function);
  if (typeExpression != deExpressionNull) {
    if (returnType == deNoneDatatypeCreate()) {
      deError(line, "Function %s must return a value", deFunctionGetName(function));
    }
    if (!deDatatypeMatchesTypeExpression(scopeBlock, returnType, typeExpression)) {
      deError(line, "Return statement violates function %s's type constraint: %s",
              deFunctionGetName(function), deDatatypeGetTypeString(returnType));
    }
  }
}

// Set the integer expression's datatype.
static void bindIntegerExpression(deExpression expression) {
  deBigint bigint = deExpressionGetBigint(expression);
  uint32 width = deBigintGetWidth(bigint);
  deDatatype datatype;
  if (deBigintSigned(bigint)) {
    datatype = deIntDatatypeCreate(width);
  } else {
    datatype = deUintDatatypeCreate(width);
  }
  deExpressionSetDatatype(expression, datatype);
  deExpressionSetAutocast(expression, deBigintWidthUnspecified(bigint));
}

// Set the random uint expression's datatype, which is just an unsigned integer.
static void bindRandUintExpression(deExpression expression) {
  uint32 width = deExpressionGetWidth(expression);
  deDatatype datatype = deUintDatatypeCreate(width);
  datatype = deSetDatatypeSecret(datatype, true);
  deExpressionSetDatatype(expression, datatype);
}

// Set the float expression's datatype.
static void bindFloatExpression(deExpression expression) {
  deFloat floatVal = deExpressionGetFloat(expression);
  uint32 width = deFloatGetWidth(floatVal);
  deDatatype datatype = deFloatDatatypeCreate(width);
  deExpressionSetDatatype(expression, datatype);
}

// Find the position of the parameter.
static uint32_t findParamIndex(deVariable variable) {
  uint32_t xParam = 0;
  deBlock block = deVariableGetBlock(variable);
  deVariable param;
  deForeachBlockVariable(block, param) {
    if (param == variable) {
      return xParam;
    }
    xParam++;
  } deEndBlockVariable;
  utExit("Broken varialbe list");
  return 0;  // Dummy return.
}

// Bind the identifier expression to a type.
static void bindIdentExpression(deBlock scopeBlock, deExpression expression) {
  utSym name = deExpressionGetName(expression);
  deIdent ident = deFindIdent(scopeBlock, name);
  deLine line = deExpressionGetLine(expression);
  if (ident == deIdentNull) {
    if (deBlockGetType(scopeBlock) == DE_BLOCK_FUNCTION) {
      deFunction function = deBlockGetOwningFunction(scopeBlock);
      deFunctionType type = deFunctionGetType(function);
      if (type == DE_FUNC_PACKAGE || type == DE_FUNC_MODULE) {
        printf("Did you mean to access %s in %s %s?\n",
            utSymGetName(name), type == DE_FUNC_MODULE? "module" : "package",
            deFunctionGetName(function));
      }
    }
    deError(line, "Undefined identifier: %s", utSymGetName(name));
  }
  deIdent oldIdent = deExpressionGetIdent(expression);
  if (oldIdent != deIdentNull) {
    deIdentRemoveExpression(oldIdent, expression);
  }
  deIdentAppendExpression(ident, expression);
  deDatatype datatype = deGetIdentDatatype(ident);
  if (datatype == deDatatypeNull) {
    utExit("Identifier referenced with no data type set");
  }
  deExpressionSetDatatype(expression, datatype);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type == DE_TYPE_TCLASS) {
    deExpressionSetIsType(expression, true);
  }
  if (deIdentGetType(ident) == DE_IDENT_VARIABLE) {
    deVariable variable = deIdentGetVariable(ident);
    bool isType = deVariableIsType(variable);
    deExpressionSetIsType(expression, isType);
    if (!isType) {
      if (!deBindingAssignmentTarget) {
        deVariableSetInstantiated(variable, deVariableInstantiated(variable) || deInstantiating);
      }
      deExpressionSetConst(expression, deVariableConst(variable));
    }
    if (deInstantiating && deVariableGetType(variable) == DE_VAR_PARAMETER && deCurrentSignature != deSignatureNull) {
      deParamspec paramspec = deSignatureGetiParamspec(
          deCurrentSignature, findParamIndex(variable));
      deParamspecSetInstantiated(paramspec, true);
    }
  }
}

// Bind the array expression.
static void bindArrayExpression(deBlock scopeBlock, deExpression expression) {
  deLine line = deExpressionGetLine(expression);
  deExpression firstElement = deExpressionGetFirstExpression(expression);
  bindExpression(scopeBlock, firstElement);
  deDatatype datatype = getDatatype(firstElement);
  deExpression nextElement = deExpressionGetNextExpression(firstElement);
  while (nextElement != deExpressionNull) {
    bindExpression(scopeBlock, nextElement);
    deDatatype elementType = getDatatype(nextElement);
    if (elementType != datatype &&
        deDatatypeGetType(datatype) == deDatatypeGetType(elementType) &&
        (deDatatypeNullable(datatype) || deDatatypeNullable(elementType))) {
      datatype = deSetDatatypeNullable(datatype, true, line);
      elementType = deSetDatatypeNullable(elementType, true, line);
    }
    if (elementType != datatype) {
      deError(line, "Array elements must have the same type:%s",
          deGetOldVsNewDatatypeStrings(getDatatype(nextElement), datatype));
    }
    if (deExpressionIsType(nextElement)) {
      deError(line, "Array type expressions can contain only one type, like [u32]");
    }
    nextElement = deExpressionGetNextExpression(nextElement);
  }
  deDatatype arrayDatatype = deArrayDatatypeCreate(datatype);
  deExpressionSetDatatype(expression, arrayDatatype);
  deExpressionSetIsType(expression, deExpressionIsType(firstElement));
}

// Modify the datatype in the constant integer expression tree to match the
// datatype.
static void autocastExpression(deExpression expression, deDatatype datatype) {
  deDatatype oldDatatype = deExpressionGetDatatype(expression);
  if (!deDatatypeIsInteger(oldDatatype) || !deDatatypeIsInteger(datatype)) {
    return;  // We only auto-cast integers without type specifiers to integers.
  }
  deExpressionSetDatatype(expression, datatype);
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    autocastExpression(child, datatype);
  }
}

// Return true if the types are the same, other than for their secret bit.
static bool typesAreEquivalent(deDatatype type1, deDatatype type2) {
  return deSetDatatypeSecret(type1, false) == deSetDatatypeSecret(type2, false);
}

// Bind a binary expression, returning the datatypes of the left and right
// sub-expressions.
static void bindBinaryExpression(deBlock scopeBlock, deExpression expression,
    deDatatype* leftType, deDatatype* rightType, bool compareTypes) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  bindExpression(scopeBlock, left);
  bindExpression(scopeBlock, right);
  *leftType = getDatatype(left);
  *rightType = getDatatype(right);
  if (compareTypes && !typesAreEquivalent(*leftType, *rightType)) {
    // Try auto-cast.
    if (deExpressionAutocast(left) && !deExpressionAutocast(right)) {
      autocastExpression(left, *rightType);
      *leftType = deExpressionGetDatatype(left);
    } else if (deExpressionAutocast(right) && !deExpressionAutocast(left)) {
      autocastExpression(right, *leftType);
      *rightType = deExpressionGetDatatype(right);
    }
  }
  if (compareTypes && deExpressionAutocast(left) && deExpressionAutocast(right)) {
    deExpressionSetAutocast(expression, true);
  }
}

// Verify the datatype can be cast to a modular integer.  This just means it is INT or UINT.
static void verifyExpressionCanCastToModint(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (!deDatatypeIsInteger(datatype)) {
    deError(deExpressionGetLine(expression), "Expression cannot be cast to a modular integer");
  }
}

// Bind a modular expression, which is built from modular arithmetic friendly operators.  Only
// modular operators such as add/sub/exp expressions are set to |modularType|.
static void bindModularExpression(deBlock scopeBlock, deExpression expression,
    deDatatype modularType) {
  deLine line = deExpressionGetLine(expression);
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_INTEGER:
    case DE_EXPR_IDENT:
    case DE_EXPR_RANDUINT:
    case DE_EXPR_CAST:
    case DE_EXPR_CALL:
    case DE_EXPR_INDEX:
    case DE_EXPR_DOT:
    case DE_EXPR_WIDTHOF:
      // These are non-modular operators that are legal in modular expressions.
      // Bind them, and verify they can be cast to the modular type.  The cast
      // will be done in the assembly generators.
      bindExpression(scopeBlock, expression);
      verifyExpressionCanCastToModint(expression);
      break;
    case DE_EXPR_ADD:
    case DE_EXPR_SUB:
    case DE_EXPR_MUL:
    case DE_EXPR_DIV: {
      deExpression left = deExpressionGetFirstExpression(expression);
      deExpression right = deExpressionGetNextExpression(left);
      bindModularExpression(scopeBlock, left, modularType);
      bindModularExpression(scopeBlock, right, modularType);
      deExpressionSetDatatype(expression, modularType);
      break;
    }
    case DE_EXPR_EXP: {
      deExpression left = deExpressionGetFirstExpression(expression);
      deExpression right = deExpressionGetNextExpression(left);
      bindModularExpression(scopeBlock, left, modularType);
      bindExpression(scopeBlock, right);
      if (deDatatypeGetType(deExpressionGetDatatype(right)) != DE_TYPE_UINT) {
        deError(line, "Modular exponents most be uint");
      }
      deExpressionSetDatatype(expression, modularType);
      break;
    }
    case DE_EXPR_REVEAL:
    case DE_EXPR_SECRET:
    case DE_EXPR_NEGATE: {
      deExpression left = deExpressionGetFirstExpression(expression);
      bindModularExpression(scopeBlock, left, modularType);
      deExpressionSetDatatype(expression, modularType);
      break;
    }
    case DE_EXPR_EQUAL:
    case DE_EXPR_NOTEQUAL: {
      deExpression left = deExpressionGetFirstExpression(expression);
      deExpression right = deExpressionGetNextExpression(left);
      bindModularExpression(scopeBlock, left, modularType);
      bindModularExpression(scopeBlock, right, modularType);
      deExpressionSetDatatype(expression, deBoolDatatypeCreate());
      return;
    }
    default:
      deError(line, "Invalid modular arithmetic expression");
  }
  deExpressionSetDatatype(expression, modularType);
}

// Bind a modular integer expression.  Adding "mod p" after an expression forces
// all of the expressions to the left to be computed mod p.
static void bindModintExpression(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  bindExpression(scopeBlock, right);
  deDatatype modulusType = getDatatype(right);
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeGetType(modulusType) != DE_TYPE_UINT) {
    deError(line, "Modulus must be an unsigned integer");
  }
  if (deDatatypeSecret(modulusType)) {
    deError(line, "Modulus cannot be secret");
  }
  deDatatype datatype;
  datatype = deModintDatatypeCreate(right);
  bindModularExpression(scopeBlock, left, datatype);
  deDatatype resultType = deExpressionGetDatatype(left);
  if (deDatatypeGetType(resultType) == DE_TYPE_MODINT) {
    resultType = modulusType;
  }
  deExpressionSetDatatype(expression, resultType);
}

// Set parameter datatypes to those in the datatype array.  Save existing
// parameter types.
static void setParameterDatatypes(deBlock block, deDatatypeArray parameterTypes) {
  deVariable variable;
  uint32 numParams = deDatatypeArrayGetUsedDatatype(parameterTypes);
  uint32 xParam = 0;
  deForeachBlockVariable(block, variable) {
    if (deVariableGetType(variable) != DE_VAR_PARAMETER || xParam >= numParams) {
      return;
    }
    deVariableSetSavedDatatype(variable, deVariableGetDatatype(variable));
    deVariableSetDatatype(variable, deDatatypeArrayGetiDatatype(parameterTypes, xParam));
    xParam++;
  } deEndBlockVariable;
}

// Restore parameter datatypes to what they were before.
static void unsetParameterDatatypes(deBlock block, deDatatypeArray parameterTypes) {
  deVariable variable;
  uint32 numParams = deDatatypeArrayGetUsedDatatype(parameterTypes);
  uint32 xParam = 0;
  deForeachBlockVariable(block, variable) {
    if (deVariableGetType(variable) != DE_VAR_PARAMETER || xParam >= numParams) {
      return;
    }
    deVariableSetDatatype(variable, deVariableGetSavedDatatype(variable));
    xParam++;
  } deEndBlockVariable;
}

// Determine if the expression matches the overloaded operator.
static bool parameterTypesMatchesOverload(deDatatypeArray parameterTypes, deFunction function) {
  uint32 xParam = 0;
  deBlock block = deFunctionGetSubBlock(function);
  setParameterDatatypes(block, parameterTypes);
  uint32 numParams = deDatatypeArrayGetUsedDatatype(parameterTypes);
  deVariable parameter;
  deForeachBlockVariable(block, parameter) {
    if (deVariableGetType(parameter) != DE_VAR_PARAMETER || xParam == numParams) {
      return deVariableGetType(parameter) != DE_VAR_PARAMETER && xParam == numParams;
    }
    deDatatype datatype = deDatatypeArrayGetiDatatype(parameterTypes, xParam);
    // Set the parameter's datatype in case it is used in a type constraint.
    deVariableSetDatatype(parameter, datatype);
    deExpression typeExpression = deVariableGetTypeExpression(parameter);
    if (typeExpression != deExpressionNull &&
        !deDatatypeMatchesTypeExpression(block, datatype, typeExpression)) {
      unsetParameterDatatypes(block, parameterTypes);
      return false;
    }
    xParam++;
  } deEndBlockVariable;
  unsetParameterDatatypes(block, parameterTypes);
  return xParam == numParams;
}

// Forward declaration for recursion.
static deDatatype bindFunctionCall(deBlock scopeBlock, deFunction function,
          deExpression expression, deExpression parameters, deDatatype selfType,
          deDatatypeArray parameterTypes, bool fromFuncPtrExpr);

// Find a matching operator overload.
static deFunction findMatchingOperatorOverload(deBlock scopeBlock, deExpression expression,
      deDatatypeArray parameterTypes) {
  deLine line = deExpressionGetLine(expression);
  deExpressionType opType = deExpressionGetType(expression);
  if (opType == DE_EXPR_NEGATE) {
    opType = DE_EXPR_SUB;
  }
  if (opType == DE_EXPR_NEGATETRUNC) {
    opType = DE_EXPR_SUBTRUNC;
  }
  deOperator operator = deRootFindOperator(deTheRoot, opType);
  if (operator == deOperatorNull) {
    return deFunctionNull;
  }
  deFunction function;
  deFunction operatorFunc = deFunctionNull;
  deForeachOperatorFunction(operator, function) {
    if (parameterTypesMatchesOverload(parameterTypes, function)) {
      if (operatorFunc != deFunctionNull) {
        deError(line, "Ambiguous overload of operator '%s'", deOperatorGetName(operator));
      }
      operatorFunc = function;
    }
  } deEndOperatorFunction;
  return operatorFunc;
}

// Look for an overloaded operator matching this expression's signature, and if
// one is found, bind to it.  Create a signature for the call to the operator
// overload.
static bool bindOverloadedOperator(deBlock scopeBlock, deExpression expression) {
  // Parameters are already bound.
  deDatatypeArray parameterTypes = deDatatypeArrayAlloc();
  deExpression parameter;
  deForeachExpressionExpression(expression, parameter) {
    deDatatype datatype = getDatatype(parameter);
    deDatatypeArrayAppendDatatype(parameterTypes, datatype);
  } deEndExpressionExpression;
  deFunction operatorFunc = findMatchingOperatorOverload(scopeBlock, expression, parameterTypes);
  if (operatorFunc == deFunctionNull) {
    deDatatypeArrayFree(parameterTypes);
    return false;
  }
  deDatatype datatype = bindFunctionCall(scopeBlock, operatorFunc, expression,
      expression, deDatatypeNull, parameterTypes, false);
  deExpressionSetDatatype(expression, datatype);
  deDatatypeArrayFree(parameterTypes);
  return true;
}

// Bind a binary arithmetic expression.  The left and right types should have
// the same numeric type, resulting in the same type.
static void bindBinaryArithmeticExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, true);
  if (bindOverloadedOperator(scopeBlock, expression)) {
    return;
  }
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  deLine line = deExpressionGetLine(expression);
  if (leftType != rightType) {
    deError(line, "Non-equal types passed to binary operator");
  }
  // Allow addition on strings and arrays.
  deDatatypeType type = deDatatypeGetType(leftType);
  deExpressionType exprType = deExpressionGetType(expression);
  if ((type != DE_TYPE_ARRAY || exprType != DE_EXPR_ADD) &&
      (type != DE_TYPE_STRING || (exprType != DE_EXPR_ADD && exprType != DE_EXPR_BITXOR)) &&
      !deDatatypeIsInteger(leftType) && type != DE_TYPE_FLOAT) {
    deError(line, "Invalid types for binary arithmetic operator");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind and AND, OR, or XOR operator.  If operating on numbers, bitwise
// operators are used.  If operating on Boolean values, logical operators are
// used.
static void bindBinaryBoolOrArithmeticExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, true);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeGetType(leftType) != DE_TYPE_BOOL ||
      deDatatypeGetType(rightType) != DE_TYPE_BOOL) {
    deError(line, "Non-Boolean types passed to Boolean operator");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a bitwise OR expression.  This is different from the other bitwise
// operators because it also used in type unions, such as "a: Uint | Int".
static void bindBitwiseOrExpression(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  if (deExpressionIsType(left)) {
    deLine line = deExpressionGetLine(expression);
    if (!deExpressionIsType(right)) {
      deError(line, "Non-equal types passed to binary operator");
    }
    deExpressionSetIsType(expression, true);
    deExpressionSetDatatype(expression, deNoneDatatypeCreate());
  } else {
    bindBinaryArithmeticExpression(scopeBlock, expression);
  }
}

// Verify the toString method, or generate of if it does not exist.  Insert a
// call to the toSting() method.
static deExpression verifyOrGenerateToStringMethod(deBlock scopeBlock, deExpression selfExpr) {
  deLine line = deExpressionGetLine(selfExpr);
  deClass theClass = deDatatypeGetClass(deExpressionGetDatatype(selfExpr));
  utSym toStringSym = utSymCreate("toString");
  deFunction toStringMethod = deClassFindMethod(theClass, toStringSym);
  if (toStringMethod == deFunctionNull) {
    toStringMethod = deGenerateDefaultToStringMethod(theClass);
  }
  deExpression callExpr = deExpressionCreate(DE_EXPR_CALL, line);
  deExpression listExpr = deExpressionGetExpression(selfExpr);
  deExpressionInsertAfterExpression(listExpr, selfExpr, callExpr);
  deExpressionRemoveExpression(listExpr, selfExpr);
  deExpression identExpr = deIdentExpressionCreate(toStringSym, line);
  deExpression dotExpr = deBinaryExpressionCreate(DE_EXPR_DOT, selfExpr, identExpr, line);
  deExpressionAppendExpression(callExpr, dotExpr);
  deExpression paramsExpr = deExpressionCreate(DE_EXPR_LIST, line);
  deExpressionAppendExpression(callExpr, paramsExpr);
  bindExpression(scopeBlock, callExpr);
  return callExpr;
}

// Verify the expression can be printed.  For example, tuples cannot be printed
// because the compiled program does not have access to the subtype info.
static deExpression checkExpressionIsPrintable(deBlock scopeBlock, deExpression
    expression, bool convertClassesToStrings) {
  deLine line = deExpressionGetLine(expression);
  deDatatype datatype = deExpressionGetDatatype(expression);
  switch (deDatatypeGetType(datatype)) {
  case DE_TYPE_NONE:
    deError(line, "Null type in print argument list");
    break;
  case DE_TYPE_BOOL:
  case DE_TYPE_STRING:
  case DE_TYPE_UINT:
  case DE_TYPE_INT:
  case DE_TYPE_FLOAT:
  case DE_TYPE_TUPLE:
  case DE_TYPE_STRUCT:
  case DE_TYPE_ENUMCLASS:
  case DE_TYPE_ENUM:
  case DE_TYPE_ARRAY:
  case DE_TYPE_NULL:
  case DE_TYPE_TCLASS:
    break;
  case DE_TYPE_MODINT:
    utExit("Modint type at top level expression");
    break;
  case DE_TYPE_CLASS:
    if (convertClassesToStrings) {
      return verifyOrGenerateToStringMethod(scopeBlock, expression);
    }
    break;
  case DE_TYPE_FUNCTION:
  case DE_TYPE_FUNCPTR:
    deError(line, "Cannot print function pointers");
    break;
  }
  return expression;
}

// Read a uint16 from the string.  Update the string pointer to point to first
// non-digit.  Given an error if the value does not fit in a uint16.
static uint16 readUint16(char **p, char *end, deLine line) {
  if (*p == end) {
    return 0;
  }
  char *q = *p;
  uint32 value = 0;
  char c = *q;
  if (!isdigit(c)) {
    return 0;
  }
  do {
    value = value * 10 + c - '0';
    if (value > UINT16_MAX) {
      deError(line, "Integer width cannot exceed 2^16 - 1");
    }
    c = *++q;
  } while (isdigit(c) && q != end);
  *p = q;
  return value;
}

// Verify the format specifier matches the datatype.
static char* verifyFormatSpecifier(char* p, char *end, deDatatype datatype, deLine line,
    char **buf, uint32 *len, uint32 *pos) {
  if (deDatatypeGetType(datatype) == DE_TYPE_ENUM) {
    deBlock enumBlock = deFunctionGetSubBlock(deDatatypeGetFunction(datatype));
    datatype = deFindEnumIntType(enumBlock);
  } else if (deDatatypeGetType(datatype) == DE_TYPE_CLASS) {
    uint32 width = deDatatypeGetWidth(datatype);
    utAssert(width != 0);
    datatype = deUintDatatypeCreate(width);
  }
  deDatatypeType type = deDatatypeGetType(datatype);
  char c = *p++;
  *buf = deAppendCharToBuffer(*buf, len, pos, c);
  if (c == 's') {
    if (type != DE_TYPE_STRING) {
      deError(line, "Expected String argument");
    }
  } else if (c == 'i' || c == 'u' || c == 'x' || c == 'f') {
    if ((c == 'i' && type != DE_TYPE_INT)) {
      deError(line, "Expected Int argument");
    } else if ((c == 'u' && type != DE_TYPE_UINT)) {
      deError(line, "Expected Uint argument");
    } else if (c == 'x' && type != DE_TYPE_INT && type != DE_TYPE_UINT) {
      deError(line, "Expected Int or Uint argument");
    } else if (c == 'f' && type != DE_TYPE_FLOAT) {
      deError(line, "Expected Int or Uint argument");
    }
    uint32 width = deDatatypeGetWidth(datatype);
    uint16 specWidth = readUint16(&p, end, line);
    if (specWidth != 0 && width != specWidth) {
      deError(line, "Specified width does not match argument");
    }
    *buf = deAppendToBuffer(*buf, len, pos, utSprintf("%u", width));
  } else if (c == 'f') {
    if (type != DE_TYPE_FLOAT) {
      deError(line, "Expected float argument");
    }
  } else if (c == 'b') {
    if (type != DE_TYPE_BOOL) {
      deError(line, "Expected bool argument");
    }
  } else if (c == '[') {
    if (type != DE_TYPE_ARRAY) {
      deError(line, "Expected array argument");
    }
    deDatatype elementType = deDatatypeGetElementType(datatype);
    p = verifyFormatSpecifier(p, end, elementType, line, buf, len, pos);
    c = *p++;
    *buf = deAppendCharToBuffer(*buf, len, pos, c);
    if (c != ']') {
      deError(line, "Expected ']' to end array format specifier");
    }
  } else if (c == '(') {
    if (type != DE_TYPE_TUPLE) {
      deError(line, "Expected tuple argument");
    }
    for (uint32 i = 0; i < deDatatypeGetNumTypeList(datatype); i++) {
      deDatatype elementType = deDatatypeGetiTypeList(datatype, i);
      p = verifyFormatSpecifier(p, end, elementType, line, buf, len, pos);
      if (i + 1 != deDatatypeGetNumTypeList(datatype)) {
        c = *p++;
        *buf = deAppendCharToBuffer(*buf, len, pos, c);
        if (c != ',') {
          deError(line, "Expected ',' between tuple element specifiers.");
        }
      }
    }
    c = *p++;
    *buf = deAppendCharToBuffer(*buf, len, pos, c);
    if (c != ')') {
      deError(line, "Expected ')' to end tuple format specifier");
    }
  } else {
    deError(line, "Unsupported format specifier: %c", c);
  }
  return p;
}

// Verify the printf parameters are valid.  Currently, we support:
//
//   %b        - Match an bool value: prints true or false
//   %i<width> - Match an Int value
//   %u<width> - Match a Uint value
//   %f        - Match a Float value
//   %s - match a string value
//   %x<width> - Match an Int or Uint value, print in lower-case-hex
//
// Escapes can be \" \\ \n, \t, \a, \b, \e,\f, \r \v, or \xx, where xx is a hex
// encoding of the byte.
//
// Generate a new format specifier that includes widths, since widths are
// optional.
// TODO: Add support for format modifiers, e.g. %12s, %-12s, %$1d, %8d, %08u...
static void verifyPrintfParameters(deBlock scopeBlock, deExpression expression) {
  uint32 len = 42;
  uint32 pos = 0;
  char *buf = utMakeString(len);
  deLine line = deExpressionGetLine(expression);
  deExpression format = deExpressionGetFirstExpression(expression);
  deExpression argument = deExpressionGetNextExpression(format);
  bool isTuple = false;
  if (deDatatypeGetType(deExpressionGetDatatype(argument)) == DE_TYPE_TUPLE) {
    isTuple = true;
    argument = deExpressionGetFirstExpression(argument);
  }
  if (deDatatypeGetType(deExpressionGetDatatype(format)) != DE_TYPE_STRING) {
    deError(line, "Format specifier must be a constant string.\n");
  }
  deString string = deExpressionGetString(format);
  char *p = deStringGetText(string);
  char *end = p + deStringGetNumText(string);
  while (p < end) {
    char c = *p++;
    buf = deAppendCharToBuffer(buf, &len, &pos, c);
    if (c == '\\') {
      c = *p++;
      buf = deAppendCharToBuffer(buf, &len, &pos, c);
      if (p >= end) {
        deError(line, "Incomplete escape sequence");
      }
      if (c == 'x') {
        for (uint32 i = 0; i < 2; i++) {
          c = *p++;
          buf = deAppendCharToBuffer(buf, &len, &pos, c);
          if (p >= end) {
            deError(line, "Incomplete escape sequence");
          }
          if (!isxdigit(c)) {
            deError(line, "Invalid hex escape: should be 2 hex digits");
          }
        }
      } else if (c != '\\' && c != '"' && c != 'n' && c != 't' && c != 'a' &&
            c != 'b' && c != 'e' && c != 'f' && c != 'r' && c != 'v') {
        deError(line, "Invalid escape sequence '\\%c'", c);
      }
    } else if (c == '%') {
      if (argument == deExpressionNull) {
        deError(line, "Too few arguments for format");
      }
      argument = checkExpressionIsPrintable(scopeBlock, argument, false);
      deDatatype datatype = deExpressionGetDatatype(argument);
      p = verifyFormatSpecifier(p, end, datatype, line, &buf, &len, &pos);
      if (isTuple) {
        argument = deExpressionGetNextExpression(argument);
      } else {
        argument = deExpressionNull;
      }
    }
  }
  if (argument != deExpressionNull) {
    deError(line, "Too many arguments for format");
  }
  deExpressionSetAltString(format, deMutableCStringCreate(buf));
}

// The % operator is overloaded: two integer types or a string on the left and
// tuple on the right.  This results in sprintf(left, members of tuple...),
// returning a string.
static void bindModExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  if (bindOverloadedOperator(scopeBlock, expression)) {
    return;
  }
  deLine line = deExpressionGetLine(expression);
  deDatatypeType type = deDatatypeGetType(leftType);
  if (deDatatypeTypeIsInteger(type) || type == DE_TYPE_FLOAT) {
    if (!typesAreEquivalent(leftType, rightType)) {
      deError(line, "Non-equal types passed to binary operator");
    }
    deExpressionSetDatatype(expression, leftType);
    return;
  }
  if (deDatatypeGetType(leftType) != DE_TYPE_STRING) {
    deError(line, "Invalid left operand type for %% operator");
  }
  verifyPrintfParameters(scopeBlock, expression);
  deDatatype datatype = deStringDatatypeCreate();
  if (deDatatypeSecret(leftType) || deDatatypeSecret(rightType)) {
    datatype = deSetDatatypeSecret(datatype, true);
  }
  deExpressionSetDatatype(expression, datatype);
}

// Bind an exponentiation expression.  Exponent must be a non-secret uint, while
// the base can be a uint or modint.
static void bindExponentiationExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  if (bindOverloadedOperator(scopeBlock, expression)) {
    return;
  }
  deLine line = deExpressionGetLine(expression);
  if (!deDatatypeIsInteger(leftType)) {
    deError(line, "Base of exponentiation operator must be uint or modint");
  }
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deError(line, "Exponent must be a uint");
  }
  if (deDatatypeSecret(rightType)) {
    deError(line, "Exponent cannot be secret");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a shift/rotate expression.  The distance must be a uint.  The value
// being shifted (left operand) must be an integer.
static void bindShiftExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  if (bindOverloadedOperator(scopeBlock, expression)) {
    return;
  }
  deLine line = deExpressionGetLine(expression);
  if (!deDatatypeIsInteger(leftType)) {
    deError(line, "Only integers can be shifted/rotated");
  }
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deError(line, "Shift/rotate distance must be a uint");
  }
  if (deDatatypeSecret(rightType)) {
    deError(line, "Shift/rotate distance cannot be secret");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a relational operator.  Both operands must be strings, arrays, or integers.
static void bindRelationalExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, true);
  if (bindOverloadedOperator(scopeBlock, expression)) {
    return;
  }
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  deLine line = deExpressionGetLine(expression);
  if (!typesAreEquivalent(leftType, rightType)) {
    deError(line, "Non-equal types passed to relational operator:%s",
        deGetOldVsNewDatatypeStrings(leftType, rightType));
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_UINT && type != DE_TYPE_INT && type != DE_TYPE_FLOAT &&
      type != DE_TYPE_STRING && type != DE_TYPE_ARRAY) {
    deError(line, "Invalid types passed to relational operator");
  }
  bool secret = deDatatypeSecret(leftType) || deDatatypeSecret(rightType);
  deExpressionSetDatatype(expression, deSetDatatypeSecret(deBoolDatatypeCreate(), secret));
}

// Bind a relational operator.  Both operands must be integers.
static void bindEqualityExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, true);
  if (bindOverloadedOperator(scopeBlock, expression)) {
    return;
  }
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (leftType != rightType) {
    deDatatype unifiedType = deUnifyDatatypes(leftType, rightType);
    if (unifiedType == deDatatypeNull) {
      deError(line, "Non-equal types passed to relational operator:%s",
          deGetOldVsNewDatatypeStrings(leftType, rightType));
    }
  }
  deExpressionSetDatatype(expression,
      deSetDatatypeSecret(deBoolDatatypeCreate(), deDatatypeSecret(leftType)));
}

// Bind a unary expression, returning the datatype of the child.
static deDatatype bindUnaryExpression(deBlock scopeBlock, deExpression expression) {
  deExpression child = deExpressionGetFirstExpression(expression);
  bindExpression(scopeBlock, child);
  return getDatatype(child);
}

// Bind a negate expression.  The operand must be an integer.
static void bindUnaryArithmeticExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype childType = bindUnaryExpression(scopeBlock, expression);
  if (bindOverloadedOperator(scopeBlock, expression)) {
    return;
  }
  deLine line = deExpressionGetLine(expression);
  if (!deDatatypeIsInteger(childType) && !deDatatypeIsFloat(childType)) {
    deError(line, "Only integers can be negated");
  }
  deExpressionSetDatatype(expression, childType);
  deExpression child = deExpressionGetFirstExpression(expression);
  deExpressionSetAutocast(expression, deExpressionAutocast(child));
}

// Bind a not expression.  It does logical not on Boolean operands, and
// complement on integer operands.
static void bindNotExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype childType = bindUnaryExpression(scopeBlock, expression);
  if (bindOverloadedOperator(scopeBlock, expression)) {
    return;
  }
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeGetType(childType) != DE_TYPE_BOOL) {
    deError(line, "Not operator only works on Boolean types");
  }
  deExpressionSetDatatype(expression, childType);
}

// Determine if the datatype is a number or enumerated value.
static bool datatypeIsNumberOrEnum(deDatatypeType type) {
  return deDatatypeTypeIsNumber(type) || type == DE_TYPE_ENUM;
}

// Determine if the datatype is a number or enumerated value.
static bool datatypeIsNumberOrEnumClass(deDatatypeType type) {
  return deDatatypeTypeIsNumber(type) || type == DE_TYPE_ENUMCLASS || type == DE_TYPE_ENUM;
}

// Verify the cast expression is valid, and return the resulting datatype.
static void verifyCast(deDatatype leftDatatype, deDatatype rightDatatype, deLine line) {
  if (leftDatatype == rightDatatype) {
    return;  // The cast is a nop.
  }
  if (leftDatatype == deDatatypeNull) {
    deError(line, "Casts require qualified types");
  }
  if (deDatatypeGetType(leftDatatype) == DE_TYPE_CLASS &&
      deDatatypeGetType(rightDatatype) == DE_TYPE_NULL) {
    // This looks like a type binding hint.
    if (deClassGetTclass(deDatatypeGetClass(leftDatatype)) != deDatatypeGetTclass(rightDatatype)) {
      deError(line, "Casting to different class types is not allowed.");
    }
    return;
  }
  deDatatypeType leftType = deDatatypeGetType(leftDatatype);
  deDatatypeType rightType = deDatatypeGetType(rightDatatype);
  if (leftType == DE_TYPE_CLASS && rightType == DE_TYPE_CLASS &&
      deDatatypeGetClass(leftDatatype) == deDatatypeGetClass(rightDatatype) &&
      deSetDatatypeNullable(leftDatatype, true, line) ==
      deSetDatatypeNullable(rightDatatype, true, line)) {
    return;
  }
  if (datatypeIsNumberOrEnumClass(leftType) && datatypeIsNumberOrEnum(rightType)) {
    return;
  }
  if (deDatatypeTypeIsInteger(rightType) ||
      (!deDatatypeTypeIsInteger(leftType) && rightType == DE_TYPE_STRING)) {
    // Swap datatypes so the non-array is the left type.
    deDatatype tempDatatype = leftDatatype;
    leftDatatype = rightDatatype;
    rightDatatype = tempDatatype;
    leftType = deDatatypeGetType(leftDatatype);
    rightType = deDatatypeGetType(rightDatatype);
  }
  if (!deDatatypeTypeIsInteger(leftType) && leftType != DE_TYPE_STRING) {
    deError(line, "Invalid cast: only casting from/to integers and from/to string are allowed");
  }
  if (leftType == DE_TYPE_STRING) {
    if (rightType != DE_TYPE_ARRAY ||
        deDatatypeGetType(deDatatypeGetElementType(rightDatatype)) !=
            DE_TYPE_UINT) {
      deError(line, "Invalid string conversion.  Only conversions from/to [u8] are allowed.");
    }
    return;
  }
  if (rightType == DE_TYPE_ARRAY) {
    deDatatype elementDatatype = deDatatypeGetElementType(rightDatatype);
    if (deDatatypeGetType(elementDatatype) != DE_TYPE_UINT) {
      deError(line, "Invalid cast: can only convert from/to uint arrays");
    }
    return;
  }
  if (!deDatatypeTypeIsInteger(rightType) && rightType != DE_TYPE_CLASS) {
    deError(line, "Invalid cast");
  }
  if (rightType  == DE_TYPE_CLASS) {
    // Verify the integer width matches the class reference width.
    deClass theClass = deDatatypeGetClass(rightDatatype);
    if (deDatatypeGetWidth(leftDatatype) != deClassGetRefWidth(theClass)) {
      deError(line, "Invalid cast: integer width does not match class reference width");
    }
  }
}

// Bind a cast expression.  Various conversions are allowed.  For example:
//
//   <u32>10i16
//   <u32[]>rsaKey
//   <u8[]>"Hello, World!",
//   <string>[0x74u8, 0x65u8, 0x73u8, 0x74u8]
//   <u32[]>(123u255 mod p)
//   <u32[]>"I will be converted to an array of u32, little-endian"
//   <u16> 0xdeadbeefu32  // Error!
//   <u32>-1u32  // Error!  -1 is not the same number as 0xffffffff.
//   <u8[]>[1u32, 2u32, 3u32]  // Results in a 12-byte array.
//   <self>0u32  // Same as null(self)
//   objectIndex = <u32>object  // Convert an object reference to an integer.
//
// Integers are converted little-endian.  An exception is thrown if a conversion
// results in data loss.
static void bindCastExpression(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  bool savedInstantiating = deInstantiating;
  deInstantiating = false;
  bindExpression(scopeBlock, left);
  deInstantiating = savedInstantiating;
  bindExpression(scopeBlock, right);
  deDatatype leftDatatype = deExpressionGetDatatype(left);
  deDatatype rightDatatype = deExpressionGetDatatype(right);
  deLine line = deExpressionGetLine(expression);
  // We ignore the secrecy of the left type: you can't cast away secrecy.  Just
  // force the left type to have the same secrecy value as the right.
  leftDatatype = deSetDatatypeSecret(leftDatatype, deDatatypeSecret(rightDatatype));
  if (deDatatypeGetType(leftDatatype) == DE_TYPE_CLASS) {
    leftDatatype = deSetDatatypeNullable(
        leftDatatype, deDatatypeNullable(rightDatatype), deLineNull);
  }
  if (deDatatypeGetType(leftDatatype) == DE_TYPE_ENUMCLASS) {
    // If the cast is to an ENUMCLASS, instead cast to an ENUM.
    deBlock enumBlock = deFunctionGetSubBlock(deDatatypeGetFunction(leftDatatype));
    leftDatatype = deFindEnumIntType(enumBlock);
  }
  verifyCast(leftDatatype, rightDatatype, line);
  deExpressionSetDatatype(expression, leftDatatype);
}

// Bind a select expression.  The selector must be Boolean, and the two data
// values must have the same type.
static void bindSelectExpression(deBlock scopeBlock, deExpression expression) {
  deExpression select = deExpressionGetFirstExpression(expression);
  deExpression left = deExpressionGetNextExpression(select);
  deExpression right = deExpressionGetNextExpression(left);
  bindExpression(scopeBlock, select);
  bindExpression(scopeBlock, left);
  bindExpression(scopeBlock, right);
  deDatatype selectType = getDatatype(select);
  deDatatype leftType = getDatatype(left);
  deDatatype rightType = getDatatype(right);
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (deDatatypeGetType(selectType) != DE_TYPE_BOOL) {
    deError(line, "Select must be Boolean");
  }
  if (leftType != rightType) {
    deError(line, "Select operator applied to different data types:%s",
        deGetOldVsNewDatatypeStrings(leftType, rightType));
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a list of expressions.
static void bindExpressionList(deBlock scopeBlock, deExpression expressionList) {
  deExpression child;
  deForeachExpressionExpression(expressionList, child) {
    bindExpression(scopeBlock, child);
  } deEndExpressionExpression;
  deExpressionSetDatatype(expressionList, deNoneDatatypeCreate());
}

// Bind a parameter list.  The only special case here is that null is allowed
// without parameters, meaning null(self), where self is the self added to the
// constructor call.
static void bindParameterList(deBlock scopeBlock, deExpression parameterList) {
  deExpression child;
  deForeachExpressionExpression(parameterList, child) {
    bindExpression(scopeBlock, child);
  }
  deEndExpressionExpression;
  deExpressionSetDatatype(parameterList, deNoneDatatypeCreate());
}

// Compare the parameter types to the function pointer parameter types from the
// call type.  Report an error on mismatch.
static void compareFuncptrParameters(deDatatype callType,
                                     deDatatypeArray parameterTypes,
                                     deLine line) {
  uint32 numParameters = deDatatypeGetNumTypeList(callType);
  if (deDatatypeArrayGetUsedDatatype(parameterTypes) != numParameters) {
    deError(line, "Wrong number of parameters to function call: Expected %u, have %u",
            numParameters, deDatatypeArrayGetUsedDatatype(parameterTypes));
  }
  for (uint32 i = 0; i < numParameters; i++) {
    if (deDatatypeGetiTypeList(callType, i) !=
        deDatatypeArrayGetiDatatype(parameterTypes, i)) {
      deError( line, "Incorrect type passed in argument %u", i);
    }
  }
}

// Check that the parameter types match their constraints.
static void checkParameterTypeConstraints(deBlock block, deLine line) {
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    if (deVariableGetType(variable) != DE_VAR_PARAMETER) {
      return;
    }
    checkVariableDatatype(block, variable, line);
  } deEndBlockVariable
}

// Verify that all named parameters match parameter variables on the block.
static void verifyNamedParametersMatch(deBlock block, deExpression firstNamedParameter) {
  for (deExpression parameter = firstNamedParameter; parameter != deExpressionNull;
       parameter = deExpressionGetNextExpression(parameter)) {
    deLine line = deExpressionGetLine(parameter);
    utSym name = deExpressionGetName(deExpressionGetFirstExpression(parameter));
    deIdent ident = deBlockFindIdent(block, name);
    if (ident == deIdentNull || deIdentGetType(ident) != DE_IDENT_VARIABLE) {
      deError(line, "Parameter %s not found in %s", utSymGetName(name),
          deGetBlockPath(block, false));
    }
    deVariable var = deIdentGetVariable(ident);
    if (deVariableGetType(var) != DE_VAR_PARAMETER) {
      deError(line, "Parameter %s not found in %s", utSymGetName(name),
          deGetBlockPath(block, false));
    }
  }
}

// Restore variable datatypes for variables on the block.
static void restoreParameterDatatypes(deBlock block) {
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    if (deVariableGetType(variable) != DE_VAR_PARAMETER) {
      return;
    }
    deVariableSetDatatype(variable, deVariableGetSavedDatatype(variable));
  } deEndBlockVariable;
}

// Fill out the parameter types passed to the function using default parameter values.
static void fillOutDefaultParamters(deBlock block, deDatatype selfType, deDatatypeArray paramTypes,
    deExpression firstNamedParameter, deLine line) {
  verifyNamedParametersMatch(block, firstNamedParameter);
  deVariable variable = deBlockGetFirstVariable(block);
  uint32 xDatatype = 0;  // Index into signature datatypes.
  deFunctionType funcType = deFunctionGetType(deBlockGetOwningFunction(block));
  while (variable != deVariableNull && deVariableGetType(variable) == DE_VAR_PARAMETER) {
    deVariableSetSavedDatatype(variable, deVariableGetDatatype(variable));
    if (xDatatype == deDatatypeArrayGetUsedDatatype(paramTypes)) {
      // We've past the positional parameters.  Only named parameters remain.
      deExpression defaultValue = deVariableGetInitializerExpression(variable);
      if (defaultValue == deExpressionNull) {
        deError(line, "Not enough parameters");
      }
      deExpression namedParameter = deFindNamedParameter(
          firstNamedParameter, deVariableGetSym(variable));
      deDatatype varDatatype;
      if (namedParameter != deExpressionNull) {
        varDatatype = deExpressionGetDatatype(namedParameter);
      } else {
        // Use the default value.
        bindExpression(block, defaultValue);
        varDatatype = deExpressionGetDatatype(defaultValue);
      }
      deVariableSetDatatype(variable, varDatatype);
      deDatatypeArrayAppendDatatype(paramTypes, deVariableGetDatatype(variable));
    } else {
      // Still using positional parameters.
      deDatatype datatype = deDatatypeArrayGetiDatatype(paramTypes, xDatatype);
      deDatatypeType type = deDatatypeGetType(datatype);
      if (type == DE_TYPE_TCLASS) {
        utAssert(selfType != deDatatypeNull && (deDatatypeGetType(selfType) == DE_TYPE_TCLASS ||
            deDatatypeGetType(selfType) == DE_TYPE_CLASS));
        deTclass tclass;
        if (deDatatypeGetType(selfType) == DE_TYPE_CLASS) {
          tclass = deClassGetTclass(deDatatypeGetClass(selfType));
        } else {
          tclass = deDatatypeGetTclass(selfType);
        }
        if (deDatatypeGetTclass(datatype) != tclass) {
          deError(line, "Called constructor with incorrect self-type");
        }
        datatype = selfType;
      }
      // Only fully-qualified types can be passed as parameters, with the exception that null
      // parameters passed to constructors can have the Tclass type.
      if ((type == DE_TYPE_NONE || type == DE_TYPE_TCLASS || type == DE_TYPE_FUNCTION) &&
          !(type == DE_TYPE_TCLASS && funcType == DE_FUNC_CONSTRUCTOR)) {
        deError(line, "Invalid type expression passed to parameter %s: %s.",
            deVariableGetName(variable), deDatatypeTypeGetName(type));
      }
      deVariableSetDatatype(variable, datatype);
    }
    xDatatype++;
    variable = deVariableGetNextBlockVariable(variable);
  }
  if (xDatatype < deDatatypeArrayGetUsedDatatype(paramTypes)) {
    deError(line, "Too many parameters");
  }
  checkParameterTypeConstraints(block, line);
  restoreParameterDatatypes(block);
}

// Bind a called function using the signature.
static void bindFunctionBlock(deFunction function, deSignature signature) {
  deSignature savedSignature = deCurrentSignature;
  deCurrentSignature = signature;
  deBlock subBlock = deFunctionGetSubBlock(function);
  utAssert(!deSignatureBinding(signature));
  deSignatureSetBinding(signature, true);
  bindBlock(subBlock, subBlock, signature);
  deSignatureSetBinding(signature, false);
  deCurrentSignature = savedSignature;
}

// Add a default show methods that can be called from the debugger.
static void addDefaultClassShowMethod(deClass theClass) {
  utSym showSym = utSymCreate("show");
  deFunction showMethod = deClassFindMethod(theClass, showSym);
  if (showMethod != deFunctionNull) {
    return;
  }
  showMethod = deGenerateDefaultShowMethod(theClass);
  deLine line = deTclassGetLine(deClassGetTclass(theClass));
  deDatatypeArray parameterTypes = deDatatypeArrayAlloc();
  deDatatype selfType = deClassDatatypeCreate(theClass);
  deDatatypeArrayAppendDatatype(parameterTypes, selfType);
  deSignature signature = deLookupSignature(showMethod, parameterTypes);
  utAssert(signature == deSignatureNull);
  signature = deSignatureCreate(showMethod, parameterTypes, line);
  deParamspec paramspec = deSignatureGetiParamspec(signature, 0);
  deParamspecSetInstantiated(paramspec, true);
  deSignatureSetInstantiated(signature, true);
}

// Forward declaration for recursion.
static void bindLazySignatures(deClass theClass);

// Bind the constructor call signature.
static void instantiateConstructorSignature(deSignature signature) {
  if (deSignatureGetLazyClass(signature) != deClassNull) {
    deClassRemoveLazySignature(deSignatureGetLazyClass(signature), signature);
  }
  deDatatype selfType = deSignatureGetReturnType(signature);
  deClass theClass = deDatatypeGetClass(selfType);
  deClass savedClass = deCurrentClass;
  deCurrentClass = theClass;
  deSignature savedSignature = deCurrentSignature;
  deCurrentSignature = signature;
  deFunction constructor = deSignatureGetFunction(signature);
  deBlock subBlock = deFunctionGetSubBlock(constructor);
  if (!deClassBound(theClass)) {
    // Wait until here to do this, since generators may add stuff to the tclass before here.
    // We only want to copy idents once, the first time we bind the class.
    deCopyFunctionIdentsToBlock(subBlock, deClassGetSubBlock(theClass));
  }
  deSignatureSetInstantiated(signature, deSignatureInstantiated(signature) | deInstantiating);
  bindBlock(subBlock, subBlock, signature);
  if (!deClassBound(theClass)) {
    // Wait until member variables have datatypes set to create member relationships.
    deAddClassMemberRelations(theClass);
  }
  deCurrentSignature = savedSignature;
  deCurrentClass = savedClass;
  bindLazySignatures(theClass);
  deClassSetBound(theClass, true);
}

// Signatures that were created while binding the block.
static void bindLazySignatures(deClass theClass) {
  deSignature signature;
  deSafeForeachClassLazySignature(theClass, signature) {
    instantiateConstructorSignature(signature);
  } deEndSafeClassLazySignature;
}

// Determine if the expression is an identifier bound to a non-const variable.
static bool expressionIsNonConstVariable(deExpression expression) {
  if (deExpressionGetType(expression) != DE_EXPR_IDENT) {
    return false;
  }
  deIdent ident = deExpressionGetIdent(expression);
  if (deIdentGetType(ident) != DE_IDENT_VARIABLE) {
    return false;
  }
  deVariable variable = deIdentGetVariable(ident);
  return !deVariableConst(variable);
}

// Check that var parameters are bound to non-const variables.  If |addSelf| is
// true, we skip the first paramspec on the signature, since this is bound to
// self.
static void checkVarParams(deSignature signature, deExpression parameters, bool addSelf,
      bool fromFuncPtrExpr) {
  bool skip = addSelf;
  deExpression parameter = deExpressionGetFirstExpression(parameters);
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    if (parameter == deExpressionNull) {
      return;
    }
    if (!skip) {
      deVariable variable = deParamspecGetVariable(paramspec);
      if (!deVariableConst(variable) && !expressionIsNonConstVariable(parameter)) {
        deError(deExpressionGetLine(parameter),
            "Parameter %s must be passed a non-const variable", deVariableGetName(variable));
      }
      if (!fromFuncPtrExpr && deInstantiating &&
          deParamspecInstantiated(paramspec) && deExpressionIsType(parameter)) {
        deError(deExpressionGetLine(parameter),
            "Parameter %s cannot be a type since its value is used", deVariableGetName(variable));
      }
      parameter = deExpressionGetNextExpression(parameter);
    }
    skip = false;
  } deEndSignatureParamspec;
}

// Bind the constructor call.  The datatype is a theClass, which is hashed based
// on its signature.  This allows us to update the datatype later once the
// constructor is fully bound and we know the theClass's data members.
static deDatatype bindConstructorCall(deFunction constructor, deExpression expression,
    deExpression parameters, deDatatypeArray parameterTypes, bool fromFuncPtrExpr) {
  deTclass tclass = deFunctionGetTclass(constructor);
  deLine line = deExpressionGetLine(expression);
  deBlock subBlock = deFunctionGetSubBlock(constructor);
  deSignature signature = deLookupSignature(constructor, parameterTypes);
  if (signature == deSignatureNull) {
    signature = deSignatureCreate(constructor, parameterTypes, line);
    deSignatureSetInstantiated(signature, deSignatureInstantiated(signature) | deInstantiating);
    // Returns an old theClass if the class signatures match.
    deClass theClass = deClassCreate(tclass, signature);
    deClassAppendSignature(theClass, signature);
    deSignature oldSignature = deClassGetFirstSignature(theClass);
    if (deSignaturePartial(oldSignature)) {
      deSignatureDestroy(oldSignature);
    }
    deDatatype selfType = deClassDatatypeCreate(theClass);
    deSignatureSetReturnType(signature, selfType);
    deSignature resolvedSignature = deResolveConstructorSignature(signature);
    if (resolvedSignature != signature) {
      // We deal with this signature elsewhere, so we're done.
      deExpressionSetSignature(expression, resolvedSignature);
      return deSignatureGetReturnType(resolvedSignature);
    } else {
      setSignatureReturnType(subBlock, signature, selfType, line);
      if (deCurrentClass == deClassNull) {
        if (deInstantiating) {
          // Delay binding of signature until we actually instantiate it.
          instantiateConstructorSignature(signature);
        }
      } else {
        // Do lazy binding of constructor blocks: bind them after the
        // constructor bock is bound.
        deClassAppendLazySignature(deCurrentClass, signature);
      }
    }
  }
  checkVarParams(signature, parameters, true, fromFuncPtrExpr);
  if (deInstantiating && deSignatureGetLazyClass(signature) == deClassNull &&
      !deClassBound(deSignatureGetClass(signature))) {
    // Now bind the signature that we delayed above.
    if (deCurrentClass == deClassNull) {
      instantiateConstructorSignature(signature);
    } else {
      deClassAppendLazySignature(deCurrentClass, signature);
    }
  }
  deExpressionSetSignature(expression, signature);
  return deSignatureGetReturnType(signature);
}

// Bind a function call, other than a built-in.  Default parameters should
// already have been added.
static deDatatype bindFunctionCall(deBlock scopeBlock, deFunction function,
          deExpression expression, deExpression parameters, deDatatype selfType,
          deDatatypeArray parameterTypes, bool fromFuncPtrExpr) {
  deLine line = deExpressionGetLine(expression);
  deSignature signature = deLookupSignature(function, parameterTypes);
  if (signature == deSignatureNull) {
    if (deFunctionExported(function)) {
      signature = deCreateFullySpecifiedSignature(function);
    } else {
      signature = deSignatureCreate(function, parameterTypes, line);
    }
    if (selfType != deDatatypeNull) {
      // Always instantiate self.
      if (deSignatureGetNumParamspec(signature) == 0) {
        deError(line, "Add a self parameter to the function");
      }
      deParamspec paramspec = deSignatureGetiParamspec(signature, 0);
      deParamspecSetInstantiated(paramspec, true);
    }
    deSignatureSetInstantiated(signature, deSignatureInstantiated(signature) | deInstantiating);
    bindFunctionBlock(function, signature);
  } else if (deInstantiating && !deSignatureInstantiated(signature)) {
    // Rebind the function if we are instantiating it this time.
    deSignatureSetInstantiated(signature, true);
    bindFunctionBlock(function, signature);
  }
  checkVarParams(signature, parameters, selfType != deDatatypeNull, fromFuncPtrExpr);
  deExpressionSetSignature(expression, signature);
  deDatatype returnType = deSignatureGetReturnType(signature);
  // If a function is called recursively, the return type should already
  // have been set, unless there is no return value, in which case, we
  // expect the return type to still be null.  In that case, go ahead and
  // set the return type to none.  If it later returns a value, this will
  // cause an error.
  if (returnType == deDatatypeNull) {
    deFunction function = deSignatureGetFunction(signature);
    utAssert(function != deFunctionNull);
    if (deFunctionReturnsValue(function)) {
      deError(line,
              "Function %s recursive call should come after the base-case",
              deFunctionGetName(function));
    }
    returnType = deNoneDatatypeCreate();
  }
  deExpressionSetDatatype(expression, returnType);
  return returnType;
}

// Bind a structure instantiation.  All parameters in the struct have already
// been bound, so just build the datatype.  Free the datatype array.
static deDatatype bindStructCall(deBlock scopeBlock, deFunction function,
          deExpression expression, deDatatype selfType,
          deDatatypeArray parameterTypes) {
  deLine line = deExpressionGetLine(expression);
  if (selfType != deDatatypeNull) {
    deError(line, "Struct %s is not a method.", deFunctionGetName(function));
  }
  return deStructDatatypeCreate(function, parameterTypes, line);
}

// Pre-bind constructor call types, using DE_TYPE_NULL placeholders for
// non-template types.  This returns the correct datatype, but the signature
// will need to be unified later with more specific signatures.
static deDatatype preBindConstructor(deBlock scopeBlock, deDatatype callType,
    deExpression parameters) {
  deDatatypeArray types = deDatatypeArrayAlloc();
  deDatatypeArrayAppendDatatype(types, callType);
  deTclass tclass = deDatatypeGetTclass(callType);
  deFunction constructor = deTclassGetFunction(tclass);
  deBlock tclassBlock = deFunctionGetSubBlock(constructor);
  // Skip self parameter.
  deVariable var = deVariableGetNextBlockVariable(deBlockGetFirstVariable(tclassBlock));
  deLine line = deExpressionGetLine(parameters);
  deExpression param;
  deForeachExpressionExpression(parameters, param) {
    if (var == deVariableNull || deVariableGetType(var) != DE_VAR_PARAMETER) {
      deError(line, "Too many parameters to constructor %s", deFunctionGetName(constructor));
    }
    if (deVariableInTclassSignature(var)) {
      bindExpression(scopeBlock, param);
      deDatatypeArrayAppendDatatype(types, deExpressionGetDatatype(param));
    } else {
      // None datatype will eventually be replaced when we bind a call.
      deDatatypeArrayAppendDatatype(types, deNoneDatatypeCreate());
    }
    var = deVariableGetNextBlockVariable(var);
  } deEndExpressionExpression;
  // Now add default parameters.
  while (var != deVariableNull && deVariableGetType(var) == DE_VAR_PARAMETER) {
    deExpression defaultValue = deVariableGetInitializerExpression(var);
    if (defaultValue == deExpressionNull) {
      deError(line, "Not enough parameters");
    }
    if (deVariableInTclassSignature(var)) {
      bindExpression(scopeBlock, defaultValue);
      deDatatypeArrayAppendDatatype(types, deExpressionGetDatatype(defaultValue));
    } else {
      deDatatypeArrayAppendDatatype(types, deNoneDatatypeCreate());
    }
    var = deVariableGetNextBlockVariable(var);
  }
  deSignature signature = deSignatureCreate(constructor, types, line);
  deSignatureSetPartial(signature, true);
  deClass theClass = deClassCreate(tclass, signature);
  deDatatype selfType = deClassDatatypeCreate(theClass);
  if (deClassGetFirstSignature(theClass) == deSignatureNull) {
    // Keep it so we can match it later.
    deSignatureSetReturnType(signature, selfType);
    deResolveConstructorSignature(signature);
    deClassAppendSignature(theClass, signature);
  } else {
    deSignatureDestroy(signature);  // It is not needed for matching.
  }
  return selfType;
}

// Bind positional parameters.  Return the first named parameter.
static deExpression bindPositionalParameters(deBlock scopeBlock, deExpression parameters,
    deDatatypeArray parameterTypes) {
  deExpression parameter;
  deForeachExpressionExpression(parameters, parameter) {
    if (deExpressionGetType(parameter) == DE_EXPR_NAMEDPARAM) {
      return parameter;
    }
    deDatatype datatype = getDatatype(parameter);
    deDatatypeArrayAppendDatatype(parameterTypes, datatype);
  } deEndExpressionExpression;
  return deExpressionNull;
}

// Verify that it is OK for code to call the function.
static void verifyFunctionIsCallable(deBlock scopeBlock, deFunction function) {
  deFunctionType type = deFunctionGetType(function);
  switch (type) {
    case DE_FUNC_PLAIN:
    case DE_FUNC_UNITTEST:
    case DE_FUNC_OPERATOR:
    case DE_FUNC_CONSTRUCTOR:
    case DE_FUNC_DESTRUCTOR:
    case DE_FUNC_ITERATOR:
    case DE_FUNC_STRUCT:
      return;
    case DE_FUNC_MODULE:
    case DE_FUNC_PACKAGE: {
      if (deFunctionGetType(deBlockGetOwningFunction(scopeBlock)) == DE_FUNC_PACKAGE) {
        return;
      }
    }
    case DE_FUNC_FINAL:
    case DE_FUNC_ENUM:
    case DE_FUNC_GENERATOR:
      break;
  }
  deError(deFunctionGetLine(function), "Cannot call function %s, which which has type %s\n",
     deFunctionGetName(function), deGetFunctionTypeName(type));
}

// Bind a call expression.  When binding in a function pointer expression, we
// use different parameter checks, because we want to instantiate the function,
// and allow types to be passed as parameters, even if they are used inside the
// function.  E.g. &sum(u32, u32) should instantiate sum as if real u32 values
// were passed in, as functions called through pointers cannot take types as
// inputs.
static void bindCallExpression(deBlock scopeBlock, deExpression expression,
      bool fromFuncPtrExpr) {
  deExpression accessExpression = deExpressionGetFirstExpression(expression);
  bindExpression(scopeBlock, accessExpression);
  deLine line = deExpressionGetLine(expression);
  deDatatype callType = getDatatype(accessExpression);
  deDatatypeType type = deDatatypeGetType(callType);
  deExpression parameters = deExpressionGetNextExpression(accessExpression);
  deDatatypeArray parameterTypes = deDatatypeArrayAlloc();
  deDatatype selfType = deDatatypeNull;
  if (deExpressionIsMethodCall(accessExpression)) {
    // Add the type of the object on the left of the dot expression as self parameter.
    selfType = deExpressionGetDatatype(deExpressionGetFirstExpression(accessExpression));
    deDatatypeArrayAppendDatatype(parameterTypes, selfType);
  } else if (type == DE_TYPE_TCLASS) {
    // This is a constructor call.  Add the class datatype as the self parameter.
    selfType = preBindConstructor(scopeBlock, callType, parameters);
    deDatatypeArrayAppendDatatype(parameterTypes, selfType);
  }
  bindParameterList(scopeBlock, parameters);
  deExpression firstNamedParameter = bindPositionalParameters(
      scopeBlock, parameters, parameterTypes);
  deDatatype returnType;
  switch (type) {
    case DE_TYPE_FUNCTION: {
      deFunction function = deDatatypeGetFunction(callType);
      verifyFunctionIsCallable(scopeBlock, function);
      fillOutDefaultParamters(deFunctionGetSubBlock(function), selfType, parameterTypes,
          firstNamedParameter, line);
      if (deFunctionBuiltin(function)) {
        returnType = deBindBuiltinCall(scopeBlock, function, parameterTypes, expression);
      } else if (deFunctionGetType(function) == DE_FUNC_STRUCT) {
        returnType = bindStructCall(scopeBlock, function, expression,
            selfType, parameterTypes);
        parameterTypes = deDatatypeArrayNull;  // It is freed in bindStructCall.
      } else {
        returnType = bindFunctionCall(scopeBlock, function, expression,
            parameters, selfType, parameterTypes, fromFuncPtrExpr);
      }
      break;
    }
    case DE_TYPE_FUNCPTR:
      compareFuncptrParameters(callType, parameterTypes, line);
      returnType = deDatatypeGetReturnType(callType);
      break;
    case DE_TYPE_TCLASS: {
      deTclass tclass = deDatatypeGetTclass(callType);
      deFunction constructor = deTclassGetFunction(tclass);
      fillOutDefaultParamters(deFunctionGetSubBlock(constructor), selfType, parameterTypes,
          firstNamedParameter, line);
      returnType = bindConstructorCall(constructor, expression, parameters, parameterTypes,
          fromFuncPtrExpr);
      break;
    }
    default:
      deError(line, "Tried to call non-function");
      return;  // Can't get here...
  }
  deExpressionSetDatatype(expression, returnType);
  if (parameterTypes != deDatatypeArrayNull) {
    deDatatypeArrayFree(parameterTypes);
  }
}

// Bind the index expression.
static void bindIndexExpression(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  bindExpression(scopeBlock, left);
  bool savedBindingAssignmentTarget = deBindingAssignmentTarget;
  deBindingAssignmentTarget = false;
  bindExpression(scopeBlock, right);
  deBindingAssignmentTarget = savedBindingAssignmentTarget;
  deDatatype leftType = getDatatype(left);
  deDatatype rightType = getDatatype(right);
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deError(line, "Index values must be uint");
  }
  if (deDatatypeSecret(rightType)) {
    deError(line, "Indexing with a secret is not allowed");
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_ARRAY && type != DE_TYPE_STRING && type != DE_TYPE_TUPLE &&
      type != DE_TYPE_STRUCT) {
    deError(line, "Index into non-array/non-string/non-tuple/non-struct type");
  }
  if (type == DE_TYPE_TUPLE || type == DE_TYPE_STRUCT) {
    deExpression right = deExpressionGetNextExpression(left);
    if (deExpressionGetType(right) != DE_EXPR_INTEGER) {
      deError(line,
          "Tuples and Structs can only be indexed by constant integers, like y = point[1]");
    }
    uint32 index = deBigintGetUint32(deExpressionGetBigint(right), line);
    if (index >= deDatatypeGetNumTypeList(leftType)) {
      deError(line, "Tuple index out of bounds");
    }
    deExpressionSetDatatype(expression, deDatatypeGetiTypeList(leftType, index));
  } else {
    deDatatype elementType = deDatatypeGetElementType(leftType);
    deExpressionSetDatatype(expression, elementType);
  }
  deExpressionSetConst(expression, deExpressionConst(left));
}

// Bind the slice expression.
static void bindSliceExpression(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression lower = deExpressionGetNextExpression(left);
  deExpression upper = deExpressionGetNextExpression(lower);
  deDatatype leftType, lowerType, upperType;
  bindExpression(scopeBlock, left);
  bindExpression(scopeBlock, lower);
  bindExpression(scopeBlock, upper);
  leftType = deExpressionGetDatatype(left);
  lowerType = deExpressionGetDatatype(lower);
  upperType = deExpressionGetDatatype(upper);
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeGetType(lowerType) != DE_TYPE_UINT ||
      deDatatypeGetType(upperType) != DE_TYPE_UINT) {
    deError(line, "Index values must be unsigned integers");
  }
  if (deDatatypeSecret(lowerType) || deDatatypeSecret(upperType)) {
    deError(line, "Indexing with a secret is not allowed");
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_ARRAY && type != DE_TYPE_STRING) {
    deError(line, "Slicing a non-array/non-string type");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind the markSecret or markPublic expression.
static void bindMarkSecretOrPublic(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type == DE_TYPE_CLASS || type == DE_TYPE_NULL) {
    deError(deExpressionGetLine(expression), "Object references cannot be marked secret");
  }
  bool secret = deExpressionGetType(expression) == DE_EXPR_SECRET;
  datatype = deSetDatatypeSecret(datatype, secret);
  deExpressionSetDatatype(expression, datatype);
  if (deExpressionIsType(deExpressionGetFirstExpression(expression))) {
    deExpressionSetIsType(expression, true);
  }
}

// Deal with the case of writing to a class variable in a constructor, which is
// how class variables are defined.  Return true if we create a new member
// variable.
static bool dealWithTclassVariableAssignment(deBlock scopeBlock,
    deExpression target, deExpression value, bool isConst) {
  deDatatype datatype = getDatatype(value);
  deFunctionType funcType = deFunctionGetType(deBlockGetOwningFunction(scopeBlock));
  if (funcType != DE_FUNC_CONSTRUCTOR || deExpressionGetType(target) != DE_EXPR_DOT) {
    return false;
  }
  if (deCurrentSignature == deSignatureNull ||
      deFunctionGetType(deSignatureGetFunction(deCurrentSignature)) != DE_FUNC_CONSTRUCTOR) {
    return false;
  }
  deExpression left = deExpressionGetFirstExpression(target);
  deExpression right = deExpressionGetNextExpression(left);
  if (deExpressionGetType(left) != DE_EXPR_IDENT ||
      deExpressionGetType(right) != DE_EXPR_IDENT) {
    return false;
  }
  utSym selfName = deExpressionGetName(left);
  deIdent selfIdent = deBlockFindIdent(scopeBlock, selfName);
  deLine line = deExpressionGetLine(target);
  if (selfIdent == deIdentNull) {
    deError(line, "Unknown self identifier %s", utSymGetName(selfName));
  }
  if (deIdentGetType(selfIdent) != DE_IDENT_VARIABLE) {
    return false;
  }
  deVariable selfVariable = deIdentGetVariable(selfIdent);
  if (selfVariable != deBlockGetFirstVariable(scopeBlock)) {
    return false;
  }
  // Assigning to self.<varName>.
  deClass theClass = deDatatypeGetClass(deSignatureGetReturnType(deCurrentSignature));
  deBlock theClassBlock = deClassGetSubBlock(theClass);
  utSym name = deExpressionGetName(right);
  deIdent varIdent = deBlockFindIdent(theClassBlock, name);
  deVariable var;
  bool createdVar = false;
  if (varIdent == deIdentNull) {
    deStatement statement = deFindExpressionStatement(target);
    bool generated = deStatementGenerated(statement);
    var = deVariableCreate(theClassBlock, DE_VAR_LOCAL, isConst, name, deExpressionNull,
        generated, line);
    deStatementInsertVariable(deCurrentStatement, var);
    createdVar = true;
    if (deVariableGetDatatype(var) != deDatatypeNull) {
      if (deVariableConst(var)) {
        deError(line, "Assigning to const variable %s ", deVariableGetName(var));
      }
    }
    setVariableDatatype(scopeBlock, var, datatype, line);
    deBlock currentBlock = deStatementGetBlock(statement);
    deStatementSetIsFirstAssignment(statement, currentBlock == scopeBlock);
  } else {
    var = deIdentGetVariable(varIdent);
    setVariableDatatype(scopeBlock, var, datatype, line);
  }
  deVariableSetIsType(var, deVariableIsType(var) || deExpressionIsType(value));
  deVariableSetInstantiated(var, deVariableInstantiated(var) ||
      (deInstantiating && !deExpressionIsType(value)));
  return createdVar;
}

// Create an array of datatypes for the expression's children.
deDatatypeArray deListDatatypes(deExpression expressionList) {
  deDatatypeArray types = deDatatypeArrayAlloc();
  deExpression child;
  deForeachExpressionExpression(expressionList, child) {
    deDatatypeArrayAppendDatatype(types, deExpressionGetDatatype(child));
  } deEndExpressionExpression;
  return types;
}

// Bind an access expression.  Set deBindingAssignmentTarget.
static void bindAccessExpression(deBlock scopeBlock, deExpression accessExpression) {
  bool savedBindingAssignmentTarget = deBindingAssignmentTarget;
  deBindingAssignmentTarget = true;
  bindExpression(scopeBlock, accessExpression);
  deBindingAssignmentTarget = savedBindingAssignmentTarget;
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
  bindAccessExpression(scopeBlock, target);
}

// Bind an assignment statement.  If the left-hand side is a variable, then set
// its type, or verify its type has not changed if it already has a type.
static void bindAssignmentExpression(deBlock scopeBlock, deExpression expression, bool isConst) {
  deLine line = deExpressionGetLine(expression);
  deExpression target = deExpressionGetFirstExpression(expression);
  deExpressionType targetType = deExpressionGetType(target);
  if (targetType == DE_EXPR_CALL) {
    deError(line, "Cannot assign to a call expression");
  }
  if (targetType == DE_EXPR_CALL || targetType == DE_EXPR_NULL) {
    deError(line, "Cannot assign to a null expression");
  }
  deExpression value = deExpressionGetNextExpression(target);
  deExpression constraint = deExpressionGetNextExpression(value);
  deDatatype valueType;
  if (deExpressionGetType(expression) == DE_EXPR_EQUALS) {
    // This has already been done if this is an op-equals expression.
    bindExpression(scopeBlock, value);
    valueType = getDatatype(value);
  } else {
    // The value type has already been confirmed to match target.
    valueType = getDatatype(target);
  }
  if (deDatatypeGetType(valueType) == DE_TYPE_NONE) {
    deError(line, "Right hand side of assignment must have a type");
  }
  if (constraint != deExpressionNull &&
      !deDatatypeMatchesTypeExpression(scopeBlock, valueType, constraint)) {
    deError(line, "Assignment violates type constraint: %s",
            deDatatypeGetTypeString(valueType));
  }
  bool isNewVar = dealWithTclassVariableAssignment(scopeBlock, target, value, isConst);
  if (deDatatypeGetType(valueType) == DE_TYPE_FUNCTION) {
    deError(line, "Variables cannot be assigned functions directly.  Use &func(...)");
  }
  if (deDatatypeGetType(valueType) == DE_TYPE_TCLASS) {
    char* name = deTclassGetName(deDatatypeGetTclass(valueType));
    deError(line, "To construct an object of type %s, use %s(...)", name, name);
  }
  if (deExpressionGetType(target) != DE_EXPR_IDENT) {
    bindAccessExpression(scopeBlock, target);
    if (!isNewVar && deExpressionConst(target)) {
      deError(line, "Assigning to const expression");
    }
    deDatatype targetType = getDatatype(target);
    if (targetType != deDatatypeNull) {
      deDatatype unifiedType = deUnifyDatatypes(targetType, valueType);
      if (unifiedType == deDatatypeNull) {
        deError(line, "Writing different datatype to existing value:%s",
            deGetOldVsNewDatatypeStrings(targetType, valueType));
      }
      if (targetType != unifiedType) {
        // Refine the target datatype.
        deRefineAccessExpressionDatatype(scopeBlock, target, unifiedType);
      }
    }
    if (deExpressionIsType(target)) {
      deError(line, "Cannot write types to non-variables");
    }
  } else {
    // This might be a local variable assignment.
    utSym name = deExpressionGetName(target);
    deIdent ident = deFindIdent(scopeBlock, name);
    if (ident != deIdentNull && deIdentIsModuleOrPackage(ident)) {
      // Specifically allow shadowing of module and package names.
      // It is common to want to import foo, and later use foo = Foo().
      ident = deIdentNull;
    }
    if (ident == deIdentNull) {
      // This is a new local variable.
      bool generated = deStatementGenerated(deFindExpressionStatement(expression));
      deVariable variable = deVariableCreate(scopeBlock, DE_VAR_LOCAL, isConst, name,
          deExpressionNull, generated, line);
      deStatementInsertVariable(deCurrentStatement, variable);
      deBlock currentBlock = deStatementGetBlock(deFindExpressionStatement(expression));
      deVariableSetInitializedAtTop(variable, currentBlock == scopeBlock);
      deStatement statement = deFindExpressionStatement(expression);
      deStatementSetIsFirstAssignment(statement, currentBlock == scopeBlock);
      setVariableDatatype(scopeBlock, variable, valueType, line);
      deVariableSetIsType(variable, deExpressionIsType(value));
      deVariableSetInstantiated(variable, deInstantiating && !deExpressionIsType(value));
      ident = deVariableGetIdent(variable);
    } else {
      if (deIdentGetType(ident) != DE_IDENT_VARIABLE) {
        deError(line, "Tried to assign to a non-variable (class or function)");
      }
      deVariable variable = deIdentGetVariable(ident);
      deDatatype oldType = deVariableGetDatatype(variable);
      if (oldType == deDatatypeNull) {
        setVariableDatatype(scopeBlock, variable, valueType, line);
      } else if (deUnifyDatatypes(oldType, valueType) == deDatatypeNull) {
        deError(line, "Type mismatch while updating an existing variable:%s",
            deGetOldVsNewDatatypeStrings(oldType, valueType));
      } else if (deVariableConst(variable)) {
        deError(line, "Assigning to const variable %s", deVariableGetName(variable));
      } else if (isConst) {
        deError(line, "Const declaration can only be on first assignment to a variable");
      }
      bindAccessExpression(scopeBlock, target);
      if (oldType != valueType) {
        // Refine the target datatype.
        deRefineAccessExpressionDatatype(scopeBlock, target, valueType);
      }
      deVariableSetIsType(variable, deVariableIsType(variable) || deExpressionIsType(value));
      deVariableSetInstantiated(variable, deVariableInstantiated(variable) ||
          (deInstantiating && !deExpressionIsType(value)));
      deIdent oldIdent = deExpressionGetIdent(target);
      if (oldIdent != deIdentNull) {
        deIdentRemoveExpression(oldIdent, target);
      }
    }
    deIdentAppendExpression(ident, target);
    deExpressionSetDatatype(target, deExpressionGetDatatype(value));
  }
  deExpressionSetDatatype(expression, valueType);
  deExpressionSetIsType(expression, deExpressionIsType(value));
  if (deExpressionIsType(value)) {
    deStatement statement = deFindExpressionStatement(expression);
    deStatementSetInstantiated(statement, false);
  }
}

// Find the index of the struct member indicated by the identifier expression.
static uint32 findStructIdentIndex(deDatatype datatype, deExpression identExpr) {
  deFunction function = deDatatypeGetFunction(datatype);
  deLine line = deExpressionGetLine(identExpr);
  if (deExpressionGetType(identExpr) != DE_EXPR_IDENT) {
    deError(line, "Expected an identifier after dot");
  }
  utSym sym = deExpressionGetName(identExpr);
  uint32 xVar = 0;
  deBlock block = deFunctionGetSubBlock(function);
  deVariable var;
  deForeachBlockVariable(block, var) {
    if (deVariableGetSym(var) == sym) {
      return xVar;
    }
    xVar++;
  } deEndBlockVariable;
  deError(line, "No struct member named %s found in %s", utSymGetName(sym),
      deFunctionGetName(function));
  return 0;  // Dummy return.
}

// Bind a dot expression.  If we're binding a constructor, search in the
// current theClass rather than the class constructor.
static void bindDotExpression(deBlock scopeBlock, deExpression expression) {
  deExpression accessExpression = deExpressionGetFirstExpression(expression);
  deExpression rightExpression = deExpressionGetNextExpression(accessExpression);
  bool savedBindingAssignmentTarget = deBindingAssignmentTarget;
  deBindingAssignmentTarget = false;
  bindExpression(scopeBlock, accessExpression);
  deBindingAssignmentTarget = savedBindingAssignmentTarget;
  deDatatype datatype = getDatatype(accessExpression);
  deDatatypeType type = deDatatypeGetType(datatype);
  deBlock classBlock;
  deLine line = deExpressionGetLine(expression);
  if (type == DE_TYPE_STRUCT) {
    uint32 index = findStructIdentIndex(datatype, rightExpression);
    deExpressionSetDatatype(expression, deDatatypeGetiTypeList(datatype, index));
    return;
  }
  if (type == DE_TYPE_CLASS) {
    classBlock = deClassGetSubBlock(deDatatypeGetClass(datatype));
  } else if (type == DE_TYPE_NULL) {
    deError(line,
        "\n    Trying to access member of partially unified class %s.  This can\n"
        "    be caused by having a relationship between template classes without ever\n"
        "    adding a child object to the relationship.  This can cause the compiler to\n"
        "    still lack type information when asked to destroy an object of the partially\n"
        "    unified class.  Try deleting unused Dict objects, or inserting some data.\n",
        deTclassGetName(deDatatypeGetTclass(datatype)));
    return;  // Can't get here.
  } else if (type == DE_TYPE_TCLASS) {
    classBlock = deFunctionGetSubBlock(deTclassGetFunction(deDatatypeGetTclass(datatype)));
  } else if (type == DE_TYPE_FUNCTION || type == DE_TYPE_STRUCT || type == DE_TYPE_ENUMCLASS) {
    deFunction function = deDatatypeGetFunction(datatype);
    deFunctionType funcType = deFunctionGetType(function);
    if (funcType != DE_FUNC_PACKAGE && funcType != DE_FUNC_MODULE &&
        funcType != DE_FUNC_STRUCT && funcType != DE_FUNC_ENUM) {
      deError(line, "Cannot access identifiers inside a function");
    }
    classBlock = deFunctionGetSubBlock(function);
  } else {
    // Some builtin types have method calls.
    deTclass tclass = deFindDatatypeTclass(datatype);
    classBlock = deFunctionGetSubBlock(deTclassGetFunction(tclass));
  }
  if (deExpressionGetType(rightExpression) != DE_EXPR_IDENT) {
    deError(line, "An identifier is expected after '.'");
  }
  utSym name = deExpressionGetName(rightExpression);
  deIdent ident = deBlockFindIdent(classBlock, name);
  if (ident == deIdentNull) {
    deError(line, "No method name %s was found", utSymGetName(name));
  }
  bindExpression(classBlock, rightExpression);
  deExpressionSetDatatype(expression, deExpressionGetDatatype(rightExpression));
  deExpressionSetConst(expression, deExpressionConst(rightExpression));
}

// Bind the assignment operator expression.
static void bindAssignmentOperatorExpression(deBlock scopeBlock, deExpression expression) {
  deExpressionType assignmentType = deExpressionGetType(expression);
  // This verifies doing the operator, without the assignment.
  deExpressionSetType(expression, assignmentType - DE_EXPR_ADD_EQUALS + DE_EXPR_ADD);
  bindExpression(scopeBlock, expression);
  deExpression target = deExpressionGetFirstExpression(expression);
  deDatatype oldDatatype = deExpressionGetDatatype(target);
  deDatatype newDatatype = deExpressionGetDatatype(expression);
  if (oldDatatype != newDatatype) {
    deError(deExpressionGetLine(expression), "Incompatible type in reassignment");
  }
  deExpressionSetType(expression, assignmentType);
  // Now verify the assignment.
  bindAssignmentExpression(scopeBlock, expression, false);
}

// Bind the tuple expression.
static void bindTupleExpression(deBlock scopeBlock, deExpression expression) {
  bindExpressionList(scopeBlock, expression);
  deDatatypeArray types = deListDatatypes(expression);
  deDatatype tupleType = deTupleDatatypeCreate(types);
  deExpressionSetDatatype(expression, tupleType);
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    if (deExpressionIsType(child)) {
      deExpressionSetIsType(expression, true);
    }
  } deEndExpressionExpression;
}

// Bind a null expression to the class type.
static void bindNullExpression(deBlock scopeBlock, deExpression expression) {
  bool savedInstantiating = deInstantiating;
  deInstantiating = false;
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  if (deDatatypeGetType(datatype) == DE_TYPE_TCLASS) {
    // If there are no template parameters, we can find the class.
    deClass theClass = deTclassGetDefaultClass(deDatatypeGetTclass(datatype));
    if (theClass != deClassNull) {
      datatype = deClassGetDatatype(theClass);
    }
  }
  if (deDatatypeGetType(datatype) == DE_TYPE_TCLASS) {
    datatype = deNullDatatypeCreate(deDatatypeGetTclass(datatype));
  }
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_NULL:
    case DE_TYPE_BOOL:
    case DE_TYPE_STRING:
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_FLOAT:
    case DE_TYPE_ARRAY:
    case DE_TYPE_TUPLE:
    case DE_TYPE_STRUCT:
    case DE_TYPE_ENUMCLASS:
    case DE_TYPE_ENUM:
    case DE_TYPE_FUNCPTR:
      break;
    case DE_TYPE_CLASS:
      datatype = deSetDatatypeNullable(datatype, true, deExpressionGetLine(expression));
      break;
    case DE_TYPE_FUNCTION: {
      deFunctionType type = deFunctionGetType(deDatatypeGetFunction(datatype));
      if (type != DE_FUNC_STRUCT && type != DE_FUNC_ENUM) {
        deError(deExpressionGetLine(expression), "Cannot create default initial value for type %s",
            deDatatypeGetTypeString(datatype));
      }
      break;
    }
    case DE_TYPE_TCLASS:
    case DE_TYPE_MODINT:
    case DE_TYPE_NONE:
      deError(deExpressionGetLine(expression), "Cannot create default initial value for type %s",
          deDatatypeGetTypeString(datatype));
      break;
  }
  deExpressionSetDatatype(expression, datatype);
  deInstantiating = savedInstantiating;
}

// Bind a notnull expression.
static void bindNotNullExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = deSetDatatypeNullable(bindUnaryExpression(scopeBlock, expression), false,
      deExpressionGetLine(expression));
  deExpressionSetDatatype(expression, datatype);
}

// Set all the variables passed as instantiated.  Any function that can be
// called through a pointer must accept all parameters on the stack, even if
// they are unused, or only used for their types.
static void setAllSignatureVariablesToInstantiated(deSignature signature) {
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    deParamspecSetInstantiated(paramspec, true);
  } deEndSignatureParamspec;
}

// Bind a function pointer expression.  We have to mark all the parameter
// variables as not types during binding, even though we typically specify only
// the parameter types in the function address expression.  When we call through
// the function pointer, all parameters will be instantiated, which may lead to
// some unused parameters being instantiated.  Any function signature that has
// its address taken is called by passing all parameters, since that's how we
// call it through the function pointer.  In case we pass a type to the function
// when called directly, we push default values for the type parameters.
static void bindFunctionPointerExpression(deBlock scopeBlock, deExpression expression) {
  bool savedInstantiating = deInstantiating;
  deInstantiating = true;
  deExpression functionCallExpression = deExpressionGetFirstExpression(expression);
  bindCallExpression(scopeBlock, functionCallExpression, true);
  deDatatype returnType = getDatatype(functionCallExpression);
  deExpression functionExpression = deExpressionGetFirstExpression(functionCallExpression);
  deExpression parameters = deExpressionGetNextExpression(functionExpression);
  deDatatypeArray parameterTypes = deDatatypeArrayAlloc();
  deExpression parameter;
  deForeachExpressionExpression(parameters, parameter) {
    deDatatype datatype = deExpressionGetDatatype(parameter);
    deDatatypeArrayAppendDatatype(parameterTypes, datatype);
  } deEndExpressionExpression;
  deDatatype funcptrType = deFuncptrDatatypeCreate(returnType, parameterTypes);
  deDatatype functionDatatype = deExpressionGetDatatype(functionExpression);
  utAssert(deDatatypeGetType(functionDatatype) == DE_TYPE_FUNCTION);
  deFunction function = deDatatypeGetFunction(functionDatatype);
  deSignature signature = deLookupSignature(function, parameterTypes);
  deLine line = deExpressionGetLine(expression);
  if (signature == deSignatureNull) {
    signature = deSignatureCreate(function, parameterTypes, line);
  }
  deSignatureSetIsCalledByFuncptr(signature, true);
  setAllSignatureVariablesToInstantiated(signature);
  deExpressionSetSignature(expression, signature);
  deExpressionSetDatatype(expression, funcptrType);
  deInstantiating = savedInstantiating;
}

// Bind an arrayof expression.
static void bindArrayofExpression(deBlock scopeBlock, deExpression expression) {
  bool savedInstantiating = deInstantiating;
  deInstantiating = false;
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  if (deDatatypeGetType(datatype) == DE_TYPE_TCLASS) {
    datatype = deNullDatatypeCreate(deDatatypeGetTclass(datatype));
  }
  deExpressionSetDatatype(expression, deArrayDatatypeCreate(datatype));
  deInstantiating = savedInstantiating;
}

// Bind a typeof expression.
static void bindTypeofExpression(deBlock scopeBlock, deExpression expression) {
  bool savedInstantiating = deInstantiating;
  deInstantiating = false;
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  deExpressionSetDatatype(expression, datatype);
  deExpressionSetIsType(expression, true);
  deInstantiating = savedInstantiating;
}

// Bind a signed() or unsigned() type conversion expression.
static void bindSignConversionExpression(deBlock scopeBlock, deExpression expression) {
  deExpression child = deExpressionGetFirstExpression(expression);
  bindExpression(scopeBlock, child);
  deDatatype datatype = deExpressionGetDatatype(child);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type != DE_TYPE_UINT && type != DE_TYPE_INT) {
    deError(deExpressionGetLine(expression), "Cannot  change sign of non-integer");
  }
  datatype = deDatatypeSetSigned(datatype, deExpressionGetType(expression)== DE_EXPR_SIGNED);
  deExpressionSetDatatype(expression, datatype);
}

// Bind a widthof expression.  The expression type is u32.
static void bindWidthofExpression(deBlock scopeBlock, deExpression expression) {
  bool savedInstantiating = deInstantiating;
  deInstantiating = false;
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  if (!deDatatypeIsNumber(datatype)) {
    deError(deExpressionGetLine(expression), "widthof applied to non-number");
  }
  deExpressionSetDatatype(expression, deUintDatatypeCreate(32));
  deInstantiating = savedInstantiating;
}

// Bind an isnull expression.  The expression type is bool.
static void bindIsnullExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type != DE_TYPE_CLASS && type != DE_TYPE_NULL) {
    deError(deExpressionGetLine(expression), "isnull applied to non-object");
  }
  deExpressionSetDatatype(expression, deBoolDatatypeCreate());
}

// Bind a ... expression, eg case u1 ... u32.
static void bindDotDotDotExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftDatatype, rightDatatype;
  bindBinaryExpression(scopeBlock, expression, &leftDatatype, &rightDatatype, true);
  deLine line = deExpressionGetLine(expression);
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  if (deExpressionIsType(left) != deExpressionIsType(right)) {
    deError(line, "Ranges must be either types or integers, eg not u32 .. 64");
  }
  deDatatypeType leftType = deDatatypeGetType(leftDatatype);
  deDatatypeType rightType = deDatatypeGetType(rightDatatype);
  if (deExpressionIsType(left)) {
    if (leftType != DE_TYPE_UINT && leftType != DE_TYPE_INT) {
      deError(line, "Type ranges are only allowed for Int and Uint types, eg u1 ... u32");
    }
    if (leftType != rightType) {
      deError(line, "Type ranges must have the same sign, eg u1 ... u32 or i1 ... i32");
    }
    uint32 leftWidth = deDatatypeGetWidth(leftDatatype);
    uint32 rightWidth = deDatatypeGetWidth(rightDatatype);
    if (leftWidth > rightWidth) {
      deError(line, "Left type width must be <= right type width, eg i64 ... i256");
    }
    deTclass tclass = deFindDatatypeTclass(leftDatatype);
    deExpressionSetDatatype(expression, deTclassDatatypeCreate(tclass));
    deExpressionSetIsType(expression, true);
  } else {
    if (leftType != DE_TYPE_UINT && leftType != DE_TYPE_INT) {
      deError(line, "Integer ranges are only allowed for Int and Uint types, eg u1 ... u32");
    }
    if (leftDatatype != rightDatatype) {
      deError(line, "Type ranges limits must have the same type, eg 1 ... 10 or 1i32 ... 10i32:%s",
          deGetOldVsNewDatatypeStrings(leftDatatype, rightDatatype));
    }
    deExpressionSetDatatype(expression, leftDatatype);
  }
}

// Bind a named parameter.  Just skip the name, and set the type to the type of
// the expression on the right.
static void bindNamedParameter(deBlock scopeBlock, deExpression expression) {
  deExpression rightExpression = deExpressionGetLastExpression(expression);
  bindExpression(scopeBlock, rightExpression);
  deExpressionSetDatatype(expression, deExpressionGetDatatype(rightExpression));
  deExpressionSetIsType(expression, deExpressionIsType(rightExpression));
}

// Bind an "in" expression.  These are all overloads.
static void bindInExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  if (!bindOverloadedOperator(scopeBlock, expression)) {
    char *elemType = utAllocString(deDatatypeGetTypeString(leftType));
    char *setType = deDatatypeGetTypeString(rightType);
    deError(deExpressionGetLine(expression),
        "No overload for %s in %s", elemType, setType);
  }
  deExpressionSetDatatype(expression, deBoolDatatypeCreate());
}

// Bind the expression.
static void bindExpression(deBlock scopeBlock, deExpression expression) {
  deExpressionSetIsType(expression, false);
  deExpressionSetAutocast(expression, false);
  deExpressionSetConst(expression, false);
  deExpressionSetSignature(expression, deSignatureNull);
  deExpressionSetAltString(expression, deStringNull);
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_INTEGER:
      bindIntegerExpression(expression);
      break;
    case DE_EXPR_FLOAT:
      bindFloatExpression(expression);
      break;
    case DE_EXPR_BOOL:
      deExpressionSetDatatype(expression, deBoolDatatypeCreate());
      break;
    case DE_EXPR_STRING:
      deExpressionSetDatatype(expression, deStringDatatypeCreate());
      break;
    case DE_EXPR_IDENT:
      bindIdentExpression(scopeBlock, expression);
      break;
    case DE_EXPR_ARRAY:
      bindArrayExpression(scopeBlock, expression);
      break;
    case DE_EXPR_RANDUINT:
      bindRandUintExpression(expression);
      break;
    case DE_EXPR_MODINT:
      bindModintExpression(scopeBlock, expression);
      break;
    case DE_EXPR_BITOR :
      bindBitwiseOrExpression(scopeBlock, expression);
      break;
    case DE_EXPR_ADD:
    case DE_EXPR_SUB:
    case DE_EXPR_MUL:
    case DE_EXPR_DIV:
      bindBinaryArithmeticExpression(scopeBlock, expression);
      break;
    case DE_EXPR_BITAND:
    case DE_EXPR_BITXOR:
    case DE_EXPR_ADDTRUNC:
    case DE_EXPR_SUBTRUNC:
    case DE_EXPR_MULTRUNC:
      bindBinaryArithmeticExpression(scopeBlock, expression);
      if (deDatatypeIsFloat(deExpressionGetDatatype(expression))) {
        deError(deExpressionGetLine(expression),
                "Invalid binary operation on floating point types.");
      }
      break;
    case DE_EXPR_MOD:
      bindModExpression(scopeBlock, expression);
      break;
    case DE_EXPR_AND:
    case DE_EXPR_OR :
    case DE_EXPR_XOR:
      bindBinaryBoolOrArithmeticExpression(scopeBlock, expression);
      break;
    case DE_EXPR_EXP:
      bindExponentiationExpression(scopeBlock, expression);
      break;
    case DE_EXPR_SHL:
    case DE_EXPR_SHR:
    case DE_EXPR_ROTL:
    case DE_EXPR_ROTR:
      bindShiftExpression(scopeBlock, expression);
      break;
    case DE_EXPR_LT:
    case DE_EXPR_LE:
    case DE_EXPR_GT:
    case DE_EXPR_GE:
      bindRelationalExpression(scopeBlock, expression);
      break;
    case DE_EXPR_EQUAL:
    case DE_EXPR_NOTEQUAL:
      bindEqualityExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NEGATE:
    case DE_EXPR_NEGATETRUNC:
    case DE_EXPR_BITNOT:
      bindUnaryArithmeticExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NOT:
      bindNotExpression(scopeBlock, expression);
      break;
    case DE_EXPR_CAST:
    case DE_EXPR_CASTTRUNC:
      bindCastExpression(scopeBlock, expression);
      break;
    case DE_EXPR_SELECT:
      bindSelectExpression(scopeBlock, expression);
      break;
    case DE_EXPR_CALL:
      bindCallExpression(scopeBlock, expression, false);
      break;
    case DE_EXPR_INDEX:
      bindIndexExpression(scopeBlock, expression);
      break;
    case DE_EXPR_SLICE:
      bindSliceExpression(scopeBlock, expression);
      break;
    case DE_EXPR_SECRET:
    case DE_EXPR_REVEAL:
      bindMarkSecretOrPublic(scopeBlock, expression);
      deExpressionSetIsType(expression,
          deExpressionIsType(deExpressionGetFirstExpression(expression)));
      break;
    case DE_EXPR_EQUALS:
      bindAssignmentExpression(scopeBlock, expression, false);
      break;
    case DE_EXPR_ADD_EQUALS:
    case DE_EXPR_SUB_EQUALS:
    case DE_EXPR_MUL_EQUALS:
    case DE_EXPR_DIV_EQUALS:
    case DE_EXPR_MOD_EQUALS:
    case DE_EXPR_AND_EQUALS:
    case DE_EXPR_OR_EQUALS :
    case DE_EXPR_XOR_EQUALS:
    case DE_EXPR_EXP_EQUALS:
    case DE_EXPR_SHL_EQUALS:
    case DE_EXPR_SHR_EQUALS:
    case DE_EXPR_ROTL_EQUALS:
    case DE_EXPR_ROTR_EQUALS:
    case DE_EXPR_BITAND_EQUALS:
    case DE_EXPR_BITOR_EQUALS:
    case DE_EXPR_BITXOR_EQUALS:
    case DE_EXPR_ADDTRUNC_EQUALS:
    case DE_EXPR_SUBTRUNC_EQUALS:
    case DE_EXPR_MULTRUNC_EQUALS:
      bindAssignmentOperatorExpression(scopeBlock, expression);
      break;
    case DE_EXPR_DOT:
      bindDotExpression(scopeBlock, expression);
      break;
    case DE_EXPR_DOTDOTDOT:
      bindDotDotDotExpression(scopeBlock, expression);
      break;
    case DE_EXPR_LIST:
      // Happens in print statements.
      bindExpressionList(scopeBlock, expression);
      break;
    case DE_EXPR_TUPLE:
      bindTupleExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NULL:
      bindNullExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NOTNULL:
      bindNotNullExpression(scopeBlock, expression);
      break;
    case DE_EXPR_FUNCADDR:
      bindFunctionPointerExpression(scopeBlock, expression);
      break;
    case DE_EXPR_ARRAYOF:
      bindArrayofExpression(scopeBlock, expression);
      break;
      break;
    case DE_EXPR_TYPEOF:
      bindTypeofExpression(scopeBlock, expression);
      break;
    case DE_EXPR_UNSIGNED:
    case DE_EXPR_SIGNED:
      bindSignConversionExpression(scopeBlock, expression);
      break;
    case DE_EXPR_WIDTHOF:
      bindWidthofExpression(scopeBlock, expression);
      break;
    case DE_EXPR_ISNULL:
      bindIsnullExpression(scopeBlock, expression);
      break;
    case DE_EXPR_UINTTYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deUintDatatypeCreate(deExpressionGetWidth(expression)));
      break;
    case DE_EXPR_INTTYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deIntDatatypeCreate(deExpressionGetWidth(expression)));
      break;
    case DE_EXPR_FLOATTYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deFloatDatatypeCreate(deExpressionGetWidth(expression)));
      break;
    case DE_EXPR_STRINGTYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deStringDatatypeCreate());
      break;
    case DE_EXPR_BOOLTYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deBoolDatatypeCreate());
      break;
    case DE_EXPR_AS:
      utExit("Unexpected expression type");
      break;
    case DE_EXPR_IN:
      bindInExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NAMEDPARAM:
      bindNamedParameter(scopeBlock, expression);
      break;
  }
}

// Update the functions return value.  If it is not set, set it.  If it is set,
// verify that it is the same as the type of the return statement.
static void updateFunctionType(deBlock scopeBlock, deExpression expression, deLine line) {
  deDatatype datatype = deSignatureGetReturnType(deCurrentSignature);
  if (datatype == deDatatypeNull) {
    // Must be the first
    if (expression != deExpressionNull) {
      setSignatureReturnType(scopeBlock, deCurrentSignature,
          deExpressionGetDatatype(expression), line);
    } else {
      setSignatureReturnType(scopeBlock, deCurrentSignature, deNoneDatatypeCreate(), line);
    }
  } else {
    // Verify the type is the same.
    if (expression == deExpressionNull) {
      if (datatype != deNoneDatatypeCreate()) {
        deError(line, "Return statement without a return value cannot come after one "
            "with a return value.");
      }
    } else {
      if (deExpressionAutocast(expression)) {
        autocastExpression(expression, datatype);
      }
      deDatatype unifiedType = deUnifyDatatypes(datatype, deExpressionGetDatatype(expression));
      if (unifiedType == deDatatypeNull) {
        deError(line,
            "Return statement has different type than prior return statement:%s",
            deGetOldVsNewDatatypeStrings(deExpressionGetDatatype(expression), datatype));
      }
      if (unifiedType != datatype) {
        setSignatureReturnType(scopeBlock, deCurrentSignature, unifiedType, line);
      }
    }
  }
}

// Bind the first matching case type statement.  Only this case will result in
// generating code.  The others will be marked as disabled.
static void bindFirstMatchingCaseTypeStatement(deBlock scopeBlock,
    deStatement switchStatement, deDatatype datatype) {
  deStatement savedStatement = deCurrentStatement;
  deBlock subBlock = deStatementGetSubBlock(switchStatement);
  bool foundMatchingCase = false;
  deStatement caseStatement;
  deForeachBlockStatement(subBlock, caseStatement) {
    deCurrentStatement = caseStatement;
    deStatementSetInstantiated(caseStatement, false);
    if (!foundMatchingCase) {
      if (deStatementGetType(caseStatement) == DE_STATEMENT_CASE) {
        deExpression typeExpressionList = deStatementGetExpression(caseStatement);
        deExpression typeExpression;
        deForeachExpressionExpression(typeExpressionList, typeExpression) {
          if (deDatatypeMatchesTypeExpression(scopeBlock, datatype, typeExpression)) {
            foundMatchingCase = true;
          }
        } deEndExpressionExpression;
      } else {
        utAssert(deStatementGetType(caseStatement) == DE_STATEMENT_DEFAULT);
        foundMatchingCase = true;
      }
      if (foundMatchingCase && deInstantiating) {
        deStatementSetInstantiated(caseStatement, true);
        deBlock caseBlock = deStatementGetSubBlock(caseStatement);
        bindBlock(scopeBlock, caseBlock, deSignatureNull);
        deBlockSetCanContinue(subBlock, deBlockCanContinue(caseBlock));
        deBlockSetCanReturn(subBlock, deBlockCanReturn(caseBlock));
      }
    }
  } deEndBlockStatement;
  deCurrentStatement = savedStatement;
  if (!foundMatchingCase) {
    deError(deStatementGetLine(switchStatement), "No matching case found");
  }
}

// Add a default case that throws an error if the default is missing.
static void addDefaultIfMissing(deStatement switchStatement) {
  deBlock block = deStatementGetSubBlock(switchStatement);
  deStatement defaultCase = deBlockGetLastStatement(block);
  if (defaultCase == deStatementNull || deStatementGetType(defaultCase) != DE_STATEMENT_DEFAULT) {
    deLine line = deStatementGetLine(switchStatement);
    deStatement statement = deStatementCreate(block, DE_STATEMENT_DEFAULT, line);
    deFilepath filepath = deBlockGetFilepath(deStatementGetBlock(statement));
    deBlock subBlock = deBlockCreate(filepath, DE_BLOCK_STATEMENT, line);
    deStatementInsertSubBlock(statement, subBlock);
    deStatement throwStatement = deStatementCreate(subBlock, DE_STATEMENT_THROW, line);
    deExpression expression = deExpressionCreate(DE_EXPR_LIST, line);
    deStatementInsertExpression(throwStatement, expression);
    deExpression message = deStringExpressionCreate(
        deMutableCStringCreate("No case matched switch expression"), line);
    deExpressionAppendExpression(expression, message);
  }
}

// Bind all the case statements in the switch statement.  Verify that they all
// have the same type.  Mark all the cases as enabled.  A switch-statement
// without a default case that has no match throws an exception.
static void bindCaseStatements(deBlock scopeBlock, deStatement switchStatement, deDatatype datatype) {
  deStatement savedStatement = deCurrentStatement;
  addDefaultIfMissing(switchStatement);
  bool canContinue = false;
  bool canReturn = false;
  deBlock subBlock = deStatementGetSubBlock(switchStatement);
  deStatement caseStatement;
  deForeachBlockStatement(subBlock, caseStatement) {
    deCurrentStatement = caseStatement;
    deStatementSetInstantiated(caseStatement, deInstantiating);
    deLine line = deStatementGetLine(caseStatement);
    if (deStatementGetType(caseStatement) == DE_STATEMENT_CASE) {
      deExpression listExpression = deStatementGetExpression(caseStatement);
      bindExpression(scopeBlock, listExpression);
      deExpression expression;
      deForeachExpressionExpression(listExpression, expression) {
        if (deExpressionGetDatatype(expression) != datatype) {
          deError(line, "Case expression has different type than switch expression:%s",
            deGetOldVsNewDatatypeStrings(deExpressionGetDatatype(expression), datatype));
        }
      } deEndExpressionExpression;
    }
    deBlock statementBlock = deStatementGetSubBlock(caseStatement);
    bindBlock(scopeBlock, statementBlock, deSignatureNull);
    canReturn |= deBlockCanReturn(statementBlock);
    canContinue |= deBlockCanContinue(statementBlock);
  } deEndBlockStatement;
  deBlockSetCanReturn(subBlock, canReturn);
  deBlockSetCanContinue(subBlock, canContinue);
  deCurrentStatement = savedStatement;
}

// Bind a switch statement.  Bind all cases and confirm the expressions have
// the same type.
static void bindSwitchStatement(deBlock scopeBlock, deStatement switchStatement) {
  deExpression switchExpression = deStatementGetExpression(switchStatement);
  bindExpression(scopeBlock, switchExpression);
  deDatatype datatype = deExpressionGetDatatype(switchExpression);
  if (deExpressionIsType(switchExpression)) {
    deError(deExpressionGetLine(switchExpression),
        "Cannot switch on a type.  Did you mean typeswitch?");
  }
  bindCaseStatements(scopeBlock, switchStatement, datatype);
}

// Only bind the first matching type expression.
static void bindTypeswitchStatement(deBlock scopeBlock, deStatement switchStatement) {
  deExpression switchExpression = deStatementGetExpression(switchStatement);
  bindExpression(scopeBlock, switchExpression);
  deDatatype datatype = deExpressionGetDatatype(switchExpression);
  bindFirstMatchingCaseTypeStatement(scopeBlock, switchStatement, datatype);
}

// Bind the statement.
static void bindStatement(deBlock scopeBlock, deStatement statement) {
  deStatement savedStatement = deCurrentStatement;
  deCurrentStatement = statement;
  deStatementType statementType = deStatementGetType(statement);
  if (statementType == DE_STATEMENT_SWITCH) {
    bindSwitchStatement(scopeBlock, statement);
    deCurrentStatement = savedStatement;
    return;
  }
  if (statementType == DE_STATEMENT_TYPESWITCH) {
    bindTypeswitchStatement(scopeBlock, statement);
    deCurrentStatement = savedStatement;
    return;
  }
  deLine line = deStatementGetLine(statement);
  deExpression expression = deStatementGetExpression(statement);
  if (expression != deExpressionNull && !deStatementIsImport(statement)) {
    if ((statementType == DE_STATEMENT_RETURN || statementType == DE_STATEMENT_YIELD)
        && deBlockGetType(scopeBlock) == DE_BLOCK_FUNCTION) {
      // This helps report recursion errors in a less confusing way.
      deFunction function = deBlockGetOwningFunction(scopeBlock);
      deFunctionSetReturnsValue(function, true);
    }
    bindExpression(scopeBlock, expression);
    deDatatype datatype = deExpressionGetDatatype(expression);
    deDatatypeType type = deDatatypeGetType(datatype);
    deLine line = deExpressionGetLine(expression);
    switch (statementType) {
      case DE_STATEMENT_IF:
      case DE_STATEMENT_WHILE:
        if (type != DE_TYPE_BOOL) {
          deError(line, "Boolean type required");
        }
        if (deDatatypeSecret(datatype)) {
          deError(line, "Branching on a secret is not allowed");
        }
        break;
      case DE_STATEMENT_FOR:
        datatype = deExpressionGetDatatype(
            deExpresssionIndexExpression(expression, 1));
        if (deDatatypeGetType(datatype) != DE_TYPE_BOOL) {
          deError(line, "Boolean type required");
        }
        if (deDatatypeSecret(datatype)) {
          deError(line, "Branching on a secret is not allowed");
        }
        break;
      case DE_STATEMENT_PRINT: {
        deExpression child;
        deSafeForeachExpressionExpression(expression, child) {
          // Use safe loop since child may be replaced with child.toString()
          if (deDatatypeSecret(deExpressionGetDatatype(child))) {
            deError(line, "Printing a secret is not allowed");
          }
          checkExpressionIsPrintable(scopeBlock, child, true);
        } deEndSafeExpressionExpression;
        break;
      }
      case DE_STATEMENT_REF:
      case DE_STATEMENT_UNREF:
        if (deDatatypeGetType(datatype) != DE_TYPE_CLASS) {
          deError(line, "Ref/unref statements require an instance of a class");
        }
        break;
      default:
        break;
    }
  }
  if (statementType == DE_STATEMENT_RETURN || statementType == DE_STATEMENT_YIELD) {
    if (deBlockGetOwningFunction(scopeBlock) != deFunctionNull) {
      deFunction function = deBlockGetOwningFunction(scopeBlock);
      if (deFunctionGetType(function) != DE_FUNC_CONSTRUCTOR) {
        updateFunctionType(scopeBlock, expression, deStatementGetLine(statement));
      }
    } else if (deStatementGetExpression(statement) != deExpressionNull) {
      if (scopeBlock == deRootGetBlock(deTheRoot)) {
        deError(line, "Cannot return a value from global scope");
      } else {
        deError(line, "Constructors cannot return a value");
      }
    }
  }
  deBlock subBlock = deStatementGetSubBlock(statement);
  if (subBlock != deBlockNull) {
    bindBlock(scopeBlock, subBlock, deSignatureNull);
  }
  deCurrentStatement = savedStatement;
}

// Bind parameter variables to the given types, in order.
static void bindParameters(deBlock block, deSignature signature) {
  deVariable variable = deBlockGetFirstVariable(block);
  deFunction function = deSignatureGetFunction(signature);
  deFunctionType funcType = deFunctionGetType(function);
  uint32 xDatatype = 0;  // Index into signature datatypes.
  deLine line = deSignatureGetLine(signature);
  if (funcType == DE_FUNC_CONSTRUCTOR) {
    // This is a constructor.  Bind the self parameter to the return type.
    deVariableSetDatatype(variable, deSignatureGetReturnType(signature));
    variable = deVariableGetNextBlockVariable(variable);
    xDatatype++;
  } else {
    utAssert(function != deFunctionNull);
  }
  while (variable != deVariableNull && deVariableGetType(variable) == DE_VAR_PARAMETER) {
    deDatatype datatype = deSignatureGetiType(signature, xDatatype);
    if (funcType == DE_FUNC_CONSTRUCTOR && deDatatypeGetType(datatype) == DE_TYPE_TCLASS) {
      // Bind null-self parameters to the self-type.
      datatype = deSignatureGetReturnType(signature);
    }
    xDatatype++;
    deVariableSetDatatype(variable, datatype);
    // Bind assuming all parameters are instantiated, and prune
    // uninstantiated parameters when generating code.  The instantiated
    // flags are set on signatures post-binding.
    deVariableSetIsType(variable, false);
    deDatatypeType type = deDatatypeGetType(datatype);
    if (type == DE_TYPE_NONE || type == DE_TYPE_FUNCTION) {
      deError(line, "Invalid type expression passed to parameter %s: %s.",
              deVariableGetName(variable), deDatatypeTypeGetName(type));
    }
    variable = deVariableGetNextBlockVariable(variable);
  }
  utAssert(xDatatype == deSignatureGetUsedParamspec(signature));
  checkParameterTypeConstraints(block, line);
}

// Check that the variables that have been assigned types rather than values are
// never instantiated.
static void checkForInstantiatingTypeVariables(deBlock block) {
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    if (deVariableIsType(variable) && deVariableInstantiated(variable)) {
      deLine line = deVariableGetLine(variable);
      deError(line, "Variable %s is assigned a type, but also instantiated",
              deVariableGetName(variable));
    }
  } deEndBlockVariable;
}

// Reset the binding on the block so it can be bound again.  This is needed
// because functions are bound once for each set of unique parameter signatures
// passed to the function.  Datatypes on expressions do not need to be reset,
// but non-parameter variables should be deleted.
static void  resetBlockBinding(deBlock block) {
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    deVariableSetDatatype(variable, deDatatypeNull);
    deVariableSetInstantiated(variable, false);
    deVariableSetIsType(variable, false);
  } deEndBlockVariable;
  if (deBlockIsDestructor(block)) {
    // The self variable of destructors needs to be marked as instantiated.
    deVariable self = deBlockGetFirstVariable(block);
    deVariableSetInstantiated(self, deInstantiating);
  }
  if (block == deRootGetBlock(deTheRoot)) {
    bindArgvVariable(block);
  }
}

// This special case is for an if-elseif-else chain of statements that has an
// else clause, and where every sub-block cannot continue.
static bool allIfClausesReturn(deStatement statement) {
  do {
    deStatementType type = deStatementGetType(statement);
    if (type != DE_STATEMENT_IF && type != DE_STATEMENT_ELSEIF &&
        type != DE_STATEMENT_ELSE) {
      return true;
    }
    deBlock subBlock = deStatementGetSubBlock(statement);
    if (deBlockCanContinue(subBlock)) {
      return false;
    }
    statement = deStatementGetPrevBlockStatement(statement);
  } while (statement != deStatementNull);
  return true;
}

// Return true if the called function can return.
static bool callCanReturn(deExpression callExpression) {
  deExpression accessExpression = deExpressionGetFirstExpression(callExpression);
  deDatatype datatype = deExpressionGetDatatype(accessExpression);
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_TCLASS:
    case DE_TYPE_FUNCPTR:
      return true;
    case DE_TYPE_FUNCTION: {
      deFunction function = deDatatypeGetFunction(datatype);
      deBlock subBlock = deFunctionGetSubBlock(function);
      return deBlockCanReturn(subBlock);
    }
    default:
      utExit("Unexpected call access expression type");
  }
  return false;  // Dummy return;
}

// Update the canContinue and canReturn parameters given the bound statement.
static void updateReachability(deStatement statement, bool* canContinue, bool* canReturn) {
  deBlock subBlock = deStatementGetSubBlock(statement);
  bool subBlockCanReturn = false;
  bool subBlockCanContinue = true;
  if (subBlock != deBlockNull) {
    subBlockCanReturn = deBlockCanReturn(subBlock);
    subBlockCanContinue = deBlockCanContinue(subBlock);
    *canReturn |= subBlockCanReturn;
  }
  switch (deStatementGetType(statement)) {
    case DE_STATEMENT_IF:
    case DE_STATEMENT_ELSEIF:
      break;
    case DE_STATEMENT_ELSE:
      if (allIfClausesReturn(statement)) {
        *canContinue = false;
      }
      break;
    case DE_STATEMENT_SWITCH:
    case DE_STATEMENT_TYPESWITCH:
      *canContinue &= subBlockCanContinue;
      break;
    case DE_STATEMENT_DO:
      *canContinue &= subBlockCanContinue;
      break;
    case DE_STATEMENT_CALL:
      *canContinue &= callCanReturn(deStatementGetExpression(statement));
      break;
    case DE_STATEMENT_THROW:
      *canContinue = false;
      break;
    case DE_STATEMENT_RETURN:
      *canContinue = false;
      *canReturn = true;
      break;
    case DE_STATEMENT_YIELD:
      *canReturn = true;
      break;
    case DE_STATEMENT_ASSIGN:
    case DE_STATEMENT_WHILE:
    case DE_STATEMENT_FOR:
    case DE_STATEMENT_FOREACH:
    case DE_STATEMENT_PRINT:
    case DE_STATEMENT_USE:
    case DE_STATEMENT_IMPORT:
    case DE_STATEMENT_IMPORTLIB:
    case DE_STATEMENT_IMPORTRPC:
    case DE_STATEMENT_REF:
    case DE_STATEMENT_UNREF:
      // Always can continue through these.
      break;
    case DE_STATEMENT_CASE:
    case DE_STATEMENT_DEFAULT:
    case DE_STATEMENT_APPENDCODE:
    case DE_STATEMENT_PREPENDCODE:
    case DE_STATEMENT_RELATION:
    case DE_STATEMENT_GENERATE:
      utExit("Unexpected statement type");
      break;
  }
}

// Determine if the block is an iterator.
static bool blockIsIterator(deBlock block) {
  if (deBlockGetType(block) != DE_BLOCK_FUNCTION) {
    return false;
  }
  return deFunctionGetType(deBlockGetOwningFunction(block)) == DE_FUNC_ITERATOR;
}

// Determine if the expression is bound to an iterator.
static bool datatypeIsIterator(deDatatype datatype) {
  if (deDatatypeGetType(datatype) != DE_TYPE_FUNCTION) {
    return false;
  }
  deFunction function = deDatatypeGetFunction(datatype);
  return deFunctionGetType(function) == DE_FUNC_ITERATOR;
}

// Automatically add .values() in for <var> in <expr> statements when <expr>
// does not already name an iterator.  This lets us use Python-like loops like
// 'for i in [1, 2, 3] {'.  It also allows classes to define 'iterator
// values(self)' so for example, an instance of Set called set could work with
// 'for element in set {'.
static void addValuesIteratorIfNeeded(deBlock scopeBlock, deStatement statement) {
  deExpression assignment = deStatementGetExpression(statement);
  deExpression access = deExpressionGetFirstExpression(assignment);
  deExpression callExpr = deExpressionGetNextExpression(access);
  bindExpression(scopeBlock, callExpr);
  if (deExpressionGetType(callExpr) == DE_EXPR_CALL) {
    deDatatype datatype = deExpressionGetDatatype(deExpressionGetFirstExpression(callExpr));
    if (datatypeIsIterator(datatype)) {
      return;  // Already have an iterator.
    }
  }
  // Add .values().
  deExpressionRemoveExpression(assignment, callExpr);
  deLine line = deExpressionGetLine(callExpr);
  deExpression valuesExpr = deIdentExpressionCreate(utSymCreate("values"), line);
  deExpression dotExpr = deBinaryExpressionCreate(DE_EXPR_DOT, callExpr, valuesExpr, line);
  deExpression emptyParamsExpr = deExpressionCreate(DE_EXPR_LIST, line);
  deExpression valuesCallExpr = deBinaryExpressionCreate(DE_EXPR_CALL, dotExpr,
      emptyParamsExpr, line);
  deExpressionAppendExpression(assignment, valuesCallExpr);
}

// Determine all expression and variable types.  This function can be called
// multiple times on the same block with different parameter types.  Perform
// reachability analysis to determine if the block can return and if it can also
// continue.  Scope-level blocks that can continue have a return added at the
// end so they will instead return.  Unreachable statements result in an error.
static void bindBlock(deBlock scopeBlock, deBlock block, deSignature signature) {
  resetBlockBinding(block);
  if (signature != deSignatureNull) {
    deFunction function = deSignatureGetFunction(signature);
    bindParameters(block, signature);
    if (deFunctionExtern(function)) {
      deCreateFullySpecifiedSignature(function);
      deBlockSetCanReturn(deFunctionGetSubBlock(function), true);
      return;  // External linked functions do not have internals.
    }
  }
  // The empty block continues, and does not return.
  bool canContinue = true;
  bool canReturn = false;
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deLine line = deStatementGetLine(statement);
    if (deStatementGetType(statement) == DE_STATEMENT_RELATION ||
        deStatementGetType(statement) == DE_STATEMENT_GENERATE) {
      deStatementSetInstantiated(statement, false);
      if (deBlockGetModuleFilepath(scopeBlock) == deFilepathNull) {
        deError(line, "Relation statements must be in the global scope");
      }
    } else {
      if (!canContinue) {
        deError(line, "Cannot reach statement");
      }
      if (deStatementGetType(statement) == DE_STATEMENT_FOREACH) {
        addValuesIteratorIfNeeded(scopeBlock, statement);
        if (deInlining) {
          utAssert(deInstantiating);
          // This sets the signature on the call expression, so the iterator
          // function can be bound in deInlineIterator.
          deStatement savedStatement = deCurrentStatement;
          deCurrentStatement = statement;
          // Bind the whole statement to create variables assigned in the body.
          bindStatement(scopeBlock, statement);
          deCurrentStatement = savedStatement;
          deInlining = false;
          statement = deInlineIterator(scopeBlock, statement);
          deInlining = true;
        }
      }
      deStatementSetInstantiated(statement, deInstantiating);
      bindStatement(scopeBlock, statement);
      updateReachability(statement, &canContinue, &canReturn);
    }
  } deEndBlockStatement;
  if (block == scopeBlock) {
    checkForInstantiatingTypeVariables(block);
    if (canContinue && !blockIsIterator(scopeBlock)) {
      // Add a return at the end of the block.
      deLine line = deBlockGetLine(block);
      deStatement lastStatement = deBlockGetLastStatement(block);
      if (lastStatement != deStatementNull) {
        line = deStatementGetLine(lastStatement);
      }
      statement = deStatementCreate(block, DE_STATEMENT_RETURN, line);
      if (block == deRootGetBlock(deTheRoot)) {
        // Add return 0; at the end of main().
        deBigint zero = deInt32BigintCreate(0);
        deStatementSetExpression(statement, deIntegerExpressionCreate(zero, line));
      }
      deStatementSetInstantiated(statement, true);
      bindStatement(scopeBlock, statement);
      updateReachability(statement, &canContinue, &canReturn);
    } else if (signature != deSignatureNull &&
               deSignatureGetReturnType(signature) == deDatatypeNull) {
      deLine line = deSignatureGetLine(signature);
      setSignatureReturnType(scopeBlock, signature, deNoneDatatypeCreate(), line);
    }
    if (deCurrentClass != deClassNull) {
      bindLazySignatures(deCurrentClass);
    }
  }
  deConstantPropagation(scopeBlock, block);
  deBlockSetCanReturn(block, canReturn);
  deBlockSetCanContinue(block, canContinue);
}

// Instantiate a relation.
void deInstantiateRelation(deStatement statement) {
  deSignature savedSignature = deCurrentSignature;
  deClass savedClass = deCurrentClass;
  deStatement savedStatement = deCurrentStatement;
  deCurrentStatement = statement;
  bool savedInstantiating = deInstantiating;
  deInstantiating = true;
  deExecuteRelationStatement(statement);
  deInstantiating = savedInstantiating;
  deCurrentStatement = savedStatement;
  deCurrentSignature = savedSignature;
  deCurrentClass = savedClass;
}

// Bind exported functions and constructors.
static void bindExports(void) {
  deFunction function;
  deForeachRootFunction(deTheRoot, function) {
    if (deFunctionExported(function)) {
      deCreateFullySpecifiedSignature(function);
      if (deFunctionExtern(function)) {
        // We don't bind external functions, but we do look at its block to see
        // if it can return.
        deBlockSetCanReturn(deFunctionGetSubBlock(function), true);
      }
    }
  } deEndRootFunction;
}

// After type binding, any NULL classes that still exist are the result of
// declaring classes that were never constructed.  These can be destroyed.
// The remaining code after destroying this unused code should be fully bound,
// and ready for code generation.
static void destroyUnusedTclassesContents(void) {
  // This iterator is tricky because if we destroy an tclass, and it has an
  // inner tclass, we'll destroy that too, breaking the assumption made by the
  // auto-generated safe iterators.  Inner tclasses are always after their
  // outer tclasses, so it should be safe to destroy them in a backwards
  // traversal of tclasses.
  deTclass tclass, prevTclass;
  for (tclass = deRootGetLastTclass(deTheRoot); tclass != deTclassNull;
       tclass = prevTclass) {
    prevTclass = deTclassGetPrevRootTclass(tclass);
    if (!deTclassBuiltin(tclass) && deTclassGetNumClasses(tclass) == 0) {
      deDestroyTclassContents(tclass);
    }
  }
}

// Add default show methods for classes that have not defined them.
static void addDefaultShowMethods() {
  deClass theClass;
  deForeachRootClass(deTheRoot, theClass) {
    if (deClassBound(theClass)) {
      addDefaultClassShowMethod(theClass);
    }
  } deEndRootClass;
}

// Bind types to all expressions.  Propagates through functions, and can create
// new functions with different bytecode when the parameters are different.
// Keep track of all function signatures.
void deBind(void) {
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  deFunction mainFunc = deBlockGetOwningFunction(rootBlock);
  deInstantiating = true;
  deInlining = false;  // Inlining is done when the code generators re-bind the block.
  deBindingAssignmentTarget = false;
  deSignature mainSignature = deSignatureCreate(mainFunc,
      deDatatypeArrayAlloc(), deFunctionGetLine(mainFunc));
  deSignatureSetInstantiated(mainSignature, true);
  deDatatype mainReturnType = deIntDatatypeCreate(32);
  deSignatureSetReturnType(mainSignature, mainReturnType);
  deCurrentSignature = mainSignature;
  deCurrentClass = deClassNull;
  bindBlock(rootBlock, rootBlock, mainSignature);
  deCurrentStatement = deStatementNull;
  bindExports();
  destroyUnusedTclassesContents();
  if (deDebugMode) {
    addDefaultShowMethods();
  }
}

// This is used to bind new statements after adding memory management stuff.
void deBindNewStatement(deBlock scopeBlock, deStatement statement) {
  deInstantiating = true;
  deCurrentSignature = deSignatureNull;
  deCurrentStatement = deStatementNull;
  deCurrentClass = deClassNull;
  bindStatement(scopeBlock, statement);
}

// Initialize global variables.
void deBindStart(void) {
  deInstantiating = false;
  deInlining = false;
  deBindingAssignmentTarget = false;
  deCurrentSignature = deSignatureNull;
  deCurrentStatement = deStatementNull;
  deCurrentClass = deClassNull;
}

// Bind a block.  Binding a block is idempotent: it can be called multiple times.
void deBindBlock(deBlock block, deSignature signature, bool inlineIterators) {
  if (deUseNewBinder) {
    deApplySignatureBindings(signature);
    return;
  }
  deInstantiating = true;
  deInlining = inlineIterators;
  bool savedBindingAssignmentTarget = deBindingAssignmentTarget ;
  deBindingAssignmentTarget = false;
  deSignature savedSignature = deCurrentSignature;
  deCurrentSignature = signature;
  deStatement savedStatement = deCurrentStatement;
  deCurrentStatement = deStatementNull;
  deClass savedClass = deCurrentClass;
  deCurrentClass = deClassNull;
  bindBlock(block, block, signature);
  deCurrentClass = savedClass;
  deCurrentStatement = savedStatement;
  deCurrentSignature = savedSignature;
  deBindingAssignmentTarget = savedBindingAssignmentTarget;
}

// Bind an expression.  The caller is responsible for setting deInstantiating.
void deBindExpression(deBlock scopeBlock, deExpression expression) {
  bindExpression(scopeBlock, expression);
}

// Bind extern RPCs.  These have no implementation, but we need to generate code
// for them.
void deBindRPCs(void) {
  deFunction function;
  deForeachRootFunction(deTheRoot, function) {
    if (deFunctionGetLinkage(function) == DE_LINK_EXTERN_RPC) {
      deCreateFullySpecifiedSignature(function);
    }
  } deEndRootFunction;
}
