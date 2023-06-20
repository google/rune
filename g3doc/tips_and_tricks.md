# Tips and Tricks for Contributint to Rune

Currently the main effort is building the bootstrap Rune compiler: rewriting the
Rune compiler in Rune. Here are some tips for working on this effort.

1.  Use "make -j10" or something like that in the top level Rune directory to
    build Rune. Debugging is simpler using make.
2.  In bootstrap/database or bootstrap/parsegen, also use make.
3.  If the C Rune compiler fails in a way that looks like a bug, ping
    waywardgeek@google.com. There are lots of C Rune compiler bugs remaining,
    and some will be fixed, while others we will work around.
4.  Avoid poorly tested features of Rune. In particular, try to use
    cascade-delete classes rather than ref-counted classes, and try to use
    classes rather than structs.
5.  Run "make" a lot. Sometimes the Rune compiler loses its mind and starts
    giving incorrect feedback, making it impossible to fix the error, because
    you have no idea where it is. It is nice in these cases to revert the last
    few lines changed and to try again. Usually, the error is real, but the
    reporting is borked.
6.  You can debug your Rune binary with gdb, but there are a lot of gotchas:
    1.  The LLVM compiler optimizes out a TON of parameters. If you follow the
        explicit advice of the LLVM gurus, you'll avoid writing all your
        parameters to local variable storage like C/C++ front-ends do. This
        makes life hard for the LLVM back-end folks, but today, they punish
        those who follow their advice by optimizing out parameters entirely,
        making debugging *much* harder. To see what is available, use "info
        locals" in gdb.
    2.  Use the bulitin helper functions! You can type 'source prettyprint.py'
        if your are in the top-level Rune directory, and then when you print an
        object, you'll see its fields.
    3.  Use the "show" methods, e.g. `call pegparser_Peg_show(parser)` will show
        you the contents of `parser` assuming Clang has not optimized it away.
    4.  Write `dump` methods for your classes, and use the ones that already
        exist. They are invaluable for debugging in gdb.
7.  The Rune C compiler is NOT memory safe. In particular, it passes all tuples,
    structures and arrays by reference, een if the called function may realloc
    the arrays containing them. This is mostly a problem when writing copy
    methods for classes: DO NOT PASS ARRAYs, TUPLES, or STRUCTs to copy
    constructors. The auto-generated allocation function may move the arrays
    holding the data, causing your program to crash randomly. The work-around is
    to assign the tuple/array/struct to a local variable, which in the C Rune
    compiler forces a copy, and to pass the copy to the copy-constructor.
8.  There is a global 'counter' variable which can be used in condition
    expressions on GDB breakpoints.
