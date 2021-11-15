CPP=clang++-9
CCFLAGS=-Wall -O3

all: priority_queue fh binary_trees_cc

priority_queue: priority_queue.cc
	$(CPP) $(CCFLAGS) -o priority_queue priority_queue.cc

fh: fh.rn
	rune -U -O fh.rn

binary_trees_cc: binary_trees.cc
	clang++ -O3 binary_trees.cc -o binary_trees_cc

clean:
	rm priority_queue fh
