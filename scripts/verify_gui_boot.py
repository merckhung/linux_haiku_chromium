#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""
Headless QEMU verification runner for the Haiku GUI desktop (Tier 2) and the
Haiku GUI + Chromium desktop (Tier 3), for x86_64 (KVM) and ARM64 (QEMU virt).

It boots the kernel + initramfs, waits for the expected serial-console
milestones (and optionally for a page-load marker such as
"[Chromium/OzoneHaiku] Window title: Yahoo"), drives the desktop via QMP input
events (Terminal commands, mouse wheel), and captures framebuffer screenshots.
It exits non-zero if any milestone is missing or the screen is blank.
"""

import argparse
from datetime import datetime, timezone
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
import re
import secrets
import select
import socket
import struct
import subprocess
import sys
import threading
import time
import zlib


class BrowserInputProbe:
    """Serve a fresh page and collect DOM events from the running browser."""

    def __enter__(self):
        self.nonce = secrets.token_hex(12)
        self.events = {name: threading.Event() for name in
                       ("ready", "click", "key", "wheel-down", "scrolled", "wheel-up", "top")}
        probe = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                prefix = f"/{probe.nonce}/"
                if self.path == prefix:
                    page = ("<!doctype html><meta charset=utf-8>"
                            "<title>Haiku browser input verification</title>"
                            "<style>body{height:3000px;background:#edf4ff;color:#123;"
                            "font:28px sans-serif;margin:40px}</style>"
                            "<h1>Browser input verification</h1>"
                            "<p>This page tests real keyboard, mouse and scrolling events.</p>"
                            f"<p>Run token: {probe.nonce}</p>"
                            "<script>const sent=new Set();let moved=false;"
                            "function report(s){if(sent.has(s))return;sent.add(s);"
                            f"fetch('{prefix}event/'+s,{{cache:'no-store'}});}}"
                            "addEventListener('pointerdown',()=>report('click'));"
                            "addEventListener('keydown',e=>{if(e.key==='k')report('key')});"
                            "addEventListener('wheel',e=>report(e.deltaY>0?'wheel-down':'wheel-up'));"
                            "addEventListener('scroll',()=>{if(scrollY>100){moved=true;report('scrolled')}"
                            "else if(moved&&scrollY===0)report('top')});"
                            "addEventListener('load',()=>requestAnimationFrame(()=>"
                            "requestAnimationFrame(()=>report('ready'))));</script>").encode()
                    self.send_response(200)
                    self.send_header("Content-Type", "text/html; charset=utf-8")
                    self.send_header("Content-Length", str(len(page)))
                    self.send_header("Cache-Control", "no-store")
                    self.end_headers()
                    self.wfile.write(page)
                    print("[*] Chromium input probe page served.", flush=True)
                elif self.path.startswith(prefix + "event/"):
                    event = self.path.removeprefix(prefix + "event/")
                    if event not in probe.events:
                        self.send_error(404)
                        return
                    probe.events[event].set()
                    self.send_response(204)
                    self.end_headers()
                else:
                    self.send_error(404)

        # Chromium can open speculative connections without sending a request.
        # An idle connection must not block the page or its DOM event reports.
        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f"http://10.0.2.2:{self.server.server_port}/{self.nonce}/"
        return self

    def __exit__(self, *args):
        self.server.shutdown()
        self.thread.join()
        self.server.server_close()


def ppm_to_png(ppm_path, png_path):
    """Converts a Netpbm P6 PPM image to PNG using pure Python standard library."""
    with open(ppm_path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            raise ValueError(f"Invalid PPM magic: {magic}")
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        parts = line.split()
        if len(parts) == 2:
            w, h = int(parts[0]), int(parts[1])
            max_val = int(f.readline().strip())
        else:
            w = int(parts[0])
            h = int(f.readline().strip())
            max_val = int(f.readline().strip())

        raw = f.read()

    row_bytes = w * 3
    filtered = bytearray()
    for y in range(h):
        filtered.append(0)  # Filter type 0 (None)
        filtered.extend(raw[y * row_bytes : (y + 1) * row_bytes])

    compressed = zlib.compress(bytes(filtered), 9)

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", compressed)
        + chunk(b"IEND", b"")
    )

    os.makedirs(os.path.dirname(os.path.abspath(png_path)), exist_ok=True)
    with open(png_path, "wb") as f:
        f.write(png)
    print(f"[+] Converted {ppm_path} -> {png_path} ({os.path.getsize(png_path)} bytes)")
    print(f"Artifact screenshot {os.path.basename(png_path)} sha256: {hashlib.sha256(png).hexdigest()}")


def analyze_ppm(ppm_path):
    """Analyzes PPM pixel diversity and returns (unique_colors, width, height)."""
    with open(ppm_path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P6":
            return 0, 0, 0
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        parts = line.split()
        if len(parts) == 2:
            w, h = int(parts[0]), int(parts[1])
            f.readline()
        else:
            w = int(parts[0])
            h = int(f.readline().strip())
            f.readline()
        data = f.read()

    unique_colors = set()
    for i in range(0, len(data), 3):
        unique_colors.add(data[i : i + 3])

    return len(unique_colors), w, h


def region_colors(ppm_path, rect):
    """Counts distinct colours inside rect=(x0, y0, x1, y1) of a P6 PPM."""
    with open(ppm_path, "rb") as f:
        if f.readline().strip() != b"P6":
            return 0
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        parts = line.split()
        if len(parts) == 2:
            w, h = int(parts[0]), int(parts[1])
        else:
            w, h = int(parts[0]), int(f.readline().strip())
        f.readline()
        data = f.read()
    x0, y0, x1, y1 = rect
    colors = set()
    for y in range(max(0, y0), min(h, y1)):
        row = data[(y * w + max(0, x0)) * 3:(y * w + min(w, x1)) * 3]
        for i in range(0, len(row), 3):
            colors.add(row[i:i + 3])
    return len(colors)


def run_gui_verification(args):
    kernel_path = os.path.abspath(args.kernel)
    initrd_path = os.path.abspath(args.initrd)
    qmp_sock = f"/tmp/qmp_gui_{os.getpid()}.sock"

    print(f"Verification started UTC: {datetime.now(timezone.utc).isoformat()}")
    for label, path in (("kernel", kernel_path), ("initrd", initrd_path)):
        digest = hashlib.sha256()
        with open(path, "rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        print(f"Artifact {label} sha256: {digest.hexdigest()}", flush=True)

    if os.path.exists(qmp_sock):
        os.unlink(qmp_sock)

    if args.arch in ("arm64", "aarch64"):
        cmd = [
            "qemu-system-aarch64",
            "-M", "virt",
            "-cpu", "cortex-a72",
            "-kernel", kernel_path,
            "-initrd", initrd_path,
            "-m", args.memory,
            "-smp", "8",
            "-device", "virtio-gpu-pci",
            "-device", "virtio-keyboard-pci",
            "-device", "virtio-tablet-pci",
            "-netdev", "user,id=net0",
            "-device", "virtio-net-pci,netdev=net0",
            "-display", "none",
            "-serial", "mon:stdio",
            "-qmp", f"unix:{qmp_sock},server,nowait",
            "-append", args.cmdline,
            "-no-reboot",
        ]
    else:
        cmd = [
            "qemu-system-x86_64",
            "-kernel", kernel_path,
            "-initrd", initrd_path,
            "-m", args.memory,
            "-smp", "4",
            "-vga", "std",
            "-device", "virtio-tablet-pci",
            "-netdev", "user,id=net0",
            "-device", "virtio-net-pci,netdev=net0",
            "-display", "none",
            "-serial", "mon:stdio",
            "-qmp", f"unix:{qmp_sock},server,nowait",
            "-append", args.cmdline,
            "-no-reboot",
        ]
        if os.path.exists("/dev/kvm") and os.access("/dev/kvm", os.R_OK | os.W_OK):
            cmd.extend(["-enable-kvm", "-cpu", "host"])
        else:
            cmd.extend(["-cpu", "max"])

    print(f"[*] Launching QEMU GUI verification: {' '.join(cmd)}")
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
    accumulated_text = ""
    gui_ready = False
    missing_marker = False
    content_failed = False
    stdout_fd = proc.stdout.fileno()

    def record_serial(chunk):
        nonlocal accumulated_text
        text = chunk.decode("utf-8", errors="replace")
        print(text, end="", flush=True)
        accumulated_text += text
        for name, pid in re.findall(
                r'\[init\] Forked (app_server|Deskbar|Terminal|WidgetGallery|chrome) with PID (\d+)',
                accumulated_text):
            if re.search(r'\[init\] Child process ' + pid + r' (?:exited|terminated)', accumulated_text):
                sys.exit(f"GUI verification failed: {name} exited during the run")
        return text

    try:
        # Wait up to timeout while reading serial non-blockingly via select + os.read
        while time.time() - start_time < args.timeout:
            rlist, _, _ = select.select([stdout_fd], [], [], 0.1)
            if not rlist:
                if proc.poll() is not None:
                    break
                continue

            chunk_bytes = os.read(stdout_fd, 4096)
            if not chunk_bytes:
                if proc.poll() is not None:
                    break
                continue

            record_serial(chunk_bytes)

            for req in list(required_milestones):
                if req not in passed_milestones and req in accumulated_text:
                    print(f"\n[+] Verified milestone: '{req}'", flush=True)
                    passed_milestones.add(req)

            if required_milestones and passed_milestones == required_milestones:
                gui_ready = True
                break

        # Optionally wait for an additional marker (e.g. a page-load title).
        if gui_ready and args.wait_for:
            print(f"\n[*] Waiting up to {args.wait_for_timeout}s for marker: '{args.wait_for}'", flush=True)
            marker_deadline = time.time() + args.wait_for_timeout
            while args.wait_for not in accumulated_text and time.time() < marker_deadline:
                rlist, _, _ = select.select([stdout_fd], [], [], 0.2)
                if rlist:
                    chunk_bytes = os.read(stdout_fd, 4096)
                    if chunk_bytes:
                        record_serial(chunk_bytes)
                elif proc.poll() is not None:
                    break
            if args.wait_for in accumulated_text:
                print(f"\n[+] Verified marker: '{args.wait_for}' "
                      f"({time.time() - start_time:.1f}s after QEMU start)", flush=True)
            else:
                print(f"\n[-] Marker not seen: '{args.wait_for}'", flush=True)
                gui_ready = False
                missing_marker = True

        # Give app_server & GUI windows extra time to finish drawing while draining stdout
        print(f"\n[*] Waiting {args.settle}s for app_server and window decor rendering...", flush=True)
        settle_end = time.time() + args.settle
        while time.time() < settle_end:
            rlist, _, _ = select.select([stdout_fd], [], [], 0.1)
            if rlist:
                chunk_bytes = os.read(stdout_fd, 4096)
                if chunk_bytes:
                    record_serial(chunk_bytes)

        # Connect to QMP and perform screendump
        if not os.path.exists(qmp_sock):
            sys.exit("Missing QMP socket; no framebuffer was verified")
        if os.path.exists(qmp_sock):
            print(f"[*] Connecting to QMP socket {qmp_sock}...")
            qmp = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            qmp.settimeout(5.0)
            qmp.connect(qmp_sock)

            qmp_stream = qmp.makefile("rb")
            greeting = json.loads(qmp_stream.readline())
            if "QMP" not in greeting:
                raise RuntimeError("Missing QMP greeting")
            request_id = 0

            def qmp_cmd(obj):
                nonlocal request_id
                request_id += 1
                obj = dict(obj, id=request_id)
                if obj.get("execute") == "screendump":
                    # Never accept a leftover capture if this command fails.
                    path = obj["arguments"]["filename"]
                    if os.path.exists(path):
                        os.unlink(path)
                qmp.sendall((json.dumps(obj) + "\n").encode("utf-8"))
                while True:
                    line = qmp_stream.readline()
                    if not line:
                        raise RuntimeError("QMP closed before replying")
                    response = json.loads(line)
                    if response.get("id") != request_id:
                        continue  # QMP can emit asynchronous events between replies.
                    if "error" in response:
                        raise RuntimeError(f"QMP {obj['execute']} failed: {response['error']}")
                    if "return" not in response:
                        raise RuntimeError("Malformed QMP reply")
                    return response

            qmp_cmd({"execute": "qmp_capabilities"})

            def send_mouse_abs(x, y):
                ax = int(x * 32767 / 1279)
                ay = int(y * 32767 / 799)
                qmp_cmd({
                    "execute": "input-send-event",
                    "arguments": {
                        "events": [
                            {"type": "abs", "data": {"axis": "x", "value": ax}},
                            {"type": "abs", "data": {"axis": "y", "value": ay}},
                        ]
                    },
                })

            def send_mouse_click():
                qmp_cmd({
                    "execute": "input-send-event",
                    "arguments": {
                        "events": [
                            {"type": "btn", "data": {"down": True, "button": "left"}},
                        ]
                    },
                })
                time.sleep(0.05)
                qmp_cmd({
                    "execute": "input-send-event",
                    "arguments": {
                        "events": [
                            {"type": "btn", "data": {"down": False, "button": "left"}},
                        ]
                    },
                })

            def send_key(qcode, modifiers=()):
                for modifier in modifiers:
                    qmp_cmd({"execute": "input-send-event", "arguments": {"events": [
                        {"type": "key", "data": {"down": True,
                         "key": {"type": "qcode", "data": modifier}}}]}})
                qmp_cmd({
                    "execute": "input-send-event",
                    "arguments": {
                        "events": [
                            {"type": "key", "data": {"down": True, "key": {"type": "qcode", "data": qcode}}},
                        ]
                    },
                })
                time.sleep(0.03)
                qmp_cmd({
                    "execute": "input-send-event",
                    "arguments": {
                        "events": [
                            {"type": "key", "data": {"down": False, "key": {"type": "qcode", "data": qcode}}},
                        ]
                    },
                })
                for modifier in reversed(modifiers):
                    qmp_cmd({"execute": "input-send-event", "arguments": {"events": [
                        {"type": "key", "data": {"down": False,
                         "key": {"type": "qcode", "data": modifier}}}]}})

            def send_str(s):
                qcode_map = {
                    " ": "spc",
                    "\n": "ret",
                    "-": "minus",
                    "/": "slash",
                    ".": "dot",
                }
                for ch in s:
                    if ch == ":":
                        send_key("semicolon", ("shift",))
                        time.sleep(args.key_delay)
                        continue
                    code = qcode_map.get(ch, ch.lower() if ch.isalnum() else None)
                    if code:
                        send_key(code)
                        time.sleep(args.key_delay)

            # Test mouse click & keyboard input on Terminal window at (150, 200)
            # Show proof of 64-bit Linux kernel + Haiku GUI app_server in Terminal
            print("[*] Bringing Terminal to front (click its window tab) and typing commands via QMP...")
            send_mouse_abs(110, 18)
            time.sleep(0.1)
            send_mouse_click()
            time.sleep(args.cmd_delay)
            for command in ("uname -srm", "cat /proc/version", "ls /boot/system/servers",
                            "ls /boot/system/apps"):
                send_str(command + "\n")
                time.sleep(args.cmd_delay)

            # Prove that GUI keyboard input actually reached Terminal's shell.
            # A fresh random filename prevents an old image or log from passing.
            # Only QMP input creates the file; the independent serial shell checks it.
            nonce = secrets.token_hex(12)
            proof_path = f"/run/gui-input-{nonce}"
            proof_marker = f"GUI_INPUT_PROOF_{nonce}"
            send_str(f"touch {proof_path}\n")
            serial_command = (
                f"for attempt in 1 2 3 4 5 6 7 8 9 10; do "
                f"if test -f {proof_path}; then "
                f"printf '\\n{proof_marker}\\n'; break; fi; sleep 1; done\n"
            )
            proc.stdin.write(serial_command.encode())
            proc.stdin.flush()
            proof_output = ""
            proof_deadline = time.monotonic() + 20
            # Match an output line, not the serial console's echoed input command.
            proof_pattern = re.compile(r"(?:^|\n)" + proof_marker + r"\r?\n")
            while not proof_pattern.search(proof_output) and time.monotonic() < proof_deadline:
                ready, _, _ = select.select([stdout_fd], [], [], 0.2)
                if ready:
                    chunk = os.read(stdout_fd, 4096)
                    if not chunk:
                        break
                    proof_output += record_serial(chunk)
                elif proc.poll() is not None:
                    break
            if not proof_pattern.search(proof_output):
                sys.exit("GUI keyboard proof failed: Terminal did not create the random-token file")
            print("[+] Verified GUI keyboard input through Terminal and independent serial file check.")

            # Capture Terminal proof screenshot while Terminal is active in foreground
            terminal_png = os.path.abspath(
                os.path.join(os.path.dirname(args.screenshot_png or args.screenshot_ppm),
                             f"haiku-terminal-proof-{args.arch}.png")
            )
            terminal_ppm = terminal_png.replace(".png", ".ppm")
            print(f"[*] Dumping Terminal proof framebuffer via QMP to {terminal_ppm}...")
            qmp_cmd({"execute": "screendump", "arguments": {"filename": terminal_ppm}})
            if os.path.exists(terminal_ppm) and os.path.getsize(terminal_ppm) > 0:
                t_colors, tw, th = analyze_ppm(terminal_ppm)
                print(f"[+] Terminal proof captured: {tw}x{th}, {t_colors} unique colors")
                ppm_to_png(terminal_ppm, terminal_png)
                try:
                    os.unlink(terminal_ppm)
                except OSError:
                    pass
            else:
                sys.exit("Terminal proof framebuffer was not captured")

            # Bring the target window forward after capturing Terminal.
            focus_x = args.focus_x if args.focus_x is not None else (1000 if args.wait_for else 830)
            focus_y = args.focus_y if args.focus_y is not None else (105 if args.wait_for else 620)
            send_mouse_abs(focus_x, focus_y)
            time.sleep(0.1)
            send_mouse_click()
            time.sleep(args.cmd_delay)
            send_mouse_abs(900, 450)
            time.sleep(0.2)

            def send_mouse_wheel(button_name, ticks=3):
                for _ in range(ticks):
                    qmp_cmd({
                        "execute": "input-send-event",
                        "arguments": {
                            "events": [
                                {"type": "btn", "data": {"down": True, "button": button_name}},
                            ]
                        },
                    })
                    time.sleep(0.02)
                    qmp_cmd({
                        "execute": "input-send-event",
                        "arguments": {
                            "events": [
                                {"type": "btn", "data": {"down": False, "button": button_name}},
                            ]
                        },
                    })
                    time.sleep(0.05)

            print("[*] Sending mouse wheel down/up via QMP...")
            send_mouse_wheel("wheel-down", ticks=args.wheel_down)
            time.sleep(args.cmd_delay)
            send_mouse_wheel("wheel-up", ticks=args.wheel_up)
            time.sleep(args.cmd_delay * 2)
            # Avoid pausing a page's carousel by hovering over its photograph.
            if args.wait_for:
                send_mouse_abs(1260, 760)

            ppm_path = os.path.abspath(args.screenshot_ppm)
            os.makedirs(os.path.dirname(ppm_path), exist_ok=True)

            print(f"[*] Dumping framebuffer via QMP to {ppm_path}...")
            dump_res = qmp_cmd({"execute": "screendump", "arguments": {"filename": ppm_path}})

            # Optionally require that a screen region (e.g. the Chromium page
            # area) shows real rendered content, not a blank page.
            if args.content_rect:
                rect = tuple(int(v) for v in args.content_rect.split(","))
                deadline = time.time() + args.content_timeout
                while True:
                    n = region_colors(ppm_path, rect)
                    print(f"[*] Content region {rect}: {n} distinct colours "
                          f"(need >= {args.content_min_colors})", flush=True)
                    if n >= args.content_min_colors:
                        print("[+] Verified rendered page content.", flush=True)
                        break
                    if time.time() >= deadline:
                        print("[-] Page region did not reach the required colour threshold.", flush=True)
                        content_failed = True
                        break
                    if args.wait_for:
                        # Real pages can have empty advertisement slots or
                        # monochrome photographs in the initial viewport.
                        # Inspect more of the actual page while retaining the
                        # same colour threshold and final framebuffer proof.
                        print("[*] Scrolling to inspect more loaded page content.", flush=True)
                        send_mouse_abs(900, 450)
                        send_mouse_wheel("wheel-down", ticks=2)
                        send_mouse_abs(1260, 760)
                    # Drain serial output while waiting, then re-capture.
                    wait_end = time.time() + 5.0
                    while time.time() < wait_end:
                        rlist, _, _ = select.select([stdout_fd], [], [], 0.2)
                        if rlist:
                            chunk_bytes = os.read(stdout_fd, 4096)
                            if chunk_bytes:
                                record_serial(chunk_bytes)
                    qmp_cmd({"execute": "screendump", "arguments": {"filename": ppm_path}})
            if os.path.exists(ppm_path) and os.path.getsize(ppm_path) > 0:
                colors, w, h = analyze_ppm(ppm_path)
                print(f"[+] Framebuffer captured: {w}x{h}, {colors} unique colors ({os.path.getsize(ppm_path)} bytes)")
                if colors < 10:
                    print(f"[-] ERROR: Insufficient pixel diversity ({colors} colors < 10). Screen may be blank.")
                    sys.exit(1)
                print(f"[+] Pixel diversity verified: {colors} distinct colors rendered on screen.")

                if args.screenshot_png:
                    ppm_to_png(ppm_path, os.path.abspath(args.screenshot_png))
            else:
                print(f"[-] ERROR: Failed to dump screenshot to {ppm_path}")
                sys.exit(1)

            # Preserve the Yahoo capture above, then independently exercise the
            # patched Ozone input path with observable DOM and scroll results.
            if args.wait_for:
                with BrowserInputProbe() as probe:
                    def wait_probe(event):
                        deadline = time.monotonic() + args.input_timeout
                        while not probe.events[event].is_set() and time.monotonic() < deadline:
                            ready, _, _ = select.select([stdout_fd], [], [], 0.2)
                            if ready:
                                chunk = os.read(stdout_fd, 4096)
                                if not chunk:
                                    break
                                record_serial(chunk)
                            elif proc.poll() is not None:
                                break
                        if not probe.events[event].is_set():
                            failure_ppm = ppm_path + f".probe-{probe.nonce}-{event}.ppm"
                            qmp_cmd({"execute": "screendump", "arguments": {"filename": failure_ppm}})
                            ppm_to_png(failure_ppm, failure_ppm.removesuffix(".ppm") + ".png")
                            sys.exit(f"Chromium input proof failed: no {event} callback")
                        print(f"[+] Chromium DOM callback: {event}", flush=True)

                    print(f"[*] Navigating Chromium to fresh input probe {probe.url}", flush=True)
                    # A tooltip must not steal keyboard focus from its owner.
                    send_mouse_abs(1140, 182)
                    time.sleep(2.0 + args.cmd_delay)
                    send_mouse_abs(1260, 760)
                    time.sleep(0.2)
                    send_key("l", ("ctrl",))
                    time.sleep(0.3 + args.cmd_delay)
                    send_str(probe.url + "\n")
                    wait_probe("ready")
                    time.sleep(args.cmd_delay)
                    send_mouse_abs(900, 450)
                    time.sleep(0.2)
                    send_mouse_click()
                    wait_probe("click")
                    send_key("k")
                    wait_probe("key")
                    send_mouse_wheel("wheel-down", ticks=4)
                    wait_probe("wheel-down")
                    wait_probe("scrolled")
                    send_mouse_wheel("wheel-up", ticks=8)
                    wait_probe("wheel-up")
                    wait_probe("top")
                    probe_ppm = ppm_path + f".probe-{probe.nonce}-success.ppm"
                    qmp_cmd({"execute": "screendump", "arguments": {"filename": probe_ppm}})
                    print(f"[*] Captured DOM input proof framebuffer: {probe_ppm}", flush=True)
                    print(f"[+] Verified Chromium DOM input: key, click, wheel-down/up and scrolling (HTTP nonce {probe.nonce})")
            qmp_stream.close()
            qmp.close()

    finally:
        if os.path.exists(qmp_sock):
            try:
                os.unlink(qmp_sock)
            except OSError:
                pass
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    elapsed = time.time() - start_time
    print(f"[*] Execution finished in {elapsed:.2f} seconds.")

    if missing_marker:
        print(f"[-] FAILED: marker not seen: '{args.wait_for}'")
        sys.exit(1)
    if content_failed:
        print(f"[-] FAILED: content region {args.content_rect} did not reach the required colour threshold")
        sys.exit(1)
    if not gui_ready and required_milestones:
        missing = required_milestones - passed_milestones
        print(f"[-] FAILED: Missing milestones: {missing}")
        sys.exit(1)

    if "chromium" in args.cmdline or "tier3" in args.cmdline:
        print("[+] Tier 3 Haiku GUI + Chromium QEMU verification SUCCESSFUL.")
    else:
        print("[+] Tier 2 Haiku GUI QEMU verification SUCCESSFUL.")


def main():
    parser = argparse.ArgumentParser(description="Haiku GUI QEMU verification runner")
    parser.add_argument("--arch", default="x86_64", help="Architecture: x86_64 or arm64")
    parser.add_argument("--kernel", required=True, help="Path to bzImage / Image")
    parser.add_argument("--initrd", required=True, help="Path to initramfs.cpio.gz")
    parser.add_argument("--cmdline", default="console=ttyS0 haiku.mode=gui quiet panic=1", help="Kernel cmdline")
    parser.add_argument("--memory", default="2048M", help="QEMU RAM size")
    parser.add_argument("--settle", type=float, default=4.5, help="Settle delay in seconds before screendump")
    parser.add_argument("--timeout", type=int, default=30, help="Timeout in seconds")
    parser.add_argument("--expect", action="append", help="Expected string in output (repeatable)")
    parser.add_argument("--wait-for", default=None, help="Extra serial marker to wait for after all milestones (e.g. page title)")
    parser.add_argument("--wait-for-timeout", type=int, default=120, help="Seconds to wait for --wait-for")
    parser.add_argument("--key-delay", type=float, default=0.03, help="Delay between injected key presses")
    parser.add_argument("--cmd-delay", type=float, default=0.4, help="Delay after each Terminal command / UI action")
    parser.add_argument("--focus-x", type=int, help="X of a visible window point (defaults to Chromium or WidgetGallery)")
    parser.add_argument("--focus-y", type=int, help="Y of a visible window point (defaults to Chromium or WidgetGallery)")
    parser.add_argument("--content-rect", default=None, help="x0,y0,x1,y1 region that must show rendered content")
    parser.add_argument("--content-min-colors", type=int, default=500, help="Minimum distinct colours in --content-rect")
    parser.add_argument("--content-timeout", type=int, default=120, help="Seconds to wait for --content-rect to fill")
    parser.add_argument("--input-timeout", type=float, default=30,
                        help="Seconds to wait for each required Chromium DOM callback")
    parser.add_argument("--wheel-down", type=int, default=5, help="Mouse wheel-down ticks")
    parser.add_argument("--wheel-up", type=int, default=2, help="Mouse wheel-up ticks")
    parser.add_argument("--screenshot-ppm", default="docs/screenshots/haiku-gui-x86_64.ppm", help="Output PPM path")
    parser.add_argument("--screenshot-png", default="docs/screenshots/haiku-gui-x86_64.png", help="Output PNG path")
    args = parser.parse_args()

    run_gui_verification(args)


if __name__ == "__main__":
    main()
