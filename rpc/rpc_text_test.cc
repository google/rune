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

#include "third_party/rune/rpc/rpc_text.h"

#include "third_party/absl/flags/flag.h"
#include "testing/base/public/gunit.h"
#include "third_party/rune/include/de.h"

namespace sealed::rpc {
namespace {

class EncodeDecodeTest
    : public testing::Test,
      public testing::WithParamInterface<std::tuple<const char*, const char*>> {
};

INSTANTIATE_TEST_SUITE_P(
    EncodeDecodeTestSuite, EncodeDecodeTest,
    testing::Values(
        std::make_tuple("u8", "1u8"),
        std::make_tuple("u16", "12345u16"),
        std::make_tuple("u32", "12345678u32"),
        std::make_tuple("u64", "123456789012345u64"),
        std::make_tuple("i8", "-1i8"),
        std::make_tuple("i16", "-12345i16"),
        std::make_tuple("i32", "-12345678i32"),
        std::make_tuple("i64", "-123456789012345i64"),
        std::make_tuple("f32", "3.141592f32"),
        std::make_tuple("f64", "3.141592f64"),
        std::make_tuple("string", "\"Test\""),
        std::make_tuple("(u32, [f64], string)",
                        "(123u32, [0.000000f64, 1.000000f64], \"Test\")")));

TEST_P(EncodeDecodeTest, Examples) {
  std::string rpc_type(std::get<0>(GetParam()));
  std::string rpc_text(std::get<1>(GetParam()));
  auto encoded_rpc = FromText(rpc_type, rpc_text);
  auto decoded_rpc =
      ToText(rpc_type, encoded_rpc->public_data, encoded_rpc->secret_data);
  EXPECT_TRUE(decoded_rpc.ok());
  EXPECT_EQ(rpc_text, *decoded_rpc);
}

TEST(EncodeRequest, SecretLength) {
  const std::string proto_file = absl::GetFlag(FLAGS_test_srcdir) +
      "/google3/third_party/rune/rpc/Test.rn";
  std::string text_proto = "(\"Hello\", secret(\"World\"))";
  auto message = EncodeRequest(proto_file, "Echo", text_proto);
  EXPECT_TRUE(message.ok());
  EXPECT_EQ(message->public_data.length(), message->secret_data.length());
  // The response has the same datatype as the request.
  auto response =  DecodeResponse(proto_file, "Echo", *message);
  EXPECT_TRUE(response.ok());
  EXPECT_EQ(*response, text_proto);
}

}  // namespace
}  // namespace sealed::rpc
