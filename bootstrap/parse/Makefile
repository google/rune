#  Copyright 2023 Google LLC.
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

RUNE=../../rune
RNFLAGS=-g -r ..

RUNE_TESTS=char lexer token keytab pegparser

all: runtests

runtests: $(RUNE_TESTS)
	for test in $(RUNE_TESTS); do "./$$test"; done

char: char.rn
	$(RUNE) $(RNFLAGS) char.rn

lexer: lexer.rn char.rn
	$(RUNE) $(RNFLAGS) lexer.rn

token: token.rn
	$(RUNE) $(RNFLAGS) token.rn

keytab: keytab.rn
	$(RUNE) $(RNFLAGS) keytab.rn

pegparser: pegparser.rn lexer.rn token.rn char.rn keytab.rn
	$(RUNE) $(RNFLAGS) pegparser.rn

clean:
	rm -f $(RUNE_TESTS) *.ll parsergen
