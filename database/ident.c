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

// Dump the identifier to the end of |string| for debugging purposes.
void deDumpIdentStr(deString string, deIdent ident) {
  dePrintIndentStr(string);
  deStringSprintf(string, "ident %s (0x%x) -> ", deIdentGetName(ident), deIdent2Index(ident));
  switch (deIdentGetType(ident)) {
    case DE_IDENT_FUNCTION: {
      deFunction function = deIdentGetFunction(ident);
      if (function != deFunctionNull) {
        deStringSprintf(string, "%s %x\n",deGetFunctionTypeName(deFunctionGetType(function)),
          deFunction2Index(function));
      } else {
        deStringSprintf(string, "<undefined>\n");
      }
      break;
    }
    case DE_IDENT_VARIABLE:
      deStringSprintf(string, "variable %x\n", deVariable2Index(deIdentGetVariable(ident)));
      break;
    case DE_IDENT_UNDEFINED:
      deStringSprintf(string, "<undefined>\n");
  }
}

// Dump the identifier to stdout for debugging purposes.
void deDumpIdent(deIdent ident) {
  deString string = deMutableStringCreate();
  deDumpIdentStr(string, ident);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Copy the identifier to all classes of the tclass.
static void copyIdentToClasses(deTclass tclass, deIdent ident) {
  deClass theClass;
  deForeachTclassClass(tclass, theClass) {
    deCopyIdent(ident, deClassGetSubBlock(theClass));
  } deEndTclassClass;
}

// Create a new identifier object that lies in the block's hash table of
// identifiers.
deIdent deIdentCreate(deBlock block, deIdentType type, utSym name, deLine line) {
  if (block != deBlockNull) {
    // Operator identifiers are not in any block hash table.
    deIdent oldIdent = deBlockFindIdent(block, name);
    if (oldIdent != deIdentNull) {
      // Undefined identifiers can be created during binding.  If this has not
      // a function or variable, update it.
      if (deIdentGetType(oldIdent) == DE_IDENT_UNDEFINED) {
        if (type != DE_IDENT_UNDEFINED) {
          deQueueEventBlockedStateBindings(deIdentGetUndefinedEvent(oldIdent));
        }
        deIdentSetType(oldIdent, type);
        return oldIdent;
      }
      deError(line, "Tried to create an identifier '%s' that already exists on the block",
          utSymGetName(name));
    }
  }
  deIdent ident = deIdentAlloc();
  deIdentSetType(ident, type);
  deIdentSetSym(ident, name);
  if (block != deBlockNull) {
    deBlockAppendIdent(block, ident);
    if (deUseNewBinder && deIdentGetType(ident) == DE_IDENT_FUNCTION &&
        deBlockGetType(block) == DE_BLOCK_FUNCTION) {
      // The new binder prefers importing function identifiers as soon as available.
      deFunction function = deBlockGetOwningFunction(block);
      if (deFunctionGetType(function) == DE_FUNC_CONSTRUCTOR) {
        copyIdentToClasses(deFunctionGetTclass(function), ident);
      }
    }
  }
  return ident;
}

// Create an undefined identifier.  This is so we can trigger binding events
// when it becomes defined.
deIdent deUndefinedIdentCreate(deBlock block, utSym name) {
  return deIdentCreate(block, DE_IDENT_UNDEFINED, name, deBlockGetLine(block));
}

// Create an identifier for a function.
deIdent deFunctionIdentCreate(deBlock block, deFunction function, utSym name) {
  deIdent ident = deIdentCreate(block, DE_IDENT_FUNCTION, name, deFunctionGetLine(function));
  deFunctionAppendIdent(function, ident);
  return ident;
}

// Find the identifier in the scope block, or in the module block.  If not found
// in the module block, look in the global scope.
deIdent deFindIdent(deBlock scopeBlock, utSym name) {
  deIdent ident = deBlockFindIdent(scopeBlock, name);
  if (ident != deIdentNull) {
    return ident;
  }
  deFilepath filepath = deBlockGetFilepath(scopeBlock);
  if (filepath == deFilepathNull) {
    // Builtin classes have no filepath.
    return deIdentNull;
  }
  deBlock moduleBlock = deFilepathGetModuleBlock(filepath);
  ident = deBlockFindIdent(moduleBlock, name);
  if (ident != deIdentNull) {
    return ident;
  }
  // Some identifiers, like idents for built-in classes, are in the global scope.
  ident = deBlockFindIdent(deRootGetBlock(deTheRoot), name);
  if (ident != deIdentNull) {
    return ident;
  }
  return deIdentNull;
}

// Find the datatype of the identifier.  If a variable has not yet been set, it
// will return deDatatypeNull.
deDatatype deGetIdentDatatype(deIdent ident) {
  switch (deIdentGetType(ident)) {
    case DE_IDENT_FUNCTION: {
      return deFunctionDatatypeCreate(deIdentGetFunction(ident));
    }
    case DE_IDENT_VARIABLE:
      return deVariableGetDatatype(deIdentGetVariable(ident));
    case DE_IDENT_UNDEFINED:
      return deDatatypeNull;
      break;
  }
  return deDatatypeNull;  // Dummy return.
}

// Return the sub-block of the identifier, if it has one.
deBlock deIdentGetSubBlock(deIdent ident) {
  switch (deIdentGetType(ident)) {
    case DE_IDENT_FUNCTION:
      return deFunctionGetSubBlock(deIdentGetFunction(ident));
    case DE_IDENT_VARIABLE:
    case DE_IDENT_UNDEFINED:
      return deBlockNull;
  }
  return deBlockNull;  // Dummy return.
}

// Return the line number of the identifier.
deLine deIdentGetLine(deIdent ident) {
  switch (deIdentGetType(ident)) {
    case DE_IDENT_FUNCTION:
      return deFunctionGetLine(deIdentGetFunction(ident));
    case DE_IDENT_VARIABLE:
      return deVariableGetLine(deIdentGetVariable(ident));
    case DE_IDENT_UNDEFINED:
      return deLineNull;
  }
  return 0;  // Dummy return.
}

// Find an identifier from the path expression.
static deIdent findIdentFromPath(deBlock scopeBlock, deExpression pathExpression) {
  if (deExpressionGetType(pathExpression) == DE_EXPR_AS) {
    pathExpression = deExpressionGetFirstExpression(pathExpression);
  }
  if (deExpressionGetType(pathExpression) == DE_EXPR_IDENT) {
    return deBlockFindIdent(scopeBlock, deExpressionGetName(pathExpression));
  }
  utAssert(deExpressionGetType(pathExpression) == DE_EXPR_DOT);
  deExpression subPathExpression = deExpressionGetFirstExpression(pathExpression);
  deExpression identExpression = deExpressionGetNextExpression(subPathExpression);
  utAssert(deExpressionGetType(identExpression) == DE_EXPR_IDENT);
  deIdent ident = findIdentFromPath(scopeBlock, subPathExpression);
  if (ident == deIdentNull) {
    return deIdentNull;
  }
  scopeBlock = deIdentGetSubBlock(ident);
  if (scopeBlock == deBlockNull) {
    return deIdentNull;
  }
  return deBlockFindIdent(scopeBlock, deExpressionGetName(identExpression));
}

// Find an identifier from the path expression.  |scopeBlock| if not null, is searched first.
deIdent deFindIdentFromPath(deBlock scopeBlock, deExpression pathExpression) {
  deIdent ident = findIdentFromPath(scopeBlock, pathExpression);
  if (ident != deIdentNull) {
    return ident;
  }
  // Try to find it in the global scope.
  return findIdentFromPath(deRootGetBlock(deTheRoot), pathExpression);
}

// Rename the identifier.  Also change the sym in its identifier expressions.
void deRenameIdent(deIdent ident, utSym newName) {
  deBlock scopeBlock = deIdentGetBlock(ident);
  deBlockRemoveIdent(scopeBlock, ident);
  deIdentSetSym(ident, newName);
  deBlockAppendIdent(scopeBlock, ident);
  deExpression expression;
  deForeachIdentExpression(ident, expression) {
    utAssert(deExpressionGetType(expression) == DE_EXPR_IDENT);
    deExpressionSetName(expression, newName);
  } deEndIdentExpression;
}

// Find the identifier for the block owning this identifier.
deIdent deFindIdentOwningIdent(deIdent ident) {
  deBlock block = deIdentGetBlock(ident);
  deBlock owningBlock = deBlockGetOwningBlock(block);
  if (owningBlock == deBlockNull) {
    return deIdentNull;
  }
  utSym name = utSymNull;
  switch (deBlockGetType(block)) {
    case DE_BLOCK_FUNCTION:
      name = deFunctionGetSym(deBlockGetOwningFunction(block));
      break;
    case DE_BLOCK_STATEMENT:
      utExit("Statement blocks do not have identifiers");
      break;
    case DE_BLOCK_CLASS: {
      deFunction function = deTclassGetFunction(deClassGetTclass(deBlockGetOwningClass(block)));
      name = deFunctionGetSym(function);
      break;
    }
  }
  return deBlockFindIdent(owningBlock, name);
}

// Return a path expression to the function.
deExpression deCreateIdentPathExpression(deIdent ident) {
  deLine line = deIdentGetLine(ident);
  utSym sym = deIdentGetSym(ident);
  deExpression identExpr = deIdentExpressionCreate(sym, line);
  deIdent owningIdent = deFindIdentOwningIdent(ident);
  if (owningIdent == deIdentNull) {
    return identExpr;
  }
  deExpression prefixExpr = deCreateIdentPathExpression(owningIdent);
  return deBinaryExpressionCreate(DE_EXPR_DOT, prefixExpr, identExpr, line);
}

// Copy the identifier to the destination block.  The caller must ensure the
// identifier does not already exist on |destBlock|.
deIdent deCopyIdent(deIdent ident, deBlock destBlock) {
  deIdentType type = deIdentGetType(ident);
  utAssert(type == DE_IDENT_FUNCTION);
  deIdent newIdent = deIdentCreate(destBlock, type, deIdentGetSym(ident), deLineNull);
  deFunctionAppendIdent(deIdentGetFunction(ident), newIdent);
  return newIdent;
}

// Determine if this identifier represents a module or package.
bool deIdentIsModuleOrPackage(deIdent ident) {
  if (deIdentGetType(ident) != DE_IDENT_FUNCTION) {
    return false;
  }
  deFunction function = deIdentGetFunction(ident);
  deFunctionType type = deFunctionGetType(function);
  return type == DE_FUNC_PACKAGE || type == DE_FUNC_MODULE;
}
