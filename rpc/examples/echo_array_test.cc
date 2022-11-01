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

#include <cstdint>
#include <string>

#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"
#include "third_party/rune/rpc/examples/echo_array_proto.h"
#include "third_party/sealedcomputing/wasm3/status.h"

namespace sealed::echo_array {

namespace server {

using ::sealed::wasm::Status;
using ::sealed::wasm::StatusOr;

StatusOr<std::vector<uint32_t>> EchoArray(const std::vector<uint32_t>& array) {
  LOG(FATAL) << "We can't get here";
  return Status(wasm::kInternal, "EchoArray should not be called");
}

}  // namespace server

namespace {

// Test encoding the request, then decoding as a response.
TEST(TestRequestResponse, EncodeDecode) {
  std::vector<uint32_t> request = {1, 2, 3};
  wasm::ByteString encoded_request = client::EncodeEchoArrayRequest(request);
  // The request and response have the same format.
  std::vector<uint32_t> response =
      *client::DecodeEchoArrayResponse(encoded_request);
  EXPECT_EQ(request, response);
  wasm::ByteString encoded_response = server::EncodeEchoArrayResponse(request);
  // The request and response have the same format.
  EXPECT_OK(server::DecodeEchoArrayRequest(encoded_response, response));
  EXPECT_EQ(request, response);
}

}  // namespace
}  // namespace sealed::echo_array
