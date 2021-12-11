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

In short, Rune aims to be the safest language available, while being similar to
Python, yet faster than C and C++ for most memory-intensive applications.

Consider the following example for treatment of secrets:

```
// Check the MAC (message authentication code) for a message.  A MAC is derived
// from a hash over the `macSecret` and message.  It ensures the message has not
// been modified, and was sent by someone who knows `macSecret`.
func checkMac(macSecret: secret(string), message: string, mac: string) -> bool {
    computedMac = computeMac(macSecret, message)
    return mac == computedMac
}

func computeMac(macSecret: string, message:string) -> string {
  // A popular MAC algorithm.
  return hmacSha256(macSecret, message)
}
```

Can you see the potential security flaw?  In most languages, an attacker with
accurate timing data can forge a MAC on a message of their choice, causing a
server to accept it as genuine.

Assume the attacker can tell how long it takes for `mac == computedMac` to run.
If the first byte of an attacker-chosen `mac` is wrong for the attacker-chosen
`message`, the loop terminates after just one comparison.  With 256 attempts,
the attacker can find the first byte of the expected MAC for the
attacker-controlled `message`.  Repeating this process, the attacker can forge
an entire MAC.

Users of Rune are protected, because the compiler sees that `macSecret` is
secret, and thus the result of `hmacSha256` is secret.  The string comparison
operator, when either operand is secret, will run in constant time, revealing no
timing information to the attacker.  Care must still be taken in Rune, but many
common mistakes like this are detected by the compiler, and either fixed or
flagged as an error.

As for the speed and safety of Rune's memory management, consider a simple
`Human` class.  This can be tricky to model in some languages, yet is trivial in
both SQL and Rune.

```
class Human(self, name: string, mother: Human = null, father: Human = null) {
  self.name = name
  if !isnull(mother) {
    mother.appendMotheredHuman(self)
  }
  if !isnull(father) {
    father.appendFatheredHuman(self)
  }

  func printFamilyTree(self, level: u32) {
    for i in range(level) {
      print "    "
    }
    println self.name
    for child in self.motheredHumans() {
      child.printFamilyTree(level + 1)
    }
    for child in self.fatheredHumans() {
      child.printFamilyTree(level + 1)
    }
  }
}

relation DoublyLinked Human:"Mother" Human:"Mothered" cascade
relation DoublyLinked Human:"Fater" Human:"Fathered" cascade

adam = Human("Adam")
eve = Human("Eve")
cain = Human("Cain", eve, adam)
abel = Human("Abel", eve, adam)
alice = Human("Alice", eve, adam)
bob = Human ("Bob", eve, adam)
malory = Human("Malory", alice, abel)
abel.destroy()
adam.printFamilyTree(0u32)
eve.printFamilyTree(0u32)
```

When run, this prints:

```
Adam
    Cain
    Alice
    Bob
Eve
    Cain
    Alice
    Bob
```

Note that Abel and Malory are not listed.  This is because we didn't just kill
Abel, we destroyed Abel, and this caused all of Abel's children to be
recursively destroyed.

Relation statements are similar to columns in SQL tables.  A table with a Mother
and Father column has two many-to-one relations in a database.

Relation statements give the Rune compiler critical hints for memory
optimization.  Objects which the compiler can prove are always in cascade-delete
relationships do not need to be reference counted.  The relation statements also
inform the compiler to update Node's destructor to recursively destroy children.
**Rune programmers never write destructors**, removing this footgun from the
language.

To understand why Rune's generated SoA code is so efficient, consider the arrays
of properties created for the Human example above:


```
  nextFree = [0u32]
  motherHuman = [null(human.Human(string, null, null))]
  prevHumanMotheredHuman = [null(human.Human(string, null, null))]
  nextHumanMotheredHuman = [null(human.Human(string, null, null))]
  firstMotheredHuman = [null(human.Human(string, null, null))]
  lastMotheredHuman = [null(human.Human(string, null, null))]
  faterHuman = [null(human.Human(string, null, null))]
  prevHumanFatheredHuman = [null(human.Human(string, null, null))]
  nextHumanFatheredHuman = [null(human.Human(string, null, null))]
  firstFatheredHuman = [null(human.Human(string, null, null))]
  lastFatheredHuman = [null(human.Human(string, null, null))]
  name = [""]
```

A total of 12 arrays are allocated for the Human class in SoA memory layout.  In
`printFamilyTree`, we only access 5 of them.  In AoS memory layout, all 12
fields would be loaded into cache during the tree traversal, and all fields
would be 64 bits on a 64-bit machine.  In Rune, only the string references are
64-bits by default.  As a result, **Rune loads only 25% as much data into
cache** during the traversal, improving memory load times, while simultaneously
improving cache hit rates.

This is why Rune's binarytree.rn code already runs faster than any other
single-threaded result in the [Benchmark
Games](https://benchmarksgame-team.pages.debian.net/benchmarksgame/index.html).
(Rune is not yet multi-threaded).  The only close competitor is C++, where the
author uses the little-known `MemoryPool` class from the `<memory>` library.
Not only is Rune's SoA memory layout faster, but its solution is more generic:
we can create/destroy Node objects arbitrarily, unlike the C++ benchmark based
on `MemoryPool`.  When completed, we expect Rune to win most memory-intensive
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

