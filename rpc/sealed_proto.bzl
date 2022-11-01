"""Rules for generating code to send/receive sealed RPCs."""

SealedProtoInfo = provider(
    "Track all .rn files transitively.",
    fields = ["transitive_sources"],
)

def get_transitive_srcs(src, deps):
    """Obtain the source files for a target and its transitive dependencies.

    Args:
      src: a SealedProto .rn file
      deps: a list of targets that are direct dependencies
    Returns:
      a collection of the transitive sources
    """
    return depset(
        [src],
        transitive = [dep[SealedProtoInfo].transitive_sources for dep in deps],
    )

def _genrpcfiles_impl(ctx):
    """Implementation for genrpcfiles rule."""

    # Argument list
    args = ctx.actions.args()
    args.add(ctx.file.src)
    args.add(ctx.outputs.header_out)
    args.add(ctx.outputs.client_out)
    args.add(ctx.outputs.server_out)

    transitive = get_transitive_srcs(ctx.file.src, ctx.attr.deps)

    # Output files
    outputs = [
        ctx.outputs.header_out,
        ctx.outputs.client_out,
        ctx.outputs.server_out,
    ]

    ctx.actions.run(
        arguments = [args],
        executable = ctx.executable._rpcgen,
        inputs = transitive,
        mnemonic = "rpcgen",
        outputs = outputs,
        progress_message = "Generating sealed proto %s --> %s, %s, %s" %
                           (
                               ctx.file.src.short_path,
                               ctx.outputs.header_out.short_path,
                               ctx.outputs.client_out.short_path,
                               ctx.outputs.server_out.short_path,
                           ),
    )
    return [DefaultInfo(files = depset(outputs)), SealedProtoInfo(transitive_sources = transitive)]

genrpcfiles = rule(
    attrs = {
        "src": attr.label(
            allow_single_file = [".rn"],
            doc = "The .rn source file for this rule",
            mandatory = True,
        ),
        "header_out": attr.output(
            doc = "The generated header file",
            mandatory = True,
        ),
        "client_out": attr.output(
            doc = "The generated client source file",
            mandatory = True,
        ),
        "server_out": attr.output(
            doc = "The generated server source file",
            mandatory = True,
        ),
        "deps": attr.label_list(
            allow_files = True,
            doc = "SealedProto .rn files that your .rn file imports.",
        ),
        "_rpcgen": attr.label(
            allow_files = True,
            cfg = "exec",
            default = Label("//third_party/rune/rpc:rpcgen"),
            executable = True,
        ),
    },
    doc = "Generate C++ sources from a Sealed Computing .rn file.",
    output_to_genfiles = True,
    implementation = _genrpcfiles_impl,
)

def sealed_cc_proto(
        name,
        src,
        deps = [],
        visibility = None):
    """Generate cc_library targets for the src sealed proto def file, and
       <name>_client and <name>_server.

    Args:
      name: The name of this target.
      src: The top-level .rn file.
      deps: Other sealed_cc_proto targets this one depends on.
      visibility: The usual visibility parameter.
    """

    client_lib = name + "_client"
    server_lib = name + "_server"
    header_out = name + ".h"
    client_out = name + "_client.cc"
    server_out = name + "_server.cc"

    genrpcfiles(
        name = name + "_genrpc",
        src = src,
        client_out = client_out,
        header_out = header_out,
        server_out = server_out,
        visibility = visibility,
        deps = deps,
    )

    native.cc_library(
        name = client_lib,
        srcs = [client_out],
        hdrs = [header_out],
        linkopts = select({
            "//conditions:default": [],
            "//tools/cc_target_os:emscripten": [
                "-s STANDALONE_WASM",
                "-s ERROR_ON_UNDEFINED_SYMBOLS=0",
            ],
        }),
        visibility = visibility,
        deps = [
            "//third_party/sealedcomputing/rpc:encode_decode_lite",
            "//third_party/sealedcomputing/wasm3:base",
            "//third_party/sealedcomputing/wasm3:bytestring",
            "//third_party/sealedcomputing/wasm3:logging",
            "//third_party/sealedcomputing/wasm3:send_rpc",
            "//third_party/sealedcomputing/wasm3:socket",
            "//third_party/sealedcomputing/wasm3:status",
            "//third_party/sealedcomputing/wasm3:statusor",
        ],
        alwayslink = 1,
    )

    native.cc_library(
        name = server_lib,
        srcs = [server_out],
        hdrs = [header_out],
        linkopts = select({
            "//conditions:default": [],
            "//tools/cc_target_os:emscripten": [
                "-s STANDALONE_WASM",
                "-s ERROR_ON_UNDEFINED_SYMBOLS=0",
            ],
        }),
        visibility = visibility,
        deps = [
            "//third_party/sealedcomputing/rpc:encode_decode_lite",
            "//third_party/sealedcomputing/wasm3:base",
            "//third_party/sealedcomputing/wasm3:bytestring",
            "//third_party/sealedcomputing/wasm3:logging",
            "//third_party/sealedcomputing/wasm3:socket",
            "//third_party/sealedcomputing/wasm3:status",
            "//third_party/sealedcomputing/wasm3:statusor",
        ],
        alwayslink = 1,
    )
