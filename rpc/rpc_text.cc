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

#include <string>

#include "third_party/rune/include/de.h"

namespace sealed::rpc {

absl::StatusOr<EncodedMessage> FromText(const std::string& type_string,
                                        const std::string& text) {
  deStart("dummy filename");
  deString public_data, secret_data;
  if (!deEncodeTextRpc(const_cast<char*>(type_string.c_str()),
                       const_cast<char*>(text.c_str()), &public_data,
                       &secret_data)) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Encoding text failed.");
  }
  auto result = EncodedMessage(
      std::string(reinterpret_cast<char*>(deStringGetText(public_data)),
                  deStringGetNumText(public_data)),
      std::string(reinterpret_cast<char*>(deStringGetText(secret_data)),
                  deStringGetNumText(secret_data)));
  deStop();
  return result;
}

absl::StatusOr<std::string> ToText(const std::string& type_string,
                                   const std::string& public_data,
                                   const std::string& secret_data) {
  deStart("dummy filename");
  uint8_t* publicBytes = const_cast<uint8_t*>(
      reinterpret_cast<const uint8_t*>(public_data.data()));
  uint8_t* secretBytes = const_cast<uint8_t*>(
      reinterpret_cast<const uint8_t*>(secret_data.data()));
  deString rpc_text =
      deDecodeTextRpc(const_cast<char*>(type_string.c_str()), publicBytes,
                      public_data.length(), secretBytes, secret_data.length());
  std::string result =
      std::string(reinterpret_cast<char*>(deStringGetText(rpc_text)),
                  deStringGetUsed(rpc_text));
  deStop();
  return result;
}

absl::StatusOr<EncodedMessage> EncodeRequest(const std::string& proto_file_name,
                                             const std::string& rpc_method,
                                             const std::string& text_request) {
  deString public_data, secret_data;
  deStart(const_cast<char*>(proto_file_name.c_str()));
  if (!deEncodeRequest(const_cast<char*>(proto_file_name.c_str()),
                       const_cast<char*>(rpc_method.c_str()),
                       const_cast<char*>(text_request.c_str()), &public_data,
                       &secret_data)) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Encoding request failed.");
  }
  auto result = EncodedMessage(
      std::string(reinterpret_cast<char*>(deStringGetText(public_data)),
                  deStringGetNumText(public_data)),
      std::string(reinterpret_cast<char*>(deStringGetText(secret_data)),
                  deStringGetNumText(secret_data)));
  deStop();
  return result;
}

// Decode a response from a given method defined in a proto definition.
absl::StatusOr<std::string> DecodeResponse(
    const std::string& proto_file_name, const std::string& rpc_method,
    const EncodedMessage& encoded_response) {
  deStart(const_cast<char*>(proto_file_name.c_str()));
  uint8_t* public_data = const_cast<uint8_t*>(
      reinterpret_cast<const uint8_t*>(encoded_response.public_data.data()));
  uint8_t* secret_data = const_cast<uint8_t*>(
      reinterpret_cast<const uint8_t*>(encoded_response.secret_data.data()));
  deString decoded_response =
      deDecodeResponse(const_cast<char*>(proto_file_name.c_str()),
                       const_cast<char*>(rpc_method.c_str()), public_data,
                       encoded_response.public_data.size(), secret_data,
                       encoded_response.secret_data.size());
  if (decoded_response == deStringNull) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Decoding response failed.");
  }
  auto result =
      std::string(reinterpret_cast<char*>(deStringGetText(decoded_response)),
                  deStringGetUsed(decoded_response));
  deStop();
  return result;
}

}  // namespace sealed::rpc
