#!/usr/bin/env python3
"""Generate native Stochas XML fixtures in JUCE's documented binary envelope.
Every field is read by the original SeqPersist; the native round-trip is the oracle.
"""
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET
output = Path(sys.argv[1]); output.mkdir(parents=True, exist_ok=True)
for name, rows in [('single', 1), ('dense-128', 128), ('last-sample', 1)]:
    root = ET.Element('stochas', version='1')
    ET.SubElement(root, 'seed', val='42')
    ET.SubElement(root, 'autoplay', val='0')
    layers = ET.SubElement(root, 'layer')
    layer = ET.SubElement(layers, 'l', idx='0', name=name)
    for key, value in [('mono', 0), ('numrows', 129), ('maxpoly', rows), ('notecust', 1), ('dcycle', 50)]:
        ET.SubElement(layer, key, val=str(value))
    notes = ET.SubElement(layer, 'notes')
    pattern = ET.SubElement(ET.SubElement(layer, 'pats'), 'p', idx='0')
    sequence_rows = ET.SubElement(pattern, 'rows')
    for index in range(rows):
        row = 127 - index
        note = 60 if rows == 1 else index
        ET.SubElement(notes, 'n', idx=str(row), std=str(note), cust=str(note))
        cells = ET.SubElement(ET.SubElement(sequence_rows, 'r', idx=str(row)), 'cells')
        ET.SubElement(cells, 'c', idx='1' if name == 'last-sample' else '0', prob='100', velo='100', len='0', offs='0')
    xml = ET.tostring(root, encoding='utf-8')
    (output / f'{name}.state').write_bytes(struct.pack('<II', 0x21324356, len(xml)) + xml + b'\0')
