# Patches for third-party dependencies

## cttk-gendiv-stack2-buffer.patch

Fixes a stack-buffer overflow in **CTTK** (`../CTTK`, Thomas Pornin's
constant-time toolkit, linked as `lib/libcttk.a`) that crashes the Rune
bootstrap compiler when it renders a wide-integer *literal* wider than ~10571
bits as hex — `bootstrap/rune` aborts with `*** stack smashing detected ***`
or `free(): invalid size`. This caps the `pidigits` benchmark (which uses
fixed-width wide ints for the spigot state) at ~265 digits.

Root cause: in `src/int31.c`, `gendiv_stack2()` sizes its two scratch buffers
`t1`/`t2` at `CTTK_MAX_INT_BUF / (3 * sizeof(uint32_t))` (341 words), but its
caller `gendiv()` routes to it whenever `wlen <= CTTK_MAX_INT_BUF /
(2 * sizeof(uint32_t))` (512 words). For `wlen` in [342, 512] the guard admits
values the buffers can't hold. `gendiv_stack2` uses only two temporaries, so
the buffers should be sized `/2` to match the guard (the sibling
`gendiv_stack3`, with three temporaries and a matching `/3` guard, is correct
and untouched).

Apply against a CTTK checkout and rebuild:

```sh
cd ../CTTK
patch -p1 < ../rune/patches/cttk-gendiv-stack2-buffer.patch
make
cp build/libcttk.a ../rune/lib/libcttk.a
# then rebuild the bootstrap compiler so it links the fixed lib:
cd ../rune/bootstrap && touch rune.rn && make rune
```

With the patch, wide-int literals compile correctly up to ~i15500, letting
`pidigits.rn` use a wider state type (e.g. `i15000`) and produce ~400 correct
digits instead of 265. The `pidigits.rn` in this repo stays at `i8192` so it
builds against an unpatched CTTK; widen the type only if the patch is applied.

A residual `free(): invalid size` above ~i16000 comes from a separate
malloc-path sizing issue in the runtime bigint layer (`runtime/bigint.c`) and
is not addressed here.
