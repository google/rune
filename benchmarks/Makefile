# Benchmark references + timing. The Rune programs are built by the bootstrap
# compiler (see bench.sh / README.md); this Makefile builds the C/C++ oracles.
CC      ?= clang
CXX     ?= clang++
CFLAGS  ?= -O3 -Wall
LDLIBS  ?= -lm

REFS = n_body_ref fannkuch_redux_ref spectral_norm_ref mandelbrot_ref \
       fasta_ref reverse_complement_ref k_nucleotide_ref binary_trees_cc

all: $(REFS)

%_ref: %_ref.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

binary_trees_cc: binary_trees.cc
	$(CXX) $(CFLAGS) -o $@ $<

# Build every Rune program with the bootstrap compiler and time it vs its C ref.
bench:
	bash bench.sh

clean:
	rm -f $(REFS)

.PHONY: all bench clean
