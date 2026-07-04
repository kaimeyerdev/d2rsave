#!/usr/bin/env bash
# One-time vcpkg bootstrap. Clones vcpkg into ./.vcpkg and builds the manifest deps.
# After this runs, `cmake --preset debug` will use it via CMakePresets.json.
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
echo "Next: cmake --preset debug && cmake --build --preset debug"
