// Implementations of the extern "C" functions declared by the io package.
// Extern calls are emitted with an rn_ prefix so these definitions never
// collide with the C library's own declarations.

static char* rn_getenv(const char *name) {
  char *value = getenv(name);
  return value == NULL ? "" : value;
}

static char* rn_getcwd(void) {
  char *buf = calloc(1, 4096);
  if (getcwd(buf, 4096) == NULL) {
    buf[0] = '\0';
  }
  return buf;
}

static uint64_t rn_fopenInternal(const char *fileName, const char *mode) {
  return (uint64_t)(uintptr_t)fopen(fileName, mode);
}

static uint8_t rn_fcloseInternal(uint64_t ptr) {
  return fclose((FILE*)(uintptr_t)ptr) == 0;
}

// The buffer's capacity is its current string length: the caller resizes the
// string to the read size first (strings are NUL-terminated char buffers).
static uint64_t rn_freadInternal(uint64_t ptr, char *buf) {
  size_t capacity = strlen(buf);
  size_t len = fread(buf, 1, capacity, (FILE*)(uintptr_t)ptr);
  if (len < capacity) {
    buf[len] = '\0';
  }
  return (uint64_t)len;
}

static uint8_t rn_fwriteInternal(uint64_t ptr, const char *buf) {
  FILE *file = (FILE*)(uintptr_t)ptr;
  size_t len = strlen(buf);
  uint8_t ok = fwrite(buf, 1, len, file) == len;
  // Destructors are not emitted yet, so files are never closed; flush each
  // write so the data is visible to reads before the process exits.
  fflush(file);
  return ok;
}

static uint8_t rn_ferrorInternal(uint64_t ptr) {
  return ferror((FILE*)(uintptr_t)ptr) != 0;
}
