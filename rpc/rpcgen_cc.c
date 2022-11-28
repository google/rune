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

// Generate code for reading and writing the //third_party/sealedcomputing/rpc // format from RPC declarations.
#include "rpcgen_cc.h"

#include <stdlib.h>

#include "de.h"
#include "rpcdatabase.h"
#include "third_party/rune/include/de.h"

// The maximum allowed integer width for C++ RPC calls.
#define DE_MAX_INTWIDTH 64

// Open the file and call deError on failure.
static FILE *openOrDie(char *fileName) {
  printf("Generating %s\n", fileName);
  FILE *file = fopen(fileName, "w");
  if (file == NULL) {
    deError(0, "Unable to to open %s for writing", fileName);
  }
  return file;
}

// Add the top part of the header file.
static void startHeaderTop(deString headerTop, char *baseFileName) {
  char *noSuffixName = utReplaceSuffix(baseFileName, "");
  char *nameSpace = deSnakeCase(utBaseName(noSuffixName));
  char *headerGuard = deUpperSnakeCase(noSuffixName);
  deStringSprintf(headerTop,
      "#ifndef %1$s_H_\n"
      "#define %1$s_H_\n\n"
      "// This is a generated file: DO NOT EDIT!\n\n"
      "#include <cstdint>\n"
      "#include <string>\n"
      "#include <tuple>\n"
      "#include <vector>\n\n"
      "#include \"third_party/sealedcomputing/wasm3/base.h\"\n"
      "#include \"third_party/sealedcomputing/wasm3/bytestring.h\"\n"
      "#include \"third_party/sealedcomputing/wasm3/socket.h\"\n"
      "#include \"third_party/sealedcomputing/wasm3/statusor.h\"\n\n"
      "namespace sealed {\n"
      "namespace %2$s {\n\n",
      headerGuard, nameSpace);
}

// Add the ending to the header file.
static void finishHeaderBot(deString headerBot, char *baseFileName) {
  char *noSuffixName = utReplaceSuffix(baseFileName, "");
  char *nameSpace = deSnakeCase(utBaseName(noSuffixName));
  char *headerGuard = deUpperSnakeCase(noSuffixName);
  deStringSprintf(headerBot,
    "\n}  // namespace %s\n\n"
    "}  // namespace sealed\n"
    "#endif  // %s_H_\n"
    , nameSpace, headerGuard);
}

// Forward declaration for recursion.
static void appendDatatypeString(deString string, deDatatype datatype);

// Return the type string for the structure or enum.
static void appendFunctionTypeString(deString string, deDatatype datatype) {
  deStringPuts(string, deFunctionGetName(deDatatypeGetFunction(datatype)));
}

// Return the type string for the float or double.
static void appendFloatTypeString(deString string, deDatatype datatype) {
  if (deDatatypeGetWidth(datatype) == 32) {
    deStringPuts(string, "float");
  } else {
    deStringPuts(string, "double");
  }
}

// Return the type string for an array.
static void appendArrayTypeString(deString string, deDatatype datatype) {
  deStringPuts(string, "std::vector<");
  appendDatatypeString(string, deDatatypeGetElementType(datatype));
  deStringPuts(string, ">");
}

// Return a declaration for the tuple type, based on std::tuple.
static void appendTupleTypeString(deString string, deDatatype datatype) {
  deStringPuts(string, "std::tuple<");
  deDatatype elementType;
  bool firstTime = true;
  deForeachDatatypeTypeList(datatype, elementType) {
    if (!firstTime) {
      deStringPuts(string, ", ");
    }
    appendDatatypeString(string, elementType);
    firstTime = false;
  } deEndDatatypeTypeList;
  deStringPuts(string, ">");
}

// Return a C++ formatted type string corresponding to this datatype.  The
// datatype must be instantiatable, meaning Class, not Tclass.
static void appendDatatypeString(deString string, deDatatype datatype) {
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_BOOL:
      deStringPuts(string, "bool");
      return;
    case DE_TYPE_STRING:
      deStringPuts(string, "std::string");
      break;
    case DE_TYPE_UINT:
      deStringSprintf(string, "uint%u_t", deDatatypeGetWidth(datatype));
      break;
    case DE_TYPE_INT:
      deStringSprintf(string, "int%u_t", deDatatypeGetWidth(datatype));
      break;
    case DE_TYPE_FLOAT:
      appendFloatTypeString(string, datatype);
      break;
    case DE_TYPE_ARRAY:
      appendArrayTypeString(string, datatype);
      break;
    case DE_TYPE_TUPLE:
      appendTupleTypeString(string, datatype);
      break;
    case DE_TYPE_STRUCT:
    case DE_TYPE_ENUM:
      appendFunctionTypeString(string, datatype);
      break;
    case DE_TYPE_NONE:
      deStringPuts(string, "void");
      break;
    case DE_TYPE_MODINT:
    case DE_TYPE_CLASS:
    case DE_TYPE_FUNCPTR:
    case DE_TYPE_TCLASS:
    case DE_TYPE_NULL:
    case DE_TYPE_FUNCTION:
    case DE_TYPE_ENUMCLASS:
      utExit("Unexpected datatype");
  }
}

// Forward declaration for recursion.
static void declareDatatype(deString headerTop, deDatatype datatype, deLine line);

// Declare any element types of a tuple.
static void declareTupleElementTypes(deString headerTop, deDatatype datatype, deLine line) {
  deDatatype elementType;
  deForeachDatatypeTypeList(datatype, elementType) {
    declareDatatype(headerTop, elementType, line);
  } deEndDatatypeTypeList;
}

// Declare the array datatype's element type.
static void declareArray(deString headerTop, deDatatype datatype, deLine line) {
  deDatatype elementType = deDatatypeGetElementType(datatype);
  declareDatatype(headerTop, elementType, line);
}

// Find the initializer string for the enum datatype.
static char *findEnumInitializer(deDatatype datatype) {
  deFunction function = deDatatypeGetFunction(datatype);
  deVariable variable = deBlockGetFirstVariable(deFunctionGetSubBlock(function));
  return utSprintf("%s::%s", deFunctionGetName(function), deVariableGetName(variable));
}

// Find the initializer string for the datatype, if the datatype is
// plain-old-data (POD).  Otherwise, return NULL.
static char *findDatatypeInitializer(deDatatype datatype) {
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_BOOL:
      return "false";
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
      return "0";
    case DE_TYPE_ENUM:
      return findEnumInitializer(datatype);
    case DE_TYPE_FLOAT:
      if (deDatatypeGetWidth(datatype) == 32) {
        return "0.0f";
      }
      return "0.0";
    default:
      break;
  }
  return NULL;
}

// Declare the struct datatype, and any sub-types.
static void declareStruct(deString headerTop, deDatatype datatype, deLine line) {
  deDatatype subType;
  deForeachDatatypeTypeList(datatype, subType) {
    declareDatatype(headerTop, subType, line);
  } deEndDatatypeTypeList;
  deFunction function = deDatatypeGetFunction(datatype);
  deStringSprintf(headerTop, "struct %s {\n", deFunctionGetName(function));
  uint32 xParam = 0;
  deVariable var;
  deString typeString = deMutableStringCreate();
  deForeachBlockVariable(deFunctionGetSubBlock(function), var) {
    deDatatype varDatatype = deDatatypeGetiTypeList(datatype, xParam);
    utAssert(datatype != deDatatypeNull && deDatatypeConcrete(datatype));
    deStringSetUsed(typeString, 0);
    appendDatatypeString(typeString, varDatatype);
    char *initializer = findDatatypeInitializer(varDatatype);
    if (initializer == NULL) {
      deStringSprintf(headerTop, "  %s %s;\n",
          deStringGetCstr(typeString), deVariableGetName(var));
    } else {
      deStringSprintf(headerTop, "  %s %s = %s;\n",
          deStringGetCstr(typeString), deVariableGetName(var), initializer);
    }
    xParam++;
  } deEndBlockVariable;
  deStringDestroy(typeString);
  deStringPuts(headerTop, "};\n\n");
}

static void declareEnum(deString headerTop, deDatatype datatype, deLine line) {
  deFunction function = deDatatypeGetFunction(datatype);
  deStringSprintf(headerTop, "enum class %s : uint%u_t {\n", deFunctionGetName(function),
      deDatatypeGetWidth(datatype));
  deVariable var;
  deForeachBlockVariable(deFunctionGetSubBlock(function), var) {
    utAssert(datatype != deDatatypeNull && deDatatypeConcrete(datatype));
    deStringSprintf(headerTop, "  %s = %u,\n",
        deVariableGetName(var), deVariableGetEntryValue(var));
  } deEndBlockVariable;
  deStringPuts(headerTop, "};\n\n");
}

// Declare the datatype in the header top.  This includes structs, enums, but
// not yet classes or tuples.  Use the natural datatype mapping:
//
//   enum -> enum
//   string -> std::string
//   struct -> struct
//   tuple -> std::tuple
//   array -> std::vector
//
// Declare sub-types first.  Loops are not possible since pointers are not
// allowed.
static void declareDatatype(deString headerTop, deDatatype datatype, deLine line) {
  if (rpcDatatypeDeclared(datatype)) {
    return;  // Already declared.
  }
  switch(deDatatypeGetType(datatype)) {
    case DE_TYPE_BOOL:
    case DE_TYPE_STRING:
      break;
    case DE_TYPE_UINT:
    case DE_TYPE_INT: {
      uint32 width = deDatatypeGetWidth(datatype);
      if (width != 8 && width != 16 && width != 32 && width != 64) {
        deError(line, "Unsupported integer width for a C++ RPC call");
      }
      break;
    }
    case DE_TYPE_FUNCPTR:
      deError(line, "RPC calls cannot pass function pointers");
      break;
    case DE_TYPE_TUPLE:
      declareTupleElementTypes(headerTop, datatype, line);
      break;
    case DE_TYPE_ARRAY:
      declareArray(headerTop, datatype, line);
      break;
    case DE_TYPE_STRUCT:
      declareStruct(headerTop, datatype, line);
      break;
    case DE_TYPE_ENUM:
      declareEnum(headerTop, datatype, line);
      break;
    case DE_TYPE_FLOAT: {
      uint32 width = deDatatypeGetWidth(datatype);
      if (width != 32 && width != 64) {
        deError(line, "Unsupported floating point width in RPC calls: %u", width);
      }
      break;
    }
    case DE_TYPE_FUNCTION:
    case DE_TYPE_CLASS:
    case DE_TYPE_TCLASS:
    case DE_TYPE_NULL:
    case DE_TYPE_NONE:
    case DE_TYPE_MODINT:
    case DE_TYPE_ENUMCLASS:
      utExit("Unexpected datatype in RPC call");
  }
  rpcDatatypeSetDeclared(datatype, true);
}

// Declare all of the datatypes used by the signature.
static void declareSignatureDatatypes(deString headerTop, deSignature signature, deLine line) {
  deDatatype datatype = deSignatureGetReturnType(signature);
  if (datatype != deNoneDatatypeCreate() && !rpcDatatypeDeclared(datatype)) {
    declareDatatype(headerTop, datatype, line);
  }
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    datatype = deParamspecGetDatatype(paramspec);
    declareDatatype(headerTop, datatype, line);
  } deEndSignatureParamspec;
}

// True if an array, tuple, struct, string or bignum.
static bool datatypePassedByReference(deDatatype datatype) {
  deDatatypeType type = deDatatypeGetType(datatype);
  return type == DE_TYPE_TUPLE || type == DE_TYPE_STRUCT || deDatatypeContainsArray(datatype);
}

// Declare parameters for the function by appending them to headerBot.
static void declareFunctionParams(deString headerBot, deFunction function,
    deSignature signature) {
  deString typeString = deMutableStringCreate();
  uint32 xParam = 0;
  deVariable var;
  deForeachBlockVariable(deFunctionGetSubBlock(function), var) {
    if (deVariableGetType(var) != DE_VAR_PARAMETER) {
      return;
    }
    if (xParam != 0) {
      deStringPuts(headerBot, ", ");
    }
    deParamspec paramspec = deSignatureGetiParamspec(signature, xParam);
    deDatatype datatype = deParamspecGetDatatype(paramspec);
    deStringSetUsed(typeString, 0);
    appendDatatypeString(typeString, datatype);
    char *paramType = deStringGetCstr(typeString);
    if (datatypePassedByReference(datatype)) {
      deStringSprintf(headerBot, "const %s& %s", paramType, deVariableGetName(var));
    } else {
      deStringSprintf(headerBot, "%s %s", paramType, deVariableGetName(var));
    }
    xParam++;
  } deEndBlockVariable;
  deStringDestroy(typeString);
}

// Declare the function.
static void declareSignatureFunction(deString headerBot, deSignature signature) {
  deString typeString = deMutableStringCreate();
  deDatatype returnType = deSignatureGetReturnType(signature);
  appendDatatypeString(typeString, returnType);
  deFunction function = deSignatureGetFunction(signature);
  char *retType = deStringGetCstr(typeString);
  if (returnType != deNoneDatatypeCreate()) {
      deStringSprintf(headerBot, "sealed::wasm::StatusOr<%s> %s(",
          retType, deFunctionGetName(function));
  } else {
    deStringSprintf(headerBot, "sealed::wasm::Status %s(", deFunctionGetName(function));
  }
  declareFunctionParams(headerBot, function, signature);
  deStringPuts(headerBot, ")");
  deStringDestroy(typeString);
}

// Declare a function taking a socket in addition to the declared parameters.
static void declareSignatureSocketFunction(deString headerBot, deSignature signature) {
  deString typeString = deMutableStringCreate();
  deDatatype returnType = deSignatureGetReturnType(signature);
  appendDatatypeString(typeString, returnType);
  deFunction function = deSignatureGetFunction(signature);
  char *retType = deStringGetCstr(typeString);
  if (returnType != deNoneDatatypeCreate()) {
    deStringSprintf(headerBot, "sealed::wasm::StatusOr<%s> %s(",
        retType, deFunctionGetName(function));
  } else {
    deStringSprintf(headerBot, "sealed::wasm::Status %s(",
                    deFunctionGetName(function));
  }
  declareFunctionParams(headerBot, function, signature);
  deStringPuts(headerBot, ", wasm::Socket* socket)");
  deStringDestroy(typeString);
}

// If no secrets are returned, use ByteStering.  If only secrets are returned,
// use SecretByteString.  Otherwise, use EncodedMessage, which has both
// fields.
static char *findSectypeName(deSecretType sectype) {
  switch (sectype) {
    case DE_SECTYPE_NONE:
    case DE_SECTYPE_ALL_PUBLIC:
      return "ByteString";
    case DE_SECTYPE_ALL_SECRET:
      return "SecretByteString";
    case DE_SECTYPE_MIXED:
      return "EncodedMessage";
  }
  return NULL;  // Dummy return;
}

// Find the secret type of the request.
static deSecretType findRequestSectype(deSignature signature) {
  deSecretType sectype = DE_SECTYPE_NONE;
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    deDatatype datatype = deParamspecGetDatatype(paramspec);
    sectype = deCombineSectypes(sectype, deFindDatatypeSectype(datatype));
  } deEndSignatureParamspec;
  return sectype;
}

// Generate a declaration for the encode request function.
static void declareEncodeRequest(deString headerBot, deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  deSecretType sectype = findRequestSectype(signature);
  deStringSprintf(headerBot, "sealed::wasm::%s Encode%sRequest(",
                  findSectypeName(sectype), deFunctionGetName(function));
  declareFunctionParams(headerBot, function, signature);
  deStringPuts(headerBot, ")");
}

// Generate a declaration for the encode response function.
static void declareEncodeResponse(deString headerBot, deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  deSecretType sectype = deFindDatatypeSectype(deSignatureGetReturnType(signature));
  deStringSprintf(headerBot, "sealed::wasm::%s Encode%sResponse(",
      findSectypeName(sectype), deFunctionGetName(function));
  deDatatype returnType = deSignatureGetReturnType(signature);
  if (returnType != deNoneDatatypeCreate()) {
    appendDatatypeString(headerBot, returnType);
    deStringPuts(headerBot, " response");
  }
  deStringPuts(headerBot, ")");
}

// Declare the function parameters which will be decoded.  They are all passed
// by reference.
static void declareDecodeRequestParameters(deString headerBot, deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  uint32 xParam = 0;
  deVariable var;
  deForeachBlockVariable(deFunctionGetSubBlock(function), var) {
    if (deVariableGetType(var) != DE_VAR_PARAMETER) {
      return;
    }
    if (xParam != 0) {
      deStringPuts(headerBot, ", ");
    }
    deParamspec paramspec = deSignatureGetiParamspec(signature, xParam);
    appendDatatypeString(headerBot, deParamspecGetDatatype(paramspec));
    deStringSprintf(headerBot, "& %s", deVariableGetName(var));
    xParam++;
  } deEndBlockVariable;
}

// Generate a declaration for the decode request function.
static void declareDecodeRequest(deString headerBot, deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  deSecretType sectype = findRequestSectype(signature);
  deStringSprintf(headerBot,
      "::sealed::wasm::Status Decode%sRequest(sealed::wasm::%s encoded_request, ",
      deFunctionGetName(function), findSectypeName(sectype));
  declareDecodeRequestParameters(headerBot, signature);
  deStringPuts(headerBot, ")");
}

// Generate a declaration for the decode response function.
static void declareDecodeResponse(deString headerBot, deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  char *funcName = deFunctionGetName(function);
  deDatatype returnType = deSignatureGetReturnType(signature);
  if (returnType != deNoneDatatypeCreate()) {
    deStringSprintf(headerBot, "::sealed::wasm::StatusOr<", funcName);
    appendDatatypeString(headerBot, returnType);
    deStringPuts(headerBot, ">");
  } else {
    deStringSprintf(headerBot, "::sealed::wasm::Status", funcName);
  }
  deSecretType sectype = deFindDatatypeSectype(deSignatureGetReturnType(signature));
  deStringSprintf(headerBot,
      " Decode%sResponse(const sealed::wasm::%s& encoded_response)",
      funcName, findSectypeName(sectype));
}

// Generate the C header file for both client and server of RPCs.
static void genHeaderCode(deString header, deSignature signature, deLine line) {
  declareSignatureDatatypes(header, signature, line);
  deStringPuts(header, "namespace client {\n");
  declareSignatureSocketFunction(header, signature);
  deStringPuts(header, ";\n");
  declareSignatureFunction(header, signature);
  deStringPuts(header, ";\n");
  declareEncodeRequest(header, signature);
  deStringPuts(header, ";\n");
  declareDecodeResponse(header, signature);
  deStringPuts(header, ";\n}  // namespace client\n");

  deStringPuts(header, "namespace server {\n");
  declareSignatureFunction(header, signature);
  deStringPuts(header, ";\n");
  declareEncodeResponse(header, signature);
  deStringPuts(header, ";\n");
  declareDecodeRequest(header, signature);
  deStringPuts(header, ";\n}  // namespace server\n");
}

// Write the header string to the header file.
static void writeStringToFile(char *defFileName, char *fileName, deString string) {
  FILE *file = openOrDie(fileName);
  deWriteStringToFile(file, string);
  fclose(file);
}

// Generate the top of the server.c file.
static void genServerTop(deString server, char *baseFileName, char *headerFile) {
  char *noSuffixName = utReplaceSuffix(baseFileName, "");
  char *header = utBaseName(headerFile);
  char *nameSpace = deSnakeCase(utBaseName(noSuffixName));
  deStringSprintf(server,
      "// This is a generated file.  DO NOT EDIT.\n"
      "// Serve %s RPC calls from our clients.\n"
      "\n"
      "#include \"%s\"\n"
      "#include \"third_party/sealedcomputing/rpc/encode_decode_lite.h\"\n"
      "#include \"third_party/sealedcomputing/wasm3/base.h\"\n"
      "#include \"third_party/sealedcomputing/wasm3/logging.h\"\n"
      "\n"
      "using ::sealed::rpc::Decoder;\n"
      "using ::sealed::rpc::Encoder;\n"
      "using ::sealed::wasm::ByteString;\n"
      "using ::sealed::wasm::SecretByteString;\n"
      "using ::sealed::wasm::EncodedMessage;\n"
      "using ::sealed::wasm::Status;\n\n"
      "namespace sealed {\n"
      "namespace %s {\n"
      "namespace server {\n",
      noSuffixName, header, nameSpace);
}

// Generate the top of the client.c file.
static void genClientTop(deString client, char *baseFileName, char *headerFile) {
  char *noSuffixName = utReplaceSuffix(baseFileName, "");
  char *header = utBaseName(headerFile);
  char *nameSpace = deSnakeCase(utBaseName(noSuffixName));
  deStringSprintf(client,
      "// This is a generated file.  DO NOT EDIT.\n"
      "// Send %s RPC calls from our client to servers.\n"
      "\n"
      "#include \"%s\"\n"
      "#include \"third_party/sealedcomputing/rpc/encode_decode_lite.h\"\n"
      "#include \"third_party/sealedcomputing/wasm3/base.h\"\n"
      "#include \"third_party/sealedcomputing/wasm3/bytestring.h\"\n"
      "#include \"third_party/sealedcomputing/wasm3/logging.h\"\n"
      "#include \"third_party/sealedcomputing/wasm3/send_rpc.h\"\n"
      "#include \"third_party/sealedcomputing/wasm3/socket.h\"\n"
      "\n"
      "using ::sealed::rpc::Decoder;\n"
      "using ::sealed::rpc::Encoder;\n"
      "using ::sealed::wasm::ByteString;\n"
      "using ::sealed::wasm::SecretByteString;\n"
      "using ::sealed::wasm::EncodedMessage;\n\n"
      "namespace sealed {\n"
      "namespace %s {\n"
      "namespace client {\n",
      noSuffixName, header, nameSpace);
}

static void finishClientNamespace(deString string, char *baseFileName) {
  char *noSuffixName = utReplaceSuffix(baseFileName, "");
  char *nameSpace = deSnakeCase(utBaseName(noSuffixName));
  deStringSprintf(string,
      "\n}  // namespace client\n"
      "}  // namespace %s\n"
      "}  // namespace sealed\n",
      nameSpace);
}

static void finishServerNamespace(deString string, char *baseFileName) {
  char *noSuffixName = utReplaceSuffix(baseFileName, "");
  char *nameSpace = deSnakeCase(utBaseName(noSuffixName));
  deStringSprintf(string,
      "\n}  // namespace server\n"
      "}  // namespace %s\n"
      "}  // namespace sealed\n",
      nameSpace);
}

// Get the name of the decoder method to decode the datatype.
static char *getDatatypeDecodeFuncName(deDatatype datatype) {
  switch (deDatatypeGetType(datatype)) {
    case DE_TYPE_BOOL:
      return "Bool";
    case DE_TYPE_STRING:
      return "String";
    case DE_TYPE_UINT:
    case DE_TYPE_ENUM:
      return utSprintf("U%u", deDatatypeGetWidth(datatype));
    case DE_TYPE_INT:
      return utSprintf("S%u", deDatatypeGetWidth(datatype));
    case DE_TYPE_FLOAT:
      if (deDatatypeGetWidth(datatype) == 32) {
        return "F32";
      }
      return "F64";
    default:
      utExit("Unexpected datatype");
  }
  return NULL;  // Dummy return.
}

// Forward declaration for recursion.
static void decodeParameter(deString server, deString lexpr, deDatatype datatype);

// Return "secret" or "public" depending on the secret bit on the datatype.
static char *visibilityPrefix(deDatatype datatype) {
  return deDatatypeSecret(datatype)? "secret" : "public";
}

// Decode an enumerated type.  The only complication is the required static_cast.
static void decodeEnumParameter(deString server, deString lexpr, deDatatype datatype) {
  uint32 width = deDatatypeGetWidth(datatype);
  deStringSprintf(server,
      "  if (!%3$s_decoder.U%1$u(reinterpret_cast<uint%1$u_t*>(&%2$s))) {\n"
      "    return ::sealed::wasm::Status(::sealed::wasm::kInvalidArgument,\n"
      "        \"Could not decode RPC %2$s\");\n"
      "  }\n",
      width, deStringGetCstr(lexpr), visibilityPrefix(datatype));
}

// Decode the std::tuple parameter.
static void decodeTupleParameter(deString server, deString lexpr, deDatatype datatype) {
  deSecretType sectype = deFindDatatypeSectype(datatype);
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "  public_decoder.StartStruct();\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "  secret_decoder.StartStruct();\n");
  }
  deString access = deMutableStringCreate();
  uint32_t pos = 0;
  deDatatype elementType;
  deForeachDatatypeTypeList(datatype, elementType) {
    deStringSetUsed(access, 0);
    deStringSprintf(access, "std::get<%u>(%s)", pos, deStringGetCstr(lexpr));
    decodeParameter(server, access, elementType);
    pos++;
  } deEndDatatypeTypeList;
  deStringDestroy(access);
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "  public_decoder.FinishStruct();\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "  secret_decoder.FinishStruct();\n");
  }
}

// Decode the array parameter.
static void decodeArrayParameter(deString server, deString lexpr, deDatatype datatype) {
  deSecretType sectype = deFindDatatypeSectype(datatype);
  deStringPuts(server, "  {\n    uint32_t len;\n");
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "    public_decoder.StartArray(&len);\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "    secret_decoder.StartArray(&len);\n");
  }
  deStringSprintf(server,
      "    %s.resize(len);\n"
      "    for (uint32_t i = 0; i < len; i++) {\n",
      deStringGetCstr(lexpr));
  uint32_t usedChars = deStringGetUsed(lexpr);
  deStringPuts(lexpr, "[i]");
  deDatatype elementType = deDatatypeGetElementType(datatype);;
  decodeParameter(server, lexpr, elementType);
  deStringSetUsed(lexpr, usedChars);
  deStringPuts(server, "    }\n");
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "    public_decoder.FinishArray();\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "    secret_decoder.FinishArray();\n");
  }
  deStringPuts(server, "  }\n");
}

// Decode the struct parameter.
static void decodeStructParameter(deString server, deString lexpr, deDatatype datatype) {
  deSecretType sectype = deFindDatatypeSectype(datatype);
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "  public_decoder.StartStruct();\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "  secret_decoder.StartStruct();\n");
  }
  deFunction function = deDatatypeGetFunction(datatype);
  utAssert(deFunctionGetType(function) == DE_FUNC_STRUCT);
  deBlock block = deFunctionGetSubBlock(function);
  uint32_t usedChars = deStringGetUsed(lexpr);
  deVariable var = deBlockGetFirstVariable(block);
  deDatatype elementType;
  deForeachDatatypeTypeList(datatype, elementType) {
    deStringSprintf(lexpr, ".%s", deVariableGetName(var));
    decodeParameter(server, lexpr, elementType);
    deStringSetUsed(lexpr, usedChars);
    var = deVariableGetNextBlockVariable(var);
  } deEndDatatypeTypeList;
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "  public_decoder.FinishStruct();\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "  secret_decoder.FinishStruct();\n");
  }
}

// Decode the parameter.  |lexpr| is the text for accessing the next data to be
// decoded, e.g. foo[10].name, so we can write foo[10].name = "Bob".
static void decodeParameter(deString server, deString lexpr, deDatatype datatype) {
  switch(deDatatypeGetType(datatype)) {
    case DE_TYPE_BOOL:
    case DE_TYPE_STRING:
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_FLOAT:
      // These can be directly assigned.
      deStringSprintf(server,
          "  if (!%3$s_decoder.%1$s(&%2$s)) {\n"
          "    return ::sealed::wasm::Status(::sealed::wasm::kInvalidArgument,\n"
          "        \"Could not decode RPC %2$s\");\n"
          "  }\n",
          getDatatypeDecodeFuncName(datatype), deStringGetCstr(lexpr), visibilityPrefix(datatype));
      break;
    case DE_TYPE_ENUM:
      decodeEnumParameter(server, lexpr, datatype);
      break;
    case DE_TYPE_TUPLE:
      decodeTupleParameter(server, lexpr, datatype);
      break;
    case DE_TYPE_ARRAY:
      decodeArrayParameter(server, lexpr, datatype);
      break;
    case DE_TYPE_STRUCT:
      decodeStructParameter(server, lexpr, datatype);
      break;
    case DE_TYPE_NONE:
    case DE_TYPE_FUNCPTR:
    case DE_TYPE_FUNCTION:
    case DE_TYPE_CLASS:
    case DE_TYPE_TCLASS:
    case DE_TYPE_NULL:
    case DE_TYPE_MODINT:
    case DE_TYPE_ENUMCLASS:
      utExit("Unexpected datatype in RPC call");
  }
}

// Forward declaration for recursion.
static void encodeParameter(deString server, deString expr, deDatatype datatype);

// Encode the enumerated type.
static void encodeEnumParameter(deString server, deString expr, deDatatype datatype) {
  uint32 width = deDatatypeGetWidth(datatype);
  deStringSprintf(server,
      "  %3$s_encoder.U%1$u(static_cast<uint%1$u_t>(%2$s));\n",
      width, deStringGetCstr(expr), visibilityPrefix(datatype));
}

// Encode the std::tuple parameter.
static void encodeTupleParameter(deString server, deString expr, deDatatype datatype) {
  deSecretType sectype = deFindDatatypeSectype(datatype);
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "  public_encoder.StartStruct();\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "  secret_encoder.StartStruct();\n");
  }
  deString access = deMutableStringCreate();
  uint32_t pos = 0;
  deDatatype elementType;
  deForeachDatatypeTypeList(datatype, elementType) {
    deStringSetUsed(access, 0);
    deStringSprintf(access, "std::get<%u>(%s)", pos, deStringGetCstr(expr));
    encodeParameter(server, access, elementType);
    pos++;
  } deEndDatatypeTypeList;
  deStringDestroy(access);
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "  public_encoder.FinishStruct();\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "  secret_encoder.FinishStruct();\n");
  }
}

// Encode an array parameter.
static void encodeArrayParameter(deString server, deString expr, deDatatype datatype) {
  deSecretType sectype = deFindDatatypeSectype(datatype);
  deStringSprintf(server,
      "  {\n"
      "    size_t len = %s.size();\n",
      deStringGetCstr(expr));
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "    public_encoder.StartArray(len);\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "    secret_encoder.StartArray(len);\n");
  }
  deStringPuts(server, "    for (size_t i = 0; i < len; i++) {\n");
  uint32_t usedChars = deStringGetUsed(expr);
  deDatatype elementType = deDatatypeGetElementType(datatype);
  deStringSprintf(expr, "[i]");
  encodeParameter(server, expr, elementType);
  deStringSetUsed(expr, usedChars);
  deStringPuts(server, "    }\n");
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "    public_encoder.FinishArray();\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "    secret_encoder.FinishArray();\n");
  }
  deStringPuts(server, "  }\n");
}

static void encodeStructParameter(deString server, deString expr, deDatatype datatype) {
  deSecretType sectype = deFindDatatypeSectype(datatype);
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "  public_encoder.StartStruct();\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "  secret_encoder.StartStruct();\n");
  }
  deFunction function = deDatatypeGetFunction(datatype);
  utAssert(deFunctionGetType(function) == DE_FUNC_STRUCT);
  deBlock block = deFunctionGetSubBlock(function);
  uint32_t usedChars = deStringGetUsed(expr);
  deVariable var = deBlockGetFirstVariable(block);
  deDatatype elementType;
  deForeachDatatypeTypeList(datatype, elementType) {
    deStringSprintf(expr, ".%s", deVariableGetName(var));
    encodeParameter(server, expr, elementType);
    deStringSetUsed(expr, usedChars);
    var = deVariableGetNextBlockVariable(var);
  } deEndDatatypeTypeList;
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server, "  public_encoder.FinishStruct();\n");
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    deStringPuts(server, "  secret_encoder.FinishStruct();\n");
  }
}

// Encode the parameter.  |expr| is the text for accessing the next data to be
// encoded, e.g. foo[10].name.
static void encodeParameter(deString server, deString expr, deDatatype datatype) {
  switch(deDatatypeGetType(datatype)) {
    case DE_TYPE_BOOL:
    case DE_TYPE_STRING:
    case DE_TYPE_UINT:
    case DE_TYPE_INT:
    case DE_TYPE_FLOAT:
      // These can be directly assigned.
      deStringSprintf(server, "  %s_encoder.%s(%s);\n",
          visibilityPrefix(datatype), getDatatypeDecodeFuncName(datatype), deStringGetCstr(expr));
      break;
    case DE_TYPE_ENUM:
      encodeEnumParameter(server, expr, datatype);
      break;
    case DE_TYPE_TUPLE:
      encodeTupleParameter(server, expr, datatype);
      break;
    case DE_TYPE_ARRAY:
      encodeArrayParameter(server, expr, datatype);
      break;
    case DE_TYPE_STRUCT:
      encodeStructParameter(server, expr, datatype);
      break;
    case DE_TYPE_NONE:
    case DE_TYPE_FUNCPTR:
    case DE_TYPE_FUNCTION:
    case DE_TYPE_CLASS:
    case DE_TYPE_TCLASS:
    case DE_TYPE_NULL:
    case DE_TYPE_MODINT:
    case DE_TYPE_ENUMCLASS:
      utExit("Unexpected datatype in RPC call");
  }
}

// Generate code to decode each parameter sent in the RPC.  Assign them to
// variables named the same as the parameter names.
static void genDecodeRequestParameters(deString server, deSignature signature) {
  deBlock block = deFunctionGetSubBlock(deSignatureGetFunction(signature));
  deString string = deMutableStringCreate();;
  deVariable param = deBlockGetFirstVariable(block);
  deParamspec paramspec;
  deForeachSignatureParamspec(signature, paramspec) {
    deDatatype datatype = deParamspecGetDatatype(paramspec);
    deStringSetUsed(string, 0);
    deStringPuts(string, deVariableGetName(param));
    decodeParameter(server, string, datatype);
    param = deVariableGetNextBlockVariable(param);
  } deEndSignatureParamspec;
  deStringDestroy(string);
}

// List the parameters passed to the function call.  Assume there are variables
// declared with the same names as the parameters, so just pass the parameter
// names.
static void printFunctionCallParameters(deString server, deFunction function) {
  uint32 xParam = 0;
  deVariable var;
  deForeachBlockVariable(deFunctionGetSubBlock(function), var) {
    if (deVariableGetType(var) != DE_VAR_PARAMETER) {
      return;
    }
    if (xParam != 0) {
      deStringPuts(server, ", ");
    }
    deStringSprintf(server, "%s", deVariableGetName(var));
    xParam++;
  } deEndBlockVariable;
}

// Generate a function call to the user-defined RPC handler.
static void genFunctionCall(deString server, deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  deStringSprintf(server, "%s(", deFunctionGetName(function));
  printFunctionCallParameters(server, function);
  deStringPuts(server, ");\n");
}

// Declare the encoder variables needed.
static void declareEncoders(deString string, deSecretType sectype) {
  switch (sectype) {
    case DE_SECTYPE_NONE:
    case DE_SECTYPE_ALL_PUBLIC:
      deStringPuts(string, "  Encoder public_encoder;\n");
      break;
    case DE_SECTYPE_ALL_SECRET:
      deStringPuts(string, "  Encoder secret_encoder;\n");
      break;
    case DE_SECTYPE_MIXED:
      deStringPuts(string,
          "  Encoder public_encoder;\n"
          "  Encoder secret_encoder;\n");
      break;
  }
}

// Write a return statement, which depends on the secret type.
static void returnEncodedData(deString string, deSecretType sectype) {
  switch (sectype) {
    case DE_SECTYPE_NONE:
    case DE_SECTYPE_ALL_PUBLIC:
      deStringPuts(string,
          "  return ByteString(public_encoder.Finish());\n");
      break;
    case DE_SECTYPE_ALL_SECRET:
      deStringPuts(string,
          "  return SecretByteString(secret_encoder.Finish());\n");
      break;
    case DE_SECTYPE_MIXED:
      deStringPuts(string,
          "  return EncodedMessage(public_encoder.Finish(), secret_encoder.Finish());\n");
      break;
  }
}

// Generate code to encode the response.
static void genEncodeResponse(deString server, deSignature signature) {
  deSecretType sectype = deFindDatatypeSectype(deSignatureGetReturnType(signature));
  deStringPuts(server, "\n");
  declareEncodeResponse(server, signature);
  deStringPuts(server, " {\n");
  declareEncoders(server, sectype);
  deString string = deMutableCStringCreate("response");
  deDatatype returnType = deSignatureGetReturnType(signature);
  if (returnType != deNoneDatatypeCreate()) {
    encodeParameter(server, string, returnType);
  }
  deStringDestroy(string);
  returnEncodedData(server, sectype);
  deStringPuts(server, "}\n");
}

// Declare decoder request parameters as local variables.
static void declareDecodeRequestVariables(deString server, deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  uint32 xParam = 0;
  deVariable var;
  deForeachBlockVariable(deFunctionGetSubBlock(function), var) {
    if (deVariableGetType(var) != DE_VAR_PARAMETER) {
      return;
    }
    deStringPuts(server, "  ");
    deParamspec paramspec = deSignatureGetiParamspec(signature, xParam);
    appendDatatypeString(server, deParamspecGetDatatype(paramspec));
    deStringSprintf(server, " %s;\n", deVariableGetName(var));
    xParam++;
  } deEndBlockVariable;
}

// Generate a call to the request decoder function.
static void genDecodeRequestCall(deString server, deSignature signature) {
  deFunction function = deSignatureGetFunction(signature);
  char *funcName = deFunctionGetName(function);
  deStringSprintf(server,
                  "Decode%sRequest(EncodedMessage(encoded_request, "
                  "encoded_request_secret), ",
                  funcName);
  printFunctionCallParameters(server, function);
  deStringPuts(server, ")");
}

// Declare the required decoders, public or secret.
static void declareDecoders(deString string, deSecretType sectype,
    bool is_request) {
  char *type = is_request? "request" : "response";
  switch (sectype) {
    case DE_SECTYPE_ALL_PUBLIC:
    case DE_SECTYPE_NONE:
      deStringSprintf(string, "  Decoder public_decoder(encoded_%s);\n", type);
      break;
    case DE_SECTYPE_ALL_SECRET:
      deStringSprintf(string, "  Decoder secret_decoder(encoded_%s);\n", type);
      break;
    case DE_SECTYPE_MIXED:
      deStringSprintf(string,
          "  Decoder public_decoder(encoded_%1$s.public_data);\n"
          "  Decoder secret_decoder(encoded_%1$s.secret_data);\n",
          type);
  }
}

// Generate the decode request function.
static void genDecodeRequest(deString server, deSignature signature) {
  deStringPuts(server, "\n");
  declareDecodeRequest(server, signature);
  deStringPuts(server, " {\n");
  deSecretType sectype = findRequestSectype(signature);
  declareDecoders(server, sectype, true);
  genDecodeRequestParameters(server, signature);
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    deStringPuts(server,
        "  if (!public_decoder.Finish()) {\n"
        "    return ::sealed::wasm::Status(::sealed::wasm::kInvalidArgument,\n"
        "           \"Failed decoder.Finish\");\n"
        "  }\n");
  }
  if (sectype == DE_SECTYPE_ALL_SECRET || sectype == DE_SECTYPE_MIXED) {
    deStringPuts(server,
        "  if (!secret_decoder.Finish()) {\n"
        "    return ::sealed::wasm::Status(::sealed::wasm::kInvalidArgument,\n"
        "           \"Failed decoder.Finish\");\n"
        "  }\n");
  }
  deStringPuts(server, "  return ::sealed::wasm::Status();\n}\n");
}

// Generate an RPC function served by the server.
static void genServerFunction(deString server, deSignature signature) {
  genDecodeRequest(server, signature);
  genEncodeResponse(server, signature);
  deFunction function = deSignatureGetFunction(signature);
  char *funcName = deFunctionGetName(function);
  deStringSprintf(server,
      "\n"
      "// On success, the response is returned through a call to SendResponse, and\n"
      "// true is returned.  Return false on failure.\n"
      "extern \"C\" int WASM_EXPORT %s_RPC(int32_t request_len, int32_t request_secret_len) {\n"
      "  ::sealed::wasm::ByteString encoded_request(request_len);\n"
      "  ::sealed::wasm::SecretByteString encoded_request_secret(request_secret_len);\n"
      "  biGetRequest(static_cast<void*>(encoded_request.data()), request_len);\n"
      "  biGetRequestSecret(static_cast<void*>(\n"
      "                     encoded_request_secret.data()), request_secret_len);\n"
      , funcName);
  declareDecodeRequestVariables(server, signature);
  deStringPuts(server, "  ::sealed::wasm::Status _status = ");
  genDecodeRequestCall(server, signature);
  deStringPuts(server,
               ";\n"
               "  if (!_status.ok()) {\n"
               "    ::sealed::wasm::SetResponseStatus(_status);\n"
               "    return true;\n"
               "  }\n"
               "  auto _response = ");
  genFunctionCall(server, signature);
  deStringPuts(server,
               "  if (!_response.ok()) {\n"
               "    "
               "::sealed::wasm::SetResponseStatus(::sealed::wasm::Status("
               "_response.code(), _response.message()));\n"
               "    return true;\n"
               "  }\n");

  deDatatype returnType = deSignatureGetReturnType(signature);
  deSecretType sectype = deFindDatatypeSectype(returnType);
  if (returnType != deNoneDatatypeCreate()) {
    deStringSprintf( server,
        "  ::sealed::wasm::%s encoded_response = Encode%sResponse(*_response);\n",
          findSectypeName(sectype), funcName);
  } else {
    deStringSprintf( server,
        "  ::sealed::wasm::%s encoded_response = Encode%sResponse();\n",
          findSectypeName(sectype), funcName);
  }
  deStringPuts(server,
      "  sealed::wasm::SetResponse(encoded_response);\n"
      "  return true;\n"
      "}\n");
}

// Generate code to encode each parameter sent in the RPC.
static void genEncodeRequestParameters(deString client, deSignature signature) {
  deBlock block = deFunctionGetSubBlock(deSignatureGetFunction(signature));
  deVariable param = deBlockGetFirstVariable(block);
  deParamspec paramspec;
  deString string = deMutableStringCreate();
  deForeachSignatureParamspec(signature, paramspec) {
    deStringSetUsed(string, 0);
    deStringPuts(string, deVariableGetName(param));
    encodeParameter(client, string, deParamspecGetDatatype(paramspec));
    param = deVariableGetNextBlockVariable(param);
  } deEndSignatureParamspec;
  deStringDestroy(string);
}

// Generate a function to encode a request.
static void genEncodeRequest(deString client, deSignature signature) {
  deStringPuts(client, "\n");
  declareEncodeRequest(client, signature);
  deStringPuts(client, " {\n");
  deSecretType sectype = findRequestSectype(signature);
  declareEncoders(client, sectype);
  genEncodeRequestParameters(client, signature);
  returnEncodedData(client, sectype);
  deStringPuts(client, "}\n");
}

// Generate a function to decode the response.
static void genDecodeResponse(deString client, deSignature signature) {
  deStringPuts(client, "\n");
  declareDecodeResponse(client, signature);
  deStringPuts(client, " {\n");
  deDatatype returnType = deSignatureGetReturnType(signature);
  deSecretType sectype = deFindDatatypeSectype(returnType);
  declareDecoders(client, sectype, false);
  if (returnType != deNoneDatatypeCreate()) {
    appendDatatypeString(client, returnType);
    deStringPuts(client, " response;\n");
    deString string = deMutableCStringCreate("response");;
    decodeParameter(client, string, returnType);
    deStringDestroy(string);
  }
  deFunction function = deSignatureGetFunction(signature);
  char *functionName = utSprintf("Decode%sRequest", deFunctionGetName(function));
  if (sectype == DE_SECTYPE_ALL_PUBLIC || sectype == DE_SECTYPE_MIXED) {
    deStringSprintf(client,
        "  if (!public_decoder.Finish()) {\n"
        "    return ::sealed::wasm::Status(::sealed::wasm::kInvalidArgument,\n"
        "           \"Failed decoder.Finish in %s\");\n"
        "  }\n", functionName);
  }
  if (sectype == DE_SECTYPE_ALL_SECRET || sectype == DE_SECTYPE_MIXED) {
    deStringSprintf(client,
      "  if (!secret_decoder.Finish()) {\n"
      "    return ::sealed::wasm::Status(::sealed::wasm::kInvalidArgument,\n"
      "           \"Failed decoder.Finish in %s\");\n"
      "  }\n", functionName);
  }
  if (returnType != deNoneDatatypeCreate()) {
    deStringPuts(client, "  return response;\n}\n");
  } else {
    deStringPuts(client, "  return ::sealed::wasm::Status();\n}\n");
  }
}

// Generate code to call the server for the RPC function.
static void genClientFunction(deString client, deSignature signature, char *baseFileName) {
  genEncodeRequest(client, signature);
  genDecodeResponse(client, signature);
  deFunction function = deSignatureGetFunction(signature);
  char *funcName = deFunctionGetName(function);
  deStringSprintf(client,
      "\n"
      "// Call the server for RPC %s.\n",
      funcName);
  declareSignatureFunction(client, signature);
  deSecretType requestSectype = findRequestSectype(signature);
  deStringSprintf(client,
      " {\n"
      "  ::sealed::wasm::%s encoded_request = Encode%sRequest(",
      findSectypeName(requestSectype), funcName);
  printFunctionCallParameters(client, function);
  char *serviceName = utBaseName(utReplaceSuffix(baseFileName, ""));
  deStringSprintf(client,
      ");\n"
      "  ::sealed::wasm::EncodedMessage encoded_response;\n"
      "  SC_RETURN_IF_ERROR(::sealed::wasm::SendRpc(\n"
      "      \"%s\", \"%s\", encoded_request, 0, &encoded_response));\n"
      "  return Decode%sResponse(encoded_response);\n"
      "}\n\n",
      serviceName, funcName, funcName);

  // Write socket function.
  declareSignatureSocketFunction(client, signature);
  deStringSprintf(client, " {\n  EncodedMessage encoded_request = Encode%sRequest(", funcName);
  printFunctionCallParameters(client, function);
  deStringPuts(client,
      ");\n  std::string response;\n"
      "  ::sealed::wasm::SecretByteString response_secret;\n");
  deStringSprintf(
      client,
      "  SC_RETURN_IF_ERROR(::sealed::wasm::SendRpc(\n"
      "      \"%s\", \"%s\",\n"
      "      encoded_request.public_data.string(), encoded_request.secret_data,\n"
      "      &response, &response_secret, socket));\n"
      "  return Decode%sResponse(EncodedMessage(response, response_secret));\n"
      "}\n",
      serviceName, funcName, funcName);
}

// Find all the exported RPC functions and generate code for them.
static void genCCRpcCode(char *rpcDefFile, char *headerFile, char *clientFile, char *serverFile) {
  // We declare all the required type declarations before functions, so we need
  // two header strings to track them.
  deString header = deMutableStringCreate();
  deString server = deMutableStringCreate();
  deString client = deMutableStringCreate();
  startHeaderTop(header, rpcDefFile);
  genServerTop(server, rpcDefFile, headerFile);
  genClientTop(client, rpcDefFile, headerFile);
  deSignature signature;
  deForeachRootSignature(deTheRoot, signature) {
    deFunction function = deSignatureGetFunction(signature);
    if (function != deFunctionNull) {
      if (deFunctionIsRpc(function)) {
        deLine line = deFunctionGetLine(function);
        genHeaderCode(header, signature, line);
        genServerFunction(server, signature);
        genClientFunction(client, signature, rpcDefFile);
      }
    }
  } deEndRootSignature;
  finishHeaderBot(header, rpcDefFile);
  finishServerNamespace(server, rpcDefFile);
  finishClientNamespace(client, rpcDefFile);
  writeStringToFile(rpcDefFile, headerFile, header);
  writeStringToFile(rpcDefFile, serverFile, server);
  writeStringToFile(rpcDefFile, clientFile, client);
  deStringFree(header);
  deStringFree(client);
  deStringFree(server);
}

// Find all the exported RPC functions and generate code for them.
void deGenCCRpcCode(char *rpcDefFile, char *headerFile, char *clientFile, char *serverFile) {
  deStart(rpcDefFile);
  rpcDatabaseStart();
  if (!utSetjmp()) {
    deParseModule(rpcDefFile, deRootGetBlock(deTheRoot), true);
    deBind();
    deBindRPCs();
    genCCRpcCode(rpcDefFile, headerFile, clientFile, serverFile);
    utUnsetjmp();
  } else {
    printf("Exiting due to errors\n");
    exit(1);
  }
  rpcDatabaseStop();
  deStop();
}
