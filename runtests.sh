#!/bin/bash
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

numPassed="0"
numFailed="0"

rm -f tests/*.result tests/*.ll

for outFile in tests/*.stdout; do
  test=$(echo "$outFile" | sed 's/stdout$/rn/')
  resFile=$(echo "$outFile" | sed 's/stdout$/result/')
  inputFile=$(echo "$outFile" | sed 's/stdout$/stdin/')
  argsFile=$(echo "$outFile" | sed 's/stdout$/args/')
  executable=$(echo "$test" | sed 's/\.rn$//')
  args="-g"
  if [ -e "$argsFile" ]; then
    args=`cat $argsFile`
  fi
  if [ -e "$inputFile" ]; then
    ./rune "$args" "$test" && "./$executable"  > "$resFile" < "$inputFile"
  else
    ./rune "$args" "$test" && "./$executable"  > "$resFile"
  fi
  # Compare ignoring Windows line endings.  Avoid "sed -i", whose syntax
  # differs between GNU and BSD sed.
  if cmp -s <(sed 's/\r$//' "$outFile") <(sed 's/\r$//' "$resFile"); then
    echo "$test passed"
    numPassed=$((numPassed + 1))
  else
    echo "$test failed *****************************************"
    numFailed=$((numFailed + 1))
  fi
done

for test in errortests/*.rn; do
  executable=$(echo "$test" | sed 's/\.rn$//')
  result=$(./rune "$args" -x "$test" | grep "Exiting due to error")
  if [[ "$result" == "" ]]; then
    result=$("./$executable" | grep -E "(Exception|Panic)")
  fi
  if [[ "$result" != "" ]]; then
    echo "$test passed"
    numPassed=$((numPassed + 1))
  else
    echo "*************************************** $test failed"
    numFailed=$((numFailed + 1))
  fi
done

echo "Passed: $numPassed"
echo "Failed: $numFailed"

if [ "$numFailed" -ne 0 ]; then
  exit 1
fi
