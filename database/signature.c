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

// Function signatures returned by create functions are always unique, so that
// the references can be directly compared to determine if two signatures are
// the same.

#include "de.h"

// Dump the paramspec to stdout for debugging.
void deDumpParamspecStr(deString string, deParamspec paramspec) {
  deVariable variable = deParamspecGetVariable(paramspec);
  deDatatype datatype = deParamspecGetDatatype(paramspec);
  deStringSprintf(string, "%s: %s", deVariableGetName(variable), deDatatypeGetTypeString(datatype));
}

// Dump the paramspec to stdout for debugging.
void deDumpParamspec(deParamspec paramspec) {
  deString string = deMutableStringCreate();
  deDumpParamspecStr(string, paramspec);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Dump the signature to stdout for debugging.
void deDumpSignatureStr(deString string, deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  deStringSprintf(string, "%s %s (", deGetFunctionTypeName(deFunctionGetType(function)),
      deFunctionGetName(function));
  bool firstTime = true;
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    if (!firstTime) {
      deStringPuts(string, ", ");
    }
    firstTime = false;
    deDumpParamspecStr(string, paramspec);
  } deEndSignatureParamspec;
  deStringPuts(string, ")");
  deDatatype returnType = deSignatureGetReturnType(signature);
  if (returnType != deDatatypeNull && deDatatypeGetType(returnType) != DE_TYPE_NONE) {
    deStringSprintf(string, " -> %s", deDatatypeGetTypeString(returnType));
  }
}

// Dump the signature to stdout for debugging.
void deDumpSignature(deSignature signature) {
  deString string = deMutableStringCreate();
  deDumpSignatureStr(string, signature);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Compute a 32-bit hash of the signature.
static uint32 hashSignature(deFunction function, deDatatypeArray parameterTypes) {
  uint32 hash = deFunction2Index(function);
  deDatatype datatype;
  deForeachDatatypeArrayDatatype(parameterTypes, datatype) {
    hash = utHashValues(hash, deDatatype2Index(datatype));
  } deEndDatatypeArrayDatatype;
  return hash;
}

// Add the signature to the hash table.
static void addToHashTable(deSignature signature, deDatatypeArray parameterTypes) {
  uint32 hash = hashSignature(deSignatureGetFunction(signature), parameterTypes);
  deSignatureBin bin = deRootFindSignatureBin(deTheRoot, hash);
  if (bin == deSignatureBinNull) {
    bin = deSignatureBinAlloc();
    deSignatureBinSetHash(bin, hash);
    deRootInsertSignatureBin(deTheRoot, bin);
  }
  deSignatureBinInsertSignature(bin, signature);
}

// Determine if the signature is for the same function or class call with the
// same types.
static bool signatureMatches(deSignature signature, deFunction function, deDatatypeArray parameterTypes) {
  if (deSignatureGetFunction(signature) != function) {
    return false;
  }
  uint32 numTypes = deDatatypeArrayGetUsedDatatype(parameterTypes);
  if (deSignatureGetNumParamspec(signature) != numTypes) {
    return false;
  }
  for (uint32 i = 0; i < numTypes; i++) {
    deDatatype oldType = deSignatureGetiType(signature, i);
    deDatatype newType = deDatatypeArrayGetiDatatype(parameterTypes, i);
    if (oldType != newType) {
      return false;
    }
  }
  return true;
}

// Lookup a function signature from the function and array of datatypes.
deSignature deLookupSignature(deFunction function, deDatatypeArray parameterTypes) {
  uint32 hash = hashSignature(function, parameterTypes);
  deSignatureBin bin = deRootFindSignatureBin(deTheRoot, hash);
  if (bin == deSignatureBinNull) {
    return deSignatureNull;
  }
  deSignature signature;
  deForeachSignatureBinSignature(bin, signature) {
    if (signatureMatches(signature, function, parameterTypes)) {
      return signature;
    }
  } deEndSignatureBinSignature;
  return deSignatureNull;
}

// Create a new parameter specification object on the signature.
static deParamspec deParamspecCreate(deSignature signature, deDatatype datatype) {
  deParamspec paramspec = deParamspecAlloc();
  deParamspecSetDatatype(paramspec, datatype);
  deSignatureAppendParamspec(signature, paramspec);
  return paramspec;
}

// Set the variable in each paramspec to point to the corresponding function
// parameter variable.
static void assignParamspecVariables(deSignature signature) {
  uint32 xParam = 0;
  deBlock block = deFunctionGetSubBlock(deSignatureGetFunction(signature));
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    if (deVariableGetType(variable) != DE_VAR_PARAMETER) {
      utAssert(xParam == deSignatureGetNumParamspec(signature));
      return;
    }
    deParamspec paramspec = deSignatureGetiParamspec(signature, xParam);
    deParamspecSetVariable(paramspec, variable);
    xParam++;
  } deEndBlockVariable;
  utAssert(xParam == deSignatureGetUsedParamspec(signature));
}

// Create either a class or function signature.
deSignature deSignatureCreate(deFunction function,
    deDatatypeArray parameterTypes, deLine line) {
  deSignature signature = deSignatureAlloc();
  deSignatureSetLine(signature, line);
  deFunctionAppendSignature(function, signature);
  deSignatureSetNumber(signature, deFunctionGetNumSignatures(function));
  deFunctionSetNumSignatures(function, deFunctionGetNumSignatures(function) + 1);
  uint32 numParameters = deDatatypeArrayGetUsedDatatype(parameterTypes);
  deSignatureResizeParamspecs(signature, numParameters);
  for (uint32 xParam = 0; xParam < numParameters; xParam++) {
    deDatatype datatype = deDatatypeArrayGetiDatatype(parameterTypes, xParam);
    deParamspecCreate(signature, datatype);
  }
  deSignatureSetUsedParamspec(signature, numParameters);
  assignParamspecVariables(signature);
  addToHashTable(signature, parameterTypes);
  if (deCurrentSignature != deSignatureNull) {
    deSignatureAppendCallSignature(deCurrentSignature, signature);
  }
  if (deCurrentStatement != deStatementNull) {
    deStatementAppendCallSignature(deCurrentStatement, signature);
  }
  deRootAppendSignature(deTheRoot, signature);
  return signature;
}

// Determine if the signature is a method.
bool deSignatureIsMethod(deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  if (function == deFunctionNull) {
    return false;
  }
  deBlock block = deBlockGetScopeBlock(deFunctionGetBlock(function));
  return block != deRootGetBlock(deTheRoot);
}

// Determine if the signature is a constructor.
bool deSignatureIsConstructor(deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  if (function == deFunctionNull) {
    return false;
  }
  return deFunctionGetType(function) == DE_FUNC_CONSTRUCTOR;
}

 // Change any parameter types other than the self variable from TCLASS to the
 // return type of the signature.  Return true if we change the signature.
static bool resolveSignatureParamTypes(deSignature signature) {
  deDatatype classType = deSignatureGetReturnType(signature);
  deTclass tclass = deClassGetTclass(deDatatypeGetClass(classType));
  deDatatype nullType = deNullDatatypeCreate(tclass);
  deParamspec selfParam = deSignatureGetiParamspec(signature, 0);
  bool changedType = false;
  if (deParamspecGetDatatype(selfParam) != classType) {
    deParamspecSetDatatype(selfParam, classType);
    changedType = true;
  }
  bool firstTime = true;
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    // Skip the self variable, which we leave as the tclass type.
    if (!firstTime && deParamspecGetDatatype(paramspec) == nullType) {
      deParamspecSetDatatype(paramspec, classType);
      changedType = true;
    }
    firstTime = false;
  } deEndSignatureParamspec;
  return changedType;
}

// Create a datatype array of the datatypes in the signature.
deDatatypeArray getSignatureParameterTypes(deSignature signature) {
  deDatatypeArray parameterTypes = deDatatypeArrayAlloc();
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    deDatatypeArrayAppendDatatype(parameterTypes, deParamspecGetDatatype(paramspec));
  } deEndSignatureParamspec;
  return parameterTypes;
}


// Once a constructor signature is given its return type, all the parameters that
// have the tclass type can be resolved to the class type.  This may mean that
// we find an old signature that matches, in which case, destroy the signature and
// return the old one.
deSignature deResolveConstructorSignature(deSignature signature) {
  if (!resolveSignatureParamTypes(signature)) {
    return signature;
  }
  // We modified at least one parameter type.  Remove it from the hash table,
  // and lookup an existing one.
  deSignatureBinRemoveSignature(deSignatureGetSignatureBin(signature), signature);
  deDatatypeArray parameterTypes = getSignatureParameterTypes(signature);
  deFunction constructor = deSignatureGetFunction(signature);
  deSignature oldSignature = deLookupSignature(constructor, parameterTypes);
  if (oldSignature != deSignatureNull) {
    deSignatureDestroy(signature);
    signature = oldSignature;
  } else {
    addToHashTable(signature, parameterTypes);
  }
  deDatatypeArrayFree(parameterTypes);
  return signature;
}

// Bind a type expression and return its concrete type.  If it does not fully
// specify a type, report an error.
static deDatatype findTypeExprDatatype(deBlock scopeBlock, deExpression typeExpr) {
  deBindExpression(scopeBlock, typeExpr);
  deDatatype datatype = deExpressionGetDatatype(typeExpr);
  deLine line = deExpressionGetLine(typeExpr);
  if (datatype == deDatatypeNull) {
    deError(deExpressionGetLine(typeExpr), "Expected fully qualified type");
  }
  datatype = deFindUniqueConcreteDatatype(datatype, line);
  if (datatype == deDatatypeNull) {
    deError(deExpressionGetLine(typeExpr), "Expected fully qualified type");
  }
  return datatype;
}

// Find the concrete datatype for the datatype, and report an error if it is
// still not concrete.
static deDatatype findConcreteDatatype(deDatatype datatype, deLine line) {
    if (!deDatatypeConcrete(datatype)) {
      datatype = deFindUniqueConcreteDatatype(datatype, line);
    }
    if (datatype == deDatatypeNull || !deDatatypeConcrete(datatype)) {
      deError(line, "Expected fully specified type", line);
    }
    return datatype;
}

// Return fully specified parameter types, which must be concrete type
// constraints.  The caller must free the returned datatype array.
deDatatypeArray deFindFullySpecifiedParameters(deBlock block) {
  deFunction function = deBlockGetOwningFunction(block);
  deDatatypeArray datatypes = deDatatypeArrayAlloc();
  deVariable var;
  deForeachBlockVariable(block, var) {
    if (deVariableGetType(var) != DE_VAR_PARAMETER) {
      return datatypes;
    }
    deDatatype datatype = deDatatypeNull;
    deExpression initializer = deVariableGetInitializerExpression(var);
    deExpression typeExpr = deVariableGetTypeExpression(var);
    if (initializer != deExpressionNull && !deFunctionExtern(function)) {
      // External functions must have concrete type constraints.
      deBindExpression(block, initializer);
      datatype = deExpressionGetDatatype(initializer);
    } else if (typeExpr != deExpressionNull) {
      datatype = findTypeExprDatatype(block, typeExpr);
    }
    deLine line = deVariableGetLine(var);
    datatype = findConcreteDatatype(datatype, line);
    deDatatypeArrayAppendDatatype(datatypes, datatype);
  } deEndBlockVariable;
  return datatypes;
}

// Create a signature for an exported function, which must be fully specified.
deSignature deCreateFullySpecifiedSignature(deFunction function) {
  deBlock subBlock = deFunctionGetSubBlock(function);
  deDatatypeArray parameterTypes = deFindFullySpecifiedParameters(subBlock);
  deLine line = deFunctionGetLine(function);
  deSignature signature = deLookupSignature(function, parameterTypes);
  if (signature == deSignatureNull) {
    signature = deSignatureCreate(function, parameterTypes, line);
  }
  deExpression typeExpr = deFunctionGetTypeExpression(function);
  if (typeExpr == deExpressionNull) {
    deSignatureSetReturnType(signature, deNoneDatatypeCreate());
  } else {
    deDatatype datatype = findTypeExprDatatype(deFunctionGetBlock(function), typeExpr);
    datatype = findConcreteDatatype(datatype, line);
    deSignatureSetReturnType(signature, datatype);
  }
  // Set all parameters instantiated.
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    deParamspecSetInstantiated(paramspec, true);
  } deEndSignatureParamspec;
  return signature;
}
