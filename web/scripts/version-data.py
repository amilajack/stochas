#!/usr/bin/env python3
"""Generate reproducible Build metadata, without hostnames or build-directory paths."""
from pathlib import Path
from datetime import datetime, timezone
import json
import os
import re
import subprocess
import sys
root, destination = map(Path, sys.argv[1:])
if (root / '.git').exists():
    revision = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
    source_epoch = subprocess.check_output(['git', '-C', str(root), 'show', '-s', '--format=%ct', 'HEAD'], text=True).strip()
else:
    # Corresponding-source archives deliberately omit Git's mutable database.
    # Their packager records the exact immutable commit/time beside this script;
    # never accidentally inherit an enclosing checkout's unrelated revision.
    source = json.loads((root / 'web/source-version.json').read_text())
    if set(source) != {'schema', 'revision', 'epoch'} or source['schema'] != 1:
        raise ValueError('invalid source archive version metadata')
    revision, source_epoch = source['revision'], str(source['epoch'])
if not isinstance(revision, str) or not re.fullmatch(r'[0-9a-f]{40}', revision):
    raise ValueError('source revision must be a full Git commit')
epoch = os.environ.get('SOURCE_DATE_EPOCH') or source_epoch
if not re.fullmatch(r'[0-9]+', epoch):
    raise ValueError('source epoch must be a nonnegative integer')
when = datetime.fromtimestamp(int(epoch), timezone.utc)
strings = {'MajorVersionStr':'1', 'SubVersionStr':'3', 'ReleaseNumberStr':'10',
    'ReleaseStr':'wclap', 'BuildNumberStr':revision, 'FullVersionStr':'1.3.10.wclap.' + revision,
    'BuildHost':'reproducible', 'BuildArch':'wasm32', 'BuildLocation':'wclap',
    'BuildDate':when.strftime('%Y-%m-%d'), 'BuildTime':when.strftime('%H:%M:%S')}
lines = ['#include "version.h"', 'namespace Stochas {',
    'const int Build::MajorVersionInt = 1;', 'const int Build::SubVersionInt = 3;']
lines += [f'const char* Build::{key} = {json.dumps(value)};' for key, value in strings.items()]
lines += ['}']
content = '\n'.join(lines) + '\n'
if not destination.exists() or destination.read_text() != content:
    destination.write_text(content)
