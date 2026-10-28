#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Open the six locally built variants on the current desktop, retaining logs."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess


def record_validation(root):
    """Check the six live launcher guests before publishing desktop evidence."""
    boots = json.loads((root / 'runtime/desktop-vms.json').read_text())
    expected = {(arch, tier) for arch in ('x86_64', 'arm64') for tier in (1, 2, 3)}
    if len(boots) != 6 or {(b['arch'], b['tier']) for b in boots} != expected:
        raise SystemExit('Launch all six variants before recording desktop validation')
    tree = subprocess.check_output(['xwininfo', '-root', '-tree'], text=True)
    runs = []
    logs = []
    for boot in boots:
        name = f"haiku-{boot['arch']}-tier{boot['tier']}"
        if boot['name'] != name:
            raise SystemExit('Desktop boot name differs from its architecture and tier')
        command_line = Path(f"/proc/{boot['pid']}/cmdline").read_bytes().split(b'\0')
        for label in ('kernel', 'initrd'):
            relative = Path(boot[label])
            if relative.is_absolute() or '..' in relative.parts:
                raise SystemExit('Desktop artifact must use a workspace-relative path')
            artifact = root / relative
            if (str(artifact).encode() not in command_line or
                    hashlib.sha256(artifact.read_bytes()).hexdigest() != boot[label + '_sha256']):
                raise SystemExit(f'Desktop {label} changed or differs from the running guest: {name}')
        windows = re.findall(r'^\s*(0x[0-9a-f]+) "QEMU \(' + re.escape(name) + r'\)"', tree, re.M)
        if len(windows) != 1:
            raise SystemExit(f'Expected one GTK window for {name}')
        window = subprocess.check_output(['xwininfo', '-id', windows[0]], text=True)
        if 'Map State: IsViewable' not in window:
            raise SystemExit(f'Desktop window is not mapped: {name}')
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(15)
            sock.connect(boot['qmp_socket'])
            peer, _, _ = struct.unpack('3i', sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if peer != boot['pid']:
                raise SystemExit(f'QMP peer differs from the recorded process: {name}')
            stream = sock.makefile('rwb', buffering=0)
            json.loads(stream.readline())

            def query(command):
                stream.write((json.dumps({'execute': command, 'id': command}) + '\n').encode())
                while True:
                    reply = json.loads(stream.readline())
                    if reply.get('id') == command:
                        if 'error' in reply:
                            raise SystemExit(f'QMP query failed: {name}: {command}')
                        return reply['return']

            query('qmp_capabilities')
            status = query('query-status')
            target = query('query-target')
            cpu_count = len(query('query-cpus-fast'))
            if not status['running'] or target['arch'] != ('aarch64' if boot['arch'] == 'arm64' else 'x86_64'):
                raise SystemExit(f'Desktop guest is stopped or has the wrong architecture: {name}')
        serial = (root / boot['serial_log']).read_text()
        markers = ('=== ALL 6 HAIKU LIBROOT SUITES PASSED ===', '[PASS] Tier 1 verification passed!') if boot['tier'] == 1 else (
            '[WidgetGallery] Full-blown Gallery window launched',)
        if boot['tier'] == 3:
            markers += ('[Chromium/OzoneHaiku] Window title: Yahoo',
                        '[Chromium/OzoneHaiku] Surface presented successfully')
        if any(marker not in serial for marker in markers):
            raise SystemExit(f'Desktop guest has not reached its required milestones: {name}')
        for _, pid in re.findall(r'\[init\] Forked (app_server|Deskbar|Terminal|WidgetGallery|chrome) with PID (\d+)', serial):
            if re.search(r'\[init\] Child process ' + pid + r' (?:exited|terminated)', serial):
                raise SystemExit(f'Required desktop process exited: {name}')
        relative_log = f'docs/validation/desktop-{boot["arch"]}-tier{boot["tier"]}.txt'
        clean = serial.replace(str(root), '<workspace>')
        clean = '\n'.join(line.rstrip() for line in clean.splitlines()) + '\n'
        logs.append((root / relative_log, clean))
        runs.append({key: boot[key] for key in ('name', 'arch', 'tier', 'started_utc',
                                               'kernel', 'kernel_sha256', 'initrd', 'initrd_sha256')})
        runs[-1].update({'status': status['status'], 'window_visible': True,
                         'qmp_target': target['arch'], 'guest_cpus': cpu_count,
                         'log': relative_log, 'log_sha256': hashlib.sha256(clean.encode()).hexdigest()})
    for path, clean in logs:
        path.write_text(clean)
    output = root / 'docs/validation/desktop-status.json'
    output.write_text(json.dumps({'schema': 1, 'checked_utc': datetime.now(timezone.utc).isoformat(),
                                  'method': 'Live QMP peer, process arguments, input hashes, GTK mapping and serial milestones',
                                  'runs': runs}, indent=2) + '\n')
    print('PASS: all six desktop guests running with mapped GTK windows and current boot inputs')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tiers', type=int, nargs='+', choices=(1, 2, 3), default=[1, 2, 3],
                        help='Tiers to launch; the default launches all six variants')
    parser.add_argument('--record-validation', action='store_true',
                        help='Check the existing six desktop guests and record release evidence')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    os.chdir(root)
    if not (os.environ.get('DISPLAY') or os.environ.get('WAYLAND_DISPLAY')):
        raise SystemExit('A desktop display is required')
    if args.record_validation:
        record_validation(root)
        return
    runtime = root / 'runtime'
    runtime.mkdir(exist_ok=True)
    plans = []
    # Check every selected input and socket before opening any windows.
    for arch in ('x86_64', 'arm64'):
        for tier, package, mode in ((1, 'minimal_rootfs', 'tier1'), (2, 'rootfs', 'gui'), (3, 'chromium_rootfs', 'chromium')):
            if tier not in args.tiers:
                continue
            name = f'haiku-{arch}-tier{tier}'
            kernel = root / 'bazel-bin/kernel' / ('Image' if arch == 'arm64' else 'bzImage')
            image = root / 'bazel-bin' / package / ('initramfs_arm64.cpio.gz' if arch == 'arm64' else 'initramfs.cpio.gz')
            if not kernel.is_file() or not image.is_file():
                raise SystemExit(f'Build the kernel and image first: {name}')
            sock = f'/tmp/{name}.sock'
            if Path(sock).exists():
                raise SystemExit(f'QMP socket already exists; check the existing VM: {sock}')
            plans.append((arch, tier, mode, name, kernel, image, sock))
    records = []
    processes = []
    try:
        for arch, tier, mode, name, kernel, image, sock in plans:
            cmd = ['qemu-system-aarch64' if arch == 'arm64' else 'qemu-system-x86_64',
                   '-name', name, '-kernel', str(kernel), '-initrd', str(image),
                   '-m', '6144M' if tier == 3 else '2048M', '-smp', '8' if arch == 'arm64' else '4',
                   '-display', 'gtk,gl=off,show-tabs=on', '-monitor', 'none', '-no-reboot',
                   '-qmp', f'unix:{sock},server=on,wait=off',
                   '-netdev', 'user,id=net0', '-device', 'virtio-net-pci,netdev=net0',
                   '-device', 'virtio-tablet-pci', '-device', 'virtio-keyboard-pci',
                   '-append', f'console={"ttyAMA0" if arch == "arm64" else "ttyS0"} root=/dev/ram0 rw video=Virtual-1:1280x800@60 haiku.mode={mode} quiet panic=1']
            if arch == 'arm64':
                cmd += ['-M', 'virt', '-cpu', 'cortex-a72', '-device', 'virtio-gpu-pci']
            else:
                cmd += ['-vga', 'std']
                cmd += ['-enable-kvm', '-cpu', 'host'] if os.access('/dev/kvm', os.R_OK | os.W_OK) else ['-cpu', 'max']
            serial = runtime / f'{name}-serial.log'
            if tier == 1:
                cmd += ['-chardev', f'vc,id=serial0,cols=120,rows=40,logfile={serial}', '-serial', 'chardev:serial0']
            else:
                cmd += ['-serial', f'file:{serial}']
            with (runtime / f'{name}-qemu.log').open('w') as log:
                kernel_sha = hashlib.sha256(kernel.read_bytes()).hexdigest()
                image_sha = hashlib.sha256(image.read_bytes()).hexdigest()
                started = datetime.now(timezone.utc).isoformat()
                proc = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            processes.append(proc)
            records.append({'name': name, 'arch': arch, 'tier': tier, 'pid': proc.pid,
                            'qmp_socket': sock, 'started_utc': started,
                            'kernel': str(kernel.relative_to(root)), 'kernel_sha256': kernel_sha,
                            'initrd': str(image.relative_to(root)), 'initrd_sha256': image_sha,
                            'serial_log': str(serial.relative_to(root))})
            (runtime / 'desktop-vms.json').write_text(json.dumps(records, indent=2) + '\n')
            print(f'{name}: PID {proc.pid}', flush=True)
    except BaseException:
        for proc in processes:
            if proc.poll() is None:
                proc.terminate()
        for proc in processes:
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        raise


if __name__ == '__main__':
    main()
