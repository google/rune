CFLAGS=-Wall -g -std=c11 -Wno-varargs -I../../CTTK -DRN_DEBUG
#CFLAGS=-Wall -O3 -std=c11 -Wno-varargs -I../../cttk
CC=clang

SRC= \
array.c \
bigint.c \
io.c \
random.c

HDRS= \
runtime.h \
../../CTTK/cttk.h

OBJ=$(SRC:.c=.o)

all: librune.a runtime_test

librune.a: $(OBJ)
	$(AR) cqs librune.a $(OBJ)

$(OBJ): $(SRC) $(HDRS)
	$(CC) $(CFLAGS) -c $(SRC)

runtime_test: runtime_test.c $(SRC) $(HDRS) librune.a ../lib/libcttk.a
	$(CC) $(CFLAGS) -o runtime_test runtime_test.c $(SRC) librune.a ../lib/libcttk.a

../lib/libcttk.a:
	cd ..; make lib/libcttk.a

clean:
	rm -f runtime_test librune.a *.o *.ll
