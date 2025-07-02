#  Copyright 2024 Google LLC.
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

# Tests for the rune "bootstrap" compiler.
# Note that the original C compiler is technically the bootstrap compiler,
# this is the rune-compiler-in-rune, which will ultimately generate C code
# for itself.  This C code will eventually replace the original C compiler
# as the bootstrap.
RUNE=../rune

ROOT:=$(dir $(realpath $(MAKEFILE_LIST)))../

RUNE_TESTS=\
	arrays.test\
	assignments.test\
	binary_operators.test\
	else_if_stmt.test\
	else_stmt.test\
	for_stmt.test\
	functions.test\
	helloworld.test\
	if_stmt.test\
	println_format_specifier.test\
	println_values.test\
	struct.test\
	tuples.test

runtests: $(RUNE_TESTS)

%.test: tests/%.rn
	@echo Testing $*.rn...
	@$(RUNE) -q -p $(ROOT) tests/$*.rn
	@diff <(tests/$*) tests/$*.stdout


clean: $(RUNE_TESTS:%.test=%.clean)

%.clean: tests/%.rn
	@rm tests/$* tests/$*.c
