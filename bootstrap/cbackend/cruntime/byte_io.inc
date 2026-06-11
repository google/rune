/*
 * Rune C backend byte-level stdin/stdout builtins.
 *
 * Implements the Rune builtins readByte(), writeByte(), readBytes() and
 * writeBytes().  The helper names match the Rune builtin names so calls can
 * be emitted directly.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline uint8_t readByte(void) {
  int c = getchar();
  return c == EOF ? (uint8_t)0 : (uint8_t)c;
}

static inline void writeByte(uint8_t b) {
  putchar(b);
  fflush(stdout);
}

static inline char *readBytes(uint64_t n) {
  char *buf = (char *)malloc((size_t)n + 1);
  size_t got = fread(buf, 1, (size_t)n, stdin);
  buf[got] = '\0';
  return buf;
}

static inline void writeBytes(const char *s) {
  fwrite(s, 1, strlen(s), stdout);
  fflush(stdout);
}
