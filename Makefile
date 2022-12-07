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

CFLAGS=-Wall -g -std=c11 -Wno-unused-function -Wno-varargs -DMAKEFILE_BUILD -DDD_DEBUG -Iinclude -I../CTTK -Iruntime -no-pie
LIBS=lib/librune.a lib/libcttk.a
LIBS_EXTRA=-lgmp -lm -lddutil-dbg
#CFLAGS=-Wall -O3 -std=c11 -Wno-unused-function -Wno-varargs -DMAKEFILE_BUILD -Iinclude -I../CTTK -Iruntime
#LIBS=lib/librune.a lib/libcttk.a
#LIBS_EXTRA=-lgmp -lm -lddutil

PREFIX="/usr/local"

RUNTIME= \
runtime/array.c \
runtime/bigint.c \
runtime/io.c \
runtime/random.c

SRC= \
database/bigint.c \
database/binding.c \
database/block.c \
database/builtin.c \
database/class.c \
database/datatype.c \
database/dedatabase.c \
database/enum.c \
database/expression.c \
database/filepath.c \
database/float.c \
database/function.c \
database/ident.c \
database/line.c \
database/relation.c \
database/signature.c \
database/statement.c \
database/string.c \
database/util.c \
database/value.c \
database/variable.c \
llvm/debug.c \
llvm/genllvm.c \
llvm/lldatabase.c \
llvm/llvmdecls.c \
parse/deparse.c \
parse/descan.c \
parse/parse.c \
src/bind.c \
src/bind2.c \
src/bindexpr.c \
src/constprop.c \
src/generator.c \
src/iterator.c \
src/main.c \
src/memmanage.c \
src/rune.c

DEPS=Makefile
CC=gcc
OBJS=$(patsubst %.c,obj/%.o,$(SRC))

rune: $(OBJS) $(LIBS)
	$(CC) $(CFLAGS) -o rune $(OBJS) lib/libcttk.a $(LIBS) $(LIBS_EXTRA)

$(OBJS): obj

all: obj rune schema

lib/libcttk.a:
	mkdir -p lib
	cd ../CTTK; make; mv build/libcttk.a ../rune/lib
	cd ../CTTK; make clean

lib/librune.a: runtime/librune.a
	mkdir -p lib
	cp runtime/librune.a lib

runtime/librune.a: $(RUNTIME)
	cd runtime; make librune.a

schema: Rune.ps LLVM.ps

database/dedatabase.c: include/dedatabase.h

include/dedatabase.h: database/Rune.dd
	datadraw -h include/dedatabase.h database/Rune.dd

llvm/lldatabase.c: llvm/lldatabase.h

llvm/lldatabase.h: llvm/LLVM.dd include/dedatabase.h
	datadraw -I database llvm/LLVM.dd

rpc/rpcdatabase.c: rpc/rpcdatabase.h

rpc/rpcdatabase.h: rpc/Rpc.dd include/dedatabase.h
	datadraw -I database rpc/Rpc.dd

parse/deparse.c: parse/deparse.h

parse/deparse.h: parse/deparse.y
	bison -d -Dapi.prefix="{de}" -o parse/deparse.c parse/deparse.y

parse/descan.c: parse/descan.l parse/deparse.h
	flex -o parse/descan.c parse/descan.l

Rune.ps Codegen.ps: database/Rune.dd
	dataview database/Rune.dd

LLVM.ps: llvm/LLVM.dd
	dataview llvm/LLVM.dd

check: rune
	./runtests.sh

install: rune
	install -d $(PREFIX)/bin $(PREFIX)/lib/rune $(PREFIX)/lib/rune/runtime
	install rune $(PREFIX)/bin
	install lib/libcttk.a $(PREFIX)/lib/rune
	install lib/librune.a $(PREFIX)/lib/rune
	cp -r builtin $(PREFIX)/lib/rune
	cp -r math $(PREFIX)/lib/rune
	cp -r io $(PREFIX)/lib/rune
	install runtime/package.rn $(PREFIX)/lib/rune/runtime

clean:
	rm -rf obj lib rune */*database.[ch] *.ps parse/descan.c parse/deparse.[ch] rune.log tests/*.ll tests/*.result newtests/*.ll newtests/*.result crypto_class/*.ll crypto_class/*.result errortests/*.ll
	for file in tests/*.rn crypto_class/*.rn errortests/*.rn; do exeFile=$$(echo "$$file" | sed 's/.rn$$//'); rm -f "$$exeFile"; done
	cd runtime ; make clean
	cd bootstrap/database ; make clean

obj: include/dedatabase.h llvm/lldatabase.h
	mkdir -p obj/database obj/llvm obj/parse obj/runtime obj/src obj/util obj/rpc

obj/%.o: %.c $(DEPS)
	$(CC) $(CFLAGS) -c -o $@ $<
	@$(CC) -MM $(CFLAGS) $< | sed 's|^.*:|$@:|' > $(patsubst %.o,%.d,$@)

-include $(OBJS:.o=.d)
