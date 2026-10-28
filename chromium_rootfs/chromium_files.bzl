# SPDX-License-Identifier: MIT
"""Helpers listing the pre-built Chromium files packaged into the Tier 3 rootfs."""

CHROMIUM_FILES = [
    "chrome",
    "chrome_crashpad_handler",
    "libEGL.so",
    "libGLESv2.so",
    "libffmpeg.so",
    "libvk_swiftshader.so",
    "vk_swiftshader_icd.json",
    "icudtl.dat",
    "v8_context_snapshot.bin",
    "snapshot_blob.bin",
    "chrome_100_percent.pak",
    "chrome_200_percent.pak",
    "resources.pak",
    "locales/en-US.pak",
]

CHROMIUM_EXECUTABLES = [
    "chrome",
    "chrome_crashpad_handler",
    "libEGL.so",
    "libGLESv2.so",
    "libffmpeg.so",
    "libvk_swiftshader.so",
]

def chromium_srcs(arch):
    return ["//userland/chromium:prebuilt/%s/%s" % (arch, f) for f in CHROMIUM_FILES]

def chromium_args(arch):
    args = []
    for f in CHROMIUM_FILES:
        kind = "--apps" if f in CHROMIUM_EXECUTABLES else "--files"
        args.append('%s="boot/system/apps/%s:$(location //userland/chromium:prebuilt/%s/%s)"' % (kind, f, arch, f))
    return " \\\n            ".join(args)

COMMON_DATA_FILES = """\
            --files="boot/system/data/welcome.html:$(location //userland/chromium:welcome.html)" \\
            --files="etc/ssl/certs/ca-certificates.crt:/etc/ssl/certs/ca-certificates.crt" \\
            --files="boot/system/data/fonts/DejaVuSans.ttf:/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf" \\
            --files="boot/system/data/fonts/DejaVuSans-Bold.ttf:/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" \\
            --files="boot/system/data/fonts/DejaVuSansMono.ttf:/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf" \\
            --files="boot/system/data/fonts/DejaVuSerif.ttf:/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf" \\
            --files=boot/system/data/artwork/haiku_logo.png:$(location @haiku//:data/artwork/HAIKU logo - white on transparent - small.png) \\
            --files=boot/system/data/artwork/haiku_leaf.png:$(location @haiku//:data/artwork/HAIKU Background Leaf - outline.png) \\
"""
