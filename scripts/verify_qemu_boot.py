#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Automated headless QEMU boot and verification runner.
Verifies kernel boot, init execution, Haiku libroot suites, and system status.
"""

import argparse
from datetime import datetime, timezone
import hashlib
import os
import select
import subprocess
import sys
import time


def run_qemu_test(args):
    kernel_path = os.path.abspath(args.kernel)
    initrd_path = os.path.abspath(args.initrd)

    if not os.path.exists(kernel_path):
        sys.exit(f"Error: kernel {kernel_path} not found")
    if not os.path.exists(initrd_path):
        sys.exit(f"Error: initrd {initrd_path} not found")

    print(f"Verification started UTC: {datetime.now(timezone.utc).isoformat()}")
    for label, path in (("kernel", kernel_path), ("initrd", initrd_path)):
        digest = hashlib.sha256()
        with open(path, "rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        print(f"Artifact {label} sha256: {digest.hexdigest()}", flush=True)

    if args.arch in ("arm64", "aarch64"):
        cmd = [
            "qemu-system-aarch64",
            "-M", "virt",
            "-cpu", "cortex-a72",
            "-kernel", kernel_path,
            "-initrd", initrd_path,
            "-m", "1024M",
            "-smp", "2",
            "-nographic",
            "-serial", "mon:stdio",
            "-append", args.cmdline,
            "-no-reboot",
        ]
    else:
        cmd = [
            "qemu-system-x86_64",
            "-kernel", kernel_path,
            "-initrd", initrd_path,
            "-m", "512M",
            "-smp", "2",
            "-nographic",
            "-serial", "mon:stdio",
            "-append", args.cmdline,
            "-no-reboot",
        ]

        # Check for KVM
        if os.path.exists("/dev/kvm") and os.access("/dev/kvm", os.R_OK | os.W_OK):
            cmd.extend(["-enable-kvm", "-cpu", "host"])
        else:
            cmd.extend(["-cpu", "max"])

    print(f"[*] Launching QEMU: {' '.join(cmd)}")
    start_time = time.time()
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        stdin=subprocess.PIPE,
        bufsize=0,
    )

    passed_milestones = set()
    required_milestones = set(args.expect or [])

    output_lines = []
    success = False

    try:
        while time.time() - start_time < args.timeout:
            ready, _, _ = select.select([proc.stdout], [], [], 0.1)
            if not ready:
                if proc.poll() is not None:
                    break
                continue
            chunk = os.read(proc.stdout.fileno(), 65536)
            if not chunk:
                if proc.poll() is not None:
                    break
                continue
            line = chunk.decode("utf-8", errors="replace")
            print(line, end="", flush=True)
            output_lines.append(line)
            observed = "".join(output_lines)

            for req in list(required_milestones):
                if req in observed:
                    print(f"\n[+] Verified milestone: '{req}'")
                    passed_milestones.add(req)

            if required_milestones and passed_milestones == required_milestones:
                print("\n[+] All required verification milestones observed!")
                success = True
                break

            # If no specific milestones given, check for standard pass phrase
            if not required_milestones and "[PASS] Tier 1 verification passed!" in observed:
                print("\n[+] Tier 1 verification passed!")
                success = True
                break

    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    elapsed = time.time() - start_time
    print(f"\n[*] Execution finished in {elapsed:.2f} seconds.")

    if args.log:
        os.makedirs(os.path.dirname(os.path.abspath(args.log)), exist_ok=True)
        with open(args.log, "w") as f:
            f.writelines(output_lines)

    if not success and required_milestones:
        missing = required_milestones - passed_milestones
        print(f"[-] FAILED: Missing milestones: {missing}")
        sys.exit(1)
    elif not success:
        print("[-] FAILED: Verification criteria not met.")
        sys.exit(1)

    print("[+] QEMU headless verification SUCCESSFUL.")


def main():
    parser = argparse.ArgumentParser(description="QEMU verification runner")
    parser.add_argument("--arch", default="x86_64", help="Architecture: x86_64 or arm64")
    parser.add_argument("--kernel", required=True, help="Path to bzImage / Image")
    parser.add_argument("--initrd", required=True, help="Path to initramfs.cpio.gz")
    parser.add_argument("--cmdline", default="console=ttyS0 haiku.mode=tier1 quiet panic=1", help="Kernel cmdline")
    parser.add_argument("--timeout", type=int, default=30, help="Timeout in seconds")
    parser.add_argument("--expect", action="append", help="Expected string in output (repeatable)")
    parser.add_argument("--log", help="Output log path")
    args = parser.parse_args()

    run_qemu_test(args)


if __name__ == "__main__":
    main()
