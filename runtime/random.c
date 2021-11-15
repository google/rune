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

#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>

// Read from /dev/urandom.

static FILE *deUrandomFile = NULL;

// Open /dev/urandom.
static inline void openUrandom(void) {
  if (deUrandomFile == NULL) {
    deUrandomFile = fopen("/dev/urandom", "r");
    if (deUrandomFile == NULL) {
      runtime_panicCstr("Unable to open /dev/urandom!");
    }
  }
}

// Generate random bits.
uint64_t runtime_generateTrueRandomValue(uint32_t width) {
  openUrandom();
  uint64_t bits;
  if (fread(&bits, sizeof(bits), 1, deUrandomFile) != 1) {
    runtime_panicCstr("Unable to read from /dev/urandom!");
  }
  if (width < sizeof(uint64_t) * 8) {
    bits = bits & (((uint64_t)1 << width) - 1);
  }
  return bits;
}

// Generate a random bigint.
void runtime_generateTrueRandomBytes(uint8_t *dest, uint64_t numBytes) {
  openUrandom();
  if (fread(dest, numBytes, 1, deUrandomFile) != 1) {
    runtime_panicCstr("Unable to read from /dev/urandom!");
  }
}
