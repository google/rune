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

// Dump the block to the end of |string| for debugging.
void deDumpBlockStr(deString string, deBlock block) {
  dePrintIndentStr(string);
  deStringSprintf(string, "Block 0x%x {\n", deBlock2Index(block));
  deDumpIndentLevel++;
  deIdent ident;
  deForeachBlockIdent(block, ident) {
    deDumpIdentStr(string, ident);
  } deEndBlockIdent;
  deFunction function;
  deForeachBlockFunction(block, function) {
    deDumpFunctionStr(string, function);
  } deEndBlockFunction;
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    deDumpVariableStr(string, variable);
  } deEndBlockVariable;
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deDumpStatementStr(string, statement);
    deStringPuts(string, "\n");
  } deEndBlockStatement;
  --deDumpIndentLevel;
  dePrintIndentStr(string);
  deStringPuts(string, "}\n");
}

// Dump the block to stdout for debugging.
void deDumpBlock(deBlock block) {
  deString string = deMutableStringCreate();
  deDumpBlockStr(string, block);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Count the number of parameter variables on the block.
uint32 deBlockCountParameterVariables(deBlock block) {
  deVariable variable;
  uint32 count = 0;
  deForeachBlockVariable(block, variable) {
    if (deVariableGetType(variable) == DE_VAR_PARAMETER) {
      count++;
    }
  } deEndBlockVariable;
  return count;
}

// Create a new block object.
deBlock deBlockCreate(deFilepath filepath, deBlockType type, deLine line) {
  deBlock block = deBlockAlloc();
  deBlockSetType(block, type);
  deBlockSetLine(block, line);
  if (filepath != deFilepathNull) {
    deFilepathAppendBlock(filepath, block);
  }
  return block;
}

// Return the owning block of a block.
deBlock deBlockGetOwningBlock(deBlock block) {
  switch (deBlockGetType(block)) {
    case DE_BLOCK_FUNCTION: {
      deFunction function = deBlockGetOwningFunction(block);
      deSignature signature = deFunctionGetUniquifiedSignature(function);
      if (signature != deSignatureNull) {
        return deFunctionGetBlock(deSignatureGetFunction(signature));
      }
      return deFunctionGetBlock(function);
    }
    case DE_BLOCK_STATEMENT:
      return deStatementGetBlock(deBlockGetOwningStatement(block));
    case DE_BLOCK_CLASS:
      return deFunctionGetBlock(deTemplateGetFunction(deClassGetTemplate(deBlockGetOwningClass(block))));
  }
  utExit("Unknown block type");
  return deBlockNull;  // Dummy return.
}

// Find the scoped block containing this block.
deBlock deBlockGetScopeBlock(deBlock block) {
  switch (deBlockGetType(block)) {
    case DE_BLOCK_FUNCTION:
    case DE_BLOCK_CLASS:
      return block;
    case DE_BLOCK_STATEMENT:
      return deBlockGetScopeBlock(deStatementGetBlock(deBlockGetOwningStatement(block)));
  }
  utExit("Unexpected block type");
  return deBlockNull;  // Dummy return.
}

// Copy the block's statements into the block containing |destStatement| right
// after |destStatement|.
void deCopyBlockStatementsAfterStatement(deBlock block, deStatement destStatement) {
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deAppendStatementCopyAfterStatement(statement, destStatement);
    destStatement = deStatementGetNextBlockStatement(destStatement);
  } deEndBlockStatement;
}

// Move the block's statements into the block containing |destStatement| right
// after |destStatement|.
void deMoveBlockStatementsAfterStatement(deBlock block, deStatement destStatement) {
  deBlock destBlock = deStatementGetBlock(destStatement);
  deStatement statement;
  deSafeForeachBlockStatement(block, statement) {
    deBlockRemoveStatement(block, statement);
    deBlockInsertAfterStatement(destBlock, destStatement, statement);
    destStatement = statement;
  } deEndSafeBlockStatement;
}

// Append the contents of |sourceBlock| to |destBlock|, and destroy |sourceBlock|.
void deAppendBlockToBlock(deBlock sourceBlock, deBlock destBlock) {
  deFunction function;
  deSafeForeachBlockFunction(sourceBlock, function) {
    deBlockRemoveFunction(sourceBlock, function);
    deBlockAppendFunction(destBlock, function);
    deFunctionIdentCreate(destBlock, function, deFunctionGetSym(function));
  } deEndSafeBlockFunction;
  deStatement statement;
  deSafeForeachBlockStatement(sourceBlock, statement) {
    deBlockRemoveStatement(sourceBlock, statement);
    deBlockAppendStatement(destBlock, statement);
  } deEndSafeBlockStatement;
  deBlockDestroy(sourceBlock);
}

// Prepend the contents of |sourceBlock| to |destBlock|, and destroy |sourceBlock|.
void dePrependBlockToBlock(deBlock sourceBlock, deBlock destBlock) {
  deFunction function;
  deSafeForeachBlockFunction(sourceBlock, function) {
    deBlockRemoveFunction(sourceBlock, function);
    deBlockInsertFunction(destBlock, function);
    deFunctionIdentCreate(destBlock, function, deFunctionGetSym(function));
  } deEndSafeBlockFunction;
  deStatement statement, prevStatement;
  for (statement = deBlockGetLastStatement(sourceBlock);
       statement != deStatementNull; statement = prevStatement) {
    prevStatement = deStatementGetPrevBlockStatement(statement);
    deBlockRemoveStatement(sourceBlock, statement);
    deBlockInsertStatement(destBlock, statement);
  }
  deBlockDestroy(sourceBlock);
}

// Make a shallow copy of the block, without sub-blocks.
deBlock deShallowCopyBlock(deBlock block) {
  deBlock newBlock = deBlockCreate(deBlockGetFilepath(block), deBlockGetType(block),
      deBlockGetLine(block));
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    deAppendStatementCopy(statement, newBlock);
  } deEndBlockStatement;
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    deCopyVariable(variable, newBlock);
  } deEndBlockVariable;
  return newBlock;
}

// Make a deep copy of the block.
deBlock deCopyBlock(deBlock block) {
  deBlock newBlock = deShallowCopyBlock(block);
  deFunction function;
  deForeachBlockFunction(block, function) {
    deCopyFunction(function, newBlock);
  } deEndBlockFunction;
  return newBlock;
}

// Copy identifiers for sub-templates, iterators, and functions from
// |sourceBlock| to |destBlock|.
void deCopyFunctionIdentsToBlock(deBlock sourceBlock, deBlock destBlock) {
  deIdent ident;
  deForeachBlockIdent(sourceBlock, ident) {
    if (deIdentGetType(ident) == DE_IDENT_FUNCTION) {
      deFunction function = deIdentGetFunction(ident);
      deIdent newIdent = deIdentCreate(destBlock, DE_IDENT_FUNCTION,
          deIdentGetSym(deFunctionGetFirstIdent(function)), deFunctionGetLine(function));
      deFunctionAppendIdent(function, newIdent);
    }
  } deEndBlockIdent;
}

// This special case is for an if-elseif-else chain of statements that has an
// else clause, and where every sub-block cannot continue.
static bool allIfClausesReturn(deStatement statement) {
  do {
    deStatementType type = deStatementGetType(statement);
    if (type != DE_STATEMENT_IF && type != DE_STATEMENT_ELSEIF &&
        type != DE_STATEMENT_ELSE) {
      return true;
    }
    deBlock subBlock = deStatementGetSubBlock(statement);
    if (deBlockCanContinue(subBlock)) {
      return false;
    }
    statement = deStatementGetPrevBlockStatement(statement);
  } while (statement != deStatementNull);
  return true;
}

// Update reachability for a switch or typeswitch statement.
static void updateSwitchReachability(deStatement statement, bool* retCanContinue, bool* retCanReturn) {
  bool canContinue = false;
  bool canReturn = false;
  deBlock subBlock = deStatementGetSubBlock(statement);
  deStatement caseStatement;
  bool hasDefault = false;
  deForeachBlockStatement(subBlock, caseStatement) {
    deBlock caseBlock = deStatementGetSubBlock(caseStatement);
    deBlockComputeReachability(caseBlock);
    canReturn |= deBlockCanReturn(caseBlock);
    canContinue |= deBlockCanContinue(caseBlock);
    if (deStatementGetType(caseStatement) == DE_STATEMENT_DEFAULT) {
      hasDefault = true;
    }
  } deEndBlockStatement;
  *retCanContinue &= canContinue || !hasDefault;
  *retCanReturn |= canReturn;
}

// Update the canContinue and canReturn parameters.
static void updateReachability(deStatement statement, bool* canContinue, bool* canReturn) {
  deBlock subBlock = deStatementGetSubBlock(statement);
  bool subBlockCanReturn = false;
  bool subBlockCanContinue = true;
  if (subBlock != deBlockNull) {
    deBlockComputeReachability(subBlock);
    subBlockCanReturn = deBlockCanReturn(subBlock);
    subBlockCanContinue = deBlockCanContinue(subBlock);
    *canReturn |= subBlockCanReturn;
  }
  switch (deStatementGetType(statement)) {
    case DE_STATEMENT_IF:
    case DE_STATEMENT_ELSEIF:
      break;
    case DE_STATEMENT_ELSE:
      if (allIfClausesReturn(statement)) {
        *canContinue = false;
      }
      break;
    case DE_STATEMENT_DO:
      *canContinue &= subBlockCanContinue;
      break;
    case DE_STATEMENT_TRY:
    case DE_STATEMENT_CATCH:
      break;
    case DE_STATEMENT_THROW:
      *canContinue = false;
      break;
    case DE_STATEMENT_RETURN:
      *canContinue = false;
      *canReturn = true;
      break;
    case DE_STATEMENT_YIELD:
      *canReturn = true;
      break;
    case DE_STATEMENT_CALL:
    case DE_STATEMENT_ASSIGN:
    case DE_STATEMENT_WHILE:
    case DE_STATEMENT_FOR:
    case DE_STATEMENT_FOREACH:
    case DE_STATEMENT_PRINT:
    case DE_STATEMENT_USE:
    case DE_STATEMENT_IMPORT:
    case DE_STATEMENT_IMPORTLIB:
    case DE_STATEMENT_IMPORTRPC:
    case DE_STATEMENT_REF:
    case DE_STATEMENT_UNREF:
      // Always can continue through these.
      break;
    case DE_STATEMENT_SWITCH:
    case DE_STATEMENT_TYPESWITCH:
    case DE_STATEMENT_CASE:
    case DE_STATEMENT_DEFAULT:
    case DE_STATEMENT_APPENDCODE:
    case DE_STATEMENT_PREPENDCODE:
    case DE_STATEMENT_RELATION:
    case DE_STATEMENT_TRANSFORM:
      utExit("Unexpected statement type");
      break;
  }
}

// Compute the reachability parameters canReturn and canContinue for this block
// and its subblocks.
void deBlockComputeReachability(deBlock block) {
  bool canContinue = true;
  bool canReturn = false;
  deStatement statement;
  deForeachBlockStatement(block, statement) {
    if (!canContinue) {
      deError(deStatementGetLine(statement), "Cannot reach statement");
    }
    deStatementType type = deStatementGetType(statement);
    if (type == DE_STATEMENT_SWITCH || type == DE_STATEMENT_TYPESWITCH) {
      updateSwitchReachability(statement, &canContinue, &canReturn);
    } else {
      updateReachability(statement, &canContinue, &canReturn);
    }
  } deEndBlockStatement;
  deBlockSetCanContinue(block, canContinue);
  deBlockSetCanReturn(block, canReturn);
}

// Change variable names in |newBlock| to avoid conflicting with |oldBlock|.
// Save the original name on the variable so it can be restored later.
void deResolveBlockVariableNameConfligts(deBlock newBlock, deBlock oldBlock) {
  deVariable newVariable;
  deForeachBlockVariable(newBlock, newVariable) {
    utSym name = deVariableGetSym(newVariable);
    deIdent oldIdent = deFindIdent(oldBlock, name);
    if (oldIdent != deIdentNull) {
      deVariableSetSavedName(newVariable, name);
      utSym newName = deBlockCreateUniqueName(oldBlock, name);
      deVariableRename(newVariable, newName);
    }
  } deEndBlockVariable;
}

// Restore variable names in the block to prior values.
void deRestoreBlockVariableNames(deBlock block) {
  deVariable variable;
  deForeachBlockVariable(block, variable) {
    utSym name = deVariableGetSavedName(variable);
    if (name != utSymNull) {
      deVariableRename(variable, name);
    }
    deVariableSetSavedName(variable, utSymNull);
  } deEndBlockVariable;
}

// Generate a unique name for an identifier in the block, based on |name|.
// Just use |name| if there is no conflict, otherwise, add _n, where n is an
// integer to make the name unique in the block.
utSym deBlockCreateUniqueName(deBlock scopeBlock, utSym name) {
  if (deFindIdent(scopeBlock, name) == deIdentNull) {
    return name;
  }
  uint32 counter = 1;
  utSym newName;
  do {
    newName = utSymCreateFormatted("%s_%u", utSymGetName(name), counter);
    counter++;
  } while (deFindIdent(scopeBlock, newName) != deIdentNull);
  return newName;
}

static bool isUserGenerated(deBlock scopeBlock, deBlock rootScope) {
  if (scopeBlock == rootScope) {
    return false;
  }

  deFunction fn = deBlockGetOwningFunction(scopeBlock);
  deFunctionType funcType = deFunctionGetType(fn);
  if (funcType == DE_FUNC_MODULE || funcType == DE_FUNC_PACKAGE) {
    return true;
  }

  return isUserGenerated(deFunctionGetBlock(fn), rootScope);
}

// Returns true if scopeBlock isn't part of a module or package.
// By definition, the only way scopeBlock would not be part of
// a module or package is if the scopeBlock is auto-generated.
bool deBlockIsUserGenerated(deBlock scopeBlock) {
  deFunction scopeFn = deBlockGetOwningFunction(scopeBlock);
  deBlock rootScope = deRootGetBlock(deFunctionGetRoot(scopeFn));
  return isUserGenerated(scopeBlock, rootScope);
}

// Find the owning module of a block.
deBlock deFindBlockModule(deBlock block) {
  while (block != deBlockNull) {
    if (deBlockGetType(block) == DE_BLOCK_FUNCTION) {
      deFunctionType type = deFunctionGetType(deBlockGetOwningFunction(block));
      if (type == DE_FUNC_MODULE || type == DE_FUNC_PACKAGE) {
        return block;
      }
    }
    block = deBlockGetOwningBlock(block);
  }
  return deBlockNull;
}
