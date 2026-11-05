# Stochas browser/WCLAP qualification

The complete original processor and editor now build on their original pinned
JUCE/CLAP dependencies. No shipping catalog pin is qualified yet.

Verified locally on ARM64 macOS and headless Chrome:

- Actual native processor callback audit:128-note chords,1/512-frame blocks,
  stop/reset/restart, retained automation, recorded-note FIFO, and audition
  lifecycle. Allocation/free/mutex positive controls fire; actual callbacks
  require zero such operations. Explicit bounded overload replaces the original
 48-event queue's reproduced silent loss.
- Prepared publication/Undo ownership, independent PCG random-access equality,
  strict structural deltas and concurrent field rebase, original note/MIDI
  parsers, and all-field compact state validation. The dense model retains all
 264192 cells, chains, disabled cells, labels, and inactive mapping slots.
- Actual WASI module imports only WASI, negotiates zero audio buses and MIDI1
  ports, exposes all55 native parameter IDs/text/value semantics, and matches
  native fixed-point event offsets exactly with128/512-frame blocks.
- Six authored song states roundtrip all original native fields and produce
  byte-exact native MIDI arrays in the actual module. Linear memory remains
 32 MiB through create/load/save/process in those functional cases.
- Actual original editor in an opaque cross-origin-isolated app sandbox: readable
 2x rendering, real cell interaction sends a structural delta to DSP, original
  Undo restores exact DSP state, and original preset export survives picker
  cancellation/retry through a real OPFS file handle. Exported XML reloaded by
  actual native SeqPersist converts to byte-exact original compact state.

Remaining release qualification includes the actual shared-owner/module-lock
state-save/load overlap and callback deadline fixture, matched callback/startup
cost controls, broader app editor lifecycle and complete song/export checks,
final committed-source archives, and provider/catalog availability. A standalone
encoder benchmark, reported zero latency, or functional MIDI equality does not
substitute for these checks. No real-host performance/dropout claim is made.
