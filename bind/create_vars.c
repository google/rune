//  Copyright 2023 Google LLC.
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

// Create a variable with the name of the identifier expression, if it does not already exist.
static void createVariableIfMissing(deBlock scopeBlock, deExpression expr) {
  utSym name = deExpressionGetName(expr);
  deIdent ident = deFindIdent(scopeBlock, name);
  if (ident != deIdentNull) {
    return;
  }
  deStatement statement = deFindExpressionStatement(expr);
  deVariable var = deVariableCreate(scopeBlock, DE_VAR_LOCAL, false, name, deExpressionNull,
      deStatementGenerated(statement), deExpressionGetLine(expr));
  if (deRootGetBlock(deTheRoot) == scopeBlock) {
    deVariableSetInstantiated(var, true);
  }
}

// Determine if this is a member access in a constructor.
static bool isMemberAccessInConstructor(deBlock scopeBlock, deExpression expr) {
  deClass theClass = deBlockGetOwningClass(scopeBlock);
  if (theClass == deClassNull || deExpressionGetType(expr) != DE_EXPR_DOT) {
    return false;
  }
  deExpression leftExpr = deExpressionGetFirstExpression(expr);
  if (deExpressionGetType(leftExpr) != DE_EXPR_IDENT) {
    return false;
  }
  deFunction constructor =  deTemplateGetFunction(deClassGetTemplate(theClass));
  deVariable selfVar = deBlockGetFirstVariable(deFunctionGetSubBlock(constructor));
  return deVariableGetSym(selfVar) == deExpressionGetName(leftExpr);
}

// Create variables when we find assignment expressions that assign to an identifier.
static void createExpressionVariables(deBlock scopeBlock, deExpression expr) {
  if (deExpressionGetType(expr) == DE_EXPR_EQUALS) {
    deExpression target = deExpressionGetFirstExpression(expr);
    if (deExpressionGetType(target) == DE_EXPR_IDENT) {
      createVariableIfMissing(scopeBlock, target);
    }
    if (isMemberAccessInConstructor(scopeBlock, target)) {
      createVariableIfMissing(scopeBlock, deExpressionGetLastExpression(target));
    }
  }
  deExpression child;
  deForeachExpressionExpression(expr, child) {
    createExpressionVariables(scopeBlock, child);
  } deEndExpressionExpression;
}

// Create any variables declared by the statement.
void deCreateStatementVariables(deBlock scopeBlock, deStatement statement) {
  deExpression expr = deStatementGetExpression(statement);
  if (expr != deExpressionNull) {
    createExpressionVariables(scopeBlock, expr);
  }
  deBlock subBlock = deStatementGetSubBlock(statement);
  if (subBlock != deBlockNull) {
    deCreateBlockVariables(scopeBlock, subBlock);
  }
}

// Create variables in the block from assignment expressions of the form:
//   IDENT = expression
// Then, recurse into child functions.
void deCreateBlockVariables(deBlock scopeBlock, deBlock block) {
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deCreateStatementVariables(scopeBlock, statement);
  } deEndBlockStatement;
  deFunction func;
  deForeachBlockFunction(block, func) {
    deBlock subBlock = deFunctionGetSubBlock(func);
    deCreateBlockVariables(subBlock, subBlock);
  } deEndBlockFunction;
}

// Create variables from assignment expressions.  Create variables as we
// descend the tree of functions so that globals exist when we see an
// assignment expression that could create a local variable otherwise.
void deCreateLocalAndGlobalVariables(void) {
  deBlock scopeBlock = deRootGetBlock(deTheRoot);
  deCreateBlockVariables(scopeBlock, scopeBlock);
}
