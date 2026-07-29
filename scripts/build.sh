#!/usr/bin/env bash
# Build the O-RU software for the host (simulation) by default.
#   ./scripts/build.sh            # host/SIM build + tests
#   ORU_TARGET=ON ./scripts/build.sh   # cross build (needs toolchain file)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TARGET="${ORU_TARGET:-OFF}"

if [ "$TARGET" = "ON" ]; then
    BUILD_DIR="build/target"
    EXTRA=(-DORU_TARGET=ON)
    if [ -n "${ORU_TOOLCHAIN:-}" ]; then
        EXTRA+=("-DCMAKE_TOOLCHAIN_FILE=${ORU_TOOLCHAIN}")
    fi
else
    BUILD_DIR="build/host"
    EXTRA=()
fi

echo ">> configuring ($BUILD_DIR)"
cmake -S . -B "$BUILD_DIR" "${EXTRA[@]}"

echo ">> building"
cmake --build "$BUILD_DIR" -j"$(nproc 2>/dev/null || echo 4)"

echo ">> done. binary: $BUILD_DIR/src/app/oru_app"
