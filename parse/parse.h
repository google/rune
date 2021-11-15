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

#ifndef THIRD_PARTY_RUNE_PARSE_PARSE_H_
#define THIRD_PARTY_RUNE_PARSE_PARSE_H_

#ifdef MAKEFILE_BUILD
#include "de.h"
#else
#include "third_party/rune/include/de.h"
#endif

// Flex and Bison
int deparse(void);
int delex(void);
void deerror(char *message, ...);
int fileno(FILE *__stream);
extern deBlock deCurrentBlock;
extern char *detext;
extern uint32 deLineNum;
extern deLine deCurrentLine;
extern FILE *deFile;
extern char *deInputString;
extern uint32 deInputStringPos;
extern uint32 deCommentDepth;
extern int32 deParenDepth, deBracketDepth;
extern bool deReachedEndOfFile;
extern deFilepath deCurrentFilepath;
extern bool deInGenerator;
extern bool deParsingMainModule;

#endif  // THIRD_PARTY_RUNE_PARSE_PARSE_H_
