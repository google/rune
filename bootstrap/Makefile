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

RUNE=../rune
SRC=\
database/bigint.rn\
database/block.rn\
database/class.rn\
database/datatype.rn\
database/expr.rn\
database/filepath.rn\
database/function.rn\
database/ident.rn\
database/location.rn\
database/package.rn\
database/paramspec.rn\
database/rel.rn\
database/root.rn\
database/signature.rn\
database/statement.rn\
database/template.rn\
database/trans.rn\
database/util.rn\
database/value.rn\
database/variable.rn\
parse/char.rn\
parse/hir.rn\
parse/exprTree.rn\
parse/keytab.rn\
parse/lexer.rn\
parse/package.rn\
parse/pegparser.rn\
parse/rule.rn\
parse/token.rn\
cbackend/cbuilder.rn\
cbackend/cemitter.rn\
cbackend/cfragment.rn\
cbackend/clanguageclasses.rn\
cbackend/cruntime.rn\
cbackend/ctypegen.rn\
cbackend/escapestring.rn\
cbackend/int2cstring.rn\
cbackend/package.rn\
types/builtins.rn\
types/package.rn\
types/typebuilder.rn\
types/typechecker.rn\
types/typeclasses.rn\
types/typeerror.rn\
types/typeunifier.rn\
utils/cwstringsearch.rn\
utils/float2string.rn\
utils/intmath.rn\
utils/kmpstringsearch.rn\
utils/stringfinder.rn\
utils/stringstream.rn\
utils/stringwriter.rn\
rune.rn

rune: $(SRC)
	$(RUNE) -g rune.rn

test: $(SRCS)
	$(RUNE) -g -t rune.rn

clean:
	rm -f rune rune.ll rune.log
