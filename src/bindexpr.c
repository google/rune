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

// Code for binding statements and expressions.

#include "de.h"

// TODO: Re-implement overloaded operators.

// These globals currently have to be set so we can report a proper stack trace.
static void setStackTraceGlobals(deBinding binding) {
  deStateBinding statebinding = deFindBindingStateBinding(binding);
  deCurrentStatement = deStateBindingGetStatement(statebinding);
  deCurrentSignature = deStateBindingGetBindingSignature(statebinding);
}

// Report an error at a given binding.
static void error(deBinding binding, char* format, ...) {
  char *buff;
  va_list ap;
  va_start(ap, format);
  buff = utVsprintf(format, ap);
  va_end(ap);
  setStackTraceGlobals(binding);
  deError(deStatementGetLine(deCurrentStatement), "%s", buff);
}

// Set the float expression's datatype.
static void bindFloatExpression(deBinding binding){
  deExpression expression = deBindingGetExpression(binding);
  deFloat floatVal = deExpressionGetFloat(expression);
  uint32 width = deFloatGetWidth(floatVal);
  deDatatype datatype = deFloatDatatypeCreate(width);
  deBindingSetDatatype(binding, datatype);
}

// Set the random uint expression's datatype, which is just an unsigned integer.
static void bindRandUintExpression(deBinding binding) {
  deExpression expression = deBindingGetExpression(binding);
  uint32 width = deExpressionGetWidth(expression);
  deDatatype datatype = deUintDatatypeCreate(width);
  datatype = deSetDatatypeSecret(datatype, true);
  deBindingSetDatatype(binding, datatype);
}

// Modify the datatype in the constant integer binding tree to match the
// datatype.
static void autocastExpression(deBinding binding, deDatatype datatype) {
  deDatatype oldDatatype = deBindingGetDatatype(binding);
  if (!deDatatypeIsInteger(oldDatatype) || !deDatatypeIsInteger(datatype)) {
    return;  // We only auto-cast integers without type specifiers to integers.
  }
  deBindingSetDatatype(binding, datatype);
  deBinding child;
  deForeachBindingBinding(binding, child) {
    autocastExpression(child, datatype);
  } deEndBindingBinding;
}

// Return true if the types are the same, other than for their secret bit.
static bool typesAreEquivalent(deDatatype type1, deDatatype type2) {
  return deSetDatatypeSecret(type1, false) == deSetDatatypeSecret(type2, false);
}

// Bind a binary expression, returning the datatypes of the left and right
// sub-expressions.
static void checkBinaryExpression(deSignature scopeSig, deBinding binding,
    deDatatype* leftType, deDatatype* rightType, bool compareTypes) {
  deBinding left = deBindingGetFirstBinding(binding);
  deBinding right = deBindingGetNextBinding(left);
  *leftType = deBindingGetDatatype(left);
  *rightType = deBindingGetDatatype(right);
  if (compareTypes && !typesAreEquivalent(*leftType, *rightType)) {
    // Try auto-cast.
    if (deBindingAutocast(left) && !deBindingAutocast(right)) {
      // TODO: Comment in when we test autocast.
      // coautocastExpression(left, *rightType);
      *leftType = deBindingGetDatatype(left);
    } else if (deBindingAutocast(right) && !deBindingAutocast(left)) {
      // autocastExpression(right, *leftType);
      *rightType = deBindingGetDatatype(right);
    }
  }
  if (compareTypes && deBindingAutocast(left) && deBindingAutocast(right)) {
    deBindingSetAutocast(binding, true);
  }
}

// Bind a binary arithmetic expression.  The left and right types should have
// the same numeric type, resulting in the same type.
static void bindBinaryArithmeticExpression(deSignature scopeSig, deBinding binding) {
  deDatatype leftType, rightType;
  checkBinaryExpression(scopeSig, binding, &leftType, &rightType, true);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (leftType != rightType) {
    error(binding, "Non-equal types passed to binary operator");
  }
  // Allow addition on strings and arrays.
  deDatatypeType type = deDatatypeGetType(leftType);
  deExpression expression = deBindingGetExpression(binding);
  deExpressionType exprType = deExpressionGetType(expression);
  if ((type != DE_TYPE_ARRAY || exprType != DE_EXPR_ADD) &&
      (type != DE_TYPE_STRING || (exprType != DE_EXPR_ADD && exprType != DE_EXPR_BITXOR)) &&
      !deDatatypeIsInteger(leftType) && type != DE_TYPE_FLOAT) {
    error(binding, "Invalid types for binary arithmetic operator");
  }
  deBindingSetDatatype(binding, leftType);
}

// Bind a bitwise OR expression.  This is different from the other bitwise
// operators because it also used in type unions, such as "a: Uint | Int".
static void bindBitwiseOrExpression(deSignature scopeSig, deBinding binding) {
  deBinding left = deBindingGetFirstBinding(binding);
  deBinding right = deBindingGetNextBinding(left);
  deDatatype leftType, rightType;
  checkBinaryExpression(scopeSig, binding, &leftType, &rightType, false);
  if (deBindingIsType(left)) {
    deLine line = deBindingGetLine(binding);
    if (!deBindingIsType(right)) {
      deError(line, "Non-equal types passed to binary operator");
    }
    deBindingSetIsType(binding, true);
    deBindingSetDatatype(binding, deNoneDatatypeCreate());
  } else {
    bindBinaryArithmeticExpression(scopeSig, binding);
  }
}

// Bind a binary expression, returning the datatypes of the left and right
// sub-expressions.
static void bindBinaryExpression(deSignature scopeSig, deBinding binding,
    deDatatype* leftType, deDatatype* rightType, bool compareTypes) {
  deBinding left = deBindingGetFirstBinding(binding);
  deBinding right = deBindingGetNextBinding(left);
  *leftType = deBindingGetDatatype(left);
  *rightType = deBindingGetDatatype(right);
  if (compareTypes && !typesAreEquivalent(*leftType, *rightType)) {
    // Try auto-cast.
    if (deBindingAutocast(left) && !deBindingAutocast(right)) {
      // TODO: Comment in when we test autocast.
      // autocastExpression(left, *rightType);
      *leftType = deBindingGetDatatype(left);
    } else if (deBindingAutocast(right) && !deBindingAutocast(left)) {
      // autocastExpression(right, *leftType);
      *rightType = deBindingGetDatatype(right);
    }
  }
  if (compareTypes && deBindingAutocast(left) && deBindingAutocast(right)) {
    deBindingSetAutocast(binding, true);
  }
}

// Bind an exponentiation expression.  Exponent must be a non-secret uint, while
// the base can be a uint or modint.
static void bindExponentiationExpression(deSignature scopeSig, deBinding binding) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeSig, binding, &leftType, &rightType, false);
  deLine line = deBindingGetLine(binding);
  if (!deDatatypeIsInteger(leftType)) {
    deError(line, "Base of exponentiation operator must be uint or modint");
  }
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deError(line, "Exponent must be a uint");
  }
  if (deDatatypeSecret(rightType)) {
    deError(line, "Exponent cannot be secret");
  }
  deBindingSetDatatype(binding, leftType);
}

// Bind a select expression.  The selector must be Boolean, and the two data
// values must have the same type.
static void bindSelectExpression(deSignature scopeSig, deBinding binding) {
  deBinding select = deBindingGetFirstBinding(binding);
  deBinding left = deBindingGetNextBinding(select);
  deBinding right = deBindingGetNextBinding(left);
  deDatatype selectType = deBindingGetDatatype(select);
  deDatatype leftType = deBindingGetDatatype(left);
  deDatatype rightType = deBindingGetDatatype(right);
  deLine line = deBindingGetLine(binding);
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
  deBindingSetDatatype(binding, leftType);
}

// Bind the slice expression.
static void bindSliceExpression(deSignature scopeSig, deBinding binding) {
  deBinding left = deBindingGetFirstBinding(binding);
  deBinding lower = deBindingGetNextBinding(left);
  deBinding upper = deBindingGetNextBinding(lower);
  deDatatype leftType, lowerType, upperType;
  leftType = deBindingGetDatatype(left);
  lowerType = deBindingGetDatatype(lower);
  upperType = deBindingGetDatatype(upper);
  deLine line = deBindingGetLine(binding);
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
  deBindingSetDatatype(binding, leftType);
}

// Bind a unary expression, returning the datatype of the child.
static deDatatype bindUnaryExpression(deSignature scopeSig, deBinding expression) {
  deBinding child = deBindingGetFirstBinding(expression);
  return deBindingGetDatatype(child);
}

// Bind the markSecret or markPublic expression.
static void bindMarkSecretOrPublic(deSignature scopeSig, deBinding binding) {
  deDatatype datatype = bindUnaryExpression(scopeSig, binding);
  deDatatypeType type = deDatatypeGetType(datatype);
  // TODO: This check is wrong!  Fix it.  The base types of secrets can only be
  // integers, and strings since their base type is u8.  Structs, tuples, and
  // array datatypes that contain only integer and string base types are OK.
  if (type == DE_TYPE_CLASS || type == DE_TYPE_NULL) {
    deError(deBindingGetLine(binding), "Object references cannot be marked secret");
  }
  bool secret = deBindingGetType(binding) == DE_EXPR_SECRET;
  datatype = deSetDatatypeSecret(datatype, secret);
  deBindingSetDatatype(binding, datatype);
  deBindingSetIsType(binding, deBindingIsType(deBindingGetFirstBinding(binding)));
  deBindingSetConst(binding, deBindingConst(deBindingGetFirstBinding(binding)));
}

// Bind a ... expression, eg case u1 ... u32.
static void bindDotDotDotExpression(deSignature scopeSig, deBinding binding) {
  deDatatype leftDatatype, rightDatatype;
  bindBinaryExpression(scopeSig, binding, &leftDatatype, &rightDatatype, true);
  deLine line = deBindingGetLine(binding);
  deBinding left = deBindingGetFirstBinding(binding);
  deBinding right = deBindingGetNextBinding(left);
  if (deBindingIsType(left) != deBindingIsType(right)) {
    deError(line, "Ranges must be either types or integers, eg not u32 .. 64");
  }
  deDatatypeType leftType = deDatatypeGetType(leftDatatype);
  deDatatypeType rightType = deDatatypeGetType(rightDatatype);
  if (deBindingIsType(left)) {
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
    deBindingSetDatatype(binding, deTclassDatatypeCreate(tclass));
    deBindingSetIsType(binding, true);
  } else {
    if (leftType != DE_TYPE_UINT && leftType != DE_TYPE_INT) {
      deError(line, "Integer ranges are only allowed for Int and Uint types, eg u1 ... u32");
    }
    if (leftDatatype != rightDatatype) {
      deError(line, "Type ranges limits must have the same type, eg 1 ... 10 or 1i32 ... 10i32:%s",
          deGetOldVsNewDatatypeStrings(leftDatatype, rightDatatype));
    }
    deBindingSetDatatype(binding, leftDatatype);
  }
}

// Create an array of datatypes for the expression's children.
static deDatatypeArray listDatatypes(deBinding list) {
  deDatatypeArray types = deDatatypeArrayAlloc();
  deBinding child;
  deForeachBindingBinding(list, child) {
    deDatatypeArrayAppendDatatype(types, deBindingGetDatatype(child));
  } deEndBindingBinding;
  return types;
}

// Bind the tuple expression.
static void bindTupleExpression(deSignature scopeSig, deBinding binding) {
  deDatatypeArray types = listDatatypes(binding);
  deDatatype tupleType = deTupleDatatypeCreate(types);
  deBindingSetDatatype(binding, tupleType);
  deBinding child;
  deForeachBindingBinding(binding, child) {
    if (deBindingIsType(child)) {
      deBindingSetIsType(binding, true);
    }
  } deEndBindingBinding;
}

// Bind a null expression.  We can say null(f32), which returns 0.0f32, or
// null(string), which returns "".  Calling null on a call to a constructor
// yields null for that class, such as foo = null(Foo(123)).  The difficult
// case is where we call null(Foo), where we pass a Tclass to null.  This can
// be used to set a variable or class data member to null, but it does not
// define which class the variable is bound to.  That is resolved later if
// another assignment to the variable is made with a fully qualified class
// constructor.
static void bindNullExpression(deSignature scopeSig, deBinding binding) {
  deDatatype datatype = bindUnaryExpression(scopeSig, binding);
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
        deError(deBindingGetLine(binding), "Cannot create default initial value for type %s",
            deDatatypeGetTypeString(datatype));
      }
      break;
    }
    case DE_TYPE_MODINT:
    case DE_TYPE_NONE:
      deError(deBindingGetLine(binding), "Cannot create default initial value for type %s",
          deDatatypeGetTypeString(datatype));
  }
  deBindingSetDatatype(binding, datatype);
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
// variables as not types during binding, even though we typically specify only
// the parameter types in the function address expression.  When we call through
// the function pointer, all parameters will be instantiated, which may lead to
// some unused parameters being instantiated.  Any function signature that has
// its address taken is called by passing all parameters, since that's how we
// call it through the function pointer.  In case we pass a type to the function
// when called directly, we push default values for the type parameters.
static void bindFunctionPointerExpression(deBinding binding) {
  deBinding functionCallBinding = deBindingGetFirstBinding(binding);
  deDatatype returnType = deBindingGetDatatype(functionCallBinding);
  deBinding functionBinding = deBindingGetFirstBinding(functionCallBinding);
  deBinding parameters = deBindingGetNextBinding(functionBinding);
  deDatatypeArray paramTypes = deDatatypeArrayAlloc();
  deBinding parameter;
  deForeachBindingBinding(parameters, parameter) {
    deDatatype datatype = deBindingGetDatatype(parameter);
    deDatatypeArrayAppendDatatype(paramTypes, datatype);
  } deEndBindingBinding;
  deDatatype funcptrType = deFuncptrDatatypeCreate(returnType, paramTypes);
  deDatatype functionDatatype = deBindingGetDatatype(functionBinding);
  utAssert(deDatatypeGetType(functionDatatype) == DE_TYPE_FUNCTION);
  deFunction function = deDatatypeGetFunction(functionDatatype);
  deSignature signature = deLookupSignature(function, paramTypes);
  deLine line = deBindingGetLine(binding);
  if (signature == deSignatureNull) {
    signature = deSignatureCreate(function, paramTypes, line);
  }
  deSignatureSetIsCalledByFuncptr(signature, true);
  setAllSignatureVariablesToInstantiated(signature);
  deBindingSetSignature(binding, signature);
  deBindingSetDatatype(binding, funcptrType);
}

// Bind an arrayof expression.
static void bindArrayofExpression(deSignature scopeSig, deBinding binding) {
  deDatatype datatype = bindUnaryExpression(scopeSig, binding);
  if (deDatatypeGetType(datatype) == DE_TYPE_TCLASS) {
    datatype = deNullDatatypeCreate(deDatatypeGetTclass(datatype));
  }
  deBindingSetDatatype(binding, deArrayDatatypeCreate(datatype));
}

// Bind a typeof expression.
static void bindTypeofExpression(deSignature scopeSig, deBinding binding) {
  deDatatype datatype = bindUnaryExpression(scopeSig, binding);
  deBindingSetDatatype(binding, datatype);
  deBindingSetIsType(binding, true);
}

// Bind a signed() or unsigned() type conversion expression.
static void bindSignConversionExpression(deSignature scopeSig, deBinding binding) {
  deBinding child = deBindingGetFirstBinding(binding);
  deDatatype datatype = deBindingGetDatatype(child);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type != DE_TYPE_UINT && type != DE_TYPE_INT) {
    deError(deBindingGetLine(binding), "Cannot  change sign of non-integer");
  }
  datatype = deDatatypeSetSigned(datatype, deBindingGetType(binding)== DE_EXPR_SIGNED);
  deBindingSetDatatype(binding, datatype);
}


// Bind a widthof expression.  The expression type is u32.
static void bindWidthofExpression(deSignature scopeSig, deBinding binding) {
  bool savedInstantiating = deInstantiating;
  deInstantiating = false;
  deDatatype datatype = bindUnaryExpression(scopeSig, binding);
  if (!deDatatypeIsNumber(datatype)) {
    deError(deBindingGetLine(binding), "widthof applied to non-number");
  }
  deBindingSetDatatype(binding, deUintDatatypeCreate(32));
  deInstantiating = savedInstantiating;
}

// Bind an "in" expression.  These are all overloads.
// TODO: This code should check that the left-hand datatype can actually be in
// the right-hand datatype.
static void bindInExpression(deSignature scopeSig, deBinding binding) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeSig, binding, &leftType, &rightType, false);
  deBindingSetDatatype(binding, deBoolDatatypeCreate());
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
static void bindIntegerExpression(deBinding binding) {
  deExpression expression = deBindingGetExpression(binding);
  deBigint bigint = deExpressionGetBigint(expression);
  uint32 width = deBigintGetWidth(bigint);
  deDatatype datatype;
  if (deBigintSigned(bigint)) {
    datatype = deIntDatatypeCreate(width);
  } else {
    datatype = deUintDatatypeCreate(width);
  }
  deBindingSetDatatype(binding, datatype);
  deBindingSetAutocast(binding, deBigintWidthUnspecified(bigint));
}

// Find a global variable's binding, or create one if it does not exist.
static deBinding findVariableBinding(deSignature scopeSig, deVariable variable) {
  deBinding varBinding;
  if (deVariableGetBlock(variable) != deSignatureGetBlock(scopeSig)) {
    varBinding = deVariableGetFirstBinding(variable);
    if (varBinding == deBindingNull) {
      varBinding = deVariableBindingCreate(deSignatureNull, variable);
      deBindingSetDatatype(varBinding, deVariableGetDatatype(variable));
      deBindingSetIsType(varBinding, deVariableIsType(variable));
    }
  } else {
    varBinding = deFindVariableBinding(scopeSig, variable);
    if (varBinding == deBindingNull) {
      varBinding = deVariableBindingCreate(scopeSig, variable);
    }
  }
  return varBinding;
}

// Bind the identifier expression to a type.  If the identifier does not exist,
// create an unbound identifier.  If unbound or if the identifier has not been
// bound to a datatype, add statebinding to the identifier's event and return
// false.  If we succeed in binding the identifier, queue statebindings blocked on
// this event.  scopeBlock, if present, is from a dot operation, and we must
// look only for identifiers in that block.
static bool bindIdentExpression(deSignature scopeSig, deBlock scopeBlock,
    deStateBinding statebinding, deBinding binding) {
  deExpression expression = deBindingGetExpression(binding);
  utSym sym = deExpressionGetName(expression);
  deIdent ident = deBindingGetIdent(binding);
  if (ident == deIdentNull) {
    if (scopeBlock == deBlockNull) {
      ident = deFindIdent(deSignatureGetBlock(scopeSig), sym);
      if (ident == deIdentNull) {
        // Create an undefined identifier.
        ident = deUndefinedIdentCreate(deSignatureGetBlock(scopeSig), sym);
      }
    } else {
      ident = deBlockFindIdent(scopeBlock, sym);
      if (ident == deIdentNull) {
        // Create an undefined identifier.
        ident = deUndefinedIdentCreate(scopeBlock, sym);
      }
    }
    deIdentAppendBinding(ident, binding);
  }
  switch (deIdentGetType(ident)) {
    case DE_IDENT_VARIABLE: {
      deVariable variable = deIdentGetVariable(ident);
      deBinding varBinding = findVariableBinding(scopeSig, variable);
      deDatatype datatype = deBindingGetDatatype(varBinding);
      if (datatype == deDatatypeNull || deDatatypeGetType(datatype) == DE_TYPE_NULL) {
        deEvent event = deVariableEventCreate(varBinding);
        deEventAppendStateBinding(event, statebinding);
        return false;
      }
      deBindingSetDatatype(binding, datatype);
      deBindingSetIsType(binding, deBindingIsType(varBinding));
      deBindingSetInstantiating(varBinding, deBindingInstantiating(varBinding) ||
          deBindingInstantiating(binding));
      return true;
    }
    case DE_IDENT_FUNCTION:
      deBindingSetDatatype(binding, deFunctionDatatypeCreate(deIdentGetFunction(ident)));
      return true;
    case DE_IDENT_UNDEFINED: {
      deEvent event = deUndefinedIdentEventCreate(ident);
      deEventAppendStateBinding(event, statebinding);
      return false;
    }
  }
  return false;  // Dummy return.
}

// Bind and AND, OR, or XOR operator.  If operating on numbers, bitwise
// operators are used.  If operating on Boolean values, logical operators are
// used.
static void bindBinaryBool(deSignature scopeSig, deBinding binding) {
  deDatatype leftType, rightType;
  checkBinaryExpression(scopeSig, binding, &leftType, &rightType, true);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  deLine line = deBindingGetLine(binding);
  if (deDatatypeGetType(leftType) != DE_TYPE_BOOL ||
      deDatatypeGetType(rightType) != DE_TYPE_BOOL) {
    deError(line, "Non-Boolean types passed to Boolean operator");
  }
  deBindingSetDatatype(binding, leftType);
}

// Bind a shift/rotate expression.  The distance must be a uint.  The value
// being shifted (left operand) must be an integer.
static void bindShiftExpression(deSignature scopeSig, deBinding binding) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeSig, binding, &leftType, &rightType, false);
  deLine line = deBindingGetLine(binding);
  if (!deDatatypeIsInteger(leftType)) {
    deError(line, "Only integers can be shifted/rotated");
  }
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deError(line, "Shift/rotate distance must be a uint");
  }
  if (deDatatypeSecret(rightType)) {
    deError(line, "Shift/rotate distance cannot be secret");
  }
  deBindingSetDatatype(binding, leftType);
}

// Bind a relational operator.  Both operands must be strings, arrays, or integers.
static void bindRelationalExpression(deSignature scopeSig, deBinding binding) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeSig, binding, &leftType, &rightType, true);
  if (deDatatypeSecret(leftType)) {
    rightType = deSetDatatypeSecret(rightType, true);
  } else if (deDatatypeSecret(rightType)) {
    leftType = deSetDatatypeSecret(leftType, true);
  }
  if (!typesAreEquivalent(leftType, rightType)) {
    error(binding, "Non-equal types passed to relational operator:%s",
        deGetOldVsNewDatatypeStrings(leftType, rightType));
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_UINT && type != DE_TYPE_INT && type != DE_TYPE_FLOAT &&
      type != DE_TYPE_STRING && type != DE_TYPE_ARRAY) {
    error(binding, "Invalid types passed to relational operator");
  }
  bool secret = deDatatypeSecret(leftType) || deDatatypeSecret(rightType);
  deBindingSetDatatype(binding, deSetDatatypeSecret(deBoolDatatypeCreate(), secret));
}

// Bind an equality operator.  Both operands must be integers.
static void bindEqualityExpression(deSignature scopeSig, deBinding binding) {
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeSig, binding, &leftType, &rightType, true);
  deLine line = deBindingGetLine(binding);
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
  deBindingSetDatatype(binding, deSetDatatypeSecret(deBoolDatatypeCreate(),
      deDatatypeSecret(leftType)));
}

// Bind a negate expression.  The operand must be an integer.
static void bindUnaryArithmeticExpression(deSignature scopeSig, deBinding binding) {
  deDatatype childType = bindUnaryExpression(scopeSig, binding);
  deLine line = deBindingGetLine(binding);
  if (!deDatatypeIsInteger(childType) && !deDatatypeIsFloat(childType)) {
    deError(line, "Only integers can be negated");
  }
  deBindingSetDatatype(binding, childType);
  deBinding child = deBindingGetFirstBinding(binding);
  deBindingSetAutocast(binding, deBindingAutocast(child));
}

// Bind a not expression.  It does logical not on Boolean operands, and
// complement on integer operands.
static void bindNotExpression(deSignature scopeSig, deBinding binding) {
  deDatatype childType = bindUnaryExpression(scopeSig, binding);
  deLine line = deBindingGetLine(binding);
  if (deDatatypeGetType(childType) != DE_TYPE_BOOL) {
    deError(line, "Not operator only works on Boolean types");
  }
  deBindingSetDatatype(binding, childType);
}

// Verify the cast expression is valid, and return the resulting datatype.
// Casts are allowed between numeric types, including floating point types.  We
// can also cast a string to a [u8] array and vise-versa.  Object references
// can be cast to their underlying integer type and back, e.g  <u32>Point(1,2),
// or <Point(u64, u64)>1u32.  Object-to-integer casts are dangerous and we
// should probably restrict its use to code generators.
static void verifyCast(deBinding binding, deDatatype leftDatatype,
    deDatatype rightDatatype, deLine line) {
  if (leftDatatype == rightDatatype) {
    return;  // The cast is a nop.
  }
  if (leftDatatype == deDatatypeNull) {
    error(binding, "Casts require qualified types");
  }
  if (deDatatypeGetType(leftDatatype) == DE_TYPE_CLASS &&
      deDatatypeGetType(rightDatatype) == DE_TYPE_NULL) {
    // This looks like a type binding hint.
    if (deClassGetTclass(deDatatypeGetClass(leftDatatype)) != deDatatypeGetTclass(rightDatatype)) {
      error(binding, "Casting to different class types is not allowed.");
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
    error(binding, "Invalid cast: only casting from/to integers and from/to string are allowed");
  }
  if (leftType == DE_TYPE_STRING) {
    if (rightType != DE_TYPE_ARRAY ||
        deDatatypeGetType(deDatatypeGetElementType(rightDatatype)) !=
            DE_TYPE_UINT) {
      error(binding, "Invalid string conversion.  Only conversions from/to [u8] are allowed.");
    }
    return;
  }
  if (rightType == DE_TYPE_ARRAY) {
    deDatatype elementDatatype = deDatatypeGetElementType(rightDatatype);
    if (deDatatypeGetType(elementDatatype) != DE_TYPE_UINT) {
      error(binding, "Invalid cast: can only convert from/to uint arrays");
    }
    return;
  }
  if (!deDatatypeTypeIsInteger(rightType) && rightType != DE_TYPE_CLASS) {
    error(binding, "Invalid cast");
  }
  if (rightType  == DE_TYPE_CLASS) {
    // Verify the integer width matches the class reference width.
    deClass theClass = deDatatypeGetClass(rightDatatype);
    if (deDatatypeGetWidth(leftDatatype) != deClassGetRefWidth(theClass)) {
      error(binding, "Invalid cast: integer width does not match class reference width");
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
static void bindCastExpression(deBinding binding) {
  deBinding left = deBindingGetFirstBinding(binding);
  deBinding right = deBindingGetNextBinding(left);
  deDatatype leftDatatype = deBindingGetDatatype(left);
  deDatatype rightDatatype = deBindingGetDatatype(right);
  deLine line = deBindingGetLine(binding);
  // We ignore the secrecy of the left type: you can't cast away secrecy.  Just
  // force the left type to have the same secrecy value as the right.
  leftDatatype = deSetDatatypeSecret(leftDatatype, deDatatypeSecret(rightDatatype));
  if (deDatatypeGetType(leftDatatype) == DE_TYPE_ENUMCLASS) {
    // If the cast is to an ENUMCLASS, instead cast to an ENUM.
    deBlock enumBlock = deFunctionGetSubBlock(deDatatypeGetFunction(leftDatatype));
    leftDatatype = deFindEnumIntType(enumBlock);
  }
  verifyCast(binding, leftDatatype, rightDatatype, line);
  deBindingSetDatatype(binding, leftDatatype);
}

// Determin if the access binding is a method call.
static bool isMethodCall(deBinding access) {
  if (deDatatypeGetType(deBindingGetDatatype(access)) != DE_TYPE_FUNCTION ||
      deBindingGetType(access) != DE_EXPR_DOT) {
    return false;
  }
  deBinding left = deBindingGetFirstBinding(access);
  deDatatype datatype = deBindingGetDatatype(left);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type == DE_TYPE_CLASS) {
    return true;
  }
  // Allow method calls on builtin types, such as array.length().
  return type != DE_TYPE_TCLASS  && type != DE_TYPE_FUNCTION;
}

// Find call datatypes.
static deDatatypeArray findCallDatatypes(deSignature scopeSig,
    deBinding binding, deFunction function, deBinding params) {
  deDatatypeArray paramTypes = deDatatypeArrayAlloc();
  deBlock block = deFunctionGetSubBlock(function);
  deVariable var = deBlockGetFirstVariable(block);
  deBinding access = deBindingGetFirstBinding(binding);
  if (deFunctionGetType(function) == DE_FUNC_CONSTRUCTOR) {
    deTclass tclass = deFunctionGetTclass(function);
    deDatatypeArrayAppendDatatype(paramTypes, deTclassDatatypeCreate(tclass));
    var = deVariableGetNextBlockVariable(var);
  } else if (isMethodCall(access)) {
    // Add the type of the object on the left of the dot expression as self parameter.
    deDatatype selfType = deBindingGetDatatype(deBindingGetFirstBinding(access));
    deDatatypeArrayAppendDatatype(paramTypes, selfType);
    var = deVariableGetNextBlockVariable(var);
  }
  deBinding param = deBindingGetFirstBinding(params);
  while (param != deBindingNull) {
    if (var == deVariableNull || deVariableGetType(var) != DE_VAR_PARAMETER) {
      error(params, "Too many arguments passed to function %s", deFunctionGetName(function));
    }
    deDatatypeArrayAppendDatatype(paramTypes, deBindingGetDatatype(param));
    param = deBindingGetNextBinding(param);
    var = deVariableGetNextBlockVariable(var);
  }
  // TODO: deal with default parameters, and named parameters.
  if (var != deVariableNull && deVariableGetType(var) == DE_VAR_PARAMETER) {
    error(params, "Too few arguments passed to function %s", deFunctionGetName(function));
  }
  return paramTypes;
}

// Find the function being called from the bound access expression.  There are
// three cases: a normal function call, a method call on a Tclass, and a call on
// a concrete type such as an array.
static deFunction findCalledFunction(deBinding access) {
  deDatatype accessDatatype = deBindingGetDatatype(access);
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
  deClassSetBound(theClass, true);
  return signature;
}

// Bind a call expression.
// TODO: Handle method calls, builtin calls, and structure constructors.
static bool bindCallExpression(deSignature scopeSig, deBinding binding) {
  deBinding access = deBindingGetFirstBinding(binding);
  deBinding params = deBindingGetNextBinding(access);
  deFunction function = findCalledFunction(access);
  deDatatypeArray paramTypes = findCallDatatypes(scopeSig, binding, function, params);
  if (deFunctionBuiltin(function)) {
    deDatatype returnType = deBindBuiltinCall(deSignatureGetBlock(scopeSig),
        function, paramTypes, deBindingGetExpression(binding));
    deBindingSetDatatype(binding, returnType);
    deDatatypeArrayFree(paramTypes);
    return true;
  }
  deSignature signature = deLookupSignature(function, paramTypes);
  if (signature == deSignatureNull) {
    deLine line = deBindingGetLine(access);
    setStackTraceGlobals(binding);
    signature = deSignatureCreate(function, paramTypes, line);
    if (deSignatureIsConstructor(signature)) {
      // TODO: also resolve methods so factory functions can take null types.
      signature = resolveConstructorSignature(signature);
    }
    deQueueSignature(signature);
  } else {
    deDatatypeArrayFree(paramTypes);
  }
  deBindingSetCallSignature(binding, signature);
  deSignatureSetInstantiated(signature,
      deSignatureInstantiated(signature) || deBindingInstantiating(binding));
  if (!deSignatureBound(signature)) {
    deEvent event = deSignatureEventCreate(signature);
    deStateBinding statebinding = deBindingGetStateBinding(binding);
    deEventAppendStateBinding(event, statebinding);
    return false;
  }
  deBindingSetDatatype(binding, deSignatureGetReturnType(signature));
  return true;  // Success.
}

// Bind the index expression.
static void bindIndexExpression(deSignature scopeSig, deBinding binding) {
  deBinding left = deBindingGetFirstBinding(binding);
  deBinding right = deBindingGetNextBinding(left);
  deDatatype leftType, rightType;
  bindBinaryExpression(scopeSig, binding, &leftType, &rightType, false);
  deLine line = deBindingGetLine(binding);
  if (deDatatypeGetType(rightType) != DE_TYPE_UINT) {
    deError(line, "Index values must be uint");
  }
  if (deDatatypeSecret(rightType)) {
    deError(line, "Indexing with a secret is not allowed");
  }
  deDatatypeType type = deDatatypeGetType(leftType);
  if (type != DE_TYPE_ARRAY && type != DE_TYPE_STRING && type != DE_TYPE_TUPLE) {
    deError(line, "Index into non-array/non-string/non-tuple type");
  }
  if (type == DE_TYPE_TUPLE) {
    deExpression rightExpr = deBindingGetExpression(right);
    if (deExpressionGetType(rightExpr) != DE_EXPR_INTEGER) {
      deError(line,
          "Tuples can only be indexed by constant integers, like y = point[1]");
    }
    uint32 index = deBigintGetUint32(deExpressionGetBigint(rightExpr), line);
    if (index >= deDatatypeGetNumTypeList(leftType)) {
      deError(line, "Tuple index out of bounds");
    }
    deBindingSetDatatype(binding, deDatatypeGetiTypeList(leftType, index));
  } else {
    deDatatype elementType = deDatatypeGetElementType(leftType);
    deBindingSetDatatype(binding, elementType);
  }
  deBindingSetConst(binding, deBindingConst(left));
}

// Find the scope signature for the datatype bound on the binding.  If non
// exists, report an error.
static deBlock findBindingSubScope(deBinding binding) {
  deDatatype datatype = deBindingGetDatatype(binding);
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
      error(binding, "Cannot use '.' on  datatype %s", deDatatypeGetTypeString(datatype));
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

// Create a binding for the variable if the variable is in a function or
// constructor, not a struct or enum.
static void createVariableBinding(deSignature scopeSig, deBinding access, deVariable var) {
  deBlock block = deVariableGetBlock(var);
  if (deBlockGetOwningClass(block) != deClassNull) {
     return;  // We don't create bindings for variables in class blocks.
  }
  deFunction function = deBlockGetOwningFunction(block);
  utAssert(function != deFunctionNull);
  if (block == deSignatureGetBlock(scopeSig)) {
    // This is a local variable in this signature's function.
    deVariableBindingCreate(scopeSig, var);
    return;
  }
  deFunctionType type = deFunctionGetType(function);
  if (type == DE_FUNC_PACKAGE || type == DE_FUNC_MODULE) {
    // Created a global variable.
    // TODO: Check if we're allowed to create variables here.
    deSignature signature = deFunctionGetFirstSignature(function);
    if (signature != deSignatureNull) {
      deVariableBindingCreate(signature, var);
    }
    return;
  }
  error(access, "Cannot create a variable %s in %s", deVariableGetName(var),
      deGetBlockPath(block, false));
}

// Find an existing variable with the given name, or create it if it does not exist.
// Also bind the ident expression binding to its identifier.
static deVariable findOrCreateVariable(deSignature scopeSig, deBinding access) {
  deExpression accessExpr = deBindingGetExpression(access);
  deExpression identExpr;
  deBlock scopeBlock;
  deBinding identBinding;
  if (deExpressionGetType(accessExpr) == DE_EXPR_IDENT) {
    identExpr = accessExpr;
    scopeBlock = deSignatureGetBlock(scopeSig);
    identBinding = access;
  } else {
    utAssert(deExpressionGetType(accessExpr) == DE_EXPR_DOT);
    deBinding dotAccess = deBindingGetFirstBinding(access);
    identBinding = deBindingGetNextBinding(dotAccess);
    identExpr = deBindingGetExpression(identBinding);
    scopeBlock = findBindingSubScope(dotAccess);
  }
  utAssert(deExpressionGetType(identExpr) == DE_EXPR_IDENT);
  utSym sym = deExpressionGetName(identExpr);
  deIdent ident = deBlockFindIdent(scopeBlock, sym);
  if (ident == deIdentNull || deIdentGetType(ident) == DE_IDENT_UNDEFINED) {
    bool generated = deStatementGenerated(deStateBindingGetStatement(deFindBindingStateBinding(access)));
    deLine line = deExpressionGetLine(identExpr);
    deVariable var = deVariableCreate(scopeBlock, DE_VAR_LOCAL, false, sym,
        deExpressionNull, generated, line);
    deVariableSetInstantiated(var, true);
    ident = deVariableGetIdent(var);
    createVariableBinding(scopeSig, access, var);
  }
  if (deIdentGetType(ident) == DE_IDENT_FUNCTION) {
    error(access, "%s is a function, and cannot be assigned.", utSymGetName(sym));
  }
  deIdentAppendBinding(ident, identBinding);
  return deIdentGetVariable(ident);
}

// Update a variable from an assignment binding.
static void updateVariable(deSignature scopeSig, deBinding accessBinding, deBinding targetBinding) {
  deVariable variable = findOrCreateVariable(scopeSig, accessBinding);
  deBinding varBinding = deFindVariableBinding(scopeSig, variable);
  if (varBinding == deBindingNull && deVariableGetBlock(variable) ==
      deSignatureGetBlock(scopeSig)) {
    varBinding = deVariableBindingCreate(scopeSig, variable);
  }
  deDatatype newDatatype = deBindingGetDatatype(targetBinding);
  utAssert(newDatatype != deDatatypeNull);
  deDatatype oldDatatype;
  if (varBinding != deBindingNull) {
    oldDatatype = deBindingGetDatatype(varBinding);
  } else {
    // This happens when we access other scopes with dot expressions.
    oldDatatype = deVariableGetDatatype(variable);
  }
  deDatatype datatype = newDatatype;
  if (oldDatatype != deDatatypeNull) {
    datatype = deUnifyDatatypes(oldDatatype, newDatatype);
  }
  if (datatype == deDatatypeNull) {
    error(accessBinding, "Assigning different type to %s than assigned before:%s",
      deVariableGetName(variable), deGetOldVsNewDatatypeStrings(oldDatatype, newDatatype));
  }
  deBindingSetDatatype(accessBinding, datatype);
  if (varBinding == deBindingNull) {
    deVariableSetDatatype(variable, datatype);
  } else {
    deBindingSetDatatype(varBinding, datatype);
    if ((oldDatatype == deDatatypeNull || deDatatypeGetType(oldDatatype) == DE_TYPE_NULL) &&
       deDatatypeGetType(datatype) != DE_TYPE_NULL) {
      // TODO: Block on sub-elements being null, not just variables.
      deQueueEventBlockedStateBindings(deBindingGetVariableEvent(varBinding));
    }
  }
}

// Bind an assignment expression.
static void bindAssignmentExpression(deSignature scopeSig, deBinding binding) {
  deBinding accessBinding = deBindingGetFirstBinding(binding);
  deBinding targetBinding = deBindingGetNextBinding(accessBinding);
  deExpression access = deBindingGetExpression(accessBinding);
  deExpressionType type  = deExpressionGetType(access);
  if (type == DE_EXPR_IDENT || type == DE_EXPR_DOT) {
    updateVariable(scopeSig, accessBinding, targetBinding);
  }
  deBindingSetDatatype(binding, deBindingGetDatatype(targetBinding));
}

// Bind the array expression.
static void bindArrayExpression(deSignature scopeSig, deBinding binding) { 
  deLine line = deBindingGetLine(binding);
  deBinding firstElement = deBindingGetFirstBinding(binding);
  deDatatype datatype = deBindingGetDatatype(firstElement);
  if (deBindingIsType(firstElement)) {
    deBindingSetIsType(binding, true);
  }
  deBinding nextElement = deBindingGetNextBinding(firstElement);
  while (nextElement != deBindingNull) {
    if (deBindingGetDatatype(nextElement) != datatype) {
      deError(line, "Array elements must have the same type:%s",
          deGetOldVsNewDatatypeStrings(deBindingGetDatatype(nextElement), datatype));
    }
    if (deBindingIsType(nextElement)) {
      deError(line, "Array type expressions can contain only one type, like [u32]");
    }
    nextElement = deBindingGetNextBinding(nextElement);
  }
  deDatatype arrayDatatype = deArrayDatatypeCreate(datatype);
  deBindingSetDatatype(binding, arrayDatatype);
}

// A class data member was defined with a null datatype, such as self.point =
// null(Point).  This does not tell us what the class of self.point is, if
// Point is a template class.  We have to wait for an assignment to be bound
// that clarifies the type.  Statement binding can block on null type
// resolution of a class data member.
//
// Report an error when null types appear anywhere but in an assignment to a
// data member or variable with a non-composite type.  We still can't bind
// constructs like rectangle = {null(Point), null(Point)}, because null type
// resolution only happens on variables.  Binding this would require blocking
// on access expressions like rectangle[0], which would require rebinding the
// access expressions when the variable type is refined.
static void blockOnNullResolution(deSignature scopeSig, deBinding access) {
  deExpressionType type = deBindingGetType(access);
  deLine line = deBindingGetLine(access);
  deSignature varScopeSig;
  utSym sym;
  if (type == DE_EXPR_IDENT) {
    // This is a local in the current scope.
    varScopeSig = scopeSig;
    sym = deExpressionGetName(deBindingGetExpression(access));
  } else if (type == DE_EXPR_DOT) {
    deBinding left = deBindingGetFirstBinding(access);
    deDatatype classType = deBindingGetDatatype(left);
    if (deDatatypeGetType(classType) != DE_TYPE_CLASS) {
      deError(line, "Null type found on non-variable or data member.");
    }
    deBinding right = deBindingGetNextBinding(left);
    utAssert(deBindingGetType(right) == DE_EXPR_IDENT);
    deClass theClass = deDatatypeGetClass(classType);
    varScopeSig = deClassGetFirstSignature(theClass);
    sym = deExpressionGetName(deBindingGetExpression(right));
  } else {
    deError(line, "Null type expressions can only be assigned to variables and class members.");
    return;
  }
  deIdent ident = deBlockFindIdent(deSignatureGetBlock(varScopeSig), sym);
  utAssert(ident != deIdentNull && deIdentGetType(ident) == DE_IDENT_VARIABLE);
  deVariable variable = deIdentGetVariable(ident);
  deBinding varBinding = findVariableBinding(varScopeSig, variable);
  utAssert(varBinding != deBindingNull);
  deEvent event = deVariableEventCreate(varBinding);
  deEventAppendStateBinding(event, deBindingGetStateBinding(access));
}

// Bind a dot expression.  If we're binding a constructor, search in the
// current theClass rather than the class constructor.  We can't bind the ident
// to the right of the dot using scopeSig as the scope.  We instead must wait
// until the left side is bound.  We bind the right hand side identifier
// expression here.
static bool bindDotExpression(deSignature scopeSig, deBinding binding) {
  deBinding accessBinding = deBindingGetFirstBinding(binding);
  deExpression accessExpr = deBindingGetExpression(accessBinding);
  deExpression identExpr = deExpressionGetNextExpression(accessExpr);
  deDatatype datatype = deBindingGetDatatype(accessBinding);
  deDatatypeType type = deDatatypeGetType(datatype);
  deBlock classBlock;
  if (type == DE_TYPE_CLASS) {
    classBlock = deClassGetSubBlock(deDatatypeGetClass(datatype));
  } else if (type == DE_TYPE_NULL) {
    blockOnNullResolution(scopeSig, binding);
    return false;
  } else if (type == DE_TYPE_TCLASS) {
    classBlock = deFunctionGetSubBlock(deTclassGetFunction(deDatatypeGetTclass(datatype)));
  } else if (type == DE_TYPE_FUNCTION || type == DE_TYPE_STRUCT || type == DE_TYPE_ENUMCLASS) {
    deFunction function = deDatatypeGetFunction(datatype);
    deFunctionType funcType = deFunctionGetType(function);
    if (funcType != DE_FUNC_PACKAGE && funcType != DE_FUNC_MODULE &&
        funcType != DE_FUNC_STRUCT && funcType != DE_FUNC_ENUM) {
      deLine line = deBindingGetLine(binding);
      deError(line, "Cannot access identifiers inside function %s", deFunctionGetName(function));
    }
    classBlock = deFunctionGetSubBlock(function);
  } else {
    // Some builtin types have method calls.
    deTclass tclass = deFindDatatypeTclass(datatype);
    classBlock = deFunctionGetSubBlock(deTclassGetFunction(tclass));
  }
  utAssert(deExpressionGetType(identExpr) == DE_EXPR_IDENT);
  // Make the right-hand binding, if we haven't already.
  deBinding identBinding = deBindingGetNextBinding(accessBinding);
  if (identBinding == deBindingNull) {
    identBinding = deExpressionBindingCreate(scopeSig, binding, identExpr,
        deBindingInstantiating(binding));
  }
  deStateBinding statebinding = deBindingGetStateBinding(binding);
  if (!bindIdentExpression(scopeSig, classBlock, statebinding, identBinding)) {
    return false;
  }
  deBindingSetDatatype(binding, deBindingGetDatatype(identBinding));
  deBindingSetConst(binding, deBindingConst(identBinding));
  return true;
}

// Bind an isnull expression.  The expression type is bool.
static void bindIsnullExpression(deSignature scopeSig, deBinding binding) {
  deDatatype datatype = bindUnaryExpression(scopeSig, binding);
  deDatatypeType type = deDatatypeGetType(datatype);
  if (type != DE_TYPE_CLASS && type != DE_TYPE_NULL) {
    deError(deBindingGetLine(binding), "isnull applied to non-object");
  }
  deBindingSetDatatype(binding, deBoolDatatypeCreate());
}

// TODO: Write this.
static void bindNamedParameter(deSignature scopeSig, deBinding binding){}

// Bind the binding's expression.
bool deBindExpression2(deSignature scopeSig, deBinding binding) {
  deExpression expression = deBindingGetExpression(binding);
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_INTEGER:
      bindIntegerExpression(binding);
      break;
    case DE_EXPR_FLOAT:
      bindFloatExpression(binding);
      break;
    case DE_EXPR_BOOL:
      deBindingSetDatatype(binding, deBoolDatatypeCreate());
      break;
    case DE_EXPR_STRING:
      deBindingSetDatatype(binding, deStringDatatypeCreate());
      break;
    case DE_EXPR_IDENT:
      return bindIdentExpression(scopeSig, deBlockNull, deBindingGetStateBinding(binding), binding);
    case DE_EXPR_ARRAY:
      bindArrayExpression(scopeSig, binding);
      break;
    case DE_EXPR_RANDUINT:
      bindRandUintExpression(binding);
      break;
    case DE_EXPR_MODINT:
      // TODO: Write this.
      // bindModintExpression(scopeSig, binding);
      utExit("Write me");
      break;
    case DE_EXPR_BITOR :
    case DE_EXPR_BITOR_EQUALS:
      bindBitwiseOrExpression(scopeSig, binding);
      break;
    case DE_EXPR_ADD:
    case DE_EXPR_ADD_EQUALS:
    case DE_EXPR_SUB:
    case DE_EXPR_SUB_EQUALS:
    case DE_EXPR_MUL:
    case DE_EXPR_MUL_EQUALS:
    case DE_EXPR_DIV:
    case DE_EXPR_DIV_EQUALS:
      bindBinaryArithmeticExpression(scopeSig, binding);
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
      bindBinaryArithmeticExpression(scopeSig, binding);
      if (deDatatypeIsFloat(deBindingGetDatatype(binding))) {
        deError(deExpressionGetLine(expression),
                "Invalid binary operation on floating point types.");
      }
      break;
    case DE_EXPR_MOD:
    case DE_EXPR_MOD_EQUALS:
      // TODO: Write this.
      // bindModExpression(scopeSig, binding);
      utExit("Write me");
      break;
    case DE_EXPR_AND:
    case DE_EXPR_AND_EQUALS:
    case DE_EXPR_OR :
    case DE_EXPR_OR_EQUALS :
    case DE_EXPR_XOR:
    case DE_EXPR_XOR_EQUALS:
      bindBinaryBool(scopeSig, binding);
      break;
    case DE_EXPR_EXP:
    case DE_EXPR_EXP_EQUALS:
      bindExponentiationExpression(scopeSig, binding);
      break;
    case DE_EXPR_SHL:
    case DE_EXPR_SHL_EQUALS:
    case DE_EXPR_SHR:
    case DE_EXPR_SHR_EQUALS:
    case DE_EXPR_ROTL:
    case DE_EXPR_ROTL_EQUALS:
    case DE_EXPR_ROTR:
    case DE_EXPR_ROTR_EQUALS:
      bindShiftExpression(scopeSig, binding);
      break;
    case DE_EXPR_LT:
    case DE_EXPR_LE:
    case DE_EXPR_GT:
    case DE_EXPR_GE:
      bindRelationalExpression(scopeSig, binding);
      break;
    case DE_EXPR_EQUAL:
    case DE_EXPR_NOTEQUAL:
      bindEqualityExpression(scopeSig, binding);
      break;
    case DE_EXPR_NEGATE:
    case DE_EXPR_NEGATETRUNC:
    case DE_EXPR_BITNOT:
      bindUnaryArithmeticExpression(scopeSig, binding);
      break;
    case DE_EXPR_NOT:
      bindNotExpression(scopeSig, binding);
      break;
    case DE_EXPR_CAST:
    case DE_EXPR_CASTTRUNC:
      bindCastExpression(binding);
      break;
    case DE_EXPR_SELECT:
      bindSelectExpression(scopeSig, binding);
      break;
    case DE_EXPR_CALL:
      return bindCallExpression(scopeSig, binding);
    case DE_EXPR_INDEX:
      bindIndexExpression(scopeSig, binding);
      break;
    case DE_EXPR_SLICE:
      bindSliceExpression(scopeSig, binding);
      break;
    case DE_EXPR_SECRET:
    case DE_EXPR_REVEAL:
      bindMarkSecretOrPublic(scopeSig, binding);
      deBindingSetIsType(binding, deBindingIsType(deBindingGetFirstBinding(binding)));
      break;
    case DE_EXPR_EQUALS:
      bindAssignmentExpression(scopeSig, binding);
      break;
    case DE_EXPR_DOT:
      return bindDotExpression(scopeSig, binding);
    case DE_EXPR_DOTDOTDOT:
      bindDotDotDotExpression(scopeSig, binding);
      break;
    case DE_EXPR_LIST:
      // Happens in print statements.
      deBindingSetDatatype(binding, deNoneDatatypeCreate());
      break;
    case DE_EXPR_TUPLE:
      bindTupleExpression(scopeSig, binding);
      break;
    case DE_EXPR_NULL:
      bindNullExpression(scopeSig, binding);
      break;
    case DE_EXPR_NOTNULL:
      utExit("Write me");
      break;
    case DE_EXPR_FUNCADDR:
      bindFunctionPointerExpression(binding);
      break;
    case DE_EXPR_ARRAYOF:
      bindArrayofExpression(scopeSig, binding);
      break;
      break;
    case DE_EXPR_TYPEOF:
      bindTypeofExpression(scopeSig, binding);
      break;
    case DE_EXPR_UNSIGNED:
    case DE_EXPR_SIGNED:
      bindSignConversionExpression(scopeSig, binding);
      break;
    case DE_EXPR_WIDTHOF:
      bindWidthofExpression(scopeSig, binding);
      break;
    case DE_EXPR_ISNULL:
      bindIsnullExpression(scopeSig, binding);
      break;
    case DE_EXPR_UINTTYPE:
      deBindingSetIsType(binding, true);
      deBindingSetDatatype(binding, deUintDatatypeCreate(deExpressionGetWidth(expression)));
      break;
    case DE_EXPR_INTTYPE:
      deBindingSetIsType(binding, true);
      deBindingSetDatatype(binding, deIntDatatypeCreate(deExpressionGetWidth(expression)));
      break;
    case DE_EXPR_FLOATTYPE:
      deBindingSetIsType(binding, true);
      deBindingSetDatatype(binding, deFloatDatatypeCreate(deExpressionGetWidth(expression)));
      break;
    case DE_EXPR_STRINGTYPE:
      deBindingSetIsType(binding, true);
      deBindingSetDatatype(binding, deStringDatatypeCreate());
      break;
    case DE_EXPR_BOOLTYPE:
      deBindingSetIsType(binding, true);
      deBindingSetDatatype(binding, deBoolDatatypeCreate());
      break;
    case DE_EXPR_AS:
      utExit("Unexpected expression type");
      break;
    case DE_EXPR_IN:
      bindInExpression(scopeSig, binding);
      break;
    case DE_EXPR_NAMEDPARAM:
      bindNamedParameter(scopeSig, binding);
      break;
  }
  return true;  // Success.
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
    deQueueEventBlockedStateBindings(deSignatureGetReturnEvent(signature));
  }
}

// Depending on the statement type, we may have some tasks to do once the statement is bound.
static void postProcessBoundStatement(deStateBinding statebinding) {
  switch (deStatementGetType(deStateBindingGetStatement(statebinding))) {
  case DE_STATEMENT_RETURN: {
    deDatatype datatype = deNoneDatatypeCreate();
    if (deStateBindingGetRootBinding(statebinding) != deBindingNull) {
      datatype = deBindingGetDatatype(deStateBindingGetRootBinding(statebinding));
    }
    updateSignatureReturnType(deStateBindingGetSignature(statebinding), datatype);
    break;
  }
  default:
    break;
  }
}

// Bind or continue binding the statement.
void deBindStatement2(deStateBinding statebinding) {
  deBinding binding = deStateBindingGetFirstBinding(statebinding);
  deSignature scopeSig = deStateBindingGetSignature(statebinding);
  while (binding != deBindingNull) {
    if (!deBindExpression2(scopeSig, binding)) {
      return;
    }
    deStateBindingRemoveBinding(statebinding, binding);
    binding = deStateBindingGetFirstBinding(statebinding);
  }
  postProcessBoundStatement(statebinding);
}
