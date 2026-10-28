#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Scan the Git index for secrets and private paths before publishing."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--forbid', action='append', default=[], help='Private text forbidden in release files; repeatable')
    args = parser.parse_args()
    paths = subprocess.check_output(['git', 'ls-files', '-z']).decode().split('\0')
    rules = [
        ('private key', rb'-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----'),
        ('GitHub token', rb'\bgh[pousr]_[A-Za-z0-9]{30,}\b'),
        ('GitHub fine-grained token', rb'github_pat_[A-Za-z0-9_]{50,}'),
        ('AWS access key', rb'\b(?:AKIA|ASIA)[A-Z0-9]{16}\b'),
        ('private home path', rb'(?<![A-Za-z0-9_/])/(?:home|Users)/[A-Za-z0-9_.-]+/'),
        ('private workstation path', rb'/usr/local/[^ /]+/home/[^ /]+'),
    ]
    if not any(paths):
        raise SystemExit('The Git index is empty; there is no release to audit')
    failures = []
    for name in filter(None, paths):
        path = Path(name)
        if path.is_symlink():
            failures.append((name, 'symlink must be reviewed'))
            continue
        data = subprocess.check_output(['git', 'show', ':' + name])
        for label, pattern in rules:
            if re.search(pattern, data):
                failures.append((name, label))
        for value in args.forbid:
            if value.lower().encode() in data.lower():
                failures.append((name, 'forbidden private text'))
        if name.endswith('.md') and re.search(rb'/(?:tmp|usr/local)/[^\s`]+', data):
            failures.append((name, 'machine path in Markdown'))
        # Scan materialized LFS payloads as well as the staged pointers.
        if data.startswith(b'version https://git-lfs.github.com/spec/v1\n'):
            payload = path.read_bytes()
            oid = re.search(rb'^oid sha256:([0-9a-f]{64})$', data, re.M)
            size = re.search(rb'^size ([0-9]+)$', data, re.M)
            if (not oid or not size
                    or hashlib.sha256(payload).hexdigest().encode() != oid[1]
                    or len(payload) != int(size[1])):
                failures.append((name, 'LFS payload missing or differs from staged pointer'))
            for label, pattern in rules:
                if re.search(pattern, payload):
                    failures.append((name, label + ' in LFS payload'))
            for value in args.forbid:
                if value.lower().encode() in payload.lower():
                    failures.append((name, 'forbidden text in LFS payload'))
    for name, reason in failures:
        print(f'FAIL {name}: {reason}')
    if failures:
        raise SystemExit(1)
    print(f'PASS: scanned {len(list(filter(None, paths)))} staged files and LFS payloads')
    print('Pattern scans supplement manual review; they do not prove the absence of every possible secret.')


if __name__ == '__main__':
    main()
