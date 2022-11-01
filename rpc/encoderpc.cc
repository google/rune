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

#include <iostream.h>

#include "base/init_google.h"
#include "third_party/absl/strings/escaping.h"
#include "third_party/rune/include/de.h"
#include "third_party/rune/rpc/rpc_text.h"

int main(int argc, char **argv) {
  InitGoogle(argv[0], &argc, &argv, true);
  if (argc != 3) {
    std::cout << "Usage: encoderpc <text rpc proto type> <text rpc proto>\n";
    return 1;
  }
  auto encoded_rpc = sealed::rpc::FromText(argv[1], argv[2]);
  if (!encoded_rpc.ok()) {
    std::cerr << "An error occured while encoding the message.\n";
    return 1;
  }
  std::cout << absl::BytesToHexString(encoded_rpc->public_data) << " "
            << absl::BytesToHexString(encoded_rpc->secret_data) << '\n';
  return 0;
}
