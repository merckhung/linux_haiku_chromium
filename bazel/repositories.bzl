# SPDX-License-Identifier: MIT
"""Repository rules for linux_haiku_chromium: pinned HaikuOS, Linux, and dependencies."""

def _run(ctx, args):
    result = ctx.execute(args, timeout = 3600, quiet = True)
    if result.return_code:
        fail("%s\nstdout:\n%s\nstderr:\n%s" % (args, result.stdout, result.stderr))
    return result

def _haiku_repository_impl(ctx):
    cache = ctx.os.environ.get("HAIKU_GIT_CACHE", "/tmp/haiku-git-cache")
    if cache and ctx.path(cache + "/headers").exists:
        # Cache exists with required files checked out: symlink the sparse directories
        for d in ["headers", "src", "data", "build"]:
            if ctx.path(cache + "/" + d).exists:
                ctx.symlink(cache + "/" + d, d)
    else:
        git = ctx.which("git")
        if not git:
            fail("Git must be installed and on PATH")
        clone_cmd = [str(git), "clone", "--depth=1", "--filter=blob:none", "--sparse", ctx.attr.remote, "."]
        _run(ctx, clone_cmd)
        sparse_dirs = [
            "headers",
            "build/config_headers",
            "src/servers/app",
            "src/kits",
            "src/libs/agg",
            "src/libs/linprog",
            "src/system/kernel/messaging",
            "src/apps/deskbar",
            "src/apps/terminal",
            "src/apps/aboutsystem",
            "data",
        ]
        _run(ctx, [str(git), "sparse-checkout", "set"] + sparse_dirs)

    git = ctx.which("git")
    if not git:
        fail("Git must be installed and on PATH")
    checkout = cache if cache and ctx.path(cache + "/headers").exists else "."
    actual = _run(ctx, [str(git), "-C", checkout, "rev-parse", "HEAD"]).stdout.strip()
    if actual != ctx.attr.commit:
        if checkout != ".":
            fail("HAIKU_GIT_CACHE must be checked out at " + ctx.attr.commit)
        _run(ctx, [str(git), "fetch", "--depth=1", "origin", ctx.attr.commit])
        _run(ctx, [str(git), "checkout", "--detach", "FETCH_HEAD"])

    # Expose BUILD.bazel with cc_library for headers and filegroups for sources
    ctx.file("BUILD.bazel", """# SPDX-License-Identifier: MIT
load("@rules_cc//cc:defs.bzl", "cc_library")
package(default_visibility = ["//visibility:public"])

cc_library(
    name = "headers",
    hdrs = glob([
        "headers/**",
        "build/config_headers/**",
        "src/kits/**/*.h",
        "src/servers/app/**/*.h",
    ]),
    includes = [
        "headers",
        "headers/os",
        "headers/os/kernel",
        "headers/os/support",
        "headers/os/storage",
        "headers/os/app",
        "headers/os/interface",
        "headers/os/game",
        "headers/os/mail",
        "headers/os/locale",
        "headers/os/media",
        "headers/os/print",
        "headers/os/package",
        "headers/os/device",
        "headers/os/translation",
        "headers/os/drivers",
        "headers/os/add-ons/graphics",
        "headers/os/add-ons/registrar",
        "headers/private",
        "headers/private/support",
        "headers/private/interface",
        "headers/private/app",
        "headers/private/input",
        "headers/private/storage",
        "headers/private/shared",
        "headers/private/kernel",
        "headers/private/locale",
        "headers/private/media",
        "headers/private/print",
        "headers/private/package",
        "headers/private/device",
        "headers/private/libroot",
        "headers/private/system",
        "headers/private/tracker",
        "headers/private/textencoding",
        "headers/private/graphics",
        "headers/private/graphics/common",
        "headers/private/graphics/vesa",
        "headers/libs/agg",
        "headers/libs/linprog",
        "headers/config",
        "headers/build/host/linux",
        "build/config_headers",
        "src/kits/support",
        "src/kits/app",
        "src/kits/storage",
        "src/kits/storage/mime",
        "src/kits/storage/sniffer",
        "src/kits/interface",
        "src/kits/interface/layouter",
        "src/kits/interface/textview_support",
        "src/kits/tracker",
        "src/servers/app",
        "src/servers/app/drawing",
        "src/servers/app/drawing/Painter",
        "src/servers/app/drawing/interface/remote",
        "src/servers/app/decorator",
        "src/servers/app/font",
        "src/servers/app/stackandtile",
    ] + select({
        "@platforms//cpu:aarch64": [
            "headers/private/system/arch/arm64",
            "headers/private/kernel/arch/arm64",
            "headers/os/arch/arm64",
        ],
        "//conditions:default": [
            "headers/private/system/arch/x86_64",
            "headers/private/system/arch/x86",
            "headers/private/kernel/arch/x86_64",
            "headers/private/kernel/arch/x86",
            "headers/os/arch/x86_64",
        ],
    }),
)

filegroup(name = "header_files", srcs = glob(["headers/**"]))
filegroup(name = "support_sources", srcs = glob(["src/kits/support/*.cpp"]))
filegroup(name = "app_sources", srcs = glob(["src/kits/app/*.cpp"], exclude = [
    "src/kits/app/Looper.cpp",
    "src/kits/app/AppMisc.cpp",
    "src/kits/app/Application.cpp",
    "src/kits/app/MessageRunner.cpp",
]))
filegroup(name = "storage_sources", srcs = glob([
    "src/kits/storage/*.cpp",
    "src/kits/storage/mime/*.cpp",
    "src/kits/storage/sniffer/*.cpp",
]))
filegroup(name = "interface_sources", srcs = glob(["src/kits/interface/**/*.cpp"]))
filegroup(name = "shared_sources", srcs = [
    "src/kits/shared/DragTrackingFilter.cpp",
    "src/kits/shared/LongAndDragTrackingFilter.cpp",
    "src/kits/shared/HSL.cpp",
    "src/kits/shared/HashString.cpp",
    "src/kits/shared/CalendarView.cpp",
    "src/kits/shared/ColorItem.cpp",
    "src/kits/shared/ColorListView.cpp",
    "src/kits/shared/ColorPreview.cpp",
    "src/kits/shared/Keymap.cpp",
    "src/kits/shared/NaturalCompare.cpp",
    "src/kits/shared/BitmapButton.cpp",
])
filegroup(name = "messaging_sources", srcs = ["src/system/kernel/messaging/KMessage.cpp"])
filegroup(name = "kits_sources", srcs = glob(["src/kits/**"]))
filegroup(
    name = "app_server_sources",
    srcs = glob([
        "src/servers/app/*.cpp",
        "src/servers/app/drawing/*.cpp",
        "src/servers/app/drawing/Painter/*.cpp",
        "src/servers/app/drawing/Painter/bitmap_painter/*.cpp",
        "src/servers/app/drawing/Painter/drawing_modes/*.cpp",
        "src/servers/app/decorator/*.cpp",
        "src/servers/app/font/*.cpp",
        "src/servers/app/stackandtile/*.cpp",
    ], exclude = [
        "src/servers/app/AppServer.cpp",
        "src/servers/app/Desktop.cpp",
        "src/servers/app/CursorData.cpp",
        "src/servers/app/TestServerLoopAdapter.cpp",
        "src/servers/app/font/GlobalFontManager.cpp",
    ]),
)
cc_library(
    name = "app_server_headers",
    hdrs = glob([
        "src/servers/app/**/*.h",
    ]) + [
        "src/servers/app/CursorData.cpp",
    ],
)
filegroup(name = "agg_sources", srcs = glob(["src/libs/agg/src/*.cpp"]))
filegroup(name = "linprog_sources", srcs = glob(["src/libs/linprog/*.cpp"]))
cc_library(name = "linprog_headers", hdrs = glob(["src/libs/linprog/*.h"]))
filegroup(name = "deskbar_sources", srcs = glob(["src/apps/deskbar/*.cpp", "src/apps/deskbar/*.h"]))
filegroup(name = "textencoding_sources", srcs = glob(["src/kits/textencoding/*.cpp"]))
filegroup(name = "terminal_sources", srcs = glob(["src/apps/terminal/*.cpp", "src/apps/terminal/*.c", "src/apps/terminal/*.h"]))
filegroup(name = "aboutsystem_sources", srcs = glob(["src/apps/aboutsystem/**"]))
filegroup(name = "data", srcs = glob(["data/**"]))

filegroup(name = "all_sources", srcs = glob(["**"], exclude = ["BUILD.bazel", ".git/**"]))
""")
    ctx.file("COMMIT", ctx.attr.commit + "\n")

haiku_repository = repository_rule(
    implementation = _haiku_repository_impl,
    attrs = {
        "remote": attr.string(mandatory = True),
        "commit": attr.string(mandatory = True),
    },
    environ = ["HAIKU_GIT_CACHE"],
)

def _linux_kernel_repository_impl(ctx):
    src_dir = ctx.os.environ.get("LINUX_SRC", "")
    if not src_dir or not ctx.path(src_dir).exists:
        fail("Linux kernel source directory not found at: %s. Set LINUX_SRC environment variable." % src_dir)

    ctx.file("source_path.bzl", 'KERNEL_SOURCE_PATH = "%s"\n' % src_dir)
    ctx.file("BUILD.bazel", """# SPDX-License-Identifier: GPL-2.0-only
package(default_visibility = ["//visibility:public"])
exports_files(["source_path.bzl"])
""")

linux_kernel_repository = repository_rule(
    implementation = _linux_kernel_repository_impl,
    environ = ["LINUX_SRC"],
)

def _release_repository_impl(ctx):
    ctx.download_and_extract(
        url = ctx.attr.urls,
        sha256 = ctx.attr.sha256,
        stripPrefix = ctx.attr.strip_prefix,
    )
    for patch in ctx.attr.patches:
        ctx.patch(ctx.path(patch), strip = 1)
    if ctx.attr.build_file:
        ctx.symlink(ctx.attr.build_file, "BUILD.bazel")
    else:
        ctx.file("BUILD.bazel", """# SPDX-License-Identifier: MIT
package(default_visibility = ["//visibility:public"])
exports_files(["RELEASE"])
filegroup(name = "sources", srcs = glob(["**"], exclude = ["BUILD.bazel"]))
""")
    ctx.file("RELEASE", ctx.attr.strip_prefix + "\n")

release_repository = repository_rule(
    implementation = _release_repository_impl,
    attrs = {
        "urls": attr.string_list(mandatory = True),
        "sha256": attr.string(mandatory = True),
        "strip_prefix": attr.string(mandatory = True),
        "patches": attr.label_list(allow_files = True),
        "build_file": attr.label(allow_single_file = True),
    },
)
