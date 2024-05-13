#include <stdio.h>
#include <float.h>
#include <inttypes.h>

#define STRINGIFY(s) STR(s)
#define STR(s) #s

typedef char *string;

typedef uint8_t bool;

#if defined(DBL_DECIMAL_DIG)
#  define DBL_SIG_DIGITS STRINGIFY(DBL_DECIMAL_DIG)
#  define FLT_SIG_DIGITS STRINGIFY(FLT_DECIMAL_DIG)
#elif defined(DECIMAL_DIG)
#  define DBL_SIG_DIGITS STRINGIFY(DECIMAL_DIG)
#  define FLT_SIG_DIGITS STRINGIFY(DECIMAL_DIG)
#else
#  define DBL_SIG_DIGITS STRINGIFY(DBL_DIG + 3)
#  define FLT_SIG_DIGITS STRINGIFY(FLT_DIG + 3)
#endif


void main(int argc, const char **argv) {
  double b;
  bool c;
  uint64_t a;
  string d;
  a = 1ull;
  b = 3.141;
  c = 1;
  d = "hello";
  printf("a=%" PRIu64 "\n", a);
  printf("b=%." DBL_SIG_DIGITS "g\n", b);
  printf("c=%d\n", c);
  printf("d=\"%s\"\n", d);
}

