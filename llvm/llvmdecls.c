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

// Declare  functions used by the program.
#include "ll.h"
#include "runtime.h"

#include <ctype.h>

static uint32 llStringNum;
static uint32 llArrayNum;
static uint32 llTupleNum;

// Return true if the datatype is an int or uint > uint64 width.  These
// integers are represented as bigints.
bool llDatatypeIsBigint(deDatatype datatype) {
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type == DE_TYPE_UINT || type == DE_TYPE_INT || type == DE_TYPE_MODINT) {
    uint32 width = deDatatypeGetWidth(datatype);
    return width > llSizeWidth;
  }
  return false;
}

// Return true if the datatype is represented as an array: arrays, strings, and bigints.
bool llDatatypeIsArray(deDatatype datatype) {
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type == DE_TYPE_ARRAY || type == DE_TYPE_STRING) {
    return true;
  }
  if (type == DE_TYPE_UINT || type == DE_TYPE_INT || type == DE_TYPE_MODINT) {
    uint32 width = deDatatypeGetWidth(datatype);
    return width > llSizeWidth;
  }
  return false;
}

// True if an array, tuple, string or bigint.
bool llDatatypePassedByReference(deDatatype datatype) {
  deDatatypeType type = deDatatypeGetType(datatype);
  return type == DE_TYPE_TUPLE || type == DE_TYPE_STRUCT || deDatatypeContainsArray(datatype);
}

// Escape a string.  Non-printable characters and " are represented as \xx, two
// hex digits.
static char *escapeString(char *text, uint32 len) {
  char *buf = utMakeString(3 * len);
  char *p = text;
  char *q = buf;
  while (len-- != 0) {
    uint8 c = *p++;
    if (c == '\\') {
      *q++ = '\\';
      *q++ = '\\';
    } else if (c >= ' ' && c != '"') {
      // Looks like UTF-8 can pass through.
      *q++ = c;
    } else {
      *q++ = '\\';
      *q++ = deToHex(c >> 4);
      *q++ = deToHex(c & 0xf);
    }
  }
  *q++ = '\0';
  return buf;
}

// Determine if the identifier conforms to: [-a-zA-Z$._][-a-zA-Z$._0-9]*
static bool isLegalIdentifier(char *identifier) {
  char *p = identifier;
  uint32 len = strlen(identifier);
  char c = *p++;
  if (!isalpha(c) && c != '-' && c != '.' && c != '_') {
    return false;
  }
  c = *p++;
  for (uint32 i = 1; i < len; i++) {
    if (!isalnum(c) && c != '-' && c != '.' && c != '_' && c != '$') {
      return false;
    }
    c = *p++;
  }
  return true;
}

// Return an escaped identifier.
// If they fit this format, just return them.  Otherwise, surround them with
// quotes, and escape the string.  A temp buffer may be returned.
char *llEscapeIdentifier(char *identifier) {
  if (isLegalIdentifier(identifier)) {
    return identifier;
  }
  return utSprintf("\"%s\"", escapeString(identifier, strlen(identifier)));
}

// Forward reference for recursion.
static char *getTypeString(deDatatype datatype, bool isDefinition);

// Return a type string for the function pointer.
static char *getFuncptrTypeString(deDatatype datatype) {
  char *type = utSprintf("%s (", getTypeString(deDatatypeGetReturnType(datatype), false));
  for (uint32_t i = 0; i < deDatatypeGetNumTypeList(datatype); i++) {
    char *argType = getTypeString(deDatatypeGetiTypeList(datatype, i), false);
    if (i != 0) {
      type = utSprintf("%s, %s", type, argType);
    } else {
      type = utSprintf("%s%s", type, argType);
    }
  }
  return utSprintf("%s)*", type);
}

// Write tuple declarations.
static void writeTupleDecl(llTuple tuple) {
  fprintf(llAsmFile, "%%struct.runtime_tuple%u = type {", llTupleGetNum(tuple));
  deDatatype datatype = llTupleGetDatatype(tuple);
  bool firstTime = true;
  deDatatype elementType;
  deForeachDatatypeTypeList(datatype, elementType) {
    if (!firstTime) {
      fputs(", ", llAsmFile);
    }
    firstTime = false;
    fputs(llGetTypeString(elementType, true), llAsmFile);
  } deEndDatatypeTypeList;
  fputs("}\n", llAsmFile);
}

// Declare the tuple type to represent the datatype.
static llTuple declareTuple(deDatatype datatype) {
  llTuple tuple = llRootFindTuple(deTheRoot, datatype);
  if (tuple != llTupleNull) {
    return tuple;
  }
  tuple = llTupleAlloc();
  llTupleSetDatatype(tuple, datatype);
  llTupleSetNum(tuple, llTupleNum);
  llTupleNum++;
  llRootAppendTuple(deTheRoot, tuple);
  llRootAppendNewTuple(deTheRoot, tuple);
  return tuple;
}

// Declare all the new tuples that were used while generating code for a
// function.
void llDeclareNewTuples(void) {
  llTuple tuple;
  llSafeForeachRootNewTuple(deTheRoot, tuple) {
    writeTupleDecl(tuple);
    llRootRemoveNewTuple(deTheRoot, tuple);
  } llEndSafeRootTuple;
}

// Return a type string for the tuple.
static char *getTupleTypeString(deDatatype datatype, bool isDefinition) {
  llTuple tuple = llRootFindTuple(deTheRoot, datatype);
  if (deDatatypeGetType(datatype) == DE_TYPE_TUPLE) {
    tuple = declareTuple(datatype);
  }
  if (isDefinition) {
    return utSprintf("%%struct.runtime_tuple%u", llTupleGetNum(tuple));
  }
  return utSprintf("%%struct.runtime_tuple%u*", llTupleGetNum(tuple));
}

// This is a CTTK specific function to determine the number of 32-bit words
// needed to represent an integer.  An extra word is prepended to represent the
// signed vs unsigned type, which hopefully will get integrated into CTTK
// natively in the future.  See: https://github.com/pornin/CTTK/issues/7.
uint32 llBigintBitsToWords(uint32 width, bool isSigned) {
  if (isSigned) {
    return 2 + ((width + 30)/31);
  }
  // Unsigned integers require 1 extra bit in CTTK, which only has signed
  // arithmetic.
  return 3 + width/31;
}

// Print an LLVM formatted type to llAsmFile.  If this is not the definition,
// then append * to types that are passed by reference.
static char *getTypeString(deDatatype datatype, bool isDefinition) {
  deDatatypeType type = deDatatypeGetType(datatype);
  switch (type) {
    case DE_TYPE_BOOL:
      return "i1";
    case DE_TYPE_STRING:
    case DE_TYPE_ARRAY:
      if (isDefinition) {
        return "%struct.runtime_array";
      } else {
        return "%struct.runtime_array*";
      }
    case DE_TYPE_UINT:
    case DE_TYPE_INT: {
      uint32 width = deDatatypeGetWidth(datatype);
      if (width <= llSizeWidth) {
        return utSprintf("i%u", width);
      } else {
        if (isDefinition) {
          return "%struct.runtime_array";
        } else {
          return "%struct.runtime_array*";
        }
      }
    }
    case DE_TYPE_MODINT:
      return getTypeString(deExpressionGetDatatype(deDatatypeGetModulus(datatype)), isDefinition);
      break;
    case DE_TYPE_FLOAT: {
      uint32 width = deDatatypeGetWidth(datatype);
      if (width == 32) {
        return "float";
      } else if (width == 64) {
        return "double";
      }
      utExit("Unexpected float width %u", width);
    }
    case DE_TYPE_CLASS:
      return utSprintf("i%u", deClassGetRefWidth(deDatatypeGetClass(datatype)));
    case DE_TYPE_NULL:
      return utSprintf("i%u", deTclassGetRefWidth(deDatatypeGetTclass(datatype)));
    case DE_TYPE_FUNCPTR:
      return getFuncptrTypeString(datatype);
    case DE_TYPE_STRUCT:
      return getTupleTypeString(deGetStructTupleDatatype(datatype), isDefinition);
    case DE_TYPE_ENUM:
      return utSprintf("i%u", deDatatypeGetWidth(datatype));
    case DE_TYPE_TUPLE:
      return getTupleTypeString(datatype, isDefinition);
    case DE_TYPE_NONE:
      return "void";
    case DE_TYPE_FUNCTION:
    case DE_TYPE_TCLASS:
    case DE_TYPE_ENUMCLASS:
      utExit("Unexpected type");
      break;
  }
  return NULL;  // Dummy return.
}

// Return a string representing the datatype.  Returns a temp buffer.  If this
// is not the definition, then append * to types that are passed by reference.
char *llGetTypeString(deDatatype datatype, bool isDefinition) {
  deString typeString;
  if (isDefinition) {
    typeString = llDatatypeGetDefinitionTypeString(datatype);
    if (typeString == deStringNull) {
      char *text = getTypeString(datatype, isDefinition);
      typeString = deStringCreate(text, strlen(text) + 1);
      llDatatypeSetDefinitionTypeString(datatype, typeString);
    }
  } else {
    typeString = llDatatypeGetReferenceTypeString(datatype);
    if (typeString == deStringNull) {
      char *text = getTypeString(datatype, isDefinition);
      typeString = deStringCreate(text, strlen(text) + 1);
      llDatatypeSetReferenceTypeString(datatype, typeString);
    }
  }
  return utCopyString(deStringGetText(typeString));
}

// Create a new llFuncDecl object.
static llFuncDecl createFuncDecl(char *name, char *text) {
  utSym sym = utSymCreate(name);
  if (llRootFindFuncDecl(deTheRoot, sym) != llFuncDeclNull) {
    utExit("Redeclaration of funcDecl %s", name);
  }
  llFuncDecl decl = llFuncDeclAlloc();
  llFuncDeclSetSym(decl, sym);
  llFuncDeclSetText(decl, text, strlen(text) + 1);
  llRootAppendFuncDecl(deTheRoot, decl);
  return decl;
}

// Declare runtime functions.
static void declareRuntimeFunctions(void) {
  createFuncDecl("calloc", utSprintf("declare dso_local noalias i8* @calloc(i%s, i%s)", llSize, llSize));
  createFuncDecl("runtime_initArrayFromC",
      "declare dso_local void @runtime_initArrayOfStringsFromC(%struct.runtime_array*, i8**, i32)");
  createFuncDecl("runtime_initArrayFromCUTF8",
      "declare dso_local void @runtime_initArrayOfStringsFromCUTF8(%struct.runtime_array*, i8**, i32)");
  createFuncDecl("runtime_concatArrays", utSprintf(
      "declare dso_local void @runtime_concatArrays(%%struct.runtime_array*, %%struct.runtime_array*, "
     "i%s, i1 zeroext)", llSize));
  createFuncDecl("runtime_xorStrings",
      "declare dso_local void @runtime_xorStrings(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)\n");
  createFuncDecl("runtime_freeArray", "declare dso_local void @runtime_freeArray(%struct.runtime_array*)");
  createFuncDecl("runtime_foreachArrayObject",
      "declare dso_local void @runtime_foreachArrayObject(%struct.runtime_array*, i8 *, i32, i32)");
  createFuncDecl("runtime_panicCstr", "declare dso_local void @runtime_panicCstr(i8*, ...)");
  createFuncDecl("runtime_allocArray", utSprintf(
      "declare dso_local void @runtime_allocArray(%%struct.runtime_array*, i%s, i%s, i1 zeroext)",
      llSize, llSize));
  createFuncDecl("runtime_appendArrayElement", utSprintf(
      "declare dso_local void @runtime_appendArrayElement(%%struct.runtime_array*, i8*, i%s, i1 zeroext, i1 zeroext)",
      llSize));
  createFuncDecl("runtime_arrayStart", utSprintf("declare dso_local void @runtime_arrayStart()"));
  createFuncDecl("runtime_arrayStop", "declare dso_local void @runtime_arrayStop()");
  createFuncDecl("runtime_compactArrayHeap", "declare dso_local void @runtime_compactArrayHeap()");
  createFuncDecl("runtime_copyArray", utSprintf(
      "declare dso_local void @runtime_copyArray(%%struct.runtime_array*, %%struct.runtime_array*, i%s, i1 zeroext)",
      llSize));
  createFuncDecl("runtime_moveArray", "declare dso_local void @runtime_moveArray(%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_sliceArray", utSprintf(
      "declare dso_local void @runtime_sliceArray(%%struct.runtime_array*, %%struct.runtime_array*, "
      "i%s, i%s, i%s, i1 zeroext)", llSize, llSize, llSize));
  createFuncDecl("runtime_reverseArray", utSprintf(
      "declare dso_local void @runtime_reverseArray(%%struct.runtime_array*, i%s, i1 zeroext)", llSize));
  createFuncDecl("runtime_nativeIntToString", utSprintf(
      "declare dso_local void @runtime_nativeIntToString(%%struct.runtime_array*, i%s, i32, i1 zeroext)",
      llSize));
  createFuncDecl("runtime_panic", "declare dso_local void @runtime_panic(%struct.runtime_array*, ...) noreturn");
  createFuncDecl("runtime_putsCstr", "declare dso_local void @runtime_putsCstr(i8*)");
  createFuncDecl("runtime_puts", "declare dso_local void @runtime_puts(%struct.runtime_array*)");
  createFuncDecl("runtime_resizeArray", utSprintf(
      "declare dso_local void @runtime_resizeArray(%%struct.runtime_array*, i%s, i%s, i1 zeroext)",
      llSize, llSize));
  createFuncDecl("runtime_stringToHex",
      "declare dso_local void @runtime_stringToHex(%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_hexToString",
      "declare dso_local void @runtime_hexToString(%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_stringFind", utSprintf(
      "declare dso_local i%s @runtime_stringFind(%%struct.runtime_array*, %%struct.runtime_array*, i%s)", llSize, llSize));
  createFuncDecl("runtime_stringRfind", utSprintf(
      "declare dso_local i%s @runtime_stringRfind(%%struct.runtime_array*, %%struct.runtime_array*, i%s)", llSize, llSize));
  createFuncDecl("runtime_throwExceptionCstr", "declare dso_local void @runtime_throwExceptionCstr(i8*, ...) noreturn");
  createFuncDecl("runtime_throwException", "declare dso_local void @runtime_throwException(%struct.runtime_array*, ...) noreturn");
  createFuncDecl("runtime_throwOverflow", "declare dso_local void @runtime_throwOverflow() noreturn");
  createFuncDecl("runtime_vsprintf", "declare dso_local void @runtime_vsprintf(%struct.runtime_array*, %struct.runtime_array*, %struct.__va_list_tag*)");
  createFuncDecl("runtime_sprintf", "declare dso_local void @runtime_sprintf(%struct.runtime_array*, %struct.runtime_array*, ...)");
  createFuncDecl("runtime_makeEmptyArray",
      utSprintf("declare internal {i%s*, i%s} @runtime_makeEmptyArray()", llSize, llSize));
  createFuncDecl("runtime_generateTrueRandomValue",
      utSprintf("declare dso_local i%s @runtime_generateTrueRandomValue(i32)", llSize));
  createFuncDecl("runtime_generateTrueRandomBigint",
      "declare dso_local void @runtime_generateTrueRandomBigint(%struct.runtime_array*, i32)");
  createFuncDecl("llvm.dbg.declare",
      "declare void @llvm.dbg.declare(metadata, metadata, metadata)");
  createFuncDecl("llvm.dbg.value",
      "declare void @llvm.dbg.value(metadata, metadata, metadata)");
  createFuncDecl("runtime_compareArrays", utSprintf(
      "declare i1 @runtime_compareArrays(i32, i32, %%struct.runtime_array*, "
      "%%struct.runtime_array*, i%s, i1 zeroext, i1 zeroext)", llSize));
  createFuncDecl("runtime_updateArrayBackPointer",
      "declare void @runtime_updateArrayBackPointer(%struct.runtime_array*)");
  createFuncDecl("runtime_bigintWidth", "declare zeroext i32 @runtime_bigintWidth(%struct.runtime_array*)");
  createFuncDecl("runtime_bigintSigned", "declare zeroext i1 @runtime_bigintSigned(%struct.runtime_array*)");
  createFuncDecl("runtime_bigintSecret", "declare zeroext i1 @runtime_bigintSecret(%struct.runtime_array*)");
  createFuncDecl("runtime_bigintZero", "declare i32 @runtime_bigintZero(%struct.runtime_array*)");
  createFuncDecl("runtime_boolToRnBool", "declare i32 @runtime_boolToRnBool(i1 zeroext)");
  createFuncDecl("runtime_bigintNegative", "declare i32 @runtime_bigintNegative(%struct.runtime_array*)");
  createFuncDecl("runtime_bigintCast",
     "declare void @runtime_bigintCast(%struct.runtime_array*, "
     "%struct.runtime_array*, i32 zeroext, i1 zeroext, i1 zeroext, i1 zeroext)");
  createFuncDecl("runtime_integerToBigint", utSprintf(
      "declare dso_local void @runtime_integerToBigint(%%struct.runtime_array*, "
      "i%s, i32, i1, i1)", llSize));
  createFuncDecl("runtime_bigintToInteger", utSprintf(
      "declare i%s @runtime_bigintToInteger(%%struct.runtime_array*)", llSize));
  createFuncDecl("runtime_bigintToIntegerTrunc", utSprintf(
      "declare i%s @runtime_bigintToIntegerTrunc(%%struct.runtime_array*)", llSize));
  createFuncDecl("runtime_bigintToString", utSprintf(
      "declare void @runtime_bigintToString(%%struct.runtime_array*, %%struct.runtime_array*, i32)",
      llSize, llSize));
  createFuncDecl("runtime_bigintDecodeBigEndian",
      "declare void @runtime_bigintDecodeBigEndian(%struct.runtime_array*, "
      "%struct.runtime_array*, i32 zeroext, i1 zeroext, i1 zeroext)");
  createFuncDecl("runtime_bigintDecodeLittleEndian",
      "declare void @runtime_bigintDecodeLittleEndian(%struct.runtime_array*, "
      "%struct.runtime_array*, i32 zeroext, i1 zeroext, i1 zeroext)");
  createFuncDecl("runtime_bigintEncodeBigEndian",
      "declare void @runtime_bigintEncodeBigEndian(%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintEncodeLittleEndian",
      "declare void @runtime_bigintEncodeLittleEndian(%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintToU32", "declare i32 @runtime_bigintToU32(%struct.runtime_array*)");
  createFuncDecl("runtime_compareBigints",
      "declare zeroext i1 @runtime_compareBigints(i32, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintAdd",
      "declare void @runtime_bigintAdd(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintAddTrunc",
      "declare void @runtime_bigintAddTrunc(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintSub",
      "declare void @runtime_bigintSub(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintSubTrunc",
      "declare void @runtime_bigintSubTrunc(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintMul",
      "declare void @runtime_bigintMul(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintMulTrunc",
      "declare void @runtime_bigintMulTrunc(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintDiv",
      "declare void @runtime_bigintDiv(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintMod",
      "declare void @runtime_bigintMod(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintExp",
      "declare void @runtime_bigintExp(%struct.runtime_array*, %struct.runtime_array*, i32)");
  createFuncDecl("runtime_bigintNegate",
      "declare void @runtime_bigintNegate(%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintNegateTrunc", "declare void @runtime_bigintNegateTrunc(%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintComplement",
      "declare void @runtime_bigintComplement(%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintRotl",
      "declare void @runtime_bigintRotl(%struct.runtime_array*, %struct.runtime_array*, i32)");
  createFuncDecl("runtime_bigintRotr",
      "declare void @runtime_bigintRotr(%struct.runtime_array*, %struct.runtime_array*, i32)");
  createFuncDecl("runtime_bigintShl",
      "declare void @runtime_bigintShl(%struct.runtime_array*, %struct.runtime_array*, i32)");
  createFuncDecl("runtime_bigintShr",
      "declare void @runtime_bigintShr(%struct.runtime_array*, %struct.runtime_array*, i32)");
  createFuncDecl("runtime_bigintBitwiseAnd",
      "declare void @runtime_bigintBitwiseAnd(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintBitwiseOr",
      "declare void @runtime_bigintBitwiseOr(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintBitwiseXor",
      "declare void @runtime_bigintBitwiseXor(%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintModularAdd",
      "declare void @runtime_bigintModularAdd(%struct.runtime_array*, %struct.runtime_array*, "
      "%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintModularSub",
      "declare void @runtime_bigintModularSub(%struct.runtime_array*, %struct.runtime_array*, "
      "%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintModularMul",
      "declare void @runtime_bigintModularMul(%struct.runtime_array*, %struct.runtime_array*, "
      "%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintModularDiv",
      "declare void @runtime_bigintModularDiv(%struct.runtime_array*, %struct.runtime_array*, "
      "%struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintModularExp",
      "declare void @runtime_bigintModularExp(%struct.runtime_array*, %struct.runtime_array*, "
      "%struct.runtime_array*, %struct.runtime_array*);");
  createFuncDecl("runtime_bigintModularNegate", "declare void @runtime_bigintModularNegate("
      "%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_smallnumMul", utSprintf(
      "declare i%s @runtime_smallnumMul(i%s, i%s, i1 zeroext, i1 zeroext)", llSize, llSize, llSize));
  createFuncDecl("runtime_smallnumDiv", utSprintf(
      "declare i%s @runtime_smallnumDiv(i%s, i%s, i1 zeroext, i1 zeroext)", llSize, llSize, llSize));
  createFuncDecl("runtime_smallnumMod", utSprintf(
      "declare i%s @runtime_smallnumMod(i%s, i%s, i1 zeroext, i1 zeroext)", llSize, llSize, llSize));
  createFuncDecl("runtime_smallnumExp", utSprintf(
      "declare i%s @runtime_smallnumExp(i%s, i32, i1 zeroext, i1 zeroext)", llSize, llSize));
  createFuncDecl("runtime_smallnumModReduce", utSprintf(
      "declare i%s @runtime_smallnumModReduce(i%s, i%s, i1 zeroext, i1 zeroext)", llSize, llSize, llSize));
  createFuncDecl("runtime_smallnumModularAdd", utSprintf(
      "declare i%s @runtime_smallnumModularAdd(i%s, i%s, i%s, i1 zeroext)",
      llSize, llSize, llSize, llSize));
  createFuncDecl("runtime_smallnumModularSub", utSprintf(
      "declare i%s @runtime_smallnumModularSub(i%s, i%s, i%s, i1 zeroext)",
      llSize, llSize, llSize, llSize));
  createFuncDecl("runtime_smallnumModularMul", utSprintf(
      "declare i%s @runtime_smallnumModularMul(i%s, i%s, i%s, i1 zeroext)",
      llSize, llSize, llSize, llSize));
  createFuncDecl("runtime_smallnumModularDiv", utSprintf(
      "declare i%s @runtime_smallnumModularDiv(i%s, i%s, i%s, i1 zeroext)",
      llSize, llSize, llSize, llSize));
  createFuncDecl("runtime_smallnumModularExp", utSprintf(
      "declare i%s @runtime_smallnumModularExp(i%s, i%s, i%s, i1 zeroext)",
      llSize, llSize, llSize, llSize));
  createFuncDecl("runtime_smallnumModularNegate", utSprintf(
      "declare i%s @runtime_smallnumModularNegate(i%s, i%s, i1 zeroext)",
      llSize, llSize, llSize));
  createFuncDecl("runtime_boolAnd", "declare i32 @runtime_boolAnd(i32, i32)");
  createFuncDecl("runtime_rnBoolToBool", "declare zeroext i1 @runtime_rnBoolToBool(i32)");
  createFuncDecl("runtime_boolOr", "declare i32 @runtime_boolOr(i32, i32)");
  createFuncDecl("runtime_boolNot", "declare i32 @runtime_boolNot(i32)");
  createFuncDecl("runtime_selectUint32", "declare i32 @runtime_selectUint32(i32, i32, i32)");
  createFuncDecl("runtime_bigintCondCopy",
      "declare void @runtime_bigintCondCopy(i32, %struct.runtime_array*, %struct.runtime_array*)");
  createFuncDecl("runtime_bigintDivRem",
      "declare void @runtime_bigintDivRem(%struct.runtime_array*, "
      "%struct.runtime_array*, %struct.runtime_array*, %struct.runtime_array*)");
}

// Initialize the  declarations module.
void llStart(void) {
  llDatabaseStart();
  llStringNum = 1;
  llArrayNum = 1;
  llTupleNum = 1;
  declareRuntimeFunctions();
  if (llDebugMode) {
    llCreateFilepathTags();
  }
}

// Clean up memory.
void llStop(void) {
  llDatabaseStop();
}

// Mark a function declaration as used.
void llDeclareRuntimeFunction(char *funcName) {
  llFuncDecl decl = llRootFindFuncDecl(deTheRoot, utSymCreate(funcName));
  if (decl == llFuncDeclNull) {
    utExit("Unknown declaration: %s", funcName);
  }
  if (!llFuncDeclUsed(decl)) {
    fprintf(llAsmFile, "%s\n", llFuncDeclGetText(decl));
    llFuncDeclSetUsed(decl, true);
  }
}

// Declare an overloaded function.
void llDeclareOverloadedFunction(char *text) {
  llFuncDecl decl = llRootFindFuncDecl(deTheRoot, utSymCreate(text));
  if (decl == llFuncDeclNull) {
    decl = createFuncDecl(text, text);
    fprintf(llAsmFile, "%s\n", llFuncDeclGetText(decl));
  }
}

// Write the  string to the llAsmFile.  Format:
// @.array1 = internal constant %struct.runtime_array {i64* bitcast ([14 x i8]* @.str1 to i64*), i64 14}
// @.str1 = private unnamed_addr constant [14 x i8] c"Hello, World!\0A"
static void writeString(deString string) {
  uint32 num = llStringGetNum(string);
  char *escapedString = llEscapeString(string);
  uint32 len = deStringGetNumText(string);
  fprintf(llAsmFile,
      "@.str%u = internal constant %%struct.runtime_array {i%s* bitcast "
      "([%u x i8]* @.str%u.data to i%s*), i%s %u}\n",
      num, llSize, len, num, llSize, llSize, len);
  fprintf(llAsmFile,
      "@.str%u.data = private unnamed_addr constant [%u x i8] c\"%s\", align %u\n",
      num, len, escapedString, llSizeWidth/8);
}

// Add a string constant declaration to the output.  Return the string name.
void llAddStringConstant(deString string) {
  // We expect these strings to be hashed and unique.
  utAssert(deStringGetRoot(string) == deTheRoot);
  if (llStringGetRoot(string) != deRootNull) {
    return;
  }
  llStringSetNum(string, llStringNum);
  llStringNum++;
  llRootAppendString(deTheRoot, string);
  writeString(string);
}

// Write a constant bigint array, in CTTK format.
static void writeBigintArray(llArray array) {
  deExpression expression = llArrayGetExpression(array);
  deBigint bigint = deExpressionGetBigint(expression);
  // Get width from datatype in case bigint was auto-cast.
  uint32 width = deDatatypeGetWidth(deExpressionGetDatatype(expression));
  bool isSigned = deBigintSigned(bigint);
  uint32 numWords = llBigintBitsToWords(width, isSigned);
  if (!isSigned) {
    width++;
  }
  cti_elt data[numWords];
  uint8 *source = deBigintGetData(bigint);
  int16_t len = deBigintGetNumData(bigint);
  data[0] = isSigned?  RN_SIGNED_BIT : 0;
  cti_init(data + 1, width);
  if (isSigned) {
    cti_decle_signed(data + 1, source, len);
  } else {
    cti_decle_unsigned(data + 1, source, len);
  }
  uint32 num = llArrayGetNum(array);
  fprintf(llAsmFile,
      "@.array%u = internal constant %%struct.runtime_array {i%s* bitcast "
      "([%u x i32]* @.array%u.data to i%s*), i%s %u}\n",
      num, llSize, numWords, num, llSize, llSize, numWords);
  fprintf(llAsmFile, "@.array%u.data = private unnamed_addr constant [%u x i32] [", num, numWords);
  bool firstTime = true;
  for (uint32 i = 0; i < numWords; i++) {
    if (!firstTime) {
      fputs(", ", llAsmFile);
    }
    firstTime = false;
    fprintf(llAsmFile, "i32 %u", data[i]);
  }
  fprintf(llAsmFile, "], align %u\n", llSizeWidth/8);
}

// Write the array constant, which can be a constant array expression, or a bigint.
static void writeArray(llArray array) {
  deExpression expression = llArrayGetExpression(array);
  if (llDatatypeIsBigint(deExpressionGetDatatype(expression))) {
    writeBigintArray(array);
    return;
  }
  uint32 num = llArrayGetNum(array);
  uint32 len = deExpressionCountExpressions(expression);
  deExpression element = deExpressionGetFirstExpression(expression);
  deExpressionType type = deExpressionGetType(element);
  uint32 width = 0;
  if (type == DE_EXPR_INTEGER) {
    // Get width from datatype in case bigint was auto-cast.
    width = deDatatypeGetWidth(deExpressionGetDatatype(element));
  } else if (type == DE_EXPR_BOOL) {
    width = 1;
  } else if (type == DE_EXPR_NULL) {
    width = 32;
  } else {
    utExit("Unexpected constant array expression type");
  }
  fprintf(llAsmFile,
      "@.array%u = internal constant %%struct.runtime_array {i%s* bitcast "
      "([%u x i%u]* @.array%u.data to i%s*), i%s %u}\n",
      num, llSize, len, width, num, llSize, llSize, len);
  fprintf(llAsmFile, "@.array%u.data = private unnamed_addr constant [%u x i%u] [",
      num, len, width);
  bool firstTime = true;
  deForeachExpressionExpression(expression, element) {
    if (!firstTime) {
      fputs(", ", llAsmFile);
    }
    firstTime = false;
    if (type == DE_EXPR_INTEGER) {
      deBigint bigint = deExpressionGetBigint(element);
      fprintf(llAsmFile, "i%u %s", width, deBigintToString(bigint, 10));
    } else if (type == DE_EXPR_BOOL) {
      fprintf(llAsmFile, "i1 %u", deExpressionBoolVal(element)? 1 : 0);
    } else if (type == DE_EXPR_NULL) {
      fprintf(llAsmFile, "i32 0");
    } else {
      utExit("Unexpected constant array expression type");
    }
  } deEndExpressionExpression;
  fprintf(llAsmFile, "], align %u\n", llSizeWidth/8);
}

// Add an array constant.
utSym llAddArrayConstant(deExpression expression) {
  llArray array = llArrayAlloc();
  llArraySetExpression(array, expression);
  llArraySetNum(array, llArrayNum);
  llArraySetName(array, utSymCreateFormatted("@.array%u", llArrayNum));
  llArrayNum++;
  llRootAppendArray(deTheRoot, array);
  writeArray(array);
  return llArrayGetName(array);
}

// Return a string of the form ".str<num>".
char *llStringGetName(deString string) {
  // Make sure it has been added.
  utAssert(llStringGetRoot(string) != deRootNull);
  return utSprintf("@.str%u", llStringGetNum(string));
}

// Declare an extern "C" function.
static void declareExternCFunction(deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  llFuncDecl decl = llRootFindFuncDecl(deTheRoot, deFunctionGetSym(function));
  if (decl != llFuncDeclNull) {
    llDeclareRuntimeFunction(deFunctionGetName(function));
    return;
  }
  bool first = true;
  deDatatype returnType = deSignatureGetReturnType(signature);
  char *funcName = llEscapeIdentifier(deGetSignaturePath(signature));
  if (llDatatypePassedByReference(returnType)) {
    llPrintf("declare dso_local void @%s(%s", funcName, llGetTypeString(returnType, false));
    first = false;
  } else {
    llPrintf("declare dso_local %s @%s(", llGetTypeString(returnType, false), funcName);
  }
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    if (!first) {
      llPuts(", ");
    }
    first = false;
    deDatatype datatype = deParamspecGetDatatype(paramspec);
    llPrintf("%s", llGetTypeString(datatype, false));
  } deEndSignatureParamspec;
  llPuts(")\n");
}

// Declare extern "C" functions.
void llDeclareExternCFunctions(void) {
  deSignature signature;
  deForeachRootSignature(deTheRoot, signature) {
    if (deFunctionGetLinkage(deSignatureGetFunction(signature)) == DE_LINK_EXTERN_C) {
      declareExternCFunction(signature);
    }
  } deEndRootSignature;
}

// Return the empty string if not debug mode, otherwise the tag name.
static char *getVariableTag(deVariable variable) {
  llTag tag = llVariableGetTag(variable);
  if (!llDebugMode || tag == llTagNull) {
    return "";
  }
  return utSprintf(", !dbg !%u", llTagGetNum(tag));
}

// Declare a global variable.
static void declareGlobalVariable(deVariable variable) {
  deDatatype datatype = deVariableGetDatatype(variable);
  deDatatypeType type = deDatatypeGetType(datatype);
  char *typeString = llGetTypeString(datatype, true);
  char *initializer;
  if (llDatatypeIsArray(datatype) || type == DE_TYPE_TUPLE || type == DE_TYPE_STRUCT ||
      type == DE_TYPE_FLOAT) {
    initializer = "zeroinitializer";
  } else if (type == DE_TYPE_CLASS) {
    initializer = "-1";
  } else {
    initializer = "0";
  }
  llPrintf("%s = dso_local global %s %s%s\n",
      llGetVariableName(variable), typeString, initializer, getVariableTag(variable));
}

// Declare the block's variables as globals.
void llDeclareBlockGlobals(deBlock block) {
  deFunction function = deBlockGetOwningFunction(block);
  deFunctionType type = deFunctionGetType(function);
  if (type != DE_FUNC_MODULE && type != DE_FUNC_PACKAGE) {
    return;
  }
  // Global variables are just the local variables of modules.
  if (llDebugMode) {
    llCreateGlobalVariableTags(block);
  }
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    if (deVariableGetType(variable) == DE_VAR_LOCAL &&
        deVariableInstantiated(variable)) {
      declareGlobalVariable(variable);
    }
  } deEndBlockVariable;
}

// Escape a string.  Non-printable characters are represented as \xx, two hex
// digits.
char *llEscapeString(deString string) {
  return escapeString(deStringGetText(string), deStringGetNumText(string));
}

// Escape a string.  Non-printable characters are represented as \xx, two hex
// digits.
char *llEscapeText(char *text) {
  return escapeString(text, strlen(text));
}

// Print declarations for all used functions.
void llWriteDeclarations(void) {
  if (llDebugMode) {
    llWriteDebugTags();
  }
}

