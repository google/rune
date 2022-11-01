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

#include "third_party/rune/rpc/examples/echo_secret_proto.h"
#include "third_party/sealedcomputing/wasm3/base.h"
#include "third_party/sealedcomputing/wasm3/logging.h"
#include "third_party/sealedcomputing/wasm3/statusor.h"

using ::sealed::wasm::StatusOr;

namespace sealed::echo_secret::server {

StatusOr<std::string> EchoSecret(const std::string& secretMessage) {
  return secretMessage;
}

}  // namespace sealed::echo_secret::server

extern "C" int start() {
  sealed::wasm::Serve();
  return 0;
}
