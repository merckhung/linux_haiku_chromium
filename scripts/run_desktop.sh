#!/bin/bash
# SPDX-License-Identifier: MIT
set -e

# Run the 64-bit Linux + Haiku GUI + Chromium desktop directly on the local display.
#   x86_64: QEMU + KVM, kernel bzImage.
#   ARM64 : QEMU virt (cortex-a72, TCG emulation -- slow), kernel Image.
#           Selected automatically when the initrd name contains "arm64",
#           or explicitly with ARCH=arm64.

if [ -n "${1:-}" ] && [ -f "$1" ]; then
    KERNEL="$(realpath "$1")"
else
    KERNEL=""
fi

if [ -n "${2:-}" ] && [ -f "$2" ]; then
    INITRD="$(realpath "$2")"
else
    INITRD=""
fi

REPO_ROOT="${BUILD_WORKSPACE_DIRECTORY:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"
cd "${REPO_ROOT}"

KERNEL="${KERNEL:-bazel-bin/kernel/bzImage}"
INITRD="${INITRD:-bazel-bin/rootfs/initramfs.cpio.gz}"

if [ ! -f "${KERNEL}" ] || [ ! -f "${INITRD}" ]; then
    echo "[*] Building Linux kernel and rootfs..."
    bazel --output_base=/tmp/lhc_bz build //kernel:bzImage //rootfs:image
    KERNEL="$(bazel --output_base=/tmp/lhc_bz info bazel-bin)/kernel/bzImage"
    INITRD="$(bazel --output_base=/tmp/lhc_bz info bazel-bin)/rootfs/initramfs.cpio.gz"
fi

# Detect execution mode based on rootfs image name
MODE="gui"
if [[ "${INITRD}" == *"chromium"* ]]; then
    MODE="chromium"
fi
if [ -n "${HAIKU_MODE:-}" ]; then
    MODE="${HAIKU_MODE}"
fi

ARCH="${ARCH:-x86_64}"
if [[ "${INITRD}" == *"arm64"* ]]; then
    ARCH="arm64"
fi

export DISPLAY="${DISPLAY:-:0}"
DISPLAY_ARGS=()
if [ -n "${WAYLAND_DISPLAY}" ] || [ -n "${DISPLAY}" ]; then
    DISPLAY_ARGS+=("-display" "gtk,gl=off")
else
    echo "[!] No DISPLAY or WAYLAND_DISPLAY detected. Falling back to SDL/default."
    DISPLAY_ARGS+=("-display" "default")
fi

KVM_ARGS=("-cpu" "max")
if [ -e /dev/kvm ] && [ -r /dev/kvm ] && [ -w /dev/kvm ]; then
    KVM_ARGS=("-enable-kvm" "-cpu" "host")
fi

echo "================================================================="
echo "=== Starting 64-bit Linux + Haiku GUI Desktop                 ==="
echo "================================================================="
echo "[*] Arch:         ${ARCH}"
echo "[*] Mode:         ${MODE}"
echo "[*] Kernel:       ${KERNEL}"
echo "[*] Rootfs:       ${INITRD}"
echo "[*] Display:      ${WAYLAND_DISPLAY:-${DISPLAY:-default}}"
echo "[*] Window size:  1280x800"
echo "================================================================="

if [ "${ARCH}" = "arm64" ]; then
    echo "[*] ARM64 is emulated (no KVM): expect Chromium to need a few minutes to load pages."
    exec qemu-system-aarch64 \
        -M virt \
        -cpu cortex-a72 \
        -smp 8 \
        -m 6144M \
        -kernel "${KERNEL}" \
        -initrd "${INITRD}" \
        -device virtio-gpu-pci \
        -device virtio-tablet-pci \
        -device virtio-keyboard-pci \
        -netdev user,id=net0 \
        -device virtio-net-pci,netdev=net0 \
        -name "Haiku ${ARCH} ${MODE}" \
        -qmp "unix:${QMP_SOCKET:-/tmp/lhc-${ARCH}-${MODE}.sock},server=on,wait=off" \
        "${DISPLAY_ARGS[@]}" \
        -serial mon:stdio \
        -append "console=ttyAMA0 root=/dev/ram0 rw video=Virtual-1:1280x800@60 haiku.mode=${MODE} quiet panic=1"
fi

exec qemu-system-x86_64 \
    -kernel "${KERNEL}" \
    -initrd "${INITRD}" \
    -m 8192M \
    -smp 4 \
    -vga std \
    -device virtio-tablet-pci \
    -device virtio-keyboard-pci \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -name "Haiku ${ARCH} ${MODE}" \
    -qmp "unix:${QMP_SOCKET:-/tmp/lhc-${ARCH}-${MODE}.sock},server=on,wait=off" \
    "${DISPLAY_ARGS[@]}" \
    -serial mon:stdio \
    -append "console=ttyS0 root=/dev/ram0 rw haiku.mode=${MODE} quiet panic=1" \
    "${KVM_ARGS[@]}"
