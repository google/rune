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

// Get the statement type keyword, or NULL if it does not have one.
char* deStatementTypeGetKeyword(deStatementType type) {
  switch (type) {
    case DE_STATEMENT_IF: return "if";
    case DE_STATEMENT_ELSEIF: return "else if";
    case DE_STATEMENT_ELSE: return "else";
    case DE_STATEMENT_DO: return "do";
    case DE_STATEMENT_WHILE: return "while";
    case DE_STATEMENT_FOR: return "for";
    case DE_STATEMENT_FOREACH: return "foreach";
    case DE_STATEMENT_ASSIGN: return "assignment";
    case DE_STATEMENT_CALL: return "call";
    case DE_STATEMENT_PRINT: return "print";
    case DE_STATEMENT_TRY: return "try";
    case DE_STATEMENT_RAISE: return "raise";
    case DE_STATEMENT_EXCEPT: return "except";
    case DE_STATEMENT_RETURN: return "return";
    case DE_STATEMENT_SWITCH: return "switch";
    case DE_STATEMENT_TYPESWITCH: return "typeswitch";
    case DE_STATEMENT_CASE: return "case";
    case DE_STATEMENT_DEFAULT: return "default";
    case DE_STATEMENT_RELATION: return "relation";
    case DE_STATEMENT_TRANSFORM: return "transform";
    case DE_STATEMENT_APPENDCODE: return "appendcode";
    case DE_STATEMENT_PREPENDCODE: return "prependcode";
    case DE_STATEMENT_USE: return "use";
    case DE_STATEMENT_IMPORT: return "import";
    case DE_STATEMENT_IMPORTLIB: return "importlib";
    case DE_STATEMENT_IMPORTRPC: return "importrpc";
    case DE_STATEMENT_YIELD: return "yield";
    case DE_STATEMENT_REF: return "ref";
    case DE_STATEMENT_UNREF: return "unref";
  }
  return NULL;  // Dummy return.
}

// Dump a statement to |string| without the sub-block.
void deDumpStatementNoSubBlock(deString string, deStatement statement) {
  dePrintIndentStr(string);
  char* keyword = deStatementTypeGetKeyword(deStatementGetType(statement));
  deStringSprintf(string, "statement 0x%x", deStatement2Index(statement));
  if (keyword != NULL) {
    deStringSprintf(string, " %s", keyword);
  }
  deExpression expression = deStatementGetExpression(statement);
  if (expression != deExpressionNull) {
    deStringPuts(string, " ");
    deDumpExpressionStr(string, expression);
  }
}

// Dump the statement to the end of |string| for debugging purposes.
void deDumpStatementStr(deString string, deStatement statement) {
  deDumpStatementNoSubBlock(string, statement);
  deBlock block = deStatementGetSubBlock(statement);
  if (block != deBlockNull) {
    deDumpBlockStr(string, block);
  }
}

// Dump the statement to stdout for debugging purposes.
void deDumpStatement(deStatement statement) {
  deString string = deMutableStringCreate();
  deDumpStatementStr(string, statement);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Create a new statement.
deStatement deStatementCreate(deBlock block, deStatementType type, deLine line) {
  deStatement statement = deStatementAlloc();
  deStatementSetType(statement, type);
  utAssert(line != deLineNull);
  deStatementSetLine(statement, line);
  deStatementSetGenerated(statement, deGenerating || deInIterator);
  deBlockAppendStatement(block, statement);
  return statement;
}

// Copy a statement's expression and sub-block to the new statement.
static void copyExpressionAndSubBlockToNewStatement(deStatement statement,
    deStatement newStatement) {
  deExpression expression = deStatementGetExpression(statement);
  if (expression != deExpressionNull) {
    deExpression newExpression = deCopyExpression(expression);
    deStatementInsertExpression(newStatement, newExpression);
  }
  deBlock subBlock = deStatementGetSubBlock(statement);
  if (subBlock != deBlockNull) {
    deBlock newSubBlock = deCopyBlock(subBlock);
    deStatementInsertSubBlock(newStatement, newSubBlock);
  }
  deStatementSetInstantiated(newStatement, deStatementInstantiated(statement));
  deStatementSetExecuted(newStatement, deStatementExecuted(statement));
}

// Append a deep copy of the statement to destBlock.
deStatement deAppendStatementCopy(deStatement statement, deBlock destBlock) {
  deStatement newStatement = deStatementCreate(destBlock, deStatementGetType(statement),
      deStatementGetLine(statement));
  copyExpressionAndSubBlockToNewStatement(statement, newStatement);
  deStatementSetGenerated(newStatement, deStatementGenerated(newStatement) ||
      deStatementGenerated(statement));
  deRelation relation = deStatementGetGeneratedRelation(statement);
  if (relation != deRelationNull) {
    deRelationAppendGeneratedStatement(relation, newStatement);
  }
  return newStatement;
}

// Prepend a deep copy of the statement to destBlock.
deStatement dePrependStatementCopy(deStatement statement, deBlock destBlock) {
  deStatement newStatement = deAppendStatementCopy(statement, destBlock);
  // Move the statement to the start of the block.
  deBlockRemoveStatement(destBlock, newStatement);
  deBlockInsertStatement(destBlock, newStatement);
  return newStatement;
}

// Append a deep copy of the statement to destStatement's block, right after
// |destStatement|.
deStatement deAppendStatementCopyAfterStatement(deStatement statement, deStatement destStatement) {
  deBlock destBlock = deStatementGetBlock(destStatement);
  deStatement newStatement = deAppendStatementCopy(statement, destBlock);
  deBlockRemoveStatement(destBlock, newStatement);
  deBlockInsertAfterStatement(destBlock, destStatement, newStatement);
  return newStatement;
}

// Return true if the statement is an import of any flavor.
bool deStatementIsImport(deStatement statement) {
  deStatementType type = deStatementGetType(statement);
  return type == DE_STATEMENT_USE || type == DE_STATEMENT_IMPORT ||
      type == DE_STATEMENT_IMPORTLIB || type == DE_STATEMENT_IMPORTRPC;
}
