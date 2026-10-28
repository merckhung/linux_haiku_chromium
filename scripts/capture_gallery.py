#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Capture WidgetGallery tabs from a running local QEMU framebuffer."""
import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import struct
import tempfile
import time
from verify_gui_boot import ppm_to_png


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--socket', required=True)
    parser.add_argument('--arch', choices=('x86_64', 'arm64'), required=True)
    parser.add_argument('--output', default='docs/screenshots')
    parser.add_argument('--record-evidence', action='store_true',
                        help='Record boot and capture evidence from the desktop launcher')
    parser.add_argument('--vms-json', default='runtime/desktop-vms.json')
    parser.add_argument('--ready-timeout', type=float, default=180,
                        help='Seconds to wait for the recorded gallery guest to initialize')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    boot = None
    if args.record_evidence:
        output.relative_to(root)  # Release evidence must use project-relative paths.
        records = json.loads(Path(args.vms_json).read_text())
        matches = [record for record in records if record.get('arch') == args.arch
                   and record.get('tier') == 2 and record.get('qmp_socket') == args.socket]
        if len(matches) != 1:
            raise SystemExit('No matching Tier 2 boot record; launch the desktop first')
        boot = matches[0]
        for label in ('kernel', 'initrd', 'serial_log'):
            relative = Path(boot[label])
            if relative.is_absolute() or '..' in relative.parts:
                raise SystemExit('Boot record paths must be project-relative')
        for label in ('kernel', 'initrd'):
            artifact = (root / boot[label]).resolve()
            if hashlib.sha256(artifact.read_bytes()).hexdigest() != boot[label + '_sha256']:
                raise SystemExit('Boot input changed after launch; start a guest with current inputs')
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(15)
        sock.connect(args.socket)
        if boot:
            peer_pid, _, _ = struct.unpack('3i', sock.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            if peer_pid != boot['pid']:
                raise SystemExit('QMP peer differs from the recorded desktop process')
        stream = sock.makefile('rwb', buffering=0)
        json.loads(stream.readline())
        request_id = 0

        def command(name, arguments=None):
            nonlocal request_id
            request_id += 1
            msg = {'execute': name, 'id': request_id}
            if arguments is not None:
                msg['arguments'] = arguments
            stream.write((json.dumps(msg) + '\n').encode())
            while True:
                reply = json.loads(stream.readline())
                if reply.get('id') == request_id:
                    if 'error' in reply:
                        raise RuntimeError(reply['error'])
                    return reply.get('return')

        command('qmp_capabilities')
        status = command('query-status')
        target = command('query-target')
        if not status['running'] or target['arch'] != ('aarch64' if args.arch == 'arm64' else 'x86_64'):
            raise SystemExit('QMP guest is stopped or has the wrong architecture')
        cpus = command('query-cpus-fast')
        memory = command('query-memory-size-summary')

        def check_processes(serial):
            for name, pid in re.findall(r'\[init\] Forked (app_server|Deskbar|Terminal|WidgetGallery) with PID (\d+)', serial):
                if re.search(r'\[init\] Child process ' + pid + r' (?:exited|terminated)', serial):
                    raise SystemExit(f'Required gallery process exited: {name}')

        if boot:
            deadline = time.monotonic() + args.ready_timeout
            serial_path = root / boot['serial_log']
            while True:
                serial = serial_path.read_text() if serial_path.is_file() else ''
                check_processes(serial)
                if '[WidgetGallery] Full-blown Gallery window launched' in serial:
                    break
                if time.monotonic() >= deadline or not command('query-status')['running']:
                    raise SystemExit('Recorded guest did not initialize the gallery')
                time.sleep(.25)

        def click(x, y):
            command('input-send-event', {'events': [
                {'type': 'abs', 'data': {'axis': 'x', 'value': round(x * 32767 / 1279)}},
                {'type': 'abs', 'data': {'axis': 'y', 'value': round(y * 32767 / 799)}}]})
            time.sleep(.2)
            for down in (True, False):
                command('input-send-event', {'events': [{'type': 'btn', 'data': {'down': down, 'button': 'left'}}]})
                time.sleep(.1)
            # TCG may still be draining focus and paint events after a click.
            time.sleep(10 if args.arch == 'arm64' else 1)

        # This part of the gallery stays exposed when Terminal is in front.
        click(830, 620)
        screenshots = []
        capture_lines = []
        for tab, x in enumerate((120, 260, 365, 500), 1):
            click(x, 80)
            suffix = f'tab{tab}' if args.arch == 'x86_64' else f'arm64-tab{tab}'
            png = output / f'haiku-widget-gallery-{suffix}.png'
            ppm = png.with_suffix('.ppm')
            command('screendump', {'filename': str(ppm)})
            ppm_to_png(ppm, png)
            ppm.unlink()
            print(f'Captured {png.name}', flush=True)
            if boot:
                data = png.read_bytes()
                if struct.unpack('>II', data[16:24]) != (1280, 800):
                    raise SystemExit('Gallery evidence requires a 1280x800 framebuffer')
                checksum = hashlib.sha256(data).hexdigest()
                screenshots.append({'tab': tab, 'path': str(png.relative_to(root)), 'sha256': checksum})
                capture_lines.append(f'Artifact screenshot {png.name} sha256: {checksum}')
        if boot:
            final_status = command('query-status')
            if not final_status['running']:
                raise SystemExit('Guest stopped during gallery capture')
            serial = (root / boot['serial_log']).read_text()
            check_processes(serial)
            if '[WidgetGallery] Full-blown Gallery window launched' not in serial:
                raise SystemExit('Gallery launch marker missing from guest log')
            for label in ('kernel', 'initrd'):
                if hashlib.sha256((root / boot[label]).read_bytes()).hexdigest() != boot[label + '_sha256']:
                    raise SystemExit('Boot input changed during capture; start a guest with current inputs')
            validation = root / 'docs/validation'
            validation.mkdir(exist_ok=True)
            log = validation / f'gallery-{args.arch}.txt'
            log.write_text(serial.replace(str(root), '<workspace>') + '\n' + '\n'.join(capture_lines) + '\n')
            run = {'arch': args.arch, 'started_utc': boot['started_utc'],
                   'captured_utc': datetime.now(timezone.utc).isoformat(),
                   'kernel': boot['kernel'], 'kernel_sha256': boot['kernel_sha256'],
                   'initrd': boot['initrd'], 'initrd_sha256': boot['initrd_sha256'],
                   'qmp_status': final_status, 'qmp_target': target,
                   'guest_cpus': len(cpus), 'memory': memory, 'log': str(log.relative_to(root)),
                   'log_sha256': hashlib.sha256(log.read_bytes()).hexdigest(), 'screenshots': screenshots}
            runtime = root / 'runtime'
            with (runtime / '.gallery-evidence.lock').open('w') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                manifest = validation / 'gallery.json'
                record = json.loads(manifest.read_text()) if manifest.is_file() else {'schema': 1, 'runs': []}
                record.pop('visual_review', None)
                record.pop('reviewed_utc', None)
                record['recorded_utc'] = datetime.now(timezone.utc).isoformat()
                record['capture_method'] = 'QMP screendump with lossless PPM-to-PNG conversion'
                record['runs'] = sorted([old for old in record['runs'] if old['arch'] != args.arch] + [run], key=lambda item: item['arch'])
                with tempfile.NamedTemporaryFile(dir=runtime, delete=False) as temporary:
                    temporary.write((json.dumps(record, indent=2) + '\n').encode())
                    temporary_name = temporary.name
                os.replace(temporary_name, manifest)
            print(f'Recorded current {args.arch} gallery boot and capture evidence', flush=True)


if __name__ == '__main__':
    main()
