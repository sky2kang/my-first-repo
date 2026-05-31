#!/usr/bin/env bash
# Build (host) then run the unit tests.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

cmake -S . -B build/host
cmake --build build/host -j"$(nproc 2>/dev/null || echo 4)"
ctest --test-dir build/host --output-on-failure
