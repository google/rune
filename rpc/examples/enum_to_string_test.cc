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
#include "third_party/rune/rpc/examples/enum_to_string_proto.h"

namespace sealed::enum_to_string_service {

namespace server {

using ::sealed::enum_to_string_service::Status;
using ::sealed::wasm::StatusOr;

StatusOr<std::string> EnumToString(enum_to_string_service::Status status) {
  SC_LOG(FATAL) << "We can't get here";
  return std::string();
}

}  // namespace server

namespace {

using ::sealed::enum_to_string_service::Status;

// Test encoding/decoding requests.
TEST(TestRequest, EncodeDecode) {
  Status status;
  EXPECT_OK(server::DecodeEnumToStringRequest(
                  client::EncodeEnumToStringRequest(Status::kUnknown), status));
  EXPECT_EQ(status, Status::kUnknown);
  EXPECT_OK(server::DecodeEnumToStringRequest(
      client::EncodeEnumToStringRequest(Status::kInternalError), status));
  EXPECT_EQ(status, Status::kInternalError);
  EXPECT_OK(server::DecodeEnumToStringRequest(
      client::EncodeEnumToStringRequest(Status::kThePrinterIsOnFire), status));
  EXPECT_EQ(status, Status::kThePrinterIsOnFire);
}

// Test encoding/decoding responses.
TEST(TestResponse, EncodeDecode) {
  const std::string kValue("This is a test");
  auto result = client::DecodeEnumToStringResponse(
      server::EncodeEnumToStringResponse(kValue));
  EXPECT_TRUE(result);
  EXPECT_EQ(*result, kValue);
}

}  // namespace
}  // namespace sealed::enum_to_string_service
