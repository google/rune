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

#include "testing/base/public/gmock.h"
#include "testing/base/public/gunit.h"
#include "third_party/rune/rpc/examples/echo_mixed_proto.h"

namespace sealed::echo_mixed {

namespace server {

using ::sealed::wasm::StatusOr;

StatusOr<std::string> EchoMixed(const std::string mixedMessage) {
  SC_LOG(FATAL) << "We can't get here";
  return std::string();
}

}  // namespace server

namespace {

// Test encoding/decoding requests.
TEST(TestRequest, EncodeDecode) {
  const std::string kPublic("This is a test");
  const std::string kSecret("Don't tell anyone!");
  MixedMessage decoded_message;
  EXPECT_OK(server::DecodeEchoMixedRequest(
      client::EncodeEchoMixedRequest(MixedMessage{kPublic, kSecret}),
      decoded_message));
  EXPECT_EQ(decoded_message.publicData, kPublic);
  EXPECT_EQ(decoded_message.secretData, kSecret);
}

// Test encoding/decoding responses.
TEST(TestResponse, EncodeDecode) {
  const std::string kPublic("This is a test");
  const std::string kSecret("Don't tell anyone!");
  auto result = client::DecodeEchoMixedResponse(
      server::EncodeEchoMixedResponse(MixedMessage{kPublic, kSecret}));
  EXPECT_TRUE(result);
  EXPECT_EQ((*result).publicData, kPublic);
  EXPECT_EQ((*result).secretData, kSecret);
}

}  // namespace
}  // namespace sealed::echo_secret
