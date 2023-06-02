# Rune Language Reference

**Authors**: waywardgeek@, aidenhall@, ethangertler@, arwilson@

[TOC]

## Memory safety

Rune achieves memory safety through a combination of techniques that make it
possible to feel like using Python without the overhead of garbage collection.
Several design choices were made to enable this hat trick.

The first design choice, a core principle in Rune, is that the programmer cannot
determine how we implement their data structures under the hood in the compiler.
Implementation of data structures in Rune is abstract, allowing the compiler
great freedom in how that data is laid out in memory.

Another design choice is that all class instances are reference counted, unless
the compiler can prove this it is not needed. To aid in this, Rune compiles an
entire program or shared library at once, rather than one file at a time. If
certain conventions are followed, moste objects will not even have a reference
counter allocated.

Other safety choices include throwing exceptions on integer overflow/underflow,
and bounds checking of arrays.  Since Rune is also a systems preogramming
language, all of these safety features can be disabled if required for speed,
but the default is full safety.

### Compilation units

Unlike C, Rune cannot be compiled one file at a time. The Python-like level of
Rune polymorphism requires compiling an entire program or stand-alone shared
library. This is similar to most Python code, where imports are at the top of
each file. Rune compiles the top level Rune file, along with all of its
transitive imported dependencies, at once.

The C linker is involved only when linking a binary or creating a shared
library, where all functions must be compatible with C or C++ calling
conventions, and are no longer polymorphic.

### Relationships between classes

In Rune, relationships between classes are explicitly declared with relation
statements, which can be cascade-delete, meaning that the child objects are
destroyed when the parent is destroyed. This is the single most important tool
the compiler has for efficiently managing object lifetimes. For most memory
intensive applications, most objects should be in cascade-delete relationships.

### Rules for null safety

Types returned by constructors are not null, adding some null safety without
any type constraints.  Nullable types are created with null(\<_Class name_>).  All
nullable types are checked before dereferencing, which in Rune happens
naturally as in index-out-of-bounds checks.  While this is a compiler-specific
non-user-visible detail, the current Rune compiler uses -1 as null, rather than
0, and null dereferencing is caught through array bounds checking.

For improved safety and efficiency, nullable types should be converted to
non-nullable with `<`_expression_`>!`, which checks for non-null at runtime, and
returns a non-nullable type.

Exactly how transformers for relations should handle null safety is a work in
progress.

### Rules for memory safety

Memory corruption is impossible in Rune, assuming relationship transformers are
correct. All object references are either valid or null at all times. Index
bounds are checked, and dereferencing null throws an error.

Rune destroys objects when no longer visible in almost all cases, using the
following rules:

*   Objects created by calling class constructors are reference counted, but the
    compiler only updates reference counters when there is a chance that an
    object may be destroyed while the reference is in use.
*   The compiler throws an error when at the end of a destructor, an object's
    reference counter is non-zero.
*   Relationship loops containing any class that is not a child in a
    cascade-delete relationship generates a compiler error.

With this scheme, the only case where non-visible objects are not destroyed,
causing a memory leak, is when we have a cycle of objects, all of which are in
cascade-delete relationships but are not in any relationship path to a top level
object (described below). For example:

```
class Tree(self) {
}
relation LinkedList Tree Tree cascade
tree = Tree()
tree.insertTree(tree)
```

Since `tree` has a self-reference, it is always owned by a cascade-delete
relationship, and is never destroyed, even if it is no longer visible.

### Conventions for efficiency

A "top-level" class is one that is not in any cascade-delete relationships as
the child. If the compiler sees that objects for a class always have a
cascade-delete path of parents to a top-level class, it can avoid reference
counting for that class, unless class objects are assigned directly to globals.

Programs intended to run fast should follow the following conventions to aid the
compiler's ability to avoid reference counting. If following these conventions,
most classes will not even allocate a reference counter:

*   Define "top-level" classes which are not owned by any cascade-delete
    relationship. These will be reference counted.
*   For other classes, constructors should add self to at least one
    cascade-delete relationship transitively owned by a top-level class.
*   When removing an object from a cascade-delete relationship add it to another
    before the reference goes out of scope, for at least a subset of
    cascade-delete relationships owning the class.
*   Use "safe" iterators (where "safe" is in the name) to access objects that
    may be destroyed in a loop.
*   Pass objects that may be destroyed in a function as `var` parameters, so the
    destructor can set your reference to null.
*   Avoid assigning cascade-delete objects to global variables. Instead, add
    them to relationships, even if the relationship is from a global object.

If these conventions are followed for a class, the compiler can avoid all
reference counting for that class, and should not allocate the reference counter.

The compiler should check that any function calling a destructor has been called
via functions that pass the object reference as a var parameter. An error will
be generated otherwise at runtime, as this leaves the reference counter at the
end of the destructor non-zero.

### Abstractly passing parameters by value or reference

Unlike Python, Rune abstracts away whether data is passed by reference, or
value. Consider the following Python:

```python
def foo(n):
    n = n + 1
    return n

n = 1 << 4092
m = foo(n)
print("n = %x" % n)
print("m = %x" % m)
```

When run, this prints:

n = 10000000000000000000000000000000000000000000000000000000000000000 m =
10000000000000000000000000000000000000000000000000000000000000001

Python always passes big integers by value! This is clearly not acceptable for a
systems programming language like Rune. In contrast, it is not possible in
Python to determine if tuples are passed by value or reference, because they are
immutable. This insight gave rise to parameter passing in Rune.

In Rune, all parameters passed to functions are immutable by default, unless
they are declared as `var` meaning mutable, in which case they are passed by
reference. It is not possible to determine in Rune if by default we pass arrays
by value or reference, though for efficiency, we currently pass arrays, tuples,
and structs by reference, small integers by value, and big integers by
reference. This is simply a compiler optimization detail which does not impact
behavior of code, only its speed.

### Assignment implies copy for builtin classes

Since we do not know if a value is passed by reference or value, what happens
here:

```rune
func doubleArray(a: Array) {
    b = a
    b.concat(a)
    return b
}
```

The `a` parameter is immutable, but `b` can be modified. This forces the
compiler to make a copy of `a`, unless `b` is never modified, in which case why
did the programmer bother assigning `b` to `a`? Rune makes copies of builtin
data types on assignment, which means that within a single scope, you cannot
create new references to a value, only copies. The compiler can be smart enough
to move the value rather than copy it, if the right-hand-side is no longer used
elsewhere.

There is one case we know of (are there any others?) where having multiple
references within a single scope to an object is useful: swapping. Consider
this Python code:

```python
# Assign a and b to large values we don't want to copy.
a = [1, 2, 3, 4]
b = ("this", ["is", "a"], "test")
# Swap a and b.
temp = a
a = b
b = temp
```

This is potentially inefficient in Rune. A more efficient Python solution is:

```python
(a, b) = (b, a)
```

This is the preferred method of swapping variables in Rune, and should not cause
any copy operations.

(TODO: implement tuple assignment in Rune!)

### Creating new classes with copy-on-assign semantics

Simply overload the `=` operator. If you overload a binary operator, but not its
corresponding assignment operator (e.g. overload `=`, `+`, but not `+=`), then Rune
will automatically use your two defined operators to implement the combined one.

TODO: implement this.

## Importing code

Rune has 2 concepts for using foreign code: Packages and Modules:

*   A Module is a file in the same directory.
*   A Package is a directory containing multiple modules.

**Modules** are imported with the `use` keyword. `use foo` imports all
identifiers, both exported and private, from the file foo.rn in the same
directory; using modules in other directories is not supported. The variables
from foo.rn are imported directly into the local file’s scope, without creating
a `foo` variable. The thinking here is that since modules are by definition in
the same package, they share all variables. A collection of modules is similar
to a package in golang, except an explicit statement of dependency on any other
local file is required, as opposed to only declaring them in the same package.

**Packages** are collections of modules defined by residence in a single
directory. Packages are imported using the `import` keyword. `import bar`
creates a single identifier called `bar` that exposes every exported identifier
in every module inside the `bar` directory, but unlike modules no identifiers
other than `bar` itself are imported into the local scope, and must instead
always be referenced as attributes of `bar`. For example, if the `bar` package
exports the `baz` function, `bar.baz()` would be the correct calling syntax.
Every package has a special `package.rn` module which contains initialization
code and imports other modules in the package. The identifiers exported by a
package are exactly those identifiers exported directly from `package.rn` or
exported by the modules it imports.

### Object lifetimes

## Modules, packages, and libraries

In short:

*   A _module_ is an individual Rune .rn file.
*   A _package_ is a directory of Rune .rn files, with a “package.rn” file that
    imports the rest of the files in the package.
*   A _library_ is a compiled set of Rune files, generating a shared .so file,
    that is compatible with C/C++.

We call a compiled unit a “library”, which typically compiles to a shared .so
file. You declare callable functions and methods of a library with the `exportlib`
keyword:

```rune
exportlib func sha256(s:string): string {

    ...
}
```

Calling a function in a shared library is less flexible in that function/method
type signatures must be fully specified with unqualified types. You can also
call object methods declared with `exportlib`, if in a previous call, you obtained
an object reference as a result. Before calling an `exportlib` function or method,
you must include the library with `importlib`:

```rune
importlib crypto

...

digest = crypto.sha256(data)
```

To expose class methods, use `exportlib` on each method. If you want the
constructor to be exported, declare it on the class. For example:

```rune
exportlib class Hasher(self) {
    self.state = 0u256
    exportlib func update(self, data: string) {
        ...
    }
    exportlib func final(self) {
        return self.state
    }
}
```

In another library:

```rune
importlib crypto

func hashWithInfo(info: string, data: string): string {
    hasher = crypto.Hasher()
    hasher.update(info)
    hasher.update(data)
    return hasher.final()
}
```

Rune libraries are initialized when loaded, by executing the global statements
in the top-level file, similar to Python. To be useful as Web Assembly modules,
libraries must declare either `exportlib` APIs, or `exportrpc` APIs.

## Functions

Rune provides the several variations on the syntax to declare functions. Rune's
functions do not specify return types.

```rune
// No types
func Add(a, b) {
  return a + b
}

// With explicit types
func Add(a: u8, b: u8) {
  return a + b
}
```

Rune functions can also require more advanced types, such as arrays:

```rune
// With an array type
func Sum(a: [i32]) {
  s = 0
  for i = 0, i < a.length(), i += 1 {
    s += a[i]
  }
  return s
}
```

TODO add an example with classes, including a class with one or more template
types.

## Comments

Rune uses C-like comments:

```rune
// This is a single-line comment.
/*
  This is a block comment.
  Note that /* embedded comments */ do not end the comment block.
*/
```

## Literals

The following subsections detail the syntax of lexical tokens corresponding
to elements of the given type.

### Identifiers

An _identifier_ is either an alphanumeric, consisting of a single alphanumeric token consisting of UTF8 characters, letters, digits, underscores and `$` symbols,
not beginning with a digit.

 <em>c</em> ::= [_a-zA-Z$] | UTF8_CHAR

 <em>ID</em> ::= c(c | [0-9])*

### Strings

A string is any sequence of ASCII or UTF8 characters between two `"` characters:

STRING ::= \\\"([^"] | \\.)*\\\"

### Integers

INTEGER ::= "'"[ -~]"'"
    | '\\a'" | "'\\b'" | '\\e'" | "'\\f'" | "'\\n'" | "'\\r'" | "'\\t'" | "'\\v'"
    | [0-9]+(("u"|"i")[0-9]+)?
    | "0x"[0-9a-fA-F]+(("u"|"i")[0-9]+)?


INTTYPE ::= "i"[0-9]+

UINTTYPE ::= "u"[0-9]+

### Floating point

FLOAT ::= [0-9]+"e"("-")?[0-9]+"f32"
    | [0-9]+"."("e"("-")?[0-9]+)?"f32"
    | [0-9]*"."[0-9]+("e"("-")?[0-9]+)?"f32"
    | [0-9]+"e"("-")?[0-9]+("f64")?
    | [0-9]+"."("e"("-")?[0-9]+)?("f64")?
    | [0-9]*"."[0-9]+("e"("-")?[0-9]+)?("f64")?

### Random values

RANDUINT ::= "rand"[0-9]+

## Keywords

```
appendcode arrayof   as        assert    bool        cascade
case       class     debug     default   do          else
enum       export    exportlib extern    f32         f64
final      for       func      transform transformer if
import     importlib importrpc in        isnull      iterator
message    mod       null      operator  prependcode print'
println    ref       relation  return    reveal      rpc
secret     signed    string    struct    switch      throw
typeof     unittest  unref     unsigned  use         var
while      widthof   yield
```

## Datatypes

### Bool

You get the usual `true`, `false`, and `bool`:

```rune
done = false
while !done {
    ...
    if condition tests true {
       done = true
    }
}
```

### Integers

Integers are fixed-width, and either signed or unsigned. Binary operations
between integers require that the width and sign of both operands match.

```rune
1u21 + 2i21  // Illegal because of sign mismatch
123u255 \* 456u256  // Illegal because of width mismatch
// Legal if the value of ‘a’ does not change when cast to offset’s width and sign
<offset>a + offset
```

Operations on integers are not allowed to overflow/underflow without throwing an
error, unless operators starting with `!` are used:

```rune
255u8 !+ 1u8  // Legal, and equal to 0u8
<u8>256u32  // Throws error at compile or runtime
!<u8>256u32  // Legal, and equal to 0
```

### Default integers

Integers without a specified width and sign are assumed to be `u64`, but are only
weakly attached to that type (as described below). NOTE: The default is
_unsigned_! This makes it simpler, for example, to shift/rotate by a default
integer, which must be unsigned.

*   widthof(1) // returns 64
*   println -1 // Illegal, because negation cannot be applied to `u64`.

### Default integer constant auto-casting

Default integer constants will automatically cast to the integer type of the
other operand. For example, the following is legal:

```rune
func add1(a) {
    return a + 1  // Type of 1 converts automatically to type of a.
}

println add1(1u32)
println add1(-1u1024)
```

### Floating point

Rune currently supports `f32` (C float), and `f64` (C double) types:

```rune
x = 0.1; y = 0.8
dist = sqrt(x**2 + y**2)
Println “distance = %f” % dis
```

### String

Strings are encoded in UTF-8, and are internally represented as dynamic arrays
of `u8`.

List string methods here...

### Dynamic arrays

### Tuples

### Function pointers

## Post-compilation polymorphism

C++ supports two kinds of polymorphism:

*   Post-compilation polymorphism: class inheritance
*   Pre-compilation polymorphism: templates

The style of efficiently supported post-compilation polymorphism that can be
offered by a language depends on its memory layout. With C++ AoS layout:

1.  Class inheritance is efficient.
2.  Dynamic extensions to objects (like we do in Python) is inefficient.
3.  C++ is forced to continue using AoS, making it slower.

With Rune’s SoA memory layout:

1.  Dynamic extensions to objects is efficient.
2.  Class inheritance is inefficient.
3.  Rune is forced to continue using AoS, making it faster.

Emulation of class inheritance will be added to Rune, but it will do so through
composition under the hood.

TODO: Add class inheritance to Rune.

Within a compilation unit, which is generally a Rune shared library, functions,
classes, and methods are as polymorphic as you like, similar to Python, with
polymorphism decreasing as you add type constraints. This is similar to C++
templates, just with a lot less syntax, making it easy like Python to get your
desired level of polymorphism.

### Dynamic class extensions

TODO: Implement these features.

C++ inheritance does not enable the coder to dynamically add members and methods
to existing objects, like we can do in Python. Rune chooses to emulate class
inheritance through composition, while having native support for
post-compilation dynamic class extensions.

Consider the following base class in a compiler optimizer such as an LLVM IR
pass, in an “llvm” library:

```rune
class Instruction(self, type: InstructionType, resultType: Datatype) {
    self.type = type
    self.result = resultType
    ...
}
```

An optimization pass may need to annotate an instruction with various
information, such as the lifespan of the result value. In C++, we are basically
forced to add these fields to the base-class, or to use a manual extension class
with cross-pointers. In Rune, in a different shared library for a specific
optimization pass, we can declare:

```rune
class Instruction(self): llvm.Instruction){
    self.lifespan = null(Lifespan)
    self.visited = false
}
```

When the shared library for the optimization pass is loaded, it allocates
additional arrays for the new properties that each Instruction object needs
automatically. When the optimization pass completes, the extension memory is
automatically freed.

Under the hood, all that happens is no global arrays are allocated for the new
data members. The same SoA efficiency we get accessing the original class’ data
appliances when accessing extensions.

## Statements

```
appendcode  debug    function    prependcode  return    while
assert      extern   transform   print        switch    yield
assignment  final    transformer println      throw
call        for      if          ref          unittest
class       foreach  import      relation     unref
```

## Expressions

### Operators

```
!     !*    !*=   !+    !+=   !-    !-=   !<
!=    %     %=    &     &&    &&=   &=    *
*=    +     +=    -     -=    .     ...   /
/=    <     <<    <<<   <<<=  <<=   <=    =
==    >     >=    >>    >>=   >>>   >>>=  ?
?:    ^     ^=    ^^    ^^=   []    **    **=
|     |=    ||    ||=   ~     in    mod
```
The following subsections describe the built-in operators. Note that these can be overridden for classes.

#### Logical

Logical Not (prefix `!`)
: `!e` evaluates to `true` if and only if `e` evaluates to `false` and vice-versa.
: `e` must be a boolean-typed expression.

Logical And (`&&`)
: `e1 && e2` evaluates to `true` if both `e1` and `e2` evaluate to true. If either argument is secret, then both are evaluated to avoid giving away timing information. Otherwise, if `e1` is `false`, then `e2` is not evaluated.
: Both `e1` and `e2` must be boolean-valued expressions.

Logical Exclusive Or (`^^`)
: `e1 ^^ e2` evaluates to `true` if only one of `e1` and `e2` evaluates to `true`. Note that both expressions must always be evaluated.

Logical Or (`||`)
: `e1 || e2` evaluates to `true` if either `e1` and `e2` evaluate to `true`. If `e1` is `true`, then `e2` is not evaluated.

#### Bitwise

Bitwise Not (One's complement) (`~`)
: `~e` evaluates to the integer where every bit is the opposite of the corresponding bit in `e`.
: This is only valid for integral types (signed or unsigned) of any valid bit width.

Bitwise And (`&`)
: `e1 & e2` evaluates to a string of bits where each bit is 1 if and only if the bit in the same position is 1 in both `e1` and `e2`.
: This is only valid for integral types of any valid bit width.
: `e1` and `e2` must have exactly the same type.

Bitwise Or (`|`)
: `e1 | e2` evaluates to the integer where each bit is `1` if the corresponding bit in either `e1` or `e2` is `1`.
: This is only valid for integral types (signed or unsigned) of any valid bit width.
: `e1` and `e2` must have exactly the same type.

Bitwise Exclusive Or (XOR) (`^`)
: `e1 ^ e2` evaluates to a string of bits where each bit is 1 if and only if the bit in the same position is 1 in either `e1` or `e2`, but noth both.
: This is only valid for integral types (signed or unsigned) of any valid bit width.
: `e1` and `e2` must have exactly the same type.

Shift Left (`<<`)
: `e1 << e2` evaluates to the result of shifting `e1` by `e2` bits to the left.
: This is only valid when `e1` is a signed or unsigned integer, and `e2` is unsigned.  If `e2` is greater than or equal to the bit width of `e1`, the result is `0`.
: `e1` and `e2` may have different integral types, but `e2` must be unsigned.

Rotate Left (`<<<`)
: `e1 <<< e2` evaluates to the result of rotating the bits of `e1` to the left `e2` places.  High-order bits that would overflow and be lost when shifting left are copied instead to the low-order bits that have been vacated.
: This is only valid when `e1` is a signed or unsigned int and  `e2` is an unsigned int.
: `e1` and `e2` may have different integral types, but `e2` must be unsigned.

Shift Right (`>>`)
: `e1 >> e2` evaluates to the result of shifting `e1` by `e2` bits to the right.
: This is only valid when `e1` is a signed or unsigned integer, and `e2` is unsigned.  If `e2` is greater than or equal to the bit width of `e1`, the result is `0`.
: For signed integers, the top bit, or _sign bit_ of `e1` becomes the top `e2` bits of the result. This preserves the sign of the result, sometimes known as _Arithmetic Shift Right_.
: For unsigned integers, the top `e2` bits of the result are always `0`, sometimes known as _Logical Shift Right_.
: In both cases, this enables `>>` to represent division by a power of 2.
: `e1` and `e2` may have different integral types, but `e2` must be unsigned.

Rotate Right (`>>>`)
: `e1 >>> e2` evaluates to the result of rotating the bits of `e1` to the right `e2` places.  High-order bits that would overflow and be lost when shifting right are copied instead to the high-order bits that have been vacated.
: This is only valid when `e1` is a signed or unsigned int and  `e2` is an unsigned int.
: `e1` and `e2` may have different integral types, but `e2` must be unsigned.

#### Arithmetic

Multiply (`*`)
: `e1 * e2` evaluates to the product of the values of `e1` and `e2`. This will raise an exception upon overflow.
: `e1` and `e2` must be any numeric type, integral or floating point.
: `e1` and `e2` must have exactly the same type, so for example `32u8 * 32u16` and `32i8 * 32u8` are badly-formed expressions.

Add (`+`)
: `e1 + e2` evaluates to the sum of the values of `e1` and `e2`. This will raise an exception upon overflow.
: `e1` and `e2` must be any numeric type, integral or floating point.
: `e1` and `e2` must have exactly the same type, so for example `32u8 + 32u16` and `32i8 + 32u8` are badly-formed expressions.

Subtract (`-`)
: `e1 - e2` evaluates to the result of subtracting the value of `e2` from the value of `e1`. This will raise an exception upon overflow.
: `e1` and `e2` must be any numeric type, integral or floating point.
: `e1` and `e2` must have exactly the same type, so for example `32u8 - 32u16` and `32i8 - 32u8` are badly-formed expressions.

Divide (`/`)
: `e1 / e2` evaluates to the result of dividing the value of `e1` by `e2`.  If `e1` and `e2` are integer types, the result will be the integer part of the division.
:`e1` and `e2` must be any numeric type, integral or floating point.
: if `e2` is `0` then an exception will be raised.
: `e1` and `e2` must have exactly the same type, so for example `32u8 / 32u16` and `32i8 / 32u8` are badly-formed expressions.

String Format Application/Integer Modulus (`%`)
: `s % e` returns the string `s` after substituting each occurrence of a formatting annotation within `s` with the corresponding parameter in `e`. For example, 
: `"Hello World: %d" % 1` returns `"Hello World: 1"`, and
: `"Hello %s: %d" % ("Planet", 10)` returns `"Hello Planet: 10"`.

: `e1 % e2` equals `e1 mod e2` when `e1` and `e2` are integers of any valid bitwidth.
: `e1` and `e2` must have exactly the same type, so for example `32u8 % 32u16` and `32i8 % 32u8` are badly-formed expressions.

Mathematical Modulus (`mod`)
: `e1 mod e2` evaluates to the remainder after subtracting every whole multiple of `e2` from `e1`.
: This is defined for any numeric value of `e`, integral or floating point (though `e2` should be an integer).
: For example, `10 mod 7` equals `3`, and
: `1/3 mod 7` equals `5` (in this case the [modular inverse](https://en.wikipedia.org/wiki/Modular_multiplicative_inverse) of 3 mod 7 is 5)
: `e1` and `e2` must have exactly the same type, so for example `32u8 mod 3u16` and `32i8 mod 5u8` are badly-formed expressions.

Exponentiation (`**`)
: `e1 ** e2` evaluates to the result of raising the value of `e1` to the power of the value of `e2`.
: `e1` and `e2` are defined only for numeric types.
: Unlike other operators, `e1` and `e2` may have different types. The result type is the type of `e1`, the base.
: This will raise an exception if the result would overflow the return type.
: Exponentiation in modulo arithmetic will not overflow.

Truncated Add (`!+`)
: `e1 !+ e2` is the truncated sum of `e1` and `e2`. If the sum would overflow `typeof(e1)`, then only the least significant bits are returned.
: This is only valid for integral types (signed or unsigned) for any valid bit length.
: `e1` and `e2` must have exactly the same type.

Truncated Subtract (`!-`)
: `e1 !- e2` is the value of `e1` less the value of `e2`. If this would overflow `typeof(e1)`, then only the least significant bits are returned. For example `-128i8 !- 1i8` would return `127i8`.
: This is only valid for integral types (signed or unsigned) for any valid bit length.
: `e1` and `e2` must have exactly the same type.

Truncated Multiply (`!*`)
: `e1 !* e2` is the product of `e1` and `e2`, ignoring overflow bits. For example, `16u8 * 16u8` would throw an overflow error, but `16u8 !* 16u8` would return `(16*16) & 255` or just `0`.
: This is only valid for integral types (signed or unsigned) for any valid bit length.
: `e1` and `e2` must have exactly the same type.

#### Comparison

Is Equal (`==`)
: `e1 == e2` returns `true` if and only if `e1` and `e2` are equal, where equality is defined differently for different types.
: For plain old data (POD) types, this amounts to value equality - two values are equal if they have the same bits.
: For structs, it is a _deep_ equality, _i.e._, each field and subfield is tested for equality.
: TODO: implement deep equality for structs.
: For object references, it is a _shallow_ inequality, _i.e._, only pointers are compared (that is, two object references are equal only if they are the same reference).

Not Equals (`!=`)
: `e1 != e2` returns `true` if and only if `e1 == e2` is false.

Less Than (`<`)
: `e1 < e2`  returns `true` if `e1` evaluates to an entity that is 'less' than the value of `e2`, otherwise it returns `false`.
: It is defined for numeric types, strings and arrays.  Strings are lexicographically ordered, like `strcmp` in C, but only bytewise, not multibyte chars like UTF-8.Arrays are ordered elementwise from low- to high- indices.  Tuples are not ordered.

Less Than or Equal To (`<=`)
: `e1 <= e2`  returns `true` if `e1` evaluates to an entity that is 'less than or equal to the value of `e2`, otherwise it returns `false`.
: It is defined for numeric types, strings and arrays.  Strings are lexicographically ordered, like `strcmp` in C, but only bytewise, not multibyte chars like UTF-8.Arrays are ordered elementwise from low- to high- indices.  Tuples are not ordered.

Greater Than (`>`)
: `e1 > e2`  returns `true` if `e1` evaluates to an entity that is 'greater' than the value of `e2`, otherwise it returns `false`.
: It is defined for numeric types, strings and arrays.  Strings are lexicographically ordered, like `strcmp` in C, but only bytewise, not multibyte chars like UTF-8.Arrays are ordered elementwise from low- to high- indices.  Tuples are not ordered.

Greater Than or Equal To (`>=`)
: `e1 >= e2` returns `true` if `e1` evaluates to an entity that is 'greater than or equal to' the value of `e2`, otherwise it returns `false`.
: It is defined for numeric types, strings and arrays.  Strings are lexicographically ordered, like `strcmp` in C, but only bytewise, not multibyte chars like UTF-8.Arrays are ordered elementwise from low- to high- indices.  Tuples are not ordered.

#### Object Access

Null Safety Check (postfix `!`)
: `e!` wraps expression `e` with a null safety check.  If `e` turns out to be null, rune will throw an exception.
: `e` can be any expression, even plain old data types.

Truncated Cast (`!<.>`)
: `!<t>e` converts `e` into an expression of type `t`, throwing away any bits in `e` that would overflow the size of type `t`. For example, regular cast `<u8>256` would throw an exception, but `!<u8>256` will return `0`.
: This is only valid for integral types (signed or unsigned) for any valid bit length.

Address Of (`&`)
: `&f` returns a first-class reference to a function `f`. (It does not work for arbitrary variables or objects).
: Note that `f` must be specified with concrete parameter types.

Field Access (`.`)
: `a.f` returns the value of field `f` within aggregate-typed expression `a` (either a struct, object, enum object) or an expression that evaluates to one.

Membership (`in`)
: `e in ae` returns `true` when `e` can be found within `ae`.
: Works for dict classes, arrays, classes when they overload.
: TODO: add support for substring search (as in python)

: TODO: get working for arrays, strings, tuples

Assign (`=`)
: `x = e` assigns the value of `e` to variable `x`.
: Note that this will create variable `x` if it does not already do so.  Subsequent assignments to `x` must be of the same datatype as `e`.
: When the left-hand side has form `<`_object_`>.x`, this assignment will add a new data member `x` to that object.
: TODO: decide whether or not to restrict this behavior to the class constructor.

Index (`[]`)
: `e1[e2]` evaluates to the content of element number `e2` in array or tuple `e1`.
: When `e2` is negative, or greater than the maximum index of `e1`, Rune will throw an exception.
: When `e1` is a tuple, `e2` must be a constant integer.

#### Misc

Maybe Nullable Type(postfix `?`)
: `T?` means an object that may either be null or something of type `T`.  An object of type `T` may not be null.
: `T` must be a type expression.

Range (`...`)
: Intended for case-expressions, it is used to denote a range of consecutive integers apply to a given case, _e.g._, `case 1...3`.
: When used in `typeswitch` statements, `case T_1...T_n` is valid when `T_i` is an integer type. The unsigned int types are totally ordered in ascending order of bit width (`u1, ..., u512`, _etc._), and so are the signed ints (`i1...i512`, _etc._). There is no order relation between signed and unsigned ints.

Conditional Expression (`?:`)
: `b ? be1 : e2` evaluates to `e1` if `b` evaluates to`true` and `e2` otherwise.
: Note that if either `b`, `e1` or `e2` is secret then all three are evaluated to avoid revealing timing information. Otherwise, only one of `e1` or `e2` is evaluated after `b`.
: Note also that branching on a secret in an if statement is a compile-time error, so using the ?: operator is a handy way to compute secret values without branching.

#### Precedence

The following lists operators in ascending order of precedence: operators on earlier lines have lower precedence than those on later lines. Operators on the same line have the same precedence.

```
expr ... expr
expr ? expr : expr
expr || expr
expr ^^ expr
expr && expr
expr mod expr
<, >, <=, >=, ==, !=
expr | expr
expr ^ expr
expr & expr
<<, >>, <<<, >>>
+, -, !+, !-
*, !*, /, %
!, ~, -expr, <expr>, !<expr>
expr ** expr
expr.expr, expr[expr], &func(...)
```

### Assignment operators

```
!+=  !=  &&=  +=  <<<=  <=  ==  >>=   ^=   **=  ||=
!-=  %=  &=   -=  <<=   =   >=  >>>=  ^^=  |=
```

### Modular expressions

## Builtin functions and classes

```
Dict
Heapq
max
min
abs
chr
ord
```

## Builtin relations

```
ArrayList
DoublyLinked
Hashed
HeapqList
LinkedList
OneToOne
TailLinked
```

## Builtin interators

```
range
```

## Classes

## Iterators

## Transformers

In short, transformers are compile-time interpreted Rune functions that create
new code or modify existing code. All Rune relationships are written in Rune
using transformers, which are able to modify both the parent and child classes.

The full Rune language is available to be interpreted in code transformers, and
the full Rune database representing code is available to transformers to modify
in any way.

Examples of cool capabilities transformers enable include:

* Writing relationships in Rune, which cannot be done using templates or
  inheritance.
* Creating binary load/save functions for an entire module's set of classes.
* Adding infinite undo-redo ability to a module's set of classes.
* Adding runtime reflection APIs similar to Java's.
* Generating optimized code for a parameterized algorithm, such as FFTs.

## Abstract Syntax

The following is a cut-down grammar that ignores all issues related to parsing,
disambiguation and precedence. It corresponds to the abstract syntax tree
built by the compiler after parsing.

In the following, we use a form of EBNF with the following conventions:

* Italicized words are nonterminals.
* Bold words and symbols are lexical tokens, for example keywords.
* The postfix asterisk ('e*') indicates zero or more occurrences of 'e'
* The postfix question mark ('e?') indicates zero or one occurrences of 'e'
* The bar 'e_1 | e_2' indicates the presence of either 'e_1' or 'e_2'
* Brackets '(' and ')' are for grouping complex expressions
* Capitalized words, like STRING, represents a constant value literal of the named type.  We do not describe them further (see above for more details).


<pre>
<em>runeFile</em> ::= <em>statement</em>*

<em>statement</em> ::=
      <b>appendcode</b> <em><em>pathexp</em></em>? <em>block</em>
    | <b>assert</b> <em><em>explist</em></em>
    | <em>assignment</em>
    | <em>call</em>
    | (<b>rpc</b> | <b>export</b> | <b>exportlib</b>)? <b>class</b> <em>ID</em> UINTTYPE? <b>(</b> <em>params</em> <b>)</b> <em>block</em>
    | <b>debug</b> <em>block</em>
    | <b>do</b> <em>block</em> <b>while</b> <em>exp</em>
    | <b>enum</b> <em>ID</em> <b>{</b> (<em>ID</em> (<b>=</b> INTEGER)?)* <b>}</b>
    | <b>export</b> <b>func</b> <em>ID</em> <em>proto</em> </em>block</em>
    | <b>export</b> <b>iterator</b> <em>ID</em> <em>proto</em> </em>block</em>
    | <b>export</b>? (<b>struct</b>| <b>message</b>) <em>ID</em> <b>{</b> (<em>ID</em> tyexp? (<b>=</b> <em>exp</em>)?)* <b>}</b>
    | <b>exportlib</b> <b>func</b> <em>ID</em> <em>proto</em> </em>block</em>
    | <b>extern</b> STRING <b>func</b> <em>ID</em> <em>proto</em>
    | <b>extern</b> STRING <b>iterator</b> <em>ID</em> <em>proto</em>
    | <b>extern</b> STRING <b>operator</b> (<em>binop</em>|<em>unop</em>|<em>assignop</em>) <em>proto</em>
    | <b>final</b> <b>(</b> parameter <b>)</b> <em>block</em>
    | <b>for</b> <em>ID</em>  <b>in</b> <em>exp</em> <em>block</em>
    | <b>for</b> <em>assignment</em> <b>,</b> <em>exp</em> <b>,</b> <em>assignment</em> <em>block</em>
    | <b>func</b> <em>ID</em> <em>proto</em> </em>block</em>
    | <b>if</b> <em>exp</em> <em>block</em> (<b>else</b> <b>if</b> <em>exp</em> <em>block</em>)* (<b>else</b> <em>block</em>)?
    | <b>import</b> <em>pathexp</em> (<b>as</b> <em>ID</em>)?
    | <b>importlib</b> <em>pathexp</em> (<b>as</b> <em>ID</em>)?
    | <b>importrpc</b> <em>pathexp</em> (<b>as</b> <em>ID</em>)?
    | <b>iterator</b> <em>ID</em> <em>proto</em> </em>block</em>
    | <b>operator</b> (<em>binop</em>|<em>unop</em>|<em>assignop</em>) <em>proto</em> </em>block</em>
    | <b>prependcode</b> <em>pathexp</em>? <em>block</em>
    | <b>println</b> <em>explist</em>
    | <b>print</b> <em>explist</em>
    | <b>raise</b> <em>explist</em>
    | <b>ref</b> <em>exp</em>
    | <b>relation</b> <em>pathexp</em> <em>pathexp</em> <em>label</em>? <em>pathexp</em> <em>label</em>? <b>cascade</b>? (<b>(</b> <em>explist</em> <b>)</b>)?
    | <b>return</b> <em>exp</em>?
    | <b>rpc</b> <em>ID</em> <em>funcdec</em>
    | <b>switch</b> <em>exp</em> <b>{</b> (<b>case</b> <em>exp</em> (<b>,</b> <em>exp</em>)* <em>block</em>)* (<b>default</b> <em>block</em>)? <b>}</b>
    | <b>transform</b> <em>pathexp</em> <b>(</b> <em>explist</em> <b>)</b>
    | <b>transformer</b> <em>ID</em> <b>(</b> <em>params</em> <b>)</b> <em>block</em>
    | <b>unittest</b> <em>ID</em>? <em>block</em>
    | <b>unref</b> <em>exp</em>
    | <b>use</b> <em>ID</em>
    | <b>while</b> <em>exp</em> <em>block</em>
    | <b>yield</b> <em>exp</em>

<em>assignment</em> := <em>lhs</em> <em>tyexp</em>? <em>assignop</em> <em>exp</em>

<em>call</em> ::= <em>lhs</em> <b>(</b> <em><em>args</em></em> <b>)</b>

<em>lhs</em> ::=
      <em>lhs</em> <b>(</b> <em>args</em> <b>)</b>
    | <em>lhs</em> <b>.</b> <em>ID</em>
    | <em>lhs</em> <b>[</b> <em>exp</em> <b>]</b>
    | <em>lhs</em> <b>[</b> <em>exp</em> <b>:</b> <em>exp</em> <b>]</b>

<em>label</em> ::=<b>:</b> STRING

<em>block</em> ::=  <b>{</b> <em>statement</em>* <b>}</b>

<em>proto</em> ::= <em>params</em> (<b>-></b><em>tyexp</em>)?

<em>params</em>::=  <b>(</b> <em>parm</em> (<b>,</b> <em>parm</em>)* <b>)</b>

<em>parm</em> ::=
      <b>var</b>? <em>ID</em> <em>tyexp</em>? (<b>=</b> <em>exp</em>)?
    | <b>var</b>? <b><</b> <em>ID</em> <b>></b> <em>tyexp</em>? (<b>=</b> <em>exp</em>)?

<em>explist</em> ::= <em>exp</em> (<b>,</b> <em>exp</em>)*
exp ::=
      <em>exp</em> <em>binop</em> <em>exp</em>
    | <em>exp</em> <b>?</b> <em>exp</em> <b>:</b> <em>exp</em>
    | <em>unop</em> <em>exp</em>
    | <b><</b> <em>exp</em> <b>></b> <em>exp</em>
    | <b>!<</b> <em>exp</em> <b>></b> <em>exp</em>
    | <b>&</b> <em>exp</em> <b>(</b> <em>explist</em> <b>)</b>
    | <em>ID</em>
    | STRING
    | INTEGER
    | FLOAT
    | RANDUINT
    | <b>true</b>
    | <b>false</b>
    | <em>lhs</em>
    | <b>[</b> <em>exp</em> (<b>,</b> <em>exp</em>)* <b>]</b>
    | <b>(</b> <em>exp</em> <b>)</b>
    | <em>tupleexp</em>
    | <em>tyexp</em>
    | <b>secret</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>reveal</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>arrayof</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>typeof</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>unsigned</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>signed</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>widthof</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>null</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>isnull</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>notnull</b> <b>(</b> <em>exp</em> <b>)</b>

<em>args</em> ::= (<em>ID</em> <b>=</b>)? <em>exp</em> (<b>,</b> (<em>ID</em> <b>=</b>)? <em>exp</em>)*

<em>pathexp</em> ::= <em>ID</em> (<b>.</b> <em>ID</em>)*

<em>tupleexp</em> ::=
      <b>(</b> <b>)</b>
    | <b>(</b> <em>exp</em> <b>,</b> <b>)</b>
    | <b>(</b> <em>exp</em> (<b>,</b> <em>exp</em>)* <b>,</b>? <b>)</b>

<em>unop</em> ::= <b>!</b> | <b>~</b> | <b>-</b>

<em>binop</em> ::=
      <b>+</b> | <b>-</b> | <b>*</b> | <b>/</b> | <b>%</b> | <b>&&</b> | <b>||</b> | <b>^^</b> | <b>&</b> | <b>|</b> | <b>^</b> | <b>**</b> | <b><<</b> |  <b>>></b>
    | <b><<<</b> | <b>>>></b> | <b>!+</b> | <b>!-</b> | <b>!*</b> | <b>~</b> | <b><</b> | <b><=</b> | <b>></b> | <b>>=</b> | <b>==</b> | <b>!=</b> | <b>!</b>
    | <b>[</b> <b>]</b> | <b>in</b> | <b>mod</b>

<em>assignop</em> ::= <b>=</b> | <b>+=</b> | <b>-=</b> | <b>*=</b> | <b>/=</b> | <b>%=</b> | <b>&=</b> | <b>|=</b> | <b>^=</b>
    | <b>&&=</b> | <b>||=</b> | <b>^^=</b> | <b>**=</b> | <b><<=</b> | <b>>>=</b> | <b><<<=</b> | <b>>>>=</b> | <b>!+=</b> | <b>!-=</b> | <b>!*=</b>

<em>tyexp</em> ::=
      <em>tyexp</em> <b>|</b> <em>tyexp</em>
    | <em>typrim</em>
      
<em>tyexplist</em> ::= <em>tyexp</em> (<b>,</b> <em>tyexp</em>)*

<em>typrim</em> ::=
    | <em>tyliteral</em> <b>...</b> <em>tyliteral</em>
    | <em>pathexp</em> (<b><</b> <em>tyexplist</em> <b>></b>)? (<b>?</b>)?
    | <b>[</b> <em>typrimlist</em> <b>]</b>
    | <b>(</b> <em>typrimlist</em> <b>)</b>
    | <b>(</b> <b>)</b>
    | <b>typeof</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>unsigned</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>signed</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>secret</b> <b>(</b> <em>typrim</em> <b>)</b>
    | <em>tyliteral</em>

<em>typrimlist</em> ::= <em>typrim</em> (<b>,</b> <em>typrim</em>)*

<em>tyliteral</em> ::=            
    UINTTYPE | INTTYPE | <b>string</b> | <b>bool</b> | <b>f32</b> | <b>f64</b>

</pre>


