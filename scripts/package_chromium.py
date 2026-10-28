#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate a completed patched Chromium build, strip/package it, and record provenance."""
import argparse
import ast
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile


REVISION = '3bb54dbfa29e4506909fb9d289ad1a28ab76fceb'
ROOT = Path(__file__).resolve().parent.parent


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def run(command, cwd):
    return subprocess.check_output(command, cwd=cwd, stderr=subprocess.PIPE)


def file_lists():
    tree = ast.parse((ROOT / 'chromium_rootfs/chromium_files.bzl').read_text())
    values = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name):
            if node.targets[0].id in ('CHROMIUM_FILES', 'CHROMIUM_EXECUTABLES'):
                values[node.targets[0].id] = ast.literal_eval(node.value)
    return values['CHROMIUM_FILES'], values['CHROMIUM_EXECUTABLES']


def validate_patch(source, patch):
    names = re.findall(r'^diff --git a/(\S+) b/\1$', patch.read_text(), re.M)
    if not names or len(names) != len(set(names)):
        raise ValueError('Patch must contain distinct, unrenamed source paths')
    originals = set()
    with tempfile.TemporaryDirectory(prefix='chromium-patch-check-') as temporary:
        base = Path(temporary)
        for name in names:
            if PurePosixPath(name).is_absolute() or '..' in PurePosixPath(name).parts:
                raise ValueError('Unsafe source path in patch')
            original = subprocess.run(['git', 'show', f'{REVISION}:{name}'], cwd=source,
                                      stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if original.returncode == 0:
                destination = base / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(original.stdout)
                originals.add(name)
            elif run(['git', 'ls-tree', REVISION, '--', name], source).strip():
                raise ValueError(f'Cannot read original source: {name}')
        run(['git', 'init', '-q'], base)
        run(['git', 'apply', str(patch)], base)
        for name in names:
            if (base / name).read_bytes() != (source / name).read_bytes():
                raise ValueError(f'Applied source differs from release patch: {name}')
    changed = set(filter(None, run(['git', 'diff', '--name-only', '-z', 'HEAD'], source)
                         .decode().split('\0')))
    if changed != originals:
        raise ValueError('Chromium has tracked changes beyond the release patch')
    untracked = set(filter(None, run(['git', 'ls-files', '--others', '--exclude-standard', '-z'], source)
                           .decode().split('\0')))
    if untracked != set(names) - originals:
        raise ValueError('Chromium has untracked files beyond the release patch')
    return {name: digest(source / name) for name in names}


def validate_elf(path, arch):
    with path.open('rb') as stream:
        header = stream.read(20)
    if header[:6] != b'\x7fELF\x02\x01' or int.from_bytes(header[18:20], 'little') != {
            'x86_64': 62, 'arm64': 183}[arch]:
        raise ValueError(f'Unexpected ELF architecture: {path.name}')


def check_private(data, forbidden, label):
    if re.search(rb'(?<![A-Za-z0-9_/])/(?:home|Users)/[A-Za-z0-9_.-]+/', data):
        raise ValueError(f'Private home path in {label}')
    lower = data.lower()
    for value in forbidden:
        if value.lower().encode() in lower:
            raise ValueError(f'Forbidden identity text in {label}')


def dependencies(source, temporary):
    target = temporary / 'dependencies.json'
    run(['gclient', 'revinfo', '--actual', '--output-json', str(target)], source.parent)
    snapshot = json.loads(target.read_text())
    if snapshot.get(source.name, {}).get('rev') != REVISION:
        raise ValueError('Dependency snapshot does not identify the pinned source checkout')
    for name, item in snapshot.items():
        if not item.get('rev') or name == source.name:
            continue
        if PurePosixPath(name).is_absolute() or '..' in PurePosixPath(name).parts:
            raise ValueError('Unsafe dependency path')
        repository = source.parent / name
        result = subprocess.run(['git', 'diff', '--quiet', '--ignore-submodules=all', 'HEAD', '--'],
                                cwd=repository, capture_output=True)
        if result.returncode != 0:
            raise ValueError(f'Dirty or unreadable pinned Git dependency: {name}')
        # gclient can pin a nested Git checkout differently from its parent's
        # recorded Gitlink. Accept only overrides present in the actual snapshot;
        # the child's tracked source changes are checked in its own iteration.
        changed = run(['git', 'diff', '--name-only', '-z', '--ignore-submodules=none',
                       'HEAD', '--'], repository).decode().split('\0')
        for child in filter(None, changed):
            entry = run(['git', 'ls-tree', 'HEAD', '--', child], repository).decode().split()
            child_name = name + '/' + child
            expected = snapshot.get(child_name, {}).get('rev')
            if not entry or entry[0] != '160000' or not expected:
                raise ValueError(f'Unrecorded nested dependency change: {child_name}')
            actual = run(['git', 'rev-parse', 'HEAD'], repository / child).decode().strip()
            if actual != expected:
                raise ValueError(f'Nested dependency differs from snapshot: {child_name}')
            snapshot[child_name]['parent_gitlink_revision'] = entry[2]
    return snapshot


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Synchronized and patched Chromium src directory')
    parser.add_argument('--arch', choices=('x86_64', 'arm64'), required=True)
    parser.add_argument('--strip', help='Strip executable (defaults to native or ARM64 cross strip)')
    parser.add_argument('--build-log', type=Path, action='append', default=[], help='Retain a build log hash; repeatable')
    parser.add_argument('--forbid', action='append', default=[], help='Private text forbidden in packaged files; repeatable')
    args = parser.parse_args()
    source = args.source.resolve()
    if run(['git', 'rev-parse', 'HEAD'], source).decode().strip() != REVISION:
        raise ValueError('Chromium checkout is not at the declared revision')
    files, elfs = file_lists()
    patch = ROOT / 'userland/chromium/patches/0001-chromium-ozone-haiku-linux.patch'
    patched_files = validate_patch(source, patch)
    out_name = 'HaikuLinux_x64' if args.arch == 'x86_64' else 'HaikuLinux_arm64_lean'
    output = source / 'out' / out_name
    config = ROOT / 'userland/chromium/patches/args' / f'{args.arch}.gn'
    if config.read_bytes() != (output / 'args.gn').read_bytes():
        raise ValueError('Output GN arguments differ from release GN arguments')
    targets = ['chrome', 'chrome_crashpad_handler', 'libEGL.so', 'libGLESv2.so',
               'libvk_swiftshader.so', 'libffmpeg.so']
    pending = run(['ninja', '-C', str(output), '-n', *targets], source)
    if not pending.rstrip().endswith(b'ninja: no work to do.'):
        raise ValueError('Chromium build is incomplete; no binaries were replaced')
    log = output / '.ninja_log'
    logged = {line.split('\t')[3] for line in log.read_text().splitlines()[1:]
              if len(line.split('\t')) >= 5}
    if not set(elfs).issubset(logged):
        raise ValueError('Ninja log does not contain every packaged ELF output')
    for name in files:
        if not (output / name).is_file():
            raise ValueError(f'Required build output is missing: {name}')
    for name in elfs:
        validate_elf(output / name, args.arch)
    credits = output / 'gen/components/resources/about_credits.html'
    offline_credits = ROOT / 'licenses/chromium/about_credits.html'
    if credits.read_bytes() != offline_credits.read_bytes():
        raise ValueError('Compiled credits differ from the release offline credits')
    strip = args.strip or ('aarch64-linux-gnu-strip' if args.arch == 'arm64' else 'strip')
    runtime = ROOT / 'runtime'
    runtime.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='chromium-package-', dir=runtime) as temporary:
        temporary = Path(temporary)
        snapshot = dependencies(source, temporary)
        staged = temporary / 'new'
        staged.mkdir()
        records = {}
        for name in files:
            original = output / name
            packaged = staged / name
            packaged.parent.mkdir(parents=True, exist_ok=True)
            if name in elfs:
                run([strip, '--strip-all', '-o', str(packaged), str(original)], source)
                validate_elf(packaged, args.arch)
            else:
                shutil.copyfile(original, packaged)
            packaged.chmod(0o755 if name in elfs else 0o644)
            check_private(packaged.read_bytes(), args.forbid, name)
            records[name] = {'source_sha256': digest(original),
                             'source_bytes': original.stat().st_size,
                             'sha256': digest(packaged), 'bytes': packaged.stat().st_size}
        record = {'schema': 1, 'arch': args.arch,
                  'checkout_name': source.name,
                  'recorded_utc': datetime.now(timezone.utc).isoformat(),
                  'source_revision': REVISION, 'patch_sha256': digest(patch),
                  'patched_files': patched_files, 'gn_args_sha256': digest(config),
                  'ninja_version': run(['ninja', '--version'], source).decode().strip(),
                  'ninja_log_sha256': digest(log),
                  'clang_revision': (source / 'third_party/llvm-build/Release+Asserts/cr_build_revision').read_text().strip(),
                  'clang_sha256': digest(source / 'third_party/llvm-build/Release+Asserts/bin/clang'),
                  'strip_version': run([strip, '--version'], source).decode().splitlines()[0],
                  'credits_sha256': digest(credits), 'dependencies': snapshot,
                  'build_logs': [{'name': p.name, 'sha256': digest(p)} for p in args.build_log],
                  'files': records}
        encoded = (json.dumps(record, indent=2, sort_keys=True) + '\n').encode()
        check_private(encoded, args.forbid, 'provenance record')
        destination = ROOT / 'userland/chromium/prebuilt' / args.arch
        existing = {str(p.relative_to(destination)) for p in destination.rglob('*') if p.is_file()}
        if not existing.issubset(files):
            raise ValueError('Destination contains files outside the packaged file list')
        # Prepare everything first, then replace the directory with rollback on failure.
        previous = temporary / 'previous'
        if destination.exists():
            os.replace(destination, previous)
        try:
            os.replace(staged, destination)
        except BaseException:
            if previous.exists():
                os.replace(previous, destination)
            raise
        validation = ROOT / 'docs/validation'
        validation.mkdir(exist_ok=True)
        (validation / f'chromium-build-{args.arch}.json').write_bytes(encoded)
    print(f'PASS: packaged {len(files)} validated Chromium outputs for {args.arch} and recorded provenance')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(f'Packaging refused: {error}') from error
