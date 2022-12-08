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

#include <ctype.h>
#include <stdlib.h>

char* deStringVal;
uint32 deStringAllocated;
uint32 deStringPos;
bool deGenerating;
bool deInIterator;
bool deUseNewBinder;
deSignature deCurrentSignature;

// Print indents by 2 spaces to the current level of dump-indent.
void dePrintIndent(void) {
  for (uint32 i = 0; i < deDumpIndentLevel; i++) {
    putchar(' ');
    putchar(' ');
  }
}

// Print indents by 2 spaces to the current level of dump-indent.
void dePrintIndentStr(deString string) {
  for (uint32 i = 0; i < deDumpIndentLevel; i++) {
    deStringPuts(string, "  ");
  }
}

void deError(deLine line, char* format, ...) {
  char *buff;
  va_list ap;
  va_start(ap, format);
  buff = utVsprintf(format, ap);
  va_end(ap);
  if (line != deLineNull) {
    deFilepath filepath = deLineGetFilepath(line);
    utAssert(filepath != deFilepathNull);
    char *path = deFilepathGetRelativePath(filepath);
    if (*path != '\0') {
      printf("%s:%u: ", path, deLineGetLineNum(line));
    }
  }
  printf("Error: %s\n", buff);
  if (line != deLineNull) {
    fputs(deLineGetText(line), stdout);
  }
  if (deCurrentStatement != deStatementNull && deStatementGenerated(deCurrentStatement)) {
    printf("After generation: ");
    deDumpStatement(deCurrentStatement);
  }
  dePrintStack();
  if (!deInvertReturnCode) {
    utExit("Exiting due to error...");
  }
  printf("Exiting due to error...\n");
  deGenerateDummyLLFileAndExit();
}

// Return the path to the block with '_' separators if printing as a label, and
// with '.' separators otherwise.
char *deGetBlockPath(deBlock block, bool as_label) {
  if (block == deRootGetBlock(deTheRoot)) {
    return "";
  }
  char* name = NULL;
  bool isPackage = false;
  switch (deBlockGetType(block)) {
    case DE_BLOCK_FUNCTION: {
      deFunction function = deBlockGetOwningFunction(block);
      name = deFunctionGetName(function);
      if (deFunctionGetType(function) == DE_FUNC_MODULE && !strcmp(name, "package")) {
        isPackage = true;
      }
      break;
    }
    case DE_BLOCK_STATEMENT:
      utExit("Cannot get path to a statement");
      break;
    case DE_BLOCK_CLASS: {
      deClass theClass = deBlockGetOwningClass(block);
      if (as_label) {
        deTclass tclass = deClassGetTclass(theClass);
        if (deClassGetNumber(theClass) == 1) {
          name = deTclassGetName(tclass);
        } else {
          name = utSprintf("%s_ver%u", deTclassGetName(tclass), deClassGetNumber(theClass));
        }
      } else {
        name = deTclassGetName(deClassGetTclass(theClass));
      }
      break;
    }
  }
  if (deBlockGetOwningBlock(block) == deRootGetBlock(deTheRoot)) {
    return name;
  }
  char* path = deGetBlockPath(deBlockGetOwningBlock(block), as_label);
  if (isPackage) {
    return path;
  }
  if (as_label) {
    return utSprintf("%s_%s", path, name);
  }
  return utSprintf("%s.%s", path, name);
}

// Create a label for a signature.  This will be the entry point of its function.
char* deGetSignaturePath(deSignature signature) {
  deBlock subBlock;
  deFunction function = deSignatureGetFunction(signature);
  subBlock = deFunctionGetSubBlock(function);
  char* path = deGetBlockPath(subBlock, true);
  uint32 number = deSignatureGetNumber(signature);
  if (number == 0) {
    return path;
  }
  return utSprintf("%s%u", path, number);
}

// Reset the string position.
void deResetString(void) {
  deStringPos = 0;
  deStringVal[0] = '\0';
}

// Initialize the memory management generator.
void deUtilStart(void) {
  deStringAllocated = 4096;
  deStringVal = utCalloc(deStringAllocated, sizeof(char));
  deGenerating = false;
  deCurrentSignature = deSignatureNull;
}

// Clean up after the memory management generator.
void deUtilStop(void) {
  utFree(deStringVal);
}

// Append a string to deString.
void deAddString(char* string) {
  uint32 len = strlen(string);
  if (deStringPos + len >= deStringAllocated) {
    deStringAllocated += (deStringAllocated >> 1) + len;
    utResizeArray(deStringVal, deStringAllocated);
  }
  strcpy(deStringVal + deStringPos, string);
  deStringPos += len;
}

// Sprint to the string.
#ifdef _WIN32
void deSprintToString(char* format, ...) {
  va_list ap;
  int len = 0;
  int i;
  int argn[16];
  char argt[16];
  char *args[16];
  int numarg = 0;
  const char *p = format;

  memset(argn, 0, sizeof(argn));
  memset(argt, 0, sizeof(argt));
  memset(args, 0, sizeof(args));
  while (*p) {
    if (*p == '%') {
      const char *np = ++p;
      if (*np == '%') {
        p++;
      } else {
        while (isdigit(*p)) {
          p++;
        }
        if (p != np && *p == '$') {
          p++;
          int n = atoi(np);
          if (argt[n-1] == 0) {
            argt[n-1] = *p;
            argn[n-1]++;
            if (n > numarg) numarg = n;
            p++;
          }
        }
      }
    } else {
      p++;
      len++;
    }
  }

  if (numarg == 0) {
    char buf[1];
    va_start(ap, format);
    uint32 len = vsnprintf(buf, 1, format, ap) + 1;
    va_end(ap);
    if (deStringPos + len >= deStringAllocated) {
      deStringAllocated += (deStringAllocated >> 1) + len;
      utResizeArray(deStringVal, deStringAllocated);
    }
    va_start(ap, format);
    vsnprintf(deStringVal + deStringPos, len, format, ap);
    va_end(ap);
    deStringPos += len - 1;
  } else {
    va_start(ap, format);
    for (i = 0; i < numarg; i++) {
      switch (argt[i]) {
        case 's':
          args[i] = strdup(va_arg(ap, char*));
          break;
        case 'u':
          char nbuf[16];
          snprintf(nbuf, sizeof(nbuf), "%u", va_arg(ap, uint32));
          args[i] = strdup(nbuf);
          break;
      }
      len += strlen(args[i]) * argn[i];
    }
    va_end(ap);

    if (deStringPos + len >= deStringAllocated) {
      deStringAllocated += (deStringAllocated >> 1) + len;
      utResizeArray(deStringVal, deStringAllocated);
    }
    *(deStringVal + deStringPos) = 0;
    p = format;
    while (*p) {
      if (*p == '%') {
        const char *np = ++p;
        if (*np == '%') {
          strcat(deStringVal + deStringPos, "%%");
          deStringPos++;
          deStringPos++;
          p++;
        } else {
          while (*p && isdigit(*p)) {
            p++;
          }
          if (p != np && *p == '$') {
            int n = atoi(np);
            strcat(deStringVal + deStringPos, args[n-1]);
            deStringPos += strlen(args[n-1]);
            p += 2;
          }
        }
      } else {
        char b[2];
        b[0] = *p;
        b[1] = 0;
        strcat(deStringVal + deStringPos, b);
        deStringPos++;
        p++;
      }
    }
    for (i = 0; i < numarg; i++) {
      free(args[i]);
    }
  }
}
#else
void deSprintToString(char* format, ...) {
  va_list ap;
  char buf[1];
  va_start(ap, format);
  uint32 len = vsnprintf(buf, 1, format, ap) + 1;
  va_end(ap);
  if (deStringPos + len >= deStringAllocated) {
    deStringAllocated += (deStringAllocated >> 1) + len;
    utResizeArray(deStringVal, deStringAllocated);
  }
  va_start(ap, format);
  vsnprintf(deStringVal + deStringPos, len, format, ap);
  va_end(ap);
  deStringPos += len - 1;
}
#endif

// Double the size of the temp buffer.
char *deResizeBufferIfNeeded(char *buf, uint32 *len, uint32 pos, uint32 newBytes) {
  if (pos + newBytes >= *len) {
    uint32 newLen = *len + newBytes + (*len >> 1);
    char *newBuf = utMakeString(newLen);
    memcpy(newBuf, buf, *len);
    *len = newLen;
    return newBuf;
  }
  return buf;
}

// Append text to the buffer, resizing if needed.  Append a terminating '\0'.
char *deAppendToBuffer(char *buf, uint32 *len, uint32 *pos, char *text) {
  uint32 textlen = strlen(text);
  buf = deResizeBufferIfNeeded(buf, len, *pos, textlen + 1);
  strcpy(buf + *pos, text);
  *pos += textlen;
  return buf;
}

// Append text to the buffer, resizing if needed.  Append a terminating '\0'.
char *deAppendCharToBuffer(char *buf, uint32 *len, uint32 *pos, char c) {
  buf = deResizeBufferIfNeeded(buf, len, *pos, 2);
  buf[(*pos)++] = c;
  buf[*pos] = '\0';
  return buf;
}

// If there is not room for |len| more bytes on the string, resize it to make
// more room.
static void resizeStringIfNeeded(deString string, uint32 len) {
  uint32 allocated = deStringGetNumText(string);
  uint32 used = deStringGetUsed(string);
  uint32 needed = used + len;
  if (needed > allocated) {
    allocated = needed + (allocated >> 1);
    deStringResizeTexts(string, allocated);
  }
}

// Append text to the end of a deString object.
void deStringPuts(deString string, char *text) {
  uint32 len = strlen(text);
  uint32 used = deStringGetUsed(string);
  resizeStringIfNeeded(string, len);
  memcpy(deStringGetTexts(string) + used, text, len);
  deStringSetUsed(string, used + len);
}

// Sprint to the end of the deString object.
void deStringSprintf(deString string, char *format, ...) {
  char buf[1];
  va_list ap;
  va_start(ap, format);
  uint32 len = vsnprintf(buf, 1, format, ap);
  va_end(ap);
  uint32 used = deStringGetUsed(string);
  resizeStringIfNeeded(string, used + len + 1);
  va_start(ap, format);
  vsnprintf(deStringGetTexts(string) + used, len + 1, format, ap);
  va_end(ap);
  deStringSetUsed(string, used + len);
}

// Write the string to the file.
bool deWriteStringToFile(FILE *file, deString string) {
  uint32 used = deStringGetUsed(string);
  return fwrite(deStringGetTexts(string), sizeof(uint8), used, file) == used;
}

// Return a path string corresponding to the path expression.
char *deGetPathExpressionPath(deExpression pathExpression) {
  if (deExpressionGetType(pathExpression) == DE_EXPR_AS) {
    pathExpression = deExpressionGetFirstExpression(pathExpression);
  }
  if (deExpressionGetType(pathExpression) == DE_EXPR_IDENT) {
    return utSymGetName(deExpressionGetName(pathExpression));
  }
  utAssert(deExpressionGetType(pathExpression) == DE_EXPR_DOT);
  deExpression left = deExpressionGetFirstExpression(pathExpression);
  deExpression right = deExpressionGetNextExpression(left);
  char *path = deGetPathExpressionPath(left);
  return utSprintf("%s/%s", path, utSymGetName(deExpressionGetName(right)));
}

// Append an array format specifier of the form %[<type>].
static char *appendArrayFormatSpec(char *format, uint32 *len, uint32 *pos, deDatatype datatype) {
    format = deResizeBufferIfNeeded(format, len, *pos, 1);
    format[(*pos)++] = '[';
    deDatatype elementType = deDatatypeGetElementType(datatype);
    format = deAppendFormatSpec(format, len, pos, elementType);
    format = deResizeBufferIfNeeded(format, len, *pos, 1);
    format[(*pos)++] = ']';
    return format;
}

// Append a tuple format specifier of the form %(<type1>,<type2>,...).
static char *appendTupleFormatSpec(char *format, uint32 *len, uint32 *pos, deDatatype datatype) {
    format = deResizeBufferIfNeeded(format, len, *pos, 1);
    format[(*pos)++] = '(';
    bool firstTime = true;
    deDatatype elementType;
    deForeachDatatypeTypeList(datatype, elementType) {
      if (!firstTime) {
        format = deResizeBufferIfNeeded(format, len, *pos, 1);
        format[(*pos)++] = ',';
      }
      firstTime = false;
      format = deAppendFormatSpec(format, len, pos, elementType);
    } deEndDatatypeTypeList;
    format = deResizeBufferIfNeeded(format, len, *pos, 1);
    format[(*pos)++] = ')';
  return format;
}

// Append an sprintf format specifier for the datatype
char *deAppendFormatSpec(char *format, uint32 *len, uint32 *pos, deDatatype datatype) {
  deDatatypeType type = deDatatypeGetType(datatype);
  switch (type) {
    case DE_TYPE_BOOL:
      format = deResizeBufferIfNeeded(format, len, *pos, 1);
      format[(*pos)++] = 'b';
      break;
    case DE_TYPE_STRING:
      format = deResizeBufferIfNeeded(format, len, *pos, 1);
      format[(*pos)++] = 's';
      break;
    case DE_TYPE_CLASS:
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_FLOAT:
    case DE_TYPE_ENUM: {
      char formatLetter = type == DE_TYPE_FLOAT? 'f' :
          (type == DE_TYPE_UINT || type == DE_TYPE_CLASS || type == DE_TYPE_ENUM) ? 'u' : 'i';
      char *suffix = utSprintf("%c%u", formatLetter, deDatatypeGetWidth(datatype));
      uint32 stringLen = strlen(suffix);
      format = deResizeBufferIfNeeded(format, len, *pos, stringLen);
      memcpy(format + *pos, suffix, stringLen);
      *pos += stringLen;
      break;
    }
    case DE_TYPE_ARRAY:
      format = appendArrayFormatSpec(format, len, pos, datatype);
      break;
    case DE_TYPE_TUPLE:
    case DE_TYPE_STRUCT:
      format = appendTupleFormatSpec(format, len, pos, datatype);
      break;
    default:
      utExit("Unsupported datatype in print statement");
  }
  return format;
}

// Find the print format for one print argument.
char *deAppendOneFormatElement(char *format, uint32 *len, uint32 *pos,
    deExpression expression) {
  if (deExpressionGetType(expression) == DE_EXPR_STRING) {
    char c;
    deForeachStringText(deExpressionGetString(expression), c) {
      if (c == '\\' || c == '%') {
        // Escape \ and % chars.  All other characters are directly printed.
        format = deResizeBufferIfNeeded(format, len, *pos, 2);
        format[(*pos)++] = '\\';
        format[(*pos)++] = c;
      } else {
        format = deResizeBufferIfNeeded(format, len, *pos, 1);
        format[(*pos)++] = c;
      }
    }
    deEndStringText;
  } else {
    deDatatype datatype = deExpressionGetDatatype(expression);
    if (deExpressionIsType(expression)) {
      char *text = deDatatypeGetTypeString(datatype);
      format = deAppendToBuffer(format, len, pos, text);
    } else {
      format = deResizeBufferIfNeeded(format, len, *pos, 2);
      format[(*pos)++] = '%';
      format = deAppendFormatSpec(format, len, pos, datatype);
    }
  }
  return format;
}

// Generate a format string for the print statement's arguments, compatible with rnSprintf.
deString deFindPrintFormat(deExpression expression) {
  uint32 len = 42;
  uint32 pos = 0;
  char *format = utMakeString(len);
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    format = deAppendOneFormatElement(format, &len, &pos, child);
  } deEndExpressionExpression;
  format = deResizeBufferIfNeeded(format, &len, pos, 1);
  format[pos] = '\0';
  return deCStringCreate(format);
}

// Convert bytes to hexadecimal.
char *deBytesToHex(void *bytes, uint32 len, bool littleEndian) {
  char *buf = utMakeString(len*2 + 1);
  buf[len << 1] = '\0';
  if (len == 0) {
    return buf;
  }
  char *p = buf + (littleEndian? (len - 1)*2 : 0);
  for (uint32 i = 0; i < len; i++) {
    uint8 byte = ((uint8*)bytes)[i];
    p[0] = deToHex(byte >> 4);
    p[1] = deToHex(byte & 0xf);
    p += littleEndian? -2 : 2;
  }
  return buf;
}

// Determine if the identifier conforms to: [-a-zA-Z$._][-a-zA-Z$._0-9]*
bool deIsLegalIdentifier(char *identifier) {
  uint8 *p = (uint8*)identifier;
  uint8 len = strlen(identifier);
  uint8 c = *p++;
  if (!isalpha(c) && c != '_' && c != '$' && c < 0xc0) {
    return false;
  }
  // Skip remaining UTF-8 bytes.
  do {
    c = *p++;
  } while (c >= 0x80 && c <= 0xbf);
  for (uint32 i = 1; i < len; i++) {
    if (!isalnum(c) && !isdigit(c) && c != '_' && c != '$' && c < 0xc0) {
      return false;
    }
    // Skip remaining UTF-8 bytes.
    do {
      c = *p++;
    } while (c >= 0x80 && c <= 0xbf);
  }
  return true;
}

// Convert CamelCase to snake_case.
char *deSnakeCase(char *camelCase) {
  while (*camelCase != '\0' && !isalnum(*camelCase)) {
    camelCase++;
  }
  char *snake_case = utMakeString((strlen(camelCase) << 1) + 1);
  char *p = camelCase;
  char *q = snake_case;
  while (*p != '\0') {
    if (p != camelCase && isupper(*p) && islower(*(p - 1))) {
      *q++ = '_';
    }
    if (isalnum(*p)) {
      *q++ = tolower(*p);
    } else {
      *q++ = '_';
    }
    p++;
  }
  *q = '\0';
  return snake_case;
}

// Convert CamelCase to UPPER_SNAKE_CASE..
char *deUpperSnakeCase(char *camelCase) {
  while (*camelCase != '\0' && !isalnum(*camelCase)) {
    camelCase++;
  }
  char *snake_case = utMakeString((strlen(camelCase) << 1) + 1);
  char *p = camelCase;
  char *q = snake_case;
  while (*p != '\0') {
    if (p != camelCase && isupper(*p) && islower(*(p - 1))) {
      *q++ = '_';
    }
    if (isalnum(*p)) {
      *q++ = toupper(*p);
    } else {
      *q++ = '_';
    }
    p++;
  }
  *q = '\0';
  return snake_case;
}

// Generate a dummy .ll file and exit.
void deGenerateDummyLLFileAndExit(void) {
  if (deLLVMFileName != NULL) {
    fclose(fopen(deLLVMFileName, "w"));
  }
  exit(0);
}

// This is used in error reporting.
char *deGetOldVsNewDatatypeStrings(deDatatype oldDatatype, deDatatype newDatatype) {
  deString oldDatatypeStr = deMutableCStringCreate("");
  deString newDatatypeStr = deMutableCStringCreate("");
  deDumpDatatypeStr(oldDatatypeStr, oldDatatype);
  deDumpDatatypeStr(newDatatypeStr, newDatatype);
  char *s = utSprintf("\n  old: %s\n  new: %s",
          deStringGetCstr(oldDatatypeStr), deStringGetCstr(newDatatypeStr));
  deStringDestroy(oldDatatypeStr);
  deStringDestroy(newDatatypeStr);
  return s;
}

// Print a stack trace showing how the error was created.
void dePrintStack(void) {
  deSignature signature = deCurrentSignature;
  // Nested calls should be shown only once.
  deStatement prevStatement = deStatementNull;
  printf("Stack trace:\n");
  while (signature != deSignatureNull) {
    deFunctionType type = deFunctionGetType(deSignatureGetFunction(signature));
    if (type == DE_FUNC_MODULE) {
      // Module calls are auto-generated, and not of interest for debugging.
      return;
    }
    deStatement statement = deSignatureGetCallStatement(signature);
    if (statement != deStatementNull && statement != prevStatement) {
      deBlock block = deBlockGetScopeBlock(deStatementGetBlock(statement));
      utAssert(deBlockGetType(block) == DE_BLOCK_FUNCTION);
      char *path = deGetBlockPath(block, false);
      printf("In %s: ", path);
      if (!deStatementGenerated(statement)) {
        deDumpLine(deStatementGetLine(statement));
      } else {
        // The fully generated statement can hold more information than the
        // generator line of text.
        printf("generated statement: ");
        deDumpStatement(statement);
      }
    }
    signature = deSignatureGetCallSignature(signature);
    prevStatement = statement;
  }
}
