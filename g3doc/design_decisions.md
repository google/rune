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

## Exception handling

Rune exceptions are thrown with a fixed-format error value:

```rune
struct Error {
    fileName: string = ""
    line u32 = 0u32
    customCode: ErroroCode = ErrorCode.Unknonwn
    customType: u32 = 0u32
    customuValue u32 = 0u32
    errorMessage: string = ""
}
```

When printing `Error` objects, if the custom error type and code are recognized
by the compiler, they will print like ErrorType.Code.  If they are new values
not present when the Rune library was compiled (because a dependent .so library
changed), the values will be printed as integers.

The Error struct is stored in thread-local memory, not returned on the stack.
A Boolean Exception flag is also stored in thread-local memory.  The caller
should branch to cleanup code to unwind the stack if this flag is set.

This choice is a compromise between flexibility and maintaining ABI
compatibility when dependent Rune libraries change. ErrorCode changes should be
rare ABI breaking events, while custom enums and custom enum values change
often. Because of this, trowing custom enum types should be avoided in the APIs
of Rune libraries that need ABI compatibility, preferring to return status codes
instead.

The throw statement checks the first parameter to see if it is an ErrorCode. If
not, it fills in ErrorCode.Unknown. If the first parameter is an ErrorCode, it
checks the next parameter to see if it is an enum value, and if so, sets the
custom error enum type and value. If no custom error value or value is given,
these two fields are set to 0u32. For this reason, 0 should be used for Unknown
in custom error types. Any remaining parameters are converted to a string just
like a print statement. For example:

```rune
enum HttpError {
    Unknown = 0  // Not used by the HTTP protocol.
    NotFound = 404
    ...
}

// Throws (ErrorCode.Unknown, 0u32, 0u32, "My code is having a bad day")
throw "My code is having a bad day"
// Throws (fileName, line, ErrorCode.InvalidArgument, 0u32, 0u32, "Your code passed me bad data")
throw ErrorCode.InvalidArgument, "Your code passed me bad data"
// Throws (fileName, line, ErrorCode.NotFound, <u32>HttError, <u32>HttpError.NotFound, "Could not...")
throw ErrorCode.NotFound, HttpError.NotFound, "Could not find web page at ", url
// Throws (fileName, line, ErrorCode.NotFound, <u32>HttpError, <u32>HttpError.NotFound, "")
throw ErrorCode.NotFound, HttpError.NotFound
```

Matching errors in (catch or except?) statements match either ErrorCode values,
custom enum types, or custom enum values.

ErrorCode is a standard enum value, based on absl::StatusCode, but without the
OK value, since ErrorCode only represents errors. The 0 OK value is Unknown.

Exceptions are not thrown by panic. Instead by default a stack trace is printed.
TODO: Rune should support custom panic handlers. By default panic print a stack
trace and exits.

Out of memory does not throw an exception, but by default panics. TODO: Rune
should support custom OOM handlers.

Catch (or except?) can match any of the error tuple fields, other than the error
message.

```rune
try {
    jsonPayload = getJsonResponse(url)
} catch[or except?] e {
  ErrorCode.InvalidArgument => throw e  // Propagates current error up the stack.
  HttpError.NotFound {
      Log(LogType.Info, "Web page not found: %s" % url)
      return (null(Json), HttpStatus.NotFound)  // HttpStatus, not HttpError.
  }
  HttpError => return (null(Json), HttpStatus.Unkown)
  default {
      Log(LogType.Error, e.errorMesssage)
      return (null(Json), HttpStatus.Unkown)
  }
}
```

Some points:

*   Catching errors is *slow*. The usual non-error path is optimized at the
    expense of exception handling speed.
*   Exceptions from dependent libraries should be handled in an ABI compatible
    manner. If some dependency throws a new enum type or a new enum value of a
    known enum type it should have a reasonable default action.
*   Your code can still return a StatusOr-like tuple such as (Json, HttpStatus).
    This is the normal execution path, not exception handling.

### Rationale

Exception handling is a very complex topic. See:

*   [C++ exceptions fractured community](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2018/p0709r0.pdf)
*   [Nim's adventure in exception handling](https://status-im.github.io/nim-style-guide/errors.html)
*   [Rust's design](https://www.lpalmieri.com/posts/error-handling-rust/)

In C++, divergent exception handling has fractured the community into
groups with incompatible code bases. Even a modern language like Nim struggles
with it. The Rust community seems to devote more code to error handling than C,
C++, or Python, because the standard method of returning Result\<T, E\> is too
inflexible, returning only one error type. A lot of the Rust error handling code
is devoted to translation between error types as a result.

The first paper linked above covers this complex topic in detail, and Rune is
essentially following the author's recommendation: Errors should be thrown by a
statically typed value, not a dynamic type. Rune encourages use of ErrorCode by
default, especially at the ABI level. Consumers of ABI-level error types should
not break when those error types change, so reasonable default handling should
be provided.
