# Design Decisions
As various design decisions are made, we will list pros, cons, and our choices
here.

[TOC]

## The value of null is 0
The null value is an unsigned integer with a default width of 32.  This can be
set per class, e.g.

```rune
class Foo:4 (self) {
    ...
}
```
In this case, the reference is a u4.  The two clear choices for null are 0 and
all 1's.  In this case, it would be 0u4 or 0xfu4.

### Pros
* Comparing against 0 is faster than comparing against an array's size, and in
  some cases slightly fast than comparing to all 1's.
* Having an all 1's null means we cannot simply zero out an array or structure
  and have correct default values for objects.  The compiler would have to
  generate code to explicitly set all null values at runtime.
* Unless a user casts an integer to am object reference, there should never be
  an invalid object index, other than null, so we should not have to compare
  against the array size before indexing.

### Cons
* For even stronger debugging in debug mode, it would be nice to detect array
  index out of bounds for invalid object references.  This can help track down
  compiler bugs or cases where users incorrectly cast an integer to an object
  reference.
* We waste the memory in location 0 of all arrays backing member data.  We could
  fix this in the future, with some more complexity in the compiler.

## The default integer type is u64
This is somewhat jarring to new users, who naturally assume the default integer
type should be able to represent negative numbers like -1.  Justification for
this choice requires taking a look at what happened when we tried to change the
default to i64.  Because we often return u64 from functions that return sizes,
making i64 the default placed a burden on the user, causing them to need many
more integer casts than when making u64 the default.

### Pros
* Users have fewer casts in their code when u64 is the default
* Unsigned overflow detection is faster than signed overflow detection on
  Intel/AMD x64 architectures.

### Cons
* Unsigned default integer values can be jarring for new Rune users.
