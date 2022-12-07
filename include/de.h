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

#ifndef EXPERIMENTAL_WAYWARDGEEK_RUNE_DE_H_
#define EXPERIMENTAL_WAYWARDGEEK_RUNE_DE_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef MAKEFILE_BUILD
#include "dedatabase.h"
#else
#include "third_party/rune/database/dedatabase.h"
#endif

// The root object.
extern deRoot deTheRoot;

// Main functions.
void deStart(char *fileName);
void deStop(void);
void deRunGenerators(void);
deValue deEvaluateExpression(deBlock scopeBlock, deExpression expression, deBigint modulus);
void deAddMemoryManagement(void);
void deParseBuiltinFunctions(void);
deBlock deParseModule(char *fileName, deBlock destPackageBlock, bool isMainModule);
void deParseString(char *string, deBlock currentBlock);
deStatement deInlineIterator(deBlock scopeBlock, deStatement statement);
void deConstantPropagation(deBlock scopeBlock, deBlock block);
void deInstantiateRelation(deStatement statement);
void deAnalyzeRechability(deBlock block);

// RPC functions.
bool deEncodeTextRpc(char *dataType, char *text, deString *publicData, deString *secretData);
deString deDecodeTextRpc(char *dataType, uint8 *publicData, uint32 publicLen,
    uint8 *secretData, uint32 secretLen);
bool deEncodeRequest(char *protoFileName, char *method, char *textRequest,
    deString *publicData, deString *secretData);
deString deDecodeResponse(char *protoFileName, char *method, uint8 *publicData,
    uint32 publicLen, uint8 *secretData, uint32 secretLen);
void deBindRPCs(void);

// Binding functions.
void deBindStart(void);
void deBind(void);
void deBindNewStatement(deBlock scopeBlock, deStatement statement);
void deBindBlock(deBlock block, deSignature signature, bool inlineIterators);
void deBindExpression(deBlock scopeBlock, deExpression expression);
void deApplySignatureBindings(deSignature signature);

// New event-driven binding funcions.
void deBind2(void);
bool deBindExpression2(deSignature scopeSig, deBinding binding);
void deBindStatement2(deStateBinding statebinding);
void deQueueEventBlockedStateBindings(deEvent event);
void deQueueSignature(deSignature signature);

// Block methods.
deBlock deBlockCreate(deFilepath filepath, deBlockType type, deLine line);
void deDumpBlock(deBlock block);
void deDumpBlockStr(deString string, deBlock block);
deBlock deBlockGetOwningBlock(deBlock block);
deBlock deBlockGetScopeBlock(deBlock block);
void deAppendBlockToBlock(deBlock sourceBlock, deBlock destBlock);
void dePrependBlockToBlock(deBlock sourceBlock, deBlock destBlock);
void deCopyFunctionIdentsToBlock(deBlock sourceBlock, deBlock destBlock);
deBlock deCopyBlock(deBlock block);
deBlock deSaveBlockSnapshot(deBlock block);
void deRestoreBlockSnapshot(deBlock block, deBlock snapshot);
void deCopyBlockStatementsAfterStatement(deBlock block, deStatement destStatement);
void deMoveBlockStatementsAfterStatement(deBlock block, deStatement destStatement);
uint32 deBlockCountParameterVariables(deBlock block);
void deResolveBlockVariableNameConfligts(deBlock newBlock, deBlock oldBlock);
void deRestoreBlockVariableNames(deBlock block);
utSym deBlockCreateUniqueName(deBlock scopeBlock, utSym name);
bool deBlockIsUserGenerated(deBlock scopeBlock);
static inline bool deBlockIsConstructor(deBlock block) {
  return deBlockGetType(block) == DE_BLOCK_FUNCTION &&
      deFunctionGetType(deBlockGetOwningFunction(block)) == DE_FUNC_CONSTRUCTOR;
}
static inline bool deBlockIsDestructor(deBlock block) {
  return deBlockGetType(block) == DE_BLOCK_FUNCTION &&
      deFunctionGetType(deBlockGetOwningFunction(block)) == DE_FUNC_DESTRUCTOR;
}
static inline deVariable deBlockIndexVariable(deBlock block, uint32 index) {
  deVariable variable;
  uint32 i = 0;
  deForeachBlockVariable(block, variable) {
    if (i == index) {
      return variable;
    }
    i++;
  } deEndBlockVariable;
  utExit("Indexed past end of block variables");
  return deVariableNull;
}

// Function methods.
deFunction deFunctionCreate(deFilepath filepath, deBlock block,
                            deFunctionType type, utSym name, deLinkage linkage,
                            deLine line);
deFunction deIteratorFunctionCreate(deBlock block, utSym name, utSym selfName,
                                    deLinkage linkage, deLine line);
deFunction deOperatorFunctionCreate(deBlock block, deExpressionType opType,
                                    deLine line);
char *deGetFunctionTypeName(deFunctionType type);
void deDumpFunction(deFunction function);
void deDumpFunctionStr(deString string, deFunction function);
deFunction deCopyFunction(deFunction function, deBlock destBlock);
void deInsertModuleInitializationCall(deFunction moduleFunc);
void deFunctionPrependFunctionCall(deFunction function, deFunction childFunction);
void deFunctionAppendFunctionCall(deFunction function, deFunction childFunction);
char *deFunctionGetName(deFunction function);
static inline utSym deFunctionGetSym(deFunction function) {
  return deIdentGetSym(deFunctionGetFirstIdent(function));
}
static inline bool deFunctionBuiltin(deFunction function) {
  return deFunctionGetLinkage(function) == DE_LINK_BUILTIN;
}
static inline bool deFunctionExported(deFunction function) {
  deLinkage linkage = deFunctionGetLinkage(function);
  return linkage == DE_LINK_LIBCALL || linkage == DE_LINK_RPC || linkage == DE_LINK_EXTERN_C;
}
static inline bool deFunctionIsRpc(deFunction function) {
  deLinkage linkage = deFunctionGetLinkage(function);
  return linkage == DE_LINK_RPC || linkage == DE_LINK_EXTERN_RPC;
}

// Tclass and Class methods.
deTclass deTclassCreate(deFunction constructor, uint32 refWidth, deLine line);
void deDumpTclass(deTclass tclass);
void deDumpTclassStr(deString string, deTclass tclass);
deClass deClassCreate(deTclass tclass, deSignature signature);
deClass deTclassGetDefaultClass(deTclass tclass);
deTclass deCopyTclass(deTclass tclass, deFunction destConstructor);
deFunction deGenerateDefaultToStringMethod(deClass theClass);
deFunction deGenerateDefaultShowMethod(deClass theClass);
deFunction deClassFindMethod(deClass theClass, utSym methodSym);
void deDestroyTclassContents(deTclass tclass);
static inline utSym deTclassGetSym(deTclass tclass) {
  return deFunctionGetSym(deTclassGetFunction(tclass));
}
static inline char *deTclassGetName(deTclass tclass) {
  return deFunctionGetName(deTclassGetFunction(tclass));
}
static inline bool deTclassBuiltin(deTclass tclass) {
  return deFunctionBuiltin(deTclassGetFunction(tclass));
}

// Generator methods.
deGenerator deGeneratorCreate(deBlock block, utSym name, deLine line);
void deExecuteRelationStatement(deStatement statement);
void deDumpGenerator(deGenerator generator);
void deDumpGeneratorStr(deString string, deGenerator generator);
static inline utSym deGeneratorGetSym(deGenerator generator) {
  return deIdentGetSym(deFunctionGetFirstIdent(deGeneratorGetFunction(generator)));
}
static inline char *deGeneratorGetName(deGenerator generator) {
  return utSymGetName(deGeneratorGetSym(generator));
}
static inline deBlock deGeneratorGetSubBlock(deGenerator generator) {
  return deFunctionGetSubBlock(deGeneratorGetFunction(generator));
}

// Variable methods.
deVariable deVariableCreate(deBlock block, deVariableType type, bool isConst,
                            utSym name, deExpression initializer,
                            bool generated, deLine line);
deVariable deCopyVariable(deVariable variable, deBlock destBlock);
void deDumpVariable(deVariable variable);
void deDumpVariableStr(deString str, deVariable variable);
void deVariableRename(deVariable variable, utSym newName);
static inline utSym deVariableGetSym(deVariable variable) {
  return deIdentGetSym(deVariableGetIdent(variable));
}
static inline char *deVariableGetName(deVariable variable) {
  return deIdentGetName(deVariableGetIdent(variable));
}

// Statement methods.
deStatement deStatementCreate(deBlock block, deStatementType type, deLine line);
void deDumpStatement(deStatement statement);
void deDumpStatementStr(deString string, deStatement statement);
void deDumpStatementNoSubBlock(deString string, deStatement statement);
char *deStatementTypeGetKeyword(deStatementType type);
void deAppendStatementCopy(deStatement statement, deBlock destBlock);
void dePrependStatementCopy(deStatement statement, deBlock destBlock);
void deAppendStatementCopyAfterStatement(deStatement statement, deStatement destStatement);
bool deStatementIsImport(deStatement statement);

// Expression methods.
deExpression deExpressionCreate(deExpressionType type, deLine line);
deExpression deIdentExpressionCreate(utSym name, deLine line);
deExpression deIdentExpressionCreate(utSym name, deLine line);
deExpression deIntegerExpressionCreate(deBigint bigint, deLine line);
deExpression deRandUintExpressionCreate(uint32 width, deLine line);
deExpression deFloatExpressionCreate(deFloat floatVal, deLine line);
deExpression deStringExpressionCreate(deString, deLine line);
deExpression deCStringExpressionCreate(char *text, deLine line);
deExpression deBoolExpressionCreate(bool value, deLine line);
deExpression deBinaryExpressionCreate(deExpressionType type,
                                      deExpression leftExpr,
                                      deExpression rightExpr, deLine line);
deExpression deUnaryExpressionCreate(deExpressionType type, deExpression expr,
                                     deLine line);
void deDumpExpression(deExpression expression);
void deDumpExpressionStr(deString string, deExpression expression);
deStatement deFindExpressionStatement(deExpression expression);
uint32 deExpressionCountExpressions(deExpression expression);
bool deExpressionIsMethodCall(deExpression accessExpression);
deExpression deCopyExpression(deExpression expression);
char *deExpressionTypeGetName(deExpressionType type);
deExpression deFindNamedParameter(deExpression firstParameter, utSym name);
deString deExpressionToString(deExpression expression);
void deSetExpressionToValue(deExpression expression, deValue value);
static inline deExpression deExpresssionIndexExpression(deExpression expression, uint32 index) {
  deExpression subExpression;
  uint32 i = 0;
  deForeachExpressionExpression(expression, subExpression) {
    if (i == index) {
      return subExpression;
    }
    i++;
  } deEndExpressionExpression;
  utExit("Indexed past end of expression list");
  return deExpressionNull;
}

// Ident methods.
deIdent deIdentCreate(deBlock block, deIdentType type, utSym name, deLine line);
deIdent deFunctionIdentCreate(deBlock block, deFunction function, utSym name);
deIdent deUndefinedIdentCreate(deBlock block, utSym name);
deIdent deCopyIdent(deIdent ident, deBlock destBlock);
void deDumpIdent(deIdent ident);
void deDumpIdentStr(deString string, deIdent ident);
deIdent deFindIdent(deBlock scopeBlock, utSym name);
deBlock deIdentGetSubBlock(deIdent ident);
deIdent deFindIdentOwningIdent(deIdent ident);
deLine deIdentGetLine(deIdent ident);
deIdent deFindIdentFromPath(deBlock scopeBlock, deExpression pathExpression);
deExpression deCreateIdentPathExpression(deIdent ident);
deDatatype deGetIdentDatatype(deIdent ident);
void deRenameIdent(deIdent ident, utSym newName);
bool deIdentIsModuleOrPackage(deIdent ident);

// Bigint methods.
deBigint deBigintParse(char *text, deLine line);
deBigint deUint8BigintCreate(uint8 value);
deBigint deInt8BigintCreate(int8 value);
deBigint deUint16BigintCreate(uint16 value);
deBigint deInt16BigintCreate(int16 value);
deBigint deUint32BigintCreate(uint32 value);
deBigint deInt32BigintCreate(int32 value);
deBigint deUint64BigintCreate(uint64 value);
deBigint deInt64BigintCreate(int64 value);
deBigint deNativeUintBigintCreate(uint64 value);
deBigint deZeroBigintCreate(bool isSigned, uint32 width);
uint32 deHashBigint(deBigint bigint);
bool deBigintsEqual(deBigint bigint1, deBigint bigint2);
uint32 deBigintGetUint32(deBigint bigint, deLine line);
int32 deBigintGetInt32(deBigint bigint, deLine line);
uint64 deBigintGetUint64(deBigint bigint, deLine line);
int64 deBigintGetInt64(deBigint bigint, deLine line);
deBigint deCopyBigint(deBigint bigint);
deBigint deBigintResize(deBigint bigint, uint32 width, deLine line);
bool deBigintNegative(deBigint a);
// Operations on bigints.
deBigint deBigintAdd(deBigint a, deBigint b);
deBigint deBigintSub(deBigint a, deBigint b);
deBigint deBigintNegate(deBigint a);
deBigint deBigintModularReduce(deBigint a, deBigint modulus);
void deWriteBigint(FILE *file, deBigint bigint);
char *deBigintToString(deBigint bigint, uint32 base);
void deDumpBigint(deBigint bigint);

// Float methods.  For now just 2 widths, but we add more.
deFloat deFloatCreate(deFloatType type, double value);
void deDumpFloat(deFloat floatVal);
void deDumpFloatStr(deString str, deFloat floatVal);
deFloat deFloatNegate(deFloat theFloat);
char *deFloatToString(deFloat floatVal);
deFloat deCopyFloat(deFloat theFloat);

// Datatype, and DatatypeBin methods.
void deDatatypeStart(void);
void deDatatypeStop(void);
deDatatype deUnifyDatatypes(deDatatype datatype1, deDatatype datatype2);
void deRefineAccessExpressionDatatype(deBlock scopeBlock, deExpression target,
    deDatatype valueType);
deDatatypeArray deListDatatypes(deExpression expressionList);
void deDumpDatatype(deDatatype datatype);
void deDumpDatatypeStr(deString string, deDatatype datatype);
deDatatype deNoneDatatypeCreate(void);
deDatatype deBoolDatatypeCreate(void);
deDatatype deStringDatatypeCreate(void);
deDatatype deUintDatatypeCreate(uint32 width);
deDatatype deIntDatatypeCreate(uint32 width);
deDatatype deModintDatatypeCreate(deExpression modulus);
deDatatype deFloatDatatypeCreate(uint32 width);
deDatatype deArrayDatatypeCreate(deDatatype elementType);
deDatatype deTclassDatatypeCreate(deTclass tclass);
deDatatype deClassDatatypeCreate(deClass theClass);
deDatatype deNullDatatypeCreate(deTclass tclass);
deDatatype deFunctionDatatypeCreate(deFunction function);
deDatatype deFuncptrDatatypeCreate(deDatatype returnType, deDatatypeArray parameterTypes);
deDatatype deTupleDatatypeCreate(deDatatypeArray types);
deDatatype deStructDatatypeCreate(deFunction structFunction,
                                  deDatatypeArray types, deLine line);
deDatatype deGetStructTupleDatatype(deDatatype structDatatype);
deDatatype deEnumClassDatatypeCreate(deFunction enumFunction);
deDatatype deEnumDatatypeCreate(deFunction enumFunction);
deDatatype deSetDatatypeSecret(deDatatype datatype, bool secret);
deDatatype deSetDatatypeNullable(deDatatype datatype, bool nullable, deLine line);
deDatatype deDatatypeResize(deDatatype datatype, uint32 width);
deDatatype deDatatypeSetSigned(deDatatype datatype, bool isSigned);
deTclass deFindDatatypeTclass(deDatatype datatype);
char *deDatatypeTypeGetName(deDatatypeType type);
char *deDatatypeGetDefaultValueString(deDatatype datatype);
char *deDatatypeGetTypeString(deDatatype datatype);
bool deDatatypeMatchesTypeExpression(deBlock scopeBlock, deDatatype datatype,
    deExpression typeExpression);
deDatatype deArrayDatatypeGetBaseDatatype(deDatatype datatype);
uint32 deArrayDatatypeGetDepth(deDatatype datatype);
deDatatype deFindUniqueConcreteDatatype(deDatatype datatype, deLine line);
deSecretType deFindDatatypeSectype(deDatatype datatype);
deSecretType deCombineSectypes(deSecretType a, deSecretType b);
static inline bool deDatatypeTypeIsInteger(deDatatypeType type) {
  return type == DE_TYPE_UINT || type == DE_TYPE_INT || type == DE_TYPE_MODINT;
}
static inline bool deDatatypeIsInteger(deDatatype datatype) {
  return deDatatypeTypeIsInteger(deDatatypeGetType(datatype));
}
static inline bool deDatatypeSigned(deDatatype datatype) {
  return deDatatypeGetType(datatype) == DE_TYPE_INT;
}
static inline bool deDatatypeIsFloat(deDatatype datatype) {
  return deDatatypeGetType(datatype) == DE_TYPE_FLOAT;
}
static inline bool deDatatypeTypeIsNumber(deDatatypeType type) {
  return type == DE_TYPE_UINT || type == DE_TYPE_INT || type == DE_TYPE_MODINT || type == DE_TYPE_FLOAT;
}
static inline bool deDatatypeIsNumber(deDatatype datatype) {
  return deDatatypeTypeIsNumber(deDatatypeGetType(datatype));
}

// Signature, SignatureBin, and Paramspec methods.
deSignature deLookupSignature(deFunction function, deDatatypeArray parameterTypes);
deSignature deSignatureCreate(deFunction function,
                              deDatatypeArray parameterTypes, deLine line);
deSignature deResolveConstructorSignature(deSignature signature);
bool deSignatureIsConstructor(deSignature signature);
bool deSignatureIsMethod(deSignature signature);
deSignature deCreateFullySpecifiedSignature(deFunction function);
deDatatypeArray deFindFullySpecifiedParameters(deBlock block);
void deDumpSignature(deSignature signature);
void deDumpSignatureStr(deString string, deSignature signature);
void deDumpParamspec(deParamspec paramspec);
void deDumpParamspecStr(deString string, deParamspec paramspec);
static inline deBlock deSignatureGetBlock(deSignature signature) {
  return deFunctionGetSubBlock(deSignatureGetFunction(signature));
}
static inline deDatatype deSignatureGetiType(deSignature signature, uint32 xParam) {
  return deParamspecGetDatatype(deSignatureGetiParamspec(signature, xParam));
}
static inline bool deSignatureParamInstantiated(deSignature signature, uint32 xParam) {
  return deParamspecInstantiated(deSignatureGetiParamspec(signature, xParam));
}

// Value methods.
deValue deIntegerValueCreate(deBigint bigint);
deValue deFloatValueCreate(deFloat theFloat);
deValue deBoolValueCreate(bool value);
deValue deStringValueCreate(deString string);
deValue deTclassValueCreate(deTclass tclass);
deValue deClassValueCreate(deClass theClass);
deValue deFunctionValueCreate(deFunction function);
bool deValuesEqual(deValue a, deValue b);
utSym deValueGetName(deValue value);
void deDumpValue(deValue value);
void deDumpValueStr(deString string, deValue value);

// Builtin classes.
void deBuiltinStart(void);
void deBuiltinStop(void);
deTclass deFindTypeTclass(deDatatypeType type);
deDatatype deBindBuiltinCall(deBlock scopeBlock, deFunction function,
    deDatatypeArray parameterTypes, deExpression expression);
extern deTclass deArrayTclass, deFuncptrTclass, deFunctionTclass, deBoolTclass,
    deStringTclass, deUintTclass, deIntTclass, deModintTclass, deFloatTclass,
    deTupleTclass, deStructTclass, deEnumTclass, deClassTclass;

// String methods.  Strings are uniquified and stored in a hash table.  To use a
// string as a buffer, call deStringAlloc, and later deStringFree.
deString deStringCreate(char *text, uint32 len);
deString deCStringCreate(char *text);
deString deStrinCreateFormatted(char *format, ...);
deString deMutableStringCreate(void);
deString deMutableCStringCreate(char *text);
char *deEscapeString(deString string);
char *deStringGetCstr(deString string);
deString deCopyString(deString string);
deString deUniquifyString(deString string);
bool deStringsEqual(deString string1, deString string2);

// Filepath methods.
deFilepath deFilepathCreate(char *path, deFilepath parent, bool isPackage);
char *deFilepathGetRelativePath(deFilepath filepath);

// Line methods.
deLine deLineCreate(deFilepath filepath, char *buf, uint32 len, uint32 lineNum);
void deDumpLine(deLine line);

// Operator methods.
static inline char *deOperatorGetName(deOperator theOperator) {
  return deExpressionTypeGetName(deOperatorGetType(theOperator));
}

// Relation methods.
deRelation deRelationCreate(deGenerator generator, deTclass parent, deString parentLabel,
    deTclass child, deString childLabel, bool cascadeDelete);
void deAddClassMemberRelations(deClass parentClass);
void deDumpMemberRel(deMemberRel memberRel);
void deDumpMemberRelStr(deString string, deMemberRel memberRel);
void deDumpRelation(deRelation relation);
void deDumpRelationStr(deString string, deRelation relation);
void deDumpRelations(void);
void deDumpRelationsStr(deString string);
void deVerifyRelationshipGraph(void);

// Enum methods.
void deAssignEnumEntryConstants(deBlock block);
deDatatype deFindEnumIntType(deBlock block);

// StateBinding and Binding methods.
deStateBinding deStateBindingCreate(deSignature signature,
                                    deStatement statement, bool instantiating);
deBinding deExpressionBindingCreate(deSignature signature, deBinding owningBinding,
    deExpression expression, bool instantiating);
deBinding deParameterBindingCreate(deSignature signature, deVariable variable,
    deParamspec paramspec);
deBinding deVariableBindingCreate(deSignature signature, deVariable variable);
deBinding deFindVariableBinding(deSignature signature, deVariable variable);
deBinding deFindIdentBinding(deSignature signature, deIdent ident);
deEvent deSignatureEventCreate(deSignature signature);
deEvent deUndefinedIdentEventCreate(deIdent ident);
deEvent deVariableEventCreate(deBinding varBinding);
deStateBinding deFindBindingStateBinding(deBinding binding);
static inline deExpressionType deBindingGetType(deBinding binding) {
  return deExpressionGetType(deBindingGetExpression(binding));
}
static inline deLine deBindingGetLine(deBinding binding) {
  return deExpressionGetLine(deBindingGetExpression(binding));
}

// Utilities.
void deUtilStart(void);
void deUtilStop(void);
char *deGetBlockPath(deBlock block, bool as_label);
char *deGetSignaturePath(deSignature signature);
char *deGetPathExpressionPath(deExpression pathExpression);
void deError(deLine line, char *format, ...);
void dePrintStack(void);
// These use the deStringVal global string.
void deAddString(char *string);
void deSprintToString(char *format, ...);
void deResetString(void);
// These manipulate a string buffer.
char *deResizeBufferIfNeeded(char *buf, uint32 *len, uint32 pos, uint32 newBytes);
char *deAppendToBuffer(char *buf, uint32 *len, uint32 *pos, char *text);
char *deAppendCharToBuffer(char *buf, uint32 *len, uint32 *pos, char c);
// These append to deString objects.  Use with deStringAlloc and deStringFree.
void deStringPuts(deString string, char *text);
void deStringSprintf(deString string, char *format, ...);
bool deWriteStringToFile(FILE *file, deString string);
// Formatting functions.
deString deFindPrintFormat(deExpression expression);
char *deAppendOneFormatElement(char *format, uint32 *len, uint32 *pos,
    deExpression expression);
char *deAppendFormatSpec(char *format, uint32 *len, uint32 *pos, deDatatype datatype);
char *deBytesToHex(void *bytes, uint32 len, bool littleEndian);
bool deIsLegalIdentifier(char *identifier);
char *deSnakeCase(char *camelCase);
char *deUpperSnakeCase(char *camelCase);
void deGenerateDummyLLFileAndExit(void);
char *deGetOldVsNewDatatypeStrings(deDatatype oldDatatype,
                                   deDatatype newDatatype);
static inline uint32 deBitsToBytes(uint32 bits) {
  return (bits + 7) / 8;
}
static inline uint8 deToHex(uint8 c) {
  return c <= 9? '0' + c : 'a' + c - 10;
}

// Debugging aids.
void deDump();
void dePrintIndent(void);
void dePrintIndentStr(deString string);
extern uint32 deDumpIndentLevel;

// Additional globals.
extern char *deExeName;
extern char *deLibDir;
extern char *dePackageDir;
extern bool deUnsafeMode;
extern bool deDebugMode;
extern bool deInvertReturnCode;
extern char *deLLVMFileName;
extern bool deTestMode;
extern uint32 deStackPos;
extern char *deStringVal;
extern uint32 deStringAllocated;
extern uint32 deStringPos;
extern bool deInstantiating;
extern bool deGenerating;
extern bool deInIterator;
extern deStatement deCurrentStatement;
extern deSignature deCurrentSignature;
extern char *deCurrentFileName;
extern bool deUseNewBinder;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // EXPERIMENTAL_WAYWARDGEEK_RUNE_DE_H_
