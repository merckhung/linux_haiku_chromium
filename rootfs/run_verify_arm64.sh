#!/bin/bash
# SPDX-License-Identifier: MIT
set -e

KERNEL="$1"
INITRD="$2"
RUNNER="$3"

mkdir -p docs/screenshots

python3 "${RUNNER}" \
    --arch="arm64" \
    --kernel="${KERNEL}" \
    --initrd="${INITRD}" \
    --memory="4096M" \
    --key-delay=0.15 \
    --cmd-delay=3.0 \
    --settle=15.0 \
    --cmdline="console=ttyAMA0 root=/dev/ram0 rw haiku.mode=gui quiet panic=1" \
    --expect="[init] Launching Haiku GUI subsystem..." \
    --expect="[GlobalFontManager] _ScanFontsIfNecessary() done." \
    --expect="[app_server/LinuxFB]" \
    --expect="[init] Forked Deskbar" \
    --expect="[init] Forked Terminal" \
    --expect="[WidgetGallery] Full-blown Gallery window launched" \
    --screenshot-ppm="docs/screenshots/haiku-gui-arm64.ppm" \
    --screenshot-png="docs/screenshots/haiku-gui-arm64.png" \
    --timeout=300

if [ -n "${BUILD_WORKSPACE_DIRECTORY}" ]; then
    mkdir -p "${BUILD_WORKSPACE_DIRECTORY}/docs/screenshots"
    cp -f docs/screenshots/haiku-gui-arm64.* "${BUILD_WORKSPACE_DIRECTORY}/docs/screenshots/" 2>/dev/null || true
fi
