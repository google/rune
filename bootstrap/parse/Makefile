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

RUNE_TESTS=char lexer token keytab pegparser hir

SRC= \
char.rn \
exprTree.rn \
hir.rn \
keytab.rn \
lexer.rn \
package.rn \
pegparser.rn \
rule.rn \
token.rn

all: runtests

runtests: $(RUNE_TESTS)
	for test in $(RUNE_TESTS); do "./$$test"; done

char: $(SRC)
	$(RUNE) $(RNFLAGS) char.rn

lexer: $(SRC)
	$(RUNE) $(RNFLAGS) lexer.rn

token: $(SRC)
	$(RUNE) $(RNFLAGS) token.rn

keytab: $(SRC)
	$(RUNE) $(RNFLAGS) keytab.rn

pegparser: $(SRC)
	$(RUNE) $(RNFLAGS) pegparser.rn

hir: $(SRC)
	$(RUNE) $(RNFLAGS) hir.rn

clean:
	rm -f $(RUNE_TESTS) *.ll
