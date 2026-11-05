// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <emscripten.h>
// Only the separate editor exposes browser input/clipboard/accessibility APIs.
// Exporting them from the DSP module roots their DOM dependencies at link time.
#if defined(STOCHAS_WCLAP_DSP)
#define STOCHAS_BROWSER_EXPORT
#else
#define STOCHAS_BROWSER_EXPORT EMSCRIPTEN_KEEPALIVE
#endif
