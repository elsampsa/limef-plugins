#!/bin/bash
#
# Run from the build directory:
#   mkdir build && cd build
#   ../run_cmake.bash
#   make -j$(nproc)
#
# Prerequisites:
#   1. Build libLimef.so:     cd limef && make -j$(nproc)
#   2. Stage limef:           cd limef && ./staging.bash
#   3. Build the plugin:      cd plugins/basler && mkdir build && cd build && cmake .. && make
#   4. Stage the plugin:      cd plugins/basler && ./staging.bash
#   5. Build these apps:      (you are here)
#
# LIMEF_PREFIX   — where limef + plugin headers/libs live (default: ~/limef-stage)
# PYLON_ROOT     — Pylon SDK root (default: /opt/pylon)
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIMEF_PREFIX="${LIMEF_PREFIX:-$HOME/limef-stage}"
PYLON_ROOT="${PYLON_ROOT:-/opt/pylon}"

BUILD_TYPE="Debug"
# BUILD_TYPE="Release"

echo
echo "LimefBasler Apps - CMake Configuration"
echo "======================================="
echo "LIMEF_PREFIX:  $LIMEF_PREFIX"
echo "PYLON_ROOT:    $PYLON_ROOT"
echo "Source dir:    $SCRIPT_DIR"
echo

cmake \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DLIMEF_PREFIX=$LIMEF_PREFIX \
    -DPYLON_ROOT=$PYLON_ROOT \
    $SCRIPT_DIR

echo
echo "Run 'make -j\$(nproc)' to build."
echo "Run with:"
echo "  PYLON_CAMEMU=1 LD_LIBRARY_PATH=$LIMEF_PREFIX/lib:$PYLON_ROOT/lib ./bin/basler_pipeline"
echo
