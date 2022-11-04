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

#ifndef EXPERIMENTAL_WAYWARDGEEK_RUNE_LL_H_
#define EXPERIMENTAL_WAYWARDGEEK_RUNE_LL_H_

#include "de.h"
#ifdef MAKEFILE_BUILD
#include "lldatabase.h"
#else
#include "third_party/rune/llvm/lldatabase.h"
#endif

// LLVM declarations module.
void llStart(void);
void llStop(void);
char *llGetTypeString(deDatatype datatype, bool isDefinition);
void llDeclareRuntimeFunction(char *funcName);
void llDeclareOverloadedFunction(char *text);
void llAddStringConstant(deString string);
utSym llAddArrayConstant(deExpression expression);
void llDeclareBlockGlobals(deBlock block);
void llDeclareExternCFunctions(void);
void llWriteDeclarations(void);
utSym llStringGetLabel(void);
char *llEscapeString(deString string);
char *llEscapeText(char *text);
char *llEscapeIdentifier(char *identifier);
char *llStringGetName(deString string);
void llCreateFilepathTags(void);
llTag llGenerateMainTags(void);
void llGenerateSignatureTags(deSignature signature);
void llWriteDebugTags(void);
void llCreateGlobalVariableTags(deBlock block);
void llDeclareLocalVariable(deVariable variable, uint32 argNum);
void llDeclareGlobalVariable(deVariable variable);
llTag llCreateLocationTag(llTag scopeTag, deLine line);
char *llGetVariableName(deVariable variable);
void llDeclareNewTuples(void);
bool llDatatypeIsBigint(deDatatype datatype);
bool llDatatypeIsArray(deDatatype datatype);
uint32 llBigintBitsToWords(uint32 width, bool isSigned);
bool llDatatypePassedByReference(deDatatype datatype);

// LLVM has a bug: type declarations MUST precede their use.  Therefore, when
// printing a function, use these functions instead of writing to the file
// directly.  This allows declarations required by the function to be printed
// first, using fprintf, etc.
#define llPrintf deSprintToString
#define llPuts deAddString
void llPrintBlockString(void);

// Globals.
extern FILE* llAsmFile;
extern uint32 llSizeWidth;
extern bool llDebugMode;
extern bool llNoBugs;
extern char *llSize;
extern deDatatype llSizeType;
extern char* llPath;

#endif  // EXPERIMENTAL_WAYWARDGEEK_RUNE_LL_H_
