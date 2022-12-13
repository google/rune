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

// If the identifier contains characters used only in generators, report an error.
// TODO Trojan Source attacks (IdStart IdContinue+, normalized only, no marks, UTX39)
static inline void checkIdentifierChars(char *text) {
  if (deInGenerator || deGenerating || deInIterator) {
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
      deerror("Underscores are not permitted except in code generators");
    } else if (c == '$') {
      deerror("Dollar signs are not permitted except in code generators");
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

// Comment this in to print tokens as they are parsed.
// #define DE_DEBUG

#ifdef DE_DEBUG
#define myDebug utDebug("%u ", deLineNum); utDebug
#else
#define myDebug noDebug
#endif
#undef YYLMAX
#define YYLMAX 4096

static void noDebug(char *foo, ...) {}

%}

%option prefix="de"

%Start comment

%%
<INITIAL>[ \t]*("//".*)?\n      { if (deParenDepth <= 0 && deBracketDepth <= 0) {
                                    myDebug("newline\n");
                                    return '\n';
                                  }
                                  myDebug("Skipped newline\n"); }
<INITIAL>"/*"                   { myDebug("Start block comment...\n");
                                  deCommentDepth = 1;
                                  BEGIN comment; }
<comment>"/*"                   { deCommentDepth++; }
<comment>"*/"                   { myDebug("End block comment\n");
                                  deCommentDepth--;
                                  if (deCommentDepth == 0) {
                                    BEGIN INITIAL;
                                  } }
<comment>.|"\n"                 ;
<INITIAL>"("                    { delval.lineVal = deCurrentLine;
                                  ++deParenDepth;
                                  myDebug("Char (\n"); return '('; }
<INITIAL>")"                    { delval.lineVal = deCurrentLine;
                                  --deParenDepth; myDebug("Char )\n");
                                  return ')'; }
<INITIAL>"["                    { delval.lineVal = deCurrentLine;
                                  ++deBracketDepth;
                                  myDebug("Char [\n"); return '['; }
<INITIAL>"]"                    { --deBracketDepth;
                                  myDebug("Char ]\n");
                                  return ']'; }

<INITIAL>[ \t]+                 ;
<INITIAL>"appendcode" {delval.lineVal = deCurrentLine; myDebug("KWAPPENDCODE\n"); return KWAPPENDCODE; }
<INITIAL>"arrayof" { delval.lineVal = deCurrentLine; myDebug("KWARRAYOF\n"); return KWARRAYOF; }
<INITIAL>"as"      { delval.lineVal = deCurrentLine; myDebug("KWAS\n"); return KWAS; }
<INITIAL>"assert"   { delval.lineVal = deCurrentLine; myDebug("KWASSERT\n"); return KWASSERT; }
<INITIAL>"bool"    { delval.lineVal = deCurrentLine; myDebug("KWBOOL\n"); return KWBOOL; }
<INITIAL>"cascade" { delval.lineVal = deCurrentLine; myDebug("KWCASCADE\n"); return KWCASCADE; }
<INITIAL>"case"    { delval.lineVal = deCurrentLine; myDebug("KWCASE\n"); return KWCASE; }
<INITIAL>"class"   { delval.lineVal = deCurrentLine; myDebug("KWCLASS\n"); return KWCLASS; }
<INITIAL>"debug"   { delval.lineVal = deCurrentLine; myDebug("KWDEBUG\n"); return KWDEBUG; }
<INITIAL>"default" { delval.lineVal = deCurrentLine; myDebug("KWDEFAULT\n"); return KWDEFAULT; }
<INITIAL>"do"      { delval.lineVal = deCurrentLine; myDebug("KWDO\n"); return KWDO; }
<INITIAL>"else"    { delval.lineVal = deCurrentLine; myDebug("KWELSE\n"); return KWELSE; }
<INITIAL>"enum"    { delval.lineVal = deCurrentLine; myDebug("KWENUM\n"); return KWENUM; }
<INITIAL>"export" { delval.lineVal = deCurrentLine; myDebug("KWEXPORT\n"); return KWEXPORT; }
<INITIAL>"exportlib" { delval.lineVal = deCurrentLine; myDebug("KWEXPORTLIB\n"); return KWEXPORTLIB; }
<INITIAL>"rpc"     { delval.lineVal = deCurrentLine; myDebug("KWRPC\n"); return KWRPC; }
<INITIAL>"extern"  { delval.lineVal = deCurrentLine; myDebug("KWEXTERN\n"); return KWEXTERN; }
<INITIAL>"false"                { delval.boolVal = false; myDebug("false\n"); return BOOL; }
<INITIAL>"final"   { delval.lineVal = deCurrentLine; myDebug("KWFINAL\n"); return KWFINAL; }
<INITIAL>"for"     { delval.lineVal = deCurrentLine; myDebug("KWFOR\n"); return KWFOR; }
<INITIAL>"func"    { delval.lineVal = deCurrentLine; myDebug("KWFUNC\n"); return KWFUNC; }
<INITIAL>"generate" { delval.lineVal = deCurrentLine; myDebug("KWGENERATE\n"); return KWGENERATE; }
<INITIAL>"generator" { delval.lineVal = deCurrentLine; myDebug("KWGENERATOR\n"); return KWGENERATOR; }
<INITIAL>"if"      { delval.lineVal = deCurrentLine; myDebug("KWIF\n"); return KWIF; }
<INITIAL>"import"  { delval.lineVal = deCurrentLine; myDebug("KWIMPORT\n"); return KWIMPORT; }
<INITIAL>"importlib" { delval.lineVal = deCurrentLine; myDebug("KWIMPORTLIB\n"); return KWIMPORTLIB; }
<INITIAL>"importrpc" { delval.lineVal = deCurrentLine; myDebug("KWIMPORTRPC\n"); return KWIMPORTRPC; }
<INITIAL>"in"      { delval.lineVal = deCurrentLine; myDebug("KWIN\n"); return KWIN; }
<INITIAL>"isnull"  { delval.lineVal = deCurrentLine; myDebug("KWISNULL\n"); return KWISNULL; }
<INITIAL>"iterator" { delval.lineVal = deCurrentLine; myDebug("KWITERATOR\n"); return KWITERATOR; }
<INITIAL>"mod"    { delval.lineVal = deCurrentLine; myDebug("KWMOD\n"); return KWMOD; }
<INITIAL>"null"    { delval.lineVal = deCurrentLine; myDebug("KWNULL\n"); return KWNULL; }
<INITIAL>"operator" { delval.lineVal = deCurrentLine; myDebug("KWOPERATOR\n"); return KWOPERATOR; }
<INITIAL>"prependcode" { delval.lineVal = deCurrentLine; myDebug("KWPREPENDCODE\n"); return KWPREPENDCODE; }
<INITIAL>"print"   { delval.lineVal = deCurrentLine; myDebug("KWPRINT\n"); return KWPRINT; }
<INITIAL>"println" { delval.lineVal = deCurrentLine; myDebug("KWPRINTLN\n"); return KWPRINTLN; }
<INITIAL>"ref"     { delval.lineVal = deCurrentLine; myDebug("KWREF\n"); return KWREF; }
<INITIAL>"relation" { delval.lineVal = deCurrentLine; myDebug("KWRELATION\n"); return KWRELATION; }
<INITIAL>"return"  { delval.lineVal = deCurrentLine; myDebug("KWRETURN\n"); return KWRETURN; }
<INITIAL>"reveal"  { delval.lineVal = deCurrentLine; myDebug("KWREVEAL\n"); return KWREVEAL; }
<INITIAL>"secret"  { delval.lineVal = deCurrentLine; myDebug("KWSECRET\n"); return KWSECRET; }
<INITIAL>"signed"  { delval.lineVal = deCurrentLine; myDebug("KWSIGNED\n"); return KWSIGNED; }
<INITIAL>"string"  { delval.lineVal = deCurrentLine; myDebug("KWSTRING\n"); return KWSTRING; }
<INITIAL>("struct"|"message")  { delval.lineVal = deCurrentLine; myDebug("KWSTRUCT\n"); return KWSTRUCT; }
<INITIAL>"switch"  { delval.lineVal = deCurrentLine; myDebug("KWSWITCH\n"); return KWSWITCH; }
<INITIAL>"typeswitch"  { delval.lineVal = deCurrentLine; myDebug("KWTYPESWITCH\n"); return KWTYPESWITCH; }
<INITIAL>"throw"   { delval.lineVal = deCurrentLine; myDebug("KWTHROW\n"); return KWTHROW; }
<INITIAL>"true"                 { delval.boolVal = true; myDebug("true\n"); return BOOL; }
<INITIAL>"typeof"  { delval.lineVal = deCurrentLine; myDebug("KWTYPEOF\n"); return KWTYPEOF; }
<INITIAL>"unittest" { delval.lineVal = deCurrentLine; myDebug("KWUNITTEST\n"); return KWUNITTEST; }
<INITIAL>"unref"   { delval.lineVal = deCurrentLine; myDebug("KWUNREF\n"); return KWUNREF; }
<INITIAL>"unsigned" { delval.lineVal = deCurrentLine; myDebug("KWUNSIGNED\n"); return KWUNSIGNED; }
<INITIAL>"use"     { delval.lineVal = deCurrentLine; myDebug("KWUSE\n"); return KWUSE; }
<INITIAL>"var"     { delval.lineVal = deCurrentLine; myDebug("KWVAR\n"); return KWVAR; }
<INITIAL>"while"   { delval.lineVal = deCurrentLine; myDebug("KWWHILE\n"); return KWWHILE; }
<INITIAL>"widthof" { delval.lineVal = deCurrentLine; myDebug("KWWIDTHOF\n"); return KWWIDTHOF; }
<INITIAL>"yield"   { delval.lineVal = deCurrentLine; myDebug("KWYIELD\n"); return KWYIELD; }

<INITIAL>"+="     { delval.lineVal = deCurrentLine; myDebug("KWADDEQUALS\n"); return KWADDEQUALS; }
<INITIAL>"!+="    { delval.lineVal = deCurrentLine; myDebug("KWADDTRUNCEQUALS\n"); return KWADDTRUNCEQUALS; }
<INITIAL>"!+"     { delval.lineVal = deCurrentLine; myDebug("KWADDTRUNC\n"); return KWADDTRUNC; }
<INITIAL>"&&="    { delval.lineVal = deCurrentLine; myDebug("KWANDEQUALS\n"); return KWANDEQUALS; }
<INITIAL>"&="     { delval.lineVal = deCurrentLine; myDebug("KWANDEQUALS\n"); return KWBITANDEQUALS; }
<INITIAL>"&&"     { delval.lineVal = deCurrentLine; myDebug("KWAND\n"); return KWAND; }
<INITIAL>"->"     { delval.lineVal = deCurrentLine; myDebug("KWARROW\n"); return KWARROW; }
<INITIAL>"!<"     { delval.lineVal = deCurrentLine; myDebug("KWCASTTRUNC\n"); return KWCASTTRUNC; }
<INITIAL>"/="     { delval.lineVal = deCurrentLine; myDebug("KWDIVEQUALS\n"); return KWDIVEQUALS; }
<INITIAL>"..."    { delval.lineVal = deCurrentLine; myDebug("KWDOTDOTDOT\n"); return KWDOTDOTDOT; }
<INITIAL>"=="     { delval.lineVal = deCurrentLine; myDebug("KWEQUAL\n"); return KWEQUAL; }
<INITIAL>"**"     { delval.lineVal = deCurrentLine; myDebug("KWEXPEQUALS\n"); return KWEXP; }
<INITIAL>"**="    { delval.lineVal = deCurrentLine; myDebug("KWEXPEQUALS\n"); return KWEXPEQUALS; }
<INITIAL>">="     { delval.lineVal = deCurrentLine; myDebug("KWGE\n"); return KWGE; }
<INITIAL>"<="     { delval.lineVal = deCurrentLine; myDebug("KWLE\n"); return KWLE; }
<INITIAL>"%="     { delval.lineVal = deCurrentLine; myDebug("KWMODEQUALS\n"); return KWMODEQUALS; }
<INITIAL>"*="     { delval.lineVal = deCurrentLine; myDebug("KWMULEQUALS\n"); return KWMULEQUALS; }
<INITIAL>"!*="    { delval.lineVal = deCurrentLine; myDebug("KWMULTRUNCEQUALS\n"); return KWMULTRUNCEQUALS; }
<INITIAL>"!*"     { delval.lineVal = deCurrentLine; myDebug("KWMULTRUNC\n"); return KWMULTRUNC; }
<INITIAL>"!="     { delval.lineVal = deCurrentLine; myDebug("KWNOTEQUAL\n"); return KWNOTEQUAL; }
<INITIAL>"|="     { delval.lineVal = deCurrentLine; myDebug("KWOREQUALS\n"); return KWBITOREQUALS; }
<INITIAL>"||="    { delval.lineVal = deCurrentLine; myDebug("KWOREQUALS\n"); return KWOREQUALS; }
<INITIAL>"||"     { delval.lineVal = deCurrentLine; myDebug("KWOR\n"); return KWOR; }
<INITIAL>"<<<="   { delval.lineVal = deCurrentLine; myDebug("KWROTLEQUALS\n"); return KWROTLEQUALS; }
<INITIAL>"<<<"    { delval.lineVal = deCurrentLine; myDebug("KWROTL\n"); return KWROTL; }
<INITIAL>">>>="   { delval.lineVal = deCurrentLine; myDebug("KWROTREQUALS\n"); return KWROTREQUALS; }
<INITIAL>">>>"    { delval.lineVal = deCurrentLine; myDebug("KWROTR\n"); return KWROTR; }
<INITIAL>"<<="    { delval.lineVal = deCurrentLine; myDebug("KWSHLEQUALS\n"); return KWSHLEQUALS; }
<INITIAL>"<<"     { delval.lineVal = deCurrentLine; myDebug("KWSHL\n"); return KWSHL; }
<INITIAL>">>="    { delval.lineVal = deCurrentLine; myDebug("KWSHREQUALS\n"); return KWSHREQUALS; }
<INITIAL>">>"     { delval.lineVal = deCurrentLine; myDebug("KWSHR\n"); return KWSHR; }
<INITIAL>"-="     { delval.lineVal = deCurrentLine; myDebug("KWSUBEQUALS\n"); return KWSUBEQUALS; }
<INITIAL>"!-="    { delval.lineVal = deCurrentLine; myDebug("KWSUBTRUNCEQUALS\n"); return KWSUBTRUNCEQUALS; }
<INITIAL>"!-"     { delval.lineVal = deCurrentLine; myDebug("KWSUBTRUNC\n"); return KWSUBTRUNC; }
<INITIAL>"^="     { delval.lineVal = deCurrentLine; myDebug("KWXOREQUALS\n"); return KWBITXOREQUALS; }
<INITIAL>"^^="    { delval.lineVal = deCurrentLine; myDebug("KWXOREQUALS\n"); return KWXOREQUALS; }
<INITIAL>"^^"     { delval.lineVal = deCurrentLine; myDebug("KWXOR\n"); return KWXOR; }
<INITIAL>"f32"    { delval.lineVal = deCurrentLine; myDebug("KWF32\n"); return KWF32; }
<INITIAL>"f64"    { delval.lineVal = deCurrentLine; myDebug("KWF64\n"); return KWF64; }

<INITIAL>"'\\a'"  { delval.bigintVal = deUint8BigintCreate(7); myDebug("'\\a'\n"); return INTEGER; }
<INITIAL>"'\\b'"  { delval.bigintVal = deUint8BigintCreate(8); myDebug("'\\b'\n"); return INTEGER; }
<INITIAL>"'\\e'"  { delval.bigintVal = deUint8BigintCreate(0x1b); myDebug("'\\e'\n"); return INTEGER; }
<INITIAL>"'\\f'"  { delval.bigintVal = deUint8BigintCreate(0xc); myDebug("'\\f'\n"); return INTEGER; }
<INITIAL>"'\\n'"  { delval.bigintVal = deUint8BigintCreate(0xa); myDebug("'\\n'\n"); return INTEGER; }
<INITIAL>"'\\r'"  { delval.bigintVal = deUint8BigintCreate(0xd); myDebug("'\\r'\n"); return INTEGER; }
<INITIAL>"'\\t'"  { delval.bigintVal = deUint8BigintCreate(0x9); myDebug("'\\t'\n"); return INTEGER; }
<INITIAL>"'\\v'"  { delval.bigintVal = deUint8BigintCreate(0xb); myDebug("'\\v'\n"); return INTEGER; }

<INITIAL>[0-9]+"e"("-")?[0-9]+"f32" { delval.floatVal = deFloatCreate(DE_FLOAT_SINGLE, safeAtof(detext, 32));
                                  myDebug("%s\n", detext);
                                  return FLOAT; }
<INITIAL>[0-9]+"."("e"("-")?[0-9]+)?"f32" { delval.floatVal = deFloatCreate(DE_FLOAT_SINGLE, safeAtof(detext, 32));
                                  myDebug("%s\n", detext);
                                  return FLOAT; }
<INITIAL>[0-9]*"."[0-9]+("e"("-")?[0-9]+)?"f32" {
                                  delval.floatVal = deFloatCreate(DE_FLOAT_SINGLE, safeAtof(detext, 32));
                                  myDebug("%s\n", detext);
                                  return FLOAT; }
<INITIAL>[0-9]+"e"("-")?[0-9]+("f64")? { delval.floatVal = deFloatCreate(DE_FLOAT_DOUBLE, safeAtof(detext, 64));
                                  myDebug("%s\n", detext);
                                  return FLOAT; }
<INITIAL>[0-9]+"."("e"("-")?[0-9]+)?("f64")? { delval.floatVal = deFloatCreate(DE_FLOAT_DOUBLE, safeAtof(detext, 64));
                                  myDebug("%s\n", detext);
                                  return FLOAT; }
<INITIAL>[0-9]*"."[0-9]+("e"("-")?[0-9]+)?("f64")? {
                                  delval.floatVal = deFloatCreate(DE_FLOAT_DOUBLE, safeAtof(detext, 64));
                                  myDebug("%s\n", detext);
                                  return FLOAT; }
<INITIAL>"'"[ -~]"'"            { delval.bigintVal = deUint8BigintCreate(detext[1]);
                                  myDebug("'%c'\n", detext[1]);
                                  return INTEGER; }
<INITIAL>[0-9]+(("u"|"i")[0-9]+)? { delval.bigintVal = deBigintParse(detext, deCurrentLine);
                                  myDebug("%s\n", detext);
                                  return INTEGER; }
<INITIAL>"0x"[0-9a-fA-F]+(("u"|"i")[0-9]+)? {
                                  delval.bigintVal = deBigintParse(detext, deCurrentLine);
                                  myDebug("%s\n", detext);
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
                                  myDebug("%s\n", detext);
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
                                  myDebug("%s\n", detext);
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
                                  myDebug("%s\n", detext);
                                  return INTTYPE; }
<INITIAL>([_a-zA-Z$]|[\xc0-\xff][\x80-\xbf]*)([a-zA-Z0-9_$]|[\xc0-\xff][\x80-\xbf]*)* {
                                  checkIdentifierChars(detext);
                                  myDebug("IDENT %s\n", detext);
                                  delval.symVal = utSymCreate(detext);
                                  return IDENT; }
<INITIAL>\\[^ \t\n]+            { myDebug("IDENT %s\n", detext);
                                  delval.symVal = utSymCreate(detext);
                                  return IDENT; }
<INITIAL>\"([^"]|\\.)*\"        { myDebug("STRING %s\n", detext);
                                  delval.stringVal = unescape(detext);
                                  return STRING; }
<INITIAL>.                      { delval.lineVal = deCurrentLine;
                                  myDebug("Char '%c'\n", detext[0]);
                                  return detext[0]; }
