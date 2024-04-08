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

// Code for expression statements and expressions.

#include "de.h"

typedef enum {
  DE_BINDRES_OK,
  DE_BINDRES_FAILED,
  DE_BINDRES_BLOCKED,
  DE_BINDRES_REBIND,
} deBindRes;

// Set the float expression's datatype.
static void bindFloatExpression(deExpression expression){
  deFloat floatVal = deExpressionGetFloat(expression);
  uint32 width = deFloatGetWidth(floatVal);
  deDatatype datatype = deFloatDatatypeCreate(width);
  deExpressionSetDatatype(expression, datatype);
}

// Set the random uint expression's datatype, which is just an unsigned integer.
static void bindRandUintExpression(deExpression expression) {
  uint32 width = deExpressionGetWidth(expression);
  deDatatype datatype = deUintDatatypeCreate(width);
  datatype = deSetDatatypeSecret(datatype, true);
  deExpressionSetDatatype(expression, datatype);
}

// Verify the datatype can be cast to a modular integer.  This just means it is INT or UINT.
static void verifyExpressionCanCastToModint(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  if (!deDatatypeIsInteger(datatype)) {
    deExprError(expression, "Expression cannot be cast to a modular integer");
  }
}

// Verify that the expression's child expressions can be cast to the modular
// type, except for the right side of an exponentiation expression which must be
// UINT.
static void postProcessModintExpression(deExpression expression) {
  deExpressionType type = deExpressionGetType(expression);
  if (type == DE_EXPR_EXP || type == DE_EXPR_EXP_EQUALS) {
    deDatatype rightType = deExpressionGetDatatype(deExpressionGetLastExpression(expression));
    if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
      deExprError(expression, "Modular exponent must be an unsigned integer.");
    }
    verifyExpressionCanCastToModint(deExpressionGetFirstExpression(expression));
    return;
  }
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    verifyExpressionCanCastToModint(child);
  } deEndExpressionExpression;
}

// Bind a modular expression, which is built from modular arithmetic friendly operators.  Only
// modular operators such as add/sub/exp expressions are set to |modularType|.
static void bindModularExpression(deBlock scopeBlock, deExpression expression,
    deDatatype modularType) {
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
      break;
    case DE_EXPR_ADD:
    case DE_EXPR_SUB:
    case DE_EXPR_MUL:
    case DE_EXPR_DIV: {
      deExpression left = deExpressionGetFirstExpression(expression);
      deExpression right = deExpressionGetNextExpression(left);
      bindModularExpression(scopeBlock, left, modularType);
      bindModularExpression(scopeBlock, right, modularType);
      deExpressionSetDatatype(expression, modularType);
      break;
    }
    case DE_EXPR_EXP: {
      deExpression left = deExpressionGetFirstExpression(expression);
      bindModularExpression(scopeBlock, left, modularType);
      // We must still check that the right is a UINT after it is bound.
      deExpressionSetDatatype(expression, modularType);
      break;
    }
    case DE_EXPR_REVEAL:
    case DE_EXPR_SECRET:
    case DE_EXPR_NEGATE: {
      deExpression left = deExpressionGetFirstExpression(expression);
      bindModularExpression(scopeBlock, left, modularType);
      deExpressionSetDatatype(expression, modularType);
      break;
    }
    case DE_EXPR_EQUAL:
    case DE_EXPR_NOTEQUAL: {
      deExpression left = deExpressionGetFirstExpression(expression);
      deExpression right = deExpressionGetNextExpression(left);
      bindModularExpression(scopeBlock, left, modularType);
      bindModularExpression(scopeBlock, right, modularType);
      deExpressionSetDatatype(expression, deBoolDatatypeCreate());
      return;
    }
    default:
      deExprError(expression, "Invalid modular arithmetic expression");
  }
}

// Bind a modular integer expression.  Adding "mod p" after an expression forces
// all of the expressions to the left to be computed mod p.
static void bindModintExpression(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression modulus = deExpressionGetNextExpression(left);
  deDatatype modulusType = deExpressionGetDatatype(modulus);
  if (deDatatypeGetType(modulusType) != DE_TYPE_UINT) {
    deExprError(modulus, "Modulus must be an unsigned integer");
  }
  if (deDatatypeSecret(modulusType)) {
    deExprError(modulus, "Modulus cannot be secret");
  }
  deDatatype datatype = deModintDatatypeCreate(modulus);
  bindModularExpression(scopeBlock, left, datatype);
  deDatatype resultType = deExpressionGetDatatype(left);
  if (resultType == deDatatypeNull || deDatatypeGetType(resultType) == DE_TYPE_MODINT) {
    resultType = modulusType;
  }
  deExpressionSetDatatype(expression, resultType);
}

// Modify the datatype in the constant integer expression tree to match the
// datatype.
static void autocastExpression(deExpression expression, deDatatype datatype) {
  deDatatype oldDatatype = deExpressionGetDatatype(expression);
  if (!deDatatypeIsInteger(oldDatatype) || !deDatatypeIsInteger(datatype)) {
    return;  // We only auto-cast integers without type specifiers to integers.
  }
  deExpressionSetDatatype(expression, datatype);
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    autocastExpression(child, datatype);
  } deEndExpressionExpression;
}

// Return true if the types are the same, other than for their secret bit.
static bool typesAreEquivalent(deDatatype type1, deDatatype type2) {
  return deSetDatatypeSecret(type1, false) == deSetDatatypeSecret(type2, false);
}

// Bind a binary expression, returning the datatypes of the left and right
// sub-expressions.
static void checkBinaryExpression(deBlock scopeBlock, deExpression expression,
    deDatatype* leftType, deDatatype* rightType, bool compareTypes) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  *leftType = deExpressionGetDatatype(left);
  *rightType = deExpressionGetDatatype(right);
  if (compareTypes && !typesAreEquivalent(*leftType, *rightType)) {
    // Try auto-cast.
    if (deExpressionAutocast(left) && !deExpressionAutocast(right)) {
      autocastExpression(left, *rightType);
      *leftType = deExpressionGetDatatype(left);
    } else if (deExpressionAutocast(right) && !deExpressionAutocast(left)) {
      autocastExpression(right, *leftType);
      *rightType = deExpressionGetDatatype(right);
    }
  }
  if (compareTypes && deExpressionAutocast(left) && deExpressionAutocast(right)) {
    deExpressionSetAutocast(expression, true);
  }
}

// Convert an operator-equals expression type into the corresponding operator
// type.
static deExpressionType opEqualsToOp(deExpressionType op) {
  if (op < DE_EXPR_ADD_EQUALS || op > DE_EXPR_MULTRUNC_EQUALS) {
    return op;
  }
  return op - DE_EXPR_ADD_EQUALS + DE_EXPR_ADD;
}

// Find the identifier in the block.  If not found directly, check if it is a
// class block, and look in its template if so.  Also see if scopeBlock is a
// package, in which case check in the package sub-module.
static deIdent findIdentInBlock(deBlock scopeBlock, utSym sym) {
  deIdent ident = deBlockFindIdent(scopeBlock, sym);
  if (ident == deIdentNull) {
    if (deBlockGetType(scopeBlock) == DE_BLOCK_CLASS) {
      deTemplate templ = deClassGetTemplate(deBlockGetOwningClass(scopeBlock));
      deBlock templBlock = deFunctionGetSubBlock(deTemplateGetFunction(templ));
      deIdent templIdent = deBlockFindIdent(templBlock, sym);
      if (templIdent != deIdentNull && deIdentGetType(templIdent) == DE_IDENT_FUNCTION) {
        ident = templIdent;
      }
    } else if (deBlockGetType(scopeBlock) ==DE_BLOCK_FUNCTION) {
      deFunction function = deBlockGetOwningFunction(scopeBlock);
      if (deFunctionGetType(function) == DE_FUNC_PACKAGE) {
        deIdent packageIdent = deBlockFindIdent(deFunctionGetSubBlock(function),
            utSymCreate("package"));
        if (packageIdent != deIdentNull) {
          deFunction moduleFunc = deIdentGetFunction(packageIdent);
          ident = deBlockFindIdent(deFunctionGetSubBlock(moduleFunc), sym);
        }
      }
    }
  }
  return ident;
}

// Find a matching operator overload.
static deFunction findMatchingOperatorOverload(deBlock scopeBlock, deExpression expression,
    deDatatypeArray paramTypes) {
  deExpressionType opType = opEqualsToOp(deExpressionGetType(expression));
  uint32 numParams = deDatatypeArrayGetUsedDatatype(paramTypes);
  if (numParams == 0 || numParams > 2) {
    return deFunctionNull;
  }
  // Try using the first parameter as self.
  deDatatype selfType = deDatatypeArrayGetiDatatype(paramTypes, 0);
  deBlock block;
  utSym sym = deGetOperatorSym(opType, numParams == 1);
  if (!deDatatypeNullable(selfType) && deDatatypeGetType(selfType) == DE_TYPE_CLASS) {
    block = deClassGetSubBlock(deDatatypeGetClass(selfType));
    deIdent ident = findIdentInBlock(block, sym);
    if (ident != deIdentNull) {
      return deIdentGetFunction(ident);
    }
  }
  if (numParams == 2) {
    selfType = deDatatypeArrayGetiDatatype(paramTypes, 1);
    if (!deDatatypeNullable(selfType) && deDatatypeGetType(selfType) == DE_TYPE_CLASS) {
      block = deClassGetSubBlock(deDatatypeGetClass(selfType));
      deIdent ident = deBlockFindIdent(block, sym);
      if (ident != deIdentNull) {
        return deIdentGetFunction(ident);
      }
    }
  }
  return deFunctionNull;
}

// Bind an overload operator function call.  Return false if we are blocked on
// binding the signature.
static deBindRes bindOverloadedFunctionCall(deBlock scopeBlock, deFunction function,
    deExpression expression, deDatatypeArray paramTypes) {
  deLine line = deExpressionGetLine(expression);
  deSignature signature = deLookupSignature(function, paramTypes);
  if (signature == deSignatureNull) {
    deSetStackTraceGlobals(expression);
    signature = deSignatureCreate(function, paramTypes, line);
  } else {
    deDatatypeArrayFree(paramTypes);
  }
  deExpressionSetSignature(expression, signature);
  deSignatureSetInstantiated(signature,
      deSignatureInstantiated(signature) || deExpressionInstantiating(expression));
  deQueueSignature(signature);
  deExpressionSetSignature(expression, signature);
  if (!deSignatureBound(signature)) {
    deEvent event = deSignatureEventCreate(signature);
    deBinding binding = deExpressionGetBinding(expression);
    deEventAppendBinding(event, binding);
    return DE_BINDRES_BLOCKED;
  }
  deExpressionSetDatatype(expression, deSignatureGetReturnType(signature));
  return DE_BINDRES_OK;  // Success.
}

// Determine if the expression type can be overloaded.
static bool expressionTypeCanBeOverloaded(deExpressionType type) {
  switch (type) {
    case DE_EXPR_BITOR :
    case DE_EXPR_ADD:
    case DE_EXPR_SUB:
    case DE_EXPR_MUL:
    case DE_EXPR_DIV:
    case DE_EXPR_BITAND:
    case DE_EXPR_BITXOR:
    case DE_EXPR_ADDTRUNC:
    case DE_EXPR_SUBTRUNC:
    case DE_EXPR_MULTRUNC:
    case DE_EXPR_MOD:
    case DE_EXPR_AND:
    case DE_EXPR_OR :
    case DE_EXPR_XOR:
    case DE_EXPR_EXP:
    case DE_EXPR_SHL:
    case DE_EXPR_SHR:
    case DE_EXPR_ROTL:
    case DE_EXPR_ROTR:
    case DE_EXPR_LT:
    case DE_EXPR_LE:
    case DE_EXPR_GT:
    case DE_EXPR_GE:
    case DE_EXPR_EQUAL:
    case DE_EXPR_NOTEQUAL:
    case DE_EXPR_NEGATE:
    case DE_EXPR_NEGATETRUNC:
    case DE_EXPR_BITNOT:
    case DE_EXPR_NOT:
    case DE_EXPR_INDEX:
    case DE_EXPR_CAST:
    case DE_EXPR_IN:
      return true;
    default:
      return false;
  }
}

// Look for an overloaded operator matching this expression's signature, and if
// one is found, bind to it.  Create a signature for the call to the operator
// overload.
static deBindRes bindOverloadedOperator(deBlock scopeBlock, deExpression expression) {
  // Parameters are already bound.
  deDatatypeArray paramTypes = deDatatypeArrayAlloc();
  deExpression parameter;
  deForeachExpressionExpression(expression, parameter) {
    deDatatype datatype = deExpressionGetDatatype(parameter);
    if (datatype == deDatatypeNull) {
      return DE_BINDRES_FAILED;
    }
    deDatatypeArrayAppendDatatype(paramTypes, datatype);
  } deEndExpressionExpression;
  deFunction operatorFunc = findMatchingOperatorOverload(scopeBlock, expression, paramTypes);
  if (operatorFunc == deFunctionNull) {
    deDatatypeArrayFree(paramTypes);
    return DE_BINDRES_FAILED;
  }
  return bindOverloadedFunctionCall(scopeBlock, operatorFunc, expression, paramTypes);
}

// Bind a binary arithmetic expression.  The left and right types should have
// the same numeric type, resulting in the same type.
static void bindBinaryArithmeticExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  checkBinaryExpression(scopeBlock, expression, &leftType, &rightType, true);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (leftType != rightType) {
    deExprError(expression, "Non-equal types passed to binary operator");
  }
  // Allow addition on strings and arrays.
  deDatatypeType type = deDatatypeGetType(leftType);
  deExpressionType exprType = deExpressionGetType(expression);
  if (type == DE_TYPE_ARRAY || type == DE_TYPE_STRING) {
    if (exprType != DE_EXPR_ADD && exprType != DE_EXPR_ADD_EQUALS &&
        exprType != DE_EXPR_BITXOR && exprType != DE_EXPR_BITXOR_EQUALS) {
      deExprError(expression, "Invalid types for binary arithmetic operator");
    }
  } else if (!deDatatypeIsInteger(leftType) && type != DE_TYPE_FLOAT) {
    deExprError(expression, "Invalid types for binary arithmetic operator");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a bitwise OR expression.  This is different from the other bitwise
// operators because it also used in type unions, such as "a: Uint | Int".
static void bindBitwiseOrExpression(deBlock scopeBlock, deExpression expression, bool isTypeExpr) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  if (deExpressionIsType(left) && deExpressionIsType(right)) {
    deExpressionSetIsType(expression, true);
    return;
  }
  deDatatype leftType, rightType;
  checkBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  if (isTypeExpr) {
    deExpressionSetIsType(expression, true);
    deExpressionSetDatatype(expression, deNoneDatatypeCreate());
  } else {
    bindBinaryArithmeticExpression(scopeBlock, expression);
  }
}

// Check that the left-hand side is not const.
static void checkOpEqualsAssignment(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpressionType type = deExpressionGetType(left);
  if (type == DE_EXPR_IDENT) {
    deIdent ident = deFindIdent(scopeBlock, deExpressionGetName(left));
    if (deIdentGetType(ident) == DE_IDENT_VARIABLE) {
      deVariable var = deIdentGetVariable(ident);
      if (deVariableConst(var)) {
        deExprError(expression, "Assigning to const variable %s ", deVariableGetName(var));
      }
    }
  }
}

// Bind a binary expression, returning the datatypes of the left and right
// sub-expressions.
static void bindBinaryExpression(deBlock scopeBlock, deExpression expression,
    deDatatype* leftType, deDatatype* rightType, bool compareTypes) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  *leftType = deExpressionGetDatatype(left);
  *rightType = deExpressionGetDatatype(right);
  if (compareTypes && !typesAreEquivalent(*leftType, *rightType)) {
    // Try auto-cast.
    if (deExpressionAutocast(left) && !deExpressionAutocast(right)) {
      autocastExpression(left, *rightType);
      *leftType = deExpressionGetDatatype(left);
    } else if (deExpressionAutocast(right) && !deExpressionAutocast(left)) {
      autocastExpression(right, *leftType);
      *rightType = deExpressionGetDatatype(right);
    }
  }
  if (compareTypes && deExpressionAutocast(left) && deExpressionAutocast(right)) {
    deExpressionSetAutocast(expression, true);
  }
}

// Bind an exponentiation expression.  Exponent must be a non-secret uint, while
// the base can be a uint or modint.
static void bindExponentiationExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  if (!deDatatypeIsInteger(leftType)) {
    deExprError(expression, "Base of exponentiation operator must be uint or modint");
  }
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deExprError(expression, "Exponent must be a uint");
  }
  if (deDatatypeSecret(rightType)) {
    deExprError(expression, "Exponent cannot be secret");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a select expression.  The selector must be Boolean, and the two data
// values must have the same type.
static void bindSelectExpression(deBlock scopeBlock, deExpression expression) {
  deExpression select = deExpressionGetFirstExpression(expression);
  deExpression left = deExpressionGetNextExpression(select);
  deExpression right = deExpressionGetNextExpression(left);
  deDatatype selectType = deExpressionGetDatatype(select);
  deDatatype leftType = deExpressionGetDatatype(left);
  deDatatype rightType = deExpressionGetDatatype(right);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (deDatatypeGetType(selectType) != DE_TYPE_BOOL) {
    deExprError(expression, "Select must be Boolean");
  }
  if (leftType != rightType) {
    deExprError(expression, "Select operator applied to different data types:%s",
        deGetOldVsNewDatatypeStrings(leftType, rightType));
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind the slice expression.
static void bindSliceExpression(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression lower = deExpressionGetNextExpression(left);
  deExpression upper = deExpressionGetNextExpression(lower);
  deDatatype leftType, lowerType, upperType;
  leftType = deExpressionGetDatatype(left);
  lowerType = deExpressionGetDatatype(lower);
  upperType = deExpressionGetDatatype(upper);
  if (deDatatypeGetType(lowerType) != DE_TYPE_UINT ||
      deDatatypeGetType(upperType) != DE_TYPE_UINT) {
    deExprError(expression, "Index values must be unsigned integers");
  }
  if (deDatatypeSecret(lowerType) || deDatatypeSecret(upperType)) {
    deExprError(expression, "Indexing with a secret is not allowed");
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_ARRAY && type != DE_TYPE_STRING) {
    deExprError(expression, "Slicing a non-array/non-string type");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a unary expression, returning the datatype of the child.
static deDatatype bindUnaryExpression(deBlock scopeBlock, deExpression expression) {
  deExpression child = deExpressionGetFirstExpression(expression);
  return deExpressionGetDatatype(child);
}

// Bind the secret or markPublic expression.
static void bindMarkSecretOrPublic(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type == DE_TYPE_CLASS || type == DE_TYPE_TEMPLATE) {
    deExprError(expression, "Object references cannot be marked secret");
  }
  bool secret = deExpressionGetType(expression) == DE_EXPR_SECRET;
  datatype = deSetDatatypeSecret(datatype, secret);
  deExpressionSetDatatype(expression, datatype);
  deExpressionSetIsType(expression, deExpressionIsType(deExpressionGetFirstExpression(expression)));
  deExpressionSetConst(expression, deExpressionConst(deExpressionGetFirstExpression(expression)));
}

// Bind a ... expression, eg case u1 ... u32.
static void bindDotDotDotExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftDatatype, rightDatatype;
  bindBinaryExpression(scopeBlock, expression, &leftDatatype, &rightDatatype, true);
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  if (deExpressionIsType(left) != deExpressionIsType(right)) {
    deExprError(expression, "Ranges must be either types or integers, eg not u32 .. 64");
  }
  deDatatypeType leftType = deDatatypeGetType(leftDatatype);
  deDatatypeType rightType = deDatatypeGetType(rightDatatype);
  if (deExpressionIsType(left)) {
    if (leftType != DE_TYPE_UINT && leftType != DE_TYPE_INT) {
      deExprError(expression, "Type ranges are only allowed for Int and Uint types, eg u1 ... u32");
    }
    if (leftType != rightType) {
      deExprError(expression, "Type ranges must have the same sign, eg u1 ... u32 or i1 ... i32");
    }
    uint32 leftWidth = deDatatypeGetWidth(leftDatatype);
    uint32 rightWidth = deDatatypeGetWidth(rightDatatype);
    if (leftWidth > rightWidth) {
      deExprError(expression, "Left type width must be <= right type width, eg i64 ... i256");
    }
    deTemplate templ = deFindDatatypeTemplate(leftDatatype);
    deExpressionSetDatatype(expression, deTemplateDatatypeCreate(templ));
    deExpressionSetIsType(expression, true);
  } else {
    if (leftType != DE_TYPE_UINT && leftType != DE_TYPE_INT) {
      deExprError(expression, "Integer ranges are only allowed for Int and Uint types, eg u1 ... u32");
    }
    if (leftDatatype != rightDatatype) {
      deExprError(expression, "Type ranges limits must have the same type, eg 1 ... 10 or 1i32 ... 10i32:%s",
          deGetOldVsNewDatatypeStrings(leftDatatype, rightDatatype));
    }
    deExpressionSetDatatype(expression, leftDatatype);
  }
}

// Create an array of datatypes for the expression's children.
static deDatatypeArray listDatatypes(deExpression list) {
  deDatatypeArray types = deDatatypeArrayAlloc();
  deExpression child;
  deForeachExpressionExpression(list, child) {
    deDatatypeArrayAppendDatatype(types, deExpressionGetDatatype(child));
  } deEndExpressionExpression;
  return types;
}

// Coerce non-template template datatypes to their class.  This is OK for null
// expressions, and arrayof.  Be careful that casting the datatype from the
// template to the class does not result in the compiler thinking it has a valid
// object reference when it does not, e.g. do not cast template Sym to its
// default class in a Sym.new expression.
static deDatatype coerceToClassDatatype(deDatatype datatype) {
  if (deDatatypeGetType(datatype) == DE_TYPE_TEMPLATE) {
    deTemplate templ = deDatatypeGetTemplate(datatype);
    if (!deTemplateIsTemplate(templ)) {
      return deClassDatatypeCreate(deTemplateGetDefaultClass(templ));
    }
  }
  return datatype;
}

// Create an array of datatypes for the template instantiation.  Coerce
// non-template template types to their classes.  Report an error if any
// non-qualified templates are used in the instantiation.
static deDatatypeArray listTemplateInstDatatypes(deExpression list) {
  deDatatypeArray types = deDatatypeArrayAlloc();
  deExpression child;
  deForeachExpressionExpression(list, child) {
    deDatatype datatype = deExpressionGetDatatype(child);
    if (deDatatypeGetType(datatype) == DE_TYPE_TEMPLATE) {
      deTemplate templ = deDatatypeGetTemplate(datatype);
      if (deTemplateIsTemplate(templ)) {
        deExprError(child, "Template parameters must be fully qualified");
      }
      datatype = coerceToClassDatatype(datatype);
    }
    deDatatypeArrayAppendDatatype(types, datatype);
  } deEndExpressionExpression;
  return types;
}

// Bind the tuple expression.
static void bindTupleExpression(deBlock scopeBlock, deExpression expression) {
  deDatatypeArray types = listDatatypes(expression);
  deDatatype tupleType = deTupleDatatypeCreate(types);
  deExpressionSetDatatype(expression, tupleType);
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    if (deExpressionIsType(child)) {
      deExpressionSetIsType(expression, true);
    }
  } deEndExpressionExpression;
}

// Bind a null expression.  We can say null(f32), which returns 0.0f32, or
// null(string), which returns "".  Calling null on a call to a constructor
// yields null for that class, such as foo = null(Foo(123)).  The difficult
// case is where we call null(Foo), where we pass a template to null.  This can
// be used to set a variable or class data member to null.
static void bindNullExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  datatype = coerceToClassDatatype(datatype);
  if (deDatatypeGetType(datatype) == DE_TYPE_TEMPLATE) {
    deExprError(expression, "Template class parameters must be fully specified in null expression");
  }
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_CLASS:
      datatype = deSetDatatypeNullable(datatype, true);
      break;
    case DE_TYPE_BOOL:
    case DE_TYPE_STRING:
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_FLOAT:
    case DE_TYPE_ARRAY:
    case DE_TYPE_TUPLE:
    case DE_TYPE_STRUCT:
    case DE_TYPE_ENUMCLASS:
    case DE_TYPE_ENUM:
    case DE_TYPE_FUNCPTR:
    case DE_TYPE_TEMPLATE:
    case DE_TYPE_EXPR:
      break;
    case DE_TYPE_FUNCTION: {
      deFunctionType type = deFunctionGetType(deDatatypeGetFunction(datatype));
      if (type != DE_FUNC_STRUCT && type != DE_FUNC_ENUM) {
        deExprError(expression, "Cannot create default initial value for type %s",
            deDatatypeGetTypeString(datatype));
      }
      break;
    }
    case DE_TYPE_MODINT:
    case DE_TYPE_NONE:
      deExprError(expression, "Cannot create default initial value for type %s",
          deDatatypeGetTypeString(datatype));
  }
  deExpressionSetDatatype(expression, datatype);
}

// Bind a notnull expression.
static void bindNotNullExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = deSetDatatypeNullable(bindUnaryExpression(scopeBlock, expression), false);
  deExpressionSetDatatype(expression, datatype);
}

// Set all the variables passed as instantiated.  Any function that can be
// called through a pointer must accept all parameters on the stack, even if
// they are unused, or only used for their types.
static void setAllSignatureVariablesToInstantiated(deSignature signature) {
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    deParamspecSetInstantiated(paramspec, true);
  } deEndSignatureParamspec;
}

// Bind a function pointer expression.
static void bindFunctionPointerExpression(deExpression expression) {
  deExpression callExpr = deExpressionGetFirstExpression(expression);
  deSignature signature = deExpressionGetSignature(callExpr);
  deDatatype returnType = deExpressionGetDatatype(callExpr);
  deSignatureSetIsCalledByFuncptr(signature, true);
  deSignatureSetInstantiated(signature, true);
  setAllSignatureVariablesToInstantiated(signature);
  deExpressionSetSignature(expression, signature);
  deDatatypeArray paramTypes = deSignatureGetParameterTypes(signature);
  deDatatype funcptrType = deFuncptrDatatypeCreate(returnType, paramTypes);
  deExpressionSetDatatype(expression, funcptrType);
}

// Bind an arrayof expression.
static void bindArrayofExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  datatype = coerceToClassDatatype(datatype);
  if (deDatatypeGetType(datatype) == DE_TYPE_TEMPLATE) {
    deExprError(expression, "Cannot have array of template classes");
  }
  deExpressionSetDatatype(expression, deArrayDatatypeCreate(datatype));
}

// Bind a typeof expression.
static void bindTypeofExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  deExpressionSetDatatype(expression, datatype);
  deExpressionSetIsType(expression, true);
}
// Bind a signed() or unsigned() type conversion expression.
static void bindSignConversionExpression(deBlock scopeBlock, deExpression expression) {
  deExpression child = deExpressionGetFirstExpression(expression);
  deDatatype datatype = deExpressionGetDatatype(child);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type != DE_TYPE_UINT && type != DE_TYPE_INT) {
    deExprError(expression, "Cannot  change sign of non-integer");
  }
  datatype = deDatatypeSetSigned(datatype, deExpressionGetType(expression)== DE_EXPR_SIGNED);
  deExpressionSetDatatype(expression, datatype);
}


// Bind a widthof expression.  The expression type is u32.
static void bindWidthofExpression(deBlock scopeBlock, deExpression expression) {
  bool savedInstantiating = deInstantiating;
  deInstantiating = false;
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  if (!deDatatypeIsNumber(datatype)) {
    deExprError(expression, "widthof applied to non-number");
  }
  deExpressionSetDatatype(expression, deUintDatatypeCreate(32));
  deInstantiating = savedInstantiating;
}

// Bind an "in" expression.  These are all overloads.
// TODO: This code should check that the left-hand datatype can actually be in
// the right-hand datatype.
static void bindInExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  deExpressionSetDatatype(expression, deBoolDatatypeCreate());
}

// Determine if the datatype is a number or enumerated value.
static bool datatypeIsNumberOrEnum(deDatatypeType type) {
  return deDatatypeTypeIsNumber(type) || type == DE_TYPE_ENUM;
}

// Determine if the datatype is a number or enumerated value.
static bool datatypeIsNumberOrEnumClass(deDatatypeType type) {
  return deDatatypeTypeIsNumber(type) || type == DE_TYPE_ENUMCLASS || type == DE_TYPE_ENUM;
}

// Return a string that can be printed on two lines to show the old type vs new type.
// Set the integer expression's datatype.
static void bindIntegerExpression(deExpression expression) {
  deBigint bigint = deExpressionGetBigint(expression);
  uint32 width = deBigintGetWidth(bigint);
  deDatatype datatype;
  if (deBigintSigned(bigint)) {
    datatype = deIntDatatypeCreate(width);
  } else {
    datatype = deUintDatatypeCreate(width);
  }
  deExpressionSetDatatype(expression, datatype);
  deExpressionSetAutocast(expression, deBigintWidthUnspecified(bigint));
}

// Bind the identifier expression to a type.  If the identifier does not exist,
// create an unbound identifier.  If unbound or if the identifier has not been
// bound to a datatype, add binding to the identifier's event and return
// false.  If we succeed in expression the identifier, queue bindings blocked on
// this event.  scopeBlock, if present, is from a dot operation, and we must
// look only for identifiers in that block.
static bool bindIdentExpression(deBlock scopeBlock, deBinding binding,
    deExpression expression, bool inScopeBlock) {
  utSym sym = deExpressionGetName(expression);
  deIdent ident = deExpressionGetIdent(expression);
  if (ident == deIdentNull) {
    if (!inScopeBlock) {
      ident = deFindIdent(scopeBlock, sym);
    } else {
      ident = findIdentInBlock(scopeBlock, sym);
    }
    if (ident == deIdentNull) {
      // Create an undefined identifier.
      ident = deUndefinedIdentCreate(scopeBlock, sym);
    }
    deIdentAppendExpression(ident, expression);
  }
  switch (deIdentGetType(ident)) {
    case DE_IDENT_VARIABLE: {
      deVariable variable = deIdentGetVariable(ident);
      deDatatype datatype = deVariableGetDatatype(variable);
      if (datatype == deDatatypeNull || (!deExpressionLhs(expression) &&
          !deDatatypeConcrete(datatype) && !deVariableIsType(variable))) {
        deEvent event = deVariableEventCreate(variable);
        deEventAppendBinding(event, binding);
        return false;
      }
      deExpressionSetDatatype(expression, datatype);
      deExpressionSetIsType(expression, deVariableIsType(variable));
      deVariableSetInstantiated(variable, deVariableInstantiated(variable) ||
          deExpressionInstantiating(expression));
      return true;
    }
    case DE_IDENT_FUNCTION: {
      deDatatype datatype = deFunctionDatatypeCreate(deIdentGetFunction(ident));
      deExpressionSetDatatype(expression, datatype);
      deExpressionSetIsType(expression, deDatatypeGetType(datatype) == DE_TYPE_TEMPLATE);
      return true;
    }
    case DE_IDENT_UNDEFINED: {
      deEvent event = deUndefinedIdentEventCreate(ident);
      deEventAppendBinding(event, binding);
      return false;
    }
  }
  return false;  // Dummy return.
}

// The % operator is overloaded: two integer/float types or a string on the
// left and tuple on the right.  This results in sprintf(left, members of
// tuple...), returning a string.
static void bindModExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  deDatatypeType type = deDatatypeGetType(leftType);
  if (deDatatypeTypeIsInteger(type) || type == DE_TYPE_FLOAT) {
    if (!typesAreEquivalent(leftType, rightType)) {
      deExprError(expression, "Non-equal types passed to binary operator");
    }
    deExpressionSetDatatype(expression, leftType);
    return;
  }
  if (deDatatypeGetType(leftType) != DE_TYPE_STRING) {
    deExprError(expression, "Invalid left operand type for %% operator");
  }
  deVerifyPrintfParameters(expression);
  deDatatype datatype = deStringDatatypeCreate();
  if (deDatatypeSecret(leftType) || deDatatypeSecret(rightType)) {
    datatype = deSetDatatypeSecret(datatype, true);
  }
  deExpressionSetDatatype(expression, datatype);
}


// Bind and AND, OR, or XOR operator.  If operating on numbers, bitwise
// operators are used.  If operating on Boolean values, logical operators are
// used.
static void bindBinaryBool(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  checkBinaryExpression(scopeBlock, expression, &leftType, &rightType, true);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (deDatatypeGetType(leftType) != DE_TYPE_BOOL ||
      deDatatypeGetType(rightType) != DE_TYPE_BOOL) {
    deExprError(expression, "Non-Boolean types passed to Boolean operator");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a shift/rotate expression.  The distance must be a uint.  The value
// being shifted (left operand) must be an integer.
static void bindShiftExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  if (!deDatatypeIsInteger(leftType)) {
    deExprError(expression, "Only integers can be shifted/rotated");
  }
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deExprError(expression, "Shift/rotate distance must be a uint");
  }
  if (deDatatypeSecret(rightType)) {
    deExprError(expression, "Shift/rotate distance cannot be secret");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a relational operator.  Both operands must be strings, arrays, or integers.
static void bindRelationalExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, true);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (!typesAreEquivalent(leftType, rightType)) {
    deExprError(expression, "Non-equal types passed to relational operator:%s",
        deGetOldVsNewDatatypeStrings(leftType, rightType));
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_UINT && type != DE_TYPE_INT && type != DE_TYPE_FLOAT &&
      type != DE_TYPE_STRING && type != DE_TYPE_ARRAY) {
    deExprError(expression, "Invalid types passed to relational operator");
  }
  bool secret = deDatatypeSecret(leftType) || deDatatypeSecret(rightType);
  deExpressionSetDatatype(expression, deSetDatatypeSecret(deBoolDatatypeCreate(), secret));
}

// Bind an equality operator.  Both operands must be integers.
static void bindEqualityExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, true);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (leftType != rightType) {
    deDatatype unifiedType = deUnifyDatatypes(leftType, rightType);
    if (unifiedType == deDatatypeNull) {
      deExprError(expression, "Non-equal types passed to relational operator:%s",
          deGetOldVsNewDatatypeStrings(leftType, rightType));
    }
  }
  deExpressionSetDatatype(expression, deSetDatatypeSecret(deBoolDatatypeCreate(),
      deDatatypeSecret(leftType)));
}

// Bind a negate expression.  The operand must be an integer.
static void bindUnaryArithmeticExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype childType = bindUnaryExpression(scopeBlock, expression);
  if (!deDatatypeIsInteger(childType) && !deDatatypeIsFloat(childType)) {
    deExprError(expression, "Only integers can be negated");
  }
  deExpressionSetDatatype(expression, childType);
  deExpression child = deExpressionGetFirstExpression(expression);
  deExpressionSetAutocast(expression, deExpressionAutocast(child));
}

// Bind a not expression.  It does logical not on Boolean operands, and
// complement on integer operands.
static void bindNotExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype childType = bindUnaryExpression(scopeBlock, expression);
  if (deDatatypeGetType(childType) != DE_TYPE_BOOL) {
    deExprError(expression, "Not operator only works on Boolean types");
  }
  deExpressionSetDatatype(expression, childType);
}

// Verify the cast expression is valid, and return the resulting datatype.
// Casts are allowed between numeric types, including floating point types.  We
// can also cast a string to a [u8] array and vise-versa.  Object references
// can be cast to their underlying integer type and back, e.g  <u32>Point(1,2),
// or <Point(u64, u64)>1u32.  Object-to-integer casts are dangerous and we
// should probably restrict its use to code transformers and unsafe code.
static void verifyCast(deExpression expression, deDatatype leftDatatype,
    deDatatype rightDatatype, deLine line) {
  if (leftDatatype == rightDatatype) {
    return;  // The cast is a nop.
  }
  if (leftDatatype == deDatatypeNull) {
    deExprError(expression, "Casts require qualified types");
  }
  deDatatypeType leftType = deDatatypeGetType(leftDatatype);
  deDatatypeType rightType = deDatatypeGetType(rightDatatype);
  if (datatypeIsNumberOrEnumClass(leftType) && datatypeIsNumberOrEnum(rightType)) {
    return;
  }
  if (deDatatypeTypeIsInteger(rightType) ||
      (!deDatatypeTypeIsInteger(leftType) && rightType == DE_TYPE_STRING)) {
    // Swap datatypes so the non-array is the left type.
    deDatatype tempDatatype = leftDatatype;
    leftDatatype = rightDatatype;
    rightDatatype = tempDatatype;
    leftType = deDatatypeGetType(leftDatatype);
    rightType = deDatatypeGetType(rightDatatype);
  }
  if (!deDatatypeTypeIsInteger(leftType) && leftType != DE_TYPE_STRING) {
    deExprError(expression, "Invalid cast: only casting from/to integers and from/to string are allowed");
  }
  if (leftType == DE_TYPE_STRING) {
    if (rightType != DE_TYPE_ARRAY ||
        deDatatypeGetType(deDatatypeGetElementType(rightDatatype)) !=
            DE_TYPE_UINT) {
      deExprError(expression, "Invalid string conversion.  Only conversions from/to [u8] are allowed.");
    }
    return;
  }
  if (rightType == DE_TYPE_ARRAY) {
    deDatatype elementDatatype = deDatatypeGetElementType(rightDatatype);
    if (deDatatypeGetType(elementDatatype) != DE_TYPE_UINT) {
      deExprError(expression, "Invalid cast: can only convert from/to uint arrays");
    }
    return;
  }
  if (!deDatatypeTypeIsInteger(rightType) && rightType != DE_TYPE_CLASS &&
      rightType != DE_TYPE_TEMPLATE) {
    deExprError(expression, "Invalid cast");
  }
  if (rightType  == DE_TYPE_CLASS) {
    // Verify the integer width matches the class reference width.
    deClass theClass = deDatatypeGetClass(rightDatatype);
    if (deDatatypeGetWidth(leftDatatype) != deClassGetRefWidth(theClass)) {
      deExprError(expression, "Invalid cast: integer width does not match class reference width");
    }
  }
}

// Bind a cast expression.  Various conversions are allowed.  For example:
//
//   <u32>10i16
//   <u32[]>rsaKey
//   <u8[]>"Hello, World!",
//   <string>[0x74u8, 0x65u8, 0x73u8, 0x74u8]
//   <u32[]>(123u255 mod p)
//   <u32[]>"I will be converted to an array of u32, little-endian"
//   <u16> 0xdeadbeefu32  // Error!
//   <u32>-1u32  // Error!  -1 is not the same number as 0xffffffff.
//   <u8[]>[1u32, 2u32, 3u32]  // Results in a 12-byte array.
//   <self>0u32  // Same as null(self)
//   objectIndex = <u32>object  // Convert an object reference to an integer.
//
// Integers are converted little-endian.  An exception is thrown if a conversion
// results in data loss.
static void bindCastExpression(deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDatatype leftDatatype = coerceToClassDatatype(deExpressionGetDatatype(left));
  deDatatype rightDatatype = deExpressionGetDatatype(right);
  deLine line = deExpressionGetLine(expression);
  // We ignore the secrecy of the left type: you can't cast away secrecy.  Just
  // force the left type to have the same secrecy value as the right.
  leftDatatype = deSetDatatypeSecret(leftDatatype, deDatatypeSecret(rightDatatype));
  if (deDatatypeGetType(leftDatatype) == DE_TYPE_ENUMCLASS) {
    // If the cast is to an ENUMCLASS, instead cast to an ENUM.
    deFunction enumFunc = deDatatypeGetFunction(leftDatatype);
    leftDatatype = deEnumDatatypeCreate(enumFunc);
  }
  verifyCast(expression, leftDatatype, rightDatatype, line);
  deExpressionSetDatatype(expression, leftDatatype);
}

// Verify that it is OK for code to call the function.
static void verifyFunctionIsCallable(deBlock scopeBlock, deExpression access, deFunction function) {
  deFunctionType type = deFunctionGetType(function);
  if ((type == DE_FUNC_MODULE || type == DE_FUNC_PACKAGE) &&
      deFunctionGetType(deBlockGetOwningFunction(scopeBlock)) != DE_FUNC_PACKAGE) {
    deExprError(access, "Cannot call function %s, which which has type %s\n",
       deFunctionGetName(function), deGetFunctionTypeName(type));
  }
}

// Determin if the access expression is a method call.
static bool isMethodCall(deExpression access) {
  if (deDatatypeGetType(deExpressionGetDatatype(access)) != DE_TYPE_FUNCTION ||
      deExpressionGetType(access) != DE_EXPR_DOT) {
    return false;
  }
  deExpression left = deExpressionGetFirstExpression(access);
  deDatatype datatype = deExpressionGetDatatype(left);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type == DE_TYPE_CLASS) {
    return true;
  }
  // Allow method calls on builtin types, such as array.length().
  return type != DE_TYPE_TEMPLATE  && type != DE_TYPE_FUNCTION;
}

// Return the named parameter variable.
static deVariable findNamedParam(deBlock block, deExpression param) {
  utSym name = deExpressionGetName(deExpressionGetFirstExpression(param));
  deIdent ident = deBlockFindIdent(block, name);
  if (ident == deIdentNull || deIdentGetType(ident) != DE_IDENT_VARIABLE) {
    deExprError(param, "Undefined named parameter: %s", utSymGetName(name));
  }
  deVariable var = deIdentGetVariable(ident);
  if (deVariableGetType(var) != DE_VAR_PARAMETER) {
    deExprError(param, "Undefined named parameter: %s", utSymGetName(name));
  }
  return var;
}

// Check that the expression's datatype is not none.
static void checkNotNone(deExpression expr) {
  if (deExpressionGetDatatype(expr) == deNoneDatatypeCreate()) {
    deExprError(expr, "Expression has none type.");
  }
}

// Find call datatypes.  For default parameters with default values that are
// not specified by the caller, use deNullDatatype, as this will be bound when
// expression the function.
static deDatatypeArray findCallDatatypes(deBlock scopeBlock,
    deExpression expression, deFunction function, deExpression params) {
  deDatatypeArray paramTypes = deDatatypeArrayAlloc();
  deDatatypeArray templParams = deDatatypeArrayNull;
  deBlock block = deFunctionGetSubBlock(function);
  uint32 numParams = deBlockCountParameterVariables(block);
  deDatatypeArrayResizeDatatypes(paramTypes, numParams);
  deDatatypeArraySetUsedDatatype(paramTypes, numParams);
  deDatatypeArraySetUsedDatatype(paramTypes, numParams);
  deVariable var = deBlockGetFirstVariable(block);
  deExpression access = deExpressionGetFirstExpression(expression);
  uint32 xParam = 0;
  deTemplate templ = deTemplateNull;
  if (deFunctionGetType(function) == DE_FUNC_CONSTRUCTOR) {
    templ = deFunctionGetTemplate(function);
    if (deDatatypeArrayGetUsedDatatype(paramTypes) == 0) {
      deError(deFunctionGetLine(function), "Constructors require a \"self\" parameter");
    }
    if (deTemplateIsTemplate(templ)) {
      templParams = deDatatypeArrayAlloc();
    }
    // Set a dummy value for now.  It is fixed at the end of this function.
    deDatatypeArraySetiDatatype(paramTypes, 0, deNoneDatatypeCreate());
    xParam++;
    var = deVariableGetNextBlockVariable(var);
  } else if (isMethodCall(access)) {
    // Add the type of the object on the left of the dot expression as self parameter.
    deExpression selfExpr = deExpressionGetFirstExpression(access);
    checkNotNone(selfExpr);
    deDatatype selfType = deExpressionGetDatatype(selfExpr);
    if (deDatatypeArrayGetUsedDatatype(paramTypes) == 0) {
      deExprError(access, "Function %s lacks a self parameter, but is called as a method",
          deFunctionGetName(function));
    }
    deDatatypeArraySetiDatatype(paramTypes, xParam, selfType);
    xParam++;
    var = deVariableGetNextBlockVariable(var);
  }
  deExpression param = deExpressionGetFirstExpression(params);
  bool foundNamedParam = false;
  while (param != deExpressionNull) {
    checkNotNone(param);
    foundNamedParam |= deExpressionGetType(param) == DE_EXPR_NAMEDPARAM;
    if (!foundNamedParam) {
      if (var == deVariableNull || deVariableGetType(var) != DE_VAR_PARAMETER) {
        deExprError(params, "Too many arguments passed to function %s", deFunctionGetName(function));
      }
      deDatatypeArraySetiDatatype(paramTypes, xParam, deExpressionGetDatatype(param));
      deExpressionSetSignaturePos(param, xParam);
      xParam++;
      var = deVariableGetNextBlockVariable(var);
    } else {
      var = findNamedParam(block, param);
      uint32 index = deBlockFindVariableIndex(block, var);
      if (deDatatypeArrayGetiDatatype(paramTypes, index) != deDatatypeNull) {
        deExprError(param, "Named parameter assigned twice");
      }
      deDatatypeArraySetiDatatype(paramTypes, index, deExpressionGetDatatype(param));
      deExpressionSetSignaturePos(param, index);
    }
    param = deExpressionGetNextExpression(param);
  }
  var = deBlockGetFirstVariable(block);
  for (uint32 xParam = 0; xParam < deDatatypeArrayGetUsedDatatype(paramTypes); xParam++) {
    deDatatype datatype = deDatatypeArrayGetiDatatype(paramTypes, xParam);
    if (datatype == deDatatypeNull && deVariableGetInitializerExpression(var) == deExpressionNull) {
      deExprError(params, "Parameter %s was not set and has no default value", deVariableGetName(var));
    }
    if (deVariableInTemplateSignature(var)) {
      utAssert(datatype != deDatatypeNull);
      deDatatypeArrayAppendDatatype(templParams, datatype);
    }
    var = deVariableGetNextBlockVariable(var);
  }
  if (var != deVariableNull && deVariableGetType(var) == DE_VAR_PARAMETER) {
    deExprError(params, "Too few arguments passed to function %s", deFunctionGetName(function));
  }
  if (deFunctionGetType(function) == DE_FUNC_CONSTRUCTOR) {
    deClass theClass;
    if (deTemplateIsTemplate(templ)) {
      theClass = deTemplateFindClassFromParams(templ, templParams);
    } else {
      theClass = deTemplateGetDefaultClass(templ);
    }
    utAssert(theClass != deClassNull);
    deDatatypeArraySetiDatatype(paramTypes, 0, deClassGetDatatype(theClass));
  }
  return paramTypes;
}

// Find the function being called from the bound access expression.  There are
// three cases: a normal function call, a method call on a Template, and a call on
// a concrete type such as an array.
static deFunction findCalledFunction(deExpression access) {
  deDatatype accessDatatype = deExpressionGetDatatype(access);
  deDatatypeType accessType = deDatatypeGetType(accessDatatype);
  bool isTemplate = accessType == DE_TYPE_TEMPLATE;
  if (!isTemplate && accessType != DE_TYPE_FUNCTION) {
    deTemplate templ = deFindDatatypeTemplate(accessDatatype);
    if (templ == deTemplateNull) {
      deExprError(access, "Cannot call object of type %s\n", deDatatypeGetTypeString(accessDatatype));
    }
    accessDatatype = deTemplateDatatypeCreate(templ);
    isTemplate = true;
  }
  return isTemplate? deTemplateGetFunction(deDatatypeGetTemplate(accessDatatype)) :
      deDatatypeGetFunction(accessDatatype);
}

// Compare the parameter types to the function pointer parameter types from the
// call type.  Report an error on mismatch.
static void compareFuncptrParameters(deDatatype callType,
                                     deExpression params) {
  uint32 numParameters = deDatatypeGetNumTypeList(callType);
  uint32 numPassed = deExpressionCountExpressions(params);
  if (numPassed != numParameters) {
    deExprError(params, "Wrong number of parameters to function call: Expected %u, have %u",
            numParameters, numPassed);
  }
  deExpression param = deExpressionGetFirstExpression(params);
  for (uint32 i = 0; i < numParameters; i++) {
    if (deDatatypeGetiTypeList(callType, i) != deExpressionGetDatatype(param)) {
      deExprError(param, "Incorrect type passed in argument %u", i);
    }
    param = deExpressionGetNextExpression(param);
  }
}

// Bind a function pointer call.
static void bindFunctionPointerCall(deBlock scopeBlock, deExpression expression) {
  deExpression access = deExpressionGetFirstExpression(expression);
  deExpression params = deExpressionGetNextExpression(access);
  deDatatype callType = deExpressionGetDatatype(access);
  compareFuncptrParameters(callType, params);
  deDatatype returnType = deDatatypeGetReturnType(callType);
  deExpressionSetDatatype(expression, returnType);
}

// Mark the class created by the constructor as bound.
static void markConstructorClassBound(deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  deTemplate templ = deFunctionGetTemplate(function);
  deClass theClass = deClassCreate(templ, signature);
  if (!deClassBound(theClass)) {
    deParamspec paramspec = deSignatureGetiParamspec(signature, 0);
    deSignatureSetReturnType(signature, deParamspecGetDatatype(paramspec));
    deCopyFunctionIdentsToBlock(deFunctionGetSubBlock(function), deClassGetSubBlock(theClass));
  }
  if (deSignatureGetClass(signature) == deClassNull) {
    deClassAppendSignature(theClass, signature);
  }
  deSignatureSetReturnType(signature, deClassGetDatatype(theClass));
  deSignatureSetBound(signature, true);
  deQueueEventBlockedBindings(deSignatureGetReturnEvent(signature));
  deClassSetBound(theClass, true);
  deCreateBlockVariables(deClassGetSubBlock(theClass), deFunctionGetSubBlock(function));
}

// Bind a call expression.
static bool bindCallExpression(deBlock scopeBlock, deExpression expression) {
  deExpression access = deExpressionGetFirstExpression(expression);
  if (deDatatypeGetType(deExpressionGetDatatype(access)) == DE_TYPE_FUNCPTR) {
    bindFunctionPointerCall(scopeBlock, expression);
    return true;
  }
  deExpression params = deExpressionGetNextExpression(access);
  deFunction function = findCalledFunction(access);
  verifyFunctionIsCallable(scopeBlock, access, function);
  deDatatypeArray paramTypes = findCallDatatypes(scopeBlock, expression, function, params);
  deLine line = deExpressionGetLine(expression);
  if (deFunctionBuiltin(function)) {
    deDatatype returnType = deBindBuiltinCall(scopeBlock, function, paramTypes, expression);
    deExpressionSetDatatype(expression, returnType);
    deDatatypeArrayFree(paramTypes);
    return true;
  }
  deSignature signature = deLookupSignature(function, paramTypes);
  if (signature == deSignatureNull) {
    deSetStackTraceGlobals(expression);
    signature = deSignatureCreate(function, paramTypes, line);
    if (deSignatureIsConstructor(signature)) {
      markConstructorClassBound(signature);
    }
  } else {
    deDatatypeArrayFree(paramTypes);
  }
  deExpressionSetSignature(expression, signature);
  deSignatureSetInstantiated(signature,
      deSignatureInstantiated(signature) || deExpressionInstantiating(expression));
  deQueueSignature(signature);
  if (!deSignatureBound(signature)) {
    deEvent event = deSignatureEventCreate(signature);
    deBinding binding = deExpressionGetBinding(expression);
    deEventAppendBinding(event, binding);
    return false;
  }
  deExpressionSetDatatype(expression, deSignatureGetReturnType(signature));
  return true;  // Success.
}

// Determine if the expression is an identifier bound to a non-const variable.
static bool expressionIsNonConstVariable(deExpression expression) {
  if (deExpressionGetType(expression) != DE_EXPR_IDENT) {
    return false;
  }
  deIdent ident = deExpressionGetIdent(expression);
  if (deIdentGetType(ident) != DE_IDENT_VARIABLE) {
    return false;
  }
  deVariable variable = deIdentGetVariable(ident);
  return !deVariableConst(variable);
}

// Check that type parameters are not instantiated in the signature and that
// only non-constant variables are passed to var parameters.
static void checkPassedParameters(deExpression expression) {
  deSignature signature = deExpressionGetSignature(expression);
  if (signature == deSignatureNull || deSignatureIsStruct(signature)) {
    return;
  }
  deExpression params = deExpressionGetLastExpression(expression);
  deExpression param;
  deForeachExpressionExpression(params, param) {
    deParamspec paramspec = deSignatureGetiParamspec(signature, deExpressionGetSignaturePos(param));
    deVariable var = deParamspecGetVariable(paramspec);
    if (deExpressionInstantiating(param) && deExpressionIsType(param) && deParamspecInstantiated(paramspec)) {
        deExprError(param, "Parameter %s cannot be a type since its value is used",
            deVariableGetName(var));
    }
    if (!deVariableConst(var) && !expressionIsNonConstVariable(param)) {
      deExprError(param,
          "Parameter %s must be passed a non-const variable", deVariableGetName(var));
    }
  } deEndExpressionExpression;
}

// Bind the index expression.
static void bindIndexExpression(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deExprError(expression, "Index values must be uint");
  }
  if (deDatatypeSecret(rightType)) {
    deExprError(expression, "Indexing with a secret is not allowed");
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_ARRAY && type != DE_TYPE_STRING && type != DE_TYPE_TUPLE &&
      type != DE_TYPE_STRUCT) {
    deExprError(expression, "Index into non-array/non-string/non-tuple type");
  }
  if (type == DE_TYPE_TUPLE || type == DE_TYPE_STRUCT) {
    if (deExpressionGetType(right) != DE_EXPR_INTEGER) {
      deExprError(expression,
          "Tuples can only be indexed by constant integers, like y = point[1]");
    }
    uint32 index = deBigintGetUint32(deExpressionGetBigint(right), deExpressionGetLine(expression));
    if (index >= deDatatypeGetNumTypeList(leftType)) {
      deExprError(expression, "Tuple index out of bounds");
    }
    deExpressionSetDatatype(expression, deDatatypeGetiTypeList(leftType, index));
  } else {
    deDatatype elementType = deDatatypeGetElementType(leftType);
    deExpressionSetDatatype(expression, elementType);
  }
  deExpressionSetConst(expression, deExpressionConst(left));
}

// Find the scope signature for the datatype bound on the expression.  If non
// exists, report an error.
static deBlock findExpressionSubScope(deExpression expression) {
  deDatatype datatype = deExpressionGetDatatype(expression);
  utAssert(datatype != deDatatypeNull);
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_NONE:
    case DE_TYPE_BOOL:
    case DE_TYPE_STRING:
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_MODINT:
    case DE_TYPE_FLOAT:
    case DE_TYPE_ARRAY:
    case DE_TYPE_ENUM:
    case DE_TYPE_FUNCPTR:
    case DE_TYPE_EXPR:
      deExprError(expression, "Cannot use '.' on  datatype %s", deDatatypeGetTypeString(datatype));
      break;
    case DE_TYPE_CLASS:
      return deClassGetSubBlock(deDatatypeGetClass(datatype));
    case DE_TYPE_TEMPLATE:
      return deFunctionGetSubBlock(deTemplateGetFunction(deDatatypeGetTemplate(datatype)));
    case DE_TYPE_FUNCTION:
    case DE_TYPE_TUPLE:
    case DE_TYPE_STRUCT:
    case DE_TYPE_ENUMCLASS:
      return deFunctionGetSubBlock(deDatatypeGetFunction(datatype));
  }
  return deBlockNull;  // Dummy return;
}

// Find an existing variable with the given name, or create it if it does not exist.
// Also bind the ident expression expression to its identifier.
static deVariable findOrCreateVariable(deBlock scopeBlock, deExpression access) {
  deExpression identExpr;
  bool inScopeBlock = false;
  if (deExpressionGetType(access) == DE_EXPR_IDENT) {
    identExpr = access;
  } else {
    utAssert(deExpressionGetType(access) == DE_EXPR_DOT);
    deExpression dotAccess = deExpressionGetFirstExpression(access);
    identExpr = deExpressionGetNextExpression(dotAccess);
    scopeBlock = findExpressionSubScope(dotAccess);
    inScopeBlock = true;
  }
  utAssert(deExpressionGetType(identExpr) == DE_EXPR_IDENT);
  deIdent ident = deExpressionGetIdent(identExpr);
  if (ident != deIdentNull) {
    deIdentRemoveExpression(ident, identExpr);
  }
  utSym sym = deExpressionGetName(identExpr);
  if (inScopeBlock) {
    ident = deBlockFindIdent(scopeBlock, sym);
  } else {
    ident = deFindIdent(scopeBlock, sym);
  }
  if (ident == deIdentNull || deIdentGetType(ident) == DE_IDENT_UNDEFINED) {
    bool generated = deStatementGenerated(deFindExpressionStatement(access));
    deLine line = deExpressionGetLine(identExpr);
    deVariable var = deVariableCreate(scopeBlock, DE_VAR_LOCAL, false, sym,
        deExpressionNull, generated, line);
    deExpression typeExpr = deExpressionGetNextExpression(deExpressionGetNextExpression(access));
    if (typeExpr != deExpressionNull) {
      deVariableInsertTypeExpression(var, typeExpr);
      deExpression assignment = deExpressionGetExpression(access);
      deSignature signature = deBindingGetSignature(deExpressionGetBinding(assignment));
      deCreateVariableConstraintBinding(signature, var);
    }
    ident = deVariableGetIdent(var);
  }
  if (deIdentGetType(ident) == DE_IDENT_FUNCTION) {
    deExprError(access, "%s is a function, and cannot be assigned.", utSymGetName(sym));
  }
  deIdentAppendExpression(ident, identExpr);
  return deIdentGetVariable(ident);
}

// Update a variable from an assignment expression.
static void updateVariable(deBlock scopeBlock, deVariable variable,
    deDatatype newDatatype, deExpression expr) {
  utAssert(newDatatype != deDatatypeNull);
  deDatatype oldDatatype = deVariableGetDatatype(variable);
  deDatatype datatype = newDatatype;
  if (oldDatatype != deDatatypeNull) {
    datatype = deUnifyDatatypes(oldDatatype, newDatatype);
  }
  if (datatype == deDatatypeNull) {
    deExprError(expr, "Assigning different type to %s than assigned before:%s",
      deVariableGetName(variable), deGetOldVsNewDatatypeStrings(oldDatatype, newDatatype));
  }
  deVariableSetDatatype(variable, datatype);
  if ((oldDatatype == deDatatypeNull || !deDatatypeConcrete(oldDatatype)) &&
      deDatatypeConcrete(datatype)) {
    deQueueEventBlockedBindings(deVariableGetEvent(variable));
  }
}

// Determine if the expression is bound to an iterator.
static bool datatypeIsIterator(deDatatype datatype) {
  if (deDatatypeGetType(datatype) != DE_TYPE_FUNCTION) {
    return false;
  }
  deFunction function = deDatatypeGetFunction(datatype);
  return deFunctionGetType(function) == DE_FUNC_ITERATOR;
}

// Automatically add .values() in for <var> in <expr> statements when <expr>
// does not already name an iterator.  This lets us use Python-like loops like
// 'for i in [1, 2, 3] {'.  It also allows classes to define 'iterator
// values(self)' so for example, an instance of Set called set could work with
// 'for element in set'.
static bool addValuesIteratorIfNeeded(deBlock scopeBlock, deStatement statement) {
  deExpression assignment = deStatementGetExpression(statement);
  deExpression access = deExpressionGetFirstExpression(assignment);
  deExpression callExpr = deExpressionGetNextExpression(access);
  if (deExpressionGetType(callExpr) == DE_EXPR_CALL) {
    deDatatype datatype = deExpressionGetDatatype(deExpressionGetFirstExpression(callExpr));
    if (datatypeIsIterator(datatype)) {
      return false;  // Already have an iterator.
    }
  }
  // Add .values().
  deExpressionRemoveExpression(assignment, callExpr);
  deLine line = deExpressionGetLine(callExpr);
  deExpression valuesExpr = deIdentExpressionCreate(utSymCreate("values"), line);
  deExpression dotExpr = deBinaryExpressionCreate(DE_EXPR_DOT, callExpr, valuesExpr, line);
  deExpression emptyParamsExpr = deExpressionCreate(DE_EXPR_LIST, line);
  deExpression valuesCallExpr = deBinaryExpressionCreate(DE_EXPR_CALL, dotExpr,
      emptyParamsExpr, line);
  deExpressionAppendExpression(assignment, valuesCallExpr);
  return true;
}

// Check the type constraint on the assignment expression.
static void checkAssignmentTypeConstraint(deBlock scopeBlock, deExpression expression) {
  deExpression access = deExpressionGetFirstExpression(expression);
  deExpression value = deExpressionGetNextExpression(access);
  deExpression constraint = deExpressionGetNextExpression(value);
  if (constraint == deExpressionNull) {
    return;
  }
  deDatatype datatype = deExpressionGetDatatype(value);
  if (!deDatatypeMatchesTypeExpression(scopeBlock, datatype, constraint)) {
    deExprError(expression, "Violation of type constraint: %s",
        deDatatypeGetTypeString(datatype));
  }
}

// Bind an assignment expression.
static deBindRes bindAssignmentExpression(deBlock scopeBlock, deExpression expression) {
  deExpression access = deExpressionGetFirstExpression(expression);
  deExpression value = deExpressionGetNextExpression(access);
  deExpressionType type  = deExpressionGetType(access);
  deDatatype valueDatatype = deExpressionGetDatatype(value);
  if (deDatatypeGetType(valueDatatype) == DE_TYPE_NONE) {
    deExprError(expression, "Right side of assignment does not return a value.");
  }
  deStatement statement = deExpressionGetStatement(expression);
  if (statement != deStatementNull && deStatementGetType(statement) == DE_STATEMENT_FOREACH) {
    if (addValuesIteratorIfNeeded(scopeBlock, statement)) {
      return DE_BINDRES_REBIND;
    }
  }
  if (type == DE_EXPR_IDENT || type == DE_EXPR_DOT) {
    deVariable variable = findOrCreateVariable(scopeBlock, access);
    if (deVariableConst(variable)) {
      deExprError(expression, "Assigning to const variable %s ", deVariableGetName(variable));
    }
    updateVariable(scopeBlock, variable, valueDatatype, expression);
    if (deExpressionIsType(value)) {
      deVariableSetIsType(variable, true);
    }
    deExpressionSetDatatype(access, valueDatatype);
  }
  deExpressionSetDatatype(expression, valueDatatype);
  checkAssignmentTypeConstraint(scopeBlock, expression);
  return DE_BINDRES_OK;
}

// Bind the array expression.
static void bindArrayExpression(deBlock scopeBlock, deExpression expression) {
  deExpression firstElement = deExpressionGetFirstExpression(expression);
  deDatatype datatype = deExpressionGetDatatype(firstElement);
  if (deExpressionIsType(firstElement)) {
    deExpressionSetIsType(expression, true);
  }
  deExpression nextElement = deExpressionGetNextExpression(firstElement);
  while (nextElement != deExpressionNull) {
    deDatatype elementType = deExpressionGetDatatype(nextElement);
    if (elementType != datatype) {
      if (deSetDatatypeNullable(datatype, true) == elementType) {
        datatype = elementType;  // Allow null class elements.
      } else if (deSetDatatypeNullable(elementType, true) != datatype) {
        deExprError(expression, "Array elements must have the same type:%s",
            deGetOldVsNewDatatypeStrings(deExpressionGetDatatype(nextElement), datatype));
      }
    }
    if (deExpressionIsType(nextElement)) {
      deExprError(expression, "Array type expressions can contain only one type, like [u32]");
    }
    nextElement = deExpressionGetNextExpression(nextElement);
  }
  deDatatype arrayDatatype = deArrayDatatypeCreate(datatype);
  deExpressionSetDatatype(expression, arrayDatatype);
}

// Bind a dot expression.  If we're expression a constructor, search in the
// current theClass rather than the class constructor.  We can't bind the ident
// to the right of the dot using scopeBlock as the scope.  We instead must wait
// until the left side is bound.  We bind the right hand side identifier
// expression here.
static bool bindDotExpression(deBlock scopeBlock, deExpression expression) {
  deExpression accessExpr = deExpressionGetFirstExpression(expression);
  deExpression identExpr = deExpressionGetNextExpression(accessExpr);
  deDatatype datatype = deExpressionGetDatatype(accessExpr);
  deDatatypeType type = deDatatypeGetType(datatype);
  deBlock classBlock;
  if (type == DE_TYPE_CLASS) {
    if (deDatatypeNullable(datatype)) {
      // Infer the ! operator.
      datatype = deSetDatatypeNullable(datatype, false);
      deExpressionSetDatatype(accessExpr, datatype);
    }
    classBlock = deClassGetSubBlock(deDatatypeGetClass(datatype));
  } else if (type == DE_TYPE_TEMPLATE) {
    classBlock = deFunctionGetSubBlock(deTemplateGetFunction(deDatatypeGetTemplate(datatype)));
  } else if (type == DE_TYPE_FUNCTION || type == DE_TYPE_STRUCT || type == DE_TYPE_ENUMCLASS) {
    deFunction function = deDatatypeGetFunction(datatype);
    deFunctionType funcType = deFunctionGetType(function);
    if (funcType != DE_FUNC_PACKAGE && funcType != DE_FUNC_MODULE &&
        funcType != DE_FUNC_STRUCT && funcType != DE_FUNC_ENUM) {
      deExprError(expression, "Cannot access identifiers inside function %s",
          deFunctionGetName(function));
    }
    classBlock = deFunctionGetSubBlock(function);
  } else {
    // Some builtin types have method calls.
    deTemplate templ = deFindDatatypeTemplate(datatype);
    classBlock = deFunctionGetSubBlock(deTemplateGetFunction(templ));
  }
  utAssert(deExpressionGetType(identExpr) == DE_EXPR_IDENT);
  // Make the right-hand expression, if we haven't already.
  deBinding binding = deExpressionGetBinding(expression);
  if (classBlock != deBlockNull) {
    if (!bindIdentExpression(classBlock, binding, identExpr, true)) {
      return false;
    }
  } else {
    if (!bindIdentExpression(scopeBlock, binding, identExpr, false)) {
      return false;
    }
  }
  deExpressionSetDatatype(expression, deExpressionGetDatatype(identExpr));
  deExpressionSetConst(expression, deExpressionConst(identExpr));
  deExpressionSetIsType(expression, deExpressionIsType(identExpr));
  return true;
}

// Bind an isnull expression.  The expression type is bool.
static void bindIsnullExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type != DE_TYPE_CLASS) {
    deExprError(expression, "isnull applied to non-object");
  }
  deExpressionSetDatatype(expression, deBoolDatatypeCreate());
}

// Bind a named parameter.  Just skip the name, and set the type to the type of
// the expression on the right.
static void bindNamedParameter(deBlock scopeBlock, deExpression expression) {
  deExpression right = deExpressionGetLastExpression(expression);
  deExpressionSetDatatype(expression, deExpressionGetDatatype(right));
  deExpressionSetIsType(expression, deExpressionIsType(right));
  // Find the variable in the called function so we can bind to it.
  deExpression call = deExpressionGetExpression(deExpressionGetExpression(expression));
  utAssert(deExpressionGetType(call) == DE_EXPR_CALL);
  deExpression callAccess = deExpressionGetFirstExpression(call);
  deFunction function = findCalledFunction(callAccess);
  deBlock block = deFunctionGetSubBlock(function);
  deExpression paramNameExpression = deExpressionGetFirstExpression(expression);
  utSym paramName = deExpressionGetName(paramNameExpression);
  deIdent ident = deBlockFindIdent(block, paramName);
  if (ident == deIdentNull || deIdentGetType(ident) != DE_IDENT_VARIABLE) {
    deExprError(expression, "No parameter named %s found", utSymGetName(paramName));
  }
  deVariable var = deIdentGetVariable(ident);
  if (deVariableGetType(var) != DE_VAR_PARAMETER) {
    deExprError(expression, "Variable %s is a local variable, not a parameter", utSymGetName(paramName));
  }
  if (ident != deIdentNull) {
    deIdentRemoveExpression(ident, paramNameExpression);
  }
  deIdentAppendExpression(ident, paramNameExpression);
}

// Bind a template instantiation, e.g. Point<i322, i32>.  The list expression of
// types on the right instantiates a class instance of the template on the left.
static void bindTemplateInst(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDatatype leftType = deExpressionGetDatatype(left);
  if (deDatatypeGetType(leftType) != DE_TYPE_TEMPLATE) {
    deExprError(expression, "Only template classes can have template parameters");
  }
  deTemplate templ = deDatatypeGetTemplate(leftType);
  deDatatypeArray templParams = listTemplateInstDatatypes(right);
  uint32 numParams = deDatatypeArrayGetUsedDatatype(templParams);
  uint32 expectedParams = deTemplateGetNumTemplateParams(templ);
  if (numParams != expectedParams) {
    deExprError(expression, "Template class %s expected %u parameters, got %u",
        deTemplateGetName(templ), expectedParams, numParams);
  }
  deClass theClass = deTemplateFindClassFromParams(templ, templParams);
  deExpressionSetDatatype(expression, deClassGetDatatype(theClass));
}

// Bind the expression's expression.
static deBindRes bindExpression(deBlock scopeBlock, deExpression expression, bool isTypeExpr) {
  deDatatype oldDatatype = deExpressionGetDatatype(expression);
  if (oldDatatype != deDatatypeNull && deDatatypeGetType(oldDatatype) == DE_TYPE_MODINT) {
    // TODO: Add support for operator overloading in modular expressions.
    postProcessModintExpression(expression);
    return DE_BINDRES_OK;  // Success.
  }
  if (expressionTypeCanBeOverloaded(opEqualsToOp(deExpressionGetType(expression)))) {
    deBindRes result = bindOverloadedOperator(scopeBlock, expression);
    if (result != DE_BINDRES_FAILED) {
      return result;
    }
  }
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_INTEGER:
      bindIntegerExpression(expression);
      break;
    case DE_EXPR_FLOAT:
      bindFloatExpression(expression);
      break;
    case DE_EXPR_BOOL:
      deExpressionSetDatatype(expression, deBoolDatatypeCreate());
      break;
    case DE_EXPR_STRING:
      deExpressionSetDatatype(expression, deStringDatatypeCreate());
      break;
    case DE_EXPR_IDENT:
      if (!bindIdentExpression(scopeBlock, deExpressionGetBinding(expression), expression, false)) {
        return DE_BINDRES_BLOCKED;
      }
      break;
    case DE_EXPR_ARRAY:
      bindArrayExpression(scopeBlock, expression);
      break;
    case DE_EXPR_RANDUINT:
      bindRandUintExpression(expression);
      break;
    case DE_EXPR_MODINT:
      bindModintExpression(scopeBlock, expression);
      break;
    case DE_EXPR_BITOR :
    case DE_EXPR_BITOR_EQUALS:
      bindBitwiseOrExpression(scopeBlock, expression, isTypeExpr);
      break;
    case DE_EXPR_ADD:
    case DE_EXPR_SUB:
    case DE_EXPR_MUL:
    case DE_EXPR_DIV:
      bindBinaryArithmeticExpression(scopeBlock, expression);
      break;
    case DE_EXPR_ADD_EQUALS:
    case DE_EXPR_SUB_EQUALS:
    case DE_EXPR_MUL_EQUALS:
    case DE_EXPR_DIV_EQUALS:
      bindBinaryArithmeticExpression(scopeBlock, expression);
      checkOpEqualsAssignment(scopeBlock, expression);
      break;
    case DE_EXPR_BITAND:
    case DE_EXPR_BITAND_EQUALS:
    case DE_EXPR_BITXOR:
    case DE_EXPR_BITXOR_EQUALS:
    case DE_EXPR_ADDTRUNC:
    case DE_EXPR_ADDTRUNC_EQUALS:
    case DE_EXPR_SUBTRUNC:
    case DE_EXPR_SUBTRUNC_EQUALS:
    case DE_EXPR_MULTRUNC:
    case DE_EXPR_MULTRUNC_EQUALS:
      bindBinaryArithmeticExpression(scopeBlock, expression);
      if (deDatatypeIsFloat(deExpressionGetDatatype(expression))) {
        deExprError(expression,
                "Invalid binary operation on floating point types.");
      }
      break;
    case DE_EXPR_MOD:
    case DE_EXPR_MOD_EQUALS:
      bindModExpression(scopeBlock, expression);
      break;
    case DE_EXPR_AND:
    case DE_EXPR_AND_EQUALS:
    case DE_EXPR_OR :
    case DE_EXPR_OR_EQUALS :
    case DE_EXPR_XOR:
    case DE_EXPR_XOR_EQUALS:
      bindBinaryBool(scopeBlock, expression);
      break;
    case DE_EXPR_EXP:
    case DE_EXPR_EXP_EQUALS:
      bindExponentiationExpression(scopeBlock, expression);
      break;
    case DE_EXPR_SHL:
    case DE_EXPR_SHL_EQUALS:
    case DE_EXPR_SHR:
    case DE_EXPR_SHR_EQUALS:
    case DE_EXPR_ROTL:
    case DE_EXPR_ROTL_EQUALS:
    case DE_EXPR_ROTR:
    case DE_EXPR_ROTR_EQUALS:
      bindShiftExpression(scopeBlock, expression);
      break;
    case DE_EXPR_LT:
    case DE_EXPR_LE:
    case DE_EXPR_GT:
    case DE_EXPR_GE:
      bindRelationalExpression(scopeBlock, expression);
      break;
    case DE_EXPR_EQUAL:
    case DE_EXPR_NOTEQUAL:
      bindEqualityExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NEGATE:
    case DE_EXPR_NEGATETRUNC:
    case DE_EXPR_BITNOT:
      bindUnaryArithmeticExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NOT:
      bindNotExpression(scopeBlock, expression);
      break;
    case DE_EXPR_CAST:
    case DE_EXPR_CASTTRUNC:
      bindCastExpression(expression);
      break;
    case DE_EXPR_SELECT:
      bindSelectExpression(scopeBlock, expression);
      break;
    case DE_EXPR_CALL:
      if (!bindCallExpression(scopeBlock, expression)) {
        return DE_BINDRES_BLOCKED;
      }
      checkPassedParameters(expression);
      break;
    case DE_EXPR_INDEX:
      bindIndexExpression(scopeBlock, expression);
      break;
    case DE_EXPR_SLICE:
      bindSliceExpression(scopeBlock, expression);
      break;
    case DE_EXPR_SECRET:
    case DE_EXPR_REVEAL:
      bindMarkSecretOrPublic(scopeBlock, expression);
      deExpressionSetIsType(expression, deExpressionIsType(deExpressionGetFirstExpression(expression)));
      break;
    case DE_EXPR_EQUALS:
      return bindAssignmentExpression(scopeBlock, expression);
    case DE_EXPR_DOT:
      if (!bindDotExpression(scopeBlock, expression)) {
        return DE_BINDRES_BLOCKED;
      }
      break;
    case DE_EXPR_DOTDOTDOT:
      bindDotDotDotExpression(scopeBlock, expression);
      break;
    case DE_EXPR_LIST:
      // Happens in print statements.
      deExpressionSetDatatype(expression, deNoneDatatypeCreate());
      break;
    case DE_EXPR_TUPLE:
      bindTupleExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NULL:
      bindNullExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NOTNULL:
      bindNotNullExpression(scopeBlock, expression);
      break;
    case DE_EXPR_FUNCADDR:
      bindFunctionPointerExpression(expression);
      break;
    case DE_EXPR_ARRAYOF:
      bindArrayofExpression(scopeBlock, expression);
      break;
      break;
    case DE_EXPR_TYPEOF:
      bindTypeofExpression(scopeBlock, expression);
      break;
    case DE_EXPR_UNSIGNED:
    case DE_EXPR_SIGNED:
      bindSignConversionExpression(scopeBlock, expression);
      break;
    case DE_EXPR_WIDTHOF:
      bindWidthofExpression(scopeBlock, expression);
      break;
    case DE_EXPR_ISNULL:
      bindIsnullExpression(scopeBlock, expression);
      break;
    case DE_EXPR_UINTTYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deUintDatatypeCreate(deExpressionGetWidth(expression)));
      break;
    case DE_EXPR_INTTYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deIntDatatypeCreate(deExpressionGetWidth(expression)));
      break;
    case DE_EXPR_FLOATTYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deFloatDatatypeCreate(deExpressionGetWidth(expression)));
      break;
    case DE_EXPR_STRINGTYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deStringDatatypeCreate());
      break;
    case DE_EXPR_BOOLTYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deBoolDatatypeCreate());
      break;
    case DE_EXPR_NONETYPE:
      deExpressionSetIsType(expression, true);
      deExpressionSetDatatype(expression, deNoneDatatypeCreate());
      break;
    case DE_EXPR_AS:
      utExit("Unexpected expression type");
      break;
    case DE_EXPR_IN:
      bindInExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NAMEDPARAM:
      bindNamedParameter(scopeBlock, expression);
      break;
    case DE_EXPR_TEMPLATE_INST:
      bindTemplateInst(scopeBlock, expression);
      break;
  }
  return DE_BINDRES_OK;  // Success.
}

// Update the signature's return type.  If this sets the return type for the
// first time, trigger the signature's event.
static void updateSignatureReturnType(deSignature signature, deDatatype datatype) {
  deDatatype oldDatatype = deSignatureGetReturnType(signature);
  deDatatype newDatatype = datatype;
  if (oldDatatype != deDatatypeNull) {
    newDatatype = deUnifyDatatypes(oldDatatype, datatype);
  }
  if (oldDatatype == newDatatype) {
    return;
  }
  if (newDatatype == deDatatypeNull) {
    deLine line = deFunctionGetLine(deSignatureGetFunction(signature));
    deError(line,
        "Return statement has different type than prior return statement:%s",
        deGetOldVsNewDatatypeStrings(oldDatatype, datatype));
  }
  deSignatureSetReturnType(signature, datatype);
  if ((oldDatatype == deDatatypeNull || !deDatatypeConcrete(oldDatatype)) &&
      deDatatypeConcrete(newDatatype)) {
    deSignatureSetBound(signature, true);
    deQueueEventBlockedBindings(deSignatureGetReturnEvent(signature));
  }
}

// Select the matching case of a typeswitch statement.
static void selectMatchingCase(deBlock scopeBlock, deBinding binding) {
  deStatement typeSwitchStatement = deBindingGetStatement(binding);
  deDatatype datatype = deExpressionGetDatatype(deStatementGetExpression(typeSwitchStatement));
  bool instantiating = deBindingInstantiated(binding);
  utAssert(datatype != deDatatypeNull);
  deBlock subBlock = deStatementGetSubBlock(typeSwitchStatement);
  bool foundMatchingCase = false;
  deStatement caseStatement;
  deForeachBlockStatement(subBlock, caseStatement) {
    deStatementSetInstantiated(caseStatement, false);
    if (!foundMatchingCase) {
      if (deStatementGetType(caseStatement) == DE_STATEMENT_CASE) {
        deExpression typeExpressionList = deStatementGetExpression(caseStatement);
        deExpression typeExpression;
        deForeachExpressionExpression(typeExpressionList, typeExpression) {
          if (deDatatypeMatchesTypeExpression(scopeBlock, datatype, typeExpression)) {
            foundMatchingCase = true;
          }
        } deEndExpressionExpression;
      } else {
        utAssert(deStatementGetType(caseStatement) == DE_STATEMENT_DEFAULT);
        foundMatchingCase = true;
      }
      if (foundMatchingCase && instantiating) {
        deStatementSetInstantiated(caseStatement, true);
        deBlock caseBlock = deStatementGetSubBlock(caseStatement);
        deQueueBlockStatements(deBindingGetSignature(binding), caseBlock, instantiating);
      }
    }
  } deEndBlockStatement;
  if (!foundMatchingCase) {
    deError(deStatementGetLine(typeSwitchStatement), "No matching case found");
  }
}

// Depending on the statement type, we may have some tasks to do once the statement is bound.
static void postProcessBoundStatement(deBlock scopeBlock, deBinding binding) {
  deStatement statement = deBindingGetStatement(binding);
  deStatementSetInstantiated(statement, deBindingInstantiated(binding));
  deStatementType type = deStatementGetType(statement);
  if (type == DE_STATEMENT_RETURN || type == DE_STATEMENT_YIELD) {
    deExpression expr = deStatementGetExpression(statement);
    deDatatype datatype = deNoneDatatypeCreate();
    if (deStatementGetExpression(statement) != deExpressionNull) {
      checkNotNone(expr);
      datatype = deExpressionGetDatatype(expr);
    }
    updateSignatureReturnType(deBindingGetSignature(binding), datatype);
  } else if (type == DE_STATEMENT_TYPESWITCH) {
    selectMatchingCase(scopeBlock, binding);
  } else if (type == DE_STATEMENT_PRINT || type == DE_STATEMENT_RAISE) {
    dePostProcessPrintStatement(statement);
    if (type == DE_STATEMENT_RAISE) {
      deExpression expression = deStatementGetExpression(statement);
      deExpression enumExpr = deExpressionGetFirstExpression(expression);
      if (enumExpr == deExpressionNull) {
        deExprError(expression, "Raise statement requires an enum value first");
      }
      deDatatype datatype = deExpressionGetDatatype(enumExpr);
      if (datatype == deDatatypeNull || deDatatypeGetType(datatype) != DE_TYPE_ENUM) {
        deExprError(expression, "Raise statement requires an enum value first");
      }
    }
  } else if (type == DE_STATEMENT_IF) {
    deExpression expression = deStatementGetExpression(statement);
    deDatatype datatype = deExpressionGetDatatype(expression);
    if (datatype == deDatatypeNull || deDatatypeGetType(datatype) != DE_TYPE_BOOL) {
      deExprError(expression, "If statement requires a Boolean condition");
    }
  }
}

// Set the datatype of variable to that if its default value.
static void setDefaultVariableType(deBlock scopeBlock, deBinding binding) {
  deVariable var = deBindingGetInitializerVariable(binding);
  deExpression initExpr = deVariableGetInitializerExpression(var);
  updateVariable(scopeBlock, var, deExpressionGetDatatype(initExpr), initExpr);
}

// Rebuild the queue of expressions for the binding.  Only works for statement
// bindings.
static void rebuildBinding(deBinding binding) {
  utAssert(deBindingGetType(binding) == DE_BIND_STATEMENT);
  deExpression expression;
  deSafeForeachBindingExpression(binding, expression) {
    deBindingRemoveExpression(binding, expression);
  } deEndSafeBindingExpression;
  deStatement statement = deBindingGetStatement(binding);
  expression = deStatementGetExpression(statement);
  bool instantiating = deExpressionInstantiating(expression);
  deQueueExpression(binding, expression, instantiating, false);
}

// Bind or continue expression the statement.
void deBindStatement(deBinding binding) {
  deExpression expression = deBindingGetFirstExpression(binding);
  deBlock scopeBlock = deGetBindingBlock(binding);
  while (expression != deExpressionNull) {
    deBindingType type = deBindingGetType(binding);
    bool isTypeExpr = type == DE_BIND_VAR_CONSTRAINT || type == DE_BIND_FUNC_CONSTRAINT;
    deBindRes result = bindExpression(scopeBlock, expression, isTypeExpr);
    if (result == DE_BINDRES_BLOCKED) {
      return;
    } else if (result == DE_BINDRES_REBIND) {
      rebuildBinding(binding);
    } else {
      deBindingRemoveExpression(binding, expression);
    }
    expression = deBindingGetFirstExpression(binding);
  }
  switch (deBindingGetType(binding)) {
    case  DE_BIND_STATEMENT:
      postProcessBoundStatement(scopeBlock, binding);
      break;
    case DE_BIND_DEFAULT_VALUE:
      setDefaultVariableType(scopeBlock, binding);
      break;
    case DE_BIND_VAR_CONSTRAINT: {
      deVariable variable = deBindingGetTypeVariable(binding);
      deDatatype datatype = deVariableGetDatatype(variable);
      deExpression typeExpr = deVariableGetTypeExpression(variable);
      if (datatype != deDatatypeNull &&
          !deDatatypeMatchesTypeExpression(scopeBlock, datatype, typeExpr)) {
        deCurrentSignature = deBindingGetSignature(binding);
        deSigError(deCurrentSignature, "Failed type constraint: variable %s expected type %s, got: %s",
                   deVariableGetName(variable),
                   deEscapeString(deExpressionToString(typeExpr)),
                   deDatatypeGetTypeString(datatype));
      }
      break;
    }
    case DE_BIND_FUNC_CONSTRAINT: {
      deFunction function = deBindingGetTypeFunction(binding);
      deExpression typeExpr = deFunctionGetTypeExpression(function);
      deDatatype datatype = deExpressionGetDatatype(typeExpr);
      deSignature signature = deBindingGetSignature(binding);
      if (datatype != deDatatypeNull && deDatatypeConcrete(datatype) &&
          signature != deSignatureNull) {
        updateSignatureReturnType(deBindingGetSignature(binding), datatype);
      }
      if (deFunctionExtern(function)) {
        if (datatype != deDatatypeNull) {
          datatype = deFindUniqueConcreteDatatype(datatype,typeExpr);
        }
        if (datatype == deDatatypeNull || !deDatatypeConcrete(datatype)) {
          printf("Extern function return type: %s\n", deDatatypeGetTypeString(datatype));
          deSigError(signature, "Extern function return types must be concrete");
        }
        deSignatureSetReturnType(signature, datatype);
        if (!deSignatureBound(signature)) {
          deSignatureSetBound(signature, true);
          deQueueEventBlockedBindings(deSignatureGetReturnEvent(signature));
        }
      }
      break;
    }
  }
  if (deBindingGetFirstExpression(binding) != deExpressionNull) {
    // We must have queued more expressions during post-processing.
    deBindStatement(binding);
  }
}
