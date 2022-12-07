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

rm -f tests/*.result tests/*.ll newtests/*.result newtests/*.ll

for outFile in newtests/*.stdout; do
  test=$(echo "$outFile" | sed 's/stdout$/rn/')
  resFile=$(echo "$outFile" | sed 's/stdout$/result/')
  inputFile=$(echo "$outFile" | sed 's/stdout$/stdin/')
  executable=$(echo "$test" | sed 's/\.rn$//')
  if [ -e "$inputFile" ]; then
    ./rune -X -b -g "$test" && "./$executable"  > "$resFile" < "$inputFile"
  else
    ./rune -X -b -g "$test" && "./$executable"  > "$resFile"
  fi
  if cmp -s "$outFile" "$resFile"; then
    echo "$test passed"
    numPassed=$((numPassed + 1))
  else
    echo "$test failed *****************************************"
    numFailed=$((numFailed + 1))
  fi
done

for outFile in tests/*.stdout crypto_class/*.stdout; do
  test=$(echo "$outFile" | sed 's/stdout$/rn/')
  resFile=$(echo "$outFile" | sed 's/stdout$/result/')
  inputFile=$(echo "$outFile" | sed 's/stdout$/stdin/')
  if [ -e "$inputFile" ]; then
    ./runl "$test" > "$resFile" < "$inputFile"
  else
    ./runl "$test" > "$resFile"
  fi
  if cmp -s "$outFile" "$resFile"; then
    echo "$test passed"
    numPassed=$((numPassed + 1))
  else
    echo "$test failed *****************************************"
    numFailed=$((numFailed + 1))
  fi
done

for test in errortests/*.rn; do
  result=$(./runl "$test" | egrep "(Exiting due to error|Exception)")
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
