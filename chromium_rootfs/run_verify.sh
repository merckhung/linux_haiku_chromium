#!/bin/bash
# SPDX-License-Identifier: MIT
# Tier 3 (x86_64, KVM): Haiku GUI + WidgetGallery + pre-built Chromium.
# Passes only if Chromium reports the www.yahoo.com page title AND the page
# area of the screenshot shows rendered content (>= 2000 distinct colours).
set -e

KERNEL="$1"
INITRD="$2"
RUNNER="$3"

mkdir -p docs/screenshots

python3 "${RUNNER}" \
    --arch="x86_64" \
    --kernel="${KERNEL}" \
    --initrd="${INITRD}" \
    --memory="8192M" \
    --settle=5.0 \
    --cmdline="console=ttyS0 haiku.mode=chromium quiet panic=1" \
    --expect="[init] Launching Haiku GUI subsystem..." \
    --expect="[init] Launching Haiku Deskbar (Taskbar)..." \
    --expect="[init] Launching Haiku GUI Widget Gallery..." \
    --expect="[init] Launching 64-bit Chromium on Haiku GUI..." \
    --expect="[Chromium/OzoneHaiku] Initialized BApplication" \
    --expect="[Chromium/OzoneHaiku] Window created:" \
    --wait-for="[Chromium/OzoneHaiku] Window title: Yahoo" \
    --wait-for-timeout=180 \
    --content-rect="640,280,1230,670" \
    --content-min-colors=2000 \
    --content-timeout=120 \
    --screenshot-ppm="docs/screenshots/haiku-chromium-x86_64.ppm" \
    --screenshot-png="docs/screenshots/haiku-chromium-x86_64.png" \
    --timeout=120

if [ -n "${BUILD_WORKSPACE_DIRECTORY}" ]; then
    mkdir -p "${BUILD_WORKSPACE_DIRECTORY}/docs/screenshots"
    cp -f docs/screenshots/haiku-chromium-x86_64.png "${BUILD_WORKSPACE_DIRECTORY}/docs/screenshots/"
    cp -f docs/screenshots/haiku-terminal-proof-x86_64.png "${BUILD_WORKSPACE_DIRECTORY}/docs/screenshots/"
fi
