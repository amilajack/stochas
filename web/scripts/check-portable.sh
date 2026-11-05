#!/bin/bash
# Small independent controls; assertions remain enabled in optimized builds.
set -euo pipefail
port_root="$(cd "$(dirname "$0")/../.." && pwd)"
output="$port_root/build/portable-tests"
mkdir -p "$output"
compiler="${CXX:-c++}"
"${CC:-cc}" -O2 -c "$port_root/src/pcg_basic.c" -o "$output/pcg.o"
for test in prepared-midi-queue prepared-triple-buffer protocol pcg-advance seq-random; do
  "$compiler" -std=c++17 -O2 -UNDEBUG -pthread \
    -I"$port_root/src" -I"$port_root/web/wclap" \
    "$port_root/web/tests/$test.cpp" "$output/pcg.o" -o "$output/$test"
  "$output/$test"
done
python3 "$port_root/web/tests/version-data.py"
