#!/bin/bash
# SPDX-License-Identifier: MIT
set -e

KERNEL="$1"
INITRD="$2"
RUNNER="$3"

python3 "${RUNNER}" \
    --kernel="${KERNEL}" \
    --initrd="${INITRD}" \
    --cmdline="console=ttyS0 haiku.mode=tier1 quiet panic=1" \
    --expect="=== ALL 6 HAIKU LIBROOT SUITES PASSED ===" \
    --expect="[PASS] Tier 1 verification passed!" \
    --timeout=30
