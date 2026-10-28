#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Deterministic Rootfs & CPIO Initramfs Builder for linux_haiku_chromium.
Constructs a CPIO (new ASCII 070701) archive and compresses it with gzip.
"""

import argparse
import hashlib
import json
import os
import stat
import struct
import sys
import gzip


class CpioWriter:
    """Writes SVR4 portable format CPIO archive (070701) deterministically."""

    def __init__(self, stream):
        self.stream = stream
        self.ino = 1

    def _pad4(self, n):
        pad = (4 - (n % 4)) % 4
        if pad:
            self.stream.write(b"\0" * pad)

    def write_entry(self, name, mode, content=b"", link_target=None):
        name_bytes = name.encode("utf-8") + b"\0"
        namesize = len(name_bytes)

        if link_target is not None:
            content = link_target.encode("utf-8")
        filesize = len(content)

        # 070701 header (110 bytes)
        # c_magic[6], c_ino[8], c_mode[8], c_uid[8], c_gid[8], c_nlink[8],
        # c_mtime[8], c_filesize[8], c_maj[8], c_min[8], c_rmaj[8], c_rmin[8],
        # c_namesize[8], c_check[8]
        header = (
            f"070701"
            f"{self.ino:08X}"
            f"{mode:08X}"
            f"{0:08X}"  # uid 0
            f"{0:08X}"  # gid 0
            f"{1:08X}"  # nlink
            f"{0:08X}"  # mtime 0 for reproducibility
            f"{filesize:08X}"
            f"{0:08X}"  # dev_maj
            f"{0:08X}"  # dev_min
            f"{0:08X}"  # rdev_maj
            f"{0:08X}"  # rdev_min
            f"{namesize:08X}"
            f"{0:08X}"  # check
        ).encode("ascii")

        self.stream.write(header)
        self.stream.write(name_bytes)
        self._pad4(110 + namesize)

        if filesize > 0:
            self.stream.write(content)
            self._pad4(filesize)

        self.ino += 1

    def finish(self):
        # TRAILER!!!
        self.write_entry("TRAILER!!!", 0)


def _require(src_path, spec):
    if not os.path.exists(src_path):
        sys.exit(f"[-] build_rootfs: missing input for '{spec}': {src_path}")


def build_rootfs(args):
    manifest = {}

    # In-memory or temporary output
    out_path = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)

    with gzip.GzipFile(out_path, "wb", mtime=0, compresslevel=1) as gz:
        cpio = CpioWriter(gz)

        # Standard directories
        standard_dirs = [
            "bin", "sbin", "usr", "usr/bin", "usr/sbin", "lib", "lib64", "usr/lib",
            "etc", "proc", "sys", "dev", "dev/pts", "dev/shm", "tmp", "run", "var",
            "boot", "boot/home", "boot/home/Desktop",
            "boot/system", "boot/system/bin", "boot/system/lib",
            "boot/system/servers", "boot/system/apps", "boot/system/data",
            "boot/system/data/fonts", "boot/system/data/artwork", "boot/system/settings"
        ]
        created_dirs = set(standard_dirs)
        for d in sorted(standard_dirs):
            cpio.write_entry(d, stat.S_IFDIR | 0o755)

        def ensure_parent_dirs(target_path):
            parts = target_path.strip("/").split("/")[:-1]
            cur = ""
            for p in parts:
                cur = f"{cur}/{p}" if cur else p
                if cur not in created_dirs:
                    created_dirs.add(cur)
                    cpio.write_entry(cur, stat.S_IFDIR | 0o755)

        # Standard loader symlinks
        cpio.write_entry("lib/ld-linux-x86-64.so.2", stat.S_IFLNK | 0o777, link_target="/lib64/ld-linux-x86-64.so.2")
        cpio.write_entry("lib64/ld-linux-aarch64.so.1", stat.S_IFLNK | 0o777, link_target="/lib/ld-linux-aarch64.so.1")

        # Device nodes symlinks or devtmpfs mounts
        # /init (PID 1)
        if args.init:
            _require(args.init, "--init")
        if args.init and os.path.exists(args.init):
            with open(args.init, "rb") as f:
                content = f.read()
            cpio.write_entry("init", stat.S_IFREG | 0o755, content)
            manifest["init"] = {
                "size": len(content),
                "sha256": hashlib.sha256(content).hexdigest()
            }

        # /bin/busybox
        if args.busybox:
            _require(args.busybox, "--busybox")
        if args.busybox and os.path.exists(args.busybox):
            with open(args.busybox, "rb") as f:
                content = f.read()
            cpio.write_entry("bin/busybox", stat.S_IFREG | 0o755, content)
            manifest["bin/busybox"] = {
                "size": len(content),
                "sha256": hashlib.sha256(content).hexdigest()
            }
            # Symlink /bin/sh -> busybox
            cpio.write_entry("bin/sh", stat.S_IFLNK | 0o777, link_target="busybox")
            # Common shell utilities
            for applet in [
                "ls", "cat", "echo", "ps", "kill", "mkdir", "mount", "umount",
                "sleep", "uname", "id", "ifconfig", "ip", "route", "ping",
                "wget", "nslookup", "udhcpc", "nc", "netstat", "df", "free",
                "ln", "rm", "cp", "grep", "awk", "sed", "head", "tail", "dmesg"
            ]:
                cpio.write_entry(f"bin/{applet}", stat.S_IFLNK | 0o777, link_target="busybox")

        # Tests
        if args.tests:
            for test_spec in args.tests:
                if ":" in test_spec:
                    target_name, src_path = test_spec.split(":", 1)
                else:
                    src_path = test_spec
                    target_name = f"bin/{os.path.basename(src_path)}"

                if os.path.exists(src_path):
                    with open(src_path, "rb") as f:
                        content = f.read()
                    cpio.write_entry(target_name, stat.S_IFREG | 0o755, content)
                    manifest[target_name] = {
                        "size": len(content),
                        "sha256": hashlib.sha256(content).hexdigest()
                    }

        # Shared libraries
        if args.libs:
            for lib_spec in args.libs:
                if ":" in lib_spec:
                    target_name, src_path = lib_spec.split(":", 1)
                else:
                    src_path = lib_spec
                    target_name = f"lib/{os.path.basename(src_path)}"

                target_name = os.path.normpath(target_name).lstrip("/")
                _require(src_path, lib_spec)
                if os.path.isdir(src_path):
                    for fname in os.listdir(src_path):
                        fpath = os.path.join(src_path, fname)
                        sub_target = os.path.normpath(os.path.join(target_name, fname)).lstrip("/")
                        if os.path.islink(fpath):
                            linkto = os.readlink(fpath)
                            cpio.write_entry(sub_target, stat.S_IFLNK | 0o777, link_target=linkto)
                            base = os.path.basename(sub_target)
                            cpio.write_entry(f"boot/system/lib/{base}", stat.S_IFLNK | 0o777, link_target=f"/lib/{base}")
                        elif os.path.isfile(fpath):
                            with open(fpath, "rb") as f:
                                content = f.read()
                            cpio.write_entry(sub_target, stat.S_IFREG | 0o755, content)
                            manifest[sub_target] = {
                                "size": len(content),
                                "sha256": hashlib.sha256(content).hexdigest()
                            }
                            base = os.path.basename(sub_target)
                            cpio.write_entry(f"boot/system/lib/{base}", stat.S_IFLNK | 0o777, link_target=f"/lib/{base}")
                elif os.path.exists(src_path):
                    with open(src_path, "rb") as f:
                        content = f.read()
                    cpio.write_entry(target_name, stat.S_IFREG | 0o755, content)
                    manifest[target_name] = {
                        "size": len(content),
                        "sha256": hashlib.sha256(content).hexdigest()
                    }
                    # Also symlink into /boot/system/lib
                    base = os.path.basename(target_name)
                    cpio.write_entry(f"boot/system/lib/{base}", stat.S_IFLNK | 0o777, link_target=f"/lib/{base}")

        # Haiku GUI servers & apps
        if args.apps:
            for app_spec in args.apps:
                target_name, src_path = app_spec.split(":", 1)
                _require(src_path, app_spec)
                if os.path.exists(src_path):
                    ensure_parent_dirs(target_name)
                    with open(src_path, "rb") as f:
                        content = f.read()
                    cpio.write_entry(target_name, stat.S_IFREG | 0o755, content)
                    manifest[target_name] = {
                        "size": len(content),
                        "sha256": hashlib.sha256(content).hexdigest()
                    }

        # Data files (fonts, configs, assets)
        if args.files:
            for file_spec in args.files:
                target_name, src_path = file_spec.split(":", 1)
                _require(src_path, file_spec)
                if os.path.exists(src_path):
                    ensure_parent_dirs(target_name)
                    with open(src_path, "rb") as f:
                        content = f.read()
                    cpio.write_entry(target_name, stat.S_IFREG | 0o644, content)
                    manifest[target_name] = {
                        "size": len(content),
                        "sha256": hashlib.sha256(content).hexdigest()
                    }

        cpio.finish()

    # Output manifest if requested
    if args.manifest:
        os.makedirs(os.path.dirname(os.path.abspath(args.manifest)), exist_ok=True)
        with open(args.manifest, "w") as mf:
            json.dump(manifest, mf, indent=2)

    print(f"[+] Rootfs image created: {out_path} ({os.path.getsize(out_path)} bytes, {len(manifest)} items)")


def main():
    parser = argparse.ArgumentParser(description="Build initramfs CPIO archive")
    parser.add_argument("--init", help="Path to /init executable")
    parser.add_argument("--busybox", help="Path to busybox executable")
    parser.add_argument("--tests", action="append", help="Target:src test binaries")
    parser.add_argument("--libs", action="append", help="Target:src shared libraries")
    parser.add_argument("--apps", action="append", help="Target:src applications")
    parser.add_argument("--files", action="append", help="Target:src data files")
    parser.add_argument("--out", required=True, help="Output .cpio.gz path")
    parser.add_argument("--manifest", help="Output JSON manifest path")
    args = parser.parse_args()

    build_rootfs(args)


if __name__ == "__main__":
    main()
