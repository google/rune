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

// The global array class.
deTclass deArrayTclass, deFuncptrTclass, deFunctionTclass, deBoolTclass, deStringTclass,
    deUintTclass, deIntTclass, deModintTclass, deFloatTclass, deTupleTclass,
    deStructTclass, deEnumTclass, deClassTclass, deNoneTclass;

// Builtin methods.
static deFunction deArrayLengthFunc, deArrayResizeFunc, deArrayAppendFunc,
    deArrayConcatFunc, deArrayReverseFunc, deStringLengthFunc,
    deStringResizeFunc, deStringAppendFunc, deStringConcatFunc,
    deStringReverseFunc, deStringToUintLEFunc, deUintToStringLEFunc,
    deStringToUintBEFunc, deUintToStringBEFunc, deStringToHexFunc,
    deHexToStringFunc, deFindFunc, deRfindFunc, deArrayToStringFunc,
    deBoolToStringFunc, deUintToStringFunc, deIntToStringFunc,
    deTupleToStringFunc, deStructToStringFunc, deEnumToStringFunc;

deTclass deFindTypeTclass(deDatatypeType type) {
  switch (type) {
    case DE_TYPE_BOOL:
      return deBoolTclass;
    case DE_TYPE_STRING:
      return deStringTclass;
    case DE_TYPE_UINT:
      return deUintTclass;
    case DE_TYPE_INT:
      return deIntTclass;
    case DE_TYPE_MODINT:
      return deModintTclass;
    case DE_TYPE_FLOAT:
      return deFloatTclass;
    case DE_TYPE_ARRAY:
      return deArrayTclass;
    case DE_TYPE_FUNCTION:
      return deFunctionTclass;
    case DE_TYPE_FUNCPTR:
      return deFuncptrTclass;
    case DE_TYPE_TUPLE:
      return deTupleTclass;
    case DE_TYPE_STRUCT:
      return deStructTclass;
    case DE_TYPE_ENUM:
    case DE_TYPE_ENUMCLASS:
      return deEnumTclass;
    case DE_TYPE_CLASS:
      return deClassTclass;
    case DE_TYPE_TCLASS:
    case DE_TYPE_NONE:
    case DE_TYPE_EXPR:
      utExit("Unexpected type");
  }
  return deTclassNull;  // Dummy return.
}

// Create a builtin class.  Parameters are strings.  All builtin methods have
// custom code to verify parameter types and determine the return time.
static deTclass createBuiltinTclass(char* name, deBuiltinTclassType type,
                                    uint32 numParameters, ...) {
  va_list ap;
  va_start(ap, numParameters);
  deBlock globalBlock = deRootGetBlock(deTheRoot);
  deFilepath filepath = deBlockGetFilepath(globalBlock);
  deFunction function = deFunctionCreate(filepath, globalBlock, DE_FUNC_CONSTRUCTOR,
      utSymCreate(name), DE_LINK_BUILTIN, 0);
  deTclass tclass = deTclassCreate(function, 32, deLineNull);
  deTclassSetBuiltinType(tclass, type);
  deBlock subBlock = deFunctionGetSubBlock(function);
  // Add self parameter.
  deVariableCreate(subBlock, DE_VAR_PARAMETER, false, utSymCreate("self"), deExpressionNull, false, 0);
  for (uint32 i = 0; i < numParameters; i++) {
    char* name = va_arg(ap, char*);
    deVariableCreate(subBlock, DE_VAR_PARAMETER, false, utSymCreate(name), deExpressionNull, false, 0);
  }
  va_end(ap);
  return tclass;
}

// Create a builtin method.
static deFunction addMethod(deTclass tclass, deBuiltinFuncType type, char* name, uint32 numParameters, ...) {
  va_list ap;
  va_start(ap, numParameters);
  deBlock subBlock = deFunctionGetSubBlock(deTclassGetFunction(tclass));
  utSym sym = utSymCreate(name);
  deFilepath filepath = deBlockGetFilepath(subBlock);
  utAssert(filepath != deFilepathNull);
  deFunction function = deFunctionCreate(filepath, subBlock, DE_FUNC_PLAIN, sym, DE_LINK_BUILTIN, 0);
  deFunctionSetBuiltinType(function, type);
  deBlock funcBlock = deFunctionGetSubBlock(function);
  deBlockSetCanReturn(funcBlock, true);
  // Add self parameter.
  deVariableCreate(funcBlock, DE_VAR_PARAMETER, false, utSymCreate("self"), deExpressionNull, false, 0);
  for (uint32 i = 0; i < numParameters; i++) {
    char* name = va_arg(ap, char*);
    deVariableCreate(funcBlock, DE_VAR_PARAMETER, false, utSymCreate(name), deExpressionNull, false, 0);
  }
  va_end(ap);
  return function;
}

// Set a default parameter value.
static void setParameterDefault(deFunction function, uint32 index, deExpression value) {
  deBlock subBlock = deFunctionGetSubBlock(function);
  deVariable var = deBlockIndexVariable(subBlock, index);
  utAssert(var != deVariableNull && deVariableGetType(var) == DE_VAR_PARAMETER);
  deVariableInsertInitializerExpression(var, value);
}

// Initialize the builtin classes module.
void deBuiltinStart(void) {
  deArrayTclass = createBuiltinTclass("Array", 1, DE_BUILTINTCLASS_ARRAY, "elementType");
  deArrayLengthFunc = addMethod(deArrayTclass, DE_BUILTINFUNC_ARRAYLENGTH, "length", 0);
  deArrayResizeFunc = addMethod(deArrayTclass, DE_BUILTINFUNC_ARRAYRESIZE, "resize", 1, "length");
  deArrayAppendFunc = addMethod(deArrayTclass, DE_BUILTINFUNC_ARRAYAPPEND, "append", 1, "element");
  deArrayConcatFunc = addMethod(deArrayTclass, DE_BUILTINFUNC_ARRAYCONCAT, "concat", 1, "array");
  deArrayReverseFunc = addMethod(deArrayTclass, DE_BUILTINFUNC_ARRAYREVERSE, "reverse", 0);
  deArrayToStringFunc = addMethod(deArrayTclass, DE_BUILTINFUNC_ARRAYTOSTRING, "toString", 0);
  createBuiltinTclass("Funcptr", DE_BUILTINTCLASS_FUNCPTR, 2, "function", "parameterArray");
  // TODO: upgrade Function constructor to take statement expression and
  // construct the function.  This would implement lambda expressions.
  deFunctionTclass = createBuiltinTclass("Function", DE_BUILTINTCLASS_FUNCTION, 0);
  // The value class constructors just return the value passed, and are not
  // particularly useful.
  deBoolTclass = createBuiltinTclass("Bool", DE_BUILTINTCLASS_BOOL, 1, "value");
  deBoolToStringFunc = addMethod(deBoolTclass, DE_BUILTINFUNC_BOOLTOSTRING, "toString", 0);
  deStringTclass = createBuiltinTclass("String", DE_BUILTINTCLASS_STRING, 1, "value");
  deStringLengthFunc = addMethod(deStringTclass, DE_BUILTINFUNC_STRINGLENGTH, "length", 0);
  deStringResizeFunc = addMethod(deStringTclass, DE_BUILTINFUNC_STRINGRESIZE,
      "resize", 1, "length");
  deStringAppendFunc = addMethod(deStringTclass, DE_BUILTINFUNC_STRINGAPPEND,
      "append", 1, "element");
  deStringConcatFunc = addMethod(deStringTclass, DE_BUILTINFUNC_STRINGCONCAT, "concat", 1, "array");
  deStringToUintLEFunc = addMethod(deStringTclass, DE_BUILTINFUNC_STRINGTOUINTLE,
     "toUintLE", 1, "width");
  deStringToUintBEFunc = addMethod(deStringTclass, DE_BUILTINFUNC_STRINGTOUINTBE,
     "toUintBE", 1, "width");
  deStringToHexFunc = addMethod(deStringTclass, DE_BUILTINFUNC_STRINGTOHEX, "toHex", 0);
  deHexToStringFunc = addMethod(deStringTclass, DE_BUILTINFUNC_HEXTOSTRING, "fromHex", 0);
  deFindFunc = addMethod(deStringTclass, DE_BUILTINFUNC_FIND, "find", 2, "subString", "offset");
  setParameterDefault(deFindFunc, 2, deIntegerExpressionCreate(deNativeUintBigintCreate(0), 0));
  deRfindFunc = addMethod(deStringTclass, DE_BUILTINFUNC_RFIND, "rfind", 2, "subString", "offset");
  setParameterDefault(deRfindFunc, 2, deIntegerExpressionCreate(deNativeUintBigintCreate(0), 0));
  deStringReverseFunc = addMethod(deStringTclass, DE_BUILTINFUNC_STRINGREVERSE, "reverse", 0);
  deUintTclass = createBuiltinTclass("Uint", DE_BUILTINTCLASS_UINT, 1, "value");
  deUintToStringLEFunc = addMethod(deUintTclass, DE_BUILTINFUNC_UINTTOSTRINGLE, "toStringLE", 0);
  deUintToStringBEFunc = addMethod(deUintTclass, DE_BUILTINFUNC_UINTTOSTRINGBE, "toStringBE", 0);
  deUintToStringFunc = addMethod(deUintTclass, DE_BUILTINFUNC_UINTTOSTRING, "toString", 1, "base");
  setParameterDefault(deUintToStringFunc, 1, deIntegerExpressionCreate(deNativeUintBigintCreate(10), 0));
  deIntTclass = createBuiltinTclass("Int", DE_BUILTINTCLASS_INT, 1, "value");
  deIntToStringFunc = addMethod(deIntTclass, DE_BUILTINFUNC_INTTOSTRING, "toString", 1, "base");
  setParameterDefault(deIntToStringFunc, 1, deIntegerExpressionCreate(deNativeUintBigintCreate(10), 0));
  deFloatTclass = createBuiltinTclass("Float", DE_BUILTINTCLASS_FLOAT, 1, "value");
  deModintTclass = createBuiltinTclass("Modint", DE_BUILTINTCLASS_MODINT, 1, "value");
  deTupleTclass = createBuiltinTclass("Tuple", DE_BUILTINTCLASS_TUPLE, 1, "value");
  deTupleToStringFunc = addMethod(deTupleTclass, DE_BUILTINFUNC_TUPLETOSTRING, "toString", 0);
  deStructTclass = createBuiltinTclass("Struct", DE_BUILTINTCLASS_STRUCT, 1, "value");
  deStructToStringFunc = addMethod(deStructTclass, DE_BUILTINFUNC_STRUCTTOSTRING, "toString", 0);
  deEnumTclass = createBuiltinTclass("Enum", DE_BUILTINTCLASS_ENUM, 1, "value");
  deEnumToStringFunc = addMethod(deEnumTclass, DE_BUILTINFUNC_ENUMTOSTRING, "toString", 0);
  deClassTclass = createBuiltinTclass("Class", DE_BUILTINTCLASS_STRUCT, 0);
  deNoneTclass = createBuiltinTclass("None", DE_BUILTINTCLASS_NONE, 0);
}

// Cleanup after the builtin classes module.
void deBuiltinStop(void) {
}

// Bind builtin methods of arrays.
static deDatatype bindArrayBuiltinMethod(deBlock scopeBlock, deExpression expression,
    deFunction function, deDatatypeArray parameterTypes) {
  deDatatype selfType = deDatatypeArrayGetiDatatype(parameterTypes, 0);
  utAssert(deDatatypeGetType(selfType) == DE_TYPE_ARRAY);
  deDatatype paramType = deDatatypeNull;
  if (deDatatypeArrayGetUsedDatatype(parameterTypes) >= 2) {
    paramType = deDatatypeArrayGetiDatatype(parameterTypes, 1);
  }
  if (function == deArrayLengthFunc) {
    return deUintDatatypeCreate(64);
  } else if (function == deArrayResizeFunc) {
    if (deDatatypeGetType(paramType) != DE_TYPE_UINT) {
      deExprError(expression, "Array.resize method requires a uint length parameter");
    }
    return selfType;  // Resize returns the array.
  } else if (function == deArrayAppendFunc) {
    deExpression accessExpr = deExpressionGetFirstExpression(expression);
    utAssert(deExpressionGetType(accessExpr) == DE_EXPR_DOT);
    deExpression arrayExpr = deExpressionGetFirstExpression(accessExpr);
    deRefineAccessExpressionDatatype(scopeBlock, arrayExpr, deArrayDatatypeCreate(paramType));
    deDatatype elementType = deDatatypeGetElementType(deExpressionGetDatatype(arrayExpr));
    if (paramType != elementType && paramType != deSetDatatypeNullable(elementType, false)) {
      deExprError(expression, "Array.append passed incompatible element: %s",
        deGetOldVsNewDatatypeStrings(elementType, paramType));
    }
    return deNoneDatatypeCreate();
  } else if (function == deArrayConcatFunc) {
    if (paramType != selfType) {
      deExprError(expression, "Array.append passed incompatible element: %s",
        deGetOldVsNewDatatypeStrings(selfType, paramType));
    }
    return deNoneDatatypeCreate();
  } else if (function == deArrayReverseFunc) {
    return deNoneDatatypeCreate();
  } else if (function == deArrayToStringFunc) {
    return deStringDatatypeCreate();
  }
  utExit("Unknown builtin Array method");
  return deDatatypeNull;  // Dummy return;
}

// Bind builtin methods of strings.
static deDatatype bindStringBuiltinMethod(deFunction function,
    deDatatypeArray parameterTypes, deExpression expression) {
  deDatatype selfType = deDatatypeArrayGetiDatatype(parameterTypes, 0);
  utAssert(deDatatypeGetType(selfType) == DE_TYPE_STRING);
  deDatatype paramType = deDatatypeNull;
  deDatatype param2Type = deDatatypeNull;
  if (deDatatypeArrayGetUsedDatatype(parameterTypes) >= 2) {
    paramType = deDatatypeArrayGetiDatatype(parameterTypes, 1);
    if (deDatatypeArrayGetUsedDatatype(parameterTypes) >= 3) {
      param2Type = deDatatypeArrayGetiDatatype(parameterTypes, 2);
    }
  }
  deExpression accessExpr = deExpressionGetFirstExpression(expression);
  deExpression paramsExpr = deExpressionGetNextExpression(accessExpr);
  if (function == deStringLengthFunc) {
    return deUintDatatypeCreate(64);
  } else if (function == deStringResizeFunc) {
    if (deDatatypeGetType(paramType) != DE_TYPE_UINT) {
      deExprError(expression, "String.resize method requires a uint length parameter");
    }
    return selfType; // Returns the string.
  } else if (function == deStringAppendFunc) {
    if (paramType != deDatatypeGetElementType(selfType)) {
      deExprError(expression, "String.append passed incompatible element: %s",
        deGetOldVsNewDatatypeStrings(deDatatypeGetElementType(selfType), paramType));
    }
    return deNoneDatatypeCreate();
  } else if (function == deStringConcatFunc) {
    if (paramType != selfType) {
      deExprError(expression, "String.concat passed incompatible element: %s",
        deGetOldVsNewDatatypeStrings(selfType, paramType));
    }
    return deNoneDatatypeCreate();
  } else if (function == deStringReverseFunc) {
    return deNoneDatatypeCreate();
  } else if (function == deStringToUintLEFunc) {
    deExpression uintTypeExpr = deExpressionGetFirstExpression(paramsExpr);
    if (!deExpressionIsType(uintTypeExpr) ||
        deDatatypeGetType(deExpressionGetDatatype(uintTypeExpr)) != DE_TYPE_UINT) {
      deExprError(expression,
          "String.toUintLE expectes an unsigned integer type expression, eg u256");
    }
    bool secret = deDatatypeSecret(selfType) || deDatatypeSecret(paramType);
    return deSetDatatypeSecret(paramType, secret);
  } else if (function == deStringToUintBEFunc) {
    deExpression uintTypeExpr = deExpressionGetFirstExpression(paramsExpr);
    if (!deExpressionIsType(uintTypeExpr) ||
        deDatatypeGetType(deExpressionGetDatatype(uintTypeExpr)) != DE_TYPE_UINT) {
      deExprError(expression,
          "String.toUintBE expectes an unsigned integer type expression, eg u256");
    }
    bool secret = deDatatypeSecret(selfType) || deDatatypeSecret(paramType);
    return deSetDatatypeSecret(paramType, secret);
  } else if (function == deStringToHexFunc || function == deHexToStringFunc) {
    return selfType;
  } else if (function == deFindFunc || function == deRfindFunc) {
    if (deDatatypeSecret(selfType) || deDatatypeSecret(paramType)) {
      deExprError(expression, "Cannot search for substrings in secret strings");
    }
    if (param2Type != deDatatypeNull && deDatatypeSecret(param2Type)) {
      deExprError(expression, "Cannot use secret offset in find or rfind");
    }
    return deUintDatatypeCreate(64);
  }
  utExit("Unknown builtin String method");
  return deDatatypeNull;  // Dummy return;
}

// Bind builtin methods of uints.
static deDatatype bindUintBuiltinMethod(deFunction function,
    deDatatypeArray parameterTypes, deExpression expression) {
  deDatatype selfType = deDatatypeArrayGetiDatatype(parameterTypes, 0);
  if (function == deUintToStringLEFunc || function == deUintToStringBEFunc) {
    // This conversion is constant time.
    bool secret = deDatatypeSecret(selfType);
    return deSetDatatypeSecret(deStringDatatypeCreate(), secret);
  } else if (function == deUintToStringFunc) {
    // This conversion is not constant time.
    if (deDatatypeSecret(selfType)) {
      deExprError(expression,
          "Uint.toString() cannot convert secrets to strings.  Try Uint.toStringLE()");
    }
    deDatatype paramType = deDatatypeArrayGetiDatatype(parameterTypes, 1);
    if (paramType != deDatatypeNull && deDatatypeGetType(paramType) != DE_TYPE_UINT) {
      deExprError(expression, "Int.toString(base) requires a Uint base parameter");
    }
    return deStringDatatypeCreate();
  }
  utExit("Unknown builtin Uint method");
  return deDatatypeNull;  // Dummy return;
}

// Bind builtin methods of ints.
static deDatatype bindIntBuiltinMethod(deFunction function,
    deDatatypeArray parameterTypes, deExpression expression) {
  deDatatype selfType = deDatatypeArrayGetiDatatype(parameterTypes, 0);
  if (function == deIntToStringFunc) {
    // This conversion is not constant time.
    if (deDatatypeSecret(selfType)) {
      deExprError(expression,
          "Int.toString() cannot convert secrets to strings.  Try Uint.toStringLE()");
    }
    deDatatype paramType = deDatatypeArrayGetiDatatype(parameterTypes, 1);
    if (paramType != deDatatypeNull && deDatatypeGetType(paramType) != DE_TYPE_UINT) {
      deExprError(expression, "Int.toString(base) requires a Uint base parameter");
    }
    return deStringDatatypeCreate();
  }
  utExit("Unknown builtin Int method");
  return deDatatypeNull;  // Dummy return;
}

// Bind builtin methods of Bools.
static deDatatype bindBoolBuiltinMethod(deFunction function,
    deDatatypeArray parameterTypes, deExpression expression) {
  deDatatype selfType = deDatatypeArrayGetiDatatype(parameterTypes, 0);
  if (function == deBoolToStringFunc) {
    // This conversion is not constant time.
    if (deDatatypeSecret(selfType)) {
      deExprError(expression, "Bool.toString() cannot convert secrets to strings");
    }
    return deStringDatatypeCreate();
  }
  utExit("Unknown builtin Bool method");
  return deDatatypeNull;  // Dummy return;
}

// Bind builtin methods of Tuple.
static deDatatype bindJustToStringBuiltinMethod(deFunction function,
    deDatatypeArray parameterTypes, deExpression expression) {
  deDatatype selfType = deDatatypeArrayGetiDatatype(parameterTypes, 0);
  char *typeName = deDatatypeTypeGetName(deDatatypeGetType(selfType));
  if (function == deTupleToStringFunc) {
    // Printing is not constant time.
    if (deDatatypeSecret(selfType)) {
      deExprError(expression, "%s.toString() cannot convert secrets to strings", typeName);
    }
    return deStringDatatypeCreate();
  }
  utExit("Unknown builtin Tuple method");
  return deDatatypeNull;  // Dummy return;
}

// Bind builtin method calls on builtin types.
deDatatype deBindBuiltinCall(deBlock scopeBlock, deFunction function,
    deDatatypeArray parameterTypes, deExpression expression) {
  // For now, only arrays and strings have builtin methods.
  deDatatype selfType = deDatatypeArrayGetiDatatype(parameterTypes, 0);
  deDatatypeType type = deDatatypeGetType(selfType);

  // Validate data before we return
  deExpression dotExpr = deExpressionGetFirstExpression(expression);
  utAssert(deExpressionGetType(dotExpr) == DE_EXPR_DOT);
  if (deExpressionIsType(deExpressionGetFirstExpression(dotExpr))) {
    deExprError(expression, "Expected an instance of a type, but got type instead");
  }

  if (type == DE_TYPE_ARRAY) {
    return bindArrayBuiltinMethod(scopeBlock, expression, function, parameterTypes);
  } else if (type == DE_TYPE_TUPLE || type == DE_TYPE_STRUCT || type == DE_TYPE_ENUM) {
    return bindJustToStringBuiltinMethod(function, parameterTypes, expression);
  } else if (type == DE_TYPE_STRING) {
    return bindStringBuiltinMethod(function, parameterTypes, expression);
  } else if (type == DE_TYPE_UINT) {
    return bindUintBuiltinMethod(function, parameterTypes, expression);
  } else if (type == DE_TYPE_INT) {
    return bindIntBuiltinMethod(function, parameterTypes, expression);
  } else if (type == DE_TYPE_BOOL) {
    return bindBoolBuiltinMethod(function, parameterTypes, expression);
  } else {
    utExit("Unknown builtin method call");
  }
  return deDatatypeNull;  // Dummy return.
}
