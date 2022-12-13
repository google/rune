# ᚣ Rune for Python Programmers

_A faster, safer, and more productive systems programming language_

Rune aims to provide much of the power of Python in a fast systems-programming
language designed for safety and speed.  Rune tries to offer much of the
expressive power of Python, including function polymorphism, without the
overhead of garbage collection.

This document is a brief introduction to Rune for Python programmers, with a
focus on the differences, shown by example.  Hopefully you will find this simple
to read based on your Python experience.

## Difference #1: Curly braces, not indentation

Rune uses curly braces, like C, C++, Java, C#, JavaScript, Go, etc.  This call
was tough to make, and there are pros and cons.

Pros:

- Many Python coders enjoy indentation-based statement grouping, which
  requires less typing and results in fewer lines of code.

Cons:

- Indentation based statement grouping leads to bugs when tabs and spaces
  are used in the same file.
- Some folks have very low vision (including Rune's original author).  The
  blind dislike Python’s indentation-based grouping, which screen readers by
  default do not read out loud.

Python:

```python
if a > b:
	return a
else:
	return b
```

Rune:

```rune
if a > b {
	return a
} else {
	return b
}
```

To encourage similarity in programming style, Rune requires the open curly brace
to be on the same line as the statement: Rune requires it.

## Func, not def

There are lots of languages that call functions a word that implies function,
such as "function", "fun", "func" or "fn".  Rune uses "func" as a compromise
between "function" and "fn".

Python:

```python
def max(a, b):
	return a if a >= b else b
```

Rune:

```rune
func max(a, b) {
	return a >= b ? a : b  // Rune uses most C-style operators.
}
```

Functions in both Python and Rune are highly polymorphic.  This means that you
can pass different types of objects to functions, and as long as all the
operations in the function are defined for those objects, it just works.

## Predefined functions and variables

The "max" function is predefined in both Python and Rune.  Predefined
Python-like functions and variables in the global scope are:

*   min
*   max
*   abs
*   range
*   argv (not sys.argv)
*   randString (cryptographically random bytes suitable for secret keys)
*   ord
*   chr

Note that Rune has no character type.  You can use '\n' and the usual escape
sequences but the result is a u8 integer.  The ord and chr functions convert
from a 1-character string to a u8 and back.

## Python-like CamelCase, not underscores

```rune
class Person(self, name) {
	self.name = name

	func helloWorld(self) {
		println "Hello, world, from ", self.name, "."
	}
}
```

Rune uses CapitalCamelCase by convention for classes, just like Python.  Lower
case is for variables, functions and methods.  Underscores are not legal
in identifiers.  Underscores are reserved for code generators where they ensure
non-collision with user variable names.

Rather than using \_ to create private identifiers, Rune makes identifiers public
between packages with the export keyword.  Modules within a package (files
within a directory) can always access identifiers in other modules within the
package.  The rationale for this is that ownership boundaries between code files
rarely divide modules within a package.

## Strings, text, variables and comments are all UTF-8

Strings in Rune are internally represented as arrays of 8-bit unsigned integers,
which are displayed as UTF-8 when printed.  Note that for security reasons (see
the Trojan Source attack), control characters are not allowed in Rune source files
other than newline, return, and tab characters.

The following is valid in Rune:

```
hässlich = "Μεδουσα"
schön = "Ἀφροδίτη"
\. = "."  // You can even used keywords as variables, if escaped with a backslash.
println schön , " is prettier than ", hässlich , \.  // υποκειμενικά
```

## Comments

Rune uses C-like comments:

```rune
// This is a single-line comment.
/*
  This is a block comment.
  Note that /* embedded comments */ do not end the comment block.
*/
```

## Rune is a systems programming language

Rune is meant for writing high performance secure code.  This drives the
majority of changes in Rune from Python.  For example:

*   No garbage collection (Rune’s memory system is memory safe, and fast)
*   Compiled, not interpreted (Rune compiles using the LLVM backend)
*   Rune programs are debugged using gdb

## Constructors in class declaration, not \_\_init\_\_

In general, Rune tries to avoid accessing what look like internal
compiler-specific variables where possible.  For example, constructors are not
declared with \_\_init\_\_.

Python:

```python
class Point:
	def __init__(self, x, y):
		self.x = x
		self.y = y

	def manhattanDist(self):
		return abs(self.x) + abs(self.y)

# Python 3.7+:
from dataclasses import dataclass

@dataclass
class Point:
	x: int
	y: int

	def manhattanDist(self):
		return abs(self.x) + abs(self.y)
```

Rune:

```rune
class Point(self, x, y) {
	self.x = x
	self.y = y

	func manhattanDist(self) {
		return abs(x) + abs(y)
	}
}
```

Class construction parameters are passed to the class, which now looks more like
a function where methods are just sub-functions.

## Integers

Integers in Python are either fixed-width integer, rather than infinite
precision.  As a cryptography-centric systems language, Rune uses only
fixed-integer widths, but the width can be any size up to 2<sup>24</sup>-1.

Python:

```python
modulus = 2**255 - 19  # Prime number used in curve25519 elliptic curve crypto.
```

Rune:

```rune
modulus = 2u256**255 - 19u256
```

Rune integer constants without a type suffix are unsigned 64-bit integers.  To
specify a width, add u\<width\> for unsigned integers, or i<width> for signed.

```
123 → 64-bit unsigned integer
0xdeadbeefu256 → a 256-bit unsigned integer
```

## Rune is strongly statically typed

In Rune, every type is automatically determined from constants at the leaves of
expression trees.  Further, type constraints can be annotated on every variable,
parameter, and function return type.  These type constraints are enforced at
compile-time.

If you call a function twice, with different parameter types, Rune instantiates
two different functions, one for each.  Basically, all functions in Rune are
template functions.

```rune
func add(a, b) {
	return a + b  // Like Python, addition of strings or arrays means concatenate.
}

println add(3, 5)

println add("This is", " a test")
```

Rune gives a compiler error when integer sizes are mixed.  For example:

```rune
if x == 3u32 {  // An error if x is not a u32.
	…
}
if x == <x>3  {  // Works for any integer type of x wide enough to hold 3..
	...
}
```

## Single type variables

Variables in Rune can only have a single assigned type for any given
instantiation of a function.  The following issues a compiler error:

```rune
x = 1
x = "test"  // Compiler error here since x changed type.
```

This is needed to make Rune strongly typed.

## Recursion

Types of every value in a function are determined from the parameter types
passed to the function.  For non-recursive functions, this always works out.
However, for a recursive function, Rune determines the return type from the
first return statement, which _must_ come before any recursive call.

Legal in Rune:

```rune
func fact(n) {
	if n == 1 {
		return 1
	}
	return n * fact(n - 1)
}
```

This will give a compiler error:

```rune
func fact(n) {
	if n != 1 {
		return n * fact(n - 1)
	}
	return 1
}
```

Try to evaluate the basis case first.

## Null

Unlike Python, Rune needs to know the concrete type of every constant, including
null, which acts like Python’s None.  In Rune, null is the empty reference to a
specific class type.  For example:

```rune
point p = null(Point)  // Initialize a Point reference to null.
```

For classes with template parameters, you may specify a fully qualified type like:

```rune
Point p = null(Point(u32, u32))
```

In most cases, Rune figures out the type of null from just the class name.  You
can also get away with referring to the type of an existing variable, including
self:

```rune
// null on its own here is assumed to be of the same type as self.
class Tree(self: Tree, label: string, left = null(Tree), right = null(Tree)) {
	self.label = label
	self.left = left
	self.right = right
	self.parent = null(self)  // In the body, you must be more specific.
	...
}

n1 = Tree("N1", Tree("L1"), Tree("L2"))
n2 = Tree("N2", Tree("L3"), Tree("L4"))
n3 = Tree("N3", n1, n2)
n4 = Tree("N4", null(Tree), Tree("L5"))
root = Tree("root", n3, n4)
```

Rune supports null safety.  By default, type constraints declare variables to
be non-null.  To test if a variable is null, use isnull():

```rune
if !isnull(point) {
	println point!.toString()  // Works if Point has a toString method.
}
```

Rune creates a default .toString() method for you in debug mode (using -g flag),
which can be called from gdb.  The ! suffix checks that a value is not null
null at compile time unless the compiler can prove this check is not
needed.

## Printing

Rune assumes there is a console output that can be used for debugging and/or
logging.  It is a compile-time error if you try to print a secret.  This causes
a compiler error:

```rune
println rand32  // An error because rand32 generates a secret random value.
```

Like Python before [literal string interpolation](https://peps.python.org/pep-0498/), Rune embraces printf-like formatting with the % operator

```rune
println "%x" % (2u256**255 - 19u256)  // Print curve25519’s modulus in hex.
```

Currently supported format specifiers are:

* %s		- match a string value
* %b		- Match an bool value: prints true or false
* %i		- Match an Int value
* %u		- Match a Uint value
* %x		- Match an Int or Uint value, print in lower-case-hex
* %[\<type\>]	- Array of \<type\>
* %(\<type\>)	- Tuple of \<type\>

All format specifiers are checked at compile time and are type safe.  In
general, you can print builtin types directly without formatting, and a default
format will be used.

The only difference between print and println is that println appends a newline.
These are equivalent:

```rune
print "Hello, World!\n"
println "Hello, World!"
```

## Dictionaries

In Rune, all dictionary entries have to have the same key types and the same
value types, and they have to be declared when you create the dictionary:

Rune:

```rune
d = Dict(string, u32)
d["Bill"] = 123u32
```

Python:

```python
d = {}
d["Bill"] = 123
```

## Type constraints

Rune tries to be as polymorphic as possible, which was inspired from Python.
However, in most cases,  there is value in clearly stating the types of
variables and functions.  For example:

```rune
func addThree(a: Int | Uint) -> typeof(a) {
	return a + <a>3  // <a> means cast to the type of a.
}
```

Type constraints give Rune no additional expressive power, but help avoid bugs
and convey the programmer’s intent to the reader.  It is very common in Rune to
cast a value to the type of another value, which is what \<a\> means, which is
shorthand for \<typeof(a)\>.

You can take the union of two type constraints with the | operator, [as in Python 3.10+](https://docs.python.org/3/library/typing.html#typing.Union). This means
that either type is allowed.  All unsigned integers match Uint, and all signed
integers match Int.  To specify a particular width:

```rune
func add3(a: u32) -> u32 {
	return a + 3u32
}
```

You can also restrict types in variable assignments:

```rune
a: string = 0x123.toString(2)  // a will be "100100011"
```

## Looping

Rune supports Python-style looping:

```rune
for i in range(0, 11, 2) {  // Print 0, 2, 4, …, 10, same as Python.
	println i  // "println" adds a \n at the end, while "print" does not.
}
i = 0
while i <= 10 {
	println i
	i += 2
}
```

And also C-like for-loops:

```rune
for i = 0, i <= 10, i += 2 {
	println i
}
```

And one more loop structure to reduce the assignment-in-condition problem:

```rune
do {
	c = getNextChar()
} while c != ‘\0’ {
	processChar(c)
}
```

The do-block always executes, and the while-block only executes if the condition
is true, after which we jump to the start of the do-block.  If the condition is
false, the loop terminates.  This avoids the common C/C++ hack:

In C, the programmer would be tempted to write:

```c
int c;
while ((c = getNextChar()) != '\0') {
	processChar(c);
}
```

## Iterators

Rune uses "co-routines" which are similar to Python’s "generators":

```rune
// Simplified range iterator:
iterator range(n) {
	for i = <n>0, i < n, i += <n>1 {
		yield i
	}
}
```

Currently,only one yield statement is allowed in an iterator, and iterators are
always inlined.

## Tuples and arrays

Tuples and arrays are supported in Rune.  Currently, unpacking syntax is not yet
supported, but is planned.

```rune
l = [1, 2, 3, 4]
names = ["Bill", "Bob", "Dave"]
```

Note that array elements _must have the same type_, unlike Python.  Tuples are
defined like Python, but unlike Python, parentheses are mandatory:

```rune
point = (x, y)
x = point[0]; y = point[1]
```

However, _tuples in Rune are mutable!_  Tuples are always passed by
reference, not value, just like arrays.

## Builtin type expressions

Each builtin type has a class representing its type, which holds some methods for the type:

*   Array
*   Function
*   Bool
*   String
*   Uint
*   Int
*   Tuple
*   Class
*   Float

These are template types that match categories of concrete types.  Some examples of constants of each type:

*   Array: [1, 2, 3] or [["one", "two"], ["three"]], arrayof(u8) for an empty array of u8
*   Bool: true, false  (lower case)
*   String: "Hello, World!"
*   Uint: 123u64, 0 (a u64), 0xdeadbeefu32
*   Int: -1i32, 123i64
*   Tuple: ("Bill", 123, [1u32, 2u32])
*   Class: MyString("test")
*   Float: 1.1e3 (defaults to f64, like C++ double), 3.14159f32, 2.0f64

Each concrete type can also be specified with a type expression:

```text
Array: [string], [[u32]]
Bool: bool
String: string
Uint: u64, u123
Int: i64, i2048
Tuple: (string, u64, [u32])
Class: typeof(MyString(string))
Float: f32, (f32, f64)
```

To create an empty array of a given type:

```rune
emptyArrayOfStrings = arrayof(string)
```

## Class templates

All functions, including class constructors, are templates in that calling them
with different types of parameters results in different functions being
instantiated.  This is the heart of polymorphism.  Simply calling a class
constructor with different types of arguments does not create a new class.

For classes only, you can specify the template parameters that will instantiate a
new class when different datatypes are used by putting the parameter name in
angle brackets:

```rune
class Point(self, <x>, <y>) {
	self.x = x
	self.y = y
…
}
```

This allows, for a template Matrix class that can have different element types
based on how the constructor is called.

## Secrets

Rune is designed for safer processing over secret keys used in cryptography.
When a secret is declared, the code generated by the compiler will run in
constant time when processing data involving that secret.

```rune
password = secret("PaSsw0rd1")
if password == "password" {  // This line is a compiler error.
	throw "Don’t use \"password\" as the password!"
}
```

You cannot branch based on a Boolean value derived from a secret.  You may not
use a secret integer as an index into an array.  You also may not print a
secret.  The Rune compiler automatically generates constant-time code when
arguments to an operator are secret.

Eventually, you will want to reveal data derived in part from a secret, for
example after encrypting a secret message, the ciphertext can be revealed:

```rune
password = secret("PaSsw0rd1")  // Typical poor password.  Let’s not leak it!
message = secret("Learn Rune!")
ciphertext = AesGcm256Encrypt(password, message)
println reveal(ciphertext)
```

In Rune, secrets are viral.  Any operation involving a secret yields a datatype
that is also secret.  We can add a secret and non-secret, but the result is
secret.  If you assign a non-secret value to a variable containing a secret, the
value becomes secret.

Secret is part of the datatype in Rune, enabling efficient static checking.

## Random numbers

Cryptography does not work without hard to guess random numbers.  Rune provides
cryptographic pseudo-random numbers (seeded with true random) on demand.  To
generate a random integer:

```rune
key = rand256  // A 256-bit random unsigned secret integer
```

Random strings are easy, too:

```rune
nonce = randString(16)
```

To print them, you’ll have to reveal them:

```rune
println reveal(nonce)
```

Be careful what you reveal!

## Switch statements

As most system programming languages, Rune includes a switch statement:

```rune
switch x {
	case 1 {
		println "one"
	}
	case 2 {
		println "two"
	}
	default {
		println "many"
	}
}
```

Which in Python would either have to be an `if`/`elif` chain or, from [Python 3.10 on](https://peps.python.org/pep-0636/), a `match`/`case`:

```python
match x:
    case 1:
        print("one")
    case 2:
        print("two")
    case _:
        print("many")
```

Unlike C, the cases can be arbitrary expressions.  There is no fall-through, and
no need for a break statement.

## Switching on types

Like Python, in Rune, only operators can be overloaded, not functions.  This
leads to situations where we wish we had different functionality in a function
based on the types of arguments passed.  In Python we can use
`isinstance(object, type)` or
[`functools.singledispatch`](https://docs.python.org/3/library/functools.html#functools.singledispatch)
to test the object type and change a function’s behavior.

Similarly, in Rune, we can switch on the type of an object, and only the
matching case is instantiated in the compiled code:

```rune
class MyString(self, value) {
	typeswitch value {
		case string {
			self.value: string = value
		}
		case u8 {
			self.value: string = <string>[value]
		}
		case [u8] {
			self.value: string = <string>value
		}
		case MyString {
			self.value: string = value.value
		}
	}
}
```

The compiler figures out the type of value at compile time and only instantiates
the matching case.  Types are first-class citizens in Rune.  You can pass them
to functions, assign them to variables, and use them in switch statements.

## Import and use statements

The basic Python module import statement is supported, but not `from foo import
*`. Rune also supports `import as`, e.g. `import numpy as np`.

To import modules in the same package (directory), use `use foo` syntax, which
means you can access that module's functions, even when not marked "export",
and you don't have to use a module prefix.

```rune
import math as m  // Import Rune's math package.
use hashing  // Only works if hashing.rn is in the same directory.

println "hashValue(123) = ", hashValue(123)
println "math.sqrt(123.0f64) = ", m.sqrt(123.0f64)
```

## Linking with C functions

If you need to call a C/C++ function, use the extern "C" declaration:

```rune
extern "C" putchar(c: u8)
```

You can use ordinary types, and Rune will take care of the integration
```rune
extern "C" func readln(maxLen: u64 = 0u64) -> string
```

**Currently**, if you are passing of receiving integers there is a limitation, you can only use small integers (`<= u64`).

## Operators

Like many languages, Rune has support for most C operators.  The ones that have
changed are:

*   `x**2` means x squared.
*   `A ^ B` means A XOR B
*   `i++, ++i, i--, --i` are deleted, as in Python.
*   Rotation operators (often used in cryptography) are added: `x <<< dist`, and `x >>> dist`.
*   Arithmetic overflow throws an error!  An exception is `-1u32` is allowed.
*   In the rare cases you want overflow to be undetected, use `!+, !-, !*, and !/`
*   Down-conversion that can truncate bits should use `!<type>`, e.g. `!<u256>-1i512`.

Modular addition is also quite common in cryptography, so the mathematical
notion of "mod" has been added:

```rune
// A is Alice’s pubkey, a is her privkey, g is the group generator, and p the prime modulus.
A = g**a mod p
B = g**b mod p
aliceShared = B**a mod p
bobShared = A**b mod p
assert A**b == B**a mod p
```

Mathematical expressions to the left of mod are evaluated modulo the modulus.

## Overloading operators

Rune does this a bit differently:

```rune
// <x> and <y> here mean they are class template variables.
// Different classes will be instantiated for each set of types passed for x and y.
class Point(self, <x>, <y>) {
	self.x = x
	self.y = y

	operator + (a: Point, b: Point): Point {
		return Point(a.x + b.x, a.y + b.y)
	}
}
```

Operator overloading in Rune can specify type restrictions, which can be
concrete types like u32, or type classes like Uint.  The compiler generates an
error if more than one operator overload matches a given call.

You can define the overload anywhere, not just in a class.  They become global,
which should not be a problem so long as you include a reference to an object of
your class as a parameter.

## The toString() method

If a class has a toString() method, it will be called in print and println
statements, as well as throw.  This is like Python's `__repr__` (if `__str__` is missing).
If you print an object instance of a class
without defining a toString() method, a default method will be added for you,
but only in debug mode, specified with the -g flag.

Int.toString and Uint.toString take a base as a parameter, which defaults to 10:

```rune
println 0x123.toString(8)  // prints 443
```

## Var parameters

Because Rune is a systems programming language, efficiency is critical.  Python
and Rune allow tuples to be returned which is the Pythonic way of returning
multiple values.  However, this creates new values on the stack when it is often
more efficient to directly overwrite existing values.

Rune offers Pascal-like "var" parameters (see also C#'s `ref` keyword):

```rune
func increment(var x) {
	x += <x>1
}
```

## Parameters are immutable by default

Because Rune is designed for safety, parameters are immutable and cannot be
assigned to, unless they are declared ‘var’.  This is important in Rune since
folks may forget that passing in a u128 will be by reference while passing in a
u32 will be by value.

Legal in Python:

```python
def inc(x):
	x += 1
	return x
```

Compiler error in Rune, because x is const:

```rune
func inc(x) {
	x += 1  // This assignment generates a compiler error.
	return x
}
```

## Values are copied (or moved) on variable assignment

In Python, if you want a local copy of an array, you must use a hack like this:

```python
localList = list[:]
```

In Rune, all assignments imply deep copy for built-in types.  For Class object
references, only the reference is copied.

```rune
listCopy = list  // Copies the array
assert(listCopy == list)  // True because Rune does deep list comparison.
a1 = Foo("test)  // Create a Foo object
a2 = a1  // Does not copy the Foo object in a1.  Copies the reference instead.
assert(a1 == a2)  // True because the references are equal
t1 = (1, "two")
t2 = t1  // Makes a copy of the tuple in t1
assert(t1 == t2)  // Rune does not deep compare tuples.
```

## Current builtin type methods

*   `Array.length()` -- Returns the length of the array in native machine width.
*   `Array.resize(length)` -- Resize the array.  Length is in native machine width.
*   `Array.append(element)` -- Append the element to the array.
*   `Array.concat(array)` -- Concatenate the arrays.
*   `Array.reverse() ` -- Reverse the elements in the array.
*   `Array.toString() ` -- Convert the array to a string representation.
*   `String.length()` -- Returns the length of the string in native machine width.
*   `String.resize(length)` -- Resize the string.  Length is in native machine width.
*   `String.append(c: u8)` -- Append the character to the array.
*   `String.concat(s: string)` -- Concatenate the strings.
*   `String.reverse() ` -- Reverse the characters in the string byte-by-byte.
*   `String.toUintLE(type: Uint)  // Eg s.toUintLE(u512).  Pass an integer type, not an integer width.
*   `String.toHex()` -- Convert the binary string to a hexadecimal string twice as long.
*   `String.fromHex()` -- Convert hexadecimal string to binary string.
*   `String.find()` -- Like Python find.
*   `String.rfind()` -- Like Python rfind.
*   `Uint.toStringLE()` -- Convert an unsigned integer to a string, little-endian.
*   `Uint.toString(base=10)` -- Convert an unsigned integer to a string, using the base.
*   `Int.toString(base=10)` -- Convert a signed integer to a string, using the base.
*   `Bool.toString(` -- Convert a bool value to the string "true" or "false".
*    Tuple.toString()  -- Convert the tuple to a string representation.

## Unit tests

In Rune, a special `unittest` statement is provided:

```rune
func fact(n) {
	if n == 1 {
		return 1
	}
	return n*fact(n-1)
}

unittest factTest {
	if fact(6) != 720 {
		throw "Incorrect value for fact(6)"
	}
	println "Passed"
}
```

These tests do nothing unless they are in the main module, just like if you would state, for example,

```python
if __name__ == "__main__":
	main()

do_unittests()
```

in Python.

## Rune relationships

Relationships differentiates Rune from nearly every other language.  There is a
decades old bug in our computer languages:

> Who informs the parent objects of a child object, when a child object is destroyed?

Python is garbage-collected, and does not have this dangling-pointer problem,
and Rune attempts to have similar power, while running fast.  However, if a
child object is accidentally left hanging off of one of its parents, Python will
have a memory leak.

Instead of pointers, Rune has object references, similar to Python.  Most complex
relationships between classes are instantiated with relation statements.  For
example, to create a DoublyLinked relationship between a Graph class and a Node
class:

```rune
relation DoublyLinked Graph Node cascade
```

The "cascade" at the end means cascade-delete.  If you destroy a node, as in:

```rune
node.destroy()
```

Then the auto-generated Node destructor will automatically remove itself from
its Graph.  If the graph object is destroyed:

```rune
graph.destroy()
```

Then the graph’s auto-generated destructor will destroy all its nodes, because
we specified cascade-delete.  Otherwise, it would simply remove them from the
doubly-linked list before freeing the graph object.

If you further define an Edge class and want a directed graph, you can add two
additional relationships:

```rune
relation DoublyLinked Node:from Edge:out cascade
relation DoublyLinked Node:to Edge:in cascade
```

This means that Node has an outEdges iterator, and an inEdges iterator:

```rune
// Print names of nodes in the graph that are reachable by traversing only forward edges.
func printReachableNodes(node: Node, reachedNodes) {
	print node.name
	node.visited = true
	reachedNodes.append(node)
	for edge in node.outEdges() {
		otherNode = edge.toNode
		if !otherNode.visited {
			printReachableNodes(otherNode, reachedNodes)
		}
	}
}
```

Now destroying the graph recursively deletes all its nodes and edges.

You can create your own relationship types, but take care!  Rune’s safety
guarantees require relationship generators to be bug-free, which is harder than
it sounds.  Most folks will just use the default relationship types, which are:

* LinkedList - Singly-linked list: insert is fast, remove is slow.
* DoublyLinked - Doubly-linked list: both insert and remove are fast.
* TailLinked - Like LinkedList, but has a fast append function.
* OneToOne - Parent has one child, and child has one parent
* Array - Like vectors of classes, but safer: children know how to remove
  themselves.
* Heapq - Binary heap queue, supporting constant-average-time push, log(n) pop,
  always returns smallest element, or largest if you set ascending false.
* Hashed - Hash table relationship, ordered by default.  Constant time
  insert, find, and removal.

Let's take a look at these relationship statements:

```rune
relation TailLinked Node:From Edge:Out cascade
relation TailLinked Node:To Edge:In cascade
```

TailLinked is defined in builtin/taillinked.rn, and is written in Rune.  It is
an efficient tail-linked list where the parent, Node, in this case, has a
firstOutEdge and a lastOutEdge object reference.  Since it is tail-linked, only
the forward iterator is defined, so given a node, looping through its outEdges
looks like:

```rune
for edge in node.outEdges() {
	destNode = edge.toNode
	// Do something with node...
}
```

## Reduce memory leaks with auto-generated destructors

In Python, we manually write destructors when we have complex relationships
between objects.  For example, we might have a segment object in a path that is
being merged with other segments that are all inp a straight line.  In Python we
would still need to remove the segment from all the relationships it has to
other segments and the parent object (eg a Polyline), or a memory leak would
result.

Rune saves you the hassle, and creates a destructor function that does this for
you.  Otherwise, Rune’s memory system acts almost just like Python’s garbage
collection, without actually doing garbage collection under the hood.  As stated
above, you are free to remove the object from cascade-delete relationships
manually, in which case the object’s destructor is automatically called when it
goes out of scope.

## Destructor hook

Occasionally you may need to create a destructor hook, which is a function
called when an object is destroyed.  Rune uses "final" methods for this:

```rune
class Foo(self, name) {
	self.name = name

	final(self) {
		println "Destroying ", self.name
	}
}

foo = Foo("123")
foo = null(Foo)
```

This results in "Destroying 123", because the Foo value stored in foo is
destroyed when foo is overwritten with null.

## Memory safety

One of the coolest features of Rune is its safe yet high performance memory
management.  All data is stored in dynamic arrays or on the stack.  These arrays
are not fixed like memory returned by malloc, and are occasionally compacted to
recover freed memory.  Top level objects are reference counted, and may not be
in pointer-loops with other reference counted objects..

Most objects should be in cascade-delete relationships.  These objects have no
need for reference counting: they will be destroyed when one of their
cascade-delete parents is destroyed.  Cascade-delete objects can safely be in
relationship loops.

This scheme offers memory safety like Rust, with improved performance, and
without the no-pointer-loop (other than in "unsafe" code) restriction.

## Generators

Not be confused with Python’s "generators", which other languages call
co-routines, Rune’s generators are actual code generators!  Rune generators are
interpreted by the compiler to instantiate code in existing classes, methods,
and functions.  Relationship generators automatically update both the parent and
child destructors to clean up when a child is destroyed, cascade-delete if the
relationship is cascade-delete, or remove children that are not cascade-delete
before destroying the parent.

This simple rule ensures that dangling pointers in Rune are impossible, so long
as the relationship generators are bug-free.  Generators are more powerful than
templates.  Generators can modify existing class methods, while templates
cannot.

Like complex C++ templates, most users will never write Rune generators.  If
you’ve read this far, and still want to see the magic under the hood, take a
look at existing relationship generators in the rune/builtin directory, such as
doublylinked.rn and hashed.rn.  Long-term, Rune’s generator capability will be
enhanced to have similar power to Java’s mirror classes, but they run at
compile-time.  For now, only features required to support the builtin
relationships are implemented.

## Running Rune

Follow Rune’s README to build and install Rune.  After that, you can create a
rune program, say the classic hello world, containing:

```rune
println "Hello, World!"
```

In a file called hello.rn.  Then compile it:

```bash
$ rune hello.rn
```

And run it:

```bash
$ ./hello
```

To debug it with gdb, compile with the -g flag:

```bash
$ rune -g hello.rn
$ gdb ./hello
```

In the near-term, you’ll likely run into compiler bugs.  If you do, ping me, and
I’ll fix them.  Email bugs to
[waywardgeek@google.com](mailto:waywardgeek@google.com).

## The directed graph example

The power of Rune really shows when trying to create objects even as simle as a
graph.  Look how short the graph definition is.  An example
depth-first-traversal function is defined to print all the reachable nodes from
a given node, when traversing only forward edges.

```rune
class Graph(self) {
}

class Node(self, graph, name) {
	self.name = name
	self.visited = false
}

class Edge(self, outNode: Node, inNode: Node) {
	outNode.appendOutEdge(self)
	inNode.appendInEdge(self)
}

relation DoublyLinkedList Graph  Node cascade
// Edges have a "fromNode" and a "toNode".
// Nodes have "outEdges" and "inEdges".
relation DoublyLinkedList Node:from Edge:out cascade
relation DoublyLinkedList Node:to Edge:in cascade

// Print names of nodes in the graph that are reachable by traversing only forward edges.
func printReachableNodes(node: Node, reachedNodes) {
	print node.name
	node.visited = true
	reachedNodes.append(node)
	for edge in node.outEdges() {
		otherNode = edge.toNode
		if !otherNode.visited {
			printReachableNodes(otherNode, reachedNodes)
		}
	}
}

func clearVisitedFlags(reachedNodes) {
	for i in range(reachedNodes.length()) {
		reachedNodes[i].visited = false
	}
}

unittest graphTest {
	// Build an example graph.
	graph = Graph()
	a = Node(graph, "A"); b = Node(graph, "B")
	c = Node(graph, "C"); d = Node(graph, "D")
	e = Node(graph, "E"); f = Node(graph, "F")
	Edge(a, b); Edge(b, c); Edge(c, a)
	Edge(d, b); Edge(d, a); Edge(c, e)
	Edge(e, a); Edge(f, d); Edge(f, c)

	// Should print ABCE.
	reachedNodes = arrayof(typeof(a))
	printReachableNodes(a, reachedNodes)
	println
	// Clear the visited flags on all the nodes we reached.
	clearVisitedFlags(reachedNodes)
}
```

## Write your own crypto... in Rune! (but _never_ use it!)

See the Rune code in the crypto\_class directory.

One of the driving factors behind Rune is to help folks write cryptographic code
safely.  Applications in secure enclaves tend to be crypto-heavy, and experience
shows that it is nearly impossible to get this sort of code right without help
from the compiler.

I teach how to build a [cryptographic
sponge](https://en.wikipedia.org/wiki/Sponge_function), and how to use them to
build several fundamental crypto primitives such as collision-resistant hash
functions, and also I teach how implement a basic Diffie-Hellman public key
exchange.  If you write your crypto in Rune, you’ll likely get the constant-time
part right.
