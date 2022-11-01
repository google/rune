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
    std::cout << "Usage: decoderpc <text rpc proto type> <hex public data> "
                 "<hex secret data>\n"
              << "    E.g. decoderpc '(u32, string)' `encoderpc '(123u32, "
                 "\"test\")'`\n";
    return 1;
  }
  auto rpc_text = sealed::rpc::ToText(argv[1], absl::HexStringToBytes(argv[2]),
                                      absl::HexStringToBytes(argv[3]));
  if (!rpc_text.ok()) {
    std::cerr << rpc_text.status().message();
    return 1;
  }
  std::cout << *rpc_text << '\n';
  return 0;
}
