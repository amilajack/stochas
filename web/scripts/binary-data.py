#!/usr/bin/env python3
"""Embed the same original resources as the native CMake assets target."""
from pathlib import Path
import sys
root, output = map(Path, sys.argv[1:])
files = ['image/cog.png', 'image/exclamation-64.png', 'image/not.png',
         'image/play.png', 'image/play2.png', 'image/logo_no_bg.png',
         'image/x-mark-4-64.png', 'text/infobox.txt']
output.mkdir(parents=True, exist_ok=True)
header = ['// Generated from the original Stochas assets.\n#pragma once\nnamespace SeqImageX {']
source = ['#include "BinaryData.h"\nnamespace SeqImageX {']
for file in files:
    name = ''.join(c if c.isalnum() else '_' for c in Path(file).name.replace('-', ''))
    data = (root / file).read_bytes()
    header += [f'extern const char* {name};', f'constexpr int {name}Size = {len(data)};']
    source += [f'static const unsigned char {name}_data[] = {{',
               ','.join(str(b) for b in data) + ',0};',
               f'const char* {name} = reinterpret_cast<const char*>({name}_data);']
header += ['}']; source += ['}']
for name, lines in [('BinaryData.h', header), ('BinaryData.cpp', source)]:
    content = '\n'.join(lines) + '\n'
    destination = output / name
    if not destination.exists() or destination.read_text() != content:
        destination.write_text(content)
