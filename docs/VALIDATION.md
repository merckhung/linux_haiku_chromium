<!-- SPDX-License-Identifier: MIT -->
# Local validation

The Linux kernel and project userland were rebuilt and verified locally on
2026-10-26 and 2026-10-27 (UTC). Both Chromium packages were rebuilt from a
fresh checkout of revision `3bb54dbfa29e4506909fb9d289ad1a28ab76fceb`, version
158.0.8107.0, with the project patch and per-architecture GN arguments.
The [manifest](validation/manifest.json) records current image, package,
screenshot, source-build, gallery and desktop-evidence hashes. Each retained
verifier log includes its actual UTC start time and boot-input hashes.

| Architecture | Tier | Result | Whole run | Yahoo title | Page-area colours | Log |
| --- | --- | --- | --- | --- | --- | --- |
| x86_64 | 1 | PASS | 1.83 s | — | — | [serial log](validation/tier1-x86_64.txt) |
| x86_64 | 2 | PASS | 20.18 s | — | — | [serial log](validation/tier2-x86_64.txt) |
| x86_64 | 3 | PASS | 32.19 s | 4.7 s | 106,101 | [serial log](validation/tier3-x86_64.txt) |
| arm64 | 1 | PASS | 4.87 s | — | — | [serial log](validation/tier1-arm64.txt) |
| arm64 | 2 | PASS | 80.29 s | — | — | [serial log](validation/tier2-arm64.txt) |
| arm64 | 3 | PASS | 183.52 s | 35.0 s | 87,909 | [serial log](validation/tier3-arm64.txt) |

Tier 1 requires all six libroot suites and the overall PASS marker. Its
checks include actual kernel memory and CPU information, team lifetime,
shared areas, IPC, semaphores, thread operations, read/write-lock contention
and initialization-once behavior. Tier 2 requires GUI initialization and
keyboard commands through Terminal, verified by an independent serial-shell
check of a fresh file created by those commands.

Tier 3 also requires the Yahoo title and at least 2,000 distinct colours in
the page rectangle (640,280)–(1230,670). The verifier can scroll the real page
past an empty advertisement slot or a monochrome photograph. It preserves
the Yahoo framebuffer before navigating to a fresh local HTTP page. That
page must report all seven callbacks: ready, click, key, wheel-down,
scrolled, wheel-up and top. Actual scroll position must exceed 100 pixels
and return to zero. A toolbar-tooltip hover precedes Ctrl+L to exercise
keyboard-focus preservation. The HTTP probe handles concurrent connections,
including idle speculative connections from the browser. ARM64 TCG allows
180 seconds per callback; every callback remains mandatory.

The x86_64 runs use KVM; ARM64 uses QEMU TCG emulation. These are integration
smoke tests for the exercised behavior, not complete coverage of every Haiku
or Chromium API. Unsupported compatibility APIs and unknown fields are
listed in [the architecture document](ARCHITECTURE.md).

The Chromium and Terminal screenshots were inspected visually. The Terminal
proof for each architecture belongs to its recorded Tier 3 run. All eight
gallery captures were refreshed on separately booted Tier 2 guests and
individually reviewed across all four tabs on both architectures; their boot,
input, log and image hashes are in [the gallery record](validation/gallery.json).
Every published framebuffer image is 1280×800, captured through QMP and
converted losslessly to PNG. No generated or composited GUI content is used.
Demo controls and progress bars are labelled as demonstrations; the System
Information panel reads kernel, CPU, memory and framebuffer values.

All six variants were also launched together as GTK QEMU windows using
`scripts/run_all_variants.py`. The [desktop record](validation/desktop-status.json)
and its six serial logs retain a live check of QMP peers, process arguments,
current kernel/rootfs hashes, guest architecture, boot milestones and mapped
GTK windows. Tier 1 uses the Serial0 tab. The launcher requests a 1280×800
graphics mode; the four GUI guest framebuffers were additionally checked at
that size. Recreate the desktop evidence while all six guests are running:

```bash
python3 scripts/run_all_variants.py --record-validation
```

The [x86_64](validation/chromium-build-x86_64.json) and
[ARM64](validation/chromium-build-arm64.json) build records link the pinned
Chromium revision, complete applied patch, mirrored Ozone sources, GN
arguments, compiler, dependencies, build-log hashes and all 14 packaged
outputs. Packaging required completed Ninja targets, matching ELF
architectures and exact generated credits. Both browsers link the replaceable
`libffmpeg.so`. The final integrity check read all 14 files back from each
Chromium CPIO archive and matched them to their corresponding build record.
The [notice evidence](validation/source-notices.json) records source notice,
offline-credit and FFmpeg corresponding-source checks. All 14 bundled system
package copyright notices also matched the installed package notices.

The Linux 6.12.93 archive was checked against its kernel.org SHA-256 list:
`492648a87c0b69c5ac7f43be64792b9000e3439550d4e82e4a14710c49094fa3`.
The Haiku checkout matched the commit in `MODULE.bazel`. Kernel build identity
uses generic names and a fixed epoch timestamp; `/proc/version` therefore
does not show the capture date. The logs record real verification dates.
Rebuilt source and output hashes are recorded without claiming that both
architectures reproduce identical binaries across different build paths.

To repeat the evidence and archive checks after running all six verifiers
and recording desktop and gallery evidence:

```bash
python3 scripts/record_validation.py
```

Raw build output and diagnostics remain in the ignored `runtime/` directory.
Published logs use generic workspace labels, omit trailing whitespace and
prefix standalone serial separator banners with `[serial]`. The release audit checks staged
content and materialized LFS payloads for forbidden identities, private paths
and secret patterns; pattern scans supplement manual review.
