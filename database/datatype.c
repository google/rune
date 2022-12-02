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

// Datatypes returned by create functions are always unique, so that the
// references can be directly compared to determine if two data types are the
// same.

#include "de.h"

// Pre-built data types to help avoid thrashing the hash table too much.
static deDatatype deNoneDatatype, deBoolDatatype, deStringDatatype,
    deUint8Datatype, deUint16Datatype, deUint32Datatype, deUint64Datatype,
    deInt8Datatype, deInt16Datatype, deInt32Datatype, deInt64Datatype,
    deFloat32Datatype, deFloat64Datatype;

// Dump a datatype to stdout for debugging.
void deDumpDatatype(deDatatype datatype) {
  char *string = deDatatypeGetTypeString(datatype);
  printf("%s\n", string);
  fflush(stdout);
}

// Dump a datatype to the end of |string| for debugging.
void deDumpDatatypeStr(deString string, deDatatype datatype) {
  char *text = deDatatypeGetTypeString(datatype);
  deStringPuts(string, text);
}

// Return the name of the type of data.
char *deDatatypeTypeGetName(deDatatypeType type) {
  switch (type) {
  case DE_TYPE_NONE:
    return "none";
  case DE_TYPE_NULL:
    return "null";
  case DE_TYPE_BOOL:
    return "bool";
  case DE_TYPE_STRING:
    return "string";
  case DE_TYPE_UINT:
    return "uint";
  case DE_TYPE_INT:
    return "int";
  case DE_TYPE_MODINT:
    return "modint";
  case DE_TYPE_FLOAT:
    return "float";
  case DE_TYPE_ARRAY:
    return "array";
  case DE_TYPE_TCLASS:
    return "tclass";
  case DE_TYPE_CLASS:
    return "class";
  case DE_TYPE_FUNCTION:
    return "function";
  case DE_TYPE_FUNCPTR:
    return "funcptr";
  case DE_TYPE_TUPLE:
    return "tuple";
  case DE_TYPE_STRUCT:
    return "struct";
  case DE_TYPE_ENUMCLASS:
    return "enumclass";
  case DE_TYPE_ENUM:
    return "enum";
  }
  utExit("Unknown data type");
  return NULL;  // Dummy return.
}

// Every datatype has a class.  For unsigned integers, it is the builtin Uint
// class, etc.  For classes, the type is itself, which is also true for class
// versions.
deTclass deFindDatatypeTclass(deDatatype datatype) {
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_ARRAY:
      return deArrayTclass;
    case DE_TYPE_FUNCPTR:
      return deFuncptrTclass;
    case DE_TYPE_TCLASS:
    case DE_TYPE_NULL:
      return deDatatypeGetTclass(datatype);
    case DE_TYPE_CLASS:
      return deClassGetTclass(deDatatypeGetClass(datatype));
    case DE_TYPE_FUNCTION:
      return deFunctionTclass;
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
    case DE_TYPE_TUPLE:
      return deTupleTclass;
    case DE_TYPE_STRUCT:
      return deStructTclass;
    case DE_TYPE_ENUM:
    case DE_TYPE_ENUMCLASS:
      return deEnumTclass;
    case DE_TYPE_NONE:
      break;
  }
  return deTclassNull;
}

// Create a datatype of type DE_TYPE_NONE.  Making it unique comes later.
static inline deDatatype datatypeCreate(deDatatypeType type, uint32 width, bool concrete) {
  deDatatype datatype = deDatatypeAlloc();
  deDatatypeSetType(datatype, type);
  deDatatypeSetWidth(datatype, width);
  if (type == DE_TYPE_ARRAY || type == DE_TYPE_STRING ||
      (deDatatypeTypeIsInteger(type) && width > 64)) {
    deDatatypeSetContainsArray(datatype, true);
  }
  deDatatypeSetConcrete(datatype, concrete);
  return datatype;
}

// Make a copy of the datatype.
deDatatype copyDatatype(deDatatype datatype) {
  deDatatype copy = datatypeCreate(deDatatypeGetType(datatype),
      deDatatypeGetWidth(datatype), deDatatypeConcrete(datatype));
  deDatatypeSetSecret(copy, deDatatypeSecret(datatype));
  deDatatypeSetNullable(copy, deDatatypeNullable(datatype));
  deDatatypeSetContainsArray(copy, deDatatypeContainsArray(datatype));
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_ARRAY: case DE_TYPE_STRING:
      deDatatypeSetElementType(copy, deDatatypeGetElementType(datatype));
      break;
    case DE_TYPE_FUNCPTR:
      deDatatypeSetReturnType(copy, deDatatypeGetReturnType(datatype));
      break;
    case DE_TYPE_TCLASS: case DE_TYPE_NULL:
      deDatatypeSetTclass(copy, deDatatypeGetTclass(datatype));
      break;
    case DE_TYPE_CLASS:
      deDatatypeSetClass(copy, deDatatypeGetClass(datatype));
      break;
    case DE_TYPE_FUNCTION:
      deDatatypeSetFunction(copy, deDatatypeGetFunction(datatype));
      break;
    default:
      break;
  }
  uint32 numTypeList = deDatatypeGetNumTypeList(datatype);
  if (numTypeList != 0) {
    deDatatypeResizeTypeLists(copy, numTypeList);
    memcpy(deDatatypeGetTypeLists(copy), deDatatypeGetTypeLists(datatype),
           numTypeList * sizeof(deDatatype));
  }
  return copy;
}

// Hash all the values in the datatype together for use in hash-table lookup.
static uint32 hashDatatype(deDatatype datatype) {
  uint32 hash = deDatatypeGetType(datatype);
  hash = utHashValues(hash, deDatatypeSecret(datatype));
  hash = utHashValues(hash, deDatatypeNullable(datatype));
  hash = utHashValues(hash, deDatatypeGetWidth(datatype));
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_ARRAY:
      hash = utHashValues(hash, deDatatype2Index(deDatatypeGetElementType(datatype)));
      break;
    case DE_TYPE_FUNCPTR:
      hash = utHashValues(hash, deDatatype2Index(deDatatypeGetReturnType(datatype)));
      break;
    case DE_TYPE_TCLASS:
      hash = utHashValues(hash, deTclass2Index(deDatatypeGetTclass(datatype)));
      break;
    case DE_TYPE_CLASS:
      hash = utHashValues(hash, deClass2Index(deDatatypeGetClass(datatype)));
      break;
    case DE_TYPE_FUNCTION:
      hash = utHashValues(hash, deFunction2Index(deDatatypeGetFunction(datatype)));
      break;
    default:
    break;
  }
  deDatatype tupleType;
  deForeachDatatypeTypeList(datatype, tupleType) {
    hash = utHashValues(hash, deDatatype2Index(tupleType));
  } deEndDatatypeTypeList;
  return hash;
}

// Compare two datatypes to see if they are the same.
static bool datatypesAreIdentical(deDatatype datatype1, deDatatype datatype2) {
  if (deDatatypeGetType(datatype1) != deDatatypeGetType(datatype2)) {
    return false;
  }
  if (deDatatypeSecret(datatype1) != deDatatypeSecret(datatype2)) {
    return false;
  }
  if (deDatatypeNullable(datatype1) != deDatatypeNullable(datatype2)) {
    return false;
  }
  if (deDatatypeGetWidth(datatype1) != deDatatypeGetWidth(datatype2)) {
    return false;
  }
  switch (deDatatypeGetType(datatype1)) {
    case DE_TYPE_ARRAY:
      if (deDatatypeGetElementType(datatype1) != deDatatypeGetElementType(datatype2)) {
        return false;
      }
      break;
    case DE_TYPE_FUNCPTR:
      if (deDatatypeGetReturnType(datatype1) != deDatatypeGetReturnType(datatype2)) {
        return false;
      }
      break;
    case DE_TYPE_TCLASS:
    case DE_TYPE_NULL:
      if (deDatatypeGetTclass(datatype1) != deDatatypeGetTclass(datatype2)) {
        return false;
      }
      break;
    case DE_TYPE_CLASS:
      if (deDatatypeGetClass(datatype1) != deDatatypeGetClass(datatype2)) {
        return false;
      }
      break;
    case DE_TYPE_FUNCTION:
      if (deDatatypeGetFunction(datatype1) != deDatatypeGetFunction(datatype2)) {
        return false;
      }
      break;
    case DE_TYPE_MODINT:
      if (deDatatypeGetModulus(datatype1) != deDatatypeGetModulus(datatype2)) {
        return false;
      }
      break;
    case DE_TYPE_STRUCT:
    case DE_TYPE_ENUMCLASS:
    case DE_TYPE_ENUM:
      if (deDatatypeGetFunction(datatype1) != deDatatypeGetFunction(datatype2)) {
        return false;
      }
      break;
    case DE_TYPE_NONE:
    case DE_TYPE_BOOL:
    case DE_TYPE_STRING:
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_FLOAT:
    case DE_TYPE_TUPLE:
      break;
  }
  if (deDatatypeGetNumTypeList(datatype1) !=
      deDatatypeGetNumTypeList(datatype2)) {
    return false;
  }
  for (uint32 i = 0; i < deDatatypeGetNumTypeList(datatype1); i++) {
    if (deDatatypeGetiTypeList(datatype1, i) !=
        deDatatypeGetiTypeList(datatype2, i)) {
      return false;
    }
  }
  return true;
}

// Add the data type to the hash table.  If it already exists, destroy the
// passed datatype and return the old one.
static deDatatype addToHashTable(deDatatype datatype) {
  uint32 hash = hashDatatype(datatype);
  deDatatypeBin bin = deRootFindDatatypeBin(deTheRoot, hash);
  if (bin == deDatatypeBinNull) {
    bin = deDatatypeBinAlloc();
    deDatatypeBinSetHash(bin, hash);
    deRootInsertDatatypeBin(deTheRoot, bin);
  } else {
    deDatatype oldDatatype;
    deForeachDatatypeBinDatatype(bin, oldDatatype) {
      if (datatypesAreIdentical(datatype, oldDatatype)) {
        deDatatypeDestroy(datatype);
        return oldDatatype;
      }
    } deEndDatatypeBinDatatype;
  }
  deDatatypeBinInsertDatatype(bin, datatype);
  return datatype;
}

// Initialize common data types to speed things up a bit.
void deDatatypeStart(void) {
  deNoneDatatype = addToHashTable(datatypeCreate(DE_TYPE_NONE, 0, false));
  deBoolDatatype = addToHashTable(datatypeCreate(DE_TYPE_BOOL, 0, true));
  deUint8Datatype = addToHashTable(datatypeCreate(DE_TYPE_UINT, 8, true));
  deUint16Datatype = addToHashTable(datatypeCreate(DE_TYPE_UINT, 16, true));
  deUint32Datatype = addToHashTable(datatypeCreate(DE_TYPE_UINT, 32, true));
  deUint64Datatype = addToHashTable(datatypeCreate(DE_TYPE_UINT, 64, true));
  deInt8Datatype = addToHashTable(datatypeCreate(DE_TYPE_INT, 8, true));
  deInt16Datatype = addToHashTable(datatypeCreate(DE_TYPE_INT, 16, true));
  deInt32Datatype = addToHashTable(datatypeCreate(DE_TYPE_INT, 32, true));
  deInt64Datatype = addToHashTable(datatypeCreate(DE_TYPE_INT, 64, true));
  deFloat32Datatype = addToHashTable(datatypeCreate(DE_TYPE_FLOAT, 32, true));
  deFloat64Datatype = addToHashTable(datatypeCreate(DE_TYPE_FLOAT, 64, true));
  // Be sure to build uint8 type first.
  deDatatype datatype = datatypeCreate(DE_TYPE_STRING, 0, true);
  deDatatypeSetElementType(datatype, deUint8Datatype);
  deStringDatatype = addToHashTable(datatype);
}

// Free memory used by the datatype module.
void deDatatypeStop(void) {
  // Nothing yet.
}

// Return the None datatype, representing no value computed at all.
deDatatype deNoneDatatypeCreate(void) {
  return deNoneDatatype;
}

// Return the NULL datatype, witch can be unified with classes of the same
// tclass.
deDatatype deNullDatatypeCreate(deTclass tclass) {
  deDatatype datatype = datatypeCreate(DE_TYPE_NULL, deTclassGetRefWidth(tclass), false);
  deDatatypeSetNullable(datatype, true);
  deDatatypeSetTclass(datatype, tclass);
  return addToHashTable(datatype);
}

// Return the Boolean data type.
deDatatype deBoolDatatypeCreate(void) {
  return deBoolDatatype;
}

// Return the string datatype.
deDatatype deStringDatatypeCreate(void) {
  return deStringDatatype;
}

// Create a uint datatype of the given width.  If it already exists, return the
// old one.
deDatatype deUintDatatypeCreate(uint32 width) {
  switch (width) {
    case 8: return deUint8Datatype;
    case 16: return deUint16Datatype;
    case 32: return deUint32Datatype;
    case 64: return deUint64Datatype;
    default:
      break;
  }
  if (width == 0) {
    utExit("Attempted to create zero-width integer");
  }
  return addToHashTable(datatypeCreate(DE_TYPE_UINT, width, true));
}

// Create an int datatype of the given width.  If it already exists, return the
// old one.
deDatatype deIntDatatypeCreate(uint32 width) {
  switch (width) {
    case 8: return deInt8Datatype;
    case 16: return deInt16Datatype;
    case 32: return deInt32Datatype;
    case 64: return deInt64Datatype;
    default:
      break;
  }
  if (width == 0) {
    utExit("Attempted to create zero-width integer");
  }
  return addToHashTable(datatypeCreate(DE_TYPE_INT, width, true));
}

// Create a modular integer datatype.  If it already exists, return the old one.
// Modular integer types exist only within a modular integer expression.  When
// assigned to a variable or passed as a parameter, they are converted to
// unsigned integers of the same width as the modulus.
deDatatype deModintDatatypeCreate(deExpression modulus) {
  deDatatype modulusDatatype = deExpressionGetDatatype(modulus);
  utAssert(deDatatypeIsInteger(modulusDatatype));
  deDatatype datatype = datatypeCreate(DE_TYPE_MODINT, deDatatypeGetWidth(modulusDatatype), true);
  deDatatypeSetModulus(datatype, modulus);
  return addToHashTable(datatype);
}

// Return a floating point datatype.
deDatatype deFloatDatatypeCreate(uint32 width) {
  switch (width) {
    case 32: return deFloat32Datatype;
    case 64: return deFloat64Datatype;
  }
  utExit("Tried to create float type of unsupported width %u", width);
  return deDatatypeNull;  // Dummy return.
}

// Create an array datatype.  If it already exists, return the old one.
deDatatype deArrayDatatypeCreate(deDatatype elementType) {
  deDatatype datatype = datatypeCreate(DE_TYPE_ARRAY, 0, deDatatypeConcrete(elementType));
  deDatatypeSetElementType(datatype, elementType);
  return addToHashTable(datatype);
}

// Create a tclass datatype.  If it already exists, return the old one.
deDatatype deTclassDatatypeCreate(deTclass tclass) {
  deDatatype datatype = datatypeCreate(DE_TYPE_TCLASS, deTclassGetRefWidth(tclass), false);
  deDatatypeSetTclass(datatype, tclass);
  return addToHashTable(datatype);
}

// Create a class version datatype.  |width| is the width of an object reference for the class.
deDatatype deClassDatatypeCreate(deClass theClass) {
  deDatatype datatype = datatypeCreate(DE_TYPE_CLASS, deClassGetRefWidth(theClass), true);
  deDatatypeSetClass(datatype, theClass);
  datatype = addToHashTable(datatype);
  return datatype;
}

// Return the function datatype.
deDatatype deFunctionDatatypeCreate(deFunction function) {
  switch (deFunctionGetType(function)) {
    case DE_FUNC_PLAIN:
    case DE_FUNC_UNITTEST:
    case DE_FUNC_FINAL:
    case DE_FUNC_DESTRUCTOR:
    case DE_FUNC_PACKAGE:
    case DE_FUNC_MODULE:
    case DE_FUNC_ITERATOR:
    case DE_FUNC_STRUCT:
    case DE_FUNC_GENERATOR: {
      deDatatype datatype = datatypeCreate(DE_TYPE_FUNCTION, 0, false);
      deDatatypeSetFunction(datatype, function);
      return addToHashTable(datatype);
    }
    case DE_FUNC_ENUM:
      return deEnumClassDatatypeCreate(function);
    case DE_FUNC_CONSTRUCTOR:
      return deTclassDatatypeCreate(deFunctionGetTclass(function));
    case DE_FUNC_OPERATOR:
      utExit("Operators don't have idents");
  }
  return deDatatypeNull;  // Dummy return.
}

// Return the function pointer datatype.  Do not the datatypes array.
deDatatype deFuncptrDatatypeCreate(deDatatype returnType, deDatatypeArray parameterTypes) {
  deDatatype datatype = datatypeCreate(DE_TYPE_FUNCPTR, 0, true);
  deDatatypeSetReturnType(datatype, returnType);
  uint32 numTypes = deDatatypeArrayGetUsedDatatype(parameterTypes);
  deDatatypeResizeTypeLists(datatype, numTypes);
  for (uint32 i = 0; i < numTypes; i++) {
    deDatatypeSetiTypeList(datatype, i, deDatatypeArrayGetiDatatype(parameterTypes, i));
  }
  return addToHashTable(datatype);
}

// Fill out the sub-datatype list on the datatype.  Free |types|.
static void fillDatatypeTypeList(deDatatype datatype, deDatatypeArray types) {
  uint32 numTypes = deDatatypeArrayGetUsedDatatype(types);
  deDatatypeResizeTypeLists(datatype, numTypes);
  for (uint32 i = 0; i < numTypes; i++) {
    deDatatype subType = deDatatypeArrayGetiDatatype(types, i);
    deDatatypeSetiTypeList(datatype, i, subType);
    if (deDatatypeContainsArray(subType)) {
      deDatatypeSetContainsArray(datatype, true);
    }
  }
  deDatatypeArrayFree(types);
}

// Return true if all the datatypes in the array are concrete.
static deDatatype findNonConcreteDatatype(deDatatypeArray types) {
  deDatatype datatype;
  deForeachDatatypeArrayDatatype(types, datatype) {
    if (!deDatatypeConcrete(datatype)) {
      return datatype;
    }
  } deEndDatatypeArrayDatatype;
  return deDatatypeNull;
}

// Create a tuple datatype.  If it already exists, return the old one.
// Free the types array.
deDatatype deTupleDatatypeCreate(deDatatypeArray types) {
  bool concrete = findNonConcreteDatatype(types) == deDatatypeNull;
  deDatatype datatype = datatypeCreate(DE_TYPE_TUPLE, 0, concrete);
  fillDatatypeTypeList(datatype, types);
  return addToHashTable(datatype);
}

// Create a struct datatype.  If it already exists, return the old one.
// Free the types array.
deDatatype deStructDatatypeCreate(deFunction structFunction, deDatatypeArray types, deLine line) {
  deDatatype nonConcreteDatatype = findNonConcreteDatatype(types);
  if (nonConcreteDatatype != deDatatypeNull) {
    deError(line, "Struct  %s has non-concrete datatype %s",
        deFunctionGetName(structFunction), deDatatypeGetTypeString(nonConcreteDatatype));
  }
  deDatatype datatype = datatypeCreate(DE_TYPE_STRUCT, 0, true);
  deDatatypeSetFunction(datatype, structFunction);
  fillDatatypeTypeList(datatype, types);
  return addToHashTable(datatype);
}

// Create a tuple datatype for the struct.
deDatatype deGetStructTupleDatatype(deDatatype structDatatype) {
  utAssert(deDatatypeGetType(structDatatype) == DE_TYPE_STRUCT);
  deDatatype tupleDatatype = copyDatatype(structDatatype);
  deDatatypeSetFunction(tupleDatatype, deFunctionNull);
  deDatatypeSetType(tupleDatatype, DE_TYPE_TUPLE);
  return addToHashTable(tupleDatatype);
}

// Create an enum or enumclass datatype.  If it already exists, return the old one.
static deDatatype enumDatatypeCreate(deDatatypeType type, deFunction enumFunction, bool concrete) {
  deDatatype datatype = datatypeCreate(type, 0, concrete);
  uint32 width = 0;
  deVariable var = deBlockGetFirstVariable(deFunctionGetSubBlock(enumFunction));
  if (var != deVariableNull) {
    width = deDatatypeGetWidth(deVariableGetDatatype(var));
  }
  deDatatypeSetWidth(datatype, width);
  deDatatypeSetFunction(datatype, enumFunction);
  return addToHashTable(datatype);
}

// Create an enumclass datatype.  If it already exists, return the old one.
deDatatype deEnumClassDatatypeCreate(deFunction enumFunction) {
  return enumDatatypeCreate(DE_TYPE_ENUMCLASS, enumFunction, false);
}

// Create an enum datatype.  If it already exists, return the old one.
deDatatype deEnumDatatypeCreate(deFunction enumFunction) {
  return enumDatatypeCreate(DE_TYPE_ENUM, enumFunction, true);
}

// Make the datatype secret.  If it already exists in the secret form, return
// the old one.
deDatatype deSetDatatypeNullable(deDatatype datatype, bool nullable, deLine line) {
  if (deDatatypeNullable(datatype) == nullable) {
    return datatype;
  }
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type != DE_TYPE_CLASS && type != DE_TYPE_TCLASS && type != DE_TYPE_NULL) {
    deError(line, "Cannot set nullable on non-class types.");
  }
  deDatatype nullableDatatype = copyDatatype(datatype);
  if (type == DE_TYPE_NULL && !nullable) {
    deDatatypeSetType(nullableDatatype, DE_TYPE_TCLASS);
  }
  deDatatypeSetNullable(nullableDatatype, nullable);
  return addToHashTable(nullableDatatype);
}

// Make the datatype secret.  If it already exists in the secret form, return
// the old one.
deDatatype deSetDatatypeSecret(deDatatype datatype, bool secret) {
  if (deDatatypeSecret(datatype) == secret) {
    return datatype;
  }
  deDatatype secretDatatype = copyDatatype(datatype);
  deDatatypeSetSecret(secretDatatype, secret);
  return addToHashTable(secretDatatype);
}

// Set the Uint/Int type to signed or unsigned.
deDatatype deDatatypeSetSigned(deDatatype datatype, bool isSigned) {
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type != DE_TYPE_UINT && type != DE_TYPE_INT) {
    utExit("Tried to change sign of non-integer");
  }
  uint32 width = deDatatypeGetWidth(datatype);
  if (isSigned) {
    return deIntDatatypeCreate(width);
  }
  return deUintDatatypeCreate(width);
}

// Change the width of a datatype and return the new type.
deDatatype deDatatypeResize(deDatatype datatype, uint32 width) {
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type == DE_TYPE_INT) {
    return deIntDatatypeCreate(width);
  }
  if (type == DE_TYPE_UINT) {
    return deUintDatatypeCreate(width);
  }
  utExit("Tried to resize a non-integer data type");
  return deDatatypeNull;  // Dummy return.
}

// Return a string of the typelist types, comma separated.
static deString getClassDatatypeParametersTypeString(deDatatype datatype) {
  // buffers.  Fix this.
  deString parameters = deMutableStringCreate();
  bool firstTime = true;
  // Skip the self parameter in the datatype for theClass.
  bool skipOne = true;
  deClass theClass = deDatatypeGetClass(datatype);
  deSignature signature = deClassGetFirstSignature(theClass);
  utAssert(signature != deSignatureNull);
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    deDatatype paramDatatype = deParamspecGetDatatype(paramspec);
    if (!skipOne) {
      char *childString;
      utAssert(deDatatypeGetType(paramDatatype) != DE_TYPE_TCLASS);
      if (paramDatatype == datatype) {
        childString = deGetBlockPath(deClassGetSubBlock(theClass), false);
      } else {
        childString = deDatatypeGetTypeString(paramDatatype);
      }
      if (!firstTime) {
        deStringPuts(parameters, ", ");
      }
      deStringPuts(parameters, childString);
      firstTime = false;
    }
    skipOne = false;
  } deEndSignatureParamspec;
  return parameters;
}

// Return a string of the typelist types, comma separated.
static char *getTupleDatatypeParametersValueString(deDatatype datatype) {
  // TODO: The compiler will crash if we overflow the temp Datadraw string
  // buffers.  Fix this.
  char *parameters = "";
  bool firstTime = true;
  // Skip the self parameter in the datatype for theClass.
  bool skipOne = deDatatypeGetType(datatype) == DE_TYPE_CLASS;
  deDatatype child;
  deForeachDatatypeTypeList(datatype, child) {
    if (!skipOne) {
      if (!firstTime) {
        parameters = utSprintf("%s, %s", parameters, deDatatypeGetDefaultValueString(child));
      } else {
        parameters = deDatatypeGetDefaultValueString(child);
      }
      firstTime = false;
    }
    skipOne = false;
  } deEndDatatypeTypeList;
  return parameters;
}

// Return a string of the typelist types, comma separated.
static char *getTupleDatatypeParametersTypeString(deDatatype datatype) {
  // TODO: The compiler will crash if we overflow the temp Datadraw string
  // buffers.  Fix this.
  char *parameters = "";
  bool firstTime = true;
  // Skip the self parameter in the datatype for theClass.
  bool skipOne = deDatatypeGetType(datatype) == DE_TYPE_CLASS;
  deDatatype child;
  deForeachDatatypeTypeList(datatype, child) {
    if (!skipOne) {
      if (!firstTime) {
        parameters = utSprintf("%s, %s", parameters, deDatatypeGetTypeString(child));
      } else {
        parameters = deDatatypeGetTypeString(child);
      }
      firstTime = false;
    }
    skipOne = false;
  } deEndDatatypeTypeList;
  return parameters;
}

// Generate a default value string for the theClass.
static char *getClassDefaultValue(deDatatype datatype) {
  deClass theClass = deDatatypeGetClass(datatype);
  if (deClassGetFirstSignature(theClass) == deSignatureNull) {
    char *name = deGetBlockPath(deClassGetSubBlock(theClass), false);
    return utSprintf("null(%s)", name);
  }
  deString parameters = getClassDatatypeParametersTypeString(datatype);
  char *name = deGetBlockPath(deClassGetSubBlock(theClass), false);
  char *result = utSprintf("null(%s(%s))", name, deStringGetCstr(parameters));
  deStringFree(parameters);
  return result;
}

// Return a default value string for the function pointer.
static char *getFuncptrDefaultValue(deDatatype datatype) {
  char *parameters =  getTupleDatatypeParametersTypeString(datatype);
  return utSprintf("null(func(%s))", parameters);
}

// Return a default value string for the tuple.
static char *getTupleDefaultValue(deDatatype datatype) {
  char *parameters =  getTupleDatatypeParametersValueString(datatype);
  return utSprintf("(%s)", parameters);
}

// Return a default value string for the structure.
static char *getStructDefaultValue(deDatatype datatype) {
  char *parameters =  getTupleDatatypeParametersValueString(datatype);
  return utSprintf("%s(%s)", deFunctionGetName(deDatatypeGetFunction(datatype)), parameters);
}

// Return a default value string for the structure.
static char *getEnumClassDefaultValue(deDatatype datatype) {
  // Default to the first enumerated value.
  deFunction function = deDatatypeGetFunction(datatype);
  deVariable var = deBlockGetFirstVariable(deFunctionGetSubBlock(function));
  deBlock subBlock = deFunctionGetSubBlock(function);
  char *path = deGetBlockPath(subBlock, false);
  return utSprintf("%s.%s", path, deVariableGetName(var));
}

// Return a Rune formatted value of this exact datatype.
char *deDatatypeGetDefaultValueString(deDatatype datatype) {
  if (deDatatypeSecret(datatype)) {
    return utSprintf("secret(%s)",
        deDatatypeGetDefaultValueString(deSetDatatypeSecret(datatype, false)));
  }
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_BOOL:
      return "false";
    case DE_TYPE_STRING:
      return "\"\"";
    case DE_TYPE_UINT:
      return utSprintf("0u%u", deDatatypeGetWidth(datatype));
    case DE_TYPE_INT:
      return utSprintf("0i%u", deDatatypeGetWidth(datatype));
    case DE_TYPE_MODINT:
      utExit("Tried to get default string for modular integer type");
      return "";
    case DE_TYPE_FLOAT:
      return utSprintf("0.0f%u", deDatatypeGetWidth(datatype));
    case DE_TYPE_ARRAY:
      return utSprintf("[%s]", deDatatypeGetDefaultValueString(deDatatypeGetElementType(datatype)));
    case DE_TYPE_CLASS:
      return getClassDefaultValue(datatype);
    case DE_TYPE_FUNCPTR:
      return getFuncptrDefaultValue(datatype);
    case DE_TYPE_TUPLE:
      return getTupleDefaultValue(datatype);
    case DE_TYPE_STRUCT:
      return getStructDefaultValue(datatype);
    case DE_TYPE_ENUMCLASS:
    case DE_TYPE_ENUM:
      return getEnumClassDefaultValue(datatype);
    case DE_TYPE_NONE:
      return "None";
    case DE_TYPE_NULL: {
      deFunction constructor = deTclassGetFunction(deDatatypeGetTclass(datatype));
      return utSprintf("null(%s)", deGetBlockPath(deFunctionGetSubBlock(constructor), false));
    }
    case DE_TYPE_TCLASS:
      return deTclassGetName(deDatatypeGetTclass(datatype));
    case DE_TYPE_FUNCTION:
      return utSprintf("func %s", deFunctionGetName(deDatatypeGetFunction(datatype)));
  }
  return "";  // Dummy return
}

// Return a string representing the class type.
static char *getClassTypeString(deDatatype datatype) {
  deClass theClass = deDatatypeGetClass(datatype);
  if (deClassGetFirstSignature(theClass) == deSignatureNull) {
    char *name = deGetBlockPath(deClassGetSubBlock(theClass), false);
    return utSprintf("%s", name);
  }
  deString parameters = getClassDatatypeParametersTypeString(datatype);
  char *name = deGetBlockPath(deClassGetSubBlock(theClass), false);
  char *result = utSprintf("%s(%s)", name, deStringGetCstr(parameters));
  deStringFree(parameters);
  return result;
}

// Return a string representing the funciton pointer type.
static char *getFuncptrTypeString(deDatatype datatype) {
  char *parameters =  getTupleDatatypeParametersTypeString(datatype);
  return utSprintf("func(%s)", parameters);
}

// Return the type string for the tuple.
static char *getTupleTypeString(deDatatype datatype) {
  char *parameters =  getTupleDatatypeParametersTypeString(datatype);
  return utSprintf("(%s)", parameters);
}

// Return the type string for the structure.
static char *getStructTypeString(deDatatype datatype) {
  char *parameters =  getTupleDatatypeParametersTypeString(datatype);
  return utSprintf("%s(%s)", deFunctionGetName(deDatatypeGetFunction(datatype)), parameters);
}

// Return the type string for the enumerated type.
static char *getEnumClassTypeString(deDatatype datatype) {
  deFunction function = deDatatypeGetFunction(datatype);
  deBlock subBlock = deFunctionGetSubBlock(function);
  return deGetBlockPath(subBlock, false);
}

// Return a Rune formatted type string corresponding to this datatype.  The
// datatype must be instantiatable, meaning Class, not Tclass.
char *deDatatypeGetTypeString(deDatatype datatype) {
  if (deDatatypeSecret(datatype)) {
    return utSprintf("secret(%s)", deDatatypeGetTypeString(deSetDatatypeSecret(datatype, false)));
  }
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_BOOL:
      return "bool";
    case DE_TYPE_STRING:
      return "string";
    case DE_TYPE_UINT:
      return utSprintf("u%u", deDatatypeGetWidth(datatype));
    case DE_TYPE_INT:
      return utSprintf("i%u", deDatatypeGetWidth(datatype));
    case DE_TYPE_MODINT:
      utExit("Tried to get default string for modular integer type");
      return "";
    case DE_TYPE_FLOAT:
      return utSprintf("f%u", deDatatypeGetWidth(datatype));
    case DE_TYPE_ARRAY:
      return utSprintf("[%s]", deDatatypeGetTypeString(deDatatypeGetElementType(datatype)));
    case DE_TYPE_CLASS:
      return getClassTypeString(datatype);
    case DE_TYPE_FUNCPTR:
      return getFuncptrTypeString(datatype);
    case DE_TYPE_TUPLE:
      return getTupleTypeString(datatype);
    case DE_TYPE_STRUCT:
      return getStructTypeString(datatype);
    case DE_TYPE_ENUMCLASS:
    case DE_TYPE_ENUM:
      return getEnumClassTypeString(datatype);
    case DE_TYPE_TCLASS:
    case DE_TYPE_NULL: {
      deTclass tclass = deDatatypeGetTclass(datatype);
      deFunction function = deTclassGetFunction(tclass);
      deBlock block = deFunctionGetSubBlock(function);
      char *name = deGetBlockPath(block, false);
      return utSprintf("null(%s)", name);
    }
    case DE_TYPE_FUNCTION:
      return utSprintf("func %s", deFunctionGetName(deDatatypeGetFunction(datatype)));
    case DE_TYPE_NONE:
      return "None";
  }
  return "";  // Dummy return
}

// Match a ... type constraint, e.g. u1 ... u32.
static bool matchDotDotDotTypeConstraint(deBlock scopeBlock,
    deDatatype datatype, deExpression typeExpression) {
  // Rages only make sense for u<lower> ... u<upper>, and i<lower>...i<upper>.
  deExpression left = deExpressionGetFirstExpression(typeExpression);
  deExpression right = deExpressionGetNextExpression(left);
  deExpressionType leftType = deExpressionGetType(left);
  if (leftType == DE_EXPR_UINTTYPE) {
    if (deDatatypeGetType(datatype) != DE_TYPE_UINT) {
      return false;
    }
  } else {
    if (deDatatypeGetType(datatype) != DE_TYPE_INT) {
      return false;
    }
  }
  uint32 leftWidth = deExpressionGetWidth(left);
  uint32 rightWidth = deExpressionGetWidth(right);
  uint32 width = deDatatypeGetWidth(datatype);
  return leftWidth <= width && width <= rightWidth;
}

// Match the datatype against the identifier expression.
static bool datatypeMatchesIdentExpression(deBlock scopeBlock, deDatatype datatype,
    deExpression typeExpression) {
  deLine line = deExpressionGetLine(typeExpression);
  utSym name = deExpressionGetName(typeExpression);
  deIdent ident = deFindIdent(scopeBlock, name);
  if (ident == deIdentNull) {
    deError(line, "Undefined type %s", utSymGetName(name));
  }
  if (deIdentGetType(ident) == DE_IDENT_FUNCTION) {
    deFunction function = deIdentGetFunction(ident);
    deFunctionType type = deFunctionGetType(function);
    if (type == DE_FUNC_CONSTRUCTOR) {
      // It is a class.  Don't match nullable types.
      if (deDatatypeNullable(datatype)) {
        return false;
      }
      deTclass tclass = deFunctionGetTclass(deIdentGetFunction(ident));
      if (tclass == deClassTclass) {
        // The point of "Class" is matching all class objects.
        return true;
      }
      return deFindDatatypeTclass(datatype) == tclass;
    } else if (type == DE_FUNC_STRUCT) {
      if (deDatatypeGetType(datatype) != DE_TYPE_STRUCT) {
        return false;
      }
      return deDatatypeGetFunction(datatype) == function;
    } else if (type == DE_FUNC_ENUM) {
      return function == deDatatypeGetFunction(datatype);
    }
    deError(line, "%s is a function, not a type", utSymGetName(name));
  }
  if (deIdentGetType(ident) == DE_IDENT_VARIABLE) {
    deVariable var = deIdentGetVariable(ident);
    if (!deVariableIsType(var)) {
      deError(line, "Variable %s is not a type", utSymGetName(name));
    }
    if (deVariableGetDatatype(var) != datatype) {
      char * typeName = deDatatypeGetTypeString(datatype);
      deError(line, "Type constraint violation for constraint %s: %s vs %s",
          utSymGetName(name), typeName, deDatatypeGetTypeString(deVariableGetDatatype(var)));
    }
    return true;
  }
  deError(line, "%s is not a type", utSymGetName(name));
  return false;  // Dummy return.
}

// Determine if the datatype matches the type expression.
bool deDatatypeMatchesTypeExpression(deBlock scopeBlock, deDatatype datatype,
    deExpression typeExpression) {
  deLine line = deExpressionGetLine(typeExpression);
  bool secret = deDatatypeSecret(datatype);
  switch (deExpressionGetType(typeExpression)) {
    case DE_EXPR_IDENT:
      return datatypeMatchesIdentExpression(scopeBlock, datatype, typeExpression);
    case DE_EXPR_BITOR: {
      deExpression child;
      deForeachExpressionExpression(typeExpression, child) {
        if (deDatatypeMatchesTypeExpression(scopeBlock, datatype, child)) {
          return true;
        }
      } deEndExpressionExpression;
      return false;
    }
    case DE_EXPR_ARRAY:
      if (deDatatypeGetType(datatype) != DE_TYPE_ARRAY) {
        return false;
      }
      return deDatatypeMatchesTypeExpression(
          scopeBlock, deDatatypeGetElementType(datatype),
          deExpressionGetFirstExpression(typeExpression));
    case DE_EXPR_TUPLE: {
      if (deDatatypeGetType(datatype) != DE_TYPE_TUPLE) {
        return false;
      }
      deExpression childExpression;
      uint32 i = 0;
      deForeachExpressionExpression(typeExpression, childExpression) {
        if (i == deDatatypeGetNumTypeList(datatype) ||
            !deDatatypeMatchesTypeExpression(
                scopeBlock, deDatatypeGetiTypeList(datatype, i),
                childExpression)) {
          return false;
        }
        i++;
      } deEndExpressionExpression;
      return i == deDatatypeGetNumTypeList(datatype);
    }
    case DE_EXPR_SECRET: {
      if (!deDatatypeSecret(datatype)) {
        return false;
      }
      deExpression child = deExpressionGetFirstExpression(typeExpression);
      return deDatatypeMatchesTypeExpression(
          scopeBlock, deSetDatatypeSecret(datatype, false), child);
    }
    case DE_EXPR_REVEAL:
      deError(line, "Reveal is not allowed in type constraints");
      return false;  // Dummy return.
    case DE_EXPR_NULL: {
      deExpression child = deExpressionGetFirstExpression(typeExpression);
      return deDatatypeMatchesTypeExpression(
          scopeBlock, deSetDatatypeNullable(datatype, false, line), child);
    }
    case DE_EXPR_DOT:
    case DE_EXPR_TYPEOF: {
      // We need to bind he typeof() expression.
      deBindExpression(scopeBlock, typeExpression);
      deDatatype constraintType = deExpressionGetDatatype(typeExpression);
      if (datatype == constraintType) {
        return true;
      }
      if (deDatatypeGetType(constraintType) != DE_TYPE_TCLASS) {
        deError(line, "Invalid constraint type %s", deDatatypeGetTypeString(constraintType));
      }
      deTclass tclass = deDatatypeGetTclass(constraintType);
      return deFindDatatypeTclass(datatype) == tclass;
    }
    case DE_EXPR_UINTTYPE:
      return datatype == deSetDatatypeSecret(deUintDatatypeCreate(
          deExpressionGetWidth(typeExpression)), secret);
    case DE_EXPR_INTTYPE:
      return datatype == deSetDatatypeSecret(
          deIntDatatypeCreate(deExpressionGetWidth(typeExpression)), secret);
    case DE_EXPR_FLOATTYPE:
      return datatype == deSetDatatypeSecret(deFloatDatatypeCreate(
          deExpressionGetWidth(typeExpression)), secret);
    case DE_EXPR_STRINGTYPE:
      return datatype == deSetDatatypeSecret(deStringDatatypeCreate(), secret);
    case DE_EXPR_BOOLTYPE:
      return datatype == deSetDatatypeSecret(deBoolDatatypeCreate(), secret);
    case DE_EXPR_DOTDOTDOT:
      return matchDotDotDotTypeConstraint(scopeBlock, datatype, typeExpression);
    default:
      deError(line, "Invalid type constraint expression");
  }
  return false;  // Dummy return.
}

// Unify two array datatypes.
static deDatatype unifyArrayDatatypes(deDatatype datatype1, deDatatype datatype2) {
  deDatatype elementType1 = deDatatypeGetElementType(datatype1);
  deDatatype elementType2 = deDatatypeGetElementType(datatype2);
  deDatatype unifiedType = deUnifyDatatypes(elementType1, elementType2);
  if (unifiedType == deDatatypeNull) {
    return deDatatypeNull;
  }
  return deArrayDatatypeCreate(unifiedType);
}

// Unify two tuple datatypes.
static deDatatype unifyTupleDatatypes(deDatatype datatype1, deDatatype datatype2) {
  uint32 numElements = deDatatypeGetNumTypeList(datatype1);
  if (deDatatypeGetNumTypeList(datatype2) != numElements) {
    return deDatatypeNull;
  }
  deDatatypeArray datatypes = deDatatypeArrayAlloc();
  for (uint32 i = 0; i < numElements; i++) {
    deDatatype elementType1 = deDatatypeGetiTypeList(datatype1, i);
    deDatatype elementType2 = deDatatypeGetiTypeList(datatype2, i);
    deDatatype unifiedType = deUnifyDatatypes(elementType1, elementType2);
    if (unifiedType == deDatatypeNull) {
      deDatatypeArrayFree(datatypes);
      return deDatatypeNull;
    }
    deDatatypeArrayAppendDatatype(datatypes, unifiedType);
  }
  return deTupleDatatypeCreate(datatypes);
}

// Unify two datatypes.  A NULL datatype unifies to the other class type, if
// they have the same tclasses.
deDatatype deUnifyDatatypes(deDatatype datatype1, deDatatype datatype2) {
  if (datatype1 == datatype2) {
    return datatype1;
  }
  deDatatypeType type1 = deDatatypeGetType(datatype1);
  deDatatypeType type2 = deDatatypeGetType(datatype2);
  if (type1 == DE_TYPE_NULL && type2 == DE_TYPE_CLASS) {
    deTclass tclass = deDatatypeGetTclass(datatype1);
    if (deClassGetTclass(deDatatypeGetClass(datatype2)) == tclass) {
      return deSetDatatypeNullable(datatype2, true, deLineNull);
    }
  }
  if (type2 == DE_TYPE_NULL && type1 == DE_TYPE_CLASS) {
    deTclass tclass = deDatatypeGetTclass(datatype2);
    if (deClassGetTclass(deDatatypeGetClass(datatype1)) == tclass) {
      return deSetDatatypeNullable(datatype1, true, deLineNull);
    }
  }
  if (type1 != type2) {
    return deDatatypeNull;
  }
  if (type1 == DE_TYPE_ARRAY) {
    return unifyArrayDatatypes(datatype1, datatype2);
  } else if (type1 == DE_TYPE_TUPLE) {
    return unifyTupleDatatypes(datatype1, datatype2);
  }
  if (type1 == DE_TYPE_CLASS && type2 == DE_TYPE_CLASS &&
      deDatatypeGetClass(datatype1) == deDatatypeGetClass(datatype2) &&
      (deDatatypeNullable(datatype1) || deDatatypeNullable(datatype2))) {
    deDatatype nullableType = deSetDatatypeNullable(datatype1, true, deLineNull);
    if (deSetDatatypeNullable(datatype2, true, deLineNull) == nullableType) {
      return nullableType;
    }
  }
  if ((deDatatypeSecret(datatype1) || deDatatypeSecret(datatype2))) {
    deDatatype secretType = deSetDatatypeSecret(datatype1, true);
    if (deSetDatatypeSecret(datatype2, true) == secretType) {
      return secretType;
    }
  }
  return deDatatypeNull;
}

// Return the base element type of a potentially multi-dimensional array.
deDatatype deArrayDatatypeGetBaseDatatype(deDatatype datatype) {
  deDatatype elementType = deDatatypeGetElementType(datatype);
  while (deDatatypeGetType(elementType) == DE_TYPE_ARRAY) {
    elementType = deDatatypeGetElementType(elementType);
  }
  return elementType;
}

// Return the depth of a potentially multi-dimensional array.
uint32 deArrayDatatypeGetDepth(deDatatype datatype) {
  uint32 depth = 1;
  deDatatype elementType = deDatatypeGetElementType(datatype);
  while (deDatatypeGetType(elementType) == DE_TYPE_ARRAY) {
    elementType = deDatatypeGetElementType(elementType);
    depth++;
  }
  return depth;
}

// Find a concrete datatype for the function if it is unique.  Functions
// represent struct types, which often are concrete.
static deDatatype findUniqueConcreteFunctionDatatype(deDatatype datatype, deLine line) {
  deFunction function = deDatatypeGetFunction(datatype);
  deFunctionType funcType = deFunctionGetType(function);
  // TODO: deal with other function types such as constructors.
  if (funcType == DE_FUNC_STRUCT) {
    deBlock block = deFunctionGetSubBlock(function);
    deDatatypeArray paramTypes = deFindFullySpecifiedParameters(block);
    return deStructDatatypeCreate(function, paramTypes, deFunctionGetLine(function));
  }
  return deDatatypeNull;
}

// Find a concrete datatype for the array if it is unique.
static deDatatype findUniqueConcreteArrayDatatype(deDatatype datatype, deLine line) {
  deDatatype elemType = deFindUniqueConcreteDatatype(deDatatypeGetElementType(datatype), line);
  if (elemType == deDatatypeNull) {
    return deDatatypeNull;
  }
  return deArrayDatatypeCreate(elemType);
}

// Find a concrete datatype for the array if it is unique.
static deDatatype findUniqueConcreteTclassDatatype(deDatatype datatype) {
  deClass defaultClass = deTclassGetDefaultClass(deDatatypeGetTclass(datatype));
  if (defaultClass == deClassNull) {
    return deDatatypeNull;
  }
  return deClassDatatypeCreate(defaultClass);
}

// Find a concrete datatype for the array if it is unique.
static deDatatype findUniqueConcreteTupleDatatype(deDatatype datatype, deLine line) {
  deDatatypeArray types = deDatatypeArrayAlloc();
  deDatatype type;
  deForeachDatatypeTypeList(datatype, type) {
    deDatatype concreteType = deFindUniqueConcreteDatatype(type, line);
    if (concreteType == deDatatypeNull) {
      deDatatypeArrayFree(types);
      return deDatatypeNull;
    }
  } deEndDatatypeTypeList;
  return deTupleDatatypeCreate(types);
}

// Find a concrete datatype for the array if it is unique.
static deDatatype findUniqueConcreteStructDatatype(deDatatype datatype) {
  deFunction function = deDatatypeGetFunction(datatype);
  deBlock block = deFunctionGetSubBlock(function);
  deDatatypeArray paramTypes = deFindFullySpecifiedParameters(block);
  return deStructDatatypeCreate(function, paramTypes, deFunctionGetLine(function));
}

// Allow users to specify simpler type constraints when an abstract type has
// only one possible concrete type.  For example a template class (Tclass) with
// no template parameters has only one possible class instantiation.  Instead of
// having users specify "point: typeof(Point(i32, i32))", allow them to use
// "point: Point".  This is helpful in specifying functions/methods/RPCs which
// are imported/exported, because they are required to have concrete types for
// all parameters, and the return type.
deDatatype deFindUniqueConcreteDatatype(deDatatype datatype, deLine line) {
  if (deDatatypeConcrete(datatype)) {
    return datatype;
  }
  deDatatypeType type = deDatatypeGetType(datatype);
  switch (type) {
  case DE_TYPE_FUNCTION:
    return findUniqueConcreteFunctionDatatype(datatype, line);
  case DE_TYPE_ARRAY:
    return findUniqueConcreteArrayDatatype(datatype, line);
  case DE_TYPE_TCLASS:
    return findUniqueConcreteTclassDatatype(datatype);
  case DE_TYPE_TUPLE:
    return findUniqueConcreteTupleDatatype(datatype, line);
  case DE_TYPE_STRUCT:
    return findUniqueConcreteStructDatatype(datatype);
  case DE_TYPE_ENUMCLASS:
    return deEnumDatatypeCreate(deDatatypeGetFunction(datatype));
  default:
    return deDatatypeNull;
  }
  return deDatatypeNull;  // Dummy return.
}
// Combine two sectypes.
deSecretType deCombineSectypes(deSecretType a, deSecretType b) {
  if (a == DE_SECTYPE_NONE) {
    return b;
  }
  if (b == DE_SECTYPE_NONE) {
    return a;
  }
  if (a == b) {
    return a;
  }
  return DE_SECTYPE_MIXED;
}

// Determine if every sub-element of the datatype is secret.
deSecretType deFindDatatypeSectype(deDatatype datatype) {
  if (deDatatypeSecret(datatype)) {
      return DE_SECTYPE_ALL_SECRET;
  }
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_BOOL:
    case DE_TYPE_STRING:
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_FLOAT:
    case DE_TYPE_ENUMCLASS:
    case DE_TYPE_ENUM:
      return deDatatypeSecret(datatype)? DE_SECTYPE_ALL_SECRET : DE_SECTYPE_ALL_PUBLIC;
    case DE_TYPE_ARRAY:
      return deFindDatatypeSectype(deDatatypeGetElementType(datatype));
    case DE_TYPE_TUPLE:
    case DE_TYPE_STRUCT: {
      deSecretType combinedType = DE_SECTYPE_NONE;
      deDatatype type;
      deForeachDatatypeTypeList(datatype, type) {
        deSecretType subSecretType = deFindDatatypeSectype(type);
        combinedType = deCombineSectypes(combinedType, subSecretType);
      } deEndDatatypeTypeList;
      return combinedType;
    }
    case DE_TYPE_NONE:
      return DE_SECTYPE_NONE;
    case DE_TYPE_FUNCPTR:
    case DE_TYPE_FUNCTION:
    case DE_TYPE_CLASS:
    case DE_TYPE_TCLASS:
    case DE_TYPE_NULL:
    case DE_TYPE_MODINT:
      utExit("Unexpected datatype in RPC call");
  }
  return false;  // Dummy return.
}
