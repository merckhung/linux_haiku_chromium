<!-- SPDX-License-Identifier: MIT -->
# Chromium Haiku Ozone Patch & Rebuild Guide

Chromium 158.0.8107.0 is built with the pinned source revision and
[`0001-chromium-ozone-haiku-linux.patch`](0001-chromium-ozone-haiku-linux.patch).
Both architecture packages were rebuilt from a fresh checkout. The
[x86_64 build record](../../../docs/validation/chromium-build-x86_64.json) and
[ARM64 build record](../../../docs/validation/chromium-build-arm64.json) record
the source, dependencies, patch, GN arguments, toolchain and output hashes.
See [local validation](../../../docs/VALIDATION.md) for runtime evidence.
This repository's Bazel build packages Chromium; compile Chromium separately
with the commands below.

| Item | Value |
| --- | --- |
| Chromium revision | `3bb54dbfa29e4506909fb9d289ad1a28ab76fceb` (2026-10-15) |
| Chromium version | `158.0.8107.0` |
| Patch | `0001-chromium-ozone-haiku-linux.patch` — 30 files, +1740 / −26 lines |
| x86_64 GN args | [`args/x86_64.gn`](args/x86_64.gn) (`out/HaikuLinux_x64`) |
| ARM64 GN args | [`args/arm64.gn`](args/arm64.gn) (`out/HaikuLinux_arm64_lean`) |

The two GN arg files are identical except for `target_cpu` (`"x64"` vs
`"arm64"`): Ozone with only the `haiku` and `headless` platforms, no X11,
Wayland, GLib, GTK, D-Bus, CUPS, ALSA, PulseAudio, NSS or udev, and
ANGLE + SwiftShader for GL. FFmpeg is built as a separate `libffmpeg.so` so
its LGPL code can be rebuilt and replaced without relinking Chromium.
Besides that bundled library and glibc, `chrome` needs `libexpat.so.1` and
`libgcc_s.so.1`.

---

## 1. What the patch contains

The patch is a plain `git diff` of the pinned Chromium working tree.

**New Ozone platform** — `ui/ozone/platform/haiku/` (the same files are
mirrored in [`../ozone_haiku/`](../ozone_haiku/) for easy reading):

| File | Role |
| --- | --- |
| `ozone_platform_haiku.{cc,h}` | `ui::OzonePlatform` implementation: creates the window manager, surface factory, screen, input/cursor stubs. |
| `haiku_capi.{cc,h}` | Loads `libbe.so` at runtime with `dlopen()` and resolves the `haiku_ozone_*` C ABI (implemented in this repo by [`userland/haiku_libbe/haiku_ozone_capi.cpp`](../../haiku_libbe/haiku_ozone_capi.cpp)). Chromium therefore has no link-time dependency on Haiku. |
| `haiku_window.{cc,h}` | `ui::PlatformWindow` backed by a real Haiku `BWindow` (yellow tab) via the C ABI. Window events (mouse, wheel, keyboard, resize, activation) are queued by `libbe.so` and drained on Chromium's UI thread: `libbe.so` signals a non-blocking Linux `eventfd`, which `HaikuWindow` watches with `base::CurrentUIThread::WatchFileDescriptor()` (Chromium's epoll message pump). Shortcut key codes use the unmodified Haiku `raw_char`, so control characters such as Ctrl+L retain their letter key. Tooltips use borderless floating Haiku windows with focus disabled; showing a tooltip preserves keyboard focus in its owner. This requires the `haiku_ozone_set_window_type` entry point in the matching `libbe.so`. Wheel notches are scaled to `ui::MouseWheelEvent::kWheelDelta` with explicit `tick_120ths`. |
| `haiku_surface_factory.{cc,h}` | Software (`SurfaceOzoneCanvas`, Skia raster BGRA) and GL (`GLSurfaceEglReadback` over ANGLE/SwiftShader) presentation paths. Both hand finished frames to `haiku_ozone_present_pixels()`, which blits them into a `BBitmap` drawn by `app_server`. |
| `haiku_screen.{cc,h}`, `haiku_window_manager.{cc,h}`, `client_native_pixmap_factory_haiku.{cc,h}`, `BUILD.gn` | Display info, widget registry, pixmap stub, build rules. |

**Changes to existing Chromium files** (build plumbing and making a
GLib/NSS/D-Bus-free Linux build work):
`build/config/ozone.gni`, `ui/ozone/BUILD.gn`, `ui/gl/gl_display.cc`,
`ui/linux/fallback_linux_ui.cc`,
`ui/base/accelerators/global_accelerator_listener/global_accelerator_listener_ozone.cc`,
`components/crash/core/app/crashpad_linux.cc`,
`chrome/browser/chrome_browser_main_linux.cc`,
`chrome/browser/net/profile_network_context_service.cc`,
`chrome/browser/ui/webui/certificate_manager/client_cert_sources.cc`,
`chrome/browser/web_applications/BUILD.gn`,
`chrome/utility/importer/nss_decryptor.{cc,h}`,
`net/cert/cert_verify_proc.cc`, `net/cert/internal/system_trust_store.{cc,h}`.

Run `git apply --stat 0001-chromium-ozone-haiku-linux.patch` for the
per-file breakdown.

Memory queries retain upstream Linux behavior, including zero for a failed
query. The Haiku platform requires successful application initialization and
a valid display-size query; it fails instead of inventing screen dimensions.

---

## 2. Rebuilding Chromium

Prerequisites: `depot_tools` and Chromium's Linux build dependencies (see
<https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md>).
Use a separate source workspace with a generic path, and keep it out of the
release Git index. The two target CPUs in `.gclient` below select the x86_64
and ARM64 build dependencies and sysroots. Remote execution is disabled by
the supplied GN arguments.

```bash
mkdir -p /path/to/chromium-work
cd /path/to/chromium-work
gclient config --name src --custom-var checkout_pgo_profiles=False \
  https://chromium.googlesource.com/chromium/src.git
cat >> .gclient <<'EOF'
target_os = ["linux"]
target_cpu = ["x64", "arm64"]
EOF
gclient sync --no-history --shallow --nohooks \
  --revision src@3bb54dbfa29e4506909fb9d289ad1a28ab76fceb
gclient runhooks
cd src
git rev-parse HEAD # must print the revision above

# Apply the patch (it applies cleanly to the revision above).
git apply --check /path/to/linux_haiku_chromium/userland/chromium/patches/0001-chromium-ozone-haiku-linux.patch
git apply         /path/to/linux_haiku_chromium/userland/chromium/patches/0001-chromium-ozone-haiku-linux.patch

# x86_64
mkdir -p out/HaikuLinux_x64
cp /path/to/linux_haiku_chromium/userland/chromium/patches/args/x86_64.gn out/HaikuLinux_x64/args.gn
gn gen --fail-on-unused-args out/HaikuLinux_x64
autoninja -C out/HaikuLinux_x64 chrome chrome_crashpad_handler \
  libEGL.so libGLESv2.so libvk_swiftshader.so libffmpeg.so

# ARM64 (cross-compiled on an x86_64 host)
mkdir -p out/HaikuLinux_arm64_lean
cp /path/to/linux_haiku_chromium/userland/chromium/patches/args/arm64.gn out/HaikuLinux_arm64_lean/args.gn
gn gen --fail-on-unused-args out/HaikuLinux_arm64_lean
autoninja -C out/HaikuLinux_arm64_lean chrome chrome_crashpad_handler \
  libEGL.so libGLESv2.so libvk_swiftshader.so libffmpeg.so
```

## 3. Updating the pre-built files in this repository

Run the packaging gate from this project, with `depot_tools` on `PATH`:

```bash
cd /path/to/linux_haiku_chromium
python3 scripts/package_chromium.py --source /path/to/chromium-work/src --arch x86_64
python3 scripts/package_chromium.py --source /path/to/chromium-work/src --arch arm64
```

The gate checks the source revision, the complete applied patch, exact GN
arguments, completed Ninja targets, clean pinned Git dependencies, ELF
architectures and generated credits. It strips and replaces all 14 packaged
files only after those checks pass, and writes per-architecture build records
under `docs/validation/`. Preserve build stdout and pass its paths through
repeatable `--build-log` options to retain their hashes as well. Add repeatable
`--forbid PRIVATE_TEXT` options for private identifiers; matched values are
never printed. The final release audit remains required.

Then rebuild and verify the rootfs:

```bash
bazel run //chromium_rootfs:verify         # x86_64 (KVM)
bazel run //chromium_rootfs:verify_arm64   # ARM64 (QEMU TCG emulation, slow)
```

Both verifiers require the `www.yahoo.com` page title and rendered content,
Terminal keyboard execution checked through the serial shell, and a fresh
HTTP page's callbacks for browser keyboard, pointer, wheel and scrolling
events. A screenshot alone cannot satisfy these checks.

The GN argument files enable `generate_about_credits = true` for rebuilds.
Retain the offline notice bundle alongside the binaries as well.
Both rebuilt packages contain the generated credits. See the repository's
[third-party notices](../../../THIRD_PARTY_NOTICES.md) for bundled licenses
and source-availability details.
