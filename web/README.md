# Original Stochas WCLAP port

This port runs the original Stochas processor and complete JUCE editor. The DSP
module is pure WASI; a separate browser module renders the original editor and
handles file dialogs, preferences, and XML preset import/export. It requires a
CLAP webview/3 host with shared memory, cross-origin isolation, and the existing
browser file/storage bridge. It does not open browser audio or MIDI devices.

## Build

Initialize the pinned submodules with `git submodule update --init --recursive`.
Install CMake, Python 3, Emscripten **6.0.10**, and Node **24.22.2**. Set
`STOCHAS_EMSDK_ROOT` to that SDK and `STOCHAS_NODE` to the Node executable.
The default SDK path is `~/.cache/daw-plugin-toolchains/emsdk-6.0.10`.

```sh
bash web/scripts/build-module.sh
bash web/scripts/build-ui.sh
bash web/scripts/check-portable.sh
"$STOCHAS_NODE" --test web/tests/module.mjs
```

Both builds use one compiler job and write their Emscripten cache/ports under
`build/`; the SDK is not modified. A populated same-version task cache may be
reused by setting `EM_CACHE`, `EM_PORTS`, and `EM_FROZEN_CACHE=1`.
`web/scripts/prepare-juce.py` creates a build-local overlay from the exact pinned
JUCE source and verifies every patch anchor. It never edits the submodule.

The outputs are `build/wasi-module/wclap/module.wasm` and
`build/browser-ui/ui/`. Serve the UI with COOP/COEP and the host's webview
resource/storage bridge. `ui/index.html` is the webview URI. The GUI uses a
pthread; the DSP does not depend on browser graphics, filesystem pickers, or UI
threads.

## Native and real-time controls

Configure the original native build with `-DSTOCHAS_PORT_TESTS=ON`, then build
`stochas-native-rt` and `stochas-rt-audit`. On macOS, run the former with
`DYLD_INSERT_LIBRARIES` pointing to the latter and an absolute, task-owned
`STOCHAS_CONFIG_ROOT`. Tests never create an editor and prohibit activation.
The audit includes positive allocation/free/mutex controls, then requires none
in actual processor callbacks. Portable prepared queue, random, publication,
delta, state, and file-import tests retain independent correctness assertions.
`stochas-state-convert` converts original JUCE XML state envelopes to/from the
compact runtime representation for authoring and semantic comparisons.

The original scheduler has bounded prepared storage and explicit overload
failure/recovery. Legal editor patterns are preserved; extreme input may exceed
the host's finite event admission. A reported zero plug-in latency is not a
measurement of end-to-end host latency.

## State and UI boundary

```text
original JUCE editor (browser pthread)
  XML presets / note lists / MIDI files / user preferences
  local model + original Undo + field-wise concurrent-edit rebase
             | bounded structural deltas / commands
             v
CLAP webview/3 -> prepared control publication -> original Stochas processor
             ^                                  |
             | compact snapshots / bounded telemetry / parameter feedback
             +----------------------------------+
```

Runtime CLAP state uses versioned little-endian `STCS` compact data with strict
transactional validation and prepared storage capped at 2 MiB. All original
model fields, inactive mappings, disabled cells, notes, patterns, and chains are
retained. This unshipped port does not accept the native XML binary envelope as
runtime state. Original `.stochas` XML files remain available through the
original editor; load those files and save the project to adopt compact state.
Host parameter IDs and original quantized text/value semantics are preserved.

Full-state serialization is not performed on each drag. Ordinary edits use
bounded structural deltas with revisions, acknowledgments, duplicate refusal,
and canonical resynchronization. Heavy XML/file work stays in the editor. The
host still needs to qualify snapshot/copy overlap against real audio deadlines;
functional state equality alone does not establish dropout safety.

## Licensing and provenance

See `JUCE-PORT-NOTICE.md`, the original `COPYING`, JUCE's license, the vendored
webview header notice, and the bundled fonts' OFL notices. The combined browser
build includes JUCE under AGPL-3.0-only; original Stochas remains GPL-3.0-only.
Distribute complete corresponding source, pinned dependency sources, notices,
and build instructions with binary archives. Do not ship private host fixtures
or task profiles as plug-in source.

After qualifying the exact committed source and both built outputs, run
`python3 web/scripts/package.py`. It writes deterministic `binary.tar.gz`,
`source.tar.gz`, and a per-file SHA-256 manifest in `build/wclap-package/`.
The source archive includes every recursive pinned dependency, not Git links.
`--allow-uncommitted` marks local integration artifacts explicitly unreleased;
do not publish those artifacts as a committed release.
