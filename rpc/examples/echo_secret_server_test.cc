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
#include "third_party/rune/rpc/examples/echo_secret_proto.h"

namespace sealed::echo_secret {
namespace {

// Test encoding/decoding requests.
TEST(TestRequest, EncodeDecode) {
  const std::string kSecret("Don't tell anyone!");
  std::string decoded_secret;
  EXPECT_OK(server::DecodeEchoSecretRequest(
      client::EncodeEchoSecretRequest(kSecret), decoded_secret));
  EXPECT_EQ(kSecret, decoded_secret);
}

// Test encoding/decoding responses.
TEST(TestResponse, EncodeDecode) {
  const std::string kSecret("This is a test");
  auto result = client::DecodeEchoSecretResponse(
      server::EncodeEchoSecretResponse(kSecret));
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, kSecret);
}

}  // namespace
}  // namespace sealed::echo_secret
