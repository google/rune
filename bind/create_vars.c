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
  deStatementInsertVariable(statement, var);
}

// Create variables when we find assignment expressions that assign to an identifier.
static void createExpressionVariables(deBlock scopeBlock, deExpression expr) {
  if (deExpressionGetType(expr) == DE_EXPR_EQUALS) {
    deExpression target = deExpressionGetFirstExpression(expr);
    if (deExpressionGetType(target) == DE_EXPR_IDENT) {
      createVariableIfMissing(scopeBlock, target);
    }
  }
  deExpression child;
  deForeachExpressionExpression(expr, child) {
    createExpressionVariables(scopeBlock, child);
  } deEndExpressionExpression;
}

// Create variables in the block from assignment expressions of the form:
//   IDENT = expression
// Then, recurse into child functions.
static void createBlockVariables(deBlock scopeBlock, deBlock block) {
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deExpression expr = deStatementGetExpression(statement);
    if (expr != deExpressionNull) {
      createExpressionVariables(scopeBlock, expr);
    }
    deBlock subBlock = deStatementGetSubBlock(statement);
    if (subBlock != deBlockNull) {
      createBlockVariables(scopeBlock, subBlock);
    }
  } deEndBlockStatement;
  deFunction func;
  deForeachBlockFunction(block, func) {
    deBlock subBlock = deFunctionGetSubBlock(func);
    createBlockVariables(subBlock, subBlock);
  } deEndBlockFunction;
}

// Create variables from assignment expressions.  Create variables as we
// descend the tree of functions so that globals exist when we see an
// assignment expression that could create a local variable otherwise.
void deCreateLocalAndGlobalVariables(void) {
  deBlock scopeBlock = deRootGetBlock(deTheRoot);
  createBlockVariables(scopeBlock, scopeBlock);
}
