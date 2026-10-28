#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Record local verifier evidence and check packaged Chromium against prebuilts."""
import gzip
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from package_chromium import REVISION, file_lists


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def cpio_hashes(path):
    result = {}
    with gzip.open(path, 'rb') as stream:
        while True:
            header = stream.read(110)
            if header[:6] != b'070701':
                raise ValueError('Invalid CPIO header')
            size = int(header[54:62], 16)
            namesize = int(header[94:102], 16)
            name = stream.read(namesize).rstrip(b'\0').decode()
            stream.read(-(110 + namesize) % 4)
            h = hashlib.sha256()
            remaining = size
            while remaining:
                data = stream.read(min(remaining, 1024 * 1024))
                if not data:
                    raise ValueError('Truncated CPIO archive')
                h.update(data)
                remaining -= len(data)
            stream.read(-size % 4)
            if name == 'TRAILER!!!':
                return result
            result[name] = {'bytes': size, 'sha256': h.hexdigest()}


def chromium_build_record(arch):
    path = Path('docs/validation') / f'chromium-build-{arch}.json'
    if not path.is_file():
        raise SystemExit(f'Chromium source build provenance missing: {path}')
    record = json.loads(path.read_text())
    config = Path('userland/chromium/patches/args') / f'{arch}.gn'
    patch = Path('userland/chromium/patches/0001-chromium-ozone-haiku-linux.patch')
    if (record.get('schema') != 1 or record.get('arch') != arch
            or record.get('source_revision') != REVISION
            or record.get('patch_sha256') != digest(patch)
            or record.get('gn_args_sha256') != digest(config)
            or record.get('credits_sha256') != digest(Path('licenses/chromium/about_credits.html'))):
        raise SystemExit(f'Chromium source build provenance is stale: {path}')
    files, _ = file_lists()
    if set(record.get('files', {})) != set(files):
        raise SystemExit(f'Chromium build record has an incomplete file set: {path}')
    for name in files:
        artifact = Path('userland/chromium/prebuilt') / arch / name
        expected = record['files'][name]
        if artifact.stat().st_size != expected['bytes'] or digest(artifact) != expected['sha256']:
            raise SystemExit(f'Packaged Chromium differs from the source build record: {artifact}')
    for reference in Path('userland/chromium/ozone_haiku').iterdir():
        if reference.is_file():
            name = 'ui/ozone/platform/haiku/' + reference.name
            if record.get('patched_files', {}).get(name) != digest(reference):
                raise SystemExit(f'Ozone reference source differs from the compiled patch: {reference}')
    dynamic = subprocess.check_output(['readelf', '-d', str(Path('userland/chromium/prebuilt') / arch / 'chrome')], text=True)
    if not re.search(r'\(NEEDED\).*\[libffmpeg\.so\]', dynamic):
        raise SystemExit(f'Chromium does not link the replaceable FFmpeg library: {arch}')
    notices = json.loads(Path('docs/validation/source-notices.json').read_text())
    ffmpeg = record.get('dependencies', {}).get(record.get('checkout_name', 'src') + '/third_party/ffmpeg', {})
    if ffmpeg.get('rev') != notices['ffmpeg']['source_revision']:
        raise SystemExit(f'FFmpeg corresponding source differs from the build dependency: {arch}')
    return {'record': str(path), 'sha256': digest(path), 'source_revision': REVISION}


def notice_record():
    path = Path('docs/validation/source-notices.json')
    record = json.loads(path.read_text())
    inventory = Path('licenses/chromium/inventory.json')
    if (record['chromium_revision'] != REVISION
            or record['notice_inventory_sha256'] != digest(inventory)
            or record['notice_source_files'] != len(json.loads(inventory.read_text())['files'])
            or record['offline_credits_sha256'] != digest(Path('licenses/chromium/about_credits.html'))
            or record['ffmpeg']['archive_sha256'] != digest(Path('third_party/sources/ffmpeg-42a2a7a2.tar.gz'))):
        raise SystemExit('Source notice evidence differs from the current release files')
    return {'record': str(path), 'sha256': digest(path)}


def gallery_record():
    path = Path('docs/validation/gallery.json')
    record = json.loads(path.read_text())
    runs = record.get('runs', [])
    if not record.get('visual_review') or not record.get('reviewed_utc'):
        raise SystemExit('Gallery captures require a recorded visual review')
    reviewed = datetime.fromisoformat(record['reviewed_utc'])
    if reviewed.tzinfo is None:
        raise SystemExit('Gallery review time must include its timezone')
    if {run['arch'] for run in runs} != {'x86_64', 'arm64'} or len(runs) != 2:
        raise SystemExit('Gallery evidence must cover both architectures')
    for run in runs:
        if not run.get('visual_review_completed') or not run.get('host_window_mapped'):
            raise SystemExit(f'Gallery visual and mapped-window checks missing: {run["arch"]}')
        if reviewed < datetime.fromisoformat(run['captured_utc']):
            raise SystemExit(f'Gallery visual review predates capture: {run["arch"]}')
        for label, key in (('kernel', 'kernel_sha256'), ('initrd', 'initrd_sha256'), ('log', 'log_sha256')):
            if digest(Path(run[label])) != run[key]:
                raise SystemExit(f'Gallery {label} differs from capture evidence: {run["arch"]}')
        screenshots = run.get('screenshots', [])
        if {shot['tab'] for shot in screenshots} != {1, 2, 3, 4} or len(screenshots) != 4:
            raise SystemExit(f'Gallery evidence must cover four tabs: {run["arch"]}')
        if not run.get('qmp_status', {}).get('running'):
            raise SystemExit(f'Gallery guest was not running during capture: {run["arch"]}')
        for shot in screenshots:
            if digest(Path(shot['path'])) != shot['sha256']:
                raise SystemExit(f'Gallery screenshot differs from captured evidence: {shot["path"]}')
    return {'record': str(path), 'sha256': digest(path)}


def desktop_record():
    path = Path('docs/validation/desktop-status.json')
    record = json.loads(path.read_text())
    if not isinstance(record, dict) or record.get('schema') != 1:
        raise SystemExit('Record current desktop validation with run_all_variants.py --record-validation')
    checked = datetime.fromisoformat(record['checked_utc'])
    runs = record.get('runs', [])
    expected = {(arch, tier) for arch in ('x86_64', 'arm64') for tier in (1, 2, 3)}
    if checked.tzinfo is None or len(runs) != 6 or {(r['arch'], r['tier']) for r in runs} != expected:
        raise SystemExit('Desktop validation must cover all six variants with a timezone-aware check time')
    for run in runs:
        if (run.get('status') != 'running' or run.get('window_visible') is not True
                or datetime.fromisoformat(run['started_utc']) > checked):
            raise SystemExit(f'Desktop guest was not verified running and mapped: {run["name"]}')
        package = {1: 'minimal_rootfs', 2: 'rootfs', 3: 'chromium_rootfs'}[run['tier']]
        expected_paths = {
            'kernel': Path('bazel-bin/kernel') / ('Image' if run['arch'] == 'arm64' else 'bzImage'),
            'initrd': Path('bazel-bin') / package / ('initramfs_arm64.cpio.gz' if run['arch'] == 'arm64' else 'initramfs.cpio.gz'),
        }
        for label, artifact in expected_paths.items():
            if run.get(label) != str(artifact) or run.get(label + '_sha256') != digest(artifact):
                raise SystemExit(f'Desktop {label} evidence is stale: {run["name"]}')
        if digest(Path(run['log'])) != run.get('log_sha256'):
            raise SystemExit(f'Desktop serial log differs from recorded evidence: {run["name"]}')
    return {'record': str(path), 'sha256': digest(path)}


def main():
    root = Path.cwd()
    output = Path('docs/validation')
    output.mkdir(parents=True, exist_ok=True)
    notices = notice_record()
    gallery = gallery_record()
    builds = {arch: chromium_build_record(arch) for arch in ('x86_64', 'arm64')}
    desktop = desktop_record()
    records = {'schema': 3, 'date_utc': datetime.now(timezone.utc).date().isoformat(),
               'chromium_rebuilt': True, 'chromium_builds': builds, 'source_notices': notices, 'gallery': gallery,
               'desktop': desktop,
               'runs': [], 'artifacts': {}, 'packaged_chromium': {}}
    for arch in ('x86_64', 'arm64'):
        for tier in (1, 2, 3):
            name = f'tier{tier}-{arch}'
            log = Path('runtime') / f'{name}.log'
            text = log.read_text()
            marker = 'QEMU headless verification SUCCESSFUL.' if tier == 1 else f'Tier {tier} Haiku GUI' + (' + Chromium' if tier == 3 else '') + ' QEMU verification SUCCESSFUL.'
            if marker not in text or 'Traceback' in text or '[-] FAILED' in text:
                raise SystemExit(f'Verification did not pass: {log}')
            kernel = Path('bazel-bin/kernel') / ('Image' if arch == 'arm64' else 'bzImage')
            package = {1: 'minimal_rootfs', 2: 'rootfs', 3: 'chromium_rootfs'}[tier]
            image = Path('bazel-bin') / package / ('initramfs_arm64.cpio.gz' if arch == 'arm64' else 'initramfs.cpio.gz')
            for label, artifact in (('kernel', kernel), ('initrd', image)):
                if f'Artifact {label} sha256: {digest(artifact)}' not in text:
                    raise SystemExit(f'Verifier did not run against the current {label}: {log}')
            started = re.search(r'Verification started UTC: (\S+)', text)
            if not started:
                raise SystemExit(f'Verifier timestamp absent: {log}')
            if tier > 1:
                if '[+] Verified GUI keyboard input through Terminal and independent serial file check.' not in text:
                    raise SystemExit(f'GUI keyboard proof absent: {log}')
                screenshot = Path('docs/screenshots') / f'haiku-{"gui" if tier == 2 else "chromium"}-{arch}.png'
                if f'Artifact screenshot {screenshot.name} sha256: {digest(screenshot)}' not in text:
                    raise SystemExit(f'Screenshot differs from verifier capture: {screenshot}')
                if tier == 3:
                    terminal = Path('docs/screenshots') / f'haiku-terminal-proof-{arch}.png'
                    if f'Artifact screenshot {terminal.name} sha256: {digest(terminal)}' not in text:
                        raise SystemExit(f'Terminal screenshot differs from Chromium run capture: {terminal}')
            elapsed = float(re.findall(r'Execution finished in ([0-9.]+) seconds', text)[-1])
            clean = re.sub(r'\x1b\[[0-9;?]*[A-Za-z]', '', text)
            clean = clean.replace(str(root), '<workspace>')
            clean = re.sub(r'/(?:var/)?tmp/lhc_bz\b', '<bazel-output>', clean)
            clean = re.sub(r'/tmp/qmp_gui_\d+\.sock', '<qmp-socket>', clean)
            # Retain serial banners without making them look like Git conflicts.
            clean = '\n'.join(
                ('[serial] ' + line.rstrip()) if re.fullmatch(r'=+', line.strip())
                else line.rstrip() for line in clean.splitlines()
            ) + '\n'
            destination = output / f'{name}.txt'
            destination.write_text(clean)
            rec = {'arch': arch, 'tier': tier, 'result': 'PASS', 'started_utc': started[1], 'seconds': elapsed, 'log': str(destination), 'log_sha256': digest(destination)}
            if tier == 3:
                if '[+] Verified Chromium DOM input: key, click, wheel-down/up and scrolling (HTTP nonce ' not in text:
                    raise SystemExit(f'Chromium DOM input proof absent: {log}')
                rec['yahoo_title_seconds'] = float(re.search(r'\(([0-9.]+)s after QEMU start\)', text)[1])
                rec['page_region_colors'] = int(re.findall(r'Content region .*?: (\d+) distinct', text)[-1])
                if rec['page_region_colors'] < 2000:
                    raise SystemExit(f'Chromium page capture does not meet the required colour threshold: {log}')
            records['runs'].append(rec)
        kernel = Path('bazel-bin/kernel') / ('Image' if arch == 'arm64' else 'bzImage')
        records['artifacts'][str(kernel)] = {'bytes': kernel.stat().st_size, 'sha256': digest(kernel)}
        for package in ('minimal_rootfs', 'rootfs', 'chromium_rootfs'):
            image = Path('bazel-bin') / package / ('initramfs_arm64.cpio.gz' if arch == 'arm64' else 'initramfs.cpio.gz')
            records['artifacts'][str(image)] = {'bytes': image.stat().st_size, 'sha256': digest(image)}
            # Validate archive integrity even for the tiers without Chromium.
            contents = cpio_hashes(image)
            if package != 'chromium_rootfs':
                continue
            matched = {}
            prebuilt = Path('userland/chromium/prebuilt') / arch
            for src in sorted(prebuilt.rglob('*')):
                if not src.is_file():
                    continue
                target = 'boot/system/apps/' + str(src.relative_to(prebuilt))
                expected = {'bytes': src.stat().st_size, 'sha256': digest(src)}
                if contents.get(target) != expected:
                    raise SystemExit(f'Packaged prebuilt differs or is absent: {target}')
                matched[str(src.relative_to(prebuilt))] = expected
            chrome = prebuilt / 'chrome'
            header = chrome.open('rb').read(20)
            if header[:5] != b'\x7fELF\x02' or int.from_bytes(header[18:20], 'little') != (183 if arch == 'arm64' else 62):
                raise SystemExit(f'Unexpected ELF architecture: {chrome}')
            records['packaged_chromium'][arch] = matched
    for png in sorted(Path('docs/screenshots').glob('*.png')):
        header = png.read_bytes()[:24]
        if header[:8] != b'\x89PNG\r\n\x1a\n' or struct.unpack('>II', header[16:24]) != (1280, 800):
            raise SystemExit(f'Unexpected screenshot dimensions: {png}')
        records['artifacts'][str(png)] = {'bytes': png.stat().st_size, 'sha256': digest(png)}
    (output / 'manifest.json').write_text(json.dumps(records, indent=2) + '\n')
    print('PASS: six verifier logs, six CPIO archives, both packaged Chromium sets, ELF architectures and all screenshots checked')
    for run in records['runs']:
        print(f"{run['arch']} tier {run['tier']}: {run['seconds']} s")


if __name__ == '__main__':
    main()
