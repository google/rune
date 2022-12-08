# ᚣ The Rune Programming Language

_A faster, safer, and more productive systems programming language_

-   [See the language reference here.](rune_reference.md)
-   [Credits](credits.md)

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

## Rune for Python Programmers

If you love Python and wish you could be as productive in a systems programming
language, checkout the [Rune for Python Programmers](rune4python.md) guide. Just
want to see some code? Checkout the Rune solutions in the crypto\_class directory
for the "Write your own crypto, but never use it!" workshop.

## Security first

In the age of pervasive computing, privacy and security have become critical.
Rune is designed to protect privacy of critical secrets, such as encryption
keys. Rune has several properties to help protect those secrets:

-   All operations on secrets occur in constant time, minimizing timing
    side-channel leakage.
-   Secrets cannot be used in conditional branches or memory addressing.
-   Even speculative branching and indexing on secrets are caught at
    compile-time to avoid Spectre/Meltdown.
-   Secrecy is sticky: any value in part derived from a secret is considered
    secret until "revealed".
-   Integer overflow is detected, except in unsafe mode.
-   Index out of bounds is detected, except in unsafe mode.
-   Rune supports null safety.  Null is either not possible, or checked at
    runtime.
-   Uninitialized memory access is impossible.
-   Secrets are automatically zeroed when no longer used
-   All behavior is fully defined.
-   Memory leaks are reduced through auto-generated destructors.

### Rune's solution to dangling pointers

Instead of "containers" and "collections", Rune provides "relationships".
Relationships are 2-way. Both parents and children contribute to maintaining the
relationships, and children can haven multiple parents through multiple
relationships.

With collections, a child object doesn't know it is in a relationships with
various parents, and it does not notify them automatically when destroyed. This
results is dangling pointers and memory leaks.

When an object is destroyed in Rune, it automatically removes itself from all
relationships. Child relationships which are marked cascade-delete cause an
object to destroy it's children when it is destroyed. Otherwise, it removes all
children from the relationship.

Along with some reference-counting of objects, these rules provide full memory
safety.  With this paradigm, Rune offers much of the simplicity of garbage
collection, without the speed or memory overhead.

## Memory Management

Classes in Rune are either reference-counted or cascade-deleted. If a class is
not a child of any cascade-delete relationship, then it is reference counted.
The compiler detects potential for reference-loops, and flags them as errors.
Cascade-deleted objects must always have at least one cascade-delete parent,
which is checked at runtime.

## Auto-generated destructors

Rune programmers never write destructors.  This reduces memory leaks, and
improves productivity.

A "final" function can be used to do accounting tasks when an object is
destroyed. Other than that, Rune programmers do not need to worry about the
mechanics of recursive object destruction.

## Rune is sometimes faster than C

Instead of array-of-structure (AoS) based programming, Rune uses
structure-of-array \([SoA](https://en.wikipedia.org/wiki/AoS_and_SoA)\). This
improves cache performance and reduces memory usage, in part because most object
references can be 32-bit rather than 64-bit. Benchmarks to date show memory
intensive applications, such as electronic design automation, speeding up by
about 40% while reducing memory by 20%.

## Can you see the security flaw with this code?

```
func bignumModularMul(a, b, modulus, isSecret) {
    if isSecret {
        return fastModularMul(a, b, modulus)
    }
    return constTimeModularMul(a, b, modulus)
}
```

The rules for secure coding keep changing! As of 2018: Never even
_speculatively_ branch or index on a secret! Rune catches this at compile time.
