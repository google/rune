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
#include "third_party/rune/rpc/examples/nested_messages_proto.h"

namespace sealed::nested_messages {

bool operator==(const Point& point1, const Point& point2) {
  return point1.x == point2.x && point1.y == point2.y &&
         point1.color == point2.color;
}

bool operator==(const Box& box1, const Box& box2) {
  return box1.lowerLeft == box2.lowerLeft && box1.upperRight == box2.upperRight;
}

using ::sealed::wasm::ByteString;

namespace server {

using ::sealed::wasm::Status;
using ::sealed::wasm::StatusOr;

StatusOr<Box> BoxUnion(const Box& box1, const Box& box2) {
  SC_LOG(FATAL) << "We can't get here";
  return Status(wasm::kInternal, "EchoArray should not be called");
}

}  // namespace server

namespace {

// Test encoding the request, then decoding as a response.
TEST(TestRequestResponse, EncodeDecode) {
  Box box1 = {{0, 0, Color::kRed}, {4, 1}};
  Box box2 = {{1, -1, Color::kRed}, {2, 3, Color::kRed}};
  Box boxUnion = {{0, -1, Color::kRed}, {4, 3, Color::kRed}};
  ByteString encoded_request = client::EncodeBoxUnionRequest(box1, box2);
  Box decodedBox1, decodedBox2;
  EXPECT_OK(
      server::DecodeBoxUnionRequest(encoded_request, decodedBox1, decodedBox2));
  EXPECT_EQ(decodedBox1, box1);
  EXPECT_EQ(decodedBox2, box2);
  ByteString encoded_response = server::EncodeBoxUnionResponse(boxUnion);
  Box decodedBoxUnion = *client::DecodeBoxUnionResponse(encoded_response);
  EXPECT_EQ(decodedBoxUnion, boxUnion);
}

// Verify a point's values are initialized as expected by default.
TEST(TestRequestResponse, CheckDefaultStruct) {
  Point p;
  EXPECT_EQ(p.x, 0);
  EXPECT_EQ(p.y, 0);
  EXPECT_EQ(p.color, Color::kRed);
}

}  // namespace
}  // namespace sealed::nested_messages
