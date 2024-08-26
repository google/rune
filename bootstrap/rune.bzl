"""Build rules for the Rune compiler."""

load("//tools/build_defs/build_test:build_test.bzl", "build_test")

RuneInfo = provider(
    "Track all .rn Rune files transitively so they can be compiled together" +
    " in a rune_genc rule",
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

def _rune_genc_impl(ctx):
    """Implementation for rune_genc rule."""

    transitive = get_transitive_srcs([ctx.file.src], ctx.attr.deps)
    out = ctx.outputs.out

    args = ctx.actions.args()
    args.add("-n")
    args.add("-q")
    args.add("-p")
    args.add("third_party/rune/bootstrap")
    args.add("--oc")
    args.add(out.path)
    args.add_all([ctx.expand_location(opt) for opt in ctx.attr.opts])

    # Don't call clang to generate an executable, just write the .c output.
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

rune_genc = rule(
    implementation = _rune_genc_impl,
    doc = "Generates C .c files from a Rune top-level .rn file.",
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
            doc = "Generated C .c file from Rune input files.",
        ),
        "_rune": attr.label(
            executable = True,
            cfg = "exec",
            allow_files = True,
            default = Label("//third_party/rune/bootstrap:rune"),
            doc = "Specifies path to the Rune compiler executable.",
        ),
        "_rune_runtime_files": attr.label(default = "//third_party/rune/bootstrap:rune_runtime_files"),
    },
)

def rune_cc_library(name, src, deps = [], rune_opts = [], copts = [], visibility = None):
    """Create a cc_library from a Rune source file and dependent groups of .rn files.

    Args:
      name: The cc_lilbrary name.
      src: The top-level Rune file; others are are found through deps.
      deps: List of rune_package targets.  See third_party/rune/math/BUILD for
            an example.
      rune_opts: Options to pass to the Rune compiler
      copts: Options to pass to clang when generating the cc_library.
      visibility: The usual visibility argument.
    """

    c_file_name = src[:-3] + ".c"
    o_file_name = src[:-3] + ".o"
    rune_opts += select({
        "//tools/compilation_mode:dbg": ["-g"],
        "//conditions:default": [],
    })
    rune_genc(
        name = name + "_genc",
        src = src,
        out = c_file_name,
        deps = deps,
        opts = rune_opts,
        visibility = visibility,
        tags = ["not_build:arm"],
        target_compatible_with = ["//third_party/bazel_platforms/cpu:x86_64"],
    )
    full_copts = copts + ["-Wno-main-return-type"]
    native.cc_library(
        name = name,
        srcs = [c_file_name],
        linkstatic = 1,
        copts = full_copts,
        deps = [
            "//third_party/CTTK:libcttk",
            "//third_party/rune/runtime",
        ],
        tags = ["not_build:arm"],
        target_compatible_with = ["//third_party/bazel_platforms/cpu:x86_64"],
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
      target: The name of your executable target.
      stdout: Name of the file containing the golden output.
      stdin: Optional input file which the target reads from stdin.
      args: Optional command-line arguments to pass to the target executable.
      visibility: The usual visibility parameter.
    """

    full_args = ["$(location: " + target + ")", "$(location " + stdout + ")"]
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

def exception_test(
        name,
        target,
        stdin = None,
        args = None,
        visibility = None):
    """Run a program that we expect to fail.

    This creates an sh_test target that will run your executable target with an
    optional .stdin file, and check that running the binary threw an exception.

    Args:
      name: The name of the test target.
      target: The name of your executable taret.
      stdin: Optional input file which the target reads from stdin.
      args: Optional command-line arguments to pass to the target executable.
      visibility: The usual visibility parameter.
    """

    full_args = ["$(location :" + target + ")"]
    data = [target, "//testing/shbase:googletest.sh"]
    if stdin != None:
        full_args.append("$(location " + stdin + ")")
        data.append(stdin)
    else:
        full_args.append("nostdin")
    if args != None:
        full_args += args

    native.sh_test(
        name = name + "_exception_test",
        srcs = ["//third_party/rune:run_exception_test_script"],
        args = full_args,
        data = data,
        visibility = visibility,
    )

# Rune language tests, requires compiling and executing rune programs.
def _common_rune_test_build(
        rule_name,
        name,
        src,
        deps = [],
        rune_opts = [],
        copts = [],
        visibility = None,
        no_binary = False):
    if len(src) < 4 or src[-3:] != ".rn":
        fail(rule_name + " requires src to be a Rune file ending in .rn.")
    base_name = src[:-3]
    lib_name = base_name + "_cc_library"

    rune_cc_library(
        name = lib_name,
        src = src,
        deps = deps,
        rune_opts = rune_opts,
        copts = copts,
        visibility = visibility,
    )

    if not no_binary:
        native.cc_binary(
            name = base_name,
            deps = [lib_name],
            copts = copts,
            visibility = visibility,
            tags = ["not_build:arm"],
            target_compatible_with = ["//third_party/bazel_platforms/cpu:x86_64"],
        )

    return base_name, lib_name

def rune_stdio_test(
        name,
        src,
        stdout,
        stdin = None,
        data = [],
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
      stdout: Set this to your .stdout file.
      stdin: Set this to your  if you have a .stdin file for this test.
      data: Data files that need to be present to run the test.
      deps: These are rune_package targets containing Rune files.
      rune_opts: Options passed to the Rune compiler for this test.
      copts: The copts passed to the cc_library target for this test.
      visibility: The usual visibility parameter.
    """

    base_name, _ = _common_rune_test_build(
        "rune_stdio_test",
        name,
        src,
        deps,
        rune_opts,
        copts,
        visibility,
    )

    args = ["$(location :" + base_name + ")", "$(location " + stdout + ")"]
    full_data = data + [base_name, stdout, "//testing/shbase:googletest.sh"]
    if stdin != None:
        args.append("$(location " + stdin + ")")
        full_data.append(stdin)
    else:
        args.append("nostdin")
    native.sh_test(
        name = name,
        srcs = ["//third_party/rune:run_stdio_test_script"],
        args = args,
        data = full_data,
        visibility = visibility,
        tags = ["not_build:arm"],
        target_compatible_with = ["//third_party/bazel_platforms/cpu:x86_64"],
    )

def rune_compiletime_error_test(
        name,
        src,
        deps = [],
        rune_opts = [],
        copts = [],
        visibility = None):
    """Verify Rune returns a compile-time error.

    Run the Rune compiler and verify that it returns a compile time error.

    Args:
      name: The name of the test target.
      src: The top-level Rune file for this test.
      deps: These are rune_package targets containing Rune files.
      rune_opts: Options passed to the Rune compiler for this test.
      copts: The copts passed to the cc_library target for this test.
      visibility: The usual visibility parameter.
    """

    # The -x rune opt causes Rune to return 0 on compilation error,
    # and 1 otherwise. It also generates an empty .c file.
    _, lib_name = _common_rune_test_build(
        "rune_compiletime_error_test",
        name,
        src,
        deps,
        rune_opts + ["-x"],
        copts,
        visibility,
        no_binary = True,
    )

    build_test(
        name = name,
        testonly = True,
        targets = [lib_name],
        visibility = visibility,
    )

def rune_exception_test(
        name,
        src,
        stdin = None,
        deps = [],
        rune_opts = [],
        copts = [],
        visibility = None):
    """Test that the Rune target throws an exception.

    Compile and rune a Rune target, and verify that it throws an exception.

    Args:
      name: The name of the test target.
      src: The top-level Rune file for this test.
      stdin: Set this to your  if you have a .stdin file for this test.
      deps: These are rune_package targets containing Rune files.
      rune_opts: Options passed to the Rune compiler for this test.
      copts: The copts passed to the cc_library target for this test.
      visibility: The usual visibility parameter.
    """

    base_name, _ = _common_rune_test_build(
        "rune_exception_test",
        name,
        src,
        deps,
        rune_opts,
        copts,
        visibility,
    )

    args = ["$(location :" + base_name + ")"]
    data = [base_name, "//testing/shbase:googletest.sh"]
    if stdin != None:
        args.append("$(location " + stdin + ")")
        data.append(stdin)
    else:
        args.append("nostdin")
    native.sh_test(
        name = name,
        srcs = ["//third_party/rune:run_exception_test_script"],
        args = args,
        data = data,
        visibility = visibility,
    )
