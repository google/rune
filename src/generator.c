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
#include <ctype.h>

deRelation deCurrentRelation;

// Dump the generator to stdout.
void deDumpGenerator(deGenerator generator) {
  dePrintIndent();
  printf("generator %s (0x%x) {\n", deGeneratorGetName(generator), deGenerator2Index(generator));
  deDumpIndentLevel++;
  deDumpBlock(deGeneratorGetSubBlock(generator));
  --deDumpIndentLevel;
  dePrintIndent();
  printf("}\n");
}

// Set the value of a variable, freeing any existing value first.
static void setVariableValue(deVariable variable, deValue value) {
  deValue oldValue = deVariableGetValue(variable);
  if (oldValue != deValueNull) {
    deValueDestroy(oldValue);
  }
  deVariableSetValue(variable, value);
}

// Create a new generator object.
deGenerator deGeneratorCreate(deBlock block, utSym name, deLine line) {
  deFilepath filepath = deBlockGetFilepath(block);
  deFunction function = deFunctionCreate(filepath, block, DE_FUNC_GENERATOR, name,
      DE_LINK_PACKAGE, line);
  deGenerator generator = deGeneratorAlloc();
  deGeneratorSetLine(generator, line);
  deFunctionInsertGenerator(function, generator);
  return generator;
}

// Find the generator from its path expression.
static deGenerator findGenerator(deBlock moduleBlock, deExpression pathExpression) {
  deIdent ident = deFindIdentFromPath(moduleBlock, pathExpression);
  deLine line = deExpressionGetLine(pathExpression);
  if (ident == deIdentNull) {
    deError(line, "Generator %s not found", deGetPathExpressionPath(pathExpression));
  }
  if (deIdentGetType(ident) != DE_IDENT_FUNCTION) {
    deError(line, "Not a generator: %s", deIdentGetName(ident));
  }
  deFunction function = deIdentGetFunction(ident);
  if (deFunctionGetType(function) != DE_FUNC_GENERATOR) {
    deError(line, "Not a generator: %s", deIdentGetName(ident));
  }
  return deFunctionGetGenerator(function);
}

// Return the value of the identifier.
static deValue getIdentValue(deIdent ident, deLine line) {
  switch (deIdentGetType(ident)) {
    case DE_IDENT_FUNCTION:
      return deFunctionValueCreate(deIdentGetFunction(ident));
    case DE_IDENT_VARIABLE: {
      deVariable variable = deIdentGetVariable(ident);
      if (deVariableGetBlock(variable) != deRootGetBlock(deTheRoot)) {
        return deVariableGetValue(deIdentGetVariable(ident));
      }
      if (!deVariableIsType(variable)) {
        deError(line, "Only global type variables can be passed to relation generators");
      }
      deDatatype datatype = deVariableGetDatatype(variable);
      utAssert(datatype != deDatatypeNull);
      if (deDatatypeGetType(datatype) != DE_TYPE_CLASS) {
          deError(line, "Only class type variables can be passed to relation generators");
        }
        return deClassValueCreate(deDatatypeGetClass(datatype));
      }
    case DE_IDENT_UNDEFINED:
      deError(line, "Accessing undefined variable %s in generator", deIdentGetName(ident));
      break;
    }
  return deValueNull;  // Dummy return.
}

// Perform modular reduction on |value|.
static deValue modularReduce(deValue value, deBigint modulus) {
  if (value == deValueNull || modulus == deBigintNull) {
    return value;
  }
  deDatatypeType type = deValueGetType(value);
  if (type == DE_TYPE_UINT || type == DE_TYPE_INT) {
    deBigint bigint = deValueGetBigintVal(value);
    deValue result = deIntegerValueCreate(deBigintModularReduce(bigint, modulus));
    deValueDestroy(value);
    return result;
  }
  utExit("Unexpected type in modular expression");
  return deValueNull;  // Dummy return.
}

// Evaluate the identifier expression.
static deValue evaluateIdentExpression(deBlock scopeBlock, deExpression expression, deBigint modulus) {
  utSym name = deExpressionGetName(expression);
  deIdent ident = deFindIdent(scopeBlock, name);
  deLine line = deExpressionGetLine(expression);
  if (ident == deIdentNull) {
    deError(line, "Identifier %s not found", utSymGetName(name));
  }
  return modularReduce(getIdentValue(ident, line), modulus);
}

// Evaluate a dot expression.
static deValue evaluateDotExpression(deBlock scopeBlock, deExpression expression, deBigint modulus) {
  deExpression pathExpression = deExpressionGetFirstExpression(expression);
  deExpression identExpression = deExpressionGetNextExpression(pathExpression);
  deValue value = deEvaluateExpression(scopeBlock, pathExpression, modulus);
  utSym name = deExpressionGetName(identExpression);
  if (value == deValueNull) {
    return deValueNull;
  }
  deLine line = deExpressionGetLine(expression);
  deDatatypeType type = deValueGetType(value);
  deBlock subBlock = deBlockNull;
  if (type == DE_TYPE_TCLASS) {
    subBlock = deFunctionGetSubBlock(deTclassGetFunction(deValueGetTclassVal(value)));
  } else if (type == DE_TYPE_CLASS) {
    subBlock = deFunctionGetSubBlock(
        deTclassGetFunction(deClassGetTclass(deValueGetClassVal(value))));
  } else if (type == DE_TYPE_FUNCTION) {
    subBlock = deFunctionGetSubBlock(deValueGetFuncVal(value));
  } else {
    deError(line, "Path expression on invalid type");
  }
  deIdent ident = deBlockFindIdent(subBlock, name);
  if (ident == deIdentNull) {
    deError(line, "Identifier %s not found", utSymGetName(name));
  }
  return modularReduce(getIdentValue(ident, line), modulus);
}

// Evaluate an add expression, which could be concatenation.
static deValue evaluateAddExpression(deBlock scopeBlock, deValue left, deValue right, deBigint modulus) {
  deDatatypeType type = deValueGetType(left);
  if (deDatatypeTypeIsInteger(type)) {
    return deIntegerValueCreate(deBigintAdd(deValueGetBigintVal(left), deValueGetBigintVal(right)));
  }
  if (type == DE_TYPE_ARRAY) {
    deError(0, "Array addition not yet supported in generators");
  } else if (type == DE_TYPE_STRING) {
    char *leftString = deStringGetCstr(deValueGetStringVal(left));
    char *rightString = deStringGetCstr(deValueGetStringVal(right));
    char *result = utCatStrings(leftString, rightString);
    size_t len = strlen(result);
    deValueDestroy(left);
    deValueDestroy(right);
    return deStringValueCreate(deStringCreate(result, len));
  } else if (deDatatypeTypeIsInteger(type)) {
    deBigint sum = deBigintAdd(deValueGetBigintVal(left), deValueGetBigintVal(right));
    deValueDestroy(left);
    deValueDestroy(right);
    return modularReduce(deIntegerValueCreate(sum), modulus);
  }
  deError(0, "Cannot add these types together");
  return deValueNull;  // Dummy return.
}

// Evaluate the equality expression.
static deValue evaluateBinaryExpression(deBlock scopeBlock, deExpression expression, deBigint modulus) {
  deExpression left = deExpressionGetFirstExpression(expression);
  deExpression right = deExpressionGetNextExpression(left);
  deValue leftValue = deEvaluateExpression(scopeBlock, left, modulus);
  deValue rightValue = deEvaluateExpression(scopeBlock, right, modulus);
  if (leftValue == deValueNull || rightValue == deValueNull) {
    return deValueNull;
  }
  deLine line = deExpressionGetLine(expression);
  if (deValueGetType(leftValue) != deValueGetType(rightValue)) {
    deError(0, "Different types in binary expression");
  }
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_EQUAL:
      return deBoolValueCreate(deValuesEqual(leftValue, rightValue));
    case DE_EXPR_ADD:
      return evaluateAddExpression(scopeBlock, leftValue, rightValue, modulus);
    default:
      deError(line, "Unsupported expression during generation");
  }
  return deValueNull;  // Dummy return.
}

// Evaluate a negate expression.
static deValue evaluateNegateExpression(deBlock scopeBlock, deExpression expression, deBigint modulus) {
  deValue value = deEvaluateExpression(scopeBlock, deExpressionGetFirstExpression(expression), modulus);
  if (value == deValueNull) {
    return deValueNull;
  }
  value = modularReduce(value, modulus);
  deDatatypeType type = deValueGetType(value);
  deBigint result;
  if (type == DE_TYPE_UINT || type == DE_TYPE_INT) {
    if (modulus == deBigintNull) {
      result = deBigintNegate(deValueGetBigintVal(value));
    } else {
      result = deBigintSub(modulus, deValueGetBigintVal(value));
    }
    return modularReduce(deIntegerValueCreate(result), modulus);
  } else if (deValueGetType(value) == DE_TYPE_FLOAT) {
    deFloat theFloat = deFloatNegate(deValueGetFloatVal(value));
    return deFloatValueCreate(theFloat);
  }
  return deValueNull;  // Cannot negate this type.
}

// Expand a string from the original identifier string, which may contain $ and
// _.  The $ characters until either an _ or the end of the identifier, must
// match an identifier in scope.  The underscores do not become part of the
// identifier.  // For example:
//
//   first$childLabel$childClass = null(self)
//   $parentLabel$parentClass = parent
//   func $class_Create()  // For example, myclassCreate
//
// If an identifier of the same name already existed before running the current
// generator, the identifier is uniquified by appending a number, such as
// child, if child already exists.
static char *expandText(deBlock scopeBlock, char *oldText, deLine line) {
  char *buf = utAllocString(oldText);
  char *p = buf;
  char *q = p;
  deResetString();
  bool isIdent = false;
  bool hasIdent = false;
  bool upperCase = false;
  char c = *q++;
  bool firstTime = true;
  while (true) {
    if ((!firstTime && c == '_') || (c != '_' && !isalnum(c) && c <= '~')) {
      // Found end of current sub-ident.
      if (p < q) {
        *--q = '\0';
        if (!isIdent) {
          deAddString(p);
        } else {
          hasIdent = true;
          if (!strcmp(p, "L")) {
            upperCase = true;
          } else {
            utSym name = utSymCreate(p);
            deIdent ident = deBlockFindIdent(scopeBlock, name);
            if (ident == deIdentNull ||
                deIdentGetType(ident) != DE_IDENT_VARIABLE) {
              deError(line, "Identifier %s not found", utSymGetName(name));
            }
            deValue value = deVariableGetValue(deIdentGetVariable(ident));
            utAssert(value != deValueNull);
            utSym valueName = deValueGetName(value);
            if (valueName == utSymNull) {
              deError(line, "Identifier %s cannot be included as a string",
                      utSymGetName(name));
            }
            uint32 prevPos = deStringPos;
            deAddString(utSymGetName(valueName));
            if (upperCase) {
              upperCase = false;
              deStringVal[prevPos] = toupper(deStringVal[prevPos]);
            } else {
              // Capitalize first letter if this is not the start of the identifier.
              if (prevPos == 0) {
                deStringVal[prevPos] = tolower(deStringVal[prevPos]);
              } else {
                deStringVal[prevPos] = toupper(deStringVal[prevPos]);
              }
            }
          }
        }
        *q++ = c;
      }
      if (c == '\0') {
        utFree(buf);
        if (!hasIdent) {
          return oldText;
        }
        return deStringVal;
      }
      p = q;
      isIdent = c == '$';
      firstTime = false;
    }
    c = *q++;
  }
  return NULL;  // Dummy return.
}

// Expand a symbol.
static utSym expandSym(deBlock scopeBlock, utSym oldSym, deLine line) {
  char *oldName = utSymGetName(oldSym);
  char *result = expandText(scopeBlock, oldName, line);
  if (result == oldName) {
    return oldSym;
  }
  return utSymCreate(result);
}

// Expand a string.
static deString expandString(deBlock scopeBlock, deString string, deLine line) {
  char *result = expandText(scopeBlock, deStringGetCstr(string), line);
  if (!strcmp(result, deStringGetCstr(string))) {
    return string;
  }
  return deCStringCreate(result);
}

// Evaluate the expression.  This is used for both code generation, and constant
// propagation.
deValue deEvaluateExpression(deBlock scopeBlock, deExpression expression, deBigint modulus) {
  deLine line = deExpressionGetLine(expression);
  switch (deExpressionGetType(expression)) {
    case DE_EXPR_INTEGER:
      return modularReduce(deIntegerValueCreate(
          deCopyBigint(deExpressionGetBigint(expression))), modulus);
    case DE_EXPR_FLOAT:
      return deFloatValueCreate(deCopyFloat(deExpressionGetFloat(expression)));
    case DE_EXPR_BOOL:
      return deBoolValueCreate(deExpressionBoolVal(expression));
    case DE_EXPR_STRING: {
      deString oldString = deExpressionGetString(expression);
      deString newString = expandString(scopeBlock, oldString, deExpressionGetLine(expression));
      return deStringValueCreate(newString);
    }
    case DE_EXPR_IDENT:
      return evaluateIdentExpression(scopeBlock, expression, modulus);
    case DE_EXPR_DOT:
      return evaluateDotExpression(scopeBlock, expression, modulus);
    case DE_EXPR_EQUAL:
    case DE_EXPR_ADD:
      return evaluateBinaryExpression(scopeBlock, expression, modulus);
    case DE_EXPR_NEGATE:
      return evaluateNegateExpression(scopeBlock, expression, modulus);
    default: {
      deDatatype datatype = deExpressionGetDatatype(expression);
      if (datatype == deDatatypeNull) {
        deError(line, "Cannot evaluate this expression");
      }
      deDatatypeType type = deDatatypeGetType(datatype);
      if (type == DE_TYPE_CLASS) {
        return deClassValueCreate(deDatatypeGetClass(datatype));
      }
      deError(line, "Cannot evaluate this expression yet");
    }
  }
  return deValueNull;  // Dummy return.
}

// Evaluate parameter variables to the given types, in order.
static void evaluateGeneratorParameters(deBlock moduleBlock, deBlock generatorBlock,
    deExpression parameters, deLine line) {
  deVariable variable = deBlockGetFirstVariable(generatorBlock);
  deExpression parameter = deExpressionGetFirstExpression(parameters);
  while (variable != deVariableNull && deVariableGetType(variable) == DE_VAR_PARAMETER) {
    deValue value;
    if (parameter != deExpressionNull) {
      value = deEvaluateExpression(moduleBlock, parameter, deBigintNull);
      parameter = deExpressionGetNextExpression(parameter);
    } else {
      deExpression defaultValue = deVariableGetInitializerExpression(variable);
      value = deEvaluateExpression(moduleBlock, defaultValue, deBigintNull);
    }
    setVariableValue(variable, value);
    variable = deVariableGetNextBlockVariable(variable);
  }
  if (parameter != deExpressionNull) {
    deError(line, "Too many parameters passed to generator");
  }
}

// Find the destination block of the append/prepend statement.
static deBlock findAppendStatementDestBlock(deBlock scopeBlock, deStatement statement) {
  deExpression expression = deStatementGetExpression(statement);
  if (expression == deExpressionNull) {
    return deRootGetBlock(deTheRoot);
  }
  deValue value = deEvaluateExpression(scopeBlock, expression, deBigintNull);
  deLine line = deStatementGetLine(statement);
  deDatatypeType type = deValueGetType(value);
  if (type == DE_TYPE_TCLASS) {
    return deFunctionGetSubBlock(deTclassGetFunction(deValueGetTclassVal(value)));
  } else if (type == DE_TYPE_CLASS) {
    return deFunctionGetSubBlock(deTclassGetFunction(deClassGetTclass(deValueGetClassVal(value))));
  } else if (type == DE_TYPE_FUNCTION) {
    return deFunctionGetSubBlock(deValueGetFuncVal(value));
  } else {
    deError(line, "Value of %s is not a class or function", deValueGetName(value));
  }
  return deBlockNull;  // Dummy return.
}

// Expand an identifier's symbol.
static void expandIdent(deBlock scopeBlock, deIdent ident) {
  utSym oldSym = deIdentGetSym(ident);
  utSym newSym = expandSym(scopeBlock, oldSym, deIdentGetLine(ident));
  if (newSym != oldSym) {
    deBlock block = deIdentGetBlock(ident);
    deBlockRemoveIdent(block, ident);
    deIdentSetSym(ident, newSym);
    deBlockAppendIdent(block, ident);
  }
}

// Expand all identifier and string expressions in the expression tree.
static void expandExpressionIdentifiers(deBlock scopeBlock, deExpression expression) {
  deExpressionType type = deExpressionGetType(expression);
  if (type == DE_EXPR_IDENT) {
    utSym oldSym = deExpressionGetName(expression);
    deIdent ident = deBlockFindIdent(scopeBlock, oldSym);
    utSym newSym;
    if (ident != deIdentNull) {
      deValue value = getIdentValue(ident, deExpressionGetLine(expression));
      newSym = deValueGetName(value);
    } else {
      newSym = expandSym(scopeBlock, oldSym, deExpressionGetLine(expression));
    }
    deExpressionSetName(expression, newSym);
  } else if (type == DE_EXPR_STRING) {
    deString oldString = deExpressionGetString(expression);
    deString newString = expandString(scopeBlock, oldString, deExpressionGetLine(expression));
    deExpressionSetString(expression, newString);
  }
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    expandExpressionIdentifiers(scopeBlock, child);
  } deEndExpressionExpression;
}

// Expand identifiers and strings in the entire block.
static void expandBlockIdentifiers(deBlock scopeBlock, deBlock block) {
  // This handles tclasses, functions, and variables.
  deIdent ident;
  deSafeForeachBlockIdent(block, ident) {
    expandIdent(scopeBlock, ident);
    deBlock subBlock = deIdentGetSubBlock(ident);
    if (subBlock != deBlockNull) {
      expandBlockIdentifiers(scopeBlock, subBlock);
    }
  } deEndSafeBlockIdent;
  deStatement statement;
  deStatement savedStatement = deCurrentStatement;
  deForeachBlockStatement(block, statement) {
    deCurrentStatement = statement;
    deExpression expression = deStatementGetExpression(statement);
    if (expression != deExpressionNull) {
      expandExpressionIdentifiers(scopeBlock, expression);
    }
    deBlock subBlock = deStatementGetSubBlock(statement);
    if (subBlock != deBlockNull) {
      expandBlockIdentifiers(scopeBlock, subBlock);
    }
  } deEndBlockStatement;
  deCurrentStatement = savedStatement;
}

// Append the block's statements and functions to the relation.
static void appendRelationStatementsAndFunctions(deRelation relation, deBlock block) {
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deRelationAppendGeneratedStatement(relation, statement);
  } deEndBlockStatement;
  deFunction function;
  deForeachBlockFunction(block, function) {
    deRelationAppendGeneratedFunction(relation, function);
  } deEndBlockFunction;
}

// Execute an append class statement.
static void executeAppendOrPrependStatement(deBlock scopeBlock, deStatement statement) {
  deBlock sourceBlock = deStatementGetSubBlock(statement);
  deBlock newBlock = deCopyBlock(sourceBlock);
  expandBlockIdentifiers(scopeBlock, newBlock);
  deBlock destBlock = findAppendStatementDestBlock(scopeBlock, statement);
  deStatementType type = deStatementGetType(statement);
  if (deCurrentRelation != deRelationNull) {
    appendRelationStatementsAndFunctions(deCurrentRelation, newBlock);
  }
  if (type == DE_STATEMENT_APPENDCODE) {
    deAppendBlockToBlock(newBlock, destBlock);
  } else {
    dePrependBlockToBlock(newBlock, destBlock);
  }
}

// Forward declaration for double-recursion.
static void executeBlockStatements(deBlock scopeBlock, deBlock block);

// Execute an if statement.  Evaluate the entire chain of if-elseif-else
// statements.
static void executeIfStatement(deBlock scopeBlock, deStatement statement) {
  while (true) {
    deExpression expression = deStatementGetExpression(statement);
    if (expression == deExpressionNull) {
      // Must be else-statement.
      executeBlockStatements(scopeBlock, deStatementGetSubBlock(statement));
      return;
    }
    deValue condition = deEvaluateExpression(scopeBlock, expression, deBigintNull);
    deLine line = deStatementGetLine(statement);
    if (deValueGetType(condition) != DE_TYPE_BOOL) {
      deError(line, "Non-boolean value used in if-statement");
    }
    if (deValueBoolVal(condition)) {
      executeBlockStatements(scopeBlock, deStatementGetSubBlock(statement));
      return;
    }
    statement = deStatementGetNextBlockStatement(statement);
    deStatementType type = deStatementGetType(statement);
    if (type != DE_STATEMENT_ELSEIF && type != DE_STATEMENT_ELSE) {
      return;
    }
  }
}

// Execute an assignment statement.  Generators may only assign to local
// variables in the current scope block.
static void executeAssignmentStatement(deBlock scopeBlock, deStatement statement) {
  deExpression expression = deStatementGetExpression(statement);
  deExpression targetExpr = deExpressionGetFirstExpression(expression);
  deExpression valueExpr = deExpressionGetNextExpression(targetExpr);
  deLine line = deStatementGetLine(statement);
  if (deExpressionGetType(expression) != DE_EXPR_EQUALS) {
    deError(line, "Generators do not yet support op= statements");
  }
  if (deExpressionGetType(targetExpr) != DE_EXPR_IDENT) {
    deError(line, "Generators only allow assignments to local variables");
  }
  deValue value = deEvaluateExpression(scopeBlock, valueExpr, deBigintNull);
  utSym name = deExpressionGetName(targetExpr);
  deIdent ident = deFindIdent(scopeBlock, name);
  if (ident == deIdentNull) {
    deError(line, "Identifier %s not found", utSymGetName(name));
  }
  if (deIdentGetBlock(ident) != scopeBlock ||
      deIdentGetType(ident) != DE_IDENT_VARIABLE) {
    deError(line, "Generators only allow assignments to local variables");
  }
  deVariable variable = deIdentGetVariable(ident);
  setVariableValue(variable, value);
}

// Execute the statement.
static void executeStatement(deBlock scopeBlock, deStatement statement) {
  deStatement savedStatement = deCurrentStatement;
  deCurrentStatement = statement;
  switch (deStatementGetType(statement)) {
    case DE_STATEMENT_APPENDCODE:
    case DE_STATEMENT_PREPENDCODE:
      executeAppendOrPrependStatement(scopeBlock, statement);
      break;
    case DE_STATEMENT_IF:
      executeIfStatement(scopeBlock, statement);
      break;
    case DE_STATEMENT_ELSEIF:
    case DE_STATEMENT_ELSE:
      break;
    case DE_STATEMENT_ASSIGN:
      executeAssignmentStatement(scopeBlock, statement);
      break;
    default:
      deError(deStatementGetLine(statement),
              "Unsupported statement type in generator");
  }
  deCurrentStatement = savedStatement;
}

// Execute the statements of a block.
static void executeBlockStatements(deBlock scopeBlock, deBlock block) {
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deStatement savedStatement = deCurrentStatement;
    deCurrentStatement = statement;
    executeStatement(scopeBlock, statement);
    deCurrentStatement = savedStatement;
  } deEndBlockStatement;
}

// Execute the generator with the parameters.
static void executeGenerator(deBlock moduleBlock, deGenerator generator,
    deExpression parameters, deLine line) {
  utAssert(!deGenerating);
  deGenerating = true;
  deBlock block = deGeneratorGetSubBlock(generator);
  executeBlockStatements(block, block);
  deGenerating = false;
}

// The module holding the parent class does not normally import the child's
// module, but the child constructor is referenced in null expressions in
// generated code.  Import the child constructor class into the parent's module
// so it can be found during binding.
static void importChildClassIntoParentModule(deFunction parentFunc, deFunction childFunc) {
  deBlock parentBlock = deFunctionGetBlock(parentFunc);
  deBlock childBlock = deFunctionGetBlock(childFunc);
  utSym parentSym = deFunctionGetSym(parentFunc);
  utSym childSym = deFunctionGetSym(childFunc);
  deIdent oldIdent = deBlockFindIdent(parentBlock, childSym);
  if (oldIdent == deIdentNull) {
    deIdent ident = deFunctionIdentCreate(parentBlock, childFunc, childSym);
    deIdentSetImported(ident, true);
  }
  oldIdent = deBlockFindIdent(childBlock, parentSym);
  if (oldIdent == deIdentNull) {
    deIdent ident = deFunctionIdentCreate(childBlock, parentFunc, parentSym);
    deIdentSetImported(ident, true);
  }
}

// Build a Relation edge between the two tclasses.  The first three parameters
// MUST be parent tclass, child tclass, and bool cascade.
static deRelation buildRelation(deGenerator generator) {
  deBlock block = deGeneratorGetSubBlock(generator);
  deVariable parent = deBlockGetFirstVariable(block);
  deVariable child = deVariableGetNextBlockVariable(parent);
  deVariable cascade = deVariableGetNextBlockVariable(child);
  deVariable parentLabel = deVariableGetNextBlockVariable(cascade);
  deVariable childLabel = deVariableGetNextBlockVariable(parentLabel);
  deValue parentVal = deVariableGetValue(parent);
  deValue childVal = deVariableGetValue(child);
  deValue cascadeVal = deVariableGetValue(cascade);
  deValue parentLabelVal = deVariableGetValue(parentLabel);
  deValue childLabelVal = deVariableGetValue(childLabel);
  deFunction parentFunc = deValueGetFuncVal(parentVal);
  deFunction childFunc = deValueGetFuncVal(childVal);
  bool cascadeDelete = deValueBoolVal(cascadeVal);
  deString parentLabelString = deValueGetStringVal(parentLabelVal);
  deString childLabelString = deValueGetStringVal(childLabelVal);
  deTclass parentTclass = deFunctionGetTclass(parentFunc);
  deTclass childTclass = deFunctionGetTclass(childFunc);
  importChildClassIntoParentModule(parentFunc, childFunc);
  return deRelationCreate(generator, parentTclass, parentLabelString, childTclass,
      childLabelString, cascadeDelete);
}

// Execute a generate statement.
void deExecuteRelationStatement(deStatement statement) {
  if (deStatementExecuted(statement)) {
    return;  // Already executed the relation statement.
  }
  deFilepath filepath = deBlockGetFilepath(deStatementGetBlock(statement));
  deBlock moduleBlock = deFilepathGetModuleBlock(filepath);
  deExpression call = deStatementGetExpression(statement);
  deExpression path = deExpressionGetFirstExpression(call);
  deExpression parameters = deExpressionGetNextExpression(path);
  utAssert(deInstantiating);
  deInstantiating = false;
  deBindExpression(moduleBlock, parameters);
  deGenerator generator = findGenerator(moduleBlock, path);
  deLine line = deStatementGetLine(statement);
  if (generator == deGeneratorNull) {
    deError(line, "Generator not found");
  }
  deBlock block = deGeneratorGetSubBlock(generator);
  evaluateGeneratorParameters(moduleBlock, block, parameters, line);
  deCurrentRelation = deRelationNull;
  if (deStatementGetType(statement) == DE_STATEMENT_RELATION) {
    deCurrentRelation = buildRelation(generator);
  }
  executeGenerator(moduleBlock, generator, parameters, line);
  deCurrentRelation = deRelationNull;
  deStatementSetExecuted(statement, true);
  deInstantiating = true;
}
