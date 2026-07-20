#!/usr/bin/env bash
# One-time vcpkg setup. Clones microsoft/vcpkg into ./.vcpkg and builds the
# vcpkg binary. Manifest deps are installed later by `cmake --preset Debug`.
# (`bootstrap-vcpkg.sh` below is vcpkg's own internal script, not this one.)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VCPKG_DIR="$SCRIPT_DIR/.vcpkg"

if [[ ! -d "$VCPKG_DIR/.git" ]]; then
    echo "Cloning vcpkg into $VCPKG_DIR..."
    git clone --depth 1 https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
fi

if [[ ! -x "$VCPKG_DIR/vcpkg" ]]; then
    echo "Bootstrapping vcpkg..."
    "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
fi

echo "vcpkg ready at $VCPKG_DIR"
echo "Next: cmake --preset Debug && cmake --build --preset Debug"
