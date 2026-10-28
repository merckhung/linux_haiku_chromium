#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Build wrapper for compiling 64-bit Linux kernel bzImage under Bazel.
"""

import argparse
import os
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description="Build Linux kernel image (bzImage/Image)")
    parser.add_argument("--kernel_src", required=True, help="Path to Linux kernel source tree")
    parser.add_argument("--defconfig", required=True, help="Path to defconfig file")
    parser.add_argument("--out", required=True, help="Output path for kernel image")
    parser.add_argument("--arch", default="x86_64", help="Architecture (x86_64 or arm64)")
    parser.add_argument("--jobs", default="16", help="Parallel build jobs")
    args = parser.parse_args()

    kernel_src = os.path.abspath(args.kernel_src)
    defconfig_path = os.path.abspath(args.defconfig)
    out_image = os.path.abspath(args.out)

    build_dir = os.path.join(os.path.dirname(out_image), f"kbuild_work_{args.arch}")
    os.makedirs(build_dir, exist_ok=True)

    # Copy defconfig into build_dir/.config
    dot_config = os.path.join(build_dir, ".config")
    shutil.copyfile(defconfig_path, dot_config)

    env = os.environ.copy()
    env["ARCH"] = args.arch
    # Keep machine identity out of /proc/version and published screenshots.
    env["KBUILD_BUILD_USER"] = "builder"
    env["KBUILD_BUILD_HOST"] = "linux-haiku"
    env["KBUILD_BUILD_TIMESTAMP"] = "Thu Jan  1 00:00:00 UTC 1970"
    env["KBUILD_BUILD_VERSION"] = "1"

    extra_args = []
    if args.arch == "arm64":
        target = "Image"
        src_image = os.path.join(build_dir, "arch", "arm64", "boot", "Image")
        extra_args = ["LLVM=1"]
    else:
        target = "bzImage"
        src_image = os.path.join(build_dir, "arch", "x86", "boot", "bzImage")

    # Step 1: olddefconfig to resolve dependencies and fill in defaults
    print(f"[*] Configuring {args.arch} kernel with {defconfig_path}...")
    cmd_config = [
        "make",
        f"-C{kernel_src}",
        f"O={build_dir}",
        f"ARCH={args.arch}",
    ] + extra_args + ["olddefconfig"]
    res = subprocess.run(cmd_config, env=env)
    if res.returncode != 0:
        sys.exit(f"Kernel olddefconfig failed with code {res.returncode}")

    # Step 2: Build kernel image
    print(f"[*] Building {target} with -j{args.jobs}...")
    cmd_build = [
        "make",
        f"-C{kernel_src}",
        f"O={build_dir}",
        f"ARCH={args.arch}",
        f"-j{args.jobs}",
    ] + extra_args + [target]
    res = subprocess.run(cmd_build, env=env)
    if res.returncode != 0:
        sys.exit(f"Kernel {target} build failed with code {res.returncode}")

    if not os.path.exists(src_image):
        sys.exit(f"Error: {src_image} not produced!")

    os.makedirs(os.path.dirname(out_image), exist_ok=True)
    shutil.copyfile(src_image, out_image)
    print(f"[+] Successfully built {out_image} ({os.path.getsize(out_image)} bytes)")


if __name__ == "__main__":
    main()
