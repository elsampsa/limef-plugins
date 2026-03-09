#!/bin/bash
# Extend the limef staging area with the LimefBasler plugin.
# Run this after building the plugin AND after running the main staging.bash.
#
# Usage:
#   ./staging.bash              # uses build/ (default build dir)
#   ./staging.bash build_debug  # uses an alternative build dir
#
# Reads LIMEF_STAGE from the environment, defaulting to ~/limef-stage.

if [ "${1}" = "--help" ] || [ "${1}" = "-h" ]; then
    cat <<EOF
Usage: ./staging.bash [BUILD_DIR]

Extend ~/limef-stage/ (or \$LIMEF_STAGE) with the LimefBasler plugin headers
and library, so that plugin apps can build against the live source/build tree.

  BUILD_DIR   plugin build subdirectory to use (default: build)

Run the main limef staging.bash first, then this script.

After running:
  cd apps/cpp && mkdir build && cd build
  ../run_cmake.bash
  make -j\$(nproc)
  PYLON_CAMEMU=1 LD_LIBRARY_PATH=\$HOME/limef-stage/lib:/opt/pylon/lib ./bin/basler_pipeline
EOF
    exit 0
fi

set -e

PLUGIN_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${1:-build}"
BUILD_PATH="$PLUGIN_ROOT/$BUILD_DIR"
STAGING="${LIMEF_STAGE:-$HOME/limef-stage}"

if [ ! -d "$STAGING/include/limef" ]; then
    echo "Error: $STAGING/include/limef not found"
    echo "Run the main limef staging.bash first."
    exit 1
fi

if [ ! -f "$BUILD_PATH/lib/libLimefBasler.so" ]; then
    echo "Error: $BUILD_PATH/lib/libLimefBasler.so not found — build the plugin first"
    exit 1
fi

# Plugin headers: src/ → include/limef/basler/
ln -sfn "$PLUGIN_ROOT/src"                    "$STAGING/include/limef/basler"

# Plugin library
ln -sfn "$BUILD_PATH/lib/libLimefBasler.so"   "$STAGING/lib/libLimefBasler.so"

echo "LimefBasler plugin staged at $STAGING  (build: $BUILD_DIR)"
echo "  Headers: $STAGING/include/limef/basler → $PLUGIN_ROOT/src"
echo "  Library: $STAGING/lib/libLimefBasler.so"
echo "  export LD_LIBRARY_PATH=$STAGING/lib:/opt/pylon/lib:\$LD_LIBRARY_PATH"
