#!/bin/bash
# SPDX-License-Identifier: MIT
# Tier 3 (ARM64, QEMU virt cortex-a72 under TCG emulation): Haiku GUI +
# WidgetGallery + pre-built ARM64 Chromium. Passes only if Chromium reports
# the www.yahoo.com page title AND the page area of the screenshot shows
# rendered content. Emulation is slow, so timeouts are generous.
set -e

KERNEL="$1"
INITRD="$2"
RUNNER="$3"

mkdir -p docs/screenshots

python3 "${RUNNER}" \
    --arch="arm64" \
    --kernel="${KERNEL}" \
    --initrd="${INITRD}" \
    --memory="6144M" \
    --settle=30.0 \
    --key-delay=0.15 \
    --cmd-delay=3.0 \
    --cmdline="console=ttyAMA0 root=/dev/ram0 rw haiku.mode=chromium quiet panic=1" \
    --expect="[init] Launching Haiku GUI subsystem..." \
    --expect="[init] Launching Haiku Deskbar (Taskbar)..." \
    --expect="[init] Launching Haiku GUI Widget Gallery..." \
    --expect="[init] Launching 64-bit Chromium on Haiku GUI..." \
    --expect="[Chromium/OzoneHaiku] Initialized BApplication" \
    --expect="[Chromium/OzoneHaiku] Window created:" \
    --wait-for="[Chromium/OzoneHaiku] Window title: Yahoo" \
    --wait-for-timeout=900 \
    --content-rect="640,280,1230,670" \
    --content-min-colors=2000 \
    --content-timeout=600 \
    --input-timeout=180 \
    --screenshot-ppm="docs/screenshots/haiku-chromium-arm64.ppm" \
    --screenshot-png="docs/screenshots/haiku-chromium-arm64.png" \
    --timeout=600

if [ -n "${BUILD_WORKSPACE_DIRECTORY}" ]; then
    mkdir -p "${BUILD_WORKSPACE_DIRECTORY}/docs/screenshots"
    cp -f docs/screenshots/haiku-chromium-arm64.png "${BUILD_WORKSPACE_DIRECTORY}/docs/screenshots/"
    cp -f docs/screenshots/haiku-terminal-proof-arm64.png "${BUILD_WORKSPACE_DIRECTORY}/docs/screenshots/"
fi
