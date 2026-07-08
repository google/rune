// Build a Rune array of strings from main()'s argc/argv.
// Returns char** (not void*) so that indexing the result, e.g.
// rn_argv_array(argc, argv)[1], yields a char* rather than void.
static inline char **rn_argv_array(int argc, const char **argv) {
  char **arr = (char **)array_alloc(sizeof(char *), (size_t)argc);
  for (int i = 0; i < argc; i++) {
    arr[i] = (char *)argv[i];
  }
  return arr;
}
