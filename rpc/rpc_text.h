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

#ifndef THIRD_PARTY_RUNE_RPC_RPC_TEXT_H_
#define THIRD_PARTY_RUNE_RPC_RPC_TEXT_H_

#include <string>
#include "third_party/absl/status/statusor.h"

namespace sealed::rpc {

struct EncodedMessage {
  EncodedMessage(const std::string& public_data, const std::string& secret_data)
      : public_data(public_data), secret_data(secret_data) {}
  std::string public_data;
  std::string secret_data;
};

// Encode a text RPC proto to binary.  |text| is a Rune-formatted data structure
// consisting of tuples, arrays, strings, f32, f64, and integers of type s8, u8,
// s16, u16, s32, u32, s64, and u64.  E.g. (["one", "two"], ["three"], ([1u32,
// 2u32], 3.0f32)).  |type_string| similarly is a Rune datatype, e.g. ([string],
// [string], ([u32], f32)).
absl::StatusOr<EncodedMessage> FromText(const std::string& type_string,
                                        const std::string& text);

// Decode a text RPC proto from binary to text.  |type_string| is a
// Rune-formatted type string describing the contents of the encoded RPC.  On
// failure, a non-OK status is returned.
absl::StatusOr<std::string> ToText(const std::string& type_string,
                                   const std::string& public_data,
                                   const std::string& secret_data);

// Encode a request for a given method defined in a proto definition.
absl::StatusOr<EncodedMessage> EncodeRequest(const std::string& proto_file_name,
                                             const std::string& rpc_method,
                                             const std::string& text_request);

// Decode a response from a given method defined in a proto definition.
absl::StatusOr<std::string> DecodeResponse(
    const std::string& proto_file_name, const std::string& rpc_method,
    const EncodedMessage& encoded_response);

}  // namespace sealed::rpc

#endif  // THIRD_PARTY_RUNE_RPC_RPC_TEXT_H_
