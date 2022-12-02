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

// Create a new line object to record the actual text parsed.  Include the
// terminating '\0'.
deLine deLineCreate(deFilepath filepath, char *buf, uint32 len, uint32 lineNum) {
  deLine line = deLineAlloc();
  deLineResizeTexts(line, len + 1);
  memcpy(deLineGetText(line), buf, len);
  deLineSetiText(line, len, '\0');
  deLineSetLineNum(line, lineNum);
  if (filepath != deFilepathNull) {
    deFilepathAppendLine(filepath, line);
  }
  return line;
}

// Write the line to stdout, with a filename and line number prefix.
void deDumpLine(deLine line) {
  deFilepath filepath = deLineGetFilepath(line);
  utAssert(filepath != deFilepathNull);
  char *path = deFilepathGetRelativePath(filepath);
  if (*path != '\0') {
    printf("%s:%u: ", path, deLineGetLineNum(line));
  } else {
    printf("Auto-generated: ");
  }
  fputs(deLineGetText(line), stdout);
}
