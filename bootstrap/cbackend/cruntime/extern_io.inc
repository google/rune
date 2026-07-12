// Implementations of the extern "C" functions declared by the io package.
// Extern calls are emitted with an rn_ prefix so these definitions never
// collide with the C library's own declarations.

#define RN_GETCWD_BUFFER_SIZE 4096u

static char* rn_getenv(const char *name) {
  char *value = getenv(name);
  return value == NULL ? "" : value;
}

static char* rn_getcwd(void) {
  /* The generic extern-string boundary immediately copies this borrowed
   * result into a headed Rune string. Thread-local storage avoids leaking a
   * separately allocated raw buffer on every call while remaining reentrant
   * across Rune worker threads. */
  static _Thread_local char buf[RN_GETCWD_BUFFER_SIZE];
  if (getcwd(buf, RN_GETCWD_BUFFER_SIZE) == NULL) {
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

// Float formatting (legacy runtime/float.c semantics, %.Ng round-trip).
static inline char *rn_f32tostring_ret(float v) {
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%.9g", v);
  return rn_strfromc(tmp);
}
static inline char *rn_f64tostring_ret(double v) {
  char tmp[64];
  snprintf(tmp, sizeof(tmp), "%.17g", v);
  return rn_strfromc(tmp);
}
