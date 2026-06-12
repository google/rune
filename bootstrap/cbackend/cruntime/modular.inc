// Modular arithmetic for "expr mod m" expressions, on uint64 values.
// Operands reduce into [0, m) at the leaves; each operator keeps results
// reduced.  Division multiplies by the modular inverse (extended Euclid).

static inline uint64_t mod_reduce(uint64_t v, uint64_t m) {
  return v % m;
}

static inline uint64_t mod_reduce_signed(int64_t v, uint64_t m) {
  int64_t r = v % (int64_t)m;
  if (r < 0) {
    r += (int64_t)m;
  }
  return (uint64_t)r;
}

static inline uint64_t mod_add(uint64_t a, uint64_t b, uint64_t m) {
  return (uint64_t)(((unsigned __int128)a + b) % m);
}

static inline uint64_t mod_sub(uint64_t a, uint64_t b, uint64_t m) {
  return a >= b ? a - b : a + (m - b);
}

static inline uint64_t mod_mul(uint64_t a, uint64_t b, uint64_t m) {
  return (uint64_t)((unsigned __int128)a * b % m);
}

static inline uint64_t mod_neg(uint64_t a, uint64_t m) {
  return a == 0 ? 0 : m - a;
}

static inline uint64_t mod_exp(uint64_t b, uint64_t e, uint64_t m) {
  uint64_t result = 1 % m;
  b %= m;
  while (e) {
    if (e & 1) {
      result = mod_mul(result, b, m);
    }
    b = mod_mul(b, b, m);
    e >>= 1;
  }
  return result;
}

static inline uint64_t mod_inv(uint64_t a, uint64_t m) {
  int64_t t = 0, newt = 1;
  int64_t r = (int64_t)m, newr = (int64_t)(a % m);
  while (newr != 0) {
    int64_t q = r / newr;
    int64_t tmp = t - q * newt;
    t = newt;
    newt = tmp;
    tmp = r - q * newr;
    r = newr;
    newr = tmp;
  }
  if (t < 0) {
    t += (int64_t)m;
  }
  return (uint64_t)t;
}

static inline uint64_t mod_div(uint64_t a, uint64_t b, uint64_t m) {
  return mod_mul(a, mod_inv(b, m), m);
}
