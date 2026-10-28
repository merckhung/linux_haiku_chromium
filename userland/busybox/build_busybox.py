#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Build wrapper for compiling static 64-bit BusyBox under Bazel.
"""

import argparse
import os
import shutil
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description="Build BusyBox")
    parser.add_argument("--src_dir", required=True, help="Path to BusyBox source tree")
    parser.add_argument("--out", required=True, help="Output path for busybox binary")
    parser.add_argument("--cross_compile", default="", help="Cross compile prefix (e.g. aarch64-linux-gnu-)")
    parser.add_argument("--jobs", default="16", help="Parallel build jobs")
    args = parser.parse_args()

    src_dir = os.path.abspath(args.src_dir)
    out_bin = os.path.abspath(args.out)

    suffix = f"_{args.cross_compile.replace('-', '_').rstrip('_')}" if args.cross_compile else ""
    build_dir = os.path.join(os.path.dirname(out_bin), f"bb_build_work{suffix}")
    os.makedirs(build_dir, exist_ok=True)

    env = os.environ.copy()
    if args.cross_compile:
        env["CROSS_COMPILE"] = args.cross_compile

    # Step 1: defconfig
    print(f"[*] Generating BusyBox defconfig...")
    cmd_config = ["make", f"-C{src_dir}", f"O={build_dir}", "defconfig"]
    res = subprocess.run(cmd_config, env=env)
    if res.returncode != 0:
        sys.exit(f"BusyBox defconfig failed with code {res.returncode}")

    # Step 2: Configure static build and disable obsolete tc
    config_file = os.path.join(build_dir, ".config")
    with open(config_file, "r") as f:
        cfg = f.read()

    cfg = cfg.replace("# CONFIG_STATIC is not set", "CONFIG_STATIC=y")
    cfg = cfg.replace("CONFIG_TC=y", "# CONFIG_TC is not set")
    cfg = cfg.replace("# CONFIG_FEATURE_SH_STANDALONE is not set", "CONFIG_FEATURE_SH_STANDALONE=y")
    cfg = cfg.replace("# CONFIG_FEATURE_PREFER_APPLETS is not set", "CONFIG_FEATURE_PREFER_APPLETS=y")

    with open(config_file, "w") as f:
        f.write(cfg)

    # Step 3: oldconfig
    cmd_oldconfig = ["make", f"-C{src_dir}", f"O={build_dir}", "oldconfig"]
    subprocess.run(cmd_oldconfig, env=env, check=True)

    # Step 4: build busybox
    print(f"[*] Compiling static BusyBox with -j{args.jobs}...")
    cmd_build = ["make", f"-C{src_dir}", f"O={build_dir}", f"-j{args.jobs}", "busybox"]
    subprocess.run(cmd_build, env=env, check=True)

    src_busybox = os.path.join(build_dir, "busybox")
    if not os.path.exists(src_busybox):
        sys.exit(f"Error: {src_busybox} not produced!")

    os.makedirs(os.path.dirname(out_bin), exist_ok=True)
    shutil.copyfile(src_busybox, out_bin)
    os.chmod(out_bin, 0o755)
    print(f"[+] Successfully built {out_bin} ({os.path.getsize(out_bin)} bytes)")


if __name__ == "__main__":
    main()
