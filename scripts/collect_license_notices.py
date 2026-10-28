#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Collect an offline Chromium source notice inventory (a source superset)."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('licenses/chromium'))
    parser.add_argument('--revision', required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    parts = [f'Chromium source notice inventory\nDeclared revision: {args.revision}\n\n',
        'This inventory contains license texts from the source tree and pinned dependencies.\n'
        'It is a superset: inclusion does not mean a component is present in the binary.\n'
        'Each component retains its copyright and license terms.\n'
        'See THIRD_PARTY_NOTICES.md for source availability and redistribution details.\n']
    records = []
    for path in sorted(args.source.rglob('*')):
        if not path.is_file() or not re.search(r'license|licence|copying|copyright|notice|credits|authors', path.name, re.I):
            continue
        data = path.read_bytes()
        if not data or b'\0' in data:
            continue
        text = data.decode('utf-8', errors='replace')
        pointer = text.strip()
        if len(data) < 150 and pointer.startswith(('../', './')):
            target = (path.parent / pointer).resolve()
            if target.is_file() and target.is_relative_to(args.source.resolve()):
                text = target.read_text(errors='replace')
            elif 'better_any' in str(path) and 'Apache' in path.name:
                # The vendored upstream file contains a dangling textual pointer.
                text = Path('licenses/system/Apache-2.0').read_text()
            else:
                continue
        name = str(path.relative_to(args.source))
        parts.append(f'\n{"=" * 72}\nSource path: {name}\n{"=" * 72}\n\n{text}\n')
        records.append({'path': name, 'source_file_sha256': hashlib.sha256(data).hexdigest(),
                        'notice_text_sha256': hashlib.sha256(text.encode()).hexdigest()})
    result = ''.join(parts)
    (args.output / 'NOTICE.txt').write_text(result)
    (args.output / 'inventory.json').write_text(json.dumps({'revision': args.revision, 'scope': 'source notice superset', 'files': records}, indent=2) + '\n')
    print(f'Collected {len(records)} notice texts ({len(result.encode())} bytes)')


if __name__ == '__main__':
    main()
