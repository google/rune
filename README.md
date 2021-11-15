# ᚣ The Rune Programming Language
_Safer code for secure enclaves_

This is not an officially supported Google product.

Rune is a systems programming language designed for security-sensitive
applications that are prone to common security flaws when implemented in
traditional systems languages such as C and C++.  Its primary goal is providing
safety features for hardware-enforced private computation such as [sealed
compution](https://arxiv.org/abs/1906.07841) or [secure
enclaves](https://www.infosecurity-magazine.com/opinions/enclaves-security-world/).
Rune's most notable security feature is constant-time processing of secrets.
Rune also aims to be faster than C++ for most memory-intensive applications, due
to its Structure-of-Array
\([SoA](https://en.wikipedia.org/wiki/AoS_and_SoA#:~:text=AoS%20vs.,AoS%20case%20easier%20to%20handle.)\)
memory management.

See documentation in [g3doc](g3doc/index.md) for more information about Rune.

## Compiling the Rune compiler:

You'll need 4 dependencies installed to compile Rune:

  - Bison (parser generator
  - Flex (lexer generator)
  - GNU multi-precision package gmp
  - Datadraw, an SoA data-structure generator for C
  - Clang version 10
The first four can be installed with one command:


    $ sudo apt-get install bison flex libgmp-dev clang-10

Installing Datadraw requires cloning the source from github, or getting it from
//third\_party/datadraw.

    $ git clone https://github.com/waywardgeek/datadraw.git
    $ sudo apt-get install build-essential
    $ export PATH=/usr/buildtools/buildhelpers/v4/bin:$PATH
    $ cd datadraw
    $ ./autogen.sh
    $ ./configure
    $ make
    $ sudo make install

Hopefully that all goes well...  After dependencies are installed, to build
rune:

TODO: add cloning of repo, then cd into that dir.

    $ make

You need:

* CTTK: A constant-time bignumber toolkit for safe crypto

Which was written by Thomas Pornin.  It lives in

* //third_party/CTTK

If `make` succeeds, test the Rune compiler in the rune directory with:

    $ ./runtests

To install rune under /usr/local/rune:

    $ sudo make install

Test your installation:

    $ echo 'println "Hello, World!"' > hello.rn
    $ rune -g hello.rn
    $ ./hello

You can debug your binary executable with gdb:

    $ gdb ./hello

TODO: add instructions on how to debug compiler itself, especially the datadraw debug functionality.

