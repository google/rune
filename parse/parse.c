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

// This is required to access the safe version of realpath.  It must come before
// including path.h, which also includes stdlib.h.
#define _DEFAULT_SOURCE
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "parse.h"

deRoot deTheRoot;
uint32 deDumpIndentLevel;
bool deUnsafeMode;
bool deDebugMode;
bool deInvertReturnCode;
char *deLLVMFileName;
bool deTestMode;
char *deExeName;
char *deLibDir;
char *dePackageDir;
uint32 deLineNum;
// The current line we've just read in.
deLine deCurrentLine;
FILE* deFile;
deBlock deCurrentBlock;
deFilepath deCurrentFilepath;
char* deInputString;
uint32 deInputStringPos;
uint32 deCommentDepth;
int32 deParenDepth, deBracketDepth;
bool deReachedEndOfFile;
bool deInGenerator;
bool deParsing;
char *deCurrentFileName;
bool deParsingMainModule;

// Initialize parser globals.
static void initParserGlobals(void) {
  deLineNum = 1;
  deCurrentLine = deLineNull;
  deFile = NULL;
  deInputString = NULL;
  deInputStringPos = 0;
  deCommentDepth = 0;
  deParenDepth = 0;
  deBracketDepth = 0;
  deReachedEndOfFile = false;
}

// Import the identifier into the module.
static void importIdentifier(deBlock destBlock, deBlock sourceBlock,
    deIdent ident, deLine line) {
  utSym name = deIdentGetSym(ident);
  deIdent oldIdent = deBlockFindIdent(destBlock, name);
  if (oldIdent == deIdentNull) {
    deIdent newIdent = deCopyIdent(ident, destBlock);
    deIdentSetImported(newIdent, true);
  } else if (!deIdentImported(oldIdent)) {
    deFunction function = deBlockGetOwningFunction(sourceBlock);
    char *moduleName = deIdentGetName(deFunctionGetFirstIdent(function));
    deError(line, "Imported identifier %s in module %s already exists in this scope",
        utSymGetName(name), moduleName);
  }
}

// Import the identifiers of |sourceBlock| into |destBlock|.  Both blocks are
// in the same package, so conflicts can be resolved manually.
static void importModuleIdentifiers(deBlock destBlock, deBlock sourceBlock, deLine line) {
  deIdent ident;
  deForeachBlockIdent(sourceBlock, ident) {
    if (!deIdentImported(ident)) {
      // Don't import identifiers that the module imported from other modules.
      importIdentifier(destBlock, sourceBlock, ident, line);
    }
  } deEndBlockIdent;
}

// Find an existing module that has already been imported.m
static deBlock findExistingModule(deBlock packageBlock, deExpression pathExpr) {
  deIdent ident = deFindIdentFromPath(packageBlock, pathExpr);
  deLine line = deExpressionGetLine(pathExpr);
  if (ident == deIdentNull) {
    return deBlockNull;  // Not yet loaded.
  }
  if (deIdentGetType(ident) != DE_IDENT_FUNCTION) {
    deError(line, "Identifier %s exists, but is not a module",
        deIdentGetName(ident));
  }
  deFunction function = deIdentGetFunction(ident);
  if (deFunctionGetType(function) == DE_FUNC_PACKAGE) {
    // Look for the module named package.
    utSym packageSym = utSymCreate("package");
    deIdent packageIdent = deBlockFindIdent(deFunctionGetSubBlock(function), packageSym);
    if (packageIdent == deIdentNull || deIdentGetType(packageIdent) != DE_IDENT_FUNCTION) {
      deError(line, "Identifier %s exists, but is not a module",
          deIdentGetName(ident));
    }
    function = deIdentGetFunction(packageIdent);
  }
  if (deFunctionGetType(function) != DE_FUNC_MODULE) {
    deError(line, "Identifier %s exists, but is not a module",
        deIdentGetName(ident));
  }
  return deFunctionGetSubBlock(function);
}

// Handle a use statement.
static void loadUseStatement(deStatement statement, deBlock packageBlock) {
  deExpression pathExpr = deStatementGetExpression(statement);
  deBlock functionBlock = deStatementGetBlock(statement);
  deBlock moduleBlock = findExistingModule(packageBlock, pathExpr);
  if (moduleBlock == deBlockNull) {
    deFilepath filepath = deBlockGetFilepath(functionBlock);
    char *fileName = utSprintf("%s/%s.rn", utDirName(deFilepathGetName(filepath)),
        utSymGetName(deExpressionGetName(pathExpr)));
    moduleBlock = deParseModule(fileName, packageBlock, false);
  }
  importModuleIdentifiers(functionBlock, moduleBlock, deStatementGetLine(statement));
}

// Return the path expression, and module allias if there is one.  Return NULL
static utSym getPathExpressionAndAlias(deExpression importExpression, deExpression *pathExpression) {
  if (deExpressionGetType(importExpression) == DE_EXPR_AS) {
    *pathExpression = deExpressionGetFirstExpression(importExpression);
    return deExpressionGetName(deExpressionGetNextExpression(*pathExpression));
  }
  *pathExpression = importExpression;
  // Default alias is the final identifier in the path.
  if (deExpressionGetType(importExpression) == DE_EXPR_DOT) {
    importExpression = deExpressionGetNextExpression(
        deExpressionGetFirstExpression(importExpression));
  }
  utAssert(deExpressionGetType(importExpression) == DE_EXPR_IDENT);
  return deExpressionGetName(importExpression);
}

// Check for the module being found relative to the filepath, either with .rn,
// or /package.rn appended to commonPath.
static char *findPathUnderFilepath(char *filepath, char *commonPath, bool *isPackageDir) {
  *isPackageDir = false;
  char *path = utSprintf("%s/%s.rn", filepath, commonPath);
  if (utFileExists(path)) {
    return path;
  }
  path = utSprintf("%s/%s/package.rn", filepath, commonPath);
  if (utFileExists(path)) {
    *isPackageDir = true;
    return path;
  }
  return NULL;
}

// Return a path containing all the identifiers on the import statement.  E.g.
// return "foo/bar/baz" for import foo.bar.baz.
static char *findPathExpressionPath(deExpression pathExpr) {
  if (deExpressionGetType(pathExpr) == DE_EXPR_IDENT) {
    // Base case.
    return utSymGetName(deExpressionGetName(pathExpr));
  }
  utAssert(deExpressionGetType(pathExpr) == DE_EXPR_DOT);
  deExpression prefixPathExpr = deExpressionGetFirstExpression(pathExpr);
  deExpression identExpr = deExpressionGetNextExpression(prefixPathExpr);
  char *prefix = findPathExpressionPath(prefixPathExpr);
  return utSprintf("%s/%s", prefix, utSymGetName(deExpressionGetName(identExpr)));
}

// Find the module file.  First, look relative to the current module's package,
// which is |packageBlock|.  If we can't find it there, look relative to the
// top-level rune file, which is found on the filepath on the root block.  If
// still not found, look in the system package path, pointed to by dePackageDir.
// Set |isPackageDir| to true if we found a module/package.rn file.
static char *findModuleFile(deBlock packageBlock, deExpression pathExpr, bool *isPackageDir) {
  char *commonPath = utAllocString(findPathExpressionPath(pathExpr));
  // Check relative to current package.
  char *path = findPathUnderFilepath(deFilepathGetName(
      deBlockGetFilepath(packageBlock)), commonPath, isPackageDir);
  if (path  != NULL) {
    utFree(commonPath);
    return utAllocString(path);
  }
  // Check relative to top-level package.
  path = findPathUnderFilepath(deFilepathGetName(
      deBlockGetFilepath(deRootGetBlock(deTheRoot))), commonPath, isPackageDir);
  if (path  != NULL) {
    utFree(commonPath);
    return utAllocString(path);
  }
  // Check in the shared package directory in dePackageDir.
  path = findPathUnderFilepath(dePackageDir, commonPath, isPackageDir);
  if (path  != NULL) {
    utFree(commonPath);
    return utAllocString(path);
  }
  deError(deExpressionGetLine(pathExpr),
      "Unable to find module %s.  Did you remember to add it to your dependencies?",
      commonPath);
  utFree(commonPath);
  return NULL;  // Dummy return.
}

// Find a sub-package block on the parent package, and create it if it does not
// yet exist.
static deBlock findOrCreatePackageBlock(deBlock parentPackage, deExpression identExpr) {
  utSym sym = deExpressionGetName(identExpr);
  deIdent ident = deBlockFindIdent(parentPackage, sym);
  if (ident != deIdentNull) {
    if (deIdentGetType(ident) != DE_IDENT_FUNCTION ||
        deFunctionGetType(deIdentGetFunction(ident)) != DE_FUNC_PACKAGE) {
      deError(deExpressionGetLine(identExpr),
          "Identifier %s already exists, but is not a package", utSymGetName(sym));
    }
    return deFunctionGetSubBlock(deIdentGetFunction(ident));
  }
  // We have to create the package block.
  deFilepath parentFilepath = deBlockGetFilepath(parentPackage);
  char *parentPath = deFilepathGetName(parentFilepath);
  char *newPath = utSprintf("%s/%s", parentPath, utSymGetName(sym));
  deFilepath filepath = deFilepathCreate(newPath, parentFilepath, true);
  deFunction function = deFunctionCreate(filepath, parentPackage, DE_FUNC_PACKAGE, sym,
      DE_LINK_PACKAGE, deExpressionGetLine(identExpr));
  deBlock subBlock = deFunctionGetSubBlock(function);
  deFilepathInsertModuleBlock(filepath, subBlock);
  return subBlock;
}

// Create package blocks corresponding to pathExpression.  Return the lowest
// level package block, corresponding to the end of the path if |isPackageDir|
// is true, otherwise the second to last part of the path when.  This will be
// the package in which the module should be loaded.
static deBlock createPackagePath(deExpression pathExpr, bool isPackageDir) {
  if (deExpressionGetType(pathExpr) == DE_EXPR_IDENT) {
    // Base case.
    deBlock rootBlock = deRootGetBlock(deTheRoot);
    if (isPackageDir) {
      return findOrCreatePackageBlock(rootBlock, pathExpr);
    }
    return rootBlock;
  }
  utAssert(deExpressionGetType(pathExpr) == DE_EXPR_DOT);
  deExpression prefixPathExpr = deExpressionGetFirstExpression(pathExpr);
  deExpression identExpr = deExpressionGetNextExpression(prefixPathExpr);
  deBlock parentPackage = createPackagePath(prefixPathExpr, isPackageDir);
  if (isPackageDir) {
    return findOrCreatePackageBlock(parentPackage, identExpr);
  }
  return parentPackage;
}

// Create a full path of package blocks to the module.  For example, consider
//
//     import foo.bar.baz
//
// We may not have imported any portion of this path before, and a chain of
// package blocks may need to be created for at foo, bar, and possibly baz.  If
// baz is a directory, look for package.rn in baz, if baz is a directory
// containing package.rn.
//
// First, we need to find one of:
//
//     <dir of current module>/foo/bar/baz.rn
//     <dir of current module>/foo/bar/baz/package.rn
//     <dir of top module>/foo/bar/baz.rn
//     <dir of top module>/foo/bar/baz/package.rn
//     <system package dir>/foo/bar/baz.rn
//     <system package dir>/foo/bar/baz/package.rn
//
// Accept the first that exists, in this order, and report an error if none
// exist.  Next, create the path of package blocks for foo.bar if we found
// foo.bar.rn somewhere, or create path of package blocks foo.bar.baz if baze is
// a package directory containing package.rn.
//
// Return the full path to the module, either ending in baz.rn, or
// baz/package.rn.  Set |destPackageBlock| to bar if baz.rn exists, or baz, if
// we found baz/package.rn.
static char *createPackagePathToModule(deBlock packageBlock,
    deExpression pathExpr, deBlock *destPackageBlock) {
  // First, find the file we need to load.
  bool isPackageDir;
  char *fileName = findModuleFile(packageBlock, pathExpr, &isPackageDir);
  *destPackageBlock = createPackagePath(pathExpr, isPackageDir);
  return fileName;
}

// Handle a use statement.
static void loadImportStatement(deStatement statement, deBlock packageBlock) {
  deExpression pathExpr;
  utSym alias = getPathExpressionAndAlias(deStatementGetExpression(statement), &pathExpr);
  deBlock moduleBlock = findExistingModule(packageBlock, pathExpr);
  if (moduleBlock == deBlockNull) {
    deBlock destPackageBlock;
    char *fileName = createPackagePathToModule(packageBlock, pathExpr, &destPackageBlock);
    moduleBlock = deParseModule(fileName, destPackageBlock, false);
    utFree(fileName);
  }
  // Now import just one identifier.
  deBlock destBlock = deStatementGetBlock(statement);
  deFunction moduleFunction = deBlockGetOwningFunction(moduleBlock);
  deIdent newIdent = deFunctionIdentCreate(destBlock, moduleFunction, alias);
  deIdentSetImported(newIdent, true);
}

// Load all the imported modules and packages.
static void loadImports(deBlock packageBlock, deBlock moduleBlock) {
  deStatement statement;
  deForeachBlockStatement(moduleBlock, statement) {
    switch (deStatementGetType(statement)) {
      case DE_STATEMENT_USE:
        loadUseStatement(statement, packageBlock);
        break;
      case DE_STATEMENT_IMPORT:
        loadImportStatement(statement, packageBlock);
        break;
      case DE_STATEMENT_IMPORTLIB:
      case DE_STATEMENT_IMPORTRPC:
        utExit("Write me!");
        break;
      default:
        break;
    }
  } deEndBlockStatement;
}

// Parse the file.
static void parseFile(char *fileName, char *fullName) {
  initParserGlobals();
  deFile = fopen(fullName, "r");
  deCurrentFileName = utAllocString(fileName);
  if (deFile == NULL) {
    printf("Could not open file %s\n", fileName);
    exit(1);
  }
  if (deparse() != 0) {
    printf("Failed to parse %s\n", fileName);
    exit(1);
  }
  fclose(deFile);
  deFile = NULL;
  utFree(deCurrentFileName);
  deCurrentFileName = NULL;
  deCurrentFileName = NULL;
}

// Execute module relations.
static void executeModuleRelations(deBlock moduleBlock) {
  deStatement statement;
  deForeachBlockStatement(moduleBlock, statement) {
    deStatementType type = deStatementGetType(statement);
    if (type == DE_STATEMENT_RELATION || type == DE_STATEMENT_GENERATE) {
      deInstantiateRelation(statement);
    }
  } deEndBlockStatement;
}

// Parse the Rune file into a module.  |currentBlock| should be the package
// initializer function that will call this module's initializer function.
deBlock deParseModule(char *fileName, deBlock packageBlock, bool isMainModule) {
  fileName = utConvertDirSepChars(fileName);
  char *fullName = utAllocString(utFullPath(fileName));
  if (fullName == NULL) {
    utExit("Unable to read file %s", fileName);
  }
  deFilepath parentFilepath = deBlockGetFilepath(packageBlock);
  utAssert(deFilepathIsPackage(parentFilepath));
  deFilepath filepath = deFilepathCreate(fullName, parentFilepath, false);
  deCurrentFilepath = filepath;
  utSym moduleName = utSymCreate(utReplaceSuffix(utBaseName(fileName), ""));
  if (!deIsLegalIdentifier(utSymGetName(moduleName))) {
    deError(deLineNull, "Module %s has an invalid name", utSymGetName(moduleName));
  }
  if (deBlockFindIdent(packageBlock, moduleName) != deIdentNull) {
    deError(deLineNull, "Module name %s already in use in this scope", utSymGetName(moduleName));
  }
  char *text = utSprintf("Auto-generated function %s()", utSymGetName(moduleName));
  deLine line = deLineCreate(filepath, text, strlen(text), 0);
  deFunction moduleFunc = deFunctionCreate(filepath, packageBlock, DE_FUNC_MODULE,
      moduleName, DE_LINK_MODULE, line);
  deBlock newModuleBlock = deFunctionGetSubBlock(moduleFunc);
  deParsingMainModule = isMainModule;
  deCurrentBlock = newModuleBlock;
  deFilepathInsertModuleBlock(filepath, deCurrentBlock);
  parseFile(fileName, fullName);
  deCurrentFilepath = deFilepathNull;
  deParsingMainModule = false;
  loadImports(packageBlock, newModuleBlock);
  deInsertModuleInitializationCall(moduleFunc);
  utFree(fullName);
  executeModuleRelations(newModuleBlock);
  return newModuleBlock;
}

// Parse the Rune string.
void deParseString(char* string, deBlock currentBlock) {
  deCurrentBlock = currentBlock;
  deCurrentFilepath = deBlockGetFilepath(currentBlock);
  initParserGlobals();
  deInputString = string;
  deCurrentFileName = "INTERNAL";
  if (deparse() != 0) {
    printf("Failed to parse input string\n");
    exit(1);
  }
  deCurrentFilepath = deFilepathNull;
  deCurrentBlock = deBlockNull;
  deInputString = NULL;
  deInputStringPos = 0;
}

static bool deParsedBuilinFile;
// Callback to parse a builtin file.
static void parseBuiltinFile(char *dirName, char *fileName) {
  char *suffix = utSuffix(fileName);
  if (suffix == NULL || strcmp(utSuffix(fileName), "rn")) {
    return;
  }
  char *fullName = utAllocString(utSprintf("%s/%s", dirName, fileName));
  deCurrentBlock = deRootGetBlock(deTheRoot);
  deCurrentFilepath = deFilepathCreate(fullName, deFilepathNull, false);
  deFilepathInsertModuleBlock(deCurrentFilepath, deCurrentBlock);
  parseFile(fileName, fullName);
  deCurrentFilepath = deFilepathNull;
  deCurrentBlock = deBlockNull;
  utFree(fullName);
  deParsedBuilinFile = true;
}

#ifdef _WIN32
void utForeachDirectoryFile(char *dirName, void (*func)(char *dirName, char *fileName)) {
  char findpath[_MAX_PATH];
  HANDLE fh;
  WIN32_FIND_DATA wfd;

  strcpy(findpath, dirName);
  strcat (findpath, "\\*.*");
  fh = FindFirstFile (findpath, &wfd);
  if (fh != INVALID_HANDLE_VALUE) {
    do {
      if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        func(dirName, wfd.cFileName);
      }
    } while(FindNextFile(fh, &wfd));
    FindClose(fh);
  }
}
#endif

// Parse the built-in functions in the standard library.
void deParseBuiltinFunctions(void) {
  char *builtinDir = utAllocString(utSprintf("%s/builtin", dePackageDir));
  deParsedBuilinFile = false;
  utForeachDirectoryFile(builtinDir, parseBuiltinFile);
  if (!deParsedBuilinFile) {
    utWarning("Warning: Found no builtin functions in directory %s", builtinDir);
  }
  utFree(builtinDir);
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  executeModuleRelations(rootBlock);
}
