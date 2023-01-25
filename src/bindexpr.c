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

// TODO: Re-implement overloaded operators.

typedef enum {
  DE_BINDRES_OK,
  DE_BINDRES_BLOCKED,
  DE_BINDRES_REBIND,
} deBindRes;

// These globals currently have to be set so we can report a proper stack trace.
static void setStackTraceGlobals(deExpression expression) {
  deCurrentStatement = deFindExpressionStatement(expression);
  deBinding binding = deStatementGetBinding(deCurrentStatement);
  deCurrentSignature = deBindingGetSignature(binding);
}

// Report an error at a given expression.
static void error(deExpression expression, char* format, ...) {
  char *buff;
  va_list ap;
  va_start(ap, format);
  buff = utVsprintf(format, ap);
  va_end(ap);
  setStackTraceGlobals(expression);
  deError(deStatementGetLine(deCurrentStatement), "%s", buff);
}

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
    error(expression, "Non-equal types passed to binary operator");
  }
  // Allow addition on strings and arrays.
  deDatatypeType type = deDatatypeGetType(leftType);
  deExpressionType exprType = deExpressionGetType(expression);
  if ((type != DE_TYPE_ARRAY || exprType != DE_EXPR_ADD) &&
      (type != DE_TYPE_STRING || (exprType != DE_EXPR_ADD && exprType != DE_EXPR_BITXOR)) &&
      !deDatatypeIsInteger(leftType) && type != DE_TYPE_FLOAT) {
    error(expression, "Invalid types for binary arithmetic operator");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a bitwise OR expression.  This is different from the other bitwise
// operators because it also used in type unions, such as "a: Uint | Int".
static void bindBitwiseOrExpression(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDatatype leftType, rightType;
  checkBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  if (deExpressionIsType(left)) {
    deLine line = deExpressionGetLine(expression);
    if (!deExpressionIsType(right)) {
      deError(line, "Non-equal types passed to binary operator");
    }
    deExpressionSetIsType(expression, true);
    deExpressionSetDatatype(expression, deNoneDatatypeCreate());
  } else {
    bindBinaryArithmeticExpression(scopeBlock, expression);
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
  deLine line = deExpressionGetLine(expression);
  if (!deDatatypeIsInteger(leftType)) {
    deError(line, "Base of exponentiation operator must be uint or modint");
  }
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deError(line, "Exponent must be a uint");
  }
  if (deDatatypeSecret(rightType)) {
    deError(line, "Exponent cannot be secret");
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
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (deDatatypeGetType(selectType) != DE_TYPE_BOOL) {
    deError(line, "Select must be Boolean");
  }
  if (leftType != rightType) {
    deError(line, "Select operator applied to different data types:%s",
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
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeGetType(lowerType) != DE_TYPE_UINT ||
      deDatatypeGetType(upperType) != DE_TYPE_UINT) {
    deError(line, "Index values must be unsigned integers");
  }
  if (deDatatypeSecret(lowerType) || deDatatypeSecret(upperType)) {
    deError(line, "Indexing with a secret is not allowed");
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_ARRAY && type != DE_TYPE_STRING) {
    deError(line, "Slicing a non-array/non-string type");
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
  if (type == DE_TYPE_CLASS || type == DE_TYPE_NULL) {
    deError(deExpressionGetLine(expression), "Object references cannot be marked secret");
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
  deLine line = deExpressionGetLine(expression);
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  if (deExpressionIsType(left) != deExpressionIsType(right)) {
    deError(line, "Ranges must be either types or integers, eg not u32 .. 64");
  }
  deDatatypeType leftType = deDatatypeGetType(leftDatatype);
  deDatatypeType rightType = deDatatypeGetType(rightDatatype);
  if (deExpressionIsType(left)) {
    if (leftType != DE_TYPE_UINT && leftType != DE_TYPE_INT) {
      deError(line, "Type ranges are only allowed for Int and Uint types, eg u1 ... u32");
    }
    if (leftType != rightType) {
      deError(line, "Type ranges must have the same sign, eg u1 ... u32 or i1 ... i32");
    }
    uint32 leftWidth = deDatatypeGetWidth(leftDatatype);
    uint32 rightWidth = deDatatypeGetWidth(rightDatatype);
    if (leftWidth > rightWidth) {
      deError(line, "Left type width must be <= right type width, eg i64 ... i256");
    }
    deTclass tclass = deFindDatatypeTclass(leftDatatype);
    deExpressionSetDatatype(expression, deTclassDatatypeCreate(tclass));
    deExpressionSetIsType(expression, true);
  } else {
    if (leftType != DE_TYPE_UINT && leftType != DE_TYPE_INT) {
      deError(line, "Integer ranges are only allowed for Int and Uint types, eg u1 ... u32");
    }
    if (leftDatatype != rightDatatype) {
      deError(line, "Type ranges limits must have the same type, eg 1 ... 10 or 1i32 ... 10i32:%s",
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
// case is where we call null(Foo), where we pass a Tclass to null.  This can
// be used to set a variable or class data member to null, but it does not
// define which class the variable is bound to.  That is resolved later if
// another assignment to the variable is made with a fully qualified class
// constructor.
static void bindNullExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  if (deDatatypeGetType(datatype) == DE_TYPE_TCLASS) {
    // If there are no template parameters, we can find the class.
    deClass theClass = deTclassGetDefaultClass(deDatatypeGetTclass(datatype));
    if (theClass != deClassNull) {
      datatype = deClassGetDatatype(theClass);
    }
  }
  if (deDatatypeGetType(datatype) == DE_TYPE_TCLASS) {
    datatype = deNullDatatypeCreate(deDatatypeGetTclass(datatype));
  }
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_CLASS:
    case DE_TYPE_NULL:
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
    case DE_TYPE_TCLASS:
      break;
    case DE_TYPE_FUNCTION: {
      deFunctionType type = deFunctionGetType(deDatatypeGetFunction(datatype));
      if (type != DE_FUNC_STRUCT && type != DE_FUNC_ENUM) {
        deError(deExpressionGetLine(expression), "Cannot create default initial value for type %s",
            deDatatypeGetTypeString(datatype));
      }
      break;
    }
    case DE_TYPE_MODINT:
    case DE_TYPE_NONE:
      deError(deExpressionGetLine(expression), "Cannot create default initial value for type %s",
          deDatatypeGetTypeString(datatype));
  }
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

// Bind a function pointer expression.  We have to mark all the parameter
// variables as not types during expression, even though we typically specify only
// the parameter types in the function address expression.  When we call through
// the function pointer, all parameters will be instantiated, which may lead to
// some unused parameters being instantiated.  Any function signature that has
// its address taken is called by passing all parameters, since that's how we
// call it through the function pointer.  In case we pass a type to the function
// when called directly, we push default values for the type parameters.
static void bindFunctionPointerExpression(deExpression expression) {
  deExpression functionCallExpression = deExpressionGetFirstExpression(expression);
  deDatatype returnType = deExpressionGetDatatype(functionCallExpression);
  deExpression functionExpression = deExpressionGetFirstExpression(functionCallExpression);
  deExpression parameters = deExpressionGetNextExpression(functionExpression);
  deDatatypeArray paramTypes = deDatatypeArrayAlloc();
  deExpression parameter;
  deForeachExpressionExpression(parameters, parameter) {
    deDatatype datatype = deExpressionGetDatatype(parameter);
    deDatatypeArrayAppendDatatype(paramTypes, datatype);
  } deEndExpressionExpression;
  deDatatype funcptrType = deFuncptrDatatypeCreate(returnType, paramTypes);
  deDatatype functionDatatype = deExpressionGetDatatype(functionExpression);
  utAssert(deDatatypeGetType(functionDatatype) == DE_TYPE_FUNCTION);
  deFunction function = deDatatypeGetFunction(functionDatatype);
  deSignature signature = deLookupSignature(function, paramTypes);
  deLine line = deExpressionGetLine(expression);
  if (signature == deSignatureNull) {
    signature = deSignatureCreate(function, paramTypes, line);
  }
  deSignatureSetIsCalledByFuncptr(signature, true);
  setAllSignatureVariablesToInstantiated(signature);
  deExpressionSetSignature(expression, signature);
  deExpressionSetDatatype(expression, funcptrType);
}

// Bind an arrayof expression.
static void bindArrayofExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  if (deDatatypeGetType(datatype) == DE_TYPE_TCLASS) {
    datatype = deNullDatatypeCreate(deDatatypeGetTclass(datatype));
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
    deError(deExpressionGetLine(expression), "Cannot  change sign of non-integer");
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
    deError(deExpressionGetLine(expression), "widthof applied to non-number");
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
static bool bindIdentExpression(deBlock scopeBlock, deExpression expression, bool inScopeBlock) {
  utSym sym = deExpressionGetName(expression);
  deIdent ident = deExpressionGetIdent(expression);
  if (ident == deIdentNull) {
    if (!inScopeBlock) {
      ident = deFindIdent(scopeBlock, sym);
    } else {
      ident = deBlockFindIdent(scopeBlock, sym);
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
      if (datatype == deDatatypeNull || deDatatypeGetType(datatype) == DE_TYPE_NULL) {
        deEvent event = deVariableEventCreate(variable);
        deEventAppendBinding(event, deExpressionGetBinding(expression));
        return false;
      }
      deExpressionSetDatatype(expression, datatype);
      deExpressionSetIsType(expression, deVariableIsType(variable));
      deVariableSetInstantiated(variable, deVariableInstantiated(variable) ||
          deExpressionInstantiating(expression));
      return true;
    }
    case DE_IDENT_FUNCTION:
      deExpressionSetDatatype(expression, deFunctionDatatypeCreate(deIdentGetFunction(ident)));
      return true;
    case DE_IDENT_UNDEFINED: {
      deEvent event = deUndefinedIdentEventCreate(ident);
      deStatement statement = deFindExpressionStatement(expression);
      deEventAppendBinding(event, deStatementGetBinding(statement));
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
  deLine line = deExpressionGetLine(expression);
  deDatatypeType type = deDatatypeGetType(leftType);
  if (deDatatypeTypeIsInteger(type) || type == DE_TYPE_FLOAT) {
    if (!typesAreEquivalent(leftType, rightType)) {
      deError(line, "Non-equal types passed to binary operator");
    }
    deExpressionSetDatatype(expression, leftType);
    return;
  }
  if (deDatatypeGetType(leftType) != DE_TYPE_STRING) {
    deError(line, "Invalid left operand type for %% operator");
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
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeGetType(leftType) != DE_TYPE_BOOL ||
      deDatatypeGetType(rightType) != DE_TYPE_BOOL) {
    deError(line, "Non-Boolean types passed to Boolean operator");
  }
  deExpressionSetDatatype(expression, leftType);
}

// Bind a shift/rotate expression.  The distance must be a uint.  The value
// being shifted (left operand) must be an integer.
static void bindShiftExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  deLine line = deExpressionGetLine(expression);
  if (!deDatatypeIsInteger(leftType)) {
    deError(line, "Only integers can be shifted/rotated");
  }
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deError(line, "Shift/rotate distance must be a uint");
  }
  if (deDatatypeSecret(rightType)) {
    deError(line, "Shift/rotate distance cannot be secret");
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
    error(expression, "Non-equal types passed to relational operator:%s",
        deGetOldVsNewDatatypeStrings(leftType, rightType));
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_UINT && type != DE_TYPE_INT && type != DE_TYPE_FLOAT &&
      type != DE_TYPE_STRING && type != DE_TYPE_ARRAY) {
    error(expression, "Invalid types passed to relational operator");
  }
  bool secret = deDatatypeSecret(leftType) || deDatatypeSecret(rightType);
  deExpressionSetDatatype(expression, deSetDatatypeSecret(deBoolDatatypeCreate(), secret));
}

// Bind an equality operator.  Both operands must be integers.
static void bindEqualityExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, true);
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (leftType != rightType) {
    deDatatype unifiedType = deUnifyDatatypes(leftType, rightType);
    if (unifiedType == deDatatypeNull) {
      deError(line, "Non-equal types passed to relational operator:%s",
          deGetOldVsNewDatatypeStrings(leftType, rightType));
    }
  }
  deExpressionSetDatatype(expression, deSetDatatypeSecret(deBoolDatatypeCreate(),
      deDatatypeSecret(leftType)));
}

// Bind a negate expression.  The operand must be an integer.
static void bindUnaryArithmeticExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype childType = bindUnaryExpression(scopeBlock, expression);
  deLine line = deExpressionGetLine(expression);
  if (!deDatatypeIsInteger(childType) && !deDatatypeIsFloat(childType)) {
    deError(line, "Only integers can be negated");
  }
  deExpressionSetDatatype(expression, childType);
  deExpression child = deExpressionGetFirstExpression(expression);
  deExpressionSetAutocast(expression, deExpressionAutocast(child));
}

// Bind a not expression.  It does logical not on Boolean operands, and
// complement on integer operands.
static void bindNotExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype childType = bindUnaryExpression(scopeBlock, expression);
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeGetType(childType) != DE_TYPE_BOOL) {
    deError(line, "Not operator only works on Boolean types");
  }
  deExpressionSetDatatype(expression, childType);
}

// Verify the cast expression is valid, and return the resulting datatype.
// Casts are allowed between numeric types, including floating point types.  We
// can also cast a string to a [u8] array and vise-versa.  Object references
// can be cast to their underlying integer type and back, e.g  <u32>Point(1,2),
// or <Point(u64, u64)>1u32.  Object-to-integer casts are dangerous and we
// should probably restrict its use to code generators.
static void verifyCast(deExpression expression, deDatatype leftDatatype,
    deDatatype rightDatatype, deLine line) {
  if (leftDatatype == rightDatatype) {
    return;  // The cast is a nop.
  }
  if (leftDatatype == deDatatypeNull) {
    error(expression, "Casts require qualified types");
  }
  if (deDatatypeGetType(leftDatatype) == DE_TYPE_CLASS &&
      deDatatypeGetType(rightDatatype) == DE_TYPE_NULL) {
    // This looks like a type expression hint.
    if (deClassGetTclass(deDatatypeGetClass(leftDatatype)) != deDatatypeGetTclass(rightDatatype)) {
      error(expression, "Casting to different class types is not allowed.");
    }
    return;
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
    error(expression, "Invalid cast: only casting from/to integers and from/to string are allowed");
  }
  if (leftType == DE_TYPE_STRING) {
    if (rightType != DE_TYPE_ARRAY ||
        deDatatypeGetType(deDatatypeGetElementType(rightDatatype)) !=
            DE_TYPE_UINT) {
      error(expression, "Invalid string conversion.  Only conversions from/to [u8] are allowed.");
    }
    return;
  }
  if (rightType == DE_TYPE_ARRAY) {
    deDatatype elementDatatype = deDatatypeGetElementType(rightDatatype);
    if (deDatatypeGetType(elementDatatype) != DE_TYPE_UINT) {
      error(expression, "Invalid cast: can only convert from/to uint arrays");
    }
    return;
  }
  if (!deDatatypeTypeIsInteger(rightType) && rightType != DE_TYPE_CLASS) {
    error(expression, "Invalid cast");
  }
  if (rightType  == DE_TYPE_CLASS) {
    // Verify the integer width matches the class reference width.
    deClass theClass = deDatatypeGetClass(rightDatatype);
    if (deDatatypeGetWidth(leftDatatype) != deClassGetRefWidth(theClass)) {
      error(expression, "Invalid cast: integer width does not match class reference width");
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
  deDatatype leftDatatype = deExpressionGetDatatype(left);
  deDatatype rightDatatype = deExpressionGetDatatype(right);
  deLine line = deExpressionGetLine(expression);
  // We ignore the secrecy of the left type: you can't cast away secrecy.  Just
  // force the left type to have the same secrecy value as the right.
  leftDatatype = deSetDatatypeSecret(leftDatatype, deDatatypeSecret(rightDatatype));
  if (deDatatypeGetType(leftDatatype) == DE_TYPE_ENUMCLASS) {
    // If the cast is to an ENUMCLASS, instead cast to an ENUM.
    deBlock enumBlock = deFunctionGetSubBlock(deDatatypeGetFunction(leftDatatype));
    leftDatatype = deFindEnumIntType(enumBlock);
  }
  verifyCast(expression, leftDatatype, rightDatatype, line);
  deExpressionSetDatatype(expression, leftDatatype);
}

// Verify that it is OK for code to call the function.
static void verifyFunctionIsCallable(deBlock scopeBlock, deFunction function) {
  deFunctionType type = deFunctionGetType(function);
  if ((type == DE_FUNC_MODULE || type == DE_FUNC_PACKAGE) &&
      deFunctionGetType(deBlockGetOwningFunction(scopeBlock)) != DE_FUNC_PACKAGE) {
    deError(deFunctionGetLine(function), "Cannot call function %s, which which has type %s\n",
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
  return type != DE_TYPE_TCLASS  && type != DE_TYPE_FUNCTION;
}

// Return the named parameter variable.
static deVariable findNamedParam(deBlock block, deExpression param) {
  utSym name = deExpressionGetName(deExpressionGetFirstExpression(param));
  deIdent ident = deBlockFindIdent(block, name);
  if (ident == deIdentNull || deIdentGetType(ident) != DE_IDENT_VARIABLE) {
    error(param, "Undefined named parameter: %s", utSymGetName(name));
  }
  deVariable var = deIdentGetVariable(ident);
  if (deVariableGetType(var) != DE_VAR_PARAMETER) {
    error(param, "Undefined named parameter: %s", utSymGetName(name));
  }
  return var;
}

// Find call datatypes.  For default parameters with default values that are
// not specified by the caller, use deNullDatatype, as this will be bound when
// expression the function.
static deDatatypeArray findCallDatatypes(deBlock scopeBlock,
    deExpression expression, deFunction function, deExpression params) {
  deDatatypeArray paramTypes = deDatatypeArrayAlloc();
  deBlock block = deFunctionGetSubBlock(function);
  uint32 numParams = deBlockCountParameterVariables(block);
  deDatatypeArrayResizeDatatypes(paramTypes, numParams);
  deDatatypeArraySetUsedDatatype(paramTypes, numParams);
  deVariable var = deBlockGetFirstVariable(block);
  deExpression access = deExpressionGetFirstExpression(expression);
  uint32 xParam = 0;
  if (deFunctionGetType(function) == DE_FUNC_CONSTRUCTOR) {
    deTclass tclass = deFunctionGetTclass(function);
    deDatatypeArraySetiDatatype(paramTypes, xParam, deTclassDatatypeCreate(tclass));
    xParam++;
    var = deVariableGetNextBlockVariable(var);
  } else if (isMethodCall(access)) {
    // Add the type of the object on the left of the dot expression as self parameter.
    deDatatype selfType = deExpressionGetDatatype(deExpressionGetFirstExpression(access));
    deDatatypeArraySetiDatatype(paramTypes, xParam, selfType);
    xParam++;
    var = deVariableGetNextBlockVariable(var);
  }
  deExpression param = deExpressionGetFirstExpression(params);
  bool foundNamedParam = false;
  while (param != deExpressionNull) {
    foundNamedParam |= deExpressionGetType(param) == DE_EXPR_NAMEDPARAM;
    if (!foundNamedParam) {
      if (var == deVariableNull || deVariableGetType(var) != DE_VAR_PARAMETER) {
        error(params, "Too many arguments passed to function %s", deFunctionGetName(function));
      }
      deDatatypeArraySetiDatatype(paramTypes, xParam, deExpressionGetDatatype(param));
      xParam++;
      var = deVariableGetNextBlockVariable(var);
    } else {
      var = findNamedParam(block, param);
      uint32 index = deBlockFindVariableIndex(block, var);
      if (deDatatypeArrayGetiDatatype(paramTypes, index) != deDatatypeNull) {
        error(param, "Named parameter assigned twice");
      }
      deDatatypeArraySetiDatatype(paramTypes, index, deExpressionGetDatatype(param));
    }
    param = deExpressionGetNextExpression(param);
  }
  var = deBlockGetFirstVariable(block);
  for (uint32 xParam = 0; xParam < deDatatypeArrayGetNumDatatype(paramTypes); xParam++) {
    if (deDatatypeArrayGetiDatatype(paramTypes, xParam) == deDatatypeNull &&
        deVariableGetInitializerExpression(var) == deExpressionNull) {
      error(params, "Parameter %s was not set and has no default value", deVariableGetName(var));
    }
    var = deVariableGetNextBlockVariable(var);
  }
  if (var != deVariableNull && deVariableGetType(var) == DE_VAR_PARAMETER) {
    error(params, "Too few arguments passed to function %s", deFunctionGetName(function));
  }
  return paramTypes;
}

// Find the function being called from the bound access expression.  There are
// three cases: a normal function call, a method call on a Tclass, and a call on
// a concrete type such as an array.
static deFunction findCalledFunction(deExpression access) {
  deDatatype accessDatatype = deExpressionGetDatatype(access);
  deDatatypeType accessType = deDatatypeGetType(accessDatatype);
  bool isTclass = accessType == DE_TYPE_TCLASS;
  if (!isTclass && accessType != DE_TYPE_FUNCTION) {
    deTclass tclass = deFindDatatypeTclass(accessDatatype);
    if (tclass == deTclassNull) {
      error(access, "Cannot call object of type %s\n", deDatatypeGetTypeString(accessDatatype));
    }
    accessDatatype = deTclassDatatypeCreate(tclass);
    isTclass = true;
  }
  return isTclass? deTclassGetFunction(deDatatypeGetTclass(accessDatatype)) :
      deDatatypeGetFunction(accessDatatype);
}

// Find an existing signature on the class that matches this one, and return it if it exists.
// Otherwise, resolve the signature parameter null(tclass) to the new class.
static deSignature resolveConstructorSignature(deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  deTclass tclass = deFunctionGetTclass(function);
  deClass theClass = deClassCreate(tclass, signature);
  if (!deClassBound(theClass)) {
    deCopyFunctionIdentsToBlock(deFunctionGetSubBlock(function), deClassGetSubBlock(theClass));
  }
  deDatatype selfType = deClassDatatypeCreate(theClass);
  deSignatureSetReturnType(signature, selfType);
  signature = deResolveConstructorSignature(signature);
  if (deSignatureGetClass(signature) == deClassNull) {
    deClassAppendSignature(theClass, signature);
  }
  deSignatureSetBound(signature, true);
  deQueueEventBlockedBindings(deSignatureGetReturnEvent(signature));
  deClassSetBound(theClass, true);
  return signature;
}

// Bind a call expression.
static bool bindCallExpression(deBlock scopeBlock, deExpression expression) {
  deExpression access = deExpressionGetFirstExpression(expression);
  deExpression params = deExpressionGetNextExpression(access);
  deFunction function = findCalledFunction(access);
  verifyFunctionIsCallable(scopeBlock, function);
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
    setStackTraceGlobals(expression);
    signature = deSignatureCreate(function, paramTypes, line);
    if (deSignatureIsConstructor(signature)) {
      // TODO: also resolve methods so factory functions can take null types.
      signature = resolveConstructorSignature(signature);
    }
    deQueueSignature(signature);
  } else {
    deDatatypeArrayFree(paramTypes);
  }
  deExpressionSetSignature(expression, signature);
  deSignatureSetInstantiated(signature,
      deSignatureInstantiated(signature) || deExpressionInstantiating(expression));
  if (!deSignatureBound(signature)) {
    deEvent event = deSignatureEventCreate(signature);
    deBinding binding = deExpressionGetBinding(expression);
    deEventAppendBinding(event, binding);
    return false;
  }
  deExpressionSetDatatype(expression, deSignatureGetReturnType(signature));
  return true;  // Success.
}

// Bind the index expression.
static void bindIndexExpression(deBlock scopeBlock, deExpression expression) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeBlock, expression, &leftType, &rightType, false);
  deLine line = deExpressionGetLine(expression);
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deError(line, "Index values must be uint");
  }
  if (deDatatypeSecret(rightType)) {
    deError(line, "Indexing with a secret is not allowed");
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_ARRAY && type != DE_TYPE_STRING && type != DE_TYPE_TUPLE &&
      type != DE_TYPE_STRUCT) {
    deError(line, "Index into non-array/non-string/non-tuple type");
  }
  if (type == DE_TYPE_TUPLE || type == DE_TYPE_STRUCT) {
    if (deExpressionGetType(right) != DE_EXPR_INTEGER) {
      deError(line,
          "Tuples can only be indexed by constant integers, like y = point[1]");
    }
    uint32 index = deBigintGetUint32(deExpressionGetBigint(right), line);
    if (index >= deDatatypeGetNumTypeList(leftType)) {
      deError(line, "Tuple index out of bounds");
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
    case DE_TYPE_NULL:
    case DE_TYPE_FUNCPTR:
      error(expression, "Cannot use '.' on  datatype %s", deDatatypeGetTypeString(datatype));
      break;
    case DE_TYPE_CLASS:
      return deClassGetSubBlock(deDatatypeGetClass(datatype));
    case DE_TYPE_TCLASS:
      return deFunctionGetSubBlock(deTclassGetFunction(deDatatypeGetTclass(datatype)));
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
  if (deExpressionGetType(access) == DE_EXPR_IDENT) {
    identExpr = access;
  } else {
    utAssert(deExpressionGetType(access) == DE_EXPR_DOT);
    deExpression dotAccess = deExpressionGetFirstExpression(access);
    identExpr = deExpressionGetNextExpression(dotAccess);
    scopeBlock = findExpressionSubScope(dotAccess);
  }
  utAssert(deExpressionGetType(identExpr) == DE_EXPR_IDENT);
  deIdent ident = deExpressionGetIdent(identExpr);
  if (ident != deIdentNull) {
    deIdentRemoveExpression(ident, identExpr);
  }
  utSym sym = deExpressionGetName(identExpr);
  ident = deFindIdent(scopeBlock, sym);
  if (ident == deIdentNull || deIdentGetType(ident) == DE_IDENT_UNDEFINED) {
    bool generated = deStatementGenerated(deFindExpressionStatement(access));
    deLine line = deExpressionGetLine(identExpr);
    deVariable var = deVariableCreate(scopeBlock, DE_VAR_LOCAL, false, sym,
        deExpressionNull, generated, line);
    deVariableSetInstantiated(var, true);
    ident = deVariableGetIdent(var);
  }
  if (deIdentGetType(ident) == DE_IDENT_FUNCTION) {
    error(access, "%s is a function, and cannot be assigned.", utSymGetName(sym));
  }
  deIdentAppendExpression(ident, identExpr);
  return deIdentGetVariable(ident);
}

// Update a variable from an assignment expression.
static void updateVariable(deBlock scopeBlock, deVariable variable, deExpression targetExpression) {
  deDatatype newDatatype = deExpressionGetDatatype(targetExpression);
  utAssert(newDatatype != deDatatypeNull);
  deDatatype oldDatatype = deVariableGetDatatype(variable);
  deDatatype datatype = newDatatype;
  if (oldDatatype != deDatatypeNull) {
    datatype = deUnifyDatatypes(oldDatatype, newDatatype);
  }
  if (datatype == deDatatypeNull) {
    error(targetExpression, "Assigning different type to %s than assigned before:%s",
      deVariableGetName(variable), deGetOldVsNewDatatypeStrings(oldDatatype, newDatatype));
  }
  deVariableSetDatatype(variable, datatype);
  if ((oldDatatype == deDatatypeNull || deDatatypeGetType(oldDatatype) == DE_TYPE_NULL) &&
     deDatatypeGetType(datatype) != DE_TYPE_NULL) {
    // TODO: Block on sub-elements being null, not just variables.
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

// Bind an assignment expression.
static deBindRes bindAssignmentExpression(deBlock scopeBlock, deExpression expression) {
  deExpression access = deExpressionGetFirstExpression(expression);
  deExpression target = deExpressionGetNextExpression(access);
  deExpressionType type  = deExpressionGetType(access);
  deDatatype targetDatatype = deExpressionGetDatatype(target);
  if (deDatatypeGetType(targetDatatype) == DE_TYPE_NONE) {
    error(expression, "Right side of assignment does not return a value.");
  }
  deStatement statement = deExpressionGetStatement(expression);
  if (statement != deStatementNull && deStatementGetType(statement) == DE_STATEMENT_FOREACH) {
    if (addValuesIteratorIfNeeded(scopeBlock, statement)) {
      return DE_BINDRES_REBIND;
    }
  }
  if (type == DE_EXPR_IDENT || type == DE_EXPR_DOT) {
    deVariable variable = findOrCreateVariable(scopeBlock, access);
    updateVariable(scopeBlock, variable, target);
    deExpressionSetDatatype(access, targetDatatype);
  }
  deExpressionSetDatatype(expression, targetDatatype);
  return DE_BINDRES_OK;
}

// Bind the array expression.
static void bindArrayExpression(deBlock scopeBlock, deExpression expression) { 
  deLine line = deExpressionGetLine(expression);
  deExpression firstElement = deExpressionGetFirstExpression(expression);
  deDatatype datatype = deExpressionGetDatatype(firstElement);
  if (deExpressionIsType(firstElement)) {
    deExpressionSetIsType(expression, true);
  }
  deExpression nextElement = deExpressionGetNextExpression(firstElement);
  while (nextElement != deExpressionNull) {
    if (deExpressionGetDatatype(nextElement) != datatype) {
      deError(line, "Array elements must have the same type:%s",
          deGetOldVsNewDatatypeStrings(deExpressionGetDatatype(nextElement), datatype));
    }
    if (deExpressionIsType(nextElement)) {
      deError(line, "Array type expressions can contain only one type, like [u32]");
    }
    nextElement = deExpressionGetNextExpression(nextElement);
  }
  deDatatype arrayDatatype = deArrayDatatypeCreate(datatype);
  deExpressionSetDatatype(expression, arrayDatatype);
}

// A class data member was defined with a null datatype, such as self.point =
// null(Point).  This does not tell us what the class of self.point is, if
// Point is a template class.  We have to wait for an assignment to be bound
// that clarifies the type.  Statement expression can block on null type
// resolution of a class data member.
//
// Report an error when null types appear anywhere but in an assignment to a
// data member or variable with a non-composite type.  We still can't bind
// constructs like rectangle = {null(Point), null(Point)}, because null type
// resolution only happens on variables.  Expression this would require blocking
// on access expressions like rectangle[0], which would require reexpression the
// access expressions when the variable type is refined.
static void blockOnNullResolution(deBlock scopeBlock, deExpression access) {
  deExpressionType type = deExpressionGetType(access);
  deLine line = deExpressionGetLine(access);
  deBlock varScopeBlock;
  utSym sym;
  if (type == DE_EXPR_IDENT) {
    // This is a local in the current scope.
    varScopeBlock = scopeBlock;
    sym = deExpressionGetName(access);
  } else if (type == DE_EXPR_DOT) {
    deExpression left = deExpressionGetFirstExpression(access);
    deDatatype classType = deExpressionGetDatatype(left);
    if (deDatatypeGetType(classType) != DE_TYPE_CLASS) {
      deError(line, "Null type found on non-variable or data member.");
    }
    deExpression right = deExpressionGetNextExpression(left);
    utAssert(deExpressionGetType(right) == DE_EXPR_IDENT);
    deClass theClass = deDatatypeGetClass(classType);
    varScopeBlock = deClassGetSubBlock(theClass);
    sym = deExpressionGetName(right);
  } else {
    deError(line, "Null type expressions can only be assigned to variables and class members.");
    return;
  }
  deIdent ident = deBlockFindIdent(varScopeBlock, sym);
  utAssert(ident != deIdentNull && deIdentGetType(ident) == DE_IDENT_VARIABLE);
  deVariable variable = deIdentGetVariable(ident);
  deEvent event = deVariableEventCreate(variable);
  deEventAppendBinding(event, deExpressionGetBinding(access));
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
    classBlock = deClassGetSubBlock(deDatatypeGetClass(datatype));
  } else if (type == DE_TYPE_NULL) {
    blockOnNullResolution(scopeBlock, expression);
    return false;
  } else if (type == DE_TYPE_TCLASS) {
    classBlock = deFunctionGetSubBlock(deTclassGetFunction(deDatatypeGetTclass(datatype)));
  } else if (type == DE_TYPE_FUNCTION || type == DE_TYPE_STRUCT || type == DE_TYPE_ENUMCLASS) {
    deFunction function = deDatatypeGetFunction(datatype);
    deFunctionType funcType = deFunctionGetType(function);
    if (funcType != DE_FUNC_PACKAGE && funcType != DE_FUNC_MODULE &&
        funcType != DE_FUNC_STRUCT && funcType != DE_FUNC_ENUM) {
      deLine line = deExpressionGetLine(expression);
      deError(line, "Cannot access identifiers inside function %s", deFunctionGetName(function));
    }
    classBlock = deFunctionGetSubBlock(function);
  } else {
    // Some builtin types have method calls.
    deTclass tclass = deFindDatatypeTclass(datatype);
    classBlock = deFunctionGetSubBlock(deTclassGetFunction(tclass));
  }
  utAssert(deExpressionGetType(identExpr) == DE_EXPR_IDENT);
  // Make the right-hand expression, if we haven't already.
  deExpression identExpression = deExpressionGetNextExpression(accessExpr);
  utAssert(identExpression != deExpressionNull);
  if (classBlock != deBlockNull) {
    if (!bindIdentExpression(classBlock, identExpression, true)) {
      return false;
    }
  } else {
    if (!bindIdentExpression(scopeBlock, identExpression, false)) {
      return false;
    }
  }
  deExpressionSetDatatype(expression, deExpressionGetDatatype(identExpression));
  deExpressionSetConst(expression, deExpressionConst(identExpression));
  return true;
}

// Bind an isnull expression.  The expression type is bool.
static void bindIsnullExpression(deBlock scopeBlock, deExpression expression) {
  deDatatype datatype = bindUnaryExpression(scopeBlock, expression);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type != DE_TYPE_CLASS && type != DE_TYPE_NULL) {
    deError(deExpressionGetLine(expression), "isnull applied to non-object");
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
    error(expression, "No parameter named %s found", utSymGetName(paramName));
  }
  deVariable var = deIdentGetVariable(ident);
  if (deVariableGetType(var) != DE_VAR_PARAMETER) {
    error(expression, "Variable %s is a local variable, not a parameter", utSymGetName(paramName));
  }
  deIdentAppendExpression(ident, paramNameExpression);
}

// Bind the expression's expression.
static deBindRes bindExpression(deBlock scopeBlock, deExpression expression) {
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
      if (!bindIdentExpression(scopeBlock, expression, false)) {
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
      // TODO: Write this.
      // bindModintExpression(scopeBlock, expression);
      utExit("Write me");
      break;
    case DE_EXPR_BITOR :
    case DE_EXPR_BITOR_EQUALS:
      bindBitwiseOrExpression(scopeBlock, expression);
      break;
    case DE_EXPR_ADD:
    case DE_EXPR_ADD_EQUALS:
    case DE_EXPR_SUB:
    case DE_EXPR_SUB_EQUALS:
    case DE_EXPR_MUL:
    case DE_EXPR_MUL_EQUALS:
    case DE_EXPR_DIV:
    case DE_EXPR_DIV_EQUALS:
      bindBinaryArithmeticExpression(scopeBlock, expression);
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
        deError(deExpressionGetLine(expression),
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
      utExit("Write me");
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
    case DE_EXPR_AS:
      utExit("Unexpected expression type");
      break;
    case DE_EXPR_IN:
      bindInExpression(scopeBlock, expression);
      break;
    case DE_EXPR_NAMEDPARAM:
      bindNamedParameter(scopeBlock, expression);
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
  if ((oldDatatype == deDatatypeNull || deDatatypeGetType(oldDatatype) == DE_TYPE_NULL) &&
      deDatatypeGetType(newDatatype) != DE_TYPE_NULL) {
    deSignatureSetBound(signature, true);
    deQueueEventBlockedBindings(deSignatureGetReturnEvent(signature));
  }
}

// Depending on the statement type, we may have some tasks to do once the statement is bound.
static void postProcessBoundStatement(deBinding binding) {
  deStatement statement = deBindingGetStatement(binding);
  deStatementSetInstantiated(statement, deBindingInstantiated(binding));
  deStatementType type = deStatementGetType(statement);
  if (type == DE_STATEMENT_RETURN || type == DE_STATEMENT_YIELD) {
    deDatatype datatype = deNoneDatatypeCreate();
    if (deStatementGetExpression(statement) != deExpressionNull) {
      datatype = deExpressionGetDatatype(deStatementGetExpression(statement));
    }
    updateSignatureReturnType(deBindingGetSignature(binding), datatype);
  }
}

// Set the datatype of variable to that if its default value.
static void setDefaultVariableType(deBlock scopeBlock, deBinding binding) {
  deVariable var = deBindingGetInitializerVariable(binding);
  updateVariable(scopeBlock, var, deVariableGetInitializerExpression(var));
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
  deQueueExpression(binding, expression, instantiating);
}

// Bind or continue expression the statement.
void deBindStatement2(deBinding binding) {
  deExpression expression = deBindingGetFirstExpression(binding);
  deBlock scopeBlock = deSignatureGetUniquifiedBlock(deBindingGetSignature(binding));
  while (expression != deExpressionNull) {
    deBindRes result = bindExpression(scopeBlock, expression);
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
      postProcessBoundStatement(binding);
      break;
    case DE_BIND_DEFAULT_VALUE:
      setDefaultVariableType(scopeBlock, binding);
      break;
    case DE_BIND_VAR_CONSTRAINT:
    case DE_BIND_FUNC_CONSTRAINT:
      // TODO: Check type constraints here.
      break;

  }
}
