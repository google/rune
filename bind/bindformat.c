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

// Verify the expression can be printed.  Return false if we modify the
// expression to make it printable.
static void checkExpressionIsPrintable(deExpression expression) {
  deLine line = deExpressionGetLine(expression);
  deDatatype datatype = deExpressionGetDatatype(expression);
  switch (deDatatypeGetType(datatype)) {
  case DE_TYPE_NONE:
    deError(line, "Printed argument has no type");
    break;
  case DE_TYPE_BOOL:
  case DE_TYPE_STRING:
  case DE_TYPE_UINT:
  case DE_TYPE_INT:
  case DE_TYPE_FLOAT:
  case DE_TYPE_TUPLE:
  case DE_TYPE_STRUCT:
  case DE_TYPE_ENUMCLASS:
  case DE_TYPE_ENUM:
  case DE_TYPE_ARRAY:
  case DE_TYPE_NULL:
  case DE_TYPE_TCLASS:
  case DE_TYPE_CLASS:
    break;
  case DE_TYPE_MODINT:
    utExit("Modint type at top level expression");
    break;
  case DE_TYPE_FUNCTION:
  case DE_TYPE_FUNCPTR:
    deError(line, "Cannot print function pointers");
    break;
  }
}

// Read a uint16 from the string.  Update the string pointer to point to first
// non-digit.  Given an error if the value does not fit in a uint16.
static uint16 readUint16(char **p, char *end, deLine line) {
  if (*p == end) {
    return 0;
  }
  char *q = *p;
  uint32 value = 0;
  char c = *q;
  if (!isdigit(c)) {
    return 0;
  }
  do {
    value = value * 10 + c - '0';
    if (value > UINT16_MAX) {
      deError(line, "Integer width cannot exceed 2^16 - 1");
    }
    c = *++q;
  } while (isdigit(c) && q != end);
  *p = q;
  return value;
}

// Verify the format specifier matches the datatype.
static char* verifyFormatSpecifier(char* p, char *end, deDatatype datatype, deLine line,
    char **buf, uint32 *len, uint32 *pos) {
  if (deDatatypeGetType(datatype) == DE_TYPE_ENUM) {
    deBlock enumBlock = deFunctionGetSubBlock(deDatatypeGetFunction(datatype));
    datatype = deFindEnumIntType(enumBlock);
  } else if (deDatatypeGetType(datatype) == DE_TYPE_CLASS) {
    uint32 width = deDatatypeGetWidth(datatype);
    utAssert(width != 0);
    datatype = deUintDatatypeCreate(width);
  }
  deDatatypeType type = deDatatypeGetType(datatype);
  char c = *p++;
  *buf = deAppendCharToBuffer(*buf, len, pos, c);
  if (c == 's') {
    if (type != DE_TYPE_STRING) {
      deError(line, "Expected String argument");
    }
  } else if (c == 'i' || c == 'u' || c == 'x' || c == 'f') {
    if ((c == 'i' && type != DE_TYPE_INT)) {
      deError(line, "Expected Int argument");
    } else if ((c == 'u' && type != DE_TYPE_UINT)) {
      deError(line, "Expected Uint argument");
    } else if (c == 'x' && type != DE_TYPE_INT && type != DE_TYPE_UINT) {
      deError(line, "Expected Int or Uint argument");
    } else if (c == 'f' && type != DE_TYPE_FLOAT) {
      deError(line, "Expected Int or Uint argument");
    }
    uint32 width = deDatatypeGetWidth(datatype);
    uint16 specWidth = readUint16(&p, end, line);
    if (specWidth != 0 && width != specWidth) {
      deError(line, "Specified width does not match argument");
    }
    *buf = deAppendToBuffer(*buf, len, pos, utSprintf("%u", width));
  } else if (c == 'f') {
    if (type != DE_TYPE_FLOAT) {
      deError(line, "Expected float argument");
    }
  } else if (c == 'b') {
    if (type != DE_TYPE_BOOL) {
      deError(line, "Expected bool argument");
    }
  } else if (c == '[') {
    if (type != DE_TYPE_ARRAY) {
      deError(line, "Expected array argument");
    }
    deDatatype elementType = deDatatypeGetElementType(datatype);
    p = verifyFormatSpecifier(p, end, elementType, line, buf, len, pos);
    c = *p++;
    *buf = deAppendCharToBuffer(*buf, len, pos, c);
    if (c != ']') {
      deError(line, "Expected ']' to end array format specifier");
    }
  } else if (c == '(') {
    if (type != DE_TYPE_TUPLE) {
      deError(line, "Expected tuple argument");
    }
    for (uint32 i = 0; i < deDatatypeGetNumTypeList(datatype); i++) {
      deDatatype elementType = deDatatypeGetiTypeList(datatype, i);
      p = verifyFormatSpecifier(p, end, elementType, line, buf, len, pos);
      if (i + 1 != deDatatypeGetNumTypeList(datatype)) {
        c = *p++;
        *buf = deAppendCharToBuffer(*buf, len, pos, c);
        if (c != ',') {
          deError(line, "Expected ',' between tuple element specifiers.");
        }
      }
    }
    c = *p++;
    *buf = deAppendCharToBuffer(*buf, len, pos, c);
    if (c != ')') {
      deError(line, "Expected ')' to end tuple format specifier");
    }
  } else {
    deError(line, "Unsupported format specifier: %c", c);
  }
  return p;
}

// Verify the printf parameters are valid.  Return false if we need to rebind
// due to modifications to the expression tree.
//
// Currently, we support:
//
//   %b        - Match an bool value: prints true or false
//   %i<width> - Match an Int value
//   %u<width> - Match a Uint value
//   %f        - Match a Float value
//   %s - match a string value
//   %x<width> - Match an Int or Uint value, print in lower-case-hex
//
// Escapes can be \" \\ \n, \t, \a, \b, \e,\f, \r \v, or \xx, where xx is a hex
// encoding of the byte.
//
// Generate a new format specifier that includes widths, since widths are
// optional.
// TODO: Add support for format modifiers, e.g. %12s, %-12s, %$1d, %8d, %08u...
void deVerifyPrintfParameters(deExpression expression) {
  uint32 len = 42;
  uint32 pos = 0;
  char *buf = utMakeString(len);
  deLine line = deExpressionGetLine(expression);
  deExpression format = deExpressionGetFirstExpression(expression);
  deExpression argument = deExpressionGetNextExpression(format);
  bool isTuple = false;
  if (deDatatypeGetType(deExpressionGetDatatype(argument)) == DE_TYPE_TUPLE) {
    isTuple = true;
    argument = deExpressionGetFirstExpression(argument);
  }
  if (deDatatypeGetType(deExpressionGetDatatype(format)) != DE_TYPE_STRING) {
    deError(line, "Format specifier must be a constant string.\n");
  }
  deString string = deExpressionGetString(format);
  char *p = deStringGetText(string);
  char *end = p + deStringGetNumText(string);
  while (p < end) {
    char c = *p++;
    buf = deAppendCharToBuffer(buf, &len, &pos, c);
    if (c == '\\') {
      c = *p++;
      buf = deAppendCharToBuffer(buf, &len, &pos, c);
      if (p >= end) {
        deError(line, "Incomplete escape sequence");
      }
      if (c == 'x') {
        for (uint32 i = 0; i < 2; i++) {
          c = *p++;
          buf = deAppendCharToBuffer(buf, &len, &pos, c);
          if (p >= end) {
            deError(line, "Incomplete escape sequence");
          }
          if (!isxdigit(c)) {
            deError(line, "Invalid hex escape: should be 2 hex digits");
          }
        }
      } else if (c != '\\' && c != '"' && c != 'n' && c != 't' && c != 'a' &&
            c != 'b' && c != 'e' && c != 'f' && c != 'r' && c != 'v') {
        deError(line, "Invalid escape sequence '\\%c'", c);
      }
    } else if (c == '%') {
      if (argument == deExpressionNull) {
        deError(line, "Too few arguments for format");
      }
      checkExpressionIsPrintable(argument);
      deDatatype datatype = deExpressionGetDatatype(argument);
      p = verifyFormatSpecifier(p, end, datatype, line, &buf, &len, &pos);
      if (isTuple) {
        argument = deExpressionGetNextExpression(argument);
      } else {
        argument = deExpressionNull;
      }
    }
  }
  if (argument != deExpressionNull) {
    deError(line, "Too many arguments for format");
  }
  deExpressionSetAltString(format, deMutableCStringCreate(buf));
}

// Add a .toString() method call to the parameter.  Queue the new expression.
static void addToStringCall(deBinding binding, deExpression selfExpr) {
  deLine line = deExpressionGetLine(selfExpr);
  deExpression callExpr = deExpressionCreate(DE_EXPR_CALL, line);
  deExpression listExpr = deExpressionGetExpression(selfExpr);
  deExpressionInsertAfterExpression(listExpr, selfExpr, callExpr);
  deExpressionRemoveExpression(listExpr, selfExpr);
  deExpression identExpr = deIdentExpressionCreate(deToStringSym, line);
  deExpression dotExpr = deBinaryExpressionCreate(DE_EXPR_DOT, selfExpr, identExpr, line);
  deExpressionAppendExpression(callExpr, dotExpr);
  deExpression paramsExpr = deExpressionCreate(DE_EXPR_LIST, line);
  deExpressionAppendExpression(callExpr, paramsExpr);
  deQueueExpression(binding, callExpr, deBindingInstantiated(binding), false);
}

// Convert any class expression we've printed to a toString method call.
// Otherwise, it would just print the integer object reference.
void dePostProcessPrintStatement(deStatement statement) {
  deExpression param;
  deSafeForeachExpressionExpression(deStatementGetExpression(statement), param) {
    if (deDatatypeSecret(deExpressionGetDatatype(param))) {
      deError(deExpressionGetLine(param), "Printing a secret is not allowed");
    }
    checkExpressionIsPrintable(param);
    if (deDatatypeGetType(deExpressionGetDatatype(param)) == DE_TYPE_CLASS) {
      addToStringCall(deStatementGetBinding(statement), param);
    }
  } deEndSafeExpressionExpression;
}
