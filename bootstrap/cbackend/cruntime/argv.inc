// Build a Rune array of strings from main()'s argc/argv.
static inline void *rn_argv_array(int argc, const char **argv) {
  char **arr = (char **)array_alloc(sizeof(char *), (size_t)argc);
  for (int i = 0; i < argc; i++) {
    arr[i] = (char *)argv[i];
  }
  return arr;
}
