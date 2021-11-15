"""Build rules for the Rune compiler."""

RuneInfo = provider(
    "Track all .rn Rune files transitively so they can be compiled together" +
    " in a rune_genllvm rule",
    fields = ["transitive_sources"],
)

def get_transitive_srcs(srcs, deps):
    """Obtain the source files for a target and its transitive dependencies."""

    return depset(
        srcs,
        transitive = [dep[RuneInfo].transitive_sources for dep in deps],
    )

def _rune_package_impl(ctx):
    transitive = get_transitive_srcs(ctx.files.srcs, ctx.attr.deps)
    return [RuneInfo(transitive_sources = transitive)]

# A Rune package is declared with rune_package, which should list all
# Rune source files in srcs, and depdendent rune_package targets in
# deps.
rune_package = rule(
    implementation = _rune_package_impl,
    doc = "Declares a Rune package with its files in a directory.",
    attrs = {
        "srcs": attr.label_list(
            allow_files = [".rn"],
            doc = "Rune .rn files in a directory that makes up a package.",
        ),
        "deps": attr.label_list(
            providers = [RuneInfo],
            doc = "Rune sub-packages needed to build this package.",
        ),
    },
)

def _rune_genllvm_impl(ctx):
    """Implementation for rune_genllvm rule."""

    transitive = get_transitive_srcs([ctx.file.src], ctx.attr.deps)
    out = ctx.outputs.out

    args = ctx.actions.args()
    args.add("-n")
    args.add("-p")
    args.add("third_party/rune")
    args.add("-l")
    args.add(out.path)
    args.add_all([ctx.expand_location(opt) for opt in ctx.attr.opts])

    # Don't call clange to generate an executable, just write the .ll output.
    args.add(ctx.file.src.path)

    ctx.actions.run(
        executable = ctx.executable._rune,
        arguments = [args],
        inputs = ctx.files._rune_runtime_files + transitive.to_list(),
        outputs = [out],
        mnemonic = "rune",
        progress_message = "Compilng Rune %s --> %s" %
                           (
                               ctx.file.src.short_path,
                               out.path,
                           ),
    )

    return [DefaultInfo(files = depset([out]))]

rune_genllvm = rule(
    implementation = _rune_genllvm_impl,
    doc = "Generates LLVM IR .ll files from a Rune top-level .rn file.",
    attrs = {
        "src": attr.label(
            mandatory = True,
            allow_single_file = [".rn"],
            doc = "The .rn Rune input file to the Rune compiler.",
        ),
        "opts": attr.string_list(
            doc = "Additional Rune compiler options.  -O is implied by -c opt.",
        ),
        "deps": attr.label_list(
            doc = "List of dependent Rune targets",
            providers = [RuneInfo],
        ),
        "out": attr.output(
            mandatory = True,
            doc = "Generated LLVM IR .ll file from Rune input files.",
        ),
        "_rune": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("//third_party/rune:rune"),
            doc = "Specifies path to the Rune compiler executable.",
        ),
        "_rune_runtime_files": attr.label(default = "//third_party/rune:rune_runtime_files"),
    },
    output_to_genfiles = True,
)

def llvm_codegen(name, src, out, copts = [], visibility = None):
    """This macro calls Clang on a .ll file to generate a .o file."""

    copts_string = "-fPIC " + " ".join(copts)
    cmd_dbg = "$(CC) %s -c -g -o $@ $<" % copts_string
    cmd_opt = "$(CC) %s -c -O3 -o $@ $<" % copts_string
    cmd = select({
        "//tools/compilation_mode:dbg": cmd_dbg,
        "//conditions:default": cmd_opt,
    })
    native.genrule(
        name = name,
        outs = [out],
        srcs = [src],
        cmd = cmd,
        toolchains = ["//tools/cpp:toolchain"],
        visibility = visibility,
    )

def rune_cc_library(name, src, deps = [], rune_opts = [], copts = [], visibility = None):
    """Create a cc_library from a Rune source file and dependent groups of .rn files.

    Args:
      name: The cc_lilbrary name.
      src: The top-level Rune file; others are are found through deps.
      deps: List of rune_package targets.  See third_party/rune/math/BUILD for
            an example.
      rune_opts: Options to pass to the Rune compiler
      copts: Options to pass to Clang when generating the cc_library.
      visibility: The usual visibility argument.
    """

    ll_file_name = src[:-3] + ".ll"
    o_file_name = src[:-3] + ".o"
    rune_opts += select({
        "//tools/compilation_mode:dbg": ["-g"],
        "//conditions:default": [],
    })
    rune_genllvm(
        name = name + "_genllvm",
        src = src,
        out = ll_file_name,
        deps = deps,
        opts = rune_opts,
        visibility = visibility,
    )
    llvm_codegen(
        name = name + "_codegen",
        src = ll_file_name,
        out = o_file_name,
        copts = copts,
        visibility = visibility,
    )
    native.cc_library(
        name = name,
        srcs = [o_file_name],
        linkstatic = 1,
        copts = copts,
        deps = [
            "//third_party/CTTK:libcttk",
            "//third_party/rune/runtime",
        ],
    )

def stdio_test(
        name,
        target,
        stdout,
        stdin = None,
        args = None,
        visibility = None):
    """Run a program that reads/writes stdio.

    This creates an sh_test target that will run your executable target with an
    optional .stdin file, and compare the stdout from your target to your
    .stdout file.  The .stdout file must have the same name as the Rune file,
    replacing the .rn with .stdout.  If you need a .stdin file for this test,
    name it with a .stdin suffix.  For example usage, see
    //third_party/rune/tests/BUILD.

    Args:
      name: The name of the test target.
      target: The name of your executable taret.
      stdout: Name of the file containing the golden output.
      stdin: Optional input file which the target reads from stdin.
      args: Optional command-line arguments to pass to the target executable.
      visibility: The usual visibility parameter.
    """

    full_args = ["$(location :" + target + ")", "$(location " + stdout + ")"]
    data = [target, stdout, "//testing/shbase:googletest.sh"]
    if stdin != None:
        full_args.append("$(location " + stdin + ")")
        data.append(stdin)
    else:
        full_args.append("nostdin")
    if args != None:
        full_args += args

    native.sh_test(
        name = name + "_stdio_test",
        srcs = ["//third_party/rune:run_stdio_test_script"],
        args = full_args,
        data = data,
        visibility = visibility,
    )

def rune_stdio_test(
        name,
        src,
        stdout,
        stdin = None,
        deps = [],
        rune_opts = [],
        copts = [],
        visibility = None):
    """Run a Rune test program that reads/writes stdio.

    This creates an sh_test target that will run your Rune test with an
    optional .stdin file, and compare the stdout from your Rune program to your
    .stdout file.  The .stdout file must have the same name as the Rune file,
    replacing the .rn with .stdout.  If you need a .stdin file for this test,
    name it with a .stdin suffix.  For example usage, see
    //third_party/rune/tests/BUILD.

    Args:
      name: The name of the test target.
      src: The top-level Rune file for this test.
      has_stdin: Set this to True if you have a .stdin file for this test.
      deps: These are rune_package targets containing Rune files.
      rune_opts: Options passed to the Rune compiler for this test.
      copts: The copts passed to the cc_library target for this test.
      visibility: The usual visibility parameter.
    """

    if len(src) < 4 or src[-3:] != ".rn":
        fail("rune_stdio_test requires src to be a Rune file ending in .rn.")
    base_name = src[:-3]
    lib_name = base_name + "cc_library"

    rune_cc_library(
        name = lib_name,
        src = src,
        deps = deps,
        rune_opts = rune_opts,
        copts = copts,
        visibility = visibility,
    )

    native.cc_binary(
        name = base_name,
        deps = [lib_name],
        copts = copts,
        visibility = visibility,
    )

    args = ["$(location :" + base_name + ")", "$(location " + stdout + ")"]
    data = [base_name, stdout, "//testing/shbase:googletest.sh"]
    if stdin != None:
        args.append("$(location " + stdin + ")")
        data.append(stdin)
    else:
        args.append("nostdin")
    native.sh_test(
        name = name,
        srcs = ["//third_party/rune:run_stdio_test_script"],
        args = args,
        data = data,
        visibility = visibility,
    )
