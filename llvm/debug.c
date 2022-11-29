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

#include "ll.h"

#define LL_NUM_HEADER_TAGS 3
// Max characters needed to print a tag name in a list:
// ", ! <32-bit int> '\0'"
#define LL_MAX_TAG_CHARS (2 + 1 + 10 + 10)

static uint32 llTagNum;

// Forward declarations for recursion.
static llTag createDatatypeTag(deDatatype datatype);
static uint32 findDatatypeSize(deDatatype datatype);

// Create a new debug tag.
static llTag createTag(char *text) {
  llTag tag = llRootFindTag(deTheRoot, text, strlen(text) + 1);
  if (tag != llTagNull) {
    return tag;
  }
  tag = llTagAlloc();
  llTagSetText(tag, text, strlen(text) + 1);
  llTagSetNum(tag, llTagNum);
  llTagNum++;
  llRootAppendTag(deTheRoot, tag);
  return tag;
}

// Create a file tag.  They have the form:
//   !1 = !DIFile(filename: "foo.c", directory: "/home/waywardgeek/rune")
static void createFilepathTag(deFilepath filepath) {
  char *path = deFilepathGetName(filepath);
  char *fileName = utBaseName(path);
  char *dirName = utDirName(path);
  char *text = utSprintf("!DIFile(filename: \"%s\", directory: \"%s\")", fileName, dirName);
  llTag tag = createTag(text);
  // Also set all our package parent filepaths to point to this tag, if they do
  // not already have a tag.  This causes package functions and some globals to
  // be assigned to the first module loaded in a package.
  while (filepath != deFilepathNull && llFilepathGetTag(filepath) == llTagNull) {
    llFilepathSetTag(filepath, tag);
    filepath = deFilepathGetFilepath(filepath);
  }
}

// Generate file tags.  They have the form:
void llCreateFilepathTags(void) {
  llTagNum = LL_NUM_HEADER_TAGS + 1;
  deFilepath filepath;
  deForeachRootFilepath(deTheRoot, filepath) {
    deBlock moduleBlock = deFilepathGetModuleBlock(filepath);
    utAssert(moduleBlock != deBlockNull);
    deFunctionType funcType = deFunctionGetType(deBlockGetOwningFunction(moduleBlock));
    if (funcType == DE_FUNC_MODULE || funcType == DE_FUNC_PACKAGE) {
      createFilepathTag(filepath);
    }
  } deEndRootFilepath;
}
// Create a class/function debug tag.  If |signature| is null, generate the tag
// for main.
uint32 llCreateSignatureTag(deSignature signature) {
  char *text;
  if (signature == deSignatureNull) {
    deFilepath filepath = deRootGetFirstFilepath(deTheRoot);
    llTag fileTag = llFilepathGetTag(filepath);
    text = utSprintf("distinct !DISubprogram(name: \"main\", file: !%u, "
        "line: 1, isLocal: false, isDefinition: true)", llTagGetNum(fileTag));
  } else {
    deBlock block = deSignatureGetBlock(signature);
    deFilepath filepath = deBlockGetFilepath(block);
    llTag fileTag = llFilepathGetTag(filepath);
    char *path = deGetBlockPath(deSignatureGetBlock(signature), false);
    path = llEscapeIdentifier(path);
    text = utSprintf( "distinct !DISubprogram(name: \"%s\", file: !%u, line: %u, "
        "isLocal: true, isDefinition: true)", path, llTagGetNum(fileTag));
  }
  llTag tag = createTag(text);
  return llTagGetNum(tag);
}

// Create a tag for a pointer to the datatype.  A tag must already exist for the datatype.
static llTag createPointerTag(deDatatype datatype) {
  llTag pointerTag = llDatatypeGetPointerTag(datatype);
  if (pointerTag != llTagNull) {
    return pointerTag;
  }
  llTag baseTypeTag = createDatatypeTag(datatype);
  uint32 baseTypeNum = llTagGetNum(baseTypeTag);
  char *text = utSprintf(
      "!DIDerivedType(tag: DW_TAG_pointer_type, baseType: !%u, size: %u)",
      baseTypeNum, llSizeWidth);
  pointerTag = createTag(text);
  llDatatypeSetPointerTag(datatype, pointerTag);
  return pointerTag;
}

// Create a member tag.
// Eg !16 = !DIDerivedType(tag: DW_TAG_member, name: "numElements", baseType: !37, size: 64, offset: 64)
static llTag createMemberTag(char *name, llTag baseTypeTag, uint32 size, uint32 offset) {
  uint32 baseTypeNum = llTagGetNum(baseTypeTag);
  char *text = utSprintf(
      "!DIDerivedType(tag: DW_TAG_member, name: \"%s\", baseType: !%u, size: %u, offset: %u)",
      name, baseTypeNum, size, offset);
  llTag memberTag = createTag(text);
  return memberTag;
}

// Generate an array, bigint, or string type tag.
static llTag createArrayTypeTag(deDatatype datatype) {
  deDatatype elementDatatype;
  if (deDatatypeGetType(datatype) == DE_TYPE_STRING) {
    elementDatatype = deUintDatatypeCreate(8);
  } else if (deDatatypeIsInteger(datatype)) {
    elementDatatype = deUintDatatypeCreate(32);
  } else {
    elementDatatype = deDatatypeGetElementType(datatype);
  }
  llTag elementPtrTag = createPointerTag(elementDatatype);
  llTag dataMemberTag = createMemberTag("data", elementPtrTag, llSizeWidth, 0);
  deDatatype sizetDatatype = deUintDatatypeCreate(llSizeWidth);
  llTag sizeTypeTag = createDatatypeTag(sizetDatatype);
  llTag numElementsTag = createMemberTag("numElements", sizeTypeTag, llSizeWidth, llSizeWidth);
  llTag arrayElements = createTag(utSprintf("!{!%u, !%u}", llTagGetNum(dataMemberTag),
      llTagGetNum(numElementsTag)));
  char *text = utSprintf(
      "distinct !DICompositeType(tag: DW_TAG_structure_type, size: %u, elements: !%u)",
      2*llSizeWidth, llTagGetNum(arrayElements));
  llTag tag = createTag(text);
  llDatatypeSetTag(datatype, tag);
  return tag;
}

// Generate a class type tag as a structure of 
//   !3 = !DIDerivedType(tag: DW_TAG_typedef, name: "BlaType", file: !1, line: 1, baseType: !4)
//   !4 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
static llTag createClassTypeTag(deDatatype datatype) {
  deClass theClass = deDatatypeGetClass(datatype);
  llTag baseTypeTag = createDatatypeTag(deUintDatatypeCreate(deDatatypeGetWidth(datatype)));
  deTclass tclass = deClassGetTclass(theClass);
  deBlock tclassBlock = deFunctionGetSubBlock(deTclassGetFunction(tclass));
  deFilepath filepath = deBlockGetFilepath(tclassBlock);
  deLine line = deBlockGetLine(tclassBlock);
  llTag fileTag = llFilepathGetTag(filepath);
  char *classTypeName = deGetBlockPath(deClassGetSubBlock(theClass), false);
  char *text = utSprintf(
      "!DIDerivedType(tag: DW_TAG_typedef, name: \"class.%s\", file: !%u, line: %u, baseType: !%u)\n",
      classTypeName, llTagGetNum(fileTag), deLineGetLineNum(line), llTagGetNum(baseTypeTag));
  return createTag(text);
}

// Pad |offset| to be a multiple of |size|.
static uint32 padOffset(uint32 offset, uint32 size) {
  if (size == 0) {
    return offset;
  }
  return size*((offset + size - 1)/size);
}

// Find the size of the tuple in bits.  This assumes the compiler pads
// elements to the smallest power-of-2 width that can contain the element.
// It then pads to align the element with this size.
static uint32 findTupleSize(deDatatype datatype) {
  uint32 offset = 0;
  deDatatype elementType;
  deForeachDatatypeTypeList(datatype, elementType) {
    uint32 size = findDatatypeSize(elementType);
    offset = padOffset(offset, size);
    offset += size;
  } deEndDatatypeTypeList;
  return offset;
}

// Return the datatype size.
static uint32 findDatatypeSize(deDatatype datatype) {
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_BOOL:
      return 8;
    case DE_TYPE_STRING:
    case DE_TYPE_ARRAY:
      return 2*llSizeWidth;
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_ENUM:
    case DE_TYPE_FLOAT:
      return deDatatypeGetWidth(datatype);
    case DE_TYPE_TCLASS:
    case DE_TYPE_CLASS:
      return 32;
    case DE_TYPE_FUNCPTR:
      return llSizeWidth;
    case DE_TYPE_TUPLE:
    case DE_TYPE_STRUCT:
      return findTupleSize(datatype);
    default:
      utExit("Unexpected datatype");
  }
  return 0;  // Dummy return.
}

// Crate a tag for a tuple.
static llTag createTupleOrStructTag(deDatatype datatype) {
  char *text;
  uint32 offset = 0;
  llTagArray tags = llTagArrayAlloc();
  deDatatype elementType;
  deForeachDatatypeTypeList(datatype, elementType) {
    llTag elementTag = createDatatypeTag(elementType);
    uint32 size = findDatatypeSize(elementType);
    offset = padOffset(offset, size);
    if (offset == 0) {
      text = utSprintf(
          "!DIDerivedType(tag: DW_TAG_member, baseType: !%u, size: %u)\n",
          llTagGetNum(elementTag), size);
    } else {
      text = utSprintf(
          "!DIDerivedType(tag: DW_TAG_member, baseType: !%u, size: %u, offset: %u)\n",
          llTagGetNum(elementTag), size, offset);
    }
    llTag tag = createTag(text);
    llTagArrayAppendTag(tags, tag);
    offset += size;
  } deEndDatatypeTypeList;
  text = utSprintf(
      "distinct !DICompositeType(tag: DW_TAG_structure_type, size: %u, elements: !{",
      offset);
  bool firstTime = true;
  llTag tag;
  llForeachTagArrayTag(tags, tag) {
    if (firstTime) {
      text = utSprintf("%s!%u", text, llTagGetNum(tag));
    } else {
      text = utSprintf("%s, !%u", text, llTagGetNum(tag));
    }
    firstTime = false;
  } llEndTagArrayTag;
  text = utSprintf("%s})", text);
  llTagArrayFree(tags);
  return createTag(text);
}

// Create an enumerated type tag.  E.g.:
//   !3 = !DICompositeType(tag: DW_TAG_enumeration_type, file: !1, line: 1,
//        baseType: !4, size: 32, elements: !5)
//  !4 = !DIBasicType(name: "unsigned int", size: 32,
//       encoding: DW_ATE_unsigned)
//  !5 = !{!6, !7}
//  !6 = !DIEnumerator(name: "MONDAY", value: 0, isUnsigned: true)
//  !7 = !DIEnumerator(name: "TUESDAY", value: 1, isUnsigned: true)
static llTag createEnumTag(deDatatype datatype) {
  char *text;
  llTagArray tags = llTagArrayAlloc();
  deFunction enumFunc = deDatatypeGetFunction(datatype);
  deBlock enumBlock = deFunctionGetSubBlock(enumFunc);
  deVariable var;
  deForeachBlockVariable(enumBlock, var) {
    text = utSprintf("!DIEnumerator(name: \"%s\", value: %u, isUnsigned: true)",
        deVariableGetName(var), deVariableGetEntryValue(var));
    llTag tag = createTag(text);
    llTagArrayAppendTag(tags, tag);
  } deEndBlockVariable;
  text = "!{";
  bool firstTime = true;
  llTag tag;
  llForeachTagArrayTag(tags, tag) {
    if (firstTime) {
      text = utSprintf("%s!%u", text, llTagGetNum(tag));
    } else {
      text = utSprintf("%s, !%u", text, llTagGetNum(tag));
    }
    firstTime = false;
  } llEndTagArrayTag;
  text = utSprintf("%s}", text);
  llTagArrayFree(tags);
  llTag elementsTag = createTag(text);
  deFilepath filepath = deBlockGetFilepath(enumBlock);
  llTag fileTag = llFilepathGetTag(filepath);
  deDatatype baseDatatype = deVariableGetDatatype(deBlockGetFirstVariable(enumBlock));
  uint32 width = deDatatypeGetWidth(baseDatatype);
  text = utSprintf(
    "!DIBasicType(name: \"u%u\", size: %u, encoding: DW_ATE_unsigned)", width, width);
  llTag baseTypeTag = createTag(text);
  text = utSprintf(
      "!DICompositeType(tag: DW_TAG_enumeration_type, file: !%u, line: %u, "
      "baseType: !%u, size: %u, elements: !%u)", llTagGetNum(fileTag),
      deLineGetLineNum(deFunctionGetLine(enumFunc)), llTagGetNum(baseTypeTag), width,
      llTagGetNum(elementsTag));
  return createTag(text);
}

// Create a tag for a function pointer datatype.  For example:
//   !32 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !8, size: 64)
//   !8 = !DISubroutineType(types: !9)
//   !9 = !{!10, !10, !10}
//   !10 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
static llTag createFuncptrTag(deDatatype datatype) {
  llTag tag = createDatatypeTag(deDatatypeGetReturnType(datatype));
  char *text = utSprintf("!{!%u", llTagGetNum(tag));
  deDatatype elementType;
  deForeachDatatypeTypeList(datatype, elementType) {
    tag = createDatatypeTag(elementType);
    text = utSprintf("%s, !%u", text, llTagGetNum(tag));
  } deEndDatatypeTypeList;
  text = utSprintf("%s}\n", text);
  llTag argsTag = createTag(text);
  llTag funcTypeTag = createTag(utSprintf("!DISubroutineType(types: !%u)", llTagGetNum(argsTag)));
  return createTag(utSprintf(
      "!DIDerivedType(tag: DW_TAG_pointer_type, baseType: !%u, size: %s)",
      llTagGetNum(funcTypeTag), llSize));
}

// Create a type tag for the datatype if it does not already exist.
static llTag createDatatypeTag(deDatatype datatype) {
  llTag tag = llDatatypeGetTag(datatype);
  if (tag != llTagNull) {
    return tag;
  }
  char *text = NULL;
  deDatatypeType type = deDatatypeGetType(datatype);
  if (deDatatypeTypeIsInteger(type) && deDatatypeGetWidth(datatype) > llSizeWidth) {
    return createArrayTypeTag(datatype);
  }
  switch (type) {
    case DE_TYPE_NONE:
      text = "null";
      break;
    case DE_TYPE_BOOL:
      text = "!DIBasicType(name: \"bool\", size: 8, encoding: DW_ATE_boolean)";
      break;
    case DE_TYPE_STRING:
    case DE_TYPE_ARRAY:
      return createArrayTypeTag(datatype);
    case DE_TYPE_UINT: {
      uint32 width = deDatatypeGetWidth(datatype);
      if (width != 8) {
        text = utSprintf(
            "!DIBasicType(name: \"u%u\", size: %u, encoding: DW_ATE_unsigned)",
            width, width);
      } else {
        text = utSprintf(
            "!DIBasicType(name: \"u%u\", size: %u, encoding: "
            "DW_ATE_unsigned_char)",
            width, width);
      }
      break;
    }
    case DE_TYPE_INT: {
      uint32 width = deDatatypeGetWidth(datatype);
      text = utSprintf(
          "!DIBasicType(name: \"i%u\", size: %u, encoding: DW_ATE_signed)",
          width, width);
      break;
    }
    case DE_TYPE_FLOAT: {
      uint32 width = deDatatypeGetWidth(datatype);
      text = utSprintf(
          "!DIBasicType(name: \"float\", size: %u, encoding: DW_ATE_float)",
          width);
      break;
    }
    case DE_TYPE_CLASS:
    case DE_TYPE_TCLASS:
    case DE_TYPE_NULL: {
      if (llDebugMode && type == DE_TYPE_CLASS) {
        return createClassTypeTag(datatype);
      }
      uint32 width = deDatatypeGetWidth(datatype);
      text = utSprintf(
          "!DIBasicType(name: \"object\", size: %u, encoding: DW_ATE_unsigned)", width);
      break;
    }
    case DE_TYPE_FUNCTION:
      utExit("Unexpected function type");
      return llTagNull;  // Dummy return.
    case DE_TYPE_TUPLE:
    case DE_TYPE_STRUCT:
      return createTupleOrStructTag(datatype);
    case DE_TYPE_ENUMCLASS:
    case DE_TYPE_ENUM:
      return createEnumTag(datatype);
    case DE_TYPE_FUNCPTR:
      return createFuncptrTag(datatype);
    case DE_TYPE_MODINT:
      utExit("Unexpected type");
      break;
  }
  return createTag(text);
}

// Create global variable tags.
void llCreateGlobalVariableTags(deBlock block) {
  deVariable globalVar;
  deForeachBlockVariable(block, globalVar) {
    if (deVariableInstantiated(globalVar) && deVariableGetType(globalVar) != DE_VAR_PARAMETER) {
      deDatatype datatype = deVariableGetDatatype(globalVar);
      uint32 datatypeNum = llTagGetNum(createDatatypeTag(datatype));
      deLine line = deVariableGetLine(globalVar);
      uint32 fileNum = llTagGetNum(llFilepathGetTag(deBlockGetFilepath(block)));
      char *globalVarText = utSprintf(
          "distinct !DIGlobalVariable(name: \"%s\", scope: !0, file: !%u, line: %u, "
          "type: !%u, isLocal: false, isDefinition: true)",
          llEscapeText(deVariableGetName(globalVar)), fileNum, line, datatypeNum);
      llTag globalVarTag = createTag(globalVarText);
      char *globalVarExprText = utSprintf(
          "!DIGlobalVariableExpression(var: !%u, expr: !DIExpression())",
          llTagGetNum(globalVarTag));
      llTag tag = createTag(globalVarExprText);
      llVariableSetTag(globalVar, tag);
    }
  } deEndBlockVariable;
}

// Create the type tag for main.  Currently, no arguments are passed.  We'll
// most likely switch to having main declared in the runtime, and have it
// Initialize a global array of strings to the parameters.
static uint32 createMainTypeTag(void) {
  llTag tag = createTag("!DISubroutineType(types: !{null})");
  return llTagGetNum(tag);
}

// Generate tags for the main function and global variables.
llTag llGenerateMainTags(void) {
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  deFilepath filepath = deRootGetFirstFilepath(deTheRoot);
  uint32 fileNum = llTagGetNum(llFilepathGetTag(filepath));
  uint32 typeNum = createMainTypeTag();
  deLine line = deBlockGetLine(rootBlock);
  char *text = utSprintf(
      "distinct !DISubprogram(name: \"main\", scope: !%u, file: !%u, line: %u, type: !%u, "
      "isLocal: false, isDefinition: true, scopeLine: %u, isOptimized: false, unit: !0)",
      fileNum, fileNum, line, typeNum, line);
  llTag tag = createTag(text);
  return tag;
}

// Create the type tag for the signature.
static uint32 createSignatureTypeTag(deSignature signature) {
  deString buf = deMutableStringCreate();
  deStringSprintf(buf, "%s", "!DISubroutineType(types: !{");
  bool mustUseValue = deSignatureIsCalledByFuncptr(signature);
  bool firstTime = true;
  for (uint32 i = 0; i < deSignatureGetNumParamspec(signature); i++) {
    if (mustUseValue || deSignatureParamInstantiated(signature, i)) {
      llTag typeTag = createDatatypeTag(deSignatureGetiType(signature, i));
      if (!firstTime) {
        deStringSprintf(buf, ", !%u", llTagGetNum(typeTag));
      } else {
        deStringSprintf(buf, "!%u", llTagGetNum(typeTag));
      }
      firstTime = false;
    }
  }
  deStringPuts(buf, "})");
  llTag tag = createTag(deStringGetCstr(buf));
  deStringDestroy(buf);
  return llTagGetNum(tag);
}

// Generate tags for the function or constructor.
void llGenerateSignatureTags(deSignature signature) {
  deBlock block = deSignatureGetBlock(signature);
  deFilepath filepath = deBlockGetFilepath(block);
  uint32 fileNum = llTagGetNum(llFilepathGetTag(filepath));
  uint32 typeNum = createSignatureTypeTag(signature);
  deLine line = deBlockGetLine(block);
  char *name = llEscapeText(llPath);
  char *text = utSprintf(
      "distinct !DISubprogram(name: \"%s\", scope: !%u, file: !%u, line: %u, type: !%u, "
      "isLocal: false, isDefinition: true, scopeLine: %u, isOptimized: false, unit: !0)",
      name, fileNum, fileNum, line, typeNum, line);
  llTag tag = createTag(text);
  llSignatureSetTag(signature, tag);
}

// Write the list of globals tag.
static void writeGlobalsTag(uint32 tagNum) {
  llPrintf("!%u = !{", tagNum);
  bool firstTime = true;
  deFunction function;
  deForeachRootFunction(deTheRoot, function) {
    deFunctionType type = deFunctionGetType(function);
    if (type == DE_FUNC_PACKAGE || type == DE_FUNC_MODULE) {
      deBlock block = deFunctionGetSubBlock(function);
      deVariable globalVar;
      deForeachBlockVariable(block, globalVar) {
        if (deVariableInstantiated(globalVar)) {
          if (!firstTime) {
            llPuts(", ");
          }
          firstTime = false;
          llPrintf("!%u", llTagGetNum(llVariableGetTag(globalVar)));
        }
      } deEndBlockVariable;
    }
  }
  deEndRootFunction;
  llPuts("}\n");
}

// Write the fixed header that come at the top.
static void writeTagsHeader(void) {
  llPrintf(
      "!llvm.dbg.cu = !{!0}\n"
      "!llvm.module.flags = !{!1, !2}\n\n"
      "!0 = distinct !DICompileUnit(language: DW_LANG_C99, file: !4, producer: "
      "\"rune (0.0.0)\", isOptimized: false, emissionKind: FullDebug, globals: !3)\n"
      "!1 = !{i32 2, !\"Dwarf Version\", i32 4}\n"
      "!2 = !{i32 2, !\"Debug Info Version\", i32 3}\n");
  writeGlobalsTag(LL_NUM_HEADER_TAGS);
}

// Write debug tags to the output file.
void llWriteDebugTags(void) {
  writeTagsHeader();
  llTag tag;
  llForeachRootTag(deTheRoot, tag) {
    llPrintf("!%u = %s\n", llTagGetNum(tag), llTagGetText(tag));
  } llEndRootTag;
}

// Create a new location tag.
llTag llCreateLocationTag(llTag scopeTag, deLine line) {
  char *text = utSprintf("!DILocation(line: %u, scope: !%u)",
      deLineGetLineNum(line), llTagGetNum(scopeTag));
  return createTag(text);
}

// Output a call to @llvm.dbg.declare to declare a local variable, and generate
// its debug tags.  Call this just after initializing the variable.
void llDeclareLocalVariable(deVariable variable, uint32 argNum) {
  deBlock block = deVariableGetBlock(variable);
  llTag blockTag = llBlockGetTag(block);
  deFilepath filepath = deBlockGetFilepath(block);
  llTag fileTag = llFilepathGetTag(filepath);
  llTag locationTag = llCreateLocationTag(blockTag, deVariableGetLine(variable));
  deDatatype datatype = deVariableGetDatatype(variable);
  llTag typeTag = createDatatypeTag(datatype);
  char *name = llEscapeText(deVariableGetName(variable));
  char *argPos = "";
  if (argNum != 0) {
    argPos = utSprintf(", arg: %u", argNum);
  }
  char *localVarText = utSprintf(
      "!DILocalVariable(name: \"%s\"%s, scope: !%u, file: !%u, line: %u, type: !%u)",
      name, argPos, llTagGetNum(blockTag), llTagGetNum(fileTag),
      deVariableGetLine(variable), llTagGetNum(typeTag));
  llTag localVarTag = createTag(localVarText);
  if (deVariableGetType(variable) == DE_VAR_PARAMETER) {
    char *suffix = "";
    if (!deVariableConst(variable) || llDatatypePassedByReference(datatype)) {
      suffix = "*";
    }
    llDeclareRuntimeFunction("llvm.dbg.value");
    llPrintf(
        "  call void @llvm.dbg.value(metadata %s%s %s, metadata !%u, "
        "metadata !DIExpression()), !dbg !%u\n",
        llGetTypeString(datatype, true), suffix, llGetVariableName(variable),
        llTagGetNum(localVarTag), llTagGetNum(locationTag));
  } else {
    llDeclareRuntimeFunction("llvm.dbg.declare");
    llPrintf(
        "  call void @llvm.dbg.declare(metadata %s* %s, metadata !%u, "
        "metadata !DIExpression()), !dbg !%u\n",
        llGetTypeString(datatype, true), llGetVariableName(variable),
        llTagGetNum(localVarTag), llTagGetNum(locationTag));
  }
}
