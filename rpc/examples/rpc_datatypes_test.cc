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

#include <string>

#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"
#include "third_party/rune/rpc/examples/rpc_datatypes_proto.h"

namespace sealed::rpc_datatypes {
namespace {

// Test encoding the request, then decoding as a response.
TEST(TestRequestResponse, EncodeDecode) {
  const double kPi = 3.14159265359;
  BasicTypes request = {
      .boolVal = true,
      .f32Val = 3.13f,
      .f64Val = kPi,
      .u8Val = 123,
      .i8Val = -123,
      .u16Val = 0xdead,
      .i16Val = -12345,
      .u32Val = 0xdeadbeef,
      .i32Val = -12345678,
      .u64Val = 0xdeadbeefdeadbeeflu,
      .i64Val = -1234567890123l,
      .stringVal = "This is a test",
  };
  ::sealed::wasm::ByteString encoded_request =
      client::EncodeEchoRequest(request);
  auto response_or = client::DecodeEchoResponse(encoded_request);
  EXPECT_TRUE(response_or);
  BasicTypes response = *response_or;
  EXPECT_EQ(request.boolVal, response.boolVal);
  EXPECT_EQ(request.f32Val, response.f32Val);
  EXPECT_EQ(request.f64Val, response.f64Val);
  EXPECT_EQ(request.u8Val, response.u8Val);
  EXPECT_EQ(request.i8Val, response.i8Val);
  EXPECT_EQ(request.u16Val, response.u16Val);
  EXPECT_EQ(request.i16Val, response.i16Val);
  EXPECT_EQ(request.u32Val, response.u32Val);
  EXPECT_EQ(request.i32Val, response.i32Val);
  EXPECT_EQ(request.u64Val, response.u64Val);
  EXPECT_EQ(request.i64Val, response.i64Val);
  EXPECT_EQ(request.stringVal, response.stringVal);
}

}  // namespace
}  // namespace sealed::rpc_datatypes
