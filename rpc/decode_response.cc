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
  if (argc != 5) {
    std::cout << "Usage: decode_response <sealed proto file> <method name> "
                 "<public hex data> <secret hex data>\n";
    return 1;
  }

  auto response = DecodeResponse(
      argv[1], argv[2],
      ::sealed::rpc::EncodedMessage(absl::HexStringToBytes(argv[3]),
                                    absl::HexStringToBytes(argv[4])));
  if (!response.ok()) {
    std::cerr << "An error occured while decoding the message.\n";
    return 1;
  }
  std::cout << *response << "\n";
  return 0;
}
