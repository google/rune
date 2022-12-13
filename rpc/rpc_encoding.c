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

// Convert Rune constants such as ((1u16, "test"), [1.0, 2.0]) to encoded RPCs
// ready to be sent over the wire.  Use the //third_party/sealedcomputing/rpc
// package for encoding/decoding.

#include "de.h"
#include "third_party/sealedcomputing/rpc/rpc.h"

// Encode an integer.  Legal sizes are 8, 16, 32, and 64.
static void encodeInteger(RpcEncoderContext *ctx, deExpression integerExpr) {
  deDatatype datatype = deExpressionGetDatatype(integerExpr);
  deBigint bigint = deExpressionGetBigint(integerExpr);
  uint32 width = deBigintGetWidth(bigint);
  utAssert(width == deDatatypeGetWidth(datatype));
  utAssert(deBigintSigned(bigint) == deDatatypeSigned(datatype));
  if (deBigintSigned(bigint)) {
    int64 val = deBigintGetInt64(bigint, 0);
    switch (width) {
      case 8:
        if ((int8)val != val) {
          deError(0, "Integer does not fit in an i8");
        }
        rpcEncodeS8(ctx, val);
        break;
      case 16:
        if ((int16)val != val) {
          deError(0, "Integer does not fit in an i16");
        }
        rpcEncodeS16(ctx, val);
        break;
      case 32:
        if ((int32)val != val) {
          deError(0, "Integer does not fit in an i32");
        }
        rpcEncodeS32(ctx, val);
        break;
      case 64:
        rpcEncodeS64(ctx, val);
        break;
      default:
        deError(0, "Invalid integer width: expected 8, 16, 32, or 64.  Got %u", width);
    }
  } else {
    uint64 val = deBigintGetUint64(bigint, 0);
    switch (width) {
      case 8:
        if ((uint8)val != val) {
          deError(0, "Integer does not fit in a u8");
        }
        rpcEncodeU8(ctx, val);
        break;
      case 16:
        if ((uint16)val != val) {
          deError(0, "Integer does not fit in a u16");
        }
        rpcEncodeU16(ctx, val);
        break;
      case 32:
        if ((uint32)val != val) {
          deError(0, "Integer does not fit in a u32");
        }
        rpcEncodeU32(ctx, val);
        break;
      case 64:
        rpcEncodeU64(ctx, val);
        break;
      default:
        deError(0, "Invalid integer width: expected 8, 16, 32, or 64.  Got %u", width);
    }
  }
}

// Encode a floating point constant.
static void encodeFloat(RpcEncoderContext *ctx, deExpression constValExpr) {
  deFloat theFloat = deExpressionGetFloat(constValExpr);
  switch (deFloatGetType(theFloat)) {
    case DE_FLOAT_SINGLE:
      rpcEncodeF32(ctx, deFloatGetValue(theFloat));
      break;
    case DE_FLOAT_DOUBLE:
      rpcEncodeF64(ctx, deFloatGetValue(theFloat));
      break;
  }
}

// Encode a string.
static void encodeString(RpcEncoderContext *ctx, deExpression constValExpr) {
  deString string = deExpressionGetString(constValExpr);
  rpcEncodeStartArray(ctx, deStringGetNumText(string));
  for (uint32_t i = 0; i < deStringGetNumText(string); i++) {
    rpcEncodeS8(ctx, deStringGetiText(string, i));
  }
  rpcEncodeFinishArray(ctx);
}

// Forward declaration for recursion.
static void encodeConstExpression(RpcEncoderContext *publicCtx,
    RpcEncoderContext *secretCtx, deExpression constValExpr);

// Encode a constant array.
static void encodeArray(RpcEncoderContext *publicCtx,
    RpcEncoderContext *secretCtx, deExpression constValExpr) {
  deSecretType sectype = deFindDatatypeSectype(deExpressionGetDatatype(constValExpr));
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    rpcEncodeStartArray(publicCtx, deExpressionCountExpressions(constValExpr));
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    rpcEncodeStartArray(secretCtx, deExpressionCountExpressions(constValExpr));
  }
  deExpression child;
  deForeachExpressionExpression(constValExpr, child) {
    encodeConstExpression(publicCtx, secretCtx, child);
  } deEndExpressionExpression;
  if (sectype != DE_SECTYPE_ALL_SECRET) {
    rpcEncodeFinishArray(publicCtx);
  }
  if (sectype != DE_SECTYPE_ALL_PUBLIC) {
    rpcEncodeFinishArray(secretCtx);
  }
}

// Encode a constant tuple.
static void encodeTuple(RpcEncoderContext *publicCtx,
    RpcEncoderContext *secretCtx, deExpression constValExpr) {
  rpcEncodeStartStructure(publicCtx);
  rpcEncodeStartStructure(secretCtx);
  deExpression child;
  deForeachExpressionExpression(constValExpr, child) {
    encodeConstExpression(publicCtx, secretCtx, child);
  } deEndExpressionExpression;
  rpcEncodeFinishStructure(publicCtx);
  rpcEncodeFinishStructure(secretCtx);
}

// Traverse the expression tree, setting datatypes to secret.
static void markExpressionSecret(deExpression expression) {
  deExpressionSetDatatype(expression,
      deSetDatatypeSecret(deExpressionGetDatatype(expression), true));
  deExpression child;
  deForeachExpressionExpression(expression, child) {
    markExpressionSecret(child);
  } deEndExpressionExpression;
}

// Recursively set the expression datatype to secret, and encode the
// sub-expression.
static void encodeSecret(RpcEncoderContext *publicCtx, RpcEncoderContext *secretCtx,
    deExpression constValExpr) {
  deExpression child = deExpressionGetFirstExpression(constValExpr);
  markExpressionSecret(child);
  encodeConstExpression(publicCtx, secretCtx, child);
}

// Encode the constant expression.
static void encodeConstExpression(RpcEncoderContext *publicCtx,
    RpcEncoderContext *secretCtx, deExpression constValExpr) {
  deDatatype datatype = deExpressionGetDatatype(constValExpr);
  RpcEncoderContext *defaultCtx = deDatatypeSecret(datatype)? secretCtx : publicCtx;
  switch (deExpressionGetType(constValExpr)) {
    case DE_EXPR_INTEGER:
      encodeInteger(defaultCtx, constValExpr);
      break;
    case DE_EXPR_FLOAT:
      encodeFloat(defaultCtx, constValExpr);
      break;
    case DE_EXPR_BOOL:
      rpcEncodeU8(defaultCtx, deExpressionBoolVal(constValExpr));
      break;
    case DE_EXPR_STRING:
      encodeString(defaultCtx, constValExpr);
      break;
    case DE_EXPR_ARRAY:
      encodeArray(publicCtx, secretCtx, constValExpr);
      break;
    case DE_EXPR_TUPLE:
      encodeTuple(publicCtx, secretCtx, constValExpr);
      break;
    case DE_EXPR_SECRET:
      encodeSecret(publicCtx, secretCtx, constValExpr);
      break;
    default:
      deError(0, "Unable to convert expression to RPC encoding format.");
  }
}

// Encode the constant expression in Sealed Computing RPC format.
static void constExpressionToRpc(deExpression constValExpr,
    deString *publicData, deString *secretData) {
  RpcEncoderContext publicCtx, secretCtx;
  rpcInitEncoderContext(&publicCtx, true);
  rpcInitEncoderContext(&secretCtx, false);
  encodeConstExpression(&publicCtx, &secretCtx, constValExpr);
  uint8 *publicBuf;
  RpcLengthType publicLength;
  uint8 *secretBuf;
  RpcLengthType secretLength;
  rpcFinishEncoding(&publicCtx, &publicBuf, &publicLength);
  rpcFinishEncoding(&secretCtx, &secretBuf, &secretLength);
  *publicData = deStringCreate(publicBuf, publicLength);
  *secretData = deStringCreate(secretBuf, secretLength);
  rpcFreeEncoderContext(&publicCtx);
  rpcFreeEncoderContext(&secretCtx);
}

// Return the datatype represented by |dataType|.
static deDatatype parseDatatype(char *dataType) {
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  utSym name = deBlockCreateUniqueName(rootBlock, utSymCreate("rpcdef"));
  deFunction function = deFunctionCreate(deFilepathNull, rootBlock, DE_FUNC_PACKAGE,
      name, DE_LINK_PACKAGE, 0);
  deBlock block = deFunctionGetSubBlock(function);
  // Text should be formatted as a Rune type expression, e.g. [u32].
  char *full_text = utAllocString(utCatStrings("type = ", dataType));
  deParseString(full_text, block);
  utFree(full_text);
  deStatement statement = deBlockGetFirstStatement(block);
  deExpression assignExpr = deStatementGetExpression(statement);
  deBindExpression(block, assignExpr);
  deConstantPropagation(block, block);
  deExpression datatypeExpr = deExpressionGetLastExpression(assignExpr);
  deDatatype datatype = deExpressionGetDatatype(datatypeExpr);
  deFunctionDestroy(function);
  return datatype;
}

// Encode a message using the datatype to verify the formatting of the text.
static bool encodeMessage(deDatatype datatype, char *message, deString *publicData, deString *secretData) {
  deBlock rootBlock = deRootGetBlock(deTheRoot);
  utSym name = deBlockCreateUniqueName(rootBlock, utSymCreate("rpcdef"));
  deFunction function = deFunctionCreate(deFilepathNull, rootBlock, DE_FUNC_PACKAGE,
      name, DE_LINK_PACKAGE, 0);
  deBlock block = deFunctionGetSubBlock(function);
  // Text should be formatted as a Rune constant.
  char *full_text = utAllocString(utCatStrings("val = ", message));
  deParseString(full_text, block);
  utFree(full_text);
  deStatement statement = deBlockGetFirstStatement(block);
  deExpression assignExpr = deStatementGetExpression(statement);
  deBindExpression(block, assignExpr);
  deExpression constValExpr = deExpressionGetLastExpression(assignExpr);
  if (deExpressionGetDatatype(constValExpr) != datatype) {
    deError(0, "Type of text proto does not match type expression.");
    return false;
  }
  deConstantPropagation(block, block);
  constExpressionToRpc(constValExpr, publicData, secretData);
  deFunctionDestroy(function);
  return true;
}

// Encode the Rune constant in Sealed Computing RPC format.
bool deEncodeTextRpc(char *dataType, char *text, deString *publicData, deString *secretData) {
  deDatatype datatype = parseDatatype(dataType);
  return encodeMessage(datatype, text, publicData, secretData);
}

// Decode a Boolean value, which is encoded as a u8 0 or 1.
static deExpression decodeBool(RpcDecoderContext *ctx) {
  uint8 value;
  if (!rpcDecodeU8(ctx, &value) || value > 1) {
    deError(0, "Unable to decode Boolean value");
  }
  return deBoolExpressionCreate(value, 0);
}

// Decode a string.  Return NULL if we fail.
static uint8_t *decodeBytes(RpcDecoderContext* ctx, RpcLengthType *len) {
  if (!rpcDecodeStartArray(ctx, len)) {
    return NULL;
  }
  // Allocate 1 extra for a terminating '\0', in case this is a string.
  uint8* bytes = (uint8*)calloc(*len + 1, sizeof(uint8));
  if (bytes == NULL) {
    return NULL;
  }
  for (RpcLengthType i = 0; i < *len; i++) {
    if (!rpcDecodeU8(ctx, &bytes[i])) {
      free(bytes);
      return NULL;
    }
  }
  bytes[*len] = '\0';
  if (!rpcDecodeFinishArray(ctx)) {
    free(bytes);
    return NULL;
  }
  return bytes;
}

// Decode a string.
static deExpression decodeString(RpcDecoderContext *ctx) {
  RpcLengthType len;
  uint8 *bytes = decodeBytes(ctx, &len);
  if (bytes == NULL) {
    deError(0, "Unable to decode string");
  }
  deString string = deMutableStringCreate();
  deStringSetText(string, bytes, len);
  deStringSetUsed(string, len);
  free(bytes);
  return deStringExpressionCreate(string, 0);
}

// Decode a u8.
static deExpression decodeU8(RpcDecoderContext *ctx) {
  uint8 value;
  if (!rpcDecodeU8(ctx, &value)) {
    deError(0, "Unable to decode u8 value");
  }
  deBigint bigint = deUint8BigintCreate(value);
  return deIntegerExpressionCreate(bigint, 0);
}

// Decode a u16.
static deExpression decodeU16(RpcDecoderContext *ctx) {
  uint16 value;
  if (!rpcDecodeU16(ctx, &value)) {
    deError(0, "Unable to decode u16 value");
  }
  deBigint bigint = deUint16BigintCreate(value);
  return deIntegerExpressionCreate(bigint, 0);
}

// Decode a u32.
static deExpression decodeU32(RpcDecoderContext *ctx) {
  uint32 value;
  if (!rpcDecodeU32(ctx, &value)) {
    deError(0, "Unable to decode u32 value");
  }
  deBigint bigint = deUint32BigintCreate(value);
  return deIntegerExpressionCreate(bigint, 0);
}

// Decode a u64.
static deExpression decodeU64(RpcDecoderContext *ctx) {
  uint64 value;
  if (!rpcDecodeU64(ctx, &value)) {
    deError(0, "Unable to decode u64 value");
  }
  deBigint bigint = deUint64BigintCreate(value);
  return deIntegerExpressionCreate(bigint, 0);
}

// Decode an unsigned integer.
static deExpression decodeUint(RpcDecoderContext *ctx, deDatatype datatype) {
  switch (deDatatypeGetWidth(datatype)) {
    case 8:
      return decodeU8(ctx);
    case 16:
      return decodeU16(ctx);
    case 32:
      return decodeU32(ctx);
    case 64:
      return decodeU64(ctx);
    default:
      deError(0, "Integer widths must be 8, 16, 32, or 64 bits");
  }
  return deExpressionNull;  // Dummy return.
}

// Decode an i8.
static deExpression decodeI8(RpcDecoderContext *ctx) {
  int8 value;
  if (!rpcDecodeS8(ctx, &value)) {
    deError(0, "Unable to decode i8 value");
  }
  deBigint bigint = deInt8BigintCreate(value);
  return deIntegerExpressionCreate(bigint, 0);
}

// Decode an i16.
static deExpression decodeI16(RpcDecoderContext *ctx) {
  int16 value;
  if (!rpcDecodeS16(ctx, &value)) {
    deError(0, "Unable to decode i16 value");
  }
  deBigint bigint = deInt16BigintCreate(value);
  return deIntegerExpressionCreate(bigint, 0);
}

// Decode an i32.
static deExpression decodeI32(RpcDecoderContext *ctx) {
  int32 value;
  if (!rpcDecodeS32(ctx, &value)) {
    deError(0, "Unable to decode i32 value");
  }
  deBigint bigint = deInt32BigintCreate(value);
  return deIntegerExpressionCreate(bigint, 0);
}

// Decode an i64.
static deExpression decodeI64(RpcDecoderContext *ctx) {
  int64 value;
  if (!rpcDecodeS64(ctx, &value)) {
    deError(0, "Unable to decode i64 value");
  }
  deBigint bigint = deInt64BigintCreate(value);
  return deIntegerExpressionCreate(bigint, 0);
}

// Decode a signed integer.
static deExpression decodeInt(RpcDecoderContext *ctx, deDatatype datatype) {
  switch (deDatatypeGetWidth(datatype)) {
    case 8:
      return decodeI8(ctx);
    case 16:
      return decodeI16(ctx);
    case 32:
      return decodeI32(ctx);
    case 64:
      return decodeI64(ctx);
    default:
      deError(0, "Integer widths must be 8, 16, 32, or 64 bits");
  }
  return deExpressionNull;  // Dummy return.
}

// Decode a floating point value.
static deExpression decodeFloat(RpcDecoderContext *ctx, deDatatype datatype) {
  uint32 width = deDatatypeGetWidth(datatype);
  if (width == 32) {
    float value;
    if (!rpcDecodeF32(ctx, &value)) {
      deError(0, "Unable to decode f32 value");
    }
    return deFloatExpressionCreate(deFloatCreate(DE_FLOAT_SINGLE, value), 0);
  } else if (width == 64) {
    double value;
    if (!rpcDecodeF64(ctx, &value)) {
      deError(0, "Unable to decode f64 value");
    }
    return deFloatExpressionCreate(deFloatCreate(DE_FLOAT_DOUBLE, value), 0);
  }
  utExit("Unexpected floating point type");
  return deExpressionNull;  // Dummy return.
}

// Forward declaration for recursion.
static deExpression rpcToConstExpression(RpcDecoderContext *publicCtx,
    RpcDecoderContext *secretCtx, deDatatype datatype, bool secret);

// Decode an array.
static deExpression decodeArray(RpcDecoderContext *publicCtx,
    RpcDecoderContext *secretCtx, deDatatype datatype, bool secret) {
  RpcLengthType numElements;
  deSecretType sectype = deFindDatatypeSectype(datatype);
  if ((sectype != DE_SECTYPE_ALL_SECRET && !rpcDecodeStartArray(publicCtx, &numElements)) ||
      (sectype != DE_SECTYPE_ALL_PUBLIC && !rpcDecodeStartArray(secretCtx, &numElements))) {
    deError(0, "Unable to decode array");
  }
  deExpression arrayExpr = deExpressionCreate(DE_EXPR_ARRAY, 0);
  deDatatype elementType = deDatatypeGetElementType(datatype);
  for (RpcLengthType i = 0; i < numElements; i++) {
    deExpression elementExpr = rpcToConstExpression(publicCtx, secretCtx, elementType, secret);
    deExpressionAppendExpression(arrayExpr, elementExpr);
  }
  if ((sectype != DE_SECTYPE_ALL_SECRET && !rpcDecodeFinishArray(publicCtx)) ||
      (sectype != DE_SECTYPE_ALL_PUBLIC && !rpcDecodeFinishArray(secretCtx))) {
    deError(0, "Unable to decode array");
  }
  return arrayExpr;
}

// Decode the tuple.
static deExpression decodeTuple(RpcDecoderContext *publicCtx,
    RpcDecoderContext *secretCtx, deDatatype datatype, bool secret) {
  rpcDecodeStartStructure(publicCtx);
  rpcDecodeStartStructure(secretCtx);
  deExpression tupleExpr = deExpressionCreate(DE_EXPR_TUPLE, 0);
  deDatatype elementType;
  deForeachDatatypeTypeList(datatype, elementType) {
    deExpression elementExpr =
        rpcToConstExpression(publicCtx, secretCtx, elementType, secret);
    deExpressionAppendExpression(tupleExpr, elementExpr);
  } deEndDatatypeTypeList;
  rpcDecodeFinishStructure(publicCtx);
  rpcDecodeFinishStructure(secretCtx);
  return tupleExpr;
}

// Add a secret cast.
static deExpression decodeSecret(RpcDecoderContext *publicCtx,
    RpcDecoderContext *secretCtx, deDatatype datatype) {
  deExpression result = rpcToConstExpression(publicCtx, secretCtx, datatype, true);
  deExpression secretExpr = deExpressionCreate(DE_EXPR_SECRET, deLineNull);
  deExpressionAppendExpression(secretExpr, result);
  return secretExpr;
}

// Parse the encoded RPC to a constant Rune expression. |secret| means the outer
// data type is secret.  We need to introduce a secret(...) expression only when
// the outer the outer datatype is not secret and the current datatype is secret.
static deExpression rpcToConstExpression(RpcDecoderContext *publicCtx,
    RpcDecoderContext *secretCtx, deDatatype datatype, bool secret) {
  if (!secret && deDatatypeSecret(datatype)) {
    return decodeSecret(publicCtx, secretCtx, datatype);
  }
  deDatatypeType type = deDatatypeGetType(datatype);
  RpcDecoderContext *defaultCtx = deDatatypeSecret(datatype)? secretCtx : publicCtx;
  switch (type) {
    case DE_TYPE_BOOL:
      return decodeBool(defaultCtx);
    case DE_TYPE_STRING:
      return decodeString(defaultCtx);
    case DE_TYPE_UINT:
      return decodeUint(defaultCtx, datatype);
    case DE_TYPE_INT:
      return decodeInt(defaultCtx, datatype);
    case DE_TYPE_FLOAT:
      return decodeFloat(defaultCtx, datatype);
    case DE_TYPE_ARRAY:
      return decodeArray(publicCtx, secretCtx, datatype, secret);
    case DE_TYPE_TUPLE:
      return decodeTuple(publicCtx, secretCtx, datatype, secret);
    default:
      deError(0, "Unsupported data type %s", deDatatypeTypeGetName(type));
  }
  return deExpressionNull;  // Dummy return.
}

// Decode a text buffer in Sealed Computing RPC format to a constant Rune
// expression, and then convert it to text.
static deString decodeMessage(deDatatype datatype, uint8 *publicData, uint32 publicLen,
    uint8 *secretData, uint32 secretLen) {
  RpcDecoderContext publicCtx, secretCtx;
  rpcInitDecoderContext(&publicCtx, true, publicData, publicLen);
  rpcInitDecoderContext(&secretCtx, false, secretData, secretLen);
  deExpression constValExpr = rpcToConstExpression(&publicCtx, &secretCtx, datatype, false);
  if (!rpcFinishDecoding(&publicCtx) ||
      !rpcFinishDecoding(&secretCtx)) {
    deError(0, "Failed to decode entire encoded RPC");
  }
  rpcFreeDecoderContext(&publicCtx);
  rpcFreeDecoderContext(&secretCtx);
  deString rpcText = deExpressionToString(constValExpr);
  return rpcText;
}

// Decode a text buffer in Sealed Computing RPC format to a constant Rune
// expression, and then convert it to text.
deString deDecodeTextRpc(char *dataType, uint8 *publicData, uint32 publicLen,
    uint8 *secretData, uint32 secretLen) {
  deDatatype datatype = parseDatatype(dataType);
  return decodeMessage(datatype, publicData, publicLen, secretData, secretLen);
}

// Find an RPC function declared at the top level with the given name.
static deFunction findMethod(deBlock module, char *method) {
  deIdent ident = deBlockFindIdent(module, utSymCreate(method));
  if (ident == deIdentNull || deIdentGetType(ident) != DE_IDENT_FUNCTION) {
    return deFunctionNull;
  }
  deFunction function = deIdentGetFunction(ident);
  if (deFunctionGetLinkage(function) != DE_LINK_EXTERN_RPC) {
    return deFunctionNull;
  }
  return function;
}

// Read the proto.rn file and find the method.  Return  the sub-block for the
// method's function.
static deFunction findMethodFunction(char *protoFileName, char *method) {
  deBlock module = deParseModule(protoFileName, deRootGetBlock(deTheRoot), true);
  deBind();
  deBindRPCs();
  deFunction function = findMethod(module, method);
  if (function == deFunctionNull) {
    deError(0, "No method name %s found in proto definition.", method);
  }
  return function;
}

// Encode a request for a given method in a proto.
bool deEncodeRequest(char *protoFileName, char *method, char *textRequest,
  deString *publicData, deString *secretData) {
  deBlock block = deFunctionGetSubBlock(findMethodFunction(protoFileName, method));
  deDatatypeArray paramTypes = deFindFullySpecifiedParameters(block);
  deDatatype tupleType = deTupleDatatypeCreate(paramTypes);
  return encodeMessage(tupleType, textRequest, publicData, secretData);
}

// Decode a response for a given method in a proto.
deString deDecodeResponse(char *protoFileName, char *method, uint8 *publicData,
    uint32 publicLen, uint8 *secretData, uint32 secretLen) {
  deFunction function = findMethodFunction(protoFileName, method);
  deSignature signature = deFunctionGetFirstSignature(function);
  utAssert(signature != deSignatureNull);
  utAssert(deSignatureGetNextFunctionSignature(signature) == deSignatureNull);
  deDatatype returnType = deSignatureGetReturnType(signature);
  return decodeMessage(returnType, publicData, publicLen, secretData, secretLen);
}
