#!/bin/bash
# Original editor browser build. The pinned shared SDK/cache remains read-only.
set -euo pipefail
port_root="$(cd "$(dirname "$0")/../.." && pwd)"
stochas_sdk="${STOCHAS_EMSDK_ROOT:-$HOME/.cache/daw-plugin-toolchains/emsdk-6.0.10}"
if [[ "$(cat "$stochas_sdk/upstream/emscripten/emscripten-version.txt")" != '"6.0.10"' ]]; then
  echo 'Stochas browser build requires the pinned Emscripten6.0.10 SDK' >&2
  exit 1
fi
stochas_node="${STOCHAS_NODE:-$(command -v node)}"
if [[ "$("$stochas_node" -p 'process.versions.node')" != '24.22.2' ]]; then
  echo 'Stochas browser verification requires Node24.22.2 (set STOCHAS_NODE)' >&2
  exit 1
fi
export EM_NODE_JS="$stochas_node"
export EM_CACHE="${EM_CACHE:-$port_root/build/emscripten-cache}"
export EM_PORTS="${EM_PORTS:-$port_root/build/emscripten-ports}"
export EM_FROZEN_CACHE="${EM_FROZEN_CACHE:-0}"
export EMCC_CORES=1
export CMAKE_BUILD_PARALLEL_LEVEL=1
"$stochas_sdk/upstream/emscripten/emcmake" cmake -S "$port_root/web" -B "$port_root/build/browser-ui" -DCMAKE_BUILD_TYPE=Release
cmake --build "$port_root/build/browser-ui" --target stochas-ui --parallel 1
python3 "$port_root/web/scripts/assemble-ui.py" "$port_root/build/browser-ui/ui"
