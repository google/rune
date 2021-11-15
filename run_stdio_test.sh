#  Copyright 2021 Google LLC.
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
# Run a single test with optional stdin, and required stdout, and compare the
# output to stdout.
#!/bin/bash

source testing/shbase/googletest.sh

set -x

if [[ $# < 3 ]]; then
  die "Wrong number of argumemts: $#.  Expected <executable> <stdout> <stdin> ..."
fi

target="$1"
stdout="$2"
stdin="$3"
shift 3

if [[ ! -x "$target" ]]; then
  die "$target either does not exist, or is not executable"
fi
if [[ ! -e "$stdout" ]]; then
  die "$stdout does not exist"
fi
if [[ "$stdin" != "nostdin" && ! -e "$stdin" ]]; then
  die "$stdin does not exist"
fi

if [[ "$stdin" != "nostdin" ]]; then
  $target "$@" < "$stdin" > "${TEST_TMPDIR}/result" || die "Failed execution of $target"
else
  $target "$@" > "${TEST_TMPDIR}/result" || die "Failed execution of $target"
fi
cmp "${TEST_TMPDIR}/result" "$stdout" || die "Wrong output for $target"

echo "PASS"
