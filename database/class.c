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

// Tclasses are templates.  Every class in Rune is a template.  Tclasses are
// called, just like functions, and each class signature results in a new
// constructor, but not always a new class type (class version, or Class).  The
// class type is bound to the types of the self.<variable> assignments made by
// the call to the constructor.  If the member type signature is different, it's
// a different class version.
//
// The returned datatype from a constructor points to the Class, not the
// class.  The generated class is not in the namespace.  It's variables are the
// members of the class initialized with self.<variable> = ... in the
// constructor.  Identifiers are created in the theClass block for data members
// and also identifiers are created bound to the methods and inner classes of
// the class.  This allows the theClass block to be used when binding directly.
//
// Scoping: there are only two scopes for now: local and global.  Member/method
// access is through the self variable, like Python.  In particular, local
// variables used in the class constructor are not visible to methods.  Like
// Python, methods do not see each other directly, and instead are accessed
// through the self variable.

#include "de.h"

utSym deToStringSym, deShowSym;

void deClassStart(void) {
  deToStringSym = utSymCreate("toString");
  deShowSym = utSymCreate("show");
}

void deClassStop(void) {
  // Nothing for now.
}

// Dump the class to the end of |string| for debugging purposes.
void deDumpTclassStr(deString string, deTclass tclass) {
  dePrintIndentStr(string);
  deStringSprintf(string, "class %s (0x%x) {\n", deTclassGetName(tclass), deTclass2Index(tclass));
  deDumpIndentLevel++;
  deDumpBlockStr(string, deFunctionGetSubBlock(deTclassGetFunction(tclass)));
  --deDumpIndentLevel;
  dePrintIndentStr(string);
  deStringPuts(string, "}\n");
}

// Dump the class to stdout for debugging purposes.
void deDumpTclass(deTclass tclass) {
  deString string = deMutableStringCreate();
  deDumpTclassStr(string, tclass);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Add the destroy method to the tclass.  By default, it just deletes the
// object, but code generators will be able to add more to it.
static void addDestroyMethod(deTclass tclass) {
  deBlock classBlock = deFunctionGetSubBlock(deTclassGetFunction(tclass));
  deLine line = deBlockGetLine(classBlock);
  utSym funcName = utSymCreate("destroy");
  deLinkage linkage = deFunctionGetLinkage(deTclassGetFunction(tclass));
  deFunction function = deFunctionCreate(deBlockGetFilepath(classBlock), classBlock,
      DE_FUNC_DESTRUCTOR, funcName, linkage, line);
  deBlock functionBlock = deFunctionGetSubBlock(function);
  // Add a self parameter.
  utSym paramName = utSymCreate("self");
  deVariableCreate(functionBlock, DE_VAR_PARAMETER, true, paramName, deExpressionNull, false, line);
}

// Create a new class object.  Add a destroy method.  The tclass is a child of
// its constructor function, essentially implementing inheritance through
// composition.
deTclass deTclassCreate(deFunction constructor, uint32 refWidth, deLine line) {
  deTclass tclass = deTclassAlloc();
  deTclassSetRefWidth(tclass, refWidth);
  deTclassSetLine(tclass, line);
  deFunctionInsertTclass(constructor, tclass);
  if (!deFunctionBuiltin(constructor)) {
    addDestroyMethod(tclass);
  }
  deRootAppendTclass(deTheRoot, tclass);
  return tclass;
}

// Determine if two signatures generate the same theClass.  This is true if the
// types for variables in the class constructor marked inTclassSignature have the
// same type.
static bool classSignaturesMatch(deSignature newSignature, deSignature oldSignature) {
  deFunction constructor = deSignatureGetFunction(newSignature);
  deVariable parameter;
  uint32 xParam = 0;
  deBlock block = deFunctionGetSubBlock(constructor);
  deForeachBlockVariable(block, parameter) {
    if (deVariableGetType(parameter) != DE_VAR_PARAMETER) {
      return true;
    }
    if (deVariableInTclassSignature(parameter)) {
      deDatatype newDatatype = deSignatureGetiType(newSignature, xParam);
      deDatatype oldDatatype = deSignatureGetiType(oldSignature, xParam);
      if (newDatatype != oldDatatype) {
        return false;
      }
    }
    xParam++;
  } deEndBlockVariable;
  return true;
}

// New theClass are only allocated for signatures that have different types for
// variables that are in the class signature.
// TODO: consider speeding this up with a hash table.
deClass findExistingClass(deSignature signature) {
  deTclass tclass = deFunctionGetTclass(deSignatureGetFunction(signature));
  if (!deTclassIsTemplate(tclass)) {
    return deTclassGetFirstClass(tclass);
  }
  deClass theClass;
  deForeachTclassClass(tclass, theClass) {
    deSignature otherSignature = deClassGetFirstSignature(theClass);
    if (classSignaturesMatch(signature, otherSignature)) {
      return theClass;
    }
  } deEndTclassClass;
  return deClassNull;
}

// Create a new class object.
static deClass classCreate(deTclass tclass) {
  deClass theClass = deClassAlloc();
  uint32 numClass = deTclassGetNumClasses(tclass) + 1;
  deClassSetNumber(theClass, numClass);
  deClassSetRefWidth(theClass, deTclassGetRefWidth(tclass));
  deTclassSetNumClasses(tclass, numClass);
  deFunction constructor = deTclassGetFunction(tclass);
  deFilepath filepath = deBlockGetFilepath(deFunctionGetSubBlock(constructor));
  deBlock subBlock = deBlockCreate(filepath, DE_BLOCK_CLASS, deTclassGetLine(tclass));
  deClassInsertSubBlock(theClass, subBlock);
  deTclassAppendClass(tclass, theClass);
  // Create a nextFree variable.
  deVariable nextFree = deVariableCreate(subBlock, DE_VAR_LOCAL, false, utSymCreate("nextFree"),
      deExpressionNull, true, 0);
  deVariableSetDatatype(nextFree, deUintDatatypeCreate(deTclassGetRefWidth(tclass)));
  deVariableSetInstantiated(nextFree, true);
  deRootAppendClass(deTheRoot, theClass);
  return theClass;
}

// Determine if the class matches the spec.
static bool classMatchesSpec(deClass theClass, deDatatypeArray tclassSpec) {
  deDatatype classType = deClassGetDatatype(theClass);
  uint32 numTypes = deDatatypeGetNumTypeList(classType);
  for (uint32 xType = 0; xType < numTypes; xType++) {
    if (deDatatypeGetiTypeList(classType, xType) != deDatatypeArrayGetiDatatype(tclassSpec, xType)) {
      return false;
    }
  }
  return true;
}

// Find an existing class matching the spec.
static deClass findTclassClassFromSpec(deTclass tclass, deDatatypeArray tclassSpec) {
  deClass theClass;
  deForeachTclassClass(tclass, theClass) {
    if (classMatchesSpec(theClass, tclassSpec)) {
      return theClass;
    }
  } deEndTclassClass;
  return deClassNull;
}

// Create a class from the spec.  Free |tclassSpec|.
static deClass createClassFromSpec(deTclass tclass, deDatatypeArray tclassSpec) {
  deClass theClass = classCreate(tclass);
  deDatatype datatype = deClassDatatypeCreateFromSpec(theClass, tclassSpec);
  deClassSetDatatype(theClass, datatype);
  return theClass;
}

// Find or create a class given the tclass spec.
deClass deTclassFindClassFromSpec(deTclass tclass, deDatatypeArray tclassSpec) {
  deClass theClass = findTclassClassFromSpec(tclass, tclassSpec);
  if (theClass != deClassNull) {
    return theClass;
  }
  return createClassFromSpec(tclass, deCopyDatatypeArray(tclassSpec));
}

// Create a class for the non-template Tclass if it does not yet exist.
deClass deTclassGetDefaultClass(deTclass tclass) {
  utAssert(!deTclassIsTemplate(tclass));
  deClass theClass = deTclassGetFirstClass(tclass);
  if (theClass == deClassNull) {
    theClass = classCreate(tclass);
    deClassSetDatatype(theClass, deClassDatatypeCreate(theClass));
  }
  return theClass;
}

// Create a new class object.
deClass deClassCreate(deTclass tclass, deSignature signature) {
  if (!deTclassIsTemplate(tclass)) {
    return deTclassGetDefaultClass(tclass);
  }
  deDatatypeArray tclassSpec = deFindSignatureTclassSpec(signature);
  return deTclassFindClassFromSpec(tclass, tclassSpec);
}

// Make a copy of the tclass in |destBlock|.
deTclass deCopyTclass(deTclass tclass, deFunction destConstructor) {
  return deTclassCreate(destConstructor, deTclassGetRefWidth(tclass), deTclassGetLine(tclass));
}

// Build a tuple expression for the class members.  Bind types as we go.
static deExpression buildClassTupleExpression(deBlock classBlock,
    deExpression selfExpr, bool showGenerated) {
  deExpression tupleExpr = deExpressionCreate(DE_EXPR_TUPLE, deExpressionGetLine(selfExpr));
  deDatatypeArray types = deDatatypeArrayAlloc();
  deVariable variable;
  deForeachBlockVariable(classBlock, variable) {
    if (!deVariableIsType(variable) && (showGenerated || !deVariableGenerated(variable))) {
      deDatatype datatype = deVariableGetDatatype(variable);
      if (deDatatypeConcrete(datatype)) {
        deDatatypeArrayAppendDatatype(types, datatype);
        deLine line = deVariableGetLine(variable);
        deExpression varExpr = deIdentExpressionCreate(deVariableGetSym(variable), line);
        deExpression newSelfExpr = deCopyExpression(selfExpr);
        deExpression dotExpr = deBinaryExpressionCreate(DE_EXPR_DOT, newSelfExpr, varExpr, line);
        deExpressionSetDatatype(dotExpr, datatype);
        deExpressionAppendExpression(tupleExpr, dotExpr);
      }
    }
  } deEndBlockVariable;
  deExpressionSetDatatype(tupleExpr, deTupleDatatypeCreate(types));
  return tupleExpr;
}

// Find the print format for the object tuple.
static deString findObjectPrintFormat(deExpression tupleExpr) {
  // Print to the end of deStringVal, and reset deStringPos afterwards.
  uint32 len = 42;
  uint32 pos = 0;
  char *format = utMakeString(len);
  format = deAppendToBuffer(format, &len, &pos, "{");
  bool firstTime = true;
  deExpression child;
  deForeachExpressionExpression(tupleExpr, child) {
    if (!firstTime) {
      format = deAppendToBuffer(format, &len, &pos, ", ");
    }
    firstTime = false;
    deExpression identExpr;
    if (deExpressionGetType(child) == DE_EXPR_CAST) {
      identExpr = deExpressionGetLastExpression(deExpressionGetLastExpression(child));
    } else {
      identExpr = deExpressionGetLastExpression(child);
    }
    format = deAppendToBuffer(format, &len, &pos, utSymGetName(deExpressionGetName(identExpr)));
    format = deAppendToBuffer(format, &len, &pos, " = ");
    format = deAppendOneFormatElement(format, &len, &pos, child);
  } deEndExpressionExpression;
  format = deAppendToBuffer(format, &len, &pos, "}");
  return deMutableCStringCreate(format);
}

// Add an if statement checking if self is 0.  If true, print null and return.
static void addCheckForNull(deBlock functionBlock, bool callPrint, deLine line) {
  deStatement ifStatement = deStatementCreate(functionBlock, DE_STATEMENT_IF, line);
  deExpression selfExpr = deIdentExpressionCreate(utSymCreate("self"), line);
  deExpression isNullExpr = deUnaryExpressionCreate(DE_EXPR_ISNULL, selfExpr, line);
  deStatementSetExpression(ifStatement, isNullExpr);
  deBlock subBlock = deBlockCreate(deBlockGetFilepath(functionBlock), DE_BLOCK_STATEMENT, line);
  deStatementInsertSubBlock(ifStatement, subBlock);
  deExpression stringExpr = deStringExpressionCreate(deMutableCStringCreate("null"), line);
  if (callPrint) {
    deStatement printStatement = deStatementCreate(subBlock, DE_STATEMENT_PRINT, line);
    deExpression listExpr = deExpressionCreate(DE_EXPR_LIST, line);
    deExpressionAppendExpression(listExpr, stringExpr);
    deStatementInsertExpression(printStatement, listExpr);
  }
  deStatement retState = deStatementCreate(subBlock, DE_STATEMENT_RETURN, line);
  if (!callPrint) {
    deStatementInsertExpression(retState, stringExpr);
  }
}

// Generate a default toString method for the class.
static deFunction generateDefaultMethod(deClass theClass, utSym funcName,
    bool showGenerated, bool callPrint) {
  deBlock classBlock = deClassGetSubBlock(theClass);
  deTclass tclass = deClassGetTclass(theClass);
  deLinkage linkage = deFunctionGetLinkage(deTclassGetFunction(tclass));
  deFunction function = deFunctionCreate(deBlockGetFilepath(classBlock), classBlock,
      DE_FUNC_PLAIN, funcName, linkage, 0);
  deBlock functionBlock = deFunctionGetSubBlock(function);
  // Add a self parameter.
  deLine line = deBlockGetLine(classBlock);
  utSym paramName = utSymCreate("self");
  deVariableCreate(functionBlock, DE_VAR_PARAMETER, true, paramName, deExpressionNull, false, line);
  addCheckForNull(functionBlock, callPrint, line);
  deExpression selfExpr = deIdentExpressionCreate(utSymCreate("self"), line);
  deExpressionSetDatatype(selfExpr, deClassDatatypeCreate(theClass));
  deExpression tupleExpr = buildClassTupleExpression(classBlock, selfExpr, showGenerated);
  deString format = findObjectPrintFormat(tupleExpr);
  if (showGenerated) {
    // If showing all fields, format like Foo(32) = {...}.
    deExpression uintExpr = deExpressionCreate(DE_EXPR_UINTTYPE, line);
    deExpressionSetWidth(uintExpr, deTclassGetRefWidth(tclass));
    deExpression castExpr = deBinaryExpressionCreate(DE_EXPR_CASTTRUNC, uintExpr,
        deCopyExpression(selfExpr), line);
    deExpressionInsertExpression(tupleExpr, castExpr);
    char *text = utSprintf("%s(%%u) = %s", deTclassGetName(tclass), deStringGetCstr(format));
    format = deMutableCStringCreate(text);
  }
  deExpression formatExpr = deStringExpressionCreate(format, line);
  deExpression modExpr = deBinaryExpressionCreate(DE_EXPR_MOD, formatExpr, tupleExpr, line);
  if (!callPrint) {
    deStatement retStatement = deStatementCreate(functionBlock, DE_STATEMENT_RETURN, line);
    deStatementInsertExpression(retStatement, modExpr);
  } else {
    deStatement statement = deStatementCreate(functionBlock, DE_STATEMENT_PRINT, line);
    deExpression listExpr = deBinaryExpressionCreate(DE_EXPR_LIST, modExpr,
        deStringExpressionCreate(deMutableCStringCreate("\n"), line), line);
    deStatementInsertExpression(statement, listExpr);
  }
  return function;
}

// Generate a default toString method for the class.
deFunction deGenerateDefaultToStringMethod(deClass theClass) {
  return generateDefaultMethod(theClass, deToStringSym, false, false);
}

// Generate a default print method for the class.
deFunction deGenerateDefaultShowMethod(deClass theClass) {
  return generateDefaultMethod(theClass, deShowSym, true, true);
}

// Determine if the class has a toString method.  If so, we use it to print
// class objects.
deFunction deClassFindMethod(deClass theClass, utSym methodSym) {
  deBlock block = deClassGetSubBlock(theClass);
  deIdent ident = deBlockFindIdent(block, methodSym);
  if (ident == deIdentNull || deIdentGetType(ident) != DE_IDENT_FUNCTION) {
    return deFunctionNull;
  }
  return deIdentGetFunction(ident);
}

// Some functions, like tclass functions need to continue existing even if they
// are never constructed, since they are used in datatypes.  Instead of
// destroying them, destroy most of their contents.  This will destroy
// relations and any statements and functions generated by them.  If we do not
// do this, we will have many statements trying to operate on classes that were
// never clearly defined, since we do not know how the constructor was called.
//
// This situation is common when developing modules with unit tests that may
// import other modules, but not instantiate all classes in those modules.
void deDestroyTclassContents(deTclass tclass) {
  deFunction function = deTclassGetFunction(tclass);
  deBlock oldSubBlock = deFunctionGetSubBlock(function);
  deFilepath filepath = deBlockGetFilepath(oldSubBlock);
  deLine line = deBlockGetLine(oldSubBlock);
  deBlock newSubBlock = deBlockCreate(filepath, DE_BLOCK_FUNCTION, line);
  deBlockDestroy(oldSubBlock);
  deFunctionInsertSubBlock(function, newSubBlock);
  deRelation relation;
  deSafeForeachTclassParentRelation(tclass, relation) {
    deRelationDestroy(relation);
  } deEndSafeTclassParentRelation;
  deSafeForeachTclassChildRelation(tclass, relation) {
    deRelationDestroy(relation);
  } deEndSafeTclassChildRelation;
}

// Create a signature for the default method so it becomes part of the debug
// binary.  This is useful in gdb during debugging.
static void createSignature(deClass theClass, deFunction method) {
  deLine line = deTclassGetLine(deClassGetTclass(theClass));
  deDatatypeArray parameterTypes = deDatatypeArrayAlloc();
  deDatatype selfType = deClassDatatypeCreate(theClass);
  deDatatypeArrayAppendDatatype(parameterTypes, selfType);
  deSignature signature = deLookupSignature(method, parameterTypes);
  if (signature != deSignatureNull) {
    deSignatureSetInstantiated(signature, true);
    return;
  }
  utAssert(signature == deSignatureNull);
  signature = deSignatureCreate(method, parameterTypes, line);
  deParamspec paramspec = deSignatureGetiParamspec(signature, 0);
  deParamspecSetInstantiated(paramspec, true);
  deSignatureSetInstantiated(signature, true);
  deQueueSignature(signature);
}

// Generate default methods for a class if they do not exist.  This is the
// toPrint method, and if in debug mode, a show method.
void deGenerateDefaultMethods(deClass theClass) {
  deBlock classBlock = deClassGetSubBlock(theClass);
  deIdent ident = deBlockFindIdent(classBlock, deToStringSym);
  if (ident == deIdentNull || deIdentGetType(ident) == DE_IDENT_UNDEFINED) {
    deFunction toStringMethod = deGenerateDefaultToStringMethod(theClass);
    if (deDebugMode) {
      createSignature(theClass, toStringMethod);
    }
  }
  ident = deBlockFindIdent(classBlock, deShowSym);
  if (ident == deIdentNull || deIdentGetType(ident) == DE_IDENT_UNDEFINED) {
    deFunction showMethod = deGenerateDefaultShowMethod(theClass);
    if (deDebugMode) {
      createSignature(theClass, showMethod);
    }
  }
}
