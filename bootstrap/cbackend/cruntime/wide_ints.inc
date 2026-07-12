// Integers wider than 64 bits: heap-allocated little-endian uint64_t limb
// arrays with a header word carrying the width and signedness.  Values are
// immutable; every operation allocates a fresh result.  Signed values are
// stored in two's complement within their width; bits above the width are
// always zero.

typedef uint64_t *rn_wide;
// Declarations emit defined type names with a _t suffix.
typedef uint64_t *rn_wide_t;

#define WIDE_WIDTH(v) ((uint64_t)((v)[0] & 0xffffffffu))
#define WIDE_SIGNED(v) ((int)(((v)[0] >> 32) & 1))
#define WIDE_NLIMBS(w) ((int)(((w) + 63) / 64))

static inline rn_wide wide_alloc(uint64_t width, int issigned) {
  rn_wide v = (uint64_t *)calloc((size_t)WIDE_NLIMBS(width) + 1, sizeof(uint64_t));
  v[0] = width | ((uint64_t)(issigned ? 1 : 0) << 32);
  return v;
}

static inline void wide_mask(rn_wide v) {
  uint64_t w = WIDE_WIDTH(v);
  int rem = (int)(w % 64);
  if (rem) {
    v[WIDE_NLIMBS(w)] &= (((uint64_t)1 << rem) - 1);
  }
}

static inline int wide_signbit(rn_wide v) {
  uint64_t w = WIDE_WIDTH(v);
  return (int)((v[1 + (int)((w - 1) / 64)] >> ((w - 1) % 64)) & 1);
}

static inline int wide_is_neg(rn_wide v) {
  return WIDE_SIGNED(v) && wide_signbit(v);
}

static inline rn_wide wide_from_u64(uint64_t x, uint64_t width, int issigned) {
  rn_wide v = wide_alloc(width, issigned);
  v[1] = x;
  if (issigned && (int64_t)x < 0) {
    for (int i = 2; i <= WIDE_NLIMBS(width); i++) {
      v[i] = ~(uint64_t)0;
    }
  }
  wide_mask(v);
  return v;
}

// Parse |len| little-endian bytes of |data| into a wide integer of the
// given width (String.toUintLE(<type>)).  Byte i contributes to bit i*8.
static inline rn_wide wide_from_le_bytes(const char *data, uint64_t len,
                                         uint64_t width, int issigned) {
  rn_wide v = wide_alloc(width, issigned);
  for (uint64_t i = 0; i < len; i++) {
    uint64_t bit = i * 8;
    if (bit < width) {
      v[1 + bit / 64] |= (uint64_t)(uint8_t)data[i] << (bit % 64);
    }
  }
  wide_mask(v);
  return v;
}

// The low |nbytes| bytes of |v| little-endian as a length-headed string
// (wide toStringLE()).  Embedded NUL bytes are preserved via the header.
static inline char *wide_tostring_le(rn_wide v, uint64_t nbytes) {
  char *r = rn_stralloc((size_t)nbytes);
  for (uint64_t i = 0; i < nbytes; i++) {
    uint64_t bit = i * 8;
    r[i] = (char)((v[1 + bit / 64] >> (bit % 64)) & 0xff);
  }
  return r;
}

static inline uint64_t wide_to_u64(rn_wide v) {
  uint64_t w = WIDE_WIDTH(v);
  uint64_t x = v[1];
  if (wide_is_neg(v)) {
    // Sign-extend into 64 bits.
    int rem = (int)(w < 64 ? w : 64);
    if (rem < 64) {
      x |= ~(((uint64_t)1 << rem) - 1);
    }
  }
  return x;
}

static inline rn_wide wide_fromhex(uint64_t width, int issigned, const char *hex) {
  rn_wide v = wide_alloc(width, issigned);
  int bit = 0;
  for (const char *p = hex + strlen(hex) - 1; p >= hex; p--, bit += 4) {
    uint64_t d;
    if (*p >= '0' && *p <= '9') {
      d = (uint64_t)(*p - '0');
    } else if (*p >= 'a' && *p <= 'f') {
      d = (uint64_t)(*p - 'a' + 10);
    } else {
      d = (uint64_t)(*p - 'A' + 10);
    }
    if (bit < (int)width) {
      v[1 + bit / 64] |= d << (bit % 64);
    }
  }
  wide_mask(v);
  return v;
}

// Cast to a new width/signedness; |trunc| suppresses the range check.
static inline rn_wide wide_cast(rn_wide a, uint64_t width, int issigned, int trunc) {
  rn_wide r = wide_alloc(width, issigned);
  int na = WIDE_NLIMBS(WIDE_WIDTH(a));
  int nr = WIDE_NLIMBS(width);
  int neg = wide_is_neg(a);
  for (int i = 1; i <= nr; i++) {
    r[i] = i <= na ? a[i] : (neg ? ~(uint64_t)0 : 0);
  }
  // Sign-extend a negative source through its top limb's unused bits.
  if (neg) {
    uint64_t wa = WIDE_WIDTH(a);
    int rem = (int)(wa % 64);
    if (rem && na <= nr) {
      r[na] |= ~(((uint64_t)1 << rem) - 1);
    }
  }
  wide_mask(r);
  if (!trunc) {
    // Verify the value round-trips: widen back and compare.
    if (width < WIDE_WIDTH(a)) {
      int nrm = WIDE_NLIMBS(width);
      rn_wide back = wide_alloc(WIDE_WIDTH(a), issigned);
      int negr = issigned && ((r[1 + (int)((width - 1) / 64)] >> ((width - 1) % 64)) & 1);
      for (int i = 1; i <= na; i++) {
        back[i] = i <= nrm ? r[i] : (negr ? ~(uint64_t)0 : 0);
      }
      if (negr) {
        int rem = (int)(width % 64);
        if (rem) {
          back[nrm] |= ~(((uint64_t)1 << rem) - 1);
        }
      }
      wide_mask(back);
      for (int i = 1; i <= na; i++) {
        if (back[i] != a[i]) {
          raise("Overflow");
        }
      }
      free(back);
    }
  }
  return r;
}

// Compare limbs as unsigned magnitudes.
static inline int wide_cmp_limbs(rn_wide a, rn_wide b) {
  int n = WIDE_NLIMBS(WIDE_WIDTH(a));
  for (int i = n; i >= 1; i--) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}

static inline int wide_cmp(rn_wide a, rn_wide b) {
  if (WIDE_SIGNED(a) || WIDE_SIGNED(b)) {
    int na = wide_is_neg(a), nb = wide_is_neg(b);
    if (na != nb) {
      return na ? -1 : 1;
    }
  }
  return wide_cmp_limbs(a, b);
}

static inline uint8_t wide_eq(rn_wide a, rn_wide b) {
  return wide_cmp_limbs(a, b) == 0;
}

static inline rn_wide wide_addsub(rn_wide a, rn_wide b, int sub, int trunc) {
  uint64_t w = WIDE_WIDTH(a);
  int n = WIDE_NLIMBS(w);
  rn_wide r = wide_alloc(w, WIDE_SIGNED(a) || WIDE_SIGNED(b));
  unsigned __int128 carry = sub ? 1 : 0;
  for (int i = 1; i <= n; i++) {
    unsigned __int128 t = (unsigned __int128)a[i] + (sub ? (uint64_t)~b[i] : b[i]) + carry;
    r[i] = (uint64_t)t;
    carry = t >> 64;
  }
  int sa = wide_is_neg(a), sb = wide_is_neg(b) ^ (sub ? 1 : 0);
  wide_mask(r);
  if (!trunc) {
    if (WIDE_SIGNED(r)) {
      int sr = wide_signbit(r);
      // Negating b flips its sign except for zero; good enough for the
      // overflow check on same-sign inputs.
      if (sa == sb && sr != sa) {
        raise(sub ? "Underflow" : "Overflow");
      }
    } else {
      // Unsigned: subtraction may not borrow; addition may not carry past
      // the width.
      int rem = (int)(w % 64);
      uint64_t over = rem ? (r[n] >> rem) : (uint64_t)carry;
      if (sub) {
        if (wide_cmp_limbs(b, a) > 0) {
          raise("Underflow");
        }
      } else if (over || (rem == 0 && carry)) {
        raise("Overflow");
      }
    }
    wide_mask(r);
  }
  wide_mask(r);
  return r;
}

static inline rn_wide wide_add(rn_wide a, rn_wide b) { return wide_addsub(a, b, 0, 0); }
static inline rn_wide wide_sub(rn_wide a, rn_wide b) { return wide_addsub(a, b, 1, 0); }
static inline rn_wide wide_addtrunc(rn_wide a, rn_wide b) { return wide_addsub(a, b, 0, 1); }
static inline rn_wide wide_subtrunc(rn_wide a, rn_wide b) { return wide_addsub(a, b, 1, 1); }

static inline rn_wide wide_negtrunc(rn_wide a) {
  rn_wide zero = wide_alloc(WIDE_WIDTH(a), 1);
  rn_wide r = wide_addsub(zero, a, 1, 1);
  free(zero);
  return r;
}

static inline rn_wide wide_neg(rn_wide a) {
  rn_wide zero = wide_alloc(WIDE_WIDTH(a), 1);
  rn_wide r = wide_addsub(zero, a, 1, !WIDE_SIGNED(a));
  free(zero);
  return r;
}

// Multiply into a double-width temporary; check overflow unless |trunc|.
// Signed operands multiply via magnitudes.
static inline rn_wide wide_abs(rn_wide a) {
  return wide_is_neg(a) ? wide_negtrunc(a) : a;
}

static inline rn_wide wide_mul(rn_wide a, rn_wide b, int trunc) {
  uint64_t w = WIDE_WIDTH(a);
  int n = WIDE_NLIMBS(w);
  int negr = 0;
  rn_wide ma = a, mb = b;
  if (WIDE_SIGNED(a) || WIDE_SIGNED(b)) {
    negr = wide_is_neg(a) ^ wide_is_neg(b);
    ma = wide_abs(a);
    mb = wide_abs(b);
  }
  uint64_t *t = (uint64_t *)calloc((size_t)(2 * n + 1), sizeof(uint64_t));
  for (int i = 0; i < n; i++) {
    unsigned __int128 carry = 0;
    for (int j = 0; j < n; j++) {
      unsigned __int128 cur = (unsigned __int128)ma[1 + i] * mb[1 + j] + t[i + j] + carry;
      t[i + j] = (uint64_t)cur;
      carry = cur >> 64;
    }
    t[i + n] = (uint64_t)carry;
  }
  rn_wide r = wide_alloc(w, WIDE_SIGNED(a) || WIDE_SIGNED(b));
  for (int i = 0; i < n; i++) {
    r[1 + i] = t[i];
  }
  if (!trunc) {
    // Any magnitude bits at or above the width overflowed.
    int rem = (int)(w % 64);
    uint64_t high = rem ? (t[n - 1] >> rem) : 0;
    if (WIDE_SIGNED(r) && rem == 0) {
      high = t[n - 1] >> 63;
    } else if (WIDE_SIGNED(r) && rem) {
      high = t[n - 1] >> (rem - 1);
    }
    for (int i = n; i < 2 * n; i++) {
      high |= t[i];
    }
    if (high) {
      raise("Overflow");
    }
  }
  free(t);
  if (negr) {
    rn_wide nr = wide_negtrunc(r);
    free(r);
    r = nr;
  }
  wide_mask(r);
  return r;
}

// Bit-by-bit long division of magnitudes: q = a / b, rem = a % b.
static inline void wide_divmod(rn_wide a, rn_wide b, rn_wide *qp, rn_wide *remp) {
  // Operate at the wider of the two operand widths so neither's high limbs
  // are read out of bounds or silently dropped.  In modular arithmetic the
  // modulus is frequently a different width than the dividend (e.g. reducing
  // a 2**256 value modulo a 255-bit prime).
  rn_wide divmodWideA = (rn_wide)0;
  rn_wide divmodWideB = (rn_wide)0;
  if (WIDE_WIDTH(a) != WIDE_WIDTH(b)) {
    uint64_t divmodW = WIDE_WIDTH(a) > WIDE_WIDTH(b) ? WIDE_WIDTH(a) : WIDE_WIDTH(b);
    divmodWideA = wide_cast(a, divmodW, WIDE_SIGNED(a), 1);
    divmodWideB = wide_cast(b, divmodW, WIDE_SIGNED(b), 1);
    a = divmodWideA;
    b = divmodWideB;
  }
  uint64_t w = WIDE_WIDTH(a);
  int n = WIDE_NLIMBS(w);
  int allz = 1;
  for (int i = 1; i <= n; i++) {
    if (b[i]) {
      allz = 0;
    }
  }
  if (allz) {
    raise("DivByZero");
  }
  int negq = 0, negr = 0;
  rn_wide ma = a, mb = b;
  if (WIDE_SIGNED(a) || WIDE_SIGNED(b)) {
    negq = wide_is_neg(a) ^ wide_is_neg(b);
    negr = wide_is_neg(a);
    ma = wide_abs(a);
    mb = wide_abs(b);
  }
  rn_wide q = wide_alloc(w, WIDE_SIGNED(a) || WIDE_SIGNED(b));
  rn_wide rem = wide_alloc(w, WIDE_SIGNED(a) || WIDE_SIGNED(b));
  for (int bit = (int)w - 1; bit >= 0; bit--) {
    // rem = (rem << 1) | bit_of(ma, bit)
    uint64_t carry = (ma[1 + bit / 64] >> (bit % 64)) & 1;
    for (int i = 1; i <= n; i++) {
      uint64_t nc = rem[i] >> 63;
      rem[i] = (rem[i] << 1) | carry;
      carry = nc;
    }
    wide_mask(rem);
    if (wide_cmp_limbs(rem, mb) >= 0) {
      // rem -= mb
      unsigned __int128 c = 1;
      for (int i = 1; i <= n; i++) {
        unsigned __int128 t = (unsigned __int128)rem[i] + (uint64_t)~mb[i] + c;
        rem[i] = (uint64_t)t;
        c = t >> 64;
      }
      wide_mask(rem);
      q[1 + bit / 64] |= (uint64_t)1 << (bit % 64);
    }
  }
  if (negq) {
    rn_wide t = wide_negtrunc(q);
    free(q);
    q = t;
  }
  if (negr) {
    rn_wide t = wide_negtrunc(rem);
    free(rem);
    rem = t;
  }
  if (divmodWideA) {
    free(divmodWideA);
  }
  if (divmodWideB) {
    free(divmodWideB);
  }
  if (qp) {
    *qp = q;
  } else {
    free(q);
  }
  if (remp) {
    *remp = rem;
  } else {
    free(rem);
  }
}

static inline rn_wide wide_div(rn_wide a, rn_wide b) {
  rn_wide q;
  wide_divmod(a, b, &q, 0);
  return q;
}

static inline rn_wide wide_rem(rn_wide a, rn_wide b) {
  rn_wide rem;
  wide_divmod(a, b, 0, &rem);
  return rem;
}

static inline rn_wide wide_shl(rn_wide a, uint64_t k) {
  uint64_t w = WIDE_WIDTH(a);
  int n = WIDE_NLIMBS(w);
  rn_wide r = wide_alloc(w, WIDE_SIGNED(a));
  for (int bit = (int)w - 1; bit >= (int)k; bit--) {
    int src = bit - (int)k;
    uint64_t d = (a[1 + src / 64] >> (src % 64)) & 1;
    r[1 + bit / 64] |= d << (bit % 64);
  }
  (void)n;
  return r;
}

static inline rn_wide wide_shr(rn_wide a, uint64_t k) {
  uint64_t w = WIDE_WIDTH(a);
  rn_wide r = wide_alloc(w, WIDE_SIGNED(a));
  int neg = wide_is_neg(a);
  for (int bit = 0; (uint64_t)bit < w; bit++) {
    uint64_t src = (uint64_t)bit + k;
    uint64_t d = src < w ? (a[1 + src / 64] >> (src % 64)) & 1 : (uint64_t)(neg ? 1 : 0);
    r[1 + bit / 64] |= d << (bit % 64);
  }
  return r;
}

static inline rn_wide wide_rotl(rn_wide a, uint64_t k) {
  uint64_t w = WIDE_WIDTH(a);
  k %= w;
  rn_wide hi = wide_shl(a, k);
  rn_wide lo = wide_shr(a, w - k);
  int n = WIDE_NLIMBS(w);
  for (int i = 1; i <= n; i++) {
    hi[i] |= lo[i];
  }
  free(lo);
  wide_mask(hi);
  return hi;
}

static inline rn_wide wide_rotr(rn_wide a, uint64_t k) {
  uint64_t w = WIDE_WIDTH(a);
  k %= w;
  return wide_rotl(a, w - k);
}

static inline rn_wide wide_bitop(rn_wide a, rn_wide b, int op) {
  uint64_t w = WIDE_WIDTH(a);
  int n = WIDE_NLIMBS(w);
  rn_wide r = wide_alloc(w, WIDE_SIGNED(a));
  for (int i = 1; i <= n; i++) {
    r[i] = op == 0 ? (a[i] & b[i]) : op == 1 ? (a[i] | b[i]) : (a[i] ^ b[i]);
  }
  wide_mask(r);
  return r;
}

static inline rn_wide wide_not(rn_wide a) {
  uint64_t w = WIDE_WIDTH(a);
  int n = WIDE_NLIMBS(w);
  rn_wide r = wide_alloc(w, WIDE_SIGNED(a));
  for (int i = 1; i <= n; i++) {
    r[i] = ~a[i];
  }
  wide_mask(r);
  return r;
}

static inline rn_wide wide_exp(rn_wide base, uint64_t e) {
  rn_wide r = wide_from_u64(1, WIDE_WIDTH(base), WIDE_SIGNED(base));
  rn_wide b = base;
  while (e) {
    if (e & 1) {
      r = wide_mul(r, b, 0);
    }
    e >>= 1;
    if (e) {
      b = wide_mul(b, b, 1);
    }
  }
  return r;
}

// Decimal (or other base) rendering, without a width suffix.
static inline char *wide_tostring_base(rn_wide v, uint64_t base) {
  uint64_t w = WIDE_WIDTH(v);
  int n = WIDE_NLIMBS(w);
  char *buf = (char *)malloc((size_t)w + 4);
  int pos = 0;
  int neg = wide_is_neg(v);
  rn_wide m = neg ? wide_negtrunc(v) : v;
  uint64_t *cur = (uint64_t *)malloc((size_t)n * sizeof(uint64_t));
  for (int i = 0; i < n; i++) {
    cur[i] = m[1 + i];
  }
  int nonzero = 1;
  while (nonzero) {
    // cur /= base, collecting the remainder digit.
    unsigned __int128 rem = 0;
    nonzero = 0;
    for (int i = n - 1; i >= 0; i--) {
      unsigned __int128 acc = (rem << 64) | cur[i];
      cur[i] = (uint64_t)(acc / base);
      rem = acc % base;
      if (cur[i]) {
        nonzero = 1;
      }
    }
    int d = (int)rem;
    buf[pos++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
  }
  size_t outlen = (size_t)pos + (neg ? 1u : 0u);
  char *out = rn_stralloc(outlen);
  int o = 0;
  if (neg) {
    out[o++] = '-';
  }
  for (int i = pos - 1; i >= 0; i--) {
    out[o++] = buf[i];
  }
  assert((size_t)o == outlen);
  free(buf);
  free(cur);
  return out;
}

static inline char *wide_tostring(rn_wide v) {
  return wide_tostring_base(v, 10);
}

// Pseudo-random wide value (for randN expressions; quality does not matter,
// only that successive values differ).
static uint64_t rn_rand_state = 0x9e3779b97f4a7c15ull;

static inline uint64_t rn_rand_u64(uint64_t width) {
  rn_rand_state ^= rn_rand_state << 13;
  rn_rand_state ^= rn_rand_state >> 7;
  rn_rand_state ^= rn_rand_state << 17;
  uint64_t x = rn_rand_state * 0x2545f4914f6cdd1dull;
  if (width < 64) {
    x &= (((uint64_t)1 << width) - 1);
  }
  return x;
}

static inline rn_wide wide_rand(uint64_t width) {
  rn_wide v = wide_alloc(width, 0);
  for (int i = 1; i <= WIDE_NLIMBS(width); i++) {
    v[i] = rn_rand_u64(64);
  }
  wide_mask(v);
  return v;
}

// Modular arithmetic on wide values: operands reduce into [0, m).
static inline rn_wide wide_modreduce(rn_wide v, rn_wide m) {
  rn_wide r;
  if (wide_is_neg(v)) {
    rn_wide mag = wide_negtrunc(v);
    rn_wide mr = wide_rem(mag, m);
    r = wide_addsub(m, mr, 1, 1);
    // m - 0 == m must reduce to 0.
    rn_wide r2 = wide_rem(r, m);
    free(r);
    r = r2;
  } else {
    r = wide_rem(v, m);
  }
  r[0] = WIDE_WIDTH(m);  // Result adopts the modulus's unsigned type.
  return r;
}

static inline rn_wide wide_modadd(rn_wide a, rn_wide b, rn_wide m) {
  // Add in a representation one limb wider than the modulus so the carry out
  // of the top bit survives.  A full-width modulus (e.g. Curve25519's
  // 2**255 - 19) leaves no spare bit, so a + b can exceed 2**width and
  // truncating to the modulus width would silently drop 2**width, which is
  // congruent to a small nonzero residue mod m.
  uint64_t w = WIDE_WIDTH(m);
  uint64_t w2 = w + 64;
  rn_wide wa = wide_cast(a, w2, 0, 1);
  rn_wide wb = wide_cast(b, w2, 0, 1);
  rn_wide wm = wide_cast(m, w2, 0, 1);
  rn_wide s = wide_addsub(wa, wb, 0, 1);
  if (wide_cmp_limbs(s, wm) >= 0) {
    rn_wide t = wide_rem(s, wm);
    free(s);
    s = t;
  }
  rn_wide r = wide_cast(s, w, 0, 1);
  free(wa);
  free(wb);
  free(wm);
  free(s);
  return r;
}

static inline rn_wide wide_modsub(rn_wide a, rn_wide b, rn_wide m) {
  if (wide_cmp_limbs(a, b) >= 0) {
    return wide_addsub(a, b, 1, 1);
  }
  rn_wide t = wide_addsub(m, b, 1, 1);
  rn_wide r = wide_modadd(a, t, m);
  free(t);
  return r;
}

static inline rn_wide wide_modneg(rn_wide a, rn_wide m) {
  rn_wide zero = wide_alloc(WIDE_WIDTH(m), 0);
  rn_wide r = wide_modsub(zero, a, m);
  free(zero);
  return r;
}

// (a * b) % m via a double-width product.
static inline rn_wide wide_modmul(rn_wide a, rn_wide b, rn_wide m) {
  uint64_t w = WIDE_WIDTH(m);
  uint64_t w2 = 2 * w;
  rn_wide wa = wide_cast(a, w2, 0, 1);
  rn_wide wb = wide_cast(b, w2, 0, 1);
  rn_wide wm = wide_cast(m, w2, 0, 1);
  rn_wide prod = wide_mul(wa, wb, 1);
  rn_wide rem = wide_rem(prod, wm);
  rn_wide r = wide_cast(rem, w, 0, 1);
  free(wa);
  free(wb);
  free(wm);
  free(prod);
  free(rem);
  return r;
}

static inline rn_wide wide_modexp(rn_wide base, rn_wide e, rn_wide m) {
  rn_wide r = wide_from_u64(1, WIDE_WIDTH(m), 0);
  rn_wide b = wide_modreduce(base, m);
  uint64_t we = WIDE_WIDTH(e);
  for (int bit = 0; (uint64_t)bit < we; bit++) {
    if ((e[1 + bit / 64] >> (bit % 64)) & 1) {
      r = wide_modmul(r, b, m);
    }
    b = wide_modmul(b, b, m);
  }
  return r;
}

// Modular inverse by the extended Euclid algorithm on wide magnitudes.
static inline rn_wide wide_modinv(rn_wide a, rn_wide m) {
  uint64_t w = WIDE_WIDTH(m) + 1;
  rn_wide t = wide_alloc(w, 1);
  rn_wide newt = wide_from_u64(1, w, 1);
  rn_wide r = wide_cast(m, w, 1, 1);
  rn_wide newr = wide_cast(wide_modreduce(a, m), w, 1, 1);
  rn_wide zero = wide_alloc(w, 1);
  while (wide_cmp_limbs(newr, zero) != 0) {
    rn_wide q = wide_div(r, newr);
    rn_wide t2 = wide_addsub(t, wide_mul(q, newt, 1), 1, 1);
    t = newt;
    newt = t2;
    rn_wide r2 = wide_addsub(r, wide_mul(q, newr, 1), 1, 1);
    r = newr;
    newr = r2;
  }
  if (wide_is_neg(t)) {
    rn_wide t2 = wide_addsub(t, wide_cast(m, w, 1, 1), 0, 1);
    t = t2;
  }
  rn_wide out = wide_cast(t, WIDE_WIDTH(m), 0, 1);
  return out;
}

static inline rn_wide wide_moddiv(rn_wide a, rn_wide b, rn_wide m) {
  rn_wide inv = wide_modinv(b, m);
  rn_wide r = wide_modmul(a, inv, m);
  free(inv);
  return r;
}

// Equality helper for arrays of wide values.
static inline uint8_t isequal_rn_wide(rn_wide a, rn_wide b) {
  return wide_eq(a, b);
}

// Print helper used by generated array tostring code: the value followed by
// its Rune width suffix (e.g. 12345u65), like u32 array elements print.
static inline void tostring_rn_wide_t(rn_wide v) {
  // Array printing appends the u<width>/i<width> suffix from the element
  // type, so only the digits print here.
  char *s = wide_tostring(v);
  GlobalStringWriter_write("%s", s);
  rn_strfree(s);
}

static inline uint8_t isequal_rn_wide_t(rn_wide a, rn_wide b) {
  return wide_eq(a, b);
}
