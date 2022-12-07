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

// Return the name of the function.
char *deFunctionGetName(deFunction function) {
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  if (deFunctionGetSubBlock(function) == rootBlock) {
    return "main";
  }
  return deIdentGetName(deFunctionGetFirstIdent(function));
}

// Return a name for the function type.
char *deGetFunctionTypeName(deFunctionType type) {
  switch (type) {
    case DE_FUNC_PLAIN:  // Includes methods.
      return "function";
    case DE_FUNC_UNITTEST:  // Includes methods.
      return "unittest";
    case DE_FUNC_CONSTRUCTOR:
      return "constructor";
    case DE_FUNC_DESTRUCTOR:
      return "destructor";
    case DE_FUNC_PACKAGE:  // Initializes all modules in the package.
      return "package";
    case DE_FUNC_MODULE:  // Initializes the module.
      return "module";
    case DE_FUNC_ITERATOR:
      return "iterator";
    case DE_FUNC_OPERATOR:
      return "operator";
    case DE_FUNC_FINAL:
      return "final";
    case DE_FUNC_STRUCT:
      return "struct";
    case DE_FUNC_ENUM:
      return "enum";
    case DE_FUNC_GENERATOR:
      return "generator";
  }
  return NULL;
}

// Dump the function to the end of |string| for debugging purposes.
void deDumpFunctionStr(deString string, deFunction function) {
  dePrintIndentStr(string);
  deStringSprintf(string, "%s %s (0x%x) {\n", deGetFunctionTypeName(deFunctionGetType(function)),
      deFunctionGetName(function), deFunction2Index(function));
  deDumpIndentLevel++;
  deDumpBlockStr(string, deFunctionGetSubBlock(function));
  --deDumpIndentLevel;
  dePrintIndentStr(string);
  deStringPuts(string, "}\n");
}

// Dump the function to stdout for debugging purposes.
void deDumpFunction(deFunction function) {
  deString string = deMutableStringCreate();
  deDumpFunctionStr(string, function);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Create a new function.
deFunction deFunctionCreate(deFilepath filepath, deBlock block, deFunctionType type, utSym name,
    deLinkage linkage, deLine line) {
  deFunction function = deFunctionAlloc();
  deFunctionSetType(function, type);
  deFunctionSetLinkage(function, linkage);
  deFunctionSetLine(function, line);
  if (block != deBlockNull) {
    deBlockAppendFunction(block, function);
    deFunctionIdentCreate(block, function, name);
  }
  deFunctionSetExtern(function, linkage == DE_LINK_EXTERN_C || linkage == DE_LINK_EXTERN_RPC);
  deBlock subBlock = deBlockCreate(filepath, DE_BLOCK_FUNCTION, line);
  // Assume it can return until we learn otherwise.  This is only an issue when
  // evaluating recursive functions.
  deBlockSetCanReturn(subBlock, true);
  deFunctionInsertSubBlock(function, subBlock);
  deRootAppendFunction(deTheRoot, function);
  return function;
}

// Make a copy of the function in |destBlock|.
deFunction deCopyFunction(deFunction function, deBlock destBlock) {
  deBlock subBlock = deFunctionGetSubBlock(function);
  deFunctionType type = deFunctionGetType(function);
  deFunction newFunction = deFunctionCreate(deBlockGetFilepath(destBlock), destBlock, type,
      deFunctionGetSym(function), deFunctionGetLinkage(function), deFunctionGetLine(function));
  deBlock newBlock = deCopyBlock(subBlock);
  deFunctionInsertSubBlock(newFunction, newBlock);
  if (type == DE_FUNC_CONSTRUCTOR) {
    deCopyTclass(deFunctionGetTclass(function), newFunction);
  }
  return newFunction;
}

// Append a call statement to the module initialization function in the root block.
void deInsertModuleInitializationCall(deFunction moduleFunc) {
  deIdent ident = deFunctionGetFirstIdent(moduleFunc);
  deExpression pathExpr = deCreateIdentPathExpression(ident);
  deBlock block = deFunctionGetSubBlock(moduleFunc);
  char* text = utSprintf("%s()\n", deFunctionGetName(moduleFunc));
  deLine line = deLineCreate(deBlockGetFilepath(block), text, strlen(text), 0);
  deExpression emptyParamsExpr = deExpressionCreate(DE_EXPR_LIST, line);
  deExpression callExpression = deBinaryExpressionCreate(DE_EXPR_CALL, pathExpr, emptyParamsExpr, line);
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  deStatement statement = deStatementCreate(rootBlock, DE_STATEMENT_CALL, line);
  deStatementInsertExpression(statement, callExpression);
  // Move the statement to after the last initialization call.
  deStatement lastInitializer = deRootGetLastInitializerStatement(deTheRoot);
  if (lastInitializer != deStatementNull) {
    deBlockRemoveStatement(rootBlock, statement);
    deBlockInsertAfterStatement(rootBlock, lastInitializer, statement);
  }
  deRootSetLastInitializerStatement(deTheRoot, statement);
}

// Prepend a call statement to |childFunction| at the end of |function|.
// |childFunction| will be called with no parameters.
void deFunctionPrependFunctionCall(deFunction function, deFunction childFunction) {
  deIdent ident = deFunctionGetFirstIdent(childFunction);
  deExpression pathExpr = deCreateIdentPathExpression(ident);
  deBlock block = deFunctionGetSubBlock(function);
  char* text = utSprintf("%s()\n", deFunctionGetName(childFunction));
  deLine line = deLineCreate(deBlockGetFilepath(block), text, strlen(text), 0);
  deExpression emptyParamsExpr = deExpressionCreate(DE_EXPR_LIST, line);
  deExpression callExpression = deBinaryExpressionCreate(DE_EXPR_CALL, pathExpr, emptyParamsExpr, line);
  deStatement statement = deStatementCreate(block, DE_STATEMENT_CALL, line);
  // Move the statement to the start of the block.
  deBlockRemoveStatement(block, statement);
  deBlockInsertStatement(block, statement);
  deStatementInsertExpression(statement, callExpression);
}

// Append a call statement to |childFunction| at the end of |function|.
// |childFunction| will be called with no parameters.
void deFunctionAppendFunctionCall(deFunction function, deFunction childFunction) {
  deIdent ident = deFunctionGetFirstIdent(childFunction);
  deExpression pathExpr = deCreateIdentPathExpression(ident);
  deBlock block = deFunctionGetSubBlock(function);
  char* text = utSprintf("%s()\n", deFunctionGetName(childFunction));
  deLine line = deLineCreate(deBlockGetFilepath(block), text, strlen(text), 0);
  deExpression emptyParamsExpr = deExpressionCreate(DE_EXPR_LIST, line);
  deExpression callExpression = deBinaryExpressionCreate(DE_EXPR_CALL, pathExpr, emptyParamsExpr, line);
  deStatement statement = deStatementCreate(block, DE_STATEMENT_CALL, line);
  deStatementInsertExpression(statement, callExpression);
}

// Declare an iterator.
deFunction deIteratorFunctionCreate(deBlock block, utSym name, utSym selfName,
    deLinkage linkage, deLine line) {
  deFilepath filepath = deBlockGetFilepath(block);
  deFunction iterator = deFunctionCreate(filepath, block, DE_FUNC_ITERATOR,
      name, linkage, line);
  deBlock subBlock = deFunctionGetSubBlock(iterator);
  deVariableCreate(subBlock, DE_VAR_PARAMETER, false, selfName, deExpressionNull, false, line);
  return iterator;
}

// Create an overloaded operator.
deFunction deOperatorFunctionCreate(deBlock block, deExpressionType opType, deLine line) {
  deFilepath filepath = deBlockGetFilepath(block);
  utSym name = deBlockCreateUniqueName(block, utSymCreate(deExpressionTypeGetName(opType)));
  deFunction function = deFunctionCreate(filepath, block, DE_FUNC_OPERATOR,
      name, DE_LINK_PACKAGE, line);
  deOperator operator = deRootFindOperator(deTheRoot, opType);
  if (operator == deOperatorNull) {
    operator = deOperatorAlloc();
    deOperatorSetType(operator, opType);
    deRootAppendOperator(deTheRoot, operator);
  }
  deOperatorAppendFunction(operator, function);
  return function;
}
