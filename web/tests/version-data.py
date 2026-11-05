#!/usr/bin/env python3
"""Reproducible metadata controls for the corresponding-source archive."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/version-data.py'


class SourceArchiveVersion(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'web').mkdir()
        self.metadata = {'schema': 1, 'revision': 'a' * 40, 'epoch': 0}
        self.output = self.root / 'version.cpp'

    def run_generator(self, epoch=None):
        (self.root / 'web/source-version.json').write_text(json.dumps(self.metadata))
        environment = os.environ.copy()
        environment.pop('SOURCE_DATE_EPOCH', None)
        if epoch is not None:
            environment['SOURCE_DATE_EPOCH'] = epoch
        return subprocess.run([sys.executable, str(SCRIPT), str(self.root), str(self.output)],
                              env=environment, capture_output=True, text=True)

    def test_archive_has_exact_commit_time_and_stable_output(self):
        self.assertEqual(self.run_generator().returncode, 0)
        content = self.output.read_bytes()
        modified = self.output.stat().st_mtime_ns
        self.assertIn(('1.3.10.wclap.' + 'a' * 40).encode(), content)
        self.assertIn(b'1970-01-01', content)
        self.assertEqual(self.run_generator().returncode, 0)
        self.assertEqual(self.output.read_bytes(), content)
        self.assertEqual(self.output.stat().st_mtime_ns, modified)

    def test_standard_source_date_epoch_override(self):
        self.assertEqual(self.run_generator('86400').returncode, 0)
        self.assertIn('1970-01-02', self.output.read_text())

    def test_invalid_commit_or_epoch_never_writes_metadata(self):
        for key, value in [('revision', 'main'), ('epoch', -1), ('schema', 2)]:
            original = self.metadata[key]
            self.metadata[key] = value
            self.assertNotEqual(self.run_generator().returncode, 0)
            self.assertFalse(self.output.exists())
            self.metadata[key] = original
        self.assertNotEqual(self.run_generator('not-a-timestamp').returncode, 0)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
