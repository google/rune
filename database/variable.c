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

// Dump the variable to the end of |string| for debugging purposes.
void deDumpVariableStr(deString string, deVariable variable) {
  dePrintIndentStr(string);
  if (deVariableGetType(variable) == DE_VAR_PARAMETER) {
    deStringSprintf(string, "parameter ");
  } else {
    deStringSprintf(string, "variable ");
  }
  deStringSprintf(string, "%s (0x%x)\n", deVariableGetName(variable), deVariable2Index(variable));
}

// Dump the variable to stdio for debugging purposes.
void deDumpVariable(deVariable variable) {
  deString string = deMutableStringCreate();
  deDumpVariableStr(string, variable);
  printf("%s", deStringGetCstr(string));
  fflush(stdout);
  deStringDestroy(string);
}

// Create a variable on the function.
deVariable deVariableCreate(deBlock block, deVariableType type, bool isConst, utSym name,
    deExpression initializer, bool generated, deLine line) {
  deVariable variable = deVariableAlloc();
  deVariableSetType(variable, type);
  deVariableSetConst(variable, isConst);
  deVariableSetGenerated(variable, generated);
  deVariableSetLine(variable, line);
  if (initializer != deExpressionNull) {
    deVariableInsertInitializerExpression(variable, initializer);
  }
  deBlockAppendVariable(block, variable);
  deIdent ident = deIdentCreate(block, DE_IDENT_VARIABLE, name, line);
  deVariableInsertIdent(variable, ident);
  return variable;
}

// Make a copy of a variable in |destBlock|.
deVariable deCopyVariable(deVariable variable, deBlock destBlock) {
  deExpression initializer = deVariableGetInitializerExpression(variable);
  if (initializer != deExpressionNull) {
    initializer = deCopyExpression(initializer);
  }
  deVariable newVariable = deVariableCreate(destBlock, deVariableGetType(variable), deVariableConst(variable),
      deVariableGetSym(variable), initializer, deVariableGenerated(variable), deVariableGetLine(variable));
  return newVariable;
}

// Rename the variable.  Save the old name, in case it needs to be restored later.
void deVariableRename(deVariable variable, utSym newName) {
  deVariableSetSavedName(variable, deVariableGetSym(variable));
  deIdent ident = deVariableGetIdent(variable);
  deRenameIdent(ident, newName);
}
