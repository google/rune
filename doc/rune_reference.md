# Rune Language Reference

**Authors**: waywardgeek@, aidenhall@, ethangertler@

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
the compiler can prove this it is not needed. To aid in this, RUne compiles an
entire program or shared library at once, rather tha one file at a time. If
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

### Rules for memory safety

Memory corruption is impossible in Rune, assuming relationship generators are
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
causing a memory leak, is when we have relationship loops of objects, all of
which are in cascade-delete relationships but are not in any relationship path
to a top level object (described below). For example:

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
*   Pass objects that may be destroyed to functions as `var` parameters, so the
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

```
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
they are declared as "var" meaning mutable, in which case they are passed by
reference. It is not possible to determine in Rune if by default we pass arrays
by value or reference, though for efficiency, we currently pass arrays, tuples,
and structs by reference, small integers by value, and big integers by
reference. This is simply a compiler optimization detail which does not impact
behavior of code, only its speed.

### Assignment implies copy for builtin classes

Since we do not know if a value is passed by reference or value, what happens
here:

func doubleArray(a: Array) { b = a b.concat(a) return b }

The |a| parameter is immutable, but |b| can be modified. This forces the
compiler to make a copy of |a|, unless |b| is never modified, in which case why
did the programmer bother assigning |b| to |a|? Rune makes copies of builtin
datatypes on assignment, which means that within a single scope, you cannot
create new references to a value, only copies. The compiler can be smart enough
to move the value rather than copy it, if the right-hand-side is no longer used
elsewhere.

There is one case we know of (are there any others?) where having multiple
references withing a single scope to an object is useful: swapping. Consider
this Python code:

```
# Assign |a| and |b| to large values we don't want to copy.
a = [1, 2, 3, 4]
b = ("this", ["is", "a"], "test")
# Swap a and b.
temp = a
a = b
b = temp
```

This is potentially inefficient in Rune. A more efficient Python solution is:

```
(a, b) = (b, a)
```

This is the preferred method of swapping variables in Rune, and should not cause
any copy operations.

(TODO: implement tuple assignment in Rune!)

### Creating new classes with copy-on-assign semantics

Simply overload the = operator. If you overload a binary operator, but not its
corresponding assignment operator (e.g. overload =, +, but not +=), then Rune
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
exports the `baz` function, `bar.baz()` would be the correct calling convention.
Every package has a special `package.rn` module which contains initialization
code and imports other modules in the package. The identifiers exported by a
package are defined as exactly the identifiers exported directly from
`package.rn` or exported by the modules it imports.

### Object lifetimes

## Modules, packages, and libraries

In short:

*   A module is an individual Rune .rn file.
*   A package is a directory of Rune .rn files, with a “package.rn” file that
    imports the rest of the files in the package.
*   A Library is a compiled set of Rune files, generating a shared .so file,
    that is compatible with C/C++.

We call a compiled unit a “library”, which typically compiles to a shared .so
file. You declare callable functions and methods of a library with the exportsys
keyword:

```
exportlib func sha256(s:string): string {

    ...
}
```

Calling a function in a shared library is less flexible in that function/method
type signatures must be fully specified with unqualified types. You can also
call object methods declared with exportlib, if in a previous call, you obtained
an object reference as a result. Before calling an exportlib function or method,
you must include the library with importlib:

```
importlib crypto

...

digest = crypto.sha256(data)
```

To expose class methods, use exportlib on each method. If you want the
constructor to be exported, declare it on the class. For example:

```
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

```
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
libraries must declare either exportlib APIs, or exportrpc APIs.

## Functions

Rune provides the several variations on the syntax to declare functions. Rune's
functions do not specify return types.

```
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

```
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

## Literals

### Identifiers

### Strings

### Integers

### Floating point

### Random values

## Keywords

```
appendcode  debug      for        isnull       relation  unittest
arrayof     default    func       iterator     return    unref
as          do         generate   mod          reveal    unsigned
assert      else       generator  null         secret    use
bool        export     if         operator     signed    var
cascade     exportlib  import     prependcode  string    while
case        exportrpc  importlib  print        switch    widthof
class       extern     importrpc  println      throw     yield
const       final      in         ref          typeof
```

## Datatypes

### Bool

You get the usual ‘true’, ‘false, and ‘bool’:

```
done = false
while !done {
    … If condition tests true {
            done = true
        }
}
```

### Integers

Integers are fixed-width, and either signed or unsigned. Binary operations
between integers require that the width and sign of both operands match.

```
1u21 + 2s21  // Illegal because of sign mismatch
123u255 \* 456u256  // Illegal because of width mismatch
// Legal if the value of ‘a’ does not change when cast to offset’s width and sign
<offset>a + offset
```

Operations on integers are not allowed to overflow/underflow without throwing an
error, unless operators starting with ! are used:

```
255u8 !+ 1u8  // Legal, and equal to 0u8
<u8>256u32  // Throws error at compile or runtime
!<u8>256u32  // Legal, and equal to 0
```

### Default integers

Integers without a specified width and sign are assumed to be u64, but are only
weakly attached to that type (as described below). NOTE: The default is
_unsigned_! This makes it simpler, for example, to shift/rotate by a default
integer, which must be unsigned.

*   widthof(1) // returns 64
*   println -1 //Illegal, because negation cannot be applied to u64.

### Default integer constant auto-casting

Default integer constants will automatically cast to the integer type of the
other operand. For example, the following is legal:

```
func add1(a) {
    return a + 1  // Type of 1 converts automatically to type of a.
}

println add1(1u32)
println add1(-1u1024)
```

### Floating point

Rune currently supports f32 (C float), and f64 (C double) types:

```
x = 0.1; y = 0.8
dist = sqrt(x^2 + y^2)
Println “distance = %f” % dis
```

### String

Strings are encoded in UTF-8, and are internally represented as dynamic arrays
of u8.

List string methods here...

### Dynamic arrays

### Tuples

### Function pointers

## Post-compilation polymorphism

C++ supports two kinds of polymorphism:

*   Post-compilation polymorphism: class inheritance
*   Pre-compilation polymorphism: templates

The style of efficiently supported post-compilation polymorphis that can be
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

```
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

```
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
appendcode  debug    function   prependcode  return    while
assert      extern   generate   print        switch    yield
assignment  final    generator  println      throw
call        for      if         ref          unittest
class       foreach  import     relation     unref
```

## Expressions

### Operators

```
!    !=   &=  :     <=  >>=   []  |=
!+   %    ()  <     <>  >>>=  ^   ||
!+=  %=    +  <<    =   @     ^=  ||=
!-   &    +=  <<<   ==  @=    /    ~
!-=  &&    ,  <<<=  >   @@    /=
!<   &&=   -- <<=   >=  @@=   |
```

#### Precedence

```
expr … expr
expr ? expr : expr
expr || expr
expr @@ expr
expr && exper
expr mod expr
<, >, <=, >=, ==, !=
expr | expr
expr @ expr
expr & expr
<<, >>, <<<, >>>,
+, -, !+, !-
\*, !\*, /, %
!, ~, -expr, <expr>, !<expr>
expr^expr
expr.expr, expr[expr], &func(...)
```

### Assignment operators

```
!+=  !=  &&=  +=  <<<=  <=  ==  >>=   @=   ^=  ||=
!-=  %=  &=   -=  <<=   =   >=  >>>=  @@=  |=
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
```

TailLinked

## Builtin interators

range

## Classes

## Iterrators

## Generators

In short, generators are compile-time interpreted Rune functions that create
new code or modify existing code.  All Rune relationships are written in Rune
using generators, which are able to modify both the parent and child classes.

The full Rune language is available to be interpreted in code generators,
and the full Rune database representing code is available to generators to
modify in any way.

Examples of cool capabilities generators enable include:

* Writing relationships in Rune, which cannot be done using templates or
  inheritance.
* Creating binary load/save functions for an entire module's set of classes.
* Adding infinite undo-redo ability to a module's set of classes.
* Adding runtime reflection APIs similar to Java's.
* Generatinig optimized code for a parameterized algorithm, such as FFTs.

Generators are called with

## Grammar

```
runeFile: statements

statements: {statement}

statement: import | class | appendCode | prependCode | function | externFunction
    | ifStatement | switchStatement | whileStatement | forStatement
    | assignmentStatement | callStatement | printStatement | printlnStatement
    | throwStatement | assertStatement | returnStatement | generatorStatement
    | relationStatement | generateStatement | yield | unitTestStatement
    | debugStatement | foreachStatement | finalFunction | refStatement
    | unrefStatement

import: "import" pathExpressionWithAlias newlines
    | "import" pathExpressionWithAlias newlines
    | "importrpc" pathExpressionWithAlias newlines
    | "use" IDENT newlines

class: "class" IDENT '(' parameter {, parameter} ')' block
    | exportClassHeader '(' parameter {, parameter} ')' blo

exportClassHeader: "export" "class" IDENT
    | "exportlib" "class" IDENT
    | "exportrpc" "class" IDENT

appendCode: "appendcode" [pathExpression] block

prependCode: "prependcode" [pathExpression] block

block: '{' newlines statements '}' [newlines]

function: functionHeader parameters ["->" ':' expression] block
    | exportFunctionHeader parameters ["->" ':' expression] block

functionHeader: ["export | "exportlib" | "exportrpc"] "func" IDENT
    | ["export"] "iterator" IDENT
    | ["export"] "operator" operator

operator: '+' | '-' | '\*' | '/' | "<<" | ">>"" | "<<<" | ">>>" | "!+" | "!-" |
    "!\*" | '~' | '<' | "le" | '>' | ">=" | "==" | "!=" | '!' | '[' ']'

parameters: '(' parameter {',' parameter} ')'

parameter: ["var"] IDENT [':' expression] ['=' expression]
    | ["var"] '<' IDENT '>' [':' expression] ['=' expression]

externFunction: "extern" STRING functionHeader parameters [func':' expression] newlines
    | "extern" STRING exportFunctionHeader parameters [func':' expression] newlines

ifStatement: "if" expression block {"else" "if" expression block} ["else" block]

switchStatement: "switch" expression '{' newlines {switchCase} ["default" block] '}' [newlines]

switchCase: "case" expreessionList block

whileStatement: ["do" block] "while" expression [block] newlines

forStatement: "for nonConstAssignmentExpression ',' [newlines] expression ','
    [newlines] nonConstAssignmentExpression block

assignmentStatement: assignmentExpression newlines

assignmentExpression: nonConstAssignmentExpression
    | "const" nonConstAssignmentExpression

nonConstAssignmentExpression: accessExpression [':' expression] assignmentOp expression

assignmentOp: '=' | "+=" | "-=" | "\*=" | "/=" | "%=" | "&=" | "||=" | "@@=" | "&&=" |
    "||=" | "@=" | "^=" | "<<=" | ">>=" | "<<<=" | ">>>=" | "!+=" | "!-=" | "!\*="

accessExpression: tokenExpression
    | accessExpression '(' [callParameter {',' callParameter}] ')'
    | accessExpression '.' IDENT
    | accessExpression '[' expression ']'
    | accessExpression '[' expression ':' expression ']'
    | accessExpression '[' ']'

callStatement: accessExpression '(' [callParameter {',' callParameter}] ')' newlines

callParameter: expression | IDENT = expression

printStatement: "print" expressionList newlines

printlnStatement: "println" expressionList newlines

throwStatement: "throw" expressionList newlines

assertStatement: "assert" expressionList newlines

returnStatement: "return" newlines | "return" expression newlines

generatorStatement: "generator" IDENT parameters block

generateStatement: "generate" pathExpression '(' expressionList ')' newlines

relationStatement: "relation" pathExpression pathExpression [label] pathExpression [label] ["cascade"] expressionList newlines

label: ':' STRING

yield: "yield" expression newlines

unitTestStatement: "unittest" block

debugStatement: "debug" block

foreachStatement: "for" IDENT "in" expression block

finalFunction: "final" '(' parameter ')' block

refStatement: "ref" expression newlines

unrefStatement: "unref" expression newlines

expressionList: expression {, [newlines] expression}

twoOrMoreExpressions: expression ',' [newlines] expression {',' [newlines] expression}

expression: dotDotDotExpression

dotDotDotExpression: selectExpression "dotdotdot" selectExpression
    | selectExpression

selectExpression: orExpression
    | orExpression '?' orExpression ':' orExpression

orExpression: xorExpression
  | orExpression "||" xorExpression

xorExpression: andExpression
    | xorExpression "@@" andExpression

andExpression: modExpression
    | andExpression "&&" modExpression

modExpression: relationExpression
    | relationExpression "%" bitorExpression

relationExpression: bitorExpression
    | bitorExpression '<' bitorExpression
    | bitorExpression "<=" bitorExpression
    | bitorExpression '>' bitorExpression
    | bitorExpression ">=" bitorExpression
    | bitorExpression "==" bitorExpression
    | bitorExpression "!=" bitorExpression

bitorExpression: bitxorExpression
    | bitorExpression '|' bitxorExpression
bitxorExpression: bitandExpression
    | bitxorExpression '@' bitandExpression

bitandExpression: shiftExpression
    | bitandExpression '&' shiftExpression

shiftExpression: addExpression
    | addExpression "<<" addExpression
    | addExpression ">>"" addExpression
    | addExpression "<<<" addExpression
    | addExpression ">>>" addExpression
    | addExpression "!+" addExpression
    | addExpression "!-" addExpression
    | addExpression "!\*" addExpression

addExpression: mulExpression
    | addExpression '+' mulExpression
    | addExpression '-' mulExpression
    | "!-" mulExpression

mulExpression: prefixExpression
    | mulExpression '\*' prefixExpression
    | mulExpression '/' prefixExpression
    | mulExpression '%' prefixExpression

prefixExpression: exponentiateExpression
    | '!' prefixExpression
    | '~' prefixExpression
    | '-' prefixExpression
    | '<' prefixExpression '>' prefixExpression
    | "casttrunc" prefixExpression '>' prefixExpression

exponentiateExpression: postfixExpression
    | postfixExpression '^' exponentiateExpression  // Binds right to left.

postfixExpression: accessExpression
    | '&' pathExpression '(' expressionList ')'

tokenExpression: IDENT
    | STRING
    | INTEGER
    | FLOAT
    | RANDUINT
    | BOOL
    | '[' oneOrMoreExpressions ']'
    | '(' expression ')'
    | tupleExpression
    | "null"
    | typeLiteral
    | "secret" '(' expression ')'
    | "reveal" '(' expression ')'
    | "arrayof" '(' expression ')'
    | "typeof" '(' expression ')'
    | "unsigned" '(' expression ')'
    | "signed" '(' expression ')'
    | "widthof" '(' expression ')'
    | "isnull" '(' expression ')'

typeLiteral: UINTTYPE
    | INTTYPE
    | "string"
    | "bool"
    | "f32"
    | "f64"

pathExpression: IDENT
    | pathExpression '.' IDENT

pathExpressionWithAlias: pathExpression

tupleExpression: '(' twoOrMoreExpressions ')'

newlines: '\n'
    | ';'
    | newlines '\n'
    | newlines ';'
```
