%{

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

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parse.h"
#include "deparse.h"

#define YY_NEVER_INTERACTIVE 1
#define YY_INPUT(buf,result,max_size) \
    if((result = deRead((char *) buf, max_size )) < 0 ) \
        YY_FATAL_ERROR( "input in flex scanner failed" );

// Determine if the character is an invalid control character.
static inline bool isInvalidControlChar(int c) {
  return c < ' ' && c != '\n' && c != '\t';
}

// Read an entire line at once.  If EOF is reached, append a '\n'.
static int deRead(char *buf, int maxSize) {
  char *p = buf;
  int numChars = 0;
  int c;
  if (deFile != NULL) {
    do {
      c = getc(deFile);
      if (c == '\r') {
        c = getc(deFile);
        if (c != '\n') {
          deerror("Invalid control character");
        }
      }
      // defend against Trojan source attacks.
      if (c == 0xE2) {
        *p++ = c;
        numChars++;
        c = getc(deFile);
        if (c == 0x80) {
          *p++ = c;
          numChars++;
          c = getc(deFile);
          if ((c >= 0xAA && c <= 0xAE) || c == 0x8B) {
            deerror("Invalid control character");
          }
        }
        if (c == 0x81) {
          *p++ = c;
          numChars++;
          c = getc(deFile);
          if (c >= 0xA6 && c <= 0xA9) {
            deerror("Invalid control character");
          }
        }
      }
      if (c != EOF) {
        if (isInvalidControlChar(c)) {
          deerror("Invalid control character");
        }
        *p++ = c;
        numChars++;
      }
    } while (c != '\n' && numChars < maxSize - 1 && c != EOF);
  } else {
    // We're parsing from an input string in deInputString.
    do {
      c = deInputString[deInputStringPos++];
      if (c == '\0') {
        c = EOF;
        --deInputStringPos;
      }
      if (c == '\r') {
        c = deInputString[deInputStringPos++];
        if (c != '\n') {
          deerror("Invalid control character");
        }
      }
      // defend against Trojan source attacks.
      if (c == 0xE2) {
        *p++ = c;
        numChars++;
        c = getc(deFile);
        if (c == 0x80) {
          *p++ = c;
          numChars++;
          c = getc(deFile);
          if ((c >= 0xAA && c <= 0xAE) || c == 0x8B) {
            deerror("Invalid control character");
          }
        }
        if (c == 0x81) {
          *p++ = c;
          numChars++;
          c = getc(deFile);
          if (c >= 0xA6 && c <= 0xA9) {
            deerror("Invalid control character");
          }
        }
      }
      if (c != EOF) {
        if (isInvalidControlChar(c)) {
          deerror("Invalid control character");
        }
        *p++ = c;
        numChars++;
      }
    } while (c != '\n' && numChars < maxSize - 1 && c != EOF);
  }
  if (c == EOF && !deReachedEndOfFile) {
    // Add a newline at the end of the file.
    deReachedEndOfFile = true;
    *p = '\n';
    numChars++;
  }
  deCurrentLine = deLineCreate(deCurrentFilepath, buf, numChars, deLineNum);
  deLineNum++;
  return numChars;
}

// Convert a hex digit to a 4-bit value.
static char hexDigit(char c) {
  c = tolower(c);
  return c >= 'a'? 10 + c - 'a' : c - '0';
}

// Convert two hex characters to a char.
static char hexToChar(char hi, char low) {
  return (hexDigit(hi) << 4) | hexDigit(low);
}

static deString unescape(char *string) {
  char *buffer = utMakeString(strlen(string));
  char *p = buffer;
  string++; // Skip "
  while (*string != '\0') {
    if (*string == '\\') {
      string++;
      if (*string == 'a') {
        *p++ = '\x07';
        string++;
      } else if (*string == 'b') {
        *p++ = '\x08';
        string++;
      } else if (*string == 'e') {
        *p++ = '\x1b';
        string++;
      } else if (*string == 'f') {
        *p++ = '\x0c';
        string++;
      } else if (*string == 'n') {
        *p++ = '\n';
        string++;
      } else if (*string == 'r') {
        *p++ = '\r';
        string++;
      } else if (*string == 't') {
        *p++ = '\t';
        string++;
      } else if (*string == 'v') {
        *p++ = '\x0b';
        string++;
      } else if (*string == 'x') {
        string++;
        if (!isxdigit(string[0]) || !isxdigit(string[1])) {
          deerror("Invalid hex escape sequence");
        }
        *p++ = hexToChar(string[0], string[1]);
        string += 2;
      } else {
        *p++ = *string++;
        if (*string == '\0') {
          deerror("Cannot escape terminating quote.");
        }
      }
    } else {
      *p++ = *string++;
    }
  }
  *--p = '\0'; // Overwrite terminating "
  deString text = deMutableCStringCreate(buffer);
  return text;
}

// If the identifier contains characters used only in transformers, report an error.
// TODO Trojan Source attacks (IdStart IdContinue+, normalized only, no marks, UTX39)
static inline void checkIdentifierChars(char *text) {
  if (deInTransformer || deGenerating || deInIterator) {
    return;
  }
  char *p = text;
  if (*p == '_') {
    // Allow a leading _ to indicated a private identifier
    p++;
  }
  while (*p != '\0') {
    char c = *p++;
    if (c == '_') {
      deerror("Underscores are not permitted except in code transformers");
    } else if (c == '$') {
      deerror("Dollar signs are not permitted except in code transformers");
    }
  }
}

// Call strtod and make sure that the conversion did not overflow.
static double safeAtof(char *text, uint32 width) {
  if (width == 32) {
    float floatVal = strtof(text, NULL);
    if (floatVal == HUGE_VALF) {
      deerror("Floating point value %s does not fit in a 32-bit float.", text);
    }
    return floatVal;
  } else if (width != 64) {
    utExit("Unsupported floating point width: %u", width);
  }
  double doubleVal = strtod(text, NULL);
  if (doubleVal == HUGE_VAL) {
    deerror("Floating point value %s does not fit in a 64-bit float.", text);
  }
  return doubleVal;
}

int dewrap(void) {
    return 1;
}

#undef YYLMAX
#define YYLMAX 4096

#define retToken(token) delval.lineVal = deCurrentLine; logMsg(#token "\n"); return token

static void logMsg(char *format, ...) {
  if (!deLogTokens) {
    return;
  }
  char *buf;
  va_list ap;
  va_start(ap, format);
  buf = utVsprintf(format, ap);
  va_end(ap);
  utDebug("%s", buf);
}

%}

%option prefix="de"

%Start comment

%%
<INITIAL>[ \t]*("//".*)?\n      { if (deParenDepth <= 0 && deBracketDepth <= 0) {
                                    logMsg("newline\n");
                                    return '\n';
                                  }
                                  logMsg("Skipped newline\n"); }
<INITIAL>"/*"                   { logMsg("Start block comment...\n");
                                  deCommentDepth = 1;
                                  BEGIN comment; }
<comment>"/*"                   { deCommentDepth++; }
<comment>"*/"                   { logMsg("End block comment\n");
                                  deCommentDepth--;
                                  if (deCommentDepth == 0) {
                                    BEGIN INITIAL;
                                  } }
<comment>.|"\n"                 ;
<INITIAL>"("                    { delval.lineVal = deCurrentLine;
                                  ++deParenDepth;
                                  logMsg("Char (\n"); return '('; }
<INITIAL>")"                    { delval.lineVal = deCurrentLine;
                                  --deParenDepth; logMsg("Char )\n");
                                  return ')'; }
<INITIAL>"["                    { delval.lineVal = deCurrentLine;
                                  ++deBracketDepth;
                                  logMsg("Char [\n"); return '['; }
<INITIAL>"]"                    { --deBracketDepth;
                                  logMsg("Char ]\n");
                                  return ']'; }

<INITIAL>[ \t]+                 ;
<INITIAL>"appendcode"           { retToken(KWAPPENDCODE); }
<INITIAL>"arrayof"              { retToken(KWARRAYOF); }
<INITIAL>"as"                   { retToken(KWAS); }
<INITIAL>"assert"               { retToken(KWASSERT); }
<INITIAL>"bool"                 { retToken(KWBOOL); }
<INITIAL>"cascade"              { retToken(KWCASCADE); }
<INITIAL>"class"                { retToken(KWCLASS); }
<INITIAL>"debug"                { retToken(KWDEBUG); }
<INITIAL>"default"              { retToken(KWDEFAULT); }
<INITIAL>"do"                   { retToken(KWDO); }
<INITIAL>"else"                 { retToken(KWELSE); }
<INITIAL>"enum"                 { retToken(KWENUM); }
<INITIAL>"export"               { retToken(KWEXPORT); }
<INITIAL>"exportlib"            { retToken(KWEXPORTLIB); }
<INITIAL>"rpc"                  { retToken(KWRPC); }
<INITIAL>"extern"               { retToken(KWEXTERN); }
<INITIAL>"false"                { delval.boolVal = false; logMsg("false\n"); return BOOL; }
<INITIAL>"final"                { retToken(KWFINAL); }
<INITIAL>"for"                  { retToken(KWFOR); }
<INITIAL>"func"                 { retToken(KWFUNC); }
<INITIAL>"transform"            { retToken(KWTRANSFORM); }
<INITIAL>"transformer"          { retToken(KWTRANSFORMER); }
<INITIAL>"if"                   { retToken(KWIF); }
<INITIAL>"import"               { retToken(KWIMPORT); }
<INITIAL>"importlib"            { retToken(KWIMPORTLIB); }
<INITIAL>"importrpc"            { retToken(KWIMPORTRPC); }
<INITIAL>"in"                   { retToken(KWIN); }
<INITIAL>"isnull"               { retToken(KWISNULL); }
<INITIAL>"iterator"             { retToken(KWITERATOR); }
<INITIAL>"mod"                  { retToken(KWMOD); }
<INITIAL>"null"                 { retToken(KWNULL); }
<INITIAL>"operator"             { retToken(KWOPERATOR); }
<INITIAL>"prependcode"          { retToken(KWPREPENDCODE); }
<INITIAL>"print"                { retToken(KWPRINT); }
<INITIAL>"println"              { retToken(KWPRINTLN); }
<INITIAL>"ref"                  { retToken(KWREF); }
<INITIAL>"relation"             { retToken(KWRELATION); }
<INITIAL>"return"               { retToken(KWRETURN); }
<INITIAL>"reveal"               { retToken(KWREVEAL); }
<INITIAL>"secret"               { retToken(KWSECRET); }
<INITIAL>"signed"               { retToken(KWSIGNED); }
<INITIAL>"string"               { retToken(KWSTRING); }
<INITIAL>("struct"|"message")   { retToken(KWSTRUCT); }
<INITIAL>"switch"               { retToken(KWSWITCH); }
<INITIAL>"typeswitch"           { retToken(KWTYPESWITCH); }
<INITIAL>"try"                  { retToken(KWTRY); }
<INITIAL>"catch"                { retToken(KWCATCH); }
<INITIAL>"throw"                { retToken(KWTHROW); }
<INITIAL>"true"                 { delval.boolVal = true; logMsg("true\n"); return BOOL; }
<INITIAL>"typeof"               { retToken(KWTYPEOF); }
<INITIAL>"unittest"             { retToken(KWUNITTEST); }
<INITIAL>"unref"                { retToken(KWUNREF); }
<INITIAL>"unsigned"             { retToken(KWUNSIGNED); }
<INITIAL>"use"                  { retToken(KWUSE); }
<INITIAL>"var"                  { retToken(KWVAR); }
<INITIAL>"while"                { retToken(KWWHILE); }
<INITIAL>"widthof"              { retToken(KWWIDTHOF); }
<INITIAL>"yield"                { retToken(KWYIELD); }

<INITIAL>"+="                   { retToken(KWADDEQUALS); }
<INITIAL>"!+="                  { retToken(KWADDTRUNCEQUALS); }
<INITIAL>"!+"                   { retToken(KWADDTRUNC); }
<INITIAL>"&&="                  { retToken(KWANDEQUALS); }
<INITIAL>"&="                   { retToken(KWBITANDEQUALS); }
<INITIAL>"&&"                   { retToken(KWAND); }
<INITIAL>"->"                   { retToken(KWARROW); }
<INITIAL>"=>"                   { retToken(KWIMPLIES); }
<INITIAL>"!<"                   { retToken(KWCASTTRUNC); }
<INITIAL>"/="                   { retToken(KWDIVEQUALS); }
<INITIAL>"..."                  { retToken(KWDOTDOTDOT); }
<INITIAL>"=="                   { retToken(KWEQUAL); }
<INITIAL>"**"                   { retToken(KWEXP); }
<INITIAL>"**="                  { retToken(KWEXPEQUALS); }
<INITIAL>">="                   { retToken(KWGE); }
<INITIAL>"<="                   { retToken(KWLE); }
<INITIAL>"%="                   { retToken(KWMODEQUALS); }
<INITIAL>"*="                   { retToken(KWMULEQUALS); }
<INITIAL>"!*="                  { retToken(KWMULTRUNCEQUALS); }
<INITIAL>"!*"                   { retToken(KWMULTRUNC); }
<INITIAL>"!="                   { retToken(KWNOTEQUAL); }
<INITIAL>"|="                   { retToken(KWBITOREQUALS); }
<INITIAL>"||="                  { retToken(KWOREQUALS); }
<INITIAL>"||"                   { retToken(KWOR); }
<INITIAL>"<<<="                 { retToken(KWROTLEQUALS); }
<INITIAL>"<<<"                  { retToken(KWROTL); }
<INITIAL>">>>="                 { retToken(KWROTREQUALS); }
<INITIAL>">>>"                  { retToken(KWROTR); }
<INITIAL>"<<="                  { retToken(KWSHLEQUALS); }
<INITIAL>"<<"                   { retToken(KWSHL); }
<INITIAL>">>="                  { retToken(KWSHREQUALS); }
<INITIAL>">>"                   { retToken(KWSHR); }
<INITIAL>"-="                   { retToken(KWSUBEQUALS); }
<INITIAL>"!-="                  { retToken(KWSUBTRUNCEQUALS); }
<INITIAL>"!-"                   { retToken(KWSUBTRUNC); }
<INITIAL>"^="                   { retToken(KWBITXOREQUALS); }
<INITIAL>"^^="                  { retToken(KWXOREQUALS); }
<INITIAL>"^^"                   { retToken(KWXOR); }
<INITIAL>"f32"                  { retToken(KWF32); }
<INITIAL>"f64"                  { retToken(KWF64); }

<INITIAL>"'\\a'"  { delval.bigintVal = deUint8BigintCreate(7); logMsg("'\\a'\n"); return INTEGER; }
<INITIAL>"'\\b'"  { delval.bigintVal = deUint8BigintCreate(8); logMsg("'\\b'\n"); return INTEGER; }
<INITIAL>"'\\e'"  { delval.bigintVal = deUint8BigintCreate(0x1b); logMsg("'\\e'\n"); return INTEGER; }
<INITIAL>"'\\f'"  { delval.bigintVal = deUint8BigintCreate(0xc); logMsg("'\\f'\n"); return INTEGER; }
<INITIAL>"'\\n'"  { delval.bigintVal = deUint8BigintCreate(0xa); logMsg("'\\n'\n"); return INTEGER; }
<INITIAL>"'\\r'"  { delval.bigintVal = deUint8BigintCreate(0xd); logMsg("'\\r'\n"); return INTEGER; }
<INITIAL>"'\\t'"  { delval.bigintVal = deUint8BigintCreate(0x9); logMsg("'\\t'\n"); return INTEGER; }
<INITIAL>"'\\v'"  { delval.bigintVal = deUint8BigintCreate(0xb); logMsg("'\\v'\n"); return INTEGER; }

<INITIAL>[0-9]+"e"("-")?[0-9]+"f32" { delval.floatVal = deFloatCreate(DE_FLOAT_SINGLE, safeAtof(detext, 32));
                                  logMsg("%s\n", detext);
                                  return FLOAT; }
<INITIAL>[0-9]+"."("e"("-")?[0-9]+)?"f32" { delval.floatVal = deFloatCreate(DE_FLOAT_SINGLE, safeAtof(detext, 32));
                                  logMsg("%s\n", detext);
                                  return FLOAT; }
<INITIAL>[0-9]*"."[0-9]+("e"("-")?[0-9]+)?"f32" {
                                  delval.floatVal = deFloatCreate(DE_FLOAT_SINGLE, safeAtof(detext, 32));
                                  logMsg("%s\n", detext);
                                  return FLOAT; }
<INITIAL>[0-9]+"e"("-")?[0-9]+("f64")? { delval.floatVal = deFloatCreate(DE_FLOAT_DOUBLE, safeAtof(detext, 64));
                                  logMsg("%s\n", detext);
                                  return FLOAT; }
<INITIAL>[0-9]+"."("e"("-")?[0-9]+)?("f64")? { delval.floatVal = deFloatCreate(DE_FLOAT_DOUBLE, safeAtof(detext, 64));
                                  logMsg("%s\n", detext);
                                  return FLOAT; }
<INITIAL>[0-9]*"."[0-9]+("e"("-")?[0-9]+)?("f64")? {
                                  delval.floatVal = deFloatCreate(DE_FLOAT_DOUBLE, safeAtof(detext, 64));
                                  logMsg("%s\n", detext);
                                  return FLOAT; }
<INITIAL>"'"[ -~]"'"            { delval.bigintVal = deUint8BigintCreate(detext[1]);
                                  logMsg("'%c'\n", detext[1]);
                                  return INTEGER; }
<INITIAL>[0-9]+(("u"|"i")[0-9]+)? { delval.bigintVal = deBigintParse(detext, deCurrentLine);
                                  logMsg("%s\n", detext);
                                  return INTEGER; }
<INITIAL>"0x"[0-9a-fA-F]+(("u"|"i")[0-9]+)? {
                                  delval.bigintVal = deBigintParse(detext, deCurrentLine);
                                  logMsg("%s\n", detext);
                                  return INTEGER; }
<INITIAL>"rand"[0-9]+           { char *end;
                                  uint32 width = strtol(detext + 4, &end, 10);
                                  if (*end != '\0' || width > UINT16_MAX) {
                                    deerror("Random integer is too large");
                                  }
                                  if (width == 0) {
                                    deerror("Zero-width integers are not allowed");
                                  }
                                  delval.uint16Val = width;
                                  logMsg("%s\n", detext);
                                  return RANDUINT; }
<INITIAL>"u"[0-9]+              { char *end;
                                  uint32 width = strtol(detext + 1, &end, 10);
                                  if (*end != '\0' || width > UINT16_MAX) {
                                    deerror("Random integer is too large");
                                  }
                                  if (width == 0) {
                                    deerror("Zero-width integers are not allowed");
                                  }
                                  delval.uint16Val = width;
                                  logMsg("%s\n", detext);
                                  return UINTTYPE; }
<INITIAL>"i"[0-9]+              { char *end;
                                  uint32 width = strtol(detext + 1, &end, 10);
                                  if (*end != '\0' || width > UINT16_MAX) {
                                    deerror("Random integer is too large");
                                  }
                                  if (width == 0) {
                                    deerror("Zero-width integers are not allowed");
                                  }
                                  delval.uint16Val = width;
                                  logMsg("%s\n", detext);
                                  return INTTYPE; }
<INITIAL>([_a-zA-Z$]|[\xc0-\xff][\x80-\xbf]*)([a-zA-Z0-9_$]|[\xc0-\xff][\x80-\xbf]*)* {
                                  checkIdentifierChars(detext);
                                  logMsg("IDENT %s\n", detext);
                                  delval.symVal = utSymCreate(detext);
                                  return IDENT; }
<INITIAL>\\[^ \t\n]+            { logMsg("IDENT %s\n", detext);
                                  delval.symVal = utSymCreate(detext);
                                  return IDENT; }
<INITIAL>\"([^"]|\\.)*\"        { logMsg("STRING %s\n", detext);
                                  delval.stringVal = unescape(detext);
                                  return STRING; }
<INITIAL>.                      { delval.lineVal = deCurrentLine;
                                  logMsg("Char '%c'\n", detext[0]);
                                  return detext[0]; }
