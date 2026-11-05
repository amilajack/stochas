#!/usr/bin/env python3
"""Deterministic binary + complete public corresponding-source archives.

Package only named outputs and Git source files, never build caches or private
host fixtures. Run after building and checking the exact source being packaged.
"""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[2]


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def sha(data):
    return hashlib.sha256(data).hexdigest()


def archive(destination, files):
    with destination.open('wb') as output:
        with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0) as zipped:
            with tarfile.open(fileobj=zipped, mode='w', format=tarfile.PAX_FORMAT) as tar:
                for name, source in sorted(files.items()):
                    data = source if isinstance(source, bytes) else source.read_bytes()
                    info = tarfile.TarInfo(name)
                    info.size = len(data)
                    info.mode = 0o755 if isinstance(source, Path) and source.stat().st_mode & 0o111 else 0o644
                    info.mtime = info.uid = info.gid = 0
                    tar.addfile(info, io.BytesIO(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--allow-uncommitted', action='store_true',
                        help='local qualification only; mark artifacts unreleased')
    args = parser.parse_args()
    dirty = bool(git('status', '--porcelain').strip())
    if dirty and not args.allow_uncommitted:
        parser.error('commit the qualified source first, or use --allow-uncommitted for local tests')
    submodules = git('submodule', 'status', '--recursive').decode().splitlines()
    if any(not line.startswith(' ') for line in submodules):
        parser.error('initialize and preserve every pinned dependency before packaging')

    # Git traverses nested submodules at their current checked-out source pins.
    names = set(git('ls-files', '--recurse-submodules', '-z').decode().split('\0'))
    names.update(git('ls-files', '--others', '--exclude-standard', '-z').decode().split('\0'))
    sources = {}
    for name in sorted(names - {''}):
        path = ROOT / name
        if path.is_symlink():
            raise ValueError('source symlink needs explicit packaging review: ' + name)
        if path.is_file():
            sources['stochas-source/' + name] = path
    source_revision = git('rev-parse', 'HEAD').decode().strip()
    epoch = os.environ.get('SOURCE_DATE_EPOCH') or git('show', '-s', '--format=%ct', 'HEAD').decode().strip()
    if not epoch.isdecimal():
        raise ValueError('SOURCE_DATE_EPOCH must be a nonnegative integer')
    sources['stochas-source/web/source-version.json'] = json.dumps(
        dict(schema=1, revision=source_revision, epoch=int(epoch)), sort_keys=True).encode() + b'\n'
    source_hashes = {name: sha(path if isinstance(path, bytes) else path.read_bytes())
                     for name, path in sources.items()}
    source_digest = sha(json.dumps(source_hashes, sort_keys=True, separators=(',', ':')).encode())
    source_metadata = json.dumps(dict(commit=source_revision, uncommitted=dirty,
                                     treeDigest=source_digest, files=source_hashes),
                                 sort_keys=True, indent=2).encode() + b'\n'
    sources['stochas-source/SOURCE-MANIFEST.json'] = source_metadata

    files = {'module.wasm': ROOT / 'build/wasi-module/wclap/module.wasm'}
    ui = ROOT / 'build/browser-ui/ui'
    for path in sorted(ui.rglob('*')):
        if path.is_file():
            files['ui/' + path.relative_to(ui).as_posix()] = path
    for required in ('ui/index.html', 'ui/stochas-ui.js', 'ui/stochas-ui.wasm',
                     'ui/stochas-ui.data', 'ui/support.js'):
        if required not in files:
            raise ValueError('missing original editor output: ' + required)
    if not dirty:
        for name in ('module.wasm', 'ui/stochas-ui.wasm'):
            if source_revision.encode() not in files[name].read_bytes():
                raise ValueError('rebuild from the current committed source before packaging: ' + name)
    notices = {'COPYING': ROOT / 'COPYING', 'AGPL-3.0.txt': ROOT / 'web/licenses/AGPL-3.0.txt', 'JUCE-LICENSE.md': ROOT / 'lib/JUCE/LICENSE.md',
               'JUCE-PORT-NOTICE.md': ROOT / 'web/JUCE-PORT-NOTICE.md',
               'WEBVIEW-LICENSE': ROOT / 'web/vendor/clap-webview/LICENSE',
               'Lato-OFL.txt': ROOT / 'web/fonts/OFL.txt',
               'BUILD.md': ROOT / 'web/README.md'}
    files.update({'notices/' + name: path for name, path in notices.items()})
    manifest = dict(id='org.surge-synth-team.stochas', license='AGPL-3.0-only',
                    sourceCommit=source_revision, sourceTreeDigest=source_digest,
                    sourceDateEpoch=int(epoch),
                    uncommitted=dirty, dependencies=submodules,
                    files={name: dict(bytes=path.stat().st_size, sha256=sha(path.read_bytes()))
                           for name, path in sorted(files.items())})
    files['manifest.json'] = json.dumps(manifest, sort_keys=True, indent=2).encode() + b'\n'
    output = ROOT / 'build/wclap-package'
    output.mkdir(parents=True, exist_ok=True)
    archive(output / 'source.tar.gz', sources)
    archive(output / 'binary.tar.gz', files)
    (output / 'manifest.json').write_bytes(files['manifest.json'])
    # Detect accidental edits while reading the corresponding source.
    if any(sha(path.read_bytes()) != source_hashes[name]
           for name, path in sources.items() if isinstance(path, Path)):
        raise RuntimeError('source changed while packaging; refuse these artifacts')
    print(json.dumps(dict(output=str(output), uncommitted=dirty,
                          sourceTreeDigest=source_digest, files=len(files),
                          binarySHA256=sha((output / 'binary.tar.gz').read_bytes()),
                          sourceSHA256=sha((output / 'source.tar.gz').read_bytes()))))


if __name__ == '__main__':
    main()
