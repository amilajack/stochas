# Browser platform source provenance

The named files in `web/juce/` and `web/scripts/prepare-juce.py` are adapted from
the public [Surge browser port](https://github.com/amilajack/surge), commit
`8def5147f58d4b22dfee91b8aa72611412339273`. Original SPDX license declarations
are retained. Changes adapt the platform namespace and pinned JUCE 8.0.6
source anchors to Stochas; the upstream JUCE submodule remains unchanged.

These files provide browser windowing, events, fonts, file dialogs, clipboard,
text input, and accessibility for the original Stochas editor. They contain no
replacement sequencer or MIDI scheduling implementation.

The Lato regular, bold, italic, and bold italic fonts in `web/fonts/` are from
the same public source commit. Their SIL Open Font License and original notice
are included. They supply JUCE's default sans-serif face in the browser.

`web/wclap/Wasi.{cpp,h}` and protocol framing are
also adapted from that same public commit. The WASI shim retains the qualified
finite user-file rights fix and never grants rights beyond its host preopens.

The generic webview page bootstrap and thin CLAP/WASI test bindings are also adapted from the same public Surge source pin. Named Noto fallback fonts and their OFL notices come from its resources/fonts-fallback directory; they load only when original text rendering needs them.
