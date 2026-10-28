# SPDX-License-Identifier: MIT
"""`arm64_files`: builds its `srcs` for the ARM64 Linux platform.

This lets a normal (x86_64-host) build such as
`bazel build //chromium_rootfs:image_arm64` cross-compile the Haiku userland
from source with the aarch64-linux-gnu toolchain, without any extra flags.
"""

def _arm64_transition_impl(settings, attr):
    return {"//command_line_option:platforms": [str(Label("//bazel/platforms:linux_arm64"))]}

_arm64_transition = transition(
    implementation = _arm64_transition_impl,
    inputs = [],
    outputs = ["//command_line_option:platforms"],
)

def _arm64_files_impl(ctx):
    files = []
    for src in ctx.attr.srcs:
        files.extend(src[DefaultInfo].files.to_list())
    if ctx.attr.single:
        if len(files) != 1:
            fail("%s: expected exactly one file, got %s" % (ctx.label, files))
        out = ctx.actions.declare_file(ctx.label.name)
        ctx.actions.symlink(output = out, target_file = files[0], is_executable = True)
        return [DefaultInfo(files = depset([out]))]
    return [DefaultInfo(files = depset(files))]

arm64_files = rule(
    implementation = _arm64_files_impl,
    attrs = {
        "srcs": attr.label_list(cfg = _arm64_transition, allow_files = True),
        "single": attr.bool(default = True),
    },
)
