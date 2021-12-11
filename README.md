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

Consider the following example for treatment of secrets:

```
// Check the MAC (message authentication code) for a message.  A MAC is derived
// from a hash over the `macSecret` and message.  It ensures the message has not
// been modified, and was sent by someone who knows `macSecret`.
func checkMac(macSecret: secret(string), message: string, mac: string) -> bool {
    computedMac = computeMac(macSecret, message)
    return mac == computedMac
}

func computeMac(macSecret: secret(string), message:string) -> string {
  // A popular MAC algorithm.
  return hmacSha256(macSecret, message)
}
```

Can you see the potential security flaw?  Suppose the attacker can tell how long
it takes for the comparison `mac == computedMac` to run.  If the first byte of
an attacker-chosen `mac` is wrong for the attacker-chosen `message`, the
loop terminates after just one comparison.  With 256 attempts, the attacker can
find the first byte of the expected MAC for the attacker-controlled `message`.
Repeating this process, the attacker can forge the entire MAC.

Rune is not affected, because it sees that `macSecret` is secret, and thus the
result of `hmacSha256` is secret.  The string comparison operator when either
operand is secret will be executed in constant time, revealing no timing
information to the attacker.  Care must still be taken in Rune, but many common
mistakes like this are detected by the compiler.

As for the speed of Rune's memory management, the `binarytree.rn` benchmark begins
to show what is possible.  In simplified form:

```
class Node(self) {
}

relation OneToOne Node:"ParentLeft" Node:"Left" cascade
relation OneToOne Node:"ParentRight" Node:"Right" cascade

func makeTree(depth: Uint) -> Node {
  node = Node()
  if depth != 0 {
    left = makeTree(depth - 1)
    right = makeTree(depth - 1)
    node.insertLeftNode(left)
    node.insertRightNode(right)
  }
  return node
}
```

The `relation` statements give the Rune compiler critical hints for memory
optmization.  It figures out not to reference count objects already in a
cascade-delete relationship (all Nodes but the root).  It also auto-generates a
safe destructor: Rune programmers never write destructors, removing this footgun
from the language.  Consider what happens in C++ if we call delete on a child
node, without manually maintaining up back-pointers to parent nodes?

This code already runs faster than any other single-threaded result in the
[Benchmark
Games](https://benchmarksgame-team.pages.debian.net/benchmarksgame/index.html).
(Rune is not yet multi-threaded).  The only close competitor is C++, where the
author uses the little-known `MemoryPool` class from the `<memory>` library.
Not only is Rune's SoA memory layout faster, due to improved cache performance,
its solution is generic: we can create/destroy Node objects arbitrarily, unlike
the C++ benchmark.  When completed, we expect Rune to win most memory-intensive
benchmarks.

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

