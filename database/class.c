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
  deDatatype tclassType = deTclassDatatypeCreate(tclass);
  deTclassSetDatatype(tclass, tclassType);
  deTclassSetLine(tclass, line);
  deFunctionInsertTclass(constructor, tclass);
  if (!deFunctionBuiltin(constructor)) {
    addDestroyMethod(tclass);
  }
  deRootAppendTclass(deTheRoot, tclass);
  return tclass;
}

// We allow datatypes to be different in a specific case: if newDatatype is
// NULL and oldDatatype is an instance of that TCLASS.
static bool datatypesCompatible(deDatatype newDatatype, deDatatype oldDatatype) {
  if (newDatatype == oldDatatype) {
    return true;
  }
  deDatatypeType newType = deDatatypeGetType(newDatatype);
  deDatatypeType oldType = deDatatypeGetType(oldDatatype);
  if (newType != DE_TYPE_NULL || oldType != DE_TYPE_CLASS) {
    return false;
  }
  deTclass oldTclass = deClassGetTclass(deDatatypeGetClass(oldDatatype));
  return oldTclass == deDatatypeGetTclass(newDatatype);
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
      if (!datatypesCompatible(newDatatype, oldDatatype)) {
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
  deDatatype selfType = deClassDatatypeCreate(theClass);
  deClassSetDatatype(theClass, selfType);
  // Create a nextFree variable.
  deVariable nextFree = deVariableCreate(subBlock, DE_VAR_LOCAL, false, utSymCreate("nextFree"),
      deExpressionNull, true, 0);
  deVariableSetDatatype(nextFree, deUintDatatypeCreate(deTclassGetRefWidth(tclass)));
  deVariableSetInstantiated(nextFree, true);
  deRootAppendClass(deTheRoot, theClass);
  return theClass;
}

// Create a new class object.
deClass deClassCreate(deTclass tclass, deSignature signature) {
  if (deSignatureGetClass(signature) != deClassNull) {
    return deSignatureGetClass(signature);
  }
  deClass theClass = findExistingClass(signature);
  if (theClass != deClassNull) {
    return theClass;
  }
  return classCreate(tclass);
}

// Determine if there are any template parameters, in which case it is safe to
// generate a default class.
static bool tclassHasTemplateParameters(deTclass tclass) {
  deBlock block = deFunctionGetSubBlock(deTclassGetFunction(tclass));
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    if (deVariableInTclassSignature(variable)) {
      return true;
    }
  } deEndBlockVariable;
  return false;
}

// If we already created the default class, return it.  Otherwise, check that we
// have no template parameters, and if so, create the default class.  Return
// null if we do have template parameters.
deClass deTclassGetDefaultClass(deTclass tclass) {
  if (tclassHasTemplateParameters(tclass)) {
    return deClassNull;
  }
  return deTclassGetFirstClass(tclass);
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

// Generate a default toString method for the class.
static deFunction generateDefaultMethod(deClass theClass, char *name,
    bool showGenerated, bool callPrint) {
  deBlock classBlock = deClassGetSubBlock(theClass);
  utSym funcName = utSymCreate(name);
  deTclass tclass = deClassGetTclass(theClass);
  deLinkage linkage = deFunctionGetLinkage(deTclassGetFunction(tclass));
  deFunction function = deFunctionCreate(deBlockGetFilepath(classBlock), classBlock,
      DE_FUNC_PLAIN, funcName, linkage, 0);
  deBlock functionBlock = deFunctionGetSubBlock(function);
  // Add a self parameter.
  deLine line = deBlockGetLine(classBlock);
  utSym paramName = utSymCreate("self");
  deVariableCreate(functionBlock, DE_VAR_PARAMETER, true, paramName, deExpressionNull, false, line);
  deExpression selfExpr = deIdentExpressionCreate(utSymCreate("self"), line);
  deExpressionSetDatatype(selfExpr, deClassDatatypeCreate(theClass));
  deExpression tupleExpr = buildClassTupleExpression(classBlock, selfExpr, showGenerated);
  deString format = findObjectPrintFormat(tupleExpr);
  if (showGenerated) {
    // If showing all fields, format like Foo(32) = {...}.
    deExpression uintExpr = deExpressionCreate(DE_EXPR_UINTTYPE, line);
    deExpressionSetWidth(uintExpr, deTclassGetRefWidth(tclass));
    deExpression castExpr = deBinaryExpressionCreate(DE_EXPR_CAST, uintExpr,
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
  return generateDefaultMethod(theClass, "toString", false, false);
}

// Generate a default print method for the class.
deFunction deGenerateDefaultShowMethod(deClass theClass) {
  return generateDefaultMethod(theClass, "show", true, true);
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
