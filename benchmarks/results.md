# Heap queues
The C++ std::priority_queue is currently ~3X faster at sorting integers.
However, this is basically useless as heapsort is very slow compared to mergsort
or qsort.  A more interesting case is a heapq of objects.  Rune was the same
speed once C++ had in-place objects containging a std::string and a uint32_t
cost.  Objects with more fields are slower in C++, and should in stead be
inserted as unique pointers.

In C++, std::priority_queue does not yet support unique pointers, so I was not
able to finish the benchmark.

waywardgeek@waywardgeek2:~/fig/rune/google3/experimental/waywardgeek/rune/benchmarks$ time ./fh
1514776932
1514776932
1514776932
1514776932
1514776932
1514776932
1514776932
1514776932
1514776932
1514776932

real	0m4.072s
user	0m3.726s
sys	0m0.351s
waywardgeek@waywardgeek2:~/fig/rune/google3/experimental/waywardgeek/rune/benchmarks$ time ./priority_queue
1514776932
1514776932
1514776932
1514776932
1514776932
1514776932
1514776932
1514776932
1514776932
1514776932

real	0m4.064s
user	0m4.028s
sys	0m0.034s

Pretty close to a tie.
