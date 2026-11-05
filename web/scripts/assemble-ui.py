#!/usr/bin/env python3
"""Assemble only the original editor's named browser runtime inputs."""
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parents[1]
output = Path(sys.argv[1])
output.mkdir(parents=True, exist_ok=True)
scripts = ['runtime.js', 'reports.js', 'platform.js', 'clipboard.js',
           'text-input.js', 'accessibility.js']
support = '\n'.join((root / 'browser' / name).read_text() for name in scripts)
destination = output / 'support.js'
if not destination.exists() or destination.read_text() != support:
    destination.write_text(support)
for source in [root / 'ui/index.html', *(root / 'fonts-fallback').glob('*')]:
    destination = output / ('fonts-fallback/' + source.name if source.parent.name == 'fonts-fallback' else source.name)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if not destination.exists() or destination.read_bytes() != source.read_bytes():
        shutil.copyfile(source, destination)
