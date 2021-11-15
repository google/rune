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

// Save a new call location on the frame stack.  This is just so we can report
// the call path to an error.
deStackFrame dePushStackFrame(deStatement statement) {
  deStackFrame frame = deStackFrameAlloc();
  deStackFrameSetStatement(frame, statement);
  deRootAppendStackFrame(deTheRoot, frame);
  return frame;
}

// Pop the top call stack frame.
void dePopStackFrame(void) {
  deStackFrameDestroy(deRootGetLastStackFrame(deTheRoot));
}

// Return true if the statement is an import of any flavor.
bool deStatementIsImport(deStatement statement) {
  deStatementType type = deStatementGetType(statement);
  return type == DE_STATEMENT_USE || type == DE_STATEMENT_IMPORT ||
      type == DE_STATEMENT_IMPORTLIB || type == DE_STATEMENT_IMPORTRPC;
}
