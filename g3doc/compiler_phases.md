# Rune Compiler Phases

[TODO: Flesh this out. This is only an initial skeleton, which will be fleshed
out as we build out the phases.

What we have now:

*   Manually written lexing in bootstrap/parse/lexer.rn.
*   PEG-parser interpreter in bootstrap/parse/pegparser.rn.
*   HIR-builder partially completed in bootstrap/parse/hir.rn and
    bootstrap/parse/exprTree.rn.

A minimal skeleton of code for various phases will be added next to enable
compilation of tests/helloworld.rn.

[TOC]

## Loading syntax files

To support Domain Specific Languages (DSLs, Rune allows users to extend Rune's
syntax through .syn files.

## Load imported modules

Similar to Python, the Rune compiler recursively loads modules by looking for
top-level `import` and `use` statements.

## Executing transforms and relations

`transform` and `relation` statements are executed before further analysis to
complete the code that will be compiled. Transformers will be compiled as .so
shared libraries, loaded at runtime, and will have full access to the HIR data
structures, as well as the ability to execute `prependcode` and `apendcode`
statements.

## Variable and data member discovery

All variables and class data members will be inferred from assignment
statements. Data member names will be determined for class templates.

## Semantic checking.

## Global type inference

This will propagate types in all directions globally, even across function
calls.

## Handling print/println parameters

Rune generates printf-like format strings automatically for print/println and
also `<string> % <tuple>` operations.

## Signature post-processing

Various steps include adding missing `return`statements, and reachability
analysis.

## Adding memory management

Preferably this can be SoA or AoS memory layout based on a compiler flag, to
enable users to easily compare performance both ways.

## Inline iterators.

## Lowering to LIR (Low-level IR) pass.

It is not yet clear if we will reuse the HIR data structures for this step or
create a custom LIR database.

## Passes to prepare for code generation

This phase should considerably simplify both C and LLVM IR code generation.

This includes several steps. Temporary arrays are allocated/freed. Stack
unwinding may be created here. We may lower to basic blocks. We also may explode
expression trees to into
[SSA-like](https://en.wikipedia.org/wiki/Static_single-assignment_form)
statements.

## Lifetime analysis

This detects potential user-after-free issues, as well as potential use of
uninitialized data.

## Generate C code from the LIR.

Winning benchmarks is a high priority goal for the Rune effort. Unfortunately,
Clang's front-end does a huge amount of optimization, including loop unrolling
and vectoriazation, making it hard to win benchmarks when writing LLVM IR
directly.

*   Generate LLVM IR directly It is unclear if this step is worthwhile, but it
    is straight forward if we want to continue supporting LLVM IR as a backend.

## Invoke C or LLVM IR compiler

This generates the final executable or library.
