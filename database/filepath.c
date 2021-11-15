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

// Create a filepath object that records the path to a module or package.
deFilepath deFilepathCreate(char *path, deFilepath parent, bool isPackage) {
  utSym pathSym = utSymCreate(path);
  deFilepath filepath = deRootFindFilepath(deTheRoot, pathSym);
  if (filepath != deFilepathNull) {
    return filepath;
  }
  filepath = deFilepathAlloc();
  deFilepathSetSym(filepath, pathSym);
  deRootInsertFilepath(deTheRoot, filepath);
  deFilepathSetIsPackage(filepath, isPackage);
  if (parent != deFilepathNull) {
    deFilepathAppendFilepath(parent, filepath);
  }
  return filepath;
}

// Returns the path relative to the current working directory.
char *deFilepathGetRelativePath(deFilepath filepath) {
  char *path = deFilepathGetName(filepath);
  char *cwd = utGetcwd();
  while (*path != '\0' && *path == *cwd) {
    path++;
    cwd++;
  }
  if (*path == '/') {
    path++;
  }
  return path;
}
