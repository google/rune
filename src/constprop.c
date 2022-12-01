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

// Perform constant propagation on a block.  Only propagate constants that can
// be derived just looking at the block, and any const variables within the
// block's scope.
#include "de.h"

// Forward declaration for recursion.
static bool propagateExpressionConstants(deBlock scopeBlock, deExpression expression, deBigint modulus);

// Perform constant propagation for all child expressions.  Return true if all
// children are constant.
static bool propagateChildConstants(deBlock scopeBlock, deExpression expression, deBigint modulus) {
  bool allConst = true;
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    if (!propagateExpressionConstants(scopeBlock, child, modulus)) {
      allConst = false;
    }
  } deEndExpressionExpression;
  return allConst;
}

// Compute the modulus, and if constant, perform constant propagation on the
// modular expression using the modulus.
static bool propagateModularConstants(deBlock scopeBlock, deExpression expression) {
  deExpression valueExpr = deExpressionGetFirstExpression(expression);
  deExpression modulusExpr = deExpressionGetLastExpression(expression);
  if (!propagateExpressionConstants(scopeBlock, modulusExpr, deBigintNull)) {
    return false;
  }
  utAssert(deExpressionGetType(modulusExpr) == DE_EXPR_INTEGER);
  return propagateExpressionConstants(scopeBlock, valueExpr, deExpressionGetBigint(modulusExpr));
}

// Propagate constants in the expression.  If |modulus| is not deBigintNull,
// then use modular arithmetic when propagating constants.  Return true if the
// expression is constant.
//
// This is just the beginning of constant propagation.  So far, only negation is
// actually propagated.
// TODO(waywardgeek): Flesh out constant propagation.
static bool propagateExpressionConstants(deBlock scopeBlock, deExpression expression, deBigint modulus) {
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_RANDUINT:
      return false;
    case DE_EXPR_INTEGER:
    case DE_EXPR_FLOAT:
    case DE_EXPR_BOOL:
    case DE_EXPR_STRING:
      return true;
    case DE_EXPR_ARRAY:
      propagateChildConstants(scopeBlock, expression, modulus);
      return false;
    case DE_EXPR_MODINT:
      return propagateModularConstants(scopeBlock, expression);
    case DE_EXPR_ADD:
    case DE_EXPR_SUB:
    case DE_EXPR_MUL:
    case DE_EXPR_DIV:
    case DE_EXPR_MOD:
    case DE_EXPR_BITAND:
    case DE_EXPR_AND:
    case DE_EXPR_OR :
    case DE_EXPR_XOR:
    case DE_EXPR_BITOR :
    case DE_EXPR_BITXOR:
    case DE_EXPR_EXP:
    case DE_EXPR_SHL:
    case DE_EXPR_SHR:
    case DE_EXPR_ROTL:
    case DE_EXPR_ROTR:
    case DE_EXPR_ADDTRUNC:
    case DE_EXPR_SUBTRUNC:
    case DE_EXPR_MULTRUNC:
    case DE_EXPR_BITNOT:
    case DE_EXPR_LT:
    case DE_EXPR_LE:
    case DE_EXPR_GT:
    case DE_EXPR_GE:
    case DE_EXPR_EQUAL:
    case DE_EXPR_NOTEQUAL:
    case DE_EXPR_NOT:
    case DE_EXPR_CAST:
    case DE_EXPR_CASTTRUNC:
    case DE_EXPR_SELECT:
    case DE_EXPR_CALL:
    case DE_EXPR_INDEX:
    case DE_EXPR_SLICE:
    case DE_EXPR_SECRET:
    case DE_EXPR_REVEAL:
    case DE_EXPR_EQUALS:
    case DE_EXPR_ADD_EQUALS:
    case DE_EXPR_SUB_EQUALS:
    case DE_EXPR_MUL_EQUALS:
    case DE_EXPR_DIV_EQUALS:
    case DE_EXPR_MOD_EQUALS:
    case DE_EXPR_AND_EQUALS:
    case DE_EXPR_OR_EQUALS :
    case DE_EXPR_XOR_EQUALS:
    case DE_EXPR_BITAND_EQUALS:
    case DE_EXPR_BITOR_EQUALS :
    case DE_EXPR_BITXOR_EQUALS:
    case DE_EXPR_EXP_EQUALS:
    case DE_EXPR_SHL_EQUALS:
    case DE_EXPR_SHR_EQUALS:
    case DE_EXPR_ROTL_EQUALS:
    case DE_EXPR_ROTR_EQUALS:
    case DE_EXPR_ADDTRUNC_EQUALS:
    case DE_EXPR_SUBTRUNC_EQUALS:
    case DE_EXPR_MULTRUNC_EQUALS:
    case DE_EXPR_DOT:
    case DE_EXPR_DOTDOTDOT:
    case DE_EXPR_LIST:
    case DE_EXPR_TUPLE:
    case DE_EXPR_AS:
    case DE_EXPR_IN:
    case DE_EXPR_NULL:
    case DE_EXPR_NOTNULL:
    case DE_EXPR_FUNCADDR:
    case DE_EXPR_ARRAYOF:
    case DE_EXPR_TYPEOF:
    case DE_EXPR_UNSIGNED:
    case DE_EXPR_SIGNED:
    case DE_EXPR_WIDTHOF:
    case DE_EXPR_ISNULL:
    case DE_EXPR_UINTTYPE:
    case DE_EXPR_INTTYPE:
    case DE_EXPR_FLOATTYPE:
    case DE_EXPR_STRINGTYPE:
    case DE_EXPR_BOOLTYPE:
    case DE_EXPR_NAMEDPARAM:
    case DE_EXPR_NEGATETRUNC:
    case DE_EXPR_IDENT:
      // TODO: Write code to evaluate these expressions.
      propagateChildConstants(scopeBlock, expression, modulus);
      return false;
    case DE_EXPR_NEGATE:
      if (!propagateChildConstants(scopeBlock, expression, modulus)) {
        return false;
      }
      deValue value = deEvaluateExpression(scopeBlock, expression, modulus);
      if (value == deValueNull) {
        return false;
      }
      deSetExpressionToValue(expression, value);
      return true;
  }
  return true;
}

// Propagate constants in the block.  This is done post-binding and directly
// modifies expressions.
void deConstantPropagation(deBlock scopeBlock, deBlock block) {
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deExpression expression = deStatementGetExpression(statement);
    if (expression != deExpressionNull) {
      propagateExpressionConstants(scopeBlock, expression, deBigintNull);
    }
    deBlock subBlock = deStatementGetSubBlock(statement);
    if (subBlock != deBlockNull) {
      deConstantPropagation(scopeBlock, subBlock);
    }
  } deEndBlockStatement;
}
