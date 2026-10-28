#!/bin/bash
# SPDX-License-Identifier: MIT
set -e

KERNEL="$1"
INITRD="$2"
RUNNER="$3"

mkdir -p docs/screenshots

python3 "${RUNNER}" \
    --kernel="${KERNEL}" \
    --initrd="${INITRD}" \
    --cmdline="console=ttyS0 haiku.mode=gui quiet panic=1" \
    --expect="[init] Launching Haiku GUI subsystem..." \
    --expect="[GlobalFontManager] _ScanFontsIfNecessary() done." \
    --expect="[app_server/LinuxFB]" \
    --expect="[init] Forked Deskbar" \
    --expect="[init] Forked Terminal" \
    --expect="[WidgetGallery] Full-blown Gallery window launched" \
    --screenshot-ppm="docs/screenshots/haiku-gui-x86_64.ppm" \
    --screenshot-png="docs/screenshots/haiku-gui-x86_64.png" \
    --timeout=35

if [ -n "${BUILD_WORKSPACE_DIRECTORY}" ]; then
    mkdir -p "${BUILD_WORKSPACE_DIRECTORY}/docs/screenshots"
    cp -f docs/screenshots/haiku-gui-x86_64.* "${BUILD_WORKSPACE_DIRECTORY}/docs/screenshots/" 2>/dev/null || true
fi
