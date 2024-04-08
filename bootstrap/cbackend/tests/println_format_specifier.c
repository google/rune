#include <stdio.h>
#include <float.h>
#include <inttypes.h>

#define STRINGIFY(s) STR(s)
#define STR(s) #s

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
  printf("This is a string: \"%s\"\n", "hello");
  printf("This is an int: %" PRId8 "\n", (int8_t)(-1));
  printf("This is an unsigned int: %" PRIu16 "\n", (uint16_t)(65535u));
  printf("This is a hex value: 0x%" PRIx32 "\n", (uint32_t)(3735928559ul));
  printf("This is a 64-bit int: %" PRIu64 "\n", 1ull);
  printf("This is a 64-bit signed int: %" PRId64 "\n", -1ull);
  printf("This is a float: %." FLT_SIG_DIGITS "g\n", 2.7182817500000001);
  printf("This is a negative float: %." DBL_SIG_DIGITS "g\n", -3.1415926535897931);
}

