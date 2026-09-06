#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
out=$(mktemp -d "${TMPDIR:-/tmp}/daedalus-tests.XXXXXX")
trap 'rm -rf "$out"' EXIT
"${CXX:-clang++}" -std=c++17 -O1 -g -Wall -Wextra -pthread \
  -fsanitize="${SANITIZERS:-address,undefined}" -fno-omit-frame-pointer \
  -DDAEDALUS_CTR -DDAEDALUS_ENDIAN_LITTLE=1 -DDAEDALUS_ENDIAN_BIG=2 -DDAEDALUS_ENDIAN_MODE=1 \
  -I"$repo/Source" "$repo/tests/stability_tests.cpp" \
  "$repo/Source/SysCTR/HLEAudio/AudioBufferCTR.cpp" -o "$out/stability_tests"
"$out/stability_tests" "$out/data"
