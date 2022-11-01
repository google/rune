#  Copyright 2022 Google LLC.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#!/bin/bash

set +x

source googletest.sh || exit 1

text_rpc='(255u8, ["Hello", "World!"])'
echo "$text_rpc"
hex_encoding=$(third_party/rune/rpc/encoderpc "$text_rpc")
if [[ $? != 0 ]]; then
  die "Failed in encoderpc"
fi
echo "$hex_encoding"
decoded=$(third_party/rune/rpc/decoderpc '(u8, [string])' "$hex_encoding")
echo "$decoded"
if [[ $? != 0 ]]; then
  die "Failed in decoderpc"
fi
if [[ "$decoded" != "$text_rpc" ]]; then
  die "Decoded output does not match original text RPC"
fi

echo "PASS"
