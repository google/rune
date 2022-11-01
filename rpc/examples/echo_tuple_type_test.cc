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

#include "testing/base/public/gunit.h"
#include "third_party/rune/rpc/examples/echo_tuple_type_proto.h"

using Tuple =
    std::tuple<bool, float, double, uint8_t, int8_t, uint16_t, int16_t,
               uint32_t, int32_t, uint64_t, int64_t, std::string>;
using sealed::wasm::ByteString;

namespace sealed::echo_tuple_type {
namespace {

// Test encoding the request, then decoding as a response.
TEST(TestRequestResponse, EncodeDecode) {
  Tuple request = {true,     1.234,         1.55555555555,  123,
                   -123,     12345,         -12345,         1234567,
                   -1234567, 1234567890123, -1234567890123, "This is a test"};
  wasm::ByteString encoded_request = client::EncodeEchoRequest(request);
  // The request and response have the same format.
  Tuple response = *client::DecodeEchoResponse(encoded_request);
  EXPECT_EQ(request, response);
}

}  // namespace
}  // namespace sealed::echo_tuple_type
