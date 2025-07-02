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

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "llexport.h"

static char *deClangPath = "clang";

// Run the Clang compiler on the LLVM code we generated.
static int runClangCompiler(char *llvmFileName, bool debugMode, bool optimized) {
    char *outFileName = utReplaceSuffix(llvmFileName, "");
  char *optFlag = optimized? "-O3" : "";
  if (debugMode) {
    optFlag = "-g -O0";
  }
  char *command = utSprintf("%s %s -fPIC -o %s %s %s/librune.a %s/libcttk.a",
      deClangPath, optFlag, outFileName, llvmFileName, deLibDir, deLibDir);
  utDebug("Executing: %s\n", command);
  return system(command);
}

// Print usage and exit.
static void usage(void) {
  printf("Usage: rune [options] file\n"
         "    -b        - Don't load builtin Rune files.\n"
         "    -g        - Include debug information for gdb.  Implies -l.\n"
         "    -l <llvmfile> - Write LLVM IR to <llvmfile>.\n"
         "    -L        - Log tokens parsed to rune.log.\n"
         "    -n        - No clang.  Don't compile the resulting .ll output.\n"
         "    -O        - Optimized build.  Passes -O3 to clang.\n"
         "    -p <dir>  - Use <dir> as the root directory for Rune's builtin packages.\n"
         "    -r <dir>  - Use <dir> as the root directory for the project's packages.\n"
         "    -t        - Execute unit tests for all modules.\n"
         "    -U        - Unsafe mode.  Don't generate bounds checking, overflow\n"
         "                detection, and destroyed object access detection.\n"
         "    -x        - Invert the return code: 0 if we fail, and 1 if we pass.\n");
  exit(1);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
  }
  deDebugMode = false;
  deLogTokens = false;
  deInvertReturnCode = false;
  deTestMode = false;
  deUnsafeMode = false;
  deRunePackageDir = NULL;
  deProjectPackageDir = NULL;
  bool noClang = false;
  bool optimized = false;
  deLLVMFileName = NULL;
  bool parseBuiltinFunctions = true;
  uint32 xArg = 1;
  while (xArg < argc && argv[xArg][0] == '-') {
    if (!strcmp(argv[xArg], "-g")) {
      deDebugMode = true;
    } else if (!strcmp(argv[xArg], "-b")) {
      parseBuiltinFunctions = false;
    } else if (!strcmp(argv[xArg], "-t")) {
      deTestMode = true;
    } else if (!strcmp(argv[xArg], "-O")) {
      optimized = true;
    } else if (!strcmp(argv[xArg], "-U")) {
      deUnsafeMode = true;
    } else if (!strcmp(argv[xArg], "-l")) {
      if (++xArg == argc) {
        printf("-l requires the output LLVM IR file name");
        return 1;
      }
      deLLVMFileName = argv[xArg];
    } else if (!strcmp(argv[xArg], "-L")) {
      deLogTokens = true;
    } else if (!strcmp(argv[xArg], "-n")) {
      noClang = true;
    } else if (!strcmp(argv[xArg], "-p")) {
      if (++xArg == argc) {
        printf("-p requires a path to the root package directory");
        return 1;
      }
      deRunePackageDir = argv[xArg];
    } else if (!strcmp(argv[xArg], "-r")) {
      if (++xArg == argc) {
        printf("-p requires a path to the root package directory");
        return 1;
      }
      deProjectPackageDir = argv[xArg];
    } else if (!strcmp(argv[xArg], "-clang")) {
      if (++xArg == argc) {
        printf("-C requires a path argument to the clang executable");
        return 1;
      }
      deClangPath = argv[xArg];
    } else if (!strcmp(argv[xArg], "-x")) {
      deInvertReturnCode = true;
    }  else {
      usage();
    }
    xArg++;
  }
  if (xArg + 1 != argc) {
    usage();
  }
  char* fileName = argv[xArg];
  deStart(fileName);
  if (!utSetjmp()) {
    if (parseBuiltinFunctions) {
      deParseBuiltinFunctions();
    } else {
    }
    deBlock rootBlock = deRootGetBlock(deTheRoot);
    deParseModule(fileName, rootBlock, true, deLineNull);
    deCallFinalInDestructors();
    deCreateLocalAndGlobalVariables();
    deBind();
    deVerifyRelationshipGraph();
    deAddMemoryManagement();
    deInlineIterators();
    // We generate new code in memory management and such, so check binding
    // succeeded.
    deReportEvents();
    if (deLLVMFileName == NULL) {
      deLLVMFileName = utAllocString(utReplaceSuffix(fileName, ".ll"));
    } else {
      // Since we call utFree on this below.
      deLLVMFileName = utAllocString(deLLVMFileName);
    }
    llGenerateLLVMAssemblyCode(deLLVMFileName, deDebugMode);
    if (!noClang) {
      int rc = runClangCompiler(deLLVMFileName, deDebugMode, optimized);
      if (rc != 0) {
        return rc;
      }
    }
    utFree(deLLVMFileName);
    utUnsetjmp();
  } else {
    printf("Exiting due to errors\n");
    return 1;
  }
  deStop();
  if (deInvertReturnCode) {
    return 1;
  }
  return 0;
}
