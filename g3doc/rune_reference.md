# Rune Language Reference

**Authors**: waywardgeek@, aidenhall@, ethangertler@, arwilson@

[TOC]

## Features and Design Choices

1. Goal - High-level python systems language.

2. [Memory safety](#memory-safety) without garbage collection.

3. [Relations](#relations) - unique to Rune, multiple classes can be extended simultaneously to establish relationships between them.  For example,

  `relation LinkedList Graph Node cascade`

adds a linked-list of nodes to the Graph structure:  the Graph class is augmented with a new member, a link to the first Node.  The Node structure is augmented with a link to the Graph parent, and a link to the next Node in the Graph.

This provides polymorphic collections in a single operation that adjusts both classes simultaneously. Other languages support polymorphic collections using  _templates_ (C++) or _functors_ (ML), e.g.,

```c++

template <typename Base, typename Element> class WithList: Base  {
   public:
       std::linked_list<Element> stuff;
}

class Graph: WithList<GraphBase, Node> {}
```

The downside is that this can make classes harder to reason about, since they are now akin to compile-time variables than constants.  Relations enable all the parents of an object get to be automatically notified when a child is destroyed, eliminating the possibility of dangling pointers, which is discussed below.  They also result in more efficient implementations.  For example, there is no need to allocate a new object to hold the next pointer: it is automatically added to the child class.

4. Metaprogramming - classes can be [_transformed_ post-creation](#memory-safety).  This is typically used to add new capabilities, or to implement class relationships.

5 DSL - Rune's syntax can be easily extended, enabling users to create easier to
read and understand code for specific problem domains, such as web page creation
from templates.

6 security focus

7 multiple-width integers, all of which are incompatible,  explicit casts

8 SoA vs AoS

## Memory safety

Rune achieves memory safety through a combination of techniques that make it
possible to feel like using Python without the overhead of garbage collection.
Several design choices were made to enable this.

1) Data structure layout is abstract.

The programmer's code cannot directly determine how the compiler represents data
in memory.  The compiler has great freedom in how that data is laid out in memory,
and can support either SoA or AoS or some combination optimized for cache
efficiency.

2) All classes are either reference counted, or are children of a _cascade-delete_ relation

Cascade-delete children are automatically deleted whenever their parents are.

<em>Cascade-delete classes</em> are classes which are children in any
cascade-delete relation. <em>Ref-counted classes</em> can always be data members in
another class.  Examples of ref-counted classes typically include Matrix, and
other classes that are treated like values. Most objects should be
cascade-delete for best performance. A common programming pattern in Rune
is to define a singleton Root class which is ref-counted, and have
cascade-delete paths from Root to all other class instances, with a few
ref-counted classes that appear only as data members on cascade-delete classes.
Destroying the root object will recursively destroy all the rest.

3) Safety first.

By default, Rune raises exceptions on integer overflow/underflow, bounds
checking of arrays, and null-indirection.  Data declared `secret` are compiled
to minimize timing side-channels that could be exploited in attacks
such as cache-timing attacks. Since Rune is also a systems programming language,
all of these safety features can be disabled if required for speed, but the
default is full safety.

4) Large [compilation units](#compilation-units), and whole-program (whole-library) analysis

Rune can detect use-after-free violations with static lifetime analysis, and
eliminate most ref counting for ref-counted classes.  It will generate an error
for any case that could potentially cause a memory safety violation, either at
compile-time or runtime.

### Compilation units

Unlike C, Rune is not compiled one file at a time. The Python-like level of Rune
compiles an entire program or stand-alone shared library. This is similar to
most Python code, where imports are at the top of each file. Rune compiles the
top level Rune file, along with all of its transitive imported dependencies, at
once.

The C linker is involved only when linking a binary or creating a shared
library, where all functions must be compatible with C or C++ calling
conventions. Types can be inferred everywhere except in top-level APIs in shared
libraries, where they must be explicit.

### Relations

In Rune, relations between classes are declared with `relation`
statements.  Relations are implemented with [transformers](#transformers), which
enable both the parent and child class to take part in supporting their mutual
relationship.  All parent objects are automatically updated when a child object
is destroyed, which is a significant part of Rune's memory safety.

Compared to C++ collections, Rune relations are safer, as destructors are
auto-generated and short of compiler bugs, never leave dangling pointers.

Compared to Rust's smart-pointers and collections, Rune's relations simplify
modeling of complex systems, without requiring any ref-counted objects.

Relations in Rune incorporate one of the most powerful and important
capabilities found in most SQL engines: the ability to cascade-delete objects
automatically.  This is the main reason destructors are never written in Rune,
though classes can have `final` methods to do some tidying up before being
destroyed.  Memory safety is ensured regardless.

While anyone can create new relation types, the relation types supported by default are:

* OneToOne
    * The parent and child have references to each other.  This is the simplest
      relation.  Use the `insert()` and `remove()` methods on the parent to
      add/remove a child from the relation.
* ArrayList
    * The parent has an array of child objects, and the array allocation size,
      and the used size.  Adding and removing children run  in constant time,
      but may reorder the child objects within the array.
    * The child has a parent reference, and knows its index in the parent array,
      so that it can remove itself before being destroyed.
* DoublyLinked
    * The parent has a first and last child reference.  Insertion and deletion
      are constant time, and do not impact ordering of the children.  Due to SoA
      layout, next pointers often have excellent data locality for improved cache
      efficiency.
    * The child has prev, next, and parent references.
* Hashed
    * The parent has a hash table of child objects, where the size of the table
      is always at least as large as the number of children.  Insertion and
      deletion has constant average time.  Child ordering is not preserved when
      inserting objects.
    * The child has a next pointer to the next child object that collides in the
      same hash bucket.  It also has a key field used to find the object in the
      hash table.  Keys must be unique.
* HashedClass
    * Like Hashed, but the child class defines `operator==` and `hash` methods
* HeapqList
    * The parent has a binary heap of child objects.  Insertion is constant
      average time, while removal has O(log(n)) average time, where n is the number
      of elements in the heap.
    * Child objects link to their parent and know their index in the binary heap
      so they can remove themselves when destroyed.
* LinkedList
    * Similar to DoublyLinked, but lacking a prev reference in the child class.
      Insert is constant time, bug append and removal are O(n), where n is the
      number of elements in the list.
* TailLinked
   * Like LinkedList, but adds a last reference to the parent so that append
     operations are constant time.

#### Cascade-delete relations

In Rune, a _cascade-delete_ class is a class that is the child in any
cascade-delete relation.  Otherwise, the class is reference-counted.

The Rune compiler  checks that objects are added to cascade-delete relations
before going out of scope, and will analyze all callers to verify this if a
cascade-delete object is removed rather than going out of scope.

This is an important tool for the efficiently managing object lifetimes. For
most memory intensive applications, most objects should be in cascade-delete
relationships.

#### Mandatory relations

Relations can be declared `mandatory`, which means cascade-delete, but the
parent will never be null on the child once inserted into the relation, so no
check for null is required. The compiler checks that the child has been added to
the relation before the parent is accessed from the child.

Note: Not supported in the C Rune compiler.

#### Principal classes

Principal classes in Rune are the primary unit of parallelism. A principal
object can only exist on a single thread, but each thread can have multiple
principal objects.

These are called principal classes because of a common pattern for the life
of Rune a program. It is common to start as a single-threaded program with a
singleton `Root` class that when deleted will cascade-delete all objects in
memory. For example, we might write a word processing program. Initially, the
program works on a single document, so there is no `principal` class, and instead
there is a singleton `Root` class. Eventually, as the system matures, we want to
edit multiple documents at the same time, and may want to process documents in
parallel on different threads. The `Root` class is then renamed to something
like `Document`, and a new `Root` class is created to own all the documents.

In the Rune compiler, the current `Root` class is the principal class, and
the Rune compiler is single-threaded. In the future, to take advantage of
multiple CPU cores in parallel, the Rune compiler may split a program into
multiple chunks to be processed in parallel.

TODO: Flesh out muti-threading and fiber support.

#### Memory layout for principal objects

Rune uses SoA memory layout, which means somewhere in memory there is a struct
containing all the arrays that represent a principal object.  Data members
on a principal object are part of the class' struct as well. Cascade-delete
children, transitively, are owned by the same principal object.  An object
reference is typically a 32-bit index into arrays owned by the principal object. A
cross-principal reference is a tuple of the principal object ref and the object ref
within the principal object. A principal object ref is likely a pointer to the SoA struct, but
as usual, memory layout is an optimization detail left to the compiler.

Class extensions are local to the principal object.  A dynamic extension to
arbitrary class X (e.g., a new field is 'added' to an object of type X) created
within definitions local to principal class A will _not_ be added to objects of
type X within definitions local to principal class B.  Similarly, relations
defined with X within the scope of principal class A (e.g., adding a linked list
of C objects) will _not_ be defined for instances of X within the scope of
principal class B.

This will save memory, since there will be fewer fields for objects of type X in principal class B's SoA entries.  But it also means that only the elements defined during class X's actual class definition can be used to communicate.

#### Persistent relations

Relations can also be declared `persistent`, which implies `mandatory`, and also
that the child cannot be destroyed once added to the relation, other than by
destroying the entire principal object. Persistent relations must chain to a
principal class. E.g. if `Root` owns class `Foo` persistently, then `Foo`
may own class `Bar` persistently.  These are similar to 'static' variables in
C++, for example.

This allows non-ref-counted classes that persist for the life of their owning
principal objects to be included as data members on any class owned by the
same principal class (meaning cascade-deleted via the same principal class). An
example of this in the Rune HIR is `Datatype`. Once a `Datatype` object is
inserted into the hashed relation on `Root`, calling its destructor results in a
compile-time error. This
enables
`Datatype` to be used as data members safely throughout the HIR data model
without being ref-counted.

Another example in the HIR is `Location`. By having all `Location` objects
persistently owned by `Root`, we are free to add `Location` objects to multiple
HIR classes as data members, without the possibility of dangling references.
When owning objects, such as `Expr`, are copied, `Location` objects can be used
on both the original and copy as data members, without making a copy of the
`Location` object.

Principal classes can be either cascade-delete or ref-counted, just like
any other class. By convention, the `Root` principal class is ref-counted,
but the Rune compiler can easily see its lifetime spans the entire program, so
no ref-counting is actually performed.

TODO: Define syntax for declaring principal classes, and cross-principal
relations.

#### Traditional binary tree example

A traditional binary tree can be written in Rune without ref counting and
without back-references from child `Node` objects to their parents. This can be
accomplished with three `persistent` relations. The first is a one-to-one
relation from a `Tree` class to `Node` to point to the root of the tree. The
other two are `Left` and `Right` persistent one-to-one relations from `Node` to
`Node`. This results in an efficient non-ref-counted `Node` class, with just
left and right references, but they cannot be destroyed other than by destroying
their owning `Tree`.

TODO: Add a binary tree example when we have the syntax defined for
principal classes and persistent relations.

#### Graphs, not just trees

While trees of cascade-delete classes are used almost exclusively in some
languages and databases, Rune gives you the freedom to build complex graphs of
of cascade-delete classes without fear of dangling pointers. Such data
structures are needed to represent family trees, and the Rune HIR. Classes with
multiple cascade-delete parent relations is common in Rune.

#### Exclusive classes

Some classes are instantiated in multiple cascade-delete relations, but are only
owned by one at a time. In the HIR, an example is `Expr`. An `Expr` object is
owned by one of:

*   `Expr`
*   `Statement`
*   `TypeVariable` for `variable` type constraints
*   `TypeFunction` for function return type constraints

If a class is marked `exclusive`, it may only have one parent, even though it is
a child in multiple relations.  This tells the compiler that it can use a
different internal representation for the class.   If one were to write

```rune
relation LinkedList Statement Expr cascade
relation LinkedList TypeVariable Expr cascade
```

then `Expr` objects would end up with two parent pointers, and  also linked-list pointers (next, prev).  Since we know that an `Expr` object will only have one parent pointer, marking it `exclusive` means the compiler will only generate one parent, and one set of linked-list pointers, adding also a tagged union field to indicate what parent class actually owns the object.  (Note that if the compiler detects that this type information is not required, the tagged union field would be removed also.)

#### Reducing memory leaks

No language is entirely safe from memory leaks, not even Python. However, Rune
helps avoid them through auto-generated destructors.

Another way the Rune compiler helps avoid memory leaks is by detecting when a
cascade-delete object goes out of scope without being added to a cascade-delete
relation. It is illegal in Rune for a cascade-delete object to be constructed
and not added to a cascade-delete relation. If a constructor fails to do so, the
caller must add the object to a cascade-delete relation before the object goes
out of scope.

```rune
class Graph(self) {
}

// Danger: the Node does not add itself to Graph in the constructor.
// The compiler will verify each call site.
class Node(self, name: string) {
    self.name = name
}

relation DoublyLinked Graph Node cascade

func buildGraph() {
  graph = Graph()
  n1 = Node()  // Compile-time error, due to memory leak of n1.
  return graph
}

graph = buildGraph()
```

The usual fix is to have constructors add themselves to cascade-delete
relations:

```rune
class Graph(self) {
}

class Node(self, graph: Graph, name: string) {
    self.name = name
    graph.appendNode(self)
}

relation DoublyLinked Graph Node cascade

func buildGraph() {
  graph = Graph()
  n1 = Node(graph)
  return graph
}

graph = buildGraph()
```

In debug mode, an exception is raised if a cascade-delete object is removed from
its last cascade-delete relation, by calling remove methods on its owning parent
classes, and then goes out of scope.  Since memory leaks are not a memory safety
issue, the compiler does not do this check in optimized mode, except for classes
containing secret data, to help make sure secrets are erased when no longer
used.

Note: The C Rune compiler does not perform these checks.

### Rules for null safety

Only class instances can be null in Rune.

Types returned by constructors are not null, adding some null safety without any
type constraints. Nullable types are created with null(\<*Class name*\>). All
nullable class instances are checked before dereferencing, unless the compiler
can determine a value cannot be null, for example in code like:

~~~rune
if !notnull(node) {
    processNode(node!)
}

For improved safety and efficiency, nullable types should be converted to
non-nullable with `<`_expression_`>!`, which checks for non-null at runtime, and
returns a non-nullable type.

### Destroying objects explicitly.

All destroy methods in Rune are auto-generated.  You may declare a `final`
method on a class to do special processing when an object is destroyed, but all
releations to other classes are automatically cleaned up, and cascade-delete
children are destroyed.

Rune ensures there are no outstanding references to a cascade-delete object
being destroyed.  It is a compile-time error to hold a local or global reference
of the same type as a class instance being destroyed, with a lifetime spanning a
call to the destructor for that class.  Use `safe` iterators when you may
destroy the object held by the loop variable.

TODO: Decide if this or some alternate approach should be taken.  It remains
unclear if this is the best approach: It puts a burden on the user, and the
compiler could track a set of objects that need to be checked when destroying an
object of the same class.

### Abstractly passing parameters by value or reference

In Rune, all parameters passed to functions are immutable by default, unless
they are declared as `var` meaning mutable, in which case they are passed by
reference. It is not possible to determine in Rune if by default we pass arrays
by value or reference, though for efficiency, we currently pass arrays, tuples,
and structs by reference, small integers by value, and big integers by
reference. This is simply a compiler optimization detail which does not impact
behavior of code.  The compiler currently considers integers larger than 64 bits
to be 'large', but this may change in the future.

Under the hood, references to arrays are slices.  A slice contains all the
information needed to access the portion of the array desired.  For example:

```rune
l = [1, 2, 3, 4]
sub = l[1 : 3]  // sub == [2, 3]
println sub
~~~

In this case, sub is assigned a slice rather than making a copy. This slice is
passed to println. Similarly, assigning an array to a variable from another
variable containing an array creates a slice of the whole array. If the array is
modified in any way during the lifetime of the variable assigned the array, then
a copy is made instead. From the user's point of view, assignment has copy
semantics, but when a reference will suffice a slice is used as an optimization.

The following code forces the Rune compiler to make a copy of the array `l`,
because `l` can be modified after being passed to `foo`:

```rune
func foo(array) {
    l = [3, 4, 5]  // Overwrites the global l.
    println array
}

l = [1, 2, 3]
foo(l)  // Prints [1, 2, 3], because the compiler makes a copy of l.
l2 = [6, 7, 8]  // Makes a copy because foo modifies l during l2's lifeime.
foo(l2)  // Prints [5, 6, 7], without making a copy of l2.
```

This scheme hides from the user when copies are made rather than slice
references. Copies are only made when it is possible that not making a copy
could change the behavior of the program:

```rune
l = [1, 2, 3, 4]
copy = l  // Makes a slice of l, rather than a copy.
l.append(5)
println copy  // Prints [1, 2, 3, 4].
ref = l  // Does not make a copy.
println ref  // Prints [1, 2, 3, 4, 5].
```

Swapping arrays causes arrays to be moved, not copied:

```rune
a = [1, 2, 3]
b = [3, 4, 5]

temp = a  // Moves array in a to temp, since a's lifettime is at an end.
a = b  // Same for the array in b.
b = temp  // Same for temp.

// This is equivalent to:
(a, b) = (b, a)
```

Note: The Rune C compiler always passes arrays by reference, and always copies
on assign. This can create subtle bugs that crash the generated code, since
lifetime analysis is not done.

### Creating new classes with copy-on-assign semantics

Simply overload the `=` operator. If you overload a binary operator, but not its
corresponding assignment operator (e.g. overload `=`, `+`, but not `+=`), then Rune
will automatically use your two defined operators to implement the combined one.

Note: Not supported in the Rune C compiler.

## Importing code

Rune has 2 concepts for using code in other Rune files: Packages and Modules:

*   A Module is a Rune file.
*   A Package is a directory containing multiple modules, and a `package.rn`
    file.

**Modules** in the same package can import each other with the `use` keyword.
`use foo` imports all non-variable identifiers, both exported and private, from
the file foo.rn in the same directory; using modules in other directories is not
supported. The variables from foo.rn are imported directly into the local file’s
scope, without creating a `foo` variable. The thinking here is that since
modules in the same package are typically maintained by the same people, they
can easily fix name conflicts that occur.

**Packages** are collections of modules defined by `package.rn` in the
directory. Packages are imported using the `import` keyword. `import bar`
creates a single identifier called `bar` that exposes every exported identifier
in every module inside the `bar` directory, but unlike modules, no identifiers
other than `bar` itself are imported into the local scope, and must instead
always be referenced as attributes of `bar`. For example, if the `bar` package
exports the `baz` function, `bar.baz()` would be the correct calling syntax.
Every package has a special `package.rn` module which contains initialization
code and imports other modules in the package. The identifiers exported by a
package are exactly those identifiers exported directly from `package.rn` or
exported by the modules it imports.

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

Rune provides the several variations on the syntax to declare functions.

```rune
// No types
func Add(a, b) {
  return a + b
}

// With explicit types
func Add(a: u8, b: u8) -> u8 {
  return a + b
}
```

Rune functions can also require more advanced types, such as arrays:

```rune
// With an array type
func Sum(a: [i32]) -> u64 {
  s = 0
  for i = 0, i < a.length(), i += 1 {
    s += <s>a[i]
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

Underscores and dollar signs are only allowed in *transformers*, where they are
used to generate code that cannot conflict with regular identifiers.

### Strings

A string is any sequence of ASCII or UTF8 characters between two `"` characters:

STRING ::= \\\"([^"] | \\.)*\\\"

Escape sequences supported in Rune are:

```rune
'\a' == '\x07'  // Bell.
'\' ==  '\x08'  // Backspace.
'\e' == '\x1b'  // Escape.
'\f' == '\x0c'  // Formfeed.
'\n' == '\x0a'  // Newline.
'\r' == '\x0d'  // Return.
'\t' == '\x09'  // Tab.
'\v' == '\x0b'  // Vertical tab.
```

Any other character following a \ will just be that that character, including
\\, \' and \".

### Integers

```
INTEGER ::= "'"[ -~]"'"
    | '\\a'" | "'\\b'" | '\\e'" | "'\\f'" | "'\\n'" | "'\\r'" | "'\\t'" | "'\\v'"
    | [0-9][0-9_]*(("u"|"i")[1-9][0-9_]*)?
    | "0x"[0-9a-fA-F][0-9a-fA-F_]*(("u"|"i")[1-9][0-9_]*)?
```

Note: The C Rune compiler does not allow underscores in numbers.

```
INTTYPE ::= "i"[1-9][0-9_]*
UINTTYPE ::= "u"[1-9][0-9_]*
```

Character constants, such as '\n' evaluate to u8 integers.

### Floating point

```
FLOAT ::= [0-9][0-9_]*("."([0-9][0-9_]*)?)?("e"("-")?[0-9][0-9]*)?("f32"|"f64)
    | [0-9][0-9_]*"."([0-9][0-9_]*)?("e"("-")?[0-9][0-9]*)?
    | [0-9][0-9_]*("."([0-9][0-9_]*)?)?"e"("-")?[0-9][0-9]*
```

Note: The C Rune compiler uses libc strtod format, and does not support
underscores.

### Random values

```
RANDUINT ::= "rand"[1-9][0-9_]*
```

This syntax generates random numbers suitable for cryptography. On Linux the
random numbers are provided by `getrandom`. For example:

```rune
key = rand256  // Creates a secret u256 integer.
println key  // This is a compiler error, since you cannot print secrets.
```

TODO: Add APIs for injecting entropy, and maybe APIs for health monitoring.

## Keywords

```
appendcode  else       if         panic        secret       unref
arrayof     enum       import     prependcode  signed       unsigned
as          except     importlib  print        string       use
assert      export     importrpc  println      struct       var
bool        exportlib  in         raise        switch       while
cascade     extern     isnull     raises       transform    widthof
case        f32        iterator   ref          transformer  yield
class       f64        message    relation     try
debug       final      mod        return       typeof
default     for        null       reveal       typeswitch
do          func       operator   rpc          unittest

```

## Datatypes

### Bool

You get the usual `true`, `false`, and `bool`:

```rune
done = false
while !done {
    ...
    if conditionTestsTrue() {
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

When compiling in safe mode (the default), operations on integers are not
allowed to overflow/underflow without raising an exception, unless operators
starting with `!` are used:

```rune
255u8 !+ 1u8  // Legal, and equal to 0u8
<u8>256u32  // Raises exception at compile or runtime
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
import math

x = 0.1; y = 0.8
dist = math.sqrt(x**2 + y**2)
Println “distance = %f” % dis
```

### String

Strings are encoded in UTF-8, and are internally represented as dynamic arrays
of `u8`.

List string methods here...

### Dynamic arrays

### Tuples

### Function pointers

## Run-time polymorphism

C++ supports two kinds of polymorphism:

*   Run-time polymorphism: class inheritance
*   Compile-time polymorphism: templates

Rune supports compile-time polymorphism. For example, the `max` function is
defined as:

```rune
func max(a, b) {
  return a >= b? a : b
}
```

Leaving them out increases polymorphism, but may result in more fragile and
harder to understand code. If you are just writing a simple Rune script, feel
free to leave them out entirely. However, when building a complex program, type
constraints are essential: If you know the types, then add constraints.

The style of efficiently supported run-time polymorphism that can be
offered by a language depends on its memory layout. With C++ AoS layout:

1.  Class inheritance is efficient.
2.  Dynamic extension of objects (like we do in Python) is inefficient.
3.  C++ is forced to continue using AoS, making it slower.

With Rune’s SoA memory layout:

1.  Dynamic extension of objects is efficient.
2.  Class inheritance is inefficient.

Note: The C Rune compiler does not support either inheritance or dynamic
extension.

### Dynamic class extensions

C++ inheritance does not enable the coder to dynamically add members and methods
to existing objects, like we can do in Python. Rune chooses to emulate class
inheritance through composition, while having native support for
run-time dynamic class extensions.

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
appendcode  debug    for        panic        relation   unref
assert      default  foreach    prependcode  return     use
assign      do       if         print        switch     while
assignment  else     import     println      transform  yield
call        elseif   importlib  raise        try
case        except   importrpc  ref          typeswitch
```

In the Rune HIR, a statement can have at most one expression. For example, a
chain of if-else statements has a separate statement for each if-else clause. A
`for` loop will have a tuple expression, with the assignment, condition, and
loop re-initializer as sub-expressions.

## Expressions

### Operators

```
!     !*    !*=   !+    !+=   !-    !-=   !<>
!=    %     %=    &     &&    &&=   &=    *
*=    +     +=    -     -=    .     ...   /
/=    <     <<    <<<   <<<=  <<=   <=    =
==    >     >=    >>    >>=   >>>   >>>=  ?
?:    ^     ^=    ^^    ^^=   []    **    **=
|     |=    ||    ||=   ~     in    mod   <>
```

The following subsections describe the built-in operators. Note that these can
be overridden for classes.

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
:   `e1 * e2` evaluates to the product of the values of `e1` and `e2`. This will
    raise an exception upon overflow.
:   `e1` and `e2` must be any numeric type, integral or floating point.
:   `e1` and `e2` must have exactly the same type, so for example `32u8 * 32u16`
    and `32i8 * 32u8` are badly-formed expressions.

Add (`+`)
:   `e1 + e2` evaluates to the sum of the values of `e1` and `e2`. This will
    raise an exception upon overflow.
:   `e1` and `e2` must be any numeric type, integral or floating point.
:   `e1` and `e2` must have exactly the same type, so for example `32u8 + 32u16`
    and `32i8 + 32u8` are badly-formed expressions.

Subtract (`-`)
:   `e1 - e2` evaluates to the result of subtracting the value of `e2` from the
    value of `e1`. This will raise an exception upon overflow.
:   `e1` and `e2` must be any numeric type, integral or floating point.
:   `e1` and `e2` must have exactly the same type, so for example `32u8 - 32u16`
    and `32i8 - 32u8` are badly-formed expressions.

Divide (`/`)
:   `e1 / e2` evaluates to the result of dividing the value of `e1` by `e2`. If
    `e1` and `e2` are integer types, the result will be the integer part of the
    division. :`e1` and `e2` must be any numeric type, integral or floating
    point.
:   if `e2` is `0` then an exception will be raised.
:   `e1` and `e2` must have exactly the same type, so for example `32u8 / 32u16`
    and `32i8 / 32u8` are badly-formed expressions.

String Format Application/Integer Modulus (`%`)
:   `s % e` returns the string `s` after substituting each occurrence of a
    formatting annotation within `s` with the corresponding parameter in `e`.
    For example,
:   `"Hello World: %d" % 1` returns `"Hello World: 1"`, and
:   `"Hello %s: %d" % ("Planet", 10)` returns `"Hello Planet: 10"`.

:   `e1 % e2` equals `e1 mod e2` when `e1` and `e2` are integers of any valid
    bitwidth.

:   `e1` and `e2` must have exactly the same type, so for example `32u8 % 32u16`
    and `32i8 % 32u8` are badly-formed expressions.

Mathematical Modulus (`mod`)
:   `e1 mod e2` evaluates to the remainder after subtracting every whole
    multiple of `e2` from `e1`.

:   This is defined for any numeric value of `e`, integral or floating point
    (though `e2` should be an integer).

:   For example, `10 mod 7` equals `3`, and

:   `1/3 mod 7` equals `5` (in this case the
    [modular inverse](https://en.wikipedia.org/wiki/Modular_multiplicative_inverse)
    of 3 mod 7 is 5)

:   `e1` and `e2` must have exactly the same type, so for example `32u8 mod
    3u16` and `32i8 mod 5u8` are badly-formed expressions.

Exponentiation (`**`)
:   `e1 ** e2` evaluates to the result of raising the value of `e1` to the power
    of the value of `e2`.

:   `e1` and `e2` are defined only for numeric types.

:   Unlike other operators, `e1` and `e2` may have different types. The result
    type is the type of `e1`, the base.

:   This will raise an exception if the result would overflow the return type.

:   Exponentiation in modulo arithmetic will not overflow.

Truncated Add (`!+`)
:   `e1 !+ e2` is the truncated sum of `e1` and `e2`. If the sum would overflow
    `typeof(e1)`, then only the least significant bits are returned.

:   This is only valid for integral types (signed or unsigned) for any valid bit
    length.

:   `e1` and `e2` must have exactly the same type.

Truncated Subtract (`!-`)
:   `e1 !- e2` is the value of `e1` less the value of `e2`. If this would
    overflow `typeof(e1)`, then only the least significant bits are returned.
    For example `-128i8 !- 1i8` would return `127i8`.

:   This is only valid for integral types (signed or unsigned) for any valid bit
    length.

:   `e1` and `e2` must have exactly the same type.

Truncated Multiply (`!*`)
:   `e1 !* e2` is the product of `e1` and `e2`, ignoring overflow bits. For
    example, `16u8 * 16u8` would raise an overflow error, but `16u8 !* 16u8`
    would return `(16*16) & 255` or just `0`.

:   This is only valid for integral types (signed or unsigned) for any valid bit
    length.

:   `e1` and `e2` must have exactly the same type.

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
:   `e!` wraps expression `e` with a null safety check. If `e` turns out to be
    null, rune will raise an exception.
:   `e` can be any expression, even plain old data types.

Cast (`<.>`)
:   `<t>e` converts `e` into an expression of type `t`, raising an exception if
    any bits in `e` are lost. For example, regular cast `<u8>256` would raise an
    exception.

Truncated Cast (`!<.>`)
:   `!<t>e` converts `e` into an expression of type `t`, throwing away any bits
    in `e` that would overflow the size of type `t`. For example, regular cast
    `<u8>256` would raise an exception, but `!<u8>256` will return `0`.
:   This is only valid for integral types (signed or unsigned) for any valid bit
    length.

Address Of (`&`)
:   `&f` returns a first-class reference to a function `f`. (It does not work
    for arbitrary variables or objects).
:   Note that `f` must be specified with concrete parameter types.

Field Access (`.`)
:   `a.f` returns the value of field `f` within aggregate-typed expression `a`
    (either a struct, object, enum object) or an expression that evaluates to
    one.

Membership (`in`)
:   `e in ae` returns `true` when `e` can be found within `ae`.
:   Works for dict classes, arrays, classes when they overload.
:   TODO: add support for substring search (as in python)

:   TODO: get working for arrays, strings, tuples

Assign (`=`)
:   `x = e` assigns the value of `e` to variable `x`.

:   Note that this will create variable `x` if it does not already do so.
    Subsequent assignments to `x` must be of the same datatype as `e`.

:   When the left-hand side has form `<`*object*`>.x`, this assignment will add
    a new data member `x` to that object.

:   TODO: decide whether or not to restrict this behavior to the class
    constructor.

Index (`[]`)
:   `e1[e2]` evaluates to the content of element number `e2` in array or tuple
    `e1`.

:   When `e2` is negative, or greater than the maximum index of `e1`, Rune will
    raise an exception.

:   When `e1` is a tuple, `e2` must be a constant integer.

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

Note that type expressions have fewer operators and different precedence.

```
expr ... expr
expr ? expr : expr
expr || expr
expr ^^ expr
expr && expr
expr in expr
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

### Type expressions

Operators are:

```
|
...
[] ()
. ?
```

TODO: Fill this out like the expression section above. Also, move descriptions
for type expressions out of the above section to here.

The `T1 | T2` operator creates a union type of `T1` and `T2`. This is used to
match multiple types in type constraint expressions.

The `T1 ... T2` operator is for matching integers of a given size, e.g. `u1 ...
u7`.

Array types are declared as `[T]`, where T is a type such as `u8`.

Type types are declared as `(T1, T2, ...)`, e.g. `(u32, string)`.

The dot operator is only used in path expressions to specify a type declared in
a module, e.g. `database.Statement`.

The `?` suffix operator indicates that the prior type an be null. It only
applies to class types.

### Assignment operators

```
!+=  !=  &&=  +=  <<<=  <=  ==  >>=   ^=   **=  ||=
!-=  %=  &=   -=  <<=   =   >=  >>>=  ^^=  |=
```

### Modular expressions

The `mod` operator is used to mimic mod expressions from mathematics. For
example, we can write:

```rune
-1 == 5**2 mod 13
```

This means that modulo a number congruent to 1 mod 4, the imaginary number `i`
exists. In this case, i == 5 mod 13.

Modular arithmetic is useful in cryptographic code. For example, for [elliptic
cure 25519](https://en.wikipedia.org/wiki/Curve25519), represented as an
[Edwards curve](https://en.wikipedia.org/wiki/Edwards_curve), we can compute:

```rune
m = <u255>(2u256**255 - 19)  // The modulus for curve25519.
d = -121665/121666 mod m
// The generator's Y coordinate.
gy = 4 / 5 mod m
```

Note that when paired with `mod`, like here, the divide operator (`/`) computes
the modular inverse. An exception will be raised if the modular inverse does not
exist.

```rune
1/2 == 7 mod 13
1/2 mod 12  // Raises an exception because 2 has no inverse mod 12.
```

Supported modular operators are:

```rune
== !-
+ -
* /
**
```

## Constant-time processing of secrets
A primary goal of Rune is security, and this extends to the safety of secret keys.
Rune attempts to make it easier to write cryptographic code, such as encryption
and decryption, without violating rules for avoiding side-channel leakage.

These two rules must be followed to minimize leakage of secret information via
[timing attacks](https://en.wikipedia.org/wiki/Timing_attack), such as
cache-timing attacks or Spectre/Meltdown.

1) Never branch on secret data.
2) Never index by secret data.

These rules were not known in the 1990s, and popular cryptographic algorithms
such as [AES](https://en.wikipedia.org/wiki/Advanced_Encryption_Standard) (e.g.
AES128-GCM) and [SHA2](https://en.wikipedia.org/wiki/SHA-2) (e.g. SHA256)
violate them, which is one reason we implement these primitives with hardware
acceleration: less timing information is leaked by the hardware versions that
software versions.

In an unfortunate timing violation, AES indexes into "S-tables", which are
256-byte tables indexed by a single byte of the secret key.  A cache timing
attack can detect whether the index was in a 64-bit range when indexing into the
table.  In a fraction of a second, an attacker running a prime-and-probe attack
can extract the AES secret key.

A recent exploit against popular cryptographic libraries found that because
these libraries were checking that a secret result was less than the modulus
before doing a modular reduction, they were able to extract entire RSA keys
through timing attacks.

Consider this code in C++:

```c++
bool macIsCorrect(std::string message, std::string suppliedMac, std::string secretKey) {
    return computeMac(message, secretKey) == suppliedMac;
}
```

Do you see how the attacker can use a timing attack to forge a valid MAC?

A common flaw in verifying MACs (Message Authentication Codes) is using
non-constant time comparison of the correct MAC to the attacker-supplied MAC.
If the first byte provided by the attacker is wrong, the compare loop
terminates, but if it is right, the victim code will iterate through the loop
and check the next byte.  By trying 256 different malicious MACs, the attacker
can guess the correct first byte, and by repeating this attack for each byte in
the MAC, the attacker may forge a correct MAC without ever knowing the secret
key.

In Rune, the same code is secure from this attack, so long as the user marks the
secret key as secret.  The result of `computeMac` will automatically also be
secret, because Rune propagates the secret bit to the result of all operations
over secrets.

For builtin operations, such as string compare and modular operations,
constant-time implementations are chosen by the compiler when operating over
secrets, so the above code avoids timing leakage when translated to Rune.
If the user explicitly branches or indexes on a secret value, a compile-time
error is generated.

"Constant time operations" in cryptography still expose some information to the
attacker.  For example, a password's length is not protected: hashing long
passwords takes longer than hashing short ones.  By "constant time", we mean
that processing any data inputs of the same length will take the same time.

This also applies to the ?: operator, where both sides are evaluated if the
Boolean selector is secret, and the generated code takes care not to branch on
the selector result, and instead selects the result using bitwise operators.

Note that while Rune aims to help uses minimize exposure to timing attacks,
other attacks are not mitigated.  For example, Rune does not attempt to thwart
glitching attacks, though this may be an interesting area to explore in the
future.  Rune also does not attempt to minimize power or EMI side-channels.
Many recent CPU vulnerabilities were caused by errors in hardware design, e.g.
ZombieLoad.

## Builtin functions and classes

```
Dict
Heapq
Sym
max
min
abs
chr
ord
secret
reveal
issecret
null
isnull
typeof
widthof
arrayof
signed
unsigned
issigned
```

## Builtin relations

```
ArrayList
DoublyLinked
Hashed
HashedClass
HeapqList
LinkedList
OneToOne
TailLinked
```

## Builtin iterators

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

Newlines are used as statement terminators, and to join multiple statements on
one line, terminate all but the last with semicolon. Newlines are also required
after an open curly brace. which must be on the same line as the preceding
syntax, such as an if statement or class definition. Newlines may optionally be
inserted anywhere within an expression surrounded with parenthesis, and they
generally can follow commas or the operator in a binary operation.

<pre>
<em>runeFile</em> ::= <em>statement</em>*

<em>statement</em> ::=
      <b>appendcode</b> <em><em>pathexp</em></em>? <em>block</em>
    | <b>assert</b> <em><em>explist</em></em>
    | <em>assignment</em>
    | <em>call</em>
    | (<b>rpc</b> | <b>export</b> | <b>exportlib</b>)? <b>class</b> <em>ID</em> UINTTYPE? <b>(</b> <em>params</em> <b>)</b> <em>block</em>
    | <b>debug</b> <em>block</em>
    | (<b>do</b> <em>block</em>)? <b>while</b> <em>exp</em> <em>block</em>?
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
    | <b>panic</b> <em>explist</em>
    | <b>prependcode</b> <em>pathexp</em>? <em>block</em>
    | <b>println</b> <em>explist</em>
    | <b>print</b> <em>explist</em>
    | <b>raise</b> <em>explist</em>
    | <b>ref</b> <em>exp</em>
    | <b>relation</b> <em>pathexp</em> <em>pathexp</em> <em>label</em>? <em>pathexp</em> <em>label</em>? <b>cascade</b>? (<b>(</b> <em>explist</em> <b>)</b>)?
    | <b>return</b> <em>exp</em>?
    | <b>rpc</b> <em>ID</em> <em>funcdec</em>
    | <b>switch</b> <em>exp</em> <b>{</b> (<em>exp</em> (<b>,</b> <em>exp</em>)* <em>block</em>)* (<b>default</b> <em>block</em>)? <b>}</b>
    | <b>transform</b> <em>pathexp</em> <b>(</b> <em>explist</em> <b>)</b>
    | <b>transformer</b> <em>ID</em> <b>(</b> <em>params</em> <b>)</b> <em>block</em>
    | <b>try</b> <em>block</em> <b>except</b> <em>ID</em> <b>{</b> (<em>pathexp</em> <b>=></b> (<em>block</em> | <em>statement</em>))* (<b>default</b> <em>block</em>)? <b>}</b>
    | <b>unittest</b> <em>ID</em>? <em>block</em>
    | <b>unref</b> <em>exp</em>
    | <b>use</b> <em>ID</em>
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
    | <b>(</b> <em>exp</em> <b>,</b> <b>)</b>
    | <b>typeof</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>unsigned</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>signed</b> <b>(</b> <em>exp</em> <b>)</b>
    | <b>secret</b> <b>(</b> <em>typrim</em> <b>)</b>
    | <em>tyliteral</em>

<em>typrimlist</em> ::= <em>typrim</em> (<b>,</b> <em>typrim</em>)*

<em>tyliteral</em> ::=
    UINTTYPE | INTTYPE | <b>string</b> | <b>bool</b> | <b>f32</b> | <b>f64</b>

</pre>
