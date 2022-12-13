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

// Allocate the self object for this constructor.  Also change return statements
// to return self.  Bind all new/modified statements.
static void generateConstructorString(deClass theClass) {
  deStringPos = 0;
  char *theClassPath = utAllocString(deGetBlockPath(deClassGetSubBlock(theClass), true));
  char* selfType = deDatatypeGetTypeString(deClassGetDatatype(theClass));
  uint32 refWidth = deClassGetRefWidth(theClass);
  deSprintToString(
      "appendcode {\n"
      "  func %1$s_allocate() {\n"
      "    if %1$s_firstFree != ~0u%3$u {\n"
      "      object = <%2$s>%1$s_firstFree\n"
      "      %1$s_firstFree = %1$s_nextFree[<u%3$u>object]\n"
      "    } else {\n"
      "      if %1$s_used == %1$s_allocated {\n"
      "        %1$s_allocated <<= 1u%3$u\n",
      theClassPath, selfType, refWidth);
  deVariable variable;
  deForeachBlockVariable(deClassGetSubBlock(theClass), variable) {
    deSprintToString(
          "        %1$s_%2$s.resize(%1$s_allocated)\n",
        theClassPath, deVariableGetName(variable));
  } deEndBlockVariable;
  deSprintToString(
      "      }\n"
      "      object = <%2$s>%1$s_used\n"
      "      %1$s_used += 1u%3$u\n"
      "    }\n"
      "    %1$s_nextFree[<u%3$u>object] = 1u%3$u\n",
      theClassPath, selfType, refWidth);
  deSprintToString(
      "    return object\n"
      "  }\n"
      "}\n");
  utFree(theClassPath);
}

// Free the self object in the destructor.
static void generateDestructorString(deClass theClass) {
  deStringPos = 0;
  char* theClassPath =
      utAllocString(deGetBlockPath(deClassGetSubBlock(theClass), true));
  char* self = "object";
  deSprintToString("appendcode {\n");
  deSprintToString("  func %1$s_free(object) {\n", theClassPath);
  uint32 refWidth = deClassGetRefWidth(theClass);
  bool firstTime = true;
  deVariable variable;
  deForeachBlockVariable(deClassGetSubBlock(theClass), variable) {
    if (!firstTime) {
      deVariable globalArrayVar = deVariableGetGlobalArrayVariable(variable);
      char* zero = deDatatypeGetDefaultValueString(deVariableGetDatatype(variable));
      deSprintToString("    %1$s[<u%3$u>object] = %2$s\n",
                     deVariableGetName(globalArrayVar), zero, refWidth);
    }
    firstTime = false;
  } deEndBlockVariable;
  deSprintToString(
      "    %1$s_nextFree[<u%3$u>%2$s] = %1$s_firstFree\n"
      "    %1$s_firstFree = <u%3$u>%2$s\n",
      theClassPath, self, refWidth);
  deSprintToString(
      "  }\n"
      "}\n");
  utFree(theClassPath);
}

// Add global variables and arrays to manage the theClass's memory.
static void generateRootBlockArrays(deClass theClass) {
  deStringPos = 0;
  deBlock block = deClassGetSubBlock(theClass);
  char *path = utAllocString(deGetBlockPath(block, true));
  deSprintToString(
      "prependcode {\n"
      "  %1$s_allocated = 1u%2$u\n"
      "  %1$s_used = 0u%2$u\n"
      "  %1$s_firstFree = ~0u%2$u\n",
      path, deClassGetRefWidth(theClass));
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    utAssert(deVariableInstantiated(variable) && !deVariableIsType(variable));
    char *defaultValue = deDatatypeGetDefaultValueString(deVariableGetDatatype(variable));
    deSprintToString("  %1$s_%2$s = [%3$s]\n",
        path, deVariableGetName(variable), defaultValue);
  } deEndBlockVariable;
  deAddString("}\n");
  utFree(path);
}

// Bind the new statements added to the start of the block.
static void bindNewStatements(deBlock scopeBlock, deStatement originalFirstStatement) {
  deStatement statement = deBlockGetFirstStatement(scopeBlock);
  while (statement != originalFirstStatement) {
    deBindNewStatement(scopeBlock, statement);
    statement = deStatementGetNextBlockStatement(statement);
  }
}

// set the global array variables used to provide memory for data members.
static void setGlobalArrayVariables(deClass theClass) {
  deBlock block = deClassGetSubBlock(theClass);
  deBlock globalBlock = deRootGetBlock(deTheRoot);
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    char *path = deGetBlockPath(block, true);
    utSym name = utSymCreateFormatted("%s_%s", path, deVariableGetName(variable));
    deIdent ident = deBlockFindIdent(globalBlock, name);
    utAssert(ident != deIdentNull && deIdentGetType(ident) == DE_IDENT_VARIABLE);
    deVariable globalVar = deIdentGetVariable(ident);
    deVariableSetGlobalArrayVariable(variable, globalVar);
  } deEndBlockVariable;
}

// Add statements to the constructor and to the root block for managing memory.
static void allocateSelfInConstructor(deClass theClass) {
  generateRootBlockArrays(theClass);
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  deStatement originalFirstStatement = deBlockGetFirstStatement(rootBlock);
  deGenerating = true;
  deParseString(deStringVal, rootBlock);
  bindNewStatements(rootBlock, originalFirstStatement);
  generateConstructorString(theClass);
  deParseString(deStringVal, rootBlock);
  deGenerating = false;
  deFunction allocateFunc = deBlockGetLastFunction(rootBlock);
  deDatatypeArray parameterTypes = deDatatypeArrayAlloc();
  deLine line = deTclassGetLine(deClassGetTclass(theClass));
  deSignature signature = deSignatureCreate(allocateFunc, parameterTypes, line);
  deSignatureSetInstantiated(signature, true);
  deSignatureSetReturnType(signature, deClassGetDatatype(theClass));
  deBindBlock(deFunctionGetSubBlock(allocateFunc), signature, false);
  setGlobalArrayVariables(theClass);
}

// Add memory management statements to the destructor at the end.
static void freeSelfInDestructor(deClass theClass) {
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  generateDestructorString(theClass);
  deGenerating = true;
  deParseString(deStringVal, rootBlock);
  deGenerating = false;
  deFunction freeFunc = deBlockGetLastFunction(rootBlock);
  deDatatypeArray parameterTypes = deDatatypeArrayAlloc();
  deDatatype selfType = deClassDatatypeCreate(theClass);
  deDatatypeArrayAppendDatatype(parameterTypes, selfType);
  deSignature signature = deSignatureCreate(freeFunc, parameterTypes, 0);
  deSignatureSetInstantiated(signature, true);
  deSignatureSetReturnType(signature, deNoneDatatypeCreate());
}

// Generate code for referencing and defreferencing the class.
static void generateRefAndDerefString(deClass theClass) {
  deStringPos = 0;
  char* theClassPath = deGetBlockPath(deClassGetSubBlock(theClass), true);
  deSprintToString(
      "appendcode {\n"
      "  func %1$s_ref(object) {\n"
      "    if !isnull(object) && %1$s_nextFree[<u%2$u>object] != ~0u%2$u {\n"
      "      %1$s_nextFree[<u%2$u>object] += 1u%2$u\n"
      "    }\n"
      "  }\n"
      "\n"
      "  func %1$s_unref(object) {\n"
      "    if !isnull(object) && %1$s_nextFree[<u%2$u>object] != ~0u%2$u {\n"
      "      %1$s_nextFree[<u%2$u>object] !-= 1u%2$u\n"
      "      if %1$s_nextFree[<u%2$u>object] == 0u%2$u {\n"
      "        object.destroy()\n"
      "      }\n"
      "    }\n"
      "  }\n"
      "}\n"
      , theClassPath, deClassGetRefWidth(theClass));
}

// Add ref() and deref() methods to the class.  Return the unref function.
static void addRefAndDeref(deClass theClass) {
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  generateRefAndDerefString(theClass);
  deGenerating = true;
  deParseString(deStringVal, rootBlock);
  deGenerating = false;
  deFunction unrefFunc = deBlockGetLastFunction(rootBlock);
  deDatatypeArray parameterTypes = deDatatypeArrayAlloc();
  deDatatype selfType = deClassDatatypeCreate(theClass);
  deDatatypeArrayAppendDatatype(parameterTypes, selfType);
  deSignature signature = deSignatureCreate(unrefFunc, parameterTypes, 0);
  deSignatureSetInstantiated(signature, true);
  deSignatureSetReturnType(signature, deNoneDatatypeCreate());
  // deBindBlock(deFunctionGetSubBlock(unrefFunc), signature, false);
  deFunction refFunc = deFunctionGetPrevBlockFunction(unrefFunc);
  signature = deSignatureCreate(refFunc, parameterTypes, 0);
  deSignatureSetInstantiated(signature, true);
  deSignatureSetReturnType(signature, deNoneDatatypeCreate());
  // deBindBlock(deFunctionGetSubBlock(refFunc), signature, false);
}

// Find the tclass' destructor.
static deFunction findTclassDestructor(deTclass tclass) {
  deBlock block = deFunctionGetSubBlock(deTclassGetFunction(tclass));
  utSym name = utSymCreate("destroy");
  deIdent ident = deBlockFindIdent(block, name);
  utAssert(ident != deIdentNull && deIdentGetType(ident) == DE_IDENT_FUNCTION);
  return deIdentGetFunction(ident);
}

// Call final method if it exists in class destructors.
static void callFinalInDestructors(void) {
  deTclass tclass;
  deForeachRootTclass(deTheRoot, tclass) {
    if (deTclassHasFinalMethod(tclass)) {
      deFunction destructor = findTclassDestructor(tclass);
      deBlock block = deFunctionGetSubBlock(destructor);
      deLine line = deFunctionGetLine(destructor);
      deStatement callStatement = deStatementCreate(block, DE_STATEMENT_CALL, line);

      deExpression finalExpr = deIdentExpressionCreate(utSymCreate("final"), line);
      deExpression selfExpr = deIdentExpressionCreate(utSymCreate("self"), line);
      deExpression dotExpr = deBinaryExpressionCreate(DE_EXPR_DOT, selfExpr, finalExpr, line);
      deExpression paramList = deExpressionCreate(DE_EXPR_LIST, line);
      deExpression callExpr = deBinaryExpressionCreate(DE_EXPR_CALL, dotExpr, paramList, line);
      deStatementInsertExpression(callStatement, callExpr);
      // Move the statement to the start of the block.
      deBlockRemoveStatement(block, callStatement);
      deBlockInsertStatement(block, callStatement);
    }
  } deEndRootTclass;
}

// Add code to constructors to allocate a new object, and add variables in the
// root block needed to manage object memory.  We use structure-of-array memory
// layout, so there is a global array per data member of the class.
void deAddMemoryManagement(void) {
  callFinalInDestructors();
  deClass theClass;
  deForeachRootClass(deTheRoot, theClass) {
    if (deClassBound(theClass)) {
      allocateSelfInConstructor(theClass);
      freeSelfInDestructor(theClass);
      if (deTclassRefCounted(deClassGetTclass(theClass))) {
        addRefAndDeref(theClass);
      }
    }
  } deEndRootClass;
}
