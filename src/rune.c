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

#include <stdio.h>
#include <stdlib.h>
// Hack to enable readlink in unistd.h.
#define __USE_XOPEN2K 1
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define DE_MAX_PATH (1 << 16)

// Build the argv global variable.
static void buildArgvArray(void) {
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  utSym argvSym = utSymCreate("argv");
  char text[] = "argv = readCommandLineArgs()\n";
  deLine line = deLineCreate(deBlockGetFilepath(rootBlock), text, sizeof(text), 0);
  deVariable argv = deVariableCreate(rootBlock, DE_VAR_LOCAL, true, argvSym,
      deExpressionNull, false, line);
  deDatatype argvType = deArrayDatatypeCreate(deStringDatatypeCreate());
  deVariableSetDatatype(argv, argvType);
  deVariableSetInstantiated(argv, true);
}

// Initialize all modules.
void deStart(char *fileName) {
  // TODO: Load a precompiled binary database for the standard library.
  // On Linux, this is required to avoid having to search PATH.
  utStart();
  deExeName = utNewA(char, DE_MAX_PATH);
#ifdef _WIN32
  GetModuleFileNameA(NULL, deExeName, DE_MAX_PATH);
#else
  size_t len = readlink("/proc/self/exe", deExeName, DE_MAX_PATH);
  if (len >= DE_MAX_PATH - 1 || len < 1) {
    utExit("Unable to find executable path");
  }
  deExeName[len] = '\0';
#endif
  char *commonDir = utDirName(deExeName);
  if (dePackageDir != NULL) {
    // Allocate this so we can call utFree on them later.
    dePackageDir = utAllocString(utFullPath(dePackageDir));
  }
  if (!strcmp(utBaseName(commonDir), "bin")) {
    commonDir = utDirName(commonDir);
    deLibDir = utAllocString(utCatStrings(commonDir, "/lib/rune"));
    if (dePackageDir == NULL) {
      dePackageDir = utAllocString(utCatStrings(commonDir, "/lib/rune"));
    }
  } else {
    deLibDir = utAllocString(utCatStrings(commonDir, "/lib"));
    if (dePackageDir == NULL) {
      dePackageDir = utAllocString(commonDir);
    }
  }
  deDatabaseStart();
  deDumpIndentLevel = 0;
  deTheRoot = deRootAlloc();
  char text[] = "func main(argv: [string]) -> i32 {\n";
  deLine line = deLineCreate(deFilepathNull, text, sizeof(text), 0);
  char *path = utDirName(utFullPath(fileName));
  deFilepath rootFilepath = deFilepathCreate(path, deFilepathNull, true);
  deFunction mainFunc = deFunctionCreate(rootFilepath, deBlockNull, DE_FUNC_PACKAGE,
      utSymCreate("main"), DE_LINK_MODULE, line);
  deBlock rootBlock = deFunctionGetSubBlock(mainFunc);
  deRootInsertBlock(deTheRoot, rootBlock);
  deFilepathInsertModuleBlock(rootFilepath, rootBlock);
  deDatatypeStart();
  deBuiltinStart();
  deUtilStart();
  deBindStart();
  buildArgvArray();
}

// Clean up after all modules.
void deStop(void) {
  utFree(dePackageDir);
  dePackageDir = NULL;
  utFree(deExeName);
  utFree(deLibDir);
  deUtilStop();
  deBuiltinStop();
  deDatatypeStop();
  deDatabaseStop();
  utStop(false);
}
