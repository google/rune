# ᚣ The Rune Programming Language
_Safer code for secure enclaves_

This is not an officially supported Google product.

**NOTE: Rune is an unfinished language. Feel free to kick tires and evaluate the
cool new security and efficiency features of Rune, but, for now, it is not
recommended for any production use case.**

Rune is a systems programming language designed for security-sensitive
applications. Rune can help you avoid common security flaws that often arise
when using traditional systems languages such as C and C++. Its primary goal is
providing safety features for hardware-enforced private computation such as
[sealed computation](https://arxiv.org/abs/1906.07841) or [secure
enclaves](https://www.infosecurity-magazine.com/opinions/enclaves-security-world/).
Rune's most notable security feature is constant-time processing of secrets.
Rune also aims to be faster than C++ for most memory-intensive applications,
due to its Structure-of-Array
\([SoA](https://en.wikipedia.org/wiki/AoS_and_SoA#:~:text=AoS%20vs.,AoS%20case%20easier%20to%20handle.)\)
memory management.

For more information about Rune, see additional documentation in
[g3doc](g3doc/index.md).

## Compiling the Rune compiler:

You'll need 6 dependencies installed to compile Rune:

  - Bison (parser generator)
  - Flex (lexer generator)
  - GNU multi-precision package gmp
  - Clang version 10
  - Datadraw, an SoA data-structure generator for C
  - CTTK, a constant-time big integer arithmetic library
The first four can be installed with one command:

```sh
$ sudo apt-get install bison flex libgmp-dev clang-10
```

Installing Datadraw requires cloning [the source from
github](https://github.com/waywardgeek/datadraw), or getting it from
//third\_party/datadraw.

```sh
$ git clone https://github.com/waywardgeek/datadraw.git
$ sudo apt-get install build-essential
$ cd datadraw
$ ./autogen.sh
$ ./configure
$ make
$ sudo make install
```

Hopefully that all goes well...  After dependencies are installed, to build
rune:

```sh
$ git clone https://github.com/google/rune.git
$ git clone https://github.com/pornin/CTTK.git
$ cp CTTK/inc/cttk.h CTTK
$ cd rune
$ make
```

CTTK was written by Thomas Pornin.  It provides constant-time big-integer
arithmetic.

If `make` succeeds, test the Rune compiler in the rune directory with:

```sh
$ ./runtests.sh
```

Some tests are currently expected to fail, but most should pass. To install
rune under /usr/local/rune:

```sh
$ sudo make install
```

Test your installation:

```sh
$ echo 'println "Hello, World!"' > hello.rn
$ rune -g hello.rn
$ ./hello
```

You can debug your binary executable with gdb:

```sh
$ gdb ./hello
```

TODO: add instructions on how to debug compiler itself, especially the datadraw debug functionality.

