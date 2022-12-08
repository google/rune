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

// Generate LLVM IR assembly code.
#include "ll.h"
#include "runtime.h"
#include <ctype.h>
#include <math.h>

#define LL_TMPVARS_STRING ".tmpvars."

// The LLVM IR output file.
FILE* llAsmFile;
// When true, generate debugging symbols for gdb.
bool llDebugMode;
// If true, disable bounds checking, integer overflow detection, etc.
// Set to "32" for 32-bit mode, otherwise "64".
char *llSize;
// "i32" for 32-bit mode, otherwise "i64".
deDatatype llSizeType;
// Width of uint64.
uint32 llSizeWidth;
// The top level rune file.
char *llModuleName;
// Path of the current function being generated.
char *llPath;
// It turns out that alloca acts like alloc, and allocates new space on the
// stack each time it is executed, rather than just once per function call.  We
// have to move all the alloca's to the top of the function to avoid this memory
// leak.  Since they are generated while printing the function to deStringVal,
// write these allocs here instead, and insert them at the top.
char *llTmpValueBuffer;
uint32 llTmpValueLen;
uint32 llTmpValuePos;

// This is the LLVM variable number, such as %5.  It is incremented by most, but
// not all LLVM statements.
static uint32 llVarNum;
static uint32 llTmpVarNum;
static uint32 llLabelNum;
static deBlock llCurrentScopeBlock;
static deStatement llCurrentStatement;
static deLine llCurrentLine;
// Helps us generate only one call the runtime_throwException for bounds checking per function.
static utSym llLimitCheckFailedLabel;
static utSym llBoundsCheckFailedLabel;
static utSym llPrevLabel;  // Most recently printed label: used in phi instructions.

typedef struct {
  deDatatype datatype;
  utSym name;
  bool isRef;  // We lazy deref pointers.
  bool needsFree;  // Means we need to call runtime_freeArray when popped.
  bool isDelegate;  // The next element on the stack is the instance expression.
  bool isNull;  // The next element on the stack is the instance expression.
  bool isConst;  // To indicate a copy is needed for resize or other mutation.
} llElement;

// Stack of elements.
static llElement *llStack;
static uint32 llStackAllocated;
static uint32 llStackPos;
// This just has the elements marked as needsFree.  They are freed after
// evaluation of each statement expression.
static llElement *llNeedsFree;
static uint32 llNeedsFreeAllocated;
static uint32 llNeedsFreePos;
static uint32 llNumLocalsNeedingFree;

// Access functions.
static inline deDatatype llElementGetDatatype(llElement element) { return element.datatype; }
static char *llElementGetName(llElement element) { return utSymGetName(element.name); }
static inline bool llElementIsRef(llElement element) { return element.isRef; }
static inline bool llElementNeedsFree(llElement element) { return element.needsFree; }
static inline bool llElementIsNull(llElement element) { return element.isNull; }
static inline void llElementSetNeedsFree(llElement *element, bool value) {
  element->needsFree = value;
}
static inline llElement llMakeEmptyElement(void) {
  llElement element = {0,};
  return element;
}
static inline bool llElementIsDelegate(llElement element) { return element.isDelegate; }
static inline void llSetTopOfStackAsDelegate(void) {
  llElement *element = llStack + llStackPos - 1;
  element->isDelegate = true;
}

// Determine if |datatype| is a reference counted class.
static inline bool isRefCounted(deDatatype datatype) {
  return !deStatementGenerated(llCurrentStatement) &&
      deDatatypeGetType(datatype) == DE_TYPE_CLASS &&
      deTclassRefCounted(deClassGetTclass(deDatatypeGetClass(datatype)));
}

// Generate a location tag if in debug mode.
static char *locationInfo(void) {
  if (!llDebugMode) {
    return "";
  }
  llTag scopeTag = llBlockGetTag(llCurrentScopeBlock);
  llTag tag = llCreateLocationTag(scopeTag, llCurrentLine);
  return utSprintf(", !dbg !%u", llTagGetNum(tag));
}

// Print to llTmpValueBuf.
static char *llTmpPrintf(char *format, ...) {
  va_list ap;
  char buf[1];
  va_start(ap, format);
  uint32 len = vsnprintf(buf, 1, format, ap) + 1;
  va_end(ap);
  if (llTmpValuePos + len >= llTmpValueLen) {
    llTmpValueLen += (llTmpValueLen >> 1) + len;
    utResizeArray(llTmpValueBuffer, llTmpValueLen);
  }
  va_start(ap, format);
  vsnprintf(llTmpValueBuffer + llTmpValuePos, len, format, ap);
  va_end(ap);
  llTmpValuePos += len - 1;
  return llTmpValueBuffer;
}

// Determine if any class signatures are instantiated.
static bool classInstantiated(deClass theClass) {
  deSignature signature;
  deForeachClassSignature(theClass, signature) {
    if (deSignatureInstantiated(signature)) {
      return true;
    }
  } deEndClassSignature;
  return false;
}

// Call the object's ref function.
static void refObject(llElement element) {
  if (llElementIsNull(element)) {
    return;
  }
  utAssert(!llElementIsRef(element));
  deClass theClass = deDatatypeGetClass(llElementGetDatatype(element));
  if (!classInstantiated(theClass)) {
    return;
  }
  deBlock classBlock = deClassGetSubBlock(theClass);
  char *location = locationInfo();
  char* path = utSprintf("%s_ref", deGetBlockPath(classBlock, true));
  llPrintf("  call void @%s(%s %s)%s\n", llEscapeIdentifier(path),
      llGetTypeString(llElementGetDatatype(element), false),
      llElementGetName(element), location);
}

// Call the object's unref function.
static void unrefObject(llElement element) {
  if (llElementIsNull(element)) {
    return;
  }
  deClass theClass = deDatatypeGetClass(llElementGetDatatype(element));
  if (!classInstantiated(theClass)) {
    return;
  }
  deBlock classBlock = deClassGetSubBlock(theClass);
  char *location = locationInfo();
  char* path = utSprintf("%s_unref", deGetBlockPath(classBlock, true));
  llPrintf("  call void @%s(%s %s)%s\n", llEscapeIdentifier(path),
      llGetTypeString(llElementGetDatatype(element), false),
      llElementGetName(element), location);
}

// Create an element.
static inline llElement createElement(deDatatype datatype, char *name, bool isRef) {
  llElement element;
  element.datatype = datatype;
  element.name = utSymCreate(name);
  element.isRef = isRef;
  element.isDelegate = false;
  element.needsFree = false;
  element.isNull = false;
  element.isConst = false;
  return element;
}

// Forward declarations for recursion.
static  utSym  generateBlockStatements(deBlock block, utSym label);
static void generateExpression(deExpression expression);
static void generateModularExpression(deExpression expression, llElement modulusElement);

// Return the string "true" or "false" to represent a Boolean value.
static inline char *boolVal(bool value) {
  return value? "true" : "false";
}

// Return the type string of an element.  This only differs from the datatype
// in that if it is a reference, but not a string or array, a '*' is added.
static char *getElementTypeString(llElement element) {
  deDatatype datatype = llElementGetDatatype(element);
  char *typeString = llGetTypeString(datatype, true);
  if (!llElementIsRef(element) && !llDatatypePassedByReference(datatype)) {
    return typeString;
  }
  return utSprintf("%s*", typeString);
}

// Dump an element to stdout, for debugging.
void llDumpElement(llElement element) {
  printf("%s %s\n", getElementTypeString(element), llElementGetName(element));
}

// Dump the stack to stdout, for debugging.
void llDumpStack(void) {
  for (uint32 i = 0; i < llStackPos; i++) {
    llDumpElement(llStack[i]);
  }
}

// Write the global string buffer to llAsmFile, and reset the global string
// buffer.
static void flushStringBuffer(void) {
  // Print out the portion up to point where we need to insert llTmpValueBuffer.
  char *p = strstr(deStringVal, LL_TMPVARS_STRING);
  if (p != NULL) {
    *p = '\0';
    fputs(deStringVal, llAsmFile);
    fputs(llTmpValueBuffer, llAsmFile);
    p += sizeof(LL_TMPVARS_STRING) - 1;
    fputs(p, llAsmFile);
  } else {
    fputs(deStringVal, llAsmFile);
  }
  deResetString();
  llTmpValuePos = 0;
}

// Determine if a variable is local or global.
static inline bool isLocal(deVariable variable) {
  if (deVariableGetType(variable) == DE_VAR_PARAMETER) {
    return true;
  }
  deFunction function = deBlockGetOwningFunction(deVariableGetBlock(variable));
  deFunctionType type = deFunctionGetType(function);
  return type != DE_FUNC_MODULE && type != DE_FUNC_PACKAGE;
}

// Add the element to the list of elements needing to be freed.
static void addNeedsFreeElement(llElement element) {
  if (llNeedsFreePos == llNeedsFreeAllocated) {
    llNeedsFreeAllocated <<= 2;
    utResizeArray(llNeedsFree, llNeedsFreeAllocated);
  }
  llNeedsFree[llNeedsFreePos++] = element;
}

// Remove needsFree on an element.  This currently scans the stack of elements
// that need freeing.
static void removeNeedsFreeElement(llElement element) {
  for (uint32 i = 0; i < llNeedsFreePos; i++) {
    llElement otherElement = llNeedsFree[i];
    if (otherElement.name == element.name) {
      if (i + 1 < llNeedsFreePos) {
        // Pop the last element and write it here.
        llNeedsFree[i] = llNeedsFree[--llNeedsFreePos];
      } else {
        // Just pop it off the stack.
        llNeedsFreePos--;
      }
    }
  }
}

// Write the new local variable name as "%<num> =".  Increment llVarNum and return it.
static uint32 printNewValue(void) {
  llVarNum++;
  llPrintf("  %%%u = ", llVarNum);
  return llVarNum;
}

// Write the new local variable name as "%<num> =".  Increment llVarNum and return it.
static uint32 printNewTmpValue(void) {
  llTmpVarNum++;
  llTmpPrintf("  %%.tmp%u = ", llTmpVarNum);
  return llTmpVarNum;
}

// Create an element from a uint32 value.
static inline llElement createValueElement(deDatatype datatype, uint32 value, bool isRef) {
  char buf[20];  // Big enough for UINT32_MAX in decimal, plus a few chars.
  sprintf(buf, "%%%u", value);
  return createElement(datatype, buf, isRef);
}

// Create a temp element from a uint32 value.
static inline llElement createTmpValueElement(deDatatype datatype, uint32 value, bool isRef) {
  char buf[20];  // Big enough for UINT32_MAX in decimal, plus a few chars.
  sprintf(buf, "%%.tmp%u", value);
  return createElement(datatype, buf, isRef);
}

// Push an existing element onto the stack.
static inline llElement *pushElement(llElement element, bool needsFree) {
  llElementSetNeedsFree(&element, needsFree);
  if (llStackPos == llStackAllocated) {
    llStackAllocated <<= 2;
    utResizeArray(llStack, llStackAllocated);
  }
  llStack[llStackPos] = element;
  return llStack + llStackPos++;
}

// Push an element onto the stack.
static inline llElement *push(deDatatype datatype, char *name, bool isRef) {
  llElement element = createElement(datatype, name, isRef);
  return pushElement(element, false);
}

// Push a value numbered element.
static inline llElement *pushValue(deDatatype datatype, uint32 value, bool isRef) {
  char buf[20];  // Big enough for UINT32_MAX in decimal, plus a few chars.
  sprintf(buf, "%%%u", value);
  return push(datatype, buf, isRef);
}

// Push a temp value numbered element.
static inline llElement *pushTmpValue(deDatatype datatype, uint32 value, bool isRef) {
  char buf[24];  // Big enough for UINT32_MAX in decimal, plus a few chars.
  sprintf(buf, "%%.tmp%u", value);
  return push(datatype, buf, isRef);
}

// Deref even an array or tuple.  This needs to be used with care, since arrays
// cannot be copied or loaded without corrupting the heap.
static llElement derefAnyElement(llElement *element) {
  deDatatype datatype = llElementGetDatatype(*element);
  char *typeString = llGetTypeString(datatype, true);
  uint32 value = printNewValue();
  llPrintf("load %s, %s* %s\n", typeString, typeString, llElementGetName(*element));
  char *name = utSprintf("%%%u", value);
  element->name = utSymCreate(name);
  element->isRef = false;
  return *element;
}

// Dereference an element, if it is not already dereferenced.
static llElement derefElement(llElement *element) {
  deDatatype datatype = llElementGetDatatype(*element);
  if (!llElementIsRef(*element) || llDatatypePassedByReference(datatype)) {
    // Don't deref elements that are passed by reference.
    return *element;
  }
  return derefAnyElement(element);
}

// Unref the elements in the potentially multi-dimensional array.
static void unrefArrayElements(llElement element, deDatatype baseType) {
  deClass theClass = deDatatypeGetClass(baseType);
  deBlock classBlock = deClassGetSubBlock(theClass);
  uint32 depth = deArrayDatatypeGetDepth(llElementGetDatatype(element));
  uint32 refWidth = deClassGetRefWidth(theClass);
  char* path = utSprintf("%s_unref", deGetBlockPath(classBlock, true));

  uint32 unrefPointer = printNewValue();
  llPrintf(" bitcast void (i%u)* @%s to i8*\n", refWidth, path);
  llDeclareRuntimeFunction("runtime_foreachArrayObject");
  llPrintf("  call void @runtime_foreachArrayObject(%%struct.runtime_array* %s, "
      "i8* %%%u, i32 %u, i32 %u)%s\n",
      llElementGetName(element), unrefPointer, refWidth, depth, locationInfo());
}

// Call runtime_freeArray on the variable.
static void callFree(llElement element) {
  deDatatype datatype = llElementGetDatatype(element);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type == DE_TYPE_CLASS) {
    if (llElementIsRef(element)) {
      derefElement(&element);
    }
    unrefObject(element);
  } else {
    if (type == DE_TYPE_ARRAY) {
      deDatatype baseType = deArrayDatatypeGetBaseDatatype(datatype);
      if (isRefCounted(baseType)) {
        unrefArrayElements(element, baseType);
      }
    }
    llDeclareRuntimeFunction("runtime_freeArray");
    llPrintf("  call void @runtime_freeArray(%%struct.runtime_array* %s)\n", llElementGetName(element));
  }
}

// Index into a tuple.  For values passed by reference, or if |getRef| is true,
// return a pointer to the field of the tuple.
// element.
static llElement indexTuple(llElement tuple, uint32 index, bool getRef) {
  deDatatype datatype = llElementGetDatatype(tuple);
  deDatatype elementType = deDatatypeGetiTypeList(datatype, index);
  uint32 value = printNewValue();
  char *typeString = llGetTypeString(datatype, true);
  llPrintf("getelementptr inbounds %s, %s* %s, i32 0, i32 %u\n",
      typeString, typeString, llElementGetName(tuple), index);
  llElement element = createValueElement(elementType, value, true);
  if (!getRef && !llDatatypePassedByReference(datatype)) {
    derefElement(&element);
  }
  return element;
}

// Forward declaration for recursion.
static void freeElement(llElement element);

// Call free on sub-arrays of the tuple.
static void callFreeOnTupleArrays(llElement tuple) {
  deDatatype datatype = llElementGetDatatype(tuple);
  for (uint32 i = 0; i < deDatatypeGetNumTypeList(datatype); i++) {
    deDatatype subType = deDatatypeGetiTypeList(datatype, i);
    if (deDatatypeContainsArray(subType)) {
      llElement subElement = indexTuple(tuple, i, true);
      freeElement(subElement);
    }
  }
}

// Free the element.
static void freeElement(llElement element) {
  deDatatype datatype = llElementGetDatatype(element);
  if (deDatatypeGetType(datatype) != DE_TYPE_TUPLE &&
      deDatatypeGetType(datatype) != DE_TYPE_STRUCT) {
    callFree(element);
  } else {
    callFreeOnTupleArrays(element);
  }
}

// Free elements on the llNeedsFree list.
static inline void freeElements(bool freeLocals) {
  int32 start = freeLocals? 0 : llNumLocalsNeedingFree;
  for (int32 i = (int32)llNeedsFreePos - 1; i >= start ; i--) {
    freeElement(llNeedsFree[i]);
  }
  llNeedsFreePos = llNumLocalsNeedingFree;
}

// Free elements on the llNeedsFree list.
static inline void freeRecentElements(uint32 pos) {
  for (int32 i = (int32)llNeedsFreePos - 1; i >= (int32)pos ; i--) {
    freeElement(llNeedsFree[i]);
  }
  llNeedsFreePos = pos;
}

// Reset the llNeedsFree list.  This is only sensible when calling throw.
static void resetNeedsFreeList(void) {
  llNeedsFreePos = 0;
}

// Declare an array containing a CTTK integer constant.  Push an runtime_array
// pointing to it.
static void pushBigint(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  push(datatype, utSymGetName(llAddArrayConstant(expression)), true);
}

// Push an integer element onto the stack.
static inline void pushInteger(deExpression expression) {
  deBigint bigint = deExpressionGetBigint(expression);
  // Get width from datatype in case this integer was auto-cast to a different width.
  uint32 width = deDatatypeGetWidth(deExpressionGetDatatype(expression));
  if (width > llSizeWidth) {
    pushBigint(expression);
    return;
  }
  char *name = deBigintToString(bigint, 10);
  deDatatype datatype;
  if (deBigintSigned(bigint)) {
    datatype = deIntDatatypeCreate(width);
  } else {
    datatype = deUintDatatypeCreate(width);
  }
  push(datatype, name, false);
}

// I see the clang C compiler writing out 7 digits after the decimal point.
static bool floatCanPrintBase10(deFloat floatVal) {
  double value = deFloatGetValue(floatVal);
  if (value < 0.0) {
    value = -value;
  }
  // For some reason, LLVM rejects large exponents on float values.
  if (deFloatGetWidth(floatVal) == 32 && value > 1.0e10) {
    return false;
  }
  value *= 256.0;
  return trunc(value) == value;
}

// Return a string representation of the float.  LLVM writes 64-bit hex for
// float sizes up to 64-bit.  Larger floats will need more.
static char *floatToString(deFloat floatVal) {
  double f = deFloatGetValue(floatVal);
  if (floatCanPrintBase10(floatVal)) {
    return utSprintf("%e", f);
  }
  char *hexString = deBytesToHex(&f, sizeof(double), true);
  return utSprintf("0x%s", hexString);
}

// Push a float element onto the stack.
static inline void pushFloat(deExpression expression) {
  deFloat floatVal = deExpressionGetFloat(expression);
  uint32 width = deDatatypeGetWidth(deExpressionGetDatatype(expression));
  char *name = floatToString(floatVal);
  deDatatype datatype = deFloatDatatypeCreate(width);
  push(datatype, name, false);
}

// Push a small integer constant.
static inline llElement createSmallInteger(uint64 value, uint32 width, bool isSigned) {
  deDatatype datatype;
  char *name;
  if (isSigned) {
    name = utSprintf("%lld", value);
    datatype = deIntDatatypeCreate(width);
  } else {
    name = utSprintf("%llu", value);
    datatype = deUintDatatypeCreate(width);
  }
  return createElement(datatype, name, false);
}

// Push a small integer constant.
static inline void pushSmallInteger(uint64 value, uint32 width, bool isSigned) {
  pushElement(createSmallInteger(value, width, isSigned), false);
}

// Return the top stack element, without popping it.
static llElement *topOfStack(void) {
  if (llStackPos == 0) {
    utExit("Element stack underflow");
  }
  return llStack + llStackPos - 1;
}

// Pop an element off of the stack.
static llElement popElement(bool deref) {
  if (llStackPos == 0) {
    utExit("Element stack underflow");
  }
  llStackPos--;
  llElement element = llStack[llStackPos];
  if (llElementNeedsFree(element)) {
    addNeedsFreeElement(element);
  }
  if (deref) {
    return derefElement(&element);
  }
  return element;
}

// Return the variable's name.
char *llGetVariableName(deVariable variable) {
  if (isLocal(variable)) {
    return utSprintf("%%%s", llEscapeIdentifier(deVariableGetName(variable)));
  }
  deBlock block = deVariableGetBlock(variable);
  char *path = deGetBlockPath(block, true);
  if (*path == '\0') {
    return utSprintf("@%s", llEscapeIdentifier(deVariableGetName(variable)));
  }
  return utSprintf("@%s", llEscapeIdentifier(utSprintf(
      "%s_%s", path, deVariableGetName(variable))));
}

// Generate a call to the allocate function for the constructor.
static void generateCallToAllocateFunc(deBlock block, deSignature signature) {
  deClass theClass = deDatatypeGetClass(deSignatureGetReturnType(signature));
  deBlock classBlock = deClassGetSubBlock(theClass);
  char* path = utSprintf("%s_allocate", deGetBlockPath(classBlock, true));
  deVariable selfVar = deBlockGetFirstVariable(block);
  char *self = llGetVariableName(selfVar);
  char *selfType = llGetTypeString(deVariableGetDatatype(selfVar), false);
  char *location = locationInfo();
  llPrintf("  %s = call %s @%s()\n%s", self, selfType, llEscapeIdentifier(path), location);
  if (llDebugMode) {
    llDeclareLocalVariable(selfVar, 0);
  }
}

// Generate a call the free function for the destructor.
static void generateCallToFreeFunc() {
  deVariable selfVar = deBlockGetFirstVariable(llCurrentScopeBlock);
  deClass theClass = deDatatypeGetClass(deVariableGetDatatype(selfVar));
  uint32 refWidth = deClassGetRefWidth(theClass);
  deBlock classBlock = deClassGetSubBlock(theClass);
  char* path = llEscapeIdentifier(utSprintf("%s_free", deGetBlockPath(classBlock, true)));
  llPrintf("  call void @%s(i%u %s)%s\n", path, refWidth,
      llGetVariableName(selfVar), locationInfo());
}

// Return a default value string for the type.
static char *getDefaultValue(deDatatype datatype) {
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_NONE:
    case DE_TYPE_TCLASS:
    case DE_TYPE_FUNCTION:
      utExit("Invalid data type instantiated");
      break;
    case DE_TYPE_BOOL:
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_ENUMCLASS:
    case DE_TYPE_ENUM:
      if (llDatatypeIsBigint(datatype)) {
        return "zeroinitializer";
      }
      return "0";
    case DE_TYPE_CLASS:
    case DE_TYPE_NULL:
      return "-1";
    case DE_TYPE_STRING:
    case DE_TYPE_ARRAY:
    case DE_TYPE_TUPLE:
    case DE_TYPE_STRUCT:
    case DE_TYPE_MODINT:
    case DE_TYPE_FLOAT:
      return "zeroinitializer";
    case DE_TYPE_FUNCPTR:
      return "null";
  }
  return NULL;  // Dummy return.
}

// Initialize a local variable to a default value.
static void initializeLocalVariable(deVariable variable) {
  char *value = getDefaultValue(deVariableGetDatatype(variable));
  deDatatype datatype = deVariableGetDatatype(variable);
  char *typeString = llGetTypeString(datatype, true);
  char *varName = llGetVariableName(variable);
  llPrintf("  %s = alloca %s\n", varName, typeString);
  llPrintf("  store %s %s, %s* %s\n", typeString, value, typeString, varName);
  llVariableSetInitialized(variable, true);
  if (deDatatypeContainsArray(datatype)) {
    llElement element = createElement(datatype, varName, true);
    addNeedsFreeElement(element);
  } else if (!deVariableGenerated(variable) && isRefCounted(datatype)) {
    llElement element = createElement(datatype, varName, true);
    addNeedsFreeElement(element);
  }
  if (llDebugMode) {
     llDeclareLocalVariable(variable, 0);
  }
}

// Print the top of the main function.
static void printMainTop(void) {
  llDeclareRuntimeFunction("runtime_arrayStart");
  llDeclareRuntimeFunction("runtime_initArrayFromC");
  llDeclareRuntimeFunction("runtime_initArrayFromCUTF8");
  llVarNum += 2;  // For argc and argv.
  llPrevLabel = utSymCreate("2");
  llPrintf(
      "  call void @runtime_arrayStart()\n"
      "  call void @runtime_initArrayOfStringsFromCUTF8(%%struct.runtime_array* @argv, i8** %%1, i32 %%0)\n");
}

// Declare parameter values so they are visible in gdb.
static void declareArgumentValues(deBlock block, bool mustUseValue, bool skipSelf) {
  uint32 argNum = 1;
  deVariable variable = deBlockGetFirstVariable(block);
  if (skipSelf) {
    variable = deVariableGetNextBlockVariable(variable);
  }
  while (variable != deVariableNull && deVariableGetType(variable) == DE_VAR_PARAMETER) {
    if (deVariableGetType(variable) != DE_VAR_PARAMETER) {
      return;
    }
    if (deVariableInstantiated(variable) || mustUseValue) {
      if (llDebugMode) {
        llDeclareLocalVariable(variable, argNum);
      }
      argNum++;
    }
    variable = deVariableGetNextBlockVariable(variable);
  }
}

// For non-exported blocks, return "dso_local".  Otherwise, "internal".
static char *findBlockVisibility(deBlock block) {
  bool exported;
  utAssert(deBlockGetType(block) == DE_BLOCK_FUNCTION);
  exported = deFunctionExported(deBlockGetOwningFunction(block));
  return exported? "dso_local" : "internal";
}

// Write the function header.
static void printFunctionHeader(deBlock block, deSignature signature) {
  bool first = true;
  if (signature != deSignatureNull) {
    deDatatype returnType = deSignatureGetReturnType(signature);
    deDatatype retType = returnType;
    bool returnsValuePassedByReference = llDatatypePassedByReference(returnType);
    if (returnsValuePassedByReference) {
      // The first parameter will be a pointer to the returned value.
      retType = deNoneDatatypeCreate();
    }
    char *visibility = findBlockVisibility(block);
    llPrintf("\ndefine %s %s @%s(", visibility, llGetTypeString(retType, false),
        llEscapeIdentifier(llPath));
    if (returnsValuePassedByReference) {
      first = false;
      llPrintf("%s* %%.retVal", llGetTypeString(returnType, true));
    }
  } else {
    llPrintf("\ndefine dso_local i32 @main(i32, i8**");
  }
  // If a function has its address taken, then we can't drop unused parameters.
  bool mustUseValue = signature != deSignatureNull && deSignatureIsCalledByFuncptr(signature);
  uint32 numParams = 0;
  bool skipSelf = false;
  deVariable variable = deBlockGetFirstVariable(block);
  bool isConstructor = signature != deSignatureNull &&
      deFunctionGetType(deSignatureGetFunction(signature)) == DE_FUNC_CONSTRUCTOR;
  if (isConstructor) {
    // Don't declare the self parameter in constructors.  It will be declared
    // when calling the allocate function.
    variable = deVariableGetNextBlockVariable(variable);
    skipSelf = true;
  }
  while (variable != deVariableNull && deVariableGetType(variable) == DE_VAR_PARAMETER) {
    if (deVariableInstantiated(variable) || mustUseValue) {
      if (!first) {
        llPuts(", ");
      }
      first = false;
      char *suffix = "";
      deDatatype datatype = deVariableGetDatatype(variable);
      if (!deVariableConst(variable) || llDatatypePassedByReference(datatype)) {
        suffix = "*";  // Variable passed by reference.
      }
      llPrintf("%s%s %s", llGetTypeString(datatype, true), suffix,
          llGetVariableName(variable));
      llVariableSetInitialized(variable, true);
      numParams++;
    }
    variable = deVariableGetNextBlockVariable(variable);
  }
  llVarNum = 0;
  llTmpVarNum = 0;
  llPuts(")");
  llPrevLabel = utSymCreate("0");
  if (llDebugMode) {
    llTag tag = llBlockGetTag(block);
    llPrintf(" !dbg !%u", llTagGetNum(tag));
  }
  llPuts(" {\n");
  if (llDebugMode) {
    declareArgumentValues(block, mustUseValue, skipSelf);
  }
  if (isConstructor) {
    generateCallToAllocateFunc(block, signature);
  }
  // Instantiate local variables.
  llNeedsFreePos = 0;
  if (block != deRootGetBlock(deTheRoot)) {
    while (variable != deVariableNull) {
      if (deVariableInstantiated(variable) && isLocal(variable)) {
        initializeLocalVariable(variable);
      }
      variable = deVariableGetNextBlockVariable(variable);
    }
  }
  // Hack: generate a marker in the text buffer at this point so we know where
  // to insert temp buffer alloca instructions when printing this out.
  llPuts(LL_TMPVARS_STRING);
  llNumLocalsNeedingFree = llNeedsFreePos;
  if (signature == deSignatureNull) {
    printMainTop();
  }
}

// Generate code to push a reference to the ident.
static void generateIdentExpression(deExpression expression) {
  deIdent ident = deExpressionGetIdent(expression);
  utAssert(ident != deIdentNull);
  deIdentType type = deIdentGetType(ident);
  switch (type) {
    case DE_IDENT_VARIABLE: {
      deVariable variable = deIdentGetVariable(ident);
      char *name;
       bool isRef;
      if (deFunctionGetType(deBlockGetOwningFunction(deVariableGetBlock(variable))) != DE_FUNC_ENUM) {
         name = llGetVariableName(variable);
        isRef = deVariableGetType(variable) != DE_VAR_PARAMETER || !deVariableConst(variable) ||
           llDatatypePassedByReference(deVariableGetDatatype(variable));
      } else {
        name = utSprintf("%u", deVariableGetEntryValue(variable));
        isRef = false;
      }
      push(deVariableGetDatatype(variable), name, isRef);
      break;
    }
    case DE_IDENT_FUNCTION: {
      // Push an element pointing to the function.
      deFunction function = deIdentGetFunction(ident);
      deDatatype datatype = deFunctionDatatypeCreate(function);
      push(datatype, deIdentGetName(ident), false);
      break;
    case DE_IDENT_UNDEFINED:
      utExit("Tried to generate an undefined identifier");
      break;
    }
  }
}

// Allocate a temporary array and return its element, leaving it on the stack.
static llElement allocateTempArray(deDatatype datatype) {
  uint32 value = printNewTmpValue();
  llTmpPrintf("alloca %%struct.runtime_array\n");
  llTmpPrintf("  store %%struct.runtime_array zeroinitializer, %%struct.runtime_array* %%.tmp%u\n", value);
  llElement *result = pushTmpValue(datatype, value, true);
  llElementSetNeedsFree(result, true);
  return *result;
}

// Allocate a temporary value and return its element, leaving it on the stack.
static llElement allocateTempValue(deDatatype datatype) {
  uint32 value = printNewTmpValue();
  char *typeString = llGetTypeString(datatype, true);
  llTmpPrintf("alloca %s\n", typeString);
  llTmpPrintf("  store %s zeroinitializer, %s* %%.tmp%u\n", typeString, typeString, value);
  llElement *result = pushTmpValue(datatype, value, true);
  if (deDatatypeContainsArray(datatype)) {
    llElementSetNeedsFree(result, true);
  }
  return *result;
}

// Return the element type of the array.  For strings, return the uint8 type,
// since strings are represented as arrays of uint8.  For bigints, return
// uint32, since CTTK bigints are arrays of uint32.
static deDatatype getElementType(deDatatype arrayDatatype) {
  deDatatypeType type = deDatatypeGetType(arrayDatatype);
  if (type == DE_TYPE_STRING) {
    return deUintDatatypeCreate(8);
  } else if (llDatatypeIsBigint(arrayDatatype)) {
    return deUintDatatypeCreate(32);
  }
  utAssert(type == DE_TYPE_ARRAY);
  return deDatatypeGetElementType(arrayDatatype);
}

// Load the array.data pointer, and cast it to a |datatype| pointer.
static llElement loadArrayDataPointer(llElement array) {
  deDatatype elementDatatype = getElementType(llElementGetDatatype(array));
  uint32 dataPtrAddress = printNewValue();
  llPrintf(
      "getelementptr inbounds %%struct.runtime_array, %%struct.runtime_array* %s, i32 0, i32 0\n",
      llElementGetName(array));
  uint32 dataPtr = printNewValue();
  llPrintf("load i%s*, i%s** %%%u%s\n", llSize, llSize, dataPtrAddress, locationInfo());
  char *type = llGetTypeString(elementDatatype, true);
  uint32 castDataPtr = printNewValue();
  llPrintf("bitcast i%s* %%%u to %s*\n", llSize, dataPtr, type);
  return createValueElement(elementDatatype, castDataPtr, true);
}

// Return the runtime function name that can execute this smallnum expression.
static char *findSmallnumFunction(deExpression expression) {
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_MUL: return "runtime_smallnumMul";
    case DE_EXPR_DIV: return "runtime_smallnumDiv";
    case DE_EXPR_MOD: return "runtime_smallnumMod";
    case DE_EXPR_EXP: return "runtime_smallnumExp";
    default:
      utExit("Unsupported expression type for bigints");
  }
  return NULL;  // Dummy return.
}

// Return the function name for emulating the small modular operation.
static char *findSmallnumModularFunctionName(deExpression expression) {
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_ADD: return "runtime_smallnumModularAdd";
    case DE_EXPR_SUB: return "runtime_smallnumModularSub";
    case DE_EXPR_MUL: return "runtime_smallnumModularMul";
    case DE_EXPR_DIV: return "runtime_smallnumModularDiv";
    case DE_EXPR_EXP: return "runtime_smallnumModularExp";
    case DE_EXPR_NEGATE: return "runtime_smallnumModularNegate";
    default:
      utExit("Unexpected small modular expression type");
  }
  return NULL; // Dummy return.
}

// Return the function name for emulating the bigint modular operation.
static char *findBigintModularFunctionName(deExpression expression) {
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_ADD: return "runtime_bigintModularAdd";
    case DE_EXPR_SUB: return "runtime_bigintModularSub";
    case DE_EXPR_MUL: return "runtime_bigintModularMul";
    case DE_EXPR_DIV: return "runtime_bigintModularDiv";
    case DE_EXPR_EXP: return "runtime_bigintModularExp";
    case DE_EXPR_NEGATE: return "runtime_bigintModularNegate";
    default:
      utExit("Unexpected small modular expression type");
  }
  return NULL; // Dummy return.
}

// Return the runtime function name that can execute this expression.
static char *findExpressionFunction(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (deDatatypeGetType(datatype) == DE_TYPE_MODINT) {
    if (llDatatypeIsBigint(datatype)) {
      return findBigintModularFunctionName(expression);
    }
    return findSmallnumModularFunctionName(expression);
  }
  if (!llDatatypeIsBigint(datatype)) {
    return findSmallnumFunction(expression);
  }
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_ADD: return "runtime_bigintAdd";
    case DE_EXPR_ADDTRUNC: return deUnsafeMode? "runtime_bigintAdd" : "runtime_bigintAddTrunc";
    case DE_EXPR_SUB: return "runtime_bigintSub";
    case DE_EXPR_SUBTRUNC: return deUnsafeMode? "runtime_bigintSub" : "runtime_bigintSubTrunc";
    case DE_EXPR_MUL: return "runtime_bigintMul";
    case DE_EXPR_MULTRUNC: return deUnsafeMode? "runtime_bigintMul" : "runtime_bigintMulTrunc";
    case DE_EXPR_DIV: return "runtime_bigintDiv";
    case DE_EXPR_MOD: return "runtime_bigintMod";
    case DE_EXPR_EXP: return "runtime_bigintExp";
    case DE_EXPR_NEGATE: return "runtime_bigintNeg";
    case DE_EXPR_NEGATETRUNC: return deUnsafeMode? "runtime_bigintNeg" : "runtime_bigintNegTrunc";
    case DE_EXPR_BITNOT: return "runtime_bigintNot";
    case DE_EXPR_SHR: return "runtime_bigintShr";
    case DE_EXPR_SHL: return "runtime_bigintShl";
    case DE_EXPR_ROTL: return "runtime_bigintRotl";
    case DE_EXPR_ROTR: return "runtime_bigintRotr";
    case DE_EXPR_BITAND: return "runtime_bigintBitwiseAnd";
    case DE_EXPR_BITOR: return "runtime_bigintBitwiseOr";
    case DE_EXPR_BITXOR: return "runtime_bigintBitwiseXor";
    case DE_EXPR_EQUAL: return "runtime_bigintEq";
    case DE_EXPR_GE: return "runtime_bigintGeq";
    case DE_EXPR_GT: return "runtime_bigintGt";
    case DE_EXPR_LE: return "runtime_bigintLeq";
    case DE_EXPR_LT: return "runtime_bigintLt";
    case DE_EXPR_NOTEQUAL: return "runtime_bigintNeq";
    case DE_EXPR_AND: return "runtime_boolAnd";
    case DE_EXPR_OR: return "runtime_boolOr";
    case DE_EXPR_XOR: return "runtime_bigintXor";
    case DE_EXPR_NOT: return "runtime_boolNot";
    default:
      utExit("Unsupported expression type for bigints");
  }
  return NULL;  // Dummy return.
}

// Find the bigint relational expression comparison type.
static runtime_comparisonType findBigintComparisonType(deExpression expression) {
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_LT:
      return RN_LT;
    case DE_EXPR_LE:
      return RN_LE;
    case DE_EXPR_GT:
      return RN_GT;
    case DE_EXPR_GE:
      return RN_GE;
    case DE_EXPR_EQUAL:
      return RN_EQUAL;
    case DE_EXPR_NOTEQUAL:
      return RN_NOTEQUAL;
    default:
      utExit("Unexpected array comparison type");
  }
  return RN_LT;  // Dummy return;
}

// Generate code for a binary expression.
static void generateBigintBinaryExpression(deExpression expression) {
  char *funcName = findExpressionFunction(expression);
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  generateExpression(left);
  llElement leftElement = popElement(true);
  generateExpression(right);
  llElement rightElement = popElement(true);
  llElement destArray = allocateTempValue(deExpressionGetDatatype(expression));
  llDeclareRuntimeFunction(funcName);
  llPrintf(
       "  call void @%s(%%struct.runtime_array* %s, %%struct.runtime_array* %s, %%struct.runtime_array* %s)%s\n",
      funcName, llElementGetName(destArray), llElementGetName(leftElement),
      llElementGetName(rightElement), locationInfo());
}

// Find the size of a tuple using LLVM's pointer arithmetic.  This generates
// LLVM statements, so don't call this in the middle of generating one.
static llElement findTupleSize(deDatatype datatype) {
  char *type = llGetTypeString(datatype, true);
  uint32 tmpPtr = printNewValue();
  llPrintf("getelementptr %s, %s* null, i32 1\n", type, type);
  uint32 size = printNewValue();
  llPrintf("ptrtoint %s* %%%u to i%s\n", type, tmpPtr, llSize);
  return createValueElement(llSizeType, size, false);
}

// Return the size element value of the datatype: NOT the size itself.
// NOTE: This must be called stand-alone, not as an argument to fprintf, because
// we have to instantiate a couple of lines of LLVM assembly to find the size of
// a structure.
static llElement findDatatypeSize(deDatatype datatype) {
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_NONE:
    case DE_TYPE_TCLASS:
    case DE_TYPE_FUNCTION:
    case DE_TYPE_ENUMCLASS:
      utExit("Type has no size");
      break;
    case DE_TYPE_CLASS:
    case DE_TYPE_NULL: {
      uint32 refWidth = deClassGetRefWidth(deDatatypeGetClass(datatype));
      uint32 refSize;
      if (refWidth <= 8) {
         refSize = 1;
      } else if (refWidth <= 16) {
         refSize = 2;
      } else if (refWidth <= 32) {
         refSize = 4;
      } else {
         refSize = 8;
      }
      return createSmallInteger(refSize, llSizeWidth, false);
    }
    case DE_TYPE_FUNCPTR:
      return createSmallInteger(sizeof(void*), llSizeWidth, false);
    case DE_TYPE_BOOL:
      return createSmallInteger(sizeof(uint8), llSizeWidth, false);
    case DE_TYPE_STRING:
    case DE_TYPE_ARRAY:
      return createSmallInteger(sizeof(runtime_array), llSizeWidth, false);
    case DE_TYPE_FLOAT:
      return createSmallInteger(deDatatypeGetWidth(datatype) >> 3, llSizeWidth, false);
    case DE_TYPE_MODINT:
    case DE_TYPE_UINT:
    case DE_TYPE_INT: {
      uint32_t width = deDatatypeGetWidth(datatype);
      if (deDatatypeGetWidth(datatype) > sizeof(size_t) << 3) {
        return createSmallInteger(sizeof(runtime_array), llSizeWidth, false);
      } else if (width > 32) {
        return createSmallInteger(sizeof(uint64_t), llSizeWidth, false);
      } else if (width > 16) {
        return createSmallInteger(sizeof(uint32_t), llSizeWidth, false);
      } else if (width > 8) {
        return createSmallInteger(sizeof(uint16_t), llSizeWidth, false);
      }
      return createSmallInteger(sizeof(uint8_t), llSizeWidth, false);
    }
    case DE_TYPE_ENUM: {
      deBlock enumBlock = deFunctionGetSubBlock(deDatatypeGetFunction(datatype));
      deDatatype elementType = deVariableGetDatatype(deBlockGetFirstVariable(enumBlock));
      return findDatatypeSize(deUintDatatypeCreate(deDatatypeGetWidth(elementType)));
    }
    case DE_TYPE_TUPLE:
      return findTupleSize(datatype);
    case DE_TYPE_STRUCT:
      return findTupleSize(deGetStructTupleDatatype(datatype));
  }
  utExit("Unknown datatype type");
  return llMakeEmptyElement();  // Dummy return;
}

// Copy the array, which can be an array or tuple..
static void copyArray(llElement access, llElement value, bool freeDest) {
  if (freeDest) {
    callFree(access);
  }
  if (!strcmp(llElementGetName(value), "zeroinitializer")) {
    // The arrayof expression returns zeroinitializer.
    callFree(access);
    return;
  }
  deDatatype datatype = llElementGetDatatype(value);
  deDatatypeType type = deDatatypeGetType(datatype);
  llElement sizeValue;
  bool hasSubArrays = false;
  if (type == DE_TYPE_STRING) {
    sizeValue = createSmallInteger(sizeof(uint8), llSizeWidth, false);
  } else if (type == DE_TYPE_UINT || type == DE_TYPE_INT) {
    // CTTK uses arrays of 32-bit ints to represent bigints.
    sizeValue = createSmallInteger(sizeof(uint32), llSizeWidth, false);
  } else {
    utAssert(type == DE_TYPE_ARRAY);
    deDatatype elementDatatype = deDatatypeGetElementType(datatype);
    sizeValue = findDatatypeSize(elementDatatype);
    hasSubArrays = deDatatypeGetType(elementDatatype) == DE_TYPE_ARRAY;
  }
  llDeclareRuntimeFunction("runtime_copyArray");
  char *location = locationInfo();
  llPrintf(
      "  call void @runtime_copyArray(%%struct.runtime_array* %s, %%struct.runtime_array* %s, i%s %s, "
      "i1 zeroext %s)%s\n",
      llElementGetName(access), llElementGetName(value), llSize, llElementGetName(sizeValue),
      boolVal(hasSubArrays), location);
}

// Generate concatenation of a string or array.
static void generateConcat(llElement left, llElement right) {
  deDatatype datatype = llElementGetDatatype(left);
  deDatatype elementDatatype = deDatatypeGetElementType(datatype);
  llElement sizeValue = findDatatypeSize(elementDatatype);
  llDeclareRuntimeFunction("runtime_concatArrays");
  char *location = locationInfo();
  allocateTempArray(datatype);
  llElement *destArray = topOfStack();
  copyArray(*destArray, left, false);
  bool hasSubArrays = llDatatypeIsArray(elementDatatype);
  llPrintf(
      "  call void @runtime_concatArrays(%%struct.runtime_array* %s, %%struct.runtime_array* %s, "
      "i%s %s, i1 zeroext %s)%s\n", llElementGetName(*destArray), llElementGetName(right),
      llSize, llElementGetName(sizeValue), boolVal(hasSubArrays), location);
}

// Generate a concatenate expression.
static void generateConcatExpression(deExpression expression) {
  deExpression leftExpr = deExpressionGetFirstExpression(expression);
  deExpression rightExpr = deExpressionGetNextExpression(leftExpr);
  generateExpression(leftExpr);
  llElement left = popElement(false);
  generateExpression(rightExpr);
  llElement right = popElement(false);
  generateConcat(left, right);
}

// Generate a concatenate expression.
static void generateXorStringsExpression(llElement left, llElement right) {
  deDatatype datatype = deStringDatatypeCreate();
  char *location = locationInfo();
  llElement dest = allocateTempArray(datatype);
  llDeclareRuntimeFunction("runtime_xorStrings");
  llPrintf("  call void @runtime_xorStrings(%%struct.runtime_array* %s, %%struct.runtime_array* %s, "
      "%%struct.runtime_array* %s)%s\n", llElementGetName(dest), llElementGetName(left),
      llElementGetName(right), location);
}

// Find the last parameter variable.
static deVariable findLastParamVar(deBlock block) {
  deVariable var;
  deVariable prevVar = deVariableNull;
  deForeachBlockVariable(block, var) {
    if (deVariableGetType(var) != DE_VAR_PARAMETER) {
      return prevVar;
    }
    prevVar = var;
  } deEndBlockVariable;
  return prevVar;
}

// Return the number of positional (i.e. unnamed) parameters.  Also return the
// first named parameter.
static uint32 countPositionalParams(deExpression parameters,
    deExpression *firstNamedParameter) {
  *firstNamedParameter = deExpressionNull;
  uint32 numParameters = 0;
  deExpression parameter;
  deForeachExpressionExpression(parameters, parameter) {
    if (deExpressionGetType(parameter) == DE_EXPR_NAMEDPARAM) {
      *firstNamedParameter = parameter;
      return numParameters;
    }
    numParameters++;
  } deEndExpressionExpression;
  return numParameters;
}

// Count the number of parameter variables on the block.
static uint32 countBlockParamVars(deBlock block) {
  uint32 numParamVars = 0;
  deVariable var;
  deForeachBlockVariable(block, var) {
    if (deVariableGetType(var) != DE_VAR_PARAMETER) {
      return numParamVars;
    }
    numParamVars++;
  } deEndBlockVariable;
  return numParamVars;
}

// Evaluate parameters in reverse order.
static void evaluateParameters(deSignature signature, deDatatype datatype,
    deExpression parameters, bool isMethodCall) {
  deExpression parameter;
  deExpression firstNamedParameter;
  deBlock block;
  bool isStruct = datatype != deDatatypeNull && deDatatypeGetType(datatype) == DE_TYPE_STRUCT;
  if (isStruct) {
    // We're creating a structure.
    block = deFunctionGetSubBlock(deDatatypeGetFunction(datatype));
  } else {
    block = deSignatureGetBlock(signature);
  }
  uint32 numParamVars = countBlockParamVars(block);
  uint32 numParams = countPositionalParams(parameters, &firstNamedParameter);
  deVariable paramVar = findLastParamVar(block);
  uint32 effectiveNumParamVars = numParamVars;
  if (signature != deSignatureNull) {
    if (deFunctionGetType(deSignatureGetFunction(signature)) == DE_FUNC_CONSTRUCTOR) {
      // Skip the self parameter: in constructors, we instantiate it as a local variable.
      effectiveNumParamVars--;
    } else if (isMethodCall) {
      numParams++;  // The self parameter is pushed by the access expression.
    }
  }
  if (effectiveNumParamVars > numParams) {
    // We first evaluate default parameters needed, in reverse order.
    for (int32 xParam = effectiveNumParamVars - 1; xParam >= (int32)numParams; xParam--) {
      if (signature == deSignatureNull || deSignatureParamInstantiated(signature, xParam)) {
        utSym name = deVariableGetSym(paramVar);
        deExpression namedParameter = deFindNamedParameter(firstNamedParameter, name);
        if (namedParameter != deExpressionNull) {
          generateExpression(deExpressionGetLastExpression(namedParameter));
        } else {
          deExpression defaultValue = deVariableGetInitializerExpression(paramVar);
          generateExpression(defaultValue);
        }
      }
      paramVar = deVariableGetPrevBlockVariable(paramVar);
    }
  }
  uint32 xParam = numParamVars;
  if (firstNamedParameter != deExpressionNull) {
    parameter = deExpressionGetPrevExpression(firstNamedParameter);
  } else {
    parameter = deExpressionGetLastExpression(parameters);
  }
  while (parameter != deExpressionNull) {
    xParam--;
    if (!deExpressionIsType(parameter)) {
      // Always evaluate parameters if they can be, in case there are side-effects.
      generateExpression(parameter);
      if (signature == deSignatureNull || deSignatureParamInstantiated(signature, xParam)) {
        llElement *elementPtr = topOfStack();
        if (deVariableConst(paramVar) || isStruct) {
          derefElement(elementPtr);
        } else {
          // This parameter is passed by reference, not value.
          utAssert(llElementIsRef(*elementPtr));
        }
      } else {
        // Not using the parameter result.
        popElement(false);
      }
    }
    paramVar = deVariableGetPrevBlockVariable(paramVar);
    parameter = deExpressionGetPrevExpression(parameter);
  }
}

// Evaluate parameters in reverse order.
static void evaluateIndirectCallParameters(deExpression parameters) {
  deExpression parameter;
  for (parameter = deExpressionGetLastExpression(parameters);
       parameter != deExpressionNull;
       parameter = deExpressionGetPrevExpression(parameter)) {
    generateExpression(parameter);
    llElement *elementPtr = topOfStack();
    derefElement(elementPtr);
  }
}

// Generate a call to an overloaded operator function.
static void generateOperatorOverloadCall(deExpression expression, deSignature signature) {
  deDatatype returnType = deExpressionGetDatatype(expression);
  llElement returnElement;
  uint32 savedStackPos = llStackPos;
  evaluateParameters(signature, deDatatypeNull, expression, false);
  bool returnsValuePassedByReference = llDatatypePassedByReference(returnType);
  if (returnsValuePassedByReference) {
    returnElement = allocateTempValue(returnType);
    returnType = deNoneDatatypeCreate();
  }
  bool returnsVal = deDatatypeGetType(returnType) != DE_TYPE_NONE;
  uint32 retVal = 0;
  if (returnsVal) {
    retVal = printNewValue();
  } else {
    llPuts("  ");
  }
  char *path = llEscapeIdentifier(deGetSignaturePath(signature));
  llPrintf("call %s @%s(", llGetTypeString(returnType, false), path);
  bool firstTime = true;
  while (llStackPos > savedStackPos) {
    if (!firstTime) {
      llPuts(", ");
    }
    firstTime = false;
    llElement element = popElement(false);
    llPrintf("%s %s", getElementTypeString(element),
            llElementGetName(element));
  }
  llPrintf(")%s\n", locationInfo());
  if (returnsVal) {
    pushValue(returnType, retVal, false);
  } else if (returnsValuePassedByReference) {
    pushElement(returnElement, false);
  }
}

// Generate code for a binary expression.
static void generateBinaryExpression(deExpression expression, char *op) {
  deSignature signature = deExpressionGetSignature(expression);
  if (signature != deSignatureNull) {
    generateOperatorOverloadCall(expression, signature);
    return;
  }
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (llDatatypeIsBigint(datatype)) {
    generateBigintBinaryExpression(expression);
    return;
  }
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDatatypeType exprDatatype = deDatatypeGetType(datatype);
  generateExpression(left);
  llElement leftElement = popElement(true);
  generateExpression(right);
  llElement rightElement = popElement(true);
  deExpressionType exprType = deExpressionGetType(expression);
  if (exprDatatype == DE_TYPE_STRING && exprType == DE_EXPR_BITXOR) {
    generateXorStringsExpression(leftElement, rightElement);
    return;
  }
  char *type = llGetTypeString(datatype, false);
  uint32 value = printNewValue();
  llPrintf("%s %s %s, %s%s\n", op, type,
      llElementGetName(leftElement), llElementGetName(rightElement),
      locationInfo());
  pushValue(datatype, value, false);
}

// Return the LLVM name for the trucating expression.
char *findTruncatingOpName(deExpression expression) {
  deDatatypeType type = deDatatypeGetType(deExpressionGetDatatype(expression));
  bool isSigned = type == DE_TYPE_INT;
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_ADD: return isSigned? "sadd" : "uadd";
    case DE_EXPR_SUB: return isSigned? "ssub" : "usub";
    case DE_EXPR_MUL: return isSigned? "smul" : "umul";
    case DE_EXPR_NEGATE: return isSigned? "ssub" : "usub";
    default:
      utExit("Unexpected binary truncating operator");
  }
  return NULL;  // Dummy return;
}

// Create a new label name.
static utSym newLabel(char *name) {
  utSym sym = utSymCreateFormatted("%s%u", name, llLabelNum);
  llLabelNum++;
  return sym;
}

// Print the label, if it exists.
static void printLabel(utSym label) {
  if (label != utSymNull) {
    llPrintf("%s:\n", utSymGetName(label));
    llPrevLabel = label;
  }
  freeElements(false);
}

// Generate code for a binary expression which can throw an overflow exception.
static void generateBinaryExpressionWithOverflow(deExpression expression) {
  deSignature signature = deExpressionGetSignature(expression);
  if (signature != deSignatureNull) {
    generateOperatorOverloadCall(expression, signature);
    return;
  }
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (llDatatypeIsBigint(datatype)) {
    generateBigintBinaryExpression(expression);
    return;
  }
  deDatatypeType exprDatatype = deDatatypeGetType(datatype);
  utAssert(exprDatatype == DE_TYPE_INT || exprDatatype == DE_TYPE_UINT);
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  generateExpression(left);
  llElement leftElement = popElement(true);
  generateExpression(right);
  llElement rightElement = popElement(true);
  uint32 structValue = printNewValue();
  uint32_t width = deDatatypeGetWidth(datatype);
  char *opType = findTruncatingOpName(expression);
  llDeclareOverloadedFunction(utSprintf(
      "declare {i%u, i1} @llvm.%s.with.overflow.i%u(i%u, i%u)\n",
      width, opType, width, width, width));
  llPrintf("call {i%u, i1} @llvm.%s.with.overflow.i%u(i%u %s, i%u %s)%s\n",
      width, opType, width, width, llElementGetName(leftElement),
      width, llElementGetName(rightElement), locationInfo());
  uint32 resValue = printNewValue();
  llPrintf("extractvalue {i%u, i1} %%%u, 0\n", width, structValue);
  uint32 overflowValue = printNewValue();
  llPrintf("extractvalue {i%u, i1} %%%u, 1\n", width, structValue);
  utSym passed = newLabel("overflowCheckPassed");
  utSym failed = newLabel("overflowCheckFailed");
  llPrintf("  br i1 %%%u, label %%%s, label %%%s\n",
      overflowValue, utSymGetName(failed), utSymGetName(passed));
  printLabel(failed);
  llDeclareRuntimeFunction("runtime_throwOverflow");
  llPrintf("  call void @runtime_throwOverflow()\n  unreachable\n");
  printLabel(passed);
  pushValue(datatype, resValue, false);
}

// Determine if the array has sub-arrays.
static bool arrayHasSubArrays(deDatatype datatype) {
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type != DE_TYPE_ARRAY) {
    return false;
  }
  deDatatype elementType = deDatatypeGetElementType(datatype);
  return llDatatypeIsArray(elementType);;
}

// Return the runtime_type corresponding to the deDatatypeType.
static runtime_type findRuntimeType(deDatatypeType type) {
  switch (type) {
    case DE_TYPE_BOOL:
    case DE_TYPE_TCLASS:
    case DE_TYPE_CLASS:
    case DE_TYPE_UINT:
    case DE_TYPE_STRING:
      return RN_UINT;
    case DE_TYPE_INT:
      return RN_INT;
    case DE_TYPE_FUNCPTR:
    default:
      utExit("Unexpected type");
  }
  return RN_UINT;  // Dummy return;
}

// Return the primitive datatype of a multi-dimensional array.
static deDatatype findPrimitiveDatatype(deDatatype datatype) {
  while (deDatatypeGetType(datatype) == DE_TYPE_ARRAY) {
    datatype = deDatatypeGetElementType(datatype);
  }
  if (deDatatypeGetType(datatype) == DE_TYPE_STRING) {
    return deUintDatatypeCreate(8);
  }
  return datatype;
}

// Generate a bigint comparison.
static void generateBigintComparison(llElement left, llElement right,
    runtime_comparisonType compareType) {
  llDeclareRuntimeFunction("runtime_compareBigints");
  uint32 value = printNewValue();
  llPrintf(
      "call zeroext i1 @runtime_compareBigints(i32 %u, %%struct.runtime_array* %s, "
      "%%struct.runtime_array* %s)%s\n",
      compareType, llElementGetName(left), llElementGetName(right),
      locationInfo());
  pushValue(deBoolDatatypeCreate(), value, false);
}

// Generate code to compare two arrays.
static void generateArrayComparison(llElement left, llElement right, runtime_comparisonType compareType) {
  deDatatype datatype = llElementGetDatatype(left);
  if (deDatatypeIsInteger(datatype)) {
    generateBigintComparison(left, right, compareType);
    return;
  }
  deDatatype primDatatype = findPrimitiveDatatype(datatype);
  bool hasSubArrays = arrayHasSubArrays(datatype);
  runtime_type primType = findRuntimeType(deDatatypeGetType(primDatatype));
  llElement elementSize = findDatatypeSize(primDatatype);
  llDeclareRuntimeFunction("runtime_compareArrays");
  bool secret = deDatatypeSecret(datatype) || deDatatypeSecret(llElementGetDatatype(right));
  uint32 value = printNewValue();
  llPrintf(
      "call i1 @runtime_compareArrays(i32 %u, i32 %u, "
      "%%struct.runtime_array* %s, %%struct.runtime_array* %s, i%s %s, i1 zeroext %s, i1 zeroext %s)\n",
      compareType, primType, llElementGetName(left), llElementGetName(right),
      llSize, llElementGetName(elementSize), boolVal(hasSubArrays), boolVal(secret));
  pushValue(deBoolDatatypeCreate(), value, false);
}

// Generate code to compare two arrays.
static void generateArrayRelationalExpression(deExpression expression,
    runtime_comparisonType compareType) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  generateExpression(left);
  llElement leftElement = popElement(true);
  generateExpression(right);
  llElement rightElement = popElement(true);
  generateArrayComparison(leftElement, rightElement, compareType);
}

// Determine if the expression is comparing arrays.
static bool isArrayComparison(deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  return llDatatypeIsArray(deExpressionGetDatatype(left));
}

// Generate code for a relational expression.
static void generateComparison(llElement left, llElement right, char *op) {
  deDatatype retDatatype = deBoolDatatypeCreate();
  char *typeString = llGetTypeString(llElementGetDatatype(left), false);
  uint32 value = printNewValue();
  llPrintf("%s %s %s, %s%s\n", op, typeString,
      llElementGetName(left), llElementGetName(right), locationInfo());
  pushValue(retDatatype, value, false);
}

// Generate code for a binary expression.
static void generateRelationalExpression(deExpression expression, char *op) {
  deSignature signature = deExpressionGetSignature(expression);
  if (signature != deSignatureNull) {
    generateOperatorOverloadCall(expression, signature);
    return;
  }
  if (isArrayComparison(expression)) {
    generateArrayRelationalExpression(expression, findBigintComparisonType(expression));
    return;
  }
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  generateExpression(left);
  llElement leftElement = popElement(true);
  generateExpression(right);
  llElement rightElement = popElement(true);
  generateComparison(leftElement, rightElement, op);
}

// Cast the reference to the element to a uint*.  Return the new value.
static uint32 getUintPointer(llElement element, uint32 width) {
  utAssert(llElementIsRef(element));
  char *type = llGetTypeString(llElementGetDatatype(element), true);
  uint32 value = printNewValue();
  llPrintf("bitcast %s* %s to i%u*\n", type, llElementGetName(element), width);
  return value;
}

// Store the element and return a reference to it.
static llElement storeElementAndReturnRef(llElement element) {
  char *type = llGetTypeString(llElementGetDatatype(element), true);
  uint32 value = printNewTmpValue();
  llTmpPrintf("alloca %s\n", type);
  llTmpPrintf("  store %s %s, %s* %%.tmp%u\n",
      type, llElementGetName(element), type, value);
  return createTmpValueElement(llElementGetDatatype(element), value, true);
}

// Forward declaration for recursion.
static llElement resizeSmallInteger(llElement element, uint32 newWidth,
    bool isSigned, bool truncate);

// Check that truncation won't change the value of the integer.  Thrown an
// overflow exception if the value would change.
static void checkTruncation(llElement element, llElement result,
    uint32 oldWidth, uint32 newWidth, bool isSigned) {
  llElement checkValue = resizeSmallInteger(result, oldWidth, isSigned, false);
  generateComparison(element, checkValue, "icmp eq");
  llElement condition = popElement(true);
  utSym passedLabel = newLabel("truncCheckPassed");
  utSym failedLabel = newLabel("truncCheckFailed");
  llPrintf("  br i1 %s, label %%%s, label %%%s\n",
      llElementGetName(condition), utSymGetName(passedLabel), utSymGetName(failedLabel));
  printLabel(failedLabel);
  llDeclareRuntimeFunction("runtime_throwOverflow");
  llPuts("  call void @runtime_throwOverflow()\n  unreachable\n");
  printLabel(passedLabel);
}

// Resize a small integer.
static llElement resizeSmallInteger(llElement element, uint32 newWidth,
    bool isSigned, bool truncate) {
  deDatatype oldDatatype = llElementGetDatatype(element);
  uint32 oldWidth = deDatatypeGetWidth(oldDatatype);
  deDatatype newDatatype = deDatatypeSetSigned(deDatatypeResize(oldDatatype, newWidth), isSigned);
  if (oldWidth == newWidth) {
    if (newDatatype == oldDatatype) {
      return element;
    }
    return createElement(newDatatype, llElementGetName(element), llElementIsRef(element));
  }
  char *operation;
  if (newWidth < oldWidth) {
    operation = "trunc";
  } else {
    deDatatypeType type = deDatatypeGetType(oldDatatype);
    if (type == DE_TYPE_INT) {
      operation = "sext";
    } else {
      operation = "zext";
    }
  }
  uint32 value = printNewValue();
  llPrintf("%s i%u %s to i%u%s\n",
      operation, oldWidth, llElementGetName(element), newWidth, locationInfo());
  llElement result = createValueElement(newDatatype, value, false);
  if (!truncate && !deUnsafeMode && newWidth < oldWidth) {
    checkTruncation(element, result, oldWidth, newWidth, isSigned);
  }
  return result;
}

// Convert a small integer to a bigint on the array heap.
static llElement convertSmallIntToBigint(llElement element, uint32 newWidth, bool isSigned) {
  deDatatype datatype = llElementGetDatatype(element);
  uint32 oldWidth = deDatatypeGetWidth(datatype);
  if (oldWidth < llSizeWidth) {
    element = resizeSmallInteger(element, llSizeWidth, isSigned, false);
  }
  deDatatype newDatatype = deDatatypeSetSigned(deDatatypeResize(datatype, newWidth), isSigned);
  allocateTempArray(newDatatype);
  llElement bigintArray = popElement(false);
  bool secret = deDatatypeSecret(datatype);
  llDeclareRuntimeFunction("runtime_integerToBigint");
  llPrintf(
      "  call void @runtime_integerToBigint(%%struct.runtime_array* %s, i%s %s, i32 "
      "zeroext %u, i1 zeroext %s, i1 zeroext %s)\n", llElementGetName(bigintArray), llSize,
      llElementGetName(element), newWidth, boolVal(isSigned), boolVal(secret));
  return bigintArray;
}

// Convert a bigint in an array to a small integer.
static llElement convertBigintToSmallInt(llElement bigintArray, uint32 newWidth,
    bool isSigned, bool truncate) {
  char *func = truncate? "runtime_bigintToIntegerTrunc" : "runtime_bigintToInteger";
  llDeclareRuntimeFunction(func);
  uint32 value = printNewValue();
  llPrintf("call i%s @%s(%%struct.runtime_array* %s)\n", llSize, func,
      llElementGetName(bigintArray));
  llElement result = createValueElement(llSizeType, value, false);
  if (newWidth != llSizeWidth || isSigned) {
    result = resizeSmallInteger(result, newWidth, isSigned, truncate);
  }
  return result;
}

// Resize the bigint.
static llElement resizeBigint(llElement bigintArray, uint32 newWidth,
    bool isSigned, bool truncate) {
  deDatatype datatype = llElementGetDatatype(bigintArray);
  uint32 oldWidth = deDatatypeGetWidth(datatype);
  utAssert(oldWidth > llSizeWidth && newWidth > llSizeWidth);
  deDatatype newDatatype = deDatatypeSetSigned(deDatatypeResize(datatype, newWidth), isSigned);
  llDeclareRuntimeFunction("runtime_bigintCast");
  llElement tempArray = allocateTempValue(newDatatype);
  bool secret = deDatatypeSecret(datatype);
  llPrintf(
      "  call void @runtime_bigintCast(%%struct.runtime_array* %s, "
      "%%struct.runtime_array* %s, i32 %u, i1 %s, i1 %s, i1 %s)\n",
      llElementGetName(tempArray), llElementGetName(bigintArray), newWidth,
      boolVal(isSigned), boolVal(secret), boolVal(truncate));
  return popElement(false);
}

// Extend an integer.
static llElement resizeInteger(llElement element, uint32 newWidth, bool isSigned, bool truncate) {
  deDatatype oldDatatype = llElementGetDatatype(element);
  uint32 oldWidth = deDatatypeGetWidth(oldDatatype);
  if (newWidth == oldWidth && deDatatypeSigned(oldDatatype) == isSigned) {
    return element;
  } else if (oldWidth <= llSizeWidth && newWidth > llSizeWidth) {
    return convertSmallIntToBigint(element, newWidth, isSigned);
  } else if (oldWidth > llSizeWidth && newWidth <= llSizeWidth) {
    return convertBigintToSmallInt(element, newWidth, isSigned, truncate);
  } else if (newWidth > llSizeWidth) {
    return resizeBigint(element, newWidth, isSigned, truncate);
  }
  return resizeSmallInteger(element, newWidth, isSigned, truncate);
}

// Convert the top element to u64.
static inline void resizeTop(uint32 width) {
  pushElement(resizeInteger(popElement(true), width, false, false), false);
}

// Generate a constant string.
static llElement generateString(deString text) {
  llAddStringConstant(text);
  llElement element = createElement(deStringDatatypeCreate(), llStringGetName(text), true);
  element.isConst = true;
  return element;
}

// Call runtime_sprintf given the format and the expression or tuple.
static void callSprintfOrThrow(llElement destArray,
    llElement format, deExpression argument, bool isPrint, bool skipStrings) {
  bool isTuple = false;
  deExpressionType argType = deExpressionGetType(argument);
  if (argType == DE_EXPR_TUPLE || argType == DE_EXPR_LIST) {
    argument = deExpressionGetLastExpression(argument);
    isTuple = true;
  }
  uint32 numArguments = 0;
  while (argument != deExpressionNull) {
    deDatatype datatype = deExpressionGetDatatype(argument);
    if (!skipStrings || deExpressionGetType(argument) != DE_EXPR_STRING) {
      if (!deExpressionIsType(argument)) {
        generateExpression(argument);
        llElement *elementPtr = topOfStack();
        derefElement(elementPtr);
        if (deDatatypeIsInteger(datatype) && deDatatypeGetWidth(datatype) < llSizeWidth) {
          resizeTop(llSizeWidth);
        }
        numArguments++;
      }
    }
    if (isTuple) {
      argument = deExpressionGetPrevExpression(argument);
    } else {
      argument = deExpressionNull;
    }
  }
  if (isPrint) {
    llDeclareRuntimeFunction("runtime_sprintf");
    llPrintf(
        "  call void (%%struct.runtime_array*, %%struct.runtime_array*, ...) "
        "@runtime_sprintf(%%struct.runtime_array* %s, %s %s",
        llElementGetName(destArray),
        llGetTypeString(llElementGetDatatype(format), false),
        llElementGetName(format));
  } else {
    llDeclareRuntimeFunction("runtime_throwException");
    llPrintf(
        "  call void (%%struct.runtime_array*, ...) "
        "@runtime_throwException(%s %s",
        llGetTypeString(llElementGetDatatype(format), false),
        llElementGetName(format));
  }
  for (uint32 i = 0; i < numArguments; i++) {
    llElement element = popElement(false);
    llPrintf(", %s %s", getElementTypeString(element),
             llElementGetName(element));
  }
  llPrintf(")%s\n", locationInfo());
  if (!isPrint) {
    resetNeedsFreeList();
    llPrintf("  unreachable\n");
  }
}

// A mod expression can be either uint % uint, or a string % expression/tuple.
// Figure out which is the case and generate the code.
static void generateModExpression(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (deDatatypeGetType(datatype) != DE_TYPE_STRING) {
    generateBinaryExpression(expression, "urem");
    return;
  }
  deExpression formatExpression = deExpressionGetFirstExpression(expression);
  deExpression argument = deExpressionGetNextExpression(formatExpression);
  generateExpression(formatExpression);
  llElement format = popElement(false);  // Pop the format element.
  llElement result = allocateTempValue(deStringDatatypeCreate());
  callSprintfOrThrow(result, format, argument, true, false);
}

// Generate a select instruction.
static void generateSelect(llElement selectElement, llElement data1Element, llElement data0Element) {
  char *selectName = llElementGetName(selectElement);
  char *data1Name = llElementGetName(data1Element);
  char *data0Name = llElementGetName(data0Element);
  deDatatype datatype = llElementGetDatatype(data1Element);
  char *type = llGetTypeString(datatype, false);
  uint32 value = printNewValue();
  llPrintf("select i1 %s, %s %s, %s %s%s\n", selectName, type, data1Name, type,
           data0Name, locationInfo());
  pushValue(datatype, value, false);
}

// Generate a select expression.
// TODO: This does not protect the privacy of the select bit!  Write code to
// generate a secure select when select is secret.
static void generateSelectExpression(deExpression expression) {
  deExpression select = deExpressionGetFirstExpression(expression);
  deExpression data1 = deExpressionGetNextExpression(select);
  deExpression data0 = deExpressionGetNextExpression(data1);
  generateExpression(select);
  generateExpression(data1);
  generateExpression(data0);
  llElement data0Element = popElement(true);
  llElement data1Element = popElement(true);
  llElement selectElement = popElement(true);
  generateSelect(selectElement, data1Element, data0Element);
}

// Generate a call to runtime_nativeIntToString or runtime_bigintToString, depending
// on the representation of the integer.
static void generateIntegerToString(llElement value, llElement base) {
  deDatatype datatype = llElementGetDatatype(value);
  uint32 width = deDatatypeGetWidth(datatype);
  utAssert(llElementGetDatatype(base) == deUintDatatypeCreate(32));
  llElement result = allocateTempValue(deStringDatatypeCreate());
  if (width <= llSizeWidth) {
    bool isSigned = deDatatypeGetType(datatype) == DE_TYPE_INT;
    value = resizeSmallInteger(value, llSizeWidth, isSigned, false);
    llDeclareRuntimeFunction("runtime_nativeIntToString");
    llPrintf("  call void @runtime_nativeIntToString("
        "%%struct.runtime_array* %s, i%s %s, i32 %s, i1 zeroext %s)%s\n",
        llElementGetName(result), llSize, llElementGetName(value),
        llElementGetName(base), boolVal(isSigned), locationInfo());
  } else {
    llDeclareRuntimeFunction("runtime_bigintToString");
    llPrintf(
        "  call void @runtime_bigintToString(%%struct.runtime_array* %s, "
        "%%struct.runtime_array* %s, i32 %s)%s\n",
        llElementGetName(result), llElementGetName(value),
        llElementGetName(base), locationInfo());
  }
}

// Convert any printable datatype to a string, with runtime_vsprintf.
static void generateValueToString(llElement value) {
  deDatatype datatype = llElementGetDatatype(value);
  uint32 len = 42;
  uint32 pos = 0;
  char *format = utMakeString(len);
  format[pos++] = '%';
  format = deAppendFormatSpec(format, &len, &pos, datatype);
  llElement formatElement = generateString(deStringCreate(format, pos));
  llElement result = allocateTempValue(deStringDatatypeCreate());
  llDeclareRuntimeFunction("runtime_sprintf");
  llPrintf(
      "  call void (%%struct.runtime_array*, %%struct.runtime_array*, ...) "
      "@runtime_sprintf(%%struct.runtime_array* %s, %%struct.runtime_array* %s, %s %s)%s\n",
      llElementGetName(result), llElementGetName(formatElement),
      llGetTypeString(datatype, false), llElementGetName(value), locationInfo());
}

// Generate a builtin function.  Parameters have already been pushed onto the
// stack.
static void generateBuiltinMethod(deExpression expression) {
  deExpression accessExpression = deExpressionGetFirstExpression(expression);
  utAssert(deExpressionGetType(accessExpression) == DE_EXPR_DOT);
  deExpression parameters = deExpressionGetNextExpression(accessExpression);
  deDatatype callType = deExpressionGetDatatype(accessExpression);
  utAssert(deDatatypeGetType(callType) == DE_TYPE_FUNCTION);
  deFunction function = deDatatypeGetFunction(callType);
  generateExpression(accessExpression);
  llElement element = popElement(false);
  // This should be a delegate.  We don't need the top element.  The next
  // will be the expression to access the builtin object.
  utAssert(llElementIsDelegate(element));
  llElement access = popElement(true);
  deBuiltinFuncType type = deFunctionGetBuiltinType(function);
  switch (type) {
    case DE_BUILTINFUNC_ARRAYLENGTH:
    case DE_BUILTINFUNC_STRINGLENGTH: {
      uint32 lenPtr = printNewValue();
      llPrintf("getelementptr inbounds %%struct.runtime_array, %%struct.runtime_array* %s, i32 0, i32 1\n",
          llElementGetName(access));
      uint32 lenValue = printNewValue();
      char *location = locationInfo();
      llPrintf("load i%s, i%s* %%%u%s\n", llSize, llSize, lenPtr, location);
      pushValue(llSizeType, lenValue, false);
      break;
    }
    case DE_BUILTINFUNC_ARRAYRESIZE:
    case DE_BUILTINFUNC_STRINGRESIZE: {
      deDatatype datatype = llElementGetDatatype(access);
      if (access.isConst) {
        // The array is constant and must be copied before resize.  E.g.
        // [0].resize(n).
        llElement newArray = allocateTempArray(datatype);
        copyArray(newArray, access, false);
        access = newArray;
      }
      generateExpression(deExpressionGetFirstExpression(parameters));
      llElement numElements = popElement(true);
      numElements = resizeInteger(numElements, llSizeWidth, false, false);
      bool hasSubArrays = arrayHasSubArrays(datatype);
      deDatatype elementDatatype = deDatatypeGetElementType(datatype);
      llElement elementSize = findDatatypeSize(elementDatatype);
      llDeclareRuntimeFunction("runtime_resizeArray");
      char *location = locationInfo();
      llPrintf("  call void @runtime_resizeArray(%%struct.runtime_array* %s, i%s %s, i%s %s, "
          "i1 zeroext %u)%s\n", llElementGetName(access), llSize, llElementGetName(numElements),
          llSize, llElementGetName(elementSize), hasSubArrays, location);
      // Push the array back on the stack.
      pushElement(access, access.needsFree);
      break;
    }
    case DE_BUILTINFUNC_ARRAYAPPEND:
    case DE_BUILTINFUNC_STRINGAPPEND: {
      deExpression elementExpression = deExpressionGetFirstExpression(parameters);
      generateExpression(elementExpression);
      // We need the pointer to the element in this case.
      llElement element = popElement(false);
      if (!llElementIsRef(element)) {
        element = storeElementAndReturnRef(element);
      }
      llDeclareRuntimeFunction("runtime_appendArrayElement");
      deDatatype elementDatatype = llElementGetDatatype(element);
      uint32 uint8Ptr = getUintPointer(element, 8);
      llElement sizeValue = findDatatypeSize(elementDatatype);
      char *location = locationInfo();
      llPrintf("  call void @runtime_appendArrayElement(%%struct.runtime_array* %s, i8* %%%u, "
          "i%s %s, i1 zeroext %u, i1 zeroext %u)%s\n", llElementGetName(access), uint8Ptr, llSize,
          llElementGetName(sizeValue), llDatatypeIsArray(elementDatatype),
          arrayHasSubArrays(elementDatatype), location);
      break;
    }
    case DE_BUILTINFUNC_ARRAYCONCAT:
    case DE_BUILTINFUNC_STRINGCONCAT: {
      deExpression array2Expression = deExpressionGetFirstExpression(parameters);
      generateExpression(array2Expression);
      llElement array2 = popElement(false);
      generateConcat(access, array2);
      deDatatype datatype = llElementGetDatatype(access);
      deDatatype elementDatatype = deDatatypeGetElementType(datatype);
      llElement sizeValue = findDatatypeSize(elementDatatype);
      llDeclareRuntimeFunction("runtime_concatArrays");
      char *location = locationInfo();
      llPrintf("  call void @runtime_concatArrays(%%struct.runtime_array* %s, %%struct.runtime_array* %s, "
          "i%s %s, i1 zeroext false)%s\n", llElementGetName(access), llElementGetName(array2),
          llSize, llElementGetName(sizeValue), location);
      break;
    }
    case DE_BUILTINFUNC_ARRAYREVERSE:
    case DE_BUILTINFUNC_STRINGREVERSE: {
      deDatatype datatype = llElementGetDatatype(access);
      bool hasSubArrays = arrayHasSubArrays(datatype);
      deDatatype elementDatatype = deDatatypeGetElementType(datatype);
      llElement elementSize = findDatatypeSize(elementDatatype);
      char *location = locationInfo();
      llDeclareRuntimeFunction("runtime_reverseArray");
      llPrintf("  call void @runtime_reverseArray(%%struct.runtime_array* %s, i%s %s, i1 zeroext %s)%s\n",
          llElementGetName(access), llSize, llElementGetName(elementSize),
              boolVal(hasSubArrays), location);
      break;
    }
    case DE_BUILTINFUNC_STRINGTOUINTBE:
    case DE_BUILTINFUNC_STRINGTOUINTLE: {
      deExpression widthExpression = deExpressionGetFirstExpression(parameters);
      deDatatype datatype = deExpressionGetDatatype(widthExpression);
      utAssert(deDatatypeGetType(datatype) == DE_TYPE_UINT);
      uint32 width = deDatatypeGetWidth(datatype);
      bool secret = deDatatypeSecret(deExpressionGetDatatype(expression));
      llElement bigint = allocateTempArray(datatype);
      char *location = locationInfo();
      char *funcName = type == DE_BUILTINFUNC_STRINGTOUINTBE
                          ? "runtime_bigintDecodeBigEndian"
                          : "runtime_bigintDecodeLittleEndian";
      llDeclareRuntimeFunction(funcName);
      llPrintf("  call void @%s(%%struct.runtime_array* %s, %%struct.runtime_array* %s, i32 "
               "zeroext %u, i1 zeroext false, i1 zeroext %s)%s\n",
          funcName, llElementGetName(bigint), llElementGetName(access), width,
          boolVal(secret), location);
      if (width <= llSizeWidth) {
        // We need to convert it to an integer.
        popElement(false);  // Pop off bigint.
        llElement smallnum = convertBigintToSmallInt(bigint, width, false, false);
        pushElement(smallnum, false);
      }
      break;
    }
    case DE_BUILTINFUNC_STRINGTOHEX: {
      llElement hexString = allocateTempArray(deStringDatatypeCreate());
      char *location = locationInfo();
      llDeclareRuntimeFunction("runtime_stringToHex");
      llPrintf("  call void @runtime_stringToHex(%%struct.runtime_array* %s, %%struct.runtime_array* %s)%s\n",
          llElementGetName(hexString), llElementGetName(access), location);
      break;
    }
    case DE_BUILTINFUNC_HEXTOSTRING: {
      llElement binString = allocateTempArray(deStringDatatypeCreate());
      char *location = locationInfo();
      llDeclareRuntimeFunction("runtime_hexToString");
      llPrintf("  call void @runtime_hexToString(%%struct.runtime_array* %s, %%struct.runtime_array* %s)%s\n",
          llElementGetName(binString), llElementGetName(access), location);
      break;
    }
    case DE_BUILTINFUNC_UINTTOSTRINGBE:
    case DE_BUILTINFUNC_UINTTOSTRINGLE: {
      deDatatype accessType = llElementGetDatatype(access);
      if (!llDatatypeIsBigint(accessType)) {
        access = convertSmallIntToBigint( access, deDatatypeGetWidth(accessType), false);
      }
      deDatatype datatype = deStringDatatypeCreate();
      llElement string = allocateTempArray(datatype);
      char *location = locationInfo();
      char *funcName = type == DE_BUILTINFUNC_UINTTOSTRINGBE?
          "runtime_bigintEncodeBigEndian" :
          "runtime_bigintEncodeLittleEndian";
      llDeclareRuntimeFunction(funcName);
      llPrintf("  call void @%s(%%struct.runtime_array* %s, %%struct.runtime_array* %s)%s\n",
          funcName, llElementGetName(string), llElementGetName(access), location);
      break;
    }
    case DE_BUILTINFUNC_FIND:
    case DE_BUILTINFUNC_RFIND: {
      deExpression subStringExpression = deExpressionGetFirstExpression(parameters);
      deExpression offsetExpression = deExpressionGetNextExpression(subStringExpression);
      llElement offset;
      generateExpression(subStringExpression);
      llElement subString = popElement(false);
      if (offsetExpression != deExpressionNull) {
        generateExpression(offsetExpression);
        offset = popElement(true);
        offset = resizeInteger(offset, llSizeWidth, false, false);
      } else {
        offset = createElement(llSizeType, "0", false);
      }
      char *funcName = "runtime_stringFind";
      if (type == DE_BUILTINFUNC_RFIND) {
        funcName = "runtime_stringRfind";
      }
      llDeclareRuntimeFunction(funcName);
      uint32 retValue = printNewValue();
      llPrintf("call i%s @%s(%%struct.runtime_array* %s, %%struct.runtime_array* %s, i%s %s)%s\n",
          llSize, funcName, llElementGetName(access), llElementGetName(subString),
          llSize, llElementGetName(offset), locationInfo());
      pushValue(llSizeType, retValue, false);
      break;
    }
    case DE_BUILTINFUNC_BOOLTOSTRING: {
      llElement trueElement = generateString(deCStringCreate("true"));
      llElement falseElement = generateString(deCStringCreate("false"));
      generateSelect(access, trueElement, falseElement);
      break;
    }
    case DE_BUILTINFUNC_UINTTOSTRING:
    case DE_BUILTINFUNC_INTTOSTRING: {
      deExpression baseExpression= deExpressionGetFirstExpression(parameters);
      llElement base;
      if (baseExpression != deExpressionNull) {
        generateExpression(baseExpression);
        base = popElement(true);
        base = resizeInteger(base, 32, false, false);
      } else {
        base = createElement(deUintDatatypeCreate(32), "10", false);
      }
      generateIntegerToString(access, base);
      break;
    }
  case DE_BUILTINFUNC_ARRAYTOSTRING:
  case DE_BUILTINFUNC_TUPLETOSTRING:
  case DE_BUILTINFUNC_STRUCTTOSTRING:
      generateValueToString(access);
      break;
  case DE_BUILTINFUNC_ENUMTOSTRING: {
    char *name = deFunctionGetName(deDatatypeGetFunction(llElementGetDatatype(access)));
    generateString(deStringCreate(name, strlen(name)));
    break;
  }
  }
}

// Determine if the expression is a builtin function call.
static bool isBuiltinCall(deExpression expression) {
  if (deExpressionGetType(expression) != DE_EXPR_CALL) {
    return false;
  }
  deExpression left = deExpressionGetFirstExpression(expression);
  deDatatype callType = deExpressionGetDatatype(left);
  if (deDatatypeGetType(callType) != DE_TYPE_FUNCTION) {
    return false;
  }
  return deFunctionBuiltin(deDatatypeGetFunction(callType));
}

// Generate an access expression for a tuple.
static void generateTupleIndexExpression(deExpression left, uint32 index) {
  generateExpression(left);
  llElement tuple = popElement(true);
  llElement element = indexTuple(tuple, index, false);
  pushElement(element, false);
}

// Move the element, which can be an array or tuple.
static void moveArray(llElement dest, llElement source, bool freeDest) {
  if (freeDest) {
    callFree(dest);
  }
  char *location = locationInfo();
  llDeclareRuntimeFunction("runtime_moveArray");
  llPrintf(
      "  call void @runtime_moveArray(%%struct.runtime_array* %s, %%struct.runtime_array* "
      "%s)%s\n",
      llElementGetName(dest), llElementGetName(source), location);
}

// Update the back-pointer in the array to point to this element.
static void updateArrayBackpointer(llElement array) {
  llDeclareRuntimeFunction("runtime_updateArrayBackPointer");
  llPrintf("  call void @runtime_updateArrayBackPointer(%%struct.runtime_array* %s)\n",
           llElementGetName(array), llElementGetName(array));
}

// Update back-pointers of the tuple's arrays.
static void updateTupleArrayBackpointers(llElement tuple) {
  deDatatype datatype = llElementGetDatatype(tuple);
  deDatatypeType type = deDatatypeGetType(datatype);
  utAssert(type == DE_TYPE_TUPLE || type == DE_TYPE_STRUCT);
  for (uint32 i = 0; i < deDatatypeGetNumTypeList(datatype); i++) {
    deDatatype subType = deDatatypeGetiTypeList(datatype, i);
    if (deDatatypeContainsArray(subType)) {
      llElement subElement = indexTuple(tuple, i, true);
      deDatatype subType = llElementGetDatatype(subElement);
      if (llDatatypeIsArray(subType)) {
        updateArrayBackpointer(subElement);
      } else if (deDatatypeContainsArray(subType)) {
        updateTupleArrayBackpointers(subElement);
      }
    }
  }
}

// Move the tuple with a structure copy, then update array backpointers in the tuple.
static void moveTupleOrObject(llElement dest, llElement source) {
  utAssert(llElementIsRef(dest));
  if (llElementIsRef(source)) {
    derefAnyElement(&source);
  }
  deDatatype datatype = llElementGetDatatype(source);
  deDatatype destType = llElementGetDatatype(dest);
  utAssert(datatype == llElementGetDatatype(dest) ||
      deDatatypeGetType(datatype) == DE_TYPE_NULL ||
      deDatatypeGetType(destType) == DE_TYPE_STRUCT ||
      deDatatypeNullable(destType));
  char *type = llGetTypeString(datatype, true);
  char *location = locationInfo();
  llPrintf("  store %s %s, %s* %s%s\n", type, llElementGetName(source), type,
      llElementGetName(dest), location);
  if (deDatatypeContainsArray(datatype)) {
    updateTupleArrayBackpointers(dest);
  }
}

// Call unref on an existing object, unless this is the first assignment.
static void unrefCurrentObject(llElement dest) {
  if (!deStatementIsFirstAssignment(llCurrentStatement) &&
      isRefCounted(llElementGetDatatype(dest))) {
    // Deref reference counted objects when a variable is overwritten.
    llElement derefDest = dest;
    derefAnyElement(&derefDest);
    unrefObject(derefDest);
  }
}

// Move the element, which can be an array or tuple.
static void moveElement(llElement dest, llElement source, bool freeDest) {
  if (llElementNeedsFree(source)) {
    removeNeedsFreeElement(source);
  }
  deDatatype datatype = llElementGetDatatype(source);
  if (llDatatypeIsArray(datatype)) {
    moveArray(dest, source, freeDest);
  } else {
    if (freeDest) {
      unrefCurrentObject(dest);
    }
    // TODO: Free tuple dest!
    moveTupleOrObject(dest, source);
  }
}

// Store the basic type element.
void storeBasicType(llElement dest, llElement source) {
  utAssert(llElementIsRef(dest));
  char *type = llGetTypeString(llElementGetDatatype(source), true);
  llPrintf("  store %s %s, %s* %s%s\n", type, llElementGetName(source), type,
           llElementGetName(dest), locationInfo());
}

// Forward reference for recursion.
static void copyElement(llElement access, llElement element, bool freeDest);

// Copy the tuple or struct element by element.
static void copyTuple(llElement dest, llElement source, bool freeDest) {
  utAssert(llElementIsRef(dest));
  deDatatype datatype = llElementGetDatatype(source);
  if (deDatatypeGetType(datatype) == DE_TYPE_STRUCT) {
    datatype = deGetStructTupleDatatype(datatype);
  }
  utAssert(deDatatypeGetType(datatype) == DE_TYPE_TUPLE);
  for (uint32 i = 0; i < deDatatypeGetNumTypeList(datatype); i++) {
    llElement subSourceElement = indexTuple(source, i, false);
    llElement subDestElement = indexTuple(dest, i, true);
    copyElement(subDestElement, subSourceElement, false);
  }
}

// Copy an element, which should be an array type (eg string, bigint), or tuple.
static void copyElement(llElement access, llElement element, bool freeDest) {
  deDatatype datatype = llElementGetDatatype(element);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (llDatatypeIsArray(datatype)) {
    copyArray(access, element, freeDest);
  } else if (type == DE_TYPE_TUPLE || type == DE_TYPE_STRUCT) {
    copyTuple(access, element, freeDest);
  } else if (type == DE_TYPE_CLASS) {
    derefElement(&element);
    refObject(element);
    if (freeDest) {
      unrefCurrentObject(access);
    }
    storeBasicType(access, element);
  } else {
    derefElement(&element);
    storeBasicType(access, element);
  }
}

// If we know an array/tuple won't be referenced again, call this function
// instead of copyArray.  It checks the needsFree flag, and if the array will be
// freed, it calls runtime_moveArray rather than runtime_copyArray, which is faster.
static void copyOrMoveElement(llElement dest, llElement source, bool freeDest) {
  deDatatype datatype = llElementGetDatatype(source);
  if (!llElementNeedsFree(source) &&
      (deDatatypeContainsArray(datatype) || isRefCounted(datatype))) {
    copyElement(dest, source, freeDest);
  } else {
    moveElement(dest, source, freeDest);
  }
}

// Generate write expression.  The top level operator of the access expression
// needs to be evaluated differently, since it needs to give us the address to
// write to rather than the value contained there.
static void generateWriteExpression(deExpression accessExpression) {
  llElement value = popElement(true);
  generateExpression(accessExpression);
  llElement access = popElement(false);
  deDatatype datatype = llElementGetDatatype(access);
  if (deDatatypeContainsArray(datatype) || isRefCounted(datatype) ||
      deDatatypeGetType(datatype) == DE_TYPE_TUPLE ||
      deDatatypeGetType(datatype) == DE_TYPE_STRUCT) {
    copyOrMoveElement(access, value, !deStatementIsFirstAssignment(llCurrentStatement));
  } else {
    utAssert(llElementIsRef(access));
    char *type = llGetTypeString(llElementGetDatatype(value), true);
    llPrintf("  store %s %s, %s* %s%s\n", type, llElementGetName(value), type,
             llElementGetName(access), locationInfo());
  }
}

// Generate an assignment expression.
static void generateAssignmentExpression(deExpression expression) {
  deExpression accessExpression = deExpressionGetFirstExpression(expression);
  deExpression valueExpression = deExpressionGetNextExpression(accessExpression);
  generateExpression(valueExpression);
  generateWriteExpression(accessExpression);
}

// Write a tuple field.
static void writeTupleAtIndex(llElement tuple, uint32 index, llElement value) {
  utAssert(llElementIsRef(tuple));
  llElement access = indexTuple(tuple, index, true);
  deDatatype elementType = llElementGetDatatype(access);
  char *type = llGetTypeString(llElementGetDatatype(value), true);
  if (!llDatatypePassedByReference(elementType)) {
    llPrintf("  store %s %s, %s* %s%s\n", type, llElementGetName(value), type,
             llElementGetName(access), locationInfo());
  } else {
    copyOrMoveElement(access, value, true);
  }
}

// Evaluate each element of the tuple.
static void generateTupleExpression(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  llElement tuple = allocateTempValue(datatype);
  uint32 index = 0;
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    generateExpression(child);
    llElement value = popElement(true);
    writeTupleAtIndex(tuple, index, value);
    index++;
  }
  deEndExpressionExpression;
}

// Generate a struct constructor.  This leaves a tuple on the stack.
static void generateStructConstructor(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  deDatatype tupleType = deGetStructTupleDatatype(datatype);
  llElement tuple = allocateTempValue(tupleType);
  uint32 savedStackPos = llStackPos;
  deExpression accessExpression = deExpressionGetFirstExpression(expression);
  deExpression parameters = deExpressionGetNextExpression(accessExpression);
  evaluateParameters(deSignatureNull, datatype, parameters, false);
  uint32 index = 0;
  while (llStackPos > savedStackPos) {
    llElement value = popElement(true);
    writeTupleAtIndex(tuple, index, value);
    index++;
  }
}

// Generate a function call.  The return value is reserved on the stack first,
// then the arguments in reverse order of how they are listed.
static void generateCallExpression(deExpression expression) {
  if (isBuiltinCall(expression)) {
    generateBuiltinMethod(expression);
    return;
  }
  deExpression accessExpression = deExpressionGetFirstExpression(expression);
  deDatatype callType = deExpressionGetDatatype(accessExpression);
  if (deDatatypeGetType(callType) == DE_TYPE_FUNCTION) {
    deFunction function = deDatatypeGetFunction(callType);
    if (deFunctionGetType(function) == DE_FUNC_STRUCT) {
      generateStructConstructor(expression);
      return;
    }
  }
  deExpression parameters = deExpressionGetNextExpression(accessExpression);
  deSignature signature = deExpressionGetSignature(expression);
  deDatatype returnType = deExpressionGetDatatype(expression);
  uint32 savedStackPos = llStackPos;
  if (signature != deSignatureNull) {
    evaluateParameters(signature, deDatatypeNull, parameters,
        deExpressionIsMethodCall(accessExpression));
  } else {
    evaluateIndirectCallParameters(parameters);
  }
  generateExpression(accessExpression);
  llElement element = popElement(true);
  deDatatype accessDatatype = llElementGetDatatype(element);
  deDatatypeType accessType = deDatatypeGetType(accessDatatype);
  if (llElementIsDelegate(element)) {
    // If this is a delegate, the self expression is still on the stack, and
    // needs to be derefed
    llElement *selfElement = topOfStack();
    derefElement(selfElement);
  }
  llElement returnElement;
  bool returnsValuePassedByReference = llDatatypePassedByReference(returnType);
  if (returnsValuePassedByReference) {
    returnElement = allocateTempValue(returnType);
    returnType = deNoneDatatypeCreate();
  }
  bool returnsVal = deDatatypeGetType(returnType) != DE_TYPE_NONE;
  uint32 retVal = 0;
  if (returnsVal) {
    retVal = printNewValue();
  } else {
    llPuts("  ");
  }
  if (signature == deSignatureNull) {
    utAssert(accessType == DE_TYPE_FUNCPTR);
    llPrintf("call %s %s(", llGetTypeString(returnType, false), llElementGetName(element));
  } else {
    char *path = llEscapeIdentifier(deGetSignaturePath(signature));
    llPrintf("call %s @%s(", llGetTypeString(returnType, false), path);
  }
  bool firstTime = true;
  while (llStackPos > savedStackPos) {
    if (!firstTime) {
      llPuts(", ");
    }
    firstTime = false;
    llElement element = popElement(false);
    llPrintf("%s %s", getElementTypeString(element), llElementGetName(element));
  }
  llPrintf(")%s\n", locationInfo());
  if (returnsVal) {
    // If returned value is a reference counted object, add it to the needsFree list.
    llElement result = createValueElement(returnType, retVal, false);
    pushElement(result, isRefCounted(returnType));
  } else if (returnsValuePassedByReference) {
    pushElement(returnElement, false);
  }
}

// Generate code to bounds check a value.  The message will be passed to
// runtime_throwException if the bounds check fails.
static void limitCheck(llElement index, llElement limit) {
  if (deUnsafeMode || (!llDebugMode && deStatementGenerated(llCurrentStatement))) {
    return;
  }
  deDatatype limitType = llElementGetDatatype(limit);
  uint32 width = deDatatypeGetWidth(limitType);
  if (width > llSizeWidth) {
    width = llSizeWidth;
    limit = resizeInteger(limit, width, false, false);
  }
  index = resizeInteger(index, width, false, false);
  llElement string = generateString(deCStringCreate("Shift or rotate by more than integer width"));
  generateComparison(index, limit, "icmp ult");
  llElement condition = popElement(true);
  utSym passedLabel = newLabel("limitCheckPassed");
  bool generatedFailBlock = llLimitCheckFailedLabel != utSymNull;
  if (!generatedFailBlock) {
    llLimitCheckFailedLabel = newLabel("limitCheckFailed");
  }
  llPrintf("  br i1 %s, label %%%s, label %%%s%s\n",
      llElementGetName(condition), utSymGetName(passedLabel),
      utSymGetName(llLimitCheckFailedLabel), locationInfo());
  if (!generatedFailBlock) {
    llPrintf("%s:\n", utSymGetName(llLimitCheckFailedLabel));
    llDeclareRuntimeFunction("runtime_throwException");
    llPrintf("  call void (%%struct.runtime_array*, ...) @runtime_throwException(%%struct.runtime_array* %s)%s\n",
        llElementGetName(string), locationInfo());
    llPrintf("  unreachable\n");
  }
  llPrintf("%s:\n", utSymGetName(passedLabel));
  llPrevLabel = passedLabel;
}

// Perform a bounds check before indexing into an array.
static void boundsCheck(llElement array, llElement index, char *message) {
  if (deUnsafeMode || (!llDebugMode && deStatementGenerated(llCurrentStatement))) {
    return;
  }
  uint32 value = printNewValue();
  llPrintf("getelementptr inbounds %%struct.runtime_array, %%struct.runtime_array* %s, i32 0, i32 1\n",
      llElementGetName(array), locationInfo());
  deDatatype sizetDatatype = deUintDatatypeCreate(llSizeWidth);
  llElement numElements = createValueElement(sizetDatatype, value, true);
  derefElement(&numElements);
  index = resizeInteger(index, llSizeWidth, false, false);
  llElement string = generateString(deCStringCreate("Indexed passed the end of an array"));
  generateComparison(index, numElements, "icmp ult");
  llElement condition = popElement(true);
  utSym passedLabel = newLabel("boundsCheckPassed");
  bool generatedFailBlock = llBoundsCheckFailedLabel != utSymNull;
  if (!generatedFailBlock) {
    llBoundsCheckFailedLabel = newLabel("boundsCheckFailed");
  }
  llPrintf("  br i1 %s, label %%%s, label %%%s%s\n",
      llElementGetName(condition), utSymGetName(passedLabel),
      utSymGetName(llBoundsCheckFailedLabel), locationInfo());
  if (!generatedFailBlock) {
    llPrintf("%s:\n", utSymGetName(llBoundsCheckFailedLabel));
    llDeclareRuntimeFunction("runtime_throwException");
    llPrintf("  call void (%%struct.runtime_array*, ...) @runtime_throwException(%%struct.runtime_array* %s)%s\n",
        llElementGetName(string), locationInfo());
    llPrintf("  unreachable\n");
  }
  llPrintf("%s:\n", utSymGetName(passedLabel));
  llPrevLabel = passedLabel;
}

// Index into an array.
static void indexArray(llElement array, llElement index, bool needsBoundsCheck) {
  deDatatype indexDatatype = llElementGetDatatype(index);
  if (deDatatypeGetType(indexDatatype) == DE_TYPE_CLASS) {
    uint32 refWidth = deClassGetRefWidth(deDatatypeGetClass(indexDatatype));
    index = createElement(deUintDatatypeCreate(refWidth),
        llElementGetName(index), llElementIsRef(index));
  }
  if (needsBoundsCheck) {
    boundsCheck(array, index, "Index out of bounds");
  }
  deDatatype arrayDatatype = llElementGetDatatype(array);
  deDatatype elementDatatype = getElementType(arrayDatatype);
  llElement dataPtr = loadArrayDataPointer(array);
  char *type = llGetTypeString(llElementGetDatatype(dataPtr), true);
  char *indexType = llGetTypeString(llElementGetDatatype(index), false);
  uint32 valuePtr = printNewValue();
  llPrintf("getelementptr inbounds %s, %s* %s, %s %s\n", type, type,
           llElementGetName(dataPtr), indexType, llElementGetName(index));
  pushValue(elementDatatype, valuePtr, true);
}

// Generate code for the member access.
static void generateMemberAccess(deIdent ident, deExpression left, deExpression right) {
  generateExpression(left);
  llElement index = popElement(true);
  deVariable variable = deIdentGetVariable(ident);
  deVariable arrayVar = deVariableGetGlobalArrayVariable(variable);
  char *arrayName = llGetVariableName(arrayVar);
  llElement array = createElement(deVariableGetDatatype(arrayVar), arrayName, true);
  indexArray(array, index, true);
}

// Generate an index expression.
static void generateIndexExpression(deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDatatypeType type = deDatatypeGetType(deExpressionGetDatatype(left));
  if (type == DE_TYPE_ARRAY || type == DE_TYPE_STRING) {
    generateExpression(left);
    llElement array = popElement(false);
    generateExpression(right);
    llElement index = popElement(true);
    boundsCheck(array, index, "Index out of bounds");
    indexArray(array, index, true);
  } else {
    utAssert(type == DE_TYPE_TUPLE || type == DE_TYPE_STRUCT);
    utAssert(deExpressionGetType(right) == DE_EXPR_INTEGER);
    deLine line = deExpressionGetLine(right);
    uint32 index = deBigintGetUint32(deExpressionGetBigint(right), line);
    generateTupleIndexExpression(left, index);
  }
}

// Generate a slice expression.
static void generateSliceExpression(deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression lower = deExpressionGetNextExpression(left);
  deExpression upper = deExpressionGetNextExpression(lower);
  deDatatype datatype = deExpressionGetDatatype(left);
  deDatatypeType type = deDatatypeGetType(datatype);
  utAssert(type == DE_TYPE_ARRAY || type == DE_TYPE_STRING);
  deDatatype elementDatatype = deDatatypeGetElementType(datatype);
  generateExpression(left);
  llElement sourceElement = popElement(false);
  generateExpression(lower);
  llElement lowerElement = popElement(true);
  generateExpression(upper);
  llElement upperElement = popElement(true);
  llElement destElement = allocateTempArray(datatype);
  llElement sizeValue = findDatatypeSize(elementDatatype);
  bool hasSubArrays = arrayHasSubArrays(datatype);
  llDeclareRuntimeFunction("runtime_sliceArray");
  char *location = locationInfo();
  llPrintf(
      "  call void @runtime_sliceArray(%%struct.runtime_array* %s, %%struct.runtime_array* %s, "
      "i%s %s, i%s %s, i%s %s, i1 zeroext %s)%s\n",
      llElementGetName(destElement), llElementGetName(sourceElement), llSize,
      llElementGetName(lowerElement), llSize, llElementGetName(upperElement),
      llSize, llElementGetName(sizeValue), boolVal(hasSubArrays), location);
}

// Generate code to read a member variable.
static void generateClassAccess(deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  utAssert(deExpressionGetType(right) == DE_EXPR_IDENT);
  deDatatype leftType = deExpressionGetDatatype(left);
  deClass theClass = deDatatypeGetClass(leftType);
  deBlock block = deClassGetSubBlock(theClass);
  // Expect this to be foo.x, where foo is a class instance.
  deIdent ident = deBlockFindIdent(block, deExpressionGetName(right));
  utAssert(ident != deIdentNull);
  switch (deIdentGetType(ident)) {
    case DE_IDENT_VARIABLE:
      generateMemberAccess(ident, left, right);
      break;
    case DE_IDENT_FUNCTION: {
      generateExpression(left);
      deFunction function = deIdentGetFunction(ident);
      if (deFunctionGetType(function) != DE_FUNC_CONSTRUCTOR) {
        // This is a method call.  Push a delegate.
        utAssert(deIdentGetType(ident) == DE_IDENT_FUNCTION);
        generateExpression(right);
        llSetTopOfStackAsDelegate();
      } else {
        // Calling a constructor of an inner class, so not a delegate.
        popElement(false);
        deBlock block = deFunctionGetSubBlock(function);
        deBlock savedScopeBlock = llCurrentScopeBlock;
        llCurrentScopeBlock = block;
        generateIdentExpression(right);
        llCurrentScopeBlock = savedScopeBlock;
      }
      break;
    case DE_IDENT_UNDEFINED:
      utExit("Tried to access through undefined identifier");
      break;
    }
  }
}

// Return the position of the variable in the block.
static uint32 findVariableIndex(deVariable variable) {
  deBlock block = deVariableGetBlock(variable);
  uint32 pos = 0;
  deVariable otherVar;
  deForeachBlockVariable(block, otherVar) {
    if (variable == otherVar) {
      return pos;
    }
    pos++;
  } deEndBlockVariable;
  utExit("Variable not found on block");
  return 0;  // Dummy return
}

// Generate access to a structure.
static void generateStructAccess(deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  utAssert(deExpressionGetType(right) == DE_EXPR_IDENT);
  deDatatype datatype = deExpressionGetDatatype(left);
  deFunction structFunc = deDatatypeGetFunction(datatype);
  deBlock block = deFunctionGetSubBlock(structFunc);
  deIdent ident = deBlockFindIdent(block, deExpressionGetName(right));
  utAssert(ident != deIdentNull && deIdentGetType(ident) == DE_IDENT_VARIABLE);
  deVariable var = deIdentGetVariable(ident);
  uint32 index = findVariableIndex(var);
  generateTupleIndexExpression(left, index);
}

// Generate a dot expression.  This leaves one or two elements on the stack.  If
// the expression is a method call, the self-access expression is pushded first,
// and the method function is pushed second, and the element is marked as bing a
// delegate.  If it is a plain function, the result is the one function element.
// If it is a variable access, the result is the value of the variable.
static void generateDotExpression(deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deDatatype leftDatatype = deExpressionGetDatatype(left);
  deDatatypeType leftType = deDatatypeGetType(leftDatatype);
  if (leftType == DE_TYPE_CLASS) {
    generateClassAccess(expression);
    return;
  } else if (leftType == DE_TYPE_STRUCT) {
    generateStructAccess(expression);
    return;
  }
  deExpression right = deExpressionGetNextExpression(left);
  utAssert(deExpressionGetType(right) == DE_EXPR_IDENT);
  generateExpression(left);
  llElement *element = topOfStack();
  deDatatype datatype = llElementGetDatatype(*element);
  deDatatypeType type = deDatatypeGetType(datatype);
  deBlock block = deBlockNull;
  bool isDelegate = false;
  switch (type) {
    case DE_TYPE_BOOL:
    case DE_TYPE_STRING:
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_MODINT:
    case DE_TYPE_FLOAT:
    case DE_TYPE_ARRAY:
    case DE_TYPE_TUPLE: {
      // This is a buitin type method access.
      deTclass tclass = deFindTypeTclass(type);
      block = deFunctionGetSubBlock(deTclassGetFunction(tclass));
      isDelegate = true;
      break;
    }
    case DE_TYPE_FUNCTION:
    case DE_TYPE_TCLASS:
    case DE_TYPE_ENUMCLASS: {
      popElement(false);
      deFunction function = deDatatypeGetFunction(datatype);
      block = deFunctionGetSubBlock(function);
      break;
    }
    case DE_TYPE_STRUCT:
    case DE_TYPE_FUNCPTR:
    case DE_TYPE_CLASS:
    case DE_TYPE_NONE:
    case DE_TYPE_NULL:
    case DE_TYPE_ENUM:
      utExit("Unexpected type");
      break;
  }
  deIdent ident = deExpressionGetIdent(right);
  utAssert(ident != deIdentNull);
  deBlock savedScopeBlock = llCurrentScopeBlock;
  llCurrentScopeBlock = block;
  generateIdentExpression(right);
  llCurrentScopeBlock = savedScopeBlock;
  if (isDelegate) {
    llSetTopOfStackAsDelegate();
  }
}

// Determine if the expression is a constant token.
static bool isConstant(deExpression expression) {
  deExpressionType type = deExpressionGetType(expression);
  if (type == DE_EXPR_INTEGER) {
    deDatatype datatype = deExpressionGetDatatype(expression);
    if (deDatatypeGetWidth(datatype) > llSizeWidth) {
      return false;
    }
  }
  return type == DE_EXPR_INTEGER || type == DE_EXPR_BOOL ||
         type == DE_EXPR_NULL;
}

// See if the array is constant and can be pushed with one of the constant-array
// opcodes.
static bool arrayIsConstant(deExpression expression) {
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    if (!isConstant(child)) {
      return false;
    }
  }
  deEndExpressionExpression;
  return true;
}

// Create an array from an array expression.
static void generatePushArray(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (arrayIsConstant(expression)) {
    llElement *element = push(datatype, utSymGetName(llAddArrayConstant(expression)), true);
    element->isConst = true;
    return;
  }
  // Allocate the array.
  uint32 arrayPtr = printNewTmpValue();
  llTmpPrintf("alloca %%struct.runtime_array\n");
  llTmpPrintf("  store %%struct.runtime_array zeroinitializer, %%struct.runtime_array* %%.tmp%u%s\n",
      arrayPtr, locationInfo());
  // Allocate space for elements..
  uint32 numElements = deExpressionCountExpressions(expression);
  bool hasSubArrays = arrayHasSubArrays(datatype);
  llDeclareRuntimeFunction("runtime_allocArray");
  deDatatype elementDatatype = deDatatypeGetElementType(datatype);
  llElement sizeValue = findDatatypeSize(elementDatatype);
  llPrintf(
      "  call void @runtime_allocArray(%%struct.runtime_array* %%.tmp%u, i%s %u, i%s %s, i1 "
      "zeroext %u)\n",
      arrayPtr, llSize, numElements, llSize, llElementGetName(sizeValue), hasSubArrays);
  // Set element values.
  llElement array = createTmpValueElement(datatype, arrayPtr, true);
  uint32 i = 0;
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    generateExpression(child);
    llElement value = popElement(true);
    llElement index = createSmallInteger(i, 32, false);
    indexArray(array, index, false);
    llElement dest = popElement(false);
    utAssert(llElementIsRef(dest));
    if (deDatatypeContainsArray(datatype) || isRefCounted(datatype)) {
      copyOrMoveElement(dest, value, false);
    } else {
      char *typeString = llGetTypeString(elementDatatype, false);
      llPrintf("  store %s %s, %s* %s\n", typeString, llElementGetName(value),
               typeString, llElementGetName(dest));
    }
    i++;
  }
  deEndExpressionExpression;
  pushElement(array, true);
}

// Push a default value for the datatype onto the element stack.
static void pushDefaultValue(deDatatype datatype) {
  char *value = getDefaultValue(datatype);
  llElement *element = push(datatype, value, false);
  if (llDatatypeIsArray(datatype)) {
    element->isConst = true;
  }
  if (deDatatypeGetType(datatype) == DE_TYPE_CLASS) {
    element->isNull = true;
  }
}

// This differs from pushDefaultValue in that we may have to allocate a
// temporary array or tuple.
static void pushNullValue(deDatatype datatype) {
  if (!llDatatypePassedByReference(datatype)) {
    pushDefaultValue(datatype);
  } else {
    allocateTempValue(datatype);
  }
}

// Push the address of a function.
static void pushFunctionAddress(deExpression expression) {
  deSignature signature = deExpressionGetSignature(expression);
  deDatatype datatype = deExpressionGetDatatype(expression);
  char *path = utSprintf("@%s", llEscapeIdentifier(deGetSignaturePath(signature)));
  push(datatype, path, false);
}

// If the datatype is DE_TYPE_ENUM, cast it to its underlying datatype.
static deDatatype castEnumToBaseType(deExpression expression, bool orEnumClass) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (deDatatypeGetType(datatype) != DE_TYPE_ENUM &&
      (!orEnumClass || deDatatypeGetType(datatype) != DE_TYPE_ENUMCLASS)) {
    return datatype;
  }
  deBlock enumBlock = deFunctionGetSubBlock(deDatatypeGetFunction(datatype));
  return deFindEnumIntType(enumBlock);
}

// Generate a cast expression.
static void generateCastExpression(deExpression expression, bool truncate) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDatatype leftDatatype = castEnumToBaseType(left, true);
  deDatatype rightDatatype = castEnumToBaseType(right, false);
  if (deSetDatatypeSecret(leftDatatype, false) == deSetDatatypeSecret(rightDatatype, false)) {
    // No need to generate the cast.
    generateExpression(right);
    topOfStack()->datatype = rightDatatype;
    return;
  }
  deDatatypeType leftType = deDatatypeGetType(leftDatatype);
  deDatatypeType rightType = deDatatypeGetType(rightDatatype);
  generateExpression(right);
  topOfStack()->datatype = rightDatatype;
  if (deDatatypeTypeIsInteger(leftType) && deDatatypeTypeIsInteger(rightType)) {
    llElement rightElement = popElement(true);
    uint32 newWidth = deDatatypeGetWidth(leftDatatype);
    pushElement(resizeInteger(rightElement, newWidth,
        deDatatypeSigned(leftDatatype), truncate), false);
    return;
  }
  if (leftType == DE_TYPE_CLASS || rightType == DE_TYPE_CLASS ||
      leftType == DE_TYPE_STRING || rightType == DE_TYPE_STRING) {
    // These casts are almost nops.
    llElement *element = topOfStack();
    element->datatype = deExpressionGetDatatype(expression);
    return;
  }
  llElement rightElement = popElement(true);
  char *leftTypeString = llGetTypeString(leftDatatype, true);
  char *rightTypeString = llGetTypeString(rightDatatype, true);
  uint32 value = printNewValue();
  if (leftType == DE_TYPE_UINT && rightType == DE_TYPE_FLOAT) {
      llPrintf("fptoui %s %s to %s\n", rightTypeString,
          llElementGetName(rightElement), leftTypeString);
  } else if (leftType == DE_TYPE_INT && rightType == DE_TYPE_FLOAT) {
      llPrintf("fptosi %s %s to %s\n", rightTypeString,
          llElementGetName(rightElement), leftTypeString);
  } else if (leftType == DE_TYPE_FLOAT && rightType == DE_TYPE_UINT) {
      llPrintf("uitofp %s %s to %s\n", rightTypeString,
          llElementGetName(rightElement), leftTypeString);
  } else if (leftType == DE_TYPE_FLOAT && rightType == DE_TYPE_INT) {
      llPrintf("sitofp %s %s to %s\n", rightTypeString,
          llElementGetName(rightElement), leftTypeString);
  } else if (leftType == DE_TYPE_FLOAT && rightType == DE_TYPE_FLOAT) {
    // Must be different width floats.
    uint32 oldWidth = deDatatypeGetWidth(rightDatatype);
    uint32 newWidth = deDatatypeGetWidth(leftDatatype);
    utAssert(oldWidth != newWidth);
    if (oldWidth < newWidth) {
      // Extending precision.
      utAssert(oldWidth == 32 && newWidth == 64);
      llPrintf("fpext %s %s to %s\n", rightTypeString,
          llElementGetName(rightElement), leftTypeString);
    } else {
      // Truncating precision.
      utAssert(oldWidth == 64 && newWidth == 32);
      llPrintf("fptrunc %s %s to %s\n", rightTypeString,
          llElementGetName(rightElement), leftTypeString);
    }
  } else {
    utExit("Cannot cast from array to integer or back");
  }
  pushValue(leftDatatype, value, false);
}

// Generate a cast from/to signed/unsigned.
static void generateSignedCastExpression(deExpression expression, bool isSigned) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  deExpression child = deExpressionGetFirstExpression(expression);
  generateExpression(child);
  uint32 width = deDatatypeGetWidth(datatype);
  if (width <= llSizeWidth) {
    llElement *element = topOfStack();
    deDatatype datatype = deDatatypeSetSigned(llElementGetDatatype(*element), isSigned);
    element->datatype = datatype;
    return;
  }
  llElement element = popElement(false);
  llElement result = allocateTempValue(datatype);
  llDeclareRuntimeFunction("runtime_bigintCast");
  llPrintf(
      "  call void @runtime_bigintCast(%%struct.runtime_array* %s, %%struct.runtime_array* %s, "
      "i32 %u, i1 %s, i1 %s, i1 true)%s\n",
      llElementGetName(result), llElementGetName(element), width, boolVal(isSigned),
      boolVal(deDatatypeSecret(datatype)), locationInfo());
}

// Generate a call to runtime_bigintExp.
static void generateBigintExp(deExpression expression) {
  llDeclareRuntimeFunction("runtime_bigintExp");
  deExpression base = deExpressionGetFirstExpression(expression);
  deExpression exp = deExpressionGetNextExpression(base);
  generateExpression(base);
  llElement baseElement = popElement(true);
  generateExpression(exp);
  llElement expElement = popElement(true);
  expElement = resizeInteger(expElement, 32, false, false);
  llElement destArray = allocateTempValue(deExpressionGetDatatype(expression));
  llPrintf(
      "  call void @runtime_bigintExp(%%struct.runtime_array* %s, %%struct.runtime_array* %s, "
      "i32 %s)%s\n",
      llElementGetName(destArray), llElementGetName(baseElement),
      llElementGetName(expElement), locationInfo());
}

// Generate a call to runtime_smallnumExp.
static void generateSmallnumExp(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  bool isSigned = deDatatypeGetType(datatype) == DE_TYPE_INT;
  llDeclareRuntimeFunction("runtime_smallnumExp");
  deExpression base = deExpressionGetFirstExpression(expression);
  deExpression exp = deExpressionGetNextExpression(base);
  generateExpression(base);
  resizeTop(llSizeWidth);
  llElement baseElement = popElement(true);
  baseElement = resizeInteger(baseElement, llSizeWidth, deDatatypeSigned(datatype), false);
  generateExpression(exp);
  resizeTop(32);
  llElement expElement = popElement(true);
  expElement = resizeInteger(expElement, 32, false, false);
  uint32 value = printNewValue();
  llPrintf("call i%s @runtime_smallnumExp(i%s %s, i32 %s, i1 %s, i1 %s)%s\n", llSize,
           llSize, llElementGetName(baseElement), llElementGetName(expElement),
           boolVal(isSigned), boolVal(deDatatypeSecret(datatype)), locationInfo());
  pushValue(llSizeType, value, false);
  resizeTop(deDatatypeGetWidth(datatype));
}

// Generate a non-modular exponentiation expression.
static void generateExpExpression(deExpression expression) {
  deSignature signature = deExpressionGetSignature(expression);
  if (signature != deSignatureNull) {
    generateOperatorOverloadCall(expression, signature);
    return;
  }
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (llDatatypeIsBigint(datatype)) {
    generateBigintExp(expression);
  } else {
    generateSmallnumExp(expression);
  }
}

// Generate a modular exponentiation bigint call.
static void generateModularBigintExp(deExpression expression, llElement modulusElement) {
  llDeclareRuntimeFunction("runtime_bigintModularExp");
  deExpression base = deExpressionGetFirstExpression(expression);
  deExpression exp = deExpressionGetNextExpression(base);
  generateModularExpression(base, modulusElement);
  llElement baseElement = popElement(true);
  // We can't reduce the exponent by the modulus without knowing the
  // factorization of the modulus.
  generateExpression(exp);
  llElement expElement = popElement(true);
  if (!llDatatypeIsBigint(expElement.datatype)) {
    expElement = convertSmallIntToBigint(expElement, deDatatypeGetWidth(expElement.datatype),
        deDatatypeSigned(llElementGetDatatype(expElement)));
  }
  llElement destArray = allocateTempValue(deExpressionGetDatatype(expression));
  llPrintf(
      "  call void @runtime_bigintModularExp(%%struct.runtime_array* %s, %%struct.runtime_array* "
      "%s, "
      "%%struct.runtime_array* %s, %%struct.runtime_array* %s)%s\n",
      llElementGetName(destArray), llElementGetName(baseElement),
      llElementGetName(expElement), llElementGetName(modulusElement),
      locationInfo());
}

// Generate a modular exponentiation smallnum call.
static void generateModularSmallnumExp(deExpression expression, llElement modulusElement) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  llDeclareRuntimeFunction("runtime_smallnumModularExp");
  deExpression base = deExpressionGetFirstExpression(expression);
  deExpression exp = deExpressionGetNextExpression(base);
  generateModularExpression(base, modulusElement);
  resizeTop(llSizeWidth);
  llElement baseElement = popElement(true);
  generateExpression(exp);
  resizeTop(llSizeWidth);
  llElement expElement = popElement(true);
  modulusElement = resizeSmallInteger(modulusElement, llSizeWidth, false, false);
  uint32 value = printNewValue();
  llPrintf("call i%s @runtime_smallnumModularExp(i%s %s, i%s %s, i%s %s, i1 %s)%s\n", llSize,
      llSize, llElementGetName(baseElement), llSize, llElementGetName(expElement),
      llSize, llElementGetName(modulusElement), boolVal(deDatatypeSecret(datatype)),
      locationInfo());
  pushValue(llSizeType, value, false);
  resizeTop(deDatatypeGetWidth(datatype));
}

// Generate a non-modular exponentiation expression.
static void generateModularExpExpression(deExpression expression, llElement modulusElement) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (llDatatypeIsBigint(datatype)) {
    generateModularBigintExp(expression, modulusElement);
  } else {
    generateModularSmallnumExp(expression, modulusElement);
  }
}

// Generate an op-equals assignment, such as a += 1.
static void generateOpEqualsExpression(deExpression expression) {
  deExpressionType type = deExpressionGetType(expression);
  // Temporarily override the expression type.
  deExpressionSetType(expression, type + DE_EXPR_ADD - DE_EXPR_ADD_EQUALS);
  generateExpression(expression);
  deExpressionSetType(expression, type);
  deExpression accessExpression = deExpressionGetFirstExpression(expression);
  generateWriteExpression(accessExpression);
}

// Generate a constant string expression.
static void generateStringExpression(deExpression expression) {
  deString string = deExpressionGetAltString(expression);
  if (string == deStringNull) {
    string = deExpressionGetString(expression);
  }
  string = deUniquifyString(string);
  pushElement(generateString(string), false);
}

// Generate a complement expression.  LLVM does not have complement, so XOR with -1.
static void generateComplementExpression(deExpression expression) {
  deSignature signature = deExpressionGetSignature(expression);
  if (signature != deSignatureNull) {
    generateOperatorOverloadCall(expression, signature);
    return;
  }
  deExpression left = deExpressionGetFirstExpression(expression);
  generateExpression(left);
  llElement leftElement = popElement(true);
  deDatatype datatype = llElementGetDatatype(leftElement);
  if (llDatatypeIsBigint(datatype)) {
    llDeclareRuntimeFunction("runtime_bigintComplement");
    llElement resultArray = allocateTempValue(datatype);
    llPrintf(
      "  call void @runtime_bigintComplement(%%struct.runtime_array* %s, %%struct.runtime_array* %s)\n",
      llElementGetName(resultArray), llElementGetName(leftElement));
  } else {
    char *type = llGetTypeString(datatype, false);
    uint32 value = printNewValue();
    llPrintf("xor %s %s, -1\n", type, llElementGetName(leftElement));
    pushValue(datatype, value, false);
  }
}

// Generate a negate expression.  LLVM does not have negate, so subtract from 0.
static void generateNegateExpression(deExpression expression) {
  deSignature signature = deExpressionGetSignature(expression);
  if (signature != deSignatureNull) {
    generateOperatorOverloadCall(expression, signature);
    return;
  }
  deExpression left = deExpressionGetFirstExpression(expression);
  generateExpression(left);
  llElement leftElement = popElement(true);
  deDatatype datatype = llElementGetDatatype(leftElement);
  uint32_t width = deDatatypeGetWidth(datatype);
  if (deDatatypeIsFloat(datatype)) {
    char *type = llGetTypeString(datatype, false);
    uint32 value = printNewValue();
    llPrintf("fneg %s %s\n", type, llElementGetName(leftElement));
    pushValue(datatype, value, false);
  } else if (width > llSizeWidth) {
    char *funcName = "runtime_bigintNegate";
    if (!deUnsafeMode && deExpressionGetType(expression) == DE_EXPR_NEGATETRUNC) {
      funcName = "runtime_bigintNegateTrunc";
    }
    llDeclareRuntimeFunction(funcName);
    llElement resultArray = allocateTempValue(datatype);
    llPrintf(
      "  call void @%s(%%struct.runtime_array* %s, %%struct.runtime_array* %s)\n",
      funcName, llElementGetName(resultArray), llElementGetName(leftElement));
  } else if (!deUnsafeMode && deExpressionGetType(expression) != DE_EXPR_NEGATETRUNC) {
    uint32 structValue = printNewValue();
    char *opType = findTruncatingOpName(expression);
    llDeclareOverloadedFunction(utSprintf(
        "declare {i%u, i1} @llvm.%s.with.overflow.i%u(i%u, i%u)\n",
        width, opType, width, width, width));
    llPrintf("call {i%u, i1} @llvm.%s.with.overflow.i%u(i%u 0, i%u %s)%s\n",
             width, opType, width, width, width, llElementGetName(leftElement),
             locationInfo());
    uint32 value = printNewValue();
    llPrintf("extractvalue {i%u, i1} %%%u, 0\n", width, structValue);
    uint32 overflowValue = printNewValue();
    llPrintf("extractvalue {i%u, i1} %%%u, 1\n", width, structValue);
    utSym passed = newLabel("overflowCheckPassed");
    utSym failed = newLabel("overflowCheckFailed");
    llPrintf("  br i1 %%%u, label %%%s, label %%%s\n",
        overflowValue, utSymGetName(failed), utSymGetName(passed));
    printLabel(failed);
    llDeclareRuntimeFunction("runtime_throwOverflow");
    llPrintf("  call void @runtime_throwOverflow()\n  unreachable\n");
    printLabel(passed);
    pushValue(datatype, value, false);
  } else {
    char *type = llGetTypeString(datatype, false);
    uint32 value = printNewValue();
    llPrintf("sub %s 0, %s\n", type, llElementGetName(leftElement));
    pushValue(datatype, value, false);
  }
}

// Generate a random integer.
static void generateRandomUint(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  uint32 width = deDatatypeGetWidth(datatype);
  char *location = locationInfo();
  if (width > llSizeWidth) {
    llElement dest = allocateTempValue(datatype);
    llDeclareRuntimeFunction("runtime_generateTrueRandomBigint");
    llPrintf("  call void @runtime_generateTrueRandomBigint(%%struct.runtime_array* %s, i32 %u)%s\n",
        llElementGetName(dest), width, location);
  } else {
    uint32 value = printNewValue();
    llDeclareRuntimeFunction("runtime_generateTrueRandomValue");
    llPrintf("call i%s @runtime_generateTrueRandomValue(i32 %u)%s\n", llSize, width, location);
    llElement element = resizeInteger(createValueElement(deUintDatatypeCreate(llSizeWidth),
        value, false), width, false, false);
    pushElement(element, false);
  }
}

// Return true if the expression is a constant less than width.
static bool couldBeGreaterOrEqual(deExpression expression, uint32 width) {
  if (deExpressionGetType(expression) != DE_EXPR_INTEGER) {
    return true;
  }
  deBigint bigint = deExpressionGetBigint(expression);
  uint32 intVal = deBigintGetUint32(bigint, llCurrentLine);
  if (intVal >= width) {
    fclose(llAsmFile);
    deError(llCurrentLine, "Shift or rotate by more than integer width");
  }
  return false;
}

// Generate a bigint rotate left/right intrinsic.
static void generateBigintShiftOrRotateExpression(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  uint32 width = deDatatypeGetWidth(datatype);
  utAssert(width > llSizeWidth);
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  generateExpression(left);
  llElement leftElement = popElement(true);
  generateExpression(right);
  llElement rightElement = popElement(true);
  deDatatype rightType = deExpressionGetDatatype(right);
  if (couldBeGreaterOrEqual(right, width)) {
    llElement limit = createElement(deUintDatatypeCreate(32), utSprintf("%u", width), false);
    limitCheck(rightElement, limit);
  }
  if (deDatatypeGetWidth(rightType) != 32) {
    rightElement = resizeInteger(rightElement, 32, deDatatypeSigned(rightType), false);
  }
  char *function = findExpressionFunction(expression);
  llElement resultArray = allocateTempValue(datatype);
  llDeclareRuntimeFunction(function);
  llPrintf("  call void @%s(%%struct.runtime_array* %s, %%struct.runtime_array* %s, i32 %s)%s\n",
      function, llElementGetName(resultArray), llElementGetName(leftElement),
      llElementGetName(rightElement), locationInfo());
}

// Generate a rotate left/right intrinsic.
static void generateShiftOrRotateExpression(deExpression expression) {
  deSignature signature = deExpressionGetSignature(expression);
  if (signature != deSignatureNull) {
    generateOperatorOverloadCall(expression, signature);
    return;
  }
  deDatatype datatype = deExpressionGetDatatype(expression);
  uint32 width = deDatatypeGetWidth(datatype);
  if (width > llSizeWidth) {
    generateBigintShiftOrRotateExpression(expression);
    return;
  }
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  generateExpression(left);
  llElement leftElement = popElement(true);
  generateExpression(right);
  llElement rightElement = popElement(true);
  deDatatype rightType = deExpressionGetDatatype(right);
  if (deDatatypeGetWidth(rightType) != width) {
    rightElement = resizeInteger(rightElement, width, false, false);
  }
  if (couldBeGreaterOrEqual(right, width)) {
    llElement limit = createElement(deUintDatatypeCreate(32), utSprintf("%u", width), false);
    limitCheck(rightElement, limit);
  }
  char *leftName = llElementGetName(leftElement);
  char *rightName = llElementGetName(rightElement);
  bool isRotate = false;
  char *operation = NULL;
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_SHL:
      operation = "shl";
      break;
    case DE_EXPR_SHR:
      if (deDatatypeGetType(datatype) == DE_TYPE_INT) {
        operation = "ashr";
      } else {
        operation = "lshr";
      }
      break;
    case DE_EXPR_ROTL:
      isRotate = true;
      operation = "fshl";
      break;
    case DE_EXPR_ROTR:
      isRotate = true;
      operation = "fshr";
      break;
    default:
      utExit("Unexpected shift/rotate type");
  }
  uint32 value = printNewValue();
  char *location = locationInfo();
  if (isRotate) {
    llDeclareOverloadedFunction(utSprintf(
        "declare i%u @llvm.%s.i%u(i%u, i%u, i%u)\n",
        width, operation, width, width, width, width));
    llPrintf("call i%u @llvm.%s.i%u(i%u %s, i%u %s, i%u %s)%s\n",
        width, operation, width, width, leftName, width, leftName, width, rightName, location);
  } else {
    llPrintf("%s i%u %s, %s%s\n",
        operation, width, leftName, rightName, location);
  }
  pushValue(datatype, value, false);
}

// Write a binary modular expression.
static void generateBinaryModularExpression(deExpression expression, llElement modulusElement) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  generateModularExpression(left, modulusElement);
  if (!llDatatypeIsBigint(datatype)) {
    resizeTop(llSizeWidth);
  }
  llElement leftElement = popElement(true);
  generateModularExpression(right, modulusElement);
  if (!llDatatypeIsBigint(datatype)) {
    resizeTop(llSizeWidth);
    modulusElement = resizeSmallInteger(modulusElement, llSizeWidth, false, false);
  }
  llElement rightElement = popElement(true);
  char *function = findExpressionFunction(expression);
  char *location = locationInfo();
  llDeclareRuntimeFunction(function);
  if (llDatatypeIsBigint(datatype)) {
    llElement resultArray = allocateTempValue(datatype);
    llPrintf("  call void @%s(%%struct.runtime_array* %s, %%struct.runtime_array* %s, "
             "%%struct.runtime_array* %s, %%struct.runtime_array* %s)%s\n",
        function, llElementGetName(resultArray), llElementGetName(leftElement),
        llElementGetName(rightElement), llElementGetName(modulusElement), location);
  } else {
    bool secret = deDatatypeSecret(datatype);
    uint32 value = printNewValue();
    llPrintf("call i%s @%s(i%s %s, i%s %s, i%s %s, i1 zeroext %s)%s\n",
        llSize, function, llSize, llElementGetName(leftElement), llSize,
        llElementGetName(rightElement), llSize, llElementGetName(modulusElement),
        boolVal(secret), location);
    pushValue(llSizeType, value, false);
    resizeTop(deDatatypeGetWidth(datatype));
  }
}

// Perform modular reduction to convert the integer value to the range
// [0..modulus).  If the value is secret, do the constant-time reduction.
// Otherwise, check first to see if the value is already reduced.
static void modularReduction(deExpression expression,
    llElement modulusElement) {
  generateExpression(expression);
  llElement valueElement = popElement(true);
  deDatatype valDatatype = llElementGetDatatype(valueElement);
  deDatatype modDatatype = llElementGetDatatype(modulusElement);
  uint32 valWidth = deDatatypeGetWidth(valDatatype);
  uint32 modWidth = deDatatypeGetWidth(modDatatype);
  if (valWidth < modWidth) {
    valueElement = resizeInteger(valueElement, modWidth, deDatatypeSigned(valDatatype), false);
    valDatatype = llElementGetDatatype(valueElement);
  } else if (valWidth > modWidth) {
    modulusElement = resizeInteger(modulusElement, valWidth, false, false);
    modDatatype = llElementGetDatatype(modulusElement);
  }
  char *location = locationInfo();
  if (llDatatypeIsBigint(modDatatype)) {
    char *function = "runtime_bigintMod";
    llDeclareRuntimeFunction(function);
    llElement resultArray = allocateTempValue(modDatatype);
    llPrintf("  call void @%s(%%struct.runtime_array* %s, %%struct.runtime_array* %s, "
             "%%struct.runtime_array* %s)%s\n",
        function, llElementGetName(resultArray), llElementGetName(valueElement),
        llElementGetName(modulusElement), location);
  } else {
    bool isSigned = deDatatypeGetType(valDatatype) == DE_TYPE_INT;
    bool secret = deDatatypeSecret(valDatatype);
    if (!isSigned && !secret) {
      // The simple case where the value is unsigned maps to urem.
      char *type = llGetTypeString(modDatatype, false);
      uint32 value = printNewValue();
      llPrintf("urem %s %s, %s%s\n", type,
          llElementGetName(valueElement), llElementGetName(modulusElement), location);
      pushValue(modDatatype, value, false);
    } else {
      // Edge cases are ugly, so call the runtime function to help.
      valueElement = resizeSmallInteger(valueElement, llSizeWidth,
          deDatatypeSigned(valDatatype), false);
      modulusElement = resizeSmallInteger(modulusElement, llSizeWidth, false, false);
      char *function = "runtime_smallnumModReduce";
      llDeclareRuntimeFunction(function);
      uint32 value = printNewValue();
      llPrintf("call i%s @%s(i%s %s, i%s %s, i1 zeroext %s, i1 zeroext %s)%s\n",
          llSize, function, llSize, llElementGetName(valueElement), llSize,
          llElementGetName(modulusElement), boolVal(isSigned), boolVal(secret), location);
      pushValue(llSizeType, value, false);
      if (modWidth < llSizeWidth) {
        llElement resultElement = popElement(true);
        pushElement(resizeSmallInteger(resultElement, modWidth, false, false), false);
      }
    }
  }
  resizeTop(modWidth);
}

// Generate a modular negate.  Just subtract the value from the modulus.
static void generateModularNegateExpression(deExpression expression, llElement modulusElement) {
  deExpression valExpr = deExpressionGetFirstExpression(expression);
  generateModularExpression(valExpr, modulusElement);
  llElement valElement = popElement(true);
  char *location = locationInfo();
  if (llDatatypeIsBigint(llElementGetDatatype(modulusElement))) {
    llElement destArray = allocateTempValue(deExpressionGetDatatype(expression));
    llDeclareRuntimeFunction("runtime_bigintSub");
    llPrintf(
        "  call void @runtime_bigintSub(%%struct.runtime_array* %s, %%struct.runtime_array* %s, "
        "%%struct.runtime_array* %s)%s\n",
        llElementGetName(destArray), llElementGetName(modulusElement),
        llElementGetName(valElement), location);
  } else {
    deDatatype datatype = llElementGetDatatype(modulusElement);
    char *type = llGetTypeString(datatype, false);
    uint32 value = printNewValue();
    llPrintf("sub %s %s, %s%s\n", type, llElementGetName(modulusElement),
        llElementGetName(valElement), location);
    pushValue(datatype, value, false);
  }
}

// Generate a binary equality expression, either == or !=, inside a modular
// expression.
static void generateModularEqualityExpression(deExpression expression, llElement modulusElement) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  generateModularExpression(left, modulusElement);
  llElement leftElement = popElement(true);
  generateModularExpression(right, modulusElement);
  llElement rightElement = popElement(true);
  deDatatype datatype = deExpressionGetDatatype(left);
  if (llDatatypeIsBigint(datatype)) {
    runtime_comparisonType comparisonType = findBigintComparisonType(expression);
    generateBigintComparison(leftElement, rightElement, comparisonType);
  } else {
    char *op;
    if (deExpressionGetType(expression) == DE_EXPR_EQUAL) {
      op = "icmp eq";
    } else {
      op = "icmp ne";
    }
    generateComparison(leftElement, rightElement, op);
  }
}

// Generate a modular integer expression recursively.
static void generateModularExpression(deExpression expression, llElement modulusElement) {
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
      // Generated them, and convert to the modular type.
      modularReduction(expression, modulusElement);
      break;
    case DE_EXPR_ADD:
    case DE_EXPR_SUB:
    case DE_EXPR_MUL:
    case DE_EXPR_DIV:
      generateBinaryModularExpression(expression, modulusElement);
      break;
    case DE_EXPR_EXP:
      generateModularExpExpression(expression, modulusElement);
      break;
    case DE_EXPR_REVEAL:
    case DE_EXPR_SECRET:
      generateModularExpression(deExpressionGetFirstExpression(expression), modulusElement);
      break;
    case DE_EXPR_NEGATE:
      generateModularNegateExpression(expression, modulusElement);
      break;
    case DE_EXPR_EQUAL: case DE_EXPR_NOTEQUAL:
      generateModularEqualityExpression(expression, modulusElement);
      break;
    default:
      fclose(llAsmFile);
      deError(deExpressionGetLine(expression), "Invalid modular arithmetic expression");
  }
  llElement *resultElement = topOfStack();
  deDatatype resultType = llElementGetDatatype(*resultElement);
  if (deDatatypeGetType(resultType) == DE_TYPE_MODINT ||
      deDatatypeGetType(resultType) == DE_TYPE_MODINT) {
    // The result of a modular operation is uint, not modint.
    resultElement->datatype = deUintDatatypeCreate(deDatatypeGetWidth(resultType));
  }
}

// Generate a modint expression of the form <expression> mod <variable>.
static void generateModintExpression(deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression modulusExpr = deExpressionGetNextExpression(left);
  generateExpression(modulusExpr);
  llElement modulusElement = popElement(true);
  generateModularExpression(left, modulusElement);
}

// Jump to the label.
static void jumpTo(utSym label) {
  llPrintf("  br label %%%s\n", utSymGetName(label));
}

// Sometimes we can't wait to free elements until a statement finishes.  This
// function frees all temporary values created when evaluating the expression,
// so it can only be called on expressions that don't return a temp value.
static void generateExpressionAndFreeTempElements(deExpression expression) {
  uint32 savedPos = llNeedsFreePos;
  generateExpression(expression);
  freeRecentElements(savedPos);
}

// Generate a logical AND expression.  If the result is secret, evaluate both
// operands.  If they result is not secret, evaluate the second only if the
// first is true.
static void generateLogicalAndExpression( deExpression expression) {
  deSignature signature = deExpressionGetSignature(expression);
  if (signature != deSignatureNull) {
    generateOperatorOverloadCall(expression, signature);
    return;
  }
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (deDatatypeSecret(datatype)) {
    generateBinaryExpression(expression, "and");
    return;
  }
  deExpression left = deExpressionGetFirstExpression(expression);
  generateExpressionAndFreeTempElements(left);
  llElement result1 = popElement(true);
  utSym andShortcutTakenLabel = newLabel("andShortcutTaken");
  utSym block1Label = llPrevLabel;
  utSym andShortcutNotTakenLabel = newLabel("andShortcutNotTaken");
  llPrintf("  br i1 %s, label %%%s, label %%%s%s\n",
      llElementGetName(result1), utSymGetName(andShortcutNotTakenLabel),
      utSymGetName(andShortcutTakenLabel), locationInfo());
  llPrintf("%s:\n", utSymGetName(andShortcutNotTakenLabel));
  llPrevLabel = andShortcutNotTakenLabel;
  deExpression right = deExpressionGetNextExpression(left);
  generateExpressionAndFreeTempElements(right);
  llElement result2 = popElement(true);
  jumpTo(andShortcutTakenLabel);
  llPrintf("%s:\n", utSymGetName(andShortcutTakenLabel));
  // Generate the dreaded "phony" instruction, eg:
  //   %12 = phi i1 [ false, %2 ], [ %10, %8 ]
  uint32 value = printNewValue();
  llPrintf("phi i1 [false, %%%s], [%s, %%%s]\n", utSymGetName(block1Label),
      llElementGetName(result2), utSymGetName(llPrevLabel));
  pushValue(deBoolDatatypeCreate(), value, false);
  llPrevLabel = andShortcutTakenLabel;
}

// Generate a logical OR expression.  If the result is secret, evaluate both
// operands.  If they result is not secret, evaluate the second only if the
// first is false.
static void generateLogicalOrExpression( deExpression expression) {
  deSignature signature = deExpressionGetSignature(expression);
  if (signature != deSignatureNull) {
    generateOperatorOverloadCall(expression, signature);
    return;
  }
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (deDatatypeSecret(datatype)) {
    generateBinaryExpression(expression, "or");
    return;
  }
  deExpression left = deExpressionGetFirstExpression(expression);
  generateExpressionAndFreeTempElements(left);
  llElement result1 = popElement(true);
  utSym orShortcutTakenLabel = newLabel("orShortcutTaken");
  utSym block1Label = llPrevLabel;
  utSym orShortcutNotTakenLabel = newLabel("orShortcutNotTaken");
  llPrintf("  br i1 %s, label %%%s, label %%%s%s\n",
      llElementGetName(result1), utSymGetName(orShortcutTakenLabel),
      utSymGetName(orShortcutNotTakenLabel), locationInfo());
  llPrintf("%s:\n", utSymGetName(orShortcutNotTakenLabel));
  llPrevLabel = orShortcutNotTakenLabel;
  deExpression right = deExpressionGetNextExpression(left);
  generateExpressionAndFreeTempElements(right);
  llElement result2 = popElement(true);
  jumpTo(orShortcutTakenLabel);
  llPrintf("%s:\n", utSymGetName(orShortcutTakenLabel));
  // Generate the dreaded "phony" instruction, eg:
  //   %12 = phi i1 [ false, %2 ], [ %10, %8 ]
  uint32 value = printNewValue();
  llPrintf("phi i1 [true, %%%s], [%s, %%%s]\n", utSymGetName(block1Label),
      llElementGetName(result2), utSymGetName(llPrevLabel));
  pushValue(deBoolDatatypeCreate(), value, false);
  llPrevLabel = orShortcutTakenLabel;
}

// Find the ref width of the class, if this is a class datatype, or the tclass
// if it is a NULL.  There should not still be TBD classes at this point, so
// when this is fixed, simplify this code.
static uint32 findClassRefWidth(deDatatype datatype) {
  switch(deDatatypeGetType(datatype)) {
    case DE_TYPE_CLASS:
      return deClassGetRefWidth(deDatatypeGetClass(datatype));
    case DE_TYPE_NULL:
      return deTclassGetRefWidth(deDatatypeGetTclass(datatype));
    default:
      utExit("Unexpected datatype");
  }
  return 0;  // Dummy return.
}

// Generate the sub expression and generate code to verify it is non-null, if it
// is an object type.
static void generateNotNullExpression(deExpression expression) {
  generateExpression(deExpressionGetFirstExpression(expression));
  // TODO: Generate not-null check here.
}

// Generate code for an expression.
static void generateExpression(deExpression expression) {
  llCurrentLine = deExpressionGetLine(expression);
  deDatatype datatype = deExpressionGetDatatype(expression);
  deDatatypeType type = deDatatypeGetType(datatype);
  bool isSigned = deDatatypeGetType(datatype) == DE_TYPE_INT;
  deExpressionType exprType = deExpressionGetType(expression);
  switch (exprType) {
    case DE_EXPR_INTEGER:
      pushInteger(expression);
      break;
    case DE_EXPR_RANDUINT:
      generateRandomUint(expression);
      break;
    case DE_EXPR_FLOAT:
      pushFloat(expression);
      break;
    case DE_EXPR_BOOL:
      if (deExpressionBoolVal(expression)) {
        push(datatype, "true", false);
      } else {
        push(datatype, "false", false);
      }
      break;
    case DE_EXPR_STRING:
      generateStringExpression(expression);
      break;
    case DE_EXPR_IDENT:
      generateIdentExpression(expression);
      break;
    case DE_EXPR_ARRAY:
      generatePushArray(expression);
      break;
    case DE_EXPR_MODINT:
      generateModintExpression(expression);
      break;
    case DE_EXPR_ADDTRUNC:
      generateBinaryExpression(expression, "add");
      break;
    case DE_EXPR_SUBTRUNC:
      generateBinaryExpression(expression, "sub");
      break;
    case DE_EXPR_MULTRUNC:
      generateBinaryExpression(expression, "mul");
      break;
    case DE_EXPR_ADD:
      if (type == DE_TYPE_FLOAT) {
        generateBinaryExpression(expression, "fadd");
      } else if ((type == DE_TYPE_ARRAY || type == DE_TYPE_STRING) && exprType == DE_EXPR_ADD) {
        generateConcatExpression(expression);
      } else if (deUnsafeMode) {
        generateBinaryExpression(expression, "add");
      } else {
        generateBinaryExpressionWithOverflow(expression);
      }
      break;
    case DE_EXPR_SUB:
      if (type == DE_TYPE_FLOAT) {
        generateBinaryExpression(expression, "fsub");
      } else {
        if (deUnsafeMode) {
          generateBinaryExpression(expression, "sub");
        } else {
          generateBinaryExpressionWithOverflow(expression);
        }
      }
      break;
    case DE_EXPR_MUL:
      if (type == DE_TYPE_FLOAT) {
        generateBinaryExpression(expression, "fmul");
      } else {
        if (deUnsafeMode) {
          generateBinaryExpression(expression, "mul");
        } else {
          generateBinaryExpressionWithOverflow(expression);
        }
      }
      break;
    case DE_EXPR_DIV:
      if (type == DE_TYPE_FLOAT) {
        generateBinaryExpression(expression, "fdiv");
      } else {
        if (isSigned) {
          generateBinaryExpression(expression, "sdiv");
        } else {
          generateBinaryExpression(expression, "udiv");
        }
      }
      break;
    case DE_EXPR_MOD:
      if (type == DE_TYPE_FLOAT) {
        generateBinaryExpression(expression, "frem");
      } else {
        generateModExpression(expression);
      }
      break;
    case DE_EXPR_AND:
      generateLogicalAndExpression(expression);
      break;
    case DE_EXPR_BITAND:
      generateBinaryExpression(expression, "and");
      break;
    case DE_EXPR_OR:
      generateLogicalOrExpression(expression);
      break;
    case DE_EXPR_BITOR:
      generateBinaryExpression(expression, "or");
      break;
    case DE_EXPR_XOR:
    case DE_EXPR_BITXOR:
      generateBinaryExpression(expression, "xor");
      break;
    case DE_EXPR_BITNOT:
    case DE_EXPR_NOT:
      generateComplementExpression(expression);
      break;
    case DE_EXPR_EXP:
      generateExpExpression(expression);
      break;
    case DE_EXPR_IN: {
      deSignature signature = deExpressionGetSignature(expression);
      utAssert(signature != deSignatureNull);
      generateOperatorOverloadCall(expression, signature);
      break;
    }
    case DE_EXPR_SHL:
    case DE_EXPR_SHR:
    case DE_EXPR_ROTL:
    case DE_EXPR_ROTR:
      generateShiftOrRotateExpression(expression);
      break;
    case DE_EXPR_LT: {
      deDatatype subType = deExpressionGetDatatype(deExpressionGetFirstExpression(expression));
      if (deDatatypeIsFloat(subType)) {
        generateRelationalExpression(expression, "fcmp olt");
      } else if (deDatatypeGetType(subType) == DE_TYPE_INT) {
        generateRelationalExpression(expression, "icmp slt");
      } else {
        generateRelationalExpression(expression, "icmp ult");
      }
      break;
    }
    case DE_EXPR_LE: {
      deDatatype subType = deExpressionGetDatatype(deExpressionGetFirstExpression(expression));
      if (deDatatypeIsFloat(subType)) {
        generateRelationalExpression(expression, "fcmp ole");
      } else if (deDatatypeGetType(subType) == DE_TYPE_INT) {
        generateRelationalExpression(expression, "icmp sle");
      } else {
        generateRelationalExpression(expression, "icmp ule");
      }
      break;
    }
    case DE_EXPR_GT: {
      deDatatype subType = deExpressionGetDatatype(deExpressionGetFirstExpression(expression));
      if (deDatatypeIsFloat(subType)) {
        generateRelationalExpression(expression, "fcmp ogt");
      } else if (deDatatypeGetType(subType) == DE_TYPE_INT) {
        generateRelationalExpression(expression, "icmp sgt");
      } else {
        generateRelationalExpression(expression, "icmp ugt");
      }
      break;
    }
    case DE_EXPR_GE: {
      deDatatype subType = deExpressionGetDatatype(deExpressionGetFirstExpression(expression));
      if (deDatatypeIsFloat(subType)) {
        generateRelationalExpression(expression, "fcmp oge");
      } else if (deDatatypeGetType(subType) == DE_TYPE_INT) {
        generateRelationalExpression(expression, "icmp sge");
      } else {
        generateRelationalExpression(expression, "icmp uge");
      }
      break;
    }
    case DE_EXPR_EQUAL: {
      deDatatype subType = deExpressionGetDatatype(deExpressionGetFirstExpression(expression));
      if (deDatatypeIsFloat(subType)) {
        generateRelationalExpression(expression, "fcmp oeq");
      } else {
        generateRelationalExpression(expression, "icmp eq");
      }
      break;
    }
    case DE_EXPR_NOTEQUAL: {
      deDatatype subType = deExpressionGetDatatype(deExpressionGetFirstExpression(expression));
      if (deDatatypeIsFloat(subType)) {
        generateRelationalExpression(expression, "fcmp one");
      } else {
        generateRelationalExpression(expression, "icmp ne");
      }
      break;
    }
    case DE_EXPR_NEGATE:
    case DE_EXPR_NEGATETRUNC:
      generateNegateExpression(expression);
      break;
    case DE_EXPR_UNSIGNED:
      generateSignedCastExpression(expression, false);
      break;
    case DE_EXPR_SIGNED:
      generateSignedCastExpression(expression, true);
      break;
    case DE_EXPR_CAST:
      generateCastExpression(expression, false);
      break;
    case DE_EXPR_CASTTRUNC:
      generateCastExpression(expression, true);
      break;
    case DE_EXPR_SELECT:
      generateSelectExpression(expression);
      break;
    case DE_EXPR_CALL:
      generateCallExpression(expression);
      break;
    case DE_EXPR_INDEX:
      generateIndexExpression(expression);
      break;
    case DE_EXPR_SLICE:
      generateSliceExpression(expression);
      break;
    case DE_EXPR_SECRET:
    case DE_EXPR_REVEAL:
      generateExpression(deExpressionGetFirstExpression(expression));
      break;
    case DE_EXPR_EQUALS:
      generateAssignmentExpression(expression);
      break;
    case DE_EXPR_ADD_EQUALS:
    case DE_EXPR_SUB_EQUALS:
    case DE_EXPR_MUL_EQUALS:
    case DE_EXPR_DIV_EQUALS:
    case DE_EXPR_MOD_EQUALS:
    case DE_EXPR_AND_EQUALS:
    case DE_EXPR_OR_EQUALS:
    case DE_EXPR_XOR_EQUALS:
    case DE_EXPR_BITAND_EQUALS:
    case DE_EXPR_BITOR_EQUALS:
    case DE_EXPR_BITXOR_EQUALS:
    case DE_EXPR_EXP_EQUALS:
    case DE_EXPR_SHL_EQUALS:
    case DE_EXPR_SHR_EQUALS:
    case DE_EXPR_ROTL_EQUALS:
    case DE_EXPR_ROTR_EQUALS:
    case DE_EXPR_ADDTRUNC_EQUALS:
    case DE_EXPR_SUBTRUNC_EQUALS:
    case DE_EXPR_MULTRUNC_EQUALS:
      generateOpEqualsExpression(expression);
      break;
    case DE_EXPR_DOT:
      generateDotExpression(expression);
      break;
    case DE_EXPR_TUPLE:
      generateTupleExpression(expression);
      break;
    case DE_EXPR_NULL:
      pushNullValue(datatype);
      break;
    case DE_EXPR_NOTNULL:
      generateNotNullExpression(expression);
      break;
    case DE_EXPR_ARRAYOF:
    case DE_EXPR_TYPEOF:
    case DE_EXPR_UINTTYPE:
    case DE_EXPR_INTTYPE:
    case DE_EXPR_FLOATTYPE:
    case DE_EXPR_STRINGTYPE:
    case DE_EXPR_BOOLTYPE:
      pushDefaultValue(deExpressionGetDatatype(expression));
      break;
    case DE_EXPR_FUNCADDR:
      pushFunctionAddress(expression);
      break;
    case DE_EXPR_WIDTHOF: {
      deDatatype datatype = deExpressionGetDatatype(deExpressionGetFirstExpression(expression));
      pushSmallInteger(deDatatypeGetWidth(datatype), 32, false);
      break;
    }
    case DE_EXPR_ISNULL: {
      generateExpression(deExpressionGetFirstExpression(expression));
      llElement element = popElement(true);
      deDatatype datatype = llElementGetDatatype(element);
      uint32 refWidth = findClassRefWidth(datatype);
      uint32 value = printNewValue();
      llPrintf("icmp eq i%u %s, -1%s\n", refWidth, llElementGetName(element), locationInfo());
      pushValue(deBoolDatatypeCreate(), value, false);
      break;
    }
    case DE_EXPR_NAMEDPARAM:
    case DE_EXPR_AS:
    case DE_EXPR_LIST:
    case DE_EXPR_DOTDOTDOT:
      utExit("Unexpected expression type");
      break;
  }
}

// Check to see if there are any ifelse or else statements in this if-chain,
// after this statement.
static bool stayInIfChain(deStatement statement) {
  deStatement nextStatement = deStatementGetNextBlockStatement(statement);
  if (nextStatement == deStatementNull) {
    return false;
  }
  deStatementType type = deStatementGetType(nextStatement);
  return type == DE_STATEMENT_ELSEIF || type == DE_STATEMENT_ELSE;
}

// Determine if the block's last statement is a return.
static bool blockEndsInReturn(deBlock subBlock) {
  deStatement lastStatement = deBlockGetLastStatement(subBlock);
  if (lastStatement == deStatementNull) {
    return false;
  }
  deStatementType type = deStatementGetType(lastStatement);
  return type == DE_STATEMENT_RETURN || type == DE_STATEMENT_THROW;
}

// Generate instructions for the if statement.
static utSym generateIfStatement(deStatement statement, utSym startLabel) {
  utSym doneLabel = newLabel("ifDone");
  utSym nextClauseLabel = startLabel;
  bool lastTime;
  do {
    deStatementType type = deStatementGetType(statement);
    // If this is a terminating else clause, we have no condition to print, so
    // the body label should be the same as nextClauseLabel.
    utSym ifBodyLabel = nextClauseLabel;
    if (type == DE_STATEMENT_IF || type == DE_STATEMENT_ELSEIF) {
      printLabel(nextClauseLabel);
    }
    lastTime = !stayInIfChain(statement);
    if (lastTime) {
      nextClauseLabel = doneLabel;
    } else {
      nextClauseLabel = newLabel("ifClause");
    }
    deBlock subBlock = deStatementGetSubBlock(statement);
    if (type == DE_STATEMENT_IF || type == DE_STATEMENT_ELSEIF) {
      ifBodyLabel = newLabel("ifBody");
      generateExpression(deStatementGetExpression(statement));
      freeElements(false);
      llElement condition = popElement(true);
      char *location = locationInfo();
      llPrintf("  br i1 %s, label %%%s, label %%%s%s\n",
          llElementGetName(condition), utSymGetName(ifBodyLabel),
          utSymGetName(nextClauseLabel), location);
    }
    utSym blockEndLabel = generateBlockStatements(subBlock, ifBodyLabel);
    if (!blockEndsInReturn(subBlock)) {
      printLabel(blockEndLabel);
      jumpTo(doneLabel);
    }
    statement = deStatementGetNextBlockStatement(statement);
  } while (!lastTime);
  return doneLabel;
}

// The switch statement switched on a variable type.  Generate the case that was
// selected at compile-time.
static utSym generateSelectedSwitchCase(deStatement statement, utSym startLabel) {
  deBlock subBlock = deStatementGetSubBlock(statement);
  deStatement caseStatement;
  deForeachBlockStatement(subBlock, caseStatement) {
    if (deStatementInstantiated(caseStatement)) {
      return generateBlockStatements(deStatementGetSubBlock(caseStatement), startLabel);
    }
  } deEndBlockStatement;
  return startLabel;
}

// Generate instructions for the switch statement.
static utSym generateSwitchStatement(deStatement statement, utSym startLabel) {
  printLabel(startLabel);
  generateExpression(deStatementGetExpression(statement));
  // Keep the result of the switch expression from being freed until after the
  // entire select statement is done, by increasing llNumLocalsNeedingFree to
  // include the temps we just generated.
  uint32 numNeedsFreeLocals = llNumLocalsNeedingFree;
  llNumLocalsNeedingFree = llNeedsFreePos;
  llElement target = popElement(true);
  utSym doneLabel = newLabel("switchDone");
  utSym defaultLabel = newLabel("default");
  utSym nextCaseLabel = utSymNull;
  deBlock subBlock = deStatementGetSubBlock(statement);
  deStatement caseStatement;
  deForeachBlockStatement(subBlock, caseStatement) {
    if (deStatementInstantiated(caseStatement)) {
      utSym caseBodyLabel;
      if (deStatementGetType(caseStatement) == DE_STATEMENT_DEFAULT) {
        caseBodyLabel = defaultLabel;
      } else {
        caseBodyLabel = newLabel("caseBody");
      }
      deExpression expression = deStatementGetExpression(caseStatement);
      if (expression != deExpressionNull) {
        deStatement nextStatement = deStatementGetNextBlockStatement(caseStatement);
        deExpression caseExpression;
        deForeachExpressionExpression(expression, caseExpression) {
          printLabel(nextCaseLabel);
          if (deExpressionGetNextExpression(caseExpression) != deExpressionNull) {
            nextCaseLabel = newLabel("case");
          } else if (deStatementGetNextBlockStatement(caseStatement) != deStatementNull) {
            if (deStatementGetType(nextStatement) == DE_STATEMENT_DEFAULT) {
              nextCaseLabel = defaultLabel;
            } else {
              nextCaseLabel = newLabel("case");
            }
          } else {
            nextCaseLabel = doneLabel;
          }
          generateExpression(caseExpression);
          llElement value = popElement(true);
          if (llDatatypeIsArray(llElementGetDatatype(value))) {
            generateArrayComparison(target, value, RN_EQUAL);
          } else {
            generateComparison(target, value, "icmp eq");
          }
          // Only free temp variables created in the comparison, not the switch expression.
          freeElements(false);
          llElement result = popElement(true);
          llPrintf("  br i1 %s, label %%%s, label %%%s\n",
              llElementGetName(result), utSymGetName(caseBodyLabel), utSymGetName(nextCaseLabel));
        } deEndExpressionExpression;
      }
      printLabel(caseBodyLabel);
      deBlock caseBlock = deStatementGetSubBlock(caseStatement);
      utSym blockEndLabel = generateBlockStatements(caseBlock, utSymNull);
      if (!blockEndsInReturn(caseBlock)) {
        printLabel(blockEndLabel);
        jumpTo(doneLabel);
      }
    }
  } deEndBlockStatement;
  // Restore the number of locals needing to be freed.
  llNumLocalsNeedingFree = numNeedsFreeLocals;
  return doneLabel;
}

// Generate a do-while or while statement.
static utSym generateDoWhileStatement(deStatement statement, utSym startLabel) {
  deStatement prevStatement = deStatementGetPrevBlockStatement(statement);
  if (prevStatement != deStatementNull && deStatementGetType(prevStatement) == DE_STATEMENT_DO) {
    // Already generated the while statement in this case.
    return startLabel;
  }
  deStatementType type = deStatementGetType(statement);
  utSym loopLabel = startLabel;
  if (startLabel == utSymNull) {
    loopLabel = newLabel("whileLoop");
    freeElements(false);
    jumpTo(loopLabel);
  }
  printLabel(loopLabel);
  if (type == DE_STATEMENT_DO) {
    utSym blockEndLabel = generateBlockStatements(deStatementGetSubBlock(statement), utSymNull);
    printLabel(blockEndLabel);
    // Advance to the while statement.
    statement = deStatementGetNextBlockStatement(statement);
  }
  generateExpression(deStatementGetExpression(statement));
  freeElements(false);
  llElement condition = popElement(true);
  deBlock whileBlock = deStatementGetSubBlock(statement);
  utSym doneLabel = newLabel("whileDone");
  if (whileBlock == deBlockNull) {
    // This is a do { ... } while(); loop. Jump to loop label if cond is true.
    llPrintf("  br i1 %s, label %%%s, label%%%s\n",
        llElementGetName(condition), utSymGetName(loopLabel), utSymGetName(doneLabel));
  } else {
    // This is a do { ... } while() { ... } loop, or a while() { ... } loop.
    // Jump to loopDone label if cond is false.
    utSym loopBodyLabel = newLabel("whileBody");
    llPrintf("  br i1 %s, label %%%s, label%%%s\n",
        llElementGetName(condition), utSymGetName(loopBodyLabel), utSymGetName(doneLabel));
    utSym blockEndLabel = generateBlockStatements(whileBlock, loopBodyLabel);
    printLabel(blockEndLabel);
    jumpTo(loopLabel);
  }
  return doneLabel;
}

// Generate a for-loop.  It is like a while loop with the following structure:
//   init
//   while (test) {
//     body
//     update
//   }
static utSym generateForStatement(deStatement statement, utSym startLabel) {
  deExpression expression = deStatementGetExpression(statement);
  deExpression init = deExpressionGetFirstExpression(expression);
  deExpression test = deExpressionGetNextExpression(init);
  deExpression update = deExpressionGetNextExpression(test);
  // Generate the init assignment.
  printLabel(startLabel);
  generateExpression(init);
  freeElements(false);
  // Generate the loop label.
  utSym loopLabel = newLabel("forLoop");
  jumpTo(loopLabel);
  printLabel(loopLabel);
  // Generate the test.
  generateExpression(test);
  freeElements(false);
  llElement condition = popElement(true);
  utSym forLoopBody = newLabel("forLoopBody");
  utSym forLoopDone = newLabel("forLoopDone");
  llPrintf("  br i1 %s, label %%%s, label%%%s\n",
      llElementGetName(condition), utSymGetName(forLoopBody), utSymGetName(forLoopDone));
  deBlock body = deStatementGetSubBlock(statement);
  utSym blockEndLabel = generateBlockStatements(body, forLoopBody);
  printLabel(blockEndLabel);
  generateExpression(update);
  freeElements(false);
  jumpTo(loopLabel);
  return forLoopDone;
}

// Print a string by calling runtime_puts.
static void callPuts(llElement string) {
  llDeclareRuntimeFunction("runtime_puts");
  llPrintf("  call void @runtime_puts(%s %s)\n",
      llGetTypeString(llElementGetDatatype(string), false), llElementGetName(string));
}

// Generate a print or throw statement.
static void generatePrintOrThrowStatement(deStatement statement, bool isPrint) {
  deExpression argument = deStatementGetExpression(statement);
  deExpression expression = deStatementGetExpression(statement);
  deString formatString = deFindPrintFormat(expression);
  llElement format = generateString(formatString);
  // Just initialize array element.  It is not used when throwing an exception.
  llElement array = format;
  if (isPrint) {
    array = allocateTempValue(deStringDatatypeCreate());
  }
  callSprintfOrThrow(array, format, argument, isPrint, true);
  if (isPrint) {
    llElement string = popElement(false);
    callPuts(string);
  }
}

// Generate a return statement.
static void generateReturnStatement(deStatement statement) {
  deExpression expression = deStatementGetExpression(statement);
  deFunctionType funcType = deFunctionGetType(deBlockGetOwningFunction(llCurrentScopeBlock));
  if (funcType == DE_FUNC_DESTRUCTOR) {
    generateCallToFreeFunc();
  }
  if (funcType == DE_FUNC_CONSTRUCTOR) {
    // This is a constructor.  Return self.
    freeElements(true);
    deVariable self = deBlockGetFirstVariable(llCurrentScopeBlock);
    deDatatype selfType = deVariableGetDatatype(self);
    utAssert(deDatatypeGetType(selfType) == DE_TYPE_CLASS);
    deClass theClass = deDatatypeGetClass(selfType);
    char *location = locationInfo();
    llPrintf("  ret i%u %s%s\n", deClassGetRefWidth(theClass), llGetVariableName(self), location);
  } else if (expression == deExpressionNull) {
    freeElements(true);
    char *location = locationInfo();
    llPrintf("  ret void%s\n", location);
  } else {
    generateExpression(expression);
    deDatatype returnType = deExpressionGetDatatype(expression);
    if (llDatatypePassedByReference(returnType)) {
      llElement *elementPtr = topOfStack();
      llElement retVal = createElement(returnType, "%.retVal", true);
      copyOrMoveElement(retVal, *elementPtr, false);
      freeElements(true);
      llPrintf("  ret void%s\n", locationInfo());
    } else {
      llElement element = popElement(true);
      if (isRefCounted(returnType)) {
        // Ref before freeing elements in case we are returning a local varialbe.
        refObject(element);
      }
      freeElements(true);
      llPrintf("  ret %s %s%s\n", llGetTypeString(returnType, false),
          llElementGetName(element), locationInfo());
    }
  }
}

// Generate a ref statement.  This does nothing if the object is not ref-counted.
static void generateRefOrUnrefStatement(deStatement statement) {
  deExpression expression = deStatementGetExpression(statement);
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (!deTclassRefCounted(deClassGetTclass(deDatatypeGetClass(datatype)))) {
      return;
  }
  generateExpression(expression);
  llElement element = popElement(true);
  if (deStatementGetType(statement) == DE_STATEMENT_REF) {
    refObject(element);
  } else {
    utAssert(deStatementGetType(statement) == DE_STATEMENT_UNREF);
    unrefObject(element);
  }
}

// Dump the statement about to be generated to a comment.
static void dumpStatementInComment(deStatement statement) {
  deString string = deMutableStringCreate();
  deDumpStatementNoSubBlock(string, statement);
  llPrintf("  ; %s", deStringGetCstr(string));
  deStringDestroy(string);
}

// Generate instructions for the statement.
static utSym generateStatement(deStatement statement, utSym label) {
  dumpStatementInComment(statement);
  llCurrentStatement = statement;
  llCurrentLine = deStatementGetLine(statement);
  deStatementType type = deStatementGetType(statement);
  deExpression expression = deStatementGetExpression(statement);
  switch (type) {
    case DE_STATEMENT_IF:
      label = generateIfStatement(statement, label);
      break;
    case DE_STATEMENT_ELSEIF:
    case DE_STATEMENT_ELSE:
      // Nothing to do: These are generated by the if statement.
      break;
    case DE_STATEMENT_SWITCH:
        label = generateSwitchStatement(statement, label);
        break;
    case DE_STATEMENT_TYPESWITCH:
      label = generateSelectedSwitchCase(statement, label);
      break;
    case DE_STATEMENT_DO:
    case DE_STATEMENT_WHILE:
      label = generateDoWhileStatement(statement, label);
      break;
    case DE_STATEMENT_FOR:
      label = generateForStatement(statement, label);
      break;
    case DE_STATEMENT_ASSIGN:
      printLabel(label);
      label = utSymNull;
      generateExpression(deStatementGetExpression(statement));
      break;
    case DE_STATEMENT_CALL:
      printLabel(label);
      label = utSymNull;
      generateExpression(deStatementGetExpression(statement));
      if (deExpressionGetDatatype(expression) != deNoneDatatypeCreate()) {
        popElement(false);
      }
      break;
    case DE_STATEMENT_PRINT:
      printLabel(label);
      label = utSymNull;
      generatePrintOrThrowStatement(statement, true);
      break;
    case DE_STATEMENT_THROW:
      printLabel(label);
      label = utSymNull;
      generatePrintOrThrowStatement(statement, false);
      break;
    case DE_STATEMENT_RETURN:
      printLabel(label);
      label = utSymNull;
      generateReturnStatement(statement);
      break;
    case DE_STATEMENT_CASE:
    case DE_STATEMENT_DEFAULT:
      utExit("Case or default in non-switch statement");
      break;
    case DE_STATEMENT_REF:
    case DE_STATEMENT_UNREF:
      printLabel(label);
      label = utSymNull;
      generateRefOrUnrefStatement(statement);
      break;
    case DE_STATEMENT_RELATION:
    case DE_STATEMENT_GENERATE:
    case DE_STATEMENT_APPENDCODE:
    case DE_STATEMENT_PREPENDCODE:
    case DE_STATEMENT_USE:
    case DE_STATEMENT_IMPORT:
    case DE_STATEMENT_IMPORTLIB:
    case DE_STATEMENT_IMPORTRPC:
      // Nothing to do.
      break;
    case DE_STATEMENT_YIELD:
      utExit("Not expecting to see a yield() statement during code generation");
    case DE_STATEMENT_FOREACH:
      utExit("Not expecting to see a foreach statement during code generation");
  }
  return label;
}

// Generate instructions for the block's statements.
static utSym generateBlockStatements(deBlock block, utSym label) {
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    if (deStatementInstantiated(statement)) {
      label = generateStatement(statement, label);
    }
  } deEndBlockStatement;
  return label;
}

// Reset LLVM local data on variables in the block.
static void resetBlock(deBlock block, deSignature signature) {
  uint32 xParam = 0;
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    llVariableSetInitialized(variable, false);
    if (signature != deSignatureNull && deVariableGetType(variable) == DE_VAR_PARAMETER) {
      deVariableSetInstantiated(variable, deSignatureParamInstantiated(signature, xParam));
    }
    xParam++;
  } deEndBlockVariable;
}

// Generate LLVM assembly code for a fully bound block.
static void generateBlockAssemblyCode(deBlock block, deSignature signature) {
  resetBlock(block, signature);
  // If this is an auto-generated function, like a destructor, turn off debug.
  bool savedDebugMode = llDebugMode;
  if (deBlockGetFilepath(block) == deFilepathNull) {
    llDebugMode = false;
  }
  llTmpValueBuffer[0] = '\0';
  if (signature != deSignatureNull) {
    llPath = utAllocString(deGetSignaturePath(signature));
    if (llDebugMode) {
      llGenerateSignatureTags(signature);
      llTag tag = llSignatureGetTag(signature);
      llBlockSetTag(block, tag);
    }
  } else {
    utAssert(block == deRootGetBlock(deTheRoot));
    llPath = utAllocString("");
  }
  llStackPos = 0;
  llCurrentScopeBlock = block;
  printFunctionHeader(block, signature);
  llLabelNum = 1;
  llLimitCheckFailedLabel = utSymNull;
  llBoundsCheckFailedLabel = utSymNull;
  generateBlockStatements(block, utSymNull);
  llPrintf("}\n\n");
  utFree(llPath);
  llCurrentScopeBlock = deBlockNull;
  llDebugMode = savedDebugMode;
  llDeclareNewTuples();
}

// Print header info.  Declare all the Rune runtime functions.
static void printHeader(void) {
#ifdef _WIN32
  char *triple = "target triple = \"x86_64-w64-windows-gnu\"";
#else
#ifdef MAKEFILE_BUILD
  // This is generated on my workstation by clang.
  char *triple = "target triple = \"x86_64-pc-linux-gnu\"";
#else
  // This is expected when using Blaze.
  char *triple = "target triple = \"x86_64-grtev4-linux-gnu\"";
#endif
#endif
  fprintf(llAsmFile, "; ModuleID = '%s'\n", llModuleName);
  fprintf(llAsmFile,
      "target datalayout = \"e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128\"\n"
      "%s\n\n"
      "%%struct.runtime_array = type {i64*, i64}\n",
      triple);
  fputs("%struct.runtime_bool = type { i32 }\n", llAsmFile);
}

// Generate LLVM assembly code.
void llGenerateLLVMAssemblyCode(char* fileName, bool debugMode) {
  llStackPos = 0;
  llStackAllocated = 32;
  llStack = utNewA(llElement, llStackAllocated);
  llNeedsFreePos  = 0;
  llNumLocalsNeedingFree = 0;
  llNeedsFreeAllocated = 32;
  llNeedsFree = utNewA(llElement, llNeedsFreeAllocated);
  llAsmFile = fopen(fileName, "w");
  if (llAsmFile == NULL) {
    deError(0, "Unable to write to %s", fileName);
  }
  llModuleName = utAllocString(utBaseName(fileName));
  llDebugMode = debugMode;
  llSize = "64";
  llSizeType = deUintDatatypeCreate(64);
  llSizeWidth = 64;
  deResetString();
  llTmpValueLen = 42;
  llTmpValuePos = 0;
  llTmpValueBuffer = utNewA(char, llTmpValueLen);
  llStart();
  printHeader();
  flushStringBuffer();
  llDeclareExternCFunctions();
  flushStringBuffer();
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  deFunction mainFunc = deBlockGetOwningFunction(rootBlock);
  deBindBlock(rootBlock, deFunctionGetFirstSignature(mainFunc), true);
  if (llDebugMode) {
    llTag tag = llGenerateMainTags();
    llBlockSetTag(rootBlock, tag);
  }
  llDeclareBlockGlobals(rootBlock);
  generateBlockAssemblyCode(rootBlock, deSignatureNull);
  flushStringBuffer();
  deSignature signature;
  deForeachRootSignature(deTheRoot, signature) {
    if (deSignatureInstantiated(signature)) {
      deBlock block = deSignatureGetBlock(signature);
      deFunction function = deBlockGetOwningFunction(block);
      if (block != rootBlock && deFunctionGetType(function) != DE_FUNC_ITERATOR &&
          deFunctionGetLinkage(function) != DE_LINK_EXTERN_C) {
        deBlock snapshot = deBlockNull;
        if (deFunctionNeedsUniquification(function)) {
          snapshot = deSaveBlockSnapshot(block);
        }
        deBindBlock(block, signature, true);
        deResetString();
        llDeclareBlockGlobals(block);
        generateBlockAssemblyCode(block, signature);
        flushStringBuffer();
        if (deFunctionNeedsUniquification(function)) {
          deRestoreBlockSnapshot(block, snapshot);
        }
      }
    }
  } deEndRootSignature;
  llWriteDeclarations();
  flushStringBuffer();
  fclose(llAsmFile);
  llStop();
  utFree(llNeedsFree);
  utFree(llStack);
  utFree(llModuleName);
  utFree(llTmpValueBuffer);
  llTmpValueLen = 0;
  llTmpValuePos = 0;
}
