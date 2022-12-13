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

// Return the operator precedence.
uint32 getPrecedence(deExpressionType type) {
  switch (type) {
    case DE_EXPR_MODINT: return 1;
    case DE_EXPR_SELECT: return 2;
    case DE_EXPR_AND: return 3;
    case DE_EXPR_XOR: return 4;
    case DE_EXPR_OR: return 5;
    case DE_EXPR_LT: case DE_EXPR_LE: case DE_EXPR_GT: case DE_EXPR_GE:
    case DE_EXPR_EQUAL: case DE_EXPR_NOTEQUAL:
      return 6;
    case DE_EXPR_SHL: case DE_EXPR_SHR: case DE_EXPR_ROTL: case DE_EXPR_ROTR:
      return 7;
    case DE_EXPR_BITAND: return 8;
    case DE_EXPR_BITXOR: return 9;
    case DE_EXPR_BITOR: return 10;
    case DE_EXPR_SUB:
    case DE_EXPR_ADD:
      return 11;
    case DE_EXPR_MUL: case DE_EXPR_DIV: case DE_EXPR_MOD:
      return 12;
    case DE_EXPR_EXP:
      return 13;
    case DE_EXPR_NOT: case DE_EXPR_NEGATE: case DE_EXPR_SECRET:
    case DE_EXPR_REVEAL: case DE_EXPR_FUNCADDR: case DE_EXPR_TYPEOF:
    case DE_EXPR_WIDTHOF: case DE_EXPR_ARRAYOF: case DE_EXPR_BITNOT:
    case DE_EXPR_ISNULL:
      return 14;
    case DE_EXPR_CALL: case DE_EXPR_CAST:
      return 15;
    case DE_EXPR_DOT:
    case DE_EXPR_INDEX:
      return 16;
    case DE_EXPR_INTEGER:
    case DE_EXPR_BOOL:
    case DE_EXPR_STRING:
    case DE_EXPR_IDENT:
    case DE_EXPR_UINTTYPE:
    case DE_EXPR_INTTYPE:
    case DE_EXPR_STRINGTYPE:
    case DE_EXPR_BOOLTYPE:
      return 17;
    default:
      return 1;
  }
  return 1;
}

// Return the operator name.
char *deExpressionTypeGetName(deExpressionType type) {
  switch (type) {
    case DE_EXPR_MODINT: return "mod";
    case DE_EXPR_AND: return "&&";
    case DE_EXPR_XOR: return "^^";
    case DE_EXPR_OR: return "||";
    case DE_EXPR_LT: return "<";
    case DE_EXPR_LE: return "<=";
    case DE_EXPR_GT: return ">";
    case DE_EXPR_GE: return ">=";
    case DE_EXPR_EQUAL: return "==";
    case DE_EXPR_NOTEQUAL: return "!=";
    case DE_EXPR_SHL: return "<<";
    case DE_EXPR_SHR: return ">>";
    case DE_EXPR_ROTL: return "<<<";
    case DE_EXPR_ROTR: return ">>>";
    case DE_EXPR_BITAND: return "&";
    case DE_EXPR_BITXOR: return "^";
    case DE_EXPR_BITOR: return "|";
    case DE_EXPR_NEGATE:
    case DE_EXPR_SUB:
      return "-";
    case DE_EXPR_NEGATETRUNC:
    case DE_EXPR_SUBTRUNC:
      return "!-";
    case DE_EXPR_ADD: return "+";
    case DE_EXPR_ADDTRUNC: return "!+";
    case DE_EXPR_MUL: return "*";
    case DE_EXPR_MULTRUNC: return "!*";
    case DE_EXPR_DIV: return "/";
    case DE_EXPR_MOD: return "%";
    case DE_EXPR_EXP: return "**";
    case DE_EXPR_NOT: return "!";
    case DE_EXPR_BITNOT: return "~";
    case DE_EXPR_DOT: return ".";
    case DE_EXPR_INDEX: return "[]";
    case DE_EXPR_IN: return "in";
    default:
      utExit("Unexpected expression type");
  }
  return "";  // Dummy return.
}

// Dump an expression list, separated by commas.
static void dumpExpressionList(deString string, deExpression expression) {
  deExpression child;
  bool firstTime = true;
  deForeachExpressionExpression(expression, child) {
    if (!firstTime) {
      deStringSprintf(string,", ");
    }
    firstTime = false;
    deDumpExpressionStr(string, child);
  } deEndExpressionExpression;
}

// Write out a call expression.
static void dumpCallExpr(deString string, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDumpExpressionStr(string, left);
  deStringPuts(string, "(");
  if (right != deExpressionNull) {
    deDumpExpressionStr(string, right);
  }
  deStringPuts(string, ")");
}

// Write out an index expression.
static void dumpIndexExpr(deString string, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDumpExpressionStr(string, left);
  deStringPuts(string, "[");
  if (right != deExpressionNull) {
    deDumpExpressionStr(string, right);
  }
  deStringPuts(string, "]");
}

// Write out a slice expression.
static void dumpSliceExpr(deString string, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression lower = deExpressionGetNextExpression(left);
  deExpression upper = deExpressionGetNextExpression(lower);
  deDumpExpressionStr(string, left);
  deStringPuts(string, "[");
  deDumpExpressionStr(string, lower);
  deStringPuts(string, ":");
  deDumpExpressionStr(string, upper);
  deStringPuts(string, "]");
}

// Write out a cast expression.
static void dumpCastExpr(deString string, deExpression expression,
    uint32 parentPrecedence, bool truncate) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deExpressionType type = deExpressionGetType(expression);
  uint32 precedence = getPrecedence(type);
  if (precedence <= parentPrecedence) {
    deStringPuts(string, "(");
  }
  if (truncate) {
    deStringPuts(string, "!");
  }
  deStringPuts(string, "<");
  deDumpExpressionStr(string, left);
  deStringPuts(string, ">");
  deDumpExpressionStr(string, right);
  if (precedence <= parentPrecedence) {
    deStringPuts(string, ")");
  }
}

// Write out a select expression.
static void dumpSelectExpr(deString string, deExpression expression, uint32 parentPrecedence) {
  deExpression s = deExpressionGetFirstExpression(expression);
  deExpression a = deExpressionGetNextExpression(s);
  deExpression b = deExpressionGetNextExpression(a);
  deExpressionType type = deExpressionGetType(expression);
  uint32 precedence = getPrecedence(type);
  if (precedence <= parentPrecedence) {
    deStringPuts(string, "(");
  }
  deDumpExpressionStr(string, s);
  deStringSprintf(string, " ? ");
  deDumpExpressionStr(string, a);
  deStringSprintf(string, " : ");
  deDumpExpressionStr(string, b);
  if (precedence <= parentPrecedence) {
    deStringPuts(string, ")");
  }
}

// Write out a binary expression.
static void dumpBinaryExpr(deString string, deExpression expression, char* operator,
                           uint32 parentPrecedence) {
  deExpression parent = deExpressionGetExpression(expression);
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deExpressionType type = deExpressionGetType(expression);
  uint32 precedence = getPrecedence(type);
  bool needParens = precedence < parentPrecedence ||
                    (precedence == parentPrecedence &&
                        parent != deExpressionNull &&
                        deExpressionGetFirstExpression(parent) != expression);
  if (needParens) {
    deStringPuts(string, "(");
  }
  deDumpExpressionStr(string, left);
  if (precedence <= getPrecedence(DE_EXPR_ADD)) {
    deStringSprintf(string, " %s ", operator);
  } else {
    deStringSprintf(string, "%s", operator);
  }
  deDumpExpressionStr(string, right);
  if (needParens) {
    deStringPuts(string, ")");
  }
}

// Write out a prefix expression.
static void dumpPrefixExpr(deString string, deExpression expression, char* operator,
                                 uint32 parentPrecedence) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpressionType type = deExpressionGetType(expression);
  uint32 precedence = getPrecedence(type);
  if (precedence <= parentPrecedence) {
    deStringPuts(string, "(");
  }
  deStringSprintf(string, " %s", operator);
  deDumpExpressionStr(string, left);
  if (precedence <= parentPrecedence) {
    deStringPuts(string, ")");
  }
}

// Dump a builtin function such as typeof(a).
static void dumpBuiltinExpr(deString string, deExpression expression, char* name) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deStringSprintf(string, "%s(", name);
  deDumpExpressionStr(string, left);
  deStringPuts(string, ")");
}

// Write out the array expression.
static void dumpArrayExpr(deString string, deExpression expression) {
  deStringPuts(string, "[");
  deExpression child;
  bool firstTime = true;
  deForeachExpressionExpression(expression, child) {
    if (!firstTime) {
      deStringSprintf(string, ", ");
    }
    firstTime = false;
    deDumpExpressionStr(string, child);
  } deEndExpressionExpression;
  deStringPuts(string, "]");
}

// Write out the tuple expression.
static void dumpTupleExpr(deString string, deExpression expression) {
  deStringPuts(string, "(");
  deExpression child;
  bool firstTime = true;
  deForeachExpressionExpression(expression, child) {
    if (!firstTime) {
      deStringSprintf(string, ", ");
    }
    firstTime = false;
    deDumpExpressionStr(string, child);
  } deEndExpressionExpression;
  deStringPuts(string, ")");
}

// Write out the expression in a reasonably readable format to the end of |string|.
void deDumpExpressionStr(deString string, deExpression expression) {
  deExpression parent = deExpressionGetExpression(expression);
  uint32 parentPrecedence = 0;
  if (parent != deExpressionNull) {
    parentPrecedence = getPrecedence(deExpressionGetType(parent));
  }
  deExpressionType type = deExpressionGetType(expression);
  switch (type) {
    case DE_EXPR_INTEGER: {
      deBigint bigint = deExpressionGetBigint(expression);
      deStringPuts(string, deBigintToString(bigint, 10));
      deStringSprintf(string, "%c%u", deBigintSigned(bigint)? 'i' : 'u', deBigintGetWidth(bigint));
      break;
    }
    case DE_EXPR_FLOAT:
      switch (deFloatGetType(deExpressionGetFloat(expression))) {
        case DE_FLOAT_SINGLE:
          deStringSprintf(string, "%ff32", deFloatGetValue(deExpressionGetFloat(expression)));
          break;
        case DE_FLOAT_DOUBLE:
          deStringSprintf(string, "%ff64", deFloatGetValue(deExpressionGetFloat(expression)));
          break;
        default:
          utExit("Unexpected floating point type");
      }
      break;
    case DE_EXPR_BOOL:
      if (deExpressionBoolVal(expression)) {
        deStringSprintf(string, "true");
      } else {
        deStringSprintf(string, "false");
      }
      break;
    case DE_EXPR_STRING:
      deStringSprintf(string, "\"%s\"", deEscapeString(deExpressionGetString(expression)));
      break;
    case DE_EXPR_IDENT:
      deStringSprintf(string, "%s", utSymGetName(deExpressionGetName(expression)));
      break;
    case DE_EXPR_ARRAY:
      dumpArrayExpr(string, expression);
      break;
    case DE_EXPR_TUPLE:
      dumpTupleExpr(string, expression);
      break;
    case DE_EXPR_RANDUINT:
      deStringSprintf(string, "rand%u", deExpressionGetWidth(expression));
      break;
    case DE_EXPR_MODINT:
      dumpBinaryExpr(string, expression, "mod", parentPrecedence);
      break;
    case DE_EXPR_ADD:
      dumpBinaryExpr(string, expression, "+", parentPrecedence);
      break;
    case DE_EXPR_SUB:
      dumpBinaryExpr(string, expression, "-", parentPrecedence);
      break;
    case DE_EXPR_MUL:
      dumpBinaryExpr(string, expression, "*", parentPrecedence);
      break;
    case DE_EXPR_DIV:
      dumpBinaryExpr(string, expression, "/", parentPrecedence);
      break;
    case DE_EXPR_MOD:
      dumpBinaryExpr(string, expression, "%", parentPrecedence);
      break;
    case DE_EXPR_AND:
      dumpBinaryExpr(string, expression, "&&", parentPrecedence);
      break;
    case DE_EXPR_OR:
      dumpBinaryExpr(string, expression, "||", parentPrecedence);
      break;
    case DE_EXPR_XOR:
      dumpBinaryExpr(string, expression, "^^", parentPrecedence);
      break;
    case DE_EXPR_BITAND:
      dumpBinaryExpr(string, expression, "&", parentPrecedence);
      break;
    case DE_EXPR_BITOR:
      dumpBinaryExpr(string, expression, "|", parentPrecedence);
      break;
    case DE_EXPR_BITXOR:
      dumpBinaryExpr(string, expression, "^", parentPrecedence);
      break;
    case DE_EXPR_EXP:
      dumpBinaryExpr(string, expression, "**", parentPrecedence);
      break;
    case DE_EXPR_SHL:
      dumpBinaryExpr(string, expression, "<<", parentPrecedence);
      break;
    case DE_EXPR_SHR:
      dumpBinaryExpr(string, expression, ">>", parentPrecedence);
      break;
    case DE_EXPR_ROTL:
      dumpBinaryExpr(string, expression, "<<<", parentPrecedence);
      break;
    case DE_EXPR_ROTR:
      dumpBinaryExpr(string, expression, ">>>", parentPrecedence);
      break;
    case DE_EXPR_ADDTRUNC:
      dumpBinaryExpr(string, expression, "!+", parentPrecedence);
      break;
    case DE_EXPR_SUBTRUNC:
      dumpBinaryExpr(string, expression, "!-", parentPrecedence);
      break;
    case DE_EXPR_MULTRUNC:
      dumpBinaryExpr(string, expression, "!*", parentPrecedence);
      break;
    case DE_EXPR_LT:
      dumpBinaryExpr(string, expression, "<", parentPrecedence);
      break;
    case DE_EXPR_LE:
      dumpBinaryExpr(string, expression, "<=", parentPrecedence);
      break;
    case DE_EXPR_GT:
      dumpBinaryExpr(string, expression, ">", parentPrecedence);
      break;
    case DE_EXPR_GE:
      dumpBinaryExpr(string, expression, ">=", parentPrecedence);
      break;
    case DE_EXPR_EQUAL:
      dumpBinaryExpr(string, expression, "==", parentPrecedence);
      break;
    case DE_EXPR_NOTEQUAL:
      dumpBinaryExpr(string, expression, "!=", parentPrecedence);
      break;
    case DE_EXPR_NEGATE:
      dumpPrefixExpr(string, expression, "-", parentPrecedence);
      break;
    case DE_EXPR_NEGATETRUNC:
      dumpPrefixExpr(string, expression, "!-", parentPrecedence);
      break;
    case DE_EXPR_NOT:
      dumpPrefixExpr(string, expression, "!", parentPrecedence);
      break;
    case DE_EXPR_BITNOT:
      dumpPrefixExpr(string, expression, "~", parentPrecedence);
      break;
    case DE_EXPR_CAST:
      dumpCastExpr(string, expression, parentPrecedence, false);
      break;
    case DE_EXPR_CASTTRUNC:
      dumpCastExpr(string, expression, parentPrecedence, true);
      break;
    case DE_EXPR_SELECT:
      dumpSelectExpr(string, expression, parentPrecedence);
      break;
    case DE_EXPR_CALL:
      dumpCallExpr(string, expression);
      break;
    case DE_EXPR_FUNCADDR:
      dumpPrefixExpr(string, expression, "&", parentPrecedence);
      break;
    case DE_EXPR_ARRAYOF:
      dumpBuiltinExpr(string, expression, "arrayof");
      break;
    case DE_EXPR_TYPEOF:
      dumpBuiltinExpr(string, expression, "typeof");
      break;
    case DE_EXPR_UNSIGNED:
      dumpBuiltinExpr(string, expression, "unsigned");
      break;
    case DE_EXPR_SIGNED:
      dumpBuiltinExpr(string, expression, "signed");
      break;
    case DE_EXPR_WIDTHOF:
      dumpBuiltinExpr(string, expression, "widthof");
      break;
    case DE_EXPR_ISNULL:
      dumpBuiltinExpr(string, expression, "isnull");
      break;
    case DE_EXPR_NULL:
      dumpBuiltinExpr(string, expression, "null");
      break;
    case DE_EXPR_NOTNULL:
      dumpBuiltinExpr(string, expression, "notnull");
      break;
    case DE_EXPR_INDEX:
      dumpIndexExpr(string, expression);
      break;
    case DE_EXPR_SLICE:
      dumpSliceExpr(string, expression);
      break;
    case DE_EXPR_SECRET:
      deStringPuts(string, "secret(");
      deDumpExpressionStr(string, deExpressionGetFirstExpression(expression));
      deStringPuts(string, ")");
      break;
    case DE_EXPR_REVEAL:
      dumpPrefixExpr(string, expression, "reveal", parentPrecedence);
      break;
    case DE_EXPR_EQUALS:
      dumpBinaryExpr(string, expression, "=", parentPrecedence);
      break;
    case DE_EXPR_ADD_EQUALS:
      dumpBinaryExpr(string, expression, "+=", parentPrecedence);
      break;
    case DE_EXPR_SUB_EQUALS:
      dumpBinaryExpr(string, expression, "-=", parentPrecedence);
      break;
    case DE_EXPR_MUL_EQUALS:
      dumpBinaryExpr(string, expression, "*=", parentPrecedence);
      break;
    case DE_EXPR_DIV_EQUALS:
      dumpBinaryExpr(string, expression, "/=", parentPrecedence);
      break;
    case DE_EXPR_MOD_EQUALS:
      dumpBinaryExpr(string, expression, "%=", parentPrecedence);
      break;
    case DE_EXPR_AND_EQUALS:
      dumpBinaryExpr(string, expression, "&&=", parentPrecedence);
      break;
    case DE_EXPR_OR_EQUALS:
      dumpBinaryExpr(string, expression, "||=", parentPrecedence);
      break;
    case DE_EXPR_XOR_EQUALS:
      dumpBinaryExpr(string, expression, "^^=", parentPrecedence);
      break;
    case DE_EXPR_EXP_EQUALS:
      dumpBinaryExpr(string, expression, "**=", parentPrecedence);
      break;
    case DE_EXPR_SHL_EQUALS:
      dumpBinaryExpr(string, expression, "<<=", parentPrecedence);
      break;
    case DE_EXPR_SHR_EQUALS:
      dumpBinaryExpr(string, expression, ">>=", parentPrecedence);
      break;
    case DE_EXPR_ROTL_EQUALS:
      dumpBinaryExpr(string, expression, "<<<=", parentPrecedence);
      break;
    case DE_EXPR_ROTR_EQUALS:
      dumpBinaryExpr(string, expression, ">>>=", parentPrecedence);
      break;
    case DE_EXPR_ADDTRUNC_EQUALS:
      dumpBinaryExpr(string, expression, "!+=", parentPrecedence);
      break;
    case DE_EXPR_SUBTRUNC_EQUALS:
      dumpBinaryExpr(string, expression, "!-=", parentPrecedence);
      break;
    case DE_EXPR_MULTRUNC_EQUALS:
      dumpBinaryExpr(string, expression, "!*=", parentPrecedence);
      break;
    case DE_EXPR_BITAND_EQUALS:
      dumpBinaryExpr(string, expression, "&=", parentPrecedence);
      break;
    case DE_EXPR_BITOR_EQUALS:
      dumpBinaryExpr(string, expression, "|=", parentPrecedence);
      break;
    case DE_EXPR_BITXOR_EQUALS:
      dumpBinaryExpr(string, expression, "^=", parentPrecedence);
      break;
    case DE_EXPR_AS:
      dumpBinaryExpr(string, expression, " as ", parentPrecedence);
      break;
    case DE_EXPR_IN:
      dumpBinaryExpr(string, expression, " in ", parentPrecedence);
      break;
    case DE_EXPR_DOT:
      dumpBinaryExpr(string, expression, ".", parentPrecedence);
      break;
    case DE_EXPR_DOTDOTDOT:
      dumpBinaryExpr(string, expression, "...", parentPrecedence);
      break;
    case DE_EXPR_LIST:
      dumpExpressionList(string, expression);
      break;
    case DE_EXPR_UINTTYPE:
      deStringSprintf(string, "u%u", deExpressionGetWidth(expression));
      break;
    case DE_EXPR_INTTYPE:
      deStringSprintf(string, "i%u", deExpressionGetWidth(expression));
      break;
    case DE_EXPR_FLOATTYPE:
      deStringSprintf(string, "f%u", deExpressionGetWidth(expression));
      break;
    case DE_EXPR_STRINGTYPE:
      deStringSprintf(string, "string");
      break;
    case DE_EXPR_BOOLTYPE:
      deStringSprintf(string, "bool");
      break;
    case DE_EXPR_NAMEDPARAM:
      deStringSprintf(string, "%s = ",
          utSymGetName(deExpressionGetName(deExpressionGetFirstExpression(expression))));
      deDumpExpressionStr(string, deExpressionGetLastExpression(expression));
      break;
  }
}

// Write out the expression in a reasonably readable format.
void deDumpExpression(deExpression expression) {
  deString string = deStringAlloc();
  deDumpExpressionStr(string, expression);
  fputs(deStringGetCstr(string), stdout);
  fflush(stdout);
  deStringFree(string);
}

// Write out the expression in a reasonably readable format.  The caller must
// call deStringFree on the returned string.
deString deExpressionToString(deExpression expression) {
  deString string = deStringAlloc();
  deDumpExpressionStr(string, expression);
  return string;
}

// Count the number of child expressions on the expression.
uint32 deExpressionCountExpressions(deExpression expression) {
  deExpression child;
  uint32 numChildren = 0;
  deForeachExpressionExpression(expression, child) {
    numChildren++;
  } deEndExpressionExpression;
  return numChildren;
}

// Determine if the expression type makes a type.
static bool exressionTypeMakesType(deExpressionType type) {
  switch (type) {
    case DE_EXPR_TYPEOF:
    case DE_EXPR_UINTTYPE:
    case DE_EXPR_INTTYPE:
    case DE_EXPR_STRINGTYPE:
    case DE_EXPR_BOOLTYPE:
      return true;
    default:
      return false;
  }
  return false;  // Dummy return.
}

// Create a new expression object.
deExpression deExpressionCreate(deExpressionType type, deLine line) {
  deExpression expr = deExpressionAlloc();
  deExpressionSetType(expr, type);
  deExpressionSetLine(expr, line);
  return expr;
}

// Create a binary expression.
deExpression deBinaryExpressionCreate(deExpressionType type,
    deExpression leftExpr, deExpression rightExpr, deLine line) {
  deExpression expr = deExpressionCreate(type, line);
  deExpressionAppendExpression(expr, leftExpr);
  deExpressionAppendExpression(expr, rightExpr);
  return expr;
}

// Create a unary expression.
deExpression deUnaryExpressionCreate(deExpressionType type, deExpression expr, deLine line) {
  deExpression newExpr = deExpressionCreate(type, line);
  deExpressionAppendExpression(newExpr, expr);
  return newExpr;
}

// Create an identifier expression.
deExpression deIdentExpressionCreate(utSym name, deLine line) {
  deExpression expr = deExpressionCreate(DE_EXPR_IDENT, line);
  deExpressionSetName(expr, name);
  return expr;
}

// Create a constant integer expression.
deExpression deIntegerExpressionCreate(deBigint bigint, deLine line) {
  deExpression expr = deExpressionCreate(DE_EXPR_INTEGER, line);
  deExpressionSetBigint(expr, bigint);
  return expr;
}

// Create a random uint expression.
deExpression deRandUintExpressionCreate(uint32 width, deLine line) {
  deExpression expr = deExpressionCreate(DE_EXPR_RANDUINT, line);
  deExpressionSetWidth(expr, width);
  return expr;
}

// Create a constant floating point expression.
deExpression deFloatExpressionCreate(deFloat floatVal, deLine line) {
  deExpression expr = deExpressionCreate(DE_EXPR_FLOAT, line);
  deExpressionSetFloat(expr, floatVal);
  return expr;
}

// Create a string expression.
deExpression deStringExpressionCreate(deString string, deLine line) {
  utAssert(deStringGetRoot(string) == deRootNull);
  deExpression expr = deExpressionCreate(DE_EXPR_STRING, line);
  deExpressionSetString(expr, string);
  return expr;
}

// Create a string expression.
deExpression deCStringExpressionCreate(char *text, deLine line) {
  deExpression expr = deExpressionCreate(DE_EXPR_STRING, line);
  deString string = deStringAlloc();
  deStringSetText(string, text, strlen(text));
  deExpressionSetString(expr, string);
  return expr;
}

// Create a Boolean expression.
deExpression deBoolExpressionCreate(bool value, deLine line) {
  deExpression expr = deExpressionCreate(DE_EXPR_BOOL, line);
  deExpressionSetBoolVal(expr, value);
  return expr;
}

// Find the statement owning the expression.
deStatement deFindExpressionStatement(deExpression expression) {
  deStatement statement;
  do {
    statement = deExpressionGetStatement(expression);
    expression = deExpressionGetExpression(expression);
  } while (statement == deStatementNull && expression != deExpressionNull);
  return statement;
}

// Determine if the datatype is a method call, including call to constructors.
// This means that a self parameter is needed.  Direct calls to class methods
// through the class name do not count as method calls.
bool deExpressionIsMethodCall(deExpression accessExpression) {
  if (deDatatypeGetType(deExpressionGetDatatype(accessExpression)) != DE_TYPE_FUNCTION ||
      deExpressionGetType(accessExpression) != DE_EXPR_DOT) {
    return false;
  }
  deExpression left = deExpressionGetFirstExpression(accessExpression);
  deDatatype datatype = deExpressionGetDatatype(left);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type == DE_TYPE_CLASS) {
    return true;
  }
  // Allow method calls on builtin types, such as array.length().
  return type != DE_TYPE_TCLASS  && type != DE_TYPE_FUNCTION;
}

// Make a deep copy of the expression.
deExpression deCopyExpression(deExpression expression) {
  deExpression newExpression = deExpressionCreate(deExpressionGetType(expression),
      deExpressionGetLine(expression));
  deExpressionSetDatatype(newExpression, deExpressionGetDatatype(expression));
  deExpressionSetIsType(newExpression, deExpressionIsType(expression));
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_INTEGER:
      deExpressionSetBigint(newExpression, deCopyBigint(deExpressionGetBigint(expression)));
      break;
    case DE_EXPR_STRING:
      deExpressionSetString(newExpression, deExpressionGetString(expression));
      break;
    case DE_EXPR_IDENT:
      deExpressionSetName(newExpression, deExpressionGetName(expression));
      break;
    case DE_EXPR_BOOL:
      deExpressionSetBoolVal(newExpression, deExpressionBoolVal(expression));
      break;
    case DE_EXPR_RANDUINT:
    case DE_EXPR_UINTTYPE:
    case DE_EXPR_INTTYPE:
      deExpressionSetWidth(newExpression, deExpressionGetWidth(expression));
      break;
    default:
      break;
  }
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    deExpression newChild = deCopyExpression(child);
    deExpressionAppendExpression(newExpression, newChild);
  } deEndExpressionExpression;
  return newExpression;
}

// Find the named expression in the named parameters, if it exists.
deExpression deFindNamedParameter(deExpression firstParameter, utSym name) {
  for (deExpression parameter = firstParameter; parameter != deExpressionNull;
       parameter = deExpressionGetNextExpression(parameter)) {
    if (deExpressionGetType(parameter) == DE_EXPR_NAMEDPARAM) {
      if (deExpressionGetName(deExpressionGetFirstExpression(parameter)) == name) {
        return parameter;
      }
    }
  }
  return deExpressionNull;
}

// Destroy children of |expression|.
static void destroyExpressionChildren(deExpression expression) {
  deExpression child;
  deSafeForeachExpressionExpression(expression, child) {
    deExpressionDestroy(child);
  } deEndSafeExpressionExpression;
}

// Morph the expression into the value.  This only works for builtin constant
// types, such as bool, string, etc.  Composite values, even if constant, are not
// yet supported.  The datatype of |value| must match the datatype of
// |expression|.
static void setExpressionToValue(deExpression expression, deValue value) {
  deDatatypeType type = deDatatypeGetType(deExpressionGetDatatype(expression));
  utAssert(type == DE_TYPE_MODINT || type == deValueGetType(value));
  switch (deValueGetType(value)) {
    case DE_TYPE_NONE :
    case DE_TYPE_MODINT:
    case DE_TYPE_ARRAY:
    case DE_TYPE_TUPLE:
    case DE_TYPE_STRUCT:
    case DE_TYPE_ENUM:
    case DE_TYPE_NULL:
    case DE_TYPE_TCLASS:
    case DE_TYPE_CLASS:
    case DE_TYPE_FUNCTION:
    case DE_TYPE_FUNCPTR:
    case DE_TYPE_ENUMCLASS:
      utExit("Cannot morph an expression into this type of value");
      break;
    case DE_TYPE_BOOL:
      deExpressionSetType(expression, DE_EXPR_BOOL);
      deExpressionSetBoolVal(expression, deValueBoolVal(value));
      break;
    case DE_TYPE_STRING:
      deExpressionSetType(expression, DE_EXPR_STRING);
      deExpressionSetString(expression, deValueGetStringVal(value));
      break;
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
      deExpressionSetType(expression, DE_EXPR_INTEGER);
      deExpressionSetBigint(expression, deCopyBigint(deValueGetBigintVal(value)));
      break;
    case DE_TYPE_FLOAT:
      deExpressionSetType(expression, DE_EXPR_FLOAT);
      deExpressionSetFloat(expression, deCopyFloat(deValueGetFloatVal(value)));
      break;
  }
}

// Morph the expression into a constant represented by |value|.
void deSetExpressionToValue(deExpression expression, deValue value) {
  destroyExpressionChildren(expression);
  deExpressionType type = deExpressionGetType(expression);
  if (type == DE_EXPR_INTEGER) {
    deBigintDestroy(deExpressionGetBigint(expression));
  } else if (type == DE_EXPR_FLOAT) {
    deFloatDestroy(deExpressionGetFloat(expression));
  } else if (type == DE_EXPR_STRING) {
    deStringDestroy(deExpressionGetString(expression));
  }
  setExpressionToValue(expression, value);
}
