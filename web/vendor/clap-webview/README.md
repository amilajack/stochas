# CLAP webview/3 header

Only `include/clap/ext/draft/webview.h` is vendored from the public
[CLAP repository](https://github.com/free-audio/clap) at commit
`29ffcc273be7c7c651f6c9953b99e69700e2387a`. The original MIT license is included.

The two relative core-header includes are rewritten to `<clap/plugin.h>` and
`<clap/stream.h>` so Stochas retains its pinned CLAP 1.2.0 core SDK and original
clap-juce-extensions dependency. No other CLAP headers are replaced.
